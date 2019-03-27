/*
 * Copyright 1999-2019 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/opensslconf.h>
#if defined(OPENSSL_NO_DES)
NON_EMPTY_TRANSLATION_UNIT
#else

# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include "apps.h"
# include "progs.h"
# include <openssl/crypto.h>
# include <openssl/err.h>
# include <openssl/pem.h>
# include <openssl/pkcs12.h>

# define NOKEYS          0x1
# define NOCERTS         0x2
# define INFO            0x4
# define CLCERTS         0x8
# define CACERTS         0x10

#define PASSWD_BUF_SIZE 2048

static int get_cert_chain(X509 *cert, X509_STORE *store,
                          STACK_OF(X509) **chain);
int dump_certs_keys_p12(BIO *out, const PKCS12 *p12,
                        const char *pass, int passlen, int options,
                        char *pempass, const EVP_CIPHER *enc);
int dump_certs_pkeys_bags(BIO *out, const STACK_OF(PKCS12_SAFEBAG) *bags,
                          const char *pass, int passlen, int options,
                          char *pempass, const EVP_CIPHER *enc);
int dump_certs_pkeys_bag(BIO *out, const PKCS12_SAFEBAG *bags,
                         const char *pass, int passlen,
                         int options, char *pempass, const EVP_CIPHER *enc);
int print_attribs(BIO *out, const STACK_OF(X509_ATTRIBUTE) *attrlst,
                  const char *name);
void hex_prin(BIO *out, unsigned char *buf, int len);
static int alg_print(const X509_ALGOR *alg);
int cert_load(BIO *in, STACK_OF(X509) *sk);
static int set_pbe(int *ppbe, const char *str);

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_CIPHER, OPT_NOKEYS, OPT_KEYEX, OPT_KEYSIG, OPT_NOCERTS, OPT_CLCERTS,
    OPT_CACERTS, OPT_NOOUT, OPT_INFO, OPT_CHAIN, OPT_TWOPASS, OPT_NOMACVER,
    OPT_DESCERT, OPT_EXPORT, OPT_NOITER, OPT_MACITER, OPT_NOMACITER,
    OPT_NOMAC, OPT_LMK, OPT_NODES, OPT_MACALG, OPT_CERTPBE, OPT_KEYPBE,
    OPT_INKEY, OPT_CERTFILE, OPT_NAME, OPT_CSP, OPT_CANAME,
    OPT_IN, OPT_OUT, OPT_PASSIN, OPT_PASSOUT, OPT_PASSWORD, OPT_CAPATH,
    OPT_CAFILE, OPT_NOCAPATH, OPT_NOCAFILE, OPT_ENGINE,
    OPT_R_ENUM
} OPTION_CHOICE;

const OPTIONS pkcs12_options[] = {
    {"help", OPT_HELP, '-', "Display this summary"},
    {"nokeys", OPT_NOKEYS, '-', "Don't output private keys"},
    {"keyex", OPT_KEYEX, '-', "Set MS key exchange type"},
    {"keysig", OPT_KEYSIG, '-', "Set MS key signature type"},
    {"nocerts", OPT_NOCERTS, '-', "Don't output certificates"},
    {"clcerts", OPT_CLCERTS, '-', "Only output client certificates"},
    {"cacerts", OPT_CACERTS, '-', "Only output CA certificates"},
    {"noout", OPT_NOOUT, '-', "Don't output anything, just verify"},
    {"info", OPT_INFO, '-', "Print info about PKCS#12 structure"},
    {"chain", OPT_CHAIN, '-', "Add certificate chain"},
    {"twopass", OPT_TWOPASS, '-', "Separate MAC, encryption passwords"},
    {"nomacver", OPT_NOMACVER, '-', "Don't verify MAC"},
# ifndef OPENSSL_NO_RC2
    {"descert", OPT_DESCERT, '-',
     "Encrypt output with 3DES (default RC2-40)"},
    {"certpbe", OPT_CERTPBE, 's',
     "Certificate PBE algorithm (default RC2-40)"},
# else
    {"descert", OPT_DESCERT, '-', "Encrypt output with 3DES (the default)"},
    {"certpbe", OPT_CERTPBE, 's', "Certificate PBE algorithm (default 3DES)"},
# endif
    {"export", OPT_EXPORT, '-', "Output PKCS12 file"},
    {"noiter", OPT_NOITER, '-', "Don't use encryption iteration"},
    {"maciter", OPT_MACITER, '-', "Use MAC iteration"},
    {"nomaciter", OPT_NOMACITER, '-', "Don't use MAC iteration"},
    {"nomac", OPT_NOMAC, '-', "Don't generate MAC"},
    {"LMK", OPT_LMK, '-',
     "Add local machine keyset attribute to private key"},
    {"nodes", OPT_NODES, '-', "Don't encrypt private keys"},
    {"macalg", OPT_MACALG, 's',
     "Digest algorithm used in MAC (default SHA1)"},
    {"keypbe", OPT_KEYPBE, 's', "Private key PBE algorithm (default 3DES)"},
    OPT_R_OPTIONS,
    {"inkey", OPT_INKEY, 's', "Private key if not infile"},
    {"certfile", OPT_CERTFILE, '<', "Load certs from file"},
    {"name", OPT_NAME, 's', "Use name as friendly name"},
    {"CSP", OPT_CSP, 's', "Microsoft CSP name"},
    {"caname", OPT_CANAME, 's',
     "Use name as CA friendly name (can be repeated)"},
    {"in", OPT_IN, '<', "Input filename"},
    {"out", OPT_OUT, '>', "Output filename"},
    {"passin", OPT_PASSIN, 's', "Input file pass phrase source"},
    {"passout", OPT_PASSOUT, 's', "Output file pass phrase source"},
    {"password", OPT_PASSWORD, 's', "Set import/export password source"},
    {"CApath", OPT_CAPATH, '/', "PEM-format directory of CA's"},
    {"CAfile", OPT_CAFILE, '<', "PEM-format file of CA's"},
    {"no-CAfile", OPT_NOCAFILE, '-',
     "Do not load the default certificates file"},
    {"no-CApath", OPT_NOCAPATH, '-',
     "Do not load certificates from the default certificates directory"},
    {"", OPT_CIPHER, '-', "Any supported cipher"},
# ifndef OPENSSL_NO_ENGINE
    {"engine", OPT_ENGINE, 's', "Use engine, possibly a hardware device"},
# endif
    {NULL}
};

int pkcs12_main(int argc, char **argv)
{
    char *infile = NULL, *outfile = NULL, *keyname = NULL, *certfile = NULL;
    char *name = NULL, *csp_name = NULL;
    char pass[PASSWD_BUF_SIZE] = "", macpass[PASSWD_BUF_SIZE] = "";
    int export_cert = 0, options = 0, chain = 0, twopass = 0, keytype = 0;
    int iter = PKCS12_DEFAULT_ITER, maciter = PKCS12_DEFAULT_ITER;
# ifndef OPENSSL_NO_RC2
    int cert_pbe = NID_pbe_WithSHA1And40BitRC2_CBC;
# else
    int cert_pbe = NID_pbe_WithSHA1And3_Key_TripleDES_CBC;
# endif
    int key_pbe = NID_pbe_WithSHA1And3_Key_TripleDES_CBC;
    int ret = 1, macver = 1, add_lmk = 0, private = 0;
    int noprompt = 0;
    char *passinarg = NULL, *passoutarg = NULL, *passarg = NULL;
    char *passin = NULL, *passout = NULL, *macalg = NULL;
    char *cpass = NULL, *mpass = NULL, *badpass = NULL;
    const char *CApath = NULL, *CAfile = NULL, *prog;
    int noCApath = 0, noCAfile = 0;
    ENGINE *e = NULL;
    BIO *in = NULL, *out = NULL;
    PKCS12 *p12 = NULL;
    STACK_OF(OPENSSL_STRING) *canames = NULL;
    const EVP_CIPHER *enc = EVP_des_ede3_cbc();
    OPTION_CHOICE o;

    prog = opt_init(argc, argv, pkcs12_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
 opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(pkcs12_options);
            ret = 0;
            goto end;
        case OPT_NOKEYS:
            options |= NOKEYS;
            break;
        case OPT_KEYEX:
            keytype = KEY_EX;
            break;
        case OPT_KEYSIG:
            keytype = KEY_SIG;
            break;
        case OPT_NOCERTS:
            options |= NOCERTS;
            break;
        case OPT_CLCERTS:
            options |= CLCERTS;
            break;
        case OPT_CACERTS:
            options |= CACERTS;
            break;
        case OPT_NOOUT:
            options |= (NOKEYS | NOCERTS);
            break;
        case OPT_INFO:
            options |= INFO;
            break;
        case OPT_CHAIN:
            chain = 1;
            break;
        case OPT_TWOPASS:
            twopass = 1;
            break;
        case OPT_NOMACVER:
            macver = 0;
            break;
        case OPT_DESCERT:
            cert_pbe = NID_pbe_WithSHA1And3_Key_TripleDES_CBC;
            break;
        case OPT_EXPORT:
            export_cert = 1;
            break;
        case OPT_CIPHER:
            if (!opt_cipher(opt_unknown(), &enc))
                goto opthelp;
            break;
        case OPT_NOITER:
            iter = 1;
            break;
        case OPT_MACITER:
            maciter = PKCS12_DEFAULT_ITER;
            break;
        case OPT_NOMACITER:
            maciter = 1;
            break;
        case OPT_NOMAC:
            maciter = -1;
            break;
        case OPT_MACALG:
            macalg = opt_arg();
            break;
        case OPT_NODES:
            enc = NULL;
            break;
        case OPT_CERTPBE:
            if (!set_pbe(&cert_pbe, opt_arg()))
                goto opthelp;
            break;
        case OPT_KEYPBE:
            if (!set_pbe(&key_pbe, opt_arg()))
                goto opthelp;
            break;
        case OPT_R_CASES:
            if (!opt_rand(o))
                goto end;
            break;
        case OPT_INKEY:
            keyname = opt_arg();
            break;
        case OPT_CERTFILE:
            certfile = opt_arg();
            break;
        case OPT_NAME:
            name = opt_arg();
            break;
        case OPT_LMK:
            add_lmk = 1;
            break;
        case OPT_CSP:
            csp_name = opt_arg();
            break;
        case OPT_CANAME:
            if (canames == NULL
                && (canames = sk_OPENSSL_STRING_new_null()) == NULL)
                goto end;
            sk_OPENSSL_STRING_push(canames, opt_arg());
            break;
        case OPT_IN:
            infile = opt_arg();
            break;
        case OPT_OUT:
            outfile = opt_arg();
            break;
        case OPT_PASSIN:
            passinarg = opt_arg();
            break;
        case OPT_PASSOUT:
            passoutarg = opt_arg();
            break;
        case OPT_PASSWORD:
            passarg = opt_arg();
            break;
        case OPT_CAPATH:
            CApath = opt_arg();
            break;
        case OPT_CAFILE:
            CAfile = opt_arg();
            break;
        case OPT_NOCAPATH:
            noCApath = 1;
            break;
        case OPT_NOCAFILE:
            noCAfile = 1;
            break;
        case OPT_ENGINE:
            e = setup_engine(opt_arg(), 0);
            break;
        }
    }
    argc = opt_num_rest();
    if (argc != 0)
        goto opthelp;

    private = 1;

    if (passarg != NULL) {
        if (export_cert)
            passoutarg = passarg;
        else
            passinarg = passarg;
    }

    if (!app_passwd(passinarg, passoutarg, &passin, &passout)) {
        BIO_printf(bio_err, "Error getting passwords\n");
        goto end;
    }

    if (cpass == NULL) {
        if (export_cert)
            cpass = passout;
        else
            cpass = passin;
    }

    if (cpass != NULL) {
        mpass = cpass;
        noprompt = 1;
        if (twopass) {
            if (export_cert)
                BIO_printf(bio_err, "Option -twopass cannot be used with -passout or -password\n");
            else
                BIO_printf(bio_err, "Option -twopass cannot be used with -passin or -password\n");
            goto end;
        }
    } else {
        cpass = pass;
        mpass = macpass;
    }

    if (twopass) {
        /* To avoid bit rot */
        if (1) {
#ifndef OPENSSL_NO_UI_CONSOLE
            if (EVP_read_pw_string(
                macpass, sizeof(macpass), "Enter MAC Password:", export_cert)) {
                BIO_printf(bio_err, "Can't read Password\n");
                goto end;
            }
        } else {
#endif
            BIO_printf(bio_err, "Unsupported option -twopass\n");
            goto end;
        }
    }

    if (export_cert) {
        EVP_PKEY *key = NULL;
        X509 *ucert = NULL, *x = NULL;
        STACK_OF(X509) *certs = NULL;
        const EVP_MD *macmd = NULL;
        unsigned char *catmp = NULL;
        int i;

        if ((options & (NOCERTS | NOKEYS)) == (NOCERTS | NOKEYS)) {
            BIO_printf(bio_err, "Nothing to do!\n");
            goto export_end;
        }

        if (options & NOCERTS)
            chain = 0;

        if (!(options & NOKEYS)) {
            key = load_key(keyname ? keyname : infile,
                           FORMAT_PEM, 1, passin, e, "private key");
            if (key == NULL)
                goto export_end;
        }

        /* Load in all certs in input file */
        if (!(options & NOCERTS)) {
            if (!load_certs(infile, &certs, FORMAT_PEM, NULL,
                            "certificates"))
                goto export_end;

            if (key != NULL) {
                /* Look for matching private key */
                for (i = 0; i < sk_X509_num(certs); i++) {
                    x = sk_X509_value(certs, i);
                    if (X509_check_private_key(x, key)) {
                        ucert = x;
                        /* Zero keyid and alias */
                        X509_keyid_set1(ucert, NULL, 0);
                        X509_alias_set1(ucert, NULL, 0);
                        /* Remove from list */
                        (void)sk_X509_delete(certs, i);
                        break;
                    }
                }
                if (ucert == NULL) {
                    BIO_printf(bio_err,
                               "No certificate matches private key\n");
                    goto export_end;
                }
            }

        }

        /* Add any more certificates asked for */
        if (certfile != NULL) {
            if (!load_certs(certfile, &certs, FORMAT_PEM, NULL,
                            "certificates from certfile"))
                goto export_end;
        }

        /* If chaining get chain from user cert */
        if (chain) {
            int vret;
            STACK_OF(X509) *chain2;
            X509_STORE *store;
            if ((store = setup_verify(CAfile, CApath, noCAfile, noCApath))
                    == NULL)
                goto export_end;

            vret = get_cert_chain(ucert, store, &chain2);
            X509_STORE_free(store);

            if (vret == X509_V_OK) {
                /* Exclude verified certificate */
                for (i = 1; i < sk_X509_num(chain2); i++)
                    sk_X509_push(certs, sk_X509_value(chain2, i));
                /* Free first certificate */
                X509_free(sk_X509_value(chain2, 0));
                sk_X509_free(chain2);
            } else {
                if (vret != X509_V_ERR_UNSPECIFIED)
                    BIO_printf(bio_err, "Error %s getting chain.\n",
                               X509_verify_cert_error_string(vret));
                else
                    ERR_print_errors(bio_err);
                goto export_end;
            }
        }

        /* Add any CA names */

        for (i = 0; i < sk_OPENSSL_STRING_num(canames); i++) {
            catmp = (unsigned char *)sk_OPENSSL_STRING_value(canames, i);
            X509_alias_set1(sk_X509_value(certs, i), catmp, -1);
        }

        if (csp_name != NULL && key != NULL)
            EVP_PKEY_add1_attr_by_NID(key, NID_ms_csp_name,
                                      MBSTRING_ASC, (unsigned char *)csp_name,
                                      -1);

        if (add_lmk && key != NULL)
            EVP_PKEY_add1_attr_by_NID(key, NID_LocalKeySet, 0, NULL, -1);

        if (!noprompt) {
            /* To avoid bit rot */
            if (1) {
#ifndef OPENSSL_NO_UI_CONSOLE
                if (EVP_read_pw_string(pass, sizeof(pass),
                                       "Enter Export Password:", 1)) {
                    BIO_printf(bio_err, "Can't read Password\n");
                    goto export_end;
                }
            } else {
#endif
                BIO_printf(bio_err, "Password required\n");
                goto export_end;
            }
        }

        if (!twopass)
            OPENSSL_strlcpy(macpass, pass, sizeof(macpass));

        p12 = PKCS12_create(cpass, name, key, ucert, certs,
                            key_pbe, cert_pbe, iter, -1, keytype);

        if (!p12) {
            ERR_print_errors(bio_err);
            goto export_end;
        }

        if (macalg) {
            if (!opt_md(macalg, &macmd))
                goto opthelp;
        }

        if (maciter != -1)
            PKCS12_set_mac(p12, mpass, -1, NULL, 0, maciter, macmd);

        assert(private);

        out = bio_open_owner(outfile, FORMAT_PKCS12, private);
        if (out == NULL)
            goto end;

        i2d_PKCS12_bio(out, p12);

        ret = 0;

 export_end:

        EVP_PKEY_free(key);
        sk_X509_pop_free(certs, X509_free);
        X509_free(ucert);

        goto end;

    }

    in = bio_open_default(infile, 'r', FORMAT_PKCS12);
    if (in == NULL)
        goto end;
    out = bio_open_owner(outfile, FORMAT_PEM, private);
    if (out == NULL)
        goto end;

    if ((p12 = d2i_PKCS12_bio(in, NULL)) == NULL) {
        ERR_print_errors(bio_err);
        goto end;
    }

    if (!noprompt) {
        if (1) {
#ifndef OPENSSL_NO_UI_CONSOLE
            if (EVP_read_pw_string(pass, sizeof(pass), "Enter Import Password:",
                                   0)) {
                BIO_printf(bio_err, "Can't read Password\n");
                goto end;
            }
        } else {
#endif
            BIO_printf(bio_err, "Password required\n");
            goto end;
        }
    }

    if (!twopass)
        OPENSSL_strlcpy(macpass, pass, sizeof(macpass));

    if ((options & INFO) && PKCS12_mac_present(p12)) {
        const ASN1_INTEGER *tmaciter;
        const X509_ALGOR *macalgid;
        const ASN1_OBJECT *macobj;
        const ASN1_OCTET_STRING *tmac;
        const ASN1_OCTET_STRING *tsalt;

        PKCS12_get0_mac(&tmac, &macalgid, &tsalt, &tmaciter, p12);
        /* current hash algorithms do not use parameters so extract just name,
           in future alg_print() may be needed */
        X509_ALGOR_get0(&macobj, NULL, NULL, macalgid);
        BIO_puts(bio_err, "MAC: ");
        i2a_ASN1_OBJECT(bio_err, macobj);
        BIO_printf(bio_err, ", Iteration %ld\n",
                   tmaciter != NULL ? ASN1_INTEGER_get(tmaciter) : 1L);
        BIO_printf(bio_err, "MAC length: %ld, salt length: %ld\n",
                   tmac != NULL ? ASN1_STRING_length(tmac) : 0L,
                   tsalt != NULL ? ASN1_STRING_length(tsalt) : 0L);
    }
    if (macver) {
        /* If we enter empty password try no password first */
        if (!mpass[0] && PKCS12_verify_mac(p12, NULL, 0)) {
            /* If mac and crypto pass the same set it to NULL too */
            if (!twopass)
                cpass = NULL;
        } else if (!PKCS12_verify_mac(p12, mpass, -1)) {
            /*
             * May be UTF8 from previous version of OpenSSL:
             * convert to a UTF8 form which will translate
             * to the same Unicode password.
             */
            unsigned char *utmp;
            int utmplen;
            utmp = OPENSSL_asc2uni(mpass, -1, NULL, &utmplen);
            if (utmp == NULL)
                goto end;
            badpass = OPENSSL_uni2utf8(utmp, utmplen);
            OPENSSL_free(utmp);
            if (!PKCS12_verify_mac(p12, badpass, -1)) {
                BIO_printf(bio_err, "Mac verify error: invalid password?\n");
                ERR_print_errors(bio_err);
                goto end;
            } else {
                BIO_printf(bio_err, "Warning: using broken algorithm\n");
                if (!twopass)
                    cpass = badpass;
            }
        }
    }

    assert(private);
    if (!dump_certs_keys_p12(out, p12, cpass, -1, options, passout, enc)) {
        BIO_printf(bio_err, "Error outputting keys and certificates\n");
        ERR_print_errors(bio_err);
        goto end;
    }
    ret = 0;
 end:
    PKCS12_free(p12);
    release_engine(e);
    BIO_free(in);
    BIO_free_all(out);
    sk_OPENSSL_STRING_free(canames);
    OPENSSL_free(badpass);
    OPENSSL_free(passin);
    OPENSSL_free(passout);
    return ret;
}

