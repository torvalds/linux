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
#include <openssl/conf.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/x509v3.h>

#include "pcy_int.h"
#include "ext_dat.h"

/* Certificate policies extension support: this one is a bit complex... */

static int i2r_certpol(X509V3_EXT_METHOD *method, STACK_OF(POLICYINFO) *pol,
                       BIO *out, int indent);
static STACK_OF(POLICYINFO) *r2i_certpol(X509V3_EXT_METHOD *method,
                                         X509V3_CTX *ctx, const char *value);
static void print_qualifiers(BIO *out, STACK_OF(POLICYQUALINFO) *quals,
                             int indent);
static void print_notice(BIO *out, USERNOTICE *notice, int indent);
static POLICYINFO *policy_section(X509V3_CTX *ctx,
                                  STACK_OF(CONF_VALUE) *polstrs, int ia5org);
static POLICYQUALINFO *notice_section(X509V3_CTX *ctx,
                                      STACK_OF(CONF_VALUE) *unot, int ia5org);
static int nref_nos(STACK_OF(ASN1_INTEGER) *nnums, STACK_OF(CONF_VALUE) *nos);
static int displaytext_str2tag(const char *tagstr, unsigned int *tag_len);
static int displaytext_get_tag_len(const char *tagstr);

const X509V3_EXT_METHOD v3_cpols = {
    NID_certificate_policies, 0, ASN1_ITEM_ref(CERTIFICATEPOLICIES),
    0, 0, 0, 0,
    0, 0,
    0, 0,
    (X509V3_EXT_I2R)i2r_certpol,
    (X509V3_EXT_R2I)r2i_certpol,
    NULL
};

ASN1_ITEM_TEMPLATE(CERTIFICATEPOLICIES) =
        ASN1_EX_TEMPLATE_TYPE(ASN1_TFLG_SEQUENCE_OF, 0, CERTIFICATEPOLICIES, POLICYINFO)
ASN1_ITEM_TEMPLATE_END(CERTIFICATEPOLICIES)

IMPLEMENT_ASN1_FUNCTIONS(CERTIFICATEPOLICIES)

ASN1_SEQUENCE(POLICYINFO) = {
        ASN1_SIMPLE(POLICYINFO, policyid, ASN1_OBJECT),
        ASN1_SEQUENCE_OF_OPT(POLICYINFO, qualifiers, POLICYQUALINFO)
} ASN1_SEQUENCE_END(POLICYINFO)

IMPLEMENT_ASN1_FUNCTIONS(POLICYINFO)

ASN1_ADB_TEMPLATE(policydefault) = ASN1_SIMPLE(POLICYQUALINFO, d.other, ASN1_ANY);

ASN1_ADB(POLICYQUALINFO) = {
        ADB_ENTRY(NID_id_qt_cps, ASN1_SIMPLE(POLICYQUALINFO, d.cpsuri, ASN1_IA5STRING)),
        ADB_ENTRY(NID_id_qt_unotice, ASN1_SIMPLE(POLICYQUALINFO, d.usernotice, USERNOTICE))
} ASN1_ADB_END(POLICYQUALINFO, 0, pqualid, 0, &policydefault_tt, NULL);

ASN1_SEQUENCE(POLICYQUALINFO) = {
        ASN1_SIMPLE(POLICYQUALINFO, pqualid, ASN1_OBJECT),
        ASN1_ADB_OBJECT(POLICYQUALINFO)
} ASN1_SEQUENCE_END(POLICYQUALINFO)

IMPLEMENT_ASN1_FUNCTIONS(POLICYQUALINFO)

ASN1_SEQUENCE(USERNOTICE) = {
        ASN1_OPT(USERNOTICE, noticeref, NOTICEREF),
        ASN1_OPT(USERNOTICE, exptext, DISPLAYTEXT)
} ASN1_SEQUENCE_END(USERNOTICE)

IMPLEMENT_ASN1_FUNCTIONS(USERNOTICE)

