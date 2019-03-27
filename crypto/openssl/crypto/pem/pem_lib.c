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
#include <string.h>
#include "internal/cryptlib.h"
#include <openssl/buffer.h>
#include <openssl/objects.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include "internal/asn1_int.h"
#include <openssl/des.h>
#include <openssl/engine.h>

#define MIN_LENGTH      4

static int load_iv(char **fromp, unsigned char *to, int num);
static int check_pem(const char *nm, const char *name);
int pem_check_suffix(const char *pem_str, const char *suffix);

int PEM_def_callback(char *buf, int num, int rwflag, void *userdata)
{
    int i, min_len;
    const char *prompt;

    /* We assume that the user passes a default password as userdata */
    if (userdata) {
        i = strlen(userdata);
        i = (i > num) ? num : i;
        memcpy(buf, userdata, i);
        return i;
    }

    prompt = EVP_get_pw_prompt();
    if (prompt == NULL)
        prompt = "Enter PEM pass phrase:";

    /*
     * rwflag == 0 means decryption
     * rwflag == 1 means encryption
     *
     * We assume that for encryption, we want a minimum length, while for
     * decryption, we cannot know any minimum length, so we assume zero.
     */
    min_len = rwflag ? MIN_LENGTH : 0;

    i = EVP_read_pw_string_min(buf, min_len, num, prompt, rwflag);
    if (i != 0) {
        PEMerr(PEM_F_PEM_DEF_CALLBACK, PEM_R_PROBLEMS_GETTING_PASSWORD);
        memset(buf, 0, (unsigned int)num);
        return -1;
    }
    return strlen(buf);
}

void PEM_proc_type(char *buf, int type)
{
    const char *str;
    char *p = buf + strlen(buf);

    if (type == PEM_TYPE_ENCRYPTED)
        str = "ENCRYPTED";
    else if (type == PEM_TYPE_MIC_CLEAR)
        str = "MIC-CLEAR";
    else if (type == PEM_TYPE_MIC_ONLY)
        str = "MIC-ONLY";
    else
        str = "BAD-TYPE";

    BIO_snprintf(p, PEM_BUFSIZE - (size_t)(p - buf), "Proc-Type: 4,%s\n", str);
}

void PEM_dek_info(char *buf, const char *type, int len, char *str)
{
    long i;
    char *p = buf + strlen(buf);
    int j = PEM_BUFSIZE - (size_t)(p - buf), n;

    n = BIO_snprintf(p, j, "DEK-Info: %s,", type);
    if (n > 0) {
        j -= n;
        p += n;
        for (i = 0; i < len; i++) {
            n = BIO_snprintf(p, j, "%02X", 0xff & str[i]);
            if (n <= 0)
                return;
            j -= n;
            p += n;
        }
        if (j > 1)
            strcpy(p, "\n");
    }
}

#ifndef OPENSSL_NO_STDIO
void *PEM_ASN1_read(d2i_of_void *d2i, const char *name, FILE *fp, void **x,
                    pem_password_cb *cb, void *u)
{
    BIO *b;
    void *ret;

    if ((b = BIO_new(BIO_s_file())) == NULL) {
        PEMerr(PEM_F_PEM_ASN1_READ, ERR_R_BUF_LIB);
        return 0;
    }
    BIO_set_fp(b, fp, BIO_NOCLOSE);
    ret = PEM_ASN1_read_bio(d2i, name, b, x, cb, u);
    BIO_free(b);
    return ret;
}
#endif

