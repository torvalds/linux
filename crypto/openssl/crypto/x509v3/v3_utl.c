/*
 * Copyright 1999-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* X509 v3 extension utilities */

#include "e_os.h"
#include "internal/cryptlib.h"
#include <stdio.h>
#include "internal/ctype.h"
#include <openssl/conf.h>
#include <openssl/crypto.h>
#include <openssl/x509v3.h>
#include "internal/x509_int.h"
#include <openssl/bn.h>
#include "ext_dat.h"

static char *strip_spaces(char *name);
static int sk_strcmp(const char *const *a, const char *const *b);
static STACK_OF(OPENSSL_STRING) *get_email(X509_NAME *name,
                                           GENERAL_NAMES *gens);
static void str_free(OPENSSL_STRING str);
static int append_ia5(STACK_OF(OPENSSL_STRING) **sk, const ASN1_IA5STRING *email);

static int ipv4_from_asc(unsigned char *v4, const char *in);
static int ipv6_from_asc(unsigned char *v6, const char *in);
static int ipv6_cb(const char *elem, int len, void *usr);
static int ipv6_hex(unsigned char *out, const char *in, int inlen);

/* Add a CONF_VALUE name value pair to stack */

int X509V3_add_value(const char *name, const char *value,
                     STACK_OF(CONF_VALUE) **extlist)
{
    CONF_VALUE *vtmp = NULL;
    char *tname = NULL, *tvalue = NULL;
    int sk_allocated = (*extlist == NULL);

    if (name && (tname = OPENSSL_strdup(name)) == NULL)
        goto err;
    if (value && (tvalue = OPENSSL_strdup(value)) == NULL)
        goto err;
    if ((vtmp = OPENSSL_malloc(sizeof(*vtmp))) == NULL)
        goto err;
    if (sk_allocated && (*extlist = sk_CONF_VALUE_new_null()) == NULL)
        goto err;
    vtmp->section = NULL;
    vtmp->name = tname;
    vtmp->value = tvalue;
    if (!sk_CONF_VALUE_push(*extlist, vtmp))
        goto err;
    return 1;
 err:
    X509V3err(X509V3_F_X509V3_ADD_VALUE, ERR_R_MALLOC_FAILURE);
    if (sk_allocated) {
        sk_CONF_VALUE_free(*extlist);
        *extlist = NULL;
    }
    OPENSSL_free(vtmp);
    OPENSSL_free(tname);
    OPENSSL_free(tvalue);
    return 0;
}

int X509V3_add_value_uchar(const char *name, const unsigned char *value,
                           STACK_OF(CONF_VALUE) **extlist)
{
    return X509V3_add_value(name, (const char *)value, extlist);
}

/* Free function for STACK_OF(CONF_VALUE) */

void X509V3_conf_free(CONF_VALUE *conf)
{
    if (!conf)
        return;
    OPENSSL_free(conf->name);
    OPENSSL_free(conf->value);
    OPENSSL_free(conf->section);
    OPENSSL_free(conf);
}

int X509V3_add_value_bool(const char *name, int asn1_bool,
                          STACK_OF(CONF_VALUE) **extlist)
{
    if (asn1_bool)
        return X509V3_add_value(name, "TRUE", extlist);
    return X509V3_add_value(name, "FALSE", extlist);
}

int X509V3_add_value_bool_nf(const char *name, int asn1_bool,
                             STACK_OF(CONF_VALUE) **extlist)
{
    if (asn1_bool)
        return X509V3_add_value(name, "TRUE", extlist);
    return 1;
}

static char *bignum_to_string(const BIGNUM *bn)
{
    char *tmp, *ret;
    size_t len;

    /*
     * Display large numbers in hex and small numbers in decimal. Converting to
     * decimal takes quadratic time and is no more useful than hex for large
     * numbers.
     */
    if (BN_num_bits(bn) < 128)
        return BN_bn2dec(bn);

    tmp = BN_bn2hex(bn);
    if (tmp == NULL)
        return NULL;

    len = strlen(tmp) + 3;
    ret = OPENSSL_malloc(len);
    if (ret == NULL) {
        X509V3err(X509V3_F_BIGNUM_TO_STRING, ERR_R_MALLOC_FAILURE);
        OPENSSL_free(tmp);
        return NULL;
    }

    /* Prepend "0x", but place it after the "-" if negative. */
    if (tmp[0] == '-') {
        OPENSSL_strlcpy(ret, "-0x", len);
        OPENSSL_strlcat(ret, tmp + 1, len);
    } else {
        OPENSSL_strlcpy(ret, "0x", len);
        OPENSSL_strlcat(ret, tmp, len);
    }
    OPENSSL_free(tmp);
    return ret;
}

char *i2s_ASN1_ENUMERATED(X509V3_EXT_METHOD *method, const ASN1_ENUMERATED *a)
{
    BIGNUM *bntmp = NULL;
    char *strtmp = NULL;

    if (!a)
        return NULL;
    if ((bntmp = ASN1_ENUMERATED_to_BN(a, NULL)) == NULL
        || (strtmp = bignum_to_string(bntmp)) == NULL)
        X509V3err(X509V3_F_I2S_ASN1_ENUMERATED, ERR_R_MALLOC_FAILURE);
    BN_free(bntmp);
    return strtmp;
}

