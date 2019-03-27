/*
 * Copyright 2000-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stddef.h>
#include <string.h>
#include "internal/cryptlib.h"
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/objects.h>
#include "internal/asn1_int.h"
#include "asn1_locl.h"

static int asn1_i2d_ex_primitive(ASN1_VALUE **pval, unsigned char **out,
                                 const ASN1_ITEM *it, int tag, int aclass);
static int asn1_set_seq_out(STACK_OF(ASN1_VALUE) *sk, unsigned char **out,
                            int skcontlen, const ASN1_ITEM *item,
                            int do_sort, int iclass);
static int asn1_template_ex_i2d(ASN1_VALUE **pval, unsigned char **out,
                                const ASN1_TEMPLATE *tt, int tag, int aclass);
static int asn1_item_flags_i2d(ASN1_VALUE *val, unsigned char **out,
                               const ASN1_ITEM *it, int flags);
static int asn1_ex_i2c(ASN1_VALUE **pval, unsigned char *cout, int *putype,
                       const ASN1_ITEM *it);

/*
 * Top level i2d equivalents: the 'ndef' variant instructs the encoder to use
 * indefinite length constructed encoding, where appropriate
 */

int ASN1_item_ndef_i2d(ASN1_VALUE *val, unsigned char **out,
                       const ASN1_ITEM *it)
{
    return asn1_item_flags_i2d(val, out, it, ASN1_TFLG_NDEF);
}

int ASN1_item_i2d(ASN1_VALUE *val, unsigned char **out, const ASN1_ITEM *it)
{
    return asn1_item_flags_i2d(val, out, it, 0);
}

/*
 * Encode an ASN1 item, this is use by the standard 'i2d' function. 'out'
 * points to a buffer to output the data to. The new i2d has one additional
 * feature. If the output buffer is NULL (i.e. *out == NULL) then a buffer is
 * allocated and populated with the encoding.
 */

static int asn1_item_flags_i2d(ASN1_VALUE *val, unsigned char **out,
                               const ASN1_ITEM *it, int flags)
{
    if (out && !*out) {
        unsigned char *p, *buf;
        int len;

        len = ASN1_item_ex_i2d(&val, NULL, it, -1, flags);
        if (len <= 0)
            return len;
        if ((buf = OPENSSL_malloc(len)) == NULL) {
            ASN1err(ASN1_F_ASN1_ITEM_FLAGS_I2D, ERR_R_MALLOC_FAILURE);
            return -1;
        }
        p = buf;
        ASN1_item_ex_i2d(&val, &p, it, -1, flags);
        *out = buf;
        return len;
    }

    return ASN1_item_ex_i2d(&val, out, it, -1, flags);
}

/*
 * Encode an item, taking care of IMPLICIT tagging (if any). This function
 * performs the normal item handling: it can be used in external types.
 */

