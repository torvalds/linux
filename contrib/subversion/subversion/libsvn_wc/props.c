/*
 * props.c :  routines dealing with properties in the working copy
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



#include <stdlib.h>
#include <string.h>

#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include <apr_general.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_props.h"
#include "svn_io.h"
#include "svn_hash.h"
#include "svn_mergeinfo.h"
#include "svn_wc.h"
#include "svn_utf.h"
#include "svn_diff.h"
#include "svn_sorts.h"

#include "private/svn_wc_private.h"
#include "private/svn_mergeinfo_private.h"
#include "private/svn_skel.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"

#include "wc.h"
#include "props.h"
#include "translate.h"
#include "workqueue.h"
#include "conflicts.h"

#include "svn_private_config.h"

/*---------------------------------------------------------------------*/

/*** Merging propchanges into the working copy ***/


/* Parse FROM_PROP_VAL and TO_PROP_VAL into mergeinfo hashes, and
   calculate the deltas between them. */
static svn_error_t *
diff_mergeinfo_props(svn_mergeinfo_t *deleted, svn_mergeinfo_t *added,
                     const svn_string_t *from_prop_val,
                     const svn_string_t *to_prop_val, apr_pool_t *pool)
{
  if (svn_string_compare(from_prop_val, to_prop_val))
    {
      /* Don't bothering parsing identical mergeinfo. */
      *deleted = apr_hash_make(pool);
      *added = apr_hash_make(pool);
    }
  else
    {
      svn_mergeinfo_t from, to;
      SVN_ERR(svn_mergeinfo_parse(&from, from_prop_val->data, pool));
      SVN_ERR(svn_mergeinfo_parse(&to, to_prop_val->data, pool));
      SVN_ERR(svn_mergeinfo_diff2(deleted, added, from, to,
                                  TRUE, pool, pool));
    }
  return SVN_NO_ERROR;
}

/* Parse the mergeinfo from PROP_VAL1 and PROP_VAL2, combine it, then
   reconstitute it into *OUTPUT.  Call when the WC's mergeinfo has
   been modified to combine it with incoming mergeinfo from the
   repos. */
static svn_error_t *
combine_mergeinfo_props(const svn_string_t **output,
                        const svn_string_t *prop_val1,
                        const svn_string_t *prop_val2,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_mergeinfo_t mergeinfo1, mergeinfo2;
  svn_string_t *mergeinfo_string;

  SVN_ERR(svn_mergeinfo_parse(&mergeinfo1, prop_val1->data, scratch_pool));
  SVN_ERR(svn_mergeinfo_parse(&mergeinfo2, prop_val2->data, scratch_pool));
  SVN_ERR(svn_mergeinfo_merge2(mergeinfo1, mergeinfo2, scratch_pool,
                               scratch_pool));
  SVN_ERR(svn_mergeinfo_to_string(&mergeinfo_string, mergeinfo1, result_pool));
  *output = mergeinfo_string;
  return SVN_NO_ERROR;
}

/* Perform a 3-way merge operation on mergeinfo.  FROM_PROP_VAL is
   the "base" property value, WORKING_PROP_VAL is the current value,
   and TO_PROP_VAL is the new value. */
static svn_error_t *
combine_forked_mergeinfo_props(const svn_string_t **output,
                               const svn_string_t *from_prop_val,
                               const svn_string_t *working_prop_val,
                               const svn_string_t *to_prop_val,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_mergeinfo_t from_mergeinfo, l_deleted, l_added, r_deleted, r_added;
  svn_string_t *mergeinfo_string;

  /* ### OPTIMIZE: Use from_mergeinfo when diff'ing. */
  SVN_ERR(diff_mergeinfo_props(&l_deleted, &l_added, from_prop_val,
                               working_prop_val, scratch_pool));
  SVN_ERR(diff_mergeinfo_props(&r_deleted, &r_added, from_prop_val,
                               to_prop_val, scratch_pool));
  SVN_ERR(svn_mergeinfo_merge2(l_deleted, r_deleted,
                               scratch_pool, scratch_pool));
  SVN_ERR(svn_mergeinfo_merge2(l_added, r_added,
                               scratch_pool, scratch_pool));

  /* Apply the combined deltas to the base. */
  SVN_ERR(svn_mergeinfo_parse(&from_mergeinfo, from_prop_val->data,
                              scratch_pool));
  SVN_ERR(svn_mergeinfo_merge2(from_mergeinfo, l_added,
                               scratch_pool, scratch_pool));

  SVN_ERR(svn_mergeinfo_remove2(&from_mergeinfo, l_deleted, from_mergeinfo,
                                TRUE, scratch_pool, scratch_pool));

  SVN_ERR(svn_mergeinfo_to_string(&mergeinfo_string, from_mergeinfo,
                                  result_pool));
  *output = mergeinfo_string;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_merge_props3(svn_wc_notify_state_t *state,
                    svn_wc_context_t *wc_ctx,
                    const char *local_abspath,
                    const svn_wc_conflict_version_t *left_version,
                    const svn_wc_conflict_version_t *right_version,
                    apr_hash_t *baseprops,
                    const apr_array_header_t *propchanges,
                    svn_boolean_t dry_run,
                    svn_wc_conflict_resolver_func2_t conflict_func,
                    void *conflict_baton,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *scratch_pool)
{
  int i;
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  apr_hash_t *pristine_props = NULL;
  apr_hash_t *actual_props;
  apr_hash_t *new_actual_props;
  svn_boolean_t had_props, props_mod;
  svn_boolean_t have_base;
  svn_boolean_t conflicted;
  svn_skel_t *work_items = NULL;
  svn_skel_t *conflict_skel = NULL;
  svn_wc__db_t *db = wc_ctx->db;

  /* IMPORTANT: svn_wc_merge_prop_diffs relies on the fact that baseprops
     may be NULL. */

  SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, &conflicted, NULL,
                               &had_props, &props_mod, &have_base, NULL, NULL,
                               db, local_abspath,
                               scratch_pool, scratch_pool));

  /* Checks whether the node exists and returns the hidden flag */
  if (status == svn_wc__db_status_not_present
      || status == svn_wc__db_status_server_excluded
      || status == svn_wc__db_status_excluded)
    {
      return svn_error_createf(
                    SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                    _("The node '%s' was not found."),
                    svn_dirent_local_style(local_abspath, scratch_pool));
    }
  else if (status != svn_wc__db_status_normal
           && status != svn_wc__db_status_added
           && status != svn_wc__db_status_incomplete)
    {
      return svn_error_createf(
                    SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                    _("The node '%s' does not have properties in this state."),
                    svn_dirent_local_style(local_abspath, scratch_pool));
    }
  else if (conflicted)
      {
        svn_boolean_t text_conflicted;
        svn_boolean_t prop_conflicted;
        svn_boolean_t tree_conflicted;

        SVN_ERR(svn_wc__internal_conflicted_p(&text_conflicted,
                                              &prop_conflicted,
                                              &tree_conflicted,
                                              db, local_abspath,
                                              scratch_pool));

        /* We can't install two text/prop conflicts on a single node, so
           avoid even checking that we have to merge it */
        if (text_conflicted || prop_conflicted || tree_conflicted)
          {
            return svn_error_createf(
                            SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                            _("Can't merge into conflicted node '%s'"),
                            svn_dirent_local_style(local_abspath,
                                                   scratch_pool));
          }
        /* else: Conflict was resolved by removing markers */
      }

  /* The PROPCHANGES may not have non-"normal" properties in it. If entry
     or wc props were allowed, then the following code would install them
     into the BASE and/or WORKING properties(!).  */
  for (i = propchanges->nelts; i--; )
    {
      const svn_prop_t *change = &APR_ARRAY_IDX(propchanges, i, svn_prop_t);

      if (!svn_wc_is_normal_prop(change->name))
        return svn_error_createf(SVN_ERR_BAD_PROP_KIND, NULL,
                                 _("The property '%s' may not be merged "
                                   "into '%s'."),
                                 change->name,
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool));
    }

  if (had_props)
    SVN_ERR(svn_wc__db_read_pristine_props(&pristine_props, db, local_abspath,
                                           scratch_pool, scratch_pool));
  if (pristine_props == NULL)
    pristine_props = apr_hash_make(scratch_pool);

  if (props_mod)
    SVN_ERR(svn_wc__get_actual_props(&actual_props, db, local_abspath,
                                     scratch_pool, scratch_pool));
  else
    actual_props = pristine_props;

  /* Note that while this routine does the "real" work, it's only
     prepping tempfiles and writing log commands.  */
  SVN_ERR(svn_wc__merge_props(&conflict_skel, state,
                              &new_actual_props,
                              db, local_abspath,
                              baseprops /* server_baseprops */,
                              pristine_props,
                              actual_props,
                              propchanges,
                              scratch_pool, scratch_pool));

  if (dry_run)
    {
      return SVN_NO_ERROR;
    }

  {
    const char *dir_abspath;

    if (kind == svn_node_dir)
      dir_abspath = local_abspath;
    else
      dir_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

    /* Verify that we're holding this directory's write lock.  */
    SVN_ERR(svn_wc__write_check(db, dir_abspath, scratch_pool));
  }

  if (conflict_skel)
    {
      svn_skel_t *work_item;
      SVN_ERR(svn_wc__conflict_skel_set_op_merge(conflict_skel,
                                                 left_version,
                                                 right_version,
                                                 scratch_pool,
                                                 scratch_pool));

      SVN_ERR(svn_wc__conflict_create_markers(&work_item,
                                              db, local_abspath,
                                              conflict_skel,
                                              scratch_pool, scratch_pool));

      work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);
    }

  /* After a (not-dry-run) merge, we ALWAYS have props to save.  */
  SVN_ERR_ASSERT(new_actual_props != NULL);

  SVN_ERR(svn_wc__db_op_set_props(db, local_abspath, new_actual_props,
                                  svn_wc__has_magic_property(propchanges),
                                  conflict_skel,
                                  work_items,
                                  scratch_pool));

  if (work_items != NULL)
    SVN_ERR(svn_wc__wq_run(db, local_abspath, cancel_func, cancel_baton,
                           scratch_pool));

  /* If there is a conflict, try to resolve it. */
  if (conflict_skel && conflict_func)
    {
      svn_boolean_t prop_conflicted;

      SVN_ERR(svn_wc__conflict_invoke_resolver(db, local_abspath, kind,
                                               conflict_skel,
                                               NULL /* merge_options */,
                                               conflict_func, conflict_baton,
                                               cancel_func, cancel_baton,
                                               scratch_pool));

      /* Reset *STATE if all prop conflicts were resolved. */
      SVN_ERR(svn_wc__internal_conflicted_p(
                NULL, &prop_conflicted, NULL,
                wc_ctx->db, local_abspath, scratch_pool));
      if (! prop_conflicted)
        *state = svn_wc_notify_state_merged;
    }

  return SVN_NO_ERROR;
}


