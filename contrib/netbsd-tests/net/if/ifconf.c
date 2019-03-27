/*	$NetBSD: ifconf.c,v 1.1 2014/12/08 04:23:03 ozaki-r Exp $	*/
/*
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: ifconf.c,v 1.1 2014/12/08 04:23:03 ozaki-r Exp $");
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/ioctl.h>

#include <net/if.h>

#include <stdio.h>
#include <err.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static void
help(void)
{
	fprintf(stderr, "usage:\n\t%s total\n\t%s list [<nifreqs>]\n",
	    getprogname(), getprogname());
	exit(EXIT_FAILURE);
}

static int
get_number_of_entries(void)
{
	int fd, r;
	struct ifconf ifc;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1)
		err(EXIT_FAILURE, "socket");

	ifc.ifc_len = 0;
	ifc.ifc_buf = NULL;

	r = ioctl(fd, SIOCGIFCONF, &ifc);
	if (r == -1)
		err(EXIT_FAILURE, "ioctl");

	close(fd);

	return ifc.ifc_len / sizeof(struct ifreq);
}

static void
show_number_of_entries(void)
{
	printf("%d\n", get_number_of_entries());
}

static void
show_interfaces(int nifreqs)
{
	int i, fd, r;
	struct ifconf ifc;
	struct ifreq *ifreqs;

	if (nifreqs == 0)
		nifreqs = get_number_of_entries();

	if (nifreqs <= 0)
		errx(EXIT_FAILURE, "nifreqs=%d", nifreqs);

	ifreqs = malloc(sizeof(struct ifreq) * nifreqs);
	if (ifreqs == NULL)
		err(EXIT_FAILURE, "malloc(sizeof(ifreq) * %d)", nifreqs);

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1)
		err(EXIT_FAILURE, "socket");

	ifc.ifc_len = sizeof(struct ifreq) * nifreqs;
	ifc.ifc_req = ifreqs;

	r = ioctl(fd, SIOCGIFCONF, &ifc);
	if (r == -1)
		err(EXIT_FAILURE, "ioctl");
	close(fd);

	for (i=0; i < (int)(ifc.ifc_len / sizeof(struct ifreq)); i++) {
		printf("%s: af=%hhu socklen=%hhu\n", ifreqs[i].ifr_name,
		    ifreqs[i].ifr_addr.sa_family, ifreqs[i].ifr_addr.sa_len);
	}

	free(ifreqs);
}

int
main(int argc, char *argv[])
{
	if (argc < 2)
		help();

	if (strcmp(argv[1], "total") == 0) {
		show_number_of_entries();
	} else if (strcmp(argv[1], "list") == 0) {
		if (argc == 2)
			show_interfaces(0);
		else if (argc == 3)
			show_interfaces(atoi(argv[2]));
		else
			help();
	} else
		help();

	return EXIT_SUCCESS;
}
