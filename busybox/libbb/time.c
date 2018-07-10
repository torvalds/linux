/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 2007 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"

void FAST_FUNC parse_datestr(const char *date_str, struct tm *ptm)
{
	char end = '\0';
	const char *last_colon = strrchr(date_str, ':');

	if (last_colon != NULL) {
		/* Parse input and assign appropriately to ptm */
#if ENABLE_DESKTOP
		const char *endp;
#endif

		/* HH:MM */
		if (sscanf(date_str, "%u:%u%c",
					&ptm->tm_hour,
					&ptm->tm_min,
					&end) >= 2
		) {
			/* no adjustments needed */
		} else
		/* mm.dd-HH:MM */
		if (sscanf(date_str, "%u.%u-%u:%u%c",
					&ptm->tm_mon, &ptm->tm_mday,
					&ptm->tm_hour, &ptm->tm_min,
					&end) >= 4
		) {
			/* Adjust month from 1-12 to 0-11 */
			ptm->tm_mon -= 1;
		} else
		/* yyyy.mm.dd-HH:MM */
		if (sscanf(date_str, "%u.%u.%u-%u:%u%c", &ptm->tm_year,
					&ptm->tm_mon, &ptm->tm_mday,
					&ptm->tm_hour, &ptm->tm_min,
					&end) >= 5
		/* yyyy-mm-dd HH:MM */
		 || sscanf(date_str, "%u-%u-%u %u:%u%c", &ptm->tm_year,
					&ptm->tm_mon, &ptm->tm_mday,
					&ptm->tm_hour, &ptm->tm_min,
					&end) >= 5
		) {
			ptm->tm_year -= 1900; /* Adjust years */
			ptm->tm_mon -= 1; /* Adjust month from 1-12 to 0-11 */
		} else
#if ENABLE_DESKTOP  /* strptime is BIG: ~1k in uclibc, ~10k in glibc */
		/* month_name d HH:MM:SS YYYY. Supported by GNU date */
		if ((endp = strptime(date_str, "%b %d %T %Y", ptm)) != NULL
		 && *endp == '\0'
		) {
			return; /* don't fall through to end == ":" check */
		} else
#endif
		{
			bb_error_msg_and_die(bb_msg_invalid_date, date_str);
		}
		if (end == ':') {
			/* xxx:SS */
			if (sscanf(last_colon + 1, "%u%c", &ptm->tm_sec, &end) == 1)
				end = '\0';
			/* else end != NUL and we error out */
		}
	} else
	if (strchr(date_str, '-')
	    /* Why strchr('-') check?
	     * sscanf below will trash ptm->tm_year, this breaks
	     * if parse_str is "10101010" (iow, "MMddhhmm" form)
	     * because we destroy year. Do these sscanf
	     * only if we saw a dash in parse_str.
	     */
		/* yyyy-mm-dd HH */
	 && (sscanf(date_str, "%u-%u-%u %u%c", &ptm->tm_year,
				&ptm->tm_mon, &ptm->tm_mday,
				&ptm->tm_hour,
				&end) >= 4
		/* yyyy-mm-dd */
	     || sscanf(date_str, "%u-%u-%u%c", &ptm->tm_year,
				&ptm->tm_mon, &ptm->tm_mday,
				&end) >= 3
	    )
	) {
		ptm->tm_year -= 1900; /* Adjust years */
		ptm->tm_mon -= 1; /* Adjust month from 1-12 to 0-11 */
	} else
	if (date_str[0] == '@') {
		time_t t = bb_strtol(date_str + 1, NULL, 10);
		if (!errno) {
			struct tm *lt = localtime(&t);
			if (lt) {
				*ptm = *lt;
				return;
			}
		}
		end = '1';
	} else {
		/* Googled the following on an old date manpage:
		 *
		 * The canonical representation for setting the date/time is:
		 * cc   Century (either 19 or 20)
		 * yy   Year in abbreviated form (e.g. 89, 06)
		 * mm   Numeric month, a number from 1 to 12
		 * dd   Day, a number from 1 to 31
		 * HH   Hour, a number from 0 to 23
		 * MM   Minutes, a number from 0 to 59
		 * .SS  Seconds, a number from 0 to 61 (with leap seconds)
		 * Everything but the minutes is optional
		 *
		 * "touch -t DATETIME" format: [[[[[YY]YY]MM]DD]hh]mm[.ss]
		 * Some, but not all, Unix "date DATETIME" commands
		 * move [[YY]YY] past minutes mm field (!).
		 * Coreutils date does it, and SUS mandates it.
		 * (date -s DATETIME does not support this format. lovely!)
		 * In bbox, this format is special-cased in date applet
		 * (IOW: this function assumes "touch -t" format).
		 */
		unsigned cur_year = ptm->tm_year;
		int len = strchrnul(date_str, '.') - date_str;

		/* MM[.SS] */
		if (len == 2 && sscanf(date_str, "%2u%2u%2u%2u""%2u%c" + 12,
					&ptm->tm_min,
					&end) >= 1) {
		} else
		/* HHMM[.SS] */
		if (len == 4 && sscanf(date_str, "%2u%2u%2u""%2u%2u%c" + 9,
					&ptm->tm_hour,
					&ptm->tm_min,
					&end) >= 2) {
		} else
		/* ddHHMM[.SS] */
		if (len == 6 && sscanf(date_str, "%2u%2u""%2u%2u%2u%c" + 6,
					&ptm->tm_mday,
					&ptm->tm_hour,
					&ptm->tm_min,
					&end) >= 3) {
		} else
		/* mmddHHMM[.SS] */
		if (len == 8 && sscanf(date_str, "%2u""%2u%2u%2u%2u%c" + 3,
					&ptm->tm_mon,
					&ptm->tm_mday,
					&ptm->tm_hour,
					&ptm->tm_min,
					&end) >= 4) {
			/* Adjust month from 1-12 to 0-11 */
			ptm->tm_mon -= 1;
		} else
		/* yymmddHHMM[.SS] */
		if (len == 10 && sscanf(date_str, "%2u%2u%2u%2u%2u%c",
					&ptm->tm_year,
					&ptm->tm_mon,
					&ptm->tm_mday,
					&ptm->tm_hour,
					&ptm->tm_min,
					&end) >= 5) {
			/* Adjust month from 1-12 to 0-11 */
			ptm->tm_mon -= 1;
			if ((int)cur_year >= 50) { /* >= 1950 */
				/* Adjust year: */
				/* 1. Put it in the current century */
				ptm->tm_year += (cur_year / 100) * 100;
				/* 2. If too far in the past, +100 years */
				if (ptm->tm_year < cur_year - 50)
					ptm->tm_year += 100;
				/* 3. If too far in the future, -100 years */
				if (ptm->tm_year > cur_year + 50)
					ptm->tm_year -= 100;
			}
		} else
		/* ccyymmddHHMM[.SS] */
		if (len == 12 && sscanf(date_str, "%4u%2u%2u%2u%2u%c",
					&ptm->tm_year,
					&ptm->tm_mon,
					&ptm->tm_mday,
					&ptm->tm_hour,
					&ptm->tm_min,
					&end) >= 5) {
			ptm->tm_year -= 1900; /* Adjust years */
			ptm->tm_mon -= 1; /* Adjust month from 1-12 to 0-11 */
		} else {
			bb_error_msg_and_die(bb_msg_invalid_date, date_str);
		}
		ptm->tm_sec = 0; /* assume zero if [.SS] is not given */
		if (end == '.') {
			/* xxx.SS */
			if (sscanf(strchr(date_str, '.') + 1, "%u%c",
					&ptm->tm_sec, &end) == 1)
				end = '\0';
			/* else end != NUL and we error out */
		}
	}
	if (end != '\0') {
		bb_error_msg_and_die(bb_msg_invalid_date, date_str);
	}
}

