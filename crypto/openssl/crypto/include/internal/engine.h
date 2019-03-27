/*
 * Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/engine.h>

void engine_load_openssl_int(void);
void engine_load_devcrypto_int(void);
void engine_load_rdrand_int(void);
void engine_load_dynamic_int(void);
void engine_load_padlock_int(void);
void engine_load_capi_int(void);
void engine_load_dasync_int(void);
void engine_load_afalg_int(void);
void engine_cleanup_int(void);
