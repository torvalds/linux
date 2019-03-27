/*
 * Copyright 1999-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/conf.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/x509v3.h>

#include "internal/x509_int.h"
#include "ext_dat.h"

static void *v2i_crld(const X509V3_EXT_METHOD *method,
                      X509V3_CTX *ctx, STACK_OF(CONF_VALUE) *nval);
static int i2r_crldp(const X509V3_EXT_METHOD *method, void *pcrldp, BIO *out,
                     int indent);

const X509V3_EXT_METHOD v3_crld = {
    NID_crl_distribution_points, 0, ASN1_ITEM_ref(CRL_DIST_POINTS),
    0, 0, 0, 0,
    0, 0,
    0,
    v2i_crld,
    i2r_crldp, 0,
    NULL
};

const X509V3_EXT_METHOD v3_freshest_crl = {
    NID_freshest_crl, 0, ASN1_ITEM_ref(CRL_DIST_POINTS),
    0, 0, 0, 0,
    0, 0,
    0,
    v2i_crld,
    i2r_crldp, 0,
    NULL
};

static STACK_OF(GENERAL_NAME) *gnames_from_sectname(X509V3_CTX *ctx,
                                                    char *sect)
{
    STACK_OF(CONF_VALUE) *gnsect;
    STACK_OF(GENERAL_NAME) *gens;
    if (*sect == '@')
        gnsect = X509V3_get_section(ctx, sect + 1);
    else
        gnsect = X509V3_parse_list(sect);
    if (!gnsect) {
        X509V3err(X509V3_F_GNAMES_FROM_SECTNAME, X509V3_R_SECTION_NOT_FOUND);
        return NULL;
    }
    gens = v2i_GENERAL_NAMES(NULL, ctx, gnsect);
    if (*sect == '@')
        X509V3_section_free(ctx, gnsect);
    else
        sk_CONF_VALUE_pop_free(gnsect, X509V3_conf_free);
    return gens;
}

static int set_dist_point_name(DIST_POINT_NAME **pdp, X509V3_CTX *ctx,
                               CONF_VALUE *cnf)
{
    STACK_OF(GENERAL_NAME) *fnm = NULL;
    STACK_OF(X509_NAME_ENTRY) *rnm = NULL;

    if (strncmp(cnf->name, "fullname", 9) == 0) {
        fnm = gnames_from_sectname(ctx, cnf->value);
        if (!fnm)
            goto err;
    } else if (strcmp(cnf->name, "relativename") == 0) {
        int ret;
        STACK_OF(CONF_VALUE) *dnsect;
        X509_NAME *nm;
        nm = X509_NAME_new();
        if (nm == NULL)
            return -1;
        dnsect = X509V3_get_section(ctx, cnf->value);
        if (!dnsect) {
            X509V3err(X509V3_F_SET_DIST_POINT_NAME,
                      X509V3_R_SECTION_NOT_FOUND);
            return -1;
        }
        ret = X509V3_NAME_from_section(nm, dnsect, MBSTRING_ASC);
        X509V3_section_free(ctx, dnsect);
        rnm = nm->entries;
        nm->entries = NULL;
        X509_NAME_free(nm);
        if (!ret || sk_X509_NAME_ENTRY_num(rnm) <= 0)
            goto err;
        /*
         * Since its a name fragment can't have more than one RDNSequence
         */
        if (sk_X509_NAME_ENTRY_value(rnm,
                                     sk_X509_NAME_ENTRY_num(rnm) - 1)->set) {
            X509V3err(X509V3_F_SET_DIST_POINT_NAME,
                      X509V3_R_INVALID_MULTIPLE_RDNS);
            goto err;
        }
    } else
        return 0;

    if (*pdp) {
        X509V3err(X509V3_F_SET_DIST_POINT_NAME,
                  X509V3_R_DISTPOINT_ALREADY_SET);
        goto err;
    }

    *pdp = DIST_POINT_NAME_new();
    if (*pdp == NULL)
        goto err;
    if (fnm) {
        (*pdp)->type = 0;
        (*pdp)->name.fullname = fnm;
    } else {
        (*pdp)->type = 1;
        (*pdp)->name.relativename = rnm;
    }

    return 1;

 err:
    sk_GENERAL_NAME_pop_free(fnm, GENERAL_NAME_free);
    sk_X509_NAME_ENTRY_pop_free(rnm, X509_NAME_ENTRY_free);
    return -1;
}

