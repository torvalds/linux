/*
 * Copyright 2001-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "eng_int.h"

static ENGINE_TABLE *dh_table = NULL;
static const int dummy_nid = 1;

void ENGINE_unregister_DH(ENGINE *e)
{
    engine_table_unregister(&dh_table, e);
}

static void engine_unregister_all_DH(void)
{
    engine_table_cleanup(&dh_table);
}

int ENGINE_register_DH(ENGINE *e)
{
    if (e->dh_meth)
        return engine_table_register(&dh_table,
                                     engine_unregister_all_DH, e, &dummy_nid,
                                     1, 0);
    return 1;
}

void ENGINE_register_all_DH(void)
{
    ENGINE *e;

    for (e = ENGINE_get_first(); e; e = ENGINE_get_next(e))
        ENGINE_register_DH(e);
}

int ENGINE_set_default_DH(ENGINE *e)
{
    if (e->dh_meth)
        return engine_table_register(&dh_table,
                                     engine_unregister_all_DH, e, &dummy_nid,
                                     1, 1);
    return 1;
}

/*
 * Exposed API function to get a functional reference from the implementation
 * table (ie. try to get a functional reference from the tabled structural
 * references).
 */
ENGINE *ENGINE_get_default_DH(void)
{
    return engine_table_select(&dh_table, dummy_nid);
}

/* Obtains an DH implementation from an ENGINE functional reference */
const DH_METHOD *ENGINE_get_DH(const ENGINE *e)
{
    return e->dh_meth;
}

/* Sets an DH implementation in an ENGINE structure */
int ENGINE_set_DH(ENGINE *e, const DH_METHOD *dh_meth)
{
    e->dh_meth = dh_meth;
    return 1;
}
