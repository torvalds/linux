/*
 * prettydate - convert a time stamp to something readable
 */
#include <config.h>
#include <stdio.h>

#include "ntp_fp.h"
#include "ntp_unixtime.h"	/* includes <sys/time.h> */
#include "lib_strbuf.h"
#include "ntp_stdlib.h"
#include "ntp_assert.h"
#include "ntp_calendar.h"

#if SIZEOF_TIME_T < 4
# error sizeof(time_t) < 4 -- this will not work!
#endif

static char *common_prettydate(l_fp *, int);

const char * const months[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

const char * const daynames[7] = {
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

/* Helper function to handle possible wraparound of the ntp epoch.
 *
 * Works by periodic extension of the ntp time stamp in the UN*X epoch.
 * If the 'time_t' is 32 bit, use solar cycle warping to get the value
 * in a suitable range. Also uses solar cycle warping to work around
 * really buggy implementations of 'gmtime()' / 'localtime()' that
 * cannot work with a negative time value, that is, times before
 * 1970-01-01. (MSVCRT...)
 *
 * Apart from that we're assuming that the localtime/gmtime library
 * functions have been updated so that they work...
 *
 * An explanation: The julian calendar repeats ever 28 years, because
 * it's the LCM of 7 and 1461, the week and leap year cycles. This is
 * called a 'solar cycle'. The gregorian calendar does the same as
 * long as no centennial year (divisible by 100, but not 400) goes in
 * the way. So between 1901 and 2099 (inclusive) we can warp time
 * stamps by 28 years to make them suitable for localtime() and
 * gmtime() if we have trouble. Of course this will play hubbubb with
 * the DST zone switches, so we should do it only if necessary; but as
 * we NEED a proper conversion to dates via gmtime() we should try to
 * cope with as many idiosyncrasies as possible.
 *
 */

/*
 * solar cycle in unsigned secs and years, and the cycle limits.
 */
#define SOLAR_CYCLE_SECS   0x34AADC80UL	/* 7*1461*86400*/
#define SOLAR_CYCLE_YEARS  28
#define MINFOLD -3
#define MAXFOLD	 3

static struct tm *
get_struct_tm(
	const vint64 *stamp,
	int	      local)
{
	struct tm *tm	 = NULL;
	int32	   folds = 0;
	time_t	   ts;

#ifdef HAVE_INT64

	int64 tl;
	ts = tl = stamp->q_s;

	/*
	 * If there is chance of truncation, try to fix it. Let the
	 * compiler find out if this can happen at all.
	 */
	while (ts != tl) { /* truncation? */
		if (tl < 0) {
			if (--folds < MINFOLD)
				return NULL;
			tl += SOLAR_CYCLE_SECS;
		} else {
			if (++folds > MAXFOLD)
				return NULL;
			tl -= SOLAR_CYCLE_SECS;
		}
		ts = tl; /* next try... */
	}
#else

	/*
	 * since we do not have 64-bit scalars, it's not likely we have
	 * 64-bit time_t. Assume 32 bits and properly reduce the value.
	 */
	u_int32 hi, lo;

	hi = stamp->D_s.hi;
	lo = stamp->D_s.lo;

	while ((hi && ~hi) || ((hi ^ lo) & 0x80000000u)) {
		if (M_ISNEG(hi, lo)) {
			if (--folds < MINFOLD)
				return NULL;
			M_ADD(hi, lo, 0, SOLAR_CYCLE_SECS);
		} else {
			if (++folds > MAXFOLD)
				return NULL;
			M_SUB(hi, lo, 0, SOLAR_CYCLE_SECS);
		}
	}
	ts = (int32)lo;

#endif

	/*
	 * 'ts' should be a suitable value by now. Just go ahead, but
	 * with care:
	 *
	 * There are some pathological implementations of 'gmtime()'
	 * and 'localtime()' out there. No matter if we have 32-bit or
	 * 64-bit 'time_t', try to fix this by solar cycle warping
	 * again...
	 *
	 * At least the MSDN says that the (Microsoft) Windoze
	 * versions of 'gmtime()' and 'localtime()' will bark on time
	 * stamps < 0.
	 */
	while ((tm = (*(local ? localtime : gmtime))(&ts)) == NULL)
		if (ts < 0) {
			if (--folds < MINFOLD)
				return NULL;
			ts += SOLAR_CYCLE_SECS;
		} else if (ts >= (time_t)SOLAR_CYCLE_SECS) {
			if (++folds > MAXFOLD)
				return NULL;
			ts -= SOLAR_CYCLE_SECS;
		} else
			return NULL; /* That's truly pathological! */

	/* 'tm' surely not NULL here! */
	INSIST(tm != NULL);
	if (folds != 0) {
		tm->tm_year += folds * SOLAR_CYCLE_YEARS;
		if (tm->tm_year <= 0 || tm->tm_year >= 200)
			return NULL;	/* left warp range... can't help here! */
	}

	return tm;
}

static char *
common_prettydate(
	l_fp *ts,
	int local
	)
{
	static const char pfmt0[] =
	    "%08lx.%08lx  %s, %s %2d %4d %2d:%02d:%02d.%03u";
	static const char pfmt1[] =
	    "%08lx.%08lx [%s, %s %2d %4d %2d:%02d:%02d.%03u UTC]";

	char	    *bp;
	struct tm   *tm;
	u_int	     msec;
	u_int32	     ntps;
	vint64	     sec;

	LIB_GETBUF(bp);

	if (ts->l_ui == 0 && ts->l_uf == 0) {
		strlcpy (bp, "(no time)", LIB_BUFLENGTH);
		return (bp);
	}

	/* get & fix milliseconds */
	ntps = ts->l_ui;
	msec = ts->l_uf / 4294967;	/* fract / (2 ** 32 / 1000) */
	if (msec >= 1000u) {
		msec -= 1000u;
		ntps++;
	}
	sec = ntpcal_ntp_to_time(ntps, NULL);
	tm  = get_struct_tm(&sec, local);
	if (!tm) {
		/*
		 * get a replacement, but always in UTC, using
		 * ntpcal_time_to_date()
		 */
		struct calendar jd;
		ntpcal_time_to_date(&jd, &sec);
		snprintf(bp, LIB_BUFLENGTH, local ? pfmt1 : pfmt0,
			 (u_long)ts->l_ui, (u_long)ts->l_uf,
			 daynames[jd.weekday], months[jd.month-1],
			 jd.monthday, jd.year, jd.hour,
			 jd.minute, jd.second, msec);
	} else		
		snprintf(bp, LIB_BUFLENGTH, pfmt0,
			 (u_long)ts->l_ui, (u_long)ts->l_uf,
			 daynames[tm->tm_wday], months[tm->tm_mon],
			 tm->tm_mday, 1900 + tm->tm_year, tm->tm_hour,
			 tm->tm_min, tm->tm_sec, msec);
	return bp;
}


char *
prettydate(
	l_fp *ts
	)
{
	return common_prettydate(ts, 1);
}


char *
gmprettydate(
	l_fp *ts
	)
{
	return common_prettydate(ts, 0);
}


struct tm *
ntp2unix_tm(
	u_int32 ntp, int local
	)
{
	vint64 vl;
	vl = ntpcal_ntp_to_time(ntp, NULL);
	return get_struct_tm(&vl, local);
}
	
