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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "apr_md5.h"
#include "apr_xlate.h"
#include "apr_general.h"

#include "abts.h"
#include "testutil.h"

static struct {
    const char *string;
    const char *digest;
} md5sums[] = 
{
    {"Jeff was here!",
     "\xa5\x25\x8a\x89\x11\xb2\x9d\x1f\x81\x75\x96\x3b\x60\x94\x49\xc0"},
    {"01234567890aBcDeFASDFGHJKLPOIUYTR"
     "POIUYTREWQZXCVBN  LLLLLLLLLLLLLLL",
     "\xd4\x1a\x06\x2c\xc5\xfd\x6f\x24\x67\x68\x56\x7c\x40\x8a\xd5\x69"},
    {"111111118888888888888888*******%%%%%%%%%%#####"
     "142134u8097289720432098409289nkjlfkjlmn,m..   ",
     "\xb6\xea\x5b\xe8\xca\x45\x8a\x33\xf0\xf1\x84\x6f\xf9\x65\xa8\xe1"},
    {"01234567890aBcDeFASDFGHJKLPOIUYTR"
     "POIUYTREWQZXCVBN  LLLLLLLLLLLLLLL"
     "01234567890aBcDeFASDFGHJKLPOIUYTR"
     "POIUYTREWQZXCVBN  LLLLLLLLLLLLLLL"
     "1",
     "\xd1\xa1\xc0\x97\x8a\x60\xbb\xfb\x2a\x25\x46\x9d\xa5\xae\xd0\xb0"}
};

static int num_sums = sizeof(md5sums) / sizeof(md5sums[0]);
static int count;

static void test_md5sum(abts_case *tc, void *data)
{
        apr_md5_ctx_t context;
        unsigned char digest[APR_MD5_DIGESTSIZE];
        const void *string = md5sums[count].string;
        const void *sum = md5sums[count].digest;
        unsigned int len = strlen(string);

        ABTS_ASSERT(tc, "apr_md5_init", (apr_md5_init(&context) == 0));
        ABTS_ASSERT(tc, "apr_md5_update", 
                    (apr_md5_update(&context, string, len) == 0));
        ABTS_ASSERT(tc, "apr_md5_final", (apr_md5_final(digest, &context)
                                          == 0));
        ABTS_ASSERT(tc, "check for correct md5 digest",
                    (memcmp(digest, sum, APR_MD5_DIGESTSIZE) == 0));
}

static void test_md5sum_unaligned(abts_case *tc, void *data)
{
        apr_md5_ctx_t context;
        const char *string = "abcdefghijklmnopqrstuvwxyz01234"
                             "abcdefghijklmnopqrstuvwxyz01234"
                             "abcdefghijklmnopqrstuvwxyz01234"
                             "abcdefghijklmnopqrstuvwxyz01234_";
        const char *sum =
            "\x93\x17\x22\x78\xee\x30\x82\xb3\xeb\x95\x33\xec\xea\x78\xb7\x89";
        unsigned char digest[APR_MD5_DIGESTSIZE];
        unsigned int i;

        ABTS_ASSERT(tc, "apr_md5_init", (apr_md5_init(&context) == 0));
        for (i = 0; i < 10; i++) {
                ABTS_ASSERT(tc, "apr_md5_update",
                    (apr_md5_update(&context, string, strlen(string)) == 0));
                string++;
        }
        ABTS_ASSERT(tc, "apr_md5_final", (apr_md5_final(digest, &context)
                                          == 0));
        ABTS_ASSERT(tc, "check for correct md5 digest of unaligned data",
                    (memcmp(digest, sum, APR_MD5_DIGESTSIZE) == 0));
}

abts_suite *testmd5(abts_suite *suite)
{
        suite = ADD_SUITE(suite);
        
        for (count=0; count < num_sums; count++) {
            abts_run_test(suite, test_md5sum, NULL);
        }
        abts_run_test(suite, test_md5sum_unaligned, NULL);

        return suite;
}
