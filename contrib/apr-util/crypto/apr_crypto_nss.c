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

#include "apr_lib.h"
#include "apu.h"
#include "apu_config.h"
#include "apu_errno.h"

#include <ctype.h>
#include <stdlib.h>

#include "apr_strings.h"
#include "apr_time.h"
#include "apr_buckets.h"

#include "apr_crypto_internal.h"

#if APU_HAVE_CRYPTO

#include <prerror.h>

#ifdef HAVE_NSS_NSS_H
#include <nss/nss.h>
#endif
#ifdef HAVE_NSS_H
#include <nss.h>
#endif

#ifdef HAVE_NSS_PK11PUB_H
#include <nss/pk11pub.h>
#endif
#ifdef HAVE_PK11PUB_H
#include <pk11pub.h>
#endif

struct apr_crypto_t {
    apr_pool_t *pool;
    const apr_crypto_driver_t *provider;
    apu_err_t *result;
    apr_array_header_t *keys;
    apr_crypto_config_t *config;
    apr_hash_t *types;
    apr_hash_t *modes;
};

struct apr_crypto_config_t {
       void *opaque;
};

struct apr_crypto_key_t {
    apr_pool_t *pool;
    const apr_crypto_driver_t *provider;
    const apr_crypto_t *f;
    CK_MECHANISM_TYPE cipherMech;
    SECOidTag cipherOid;
    PK11SymKey *symKey;
    int ivSize;
};

struct apr_crypto_block_t {
    apr_pool_t *pool;
    const apr_crypto_driver_t *provider;
    const apr_crypto_t *f;
    PK11Context *ctx;
    apr_crypto_key_t *key;
    int blockSize;
};

static int key_3des_192 = APR_KEY_3DES_192;
static int key_aes_128 = APR_KEY_AES_128;
static int key_aes_192 = APR_KEY_AES_192;
static int key_aes_256 = APR_KEY_AES_256;

static int mode_ecb = APR_MODE_ECB;
static int mode_cbc = APR_MODE_CBC;

/**
 * Fetch the most recent error from this driver.
 */
static apr_status_t crypto_error(const apu_err_t **result,
        const apr_crypto_t *f)
{
    *result = f->result;
    return APR_SUCCESS;
}

/**
 * Shutdown the crypto library and release resources.
 *
 * It is safe to shut down twice.
 */
static apr_status_t crypto_shutdown(void)
{
    if (NSS_IsInitialized()) {
        SECStatus s = NSS_Shutdown();
        if (s != SECSuccess) {
            return APR_EINIT;
        }
    }
    return APR_SUCCESS;
}

static apr_status_t crypto_shutdown_helper(void *data)
{
    return crypto_shutdown();
}

/**
 * Initialise the crypto library and perform one time initialisation.
 */
