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
#include "internal/numbers.h"
#include <openssl/x509v3.h>
#include <openssl/x509_vfy.h>
#include "internal/x509_int.h"
#include "internal/tsan_assist.h"

static void x509v3_cache_extensions(X509 *x);

static int check_ssl_ca(const X509 *x);
static int check_purpose_ssl_client(const X509_PURPOSE *xp, const X509 *x,
                                    int ca);
static int check_purpose_ssl_server(const X509_PURPOSE *xp, const X509 *x,
                                    int ca);
static int check_purpose_ns_ssl_server(const X509_PURPOSE *xp, const X509 *x,
                                       int ca);
static int purpose_smime(const X509 *x, int ca);
static int check_purpose_smime_sign(const X509_PURPOSE *xp, const X509 *x,
                                    int ca);
static int check_purpose_smime_encrypt(const X509_PURPOSE *xp, const X509 *x,
                                       int ca);
static int check_purpose_crl_sign(const X509_PURPOSE *xp, const X509 *x,
                                  int ca);
static int check_purpose_timestamp_sign(const X509_PURPOSE *xp, const X509 *x,
                                        int ca);
static int no_check(const X509_PURPOSE *xp, const X509 *x, int ca);
static int ocsp_helper(const X509_PURPOSE *xp, const X509 *x, int ca);

static int xp_cmp(const X509_PURPOSE *const *a, const X509_PURPOSE *const *b);
static void xptable_free(X509_PURPOSE *p);

static X509_PURPOSE xstandard[] = {
    {X509_PURPOSE_SSL_CLIENT, X509_TRUST_SSL_CLIENT, 0,
     check_purpose_ssl_client, "SSL client", "sslclient", NULL},
    {X509_PURPOSE_SSL_SERVER, X509_TRUST_SSL_SERVER, 0,
     check_purpose_ssl_server, "SSL server", "sslserver", NULL},
    {X509_PURPOSE_NS_SSL_SERVER, X509_TRUST_SSL_SERVER, 0,
     check_purpose_ns_ssl_server, "Netscape SSL server", "nssslserver", NULL},
    {X509_PURPOSE_SMIME_SIGN, X509_TRUST_EMAIL, 0, check_purpose_smime_sign,
     "S/MIME signing", "smimesign", NULL},
    {X509_PURPOSE_SMIME_ENCRYPT, X509_TRUST_EMAIL, 0,
     check_purpose_smime_encrypt, "S/MIME encryption", "smimeencrypt", NULL},
    {X509_PURPOSE_CRL_SIGN, X509_TRUST_COMPAT, 0, check_purpose_crl_sign,
     "CRL signing", "crlsign", NULL},
    {X509_PURPOSE_ANY, X509_TRUST_DEFAULT, 0, no_check, "Any Purpose", "any",
     NULL},
    {X509_PURPOSE_OCSP_HELPER, X509_TRUST_COMPAT, 0, ocsp_helper,
     "OCSP helper", "ocsphelper", NULL},
    {X509_PURPOSE_TIMESTAMP_SIGN, X509_TRUST_TSA, 0,
     check_purpose_timestamp_sign, "Time Stamp signing", "timestampsign",
     NULL},
};

#define X509_PURPOSE_COUNT OSSL_NELEM(xstandard)

static STACK_OF(X509_PURPOSE) *xptable = NULL;

static int xp_cmp(const X509_PURPOSE *const *a, const X509_PURPOSE *const *b)
{
    return (*a)->purpose - (*b)->purpose;
}

/*
 * As much as I'd like to make X509_check_purpose use a "const" X509* I
 * really can't because it does recalculate hashes and do other non-const
 * things.
 */
int X509_check_purpose(X509 *x, int id, int ca)
{
    int idx;
    const X509_PURPOSE *pt;

    x509v3_cache_extensions(x);

    /* Return if side-effect only call */
    if (id == -1)
        return 1;
    idx = X509_PURPOSE_get_by_id(id);
    if (idx == -1)
        return -1;
    pt = X509_PURPOSE_get0(idx);
    return pt->check_purpose(pt, x, ca);
}

