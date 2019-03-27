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

/**
 * @file apr_ldap_init.h
 * @brief  APR-UTIL LDAP ldap_init() functions
 */
#ifndef APR_LDAP_INIT_H
#define APR_LDAP_INIT_H

/**
 * @addtogroup APR_Util_LDAP
 * @{
 */

#include "apr_ldap.h"

#if APR_HAS_LDAP

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/**
 * Macro to detect security related return values.
 */
#if defined(LDAP_INSUFFICIENT_ACCESS)
#define APU_LDAP_INSUFFICIENT_ACCESS LDAP_INSUFFICIENT_ACCESS
#elif defined(LDAP_INSUFFICIENT_RIGHTS)
#define APU_LDAP_INSUFFICIENT_ACCESS LDAP_INSUFFICIENT_RIGHTS
#elif defined(APR_HAS_MICROSOFT_LDAPSDK)
/* The macros above fail to contemplate that LDAP_RETCODE values
 * may be represented by an enum.  autoconf tests would be much
 * more robust.
 */
#define APU_LDAP_INSUFFICIENT_ACCESS LDAP_INSUFFICIENT_RIGHTS
#else
#error The security return codes must be added to support this LDAP toolkit.
#endif

#if defined(LDAP_SECURITY_ERROR)
#define APU_LDAP_SECURITY_ERROR LDAP_SECURITY_ERROR
#else
#define APU_LDAP_SECURITY_ERROR(n)	\
    (LDAP_INAPPROPRIATE_AUTH == n) ? 1 \
    : (LDAP_INVALID_CREDENTIALS == n) ? 1 \
    : (APU_LDAP_INSUFFICIENT_ACCESS == n) ? 1 \
    : 0
#endif


/**
 * APR LDAP SSL Initialise function
 *
 * This function initialises SSL on the underlying LDAP toolkit
 * if this is necessary.
 *
 * If a CA certificate is provided, this is set, however the setting
 * of certificates via this method has been deprecated and will be removed in
 * APR v2.0.
 *
 * The apr_ldap_set_option() function with the APR_LDAP_OPT_TLS_CERT option
 * should be used instead to set certificates.
 *
 * If SSL support is not available on this platform, or a problem
 * was encountered while trying to set the certificate, the function
 * will return APR_EGENERAL. Further LDAP specific error information
 * can be found in result_err.
 * @param pool The pool to use
 * @param cert_auth_file The name of the certificate to use, can be NULL
 * @param cert_file_type The type of certificate specified. See the
 * apr_ldap_set_option() APR_LDAP_OPT_TLS_CERT option for details.
 * @param result_err The returned result
 */
APU_DECLARE_LDAP(int) apr_ldap_ssl_init(apr_pool_t *pool,
                                        const char *cert_auth_file,
                                        int cert_file_type,
                                        apr_ldap_err_t **result_err);

/**
 * APR LDAP SSL De-Initialise function
 *
 * This function tears down any SSL certificate setup previously
 * set using apr_ldap_ssl_init(). It should be called to clean
 * up if a graceful restart of a service is attempted.
 * @todo currently we do not check whether apr_ldap_ssl_init()
 * has been called first - we probably should.
 */
APU_DECLARE_LDAP(int) apr_ldap_ssl_deinit(void);

/**
 * APR LDAP initialise function
 *
 * This function is responsible for initialising an LDAP
 * connection in a toolkit independant way. It does the
 * job of ldap_init() from the C api.
 *
 * It handles both the SSL and non-SSL case, and attempts
 * to hide the complexity setup from the user. This function
 * assumes that any certificate setup necessary has already
 * been done.
 *
 * If SSL or STARTTLS needs to be enabled, and the underlying
 * toolkit supports it, the following values are accepted for
 * secure:
 *
 * APR_LDAP_NONE: No encryption
 * APR_LDAP_SSL: SSL encryption (ldaps://)
 * APR_LDAP_STARTTLS: Force STARTTLS on ldap://
 * @remark The Novell toolkit is only able to set the SSL mode via this
 * function. To work around this limitation, set the SSL mode here if no
 * per connection client certificates are present, otherwise set secure
 * APR_LDAP_NONE here, then set the per connection client certificates,
 * followed by setting the SSL mode via apr_ldap_set_option(). As Novell
 * does not support per connection client certificates, this problem is
 * worked around while still being compatible with other LDAP toolkits.
 * @param pool The pool to use
 * @param ldap The LDAP handle
 * @param hostname The name of the host to connect to. This can be either a
 * DNS name, or an IP address.
 * @param portno The port to connect to
 * @param secure The security mode to set
 * @param result_err The returned result
 */
APU_DECLARE_LDAP(int) apr_ldap_init(apr_pool_t *pool,
                                    LDAP **ldap,
                                    const char *hostname,
                                    int portno,
                                    int secure,
                                    apr_ldap_err_t **result_err);

/**
 * APR LDAP info function
 *
 * This function returns a string describing the LDAP toolkit
 * currently in use. The string is placed inside result_err->reason.
 * @param pool The pool to use
 * @param result_err The returned result
 */
APU_DECLARE_LDAP(int) apr_ldap_info(apr_pool_t *pool,
                                    apr_ldap_err_t **result_err);

#ifdef __cplusplus
}
#endif

#endif /* APR_HAS_LDAP */

/** @} */

#endif /* APR_LDAP_URL_H */
