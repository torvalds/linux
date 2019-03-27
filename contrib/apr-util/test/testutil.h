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

#include "apr_pools.h"
#include "abts.h"

#ifndef APR_TEST_UTIL
#define APR_TEST_UTIL

/* XXX FIXME */
#ifdef WIN32
#define EXTENSION ".exe"
#elif NETWARE
#define EXTENSION ".nlm"
#else
#define EXTENSION
#endif

#define STRING_MAX 8096

/* Some simple functions to make the test apps easier to write and
 * a bit more consistent...
 */

extern apr_pool_t *p;

/* Assert that RV is an APR_SUCCESS value; else fail giving strerror
 * for RV and CONTEXT message. */
void apr_assert_success(abts_case* tc, const char *context, apr_status_t rv);

void apr_assert_failure(abts_case* tc, const char *context,
                        apr_status_t rv, int lineno);
#define APR_ASSERT_FAILURE(tc, ctxt, rv) \
             apr_assert_failure(tc, ctxt, rv, __LINE__)


void initialize(void);

abts_suite *teststrmatch(abts_suite *suite);
abts_suite *testuri(abts_suite *suite);
abts_suite *testuuid(abts_suite *suite);
abts_suite *testbuckets(abts_suite *suite);
abts_suite *testpass(abts_suite *suite);
abts_suite *testmd4(abts_suite *suite);
abts_suite *testmd5(abts_suite *suite);
abts_suite *testcrypto(abts_suite *suite);
abts_suite *testldap(abts_suite *suite);
abts_suite *testdbd(abts_suite *suite);
abts_suite *testdate(abts_suite *suite);
abts_suite *testmemcache(abts_suite *suite);
abts_suite *testreslist(abts_suite *suite);
abts_suite *testqueue(abts_suite *suite);
abts_suite *testxml(abts_suite *suite);
abts_suite *testxlate(abts_suite *suite);
abts_suite *testrmm(abts_suite *suite);
abts_suite *testdbm(abts_suite *suite);

#endif /* APR_TEST_INCLUDES */
