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

#include "abts.h"
#include "testutil.h"
#include "apr_date.h"
#include "apr_general.h"

#if APR_HAVE_TIME_H
#include <time.h>
#endif /* APR_HAVE_TIME_H */

static struct datetest {
  const char *input;
  const char *output;
} tests[] = {
  { "Mon, 27 Feb 1995 20:49:44 -0800",  "Tue, 28 Feb 1995 04:49:44 GMT" },
  { "Fri,  1 Jul 2005 11:34:25 -0400",  "Fri, 01 Jul 2005 15:34:25 GMT" },
  { "Monday, 27-Feb-95 20:49:44 -0800", "Tue, 28 Feb 1995 04:49:44 GMT" },
  { "Tue, 4 Mar 1997 12:43:52 +0200",   "Tue, 04 Mar 1997 10:43:52 GMT" },
  { "Mon, 27 Feb 95 20:49:44 -0800",    "Tue, 28 Feb 1995 04:49:44 GMT" },
  { "Tue,  4 Mar 97 12:43:52 +0200",    "Tue, 04 Mar 1997 10:43:52 GMT" },
  { "Tue, 4 Mar 97 12:43:52 +0200",     "Tue, 04 Mar 1997 10:43:52 GMT" },
  { "Mon, 27 Feb 95 20:49 GMT",         "Mon, 27 Feb 1995 20:49:00 GMT" },
  { "Tue, 4 Mar 97 12:43 GMT",          "Tue, 04 Mar 1997 12:43:00 GMT" },
  { NULL, NULL }
};

static const apr_time_t year2secs[] = {
             APR_INT64_C(0),    /* 1970 */
      APR_INT64_C(31536000),    /* 1971 */
      APR_INT64_C(63072000),    /* 1972 */
      APR_INT64_C(94694400),    /* 1973 */
     APR_INT64_C(126230400),    /* 1974 */
     APR_INT64_C(157766400),    /* 1975 */
     APR_INT64_C(189302400),    /* 1976 */
     APR_INT64_C(220924800),    /* 1977 */
     APR_INT64_C(252460800),    /* 1978 */
     APR_INT64_C(283996800),    /* 1979 */
     APR_INT64_C(315532800),    /* 1980 */
     APR_INT64_C(347155200),    /* 1981 */
     APR_INT64_C(378691200),    /* 1982 */
     APR_INT64_C(410227200),    /* 1983 */
     APR_INT64_C(441763200),    /* 1984 */
     APR_INT64_C(473385600),    /* 1985 */
     APR_INT64_C(504921600),    /* 1986 */
     APR_INT64_C(536457600),    /* 1987 */
     APR_INT64_C(567993600),    /* 1988 */
     APR_INT64_C(599616000),    /* 1989 */
     APR_INT64_C(631152000),    /* 1990 */
     APR_INT64_C(662688000),    /* 1991 */
     APR_INT64_C(694224000),    /* 1992 */
     APR_INT64_C(725846400),    /* 1993 */
     APR_INT64_C(757382400),    /* 1994 */
     APR_INT64_C(788918400),    /* 1995 */
     APR_INT64_C(820454400),    /* 1996 */
     APR_INT64_C(852076800),    /* 1997 */
     APR_INT64_C(883612800),    /* 1998 */
     APR_INT64_C(915148800),    /* 1999 */
     APR_INT64_C(946684800),    /* 2000 */
     APR_INT64_C(978307200),    /* 2001 */
    APR_INT64_C(1009843200),    /* 2002 */
    APR_INT64_C(1041379200),    /* 2003 */
    APR_INT64_C(1072915200),    /* 2004 */
    APR_INT64_C(1104537600),    /* 2005 */
    APR_INT64_C(1136073600),    /* 2006 */
    APR_INT64_C(1167609600),    /* 2007 */
    APR_INT64_C(1199145600),    /* 2008 */
    APR_INT64_C(1230768000),    /* 2009 */
    APR_INT64_C(1262304000),    /* 2010 */
    APR_INT64_C(1293840000),    /* 2011 */
    APR_INT64_C(1325376000),    /* 2012 */
    APR_INT64_C(1356998400),    /* 2013 */
    APR_INT64_C(1388534400),    /* 2014 */
    APR_INT64_C(1420070400),    /* 2015 */
    APR_INT64_C(1451606400),    /* 2016 */
    APR_INT64_C(1483228800),    /* 2017 */
    APR_INT64_C(1514764800),    /* 2018 */
    APR_INT64_C(1546300800),    /* 2019 */
    APR_INT64_C(1577836800),    /* 2020 */
    APR_INT64_C(1609459200),    /* 2021 */
    APR_INT64_C(1640995200),    /* 2022 */
    APR_INT64_C(1672531200),    /* 2023 */
    APR_INT64_C(1704067200),    /* 2024 */
    APR_INT64_C(1735689600),    /* 2025 */
    APR_INT64_C(1767225600),    /* 2026 */
    APR_INT64_C(1798761600),    /* 2027 */
    APR_INT64_C(1830297600),    /* 2028 */
    APR_INT64_C(1861920000),    /* 2029 */
    APR_INT64_C(1893456000),    /* 2030 */
    APR_INT64_C(1924992000),    /* 2031 */
    APR_INT64_C(1956528000),    /* 2032 */
    APR_INT64_C(1988150400),    /* 2033 */
    APR_INT64_C(2019686400),    /* 2034 */
    APR_INT64_C(2051222400),    /* 2035 */
    APR_INT64_C(2082758400),    /* 2036 */
    APR_INT64_C(2114380800),    /* 2037 */
    APR_INT64_C(2145916800)     /* 2038 */
};

