/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include "apps.h"
#include "progs.h"
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/conf.h>
#include <openssl/err.h>
#include <openssl/asn1.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/bn.h>
#include <openssl/lhash.h>
#ifndef OPENSSL_NO_RSA
# include <openssl/rsa.h>
#endif
#ifndef OPENSSL_NO_DSA
# include <openssl/dsa.h>
#endif

#define SECTION         "req"

#define BITS            "default_bits"
#define KEYFILE         "default_keyfile"
#define PROMPT          "prompt"
#define DISTINGUISHED_NAME      "distinguished_name"
#define ATTRIBUTES      "attributes"
#define V3_EXTENSIONS   "x509_extensions"
#define REQ_EXTENSIONS  "req_extensions"
#define STRING_MASK     "string_mask"
#define UTF8_IN         "utf8"

#define DEFAULT_KEY_LENGTH      2048
#define MIN_KEY_LENGTH          512

static int make_REQ(X509_REQ *req, EVP_PKEY *pkey, char *dn, int mutlirdn,
                    int attribs, unsigned long chtype);
static int build_subject(X509_REQ *req, const char *subj, unsigned long chtype,
                         int multirdn);
static int prompt_info(X509_REQ *req,
                       STACK_OF(CONF_VALUE) *dn_sk, const char *dn_sect,
                       STACK_OF(CONF_VALUE) *attr_sk, const char *attr_sect,
                       int attribs, unsigned long chtype);
static int auto_info(X509_REQ *req, STACK_OF(CONF_VALUE) *sk,
                     STACK_OF(CONF_VALUE) *attr, int attribs,
                     unsigned long chtype);
static int add_attribute_object(X509_REQ *req, char *text, const char *def,
                                char *value, int nid, int n_min, int n_max,
                                unsigned long chtype);
static int add_DN_object(X509_NAME *n, char *text, const char *def,
                         char *value, int nid, int n_min, int n_max,
                         unsigned long chtype, int mval);
static int genpkey_cb(EVP_PKEY_CTX *ctx);
static int build_data(char *text, const char *def,
                      char *value, int n_min, int n_max,
                      char *buf, const int buf_size,
                      const char *desc1, const char *desc2
                      );
static int req_check_len(int len, int n_min, int n_max);
static int check_end(const char *str, const char *end);
static int join(char buf[], size_t buf_size, const char *name,
                const char *tail, const char *desc);
static EVP_PKEY_CTX *set_keygen_ctx(const char *gstr,
                                    int *pkey_type, long *pkeylen,
                                    char **palgnam, ENGINE *keygen_engine);
static CONF *req_conf = NULL;
static CONF *addext_conf = NULL;
static int batch = 0;

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_INFORM, OPT_OUTFORM, OPT_ENGINE, OPT_KEYGEN_ENGINE, OPT_KEY,
    OPT_PUBKEY, OPT_NEW, OPT_CONFIG, OPT_KEYFORM, OPT_IN, OPT_OUT,
    OPT_KEYOUT, OPT_PASSIN, OPT_PASSOUT, OPT_NEWKEY,
    OPT_PKEYOPT, OPT_SIGOPT, OPT_BATCH, OPT_NEWHDR, OPT_MODULUS,
    OPT_VERIFY, OPT_NODES, OPT_NOOUT, OPT_VERBOSE, OPT_UTF8,
    OPT_NAMEOPT, OPT_REQOPT, OPT_SUBJ, OPT_SUBJECT, OPT_TEXT, OPT_X509,
    OPT_MULTIVALUE_RDN, OPT_DAYS, OPT_SET_SERIAL, OPT_ADDEXT, OPT_EXTENSIONS,
    OPT_REQEXTS, OPT_PRECERT, OPT_MD,
    OPT_R_ENUM
} OPTION_CHOICE;

const OPTIONS req_options[] = {
    {"help", OPT_HELP, '-', "Display this summary"},
    {"inform", OPT_INFORM, 'F', "Input format - DER or PEM"},
    {"outform", OPT_OUTFORM, 'F', "Output format - DER or PEM"},
    {"in", OPT_IN, '<', "Input file"},
    {"out", OPT_OUT, '>', "Output file"},
    {"key", OPT_KEY, 's', "Private key to use"},
    {"keyform", OPT_KEYFORM, 'f', "Key file format"},
    {"pubkey", OPT_PUBKEY, '-', "Output public key"},
    {"new", OPT_NEW, '-', "New request"},
    {"config", OPT_CONFIG, '<', "Request template file"},
    {"keyout", OPT_KEYOUT, '>', "File to send the key to"},
    {"passin", OPT_PASSIN, 's', "Private key password source"},
    {"passout", OPT_PASSOUT, 's', "Output file pass phrase source"},
    OPT_R_OPTIONS,
    {"newkey", OPT_NEWKEY, 's', "Specify as type:bits"},
    {"pkeyopt", OPT_PKEYOPT, 's', "Public key options as opt:value"},
    {"sigopt", OPT_SIGOPT, 's', "Signature parameter in n:v form"},
    {"batch", OPT_BATCH, '-',
     "Do not ask anything during request generation"},
    {"newhdr", OPT_NEWHDR, '-', "Output \"NEW\" in the header lines"},
    {"modulus", OPT_MODULUS, '-', "RSA modulus"},
    {"verify", OPT_VERIFY, '-', "Verify signature on REQ"},
    {"nodes", OPT_NODES, '-', "Don't encrypt the output key"},
    {"noout", OPT_NOOUT, '-', "Do not output REQ"},
    {"verbose", OPT_VERBOSE, '-', "Verbose output"},
    {"utf8", OPT_UTF8, '-', "Input characters are UTF8 (default ASCII)"},
    {"nameopt", OPT_NAMEOPT, 's', "Various certificate name options"},
    {"reqopt", OPT_REQOPT, 's', "Various request text options"},
    {"text", OPT_TEXT, '-', "Text form of request"},
    {"x509", OPT_X509, '-',
     "Output a x509 structure instead of a cert request"},
    {OPT_MORE_STR, 1, 1, "(Required by some CA's)"},
    {"subj", OPT_SUBJ, 's', "Set or modify request subject"},
    {"subject", OPT_SUBJECT, '-', "Output the request's subject"},
    {"multivalue-rdn", OPT_MULTIVALUE_RDN, '-',
     "Enable support for multivalued RDNs"},
    {"days", OPT_DAYS, 'p', "Number of days cert is valid for"},
    {"set_serial", OPT_SET_SERIAL, 's', "Serial number to use"},
    {"addext", OPT_ADDEXT, 's',
     "Additional cert extension key=value pair (may be given more than once)"},
    {"extensions", OPT_EXTENSIONS, 's',
     "Cert extension section (override value in config file)"},
    {"reqexts", OPT_REQEXTS, 's',
     "Request extension section (override value in config file)"},
    {"precert", OPT_PRECERT, '-', "Add a poison extension (implies -new)"},
    {"", OPT_MD, '-', "Any supported digest"},
#ifndef OPENSSL_NO_ENGINE
    {"engine", OPT_ENGINE, 's', "Use engine, possibly a hardware device"},
    {"keygen_engine", OPT_KEYGEN_ENGINE, 's',
     "Specify engine to be used for key generation operations"},
#endif
    {NULL}
};