static apr_status_t crypto_init(apr_pool_t *pool, const char *params,
        const apu_err_t **result)
{
    SECStatus s;
    const char *dir = NULL;
    const char *keyPrefix = NULL;
    const char *certPrefix = NULL;
    const char *secmod = NULL;
    int noinit = 0;
    PRUint32 flags = 0;

    struct {
        const char *field;
        const char *value;
        int set;
    } fields[] = {
        { "dir", NULL, 0 },
        { "key3", NULL, 0 },
        { "cert7", NULL, 0 },
        { "secmod", NULL, 0 },
        { "noinit", NULL, 0 },
        { NULL, NULL, 0 }
    };
    const char *ptr;
    size_t klen;
    char **elts = NULL;
    char *elt;
    int i = 0, j;
    apr_status_t status;

    if (params) {
        if (APR_SUCCESS != (status = apr_tokenize_to_argv(params, &elts, pool))) {
            return status;
        }
        while ((elt = elts[i])) {
            ptr = strchr(elt, '=');
            if (ptr) {
                for (klen = ptr - elt; klen && apr_isspace(elt[klen - 1]); --klen)
                    ;
                ptr++;
            }
            else {
                for (klen = strlen(elt); klen && apr_isspace(elt[klen - 1]); --klen)
                    ;
            }
            elt[klen] = 0;

            for (j = 0; fields[j].field != NULL; ++j) {
                if (klen && !strcasecmp(fields[j].field, elt)) {
                    fields[j].set = 1;
                    if (ptr) {
                        fields[j].value = ptr;
                    }
                    break;
                }
            }

            i++;
        }
        dir = fields[0].value;
        keyPrefix = fields[1].value;
        certPrefix = fields[2].value;
        secmod = fields[3].value;
        noinit = fields[4].set;
    }

    /* if we've been asked to bypass, do so here */
    if (noinit) {
        return APR_SUCCESS;
    }

    /* sanity check - we can only initialise NSS once */
    if (NSS_IsInitialized()) {
        return APR_EREINIT;
    }

    apr_pool_cleanup_register(pool, pool, crypto_shutdown_helper,
            apr_pool_cleanup_null);

    if (keyPrefix || certPrefix || secmod) {
        s = NSS_Initialize(dir, certPrefix, keyPrefix, secmod, flags);
    }
    else if (dir) {
        s = NSS_InitReadWrite(dir);
    }
    else {
        s = NSS_NoDB_Init(NULL);
    }
    if (s != SECSuccess) {
        if (result) {
            apu_err_t *err = apr_pcalloc(pool, sizeof(apu_err_t));
            err->rc = PR_GetError();
            err->msg = PR_ErrorToName(s);
            err->reason = "Error during 'nss' initialisation";
            *result = err;
        }
        return APR_ECRYPT;
    }

    return APR_SUCCESS;

}

/**
 * @brief Clean encryption / decryption context.
 * @note After cleanup, a context is free to be reused if necessary.
 * @param f The context to use.
 * @return Returns APR_ENOTIMPL if not supported.
 */
static apr_status_t crypto_block_cleanup(apr_crypto_block_t *block)
{

    if (block->ctx) {
        PK11_DestroyContext(block->ctx, PR_TRUE);
        block->ctx = NULL;
    }

    return APR_SUCCESS;

}

static apr_status_t crypto_block_cleanup_helper(void *data)
{
    apr_crypto_block_t *block = (apr_crypto_block_t *) data;
    return crypto_block_cleanup(block);
}

/**
 * @brief Clean encryption / decryption context.
 * @note After cleanup, a context is free to be reused if necessary.
 * @param f The context to use.
 * @return Returns APR_ENOTIMPL if not supported.
 */
static apr_status_t crypto_cleanup(apr_crypto_t *f)
{
    apr_crypto_key_t *key;
    if (f->keys) {
        while ((key = apr_array_pop(f->keys))) {
            if (key->symKey) {
                PK11_FreeSymKey(key->symKey);
                key->symKey = NULL;
            }
        }
    }
    return APR_SUCCESS;
}

static apr_status_t crypto_cleanup_helper(void *data)
{
    apr_crypto_t *f = (apr_crypto_t *) data;
    return crypto_cleanup(f);
}

/**
 * @brief Create a context for supporting encryption. Keys, certificates,
 *        algorithms and other parameters will be set per context. More than
 *        one context can be created at one time. A cleanup will be automatically
 *        registered with the given pool to guarantee a graceful shutdown.
 * @param f - context pointer will be written here
 * @param provider - provider to use
 * @param params - parameter string
 * @param pool - process pool
 * @return APR_ENOENGINE when the engine specified does not exist. APR_EINITENGINE
 * if the engine cannot be initialised.
 */
