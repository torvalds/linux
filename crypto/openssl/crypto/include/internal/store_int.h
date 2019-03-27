/*
 * Copyright 2017-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_STORE_INT_H
# define HEADER_STORE_INT_H

# include <openssl/bio.h>
# include <openssl/store.h>
# include <openssl/ui.h>

/*
 * Two functions to read PEM data off an already opened BIO.  To be used
 * instead of OSSLSTORE_open() and OSSLSTORE_close().  Everything is done
 * as usual with OSSLSTORE_load() and OSSLSTORE_eof().
 */
OSSL_STORE_CTX *ossl_store_attach_pem_bio(BIO *bp, const UI_METHOD *ui_method,
                                          void *ui_data);
int ossl_store_detach_pem_bio(OSSL_STORE_CTX *ctx);

#endif
