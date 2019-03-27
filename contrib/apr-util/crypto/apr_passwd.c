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

#include "apr_strings.h"
#include "apr_md5.h"
#include "apr_lib.h"
#include "apr_sha1.h"
#include "apu_config.h"
#include "crypt_blowfish.h"

#if APR_HAVE_STRING_H
#include <string.h>
#endif
#if APR_HAVE_CRYPT_H
#include <crypt.h>
#endif
#if APR_HAVE_UNISTD_H
#include <unistd.h>
#endif
#if APR_HAVE_PTHREAD_H
#include <pthread.h>
#endif
#if APR_HAVE_STDLIB_H
#include <stdlib.h>
#endif

static const char * const apr1_id = "$apr1$";

#if !defined(WIN32) && !defined(BEOS) && !defined(NETWARE)
#if defined(APU_CRYPT_THREADSAFE) || !APR_HAS_THREADS || \
    defined(CRYPT_R_CRYPTD) || defined(CRYPT_R_STRUCT_CRYPT_DATA)

#define crypt_mutex_lock()
#define crypt_mutex_unlock()

#elif APR_HAVE_PTHREAD_H && defined(PTHREAD_MUTEX_INITIALIZER)

static pthread_mutex_t crypt_mutex = PTHREAD_MUTEX_INITIALIZER;
static void crypt_mutex_lock(void)
{
    pthread_mutex_lock(&crypt_mutex);
}

static void crypt_mutex_unlock(void)
{
    pthread_mutex_unlock(&crypt_mutex);
}

#else

#error apr_password_validate() is not threadsafe.  rebuild APR without thread support.

#endif
#endif

#if defined(WIN32) || defined(BEOS) || defined(NETWARE) || defined(__ANDROID__)
#define CRYPT_MISSING 1
#else
#define CRYPT_MISSING 0
#endif

/*
 * Validate a plaintext password against a smashed one.  Uses either
 * crypt() (if available) or apr_md5_encode() or apr_sha1_base64(), depending
 * upon the format of the smashed input password.  Returns APR_SUCCESS if
 * they match, or APR_EMISMATCH if they don't.  If the platform doesn't
 * support crypt, then the default check is against a clear text string.
 */
APU_DECLARE(apr_status_t) apr_password_validate(const char *passwd, 
                                                const char *hash)
{
    char sample[200];
#if !CRYPT_MISSING
    char *crypt_pw;
#endif
    if (hash[0] == '$'
        && hash[1] == '2'
        && (hash[2] == 'a' || hash[2] == 'y')
        && hash[3] == '$') {
        if (_crypt_blowfish_rn(passwd, hash, sample, sizeof(sample)) == NULL)
            return APR_FROM_OS_ERROR(errno);
    }
    else if (!strncmp(hash, apr1_id, strlen(apr1_id))) {
        /*
         * The hash was created using our custom algorithm.
         */
        apr_md5_encode(passwd, hash, sample, sizeof(sample));
    }
    else if (!strncmp(hash, APR_SHA1PW_ID, APR_SHA1PW_IDLEN)) {
         apr_sha1_base64(passwd, (int)strlen(passwd), sample);
    }
    else {
        /*
         * It's not our algorithm, so feed it to crypt() if possible.
         */
#if CRYPT_MISSING
        return (strcmp(passwd, hash) == 0) ? APR_SUCCESS : APR_EMISMATCH;
#elif defined(CRYPT_R_CRYPTD)
        apr_status_t rv;
        CRYPTD *buffer = malloc(sizeof(*buffer));

        if (buffer == NULL)
            return APR_ENOMEM;
        crypt_pw = crypt_r(passwd, hash, buffer);
        if (!crypt_pw)
            rv = APR_EMISMATCH;
        else
            rv = (strcmp(crypt_pw, hash) == 0) ? APR_SUCCESS : APR_EMISMATCH;
        free(buffer);
        return rv;
#elif defined(CRYPT_R_STRUCT_CRYPT_DATA)
        apr_status_t rv;
        struct crypt_data *buffer = malloc(sizeof(*buffer));

        if (buffer == NULL)
            return APR_ENOMEM;

#ifdef __GLIBC_PREREQ
        /*
         * For not too old glibc (>= 2.3.2), it's enough to set
         * buffer.initialized = 0. For < 2.3.2 and for other platforms,
         * we need to zero the whole struct.
         */
#if __GLIBC_PREREQ(2,4)
#define USE_CRYPT_DATA_INITALIZED
#endif
#endif

#ifdef USE_CRYPT_DATA_INITALIZED
        buffer->initialized = 0;
#else
        memset(buffer, 0, sizeof(*buffer));
#endif

        crypt_pw = crypt_r(passwd, hash, buffer);
        if (!crypt_pw)
            rv = APR_EMISMATCH;
        else
            rv = (strcmp(crypt_pw, hash) == 0) ? APR_SUCCESS : APR_EMISMATCH;
        free(buffer);
        return rv;
#else
        /* Do a bit of sanity checking since we know that crypt_r()
         * should always be used for threaded builds on AIX, and
         * problems in configure logic can result in the wrong
         * choice being made.
         */
#if defined(_AIX) && APR_HAS_THREADS
#error Configuration error!  crypt_r() should have been selected!
#endif
        {
            apr_status_t rv;

            /* Handle thread safety issues by holding a mutex around the
             * call to crypt().
             */
            crypt_mutex_lock();
            crypt_pw = crypt(passwd, hash);
            if (!crypt_pw) {
                rv = APR_EMISMATCH;
            }
            else {
                rv = (strcmp(crypt_pw, hash) == 0) ? APR_SUCCESS : APR_EMISMATCH;
            }
            crypt_mutex_unlock();
            return rv;
        }
#endif
    }
    return (strcmp(sample, hash) == 0) ? APR_SUCCESS : APR_EMISMATCH;
}

static const char * const bcrypt_id = "$2y$";
APU_DECLARE(apr_status_t) apr_bcrypt_encode(const char *pw,
                                            unsigned int count,
                                            const unsigned char *salt,
                                            apr_size_t salt_len,
                                            char *out, apr_size_t out_len)
{
    char setting[40];
    if (_crypt_gensalt_blowfish_rn(bcrypt_id, count, (const char *)salt,
                                   salt_len, setting, sizeof(setting)) == NULL)
        return APR_FROM_OS_ERROR(errno);
    if (_crypt_blowfish_rn(pw, setting, out, out_len) == NULL)
        return APR_FROM_OS_ERROR(errno);
    return APR_SUCCESS;
}
