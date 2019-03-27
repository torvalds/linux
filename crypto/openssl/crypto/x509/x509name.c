/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/safestack.h>
#include <openssl/asn1.h>
#include <openssl/objects.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include "internal/x509_int.h"

int X509_NAME_get_text_by_NID(X509_NAME *name, int nid, char *buf, int len)
{
    ASN1_OBJECT *obj;

    obj = OBJ_nid2obj(nid);
    if (obj == NULL)
        return -1;
    return X509_NAME_get_text_by_OBJ(name, obj, buf, len);
}

int X509_NAME_get_text_by_OBJ(X509_NAME *name, const ASN1_OBJECT *obj,
                              char *buf, int len)
{
    int i;
    const ASN1_STRING *data;

    i = X509_NAME_get_index_by_OBJ(name, obj, -1);
    if (i < 0)
        return -1;
    data = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, i));
    if (buf == NULL)
        return data->length;
    if (len <= 0)
        return 0;
    i = (data->length > (len - 1)) ? (len - 1) : data->length;
    memcpy(buf, data->data, i);
    buf[i] = '\0';
    return i;
}

int X509_NAME_entry_count(const X509_NAME *name)
{
    if (name == NULL)
        return 0;
    return sk_X509_NAME_ENTRY_num(name->entries);
}

int X509_NAME_get_index_by_NID(X509_NAME *name, int nid, int lastpos)
{
    ASN1_OBJECT *obj;

    obj = OBJ_nid2obj(nid);
    if (obj == NULL)
        return -2;
    return X509_NAME_get_index_by_OBJ(name, obj, lastpos);
}

/* NOTE: you should be passing -1, not 0 as lastpos */
int X509_NAME_get_index_by_OBJ(X509_NAME *name, const ASN1_OBJECT *obj, int lastpos)
{
    int n;
    X509_NAME_ENTRY *ne;
    STACK_OF(X509_NAME_ENTRY) *sk;

    if (name == NULL)
        return -1;
    if (lastpos < 0)
        lastpos = -1;
    sk = name->entries;
    n = sk_X509_NAME_ENTRY_num(sk);
    for (lastpos++; lastpos < n; lastpos++) {
        ne = sk_X509_NAME_ENTRY_value(sk, lastpos);
        if (OBJ_cmp(ne->object, obj) == 0)
            return lastpos;
    }
    return -1;
}

X509_NAME_ENTRY *X509_NAME_get_entry(const X509_NAME *name, int loc)
{
    if (name == NULL || sk_X509_NAME_ENTRY_num(name->entries) <= loc
        || loc < 0)
        return NULL;

    return sk_X509_NAME_ENTRY_value(name->entries, loc);
}

X509_NAME_ENTRY *X509_NAME_delete_entry(X509_NAME *name, int loc)
{
    X509_NAME_ENTRY *ret;
    int i, n, set_prev, set_next;
    STACK_OF(X509_NAME_ENTRY) *sk;

    if (name == NULL || sk_X509_NAME_ENTRY_num(name->entries) <= loc
        || loc < 0)
        return NULL;

    sk = name->entries;
    ret = sk_X509_NAME_ENTRY_delete(sk, loc);
    n = sk_X509_NAME_ENTRY_num(sk);
    name->modified = 1;
    if (loc == n)
        return ret;

    /* else we need to fixup the set field */
    if (loc != 0)
        set_prev = (sk_X509_NAME_ENTRY_value(sk, loc - 1))->set;
    else
        set_prev = ret->set - 1;
    set_next = sk_X509_NAME_ENTRY_value(sk, loc)->set;

    /*-
     * set_prev is the previous set
     * set is the current set
     * set_next is the following
     * prev  1 1    1 1     1 1     1 1
     * set   1      1       2       2
     * next  1 1    2 2     2 2     3 2
     * so basically only if prev and next differ by 2, then
     * re-number down by 1
     */
    if (set_prev + 1 < set_next)
        for (i = loc; i < n; i++)
            sk_X509_NAME_ENTRY_value(sk, i)->set--;
    return ret;
}

