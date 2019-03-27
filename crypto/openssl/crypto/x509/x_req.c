/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/asn1t.h>
#include <openssl/x509.h>
#include "internal/x509_int.h"

/*-
 * X509_REQ_INFO is handled in an unusual way to get round
 * invalid encodings. Some broken certificate requests don't
 * encode the attributes field if it is empty. This is in
 * violation of PKCS#10 but we need to tolerate it. We do
 * this by making the attributes field OPTIONAL then using
 * the callback to initialise it to an empty STACK.
 *
 * This means that the field will be correctly encoded unless
 * we NULL out the field.
 *
 * As a result we no longer need the req_kludge field because
 * the information is now contained in the attributes field:
 * 1. If it is NULL then it's the invalid omission.
 * 2. If it is empty it is the correct encoding.
 * 3. If it is not empty then some attributes are present.
 *
 */

static int rinf_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it,
                   void *exarg)
{
    X509_REQ_INFO *rinf = (X509_REQ_INFO *)*pval;

    if (operation == ASN1_OP_NEW_POST) {
        rinf->attributes = sk_X509_ATTRIBUTE_new_null();
        if (!rinf->attributes)
            return 0;
    }
    return 1;
}

ASN1_SEQUENCE_enc(X509_REQ_INFO, enc, rinf_cb) = {
        ASN1_SIMPLE(X509_REQ_INFO, version, ASN1_INTEGER),
        ASN1_SIMPLE(X509_REQ_INFO, subject, X509_NAME),
        ASN1_SIMPLE(X509_REQ_INFO, pubkey, X509_PUBKEY),
        /* This isn't really OPTIONAL but it gets round invalid
         * encodings
         */
        ASN1_IMP_SET_OF_OPT(X509_REQ_INFO, attributes, X509_ATTRIBUTE, 0)
} ASN1_SEQUENCE_END_enc(X509_REQ_INFO, X509_REQ_INFO)

IMPLEMENT_ASN1_FUNCTIONS(X509_REQ_INFO)

ASN1_SEQUENCE_ref(X509_REQ, 0) = {
        ASN1_EMBED(X509_REQ, req_info, X509_REQ_INFO),
        ASN1_EMBED(X509_REQ, sig_alg, X509_ALGOR),
        ASN1_SIMPLE(X509_REQ, signature, ASN1_BIT_STRING)
} ASN1_SEQUENCE_END_ref(X509_REQ, X509_REQ)

IMPLEMENT_ASN1_FUNCTIONS(X509_REQ)

IMPLEMENT_ASN1_DUP_FUNCTION(X509_REQ)
