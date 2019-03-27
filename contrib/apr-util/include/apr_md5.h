/*
 * This is work is derived from material Copyright RSA Data Security, Inc.
 *
 * The RSA copyright statement and Licence for that original material is
 * included below. This is followed by the Apache copyright statement and
 * licence for the modifications made to that material.
 */

/* Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
   rights reserved.

   License to copy and use this software is granted provided that it
   is identified as the "RSA Data Security, Inc. MD5 Message-Digest
   Algorithm" in all material mentioning or referencing this software
   or this function.

   License is also granted to make and use derivative works provided
   that such works are identified as "derived from the RSA Data
   Security, Inc. MD5 Message-Digest Algorithm" in all material
   mentioning or referencing the derived work.

   RSA Data Security, Inc. makes no representations concerning either
   the merchantability of this software or the suitability of this
   software for any particular purpose. It is provided "as is"
   without express or implied warranty of any kind.

   These notices must be retained in any copies of any part of this
   documentation and/or software.
 */

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

#ifndef APR_MD5_H
#define APR_MD5_H

#include "apu.h"
#include "apr_xlate.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @file apr_md5.h
 * @brief APR MD5 Routines
 */

/**
 * @defgroup APR_MD5 MD5 Routines
 * @ingroup APR
 * @{
 */

/** The MD5 digest size */
#define APR_MD5_DIGESTSIZE 16

/** @see apr_md5_ctx_t */
typedef struct apr_md5_ctx_t apr_md5_ctx_t;

/** MD5 context. */
struct apr_md5_ctx_t {
    /** state (ABCD) */
    apr_uint32_t state[4];
    /** number of bits, modulo 2^64 (lsb first) */
    apr_uint32_t count[2];
    /** input buffer */
    unsigned char buffer[64];
    /** translation handle 
     *  ignored if xlate is unsupported
     */
    apr_xlate_t *xlate;
};

/**
 * MD5 Initialize.  Begins an MD5 operation, writing a new context.
 * @param context The MD5 context to initialize.
 */
APU_DECLARE(apr_status_t) apr_md5_init(apr_md5_ctx_t *context);

/**
 * MD5 translation setup.  Provides the APR translation handle to be used 
 * for translating the content before calculating the digest.
 * @param context The MD5 content to set the translation for.
 * @param xlate The translation handle to use for this MD5 context 
 */
APU_DECLARE(apr_status_t) apr_md5_set_xlate(apr_md5_ctx_t *context,
                                            apr_xlate_t *xlate);

/**
 * MD5 block update operation.  Continue an MD5 message-digest operation, 
 * processing another message block, and updating the context.
 * @param context The MD5 content to update.
 * @param input next message block to update
 * @param inputLen The length of the next message block
 */
APU_DECLARE(apr_status_t) apr_md5_update(apr_md5_ctx_t *context,
                                         const void *input,
                                         apr_size_t inputLen);

/**
 * MD5 finalization.  Ends an MD5 message-digest operation, writing the 
 * message digest and zeroing the context
 * @param digest The final MD5 digest
 * @param context The MD5 content we are finalizing.
 */
APU_DECLARE(apr_status_t) apr_md5_final(unsigned char digest[APR_MD5_DIGESTSIZE],
                                        apr_md5_ctx_t *context);

/**
 * MD5 in one step
 * @param digest The final MD5 digest
 * @param input The message block to use
 * @param inputLen The length of the message block
 */
APU_DECLARE(apr_status_t) apr_md5(unsigned char digest[APR_MD5_DIGESTSIZE],
                                  const void *input,
                                  apr_size_t inputLen);

/**
 * Encode a password using an MD5 algorithm
 * @param password The password to encode
 * @param salt The salt string to use for the encoding
 * @param result The string to store the encoded password in
 * @param nbytes The size of the result buffer
 */
APU_DECLARE(apr_status_t) apr_md5_encode(const char *password, const char *salt,
                                         char *result, apr_size_t nbytes);

/**
 * Encode a password using the bcrypt algorithm
 * @param password The password to encode
 * @param count The cost of the encoding, possible values are 4 to 31
 * @param salt Pointer to binary data to be used as salt for the encoding
 * @param salt_len The size of the salt data (must be >= 16)
 * @param out The string to store the encoded password in
 * @param out_len The size of the result buffer (must be >= 61)
 */
APU_DECLARE(apr_status_t) apr_bcrypt_encode(const char *pw,
                                            unsigned int count,
                                            const unsigned char *salt,
                                            apr_size_t salt_len,
                                            char *out, apr_size_t out_len);

/**
 * Validate hashes created by APR-supported algorithms: md5, bcrypt, and sha1.
 * hashes created by crypt are supported only on platforms that provide
 * crypt(3), so don't rely on that function unless you know that your
 * application will be run only on platforms that support it.  On platforms
 * that don't support crypt(3), this falls back to a clear text string
 * comparison.
 * @param passwd The password to validate
 * @param hash The password to validate against
 */
APU_DECLARE(apr_status_t) apr_password_validate(const char *passwd, 
                                                const char *hash);


/** @} */
#ifdef __cplusplus
}
#endif

#endif /* !APR_MD5_H */
