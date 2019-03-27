/*
 * conflicts.c: routines for managing conflict data.
 *            NOTE: this code doesn't know where the conflict is
 *            actually stored.
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



#include <string.h>

#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_hash.h>
#include <apr_errno.h>

#include "svn_hash.h"
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_wc.h"
#include "svn_io.h"
#include "svn_diff.h"

#include "wc.h"
#include "wc_db.h"
#include "conflicts.h"
#include "workqueue.h"
#include "props.h"

#include "private/svn_wc_private.h"
#include "private/svn_skel.h"
#include "private/svn_sorts_private.h"
#include "private/svn_string_private.h"

#include "svn_private_config.h"

/* --------------------------------------------------------------------
 * Conflict skel management
 */

svn_skel_t *
svn_wc__conflict_skel_create(apr_pool_t *result_pool)
{
  svn_skel_t *conflict_skel = svn_skel__make_empty_list(result_pool);

  /* Add empty CONFLICTS list */
  svn_skel__prepend(svn_skel__make_empty_list(result_pool), conflict_skel);

  /* Add empty WHY list */
  svn_skel__prepend(svn_skel__make_empty_list(result_pool), conflict_skel);

  return conflict_skel;
}

svn_error_t *
svn_wc__conflict_skel_is_complete(svn_boolean_t *complete,
                                  const svn_skel_t *conflict_skel)
{
  *complete = FALSE;

  if (svn_skel__list_length(conflict_skel) < 2)
    return svn_error_create(SVN_ERR_INCOMPLETE_DATA, NULL,
                            _("Not a conflict skel"));

  if (svn_skel__list_length(conflict_skel->children) < 2)
    return SVN_NO_ERROR; /* WHY is not set */

  if (svn_skel__list_length(conflict_skel->children->next) == 0)
    return SVN_NO_ERROR; /* No conflict set */

  *complete = TRUE;
  return SVN_NO_ERROR;
}

/* Serialize a svn_wc_conflict_version_t before the existing data in skel */
static svn_error_t *
conflict__prepend_location(svn_skel_t *skel,
                           const svn_wc_conflict_version_t *location,
                           svn_boolean_t allow_NULL,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_skel_t *loc;
  SVN_ERR_ASSERT(location || allow_NULL);

  if (!location)
    {
      svn_skel__prepend(svn_skel__make_empty_list(result_pool), skel);
      return SVN_NO_ERROR;
    }

  /* ("subversion" repos_root_url repos_uuid repos_relpath rev kind) */
  loc = svn_skel__make_empty_list(result_pool);

  svn_skel__prepend_str(svn_node_kind_to_word(location->node_kind),
                        loc, result_pool);

  svn_skel__prepend_int(location->peg_rev, loc, result_pool);

  svn_skel__prepend_str(apr_pstrdup(result_pool, location->path_in_repos), loc,
                        result_pool);

  if (!location->repos_uuid) /* Can theoretically be NULL */
    svn_skel__prepend(svn_skel__make_empty_list(result_pool), loc);
  else
    svn_skel__prepend_str(location->repos_uuid, loc, result_pool);

  svn_skel__prepend_str(apr_pstrdup(result_pool, location->repos_url), loc,
                        result_pool);

  svn_skel__prepend_str(SVN_WC__CONFLICT_SRC_SUBVERSION, loc, result_pool);

  svn_skel__prepend(loc, skel);
  return SVN_NO_ERROR;
}

/* Deserialize a svn_wc_conflict_version_t from the skel.
   Set *LOCATION to NULL when the data is not a svn_wc_conflict_version_t. */
static svn_error_t *
conflict__read_location(svn_wc_conflict_version_t **location,
                        const svn_skel_t *skel,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  const char *repos_root_url;
  const char *repos_uuid;
  const char *repos_relpath;
  svn_revnum_t revision;
  apr_int64_t v;
  svn_node_kind_t node_kind;  /* note that 'none' is a legitimate value */
  const char *kind_str;

  const svn_skel_t *c = skel->children;

  if (!svn_skel__matches_atom(c, SVN_WC__CONFLICT_SRC_SUBVERSION))
    {
      *location = NULL;
      return SVN_NO_ERROR;
    }
  c = c->next;

  repos_root_url = apr_pstrmemdup(result_pool, c->data, c->len);
  c = c->next;

  if (c->is_atom)
    repos_uuid = apr_pstrmemdup(result_pool, c->data, c->len);
  else
    repos_uuid = NULL;
  c = c->next;

  repos_relpath = apr_pstrmemdup(result_pool, c->data, c->len);
  c = c->next;

  SVN_ERR(svn_skel__parse_int(&v, c, scratch_pool));
  revision = (svn_revnum_t)v;
  c = c->next;

  kind_str = apr_pstrmemdup(scratch_pool, c->data, c->len);
  node_kind = svn_node_kind_from_word(kind_str);

  *location = svn_wc_conflict_version_create2(repos_root_url,
                                              repos_uuid,
                                              repos_relpath,
                                              revision,
                                              node_kind,
                                              result_pool);
  return SVN_NO_ERROR;
}

/* Get the operation part of CONFLICT_SKELL or NULL if no operation is set
   at this time */
