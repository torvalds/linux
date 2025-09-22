/*	$OpenBSD: time.c,v 1.18 2023/03/08 04:43:04 guenther Exp $	*/
/*	$NetBSD: time.c,v 1.7 1995/03/21 13:55:25 mycroft Exp $	*/

/*-
 * Copyright (c) 1980, 1991, 1993
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

#include <sys/types.h>
#include <stdarg.h>

#include "csh.h"
#include "extern.h"

/*
 * C Shell - routines handling process timing and niceing
 */
static void	pdeltat(struct timeval *, struct timeval *);

void
settimes(void)
{
    struct rusage ruch;

    (void) clock_gettime(CLOCK_MONOTONIC, &time0);
    (void) getrusage(RUSAGE_SELF, &ru0);
    (void) getrusage(RUSAGE_CHILDREN, &ruch);
    ruadd(&ru0, &ruch);
}

/*
 * dotime is only called if it is truly a builtin function and not a
 * prefix to another command
 */
void
dotime(Char **v, struct command *t)
{
    struct timespec timedol;
    struct rusage ru1, ruch;

    (void) getrusage(RUSAGE_SELF, &ru1);
    (void) getrusage(RUSAGE_CHILDREN, &ruch);
    ruadd(&ru1, &ruch);
    (void) clock_gettime(CLOCK_MONOTONIC, &timedol);
    prusage(&ru0, &ru1, &timedol, &time0);
}

/*
 * donice is only called when it on the line by itself or with a +- value
 */
void
donice(Char **v, struct command *t)
{
    Char *cp;
    int     nval = 0;

    v++, cp = *v++;
    if (cp == 0)
	nval = 4;
    else if (*v == 0 && any("+-", cp[0]))
	nval = getn(cp);
    (void) setpriority(PRIO_PROCESS, 0, nval);
}

void
ruadd(struct rusage *ru, struct rusage *ru2)
{
    timeradd(&ru->ru_utime, &ru2->ru_utime, &ru->ru_utime);
    timeradd(&ru->ru_stime, &ru2->ru_stime, &ru->ru_stime);
    if (ru2->ru_maxrss > ru->ru_maxrss)
	ru->ru_maxrss = ru2->ru_maxrss;

    ru->ru_ixrss += ru2->ru_ixrss;
    ru->ru_idrss += ru2->ru_idrss;
    ru->ru_isrss += ru2->ru_isrss;
    ru->ru_minflt += ru2->ru_minflt;
    ru->ru_majflt += ru2->ru_majflt;
    ru->ru_nswap += ru2->ru_nswap;
    ru->ru_inblock += ru2->ru_inblock;
    ru->ru_oublock += ru2->ru_oublock;
    ru->ru_msgsnd += ru2->ru_msgsnd;
    ru->ru_msgrcv += ru2->ru_msgrcv;
    ru->ru_nsignals += ru2->ru_nsignals;
    ru->ru_nvcsw += ru2->ru_nvcsw;
    ru->ru_nivcsw += ru2->ru_nivcsw;
}

void
prusage(struct rusage *r0, struct rusage *r1, struct timespec *e,
    struct timespec *b)
{
    time_t t =
    (r1->ru_utime.tv_sec - r0->ru_utime.tv_sec) * 100 +
    (r1->ru_utime.tv_usec - r0->ru_utime.tv_usec) / 10000 +
    (r1->ru_stime.tv_sec - r0->ru_stime.tv_sec) * 100 +
    (r1->ru_stime.tv_usec - r0->ru_stime.tv_usec) / 10000;
    char *cp;
    long i;
    struct varent *vp = adrof(STRtime);

    int     ms =
    (e->tv_sec - b->tv_sec) * 100 + (e->tv_nsec - b->tv_nsec) / 10000000;

    cp = "%Uu %Ss %E %P %X+%Dk %I+%Oio %Fpf+%Ww";

    if (vp && vp->vec[0] && vp->vec[1])
	cp = short2str(vp->vec[1]);

