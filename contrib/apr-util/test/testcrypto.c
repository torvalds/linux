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
#include "apu.h"
#include "apu_errno.h"
#include "apr_pools.h"
#include "apr_dso.h"
#include "apr_crypto.h"
#include "apr_strings.h"

#if APU_HAVE_CRYPTO

#define TEST_STRING "12345"
#define ALIGNED_STRING "123456789012345"

static const apr_crypto_driver_t *get_driver(abts_case *tc, apr_pool_t *pool,
        const char *name, const char *params)
{

    const apr_crypto_driver_t *driver = NULL;
    const apu_err_t *err = NULL;
    apr_status_t rv;

    rv = apr_crypto_init(pool);
    ABTS_ASSERT(tc, "failed to init apr_crypto", rv == APR_SUCCESS);

    rv = apr_crypto_get_driver(&driver, name, params, &err, pool);
    if (APR_SUCCESS != rv && err) {
        ABTS_NOT_IMPL(tc, err->msg);
        return NULL;
    }
    if (APR_ENOTIMPL == rv) {
        ABTS_NOT_IMPL(tc, (char *)driver);
        return NULL;
    }
    ABTS_ASSERT(tc, "failed to apr_crypto_get_driver", rv == APR_SUCCESS);
    ABTS_ASSERT(tc, "apr_crypto_get_driver returned NULL", driver != NULL);
    if (!driver || rv) {
        return NULL;
    }

    return driver;

}

static const apr_crypto_driver_t *get_nss_driver(abts_case *tc,
        apr_pool_t *pool)
{

    /* initialise NSS */
    return get_driver(tc, pool, "nss", "dir=data");

}

static const apr_crypto_driver_t *get_openssl_driver(abts_case *tc,
        apr_pool_t *pool)
{

    return get_driver(tc, pool, "openssl", NULL);

}

static apr_crypto_t *make(abts_case *tc, apr_pool_t *pool,
        const apr_crypto_driver_t *driver)
{

    apr_crypto_t *f = NULL;

    if (!driver) {
        return NULL;
    }

    /* get the context */
    apr_crypto_make(&f, driver, "engine=openssl", pool);
    ABTS_ASSERT(tc, "apr_crypto_make returned NULL", f != NULL);

    return f;

}

static const apr_crypto_key_t *passphrase(abts_case *tc, apr_pool_t *pool,
        const apr_crypto_driver_t *driver, const apr_crypto_t *f,
        apr_crypto_block_key_type_e type, apr_crypto_block_key_mode_e mode,
        int doPad, const char *description)
{

    apr_crypto_key_t *key = NULL;
    const apu_err_t *result = NULL;
    const char *pass = "secret";
    const char *salt = "salt";
    apr_status_t rv;

    if (!f) {
        return NULL;
    }

    /* init the passphrase */
    rv = apr_crypto_passphrase(&key, NULL, pass, strlen(pass),
            (unsigned char *) salt, strlen(salt), type, mode, doPad, 4096, f,
            pool);
    if (APR_ENOCIPHER == rv) {
        apr_crypto_error(&result, f);
        ABTS_NOT_IMPL(tc, apr_psprintf(pool,
                        "skipped: %s %s passphrase return APR_ENOCIPHER: error %d: %s (%s)\n",
                        description, apr_crypto_driver_name(driver), result->rc,
                        result->reason ? result->reason : "", result->msg ? result->msg : ""));
        return NULL;
    }
    else {
        if (APR_SUCCESS != rv) {
            apr_crypto_error(&result, f);
            fprintf(stderr, "passphrase: %s %s native error %d: %s (%s)\n",
                    description, apr_crypto_driver_name(driver), result->rc,
                    result->reason ? result->reason : "",
                    result->msg ? result->msg : "");
        }
        ABTS_ASSERT(tc, "apr_crypto_passphrase returned APR_ENOKEY", rv != APR_ENOKEY);
        ABTS_ASSERT(tc, "apr_crypto_passphrase returned APR_EPADDING", rv != APR_EPADDING);
        ABTS_ASSERT(tc, "apr_crypto_passphrase returned APR_EKEYTYPE", rv != APR_EKEYTYPE);
        ABTS_ASSERT(tc, "failed to apr_crypto_passphrase", rv == APR_SUCCESS);
        ABTS_ASSERT(tc, "apr_crypto_passphrase returned NULL context", key != NULL);
    }
    if (rv) {
        return NULL;
    }
    return key;

}