static int check_pem(const char *nm, const char *name)
{
    /* Normal matching nm and name */
    if (strcmp(nm, name) == 0)
        return 1;

    /* Make PEM_STRING_EVP_PKEY match any private key */

    if (strcmp(name, PEM_STRING_EVP_PKEY) == 0) {
        int slen;
        const EVP_PKEY_ASN1_METHOD *ameth;
        if (strcmp(nm, PEM_STRING_PKCS8) == 0)
            return 1;
        if (strcmp(nm, PEM_STRING_PKCS8INF) == 0)
            return 1;
        slen = pem_check_suffix(nm, "PRIVATE KEY");
        if (slen > 0) {
            /*
             * NB: ENGINE implementations won't contain a deprecated old
             * private key decode function so don't look for them.
             */
            ameth = EVP_PKEY_asn1_find_str(NULL, nm, slen);
            if (ameth && ameth->old_priv_decode)
                return 1;
        }
        return 0;
    }

    if (strcmp(name, PEM_STRING_PARAMETERS) == 0) {
        int slen;
        const EVP_PKEY_ASN1_METHOD *ameth;
        slen = pem_check_suffix(nm, "PARAMETERS");
        if (slen > 0) {
            ENGINE *e;
            ameth = EVP_PKEY_asn1_find_str(&e, nm, slen);
            if (ameth) {
                int r;
                if (ameth->param_decode)
                    r = 1;
                else
                    r = 0;
#ifndef OPENSSL_NO_ENGINE
                ENGINE_finish(e);
#endif
                return r;
            }
        }
        return 0;
    }
    /* If reading DH parameters handle X9.42 DH format too */
    if (strcmp(nm, PEM_STRING_DHXPARAMS) == 0
        && strcmp(name, PEM_STRING_DHPARAMS) == 0)
        return 1;

    /* Permit older strings */

    if (strcmp(nm, PEM_STRING_X509_OLD) == 0
        && strcmp(name, PEM_STRING_X509) == 0)
        return 1;

    if (strcmp(nm, PEM_STRING_X509_REQ_OLD) == 0
        && strcmp(name, PEM_STRING_X509_REQ) == 0)
        return 1;

    /* Allow normal certs to be read as trusted certs */
    if (strcmp(nm, PEM_STRING_X509) == 0
        && strcmp(name, PEM_STRING_X509_TRUSTED) == 0)
        return 1;

    if (strcmp(nm, PEM_STRING_X509_OLD) == 0
        && strcmp(name, PEM_STRING_X509_TRUSTED) == 0)
        return 1;

    /* Some CAs use PKCS#7 with CERTIFICATE headers */
    if (strcmp(nm, PEM_STRING_X509) == 0
        && strcmp(name, PEM_STRING_PKCS7) == 0)
        return 1;

    if (strcmp(nm, PEM_STRING_PKCS7_SIGNED) == 0
        && strcmp(name, PEM_STRING_PKCS7) == 0)
        return 1;

#ifndef OPENSSL_NO_CMS
    if (strcmp(nm, PEM_STRING_X509) == 0
        && strcmp(name, PEM_STRING_CMS) == 0)
        return 1;
    /* Allow CMS to be read from PKCS#7 headers */
    if (strcmp(nm, PEM_STRING_PKCS7) == 0
        && strcmp(name, PEM_STRING_CMS) == 0)
        return 1;
#endif

    return 0;
}

static void pem_free(void *p, unsigned int flags, size_t num)
{
    if (flags & PEM_FLAG_SECURE)
        OPENSSL_secure_clear_free(p, num);
    else
        OPENSSL_free(p);
}

static void *pem_malloc(int num, unsigned int flags)
{
    return (flags & PEM_FLAG_SECURE) ? OPENSSL_secure_malloc(num)
                                     : OPENSSL_malloc(num);
}

static int pem_bytes_read_bio_flags(unsigned char **pdata, long *plen,
                                    char **pnm, const char *name, BIO *bp,
                                    pem_password_cb *cb, void *u,
                                    unsigned int flags)
{
    EVP_CIPHER_INFO cipher;
    char *nm = NULL, *header = NULL;
    unsigned char *data = NULL;
    long len = 0;
    int ret = 0;

    do {
        pem_free(nm, flags, 0);
        pem_free(header, flags, 0);
        pem_free(data, flags, len);
        if (!PEM_read_bio_ex(bp, &nm, &header, &data, &len, flags)) {
            if (ERR_GET_REASON(ERR_peek_error()) == PEM_R_NO_START_LINE)
                ERR_add_error_data(2, "Expecting: ", name);
            return 0;
        }
    } while (!check_pem(nm, name));
    if (!PEM_get_EVP_CIPHER_INFO(header, &cipher))
        goto err;
    if (!PEM_do_header(&cipher, data, &len, cb, u))
        goto err;

    *pdata = data;
    *plen = len;

    if (pnm != NULL)
        *pnm = nm;

    ret = 1;

 err:
    if (!ret || pnm == NULL)
        pem_free(nm, flags, 0);
    pem_free(header, flags, 0);
    if (!ret)
        pem_free(data, flags, len);
    return ret;
}

int PEM_bytes_read_bio(unsigned char **pdata, long *plen, char **pnm,
                       const char *name, BIO *bp, pem_password_cb *cb,
                       void *u) {
    return pem_bytes_read_bio_flags(pdata, plen, pnm, name, bp, cb, u,
                                    PEM_FLAG_EAY_COMPATIBLE);
}