time_t FAST_FUNC validate_tm_time(const char *date_str, struct tm *ptm)
{
	time_t t = mktime(ptm);
	if (t == (time_t) -1L) {
		bb_error_msg_and_die(bb_msg_invalid_date, date_str);
	}
	return t;
}

static char* strftime_fmt(char *buf, unsigned len, time_t *tp, const char *fmt)
{
	time_t t;
	if (!tp) {
		tp = &t;
		time(tp);
	}
	/* Returns pointer to NUL */
	return buf + strftime(buf, len, fmt, localtime(tp));
}

char* FAST_FUNC strftime_HHMMSS(char *buf, unsigned len, time_t *tp)
{
	return strftime_fmt(buf, len, tp, "%H:%M:%S");
}

char* FAST_FUNC strftime_YYYYMMDDHHMMSS(char *buf, unsigned len, time_t *tp)
{
	return strftime_fmt(buf, len, tp, "%Y-%m-%d %H:%M:%S");
}

#if ENABLE_MONOTONIC_SYSCALL

#include <sys/syscall.h>
/* Old glibc (< 2.3.4) does not provide this constant. We use syscall
 * directly so this definition is safe. */
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

/* libc has incredibly messy way of doing this,
 * typically requiring -lrt. We just skip all this mess */
static void get_mono(struct timespec *ts)
{
	if (syscall(__NR_clock_gettime, CLOCK_MONOTONIC, ts))
		bb_error_msg_and_die("clock_gettime(MONOTONIC) failed");
}
unsigned long long FAST_FUNC monotonic_ns(void)
{
	struct timespec ts;
	get_mono(&ts);
	return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}
unsigned long long FAST_FUNC monotonic_us(void)
{
	struct timespec ts;
	get_mono(&ts);
	return ts.tv_sec * 1000000ULL + ts.tv_nsec/1000;
}
unsigned long long FAST_FUNC monotonic_ms(void)
{
	struct timespec ts;
	get_mono(&ts);
	return ts.tv_sec * 1000ULL + ts.tv_nsec/1000000;
}
unsigned FAST_FUNC monotonic_sec(void)
{
	struct timespec ts;
	get_mono(&ts);
	return ts.tv_sec;
}

#else

unsigned long long FAST_FUNC monotonic_ns(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000000000ULL + tv.tv_usec * 1000;
}
unsigned long long FAST_FUNC monotonic_us(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000000ULL + tv.tv_usec;
}
unsigned long long FAST_FUNC monotonic_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}
unsigned FAST_FUNC monotonic_sec(void)
{
	return time(NULL);
}

#endif
