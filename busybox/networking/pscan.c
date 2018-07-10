/*
 * Pscan is a mini port scanner implementation for busybox
 *
 * Copyright 2007 Tito Ragusa <farmatito@tiscali.it>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config PSCAN
//config:	bool "pscan (6.6 kb)"
//config:	default y
//config:	help
//config:	Simple network port scanner.

//applet:IF_PSCAN(APPLET(pscan, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_PSCAN) += pscan.o

//usage:#define pscan_trivial_usage
//usage:       "[-cb] [-p MIN_PORT] [-P MAX_PORT] [-t TIMEOUT] [-T MIN_RTT] HOST"
//usage:#define pscan_full_usage "\n\n"
//usage:       "Scan a host, print all open ports\n"
//usage:     "\n	-c	Show closed ports too"
//usage:     "\n	-b	Show blocked ports too"
//usage:     "\n	-p	Scan from this port (default 1)"
//usage:     "\n	-P	Scan up to this port (default 1024)"
//usage:     "\n	-t	Timeout (default 5000 ms)"
//usage:     "\n	-T	Minimum rtt (default 5 ms, increase for congested hosts)"

#include "libbb.h"

/* debugging */
#ifdef DEBUG_PSCAN
#define DMSG(...) bb_error_msg(__VA_ARGS__)
#define DERR(...) bb_perror_msg(__VA_ARGS__)
#else
#define DMSG(...) ((void)0)
#define DERR(...) ((void)0)
#endif

static const char *port_name(unsigned port)
{
	struct servent *server;

	server = getservbyport(htons(port), NULL);
	if (server)
		return server->s_name;
	return "unknown";
}

/* We don't expect to see 1000+ seconds delay, unsigned is enough */
#define MONOTONIC_US() ((unsigned)monotonic_us())

int pscan_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int pscan_main(int argc UNUSED_PARAM, char **argv)
{
	const char *opt_max_port = "1024";      /* -P: default max port */
	const char *opt_min_port = "1";         /* -p: default min port */
	const char *opt_timeout = "5000";       /* -t: default timeout in msec */
	/* We estimate rtt and wait rtt*4 before concluding that port is
	 * totally blocked. min rtt of 5 ms may be too low if you are
	 * scanning an Internet host behind saturated/traffic shaped link.
	 * Rule of thumb: with min_rtt of N msec, scanning 1000 ports
	 * will take N seconds at absolute minimum */
	const char *opt_min_rtt = "5";          /* -T: default min rtt in msec */
	const char *result_str;
	len_and_sockaddr *lsap;
	int s;
	unsigned opt;
	unsigned port, max_port, nports;
	unsigned closed_ports = 0;
	unsigned open_ports = 0;
	/* all in usec */
	unsigned timeout;
	unsigned min_rtt;
	unsigned rtt_4;
	unsigned start, diff;

	opt = getopt32(argv, "^"
		"cbp:P:t:T:"
		"\0" "=1", /* exactly one non-option */
		&opt_min_port, &opt_max_port, &opt_timeout, &opt_min_rtt
	);
	argv += optind;
	max_port = xatou_range(opt_max_port, 1, 65535);
	port = xatou_range(opt_min_port, 1, max_port);
	nports = max_port - port + 1;
	min_rtt = xatou_range(opt_min_rtt, 1, INT_MAX/1000 / 4) * 1000;
	timeout = xatou_range(opt_timeout, 1, INT_MAX/1000 / 4) * 1000;
	/* Initial rtt is BIG: */
	rtt_4 = timeout;

	DMSG("min_rtt %u timeout %u", min_rtt, timeout);

	lsap = xhost2sockaddr(*argv, port);
	printf("Scanning %s ports %u to %u\n Port\tProto\tState\tService\n",
			*argv, port, max_port);

	for (; port <= max_port; port++) {
		DMSG("rtt %u", rtt_4);

		/* The SOCK_STREAM socket type is implemented on the TCP/IP protocol. */
		set_nport(&lsap->u.sa, htons(port));
		s = xsocket(lsap->u.sa.sa_family, SOCK_STREAM, 0);
		/* We need unblocking socket so we don't need to wait for ETIMEOUT. */
		/* Nonblocking connect typically "fails" with errno == EINPROGRESS */
		ndelay_on(s);

		DMSG("connect to port %u", port);
		result_str = NULL;
		start = MONOTONIC_US();
		if (connect(s, &lsap->u.sa, lsap->len) == 0) {
			/* Unlikely, for me even localhost fails :) */
			DMSG("connect succeeded");
			goto open;
		}
		/* Check for untypical errors... */
		if (errno != EAGAIN && errno != EINPROGRESS
		 && errno != ECONNREFUSED
		) {
			bb_perror_nomsg_and_die();
		}

		diff = 0;
		while (1) {
			if (errno == ECONNREFUSED) {
				if (opt & 1) /* -c: show closed too */
					result_str = "closed";
				closed_ports++;
				break;
			}
			DERR("port %u errno %d @%u", port, errno, diff);

			if (diff > rtt_4) {
				if (opt & 2) /* -b: show blocked too */
					result_str = "blocked";
				break;
			}
			/* Can sleep (much) longer than specified delay.
			 * We check rtt BEFORE we usleep, otherwise
			 * on localhost we'll have no writes done (!)
			 * before we exceed (rather small) rtt */
			usleep(rtt_4/8);
 open:
			diff = MONOTONIC_US() - start;
			DMSG("write to port %u @%u", port, diff - start);
			if (write(s, " ", 1) >= 0) { /* We were able to write to the socket */
				open_ports++;
				result_str = "open";
				break;
			}
		}
		DMSG("out of loop @%u", diff);
		if (result_str)
			printf("%5u" "\t" "tcp" "\t" "%s" "\t" "%s" "\n",
					port, result_str, port_name(port));

		/* Estimate new rtt - we don't want to wait entire timeout
		 * for each port. *4 allows for rise in net delay.
		 * We increase rtt quickly (rtt_4*4), decrease slowly
		 * (diff is at least rtt_4/8, *4 == rtt_4/2)
		 * because we don't want to accidentally miss ports. */
		rtt_4 = diff * 4;
		if (rtt_4 < min_rtt)
			rtt_4 = min_rtt;
		if (rtt_4 > timeout)
			rtt_4 = timeout;
		/* Clean up */
		close(s);
	}
	if (ENABLE_FEATURE_CLEAN_UP) free(lsap);

	printf("%u closed, %u open, %u timed out (or blocked) ports\n",
					closed_ports,
					open_ports,
					nports - (closed_ports + open_ports));
	return EXIT_SUCCESS;
}