int dump_certs_keys_p12(BIO *out, const PKCS12 *p12, const char *pass,
                        int passlen, int options, char *pempass,
                        const EVP_CIPHER *enc)
{
    STACK_OF(PKCS7) *asafes = NULL;
    STACK_OF(PKCS12_SAFEBAG) *bags;
    int i, bagnid;
    int ret = 0;
    PKCS7 *p7;

    if ((asafes = PKCS12_unpack_authsafes(p12)) == NULL)
        return 0;
    for (i = 0; i < sk_PKCS7_num(asafes); i++) {
        p7 = sk_PKCS7_value(asafes, i);
        bagnid = OBJ_obj2nid(p7->type);
        if (bagnid == NID_pkcs7_data) {
            bags = PKCS12_unpack_p7data(p7);
            if (options & INFO)
                BIO_printf(bio_err, "PKCS7 Data\n");
        } else if (bagnid == NID_pkcs7_encrypted) {
            if (options & INFO) {
                BIO_printf(bio_err, "PKCS7 Encrypted data: ");
                alg_print(p7->d.encrypted->enc_data->algorithm);
            }
            bags = PKCS12_unpack_p7encdata(p7, pass, passlen);
        } else {
            continue;
        }
        if (!bags)
            goto err;
        if (!dump_certs_pkeys_bags(out, bags, pass, passlen,
                                   options, pempass, enc)) {
            sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
            goto err;
        }
        sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
        bags = NULL;
    }
    ret = 1;

 err:
    sk_PKCS7_pop_free(asafes, PKCS7_free);
    return ret;
}

