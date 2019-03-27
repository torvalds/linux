/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apr.h"
#include "apu.h"
#include "apu_config.h"
#include "apr_ldap.h"
#include "apu_internal.h"
#include "apr_dso.h"
#include "apr_errno.h"
#include "apr_pools.h"
#include "apr_strings.h"
#include "apu_version.h"

#if APR_HAS_LDAP

#if APU_DSO_BUILD

static struct apr__ldap_dso_fntable *lfn = NULL;

static apr_status_t load_ldap(apr_pool_t *pool)
{
    char *modname;
    apr_dso_handle_sym_t symbol;
    apr_status_t rv;

    /* deprecate in 2.0 - permit implicit initialization */
    apu_dso_init(pool);

    rv = apu_dso_mutex_lock();
    if (rv) {
        return rv;
    }

#if defined(WIN32)
    modname = "apr_ldap-" APU_STRINGIFY(APU_MAJOR_VERSION) ".dll";
#else
    modname = "apr_ldap-" APU_STRINGIFY(APU_MAJOR_VERSION) ".so";
#endif
    rv = apu_dso_load(NULL, &symbol, modname, "apr__ldap_fns", pool);
    if (rv == APR_SUCCESS) {
        lfn = symbol;
    }
    apu_dso_mutex_unlock();

    return rv;
}

#define LOAD_LDAP_STUB(pool, failres) \
    if (!lfn && (load_ldap(pool) != APR_SUCCESS)) \
        return failres;

APU_DECLARE_LDAP(int) apr_ldap_info(apr_pool_t *pool,
                                    apr_ldap_err_t **result_err)
{
    LOAD_LDAP_STUB(pool, -1);
    return lfn->info(pool, result_err);
}

APU_DECLARE_LDAP(int) apr_ldap_init(apr_pool_t *pool,
                                    LDAP **ldap,
                                    const char *hostname,
                                    int portno,
                                    int secure,
                                    apr_ldap_err_t **result_err)
{
    LOAD_LDAP_STUB(pool, -1);
    return lfn->init(pool, ldap, hostname, portno, secure, result_err);
}

APU_DECLARE_LDAP(int) apr_ldap_ssl_init(apr_pool_t *pool,
                                        const char *cert_auth_file,
                                        int cert_file_type,
                                        apr_ldap_err_t **result_err)
{
    LOAD_LDAP_STUB(pool, -1);
    return lfn->ssl_init(pool, cert_auth_file, cert_file_type, result_err);
}

APU_DECLARE_LDAP(int) apr_ldap_ssl_deinit(void)
{
    if (!lfn)
        return -1;
    return lfn->ssl_deinit();
}

APU_DECLARE_LDAP(int) apr_ldap_get_option(apr_pool_t *pool,
                                          LDAP *ldap,
                                          int option,
                                          void *outvalue,
                                          apr_ldap_err_t **result_err)
{
    LOAD_LDAP_STUB(pool, -1);
    return lfn->get_option(pool, ldap, option, outvalue, result_err);
}

APU_DECLARE_LDAP(int) apr_ldap_set_option(apr_pool_t *pool,
                                          LDAP *ldap,
                                          int option,
                                          const void *invalue,
                                          apr_ldap_err_t **result_err)
{
    LOAD_LDAP_STUB(pool, -1);
    return lfn->set_option(pool, ldap, option, invalue, result_err);
}

APU_DECLARE_LDAP(apr_status_t) apr_ldap_rebind_init(apr_pool_t *pool)
{
    LOAD_LDAP_STUB(pool, APR_EGENERAL);
    return lfn->rebind_init(pool);
}

APU_DECLARE_LDAP(apr_status_t) apr_ldap_rebind_add(apr_pool_t *pool,
                                                   LDAP *ld,
                                                   const char *bindDN,
                                                   const char *bindPW)
{
    LOAD_LDAP_STUB(pool, APR_EGENERAL);
    return lfn->rebind_add(pool, ld, bindDN, bindPW);
}

APU_DECLARE_LDAP(apr_status_t) apr_ldap_rebind_remove(LDAP *ld)
{
    if (!lfn)
        return APR_EGENERAL;
    return lfn->rebind_remove(ld);
}

#endif /* APU_DSO_BUILD */

#endif /* APR_HAS_LDAP */

