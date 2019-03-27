/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * apr_date.c: date parsing utility routines
 *     These routines are (hopefully) platform independent.
 * 
 * 27 Oct 1996  Roy Fielding
 *     Extracted (with many modifications) from mod_proxy.c and
 *     tested with over 50,000 randomly chosen valid date strings
 *     and several hundred variations of invalid date strings.
 * 
 */

#include "apr.h"
#include "apr_lib.h"

#define APR_WANT_STRFUNC
#include "apr_want.h"

#if APR_HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if APR_HAVE_CTYPE_H
#include <ctype.h>
#endif

#include "apr_date.h"

/*
 * Compare a string to a mask
 * Mask characters (arbitrary maximum is 256 characters, just in case):
 *   @ - uppercase letter
 *   $ - lowercase letter
 *   & - hex digit
 *   # - digit
 *   ~ - digit or space
 *   * - swallow remaining characters 
 *  <x> - exact match for any other character
 */
APU_DECLARE(int) apr_date_checkmask(const char *data, const char *mask)
{
    int i;
    char d;

    for (i = 0; i < 256; i++) {
        d = data[i];
        switch (mask[i]) {
        case '\0':
            return (d == '\0');

        case '*':
            return 1;

        case '@':
            if (!apr_isupper(d))
                return 0;
            break;
        case '$':
            if (!apr_islower(d))
                return 0;
            break;
        case '#':
            if (!apr_isdigit(d))
                return 0;
            break;
        case '&':
            if (!apr_isxdigit(d))
                return 0;
            break;
        case '~':
            if ((d != ' ') && !apr_isdigit(d))
                return 0;
            break;
        default:
            if (mask[i] != d)
                return 0;
            break;
        }
    }
    return 0;          /* We only get here if mask is corrupted (exceeds 256) */
}

/*
 * Parses an HTTP date in one of three standard forms:
 *
 *     Sun, 06 Nov 1994 08:49:37 GMT  ; RFC 822, updated by RFC 1123
 *     Sunday, 06-Nov-94 08:49:37 GMT ; RFC 850, obsoleted by RFC 1036
 *     Sun Nov  6 08:49:37 1994       ; ANSI C's asctime() format
 *
 * and returns the apr_time_t number of microseconds since 1 Jan 1970 GMT, 
 * or APR_DATE_BAD if this would be out of range or if the date is invalid.
 *
 * The restricted HTTP syntax is
 * 
 *     HTTP-date    = rfc1123-date | rfc850-date | asctime-date
 *
 *     rfc1123-date = wkday "," SP date1 SP time SP "GMT"
 *     rfc850-date  = weekday "," SP date2 SP time SP "GMT"
 *     asctime-date = wkday SP date3 SP time SP 4DIGIT
 *
 *     date1        = 2DIGIT SP month SP 4DIGIT
 *                    ; day month year (e.g., 02 Jun 1982)
 *     date2        = 2DIGIT "-" month "-" 2DIGIT
 *                    ; day-month-year (e.g., 02-Jun-82)
 *     date3        = month SP ( 2DIGIT | ( SP 1DIGIT ))
 *                    ; month day (e.g., Jun  2)
 *
 *     time         = 2DIGIT ":" 2DIGIT ":" 2DIGIT
 *                    ; 00:00:00 - 23:59:59
 *
 *     wkday        = "Mon" | "Tue" | "Wed"
 *                  | "Thu" | "Fri" | "Sat" | "Sun"
 *
 *     weekday      = "Monday" | "Tuesday" | "Wednesday"
 *                  | "Thursday" | "Friday" | "Saturday" | "Sunday"
 *
 *     month        = "Jan" | "Feb" | "Mar" | "Apr"
 *                  | "May" | "Jun" | "Jul" | "Aug"
 *                  | "Sep" | "Oct" | "Nov" | "Dec"
 *
 * However, for the sake of robustness (and Netscapeness), we ignore the
 * weekday and anything after the time field (including the timezone).
 *
 * This routine is intended to be very fast; 10x faster than using sscanf.
 *
 * Originally from Andrew Daviel <andrew@vancouver-webpages.com>, 29 Jul 96
 * but many changes since then.
 *
 */
