/*
 * crypto.c :  cryptographic routines
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include "crypto.h"

#ifdef SVN_HAVE_CRYPTO
#include <apr_random.h>
#include <apr_crypto.h>
#endif /* SVN_HAVE_CRYPTO */

#include "svn_types.h"
#include "svn_checksum.h"

#include "svn_private_config.h"
#include "private/svn_atomic.h"


/* 1000 iterations is the recommended minimum, per RFC 2898, section 4.2.  */
#define NUM_ITERATIONS 1000


/* Size (in bytes) of the random data we'll prepend to encrypted data. */
#define RANDOM_PREFIX_LEN 4


/* A structure for containing Subversion's cryptography-related bits
   (so we can avoid passing around APR-isms outside this module). */
struct svn_crypto__ctx_t {
#ifdef SVN_HAVE_CRYPTO
  apr_crypto_t *crypto;  /* APR cryptography context. */

#if 0
  /* ### For now, we will use apr_generate_random_bytes(). If we need
     ### more strength, then we can set this member using
     ### apr_random_standard_new(), then use
     ### apr_generate_random_bytes() to generate entropy for seeding
     ### apr_random_t. See httpd/server/core.c:ap_init_rng()  */
  apr_random_t *rand;
#endif /* 0 */
#else /* SVN_HAVE_CRYPTO */
  int unused_but_required_to_satisfy_c_compilers;
#endif /* SVN_HAVE_CRYPTO */
};



/*** Helper Functions ***/
#ifdef SVN_HAVE_CRYPTO


/* One-time initialization of the cryptography subsystem. */
static volatile svn_atomic_t crypto_init_state = 0;


#define CRYPTO_INIT(scratch_pool) \
  SVN_ERR(svn_atomic__init_once(&crypto_init_state, \
                                crypto_init, NULL, (scratch_pool)))


/* Initialize the APR cryptography subsystem (if available), using
   ANY_POOL's ancestor root pool for the registration of cleanups,
   shutdowns, etc.   */
/* Don't call this function directly!  Use svn_atomic__init_once(). */
static svn_error_t *
crypto_init(void *baton, apr_pool_t *any_pool)
{
  /* NOTE: this function will locate the topmost ancestor of ANY_POOL
     for its cleanup handlers. We don't have to worry about ANY_POOL
     being cleared.  */
  apr_status_t apr_err = apr_crypto_init(any_pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err,
                              _("Failed to initialize cryptography "
                                "subsystem"));

  return SVN_NO_ERROR;
}


/* If APU_ERR is non-NULL, create and return a Subversion error using
   APR_ERR and APU_ERR. */
static svn_error_t *
err_from_apu_err(apr_status_t apr_err,
                 const apu_err_t *apu_err)
{
  if (apu_err)
    return svn_error_createf(apr_err, NULL,
                             _("code (%d), reason (\"%s\"), msg (\"%s\")"),
                             apu_err->rc,
                             apu_err->reason ? apu_err->reason : "",
                             apu_err->msg ? apu_err->msg : "");
  return SVN_NO_ERROR;
}


/* Generate a Subversion error which describes the state reflected by
   APR_ERR and any crypto errors registered with CTX. */
static svn_error_t *
crypto_error_create(svn_crypto__ctx_t *ctx,
                    apr_status_t apr_err,
                    const char *msg)
{
  const apu_err_t *apu_err;
  apr_status_t rv = apr_crypto_error(&apu_err, ctx->crypto);
  svn_error_t *child;

  /* Ugh. The APIs are a bit slippery, so be wary.  */
  if (apr_err == APR_SUCCESS)
    apr_err = APR_EGENERAL;

  if (rv == APR_SUCCESS)
    child = err_from_apu_err(apr_err, apu_err);
  else
    child = svn_error_wrap_apr(rv, _("Fetching error from APR"));

  return svn_error_create(apr_err, child, msg);
}


/* Set RAND_BYTES to a block of bytes containing random data RAND_LEN
   long and allocated from RESULT_POOL. */
