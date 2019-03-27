/*
 * Copyright 2000-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stddef.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/objects.h>
#include "asn1_locl.h"

/* Free up an ASN1 structure */

void ASN1_item_free(ASN1_VALUE *val, const ASN1_ITEM *it)
{
    asn1_item_embed_free(&val, it, 0);
}

void ASN1_item_ex_free(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
    asn1_item_embed_free(pval, it, 0);
}

void asn1_item_embed_free(ASN1_VALUE **pval, const ASN1_ITEM *it, int embed)
{
    const ASN1_TEMPLATE *tt = NULL, *seqtt;
    const ASN1_EXTERN_FUNCS *ef;
    const ASN1_AUX *aux = it->funcs;
    ASN1_aux_cb *asn1_cb;
    int i;

    if (!pval)
        return;
    if ((it->itype != ASN1_ITYPE_PRIMITIVE) && !*pval)
        return;
    if (aux && aux->asn1_cb)
        asn1_cb = aux->asn1_cb;
    else
        asn1_cb = 0;

    switch (it->itype) {

    case ASN1_ITYPE_PRIMITIVE:
        if (it->templates)
            asn1_template_free(pval, it->templates);
        else
            asn1_primitive_free(pval, it, embed);
        break;

    case ASN1_ITYPE_MSTRING:
        asn1_primitive_free(pval, it, embed);
        break;

    case ASN1_ITYPE_CHOICE:
        if (asn1_cb) {
            i = asn1_cb(ASN1_OP_FREE_PRE, pval, it, NULL);
            if (i == 2)
                return;
        }
        i = asn1_get_choice_selector(pval, it);
        if ((i >= 0) && (i < it->tcount)) {
            ASN1_VALUE **pchval;

            tt = it->templates + i;
            pchval = asn1_get_field_ptr(pval, tt);
            asn1_template_free(pchval, tt);
        }
        if (asn1_cb)
            asn1_cb(ASN1_OP_FREE_POST, pval, it, NULL);
        if (embed == 0) {
            OPENSSL_free(*pval);
            *pval = NULL;
        }
        break;

    case ASN1_ITYPE_EXTERN:
        ef = it->funcs;
        if (ef && ef->asn1_ex_free)
            ef->asn1_ex_free(pval, it);
        break;

    case ASN1_ITYPE_NDEF_SEQUENCE:
    case ASN1_ITYPE_SEQUENCE:
        if (asn1_do_lock(pval, -1, it) != 0) /* if error or ref-counter > 0 */
            return;
        if (asn1_cb) {
            i = asn1_cb(ASN1_OP_FREE_PRE, pval, it, NULL);
            if (i == 2)
                return;
        }
        asn1_enc_free(pval, it);
        /*
         * If we free up as normal we will invalidate any ANY DEFINED BY
         * field and we won't be able to determine the type of the field it
         * defines. So free up in reverse order.
         */
        tt = it->templates + it->tcount;
        for (i = 0; i < it->tcount; i++) {
            ASN1_VALUE **pseqval;

            tt--;
            seqtt = asn1_do_adb(pval, tt, 0);
            if (!seqtt)
                continue;
            pseqval = asn1_get_field_ptr(pval, seqtt);
            asn1_template_free(pseqval, seqtt);
        }
        if (asn1_cb)
            asn1_cb(ASN1_OP_FREE_POST, pval, it, NULL);
        if (embed == 0) {
            OPENSSL_free(*pval);
            *pval = NULL;
        }
        break;
    }
}

void asn1_template_free(ASN1_VALUE **pval, const ASN1_TEMPLATE *tt)
{
    int embed = tt->flags & ASN1_TFLG_EMBED;
    ASN1_VALUE *tval;
    if (embed) {
        tval = (ASN1_VALUE *)pval;
        pval = &tval;
    }
    if (tt->flags & ASN1_TFLG_SK_MASK) {
        STACK_OF(ASN1_VALUE) *sk = (STACK_OF(ASN1_VALUE) *)*pval;
        int i;

        for (i = 0; i < sk_ASN1_VALUE_num(sk); i++) {
            ASN1_VALUE *vtmp = sk_ASN1_VALUE_value(sk, i);

            asn1_item_embed_free(&vtmp, ASN1_ITEM_ptr(tt->item), embed);
        }
        sk_ASN1_VALUE_free(sk);
        *pval = NULL;
    } else {
        asn1_item_embed_free(pval, ASN1_ITEM_ptr(tt->item), embed);
    }
}

void asn1_primitive_free(ASN1_VALUE **pval, const ASN1_ITEM *it, int embed)
{
    int utype;

    /* Special case: if 'it' is a primitive with a free_func, use that. */
    if (it) {
        const ASN1_PRIMITIVE_FUNCS *pf = it->funcs;

        if (embed) {
            if (pf && pf->prim_clear) {
                pf->prim_clear(pval, it);
                return;
            }
        } else if (pf && pf->prim_free) {
            pf->prim_free(pval, it);
            return;
        }
    }

    /* Special case: if 'it' is NULL, free contents of ASN1_TYPE */
    if (!it) {
        ASN1_TYPE *typ = (ASN1_TYPE *)*pval;

        utype = typ->type;
        pval = &typ->value.asn1_value;
        if (!*pval)
            return;
    } else if (it->itype == ASN1_ITYPE_MSTRING) {
        utype = -1;
        if (!*pval)
            return;
    } else {
        utype = it->utype;
        if ((utype != V_ASN1_BOOLEAN) && !*pval)
            return;
    }

    switch (utype) {
    case V_ASN1_OBJECT:
        ASN1_OBJECT_free((ASN1_OBJECT *)*pval);
        break;

    case V_ASN1_BOOLEAN:
        if (it)
            *(ASN1_BOOLEAN *)pval = it->size;
        else
            *(ASN1_BOOLEAN *)pval = -1;
        return;

    case V_ASN1_NULL:
        break;

    case V_ASN1_ANY:
        asn1_primitive_free(pval, NULL, 0);
        OPENSSL_free(*pval);
        break;

    default:
        asn1_string_embed_free((ASN1_STRING *)*pval, embed);
        break;
    }
    *pval = NULL;
}
