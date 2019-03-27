/*
 * translate.h :  eol and keyword translation
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


#ifndef SVN_LIBSVN_WC_TRANSLATE_H
#define SVN_LIBSVN_WC_TRANSLATE_H

#include <apr_pools.h>
#include "svn_types.h"
#include "svn_subst.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Newline and keyword translation properties */

/* If EOL is not-NULL query the SVN_PROP_EOL_STYLE property on file
   LOCAL_ABSPATH in DB.  If STYLE is non-null, set *STYLE to LOCAL_ABSPATH's
   eol style.  Set *EOL to

      - NULL for svn_subst_eol_style_none, or

      - a null-terminated C string containing the native eol marker
        for this platform, for svn_subst_eol_style_native, or

      - a null-terminated C string containing the eol marker indicated
        by the property value, for svn_subst_eol_style_fixed.

   If STYLE is null on entry, ignore it.  If *EOL is non-null on exit,
   it is a static string not allocated in POOL.

   If KEYWORDS is not NULL Expand keywords for the file at LOCAL_ABSPATH
   in DB, by parsing a whitespace-delimited list of keywords.  If any keywords
   are found in the list, allocate *KEYWORDS from RESULT_POOL and populate it
   with mappings from (const char *) keywords to their (svn_string_t *)
   values (also allocated in RESULT_POOL).

   If a keyword is in the list, but no corresponding value is
   available, do not create a hash entry for it.  If no keywords are
   found in the list, or if there is no list, set *KEYWORDS to NULL.

   If SPECIAL is not NULL determine if the svn:special flag is set on
   LOCAL_ABSPATH in DB.  If so, set SPECIAL to TRUE, if not, set it to FALSE.

   If PROPS is not NULL, use PROPS instead of the properties on LOCAL_ABSPATH.

   If WRI_ABSPATH is not NULL, retrieve the information for LOCAL_ABSPATH
   from the working copy identified by WRI_ABSPATH. Falling back to file
   external information if the file is not present as versioned node.

   If FOR_NORMALIZATION is TRUE, just return a list of keywords instead of
   calculating their intended values.

   Use SCRATCH_POOL for temporary allocation, RESULT_POOL for allocating
   *STYLE and *EOL.
*/
svn_error_t *
svn_wc__get_translate_info(svn_subst_eol_style_t *style,
                           const char **eol,
                           apr_hash_t **keywords,
                           svn_boolean_t *special,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           apr_hash_t *props,
                           svn_boolean_t for_normalization,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/* Reverse parser.  Given a real EOL string ("\n", "\r", or "\r\n"),
   return an encoded *VALUE ("LF", "CR", "CRLF") that one might see in
   the property value. */
void svn_wc__eol_value_from_string(const char **value,
                                   const char *eol);

/* Expand keywords for the file at LOCAL_ABSPATH in DB, by parsing a
   whitespace-delimited list of keywords KEYWORD_LIST.  If any keywords
   are found in the list, allocate *KEYWORDS from RESULT_POOL and populate
   it with mappings from (const char *) keywords to their (svn_string_t *)
   values (also allocated in RESULT_POOL).

   If a keyword is in the list, but no corresponding value is
   available, do not create a hash entry for it.  If no keywords are
   found in the list, or if there is no list, set *KEYWORDS to NULL.
     ### THIS LOOKS WRONG -- it creates a hash entry for every recognized kw
         and expands each missing value as an empty string or "-1" or similar.

   Use LOCAL_ABSPATH to expand keyword values.

   If WRI_ABSPATH is not NULL, retrieve the information for LOCAL_ABSPATH
   from the working copy identified by WRI_ABSPATH. Falling back to file
   external information if the file is not present as versioned node.
     ### THIS IS NOT IMPLEMENTED -- WRI_ABSPATH is ignored

   If FOR_NORMALIZATION is TRUE, just return a list of keywords instead of
   calculating their intended values.
     ### This would be better done by a separate API, since in this case
         only the KEYWORD_LIST input parameter is needed. (And there is no
         need to print "-1" as the revision value.)

   Use SCRATCH_POOL for any temporary allocations.
*/
svn_error_t *
svn_wc__expand_keywords(apr_hash_t **keywords,
                        svn_wc__db_t *db,
                        const char *local_abspath,
                        const char *wri_abspath,
                        const char *keyword_list,
                        svn_boolean_t for_normalization,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Sync the write and execute bit for LOCAL_ABSPATH with what is currently
   indicated by the properties in the database:

    * If the SVN_PROP_NEEDS_LOCK property is present and there is no
      lock token for the file in the working copy, set LOCAL_ABSPATH to
      read-only.
    * If the SVN_PROP_EXECUTABLE property is present at all, then set
      LOCAL_ABSPATH executable.

   If DID_SET is non-null, then liberally set *DID_SET to TRUE if we might
   have change the permissions on LOCAL_ABSPATH.  (A TRUE value in *DID_SET
   does not guarantee that we changed the permissions, simply that more
   investigation is warrented.)

   This function looks at the current values of the above properties,
   including any scheduled-but-not-yet-committed changes.

   If LOCAL_ABSPATH is a directory, this function is a no-op.

   Use SCRATCH_POOL for any temporary allocations.
 */
svn_error_t *
svn_wc__sync_flags_with_props(svn_boolean_t *did_set,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *scratch_pool);

/* Internal version of svn_wc_translated_stream2(), which see. */
svn_error_t *
svn_wc__internal_translated_stream(svn_stream_t **stream,
                                   svn_wc__db_t *db,
                                   const char *local_abspath,
                                   const char *versioned_abspath,
                                   apr_uint32_t flags,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool);

/* Like svn_wc_translated_file2(), except the working copy database
 * is used directly and the function assumes abspaths. */
svn_error_t *
svn_wc__internal_translated_file(const char **xlated_abspath,
                                 const char *src_abspath,
                                 svn_wc__db_t *db,
                                 const char *versioned_abspath,
                                 apr_uint32_t flags,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_TRANSLATE_H */
