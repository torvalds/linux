/*	$NetBSD: domacro.c,v 1.8 2009/05/20 12:53:47 lukem Exp $	*/
/*	from	NetBSD: domacro.c,v 1.22 2009/04/12 10:18:52 lukem Exp	*/

/*
 * Copyright (c) 1985, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "tnftp.h"

#if 0	/* tnftp */

#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)domacro.c	8.3 (Berkeley) 4/2/94";
#else
__RCSID(" NetBSD: domacro.c,v 1.22 2009/04/12 10:18:52 lukem Exp  ");
#endif
#endif /* not lint */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#endif	/* tnftp */

#include "ftp_var.h"

void
domacro(int argc, char *argv[])
{
	int i, j, count = 2, loopflg = 0;
	char *cp1, *cp2, line2[FTPBUFLEN];
	struct cmd *c;
	char cmdbuf[MAX_C_NAME];

	if ((argc == 0 && argv != NULL) ||
	    (argc < 2 && !another(&argc, &argv, "macro name"))) {
		UPRINTF("usage: %s macro_name [args]\n", argv[0]);
		code = -1;
		return;
	}
	for (i = 0; i < macnum; ++i) {
		if (!strncmp(argv[1], macros[i].mac_name, 9))
			break;
	}
	if (i == macnum) {
		fprintf(ttyout, "'%s' macro not found.\n", argv[1]);
		code = -1;
		return;
	}
	(void)strlcpy(line2, line, sizeof(line2));
 TOP:
	cp1 = macros[i].mac_start;
	while (cp1 != macros[i].mac_end) {
		while (isspace((unsigned char)*cp1))
			cp1++;
		cp2 = line;
		while (*cp1 != '\0') {
			switch(*cp1) {
			case '\\':
				*cp2++ = *++cp1;
				break;
			case '$':
				if (isdigit((unsigned char)*(cp1+1))) {
					j = 0;
					while (isdigit((unsigned char)*++cp1))
						j = 10*j +  *cp1 - '0';
					cp1--;
					if (argc - 2 >= j) {
						(void)strlcpy(cp2, argv[j+1],
						    sizeof(line) - (cp2 - line));
						cp2 += strlen(argv[j+1]);
					}
					break;
				}
				if (*(cp1+1) == 'i') {
					loopflg = 1;
					cp1++;
					if (count < argc) {
						(void)strlcpy(cp2, argv[count],
						    sizeof(line) - (cp2 - line));
						cp2 += strlen(argv[count]);
					}
					break;
				}
				/* intentional drop through */
			default:
				*cp2++ = *cp1;
				break;
			}
			if (*cp1 != '\0')
				cp1++;
		}
		*cp2 = '\0';
		makeargv();
		c = getcmd(margv[0]);
		if (c == (struct cmd *)-1) {
			fputs("?Ambiguous command.\n", ttyout);
			code = -1;
		} else if (c == 0) {
			fputs("?Invalid command.\n", ttyout);
			code = -1;
		} else if (c->c_conn && !connected) {
			fputs("Not connected.\n", ttyout);
			code = -1;
		} else {
			if (verbose) {
				fputs(line, ttyout);
				putc('\n', ttyout);
			}
			(void)strlcpy(cmdbuf, c->c_name, sizeof(cmdbuf));
			margv[0] = cmdbuf;
			(*c->c_handler)(margc, margv);
			if (bell && c->c_bell)
				(void)putc('\007', ttyout);
			(void)strlcpy(line, line2, sizeof(line));
			makeargv();
			argc = margc;
			argv = margv;
		}
		if (cp1 != macros[i].mac_end)
			cp1++;
	}
	if (loopflg && ++count < argc)
		goto TOP;
}
