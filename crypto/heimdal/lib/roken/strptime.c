/*
 * Copyright (c) 1999, 2003, 2005 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of KTH nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY KTH AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL KTH OR ITS CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include <config.h>
#include "roken.h"
#ifdef TEST_STRPFTIME
#include "strpftime-test.h"
#endif
#include <ctype.h>

static const char *abb_weekdays[] = {
    "Sun",
    "Mon",
    "Tue",
    "Wed",
    "Thu",
    "Fri",
    "Sat",
    NULL
};

static const char *full_weekdays[] = {
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday",
    NULL
};

static const char *abb_month[] = {
    "Jan",
    "Feb",
    "Mar",
    "Apr",
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sep",
    "Oct",
    "Nov",
    "Dec",
    NULL
};

static const char *full_month[] = {
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December",
    NULL,
};

static const char *ampm[] = {
    "am",
    "pm",
    NULL
};

/*
 * Try to match `*buf' to one of the strings in `strs'.  Return the
 * index of the matching string (or -1 if none).  Also advance buf.
 */

static int
match_string (const char **buf, const char **strs)
{
    int i = 0;

    for (i = 0; strs[i] != NULL; ++i) {
	int len = strlen (strs[i]);

	if (strncasecmp (*buf, strs[i], len) == 0) {
	    *buf += len;
	    return i;
	}
    }
    return -1;
}

/*
 * Try to match `*buf' to at the most `n' characters and return the
 * resulting number in `num'. Returns 0 or an error.  Also advance
 * buf.
 */

static int
parse_number (const char **buf, int n, int *num)
{
    char *s, *str;
    int i;

    str = malloc(n + 1);
    if (str == NULL)
	return -1;

    /* skip whitespace */
    for (; **buf != '\0' && isspace((unsigned char)(**buf)); (*buf)++)
	;

    /* parse at least n characters */
    for (i = 0; **buf != '\0' && i < n && isdigit((unsigned char)(**buf)); i++, (*buf)++)
	str[i] = **buf;
    str[i] = '\0';

    *num = strtol (str, &s, 10);
    free(str);
    if (s == str)
	return -1;

    return 0;
}

/*
 * tm_year is relative this year
 */

const int tm_year_base = 1900;

/*
 * Return TRUE iff `year' was a leap year.
 */

static int
is_leap_year (int year)
{
    return (year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0);
}

/*
 * Return the weekday [0,6] (0 = Sunday) of the first day of `year'
 */

static int
first_day (int year)
{
    int ret = 4;

    for (; year > 1970; --year)
	ret = (ret + (is_leap_year (year) ? 366 : 365)) % 7;
    return ret;
}

/*
 * Set `timeptr' given `wnum' (week number [0, 53])
 */

static void
set_week_number_sun (struct tm *timeptr, int wnum)
{
    int fday = first_day (timeptr->tm_year + tm_year_base);

    timeptr->tm_yday = wnum * 7 + timeptr->tm_wday - fday;
    if (timeptr->tm_yday < 0) {
	timeptr->tm_wday = fday;
	timeptr->tm_yday = 0;
    }
}

/*
 * Set `timeptr' given `wnum' (week number [0, 53])
 */

static void
set_week_number_mon (struct tm *timeptr, int wnum)
{
    int fday = (first_day (timeptr->tm_year + tm_year_base) + 6) % 7;

    timeptr->tm_yday = wnum * 7 + (timeptr->tm_wday + 6) % 7 - fday;
    if (timeptr->tm_yday < 0) {
	timeptr->tm_wday = (fday + 1) % 7;
	timeptr->tm_yday = 0;
    }
}

/*
 * Set `timeptr' given `wnum' (week number [0, 53])
 */

static void
set_week_number_mon4 (struct tm *timeptr, int wnum)
{
    int fday = (first_day (timeptr->tm_year + tm_year_base) + 6) % 7;
    int offset = 0;

    if (fday < 4)
	offset += 7;

    timeptr->tm_yday = offset + (wnum - 1) * 7 + timeptr->tm_wday - fday;
    if (timeptr->tm_yday < 0) {
	timeptr->tm_wday = fday;
	timeptr->tm_yday = 0;
    }
}

/*
 *
 */

