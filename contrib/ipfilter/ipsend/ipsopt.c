/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 */
#if !defined(lint)
static const char sccsid[] = "@(#)ipsopt.c	1.2 1/11/96 (C)1995 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif
#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "ipsend.h"


#ifndef	__P
# ifdef	__STDC__
#  define	__P(x)	x
# else
#  define	__P(x)	()
# endif
#endif


struct ipopt_names ionames[] = {
	{ IPOPT_EOL,	0x01,	1, "eol" },
	{ IPOPT_NOP,	0x02,	1, "nop" },
	{ IPOPT_RR,	0x04,	3, "rr" },	/* 1 route */
	{ IPOPT_TS,	0x08,	8, "ts" },	/* 1 TS */
	{ IPOPT_SECURITY, 0x08,	11, "sec-level" },
	{ IPOPT_LSRR,	0x10,	7, "lsrr" },	/* 1 route */
	{ IPOPT_SATID,	0x20,	4, "satid" },
	{ IPOPT_SSRR,	0x40,	7, "ssrr" },	/* 1 route */
	{ 0, 0, 0, NULL }	/* must be last */
};

struct	ipopt_names secnames[] = {
	{ IPOPT_SECUR_UNCLASS,	0x0100,	0, "unclass" },
	{ IPOPT_SECUR_CONFID,	0x0200,	0, "confid" },
	{ IPOPT_SECUR_EFTO,	0x0400,	0, "efto" },
	{ IPOPT_SECUR_MMMM,	0x0800,	0, "mmmm" },
	{ IPOPT_SECUR_RESTR,	0x1000,	0, "restr" },
	{ IPOPT_SECUR_SECRET,	0x2000,	0, "secret" },
	{ IPOPT_SECUR_TOPSECRET, 0x4000,0, "topsecret" },
	{ 0, 0, 0, NULL }	/* must be last */
};


u_short ipseclevel(slevel)
	char *slevel;
{
	struct ipopt_names *so;

	for (so = secnames; so->on_name; so++)
		if (!strcasecmp(slevel, so->on_name))
			break;

	if (!so->on_name) {
		fprintf(stderr, "no such security level: %s\n", slevel);
		return 0;
	}
	return so->on_value;
}


int addipopt(op, io, len, class)
	char *op;
	struct ipopt_names *io;
	int len;
	char *class;
{
	struct in_addr ipadr;
	int olen = len, srr = 0;
	u_short val;
	u_char lvl;
	char *s = op, *t;

	if ((len + io->on_siz) > 48) {
		fprintf(stderr, "options too long\n");
		return 0;
	}
	len += io->on_siz;
	*op++ = io->on_value;
	if (io->on_siz > 1) {
		/*
		 * Allow option to specify RR buffer length in bytes.
		 */
		if (io->on_value == IPOPT_RR) {
			val = (class && *class) ? atoi(class) : 4;
			*op++ = val + io->on_siz;
			len += val;
		} else
			*op++ = io->on_siz;
		if (io->on_value == IPOPT_TS)
			*op++ = IPOPT_MINOFF + 1;
		else
			*op++ = IPOPT_MINOFF;

		while (class && *class) {
			t = NULL;
			switch (io->on_value)
			{
			case IPOPT_SECURITY :
				lvl = ipseclevel(class);
				*(op - 1) = lvl;
				break;
			case IPOPT_LSRR :
			case IPOPT_SSRR :
				if ((t = strchr(class, ',')))
					*t = '\0';
				ipadr.s_addr = inet_addr(class);
				srr++;
				bcopy((char *)&ipadr, op, sizeof(ipadr));
				op += sizeof(ipadr);
				break;
			case IPOPT_SATID :
				val = atoi(class);
				bcopy((char *)&val, op, 2);
				break;
			}

			if (t)
				*t++ = ',';
			class = t;
		}
		if (srr)
			s[IPOPT_OLEN] = IPOPT_MINOFF - 1 + 4 * srr;
		if (io->on_value == IPOPT_RR)
			op += val;
		else
			op += io->on_siz - 3;
	}
	return len - olen;
}


u_32_t buildopts(cp, op, len)
	char *cp, *op;
	int len;
{
	struct ipopt_names *io;
	u_32_t msk = 0;
	char *s, *t;
	int inc, lastop = -1;

	for (s = strtok(cp, ","); s; s = strtok(NULL, ",")) {
		if ((t = strchr(s, '=')))
			*t++ = '\0';
		for (io = ionames; io->on_name; io++) {
			if (strcasecmp(s, io->on_name) || (msk & io->on_bit))
				continue;
			lastop = io->on_value;
			if ((inc = addipopt(op, io, len, t))) {
				op += inc;
				len += inc;
			}
			msk |= io->on_bit;
			break;
		}
		if (!io->on_name) {
			fprintf(stderr, "unknown IP option name %s\n", s);
			return 0;
		}
	}

	if (len & 3) {
		while (len & 3) {
			*op++ = ((len & 3) == 3) ? IPOPT_EOL : IPOPT_NOP;
			len++;
		}
	} else {
		if (lastop != IPOPT_EOL) {
			if (lastop == IPOPT_NOP)
				*(op - 1) = IPOPT_EOL;
			else {
				*op++ = IPOPT_NOP;
				*op++ = IPOPT_NOP;
				*op++ = IPOPT_NOP;
				*op = IPOPT_EOL;
				len += 4;
			}
		}
	}
	return len;
}
