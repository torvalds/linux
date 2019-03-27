/*
 * Copyright 1999-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/x509v3.h>
#include "internal/x509_int.h"

static int tr_cmp(const X509_TRUST *const *a, const X509_TRUST *const *b);
static void trtable_free(X509_TRUST *p);

static int trust_1oidany(X509_TRUST *trust, X509 *x, int flags);
static int trust_1oid(X509_TRUST *trust, X509 *x, int flags);
static int trust_compat(X509_TRUST *trust, X509 *x, int flags);

static int obj_trust(int id, X509 *x, int flags);
static int (*default_trust) (int id, X509 *x, int flags) = obj_trust;

/*
 * WARNING: the following table should be kept in order of trust and without
 * any gaps so we can just subtract the minimum trust value to get an index
 * into the table
 */

static X509_TRUST trstandard[] = {
    {X509_TRUST_COMPAT, 0, trust_compat, "compatible", 0, NULL},
    {X509_TRUST_SSL_CLIENT, 0, trust_1oidany, "SSL Client", NID_client_auth,
     NULL},
    {X509_TRUST_SSL_SERVER, 0, trust_1oidany, "SSL Server", NID_server_auth,
     NULL},
    {X509_TRUST_EMAIL, 0, trust_1oidany, "S/MIME email", NID_email_protect,
     NULL},
    {X509_TRUST_OBJECT_SIGN, 0, trust_1oidany, "Object Signer", NID_code_sign,
     NULL},
    {X509_TRUST_OCSP_SIGN, 0, trust_1oid, "OCSP responder", NID_OCSP_sign,
     NULL},
    {X509_TRUST_OCSP_REQUEST, 0, trust_1oid, "OCSP request", NID_ad_OCSP,
     NULL},
    {X509_TRUST_TSA, 0, trust_1oidany, "TSA server", NID_time_stamp, NULL}
};

#define X509_TRUST_COUNT        OSSL_NELEM(trstandard)

static STACK_OF(X509_TRUST) *trtable = NULL;

static int tr_cmp(const X509_TRUST *const *a, const X509_TRUST *const *b)
{
    return (*a)->trust - (*b)->trust;
}

int (*X509_TRUST_set_default(int (*trust) (int, X509 *, int))) (int, X509 *,
                                                                int) {
    int (*oldtrust) (int, X509 *, int);
    oldtrust = default_trust;
    default_trust = trust;
    return oldtrust;
}

int X509_check_trust(X509 *x, int id, int flags)
{
    X509_TRUST *pt;
    int idx;

    /* We get this as a default value */
    if (id == X509_TRUST_DEFAULT)
        return obj_trust(NID_anyExtendedKeyUsage, x,
                         flags | X509_TRUST_DO_SS_COMPAT);
    idx = X509_TRUST_get_by_id(id);
    if (idx == -1)
        return default_trust(id, x, flags);
    pt = X509_TRUST_get0(idx);
    return pt->check_trust(pt, x, flags);
}

int X509_TRUST_get_count(void)
{
    if (!trtable)
        return X509_TRUST_COUNT;
    return sk_X509_TRUST_num(trtable) + X509_TRUST_COUNT;
}

X509_TRUST *X509_TRUST_get0(int idx)
{
    if (idx < 0)
        return NULL;
    if (idx < (int)X509_TRUST_COUNT)
        return trstandard + idx;
    return sk_X509_TRUST_value(trtable, idx - X509_TRUST_COUNT);
}

int X509_TRUST_get_by_id(int id)
{
    X509_TRUST tmp;
    int idx;

    if ((id >= X509_TRUST_MIN) && (id <= X509_TRUST_MAX))
        return id - X509_TRUST_MIN;
    if (trtable == NULL)
        return -1;
    tmp.trust = id;
    idx = sk_X509_TRUST_find(trtable, &tmp);
    if (idx < 0)
        return -1;
    return idx + X509_TRUST_COUNT;
}

int X509_TRUST_set(int *t, int trust)
{
    if (X509_TRUST_get_by_id(trust) == -1) {
        X509err(X509_F_X509_TRUST_SET, X509_R_INVALID_TRUST);
        return 0;
    }
    *t = trust;
    return 1;
}

