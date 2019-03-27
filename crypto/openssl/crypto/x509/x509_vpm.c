/*
 * Copyright 2004-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>

#include "internal/cryptlib.h"
#include <openssl/crypto.h>
#include <openssl/buffer.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include "internal/x509_int.h"

#include "x509_lcl.h"

/* X509_VERIFY_PARAM functions */

#define SET_HOST 0
#define ADD_HOST 1

static char *str_copy(const char *s)
{
    return OPENSSL_strdup(s);
}

static void str_free(char *s)
{
    OPENSSL_free(s);
}

static int int_x509_param_set_hosts(X509_VERIFY_PARAM *vpm, int mode,
                                    const char *name, size_t namelen)
{
    char *copy;

    /*
     * Refuse names with embedded NUL bytes, except perhaps as final byte.
     * XXX: Do we need to push an error onto the error stack?
     */
    if (namelen == 0 || name == NULL)
        namelen = name ? strlen(name) : 0;
    else if (name && memchr(name, '\0', namelen > 1 ? namelen - 1 : namelen))
        return 0;
    if (namelen > 0 && name[namelen - 1] == '\0')
        --namelen;

    if (mode == SET_HOST) {
        sk_OPENSSL_STRING_pop_free(vpm->hosts, str_free);
        vpm->hosts = NULL;
    }
    if (name == NULL || namelen == 0)
        return 1;

    copy = OPENSSL_strndup(name, namelen);
    if (copy == NULL)
        return 0;

    if (vpm->hosts == NULL &&
        (vpm->hosts = sk_OPENSSL_STRING_new_null()) == NULL) {
        OPENSSL_free(copy);
        return 0;
    }

    if (!sk_OPENSSL_STRING_push(vpm->hosts, copy)) {
        OPENSSL_free(copy);
        if (sk_OPENSSL_STRING_num(vpm->hosts) == 0) {
            sk_OPENSSL_STRING_free(vpm->hosts);
            vpm->hosts = NULL;
        }
        return 0;
    }

    return 1;
}


X509_VERIFY_PARAM *X509_VERIFY_PARAM_new(void)
{
    X509_VERIFY_PARAM *param;

    param = OPENSSL_zalloc(sizeof(*param));
    if (param == NULL) {
        X509err(X509_F_X509_VERIFY_PARAM_NEW, ERR_R_MALLOC_FAILURE);
        return NULL;
    }
    param->trust = X509_TRUST_DEFAULT;
    /* param->inh_flags = X509_VP_FLAG_DEFAULT; */
    param->depth = -1;
    param->auth_level = -1; /* -1 means unset, 0 is explicit */
    return param;
}

void X509_VERIFY_PARAM_free(X509_VERIFY_PARAM *param)
{
    if (param == NULL)
        return;
    sk_ASN1_OBJECT_pop_free(param->policies, ASN1_OBJECT_free);
    sk_OPENSSL_STRING_pop_free(param->hosts, str_free);
    OPENSSL_free(param->peername);
    OPENSSL_free(param->email);
    OPENSSL_free(param->ip);
    OPENSSL_free(param);
}

/*-
 * This function determines how parameters are "inherited" from one structure
 * to another. There are several different ways this can happen.
 *
 * 1. If a child structure needs to have its values initialized from a parent
 *    they are simply copied across. For example SSL_CTX copied to SSL.
 * 2. If the structure should take on values only if they are currently unset.
 *    For example the values in an SSL structure will take appropriate value
 *    for SSL servers or clients but only if the application has not set new
 *    ones.
 *
 * The "inh_flags" field determines how this function behaves.
 *
 * Normally any values which are set in the default are not copied from the
 * destination and verify flags are ORed together.
 *
 * If X509_VP_FLAG_DEFAULT is set then anything set in the source is copied
 * to the destination. Effectively the values in "to" become default values
 * which will be used only if nothing new is set in "from".
 *
 * If X509_VP_FLAG_OVERWRITE is set then all value are copied across whether
 * they are set or not. Flags is still Ored though.
 *
 * If X509_VP_FLAG_RESET_FLAGS is set then the flags value is copied instead
 * of ORed.
 *
 * If X509_VP_FLAG_LOCKED is set then no values are copied.
 *
 * If X509_VP_FLAG_ONCE is set then the current inh_flags setting is zeroed
 * after the next call.
 */

