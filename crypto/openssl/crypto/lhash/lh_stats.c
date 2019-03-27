/*
 * Copyright 1995-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/*
 * If you wish to build this outside of OpenSSL, remove the following lines
 * and things should work as expected
 */
#include "internal/cryptlib.h"

#include <openssl/bio.h>
#include <openssl/lhash.h>
#include "lhash_lcl.h"

# ifndef OPENSSL_NO_STDIO
void OPENSSL_LH_stats(const OPENSSL_LHASH *lh, FILE *fp)
{
    BIO *bp;

    bp = BIO_new(BIO_s_file());
    if (bp == NULL)
        return;
    BIO_set_fp(bp, fp, BIO_NOCLOSE);
    OPENSSL_LH_stats_bio(lh, bp);
    BIO_free(bp);
}

void OPENSSL_LH_node_stats(const OPENSSL_LHASH *lh, FILE *fp)
{
    BIO *bp;

    bp = BIO_new(BIO_s_file());
    if (bp == NULL)
        return;
    BIO_set_fp(bp, fp, BIO_NOCLOSE);
    OPENSSL_LH_node_stats_bio(lh, bp);
    BIO_free(bp);
}

void OPENSSL_LH_node_usage_stats(const OPENSSL_LHASH *lh, FILE *fp)
{
    BIO *bp;

    bp = BIO_new(BIO_s_file());
    if (bp == NULL)
        return;
    BIO_set_fp(bp, fp, BIO_NOCLOSE);
    OPENSSL_LH_node_usage_stats_bio(lh, bp);
    BIO_free(bp);
}

# endif

void OPENSSL_LH_stats_bio(const OPENSSL_LHASH *lh, BIO *out)
{
    BIO_printf(out, "num_items             = %lu\n", lh->num_items);
    BIO_printf(out, "num_nodes             = %u\n",  lh->num_nodes);
    BIO_printf(out, "num_alloc_nodes       = %u\n",  lh->num_alloc_nodes);
    BIO_printf(out, "num_expands           = %lu\n", lh->num_expands);
    BIO_printf(out, "num_expand_reallocs   = %lu\n", lh->num_expand_reallocs);
    BIO_printf(out, "num_contracts         = %lu\n", lh->num_contracts);
    BIO_printf(out, "num_contract_reallocs = %lu\n", lh->num_contract_reallocs);
    BIO_printf(out, "num_hash_calls        = %lu\n", lh->num_hash_calls);
    BIO_printf(out, "num_comp_calls        = %lu\n", lh->num_comp_calls);
    BIO_printf(out, "num_insert            = %lu\n", lh->num_insert);
    BIO_printf(out, "num_replace           = %lu\n", lh->num_replace);
    BIO_printf(out, "num_delete            = %lu\n", lh->num_delete);
    BIO_printf(out, "num_no_delete         = %lu\n", lh->num_no_delete);
    BIO_printf(out, "num_retrieve          = %lu\n", lh->num_retrieve);
    BIO_printf(out, "num_retrieve_miss     = %lu\n", lh->num_retrieve_miss);
    BIO_printf(out, "num_hash_comps        = %lu\n", lh->num_hash_comps);
}

void OPENSSL_LH_node_stats_bio(const OPENSSL_LHASH *lh, BIO *out)
{
    OPENSSL_LH_NODE *n;
    unsigned int i, num;

    for (i = 0; i < lh->num_nodes; i++) {
        for (n = lh->b[i], num = 0; n != NULL; n = n->next)
            num++;
        BIO_printf(out, "node %6u -> %3u\n", i, num);
    }
}

void OPENSSL_LH_node_usage_stats_bio(const OPENSSL_LHASH *lh, BIO *out)
{
    OPENSSL_LH_NODE *n;
    unsigned long num;
    unsigned int i;
    unsigned long total = 0, n_used = 0;

    for (i = 0; i < lh->num_nodes; i++) {
        for (n = lh->b[i], num = 0; n != NULL; n = n->next)
            num++;
        if (num != 0) {
            n_used++;
            total += num;
        }
    }
    BIO_printf(out, "%lu nodes used out of %u\n", n_used, lh->num_nodes);
    BIO_printf(out, "%lu items\n", total);
    if (n_used == 0)
        return;
    BIO_printf(out, "load %d.%02d  actual load %d.%02d\n",
               (int)(total / lh->num_nodes),
               (int)((total % lh->num_nodes) * 100 / lh->num_nodes),
               (int)(total / n_used), (int)((total % n_used) * 100 / n_used));
}
