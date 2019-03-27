/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* Part of the code in here was originally in conf.c, which is now removed */

#include "e_os.h"
#include "internal/cryptlib.h"
#include <stdlib.h>
#include <string.h>
#include <openssl/conf.h>
#include <openssl/conf_api.h>

static void value_free_hash(const CONF_VALUE *a, LHASH_OF(CONF_VALUE) *conf);
static void value_free_stack_doall(CONF_VALUE *a);

/* Up until OpenSSL 0.9.5a, this was get_section */
CONF_VALUE *_CONF_get_section(const CONF *conf, const char *section)
{
    CONF_VALUE *v, vv;

    if ((conf == NULL) || (section == NULL))
        return NULL;
    vv.name = NULL;
    vv.section = (char *)section;
    v = lh_CONF_VALUE_retrieve(conf->data, &vv);
    return v;
}

/* Up until OpenSSL 0.9.5a, this was CONF_get_section */
STACK_OF(CONF_VALUE) *_CONF_get_section_values(const CONF *conf,
                                               const char *section)
{
    CONF_VALUE *v;

    v = _CONF_get_section(conf, section);
    if (v != NULL)
        return ((STACK_OF(CONF_VALUE) *)v->value);
    else
        return NULL;
}

int _CONF_add_string(CONF *conf, CONF_VALUE *section, CONF_VALUE *value)
{
    CONF_VALUE *v = NULL;
    STACK_OF(CONF_VALUE) *ts;

    ts = (STACK_OF(CONF_VALUE) *)section->value;

    value->section = section->section;
    if (!sk_CONF_VALUE_push(ts, value)) {
        return 0;
    }

    v = lh_CONF_VALUE_insert(conf->data, value);
    if (v != NULL) {
        (void)sk_CONF_VALUE_delete_ptr(ts, v);
        OPENSSL_free(v->name);
        OPENSSL_free(v->value);
        OPENSSL_free(v);
    }
    return 1;
}

char *_CONF_get_string(const CONF *conf, const char *section,
                       const char *name)
{
    CONF_VALUE *v, vv;
    char *p;

    if (name == NULL)
        return NULL;
    if (conf != NULL) {
        if (section != NULL) {
            vv.name = (char *)name;
            vv.section = (char *)section;
            v = lh_CONF_VALUE_retrieve(conf->data, &vv);
            if (v != NULL)
                return v->value;
            if (strcmp(section, "ENV") == 0) {
                p = ossl_safe_getenv(name);
                if (p != NULL)
                    return p;
            }
        }
        vv.section = "default";
        vv.name = (char *)name;
        v = lh_CONF_VALUE_retrieve(conf->data, &vv);
        if (v != NULL)
            return v->value;
        else
            return NULL;
    } else
        return ossl_safe_getenv(name);
}

static unsigned long conf_value_hash(const CONF_VALUE *v)
{
    return (OPENSSL_LH_strhash(v->section) << 2) ^ OPENSSL_LH_strhash(v->name);
}

static int conf_value_cmp(const CONF_VALUE *a, const CONF_VALUE *b)
{
    int i;

    if (a->section != b->section) {
        i = strcmp(a->section, b->section);
        if (i)
            return i;
    }

    if ((a->name != NULL) && (b->name != NULL)) {
        i = strcmp(a->name, b->name);
        return i;
    } else if (a->name == b->name)
        return 0;
    else
        return ((a->name == NULL) ? -1 : 1);
}

int _CONF_new_data(CONF *conf)
{
    if (conf == NULL) {
        return 0;
    }
    if (conf->data == NULL) {
        conf->data = lh_CONF_VALUE_new(conf_value_hash, conf_value_cmp);
        if (conf->data == NULL)
            return 0;
    }
    return 1;
}

typedef LHASH_OF(CONF_VALUE) LH_CONF_VALUE;

IMPLEMENT_LHASH_DOALL_ARG_CONST(CONF_VALUE, LH_CONF_VALUE);

void _CONF_free_data(CONF *conf)
{
    if (conf == NULL || conf->data == NULL)
        return;

    /* evil thing to make sure the 'OPENSSL_free()' works as expected */
    lh_CONF_VALUE_set_down_load(conf->data, 0);
    lh_CONF_VALUE_doall_LH_CONF_VALUE(conf->data, value_free_hash, conf->data);

    /*
     * We now have only 'section' entries in the hash table. Due to problems
     * with
     */

    lh_CONF_VALUE_doall(conf->data, value_free_stack_doall);
    lh_CONF_VALUE_free(conf->data);
}

static void value_free_hash(const CONF_VALUE *a, LHASH_OF(CONF_VALUE) *conf)
{
    if (a->name != NULL)
        (void)lh_CONF_VALUE_delete(conf, a);
}

static void value_free_stack_doall(CONF_VALUE *a)
{
    CONF_VALUE *vv;
    STACK_OF(CONF_VALUE) *sk;
    int i;

    if (a->name != NULL)
        return;

    sk = (STACK_OF(CONF_VALUE) *)a->value;
    for (i = sk_CONF_VALUE_num(sk) - 1; i >= 0; i--) {
        vv = sk_CONF_VALUE_value(sk, i);
        OPENSSL_free(vv->value);
        OPENSSL_free(vv->name);
        OPENSSL_free(vv);
    }
    sk_CONF_VALUE_free(sk);
    OPENSSL_free(a->section);
    OPENSSL_free(a);
}

/* Up until OpenSSL 0.9.5a, this was new_section */
CONF_VALUE *_CONF_new_section(CONF *conf, const char *section)
{
    STACK_OF(CONF_VALUE) *sk = NULL;
    int i;
    CONF_VALUE *v = NULL, *vv;

    if ((sk = sk_CONF_VALUE_new_null()) == NULL)
        goto err;
    if ((v = OPENSSL_malloc(sizeof(*v))) == NULL)
        goto err;
    i = strlen(section) + 1;
    if ((v->section = OPENSSL_malloc(i)) == NULL)
        goto err;

    memcpy(v->section, section, i);
    v->name = NULL;
    v->value = (char *)sk;

    vv = lh_CONF_VALUE_insert(conf->data, v);
    if (vv != NULL || lh_CONF_VALUE_error(conf->data) > 0)
        goto err;
    return v;

 err:
    sk_CONF_VALUE_free(sk);
    if (v != NULL)
        OPENSSL_free(v->section);
    OPENSSL_free(v);
    return NULL;
}
