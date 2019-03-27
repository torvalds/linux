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
 
 /* Setup:
  *  - Create or edit the file data/host.data and add an
  *     ldap server DN.  Multiple DNs may be listed on
  *     a single line.
  *  - Copy the server certificates to the data/ directory.
  *     All DER type certificates must have the .der extention.
  *     All BASE64 or PEM certificates must have the .b64
  *     extension.  All certificate files copied to the /data
  *     directory will be added to the ldap certificate store.
  */
   
 /* This test covers the following three types of connections:
  *  - Unsecure ldap://
  *  - Secure ldaps://
  *  - Secure ldap://+Start_TLS
  *
  *  - (TBD) Mutual authentication 
  *
  * There are other variations that should be tested:
  *  - All of the above with multiple redundant LDAP servers
  *     This can be done by listing more than one server DN
  *      in the host.data file.  The DNs should all be listed
  *      on one line separated by a space.
  *  - All of the above with multiple certificates
  *     If more than one certificate is found in the data/
  *      directory, each certificate found will be added
  *      to the certificate store.
  *  - All of the above on alternate ports
  *     An alternate port can be specified as part of the
  *      host in the host.data file.  The ":port" should 
  *      follow each DN listed.  Default is 389 and 636.
  *  - Secure connections with mutual authentication
  */

#include "testutil.h"

#include "apr.h"
#include "apr_general.h"
#include "apr_ldap.h"
#include "apr_file_io.h"
#include "apr_file_info.h"
#include "apr_strings.h"
#if APR_HAVE_STDLIB_H
#include <stdlib.h>
#endif
#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include "apr_want.h"

#define DIRNAME "data"
#define FILENAME DIRNAME "/host.data"
#define CERTFILEDER DIRNAME "/*.der"
#define CERTFILEB64 DIRNAME "/*.b64"

#if APR_HAS_LDAP

static char ldap_host[256];

static int get_ldap_host(void)
{
    apr_status_t rv;
    apr_file_t *thefile = NULL;
    char *ptr;

    ldap_host[0] = '\0';
    rv = apr_file_open(&thefile, FILENAME, 
                       APR_FOPEN_READ,
                       APR_UREAD | APR_UWRITE | APR_GREAD, p);
    if (rv != APR_SUCCESS) {
        return 0;
    }

    rv = apr_file_gets(ldap_host, sizeof(ldap_host), thefile);
    if (rv != APR_SUCCESS) {
        return 0;
    }

    ptr = strstr (ldap_host, "\r\n");
    if (ptr) {
        *ptr = '\0';
    }
    apr_file_close(thefile);

    return 1;

}

static int add_ldap_certs(abts_case *tc)
{
    apr_status_t status;
    apr_dir_t *thedir;
    apr_finfo_t dirent;
    apr_ldap_err_t *result = NULL;

    if ((status = apr_dir_open(&thedir, DIRNAME, p)) == APR_SUCCESS) {
        apr_ldap_opt_tls_cert_t *cert = (apr_ldap_opt_tls_cert_t *)apr_pcalloc(p, sizeof(apr_ldap_opt_tls_cert_t));

        do {
            status = apr_dir_read(&dirent, APR_FINFO_MIN | APR_FINFO_NAME, thedir);
            if (APR_STATUS_IS_INCOMPLETE(status)) {
                continue; /* ignore un-stat()able files */
            }
            else if (status != APR_SUCCESS) {
                break;
            }

            if (strstr(dirent.name, ".der")) {
                cert->type = APR_LDAP_CA_TYPE_DER;
                cert->path = apr_pstrcat (p, DIRNAME, "/", dirent.name, NULL);
                apr_ldap_set_option(p, NULL, APR_LDAP_OPT_TLS_CERT, (void *)cert, &result);
                ABTS_TRUE(tc, result->rc == LDAP_SUCCESS);
            }
            if (strstr(dirent.name, ".b64")) {
                cert->type = APR_LDAP_CA_TYPE_BASE64;
                cert->path = apr_pstrcat (p, DIRNAME, "/", dirent.name, NULL);
                apr_ldap_set_option(p, NULL, APR_LDAP_OPT_TLS_CERT, (void *)cert, &result);
                ABTS_TRUE(tc, result->rc == LDAP_SUCCESS);
            }

        } while (1);

        apr_dir_close(thedir);
    }
    return 0;
}

static void test_ldap_connection(abts_case *tc, LDAP *ldap)
{
    int version  = LDAP_VERSION3;
    int failures, result;
    
    /* always default to LDAP V3 */
    ldap_set_option(ldap, LDAP_OPT_PROTOCOL_VERSION, &version);

    for (failures=0; failures<10; failures++)
    {
        result = ldap_simple_bind_s(ldap,
                                    (char *)NULL,
                                    (char *)NULL);
        if (LDAP_SERVER_DOWN != result)
            break;
    }

    ABTS_TRUE(tc, result == LDAP_SUCCESS);
    if (result != LDAP_SUCCESS) {
        abts_log_message("%s\n", ldap_err2string(result));
    }

    ldap_unbind_s(ldap);

    return;
}

static void test_ldap(abts_case *tc, void *data)
{
    apr_pool_t *pool = p;
    LDAP *ldap;
    apr_ldap_err_t *result = NULL;


    ABTS_ASSERT(tc, "failed to get host", ldap_host[0] != '\0');
    
    apr_ldap_init(pool, &ldap,
                  ldap_host, LDAP_PORT,
                  APR_LDAP_NONE, &(result));

    ABTS_TRUE(tc, ldap != NULL);
    ABTS_PTR_NOTNULL(tc, result);

    if (result->rc == LDAP_SUCCESS) {
        test_ldap_connection(tc, ldap);
    }
}

static void test_ldaps(abts_case *tc, void *data)
{
    apr_pool_t *pool = p;
    LDAP *ldap;
    apr_ldap_err_t *result = NULL;

    apr_ldap_init(pool, &ldap,
                  ldap_host, LDAPS_PORT,
                  APR_LDAP_SSL, &(result));

    ABTS_TRUE(tc, ldap != NULL);
    ABTS_PTR_NOTNULL(tc, result);

    if (result->rc == LDAP_SUCCESS) {
        add_ldap_certs(tc);

        test_ldap_connection(tc, ldap);
    }
}

static void test_ldap_tls(abts_case *tc, void *data)
{
    apr_pool_t *pool = p;
    LDAP *ldap;
    apr_ldap_err_t *result = NULL;

    apr_ldap_init(pool, &ldap,
                  ldap_host, LDAP_PORT,
                  APR_LDAP_STARTTLS, &(result));

    ABTS_TRUE(tc, ldap != NULL);
    ABTS_PTR_NOTNULL(tc, result);

    if (result->rc == LDAP_SUCCESS) {
        add_ldap_certs(tc);

        test_ldap_connection(tc, ldap);
    }
}

#endif /* APR_HAS_LDAP */

abts_suite *testldap(abts_suite *suite)
{
#if APR_HAS_LDAP
    apr_ldap_err_t *result = NULL;
    suite = ADD_SUITE(suite);

    apr_ldap_ssl_init(p, NULL, 0, &result);

    if (get_ldap_host()) {
        abts_run_test(suite, test_ldap, NULL);
        abts_run_test(suite, test_ldaps, NULL);
        abts_run_test(suite, test_ldap_tls, NULL);
    }
#endif /* APR_HAS_LDAP */

    return suite;
}

