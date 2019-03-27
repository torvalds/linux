/*
 * Copyright 2008-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* CMS utility function */

#include <stdio.h>
#include <string.h>
#include "apps.h"
#include "progs.h"

#ifndef OPENSSL_NO_CMS

# include <openssl/crypto.h>
# include <openssl/pem.h>
# include <openssl/err.h>
# include <openssl/x509_vfy.h>
# include <openssl/x509v3.h>
# include <openssl/cms.h>

static int save_certs(char *signerfile, STACK_OF(X509) *signers);
static int cms_cb(int ok, X509_STORE_CTX *ctx);
static void receipt_request_print(CMS_ContentInfo *cms);
static CMS_ReceiptRequest *make_receipt_request(STACK_OF(OPENSSL_STRING)
                                                *rr_to, int rr_allorfirst, STACK_OF(OPENSSL_STRING)
                                                *rr_from);
static int cms_set_pkey_param(EVP_PKEY_CTX *pctx,
                              STACK_OF(OPENSSL_STRING) *param);

# define SMIME_OP        0x10
# define SMIME_IP        0x20
# define SMIME_SIGNERS   0x40
# define SMIME_ENCRYPT           (1 | SMIME_OP)
# define SMIME_DECRYPT           (2 | SMIME_IP)
# define SMIME_SIGN              (3 | SMIME_OP | SMIME_SIGNERS)
# define SMIME_VERIFY            (4 | SMIME_IP)
# define SMIME_CMSOUT            (5 | SMIME_IP | SMIME_OP)
# define SMIME_RESIGN            (6 | SMIME_IP | SMIME_OP | SMIME_SIGNERS)
# define SMIME_DATAOUT           (7 | SMIME_IP)
# define SMIME_DATA_CREATE       (8 | SMIME_OP)
# define SMIME_DIGEST_VERIFY     (9 | SMIME_IP)
# define SMIME_DIGEST_CREATE     (10 | SMIME_OP)
# define SMIME_UNCOMPRESS        (11 | SMIME_IP)
# define SMIME_COMPRESS          (12 | SMIME_OP)
# define SMIME_ENCRYPTED_DECRYPT (13 | SMIME_IP)
# define SMIME_ENCRYPTED_ENCRYPT (14 | SMIME_OP)
# define SMIME_SIGN_RECEIPT      (15 | SMIME_IP | SMIME_OP)
# define SMIME_VERIFY_RECEIPT    (16 | SMIME_IP)

static int verify_err = 0;

typedef struct cms_key_param_st cms_key_param;

struct cms_key_param_st {
    int idx;
    STACK_OF(OPENSSL_STRING) *param;
    cms_key_param *next;
};

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_INFORM, OPT_OUTFORM, OPT_IN, OPT_OUT, OPT_ENCRYPT,
    OPT_DECRYPT, OPT_SIGN, OPT_SIGN_RECEIPT, OPT_RESIGN,
    OPT_VERIFY, OPT_VERIFY_RETCODE, OPT_VERIFY_RECEIPT,
    OPT_CMSOUT, OPT_DATA_OUT, OPT_DATA_CREATE, OPT_DIGEST_VERIFY,
    OPT_DIGEST_CREATE, OPT_COMPRESS, OPT_UNCOMPRESS,
    OPT_ED_DECRYPT, OPT_ED_ENCRYPT, OPT_DEBUG_DECRYPT, OPT_TEXT,
    OPT_ASCIICRLF, OPT_NOINTERN, OPT_NOVERIFY, OPT_NOCERTS,
    OPT_NOATTR, OPT_NODETACH, OPT_NOSMIMECAP, OPT_BINARY, OPT_KEYID,
    OPT_NOSIGS, OPT_NO_CONTENT_VERIFY, OPT_NO_ATTR_VERIFY, OPT_INDEF,
    OPT_NOINDEF, OPT_CRLFEOL, OPT_NOOUT, OPT_RR_PRINT,
    OPT_RR_ALL, OPT_RR_FIRST, OPT_RCTFORM, OPT_CERTFILE, OPT_CAFILE,
    OPT_CAPATH, OPT_NOCAPATH, OPT_NOCAFILE,OPT_CONTENT, OPT_PRINT,
    OPT_SECRETKEY, OPT_SECRETKEYID, OPT_PWRI_PASSWORD, OPT_ECONTENT_TYPE,
    OPT_PASSIN, OPT_TO, OPT_FROM, OPT_SUBJECT, OPT_SIGNER, OPT_RECIP,
    OPT_CERTSOUT, OPT_MD, OPT_INKEY, OPT_KEYFORM, OPT_KEYOPT, OPT_RR_FROM,
    OPT_RR_TO, OPT_AES128_WRAP, OPT_AES192_WRAP, OPT_AES256_WRAP,
    OPT_3DES_WRAP, OPT_ENGINE,
    OPT_R_ENUM,
    OPT_V_ENUM,
    OPT_CIPHER
} OPTION_CHOICE;