/* Macro to test if a field should be copied from src to dest */

#define test_x509_verify_param_copy(field, def) \
        (to_overwrite || \
                ((src->field != def) && (to_default || (dest->field == def))))

/* Macro to test and copy a field if necessary */

#define x509_verify_param_copy(field, def) \
        if (test_x509_verify_param_copy(field, def)) \
                dest->field = src->field

int X509_VERIFY_PARAM_inherit(X509_VERIFY_PARAM *dest,
                              const X509_VERIFY_PARAM *src)
{
    unsigned long inh_flags;
    int to_default, to_overwrite;
    if (!src)
        return 1;
    inh_flags = dest->inh_flags | src->inh_flags;

    if (inh_flags & X509_VP_FLAG_ONCE)
        dest->inh_flags = 0;

    if (inh_flags & X509_VP_FLAG_LOCKED)
        return 1;

    if (inh_flags & X509_VP_FLAG_DEFAULT)
        to_default = 1;
    else
        to_default = 0;

    if (inh_flags & X509_VP_FLAG_OVERWRITE)
        to_overwrite = 1;
    else
        to_overwrite = 0;

    x509_verify_param_copy(purpose, 0);
    x509_verify_param_copy(trust, X509_TRUST_DEFAULT);
    x509_verify_param_copy(depth, -1);
    x509_verify_param_copy(auth_level, -1);

    /* If overwrite or check time not set, copy across */

    if (to_overwrite || !(dest->flags & X509_V_FLAG_USE_CHECK_TIME)) {
        dest->check_time = src->check_time;
        dest->flags &= ~X509_V_FLAG_USE_CHECK_TIME;
        /* Don't need to copy flag: that is done below */
    }

    if (inh_flags & X509_VP_FLAG_RESET_FLAGS)
        dest->flags = 0;

    dest->flags |= src->flags;

    if (test_x509_verify_param_copy(policies, NULL)) {
        if (!X509_VERIFY_PARAM_set1_policies(dest, src->policies))
            return 0;
    }

    /* Copy the host flags if and only if we're copying the host list */
    if (test_x509_verify_param_copy(hosts, NULL)) {
        sk_OPENSSL_STRING_pop_free(dest->hosts, str_free);
        dest->hosts = NULL;
        if (src->hosts) {
            dest->hosts =
                sk_OPENSSL_STRING_deep_copy(src->hosts, str_copy, str_free);
            if (dest->hosts == NULL)
                return 0;
            dest->hostflags = src->hostflags;
        }
    }

    if (test_x509_verify_param_copy(email, NULL)) {
        if (!X509_VERIFY_PARAM_set1_email(dest, src->email, src->emaillen))
            return 0;
    }

    if (test_x509_verify_param_copy(ip, NULL)) {
        if (!X509_VERIFY_PARAM_set1_ip(dest, src->ip, src->iplen))
            return 0;
    }

    return 1;
}

int X509_VERIFY_PARAM_set1(X509_VERIFY_PARAM *to,
                           const X509_VERIFY_PARAM *from)
{
    unsigned long save_flags = to->inh_flags;
    int ret;
    to->inh_flags |= X509_VP_FLAG_DEFAULT;
    ret = X509_VERIFY_PARAM_inherit(to, from);
    to->inh_flags = save_flags;
    return ret;
}

