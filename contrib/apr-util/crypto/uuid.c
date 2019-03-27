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

#include <stdio.h>      /* for sprintf */

#include "apr.h"
#include "apr_uuid.h"
#include "apr_errno.h"
#include "apr_lib.h"


APU_DECLARE(void) apr_uuid_format(char *buffer, const apr_uuid_t *uuid)
{
    const unsigned char *d = uuid->data;

    sprintf(buffer,
            "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7],
            d[8], d[9], d[10], d[11], d[12], d[13], d[14], d[15]);
}

/* convert a pair of hex digits to an integer value [0,255] */
#if 'A' == 65
static unsigned char parse_hexpair(const char *s)
{
    int result;
    int temp;

    result = s[0] - '0';
    if (result > 48)
	result = (result - 39) << 4;
    else if (result > 16)
	result = (result - 7) << 4;
    else
	result = result << 4;

    temp = s[1] - '0';
    if (temp > 48)
	result |= temp - 39;
    else if (temp > 16)
	result |= temp - 7;
    else
	result |= temp;

    return (unsigned char)result;
}
#else
static unsigned char parse_hexpair(const char *s)
{
    int result;

    if (isdigit(*s)) {
        result = (*s - '0') << 4;
    }
    else {
        if (isupper(*s)) {
            result = (*s - 'A' + 10) << 4;
        }
        else {
            result = (*s - 'a' + 10) << 4;
        }
    }

    ++s;
    if (isdigit(*s)) {
        result |= (*s - '0');
    }
    else {
        if (isupper(*s)) {
            result |= (*s - 'A' + 10);
        }
        else {
            result |= (*s - 'a' + 10);
        }
    }

    return (unsigned char)result;
}
#endif

APU_DECLARE(apr_status_t) apr_uuid_parse(apr_uuid_t *uuid,
                                         const char *uuid_str)
{
    int i;
    unsigned char *d = uuid->data;

    for (i = 0; i < 36; ++i) {
	char c = uuid_str[i];
	if (!apr_isxdigit(c) &&
	    !(c == '-' && (i == 8 || i == 13 || i == 18 || i == 23)))
            /* ### need a better value */
	    return APR_BADARG;
    }
    if (uuid_str[36] != '\0') {
        /* ### need a better value */
	return APR_BADARG;
    }

    d[0] = parse_hexpair(&uuid_str[0]);
    d[1] = parse_hexpair(&uuid_str[2]);
    d[2] = parse_hexpair(&uuid_str[4]);
    d[3] = parse_hexpair(&uuid_str[6]);

    d[4] = parse_hexpair(&uuid_str[9]);
    d[5] = parse_hexpair(&uuid_str[11]);

    d[6] = parse_hexpair(&uuid_str[14]);
    d[7] = parse_hexpair(&uuid_str[16]);

    d[8] = parse_hexpair(&uuid_str[19]);
    d[9] = parse_hexpair(&uuid_str[21]);

    for (i = 6; i--;)
	d[10 + i] = parse_hexpair(&uuid_str[i*2+24]);

    return APR_SUCCESS;
}
