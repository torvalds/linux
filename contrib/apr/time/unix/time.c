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

#include "apr_portable.h"
#include "apr_time.h"
#include "apr_lib.h"
#include "apr_private.h"
#include "apr_strings.h"

/* private APR headers */
#include "apr_arch_internal_time.h"

/* System Headers required for time library */
#if APR_HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#if APR_HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif
/* End System Headers */

#if !defined(HAVE_STRUCT_TM_TM_GMTOFF) && !defined(HAVE_STRUCT_TM___TM_GMTOFF)
static apr_int32_t server_gmt_offset;
#define NO_GMTOFF_IN_STRUCT_TM
#endif          

static apr_int32_t get_offset(struct tm *tm)
{
#if defined(HAVE_STRUCT_TM_TM_GMTOFF)
    return tm->tm_gmtoff;
#elif defined(HAVE_STRUCT_TM___TM_GMTOFF)
    return tm->__tm_gmtoff;
#else
#ifdef NETWARE
    /* Need to adjust the global variable each time otherwise
        the web server would have to be restarted when daylight
        savings changes.
    */
    if (daylightOnOff) {
        return server_gmt_offset + daylightOffset;
    }
#else
    if (tm->tm_isdst)
        return server_gmt_offset + 3600;
#endif
    return server_gmt_offset;
#endif
}

APR_DECLARE(apr_status_t) apr_time_ansi_put(apr_time_t *result,
                                            time_t input)
{
    *result = (apr_time_t)input * APR_USEC_PER_SEC;
    return APR_SUCCESS;
}

/* NB NB NB NB This returns GMT!!!!!!!!!! */
APR_DECLARE(apr_time_t) apr_time_now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * APR_USEC_PER_SEC + tv.tv_usec;
}

static void explode_time(apr_time_exp_t *xt, apr_time_t t,
                         apr_int32_t offset, int use_localtime)
{
    struct tm tm;
    time_t tt = (t / APR_USEC_PER_SEC) + offset;
    xt->tm_usec = t % APR_USEC_PER_SEC;

#if APR_HAS_THREADS && defined (_POSIX_THREAD_SAFE_FUNCTIONS)
    if (use_localtime)
        localtime_r(&tt, &tm);
    else
        gmtime_r(&tt, &tm);
#else
    if (use_localtime)
        tm = *localtime(&tt);
    else
        tm = *gmtime(&tt);
#endif

    xt->tm_sec  = tm.tm_sec;
    xt->tm_min  = tm.tm_min;
    xt->tm_hour = tm.tm_hour;
    xt->tm_mday = tm.tm_mday;
    xt->tm_mon  = tm.tm_mon;
    xt->tm_year = tm.tm_year;
    xt->tm_wday = tm.tm_wday;
    xt->tm_yday = tm.tm_yday;
    xt->tm_isdst = tm.tm_isdst;
    xt->tm_gmtoff = get_offset(&tm);
}