ASN1_SEQUENCE(NOTICEREF) = {
        ASN1_SIMPLE(NOTICEREF, organization, DISPLAYTEXT),
        ASN1_SEQUENCE_OF(NOTICEREF, noticenos, ASN1_INTEGER)
} ASN1_SEQUENCE_END(NOTICEREF)

IMPLEMENT_ASN1_FUNCTIONS(NOTICEREF)

static STACK_OF(POLICYINFO) *r2i_certpol(X509V3_EXT_METHOD *method,
                                         X509V3_CTX *ctx, const char *value)
{
    STACK_OF(POLICYINFO) *pols;
    char *pstr;
    POLICYINFO *pol;
    ASN1_OBJECT *pobj;
    STACK_OF(CONF_VALUE) *vals = X509V3_parse_list(value);
    CONF_VALUE *cnf;
    const int num = sk_CONF_VALUE_num(vals);
    int i, ia5org;

    if (vals == NULL) {
        X509V3err(X509V3_F_R2I_CERTPOL, ERR_R_X509V3_LIB);
        return NULL;
    }

    pols = sk_POLICYINFO_new_reserve(NULL, num);
    if (pols == NULL) {
        X509V3err(X509V3_F_R2I_CERTPOL, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    ia5org = 0;
    for (i = 0; i < num; i++) {
        cnf = sk_CONF_VALUE_value(vals, i);

        if (cnf->value || !cnf->name) {
            X509V3err(X509V3_F_R2I_CERTPOL,
                      X509V3_R_INVALID_POLICY_IDENTIFIER);
            X509V3_conf_err(cnf);
            goto err;
        }
        pstr = cnf->name;
        if (strcmp(pstr, "ia5org") == 0) {
            ia5org = 1;
            continue;
        } else if (*pstr == '@') {
            STACK_OF(CONF_VALUE) *polsect;
            polsect = X509V3_get_section(ctx, pstr + 1);
            if (!polsect) {
                X509V3err(X509V3_F_R2I_CERTPOL, X509V3_R_INVALID_SECTION);

                X509V3_conf_err(cnf);
                goto err;
            }
            pol = policy_section(ctx, polsect, ia5org);
            X509V3_section_free(ctx, polsect);
            if (pol == NULL)
                goto err;
        } else {
            if ((pobj = OBJ_txt2obj(cnf->name, 0)) == NULL) {
                X509V3err(X509V3_F_R2I_CERTPOL,
                          X509V3_R_INVALID_OBJECT_IDENTIFIER);
                X509V3_conf_err(cnf);
                goto err;
            }
            pol = POLICYINFO_new();
            if (pol == NULL) {
                ASN1_OBJECT_free(pobj);
                X509V3err(X509V3_F_R2I_CERTPOL, ERR_R_MALLOC_FAILURE);
                goto err;
            }
            pol->policyid = pobj;
        }
        if (!sk_POLICYINFO_push(pols, pol)) {
            POLICYINFO_free(pol);
            X509V3err(X509V3_F_R2I_CERTPOL, ERR_R_MALLOC_FAILURE);
            goto err;
        }
    }
    sk_CONF_VALUE_pop_free(vals, X509V3_conf_free);
    return pols;
 err:
    sk_CONF_VALUE_pop_free(vals, X509V3_conf_free);
    sk_POLICYINFO_pop_free(pols, POLICYINFO_free);
    return NULL;
}

static POLICYINFO *policy_section(X509V3_CTX *ctx,
                                  STACK_OF(CONF_VALUE) *polstrs, int ia5org)
{
    int i;
    CONF_VALUE *cnf;
    POLICYINFO *pol;
    POLICYQUALINFO *qual;

    if ((pol = POLICYINFO_new()) == NULL)
        goto merr;
    for (i = 0; i < sk_CONF_VALUE_num(polstrs); i++) {
        cnf = sk_CONF_VALUE_value(polstrs, i);
        if (strcmp(cnf->name, "policyIdentifier") == 0) {
            ASN1_OBJECT *pobj;
            if ((pobj = OBJ_txt2obj(cnf->value, 0)) == NULL) {
                X509V3err(X509V3_F_POLICY_SECTION,
                          X509V3_R_INVALID_OBJECT_IDENTIFIER);
                X509V3_conf_err(cnf);
                goto err;
            }
            pol->policyid = pobj;

        } else if (!name_cmp(cnf->name, "CPS")) {
            if (pol->qualifiers == NULL)
                pol->qualifiers = sk_POLICYQUALINFO_new_null();
            if ((qual = POLICYQUALINFO_new()) == NULL)
                goto merr;
            if (!sk_POLICYQUALINFO_push(pol->qualifiers, qual))
                goto merr;
            if ((qual->pqualid = OBJ_nid2obj(NID_id_qt_cps)) == NULL) {
                X509V3err(X509V3_F_POLICY_SECTION, ERR_R_INTERNAL_ERROR);
                goto err;
            }
            if ((qual->d.cpsuri = ASN1_IA5STRING_new()) == NULL)
                goto merr;
            if (!ASN1_STRING_set(qual->d.cpsuri, cnf->value,
                                 strlen(cnf->value)))
                goto merr;
        } else if (!name_cmp(cnf->name, "userNotice")) {
            STACK_OF(CONF_VALUE) *unot;
            if (*cnf->value != '@') {
                X509V3err(X509V3_F_POLICY_SECTION,
                          X509V3_R_EXPECTED_A_SECTION_NAME);
                X509V3_conf_err(cnf);
                goto err;
            }
            unot = X509V3_get_section(ctx, cnf->value + 1);
            if (!unot) {
                X509V3err(X509V3_F_POLICY_SECTION, X509V3_R_INVALID_SECTION);

                X509V3_conf_err(cnf);
                goto err;
            }
            qual = notice_section(ctx, unot, ia5org);
            X509V3_section_free(ctx, unot);
            if (!qual)
                goto err;
            if (!pol->qualifiers)
                pol->qualifiers = sk_POLICYQUALINFO_new_null();
            if (!sk_POLICYQUALINFO_push(pol->qualifiers, qual))
                goto merr;
        } else {
            X509V3err(X509V3_F_POLICY_SECTION, X509V3_R_INVALID_OPTION);

            X509V3_conf_err(cnf);
            goto err;
        }
    }
    if (!pol->policyid) {
        X509V3err(X509V3_F_POLICY_SECTION, X509V3_R_NO_POLICY_IDENTIFIER);
        goto err;
    }

    return pol;

 merr:
    X509V3err(X509V3_F_POLICY_SECTION, ERR_R_MALLOC_FAILURE);

 err:
    POLICYINFO_free(pol);
    return NULL;
}

static int displaytext_get_tag_len(const char *tagstr)
{
    char *colon = strchr(tagstr, ':');

    return (colon == NULL) ? -1 : colon - tagstr;
}

static int displaytext_str2tag(const char *tagstr, unsigned int *tag_len)
{
    int len;

    *tag_len = 0;
    len = displaytext_get_tag_len(tagstr);

    if (len == -1)
        return V_ASN1_VISIBLESTRING;
    *tag_len = len;
    if (len == sizeof("UTF8") - 1 && strncmp(tagstr, "UTF8", len) == 0)
        return V_ASN1_UTF8STRING;
    if (len == sizeof("UTF8String") - 1 && strncmp(tagstr, "UTF8String", len) == 0)
        return V_ASN1_UTF8STRING;
    if (len == sizeof("BMP") - 1 && strncmp(tagstr, "BMP", len) == 0)
        return V_ASN1_BMPSTRING;
    if (len == sizeof("BMPSTRING") - 1 && strncmp(tagstr, "BMPSTRING", len) == 0)
        return V_ASN1_BMPSTRING;
    if (len == sizeof("VISIBLE") - 1 && strncmp(tagstr, "VISIBLE", len) == 0)
        return V_ASN1_VISIBLESTRING;
    if (len == sizeof("VISIBLESTRING") - 1 && strncmp(tagstr, "VISIBLESTRING", len) == 0)
        return V_ASN1_VISIBLESTRING;
    *tag_len = 0;
    return V_ASN1_VISIBLESTRING;
}

static POLICYQUALINFO *notice_section(X509V3_CTX *ctx,
                                      STACK_OF(CONF_VALUE) *unot, int ia5org)
{
    int i, ret, len, tag;
    unsigned int tag_len;
    CONF_VALUE *cnf;
    USERNOTICE *not;
    POLICYQUALINFO *qual;
    char *value = NULL;

    if ((qual = POLICYQUALINFO_new()) == NULL)
        goto merr;
    if ((qual->pqualid = OBJ_nid2obj(NID_id_qt_unotice)) == NULL) {
        X509V3err(X509V3_F_NOTICE_SECTION, ERR_R_INTERNAL_ERROR);
        goto err;
    }
    if ((not = USERNOTICE_new()) == NULL)
        goto merr;
    qual->d.usernotice = not;
    for (i = 0; i < sk_CONF_VALUE_num(unot); i++) {
        cnf = sk_CONF_VALUE_value(unot, i);
        value = cnf->value;
        if (strcmp(cnf->name, "explicitText") == 0) {
            tag = displaytext_str2tag(value, &tag_len);
            if ((not->exptext = ASN1_STRING_type_new(tag)) == NULL)
                goto merr;
            if (tag_len != 0)
                value += tag_len + 1;
            len = strlen(value);
            if (!ASN1_STRING_set(not->exptext, value, len))
                goto merr;
        } else if (strcmp(cnf->name, "organization") == 0) {
            NOTICEREF *nref;
            if (!not->noticeref) {
                if ((nref = NOTICEREF_new()) == NULL)
                    goto merr;
                not->noticeref = nref;
            } else
                nref = not->noticeref;
            if (ia5org)
                nref->organization->type = V_ASN1_IA5STRING;
            else
                nref->organization->type = V_ASN1_VISIBLESTRING;
            if (!ASN1_STRING_set(nref->organization, cnf->value,
                                 strlen(cnf->value)))
                goto merr;
        } else if (strcmp(cnf->name, "noticeNumbers") == 0) {
            NOTICEREF *nref;
            STACK_OF(CONF_VALUE) *nos;
            if (!not->noticeref) {
                if ((nref = NOTICEREF_new()) == NULL)
                    goto merr;
                not->noticeref = nref;
            } else
                nref = not->noticeref;
            nos = X509V3_parse_list(cnf->value);
            if (!nos || !sk_CONF_VALUE_num(nos)) {
                X509V3err(X509V3_F_NOTICE_SECTION, X509V3_R_INVALID_NUMBERS);
                X509V3_conf_err(cnf);
                sk_CONF_VALUE_pop_free(nos, X509V3_conf_free);
                goto err;
            }
            ret = nref_nos(nref->noticenos, nos);
            sk_CONF_VALUE_pop_free(nos, X509V3_conf_free);
            if (!ret)
                goto err;
        } else {
            X509V3err(X509V3_F_NOTICE_SECTION, X509V3_R_INVALID_OPTION);
            X509V3_conf_err(cnf);
            goto err;
        }
    }

    if (not->noticeref &&
        (!not->noticeref->noticenos || !not->noticeref->organization)) {
        X509V3err(X509V3_F_NOTICE_SECTION,
                  X509V3_R_NEED_ORGANIZATION_AND_NUMBERS);
        goto err;
    }

    return qual;

 merr:
    X509V3err(X509V3_F_NOTICE_SECTION, ERR_R_MALLOC_FAILURE);

 err:
    POLICYQUALINFO_free(qual);
    return NULL;
}

static int nref_nos(STACK_OF(ASN1_INTEGER) *nnums, STACK_OF(CONF_VALUE) *nos)
{
    CONF_VALUE *cnf;
    ASN1_INTEGER *aint;

    int i;

    for (i = 0; i < sk_CONF_VALUE_num(nos); i++) {
        cnf = sk_CONF_VALUE_value(nos, i);
        if ((aint = s2i_ASN1_INTEGER(NULL, cnf->name)) == NULL) {
            X509V3err(X509V3_F_NREF_NOS, X509V3_R_INVALID_NUMBER);
            goto err;
        }
        if (!sk_ASN1_INTEGER_push(nnums, aint))
            goto merr;
    }
    return 1;

 merr:
    ASN1_INTEGER_free(aint);
    X509V3err(X509V3_F_NREF_NOS, ERR_R_MALLOC_FAILURE);

 err:
    return 0;
}

static int i2r_certpol(X509V3_EXT_METHOD *method, STACK_OF(POLICYINFO) *pol,
                       BIO *out, int indent)
{
    int i;
    POLICYINFO *pinfo;
    /* First print out the policy OIDs */
    for (i = 0; i < sk_POLICYINFO_num(pol); i++) {
        pinfo = sk_POLICYINFO_value(pol, i);
        BIO_printf(out, "%*sPolicy: ", indent, "");
        i2a_ASN1_OBJECT(out, pinfo->policyid);
        BIO_puts(out, "\n");
        if (pinfo->qualifiers)
            print_qualifiers(out, pinfo->qualifiers, indent + 2);
    }
    return 1;
}

static void print_qualifiers(BIO *out, STACK_OF(POLICYQUALINFO) *quals,
                             int indent)
{
    POLICYQUALINFO *qualinfo;
    int i;
    for (i = 0; i < sk_POLICYQUALINFO_num(quals); i++) {
        qualinfo = sk_POLICYQUALINFO_value(quals, i);
        switch (OBJ_obj2nid(qualinfo->pqualid)) {
        case NID_id_qt_cps:
            BIO_printf(out, "%*sCPS: %s\n", indent, "",
                       qualinfo->d.cpsuri->data);
            break;

        case NID_id_qt_unotice:
            BIO_printf(out, "%*sUser Notice:\n", indent, "");
            print_notice(out, qualinfo->d.usernotice, indent + 2);
            break;

        default:
            BIO_printf(out, "%*sUnknown Qualifier: ", indent + 2, "");

            i2a_ASN1_OBJECT(out, qualinfo->pqualid);
            BIO_puts(out, "\n");
            break;
        }
    }
}

static void print_notice(BIO *out, USERNOTICE *notice, int indent)
{
    int i;
    if (notice->noticeref) {
        NOTICEREF *ref;
        ref = notice->noticeref;
        BIO_printf(out, "%*sOrganization: %s\n", indent, "",
                   ref->organization->data);
        BIO_printf(out, "%*sNumber%s: ", indent, "",
                   sk_ASN1_INTEGER_num(ref->noticenos) > 1 ? "s" : "");
        for (i = 0; i < sk_ASN1_INTEGER_num(ref->noticenos); i++) {
            ASN1_INTEGER *num;
            char *tmp;
            num = sk_ASN1_INTEGER_value(ref->noticenos, i);
            if (i)
                BIO_puts(out, ", ");
            if (num == NULL)
                BIO_puts(out, "(null)");
            else {
                tmp = i2s_ASN1_INTEGER(NULL, num);
                if (tmp == NULL)
                    return;
                BIO_puts(out, tmp);
                OPENSSL_free(tmp);
            }
        }
        BIO_puts(out, "\n");
    }
    if (notice->exptext)
        BIO_printf(out, "%*sExplicit Text: %s\n", indent, "",
                   notice->exptext->data);
}

void X509_POLICY_NODE_print(BIO *out, X509_POLICY_NODE *node, int indent)
{
    const X509_POLICY_DATA *dat = node->data;

    BIO_printf(out, "%*sPolicy: ", indent, "");

    i2a_ASN1_OBJECT(out, dat->valid_policy);
    BIO_puts(out, "\n");
    BIO_printf(out, "%*s%s\n", indent + 2, "",
               node_data_critical(dat) ? "Critical" : "Non Critical");
    if (dat->qualifier_set)
        print_qualifiers(out, dat->qualifier_set, indent + 2);
    else
        BIO_printf(out, "%*sNo Qualifiers\n", indent + 2, "");
}