const OPTIONS cms_options[] = {
    {OPT_HELP_STR, 1, '-', "Usage: %s [options] cert.pem...\n"},
    {OPT_HELP_STR, 1, '-',
        "  cert.pem... recipient certs for encryption\n"},
    {OPT_HELP_STR, 1, '-', "Valid options are:\n"},
    {"help", OPT_HELP, '-', "Display this summary"},
    {"inform", OPT_INFORM, 'c', "Input format SMIME (default), PEM or DER"},
    {"outform", OPT_OUTFORM, 'c',
     "Output format SMIME (default), PEM or DER"},
    {"in", OPT_IN, '<', "Input file"},
    {"out", OPT_OUT, '>', "Output file"},
    {"encrypt", OPT_ENCRYPT, '-', "Encrypt message"},
    {"decrypt", OPT_DECRYPT, '-', "Decrypt encrypted message"},
    {"sign", OPT_SIGN, '-', "Sign message"},
    {"sign_receipt", OPT_SIGN_RECEIPT, '-', "Generate a signed receipt for the message"},
    {"resign", OPT_RESIGN, '-', "Resign a signed message"},
    {"verify", OPT_VERIFY, '-', "Verify signed message"},
    {"verify_retcode", OPT_VERIFY_RETCODE, '-'},
    {"verify_receipt", OPT_VERIFY_RECEIPT, '<'},
    {"cmsout", OPT_CMSOUT, '-', "Output CMS structure"},
    {"data_out", OPT_DATA_OUT, '-'},
    {"data_create", OPT_DATA_CREATE, '-'},
    {"digest_verify", OPT_DIGEST_VERIFY, '-'},
    {"digest_create", OPT_DIGEST_CREATE, '-'},
    {"compress", OPT_COMPRESS, '-'},
    {"uncompress", OPT_UNCOMPRESS, '-'},
    {"EncryptedData_decrypt", OPT_ED_DECRYPT, '-'},
    {"EncryptedData_encrypt", OPT_ED_ENCRYPT, '-'},
    {"debug_decrypt", OPT_DEBUG_DECRYPT, '-'},
    {"text", OPT_TEXT, '-', "Include or delete text MIME headers"},
    {"asciicrlf", OPT_ASCIICRLF, '-'},
    {"nointern", OPT_NOINTERN, '-',
     "Don't search certificates in message for signer"},
    {"noverify", OPT_NOVERIFY, '-', "Don't verify signers certificate"},
    {"nocerts", OPT_NOCERTS, '-',
     "Don't include signers certificate when signing"},
    {"noattr", OPT_NOATTR, '-', "Don't include any signed attributes"},
    {"nodetach", OPT_NODETACH, '-', "Use opaque signing"},
    {"nosmimecap", OPT_NOSMIMECAP, '-', "Omit the SMIMECapabilities attribute"},
    {"binary", OPT_BINARY, '-', "Don't translate message to text"},
    {"keyid", OPT_KEYID, '-', "Use subject key identifier"},
    {"nosigs", OPT_NOSIGS, '-', "Don't verify message signature"},
    {"no_content_verify", OPT_NO_CONTENT_VERIFY, '-'},
    {"no_attr_verify", OPT_NO_ATTR_VERIFY, '-'},
    {"stream", OPT_INDEF, '-', "Enable CMS streaming"},
    {"indef", OPT_INDEF, '-', "Same as -stream"},
    {"noindef", OPT_NOINDEF, '-', "Disable CMS streaming"},
    {"crlfeol", OPT_CRLFEOL, '-', "Use CRLF as EOL termination instead of CR only" },
    {"noout", OPT_NOOUT, '-', "For the -cmsout operation do not output the parsed CMS structure"},
    {"receipt_request_print", OPT_RR_PRINT, '-', "Print CMS Receipt Request" },
    {"receipt_request_all", OPT_RR_ALL, '-'},
    {"receipt_request_first", OPT_RR_FIRST, '-'},
    {"rctform", OPT_RCTFORM, 'F', "Receipt file format"},
    {"certfile", OPT_CERTFILE, '<', "Other certificates file"},
    {"CAfile", OPT_CAFILE, '<', "Trusted certificates file"},
    {"CApath", OPT_CAPATH, '/', "trusted certificates directory"},
    {"no-CAfile", OPT_NOCAFILE, '-',
     "Do not load the default certificates file"},
    {"no-CApath", OPT_NOCAPATH, '-',
     "Do not load certificates from the default certificates directory"},
    {"content", OPT_CONTENT, '<',
     "Supply or override content for detached signature"},
    {"print", OPT_PRINT, '-',
     "For the -cmsout operation print out all fields of the CMS structure"},
    {"secretkey", OPT_SECRETKEY, 's'},
    {"secretkeyid", OPT_SECRETKEYID, 's'},
    {"pwri_password", OPT_PWRI_PASSWORD, 's'},
    {"econtent_type", OPT_ECONTENT_TYPE, 's'},
    {"passin", OPT_PASSIN, 's', "Input file pass phrase source"},
    {"to", OPT_TO, 's', "To address"},
    {"from", OPT_FROM, 's', "From address"},
    {"subject", OPT_SUBJECT, 's', "Subject"},
    {"signer", OPT_SIGNER, 's', "Signer certificate file"},
    {"recip", OPT_RECIP, '<', "Recipient cert file for decryption"},
    {"certsout", OPT_CERTSOUT, '>', "Certificate output file"},
    {"md", OPT_MD, 's', "Digest algorithm to use when signing or resigning"},
    {"inkey", OPT_INKEY, 's',
     "Input private key (if not signer or recipient)"},
    {"keyform", OPT_KEYFORM, 'f', "Input private key format (PEM or ENGINE)"},
    {"keyopt", OPT_KEYOPT, 's', "Set public key parameters as n:v pairs"},
    {"receipt_request_from", OPT_RR_FROM, 's'},
    {"receipt_request_to", OPT_RR_TO, 's'},
    {"", OPT_CIPHER, '-', "Any supported cipher"},
    OPT_R_OPTIONS,
    OPT_V_OPTIONS,
    {"aes128-wrap", OPT_AES128_WRAP, '-', "Use AES128 to wrap key"},
    {"aes192-wrap", OPT_AES192_WRAP, '-', "Use AES192 to wrap key"},
    {"aes256-wrap", OPT_AES256_WRAP, '-', "Use AES256 to wrap key"},
# ifndef OPENSSL_NO_DES
    {"des3-wrap", OPT_3DES_WRAP, '-', "Use 3DES-EDE to wrap key"},
# endif
# ifndef OPENSSL_NO_ENGINE
    {"engine", OPT_ENGINE, 's', "Use engine e, possibly a hardware device"},
# endif
    {NULL}
};

