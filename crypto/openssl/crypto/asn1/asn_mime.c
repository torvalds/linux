/*
 * Copyright 2008-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/ctype.h"
#include "internal/cryptlib.h"
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include "internal/evp_int.h"
#include "internal/bio.h"
#include "asn1_locl.h"

/*
 * Generalised MIME like utilities for streaming ASN1. Although many have a
 * PKCS7/CMS like flavour others are more general purpose.
 */

/*
 * MIME format structures Note that all are translated to lower case apart
 * from parameter values. Quotes are stripped off
 */

struct mime_param_st {
    char *param_name;           /* Param name e.g. "micalg" */
    char *param_value;          /* Param value e.g. "sha1" */
};

struct mime_header_st {
    char *name;                 /* Name of line e.g. "content-type" */
    char *value;                /* Value of line e.g. "text/plain" */
    STACK_OF(MIME_PARAM) *params; /* Zero or more parameters */
};

static int asn1_output_data(BIO *out, BIO *data, ASN1_VALUE *val, int flags,
                            const ASN1_ITEM *it);
static char *strip_ends(char *name);
static char *strip_start(char *name);
static char *strip_end(char *name);
static MIME_HEADER *mime_hdr_new(const char *name, const char *value);
static int mime_hdr_addparam(MIME_HEADER *mhdr, const char *name, const char *value);
static STACK_OF(MIME_HEADER) *mime_parse_hdr(BIO *bio);
static int mime_hdr_cmp(const MIME_HEADER *const *a,
                        const MIME_HEADER *const *b);
static int mime_param_cmp(const MIME_PARAM *const *a,
                          const MIME_PARAM *const *b);
static void mime_param_free(MIME_PARAM *param);
static int mime_bound_check(char *line, int linelen, const char *bound, int blen);
static int multi_split(BIO *bio, const char *bound, STACK_OF(BIO) **ret);
static int strip_eol(char *linebuf, int *plen, int flags);
static MIME_HEADER *mime_hdr_find(STACK_OF(MIME_HEADER) *hdrs, const char *name);
static MIME_PARAM *mime_param_find(MIME_HEADER *hdr, const char *name);
static void mime_hdr_free(MIME_HEADER *hdr);

#define MAX_SMLEN 1024
#define mime_debug(x)           /* x */

/* Output an ASN1 structure in BER format streaming if necessary */