/*
 * An LHASH of strings, where each string is an extension name.
 */
static unsigned long ext_name_hash(const OPENSSL_STRING *a)
{
    return OPENSSL_LH_strhash((const char *)a);
}

static int ext_name_cmp(const OPENSSL_STRING *a, const OPENSSL_STRING *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static void exts_cleanup(OPENSSL_STRING *x)
{
    OPENSSL_free((char *)x);
}

/*
 * Is the |kv| key already duplicated?  This is remarkably tricky to get
 * right.  Return 0 if unique, -1 on runtime error; 1 if found or a syntax
 * error.
 */
static int duplicated(LHASH_OF(OPENSSL_STRING) *addexts, char *kv)
{
    char *p;
    size_t off;

    /* Check syntax. */
    /* Skip leading whitespace, make a copy. */
    while (*kv && isspace(*kv))
        if (*++kv == '\0')
            return 1;
    if ((p = strchr(kv, '=')) == NULL)
        return 1;
    off = p - kv;
    if ((kv = OPENSSL_strdup(kv)) == NULL)
        return -1;

    /* Skip trailing space before the equal sign. */
    for (p = kv + off; p > kv; --p)
        if (!isspace(p[-1]))
            break;
    if (p == kv) {
        OPENSSL_free(kv);
        return 1;
    }
    *p = '\0';

    /* Finally have a clean "key"; see if it's there [by attempt to add it]. */
    if ((p = (char *)lh_OPENSSL_STRING_insert(addexts, (OPENSSL_STRING*)kv))
        != NULL || lh_OPENSSL_STRING_error(addexts)) {
        OPENSSL_free(p != NULL ? p : kv);
        return -1;
    }

    return 0;
}

int req_main(int argc, char **argv)
{
    ASN1_INTEGER *serial = NULL;
    BIO *in = NULL, *out = NULL;
    ENGINE *e = NULL, *gen_eng = NULL;
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *genctx = NULL;
    STACK_OF(OPENSSL_STRING) *pkeyopts = NULL, *sigopts = NULL;
    LHASH_OF(OPENSSL_STRING) *addexts = NULL;
    X509 *x509ss = NULL;
    X509_REQ *req = NULL;
    const EVP_CIPHER *cipher = NULL;
    const EVP_MD *md_alg = NULL, *digest = NULL;
    BIO *addext_bio = NULL;
    char *extensions = NULL, *infile = NULL;
    char *outfile = NULL, *keyfile = NULL;
    char *keyalgstr = NULL, *p, *prog, *passargin = NULL, *passargout = NULL;
    char *passin = NULL, *passout = NULL;
    char *nofree_passin = NULL, *nofree_passout = NULL;
    char *req_exts = NULL, *subj = NULL;
    char *template = default_config_file, *keyout = NULL;
    const char *keyalg = NULL;
    OPTION_CHOICE o;
    int ret = 1, x509 = 0, days = 0, i = 0, newreq = 0, verbose = 0;
    int pkey_type = -1, private = 0;
    int informat = FORMAT_PEM, outformat = FORMAT_PEM, keyform = FORMAT_PEM;
    int modulus = 0, multirdn = 0, verify = 0, noout = 0, text = 0;
    int nodes = 0, newhdr = 0, subject = 0, pubkey = 0, precert = 0;
    long newkey = -1;
    unsigned long chtype = MBSTRING_ASC, reqflag = 0;

#ifndef OPENSSL_NO_DES
    cipher = EVP_des_ede3_cbc();
#endif

    prog = opt_init(argc, argv, req_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
 opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(req_options);
            ret = 0;
            goto end;
        case OPT_INFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PEMDER, &informat))
                goto opthelp;
            break;
        case OPT_OUTFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PEMDER, &outformat))
                goto opthelp;
            break;
        case OPT_ENGINE:
            e = setup_engine(opt_arg(), 0);
            break;
        case OPT_KEYGEN_ENGINE:
#ifndef OPENSSL_NO_ENGINE
            gen_eng = ENGINE_by_id(opt_arg());
            if (gen_eng == NULL) {
                BIO_printf(bio_err, "Can't find keygen engine %s\n", *argv);
                goto opthelp;
            }
