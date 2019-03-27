/*
 * Copyright 2000-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <string.h>
#include "internal/cryptlib.h"
#include "internal/asn1_int.h"
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/asn1.h>

#include "charmap.h"

/*
 * ASN1_STRING_print_ex() and X509_NAME_print_ex(). Enhanced string and name
 * printing routines handling multibyte characters, RFC2253 and a host of
 * other options.
 */

#define CHARTYPE_BS_ESC         (ASN1_STRFLGS_ESC_2253 | CHARTYPE_FIRST_ESC_2253 | CHARTYPE_LAST_ESC_2253)

#define ESC_FLAGS (ASN1_STRFLGS_ESC_2253 | \
                  ASN1_STRFLGS_ESC_2254 | \
                  ASN1_STRFLGS_ESC_QUOTE | \
                  ASN1_STRFLGS_ESC_CTRL | \
                  ASN1_STRFLGS_ESC_MSB)

/*
 * Three IO functions for sending data to memory, a BIO and and a FILE
 * pointer.
 */
static int send_bio_chars(void *arg, const void *buf, int len)
{
    if (!arg)
        return 1;
    if (BIO_write(arg, buf, len) != len)
        return 0;
    return 1;
}

#ifndef OPENSSL_NO_STDIO
static int send_fp_chars(void *arg, const void *buf, int len)
{
    if (!arg)
        return 1;
    if (fwrite(buf, 1, len, arg) != (unsigned int)len)
        return 0;
    return 1;
}
#endif

typedef int char_io (void *arg, const void *buf, int len);

/*
 * This function handles display of strings, one character at a time. It is
 * passed an unsigned long for each character because it could come from 2 or
 * even 4 byte forms.
 */

static int do_esc_char(unsigned long c, unsigned short flags, char *do_quotes,
                       char_io *io_ch, void *arg)
{
    unsigned short chflgs;
    unsigned char chtmp;
    char tmphex[HEX_SIZE(long) + 3];

    if (c > 0xffffffffL)
        return -1;
    if (c > 0xffff) {
        BIO_snprintf(tmphex, sizeof(tmphex), "\\W%08lX", c);
        if (!io_ch(arg, tmphex, 10))
            return -1;
        return 10;
    }
    if (c > 0xff) {
        BIO_snprintf(tmphex, sizeof(tmphex), "\\U%04lX", c);
        if (!io_ch(arg, tmphex, 6))
            return -1;
        return 6;
    }
    chtmp = (unsigned char)c;
    if (chtmp > 0x7f)
        chflgs = flags & ASN1_STRFLGS_ESC_MSB;
    else
        chflgs = char_type[chtmp] & flags;
    if (chflgs & CHARTYPE_BS_ESC) {
        /* If we don't escape with quotes, signal we need quotes */
        if (chflgs & ASN1_STRFLGS_ESC_QUOTE) {
            if (do_quotes)
                *do_quotes = 1;
            if (!io_ch(arg, &chtmp, 1))
                return -1;
            return 1;
        }
        if (!io_ch(arg, "\\", 1))
            return -1;
        if (!io_ch(arg, &chtmp, 1))
            return -1;
        return 2;
    }
    if (chflgs & (ASN1_STRFLGS_ESC_CTRL
                  | ASN1_STRFLGS_ESC_MSB
                  | ASN1_STRFLGS_ESC_2254)) {
        BIO_snprintf(tmphex, 11, "\\%02X", chtmp);
        if (!io_ch(arg, tmphex, 3))
            return -1;
        return 3;
    }
    /*
     * If we get this far and do any escaping at all must escape the escape
     * character itself: backslash.
     */
    if (chtmp == '\\' && (flags & ESC_FLAGS)) {
        if (!io_ch(arg, "\\\\", 2))
            return -1;
        return 2;
    }
    if (!io_ch(arg, &chtmp, 1))
        return -1;
    return 1;
}

#define BUF_TYPE_WIDTH_MASK     0x7
#define BUF_TYPE_CONVUTF8       0x8