int X509_PURPOSE_set(int *p, int purpose)
{
    if (X509_PURPOSE_get_by_id(purpose) == -1) {
        X509V3err(X509V3_F_X509_PURPOSE_SET, X509V3_R_INVALID_PURPOSE);
        return 0;
    }
    *p = purpose;
    return 1;
}

int X509_PURPOSE_get_count(void)
{
    if (!xptable)
        return X509_PURPOSE_COUNT;
    return sk_X509_PURPOSE_num(xptable) + X509_PURPOSE_COUNT;
}

X509_PURPOSE *X509_PURPOSE_get0(int idx)
{
    if (idx < 0)
        return NULL;
    if (idx < (int)X509_PURPOSE_COUNT)
        return xstandard + idx;
    return sk_X509_PURPOSE_value(xptable, idx - X509_PURPOSE_COUNT);
}

int X509_PURPOSE_get_by_sname(const char *sname)
{
    int i;
    X509_PURPOSE *xptmp;
    for (i = 0; i < X509_PURPOSE_get_count(); i++) {
        xptmp = X509_PURPOSE_get0(i);
        if (strcmp(xptmp->sname, sname) == 0)
            return i;
    }
    return -1;
}

int X509_PURPOSE_get_by_id(int purpose)
{
    X509_PURPOSE tmp;
    int idx;

    if ((purpose >= X509_PURPOSE_MIN) && (purpose <= X509_PURPOSE_MAX))
        return purpose - X509_PURPOSE_MIN;
    if (xptable == NULL)
        return -1;
    tmp.purpose = purpose;
    idx = sk_X509_PURPOSE_find(xptable, &tmp);
    if (idx < 0)
        return -1;
    return idx + X509_PURPOSE_COUNT;
}

