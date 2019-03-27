/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/ctype.h"
#include "internal/cryptlib.h"
#include <openssl/asn1t.h>
#include <openssl/x509.h>
#include "internal/x509_int.h"
#include "internal/asn1_int.h"
#include "x509_lcl.h"

/*
 * Maximum length of X509_NAME: much larger than anything we should
 * ever see in practice.
 */

#define X509_NAME_MAX (1024 * 1024)

static int x509_name_ex_d2i(ASN1_VALUE **val,
                            const unsigned char **in, long len,
                            const ASN1_ITEM *it,
                            int tag, int aclass, char opt, ASN1_TLC *ctx);

static int x509_name_ex_i2d(ASN1_VALUE **val, unsigned char **out,
                            const ASN1_ITEM *it, int tag, int aclass);
static int x509_name_ex_new(ASN1_VALUE **val, const ASN1_ITEM *it);
static void x509_name_ex_free(ASN1_VALUE **val, const ASN1_ITEM *it);

static int x509_name_encode(X509_NAME *a);
static int x509_name_canon(X509_NAME *a);
static int asn1_string_canon(ASN1_STRING *out, const ASN1_STRING *in);
static int i2d_name_canon(STACK_OF(STACK_OF_X509_NAME_ENTRY) * intname,
                          unsigned char **in);

static int x509_name_ex_print(BIO *out, ASN1_VALUE **pval,
                              int indent,
                              const char *fname, const ASN1_PCTX *pctx);

ASN1_SEQUENCE(X509_NAME_ENTRY) = {
        ASN1_SIMPLE(X509_NAME_ENTRY, object, ASN1_OBJECT),
        ASN1_SIMPLE(X509_NAME_ENTRY, value, ASN1_PRINTABLE)
} ASN1_SEQUENCE_END(X509_NAME_ENTRY)

IMPLEMENT_ASN1_FUNCTIONS(X509_NAME_ENTRY)
IMPLEMENT_ASN1_DUP_FUNCTION(X509_NAME_ENTRY)

/*
 * For the "Name" type we need a SEQUENCE OF { SET OF X509_NAME_ENTRY } so
 * declare two template wrappers for this
 */

ASN1_ITEM_TEMPLATE(X509_NAME_ENTRIES) =
        ASN1_EX_TEMPLATE_TYPE(ASN1_TFLG_SET_OF, 0, RDNS, X509_NAME_ENTRY)
static_ASN1_ITEM_TEMPLATE_END(X509_NAME_ENTRIES)

ASN1_ITEM_TEMPLATE(X509_NAME_INTERNAL) =
        ASN1_EX_TEMPLATE_TYPE(ASN1_TFLG_SEQUENCE_OF, 0, Name, X509_NAME_ENTRIES)
static_ASN1_ITEM_TEMPLATE_END(X509_NAME_INTERNAL)

/*
 * Normally that's where it would end: we'd have two nested STACK structures
 * representing the ASN1. Unfortunately X509_NAME uses a completely different
 * form and caches encodings so we have to process the internal form and
 * convert to the external form.
 */

static const ASN1_EXTERN_FUNCS x509_name_ff = {
    NULL,
    x509_name_ex_new,
    x509_name_ex_free,
    0,                          /* Default clear behaviour is OK */
    x509_name_ex_d2i,
    x509_name_ex_i2d,
    x509_name_ex_print
};

IMPLEMENT_EXTERN_ASN1(X509_NAME, V_ASN1_SEQUENCE, x509_name_ff)

IMPLEMENT_ASN1_FUNCTIONS(X509_NAME)

IMPLEMENT_ASN1_DUP_FUNCTION(X509_NAME)

static int x509_name_ex_new(ASN1_VALUE **val, const ASN1_ITEM *it)
{
    X509_NAME *ret = OPENSSL_zalloc(sizeof(*ret));

    if (ret == NULL)
        goto memerr;
    if ((ret->entries = sk_X509_NAME_ENTRY_new_null()) == NULL)
        goto memerr;
    if ((ret->bytes = BUF_MEM_new()) == NULL)
        goto memerr;
    ret->modified = 1;
    *val = (ASN1_VALUE *)ret;
    return 1;

 memerr:
    ASN1err(ASN1_F_X509_NAME_EX_NEW, ERR_R_MALLOC_FAILURE);
    if (ret) {
        sk_X509_NAME_ENTRY_free(ret->entries);
        OPENSSL_free(ret);
    }
    return 0;
}

static void x509_name_ex_free(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
    X509_NAME *a;

    if (!pval || !*pval)
        return;
    a = (X509_NAME *)*pval;

    BUF_MEM_free(a->bytes);
    sk_X509_NAME_ENTRY_pop_free(a->entries, X509_NAME_ENTRY_free);
    OPENSSL_free(a->canon_enc);
    OPENSSL_free(a);
    *pval = NULL;
}