/*
 * This function sends each character in a buffer to do_esc_char(). It
 * interprets the content formats and converts to or from UTF8 as
 * appropriate.
 */

static int do_buf(unsigned char *buf, int buflen,
                  int type, unsigned short flags, char *quotes, char_io *io_ch,
                  void *arg)
{
    int i, outlen, len, charwidth;
    unsigned short orflags;
    unsigned char *p, *q;
    unsigned long c;

    p = buf;
    q = buf + buflen;
    outlen = 0;
    charwidth = type & BUF_TYPE_WIDTH_MASK;

    switch (charwidth) {
    case 4:
        if (buflen & 3) {
            ASN1err(ASN1_F_DO_BUF, ASN1_R_INVALID_UNIVERSALSTRING_LENGTH);
            return -1;
        }
        break;
    case 2:
        if (buflen & 1) {
            ASN1err(ASN1_F_DO_BUF, ASN1_R_INVALID_BMPSTRING_LENGTH);
            return -1;
        }
        break;
    default:
        break;
    }

    while (p != q) {
        if (p == buf && flags & ASN1_STRFLGS_ESC_2253)
            orflags = CHARTYPE_FIRST_ESC_2253;
        else
            orflags = 0;

        switch (charwidth) {
        case 4:
            c = ((unsigned long)*p++) << 24;
            c |= ((unsigned long)*p++) << 16;
            c |= ((unsigned long)*p++) << 8;
            c |= *p++;
            break;

        case 2:
            c = ((unsigned long)*p++) << 8;
            c |= *p++;
            break;

        case 1:
            c = *p++;
            break;

        case 0:
            i = UTF8_getc(p, buflen, &c);
            if (i < 0)
                return -1;      /* Invalid UTF8String */
            buflen -= i;
            p += i;
            break;
        default:
            return -1;          /* invalid width */
        }
        if (p == q && flags & ASN1_STRFLGS_ESC_2253)
            orflags = CHARTYPE_LAST_ESC_2253;
        if (type & BUF_TYPE_CONVUTF8) {
            unsigned char utfbuf[6];
            int utflen;
            utflen = UTF8_putc(utfbuf, sizeof(utfbuf), c);
            for (i = 0; i < utflen; i++) {
                /*
                 * We don't need to worry about setting orflags correctly
                 * because if utflen==1 its value will be correct anyway
                 * otherwise each character will be > 0x7f and so the
                 * character will never be escaped on first and last.
                 */
                len = do_esc_char(utfbuf[i], flags | orflags, quotes,
                                  io_ch, arg);
                if (len < 0)
                    return -1;
                outlen += len;
            }
        } else {
            len = do_esc_char(c, flags | orflags, quotes,
                              io_ch, arg);
            if (len < 0)
                return -1;
            outlen += len;
        }
    }
    return outlen;
}

/* This function hex dumps a buffer of characters */

static int do_hex_dump(char_io *io_ch, void *arg, unsigned char *buf,
                       int buflen)
{
    static const char hexdig[] = "0123456789ABCDEF";
    unsigned char *p, *q;
    char hextmp[2];
    if (arg) {
        p = buf;
        q = buf + buflen;
        while (p != q) {
            hextmp[0] = hexdig[*p >> 4];
            hextmp[1] = hexdig[*p & 0xf];
            if (!io_ch(arg, hextmp, 2))
                return -1;
            p++;
        }
    }
    return buflen << 1;
}

/*
 * "dump" a string. This is done when the type is unknown, or the flags
 * request it. We can either dump the content octets or the entire DER
 * encoding. This uses the RFC2253 #01234 format.
 */

