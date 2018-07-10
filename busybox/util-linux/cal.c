/* vi: set sw=4 ts=4: */
/*
 * Calendar implementation for busybox
 *
 * See original copyright at the end of this file
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* Mar 16, 2003      Manuel Novoa III   (mjn3@codepoet.org)
 *
 * Major size reduction... over 50% (>1.5k) on i386.
 */
//config:config CAL
//config:	bool "cal (6.5 kb)"
//config:	default y
//config:	help
//config:	cal is used to display a monthly calendar.

//applet:IF_CAL(APPLET_NOEXEC(cal, cal, BB_DIR_USR_BIN, BB_SUID_DROP, cal))
/* NOEXEC despite rare cases when it can be a "runner" (e.g. cal -n12000 takes you into years 30xx) */

//kbuild:lib-$(CONFIG_CAL) += cal.o

/* BB_AUDIT SUSv3 compliant with -j and -y extensions (from util-linux). */
/* BB_AUDIT BUG: The output of 'cal -j 1752' is incorrect.  The upstream
 * BB_AUDIT BUG: version in util-linux seems to be broken as well. */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/cal.html */

//usage:#define cal_trivial_usage
//usage:       "[-jy] [[MONTH] YEAR]"
//usage:#define cal_full_usage "\n\n"
//usage:       "Display a calendar\n"
//usage:     "\n	-j	Use julian dates"
//usage:     "\n	-y	Display the entire year"

#include "libbb.h"
#include "unicode.h"

/* We often use "unsigned" instead of "int", it's easier to div on most CPUs */

#define	THURSDAY		4		/* for reformation */
#define	SATURDAY		6		/* 1 Jan 1 was a Saturday */

#define	FIRST_MISSING_DAY	639787		/* 3 Sep 1752 */
#define	NUMBER_MISSING_DAYS	11		/* 11 day correction */

#define	MAXDAYS			42		/* max slots in a month array */
#define	SPACE			-1		/* used in day array */