static unsigned char *encrypt_block(abts_case *tc, apr_pool_t *pool,
        const apr_crypto_driver_t *driver, const apr_crypto_t *f,
        const apr_crypto_key_t *key, const unsigned char *in,
        const apr_size_t inlen, unsigned char **cipherText,
        apr_size_t *cipherTextLen, const unsigned char **iv,
        apr_size_t *blockSize, const char *description)
{

    apr_crypto_block_t *block = NULL;
    const apu_err_t *result = NULL;
    apr_size_t len = 0;
    apr_status_t rv;

    if (!driver || !f || !key || !in) {
        return NULL;
    }

    /* init the encryption */
    rv = apr_crypto_block_encrypt_init(&block, iv, key, blockSize, pool);
    if (APR_ENOTIMPL == rv) {
        ABTS_NOT_IMPL(tc, "apr_crypto_block_encrypt_init returned APR_ENOTIMPL");
    }
    else {
        if (APR_SUCCESS != rv) {
            apr_crypto_error(&result, f);
            fprintf(stderr, "encrypt_init: %s %s native error %d: %s (%s)\n",
                    description, apr_crypto_driver_name(driver), result->rc,
                    result->reason ? result->reason : "",
                    result->msg ? result->msg : "");
        }
        ABTS_ASSERT(tc, "apr_crypto_block_encrypt_init returned APR_ENOKEY", rv != APR_ENOKEY);
        ABTS_ASSERT(tc, "apr_crypto_block_encrypt_init returned APR_ENOIV", rv != APR_ENOIV);
        ABTS_ASSERT(tc, "apr_crypto_block_encrypt_init returned APR_EKEYTYPE", rv != APR_EKEYTYPE);
        ABTS_ASSERT(tc, "apr_crypto_block_encrypt_init returned APR_EKEYLENGTH", rv != APR_EKEYLENGTH);
        ABTS_ASSERT(tc, "failed to apr_crypto_block_encrypt_init", rv == APR_SUCCESS);
        ABTS_ASSERT(tc, "apr_crypto_block_encrypt_init returned NULL context", block != NULL);
    }
    if (!block || rv) {
        return NULL;
    }

    /* encrypt the block */
    rv = apr_crypto_block_encrypt(cipherText, cipherTextLen, in, inlen, block);
    if (APR_SUCCESS != rv) {
        apr_crypto_error(&result, f);
        fprintf(stderr, "encrypt: %s %s native error %d: %s (%s)\n",
                description, apr_crypto_driver_name(driver), result->rc,
                result->reason ? result->reason : "", result->msg ? result->msg
                        : "");
    }
    ABTS_ASSERT(tc, "apr_crypto_block_encrypt returned APR_ECRYPT", rv != APR_ECRYPT);
    ABTS_ASSERT(tc, "failed to apr_crypto_block_encrypt", rv == APR_SUCCESS);
    ABTS_ASSERT(tc, "apr_crypto_block_encrypt failed to allocate buffer", *cipherText != NULL);
    if (rv) {
        return NULL;
    }

    /* finalise the encryption */
    rv = apr_crypto_block_encrypt_finish(*cipherText + *cipherTextLen, &len,
            block);
    if (APR_SUCCESS != rv) {
        apr_crypto_error(&result, f);
        fprintf(stderr, "encrypt_finish: %s %s native error %d: %s (%s)\n",
                description, apr_crypto_driver_name(driver), result->rc,
                result->reason ? result->reason : "", result->msg ? result->msg
                        : "");
    }
    ABTS_ASSERT(tc, "apr_crypto_block_encrypt_finish returned APR_ECRYPT", rv != APR_ECRYPT);
    ABTS_ASSERT(tc, "apr_crypto_block_encrypt_finish returned APR_EPADDING", rv != APR_EPADDING);
    ABTS_ASSERT(tc, "failed to apr_crypto_block_encrypt_finish", rv == APR_SUCCESS);
    *cipherTextLen += len;
    apr_crypto_block_cleanup(block);
    if (rv) {
        return NULL;
    }

    return *cipherText;

}