static int do_dump(unsigned long lflags, char_io *io_ch, void *arg,
                   const ASN1_STRING *str)
{
    /*
     * Placing the ASN1_STRING in a temp ASN1_TYPE allows the DER encoding to
     * readily obtained
     */
    ASN1_TYPE t;
    unsigned char *der_buf, *p;
    int outlen, der_len;

    if (!io_ch(arg, "#", 1))
        return -1;
    /* If we don't dump DER encoding just dump content octets */
    if (!(lflags & ASN1_STRFLGS_DUMP_DER)) {
        outlen = do_hex_dump(io_ch, arg, str->data, str->length);
        if (outlen < 0)
            return -1;
        return outlen + 1;
    }
    t.type = str->type;
    t.value.ptr = (char *)str;
    der_len = i2d_ASN1_TYPE(&t, NULL);
    if ((der_buf = OPENSSL_malloc(der_len)) == NULL) {
        ASN1err(ASN1_F_DO_DUMP, ERR_R_MALLOC_FAILURE);
        return -1;
    }
    p = der_buf;
    i2d_ASN1_TYPE(&t, &p);
    outlen = do_hex_dump(io_ch, arg, der_buf, der_len);
    OPENSSL_free(der_buf);
    if (outlen < 0)
        return -1;
    return outlen + 1;
}

/*
 * Lookup table to convert tags to character widths, 0 = UTF8 encoded, -1 is
 * used for non string types otherwise it is the number of bytes per
 * character
 */

static const signed char tag2nbyte[] = {
    -1, -1, -1, -1, -1,         /* 0-4 */
    -1, -1, -1, -1, -1,         /* 5-9 */
    -1, -1,                     /* 10-11 */
     0,                         /* 12 V_ASN1_UTF8STRING */
    -1, -1, -1, -1, -1,         /* 13-17 */
     1,                         /* 18 V_ASN1_NUMERICSTRING */
     1,                         /* 19 V_ASN1_PRINTABLESTRING */
     1,                         /* 20 V_ASN1_T61STRING */
    -1,                         /* 21 */
     1,                         /* 22 V_ASN1_IA5STRING */
     1,                         /* 23 V_ASN1_UTCTIME */
     1,                         /* 24 V_ASN1_GENERALIZEDTIME */
    -1,                         /* 25 */
     1,                         /* 26 V_ASN1_ISO64STRING */
    -1,                         /* 27 */
     4,                         /* 28 V_ASN1_UNIVERSALSTRING */
    -1,                         /* 29 */
     2                          /* 30 V_ASN1_BMPSTRING */
};

/*
 * This is the main function, print out an ASN1_STRING taking note of various
 * escape and display options. Returns number of characters written or -1 if
 * an error occurred.
 */

static int do_print_ex(char_io *io_ch, void *arg, unsigned long lflags,
                       const ASN1_STRING *str)
{
    int outlen, len;
    int type;
    char quotes;
    unsigned short flags;
    quotes = 0;
    /* Keep a copy of escape flags */
    flags = (unsigned short)(lflags & ESC_FLAGS);

    type = str->type;

    outlen = 0;

    if (lflags & ASN1_STRFLGS_SHOW_TYPE) {
        const char *tagname;
        tagname = ASN1_tag2str(type);
        outlen += strlen(tagname);
        if (!io_ch(arg, tagname, outlen) || !io_ch(arg, ":", 1))
            return -1;
        outlen++;
    }

    /* Decide what to do with type, either dump content or display it */

    /* Dump everything */
    if (lflags & ASN1_STRFLGS_DUMP_ALL)
        type = -1;
    /* Ignore the string type */
    else if (lflags & ASN1_STRFLGS_IGNORE_TYPE)
        type = 1;
    else {
        /* Else determine width based on type */
        if ((type > 0) && (type < 31))
            type = tag2nbyte[type];
        else
            type = -1;
        if ((type == -1) && !(lflags & ASN1_STRFLGS_DUMP_UNKNOWN))
            type = 1;
    }

    if (type == -1) {
        len = do_dump(lflags, io_ch, arg, str);
        if (len < 0)
            return -1;
        outlen += len;
        return outlen;
    }

    if (lflags & ASN1_STRFLGS_UTF8_CONVERT) {
        /*
         * Note: if string is UTF8 and we want to convert to UTF8 then we
         * just interpret it as 1 byte per character to avoid converting
         * twice.
         */
        if (!type)
            type = 1;
        else
            type |= BUF_TYPE_CONVUTF8;
    }

    len = do_buf(str->data, str->length, type, flags, &quotes, io_ch, NULL);
    if (len < 0)
        return -1;
    outlen += len;
    if (quotes)
        outlen += 2;
    if (!arg)
        return outlen;
    if (quotes && !io_ch(arg, "\"", 1))
        return -1;
    if (do_buf(str->data, str->length, type, flags, NULL, io_ch, arg) < 0)
        return -1;
    if (quotes && !io_ch(arg, "\"", 1))
        return -1;
    return outlen;
}