int cms_main(int argc, char **argv)
{
    ASN1_OBJECT *econtent_type = NULL;
    BIO *in = NULL, *out = NULL, *indata = NULL, *rctin = NULL;
    CMS_ContentInfo *cms = NULL, *rcms = NULL;
    CMS_ReceiptRequest *rr = NULL;
    ENGINE *e = NULL;
    EVP_PKEY *key = NULL;
    const EVP_CIPHER *cipher = NULL, *wrap_cipher = NULL;
    const EVP_MD *sign_md = NULL;
    STACK_OF(OPENSSL_STRING) *rr_to = NULL, *rr_from = NULL;
    STACK_OF(OPENSSL_STRING) *sksigners = NULL, *skkeys = NULL;
    STACK_OF(X509) *encerts = NULL, *other = NULL;
    X509 *cert = NULL, *recip = NULL, *signer = NULL;
    X509_STORE *store = NULL;
    X509_VERIFY_PARAM *vpm = NULL;
    char *certfile = NULL, *keyfile = NULL, *contfile = NULL;
    const char *CAfile = NULL, *CApath = NULL;
    char *certsoutfile = NULL;
    int noCAfile = 0, noCApath = 0;
    char *infile = NULL, *outfile = NULL, *rctfile = NULL;
    char *passinarg = NULL, *passin = NULL, *signerfile = NULL, *recipfile = NULL;
    char *to = NULL, *from = NULL, *subject = NULL, *prog;
    cms_key_param *key_first = NULL, *key_param = NULL;
    int flags = CMS_DETACHED, noout = 0, print = 0, keyidx = -1, vpmtouched = 0;
    int informat = FORMAT_SMIME, outformat = FORMAT_SMIME;
    int operation = 0, ret = 1, rr_print = 0, rr_allorfirst = -1;
    int verify_retcode = 0, rctformat = FORMAT_SMIME, keyform = FORMAT_PEM;
    size_t secret_keylen = 0, secret_keyidlen = 0;
    unsigned char *pwri_pass = NULL, *pwri_tmp = NULL;
    unsigned char *secret_key = NULL, *secret_keyid = NULL;
    long ltmp;
    const char *mime_eol = "\n";
    OPTION_CHOICE o;

    if ((vpm = X509_VERIFY_PARAM_new()) == NULL)
        return 1;

    prog = opt_init(argc, argv, cms_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
 opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(cms_options);
            ret = 0;
            goto end;
        case OPT_INFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PDS, &informat))
                goto opthelp;
            break;
        case OPT_OUTFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PDS, &outformat))
                goto opthelp;
            break;
        case OPT_OUT:
            outfile = opt_arg();
            break;
        case OPT_ENCRYPT:
            operation = SMIME_ENCRYPT;
            break;
        case OPT_DECRYPT:
            operation = SMIME_DECRYPT;
            break;
        case OPT_SIGN:
            operation = SMIME_SIGN;
            break;
        case OPT_SIGN_RECEIPT:
            operation = SMIME_SIGN_RECEIPT;
            break;
        case OPT_RESIGN:
            operation = SMIME_RESIGN;
            break;
        case OPT_VERIFY:
            operation = SMIME_VERIFY;
            break;
        case OPT_VERIFY_RETCODE:
            verify_retcode = 1;
            break;
        case OPT_VERIFY_RECEIPT:
            operation = SMIME_VERIFY_RECEIPT;
            rctfile = opt_arg();
            break;
        case OPT_CMSOUT:
            operation = SMIME_CMSOUT;
            break;
        case OPT_DATA_OUT:
            operation = SMIME_DATAOUT;
            break;
        case OPT_DATA_CREATE:
            operation = SMIME_DATA_CREATE;
            break;
        case OPT_DIGEST_VERIFY:
            operation = SMIME_DIGEST_VERIFY;
            break;
        case OPT_DIGEST_CREATE:
            operation = SMIME_DIGEST_CREATE;
            break;
        case OPT_COMPRESS:
            operation = SMIME_COMPRESS;
            break;
        case OPT_UNCOMPRESS:
            operation = SMIME_UNCOMPRESS;
            break;
        case OPT_ED_DECRYPT:
            operation = SMIME_ENCRYPTED_DECRYPT;
            break;
        case OPT_ED_ENCRYPT:
            operation = SMIME_ENCRYPTED_ENCRYPT;
            break;
        case OPT_DEBUG_DECRYPT:
            flags |= CMS_DEBUG_DECRYPT;
            break;
        case OPT_TEXT:
            flags |= CMS_TEXT;
            break;
        case OPT_ASCIICRLF:
            flags |= CMS_ASCIICRLF;
            break;
        case OPT_NOINTERN:
            flags |= CMS_NOINTERN;
            break;
        case OPT_NOVERIFY:
            flags |= CMS_NO_SIGNER_CERT_VERIFY;
            break;
        case OPT_NOCERTS:
            flags |= CMS_NOCERTS;
            break;
        case OPT_NOATTR:
            flags |= CMS_NOATTR;
            break;
        case OPT_NODETACH:
            flags &= ~CMS_DETACHED;
            break;
        case OPT_NOSMIMECAP:
            flags |= CMS_NOSMIMECAP;
            break;
        case OPT_BINARY:
            flags |= CMS_BINARY;
            break;
        case OPT_KEYID:
            flags |= CMS_USE_KEYID;
            break;
        case OPT_NOSIGS:
            flags |= CMS_NOSIGS;
            break;
        case OPT_NO_CONTENT_VERIFY:
            flags |= CMS_NO_CONTENT_VERIFY;
            break;
        case OPT_NO_ATTR_VERIFY:
            flags |= CMS_NO_ATTR_VERIFY;
            break;
        case OPT_INDEF:
            flags |= CMS_STREAM;
            break;
        case OPT_NOINDEF:
            flags &= ~CMS_STREAM;
            break;
        case OPT_CRLFEOL:
            mime_eol = "\r\n";
            flags |= CMS_CRLFEOL;
            break;
        case OPT_NOOUT:
            noout = 1;
            break;
        case OPT_RR_PRINT:
            rr_print = 1;
            break;
        case OPT_RR_ALL:
            rr_allorfirst = 0;
            break;
        case OPT_RR_FIRST:
            rr_allorfirst = 1;
            break;
        case OPT_RCTFORM:
            if (rctformat == FORMAT_SMIME)
                rcms = SMIME_read_CMS(rctin, NULL);
            else if (rctformat == FORMAT_PEM)
                rcms = PEM_read_bio_CMS(rctin, NULL, NULL, NULL);
            else if (rctformat == FORMAT_ASN1)
                if (!opt_format(opt_arg(),
                                OPT_FMT_PEMDER | OPT_FMT_SMIME, &rctformat))
                    goto opthelp;
            break;
        case OPT_CERTFILE:
            certfile = opt_arg();
            break;
        case OPT_CAFILE:
            CAfile = opt_arg();
            break;
        case OPT_CAPATH:
            CApath = opt_arg();
            break;
        case OPT_NOCAFILE:
            noCAfile = 1;
            break;
        case OPT_NOCAPATH:
            noCApath = 1;
            break;
        case OPT_IN:
            infile = opt_arg();
            break;
        case OPT_CONTENT:
            contfile = opt_arg();
            break;
        case OPT_RR_FROM:
            if (rr_from == NULL
                && (rr_from = sk_OPENSSL_STRING_new_null()) == NULL)
                goto end;
            sk_OPENSSL_STRING_push(rr_from, opt_arg());
            break;
        case OPT_RR_TO:
            if (rr_to == NULL
                && (rr_to = sk_OPENSSL_STRING_new_null()) == NULL)
                goto end;
            sk_OPENSSL_STRING_push(rr_to, opt_arg());
            break;
        case OPT_PRINT:
            noout = print = 1;
            break;
        case OPT_SECRETKEY:
            if (secret_key != NULL) {
                BIO_printf(bio_err, "Invalid key (supplied twice) %s\n",
                           opt_arg());
                goto opthelp;
            }
            secret_key = OPENSSL_hexstr2buf(opt_arg(), &ltmp);
            if (secret_key == NULL) {
                BIO_printf(bio_err, "Invalid key %s\n", opt_arg());
                goto end;
            }
            secret_keylen = (size_t)ltmp;
            break;
        case OPT_SECRETKEYID:
            if (secret_keyid != NULL) {
                BIO_printf(bio_err, "Invalid id (supplied twice) %s\n",
                           opt_arg());
                goto opthelp;
            }
            secret_keyid = OPENSSL_hexstr2buf(opt_arg(), &ltmp);
            if (secret_keyid == NULL) {
                BIO_printf(bio_err, "Invalid id %s\n", opt_arg());
                goto opthelp;
            }
            secret_keyidlen = (size_t)ltmp;
            break;
        case OPT_PWRI_PASSWORD:
            pwri_pass = (unsigned char *)opt_arg();
            break;
        case OPT_ECONTENT_TYPE:
            if (econtent_type != NULL) {
                BIO_printf(bio_err, "Invalid OID (supplied twice) %s\n",
                           opt_arg());
                goto opthelp;
            }
            econtent_type = OBJ_txt2obj(opt_arg(), 0);
            if (econtent_type == NULL) {
                BIO_printf(bio_err, "Invalid OID %s\n", opt_arg());
                goto opthelp;
            }
            break;
        case OPT_ENGINE:
            e = setup_engine(opt_arg(), 0);
            break;
        case OPT_PASSIN:
            passinarg = opt_arg();
            break;
        case OPT_TO:
            to = opt_arg();
            break;
        case OPT_FROM:
            from = opt_arg();
            break;
        case OPT_SUBJECT:
            subject = opt_arg();
            break;
        case OPT_CERTSOUT:
            certsoutfile = opt_arg();
            break;
        case OPT_MD:
            if (!opt_md(opt_arg(), &sign_md))
                goto end;
            break;
        case OPT_SIGNER:
            /* If previous -signer argument add signer to list */
            if (signerfile != NULL) {
                if (sksigners == NULL
                    && (sksigners = sk_OPENSSL_STRING_new_null()) == NULL)
                    goto end;
                sk_OPENSSL_STRING_push(sksigners, signerfile);
                if (keyfile == NULL)
                    keyfile = signerfile;
                if (skkeys == NULL
                    && (skkeys = sk_OPENSSL_STRING_new_null()) == NULL)
                    goto end;
                sk_OPENSSL_STRING_push(skkeys, keyfile);
                keyfile = NULL;
            }
            signerfile = opt_arg();
            break;
        case OPT_INKEY:
            /* If previous -inkey argument add signer to list */
            if (keyfile != NULL) {
                if (signerfile == NULL) {
                    BIO_puts(bio_err, "Illegal -inkey without -signer\n");
                    goto end;
                }
                if (sksigners == NULL
                    && (sksigners = sk_OPENSSL_STRING_new_null()) == NULL)
                    goto end;
                sk_OPENSSL_STRING_push(sksigners, signerfile);
                signerfile = NULL;
                if (skkeys == NULL
                    && (skkeys = sk_OPENSSL_STRING_new_null()) == NULL)
                    goto end;
                sk_OPENSSL_STRING_push(skkeys, keyfile);
            }
            keyfile = opt_arg();
            break;
        case OPT_KEYFORM:
            if (!opt_format(opt_arg(), OPT_FMT_ANY, &keyform))
                goto opthelp;
            break;
        case OPT_RECIP:
            if (operation == SMIME_ENCRYPT) {
                if (encerts == NULL && (encerts = sk_X509_new_null()) == NULL)
                    goto end;
                cert = load_cert(opt_arg(), FORMAT_PEM,
                                 "recipient certificate file");
                if (cert == NULL)
                    goto end;
                sk_X509_push(encerts, cert);
                cert = NULL;
            } else {
                recipfile = opt_arg();
            }
            break;
        case OPT_CIPHER:
            if (!opt_cipher(opt_unknown(), &cipher))
                goto end;
            break;
        case OPT_KEYOPT:
            keyidx = -1;
            if (operation == SMIME_ENCRYPT) {
                if (encerts != NULL)
                    keyidx += sk_X509_num(encerts);
            } else {
                if (keyfile != NULL || signerfile != NULL)
                    keyidx++;
                if (skkeys != NULL)
                    keyidx += sk_OPENSSL_STRING_num(skkeys);
            }
            if (keyidx < 0) {
                BIO_printf(bio_err, "No key specified\n");
                goto opthelp;
            }
            if (key_param == NULL || key_param->idx != keyidx) {
                cms_key_param *nparam;
                nparam = app_malloc(sizeof(*nparam), "key param buffer");
                nparam->idx = keyidx;
                if ((nparam->param = sk_OPENSSL_STRING_new_null()) == NULL)
                    goto end;
                nparam->next = NULL;
                if (key_first == NULL)
                    key_first = nparam;
                else
                    key_param->next = nparam;
                key_param = nparam;
            }
            sk_OPENSSL_STRING_push(key_param->param, opt_arg());
            break;
        case OPT_V_CASES:
            if (!opt_verify(o, vpm))
                goto end;
            vpmtouched++;
            break;
        case OPT_R_CASES:
            if (!opt_rand(o))
                goto end;
            break;
        case OPT_3DES_WRAP:
