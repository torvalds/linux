/*
 * Copyright 2000-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stddef.h>
#include <openssl/asn1.h>
#include <openssl/objects.h>
#include <openssl/err.h>
#include <openssl/asn1t.h>
#include <string.h>
#include "asn1_locl.h"

static int asn1_item_embed_new(ASN1_VALUE **pval, const ASN1_ITEM *it,
                               int embed);
static int asn1_primitive_new(ASN1_VALUE **pval, const ASN1_ITEM *it,
                              int embed);
static void asn1_item_clear(ASN1_VALUE **pval, const ASN1_ITEM *it);
static int asn1_template_new(ASN1_VALUE **pval, const ASN1_TEMPLATE *tt);
static void asn1_template_clear(ASN1_VALUE **pval, const ASN1_TEMPLATE *tt);
static void asn1_primitive_clear(ASN1_VALUE **pval, const ASN1_ITEM *it);

ASN1_VALUE *ASN1_item_new(const ASN1_ITEM *it)
{
    ASN1_VALUE *ret = NULL;
    if (ASN1_item_ex_new(&ret, it) > 0)
        return ret;
    return NULL;
}

/* Allocate an ASN1 structure */

int ASN1_item_ex_new(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
    return asn1_item_embed_new(pval, it, 0);
}

int asn1_item_embed_new(ASN1_VALUE **pval, const ASN1_ITEM *it, int embed)
{
    const ASN1_TEMPLATE *tt = NULL;
    const ASN1_EXTERN_FUNCS *ef;
    const ASN1_AUX *aux = it->funcs;
    ASN1_aux_cb *asn1_cb;
    ASN1_VALUE **pseqval;
    int i;
    if (aux && aux->asn1_cb)
        asn1_cb = aux->asn1_cb;
    else
        asn1_cb = 0;

#ifndef OPENSSL_NO_CRYPTO_MDEBUG
    OPENSSL_mem_debug_push(it->sname ? it->sname : "asn1_item_embed_new");
#endif

    switch (it->itype) {

    case ASN1_ITYPE_EXTERN:
        ef = it->funcs;
        if (ef && ef->asn1_ex_new) {
            if (!ef->asn1_ex_new(pval, it))
                goto memerr;
        }
        break;

    case ASN1_ITYPE_PRIMITIVE:
        if (it->templates) {
            if (!asn1_template_new(pval, it->templates))
                goto memerr;
        } else if (!asn1_primitive_new(pval, it, embed))
            goto memerr;
        break;

    case ASN1_ITYPE_MSTRING:
        if (!asn1_primitive_new(pval, it, embed))
            goto memerr;
        break;

    case ASN1_ITYPE_CHOICE:
        if (asn1_cb) {
            i = asn1_cb(ASN1_OP_NEW_PRE, pval, it, NULL);
            if (!i)
                goto auxerr;
            if (i == 2) {
#ifndef OPENSSL_NO_CRYPTO_MDEBUG
                OPENSSL_mem_debug_pop();
#endif
                return 1;
            }
        }
        if (embed) {
            memset(*pval, 0, it->size);
        } else {
            *pval = OPENSSL_zalloc(it->size);
            if (*pval == NULL)
                goto memerr;
        }
        asn1_set_choice_selector(pval, -1, it);
        if (asn1_cb && !asn1_cb(ASN1_OP_NEW_POST, pval, it, NULL))
            goto auxerr2;
        break;

    case ASN1_ITYPE_NDEF_SEQUENCE:
    case ASN1_ITYPE_SEQUENCE:
        if (asn1_cb) {
            i = asn1_cb(ASN1_OP_NEW_PRE, pval, it, NULL);
            if (!i)
                goto auxerr;
            if (i == 2) {
#ifndef OPENSSL_NO_CRYPTO_MDEBUG
                OPENSSL_mem_debug_pop();
#endif
                return 1;
            }
        }
        if (embed) {
            memset(*pval, 0, it->size);
        } else {
            *pval = OPENSSL_zalloc(it->size);
            if (*pval == NULL)
                goto memerr;
        }
        /* 0 : init. lock */
        if (asn1_do_lock(pval, 0, it) < 0) {
            if (!embed) {
                OPENSSL_free(*pval);
                *pval = NULL;
            }
            goto memerr;
        }
        asn1_enc_init(pval, it);
        for (i = 0, tt = it->templates; i < it->tcount; tt++, i++) {
            pseqval = asn1_get_field_ptr(pval, tt);
            if (!asn1_template_new(pseqval, tt))
                goto memerr2;
        }
        if (asn1_cb && !asn1_cb(ASN1_OP_NEW_POST, pval, it, NULL))
            goto auxerr2;
        break;
    }
#ifndef OPENSSL_NO_CRYPTO_MDEBUG
    OPENSSL_mem_debug_pop();
#endif
    return 1;

 memerr2:
    asn1_item_embed_free(pval, it, embed);
 memerr:
    ASN1err(ASN1_F_ASN1_ITEM_EMBED_NEW, ERR_R_MALLOC_FAILURE);
#ifndef OPENSSL_NO_CRYPTO_MDEBUG
    OPENSSL_mem_debug_pop();
#endif
    return 0;

 auxerr2:
    asn1_item_embed_free(pval, it, embed);
 auxerr:
    ASN1err(ASN1_F_ASN1_ITEM_EMBED_NEW, ASN1_R_AUX_ERROR);
#ifndef OPENSSL_NO_CRYPTO_MDEBUG
    OPENSSL_mem_debug_pop();
#endif
    return 0;

}

