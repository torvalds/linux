/*
 * Copyright 1999-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* X509 v3 extension utilities */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/conf.h>
#include <openssl/x509v3.h>

/* Extension printing routines */

static int unknown_ext_print(BIO *out, const unsigned char *ext, int extlen,
                             unsigned long flag, int indent, int supported);

/* Print out a name+value stack */

void X509V3_EXT_val_prn(BIO *out, STACK_OF(CONF_VALUE) *val, int indent,
                        int ml)
{
    int i;
    CONF_VALUE *nval;
    if (!val)
        return;
    if (!ml || !sk_CONF_VALUE_num(val)) {
        BIO_printf(out, "%*s", indent, "");
        if (!sk_CONF_VALUE_num(val))
            BIO_puts(out, "<EMPTY>\n");
    }
    for (i = 0; i < sk_CONF_VALUE_num(val); i++) {
        if (ml)
            BIO_printf(out, "%*s", indent, "");
        else if (i > 0)
            BIO_printf(out, ", ");
        nval = sk_CONF_VALUE_value(val, i);
        if (!nval->name)
            BIO_puts(out, nval->value);
        else if (!nval->value)
            BIO_puts(out, nval->name);
#ifndef CHARSET_EBCDIC
        else
            BIO_printf(out, "%s:%s", nval->name, nval->value);
#else
        else {
            int len;
            char *tmp;
            len = strlen(nval->value) + 1;
            tmp = OPENSSL_malloc(len);
            if (tmp != NULL) {
                ascii2ebcdic(tmp, nval->value, len);
                BIO_printf(out, "%s:%s", nval->name, tmp);
                OPENSSL_free(tmp);
            }
        }
#endif
        if (ml)
            BIO_puts(out, "\n");
    }
}

/* Main routine: print out a general extension */

int X509V3_EXT_print(BIO *out, X509_EXTENSION *ext, unsigned long flag,
                     int indent)
{
    void *ext_str = NULL;
    char *value = NULL;
    ASN1_OCTET_STRING *extoct;
    const unsigned char *p;
    int extlen;
    const X509V3_EXT_METHOD *method;
    STACK_OF(CONF_VALUE) *nval = NULL;
    int ok = 1;

    extoct = X509_EXTENSION_get_data(ext);
    p = ASN1_STRING_get0_data(extoct);
    extlen = ASN1_STRING_length(extoct);

    if ((method = X509V3_EXT_get(ext)) == NULL)
        return unknown_ext_print(out, p, extlen, flag, indent, 0);
    if (method->it)
        ext_str = ASN1_item_d2i(NULL, &p, extlen, ASN1_ITEM_ptr(method->it));
    else
        ext_str = method->d2i(NULL, &p, extlen);

    if (!ext_str)
        return unknown_ext_print(out, p, extlen, flag, indent, 1);

    if (method->i2s) {
        if ((value = method->i2s(method, ext_str)) == NULL) {
            ok = 0;
            goto err;
        }
#ifndef CHARSET_EBCDIC
        BIO_printf(out, "%*s%s", indent, "", value);
#else
        {
            int len;
            char *tmp;
            len = strlen(value) + 1;
            tmp = OPENSSL_malloc(len);
            if (tmp != NULL) {
                ascii2ebcdic(tmp, value, len);
                BIO_printf(out, "%*s%s", indent, "", tmp);
                OPENSSL_free(tmp);
            }
        }
#endif
    } else if (method->i2v) {
        if ((nval = method->i2v(method, ext_str, NULL)) == NULL) {
            ok = 0;
            goto err;
        }
        X509V3_EXT_val_prn(out, nval, indent,
                           method->ext_flags & X509V3_EXT_MULTILINE);
    } else if (method->i2r) {
        if (!method->i2r(method, ext_str, out, indent))
            ok = 0;
    } else
        ok = 0;

 err:
    sk_CONF_VALUE_pop_free(nval, X509V3_conf_free);
    OPENSSL_free(value);
    if (method->it)
        ASN1_item_free(ext_str, ASN1_ITEM_ptr(method->it));
    else
        method->ext_free(ext_str);
    return ok;
}

int X509V3_extensions_print(BIO *bp, const char *title,
                            const STACK_OF(X509_EXTENSION) *exts,
                            unsigned long flag, int indent)
{
    int i, j;

    if (sk_X509_EXTENSION_num(exts) <= 0)
        return 1;

    if (title) {
        BIO_printf(bp, "%*s%s:\n", indent, "", title);
        indent += 4;
    }

    for (i = 0; i < sk_X509_EXTENSION_num(exts); i++) {
        ASN1_OBJECT *obj;
        X509_EXTENSION *ex;
        ex = sk_X509_EXTENSION_value(exts, i);
        if (indent && BIO_printf(bp, "%*s", indent, "") <= 0)
            return 0;
        obj = X509_EXTENSION_get_object(ex);
        i2a_ASN1_OBJECT(bp, obj);
        j = X509_EXTENSION_get_critical(ex);
        if (BIO_printf(bp, ": %s\n", j ? "critical" : "") <= 0)
            return 0;
        if (!X509V3_EXT_print(bp, ex, flag, indent + 4)) {
            BIO_printf(bp, "%*s", indent + 4, "");
            ASN1_STRING_print(bp, X509_EXTENSION_get_data(ex));
        }
        if (BIO_write(bp, "\n", 1) <= 0)
            return 0;
    }
    return 1;
}

static int unknown_ext_print(BIO *out, const unsigned char *ext, int extlen,
                             unsigned long flag, int indent, int supported)
{
    switch (flag & X509V3_EXT_UNKNOWN_MASK) {

    case X509V3_EXT_DEFAULT:
        return 0;

    case X509V3_EXT_ERROR_UNKNOWN:
        if (supported)
            BIO_printf(out, "%*s<Parse Error>", indent, "");
        else
            BIO_printf(out, "%*s<Not Supported>", indent, "");
        return 1;

    case X509V3_EXT_PARSE_UNKNOWN:
        return ASN1_parse_dump(out, ext, extlen, indent, -1);
    case X509V3_EXT_DUMP_UNKNOWN:
        return BIO_dump_indent(out, (const char *)ext, extlen, indent);

    default:
        return 1;
    }
}

#ifndef OPENSSL_NO_STDIO
int X509V3_EXT_print_fp(FILE *fp, X509_EXTENSION *ext, int flag, int indent)
{
    BIO *bio_tmp;
    int ret;

    if ((bio_tmp = BIO_new_fp(fp, BIO_NOCLOSE)) == NULL)
        return 0;
    ret = X509V3_EXT_print(bio_tmp, ext, flag, indent);
    BIO_free(bio_tmp);
    return ret;
}
#endif