ROKEN_LIB_FUNCTION char * ROKEN_LIB_CALL
strptime (const char *buf, const char *format, struct tm *timeptr)
{
    char c;

    for (; (c = *format) != '\0'; ++format) {
	char *s;
	int ret;

	if (isspace ((unsigned char)c)) {
	    while (isspace ((unsigned char)*buf))
		++buf;
	} else if (c == '%' && format[1] != '\0') {
	    c = *++format;
	    if (c == 'E' || c == 'O')
		c = *++format;
	    switch (c) {
	    case 'A' :
		ret = match_string (&buf, full_weekdays);
		if (ret < 0)
		    return NULL;
		timeptr->tm_wday = ret;
		break;
	    case 'a' :
		ret = match_string (&buf, abb_weekdays);
		if (ret < 0)
		    return NULL;
		timeptr->tm_wday = ret;
		break;
	    case 'B' :
		ret = match_string (&buf, full_month);
		if (ret < 0)
		    return NULL;
		timeptr->tm_mon = ret;
		break;
	    case 'b' :
	    case 'h' :
		ret = match_string (&buf, abb_month);
		if (ret < 0)
		    return NULL;
		timeptr->tm_mon = ret;
		break;
	    case 'C' :
		if (parse_number(&buf, 2, &ret))
		    return NULL;
		timeptr->tm_year = (ret * 100) - tm_year_base;
		break;
	    case 'c' :
		abort ();
	    case 'D' :		/* %m/%d/%y */
		s = strptime (buf, "%m/%d/%y", timeptr);
		if (s == NULL)
		    return NULL;
		buf = s;
		break;
	    case 'd' :
	    case 'e' :
		if (parse_number(&buf, 2, &ret))
		    return NULL;
		timeptr->tm_mday = ret;
		break;
	    case 'H' :
	    case 'k' :
		if (parse_number(&buf, 2, &ret))
		    return NULL;
		timeptr->tm_hour = ret;
		break;
	    case 'I' :
	    case 'l' :
		if (parse_number(&buf, 2, &ret))
		    return NULL;
		if (ret == 12)
		    timeptr->tm_hour = 0;
		else
		    timeptr->tm_hour = ret;
		break;
	    case 'j' :
		if (parse_number(&buf, 3, &ret))
		    return NULL;
		if (ret == 0)
		    return NULL;
		timeptr->tm_yday = ret - 1;
		break;
	    case 'm' :
		if (parse_number(&buf, 2, &ret))
		    return NULL;
		if (ret == 0)
		    return NULL;
		timeptr->tm_mon = ret - 1;
		break;
	    case 'M' :
		if (parse_number(&buf, 2, &ret))
		    return NULL;
		timeptr->tm_min = ret;
		break;
	    case 'n' :
		while (isspace ((unsigned char)*buf))
		    buf++;
		break;
	    case 'p' :
		ret = match_string (&buf, ampm);
		if (ret < 0)
		    return NULL;
		if (timeptr->tm_hour == 0) {
		    if (ret == 1)
			timeptr->tm_hour = 12;
		} else
		    timeptr->tm_hour += 12;
		break;
	    case 'r' :		/* %I:%M:%S %p */
		s = strptime (buf, "%I:%M:%S %p", timeptr);
		if (s == NULL)
		    return NULL;
		buf = s;
		break;
	    case 'R' :		/* %H:%M */
		s = strptime (buf, "%H:%M", timeptr);
		if (s == NULL)
		    return NULL;
		buf = s;
		break;
	    case 'S' :
		if (parse_number(&buf, 2, &ret))
		    return NULL;
		timeptr->tm_sec = ret;
		break;
	    case 't' :
		while (isspace ((unsigned char)*buf))
		    buf++;
		break;
	    case 'T' :		/* %H:%M:%S */
	    case 'X' :
		s = strptime (buf, "%H:%M:%S", timeptr);
		if (s == NULL)
		    return NULL;
		buf = s;
		break;
	    case 'u' :
		if (parse_number(&buf, 1, &ret))
		    return NULL;
		if (ret <= 0)
		    return NULL;
		timeptr->tm_wday = ret - 1;
		break;
	    case 'w' :
		if (parse_number(&buf, 1, &ret))
		    return NULL;
		timeptr->tm_wday = ret;
		break;
	    case 'U' :
		if (parse_number(&buf, 2, &ret))
		    return NULL;
		set_week_number_sun (timeptr, ret);
		break;
	    case 'V' :
		if (parse_number(&buf, 2, &ret))
		    return NULL;
		set_week_number_mon4 (timeptr, ret);
		break;
	    case 'W' :
		if (parse_number(&buf, 2, &ret))
		    return NULL;
		set_week_number_mon (timeptr, ret);
		break;
	    case 'x' :
		s = strptime (buf, "%Y:%m:%d", timeptr);
		if (s == NULL)
		    return NULL;
		buf = s;
		break;
	    case 'y' :
		if (parse_number(&buf, 2, &ret))
		    return NULL;
		if (ret < 70)
		    timeptr->tm_year = 100 + ret;
		else
		    timeptr->tm_year = ret;
		break;
	    case 'Y' :
		if (parse_number(&buf, 4, &ret))
		    return NULL;
		timeptr->tm_year = ret - tm_year_base;
		break;
	    case 'Z' :
		abort ();
	    case '\0' :
		--format;
		/* FALLTHROUGH */
	    case '%' :
		if (*buf == '%')
		    ++buf;
		else
		    return NULL;
		break;
	    default :
		if (*buf == '%' || *++buf == c)
		    ++buf;
		else
		    return NULL;
		break;
	    }
	} else {
	    if (*buf == c)
		++buf;
	    else
		return NULL;
	}
    }
    return rk_UNCONST(buf);
}
