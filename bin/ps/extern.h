/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993, 1994
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
 *
 *	@(#)extern.h	8.3 (Berkeley) 4/2/94
 * $FreeBSD$
 */

struct kinfo;
struct nlist;
struct var;
struct varent;

extern fixpt_t ccpu;
extern int cflag, eval, fscale, nlistread, rawcpu;
extern unsigned long mempages;
extern time_t now;
extern int showthreads, sumrusage, termwidth;
extern STAILQ_HEAD(velisthead, varent) varlist;

__BEGIN_DECLS
char	 *arguments(KINFO *, VARENT *);
char	 *command(KINFO *, VARENT *);
char	 *cputime(KINFO *, VARENT *);
int	 donlist(void);
char	 *elapsed(KINFO *, VARENT *);
char	 *elapseds(KINFO *, VARENT *);
char	 *emulname(KINFO *, VARENT *);
VARENT	*find_varentry(VAR *);
const	 char *fmt_argv(char **, char *, char *, size_t);
double	 getpcpu(const KINFO *);
char	 *jailname(KINFO *, VARENT *);
char	 *kvar(KINFO *, VARENT *);
char	 *label(KINFO *, VARENT *);
char	 *loginclass(KINFO *, VARENT *);
char	 *logname(KINFO *, VARENT *);
char	 *longtname(KINFO *, VARENT *);
char	 *lstarted(KINFO *, VARENT *);
char	 *maxrss(KINFO *, VARENT *);
char	 *lockname(KINFO *, VARENT *);
char	 *mwchan(KINFO *, VARENT *);
char	 *nwchan(KINFO *, VARENT *);
char	 *pagein(KINFO *, VARENT *);
void	 parsefmt(const char *, int);
char	 *pcpu(KINFO *, VARENT *);
char	 *pmem(KINFO *, VARENT *);
char	 *pri(KINFO *, VARENT *);
void	 printheader(void);
char	 *priorityr(KINFO *, VARENT *);
char	 *egroupname(KINFO *, VARENT *);
char	 *rgroupname(KINFO *, VARENT *);
char	 *runame(KINFO *, VARENT *);
char	 *rvar(KINFO *, VARENT *);
void	 showkey(void);
char	 *started(KINFO *, VARENT *);
char	 *state(KINFO *, VARENT *);
char	 *systime(KINFO *, VARENT *);
char	 *tdev(KINFO *, VARENT *);
char	 *tdnam(KINFO *, VARENT *);
char	 *tname(KINFO *, VARENT *);
char	 *ucomm(KINFO *, VARENT *);
char	 *uname(KINFO *, VARENT *);
char	 *upr(KINFO *, VARENT *);
char	 *usertime(KINFO *, VARENT *);
char	 *vsize(KINFO *, VARENT *);
char	 *wchan(KINFO *, VARENT *);
__END_DECLS