static unsigned char *decrypt_block(abts_case *tc, apr_pool_t *pool,
        const apr_crypto_driver_t *driver, const apr_crypto_t *f,
        const apr_crypto_key_t *key, unsigned char *cipherText,
        apr_size_t cipherTextLen, unsigned char **plainText,
        apr_size_t *plainTextLen, const unsigned char *iv,
        apr_size_t *blockSize, const char *description)
{

    apr_crypto_block_t *block = NULL;
    const apu_err_t *result = NULL;
    apr_size_t len = 0;
    apr_status_t rv;

    if (!driver || !f || !key || !cipherText) {
        return NULL;
    }

    /* init the decryption */
    rv = apr_crypto_block_decrypt_init(&block, blockSize, iv, key, pool);
    if (APR_ENOTIMPL == rv) {
        ABTS_NOT_IMPL(tc, "apr_crypto_block_decrypt_init returned APR_ENOTIMPL");
    }
    else {
        if (APR_SUCCESS != rv) {
            apr_crypto_error(&result, f);
            fprintf(stderr, "decrypt_init: %s %s native error %d: %s (%s)\n",
                    description, apr_crypto_driver_name(driver), result->rc,
                    result->reason ? result->reason : "",
                    result->msg ? result->msg : "");
        }
        ABTS_ASSERT(tc, "apr_crypto_block_decrypt_init returned APR_ENOKEY", rv != APR_ENOKEY);
        ABTS_ASSERT(tc, "apr_crypto_block_decrypt_init returned APR_ENOIV", rv != APR_ENOIV);
        ABTS_ASSERT(tc, "apr_crypto_block_decrypt_init returned APR_EKEYTYPE", rv != APR_EKEYTYPE);
        ABTS_ASSERT(tc, "apr_crypto_block_decrypt_init returned APR_EKEYLENGTH", rv != APR_EKEYLENGTH);
        ABTS_ASSERT(tc, "failed to apr_crypto_block_decrypt_init", rv == APR_SUCCESS);
        ABTS_ASSERT(tc, "apr_crypto_block_decrypt_init returned NULL context", block != NULL);
    }
    if (!block || rv) {
        return NULL;
    }

    /* decrypt the block */
    rv = apr_crypto_block_decrypt(plainText, plainTextLen, cipherText,
            cipherTextLen, block);
    if (APR_SUCCESS != rv) {
        apr_crypto_error(&result, f);
        fprintf(stderr, "decrypt: %s %s native error %d: %s (%s)\n",
                description, apr_crypto_driver_name(driver), result->rc,
                result->reason ? result->reason : "", result->msg ? result->msg
                        : "");
    }
    ABTS_ASSERT(tc, "apr_crypto_block_decrypt returned APR_ECRYPT", rv != APR_ECRYPT);
    ABTS_ASSERT(tc, "failed to apr_crypto_block_decrypt", rv == APR_SUCCESS);
    ABTS_ASSERT(tc, "apr_crypto_block_decrypt failed to allocate buffer", *plainText != NULL);
    if (rv) {
        return NULL;
    }

    /* finalise the decryption */
    rv = apr_crypto_block_decrypt_finish(*plainText + *plainTextLen, &len,
            block);
    if (APR_SUCCESS != rv) {
        apr_crypto_error(&result, f);
        fprintf(stderr, "decrypt_finish: %s %s native error %d: %s (%s)\n",
                description, apr_crypto_driver_name(driver), result->rc,
                result->reason ? result->reason : "", result->msg ? result->msg
                        : "");
    }
    ABTS_ASSERT(tc, "apr_crypto_block_decrypt_finish returned APR_ECRYPT", rv != APR_ECRYPT);
    ABTS_ASSERT(tc, "apr_crypto_block_decrypt_finish returned APR_EPADDING", rv != APR_EPADDING);
    ABTS_ASSERT(tc, "failed to apr_crypto_block_decrypt_finish", rv == APR_SUCCESS);
    if (rv) {
        return NULL;
    }

    *plainTextLen += len;
    apr_crypto_block_cleanup(block);

    return *plainText;

}

/**
 * Interoperability test.
 *
 * data must point at an array of two driver structures. Data will be encrypted
 * with the first driver, and decrypted with the second.
 *
 * If the two drivers interoperate, the test passes.
 */
static void crypto_block_cross(abts_case *tc, apr_pool_t *pool,
        const apr_crypto_driver_t **drivers,
        const apr_crypto_block_key_type_e type,
        const apr_crypto_block_key_mode_e mode, int doPad,
        const unsigned char *in, apr_size_t inlen, const char *description)
{
    const apr_crypto_driver_t *driver1 = drivers[0];
    const apr_crypto_driver_t *driver2 = drivers[1];
    apr_crypto_t *f1 = NULL;
    apr_crypto_t *f2 = NULL;
    const apr_crypto_key_t *key1 = NULL;
    const apr_crypto_key_t *key2 = NULL;

    unsigned char *cipherText = NULL;
    apr_size_t cipherTextLen = 0;
    unsigned char *plainText = NULL;
    apr_size_t plainTextLen = 0;
    const unsigned char *iv = NULL;
    apr_size_t blockSize = 0;

    f1 = make(tc, pool, driver1);
    f2 = make(tc, pool, driver2);
    key1 = passphrase(tc, pool, driver1, f1, type, mode, doPad, description);
    key2 = passphrase(tc, pool, driver2, f2, type, mode, doPad, description);

    cipherText = encrypt_block(tc, pool, driver1, f1, key1, in, inlen,
            &cipherText, &cipherTextLen, &iv, &blockSize, description);
    plainText = decrypt_block(tc, pool, driver2, f2, key2, cipherText,
            cipherTextLen, &plainText, &plainTextLen, iv, &blockSize,
            description);

    if (cipherText && plainText) {
        if (memcmp(in, plainText, inlen)) {
            fprintf(stderr, "cross mismatch: %s %s/%s\n", description,
                    apr_crypto_driver_name(driver1), apr_crypto_driver_name(
                            driver2));
        }
        ABTS_STR_EQUAL(tc, (char *)in, (char *)plainText);
    }

}

