/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/opensslconf.h>

#include "apps.h"
#include "progs.h"
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/store.h>
#include <openssl/x509v3.h>      /* s2i_ASN1_INTEGER */

static int process(const char *uri, const UI_METHOD *uimeth, PW_CB_DATA *uidata,
                   int expected, int criterion, OSSL_STORE_SEARCH *search,
                   int text, int noout, int recursive, int indent, BIO *out,
                   const char *prog);

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP, OPT_ENGINE, OPT_OUT, OPT_PASSIN,
    OPT_NOOUT, OPT_TEXT, OPT_RECURSIVE,
    OPT_SEARCHFOR_CERTS, OPT_SEARCHFOR_KEYS, OPT_SEARCHFOR_CRLS,
    OPT_CRITERION_SUBJECT, OPT_CRITERION_ISSUER, OPT_CRITERION_SERIAL,
    OPT_CRITERION_FINGERPRINT, OPT_CRITERION_ALIAS,
    OPT_MD
} OPTION_CHOICE;

const OPTIONS storeutl_options[] = {
    {OPT_HELP_STR, 1, '-', "Usage: %s [options] uri\nValid options are:\n"},
    {"help", OPT_HELP, '-', "Display this summary"},
    {"out", OPT_OUT, '>', "Output file - default stdout"},
    {"passin", OPT_PASSIN, 's', "Input file pass phrase source"},
    {"text", OPT_TEXT, '-', "Print a text form of the objects"},
    {"noout", OPT_NOOUT, '-', "No PEM output, just status"},
    {"certs", OPT_SEARCHFOR_CERTS, '-', "Search for certificates only"},
    {"keys", OPT_SEARCHFOR_KEYS, '-', "Search for keys only"},
    {"crls", OPT_SEARCHFOR_CRLS, '-', "Search for CRLs only"},
    {"subject", OPT_CRITERION_SUBJECT, 's', "Search by subject"},
    {"issuer", OPT_CRITERION_ISSUER, 's', "Search by issuer and serial, issuer name"},
    {"serial", OPT_CRITERION_SERIAL, 's', "Search by issuer and serial, serial number"},
    {"fingerprint", OPT_CRITERION_FINGERPRINT, 's', "Search by public key fingerprint, given in hex"},
    {"alias", OPT_CRITERION_ALIAS, 's', "Search by alias"},
    {"", OPT_MD, '-', "Any supported digest"},
#ifndef OPENSSL_NO_ENGINE
    {"engine", OPT_ENGINE, 's', "Use engine, possibly a hardware device"},
#endif
    {"r", OPT_RECURSIVE, '-', "Recurse through names"},
    {NULL}
};

