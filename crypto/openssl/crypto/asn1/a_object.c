/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <limits.h>
#include "internal/ctype.h"
#include "internal/cryptlib.h"
#include <openssl/buffer.h>
#include <openssl/asn1.h>
#include <openssl/objects.h>
#include <openssl/bn.h>
#include "internal/asn1_int.h"
#include "asn1_locl.h"

int i2d_ASN1_OBJECT(const ASN1_OBJECT *a, unsigned char **pp)
{
    unsigned char *p, *allocated = NULL;
    int objsize;

    if ((a == NULL) || (a->data == NULL))
        return 0;

    objsize = ASN1_object_size(0, a->length, V_ASN1_OBJECT);
    if (pp == NULL || objsize == -1)
        return objsize;

    if (*pp == NULL) {
        if ((p = allocated = OPENSSL_malloc(objsize)) == NULL) {
            ASN1err(ASN1_F_I2D_ASN1_OBJECT, ERR_R_MALLOC_FAILURE);
            return 0;
        }
    } else {
        p = *pp;
    }

    ASN1_put_object(&p, 0, a->length, V_ASN1_OBJECT, V_ASN1_UNIVERSAL);
    memcpy(p, a->data, a->length);

    /*
     * If a new buffer was allocated, just return it back.
     * If not, return the incremented buffer pointer.
     */
    *pp = allocated != NULL ? allocated : p + a->length;
    return objsize;
}

int a2d_ASN1_OBJECT(unsigned char *out, int olen, const char *buf, int num)
{
    int i, first, len = 0, c, use_bn;
    char ftmp[24], *tmp = ftmp;
    int tmpsize = sizeof(ftmp);
    const char *p;
    unsigned long l;
    BIGNUM *bl = NULL;

    if (num == 0)
        return 0;
    else if (num == -1)
        num = strlen(buf);

    p = buf;
    c = *(p++);
    num--;
    if ((c >= '0') && (c <= '2')) {
        first = c - '0';
    } else {
        ASN1err(ASN1_F_A2D_ASN1_OBJECT, ASN1_R_FIRST_NUM_TOO_LARGE);
        goto err;
    }

    if (num <= 0) {
        ASN1err(ASN1_F_A2D_ASN1_OBJECT, ASN1_R_MISSING_SECOND_NUMBER);
        goto err;
    }
    c = *(p++);
    num--;
    for (;;) {
        if (num <= 0)
            break;
        if ((c != '.') && (c != ' ')) {
            ASN1err(ASN1_F_A2D_ASN1_OBJECT, ASN1_R_INVALID_SEPARATOR);
            goto err;
        }
        l = 0;
        use_bn = 0;
        for (;;) {
            if (num <= 0)
                break;
            num--;
            c = *(p++);
            if ((c == ' ') || (c == '.'))
                break;
            if (!ossl_isdigit(c)) {
                ASN1err(ASN1_F_A2D_ASN1_OBJECT, ASN1_R_INVALID_DIGIT);
                goto err;
            }
            if (!use_bn && l >= ((ULONG_MAX - 80) / 10L)) {
                use_bn = 1;
                if (bl == NULL)
                    bl = BN_new();
                if (bl == NULL || !BN_set_word(bl, l))
                    goto err;
            }
            if (use_bn) {
                if (!BN_mul_word(bl, 10L)
                    || !BN_add_word(bl, c - '0'))
                    goto err;
            } else
                l = l * 10L + (long)(c - '0');
        }
        if (len == 0) {
            if ((first < 2) && (l >= 40)) {
                ASN1err(ASN1_F_A2D_ASN1_OBJECT,
                        ASN1_R_SECOND_NUMBER_TOO_LARGE);
                goto err;
            }
            if (use_bn) {
                if (!BN_add_word(bl, first * 40))
                    goto err;
            } else
                l += (long)first *40;
        }
        i = 0;
        if (use_bn) {
            int blsize;
            blsize = BN_num_bits(bl);
            blsize = (blsize + 6) / 7;
            if (blsize > tmpsize) {
                if (tmp != ftmp)
                    OPENSSL_free(tmp);
                tmpsize = blsize + 32;
                tmp = OPENSSL_malloc(tmpsize);
                if (tmp == NULL)
                    goto err;
            }
            while (blsize--) {
                BN_ULONG t = BN_div_word(bl, 0x80L);
                if (t == (BN_ULONG)-1)
                    goto err;
                tmp[i++] = (unsigned char)t;
            }
        } else {

            for (;;) {
                tmp[i++] = (unsigned char)l & 0x7f;
                l >>= 7L;
                if (l == 0L)
                    break;
            }

        }
        if (out != NULL) {
            if (len + i > olen) {
                ASN1err(ASN1_F_A2D_ASN1_OBJECT, ASN1_R_BUFFER_TOO_SMALL);
                goto err;
            }
            while (--i > 0)
                out[len++] = tmp[i] | 0x80;
            out[len++] = tmp[0];
        } else
            len += i;
    }
    if (tmp != ftmp)
        OPENSSL_free(tmp);
    BN_free(bl);
    return len;
 err:
    if (tmp != ftmp)
        OPENSSL_free(tmp);
    BN_free(bl);
    return 0;
}

