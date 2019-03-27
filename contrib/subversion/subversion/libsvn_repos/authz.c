/* authz.c : path-based access control
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


/*** Includes. ***/

#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_fnmatch.h>

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_repos.h"
#include "svn_config.h"
#include "svn_ctype.h"
#include "private/svn_atomic.h"
#include "private/svn_fspath.h"
#include "private/svn_repos_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_subr_private.h"
#include "repos.h"
#include "authz.h"
#include "config_file.h"


/*** Access rights. ***/

/* This structure describes the access rights given to a specific user by
 * a path rule (actually the rule set specified for a path).  I.e. there is
 * one instance of this per path rule.
 */
typedef struct path_access_t
{
  /* Sequence number of the path rule that this struct was derived from.
   * If multiple rules apply to the same path (only possible with wildcard
   * matching), the one with the highest SEQUENCE_NUMBER wins, i.e. the latest
   * one defined in the authz file.
   *
   * A value of 0 denotes the default rule at the repository root denying
   * access to everybody.  User-defined path rules start with ID 1.
   */
  int sequence_number;

  /* Access rights of the respective user as defined by the rule set. */
  authz_access_t rights;
} path_access_t;

/* Use this to indicate that no sequence ID has been assigned.
 * It will automatically be inferior to (less than) any other sequence ID. */
#define NO_SEQUENCE_NUMBER (-1)

/* Convenience structure combining the node-local access rights with the
 * min and max rights granted within the sub-tree. */
typedef struct limited_rights_t
{
  /* Access granted to the current user.  If the SEQUENCE_NUMBER member is
   * NO_SEQUENCE_NUMBER, there has been no specific path rule for this PATH
   * but only for some sub-path(s).  There is always a rule at the root node.
   */
  path_access_t access;

  /* Minimal access rights that the user has on this or any other node in 
   * the sub-tree.  This does not take inherited rights into account. */
  authz_access_t min_rights;

  /* Maximal access rights that the user has on this or any other node in 
   * the sub-tree.  This does not take inherited rights into account. */
  authz_access_t max_rights;

} limited_rights_t;

/* Return TRUE, if RIGHTS has local rights defined in the ACCESS member. */
static svn_boolean_t
has_local_rule(const limited_rights_t *rights)
{
  return rights->access.sequence_number != NO_SEQUENCE_NUMBER;
}

/* Aggregate the ACCESS spec of TARGET and RIGHTS into TARGET.  I.e. if both
 * are specified, pick one in accordance to the precedence rules. */
static void
combine_access(limited_rights_t *target,
               const limited_rights_t *rights)
{
  /* This implies the check for NO_SEQUENCE_NUMBER, i.e no rights being
   * specified. */
  if (target->access.sequence_number < rights->access.sequence_number)
    target->access = rights->access;
}

/* Aggregate the min / max access rights of TARGET and RIGHTS into TARGET. */
static void
combine_right_limits(limited_rights_t *target,
                     const limited_rights_t *rights)
{
  target->max_rights |= rights->max_rights;
  target->min_rights &= rights->min_rights;
}



/*** Authz cache access. ***/

/* All authz instances currently in use as well as all filtered authz
 * instances in use will be cached here.
 * Both caches will be instantiated at most once. */
static svn_object_pool__t *authz_pool = NULL;
static svn_object_pool__t *filtered_pool = NULL;
static svn_atomic_t authz_pool_initialized = FALSE;

