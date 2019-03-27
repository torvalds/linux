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
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/objects.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include "internal/numbers.h"
#include "asn1_locl.h"


/*
 * Constructed types with a recursive definition (such as can be found in PKCS7)
 * could eventually exceed the stack given malicious input with excessive
 * recursion. Therefore we limit the stack depth. This is the maximum number of
 * recursive invocations of asn1_item_embed_d2i().
 */
#define ASN1_MAX_CONSTRUCTED_NEST 30

static int asn1_item_embed_d2i(ASN1_VALUE **pval, const unsigned char **in,
                               long len, const ASN1_ITEM *it,
                               int tag, int aclass, char opt, ASN1_TLC *ctx,
                               int depth);

static int asn1_check_eoc(const unsigned char **in, long len);
static int asn1_find_end(const unsigned char **in, long len, char inf);

static int asn1_collect(BUF_MEM *buf, const unsigned char **in, long len,
                        char inf, int tag, int aclass, int depth);

static int collect_data(BUF_MEM *buf, const unsigned char **p, long plen);

static int asn1_check_tlen(long *olen, int *otag, unsigned char *oclass,
                           char *inf, char *cst,
                           const unsigned char **in, long len,
                           int exptag, int expclass, char opt, ASN1_TLC *ctx);

static int asn1_template_ex_d2i(ASN1_VALUE **pval,
                                const unsigned char **in, long len,
                                const ASN1_TEMPLATE *tt, char opt,
                                ASN1_TLC *ctx, int depth);
static int asn1_template_noexp_d2i(ASN1_VALUE **val,
                                   const unsigned char **in, long len,
                                   const ASN1_TEMPLATE *tt, char opt,
                                   ASN1_TLC *ctx, int depth);
static int asn1_d2i_ex_primitive(ASN1_VALUE **pval,
                                 const unsigned char **in, long len,
                                 const ASN1_ITEM *it,
                                 int tag, int aclass, char opt,
                                 ASN1_TLC *ctx);
static int asn1_ex_c2i(ASN1_VALUE **pval, const unsigned char *cont, int len,
                       int utype, char *free_cont, const ASN1_ITEM *it);

/* Table to convert tags to bit values, used for MSTRING type */
static const unsigned long tag2bit[32] = {
    /* tags  0 -  3 */
    0, 0, 0, B_ASN1_BIT_STRING,
    /* tags  4- 7 */
    B_ASN1_OCTET_STRING, 0, 0, B_ASN1_UNKNOWN,
    /* tags  8-11 */
    B_ASN1_UNKNOWN, B_ASN1_UNKNOWN, B_ASN1_UNKNOWN, B_ASN1_UNKNOWN,
    /* tags 12-15 */
    B_ASN1_UTF8STRING, B_ASN1_UNKNOWN, B_ASN1_UNKNOWN, B_ASN1_UNKNOWN,
    /* tags 16-19 */
    B_ASN1_SEQUENCE, 0, B_ASN1_NUMERICSTRING, B_ASN1_PRINTABLESTRING,
    /* tags 20-22 */
    B_ASN1_T61STRING, B_ASN1_VIDEOTEXSTRING, B_ASN1_IA5STRING,
    /* tags 23-24 */
    B_ASN1_UTCTIME, B_ASN1_GENERALIZEDTIME,
    /* tags 25-27 */
    B_ASN1_GRAPHICSTRING, B_ASN1_ISO64STRING, B_ASN1_GENERALSTRING,
    /* tags 28-31 */
    B_ASN1_UNIVERSALSTRING, B_ASN1_UNKNOWN, B_ASN1_BMPSTRING, B_ASN1_UNKNOWN,
};

unsigned long ASN1_tag2bit(int tag)
{
    if ((tag < 0) || (tag > 30))
        return 0;
    return tag2bit[tag];
}

/* Macro to initialize and invalidate the cache */

#define asn1_tlc_clear(c)       if (c) (c)->valid = 0
/* Version to avoid compiler warning about 'c' always non-NULL */
#define asn1_tlc_clear_nc(c)    (c)->valid = 0

/*
 * Decode an ASN1 item, this currently behaves just like a standard 'd2i'
 * function. 'in' points to a buffer to read the data from, in future we
 * will have more advanced versions that can input data a piece at a time and
 * this will simply be a special case.
 */

ASN1_VALUE *ASN1_item_d2i(ASN1_VALUE **pval,
                          const unsigned char **in, long len,
                          const ASN1_ITEM *it)
{
    ASN1_TLC c;
    ASN1_VALUE *ptmpval = NULL;
    if (!pval)
        pval = &ptmpval;
    asn1_tlc_clear_nc(&c);
    if (ASN1_item_ex_d2i(pval, in, len, it, -1, 0, 0, &c) > 0)
        return *pval;
    return NULL;
}

int ASN1_item_ex_d2i(ASN1_VALUE **pval, const unsigned char **in, long len,
                     const ASN1_ITEM *it,
                     int tag, int aclass, char opt, ASN1_TLC *ctx)
{
    int rv;
    rv = asn1_item_embed_d2i(pval, in, len, it, tag, aclass, opt, ctx, 0);
    if (rv <= 0)
        ASN1_item_ex_free(pval, it);
    return rv;
}