static apr_status_t crypto_make(apr_crypto_t **ff,
        const apr_crypto_driver_t *provider, const char *params,
        apr_pool_t *pool)
{
    apr_crypto_config_t *config = NULL;
    apr_crypto_t *f;

    f = apr_pcalloc(pool, sizeof(apr_crypto_t));
    if (!f) {
        return APR_ENOMEM;
    }
    *ff = f;
    f->pool = pool;
    f->provider = provider;
    config = f->config = apr_pcalloc(pool, sizeof(apr_crypto_config_t));
    if (!config) {
        return APR_ENOMEM;
    }
    f->result = apr_pcalloc(pool, sizeof(apu_err_t));
    if (!f->result) {
        return APR_ENOMEM;
    }
    f->keys = apr_array_make(pool, 10, sizeof(apr_crypto_key_t));

    f->types = apr_hash_make(pool);
    if (!f->types) {
        return APR_ENOMEM;
    }
    apr_hash_set(f->types, "3des192", APR_HASH_KEY_STRING, &(key_3des_192));
    apr_hash_set(f->types, "aes128", APR_HASH_KEY_STRING, &(key_aes_128));
    apr_hash_set(f->types, "aes192", APR_HASH_KEY_STRING, &(key_aes_192));
    apr_hash_set(f->types, "aes256", APR_HASH_KEY_STRING, &(key_aes_256));

    f->modes = apr_hash_make(pool);
    if (!f->modes) {
        return APR_ENOMEM;
    }
    apr_hash_set(f->modes, "ecb", APR_HASH_KEY_STRING, &(mode_ecb));
    apr_hash_set(f->modes, "cbc", APR_HASH_KEY_STRING, &(mode_cbc));

    apr_pool_cleanup_register(pool, f, crypto_cleanup_helper,
            apr_pool_cleanup_null);

    return APR_SUCCESS;

}

/**
 * @brief Get a hash table of key types, keyed by the name of the type against
 * an integer pointer constant.
 *
 * @param types - hashtable of key types keyed to constants.
 * @param f - encryption context
 * @return APR_SUCCESS for success
 */
static apr_status_t crypto_get_block_key_types(apr_hash_t **types,
        const apr_crypto_t *f)
{
    *types = f->types;
    return APR_SUCCESS;
}

/**
 * @brief Get a hash table of key modes, keyed by the name of the mode against
 * an integer pointer constant.
 *
 * @param modes - hashtable of key modes keyed to constants.
 * @param f - encryption context
 * @return APR_SUCCESS for success
 */
static apr_status_t crypto_get_block_key_modes(apr_hash_t **modes,
        const apr_crypto_t *f)
{
    *modes = f->modes;
    return APR_SUCCESS;
}

/**
 * @brief Create a key from the given passphrase. By default, the PBKDF2
 *        algorithm is used to generate the key from the passphrase. It is expected
 *        that the same pass phrase will generate the same key, regardless of the
 *        backend crypto platform used. The key is cleaned up when the context
 *        is cleaned, and may be reused with multiple encryption or decryption
 *        operations.
 * @note If *key is NULL, a apr_crypto_key_t will be created from a pool. If
 *       *key is not NULL, *key must point at a previously created structure.
 * @param key The key returned, see note.
 * @param ivSize The size of the initialisation vector will be returned, based
 *               on whether an IV is relevant for this type of crypto.
 * @param pass The passphrase to use.
 * @param passLen The passphrase length in bytes
 * @param salt The salt to use.
 * @param saltLen The salt length in bytes
 * @param type 3DES_192, AES_128, AES_192, AES_256.
 * @param mode Electronic Code Book / Cipher Block Chaining.
 * @param doPad Pad if necessary.
 * @param iterations Iteration count
 * @param f The context to use.
 * @param p The pool to use.
 * @return Returns APR_ENOKEY if the pass phrase is missing or empty, or if a backend
 *         error occurred while generating the key. APR_ENOCIPHER if the type or mode
 *         is not supported by the particular backend. APR_EKEYTYPE if the key type is
 *         not known. APR_EPADDING if padding was requested but is not supported.
 *         APR_ENOTIMPL if not implemented.
 */
