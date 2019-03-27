/*
 * Copyright 2001-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "eng_int.h"

static ENGINE_TABLE *rand_table = NULL;
static const int dummy_nid = 1;

void ENGINE_unregister_RAND(ENGINE *e)
{
    engine_table_unregister(&rand_table, e);
}

static void engine_unregister_all_RAND(void)
{
    engine_table_cleanup(&rand_table);
}

int ENGINE_register_RAND(ENGINE *e)
{
    if (e->rand_meth)
        return engine_table_register(&rand_table,
                                     engine_unregister_all_RAND, e,
                                     &dummy_nid, 1, 0);
    return 1;
}

void ENGINE_register_all_RAND(void)
{
    ENGINE *e;

    for (e = ENGINE_get_first(); e; e = ENGINE_get_next(e))
        ENGINE_register_RAND(e);
}

int ENGINE_set_default_RAND(ENGINE *e)
{
    if (e->rand_meth)
        return engine_table_register(&rand_table,
                                     engine_unregister_all_RAND, e,
                                     &dummy_nid, 1, 1);
    return 1;
}

/*
 * Exposed API function to get a functional reference from the implementation
 * table (ie. try to get a functional reference from the tabled structural
 * references).
 */
ENGINE *ENGINE_get_default_RAND(void)
{
    return engine_table_select(&rand_table, dummy_nid);
}

/* Obtains an RAND implementation from an ENGINE functional reference */
const RAND_METHOD *ENGINE_get_RAND(const ENGINE *e)
{
    return e->rand_meth;
}

/* Sets an RAND implementation in an ENGINE structure */
int ENGINE_set_RAND(ENGINE *e, const RAND_METHOD *rand_meth)
{
    e->rand_meth = rand_meth;
    return 1;
}