/* Generate a message to describe the property conflict among these four
   values.

   Note that this function (currently) interprets the property values as
   strings, but they could actually be binary values. We'll keep the
   types as svn_string_t in case we fix this in the future.  */
static svn_stringbuf_t *
generate_conflict_message(const char *propname,
                          const svn_string_t *original,
                          const svn_string_t *mine,
                          const svn_string_t *incoming,
                          const svn_string_t *incoming_base,
                          apr_pool_t *result_pool)
{
  if (incoming_base == NULL)
    {
      /* Attempting to add the value INCOMING.  */
      SVN_ERR_ASSERT_NO_RETURN(incoming != NULL);

      if (mine)
        {
          /* To have a conflict, these must be different.  */
          SVN_ERR_ASSERT_NO_RETURN(!svn_string_compare(mine, incoming));

          /* Note that we don't care whether MINE is locally-added or
             edited, or just something different that is a copy of the
             pristine ORIGINAL.  */
          return svn_stringbuf_createf(result_pool,
                                       _("Trying to add new property '%s'\n"
                                         "but the property already exists.\n"),
                                       propname);
        }

      /* To have a conflict, we must have an ORIGINAL which has been
         locally-deleted.  */
      SVN_ERR_ASSERT_NO_RETURN(original != NULL);
      return svn_stringbuf_createf(result_pool,
                                   _("Trying to add new property '%s'\n"
                                     "but the property has been locally "
                                     "deleted.\n"),
                                   propname);
    }

  if (incoming == NULL)
    {
      /* Attempting to delete the value INCOMING_BASE.  */
      SVN_ERR_ASSERT_NO_RETURN(incoming_base != NULL);

      /* Are we trying to delete a local addition? */
      if (original == NULL && mine != NULL)
        return svn_stringbuf_createf(result_pool,
                                     _("Trying to delete property '%s'\n"
                                       "but the property has been locally "
                                       "added.\n"),
                                     propname);

      /* A conflict can only occur if we originally had the property;
         otherwise, we would have merged the property-delete into the
         non-existent property.  */
      SVN_ERR_ASSERT_NO_RETURN(original != NULL);

      if (svn_string_compare(original, incoming_base))
        {
          if (mine)
            /* We were trying to delete the correct property, but an edit
               caused the conflict.  */
            return svn_stringbuf_createf(result_pool,
                                         _("Trying to delete property '%s'\n"
                                           "but the property has been locally "
                                           "modified.\n"),
                                         propname);
        }
      else if (mine == NULL)
        {
          /* We were trying to delete the property, but we have locally
             deleted the same property, but with a different value. */
          return svn_stringbuf_createf(result_pool,
                                       _("Trying to delete property '%s'\n"
                                         "but the property has been locally "
                                         "deleted and had a different "
                                         "value.\n"),
                                       propname);
        }

      /* We were trying to delete INCOMING_BASE but our ORIGINAL is
         something else entirely.  */
      SVN_ERR_ASSERT_NO_RETURN(!svn_string_compare(original, incoming_base));

      return svn_stringbuf_createf(result_pool,
                                   _("Trying to delete property '%s'\n"
                                     "but the local property value is "
                                     "different.\n"),
                                   propname);
    }

  /* Attempting to change the property from INCOMING_BASE to INCOMING.  */

  /* If we have a (current) property value, then it should be different
     from the INCOMING_BASE; otherwise, the incoming change would have
     been applied to it.  */
  SVN_ERR_ASSERT_NO_RETURN(!mine || !svn_string_compare(mine, incoming_base));

  if (original && mine && svn_string_compare(original, mine))
    {
      /* We have an unchanged property, so the original values must
         have been different.  */
      SVN_ERR_ASSERT_NO_RETURN(!svn_string_compare(original, incoming_base));
      return svn_stringbuf_createf(result_pool,
                                   _("Trying to change property '%s'\n"
                                     "but the local property value conflicts "
                                     "with the incoming change.\n"),
                                   propname);
    }

  if (original && mine)
    return svn_stringbuf_createf(result_pool,
                                 _("Trying to change property '%s'\n"
                                   "but the property has already been locally "
                                   "changed to a different value.\n"),
                                 propname);

  if (original)
    return svn_stringbuf_createf(result_pool,
                                 _("Trying to change property '%s'\nbut "
                                   "the property has been locally deleted.\n"),
                                 propname);

  if (mine)
    return svn_stringbuf_createf(result_pool,
                                 _("Trying to change property '%s'\nbut the "
                                   "property has been locally added with a "
                                   "different value.\n"),
                                 propname);

  return svn_stringbuf_createf(result_pool,
                               _("Trying to change property '%s'\nbut "
                                 "the property does not exist locally.\n"),
                               propname);
}


/* SKEL will be one of:

   ()
   (VALUE)

   Return NULL for the former (the particular property value was not
   present), and VALUE for the second.  */
static const svn_string_t *
maybe_prop_value(const svn_skel_t *skel,
                 apr_pool_t *result_pool)
{
  if (skel->children == NULL)
    return NULL;

  return svn_string_ncreate(skel->children->data,
                            skel->children->len,
                            result_pool);
}


/* Create a property rejection description for the specified property.
   The result will be allocated in RESULT_POOL. */
static svn_error_t *
prop_conflict_new(const svn_string_t **conflict_desc,
                  const char *propname,
                  const svn_string_t *original,
                  const svn_string_t *mine,
                  const svn_string_t *incoming,
                  const svn_string_t *incoming_base,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_diff_t *diff;
  svn_diff_file_options_t *diff_opts;
  svn_stringbuf_t *buf;
  svn_boolean_t incoming_base_is_binary;
  svn_boolean_t mine_is_binary;
  svn_boolean_t incoming_is_binary;

  buf = generate_conflict_message(propname, original, mine, incoming,
                                  incoming_base, scratch_pool);

  /* Convert deleted or not-yet-added values to empty-string values, for the
     purposes of diff generation and binary detection. */
  if (mine == NULL)
    mine = svn_string_create_empty(scratch_pool);
  if (incoming == NULL)
    incoming = svn_string_create_empty(scratch_pool);
  if (incoming_base == NULL)
    incoming_base = svn_string_create_empty(scratch_pool);

  /* How we render the conflict:

     We have four sides: original, mine, incoming_base, incoming.
     We render the conflict as a 3-way diff.  A diff3 API has three parts,
     called:

       <<< - original
       ||| - modified (or "older")
       === - latest (or "theirs")
       >>>

     We fill those parts as follows:

       PART      FILLED BY SKEL MEMBER  USER-FACING ROLE
       ====      =====================  ================
       original  mine                   was WORKING tree at conflict creation
       modified  incoming_base          left-hand side of merge
       latest    incoming               right-hand side of merge
       (none)    original               was BASE tree at conflict creation

     An 'update -r rN' is treated like a 'merge -r BASE:rN', i.e., in an
     'update' operation skel->original and skel->incoming_base coincide.

     Note that the term "original" is used both in the skel and in diff3
     with different meanings.  Note also that the skel's ORIGINAL value was
     at some point in the BASE tree, but the BASE tree need not have contained
     the INCOMING_BASE value.

     Yes, it's confusing. */

  /* If any of the property values involved in the diff is binary data,
   * do not generate a diff. */
  incoming_base_is_binary = svn_io_is_binary_data(incoming_base->data,
                                                  incoming_base->len);
  mine_is_binary = svn_io_is_binary_data(mine->data, mine->len);
  incoming_is_binary = svn_io_is_binary_data(incoming->data, incoming->len);

  if (!(incoming_base_is_binary || mine_is_binary || incoming_is_binary))
    {
      diff_opts = svn_diff_file_options_create(scratch_pool);
      diff_opts->ignore_space = svn_diff_file_ignore_space_none;
      diff_opts->ignore_eol_style = FALSE;
      diff_opts->show_c_function = FALSE;
      /* Pass skel member INCOMING_BASE into the formal parameter ORIGINAL.
         Ignore the skel member ORIGINAL. */
      SVN_ERR(svn_diff_mem_string_diff3(&diff, incoming_base, mine, incoming,
                                        diff_opts, scratch_pool));
      if (svn_diff_contains_conflicts(diff))
        {
          svn_stream_t *stream;
          svn_diff_conflict_display_style_t style;
          const char *mine_marker = _("<<<<<<< (local property value)");
          const char *incoming_marker = _(">>>>>>> (incoming 'changed to' value)");
          const char *incoming_base_marker = _("||||||| (incoming 'changed from' value)");
          const char *separator = "=======";
          svn_string_t *incoming_base_ascii =
            svn_string_create(svn_utf_cstring_from_utf8_fuzzy(incoming_base->data,
                                                              scratch_pool),
                              scratch_pool);
          svn_string_t *mine_ascii =
            svn_string_create(svn_utf_cstring_from_utf8_fuzzy(mine->data,
                                                              scratch_pool),
                              scratch_pool);
          svn_string_t *incoming_ascii =
            svn_string_create(svn_utf_cstring_from_utf8_fuzzy(incoming->data,
                                                              scratch_pool),
                              scratch_pool);

          style = svn_diff_conflict_display_modified_original_latest;
          stream = svn_stream_from_stringbuf(buf, scratch_pool);
          SVN_ERR(svn_stream_skip(stream, buf->len));
          SVN_ERR(svn_diff_mem_string_output_merge3(stream, diff,
                                                    incoming_base_ascii,
                                                    mine_ascii,
                                                    incoming_ascii,
                                                    incoming_base_marker, mine_marker,
                                                    incoming_marker, separator,
                                                    style,
                                                    cancel_func, cancel_baton,
                                                    scratch_pool));
          SVN_ERR(svn_stream_close(stream));

          *conflict_desc = svn_string_create_from_buf(buf, result_pool);
          return SVN_NO_ERROR;
        }
    }

  /* If we could not print a conflict diff just print full values . */
  if (mine->len > 0)
    {
      svn_stringbuf_appendcstr(buf, _("Local property value:\n"));
      if (mine_is_binary)
        svn_stringbuf_appendcstr(buf, _("Cannot display: property value is "
                                        "binary data\n"));
      else
        svn_stringbuf_appendbytes(buf, mine->data, mine->len);
      svn_stringbuf_appendcstr(buf, "\n");
    }

  if (incoming->len > 0)
    {
      svn_stringbuf_appendcstr(buf, _("Incoming property value:\n"));
      if (incoming_is_binary)
        svn_stringbuf_appendcstr(buf, _("Cannot display: property value is "
                                        "binary data\n"));
      else
        svn_stringbuf_appendbytes(buf, incoming->data, incoming->len);
      svn_stringbuf_appendcstr(buf, "\n");
    }

  *conflict_desc = svn_string_create_from_buf(buf, result_pool);
  return SVN_NO_ERROR;
}

