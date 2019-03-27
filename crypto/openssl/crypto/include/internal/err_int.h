/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef INTERNAL_ERR_INT_H
# define INTERNAL_ERR_INT_H

int err_load_crypto_strings_int(void);
void err_cleanup(void);
void err_delete_thread_state(void);
int err_shelve_state(void **);
void err_unshelve_state(void *);

#endif