#endif
            break;
        case OPT_KEY:
            keyfile = opt_arg();
            break;
        case OPT_PUBKEY:
            pubkey = 1;
            break;
        case OPT_NEW:
            newreq = 1;
            break;
        case OPT_CONFIG:
            template = opt_arg();
            break;
        case OPT_KEYFORM:
            if (!opt_format(opt_arg(), OPT_FMT_ANY, &keyform))
                goto opthelp;
            break;
        case OPT_IN:
            infile = opt_arg();
            break;
        case OPT_OUT:
            outfile = opt_arg();
            break;
        case OPT_KEYOUT:
            keyout = opt_arg();
            break;
        case OPT_PASSIN:
            passargin = opt_arg();
            break;
        case OPT_PASSOUT:
            passargout = opt_arg();
            break;
        case OPT_R_CASES:
            if (!opt_rand(o))
                goto end;
            break;
        case OPT_NEWKEY:
            keyalg = opt_arg();
            newreq = 1;
            break;
        case OPT_PKEYOPT:
            if (!pkeyopts)
                pkeyopts = sk_OPENSSL_STRING_new_null();
            if (!pkeyopts || !sk_OPENSSL_STRING_push(pkeyopts, opt_arg()))
                goto opthelp;
            break;
        case OPT_SIGOPT:
            if (!sigopts)
                sigopts = sk_OPENSSL_STRING_new_null();
            if (!sigopts || !sk_OPENSSL_STRING_push(sigopts, opt_arg()))
                goto opthelp;
            break;
        case OPT_BATCH:
            batch = 1;
            break;
        case OPT_NEWHDR:
            newhdr = 1;
            break;
        case OPT_MODULUS:
            modulus = 1;
            break;
        case OPT_VERIFY:
            verify = 1;
            break;
        case OPT_NODES:
            nodes = 1;
            break;
        case OPT_NOOUT:
            noout = 1;
            break;
        case OPT_VERBOSE:
            verbose = 1;
            break;
        case OPT_UTF8:
            chtype = MBSTRING_UTF8;
            break;
        case OPT_NAMEOPT:
            if (!set_nameopt(opt_arg()))
                goto opthelp;
            break;
        case OPT_REQOPT:
            if (!set_cert_ex(&reqflag, opt_arg()))
                goto opthelp;
            break;
        case OPT_TEXT:
            text = 1;
            break;
        case OPT_X509:
            x509 = 1;
            break;
        case OPT_DAYS:
            days = atoi(opt_arg());
            break;
        case OPT_SET_SERIAL:
            if (serial != NULL) {
                BIO_printf(bio_err, "Serial number supplied twice\n");
                goto opthelp;
            }
            serial = s2i_ASN1_INTEGER(NULL, opt_arg());
            if (serial == NULL)
                goto opthelp;
            break;
        case OPT_SUBJECT:
            subject = 1;
            break;
        case OPT_SUBJ:
            subj = opt_arg();
            break;
        case OPT_MULTIVALUE_RDN:
            multirdn = 1;
            break;
        case OPT_ADDEXT:
            p = opt_arg();
            if (addexts == NULL) {
                addexts = lh_OPENSSL_STRING_new(ext_name_hash, ext_name_cmp);
                addext_bio = BIO_new(BIO_s_mem());
                if (addexts == NULL || addext_bio == NULL)
                    goto end;
            }
            i = duplicated(addexts, p);
            if (i == 1)
                goto opthelp;
            if (i < 0 || BIO_printf(addext_bio, "%s\n", opt_arg()) < 0)
                goto end;
            break;
        case OPT_EXTENSIONS:
            extensions = opt_arg();
            break;
        case OPT_REQEXTS:
            req_exts = opt_arg();
            break;
        case OPT_PRECERT:
            newreq = precert = 1;
            break;
        case OPT_MD:
            if (!opt_md(opt_unknown(), &md_alg))
                goto opthelp;
            digest = md_alg;
            break;
        }
    }
    argc = opt_num_rest();
    if (argc != 0)
        goto opthelp;

    if (days && !x509)
        BIO_printf(bio_err, "Ignoring -days; not generating a certificate\n");
    if (x509 && infile == NULL)
        newreq = 1;

    /* TODO: simplify this as pkey is still always NULL here */
    private = newreq && (pkey == NULL) ? 1 : 0;

    if (!app_passwd(passargin, passargout, &passin, &passout)) {
        BIO_printf(bio_err, "Error getting passwords\n");
        goto end;
    }

    if (verbose)
        BIO_printf(bio_err, "Using configuration from %s\n", template);
    req_conf = app_load_config(template);
    if (addext_bio) {
        if (verbose)
            BIO_printf(bio_err,
                       "Using additional configuration from command line\n");
        addext_conf = app_load_config_bio(addext_bio, NULL);
    }
    if (template != default_config_file && !app_load_modules(req_conf))
        goto end;

    if (req_conf != NULL) {
        p = NCONF_get_string(req_conf, NULL, "oid_file");
        if (p == NULL)
            ERR_clear_error();
        if (p != NULL) {
            BIO *oid_bio;

            oid_bio = BIO_new_file(p, "r");
            if (oid_bio == NULL) {
                /*-
                BIO_printf(bio_err,"problems opening %s for extra oid's\n",p);
                ERR_print_errors(bio_err);
                */
            } else {
                OBJ_create_objects(oid_bio);
                BIO_free(oid_bio);
            }
        }
    }
    if (!add_oid_section(req_conf))
        goto end;

    if (md_alg == NULL) {
        p = NCONF_get_string(req_conf, SECTION, "default_md");
        if (p == NULL) {
            ERR_clear_error();
        } else {
            if (!opt_md(p, &md_alg))
                goto opthelp;
            digest = md_alg;
        }
    }

    if (extensions == NULL) {
        extensions = NCONF_get_string(req_conf, SECTION, V3_EXTENSIONS);
        if (extensions == NULL)
            ERR_clear_error();
    }
    if (extensions != NULL) {
        /* Check syntax of file */
        X509V3_CTX ctx;
        X509V3_set_ctx_test(&ctx);
        X509V3_set_nconf(&ctx, req_conf);
        if (!X509V3_EXT_add_nconf(req_conf, &ctx, extensions, NULL)) {
            BIO_printf(bio_err,
                       "Error Loading extension section %s\n", extensions);
            goto end;
        }
    }
    if (addext_conf != NULL) {
        /* Check syntax of command line extensions */
        X509V3_CTX ctx;
        X509V3_set_ctx_test(&ctx);
        X509V3_set_nconf(&ctx, addext_conf);
        if (!X509V3_EXT_add_nconf(addext_conf, &ctx, "default", NULL)) {
            BIO_printf(bio_err, "Error Loading command line extensions\n");
            goto end;
        }
    }

    if (passin == NULL) {
        passin = nofree_passin =
            NCONF_get_string(req_conf, SECTION, "input_password");
        if (passin == NULL)
            ERR_clear_error();
    }

    if (passout == NULL) {
        passout = nofree_passout =
            NCONF_get_string(req_conf, SECTION, "output_password");
        if (passout == NULL)
            ERR_clear_error();
    }

    p = NCONF_get_string(req_conf, SECTION, STRING_MASK);
    if (p == NULL)
        ERR_clear_error();

    if (p != NULL && !ASN1_STRING_set_default_mask_asc(p)) {
        BIO_printf(bio_err, "Invalid global string mask setting %s\n", p);
        goto end;
    }

    if (chtype != MBSTRING_UTF8) {
        p = NCONF_get_string(req_conf, SECTION, UTF8_IN);
        if (p == NULL)
            ERR_clear_error();
        else if (strcmp(p, "yes") == 0)
            chtype = MBSTRING_UTF8;
    }

    if (req_exts == NULL) {
        req_exts = NCONF_get_string(req_conf, SECTION, REQ_EXTENSIONS);
        if (req_exts == NULL)
            ERR_clear_error();
    }
    if (req_exts != NULL) {
        /* Check syntax of file */
        X509V3_CTX ctx;
        X509V3_set_ctx_test(&ctx);
        X509V3_set_nconf(&ctx, req_conf);
        if (!X509V3_EXT_add_nconf(req_conf, &ctx, req_exts, NULL)) {
            BIO_printf(bio_err,
                       "Error Loading request extension section %s\n",
                       req_exts);
            goto end;
        }
    }

    if (keyfile != NULL) {
        pkey = load_key(keyfile, keyform, 0, passin, e, "Private Key");
        if (pkey == NULL) {
            /* load_key() has already printed an appropriate message */
            goto end;
        } else {
            app_RAND_load_conf(req_conf, SECTION);
        }
    }

    if (newreq && (pkey == NULL)) {
        app_RAND_load_conf(req_conf, SECTION);

        if (!NCONF_get_number(req_conf, SECTION, BITS, &newkey)) {
            newkey = DEFAULT_KEY_LENGTH;
        }

        if (keyalg != NULL) {
            genctx = set_keygen_ctx(keyalg, &pkey_type, &newkey,
                                    &keyalgstr, gen_eng);
            if (genctx == NULL)
                goto end;
        }

        if (newkey < MIN_KEY_LENGTH
            && (pkey_type == EVP_PKEY_RSA || pkey_type == EVP_PKEY_DSA)) {
            BIO_printf(bio_err, "private key length is too short,\n");
            BIO_printf(bio_err, "it needs to be at least %d bits, not %ld\n",
                       MIN_KEY_LENGTH, newkey);
            goto end;
        }

        if (pkey_type == EVP_PKEY_RSA && newkey > OPENSSL_RSA_MAX_MODULUS_BITS)
            BIO_printf(bio_err,
                       "Warning: It is not recommended to use more than %d bit for RSA keys.\n"
                       "         Your key size is %ld! Larger key size may behave not as expected.\n",
                       OPENSSL_RSA_MAX_MODULUS_BITS, newkey);

#ifndef OPENSSL_NO_DSA
        if (pkey_type == EVP_PKEY_DSA && newkey > OPENSSL_DSA_MAX_MODULUS_BITS)
            BIO_printf(bio_err,
                       "Warning: It is not recommended to use more than %d bit for DSA keys.\n"
                       "         Your key size is %ld! Larger key size may behave not as expected.\n",
                       OPENSSL_DSA_MAX_MODULUS_BITS, newkey);
#endif

        if (genctx == NULL) {
            genctx = set_keygen_ctx(NULL, &pkey_type, &newkey,
                                    &keyalgstr, gen_eng);
            if (!genctx)
                goto end;
        }

        if (pkeyopts != NULL) {
            char *genopt;
            for (i = 0; i < sk_OPENSSL_STRING_num(pkeyopts); i++) {
                genopt = sk_OPENSSL_STRING_value(pkeyopts, i);
                if (pkey_ctrl_string(genctx, genopt) <= 0) {
                    BIO_printf(bio_err, "parameter error \"%s\"\n", genopt);
                    ERR_print_errors(bio_err);
                    goto end;
                }
            }
        }

        if (pkey_type == EVP_PKEY_EC) {
            BIO_printf(bio_err, "Generating an EC private key\n");
        } else {
            BIO_printf(bio_err, "Generating a %s private key\n", keyalgstr);
        }

        EVP_PKEY_CTX_set_cb(genctx, genpkey_cb);
        EVP_PKEY_CTX_set_app_data(genctx, bio_err);

        if (EVP_PKEY_keygen(genctx, &pkey) <= 0) {
            BIO_puts(bio_err, "Error Generating Key\n");
            goto end;
        }

        EVP_PKEY_CTX_free(genctx);
        genctx = NULL;

        if (keyout == NULL) {
            keyout = NCONF_get_string(req_conf, SECTION, KEYFILE);
            if (keyout == NULL)
                ERR_clear_error();
        }

        if (keyout == NULL)
            BIO_printf(bio_err, "writing new private key to stdout\n");
        else
            BIO_printf(bio_err, "writing new private key to '%s'\n", keyout);
        out = bio_open_owner(keyout, outformat, private);
        if (out == NULL)
            goto end;

        p = NCONF_get_string(req_conf, SECTION, "encrypt_rsa_key");
        if (p == NULL) {
            ERR_clear_error();
            p = NCONF_get_string(req_conf, SECTION, "encrypt_key");
            if (p == NULL)
                ERR_clear_error();
        }
        if ((p != NULL) && (strcmp(p, "no") == 0))
            cipher = NULL;
        if (nodes)
            cipher = NULL;

        i = 0;
 loop:
        assert(private);
        if (!PEM_write_bio_PrivateKey(out, pkey, cipher,
                                      NULL, 0, NULL, passout)) {
            if ((ERR_GET_REASON(ERR_peek_error()) ==
                 PEM_R_PROBLEMS_GETTING_PASSWORD) && (i < 3)) {
                ERR_clear_error();
                i++;
                goto loop;
            }
            goto end;
        }
        BIO_free(out);
        out = NULL;
        BIO_printf(bio_err, "-----\n");
    }

    if (!newreq) {
        in = bio_open_default(infile, 'r', informat);
        if (in == NULL)
            goto end;

        if (informat == FORMAT_ASN1)
            req = d2i_X509_REQ_bio(in, NULL);
        else
            req = PEM_read_bio_X509_REQ(in, NULL, NULL, NULL);
        if (req == NULL) {
            BIO_printf(bio_err, "unable to load X509 request\n");
            goto end;
        }
    }

    if (newreq || x509) {
        if (pkey == NULL) {
            BIO_printf(bio_err, "you need to specify a private key\n");
            goto end;
        }

        if (req == NULL) {
            req = X509_REQ_new();
            if (req == NULL) {
                goto end;
            }

            i = make_REQ(req, pkey, subj, multirdn, !x509, chtype);
            subj = NULL;        /* done processing '-subj' option */
            if (!i) {
                BIO_printf(bio_err, "problems making Certificate Request\n");
                goto end;
            }
        }
        if (x509) {
            EVP_PKEY *tmppkey;
            X509V3_CTX ext_ctx;
            if ((x509ss = X509_new()) == NULL)
                goto end;

            /* Set version to V3 */
            if ((extensions != NULL || addext_conf != NULL)
                && !X509_set_version(x509ss, 2))
                goto end;
            if (serial != NULL) {
                if (!X509_set_serialNumber(x509ss, serial))
                    goto end;
            } else {
                if (!rand_serial(NULL, X509_get_serialNumber(x509ss)))
                    goto end;
            }

            if (!X509_set_issuer_name(x509ss, X509_REQ_get_subject_name(req)))
                goto end;
            if (days == 0) {
                /* set default days if it's not specified */
                days = 30;
            }
            if (!set_cert_times(x509ss, NULL, NULL, days))
                goto end;
            if (!X509_set_subject_name
                (x509ss, X509_REQ_get_subject_name(req)))
                goto end;
            tmppkey = X509_REQ_get0_pubkey(req);
            if (!tmppkey || !X509_set_pubkey(x509ss, tmppkey))
                goto end;

            /* Set up V3 context struct */

            X509V3_set_ctx(&ext_ctx, x509ss, x509ss, NULL, NULL, 0);
            X509V3_set_nconf(&ext_ctx, req_conf);

            /* Add extensions */
            if (extensions != NULL && !X509V3_EXT_add_nconf(req_conf,
                                                            &ext_ctx, extensions,
                                                            x509ss)) {
                BIO_printf(bio_err, "Error Loading extension section %s\n",
                           extensions);
                goto end;
            }
            if (addext_conf != NULL
                && !X509V3_EXT_add_nconf(addext_conf, &ext_ctx, "default",
                                         x509ss)) {
                BIO_printf(bio_err, "Error Loading command line extensions\n");
                goto end;
            }

            /* If a pre-cert was requested, we need to add a poison extension */
            if (precert) {
                if (X509_add1_ext_i2d(x509ss, NID_ct_precert_poison, NULL, 1, 0)
                    != 1) {
                    BIO_printf(bio_err, "Error adding poison extension\n");
                    goto end;
                }
            }

            i = do_X509_sign(x509ss, pkey, digest, sigopts);
            if (!i) {
                ERR_print_errors(bio_err);
                goto end;
            }
        } else {
            X509V3_CTX ext_ctx;

            /* Set up V3 context struct */

            X509V3_set_ctx(&ext_ctx, NULL, NULL, req, NULL, 0);
            X509V3_set_nconf(&ext_ctx, req_conf);

            /* Add extensions */
            if (req_exts != NULL
                && !X509V3_EXT_REQ_add_nconf(req_conf, &ext_ctx,
                                             req_exts, req)) {
                BIO_printf(bio_err, "Error Loading extension section %s\n",
                           req_exts);
                goto end;
            }
            if (addext_conf != NULL
                && !X509V3_EXT_REQ_add_nconf(addext_conf, &ext_ctx, "default",
                                             req)) {
                BIO_printf(bio_err, "Error Loading command line extensions\n");
                goto end;
            }
            i = do_X509_REQ_sign(req, pkey, digest, sigopts);
            if (!i) {
                ERR_print_errors(bio_err);
                goto end;
            }
        }
    }

    if (subj && x509) {
        BIO_printf(bio_err, "Cannot modify certificate subject\n");
        goto end;
    }

    if (subj && !x509) {
        if (verbose) {
            BIO_printf(bio_err, "Modifying Request's Subject\n");
            print_name(bio_err, "old subject=",
                       X509_REQ_get_subject_name(req), get_nameopt());
        }

        if (build_subject(req, subj, chtype, multirdn) == 0) {
            BIO_printf(bio_err, "ERROR: cannot modify subject\n");
            ret = 1;
            goto end;
        }

        if (verbose) {
            print_name(bio_err, "new subject=",
                       X509_REQ_get_subject_name(req), get_nameopt());
        }
    }

    if (verify && !x509) {
        EVP_PKEY *tpubkey = pkey;

        if (tpubkey == NULL) {
            tpubkey = X509_REQ_get0_pubkey(req);
            if (tpubkey == NULL)
                goto end;
        }

        i = X509_REQ_verify(req, tpubkey);

        if (i < 0) {
            goto end;
        } else if (i == 0) {
            BIO_printf(bio_err, "verify failure\n");
            ERR_print_errors(bio_err);
        } else {                 /* if (i > 0) */
            BIO_printf(bio_err, "verify OK\n");
        }
    }

    if (noout && !text && !modulus && !subject && !pubkey) {
        ret = 0;
        goto end;
    }

    out = bio_open_default(outfile,
                           keyout != NULL && outfile != NULL &&
                           strcmp(keyout, outfile) == 0 ? 'a' : 'w',
                           outformat);
    if (out == NULL)
        goto end;

    if (pubkey) {
        EVP_PKEY *tpubkey = X509_REQ_get0_pubkey(req);

        if (tpubkey == NULL) {
            BIO_printf(bio_err, "Error getting public key\n");
            ERR_print_errors(bio_err);
            goto end;
        }
        PEM_write_bio_PUBKEY(out, tpubkey);
    }

    if (text) {
        if (x509)
            X509_print_ex(out, x509ss, get_nameopt(), reqflag);
        else
            X509_REQ_print_ex(out, req, get_nameopt(), reqflag);
    }

    if (subject) {
        if (x509)
            print_name(out, "subject=", X509_get_subject_name(x509ss),
                       get_nameopt());
        else
            print_name(out, "subject=", X509_REQ_get_subject_name(req),
                       get_nameopt());
    }

    if (modulus) {
        EVP_PKEY *tpubkey;

        if (x509)
            tpubkey = X509_get0_pubkey(x509ss);
        else
            tpubkey = X509_REQ_get0_pubkey(req);
        if (tpubkey == NULL) {
            fprintf(stdout, "Modulus=unavailable\n");
            goto end;
        }
        fprintf(stdout, "Modulus=");
#ifndef OPENSSL_NO_RSA
        if (EVP_PKEY_base_id(tpubkey) == EVP_PKEY_RSA) {
            const BIGNUM *n;
            RSA_get0_key(EVP_PKEY_get0_RSA(tpubkey), &n, NULL, NULL);
            BN_print(out, n);
        } else
#endif
            fprintf(stdout, "Wrong Algorithm type");
        fprintf(stdout, "\n");
    }

    if (!noout && !x509) {
        if (outformat == FORMAT_ASN1)
            i = i2d_X509_REQ_bio(out, req);
        else if (newhdr)
            i = PEM_write_bio_X509_REQ_NEW(out, req);
        else
            i = PEM_write_bio_X509_REQ(out, req);
        if (!i) {
            BIO_printf(bio_err, "unable to write X509 request\n");
            goto end;
        }
    }
    if (!noout && x509 && (x509ss != NULL)) {
        if (outformat == FORMAT_ASN1)
            i = i2d_X509_bio(out, x509ss);
        else
            i = PEM_write_bio_X509(out, x509ss);
        if (!i) {
            BIO_printf(bio_err, "unable to write X509 certificate\n");
            goto end;
        }
    }
    ret = 0;
 end:
    if (ret) {
        ERR_print_errors(bio_err);
    }
    NCONF_free(req_conf);
    NCONF_free(addext_conf);
    BIO_free(addext_bio);
    BIO_free(in);
    BIO_free_all(out);
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(genctx);
    sk_OPENSSL_STRING_free(pkeyopts);
    sk_OPENSSL_STRING_free(sigopts);
    lh_OPENSSL_STRING_doall(addexts, exts_cleanup);
    lh_OPENSSL_STRING_free(addexts);
