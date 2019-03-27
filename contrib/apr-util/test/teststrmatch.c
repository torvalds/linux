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

#include "testutil.h"

#include "apr.h"
#include "apr_general.h"
#include "apr_strmatch.h"
#if APR_HAVE_STDLIB_H
#include <stdlib.h>
#endif
#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include "apr_want.h"

static void test_str(abts_case *tc, void *data)
{
    apr_pool_t *pool = p;
    const apr_strmatch_pattern *pattern;
    const apr_strmatch_pattern *pattern_nocase;
    const apr_strmatch_pattern *pattern_onechar;
    const apr_strmatch_pattern *pattern_zero;
    const char *match = NULL;
    const char *input1 = "string that contains a patterN...";
    const char *input2 = "string that contains a pattern...";
    const char *input3 = "pattern at the start of a string";
    const char *input4 = "string that ends with a pattern";
    const char *input5 = "patter\200n not found, negative chars in input";
    const char *input6 = "patter\200n, negative chars, contains pattern...";

    pattern = apr_strmatch_precompile(pool, "pattern", 1);
    ABTS_PTR_NOTNULL(tc, pattern);
 
    pattern_nocase = apr_strmatch_precompile(pool, "pattern", 0);
    ABTS_PTR_NOTNULL(tc, pattern_nocase);

    pattern_onechar = apr_strmatch_precompile(pool, "g", 0);
    ABTS_PTR_NOTNULL(tc, pattern_onechar);

    pattern_zero = apr_strmatch_precompile(pool, "", 0);
    ABTS_PTR_NOTNULL(tc, pattern_zero);

    match = apr_strmatch(pattern, input1, strlen(input1));
    ABTS_PTR_EQUAL(tc, NULL, match);

    match = apr_strmatch(pattern, input2, strlen(input2));
    ABTS_PTR_EQUAL(tc, input2 + 23, match);

    match = apr_strmatch(pattern_onechar, input1, strlen(input1));
    ABTS_PTR_EQUAL(tc, input1 + 5, match);

    match = apr_strmatch(pattern_zero, input1, strlen(input1));
    ABTS_PTR_EQUAL(tc, input1, match);

    match = apr_strmatch(pattern_nocase, input1, strlen(input1));
    ABTS_PTR_EQUAL(tc, input1 + 23, match);

    match = apr_strmatch(pattern, input3, strlen(input3));
    ABTS_PTR_EQUAL(tc, input3, match);

    match = apr_strmatch(pattern, input4, strlen(input4));
    ABTS_PTR_EQUAL(tc, input4 + 24, match);

    match = apr_strmatch(pattern, input5, strlen(input5));
    ABTS_PTR_EQUAL(tc, NULL, match);

    match = apr_strmatch(pattern, input6, strlen(input6));
    ABTS_PTR_EQUAL(tc, input6 + 35, match);
}

abts_suite *teststrmatch(abts_suite *suite)
{
    suite = ADD_SUITE(suite);

    abts_run_test(suite, test_str, NULL);

    return suite;
}

