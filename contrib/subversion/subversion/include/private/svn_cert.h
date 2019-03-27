/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_cert.h
 * @brief Implementation of certificate validation functions
 */

#ifndef SVN_CERT_H
#define SVN_CERT_H

#include <apr.h>

#include "svn_types.h"
#include "svn_string.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Return TRUE iff @a pattern matches @a hostname as defined
 * by the matching rules of RFC 6125.  In the context of RFC
 * 6125 the pattern is the domain name portion of the presented
 * identifier (which comes from the Common Name or a DNSName
 * portion of the subjectAltName of an X.509 certificate) and
 * the hostname is the source domain (i.e. the host portion
 * of the URI the user entered).
 *
 * @note With respect to wildcards we only support matching
 * wildcards in the left-most label and as the only character
 * in the left-most label (i.e. we support RFC 6125 § 6.4.3
 * Rule 1 and 2 but not the optional Rule 3).  This may change
 * in the future.
 *
 * @note Subversion does not at current support internationalized
 * domain names.  Both values are presumed to be in NR-LDH label
 * or A-label form (see RFC 5890 for the definition).
 *
 * @since New in 1.9.
 */
svn_boolean_t
svn_cert__match_dns_identity(svn_string_t *pattern, svn_string_t *hostname);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CERT_H */