#ifndef OPENSSL_NO_ENGINE
    ENGINE_free(gen_eng);
#endif
    OPENSSL_free(keyalgstr);
    X509_REQ_free(req);
    X509_free(x509ss);
    ASN1_INTEGER_free(serial);
    release_engine(e);
    if (passin != nofree_passin)
        OPENSSL_free(passin);
    if (passout != nofree_passout)
        OPENSSL_free(passout);
    return ret;
}

static int make_REQ(X509_REQ *req, EVP_PKEY *pkey, char *subj, int multirdn,
                    int attribs, unsigned long chtype)
{
    int ret = 0, i;
    char no_prompt = 0;
    STACK_OF(CONF_VALUE) *dn_sk, *attr_sk = NULL;
    char *tmp, *dn_sect, *attr_sect;

    tmp = NCONF_get_string(req_conf, SECTION, PROMPT);
    if (tmp == NULL)
        ERR_clear_error();
    if ((tmp != NULL) && strcmp(tmp, "no") == 0)
        no_prompt = 1;

    dn_sect = NCONF_get_string(req_conf, SECTION, DISTINGUISHED_NAME);
    if (dn_sect == NULL) {
        BIO_printf(bio_err, "unable to find '%s' in config\n",
                   DISTINGUISHED_NAME);
        goto err;
    }
    dn_sk = NCONF_get_section(req_conf, dn_sect);
    if (dn_sk == NULL) {
        BIO_printf(bio_err, "unable to get '%s' section\n", dn_sect);
        goto err;
    }

    attr_sect = NCONF_get_string(req_conf, SECTION, ATTRIBUTES);
    if (attr_sect == NULL) {
        ERR_clear_error();
        attr_sk = NULL;
    } else {
        attr_sk = NCONF_get_section(req_conf, attr_sect);
        if (attr_sk == NULL) {
            BIO_printf(bio_err, "unable to get '%s' section\n", attr_sect);
            goto err;
        }
    }

    /* setup version number */
    if (!X509_REQ_set_version(req, 0L))
        goto err;               /* version 1 */

    if (subj)
        i = build_subject(req, subj, chtype, multirdn);
    else if (no_prompt)
        i = auto_info(req, dn_sk, attr_sk, attribs, chtype);
    else
        i = prompt_info(req, dn_sk, dn_sect, attr_sk, attr_sect, attribs,
                        chtype);
    if (!i)
        goto err;

    if (!X509_REQ_set_pubkey(req, pkey))
        goto err;

    ret = 1;
 err:
    return ret;
}