static const unsigned char days_in_month[] ALIGN1 = {
	0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static const unsigned char sep1752[] ALIGN1 = {
		1,	2,	14,	15,	16,
	17,	18,	19,	20,	21,	22,	23,
	24,	25,	26,	27,	28,	29,	30
};

/* Set to 0 or 1 in main */
#define julian ((unsigned)option_mask32)

/* leap year -- account for Gregorian reformation in 1752 */
static int leap_year(unsigned yr)
{
	if (yr <= 1752)
		return !(yr % 4);
	return (!(yr % 4) && (yr % 100)) || !(yr % 400);
}

/* number of centuries since 1700, not inclusive */
#define	centuries_since_1700(yr) \
	((yr) > 1700 ? (yr) / 100 - 17 : 0)

/* number of centuries since 1700 whose modulo of 400 is 0 */
#define	quad_centuries_since_1700(yr) \
	((yr) > 1600 ? ((yr) - 1600) / 400 : 0)

/* number of leap years between year 1 and this year, not inclusive */
#define	leap_years_since_year_1(yr) \
	((yr) / 4 - centuries_since_1700(yr) + quad_centuries_since_1700(yr))

static void center(char *, unsigned, unsigned);
static void day_array(unsigned, unsigned, unsigned *);
static void trim_trailing_spaces_and_print(char *);

static void blank_string(char *buf, size_t buflen);
static char *build_row(char *p, unsigned *dp);

#define	DAY_LEN		3		/* 3 spaces per day */
#define	J_DAY_LEN	(DAY_LEN + 1)
#define	WEEK_LEN	20		/* 7 * 3 - one space at the end */
#define	J_WEEK_LEN	(WEEK_LEN + 7)
#define	HEAD_SEP	2		/* spaces between day headings */

int cal_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int cal_main(int argc UNUSED_PARAM, char **argv)
{
	struct tm zero_tm;
	time_t now;
	unsigned month, year, flags, i;
	char *month_names[12];
	/* normal heading: */
	/* "Su Mo Tu We Th Fr Sa" */
	/* -j heading: */
	/* " Su  Mo  Tu  We  Th  Fr  Sa" */
	char day_headings[ENABLE_UNICODE_SUPPORT ? 28 * 6 : 28];
	IF_UNICODE_SUPPORT(char *hp = day_headings;)
	char buf[40];

	init_unicode();

	flags = getopt32(argv, "jy");
	/* This sets julian = flags & 1: */
	option_mask32 &= 1;
	month = 0;
	argv += optind;

	if (!argv[0]) {
		struct tm *ptm;

		time(&now);
		ptm = localtime(&now);
		year = ptm->tm_year + 1900;
		if (!(flags & 2)) { /* no -y */
			month = ptm->tm_mon + 1;
		}
	} else {
		if (argv[1]) {
			if (argv[2]) {
				bb_show_usage();
			}
			if (!(flags & 2)) { /* no -y */
				month = xatou_range(*argv, 1, 12);
			}
			argv++;
		}
		year = xatou_range(*argv, 1, 9999);
	}

	blank_string(day_headings, sizeof(day_headings) - 7 + 7*julian);

	i = 0;
	do {
		zero_tm.tm_mon = i;
		/* full month name according to locale */
		strftime(buf, sizeof(buf), "%B", &zero_tm);
		month_names[i] = xstrdup(buf);

		if (i < 7) {
			zero_tm.tm_wday = i;
			/* abbreviated weekday name according to locale */
			strftime(buf, sizeof(buf), "%a", &zero_tm);
#if ENABLE_UNICODE_SUPPORT
			if (julian)
				*hp++ = ' ';
			{
				char *two_wchars = unicode_conv_to_printable_fixedwidth(/*NULL,*/ buf, 2);
				strcpy(hp, two_wchars);
				free(two_wchars);
			}
			hp += strlen(hp);
			*hp++ = ' ';
#else
			strncpy(day_headings + i * (3+julian) + julian, buf, 2);
#endif
		}
	} while (++i < 12);
	IF_UNICODE_SUPPORT(hp[-1] = '\0';)

	if (month) {
		unsigned row, len, days[MAXDAYS];
		unsigned *dp = days;
		char lineout[30];

		day_array(month, year, dp);
		len = sprintf(lineout, "%s %u", month_names[month - 1], year);
		printf("%*s%s\n%s\n",
				((7*julian + WEEK_LEN) - len) / 2, "",
				lineout, day_headings);
		for (row = 0; row < 6; row++) {
			build_row(lineout, dp)[0] = '\0';
			dp += 7;
			trim_trailing_spaces_and_print(lineout);
		}
	} else {
		unsigned row, which_cal, week_len, days[12][MAXDAYS];
		unsigned *dp;
		char lineout[80];

		sprintf(lineout, "%u", year);
		center(lineout,
				(WEEK_LEN * 3 + HEAD_SEP * 2)
				+ julian * (J_WEEK_LEN * 2 + HEAD_SEP
						- (WEEK_LEN * 3 + HEAD_SEP * 2)),
				0
		);
		puts("\n");		/* two \n's */
		for (i = 0; i < 12; i++) {
			day_array(i + 1, year, days[i]);
		}
		blank_string(lineout, sizeof(lineout));
		week_len = WEEK_LEN + julian * (J_WEEK_LEN - WEEK_LEN);
		for (month = 0; month < 12; month += 3-julian) {
			center(month_names[month], week_len, HEAD_SEP);
			if (!julian) {
				center(month_names[month + 1], week_len, HEAD_SEP);
			}
			center(month_names[month + 2 - julian], week_len, 0);
			printf("\n%s%*s%s", day_headings, HEAD_SEP, "", day_headings);
			if (!julian) {
				printf("%*s%s", HEAD_SEP, "", day_headings);
			}
			bb_putchar('\n');
			for (row = 0; row < (6*7); row += 7) {
				for (which_cal = 0; which_cal < 3-julian; which_cal++) {
					dp = days[month + which_cal] + row;
					build_row(lineout + which_cal * (week_len + 2), dp);
				}
				/* blank_string took care of nul termination. */
				trim_trailing_spaces_and_print(lineout);
			}
		}
	}

	fflush_stdout_and_exit(EXIT_SUCCESS);
}

/*
 * day_array --
 *	Fill in an array of 42 integers with a calendar.  Assume for a moment
 *	that you took the (maximum) 6 rows in a calendar and stretched them
 *	out end to end.  You would have 42 numbers or spaces.  This routine
 *	builds that array for any month from Jan. 1 through Dec. 9999.
 */
static void day_array(unsigned month, unsigned year, unsigned *days)
{
	unsigned long temp;
	unsigned i;
	unsigned day, dw, dm;

	memset(days, SPACE, MAXDAYS * sizeof(int));

	if ((month == 9) && (year == 1752)) {
		/* Assumes the Gregorian reformation eliminates
		 * 3 Sep. 1752 through 13 Sep. 1752.
		 */
		unsigned j_offset = julian * 244;
		size_t oday = 0;

		do {
			days[oday+2] = sep1752[oday] + j_offset;
		} while (++oday < sizeof(sep1752));

		return;
	}

	/* day_in_year
	 * return the 1 based day number within the year
	 */
	day = 1;
	if ((month > 2) && leap_year(year)) {
		++day;
	}

	i = month;
	while (i) {
		day += days_in_month[--i];
	}

	/* day_in_week
	 * return the 0 based day number for any date from 1 Jan. 1 to
	 * 31 Dec. 9999.  Assumes the Gregorian reformation eliminates
	 * 3 Sep. 1752 through 13 Sep. 1752.  Returns Thursday for all
	 * missing days.
	 */
	temp = (long)(year - 1) * 365 + leap_years_since_year_1(year - 1) + day;
	if (temp < FIRST_MISSING_DAY) {
		dw = ((temp - 1 + SATURDAY) % 7);
	} else {
		dw = (((temp - 1 + SATURDAY) - NUMBER_MISSING_DAYS) % 7);
	}

	if (!julian) {
		day = 1;
	}

	dm = days_in_month[month];
	if ((month == 2) && leap_year(year)) {
		++dm;
	}

	do {
		days[dw++] = day++;
	} while (--dm);
}

static void trim_trailing_spaces_and_print(char *s)
{
	char *p = s;

	while (*p) {
		++p;
	}
	while (p != s) {
		--p;
		if (!isspace(*p)) {
			p[1] = '\0';
			break;
		}
	}

	puts(s);
}

static void center(char *str, unsigned len, unsigned separate)
{
	unsigned n = strlen(str);
	len -= n;
	printf("%*s%*s", (len/2) + n, str, (len/2) + (len % 2) + separate, "");
}

static void blank_string(char *buf, size_t buflen)
{
	memset(buf, ' ', buflen);
	buf[buflen-1] = '\0';
}

static char *build_row(char *p, unsigned *dp)
{
	unsigned col, val, day;

	memset(p, ' ', (julian + DAY_LEN) * 7);

	col = 0;
	do {
		day = *dp++;
		if (day != SPACE) {
			if (julian) {
				++p;
				if (day >= 100) {
					*p = '0';
					p[-1] = (day / 100) + '0';
					day %= 100;
				}
			}
			val = day / 10;
			if (val > 0) {
				*p = val + '0';
			}
			*++p = day % 10 + '0';
			p += 2;
		} else {
			p += DAY_LEN + julian;
		}
	} while (++col < 7);

	return p;
}

/*
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kim Letkeman.
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND
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
