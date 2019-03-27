/*
 * Copyright 2012-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <openssl/crypto.h>
#include "internal/cryptlib.h"
#include <openssl/conf.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

/* Algorithm configuration module. */

static int alg_module_init(CONF_IMODULE *md, const CONF *cnf)
{
    int i;
    const char *oid_section;
    STACK_OF(CONF_VALUE) *sktmp;
    CONF_VALUE *oval;

    oid_section = CONF_imodule_get_value(md);
    if ((sktmp = NCONF_get_section(cnf, oid_section)) == NULL) {
        EVPerr(EVP_F_ALG_MODULE_INIT, EVP_R_ERROR_LOADING_SECTION);
        return 0;
    }
    for (i = 0; i < sk_CONF_VALUE_num(sktmp); i++) {
        oval = sk_CONF_VALUE_value(sktmp, i);
        if (strcmp(oval->name, "fips_mode") == 0) {
            int m;
            if (!X509V3_get_value_bool(oval, &m)) {
                EVPerr(EVP_F_ALG_MODULE_INIT, EVP_R_INVALID_FIPS_MODE);
                return 0;
            }
            if (m > 0) {
                EVPerr(EVP_F_ALG_MODULE_INIT, EVP_R_FIPS_MODE_NOT_SUPPORTED);
                return 0;
            }
        } else {
            EVPerr(EVP_F_ALG_MODULE_INIT, EVP_R_UNKNOWN_OPTION);
            ERR_add_error_data(4, "name=", oval->name,
                               ", value=", oval->value);
        }

    }
    return 1;
}

void EVP_add_alg_module(void)
{
    CONF_module_add("alg_section", alg_module_init, 0);
}
