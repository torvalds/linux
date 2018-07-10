/*
 * Copyright (C) 2017 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config SSL_CLIENT
//config:	bool "ssl_client (23 kb)"
//config:	default y
//config:	select TLS
//config:	help
//config:	This tool pipes data to/from a socket, TLS-encrypting it.

//applet:IF_SSL_CLIENT(APPLET(ssl_client, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_SSL_CLIENT) += ssl_client.o

//usage:#define ssl_client_trivial_usage
//usage:       "[-e] -s FD [-r FD] [-n SNI]"
//usage:#define ssl_client_full_usage ""

#include "libbb.h"

int ssl_client_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ssl_client_main(int argc UNUSED_PARAM, char **argv)
{
	tls_state_t *tls;
	const char *sni = NULL;
	int opt;

	// INIT_G();

	tls = new_tls_state();
	opt = getopt32(argv, "es:+r:+n:", &tls->ofd, &tls->ifd, &sni);
	if (!(opt & (1<<2))) {
		/* -r N defaults to -s N */
		tls->ifd = tls->ofd;
	}

	if (!(opt & (3<<1))) {
		if (!argv[1])
			bb_show_usage();
		/* Undocumented debug feature: without -s and -r, takes HOST arg and connects to it */
		//
		// Talk to kernel.org:
		// printf "GET / HTTP/1.1\r\nHost: kernel.org\r\n\r\n" | busybox ssl_client kernel.org
		if (!sni)
			sni = argv[1];
		tls->ifd = tls->ofd = create_and_connect_stream_or_die(argv[1], 443);
	}

	tls_handshake(tls, sni);

	BUILD_BUG_ON(TLSLOOP_EXIT_ON_LOCAL_EOF != 1);
	tls_run_copy_loop(tls, /*flags*/ opt & 1);

	return EXIT_SUCCESS;
}