/* Parse a property conflict description from the provided SKEL.
   The result includes a descriptive message (see generate_conflict_message)
   and maybe a diff of property values containing conflict markers.
   The result will be allocated in RESULT_POOL.

   Note: SKEL is a single property conflict of the form:

   ("prop" ([ORIGINAL]) ([MINE]) ([INCOMING]) ([INCOMING_BASE]))

   Note: This is not the same format as the property conflicts we store in
   wc.db since 1.8. This is the legacy format used in the Workqueue in 1.7-1.8 */
static svn_error_t *
prop_conflict_from_skel(const svn_string_t **conflict_desc,
                        const svn_skel_t *skel,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  const svn_string_t *original;
  const svn_string_t *mine;
  const svn_string_t *incoming;
  const svn_string_t *incoming_base;
  const char *propname;

  /* Navigate to the property name.  */
  skel = skel->children->next;

  /* We need to copy these into SCRATCH_POOL in order to nul-terminate
     the values.  */
  propname = apr_pstrmemdup(scratch_pool, skel->data, skel->len);
  original = maybe_prop_value(skel->next, scratch_pool);
  mine = maybe_prop_value(skel->next->next, scratch_pool);
  incoming = maybe_prop_value(skel->next->next->next, scratch_pool);
  incoming_base = maybe_prop_value(skel->next->next->next->next, scratch_pool);

  return svn_error_trace(prop_conflict_new(conflict_desc,
                                           propname,
                                           original, mine,
                                           incoming, incoming_base,
                                           cancel_func, cancel_baton,
                                           result_pool, scratch_pool));
}

/* Create a property conflict file at PREJFILE based on the property
   conflicts in CONFLICT_SKEL.  */
svn_error_t *
svn_wc__create_prejfile(const char **tmp_prejfile_abspath,
                        svn_wc__db_t *db,
                        const char *local_abspath,
                        const svn_skel_t *prop_conflict_data,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  const char *tempdir_abspath;
  svn_stream_t *stream;
  const char *temp_abspath;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  const svn_skel_t *scan;

  SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&tempdir_abspath,
                                         db, local_abspath,
                                         iterpool, iterpool));

  SVN_ERR(svn_stream_open_unique(&stream, &temp_abspath,
                                 tempdir_abspath, svn_io_file_del_none,
                                 scratch_pool, iterpool));

  if (prop_conflict_data)
    {
      for (scan = prop_conflict_data->children->next;
            scan != NULL; scan = scan->next)
        {
          const svn_string_t *conflict_desc;

          svn_pool_clear(iterpool);

          SVN_ERR(prop_conflict_from_skel(&conflict_desc, scan,
                                          cancel_func, cancel_baton,
                                          iterpool, iterpool));

          SVN_ERR(svn_stream_puts(stream, conflict_desc->data));
        }
    }
  else
    {
      svn_wc_operation_t operation;
      apr_hash_index_t *hi;
      apr_hash_t *old_props;
      apr_hash_t *mine_props;
      apr_hash_t *their_original_props;
      apr_hash_t *their_props;
      apr_hash_t *conflicted_props;
      svn_skel_t *conflicts;

      SVN_ERR(svn_wc__db_read_conflict(&conflicts, NULL, NULL,
                                       db, local_abspath,
                                      scratch_pool, scratch_pool));

      SVN_ERR(svn_wc__conflict_read_info(&operation, NULL, NULL, NULL, NULL,
                                         db, local_abspath,
                                         conflicts,
                                         scratch_pool, scratch_pool));

      SVN_ERR(svn_wc__conflict_read_prop_conflict(NULL,
                                                  &mine_props,
                                                  &their_original_props,
                                                  &their_props,
                                                  &conflicted_props,
                                                  db, local_abspath,
                                                  conflicts,
                                                  scratch_pool,
                                                  scratch_pool));

      if (operation == svn_wc_operation_merge)
        SVN_ERR(svn_wc__db_read_pristine_props(&old_props, db, local_abspath,
                                                scratch_pool, scratch_pool));
      else
        old_props = their_original_props;

      /* ### TODO: Sort conflicts? */
      for (hi = apr_hash_first(scratch_pool, conflicted_props);
           hi;
           hi = apr_hash_next(hi))
        {
          const svn_string_t *conflict_desc;
          const char *propname = apr_hash_this_key(hi);
          const svn_string_t *old_value;
          const svn_string_t *mine_value;
          const svn_string_t *their_value;
          const svn_string_t *their_original_value;

          svn_pool_clear(iterpool);

          old_value = old_props ? svn_hash_gets(old_props, propname) : NULL;
          mine_value = mine_props ? svn_hash_gets(mine_props, propname) : NULL;
          their_value = their_props ? svn_hash_gets(their_props, propname)
                                    : NULL;
          their_original_value = their_original_props
                                    ? svn_hash_gets(their_original_props, propname)
                                    : NULL;

          SVN_ERR(prop_conflict_new(&conflict_desc,
                                    propname, old_value, mine_value,
                                    their_value, their_original_value,
                                    cancel_func, cancel_baton,
                                    iterpool, iterpool));

          SVN_ERR(svn_stream_puts(stream, conflict_desc->data));
        }
    }

  SVN_ERR(svn_stream_close(stream));

  svn_pool_destroy(iterpool);

  *tmp_prejfile_abspath = apr_pstrdup(result_pool, temp_abspath);
  return SVN_NO_ERROR;
}


/* Set the value of *STATE to NEW_VALUE if STATE is not NULL
 * and NEW_VALUE is a higer order value than *STATE's current value
 * using this ordering (lower order first):
 *
 * - unknown, unchanged, inapplicable
 * - changed
 * - merged
 * - missing
 * - obstructed
 * - conflicted
 *
 */
static void
set_prop_merge_state(svn_wc_notify_state_t *state,
                     svn_wc_notify_state_t new_value)
{
  static char ordering[] =
    { svn_wc_notify_state_unknown,
      svn_wc_notify_state_unchanged,
      svn_wc_notify_state_inapplicable,
      svn_wc_notify_state_changed,
      svn_wc_notify_state_merged,
      svn_wc_notify_state_obstructed,
      svn_wc_notify_state_conflicted };
  int state_pos = 0, i;

  if (! state)
    return;

  /* Find *STATE in our ordering */
  for (i = 0; i < sizeof(ordering); i++)
    {
      if (*state == ordering[i])
        {
          state_pos = i;
          break;
        }
    }

  /* Find NEW_VALUE in our ordering
   * We don't need to look further than where we found *STATE though:
   * If we find our value, it's order is too low.
   * If we don't find it, we'll want to set it, no matter its order.
   */

  for (i = 0; i <= state_pos; i++)
    {
      if (new_value == ordering[i])
        return;
    }

  *state = new_value;
}