/* Implements svn_atomic__err_init_func_t. */
static svn_error_t *
synchronized_authz_initialize(void *baton, apr_pool_t *pool)
{
#if APR_HAS_THREADS
  svn_boolean_t multi_threaded = TRUE;
#else
  svn_boolean_t multi_threaded = FALSE;
#endif

  SVN_ERR(svn_object_pool__create(&authz_pool, multi_threaded, pool));
  SVN_ERR(svn_object_pool__create(&filtered_pool, multi_threaded, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_authz_initialize(apr_pool_t *pool)
{
  /* Protect against multiple calls. */
  return svn_error_trace(svn_atomic__init_once(&authz_pool_initialized,
                                               synchronized_authz_initialize,
                                               NULL, pool));
}

/* Return a combination of AUTHZ_KEY and GROUPS_KEY, allocated in RESULT_POOL.
 * GROUPS_KEY may be NULL.  This is the key for the AUTHZ_POOL.
 */
static svn_membuf_t *
construct_authz_key(const svn_checksum_t *authz_key,
                    const svn_checksum_t *groups_key,
                    apr_pool_t *result_pool)
{
  svn_membuf_t *result = apr_pcalloc(result_pool, sizeof(*result));
  if (groups_key)
    {
      apr_size_t authz_size = svn_checksum_size(authz_key);
      apr_size_t groups_size = svn_checksum_size(groups_key);

      svn_membuf__create(result, authz_size + groups_size, result_pool);
      result->size = authz_size + groups_size; /* exact length is required! */

      memcpy(result->data, authz_key->digest, authz_size);
      memcpy((char *)result->data + authz_size,
             groups_key->digest, groups_size);
    }
  else
    {
      apr_size_t size = svn_checksum_size(authz_key);
      svn_membuf__create(result, size, result_pool);
      result->size = size; /* exact length is required! */
      memcpy(result->data, authz_key->digest, size);
    }

  return result;
}

/* Return a combination of REPOS_NAME, USER and AUTHZ_ID, allocated in
 * RESULT_POOL.  USER may be NULL.  This is the key for the FILTERED_POOL.
 */
static svn_membuf_t *
construct_filtered_key(const char *repos_name,
                       const char *user,
                       const svn_membuf_t *authz_id,
                       apr_pool_t *result_pool)
{
  svn_membuf_t *result = apr_pcalloc(result_pool, sizeof(*result));
  size_t repos_len = strlen(repos_name);
  size_t user_len = user ? strlen(user) : 1;
  const char *nullable_user = user ? user : "\0";
  size_t size = authz_id->size + repos_len + 1 + user_len + 1;

  svn_membuf__create(result, size, result_pool);
  result->size = size;

  memcpy(result->data, repos_name, repos_len + 1);
  size = repos_len + 1;
  memcpy((char *)result->data + size, nullable_user, user_len + 1);
  size += user_len + 1;
  memcpy((char *)result->data + size, authz_id->data, authz_id->size);

  return result;
}


/*** Constructing the prefix tree. ***/

/* Since prefix arrays may have more than one hit, we need to link them
 * for fast lookup. */
typedef struct sorted_pattern_t
{
  /* The filtered tree node carrying the prefix. */
  struct node_t *node;

  /* Entry that is a prefix to this one or NULL. */
  struct sorted_pattern_t *next;
} sorted_pattern_t;

/* Substructure of node_t.  It contains all sub-node that use patterns
 * in the next segment level. We keep it separate to save a bit of memory
 * and to be able to check for pattern presence in a single operation.
 */
typedef struct node_pattern_t
{
  /* If not NULL, this represents the "*" follow-segment. */
  struct node_t *any;

  /* If not NULL, this represents the "**" follow-segment. */
  struct node_t *any_var;

  /* If not NULL, the segments of all sorted_pattern_t in this array are the
   * prefix part of "prefix*" patterns.  Sorted by segment prefix. */
  apr_array_header_t *prefixes;

  /* If not NULL, the segments of all sorted_pattern_t in this array are the
   * reversed suffix part of "*suffix" patterns.  Sorted by reversed
   * segment suffix. */
  apr_array_header_t *suffixes;

  /* If not NULL, the segments of all sorted_pattern_t in this array contain
   * wildcards and don't fit into any of the above categories.
   * The NEXT members of the elements will not be used. */
  apr_array_header_t *complex;

  /* This node itself is a "**" segment and must therefore itself be added
   * to the matching node list for the next level. */
  svn_boolean_t repeat;
} node_pattern_t;

/* The pattern tree.  All relevant path rules are being folded into this
 * prefix tree, with a single, whole segment stored at each node.  The whole
 * tree applies to a single user only.
 */
typedef struct node_t
{
  /* The segment as specified in the path rule.  During the lookup tree walk,
   * this will compared to the respective segment of the path to check. */
  svn_string_t segment;

  /* Immediate access rights granted by rules on this node and the min /
   * max rights on any path in this sub-tree. */
  limited_rights_t rights;

  /* Map of sub-segment(const char *) to respective node (node_t) for all
   * sub-segments that have rules on themselves or their respective subtrees.
   * NULL, if there are no rules for sub-paths relevant to the user. */
  apr_hash_t *sub_nodes;

  /* If not NULL, this contains the pattern-based segment sub-nodes. */
  node_pattern_t *pattern_sub_nodes;
} node_t;

/* Create a new tree node for SEGMENT.
   Note: SEGMENT->pattern is always interned and therefore does not
   have to be copied into the result pool. */
static node_t *
create_node(authz_rule_segment_t *segment,
            apr_pool_t *result_pool)
{
  node_t *result = apr_pcalloc(result_pool, sizeof(*result));
  if (segment)
    result->segment = segment->pattern;
  else
    {
      result->segment.data = "";
      result->segment.len = 0;
    }
  result->rights.access.sequence_number = NO_SEQUENCE_NUMBER;
  return result;
}

/* Auto-create a node in *NODE, make it apply to SEGMENT and return it. */
static node_t *
ensure_node(node_t **node,
            authz_rule_segment_t *segment,
            apr_pool_t *result_pool)
{
  if (!*node)
    *node = create_node(segment, result_pool);

  return *node;
}

/* compare_func comparing segment names. It takes a sorted_pattern_t* as
 * VOID_LHS and a const authz_rule_segment_t * as VOID_RHS.
 */
static int
compare_node_rule_segment(const void *void_lhs,
                          const void *void_rhs)
{
  const sorted_pattern_t *element = void_lhs;
  const authz_rule_segment_t *segment = void_rhs;

  return strcmp(element->node->segment.data, segment->pattern.data);
}

/* compare_func comparing segment names. It takes a sorted_pattern_t* as
 * VOID_LHS and a const char * as VOID_RHS.
 */
static int
compare_node_path_segment(const void *void_lhs,
                          const void *void_rhs)
{
  const sorted_pattern_t *element = void_lhs;
  const char *segment = void_rhs;

  return strcmp(element->node->segment.data, segment);
}

/* Make sure a node_t* for SEGMENT exists in *ARRAY and return it.
 * Auto-create either if they don't exist.  Entries in *ARRAY are
 * sorted by their segment strings.
 */
static node_t *
ensure_node_in_array(apr_array_header_t **array,
                     authz_rule_segment_t *segment,
                     apr_pool_t *result_pool)
{
  int idx;
  sorted_pattern_t entry;
  sorted_pattern_t *entry_ptr;

  /* Auto-create the array. */
  if (!*array)
    *array = apr_array_make(result_pool, 4, sizeof(sorted_pattern_t));

  /* Find the node in ARRAY and the IDX at which it were to be inserted.
   * Initialize IDX such that we won't attempt a hinted lookup (likely
   * to fail and therefore pure overhead). */
  idx = (*array)->nelts;
  entry_ptr = svn_sort__array_lookup(*array, segment, &idx,
                                     compare_node_rule_segment);
  if (entry_ptr)
    return entry_ptr->node;

  /* There is no such node, yet.
   * Create one and insert it into the sorted array. */
  entry.node = create_node(segment, result_pool);
  entry.next = NULL;
  svn_sort__array_insert(*array, &entry, idx);

  return entry.node;
}

/* Auto-create the PATTERN_SUB_NODES sub-structure in *NODE and return it. */
static node_pattern_t *
ensure_pattern_sub_nodes(node_t *node,
                         apr_pool_t *result_pool)
{
  if (node->pattern_sub_nodes == NULL)
    node->pattern_sub_nodes = apr_pcalloc(result_pool,
                                          sizeof(*node->pattern_sub_nodes));

  return node->pattern_sub_nodes;
}

/* Combine an ACL rule segment with the corresponding node in our filtered
 * data model. */
typedef struct node_segment_pair_t
{
  authz_rule_segment_t *segment;
  node_t *node;
} node_segment_pair_t;

/* Context object to be used with process_acl. It allows us to re-use
 * information from previous insertions. */
typedef struct construction_context_t
{
  /* Array of node_segment_pair_t.  It contains all segments already
   * processed of the current insertion together with the respective
   * nodes in our filtered tree.  Before the next lookup, the tree
   * walk for the common prefix can be skipped. */
  apr_array_header_t *path;
} construction_context_t;

/* Return a new context object allocated in RESULT_POOL. */
static construction_context_t *
create_construction_context(apr_pool_t *result_pool)
{
  construction_context_t *result = apr_pcalloc(result_pool, sizeof(*result));

  /* Array will be auto-extended but this initial size will make it rarely
   * ever necessary. */
  result->path = apr_array_make(result_pool, 32, sizeof(node_segment_pair_t));

  return result;
}

/* Constructor utility:  Below NODE, recursively insert sub-nodes for the
 * path given as *SEGMENTS of length SEGMENT_COUNT. If matching nodes
 * already exist, use those instead of creating new ones.  Set the leave
 * node's access rights spec to PATH_ACCESS.  Update the context info in CTX.
 */
static void
insert_path(construction_context_t *ctx,
            node_t *node,
            path_access_t *path_access,
            int segment_count,
            authz_rule_segment_t *segment,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  node_t *sub_node;
  node_segment_pair_t *node_segment;

  /* End of path? */
  if (segment_count == 0)
    {
      /* Set access rights.  Note that there might be multiple rules for
       * the same path due to non-repo-specific rules vs. repo-specific
       * ones.  Whichever gets defined last wins.
       */
      limited_rights_t rights;
      rights.access = *path_access;
      rights.max_rights = path_access->rights;
      rights.min_rights = path_access->rights;
      combine_access(&node->rights, &rights);
      return;
    }

  /* Any wildcards?  They will go into a separate sub-structure. */
  if (segment->kind != authz_rule_literal)
    ensure_pattern_sub_nodes(node, result_pool);

  switch (segment->kind)
    {
      /* A full wildcard segment? */
    case authz_rule_any_segment:
      sub_node = ensure_node(&node->pattern_sub_nodes->any,
                             segment, result_pool);
      break;

      /* One or more full wildcard segments? */
    case authz_rule_any_recursive:
      sub_node = ensure_node(&node->pattern_sub_nodes->any_var,
                             segment, result_pool);
      ensure_pattern_sub_nodes(sub_node, result_pool)->repeat = TRUE;
      break;

      /* A single wildcard at the end of the segment? */
    case authz_rule_prefix:
      sub_node = ensure_node_in_array(&node->pattern_sub_nodes->prefixes,
                                      segment, result_pool);
      break;

      /* A single wildcard at the start of segments? */
    case authz_rule_suffix:
      sub_node = ensure_node_in_array(&node->pattern_sub_nodes->suffixes,
                                      segment, result_pool);
      break;

      /* General pattern? */
    case authz_rule_fnmatch:
      sub_node = ensure_node_in_array(&node->pattern_sub_nodes->complex,
                                      segment, result_pool);
      break;

      /* Then it must be a literal. */
    default:
      SVN_ERR_ASSERT_NO_RETURN(segment->kind == authz_rule_literal);

      if (!node->sub_nodes)
        {
          node->sub_nodes = svn_hash__make(result_pool);
          sub_node = NULL;
        }
      else
        {
          sub_node = svn_hash_gets(node->sub_nodes, segment->pattern.data);
        }

      /* Auto-insert a sub-node for the current segment. */
      if (!sub_node)
        {
          sub_node = create_node(segment, result_pool);
          apr_hash_set(node->sub_nodes,
                       sub_node->segment.data,
                       sub_node->segment.len,
                       sub_node);
        }
    }

  /* Update context. */
  node_segment = apr_array_push(ctx->path);
  node_segment->segment = segment;
  node_segment->node = sub_node;

  /* Continue at the sub-node with the next segment. */
  insert_path(ctx, sub_node, path_access, segment_count - 1, segment + 1,
              result_pool, scratch_pool);
}


/* If the ACL is relevant to the REPOSITORY and user (given as MEMBERSHIPS
 * plus ANONYMOUS flag), insert the respective nodes into tree starting
 * at ROOT.  Use the context info of the previous call in CTX to eliminate
 * repeated lookups.  Allocate new nodes in RESULT_POOL and use SCRATCH_POOL
 * for temporary allocations.
 */
static void
process_acl(construction_context_t *ctx,
            const authz_acl_t *acl,
            node_t *root,
            const char *repository,
            const char *user,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  path_access_t path_access;
  int i;
  node_t *node;

  /* Skip ACLs that don't say anything about the current user
     and/or repository. */
  if (!svn_authz__get_acl_access(&path_access.rights, acl, user, repository))
    return;

  /* Insert the rule into the filtered tree. */
  path_access.sequence_number = acl->sequence_number;

  /* Try to reuse results from previous runs.
   * Basically, skip the common prefix. */
  node = root;
  for (i = 0; i < ctx->path->nelts; ++i)
    {
      const node_segment_pair_t *step
        = &APR_ARRAY_IDX(ctx->path, i, const node_segment_pair_t);

      /* Exploit the fact that all strings in the authz model are unique /
       * internized and can be identified by address alone. */
      if (   !step->node
          || i >= acl->rule.len
          || step->segment->kind != acl->rule.path[i].kind
          || step->segment->pattern.data != acl->rule.path[i].pattern.data)
        {
          ctx->path->nelts = i;
          break;
        }
      else
        {
          node = step->node;
        }
    }

  /* Insert the path rule into the filtered tree. */
  insert_path(ctx, node, &path_access,
              acl->rule.len - i, acl->rule.path + i,
              result_pool, scratch_pool);
}

/* Forward declaration ... */
static svn_boolean_t
trim_tree(node_t *node,
          int latest_any_var,
          apr_pool_t *scratch_pool);

/* Call trim_tree() with LATEST_ANY_VAR on all elements in the *HASH of
 * node_t * and remove empty nodes from.  *HASH may be NULL.  If all nodes
 * could be removed, set *HASH to NULL and return TRUE.  Allocate temporary
 * data in SCRATCH_POOL.
 */
static svn_boolean_t
trim_subnode_hash(apr_hash_t **hash,
                  int latest_any_var,
                  apr_pool_t *scratch_pool)
{
  if (*hash)
    {
      apr_array_header_t *to_remove = apr_array_make(scratch_pool, 0,
                                                     sizeof(node_t *));

      apr_hash_index_t *hi;
      for (hi = apr_hash_first(scratch_pool, *hash);
           hi;
           hi = apr_hash_next(hi))
        {
          node_t *node = apr_hash_this_val(hi);
          if (trim_tree(node, latest_any_var, scratch_pool))
            APR_ARRAY_PUSH(to_remove, node_t *) = node;
        }

      /* Are some nodes left? */
      if (to_remove->nelts < apr_hash_count(*hash))
        {
          /* Remove empty nodes (if any). */
          int i;
          for (i = 0; i < to_remove->nelts; ++i)
            {
              node_t *node = APR_ARRAY_IDX(to_remove, i, node_t *);
              apr_hash_set(*hash, node->segment.data, node->segment.len,
                           NULL);
            }

          return FALSE;
        }

      /* No nodes left.  A NULL hash is more efficient than an empty one. */
      *hash = NULL;
    }

  return TRUE;
}

/* Call trim_tree() with LATEST_ANY_VAR on all elements in the *ARRAY of
 * node_t * and remove empty nodes from.  *ARRAY may be NULL.  If all nodes
 * could be removed, set *ARRAY to NULL and return TRUE.  Allocate
 * temporary data in SCRATCH_POOL.
 */
static svn_boolean_t
trim_subnode_array(apr_array_header_t **array,
                   int latest_any_var,
                   apr_pool_t *scratch_pool)
{
  if (*array)
    {
      int i, dest;
      for (i = 0, dest = 0; i < (*array)->nelts; ++i)
        {
          node_t *node = APR_ARRAY_IDX(*array, i, sorted_pattern_t).node;
          if (!trim_tree(node, latest_any_var, scratch_pool))
            {
              if (i != dest)
                APR_ARRAY_IDX(*array, dest, sorted_pattern_t)
                  = APR_ARRAY_IDX(*array, i, sorted_pattern_t);
              ++dest;
            }
        }

      /* Are some nodes left? */
      if (dest)
        {
          /* Trim it to the number of valid entries. */
          (*array)->nelts = dest;
          return FALSE;
        }

      /* No nodes left.  A NULL array is more efficient than an empty one. */
      *array = NULL;
    }

  return TRUE;
}

/* Remove all rules and sub-nodes from NODE that are fully eclipsed by the
 * "any-var" rule with sequence number LATEST_ANY_VAR.  Return TRUE, if
 * there are no rules left in the sub-tree, including NODE.
 * Allocate temporary data in SCRATCH_POOL.
 */
static svn_boolean_t
trim_tree(node_t *node,
          int latest_any_var,
          apr_pool_t *scratch_pool)
{
  svn_boolean_t removed_all = TRUE;

  /* For convenience, we allow NODE to be NULL: */
  if (!node)
    return TRUE;

  /* Do we have a later "any_var" rule at this node. */
  if (   node->pattern_sub_nodes
      && node->pattern_sub_nodes->any_var
      &&   node->pattern_sub_nodes->any_var->rights.access.sequence_number
         > latest_any_var)
    {
      latest_any_var
        = node->pattern_sub_nodes->any_var->rights.access.sequence_number;
    }

  /* Is there a local rule at this node that is not eclipsed by any_var? */
  if (has_local_rule(&node->rights))
    {
      /* Remove the local rule, if it got eclipsed.
       * Note that for the latest any_var node, the sequence number is equal. */
      if (node->rights.access.sequence_number >= latest_any_var)
        removed_all = FALSE;
      else
         node->rights.access.sequence_number = NO_SEQUENCE_NUMBER;
    }

  /* Process all sub-nodes. */
  removed_all &= trim_subnode_hash(&node->sub_nodes, latest_any_var,
                                   scratch_pool);

  if (node->pattern_sub_nodes)
    {
      if (trim_tree(node->pattern_sub_nodes->any, latest_any_var,
                    scratch_pool))
        node->pattern_sub_nodes->any = NULL;
      else
        removed_all = FALSE;

      if (trim_tree(node->pattern_sub_nodes->any_var, latest_any_var,
                    scratch_pool))
        node->pattern_sub_nodes->any_var = NULL;
      else
        removed_all = FALSE;

      removed_all &= trim_subnode_array(&node->pattern_sub_nodes->prefixes,
                                        latest_any_var, scratch_pool);
      removed_all &= trim_subnode_array(&node->pattern_sub_nodes->suffixes,
                                        latest_any_var, scratch_pool);
      removed_all &= trim_subnode_array(&node->pattern_sub_nodes->complex,
                                        latest_any_var, scratch_pool);

      /* Trim the tree as much as possible to speed up lookup(). */
      if (removed_all)
        node->pattern_sub_nodes = NULL;
    }

  return removed_all;
}

/* Forward declaration ... */
static void
finalize_tree(node_t *node,
              limited_rights_t *sum,
              apr_pool_t *scratch_pool);

/* Call finalize_tree() on all elements in the HASH of node_t *, passing
 * SUM along. HASH may be NULL. Use SCRATCH_POOL for temporary allocations.
 */
static void
finalize_subnode_hash(apr_hash_t *hash,
                      limited_rights_t *sum,
                      apr_pool_t *scratch_pool)
{
  if (hash)
    {
      apr_hash_index_t *hi;
      for (hi = apr_hash_first(scratch_pool, hash);
           hi;
           hi = apr_hash_next(hi))
        finalize_tree(apr_hash_this_val(hi), sum, scratch_pool);
    }
}

/* Call finalize_up_tree() on all elements in the ARRAY of node_t *,
 * passing SUM along.  ARRAY may be NULL.  Use SCRATCH_POOL for temporary
 * allocations.
 */
static void
finalize_subnode_array(apr_array_header_t *array,
                       limited_rights_t *sum,
                       apr_pool_t *scratch_pool)
{
  if (array)
    {
      int i;
      for (i = 0; i < array->nelts; ++i)
        finalize_tree(APR_ARRAY_IDX(array, i, sorted_pattern_t).node, sum,
                      scratch_pool);
    }
}

/* Link prefixes within the sorted ARRAY. */
static void
link_prefix_patterns(apr_array_header_t *array)
{
  int i;
  if (!array)
    return;

  for (i = 1; i < array->nelts; ++i)
    {
      sorted_pattern_t *prev
        = &APR_ARRAY_IDX(array, i - 1, sorted_pattern_t);
      sorted_pattern_t *pattern
        = &APR_ARRAY_IDX(array, i, sorted_pattern_t);

      /* Does PATTERN potentially have a prefix in ARRAY?
       * If so, at least the first char must match with the predecessor's
       * because the array is sorted by that string. */
      if (prev->node->segment.data[0] != pattern->node->segment.data[0])
        continue;

      /* Only the predecessor or any of its prefixes can be the closest
       * prefix to PATTERN. */
      for ( ; prev; prev = prev->next)
        if (   prev->node->segment.len < pattern->node->segment.len
            && !memcmp(prev->node->segment.data,
                       pattern->node->segment.data,
                       prev->node->segment.len))
          {
            pattern->next = prev;
            break;
          }
    }
}

/* Recursively finalization the tree node properties for NODE.  Update SUM
 * (of NODE's parent) by combining it with the recursive access rights info
 * on NODE.  Use SCRATCH_POOL for temporary allocations.
 */
static void
finalize_tree(node_t *node,
              limited_rights_t *sum,
              apr_pool_t *scratch_pool)
{
  limited_rights_t *local_sum = &node->rights;

  /* For convenience, we allow NODE to be NULL: */
  if (!node)
    return;

  /* Sum of rights at NODE - so far. */
  if (has_local_rule(local_sum))
    {
      local_sum->max_rights = local_sum->access.rights;
      local_sum->min_rights = local_sum->access.rights;
    }
  else
    {
      local_sum->min_rights = authz_access_write;
      local_sum->max_rights = authz_access_none;
    }

  /* Process all sub-nodes. */
  finalize_subnode_hash(node->sub_nodes, local_sum, scratch_pool);

  if (node->pattern_sub_nodes)
    {
      finalize_tree(node->pattern_sub_nodes->any, local_sum, scratch_pool);
      finalize_tree(node->pattern_sub_nodes->any_var, local_sum, scratch_pool);

      finalize_subnode_array(node->pattern_sub_nodes->prefixes, local_sum,
                             scratch_pool);
      finalize_subnode_array(node->pattern_sub_nodes->suffixes, local_sum,
                             scratch_pool);
      finalize_subnode_array(node->pattern_sub_nodes->complex, local_sum,
                             scratch_pool);

      /* Link up the prefixes / suffixes. */
      link_prefix_patterns(node->pattern_sub_nodes->prefixes);
      link_prefix_patterns(node->pattern_sub_nodes->suffixes);
    }

  /* Add our min / max info to the parent's info.
   * Idempotent for parent == node (happens at root). */
  combine_right_limits(sum, local_sum);
}

/* From the authz CONFIG, extract the parts relevant to USER and REPOSITORY.
 * Return the filtered rule tree.
 */
static node_t *
create_user_authz(authz_full_t *authz,
                  const char *repository,
                  const char *user,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  int i;
  node_t *root = create_node(NULL, result_pool);
  construction_context_t *ctx = create_construction_context(scratch_pool);

  /* Use a separate sub-pool to keep memory usage tight. */
  apr_pool_t *subpool = svn_pool_create(scratch_pool);

  /* Find all ACLs for REPOSITORY. 
   * Note that repo-specific rules replace global rules,
   * even if they don't apply to the current user. */
  apr_array_header_t *acls = apr_array_make(subpool, authz->acls->nelts,
                                            sizeof(authz_acl_t *));
  for (i = 0; i < authz->acls->nelts; ++i)
    {
      const authz_acl_t *acl = &APR_ARRAY_IDX(authz->acls, i, authz_acl_t);
      if (svn_authz__acl_applies_to_repo(acl, repository))
        {
          /* ACLs in the AUTHZ are sorted by path and repository.
           * So, if there is a rule for the repo and a global rule for the
           * same path, we will detect them here. */
          if (acls->nelts)
            {
              const authz_acl_t *prev_acl
                = APR_ARRAY_IDX(acls, acls->nelts - 1, const authz_acl_t *);
              if (svn_authz__compare_paths(&prev_acl->rule, &acl->rule) == 0)
                {
                  SVN_ERR_ASSERT_NO_RETURN(!strcmp(prev_acl->rule.repos,
                                                   AUTHZ_ANY_REPOSITORY));
                  SVN_ERR_ASSERT_NO_RETURN(strcmp(acl->rule.repos,
                                                  AUTHZ_ANY_REPOSITORY));
                  apr_array_pop(acls);
                }
            }

          APR_ARRAY_PUSH(acls, const authz_acl_t *) = acl;
        }
    }

  /* Filtering and tree construction. */
  for (i = 0; i < acls->nelts; ++i)
    process_acl(ctx, APR_ARRAY_IDX(acls, i, const authz_acl_t *),
                root, repository, user, result_pool, subpool);

  /* If there is no relevant rule at the root node, the "no access" default
   * applies. Give it a SEQUENCE_NUMBER that will never overrule others. */
  if (!has_local_rule(&root->rights))
    {
      root->rights.access.sequence_number = 0;
      root->rights.access.rights = authz_access_none;
    }

  /* Trim the tree.
   *
   * We can't do pattern comparison, so for most pattern rules we cannot
   * say that a set of rules "eclipses" / overrides a given other set of
   * rules for all possible paths.  That limits the accuracy of our check
   * for recursive access in similar ways than for non-pattern rules.
   *
   * However, the user expects a rule ending with "**" to eclipse any older
   * rule in that sub-tree recursively.  So, this trim function removes
   * eclipsed nodes from the tree.
   */
  svn_pool_clear(subpool);
  trim_tree(root, NO_SEQUENCE_NUMBER, subpool);

  /* Calculate recursive rights. 
   *
   * This is a bottom-up calculation of the range of access rights
   * specified anywhere in  the respective sub-tree, including the base
   * node itself.
   *
   * To prevent additional finalization passes, we piggy-back the addition
   * of the ordering links of the prefix and suffix sub-node rules.
   */
  svn_pool_clear(subpool);
  finalize_tree(root, &root->rights, subpool);

  /* Done. */
  svn_pool_destroy(subpool);
  return root;
}


/*** Lookup. ***/

/* Reusable lookup state object. It is easy to pass to functions and
 * recycling it between lookups saves significant setup costs. */
typedef struct lookup_state_t
{
  /* Rights immediately applying to this node and limits to the rights to
   * any sub-path. */
  limited_rights_t rights;

  /* Nodes applying to the path followed so far. */
  apr_array_header_t *current;

  /* Temporary array containing the nodes applying to the next path
   * segment (used to build up the next contents of CURRENT). */
  apr_array_header_t *next;

  /* Scratch pad for path operations. */
  svn_stringbuf_t *scratch_pad;

  /* After each lookup iteration, CURRENT and PARENT_RIGHTS will
   * apply to this path. */
  svn_stringbuf_t *parent_path;

  /* Rights that apply at PARENT_PATH, if PARENT_PATH is not empty. */
  limited_rights_t parent_rights;

} lookup_state_t;

/* Constructor for lookup_state_t. */
static lookup_state_t *
create_lookup_state(apr_pool_t *result_pool)
{
  lookup_state_t *state = apr_pcalloc(result_pool, sizeof(*state));
 
  state->next = apr_array_make(result_pool, 4, sizeof(node_t *));
  state->current = apr_array_make(result_pool, 4, sizeof(node_t *));

  /* Virtually all path segments should fit into this buffer.  If they
   * don't, the buffer gets automatically reallocated.
   *
   * Using a smaller initial size would be fine as well but does not
   * buy us much for the increased risk of being expanded anyway - at
   * some extra cost. */
  state->scratch_pad = svn_stringbuf_create_ensure(200, result_pool);

  /* Most paths should fit into this buffer.  The same rationale as
   * above applies. */
  state->parent_path = svn_stringbuf_create_ensure(200, result_pool);

  return state;
}

/* Clear the current contents of STATE and re-initialize it for ROOT.
 * Check whether we can reuse a previous parent path lookup to shorten
 * the current PATH walk.  Return the full or remaining portion of
 * PATH, respectively.  PATH must not be NULL. */
static const char *
init_lockup_state(lookup_state_t *state,
                  node_t *root,
                  const char *path)
{
  apr_size_t len = strlen(path);
  if (   (len > state->parent_path->len)
      && state->parent_path->len
      && (path[state->parent_path->len] == '/')
      && !memcmp(path, state->parent_path->data, state->parent_path->len))
    {
      /* The PARENT_PATH of the previous lookup is actually a parent path
       * of PATH.  The CURRENT node list already matches the parent path
       * and we only have to set the correct rights info. */
      state->rights = state->parent_rights;

      /* Tell the caller where to proceed. */
      return path + state->parent_path->len;
    }

  /* Start lookup at ROOT for the full PATH. */
  state->rights = root->rights;
  state->parent_rights = root->rights;

  apr_array_clear(state->next);
  apr_array_clear(state->current);
  APR_ARRAY_PUSH(state->current, node_t *) = root;

  /* Var-segment rules match empty segments as well */
  if (root->pattern_sub_nodes && root->pattern_sub_nodes->any_var)
   {
      node_t *node = root->pattern_sub_nodes->any_var;

      /* This is non-recursive due to ACL normalization. */
      combine_access(&state->rights, &node->rights);
      combine_right_limits(&state->rights, &node->rights);
      APR_ARRAY_PUSH(state->current, node_t *) = node;
   }

  svn_stringbuf_setempty(state->parent_path);
  svn_stringbuf_setempty(state->scratch_pad);

  return path;
}

/* Add NODE to the list of NEXT nodes in STATE.  NODE may be NULL in which
 * case this is a no-op.  Also update and aggregate the access rights data
 * for the next path segment.
 */
static void
add_next_node(lookup_state_t *state,
              node_t *node)
{
  /* Allowing NULL nodes simplifies the caller. */
  if (node)
    {
      /* The rule with the highest sequence number is the one that applies.
       * Not all nodes that we are following have rules that apply directly
       * to this path but are mere intermediates that may only have some
       * matching deep sub-node. */
      combine_access(&state->rights, &node->rights);

      /* The rule tree node can be seen as an overlay of all the nodes that
       * we are following.  Any of them _may_ match eventually, so the min/
       * max possible access rights are a combination of all these sub-trees.
       */
      combine_right_limits(&state->rights, &node->rights);

      /* NODE is now enlisted as a (potential) match for the next segment. */
      APR_ARRAY_PUSH(state->next, node_t *) = node;

      /* Variable length sub-segment sequences apply to the same node as
       * they match empty sequences as well. */
      if (node->pattern_sub_nodes && node->pattern_sub_nodes->any_var)
        {
          node = node->pattern_sub_nodes->any_var;

          /* This is non-recursive due to ACL normalization. */
          combine_access(&state->rights, &node->rights);
          combine_right_limits(&state->rights, &node->rights);
          APR_ARRAY_PUSH(state->next, node_t *) = node;
        }
    }
}

/* If PREFIX is indeed a prefix (or exact match) or SEGMENT, add the
 * node in PREFIX to STATE. */
static void
add_if_prefix_matches(lookup_state_t *state,
                      const sorted_pattern_t *prefix,
                      const svn_stringbuf_t *segment)
{
  node_t *node = prefix->node;
  if (   node->segment.len <= segment->len
      && !memcmp(node->segment.data, segment->data, node->segment.len))
    add_next_node(state, node);
}

/* Scan the PREFIXES array of node_t* for all entries whose SEGMENT members
 * are prefixes of SEGMENT.  Add these to STATE for the next tree level. */
static void
add_prefix_matches(lookup_state_t *state,
                   const svn_stringbuf_t *segment,
                   apr_array_header_t *prefixes)
{
  /* Index of the first node that might be a match.  All matches will
   * be at this and the immediately following indexes. */
  int i = svn_sort__bsearch_lower_bound(prefixes, segment->data,
                                        compare_node_path_segment);

  /* The entry we found may be an exact match (but not a true prefix).
   * The prefix matching test will still work. */
  if (i < prefixes->nelts)
    add_if_prefix_matches(state,
                          &APR_ARRAY_IDX(prefixes, i, sorted_pattern_t),
                          segment);

  /* The immediate predecessor may be a true prefix and all potential
   * prefixes can be found following the NEXT links between the array
   * indexes. */
  if (i > 0)
    {
      sorted_pattern_t *pattern;
      for (pattern = &APR_ARRAY_IDX(prefixes, i - 1, sorted_pattern_t);
           pattern;
           pattern = pattern->next)
        {
          add_if_prefix_matches(state, pattern, segment);
        }
    }
}

/* Scan the PATTERNS array of node_t* for all entries whose SEGMENT members
 * (usually containing wildcards) match SEGMENT.  Add these to STATE for the
 * next tree level. */
static void
add_complex_matches(lookup_state_t *state,
                    const svn_stringbuf_t *segment,
                    apr_array_header_t *patterns)
{
  int i;
  for (i = 0; i < patterns->nelts; ++i)
    {
      node_t *node = APR_ARRAY_IDX(patterns, i, sorted_pattern_t).node;
      if (0 == apr_fnmatch(node->segment.data, segment->data, 0))
        add_next_node(state, node);
    }
}

/* Extract the next segment from PATH and copy it into SEGMENT, whose current
 * contents get overwritten.  Empty paths ("") are supported and leading '/'
 * segment separators will be interpreted as an empty segment ("").  Non-
 * normalizes parts, i.e. sequences of '/', will be treated as a single '/'.
 *
 * Return the start of the next segment within PATH, skipping the '/'
 * separator(s).  Return NULL, if there are no further segments.
 *
 * The caller (only called by lookup(), ATM) must ensure that SEGMENT has
 * enough room to store all of PATH.
 */
static const char *
next_segment(svn_stringbuf_t *segment,
             const char *path)
{
  apr_size_t len;
  char c;

  /* Read and scan PATH for NUL and '/' -- whichever comes first. */
  for (len = 0, c = *path; c; c = path[++len])
    if (c == '/')
      {
        /* End of segment. */
        segment->data[len] = 0;
        segment->len = len;

        /* If PATH is not normalized, this is where we skip whole sequences
         * of separators. */
        while (path[++len] == '/')
          ;

        /* Continue behind the last separator in the sequence.  We will
         * treat trailing '/' as indicating an empty trailing segment.
         * Therefore, we never have to return NULL here. */
        return path + len;
      }
    else
      {
        /* Copy segment contents directly into the result buffer.
         * On many architectures, this is almost or entirely for free. */
        segment->data[len] = c;
      }

  /* No separator found, so all of PATH has been the last segment. */
  segment->data[len] = 0;
  segment->len = len;

  /* Tell the caller that this has been the last segment. */
  return NULL;
}

/* Starting at the respective user's authz root node provided with STATE,
 * follow PATH and return TRUE, iff the REQUIRED access has been granted to
 * that user for this PATH.  REQUIRED must not contain svn_authz_recursive.
 * If RECURSIVE is set, all paths in the sub-tree at and below PATH must
 * have REQUIRED access.  PATH does not need to be normalized, may be empty
 * but must not be NULL.
 */
static svn_boolean_t
lookup(lookup_state_t *state,
       const char *path,
       authz_access_t required,
       svn_boolean_t recursive,
       apr_pool_t *scratch_pool)
{
  /* Create a scratch pad large enough to hold any of PATH's segments. */
  apr_size_t path_len = strlen(path);
  svn_stringbuf_ensure(state->scratch_pad, path_len);

  /* Normalize start and end of PATH.  Most paths will be fully normalized,
   * so keep the overhead as low as possible. */
  if (path_len && path[path_len-1] == '/')
    {
      do
      {
        --path_len;
      }
      while (path_len && path[path_len-1] == '/');
      path = apr_pstrmemdup(scratch_pool, path, path_len);
    }

  while (path[0] == '/')
    ++path;     /* Don't update PATH_LEN as we won't need it anymore. */

  /* Actually walk the path rule tree following PATH until we run out of
   * either tree or PATH. */
  while (state->current->nelts && path)
    {
      apr_array_header_t *temp;
      int i;
      svn_stringbuf_t *segment = state->scratch_pad;

      /* Shortcut 1: We could nowhere find enough rights in this sub-tree. */
      if ((state->rights.max_rights & required) != required)
        return FALSE;

      /* Shortcut 2: We will find enough rights everywhere in this sub-tree. */
      if ((state->rights.min_rights & required) == required)
        return TRUE;

      /* Extract the next segment. */
      path = next_segment(segment, path);

      /* Initial state for this segment. */
      apr_array_clear(state->next);
      state->rights.access.sequence_number = NO_SEQUENCE_NUMBER;
      state->rights.access.rights = authz_access_none;

      /* These init values ensure that the first node's value will be used
       * when combined with them.  If there is no first node,
       * state->access.sequence_number remains unchanged and we will use
       * the parent's (i.e. inherited) access rights. */
      state->rights.min_rights = authz_access_write;
      state->rights.max_rights = authz_access_none;

      /* Update the PARENT_PATH member in STATE to match the nodes in
       * CURRENT at the end of this iteration, i.e. if and when NEXT
       * has become CURRENT. */
      if (path)
        {
          svn_stringbuf_appendbyte(state->parent_path, '/');
          svn_stringbuf_appendbytes(state->parent_path, segment->data,
                                    segment->len);
        }

      /* Scan follow all alternative routes to the next level. */
      for (i = 0; i < state->current->nelts; ++i)
        {
          node_t *node = APR_ARRAY_IDX(state->current, i, node_t *);
          if (node->sub_nodes)
            add_next_node(state, apr_hash_get(node->sub_nodes, segment->data,
                                              segment->len));

          /* Process alternative, wildcard-based sub-nodes. */
          if (node->pattern_sub_nodes)
            {
              add_next_node(state, node->pattern_sub_nodes->any);

              /* If the current node represents a "**" pattern, it matches
               * to all levels. So, add it to the list for the NEXT level. */
              if (node->pattern_sub_nodes->repeat)
                add_next_node(state, node);

              /* Find all prefix pattern matches. */
              if (node->pattern_sub_nodes->prefixes)
                add_prefix_matches(state, segment,
                                   node->pattern_sub_nodes->prefixes);

              if (node->pattern_sub_nodes->complex)
                add_complex_matches(state, segment,
                                    node->pattern_sub_nodes->complex);

              /* Find all suffux pattern matches.
               * This must be the last check as it destroys SEGMENT. */
              if (node->pattern_sub_nodes->suffixes)
                {
                  /* Suffixes behave like reversed prefixes. */
                  svn_authz__reverse_string(segment->data, segment->len);
                  add_prefix_matches(state, segment,
                                     node->pattern_sub_nodes->suffixes);
                }
            }
        }

      /* If no rule applied to this SEGMENT directly, the parent rights
       * will apply to at least the SEGMENT node itself and possibly
       * other parts deeper in it's subtree. */
      if (!has_local_rule(&state->rights))
        {
          state->rights.access = state->parent_rights.access;
          state->rights.min_rights &= state->parent_rights.access.rights;
          state->rights.max_rights |= state->parent_rights.access.rights;
        }

      /* The list of nodes for SEGMENT is now complete.  If we need to
       * continue, make it the current and put the old one into the recycler.
       *
       * If this is the end of the path, keep the parent path and rights in
       * STATE as are such that sibling lookups will benefit from it.
       */
      if (path)
        {
          temp = state->current;
          state->current = state->next;
          state->next = temp;

          /* In STATE, PARENT_PATH, PARENT_RIGHTS and CURRENT are now in sync. */
          state->parent_rights = state->rights;
        }
    }

  /* If we check recursively, none of the (potential) sub-paths must have
   * less than the REQUIRED access rights.  "Potential" because we don't
   * verify that the respective paths actually exist in the repository.
   */
  if (recursive)
    return (state->rights.min_rights & required) == required;

  /* Return whether the access rights on PATH fully include REQUIRED. */
  return (state->rights.access.rights & required) == required;
}



/*** The authz data structure. ***/

/* An entry in svn_authz_t's USER_RULES cache.  All members must be
 * allocated in the POOL and the latter has to be cleared / destroyed
 * before overwriting the entries' contents.
 */
struct authz_user_rules_t
{
  /* User name for which we filtered the rules.
   * User NULL for the anonymous user. */
  const char *user;

  /* Repository name for which we filtered the rules.
   * May be empty but never NULL for used entries. */
  const char *repository;

  /* The combined min/max rights USER has on REPOSITORY. */
  authz_rights_t global_rights;

  /* Root of the filtered path rule tree.
   * Will remain NULL until the first usage. */
  node_t *root;

  /* Reusable lookup state instance. */
  lookup_state_t *lookup_state;

  /* Pool from which all data within this struct got allocated.
   * Can be destroyed or cleaned up with no further side-effects. */
  apr_pool_t *pool;
};

/* Return TRUE, iff AUTHZ matches the pair of REPOS_NAME and USER.
 * Note that USER may be NULL.
 */
static svn_boolean_t
matches_filtered_tree(const authz_user_rules_t *authz,
                      const char *repos_name,
                      const char *user)
{
  /* Does the user match? */
  if (user)
    {
      if (authz->user == NULL || strcmp(user, authz->user))
        return FALSE;
    }
  else if (authz->user != NULL)
    return FALSE;

  /* Does the repository match as well? */
  return strcmp(repos_name, authz->repository) == 0;
}

/* Check if AUTHZ's already contains a path rule tree filtered for this
 * USER, REPOS_NAME combination.  If that does not exist, yet, create one
 * but don't construct the actual filtered tree, yet.
 */
static authz_user_rules_t *
get_user_rules(svn_authz_t *authz,
               const char *repos_name,
               const char *user)
{
  apr_pool_t *pool;

  /* Search our cache for a suitable previously filtered tree. */
  if (authz->filtered)
    {
      /* Is this a suitable filtered tree? */
      if (matches_filtered_tree(authz->filtered, repos_name, user))
        return authz->filtered;

      /* Drop the old filtered tree before creating a new one. */
      svn_pool_destroy(authz->filtered->pool);
      authz->filtered = NULL;
    }

  /* Global cache lookup.  Filter the full model only if necessary. */
  pool = svn_pool_create(authz->pool);

  /* Write a new entry. */
  authz->filtered = apr_palloc(pool, sizeof(*authz->filtered));
  authz->filtered->pool = pool;
  authz->filtered->repository = apr_pstrdup(pool, repos_name);
  authz->filtered->user = user ? apr_pstrdup(pool, user) : NULL;
  authz->filtered->lookup_state = create_lookup_state(pool);
  authz->filtered->root = NULL;

  svn_authz__get_global_rights(&authz->filtered->global_rights,
                               authz->full, user, repos_name);

  return authz->filtered;
}

/* In AUTHZ's user rules, construct the actual filtered tree.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
filter_tree(svn_authz_t *authz,
            apr_pool_t *scratch_pool)
{
  apr_pool_t *pool = authz->filtered->pool;
  const char *repos_name = authz->filtered->repository;
  const char *user = authz->filtered->user;
  node_t *root;

  if (filtered_pool)
    {
      svn_membuf_t *key = construct_filtered_key(repos_name, user,
                                                 authz->authz_id,
                                                 scratch_pool);

      /* Cache lookup. */
      SVN_ERR(svn_object_pool__lookup((void **)&root, filtered_pool, key,
                                      pool));

      if (!root)
        {
          apr_pool_t *item_pool = svn_object_pool__new_item_pool(authz_pool);
          authz_full_t *add_ref = NULL;

          /* Make sure the underlying full authz object lives as long as the
           * filtered one that we are about to create.  We do this by adding
           * a reference to it in ITEM_POOL (which may live longer than AUTHZ).
           *
           * Note that we already have a reference to that full authz in
           * AUTHZ->FULL. Assert that we actually don't created multiple
           * instances of the same full model.
           */
          svn_error_clear(svn_object_pool__lookup((void **)&add_ref,
                                                  authz_pool, authz->authz_id,
                                                  item_pool));
          SVN_ERR_ASSERT(add_ref == authz->full);

          /* Now construct the new filtered tree and cache it. */
          root = create_user_authz(authz->full, repos_name, user, item_pool,
                                   scratch_pool);
          svn_error_clear(svn_object_pool__insert((void **)&root,
                                                  filtered_pool, key, root,
                                                  item_pool, pool));
        }
     }
  else
    {
      root = create_user_authz(authz->full, repos_name, user, pool,
                               scratch_pool);
    }

  /* Write a new entry. */
  authz->filtered->root = root;

  return SVN_NO_ERROR;
}



/* Read authz configuration data from PATH into *AUTHZ_P, allocated in
   RESULT_POOL.  Return the cache key in *AUTHZ_ID.  If GROUPS_PATH is set,
   use the global groups parsed from it.  Use SCRATCH_POOL for temporary
   allocations.

   PATH and GROUPS_PATH may be a dirent or an absolute file url.  REPOS_HINT
   may be specified to speed up access to in-repo authz files.

   If PATH or GROUPS_PATH is not a valid authz rule file, then return
   SVN_AUTHZ_INVALID_CONFIG.  The contents of *AUTHZ_P is then
   undefined.  If MUST_EXIST is TRUE, a missing authz or global groups file
   is also an error. */
static svn_error_t *
authz_read(authz_full_t **authz_p,
           svn_membuf_t **authz_id,
           const char *path,
           const char *groups_path,
           svn_boolean_t must_exist,
           svn_repos_t *repos_hint,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool)
{
  svn_error_t* err = NULL;
  svn_stream_t *rules_stream = NULL;
  svn_stream_t *groups_stream = NULL;
  svn_checksum_t *rules_checksum = NULL;
  svn_checksum_t *groups_checksum = NULL;

  config_access_t *config_access =
    svn_repos__create_config_access(repos_hint, scratch_pool);

  /* Open the main authz file */
  SVN_ERR(svn_repos__get_config(&rules_stream, &rules_checksum, config_access,
                                path, must_exist, scratch_pool));

  /* Open the optional groups file */
  if (groups_path)
    SVN_ERR(svn_repos__get_config(&groups_stream, &groups_checksum,
                                  config_access, groups_path, must_exist,
                                  scratch_pool));

  /* The authz cache is optional. */
  *authz_id = construct_authz_key(rules_checksum, groups_checksum,
                                  result_pool);
  if (authz_pool)
    {
      /* Cache lookup. */
      SVN_ERR(svn_object_pool__lookup((void **)authz_p, authz_pool,
                                      *authz_id, result_pool));

      /* If not found, parse and add to cache. */
      if (!*authz_p)
        {
          apr_pool_t *item_pool = svn_object_pool__new_item_pool(authz_pool);

          /* Parse the configuration(s) and construct the full authz model
           * from it. */
          err = svn_authz__parse(authz_p, rules_stream, groups_stream,
                                item_pool, scratch_pool);
          if (err != SVN_NO_ERROR)
            {
              /* That pool would otherwise never get destroyed. */
              svn_pool_destroy(item_pool);

              /* Add the URL / file name to the error stack since the parser
               * doesn't have it. */
              err = svn_error_quick_wrapf(err,
                                   "Error while parsing config file: '%s':",
                                   path);
            }
          else
            {
              SVN_ERR(svn_object_pool__insert((void **)authz_p, authz_pool,
                                              *authz_id, *authz_p,
                                              item_pool, result_pool));
            }
        }
    }
  else
    {
      /* Parse the configuration(s) and construct the full authz model from
       * it. */
      err = svn_error_quick_wrapf(svn_authz__parse(authz_p, rules_stream,
                                                   groups_stream,
                                                   result_pool, scratch_pool),
                                  "Error while parsing authz file: '%s':",
                                  path);
    }

  svn_repos__destroy_config_access(config_access);

  return err;
}



/*** Public functions. ***/

svn_error_t *
svn_repos_authz_read3(svn_authz_t **authz_p,
                      const char *path,
                      const char *groups_path,
                      svn_boolean_t must_exist,
                      svn_repos_t *repos_hint,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_authz_t *authz = apr_pcalloc(result_pool, sizeof(*authz));
  authz->pool = result_pool;

  SVN_ERR(authz_read(&authz->full, &authz->authz_id, path, groups_path,
                     must_exist, repos_hint, result_pool, scratch_pool));

  *authz_p = authz;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_authz_parse(svn_authz_t **authz_p, svn_stream_t *stream,
                      svn_stream_t *groups_stream, apr_pool_t *pool)
{
  apr_pool_t *scratch_pool = svn_pool_create(pool);
  svn_authz_t *authz = apr_pcalloc(pool, sizeof(*authz));
  authz->pool = pool;

  /* Parse the configuration and construct the full authz model from it. */
  SVN_ERR(svn_authz__parse(&authz->full, stream, groups_stream, pool,
                           scratch_pool));

  svn_pool_destroy(scratch_pool);

  *authz_p = authz;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_authz_check_access(svn_authz_t *authz, const char *repos_name,
                             const char *path, const char *user,
                             svn_repos_authz_access_t required_access,
                             svn_boolean_t *access_granted,
                             apr_pool_t *pool)
{
  const authz_access_t required =
    ((required_access & svn_authz_read ? authz_access_read_flag : 0)
     | (required_access & svn_authz_write ? authz_access_write_flag : 0));

  /* Pick or create the suitable pre-filtered path rule tree. */
  authz_user_rules_t *rules = get_user_rules(
      authz,
      (repos_name ? repos_name : AUTHZ_ANY_REPOSITORY),
      user);

  /* In many scenarios, users have uniform access to a repository
   * (blanket access or no access at all).
   *
   * In these cases, don't bother creating or consulting the filtered tree.
   */
  if ((rules->global_rights.min_access & required) == required)
    {
      *access_granted = TRUE;
      return SVN_NO_ERROR;
    }

  if ((rules->global_rights.max_access & required) != required)
    {
      *access_granted = FALSE;
      return SVN_NO_ERROR;
    }

  /* No specific path given, i.e. looking for anywhere in the tree? */
  if (!path)
    {
      *access_granted =
        ((rules->global_rights.max_access & required) == required);
      return SVN_NO_ERROR;
    }

  /* Rules tree lookup */

  /* Did we already filter the data model? */
  if (!rules->root)
    SVN_ERR(filter_tree(authz, pool));

  /* Re-use previous lookup results, if possible. */
  path = init_lockup_state(authz->filtered->lookup_state,
                           authz->filtered->root, path);

  /* Sanity check. */
  SVN_ERR_ASSERT(path[0] == '/');

  /* Determine the granted access for the requested path.
   * PATH does not need to be normalized for lockup(). */
  *access_granted = lookup(rules->lookup_state, path, required,
                           !!(required_access & svn_authz_recursive), pool);

  return SVN_NO_ERROR;
}