static svn_error_t *
get_random_bytes(const unsigned char **rand_bytes,
                 svn_crypto__ctx_t *ctx,
                 apr_size_t rand_len,
                 apr_pool_t *result_pool)
{
  apr_status_t apr_err;
  unsigned char *bytes;

  bytes = apr_palloc(result_pool, rand_len);
  apr_err = apr_generate_random_bytes(bytes, rand_len);
  if (apr_err != APR_SUCCESS)
    return svn_error_wrap_apr(apr_err, _("Error obtaining random data"));

  *rand_bytes = bytes;
  return SVN_NO_ERROR;
}


/* Return an svn_string_t allocated from RESULT_POOL, with its .data
   and .len members set to DATA and LEN, respective.

   WARNING: No lifetime management of DATA is offered here, so you
   probably want to ensure that that information is allocated in a
   sufficiently long-lived pool (such as, for example, RESULT_POOL). */
static const svn_string_t *
wrap_as_string(const unsigned char *data,
               apr_size_t len,
               apr_pool_t *result_pool)
{
  svn_string_t *s = apr_palloc(result_pool, sizeof(*s));

  s->data = (const char *)data;  /* better already be in RESULT_POOL  */
  s->len = len;
  return s;
}


#endif /* SVN_HAVE_CRYPTO */



/*** Semi-public APIs ***/

/* Return TRUE iff Subversion's cryptographic support is available. */
svn_boolean_t svn_crypto__is_available(void)
{
#ifdef SVN_HAVE_CRYPTO
  return TRUE;
#else /* SVN_HAVE_CRYPTO */
  return FALSE;
#endif /* SVN_HAVE_CRYPTO */
}


/* Set CTX to a Subversion cryptography context allocated from
   RESULT_POOL.  */
svn_error_t *
svn_crypto__context_create(svn_crypto__ctx_t **ctx,
                           apr_pool_t *result_pool)
{
#ifdef SVN_HAVE_CRYPTO
  apr_status_t apr_err;
  const apu_err_t *apu_err = NULL;
  apr_crypto_t *apr_crypto;
  const apr_crypto_driver_t *driver;

  CRYPTO_INIT(result_pool);

  /* Load the crypto driver.

     ### TODO: For the sake of flexibility, should we use
     ### APU_CRYPTO_RECOMMENDED_DRIVER instead of hard coding
     ### "openssl" here?

     NOTE: Potential bugs in get_driver() imply we might get
     APR_SUCCESS and NULL.  Sigh. Just be a little more careful in
     error generation here.  */
  apr_err = apr_crypto_get_driver(&driver, "openssl", NULL, &apu_err,
                                  result_pool);
  if (apr_err != APR_SUCCESS)
    return svn_error_create(apr_err, err_from_apu_err(apr_err, apu_err),
                            _("OpenSSL crypto driver error"));
  if (driver == NULL)
    return svn_error_create(APR_EGENERAL,
                            err_from_apu_err(APR_EGENERAL, apu_err),
                            _("Bad return value while loading crypto "
                              "driver"));

  apr_err = apr_crypto_make(&apr_crypto, driver, NULL, result_pool);
  if (apr_err != APR_SUCCESS || apr_crypto == NULL)
    return svn_error_create(apr_err, NULL,
                            _("Error creating OpenSSL crypto context"));

  /* Allocate and initialize our crypto context. */
  *ctx = apr_palloc(result_pool, sizeof(**ctx));
  (*ctx)->crypto = apr_crypto;

  return SVN_NO_ERROR;
#else /* SVN_HAVE_CRYPTO */
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                          "Cryptographic support is not available");
#endif /* SVN_HAVE_CRYPTO */
}


