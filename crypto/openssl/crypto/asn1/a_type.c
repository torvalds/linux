/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/asn1t.h>
#include <openssl/objects.h>
#include "asn1_locl.h"

int ASN1_TYPE_get(const ASN1_TYPE *a)
{
    if ((a->value.ptr != NULL) || (a->type == V_ASN1_NULL))
        return a->type;
    else
        return 0;
}

void ASN1_TYPE_set(ASN1_TYPE *a, int type, void *value)
{
    if (a->value.ptr != NULL) {
        ASN1_TYPE **tmp_a = &a;
        asn1_primitive_free((ASN1_VALUE **)tmp_a, NULL, 0);
    }
    a->type = type;
    if (type == V_ASN1_BOOLEAN)
        a->value.boolean = value ? 0xff : 0;
    else
        a->value.ptr = value;
}

int ASN1_TYPE_set1(ASN1_TYPE *a, int type, const void *value)
{
    if (!value || (type == V_ASN1_BOOLEAN)) {
        void *p = (void *)value;
        ASN1_TYPE_set(a, type, p);
    } else if (type == V_ASN1_OBJECT) {
        ASN1_OBJECT *odup;
        odup = OBJ_dup(value);
        if (!odup)
            return 0;
        ASN1_TYPE_set(a, type, odup);
    } else {
        ASN1_STRING *sdup;
        sdup = ASN1_STRING_dup(value);
        if (!sdup)
            return 0;
        ASN1_TYPE_set(a, type, sdup);
    }
    return 1;
}

/* Returns 0 if they are equal, != 0 otherwise. */
int ASN1_TYPE_cmp(const ASN1_TYPE *a, const ASN1_TYPE *b)
{
    int result = -1;

    if (!a || !b || a->type != b->type)
        return -1;

    switch (a->type) {
    case V_ASN1_OBJECT:
        result = OBJ_cmp(a->value.object, b->value.object);
        break;
    case V_ASN1_BOOLEAN:
        result = a->value.boolean - b->value.boolean;
        break;
    case V_ASN1_NULL:
        result = 0;             /* They do not have content. */
        break;
    case V_ASN1_INTEGER:
    case V_ASN1_ENUMERATED:
    case V_ASN1_BIT_STRING:
    case V_ASN1_OCTET_STRING:
    case V_ASN1_SEQUENCE:
    case V_ASN1_SET:
    case V_ASN1_NUMERICSTRING:
    case V_ASN1_PRINTABLESTRING:
    case V_ASN1_T61STRING:
    case V_ASN1_VIDEOTEXSTRING:
    case V_ASN1_IA5STRING:
    case V_ASN1_UTCTIME:
    case V_ASN1_GENERALIZEDTIME:
    case V_ASN1_GRAPHICSTRING:
    case V_ASN1_VISIBLESTRING:
    case V_ASN1_GENERALSTRING:
    case V_ASN1_UNIVERSALSTRING:
    case V_ASN1_BMPSTRING:
    case V_ASN1_UTF8STRING:
    case V_ASN1_OTHER:
    default:
        result = ASN1_STRING_cmp((ASN1_STRING *)a->value.ptr,
                                 (ASN1_STRING *)b->value.ptr);
        break;
    }

    return result;
}

ASN1_TYPE *ASN1_TYPE_pack_sequence(const ASN1_ITEM *it, void *s, ASN1_TYPE **t)
{
    ASN1_OCTET_STRING *oct;
    ASN1_TYPE *rt;

    oct = ASN1_item_pack(s, it, NULL);
    if (oct == NULL)
        return NULL;

    if (t && *t) {
        rt = *t;
    } else {
        rt = ASN1_TYPE_new();
        if (rt == NULL) {
            ASN1_OCTET_STRING_free(oct);
            return NULL;
        }
        if (t)
            *t = rt;
    }
    ASN1_TYPE_set(rt, V_ASN1_SEQUENCE, oct);
    return rt;
}

void *ASN1_TYPE_unpack_sequence(const ASN1_ITEM *it, const ASN1_TYPE *t)
{
    if (t == NULL || t->type != V_ASN1_SEQUENCE || t->value.sequence == NULL)
        return NULL;
    return ASN1_item_unpack(t->value.sequence, it);
}
