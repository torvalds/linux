/*
 * Copyright 2001-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "eng_int.h"

static ENGINE_TABLE *dsa_table = NULL;
static const int dummy_nid = 1;

void ENGINE_unregister_DSA(ENGINE *e)
{
    engine_table_unregister(&dsa_table, e);
}

static void engine_unregister_all_DSA(void)
{
    engine_table_cleanup(&dsa_table);
}

int ENGINE_register_DSA(ENGINE *e)
{
    if (e->dsa_meth)
        return engine_table_register(&dsa_table,
                                     engine_unregister_all_DSA, e, &dummy_nid,
                                     1, 0);
    return 1;
}

void ENGINE_register_all_DSA(void)
{
    ENGINE *e;

    for (e = ENGINE_get_first(); e; e = ENGINE_get_next(e))
        ENGINE_register_DSA(e);
}

int ENGINE_set_default_DSA(ENGINE *e)
{
    if (e->dsa_meth)
        return engine_table_register(&dsa_table,
                                     engine_unregister_all_DSA, e, &dummy_nid,
                                     1, 1);
    return 1;
}

/*
 * Exposed API function to get a functional reference from the implementation
 * table (ie. try to get a functional reference from the tabled structural
 * references).
 */
ENGINE *ENGINE_get_default_DSA(void)
{
    return engine_table_select(&dsa_table, dummy_nid);
}

/* Obtains an DSA implementation from an ENGINE functional reference */
const DSA_METHOD *ENGINE_get_DSA(const ENGINE *e)
{
    return e->dsa_meth;
}

/* Sets an DSA implementation in an ENGINE structure */
int ENGINE_set_DSA(ENGINE *e, const DSA_METHOD *dsa_meth)
{
    e->dsa_meth = dsa_meth;
    return 1;
}
