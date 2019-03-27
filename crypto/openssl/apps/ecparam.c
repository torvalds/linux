/*
 * Copyright 2002-2018 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright (c) 2002, Oracle and/or its affiliates. All rights reserved
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/opensslconf.h>
#ifdef OPENSSL_NO_EC
NON_EMPTY_TRANSLATION_UNIT
#else

# include <stdio.h>
# include <stdlib.h>
# include <time.h>
# include <string.h>
# include "apps.h"
# include "progs.h"
# include <openssl/bio.h>
# include <openssl/err.h>
# include <openssl/bn.h>
# include <openssl/ec.h>
# include <openssl/x509.h>
# include <openssl/pem.h>

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_INFORM, OPT_OUTFORM, OPT_IN, OPT_OUT, OPT_TEXT, OPT_C,
    OPT_CHECK, OPT_LIST_CURVES, OPT_NO_SEED, OPT_NOOUT, OPT_NAME,
    OPT_CONV_FORM, OPT_PARAM_ENC, OPT_GENKEY, OPT_ENGINE,
    OPT_R_ENUM
} OPTION_CHOICE;

const OPTIONS ecparam_options[] = {
    {"help", OPT_HELP, '-', "Display this summary"},
    {"inform", OPT_INFORM, 'F', "Input format - default PEM (DER or PEM)"},
    {"outform", OPT_OUTFORM, 'F', "Output format - default PEM"},
    {"in", OPT_IN, '<', "Input file  - default stdin"},
    {"out", OPT_OUT, '>', "Output file - default stdout"},
    {"text", OPT_TEXT, '-', "Print the ec parameters in text form"},
    {"C", OPT_C, '-', "Print a 'C' function creating the parameters"},
    {"check", OPT_CHECK, '-', "Validate the ec parameters"},
    {"list_curves", OPT_LIST_CURVES, '-',
     "Prints a list of all curve 'short names'"},
    {"no_seed", OPT_NO_SEED, '-',
     "If 'explicit' parameters are chosen do not use the seed"},
    {"noout", OPT_NOOUT, '-', "Do not print the ec parameter"},
    {"name", OPT_NAME, 's',
     "Use the ec parameters with specified 'short name'"},
    {"conv_form", OPT_CONV_FORM, 's', "Specifies the point conversion form "},
    {"param_enc", OPT_PARAM_ENC, 's',
     "Specifies the way the ec parameters are encoded"},
    {"genkey", OPT_GENKEY, '-', "Generate ec key"},
    OPT_R_OPTIONS,
# ifndef OPENSSL_NO_ENGINE
    {"engine", OPT_ENGINE, 's', "Use engine, possibly a hardware device"},
# endif
    {NULL}
};

static OPT_PAIR forms[] = {
    {"compressed", POINT_CONVERSION_COMPRESSED},
    {"uncompressed", POINT_CONVERSION_UNCOMPRESSED},
    {"hybrid", POINT_CONVERSION_HYBRID},
    {NULL}
};

static OPT_PAIR encodings[] = {
    {"named_curve", OPENSSL_EC_NAMED_CURVE},
    {"explicit", 0},
    {NULL}
};

int ecparam_main(int argc, char **argv)
{
    ENGINE *e = NULL;
    BIGNUM *ec_gen = NULL, *ec_order = NULL, *ec_cofactor = NULL;
    BIGNUM *ec_p = NULL, *ec_a = NULL, *ec_b = NULL;
    BIO *in = NULL, *out = NULL;
    EC_GROUP *group = NULL;
    point_conversion_form_t form = POINT_CONVERSION_UNCOMPRESSED;
    char *curve_name = NULL;
    char *infile = NULL, *outfile = NULL, *prog;
    unsigned char *buffer = NULL;
    OPTION_CHOICE o;
    int asn1_flag = OPENSSL_EC_NAMED_CURVE, new_asn1_flag = 0;
    int informat = FORMAT_PEM, outformat = FORMAT_PEM, noout = 0, C = 0;
    int ret = 1, private = 0;
    int list_curves = 0, no_seed = 0, check = 0, new_form = 0;
    int text = 0, i, genkey = 0;

    prog = opt_init(argc, argv, ecparam_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
 opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(ecparam_options);
            ret = 0;
            goto end;
        case OPT_INFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PEMDER, &informat))
                goto opthelp;
            break;
        case OPT_IN:
            infile = opt_arg();
            break;
        case OPT_OUTFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PEMDER, &outformat))
                goto opthelp;
            break;
        case OPT_OUT:
            outfile = opt_arg();
            break;
        case OPT_TEXT:
            text = 1;
            break;
        case OPT_C:
            C = 1;
            break;
        case OPT_CHECK:
            check = 1;
            break;
        case OPT_LIST_CURVES:
            list_curves = 1;
            break;
        case OPT_NO_SEED:
            no_seed = 1;
            break;
        case OPT_NOOUT:
            noout = 1;
            break;
        case OPT_NAME:
            curve_name = opt_arg();
            break;
        case OPT_CONV_FORM:
            if (!opt_pair(opt_arg(), forms, &new_form))
                goto opthelp;
            form = new_form;
            new_form = 1;
            break;
        case OPT_PARAM_ENC:
            if (!opt_pair(opt_arg(), encodings, &asn1_flag))
                goto opthelp;
            new_asn1_flag = 1;
            break;
        case OPT_GENKEY:
            genkey = 1;
            break;
        case OPT_R_CASES:
            if (!opt_rand(o))
                goto end;
            break;
        case OPT_ENGINE:
            e = setup_engine(opt_arg(), 0);
            break;
        }
    }
    argc = opt_num_rest();
    if (argc != 0)
        goto opthelp;

    private = genkey ? 1 : 0;

    in = bio_open_default(infile, 'r', informat);
    if (in == NULL)
        goto end;
    out = bio_open_owner(outfile, outformat, private);
    if (out == NULL)
        goto end;

    if (list_curves) {
        EC_builtin_curve *curves = NULL;
        size_t crv_len = EC_get_builtin_curves(NULL, 0);
        size_t n;

        curves = app_malloc((int)sizeof(*curves) * crv_len, "list curves");
        if (!EC_get_builtin_curves(curves, crv_len)) {
            OPENSSL_free(curves);
            goto end;
        }

        for (n = 0; n < crv_len; n++) {
            const char *comment;
            const char *sname;
            comment = curves[n].comment;
            sname = OBJ_nid2sn(curves[n].nid);
            if (comment == NULL)
                comment = "CURVE DESCRIPTION NOT AVAILABLE";
            if (sname == NULL)
                sname = "";

            BIO_printf(out, "  %-10s: ", sname);
            BIO_printf(out, "%s\n", comment);
        }

        OPENSSL_free(curves);
        ret = 0;
        goto end;
    }

    if (curve_name != NULL) {
        int nid;

        /*
         * workaround for the SECG curve names secp192r1 and secp256r1 (which
         * are the same as the curves prime192v1 and prime256v1 defined in
         * X9.62)
         */
        if (strcmp(curve_name, "secp192r1") == 0) {
            BIO_printf(bio_err, "using curve name prime192v1 "
                       "instead of secp192r1\n");
            nid = NID_X9_62_prime192v1;
        } else if (strcmp(curve_name, "secp256r1") == 0) {
            BIO_printf(bio_err, "using curve name prime256v1 "
                       "instead of secp256r1\n");
            nid = NID_X9_62_prime256v1;
        } else {
            nid = OBJ_sn2nid(curve_name);
        }

        if (nid == 0)
            nid = EC_curve_nist2nid(curve_name);

        if (nid == 0) {
            BIO_printf(bio_err, "unknown curve name (%s)\n", curve_name);
            goto end;
        }

        group = EC_GROUP_new_by_curve_name(nid);
        if (group == NULL) {
            BIO_printf(bio_err, "unable to create curve (%s)\n", curve_name);
            goto end;
        }
        EC_GROUP_set_asn1_flag(group, asn1_flag);
        EC_GROUP_set_point_conversion_form(group, form);
    } else if (informat == FORMAT_ASN1) {
        group = d2i_ECPKParameters_bio(in, NULL);
    } else {
        group = PEM_read_bio_ECPKParameters(in, NULL, NULL, NULL);
    }
    if (group == NULL) {
        BIO_printf(bio_err, "unable to load elliptic curve parameters\n");
        ERR_print_errors(bio_err);
        goto end;
    }

    if (new_form)
        EC_GROUP_set_point_conversion_form(group, form);

    if (new_asn1_flag)
        EC_GROUP_set_asn1_flag(group, asn1_flag);

    if (no_seed) {
        EC_GROUP_set_seed(group, NULL, 0);
    }

    if (text) {
        if (!ECPKParameters_print(out, group, 0))
            goto end;
    }

    if (check) {
        BIO_printf(bio_err, "checking elliptic curve parameters: ");
        if (!EC_GROUP_check(group, NULL)) {
            BIO_printf(bio_err, "failed\n");
            ERR_print_errors(bio_err);
            goto end;
        }
        BIO_printf(bio_err, "ok\n");

    }

    if (C) {
        size_t buf_len = 0, tmp_len = 0;
        const EC_POINT *point;
        int is_prime, len = 0;
        const EC_METHOD *meth = EC_GROUP_method_of(group);

        if ((ec_p = BN_new()) == NULL
                || (ec_a = BN_new()) == NULL
                || (ec_b = BN_new()) == NULL
                || (ec_gen = BN_new()) == NULL
                || (ec_order = BN_new()) == NULL
                || (ec_cofactor = BN_new()) == NULL) {
            perror("Can't allocate BN");
            goto end;
        }

        is_prime = (EC_METHOD_get_field_type(meth) == NID_X9_62_prime_field);
        if (!is_prime) {
            BIO_printf(bio_err, "Can only handle X9.62 prime fields\n");
            goto end;
        }

        if (!EC_GROUP_get_curve(group, ec_p, ec_a, ec_b, NULL))
            goto end;

        if ((point = EC_GROUP_get0_generator(group)) == NULL)
            goto end;
        if (!EC_POINT_point2bn(group, point,
                               EC_GROUP_get_point_conversion_form(group),
                               ec_gen, NULL))
            goto end;
        if (!EC_GROUP_get_order(group, ec_order, NULL))
            goto end;
        if (!EC_GROUP_get_cofactor(group, ec_cofactor, NULL))
            goto end;

        if (!ec_p || !ec_a || !ec_b || !ec_gen || !ec_order || !ec_cofactor)
            goto end;

        len = BN_num_bits(ec_order);

        if ((tmp_len = (size_t)BN_num_bytes(ec_p)) > buf_len)
            buf_len = tmp_len;
        if ((tmp_len = (size_t)BN_num_bytes(ec_a)) > buf_len)
            buf_len = tmp_len;
        if ((tmp_len = (size_t)BN_num_bytes(ec_b)) > buf_len)
            buf_len = tmp_len;
        if ((tmp_len = (size_t)BN_num_bytes(ec_gen)) > buf_len)
            buf_len = tmp_len;
        if ((tmp_len = (size_t)BN_num_bytes(ec_order)) > buf_len)
            buf_len = tmp_len;
        if ((tmp_len = (size_t)BN_num_bytes(ec_cofactor)) > buf_len)
            buf_len = tmp_len;

        buffer = app_malloc(buf_len, "BN buffer");

        BIO_printf(out, "EC_GROUP *get_ec_group_%d(void)\n{\n", len);
        print_bignum_var(out, ec_p, "ec_p", len, buffer);
        print_bignum_var(out, ec_a, "ec_a", len, buffer);
        print_bignum_var(out, ec_b, "ec_b", len, buffer);
        print_bignum_var(out, ec_gen, "ec_gen", len, buffer);
        print_bignum_var(out, ec_order, "ec_order", len, buffer);
        print_bignum_var(out, ec_cofactor, "ec_cofactor", len, buffer);
        BIO_printf(out, "    int ok = 0;\n"
                        "    EC_GROUP *group = NULL;\n"
                        "    EC_POINT *point = NULL;\n"
                        "    BIGNUM *tmp_1 = NULL;\n"
                        "    BIGNUM *tmp_2 = NULL;\n"
                        "    BIGNUM *tmp_3 = NULL;\n"
                        "\n");

        BIO_printf(out, "    if ((tmp_1 = BN_bin2bn(ec_p_%d, sizeof(ec_p_%d), NULL)) == NULL)\n"
                        "        goto err;\n", len, len);
        BIO_printf(out, "    if ((tmp_2 = BN_bin2bn(ec_a_%d, sizeof(ec_a_%d), NULL)) == NULL)\n"
                        "        goto err;\n", len, len);
        BIO_printf(out, "    if ((tmp_3 = BN_bin2bn(ec_b_%d, sizeof(ec_b_%d), NULL)) == NULL)\n"
                        "        goto err;\n", len, len);
        BIO_printf(out, "    if ((group = EC_GROUP_new_curve_GFp(tmp_1, tmp_2, tmp_3, NULL)) == NULL)\n"
                        "        goto err;\n"
                        "\n");
        BIO_printf(out, "    /* build generator */\n");
        BIO_printf(out, "    if ((tmp_1 = BN_bin2bn(ec_gen_%d, sizeof(ec_gen_%d), tmp_1)) == NULL)\n"
                        "        goto err;\n", len, len);
        BIO_printf(out, "    point = EC_POINT_bn2point(group, tmp_1, NULL, NULL);\n");
        BIO_printf(out, "    if (point == NULL)\n"
                        "        goto err;\n");
        BIO_printf(out, "    if ((tmp_2 = BN_bin2bn(ec_order_%d, sizeof(ec_order_%d), tmp_2)) == NULL)\n"
                        "        goto err;\n", len, len);
        BIO_printf(out, "    if ((tmp_3 = BN_bin2bn(ec_cofactor_%d, sizeof(ec_cofactor_%d), tmp_3)) == NULL)\n"
                        "        goto err;\n", len, len);
        BIO_printf(out, "    if (!EC_GROUP_set_generator(group, point, tmp_2, tmp_3))\n"
                        "        goto err;\n"
                        "ok = 1;"
                        "\n");
        BIO_printf(out, "err:\n"
                        "    BN_free(tmp_1);\n"
                        "    BN_free(tmp_2);\n"
                        "    BN_free(tmp_3);\n"
                        "    EC_POINT_free(point);\n"
                        "    if (!ok) {\n"
                        "        EC_GROUP_free(group);\n"
                        "        return NULL;\n"
                        "    }\n"
                        "    return (group);\n"
                        "}\n");
    }

    if (outformat == FORMAT_ASN1 && genkey)
        noout = 1;

    if (!noout) {
        if (outformat == FORMAT_ASN1)
            i = i2d_ECPKParameters_bio(out, group);
        else
            i = PEM_write_bio_ECPKParameters(out, group);
        if (!i) {
            BIO_printf(bio_err, "unable to write elliptic "
                       "curve parameters\n");
            ERR_print_errors(bio_err);
            goto end;
        }
    }

    if (genkey) {
        EC_KEY *eckey = EC_KEY_new();

        if (eckey == NULL)
            goto end;

        if (EC_KEY_set_group(eckey, group) == 0) {
            BIO_printf(bio_err, "unable to set group when generating key\n");
            EC_KEY_free(eckey);
            ERR_print_errors(bio_err);
            goto end;
        }

        if (new_form)
            EC_KEY_set_conv_form(eckey, form);

        if (!EC_KEY_generate_key(eckey)) {
            BIO_printf(bio_err, "unable to generate key\n");
            EC_KEY_free(eckey);
            ERR_print_errors(bio_err);
            goto end;
        }
        assert(private);
        if (outformat == FORMAT_ASN1)
            i = i2d_ECPrivateKey_bio(out, eckey);
        else
            i = PEM_write_bio_ECPrivateKey(out, eckey, NULL,
                                           NULL, 0, NULL, NULL);
        EC_KEY_free(eckey);
    }

    ret = 0;
 end:
    BN_free(ec_p);
    BN_free(ec_a);
    BN_free(ec_b);
    BN_free(ec_gen);
    BN_free(ec_order);
    BN_free(ec_cofactor);
    OPENSSL_free(buffer);
    EC_GROUP_free(group);
    release_engine(e);
    BIO_free(in);
    BIO_free_all(out);
    return ret;
}

#endif