static apr_status_t crypto_passphrase(apr_crypto_key_t **k, apr_size_t *ivSize,
        const char *pass, apr_size_t passLen, const unsigned char * salt,
        apr_size_t saltLen, const apr_crypto_block_key_type_e type,
        const apr_crypto_block_key_mode_e mode, const int doPad,
        const int iterations, const apr_crypto_t *f, apr_pool_t *p)
{
    apr_status_t rv = APR_SUCCESS;
    PK11SlotInfo * slot;
    SECItem passItem;
    SECItem saltItem;
    SECAlgorithmID *algid;
    void *wincx = NULL; /* what is wincx? */
    apr_crypto_key_t *key = *k;

    if (!key) {
        *k = key = apr_array_push(f->keys);
    }
    if (!key) {
        return APR_ENOMEM;
    }

    key->f = f;
    key->provider = f->provider;

    /* decide on what cipher mechanism we will be using */
    switch (type) {

    case (APR_KEY_3DES_192):
        if (APR_MODE_CBC == mode) {
            key->cipherOid = SEC_OID_DES_EDE3_CBC;
        }
        else if (APR_MODE_ECB == mode) {
            return APR_ENOCIPHER;
            /* No OID for CKM_DES3_ECB; */
        }
        break;
    case (APR_KEY_AES_128):
        if (APR_MODE_CBC == mode) {
            key->cipherOid = SEC_OID_AES_128_CBC;
        }
        else {
            key->cipherOid = SEC_OID_AES_128_ECB;
        }
        break;
    case (APR_KEY_AES_192):
        if (APR_MODE_CBC == mode) {
            key->cipherOid = SEC_OID_AES_192_CBC;
        }
        else {
            key->cipherOid = SEC_OID_AES_192_ECB;
        }
        break;
    case (APR_KEY_AES_256):
        if (APR_MODE_CBC == mode) {
            key->cipherOid = SEC_OID_AES_256_CBC;
        }
        else {
            key->cipherOid = SEC_OID_AES_256_ECB;
        }
        break;
    default:
        /* unknown key type, give up */
        return APR_EKEYTYPE;
    }

    /* AES_128_CBC --> CKM_AES_CBC --> CKM_AES_CBC_PAD */
    key->cipherMech = PK11_AlgtagToMechanism(key->cipherOid);
    if (key->cipherMech == CKM_INVALID_MECHANISM) {
        return APR_ENOCIPHER;
    }
    if (doPad) {
        CK_MECHANISM_TYPE paddedMech;
        paddedMech = PK11_GetPadMechanism(key->cipherMech);
        if (CKM_INVALID_MECHANISM == paddedMech || key->cipherMech
                == paddedMech) {
            return APR_EPADDING;
        }
        key->cipherMech = paddedMech;
    }

    /* Turn the raw passphrase and salt into SECItems */
    passItem.data = (unsigned char*) pass;
    passItem.len = passLen;
    saltItem.data = (unsigned char*) salt;
    saltItem.len = saltLen;

    /* generate the key */
    /* pbeAlg and cipherAlg are the same. NSS decides the keylength. */
    algid = PK11_CreatePBEV2AlgorithmID(key->cipherOid, key->cipherOid,
            SEC_OID_HMAC_SHA1, 0, iterations, &saltItem);
    if (algid) {
        slot = PK11_GetBestSlot(key->cipherMech, wincx);
        if (slot) {
            key->symKey = PK11_PBEKeyGen(slot, algid, &passItem, PR_FALSE,
                    wincx);
            PK11_FreeSlot(slot);
        }
        SECOID_DestroyAlgorithmID(algid, PR_TRUE);
    }

    /* sanity check? */
    if (!key->symKey) {
        PRErrorCode perr = PORT_GetError();
        if (perr) {
            f->result->rc = perr;
            f->result->msg = PR_ErrorToName(perr);
            rv = APR_ENOKEY;
        }
    }

    key->ivSize = PK11_GetIVLength(key->cipherMech);
    if (ivSize) {
        *ivSize = key->ivSize;
    }

    return rv;
}

/**
 * @brief Initialise a context for encrypting arbitrary data using the given key.
 * @note If *ctx is NULL, a apr_crypto_block_t will be created from a pool. If
 *       *ctx is not NULL, *ctx must point at a previously created structure.
 * @param ctx The block context returned, see note.
 * @param iv Optional initialisation vector. If the buffer pointed to is NULL,
 *           an IV will be created at random, in space allocated from the pool.
 *           If the buffer pointed to is not NULL, the IV in the buffer will be
 *           used.
 * @param key The key structure.
 * @param blockSize The block size of the cipher.
 * @param p The pool to use.
 * @return Returns APR_ENOIV if an initialisation vector is required but not specified.
 *         Returns APR_EINIT if the backend failed to initialise the context. Returns
 *         APR_ENOTIMPL if not implemented.
 */