/*
 * Decode an item, taking care of IMPLICIT tagging, if any. If 'opt' set and
 * tag mismatch return -1 to handle OPTIONAL
 */

static int asn1_item_embed_d2i(ASN1_VALUE **pval, const unsigned char **in,
                               long len, const ASN1_ITEM *it,
                               int tag, int aclass, char opt, ASN1_TLC *ctx,
                               int depth)
{
    const ASN1_TEMPLATE *tt, *errtt = NULL;
    const ASN1_EXTERN_FUNCS *ef;
    const ASN1_AUX *aux = it->funcs;
    ASN1_aux_cb *asn1_cb;
    const unsigned char *p = NULL, *q;
    unsigned char oclass;
    char seq_eoc, seq_nolen, cst, isopt;
    long tmplen;
    int i;
    int otag;
    int ret = 0;
    ASN1_VALUE **pchptr;
    if (!pval)
        return 0;
    if (aux && aux->asn1_cb)
        asn1_cb = aux->asn1_cb;
    else
        asn1_cb = 0;

    if (++depth > ASN1_MAX_CONSTRUCTED_NEST) {
        ASN1err(ASN1_F_ASN1_ITEM_EMBED_D2I, ASN1_R_NESTED_TOO_DEEP);
        goto err;
    }

    switch (it->itype) {
    case ASN1_ITYPE_PRIMITIVE:
        if (it->templates) {
            /*
             * tagging or OPTIONAL is currently illegal on an item template
             * because the flags can't get passed down. In practice this
             * isn't a problem: we include the relevant flags from the item
             * template in the template itself.
             */
            if ((tag != -1) || opt) {
                ASN1err(ASN1_F_ASN1_ITEM_EMBED_D2I,
                        ASN1_R_ILLEGAL_OPTIONS_ON_ITEM_TEMPLATE);
                goto err;
            }
            return asn1_template_ex_d2i(pval, in, len,
                                        it->templates, opt, ctx, depth);
        }
        return asn1_d2i_ex_primitive(pval, in, len, it,
                                     tag, aclass, opt, ctx);

    case ASN1_ITYPE_MSTRING:
        p = *in;
        /* Just read in tag and class */
        ret = asn1_check_tlen(NULL, &otag, &oclass, NULL, NULL,
                              &p, len, -1, 0, 1, ctx);
        if (!ret) {
            ASN1err(ASN1_F_ASN1_ITEM_EMBED_D2I, ERR_R_NESTED_ASN1_ERROR);
            goto err;
        }

        /* Must be UNIVERSAL class */
        if (oclass != V_ASN1_UNIVERSAL) {
            /* If OPTIONAL, assume this is OK */
            if (opt)
                return -1;
            ASN1err(ASN1_F_ASN1_ITEM_EMBED_D2I, ASN1_R_MSTRING_NOT_UNIVERSAL);
            goto err;
        }
        /* Check tag matches bit map */
        if (!(ASN1_tag2bit(otag) & it->utype)) {
            /* If OPTIONAL, assume this is OK */
            if (opt)
                return -1;
            ASN1err(ASN1_F_ASN1_ITEM_EMBED_D2I, ASN1_R_MSTRING_WRONG_TAG);
            goto err;
        }
        return asn1_d2i_ex_primitive(pval, in, len, it, otag, 0, 0, ctx);

    case ASN1_ITYPE_EXTERN:
        /* Use new style d2i */
        ef = it->funcs;
        return ef->asn1_ex_d2i(pval, in, len, it, tag, aclass, opt, ctx);

    case ASN1_ITYPE_CHOICE:
        if (asn1_cb && !asn1_cb(ASN1_OP_D2I_PRE, pval, it, NULL))
            goto auxerr;
        if (*pval) {
            /* Free up and zero CHOICE value if initialised */
            i = asn1_get_choice_selector(pval, it);
            if ((i >= 0) && (i < it->tcount)) {
                tt = it->templates + i;
                pchptr = asn1_get_field_ptr(pval, tt);
                asn1_template_free(pchptr, tt);
                asn1_set_choice_selector(pval, -1, it);
            }
        } else if (!ASN1_item_ex_new(pval, it)) {
            ASN1err(ASN1_F_ASN1_ITEM_EMBED_D2I, ERR_R_NESTED_ASN1_ERROR);
            goto err;
        }
        /* CHOICE type, try each possibility in turn */
        p = *in;
        for (i = 0, tt = it->templates; i < it->tcount; i++, tt++) {
            pchptr = asn1_get_field_ptr(pval, tt);
            /*
             * We mark field as OPTIONAL so its absence can be recognised.
             */
            ret = asn1_template_ex_d2i(pchptr, &p, len, tt, 1, ctx, depth);
            /* If field not present, try the next one */
            if (ret == -1)
                continue;
            /* If positive return, read OK, break loop */
            if (ret > 0)
                break;
            /*
             * Must be an ASN1 parsing error.
             * Free up any partial choice value
             */
            asn1_template_free(pchptr, tt);
            errtt = tt;
            ASN1err(ASN1_F_ASN1_ITEM_EMBED_D2I, ERR_R_NESTED_ASN1_ERROR);
            goto err;
        }

        /* Did we fall off the end without reading anything? */
        if (i == it->tcount) {
            /* If OPTIONAL, this is OK */
            if (opt) {
                /* Free and zero it */
                ASN1_item_ex_free(pval, it);
                return -1;
            }
            ASN1err(ASN1_F_ASN1_ITEM_EMBED_D2I, ASN1_R_NO_MATCHING_CHOICE_TYPE);
            goto err;
        }

        asn1_set_choice_selector(pval, i, it);

        if (asn1_cb && !asn1_cb(ASN1_OP_D2I_POST, pval, it, NULL))
            goto auxerr;
        *in = p;
        return 1;

    case ASN1_ITYPE_NDEF_SEQUENCE:
    case ASN1_ITYPE_SEQUENCE:
        p = *in;
        tmplen = len;

        /* If no IMPLICIT tagging set to SEQUENCE, UNIVERSAL */
        if (tag == -1) {
            tag = V_ASN1_SEQUENCE;
            aclass = V_ASN1_UNIVERSAL;
        }
        /* Get SEQUENCE length and update len, p */
        ret = asn1_check_tlen(&len, NULL, NULL, &seq_eoc, &cst,
                              &p, len, tag, aclass, opt, ctx);
        if (!ret) {
            ASN1err(ASN1_F_ASN1_ITEM_EMBED_D2I, ERR_R_NESTED_ASN1_ERROR);
            goto err;
        } else if (ret == -1)
            return -1;
        if (aux && (aux->flags & ASN1_AFLG_BROKEN)) {
            len = tmplen - (p - *in);
            seq_nolen = 1;
        }
        /* If indefinite we don't do a length check */
        else
            seq_nolen = seq_eoc;
        if (!cst) {
            ASN1err(ASN1_F_ASN1_ITEM_EMBED_D2I, ASN1_R_SEQUENCE_NOT_CONSTRUCTED);
            goto err;
        }

        if (!*pval && !ASN1_item_ex_new(pval, it)) {
            ASN1err(ASN1_F_ASN1_ITEM_EMBED_D2I, ERR_R_NESTED_ASN1_ERROR);
            goto err;
        }

        if (asn1_cb && !asn1_cb(ASN1_OP_D2I_PRE, pval, it, NULL))
            goto auxerr;

        /* Free up and zero any ADB found */
        for (i = 0, tt = it->templates; i < it->tcount; i++, tt++) {
            if (tt->flags & ASN1_TFLG_ADB_MASK) {
                const ASN1_TEMPLATE *seqtt;
                ASN1_VALUE **pseqval;
                seqtt = asn1_do_adb(pval, tt, 0);
                if (seqtt == NULL)
                    continue;
                pseqval = asn1_get_field_ptr(pval, seqtt);
                asn1_template_free(pseqval, seqtt);
            }
        }

        /* Get each field entry */
        for (i = 0, tt = it->templates; i < it->tcount; i++, tt++) {
            const ASN1_TEMPLATE *seqtt;
            ASN1_VALUE **pseqval;
            seqtt = asn1_do_adb(pval, tt, 1);
            if (seqtt == NULL)
                goto err;
            pseqval = asn1_get_field_ptr(pval, seqtt);
            /* Have we ran out of data? */
            if (!len)
                break;
            q = p;
            if (asn1_check_eoc(&p, len)) {
                if (!seq_eoc) {
                    ASN1err(ASN1_F_ASN1_ITEM_EMBED_D2I, ASN1_R_UNEXPECTED_EOC);
                    goto err;
                }
                len -= p - q;
                seq_eoc = 0;
                q = p;
                break;
            }
            /*
             * This determines the OPTIONAL flag value. The field cannot be
             * omitted if it is the last of a SEQUENCE and there is still
             * data to be read. This isn't strictly necessary but it
             * increases efficiency in some cases.
             */
            if (i == (it->tcount - 1))
                isopt = 0;
            else
                isopt = (char)(seqtt->flags & ASN1_TFLG_OPTIONAL);
            /*
             * attempt to read in field, allowing each to be OPTIONAL
             */

            ret = asn1_template_ex_d2i(pseqval, &p, len, seqtt, isopt, ctx,
                                       depth);
            if (!ret) {
                errtt = seqtt;
                goto err;
            } else if (ret == -1) {
                /*
                 * OPTIONAL component absent. Free and zero the field.
                 */
                asn1_template_free(pseqval, seqtt);
                continue;
            }
            /* Update length */
            len -= p - q;
        }

        /* Check for EOC if expecting one */
        if (seq_eoc && !asn1_check_eoc(&p, len)) {
            ASN1err(ASN1_F_ASN1_ITEM_EMBED_D2I, ASN1_R_MISSING_EOC);
            goto err;
        }
        /* Check all data read */
        if (!seq_nolen && len) {
            ASN1err(ASN1_F_ASN1_ITEM_EMBED_D2I, ASN1_R_SEQUENCE_LENGTH_MISMATCH);
            goto err;
        }

        /*
         * If we get here we've got no more data in the SEQUENCE, however we
         * may not have read all fields so check all remaining are OPTIONAL
         * and clear any that are.
         */
        for (; i < it->tcount; tt++, i++) {
            const ASN1_TEMPLATE *seqtt;
            seqtt = asn1_do_adb(pval, tt, 1);
            if (seqtt == NULL)
                goto err;
            if (seqtt->flags & ASN1_TFLG_OPTIONAL) {
                ASN1_VALUE **pseqval;
                pseqval = asn1_get_field_ptr(pval, seqtt);
                asn1_template_free(pseqval, seqtt);
            } else {
                errtt = seqtt;
                ASN1err(ASN1_F_ASN1_ITEM_EMBED_D2I, ASN1_R_FIELD_MISSING);
                goto err;
            }
        }
        /* Save encoding */
        if (!asn1_enc_save(pval, *in, p - *in, it))
            goto auxerr;
        if (asn1_cb && !asn1_cb(ASN1_OP_D2I_POST, pval, it, NULL))
            goto auxerr;
        *in = p;
        return 1;

    default:
        return 0;
    }
 auxerr:
    ASN1err(ASN1_F_ASN1_ITEM_EMBED_D2I, ASN1_R_AUX_ERROR);
 err:
    if (errtt)
        ERR_add_error_data(4, "Field=", errtt->field_name,
                           ", Type=", it->sname);
    else
        ERR_add_error_data(2, "Type=", it->sname);
    return 0;
}

