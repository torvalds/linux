/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */
#include <openssl/crypto.h>

#include "internal/tsan_assist.h"

struct lhash_node_st {
    void *data;
    struct lhash_node_st *next;
    unsigned long hash;
};

struct lhash_st {
    OPENSSL_LH_NODE **b;
    OPENSSL_LH_COMPFUNC comp;
    OPENSSL_LH_HASHFUNC hash;
    unsigned int num_nodes;
    unsigned int num_alloc_nodes;
    unsigned int p;
    unsigned int pmax;
    unsigned long up_load;      /* load times 256 */
    unsigned long down_load;    /* load times 256 */
    unsigned long num_items;
    unsigned long num_expands;
    unsigned long num_expand_reallocs;
    unsigned long num_contracts;
    unsigned long num_contract_reallocs;
    TSAN_QUALIFIER unsigned long num_hash_calls;
    TSAN_QUALIFIER unsigned long num_comp_calls;
    unsigned long num_insert;
    unsigned long num_replace;
    unsigned long num_delete;
    unsigned long num_no_delete;
    TSAN_QUALIFIER unsigned long num_retrieve;
    TSAN_QUALIFIER unsigned long num_retrieve_miss;
    TSAN_QUALIFIER unsigned long num_hash_comps;
    int error;
};