int ASN1_item_ex_i2d(ASN1_VALUE **pval, unsigned char **out,
                     const ASN1_ITEM *it, int tag, int aclass)
{
    const ASN1_TEMPLATE *tt = NULL;
    int i, seqcontlen, seqlen, ndef = 1;
    const ASN1_EXTERN_FUNCS *ef;
    const ASN1_AUX *aux = it->funcs;
    ASN1_aux_cb *asn1_cb = 0;

    if ((it->itype != ASN1_ITYPE_PRIMITIVE) && !*pval)
        return 0;

    if (aux && aux->asn1_cb)
        asn1_cb = aux->asn1_cb;

    switch (it->itype) {

    case ASN1_ITYPE_PRIMITIVE:
        if (it->templates)
            return asn1_template_ex_i2d(pval, out, it->templates,
                                        tag, aclass);
        return asn1_i2d_ex_primitive(pval, out, it, tag, aclass);

    case ASN1_ITYPE_MSTRING:
        return asn1_i2d_ex_primitive(pval, out, it, -1, aclass);

    case ASN1_ITYPE_CHOICE:
        if (asn1_cb && !asn1_cb(ASN1_OP_I2D_PRE, pval, it, NULL))
            return 0;
        i = asn1_get_choice_selector(pval, it);
        if ((i >= 0) && (i < it->tcount)) {
            ASN1_VALUE **pchval;
            const ASN1_TEMPLATE *chtt;
            chtt = it->templates + i;
            pchval = asn1_get_field_ptr(pval, chtt);
            return asn1_template_ex_i2d(pchval, out, chtt, -1, aclass);
        }
        /* Fixme: error condition if selector out of range */
        if (asn1_cb && !asn1_cb(ASN1_OP_I2D_POST, pval, it, NULL))
            return 0;
        break;

    case ASN1_ITYPE_EXTERN:
        /* If new style i2d it does all the work */
        ef = it->funcs;
        return ef->asn1_ex_i2d(pval, out, it, tag, aclass);

    case ASN1_ITYPE_NDEF_SEQUENCE:
        /* Use indefinite length constructed if requested */
        if (aclass & ASN1_TFLG_NDEF)
            ndef = 2;
        /* fall through */

    case ASN1_ITYPE_SEQUENCE:
        i = asn1_enc_restore(&seqcontlen, out, pval, it);
        /* An error occurred */
        if (i < 0)
            return 0;
        /* We have a valid cached encoding... */
        if (i > 0)
            return seqcontlen;
        /* Otherwise carry on */
        seqcontlen = 0;
        /* If no IMPLICIT tagging set to SEQUENCE, UNIVERSAL */
        if (tag == -1) {
            tag = V_ASN1_SEQUENCE;
            /* Retain any other flags in aclass */
            aclass = (aclass & ~ASN1_TFLG_TAG_CLASS)
                | V_ASN1_UNIVERSAL;
        }
        if (asn1_cb && !asn1_cb(ASN1_OP_I2D_PRE, pval, it, NULL))
            return 0;
        /* First work out sequence content length */
        for (i = 0, tt = it->templates; i < it->tcount; tt++, i++) {
            const ASN1_TEMPLATE *seqtt;
            ASN1_VALUE **pseqval;
            int tmplen;
            seqtt = asn1_do_adb(pval, tt, 1);
            if (!seqtt)
                return 0;
            pseqval = asn1_get_field_ptr(pval, seqtt);
            tmplen = asn1_template_ex_i2d(pseqval, NULL, seqtt, -1, aclass);
            if (tmplen == -1 || (tmplen > INT_MAX - seqcontlen))
                return -1;
            seqcontlen += tmplen;
        }

        seqlen = ASN1_object_size(ndef, seqcontlen, tag);
        if (!out || seqlen == -1)
            return seqlen;
        /* Output SEQUENCE header */
        ASN1_put_object(out, ndef, seqcontlen, tag, aclass);
        for (i = 0, tt = it->templates; i < it->tcount; tt++, i++) {
            const ASN1_TEMPLATE *seqtt;
            ASN1_VALUE **pseqval;
            seqtt = asn1_do_adb(pval, tt, 1);
            if (!seqtt)
                return 0;
            pseqval = asn1_get_field_ptr(pval, seqtt);
            /* FIXME: check for errors in enhanced version */
            asn1_template_ex_i2d(pseqval, out, seqtt, -1, aclass);
        }
        if (ndef == 2)
            ASN1_put_eoc(out);
        if (asn1_cb && !asn1_cb(ASN1_OP_I2D_POST, pval, it, NULL))
            return 0;
        return seqlen;

    default:
        return 0;

    }
    return 0;
}

