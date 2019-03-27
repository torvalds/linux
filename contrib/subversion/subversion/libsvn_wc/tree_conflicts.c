/*
 * tree_conflicts.c: Storage of tree conflict descriptions in the WC.
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

#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_types.h"
#include "svn_pools.h"

#include "tree_conflicts.h"
#include "conflicts.h"
#include "wc.h"

#include "private/svn_skel.h"
#include "private/svn_wc_private.h"
#include "private/svn_token.h"

#include "svn_private_config.h"

/* ### this should move to a more general location...  */
/* A map for svn_node_kind_t values. */
/* FIXME: this mapping defines a different representation of
          svn_node_unknown than the one defined in token-map.h */
static const svn_token_map_t node_kind_map[] =
{
  { "none", svn_node_none },
  { "file", svn_node_file },
  { "dir",  svn_node_dir },
  { "",     svn_node_unknown },
  /* ### should also map svn_node_symlink */
  { NULL }
};

/* A map for svn_wc_operation_t values. */
const svn_token_map_t svn_wc__operation_map[] =
{
  { "none",   svn_wc_operation_none },
  { "update", svn_wc_operation_update },
  { "switch", svn_wc_operation_switch },
  { "merge",  svn_wc_operation_merge },
  { NULL }
};

/* A map for svn_wc_conflict_action_t values. */
const svn_token_map_t svn_wc__conflict_action_map[] =
{
  { "edited",   svn_wc_conflict_action_edit },
  { "deleted",  svn_wc_conflict_action_delete },
  { "added",    svn_wc_conflict_action_add },
  { "replaced", svn_wc_conflict_action_replace },
  { NULL }
};

/* A map for svn_wc_conflict_reason_t values. */
const svn_token_map_t svn_wc__conflict_reason_map[] =
{
  { "edited",      svn_wc_conflict_reason_edited },
  { "deleted",     svn_wc_conflict_reason_deleted },
  { "missing",     svn_wc_conflict_reason_missing },
  { "obstructed",  svn_wc_conflict_reason_obstructed },
  { "added",       svn_wc_conflict_reason_added },
  { "replaced",    svn_wc_conflict_reason_replaced },
  { "unversioned", svn_wc_conflict_reason_unversioned },
  { "moved-away", svn_wc_conflict_reason_moved_away },
  { "moved-here", svn_wc_conflict_reason_moved_here },
  { NULL }
};


/* */
static svn_boolean_t
is_valid_version_info_skel(const svn_skel_t *skel)
{
  return (svn_skel__list_length(skel) == 5
          && svn_skel__matches_atom(skel->children, "version")
          && skel->children->next->is_atom
          && skel->children->next->next->is_atom
          && skel->children->next->next->next->is_atom
          && skel->children->next->next->next->next->is_atom);
}


/* */
static svn_boolean_t
is_valid_conflict_skel(const svn_skel_t *skel)
{
  int i;

  if (svn_skel__list_length(skel) != 8
      || !svn_skel__matches_atom(skel->children, "conflict"))
    return FALSE;

  /* 5 atoms ... */
  skel = skel->children->next;
  for (i = 5; i--; skel = skel->next)
    if (!skel->is_atom)
      return FALSE;

  /* ... and 2 version info skels. */
  return (is_valid_version_info_skel(skel)
          && is_valid_version_info_skel(skel->next));
}


/* Parse the enumeration value in VALUE into a plain
 * 'int', using MAP to convert from strings to enumeration values.
 * In MAP, a null .str field marks the end of the map.
 */
static svn_error_t *
read_enum_field(int *result,
                const svn_token_map_t *map,
                const svn_skel_t *skel)
{
  int value = svn_token__from_mem(map, skel->data, skel->len);

  if (value == SVN_TOKEN_UNKNOWN)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Unknown enumeration value in tree conflict "
                              "description"));

  *result = value;

  return SVN_NO_ERROR;
}