    for (; *cp; cp++)
	if (*cp != '%')
	    (void) fputc(*cp, cshout);
	else if (cp[1])
	    switch (*++cp) {

	    case 'U':		/* user CPU time used */
		pdeltat(&r1->ru_utime, &r0->ru_utime);
		break;

	    case 'S':		/* system CPU time used */
		pdeltat(&r1->ru_stime, &r0->ru_stime);
		break;

	    case 'E':		/* elapsed (wall-clock) time */
		pcsecs((long) ms);
		break;

	    case 'P':		/* percent time spent running */
		/* check if it did not run at all */
		i = (ms == 0) ? 0 : ((long long)t * 1000 / ms);
		/* nn.n% */
		(void) fprintf(cshout, "%ld.%01ld%%", i / 10, i % 10);
		break;

	    case 'W':		/* number of swaps */
		i = r1->ru_nswap - r0->ru_nswap;
		(void) fprintf(cshout, "%ld", i);
		break;

	    case 'X':		/* (average) shared text size */
		(void) fprintf(cshout, "%ld", t == 0 ? 0L :
			       (long)((r1->ru_ixrss - r0->ru_ixrss) / t));
		break;

	    case 'D':		/* (average) unshared data size */
		(void) fprintf(cshout, "%ld", t == 0 ? 0L :
			(long)((r1->ru_idrss + r1->ru_isrss -
			       (r0->ru_idrss + r0->ru_isrss)) / t));
		break;

	    case 'K':		/* (average) total data memory used  */
		(void) fprintf(cshout, "%ld", t == 0 ? 0L :
			(long)(((r1->ru_ixrss + r1->ru_isrss + r1->ru_idrss)
			      - (r0->ru_ixrss + r0->ru_idrss + r0->ru_isrss))
			      / t));
		break;

	    case 'M':		/* max. Resident Set Size */
		(void) fprintf(cshout, "%ld", r1->ru_maxrss / 2L);
		break;

	    case 'F':		/* page faults */
		(void) fprintf(cshout, "%ld", r1->ru_majflt - r0->ru_majflt);
		break;

	    case 'R':		/* page reclaims */
		(void) fprintf(cshout, "%ld", r1->ru_minflt - r0->ru_minflt);
		break;

	    case 'I':		/* FS blocks in */
		(void) fprintf(cshout, "%ld", r1->ru_inblock - r0->ru_inblock);
		break;

	    case 'O':		/* FS blocks out */
		(void) fprintf(cshout, "%ld", r1->ru_oublock - r0->ru_oublock);
		break;

	    case 'r':		/* socket messages received */
		(void) fprintf(cshout, "%ld", r1->ru_msgrcv - r0->ru_msgrcv);
		break;

	    case 's':		/* socket messages sent */
		(void) fprintf(cshout, "%ld", r1->ru_msgsnd - r0->ru_msgsnd);
		break;

	    case 'k':		/* number of signals received */
		(void) fprintf(cshout, "%ld", r1->ru_nsignals-r0->ru_nsignals);
		break;

	    case 'w':		/* num. voluntary context switches (waits) */
		(void) fprintf(cshout, "%ld", r1->ru_nvcsw - r0->ru_nvcsw);
		break;

	    case 'c':		/* num. involuntary context switches */
		(void) fprintf(cshout, "%ld", r1->ru_nivcsw - r0->ru_nivcsw);
		break;
	    }
    (void) fputc('\n', cshout);
}

static void
pdeltat(struct timeval *t1, struct timeval *t0)
{
    struct timeval td;

    timersub(t1, t0, &td);
    (void) fprintf(cshout, "%lld.%01ld", (long long)td.tv_sec,
	td.tv_usec / 100000);
}

#define  P2DIG(i) (void) fprintf(cshout, "%d%d", (i) / 10, (i) % 10)

void
psecs(long l)
{
    int i;

    i = l / 3600;
    if (i) {
	(void) fprintf(cshout, "%d:", i);
	i = l % 3600;
	P2DIG(i / 60);
	goto minsec;
    }
    i = l;
    (void) fprintf(cshout, "%d", i / 60);
minsec:
    i %= 60;
    (void) fputc(':', cshout);
    P2DIG(i);
}

void
pcsecs(long l)			/* PWP: print mm:ss.dd, l is in sec*100 */
{
    int i;

    i = l / 360000;
    if (i) {
	(void) fprintf(cshout, "%d:", i);
	i = (l % 360000) / 100;
	P2DIG(i / 60);
	goto minsec;
    }
    i = l / 100;
    (void) fprintf(cshout, "%d", i / 60);
minsec:
    i %= 60;
    (void) fputc(':', cshout);
    P2DIG(i);
    (void) fputc('.', cshout);
    P2DIG((int) (l % 100));
}
