/*
 * ra_svn_sasl.h :  SASL-related declarations shared between the
 * ra_svn and svnserve module
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */



#ifndef RA_SVN_SASL_H
#define RA_SVN_SASL_H

/* Keep this include statement at the top of this file. */
#include "private/ra_svn_wrapped_sasl.h"

#include <apr_errno.h>
#include <apr_pools.h>

#include "svn_error.h"
#include "svn_ra_svn.h"

#include "private/svn_atomic.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** The application and service name used for sasl_client_new,
 * sasl_server_init, and sasl_server_new. */
#define SVN_RA_SVN_SASL_NAME "svn"

extern volatile svn_atomic_t svn_ra_svn__sasl_status;

/* Initialize secprops with default values. */
void
svn_ra_svn__default_secprops(sasl_security_properties_t *secprops);

/* This function is called by the client and the server before
   calling sasl_{client, server}_init, pool is used for allocations. */
svn_error_t *
svn_ra_svn__sasl_common_init(apr_pool_t *pool);

/* Sets local_addrport and remote_addrport to a string containing the
   remote and local IP address and port, formatted like this: a.b.c.d;port. */
svn_error_t *
svn_ra_svn__get_addresses(const char **local_addrport,
                          const char **remote_addrport,
                          svn_ra_svn_conn_t *conn,
                          apr_pool_t *pool);

/* If a security layer was negotiated during the authentication exchange,
   create an encrypted stream for conn. */
svn_error_t *
svn_ra_svn__enable_sasl_encryption(svn_ra_svn_conn_t *conn,
                                   sasl_conn_t *sasl_ctx,
                                   apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* RA_SVN_SASL_H */