static const BIT_STRING_BITNAME reason_flags[] = {
    {0, "Unused", "unused"},
    {1, "Key Compromise", "keyCompromise"},
    {2, "CA Compromise", "CACompromise"},
    {3, "Affiliation Changed", "affiliationChanged"},
    {4, "Superseded", "superseded"},
    {5, "Cessation Of Operation", "cessationOfOperation"},
    {6, "Certificate Hold", "certificateHold"},
    {7, "Privilege Withdrawn", "privilegeWithdrawn"},
    {8, "AA Compromise", "AACompromise"},
    {-1, NULL, NULL}
};

static int set_reasons(ASN1_BIT_STRING **preas, char *value)
{
    STACK_OF(CONF_VALUE) *rsk = NULL;
    const BIT_STRING_BITNAME *pbn;
    const char *bnam;
    int i, ret = 0;
    rsk = X509V3_parse_list(value);
    if (rsk == NULL)
        return 0;
    if (*preas != NULL)
        goto err;
    for (i = 0; i < sk_CONF_VALUE_num(rsk); i++) {
        bnam = sk_CONF_VALUE_value(rsk, i)->name;
        if (*preas == NULL) {
            *preas = ASN1_BIT_STRING_new();
            if (*preas == NULL)
                goto err;
        }
        for (pbn = reason_flags; pbn->lname; pbn++) {
            if (strcmp(pbn->sname, bnam) == 0) {
                if (!ASN1_BIT_STRING_set_bit(*preas, pbn->bitnum, 1))
                    goto err;
                break;
            }
        }
        if (!pbn->lname)
            goto err;
    }
    ret = 1;

 err:
    sk_CONF_VALUE_pop_free(rsk, X509V3_conf_free);
    return ret;
}

static int print_reasons(BIO *out, const char *rname,
                         ASN1_BIT_STRING *rflags, int indent)
{
    int first = 1;
    const BIT_STRING_BITNAME *pbn;
    BIO_printf(out, "%*s%s:\n%*s", indent, "", rname, indent + 2, "");
    for (pbn = reason_flags; pbn->lname; pbn++) {
        if (ASN1_BIT_STRING_get_bit(rflags, pbn->bitnum)) {
            if (first)
                first = 0;
            else
                BIO_puts(out, ", ");
            BIO_puts(out, pbn->lname);
        }
    }
    if (first)
        BIO_puts(out, "<EMPTY>\n");
    else
        BIO_puts(out, "\n");
    return 1;
}

static DIST_POINT *crldp_from_section(X509V3_CTX *ctx,
                                      STACK_OF(CONF_VALUE) *nval)
{
    int i;
    CONF_VALUE *cnf;
    DIST_POINT *point = DIST_POINT_new();

    if (point == NULL)
        goto err;
    for (i = 0; i < sk_CONF_VALUE_num(nval); i++) {
        int ret;
        cnf = sk_CONF_VALUE_value(nval, i);
        ret = set_dist_point_name(&point->distpoint, ctx, cnf);
        if (ret > 0)
            continue;
        if (ret < 0)
            goto err;
        if (strcmp(cnf->name, "reasons") == 0) {
            if (!set_reasons(&point->reasons, cnf->value))
                goto err;
        } else if (strcmp(cnf->name, "CRLissuer") == 0) {
            point->CRLissuer = gnames_from_sectname(ctx, cnf->value);
            if (!point->CRLissuer)
                goto err;
        }
    }

    return point;

 err:
    DIST_POINT_free(point);
    return NULL;
}