# ifndef OPENSSL_NO_DES
            wrap_cipher = EVP_des_ede3_wrap();
# endif
            break;
        case OPT_AES128_WRAP:
            wrap_cipher = EVP_aes_128_wrap();
            break;
        case OPT_AES192_WRAP:
            wrap_cipher = EVP_aes_192_wrap();
            break;
        case OPT_AES256_WRAP:
            wrap_cipher = EVP_aes_256_wrap();
            break;
        }
    }
    argc = opt_num_rest();
    argv = opt_rest();

    if ((rr_allorfirst != -1 || rr_from != NULL) && rr_to == NULL) {
        BIO_puts(bio_err, "No Signed Receipts Recipients\n");
        goto opthelp;
    }

    if (!(operation & SMIME_SIGNERS) && (rr_to != NULL || rr_from != NULL)) {
        BIO_puts(bio_err, "Signed receipts only allowed with -sign\n");
        goto opthelp;
    }
    if (!(operation & SMIME_SIGNERS) && (skkeys != NULL || sksigners != NULL)) {
        BIO_puts(bio_err, "Multiple signers or keys not allowed\n");
        goto opthelp;
    }

    if (operation & SMIME_SIGNERS) {
        if (keyfile != NULL && signerfile == NULL) {
            BIO_puts(bio_err, "Illegal -inkey without -signer\n");
            goto opthelp;
        }
        /* Check to see if any final signer needs to be appended */
        if (signerfile != NULL) {
            if (sksigners == NULL
                && (sksigners = sk_OPENSSL_STRING_new_null()) == NULL)
                goto end;
            sk_OPENSSL_STRING_push(sksigners, signerfile);
            if (skkeys == NULL && (skkeys = sk_OPENSSL_STRING_new_null()) == NULL)
                goto end;
            if (keyfile == NULL)
                keyfile = signerfile;
            sk_OPENSSL_STRING_push(skkeys, keyfile);
        }
        if (sksigners == NULL) {
            BIO_printf(bio_err, "No signer certificate specified\n");
            goto opthelp;
        }
        signerfile = NULL;
        keyfile = NULL;
    } else if (operation == SMIME_DECRYPT) {
        if (recipfile == NULL && keyfile == NULL
            && secret_key == NULL && pwri_pass == NULL) {
            BIO_printf(bio_err,
                       "No recipient certificate or key specified\n");
            goto opthelp;
        }
    } else if (operation == SMIME_ENCRYPT) {
        if (*argv == NULL && secret_key == NULL
            && pwri_pass == NULL && encerts == NULL) {
            BIO_printf(bio_err, "No recipient(s) certificate(s) specified\n");
            goto opthelp;
        }
    } else if (!operation) {
        goto opthelp;
    }

    if (!app_passwd(passinarg, NULL, &passin, NULL)) {
        BIO_printf(bio_err, "Error getting password\n");
        goto end;
    }

    ret = 2;

    if (!(operation & SMIME_SIGNERS))
        flags &= ~CMS_DETACHED;

    if (!(operation & SMIME_OP))
        if (flags & CMS_BINARY)
            outformat = FORMAT_BINARY;

    if (!(operation & SMIME_IP))
        if (flags & CMS_BINARY)
            informat = FORMAT_BINARY;

    if (operation == SMIME_ENCRYPT) {
        if (!cipher) {
# ifndef OPENSSL_NO_DES
            cipher = EVP_des_ede3_cbc();
# else
            BIO_printf(bio_err, "No cipher selected\n");
            goto end;
# endif
        }

        if (secret_key && !secret_keyid) {
            BIO_printf(bio_err, "No secret key id\n");
            goto end;
        }

        if (*argv && encerts == NULL)
            if ((encerts = sk_X509_new_null()) == NULL)
                goto end;
        while (*argv) {
            if ((cert = load_cert(*argv, FORMAT_PEM,
                                  "recipient certificate file")) == NULL)
                goto end;
            sk_X509_push(encerts, cert);
            cert = NULL;
            argv++;
        }
    }

    if (certfile != NULL) {
        if (!load_certs(certfile, &other, FORMAT_PEM, NULL,
                        "certificate file")) {
            ERR_print_errors(bio_err);
            goto end;
        }
    }

    if (recipfile != NULL && (operation == SMIME_DECRYPT)) {
        if ((recip = load_cert(recipfile, FORMAT_PEM,
                               "recipient certificate file")) == NULL) {
            ERR_print_errors(bio_err);
            goto end;
        }
    }

    if (operation == SMIME_SIGN_RECEIPT) {
        if ((signer = load_cert(signerfile, FORMAT_PEM,
                                "receipt signer certificate file")) == NULL) {
            ERR_print_errors(bio_err);
            goto end;
        }
    }

    if (operation == SMIME_DECRYPT) {
        if (keyfile == NULL)
            keyfile = recipfile;
    } else if ((operation == SMIME_SIGN) || (operation == SMIME_SIGN_RECEIPT)) {
        if (keyfile == NULL)
            keyfile = signerfile;
    } else {
        keyfile = NULL;
    }

    if (keyfile != NULL) {
        key = load_key(keyfile, keyform, 0, passin, e, "signing key file");
        if (key == NULL)
            goto end;
    }

    in = bio_open_default(infile, 'r', informat);
    if (in == NULL)
        goto end;

    if (operation & SMIME_IP) {
        if (informat == FORMAT_SMIME) {
            cms = SMIME_read_CMS(in, &indata);
        } else if (informat == FORMAT_PEM) {
            cms = PEM_read_bio_CMS(in, NULL, NULL, NULL);
        } else if (informat == FORMAT_ASN1) {
            cms = d2i_CMS_bio(in, NULL);
        } else {
            BIO_printf(bio_err, "Bad input format for CMS file\n");
            goto end;
        }

        if (cms == NULL) {
            BIO_printf(bio_err, "Error reading S/MIME message\n");
            goto end;
        }
        if (contfile != NULL) {
            BIO_free(indata);
            if ((indata = BIO_new_file(contfile, "rb")) == NULL) {
                BIO_printf(bio_err, "Can't read content file %s\n", contfile);
                goto end;
            }
        }
        if (certsoutfile != NULL) {
            STACK_OF(X509) *allcerts;
            allcerts = CMS_get1_certs(cms);
            if (!save_certs(certsoutfile, allcerts)) {
                BIO_printf(bio_err,
                           "Error writing certs to %s\n", certsoutfile);
                ret = 5;
                goto end;
            }
            sk_X509_pop_free(allcerts, X509_free);
        }
    }

    if (rctfile != NULL) {
        char *rctmode = (rctformat == FORMAT_ASN1) ? "rb" : "r";
        if ((rctin = BIO_new_file(rctfile, rctmode)) == NULL) {
            BIO_printf(bio_err, "Can't open receipt file %s\n", rctfile);
            goto end;
        }

        if (rctformat == FORMAT_SMIME) {
            rcms = SMIME_read_CMS(rctin, NULL);
        } else if (rctformat == FORMAT_PEM) {
            rcms = PEM_read_bio_CMS(rctin, NULL, NULL, NULL);
        } else if (rctformat == FORMAT_ASN1) {
            rcms = d2i_CMS_bio(rctin, NULL);
        } else {
            BIO_printf(bio_err, "Bad input format for receipt\n");
            goto end;
        }

        if (rcms == NULL) {
            BIO_printf(bio_err, "Error reading receipt\n");
            goto end;
        }
    }

    out = bio_open_default(outfile, 'w', outformat);
    if (out == NULL)
        goto end;

    if ((operation == SMIME_VERIFY) || (operation == SMIME_VERIFY_RECEIPT)) {
        if ((store = setup_verify(CAfile, CApath, noCAfile, noCApath)) == NULL)
            goto end;
        X509_STORE_set_verify_cb(store, cms_cb);
        if (vpmtouched)
            X509_STORE_set1_param(store, vpm);
    }

    ret = 3;

    if (operation == SMIME_DATA_CREATE) {
        cms = CMS_data_create(in, flags);
    } else if (operation == SMIME_DIGEST_CREATE) {
        cms = CMS_digest_create(in, sign_md, flags);
    } else if (operation == SMIME_COMPRESS) {
        cms = CMS_compress(in, -1, flags);
    } else if (operation == SMIME_ENCRYPT) {
        int i;
        flags |= CMS_PARTIAL;
        cms = CMS_encrypt(NULL, in, cipher, flags);
        if (cms == NULL)
            goto end;
        for (i = 0; i < sk_X509_num(encerts); i++) {
            CMS_RecipientInfo *ri;
            cms_key_param *kparam;
            int tflags = flags;
            X509 *x = sk_X509_value(encerts, i);
            for (kparam = key_first; kparam; kparam = kparam->next) {
                if (kparam->idx == i) {
                    tflags |= CMS_KEY_PARAM;
                    break;
                }
            }
            ri = CMS_add1_recipient_cert(cms, x, tflags);
            if (ri == NULL)
                goto end;
            if (kparam != NULL) {
                EVP_PKEY_CTX *pctx;
                pctx = CMS_RecipientInfo_get0_pkey_ctx(ri);
                if (!cms_set_pkey_param(pctx, kparam->param))
                    goto end;
            }
            if (CMS_RecipientInfo_type(ri) == CMS_RECIPINFO_AGREE
                && wrap_cipher) {
                EVP_CIPHER_CTX *wctx;
                wctx = CMS_RecipientInfo_kari_get0_ctx(ri);
                EVP_EncryptInit_ex(wctx, wrap_cipher, NULL, NULL, NULL);
            }
        }

        if (secret_key != NULL) {
            if (!CMS_add0_recipient_key(cms, NID_undef,
                                        secret_key, secret_keylen,
                                        secret_keyid, secret_keyidlen,
                                        NULL, NULL, NULL))
                goto end;
            /* NULL these because call absorbs them */
            secret_key = NULL;
            secret_keyid = NULL;
        }
        if (pwri_pass != NULL) {
            pwri_tmp = (unsigned char *)OPENSSL_strdup((char *)pwri_pass);
            if (pwri_tmp == NULL)
                goto end;
            if (CMS_add0_recipient_password(cms,
                                            -1, NID_undef, NID_undef,
                                            pwri_tmp, -1, NULL) == NULL)
                goto end;
            pwri_tmp = NULL;
        }
        if (!(flags & CMS_STREAM)) {
            if (!CMS_final(cms, in, NULL, flags))
                goto end;
        }
    } else if (operation == SMIME_ENCRYPTED_ENCRYPT) {
        cms = CMS_EncryptedData_encrypt(in, cipher,
                                        secret_key, secret_keylen, flags);

    } else if (operation == SMIME_SIGN_RECEIPT) {
        CMS_ContentInfo *srcms = NULL;
        STACK_OF(CMS_SignerInfo) *sis;
        CMS_SignerInfo *si;
        sis = CMS_get0_SignerInfos(cms);
        if (sis == NULL)
            goto end;
        si = sk_CMS_SignerInfo_value(sis, 0);
        srcms = CMS_sign_receipt(si, signer, key, other, flags);
        if (srcms == NULL)
            goto end;
        CMS_ContentInfo_free(cms);
        cms = srcms;
    } else if (operation & SMIME_SIGNERS) {
        int i;
        /*
         * If detached data content we enable streaming if S/MIME output
         * format.
         */
        if (operation == SMIME_SIGN) {

            if (flags & CMS_DETACHED) {
                if (outformat == FORMAT_SMIME)
                    flags |= CMS_STREAM;
            }
            flags |= CMS_PARTIAL;
            cms = CMS_sign(NULL, NULL, other, in, flags);
            if (cms == NULL)
                goto end;
            if (econtent_type != NULL)
                CMS_set1_eContentType(cms, econtent_type);

            if (rr_to != NULL) {
                rr = make_receipt_request(rr_to, rr_allorfirst, rr_from);
                if (rr == NULL) {
                    BIO_puts(bio_err,
                             "Signed Receipt Request Creation Error\n");
                    goto end;
                }
            }
        } else {
            flags |= CMS_REUSE_DIGEST;
        }
        for (i = 0; i < sk_OPENSSL_STRING_num(sksigners); i++) {
            CMS_SignerInfo *si;
            cms_key_param *kparam;
            int tflags = flags;
            signerfile = sk_OPENSSL_STRING_value(sksigners, i);
            keyfile = sk_OPENSSL_STRING_value(skkeys, i);

            signer = load_cert(signerfile, FORMAT_PEM, "signer certificate");
            if (signer == NULL) {
                ret = 2;
                goto end;
            }
            key = load_key(keyfile, keyform, 0, passin, e, "signing key file");
            if (key == NULL) {
                ret = 2;
                goto end;
            }
            for (kparam = key_first; kparam; kparam = kparam->next) {
                if (kparam->idx == i) {
                    tflags |= CMS_KEY_PARAM;
                    break;
                }
            }
            si = CMS_add1_signer(cms, signer, key, sign_md, tflags);
            if (si == NULL)
                goto end;
            if (kparam != NULL) {
                EVP_PKEY_CTX *pctx;
                pctx = CMS_SignerInfo_get0_pkey_ctx(si);
                if (!cms_set_pkey_param(pctx, kparam->param))
                    goto end;
            }
            if (rr != NULL && !CMS_add1_ReceiptRequest(si, rr))
                goto end;
            X509_free(signer);
            signer = NULL;
            EVP_PKEY_free(key);
            key = NULL;
        }
        /* If not streaming or resigning finalize structure */
        if ((operation == SMIME_SIGN) && !(flags & CMS_STREAM)) {
            if (!CMS_final(cms, in, NULL, flags))
                goto end;
        }
    }

    if (cms == NULL) {
        BIO_printf(bio_err, "Error creating CMS structure\n");
        goto end;
    }

    ret = 4;
    if (operation == SMIME_DECRYPT) {
        if (flags & CMS_DEBUG_DECRYPT)
            CMS_decrypt(cms, NULL, NULL, NULL, NULL, flags);

        if (secret_key != NULL) {
            if (!CMS_decrypt_set1_key(cms,
                                      secret_key, secret_keylen,
                                      secret_keyid, secret_keyidlen)) {
                BIO_puts(bio_err, "Error decrypting CMS using secret key\n");
                goto end;
            }
        }

        if (key != NULL) {
            if (!CMS_decrypt_set1_pkey(cms, key, recip)) {
                BIO_puts(bio_err, "Error decrypting CMS using private key\n");
                goto end;
            }
        }

        if (pwri_pass != NULL) {
            if (!CMS_decrypt_set1_password(cms, pwri_pass, -1)) {
                BIO_puts(bio_err, "Error decrypting CMS using password\n");
                goto end;
            }
        }

        if (!CMS_decrypt(cms, NULL, NULL, indata, out, flags)) {
            BIO_printf(bio_err, "Error decrypting CMS structure\n");
            goto end;
        }
    } else if (operation == SMIME_DATAOUT) {
        if (!CMS_data(cms, out, flags))
            goto end;
    } else if (operation == SMIME_UNCOMPRESS) {
        if (!CMS_uncompress(cms, indata, out, flags))
            goto end;
    } else if (operation == SMIME_DIGEST_VERIFY) {
        if (CMS_digest_verify(cms, indata, out, flags) > 0) {
            BIO_printf(bio_err, "Verification successful\n");
        } else {
            BIO_printf(bio_err, "Verification failure\n");
            goto end;
        }
    } else if (operation == SMIME_ENCRYPTED_DECRYPT) {
        if (!CMS_EncryptedData_decrypt(cms, secret_key, secret_keylen,
                                       indata, out, flags))
            goto end;
    } else if (operation == SMIME_VERIFY) {
        if (CMS_verify(cms, other, store, indata, out, flags) > 0) {
            BIO_printf(bio_err, "Verification successful\n");
        } else {
            BIO_printf(bio_err, "Verification failure\n");
            if (verify_retcode)
                ret = verify_err + 32;
            goto end;
        }
        if (signerfile != NULL) {
            STACK_OF(X509) *signers;
            signers = CMS_get0_signers(cms);
            if (!save_certs(signerfile, signers)) {
                BIO_printf(bio_err,
                           "Error writing signers to %s\n", signerfile);
                ret = 5;
                goto end;
            }
            sk_X509_free(signers);
        }
        if (rr_print)
            receipt_request_print(cms);

    } else if (operation == SMIME_VERIFY_RECEIPT) {
        if (CMS_verify_receipt(rcms, cms, other, store, flags) > 0) {
            BIO_printf(bio_err, "Verification successful\n");
        } else {
            BIO_printf(bio_err, "Verification failure\n");
            goto end;
        }
    } else {
        if (noout) {
            if (print)
                CMS_ContentInfo_print_ctx(out, cms, 0, NULL);
        } else if (outformat == FORMAT_SMIME) {
            if (to)
                BIO_printf(out, "To: %s%s", to, mime_eol);
            if (from)
                BIO_printf(out, "From: %s%s", from, mime_eol);
            if (subject)
                BIO_printf(out, "Subject: %s%s", subject, mime_eol);
            if (operation == SMIME_RESIGN)
                ret = SMIME_write_CMS(out, cms, indata, flags);
            else
                ret = SMIME_write_CMS(out, cms, in, flags);
        } else if (outformat == FORMAT_PEM) {
            ret = PEM_write_bio_CMS_stream(out, cms, in, flags);
        } else if (outformat == FORMAT_ASN1) {
            ret = i2d_CMS_bio_stream(out, cms, in, flags);
        } else {
            BIO_printf(bio_err, "Bad output format for CMS file\n");
            goto end;
        }
        if (ret <= 0) {
            ret = 6;
            goto end;
        }
    }
    ret = 0;
 end:
    if (ret)
        ERR_print_errors(bio_err);
    sk_X509_pop_free(encerts, X509_free);
    sk_X509_pop_free(other, X509_free);
    X509_VERIFY_PARAM_free(vpm);
    sk_OPENSSL_STRING_free(sksigners);
    sk_OPENSSL_STRING_free(skkeys);
    OPENSSL_free(secret_key);
    OPENSSL_free(secret_keyid);
    OPENSSL_free(pwri_tmp);
    ASN1_OBJECT_free(econtent_type);
    CMS_ReceiptRequest_free(rr);
    sk_OPENSSL_STRING_free(rr_to);
    sk_OPENSSL_STRING_free(rr_from);
    for (key_param = key_first; key_param;) {
        cms_key_param *tparam;
        sk_OPENSSL_STRING_free(key_param->param);
        tparam = key_param->next;
        OPENSSL_free(key_param);
        key_param = tparam;
    }
    X509_STORE_free(store);
    X509_free(cert);
    X509_free(recip);
    X509_free(signer);
    EVP_PKEY_free(key);
    CMS_ContentInfo_free(cms);
    CMS_ContentInfo_free(rcms);
    release_engine(e);
    BIO_free(rctin);
    BIO_free(in);
    BIO_free(indata);
    BIO_free_all(out);
    OPENSSL_free(passin);
    return ret;
}