int X509_NAME_add_entry_by_OBJ(X509_NAME *name, const ASN1_OBJECT *obj, int type,
                               const unsigned char *bytes, int len, int loc,
                               int set)
{
    X509_NAME_ENTRY *ne;
    int ret;

    ne = X509_NAME_ENTRY_create_by_OBJ(NULL, obj, type, bytes, len);
    if (!ne)
        return 0;
    ret = X509_NAME_add_entry(name, ne, loc, set);
    X509_NAME_ENTRY_free(ne);
    return ret;
}

int X509_NAME_add_entry_by_NID(X509_NAME *name, int nid, int type,
                               const unsigned char *bytes, int len, int loc,
                               int set)
{
    X509_NAME_ENTRY *ne;
    int ret;
    ne = X509_NAME_ENTRY_create_by_NID(NULL, nid, type, bytes, len);
    if (!ne)
        return 0;
    ret = X509_NAME_add_entry(name, ne, loc, set);
    X509_NAME_ENTRY_free(ne);
    return ret;
}

int X509_NAME_add_entry_by_txt(X509_NAME *name, const char *field, int type,
                               const unsigned char *bytes, int len, int loc,
                               int set)
{
    X509_NAME_ENTRY *ne;
    int ret;
    ne = X509_NAME_ENTRY_create_by_txt(NULL, field, type, bytes, len);
    if (!ne)
        return 0;
    ret = X509_NAME_add_entry(name, ne, loc, set);
    X509_NAME_ENTRY_free(ne);
    return ret;
}

/*
 * if set is -1, append to previous set, 0 'a new one', and 1, prepend to the
 * guy we are about to stomp on.
 */