/**
 * Test initialisation.
 */
static void test_crypto_init(abts_case *tc, void *data)
{
    apr_pool_t *pool = NULL;
    apr_status_t rv;

    apr_pool_create(&pool, NULL);

    rv = apr_crypto_init(pool);
    ABTS_ASSERT(tc, "failed to init apr_crypto", rv == APR_SUCCESS);

    apr_pool_destroy(pool);

}

/**
 * Simple test of OpenSSL block crypt.
 */
static void test_crypto_block_openssl(abts_case *tc, void *data)
{
    apr_pool_t *pool = NULL;
    const apr_crypto_driver_t *drivers[] = { NULL, NULL };

    const unsigned char *in = (const unsigned char *) ALIGNED_STRING;
    apr_size_t inlen = sizeof(ALIGNED_STRING);

    apr_pool_create(&pool, NULL);
    drivers[0] = get_openssl_driver(tc, pool);
    drivers[1] = get_openssl_driver(tc, pool);
    crypto_block_cross(tc, pool, drivers, APR_KEY_3DES_192, APR_MODE_CBC, 0,
            in, inlen, "KEY_3DES_192/MODE_CBC");
    crypto_block_cross(tc, pool, drivers, APR_KEY_3DES_192, APR_MODE_ECB, 0,
            in, inlen, "KEY_3DES_192/MODE_ECB");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_256, APR_MODE_CBC, 0, in,
            inlen, "KEY_AES_256/MODE_CBC");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_256, APR_MODE_ECB, 0, in,
            inlen, "KEY_AES_256/MODE_ECB");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_192, APR_MODE_CBC, 0, in,
            inlen, "KEY_AES_192/MODE_CBC");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_192, APR_MODE_ECB, 0, in,
            inlen, "KEY_AES_192/MODE_ECB");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_128, APR_MODE_CBC, 0, in,
            inlen, "KEY_AES_128/MODE_CBC");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_128, APR_MODE_ECB, 0, in,
            inlen, "KEY_AES_128/MODE_ECB");
    apr_pool_destroy(pool);

}

/**
 * Simple test of NSS block crypt.
 */
static void test_crypto_block_nss(abts_case *tc, void *data)
{
    apr_pool_t *pool = NULL;
    const apr_crypto_driver_t *drivers[] = { NULL, NULL };

    const unsigned char *in = (const unsigned char *) ALIGNED_STRING;
    apr_size_t inlen = sizeof(ALIGNED_STRING);

    apr_pool_create(&pool, NULL);
    drivers[0] = get_nss_driver(tc, pool);
    drivers[1] = get_nss_driver(tc, pool);
    crypto_block_cross(tc, pool, drivers, APR_KEY_3DES_192, APR_MODE_CBC, 0,
            in, inlen, "KEY_3DES_192/MODE_CBC");
    /* KEY_3DES_192 / MODE_ECB doesn't work on NSS */
    /* crypto_block_cross(tc, pool, drivers, KEY_3DES_192, MODE_ECB, 0, in, inlen, "KEY_3DES_192/MODE_ECB"); */
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_256, APR_MODE_CBC, 0, in,
            inlen, "KEY_AES_256/MODE_CBC");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_256, APR_MODE_ECB, 0, in,
            inlen, "KEY_AES_256/MODE_ECB");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_192, APR_MODE_CBC, 0, in,
            inlen, "KEY_AES_192/MODE_CBC");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_192, APR_MODE_ECB, 0, in,
            inlen, "KEY_AES_192/MODE_ECB");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_128, APR_MODE_CBC, 0, in,
            inlen, "KEY_AES_128/MODE_CBC");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_128, APR_MODE_ECB, 0, in,
            inlen, "KEY_AES_128/MODE_ECB");
    apr_pool_destroy(pool);

}

/**
 * Encrypt NSS, decrypt OpenSSL.
 */
