/*
 * Copyright 2006-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/objects.h>
#include <openssl/bn.h>
#include <openssl/x509v3.h>
#include <openssl/ts.h>
#include "ts_lcl.h"

struct status_map_st {
    int bit;
    const char *text;
};

static int ts_status_map_print(BIO *bio, const struct status_map_st *a,
                               const ASN1_BIT_STRING *v);
static int ts_ACCURACY_print_bio(BIO *bio, const TS_ACCURACY *accuracy);


int TS_RESP_print_bio(BIO *bio, TS_RESP *a)
{
    BIO_printf(bio, "Status info:\n");
    TS_STATUS_INFO_print_bio(bio, a->status_info);

    BIO_printf(bio, "\nTST info:\n");
    if (a->tst_info != NULL)
        TS_TST_INFO_print_bio(bio, a->tst_info);
    else
        BIO_printf(bio, "Not included.\n");

    return 1;
}

int TS_STATUS_INFO_print_bio(BIO *bio, TS_STATUS_INFO *a)
{
    static const char *status_map[] = {
        "Granted.",
        "Granted with modifications.",
        "Rejected.",
        "Waiting.",
        "Revocation warning.",
        "Revoked."
    };
    static const struct status_map_st failure_map[] = {
        {TS_INFO_BAD_ALG,
         "unrecognized or unsupported algorithm identifier"},
        {TS_INFO_BAD_REQUEST,
         "transaction not permitted or supported"},
        {TS_INFO_BAD_DATA_FORMAT,
         "the data submitted has the wrong format"},
        {TS_INFO_TIME_NOT_AVAILABLE,
         "the TSA's time source is not available"},
        {TS_INFO_UNACCEPTED_POLICY,
         "the requested TSA policy is not supported by the TSA"},
        {TS_INFO_UNACCEPTED_EXTENSION,
         "the requested extension is not supported by the TSA"},
        {TS_INFO_ADD_INFO_NOT_AVAILABLE,
         "the additional information requested could not be understood "
         "or is not available"},
        {TS_INFO_SYSTEM_FAILURE,
         "the request cannot be handled due to system failure"},
        {-1, NULL}
    };
    long status;
    int i, lines = 0;

    BIO_printf(bio, "Status: ");
    status = ASN1_INTEGER_get(a->status);
    if (0 <= status && status < (long)OSSL_NELEM(status_map))
        BIO_printf(bio, "%s\n", status_map[status]);
    else
        BIO_printf(bio, "out of bounds\n");

    BIO_printf(bio, "Status description: ");
    for (i = 0; i < sk_ASN1_UTF8STRING_num(a->text); ++i) {
        if (i > 0)
            BIO_puts(bio, "\t");
        ASN1_STRING_print_ex(bio, sk_ASN1_UTF8STRING_value(a->text, i), 0);
        BIO_puts(bio, "\n");
    }
    if (i == 0)
        BIO_printf(bio, "unspecified\n");

    BIO_printf(bio, "Failure info: ");
    if (a->failure_info != NULL)
        lines = ts_status_map_print(bio, failure_map, a->failure_info);
    if (lines == 0)
        BIO_printf(bio, "unspecified");
    BIO_printf(bio, "\n");

    return 1;
}

static int ts_status_map_print(BIO *bio, const struct status_map_st *a,
                               const ASN1_BIT_STRING *v)
{
    int lines = 0;

    for (; a->bit >= 0; ++a) {
        if (ASN1_BIT_STRING_get_bit(v, a->bit)) {
            if (++lines > 1)
                BIO_printf(bio, ", ");
            BIO_printf(bio, "%s", a->text);
        }
    }

    return lines;
}

int TS_TST_INFO_print_bio(BIO *bio, TS_TST_INFO *a)
{
    int v;

    if (a == NULL)
        return 0;

    v = ASN1_INTEGER_get(a->version);
    BIO_printf(bio, "Version: %d\n", v);

    BIO_printf(bio, "Policy OID: ");
    TS_OBJ_print_bio(bio, a->policy_id);

    TS_MSG_IMPRINT_print_bio(bio, a->msg_imprint);

    BIO_printf(bio, "Serial number: ");
    if (a->serial == NULL)
        BIO_printf(bio, "unspecified");
    else
        TS_ASN1_INTEGER_print_bio(bio, a->serial);
    BIO_write(bio, "\n", 1);

    BIO_printf(bio, "Time stamp: ");
    ASN1_GENERALIZEDTIME_print(bio, a->time);
    BIO_write(bio, "\n", 1);

    BIO_printf(bio, "Accuracy: ");
    if (a->accuracy == NULL)
        BIO_printf(bio, "unspecified");
    else
        ts_ACCURACY_print_bio(bio, a->accuracy);
    BIO_write(bio, "\n", 1);

    BIO_printf(bio, "Ordering: %s\n", a->ordering ? "yes" : "no");

    BIO_printf(bio, "Nonce: ");
    if (a->nonce == NULL)
        BIO_printf(bio, "unspecified");
    else
        TS_ASN1_INTEGER_print_bio(bio, a->nonce);
    BIO_write(bio, "\n", 1);

    BIO_printf(bio, "TSA: ");
    if (a->tsa == NULL)
        BIO_printf(bio, "unspecified");
    else {
        STACK_OF(CONF_VALUE) *nval;
        if ((nval = i2v_GENERAL_NAME(NULL, a->tsa, NULL)))
            X509V3_EXT_val_prn(bio, nval, 0, 0);
        sk_CONF_VALUE_pop_free(nval, X509V3_conf_free);
    }
    BIO_write(bio, "\n", 1);

    TS_ext_print_bio(bio, a->extensions);

    return 1;
}

static int ts_ACCURACY_print_bio(BIO *bio, const TS_ACCURACY *a)
{
    if (a->seconds != NULL)
        TS_ASN1_INTEGER_print_bio(bio, a->seconds);
    else
        BIO_printf(bio, "unspecified");
    BIO_printf(bio, " seconds, ");
    if (a->millis != NULL)
        TS_ASN1_INTEGER_print_bio(bio, a->millis);
    else
        BIO_printf(bio, "unspecified");
    BIO_printf(bio, " millis, ");
    if (a->micros != NULL)
        TS_ASN1_INTEGER_print_bio(bio, a->micros);
    else
        BIO_printf(bio, "unspecified");
    BIO_printf(bio, " micros");

    return 1;
}