int PEM_bytes_read_bio_secmem(unsigned char **pdata, long *plen, char **pnm,
                              const char *name, BIO *bp, pem_password_cb *cb,
                              void *u) {
    return pem_bytes_read_bio_flags(pdata, plen, pnm, name, bp, cb, u,
                                    PEM_FLAG_SECURE | PEM_FLAG_EAY_COMPATIBLE);
}

#ifndef OPENSSL_NO_STDIO
int PEM_ASN1_write(i2d_of_void *i2d, const char *name, FILE *fp,
                   void *x, const EVP_CIPHER *enc, unsigned char *kstr,
                   int klen, pem_password_cb *callback, void *u)
{
    BIO *b;
    int ret;

    if ((b = BIO_new(BIO_s_file())) == NULL) {
        PEMerr(PEM_F_PEM_ASN1_WRITE, ERR_R_BUF_LIB);
        return 0;
    }
    BIO_set_fp(b, fp, BIO_NOCLOSE);
    ret = PEM_ASN1_write_bio(i2d, name, b, x, enc, kstr, klen, callback, u);
    BIO_free(b);
    return ret;
}
#endif

int PEM_ASN1_write_bio(i2d_of_void *i2d, const char *name, BIO *bp,
                       void *x, const EVP_CIPHER *enc, unsigned char *kstr,
                       int klen, pem_password_cb *callback, void *u)
{
    EVP_CIPHER_CTX *ctx = NULL;
    int dsize = 0, i = 0, j = 0, ret = 0;
    unsigned char *p, *data = NULL;
    const char *objstr = NULL;
    char buf[PEM_BUFSIZE];
    unsigned char key[EVP_MAX_KEY_LENGTH];
    unsigned char iv[EVP_MAX_IV_LENGTH];

    if (enc != NULL) {
        objstr = OBJ_nid2sn(EVP_CIPHER_nid(enc));
        if (objstr == NULL || EVP_CIPHER_iv_length(enc) == 0
                || EVP_CIPHER_iv_length(enc) > (int)sizeof(iv)
                   /*
                    * Check "Proc-Type: 4,Encrypted\nDEK-Info: objstr,hex-iv\n"
                    * fits into buf
                    */
                || (strlen(objstr) + 23 + 2 * EVP_CIPHER_iv_length(enc) + 13)
                   > sizeof(buf)) {
            PEMerr(PEM_F_PEM_ASN1_WRITE_BIO, PEM_R_UNSUPPORTED_CIPHER);
            goto err;
        }
    }

    if ((dsize = i2d(x, NULL)) < 0) {
        PEMerr(PEM_F_PEM_ASN1_WRITE_BIO, ERR_R_ASN1_LIB);
        dsize = 0;
        goto err;
    }
    /* dsize + 8 bytes are needed */
    /* actually it needs the cipher block size extra... */
    data = OPENSSL_malloc((unsigned int)dsize + 20);
    if (data == NULL) {
        PEMerr(PEM_F_PEM_ASN1_WRITE_BIO, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    p = data;
    i = i2d(x, &p);

    if (enc != NULL) {
        if (kstr == NULL) {
            if (callback == NULL)
                klen = PEM_def_callback(buf, PEM_BUFSIZE, 1, u);
            else
                klen = (*callback) (buf, PEM_BUFSIZE, 1, u);
            if (klen <= 0) {
                PEMerr(PEM_F_PEM_ASN1_WRITE_BIO, PEM_R_READ_KEY);
                goto err;
            }
#ifdef CHARSET_EBCDIC
            /* Convert the pass phrase from EBCDIC */
            ebcdic2ascii(buf, buf, klen);
#endif
            kstr = (unsigned char *)buf;
        }
        if (RAND_bytes(iv, EVP_CIPHER_iv_length(enc)) <= 0) /* Generate a salt */
            goto err;
        /*
         * The 'iv' is used as the iv and as a salt.  It is NOT taken from
         * the BytesToKey function
         */
        if (!EVP_BytesToKey(enc, EVP_md5(), iv, kstr, klen, 1, key, NULL))
            goto err;

        if (kstr == (unsigned char *)buf)
            OPENSSL_cleanse(buf, PEM_BUFSIZE);

        buf[0] = '\0';
        PEM_proc_type(buf, PEM_TYPE_ENCRYPTED);
        PEM_dek_info(buf, objstr, EVP_CIPHER_iv_length(enc), (char *)iv);
        /* k=strlen(buf); */

        ret = 1;
        if ((ctx = EVP_CIPHER_CTX_new()) == NULL
            || !EVP_EncryptInit_ex(ctx, enc, NULL, key, iv)
            || !EVP_EncryptUpdate(ctx, data, &j, data, i)
            || !EVP_EncryptFinal_ex(ctx, &(data[j]), &i))
            ret = 0;
        if (ret == 0)
            goto err;
        i += j;
    } else {
        ret = 1;
        buf[0] = '\0';
    }
    i = PEM_write_bio(bp, name, buf, data, i);
    if (i <= 0)
        ret = 0;
 err:
    OPENSSL_cleanse(key, sizeof(key));
    OPENSSL_cleanse(iv, sizeof(iv));
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(buf, PEM_BUFSIZE);
    OPENSSL_clear_free(data, (unsigned int)dsize);
    return ret;
}

int PEM_do_header(EVP_CIPHER_INFO *cipher, unsigned char *data, long *plen,
                  pem_password_cb *callback, void *u)
{
    int ok;
    int keylen;
    long len = *plen;
    int ilen = (int) len;       /* EVP_DecryptUpdate etc. take int lengths */
    EVP_CIPHER_CTX *ctx;
    unsigned char key[EVP_MAX_KEY_LENGTH];
    char buf[PEM_BUFSIZE];

#if LONG_MAX > INT_MAX
    /* Check that we did not truncate the length */
    if (len > INT_MAX) {
        PEMerr(PEM_F_PEM_DO_HEADER, PEM_R_HEADER_TOO_LONG);
        return 0;
    }
#endif

    if (cipher->cipher == NULL)
        return 1;
    if (callback == NULL)
        keylen = PEM_def_callback(buf, PEM_BUFSIZE, 0, u);
    else
        keylen = callback(buf, PEM_BUFSIZE, 0, u);
    if (keylen < 0) {
        PEMerr(PEM_F_PEM_DO_HEADER, PEM_R_BAD_PASSWORD_READ);
        return 0;
    }
#ifdef CHARSET_EBCDIC
    /* Convert the pass phrase from EBCDIC */
    ebcdic2ascii(buf, buf, keylen);
#endif

    if (!EVP_BytesToKey(cipher->cipher, EVP_md5(), &(cipher->iv[0]),
                        (unsigned char *)buf, keylen, 1, key, NULL))
        return 0;

    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL)
        return 0;

    ok = EVP_DecryptInit_ex(ctx, cipher->cipher, NULL, key, &(cipher->iv[0]));
    if (ok)
        ok = EVP_DecryptUpdate(ctx, data, &ilen, data, ilen);
    if (ok) {
        /* Squirrel away the length of data decrypted so far. */
        *plen = ilen;
        ok = EVP_DecryptFinal_ex(ctx, &(data[ilen]), &ilen);
    }
    if (ok)
        *plen += ilen;
    else
        PEMerr(PEM_F_PEM_DO_HEADER, PEM_R_BAD_DECRYPT);

    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse((char *)buf, sizeof(buf));
    OPENSSL_cleanse((char *)key, sizeof(key));
    return ok;
}

/*
 * This implements a very limited PEM header parser that does not support the
 * full grammar of rfc1421.  In particular, folded headers are not supported,
 * nor is additional whitespace.
 *
 * A robust implementation would make use of a library that turns the headers
 * into a BIO from which one folded line is read at a time, and is then split
 * into a header label and content.  We would then parse the content of the
 * headers we care about.  This is overkill for just this limited use-case, but
 * presumably we also parse rfc822-style headers for S/MIME, so a common
 * abstraction might well be more generally useful.
 */
int PEM_get_EVP_CIPHER_INFO(char *header, EVP_CIPHER_INFO *cipher)
{
    static const char ProcType[] = "Proc-Type:";
    static const char ENCRYPTED[] = "ENCRYPTED";
    static const char DEKInfo[] = "DEK-Info:";
    const EVP_CIPHER *enc = NULL;
    int ivlen;
    char *dekinfostart, c;

    cipher->cipher = NULL;
    memset(cipher->iv, 0, sizeof(cipher->iv));
    if ((header == NULL) || (*header == '\0') || (*header == '\n'))
        return 1;

    if (strncmp(header, ProcType, sizeof(ProcType)-1) != 0) {
        PEMerr(PEM_F_PEM_GET_EVP_CIPHER_INFO, PEM_R_NOT_PROC_TYPE);
        return 0;
    }
    header += sizeof(ProcType)-1;
    header += strspn(header, " \t");

    if (*header++ != '4' || *header++ != ',')
        return 0;
    header += strspn(header, " \t");

    /* We expect "ENCRYPTED" followed by optional white-space + line break */
    if (strncmp(header, ENCRYPTED, sizeof(ENCRYPTED)-1) != 0 ||
        strspn(header+sizeof(ENCRYPTED)-1, " \t\r\n") == 0) {
        PEMerr(PEM_F_PEM_GET_EVP_CIPHER_INFO, PEM_R_NOT_ENCRYPTED);
        return 0;
    }
    header += sizeof(ENCRYPTED)-1;
    header += strspn(header, " \t\r");
    if (*header++ != '\n') {
        PEMerr(PEM_F_PEM_GET_EVP_CIPHER_INFO, PEM_R_SHORT_HEADER);
        return 0;
    }

    /*-
     * https://tools.ietf.org/html/rfc1421#section-4.6.1.3
     * We expect "DEK-Info: algo[,hex-parameters]"
     */
    if (strncmp(header, DEKInfo, sizeof(DEKInfo)-1) != 0) {
        PEMerr(PEM_F_PEM_GET_EVP_CIPHER_INFO, PEM_R_NOT_DEK_INFO);
        return 0;
    }
    header += sizeof(DEKInfo)-1;
    header += strspn(header, " \t");

    /*
     * DEK-INFO is a comma-separated combination of algorithm name and optional
     * parameters.
     */
    dekinfostart = header;
    header += strcspn(header, " \t,");
    c = *header;
    *header = '\0';
    cipher->cipher = enc = EVP_get_cipherbyname(dekinfostart);
    *header = c;
    header += strspn(header, " \t");

    if (enc == NULL) {
        PEMerr(PEM_F_PEM_GET_EVP_CIPHER_INFO, PEM_R_UNSUPPORTED_ENCRYPTION);
        return 0;
    }
    ivlen = EVP_CIPHER_iv_length(enc);
    if (ivlen > 0 && *header++ != ',') {
        PEMerr(PEM_F_PEM_GET_EVP_CIPHER_INFO, PEM_R_MISSING_DEK_IV);
        return 0;
    } else if (ivlen == 0 && *header == ',') {
        PEMerr(PEM_F_PEM_GET_EVP_CIPHER_INFO, PEM_R_UNEXPECTED_DEK_IV);
        return 0;
    }

    if (!load_iv(&header, cipher->iv, EVP_CIPHER_iv_length(enc)))
        return 0;

    return 1;
}

static int load_iv(char **fromp, unsigned char *to, int num)
{
    int v, i;
    char *from;

    from = *fromp;
    for (i = 0; i < num; i++)
        to[i] = 0;
    num *= 2;
    for (i = 0; i < num; i++) {
        v = OPENSSL_hexchar2int(*from);
        if (v < 0) {
            PEMerr(PEM_F_LOAD_IV, PEM_R_BAD_IV_CHARS);
            return 0;
        }
        from++;
        to[i / 2] |= v << (long)((!(i & 1)) * 4);
    }

    *fromp = from;
    return 1;
}

#ifndef OPENSSL_NO_STDIO
int PEM_write(FILE *fp, const char *name, const char *header,
              const unsigned char *data, long len)
{
    BIO *b;
    int ret;

    if ((b = BIO_new(BIO_s_file())) == NULL) {
        PEMerr(PEM_F_PEM_WRITE, ERR_R_BUF_LIB);
        return 0;
    }
    BIO_set_fp(b, fp, BIO_NOCLOSE);
    ret = PEM_write_bio(b, name, header, data, len);
    BIO_free(b);
    return ret;
}
#endif

int PEM_write_bio(BIO *bp, const char *name, const char *header,
                  const unsigned char *data, long len)
{
    int nlen, n, i, j, outl;
    unsigned char *buf = NULL;
    EVP_ENCODE_CTX *ctx = EVP_ENCODE_CTX_new();
    int reason = ERR_R_BUF_LIB;
    int retval = 0;

    if (ctx == NULL) {
        reason = ERR_R_MALLOC_FAILURE;
        goto err;
    }

    EVP_EncodeInit(ctx);
    nlen = strlen(name);

    if ((BIO_write(bp, "-----BEGIN ", 11) != 11) ||
        (BIO_write(bp, name, nlen) != nlen) ||
        (BIO_write(bp, "-----\n", 6) != 6))
        goto err;

    i = strlen(header);
    if (i > 0) {
        if ((BIO_write(bp, header, i) != i) || (BIO_write(bp, "\n", 1) != 1))
            goto err;
    }

    buf = OPENSSL_malloc(PEM_BUFSIZE * 8);
    if (buf == NULL) {
        reason = ERR_R_MALLOC_FAILURE;
        goto err;
    }

    i = j = 0;
    while (len > 0) {
        n = (int)((len > (PEM_BUFSIZE * 5)) ? (PEM_BUFSIZE * 5) : len);
        if (!EVP_EncodeUpdate(ctx, buf, &outl, &(data[j]), n))
            goto err;
        if ((outl) && (BIO_write(bp, (char *)buf, outl) != outl))
            goto err;
        i += outl;
        len -= n;
        j += n;
    }
    EVP_EncodeFinal(ctx, buf, &outl);
    if ((outl > 0) && (BIO_write(bp, (char *)buf, outl) != outl))
        goto err;
    if ((BIO_write(bp, "-----END ", 9) != 9) ||
        (BIO_write(bp, name, nlen) != nlen) ||
        (BIO_write(bp, "-----\n", 6) != 6))
        goto err;
    retval = i + outl;

 err:
    if (retval == 0)
        PEMerr(PEM_F_PEM_WRITE_BIO, reason);
    EVP_ENCODE_CTX_free(ctx);
    OPENSSL_clear_free(buf, PEM_BUFSIZE * 8);
    return retval;
}

#ifndef OPENSSL_NO_STDIO
int PEM_read(FILE *fp, char **name, char **header, unsigned char **data,
             long *len)
{
    BIO *b;
    int ret;

    if ((b = BIO_new(BIO_s_file())) == NULL) {
        PEMerr(PEM_F_PEM_READ, ERR_R_BUF_LIB);
        return 0;
    }
    BIO_set_fp(b, fp, BIO_NOCLOSE);
    ret = PEM_read_bio(b, name, header, data, len);
    BIO_free(b);
    return ret;
}
#endif

/* Some helpers for PEM_read_bio_ex(). */
static int sanitize_line(char *linebuf, int len, unsigned int flags)
{
    int i;

    if (flags & PEM_FLAG_EAY_COMPATIBLE) {
        /* Strip trailing whitespace */
        while ((len >= 0) && (linebuf[len] <= ' '))
            len--;
        /* Go back to whitespace before applying uniform line ending. */
        len++;
    } else if (flags & PEM_FLAG_ONLY_B64) {
        for (i = 0; i < len; ++i) {
            if (!ossl_isbase64(linebuf[i]) || linebuf[i] == '\n'
                || linebuf[i] == '\r')
                break;
        }
        len = i;
    } else {
        /* EVP_DecodeBlock strips leading and trailing whitespace, so just strip
         * control characters in-place and let everything through. */
        for (i = 0; i < len; ++i) {
            if (linebuf[i] == '\n' || linebuf[i] == '\r')
                break;
            if (ossl_iscntrl(linebuf[i]))
                linebuf[i] = ' ';
        }
        len = i;
    }
    /* The caller allocated LINESIZE+1, so this is safe. */
    linebuf[len++] = '\n';
    linebuf[len] = '\0';
    return len;
}

#define LINESIZE 255
/* Note trailing spaces for begin and end. */
static const char beginstr[] = "-----BEGIN ";
static const char endstr[] = "-----END ";
static const char tailstr[] = "-----\n";
#define BEGINLEN ((int)(sizeof(beginstr) - 1))
#define ENDLEN ((int)(sizeof(endstr) - 1))
#define TAILLEN ((int)(sizeof(tailstr) - 1))
static int get_name(BIO *bp, char **name, unsigned int flags)
{
    char *linebuf;
    int ret = 0;
    int len;

    /*
     * Need to hold trailing NUL (accounted for by BIO_gets() and the newline
     * that will be added by sanitize_line() (the extra '1').
     */
    linebuf = pem_malloc(LINESIZE + 1, flags);
    if (linebuf == NULL) {
        PEMerr(PEM_F_GET_NAME, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    do {
        len = BIO_gets(bp, linebuf, LINESIZE);

        if (len <= 0) {
            PEMerr(PEM_F_GET_NAME, PEM_R_NO_START_LINE);
            goto err;
        }

        /* Strip trailing garbage and standardize ending. */
        len = sanitize_line(linebuf, len, flags & ~PEM_FLAG_ONLY_B64);

        /* Allow leading empty or non-matching lines. */
    } while (strncmp(linebuf, beginstr, BEGINLEN) != 0
             || len < TAILLEN
             || strncmp(linebuf + len - TAILLEN, tailstr, TAILLEN) != 0);
    linebuf[len - TAILLEN] = '\0';
    len = len - BEGINLEN - TAILLEN + 1;
    *name = pem_malloc(len, flags);
    if (*name == NULL) {
        PEMerr(PEM_F_GET_NAME, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    memcpy(*name, linebuf + BEGINLEN, len);
    ret = 1;

err:
    pem_free(linebuf, flags, LINESIZE + 1);
    return ret;
}

/* Keep track of how much of a header we've seen. */
enum header_status {
    MAYBE_HEADER,
    IN_HEADER,
    POST_HEADER
};

/**
 * Extract the optional PEM header, with details on the type of content and
 * any encryption used on the contents, and the bulk of the data from the bio.
 * The end of the header is marked by a blank line; if the end-of-input marker
 * is reached prior to a blank line, there is no header.
 *
 * The header and data arguments are BIO** since we may have to swap them
 * if there is no header, for efficiency.
 *
 * We need the name of the PEM-encoded type to verify the end string.
 */
static int get_header_and_data(BIO *bp, BIO **header, BIO **data, char *name,
                               unsigned int flags)
{
    BIO *tmp = *header;
    char *linebuf, *p;
    int len, line, ret = 0, end = 0;
    /* 0 if not seen (yet), 1 if reading header, 2 if finished header */
    enum header_status got_header = MAYBE_HEADER;
    unsigned int flags_mask;
    size_t namelen;

    /* Need to hold trailing NUL (accounted for by BIO_gets() and the newline
     * that will be added by sanitize_line() (the extra '1'). */
    linebuf = pem_malloc(LINESIZE + 1, flags);
    if (linebuf == NULL) {
        PEMerr(PEM_F_GET_HEADER_AND_DATA, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    for (line = 0; ; line++) {
        flags_mask = ~0u;
        len = BIO_gets(bp, linebuf, LINESIZE);
        if (len <= 0) {
            PEMerr(PEM_F_GET_HEADER_AND_DATA, PEM_R_SHORT_HEADER);
            goto err;
        }

        if (got_header == MAYBE_HEADER) {
            if (memchr(linebuf, ':', len) != NULL)
                got_header = IN_HEADER;
        }
        if (!strncmp(linebuf, endstr, ENDLEN) || got_header == IN_HEADER)
            flags_mask &= ~PEM_FLAG_ONLY_B64;
        len = sanitize_line(linebuf, len, flags & flags_mask);

        /* Check for end of header. */
        if (linebuf[0] == '\n') {
            if (got_header == POST_HEADER) {
                /* Another blank line is an error. */
                PEMerr(PEM_F_GET_HEADER_AND_DATA, PEM_R_BAD_END_LINE);
                goto err;
            }
            got_header = POST_HEADER;
            tmp = *data;
            continue;
        }

        /* Check for end of stream (which means there is no header). */
        if (strncmp(linebuf, endstr, ENDLEN) == 0) {
            p = linebuf + ENDLEN;
            namelen = strlen(name);
            if (strncmp(p, name, namelen) != 0 ||
                strncmp(p + namelen, tailstr, TAILLEN) != 0) {
                PEMerr(PEM_F_GET_HEADER_AND_DATA, PEM_R_BAD_END_LINE);
                goto err;
            }
            if (got_header == MAYBE_HEADER) {
                *header = *data;
                *data = tmp;
            }
            break;
        } else if (end) {
            /* Malformed input; short line not at end of data. */
            PEMerr(PEM_F_GET_HEADER_AND_DATA, PEM_R_BAD_END_LINE);
            goto err;
        }
        /*
         * Else, a line of text -- could be header or data; we don't
         * know yet.  Just pass it through.
         */
        if (BIO_puts(tmp, linebuf) < 0)
            goto err;
        /*
         * Only encrypted files need the line length check applied.
         */
        if (got_header == POST_HEADER) {
            /* 65 includes the trailing newline */
            if (len > 65)
                goto err;
            if (len < 65)
                end = 1;
        }
    }

    ret = 1;
err:
    pem_free(linebuf, flags, LINESIZE + 1);
    return ret;
}

/**
 * Read in PEM-formatted data from the given BIO.
 *
 * By nature of the PEM format, all content must be printable ASCII (except
 * for line endings).  Other characters are malformed input and will be rejected.
 */
int PEM_read_bio_ex(BIO *bp, char **name_out, char **header,
                    unsigned char **data, long *len_out, unsigned int flags)
{
    EVP_ENCODE_CTX *ctx = EVP_ENCODE_CTX_new();
    const BIO_METHOD *bmeth;
    BIO *headerB = NULL, *dataB = NULL;
    char *name = NULL;
    int len, taillen, headerlen, ret = 0;
    BUF_MEM * buf_mem;

    if (ctx == NULL) {
        PEMerr(PEM_F_PEM_READ_BIO_EX, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    *len_out = 0;
    *name_out = *header = NULL;
    *data = NULL;
    if ((flags & PEM_FLAG_EAY_COMPATIBLE) && (flags & PEM_FLAG_ONLY_B64)) {
        /* These two are mutually incompatible; bail out. */
        PEMerr(PEM_F_PEM_READ_BIO_EX, ERR_R_PASSED_INVALID_ARGUMENT);
        goto end;
    }
    bmeth = (flags & PEM_FLAG_SECURE) ? BIO_s_secmem() : BIO_s_mem();

    headerB = BIO_new(bmeth);
    dataB = BIO_new(bmeth);
    if (headerB == NULL || dataB == NULL) {
        PEMerr(PEM_F_PEM_READ_BIO_EX, ERR_R_MALLOC_FAILURE);
        goto end;
    }

    if (!get_name(bp, &name, flags))
        goto end;
    if (!get_header_and_data(bp, &headerB, &dataB, name, flags))
        goto end;

    EVP_DecodeInit(ctx);
    BIO_get_mem_ptr(dataB, &buf_mem);
    len = buf_mem->length;
    if (EVP_DecodeUpdate(ctx, (unsigned char*)buf_mem->data, &len,
                         (unsigned char*)buf_mem->data, len) < 0
            || EVP_DecodeFinal(ctx, (unsigned char*)&(buf_mem->data[len]),
                               &taillen) < 0) {
        PEMerr(PEM_F_PEM_READ_BIO_EX, PEM_R_BAD_BASE64_DECODE);
        goto end;
    }
    len += taillen;
    buf_mem->length = len;

    /* There was no data in the PEM file; avoid malloc(0). */
    if (len == 0)
        goto end;
    headerlen = BIO_get_mem_data(headerB, NULL);
    *header = pem_malloc(headerlen + 1, flags);
    *data = pem_malloc(len, flags);
    if (*header == NULL || *data == NULL) {
        pem_free(*header, flags, 0);
        pem_free(*data, flags, 0);
        goto end;
    }
    BIO_read(headerB, *header, headerlen);
    (*header)[headerlen] = '\0';
    BIO_read(dataB, *data, len);
    *len_out = len;
    *name_out = name;
    name = NULL;
    ret = 1;

end:
    EVP_ENCODE_CTX_free(ctx);
    pem_free(name, flags, 0);
    BIO_free(headerB);
    BIO_free(dataB);
    return ret;
}

int PEM_read_bio(BIO *bp, char **name, char **header, unsigned char **data,
                 long *len)
{
    return PEM_read_bio_ex(bp, name, header, data, len, PEM_FLAG_EAY_COMPATIBLE);
}

/*
 * Check pem string and return prefix length. If for example the pem_str ==
 * "RSA PRIVATE KEY" and suffix = "PRIVATE KEY" the return value is 3 for the
 * string "RSA".
 */

int pem_check_suffix(const char *pem_str, const char *suffix)
{
    int pem_len = strlen(pem_str);
    int suffix_len = strlen(suffix);
    const char *p;
    if (suffix_len + 1 >= pem_len)
        return 0;
    p = pem_str + pem_len - suffix_len;
    if (strcmp(p, suffix))
        return 0;
    p--;
    if (*p != ' ')
        return 0;
    return p - pem_str;
}