static void test_crypto_block_nss_openssl(abts_case *tc, void *data)
{
    apr_pool_t *pool = NULL;
    const apr_crypto_driver_t *drivers[] = { NULL, NULL };

    const unsigned char *in = (const unsigned char *) ALIGNED_STRING;
    apr_size_t inlen = sizeof(ALIGNED_STRING);

    apr_pool_create(&pool, NULL);
    drivers[0] = get_nss_driver(tc, pool);
    drivers[1] = get_openssl_driver(tc, pool);

    crypto_block_cross(tc, pool, drivers, APR_KEY_3DES_192, APR_MODE_CBC, 0,
            in, inlen, "KEY_3DES_192/MODE_CBC");

    /* KEY_3DES_192 / MODE_ECB doesn't work on NSS */
    /* crypto_block_cross(tc, pool, drivers, KEY_3DES_192, MODE_ECB, 0, in, inlen, "KEY_3DES_192/MODE_ECB"); */
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_256, APR_MODE_CBC, 0, in,
            inlen, "KEY_AES_256/MODE_CBC");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_256, APR_MODE_ECB, 0, in,
            inlen, "KEY_AES_256/MODE_ECB");

    /* all 4 of these tests fail to interoperate - a clue from the xml-security code is that
     * NSS cannot distinguish between the 128 and 192 bit versions of AES. Will need to be
     * investigated.
     */
    /*
     crypto_block_cross(tc, pool, drivers, KEY_AES_192, MODE_CBC, 0, in, inlen, "KEY_AES_192/MODE_CBC");
     crypto_block_cross(tc, pool, drivers, KEY_AES_192, MODE_ECB, 0, in, inlen, "KEY_AES_192/MODE_ECB");
     crypto_block_cross(tc, pool, drivers, KEY_AES_128, MODE_CBC, 0, in, inlen, "KEY_AES_128/MODE_CBC");
     crypto_block_cross(tc, pool, drivers, KEY_AES_128, MODE_ECB, 0, in, inlen, "KEY_AES_128/MODE_ECB");
     */
    apr_pool_destroy(pool);

}

/**
 * Encrypt OpenSSL, decrypt NSS.
 */
static void test_crypto_block_openssl_nss(abts_case *tc, void *data)
{
    apr_pool_t *pool = NULL;
    const apr_crypto_driver_t *drivers[] = { NULL, NULL };

    const unsigned char *in = (const unsigned char *) ALIGNED_STRING;
    apr_size_t inlen = sizeof(ALIGNED_STRING);

    apr_pool_create(&pool, NULL);
    drivers[0] = get_openssl_driver(tc, pool);
    drivers[1] = get_nss_driver(tc, pool);
    crypto_block_cross(tc, pool, drivers, APR_KEY_3DES_192, APR_MODE_CBC, 0,
            in, inlen, "KEY_3DES_192/MODE_CBC");

    /* KEY_3DES_192 / MODE_ECB doesn't work on NSS */
    /* crypto_block_cross(tc, pool, drivers, KEY_3DES_192, MODE_ECB, 0, in, inlen, "KEY_3DES_192/MODE_ECB"); */

    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_256, APR_MODE_CBC, 0, in,
            inlen, "KEY_AES_256/MODE_CBC");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_256, APR_MODE_ECB, 0, in,
            inlen, "KEY_AES_256/MODE_ECB");

    /* all 4 of these tests fail to interoperate - a clue from the xml-security code is that
     * NSS cannot distinguish between the 128 and 192 bit versions of AES. Will need to be
     * investigated.
     */
    /*
     crypto_block_cross(tc, pool, drivers, KEY_AES_192, MODE_CBC, 0, in, inlen, "KEY_AES_192/MODE_CBC");
     crypto_block_cross(tc, pool, drivers, KEY_AES_192, MODE_ECB, 0, in, inlen, "KEY_AES_192/MODE_ECB");
     crypto_block_cross(tc, pool, drivers, KEY_AES_128, MODE_CBC, 0, in, inlen, "KEY_AES_128/MODE_CBC");
     crypto_block_cross(tc, pool, drivers, KEY_AES_128, MODE_ECB, 0, in, inlen, "KEY_AES_128/MODE_ECB");
     */
    apr_pool_destroy(pool);

}

/**
 * Simple test of OpenSSL block crypt.
 */
static void test_crypto_block_openssl_pad(abts_case *tc, void *data)
{
    apr_pool_t *pool = NULL;
    const apr_crypto_driver_t *drivers[] = { NULL, NULL };

    const unsigned char *in = (const unsigned char *) TEST_STRING;
    apr_size_t inlen = sizeof(TEST_STRING);

    apr_pool_create(&pool, NULL);
    drivers[0] = get_openssl_driver(tc, pool);
    drivers[1] = get_openssl_driver(tc, pool);

    crypto_block_cross(tc, pool, drivers, APR_KEY_3DES_192, APR_MODE_CBC, 1,
            in, inlen, "KEY_3DES_192/MODE_CBC");
    crypto_block_cross(tc, pool, drivers, APR_KEY_3DES_192, APR_MODE_ECB, 1,
            in, inlen, "KEY_3DES_192/MODE_ECB");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_256, APR_MODE_CBC, 1, in,
            inlen, "KEY_AES_256/MODE_CBC");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_256, APR_MODE_ECB, 1, in,
            inlen, "KEY_AES_256/MODE_ECB");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_192, APR_MODE_CBC, 1, in,
            inlen, "KEY_AES_192/MODE_CBC");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_192, APR_MODE_ECB, 1, in,
            inlen, "KEY_AES_192/MODE_ECB");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_128, APR_MODE_CBC, 1, in,
            inlen, "KEY_AES_128/MODE_CBC");
    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_128, APR_MODE_ECB, 1, in,
            inlen, "KEY_AES_128/MODE_ECB");

    apr_pool_destroy(pool);

}