svn_error_t *
svn_crypto__encrypt_password(const svn_string_t **ciphertext,
                             const svn_string_t **iv,
                             const svn_string_t **salt,
                             svn_crypto__ctx_t *ctx,
                             const char *password,
                             const svn_string_t *master,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
#ifdef SVN_HAVE_CRYPTO
  svn_error_t *err = SVN_NO_ERROR;
  const unsigned char *salt_vector;
  const unsigned char *iv_vector;
  apr_size_t iv_len;
  apr_crypto_key_t *key = NULL;
  apr_status_t apr_err;
  const unsigned char *prefix;
  apr_crypto_block_t *block_ctx = NULL;
  apr_size_t block_size;
  unsigned char *assembled;
  apr_size_t password_len, assembled_len = 0;
  apr_size_t result_len;
  unsigned char *result;
  apr_size_t ignored_result_len = 0;

  SVN_ERR_ASSERT(ctx != NULL);

  /* Generate the salt. */
#define SALT_LEN 8
  SVN_ERR(get_random_bytes(&salt_vector, ctx, SALT_LEN, result_pool));

  /* Initialize the passphrase.  */
  apr_err = apr_crypto_passphrase(&key, &iv_len,
                                  master->data, master->len,
                                  salt_vector, SALT_LEN,
                                  APR_KEY_AES_256, APR_MODE_CBC,
                                  FALSE /* doPad */, NUM_ITERATIONS,
                                  ctx->crypto,
                                  scratch_pool);
  if (apr_err != APR_SUCCESS)
    return svn_error_trace(crypto_error_create(
                               ctx, apr_err,
                               _("Error creating derived key")));
  if (! key)
    return svn_error_create(APR_EGENERAL, NULL,
                            _("Error creating derived key"));
  if (iv_len == 0)
    return svn_error_create(APR_EGENERAL, NULL,
                            _("Unexpected IV length returned"));

  /* Generate the proper length IV.  */
  SVN_ERR(get_random_bytes(&iv_vector, ctx, iv_len, result_pool));

  /* Initialize block encryption. */
  apr_err = apr_crypto_block_encrypt_init(&block_ctx, &iv_vector, key,
                                          &block_size, scratch_pool);
  if ((apr_err != APR_SUCCESS) || (! block_ctx))
    return svn_error_trace(crypto_error_create(
                             ctx, apr_err,
                             _("Error initializing block encryption")));

  /* Generate a 4-byte prefix. */
  SVN_ERR(get_random_bytes(&prefix, ctx, RANDOM_PREFIX_LEN, scratch_pool));

  /* Combine our prefix, original password, and appropriate padding.
     We won't bother padding if the prefix and password combined
     perfectly align on the block boundary.  If they don't,
     however, we'll drop a NUL byte after the password and pad with
     random stuff after that to the block boundary. */
  password_len = strlen(password);
  assembled_len = RANDOM_PREFIX_LEN + password_len;
  if ((assembled_len % block_size) == 0)
    {
      assembled = apr_palloc(scratch_pool, assembled_len);
      memcpy(assembled, prefix, RANDOM_PREFIX_LEN);
      memcpy(assembled + RANDOM_PREFIX_LEN, password, password_len);
    }
  else
    {
      const unsigned char *padding;
      apr_size_t pad_len = block_size - (assembled_len % block_size) - 1;

      SVN_ERR(get_random_bytes(&padding, ctx, pad_len, scratch_pool));
      assembled_len = assembled_len + 1 + pad_len;
      assembled = apr_palloc(scratch_pool, assembled_len);
      memcpy(assembled, prefix, RANDOM_PREFIX_LEN);
      memcpy(assembled + RANDOM_PREFIX_LEN, password, password_len);
      *(assembled + RANDOM_PREFIX_LEN + password_len) = '\0';
      memcpy(assembled + RANDOM_PREFIX_LEN + password_len + 1,
             padding, pad_len);
    }

  /* Get the length that we need to allocate.  */
  apr_err = apr_crypto_block_encrypt(NULL, &result_len, assembled,
                                     assembled_len, block_ctx);
  if (apr_err != APR_SUCCESS)
    {
      err = crypto_error_create(ctx, apr_err,
                                _("Error fetching result length"));
      goto cleanup;
    }

  /* Allocate our result buffer.  */
  result = apr_palloc(result_pool, result_len);

  /* Encrypt the block. */
  apr_err = apr_crypto_block_encrypt(&result, &result_len, assembled,
                                     assembled_len, block_ctx);
  if (apr_err != APR_SUCCESS)
    {
      err = crypto_error_create(ctx, apr_err,
                                _("Error during block encryption"));
      goto cleanup;
    }

  /* Finalize the block encryption. Since we padded everything, this should
     not produce any more encrypted output.  */
  apr_err = apr_crypto_block_encrypt_finish(NULL,
                                            &ignored_result_len,
                                            block_ctx);
  if (apr_err != APR_SUCCESS)
    {
      err = crypto_error_create(ctx, apr_err,
                                _("Error finalizing block encryption"));
      goto cleanup;
    }

  *ciphertext = wrap_as_string(result, result_len, result_pool);
  *iv = wrap_as_string(iv_vector, iv_len, result_pool);
  *salt = wrap_as_string(salt_vector, SALT_LEN, result_pool);

 cleanup:
  apr_crypto_block_cleanup(block_ctx);
  return err;
#else /* SVN_HAVE_CRYPTO */
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                          "Cryptographic support is not available");
#endif /* SVN_HAVE_CRYPTO */
}