int dump_certs_pkeys_bags(BIO *out, const STACK_OF(PKCS12_SAFEBAG) *bags,
                          const char *pass, int passlen, int options,
                          char *pempass, const EVP_CIPHER *enc)
{
    int i;
    for (i = 0; i < sk_PKCS12_SAFEBAG_num(bags); i++) {
        if (!dump_certs_pkeys_bag(out,
                                  sk_PKCS12_SAFEBAG_value(bags, i),
                                  pass, passlen, options, pempass, enc))
            return 0;
    }
    return 1;
}

int dump_certs_pkeys_bag(BIO *out, const PKCS12_SAFEBAG *bag,
                         const char *pass, int passlen, int options,
                         char *pempass, const EVP_CIPHER *enc)
{
    EVP_PKEY *pkey;
    PKCS8_PRIV_KEY_INFO *p8;
    const PKCS8_PRIV_KEY_INFO *p8c;
    X509 *x509;
    const STACK_OF(X509_ATTRIBUTE) *attrs;
    int ret = 0;

    attrs = PKCS12_SAFEBAG_get0_attrs(bag);

    switch (PKCS12_SAFEBAG_get_nid(bag)) {
    case NID_keyBag:
        if (options & INFO)
            BIO_printf(bio_err, "Key bag\n");
        if (options & NOKEYS)
            return 1;
        print_attribs(out, attrs, "Bag Attributes");
        p8c = PKCS12_SAFEBAG_get0_p8inf(bag);
        if ((pkey = EVP_PKCS82PKEY(p8c)) == NULL)
            return 0;
        print_attribs(out, PKCS8_pkey_get0_attrs(p8c), "Key Attributes");
        ret = PEM_write_bio_PrivateKey(out, pkey, enc, NULL, 0, NULL, pempass);
        EVP_PKEY_free(pkey);
        break;

    case NID_pkcs8ShroudedKeyBag:
        if (options & INFO) {
            const X509_SIG *tp8;
            const X509_ALGOR *tp8alg;

            BIO_printf(bio_err, "Shrouded Keybag: ");
            tp8 = PKCS12_SAFEBAG_get0_pkcs8(bag);
            X509_SIG_get0(tp8, &tp8alg, NULL);
            alg_print(tp8alg);
        }
        if (options & NOKEYS)
            return 1;
        print_attribs(out, attrs, "Bag Attributes");
        if ((p8 = PKCS12_decrypt_skey(bag, pass, passlen)) == NULL)
            return 0;
        if ((pkey = EVP_PKCS82PKEY(p8)) == NULL) {
            PKCS8_PRIV_KEY_INFO_free(p8);
            return 0;
        }
        print_attribs(out, PKCS8_pkey_get0_attrs(p8), "Key Attributes");
        PKCS8_PRIV_KEY_INFO_free(p8);
        ret = PEM_write_bio_PrivateKey(out, pkey, enc, NULL, 0, NULL, pempass);
        EVP_PKEY_free(pkey);
        break;

    case NID_certBag:
        if (options & INFO)
            BIO_printf(bio_err, "Certificate bag\n");
        if (options & NOCERTS)
            return 1;
        if (PKCS12_SAFEBAG_get0_attr(bag, NID_localKeyID)) {
            if (options & CACERTS)
                return 1;
        } else if (options & CLCERTS)
            return 1;
        print_attribs(out, attrs, "Bag Attributes");
        if (PKCS12_SAFEBAG_get_bag_nid(bag) != NID_x509Certificate)
            return 1;
        if ((x509 = PKCS12_SAFEBAG_get1_cert(bag)) == NULL)
            return 0;
        dump_cert_text(out, x509);
        ret = PEM_write_bio_X509(out, x509);
        X509_free(x509);
        break;

    case NID_safeContentsBag:
        if (options & INFO)
            BIO_printf(bio_err, "Safe Contents bag\n");
        print_attribs(out, attrs, "Bag Attributes");
        return dump_certs_pkeys_bags(out, PKCS12_SAFEBAG_get0_safes(bag),
                                     pass, passlen, options, pempass, enc);

    default:
        BIO_printf(bio_err, "Warning unsupported bag type: ");
        i2a_ASN1_OBJECT(bio_err, PKCS12_SAFEBAG_get0_type(bag));
        BIO_printf(bio_err, "\n");
        return 1;
    }
    return ret;
}