/*
 * Templates are handled with two separate functions. One handles any
 * EXPLICIT tag and the other handles the rest.
 */

static int asn1_template_ex_d2i(ASN1_VALUE **val,
                                const unsigned char **in, long inlen,
                                const ASN1_TEMPLATE *tt, char opt,
                                ASN1_TLC *ctx, int depth)
{
    int flags, aclass;
    int ret;
    long len;
    const unsigned char *p, *q;
    char exp_eoc;
    if (!val)
        return 0;
    flags = tt->flags;
    aclass = flags & ASN1_TFLG_TAG_CLASS;

    p = *in;

    /* Check if EXPLICIT tag expected */
    if (flags & ASN1_TFLG_EXPTAG) {
        char cst;
        /*
         * Need to work out amount of data available to the inner content and
         * where it starts: so read in EXPLICIT header to get the info.
         */
        ret = asn1_check_tlen(&len, NULL, NULL, &exp_eoc, &cst,
                              &p, inlen, tt->tag, aclass, opt, ctx);
        q = p;
        if (!ret) {
            ASN1err(ASN1_F_ASN1_TEMPLATE_EX_D2I, ERR_R_NESTED_ASN1_ERROR);
            return 0;
        } else if (ret == -1)
            return -1;
        if (!cst) {
            ASN1err(ASN1_F_ASN1_TEMPLATE_EX_D2I,
                    ASN1_R_EXPLICIT_TAG_NOT_CONSTRUCTED);
            return 0;
        }
        /* We've found the field so it can't be OPTIONAL now */
        ret = asn1_template_noexp_d2i(val, &p, len, tt, 0, ctx, depth);
        if (!ret) {
            ASN1err(ASN1_F_ASN1_TEMPLATE_EX_D2I, ERR_R_NESTED_ASN1_ERROR);
            return 0;
        }
        /* We read the field in OK so update length */
        len -= p - q;
        if (exp_eoc) {
            /* If NDEF we must have an EOC here */
            if (!asn1_check_eoc(&p, len)) {
                ASN1err(ASN1_F_ASN1_TEMPLATE_EX_D2I, ASN1_R_MISSING_EOC);
                goto err;
            }
        } else {
            /*
             * Otherwise we must hit the EXPLICIT tag end or its an error
             */
            if (len) {
                ASN1err(ASN1_F_ASN1_TEMPLATE_EX_D2I,
                        ASN1_R_EXPLICIT_LENGTH_MISMATCH);
                goto err;
            }
        }
    } else
        return asn1_template_noexp_d2i(val, in, inlen, tt, opt, ctx, depth);

    *in = p;
    return 1;

 err:
    return 0;
}