int i2t_ASN1_OBJECT(char *buf, int buf_len, const ASN1_OBJECT *a)
{
    return OBJ_obj2txt(buf, buf_len, a, 0);
}

int i2a_ASN1_OBJECT(BIO *bp, const ASN1_OBJECT *a)
{
    char buf[80], *p = buf;
    int i;

    if ((a == NULL) || (a->data == NULL))
        return BIO_write(bp, "NULL", 4);
    i = i2t_ASN1_OBJECT(buf, sizeof(buf), a);
    if (i > (int)(sizeof(buf) - 1)) {
        if ((p = OPENSSL_malloc(i + 1)) == NULL) {
            ASN1err(ASN1_F_I2A_ASN1_OBJECT, ERR_R_MALLOC_FAILURE);
            return -1;
        }
        i2t_ASN1_OBJECT(p, i + 1, a);
    }
    if (i <= 0) {
        i = BIO_write(bp, "<INVALID>", 9);
        i += BIO_dump(bp, (const char *)a->data, a->length);
        return i;
    }
    BIO_write(bp, p, i);
    if (p != buf)
        OPENSSL_free(p);
    return i;
}

ASN1_OBJECT *d2i_ASN1_OBJECT(ASN1_OBJECT **a, const unsigned char **pp,
                             long length)
{
    const unsigned char *p;
    long len;
    int tag, xclass;
    int inf, i;
    ASN1_OBJECT *ret = NULL;
    p = *pp;
    inf = ASN1_get_object(&p, &len, &tag, &xclass, length);
    if (inf & 0x80) {
        i = ASN1_R_BAD_OBJECT_HEADER;
        goto err;
    }

    if (tag != V_ASN1_OBJECT) {
        i = ASN1_R_EXPECTING_AN_OBJECT;
        goto err;
    }
    ret = c2i_ASN1_OBJECT(a, &p, len);
    if (ret)
        *pp = p;
    return ret;
 err:
    ASN1err(ASN1_F_D2I_ASN1_OBJECT, i);
    return NULL;
}