static void local_sk_X509_NAME_ENTRY_free(STACK_OF(X509_NAME_ENTRY) *ne)
{
    sk_X509_NAME_ENTRY_free(ne);
}

static void local_sk_X509_NAME_ENTRY_pop_free(STACK_OF(X509_NAME_ENTRY) *ne)
{
    sk_X509_NAME_ENTRY_pop_free(ne, X509_NAME_ENTRY_free);
}

static int x509_name_ex_d2i(ASN1_VALUE **val,
                            const unsigned char **in, long len,
                            const ASN1_ITEM *it, int tag, int aclass,
                            char opt, ASN1_TLC *ctx)
{
    const unsigned char *p = *in, *q;
    union {
        STACK_OF(STACK_OF_X509_NAME_ENTRY) *s;
        ASN1_VALUE *a;
    } intname = {
        NULL
    };
    union {
        X509_NAME *x;
        ASN1_VALUE *a;
    } nm = {
        NULL
    };
    int i, j, ret;
    STACK_OF(X509_NAME_ENTRY) *entries;
    X509_NAME_ENTRY *entry;
    if (len > X509_NAME_MAX)
        len = X509_NAME_MAX;
    q = p;

    /* Get internal representation of Name */
    ret = ASN1_item_ex_d2i(&intname.a,
                           &p, len, ASN1_ITEM_rptr(X509_NAME_INTERNAL),
                           tag, aclass, opt, ctx);

    if (ret <= 0)
        return ret;

    if (*val)
        x509_name_ex_free(val, NULL);
    if (!x509_name_ex_new(&nm.a, NULL))
        goto err;
    /* We've decoded it: now cache encoding */
    if (!BUF_MEM_grow(nm.x->bytes, p - q))
        goto err;
    memcpy(nm.x->bytes->data, q, p - q);

    /* Convert internal representation to X509_NAME structure */
    for (i = 0; i < sk_STACK_OF_X509_NAME_ENTRY_num(intname.s); i++) {
        entries = sk_STACK_OF_X509_NAME_ENTRY_value(intname.s, i);
        for (j = 0; j < sk_X509_NAME_ENTRY_num(entries); j++) {
            entry = sk_X509_NAME_ENTRY_value(entries, j);
            entry->set = i;
            if (!sk_X509_NAME_ENTRY_push(nm.x->entries, entry))
                goto err;
            sk_X509_NAME_ENTRY_set(entries, j, NULL);
        }
    }
    ret = x509_name_canon(nm.x);
    if (!ret)
        goto err;
    sk_STACK_OF_X509_NAME_ENTRY_pop_free(intname.s,
                                         local_sk_X509_NAME_ENTRY_free);
    nm.x->modified = 0;
    *val = nm.a;
    *in = p;
    return ret;

 err:
    if (nm.x != NULL)
        X509_NAME_free(nm.x);
    sk_STACK_OF_X509_NAME_ENTRY_pop_free(intname.s,
                                         local_sk_X509_NAME_ENTRY_pop_free);
    ASN1err(ASN1_F_X509_NAME_EX_D2I, ERR_R_NESTED_ASN1_ERROR);
    return 0;
}

static int x509_name_ex_i2d(ASN1_VALUE **val, unsigned char **out,
                            const ASN1_ITEM *it, int tag, int aclass)
{
    int ret;
    X509_NAME *a = (X509_NAME *)*val;
    if (a->modified) {
        ret = x509_name_encode(a);
        if (ret < 0)
            return ret;
        ret = x509_name_canon(a);
        if (ret < 0)
            return ret;
    }
    ret = a->bytes->length;
    if (out != NULL) {
        memcpy(*out, a->bytes->data, ret);
        *out += ret;
    }
    return ret;
}

