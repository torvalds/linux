/*
 * Copyright (c) 1987, 1989 Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Arthur David Olson of the National Cancer Institute.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * SUCH DAMAGE.  */

/*static char *sccsid = "from: @(#)ctime.c	5.26 (Berkeley) 2/23/91";*/

/*
 * This implementation of mktime is lifted straight from the NetBSD (BSD 4.4)
 * version.  I modified it slightly to divorce it from the internals of the
 * ctime library.  Thus this version can't use details of the internal
 * timezone state file to figure out strange unnormalized struct tm values,
 * as might result from someone doing date math on the tm struct then passing
 * it to mktime.
 *
 * It just does as well as it can at normalizing the tm input, then does a
 * binary search of the time space using the system's localtime() function.
 *
 * The original binary search was defective in that it didn't consider the
 * setting of tm_isdst when comparing tm values, causing the search to be
 * flubbed for times near the dst/standard time changeover.  The original
 * code seems to make up for this by grubbing through the timezone info
 * whenever the binary search barfed.  Since I don't have that luxury in
 * portable code, I have to take care of tm_isdst in the comparison routine.
 * This requires knowing how many minutes offset dst is from standard time.
 *
 * So, if you live somewhere in the world where dst is not 60 minutes offset,
 * and your vendor doesn't supply mktime(), you'll have to edit this variable
 * by hand.  Sorry about that.
 */

#include <config.h>
#include "ntp_machine.h"

#if !defined(HAVE_MKTIME) || ( !defined(HAVE_TIMEGM) && defined(WANT_TIMEGM) )

#if SIZEOF_TIME_T >= 8
#error libntp supplied mktime()/timegm() do not support 64-bit time_t
#endif

#ifndef DSTMINUTES
#define DSTMINUTES 60
#endif

#define FALSE 0
#define TRUE 1

/* some constants from tzfile.h */
#define SECSPERMIN      60
#define MINSPERHOUR     60
#define HOURSPERDAY     24
#define DAYSPERWEEK     7
#define DAYSPERNYEAR    365
#define DAYSPERLYEAR    366
#define SECSPERHOUR     (SECSPERMIN * MINSPERHOUR)
#define SECSPERDAY      ((long) SECSPERHOUR * HOURSPERDAY)
#define MONSPERYEAR     12
#define TM_YEAR_BASE    1900
#define isleap(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)