/* Used for line indenting: print 'indent' spaces */

static int do_indent(char_io *io_ch, void *arg, int indent)
{
    int i;
    for (i = 0; i < indent; i++)
        if (!io_ch(arg, " ", 1))
            return 0;
    return 1;
}

#define FN_WIDTH_LN     25
#define FN_WIDTH_SN     10

static int do_name_ex(char_io *io_ch, void *arg, const X509_NAME *n,
                      int indent, unsigned long flags)
{
    int i, prev = -1, orflags, cnt;
    int fn_opt, fn_nid;
    ASN1_OBJECT *fn;
    const ASN1_STRING *val;
    const X509_NAME_ENTRY *ent;
    char objtmp[80];
    const char *objbuf;
    int outlen, len;
    char *sep_dn, *sep_mv, *sep_eq;
    int sep_dn_len, sep_mv_len, sep_eq_len;
    if (indent < 0)
        indent = 0;
    outlen = indent;
    if (!do_indent(io_ch, arg, indent))
        return -1;
    switch (flags & XN_FLAG_SEP_MASK) {
    case XN_FLAG_SEP_MULTILINE:
        sep_dn = "\n";
        sep_dn_len = 1;
        sep_mv = " + ";
        sep_mv_len = 3;
        break;

    case XN_FLAG_SEP_COMMA_PLUS:
        sep_dn = ",";
        sep_dn_len = 1;
        sep_mv = "+";
        sep_mv_len = 1;
        indent = 0;
        break;

    case XN_FLAG_SEP_CPLUS_SPC:
        sep_dn = ", ";
        sep_dn_len = 2;
        sep_mv = " + ";
        sep_mv_len = 3;
        indent = 0;
        break;

    case XN_FLAG_SEP_SPLUS_SPC:
        sep_dn = "; ";
        sep_dn_len = 2;
        sep_mv = " + ";
        sep_mv_len = 3;
        indent = 0;
        break;

    default:
        return -1;
    }

    if (flags & XN_FLAG_SPC_EQ) {
        sep_eq = " = ";
        sep_eq_len = 3;
    } else {
        sep_eq = "=";
        sep_eq_len = 1;
    }

    fn_opt = flags & XN_FLAG_FN_MASK;

    cnt = X509_NAME_entry_count(n);
    for (i = 0; i < cnt; i++) {
        if (flags & XN_FLAG_DN_REV)
            ent = X509_NAME_get_entry(n, cnt - i - 1);
        else
            ent = X509_NAME_get_entry(n, i);
        if (prev != -1) {
            if (prev == X509_NAME_ENTRY_set(ent)) {
                if (!io_ch(arg, sep_mv, sep_mv_len))
                    return -1;
                outlen += sep_mv_len;
            } else {
                if (!io_ch(arg, sep_dn, sep_dn_len))
                    return -1;
                outlen += sep_dn_len;
                if (!do_indent(io_ch, arg, indent))
                    return -1;
                outlen += indent;
            }
        }
        prev = X509_NAME_ENTRY_set(ent);
        fn = X509_NAME_ENTRY_get_object(ent);
        val = X509_NAME_ENTRY_get_data(ent);
        fn_nid = OBJ_obj2nid(fn);
        if (fn_opt != XN_FLAG_FN_NONE) {
            int objlen, fld_len;
            if ((fn_opt == XN_FLAG_FN_OID) || (fn_nid == NID_undef)) {
                OBJ_obj2txt(objtmp, sizeof(objtmp), fn, 1);
                fld_len = 0;    /* XXX: what should this be? */
                objbuf = objtmp;
            } else {
                if (fn_opt == XN_FLAG_FN_SN) {
                    fld_len = FN_WIDTH_SN;
                    objbuf = OBJ_nid2sn(fn_nid);
                } else if (fn_opt == XN_FLAG_FN_LN) {
                    fld_len = FN_WIDTH_LN;
                    objbuf = OBJ_nid2ln(fn_nid);
                } else {
                    fld_len = 0; /* XXX: what should this be? */
                    objbuf = "";
                }
            }
            objlen = strlen(objbuf);
            if (!io_ch(arg, objbuf, objlen))
                return -1;
            if ((objlen < fld_len) && (flags & XN_FLAG_FN_ALIGN)) {
                if (!do_indent(io_ch, arg, fld_len - objlen))
                    return -1;
                outlen += fld_len - objlen;
            }
            if (!io_ch(arg, sep_eq, sep_eq_len))
                return -1;
            outlen += objlen + sep_eq_len;
        }
        /*
         * If the field name is unknown then fix up the DER dump flag. We
         * might want to limit this further so it will DER dump on anything
         * other than a few 'standard' fields.
         */
        if ((fn_nid == NID_undef) && (flags & XN_FLAG_DUMP_UNKNOWN_FIELDS))
            orflags = ASN1_STRFLGS_DUMP_ALL;
        else
            orflags = 0;

        len = do_print_ex(io_ch, arg, flags | orflags, val);
        if (len < 0)
            return -1;
        outlen += len;
    }
    return outlen;
}