static int asn1_template_noexp_d2i(ASN1_VALUE **val,
                                   const unsigned char **in, long len,
                                   const ASN1_TEMPLATE *tt, char opt,
                                   ASN1_TLC *ctx, int depth)
{
    int flags, aclass;
    int ret;
    ASN1_VALUE *tval;
    const unsigned char *p, *q;
    if (!val)
        return 0;
    flags = tt->flags;
    aclass = flags & ASN1_TFLG_TAG_CLASS;

    p = *in;
    q = p;

    /*
     * If field is embedded then val needs fixing so it is a pointer to
     * a pointer to a field.
     */
    if (tt->flags & ASN1_TFLG_EMBED) {
        tval = (ASN1_VALUE *)val;
        val = &tval;
    }

    if (flags & ASN1_TFLG_SK_MASK) {
        /* SET OF, SEQUENCE OF */
        int sktag, skaclass;
        char sk_eoc;
        /* First work out expected inner tag value */
        if (flags & ASN1_TFLG_IMPTAG) {
            sktag = tt->tag;
            skaclass = aclass;
        } else {
            skaclass = V_ASN1_UNIVERSAL;
            if (flags & ASN1_TFLG_SET_OF)
                sktag = V_ASN1_SET;
            else
                sktag = V_ASN1_SEQUENCE;
        }
        /* Get the tag */
        ret = asn1_check_tlen(&len, NULL, NULL, &sk_eoc, NULL,
                              &p, len, sktag, skaclass, opt, ctx);
        if (!ret) {
            ASN1err(ASN1_F_ASN1_TEMPLATE_NOEXP_D2I, ERR_R_NESTED_ASN1_ERROR);
            return 0;
        } else if (ret == -1)
            return -1;
        if (!*val)
            *val = (ASN1_VALUE *)sk_ASN1_VALUE_new_null();
        else {
            /*
             * We've got a valid STACK: free up any items present
             */
            STACK_OF(ASN1_VALUE) *sktmp = (STACK_OF(ASN1_VALUE) *)*val;
            ASN1_VALUE *vtmp;
            while (sk_ASN1_VALUE_num(sktmp) > 0) {
                vtmp = sk_ASN1_VALUE_pop(sktmp);
                ASN1_item_ex_free(&vtmp, ASN1_ITEM_ptr(tt->item));
            }
        }

        if (!*val) {
            ASN1err(ASN1_F_ASN1_TEMPLATE_NOEXP_D2I, ERR_R_MALLOC_FAILURE);
            goto err;
        }

        /* Read as many items as we can */
        while (len > 0) {
            ASN1_VALUE *skfield;
            q = p;
            /* See if EOC found */
            if (asn1_check_eoc(&p, len)) {
                if (!sk_eoc) {
                    ASN1err(ASN1_F_ASN1_TEMPLATE_NOEXP_D2I,
                            ASN1_R_UNEXPECTED_EOC);
                    goto err;
                }
                len -= p - q;
                sk_eoc = 0;
                break;
            }
            skfield = NULL;
            if (!asn1_item_embed_d2i(&skfield, &p, len,
                                     ASN1_ITEM_ptr(tt->item), -1, 0, 0, ctx,
                                     depth)) {
                ASN1err(ASN1_F_ASN1_TEMPLATE_NOEXP_D2I,
                        ERR_R_NESTED_ASN1_ERROR);
                /* |skfield| may be partially allocated despite failure. */
                ASN1_item_free(skfield, ASN1_ITEM_ptr(tt->item));
                goto err;
            }
            len -= p - q;
            if (!sk_ASN1_VALUE_push((STACK_OF(ASN1_VALUE) *)*val, skfield)) {
                ASN1err(ASN1_F_ASN1_TEMPLATE_NOEXP_D2I, ERR_R_MALLOC_FAILURE);
                ASN1_item_free(skfield, ASN1_ITEM_ptr(tt->item));
                goto err;
            }
        }
        if (sk_eoc) {
            ASN1err(ASN1_F_ASN1_TEMPLATE_NOEXP_D2I, ASN1_R_MISSING_EOC);
            goto err;
        }
    } else if (flags & ASN1_TFLG_IMPTAG) {
        /* IMPLICIT tagging */
        ret = asn1_item_embed_d2i(val, &p, len,
                                  ASN1_ITEM_ptr(tt->item), tt->tag, aclass, opt,
                                  ctx, depth);
        if (!ret) {
            ASN1err(ASN1_F_ASN1_TEMPLATE_NOEXP_D2I, ERR_R_NESTED_ASN1_ERROR);
            goto err;
        } else if (ret == -1)
            return -1;
    } else {
        /* Nothing special */
        ret = asn1_item_embed_d2i(val, &p, len, ASN1_ITEM_ptr(tt->item),
                                  -1, 0, opt, ctx, depth);
        if (!ret) {
            ASN1err(ASN1_F_ASN1_TEMPLATE_NOEXP_D2I, ERR_R_NESTED_ASN1_ERROR);
            goto err;
        } else if (ret == -1)
            return -1;
    }

    *in = p;
    return 1;

 err:
    return 0;
}