static int int_x509_param_set1(char **pdest, size_t *pdestlen,
                               const char *src, size_t srclen)
{
    void *tmp;
    if (src) {
        if (srclen == 0)
            srclen = strlen(src);

        tmp = OPENSSL_memdup(src, srclen);
        if (tmp == NULL)
            return 0;
    } else {
        tmp = NULL;
        srclen = 0;
    }
    OPENSSL_free(*pdest);
    *pdest = tmp;
    if (pdestlen != NULL)
        *pdestlen = srclen;
    return 1;
}

int X509_VERIFY_PARAM_set1_name(X509_VERIFY_PARAM *param, const char *name)
{
    OPENSSL_free(param->name);
    param->name = OPENSSL_strdup(name);
    if (param->name)
        return 1;
    return 0;
}

int X509_VERIFY_PARAM_set_flags(X509_VERIFY_PARAM *param, unsigned long flags)
{
    param->flags |= flags;
    if (flags & X509_V_FLAG_POLICY_MASK)
        param->flags |= X509_V_FLAG_POLICY_CHECK;
    return 1;
}

int X509_VERIFY_PARAM_clear_flags(X509_VERIFY_PARAM *param,
                                  unsigned long flags)
{
    param->flags &= ~flags;
    return 1;
}

unsigned long X509_VERIFY_PARAM_get_flags(X509_VERIFY_PARAM *param)
{
    return param->flags;
}

uint32_t X509_VERIFY_PARAM_get_inh_flags(const X509_VERIFY_PARAM *param)
{
    return param->inh_flags;
}

int X509_VERIFY_PARAM_set_inh_flags(X509_VERIFY_PARAM *param, uint32_t flags)
{
    param->inh_flags = flags;
    return 1;
}

int X509_VERIFY_PARAM_set_purpose(X509_VERIFY_PARAM *param, int purpose)
{
    return X509_PURPOSE_set(&param->purpose, purpose);
}

int X509_VERIFY_PARAM_set_trust(X509_VERIFY_PARAM *param, int trust)
{
    return X509_TRUST_set(&param->trust, trust);
}

void X509_VERIFY_PARAM_set_depth(X509_VERIFY_PARAM *param, int depth)
{
    param->depth = depth;
}

void X509_VERIFY_PARAM_set_auth_level(X509_VERIFY_PARAM *param, int auth_level)
{
    param->auth_level = auth_level;
}

time_t X509_VERIFY_PARAM_get_time(const X509_VERIFY_PARAM *param)
{
    return param->check_time;
}

void X509_VERIFY_PARAM_set_time(X509_VERIFY_PARAM *param, time_t t)
{
    param->check_time = t;
    param->flags |= X509_V_FLAG_USE_CHECK_TIME;
}

int X509_VERIFY_PARAM_add0_policy(X509_VERIFY_PARAM *param,
                                  ASN1_OBJECT *policy)
{
    if (!param->policies) {
        param->policies = sk_ASN1_OBJECT_new_null();
        if (!param->policies)
            return 0;
    }
    if (!sk_ASN1_OBJECT_push(param->policies, policy))
        return 0;
    return 1;
}

int X509_VERIFY_PARAM_set1_policies(X509_VERIFY_PARAM *param,
                                    STACK_OF(ASN1_OBJECT) *policies)
{
    int i;
    ASN1_OBJECT *oid, *doid;

    if (!param)
        return 0;
    sk_ASN1_OBJECT_pop_free(param->policies, ASN1_OBJECT_free);

    if (!policies) {
        param->policies = NULL;
        return 1;
    }

    param->policies = sk_ASN1_OBJECT_new_null();
    if (!param->policies)
        return 0;

    for (i = 0; i < sk_ASN1_OBJECT_num(policies); i++) {
        oid = sk_ASN1_OBJECT_value(policies, i);
        doid = OBJ_dup(oid);
        if (!doid)
            return 0;
        if (!sk_ASN1_OBJECT_push(param->policies, doid)) {
            ASN1_OBJECT_free(doid);
            return 0;
        }
    }
    param->flags |= X509_V_FLAG_POLICY_CHECK;
    return 1;
}