/* Given a single certificate return a verified chain or NULL if error */

static int get_cert_chain(X509 *cert, X509_STORE *store,
                          STACK_OF(X509) **chain)
{
    X509_STORE_CTX *store_ctx = NULL;
    STACK_OF(X509) *chn = NULL;
    int i = 0;

    store_ctx = X509_STORE_CTX_new();
    if (store_ctx == NULL) {
        i =  X509_V_ERR_UNSPECIFIED;
        goto end;
    }
    if (!X509_STORE_CTX_init(store_ctx, store, cert, NULL)) {
        i =  X509_V_ERR_UNSPECIFIED;
        goto end;
    }


    if (X509_verify_cert(store_ctx) > 0)
        chn = X509_STORE_CTX_get1_chain(store_ctx);
    else if ((i = X509_STORE_CTX_get_error(store_ctx)) == 0)
        i = X509_V_ERR_UNSPECIFIED;

end:
    X509_STORE_CTX_free(store_ctx);
    *chain = chn;
    return i;
}

static int alg_print(const X509_ALGOR *alg)
{
    int pbenid, aparamtype;
    const ASN1_OBJECT *aoid;
    const void *aparam;
    PBEPARAM *pbe = NULL;

    X509_ALGOR_get0(&aoid, &aparamtype, &aparam, alg);

    pbenid = OBJ_obj2nid(aoid);

    BIO_printf(bio_err, "%s", OBJ_nid2ln(pbenid));

    /*
     * If PBE algorithm is PBES2 decode algorithm parameters
     * for additional details.
     */
    if (pbenid == NID_pbes2) {
        PBE2PARAM *pbe2 = NULL;
        int encnid;
        if (aparamtype == V_ASN1_SEQUENCE)
            pbe2 = ASN1_item_unpack(aparam, ASN1_ITEM_rptr(PBE2PARAM));
        if (pbe2 == NULL) {
            BIO_puts(bio_err, ", <unsupported parameters>");
            goto done;
        }
        X509_ALGOR_get0(&aoid, &aparamtype, &aparam, pbe2->keyfunc);
        pbenid = OBJ_obj2nid(aoid);
        X509_ALGOR_get0(&aoid, NULL, NULL, pbe2->encryption);
        encnid = OBJ_obj2nid(aoid);
        BIO_printf(bio_err, ", %s, %s", OBJ_nid2ln(pbenid),
                   OBJ_nid2sn(encnid));
        /* If KDF is PBKDF2 decode parameters */
        if (pbenid == NID_id_pbkdf2) {
            PBKDF2PARAM *kdf = NULL;
            int prfnid;
            if (aparamtype == V_ASN1_SEQUENCE)
                kdf = ASN1_item_unpack(aparam, ASN1_ITEM_rptr(PBKDF2PARAM));
            if (kdf == NULL) {
                BIO_puts(bio_err, ", <unsupported parameters>");
                goto done;
            }

            if (kdf->prf == NULL) {
                prfnid = NID_hmacWithSHA1;
            } else {
                X509_ALGOR_get0(&aoid, NULL, NULL, kdf->prf);
                prfnid = OBJ_obj2nid(aoid);
            }
            BIO_printf(bio_err, ", Iteration %ld, PRF %s",
                       ASN1_INTEGER_get(kdf->iter), OBJ_nid2sn(prfnid));
            PBKDF2PARAM_free(kdf);
#ifndef OPENSSL_NO_SCRYPT
        } else if (pbenid == NID_id_scrypt) {
            SCRYPT_PARAMS *kdf = NULL;

            if (aparamtype == V_ASN1_SEQUENCE)
                kdf = ASN1_item_unpack(aparam, ASN1_ITEM_rptr(SCRYPT_PARAMS));
            if (kdf == NULL) {
                BIO_puts(bio_err, ", <unsupported parameters>");
                goto done;
            }
            BIO_printf(bio_err, ", Salt length: %d, Cost(N): %ld, "
                       "Block size(r): %ld, Paralelizm(p): %ld",
                       ASN1_STRING_length(kdf->salt),
                       ASN1_INTEGER_get(kdf->costParameter),
                       ASN1_INTEGER_get(kdf->blockSize),
                       ASN1_INTEGER_get(kdf->parallelizationParameter));
            SCRYPT_PARAMS_free(kdf);
#endif
        }
        PBE2PARAM_free(pbe2);
    } else {
        if (aparamtype == V_ASN1_SEQUENCE)
            pbe = ASN1_item_unpack(aparam, ASN1_ITEM_rptr(PBEPARAM));
        if (pbe == NULL) {
            BIO_puts(bio_err, ", <unsupported parameters>");
            goto done;
        }
        BIO_printf(bio_err, ", Iteration %ld", ASN1_INTEGER_get(pbe->iter));
        PBEPARAM_free(pbe);
    }
 done:
    BIO_puts(bio_err, "\n");
    return 1;
}