static void *v2i_crld(const X509V3_EXT_METHOD *method,
                      X509V3_CTX *ctx, STACK_OF(CONF_VALUE) *nval)
{
    STACK_OF(DIST_POINT) *crld;
    GENERAL_NAMES *gens = NULL;
    GENERAL_NAME *gen = NULL;
    CONF_VALUE *cnf;
    const int num = sk_CONF_VALUE_num(nval);
    int i;

    crld = sk_DIST_POINT_new_reserve(NULL, num);
    if (crld == NULL)
        goto merr;
    for (i = 0; i < num; i++) {
        DIST_POINT *point;

        cnf = sk_CONF_VALUE_value(nval, i);
        if (!cnf->value) {
            STACK_OF(CONF_VALUE) *dpsect;
            dpsect = X509V3_get_section(ctx, cnf->name);
            if (!dpsect)
                goto err;
            point = crldp_from_section(ctx, dpsect);
            X509V3_section_free(ctx, dpsect);
            if (!point)
                goto err;
            sk_DIST_POINT_push(crld, point); /* no failure as it was reserved */
        } else {
            if ((gen = v2i_GENERAL_NAME(method, ctx, cnf)) == NULL)
                goto err;
            if ((gens = GENERAL_NAMES_new()) == NULL)
                goto merr;
            if (!sk_GENERAL_NAME_push(gens, gen))
                goto merr;
            gen = NULL;
            if ((point = DIST_POINT_new()) == NULL)
                goto merr;
            sk_DIST_POINT_push(crld, point); /* no failure as it was reserved */
            if ((point->distpoint = DIST_POINT_NAME_new()) == NULL)
                goto merr;
            point->distpoint->name.fullname = gens;
            point->distpoint->type = 0;
            gens = NULL;
        }
    }
    return crld;

 merr:
    X509V3err(X509V3_F_V2I_CRLD, ERR_R_MALLOC_FAILURE);
 err:
    GENERAL_NAME_free(gen);
    GENERAL_NAMES_free(gens);
    sk_DIST_POINT_pop_free(crld, DIST_POINT_free);
    return NULL;
}

static int dpn_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it,
                  void *exarg)
{
    DIST_POINT_NAME *dpn = (DIST_POINT_NAME *)*pval;

    switch (operation) {
    case ASN1_OP_NEW_POST:
        dpn->dpname = NULL;
        break;

    case ASN1_OP_FREE_POST:
        X509_NAME_free(dpn->dpname);
        break;
    }
    return 1;
}


ASN1_CHOICE_cb(DIST_POINT_NAME, dpn_cb) = {
        ASN1_IMP_SEQUENCE_OF(DIST_POINT_NAME, name.fullname, GENERAL_NAME, 0),
        ASN1_IMP_SET_OF(DIST_POINT_NAME, name.relativename, X509_NAME_ENTRY, 1)
} ASN1_CHOICE_END_cb(DIST_POINT_NAME, DIST_POINT_NAME, type)


IMPLEMENT_ASN1_FUNCTIONS(DIST_POINT_NAME)

ASN1_SEQUENCE(DIST_POINT) = {
        ASN1_EXP_OPT(DIST_POINT, distpoint, DIST_POINT_NAME, 0),
        ASN1_IMP_OPT(DIST_POINT, reasons, ASN1_BIT_STRING, 1),
        ASN1_IMP_SEQUENCE_OF_OPT(DIST_POINT, CRLissuer, GENERAL_NAME, 2)
} ASN1_SEQUENCE_END(DIST_POINT)

IMPLEMENT_ASN1_FUNCTIONS(DIST_POINT)

ASN1_ITEM_TEMPLATE(CRL_DIST_POINTS) =
        ASN1_EX_TEMPLATE_TYPE(ASN1_TFLG_SEQUENCE_OF, 0, CRLDistributionPoints, DIST_POINT)