static int asn1_d2i_ex_primitive(ASN1_VALUE **pval,
                                 const unsigned char **in, long inlen,
                                 const ASN1_ITEM *it,
                                 int tag, int aclass, char opt, ASN1_TLC *ctx)
{
    int ret = 0, utype;
    long plen;
    char cst, inf, free_cont = 0;
    const unsigned char *p;
    BUF_MEM buf = { 0, NULL, 0, 0 };
    const unsigned char *cont = NULL;
    long len;
    if (!pval) {
        ASN1err(ASN1_F_ASN1_D2I_EX_PRIMITIVE, ASN1_R_ILLEGAL_NULL);
        return 0;               /* Should never happen */
    }

    if (it->itype == ASN1_ITYPE_MSTRING) {
        utype = tag;
        tag = -1;
    } else
        utype = it->utype;

    if (utype == V_ASN1_ANY) {
        /* If type is ANY need to figure out type from tag */
        unsigned char oclass;
        if (tag >= 0) {
            ASN1err(ASN1_F_ASN1_D2I_EX_PRIMITIVE, ASN1_R_ILLEGAL_TAGGED_ANY);
            return 0;
        }
        if (opt) {
            ASN1err(ASN1_F_ASN1_D2I_EX_PRIMITIVE,
                    ASN1_R_ILLEGAL_OPTIONAL_ANY);
            return 0;
        }
        p = *in;
        ret = asn1_check_tlen(NULL, &utype, &oclass, NULL, NULL,
                              &p, inlen, -1, 0, 0, ctx);
        if (!ret) {
            ASN1err(ASN1_F_ASN1_D2I_EX_PRIMITIVE, ERR_R_NESTED_ASN1_ERROR);
            return 0;
        }
        if (oclass != V_ASN1_UNIVERSAL)
            utype = V_ASN1_OTHER;
    }
    if (tag == -1) {
        tag = utype;
        aclass = V_ASN1_UNIVERSAL;
    }
    p = *in;
    /* Check header */
    ret = asn1_check_tlen(&plen, NULL, NULL, &inf, &cst,
                          &p, inlen, tag, aclass, opt, ctx);
    if (!ret) {
        ASN1err(ASN1_F_ASN1_D2I_EX_PRIMITIVE, ERR_R_NESTED_ASN1_ERROR);
        return 0;
    } else if (ret == -1)
        return -1;
    ret = 0;
    /* SEQUENCE, SET and "OTHER" are left in encoded form */
    if ((utype == V_ASN1_SEQUENCE)
        || (utype == V_ASN1_SET) || (utype == V_ASN1_OTHER)) {
        /*
         * Clear context cache for type OTHER because the auto clear when we
         * have a exact match won't work
         */
        if (utype == V_ASN1_OTHER) {
            asn1_tlc_clear(ctx);
        }
        /* SEQUENCE and SET must be constructed */
        else if (!cst) {
            ASN1err(ASN1_F_ASN1_D2I_EX_PRIMITIVE,
                    ASN1_R_TYPE_NOT_CONSTRUCTED);
            return 0;
        }

        cont = *in;
        /* If indefinite length constructed find the real end */
        if (inf) {
            if (!asn1_find_end(&p, plen, inf))
                goto err;
            len = p - cont;
        } else {
            len = p - cont + plen;
            p += plen;
        }
    } else if (cst) {
        if (utype == V_ASN1_NULL || utype == V_ASN1_BOOLEAN
            || utype == V_ASN1_OBJECT || utype == V_ASN1_INTEGER
            || utype == V_ASN1_ENUMERATED) {
            ASN1err(ASN1_F_ASN1_D2I_EX_PRIMITIVE, ASN1_R_TYPE_NOT_PRIMITIVE);
            return 0;
        }

        /* Free any returned 'buf' content */
        free_cont = 1;
        /*
         * Should really check the internal tags are correct but some things
         * may get this wrong. The relevant specs say that constructed string
         * types should be OCTET STRINGs internally irrespective of the type.
         * So instead just check for UNIVERSAL class and ignore the tag.
         */
        if (!asn1_collect(&buf, &p, plen, inf, -1, V_ASN1_UNIVERSAL, 0)) {
            goto err;
        }
        len = buf.length;
        /* Append a final null to string */
        if (!BUF_MEM_grow_clean(&buf, len + 1)) {
            ASN1err(ASN1_F_ASN1_D2I_EX_PRIMITIVE, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        buf.data[len] = 0;
        cont = (const unsigned char *)buf.data;
    } else {
        cont = p;
        len = plen;
        p += plen;
    }

    /* We now have content length and type: translate into a structure */
    /* asn1_ex_c2i may reuse allocated buffer, and so sets free_cont to 0 */
    if (!asn1_ex_c2i(pval, cont, len, utype, &free_cont, it))
        goto err;

    *in = p;
    ret = 1;
 err:
    if (free_cont)
        OPENSSL_free(buf.data);
    return ret;
}

/* Translate ASN1 content octets into a structure */

static int asn1_ex_c2i(ASN1_VALUE **pval, const unsigned char *cont, int len,
                       int utype, char *free_cont, const ASN1_ITEM *it)
{
    ASN1_VALUE **opval = NULL;
    ASN1_STRING *stmp;
    ASN1_TYPE *typ = NULL;
    int ret = 0;
    const ASN1_PRIMITIVE_FUNCS *pf;
    ASN1_INTEGER **tint;
    pf = it->funcs;

    if (pf && pf->prim_c2i)
        return pf->prim_c2i(pval, cont, len, utype, free_cont, it);
    /* If ANY type clear type and set pointer to internal value */
    if (it->utype == V_ASN1_ANY) {
        if (!*pval) {
            typ = ASN1_TYPE_new();
            if (typ == NULL)
                goto err;
            *pval = (ASN1_VALUE *)typ;
        } else
            typ = (ASN1_TYPE *)*pval;

        if (utype != typ->type)
            ASN1_TYPE_set(typ, utype, NULL);
        opval = pval;
        pval = &typ->value.asn1_value;
    }
    switch (utype) {
    case V_ASN1_OBJECT:
        if (!c2i_ASN1_OBJECT((ASN1_OBJECT **)pval, &cont, len))
            goto err;
        break;

    case V_ASN1_NULL:
        if (len) {
            ASN1err(ASN1_F_ASN1_EX_C2I, ASN1_R_NULL_IS_WRONG_LENGTH);
            goto err;
        }
        *pval = (ASN1_VALUE *)1;
        break;

    case V_ASN1_BOOLEAN:
        if (len != 1) {
            ASN1err(ASN1_F_ASN1_EX_C2I, ASN1_R_BOOLEAN_IS_WRONG_LENGTH);
            goto err;
        } else {
            ASN1_BOOLEAN *tbool;
            tbool = (ASN1_BOOLEAN *)pval;
            *tbool = *cont;
        }
        break;

    case V_ASN1_BIT_STRING:
        if (!c2i_ASN1_BIT_STRING((ASN1_BIT_STRING **)pval, &cont, len))
            goto err;
        break;

    case V_ASN1_INTEGER:
    case V_ASN1_ENUMERATED:
        tint = (ASN1_INTEGER **)pval;
        if (!c2i_ASN1_INTEGER(tint, &cont, len))
            goto err;
        /* Fixup type to match the expected form */
        (*tint)->type = utype | ((*tint)->type & V_ASN1_NEG);
        break;

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
    case V_ASN1_OTHER:
    case V_ASN1_SET:
    case V_ASN1_SEQUENCE:
    default:
        if (utype == V_ASN1_BMPSTRING && (len & 1)) {
            ASN1err(ASN1_F_ASN1_EX_C2I, ASN1_R_BMPSTRING_IS_WRONG_LENGTH);
            goto err;
        }
        if (utype == V_ASN1_UNIVERSALSTRING && (len & 3)) {
            ASN1err(ASN1_F_ASN1_EX_C2I,
                    ASN1_R_UNIVERSALSTRING_IS_WRONG_LENGTH);
            goto err;
        }
        /* All based on ASN1_STRING and handled the same */
        if (!*pval) {
            stmp = ASN1_STRING_type_new(utype);
            if (stmp == NULL) {
                ASN1err(ASN1_F_ASN1_EX_C2I, ERR_R_MALLOC_FAILURE);
                goto err;
            }
            *pval = (ASN1_VALUE *)stmp;
        } else {
            stmp = (ASN1_STRING *)*pval;
            stmp->type = utype;
        }
        /* If we've already allocated a buffer use it */
        if (*free_cont) {
            OPENSSL_free(stmp->data);
            stmp->data = (unsigned char *)cont; /* UGLY CAST! RL */
            stmp->length = len;
            *free_cont = 0;
        } else {
            if (!ASN1_STRING_set(stmp, cont, len)) {
                ASN1err(ASN1_F_ASN1_EX_C2I, ERR_R_MALLOC_FAILURE);
                ASN1_STRING_free(stmp);
                *pval = NULL;
                goto err;
            }
        }
        break;
    }
    /* If ASN1_ANY and NULL type fix up value */
    if (typ && (utype == V_ASN1_NULL))
        typ->value.ptr = NULL;

    ret = 1;
 err:
    if (!ret) {
        ASN1_TYPE_free(typ);
        if (opval)
            *opval = NULL;
    }
    return ret;
}

/*
 * This function finds the end of an ASN1 structure when passed its maximum
 * length, whether it is indefinite length and a pointer to the content. This
 * is more efficient than calling asn1_collect because it does not recurse on
 * each indefinite length header.
 */

static int asn1_find_end(const unsigned char **in, long len, char inf)
{
    uint32_t expected_eoc;
    long plen;
    const unsigned char *p = *in, *q;
    /* If not indefinite length constructed just add length */
    if (inf == 0) {
        *in += len;
        return 1;
    }
    expected_eoc = 1;
    /*
     * Indefinite length constructed form. Find the end when enough EOCs are
     * found. If more indefinite length constructed headers are encountered
     * increment the expected eoc count otherwise just skip to the end of the
     * data.
     */
    while (len > 0) {
        if (asn1_check_eoc(&p, len)) {
            expected_eoc--;
            if (expected_eoc == 0)
                break;
            len -= 2;
            continue;
        }
        q = p;
        /* Just read in a header: only care about the length */
        if (!asn1_check_tlen(&plen, NULL, NULL, &inf, NULL, &p, len,
                             -1, 0, 0, NULL)) {
            ASN1err(ASN1_F_ASN1_FIND_END, ERR_R_NESTED_ASN1_ERROR);
            return 0;
        }
        if (inf) {
            if (expected_eoc == UINT32_MAX) {
                ASN1err(ASN1_F_ASN1_FIND_END, ERR_R_NESTED_ASN1_ERROR);
                return 0;
            }
            expected_eoc++;
        } else {
            p += plen;
        }
        len -= p - q;
    }
    if (expected_eoc) {
        ASN1err(ASN1_F_ASN1_FIND_END, ASN1_R_MISSING_EOC);
        return 0;
    }
    *in = p;
    return 1;
}

/*
 * This function collects the asn1 data from a constructed string type into
 * a buffer. The values of 'in' and 'len' should refer to the contents of the
 * constructed type and 'inf' should be set if it is indefinite length.
 */

#ifndef ASN1_MAX_STRING_NEST
/*
 * This determines how many levels of recursion are permitted in ASN1 string
 * types. If it is not limited stack overflows can occur. If set to zero no
 * recursion is allowed at all. Although zero should be adequate examples
 * exist that require a value of 1. So 5 should be more than enough.
 */
# define ASN1_MAX_STRING_NEST 5
#endif

static int asn1_collect(BUF_MEM *buf, const unsigned char **in, long len,
                        char inf, int tag, int aclass, int depth)
{
    const unsigned char *p, *q;
    long plen;
    char cst, ininf;
    p = *in;
    inf &= 1;
    /*
     * If no buffer and not indefinite length constructed just pass over the
     * encoded data
     */
    if (!buf && !inf) {
        *in += len;
        return 1;
    }
    while (len > 0) {
        q = p;
        /* Check for EOC */
        if (asn1_check_eoc(&p, len)) {
            /*
             * EOC is illegal outside indefinite length constructed form
             */
            if (!inf) {
                ASN1err(ASN1_F_ASN1_COLLECT, ASN1_R_UNEXPECTED_EOC);
                return 0;
            }
            inf = 0;
            break;
        }

        if (!asn1_check_tlen(&plen, NULL, NULL, &ininf, &cst, &p,
                             len, tag, aclass, 0, NULL)) {
            ASN1err(ASN1_F_ASN1_COLLECT, ERR_R_NESTED_ASN1_ERROR);
            return 0;
        }

        /* If indefinite length constructed update max length */
        if (cst) {
            if (depth >= ASN1_MAX_STRING_NEST) {
                ASN1err(ASN1_F_ASN1_COLLECT, ASN1_R_NESTED_ASN1_STRING);
                return 0;
            }
            if (!asn1_collect(buf, &p, plen, ininf, tag, aclass, depth + 1))
                return 0;
        } else if (plen && !collect_data(buf, &p, plen))
            return 0;
        len -= p - q;
    }
    if (inf) {
        ASN1err(ASN1_F_ASN1_COLLECT, ASN1_R_MISSING_EOC);
        return 0;
    }
    *in = p;
    return 1;
}

static int collect_data(BUF_MEM *buf, const unsigned char **p, long plen)
{
    int len;
    if (buf) {
        len = buf->length;
        if (!BUF_MEM_grow_clean(buf, len + plen)) {
            ASN1err(ASN1_F_COLLECT_DATA, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        memcpy(buf->data + len, *p, plen);
    }
    *p += plen;
    return 1;
}

/* Check for ASN1 EOC and swallow it if found */

static int asn1_check_eoc(const unsigned char **in, long len)
{
    const unsigned char *p;
    if (len < 2)
        return 0;
    p = *in;
    if (!p[0] && !p[1]) {
        *in += 2;
        return 1;
    }
    return 0;
}

/*
 * Check an ASN1 tag and length: a bit like ASN1_get_object but it sets the
 * length for indefinite length constructed form, we don't know the exact
 * length but we can set an upper bound to the amount of data available minus
 * the header length just read.
 */

static int asn1_check_tlen(long *olen, int *otag, unsigned char *oclass,
                           char *inf, char *cst,
                           const unsigned char **in, long len,
                           int exptag, int expclass, char opt, ASN1_TLC *ctx)
{
    int i;
    int ptag, pclass;
    long plen;
    const unsigned char *p, *q;
    p = *in;
    q = p;

    if (ctx && ctx->valid) {
        i = ctx->ret;
        plen = ctx->plen;
        pclass = ctx->pclass;
        ptag = ctx->ptag;
        p += ctx->hdrlen;
    } else {
        i = ASN1_get_object(&p, &plen, &ptag, &pclass, len);
        if (ctx) {
            ctx->ret = i;
            ctx->plen = plen;
            ctx->pclass = pclass;
            ctx->ptag = ptag;
            ctx->hdrlen = p - q;
            ctx->valid = 1;
            /*
             * If definite length, and no error, length + header can't exceed
             * total amount of data available.
             */
            if (!(i & 0x81) && ((plen + ctx->hdrlen) > len)) {
                ASN1err(ASN1_F_ASN1_CHECK_TLEN, ASN1_R_TOO_LONG);
                asn1_tlc_clear(ctx);
                return 0;
            }
        }
    }

    if (i & 0x80) {
        ASN1err(ASN1_F_ASN1_CHECK_TLEN, ASN1_R_BAD_OBJECT_HEADER);
        asn1_tlc_clear(ctx);
        return 0;
    }
    if (exptag >= 0) {
        if ((exptag != ptag) || (expclass != pclass)) {
            /*
             * If type is OPTIONAL, not an error: indicate missing type.
             */
            if (opt)
                return -1;
            asn1_tlc_clear(ctx);
            ASN1err(ASN1_F_ASN1_CHECK_TLEN, ASN1_R_WRONG_TAG);
            return 0;
        }
        /*
         * We have a tag and class match: assume we are going to do something
         * with it
         */
        asn1_tlc_clear(ctx);
    }

    if (i & 1)
        plen = len - (p - q);

    if (inf)
        *inf = i & 1;

    if (cst)
        *cst = i & V_ASN1_CONSTRUCTED;

    if (olen)
        *olen = plen;

    if (oclass)
        *oclass = pclass;

    if (otag)
        *otag = ptag;

    *in = p;
    return 1;
}