static int asn1_template_ex_i2d(ASN1_VALUE **pval, unsigned char **out,
                                const ASN1_TEMPLATE *tt, int tag, int iclass)
{
    int i, ret, flags, ttag, tclass, ndef;
    ASN1_VALUE *tval;
    flags = tt->flags;

    /*
     * If field is embedded then val needs fixing so it is a pointer to
     * a pointer to a field.
     */
    if (flags & ASN1_TFLG_EMBED) {
        tval = (ASN1_VALUE *)pval;
        pval = &tval;
    }
    /*
     * Work out tag and class to use: tagging may come either from the
     * template or the arguments, not both because this would create
     * ambiguity. Additionally the iclass argument may contain some
     * additional flags which should be noted and passed down to other
     * levels.
     */
    if (flags & ASN1_TFLG_TAG_MASK) {
        /* Error if argument and template tagging */
        if (tag != -1)
            /* FIXME: error code here */
            return -1;
        /* Get tagging from template */
        ttag = tt->tag;
        tclass = flags & ASN1_TFLG_TAG_CLASS;
    } else if (tag != -1) {
        /* No template tagging, get from arguments */
        ttag = tag;
        tclass = iclass & ASN1_TFLG_TAG_CLASS;
    } else {
        ttag = -1;
        tclass = 0;
    }
    /*
     * Remove any class mask from iflag.
     */
    iclass &= ~ASN1_TFLG_TAG_CLASS;

    /*
     * At this point 'ttag' contains the outer tag to use, 'tclass' is the
     * class and iclass is any flags passed to this function.
     */

    /* if template and arguments require ndef, use it */
    if ((flags & ASN1_TFLG_NDEF) && (iclass & ASN1_TFLG_NDEF))
        ndef = 2;
    else
        ndef = 1;

    if (flags & ASN1_TFLG_SK_MASK) {
        /* SET OF, SEQUENCE OF */
        STACK_OF(ASN1_VALUE) *sk = (STACK_OF(ASN1_VALUE) *)*pval;
        int isset, sktag, skaclass;
        int skcontlen, sklen;
        ASN1_VALUE *skitem;

        if (!*pval)
            return 0;

        if (flags & ASN1_TFLG_SET_OF) {
            isset = 1;
            /* 2 means we reorder */
            if (flags & ASN1_TFLG_SEQUENCE_OF)
                isset = 2;
        } else
            isset = 0;

        /*
         * Work out inner tag value: if EXPLICIT or no tagging use underlying
         * type.
         */
        if ((ttag != -1) && !(flags & ASN1_TFLG_EXPTAG)) {
            sktag = ttag;
            skaclass = tclass;
        } else {
            skaclass = V_ASN1_UNIVERSAL;
            if (isset)
                sktag = V_ASN1_SET;
            else
                sktag = V_ASN1_SEQUENCE;
        }

        /* Determine total length of items */
        skcontlen = 0;
        for (i = 0; i < sk_ASN1_VALUE_num(sk); i++) {
            int tmplen;
            skitem = sk_ASN1_VALUE_value(sk, i);
            tmplen = ASN1_item_ex_i2d(&skitem, NULL, ASN1_ITEM_ptr(tt->item),
                                      -1, iclass);
            if (tmplen == -1 || (skcontlen > INT_MAX - tmplen))
                return -1;
            skcontlen += tmplen;
        }
        sklen = ASN1_object_size(ndef, skcontlen, sktag);
        if (sklen == -1)
            return -1;
        /* If EXPLICIT need length of surrounding tag */
        if (flags & ASN1_TFLG_EXPTAG)
            ret = ASN1_object_size(ndef, sklen, ttag);
        else
            ret = sklen;

        if (!out || ret == -1)
            return ret;

        /* Now encode this lot... */
        /* EXPLICIT tag */
        if (flags & ASN1_TFLG_EXPTAG)
            ASN1_put_object(out, ndef, sklen, ttag, tclass);
        /* SET or SEQUENCE and IMPLICIT tag */
        ASN1_put_object(out, ndef, skcontlen, sktag, skaclass);
        /* And the stuff itself */
        asn1_set_seq_out(sk, out, skcontlen, ASN1_ITEM_ptr(tt->item),
                         isset, iclass);
        if (ndef == 2) {
            ASN1_put_eoc(out);
            if (flags & ASN1_TFLG_EXPTAG)
                ASN1_put_eoc(out);
        }

        return ret;
    }

    if (flags & ASN1_TFLG_EXPTAG) {
        /* EXPLICIT tagging */
        /* Find length of tagged item */
        i = ASN1_item_ex_i2d(pval, NULL, ASN1_ITEM_ptr(tt->item), -1, iclass);
        if (!i)
            return 0;
        /* Find length of EXPLICIT tag */
        ret = ASN1_object_size(ndef, i, ttag);
        if (out && ret != -1) {
            /* Output tag and item */
            ASN1_put_object(out, ndef, i, ttag, tclass);
            ASN1_item_ex_i2d(pval, out, ASN1_ITEM_ptr(tt->item), -1, iclass);
            if (ndef == 2)
                ASN1_put_eoc(out);
        }
        return ret;
    }

    /* Either normal or IMPLICIT tagging: combine class and flags */
    return ASN1_item_ex_i2d(pval, out, ASN1_ITEM_ptr(tt->item),
                            ttag, tclass | iclass);

}

