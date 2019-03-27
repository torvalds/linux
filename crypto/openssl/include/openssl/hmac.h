/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_HMAC_H
# define HEADER_HMAC_H

# include <openssl/opensslconf.h>

# include <openssl/evp.h>

# if OPENSSL_API_COMPAT < 0x10200000L
#  define HMAC_MAX_MD_CBLOCK      128    /* Deprecated */
# endif

#ifdef  __cplusplus
extern "C" {
#endif

size_t HMAC_size(const HMAC_CTX *e);
HMAC_CTX *HMAC_CTX_new(void);
int HMAC_CTX_reset(HMAC_CTX *ctx);
void HMAC_CTX_free(HMAC_CTX *ctx);

DEPRECATEDIN_1_1_0(__owur int HMAC_Init(HMAC_CTX *ctx, const void *key, int len,
                     const EVP_MD *md))

/*__owur*/ int HMAC_Init_ex(HMAC_CTX *ctx, const void *key, int len,
                            const EVP_MD *md, ENGINE *impl);
/*__owur*/ int HMAC_Update(HMAC_CTX *ctx, const unsigned char *data,
                           size_t len);
/*__owur*/ int HMAC_Final(HMAC_CTX *ctx, unsigned char *md,
                          unsigned int *len);
unsigned char *HMAC(const EVP_MD *evp_md, const void *key, int key_len,
                    const unsigned char *d, size_t n, unsigned char *md,
                    unsigned int *md_len);
__owur int HMAC_CTX_copy(HMAC_CTX *dctx, HMAC_CTX *sctx);

void HMAC_CTX_set_flags(HMAC_CTX *ctx, unsigned long flags);
const EVP_MD *HMAC_CTX_get_md(const HMAC_CTX *ctx);

#ifdef  __cplusplus
}
#endif

#endif