/*
 * subject is expected to be in the format /type0=value0/type1=value1/type2=...
 * where characters may be escaped by \
 */
static int build_subject(X509_REQ *req, const char *subject, unsigned long chtype,
                         int multirdn)
{
    X509_NAME *n;

    if ((n = parse_name(subject, chtype, multirdn)) == NULL)
        return 0;

    if (!X509_REQ_set_subject_name(req, n)) {
        X509_NAME_free(n);
        return 0;
    }
    X509_NAME_free(n);
    return 1;
}

static int prompt_info(X509_REQ *req,
                       STACK_OF(CONF_VALUE) *dn_sk, const char *dn_sect,
                       STACK_OF(CONF_VALUE) *attr_sk, const char *attr_sect,
                       int attribs, unsigned long chtype)
{
    int i;
    char *p, *q;
    char buf[100];
    int nid, mval;
    long n_min, n_max;
    char *type, *value;
    const char *def;
    CONF_VALUE *v;
    X509_NAME *subj;
    subj = X509_REQ_get_subject_name(req);

    if (!batch) {
        BIO_printf(bio_err,
                   "You are about to be asked to enter information that will be incorporated\n");
        BIO_printf(bio_err, "into your certificate request.\n");
        BIO_printf(bio_err,
                   "What you are about to enter is what is called a Distinguished Name or a DN.\n");
        BIO_printf(bio_err,
                   "There are quite a few fields but you can leave some blank\n");
        BIO_printf(bio_err,
                   "For some fields there will be a default value,\n");
        BIO_printf(bio_err,
                   "If you enter '.', the field will be left blank.\n");
        BIO_printf(bio_err, "-----\n");
    }

    if (sk_CONF_VALUE_num(dn_sk)) {
        i = -1;
 start:
        for ( ; ; ) {
            i++;
            if (sk_CONF_VALUE_num(dn_sk) <= i)
                break;

            v = sk_CONF_VALUE_value(dn_sk, i);
            p = q = NULL;
            type = v->name;
            if (!check_end(type, "_min") || !check_end(type, "_max") ||
                !check_end(type, "_default") || !check_end(type, "_value"))
                continue;
            /*
             * Skip past any leading X. X: X, etc to allow for multiple
             * instances
             */
            for (p = v->name; *p; p++)
                if ((*p == ':') || (*p == ',') || (*p == '.')) {
                    p++;
                    if (*p)
                        type = p;
                    break;
                }
            if (*type == '+') {
                mval = -1;
                type++;
            } else {
                mval = 0;
            }
            /* If OBJ not recognised ignore it */
            if ((nid = OBJ_txt2nid(type)) == NID_undef)
                goto start;
            if (!join(buf, sizeof(buf), v->name, "_default", "Name"))
                return 0;
            if ((def = NCONF_get_string(req_conf, dn_sect, buf)) == NULL) {
                ERR_clear_error();
                def = "";
            }

            if (!join(buf, sizeof(buf), v->name, "_value", "Name"))
                return 0;
            if ((value = NCONF_get_string(req_conf, dn_sect, buf)) == NULL) {
                ERR_clear_error();
                value = NULL;
            }

            if (!join(buf, sizeof(buf), v->name, "_min", "Name"))
                return 0;
            if (!NCONF_get_number(req_conf, dn_sect, buf, &n_min)) {
                ERR_clear_error();
                n_min = -1;
            }


            if (!join(buf, sizeof(buf), v->name, "_max", "Name"))
                return 0;
            if (!NCONF_get_number(req_conf, dn_sect, buf, &n_max)) {
                ERR_clear_error();
                n_max = -1;
            }

            if (!add_DN_object(subj, v->value, def, value, nid,
                               n_min, n_max, chtype, mval))
                return 0;
        }
        if (X509_NAME_entry_count(subj) == 0) {
            BIO_printf(bio_err,
                       "error, no objects specified in config file\n");
            return 0;
        }

        if (attribs) {
            if ((attr_sk != NULL) && (sk_CONF_VALUE_num(attr_sk) > 0)
                && (!batch)) {
                BIO_printf(bio_err,
                           "\nPlease enter the following 'extra' attributes\n");
                BIO_printf(bio_err,
                           "to be sent with your certificate request\n");
            }

            i = -1;
 start2:
            for ( ; ; ) {
                i++;
                if ((attr_sk == NULL) || (sk_CONF_VALUE_num(attr_sk) <= i))
                    break;

                v = sk_CONF_VALUE_value(attr_sk, i);
                type = v->name;
                if ((nid = OBJ_txt2nid(type)) == NID_undef)
                    goto start2;

                if (!join(buf, sizeof(buf), type, "_default", "Name"))
                    return 0;
                if ((def = NCONF_get_string(req_conf, attr_sect, buf))
                    == NULL) {
                    ERR_clear_error();
                    def = "";
                }

                if (!join(buf, sizeof(buf), type, "_value", "Name"))
                    return 0;
                if ((value = NCONF_get_string(req_conf, attr_sect, buf))
                    == NULL) {
                    ERR_clear_error();
                    value = NULL;
                }

                if (!join(buf, sizeof(buf), type,"_min", "Name"))
                    return 0;
                if (!NCONF_get_number(req_conf, attr_sect, buf, &n_min)) {
                    ERR_clear_error();
                    n_min = -1;
                }

                if (!join(buf, sizeof(buf), type, "_max", "Name"))
                    return 0;
                if (!NCONF_get_number(req_conf, attr_sect, buf, &n_max)) {
                    ERR_clear_error();
                    n_max = -1;
                }

                if (!add_attribute_object(req,
                                          v->value, def, value, nid, n_min,
                                          n_max, chtype))
                    return 0;
            }
        }
    } else {
        BIO_printf(bio_err, "No template, please set one up.\n");
        return 0;
    }

    return 1;

}

