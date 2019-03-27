/*
 * Copyright 2006-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "eng_int.h"
#include <openssl/evp.h>

static ENGINE_TABLE *pkey_meth_table = NULL;

void ENGINE_unregister_pkey_meths(ENGINE *e)
{
    engine_table_unregister(&pkey_meth_table, e);
}

static void engine_unregister_all_pkey_meths(void)
{
    engine_table_cleanup(&pkey_meth_table);
}

int ENGINE_register_pkey_meths(ENGINE *e)
{
    if (e->pkey_meths) {
        const int *nids;
        int num_nids = e->pkey_meths(e, NULL, &nids, 0);
        if (num_nids > 0)
            return engine_table_register(&pkey_meth_table,
                                         engine_unregister_all_pkey_meths, e,
                                         nids, num_nids, 0);
    }
    return 1;
}

void ENGINE_register_all_pkey_meths(void)
{
    ENGINE *e;

    for (e = ENGINE_get_first(); e; e = ENGINE_get_next(e))
        ENGINE_register_pkey_meths(e);
}

int ENGINE_set_default_pkey_meths(ENGINE *e)
{
    if (e->pkey_meths) {
        const int *nids;
        int num_nids = e->pkey_meths(e, NULL, &nids, 0);
        if (num_nids > 0)
            return engine_table_register(&pkey_meth_table,
                                         engine_unregister_all_pkey_meths, e,
                                         nids, num_nids, 1);
    }
    return 1;
}

/*
 * Exposed API function to get a functional reference from the implementation
 * table (ie. try to get a functional reference from the tabled structural
 * references) for a given pkey_meth 'nid'
 */
ENGINE *ENGINE_get_pkey_meth_engine(int nid)
{
    return engine_table_select(&pkey_meth_table, nid);
}

/* Obtains a pkey_meth implementation from an ENGINE functional reference */
const EVP_PKEY_METHOD *ENGINE_get_pkey_meth(ENGINE *e, int nid)
{
    EVP_PKEY_METHOD *ret;
    ENGINE_PKEY_METHS_PTR fn = ENGINE_get_pkey_meths(e);
    if (!fn || !fn(e, &ret, NULL, nid)) {
        ENGINEerr(ENGINE_F_ENGINE_GET_PKEY_METH,
                  ENGINE_R_UNIMPLEMENTED_PUBLIC_KEY_METHOD);
        return NULL;
    }
    return ret;
}

/* Gets the pkey_meth callback from an ENGINE structure */
ENGINE_PKEY_METHS_PTR ENGINE_get_pkey_meths(const ENGINE *e)
{
    return e->pkey_meths;
}

/* Sets the pkey_meth callback in an ENGINE structure */
int ENGINE_set_pkey_meths(ENGINE *e, ENGINE_PKEY_METHS_PTR f)
{
    e->pkey_meths = f;
    return 1;
}

/*
 * Internal function to free up EVP_PKEY_METHOD structures before an ENGINE
 * is destroyed
 */

void engine_pkey_meths_free(ENGINE *e)
{
    int i;
    EVP_PKEY_METHOD *pkm;
    if (e->pkey_meths) {
        const int *pknids;
        int npknids;
        npknids = e->pkey_meths(e, NULL, &pknids, 0);
        for (i = 0; i < npknids; i++) {
            if (e->pkey_meths(e, &pkm, NULL, pknids[i])) {
                EVP_PKEY_meth_free(pkm);
            }
        }
    }
}