static int save_certs(char *signerfile, STACK_OF(X509) *signers)
{
    int i;
    BIO *tmp;
    if (signerfile == NULL)
        return 1;
    tmp = BIO_new_file(signerfile, "w");
    if (tmp == NULL)
        return 0;
    for (i = 0; i < sk_X509_num(signers); i++)
        PEM_write_bio_X509(tmp, sk_X509_value(signers, i));
    BIO_free(tmp);
    return 1;
}

/* Minimal callback just to output policy info (if any) */

static int cms_cb(int ok, X509_STORE_CTX *ctx)
{
    int error;

    error = X509_STORE_CTX_get_error(ctx);

    verify_err = error;

    if ((error != X509_V_ERR_NO_EXPLICIT_POLICY)
        && ((error != X509_V_OK) || (ok != 2)))
        return ok;

    policies_print(ctx);

    return ok;

}

static void gnames_stack_print(STACK_OF(GENERAL_NAMES) *gns)
{
    STACK_OF(GENERAL_NAME) *gens;
    GENERAL_NAME *gen;
    int i, j;

    for (i = 0; i < sk_GENERAL_NAMES_num(gns); i++) {
        gens = sk_GENERAL_NAMES_value(gns, i);
        for (j = 0; j < sk_GENERAL_NAME_num(gens); j++) {
            gen = sk_GENERAL_NAME_value(gens, j);
            BIO_puts(bio_err, "    ");
            GENERAL_NAME_print(bio_err, gen);
            BIO_puts(bio_err, "\n");
        }
    }
    return;
}