int i2d_ASN1_bio_stream(BIO *out, ASN1_VALUE *val, BIO *in, int flags,
                        const ASN1_ITEM *it)
{
    /* If streaming create stream BIO and copy all content through it */
    if (flags & SMIME_STREAM) {
        BIO *bio, *tbio;
        bio = BIO_new_NDEF(out, val, it);
        if (!bio) {
            ASN1err(ASN1_F_I2D_ASN1_BIO_STREAM, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        SMIME_crlf_copy(in, bio, flags);
        (void)BIO_flush(bio);
        /* Free up successive BIOs until we hit the old output BIO */
        do {
            tbio = BIO_pop(bio);
            BIO_free(bio);
            bio = tbio;
        } while (bio != out);
    }
    /*
     * else just write out ASN1 structure which will have all content stored
     * internally
     */
    else
        ASN1_item_i2d_bio(it, out, val);
    return 1;
}

/* Base 64 read and write of ASN1 structure */

static int B64_write_ASN1(BIO *out, ASN1_VALUE *val, BIO *in, int flags,
                          const ASN1_ITEM *it)
{
    BIO *b64;
    int r;
    b64 = BIO_new(BIO_f_base64());
    if (b64 == NULL) {
        ASN1err(ASN1_F_B64_WRITE_ASN1, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    /*
     * prepend the b64 BIO so all data is base64 encoded.
     */
    out = BIO_push(b64, out);
    r = i2d_ASN1_bio_stream(out, val, in, flags, it);
    (void)BIO_flush(out);
    BIO_pop(out);
    BIO_free(b64);
    return r;
}

/* Streaming ASN1 PEM write */

int PEM_write_bio_ASN1_stream(BIO *out, ASN1_VALUE *val, BIO *in, int flags,
                              const char *hdr, const ASN1_ITEM *it)
{
    int r;
    BIO_printf(out, "-----BEGIN %s-----\n", hdr);
    r = B64_write_ASN1(out, val, in, flags, it);
    BIO_printf(out, "-----END %s-----\n", hdr);
    return r;
}

static ASN1_VALUE *b64_read_asn1(BIO *bio, const ASN1_ITEM *it)
{
    BIO *b64;
    ASN1_VALUE *val;

    if ((b64 = BIO_new(BIO_f_base64())) == NULL) {
        ASN1err(ASN1_F_B64_READ_ASN1, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    bio = BIO_push(b64, bio);
    val = ASN1_item_d2i_bio(it, bio, NULL);
    if (!val)
        ASN1err(ASN1_F_B64_READ_ASN1, ASN1_R_DECODE_ERROR);
    (void)BIO_flush(bio);
    BIO_pop(bio);
    BIO_free(b64);
    return val;
}

/* Generate the MIME "micalg" parameter from RFC3851, RFC4490 */

static int asn1_write_micalg(BIO *out, STACK_OF(X509_ALGOR) *mdalgs)
{
    const EVP_MD *md;
    int i, have_unknown = 0, write_comma, ret = 0, md_nid;
    have_unknown = 0;
    write_comma = 0;
    for (i = 0; i < sk_X509_ALGOR_num(mdalgs); i++) {
        if (write_comma)
            BIO_write(out, ",", 1);
        write_comma = 1;
        md_nid = OBJ_obj2nid(sk_X509_ALGOR_value(mdalgs, i)->algorithm);
        md = EVP_get_digestbynid(md_nid);
        if (md && md->md_ctrl) {
            int rv;
            char *micstr;
            rv = md->md_ctrl(NULL, EVP_MD_CTRL_MICALG, 0, &micstr);
            if (rv > 0) {
                BIO_puts(out, micstr);
                OPENSSL_free(micstr);
                continue;
            }
            if (rv != -2)
                goto err;
        }
        switch (md_nid) {
        case NID_sha1:
            BIO_puts(out, "sha1");
            break;

        case NID_md5:
            BIO_puts(out, "md5");
            break;

        case NID_sha256:
            BIO_puts(out, "sha-256");
            break;

        case NID_sha384:
            BIO_puts(out, "sha-384");
            break;

        case NID_sha512:
            BIO_puts(out, "sha-512");
            break;

        case NID_id_GostR3411_94:
            BIO_puts(out, "gostr3411-94");
            goto err;

        default:
            if (have_unknown)
                write_comma = 0;
            else {
                BIO_puts(out, "unknown");
                have_unknown = 1;
            }
            break;

        }
    }

    ret = 1;
 err:

    return ret;

}

/* SMIME sender */

int SMIME_write_ASN1(BIO *bio, ASN1_VALUE *val, BIO *data, int flags,
                     int ctype_nid, int econt_nid,
                     STACK_OF(X509_ALGOR) *mdalgs, const ASN1_ITEM *it)
{
    char bound[33], c;
    int i;
    const char *mime_prefix, *mime_eol, *cname = "smime.p7m";
    const char *msg_type = NULL;
    if (flags & SMIME_OLDMIME)
        mime_prefix = "application/x-pkcs7-";
    else
        mime_prefix = "application/pkcs7-";

    if (flags & SMIME_CRLFEOL)
        mime_eol = "\r\n";
    else
        mime_eol = "\n";
    if ((flags & SMIME_DETACHED) && data) {
        /* We want multipart/signed */
        /* Generate a random boundary */
        if (RAND_bytes((unsigned char *)bound, 32) <= 0)
            return 0;
        for (i = 0; i < 32; i++) {
            c = bound[i] & 0xf;
            if (c < 10)
                c += '0';
            else
                c += 'A' - 10;
            bound[i] = c;
        }
        bound[32] = 0;
        BIO_printf(bio, "MIME-Version: 1.0%s", mime_eol);
        BIO_printf(bio, "Content-Type: multipart/signed;");
        BIO_printf(bio, " protocol=\"%ssignature\";", mime_prefix);
        BIO_puts(bio, " micalg=\"");
        asn1_write_micalg(bio, mdalgs);
        BIO_printf(bio, "\"; boundary=\"----%s\"%s%s",
                   bound, mime_eol, mime_eol);
        BIO_printf(bio, "This is an S/MIME signed message%s%s",
                   mime_eol, mime_eol);
        /* Now write out the first part */
        BIO_printf(bio, "------%s%s", bound, mime_eol);
        if (!asn1_output_data(bio, data, val, flags, it))
            return 0;
        BIO_printf(bio, "%s------%s%s", mime_eol, bound, mime_eol);

        /* Headers for signature */

        BIO_printf(bio, "Content-Type: %ssignature;", mime_prefix);
        BIO_printf(bio, " name=\"smime.p7s\"%s", mime_eol);
        BIO_printf(bio, "Content-Transfer-Encoding: base64%s", mime_eol);
        BIO_printf(bio, "Content-Disposition: attachment;");
        BIO_printf(bio, " filename=\"smime.p7s\"%s%s", mime_eol, mime_eol);
        B64_write_ASN1(bio, val, NULL, 0, it);
        BIO_printf(bio, "%s------%s--%s%s", mime_eol, bound,
                   mime_eol, mime_eol);
        return 1;
    }

    /* Determine smime-type header */

    if (ctype_nid == NID_pkcs7_enveloped)
        msg_type = "enveloped-data";
    else if (ctype_nid == NID_pkcs7_signed) {
        if (econt_nid == NID_id_smime_ct_receipt)
            msg_type = "signed-receipt";
        else if (sk_X509_ALGOR_num(mdalgs) >= 0)
            msg_type = "signed-data";
        else
            msg_type = "certs-only";
    } else if (ctype_nid == NID_id_smime_ct_compressedData) {
        msg_type = "compressed-data";
        cname = "smime.p7z";
    }
    /* MIME headers */
    BIO_printf(bio, "MIME-Version: 1.0%s", mime_eol);
    BIO_printf(bio, "Content-Disposition: attachment;");
    BIO_printf(bio, " filename=\"%s\"%s", cname, mime_eol);
    BIO_printf(bio, "Content-Type: %smime;", mime_prefix);
    if (msg_type)
        BIO_printf(bio, " smime-type=%s;", msg_type);
    BIO_printf(bio, " name=\"%s\"%s", cname, mime_eol);
    BIO_printf(bio, "Content-Transfer-Encoding: base64%s%s",
               mime_eol, mime_eol);
    if (!B64_write_ASN1(bio, val, data, flags, it))
        return 0;
    BIO_printf(bio, "%s", mime_eol);
    return 1;
}

/* Handle output of ASN1 data */

static int asn1_output_data(BIO *out, BIO *data, ASN1_VALUE *val, int flags,
                            const ASN1_ITEM *it)
{
    BIO *tmpbio;
    const ASN1_AUX *aux = it->funcs;
    ASN1_STREAM_ARG sarg;
    int rv = 1;

    /*
     * If data is not detached or resigning then the output BIO is already
     * set up to finalise when it is written through.
     */
    if (!(flags & SMIME_DETACHED) || (flags & PKCS7_REUSE_DIGEST)) {
        SMIME_crlf_copy(data, out, flags);
        return 1;
    }

    if (!aux || !aux->asn1_cb) {
        ASN1err(ASN1_F_ASN1_OUTPUT_DATA, ASN1_R_STREAMING_NOT_SUPPORTED);
        return 0;
    }

    sarg.out = out;
    sarg.ndef_bio = NULL;
    sarg.boundary = NULL;

    /* Let ASN1 code prepend any needed BIOs */

    if (aux->asn1_cb(ASN1_OP_DETACHED_PRE, &val, it, &sarg) <= 0)
        return 0;

    /* Copy data across, passing through filter BIOs for processing */
    SMIME_crlf_copy(data, sarg.ndef_bio, flags);

    /* Finalize structure */
    if (aux->asn1_cb(ASN1_OP_DETACHED_POST, &val, it, &sarg) <= 0)
        rv = 0;

    /* Now remove any digests prepended to the BIO */

    while (sarg.ndef_bio != out) {
        tmpbio = BIO_pop(sarg.ndef_bio);
        BIO_free(sarg.ndef_bio);
        sarg.ndef_bio = tmpbio;
    }

    return rv;

}

/*
 * SMIME reader: handle multipart/signed and opaque signing. in multipart
 * case the content is placed in a memory BIO pointed to by "bcont". In
 * opaque this is set to NULL
 */

ASN1_VALUE *SMIME_read_ASN1(BIO *bio, BIO **bcont, const ASN1_ITEM *it)
{
    BIO *asnin;
    STACK_OF(MIME_HEADER) *headers = NULL;
    STACK_OF(BIO) *parts = NULL;
    MIME_HEADER *hdr;
    MIME_PARAM *prm;
    ASN1_VALUE *val;
    int ret;

    if (bcont)
        *bcont = NULL;

    if ((headers = mime_parse_hdr(bio)) == NULL) {
        ASN1err(ASN1_F_SMIME_READ_ASN1, ASN1_R_MIME_PARSE_ERROR);
        return NULL;
    }

    if ((hdr = mime_hdr_find(headers, "content-type")) == NULL
        || hdr->value == NULL) {
        sk_MIME_HEADER_pop_free(headers, mime_hdr_free);
        ASN1err(ASN1_F_SMIME_READ_ASN1, ASN1_R_NO_CONTENT_TYPE);
        return NULL;
    }

    /* Handle multipart/signed */

    if (strcmp(hdr->value, "multipart/signed") == 0) {
        /* Split into two parts */
        prm = mime_param_find(hdr, "boundary");
        if (!prm || !prm->param_value) {
            sk_MIME_HEADER_pop_free(headers, mime_hdr_free);
            ASN1err(ASN1_F_SMIME_READ_ASN1, ASN1_R_NO_MULTIPART_BOUNDARY);
            return NULL;
        }
        ret = multi_split(bio, prm->param_value, &parts);
        sk_MIME_HEADER_pop_free(headers, mime_hdr_free);
        if (!ret || (sk_BIO_num(parts) != 2)) {
            ASN1err(ASN1_F_SMIME_READ_ASN1, ASN1_R_NO_MULTIPART_BODY_FAILURE);
            sk_BIO_pop_free(parts, BIO_vfree);
            return NULL;
        }

        /* Parse the signature piece */
        asnin = sk_BIO_value(parts, 1);

        if ((headers = mime_parse_hdr(asnin)) == NULL) {
            ASN1err(ASN1_F_SMIME_READ_ASN1, ASN1_R_MIME_SIG_PARSE_ERROR);
            sk_BIO_pop_free(parts, BIO_vfree);
            return NULL;
        }

        /* Get content type */

        if ((hdr = mime_hdr_find(headers, "content-type")) == NULL
            || hdr->value == NULL) {
            sk_MIME_HEADER_pop_free(headers, mime_hdr_free);
            ASN1err(ASN1_F_SMIME_READ_ASN1, ASN1_R_NO_SIG_CONTENT_TYPE);
            sk_BIO_pop_free(parts, BIO_vfree);
            return NULL;
        }

        if (strcmp(hdr->value, "application/x-pkcs7-signature") &&
            strcmp(hdr->value, "application/pkcs7-signature")) {
            ASN1err(ASN1_F_SMIME_READ_ASN1, ASN1_R_SIG_INVALID_MIME_TYPE);
            ERR_add_error_data(2, "type: ", hdr->value);
            sk_MIME_HEADER_pop_free(headers, mime_hdr_free);
            sk_BIO_pop_free(parts, BIO_vfree);
            return NULL;
        }
        sk_MIME_HEADER_pop_free(headers, mime_hdr_free);
        /* Read in ASN1 */
        if ((val = b64_read_asn1(asnin, it)) == NULL) {
            ASN1err(ASN1_F_SMIME_READ_ASN1, ASN1_R_ASN1_SIG_PARSE_ERROR);
            sk_BIO_pop_free(parts, BIO_vfree);
            return NULL;
        }

        if (bcont) {
            *bcont = sk_BIO_value(parts, 0);
            BIO_free(asnin);
            sk_BIO_free(parts);
        } else
            sk_BIO_pop_free(parts, BIO_vfree);
        return val;
    }

    /* OK, if not multipart/signed try opaque signature */

    if (strcmp(hdr->value, "application/x-pkcs7-mime") &&
        strcmp(hdr->value, "application/pkcs7-mime")) {
        ASN1err(ASN1_F_SMIME_READ_ASN1, ASN1_R_INVALID_MIME_TYPE);
        ERR_add_error_data(2, "type: ", hdr->value);
        sk_MIME_HEADER_pop_free(headers, mime_hdr_free);
        return NULL;
    }

    sk_MIME_HEADER_pop_free(headers, mime_hdr_free);

    if ((val = b64_read_asn1(bio, it)) == NULL) {
        ASN1err(ASN1_F_SMIME_READ_ASN1, ASN1_R_ASN1_PARSE_ERROR);
        return NULL;
    }
    return val;

}

/* Copy text from one BIO to another making the output CRLF at EOL */
int SMIME_crlf_copy(BIO *in, BIO *out, int flags)
{
    BIO *bf;
    char eol;
    int len;
    char linebuf[MAX_SMLEN];
    /*
     * Buffer output so we don't write one line at a time. This is useful
     * when streaming as we don't end up with one OCTET STRING per line.
     */
    bf = BIO_new(BIO_f_buffer());
    if (bf == NULL)
        return 0;
    out = BIO_push(bf, out);
    if (flags & SMIME_BINARY) {
        while ((len = BIO_read(in, linebuf, MAX_SMLEN)) > 0)
            BIO_write(out, linebuf, len);
    } else {
        int eolcnt = 0;
        if (flags & SMIME_TEXT)
            BIO_printf(out, "Content-Type: text/plain\r\n\r\n");
        while ((len = BIO_gets(in, linebuf, MAX_SMLEN)) > 0) {
            eol = strip_eol(linebuf, &len, flags);
            if (len) {
                /* Not EOF: write out all CRLF */
                if (flags & SMIME_ASCIICRLF) {
                    int i;
                    for (i = 0; i < eolcnt; i++)
                        BIO_write(out, "\r\n", 2);
                    eolcnt = 0;
                }
                BIO_write(out, linebuf, len);
                if (eol)
                    BIO_write(out, "\r\n", 2);
            } else if (flags & SMIME_ASCIICRLF)
                eolcnt++;
            else if (eol)
                BIO_write(out, "\r\n", 2);
        }
    }
    (void)BIO_flush(out);
    BIO_pop(out);
    BIO_free(bf);
    return 1;
}

/* Strip off headers if they are text/plain */
int SMIME_text(BIO *in, BIO *out)
{
    char iobuf[4096];
    int len;
    STACK_OF(MIME_HEADER) *headers;
    MIME_HEADER *hdr;

    if ((headers = mime_parse_hdr(in)) == NULL) {
        ASN1err(ASN1_F_SMIME_TEXT, ASN1_R_MIME_PARSE_ERROR);
        return 0;
    }
    if ((hdr = mime_hdr_find(headers, "content-type")) == NULL
        || hdr->value == NULL) {
        ASN1err(ASN1_F_SMIME_TEXT, ASN1_R_MIME_NO_CONTENT_TYPE);
        sk_MIME_HEADER_pop_free(headers, mime_hdr_free);
        return 0;
    }
    if (strcmp(hdr->value, "text/plain")) {
        ASN1err(ASN1_F_SMIME_TEXT, ASN1_R_INVALID_MIME_TYPE);
        ERR_add_error_data(2, "type: ", hdr->value);
        sk_MIME_HEADER_pop_free(headers, mime_hdr_free);
        return 0;
    }
    sk_MIME_HEADER_pop_free(headers, mime_hdr_free);
    while ((len = BIO_read(in, iobuf, sizeof(iobuf))) > 0)
        BIO_write(out, iobuf, len);
    if (len < 0)
        return 0;
    return 1;
}

/*
 * Split a multipart/XXX message body into component parts: result is
 * canonical parts in a STACK of bios
 */

static int multi_split(BIO *bio, const char *bound, STACK_OF(BIO) **ret)
{
    char linebuf[MAX_SMLEN];
    int len, blen;
    int eol = 0, next_eol = 0;
    BIO *bpart = NULL;
    STACK_OF(BIO) *parts;
    char state, part, first;

    blen = strlen(bound);
    part = 0;
    state = 0;
    first = 1;
    parts = sk_BIO_new_null();
    *ret = parts;
    if (*ret == NULL)
        return 0;
    while ((len = BIO_gets(bio, linebuf, MAX_SMLEN)) > 0) {
        state = mime_bound_check(linebuf, len, bound, blen);
        if (state == 1) {
            first = 1;
            part++;
        } else if (state == 2) {
            if (!sk_BIO_push(parts, bpart)) {
                BIO_free(bpart);
                return 0;
            }
            return 1;
        } else if (part) {
            /* Strip CR+LF from linebuf */
            next_eol = strip_eol(linebuf, &len, 0);
            if (first) {
                first = 0;
                if (bpart)
                    if (!sk_BIO_push(parts, bpart)) {
                        BIO_free(bpart);
                        return 0;
                    }
                bpart = BIO_new(BIO_s_mem());
                if (bpart == NULL)
                    return 0;
                BIO_set_mem_eof_return(bpart, 0);
            } else if (eol)
                BIO_write(bpart, "\r\n", 2);
            eol = next_eol;
            if (len)
                BIO_write(bpart, linebuf, len);
        }
    }
    BIO_free(bpart);
    return 0;
}

/* This is the big one: parse MIME header lines up to message body */

#define MIME_INVALID    0
#define MIME_START      1
#define MIME_TYPE       2
#define MIME_NAME       3
#define MIME_VALUE      4
#define MIME_QUOTE      5
#define MIME_COMMENT    6

static STACK_OF(MIME_HEADER) *mime_parse_hdr(BIO *bio)
{
    char *p, *q, c;
    char *ntmp;
    char linebuf[MAX_SMLEN];
    MIME_HEADER *mhdr = NULL, *new_hdr = NULL;
    STACK_OF(MIME_HEADER) *headers;
    int len, state, save_state = 0;

    headers = sk_MIME_HEADER_new(mime_hdr_cmp);
    if (headers == NULL)
        return NULL;
    while ((len = BIO_gets(bio, linebuf, MAX_SMLEN)) > 0) {
        /* If whitespace at line start then continuation line */
        if (mhdr && ossl_isspace(linebuf[0]))
            state = MIME_NAME;
        else
            state = MIME_START;
        ntmp = NULL;
        /* Go through all characters */
        for (p = linebuf, q = linebuf; (c = *p) && (c != '\r') && (c != '\n');
             p++) {

            /*
             * State machine to handle MIME headers if this looks horrible
             * that's because it *is*
             */

            switch (state) {
            case MIME_START:
                if (c == ':') {
                    state = MIME_TYPE;
                    *p = 0;
                    ntmp = strip_ends(q);
                    q = p + 1;
                }
                break;

            case MIME_TYPE:
                if (c == ';') {
                    mime_debug("Found End Value\n");
                    *p = 0;
                    new_hdr = mime_hdr_new(ntmp, strip_ends(q));
                    if (new_hdr == NULL)
                        goto err;
                    if (!sk_MIME_HEADER_push(headers, new_hdr))
                        goto err;
                    mhdr = new_hdr;
                    new_hdr = NULL;
                    ntmp = NULL;
                    q = p + 1;
                    state = MIME_NAME;
                } else if (c == '(') {
                    save_state = state;
                    state = MIME_COMMENT;
                }
                break;

            case MIME_COMMENT:
                if (c == ')') {
                    state = save_state;
                }
                break;

            case MIME_NAME:
                if (c == '=') {
                    state = MIME_VALUE;
                    *p = 0;
                    ntmp = strip_ends(q);
                    q = p + 1;
                }
                break;

            case MIME_VALUE:
                if (c == ';') {
                    state = MIME_NAME;
                    *p = 0;
                    mime_hdr_addparam(mhdr, ntmp, strip_ends(q));
                    ntmp = NULL;
                    q = p + 1;
                } else if (c == '"') {
                    mime_debug("Found Quote\n");
                    state = MIME_QUOTE;
                } else if (c == '(') {
                    save_state = state;
                    state = MIME_COMMENT;
                }
                break;

            case MIME_QUOTE:
                if (c == '"') {
                    mime_debug("Found Match Quote\n");
                    state = MIME_VALUE;
                }
                break;
            }
        }

        if (state == MIME_TYPE) {
            new_hdr = mime_hdr_new(ntmp, strip_ends(q));
            if (new_hdr == NULL)
                goto err;
            if (!sk_MIME_HEADER_push(headers, new_hdr))
                goto err;
            mhdr = new_hdr;
            new_hdr = NULL;
        } else if (state == MIME_VALUE)
            mime_hdr_addparam(mhdr, ntmp, strip_ends(q));
        if (p == linebuf)
            break;              /* Blank line means end of headers */
    }

    return headers;

err:
    mime_hdr_free(new_hdr);
    sk_MIME_HEADER_pop_free(headers, mime_hdr_free);
    return NULL;
}

static char *strip_ends(char *name)
{
    return strip_end(strip_start(name));
}

/* Strip a parameter of whitespace from start of param */
static char *strip_start(char *name)
{
    char *p, c;
    /* Look for first non white space or quote */
    for (p = name; (c = *p); p++) {
        if (c == '"') {
            /* Next char is start of string if non null */
            if (p[1])
                return p + 1;
            /* Else null string */
            return NULL;
        }
        if (!ossl_isspace(c))
            return p;
    }
    return NULL;
}

/* As above but strip from end of string : maybe should handle brackets? */
static char *strip_end(char *name)
{
    char *p, c;
    if (!name)
        return NULL;
    /* Look for first non white space or quote */
    for (p = name + strlen(name) - 1; p >= name; p--) {
        c = *p;
        if (c == '"') {
            if (p - 1 == name)
                return NULL;
            *p = 0;
            return name;
        }
        if (ossl_isspace(c))
            *p = 0;
        else
            return name;
    }
    return NULL;
}

static MIME_HEADER *mime_hdr_new(const char *name, const char *value)
{
    MIME_HEADER *mhdr = NULL;
    char *tmpname = NULL, *tmpval = NULL, *p;

    if (name) {
        if ((tmpname = OPENSSL_strdup(name)) == NULL)
            return NULL;
        for (p = tmpname; *p; p++)
            *p = ossl_tolower(*p);
    }
    if (value) {
        if ((tmpval = OPENSSL_strdup(value)) == NULL)
            goto err;
        for (p = tmpval; *p; p++)
            *p = ossl_tolower(*p);
    }
    mhdr = OPENSSL_malloc(sizeof(*mhdr));
    if (mhdr == NULL)
        goto err;
    mhdr->name = tmpname;
    mhdr->value = tmpval;
    if ((mhdr->params = sk_MIME_PARAM_new(mime_param_cmp)) == NULL)
        goto err;
    return mhdr;

 err:
    OPENSSL_free(tmpname);
    OPENSSL_free(tmpval);
    OPENSSL_free(mhdr);
    return NULL;
}

static int mime_hdr_addparam(MIME_HEADER *mhdr, const char *name, const char *value)
{
    char *tmpname = NULL, *tmpval = NULL, *p;
    MIME_PARAM *mparam = NULL;

    if (name) {
        tmpname = OPENSSL_strdup(name);
        if (!tmpname)
            goto err;
        for (p = tmpname; *p; p++)
            *p = ossl_tolower(*p);
    }
    if (value) {
        tmpval = OPENSSL_strdup(value);
        if (!tmpval)
            goto err;
    }
    /* Parameter values are case sensitive so leave as is */
    mparam = OPENSSL_malloc(sizeof(*mparam));
    if (mparam == NULL)
        goto err;
    mparam->param_name = tmpname;
    mparam->param_value = tmpval;
    if (!sk_MIME_PARAM_push(mhdr->params, mparam))
        goto err;
    return 1;
 err:
    OPENSSL_free(tmpname);
    OPENSSL_free(tmpval);
    OPENSSL_free(mparam);
    return 0;
}

static int mime_hdr_cmp(const MIME_HEADER *const *a,
                        const MIME_HEADER *const *b)
{
    if (!(*a)->name || !(*b)->name)
        return ! !(*a)->name - ! !(*b)->name;

    return strcmp((*a)->name, (*b)->name);
}

static int mime_param_cmp(const MIME_PARAM *const *a,
                          const MIME_PARAM *const *b)
{
    if (!(*a)->param_name || !(*b)->param_name)
        return ! !(*a)->param_name - ! !(*b)->param_name;
    return strcmp((*a)->param_name, (*b)->param_name);
}

/* Find a header with a given name (if possible) */

static MIME_HEADER *mime_hdr_find(STACK_OF(MIME_HEADER) *hdrs, const char *name)
{
    MIME_HEADER htmp;
    int idx;

    htmp.name = (char *)name;
    htmp.value = NULL;
    htmp.params = NULL;

    idx = sk_MIME_HEADER_find(hdrs, &htmp);
    return sk_MIME_HEADER_value(hdrs, idx);
}

static MIME_PARAM *mime_param_find(MIME_HEADER *hdr, const char *name)
{
    MIME_PARAM param;
    int idx;

    param.param_name = (char *)name;
    param.param_value = NULL;
    idx = sk_MIME_PARAM_find(hdr->params, &param);
    return sk_MIME_PARAM_value(hdr->params, idx);
}

static void mime_hdr_free(MIME_HEADER *hdr)
{
    if (hdr == NULL)
        return;
    OPENSSL_free(hdr->name);
    OPENSSL_free(hdr->value);
    if (hdr->params)
        sk_MIME_PARAM_pop_free(hdr->params, mime_param_free);
    OPENSSL_free(hdr);
}

static void mime_param_free(MIME_PARAM *param)
{
    OPENSSL_free(param->param_name);
    OPENSSL_free(param->param_value);
    OPENSSL_free(param);
}

/*-
 * Check for a multipart boundary. Returns:
 * 0 : no boundary
 * 1 : part boundary
 * 2 : final boundary
 */
static int mime_bound_check(char *line, int linelen, const char *bound, int blen)
{
    if (linelen == -1)
        linelen = strlen(line);
    if (blen == -1)
        blen = strlen(bound);
    /* Quickly eliminate if line length too short */
    if (blen + 2 > linelen)
        return 0;
    /* Check for part boundary */
    if ((strncmp(line, "--", 2) == 0)
        && strncmp(line + 2, bound, blen) == 0) {
        if (strncmp(line + blen + 2, "--", 2) == 0)
            return 2;
        else
            return 1;
    }
    return 0;
}

static int strip_eol(char *linebuf, int *plen, int flags)
{
    int len = *plen;
    char *p, c;
    int is_eol = 0;

    for (p = linebuf + len - 1; len > 0; len--, p--) {
        c = *p;
        if (c == '\n') {
            is_eol = 1;
        } else if (is_eol && flags & SMIME_ASCIICRLF && c == 32) {
            /* Strip trailing space on a line; 32 == ASCII for ' ' */
            continue;
        } else if (c != '\r') {
            break;
        }
    }
    *plen = len;
    return is_eol;
}
