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
#include "apr_dso.h"
#include "apu.h"

#ifndef APU_INTERNAL_H
#define APU_INTERNAL_H

#if APU_DSO_BUILD

#ifdef __cplusplus
extern "C" {
#endif

/* For modular dso loading, an internal interlock to allow us to
 * continue to initialize modules by multiple threads, the caller
 * of apu_dso_load must lock first, and not unlock until any init
 * finalization is complete.
 */
apr_status_t apu_dso_init(apr_pool_t *pool);

apr_status_t apu_dso_mutex_lock(void);
apr_status_t apu_dso_mutex_unlock(void);

apr_status_t apu_dso_load(apr_dso_handle_t **dso, apr_dso_handle_sym_t *dsoptr, const char *module,
                          const char *modsym, apr_pool_t *pool);

#if APR_HAS_LDAP

/* For LDAP internal builds, wrap our LDAP namespace */

struct apr__ldap_dso_fntable {
    int (*info)(apr_pool_t *pool, apr_ldap_err_t **result_err);
    int (*init)(apr_pool_t *pool, LDAP **ldap, const char *hostname,
                int portno, int secure, apr_ldap_err_t **result_err);
    int (*ssl_init)(apr_pool_t *pool, const char *cert_auth_file,
                    int cert_file_type, apr_ldap_err_t **result_err);
    int (*ssl_deinit)(void);
    int (*get_option)(apr_pool_t *pool, LDAP *ldap, int option,
                      void *outvalue, apr_ldap_err_t **result_err);
    int (*set_option)(apr_pool_t *pool, LDAP *ldap, int option,
                      const void *invalue, apr_ldap_err_t **result_err);
    apr_status_t (*rebind_init)(apr_pool_t *pool);
    apr_status_t (*rebind_add)(apr_pool_t *pool, LDAP *ld,
                               const char *bindDN, const char *bindPW);
    apr_status_t (*rebind_remove)(LDAP *ld);
};

#endif /* APR_HAS_LDAP */

#ifdef __cplusplus
}
#endif

#endif /* APU_DSO_BUILD */

#endif /* APU_INTERNAL_H */

