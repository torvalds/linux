/*
 * Copyright 2001-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "eng_int.h"

static ENGINE_TABLE *rsa_table = NULL;
static const int dummy_nid = 1;

void ENGINE_unregister_RSA(ENGINE *e)
{
    engine_table_unregister(&rsa_table, e);
}

static void engine_unregister_all_RSA(void)
{
    engine_table_cleanup(&rsa_table);
}

int ENGINE_register_RSA(ENGINE *e)
{
    if (e->rsa_meth)
        return engine_table_register(&rsa_table,
                                     engine_unregister_all_RSA, e, &dummy_nid,
                                     1, 0);
    return 1;
}

void ENGINE_register_all_RSA(void)
{
    ENGINE *e;

    for (e = ENGINE_get_first(); e; e = ENGINE_get_next(e))
        ENGINE_register_RSA(e);
}

int ENGINE_set_default_RSA(ENGINE *e)
{
    if (e->rsa_meth)
        return engine_table_register(&rsa_table,
                                     engine_unregister_all_RSA, e, &dummy_nid,
                                     1, 1);
    return 1;
}

/*
 * Exposed API function to get a functional reference from the implementation
 * table (ie. try to get a functional reference from the tabled structural
 * references).
 */
ENGINE *ENGINE_get_default_RSA(void)
{
    return engine_table_select(&rsa_table, dummy_nid);
}

/* Obtains an RSA implementation from an ENGINE functional reference */
const RSA_METHOD *ENGINE_get_RSA(const ENGINE *e)
{
    return e->rsa_meth;
}

/* Sets an RSA implementation in an ENGINE structure */
int ENGINE_set_RSA(ENGINE *e, const RSA_METHOD *rsa_meth)
{
    e->rsa_meth = rsa_meth;
    return 1;
}