int X509_NAME_add_entry(X509_NAME *name, const X509_NAME_ENTRY *ne, int loc,
                        int set)
{
    X509_NAME_ENTRY *new_name = NULL;
    int n, i, inc;
    STACK_OF(X509_NAME_ENTRY) *sk;

    if (name == NULL)
        return 0;
    sk = name->entries;
    n = sk_X509_NAME_ENTRY_num(sk);
    if (loc > n)
        loc = n;
    else if (loc < 0)
        loc = n;
    inc = (set == 0);
    name->modified = 1;

    if (set == -1) {
        if (loc == 0) {
            set = 0;
            inc = 1;
        } else {
            set = sk_X509_NAME_ENTRY_value(sk, loc - 1)->set;
        }
    } else {                    /* if (set >= 0) */

        if (loc >= n) {
            if (loc != 0)
                set = sk_X509_NAME_ENTRY_value(sk, loc - 1)->set + 1;
            else
                set = 0;
        } else
            set = sk_X509_NAME_ENTRY_value(sk, loc)->set;
    }

    /*
     * X509_NAME_ENTRY_dup is ASN1 generated code, that can't be easily
     * const'ified; harmless cast since dup() don't modify its input.
     */
    if ((new_name = X509_NAME_ENTRY_dup((X509_NAME_ENTRY *)ne)) == NULL)
        goto err;
    new_name->set = set;
    if (!sk_X509_NAME_ENTRY_insert(sk, new_name, loc)) {
        X509err(X509_F_X509_NAME_ADD_ENTRY, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    if (inc) {
        n = sk_X509_NAME_ENTRY_num(sk);
        for (i = loc + 1; i < n; i++)
            sk_X509_NAME_ENTRY_value(sk, i)->set += 1;
    }
    return 1;
 err:
    X509_NAME_ENTRY_free(new_name);
    return 0;
}

X509_NAME_ENTRY *X509_NAME_ENTRY_create_by_txt(X509_NAME_ENTRY **ne,
                                               const char *field, int type,
                                               const unsigned char *bytes,
                                               int len)
{
    ASN1_OBJECT *obj;
    X509_NAME_ENTRY *nentry;

    obj = OBJ_txt2obj(field, 0);
    if (obj == NULL) {
        X509err(X509_F_X509_NAME_ENTRY_CREATE_BY_TXT,
                X509_R_INVALID_FIELD_NAME);
        ERR_add_error_data(2, "name=", field);
        return NULL;
    }
    nentry = X509_NAME_ENTRY_create_by_OBJ(ne, obj, type, bytes, len);
    ASN1_OBJECT_free(obj);
    return nentry;
}

X509_NAME_ENTRY *X509_NAME_ENTRY_create_by_NID(X509_NAME_ENTRY **ne, int nid,
                                               int type,
                                               const unsigned char *bytes,
                                               int len)
{
    ASN1_OBJECT *obj;
    X509_NAME_ENTRY *nentry;

    obj = OBJ_nid2obj(nid);
    if (obj == NULL) {
        X509err(X509_F_X509_NAME_ENTRY_CREATE_BY_NID, X509_R_UNKNOWN_NID);
        return NULL;
    }
    nentry = X509_NAME_ENTRY_create_by_OBJ(ne, obj, type, bytes, len);
    ASN1_OBJECT_free(obj);
    return nentry;
}

X509_NAME_ENTRY *X509_NAME_ENTRY_create_by_OBJ(X509_NAME_ENTRY **ne,
                                               const ASN1_OBJECT *obj, int type,
                                               const unsigned char *bytes,
                                               int len)
{
    X509_NAME_ENTRY *ret;

    if ((ne == NULL) || (*ne == NULL)) {
        if ((ret = X509_NAME_ENTRY_new()) == NULL)
            return NULL;
    } else
        ret = *ne;

    if (!X509_NAME_ENTRY_set_object(ret, obj))
        goto err;
    if (!X509_NAME_ENTRY_set_data(ret, type, bytes, len))
        goto err;

    if ((ne != NULL) && (*ne == NULL))
        *ne = ret;
    return ret;
 err:
    if ((ne == NULL) || (ret != *ne))
        X509_NAME_ENTRY_free(ret);
    return NULL;
}

int X509_NAME_ENTRY_set_object(X509_NAME_ENTRY *ne, const ASN1_OBJECT *obj)
{
    if ((ne == NULL) || (obj == NULL)) {
        X509err(X509_F_X509_NAME_ENTRY_SET_OBJECT,
                ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    ASN1_OBJECT_free(ne->object);
    ne->object = OBJ_dup(obj);
    return ((ne->object == NULL) ? 0 : 1);
}

int X509_NAME_ENTRY_set_data(X509_NAME_ENTRY *ne, int type,
                             const unsigned char *bytes, int len)
{
    int i;

    if ((ne == NULL) || ((bytes == NULL) && (len != 0)))
        return 0;
    if ((type > 0) && (type & MBSTRING_FLAG))
        return ASN1_STRING_set_by_NID(&ne->value, bytes,
                                      len, type,
                                      OBJ_obj2nid(ne->object)) ? 1 : 0;
    if (len < 0)
        len = strlen((const char *)bytes);
    i = ASN1_STRING_set(ne->value, bytes, len);
    if (!i)
        return 0;
    if (type != V_ASN1_UNDEF) {
        if (type == V_ASN1_APP_CHOOSE)
            ne->value->type = ASN1_PRINTABLE_type(bytes, len);
        else
            ne->value->type = type;
    }
    return 1;
}

ASN1_OBJECT *X509_NAME_ENTRY_get_object(const X509_NAME_ENTRY *ne)
{
    if (ne == NULL)
        return NULL;
    return ne->object;
}

ASN1_STRING *X509_NAME_ENTRY_get_data(const X509_NAME_ENTRY *ne)
{
    if (ne == NULL)
        return NULL;
    return ne->value;
}

int X509_NAME_ENTRY_set(const X509_NAME_ENTRY *ne)
{
    return ne->set;
}