/* Temporary structure used to hold DER encoding of items for SET OF */

typedef struct {
    unsigned char *data;
    int length;
    ASN1_VALUE *field;
} DER_ENC;

static int der_cmp(const void *a, const void *b)
{
    const DER_ENC *d1 = a, *d2 = b;
    int cmplen, i;
    cmplen = (d1->length < d2->length) ? d1->length : d2->length;
    i = memcmp(d1->data, d2->data, cmplen);
    if (i)
        return i;
    return d1->length - d2->length;
}

/* Output the content octets of SET OF or SEQUENCE OF */

static int asn1_set_seq_out(STACK_OF(ASN1_VALUE) *sk, unsigned char **out,
                            int skcontlen, const ASN1_ITEM *item,
                            int do_sort, int iclass)
{
    int i;
    ASN1_VALUE *skitem;
    unsigned char *tmpdat = NULL, *p = NULL;
    DER_ENC *derlst = NULL, *tder;
    if (do_sort) {
        /* Don't need to sort less than 2 items */
        if (sk_ASN1_VALUE_num(sk) < 2)
            do_sort = 0;
        else {
            derlst = OPENSSL_malloc(sk_ASN1_VALUE_num(sk)
                                    * sizeof(*derlst));
            if (derlst == NULL)
                return 0;
            tmpdat = OPENSSL_malloc(skcontlen);
            if (tmpdat == NULL) {
                OPENSSL_free(derlst);
                return 0;
            }
        }
    }
    /* If not sorting just output each item */
    if (!do_sort) {
        for (i = 0; i < sk_ASN1_VALUE_num(sk); i++) {
            skitem = sk_ASN1_VALUE_value(sk, i);
            ASN1_item_ex_i2d(&skitem, out, item, -1, iclass);
        }
        return 1;
    }
    p = tmpdat;

    /* Doing sort: build up a list of each member's DER encoding */
    for (i = 0, tder = derlst; i < sk_ASN1_VALUE_num(sk); i++, tder++) {
        skitem = sk_ASN1_VALUE_value(sk, i);
        tder->data = p;
        tder->length = ASN1_item_ex_i2d(&skitem, &p, item, -1, iclass);
        tder->field = skitem;
    }

    /* Now sort them */
    qsort(derlst, sk_ASN1_VALUE_num(sk), sizeof(*derlst), der_cmp);
    /* Output sorted DER encoding */
    p = *out;
    for (i = 0, tder = derlst; i < sk_ASN1_VALUE_num(sk); i++, tder++) {
        memcpy(p, tder->data, tder->length);
        p += tder->length;
    }
    *out = p;
    /* If do_sort is 2 then reorder the STACK */
    if (do_sort == 2) {
        for (i = 0, tder = derlst; i < sk_ASN1_VALUE_num(sk); i++, tder++)
            (void)sk_ASN1_VALUE_set(sk, i, tder->field);
    }
    OPENSSL_free(derlst);
    OPENSSL_free(tmpdat);
    return 1;
}