int X509_TRUST_add(int id, int flags, int (*ck) (X509_TRUST *, X509 *, int),
                   const char *name, int arg1, void *arg2)
{
    int idx;
    X509_TRUST *trtmp;
    /*
     * This is set according to what we change: application can't set it
     */
    flags &= ~X509_TRUST_DYNAMIC;
    /* This will always be set for application modified trust entries */
    flags |= X509_TRUST_DYNAMIC_NAME;
    /* Get existing entry if any */
    idx = X509_TRUST_get_by_id(id);
    /* Need a new entry */
    if (idx == -1) {
        if ((trtmp = OPENSSL_malloc(sizeof(*trtmp))) == NULL) {
            X509err(X509_F_X509_TRUST_ADD, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        trtmp->flags = X509_TRUST_DYNAMIC;
    } else
        trtmp = X509_TRUST_get0(idx);

    /* OPENSSL_free existing name if dynamic */
    if (trtmp->flags & X509_TRUST_DYNAMIC_NAME)
        OPENSSL_free(trtmp->name);
    /* dup supplied name */
    if ((trtmp->name = OPENSSL_strdup(name)) == NULL) {
        X509err(X509_F_X509_TRUST_ADD, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    /* Keep the dynamic flag of existing entry */
    trtmp->flags &= X509_TRUST_DYNAMIC;
    /* Set all other flags */
    trtmp->flags |= flags;

    trtmp->trust = id;
    trtmp->check_trust = ck;
    trtmp->arg1 = arg1;
    trtmp->arg2 = arg2;

    /* If its a new entry manage the dynamic table */
    if (idx == -1) {
        if (trtable == NULL
            && (trtable = sk_X509_TRUST_new(tr_cmp)) == NULL) {
            X509err(X509_F_X509_TRUST_ADD, ERR_R_MALLOC_FAILURE);
            goto err;;
        }
        if (!sk_X509_TRUST_push(trtable, trtmp)) {
            X509err(X509_F_X509_TRUST_ADD, ERR_R_MALLOC_FAILURE);
            goto err;
        }
    }
    return 1;
 err:
    if (idx == -1) {
        OPENSSL_free(trtmp->name);
        OPENSSL_free(trtmp);
    }
    return 0;
}

static void trtable_free(X509_TRUST *p)
{
    if (!p)
        return;
    if (p->flags & X509_TRUST_DYNAMIC) {
        if (p->flags & X509_TRUST_DYNAMIC_NAME)
            OPENSSL_free(p->name);
        OPENSSL_free(p);
    }
}

void X509_TRUST_cleanup(void)
{
    sk_X509_TRUST_pop_free(trtable, trtable_free);
    trtable = NULL;
}

int X509_TRUST_get_flags(const X509_TRUST *xp)
{
    return xp->flags;
}

char *X509_TRUST_get0_name(const X509_TRUST *xp)
{
    return xp->name;
}

int X509_TRUST_get_trust(const X509_TRUST *xp)
{
    return xp->trust;
}

static int trust_1oidany(X509_TRUST *trust, X509 *x, int flags)
{
    /*
     * Declare the chain verified if the desired trust OID is not rejected in
     * any auxiliary trust info for this certificate, and the OID is either
     * expressly trusted, or else either "anyEKU" is trusted, or the
     * certificate is self-signed.
     */
    flags |= X509_TRUST_DO_SS_COMPAT | X509_TRUST_OK_ANY_EKU;
    return obj_trust(trust->arg1, x, flags);
}

static int trust_1oid(X509_TRUST *trust, X509 *x, int flags)
{
    /*
     * Declare the chain verified only if the desired trust OID is not
     * rejected and is expressly trusted.  Neither "anyEKU" nor "compat"
     * trust in self-signed certificates apply.
     */
    flags &= ~(X509_TRUST_DO_SS_COMPAT | X509_TRUST_OK_ANY_EKU);
    return obj_trust(trust->arg1, x, flags);
}

static int trust_compat(X509_TRUST *trust, X509 *x, int flags)
{
    /* Call for side-effect of computing hash and caching extensions */
    X509_check_purpose(x, -1, 0);
    if ((flags & X509_TRUST_NO_SS_COMPAT) == 0 && x->ex_flags & EXFLAG_SS)
        return X509_TRUST_TRUSTED;
    else
        return X509_TRUST_UNTRUSTED;
}

static int obj_trust(int id, X509 *x, int flags)
{
    X509_CERT_AUX *ax = x->aux;
    int i;

    if (ax && ax->reject) {
        for (i = 0; i < sk_ASN1_OBJECT_num(ax->reject); i++) {
            ASN1_OBJECT *obj = sk_ASN1_OBJECT_value(ax->reject, i);
            int nid = OBJ_obj2nid(obj);

            if (nid == id || (nid == NID_anyExtendedKeyUsage &&
                (flags & X509_TRUST_OK_ANY_EKU)))
                return X509_TRUST_REJECTED;
        }
    }

    if (ax && ax->trust) {
        for (i = 0; i < sk_ASN1_OBJECT_num(ax->trust); i++) {
            ASN1_OBJECT *obj = sk_ASN1_OBJECT_value(ax->trust, i);
            int nid = OBJ_obj2nid(obj);

            if (nid == id || (nid == NID_anyExtendedKeyUsage &&
                (flags & X509_TRUST_OK_ANY_EKU)))
                return X509_TRUST_TRUSTED;
        }
        /*
         * Reject when explicit trust EKU are set and none match.
         *
         * Returning untrusted is enough for for full chains that end in
         * self-signed roots, because when explicit trust is specified it
         * suppresses the default blanket trust of self-signed objects.
         *
         * But for partial chains, this is not enough, because absent a similar
         * trust-self-signed policy, non matching EKUs are indistinguishable
         * from lack of EKU constraints.
         *
         * Therefore, failure to match any trusted purpose must trigger an
         * explicit reject.
         */
        return X509_TRUST_REJECTED;
    }

    if ((flags & X509_TRUST_DO_SS_COMPAT) == 0)
        return X509_TRUST_UNTRUSTED;

    /*
     * Not rejected, and there is no list of accepted uses, try compat.
     */
    return trust_compat(NULL, x, flags);
}