int X509_VERIFY_PARAM_set1_host(X509_VERIFY_PARAM *param,
                                const char *name, size_t namelen)
{
    return int_x509_param_set_hosts(param, SET_HOST, name, namelen);
}

int X509_VERIFY_PARAM_add1_host(X509_VERIFY_PARAM *param,
                                const char *name, size_t namelen)
{
    return int_x509_param_set_hosts(param, ADD_HOST, name, namelen);
}

void X509_VERIFY_PARAM_set_hostflags(X509_VERIFY_PARAM *param,
                                     unsigned int flags)
{
    param->hostflags = flags;
}

unsigned int X509_VERIFY_PARAM_get_hostflags(const X509_VERIFY_PARAM *param)
{
    return param->hostflags;
}

char *X509_VERIFY_PARAM_get0_peername(X509_VERIFY_PARAM *param)
{
    return param->peername;
}

/*
 * Move peername from one param structure to another, freeing any name present
 * at the target.  If the source is a NULL parameter structure, free and zero
 * the target peername.
 */
void X509_VERIFY_PARAM_move_peername(X509_VERIFY_PARAM *to,
                                     X509_VERIFY_PARAM *from)
{
    char *peername = (from != NULL) ? from->peername : NULL;

    if (to->peername != peername) {
        OPENSSL_free(to->peername);
        to->peername = peername;
    }
    if (from)
        from->peername = NULL;
}

int X509_VERIFY_PARAM_set1_email(X509_VERIFY_PARAM *param,
                                 const char *email, size_t emaillen)
{
    return int_x509_param_set1(&param->email, &param->emaillen,
                               email, emaillen);
}

int X509_VERIFY_PARAM_set1_ip(X509_VERIFY_PARAM *param,
                              const unsigned char *ip, size_t iplen)
{
    if (iplen != 0 && iplen != 4 && iplen != 16)
        return 0;
    return int_x509_param_set1((char **)&param->ip, &param->iplen,
                               (char *)ip, iplen);
}

int X509_VERIFY_PARAM_set1_ip_asc(X509_VERIFY_PARAM *param, const char *ipasc)
{
    unsigned char ipout[16];
    size_t iplen;

    iplen = (size_t)a2i_ipadd(ipout, ipasc);
    if (iplen == 0)
        return 0;
    return X509_VERIFY_PARAM_set1_ip(param, ipout, iplen);
}

int X509_VERIFY_PARAM_get_depth(const X509_VERIFY_PARAM *param)
{
    return param->depth;
}

int X509_VERIFY_PARAM_get_auth_level(const X509_VERIFY_PARAM *param)
{
    return param->auth_level;
}

const char *X509_VERIFY_PARAM_get0_name(const X509_VERIFY_PARAM *param)
{
    return param->name;
}

#define vpm_empty_id NULL, 0U, NULL, NULL, 0, NULL, 0

/*
 * Default verify parameters: these are used for various applications and can
 * be overridden by the user specified table. NB: the 'name' field *must* be
 * in alphabetical order because it will be searched using OBJ_search.
 */