int storeutl_main(int argc, char *argv[])
{
    int ret = 1, noout = 0, text = 0, recursive = 0;
    char *outfile = NULL, *passin = NULL, *passinarg = NULL;
    BIO *out = NULL;
    ENGINE *e = NULL;
    OPTION_CHOICE o;
    char *prog = opt_init(argc, argv, storeutl_options);
    PW_CB_DATA pw_cb_data;
    int expected = 0;
    int criterion = 0;
    X509_NAME *subject = NULL, *issuer = NULL;
    ASN1_INTEGER *serial = NULL;
    unsigned char *fingerprint = NULL;
    size_t fingerprintlen = 0;
    char *alias = NULL;
    OSSL_STORE_SEARCH *search = NULL;
    const EVP_MD *digest = NULL;

    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
 opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(storeutl_options);
            ret = 0;
            goto end;
        case OPT_OUT:
            outfile = opt_arg();
            break;
        case OPT_PASSIN:
            passinarg = opt_arg();
            break;
        case OPT_NOOUT:
            noout = 1;
            break;
        case OPT_TEXT:
            text = 1;
            break;
        case OPT_RECURSIVE:
            recursive = 1;
            break;
        case OPT_SEARCHFOR_CERTS:
        case OPT_SEARCHFOR_KEYS:
        case OPT_SEARCHFOR_CRLS:
            if (expected != 0) {
                BIO_printf(bio_err, "%s: only one search type can be given.\n",
                           prog);
                goto end;
            }
            {
                static const struct {
                    enum OPTION_choice choice;
                    int type;
                } map[] = {
                    {OPT_SEARCHFOR_CERTS, OSSL_STORE_INFO_CERT},
                    {OPT_SEARCHFOR_KEYS, OSSL_STORE_INFO_PKEY},
                    {OPT_SEARCHFOR_CRLS, OSSL_STORE_INFO_CRL},
                };
                size_t i;

                for (i = 0; i < OSSL_NELEM(map); i++) {
                    if (o == map[i].choice) {
                        expected = map[i].type;
                        break;
                    }
                }
                /*
                 * If expected wasn't set at this point, it means the map
                 * isn't syncronised with the possible options leading here.
                 */
                OPENSSL_assert(expected != 0);
            }
            break;
        case OPT_CRITERION_SUBJECT:
            if (criterion != 0) {
                BIO_printf(bio_err, "%s: criterion already given.\n",
                           prog);
                goto end;
            }
            criterion = OSSL_STORE_SEARCH_BY_NAME;
            if (subject != NULL) {
                BIO_printf(bio_err, "%s: subject already given.\n",
                           prog);
                goto end;
            }
            if ((subject = parse_name(opt_arg(), MBSTRING_UTF8, 1)) == NULL) {
                BIO_printf(bio_err, "%s: can't parse subject argument.\n",
                           prog);
                goto end;
            }
            break;
        case OPT_CRITERION_ISSUER:
            if (criterion != 0
                || (criterion == OSSL_STORE_SEARCH_BY_ISSUER_SERIAL
                    && issuer != NULL)) {
                BIO_printf(bio_err, "%s: criterion already given.\n",
                           prog);
                goto end;
            }
            criterion = OSSL_STORE_SEARCH_BY_ISSUER_SERIAL;
            if (issuer != NULL) {
                BIO_printf(bio_err, "%s: issuer already given.\n",
                           prog);
                goto end;
            }
            if ((issuer = parse_name(opt_arg(), MBSTRING_UTF8, 1)) == NULL) {
                BIO_printf(bio_err, "%s: can't parse issuer argument.\n",
                           prog);
                goto end;
            }
            break;
        case OPT_CRITERION_SERIAL:
            if (criterion != 0
                || (criterion == OSSL_STORE_SEARCH_BY_ISSUER_SERIAL
                    && serial != NULL)) {
                BIO_printf(bio_err, "%s: criterion already given.\n",
                           prog);
                goto end;
            }
            criterion = OSSL_STORE_SEARCH_BY_ISSUER_SERIAL;
            if (serial != NULL) {
                BIO_printf(bio_err, "%s: serial number already given.\n",
                           prog);
                goto end;
            }
            if ((serial = s2i_ASN1_INTEGER(NULL, opt_arg())) == NULL) {
                BIO_printf(bio_err, "%s: can't parse serial number argument.\n",
                           prog);
                goto end;
            }
            break;
        case OPT_CRITERION_FINGERPRINT:
            if (criterion != 0
                || (criterion == OSSL_STORE_SEARCH_BY_KEY_FINGERPRINT
                    && fingerprint != NULL)) {
                BIO_printf(bio_err, "%s: criterion already given.\n",
                           prog);
                goto end;
            }
            criterion = OSSL_STORE_SEARCH_BY_KEY_FINGERPRINT;
            if (fingerprint != NULL) {
                BIO_printf(bio_err, "%s: fingerprint already given.\n",
                           prog);
                goto end;
            }
            {
                long tmplen = 0;

                if ((fingerprint = OPENSSL_hexstr2buf(opt_arg(), &tmplen))
                    == NULL) {
                    BIO_printf(bio_err,
                               "%s: can't parse fingerprint argument.\n",
                               prog);
                    goto end;
                }
                fingerprintlen = (size_t)tmplen;
            }
            break;
        case OPT_CRITERION_ALIAS:
            if (criterion != 0) {
                BIO_printf(bio_err, "%s: criterion already given.\n",
                           prog);
                goto end;
            }
            criterion = OSSL_STORE_SEARCH_BY_ALIAS;
            if (alias != NULL) {
                BIO_printf(bio_err, "%s: alias already given.\n",
                           prog);
                goto end;
            }
            if ((alias = OPENSSL_strdup(opt_arg())) == NULL) {
                BIO_printf(bio_err, "%s: can't parse alias argument.\n",
                           prog);
                goto end;
            }
            break;
        case OPT_ENGINE:
            e = setup_engine(opt_arg(), 0);
            break;
        case OPT_MD:
            if (!opt_md(opt_unknown(), &digest))
                goto opthelp;
        }
    }
    argc = opt_num_rest();
    argv = opt_rest();

    if (argc == 0) {
        BIO_printf(bio_err, "%s: No URI given, nothing to do...\n", prog);
        goto opthelp;
    }
    if (argc > 1) {
        BIO_printf(bio_err, "%s: Unknown extra parameters after URI\n", prog);
        goto opthelp;
    }

    if (criterion != 0) {
        switch (criterion) {
        case OSSL_STORE_SEARCH_BY_NAME:
            if ((search = OSSL_STORE_SEARCH_by_name(subject)) == NULL) {
                ERR_print_errors(bio_err);
                goto end;
            }
            break;
        case OSSL_STORE_SEARCH_BY_ISSUER_SERIAL:
            if (issuer == NULL || serial == NULL) {
                BIO_printf(bio_err,
                           "%s: both -issuer and -serial must be given.\n",
                           prog);
                goto end;
            }
            if ((search = OSSL_STORE_SEARCH_by_issuer_serial(issuer, serial))
                == NULL) {
                ERR_print_errors(bio_err);
                goto end;
            }
            break;
        case OSSL_STORE_SEARCH_BY_KEY_FINGERPRINT:
            if ((search = OSSL_STORE_SEARCH_by_key_fingerprint(digest,
                                                               fingerprint,
                                                               fingerprintlen))
                == NULL) {
                ERR_print_errors(bio_err);
                goto end;
            }
            break;
        case OSSL_STORE_SEARCH_BY_ALIAS:
            if ((search = OSSL_STORE_SEARCH_by_alias(alias)) == NULL) {
                ERR_print_errors(bio_err);
                goto end;
            }
            break;
        }
    }

    if (!app_passwd(passinarg, NULL, &passin, NULL)) {
        BIO_printf(bio_err, "Error getting passwords\n");
        goto end;
    }
    pw_cb_data.password = passin;
    pw_cb_data.prompt_info = argv[0];

    out = bio_open_default(outfile, 'w', FORMAT_TEXT);
    if (out == NULL)
        goto end;

    ret = process(argv[0], get_ui_method(), &pw_cb_data,
                  expected, criterion, search,
                  text, noout, recursive, 0, out, prog);

 end:
    OPENSSL_free(fingerprint);
    OPENSSL_free(alias);
    ASN1_INTEGER_free(serial);
    X509_NAME_free(subject);
    X509_NAME_free(issuer);
    OSSL_STORE_SEARCH_free(search);
    BIO_free_all(out);
    OPENSSL_free(passin);
    release_engine(e);
    return ret;
}