static apr_status_t crypto_block_encrypt_init(apr_crypto_block_t **ctx,
        const unsigned char **iv, const apr_crypto_key_t *key,
        apr_size_t *blockSize, apr_pool_t *p)
{
    PRErrorCode perr;
    SECItem * secParam;
    SECItem ivItem;
    unsigned char * usedIv;
    apr_crypto_block_t *block = *ctx;
    if (!block) {
        *ctx = block = apr_pcalloc(p, sizeof(apr_crypto_block_t));
    }
    if (!block) {
        return APR_ENOMEM;
    }
    block->f = key->f;
    block->pool = p;
    block->provider = key->provider;

    apr_pool_cleanup_register(p, block, crypto_block_cleanup_helper,
            apr_pool_cleanup_null);

    if (key->ivSize) {
        if (iv == NULL) {
            return APR_ENOIV;
        }
        if (*iv == NULL) {
            SECStatus s;
            usedIv = apr_pcalloc(p, key->ivSize);
            if (!usedIv) {
                return APR_ENOMEM;
            }
            apr_crypto_clear(p, usedIv, key->ivSize);
            s = PK11_GenerateRandom(usedIv, key->ivSize);
            if (s != SECSuccess) {
                return APR_ENOIV;
            }
            *iv = usedIv;
        }
        else {
            usedIv = (unsigned char *) *iv;
        }
        ivItem.data = usedIv;
        ivItem.len = key->ivSize;
        secParam = PK11_ParamFromIV(key->cipherMech, &ivItem);
    }
    else {
        secParam = PK11_GenerateNewParam(key->cipherMech, key->symKey);
    }
    block->blockSize = PK11_GetBlockSize(key->cipherMech, secParam);
    block->ctx = PK11_CreateContextBySymKey(key->cipherMech, CKA_ENCRYPT,
            key->symKey, secParam);

    /* did an error occur? */
    perr = PORT_GetError();
    if (perr || !block->ctx) {
        key->f->result->rc = perr;
        key->f->result->msg = PR_ErrorToName(perr);
        return APR_EINIT;
    }

    if (blockSize) {
        *blockSize = PK11_GetBlockSize(key->cipherMech, secParam);
    }

    return APR_SUCCESS;

}

/**
 * @brief Encrypt data provided by in, write it to out.
 * @note The number of bytes written will be written to outlen. If
 *       out is NULL, outlen will contain the maximum size of the
 *       buffer needed to hold the data, including any data
 *       generated by apr_crypto_block_encrypt_finish below. If *out points
 *       to NULL, a buffer sufficiently large will be created from
 *       the pool provided. If *out points to a not-NULL value, this
 *       value will be used as a buffer instead.
 * @param out Address of a buffer to which data will be written,
 *        see note.
 * @param outlen Length of the output will be written here.
 * @param in Address of the buffer to read.
 * @param inlen Length of the buffer to read.
 * @param ctx The block context to use.
 * @return APR_ECRYPT if an error occurred. Returns APR_ENOTIMPL if
 *         not implemented.
 */
static apr_status_t crypto_block_encrypt(unsigned char **out,
        apr_size_t *outlen, const unsigned char *in, apr_size_t inlen,
        apr_crypto_block_t *block)
{

    unsigned char *buffer;
    int outl = (int) *outlen;
    SECStatus s;
    if (!out) {
        *outlen = inlen + block->blockSize;
        return APR_SUCCESS;
    }
    if (!*out) {
        buffer = apr_palloc(block->pool, inlen + block->blockSize);
        if (!buffer) {
            return APR_ENOMEM;
        }
        apr_crypto_clear(block->pool, buffer, inlen + block->blockSize);
        *out = buffer;
    }

    s = PK11_CipherOp(block->ctx, *out, &outl, inlen, (unsigned char*) in,
            inlen);
    if (s != SECSuccess) {
        PRErrorCode perr = PORT_GetError();
        if (perr) {
            block->f->result->rc = perr;
            block->f->result->msg = PR_ErrorToName(perr);
        }
        return APR_ECRYPT;
    }
    *outlen = outl;

    return APR_SUCCESS;

}