/**
 * Simple test of NSS block crypt.
 */
static void test_crypto_block_nss_pad(abts_case *tc, void *data)
{
    apr_pool_t *pool = NULL;
    const apr_crypto_driver_t *drivers[] =
    { NULL, NULL };

    const unsigned char *in = (const unsigned char *) TEST_STRING;
    apr_size_t inlen = sizeof(TEST_STRING);

    apr_pool_create(&pool, NULL);
    drivers[0] = get_nss_driver(tc, pool);
    drivers[1] = get_nss_driver(tc, pool);

    crypto_block_cross(tc, pool, drivers, APR_KEY_3DES_192, APR_MODE_CBC, 1,
            in, inlen, "KEY_3DES_192/MODE_CBC");
    /* KEY_3DES_192 / MODE_ECB doesn't work on NSS */
    /* crypto_block_cross(tc, pool, drivers, KEY_3DES_192, MODE_ECB, 1, in, inlen, "KEY_3DES_192/MODE_ECB"); */

    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_256, APR_MODE_CBC, 1, in,
            inlen, "KEY_AES_256/MODE_CBC");

    /* KEY_AES_256 / MODE_ECB doesn't support padding on NSS */
    /*crypto_block_cross(tc, pool, drivers, KEY_AES_256, MODE_ECB, 1, in, inlen, "KEY_AES_256/MODE_ECB");*/

    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_192, APR_MODE_CBC, 1, in,
            inlen, "KEY_AES_192/MODE_CBC");

    /* KEY_AES_256 / MODE_ECB doesn't support padding on NSS */
    /*crypto_block_cross(tc, pool, drivers, KEY_AES_192, MODE_ECB, 1, in, inlen, "KEY_AES_192/MODE_ECB");*/

    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_128, APR_MODE_CBC, 1, in,
            inlen, "KEY_AES_128/MODE_CBC");

    /* KEY_AES_256 / MODE_ECB doesn't support padding on NSS */
    /*crypto_block_cross(tc, pool, drivers, KEY_AES_128, MODE_ECB, 1, in, inlen, "KEY_AES_128/MODE_ECB");*/

    apr_pool_destroy(pool);

}

/**
 * Encrypt NSS, decrypt OpenSSL.
 */
static void test_crypto_block_nss_openssl_pad(abts_case *tc, void *data)
{
    apr_pool_t *pool = NULL;
    const apr_crypto_driver_t *drivers[] = { NULL, NULL };

    const unsigned char *in = (const unsigned char *) TEST_STRING;
    apr_size_t inlen = sizeof(TEST_STRING);

    apr_pool_create(&pool, NULL);
    drivers[0] = get_nss_driver(tc, pool);
    drivers[1] = get_openssl_driver(tc, pool);

    crypto_block_cross(tc, pool, drivers, APR_KEY_3DES_192, APR_MODE_CBC, 1,
            in, inlen, "KEY_3DES_192/MODE_CBC");

    /* KEY_3DES_192 / MODE_ECB doesn't work on NSS */
    /* crypto_block_cross(tc, pool, drivers, KEY_3DES_192, MODE_ECB, 1, in, inlen, "KEY_3DES_192/MODE_ECB"); */

    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_256, APR_MODE_CBC, 1, in,
            inlen, "KEY_AES_256/MODE_CBC");

    /* KEY_AES_256 / MODE_ECB doesn't support padding on NSS */
    /*crypto_block_cross(tc, pool, drivers, KEY_AES_256, MODE_ECB, 1, in, inlen, "KEY_AES_256/MODE_ECB");*/

    /* all 4 of these tests fail to interoperate - a clue from the xml-security code is that
     * NSS cannot distinguish between the 128 and 192 bit versions of AES. Will need to be
     * investigated.
     */
    /*
     crypto_block_cross(tc, pool, drivers, KEY_AES_192, MODE_CBC, 1, in, inlen, "KEY_AES_192/MODE_CBC");
     crypto_block_cross(tc, pool, drivers, KEY_AES_192, MODE_ECB, 1, in, inlen, "KEY_AES_192/MODE_ECB");
     crypto_block_cross(tc, pool, drivers, KEY_AES_128, MODE_CBC, 1, in, inlen, "KEY_AES_128/MODE_CBC");
     crypto_block_cross(tc, pool, drivers, KEY_AES_128, MODE_ECB, 1, in, inlen, "KEY_AES_128/MODE_ECB");
     */
    apr_pool_destroy(pool);

}

/**
 * Encrypt OpenSSL, decrypt NSS.
 */