static int x509_name_encode(X509_NAME *a)
{
    union {
        STACK_OF(STACK_OF_X509_NAME_ENTRY) *s;
        ASN1_VALUE *a;
    } intname = {
        NULL
    };
    int len;
    unsigned char *p;
    STACK_OF(X509_NAME_ENTRY) *entries = NULL;
    X509_NAME_ENTRY *entry;
    int i, set = -1;
    intname.s = sk_STACK_OF_X509_NAME_ENTRY_new_null();
    if (!intname.s)
        goto memerr;
    for (i = 0; i < sk_X509_NAME_ENTRY_num(a->entries); i++) {
        entry = sk_X509_NAME_ENTRY_value(a->entries, i);
        if (entry->set != set) {
            entries = sk_X509_NAME_ENTRY_new_null();
            if (!entries)
                goto memerr;
            if (!sk_STACK_OF_X509_NAME_ENTRY_push(intname.s, entries)) {
                sk_X509_NAME_ENTRY_free(entries);
                goto memerr;
            }
            set = entry->set;
        }
        if (!sk_X509_NAME_ENTRY_push(entries, entry))
            goto memerr;
    }
    len = ASN1_item_ex_i2d(&intname.a, NULL,
                           ASN1_ITEM_rptr(X509_NAME_INTERNAL), -1, -1);
    if (!BUF_MEM_grow(a->bytes, len))
        goto memerr;
    p = (unsigned char *)a->bytes->data;
    ASN1_item_ex_i2d(&intname.a,
                     &p, ASN1_ITEM_rptr(X509_NAME_INTERNAL), -1, -1);
    sk_STACK_OF_X509_NAME_ENTRY_pop_free(intname.s,
                                         local_sk_X509_NAME_ENTRY_free);
    a->modified = 0;
    return len;
 memerr:
    sk_STACK_OF_X509_NAME_ENTRY_pop_free(intname.s,
                                         local_sk_X509_NAME_ENTRY_free);
    ASN1err(ASN1_F_X509_NAME_ENCODE, ERR_R_MALLOC_FAILURE);
    return -1;
}

static int x509_name_ex_print(BIO *out, ASN1_VALUE **pval,
                              int indent,
                              const char *fname, const ASN1_PCTX *pctx)
{
    if (X509_NAME_print_ex(out, (const X509_NAME *)*pval,
                           indent, pctx->nm_flags) <= 0)
        return 0;
    return 2;
}

/*
 * This function generates the canonical encoding of the Name structure. In
 * it all strings are converted to UTF8, leading, trailing and multiple
 * spaces collapsed, converted to lower case and the leading SEQUENCE header
 * removed. In future we could also normalize the UTF8 too. By doing this
 * comparison of Name structures can be rapidly performed by just using
 * memcmp() of the canonical encoding. By omitting the leading SEQUENCE name
 * constraints of type dirName can also be checked with a simple memcmp().
 */