svn_error_t *
svn_crypto__decrypt_password(const char **plaintext,
                             svn_crypto__ctx_t *ctx,
                             const svn_string_t *ciphertext,
                             const svn_string_t *iv,
                             const svn_string_t *salt,
                             const svn_string_t *master,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
#ifdef SVN_HAVE_CRYPTO
  svn_error_t *err = SVN_NO_ERROR;
  apr_status_t apr_err;
  apr_crypto_block_t *block_ctx = NULL;
  apr_size_t block_size, iv_len;
  apr_crypto_key_t *key = NULL;
  unsigned char *result;
  apr_size_t result_len = 0, final_len = 0;

  /* Initialize the passphrase.  */
  apr_err = apr_crypto_passphrase(&key, &iv_len,
                                  master->data, master->len,
                                  (unsigned char *)salt->data, salt->len,
                                  APR_KEY_AES_256, APR_MODE_CBC,
                                  FALSE /* doPad */, NUM_ITERATIONS,
                                  ctx->crypto, scratch_pool);
  if (apr_err != APR_SUCCESS)
    return svn_error_trace(crypto_error_create(
                               ctx, apr_err,
                               _("Error creating derived key")));
  if (! key)
    return svn_error_create(APR_EGENERAL, NULL,
                            _("Error creating derived key"));
  if (iv_len == 0)
    return svn_error_create(APR_EGENERAL, NULL,
                            _("Unexpected IV length returned"));
  if (iv_len != iv->len)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("Provided IV has incorrect length"));

  apr_err = apr_crypto_block_decrypt_init(&block_ctx, &block_size,
                                          (unsigned char *)iv->data,
                                          key, scratch_pool);
  if ((apr_err != APR_SUCCESS) || (! block_ctx))
    return svn_error_trace(crypto_error_create(
                             ctx, apr_err,
                             _("Error initializing block decryption")));

  apr_err = apr_crypto_block_decrypt(NULL, &result_len,
                                     (unsigned char *)ciphertext->data,
                                     ciphertext->len, block_ctx);
  if (apr_err != APR_SUCCESS)
    {
      err = crypto_error_create(ctx, apr_err,
                                _("Error fetching result length"));
      goto cleanup;
    }

  result = apr_palloc(scratch_pool, result_len);
  apr_err = apr_crypto_block_decrypt(&result, &result_len,
                                     (unsigned char *)ciphertext->data,
                                     ciphertext->len, block_ctx);
  if (apr_err != APR_SUCCESS)
    {
      err = crypto_error_create(ctx, apr_err,
                                _("Error during block decryption"));
      goto cleanup;
    }

  apr_err = apr_crypto_block_decrypt_finish(result + result_len, &final_len,
                                            block_ctx);
  if (apr_err != APR_SUCCESS)
    {
      err = crypto_error_create(ctx, apr_err,
                                _("Error finalizing block decryption"));
      goto cleanup;
    }

  /* Copy the non-random bits of the resulting plaintext, skipping the
     prefix and ignoring any trailing padding. */
  *plaintext = apr_pstrndup(result_pool,
                            (const char *)(result + RANDOM_PREFIX_LEN),
                            result_len + final_len - RANDOM_PREFIX_LEN);

 cleanup:
  apr_crypto_block_cleanup(block_ctx);
  return err;
#else /* SVN_HAVE_CRYPTO */
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                          "Cryptographic support is not available");
#endif /* SVN_HAVE_CRYPTO */
}


