/*	$NetBSD: print-ascii.c,v 1.1 1999/09/30 14:49:12 sjg Exp $ 	*/

/*-
 * Copyright (c) 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Alan Barrett and Simon J. Gerraty.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* \summary: ASCII packet dump printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>
#include <stdio.h>

#include "netdissect.h"

#define ASCII_LINELENGTH 300
#define HEXDUMP_BYTES_PER_LINE 16
#define HEXDUMP_SHORTS_PER_LINE (HEXDUMP_BYTES_PER_LINE / 2)
#define HEXDUMP_HEXSTUFF_PER_SHORT 5 /* 4 hex digits and a space */
#define HEXDUMP_HEXSTUFF_PER_LINE \
		(HEXDUMP_HEXSTUFF_PER_SHORT * HEXDUMP_SHORTS_PER_LINE)

void
ascii_print(netdissect_options *ndo,
            const u_char *cp, u_int length)
{
	u_int caplength;
	register u_char s;

	caplength = (ndo->ndo_snapend >= cp) ? ndo->ndo_snapend - cp : 0;
	if (length > caplength)
		length = caplength;
	ND_PRINT((ndo, "\n"));
	while (length > 0) {
		s = *cp++;
		length--;
		if (s == '\r') {
			/*
			 * Don't print CRs at the end of the line; they
			 * don't belong at the ends of lines on UN*X,
			 * and the standard I/O library will give us one
			 * on Windows so we don't need to print one
			 * ourselves.
			 *
			 * In the middle of a line, just print a '.'.
			 */
			if (length > 1 && *cp != '\n')
				ND_PRINT((ndo, "."));
		} else {
			if (!ND_ISGRAPH(s) &&
			    (s != '\t' && s != ' ' && s != '\n'))
				ND_PRINT((ndo, "."));
			else
				ND_PRINT((ndo, "%c", s));
		}
	}
}

void
hex_and_ascii_print_with_offset(netdissect_options *ndo, register const char *ident,
    register const u_char *cp, register u_int length, register u_int oset)
{
	u_int caplength;
	register u_int i;
	register int s1, s2;
	register int nshorts;
	char hexstuff[HEXDUMP_SHORTS_PER_LINE*HEXDUMP_HEXSTUFF_PER_SHORT+1], *hsp;
	char asciistuff[ASCII_LINELENGTH+1], *asp;

	caplength = (ndo->ndo_snapend >= cp) ? ndo->ndo_snapend - cp : 0;
	if (length > caplength)
		length = caplength;
	nshorts = length / sizeof(u_short);
	i = 0;
	hsp = hexstuff; asp = asciistuff;
	while (--nshorts >= 0) {
		s1 = *cp++;
		s2 = *cp++;
		(void)snprintf(hsp, sizeof(hexstuff) - (hsp - hexstuff),
		    " %02x%02x", s1, s2);
		hsp += HEXDUMP_HEXSTUFF_PER_SHORT;
		*(asp++) = (ND_ISGRAPH(s1) ? s1 : '.');
		*(asp++) = (ND_ISGRAPH(s2) ? s2 : '.');
		i++;
		if (i >= HEXDUMP_SHORTS_PER_LINE) {
			*hsp = *asp = '\0';
			ND_PRINT((ndo, "%s0x%04x: %-*s  %s",
			    ident, oset, HEXDUMP_HEXSTUFF_PER_LINE,
			    hexstuff, asciistuff));
			i = 0; hsp = hexstuff; asp = asciistuff;
			oset += HEXDUMP_BYTES_PER_LINE;
		}
	}
	if (length & 1) {
		s1 = *cp++;
		(void)snprintf(hsp, sizeof(hexstuff) - (hsp - hexstuff),
		    " %02x", s1);
		hsp += 3;
		*(asp++) = (ND_ISGRAPH(s1) ? s1 : '.');
		++i;
	}
	if (i > 0) {
		*hsp = *asp = '\0';
		ND_PRINT((ndo, "%s0x%04x: %-*s  %s",
		     ident, oset, HEXDUMP_HEXSTUFF_PER_LINE,
		     hexstuff, asciistuff));
	}
}

void
hex_and_ascii_print(netdissect_options *ndo, register const char *ident,
    register const u_char *cp, register u_int length)
{
	hex_and_ascii_print_with_offset(ndo, ident, cp, length, 0);
}

/*
 * telnet_print() wants this.  It is essentially default_print_unaligned()
 */
void
hex_print_with_offset(netdissect_options *ndo,
                      const char *ident, const u_char *cp, u_int length,
		      u_int oset)
{
	u_int caplength;
	register u_int i, s;
	register int nshorts;

	caplength = (ndo->ndo_snapend >= cp) ? ndo->ndo_snapend - cp : 0;
	if (length > caplength)
		length = caplength;
	nshorts = (u_int) length / sizeof(u_short);
	i = 0;
	while (--nshorts >= 0) {
		if ((i++ % 8) == 0) {
			ND_PRINT((ndo,"%s0x%04x: ", ident, oset));
			oset += HEXDUMP_BYTES_PER_LINE;
		}
		s = *cp++;
		ND_PRINT((ndo," %02x%02x", s, *cp++));
	}
	if (length & 1) {
		if ((i % 8) == 0)
			ND_PRINT((ndo,"%s0x%04x: ", ident, oset));
		ND_PRINT((ndo," %02x", *cp));
	}
}

/*
 * just for completeness
 */
void
hex_print(netdissect_options *ndo,const char *ident, const u_char *cp, u_int length)
{
  hex_print_with_offset(ndo, ident, cp, length, 0);
}

#ifdef MAIN
int
main(int argc, char *argv[])
{
	hex_print("\n\t", "Hello, World!\n", 14);
	printf("\n");
	hex_and_ascii_print("\n\t", "Hello, World!\n", 14);
	printf("\n");
	ascii_print("Hello, World!\n", 14);
	printf("\n");
#define TMSG "Now is the winter of our discontent...\n"
	hex_print_with_offset("\n\t", TMSG, sizeof(TMSG) - 1, 0x100);
	printf("\n");
	hex_and_ascii_print_with_offset("\n\t", TMSG, sizeof(TMSG) - 1, 0x100);
	printf("\n");
	exit(0);
}
#endif /* MAIN */
