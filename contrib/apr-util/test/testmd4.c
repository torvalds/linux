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
/* This is derived from material copyright RSA Data Security, Inc.
 * Their notice is reproduced below in its entirety.
 *
 * Copyright (C) 1990-2, RSA Data Security, Inc. Created 1990. All
 * rights reserved.
 *
 * RSA Data Security, Inc. makes no representations concerning either
 * the merchantability of this software or the suitability of this
 * software for any particular purpose. It is provided "as is"
 * without express or implied warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this
 * documentation and/or software.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "apr_errno.h"
#include "apr_md4.h"
#include "apr_file_io.h"

#include "abts.h"
#include "testutil.h"

static struct {
        const char *string;
        const char *md4sum;
} md4sums[] = 
{
/* 
* Taken from the old md4 test suite.
* MD4 ("") = 31d6cfe0d16ae931b73c59d7e0c089c0
* MD4 ("a") = bde52cb31de33e46245e05fbdbd6fb24
* MD4 ("abc") = a448017aaf21d8525fc10ae87aa6729d
* MD4 ("message digest") = d9130a8164549fe818874806e1c7014b
* MD4 ("abcdefghijklmnopqrstuvwxyz") = d79e1c308aa5bbcdeea8ed63df412da9
* MD4 ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")
* MD4 ("12345678901234567890123456789012345678901234567890123456789012345678901234567890") = e33b4ddc9c38f2199c3e7b164fcc0536
* 
*/
        {"", 
         "\x31\xd6\xcf\xe0\xd1\x6a\xe9\x31\xb7\x3c\x59\xd7\xe0\xc0\x89\xc0"},
        {"a", 
         "\xbd\xe5\x2c\xb3\x1d\xe3\x3e\x46\x24\x5e\x05\xfb\xdb\xd6\xfb\x24"},
        {"abc", 
         "\xa4\x48\x01\x7a\xaf\x21\xd8\x52\x5f\xc1\x0a\xe8\x7a\xa6\x72\x9d"},
        {"message digest", 
         "\xd9\x13\x0a\x81\x64\x54\x9f\xe8\x18\x87\x48\x06\xe1\xc7\x01\x4b"},
        {"abcdefghijklmnopqrstuvwxyz", 
         "\xd7\x9e\x1c\x30\x8a\xa5\xbb\xcd\xee\xa8\xed\x63\xdf\x41\x2d\xa9"},
        {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 
         "\x04\x3f\x85\x82\xf2\x41\xdb\x35\x1c\xe6\x27\xe1\x53\xe7\xf0\xe4"},
        {"12345678901234567890123456789012345678901234567890123456789012345678901234567890", 
         "\xe3\x3b\x4d\xdc\x9c\x38\xf2\x19\x9c\x3e\x7b\x16\x4f\xcc\x05\x36"}
};

static int num_sums = sizeof(md4sums) / sizeof(md4sums[0]); 
static int count;

#if 0
static int MDStringComp(const void *string, const void *sum)
{
        apr_md4_ctx_t context;
        unsigned char digest[APR_MD4_DIGESTSIZE];
        unsigned int len = strlen(string);

        apr_md4_init(&context);
        apr_md4_update(&context, (unsigned char *)string, len);
        apr_md4_final(digest, &context);
        return (memcmp(digest, sum, APR_MD4_DIGESTSIZE));

}
#endif

static void test_md4sum(abts_case *tc, void *data)
{
        apr_md4_ctx_t context;
        unsigned char digest[APR_MD4_DIGESTSIZE];
        const void *string = md4sums[count].string;
        const void *sum = md4sums[count].md4sum;
        unsigned int len = strlen(string);

        ABTS_ASSERT(tc, "apr_md4_init", (apr_md4_init(&context) == 0));
        ABTS_ASSERT(tc, "apr_md4_update", 
                    (apr_md4_update(&context, 
                                    (unsigned char *)string, len) == 0));
        
        ABTS_ASSERT(tc, "apr_md4_final", (apr_md4_final(digest, &context) ==0));
        ABTS_ASSERT(tc, "check for correct md4 digest", 
                    (memcmp(digest, sum, APR_MD4_DIGESTSIZE) == 0));
}

abts_suite *testmd4(abts_suite *suite)
{
        suite = ADD_SUITE(suite);

        for (count=0; count < num_sums; count++) {
            abts_run_test(suite, test_md4sum, NULL);
        }

        return suite;
}