int X509_PURPOSE_add(int id, int trust, int flags,
                     int (*ck) (const X509_PURPOSE *, const X509 *, int),
                     const char *name, const char *sname, void *arg)
{
    int idx;
    X509_PURPOSE *ptmp;
    /*
     * This is set according to what we change: application can't set it
     */
    flags &= ~X509_PURPOSE_DYNAMIC;
    /* This will always be set for application modified trust entries */
    flags |= X509_PURPOSE_DYNAMIC_NAME;
    /* Get existing entry if any */
    idx = X509_PURPOSE_get_by_id(id);
    /* Need a new entry */
    if (idx == -1) {
        if ((ptmp = OPENSSL_malloc(sizeof(*ptmp))) == NULL) {
            X509V3err(X509V3_F_X509_PURPOSE_ADD, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        ptmp->flags = X509_PURPOSE_DYNAMIC;
    } else
        ptmp = X509_PURPOSE_get0(idx);

    /* OPENSSL_free existing name if dynamic */
    if (ptmp->flags & X509_PURPOSE_DYNAMIC_NAME) {
        OPENSSL_free(ptmp->name);
        OPENSSL_free(ptmp->sname);
    }
    /* dup supplied name */
    ptmp->name = OPENSSL_strdup(name);
    ptmp->sname = OPENSSL_strdup(sname);
    if (!ptmp->name || !ptmp->sname) {
        X509V3err(X509V3_F_X509_PURPOSE_ADD, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    /* Keep the dynamic flag of existing entry */
    ptmp->flags &= X509_PURPOSE_DYNAMIC;
    /* Set all other flags */
    ptmp->flags |= flags;

    ptmp->purpose = id;
    ptmp->trust = trust;
    ptmp->check_purpose = ck;
    ptmp->usr_data = arg;

    /* If its a new entry manage the dynamic table */
    if (idx == -1) {
        if (xptable == NULL
            && (xptable = sk_X509_PURPOSE_new(xp_cmp)) == NULL) {
            X509V3err(X509V3_F_X509_PURPOSE_ADD, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        if (!sk_X509_PURPOSE_push(xptable, ptmp)) {
            X509V3err(X509V3_F_X509_PURPOSE_ADD, ERR_R_MALLOC_FAILURE);
            goto err;
        }
    }
    return 1;
 err:
    if (idx == -1) {
        OPENSSL_free(ptmp->name);
        OPENSSL_free(ptmp->sname);
        OPENSSL_free(ptmp);
    }
    return 0;
}

static void xptable_free(X509_PURPOSE *p)
{
    if (!p)
        return;
    if (p->flags & X509_PURPOSE_DYNAMIC) {
        if (p->flags & X509_PURPOSE_DYNAMIC_NAME) {
            OPENSSL_free(p->name);
            OPENSSL_free(p->sname);
        }
        OPENSSL_free(p);
    }
}

void X509_PURPOSE_cleanup(void)
{
    sk_X509_PURPOSE_pop_free(xptable, xptable_free);
    xptable = NULL;
}

int X509_PURPOSE_get_id(const X509_PURPOSE *xp)
{
    return xp->purpose;
}

char *X509_PURPOSE_get0_name(const X509_PURPOSE *xp)
{
    return xp->name;
}

char *X509_PURPOSE_get0_sname(const X509_PURPOSE *xp)
{
    return xp->sname;
}

int X509_PURPOSE_get_trust(const X509_PURPOSE *xp)
{
    return xp->trust;
}

static int nid_cmp(const int *a, const int *b)
{
    return *a - *b;
}

DECLARE_OBJ_BSEARCH_CMP_FN(int, int, nid);
IMPLEMENT_OBJ_BSEARCH_CMP_FN(int, int, nid);

int X509_supported_extension(X509_EXTENSION *ex)
{
    /*
     * This table is a list of the NIDs of supported extensions: that is
     * those which are used by the verify process. If an extension is
     * critical and doesn't appear in this list then the verify process will
     * normally reject the certificate. The list must be kept in numerical
     * order because it will be searched using bsearch.
     */

    static const int supported_nids[] = {
        NID_netscape_cert_type, /* 71 */
        NID_key_usage,          /* 83 */
        NID_subject_alt_name,   /* 85 */
        NID_basic_constraints,  /* 87 */
        NID_certificate_policies, /* 89 */
        NID_crl_distribution_points, /* 103 */
        NID_ext_key_usage,      /* 126 */
#ifndef OPENSSL_NO_RFC3779
        NID_sbgp_ipAddrBlock,   /* 290 */
        NID_sbgp_autonomousSysNum, /* 291 */
#endif
        NID_policy_constraints, /* 401 */
        NID_proxyCertInfo,      /* 663 */
        NID_name_constraints,   /* 666 */
        NID_policy_mappings,    /* 747 */
        NID_inhibit_any_policy  /* 748 */
    };

    int ex_nid = OBJ_obj2nid(X509_EXTENSION_get_object(ex));

    if (ex_nid == NID_undef)
        return 0;

    if (OBJ_bsearch_nid(&ex_nid, supported_nids, OSSL_NELEM(supported_nids)))
        return 1;
    return 0;
}

static void setup_dp(X509 *x, DIST_POINT *dp)
{
    X509_NAME *iname = NULL;
    int i;
    if (dp->reasons) {
        if (dp->reasons->length > 0)
            dp->dp_reasons = dp->reasons->data[0];
        if (dp->reasons->length > 1)
            dp->dp_reasons |= (dp->reasons->data[1] << 8);
        dp->dp_reasons &= CRLDP_ALL_REASONS;
    } else
        dp->dp_reasons = CRLDP_ALL_REASONS;
    if (!dp->distpoint || (dp->distpoint->type != 1))
        return;
    for (i = 0; i < sk_GENERAL_NAME_num(dp->CRLissuer); i++) {
        GENERAL_NAME *gen = sk_GENERAL_NAME_value(dp->CRLissuer, i);
        if (gen->type == GEN_DIRNAME) {
            iname = gen->d.directoryName;
            break;
        }
    }
    if (!iname)
        iname = X509_get_issuer_name(x);

    DIST_POINT_set_dpname(dp->distpoint, iname);

}

static void setup_crldp(X509 *x)
{
    int i;
    x->crldp = X509_get_ext_d2i(x, NID_crl_distribution_points, NULL, NULL);
    for (i = 0; i < sk_DIST_POINT_num(x->crldp); i++)
        setup_dp(x, sk_DIST_POINT_value(x->crldp, i));
}

#define V1_ROOT (EXFLAG_V1|EXFLAG_SS)
#define ku_reject(x, usage) \
        (((x)->ex_flags & EXFLAG_KUSAGE) && !((x)->ex_kusage & (usage)))
#define xku_reject(x, usage) \
        (((x)->ex_flags & EXFLAG_XKUSAGE) && !((x)->ex_xkusage & (usage)))
#define ns_reject(x, usage) \
        (((x)->ex_flags & EXFLAG_NSCERT) && !((x)->ex_nscert & (usage)))

static void x509v3_cache_extensions(X509 *x)
{
    BASIC_CONSTRAINTS *bs;
    PROXY_CERT_INFO_EXTENSION *pci;
    ASN1_BIT_STRING *usage;
    ASN1_BIT_STRING *ns;
    EXTENDED_KEY_USAGE *extusage;
    X509_EXTENSION *ex;
    int i;

#ifdef tsan_ld_acq
    /* fast lock-free check, see end of the function for details. */
    if (tsan_ld_acq((TSAN_QUALIFIER int *)&x->ex_cached))
        return;
#endif

    CRYPTO_THREAD_write_lock(x->lock);
    if (x->ex_flags & EXFLAG_SET) {
        CRYPTO_THREAD_unlock(x->lock);
        return;
    }

    X509_digest(x, EVP_sha1(), x->sha1_hash, NULL);
    /* V1 should mean no extensions ... */
    if (!X509_get_version(x))
        x->ex_flags |= EXFLAG_V1;
    /* Handle basic constraints */
    if ((bs = X509_get_ext_d2i(x, NID_basic_constraints, NULL, NULL))) {
        if (bs->ca)
            x->ex_flags |= EXFLAG_CA;
        if (bs->pathlen) {
            if ((bs->pathlen->type == V_ASN1_NEG_INTEGER)
                || !bs->ca) {
                x->ex_flags |= EXFLAG_INVALID;
                x->ex_pathlen = 0;
            } else
                x->ex_pathlen = ASN1_INTEGER_get(bs->pathlen);
        } else
            x->ex_pathlen = -1;
        BASIC_CONSTRAINTS_free(bs);
        x->ex_flags |= EXFLAG_BCONS;
    }
    /* Handle proxy certificates */
    if ((pci = X509_get_ext_d2i(x, NID_proxyCertInfo, NULL, NULL))) {
        if (x->ex_flags & EXFLAG_CA
            || X509_get_ext_by_NID(x, NID_subject_alt_name, -1) >= 0
            || X509_get_ext_by_NID(x, NID_issuer_alt_name, -1) >= 0) {
            x->ex_flags |= EXFLAG_INVALID;
        }
        if (pci->pcPathLengthConstraint) {
            x->ex_pcpathlen = ASN1_INTEGER_get(pci->pcPathLengthConstraint);
        } else
            x->ex_pcpathlen = -1;
        PROXY_CERT_INFO_EXTENSION_free(pci);
        x->ex_flags |= EXFLAG_PROXY;
    }
    /* Handle key usage */
    if ((usage = X509_get_ext_d2i(x, NID_key_usage, NULL, NULL))) {
        if (usage->length > 0) {
            x->ex_kusage = usage->data[0];
            if (usage->length > 1)
                x->ex_kusage |= usage->data[1] << 8;
        } else
            x->ex_kusage = 0;
        x->ex_flags |= EXFLAG_KUSAGE;
        ASN1_BIT_STRING_free(usage);
    }
    x->ex_xkusage = 0;
    if ((extusage = X509_get_ext_d2i(x, NID_ext_key_usage, NULL, NULL))) {
        x->ex_flags |= EXFLAG_XKUSAGE;
        for (i = 0; i < sk_ASN1_OBJECT_num(extusage); i++) {
            switch (OBJ_obj2nid(sk_ASN1_OBJECT_value(extusage, i))) {
            case NID_server_auth:
                x->ex_xkusage |= XKU_SSL_SERVER;
                break;

            case NID_client_auth:
                x->ex_xkusage |= XKU_SSL_CLIENT;
                break;

            case NID_email_protect:
                x->ex_xkusage |= XKU_SMIME;
                break;

            case NID_code_sign:
                x->ex_xkusage |= XKU_CODE_SIGN;
                break;

            case NID_ms_sgc:
            case NID_ns_sgc:
                x->ex_xkusage |= XKU_SGC;
                break;

            case NID_OCSP_sign:
                x->ex_xkusage |= XKU_OCSP_SIGN;
                break;

            case NID_time_stamp:
                x->ex_xkusage |= XKU_TIMESTAMP;
                break;

            case NID_dvcs:
                x->ex_xkusage |= XKU_DVCS;
                break;

            case NID_anyExtendedKeyUsage:
                x->ex_xkusage |= XKU_ANYEKU;
                break;
            }
        }
        sk_ASN1_OBJECT_pop_free(extusage, ASN1_OBJECT_free);
    }

    if ((ns = X509_get_ext_d2i(x, NID_netscape_cert_type, NULL, NULL))) {
        if (ns->length > 0)
            x->ex_nscert = ns->data[0];
        else
            x->ex_nscert = 0;
        x->ex_flags |= EXFLAG_NSCERT;
        ASN1_BIT_STRING_free(ns);
    }
    x->skid = X509_get_ext_d2i(x, NID_subject_key_identifier, NULL, NULL);
    x->akid = X509_get_ext_d2i(x, NID_authority_key_identifier, NULL, NULL);
    /* Does subject name match issuer ? */
    if (!X509_NAME_cmp(X509_get_subject_name(x), X509_get_issuer_name(x))) {
        x->ex_flags |= EXFLAG_SI;
        /* If SKID matches AKID also indicate self signed */
        if (X509_check_akid(x, x->akid) == X509_V_OK &&
            !ku_reject(x, KU_KEY_CERT_SIGN))
            x->ex_flags |= EXFLAG_SS;
    }
    x->altname = X509_get_ext_d2i(x, NID_subject_alt_name, NULL, NULL);
    x->nc = X509_get_ext_d2i(x, NID_name_constraints, &i, NULL);
    if (!x->nc && (i != -1))
        x->ex_flags |= EXFLAG_INVALID;
    setup_crldp(x);

#ifndef OPENSSL_NO_RFC3779
    x->rfc3779_addr = X509_get_ext_d2i(x, NID_sbgp_ipAddrBlock, NULL, NULL);
    x->rfc3779_asid = X509_get_ext_d2i(x, NID_sbgp_autonomousSysNum,
                                       NULL, NULL);
#endif
    for (i = 0; i < X509_get_ext_count(x); i++) {
        ex = X509_get_ext(x, i);
        if (OBJ_obj2nid(X509_EXTENSION_get_object(ex))
            == NID_freshest_crl)
            x->ex_flags |= EXFLAG_FRESHEST;
        if (!X509_EXTENSION_get_critical(ex))
            continue;
        if (!X509_supported_extension(ex)) {
            x->ex_flags |= EXFLAG_CRITICAL;
            break;
        }
    }
    x509_init_sig_info(x);
    x->ex_flags |= EXFLAG_SET;
#ifdef tsan_st_rel
    tsan_st_rel((TSAN_QUALIFIER int *)&x->ex_cached, 1);
    /*
     * Above store triggers fast lock-free check in the beginning of the
     * function. But one has to ensure that the structure is "stable", i.e.
     * all stores are visible on all processors. Hence the release fence.
     */
#endif
    CRYPTO_THREAD_unlock(x->lock);
}

/*-
 * CA checks common to all purposes
 * return codes:
 * 0 not a CA
 * 1 is a CA
 * 2 basicConstraints absent so "maybe" a CA
 * 3 basicConstraints absent but self signed V1.
 * 4 basicConstraints absent but keyUsage present and keyCertSign asserted.
 */

static int check_ca(const X509 *x)
{
    /* keyUsage if present should allow cert signing */
    if (ku_reject(x, KU_KEY_CERT_SIGN))
        return 0;
    if (x->ex_flags & EXFLAG_BCONS) {
        if (x->ex_flags & EXFLAG_CA)
            return 1;
        /* If basicConstraints says not a CA then say so */
        else
            return 0;
    } else {
        /* we support V1 roots for...  uh, I don't really know why. */
        if ((x->ex_flags & V1_ROOT) == V1_ROOT)
            return 3;
        /*
         * If key usage present it must have certSign so tolerate it
         */
        else if (x->ex_flags & EXFLAG_KUSAGE)
            return 4;
        /* Older certificates could have Netscape-specific CA types */
        else if (x->ex_flags & EXFLAG_NSCERT && x->ex_nscert & NS_ANY_CA)
            return 5;
        /* can this still be regarded a CA certificate?  I doubt it */
        return 0;
    }
}

void X509_set_proxy_flag(X509 *x)
{
    x->ex_flags |= EXFLAG_PROXY;
}

void X509_set_proxy_pathlen(X509 *x, long l)
{
    x->ex_pcpathlen = l;
}

int X509_check_ca(X509 *x)
{
    x509v3_cache_extensions(x);

    return check_ca(x);
}

/* Check SSL CA: common checks for SSL client and server */
static int check_ssl_ca(const X509 *x)
{
    int ca_ret;
    ca_ret = check_ca(x);
    if (!ca_ret)
        return 0;
    /* check nsCertType if present */
    if (ca_ret != 5 || x->ex_nscert & NS_SSL_CA)
        return ca_ret;
    else
        return 0;
}

static int check_purpose_ssl_client(const X509_PURPOSE *xp, const X509 *x,
                                    int ca)
{
    if (xku_reject(x, XKU_SSL_CLIENT))
        return 0;
    if (ca)
        return check_ssl_ca(x);
    /* We need to do digital signatures or key agreement */
    if (ku_reject(x, KU_DIGITAL_SIGNATURE | KU_KEY_AGREEMENT))
        return 0;
    /* nsCertType if present should allow SSL client use */
    if (ns_reject(x, NS_SSL_CLIENT))
        return 0;
    return 1;
}

/*
 * Key usage needed for TLS/SSL server: digital signature, encipherment or
 * key agreement. The ssl code can check this more thoroughly for individual
 * key types.
 */
#define KU_TLS \
        KU_DIGITAL_SIGNATURE|KU_KEY_ENCIPHERMENT|KU_KEY_AGREEMENT

static int check_purpose_ssl_server(const X509_PURPOSE *xp, const X509 *x,
                                    int ca)
{
    if (xku_reject(x, XKU_SSL_SERVER | XKU_SGC))
        return 0;
    if (ca)
        return check_ssl_ca(x);

    if (ns_reject(x, NS_SSL_SERVER))
        return 0;
    if (ku_reject(x, KU_TLS))
        return 0;

    return 1;

}

static int check_purpose_ns_ssl_server(const X509_PURPOSE *xp, const X509 *x,
                                       int ca)
{
    int ret;
    ret = check_purpose_ssl_server(xp, x, ca);
    if (!ret || ca)
        return ret;
    /* We need to encipher or Netscape complains */
    if (ku_reject(x, KU_KEY_ENCIPHERMENT))
        return 0;
    return ret;
}

/* common S/MIME checks */
static int purpose_smime(const X509 *x, int ca)
{
    if (xku_reject(x, XKU_SMIME))
        return 0;
    if (ca) {
        int ca_ret;
        ca_ret = check_ca(x);
        if (!ca_ret)
            return 0;
        /* check nsCertType if present */
        if (ca_ret != 5 || x->ex_nscert & NS_SMIME_CA)
            return ca_ret;
        else
            return 0;
    }
    if (x->ex_flags & EXFLAG_NSCERT) {
        if (x->ex_nscert & NS_SMIME)
            return 1;
        /* Workaround for some buggy certificates */
        if (x->ex_nscert & NS_SSL_CLIENT)
            return 2;
        return 0;
    }
    return 1;
}

static int check_purpose_smime_sign(const X509_PURPOSE *xp, const X509 *x,
                                    int ca)
{
    int ret;
    ret = purpose_smime(x, ca);
    if (!ret || ca)
        return ret;
    if (ku_reject(x, KU_DIGITAL_SIGNATURE | KU_NON_REPUDIATION))
        return 0;
    return ret;
}

static int check_purpose_smime_encrypt(const X509_PURPOSE *xp, const X509 *x,
                                       int ca)
{
    int ret;
    ret = purpose_smime(x, ca);
    if (!ret || ca)
        return ret;
    if (ku_reject(x, KU_KEY_ENCIPHERMENT))
        return 0;
    return ret;
}

static int check_purpose_crl_sign(const X509_PURPOSE *xp, const X509 *x,
                                  int ca)
{
    if (ca) {
        int ca_ret;
        if ((ca_ret = check_ca(x)) != 2)
            return ca_ret;
        else
            return 0;
    }
    if (ku_reject(x, KU_CRL_SIGN))
        return 0;
    return 1;
}

/*
 * OCSP helper: this is *not* a full OCSP check. It just checks that each CA
 * is valid. Additional checks must be made on the chain.
 */

static int ocsp_helper(const X509_PURPOSE *xp, const X509 *x, int ca)
{
    /*
     * Must be a valid CA.  Should we really support the "I don't know" value
     * (2)?
     */
    if (ca)
        return check_ca(x);
    /* leaf certificate is checked in OCSP_verify() */
    return 1;
}

static int check_purpose_timestamp_sign(const X509_PURPOSE *xp, const X509 *x,
                                        int ca)
{
    int i_ext;

    /* If ca is true we must return if this is a valid CA certificate. */
    if (ca)
        return check_ca(x);

    /*
     * Check the optional key usage field:
     * if Key Usage is present, it must be one of digitalSignature
     * and/or nonRepudiation (other values are not consistent and shall
     * be rejected).
     */
    if ((x->ex_flags & EXFLAG_KUSAGE)
        && ((x->ex_kusage & ~(KU_NON_REPUDIATION | KU_DIGITAL_SIGNATURE)) ||
            !(x->ex_kusage & (KU_NON_REPUDIATION | KU_DIGITAL_SIGNATURE))))
        return 0;

    /* Only time stamp key usage is permitted and it's required. */
    if (!(x->ex_flags & EXFLAG_XKUSAGE) || x->ex_xkusage != XKU_TIMESTAMP)
        return 0;

    /* Extended Key Usage MUST be critical */
    i_ext = X509_get_ext_by_NID(x, NID_ext_key_usage, -1);
    if (i_ext >= 0) {
        X509_EXTENSION *ext = X509_get_ext((X509 *)x, i_ext);
        if (!X509_EXTENSION_get_critical(ext))
            return 0;
    }

    return 1;
}

static int no_check(const X509_PURPOSE *xp, const X509 *x, int ca)
{
    return 1;
}

/*-
 * Various checks to see if one certificate issued the second.
 * This can be used to prune a set of possible issuer certificates
 * which have been looked up using some simple method such as by
 * subject name.
 * These are:
 * 1. Check issuer_name(subject) == subject_name(issuer)
 * 2. If akid(subject) exists check it matches issuer
 * 3. If key_usage(issuer) exists check it supports certificate signing
 * returns 0 for OK, positive for reason for mismatch, reasons match
 * codes for X509_verify_cert()
 */

int X509_check_issued(X509 *issuer, X509 *subject)
{
    if (X509_NAME_cmp(X509_get_subject_name(issuer),
                      X509_get_issuer_name(subject)))
        return X509_V_ERR_SUBJECT_ISSUER_MISMATCH;

    x509v3_cache_extensions(issuer);
    x509v3_cache_extensions(subject);

    if (subject->akid) {
        int ret = X509_check_akid(issuer, subject->akid);
        if (ret != X509_V_OK)
            return ret;
    }

    if (subject->ex_flags & EXFLAG_PROXY) {
        if (ku_reject(issuer, KU_DIGITAL_SIGNATURE))
            return X509_V_ERR_KEYUSAGE_NO_DIGITAL_SIGNATURE;
    } else if (ku_reject(issuer, KU_KEY_CERT_SIGN))
        return X509_V_ERR_KEYUSAGE_NO_CERTSIGN;
    return X509_V_OK;
}

int X509_check_akid(X509 *issuer, AUTHORITY_KEYID *akid)
{

    if (!akid)
        return X509_V_OK;

    /* Check key ids (if present) */
    if (akid->keyid && issuer->skid &&
        ASN1_OCTET_STRING_cmp(akid->keyid, issuer->skid))
        return X509_V_ERR_AKID_SKID_MISMATCH;
    /* Check serial number */
    if (akid->serial &&
        ASN1_INTEGER_cmp(X509_get_serialNumber(issuer), akid->serial))
        return X509_V_ERR_AKID_ISSUER_SERIAL_MISMATCH;
    /* Check issuer name */
    if (akid->issuer) {
        /*
         * Ugh, for some peculiar reason AKID includes SEQUENCE OF
         * GeneralName. So look for a DirName. There may be more than one but
         * we only take any notice of the first.
         */
        GENERAL_NAMES *gens;
        GENERAL_NAME *gen;
        X509_NAME *nm = NULL;
        int i;
        gens = akid->issuer;
        for (i = 0; i < sk_GENERAL_NAME_num(gens); i++) {
            gen = sk_GENERAL_NAME_value(gens, i);
            if (gen->type == GEN_DIRNAME) {
                nm = gen->d.dirn;
                break;
            }
        }
        if (nm && X509_NAME_cmp(nm, X509_get_issuer_name(issuer)))
            return X509_V_ERR_AKID_ISSUER_SERIAL_MISMATCH;
    }
    return X509_V_OK;
}

uint32_t X509_get_extension_flags(X509 *x)
{
    /* Call for side-effect of computing hash and caching extensions */
    X509_check_purpose(x, -1, -1);
    return x->ex_flags;
}

uint32_t X509_get_key_usage(X509 *x)
{
    /* Call for side-effect of computing hash and caching extensions */
    X509_check_purpose(x, -1, -1);
    if (x->ex_flags & EXFLAG_KUSAGE)
        return x->ex_kusage;
    return UINT32_MAX;
}

uint32_t X509_get_extended_key_usage(X509 *x)
{
    /* Call for side-effect of computing hash and caching extensions */
    X509_check_purpose(x, -1, -1);
    if (x->ex_flags & EXFLAG_XKUSAGE)
        return x->ex_xkusage;
    return UINT32_MAX;
}

const ASN1_OCTET_STRING *X509_get0_subject_key_id(X509 *x)
{
    /* Call for side-effect of computing hash and caching extensions */
    X509_check_purpose(x, -1, -1);
    return x->skid;
}

const ASN1_OCTET_STRING *X509_get0_authority_key_id(X509 *x)
{
    /* Call for side-effect of computing hash and caching extensions */
    X509_check_purpose(x, -1, -1);
    return (x->akid != NULL ? x->akid->keyid : NULL);
}

long X509_get_pathlen(X509 *x)
{
    /* Called for side effect of caching extensions */
    if (X509_check_purpose(x, -1, -1) != 1
            || (x->ex_flags & EXFLAG_BCONS) == 0)
        return -1;
    return x->ex_pathlen;
}

long X509_get_proxy_pathlen(X509 *x)
{
    /* Called for side effect of caching extensions */
    if (X509_check_purpose(x, -1, -1) != 1
            || (x->ex_flags & EXFLAG_PROXY) == 0)
        return -1;
    return x->ex_pcpathlen;
}