static const X509_VERIFY_PARAM default_table[] = {
    {
     "default",                 /* X509 default parameters */
     0,                         /* Check time */
     0,                         /* internal flags */
     X509_V_FLAG_TRUSTED_FIRST, /* flags */
     0,                         /* purpose */
     0,                         /* trust */
     100,                       /* depth */
     -1,                        /* auth_level */
     NULL,                      /* policies */
     vpm_empty_id},
    {
     "pkcs7",                   /* S/MIME sign parameters */
     0,                         /* Check time */
     0,                         /* internal flags */
     0,                         /* flags */
     X509_PURPOSE_SMIME_SIGN,   /* purpose */
     X509_TRUST_EMAIL,          /* trust */
     -1,                        /* depth */
     -1,                        /* auth_level */
     NULL,                      /* policies */
     vpm_empty_id},
    {
     "smime_sign",              /* S/MIME sign parameters */
     0,                         /* Check time */
     0,                         /* internal flags */
     0,                         /* flags */
     X509_PURPOSE_SMIME_SIGN,   /* purpose */
     X509_TRUST_EMAIL,          /* trust */
     -1,                        /* depth */
     -1,                        /* auth_level */
     NULL,                      /* policies */
     vpm_empty_id},
    {
     "ssl_client",              /* SSL/TLS client parameters */
     0,                         /* Check time */
     0,                         /* internal flags */
     0,                         /* flags */
     X509_PURPOSE_SSL_CLIENT,   /* purpose */
     X509_TRUST_SSL_CLIENT,     /* trust */
     -1,                        /* depth */
     -1,                        /* auth_level */
     NULL,                      /* policies */
     vpm_empty_id},
    {
     "ssl_server",              /* SSL/TLS server parameters */
     0,                         /* Check time */
     0,                         /* internal flags */
     0,                         /* flags */
     X509_PURPOSE_SSL_SERVER,   /* purpose */
     X509_TRUST_SSL_SERVER,     /* trust */
     -1,                        /* depth */
     -1,                        /* auth_level */
     NULL,                      /* policies */
     vpm_empty_id}
};

static STACK_OF(X509_VERIFY_PARAM) *param_table = NULL;

static int table_cmp(const X509_VERIFY_PARAM *a, const X509_VERIFY_PARAM *b)
{
    return strcmp(a->name, b->name);
}

DECLARE_OBJ_BSEARCH_CMP_FN(X509_VERIFY_PARAM, X509_VERIFY_PARAM, table);
IMPLEMENT_OBJ_BSEARCH_CMP_FN(X509_VERIFY_PARAM, X509_VERIFY_PARAM, table);

static int param_cmp(const X509_VERIFY_PARAM *const *a,
                     const X509_VERIFY_PARAM *const *b)
{
    return strcmp((*a)->name, (*b)->name);
}

int X509_VERIFY_PARAM_add0_table(X509_VERIFY_PARAM *param)
{
    int idx;
    X509_VERIFY_PARAM *ptmp;
    if (param_table == NULL) {
        param_table = sk_X509_VERIFY_PARAM_new(param_cmp);
        if (param_table == NULL)
            return 0;
    } else {
        idx = sk_X509_VERIFY_PARAM_find(param_table, param);
        if (idx >= 0) {
            ptmp = sk_X509_VERIFY_PARAM_delete(param_table, idx);
            X509_VERIFY_PARAM_free(ptmp);
        }
    }
    if (!sk_X509_VERIFY_PARAM_push(param_table, param))
        return 0;
    return 1;
}

int X509_VERIFY_PARAM_get_count(void)
{
    int num = OSSL_NELEM(default_table);
    if (param_table)
        num += sk_X509_VERIFY_PARAM_num(param_table);
    return num;
}

const X509_VERIFY_PARAM *X509_VERIFY_PARAM_get0(int id)
{
    int num = OSSL_NELEM(default_table);
    if (id < num)
        return default_table + id;
    return sk_X509_VERIFY_PARAM_value(param_table, id - num);
}

const X509_VERIFY_PARAM *X509_VERIFY_PARAM_lookup(const char *name)
{
    int idx;
    X509_VERIFY_PARAM pm;

    pm.name = (char *)name;
    if (param_table != NULL) {
        idx = sk_X509_VERIFY_PARAM_find(param_table, &pm);
        if (idx >= 0)
            return sk_X509_VERIFY_PARAM_value(param_table, idx);
    }
    return OBJ_bsearch_table(&pm, default_table, OSSL_NELEM(default_table));
}

void X509_VERIFY_PARAM_table_cleanup(void)
{
    sk_X509_VERIFY_PARAM_pop_free(param_table, X509_VERIFY_PARAM_free);
    param_table = NULL;
}
