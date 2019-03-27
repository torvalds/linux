/*
 * Copyright (c) 1990, 1993, 1994, 1995, 1996
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pcap-types.h>

#include <ctype.h>
#include <memory.h>
#include <stdio.h>
#include <string.h>

#include "pcap-int.h"

#include <pcap/namedb.h>

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

static inline int skip_space(FILE *);
static inline int skip_line(FILE *);

/* Hex digit to integer. */
static inline u_char
xdtoi(u_char c)
{
	if (isdigit(c))
		return (u_char)(c - '0');
	else if (islower(c))
		return (u_char)(c - 'a' + 10);
	else
		return (u_char)(c - 'A' + 10);
}

static inline int
skip_space(FILE *f)
{
	int c;

	do {
		c = getc(f);
	} while (isspace(c) && c != '\n');

	return c;
}

static inline int
skip_line(FILE *f)
{
	int c;

	do
		c = getc(f);
	while (c != '\n' && c != EOF);

	return c;
}

struct pcap_etherent *
pcap_next_etherent(FILE *fp)
{
	register int c, i;
	u_char d;
	char *bp;
	size_t namesize;
	static struct pcap_etherent e;

	memset((char *)&e, 0, sizeof(e));
	for (;;) {
		/* Find addr */
		c = skip_space(fp);
		if (c == EOF)
			return (NULL);
		if (c == '\n')
			continue;

		/* If this is a comment, or first thing on line
		   cannot be Ethernet address, skip the line. */
		if (!isxdigit(c)) {
			c = skip_line(fp);
			if (c == EOF)
				return (NULL);
			continue;
		}

		/* must be the start of an address */
		for (i = 0; i < 6; i += 1) {
			d = xdtoi((u_char)c);
			c = getc(fp);
			if (c == EOF)
				return (NULL);
			if (isxdigit(c)) {
				d <<= 4;
				d |= xdtoi((u_char)c);
				c = getc(fp);
				if (c == EOF)
					return (NULL);
			}
			e.addr[i] = d;
			if (c != ':')
				break;
			c = getc(fp);
			if (c == EOF)
				return (NULL);
		}

		/* Must be whitespace */
		if (!isspace(c)) {
			c = skip_line(fp);
			if (c == EOF)
				return (NULL);
			continue;
		}
		c = skip_space(fp);
		if (c == EOF)
			return (NULL);

		/* hit end of line... */
		if (c == '\n')
			continue;

		if (c == '#') {
			c = skip_line(fp);
			if (c == EOF)
				return (NULL);
			continue;
		}

		/* pick up name */
		bp = e.name;
		/* Use 'namesize' to prevent buffer overflow. */
		namesize = sizeof(e.name) - 1;
		do {
			*bp++ = (u_char)c;
			c = getc(fp);
			if (c == EOF)
				return (NULL);
		} while (!isspace(c) && --namesize != 0);
		*bp = '\0';

		/* Eat trailing junk */
		if (c != '\n')
			(void)skip_line(fp);

		return &e;
	}
}