/* Load all certificates from a given file */

int cert_load(BIO *in, STACK_OF(X509) *sk)
{
    int ret;
    X509 *cert;
    ret = 0;
    while ((cert = PEM_read_bio_X509(in, NULL, NULL, NULL))) {
        ret = 1;
        sk_X509_push(sk, cert);
    }
    if (ret)
        ERR_clear_error();
    return ret;
}

/* Generalised attribute print: handle PKCS#8 and bag attributes */

int print_attribs(BIO *out, const STACK_OF(X509_ATTRIBUTE) *attrlst,
                  const char *name)
{
    X509_ATTRIBUTE *attr;
    ASN1_TYPE *av;
    char *value;
    int i, attr_nid;
    if (!attrlst) {
        BIO_printf(out, "%s: <No Attributes>\n", name);
        return 1;
    }
    if (!sk_X509_ATTRIBUTE_num(attrlst)) {
        BIO_printf(out, "%s: <Empty Attributes>\n", name);
        return 1;
    }
    BIO_printf(out, "%s\n", name);
    for (i = 0; i < sk_X509_ATTRIBUTE_num(attrlst); i++) {
        ASN1_OBJECT *attr_obj;
        attr = sk_X509_ATTRIBUTE_value(attrlst, i);
        attr_obj = X509_ATTRIBUTE_get0_object(attr);
        attr_nid = OBJ_obj2nid(attr_obj);
        BIO_printf(out, "    ");
        if (attr_nid == NID_undef) {
            i2a_ASN1_OBJECT(out, attr_obj);
            BIO_printf(out, ": ");
        } else {
            BIO_printf(out, "%s: ", OBJ_nid2ln(attr_nid));
        }

        if (X509_ATTRIBUTE_count(attr)) {
            av = X509_ATTRIBUTE_get0_type(attr, 0);
            switch (av->type) {
            case V_ASN1_BMPSTRING:
                value = OPENSSL_uni2asc(av->value.bmpstring->data,
                                        av->value.bmpstring->length);
                BIO_printf(out, "%s\n", value);
                OPENSSL_free(value);
                break;

            case V_ASN1_OCTET_STRING:
                hex_prin(out, av->value.octet_string->data,
                         av->value.octet_string->length);
                BIO_printf(out, "\n");
                break;

            case V_ASN1_BIT_STRING:
                hex_prin(out, av->value.bit_string->data,
                         av->value.bit_string->length);
                BIO_printf(out, "\n");
                break;

            default:
                BIO_printf(out, "<Unsupported tag %d>\n", av->type);
                break;
            }
        } else {
            BIO_printf(out, "<No Values>\n");
        }
    }
    return 1;
}

void hex_prin(BIO *out, unsigned char *buf, int len)
{
    int i;
    for (i = 0; i < len; i++)
        BIO_printf(out, "%02X ", buf[i]);
}

static int set_pbe(int *ppbe, const char *str)
{
    if (!str)
        return 0;
    if (strcmp(str, "NONE") == 0) {
        *ppbe = -1;
        return 1;
    }
    *ppbe = OBJ_txt2nid(str);
    if (*ppbe == NID_undef) {
        BIO_printf(bio_err, "Unknown PBE algorithm %s\n", str);
        return 0;
    }
    return 1;
}

#endif
