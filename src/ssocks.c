/*
 *      ssocks.c
 *      
 *      Created on: 2011-03-31
 *      Author:     Hugo Caron
 *      Email:      <h.caron@codsec.com>
 * 
 * Copyright (C) 2011 by Hugo Caron
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "bor-util.h"
#include "bor-timer.h"
#include "net-util.h"
#include "output-util.h"

#include "socks5-client.h"
#include "socks5-server.h"
#include "socks5-common.h"

#include <config.h>
#include <getopt.h>


#define PORT 1080

struct globalArgs_t {
	char *host;				// -h option
	unsigned int port;		// -p option
	unsigned int listen;	// -l option
	unsigned int verbosity;	// -v
	unsigned int background;// -b

	char *uname;			// -u option
	char *passwd;			// -p option

	char *sockshost;		// -s host:port
	int socksport;
} globalArgs;

int boucle_princ = 1;
void capte_fin (int sig){
    TRACE(L_VERBOSE, "server: signal %d caught\n", sig);
    boucle_princ = 0;
}
/*
 * TODO: Bind localhost not 0.0.0.0
 */
void usage(char *name){
	printf("sSocks Socks5 Server Relay v%s\n", PACKAGE_VERSION);
	printf("Run a socks server on your localhost interface, and\n");
	printf("relay all data to the server specified in --socks\n");
	printf("Used to bypass browser limitation with authentication\n");
	printf("Usage:\n");
	printf("\t%s --socks socksserv.com:1080\n", name);
	printf("\t%s --socks localhost:1080 --listen 1088\n", name);
	printf("\t%s --socks socksserv.com:1080 --uname admin --passwd abcde\n", name);
	printf("\t%s -s socksserv.com:1080 -u admin -p abcde -l 1080 -b\n", name);
	printf("Options:\n");
	printf("\t--verbose (increase verbose level)\n\n");
	printf("\t--socks {host:port}\n");
	printf("\t--uname {uname}\n");
	printf("\t--passwd {passwd}\n");
	printf("\t--listen {port}\n");
	printf("\t--background\n");
	printf("\n");
	printf("Bug report %s\n", PACKAGE_BUGREPORT);
}

void server(char *sockshost, int socksport, int port,
		char *uname, char *passwd){
    int soc_ec = -1, maxfd, res, nc;  
    Client tc[MAXCLI]; 
    ConfigDynamic config;

    config.host = sockshost;
    config.port = socksport;
    config.uname = uname;
    config.passwd = passwd;

    fd_set set_read;
    fd_set set_write;
    
    /* Init client tab */
    for (nc = 0; nc < MAXCLI; nc++) init_client (&tc[nc], nc, M_DYNAMIC, &config);
    
    soc_ec = new_listen_socket (port, MAXCLI); 
    if (soc_ec < 0) goto fin_serveur;
    
	if ( globalArgs.background == 1 ){
		TRACE(L_NOTICE, "server: background ...");
		if ( daemon(0, 0) != 0 ){
			perror("daemon");
			exit(1);
		}
	}

    bor_signal (SIGINT, capte_fin, SA_RESTART);
    
    while (boucle_princ) {
        init_select_dynamic (soc_ec, tc, &maxfd, &set_read, &set_write);
        
        res = select (maxfd+1, &set_read, &set_write, NULL, bor_timer_delay());

        if (res > 0) {  /* Search eligible sockets */
            if (FD_ISSET (soc_ec, &set_read))
                if (new_connection (soc_ec, tc) < 0) goto fin_serveur;
            
            for (nc = 0; nc < MAXCLI; nc++){
				if ( tc[nc].state != E_WAIT ){
					if (tc[nc].soc != -1 && FD_ISSET (tc[nc].soc, &set_read))
						dispatch_server_read (&tc[nc]);
					else if (tc[nc].soc != -1 && FD_ISSET (tc[nc].soc, &set_write))
						dispatch_server_write (&tc[nc]);
                }
                    
				if (tc[nc].soc_stream != -1 && FD_ISSET (tc[nc].soc_stream, &set_read)){
					dispatch_client(&tc[nc]);
				}else if(tc[nc].soc_stream != -1 && FD_ISSET (tc[nc].soc_stream, &set_write)){
					dispatch_client(&tc[nc]);
				}
			}
                
        } else if ( res == 0){
            /* If timeout was set in select and expired */
        }else if (res < 0) { 
            if (errno == EINTR) ;  /* Received signal, it does nothing */
            else { perror ("select"); goto fin_serveur; }
        }
    }   

fin_serveur: 
    printf ("Server: closing sockets ...\n");

    if (soc_ec != -1) close (soc_ec); 
    for (nc = 0; nc < MAXCLI; nc++) raz_client (&tc[nc]);
}


void parseArg(int argc, char *argv[]){
	memset(&globalArgs, 0, sizeof(globalArgs));

	int c;
	while (1){
		static struct option long_options[] = {
			{"help",    no_argument,       0, 'h'},
			{"verbose", no_argument,       0, 'v'},
			{"background", no_argument,    0, 'b'},
			{"socks",   required_argument, 0, 's'},
			{"uname",   required_argument, 0, 'u'},
			{"passwd",  required_argument, 0, 'p'},
			{"listen",  required_argument, 0, 'l'},
			{0, 0, 0, 0}
		};

		/* getopt_long stores the option index here. */
		int option_index = 0;

		c = getopt_long (argc, argv, "h?bvs:u:p:l:",
					long_options, &option_index);

		/* Detect the end of the options. */
		if (c == -1)
			break;

		char *port;

		switch (c)	{
			case 0:
				/* If this option set a flag, do nothing else now. */
				if (long_options[option_index].flag != 0)
					break;
				printf ("option %s", long_options[option_index].name);
				if (optarg)
					printf (" with arg %s", optarg);
				printf ("\n");
				break;

			case 'v':
				//globalArgs.verbosity++;
				verbosity++;
				break;

			case 'b':
				globalArgs.background = 1;
				break;

			case 's':

				port = strchr(optarg, ':');
				if ( port == NULL ){
					usage(argv[0]);
					exit(1);
				}
				*port = 0; port++;
				globalArgs.sockshost = optarg;
				globalArgs.socksport = atoi(port);
				/*printf("Connect trought socks %s:%d\n",
					globalArgs.sockshost, globalArgs.socksport);*/
				break;

			case 'u':
				/* printf("Username: %s\n", optarg); */
				globalArgs.uname = optarg;
				break;

			case 'p':
				/* printf("Passwd: %s\n", optarg); */
				globalArgs.passwd = optarg;
				break;

			case 'l':
				/* printf("Listening on port: %d\n", atoi(optarg)); */
				globalArgs.listen = atoi(optarg);
				break;

			case '?':
				/* getopt_long already printed an error message. */
				usage(argv[0]);
				exit(1);
				break;

			case 'h':
				usage(argv[0]);
				exit(1);
				break;

			default:
				abort ();
		}
	}

	if ( globalArgs.sockshost == NULL || globalArgs.socksport == 0 ){
		usage(argv[0]);
		exit(1);
	}
}


int main (int argc, char *argv[]){
	parseArg(argc, argv);
	server(globalArgs.sockshost, globalArgs.socksport, globalArgs.listen,
			globalArgs.uname, globalArgs.passwd);
	exit(0);
}