/* Parse the conflict info fields from SKEL into *VERSION_INFO. */
static svn_error_t *
read_node_version_info(const svn_wc_conflict_version_t **version_info,
                       const svn_skel_t *skel,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  int n;
  const char *repos_root;
  const char *repos_relpath;
  svn_revnum_t peg_rev;
  svn_node_kind_t kind;

  if (!is_valid_version_info_skel(skel))
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Invalid version info in tree conflict "
                              "description"));

  repos_root = apr_pstrmemdup(scratch_pool,
                              skel->children->next->data,
                              skel->children->next->len);
  if (*repos_root == '\0')
    {
      *version_info = NULL;
      return SVN_NO_ERROR;
    }

  /* Apply the Subversion 1.7+ url canonicalization rules to a pre 1.7 url */
  repos_root = svn_uri_canonicalize(repos_root, result_pool);

  peg_rev = SVN_STR_TO_REV(apr_pstrmemdup(scratch_pool,
                                          skel->children->next->next->data,
                                          skel->children->next->next->len));

  repos_relpath = apr_pstrmemdup(result_pool,
                                 skel->children->next->next->next->data,
                                 skel->children->next->next->next->len);

  SVN_ERR(read_enum_field(&n, node_kind_map,
                          skel->children->next->next->next->next));
  kind = (svn_node_kind_t)n;

  *version_info = svn_wc_conflict_version_create2(repos_root,
                                                  NULL,
                                                  repos_relpath,
                                                  peg_rev,
                                                  kind,
                                                  result_pool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__deserialize_conflict(const svn_wc_conflict_description2_t **conflict,
                             const svn_skel_t *skel,
                             const char *dir_path,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  const char *victim_basename;
  const char *victim_abspath;
  svn_node_kind_t node_kind;
  svn_wc_operation_t operation;
  svn_wc_conflict_action_t action;
  svn_wc_conflict_reason_t reason;
  const svn_wc_conflict_version_t *src_left_version;
  const svn_wc_conflict_version_t *src_right_version;
  int n;
  svn_wc_conflict_description2_t *new_conflict;

  if (!is_valid_conflict_skel(skel))
    return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                             _("Invalid conflict info '%s' in tree conflict "
                               "description"),
                             skel ? svn_skel__unparse(skel, scratch_pool)->data
                                  : "(null)");

  /* victim basename */
  victim_basename = apr_pstrmemdup(scratch_pool,
                                   skel->children->next->data,
                                   skel->children->next->len);
  if (victim_basename[0] == '\0')
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Empty 'victim' field in tree conflict "
                              "description"));

  /* node_kind */
  SVN_ERR(read_enum_field(&n, node_kind_map, skel->children->next->next));
  node_kind = (svn_node_kind_t)n;
  if (node_kind != svn_node_file && node_kind != svn_node_dir)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
             _("Invalid 'node_kind' field in tree conflict description"));

  /* operation */
  SVN_ERR(read_enum_field(&n, svn_wc__operation_map,
                          skel->children->next->next->next));
  operation = (svn_wc_operation_t)n;

  SVN_ERR(svn_dirent_get_absolute(&victim_abspath,
                    svn_dirent_join(dir_path, victim_basename, scratch_pool),
                    scratch_pool));

  /* action */
  SVN_ERR(read_enum_field(&n, svn_wc__conflict_action_map,
                          skel->children->next->next->next->next));
  action = n;

  /* reason */
  SVN_ERR(read_enum_field(&n, svn_wc__conflict_reason_map,
                          skel->children->next->next->next->next->next));
  reason = n;

  /* Let's just make it a bit easier on ourself here... */
  skel = skel->children->next->next->next->next->next->next;

  /* src_left_version */
  SVN_ERR(read_node_version_info(&src_left_version, skel,
                                 result_pool, scratch_pool));

  /* src_right_version */
  SVN_ERR(read_node_version_info(&src_right_version, skel->next,
                                 result_pool, scratch_pool));

  new_conflict = svn_wc_conflict_description_create_tree2(victim_abspath,
    node_kind, operation, src_left_version, src_right_version,
    result_pool);
  new_conflict->action = action;
  new_conflict->reason = reason;

  *conflict = new_conflict;

  return SVN_NO_ERROR;
}