static int asn1_i2d_ex_primitive(ASN1_VALUE **pval, unsigned char **out,
                                 const ASN1_ITEM *it, int tag, int aclass)
{
    int len;
    int utype;
    int usetag;
    int ndef = 0;

    utype = it->utype;

    /*
     * Get length of content octets and maybe find out the underlying type.
     */

    len = asn1_ex_i2c(pval, NULL, &utype, it);

    /*
     * If SEQUENCE, SET or OTHER then header is included in pseudo content
     * octets so don't include tag+length. We need to check here because the
     * call to asn1_ex_i2c() could change utype.
     */
    if ((utype == V_ASN1_SEQUENCE) || (utype == V_ASN1_SET) ||
        (utype == V_ASN1_OTHER))
        usetag = 0;
    else
        usetag = 1;

    /* -1 means omit type */

    if (len == -1)
        return 0;

    /* -2 return is special meaning use ndef */
    if (len == -2) {
        ndef = 2;
        len = 0;
    }

    /* If not implicitly tagged get tag from underlying type */
    if (tag == -1)
        tag = utype;

    /* Output tag+length followed by content octets */
    if (out) {
        if (usetag)
            ASN1_put_object(out, ndef, len, tag, aclass);
        asn1_ex_i2c(pval, *out, &utype, it);
        if (ndef)
            ASN1_put_eoc(out);
        else
            *out += len;
    }

    if (usetag)
        return ASN1_object_size(ndef, len, tag);
    return len;
}

/* Produce content octets from a structure */

static int asn1_ex_i2c(ASN1_VALUE **pval, unsigned char *cout, int *putype,
                       const ASN1_ITEM *it)
{
    ASN1_BOOLEAN *tbool = NULL;
    ASN1_STRING *strtmp;
    ASN1_OBJECT *otmp;
    int utype;
    const unsigned char *cont;
    unsigned char c;
    int len;
    const ASN1_PRIMITIVE_FUNCS *pf;
    pf = it->funcs;
    if (pf && pf->prim_i2c)
        return pf->prim_i2c(pval, cout, putype, it);

    /* Should type be omitted? */
    if ((it->itype != ASN1_ITYPE_PRIMITIVE)
        || (it->utype != V_ASN1_BOOLEAN)) {
        if (!*pval)
            return -1;
    }

    if (it->itype == ASN1_ITYPE_MSTRING) {
        /* If MSTRING type set the underlying type */
        strtmp = (ASN1_STRING *)*pval;
        utype = strtmp->type;
        *putype = utype;
    } else if (it->utype == V_ASN1_ANY) {
        /* If ANY set type and pointer to value */
        ASN1_TYPE *typ;
        typ = (ASN1_TYPE *)*pval;
        utype = typ->type;
        *putype = utype;
        pval = &typ->value.asn1_value;
    } else
        utype = *putype;

    switch (utype) {
    case V_ASN1_OBJECT:
        otmp = (ASN1_OBJECT *)*pval;
        cont = otmp->data;
        len = otmp->length;
        if (cont == NULL || len == 0)
            return -1;
        break;

    case V_ASN1_NULL:
        cont = NULL;
        len = 0;
        break;

    case V_ASN1_BOOLEAN:
        tbool = (ASN1_BOOLEAN *)pval;
        if (*tbool == -1)
            return -1;
        if (it->utype != V_ASN1_ANY) {
            /*
             * Default handling if value == size field then omit
             */
            if (*tbool && (it->size > 0))
                return -1;
            if (!*tbool && !it->size)
                return -1;
        }
        c = (unsigned char)*tbool;
        cont = &c;
        len = 1;
        break;

    case V_ASN1_BIT_STRING:
        return i2c_ASN1_BIT_STRING((ASN1_BIT_STRING *)*pval,
                                   cout ? &cout : NULL);

    case V_ASN1_INTEGER:
    case V_ASN1_ENUMERATED:
        /*
         * These are all have the same content format as ASN1_INTEGER
         */
        return i2c_ASN1_INTEGER((ASN1_INTEGER *)*pval, cout ? &cout : NULL);

    case V_ASN1_OCTET_STRING:
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
    case V_ASN1_SEQUENCE:
    case V_ASN1_SET:
    default:
        /* All based on ASN1_STRING and handled the same */
        strtmp = (ASN1_STRING *)*pval;
        /* Special handling for NDEF */
        if ((it->size == ASN1_TFLG_NDEF)
            && (strtmp->flags & ASN1_STRING_FLAG_NDEF)) {
            if (cout) {
                strtmp->data = cout;
                strtmp->length = 0;
            }
            /* Special return code */
            return -2;
        }
        cont = strtmp->data;
        len = strtmp->length;

        break;

    }
    if (cout && len)
        memcpy(cout, cont, len);
    return len;
}