/* Apply the addition of a property with name PROPNAME and value NEW_VAL to
 * the existing property with value WORKING_VAL, that originally had value
 * PRISTINE_VAL.
 *
 * Sets *RESULT_VAL to the resulting value.
 * Sets *CONFLICT_REMAINS to TRUE if the change caused a conflict.
 * Sets *DID_MERGE to true if the result is caused by a merge
 */
static svn_error_t *
apply_single_prop_add(const svn_string_t **result_val,
                      svn_boolean_t *conflict_remains,
                      svn_boolean_t *did_merge,
                      const char *propname,
                      const svn_string_t *pristine_val,
                      const svn_string_t *new_val,
                      const svn_string_t *working_val,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)

{
  *conflict_remains = FALSE;

  if (working_val)
    {
      /* the property already exists in actual_props... */

      if (svn_string_compare(working_val, new_val))
        /* The value we want is already there, so it's a merge. */
        *did_merge = TRUE;

      else
        {
          svn_boolean_t merged_prop = FALSE;

          /* The WC difference doesn't match the new value.
           We only merge mergeinfo;  other props conflict */
          if (strcmp(propname, SVN_PROP_MERGEINFO) == 0)
            {
              const svn_string_t *merged_val;
              svn_error_t *err = combine_mergeinfo_props(&merged_val,
                                                         working_val,
                                                         new_val,
                                                         result_pool,
                                                         scratch_pool);

              /* Issue #3896 'mergeinfo syntax errors should be treated
                 gracefully': If bogus mergeinfo is present we can't
                 merge intelligently, so raise a conflict instead. */
              if (err)
                {
                  if (err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
                    svn_error_clear(err);
                  else
                    return svn_error_trace(err);
                  }
              else
                {
                  merged_prop = TRUE;
                  *result_val = merged_val;
                  *did_merge = TRUE;
                }
            }

          if (!merged_prop)
            *conflict_remains = TRUE;
        }
    }
  else if (pristine_val)
    *conflict_remains = TRUE;
  else  /* property doesn't yet exist in actual_props...  */
    /* so just set it */
    *result_val = new_val;

  return SVN_NO_ERROR;
}


/* Apply the deletion of a property to the existing
 * property with value WORKING_VAL, that originally had value PRISTINE_VAL.
 *
 * Sets *RESULT_VAL to the resulting value.
 * Sets *CONFLICT_REMAINS to TRUE if the change caused a conflict.
 * Sets *DID_MERGE to true if the result is caused by a merge
 */
static svn_error_t *
apply_single_prop_delete(const svn_string_t **result_val,
                         svn_boolean_t *conflict_remains,
                         svn_boolean_t *did_merge,
                         const svn_string_t *base_val,
                         const svn_string_t *old_val,
                         const svn_string_t *working_val)
{
  *conflict_remains = FALSE;

  if (! base_val)
    {
      if (working_val
          && !svn_string_compare(working_val, old_val))
        {
          /* We are trying to delete a locally-added prop. */
          *conflict_remains = TRUE;
        }
      else
        {
          *result_val = NULL;
          if (old_val)
            /* This is a merge, merging a delete into non-existent
               property or a local addition of same prop value. */
            *did_merge = TRUE;
        }
    }

  else if (svn_string_compare(base_val, old_val))
    {
       if (working_val)
         {
           if (svn_string_compare(working_val, old_val))
             /* they have the same values, so it's an update */
             *result_val = NULL;
           else
             *conflict_remains = TRUE;
         }
       else
         /* The property is locally deleted from the same value, so it's
            a merge */
         *did_merge = TRUE;
    }

  else
    *conflict_remains = TRUE;

  return SVN_NO_ERROR;
}


/* Merge a change to the mergeinfo property. Similar to
   apply_single_prop_change(), except that the property name is always
   SVN_PROP_MERGEINFO. */
/* ### This function is extracted straight from the previous all-in-one
   version of apply_single_prop_change() by removing the code paths that
   were not followed for this property, but with no attempt to rationalize
   the remainder. */
static svn_error_t *
apply_single_mergeinfo_prop_change(const svn_string_t **result_val,
                                   svn_boolean_t *conflict_remains,
                                   svn_boolean_t *did_merge,
                                   const svn_string_t *base_val,
                                   const svn_string_t *old_val,
                                   const svn_string_t *new_val,
                                   const svn_string_t *working_val,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  if ((working_val && ! base_val)
      || (! working_val && base_val)
      || (working_val && base_val
          && !svn_string_compare(working_val, base_val)))
    {
      /* Locally changed property */
      if (working_val)
        {
          if (svn_string_compare(working_val, new_val))
            /* The new value equals the changed value: a no-op merge */
            *did_merge = TRUE;
          else
            {
              /* We have base, WC, and new values.  Discover
                 deltas between base <-> WC, and base <->
                 incoming.  Combine those deltas, and apply
                 them to base to get the new value. */
              SVN_ERR(combine_forked_mergeinfo_props(&new_val, old_val,
                                                     working_val,
                                                     new_val,
                                                     result_pool,
                                                     scratch_pool));
              *result_val = new_val;
              *did_merge = TRUE;
            }
        }
      else
        {
          /* There is a base_val but no working_val */
          *conflict_remains = TRUE;
        }
    }

  else if (! working_val) /* means !working_val && !base_val due
                             to conditions above: no prop at all */
    {
      /* Discover any mergeinfo additions in the
         incoming value relative to the base, and
         "combine" those with the empty WC value. */
      svn_mergeinfo_t deleted_mergeinfo, added_mergeinfo;
      svn_string_t *mergeinfo_string;

      SVN_ERR(diff_mergeinfo_props(&deleted_mergeinfo,
                                   &added_mergeinfo,
                                   old_val, new_val, scratch_pool));
      SVN_ERR(svn_mergeinfo_to_string(&mergeinfo_string,
                                      added_mergeinfo, result_pool));
      *result_val = mergeinfo_string;
    }

  else /* means working && base && svn_string_compare(working, base) */
    {
      if (svn_string_compare(old_val, base_val))
        *result_val = new_val;
      else
        {
          /* We have base, WC, and new values.  Discover
             deltas between base <-> WC, and base <->
             incoming.  Combine those deltas, and apply
             them to base to get the new value. */
          SVN_ERR(combine_forked_mergeinfo_props(&new_val, old_val,
                                                 working_val,
                                                 new_val, result_pool,
                                                 scratch_pool));
          *result_val = new_val;
          *did_merge = TRUE;
        }
    }

  return SVN_NO_ERROR;
}

/* Merge a change to a property, using the rule that if the working value
   is equal to the new value then there is nothing we need to do. Else, if
   the working value is the same as the old value then apply the change as a
   simple update (replacement), otherwise invoke maybe_generate_propconflict().
   The definition of the arguments and behaviour is the same as
   apply_single_prop_change(). */
static svn_error_t *
apply_single_generic_prop_change(const svn_string_t **result_val,
                                 svn_boolean_t *conflict_remains,
                                 svn_boolean_t *did_merge,
                                 const svn_string_t *old_val,
                                 const svn_string_t *new_val,
                                 const svn_string_t *working_val)
{
  SVN_ERR_ASSERT(old_val != NULL);

  /* If working_val is the same as new_val already then there is
   * nothing to do */
  if (working_val && new_val
      && svn_string_compare(working_val, new_val))
    {
      /* All values identical is a trivial, non-notifiable merge */
      if (! old_val || ! svn_string_compare(old_val, new_val))
        *did_merge = TRUE;
    }
  /* If working_val is the same as old_val... */
  else if (working_val && old_val
      && svn_string_compare(working_val, old_val))
    {
      /* A trivial update: change it to new_val. */
      *result_val = new_val;
    }
  else
    {
      /* Merge the change. */
      *conflict_remains = TRUE;
    }

  return SVN_NO_ERROR;
}

/* Change the property with name PROPNAME, setting *RESULT_VAL,
 * *CONFLICT_REMAINS and *DID_MERGE according to the merge outcome.
 *
 * BASE_VAL contains the working copy base property value. (May be null.)
 *
 * OLD_VAL contains the value of the property the server
 * thinks it's overwriting. (Not null.)
 *
 * NEW_VAL contains the value to be set. (Not null.)
 *
 * WORKING_VAL contains the working copy actual value. (May be null.)
 */
static svn_error_t *
apply_single_prop_change(const svn_string_t **result_val,
                         svn_boolean_t *conflict_remains,
                         svn_boolean_t *did_merge,
                         const char *propname,
                         const svn_string_t *base_val,
                         const svn_string_t *old_val,
                         const svn_string_t *new_val,
                         const svn_string_t *working_val,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_boolean_t merged_prop = FALSE;

  *conflict_remains = FALSE;

  /* Note: The purpose is to apply the change (old_val -> new_val) onto
     (working_val). There is no need for base_val to be involved in the
     process except as a bit of context to help the user understand and
     resolve any conflict. */

  /* Decide how to merge, based on whether we know anything special about
     the property. */
  if (strcmp(propname, SVN_PROP_MERGEINFO) == 0)
    {
      /* We know how to merge any mergeinfo property change...

         ...But Issue #3896 'mergeinfo syntax errors should be treated
         gracefully' might thwart us.  If bogus mergeinfo is present we
         can't merge intelligently, so let the standard method deal with
         it instead. */
      svn_error_t *err = apply_single_mergeinfo_prop_change(result_val,
                                                            conflict_remains,
                                                            did_merge,
                                                            base_val,
                                                            old_val,
                                                            new_val,
                                                            working_val,
                                                            result_pool,
                                                            scratch_pool);
       if (err)
         {
           if (err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
             svn_error_clear(err);
           else
             return svn_error_trace(err);
           }
       else
         {
           merged_prop = TRUE;
         }
    }

  if (!merged_prop)
    {
      /* The standard method: perform a simple update automatically, but
         pass any other kind of merge to maybe_generate_propconflict(). */

      SVN_ERR(apply_single_generic_prop_change(result_val, conflict_remains,
                                               did_merge,
                                               old_val, new_val, working_val));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__merge_props(svn_skel_t **conflict_skel,
                    svn_wc_notify_state_t *state,
                    apr_hash_t **new_actual_props,
                    svn_wc__db_t *db,
                    const char *local_abspath,
                    apr_hash_t *server_baseprops,
                    apr_hash_t *pristine_props,
                    apr_hash_t *actual_props,
                    const apr_array_header_t *propchanges,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  int i;
  apr_hash_t *conflict_props = NULL;
  apr_hash_t *their_props;

  SVN_ERR_ASSERT(pristine_props != NULL);
  SVN_ERR_ASSERT(actual_props != NULL);

  *new_actual_props = apr_hash_copy(result_pool, actual_props);

  if (!server_baseprops)
    server_baseprops = pristine_props;

  their_props = apr_hash_copy(scratch_pool, server_baseprops);

  if (state)
    {
      /* Start out assuming no changes or conflicts.  Don't bother to
         examine propchanges->nelts yet; even if we knew there were
         propchanges, we wouldn't yet know if they are "normal" props,
         as opposed wc or entry props.  */
      *state = svn_wc_notify_state_unchanged;
    }

  /* Looping over the array of incoming propchanges we want to apply: */
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < propchanges->nelts; i++)
    {
      const svn_prop_t *incoming_change
        = &APR_ARRAY_IDX(propchanges, i, svn_prop_t);
      const char *propname = incoming_change->name;
      const svn_string_t *base_val  /* Pristine in WC */
        = svn_hash_gets(pristine_props, propname);
      const svn_string_t *from_val  /* Merge left */
        = svn_hash_gets(server_baseprops, propname);
      const svn_string_t *to_val    /* Merge right */
        = incoming_change->value;
      const svn_string_t *working_val  /* Mine */
        = svn_hash_gets(actual_props, propname);
      const svn_string_t *result_val;
      svn_boolean_t conflict_remains;
      svn_boolean_t did_merge = FALSE;

      svn_pool_clear(iterpool);

      to_val = svn_string_dup(to_val, result_pool);

      svn_hash_sets(their_props, propname, to_val);


      /* We already know that state is at least `changed', so mark
         that, but remember that we may later upgrade to `merged' or
         even `conflicted'. */
      set_prop_merge_state(state, svn_wc_notify_state_changed);

      result_val = working_val;

      if (! from_val)  /* adding a new property */
        SVN_ERR(apply_single_prop_add(&result_val, &conflict_remains,
                                      &did_merge, propname,
                                      base_val, to_val, working_val,
                                      result_pool, iterpool));

      else if (! to_val) /* delete an existing property */
        SVN_ERR(apply_single_prop_delete(&result_val, &conflict_remains,
                                         &did_merge,
                                         base_val, from_val, working_val));

      else  /* changing an existing property */
        SVN_ERR(apply_single_prop_change(&result_val, &conflict_remains,
                                         &did_merge, propname,
                                         base_val, from_val, to_val, working_val,
                                         result_pool, iterpool));

      if (result_val != working_val)
        svn_hash_sets(*new_actual_props, propname, result_val);
      if (did_merge)
        set_prop_merge_state(state, svn_wc_notify_state_merged);

      /* merging logic complete, now we need to possibly log conflict
         data to tmpfiles.  */

      if (conflict_remains)
        {
          set_prop_merge_state(state, svn_wc_notify_state_conflicted);

          if (!conflict_props)
            conflict_props = apr_hash_make(scratch_pool);

          svn_hash_sets(conflict_props, propname, "");
        }

    }  /* foreach propchange ... */
  svn_pool_destroy(iterpool);

  /* Finished applying all incoming propchanges to our hashes! */

  if (conflict_props != NULL)
    {
      /* Ok, we got some conflict. Lets store all the property knowledge we
         have for resolving later */

      if (!*conflict_skel)
        *conflict_skel = svn_wc__conflict_skel_create(result_pool);

      SVN_ERR(svn_wc__conflict_skel_add_prop_conflict(*conflict_skel,
                                                      db, local_abspath,
                                                      NULL /* reject_path */,
                                                      actual_props,
                                                      server_baseprops,
                                                      their_props,
                                                      conflict_props,
                                                      result_pool,
                                                      scratch_pool));
    }

  return SVN_NO_ERROR;
}


/* Set a single 'wcprop' NAME to VALUE for versioned object LOCAL_ABSPATH.
   If VALUE is null, remove property NAME.  */
static svn_error_t *
wcprop_set(svn_wc__db_t *db,
           const char *local_abspath,
           const char *name,
           const svn_string_t *value,
           apr_pool_t *scratch_pool)
{
  apr_hash_t *prophash;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* Note: this is not well-transacted. But... meh. This is merely a cache,
     and if two processes are trying to modify this one entry at the same
     time, then fine: we can let one be a winner, and one a loser. Of course,
     if there are *other* state changes afoot, then the lack of a txn could
     be a real issue, but we cannot solve that here.  */

  SVN_ERR(svn_wc__db_base_get_dav_cache(&prophash, db, local_abspath,
                                        scratch_pool, scratch_pool));

  if (prophash == NULL)
    prophash = apr_hash_make(scratch_pool);

  svn_hash_sets(prophash, name, value);
  return svn_error_trace(svn_wc__db_base_set_dav_cache(db, local_abspath,
                                                       prophash,
                                                       scratch_pool));
}


svn_error_t *
svn_wc__get_actual_props(apr_hash_t **props,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* ### perform some state checking. for example, locally-deleted nodes
     ### should not have any ACTUAL props.  */

  return svn_error_trace(svn_wc__db_read_props(props, db, local_abspath,
                                               result_pool, scratch_pool));
}


svn_error_t *
svn_wc_prop_list2(apr_hash_t **props,
                  svn_wc_context_t *wc_ctx,
                  const char *local_abspath,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_wc__get_actual_props(props,
                                                  wc_ctx->db,
                                                  local_abspath,
                                                  result_pool,
                                                  scratch_pool));
}

struct propname_filter_baton_t {
  svn_wc__proplist_receiver_t receiver_func;
  void *receiver_baton;
  const char *propname;
};

static svn_error_t *
propname_filter_receiver(void *baton,
                         const char *local_abspath,
                         apr_hash_t *props,
                         apr_pool_t *scratch_pool)
{
  struct propname_filter_baton_t *pfb = baton;
  const svn_string_t *propval = svn_hash_gets(props, pfb->propname);

  if (propval)
    {
      props = apr_hash_make(scratch_pool);
      svn_hash_sets(props, pfb->propname, propval);

      SVN_ERR(pfb->receiver_func(pfb->receiver_baton, local_abspath, props,
                                 scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__prop_list_recursive(svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            const char *propname,
                            svn_depth_t depth,
                            svn_boolean_t pristine,
                            const apr_array_header_t *changelists,
                            svn_wc__proplist_receiver_t receiver_func,
                            void *receiver_baton,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *scratch_pool)
{
  svn_wc__proplist_receiver_t receiver = receiver_func;
  void *baton = receiver_baton;
  struct propname_filter_baton_t pfb;

  pfb.receiver_func = receiver_func;
  pfb.receiver_baton = receiver_baton;
  pfb.propname = propname;

  SVN_ERR_ASSERT(receiver_func);

  if (propname)
    {
      baton = &pfb;
      receiver = propname_filter_receiver;
    }

  switch (depth)
    {
    case svn_depth_empty:
      {
        apr_hash_t *props;
        apr_hash_t *changelist_hash = NULL;

        if (changelists && changelists->nelts)
          SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash,
                                             changelists, scratch_pool));

        if (!svn_wc__internal_changelist_match(wc_ctx->db, local_abspath,
                                               changelist_hash, scratch_pool))
          break;

        if (pristine)
          SVN_ERR(svn_wc__db_read_pristine_props(&props, wc_ctx->db,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));
        else
          SVN_ERR(svn_wc__db_read_props(&props, wc_ctx->db, local_abspath,
                                        scratch_pool, scratch_pool));

        if (props && apr_hash_count(props) > 0)
          SVN_ERR(receiver(baton, local_abspath, props, scratch_pool));
      }
      break;
    case svn_depth_files:
    case svn_depth_immediates:
    case svn_depth_infinity:
      {
        SVN_ERR(svn_wc__db_read_props_streamily(wc_ctx->db, local_abspath,
                                                depth, pristine,
                                                changelists, receiver, baton,
                                                cancel_func, cancel_baton,
                                                scratch_pool));
      }
      break;
    default:
      SVN_ERR_MALFUNCTION();
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__prop_retrieve_recursive(apr_hash_t **values,
                                svn_wc_context_t *wc_ctx,
                                const char *local_abspath,
                                const char *propname,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  return svn_error_trace(
            svn_wc__db_prop_retrieve_recursive(values,
                                               wc_ctx->db,
                                               local_abspath,
                                               propname,
                                               result_pool, scratch_pool));
}

svn_error_t *
svn_wc_get_pristine_props(apr_hash_t **props,
                          svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* Certain node stats do not have properties defined on them. Check the
     state, and return NULL for these situations.  */

  err = svn_wc__db_read_pristine_props(props, wc_ctx->db, local_abspath,
                                       result_pool, scratch_pool);

  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_UNEXPECTED_STATUS)
        return svn_error_trace(err);

      svn_error_clear(err);

      /* Documented behavior is to set *PROPS to NULL */
      *props = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_prop_get2(const svn_string_t **value,
                 svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 const char *name,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  enum svn_prop_kind kind = svn_property_kind2(name);
  svn_error_t *err;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (kind == svn_prop_entry_kind)
    {
      /* we don't do entry properties here */
      return svn_error_createf(SVN_ERR_BAD_PROP_KIND, NULL,
                               _("Property '%s' is an entry property"), name);
    }

  err = svn_wc__internal_propget(value, wc_ctx->db, local_abspath, name,
                                 result_pool, scratch_pool);

  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_UNEXPECTED_STATUS)
        return svn_error_trace(err);

      svn_error_clear(err);
      /* Documented behavior is to set *VALUE to NULL */
      *value = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__internal_propget(const svn_string_t **value,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         const char *name,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_hash_t *prophash = NULL;
  enum svn_prop_kind kind = svn_property_kind2(name);

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(kind != svn_prop_entry_kind);

  if (kind == svn_prop_wc_kind)
    {
      SVN_ERR_W(svn_wc__db_base_get_dav_cache(&prophash, db, local_abspath,
                                              result_pool, scratch_pool),
                _("Failed to load properties"));
    }
  else
    {
      /* regular prop */
      SVN_ERR_W(svn_wc__get_actual_props(&prophash, db, local_abspath,
                                         result_pool, scratch_pool),
                _("Failed to load properties"));
    }

  if (prophash)
    *value = svn_hash_gets(prophash, name);
  else
    *value = NULL;

  return SVN_NO_ERROR;
}


/* The special Subversion properties are not valid for all node kinds.
   Return an error if NAME is an invalid Subversion property for PATH which
   is of kind NODE_KIND.  NAME must be in the "svn:" name space.

   Note that we only disallow the property if we're sure it's one that
   already has a meaning for a different node kind.  We don't disallow
   setting an *unknown* svn: prop here, at this level; a higher level
   should disallow that if desired.
  */
static svn_error_t *
validate_prop_against_node_kind(const char *name,
                                const char *path,
                                svn_node_kind_t node_kind,
                                apr_pool_t *pool)
{
  const char *path_display
    = svn_path_is_url(path) ? path : svn_dirent_local_style(path, pool);

  switch (node_kind)
    {
    case svn_node_dir:
      if (! svn_prop_is_known_svn_dir_prop(name)
          && svn_prop_is_known_svn_file_prop(name))
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("Cannot set '%s' on a directory ('%s')"),
                                 name, path_display);
      break;
    case svn_node_file:
      if (! svn_prop_is_known_svn_file_prop(name)
          && svn_prop_is_known_svn_dir_prop(name))
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("Cannot set '%s' on a file ('%s')"),
                                 name,
                                 path_display);
      break;
    default:
      return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                               _("'%s' is not a file or directory"),
                               path_display);
    }

  return SVN_NO_ERROR;
}


struct getter_baton {
  const svn_string_t *mime_type;
  const char *local_abspath;
};


/* Provide the MIME_TYPE and/or push the content to STREAM for the file
 * referenced by (getter_baton *) BATON.
 *
 * Implements svn_wc_canonicalize_svn_prop_get_file_t. */
static svn_error_t *
get_file_for_validation(const svn_string_t **mime_type,
                        svn_stream_t *stream,
                        void *baton,
                        apr_pool_t *pool)
{
  struct getter_baton *gb = baton;

  if (mime_type)
    *mime_type = gb->mime_type;

  if (stream)
    {
      svn_stream_t *read_stream;

      /* Copy the text of GB->LOCAL_ABSPATH into STREAM. */
      SVN_ERR(svn_stream_open_readonly(&read_stream, gb->local_abspath,
                                       pool, pool));
      SVN_ERR(svn_stream_copy3(read_stream, svn_stream_disown(stream, pool),
                               NULL, NULL, pool));
    }

  return SVN_NO_ERROR;
}


/* Validate that a file has a 'non-binary' MIME type and contains
 * self-consistent line endings.  If not, then return an error.
 *
 * Call GETTER (which must not be NULL) with GETTER_BATON to get the
 * file's MIME type and/or content.  If the MIME type is non-null and
 * is categorized as 'binary' then return an error and do not request
 * the file content.
 *
 * Use PATH (a local path or a URL) only for error messages.
 */
static svn_error_t *
validate_eol_prop_against_file(const char *path,
                               svn_wc_canonicalize_svn_prop_get_file_t getter,
                               void *getter_baton,
                               apr_pool_t *pool)
{
  svn_stream_t *translating_stream;
  svn_error_t *err;
  const svn_string_t *mime_type;
  const char *path_display
    = svn_path_is_url(path) ? path : svn_dirent_local_style(path, pool);

  /* First just ask the "getter" for the MIME type. */
  SVN_ERR(getter(&mime_type, NULL, getter_baton, pool));

  /* See if this file has been determined to be binary. */
  if (mime_type && svn_mime_type_is_binary(mime_type->data))
    return svn_error_createf
      (SVN_ERR_ILLEGAL_TARGET, NULL,
       _("Can't set '%s': "
         "file '%s' has binary mime type property"),
       SVN_PROP_EOL_STYLE, path_display);

  /* Now ask the getter for the contents of the file; this will do a
     newline translation.  All we really care about here is whether or
     not the function fails on inconsistent line endings.  The
     function is "translating" to an empty stream.  This is
     sneeeeeeeeeeeaky. */
  translating_stream = svn_subst_stream_translated(svn_stream_empty(pool),
                                                   "", FALSE, NULL, FALSE,
                                                   pool);

  err = getter(NULL, translating_stream, getter_baton, pool);

  err = svn_error_compose_create(err, svn_stream_close(translating_stream));

  if (err && err->apr_err == SVN_ERR_IO_INCONSISTENT_EOL)
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, err,
                             _("File '%s' has inconsistent newlines"),
                             path_display);

  return svn_error_trace(err);
}

static svn_error_t *
do_propset(svn_wc__db_t *db,
           const char *local_abspath,
           svn_node_kind_t kind,
           const char *name,
           const svn_string_t *value,
           svn_boolean_t skip_checks,
           svn_wc_notify_func2_t notify_func,
           void *notify_baton,
           apr_pool_t *scratch_pool)
{
  apr_hash_t *prophash;
  svn_wc_notify_action_t notify_action;
  svn_skel_t *work_item = NULL;
  svn_boolean_t clear_recorded_info = FALSE;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR_W(svn_wc__db_read_props(&prophash, db, local_abspath,
                                  scratch_pool, scratch_pool),
            _("Failed to load current properties"));

  /* Setting an inappropriate property is not allowed (unless
     overridden by 'skip_checks', in some circumstances).  Deleting an
     inappropriate property is allowed, however, since older clients
     allowed (and other clients possibly still allow) setting it in
     the first place. */
  if (value && svn_prop_is_svn_prop(name))
    {
      const svn_string_t *new_value;
      struct getter_baton gb;

      gb.mime_type = svn_hash_gets(prophash, SVN_PROP_MIME_TYPE);
      gb.local_abspath = local_abspath;

      SVN_ERR(svn_wc_canonicalize_svn_prop(&new_value, name, value,
                                           local_abspath, kind,
                                           skip_checks,
                                           get_file_for_validation, &gb,
                                           scratch_pool));
      value = new_value;
    }

  if (kind == svn_node_file
        && (strcmp(name, SVN_PROP_EXECUTABLE) == 0
            || strcmp(name, SVN_PROP_NEEDS_LOCK) == 0))
    {
      SVN_ERR(svn_wc__wq_build_sync_file_flags(&work_item, db, local_abspath,
                                               scratch_pool, scratch_pool));
    }

  /* If we're changing this file's list of expanded keywords, then
   * we'll need to invalidate its text timestamp, since keyword
   * expansion affects the comparison of working file to text base.
   *
   * Here we retrieve the old list of expanded keywords; after the
   * property is set, we'll grab the new list and see if it differs
   * from the old one.
   */
  if (kind == svn_node_file && strcmp(name, SVN_PROP_KEYWORDS) == 0)
    {
      svn_string_t *old_value = svn_hash_gets(prophash, SVN_PROP_KEYWORDS);
      apr_hash_t *old_keywords, *new_keywords;

      if (old_value)
        SVN_ERR(svn_wc__expand_keywords(&old_keywords,
                                        db, local_abspath, NULL,
                                        old_value->data, TRUE,
                                        scratch_pool, scratch_pool));
      else
        old_keywords = apr_hash_make(scratch_pool);

      if (value)
        SVN_ERR(svn_wc__expand_keywords(&new_keywords,
                                        db, local_abspath, NULL,
                                        value->data, TRUE,
                                        scratch_pool, scratch_pool));
      else
        new_keywords = apr_hash_make(scratch_pool);

      if (svn_subst_keywords_differ2(old_keywords, new_keywords, FALSE,
                                     scratch_pool))
        {
          /* If the keywords have changed, then the translation of the file
             may be different. We should invalidate the RECORDED_SIZE
             and RECORDED_TIME on this node.

             Note that we don't immediately re-translate the file. But a
             "has it changed?" check in the future will do a translation
             from the pristine, and it will want to compare the (new)
             resulting RECORDED_SIZE against the working copy file.

             Also, when this file is (de)translated with the new keywords,
             then it could be different, relative to the pristine. We want
             to ensure the RECORDED_TIME is different, to indicate that
             a full detranslate/compare is performed.  */
          clear_recorded_info = TRUE;
        }
    }
  else if (kind == svn_node_file && strcmp(name, SVN_PROP_EOL_STYLE) == 0)
    {
      svn_string_t *old_value = svn_hash_gets(prophash, SVN_PROP_EOL_STYLE);

      if (((value == NULL) != (old_value == NULL))
          || (value && ! svn_string_compare(value, old_value)))
        {
          clear_recorded_info = TRUE;
        }
    }

  /* Find out what type of property change we are doing: add, modify, or
     delete. */
  if (svn_hash_gets(prophash, name) == NULL)
    {
      if (value == NULL)
        /* Deleting a non-existent property. */
        notify_action = svn_wc_notify_property_deleted_nonexistent;
      else
        /* Adding a property. */
        notify_action = svn_wc_notify_property_added;
    }
  else
    {
      if (value == NULL)
        /* Deleting the property. */
        notify_action = svn_wc_notify_property_deleted;
      else
        /* Modifying property. */
        notify_action = svn_wc_notify_property_modified;
    }

  /* Now we have all the properties in our hash.  Simply merge the new
     property into it. */
  svn_hash_sets(prophash, name, value);

  /* Drop it right into the db..  */
  SVN_ERR(svn_wc__db_op_set_props(db, local_abspath, prophash,
                                  clear_recorded_info, NULL, work_item,
                                  scratch_pool));

  /* Run our workqueue item for sync'ing flags with props. */
  if (work_item)
    SVN_ERR(svn_wc__wq_run(db, local_abspath, NULL, NULL, scratch_pool));

  if (notify_func)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(local_abspath,
                                                     notify_action,
                                                     scratch_pool);
      notify->prop_name = name;
      notify->kind = kind;

      (*notify_func)(notify_baton, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* A baton for propset_walk_cb. */
struct propset_walk_baton
{
  const char *propname;  /* The name of the property to set. */
  const svn_string_t *propval;  /* The value to set. */
  svn_wc__db_t *db;  /* Database for the tree being walked. */
  svn_boolean_t force;  /* True iff force was passed. */
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
};

/* An node-walk callback for svn_wc_prop_set4().
 *
 * For LOCAL_ABSPATH, set the property named wb->PROPNAME to the value
 * wb->PROPVAL, where "wb" is the WALK_BATON of type "struct
 * propset_walk_baton *".
 */
static svn_error_t *
propset_walk_cb(const char *local_abspath,
                svn_node_kind_t kind,
                void *walk_baton,
                apr_pool_t *scratch_pool)
{
  struct propset_walk_baton *wb = walk_baton;
  svn_error_t *err;

  err = do_propset(wb->db, local_abspath, kind, wb->propname, wb->propval,
                   wb->force, wb->notify_func, wb->notify_baton, scratch_pool);
  if (err && (err->apr_err == SVN_ERR_ILLEGAL_TARGET
              || err->apr_err == SVN_ERR_WC_PATH_UNEXPECTED_STATUS))
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
    }

  return svn_error_trace(err);
}

svn_error_t *
svn_wc_prop_set4(svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 const char *name,
                 const svn_string_t *value,
                 svn_depth_t depth,
                 svn_boolean_t skip_checks,
                 const apr_array_header_t *changelist_filter,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 svn_wc_notify_func2_t notify_func,
                 void *notify_baton,
                 apr_pool_t *scratch_pool)
{
  enum svn_prop_kind prop_kind = svn_property_kind2(name);
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  svn_wc__db_t *db = wc_ctx->db;

  /* we don't do entry properties here */
  if (prop_kind == svn_prop_entry_kind)
    return svn_error_createf(SVN_ERR_BAD_PROP_KIND, NULL,
                             _("Property '%s' is an entry property"), name);

  /* Check to see if we're setting the dav cache. */
  if (prop_kind == svn_prop_wc_kind)
    {
      SVN_ERR_ASSERT(depth == svn_depth_empty);
      return svn_error_trace(wcprop_set(wc_ctx->db, local_abspath,
                                        name, value, scratch_pool));
    }

  SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               wc_ctx->db, local_abspath,
                               scratch_pool, scratch_pool));

  if (status != svn_wc__db_status_normal
      && status != svn_wc__db_status_added
      && status != svn_wc__db_status_incomplete)
    {
      return svn_error_createf(SVN_ERR_WC_INVALID_SCHEDULE, NULL,
                                  _("Can't set properties on '%s':"
                                  " invalid status for updating properties."),
                                  svn_dirent_local_style(local_abspath,
                                                         scratch_pool));
    }

  /* We have to do this little DIR_ABSPATH dance for backwards compat.
     But from 1.7 onwards, all locks are of infinite depth, and from 1.6
     backward we never call this API with depth > empty, so we only need
     to do the write check once per call, here (and not for every node in
     the node walker).

     ### Note that we could check for a write lock on local_abspath first
     ### if we would want to. And then justy check for kind if that fails.
     ### ... but we need kind for the "svn:" property checks anyway */
  {
    const char *dir_abspath;

    if (kind == svn_node_dir)
      dir_abspath = local_abspath;
    else
      dir_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

    /* Verify that we're holding this directory's write lock.  */
    SVN_ERR(svn_wc__write_check(db, dir_abspath, scratch_pool));
  }

  if (depth == svn_depth_empty || kind != svn_node_dir)
    {
      apr_hash_t *changelist_hash = NULL;

      if (changelist_filter && changelist_filter->nelts)
        SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash, changelist_filter,
                                           scratch_pool));

      if (!svn_wc__internal_changelist_match(wc_ctx->db, local_abspath,
                                             changelist_hash, scratch_pool))
        return SVN_NO_ERROR;

      SVN_ERR(do_propset(wc_ctx->db, local_abspath,
                         kind == svn_node_dir
                            ? svn_node_dir
                            : svn_node_file,
                         name, value, skip_checks,
                         notify_func, notify_baton, scratch_pool));

    }
  else
    {
      struct propset_walk_baton wb;

      wb.propname = name;
      wb.propval = value;
      wb.db = wc_ctx->db;
      wb.force = skip_checks;
      wb.notify_func = notify_func;
      wb.notify_baton = notify_baton;

      SVN_ERR(svn_wc__internal_walk_children(wc_ctx->db, local_abspath,
                                             FALSE, changelist_filter,
                                             propset_walk_cb, &wb,
                                             depth,
                                             cancel_func, cancel_baton,
                                             scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Check that NAME names a regular prop. Return an error if it names an
 * entry prop or a WC prop. */
static svn_error_t *
ensure_prop_is_regular_kind(const char *name)
{
  enum svn_prop_kind prop_kind = svn_property_kind2(name);

  /* we don't do entry properties here */
  if (prop_kind == svn_prop_entry_kind)
    return svn_error_createf(SVN_ERR_BAD_PROP_KIND, NULL,
                             _("Property '%s' is an entry property"), name);

  /* Check to see if we're setting the dav cache. */
  if (prop_kind == svn_prop_wc_kind)
    return svn_error_createf(SVN_ERR_BAD_PROP_KIND, NULL,
                             _("Property '%s' is a WC property, not "
                               "a regular property"), name);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__canonicalize_props(apr_hash_t **prepared_props,
                           const char *local_abspath,
                           svn_node_kind_t node_kind,
                           const apr_hash_t *props,
                           svn_boolean_t skip_some_checks,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  const svn_string_t *mime_type;
  struct getter_baton gb;
  apr_hash_index_t *hi;

  /* While we allocate new parts of *PREPARED_PROPS in RESULT_POOL, we
     don't promise to deep-copy the unchanged keys and values. */
  *prepared_props = apr_hash_make(result_pool);

  /* Before we can canonicalize svn:eol-style we need to know svn:mime-type,
   * so process that first. */
  mime_type = svn_hash_gets((apr_hash_t *)props, SVN_PROP_MIME_TYPE);
  if (mime_type)
    {
      SVN_ERR(svn_wc_canonicalize_svn_prop(
                &mime_type, SVN_PROP_MIME_TYPE, mime_type,
                local_abspath, node_kind, skip_some_checks,
                NULL, NULL, scratch_pool));
      svn_hash_sets(*prepared_props, SVN_PROP_MIME_TYPE, mime_type);
    }

  /* Set up the context for canonicalizing the other properties. */
  gb.mime_type = mime_type;
  gb.local_abspath = local_abspath;

  /* Check and canonicalize the other properties. */
  for (hi = apr_hash_first(scratch_pool, (apr_hash_t *)props); hi;
       hi = apr_hash_next(hi))
    {
      const char *name = apr_hash_this_key(hi);
      const svn_string_t *value = apr_hash_this_val(hi);

      if (strcmp(name, SVN_PROP_MIME_TYPE) == 0)
        continue;

      SVN_ERR(ensure_prop_is_regular_kind(name));
      SVN_ERR(svn_wc_canonicalize_svn_prop(
                &value, name, value,
                local_abspath, node_kind, skip_some_checks,
                get_file_for_validation, &gb, scratch_pool));
      svn_hash_sets(*prepared_props, name, value);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_canonicalize_svn_prop(const svn_string_t **propval_p,
                             const char *propname,
                             const svn_string_t *propval,
                             const char *path,
                             svn_node_kind_t kind,
                             svn_boolean_t skip_some_checks,
                             svn_wc_canonicalize_svn_prop_get_file_t getter,
                             void *getter_baton,
                             apr_pool_t *pool)
{
  svn_stringbuf_t *new_value = NULL;

  /* Keep this static, it may get stored (for read-only purposes) in a
     hash that outlives this function. */
  static const svn_string_t boolean_value
    = SVN__STATIC_STRING(SVN_PROP_BOOLEAN_TRUE);

  SVN_ERR(validate_prop_against_node_kind(propname, path, kind, pool));

  /* This code may place the new prop val in either NEW_VALUE or PROPVAL. */
  if (!skip_some_checks && (strcmp(propname, SVN_PROP_EOL_STYLE) == 0))
    {
      svn_subst_eol_style_t eol_style;
      const char *ignored_eol;
      new_value = svn_stringbuf_create_from_string(propval, pool);
      svn_stringbuf_strip_whitespace(new_value);
      svn_subst_eol_style_from_value(&eol_style, &ignored_eol, new_value->data);
      if (eol_style == svn_subst_eol_style_unknown)
        return svn_error_createf(SVN_ERR_IO_UNKNOWN_EOL, NULL,
                                 _("Unrecognized line ending style '%s' for '%s'"),
                                 new_value->data,
                                 svn_dirent_local_style(path, pool));
      SVN_ERR(validate_eol_prop_against_file(path, getter, getter_baton,
                                             pool));
    }
  else if (!skip_some_checks && (strcmp(propname, SVN_PROP_MIME_TYPE) == 0))
    {
      new_value = svn_stringbuf_create_from_string(propval, pool);
      svn_stringbuf_strip_whitespace(new_value);
      SVN_ERR(svn_mime_type_validate(new_value->data, pool));
    }
  else if (strcmp(propname, SVN_PROP_IGNORE) == 0
           || strcmp(propname, SVN_PROP_EXTERNALS) == 0
           || strcmp(propname, SVN_PROP_INHERITABLE_IGNORES) == 0
           || strcmp(propname, SVN_PROP_INHERITABLE_AUTO_PROPS) == 0)
    {
      /* Make sure that the last line ends in a newline */
      if (propval->len == 0
          || propval->data[propval->len - 1] != '\n')
        {
          new_value = svn_stringbuf_create_from_string(propval, pool);
          svn_stringbuf_appendbyte(new_value, '\n');
        }

      /* Make sure this is a valid externals property.  Do not
         allow 'skip_some_checks' to override, as there is no circumstance in
         which this is proper (because there is no circumstance in
         which Subversion can handle it). */
      if (strcmp(propname, SVN_PROP_EXTERNALS) == 0)
        {
          /* We don't allow "." nor ".." as target directories in
             an svn:externals line.  As it happens, our parse code
             checks for this, so all we have to is invoke it --
             we're not interested in the parsed result, only in
             whether or not the parsing errored. */
          apr_array_header_t *externals = NULL;
          apr_array_header_t *duplicate_targets = NULL;
          SVN_ERR(svn_wc_parse_externals_description3(&externals, path,
                                                      propval->data, FALSE,
                                                      /*scratch_*/pool));
          SVN_ERR(svn_wc__externals_find_target_dups(&duplicate_targets,
                                                     externals,
                                                     /*scratch_*/pool,
                                                     /*scratch_*/pool));
          if (duplicate_targets && duplicate_targets->nelts > 0)
            {
              const char *more_str = "";
              if (duplicate_targets->nelts > 1)
                {
                  more_str = apr_psprintf(/*scratch_*/pool,
                               _(" (%d more duplicate targets found)"),
                               duplicate_targets->nelts - 1);
                }
              return svn_error_createf(
                SVN_ERR_WC_DUPLICATE_EXTERNALS_TARGET, NULL,
                _("Invalid %s property on '%s': "
                  "target '%s' appears more than once%s"),
                SVN_PROP_EXTERNALS,
                svn_dirent_local_style(path, pool),
                APR_ARRAY_IDX(duplicate_targets, 0, const char*),
                more_str);
            }
        }
    }
  else if (strcmp(propname, SVN_PROP_KEYWORDS) == 0)
    {
      new_value = svn_stringbuf_create_from_string(propval, pool);
      svn_stringbuf_strip_whitespace(new_value);
    }
  else if (svn_prop_is_boolean(propname))
    {
      /* SVN_PROP_EXECUTABLE, SVN_PROP_NEEDS_LOCK, SVN_PROP_SPECIAL */
      propval = &boolean_value;
    }
  else if (strcmp(propname, SVN_PROP_MERGEINFO) == 0)
    {
      apr_hash_t *mergeinfo;
      svn_string_t *new_value_str;

      SVN_ERR(svn_mergeinfo_parse(&mergeinfo, propval->data, pool));

      /* Non-inheritable mergeinfo is only valid on directories. */
      if (kind != svn_node_dir
          && svn_mergeinfo__is_noninheritable(mergeinfo, pool))
        return svn_error_createf(
          SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
          _("Cannot set non-inheritable mergeinfo on a non-directory ('%s')"),
          svn_dirent_local_style(path, pool));

      SVN_ERR(svn_mergeinfo_to_string(&new_value_str, mergeinfo, pool));
      propval = new_value_str;
    }

  if (new_value)
    *propval_p = svn_stringbuf__morph_into_string(new_value);
  else
    *propval_p = propval;

  return SVN_NO_ERROR;
}


svn_boolean_t
svn_wc_is_normal_prop(const char *name)
{
  enum svn_prop_kind kind = svn_property_kind2(name);
  return (kind == svn_prop_regular_kind);
}


svn_boolean_t
svn_wc_is_wc_prop(const char *name)
{
  enum svn_prop_kind kind = svn_property_kind2(name);
  return (kind == svn_prop_wc_kind);
}


svn_boolean_t
svn_wc_is_entry_prop(const char *name)
{
  enum svn_prop_kind kind = svn_property_kind2(name);
  return (kind == svn_prop_entry_kind);
}


svn_error_t *
svn_wc__props_modified(svn_boolean_t *modified_p,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, modified_p, NULL, NULL, NULL,
                               db, local_abspath,
                               scratch_pool, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_props_modified_p2(svn_boolean_t *modified_p,
                         svn_wc_context_t* wc_ctx,
                         const char *local_abspath,
                         apr_pool_t *scratch_pool)
{
  return svn_error_trace(
             svn_wc__props_modified(modified_p,
                                    wc_ctx->db,
                                    local_abspath,
                                    scratch_pool));
}

svn_error_t *
svn_wc__internal_propdiff(apr_array_header_t **propchanges,
                          apr_hash_t **original_props,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  apr_hash_t *baseprops;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* ### if pristines are not defined, then should this raise an error,
     ### or use an empty set?  */
  SVN_ERR(svn_wc__db_read_pristine_props(&baseprops, db, local_abspath,
                                         result_pool, scratch_pool));

  if (original_props != NULL)
    *original_props = baseprops;

  if (propchanges != NULL)
    {
      apr_hash_t *actual_props;

      /* Some nodes do not have pristine props, so let's just use an empty
         set here. Thus, any ACTUAL props are additions.  */
      if (baseprops == NULL)
        baseprops = apr_hash_make(scratch_pool);

      SVN_ERR(svn_wc__db_read_props(&actual_props, db, local_abspath,
                                    result_pool, scratch_pool));
      /* ### be wary. certain nodes don't have ACTUAL props either. we
         ### may want to raise an error. or maybe that is a deletion of
         ### any potential pristine props?  */

      SVN_ERR(svn_prop_diffs(propchanges, actual_props, baseprops,
                             result_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_get_prop_diffs2(apr_array_header_t **propchanges,
                       apr_hash_t **original_props,
                       svn_wc_context_t *wc_ctx,
                       const char *local_abspath,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_wc__internal_propdiff(propchanges,
                                    original_props, wc_ctx->db, local_abspath,
                                    result_pool, scratch_pool));
}

svn_boolean_t
svn_wc__has_magic_property(const apr_array_header_t *properties)
{
  int i;

  for (i = 0; i < properties->nelts; i++)
    {
      const svn_prop_t *property = &APR_ARRAY_IDX(properties, i, svn_prop_t);

      if (strcmp(property->name, SVN_PROP_EXECUTABLE) == 0
          || strcmp(property->name, SVN_PROP_KEYWORDS) == 0
          || strcmp(property->name, SVN_PROP_EOL_STYLE) == 0
          || strcmp(property->name, SVN_PROP_SPECIAL) == 0
          || strcmp(property->name, SVN_PROP_NEEDS_LOCK) == 0)
        return TRUE;
    }
  return FALSE;
}

svn_error_t *
svn_wc__get_iprops(apr_array_header_t **inherited_props,
                   svn_wc_context_t *wc_ctx,
                   const char *local_abspath,
                   const char *propname,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  return svn_error_trace(
            svn_wc__db_read_inherited_props(inherited_props, NULL,
                                            wc_ctx->db, local_abspath,
                                            propname,
                                            result_pool, scratch_pool));
}

svn_error_t *
svn_wc__get_cached_iprop_children(apr_hash_t **iprop_paths,
                                  svn_depth_t depth,
                                  svn_wc_context_t *wc_ctx,
                                  const char *local_abspath,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__db_get_children_with_cached_iprops(iprop_paths,
                                                     depth,
                                                     local_abspath,
                                                     wc_ctx->db,
                                                     result_pool,
                                                     scratch_pool));
  return SVN_NO_ERROR;
}
