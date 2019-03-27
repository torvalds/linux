/*
 * Copyright 2001-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "eng_int.h"

static ENGINE_TABLE *cipher_table = NULL;

void ENGINE_unregister_ciphers(ENGINE *e)
{
    engine_table_unregister(&cipher_table, e);
}

static void engine_unregister_all_ciphers(void)
{
    engine_table_cleanup(&cipher_table);
}

int ENGINE_register_ciphers(ENGINE *e)
{
    if (e->ciphers) {
        const int *nids;
        int num_nids = e->ciphers(e, NULL, &nids, 0);
        if (num_nids > 0)
            return engine_table_register(&cipher_table,
                                         engine_unregister_all_ciphers, e,
                                         nids, num_nids, 0);
    }
    return 1;
}

void ENGINE_register_all_ciphers(void)
{
    ENGINE *e;

    for (e = ENGINE_get_first(); e; e = ENGINE_get_next(e))
        ENGINE_register_ciphers(e);
}

int ENGINE_set_default_ciphers(ENGINE *e)
{
    if (e->ciphers) {
        const int *nids;
        int num_nids = e->ciphers(e, NULL, &nids, 0);
        if (num_nids > 0)
            return engine_table_register(&cipher_table,
                                         engine_unregister_all_ciphers, e,
                                         nids, num_nids, 1);
    }
    return 1;
}

/*
 * Exposed API function to get a functional reference from the implementation
 * table (ie. try to get a functional reference from the tabled structural
 * references) for a given cipher 'nid'
 */
ENGINE *ENGINE_get_cipher_engine(int nid)
{
    return engine_table_select(&cipher_table, nid);
}

/* Obtains a cipher implementation from an ENGINE functional reference */
const EVP_CIPHER *ENGINE_get_cipher(ENGINE *e, int nid)
{
    const EVP_CIPHER *ret;
    ENGINE_CIPHERS_PTR fn = ENGINE_get_ciphers(e);
    if (!fn || !fn(e, &ret, NULL, nid)) {
        ENGINEerr(ENGINE_F_ENGINE_GET_CIPHER, ENGINE_R_UNIMPLEMENTED_CIPHER);
        return NULL;
    }
    return ret;
}

/* Gets the cipher callback from an ENGINE structure */
ENGINE_CIPHERS_PTR ENGINE_get_ciphers(const ENGINE *e)
{
    return e->ciphers;
}

/* Sets the cipher callback in an ENGINE structure */
int ENGINE_set_ciphers(ENGINE *e, ENGINE_CIPHERS_PTR f)
{
    e->ciphers = f;
    return 1;
}
