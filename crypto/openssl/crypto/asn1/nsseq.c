/*
 * Copyright 1999-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <openssl/asn1t.h>
#include <openssl/x509.h>
#include <openssl/objects.h>

static int nsseq_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it,
                    void *exarg)
{
    if (operation == ASN1_OP_NEW_POST) {
        NETSCAPE_CERT_SEQUENCE *nsseq;
        nsseq = (NETSCAPE_CERT_SEQUENCE *)*pval;
        nsseq->type = OBJ_nid2obj(NID_netscape_cert_sequence);
    }
    return 1;
}

/* Netscape certificate sequence structure */

ASN1_SEQUENCE_cb(NETSCAPE_CERT_SEQUENCE, nsseq_cb) = {
        ASN1_SIMPLE(NETSCAPE_CERT_SEQUENCE, type, ASN1_OBJECT),
        ASN1_EXP_SEQUENCE_OF_OPT(NETSCAPE_CERT_SEQUENCE, certs, X509, 0)
} ASN1_SEQUENCE_END_cb(NETSCAPE_CERT_SEQUENCE, NETSCAPE_CERT_SEQUENCE)

IMPLEMENT_ASN1_FUNCTIONS(NETSCAPE_CERT_SEQUENCE)
