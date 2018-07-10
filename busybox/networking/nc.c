/* vi: set sw=4 ts=4: */
/*
 * nc: mini-netcat - built from the ground up for LRP
 *
 * Copyright (C) 1998, 1999  Charles P. Wright
 * Copyright (C) 1998  Dave Cinege
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config NC
//config:	bool "nc (11 kb)"
//config:	default y
//config:	help
//config:	A simple Unix utility which reads and writes data across network
//config:	connections.
//config:
//config:config NETCAT
//config:	bool "netcat (11 kb)"
//config:	default n
//config:	help
//config:	Alias to nc.
//config:
//config:config NC_SERVER
//config:	bool "Netcat server options (-l)"
//config:	default y
//config:	depends on NC || NETCAT
//config:	help
//config:	Allow netcat to act as a server.
//config:
//config:config NC_EXTRA
//config:	bool "Netcat extensions (-eiw and -f FILE)"
//config:	default y
//config:	depends on NC || NETCAT
//config:	help
//config:	Add -e (support for executing the rest of the command line after
//config:	making or receiving a successful connection), -i (delay interval for
//config:	lines sent), -w (timeout for initial connection).
//config:
//config:config NC_110_COMPAT
//config:	bool "Netcat 1.10 compatibility (+2.5k)"
//config:	default y
//config:	depends on NC || NETCAT
//config:	help
//config:	This option makes nc closely follow original nc-1.10.
//config:	The code is about 2.5k bigger. It enables
//config:	-s ADDR, -n, -u, -v, -o FILE, -z options, but loses
//config:	busybox-specific extensions: -f FILE.

//applet:IF_NC(APPLET(nc, BB_DIR_USR_BIN, BB_SUID_DROP))
//                 APPLET_ODDNAME:name    main location        suid_type     help
//applet:IF_NETCAT(APPLET_ODDNAME(netcat, nc,  BB_DIR_USR_BIN, BB_SUID_DROP, nc))

//kbuild:lib-$(CONFIG_NC) += nc.o
//kbuild:lib-$(CONFIG_NETCAT) += nc.o

#include "libbb.h"
#include "common_bufsiz.h"
#if ENABLE_NC_110_COMPAT
# include "nc_bloaty.c"
#else

//usage:#if !ENABLE_NC_110_COMPAT
//usage:
//usage:#if ENABLE_NC_SERVER || ENABLE_NC_EXTRA
//usage:#define NC_OPTIONS_STR "\n"
//usage:#else
//usage:#define NC_OPTIONS_STR
//usage:#endif
//usage:
//usage:#define nc_trivial_usage
//usage:	IF_NC_EXTRA("[-iN] [-wN] ")IF_NC_SERVER("[-l] [-p PORT] ")
//usage:       "["IF_NC_EXTRA("-f FILE|")"IPADDR PORT]"IF_NC_EXTRA(" [-e PROG]")
//usage:#define nc_full_usage "\n\n"
//usage:       "Open a pipe to IP:PORT" IF_NC_EXTRA(" or FILE")
//usage:	NC_OPTIONS_STR
//usage:	IF_NC_SERVER(
//usage:     "\n	-l	Listen mode, for inbound connects"
//usage:	IF_NC_EXTRA(
//usage:     "\n		(use -ll with -e for persistent server)"
//usage:	)
//usage:     "\n	-p PORT	Local port"
//usage:	)
//usage:	IF_NC_EXTRA(
//usage:     "\n	-w SEC	Connect timeout"
//usage:     "\n	-i SEC	Delay interval for lines sent"
//usage:     "\n	-f FILE	Use file (ala /dev/ttyS0) instead of network"
//usage:     "\n	-e PROG	Run PROG after connect"
//usage:	)
//usage:
//usage:#define nc_notes_usage ""
//usage:	IF_NC_EXTRA(
//usage:       "To use netcat as a terminal emulator on a serial port:\n\n"
//usage:       "$ stty 115200 -F /dev/ttyS0\n"
//usage:       "$ stty raw -echo -ctlecho && nc -f /dev/ttyS0\n"
//usage:	)
//usage:
//usage:#define nc_example_usage
//usage:       "$ nc foobar.somedomain.com 25\n"
//usage:       "220 foobar ESMTP Exim 3.12 #1 Sat, 15 Apr 2000 00:03:02 -0600\n"
//usage:       "help\n"
//usage:       "214-Commands supported:\n"
//usage:       "214-    HELO EHLO MAIL RCPT DATA AUTH\n"
//usage:       "214     NOOP QUIT RSET HELP\n"
//usage:       "quit\n"
//usage:       "221 foobar closing connection\n"
//usage:
//usage:#endif

/* Lots of small differences in features
 * when compared to "standard" nc
 */

static void timeout(int signum UNUSED_PARAM)
{
	bb_error_msg_and_die("timed out");
}