ASN1_ITEM_TEMPLATE_END(CRL_DIST_POINTS)

IMPLEMENT_ASN1_FUNCTIONS(CRL_DIST_POINTS)

ASN1_SEQUENCE(ISSUING_DIST_POINT) = {
        ASN1_EXP_OPT(ISSUING_DIST_POINT, distpoint, DIST_POINT_NAME, 0),
        ASN1_IMP_OPT(ISSUING_DIST_POINT, onlyuser, ASN1_FBOOLEAN, 1),
        ASN1_IMP_OPT(ISSUING_DIST_POINT, onlyCA, ASN1_FBOOLEAN, 2),
        ASN1_IMP_OPT(ISSUING_DIST_POINT, onlysomereasons, ASN1_BIT_STRING, 3),
        ASN1_IMP_OPT(ISSUING_DIST_POINT, indirectCRL, ASN1_FBOOLEAN, 4),
        ASN1_IMP_OPT(ISSUING_DIST_POINT, onlyattr, ASN1_FBOOLEAN, 5)
} ASN1_SEQUENCE_END(ISSUING_DIST_POINT)

IMPLEMENT_ASN1_FUNCTIONS(ISSUING_DIST_POINT)

static int i2r_idp(const X509V3_EXT_METHOD *method, void *pidp, BIO *out,
                   int indent);
static void *v2i_idp(const X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
                     STACK_OF(CONF_VALUE) *nval);

const X509V3_EXT_METHOD v3_idp = {
    NID_issuing_distribution_point, X509V3_EXT_MULTILINE,
    ASN1_ITEM_ref(ISSUING_DIST_POINT),
    0, 0, 0, 0,
    0, 0,
    0,
    v2i_idp,
    i2r_idp, 0,
    NULL
};

static void *v2i_idp(const X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
                     STACK_OF(CONF_VALUE) *nval)
{
    ISSUING_DIST_POINT *idp = NULL;
    CONF_VALUE *cnf;
    char *name, *val;
    int i, ret;
    idp = ISSUING_DIST_POINT_new();
    if (idp == NULL)
        goto merr;
    for (i = 0; i < sk_CONF_VALUE_num(nval); i++) {
        cnf = sk_CONF_VALUE_value(nval, i);
        name = cnf->name;
        val = cnf->value;
        ret = set_dist_point_name(&idp->distpoint, ctx, cnf);
        if (ret > 0)
            continue;
        if (ret < 0)
            goto err;
        if (strcmp(name, "onlyuser") == 0) {
            if (!X509V3_get_value_bool(cnf, &idp->onlyuser))
                goto err;
        } else if (strcmp(name, "onlyCA") == 0) {
            if (!X509V3_get_value_bool(cnf, &idp->onlyCA))
                goto err;
        } else if (strcmp(name, "onlyAA") == 0) {
            if (!X509V3_get_value_bool(cnf, &idp->onlyattr))
                goto err;
        } else if (strcmp(name, "indirectCRL") == 0) {
            if (!X509V3_get_value_bool(cnf, &idp->indirectCRL))
                goto err;
        } else if (strcmp(name, "onlysomereasons") == 0) {
            if (!set_reasons(&idp->onlysomereasons, val))
                goto err;
        } else {
            X509V3err(X509V3_F_V2I_IDP, X509V3_R_INVALID_NAME);
            X509V3_conf_err(cnf);
            goto err;
        }
    }
    return idp;

 merr:
    X509V3err(X509V3_F_V2I_IDP, ERR_R_MALLOC_FAILURE);
 err:
    ISSUING_DIST_POINT_free(idp);
    return NULL;
}