static int x509_name_canon(X509_NAME *a)
{
    unsigned char *p;
    STACK_OF(STACK_OF_X509_NAME_ENTRY) *intname;
    STACK_OF(X509_NAME_ENTRY) *entries = NULL;
    X509_NAME_ENTRY *entry, *tmpentry = NULL;
    int i, set = -1, ret = 0, len;

    OPENSSL_free(a->canon_enc);
    a->canon_enc = NULL;
    /* Special case: empty X509_NAME => null encoding */
    if (sk_X509_NAME_ENTRY_num(a->entries) == 0) {
        a->canon_enclen = 0;
        return 1;
    }
    intname = sk_STACK_OF_X509_NAME_ENTRY_new_null();
    if (intname == NULL) {
        X509err(X509_F_X509_NAME_CANON, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    for (i = 0; i < sk_X509_NAME_ENTRY_num(a->entries); i++) {
        entry = sk_X509_NAME_ENTRY_value(a->entries, i);
        if (entry->set != set) {
            entries = sk_X509_NAME_ENTRY_new_null();
            if (entries == NULL)
                goto err;
            if (!sk_STACK_OF_X509_NAME_ENTRY_push(intname, entries)) {
                sk_X509_NAME_ENTRY_free(entries);
                X509err(X509_F_X509_NAME_CANON, ERR_R_MALLOC_FAILURE);
                goto err;
            }
            set = entry->set;
        }
        tmpentry = X509_NAME_ENTRY_new();
        if (tmpentry == NULL) {
            X509err(X509_F_X509_NAME_CANON, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        tmpentry->object = OBJ_dup(entry->object);
        if (tmpentry->object == NULL) {
            X509err(X509_F_X509_NAME_CANON, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        if (!asn1_string_canon(tmpentry->value, entry->value))
            goto err;
        if (!sk_X509_NAME_ENTRY_push(entries, tmpentry)) {
            X509err(X509_F_X509_NAME_CANON, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        tmpentry = NULL;
    }

    /* Finally generate encoding */
    len = i2d_name_canon(intname, NULL);
    if (len < 0)
        goto err;
    a->canon_enclen = len;

    p = OPENSSL_malloc(a->canon_enclen);
    if (p == NULL) {
        X509err(X509_F_X509_NAME_CANON, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    a->canon_enc = p;

    i2d_name_canon(intname, &p);

    ret = 1;

 err:
    X509_NAME_ENTRY_free(tmpentry);
    sk_STACK_OF_X509_NAME_ENTRY_pop_free(intname,
                                         local_sk_X509_NAME_ENTRY_pop_free);
    return ret;
}

/* Bitmap of all the types of string that will be canonicalized. */

#define ASN1_MASK_CANON \
        (B_ASN1_UTF8STRING | B_ASN1_BMPSTRING | B_ASN1_UNIVERSALSTRING \
        | B_ASN1_PRINTABLESTRING | B_ASN1_T61STRING | B_ASN1_IA5STRING \
        | B_ASN1_VISIBLESTRING)

static int asn1_string_canon(ASN1_STRING *out, const ASN1_STRING *in)
{
    unsigned char *to, *from;
    int len, i;

    /* If type not in bitmask just copy string across */
    if (!(ASN1_tag2bit(in->type) & ASN1_MASK_CANON)) {
        if (!ASN1_STRING_copy(out, in))
            return 0;
        return 1;
    }

    out->type = V_ASN1_UTF8STRING;
    out->length = ASN1_STRING_to_UTF8(&out->data, in);
    if (out->length == -1)
        return 0;

    to = out->data;
    from = to;

    len = out->length;

    /*
     * Convert string in place to canonical form. Ultimately we may need to
     * handle a wider range of characters but for now ignore anything with
     * MSB set and rely on the ossl_isspace() to fail on bad characters without
     * needing isascii or range checks as well.
     */

    /* Ignore leading spaces */
    while (len > 0 && ossl_isspace(*from)) {
        from++;
        len--;
    }

    to = from + len;

    /* Ignore trailing spaces */
    while (len > 0 && ossl_isspace(to[-1])) {
        to--;
        len--;
    }

    to = out->data;

    i = 0;
    while (i < len) {
        /* If not ASCII set just copy across */
        if (!ossl_isascii(*from)) {
            *to++ = *from++;
            i++;
        }
        /* Collapse multiple spaces */
        else if (ossl_isspace(*from)) {
            /* Copy one space across */
            *to++ = ' ';
            /*
             * Ignore subsequent spaces. Note: don't need to check len here
             * because we know the last character is a non-space so we can't
             * overflow.
             */
            do {
                from++;
                i++;
            }
            while (ossl_isspace(*from));
        } else {
            *to++ = ossl_tolower(*from);
            from++;
            i++;
        }
    }

    out->length = to - out->data;

    return 1;

}

static int i2d_name_canon(STACK_OF(STACK_OF_X509_NAME_ENTRY) * _intname,
                          unsigned char **in)
{
    int i, len, ltmp;
    ASN1_VALUE *v;
    STACK_OF(ASN1_VALUE) *intname = (STACK_OF(ASN1_VALUE) *)_intname;

    len = 0;
    for (i = 0; i < sk_ASN1_VALUE_num(intname); i++) {
        v = sk_ASN1_VALUE_value(intname, i);
        ltmp = ASN1_item_ex_i2d(&v, in,
                                ASN1_ITEM_rptr(X509_NAME_ENTRIES), -1, -1);
        if (ltmp < 0)
            return ltmp;
        len += ltmp;
    }
    return len;
}

int X509_NAME_set(X509_NAME **xn, X509_NAME *name)
{
    if (*xn == name)
        return *xn != NULL;
    if ((name = X509_NAME_dup(name)) == NULL)
        return 0;
    X509_NAME_free(*xn);
    *xn = name;
    return 1;
}

int X509_NAME_print(BIO *bp, const X509_NAME *name, int obase)
{
    char *s, *c, *b;
    int l, i;

    l = 80 - 2 - obase;

    b = X509_NAME_oneline(name, NULL, 0);
    if (!b)
        return 0;
    if (!*b) {
        OPENSSL_free(b);
        return 1;
    }
    s = b + 1;                  /* skip the first slash */

    c = s;
    for (;;) {
        if (((*s == '/') &&
             (ossl_isupper(s[1]) && ((s[2] == '=') ||
                                (ossl_isupper(s[2]) && (s[3] == '='))
              ))) || (*s == '\0'))
        {
            i = s - c;
            if (BIO_write(bp, c, i) != i)
                goto err;
            c = s + 1;          /* skip following slash */
            if (*s != '\0') {
                if (BIO_write(bp, ", ", 2) != 2)
                    goto err;
            }
            l--;
        }
        if (*s == '\0')
            break;
        s++;
        l--;
    }

    OPENSSL_free(b);
    return 1;
 err:
    X509err(X509_F_X509_NAME_PRINT, ERR_R_BUF_LIB);
    OPENSSL_free(b);
    return 0;
}

int X509_NAME_get0_der(X509_NAME *nm, const unsigned char **pder,
                       size_t *pderlen)
{
    /* Make sure encoding is valid */
    if (i2d_X509_NAME(nm, NULL) <= 0)
        return 0;
    if (pder != NULL)
        *pder = (unsigned char *)nm->bytes->data;
    if (pderlen != NULL)
        *pderlen = nm->bytes->length;
    return 1;
}