/**
 * @brief Encrypt final data block, write it to out.
 * @note If necessary the final block will be written out after being
 *       padded. Typically the final block will be written to the
 *       same buffer used by apr_crypto_block_encrypt, offset by the
 *       number of bytes returned as actually written by the
 *       apr_crypto_block_encrypt() call. After this call, the context
 *       is cleaned and can be reused by apr_crypto_block_encrypt_init().
 * @param out Address of a buffer to which data will be written. This
 *            buffer must already exist, and is usually the same
 *            buffer used by apr_evp_crypt(). See note.
 * @param outlen Length of the output will be written here.
 * @param ctx The block context to use.
 * @return APR_ECRYPT if an error occurred.
 * @return APR_EPADDING if padding was enabled and the block was incorrectly
 *         formatted.
 * @return APR_ENOTIMPL if not implemented.
 */
static apr_status_t crypto_block_encrypt_finish(unsigned char *out,
        apr_size_t *outlen, apr_crypto_block_t *block)
{

    apr_status_t rv = APR_SUCCESS;
    unsigned int outl = *outlen;

    SECStatus s = PK11_DigestFinal(block->ctx, out, &outl, block->blockSize);
    *outlen = outl;

    if (s != SECSuccess) {
        PRErrorCode perr = PORT_GetError();
        if (perr) {
            block->f->result->rc = perr;
            block->f->result->msg = PR_ErrorToName(perr);
        }
        rv = APR_ECRYPT;
    }
    crypto_block_cleanup(block);

    return rv;

}

/**
 * @brief Initialise a context for decrypting arbitrary data using the given key.
 * @note If *ctx is NULL, a apr_crypto_block_t will be created from a pool. If
 *       *ctx is not NULL, *ctx must point at a previously created structure.
 * @param ctx The block context returned, see note.
 * @param blockSize The block size of the cipher.
 * @param iv Optional initialisation vector. If the buffer pointed to is NULL,
 *           an IV will be created at random, in space allocated from the pool.
 *           If the buffer is not NULL, the IV in the buffer will be used.
 * @param key The key structure.
 * @param p The pool to use.
 * @return Returns APR_ENOIV if an initialisation vector is required but not specified.
 *         Returns APR_EINIT if the backend failed to initialise the context. Returns
 *         APR_ENOTIMPL if not implemented.
 */
static apr_status_t crypto_block_decrypt_init(apr_crypto_block_t **ctx,
        apr_size_t *blockSize, const unsigned char *iv,
        const apr_crypto_key_t *key, apr_pool_t *p)
{
    PRErrorCode perr;
    SECItem * secParam;
    apr_crypto_block_t *block = *ctx;
    if (!block) {
        *ctx = block = apr_pcalloc(p, sizeof(apr_crypto_block_t));
    }
    if (!block) {
        return APR_ENOMEM;
    }
    block->f = key->f;
    block->pool = p;
    block->provider = key->provider;

    apr_pool_cleanup_register(p, block, crypto_block_cleanup_helper,
            apr_pool_cleanup_null);

    if (key->ivSize) {
        SECItem ivItem;
        if (iv == NULL) {
            return APR_ENOIV; /* Cannot initialise without an IV */
        }
        ivItem.data = (unsigned char*) iv;
        ivItem.len = key->ivSize;
        secParam = PK11_ParamFromIV(key->cipherMech, &ivItem);
    }
    else {
        secParam = PK11_GenerateNewParam(key->cipherMech, key->symKey);
    }
    block->blockSize = PK11_GetBlockSize(key->cipherMech, secParam);
    block->ctx = PK11_CreateContextBySymKey(key->cipherMech, CKA_DECRYPT,
            key->symKey, secParam);

    /* did an error occur? */
    perr = PORT_GetError();
    if (perr || !block->ctx) {
        key->f->result->rc = perr;
        key->f->result->msg = PR_ErrorToName(perr);
        return APR_EINIT;
    }

    if (blockSize) {
        *blockSize = PK11_GetBlockSize(key->cipherMech, secParam);
    }

    return APR_SUCCESS;

}