char *i2s_ASN1_INTEGER(X509V3_EXT_METHOD *method, const ASN1_INTEGER *a)
{
    BIGNUM *bntmp = NULL;
    char *strtmp = NULL;

    if (!a)
        return NULL;
    if ((bntmp = ASN1_INTEGER_to_BN(a, NULL)) == NULL
        || (strtmp = bignum_to_string(bntmp)) == NULL)
        X509V3err(X509V3_F_I2S_ASN1_INTEGER, ERR_R_MALLOC_FAILURE);
    BN_free(bntmp);
    return strtmp;
}

ASN1_INTEGER *s2i_ASN1_INTEGER(X509V3_EXT_METHOD *method, const char *value)
{
    BIGNUM *bn = NULL;
    ASN1_INTEGER *aint;
    int isneg, ishex;
    int ret;
    if (value == NULL) {
        X509V3err(X509V3_F_S2I_ASN1_INTEGER, X509V3_R_INVALID_NULL_VALUE);
        return NULL;
    }
    bn = BN_new();
    if (bn == NULL) {
        X509V3err(X509V3_F_S2I_ASN1_INTEGER, ERR_R_MALLOC_FAILURE);
        return NULL;
    }
    if (value[0] == '-') {
        value++;
        isneg = 1;
    } else
        isneg = 0;

    if (value[0] == '0' && ((value[1] == 'x') || (value[1] == 'X'))) {
        value += 2;
        ishex = 1;
    } else
        ishex = 0;

    if (ishex)
        ret = BN_hex2bn(&bn, value);
    else
        ret = BN_dec2bn(&bn, value);

    if (!ret || value[ret]) {
        BN_free(bn);
        X509V3err(X509V3_F_S2I_ASN1_INTEGER, X509V3_R_BN_DEC2BN_ERROR);
        return NULL;
    }

    if (isneg && BN_is_zero(bn))
        isneg = 0;

    aint = BN_to_ASN1_INTEGER(bn, NULL);
    BN_free(bn);
    if (!aint) {
        X509V3err(X509V3_F_S2I_ASN1_INTEGER,
                  X509V3_R_BN_TO_ASN1_INTEGER_ERROR);
        return NULL;
    }
    if (isneg)
        aint->type |= V_ASN1_NEG;
    return aint;
}

int X509V3_add_value_int(const char *name, const ASN1_INTEGER *aint,
                         STACK_OF(CONF_VALUE) **extlist)
{
    char *strtmp;
    int ret;

    if (!aint)
        return 1;
    if ((strtmp = i2s_ASN1_INTEGER(NULL, aint)) == NULL)
        return 0;
    ret = X509V3_add_value(name, strtmp, extlist);
    OPENSSL_free(strtmp);
    return ret;
}

int X509V3_get_value_bool(const CONF_VALUE *value, int *asn1_bool)
{
    const char *btmp;

    if ((btmp = value->value) == NULL)
        goto err;
    if (strcmp(btmp, "TRUE") == 0
        || strcmp(btmp, "true") == 0
        || strcmp(btmp, "Y") == 0
        || strcmp(btmp, "y") == 0
        || strcmp(btmp, "YES") == 0
        || strcmp(btmp, "yes") == 0) {
        *asn1_bool = 0xff;
        return 1;
    }
    if (strcmp(btmp, "FALSE") == 0
        || strcmp(btmp, "false") == 0
        || strcmp(btmp, "N") == 0
        || strcmp(btmp, "n") == 0
        || strcmp(btmp, "NO") == 0
        || strcmp(btmp, "no") == 0) {
        *asn1_bool = 0;
        return 1;
    }
 err:
    X509V3err(X509V3_F_X509V3_GET_VALUE_BOOL,
              X509V3_R_INVALID_BOOLEAN_STRING);
    X509V3_conf_err(value);
    return 0;
}

int X509V3_get_value_int(const CONF_VALUE *value, ASN1_INTEGER **aint)
{
    ASN1_INTEGER *itmp;

    if ((itmp = s2i_ASN1_INTEGER(NULL, value->value)) == NULL) {
        X509V3_conf_err(value);
        return 0;
    }
    *aint = itmp;
    return 1;
}

#define HDR_NAME        1
#define HDR_VALUE       2

/*
 * #define DEBUG
 */