static int indent_printf(int indent, BIO *bio, const char *format, ...)
{
    va_list args;
    int ret;

    va_start(args, format);

    ret = BIO_printf(bio, "%*s", indent, "") + BIO_vprintf(bio, format, args);

    va_end(args);
    return ret;
}

static int process(const char *uri, const UI_METHOD *uimeth, PW_CB_DATA *uidata,
                   int expected, int criterion, OSSL_STORE_SEARCH *search,
                   int text, int noout, int recursive, int indent, BIO *out,
                   const char *prog)
{
    OSSL_STORE_CTX *store_ctx = NULL;
    int ret = 1, items = 0;

    if ((store_ctx = OSSL_STORE_open(uri, uimeth, uidata, NULL, NULL))
        == NULL) {
        BIO_printf(bio_err, "Couldn't open file or uri %s\n", uri);
        ERR_print_errors(bio_err);
        return ret;
    }

    if (expected != 0) {
        if (!OSSL_STORE_expect(store_ctx, expected)) {
            ERR_print_errors(bio_err);
            goto end2;
        }
    }

    if (criterion != 0) {
        if (!OSSL_STORE_supports_search(store_ctx, criterion)) {
            BIO_printf(bio_err,
                       "%s: the store scheme doesn't support the given search criteria.\n",
                       prog);
            goto end2;
        }

        if (!OSSL_STORE_find(store_ctx, search)) {
            ERR_print_errors(bio_err);
            goto end2;
        }
    }

    /* From here on, we count errors, and we'll return the count at the end */
    ret = 0;

    for (;;) {
        OSSL_STORE_INFO *info = OSSL_STORE_load(store_ctx);
        int type = info == NULL ? 0 : OSSL_STORE_INFO_get_type(info);
        const char *infostr =
            info == NULL ? NULL : OSSL_STORE_INFO_type_string(type);

        if (info == NULL) {
            if (OSSL_STORE_eof(store_ctx))
                break;

            if (OSSL_STORE_error(store_ctx)) {
                if (recursive)
                    ERR_clear_error();
                else
                    ERR_print_errors(bio_err);
                ret++;
                continue;
            }

            BIO_printf(bio_err,
                       "ERROR: OSSL_STORE_load() returned NULL without "
                       "eof or error indications\n");
            BIO_printf(bio_err, "       This is an error in the loader\n");
            ERR_print_errors(bio_err);
            ret++;
            break;
        }

        if (type == OSSL_STORE_INFO_NAME) {
            const char *name = OSSL_STORE_INFO_get0_NAME(info);
            const char *desc = OSSL_STORE_INFO_get0_NAME_description(info);
            indent_printf(indent, bio_out, "%d: %s: %s\n", items, infostr,
                          name);
            if (desc != NULL)
                indent_printf(indent, bio_out, "%s\n", desc);
        } else {
            indent_printf(indent, bio_out, "%d: %s\n", items, infostr);
        }

        /*
         * Unfortunately, PEM_X509_INFO_write_bio() is sorely lacking in
         * functionality, so we must figure out how exactly to write things
         * ourselves...
         */
        switch (type) {
        case OSSL_STORE_INFO_NAME:
            if (recursive) {
                const char *suburi = OSSL_STORE_INFO_get0_NAME(info);
                ret += process(suburi, uimeth, uidata,
                               expected, criterion, search,
                               text, noout, recursive, indent + 2, out, prog);
            }
            break;
        case OSSL_STORE_INFO_PARAMS:
            if (text)
                EVP_PKEY_print_params(out, OSSL_STORE_INFO_get0_PARAMS(info),
                                      0, NULL);
            if (!noout)
                PEM_write_bio_Parameters(out,
                                         OSSL_STORE_INFO_get0_PARAMS(info));
            break;
        case OSSL_STORE_INFO_PKEY:
            if (text)
                EVP_PKEY_print_private(out, OSSL_STORE_INFO_get0_PKEY(info),
                                       0, NULL);
            if (!noout)
                PEM_write_bio_PrivateKey(out, OSSL_STORE_INFO_get0_PKEY(info),
                                         NULL, NULL, 0, NULL, NULL);
            break;
        case OSSL_STORE_INFO_CERT:
            if (text)
                X509_print(out, OSSL_STORE_INFO_get0_CERT(info));
            if (!noout)
                PEM_write_bio_X509(out, OSSL_STORE_INFO_get0_CERT(info));
            break;
        case OSSL_STORE_INFO_CRL:
            if (text)
                X509_CRL_print(out, OSSL_STORE_INFO_get0_CRL(info));
            if (!noout)
                PEM_write_bio_X509_CRL(out, OSSL_STORE_INFO_get0_CRL(info));
            break;
        default:
            BIO_printf(bio_err, "!!! Unknown code\n");
            ret++;
            break;
        }
        items++;
        OSSL_STORE_INFO_free(info);
    }
    indent_printf(indent, out, "Total found: %d\n", items);

 end2:
    if (!OSSL_STORE_close(store_ctx)) {
        ERR_print_errors(bio_err);
        ret++;
    }

    return ret;
}