/**
 * @brief Decrypt data provided by in, write it to out.
 * @note The number of bytes written will be written to outlen. If
 *       out is NULL, outlen will contain the maximum size of the
 *       buffer needed to hold the data, including any data
 *       generated by apr_crypto_block_decrypt_finish below. If *out points
 *       to NULL, a buffer sufficiently large will be created from
 *       the pool provided. If *out points to a not-NULL value, this
 *       value will be used as a buffer instead.
 * @param out Address of a buffer to which data will be written,
 *        see note.
 * @param outlen Length of the output will be written here.
 * @param in Address of the buffer to read.
 * @param inlen Length of the buffer to read.
 * @param ctx The block context to use.
 * @return APR_ECRYPT if an error occurred. Returns APR_ENOTIMPL if
 *         not implemented.
 */
static apr_status_t crypto_block_decrypt(unsigned char **out,
        apr_size_t *outlen, const unsigned char *in, apr_size_t inlen,
        apr_crypto_block_t *block)
{

    unsigned char *buffer;
    int outl = (int) *outlen;
    SECStatus s;
    if (!out) {
        *outlen = inlen + block->blockSize;
        return APR_SUCCESS;
    }
    if (!*out) {
        buffer = apr_palloc(block->pool, inlen + block->blockSize);
        if (!buffer) {
            return APR_ENOMEM;
        }
        apr_crypto_clear(block->pool, buffer, inlen + block->blockSize);
        *out = buffer;
    }

    s = PK11_CipherOp(block->ctx, *out, &outl, inlen, (unsigned char*) in,
            inlen);
    if (s != SECSuccess) {
        PRErrorCode perr = PORT_GetError();
        if (perr) {
            block->f->result->rc = perr;
            block->f->result->msg = PR_ErrorToName(perr);
        }
        return APR_ECRYPT;
    }
    *outlen = outl;

    return APR_SUCCESS;

}

/**
 * @brief Decrypt final data block, write it to out.
 * @note If necessary the final block will be written out after being
 *       padded. Typically the final block will be written to the
 *       same buffer used by apr_crypto_block_decrypt, offset by the
 *       number of bytes returned as actually written by the
 *       apr_crypto_block_decrypt() call. After this call, the context
 *       is cleaned and can be reused by apr_crypto_block_decrypt_init().
 * @param out Address of a buffer to which data will be written. This
 *            buffer must already exist, and is usually the same
 *            buffer used by apr_evp_crypt(). See note.
 * @param outlen Length of the output will be written here.
 * @param ctx The block context to use.
 * @return APR_ECRYPT if an error occurred.
 * @return APR_EPADDING if padding was enabled and the block was incorrectly
 *         formatted.
 * @return APR_ENOTIMPL if not implemented.
 */
static apr_status_t crypto_block_decrypt_finish(unsigned char *out,
        apr_size_t *outlen, apr_crypto_block_t *block)
{

    apr_status_t rv = APR_SUCCESS;
    unsigned int outl = *outlen;

    SECStatus s = PK11_DigestFinal(block->ctx, out, &outl, block->blockSize);
    *outlen = outl;

    if (s != SECSuccess) {
        PRErrorCode perr = PORT_GetError();
        if (perr) {
            block->f->result->rc = perr;
            block->f->result->msg = PR_ErrorToName(perr);
        }
        rv = APR_ECRYPT;
    }
    crypto_block_cleanup(block);

    return rv;

}

/**
 * NSS module.
 */
APU_MODULE_DECLARE_DATA const apr_crypto_driver_t apr_crypto_nss_driver = {
    "nss", crypto_init, crypto_make, crypto_get_block_key_types,
    crypto_get_block_key_modes, crypto_passphrase,
    crypto_block_encrypt_init, crypto_block_encrypt,
    crypto_block_encrypt_finish, crypto_block_decrypt_init,
    crypto_block_decrypt, crypto_block_decrypt_finish,
    crypto_block_cleanup, crypto_cleanup, crypto_shutdown, crypto_error
};

#endif
