/*
 * Copyright (c) 1997-2004 Kungliga Tekniska Högskolan
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
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "krb5_locl.h"

/**
 * Set the absolute time that the caller knows the kdc has so the
 * kerberos library can calculate the relative diffrence beteen the
 * KDC time and local system time.
 *
 * @param context Keberos 5 context.
 * @param sec The applications new of "now" in seconds
 * @param usec The applications new of "now" in micro seconds

 * @return Kerberos 5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_real_time (krb5_context context,
		    krb5_timestamp sec,
		    int32_t usec)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);

    context->kdc_sec_offset = sec - tv.tv_sec;

    /**
     * If the caller passes in a negative usec, its assumed to be
     * unknown and the function will use the current time usec.
     */
    if (usec >= 0) {
	context->kdc_usec_offset = usec - tv.tv_usec;

	if (context->kdc_usec_offset < 0) {
	    context->kdc_sec_offset--;
	    context->kdc_usec_offset += 1000000;
	}
    } else
	context->kdc_usec_offset = tv.tv_usec;

    return 0;
}

/*
 * return ``corrected'' time in `timeret'.
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_timeofday (krb5_context context,
		krb5_timestamp *timeret)
{
    *timeret = time(NULL) + context->kdc_sec_offset;
    return 0;
}

/*
 * like gettimeofday but with time correction to the KDC
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_us_timeofday (krb5_context context,
		   krb5_timestamp *sec,
		   int32_t *usec)
{
    struct timeval tv;

    gettimeofday (&tv, NULL);

    *sec  = tv.tv_sec + context->kdc_sec_offset;
    *usec = tv.tv_usec;		/* XXX */
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_format_time(krb5_context context, time_t t,
		 char *s, size_t len, krb5_boolean include_time)
{
    struct tm *tm;
    if(context->log_utc)
	tm = gmtime (&t);
    else
	tm = localtime(&t);
    if(tm == NULL ||
       strftime(s, len, include_time ? context->time_fmt : context->date_fmt, tm) == 0)
	snprintf(s, len, "%ld", (long)t);
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_string_to_deltat(const char *string, krb5_deltat *deltat)
{
    if((*deltat = parse_time(string, "s")) == -1)
	return KRB5_DELTAT_BADFORMAT;
    return 0;
}