static int auto_info(X509_REQ *req, STACK_OF(CONF_VALUE) *dn_sk,
                     STACK_OF(CONF_VALUE) *attr_sk, int attribs,
                     unsigned long chtype)
{
    int i, spec_char, plus_char;
    char *p, *q;
    char *type;
    CONF_VALUE *v;
    X509_NAME *subj;

    subj = X509_REQ_get_subject_name(req);

    for (i = 0; i < sk_CONF_VALUE_num(dn_sk); i++) {
        int mval;
        v = sk_CONF_VALUE_value(dn_sk, i);
        p = q = NULL;
        type = v->name;
        /*
         * Skip past any leading X. X: X, etc to allow for multiple instances
         */
        for (p = v->name; *p; p++) {
#ifndef CHARSET_EBCDIC
            spec_char = ((*p == ':') || (*p == ',') || (*p == '.'));
#else
            spec_char = ((*p == os_toascii[':']) || (*p == os_toascii[','])
                    || (*p == os_toascii['.']));
#endif
            if (spec_char) {
                p++;
                if (*p)
                    type = p;
                break;
            }
        }
#ifndef CHARSET_EBCDIC
        plus_char = (*type == '+');
#else
        plus_char = (*type == os_toascii['+']);
#endif
        if (plus_char) {
            type++;
            mval = -1;
        } else {
            mval = 0;
        }
        if (!X509_NAME_add_entry_by_txt(subj, type, chtype,
                                        (unsigned char *)v->value, -1, -1,
                                        mval))
            return 0;

    }

    if (!X509_NAME_entry_count(subj)) {
        BIO_printf(bio_err, "error, no objects specified in config file\n");
        return 0;
    }
    if (attribs) {
        for (i = 0; i < sk_CONF_VALUE_num(attr_sk); i++) {
            v = sk_CONF_VALUE_value(attr_sk, i);
            if (!X509_REQ_add1_attr_by_txt(req, v->name, chtype,
                                           (unsigned char *)v->value, -1))
                return 0;
        }
    }
    return 1;
}