static void test_crypto_block_openssl_nss_pad(abts_case *tc, void *data)
{
    apr_pool_t *pool = NULL;
    const apr_crypto_driver_t *drivers[] = { NULL, NULL };

    const unsigned char *in = (const unsigned char *) TEST_STRING;
    apr_size_t inlen = sizeof(TEST_STRING);

    apr_pool_create(&pool, NULL);
    drivers[0] = get_openssl_driver(tc, pool);
    drivers[1] = get_nss_driver(tc, pool);
    crypto_block_cross(tc, pool, drivers, APR_KEY_3DES_192, APR_MODE_CBC, 1,
            in, inlen, "KEY_3DES_192/MODE_CBC");

    /* KEY_3DES_192 / MODE_ECB doesn't work on NSS */
    /* crypto_block_cross(tc, pool, drivers, KEY_3DES_192, MODE_ECB, 1, in, inlen, "KEY_3DES_192/MODE_ECB"); */

    crypto_block_cross(tc, pool, drivers, APR_KEY_AES_256, APR_MODE_CBC, 1, in,
            inlen, "KEY_AES_256/MODE_CBC");

    /* KEY_AES_256 / MODE_ECB doesn't support padding on NSS */
    /*crypto_block_cross(tc, pool, drivers, KEY_AES_256, MODE_ECB, 1, in, inlen, "KEY_AES_256/MODE_ECB");*/

    /* all 4 of these tests fail to interoperate - a clue from the xml-security code is that
     * NSS cannot distinguish between the 128 and 192 bit versions of AES. Will need to be
     * investigated.
     */
    /*
     crypto_block_cross(tc, pool, drivers, KEY_AES_192, MODE_CBC, 1, in, inlen, "KEY_AES_192/MODE_CBC");
     crypto_block_cross(tc, pool, drivers, KEY_AES_192, MODE_ECB, 1, in, inlen, "KEY_AES_192/MODE_ECB");
     crypto_block_cross(tc, pool, drivers, KEY_AES_128, MODE_CBC, 1, in, inlen, "KEY_AES_128/MODE_CBC");
     crypto_block_cross(tc, pool, drivers, KEY_AES_128, MODE_ECB, 1, in, inlen, "KEY_AES_128/MODE_ECB");
     */
    apr_pool_destroy(pool);

}

/**
 * Get Types, OpenSSL.
 */
static void test_crypto_get_block_key_types_openssl(abts_case *tc, void *data)
{
    apr_pool_t *pool = NULL;
    const apr_crypto_driver_t *driver;
    apr_crypto_t *f;
    apr_hash_t *types;
    int *key_3des_192;
    int *key_aes_128;
    int *key_aes_192;
    int *key_aes_256;

    apr_pool_create(&pool, NULL);
    driver = get_openssl_driver(tc, pool);
    if (driver) {

        f = make(tc, pool, driver);
        apr_crypto_get_block_key_types(&types, f);

        key_3des_192 = apr_hash_get(types, "3des192", APR_HASH_KEY_STRING);
        ABTS_PTR_NOTNULL(tc, key_3des_192);
        ABTS_INT_EQUAL(tc, *key_3des_192, APR_KEY_3DES_192);

        key_aes_128 = apr_hash_get(types, "aes128", APR_HASH_KEY_STRING);
        ABTS_PTR_NOTNULL(tc, key_aes_128);
        ABTS_INT_EQUAL(tc, *key_aes_128, APR_KEY_AES_128);

        key_aes_192 = apr_hash_get(types, "aes192", APR_HASH_KEY_STRING);
        ABTS_PTR_NOTNULL(tc, key_aes_192);
        ABTS_INT_EQUAL(tc, *key_aes_192, APR_KEY_AES_192);

        key_aes_256 = apr_hash_get(types, "aes256", APR_HASH_KEY_STRING);
        ABTS_PTR_NOTNULL(tc, key_aes_256);
        ABTS_INT_EQUAL(tc, *key_aes_256, APR_KEY_AES_256);

    }

    apr_pool_destroy(pool);

}

/**
 * Get Types, NSS.
 */
static void test_crypto_get_block_key_types_nss(abts_case *tc, void *data)
{
    apr_pool_t *pool = NULL;
    const apr_crypto_driver_t *driver;
    apr_crypto_t *f;
    apr_hash_t *types;
    int *key_3des_192;
    int *key_aes_128;
    int *key_aes_192;
    int *key_aes_256;

    apr_pool_create(&pool, NULL);
    driver = get_nss_driver(tc, pool);
    if (driver) {

        f = make(tc, pool, driver);
        apr_crypto_get_block_key_types(&types, f);

        key_3des_192 = apr_hash_get(types, "3des192", APR_HASH_KEY_STRING);
        ABTS_PTR_NOTNULL(tc, key_3des_192);
        ABTS_INT_EQUAL(tc, *key_3des_192, APR_KEY_3DES_192);

        key_aes_128 = apr_hash_get(types, "aes128", APR_HASH_KEY_STRING);
        ABTS_PTR_NOTNULL(tc, key_aes_128);
        ABTS_INT_EQUAL(tc, *key_aes_128, APR_KEY_AES_128);

        key_aes_192 = apr_hash_get(types, "aes192", APR_HASH_KEY_STRING);
        ABTS_PTR_NOTNULL(tc, key_aes_192);
        ABTS_INT_EQUAL(tc, *key_aes_192, APR_KEY_AES_192);

        key_aes_256 = apr_hash_get(types, "aes256", APR_HASH_KEY_STRING);
        ABTS_PTR_NOTNULL(tc, key_aes_256);
        ABTS_INT_EQUAL(tc, *key_aes_256, APR_KEY_AES_256);

    }

    apr_pool_destroy(pool);

}