const char month_snames[12][4] = {
    "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};

/* XXX: non-portable */
static void gm_timestr_822(char *ts, apr_time_t sec)
{
    static const char *const days[7]=
        {"Sun","Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    struct tm *tms;
    time_t ls = (time_t)sec;

    tms = gmtime(&ls);

    sprintf(ts, "%s, %.2d %s %d %.2d:%.2d:%.2d GMT", days[tms->tm_wday],
            tms->tm_mday, month_snames[tms->tm_mon], tms->tm_year + 1900,
            tms->tm_hour, tms->tm_min, tms->tm_sec);
}

/* Linear congruential generator */
static apr_uint32_t lgc(apr_uint32_t a)
{
    apr_uint64_t z = a;
    z *= 279470273;
    z %= APR_UINT64_C(4294967291);
    return (apr_uint32_t)z;
}

static void test_date_parse_http(abts_case *tc, void *data)
{
    int year, i;
    apr_time_t guess;
    apr_time_t offset = 0;
    apr_time_t secstodate, newsecs;
    char datestr[50];

    for (year = 1970; year < 2038; ++year) {
        secstodate = year2secs[year - 1970] + offset;
        gm_timestr_822(datestr, secstodate);
        secstodate *= APR_USEC_PER_SEC;
        newsecs = apr_date_parse_http(datestr);
        ABTS_TRUE(tc, secstodate == newsecs);
    }

#if APR_HAS_RANDOM
    apr_generate_random_bytes((unsigned char *)&guess, sizeof(guess));
#else
    guess = apr_time_now() % APR_TIME_C(4294967291);
#endif

    for (i = 0; i < 10000; ++i) {
        guess = (time_t)lgc((apr_uint32_t)guess);
        if (guess < 0)
            guess *= -1;
        secstodate = guess + offset;
        gm_timestr_822(datestr, secstodate);
        secstodate *= APR_USEC_PER_SEC;
        newsecs = apr_date_parse_http(datestr);
        ABTS_TRUE(tc, secstodate == newsecs);
    }
}

static void test_date_rfc(abts_case *tc, void *data)
{
    apr_time_t date;
    int i = 0;

    while (tests[i].input) {
        char str_date[APR_RFC822_DATE_LEN] = { 0 };

        date = apr_date_parse_rfc(tests[i].input);

        apr_rfc822_date(str_date, date);

        ABTS_STR_EQUAL(tc, str_date, tests[i].output);

        i++;
    }
}

abts_suite *testdate(abts_suite *suite)
{
    suite = ADD_SUITE(suite);

    abts_run_test(suite, test_date_parse_http, NULL);
    abts_run_test(suite, test_date_rfc, NULL);

    return suite;
}