static int	mon_lengths[2][MONSPERYEAR] = {
	{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
	{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};

static int	year_lengths[2] = {
	DAYSPERNYEAR, DAYSPERLYEAR
};

/*
** Adapted from code provided by Robert Elz, who writes:
**	The "best" way to do mktime I think is based on an idea of Bob
**	Kridle's (so its said...) from a long time ago. (mtxinu!kridle now).
**	It does a binary search of the time_t space.  Since time_t's are
**	just 32 bits, its a max of 32 iterations (even at 64 bits it
**	would still be very reasonable).
*/

#ifndef WRONG
#define WRONG	(-1)
#endif /* !defined WRONG */

static void
normalize(
	int * tensptr,
	int * unitsptr,
	int	base
	)
{
	if (*unitsptr >= base) {
		*tensptr += *unitsptr / base;
		*unitsptr %= base;
	} else if (*unitsptr < 0) {
		--*tensptr;
		*unitsptr += base;
		if (*unitsptr < 0) {
			*tensptr -= 1 + (-*unitsptr) / base;
			*unitsptr = base - (-*unitsptr) % base;
		}
	}
}

static struct tm *
mkdst(
	struct tm *	tmp
	)
{
    /* jds */
    static struct tm tmbuf;

    tmbuf = *tmp;
    tmbuf.tm_isdst = 1;
    tmbuf.tm_min += DSTMINUTES;
    normalize(&tmbuf.tm_hour, &tmbuf.tm_min, MINSPERHOUR);
    return &tmbuf;
}

static int
tmcomp(
	register struct tm * atmp,
	register struct tm * btmp
	)
{
	register int	result;

	/* compare down to the same day */

	if ((result = (atmp->tm_year - btmp->tm_year)) == 0 &&
	    (result = (atmp->tm_mon - btmp->tm_mon)) == 0)
	    result = (atmp->tm_mday - btmp->tm_mday);

	if(result != 0)
	    return result;

	/* get rid of one-sided dst bias */

	if(atmp->tm_isdst == 1 && !btmp->tm_isdst)
	    btmp = mkdst(btmp);
	else if(btmp->tm_isdst == 1 && !atmp->tm_isdst)
	    atmp = mkdst(atmp);

	/* compare the rest of the way */

	if ((result = (atmp->tm_hour - btmp->tm_hour)) == 0 &&
	    (result = (atmp->tm_min - btmp->tm_min)) == 0)
	    result = atmp->tm_sec - btmp->tm_sec;
	return result;
}


static time_t
time2(
	struct tm *	tmp,
	int * 		okayp,
	int		usezn
	)
{
	register int			dir;
	register int			bits;
	register int			i;
	register int			saved_seconds;
	time_t				t;
	struct tm			yourtm, mytm;

	*okayp = FALSE;
	yourtm = *tmp;
	if (yourtm.tm_sec >= SECSPERMIN + 2 || yourtm.tm_sec < 0)
		normalize(&yourtm.tm_min, &yourtm.tm_sec, SECSPERMIN);
	normalize(&yourtm.tm_hour, &yourtm.tm_min, MINSPERHOUR);
	normalize(&yourtm.tm_mday, &yourtm.tm_hour, HOURSPERDAY);
	normalize(&yourtm.tm_year, &yourtm.tm_mon, MONSPERYEAR);
	while (yourtm.tm_mday <= 0) {
		--yourtm.tm_year;
		yourtm.tm_mday +=
			year_lengths[isleap(yourtm.tm_year + TM_YEAR_BASE)];
	}
	for ( ; ; ) {
		i = mon_lengths[isleap(yourtm.tm_year +
			TM_YEAR_BASE)][yourtm.tm_mon];
		if (yourtm.tm_mday <= i)
			break;
		yourtm.tm_mday -= i;
		if (++yourtm.tm_mon >= MONSPERYEAR) {
			yourtm.tm_mon = 0;
			++yourtm.tm_year;
		}
	}
	saved_seconds = yourtm.tm_sec;
	yourtm.tm_sec = 0;
	/*
	** Calculate the number of magnitude bits in a time_t
	** (this works regardless of whether time_t is
	** signed or unsigned, though lint complains if unsigned).
	*/
	for (bits = 0, t = 1; t > 0; ++bits, t <<= 1)
		;
	/*
	** If time_t is signed, then 0 is the median value,
	** if time_t is unsigned, then 1 << bits is median.
	*/
	t = (t < 0) ? 0 : ((time_t) 1 << bits);
	for ( ; ; ) {
		if (usezn)
			mytm = *localtime(&t);
		else
			mytm = *gmtime(&t);
		dir = tmcomp(&mytm, &yourtm);
		if (dir != 0) {
			if (bits-- < 0)
				return WRONG;
			if (bits < 0)
				--t;
			else if (dir > 0)
				t -= (time_t) 1 << bits;
			else	t += (time_t) 1 << bits;
			continue;
		}
		if (yourtm.tm_isdst < 0 || mytm.tm_isdst == yourtm.tm_isdst)
			break;

		return WRONG;
	}
	t += saved_seconds;
	if (usezn)
		*tmp = *localtime(&t);
	else
		*tmp = *gmtime(&t);
	*okayp = TRUE;
	return t;
}
#else
int mktime_bs;
#endif /* !HAVE_MKTIME || !HAVE_TIMEGM */

#ifndef HAVE_MKTIME
static time_t
time1(
	struct tm * tmp
	)
{
	register time_t			t;
	int				okay;

	if (tmp->tm_isdst > 1)
		tmp->tm_isdst = 1;
	t = time2(tmp, &okay, 1);
	if (okay || tmp->tm_isdst < 0)
		return t;

	return WRONG;
}

time_t
mktime(
	struct tm * tmp
	)
{
	return time1(tmp);
}
#endif /* !HAVE_MKTIME */

#ifdef WANT_TIMEGM
#ifndef HAVE_TIMEGM
time_t
timegm(
	struct tm * tmp
	)
{
	register time_t			t;
	int				okay;

	tmp->tm_isdst = 0;
	t = time2(tmp, &okay, 0);
	if (okay || tmp->tm_isdst < 0)
		return t;

	return WRONG;
}
#endif /* !HAVE_TIMEGM */
#endif /* WANT_TIMEGM */