STACK_OF(CONF_VALUE) *X509V3_parse_list(const char *line)
{
    char *p, *q, c;
    char *ntmp, *vtmp;
    STACK_OF(CONF_VALUE) *values = NULL;
    char *linebuf;
    int state;
    /* We are going to modify the line so copy it first */
    linebuf = OPENSSL_strdup(line);
    if (linebuf == NULL) {
        X509V3err(X509V3_F_X509V3_PARSE_LIST, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    state = HDR_NAME;
    ntmp = NULL;
    /* Go through all characters */
    for (p = linebuf, q = linebuf; (c = *p) && (c != '\r') && (c != '\n');
         p++) {

        switch (state) {
        case HDR_NAME:
            if (c == ':') {
                state = HDR_VALUE;
                *p = 0;
                ntmp = strip_spaces(q);
                if (!ntmp) {
                    X509V3err(X509V3_F_X509V3_PARSE_LIST,
                              X509V3_R_INVALID_NULL_NAME);
                    goto err;
                }
                q = p + 1;
            } else if (c == ',') {
                *p = 0;
                ntmp = strip_spaces(q);
                q = p + 1;
                if (!ntmp) {
                    X509V3err(X509V3_F_X509V3_PARSE_LIST,
                              X509V3_R_INVALID_NULL_NAME);
                    goto err;
                }
                X509V3_add_value(ntmp, NULL, &values);
            }
            break;

        case HDR_VALUE:
            if (c == ',') {
                state = HDR_NAME;
                *p = 0;
                vtmp = strip_spaces(q);
                if (!vtmp) {
                    X509V3err(X509V3_F_X509V3_PARSE_LIST,
                              X509V3_R_INVALID_NULL_VALUE);
                    goto err;
                }
                X509V3_add_value(ntmp, vtmp, &values);
                ntmp = NULL;
                q = p + 1;
            }

        }
    }

    if (state == HDR_VALUE) {
        vtmp = strip_spaces(q);
        if (!vtmp) {
            X509V3err(X509V3_F_X509V3_PARSE_LIST,
                      X509V3_R_INVALID_NULL_VALUE);
            goto err;
        }
        X509V3_add_value(ntmp, vtmp, &values);
    } else {
        ntmp = strip_spaces(q);
        if (!ntmp) {
            X509V3err(X509V3_F_X509V3_PARSE_LIST, X509V3_R_INVALID_NULL_NAME);
            goto err;
        }
        X509V3_add_value(ntmp, NULL, &values);
    }
    OPENSSL_free(linebuf);
    return values;

 err:
    OPENSSL_free(linebuf);
    sk_CONF_VALUE_pop_free(values, X509V3_conf_free);
    return NULL;

}

/* Delete leading and trailing spaces from a string */
static char *strip_spaces(char *name)
{
    char *p, *q;
    /* Skip over leading spaces */
    p = name;
    while (*p && ossl_isspace(*p))
        p++;
    if (!*p)
        return NULL;
    q = p + strlen(p) - 1;
    while ((q != p) && ossl_isspace(*q))
        q--;
    if (p != q)
        q[1] = 0;
    if (!*p)
        return NULL;
    return p;
}


/*
 * V2I name comparison function: returns zero if 'name' matches cmp or cmp.*
 */

int name_cmp(const char *name, const char *cmp)
{
    int len, ret;
    char c;
    len = strlen(cmp);
    if ((ret = strncmp(name, cmp, len)))
        return ret;
    c = name[len];
    if (!c || (c == '.'))
        return 0;
    return 1;
}

static int sk_strcmp(const char *const *a, const char *const *b)
{
    return strcmp(*a, *b);
}

STACK_OF(OPENSSL_STRING) *X509_get1_email(X509 *x)
{
    GENERAL_NAMES *gens;
    STACK_OF(OPENSSL_STRING) *ret;

    gens = X509_get_ext_d2i(x, NID_subject_alt_name, NULL, NULL);
    ret = get_email(X509_get_subject_name(x), gens);
    sk_GENERAL_NAME_pop_free(gens, GENERAL_NAME_free);
    return ret;
}

STACK_OF(OPENSSL_STRING) *X509_get1_ocsp(X509 *x)
{
    AUTHORITY_INFO_ACCESS *info;
    STACK_OF(OPENSSL_STRING) *ret = NULL;
    int i;

    info = X509_get_ext_d2i(x, NID_info_access, NULL, NULL);
    if (!info)
        return NULL;
    for (i = 0; i < sk_ACCESS_DESCRIPTION_num(info); i++) {
        ACCESS_DESCRIPTION *ad = sk_ACCESS_DESCRIPTION_value(info, i);
        if (OBJ_obj2nid(ad->method) == NID_ad_OCSP) {
            if (ad->location->type == GEN_URI) {
                if (!append_ia5
                    (&ret, ad->location->d.uniformResourceIdentifier))
                    break;
            }
        }
    }
    AUTHORITY_INFO_ACCESS_free(info);
    return ret;
}

STACK_OF(OPENSSL_STRING) *X509_REQ_get1_email(X509_REQ *x)
{
    GENERAL_NAMES *gens;
    STACK_OF(X509_EXTENSION) *exts;
    STACK_OF(OPENSSL_STRING) *ret;

    exts = X509_REQ_get_extensions(x);
    gens = X509V3_get_d2i(exts, NID_subject_alt_name, NULL, NULL);
    ret = get_email(X509_REQ_get_subject_name(x), gens);
    sk_GENERAL_NAME_pop_free(gens, GENERAL_NAME_free);
    sk_X509_EXTENSION_pop_free(exts, X509_EXTENSION_free);
    return ret;
}

static STACK_OF(OPENSSL_STRING) *get_email(X509_NAME *name,
                                           GENERAL_NAMES *gens)
{
    STACK_OF(OPENSSL_STRING) *ret = NULL;
    X509_NAME_ENTRY *ne;
    const ASN1_IA5STRING *email;
    GENERAL_NAME *gen;
    int i = -1;

    /* Now add any email address(es) to STACK */
    /* First supplied X509_NAME */
    while ((i = X509_NAME_get_index_by_NID(name,
                                           NID_pkcs9_emailAddress, i)) >= 0) {
        ne = X509_NAME_get_entry(name, i);
        email = X509_NAME_ENTRY_get_data(ne);
        if (!append_ia5(&ret, email))
            return NULL;
    }
    for (i = 0; i < sk_GENERAL_NAME_num(gens); i++) {
        gen = sk_GENERAL_NAME_value(gens, i);
        if (gen->type != GEN_EMAIL)
            continue;
        if (!append_ia5(&ret, gen->d.ia5))
            return NULL;
    }
    return ret;
}

static void str_free(OPENSSL_STRING str)
{
    OPENSSL_free(str);
}

static int append_ia5(STACK_OF(OPENSSL_STRING) **sk, const ASN1_IA5STRING *email)
{
    char *emtmp;
    /* First some sanity checks */
    if (email->type != V_ASN1_IA5STRING)
        return 1;
    if (!email->data || !email->length)
        return 1;
    if (*sk == NULL)
        *sk = sk_OPENSSL_STRING_new(sk_strcmp);
    if (*sk == NULL)
        return 0;
    /* Don't add duplicates */
    if (sk_OPENSSL_STRING_find(*sk, (char *)email->data) != -1)
        return 1;
    emtmp = OPENSSL_strdup((char *)email->data);
    if (emtmp == NULL || !sk_OPENSSL_STRING_push(*sk, emtmp)) {
        OPENSSL_free(emtmp);    /* free on push failure */
        X509_email_free(*sk);
        *sk = NULL;
        return 0;
    }
    return 1;
}

void X509_email_free(STACK_OF(OPENSSL_STRING) *sk)
{
    sk_OPENSSL_STRING_pop_free(sk, str_free);
}

typedef int (*equal_fn) (const unsigned char *pattern, size_t pattern_len,
                         const unsigned char *subject, size_t subject_len,
                         unsigned int flags);

/* Skip pattern prefix to match "wildcard" subject */
static void skip_prefix(const unsigned char **p, size_t *plen,
                        size_t subject_len,
                        unsigned int flags)
{
    const unsigned char *pattern = *p;
    size_t pattern_len = *plen;

    /*
     * If subject starts with a leading '.' followed by more octets, and
     * pattern is longer, compare just an equal-length suffix with the
     * full subject (starting at the '.'), provided the prefix contains
     * no NULs.
     */
    if ((flags & _X509_CHECK_FLAG_DOT_SUBDOMAINS) == 0)
        return;

    while (pattern_len > subject_len && *pattern) {
        if ((flags & X509_CHECK_FLAG_SINGLE_LABEL_SUBDOMAINS) &&
            *pattern == '.')
            break;
        ++pattern;
        --pattern_len;
    }

    /* Skip if entire prefix acceptable */
    if (pattern_len == subject_len) {
        *p = pattern;
        *plen = pattern_len;
    }
}

/* Compare while ASCII ignoring case. */
static int equal_nocase(const unsigned char *pattern, size_t pattern_len,
                        const unsigned char *subject, size_t subject_len,
                        unsigned int flags)
{
    skip_prefix(&pattern, &pattern_len, subject_len, flags);
    if (pattern_len != subject_len)
        return 0;
    while (pattern_len) {
        unsigned char l = *pattern;
        unsigned char r = *subject;
        /* The pattern must not contain NUL characters. */
        if (l == 0)
            return 0;
        if (l != r) {
            if ('A' <= l && l <= 'Z')
                l = (l - 'A') + 'a';
            if ('A' <= r && r <= 'Z')
                r = (r - 'A') + 'a';
            if (l != r)
                return 0;
        }
        ++pattern;
        ++subject;
        --pattern_len;
    }
    return 1;
}

/* Compare using memcmp. */
static int equal_case(const unsigned char *pattern, size_t pattern_len,
                      const unsigned char *subject, size_t subject_len,
                      unsigned int flags)
{
    skip_prefix(&pattern, &pattern_len, subject_len, flags);
    if (pattern_len != subject_len)
        return 0;
    return !memcmp(pattern, subject, pattern_len);
}

/*
 * RFC 5280, section 7.5, requires that only the domain is compared in a
 * case-insensitive manner.
 */
static int equal_email(const unsigned char *a, size_t a_len,
                       const unsigned char *b, size_t b_len,
                       unsigned int unused_flags)
{
    size_t i = a_len;
    if (a_len != b_len)
        return 0;
    /*
     * We search backwards for the '@' character, so that we do not have to
     * deal with quoted local-parts.  The domain part is compared in a
     * case-insensitive manner.
     */
    while (i > 0) {
        --i;
        if (a[i] == '@' || b[i] == '@') {
            if (!equal_nocase(a + i, a_len - i, b + i, a_len - i, 0))
                return 0;
            break;
        }
    }
    if (i == 0)
        i = a_len;
    return equal_case(a, i, b, i, 0);
}

/*
 * Compare the prefix and suffix with the subject, and check that the
 * characters in-between are valid.
 */
static int wildcard_match(const unsigned char *prefix, size_t prefix_len,
                          const unsigned char *suffix, size_t suffix_len,
                          const unsigned char *subject, size_t subject_len,
                          unsigned int flags)
{
    const unsigned char *wildcard_start;
    const unsigned char *wildcard_end;
    const unsigned char *p;
    int allow_multi = 0;
    int allow_idna = 0;

    if (subject_len < prefix_len + suffix_len)
        return 0;
    if (!equal_nocase(prefix, prefix_len, subject, prefix_len, flags))
        return 0;
    wildcard_start = subject + prefix_len;
    wildcard_end = subject + (subject_len - suffix_len);
    if (!equal_nocase(wildcard_end, suffix_len, suffix, suffix_len, flags))
        return 0;
    /*
     * If the wildcard makes up the entire first label, it must match at
     * least one character.
     */
    if (prefix_len == 0 && *suffix == '.') {
        if (wildcard_start == wildcard_end)
            return 0;
        allow_idna = 1;
        if (flags & X509_CHECK_FLAG_MULTI_LABEL_WILDCARDS)
            allow_multi = 1;
    }
    /* IDNA labels cannot match partial wildcards */
    if (!allow_idna &&
        subject_len >= 4 && strncasecmp((char *)subject, "xn--", 4) == 0)
        return 0;
    /* The wildcard may match a literal '*' */
    if (wildcard_end == wildcard_start + 1 && *wildcard_start == '*')
        return 1;
    /*
     * Check that the part matched by the wildcard contains only
     * permitted characters and only matches a single label unless
     * allow_multi is set.
     */
    for (p = wildcard_start; p != wildcard_end; ++p)
        if (!(('0' <= *p && *p <= '9') ||
              ('A' <= *p && *p <= 'Z') ||
              ('a' <= *p && *p <= 'z') ||
              *p == '-' || (allow_multi && *p == '.')))
            return 0;
    return 1;
}

#define LABEL_START     (1 << 0)
#define LABEL_END       (1 << 1)
#define LABEL_HYPHEN    (1 << 2)
#define LABEL_IDNA      (1 << 3)

static const unsigned char *valid_star(const unsigned char *p, size_t len,
                                       unsigned int flags)
{
    const unsigned char *star = 0;
    size_t i;
    int state = LABEL_START;
    int dots = 0;
    for (i = 0; i < len; ++i) {
        /*
         * Locate first and only legal wildcard, either at the start
         * or end of a non-IDNA first and not final label.
         */
        if (p[i] == '*') {
            int atstart = (state & LABEL_START);
            int atend = (i == len - 1 || p[i + 1] == '.');
            /*-
             * At most one wildcard per pattern.
             * No wildcards in IDNA labels.
             * No wildcards after the first label.
             */
            if (star != NULL || (state & LABEL_IDNA) != 0 || dots)
                return NULL;
            /* Only full-label '*.example.com' wildcards? */
            if ((flags & X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS)
                && (!atstart || !atend))
                return NULL;
            /* No 'foo*bar' wildcards */
            if (!atstart && !atend)
                return NULL;
            star = &p[i];
            state &= ~LABEL_START;
        } else if (('a' <= p[i] && p[i] <= 'z')
                   || ('A' <= p[i] && p[i] <= 'Z')
                   || ('0' <= p[i] && p[i] <= '9')) {
            if ((state & LABEL_START) != 0
                && len - i >= 4 && strncasecmp((char *)&p[i], "xn--", 4) == 0)
                state |= LABEL_IDNA;
            state &= ~(LABEL_HYPHEN | LABEL_START);
        } else if (p[i] == '.') {
            if ((state & (LABEL_HYPHEN | LABEL_START)) != 0)
                return NULL;
            state = LABEL_START;
            ++dots;
        } else if (p[i] == '-') {
            /* no domain/subdomain starts with '-' */
            if ((state & LABEL_START) != 0)
                return NULL;
            state |= LABEL_HYPHEN;
        } else
            return NULL;
    }

    /*
     * The final label must not end in a hyphen or ".", and
     * there must be at least two dots after the star.
     */
    if ((state & (LABEL_START | LABEL_HYPHEN)) != 0 || dots < 2)
        return NULL;
    return star;
}

/* Compare using wildcards. */
static int equal_wildcard(const unsigned char *pattern, size_t pattern_len,
                          const unsigned char *subject, size_t subject_len,
                          unsigned int flags)
{
    const unsigned char *star = NULL;

    /*
     * Subject names starting with '.' can only match a wildcard pattern
     * via a subject sub-domain pattern suffix match.
     */
    if (!(subject_len > 1 && subject[0] == '.'))
        star = valid_star(pattern, pattern_len, flags);
    if (star == NULL)
        return equal_nocase(pattern, pattern_len,
                            subject, subject_len, flags);
    return wildcard_match(pattern, star - pattern,
                          star + 1, (pattern + pattern_len) - star - 1,
                          subject, subject_len, flags);
}

/*
 * Compare an ASN1_STRING to a supplied string. If they match return 1. If
 * cmp_type > 0 only compare if string matches the type, otherwise convert it
 * to UTF8.
 */

static int do_check_string(const ASN1_STRING *a, int cmp_type, equal_fn equal,
                           unsigned int flags, const char *b, size_t blen,
                           char **peername)
{
    int rv = 0;

    if (!a->data || !a->length)
        return 0;
    if (cmp_type > 0) {
        if (cmp_type != a->type)
            return 0;
        if (cmp_type == V_ASN1_IA5STRING)
            rv = equal(a->data, a->length, (unsigned char *)b, blen, flags);
        else if (a->length == (int)blen && !memcmp(a->data, b, blen))
            rv = 1;
        if (rv > 0 && peername)
            *peername = OPENSSL_strndup((char *)a->data, a->length);
    } else {
        int astrlen;
        unsigned char *astr;
        astrlen = ASN1_STRING_to_UTF8(&astr, a);
        if (astrlen < 0) {
            /*
             * -1 could be an internal malloc failure or a decoding error from
             * malformed input; we can't distinguish.
             */
            return -1;
        }
        rv = equal(astr, astrlen, (unsigned char *)b, blen, flags);
        if (rv > 0 && peername)
            *peername = OPENSSL_strndup((char *)astr, astrlen);
        OPENSSL_free(astr);
    }
    return rv;
}

static int do_x509_check(X509 *x, const char *chk, size_t chklen,
                         unsigned int flags, int check_type, char **peername)
{
    GENERAL_NAMES *gens = NULL;
    X509_NAME *name = NULL;
    int i;
    int cnid = NID_undef;
    int alt_type;
    int san_present = 0;
    int rv = 0;
    equal_fn equal;

    /* See below, this flag is internal-only */
    flags &= ~_X509_CHECK_FLAG_DOT_SUBDOMAINS;
    if (check_type == GEN_EMAIL) {
        cnid = NID_pkcs9_emailAddress;
        alt_type = V_ASN1_IA5STRING;
        equal = equal_email;
    } else if (check_type == GEN_DNS) {
        cnid = NID_commonName;
        /* Implicit client-side DNS sub-domain pattern */
        if (chklen > 1 && chk[0] == '.')
            flags |= _X509_CHECK_FLAG_DOT_SUBDOMAINS;
        alt_type = V_ASN1_IA5STRING;
        if (flags & X509_CHECK_FLAG_NO_WILDCARDS)
            equal = equal_nocase;
        else
            equal = equal_wildcard;
    } else {
        alt_type = V_ASN1_OCTET_STRING;
        equal = equal_case;
    }

    if (chklen == 0)
        chklen = strlen(chk);

    gens = X509_get_ext_d2i(x, NID_subject_alt_name, NULL, NULL);
    if (gens) {
        for (i = 0; i < sk_GENERAL_NAME_num(gens); i++) {
            GENERAL_NAME *gen;
            ASN1_STRING *cstr;
            gen = sk_GENERAL_NAME_value(gens, i);
            if (gen->type != check_type)
                continue;
            san_present = 1;
            if (check_type == GEN_EMAIL)
                cstr = gen->d.rfc822Name;
            else if (check_type == GEN_DNS)
                cstr = gen->d.dNSName;
            else
                cstr = gen->d.iPAddress;
            /* Positive on success, negative on error! */
            if ((rv = do_check_string(cstr, alt_type, equal, flags,
                                      chk, chklen, peername)) != 0)
                break;
        }
        GENERAL_NAMES_free(gens);
        if (rv != 0)
            return rv;
        if (san_present && !(flags & X509_CHECK_FLAG_ALWAYS_CHECK_SUBJECT))
            return 0;
    }

    /* We're done if CN-ID is not pertinent */
    if (cnid == NID_undef || (flags & X509_CHECK_FLAG_NEVER_CHECK_SUBJECT))
        return 0;

    i = -1;
    name = X509_get_subject_name(x);
    while ((i = X509_NAME_get_index_by_NID(name, cnid, i)) >= 0) {
        const X509_NAME_ENTRY *ne = X509_NAME_get_entry(name, i);
        const ASN1_STRING *str = X509_NAME_ENTRY_get_data(ne);

        /* Positive on success, negative on error! */
        if ((rv = do_check_string(str, -1, equal, flags,
                                  chk, chklen, peername)) != 0)
            return rv;
    }
    return 0;
}

int X509_check_host(X509 *x, const char *chk, size_t chklen,
                    unsigned int flags, char **peername)
{
    if (chk == NULL)
        return -2;
    /*
     * Embedded NULs are disallowed, except as the last character of a
     * string of length 2 or more (tolerate caller including terminating
     * NUL in string length).
     */
    if (chklen == 0)
        chklen = strlen(chk);
    else if (memchr(chk, '\0', chklen > 1 ? chklen - 1 : chklen))
        return -2;
    if (chklen > 1 && chk[chklen - 1] == '\0')
        --chklen;
    return do_x509_check(x, chk, chklen, flags, GEN_DNS, peername);
}

int X509_check_email(X509 *x, const char *chk, size_t chklen,
                     unsigned int flags)
{
    if (chk == NULL)
        return -2;
    /*
     * Embedded NULs are disallowed, except as the last character of a
     * string of length 2 or more (tolerate caller including terminating
     * NUL in string length).
     */
    if (chklen == 0)
        chklen = strlen((char *)chk);
    else if (memchr(chk, '\0', chklen > 1 ? chklen - 1 : chklen))
        return -2;
    if (chklen > 1 && chk[chklen - 1] == '\0')
        --chklen;
    return do_x509_check(x, chk, chklen, flags, GEN_EMAIL, NULL);
}

int X509_check_ip(X509 *x, const unsigned char *chk, size_t chklen,
                  unsigned int flags)
{
    if (chk == NULL)
        return -2;
    return do_x509_check(x, (char *)chk, chklen, flags, GEN_IPADD, NULL);
}

int X509_check_ip_asc(X509 *x, const char *ipasc, unsigned int flags)
{
    unsigned char ipout[16];
    size_t iplen;

    if (ipasc == NULL)
        return -2;
    iplen = (size_t)a2i_ipadd(ipout, ipasc);
    if (iplen == 0)
        return -2;
    return do_x509_check(x, (char *)ipout, iplen, flags, GEN_IPADD, NULL);
}

/*
 * Convert IP addresses both IPv4 and IPv6 into an OCTET STRING compatible
 * with RFC3280.
 */

ASN1_OCTET_STRING *a2i_IPADDRESS(const char *ipasc)
{
    unsigned char ipout[16];
    ASN1_OCTET_STRING *ret;
    int iplen;

    /* If string contains a ':' assume IPv6 */

    iplen = a2i_ipadd(ipout, ipasc);

    if (!iplen)
        return NULL;

    ret = ASN1_OCTET_STRING_new();
    if (ret == NULL)
        return NULL;
    if (!ASN1_OCTET_STRING_set(ret, ipout, iplen)) {
        ASN1_OCTET_STRING_free(ret);
        return NULL;
    }
    return ret;
}

ASN1_OCTET_STRING *a2i_IPADDRESS_NC(const char *ipasc)
{
    ASN1_OCTET_STRING *ret = NULL;
    unsigned char ipout[32];
    char *iptmp = NULL, *p;
    int iplen1, iplen2;
    p = strchr(ipasc, '/');
    if (!p)
        return NULL;
    iptmp = OPENSSL_strdup(ipasc);
    if (!iptmp)
        return NULL;
    p = iptmp + (p - ipasc);
    *p++ = 0;

    iplen1 = a2i_ipadd(ipout, iptmp);

    if (!iplen1)
        goto err;

    iplen2 = a2i_ipadd(ipout + iplen1, p);

    OPENSSL_free(iptmp);
    iptmp = NULL;

    if (!iplen2 || (iplen1 != iplen2))
        goto err;

    ret = ASN1_OCTET_STRING_new();
    if (ret == NULL)
        goto err;
    if (!ASN1_OCTET_STRING_set(ret, ipout, iplen1 + iplen2))
        goto err;

    return ret;

 err:
    OPENSSL_free(iptmp);
    ASN1_OCTET_STRING_free(ret);
    return NULL;
}

int a2i_ipadd(unsigned char *ipout, const char *ipasc)
{
    /* If string contains a ':' assume IPv6 */

    if (strchr(ipasc, ':')) {
        if (!ipv6_from_asc(ipout, ipasc))
            return 0;
        return 16;
    } else {
        if (!ipv4_from_asc(ipout, ipasc))
            return 0;
        return 4;
    }
}

static int ipv4_from_asc(unsigned char *v4, const char *in)
{
    int a0, a1, a2, a3;
    if (sscanf(in, "%d.%d.%d.%d", &a0, &a1, &a2, &a3) != 4)
        return 0;
    if ((a0 < 0) || (a0 > 255) || (a1 < 0) || (a1 > 255)
        || (a2 < 0) || (a2 > 255) || (a3 < 0) || (a3 > 255))
        return 0;
    v4[0] = a0;
    v4[1] = a1;
    v4[2] = a2;
    v4[3] = a3;
    return 1;
}

typedef struct {
    /* Temporary store for IPV6 output */
    unsigned char tmp[16];
    /* Total number of bytes in tmp */
    int total;
    /* The position of a zero (corresponding to '::') */
    int zero_pos;
    /* Number of zeroes */
    int zero_cnt;
} IPV6_STAT;

static int ipv6_from_asc(unsigned char *v6, const char *in)
{
    IPV6_STAT v6stat;
    v6stat.total = 0;
    v6stat.zero_pos = -1;
    v6stat.zero_cnt = 0;
    /*
     * Treat the IPv6 representation as a list of values separated by ':'.
     * The presence of a '::' will parse as one, two or three zero length
     * elements.
     */
    if (!CONF_parse_list(in, ':', 0, ipv6_cb, &v6stat))
        return 0;

    /* Now for some sanity checks */

    if (v6stat.zero_pos == -1) {
        /* If no '::' must have exactly 16 bytes */
        if (v6stat.total != 16)
            return 0;
    } else {
        /* If '::' must have less than 16 bytes */
        if (v6stat.total == 16)
            return 0;
        /* More than three zeroes is an error */
        if (v6stat.zero_cnt > 3)
            return 0;
        /* Can only have three zeroes if nothing else present */
        else if (v6stat.zero_cnt == 3) {
            if (v6stat.total > 0)
                return 0;
        }
        /* Can only have two zeroes if at start or end */
        else if (v6stat.zero_cnt == 2) {
            if ((v6stat.zero_pos != 0)
                && (v6stat.zero_pos != v6stat.total))
                return 0;
        } else
            /* Can only have one zero if *not* start or end */
        {
            if ((v6stat.zero_pos == 0)
                || (v6stat.zero_pos == v6stat.total))
                return 0;
        }
    }

    /* Format result */

    if (v6stat.zero_pos >= 0) {
        /* Copy initial part */
        memcpy(v6, v6stat.tmp, v6stat.zero_pos);
        /* Zero middle */
        memset(v6 + v6stat.zero_pos, 0, 16 - v6stat.total);
        /* Copy final part */
        if (v6stat.total != v6stat.zero_pos)
            memcpy(v6 + v6stat.zero_pos + 16 - v6stat.total,
                   v6stat.tmp + v6stat.zero_pos,
                   v6stat.total - v6stat.zero_pos);
    } else
        memcpy(v6, v6stat.tmp, 16);

    return 1;
}

static int ipv6_cb(const char *elem, int len, void *usr)
{
    IPV6_STAT *s = usr;
    /* Error if 16 bytes written */
    if (s->total == 16)
        return 0;
    if (len == 0) {
        /* Zero length element, corresponds to '::' */
        if (s->zero_pos == -1)
            s->zero_pos = s->total;
        /* If we've already got a :: its an error */
        else if (s->zero_pos != s->total)
            return 0;
        s->zero_cnt++;
    } else {
        /* If more than 4 characters could be final a.b.c.d form */
        if (len > 4) {
            /* Need at least 4 bytes left */
            if (s->total > 12)
                return 0;
            /* Must be end of string */
            if (elem[len])
                return 0;
            if (!ipv4_from_asc(s->tmp + s->total, elem))
                return 0;
            s->total += 4;
        } else {
            if (!ipv6_hex(s->tmp + s->total, elem, len))
                return 0;
            s->total += 2;
        }
    }
    return 1;
}

/*
 * Convert a string of up to 4 hex digits into the corresponding IPv6 form.
 */

static int ipv6_hex(unsigned char *out, const char *in, int inlen)
{
    unsigned char c;
    unsigned int num = 0;
    int x;

    if (inlen > 4)
        return 0;
    while (inlen--) {
        c = *in++;
        num <<= 4;
        x = OPENSSL_hexchar2int(c);
        if (x < 0)
            return 0;
        num |= (char)x;
    }
    out[0] = num >> 8;
    out[1] = num & 0xff;
    return 1;
}

int X509V3_NAME_from_section(X509_NAME *nm, STACK_OF(CONF_VALUE) *dn_sk,
                             unsigned long chtype)
{
    CONF_VALUE *v;
    int i, mval, spec_char, plus_char;
    char *p, *type;
    if (!nm)
        return 0;

    for (i = 0; i < sk_CONF_VALUE_num(dn_sk); i++) {
        v = sk_CONF_VALUE_value(dn_sk, i);
        type = v->name;
        /*
         * Skip past any leading X. X: X, etc to allow for multiple instances
         */
        for (p = type; *p; p++) {
#ifndef CHARSET_EBCDIC
            spec_char = ((*p == ':') || (*p == ',') || (*p == '.'));
#else
            spec_char = ((*p == os_toascii[':']) || (*p == os_toascii[','])
                    || (*p == os_toascii['.']));
#endif
            if (spec_char) {
                p++;
                if (*p)
                    type = p;
                break;
            }
        }
#ifndef CHARSET_EBCDIC
        plus_char = (*type == '+');
#else
        plus_char = (*type == os_toascii['+']);
#endif
        if (plus_char) {
            mval = -1;
            type++;
        } else
            mval = 0;
        if (!X509_NAME_add_entry_by_txt(nm, type, chtype,
                                        (unsigned char *)v->value, -1, -1,
                                        mval))
            return 0;

    }
    return 1;
}
