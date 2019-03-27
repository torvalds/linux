/*
 * props.h :  properties
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


#ifndef SVN_LIBSVN_WC_PROPS_H
#define SVN_LIBSVN_WC_PROPS_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_props.h"

#include "wc_db.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Internal function for diffing props. See svn_wc_get_prop_diffs2(). */
svn_error_t *
svn_wc__internal_propdiff(apr_array_header_t **propchanges,
                          apr_hash_t **original_props,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);


/* Internal function for fetching a property. See svn_wc_prop_get2(). */
svn_error_t *
svn_wc__internal_propget(const svn_string_t **value,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         const char *name,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);

/* Validate and canonicalize the PROPS like svn_wc_prop_set4() does;
 * see that function for details of the SKIP_SOME_CHECKS option.
 *
 * The properties are checked against the node at LOCAL_ABSPATH (which
 * need not be under version control) of kind KIND.  This text of this
 * node may be read (if it is a file) in order to validate the
 * svn:eol-style property.
 *
 * Only regular props are accepted; WC props and entry props raise an error
 * (unlike svn_wc_prop_set4() which accepts WC props).
 *
 * Set *PREPARED_PROPS to the resulting canonicalized properties,
 * allocating any new data in RESULT_POOL but making shallow copies of
 * keys and unchanged values from PROPS.
 */
svn_error_t *
svn_wc__canonicalize_props(apr_hash_t **prepared_props,
                           const char *local_abspath,
                           svn_node_kind_t node_kind,
                           const apr_hash_t *props,
                           svn_boolean_t skip_some_checks,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);


/* Given LOCAL_ABSPATH/DB and an array of PROPCHANGES based on
   SERVER_BASEPROPS, calculate what changes should be applied to the working
   copy.

   We return the new property collections to the caller, so the caller
   can combine the property update with other operations.

   If SERVER_BASEPROPS is NULL then use the pristine props as PROPCHANGES
   base.

   Return the new set of actual properties in *NEW_ACTUAL_PROPS.

   Append any conflicts of the actual props to *CONFLICT_SKEL.  (First
   allocate *CONFLICT_SKEL from RESULT_POOL if it is initially NULL.
   CONFLICT_SKEL itself must not be NULL.)

   If STATE is non-null, set *STATE to the state of the local properties
   after the merge, one of:

     svn_wc_notify_state_unchanged
     svn_wc_notify_state_changed
     svn_wc_notify_state_merged
     svn_wc_notify_state_conflicted
 */
svn_error_t *
svn_wc__merge_props(svn_skel_t **conflict_skel,
                    svn_wc_notify_state_t *state,
                    apr_hash_t **new_actual_props,
                    svn_wc__db_t *db,
                    const char *local_abspath,
                    /*const*/ apr_hash_t *server_baseprops,
                    /*const*/ apr_hash_t *pristine_props,
                    /*const*/ apr_hash_t *actual_props,
                    const apr_array_header_t *propchanges,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool);


/* Given PROPERTIES is array of @c svn_prop_t structures. Returns TRUE if any
   of the PROPERTIES are the known "magic" ones that might require
   changing the working file. */
svn_boolean_t svn_wc__has_magic_property(const apr_array_header_t *properties);

/* Set *MODIFIED_P TRUE if the props for LOCAL_ABSPATH have been modified. */
svn_error_t *
svn_wc__props_modified(svn_boolean_t *modified_p,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool);

/* Internal version of svn_wc_prop_list2().  */
svn_error_t *
svn_wc__get_actual_props(apr_hash_t **props,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);

/* Creates a property reject file at *TMP_PREJFILE_ABSPATH, with
   either the property conflict data from DB (when PROP_CONFLICT_DATA
   is NULL) or the information in PROP_CONFLICT_DATA if it isn't.
 */
svn_error_t *
svn_wc__create_prejfile(const char **tmp_prejfile_abspath,
                        svn_wc__db_t *db,
                        const char *local_abspath,
                        const svn_skel_t *prop_conflict_data,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_PROPS_H */
