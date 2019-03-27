/*
 * Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifdef OPENSSL_NO_CT
# error "CT is disabled"
#endif

#include <openssl/asn1.h>
#include <openssl/bio.h>

#include "ct_locl.h"

static void SCT_signature_algorithms_print(const SCT *sct, BIO *out)
{
    int nid = SCT_get_signature_nid(sct);

    if (nid == NID_undef)
        BIO_printf(out, "%02X%02X", sct->hash_alg, sct->sig_alg);
    else
        BIO_printf(out, "%s", OBJ_nid2ln(nid));
}

static void timestamp_print(uint64_t timestamp, BIO *out)
{
    ASN1_GENERALIZEDTIME *gen = ASN1_GENERALIZEDTIME_new();
    char genstr[20];

    if (gen == NULL)
        return;
    ASN1_GENERALIZEDTIME_adj(gen, (time_t)0,
                             (int)(timestamp / 86400000),
                             (timestamp % 86400000) / 1000);
    /*
     * Note GeneralizedTime from ASN1_GENERALIZETIME_adj is always 15
     * characters long with a final Z. Update it with fractional seconds.
     */
    BIO_snprintf(genstr, sizeof(genstr), "%.14s.%03dZ",
                 ASN1_STRING_get0_data(gen), (unsigned int)(timestamp % 1000));
    if (ASN1_GENERALIZEDTIME_set_string(gen, genstr))
        ASN1_GENERALIZEDTIME_print(out, gen);
    ASN1_GENERALIZEDTIME_free(gen);
}

const char *SCT_validation_status_string(const SCT *sct)
{

    switch (SCT_get_validation_status(sct)) {
    case SCT_VALIDATION_STATUS_NOT_SET:
        return "not set";
    case SCT_VALIDATION_STATUS_UNKNOWN_VERSION:
        return "unknown version";
    case SCT_VALIDATION_STATUS_UNKNOWN_LOG:
        return "unknown log";
    case SCT_VALIDATION_STATUS_UNVERIFIED:
        return "unverified";
    case SCT_VALIDATION_STATUS_INVALID:
        return "invalid";
    case SCT_VALIDATION_STATUS_VALID:
        return "valid";
    }
    return "unknown status";
}

void SCT_print(const SCT *sct, BIO *out, int indent,
               const CTLOG_STORE *log_store)
{
    const CTLOG *log = NULL;

    if (log_store != NULL) {
        log = CTLOG_STORE_get0_log_by_id(log_store, sct->log_id,
                                         sct->log_id_len);
    }

    BIO_printf(out, "%*sSigned Certificate Timestamp:", indent, "");
    BIO_printf(out, "\n%*sVersion   : ", indent + 4, "");

    if (sct->version != SCT_VERSION_V1) {
        BIO_printf(out, "unknown\n%*s", indent + 16, "");
        BIO_hex_string(out, indent + 16, 16, sct->sct, sct->sct_len);
        return;
    }

    BIO_printf(out, "v1 (0x0)");

    if (log != NULL) {
        BIO_printf(out, "\n%*sLog       : %s", indent + 4, "",
                   CTLOG_get0_name(log));
    }

    BIO_printf(out, "\n%*sLog ID    : ", indent + 4, "");
    BIO_hex_string(out, indent + 16, 16, sct->log_id, sct->log_id_len);

    BIO_printf(out, "\n%*sTimestamp : ", indent + 4, "");
    timestamp_print(sct->timestamp, out);

    BIO_printf(out, "\n%*sExtensions: ", indent + 4, "");
    if (sct->ext_len == 0)
        BIO_printf(out, "none");
    else
        BIO_hex_string(out, indent + 16, 16, sct->ext, sct->ext_len);

    BIO_printf(out, "\n%*sSignature : ", indent + 4, "");
    SCT_signature_algorithms_print(sct, out);
    BIO_printf(out, "\n%*s            ", indent + 4, "");
    BIO_hex_string(out, indent + 16, 16, sct->sig, sct->sig_len);
}

void SCT_LIST_print(const STACK_OF(SCT) *sct_list, BIO *out, int indent,
                    const char *separator, const CTLOG_STORE *log_store)
{
    int sct_count = sk_SCT_num(sct_list);
    int i;

    for (i = 0; i < sct_count; ++i) {
        SCT *sct = sk_SCT_value(sct_list, i);

        SCT_print(sct, out, indent, log_store);
        if (i < sk_SCT_num(sct_list) - 1)
            BIO_printf(out, "%s", separator);
    }
}
