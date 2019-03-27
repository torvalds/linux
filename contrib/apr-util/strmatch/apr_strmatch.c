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

#include "apr_strmatch.h"
#include "apr_lib.h"
#define APR_WANT_STRFUNC
#include "apr_want.h"


#define NUM_CHARS  256

/*
 * String searching functions
 */
static const char *match_no_op(const apr_strmatch_pattern *this_pattern,
                               const char *s, apr_size_t slen)
{
    return s;
}

static const char *match_boyer_moore_horspool(
                               const apr_strmatch_pattern *this_pattern,
                               const char *s, apr_size_t slen)
{
    const char *s_end = s + slen;
    apr_size_t *shift = (apr_size_t *)(this_pattern->context);
    const char *s_next = s + this_pattern->length - 1;
    const char *p_start = this_pattern->pattern;
    const char *p_end = p_start + this_pattern->length - 1;
    while (s_next < s_end) {
        const char *s_tmp = s_next;
        const char *p_tmp = p_end;
        while (*s_tmp == *p_tmp) {
            p_tmp--;
            if (p_tmp < p_start) {
                return s_tmp;
            }
            s_tmp--;
        }
        s_next += shift[(int)*((const unsigned char *)s_next)];
    }
    return NULL;
}

static const char *match_boyer_moore_horspool_nocase(
                               const apr_strmatch_pattern *this_pattern,
                               const char *s, apr_size_t slen)
{
    const char *s_end = s + slen;
    apr_size_t *shift = (apr_size_t *)(this_pattern->context);
    const char *s_next = s + this_pattern->length - 1;
    const char *p_start = this_pattern->pattern;
    const char *p_end = p_start + this_pattern->length - 1;
    while (s_next < s_end) {
        const char *s_tmp = s_next;
        const char *p_tmp = p_end;
        while (apr_tolower(*s_tmp) == apr_tolower(*p_tmp)) {
            p_tmp--;
            if (p_tmp < p_start) {
                return s_tmp;
            }
            s_tmp--;
        }
        s_next += shift[(unsigned char)apr_tolower(*s_next)];
    }
    return NULL;
}

APU_DECLARE(const apr_strmatch_pattern *) apr_strmatch_precompile(
                                              apr_pool_t *p, const char *s,
                                              int case_sensitive)
{
    apr_strmatch_pattern *pattern;
    apr_size_t i;
    apr_size_t *shift;

    pattern = apr_palloc(p, sizeof(*pattern));
    pattern->pattern = s;
    pattern->length = strlen(s);
    if (pattern->length == 0) {
        pattern->compare = match_no_op;
        pattern->context = NULL;
        return pattern;
    }

    shift = (apr_size_t *)apr_palloc(p, sizeof(apr_size_t) * NUM_CHARS);
    for (i = 0; i < NUM_CHARS; i++) {
        shift[i] = pattern->length;
    }
    if (case_sensitive) {
        pattern->compare = match_boyer_moore_horspool;
        for (i = 0; i < pattern->length - 1; i++) {
            shift[(unsigned char)s[i]] = pattern->length - i - 1;
        }
    }
    else {
        pattern->compare = match_boyer_moore_horspool_nocase;
        for (i = 0; i < pattern->length - 1; i++) {
            shift[(unsigned char)apr_tolower(s[i])] = pattern->length - i - 1;
        }
    }
    pattern->context = shift;

    return pattern;
}
