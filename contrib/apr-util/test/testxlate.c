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

#include "apr.h"
#include "apr_errno.h"
#include "apr_general.h"
#include "apr_strings.h"
#include "apr_xlate.h"
#include "abts.h"
#include "testutil.h"

#if APR_HAS_XLATE

static const char test_utf8[] = "Edelwei\xc3\x9f";
static const char test_utf7[] = "Edelwei+AN8-";
static const char test_latin1[] = "Edelwei\xdf";
static const char test_latin2[] = "Edelwei\xdf";

static void test_conversion(abts_case *tc, apr_xlate_t *convset,
                            const char *inbuf, const char *expected)
{
    static char buf[1024];
    apr_size_t inbytes_left = strlen(inbuf);
    apr_size_t outbytes_left = sizeof(buf) - 1;
    apr_status_t rv;

    rv = apr_xlate_conv_buffer(convset, inbuf, &inbytes_left, buf, &outbytes_left);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    if (rv != APR_SUCCESS)
        return;

    rv = apr_xlate_conv_buffer(convset, NULL, NULL, buf + sizeof(buf) -
                               outbytes_left - 1, &outbytes_left);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    buf[sizeof(buf) - outbytes_left - 1] = '\0';

    ABTS_STR_EQUAL(tc, expected, buf);
}

static void one_test(abts_case *tc, const char *cs1, const char *cs2,
                     const char *str1, const char *str2,
                     apr_pool_t *pool)
{
    apr_status_t rv;
    apr_xlate_t *convset;

    rv = apr_xlate_open(&convset, cs2, cs1, pool);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    if (rv != APR_SUCCESS)
        return;

    test_conversion(tc, convset, str1, str2);

    rv = apr_xlate_close(convset);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
}

#if APU_HAVE_APR_ICONV
/* it is a bug if iconv_open() fails */
static int is_transform_supported(abts_case *tc, const char *cs1,
                                  const char *cs2, apr_pool_t *pool) {
    return 1;
}
#else
/* some iconv implementations don't support all tested transforms;
 * example: 8859-1 <-> 8859-2 using native Solaris iconv
 */
static int is_transform_supported(abts_case *tc, const char *cs1,
                                  const char *cs2, apr_pool_t *pool) {
    apr_status_t rv;
    apr_xlate_t *convset;

    rv = apr_xlate_open(&convset, cs2, cs1, pool);
    if (rv != APR_SUCCESS) {
        return 0;
    }

    rv = apr_xlate_close(convset);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    return 1;
}
#endif

static void test_transformation(abts_case *tc, void *data)
{
    /* 1. Identity transformation: UTF-8 -> UTF-8 */
    one_test(tc, "UTF-8", "UTF-8", test_utf8, test_utf8, p);

    /* 2. UTF-8 <-> ISO-8859-1 */
    one_test(tc, "UTF-8", "ISO-8859-1", test_utf8, test_latin1, p);
    one_test(tc, "ISO-8859-1", "UTF-8", test_latin1, test_utf8, p);

    /* 3. ISO-8859-1 <-> ISO-8859-2, identity */
    if (is_transform_supported(tc, "ISO-8859-1", "ISO-8859-2", p)) {
        one_test(tc, "ISO-8859-1", "ISO-8859-2", test_latin1, test_latin2, p);
    }
    if (is_transform_supported(tc, "ISO-8859-2", "ISO-8859-1", p)) {
        one_test(tc, "ISO-8859-2", "ISO-8859-1", test_latin2, test_latin1, p);
    }

    /* 4. Transformation using charset aliases */
    one_test(tc, "UTF-8", "UTF-7", test_utf8, test_utf7, p);
    one_test(tc, "UTF-7", "UTF-8", test_utf7, test_utf8, p);
}

#endif /* APR_HAS_XLATE */

abts_suite *testxlate(abts_suite *suite)
{
    suite = ADD_SUITE(suite);

#if APR_HAS_XLATE
    abts_run_test(suite, test_transformation, NULL);
#endif

    return suite;
}
