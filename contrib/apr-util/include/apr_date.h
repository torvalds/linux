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

#ifndef APR_DATE_H
#define APR_DATE_H

/**
 * @file apr_date.h
 * @brief APR-UTIL date routines
 */

/**
 * @defgroup APR_Util_Date Date routines
 * @ingroup APR_Util
 * @{
 */

/*
 * apr_date.h: prototypes for date parsing utility routines
 */

#include "apu.h"
#include "apr_time.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A bad date. */
#define APR_DATE_BAD ((apr_time_t)0)

/**
 * Compare a string to a mask
 * @param data The string to compare
 * @param mask Mask characters (arbitrary maximum is 256 characters):
 * <PRE>
 *   '\@' - uppercase letter
 *   '\$' - lowercase letter
 *   '\&' - hex digit
 *   '#' - digit
 *   '~' - digit or space
 *   '*' - swallow remaining characters
 * </PRE>
 * @remark The mask tests for an exact match for any other character
 * @return 1 if the string matches, 0 otherwise
 */
APU_DECLARE(int) apr_date_checkmask(const char *data, const char *mask);

/**
 * Parses an HTTP date in one of three standard forms:
 * <PRE>
 *     Sun, 06 Nov 1994 08:49:37 GMT  ; RFC 822, updated by RFC 1123
 *     Sunday, 06-Nov-94 08:49:37 GMT ; RFC 850, obsoleted by RFC 1036
 *     Sun Nov  6 08:49:37 1994       ; ANSI C's asctime() format
 * </PRE>
 * @param date The date in one of the three formats above
 * @return the apr_time_t number of microseconds since 1 Jan 1970 GMT, or
 *         0 if this would be out of range or if the date is invalid.
 */
APU_DECLARE(apr_time_t) apr_date_parse_http(const char *date);

/**
 * Parses a string resembling an RFC 822 date.  This is meant to be
 * leinent in its parsing of dates.  Hence, this will parse a wider 
 * range of dates than apr_date_parse_http.
 *
 * The prominent mailer (or poster, if mailer is unknown) that has
 * been seen in the wild is included for the unknown formats.
 * <PRE>
 *     Sun, 06 Nov 1994 08:49:37 GMT  ; RFC 822, updated by RFC 1123
 *     Sunday, 06-Nov-94 08:49:37 GMT ; RFC 850, obsoleted by RFC 1036
 *     Sun Nov  6 08:49:37 1994       ; ANSI C's asctime() format
 *     Sun, 6 Nov 1994 08:49:37 GMT   ; RFC 822, updated by RFC 1123
 *     Sun, 06 Nov 94 08:49:37 GMT    ; RFC 822
 *     Sun, 6 Nov 94 08:49:37 GMT     ; RFC 822
 *     Sun, 06 Nov 94 08:49 GMT       ; Unknown [drtr\@ast.cam.ac.uk] 
 *     Sun, 6 Nov 94 08:49 GMT        ; Unknown [drtr\@ast.cam.ac.uk]
 *     Sun, 06 Nov 94 8:49:37 GMT     ; Unknown [Elm 70.85]
 *     Sun, 6 Nov 94 8:49:37 GMT      ; Unknown [Elm 70.85] 
 * </PRE>
 *
 * @param date The date in one of the formats above
 * @return the apr_time_t number of microseconds since 1 Jan 1970 GMT, or
 *         0 if this would be out of range or if the date is invalid.
 */
APU_DECLARE(apr_time_t) apr_date_parse_rfc(const char *date);

/** @} */
#ifdef __cplusplus
}
#endif

#endif	/* !APR_DATE_H */