APU_DECLARE(apr_time_t) apr_date_parse_http(const char *date)
{
    apr_time_exp_t ds;
    apr_time_t result;
    int mint, mon;
    const char *monstr, *timstr;
    static const int months[12] =
    {
    ('J' << 16) | ('a' << 8) | 'n', ('F' << 16) | ('e' << 8) | 'b',
    ('M' << 16) | ('a' << 8) | 'r', ('A' << 16) | ('p' << 8) | 'r',
    ('M' << 16) | ('a' << 8) | 'y', ('J' << 16) | ('u' << 8) | 'n',
    ('J' << 16) | ('u' << 8) | 'l', ('A' << 16) | ('u' << 8) | 'g',
    ('S' << 16) | ('e' << 8) | 'p', ('O' << 16) | ('c' << 8) | 't',
    ('N' << 16) | ('o' << 8) | 'v', ('D' << 16) | ('e' << 8) | 'c'};

    if (!date)
        return APR_DATE_BAD;

    while (*date && apr_isspace(*date))    /* Find first non-whitespace char */
        ++date;

    if (*date == '\0') 
        return APR_DATE_BAD;

    if ((date = strchr(date, ' ')) == NULL)       /* Find space after weekday */
        return APR_DATE_BAD;

    ++date;        /* Now pointing to first char after space, which should be */

    /* start of the actual date information for all 4 formats. */

    if (apr_date_checkmask(date, "## @$$ #### ##:##:## *")) {
        /* RFC 1123 format with two days */
        ds.tm_year = ((date[7] - '0') * 10 + (date[8] - '0') - 19) * 100;
        if (ds.tm_year < 0)
            return APR_DATE_BAD;

        ds.tm_year += ((date[9] - '0') * 10) + (date[10] - '0');

        ds.tm_mday = ((date[0] - '0') * 10) + (date[1] - '0');

        monstr = date + 3;
        timstr = date + 12;
    }
    else if (apr_date_checkmask(date, "##-@$$-## ##:##:## *")) { 
        /* RFC 850 format */
        ds.tm_year = ((date[7] - '0') * 10) + (date[8] - '0');
        if (ds.tm_year < 70)
            ds.tm_year += 100;

        ds.tm_mday = ((date[0] - '0') * 10) + (date[1] - '0');

        monstr = date + 3;
        timstr = date + 10;
    }
    else if (apr_date_checkmask(date, "@$$ ~# ##:##:## ####*")) {
        /* asctime format */
        ds.tm_year = ((date[16] - '0') * 10 + (date[17] - '0') - 19) * 100;
        if (ds.tm_year < 0) 
            return APR_DATE_BAD;

        ds.tm_year += ((date[18] - '0') * 10) + (date[19] - '0');

        if (date[4] == ' ')
            ds.tm_mday = 0;
        else
            ds.tm_mday = (date[4] - '0') * 10;

        ds.tm_mday += (date[5] - '0');

        monstr = date;
        timstr = date + 7;
    }
    else if (apr_date_checkmask(date, "# @$$ #### ##:##:## *")) {
        /* RFC 1123 format with one day */
        ds.tm_year = ((date[6] - '0') * 10 + (date[7] - '0') - 19) * 100;
        if (ds.tm_year < 0)
            return APR_DATE_BAD;

        ds.tm_year += ((date[8] - '0') * 10) + (date[9] - '0');

        ds.tm_mday = (date[0] - '0');

        monstr = date + 2;
        timstr = date + 11;
    }
    else
        return APR_DATE_BAD;

    if (ds.tm_mday <= 0 || ds.tm_mday > 31)
        return APR_DATE_BAD;

    ds.tm_hour = ((timstr[0] - '0') * 10) + (timstr[1] - '0');
    ds.tm_min = ((timstr[3] - '0') * 10) + (timstr[4] - '0');
    ds.tm_sec = ((timstr[6] - '0') * 10) + (timstr[7] - '0');

    if ((ds.tm_hour > 23) || (ds.tm_min > 59) || (ds.tm_sec > 61)) 
        return APR_DATE_BAD;

    mint = (monstr[0] << 16) | (monstr[1] << 8) | monstr[2];
    for (mon = 0; mon < 12; mon++)
        if (mint == months[mon])
            break;

    if (mon == 12)
        return APR_DATE_BAD;

    if ((ds.tm_mday == 31) && (mon == 3 || mon == 5 || mon == 8 || mon == 10))
        return APR_DATE_BAD;

    /* February gets special check for leapyear */
    if ((mon == 1) &&
        ((ds.tm_mday > 29) || 
        ((ds.tm_mday == 29)
        && ((ds.tm_year & 3)
        || (((ds.tm_year % 100) == 0)
        && (((ds.tm_year % 400) != 100)))))))
        return APR_DATE_BAD;

    ds.tm_mon = mon;

    /* ap_mplode_time uses tm_usec and tm_gmtoff fields, but they haven't 
     * been set yet. 
     * It should be safe to just zero out these values.
     * tm_usec is the number of microseconds into the second.  HTTP only
     * cares about second granularity.
     * tm_gmtoff is the number of seconds off of GMT the time is.  By
     * definition all times going through this function are in GMT, so this
     * is zero. 
     */
    ds.tm_usec = 0;
    ds.tm_gmtoff = 0;
    if (apr_time_exp_get(&result, &ds) != APR_SUCCESS) 
        return APR_DATE_BAD;
    
    return result;
}