static svn_error_t *
conflict__get_operation(svn_skel_t **why,
                        const svn_skel_t *conflict_skel)
{
  SVN_ERR_ASSERT(conflict_skel
                 && conflict_skel->children
                 && conflict_skel->children->next
                 && !conflict_skel->children->next->is_atom);

  *why = conflict_skel->children;

  if (!(*why)->children)
    *why = NULL; /* Operation is not set yet */

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__conflict_skel_set_op_update(svn_skel_t *conflict_skel,
                                    const svn_wc_conflict_version_t *original,
                                    const svn_wc_conflict_version_t *target,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  svn_skel_t *why;
  svn_skel_t *origins;

  SVN_ERR_ASSERT(conflict_skel
                 && conflict_skel->children
                 && conflict_skel->children->next
                 && !conflict_skel->children->next->is_atom);

  SVN_ERR(conflict__get_operation(&why, conflict_skel));

  SVN_ERR_ASSERT(why == NULL); /* No operation set */

  why = conflict_skel->children;

  origins = svn_skel__make_empty_list(result_pool);

  SVN_ERR(conflict__prepend_location(origins, target, TRUE,
                                     result_pool, scratch_pool));
  SVN_ERR(conflict__prepend_location(origins, original, TRUE,
                                     result_pool, scratch_pool));

  svn_skel__prepend(origins, why);
  svn_skel__prepend_str(SVN_WC__CONFLICT_OP_UPDATE, why, result_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_skel_set_op_switch(svn_skel_t *conflict_skel,
                                    const svn_wc_conflict_version_t *original,
                                    const svn_wc_conflict_version_t *target,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  svn_skel_t *why;
  svn_skel_t *origins;

  SVN_ERR_ASSERT(conflict_skel
                 && conflict_skel->children
                 && conflict_skel->children->next
                 && !conflict_skel->children->next->is_atom);

  SVN_ERR(conflict__get_operation(&why, conflict_skel));

  SVN_ERR_ASSERT(why == NULL); /* No operation set */

  why = conflict_skel->children;

  origins = svn_skel__make_empty_list(result_pool);

  SVN_ERR(conflict__prepend_location(origins, target, TRUE,
                                     result_pool, scratch_pool));
  SVN_ERR(conflict__prepend_location(origins, original, TRUE,
                                     result_pool, scratch_pool));

  svn_skel__prepend(origins, why);
  svn_skel__prepend_str(SVN_WC__CONFLICT_OP_SWITCH, why, result_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_skel_set_op_merge(svn_skel_t *conflict_skel,
                                   const svn_wc_conflict_version_t *left,
                                   const svn_wc_conflict_version_t *right,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  svn_skel_t *why;
  svn_skel_t *origins;

  SVN_ERR_ASSERT(conflict_skel
                 && conflict_skel->children
                 && conflict_skel->children->next
                 && !conflict_skel->children->next->is_atom);

  SVN_ERR(conflict__get_operation(&why, conflict_skel));

  SVN_ERR_ASSERT(why == NULL); /* No operation set */

  why = conflict_skel->children;

  origins = svn_skel__make_empty_list(result_pool);

  SVN_ERR(conflict__prepend_location(origins, right, TRUE,
                                     result_pool, scratch_pool));

  SVN_ERR(conflict__prepend_location(origins, left, TRUE,
                                     result_pool, scratch_pool));

  svn_skel__prepend(origins, why);
  svn_skel__prepend_str(SVN_WC__CONFLICT_OP_MERGE, why, result_pool);

  return SVN_NO_ERROR;
}

/* Gets the conflict data of the specified type CONFLICT_TYPE from
   CONFLICT_SKEL, or NULL if no such conflict is recorded */
static svn_error_t *
conflict__get_conflict(svn_skel_t **conflict,
                       const svn_skel_t *conflict_skel,
                       const char *conflict_type)
{
  svn_skel_t *c;

  SVN_ERR_ASSERT(conflict_skel
                 && conflict_skel->children
                 && conflict_skel->children->next
                 && !conflict_skel->children->next->is_atom);

  for(c = conflict_skel->children->next->children;
      c;
      c = c->next)
    {
      if (svn_skel__matches_atom(c->children, conflict_type))
        {
          *conflict = c;
          return SVN_NO_ERROR;
        }
    }

  *conflict = NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_skel_add_text_conflict(svn_skel_t *conflict_skel,
                                        svn_wc__db_t *db,
                                        const char *wri_abspath,
                                        const char *mine_abspath,
                                        const char *their_old_abspath,
                                        const char *their_abspath,
                                        apr_pool_t *result_pool,
                                        apr_pool_t *scratch_pool)
{
  svn_skel_t *text_conflict;
  svn_skel_t *markers;

  SVN_ERR(conflict__get_conflict(&text_conflict, conflict_skel,
                                 SVN_WC__CONFLICT_KIND_TEXT));

  SVN_ERR_ASSERT(!text_conflict); /* ### Use proper error? */

  /* Current skel format
     ("text"
      (OLD MINE OLD-THEIRS THEIRS)) */

  text_conflict = svn_skel__make_empty_list(result_pool);
  markers = svn_skel__make_empty_list(result_pool);

if (their_abspath)
    {
      const char *their_relpath;

      SVN_ERR(svn_wc__db_to_relpath(&their_relpath,
                                    db, wri_abspath, their_abspath,
                                    result_pool, scratch_pool));
      svn_skel__prepend_str(their_relpath, markers, result_pool);
    }
  else
    svn_skel__prepend(svn_skel__make_empty_list(result_pool), markers);

  if (mine_abspath)
    {
      const char *mine_relpath;

      SVN_ERR(svn_wc__db_to_relpath(&mine_relpath,
                                    db, wri_abspath, mine_abspath,
                                    result_pool, scratch_pool));
      svn_skel__prepend_str(mine_relpath, markers, result_pool);
    }
  else
    svn_skel__prepend(svn_skel__make_empty_list(result_pool), markers);

  if (their_old_abspath)
    {
      const char *original_relpath;

      SVN_ERR(svn_wc__db_to_relpath(&original_relpath,
                                    db, wri_abspath, their_old_abspath,
                                    result_pool, scratch_pool));
      svn_skel__prepend_str(original_relpath, markers, result_pool);
    }
  else
    svn_skel__prepend(svn_skel__make_empty_list(result_pool), markers);

  svn_skel__prepend(markers, text_conflict);
  svn_skel__prepend_str(SVN_WC__CONFLICT_KIND_TEXT, text_conflict,
                        result_pool);

  /* And add it to the conflict skel */
  svn_skel__prepend(text_conflict, conflict_skel->children->next);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_skel_add_prop_conflict(svn_skel_t *conflict_skel,
                                        svn_wc__db_t *db,
                                        const char *wri_abspath,
                                        const char *marker_abspath,
                                        const apr_hash_t *mine_props,
                                        const apr_hash_t *their_old_props,
                                        const apr_hash_t *their_props,
                                        const apr_hash_t *conflicted_prop_names,
                                        apr_pool_t *result_pool,
                                        apr_pool_t *scratch_pool)
{
  svn_skel_t *prop_conflict;
  svn_skel_t *props;
  svn_skel_t *conflict_names;
  svn_skel_t *markers;
  apr_hash_index_t *hi;

  SVN_ERR(conflict__get_conflict(&prop_conflict, conflict_skel,
                                 SVN_WC__CONFLICT_KIND_PROP));

  SVN_ERR_ASSERT(!prop_conflict); /* ### Use proper error? */

  /* This function currently implements:
     ("prop"
      ("marker_relpath")
      prop-conflicted_prop_names
      old-props
      mine-props
      their-props)
     NULL lists are recorded as "" */
  /* ### Seems that this may not match what we read out.  Read-out of
   * 'theirs-old' comes as NULL. */

  prop_conflict = svn_skel__make_empty_list(result_pool);

  if (their_props)
    {
      SVN_ERR(svn_skel__unparse_proplist(&props, their_props, result_pool));
      svn_skel__prepend(props, prop_conflict);
    }
  else
    svn_skel__prepend_str("", prop_conflict, result_pool); /* No their_props */

  if (mine_props)
    {
      SVN_ERR(svn_skel__unparse_proplist(&props, mine_props, result_pool));
      svn_skel__prepend(props, prop_conflict);
    }
  else
    svn_skel__prepend_str("", prop_conflict, result_pool); /* No mine_props */

  if (their_old_props)
    {
      SVN_ERR(svn_skel__unparse_proplist(&props, their_old_props,
                                         result_pool));
      svn_skel__prepend(props, prop_conflict);
    }
  else
    svn_skel__prepend_str("", prop_conflict, result_pool); /* No old_props */

  conflict_names = svn_skel__make_empty_list(result_pool);
  for (hi = apr_hash_first(scratch_pool, (apr_hash_t *)conflicted_prop_names);
       hi;
       hi = apr_hash_next(hi))
    {
      svn_skel__prepend_str(apr_pstrdup(result_pool, apr_hash_this_key(hi)),
                            conflict_names,
                            result_pool);
    }
  svn_skel__prepend(conflict_names, prop_conflict);

  markers = svn_skel__make_empty_list(result_pool);

  if (marker_abspath)
    {
      const char *marker_relpath;
      SVN_ERR(svn_wc__db_to_relpath(&marker_relpath, db, wri_abspath,
                                    marker_abspath,
                                    result_pool, scratch_pool));

      svn_skel__prepend_str(marker_relpath, markers, result_pool);
    }
/*else // ### set via svn_wc__conflict_create_markers
    svn_skel__prepend(svn_skel__make_empty_list(result_pool), markers);*/

  svn_skel__prepend(markers, prop_conflict);

  svn_skel__prepend_str(SVN_WC__CONFLICT_KIND_PROP, prop_conflict, result_pool);

  /* And add it to the conflict skel */
  svn_skel__prepend(prop_conflict, conflict_skel->children->next);

  return SVN_NO_ERROR;
}

/* A map for svn_wc_conflict_reason_t values. */
static const svn_token_map_t reason_map[] =
{
  { "edited",           svn_wc_conflict_reason_edited },
  { "obstructed",       svn_wc_conflict_reason_obstructed },
  { "deleted",          svn_wc_conflict_reason_deleted },
  { "missing",          svn_wc_conflict_reason_missing },
  { "unversioned",      svn_wc_conflict_reason_unversioned },
  { "added",            svn_wc_conflict_reason_added },
  { "replaced",         svn_wc_conflict_reason_replaced },
  { "moved-away",       svn_wc_conflict_reason_moved_away },
  { "moved-here",       svn_wc_conflict_reason_moved_here },
  { NULL }
};

static const svn_token_map_t action_map[] =
{
  { "edited",           svn_wc_conflict_action_edit },
  { "added",            svn_wc_conflict_action_add },
  { "deleted",          svn_wc_conflict_action_delete },
  { "replaced",         svn_wc_conflict_action_replace },
  { NULL }
};

svn_error_t *
svn_wc__conflict_skel_add_tree_conflict(svn_skel_t *conflict_skel,
                                        svn_wc__db_t *db,
                                        const char *wri_abspath,
                                        svn_wc_conflict_reason_t reason,
                                        svn_wc_conflict_action_t action,
                                        const char *move_src_op_root_abspath,
                                        apr_pool_t *result_pool,
                                        apr_pool_t *scratch_pool)
{
  svn_skel_t *tree_conflict;
  svn_skel_t *markers;

  SVN_ERR(conflict__get_conflict(&tree_conflict, conflict_skel,
                                 SVN_WC__CONFLICT_KIND_TREE));

  SVN_ERR_ASSERT(!tree_conflict); /* ### Use proper error? */

  SVN_ERR_ASSERT(reason == svn_wc_conflict_reason_moved_away
                 || !move_src_op_root_abspath); /* ### Use proper error? */

  tree_conflict = svn_skel__make_empty_list(result_pool);

  if (reason == svn_wc_conflict_reason_moved_away
      && move_src_op_root_abspath)
    {
      const char *move_src_op_root_relpath;

      SVN_ERR(svn_wc__db_to_relpath(&move_src_op_root_relpath,
                                    db, wri_abspath,
                                    move_src_op_root_abspath,
                                    result_pool, scratch_pool));

      svn_skel__prepend_str(move_src_op_root_relpath, tree_conflict,
                            result_pool);
    }

  svn_skel__prepend_str(svn_token__to_word(action_map, action),
                        tree_conflict, result_pool);

  svn_skel__prepend_str(svn_token__to_word(reason_map, reason),
                        tree_conflict, result_pool);

  /* Tree conflicts have no marker files */
  markers = svn_skel__make_empty_list(result_pool);
  svn_skel__prepend(markers, tree_conflict);

  svn_skel__prepend_str(SVN_WC__CONFLICT_KIND_TREE, tree_conflict,
                        result_pool);

  /* And add it to the conflict skel */
  svn_skel__prepend(tree_conflict, conflict_skel->children->next);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_skel_resolve(svn_boolean_t *completely_resolved,
                              svn_skel_t *conflict_skel,
                              svn_wc__db_t *db,
                              const char *wri_abspath,
                              svn_boolean_t resolve_text,
                              const char *resolve_prop,
                              svn_boolean_t resolve_tree,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  svn_skel_t *op;
  svn_skel_t **pconflict;
  SVN_ERR(conflict__get_operation(&op, conflict_skel));

  if (!op)
    return svn_error_create(SVN_ERR_INCOMPLETE_DATA, NULL,
                            _("Not a completed conflict skel"));

  /* We are going to drop items from a linked list. Instead of keeping
     a pointer to the item we want to drop we store a pointer to the
     pointer of what we may drop, to allow setting it to the next item. */

  pconflict = &(conflict_skel->children->next->children);
  while (*pconflict)
    {
      svn_skel_t *c = (*pconflict)->children;

      if (resolve_text
          && svn_skel__matches_atom(c, SVN_WC__CONFLICT_KIND_TEXT))
        {
          /* Remove the text conflict from the linked list */
          *pconflict = (*pconflict)->next;
          continue;
        }
      else if (resolve_prop
               && svn_skel__matches_atom(c, SVN_WC__CONFLICT_KIND_PROP))
        {
          svn_skel_t **ppropnames = &(c->next->next->children);

          if (resolve_prop[0] == '\0')
            *ppropnames = NULL; /* remove all conflicted property names */
          else
            while (*ppropnames)
              {
                if (svn_skel__matches_atom(*ppropnames, resolve_prop))
                  {
                    *ppropnames = (*ppropnames)->next;
                    break;
                  }
                ppropnames = &((*ppropnames)->next);
              }

          /* If no conflicted property names left */
          if (!c->next->next->children)
            {
              /* Remove the propery conflict skel from the linked list */
             *pconflict = (*pconflict)->next;
             continue;
            }
        }
      else if (resolve_tree
               && svn_skel__matches_atom(c, SVN_WC__CONFLICT_KIND_TREE))
        {
          /* Remove the tree conflict from the linked list */
          *pconflict = (*pconflict)->next;
          continue;
        }

      pconflict = &((*pconflict)->next);
    }

  if (completely_resolved)
    {
      /* Nice, we can just call the complete function */
      svn_boolean_t complete_conflict;
      SVN_ERR(svn_wc__conflict_skel_is_complete(&complete_conflict,
                                                conflict_skel));

      *completely_resolved = !complete_conflict;
    }
  return SVN_NO_ERROR;
}


/* A map for svn_wc_operation_t values. */
static const svn_token_map_t operation_map[] =
{
  { "",   svn_wc_operation_none },
  { SVN_WC__CONFLICT_OP_UPDATE, svn_wc_operation_update },
  { SVN_WC__CONFLICT_OP_SWITCH, svn_wc_operation_switch },
  { SVN_WC__CONFLICT_OP_MERGE,  svn_wc_operation_merge },
  { NULL }
};

svn_error_t *
svn_wc__conflict_read_info(svn_wc_operation_t *operation,
                           const apr_array_header_t **locations,
                           svn_boolean_t *text_conflicted,
                           svn_boolean_t *prop_conflicted,
                           svn_boolean_t *tree_conflicted,
                           svn_wc__db_t *db,
                           const char *wri_abspath,
                           const svn_skel_t *conflict_skel,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_skel_t *op;
  const svn_skel_t *c;

  SVN_ERR(conflict__get_operation(&op, conflict_skel));

  if (!op)
    return svn_error_create(SVN_ERR_INCOMPLETE_DATA, NULL,
                            _("Not a completed conflict skel"));

  c = op->children;
  if (operation)
    {
      int value = svn_token__from_mem(operation_map, c->data, c->len);

      if (value != SVN_TOKEN_UNKNOWN)
        *operation = value;
      else
        *operation = svn_wc_operation_none;
    }
  c = c->next;

  if (locations && c->children)
    {
      const svn_skel_t *loc_skel;
      svn_wc_conflict_version_t *loc;
      apr_array_header_t *locs = apr_array_make(result_pool, 2, sizeof(loc));

      for (loc_skel = c->children; loc_skel; loc_skel = loc_skel->next)
        {
          SVN_ERR(conflict__read_location(&loc, loc_skel, result_pool,
                                          scratch_pool));

          APR_ARRAY_PUSH(locs, svn_wc_conflict_version_t *) = loc;
        }

      *locations = locs;
    }
  else if (locations)
    *locations = NULL;

  if (text_conflicted)
    {
      svn_skel_t *c_skel;
      SVN_ERR(conflict__get_conflict(&c_skel, conflict_skel,
                                     SVN_WC__CONFLICT_KIND_TEXT));

      *text_conflicted = (c_skel != NULL);
    }

  if (prop_conflicted)
    {
      svn_skel_t *c_skel;
      SVN_ERR(conflict__get_conflict(&c_skel, conflict_skel,
                                     SVN_WC__CONFLICT_KIND_PROP));

      *prop_conflicted = (c_skel != NULL);
    }

  if (tree_conflicted)
    {
      svn_skel_t *c_skel;
      SVN_ERR(conflict__get_conflict(&c_skel, conflict_skel,
                                     SVN_WC__CONFLICT_KIND_TREE));

      *tree_conflicted = (c_skel != NULL);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__conflict_read_text_conflict(const char **mine_abspath,
                                    const char **their_old_abspath,
                                    const char **their_abspath,
                                    svn_wc__db_t *db,
                                    const char *wri_abspath,
                                    const svn_skel_t *conflict_skel,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  svn_skel_t *text_conflict;
  const svn_skel_t *m;

  SVN_ERR(conflict__get_conflict(&text_conflict, conflict_skel,
                                 SVN_WC__CONFLICT_KIND_TEXT));

  if (!text_conflict)
    return svn_error_create(SVN_ERR_WC_MISSING, NULL, _("Conflict not set"));

  m = text_conflict->children->next->children;

  if (their_old_abspath)
    {
      if (m->is_atom)
        {
          const char *original_relpath;

          original_relpath = apr_pstrmemdup(scratch_pool, m->data, m->len);
          SVN_ERR(svn_wc__db_from_relpath(their_old_abspath,
                                          db, wri_abspath, original_relpath,
                                          result_pool, scratch_pool));
        }
      else
        *their_old_abspath = NULL;
    }
  m = m->next;

  if (mine_abspath)
    {
      if (m->is_atom)
        {
          const char *mine_relpath;

          mine_relpath = apr_pstrmemdup(scratch_pool, m->data, m->len);
          SVN_ERR(svn_wc__db_from_relpath(mine_abspath,
                                          db, wri_abspath, mine_relpath,
                                          result_pool, scratch_pool));
        }
      else
        *mine_abspath = NULL;
    }
  m = m->next;

  if (their_abspath)
    {
      if (m->is_atom)
        {
          const char *their_relpath;

          their_relpath = apr_pstrmemdup(scratch_pool, m->data, m->len);
          SVN_ERR(svn_wc__db_from_relpath(their_abspath,
                                          db, wri_abspath, their_relpath,
                                          result_pool, scratch_pool));
        }
      else
        *their_abspath = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_read_prop_conflict(const char **marker_abspath,
                                    apr_hash_t **mine_props,
                                    apr_hash_t **their_old_props,
                                    apr_hash_t **their_props,
                                    apr_hash_t **conflicted_prop_names,
                                    svn_wc__db_t *db,
                                    const char *wri_abspath,
                                    const svn_skel_t *conflict_skel,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  svn_skel_t *prop_conflict;
  const svn_skel_t *c;

  SVN_ERR(conflict__get_conflict(&prop_conflict, conflict_skel,
                                 SVN_WC__CONFLICT_KIND_PROP));

  if (!prop_conflict)
    return svn_error_create(SVN_ERR_WC_MISSING, NULL, _("Conflict not set"));

  c = prop_conflict->children;

  c = c->next; /* Skip "prop" */

  /* Get marker file */
  if (marker_abspath)
    {
      const char *marker_relpath;

      if (c->children && c->children->is_atom)
        {
          marker_relpath = apr_pstrmemdup(result_pool, c->children->data,
                                        c->children->len);

          SVN_ERR(svn_wc__db_from_relpath(marker_abspath, db, wri_abspath,
                                          marker_relpath,
                                          result_pool, scratch_pool));
        }
      else
        *marker_abspath = NULL;
    }
  c = c->next;

  /* Get conflicted properties */
  if (conflicted_prop_names)
    {
      const svn_skel_t *name;
      *conflicted_prop_names = apr_hash_make(result_pool);

      for (name = c->children; name; name = name->next)
        {
          svn_hash_sets(*conflicted_prop_names,
                        apr_pstrmemdup(result_pool, name->data, name->len),
                        "");
        }
    }
  c = c->next;

  /* Get original properties */
  if (their_old_props)
    {
      if (c->is_atom)
        *their_old_props = apr_hash_make(result_pool);
      else
        SVN_ERR(svn_skel__parse_proplist(their_old_props, c, result_pool));
    }
  c = c->next;

  /* Get mine properties */
  if (mine_props)
    {
      if (c->is_atom)
        *mine_props = apr_hash_make(result_pool);
      else
        SVN_ERR(svn_skel__parse_proplist(mine_props, c, result_pool));
    }
  c = c->next;

  /* Get their properties */
  if (their_props)
    {
      if (c->is_atom)
        *their_props = apr_hash_make(result_pool);
      else
        SVN_ERR(svn_skel__parse_proplist(their_props, c, result_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_read_tree_conflict(svn_wc_conflict_reason_t *reason,
                                    svn_wc_conflict_action_t *action,
                                    const char **move_src_op_root_abspath,
                                    svn_wc__db_t *db,
                                    const char *wri_abspath,
                                    const svn_skel_t *conflict_skel,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  svn_skel_t *tree_conflict;
  const svn_skel_t *c;
  svn_boolean_t is_moved_away = FALSE;

  SVN_ERR(conflict__get_conflict(&tree_conflict, conflict_skel,
                                 SVN_WC__CONFLICT_KIND_TREE));

  if (!tree_conflict)
    return svn_error_create(SVN_ERR_WC_MISSING, NULL, _("Conflict not set"));

  c = tree_conflict->children;

  c = c->next; /* Skip "tree" */

  c = c->next; /* Skip markers */

  {
    int value = svn_token__from_mem(reason_map, c->data, c->len);

    if (reason)
      {
        if (value != SVN_TOKEN_UNKNOWN)
          *reason = value;
        else
          *reason = svn_wc_conflict_reason_edited;
      }

      is_moved_away = (value == svn_wc_conflict_reason_moved_away);
    }
  c = c->next;

  if (action)
    {
      int value = svn_token__from_mem(action_map, c->data, c->len);

      if (value != SVN_TOKEN_UNKNOWN)
        *action = value;
      else
        *action = svn_wc_conflict_action_edit;
    }

  c = c->next;

  if (move_src_op_root_abspath)
    {
      /* Only set for update and switch tree conflicts */
      if (c && is_moved_away)
        {
          const char *move_src_op_root_relpath
                            = apr_pstrmemdup(scratch_pool, c->data, c->len);

          SVN_ERR(svn_wc__db_from_relpath(move_src_op_root_abspath,
                                          db, wri_abspath,
                                          move_src_op_root_relpath,
                                          result_pool, scratch_pool));
        }
      else
        *move_src_op_root_abspath = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_read_markers(const apr_array_header_t **markers,
                              svn_wc__db_t *db,
                              const char *wri_abspath,
                              const svn_skel_t *conflict_skel,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  const svn_skel_t *conflict;
  apr_array_header_t *list = NULL;

  SVN_ERR_ASSERT(conflict_skel != NULL);

  /* Walk the conflicts */
  for (conflict = conflict_skel->children->next->children;
       conflict;
       conflict = conflict->next)
    {
      const svn_skel_t *marker;

      /* Get the list of markers stored per conflict */
      for (marker = conflict->children->next->children;
           marker;
           marker = marker->next)
        {
          /* Skip placeholders */
          if (! marker->is_atom)
            continue;

          if (! list)
            list = apr_array_make(result_pool, 4, sizeof(const char *));

          SVN_ERR(svn_wc__db_from_relpath(
                        &APR_ARRAY_PUSH(list, const char*),
                        db, wri_abspath,
                        apr_pstrmemdup(scratch_pool, marker->data,
                                       marker->len),
                        result_pool, scratch_pool));
        }
    }
  *markers = list;

  return SVN_NO_ERROR;
}

/* --------------------------------------------------------------------
 */


svn_error_t *
svn_wc__conflict_create_markers(svn_skel_t **work_items,
                                svn_wc__db_t *db,
                                const char *local_abspath,
                                svn_skel_t *conflict_skel,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  svn_boolean_t prop_conflicted;
  svn_wc_operation_t operation;
  *work_items = NULL;

  SVN_ERR(svn_wc__conflict_read_info(&operation, NULL,
                                     NULL, &prop_conflicted, NULL,
                                     db, local_abspath,
                                     conflict_skel,
                                     scratch_pool, scratch_pool));

  if (prop_conflicted)
    {
      const char *marker_abspath = NULL;
      svn_node_kind_t kind;
      const char *marker_dir;
      const char *marker_name;
      const char *marker_relpath;

      /* Ok, currently we have to do a few things for property conflicts:
         - Create a marker file
         - Store the name in the conflict_skel
         - Create a WQ item that fills the marker with the expected data */

      SVN_ERR(svn_io_check_path(local_abspath, &kind, scratch_pool));

      if (kind == svn_node_dir)
        {
          marker_dir = local_abspath;
          marker_name = SVN_WC__THIS_DIR_PREJ;
        }
      else
        svn_dirent_split(&marker_dir, &marker_name, local_abspath,
                         scratch_pool);

      SVN_ERR(svn_io_open_uniquely_named(NULL, &marker_abspath,
                                         marker_dir,
                                         marker_name,
                                         SVN_WC__PROP_REJ_EXT,
                                         svn_io_file_del_none,
                                         scratch_pool, scratch_pool));

      SVN_ERR(svn_wc__db_to_relpath(&marker_relpath, db, local_abspath,
                                    marker_abspath, result_pool, result_pool));

      /* And store the marker in the skel */
      {
        svn_skel_t *prop_conflict;
        SVN_ERR(conflict__get_conflict(&prop_conflict, conflict_skel,
                                       SVN_WC__CONFLICT_KIND_PROP));

        svn_skel__prepend_str(marker_relpath, prop_conflict->children->next,
                            result_pool);
      }
      SVN_ERR(svn_wc__wq_build_prej_install(work_items,
                                            db, local_abspath,
                                            scratch_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Helper function for the three apply_* functions below, used when
 * merging properties together.
 *
 * Given property PROPNAME on LOCAL_ABSPATH, and four possible property
 * values, generate four tmpfiles and pass them to CONFLICT_FUNC callback.
 * This gives the client an opportunity to interactively resolve the
 * property conflict.
 *
 * BASE_VAL/WORKING_VAL represent the current state of the working
 * copy, and INCOMING_OLD_VAL/INCOMING_NEW_VAL represents the incoming
 * propchange.  Any of these values might be NULL, indicating either
 * non-existence or intent-to-delete.
 *
 * If the callback isn't available, or if it responds with
 * 'choose_postpone', then set *CONFLICT_REMAINS to TRUE and return.
 *
 * If the callback responds with a choice of 'base', 'theirs', 'mine',
 * or 'merged', then install the proper value into ACTUAL_PROPS and
 * set *CONFLICT_REMAINS to FALSE.
 */
static svn_error_t *
generate_propconflict(svn_boolean_t *conflict_remains,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      svn_node_kind_t kind,
                      svn_wc_operation_t operation,
                      const svn_wc_conflict_version_t *left_version,
                      const svn_wc_conflict_version_t *right_version,
                      const char *propname,
                      const svn_string_t *base_val,
                      const svn_string_t *working_val,
                      const svn_string_t *incoming_old_val,
                      const svn_string_t *incoming_new_val,
                      svn_wc_conflict_resolver_func2_t conflict_func,
                      void *conflict_baton,
                      svn_cancel_func_t cancel_func,
                      void *cancel_baton,
                      apr_pool_t *scratch_pool)
{
  svn_wc_conflict_result_t *result = NULL;
  svn_wc_conflict_description2_t *cdesc;
  const char *dirpath = svn_dirent_dirname(local_abspath, scratch_pool);
  const svn_string_t *new_value = NULL;

  cdesc = svn_wc_conflict_description_create_prop2(
                local_abspath,
                kind,
                propname, scratch_pool);

  cdesc->operation = operation;
  cdesc->src_left_version = left_version;
  cdesc->src_right_version = right_version;

  /* Create a tmpfile for each of the string_t's we've got.  */
  if (working_val)
    {
      const char *file_name;

      SVN_ERR(svn_io_write_unique(&file_name, dirpath, working_val->data,
                                  working_val->len,
                                  svn_io_file_del_on_pool_cleanup,
                                  scratch_pool));
      cdesc->my_abspath = svn_dirent_join(dirpath, file_name, scratch_pool);
      cdesc->prop_value_working = working_val;
    }

  if (incoming_new_val)
    {
      const char *file_name;

      SVN_ERR(svn_io_write_unique(&file_name, dirpath, incoming_new_val->data,
                                  incoming_new_val->len,
                                  svn_io_file_del_on_pool_cleanup,
                                  scratch_pool));

      /* ### For property conflicts, cd2 stores prop_reject_abspath in
       * ### their_abspath, and stores theirs_abspath in merged_file. */
      cdesc->merged_file = svn_dirent_join(dirpath, file_name, scratch_pool);
      cdesc->prop_value_incoming_new = incoming_new_val;
    }

  if (!base_val && !incoming_old_val)
    {
      /* If base and old are both NULL, then that's fine, we just let
         base_file stay NULL as-is.  Both agents are attempting to add a
         new property.  */
    }
  else if ((base_val && !incoming_old_val)
           || (!base_val && incoming_old_val))
    {
      /* If only one of base and old are defined, then we've got a
         situation where one agent is attempting to add the property
         for the first time, and the other agent is changing a
         property it thinks already exists.  In this case, we return
         whichever older-value happens to be defined, so that the
         conflict-callback can still attempt a 3-way merge. */

      const svn_string_t *conflict_base_val = base_val ? base_val
                                                       : incoming_old_val;
      const char *file_name;

      SVN_ERR(svn_io_write_unique(&file_name, dirpath,
                                  conflict_base_val->data,
                                  conflict_base_val->len,
                                  svn_io_file_del_on_pool_cleanup,
                                  scratch_pool));
      cdesc->base_abspath = svn_dirent_join(dirpath, file_name, scratch_pool);
    }
  else  /* base and old are both non-NULL */
    {
      const svn_string_t *conflict_base_val;
      const char *file_name;

      if (! svn_string_compare(base_val, incoming_old_val))
        {
          /* What happens if 'base' and 'old' don't match up?  In an
             ideal situation, they would.  But if they don't, this is
             a classic example of a patch 'hunk' failing to apply due
             to a lack of context.  For example: imagine that the user
             is busy changing the property from a value of "cat" to
             "dog", but the incoming propchange wants to change the
             same property value from "red" to "green".  Total context
             mismatch.

             HOWEVER: we can still pass one of the two base values as
             'base_file' to the callback anyway.  It's still useful to
             present the working and new values to the user to
             compare. */

          if (working_val && svn_string_compare(base_val, working_val))
            conflict_base_val = incoming_old_val;
          else
            conflict_base_val = base_val;
        }
      else
        {
          conflict_base_val = base_val;
        }

      SVN_ERR(svn_io_write_unique(&file_name, dirpath, conflict_base_val->data,
                                  conflict_base_val->len,
                                  svn_io_file_del_on_pool_cleanup, scratch_pool));
      cdesc->base_abspath = svn_dirent_join(dirpath, file_name, scratch_pool);

      cdesc->prop_value_base = base_val;
      cdesc->prop_value_incoming_old = incoming_old_val;

      if (working_val && incoming_new_val)
        {
          svn_stream_t *mergestream;
          svn_diff_t *diff;
          svn_diff_file_options_t *options =
            svn_diff_file_options_create(scratch_pool);

          SVN_ERR(svn_stream_open_unique(&mergestream, &cdesc->prop_reject_abspath,
                                         NULL, svn_io_file_del_on_pool_cleanup,
                                         scratch_pool, scratch_pool));
          SVN_ERR(svn_diff_mem_string_diff3(&diff, conflict_base_val,
                                            working_val,
                                            incoming_new_val, options, scratch_pool));
          SVN_ERR(svn_diff_mem_string_output_merge3(mergestream, diff,
                   conflict_base_val, working_val,
                   incoming_new_val, NULL, NULL, NULL, NULL,
                   svn_diff_conflict_display_modified_latest,
                   cancel_func, cancel_baton, scratch_pool));
          SVN_ERR(svn_stream_close(mergestream));

          /* ### For property conflicts, cd2 stores prop_reject_abspath in
           * ### their_abspath, and stores theirs_abspath in merged_file. */
          cdesc->their_abspath = cdesc->prop_reject_abspath;
        }
    }

  if (!incoming_old_val && incoming_new_val)
    cdesc->action = svn_wc_conflict_action_add;
  else if (incoming_old_val && !incoming_new_val)
    cdesc->action = svn_wc_conflict_action_delete;
  else
    cdesc->action = svn_wc_conflict_action_edit;

  if (base_val && !working_val)
    cdesc->reason = svn_wc_conflict_reason_deleted;
  else if (!base_val && working_val)
    cdesc->reason = svn_wc_conflict_reason_obstructed;
  else
    cdesc->reason = svn_wc_conflict_reason_edited;

  /* Invoke the interactive conflict callback. */
  SVN_ERR(conflict_func(&result, cdesc, conflict_baton, scratch_pool,
                        scratch_pool));
  if (result == NULL)
    {
      *conflict_remains = TRUE;
      return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                              NULL, _("Conflict callback violated API:"
                                      " returned no results"));
    }


  switch (result->choice)
    {
      default:
      case svn_wc_conflict_choose_postpone:
        {
          *conflict_remains = TRUE;
          break;
        }
      case svn_wc_conflict_choose_mine_full:
        {
          /* No need to change actual_props; it already contains working_val */
          *conflict_remains = FALSE;
          new_value = working_val;
          break;
        }
      /* I think _mine_full and _theirs_full are appropriate for prop
         behavior as well as the text behavior.  There should even be
         analogous behaviors for _mine and _theirs when those are
         ready, namely: fold in all non-conflicting prop changes, and
         then choose _mine side or _theirs side for conflicting ones. */
      case svn_wc_conflict_choose_theirs_full:
        {
          *conflict_remains = FALSE;
          new_value = incoming_new_val;
          break;
        }
      case svn_wc_conflict_choose_base:
        {
          *conflict_remains = FALSE;
          new_value = base_val;
          break;
        }
      case svn_wc_conflict_choose_merged:
        {
          if (!cdesc->merged_file 
              && (!result->merged_file && !result->merged_value))
            return svn_error_create
                (SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                 NULL, _("Conflict callback violated API:"
                         " returned no merged file"));

          if (result->merged_value)
            new_value = result->merged_value;
          else
            {
              svn_stringbuf_t *merged_stringbuf;

              SVN_ERR(svn_stringbuf_from_file2(&merged_stringbuf,
                                               result->merged_file ?
                                                    result->merged_file :
                                                    cdesc->merged_file,
                                               scratch_pool));
              new_value = svn_stringbuf__morph_into_string(merged_stringbuf);
            }
          *conflict_remains = FALSE;
          break;
        }
    }

  if (!*conflict_remains)
    {
      apr_hash_t *props;

      /* For now, just set the property values. This should really do some of the
         more advanced things from svn_wc_prop_set() */

      SVN_ERR(svn_wc__db_read_props(&props, db, local_abspath, scratch_pool,
                                    scratch_pool));

      svn_hash_sets(props, propname, new_value);

      SVN_ERR(svn_wc__db_op_set_props(db, local_abspath, props,
                                      FALSE, NULL, NULL,
                                      scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Perform a 3-way merge in which conflicts are expected, showing the
 * conflicts in the way specified by STYLE, and using MERGE_OPTIONS.
 *
 * The three input files are LEFT_ABSPATH (the base), DETRANSLATED_TARGET
 * and RIGHT_ABSPATH.  The output is stored in a new temporary file,
 * whose name is put into *CHOSEN_ABSPATH.
 *
 * The output file will be deleted according to DELETE_WHEN.  If
 * DELETE_WHEN is 'on pool cleanup', it refers to RESULT_POOL.
 *
 * DB and WRI_ABSPATH are used to choose a directory for the output file.
 *
 * Allocate *CHOSEN_ABSPATH in RESULT_POOL.  Use SCRATCH_POOL for temporary
 * allocations.
 */
static svn_error_t *
merge_showing_conflicts(const char **chosen_abspath,
                        svn_wc__db_t *db,
                        const char *wri_abspath,
                        svn_diff_conflict_display_style_t style,
                        const apr_array_header_t *merge_options,
                        const char *left_abspath,
                        const char *detranslated_target,
                        const char *right_abspath,
                        svn_io_file_del_t delete_when,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  const char *temp_dir;
  svn_stream_t *chosen_stream;
  svn_diff_t *diff;
  svn_diff_file_options_t *diff3_options;

  diff3_options = svn_diff_file_options_create(scratch_pool);
  if (merge_options)
    SVN_ERR(svn_diff_file_options_parse(diff3_options,
                                        merge_options,
                                        scratch_pool));

  SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&temp_dir, db,
                                         wri_abspath,
                                         scratch_pool, scratch_pool));
  /* We need to open the stream in RESULT_POOL because that controls the
   * lifetime of the file if DELETE_WHEN is 'on pool cleanup'.  (We also
   * want to allocate CHOSEN_ABSPATH in RESULT_POOL, but we don't care
   * about the stream itself.) */
  SVN_ERR(svn_stream_open_unique(&chosen_stream, chosen_abspath,
                                 temp_dir, delete_when,
                                 result_pool, scratch_pool));
  SVN_ERR(svn_diff_file_diff3_2(&diff,
                                left_abspath,
                                detranslated_target, right_abspath,
                                diff3_options, scratch_pool));
  SVN_ERR(svn_diff_file_output_merge3(chosen_stream, diff,
                                      left_abspath,
                                      detranslated_target,
                                      right_abspath,
                                      NULL, NULL, NULL, NULL, /* markers */
                                      style, cancel_func, cancel_baton,
                                      scratch_pool));
  SVN_ERR(svn_stream_close(chosen_stream));

  return SVN_NO_ERROR;
}

/* Prepare to delete an artifact file at ARTIFACT_FILE_ABSPATH in the
 * working copy at DB/WRI_ABSPATH.
 *
 * Set *WORK_ITEMS to a new work item that, when run, will delete the
 * artifact file; or to NULL if there is no file to delete.
 *
 * Set *FILE_FOUND to TRUE if the artifact file is found on disk and its
 * node kind is 'file'; otherwise do not change *FILE_FOUND.  FILE_FOUND
 * may be NULL if not required.
 */
static svn_error_t *
remove_artifact_file_if_exists(svn_skel_t **work_items,
                               svn_boolean_t *file_found,
                               svn_wc__db_t *db,
                               const char *wri_abspath,
                               const char *artifact_file_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  *work_items = NULL;
  if (artifact_file_abspath)
    {
      svn_node_kind_t node_kind;

      SVN_ERR(svn_io_check_path(artifact_file_abspath, &node_kind,
                                scratch_pool));
      if (node_kind == svn_node_file)
        {
          SVN_ERR(svn_wc__wq_build_file_remove(work_items,
                                               db, wri_abspath,
                                               artifact_file_abspath,
                                               result_pool, scratch_pool));
          if (file_found)
            *file_found = TRUE;
        }
    }

  return SVN_NO_ERROR;
}

/* Create a new file in the same directory as LOCAL_ABSPATH, with the
   same basename as LOCAL_ABSPATH, with a ".edited" extension, and set
   *WORK_ITEM to a new work item that will copy and translate from the file
   SOURCE_ABSPATH to that new file.  It will be translated from repository-
   normal form to working-copy form according to the versioned properties
   of LOCAL_ABSPATH that are current when the work item is executed.

   DB should have a write lock for the directory containing SOURCE.

   Allocate *WORK_ITEM in RESULT_POOL. */
static svn_error_t *
save_merge_result(svn_skel_t **work_item,
                  svn_wc__db_t *db,
                  const char *local_abspath,
                  const char *source_abspath,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  const char *edited_copy_abspath;
  const char *dir_abspath;
  const char *filename;

  svn_dirent_split(&dir_abspath, &filename, local_abspath, scratch_pool);

  /* ### Should use preserved-conflict-file-exts. */
  /* Create the .edited file within this file's DIR_ABSPATH  */
  SVN_ERR(svn_io_open_uniquely_named(NULL,
                                     &edited_copy_abspath,
                                     dir_abspath,
                                     filename,
                                     ".edited",
                                     svn_io_file_del_none,
                                     scratch_pool, scratch_pool));
  SVN_ERR(svn_wc__wq_build_file_copy_translated(work_item,
                                                db, local_abspath,
                                                source_abspath,
                                                edited_copy_abspath,
                                                result_pool, scratch_pool));
  return SVN_NO_ERROR;
}



/* Resolve the text conflict in CONFLICT, which is currently recorded
 * on DB/LOCAL_ABSPATH in the manner specified by CHOICE.
 *
 * Set *WORK_ITEMS to new work items that will make the on-disk changes
 * needed to complete the resolution (but not to mark it as resolved).
 *
 * Set *FOUND_ARTIFACT to true if conflict markers are removed; otherwise
 * (which is only if CHOICE is 'postpone') to false.
 *
 * CHOICE, MERGED_FILE and SAVE_MERGED are typically values provided by
 * the conflict resolver.
 *
 * MERGE_OPTIONS allows customizing the diff handling when using
 * per hunk conflict resolving.
 */
static svn_error_t *
build_text_conflict_resolve_items(svn_skel_t **work_items,
                                  svn_boolean_t *found_artifact,
                                  svn_wc__db_t *db,
                                  const char *local_abspath,
                                  const svn_skel_t *conflict,
                                  svn_wc_conflict_choice_t choice,
                                  const char *merged_file,
                                  svn_boolean_t save_merged,
                                  const apr_array_header_t *merge_options,
                                  svn_cancel_func_t cancel_func,
                                  void *cancel_baton,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  const char *mine_abspath;
  const char *their_old_abspath;
  const char *their_abspath;
  svn_skel_t *work_item;
  const char *install_from_abspath = NULL;
  svn_boolean_t remove_source = FALSE;

  *work_items = NULL;

  if (found_artifact)
    *found_artifact = FALSE;

  SVN_ERR(svn_wc__conflict_read_text_conflict(&mine_abspath,
                                              &their_old_abspath,
                                              &their_abspath,
                                              db, local_abspath,
                                              conflict,
                                              scratch_pool, scratch_pool));

  if (save_merged)
    SVN_ERR(save_merge_result(work_items,
                              db, local_abspath,
                              merged_file
                                ? merged_file
                                : local_abspath,
                              result_pool, scratch_pool));

  if (choice == svn_wc_conflict_choose_postpone)
    return SVN_NO_ERROR;

  switch (choice)
    {
      /* If the callback wants to use one of the fulltexts
         to resolve the conflict, so be it.*/
      case svn_wc_conflict_choose_base:
        {
          install_from_abspath = their_old_abspath;
          break;
        }
      case svn_wc_conflict_choose_theirs_full:
        {
          install_from_abspath = their_abspath;
          break;
        }
      case svn_wc_conflict_choose_mine_full:
        {
          /* In case of selecting to resolve the conflict choosing the full
             own file, allow the text conflict resolution to just take the
             existing local file if no merged file was present (case: binary
             file conflicts do not generate a locally merge file).
          */
          install_from_abspath = mine_abspath
                                   ? mine_abspath
                                   : local_abspath;
          break;
        }
      case svn_wc_conflict_choose_theirs_conflict:
      case svn_wc_conflict_choose_mine_conflict:
        {
          svn_diff_conflict_display_style_t style
            = choice == svn_wc_conflict_choose_theirs_conflict
                ? svn_diff_conflict_display_latest
                : svn_diff_conflict_display_modified;

          if (mine_abspath == NULL)
            return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                     _("Conflict on '%s' cannot be resolved to "
                                       "'theirs-conflict' or 'mine-conflict' "
                                       "because a merged version of the file "
                                       "cannot be created."),
                                     svn_dirent_local_style(local_abspath,
                                                            scratch_pool));

          SVN_ERR(merge_showing_conflicts(&install_from_abspath,
                                          db, local_abspath,
                                          style, merge_options,
                                          their_old_abspath,
                                          mine_abspath,
                                          their_abspath,
                                          /* ### why not same as other caller? */
                                          svn_io_file_del_none,
                                          cancel_func, cancel_baton,
                                          scratch_pool, scratch_pool));
          remove_source = TRUE;
          break;
        }

        /* For the case of 3-way file merging, we don't
           really distinguish between these return values;
           if the callback claims to have "generally
           resolved" the situation, we still interpret
           that as "OK, we'll assume the merged version is
           good to use". */
      case svn_wc_conflict_choose_merged:
        {
          install_from_abspath = merged_file
                                  ? merged_file
                                  : local_abspath;
          break;
        }
      case svn_wc_conflict_choose_postpone:
        {
          /* Assume conflict remains. */
          return SVN_NO_ERROR;
        }
      default:
        SVN_ERR_ASSERT(choice == svn_wc_conflict_choose_postpone);
    }

  if (install_from_abspath == NULL)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Conflict on '%s' could not be resolved "
                               "because the chosen version of the file "
                               "is not available."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  /* ### It would be nice if we could somehow pass RECORD_FILEINFO
         as true in some easy cases. */
  SVN_ERR(svn_wc__wq_build_file_install(&work_item,
                                        db, local_abspath,
                                        install_from_abspath,
                                        FALSE /* use_commit_times */,
                                        FALSE /* record_fileinfo */,
                                        result_pool, scratch_pool));
  *work_items = svn_wc__wq_merge(*work_items, work_item, result_pool);

  if (remove_source)
    {
      SVN_ERR(svn_wc__wq_build_file_remove(&work_item,
                                           db, local_abspath,
                                           install_from_abspath,
                                           result_pool, scratch_pool));
      *work_items = svn_wc__wq_merge(*work_items, work_item, result_pool);
    }

  SVN_ERR(remove_artifact_file_if_exists(&work_item, found_artifact,
                                         db, local_abspath,
                                         their_old_abspath,
                                         result_pool, scratch_pool));
  *work_items = svn_wc__wq_merge(*work_items, work_item, result_pool);

  SVN_ERR(remove_artifact_file_if_exists(&work_item, found_artifact,
                                         db, local_abspath,
                                         their_abspath,
                                         result_pool, scratch_pool));
  *work_items = svn_wc__wq_merge(*work_items, work_item, result_pool);

  SVN_ERR(remove_artifact_file_if_exists(&work_item, found_artifact,
                                         db, local_abspath,
                                         mine_abspath,
                                         result_pool, scratch_pool));
  *work_items = svn_wc__wq_merge(*work_items, work_item, result_pool);

  return SVN_NO_ERROR;
}


/* Set *DESC to a new description of the text conflict in
 * CONFLICT_SKEL.  If there is no text conflict in CONFLICT_SKEL, return
 * an error.
 *
 * Use OPERATION and shallow copies of LEFT_VERSION and RIGHT_VERSION,
 * rather than reading them from CONFLICT_SKEL.  Use IS_BINARY and
 * MIME_TYPE for the corresponding fields of *DESC.
 *
 * Allocate results in RESULT_POOL.  SCRATCH_POOL is used for temporary
 * allocations. */
static svn_error_t *
read_text_conflict_desc(svn_wc_conflict_description2_t **desc,
                        svn_wc__db_t *db,
                        const char *local_abspath,
                        const svn_skel_t *conflict_skel,
                        const char *mime_type,
                        svn_wc_operation_t operation,
                        const svn_wc_conflict_version_t *left_version,
                        const svn_wc_conflict_version_t *right_version,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  *desc = svn_wc_conflict_description_create_text2(local_abspath, result_pool);
  (*desc)->mime_type = mime_type;
  (*desc)->is_binary = mime_type ? svn_mime_type_is_binary(mime_type) : FALSE;
  (*desc)->operation = operation;
  (*desc)->src_left_version = left_version;
  (*desc)->src_right_version = right_version;

  SVN_ERR(svn_wc__conflict_read_text_conflict(&(*desc)->my_abspath,
                                              &(*desc)->base_abspath,
                                              &(*desc)->their_abspath,
                                              db, local_abspath,
                                              conflict_skel,
                                              result_pool, scratch_pool));
  (*desc)->merged_file = apr_pstrdup(result_pool, local_abspath);

  return SVN_NO_ERROR;
}

/* Set *CONFLICT_DESC to a new description of the tree conflict in
 * CONFLICT_SKEL.  If there is no tree conflict in CONFLICT_SKEL, return
 * an error.
 *
 * Use OPERATION and shallow copies of LEFT_VERSION and RIGHT_VERSION,
 * rather than reading them from CONFLICT_SKEL.
 *
 * Allocate results in RESULT_POOL.  SCRATCH_POOL is used for temporary
 * allocations. */
static svn_error_t *
read_tree_conflict_desc(svn_wc_conflict_description2_t **desc,
                        svn_wc__db_t *db,
                        const char *local_abspath,
                        svn_node_kind_t node_kind,
                        const svn_skel_t *conflict_skel,
                        svn_wc_operation_t operation,
                        const svn_wc_conflict_version_t *left_version,
                        const svn_wc_conflict_version_t *right_version,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_node_kind_t local_kind;
  svn_wc_conflict_reason_t reason;
  svn_wc_conflict_action_t action;

  SVN_ERR(svn_wc__conflict_read_tree_conflict(
            &reason, &action, NULL,
            db, local_abspath, conflict_skel, scratch_pool, scratch_pool));

  if (reason == svn_wc_conflict_reason_missing)
    local_kind = svn_node_none;
  else if (reason == svn_wc_conflict_reason_unversioned ||
           reason == svn_wc_conflict_reason_obstructed)
    SVN_ERR(svn_io_check_path(local_abspath, &local_kind, scratch_pool));
  else if (action == svn_wc_conflict_action_delete
           && left_version
           && (operation == svn_wc_operation_update
               ||operation == svn_wc_operation_switch)
           && (reason == svn_wc_conflict_reason_deleted
               || reason == svn_wc_conflict_reason_moved_away))
    {
      /* We have nothing locally to take the kind from */
      local_kind = left_version->node_kind;
    }
  else
    local_kind = node_kind;

  *desc = svn_wc_conflict_description_create_tree2(local_abspath, local_kind,
                                                   operation,
                                                   left_version, right_version,
                                                   result_pool);
  (*desc)->reason = reason;
  (*desc)->action = action;

  return SVN_NO_ERROR;
}

/* Forward definition */
static svn_error_t *
resolve_tree_conflict_on_node(svn_boolean_t *did_resolve,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              const svn_skel_t *conflict,
                              svn_wc_conflict_choice_t conflict_choice,
                              apr_hash_t *resolve_later,
                              svn_wc_notify_func2_t notify_func,
                              void *notify_baton,
                              svn_cancel_func_t cancel_func,
                              void *cancel_baton,
                              apr_pool_t *scratch_pool);

svn_error_t *
svn_wc__conflict_invoke_resolver(svn_wc__db_t *db,
                                 const char *local_abspath,
                                 svn_node_kind_t kind,
                                 const svn_skel_t *conflict_skel,
                                 const apr_array_header_t *merge_options,
                                 svn_wc_conflict_resolver_func2_t resolver_func,
                                 void *resolver_baton,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 apr_pool_t *scratch_pool)
{
  svn_boolean_t text_conflicted;
  svn_boolean_t prop_conflicted;
  svn_boolean_t tree_conflicted;
  svn_wc_operation_t operation;
  const apr_array_header_t *locations;
  const svn_wc_conflict_version_t *left_version = NULL;
  const svn_wc_conflict_version_t *right_version = NULL;

  SVN_ERR(svn_wc__conflict_read_info(&operation, &locations,
                                     &text_conflicted, &prop_conflicted,
                                     &tree_conflicted,
                                     db, local_abspath, conflict_skel,
                                     scratch_pool, scratch_pool));

  if (locations && locations->nelts > 0)
    left_version = APR_ARRAY_IDX(locations, 0, const svn_wc_conflict_version_t *);

  if (locations && locations->nelts > 1)
    right_version = APR_ARRAY_IDX(locations, 1, const svn_wc_conflict_version_t *);

  /* Quick and dirty compatibility wrapper. My guess would be that most resolvers
     would want to look at all properties at the same time.

     ### svn currently only invokes this from the merge code to collect the list of
     ### conflicted paths. Eventually this code will be the base for 'svn resolve'
     ### and at that time the test coverage will improve
     */
  if (prop_conflicted)
    {
      apr_hash_t *old_props;
      apr_hash_t *mine_props;
      apr_hash_t *their_props;
      apr_hash_t *old_their_props;
      apr_hash_t *conflicted;
      apr_pool_t *iterpool;
      apr_hash_index_t *hi;
      svn_boolean_t mark_resolved = TRUE;

      SVN_ERR(svn_wc__conflict_read_prop_conflict(NULL,
                                                  &mine_props,
                                                  &old_their_props,
                                                  &their_props,
                                                  &conflicted,
                                                  db, local_abspath,
                                                  conflict_skel,
                                                  scratch_pool, scratch_pool));

      if (operation == svn_wc_operation_merge)
        SVN_ERR(svn_wc__db_read_pristine_props(&old_props, db, local_abspath,
                                               scratch_pool, scratch_pool));
      else
        old_props = old_their_props;

      iterpool = svn_pool_create(scratch_pool);

      for (hi = apr_hash_first(scratch_pool, conflicted);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *propname = apr_hash_this_key(hi);
          svn_boolean_t conflict_remains = TRUE;

          svn_pool_clear(iterpool);

          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          SVN_ERR(generate_propconflict(&conflict_remains,
                                        db, local_abspath, kind,
                                        operation,
                                        left_version,
                                        right_version,
                                        propname,
                                        old_props
                                          ? svn_hash_gets(old_props, propname)
                                          : NULL,
                                        mine_props
                                          ? svn_hash_gets(mine_props, propname)
                                          : NULL,
                                        old_their_props
                                          ? svn_hash_gets(old_their_props, propname)
                                          : NULL,
                                        their_props
                                          ? svn_hash_gets(their_props, propname)
                                          : NULL,
                                        resolver_func, resolver_baton,
                                        cancel_func, cancel_baton,
                                        iterpool));

          if (conflict_remains)
            mark_resolved = FALSE;
        }

      if (mark_resolved)
        {
          SVN_ERR(svn_wc__mark_resolved_prop_conflicts(db, local_abspath,
                                                       scratch_pool));
        }
      svn_pool_destroy(iterpool);
    }

  if (text_conflicted)
    {
      svn_skel_t *work_items;
      svn_boolean_t was_resolved;
      svn_wc_conflict_description2_t *desc;
      apr_hash_t *props;
      svn_wc_conflict_result_t *result;

      SVN_ERR(svn_wc__db_read_props(&props, db, local_abspath,
                                    scratch_pool, scratch_pool));

      SVN_ERR(read_text_conflict_desc(&desc,
                                      db, local_abspath, conflict_skel,
                                      svn_prop_get_value(props,
                                                         SVN_PROP_MIME_TYPE),
                                      operation, left_version, right_version,
                                      scratch_pool, scratch_pool));


      work_items = NULL;
      was_resolved = FALSE;

      /* Give the conflict resolution callback a chance to clean
         up the conflicts before we mark the file 'conflicted' */

      SVN_ERR(resolver_func(&result, desc, resolver_baton, scratch_pool,
                            scratch_pool));
      if (result == NULL)
        return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                _("Conflict callback violated API:"
                                  " returned no results"));

      SVN_ERR(build_text_conflict_resolve_items(&work_items, &was_resolved,
                                                db, local_abspath,
                                                conflict_skel, result->choice,
                                                result->merged_file,
                                                result->save_merged,
                                                merge_options,
                                                cancel_func, cancel_baton,
                                                scratch_pool, scratch_pool));

      if (result->choice != svn_wc_conflict_choose_postpone)
        {
          SVN_ERR(svn_wc__db_op_mark_resolved(db, local_abspath,
                                              TRUE, FALSE, FALSE,
                                              work_items, scratch_pool));
          SVN_ERR(svn_wc__wq_run(db, local_abspath,
                                 cancel_func, cancel_baton,
                                 scratch_pool));
        }
    }

  if (tree_conflicted)
    {
      svn_wc_conflict_result_t *result;
      svn_wc_conflict_description2_t *desc;
      svn_boolean_t resolved;
      svn_node_kind_t node_kind;

      SVN_ERR(svn_wc__db_read_kind(&node_kind, db, local_abspath, TRUE,
                                   TRUE, FALSE, scratch_pool));

      SVN_ERR(read_tree_conflict_desc(&desc,
                                      db, local_abspath, node_kind,
                                      conflict_skel,
                                      operation, left_version, right_version,
                                      scratch_pool, scratch_pool));

      /* Tell the resolver func about this conflict. */
      SVN_ERR(resolver_func(&result, desc, resolver_baton, scratch_pool,
                            scratch_pool));

      if (result == NULL)
        return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                _("Conflict callback violated API:"
                                  " returned no results"));

      /* Pass retry hash to avoid erroring out on cases where update
         can continue safely. ### Need notify handling */
      if (result->choice != svn_wc_conflict_choose_postpone)
        SVN_ERR(resolve_tree_conflict_on_node(&resolved,
                                              db, local_abspath, conflict_skel,
                                              result->choice,
                                              apr_hash_make(scratch_pool),
                                              NULL, NULL, /* ### notify */
                                              cancel_func, cancel_baton,
                                              scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Read all property conflicts contained in CONFLICT_SKEL into
 * individual conflict descriptions, and append those descriptions
 * to the CONFLICTS array.  If there is no property conflict in
 * CONFLICT_SKEL, return an error.
 *
 * If NOT create_tempfiles, always create a legacy property conflict
 * descriptor.
 *
 * Use NODE_KIND, OPERATION and shallow copies of LEFT_VERSION and
 * RIGHT_VERSION, rather than reading them from CONFLICT_SKEL.
 *
 * Allocate results in RESULT_POOL. SCRATCH_POOL is used for temporary
 * allocations. */
static svn_error_t *
read_prop_conflict_descs(apr_array_header_t *conflicts,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         svn_skel_t *conflict_skel,
                         svn_boolean_t create_tempfiles,
                         svn_node_kind_t node_kind,
                         svn_wc_operation_t operation,
                         const svn_wc_conflict_version_t *left_version,
                         const svn_wc_conflict_version_t *right_version,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  const char *prop_reject_abspath;
  apr_hash_t *base_props;
  apr_hash_t *my_props;
  apr_hash_t *their_old_props;
  apr_hash_t *their_props;
  apr_hash_t *conflicted_props;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;
  svn_boolean_t prop_conflicted;

  SVN_ERR(svn_wc__conflict_read_info(&operation, NULL, NULL, &prop_conflicted,
                                     NULL, db, local_abspath, conflict_skel,
                                     scratch_pool, scratch_pool));

  if (!prop_conflicted)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__conflict_read_prop_conflict(&prop_reject_abspath,
                                              &my_props,
                                              &their_old_props,
                                              &their_props,
                                              &conflicted_props,
                                              db, local_abspath,
                                              conflict_skel,
                                              scratch_pool, scratch_pool));

  prop_reject_abspath = apr_pstrdup(result_pool, prop_reject_abspath);

  if (apr_hash_count(conflicted_props) == 0)
    {
      /* Legacy prop conflict with only a .reject file. */
      svn_wc_conflict_description2_t *desc;

      desc  = svn_wc_conflict_description_create_prop2(local_abspath,
                                                       node_kind,
                                                       "", result_pool);

      /* ### For property conflicts, cd2 stores prop_reject_abspath in
       * ### their_abspath, and stores theirs_abspath in merged_file. */
      desc->prop_reject_abspath = prop_reject_abspath; /* in result_pool */
      desc->their_abspath = desc->prop_reject_abspath;

      desc->operation = operation;
      desc->src_left_version = left_version;
      desc->src_right_version = right_version;

      APR_ARRAY_PUSH(conflicts, svn_wc_conflict_description2_t *) = desc;

      return SVN_NO_ERROR;
    }

  if (operation == svn_wc_operation_merge)
    SVN_ERR(svn_wc__db_read_pristine_props(&base_props, db, local_abspath,
                                           result_pool, scratch_pool));
  else
    base_props = NULL;
  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, conflicted_props);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *propname = apr_hash_this_key(hi);
      svn_string_t *old_value;
      svn_string_t *my_value;
      svn_string_t *their_value;
      svn_wc_conflict_description2_t *desc;

      svn_pool_clear(iterpool);

      desc = svn_wc_conflict_description_create_prop2(local_abspath,
                                                      node_kind,
                                                      propname,
                                                      result_pool);

      desc->operation = operation;
      desc->src_left_version = left_version;
      desc->src_right_version = right_version;

      desc->property_name = apr_pstrdup(result_pool, propname);

      my_value = svn_hash_gets(my_props, propname);
      their_value = svn_hash_gets(their_props, propname);
      old_value = svn_hash_gets(their_old_props, propname);

      /* Compute the incoming side of the conflict ('action'). */
      if (their_value == NULL)
        desc->action = svn_wc_conflict_action_delete;
      else if (old_value == NULL)
        desc->action = svn_wc_conflict_action_add;
      else
        desc->action = svn_wc_conflict_action_edit;

      /* Compute the local side of the conflict ('reason'). */
      if (my_value == NULL)
        desc->reason = svn_wc_conflict_reason_deleted;
      else if (old_value == NULL)
        desc->reason = svn_wc_conflict_reason_added;
      else
        desc->reason = svn_wc_conflict_reason_edited;

      /* ### For property conflicts, cd2 stores prop_reject_abspath in
       * ### their_abspath, and stores theirs_abspath in merged_file. */
      desc->prop_reject_abspath = prop_reject_abspath; /* in result_pool */
      desc->their_abspath = desc->prop_reject_abspath;

      desc->prop_value_base = base_props ? svn_hash_gets(base_props, propname)
                                         : desc->prop_value_incoming_old;

      if (my_value)
        {
          svn_stream_t *s;
          apr_size_t len;

          if (create_tempfiles)
            {
              SVN_ERR(svn_stream_open_unique(&s, &desc->my_abspath, NULL,
                                             svn_io_file_del_on_pool_cleanup,
                                             result_pool, iterpool));
              len = my_value->len;
              SVN_ERR(svn_stream_write(s, my_value->data, &len));
              SVN_ERR(svn_stream_close(s));
            }

          desc->prop_value_working = svn_string_dup(my_value, result_pool);
        }

      if (their_value)
        {
          svn_stream_t *s;
          apr_size_t len;

          /* ### For property conflicts, cd2 stores prop_reject_abspath in
           * ### their_abspath, and stores theirs_abspath in merged_file. */
          if (create_tempfiles)
            {
              SVN_ERR(svn_stream_open_unique(&s, &desc->merged_file, NULL,
                                             svn_io_file_del_on_pool_cleanup,
                                             result_pool, iterpool));
              len = their_value->len;
              SVN_ERR(svn_stream_write(s, their_value->data, &len));
              SVN_ERR(svn_stream_close(s));
            }

          desc->prop_value_incoming_new = svn_string_dup(their_value, result_pool);
        }

      if (old_value)
        {
          svn_stream_t *s;
          apr_size_t len;

          if (create_tempfiles)
            {
              SVN_ERR(svn_stream_open_unique(&s, &desc->base_abspath, NULL,
                                             svn_io_file_del_on_pool_cleanup,
                                             result_pool, iterpool));
              len = old_value->len;
              SVN_ERR(svn_stream_write(s, old_value->data, &len));
              SVN_ERR(svn_stream_close(s));
            }

          desc->prop_value_incoming_old = svn_string_dup(old_value, result_pool);
        }

      APR_ARRAY_PUSH(conflicts, svn_wc_conflict_description2_t *) = desc;
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__read_conflicts(const apr_array_header_t **conflicts,
                       svn_skel_t **conflict_skel,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       svn_boolean_t create_tempfiles,
                       svn_boolean_t only_tree_conflict,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_skel_t *the_conflict_skel;
  apr_array_header_t *cflcts;
  svn_boolean_t prop_conflicted;
  svn_boolean_t text_conflicted;
  svn_boolean_t tree_conflicted;
  svn_wc_operation_t operation;
  const apr_array_header_t *locations;
  const svn_wc_conflict_version_t *left_version = NULL;
  const svn_wc_conflict_version_t *right_version = NULL;
  svn_node_kind_t node_kind;
  apr_hash_t *props;

  if (!conflict_skel)
    conflict_skel = &the_conflict_skel;

  SVN_ERR(svn_wc__db_read_conflict(conflict_skel, &node_kind, &props,
                                   db, local_abspath,
                                   (conflict_skel == &the_conflict_skel)
                                        ? scratch_pool
                                        : result_pool,
                                   scratch_pool));

  if (!*conflict_skel)
    {
      /* Some callers expect not NULL */
      *conflicts = apr_array_make(result_pool, 0,
                                  sizeof(svn_wc_conflict_description2_t *));
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_wc__conflict_read_info(&operation, &locations, &text_conflicted,
                                     &prop_conflicted, &tree_conflicted,
                                     db, local_abspath, *conflict_skel,
                                     result_pool, scratch_pool));

  cflcts = apr_array_make(result_pool, 4,
                          sizeof(svn_wc_conflict_description2_t *));

  if (locations && locations->nelts > 0)
    left_version = APR_ARRAY_IDX(locations, 0, const svn_wc_conflict_version_t *);
  if (locations && locations->nelts > 1)
    right_version = APR_ARRAY_IDX(locations, 1, const svn_wc_conflict_version_t *);

  if (prop_conflicted && !only_tree_conflict)
    {
      SVN_ERR(read_prop_conflict_descs(cflcts,
                                       db, local_abspath, *conflict_skel,
                                       create_tempfiles, node_kind,
                                       operation, left_version, right_version,
                                       result_pool, scratch_pool));
    }

  if (text_conflicted && !only_tree_conflict)
    {
      svn_wc_conflict_description2_t *desc;

      SVN_ERR(read_text_conflict_desc(&desc,
                                      db, local_abspath, *conflict_skel,
                                      svn_prop_get_value(props,
                                                         SVN_PROP_MIME_TYPE),
                                      operation, left_version, right_version,
                                      result_pool, scratch_pool));
      APR_ARRAY_PUSH(cflcts, svn_wc_conflict_description2_t *) = desc;
    }

  if (tree_conflicted)
    {
      svn_wc_conflict_description2_t *desc;

      SVN_ERR(read_tree_conflict_desc(&desc,
                                      db, local_abspath, node_kind,
                                      *conflict_skel,
                                      operation, left_version, right_version,
                                      result_pool, scratch_pool));

      APR_ARRAY_PUSH(cflcts, const svn_wc_conflict_description2_t *) = desc;
    }

  *conflicts = cflcts;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__read_conflict_descriptions2_t(const apr_array_header_t **conflicts,
                                      svn_wc_context_t *wc_ctx,
                                      const char *local_abspath,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool)
{
  return svn_wc__read_conflicts(conflicts, NULL, wc_ctx->db, local_abspath, 
                                FALSE, FALSE, result_pool, scratch_pool);
}


/*** Resolving a conflict automatically ***/

/*
 * Resolve the property conflicts found in DB/LOCAL_ABSPATH according
 * to CONFLICT_CHOICE.
 *
 * It is not an error if there is no prop conflict. If a prop conflict
 * existed and was resolved, set *DID_RESOLVE to TRUE, else set it to FALSE.
 *
 * Note: When there are no conflict markers on-disk to remove there is
 * no existing text conflict (unless we are still in the process of
 * creating the text conflict and we didn't register a marker file yet).
 * In this case the database contains old information, which we should
 * remove to avoid checking the next time. Resolving a property conflict
 * by just removing the marker file is a fully supported scenario since
 * Subversion 1.0.
 *
 * ### TODO [JAF] The '*_full' and '*_conflict' choices should differ.
 *     In my opinion, 'mine_full'/'theirs_full' should select
 *     the entire set of properties from 'mine' or 'theirs' respectively,
 *     while 'mine_conflict'/'theirs_conflict' should select just the
 *     properties that are in conflict.  Or, '_full' should select the
 *     entire property whereas '_conflict' should do a text merge within
 *     each property, selecting hunks.  Or all three kinds of behaviour
 *     should be available (full set of props, full value of conflicting
 *     props, or conflicting text hunks).
 * ### BH: If we make *_full select the full set of properties, we should
 *     check if we shouldn't make it also select the full text for files.
 *
 * ### TODO [JAF] All this complexity should not be down here in libsvn_wc
 *     but in a layer above.
 *
 * ### TODO [JAF] Options for 'base' should be like options for 'mine' and
 *     for 'theirs' -- choose full set of props, full value of conflicting
 *     props, or conflicting text hunks.
 *
 */
static svn_error_t *
resolve_prop_conflict_on_node(svn_boolean_t *did_resolve,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              svn_skel_t *conflicts,
                              const char *conflicted_propname,
                              svn_wc_conflict_choice_t conflict_choice,
                              const char *merged_file,
                              const svn_string_t *merged_value,
                              svn_cancel_func_t cancel_func,
                              void *cancel_baton,
                              apr_pool_t *scratch_pool)
{
  const char *prop_reject_file;
  apr_hash_t *mine_props;
  apr_hash_t *their_old_props;
  apr_hash_t *their_props;
  apr_hash_t *conflicted_props;
  apr_hash_t *old_props;
  apr_hash_t *resolve_from = NULL;
  svn_skel_t *work_items = NULL;
  svn_wc_operation_t operation;
  svn_boolean_t prop_conflicted;
  apr_hash_t *actual_props;
  svn_boolean_t resolved_all, resolved_all_prop;

  *did_resolve = FALSE;

  SVN_ERR(svn_wc__conflict_read_info(&operation, NULL, NULL, &prop_conflicted,
                                     NULL, db, local_abspath, conflicts,
                                     scratch_pool, scratch_pool));
  if (!prop_conflicted)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__conflict_read_prop_conflict(&prop_reject_file,
                                              &mine_props, &their_old_props,
                                              &their_props, &conflicted_props,
                                              db, local_abspath, conflicts,
                                              scratch_pool, scratch_pool));

  if (!conflicted_props)
    {
      /* We have a pre 1.8 property conflict. Just mark it resolved */

      SVN_ERR(remove_artifact_file_if_exists(&work_items, did_resolve,
                                             db, local_abspath, prop_reject_file,
                                             scratch_pool, scratch_pool));
      SVN_ERR(svn_wc__db_op_mark_resolved(db, local_abspath, FALSE, TRUE, FALSE,
                                      work_items, scratch_pool));
      SVN_ERR(svn_wc__wq_run(db, local_abspath, cancel_func, cancel_baton,
                             scratch_pool));
      return SVN_NO_ERROR;
    }

  if (conflicted_propname[0] != '\0'
      && !svn_hash_gets(conflicted_props, conflicted_propname))
    {
      return SVN_NO_ERROR; /* This property is not conflicted! */
    }

  if (operation == svn_wc_operation_merge)
      SVN_ERR(svn_wc__db_read_pristine_props(&old_props, db, local_abspath,
                                             scratch_pool, scratch_pool));
    else
      old_props = their_old_props;

  SVN_ERR(svn_wc__db_read_props(&actual_props, db, local_abspath,
                                scratch_pool, scratch_pool));

  /* We currently handle *_conflict as *_full as this argument is currently
     always applied for all conflicts on a node at the same time. Giving
     an error would break some tests that assumed that this would just
     resolve property conflicts to working.

     An alternative way to handle these conflicts would be to just copy all
     property state from mine/theirs on the _full option instead of just the
     conflicted properties. In some ways this feels like a sensible option as
     that would take both properties and text from mine/theirs, but when not
     both properties and text are conflicted we would fail in doing so.
   */
  switch (conflict_choice)
    {
    case svn_wc_conflict_choose_base:
      resolve_from = their_old_props ? their_old_props : old_props;
      break;
    case svn_wc_conflict_choose_mine_full:
    case svn_wc_conflict_choose_mine_conflict:
      resolve_from = mine_props;
      break;
    case svn_wc_conflict_choose_theirs_full:
    case svn_wc_conflict_choose_theirs_conflict:
      resolve_from = their_props;
      break;
    case svn_wc_conflict_choose_merged:
      if ((merged_file || merged_value) && conflicted_propname[0] != '\0')
        {
          resolve_from = apr_hash_copy(scratch_pool, actual_props);

          if (!merged_value)
            {
              svn_stringbuf_t *merged_propval;

              SVN_ERR(svn_stringbuf_from_file2(&merged_propval, merged_file,
                                               scratch_pool));

              merged_value = svn_stringbuf__morph_into_string(merged_propval);
            }
          svn_hash_sets(resolve_from, conflicted_propname, merged_value);
        }
      else
        resolve_from = NULL;
      break;
    default:
      return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                              _("Invalid 'conflict_result' argument"));
    }


  if (resolve_from)
    {
      apr_hash_index_t *hi;
      apr_hash_t *apply_on_props;

      if (conflicted_propname[0] == '\0')
        {
          /* Apply to all conflicted properties */
          apply_on_props = conflicted_props;
        }
      else
        {
          /* Apply to a single property */
          apply_on_props = apr_hash_make(scratch_pool);
          svn_hash_sets(apply_on_props, conflicted_propname, "");
        }

      /* Apply the selected changes */
      for (hi = apr_hash_first(scratch_pool, apply_on_props);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *propname = apr_hash_this_key(hi);
          svn_string_t *new_value = NULL;

          new_value = svn_hash_gets(resolve_from, propname);

          svn_hash_sets(actual_props, propname, new_value);
        }
    }
  /*else the user accepted the properties as-is */

  /* This function handles conflicted_propname "" as resolving
     all property conflicts... Just what we need here */
  SVN_ERR(svn_wc__conflict_skel_resolve(&resolved_all, conflicts,
                                        db, local_abspath,
                                        FALSE, conflicted_propname,
                                        FALSE,
                                        scratch_pool, scratch_pool));

  if (!resolved_all)
    {
      /* Are there still property conflicts left? (or only...) */
      SVN_ERR(svn_wc__conflict_read_info(NULL, NULL, NULL, &prop_conflicted,
                                         NULL, db, local_abspath, conflicts,
                                         scratch_pool, scratch_pool));

      resolved_all_prop = (! prop_conflicted);
    }
  else
    {
      resolved_all_prop = TRUE;
      conflicts = NULL;
    }

  if (resolved_all_prop)
    {
      /* Legacy behavior: Only report property conflicts as resolved when the
         property reject file exists

         If not the UI shows the conflict as already resolved
         (and in this case we just remove the in-db conflict) */
      SVN_ERR(remove_artifact_file_if_exists(&work_items, did_resolve,
                                             db, local_abspath,
                                             prop_reject_file,
                                             scratch_pool, scratch_pool));
    }
  else
    {
      /* Create a new prej file, based on the remaining conflicts */
      SVN_ERR(svn_wc__wq_build_prej_install(&work_items,
                                            db, local_abspath,
                                            scratch_pool, scratch_pool));
      *did_resolve = TRUE; /* We resolved a property conflict */
    }

  /* This installs the updated conflict skel */
  SVN_ERR(svn_wc__db_op_set_props(db, local_abspath, actual_props,
                                  FALSE, conflicts, work_items,
                                  scratch_pool));

  if (resolved_all)
    {
      /* Remove the whole conflict. Should probably be integrated
         into the op_set_props() call */
      SVN_ERR(svn_wc__db_op_mark_resolved(db, local_abspath,
                                          FALSE, TRUE, FALSE,
                                          NULL, scratch_pool));
    }

  SVN_ERR(svn_wc__wq_run(db, local_abspath, cancel_func, cancel_baton,
                         scratch_pool));

  return SVN_NO_ERROR;
}

/* 
 * Record a tree conflict resolution failure due to error condition ERR
 * in the RESOLVE_LATER hash table. If the hash table is not available
 * (meaning the caller does not wish to retry resolution later), or if
 * the error condition does not indicate circumstances where another
 * existing tree conflict is blocking the resolution attempt, then
 * return the error ERR itself.
 */
static svn_error_t *
handle_tree_conflict_resolution_failure(const char *local_abspath,
                                        svn_error_t *err,
                                        apr_hash_t *resolve_later)
{
  const char *dup_abspath;

  if (!resolve_later
      || (err->apr_err != SVN_ERR_WC_OBSTRUCTED_UPDATE
          && err->apr_err != SVN_ERR_WC_FOUND_CONFLICT))
    return svn_error_trace(err); /* Give up. Do not retry resolution later. */

  svn_error_clear(err);
  dup_abspath = apr_pstrdup(apr_hash_pool_get(resolve_later),
                            local_abspath);

  svn_hash_sets(resolve_later, dup_abspath, dup_abspath);

  return SVN_NO_ERROR; /* Caller may retry after resolving other conflicts. */
}

/*
 * Resolve the tree conflict found in DB/LOCAL_ABSPATH according to
 * CONFLICT_CHOICE.
 *
 * It is not an error if there is no tree conflict. If a tree conflict
 * existed and was resolved, set *DID_RESOLVE to TRUE, else set it to FALSE.
 *
 * It is not an error if there is no tree conflict.
 *
 * If the conflict can't be resolved yet (e.g. because another tree conflict
 * is blocking a storage location), and RESOLVE_LATER is not NULL, store the
 * tree conflict in RESOLVE_LATER and do not mark the conflict resolved.
 * Else if RESOLVE_LATER is NULL, do not mark the conflict resolved and
 * return the error which prevented the conflict from being marked resolved.
 */
static svn_error_t *
resolve_tree_conflict_on_node(svn_boolean_t *did_resolve,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              const svn_skel_t *conflicts,
                              svn_wc_conflict_choice_t conflict_choice,
                              apr_hash_t *resolve_later,
                              svn_wc_notify_func2_t notify_func,
                              void *notify_baton,
                              svn_cancel_func_t cancel_func,
                              void *cancel_baton,
                              apr_pool_t *scratch_pool)
{
  svn_wc_conflict_reason_t reason;
  svn_wc_conflict_action_t action;
  svn_wc_operation_t operation;
  svn_boolean_t tree_conflicted;
  const char *src_op_root_abspath;

  *did_resolve = FALSE;

  SVN_ERR(svn_wc__conflict_read_info(&operation, NULL, NULL, NULL,
                                     &tree_conflicted, db, local_abspath,
                                     conflicts, scratch_pool, scratch_pool));
  if (!tree_conflicted)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__conflict_read_tree_conflict(&reason, &action,
                                              &src_op_root_abspath,
                                              db, local_abspath,
                                              conflicts,
                                              scratch_pool, scratch_pool));

  if (operation == svn_wc_operation_update
      || operation == svn_wc_operation_switch)
    {
      svn_error_t *err;
      if (reason == svn_wc_conflict_reason_deleted ||
          reason == svn_wc_conflict_reason_replaced)
        {
          if (conflict_choice == svn_wc_conflict_choose_merged)
            {
              /* Break moves for any children moved out of this directory,
               * and leave this directory deleted. */

              if (action != svn_wc_conflict_action_delete)
                {
                  SVN_ERR(svn_wc__db_op_break_moved_away(
                                  db, local_abspath, src_op_root_abspath, TRUE,
                                  notify_func, notify_baton,
                                  scratch_pool));
                  *did_resolve = TRUE;
                  return SVN_NO_ERROR; /* Marked resolved by function*/
                }
              /* else # The move is/moves are already broken */


              *did_resolve = TRUE;
            }
          else if (conflict_choice == svn_wc_conflict_choose_mine_conflict)
            {
              svn_skel_t *new_conflicts;

              /* Raise local moved-away vs. incoming edit conflicts on
               * any children moved out of this directory, and leave
               * this directory as-is.
               *
               * The newly conflicted moved-away children will be updated
               * if they are resolved with 'mine_conflict' as well. */
              err = svn_wc__db_op_raise_moved_away(
                        db, local_abspath, notify_func, notify_baton,
                        scratch_pool);

              if (err)
                SVN_ERR(handle_tree_conflict_resolution_failure(
                          local_abspath, err, resolve_later));

              /* We might now have a moved-away on *this* path, let's
                 try to resolve that directly if that is the case */
              SVN_ERR(svn_wc__db_read_conflict(&new_conflicts, NULL, NULL,
                                               db, local_abspath,
                                               scratch_pool, scratch_pool));

              if (new_conflicts)
                SVN_ERR(svn_wc__conflict_read_info(NULL, NULL, NULL, NULL,
                                                   &tree_conflicted,
                                                   db, local_abspath,
                                                   new_conflicts,
                                                   scratch_pool,
                                                   scratch_pool));

              if (!new_conflicts || !tree_conflicted)
                {
                  /* TC is marked resolved by calling
                     svn_wc__db_op_raise_moved_away */
                  *did_resolve = TRUE;
                  return SVN_NO_ERROR;
                }

              SVN_ERR(svn_wc__conflict_read_tree_conflict(&reason, &action,
                                                          &src_op_root_abspath,
                                                          db, local_abspath,
                                                          new_conflicts,
                                                          scratch_pool,
                                                          scratch_pool));

              if (reason != svn_wc_conflict_reason_moved_away)
                {
                  *did_resolve = TRUE;
                  return SVN_NO_ERROR; /* We fixed one, but... */
                }

              conflicts = new_conflicts;
              /* Fall through in moved_away handling */
            }
          else
            return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                                     NULL,
                                     _("Tree conflict can only be resolved to "
                                       "'working' or 'mine-conflict' state; "
                                       "'%s' not resolved"),
                                     svn_dirent_local_style(local_abspath,
                                                            scratch_pool));
        }

      if (reason == svn_wc_conflict_reason_moved_away
           && action == svn_wc_conflict_action_edit)
        {
          /* After updates, we can resolve local moved-away
           * vs. any incoming change, either by updating the
           * moved-away node (mine-conflict) or by breaking the
           * move (theirs-conflict). */
          if (conflict_choice == svn_wc_conflict_choose_mine_conflict)
            {
              err = svn_wc__db_update_moved_away_conflict_victim(
                        db, local_abspath, src_op_root_abspath,
                        operation, action, reason,
                        cancel_func, cancel_baton,
                        notify_func, notify_baton,
                        scratch_pool);

              if (err)
                SVN_ERR(handle_tree_conflict_resolution_failure(
                          local_abspath, err, resolve_later));
              else
                *did_resolve = TRUE;
            }
          else if (conflict_choice == svn_wc_conflict_choose_merged)
            {
              /* We must break the move if the user accepts the current
               * working copy state instead of updating the move.
               * Else the move would be left in an invalid state. */

              SVN_ERR(svn_wc__db_op_break_moved_away(db, local_abspath,
                                                     src_op_root_abspath, TRUE,
                                                     notify_func, notify_baton,
                                                     scratch_pool));
              *did_resolve = TRUE;
              return SVN_NO_ERROR; /* Conflict is marked resolved */
            }
          else
            return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                                     NULL,
                                     _("Tree conflict can only be resolved to "
                                       "'working' or 'mine-conflict' state; "
                                       "'%s' not resolved"),
                                     svn_dirent_local_style(local_abspath,
                                                            scratch_pool));
        }
      else if (reason == svn_wc_conflict_reason_moved_away
               && action != svn_wc_conflict_action_edit)
        {
          /* action added is impossible, because that would imply that
             something was added, but before that already moved...
             (which would imply a replace) */
          SVN_ERR_ASSERT(action == svn_wc_conflict_action_delete
                         || action == svn_wc_conflict_action_replace);

          if (conflict_choice == svn_wc_conflict_choose_merged)
            {
              /* Whatever was moved is removed at its original location by the
                 update. That must also remove the recording of the move, so
                 we don't have to do anything here. */

              *did_resolve = TRUE;
            }
          else if (conflict_choice == svn_wc_conflict_choose_mine_conflict)
            {
              return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                                       NULL,
                                       _("Tree conflict can only be "
                                         "resolved to 'working' state; "
                                         "'%s' is no longer moved"),
                                       svn_dirent_local_style(local_abspath,
                                                              scratch_pool));
            }
        }
    }

  if (! *did_resolve)
    {
      if (conflict_choice != svn_wc_conflict_choose_merged)
        {
          /* For other tree conflicts, there is no way to pick
           * theirs-full or mine-full, etc. Throw an error if the
           * user expects us to be smarter than we really are. */
          return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                                   NULL,
                                   _("Tree conflict can only be "
                                     "resolved to 'working' state; "
                                     "'%s' not resolved"),
                                   svn_dirent_local_style(local_abspath,
                                                          scratch_pool));
        }
      else
        *did_resolve = TRUE;
    }

  SVN_ERR_ASSERT(*did_resolve);

  SVN_ERR(svn_wc__db_op_mark_resolved(db, local_abspath, FALSE, FALSE, TRUE,
                                      NULL, scratch_pool));
  SVN_ERR(svn_wc__wq_run(db, local_abspath, cancel_func, cancel_baton,
                         scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__mark_resolved_text_conflict(svn_wc__db_t *db,
                                    const char *local_abspath,
                                    svn_cancel_func_t cancel_func,
                                    void *cancel_baton,
                                    apr_pool_t *scratch_pool)
{
  svn_skel_t *work_items;
  svn_skel_t *conflict;

  SVN_ERR(svn_wc__db_read_conflict(&conflict, NULL, NULL,
                                   db, local_abspath,
                                   scratch_pool, scratch_pool));

  if (!conflict)
    return SVN_NO_ERROR;

  SVN_ERR(build_text_conflict_resolve_items(&work_items, NULL,
                                            db, local_abspath, conflict,
                                            svn_wc_conflict_choose_merged,
                                            NULL, FALSE, NULL,
                                            cancel_func, cancel_baton,
                                            scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__db_op_mark_resolved(db, local_abspath, TRUE, FALSE, FALSE,
                                      work_items, scratch_pool));

  return svn_error_trace(svn_wc__wq_run(db, local_abspath,
                                        cancel_func, cancel_baton,
                                        scratch_pool));
}

svn_error_t *
svn_wc__mark_resolved_prop_conflicts(svn_wc__db_t *db,
                                     const char *local_abspath,
                                     apr_pool_t *scratch_pool)
{
  svn_boolean_t ignored_result;
  svn_skel_t *conflicts;

  SVN_ERR(svn_wc__db_read_conflict(&conflicts, NULL, NULL,
                                   db, local_abspath,
                                   scratch_pool, scratch_pool));

  if (!conflicts)
    return SVN_NO_ERROR;

  return svn_error_trace(resolve_prop_conflict_on_node(
                           &ignored_result,
                           db, local_abspath, conflicts, "",
                           svn_wc_conflict_choose_merged,
                           NULL, NULL,
                           NULL, NULL,
                           scratch_pool));
}


/* Baton for conflict_status_walker */
struct conflict_status_walker_baton
{
  svn_wc__db_t *db;
  svn_boolean_t resolve_text;
  const char *resolve_prop;
  svn_boolean_t resolve_tree;
  svn_wc_conflict_choice_t conflict_choice;
  svn_wc_conflict_resolver_func2_t conflict_func;
  void *conflict_baton;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
  svn_boolean_t resolved_one;
  apr_hash_t *resolve_later;
};

/* Implements svn_wc_notify_func2_t to collect new conflicts caused by
   resolving a tree conflict. */
static void
tree_conflict_collector(void *baton,
                        const svn_wc_notify_t *notify,
                        apr_pool_t *pool)
{
  struct conflict_status_walker_baton *cswb = baton;

  if (cswb->notify_func)
    cswb->notify_func(cswb->notify_baton, notify, pool);

  if (cswb->resolve_later
      && (notify->action == svn_wc_notify_tree_conflict
          || notify->prop_state == svn_wc_notify_state_conflicted
          || notify->content_state == svn_wc_notify_state_conflicted))
    {
      if (!svn_hash_gets(cswb->resolve_later, notify->path))
        {
          const char *dup_path;

          dup_path = apr_pstrdup(apr_hash_pool_get(cswb->resolve_later),
                                 notify->path);

          svn_hash_sets(cswb->resolve_later, dup_path, dup_path);
        }
    }
}

/* Implements svn_wc_status4_t to walk all conflicts to resolve.
 */
static svn_error_t *
conflict_status_walker(void *baton,
                       const char *local_abspath,
                       const svn_wc_status3_t *status,
                       apr_pool_t *scratch_pool)
{
  struct conflict_status_walker_baton *cswb = baton;
  svn_wc__db_t *db = cswb->db;
  svn_wc_notify_action_t notify_action = svn_wc_notify_resolved;
  const apr_array_header_t *conflicts;
  apr_pool_t *iterpool;
  int i;
  svn_boolean_t resolved = FALSE;
  svn_skel_t *conflict;
  const svn_wc_conflict_description2_t *cd;

  if (!status->conflicted)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_wc__read_conflicts(&conflicts, &conflict,
                                 db, local_abspath,
                                 (cswb->conflict_func != NULL) /* tmp files */,
                                 FALSE /* only tree conflicts */,
                                 scratch_pool, iterpool));

  for (i = 0; i < conflicts->nelts; i++)
    {
      svn_boolean_t did_resolve;
      svn_wc_conflict_choice_t my_choice = cswb->conflict_choice;
      svn_wc_conflict_result_t *result = NULL;
      svn_skel_t *work_items;

      cd = APR_ARRAY_IDX(conflicts, i, const svn_wc_conflict_description2_t *);

      if ((cd->kind == svn_wc_conflict_kind_property
           && (!cswb->resolve_prop
               || (*cswb->resolve_prop != '\0'
                   && strcmp(cswb->resolve_prop, cd->property_name) != 0)))
          || (cd->kind == svn_wc_conflict_kind_text && !cswb->resolve_text)
          || (cd->kind == svn_wc_conflict_kind_tree && !cswb->resolve_tree))
        {
          continue; /* Easy out. Don't call resolver func and ignore result */
        }

      svn_pool_clear(iterpool);

      if (my_choice == svn_wc_conflict_choose_unspecified)
        {
          if (!cswb->conflict_func)
            return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                    _("No conflict-callback and no "
                                      "pre-defined conflict-choice provided"));

          SVN_ERR(cswb->conflict_func(&result, cd, cswb->conflict_baton,
                                      iterpool, iterpool));

          my_choice = result->choice;
        }


      if (my_choice == svn_wc_conflict_choose_postpone)
        continue;

      switch (cd->kind)
        {
          case svn_wc_conflict_kind_tree:
            SVN_ERR(resolve_tree_conflict_on_node(&did_resolve,
                                                  db,
                                                  local_abspath, conflict,
                                                  my_choice,
                                                  cswb->resolve_later,
                                                  tree_conflict_collector,
                                                  cswb,
                                                  cswb->cancel_func,
                                                  cswb->cancel_baton,
                                                  iterpool));

            if (did_resolve)
              {
                resolved = TRUE;
                notify_action = svn_wc_notify_resolved_tree;
              }
            break;

          case svn_wc_conflict_kind_text:
            SVN_ERR(build_text_conflict_resolve_items(
                                        &work_items,
                                        &resolved,
                                        db, local_abspath, conflict,
                                        my_choice,
                                        result ? result->merged_file
                                               : NULL,
                                        result ? result->save_merged
                                               : FALSE,
                                        NULL /* merge_options */,
                                        cswb->cancel_func,
                                        cswb->cancel_baton,
                                        iterpool, iterpool));

            SVN_ERR(svn_wc__db_op_mark_resolved(db, local_abspath,
                                                TRUE, FALSE, FALSE,
                                                work_items, iterpool));
            SVN_ERR(svn_wc__wq_run(db, local_abspath,
                                   cswb->cancel_func, cswb->cancel_baton,
                                   iterpool));
            if (resolved)
                notify_action = svn_wc_notify_resolved_text;
            break;

          case svn_wc_conflict_kind_property:
            SVN_ERR(resolve_prop_conflict_on_node(&did_resolve,
                                                  db,
                                                  local_abspath,
                                                  conflict,
                                                  cd->property_name,
                                                  my_choice,
                                                  result
                                                    ? result->merged_file
                                                    : NULL,
                                                  result
                                                    ? result->merged_value
                                                    : NULL,
                                                  cswb->cancel_func,
                                                  cswb->cancel_baton,
                                                  iterpool));

            if (did_resolve)
              {
                resolved = TRUE;
                notify_action = svn_wc_notify_resolved_prop;
              }
            break;

          default:
            /* We can't resolve other conflict types */
            break;
        }
    }

  /* Notify */
  if (cswb->notify_func && resolved)
    {
      svn_wc_notify_t *notify;

      /* If our caller asked for all conflicts to be resolved,
       * send a general 'resolved' notification. */
      if (cswb->resolve_text && cswb->resolve_tree &&
          (cswb->resolve_prop == NULL || cswb->resolve_prop[0] == '\0'))
        notify_action = svn_wc_notify_resolved;

      /* If we resolved a property conflict, but no specific property was
       * requested by the caller, send a general 'resolved' notification. */
      if (notify_action == svn_wc_notify_resolved_prop &&
          (cswb->resolve_prop == NULL || cswb->resolve_prop[0] == '\0'))
        notify_action = svn_wc_notify_resolved;

      notify = svn_wc_create_notify(local_abspath, notify_action, iterpool);

      /* Add the property name for property-specific notifications. */
      if (notify_action == svn_wc_notify_resolved_prop)
        {
          notify->prop_name = cd->property_name;
          SVN_ERR_ASSERT(strlen(notify->prop_name) > 0);
        }

      cswb->notify_func(cswb->notify_baton, notify, iterpool);

    }
  if (resolved)
    cswb->resolved_one = TRUE;

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__resolve_conflicts(svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          svn_depth_t depth,
                          svn_boolean_t resolve_text,
                          const char *resolve_prop,
                          svn_boolean_t resolve_tree,
                          svn_wc_conflict_choice_t conflict_choice,
                          svn_wc_conflict_resolver_func2_t conflict_func,
                          void *conflict_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          apr_pool_t *scratch_pool)
{
  struct conflict_status_walker_baton cswb;
  apr_pool_t *iterpool = NULL;
  svn_error_t *err;

  if (depth == svn_depth_unknown)
    depth = svn_depth_infinity;

  cswb.db = wc_ctx->db;
  cswb.resolve_text = resolve_text;
  cswb.resolve_prop = resolve_prop;
  cswb.resolve_tree = resolve_tree;
  cswb.conflict_choice = conflict_choice;

  cswb.conflict_func = conflict_func;
  cswb.conflict_baton = conflict_baton;

  cswb.cancel_func = cancel_func;
  cswb.cancel_baton = cancel_baton;

  cswb.notify_func = notify_func;
  cswb.notify_baton = notify_baton;

  cswb.resolved_one = FALSE;
  cswb.resolve_later = (depth != svn_depth_empty)
                          ? apr_hash_make(scratch_pool)
                          : NULL;

  if (notify_func)
    notify_func(notify_baton,
                svn_wc_create_notify(local_abspath,
                                    svn_wc_notify_conflict_resolver_starting,
                                    scratch_pool),
                scratch_pool);

  err = svn_wc_walk_status(wc_ctx,
                           local_abspath,
                           depth,
                           FALSE /* get_all */,
                           FALSE /* no_ignore */,
                           TRUE /* ignore_text_mods */,
                           NULL /* ignore_patterns */,
                           conflict_status_walker, &cswb,
                           cancel_func, cancel_baton,
                           scratch_pool);

  /* If we got new tree conflicts (or delayed conflicts) during the initial
     walk, we now walk them one by one as closure. */
  while (!err && cswb.resolve_later && apr_hash_count(cswb.resolve_later))
    {
      apr_hash_index_t *hi;
      svn_wc_status3_t *status = NULL;
      const char *tc_abspath = NULL;

      if (iterpool)
        svn_pool_clear(iterpool);
      else
        iterpool = svn_pool_create(scratch_pool);

      hi = apr_hash_first(scratch_pool, cswb.resolve_later);
      cswb.resolve_later = apr_hash_make(scratch_pool);
      cswb.resolved_one = FALSE;

      for (; hi && !err; hi = apr_hash_next(hi))
        {
          const char *relpath;
          svn_pool_clear(iterpool);

          tc_abspath = apr_hash_this_key(hi);

          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          relpath = svn_dirent_skip_ancestor(local_abspath,
                                             tc_abspath);

          if (!relpath
              || (depth >= svn_depth_empty
                  && depth < svn_depth_infinity
                  && strchr(relpath, '/')))
            {
              continue;
            }

          SVN_ERR(svn_wc_status3(&status, wc_ctx, tc_abspath,
                                 iterpool, iterpool));

          if (depth == svn_depth_files
              && status->kind == svn_node_dir)
            continue;

          err = svn_error_trace(conflict_status_walker(&cswb, tc_abspath,
                                                       status, scratch_pool));
        }

      /* None of the remaining conflicts got resolved, and non did provide
         an error...

         We can fix that if we disable the 'resolve_later' option...
       */
      if (!cswb.resolved_one && !err && tc_abspath
          && apr_hash_count(cswb.resolve_later))
        {
          /* Run the last resolve operation again. We still have status
             and tc_abspath for that one. */

          cswb.resolve_later = NULL; /* Produce proper error! */

          /* Recreate the error */
          err = svn_error_trace(conflict_status_walker(&cswb, tc_abspath,
                                                       status, scratch_pool));

          SVN_ERR_ASSERT(err != NULL);

          err = svn_error_createf(
                    SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, err,
                    _("Unable to resolve pending conflict on '%s'"),
                    svn_dirent_local_style(tc_abspath, scratch_pool));
          break;
        }
    }

  if (iterpool)
    svn_pool_destroy(iterpool);

  if (err && err->apr_err != SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE)
    err = svn_error_createf(
                SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, err,
                _("Unable to resolve conflicts on '%s'"),
                svn_dirent_local_style(local_abspath, scratch_pool));

  SVN_ERR(err);

  if (notify_func)
    notify_func(notify_baton,
                svn_wc_create_notify(local_abspath,
                                    svn_wc_notify_conflict_resolver_done,
                                    scratch_pool),
                scratch_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_resolved_conflict5(svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          svn_depth_t depth,
                          svn_boolean_t resolve_text,
                          const char *resolve_prop,
                          svn_boolean_t resolve_tree,
                          svn_wc_conflict_choice_t conflict_choice,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_wc__resolve_conflicts(wc_ctx, local_abspath,
                                                   depth, resolve_text,
                                                   resolve_prop, resolve_tree,
                                                   conflict_choice,
                                                   NULL, NULL,
                                                   cancel_func, cancel_baton,
                                                   notify_func, notify_baton,
                                                   scratch_pool));
}

/* Constructor for the result-structure returned by conflict callbacks. */
svn_wc_conflict_result_t *
svn_wc_create_conflict_result(svn_wc_conflict_choice_t choice,
                              const char *merged_file,
                              apr_pool_t *pool)
{
  svn_wc_conflict_result_t *result = apr_pcalloc(pool, sizeof(*result));
  result->choice = choice;
  result->merged_file = apr_pstrdup(pool, merged_file);
  result->save_merged = FALSE;
  result->merged_value = NULL;

  /* If we add more fields to svn_wc_conflict_result_t, add them here. */

  return result;
}

svn_error_t *
svn_wc__conflict_text_mark_resolved(svn_wc_context_t *wc_ctx,
                                    const char *local_abspath,
                                    svn_wc_conflict_choice_t choice,
                                    svn_cancel_func_t cancel_func,
                                    void *cancel_baton,
                                    svn_wc_notify_func2_t notify_func,
                                    void *notify_baton,
                                    apr_pool_t *scratch_pool)
{
  svn_skel_t *work_items;
  svn_skel_t *conflict;
  svn_boolean_t did_resolve;

  SVN_ERR(svn_wc__db_read_conflict(&conflict, NULL, NULL,
                                   wc_ctx->db, local_abspath,
                                   scratch_pool, scratch_pool));

  if (!conflict)
    return SVN_NO_ERROR;

  SVN_ERR(build_text_conflict_resolve_items(&work_items, &did_resolve,
                                            wc_ctx->db, local_abspath,
                                            conflict, choice,
                                            NULL, FALSE, NULL,
                                            cancel_func, cancel_baton,
                                            scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__db_op_mark_resolved(wc_ctx->db, local_abspath,
                                      TRUE, FALSE, FALSE,
                                      work_items, scratch_pool));

  SVN_ERR(svn_wc__wq_run(wc_ctx->db, local_abspath,
                         cancel_func, cancel_baton,
                         scratch_pool));

  if (did_resolve && notify_func)
    notify_func(notify_baton,
                svn_wc_create_notify(local_abspath,
                                     svn_wc_notify_resolved_text,
                                     scratch_pool),
                scratch_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_prop_mark_resolved(svn_wc_context_t *wc_ctx,
                                    const char *local_abspath,
                                    const char *propname,
                                    svn_wc_conflict_choice_t choice,
                                    const svn_string_t *merged_value,
                                    svn_wc_notify_func2_t notify_func,
                                    void *notify_baton,
                                    apr_pool_t *scratch_pool)
{
  svn_boolean_t did_resolve;
  svn_skel_t *conflicts;

  SVN_ERR(svn_wc__db_read_conflict(&conflicts, NULL, NULL,
                                   wc_ctx->db, local_abspath,
                                   scratch_pool, scratch_pool));

  if (!conflicts)
    return SVN_NO_ERROR;

  SVN_ERR(resolve_prop_conflict_on_node(&did_resolve, wc_ctx->db,
                                        local_abspath, conflicts,
                                        propname, choice, NULL, merged_value,
                                        NULL, NULL, scratch_pool));

  if (did_resolve && notify_func)
    {
      svn_wc_notify_t *notify;

      /* Send a general notification if no specific property was requested. */
      if (propname == NULL || propname[0] == '\0')
        {
          notify = svn_wc_create_notify(local_abspath,
                                        svn_wc_notify_resolved,
                                        scratch_pool);
        }
      else
        {
          notify = svn_wc_create_notify(local_abspath,
                                        svn_wc_notify_resolved_prop,
                                        scratch_pool);
          notify->prop_name = propname;
        }

      notify_func(notify_baton, notify, scratch_pool);
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_tree_update_break_moved_away(svn_wc_context_t *wc_ctx,
                                              const char *local_abspath,
                                              svn_cancel_func_t cancel_func,
                                              void *cancel_baton,
                                              svn_wc_notify_func2_t notify_func,
                                              void *notify_baton,
                                              apr_pool_t *scratch_pool)
{
  svn_wc_conflict_reason_t reason;
  svn_wc_conflict_action_t action;
  svn_wc_operation_t operation;
  svn_boolean_t tree_conflicted;
  const char *src_op_root_abspath;
  const apr_array_header_t *conflicts;
  svn_skel_t *conflict_skel;

  SVN_ERR(svn_wc__read_conflicts(&conflicts, &conflict_skel,
                                 wc_ctx->db, local_abspath,
                                 FALSE, /* no tempfiles */
                                 FALSE, /* only tree conflicts */
                                 scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__conflict_read_info(&operation, NULL, NULL, NULL,
                                     &tree_conflicted, wc_ctx->db,
                                     local_abspath, conflict_skel,
                                     scratch_pool, scratch_pool));
  if (!tree_conflicted)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__conflict_read_tree_conflict(&reason, &action,
                                              &src_op_root_abspath,
                                              wc_ctx->db, local_abspath,
                                              conflict_skel,
                                              scratch_pool, scratch_pool));

  /* Make sure the expected conflict is recorded. */
  if (operation != svn_wc_operation_update &&
      operation != svn_wc_operation_switch)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Unexpected conflict operation '%s' on '%s'"),
                             svn_token__to_word(operation_map, operation),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));
  if (reason != svn_wc_conflict_reason_deleted &&
      reason != svn_wc_conflict_reason_replaced &&
      reason != svn_wc_conflict_reason_moved_away)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Unexpected conflict reason '%s' on '%s'"),
                             svn_token__to_word(reason_map, reason),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  /* Break moves for any children moved out of this directory,
   * and leave this directory deleted. */
  if (action != svn_wc_conflict_action_delete)
    {
      SVN_ERR(svn_wc__db_op_break_moved_away(
                      wc_ctx->db, local_abspath, src_op_root_abspath, TRUE,
                      notify_func, notify_baton, scratch_pool));
      /* Conflict was marked resolved by db_op_break_moved_away() call .*/

      if (notify_func)
        notify_func(notify_baton,
                    svn_wc_create_notify(local_abspath,
                                         svn_wc_notify_resolved_tree,
                                         scratch_pool),
                    scratch_pool);
      return SVN_NO_ERROR;
    }
  /* else # The move is/moves are already broken */

  SVN_ERR(svn_wc__db_op_mark_resolved(wc_ctx->db, local_abspath,
                                      FALSE, FALSE, TRUE,
                                      NULL, scratch_pool));
  SVN_ERR(svn_wc__wq_run(wc_ctx->db, local_abspath, cancel_func, cancel_baton,
                         scratch_pool));

  if (notify_func)
    notify_func(notify_baton,
                svn_wc_create_notify(local_abspath,
                                     svn_wc_notify_resolved_tree,
                                     scratch_pool),
                scratch_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_tree_update_raise_moved_away(svn_wc_context_t *wc_ctx,
                                              const char *local_abspath,
                                              svn_cancel_func_t cancel_func,
                                              void *cancel_baton,
                                              svn_wc_notify_func2_t notify_func,
                                              void *notify_baton,
                                              apr_pool_t *scratch_pool)
{
  svn_wc_conflict_reason_t reason;
  svn_wc_conflict_action_t action;
  svn_wc_operation_t operation;
  svn_boolean_t tree_conflicted;
  const apr_array_header_t *conflicts;
  svn_skel_t *conflict_skel;

  SVN_ERR(svn_wc__read_conflicts(&conflicts, &conflict_skel,
                                 wc_ctx->db, local_abspath,
                                 FALSE, /* no tempfiles */
                                 FALSE, /* only tree conflicts */
                                 scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__conflict_read_info(&operation, NULL, NULL, NULL,
                                     &tree_conflicted, wc_ctx->db,
                                     local_abspath, conflict_skel,
                                     scratch_pool, scratch_pool));
  if (!tree_conflicted)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__conflict_read_tree_conflict(&reason, &action, NULL,
                                              wc_ctx->db, local_abspath,
                                              conflict_skel,
                                              scratch_pool, scratch_pool));

  /* Make sure the expected conflict is recorded. */
  if (operation != svn_wc_operation_update &&
      operation != svn_wc_operation_switch)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Unexpected conflict operation '%s' on '%s'"),
                             svn_token__to_word(operation_map, operation),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));
  if (reason != svn_wc_conflict_reason_deleted &&
      reason != svn_wc_conflict_reason_replaced)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Unexpected conflict reason '%s' on '%s'"),
                             svn_token__to_word(reason_map, reason),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));
  if (action != svn_wc_conflict_action_edit)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Unexpected conflict action '%s' on '%s'"),
                             svn_token__to_word(action_map, action),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  /* Raise local moved-away vs. incoming edit conflicts on any children
   * moved out of this directory, and leave this directory as-is.
   * The user may choose to update newly conflicted moved-away children
   * when resolving them. If this function raises an error, the conflict
   * cannot be resolved yet because other conflicts or obstructions
   * prevent us from propagating the conflict to moved-away children. */
  SVN_ERR(svn_wc__db_op_raise_moved_away(wc_ctx->db, local_abspath,
                                         notify_func, notify_baton,
                                         scratch_pool));

  /* The conflict was marked resolved by svn_wc__db_op_raise_moved_away(). */
  if (notify_func)
    notify_func(notify_baton,
                svn_wc_create_notify(local_abspath,
                                     svn_wc_notify_resolved_tree,
                                     scratch_pool),
                scratch_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_tree_update_moved_away_node(svn_wc_context_t *wc_ctx,
                                             const char *local_abspath,
                                             svn_cancel_func_t cancel_func,
                                             void *cancel_baton,
                                             svn_wc_notify_func2_t notify_func,
                                             void *notify_baton,
                                             apr_pool_t *scratch_pool)
{
  svn_wc_conflict_reason_t reason;
  svn_wc_conflict_action_t action;
  svn_wc_operation_t operation;
  svn_boolean_t tree_conflicted;
  const char *src_op_root_abspath;
  const apr_array_header_t *conflicts;
  svn_skel_t *conflict_skel;

  SVN_ERR(svn_wc__read_conflicts(&conflicts, &conflict_skel,
                                 wc_ctx->db, local_abspath,
                                 FALSE, /* no tempfiles */
                                 FALSE, /* only tree conflicts */
                                 scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__conflict_read_info(&operation, NULL, NULL, NULL,
                                     &tree_conflicted, wc_ctx->db,
                                     local_abspath, conflict_skel,
                                     scratch_pool, scratch_pool));
  if (!tree_conflicted)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__conflict_read_tree_conflict(&reason, &action,
                                              &src_op_root_abspath,
                                              wc_ctx->db, local_abspath,
                                              conflict_skel,
                                              scratch_pool, scratch_pool));

  /* Make sure the expected conflict is recorded. */
  if (operation != svn_wc_operation_update &&
      operation != svn_wc_operation_switch)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Unexpected conflict operation '%s' on '%s'"),
                             svn_token__to_word(operation_map, operation),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));
  if (reason != svn_wc_conflict_reason_moved_away)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Unexpected conflict reason '%s' on '%s'"),
                             svn_token__to_word(reason_map, reason),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));
  if (action != svn_wc_conflict_action_edit)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Unexpected conflict action '%s' on '%s'"),
                             svn_token__to_word(action_map, action),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  /* Update the moved-away conflict victim. */
  SVN_ERR(svn_wc__db_update_moved_away_conflict_victim(wc_ctx->db,
                                                       local_abspath,
                                                       src_op_root_abspath,
                                                       operation,
                                                       action,
                                                       reason,
                                                       cancel_func,
                                                       cancel_baton,
                                                       notify_func,
                                                       notify_baton,
                                                       scratch_pool));

  SVN_ERR(svn_wc__db_op_mark_resolved(wc_ctx->db, local_abspath,
                                      FALSE, FALSE, TRUE,
                                      NULL, scratch_pool));
  SVN_ERR(svn_wc__wq_run(wc_ctx->db, local_abspath, cancel_func, cancel_baton,
                         scratch_pool));

  if (notify_func)
    notify_func(notify_baton,
                svn_wc_create_notify(local_abspath,
                                     svn_wc_notify_resolved_tree,
                                     scratch_pool),
                scratch_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_tree_update_incoming_move(svn_wc_context_t *wc_ctx,
                                           const char *local_abspath,
                                           const char *dest_abspath,
                                           svn_cancel_func_t cancel_func,
                                           void *cancel_baton,
                                           svn_wc_notify_func2_t notify_func,
                                           void *notify_baton,
                                           apr_pool_t *scratch_pool)
{
  svn_wc_conflict_reason_t local_change;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_operation_t operation;
  svn_boolean_t tree_conflicted;
  const apr_array_header_t *conflicts;
  svn_skel_t *conflict_skel;

  SVN_ERR(svn_wc__read_conflicts(&conflicts, &conflict_skel,
                                 wc_ctx->db, local_abspath,
                                 FALSE, /* no tempfiles */
                                 FALSE, /* only tree conflicts */
                                 scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__conflict_read_info(&operation, NULL, NULL, NULL,
                                     &tree_conflicted, wc_ctx->db,
                                     local_abspath, conflict_skel,
                                     scratch_pool, scratch_pool));
  if (!tree_conflicted)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__conflict_read_tree_conflict(&local_change, &incoming_change,
                                              NULL, wc_ctx->db, local_abspath,
                                              conflict_skel,
                                              scratch_pool, scratch_pool));

  /* Make sure the expected conflict is recorded. */
  if (operation != svn_wc_operation_update &&
      operation != svn_wc_operation_switch &&
      operation != svn_wc_operation_merge)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Unexpected conflict operation '%s' on '%s'"),
                             svn_token__to_word(operation_map, operation),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));
  if (local_change != svn_wc_conflict_reason_edited)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Unexpected conflict reason '%s' on '%s'"),
                             svn_token__to_word(reason_map, local_change),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));
  if (incoming_change != svn_wc_conflict_action_delete)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Unexpected conflict action '%s' on '%s'"),
                             svn_token__to_word(action_map, incoming_change),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_wc__db_update_incoming_move(wc_ctx->db, local_abspath,
                                          dest_abspath, operation,
                                          incoming_change, local_change,
                                          cancel_func, cancel_baton,
                                          notify_func, notify_baton,
                                          scratch_pool));

  SVN_ERR(svn_wc__wq_run(wc_ctx->db, local_abspath, cancel_func, cancel_baton,
                         scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_tree_update_local_add(svn_wc_context_t *wc_ctx,
                                       const char *local_abspath,
                                       svn_cancel_func_t cancel_func,
                                       void *cancel_baton,
                                       svn_wc_notify_func2_t notify_func,
                                       void *notify_baton,
                                       apr_pool_t *scratch_pool)
{
  svn_wc_conflict_reason_t local_change;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_operation_t operation;
  svn_boolean_t tree_conflicted;
  const apr_array_header_t *conflicts;
  svn_skel_t *conflict_skel;

  SVN_ERR(svn_wc__read_conflicts(&conflicts, &conflict_skel,
                                 wc_ctx->db, local_abspath,
                                 FALSE, /* no tempfiles */
                                 FALSE, /* only tree conflicts */
                                 scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__conflict_read_info(&operation, NULL, NULL, NULL,
                                     &tree_conflicted, wc_ctx->db,
                                     local_abspath, conflict_skel,
                                     scratch_pool, scratch_pool));
  if (!tree_conflicted)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__conflict_read_tree_conflict(&local_change, &incoming_change,
                                              NULL, wc_ctx->db, local_abspath,
                                              conflict_skel,
                                              scratch_pool, scratch_pool));

  /* Make sure the expected conflict is recorded. */
  if (operation != svn_wc_operation_update)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Unexpected conflict operation '%s' on '%s'"),
                             svn_token__to_word(operation_map, operation),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));
  if (local_change != svn_wc_conflict_reason_added)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Unexpected conflict reason '%s' on '%s'"),
                             svn_token__to_word(reason_map, local_change),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));
  if (incoming_change != svn_wc_conflict_action_add)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Unexpected conflict action '%s' on '%s'"),
                             svn_token__to_word(action_map, incoming_change),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_wc__db_update_local_add(wc_ctx->db, local_abspath,
                                      cancel_func, cancel_baton,
                                      notify_func, notify_baton,
                                      scratch_pool));

  SVN_ERR(svn_wc__wq_run(wc_ctx->db, local_abspath, cancel_func, cancel_baton,
                         scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__guess_incoming_move_target_nodes(apr_array_header_t **possible_targets,
                                         svn_wc_context_t *wc_ctx,
                                         const char *victim_abspath,
                                         svn_node_kind_t victim_node_kind,
                                         const char *moved_to_repos_relpath,
                                         apr_pool_t *result_pool,
                                         apr_pool_t *scratch_pool)
{
  apr_array_header_t *candidates;
  apr_pool_t *iterpool;
  int i;
  apr_size_t longest_ancestor_len = 0;

  *possible_targets = apr_array_make(result_pool, 1, sizeof(const char *));
  SVN_ERR(svn_wc__find_repos_node_in_wc(&candidates, wc_ctx->db, victim_abspath,
                                        moved_to_repos_relpath,
                                        scratch_pool, scratch_pool));

  /* Find a "useful move target" node in our set of candidates.
   * Since there is no way to be certain, filter out nodes which seem
   * unlikely candidates, and return the first node which is "good enough".
   * Nodes which are tree conflict victims don't count, and nodes which
   * cannot be modified (e.g. replaced or deleted nodes) don't count.
   * Nodes which are of a different node kind don't count either.
   * Ignore switched nodes as well, since that is an unlikely case during
   * update/swtich/merge conflict resolution. And externals shouldn't even
   * be on our candidate list in the first place.
   * If multiple candidates match these criteria, choose the one which
   * shares the longest common ancestor with the victim. */
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < candidates->nelts; i++)
    {
      const char *local_abspath;
      const char *ancestor_abspath;
      apr_size_t ancestor_len;
      svn_boolean_t tree_conflicted;
      svn_wc__db_status_t status;
      svn_boolean_t is_wcroot;
      svn_boolean_t is_switched;
      svn_node_kind_t node_kind;
      const char *moved_to_abspath;
      int insert_index;

      svn_pool_clear(iterpool);

      local_abspath = APR_ARRAY_IDX(candidates, i, const char *);

      SVN_ERR(svn_wc__internal_conflicted_p(NULL, NULL, &tree_conflicted,
                                            wc_ctx->db, local_abspath,
                                            iterpool));
      if (tree_conflicted)
        continue;

      SVN_ERR(svn_wc__db_read_info(&status, &node_kind,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL,
                                   wc_ctx->db, local_abspath, iterpool,
                                   iterpool));
      if (status != svn_wc__db_status_normal &&
          status != svn_wc__db_status_added)
        continue;

      if (node_kind != victim_node_kind)
        continue;

      SVN_ERR(svn_wc__db_is_switched(&is_wcroot, &is_switched, NULL,
                                     wc_ctx->db, local_abspath, iterpool));
      if (is_wcroot || is_switched)
        continue;

      /* This might be a move target. Fingers crossed ;-) */
      moved_to_abspath = apr_pstrdup(result_pool, local_abspath);

      /* Insert the move target into the list. Targets which are closer
       * (path-wise) to the conflict victim are more likely to be a good
       * match, so put them at the front of the list. */
      ancestor_abspath = svn_dirent_get_longest_ancestor(local_abspath,
                                                         victim_abspath,
                                                         iterpool);
      ancestor_len = strlen(ancestor_abspath);
      if (ancestor_len >= longest_ancestor_len)
        {
          longest_ancestor_len = ancestor_len;
          insert_index = 0; /* prepend */
        }
      else
        {
          insert_index = (*possible_targets)->nelts; /* append */
        }
      svn_sort__array_insert(*possible_targets, &moved_to_abspath,
                             insert_index);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