static int print_gens(BIO *out, STACK_OF(GENERAL_NAME) *gens, int indent)
{
    int i;
    for (i = 0; i < sk_GENERAL_NAME_num(gens); i++) {
        BIO_printf(out, "%*s", indent + 2, "");
        GENERAL_NAME_print(out, sk_GENERAL_NAME_value(gens, i));
        BIO_puts(out, "\n");
    }
    return 1;
}

static int print_distpoint(BIO *out, DIST_POINT_NAME *dpn, int indent)
{
    if (dpn->type == 0) {
        BIO_printf(out, "%*sFull Name:\n", indent, "");
        print_gens(out, dpn->name.fullname, indent);
    } else {
        X509_NAME ntmp;
        ntmp.entries = dpn->name.relativename;
        BIO_printf(out, "%*sRelative Name:\n%*s", indent, "", indent + 2, "");
        X509_NAME_print_ex(out, &ntmp, 0, XN_FLAG_ONELINE);
        BIO_puts(out, "\n");
    }
    return 1;
}

static int i2r_idp(const X509V3_EXT_METHOD *method, void *pidp, BIO *out,
                   int indent)
{
    ISSUING_DIST_POINT *idp = pidp;
    if (idp->distpoint)
        print_distpoint(out, idp->distpoint, indent);
    if (idp->onlyuser > 0)
        BIO_printf(out, "%*sOnly User Certificates\n", indent, "");
    if (idp->onlyCA > 0)
        BIO_printf(out, "%*sOnly CA Certificates\n", indent, "");
    if (idp->indirectCRL > 0)
        BIO_printf(out, "%*sIndirect CRL\n", indent, "");
    if (idp->onlysomereasons)
        print_reasons(out, "Only Some Reasons", idp->onlysomereasons, indent);
    if (idp->onlyattr > 0)
        BIO_printf(out, "%*sOnly Attribute Certificates\n", indent, "");
    if (!idp->distpoint && (idp->onlyuser <= 0) && (idp->onlyCA <= 0)
        && (idp->indirectCRL <= 0) && !idp->onlysomereasons
        && (idp->onlyattr <= 0))
        BIO_printf(out, "%*s<EMPTY>\n", indent, "");

    return 1;
}

static int i2r_crldp(const X509V3_EXT_METHOD *method, void *pcrldp, BIO *out,
                     int indent)
{
    STACK_OF(DIST_POINT) *crld = pcrldp;
    DIST_POINT *point;
    int i;
    for (i = 0; i < sk_DIST_POINT_num(crld); i++) {
        BIO_puts(out, "\n");
        point = sk_DIST_POINT_value(crld, i);
        if (point->distpoint)
            print_distpoint(out, point->distpoint, indent);
        if (point->reasons)
            print_reasons(out, "Reasons", point->reasons, indent);
        if (point->CRLissuer) {
            BIO_printf(out, "%*sCRL Issuer:\n", indent, "");
            print_gens(out, point->CRLissuer, indent);
        }
    }
    return 1;
}

int DIST_POINT_set_dpname(DIST_POINT_NAME *dpn, X509_NAME *iname)
{
    int i;
    STACK_OF(X509_NAME_ENTRY) *frag;
    X509_NAME_ENTRY *ne;
    if (!dpn || (dpn->type != 1))
        return 1;
    frag = dpn->name.relativename;
    dpn->dpname = X509_NAME_dup(iname);
    if (!dpn->dpname)
        return 0;
    for (i = 0; i < sk_X509_NAME_ENTRY_num(frag); i++) {
        ne = sk_X509_NAME_ENTRY_value(frag, i);
        if (!X509_NAME_add_entry(dpn->dpname, ne, -1, i ? 0 : 1)) {
            X509_NAME_free(dpn->dpname);
            dpn->dpname = NULL;
            return 0;
        }
    }
    /* generate cached encoding of name */
    if (i2d_X509_NAME(dpn->dpname, NULL) < 0) {
        X509_NAME_free(dpn->dpname);
        dpn->dpname = NULL;
        return 0;
    }
    return 1;
}
