/*
 * Copyright 1999-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/asn1.h>
#include <openssl/objects.h>

static STACK_OF(ASN1_STRING_TABLE) *stable = NULL;
static void st_free(ASN1_STRING_TABLE *tbl);
static int sk_table_cmp(const ASN1_STRING_TABLE *const *a,
                        const ASN1_STRING_TABLE *const *b);

/*
 * This is the global mask for the mbstring functions: this is use to mask
 * out certain types (such as BMPString and UTF8String) because certain
 * software (e.g. Netscape) has problems with them.
 */

static unsigned long global_mask = B_ASN1_UTF8STRING;

void ASN1_STRING_set_default_mask(unsigned long mask)
{
    global_mask = mask;
}

unsigned long ASN1_STRING_get_default_mask(void)
{
    return global_mask;
}

/*-
 * This function sets the default to various "flavours" of configuration.
 * based on an ASCII string. Currently this is:
 * MASK:XXXX : a numerical mask value.
 * nobmp : Don't use BMPStrings (just Printable, T61).
 * pkix : PKIX recommendation in RFC2459.
 * utf8only : only use UTF8Strings (RFC2459 recommendation for 2004).
 * default:   the default value, Printable, T61, BMP.
 */

int ASN1_STRING_set_default_mask_asc(const char *p)
{
    unsigned long mask;
    char *end;

    if (strncmp(p, "MASK:", 5) == 0) {
        if (!p[5])
            return 0;
        mask = strtoul(p + 5, &end, 0);
        if (*end)
            return 0;
    } else if (strcmp(p, "nombstr") == 0)
        mask = ~((unsigned long)(B_ASN1_BMPSTRING | B_ASN1_UTF8STRING));
    else if (strcmp(p, "pkix") == 0)
        mask = ~((unsigned long)B_ASN1_T61STRING);
    else if (strcmp(p, "utf8only") == 0)
        mask = B_ASN1_UTF8STRING;
    else if (strcmp(p, "default") == 0)
        mask = 0xFFFFFFFFL;
    else
        return 0;
    ASN1_STRING_set_default_mask(mask);
    return 1;
}

/*
 * The following function generates an ASN1_STRING based on limits in a
 * table. Frequently the types and length of an ASN1_STRING are restricted by
 * a corresponding OID. For example certificates and certificate requests.
 */

ASN1_STRING *ASN1_STRING_set_by_NID(ASN1_STRING **out,
                                    const unsigned char *in, int inlen,
                                    int inform, int nid)
{
    ASN1_STRING_TABLE *tbl;
    ASN1_STRING *str = NULL;
    unsigned long mask;
    int ret;

    if (out == NULL)
        out = &str;
    tbl = ASN1_STRING_TABLE_get(nid);
    if (tbl != NULL) {
        mask = tbl->mask;
        if (!(tbl->flags & STABLE_NO_MASK))
            mask &= global_mask;
        ret = ASN1_mbstring_ncopy(out, in, inlen, inform, mask,
                                  tbl->minsize, tbl->maxsize);
    } else {
        ret = ASN1_mbstring_copy(out, in, inlen, inform,
                                 DIRSTRING_TYPE & global_mask);
    }
    if (ret <= 0)
        return NULL;
    return *out;
}

/*
 * Now the tables and helper functions for the string table:
 */

#include "tbl_standard.h"

static int sk_table_cmp(const ASN1_STRING_TABLE *const *a,
                        const ASN1_STRING_TABLE *const *b)
{
    return (*a)->nid - (*b)->nid;
}

DECLARE_OBJ_BSEARCH_CMP_FN(ASN1_STRING_TABLE, ASN1_STRING_TABLE, table);

static int table_cmp(const ASN1_STRING_TABLE *a, const ASN1_STRING_TABLE *b)
{
    return a->nid - b->nid;
}

IMPLEMENT_OBJ_BSEARCH_CMP_FN(ASN1_STRING_TABLE, ASN1_STRING_TABLE, table);

ASN1_STRING_TABLE *ASN1_STRING_TABLE_get(int nid)
{
    int idx;
    ASN1_STRING_TABLE fnd;

    fnd.nid = nid;
    if (stable) {
        idx = sk_ASN1_STRING_TABLE_find(stable, &fnd);
        if (idx >= 0)
            return sk_ASN1_STRING_TABLE_value(stable, idx);
    }
    return OBJ_bsearch_table(&fnd, tbl_standard, OSSL_NELEM(tbl_standard));
}

/*
 * Return a string table pointer which can be modified: either directly from
 * table or a copy of an internal value added to the table.
 */

static ASN1_STRING_TABLE *stable_get(int nid)
{
    ASN1_STRING_TABLE *tmp, *rv;

    /* Always need a string table so allocate one if NULL */
    if (stable == NULL) {
        stable = sk_ASN1_STRING_TABLE_new(sk_table_cmp);
        if (stable == NULL)
            return NULL;
    }
    tmp = ASN1_STRING_TABLE_get(nid);
    if (tmp != NULL && tmp->flags & STABLE_FLAGS_MALLOC)
        return tmp;
    if ((rv = OPENSSL_zalloc(sizeof(*rv))) == NULL) {
        ASN1err(ASN1_F_STABLE_GET, ERR_R_MALLOC_FAILURE);
        return NULL;
    }
    if (!sk_ASN1_STRING_TABLE_push(stable, rv)) {
        OPENSSL_free(rv);
        return NULL;
    }
    if (tmp != NULL) {
        rv->nid = tmp->nid;
        rv->minsize = tmp->minsize;
        rv->maxsize = tmp->maxsize;
        rv->mask = tmp->mask;
        rv->flags = tmp->flags | STABLE_FLAGS_MALLOC;
    } else {
        rv->nid = nid;
        rv->minsize = -1;
        rv->maxsize = -1;
        rv->flags = STABLE_FLAGS_MALLOC;
    }
    return rv;
}

int ASN1_STRING_TABLE_add(int nid,
                          long minsize, long maxsize, unsigned long mask,
                          unsigned long flags)
{
    ASN1_STRING_TABLE *tmp;

    tmp = stable_get(nid);
    if (tmp == NULL) {
        ASN1err(ASN1_F_ASN1_STRING_TABLE_ADD, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    if (minsize >= 0)
        tmp->minsize = minsize;
    if (maxsize >= 0)
        tmp->maxsize = maxsize;
    if (mask)
        tmp->mask = mask;
    if (flags)
        tmp->flags = STABLE_FLAGS_MALLOC | flags;
    return 1;
}

void ASN1_STRING_TABLE_cleanup(void)
{
    STACK_OF(ASN1_STRING_TABLE) *tmp;

    tmp = stable;
    if (tmp == NULL)
        return;
    stable = NULL;
    sk_ASN1_STRING_TABLE_pop_free(tmp, st_free);
}

static void st_free(ASN1_STRING_TABLE *tbl)
{
    if (tbl->flags & STABLE_FLAGS_MALLOC)
        OPENSSL_free(tbl);
}