svn_error_t *
svn_crypto__generate_secret_checktext(const svn_string_t **ciphertext,
                                      const svn_string_t **iv,
                                      const svn_string_t **salt,
                                      const char **checktext,
                                      svn_crypto__ctx_t *ctx,
                                      const svn_string_t *master,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool)
{
#ifdef SVN_HAVE_CRYPTO
  svn_error_t *err = SVN_NO_ERROR;
  const unsigned char *salt_vector;
  const unsigned char *iv_vector;
  const unsigned char *stuff_vector;
  apr_size_t iv_len;
  apr_crypto_key_t *key = NULL;
  apr_status_t apr_err;
  apr_crypto_block_t *block_ctx = NULL;
  apr_size_t block_size;
  apr_size_t result_len;
  unsigned char *result;
  apr_size_t ignored_result_len = 0;
  apr_size_t stuff_len;
  svn_checksum_t *stuff_sum;

  SVN_ERR_ASSERT(ctx != NULL);

  /* Generate the salt. */
  SVN_ERR(get_random_bytes(&salt_vector, ctx, SALT_LEN, result_pool));

  /* Initialize the passphrase.  */
  apr_err = apr_crypto_passphrase(&key, &iv_len,
                                  master->data, master->len,
                                  salt_vector, SALT_LEN,
                                  APR_KEY_AES_256, APR_MODE_CBC,
                                  FALSE /* doPad */, NUM_ITERATIONS,
                                  ctx->crypto,
                                  scratch_pool);
  if (apr_err != APR_SUCCESS)
    return svn_error_trace(crypto_error_create(
                               ctx, apr_err,
                               _("Error creating derived key")));
  if (! key)
    return svn_error_create(APR_EGENERAL, NULL,
                            _("Error creating derived key"));
  if (iv_len == 0)
    return svn_error_create(APR_EGENERAL, NULL,
                            _("Unexpected IV length returned"));

  /* Generate the proper length IV.  */
  SVN_ERR(get_random_bytes(&iv_vector, ctx, iv_len, result_pool));

  /* Initialize block encryption. */
  apr_err = apr_crypto_block_encrypt_init(&block_ctx, &iv_vector, key,
                                          &block_size, scratch_pool);
  if ((apr_err != APR_SUCCESS) || (! block_ctx))
    return svn_error_trace(crypto_error_create(
                             ctx, apr_err,
                             _("Error initializing block encryption")));

  /* Generate a blob of random data, block-aligned per the
     requirements of the encryption algorithm, but with a minimum size
     of our choosing.  */
#define MIN_STUFF_LEN 32
  if (MIN_STUFF_LEN % block_size)
    stuff_len = MIN_STUFF_LEN + (block_size - (MIN_STUFF_LEN % block_size));
  else
    stuff_len = MIN_STUFF_LEN;
  SVN_ERR(get_random_bytes(&stuff_vector, ctx, stuff_len, scratch_pool));

  /* ### FIXME:  This should be a SHA-256.  */
  SVN_ERR(svn_checksum(&stuff_sum, svn_checksum_sha1, stuff_vector,
                       stuff_len, scratch_pool));

  /* Get the length that we need to allocate.  */
  apr_err = apr_crypto_block_encrypt(NULL, &result_len, stuff_vector,
                                     stuff_len, block_ctx);
  if (apr_err != APR_SUCCESS)
    {
      err = crypto_error_create(ctx, apr_err,
                                _("Error fetching result length"));
      goto cleanup;
    }

  /* Allocate our result buffer.  */
  result = apr_palloc(result_pool, result_len);

  /* Encrypt the block. */
  apr_err = apr_crypto_block_encrypt(&result, &result_len, stuff_vector,
                                     stuff_len, block_ctx);
  if (apr_err != APR_SUCCESS)
    {
      err = crypto_error_create(ctx, apr_err,
                                _("Error during block encryption"));
      goto cleanup;
    }

  /* Finalize the block encryption. Since we padded everything, this should
     not produce any more encrypted output.  */
  apr_err = apr_crypto_block_encrypt_finish(NULL,
                                            &ignored_result_len,
                                            block_ctx);
  if (apr_err != APR_SUCCESS)
    {
      err = crypto_error_create(ctx, apr_err,
                                _("Error finalizing block encryption"));
      goto cleanup;
    }

  *ciphertext = wrap_as_string(result, result_len, result_pool);
  *iv = wrap_as_string(iv_vector, iv_len, result_pool);
  *salt = wrap_as_string(salt_vector, SALT_LEN, result_pool);
  *checktext = svn_checksum_to_cstring(stuff_sum, result_pool);

 cleanup:
  apr_crypto_block_cleanup(block_ctx);
  return err;
#else /* SVN_HAVE_CRYPTO */
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                          "Cryptographic support is not available");
#endif /* SVN_HAVE_CRYPTO */
}