static void asn1_item_clear(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
    const ASN1_EXTERN_FUNCS *ef;

    switch (it->itype) {

    case ASN1_ITYPE_EXTERN:
        ef = it->funcs;
        if (ef && ef->asn1_ex_clear)
            ef->asn1_ex_clear(pval, it);
        else
            *pval = NULL;
        break;

    case ASN1_ITYPE_PRIMITIVE:
        if (it->templates)
            asn1_template_clear(pval, it->templates);
        else
            asn1_primitive_clear(pval, it);
        break;

    case ASN1_ITYPE_MSTRING:
        asn1_primitive_clear(pval, it);
        break;

    case ASN1_ITYPE_CHOICE:
    case ASN1_ITYPE_SEQUENCE:
    case ASN1_ITYPE_NDEF_SEQUENCE:
        *pval = NULL;
        break;
    }
}

static int asn1_template_new(ASN1_VALUE **pval, const ASN1_TEMPLATE *tt)
{
    const ASN1_ITEM *it = ASN1_ITEM_ptr(tt->item);
    int embed = tt->flags & ASN1_TFLG_EMBED;
    ASN1_VALUE *tval;
    int ret;
    if (embed) {
        tval = (ASN1_VALUE *)pval;
        pval = &tval;
    }
    if (tt->flags & ASN1_TFLG_OPTIONAL) {
        asn1_template_clear(pval, tt);
        return 1;
    }
    /* If ANY DEFINED BY nothing to do */

    if (tt->flags & ASN1_TFLG_ADB_MASK) {
        *pval = NULL;
        return 1;
    }
#ifndef OPENSSL_NO_CRYPTO_MDEBUG
    OPENSSL_mem_debug_push(tt->field_name
            ? tt->field_name : "asn1_template_new");
#endif
    /* If SET OF or SEQUENCE OF, its a STACK */
    if (tt->flags & ASN1_TFLG_SK_MASK) {
        STACK_OF(ASN1_VALUE) *skval;
        skval = sk_ASN1_VALUE_new_null();
        if (!skval) {
            ASN1err(ASN1_F_ASN1_TEMPLATE_NEW, ERR_R_MALLOC_FAILURE);
            ret = 0;
            goto done;
        }
        *pval = (ASN1_VALUE *)skval;
        ret = 1;
        goto done;
    }
    /* Otherwise pass it back to the item routine */
    ret = asn1_item_embed_new(pval, it, embed);
 done:
#ifndef OPENSSL_NO_CRYPTO_MDEBUG
    OPENSSL_mem_debug_pop();
#endif
    return ret;
}

static void asn1_template_clear(ASN1_VALUE **pval, const ASN1_TEMPLATE *tt)
{
    /* If ADB or STACK just NULL the field */
    if (tt->flags & (ASN1_TFLG_ADB_MASK | ASN1_TFLG_SK_MASK))
        *pval = NULL;
    else
        asn1_item_clear(pval, ASN1_ITEM_ptr(tt->item));
}

/*
 * NB: could probably combine most of the real XXX_new() behaviour and junk
 * all the old functions.
 */

static int asn1_primitive_new(ASN1_VALUE **pval, const ASN1_ITEM *it,
                              int embed)
{
    ASN1_TYPE *typ;
    ASN1_STRING *str;
    int utype;

    if (!it)
        return 0;

    if (it->funcs) {
        const ASN1_PRIMITIVE_FUNCS *pf = it->funcs;
        if (embed) {
            if (pf->prim_clear) {
                pf->prim_clear(pval, it);
                return 1;
            }
        } else if (pf->prim_new) {
            return pf->prim_new(pval, it);
        }
    }

    if (it->itype == ASN1_ITYPE_MSTRING)
        utype = -1;
    else
        utype = it->utype;
    switch (utype) {
    case V_ASN1_OBJECT:
        *pval = (ASN1_VALUE *)OBJ_nid2obj(NID_undef);
        return 1;

    case V_ASN1_BOOLEAN:
        *(ASN1_BOOLEAN *)pval = it->size;
        return 1;

    case V_ASN1_NULL:
        *pval = (ASN1_VALUE *)1;
        return 1;

    case V_ASN1_ANY:
        if ((typ = OPENSSL_malloc(sizeof(*typ))) == NULL) {
            ASN1err(ASN1_F_ASN1_PRIMITIVE_NEW, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        typ->value.ptr = NULL;
        typ->type = -1;
        *pval = (ASN1_VALUE *)typ;
        break;

    default:
        if (embed) {
            str = *(ASN1_STRING **)pval;
            memset(str, 0, sizeof(*str));
            str->type = utype;
            str->flags = ASN1_STRING_FLAG_EMBED;
        } else {
            str = ASN1_STRING_type_new(utype);
            *pval = (ASN1_VALUE *)str;
        }
        if (it->itype == ASN1_ITYPE_MSTRING && str)
            str->flags |= ASN1_STRING_FLAG_MSTRING;
        break;
    }
    if (*pval)
        return 1;
    return 0;
}

static void asn1_primitive_clear(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
    int utype;
    if (it && it->funcs) {
        const ASN1_PRIMITIVE_FUNCS *pf = it->funcs;
        if (pf->prim_clear)
            pf->prim_clear(pval, it);
        else
            *pval = NULL;
        return;
    }
    if (!it || (it->itype == ASN1_ITYPE_MSTRING))
        utype = -1;
    else
        utype = it->utype;
    if (utype == V_ASN1_BOOLEAN)
        *(ASN1_BOOLEAN *)pval = it->size;
    else
        *pval = NULL;
}