static void receipt_request_print(CMS_ContentInfo *cms)
{
    STACK_OF(CMS_SignerInfo) *sis;
    CMS_SignerInfo *si;
    CMS_ReceiptRequest *rr;
    int allorfirst;
    STACK_OF(GENERAL_NAMES) *rto, *rlist;
    ASN1_STRING *scid;
    int i, rv;
    sis = CMS_get0_SignerInfos(cms);
    for (i = 0; i < sk_CMS_SignerInfo_num(sis); i++) {
        si = sk_CMS_SignerInfo_value(sis, i);
        rv = CMS_get1_ReceiptRequest(si, &rr);
        BIO_printf(bio_err, "Signer %d:\n", i + 1);
        if (rv == 0) {
            BIO_puts(bio_err, "  No Receipt Request\n");
        } else if (rv < 0) {
            BIO_puts(bio_err, "  Receipt Request Parse Error\n");
            ERR_print_errors(bio_err);
        } else {
            const char *id;
            int idlen;
            CMS_ReceiptRequest_get0_values(rr, &scid, &allorfirst,
                                           &rlist, &rto);
            BIO_puts(bio_err, "  Signed Content ID:\n");
            idlen = ASN1_STRING_length(scid);
            id = (const char *)ASN1_STRING_get0_data(scid);
            BIO_dump_indent(bio_err, id, idlen, 4);
            BIO_puts(bio_err, "  Receipts From");
            if (rlist != NULL) {
                BIO_puts(bio_err, " List:\n");
                gnames_stack_print(rlist);
            } else if (allorfirst == 1) {
                BIO_puts(bio_err, ": First Tier\n");
            } else if (allorfirst == 0) {
                BIO_puts(bio_err, ": All\n");
            } else {
                BIO_printf(bio_err, " Unknown (%d)\n", allorfirst);
            }
            BIO_puts(bio_err, "  Receipts To:\n");
            gnames_stack_print(rto);
        }
        CMS_ReceiptRequest_free(rr);
    }
}