static int add_DN_object(X509_NAME *n, char *text, const char *def,
                         char *value, int nid, int n_min, int n_max,
                         unsigned long chtype, int mval)
{
    int ret = 0;
    char buf[1024];

    ret = build_data(text, def, value, n_min, n_max, buf, sizeof(buf),
                     "DN value", "DN default");
    if ((ret == 0) || (ret == 1))
        return ret;
    ret = 1;

    if (!X509_NAME_add_entry_by_NID(n, nid, chtype,
                                    (unsigned char *)buf, -1, -1, mval))
        ret = 0;

    return ret;
}

static int add_attribute_object(X509_REQ *req, char *text, const char *def,
                                char *value, int nid, int n_min,
                                int n_max, unsigned long chtype)
{
    int ret = 0;
    char buf[1024];

    ret = build_data(text, def, value, n_min, n_max, buf, sizeof(buf),
                     "Attribute value", "Attribute default");
    if ((ret == 0) || (ret == 1))
        return ret;
    ret = 1;

    if (!X509_REQ_add1_attr_by_NID(req, nid, chtype,
                                   (unsigned char *)buf, -1)) {
        BIO_printf(bio_err, "Error adding attribute\n");
        ERR_print_errors(bio_err);
        ret = 0;
    }

    return ret;
}


static int build_data(char *text, const char *def,
                         char *value, int n_min, int n_max,
                         char *buf, const int buf_size,
                         const char *desc1, const char *desc2
                         )
{
    int i;
 start:
    if (!batch)
        BIO_printf(bio_err, "%s [%s]:", text, def);
    (void)BIO_flush(bio_err);
    if (value != NULL) {
        if (!join(buf, buf_size, value, "\n", desc1))
            return 0;
        BIO_printf(bio_err, "%s\n", value);
    } else {
        buf[0] = '\0';
        if (!batch) {
            if (!fgets(buf, buf_size, stdin))
                return 0;
        } else {
            buf[0] = '\n';
            buf[1] = '\0';
        }
    }

    if (buf[0] == '\0')
        return 0;
    if (buf[0] == '\n') {
        if ((def == NULL) || (def[0] == '\0'))
            return 1;
        if (!join(buf, buf_size, def, "\n", desc2))
            return 0;
    } else if ((buf[0] == '.') && (buf[1] == '\n')) {
        return 1;
    }

    i = strlen(buf);
    if (buf[i - 1] != '\n') {
        BIO_printf(bio_err, "weird input :-(\n");
        return 0;
    }
    buf[--i] = '\0';
#ifdef CHARSET_EBCDIC
    ebcdic2ascii(buf, buf, i);
#endif
    if (!req_check_len(i, n_min, n_max)) {
        if (batch || value)
            return 0;
        goto start;
    }
    return 2;
}

static int req_check_len(int len, int n_min, int n_max)
{
    if ((n_min > 0) && (len < n_min)) {
        BIO_printf(bio_err,
                   "string is too short, it needs to be at least %d bytes long\n",
                   n_min);
        return 0;
    }
    if ((n_max >= 0) && (len > n_max)) {
        BIO_printf(bio_err,
                   "string is too long, it needs to be no more than %d bytes long\n",
                   n_max);
        return 0;
    }
    return 1;
}

/* Check if the end of a string matches 'end' */
static int check_end(const char *str, const char *end)
{
    size_t elen, slen;
    const char *tmp;

    elen = strlen(end);
    slen = strlen(str);
    if (elen > slen)
        return 1;
    tmp = str + slen - elen;
    return strcmp(tmp, end);
}

/*
 * Merge the two strings together into the result buffer checking for
 * overflow and producing an error message if there is.
 */
static int join(char buf[], size_t buf_size, const char *name,
                const char *tail, const char *desc)
{
    const size_t name_len = strlen(name), tail_len = strlen(tail);

    if (name_len + tail_len + 1 > buf_size) {
        BIO_printf(bio_err, "%s '%s' too long\n", desc, name);
        return 0;
    }
    memcpy(buf, name, name_len);
    memcpy(buf + name_len, tail, tail_len + 1);
    return 1;
}