/* Wrappers round the main functions */

int X509_NAME_print_ex(BIO *out, const X509_NAME *nm, int indent,
                       unsigned long flags)
{
    if (flags == XN_FLAG_COMPAT)
        return X509_NAME_print(out, nm, indent);
    return do_name_ex(send_bio_chars, out, nm, indent, flags);
}

#ifndef OPENSSL_NO_STDIO
int X509_NAME_print_ex_fp(FILE *fp, const X509_NAME *nm, int indent,
                          unsigned long flags)
{
    if (flags == XN_FLAG_COMPAT) {
        BIO *btmp;
        int ret;
        btmp = BIO_new_fp(fp, BIO_NOCLOSE);
        if (!btmp)
            return -1;
        ret = X509_NAME_print(btmp, nm, indent);
        BIO_free(btmp);
        return ret;
    }
    return do_name_ex(send_fp_chars, fp, nm, indent, flags);
}
#endif

int ASN1_STRING_print_ex(BIO *out, const ASN1_STRING *str, unsigned long flags)
{
    return do_print_ex(send_bio_chars, out, flags, str);
}

#ifndef OPENSSL_NO_STDIO
int ASN1_STRING_print_ex_fp(FILE *fp, const ASN1_STRING *str, unsigned long flags)
{
    return do_print_ex(send_fp_chars, fp, flags, str);
}
#endif

/*
 * Utility function: convert any string type to UTF8, returns number of bytes
 * in output string or a negative error code
 */

int ASN1_STRING_to_UTF8(unsigned char **out, const ASN1_STRING *in)
{
    ASN1_STRING stmp, *str = &stmp;
    int mbflag, type, ret;
    if (!in)
        return -1;
    type = in->type;
    if ((type < 0) || (type > 30))
        return -1;
    mbflag = tag2nbyte[type];
    if (mbflag == -1)
        return -1;
    mbflag |= MBSTRING_FLAG;
    stmp.data = NULL;
    stmp.length = 0;
    stmp.flags = 0;
    ret =
        ASN1_mbstring_copy(&str, in->data, in->length, mbflag,
                           B_ASN1_UTF8STRING);
    if (ret < 0)
        return ret;
    *out = stmp.data;
    return stmp.length;
}