/**
 * Get Modes, OpenSSL.
 */
static void test_crypto_get_block_key_modes_openssl(abts_case *tc, void *data)
{
    apr_pool_t *pool = NULL;
    const apr_crypto_driver_t *driver;
    apr_crypto_t *f;
    apr_hash_t *modes;
    int *mode_ecb;
    int *mode_cbc;

    apr_pool_create(&pool, NULL);
    driver = get_openssl_driver(tc, pool);
    if (driver) {

        f = make(tc, pool, driver);
        apr_crypto_get_block_key_modes(&modes, f);

        mode_ecb = apr_hash_get(modes, "ecb", APR_HASH_KEY_STRING);
        ABTS_PTR_NOTNULL(tc, mode_ecb);
        ABTS_INT_EQUAL(tc, *mode_ecb, APR_MODE_ECB);

        mode_cbc = apr_hash_get(modes, "cbc", APR_HASH_KEY_STRING);
        ABTS_PTR_NOTNULL(tc, mode_cbc);
        ABTS_INT_EQUAL(tc, *mode_cbc, APR_MODE_CBC);

    }

    apr_pool_destroy(pool);

}

/**
 * Get Modes, NSS.
 */
static void test_crypto_get_block_key_modes_nss(abts_case *tc, void *data)
{
    apr_pool_t *pool = NULL;
    const apr_crypto_driver_t *driver;
    apr_crypto_t *f;
    apr_hash_t *modes;
    int *mode_ecb;
    int *mode_cbc;

    apr_pool_create(&pool, NULL);
    driver = get_nss_driver(tc, pool);
    if (driver) {

        f = make(tc, pool, driver);
        apr_crypto_get_block_key_modes(&modes, f);

        mode_ecb = apr_hash_get(modes, "ecb", APR_HASH_KEY_STRING);
        ABTS_PTR_NOTNULL(tc, mode_ecb);
        ABTS_INT_EQUAL(tc, *mode_ecb, APR_MODE_ECB);

        mode_cbc = apr_hash_get(modes, "cbc", APR_HASH_KEY_STRING);
        ABTS_PTR_NOTNULL(tc, mode_cbc);
        ABTS_INT_EQUAL(tc, *mode_cbc, APR_MODE_CBC);

    }

    apr_pool_destroy(pool);

}

abts_suite *testcrypto(abts_suite *suite)
{
    suite = ADD_SUITE(suite);

    /* test simple init and shutdown */
    abts_run_test(suite, test_crypto_init, NULL);

    /* test a simple encrypt / decrypt operation - openssl */
    abts_run_test(suite, test_crypto_block_openssl, NULL);

    /* test a padded encrypt / decrypt operation - openssl */
    abts_run_test(suite, test_crypto_block_openssl_pad, NULL);

    /* test a simple encrypt / decrypt operation - nss */
    abts_run_test(suite, test_crypto_block_nss, NULL);

    /* test a padded encrypt / decrypt operation - nss */
    abts_run_test(suite, test_crypto_block_nss_pad, NULL);

    /* test encrypt nss / decrypt openssl */
    abts_run_test(suite, test_crypto_block_nss_openssl, NULL);

    /* test padded encrypt nss / decrypt openssl */
    abts_run_test(suite, test_crypto_block_nss_openssl_pad, NULL);

    /* test encrypt openssl / decrypt nss */
    abts_run_test(suite, test_crypto_block_openssl_nss, NULL);

    /* test padded encrypt openssl / decrypt nss */
    abts_run_test(suite, test_crypto_block_openssl_nss_pad, NULL);

    /* test block key types openssl */
    abts_run_test(suite, test_crypto_get_block_key_types_openssl, NULL);

    /* test block key types nss */
    abts_run_test(suite, test_crypto_get_block_key_types_nss, NULL);

    /* test block key modes openssl */
    abts_run_test(suite, test_crypto_get_block_key_modes_openssl, NULL);

    /* test block key modes nss */
    abts_run_test(suite, test_crypto_get_block_key_modes_nss, NULL);

    return suite;
}

#else

/**
 * Dummy test suite when crypto is turned off.
 */
abts_suite *testcrypto(abts_suite *suite)
{
    return ADD_SUITE(suite);
}

#endif /* APU_HAVE_CRYPTO */