static STACK_OF(GENERAL_NAMES) *make_names_stack(STACK_OF(OPENSSL_STRING) *ns)
{
    int i;
    STACK_OF(GENERAL_NAMES) *ret;
    GENERAL_NAMES *gens = NULL;
    GENERAL_NAME *gen = NULL;
    ret = sk_GENERAL_NAMES_new_null();
    if (ret == NULL)
        goto err;
    for (i = 0; i < sk_OPENSSL_STRING_num(ns); i++) {
        char *str = sk_OPENSSL_STRING_value(ns, i);
        gen = a2i_GENERAL_NAME(NULL, NULL, NULL, GEN_EMAIL, str, 0);
        if (gen == NULL)
            goto err;
        gens = GENERAL_NAMES_new();
        if (gens == NULL)
            goto err;
        if (!sk_GENERAL_NAME_push(gens, gen))
            goto err;
        gen = NULL;
        if (!sk_GENERAL_NAMES_push(ret, gens))
            goto err;
        gens = NULL;
    }

    return ret;

 err:
    sk_GENERAL_NAMES_pop_free(ret, GENERAL_NAMES_free);
    GENERAL_NAMES_free(gens);
    GENERAL_NAME_free(gen);
    return NULL;
}

static CMS_ReceiptRequest *make_receipt_request(STACK_OF(OPENSSL_STRING)
                                                *rr_to, int rr_allorfirst, STACK_OF(OPENSSL_STRING)
                                                *rr_from)
{
    STACK_OF(GENERAL_NAMES) *rct_to = NULL, *rct_from = NULL;
    CMS_ReceiptRequest *rr;
    rct_to = make_names_stack(rr_to);
    if (rct_to == NULL)
        goto err;
    if (rr_from != NULL) {
        rct_from = make_names_stack(rr_from);
        if (rct_from == NULL)
            goto err;
    } else {
        rct_from = NULL;
    }
    rr = CMS_ReceiptRequest_create0(NULL, -1, rr_allorfirst, rct_from,
                                    rct_to);
    return rr;
 err:
    sk_GENERAL_NAMES_pop_free(rct_to, GENERAL_NAMES_free);
    return NULL;
}

static int cms_set_pkey_param(EVP_PKEY_CTX *pctx,
                              STACK_OF(OPENSSL_STRING) *param)
{
    char *keyopt;
    int i;
    if (sk_OPENSSL_STRING_num(param) <= 0)
        return 1;
    for (i = 0; i < sk_OPENSSL_STRING_num(param); i++) {
        keyopt = sk_OPENSSL_STRING_value(param, i);
        if (pkey_ctrl_string(pctx, keyopt) <= 0) {
            BIO_printf(bio_err, "parameter error \"%s\"\n", keyopt);
            ERR_print_errors(bio_err);
            return 0;
        }
    }
    return 1;
}

#endif
