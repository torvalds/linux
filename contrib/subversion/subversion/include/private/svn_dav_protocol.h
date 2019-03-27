/*
 * svn_dav_protocol.h: Declarations of the protocol shared by the
 * mod_dav_svn backend for httpd's mod_dav and its ra_serf RA DAV clients.
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

#ifndef SVN_DAV_PROTOCOL_H
#define SVN_DAV_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Names for the custom HTTP REPORTs understood by mod_dav_svn, sans
    namespace. */
#define SVN_DAV__MERGEINFO_REPORT "mergeinfo-report"
#define SVN_DAV__INHERITED_PROPS_REPORT "inherited-props-report"

/** Names for XML child elements of the custom HTTP REPORTs understood
    by mod_dav_svn, sans namespace. */
#define SVN_DAV__CREATIONDATE "creationdate"
#define SVN_DAV__MERGEINFO_ITEM "mergeinfo-item"
#define SVN_DAV__MERGEINFO_PATH "mergeinfo-path"
#define SVN_DAV__MERGEINFO_INFO "mergeinfo-info"
#define SVN_DAV__PATH "path"
#define SVN_DAV__INHERIT "inherit"
#define SVN_DAV__REVISION "revision"
#define SVN_DAV__INCLUDE_DESCENDANTS "include-descendants"
#define SVN_DAV__VERSION_NAME "version-name"
#define SVN_DAV__IPROP_ITEM "iprop-item"
#define SVN_DAV__IPROP_PATH "iprop-path"
#define SVN_DAV__IPROP_PROPNAME "iprop-propname"
#define SVN_DAV__IPROP_PROPVAL "iprop-propval"

/** Names of XML elements attributes and tags for svn_ra_change_rev_prop2()'s
    extension of PROPPATCH.  */
#define SVN_DAV__OLD_VALUE "old-value"
#define SVN_DAV__OLD_VALUE__ABSENT "absent"

/** Helper typedef for svn_ra_change_rev_prop2() implementation. */
typedef struct svn_dav__two_props_t {
  const svn_string_t *const *old_value_p;
  const svn_string_t *new_value;
} svn_dav__two_props_t;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DAV_PROTOCOL_H */