ASN1_OBJECT *c2i_ASN1_OBJECT(ASN1_OBJECT **a, const unsigned char **pp,
                             long len)
{
    ASN1_OBJECT *ret = NULL, tobj;
    const unsigned char *p;
    unsigned char *data;
    int i, length;

    /*
     * Sanity check OID encoding. Need at least one content octet. MSB must
     * be clear in the last octet. can't have leading 0x80 in subidentifiers,
     * see: X.690 8.19.2
     */
    if (len <= 0 || len > INT_MAX || pp == NULL || (p = *pp) == NULL ||
        p[len - 1] & 0x80) {
        ASN1err(ASN1_F_C2I_ASN1_OBJECT, ASN1_R_INVALID_OBJECT_ENCODING);
        return NULL;
    }
    /* Now 0 < len <= INT_MAX, so the cast is safe. */
    length = (int)len;
    /*
     * Try to lookup OID in table: these are all valid encodings so if we get
     * a match we know the OID is valid.
     */
    tobj.nid = NID_undef;
    tobj.data = p;
    tobj.length = length;
    tobj.flags = 0;
    i = OBJ_obj2nid(&tobj);
    if (i != NID_undef) {
        /*
         * Return shared registered OID object: this improves efficiency
         * because we don't have to return a dynamically allocated OID
         * and NID lookups can use the cached value.
         */
        ret = OBJ_nid2obj(i);
        if (a) {
            ASN1_OBJECT_free(*a);
            *a = ret;
        }
        *pp += len;
        return ret;
    }
    for (i = 0; i < length; i++, p++) {
        if (*p == 0x80 && (!i || !(p[-1] & 0x80))) {
            ASN1err(ASN1_F_C2I_ASN1_OBJECT, ASN1_R_INVALID_OBJECT_ENCODING);
            return NULL;
        }
    }

    /*
     * only the ASN1_OBJECTs from the 'table' will have values for ->sn or
     * ->ln
     */
    if ((a == NULL) || ((*a) == NULL) ||
        !((*a)->flags & ASN1_OBJECT_FLAG_DYNAMIC)) {
        if ((ret = ASN1_OBJECT_new()) == NULL)
            return NULL;
    } else
        ret = (*a);

    p = *pp;
    /* detach data from object */
    data = (unsigned char *)ret->data;
    ret->data = NULL;
    /* once detached we can change it */
    if ((data == NULL) || (ret->length < length)) {
        ret->length = 0;
        OPENSSL_free(data);
        data = OPENSSL_malloc(length);
        if (data == NULL) {
            i = ERR_R_MALLOC_FAILURE;
            goto err;
        }
        ret->flags |= ASN1_OBJECT_FLAG_DYNAMIC_DATA;
    }
    memcpy(data, p, length);
    /* reattach data to object, after which it remains const */
    ret->data = data;
    ret->length = length;
    ret->sn = NULL;
    ret->ln = NULL;
    /* ret->flags=ASN1_OBJECT_FLAG_DYNAMIC; we know it is dynamic */
    p += length;

    if (a != NULL)
        (*a) = ret;
    *pp = p;
    return ret;
 err:
    ASN1err(ASN1_F_C2I_ASN1_OBJECT, i);
    if ((a == NULL) || (*a != ret))
        ASN1_OBJECT_free(ret);
    return NULL;
}

ASN1_OBJECT *ASN1_OBJECT_new(void)
{
    ASN1_OBJECT *ret;

    ret = OPENSSL_zalloc(sizeof(*ret));
    if (ret == NULL) {
        ASN1err(ASN1_F_ASN1_OBJECT_NEW, ERR_R_MALLOC_FAILURE);
        return NULL;
    }
    ret->flags = ASN1_OBJECT_FLAG_DYNAMIC;
    return ret;
}

void ASN1_OBJECT_free(ASN1_OBJECT *a)
{
    if (a == NULL)
        return;
    if (a->flags & ASN1_OBJECT_FLAG_DYNAMIC_STRINGS) {
#ifndef CONST_STRICT            /* disable purely for compile-time strict
                                 * const checking. Doing this on a "real"
                                 * compile will cause memory leaks */
        OPENSSL_free((void*)a->sn);
        OPENSSL_free((void*)a->ln);
#endif
        a->sn = a->ln = NULL;
    }
    if (a->flags & ASN1_OBJECT_FLAG_DYNAMIC_DATA) {
        OPENSSL_free((void*)a->data);
        a->data = NULL;
        a->length = 0;
    }
    if (a->flags & ASN1_OBJECT_FLAG_DYNAMIC)
        OPENSSL_free(a);
}

ASN1_OBJECT *ASN1_OBJECT_create(int nid, unsigned char *data, int len,
                                const char *sn, const char *ln)
{
    ASN1_OBJECT o;

    o.sn = sn;
    o.ln = ln;
    o.data = data;
    o.nid = nid;
    o.length = len;
    o.flags = ASN1_OBJECT_FLAG_DYNAMIC | ASN1_OBJECT_FLAG_DYNAMIC_STRINGS |
        ASN1_OBJECT_FLAG_DYNAMIC_DATA;
    return OBJ_dup(&o);
}
