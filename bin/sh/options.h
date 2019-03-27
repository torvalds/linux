/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
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
 *
 *	@(#)options.h	8.2 (Berkeley) 5/4/95
 * $FreeBSD$
 */

struct shparam {
	int nparam;		/* # of positional parameters (without $0) */
	unsigned char malloc;	/* if parameter list dynamically allocated */
	unsigned char reset;	/* if getopts has been reset */
	char **p;		/* parameter list */
	char **optp;		/* parameter list for getopts */
	char **optnext;		/* next parameter to be processed by getopts */
	char *optptr;		/* used by getopts */
};



#define eflag optval[0]
#define fflag optval[1]
#define Iflag optval[2]
#define iflag optval[3]
#define mflag optval[4]
#define nflag optval[5]
#define sflag optval[6]
#define xflag optval[7]
#define vflag optval[8]
#define Vflag optval[9]
#define	Eflag optval[10]
#define	Cflag optval[11]
#define	aflag optval[12]
#define	bflag optval[13]
#define	uflag optval[14]
#define	privileged optval[15]
#define	Tflag optval[16]
#define	Pflag optval[17]
#define	hflag optval[18]
#define	nologflag optval[19]
#define	pipefailflag optval[20]

#define NSHORTOPTS	19
#define NOPTS		21

extern char optval[NOPTS];
extern const char optletter[NSHORTOPTS];
#ifdef DEFINE_OPTIONS
char optval[NOPTS];
const char optletter[NSHORTOPTS] = "efIimnsxvVECabupTPh";
static const unsigned char optname[] =
	"\007errexit"
	"\006noglob"
	"\011ignoreeof"
	"\013interactive"
	"\007monitor"
	"\006noexec"
	"\005stdin"
	"\006xtrace"
	"\007verbose"
	"\002vi"
	"\005emacs"
	"\011noclobber"
	"\011allexport"
	"\006notify"
	"\007nounset"
	"\012privileged"
	"\012trapsasync"
	"\010physical"
	"\010trackall"
	"\005nolog"
	"\010pipefail"
;
#endif


extern char *minusc;		/* argument to -c option */
extern char *arg0;		/* $0 */
extern struct shparam shellparam;  /* $@ */
extern char **argptr;		/* argument list for builtin commands */
extern char *shoptarg;		/* set by nextopt */
extern char *nextopt_optptr;	/* used by nextopt */

void procargs(int, char **);
void optschanged(void);
void freeparam(struct shparam *);
int nextopt(const char *);
void getoptsreset(const char *);
