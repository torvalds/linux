/*	$OpenBSD: sysctl.c,v 1.17 2025/08/06 16:50:53 florian Exp $	*/

/*
 * Copyright (c) 2009 Theo de Raadt <deraadt@openbsd.org>
 * Copyright (c) 2007 Kenneth R. Westerback <krw@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/uio.h>

#include <netinet/in.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <machine/cpu.h>

struct var {
	char *name;
	int (*print)(struct var *);
	int nmib;
	int mib[3];
};

int	pstring(struct var *);
int	pint(struct var *);

struct var vars[] = {
	{ "kern.osrelease", pstring, 2,
	    { CTL_KERN, KERN_OSRELEASE }},
	{ "hw.machine", pstring, 2,
	    { CTL_HW, HW_MACHINE }},
	{ "hw.model", pstring, 2,
	    { CTL_HW, HW_MODEL }},
	{ "hw.product", pstring, 2,
	    { CTL_HW, HW_PRODUCT }},
	{ "hw.disknames", pstring, 2,
	    { CTL_HW, HW_DISKNAMES }},
	{ "hw.ncpufound", pint, 2,
	    { CTL_HW, HW_NCPUFOUND }},
#ifdef __amd64__
	{ "machdep.retpoline", pint, 2,
	    { CTL_MACHDEP, CPU_RETPOLINE }},
#endif
#ifdef CPU_COMPATIBLE
	{ "machdep.compatible", pstring, 2,
	    { CTL_MACHDEP, CPU_COMPATIBLE }},
#endif
};

int	nflag;
char	*name;

int
pint(struct var *v)
{
	int n;
	size_t len = sizeof(n);

	if (sysctl(v->mib, v->nmib, &n, &len, NULL, 0) != -1) {
		if (nflag == 0)
			printf("%s=", v->name);
		printf("%d\n", n);
		return (0);
	}
	return (1);
}

int
pstring(struct var *v)
{
	char *p;
	size_t len;

	if (sysctl(v->mib, v->nmib, NULL, &len, NULL, 0) != -1)
		if ((p = malloc(len)) != NULL)
			if (sysctl(v->mib, v->nmib, p, &len, NULL, 0) != -1) {
				if (nflag == 0)
					printf("%s=", v->name);
				printf("%s\n", p);
				return (0);
			}
	return (1);
}

int
main(int argc, char *argv[])
{
	int ch, i;

	while ((ch = getopt(argc, argv, "n")) != -1) {
		switch (ch) {
		case 'n':
			nflag = 1;
			break;
		default:
			fprintf(stderr, "usage: sysctl [-n] [name]\n");
			exit(1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0) {
		for (i = 0; i < sizeof(vars)/sizeof(vars[0]); i++)
			(vars[i].print)(&vars[i]);
		exit(0);
	}

	while (argc--) {
		name = *argv++;
		for (i = 0; i < sizeof(vars)/sizeof(vars[0]); i++) {
			if (strcmp(name, vars[i].name) == 0) {
				(vars[i].print)(&vars[i]);
				break;
			}
		}
	}

	exit(0);
}