/*
 * Parses a string resembling an RFC 822 date.  This is meant to be
 * leinent in its parsing of dates.  Hence, this will parse a wider 
 * range of dates than apr_date_parse_http.
 *
 * The prominent mailer (or poster, if mailer is unknown) that has
 * been seen in the wild is included for the unknown formats.
 *
 *     Sun, 06 Nov 1994 08:49:37 GMT  ; RFC 822, updated by RFC 1123
 *     Sunday, 06-Nov-94 08:49:37 GMT ; RFC 850, obsoleted by RFC 1036
 *     Sun Nov  6 08:49:37 1994       ; ANSI C's asctime() format
 *     Sun, 6 Nov 1994 08:49:37 GMT   ; RFC 822, updated by RFC 1123
 *     Sun, 06 Nov 94 08:49:37 GMT    ; RFC 822
 *     Sun, 6 Nov 94 08:49:37 GMT     ; RFC 822
 *     Sun, 06 Nov 94 08:49 GMT       ; Unknown [drtr@ast.cam.ac.uk] 
 *     Sun, 6 Nov 94 08:49 GMT        ; Unknown [drtr@ast.cam.ac.uk]
 *     Sun, 06 Nov 94 8:49:37 GMT     ; Unknown [Elm 70.85]
 *     Sun, 6 Nov 94 8:49:37 GMT      ; Unknown [Elm 70.85] 
 *     Mon,  7 Jan 2002 07:21:22 GMT  ; Unknown [Postfix]
 *     Sun, 06-Nov-1994 08:49:37 GMT  ; RFC 850 with four digit years
 *
 */

#define TIMEPARSE(ds,hr10,hr1,min10,min1,sec10,sec1)        \
    {                                                       \
        ds.tm_hour = ((hr10 - '0') * 10) + (hr1 - '0');     \
        ds.tm_min = ((min10 - '0') * 10) + (min1 - '0');    \
        ds.tm_sec = ((sec10 - '0') * 10) + (sec1 - '0');    \
    }
#define TIMEPARSE_STD(ds,timstr)                            \
    {                                                       \
        TIMEPARSE(ds, timstr[0],timstr[1],                  \
                      timstr[3],timstr[4],                  \
                      timstr[6],timstr[7]);                 \
    }