/* Prepend to SKEL the string corresponding to enumeration value N, as found
 * in MAP. */
static void
skel_prepend_enum(svn_skel_t *skel,
                  const svn_token_map_t *map,
                  int n,
                  apr_pool_t *result_pool)
{
  svn_skel__prepend(svn_skel__str_atom(svn_token__to_word(map, n),
                                       result_pool), skel);
}


/* Prepend to PARENT_SKEL the several fields that represent VERSION_INFO, */
static svn_error_t *
prepend_version_info_skel(svn_skel_t *parent_skel,
                          const svn_wc_conflict_version_t *version_info,
                          apr_pool_t *pool)
{
  svn_skel_t *skel = svn_skel__make_empty_list(pool);

  /* node_kind */
  skel_prepend_enum(skel, node_kind_map, version_info->node_kind, pool);

  /* path_in_repos */
  svn_skel__prepend(svn_skel__str_atom(version_info->path_in_repos
                                       ? version_info->path_in_repos
                                       : "", pool), skel);

  /* peg_rev */
  svn_skel__prepend(svn_skel__str_atom(apr_psprintf(pool, "%ld",
                                                    version_info->peg_rev),
                                       pool), skel);

  /* repos_url */
  svn_skel__prepend(svn_skel__str_atom(version_info->repos_url
                                       ? version_info->repos_url
                                       : "", pool), skel);

  svn_skel__prepend(svn_skel__str_atom("version", pool), skel);

  SVN_ERR_ASSERT(is_valid_version_info_skel(skel));

  svn_skel__prepend(skel, parent_skel);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__serialize_conflict(svn_skel_t **skel,
                           const svn_wc_conflict_description2_t *conflict,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  /* A conflict version struct with all fields null/invalid. */
  static const svn_wc_conflict_version_t null_version = {
    NULL, SVN_INVALID_REVNUM, NULL, svn_node_unknown };
  svn_skel_t *c_skel = svn_skel__make_empty_list(result_pool);
  const char *victim_basename;

  /* src_right_version */
  if (conflict->src_right_version)
    SVN_ERR(prepend_version_info_skel(c_skel, conflict->src_right_version,
                                      result_pool));
  else
    SVN_ERR(prepend_version_info_skel(c_skel, &null_version, result_pool));

  /* src_left_version */
  if (conflict->src_left_version)
    SVN_ERR(prepend_version_info_skel(c_skel, conflict->src_left_version,
                                      result_pool));
  else
    SVN_ERR(prepend_version_info_skel(c_skel, &null_version, result_pool));

  /* local change */
  skel_prepend_enum(c_skel, svn_wc__conflict_reason_map,
                    conflict->reason, result_pool);

  /* incoming change */
  skel_prepend_enum(c_skel, svn_wc__conflict_action_map,
                    conflict->action, result_pool);

  /* operation */
  skel_prepend_enum(c_skel, svn_wc__operation_map, conflict->operation,
                    result_pool);

  /* node_kind */
  SVN_ERR_ASSERT(conflict->node_kind == svn_node_dir
                 || conflict->node_kind == svn_node_file
                 || conflict->node_kind == svn_node_none);
  skel_prepend_enum(c_skel, node_kind_map, conflict->node_kind,
                    result_pool);

  /* Victim path (escaping separator chars). */
  victim_basename = svn_dirent_basename(conflict->local_abspath, result_pool);
  SVN_ERR_ASSERT(victim_basename[0]);
  svn_skel__prepend(svn_skel__str_atom(victim_basename, result_pool), c_skel);

  svn_skel__prepend(svn_skel__str_atom("conflict", result_pool), c_skel);

  SVN_ERR_ASSERT(is_valid_conflict_skel(c_skel));

  *skel = c_skel;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__del_tree_conflict(svn_wc_context_t *wc_ctx,
                          const char *victim_abspath,
                          apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(victim_abspath));

  SVN_ERR(svn_wc__db_op_mark_resolved(wc_ctx->db, victim_abspath,
                                      FALSE, FALSE, TRUE, NULL,
                                      scratch_pool));

   return SVN_NO_ERROR;
 }

svn_error_t *
svn_wc__add_tree_conflict(svn_wc_context_t *wc_ctx,
                          const svn_wc_conflict_description2_t *conflict,
                          apr_pool_t *scratch_pool)
{
  svn_boolean_t existing_conflict;
  svn_skel_t *conflict_skel;
  svn_error_t *err;

  SVN_ERR_ASSERT(conflict != NULL);
  SVN_ERR_ASSERT(conflict->operation == svn_wc_operation_merge ||
                 (conflict->reason != svn_wc_conflict_reason_moved_away &&
                  conflict->reason != svn_wc_conflict_reason_moved_here));

  /* Re-adding an existing tree conflict victim is an error. */
  err = svn_wc__internal_conflicted_p(NULL, NULL, &existing_conflict,
                                      wc_ctx->db, conflict->local_abspath,
                                      scratch_pool);
  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_trace(err);

      svn_error_clear(err);
    }
  else if (existing_conflict)
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                             _("Attempt to add tree conflict that already "
                               "exists at '%s'"),
                             svn_dirent_local_style(conflict->local_abspath,
                                                    scratch_pool));
  else if (!conflict)
    return SVN_NO_ERROR;

  conflict_skel = svn_wc__conflict_skel_create(scratch_pool);

  SVN_ERR(svn_wc__conflict_skel_add_tree_conflict(conflict_skel, wc_ctx->db,
                                                  conflict->local_abspath,
                                                  conflict->reason,
                                                  conflict->action,
                                                  NULL,
                                                  scratch_pool, scratch_pool));

  switch (conflict->operation)
    {
      case svn_wc_operation_update:
      default:
        SVN_ERR(svn_wc__conflict_skel_set_op_update(conflict_skel,
                                                    conflict->src_left_version,
                                                    conflict->src_right_version,
                                                    scratch_pool, scratch_pool));
        break;
      case svn_wc_operation_switch:
        SVN_ERR(svn_wc__conflict_skel_set_op_switch(conflict_skel,
                                                    conflict->src_left_version,
                                                    conflict->src_right_version,
                                                    scratch_pool, scratch_pool));
        break;
      case svn_wc_operation_merge:
        SVN_ERR(svn_wc__conflict_skel_set_op_merge(conflict_skel,
                                                   conflict->src_left_version,
                                                   conflict->src_right_version,
                                                   scratch_pool, scratch_pool));
        break;
    }

  return svn_error_trace(
                svn_wc__db_op_mark_conflict(wc_ctx->db, conflict->local_abspath,
                                            conflict_skel, NULL, scratch_pool));
}


svn_error_t *
svn_wc__get_tree_conflict(const svn_wc_conflict_description2_t **tree_conflict,
                          svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  const apr_array_header_t *conflicts;
  int i;
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__read_conflicts(&conflicts, NULL,
                                 wc_ctx->db, local_abspath,
                                 FALSE /* temp files */,
                                 TRUE /* only tree conflicts */,
                                 scratch_pool, scratch_pool));

  if (!conflicts || conflicts->nelts == 0)
    {
      *tree_conflict = NULL;
      return SVN_NO_ERROR;
    }

  for (i = 0; i < conflicts->nelts; i++)
    {
      const svn_wc_conflict_description2_t *desc;

      desc = APR_ARRAY_IDX(conflicts, i, svn_wc_conflict_description2_t *);

      if (desc->kind == svn_wc_conflict_kind_tree)
        {
          *tree_conflict = svn_wc_conflict_description2_dup(desc, result_pool);
          return SVN_NO_ERROR;
        }
    }

  *tree_conflict = NULL;
  return SVN_NO_ERROR;
}