int nc_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int nc_main(int argc, char **argv)
{
	/* sfd sits _here_ only because of "repeat" option (-l -l). */
	int sfd = sfd; /* for gcc */
	int cfd = 0;
	unsigned lport = 0;
	IF_NOT_NC_SERVER(const) unsigned do_listen = 0;
	IF_NOT_NC_EXTRA (const) unsigned wsecs = 0;
	IF_NOT_NC_EXTRA (const) unsigned delay = 0;
	IF_NOT_NC_EXTRA (const int execparam = 0;)
	IF_NC_EXTRA     (char **execparam = NULL;)
	struct pollfd pfds[2];
	int opt; /* must be signed (getopt returns -1) */

	if (ENABLE_NC_SERVER || ENABLE_NC_EXTRA) {
		/* getopt32 is _almost_ usable:
		** it cannot handle "... -e PROG -prog-opt" */
		while ((opt = getopt(argc, argv,
			"" IF_NC_SERVER("lp:") IF_NC_EXTRA("w:i:f:e:") )) > 0
		) {
			if (ENABLE_NC_SERVER && opt == 'l')
				IF_NC_SERVER(do_listen++);
			else if (ENABLE_NC_SERVER && opt == 'p')
				IF_NC_SERVER(lport = bb_lookup_port(optarg, "tcp", 0));
			else if (ENABLE_NC_EXTRA && opt == 'w')
				IF_NC_EXTRA( wsecs = xatou(optarg));
			else if (ENABLE_NC_EXTRA && opt == 'i')
				IF_NC_EXTRA( delay = xatou(optarg));
			else if (ENABLE_NC_EXTRA && opt == 'f')
				IF_NC_EXTRA( cfd = xopen(optarg, O_RDWR));
			else if (ENABLE_NC_EXTRA && opt == 'e' && optind <= argc) {
				/* We cannot just 'break'. We should let getopt finish.
				** Or else we won't be able to find where
				** 'host' and 'port' params are
				** (think "nc -w 60 host port -e PROG"). */
				IF_NC_EXTRA(
					char **p;
					// +2: one for progname (optarg) and one for NULL
					execparam = xzalloc(sizeof(char*) * (argc - optind + 2));
					p = execparam;
					*p++ = optarg;
					while (optind < argc) {
						*p++ = argv[optind++];
					}
				)
				/* optind points to argv[argc] (NULL) now.
				** FIXME: we assume that getopt will not count options
				** possibly present on "-e PROG ARGS" and will not
				** include them into final value of optind
				** which is to be used ...  */
			} else bb_show_usage();
		}
		argv += optind; /* ... here! */
		argc -= optind;
		// -l and -f don't mix
		if (do_listen && cfd) bb_show_usage();
		// File mode needs need zero arguments, listen mode needs zero or one,
		// client mode needs one or two
		if (cfd) {
			if (argc) bb_show_usage();
		} else if (do_listen) {
			if (argc > 1) bb_show_usage();
		} else {
			if (!argc || argc > 2) bb_show_usage();
		}
	} else {
		if (argc != 3) bb_show_usage();
		argc--;
		argv++;
	}

	if (wsecs) {
		signal(SIGALRM, timeout);
		alarm(wsecs);
	}

	if (!cfd) {
		if (do_listen) {
			sfd = create_and_bind_stream_or_die(argv[0], lport);
			xlisten(sfd, do_listen); /* can be > 1 */
#if 0  /* nc-1.10 does not do this (without -v) */
			/* If we didn't specify a port number,
			 * query and print it after listen() */
			if (!lport) {
				len_and_sockaddr lsa;
				lsa.len = LSA_SIZEOF_SA;
				getsockname(sfd, &lsa.u.sa, &lsa.len);
				lport = get_nport(&lsa.u.sa);
				fdprintf(2, "%d\n", ntohs(lport));
			}
#endif
			close_on_exec_on(sfd);
 accept_again:
			cfd = accept(sfd, NULL, 0);
			if (cfd < 0)
				bb_perror_msg_and_die("accept");
			if (!execparam)
				close(sfd);
		} else {
			cfd = create_and_connect_stream_or_die(argv[0],
				argv[1] ? bb_lookup_port(argv[1], "tcp", 0) : 0);
		}
	}

	if (wsecs) {
		alarm(0);
		/* Non-ignored signals revert to SIG_DFL on exec anyway */
		/*signal(SIGALRM, SIG_DFL);*/
	}

	/* -e given? */
	if (execparam) {
		pid_t pid;
		/* With more than one -l, repeatedly act as server */
		if (do_listen > 1 && (pid = xvfork()) != 0) {
			/* parent */
			/* prevent zombies */
			signal(SIGCHLD, SIG_IGN);
			close(cfd);
			goto accept_again;
		}
		/* child, or main thread if only one -l */
		xmove_fd(cfd, 0);
		xdup2(0, 1);
		/*xdup2(0, 2); - original nc 1.10 does this, we don't */
		IF_NC_EXTRA(BB_EXECVP(execparam[0], execparam);)
		IF_NC_EXTRA(bb_perror_msg_and_die("can't execute '%s'", execparam[0]);)
	}

	/* loop copying stdin to cfd, and cfd to stdout */

	pfds[0].fd = STDIN_FILENO;
	pfds[0].events = POLLIN;
	pfds[1].fd = cfd;
	pfds[1].events = POLLIN;

#define iobuf bb_common_bufsiz1
	setup_common_bufsiz();
	for (;;) {
		int fdidx;
		int ofd;
		int nread;

		if (safe_poll(pfds, 2, -1) < 0)
			bb_perror_msg_and_die("poll");

		fdidx = 0;
		while (1) {
			if (pfds[fdidx].revents) {
				nread = safe_read(pfds[fdidx].fd, iobuf, COMMON_BUFSIZE);
				if (fdidx != 0) {
					if (nread < 1)
						exit(EXIT_SUCCESS);
					ofd = STDOUT_FILENO;
				} else {
					if (nread < 1) {
						/* Close outgoing half-connection so they get EOF,
						 * but leave incoming alone so we can see response */
						shutdown(cfd, SHUT_WR);
						pfds[0].fd = -1;
					}
					ofd = cfd;
				}
				xwrite(ofd, iobuf, nread);
				if (delay > 0)
					sleep(delay);
			}
			if (fdidx == 1)
				break;
			fdidx++;
		}
	}
}
#endif