APU_DECLARE(apr_time_t) apr_date_parse_rfc(const char *date)
{
    apr_time_exp_t ds;
    apr_time_t result;
    int mint, mon;
    const char *monstr, *timstr, *gmtstr;
    static const int months[12] =
    {
    ('J' << 16) | ('a' << 8) | 'n', ('F' << 16) | ('e' << 8) | 'b',
    ('M' << 16) | ('a' << 8) | 'r', ('A' << 16) | ('p' << 8) | 'r',
    ('M' << 16) | ('a' << 8) | 'y', ('J' << 16) | ('u' << 8) | 'n',
    ('J' << 16) | ('u' << 8) | 'l', ('A' << 16) | ('u' << 8) | 'g',
    ('S' << 16) | ('e' << 8) | 'p', ('O' << 16) | ('c' << 8) | 't',
    ('N' << 16) | ('o' << 8) | 'v', ('D' << 16) | ('e' << 8) | 'c' };

    if (!date)
        return APR_DATE_BAD;

    /* Not all dates have text days at the beginning. */
    if (!apr_isdigit(date[0]))
    {
        while (*date && apr_isspace(*date)) /* Find first non-whitespace char */
            ++date;

        if (*date == '\0') 
            return APR_DATE_BAD;

        if ((date = strchr(date, ' ')) == NULL)   /* Find space after weekday */
            return APR_DATE_BAD;

        ++date;    /* Now pointing to first char after space, which should be */    }

    /* start of the actual date information for all 11 formats. */
    if (apr_date_checkmask(date, "## @$$ #### ##:##:## *")) {   /* RFC 1123 format */
        ds.tm_year = ((date[7] - '0') * 10 + (date[8] - '0') - 19) * 100;

        if (ds.tm_year < 0)
            return APR_DATE_BAD;

        ds.tm_year += ((date[9] - '0') * 10) + (date[10] - '0');

        ds.tm_mday = ((date[0] - '0') * 10) + (date[1] - '0');

        monstr = date + 3;
        timstr = date + 12;
        gmtstr = date + 21;

        TIMEPARSE_STD(ds, timstr);
    }
    else if (apr_date_checkmask(date, "##-@$$-## ##:##:## *")) {/* RFC 850 format  */
        ds.tm_year = ((date[7] - '0') * 10) + (date[8] - '0');

        if (ds.tm_year < 70)
            ds.tm_year += 100;

        ds.tm_mday = ((date[0] - '0') * 10) + (date[1] - '0');

        monstr = date + 3;
        timstr = date + 10;
        gmtstr = date + 19;

        TIMEPARSE_STD(ds, timstr);
    }
    else if (apr_date_checkmask(date, "@$$ ~# ##:##:## ####*")) {
        /* asctime format */
        ds.tm_year = ((date[16] - '0') * 10 + (date[17] - '0') - 19) * 100;
        if (ds.tm_year < 0) 
            return APR_DATE_BAD;

        ds.tm_year += ((date[18] - '0') * 10) + (date[19] - '0');

        if (date[4] == ' ')
            ds.tm_mday = 0;
        else
            ds.tm_mday = (date[4] - '0') * 10;

        ds.tm_mday += (date[5] - '0');

        monstr = date;
        timstr = date + 7;
        gmtstr = NULL;

        TIMEPARSE_STD(ds, timstr);
    }
    else if (apr_date_checkmask(date, "# @$$ #### ##:##:## *")) {
        /* RFC 1123 format*/
        ds.tm_year = ((date[6] - '0') * 10 + (date[7] - '0') - 19) * 100;

        if (ds.tm_year < 0)
            return APR_DATE_BAD;

        ds.tm_year += ((date[8] - '0') * 10) + (date[9] - '0');
        ds.tm_mday = (date[0] - '0');

        monstr = date + 2;
        timstr = date + 11;
        gmtstr = date + 20;

        TIMEPARSE_STD(ds, timstr);
    }
    else if (apr_date_checkmask(date, "## @$$ ## ##:##:## *")) {
        /* This is the old RFC 1123 date format - many many years ago, people
         * used two-digit years.  Oh, how foolish.
         *
         * Two-digit day, two-digit year version. */
        ds.tm_year = ((date[7] - '0') * 10) + (date[8] - '0');

        if (ds.tm_year < 70)
            ds.tm_year += 100;

        ds.tm_mday = ((date[0] - '0') * 10) + (date[1] - '0');

        monstr = date + 3;
        timstr = date + 10;
        gmtstr = date + 19;

        TIMEPARSE_STD(ds, timstr);
    } 
    else if (apr_date_checkmask(date, " # @$$ ## ##:##:## *")) {
        /* This is the old RFC 1123 date format - many many years ago, people
         * used two-digit years.  Oh, how foolish.
         *
         * Space + one-digit day, two-digit year version.*/
        ds.tm_year = ((date[7] - '0') * 10) + (date[8] - '0');

        if (ds.tm_year < 70)
            ds.tm_year += 100;

        ds.tm_mday = (date[1] - '0');

        monstr = date + 3;
        timstr = date + 10;
        gmtstr = date + 19;

        TIMEPARSE_STD(ds, timstr);
    } 
    else if (apr_date_checkmask(date, "# @$$ ## ##:##:## *")) {
        /* This is the old RFC 1123 date format - many many years ago, people
         * used two-digit years.  Oh, how foolish.
         *
         * One-digit day, two-digit year version. */
        ds.tm_year = ((date[6] - '0') * 10) + (date[7] - '0');

        if (ds.tm_year < 70)
            ds.tm_year += 100;

        ds.tm_mday = (date[0] - '0');

        monstr = date + 2;
        timstr = date + 9;
        gmtstr = date + 18;

        TIMEPARSE_STD(ds, timstr);
    } 
    else if (apr_date_checkmask(date, "## @$$ ## ##:## *")) {
        /* Loser format.  This is quite bogus.  */
        ds.tm_year = ((date[7] - '0') * 10) + (date[8] - '0');

        if (ds.tm_year < 70)
            ds.tm_year += 100;

        ds.tm_mday = ((date[0] - '0') * 10) + (date[1] - '0');

        monstr = date + 3;
        timstr = date + 10;
        gmtstr = NULL;

        TIMEPARSE(ds, timstr[0],timstr[1], timstr[3],timstr[4], '0','0');
    } 
    else if (apr_date_checkmask(date, "# @$$ ## ##:## *")) {
        /* Loser format.  This is quite bogus.  */
        ds.tm_year = ((date[6] - '0') * 10) + (date[7] - '0');

        if (ds.tm_year < 70)
            ds.tm_year += 100;

        ds.tm_mday = (date[0] - '0');

        monstr = date + 2;
        timstr = date + 9;
        gmtstr = NULL;

        TIMEPARSE(ds, timstr[0],timstr[1], timstr[3],timstr[4], '0','0');
    }
    else if (apr_date_checkmask(date, "## @$$ ## #:##:## *")) {
        /* Loser format.  This is quite bogus.  */
        ds.tm_year = ((date[7] - '0') * 10) + (date[8] - '0');

        if (ds.tm_year < 70)
            ds.tm_year += 100;

        ds.tm_mday = ((date[0] - '0') * 10) + (date[1] - '0');

        monstr = date + 3;
        timstr = date + 9;
        gmtstr = date + 18;

        TIMEPARSE(ds, '0',timstr[1], timstr[3],timstr[4], timstr[6],timstr[7]);
    }
    else if (apr_date_checkmask(date, "# @$$ ## #:##:## *")) {
         /* Loser format.  This is quite bogus.  */
        ds.tm_year = ((date[6] - '0') * 10) + (date[7] - '0');

        if (ds.tm_year < 70)
            ds.tm_year += 100;

        ds.tm_mday = (date[0] - '0');

        monstr = date + 2;
        timstr = date + 8;
        gmtstr = date + 17;

        TIMEPARSE(ds, '0',timstr[1], timstr[3],timstr[4], timstr[6],timstr[7]);
    }
    else if (apr_date_checkmask(date, " # @$$ #### ##:##:## *")) {   
        /* RFC 1123 format with a space instead of a leading zero. */
        ds.tm_year = ((date[7] - '0') * 10 + (date[8] - '0') - 19) * 100;

        if (ds.tm_year < 0)
            return APR_DATE_BAD;

        ds.tm_year += ((date[9] - '0') * 10) + (date[10] - '0');

        ds.tm_mday = (date[1] - '0');

        monstr = date + 3;
        timstr = date + 12;
        gmtstr = date + 21;

        TIMEPARSE_STD(ds, timstr);
    }
    else if (apr_date_checkmask(date, "##-@$$-#### ##:##:## *")) {
       /* RFC 1123 with dashes instead of spaces between date/month/year
        * This also looks like RFC 850 with four digit years.
        */
        ds.tm_year = ((date[7] - '0') * 10 + (date[8] - '0') - 19) * 100;
        if (ds.tm_year < 0)
            return APR_DATE_BAD;

        ds.tm_year += ((date[9] - '0') * 10) + (date[10] - '0');

        ds.tm_mday = ((date[0] - '0') * 10) + (date[1] - '0');

        monstr = date + 3;
        timstr = date + 12;
        gmtstr = date + 21;

        TIMEPARSE_STD(ds, timstr);
    }
    else
        return APR_DATE_BAD;

    if (ds.tm_mday <= 0 || ds.tm_mday > 31)
        return APR_DATE_BAD;

    if ((ds.tm_hour > 23) || (ds.tm_min > 59) || (ds.tm_sec > 61)) 
        return APR_DATE_BAD;

    mint = (monstr[0] << 16) | (monstr[1] << 8) | monstr[2];
    for (mon = 0; mon < 12; mon++)
        if (mint == months[mon])
            break;

    if (mon == 12)
        return APR_DATE_BAD;

    if ((ds.tm_mday == 31) && (mon == 3 || mon == 5 || mon == 8 || mon == 10))
        return APR_DATE_BAD;

    /* February gets special check for leapyear */

    if ((mon == 1) &&
        ((ds.tm_mday > 29)
        || ((ds.tm_mday == 29)
        && ((ds.tm_year & 3)
        || (((ds.tm_year % 100) == 0)
        && (((ds.tm_year % 400) != 100)))))))
        return APR_DATE_BAD;

    ds.tm_mon = mon;

    /* tm_gmtoff is the number of seconds off of GMT the time is.
     *
     * We only currently support: [+-]ZZZZ where Z is the offset in
     * hours from GMT.
     *
     * If there is any confusion, tm_gmtoff will remain 0.
     */
    ds.tm_gmtoff = 0;

    /* Do we have a timezone ? */
    if (gmtstr) {
        int offset;
        switch (*gmtstr) {
        case '-':
            offset = atoi(gmtstr+1);
            ds.tm_gmtoff -= (offset / 100) * 60 * 60;
            ds.tm_gmtoff -= (offset % 100) * 60;
            break;
        case '+':
            offset = atoi(gmtstr+1);
            ds.tm_gmtoff += (offset / 100) * 60 * 60;
            ds.tm_gmtoff += (offset % 100) * 60;
            break;
        }
    }

    /* apr_time_exp_get uses tm_usec field, but it hasn't been set yet. 
     * It should be safe to just zero out this value.
     * tm_usec is the number of microseconds into the second.  HTTP only
     * cares about second granularity.
     */
    ds.tm_usec = 0;

    if (apr_time_exp_gmt_get(&result, &ds) != APR_SUCCESS) 
        return APR_DATE_BAD;
    
    return result;
}