APR_DECLARE(apr_status_t) apr_time_exp_tz(apr_time_exp_t *result,
                                          apr_time_t input, apr_int32_t offs)
{
    explode_time(result, input, offs, 0);
    result->tm_gmtoff = offs;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_time_exp_gmt(apr_time_exp_t *result,
                                           apr_time_t input)
{
    return apr_time_exp_tz(result, input, 0);
}

APR_DECLARE(apr_status_t) apr_time_exp_lt(apr_time_exp_t *result,
                                                apr_time_t input)
{
#if defined(__EMX__)
    /* EMX gcc (OS/2) has a timezone global we can use */
    return apr_time_exp_tz(result, input, -timezone);
#else
    explode_time(result, input, 0, 1);
    return APR_SUCCESS;
#endif /* __EMX__ */
}

APR_DECLARE(apr_status_t) apr_time_exp_get(apr_time_t *t, apr_time_exp_t *xt)
{
    apr_time_t year = xt->tm_year;
    apr_time_t days;
    static const int dayoffset[12] =
    {306, 337, 0, 31, 61, 92, 122, 153, 184, 214, 245, 275};

    /* shift new year to 1st March in order to make leap year calc easy */

    if (xt->tm_mon < 2)
        year--;

    /* Find number of days since 1st March 1900 (in the Gregorian calendar). */

    days = year * 365 + year / 4 - year / 100 + (year / 100 + 3) / 4;
    days += dayoffset[xt->tm_mon] + xt->tm_mday - 1;
    days -= 25508;              /* 1 jan 1970 is 25508 days since 1 mar 1900 */
    days = ((days * 24 + xt->tm_hour) * 60 + xt->tm_min) * 60 + xt->tm_sec;

    if (days < 0) {
        return APR_EBADDATE;
    }
    *t = days * APR_USEC_PER_SEC + xt->tm_usec;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_time_exp_gmt_get(apr_time_t *t, 
                                               apr_time_exp_t *xt)
{
    apr_status_t status = apr_time_exp_get(t, xt);
    if (status == APR_SUCCESS)
        *t -= (apr_time_t) xt->tm_gmtoff * APR_USEC_PER_SEC;
    return status;
}

APR_DECLARE(apr_status_t) apr_os_imp_time_get(apr_os_imp_time_t **ostime,
                                              apr_time_t *aprtime)
{
    (*ostime)->tv_usec = *aprtime % APR_USEC_PER_SEC;
    (*ostime)->tv_sec = *aprtime / APR_USEC_PER_SEC;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_os_exp_time_get(apr_os_exp_time_t **ostime,
                                              apr_time_exp_t *aprtime)
{
    (*ostime)->tm_sec  = aprtime->tm_sec;
    (*ostime)->tm_min  = aprtime->tm_min;
    (*ostime)->tm_hour = aprtime->tm_hour;
    (*ostime)->tm_mday = aprtime->tm_mday;
    (*ostime)->tm_mon  = aprtime->tm_mon;
    (*ostime)->tm_year = aprtime->tm_year;
    (*ostime)->tm_wday = aprtime->tm_wday;
    (*ostime)->tm_yday = aprtime->tm_yday;
    (*ostime)->tm_isdst = aprtime->tm_isdst;

#if defined(HAVE_STRUCT_TM_TM_GMTOFF)
    (*ostime)->tm_gmtoff = aprtime->tm_gmtoff;
#elif defined(HAVE_STRUCT_TM___TM_GMTOFF)
    (*ostime)->__tm_gmtoff = aprtime->tm_gmtoff;
#endif

    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_os_imp_time_put(apr_time_t *aprtime,
                                              apr_os_imp_time_t **ostime,
                                              apr_pool_t *cont)
{
    *aprtime = (*ostime)->tv_sec * APR_USEC_PER_SEC + (*ostime)->tv_usec;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_os_exp_time_put(apr_time_exp_t *aprtime,
                                              apr_os_exp_time_t **ostime,
                                              apr_pool_t *cont)
{
    aprtime->tm_sec = (*ostime)->tm_sec;
    aprtime->tm_min = (*ostime)->tm_min;
    aprtime->tm_hour = (*ostime)->tm_hour;
    aprtime->tm_mday = (*ostime)->tm_mday;
    aprtime->tm_mon = (*ostime)->tm_mon;
    aprtime->tm_year = (*ostime)->tm_year;
    aprtime->tm_wday = (*ostime)->tm_wday;
    aprtime->tm_yday = (*ostime)->tm_yday;
    aprtime->tm_isdst = (*ostime)->tm_isdst;

#if defined(HAVE_STRUCT_TM_TM_GMTOFF)
    aprtime->tm_gmtoff = (*ostime)->tm_gmtoff;
#elif defined(HAVE_STRUCT_TM___TM_GMTOFF)
    aprtime->tm_gmtoff = (*ostime)->__tm_gmtoff;
#endif

    return APR_SUCCESS;
}

APR_DECLARE(void) apr_sleep(apr_interval_time_t t)
{
#ifdef OS2
    DosSleep(t/1000);
#elif defined(BEOS)
    snooze(t);
#elif defined(NETWARE)
    delay(t/1000);
#else
    struct timeval tv;
    tv.tv_usec = t % APR_USEC_PER_SEC;
    tv.tv_sec = t / APR_USEC_PER_SEC;
    select(0, NULL, NULL, NULL, &tv);
#endif
}

#ifdef OS2
APR_DECLARE(apr_status_t) apr_os2_time_to_apr_time(apr_time_t *result,
                                                   FDATE os2date,
                                                   FTIME os2time)
{
  struct tm tmpdate;

  memset(&tmpdate, 0, sizeof(tmpdate));
  tmpdate.tm_hour  = os2time.hours;
  tmpdate.tm_min   = os2time.minutes;
  tmpdate.tm_sec   = os2time.twosecs * 2;

  tmpdate.tm_mday  = os2date.day;
  tmpdate.tm_mon   = os2date.month - 1;
  tmpdate.tm_year  = os2date.year + 80;
  tmpdate.tm_isdst = -1;

  *result = mktime(&tmpdate) * APR_USEC_PER_SEC;
  return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_apr_time_to_os2_time(FDATE *os2date,
                                                   FTIME *os2time,
                                                   apr_time_t aprtime)
{
    time_t ansitime = aprtime / APR_USEC_PER_SEC;
    struct tm *lt;
    lt = localtime(&ansitime);
    os2time->hours    = lt->tm_hour;
    os2time->minutes  = lt->tm_min;
    os2time->twosecs  = lt->tm_sec / 2;

    os2date->day      = lt->tm_mday;
    os2date->month    = lt->tm_mon + 1;
    os2date->year     = lt->tm_year - 80;
    return APR_SUCCESS;
}
#endif

#ifdef NETWARE
APR_DECLARE(void) apr_netware_setup_time(void)
{
    tzset();
    server_gmt_offset = -TZONE;
}
#else
APR_DECLARE(void) apr_unix_setup_time(void)
{
#ifdef NO_GMTOFF_IN_STRUCT_TM
    /* Precompute the offset from GMT on systems where it's not
       in struct tm.

       Note: This offset is normalized to be independent of daylight
       savings time; if the calculation happens to be done in a
       time/place where a daylight savings adjustment is in effect,
       the returned offset has the same value that it would have
       in the same location if daylight savings were not in effect.
       The reason for this is that the returned offset can be
       applied to a past or future timestamp in explode_time(),
       so the DST adjustment obtained from the current time won't
       necessarily be applicable.

       mktime() is the inverse of localtime(); so, presumably,
       passing in a struct tm made by gmtime() let's us calculate
       the true GMT offset. However, there's a catch: if daylight
       savings is in effect, gmtime()will set the tm_isdst field
       and confuse mktime() into returning a time that's offset
       by one hour. In that case, we must adjust the calculated GMT
       offset.

     */

    struct timeval now;
    time_t t1, t2;
    struct tm t;

    gettimeofday(&now, NULL);
    t1 = now.tv_sec;
    t2 = 0;

#if APR_HAS_THREADS && defined(_POSIX_THREAD_SAFE_FUNCTIONS)
    gmtime_r(&t1, &t);
#else
    t = *gmtime(&t1);
#endif
    t.tm_isdst = 0; /* we know this GMT time isn't daylight-savings */
    t2 = mktime(&t);
    server_gmt_offset = (apr_int32_t) difftime(t1, t2);
#endif /* NO_GMTOFF_IN_STRUCT_TM */
}

#endif

/* A noop on all known Unix implementations */
APR_DECLARE(void) apr_time_clock_hires(apr_pool_t *p)
{
    return;
}