static EVP_PKEY_CTX *set_keygen_ctx(const char *gstr,
                                    int *pkey_type, long *pkeylen,
                                    char **palgnam, ENGINE *keygen_engine)
{
    EVP_PKEY_CTX *gctx = NULL;
    EVP_PKEY *param = NULL;
    long keylen = -1;
    BIO *pbio = NULL;
    const char *paramfile = NULL;

    if (gstr == NULL) {
        *pkey_type = EVP_PKEY_RSA;
        keylen = *pkeylen;
    } else if (gstr[0] >= '0' && gstr[0] <= '9') {
        *pkey_type = EVP_PKEY_RSA;
        keylen = atol(gstr);
        *pkeylen = keylen;
    } else if (strncmp(gstr, "param:", 6) == 0) {
        paramfile = gstr + 6;
    } else {
        const char *p = strchr(gstr, ':');
        int len;
        ENGINE *tmpeng;
        const EVP_PKEY_ASN1_METHOD *ameth;

        if (p != NULL)
            len = p - gstr;
        else
            len = strlen(gstr);
        /*
         * The lookup of a the string will cover all engines so keep a note
         * of the implementation.
         */

        ameth = EVP_PKEY_asn1_find_str(&tmpeng, gstr, len);

        if (ameth == NULL) {
            BIO_printf(bio_err, "Unknown algorithm %.*s\n", len, gstr);
            return NULL;
        }

        EVP_PKEY_asn1_get0_info(NULL, pkey_type, NULL, NULL, NULL, ameth);
#ifndef OPENSSL_NO_ENGINE
        ENGINE_finish(tmpeng);
#endif
        if (*pkey_type == EVP_PKEY_RSA) {
            if (p != NULL) {
                keylen = atol(p + 1);
                *pkeylen = keylen;
            } else {
                keylen = *pkeylen;
            }
        } else if (p != NULL) {
            paramfile = p + 1;
        }
    }

    if (paramfile != NULL) {
        pbio = BIO_new_file(paramfile, "r");
        if (pbio == NULL) {
            BIO_printf(bio_err, "Can't open parameter file %s\n", paramfile);
            return NULL;
        }
        param = PEM_read_bio_Parameters(pbio, NULL);

        if (param == NULL) {
            X509 *x;

            (void)BIO_reset(pbio);
            x = PEM_read_bio_X509(pbio, NULL, NULL, NULL);
            if (x != NULL) {
                param = X509_get_pubkey(x);
                X509_free(x);
            }
        }

        BIO_free(pbio);

        if (param == NULL) {
            BIO_printf(bio_err, "Error reading parameter file %s\n", paramfile);
            return NULL;
        }
        if (*pkey_type == -1) {
            *pkey_type = EVP_PKEY_id(param);
        } else if (*pkey_type != EVP_PKEY_base_id(param)) {
            BIO_printf(bio_err, "Key Type does not match parameters\n");
            EVP_PKEY_free(param);
            return NULL;
        }
    }

    if (palgnam != NULL) {
        const EVP_PKEY_ASN1_METHOD *ameth;
        ENGINE *tmpeng;
        const char *anam;

        ameth = EVP_PKEY_asn1_find(&tmpeng, *pkey_type);
        if (ameth == NULL) {
            BIO_puts(bio_err, "Internal error: can't find key algorithm\n");
            return NULL;
        }
        EVP_PKEY_asn1_get0_info(NULL, NULL, NULL, NULL, &anam, ameth);
        *palgnam = OPENSSL_strdup(anam);
#ifndef OPENSSL_NO_ENGINE
        ENGINE_finish(tmpeng);
#endif
    }

    if (param != NULL) {
        gctx = EVP_PKEY_CTX_new(param, keygen_engine);
        *pkeylen = EVP_PKEY_bits(param);
        EVP_PKEY_free(param);
    } else {
        gctx = EVP_PKEY_CTX_new_id(*pkey_type, keygen_engine);
    }

    if (gctx == NULL) {
        BIO_puts(bio_err, "Error allocating keygen context\n");
        ERR_print_errors(bio_err);
        return NULL;
    }

    if (EVP_PKEY_keygen_init(gctx) <= 0) {
        BIO_puts(bio_err, "Error initializing keygen context\n");
        ERR_print_errors(bio_err);
        EVP_PKEY_CTX_free(gctx);
        return NULL;
    }
#ifndef OPENSSL_NO_RSA
    if ((*pkey_type == EVP_PKEY_RSA) && (keylen != -1)) {
        if (EVP_PKEY_CTX_set_rsa_keygen_bits(gctx, keylen) <= 0) {
            BIO_puts(bio_err, "Error setting RSA keysize\n");
            ERR_print_errors(bio_err);
            EVP_PKEY_CTX_free(gctx);
            return NULL;
        }
    }
#endif

    return gctx;
}

static int genpkey_cb(EVP_PKEY_CTX *ctx)
{
    char c = '*';
    BIO *b = EVP_PKEY_CTX_get_app_data(ctx);
    int p;
    p = EVP_PKEY_CTX_get_keygen_info(ctx, 0);
    if (p == 0)
        c = '.';
    if (p == 1)
        c = '+';
    if (p == 2)
        c = '*';
    if (p == 3)
        c = '\n';
    BIO_write(b, &c, 1);
    (void)BIO_flush(b);
    return 1;
}

static int do_sign_init(EVP_MD_CTX *ctx, EVP_PKEY *pkey,
                        const EVP_MD *md, STACK_OF(OPENSSL_STRING) *sigopts)
{
    EVP_PKEY_CTX *pkctx = NULL;
    int i, def_nid;

    if (ctx == NULL)
        return 0;
    /*
     * EVP_PKEY_get_default_digest_nid() returns 2 if the digest is mandatory
     * for this algorithm.
     */
    if (EVP_PKEY_get_default_digest_nid(pkey, &def_nid) == 2
            && def_nid == NID_undef) {
        /* The signing algorithm requires there to be no digest */
        md = NULL;
    }
    if (!EVP_DigestSignInit(ctx, &pkctx, md, NULL, pkey))
        return 0;
    for (i = 0; i < sk_OPENSSL_STRING_num(sigopts); i++) {
        char *sigopt = sk_OPENSSL_STRING_value(sigopts, i);
        if (pkey_ctrl_string(pkctx, sigopt) <= 0) {
            BIO_printf(bio_err, "parameter error \"%s\"\n", sigopt);
            ERR_print_errors(bio_err);
            return 0;
        }
    }
    return 1;
}

int do_X509_sign(X509 *x, EVP_PKEY *pkey, const EVP_MD *md,
                 STACK_OF(OPENSSL_STRING) *sigopts)
{
    int rv;
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();

    rv = do_sign_init(mctx, pkey, md, sigopts);
    if (rv > 0)
        rv = X509_sign_ctx(x, mctx);
    EVP_MD_CTX_free(mctx);
    return rv > 0 ? 1 : 0;
}

int do_X509_REQ_sign(X509_REQ *x, EVP_PKEY *pkey, const EVP_MD *md,
                     STACK_OF(OPENSSL_STRING) *sigopts)
{
    int rv;
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    rv = do_sign_init(mctx, pkey, md, sigopts);
    if (rv > 0)
        rv = X509_REQ_sign_ctx(x, mctx);
    EVP_MD_CTX_free(mctx);
    return rv > 0 ? 1 : 0;
}

int do_X509_CRL_sign(X509_CRL *x, EVP_PKEY *pkey, const EVP_MD *md,
                     STACK_OF(OPENSSL_STRING) *sigopts)
{
    int rv;
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    rv = do_sign_init(mctx, pkey, md, sigopts);
    if (rv > 0)
        rv = X509_CRL_sign_ctx(x, mctx);
    EVP_MD_CTX_free(mctx);
    return rv > 0 ? 1 : 0;
}
