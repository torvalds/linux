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

#include <stdio.h>
#include <stdlib.h>

#include "apr_errno.h"
#include "apr_strings.h"
#include "apr_file_io.h"
#include "apr_thread_pool.h"
#include "apr_md5.h"
#include "apr_sha1.h"

#include "abts.h"
#include "testutil.h"

#if defined(WIN32) || defined(BEOS) || defined(NETWARE)
#define CRYPT_ALGO_SUPPORTED 0
#else
#define CRYPT_ALGO_SUPPORTED 1
#endif

#if defined __GLIBC_PREREQ
#if __GLIBC_PREREQ(2,7)
#define GLIBCSHA_ALGO_SUPPORTED
#endif
#endif

#if CRYPT_ALGO_SUPPORTED

static struct {
    const char *password;
    const char *hash;
} passwords[] =
{
/*
  passwords and hashes created with Apache's htpasswd utility like this:
  
  htpasswd -c -b passwords pass1 pass1
  htpasswd -b passwords pass2 pass2
  htpasswd -b passwords pass3 pass3
  htpasswd -b passwords pass4 pass4
  htpasswd -b passwords pass5 pass5
  htpasswd -b passwords pass6 pass6
  htpasswd -b passwords pass7 pass7
  htpasswd -b passwords pass8 pass8
  (insert Perl one-liner to convert to initializer :) )
 */
    {"pass1", "1fWDc9QWYCWrQ"},
    {"pass2", "1fiGx3u7QoXaM"},
    {"pass3", "1fzijMylTiwCs"},
    {"pass4", "nHUYc8U2UOP7s"},
    {"pass5", "nHpETGLGPwAmA"},
    {"pass6", "nHbsbWmJ3uyhc"},
    {"pass7", "nHQ3BbF0Y9vpI"},
    {"pass8", "nHZA1rViSldQk"}
};
static int num_passwords = sizeof(passwords) / sizeof(passwords[0]);

static void test_crypt(abts_case *tc, void *data)
{
    int i;

    for (i = 0; i < num_passwords; i++) {
        apr_assert_success(tc, "check for valid password",
                           apr_password_validate(passwords[i].password,
                                                 passwords[i].hash));
    }
}

#if APR_HAS_THREADS

static void * APR_THREAD_FUNC testing_thread(apr_thread_t *thd,
                                             void *data)
{
    abts_case *tc = data;
    int i;

    for (i = 0; i < 100; i++) {
        test_crypt(tc, NULL);
    }

    return APR_SUCCESS;
}

#define NUM_THR 20

/* test for threadsafe crypt() */
static void test_threadsafe(abts_case *tc, void *data)
{
    int i;
    apr_status_t rv;
    apr_thread_pool_t *thrp;

    rv = apr_thread_pool_create(&thrp, NUM_THR/2, NUM_THR, p);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    for (i = 0; i < NUM_THR; i++) {
        rv = apr_thread_pool_push(thrp, testing_thread, tc, 0, NULL);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
    }

    apr_thread_pool_destroy(thrp);
}
#endif

#endif /* CRYPT_ALGO_SUPPORTED */

static void test_shapass(abts_case *tc, void *data)
{
    const char *pass = "hellojed";
    const char *pass2 = "hellojed2";
    char hash[100];

    apr_sha1_base64(pass, strlen(pass), hash);

    apr_assert_success(tc, "SHA1 password validated",
                       apr_password_validate(pass, hash));
    APR_ASSERT_FAILURE(tc, "wrong SHA1 password should not validate",
                       apr_password_validate(pass2, hash));
}

static void test_md5pass(abts_case *tc, void *data)
{
    const char *pass = "hellojed", *salt = "sardine";
    const char *pass2 = "hellojed2";
    char hash[100];

    apr_md5_encode(pass, salt, hash, sizeof hash);

    apr_assert_success(tc, "MD5 password validated",
                       apr_password_validate(pass, hash));
    APR_ASSERT_FAILURE(tc, "wrong MD5 password should not validate",
                       apr_password_validate(pass2, hash));
}

#ifdef GLIBCSHA_ALGO_SUPPORTED

static struct {
    const char *password;
    const char *hash;
} glibc_sha_pws[] = {
    /* SHA256 */
    { "secret1", "$5$0123456789abcdef$SFX.CooXBS8oXsbAPgU/UyiCodhrLQ19sBgvcA3Zh1D" },
    { "secret2", "$5$rounds=100000$0123456789abcdef$dLXfO5m4d.xv8G66kpz2LyL0.Mi5wjLlH0m7rtgyhyB" },
    /* SHA512 */
    { "secret3", "$6$0123456789abcdef$idOsOfoWwnCQkJm9hd2hxS4NnEs9nBA9poOFXsvtrYSoSHaOToCfyUoZwKe.ZCZnq7D95tGVoi2jxZZMyVwTL1" },
    { "secret4", "$6$rounds=100000$0123456789abcdef$ZiAMjbeA.iIGTWxq2oks9Bvz9sfxaoGPgAtpwimPEwFwkSNMTK7lLwABzzldds/n4UgCQ16HqawPrCrePr4YX1" },
    { NULL, NULL }
};

static void test_glibc_shapass(abts_case *tc, void *data)
{
    int i = 0;
    while (glibc_sha_pws[i].password) {
        apr_assert_success(tc, "check for valid glibc crypt-sha password",
                           apr_password_validate(glibc_sha_pws[i].password,
                                                 glibc_sha_pws[i].hash));
        i++;
    }
}
#endif

static void test_bcryptpass(abts_case *tc, void *data)
{
    const char *pass = "hellojed";
    const char *pass2 = "hellojed2";
    unsigned char salt[] = "sardine_sardine";
    char hash[100];
    const char *hash2 = "$2a$08$qipUJiI9fySUN38hcbz.lucXvAmtgowKOWYtB9y3CXyl6lTknruou";
    const char *pass3 = "foobar";

    apr_assert_success(tc, "bcrypt encode password", 
                       apr_bcrypt_encode(pass, 5, salt, sizeof(salt), hash,
                                         sizeof(hash)));

    apr_assert_success(tc, "bcrypt password validated",
                       apr_password_validate(pass, hash));
    APR_ASSERT_FAILURE(tc, "wrong bcrypt password should not validate",
                       apr_password_validate(pass2, hash));
    apr_assert_success(tc, "bcrypt password validated",
                       apr_password_validate(pass3, hash2));
}


abts_suite *testpass(abts_suite *suite)
{
    suite = ADD_SUITE(suite);

#if CRYPT_ALGO_SUPPORTED
    abts_run_test(suite, test_crypt, NULL);
#if APR_HAS_THREADS
    abts_run_test(suite, test_threadsafe, NULL);
#endif
#endif /* CRYPT_ALGO_SUPPORTED */
    abts_run_test(suite, test_shapass, NULL);
    abts_run_test(suite, test_md5pass, NULL);
    abts_run_test(suite, test_bcryptpass, NULL);
#ifdef GLIBCSHA_ALGO_SUPPORTED
    abts_run_test(suite, test_glibc_shapass, NULL);
#endif
    
    return suite;
}