svn_error_t *
svn_crypto__verify_secret(svn_boolean_t *is_valid,
                          svn_crypto__ctx_t *ctx,
                          const svn_string_t *master,
                          const svn_string_t *ciphertext,
                          const svn_string_t *iv,
                          const svn_string_t *salt,
                          const char *checktext,
                          apr_pool_t *scratch_pool)
{
#ifdef SVN_HAVE_CRYPTO
  svn_error_t *err = SVN_NO_ERROR;
  apr_status_t apr_err;
  apr_crypto_block_t *block_ctx = NULL;
  apr_size_t block_size, iv_len;
  apr_crypto_key_t *key = NULL;
  unsigned char *result;
  apr_size_t result_len = 0, final_len = 0;
  svn_checksum_t *result_sum;

  *is_valid = FALSE;

  /* Initialize the passphrase.  */
  apr_err = apr_crypto_passphrase(&key, &iv_len,
                                  master->data, master->len,
                                  (unsigned char *)salt->data, salt->len,
                                  APR_KEY_AES_256, APR_MODE_CBC,
                                  FALSE /* doPad */, NUM_ITERATIONS,
                                  ctx->crypto, scratch_pool);
  if (apr_err != APR_SUCCESS)
    return svn_error_trace(crypto_error_create(
                               ctx, apr_err,
                               _("Error creating derived key")));
  if (! key)
    return svn_error_create(APR_EGENERAL, NULL,
                            _("Error creating derived key"));
  if (iv_len == 0)
    return svn_error_create(APR_EGENERAL, NULL,
                            _("Unexpected IV length returned"));
  if (iv_len != iv->len)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("Provided IV has incorrect length"));

  apr_err = apr_crypto_block_decrypt_init(&block_ctx, &block_size,
                                          (unsigned char *)iv->data,
                                          key, scratch_pool);
  if ((apr_err != APR_SUCCESS) || (! block_ctx))
    return svn_error_trace(crypto_error_create(
                             ctx, apr_err,
                             _("Error initializing block decryption")));

  apr_err = apr_crypto_block_decrypt(NULL, &result_len,
                                     (unsigned char *)ciphertext->data,
                                     ciphertext->len, block_ctx);
  if (apr_err != APR_SUCCESS)
    {
      err = crypto_error_create(ctx, apr_err,
                                _("Error fetching result length"));
      goto cleanup;
    }

  result = apr_palloc(scratch_pool, result_len);
  apr_err = apr_crypto_block_decrypt(&result, &result_len,
                                     (unsigned char *)ciphertext->data,
                                     ciphertext->len, block_ctx);
  if (apr_err != APR_SUCCESS)
    {
      err = crypto_error_create(ctx, apr_err,
                                _("Error during block decryption"));
      goto cleanup;
    }

  apr_err = apr_crypto_block_decrypt_finish(result + result_len, &final_len,
                                            block_ctx);
  if (apr_err != APR_SUCCESS)
    {
      err = crypto_error_create(ctx, apr_err,
                                _("Error finalizing block decryption"));
      goto cleanup;
    }

  /* ### FIXME:  This should be a SHA-256.  */
  SVN_ERR(svn_checksum(&result_sum, svn_checksum_sha1, result,
                       result_len + final_len, scratch_pool));

  *is_valid = strcmp(checktext,
                     svn_checksum_to_cstring(result_sum, scratch_pool)) == 0;

 cleanup:
  apr_crypto_block_cleanup(block_ctx);
  return err;
#else /* SVN_HAVE_CRYPTO */
  *is_valid = FALSE;
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                          "Cryptographic support is not available");
#endif /* SVN_HAVE_CRYPTO */
}
