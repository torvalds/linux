/* load-fs-vtable.c --- dumpstream loader vtable for committing into a
 *                      Subversion filesystem.
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


#include "svn_private_config.h"
#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_string.h"
#include "svn_props.h"
#include "repos.h"
#include "svn_mergeinfo.h"
#include "svn_checksum.h"
#include "svn_subst.h"
#include "svn_dirent_uri.h"

#include <apr_lib.h>

#include "private/svn_fspath.h"
#include "private/svn_dep_compat.h"
#include "private/svn_mergeinfo_private.h"
#include "private/svn_repos_private.h"

/*----------------------------------------------------------------------*/

/** Batons used herein **/

struct parse_baton
{
  svn_repos_t *repos;
  svn_fs_t *fs;

  svn_boolean_t use_history;
  svn_boolean_t validate_props;
  svn_boolean_t ignore_dates;
  svn_boolean_t normalize_props;
  svn_boolean_t use_pre_commit_hook;
  svn_boolean_t use_post_commit_hook;
  enum svn_repos_load_uuid uuid_action;
  const char *parent_dir; /* repository relpath, or NULL */
  svn_repos_notify_func_t notify_func;
  void *notify_baton;
  apr_pool_t *notify_pool; /* scratch pool for notifications */
  apr_pool_t *pool;

  /* Start and end (inclusive) of revision range we'll pay attention
     to, or a pair of SVN_INVALID_REVNUMs if we're not filtering by
     revisions. */
  svn_revnum_t start_rev;
  svn_revnum_t end_rev;

  /* A hash mapping copy-from revisions and mergeinfo range revisions
     (svn_revnum_t *) in the dump stream to their corresponding revisions
     (svn_revnum_t *) in the loaded repository.  The hash and its
     contents are allocated in POOL. */
  /* ### See http://subversion.tigris.org/issues/show_bug.cgi?id=3903
     ### for discussion about improving the memory costs of this mapping. */
  apr_hash_t *rev_map;

  /* The most recent (youngest) revision from the dump stream mapped in
     REV_MAP.  If no revisions have been mapped yet, this is set to
     SVN_INVALID_REVNUM. */
  svn_revnum_t last_rev_mapped;

  /* The oldest revision loaded from the dump stream.  If no revisions
     have been loaded yet, this is set to SVN_INVALID_REVNUM. */
  svn_revnum_t oldest_dumpstream_rev;
};

struct revision_baton
{
  /* rev num from dump file */
  svn_revnum_t rev;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  const svn_string_t *datestamp;

  /* (rev num from dump file) minus (rev num to be committed) */
  apr_int32_t rev_offset;
  svn_boolean_t skipped;

  /* Array of svn_prop_t with revision properties. */
  apr_array_header_t *revprops;

  struct parse_baton *pb;
  apr_pool_t *pool;
};

struct node_baton
{
  const char *path;
  svn_node_kind_t kind;
  enum svn_node_action action;
  svn_checksum_t *base_checksum;        /* null, if not available */
  svn_checksum_t *result_checksum;      /* null, if not available */
  svn_checksum_t *copy_source_checksum; /* null, if not available */

  svn_revnum_t copyfrom_rev;
  const char *copyfrom_path;

  struct revision_baton *rb;
  apr_pool_t *pool;
};


/*----------------------------------------------------------------------*/

/* Record the mapping of FROM_REV to TO_REV in REV_MAP, ensuring that
   anything added to the hash is allocated in the hash's pool. */
static void
set_revision_mapping(apr_hash_t *rev_map,
                     svn_revnum_t from_rev,
                     svn_revnum_t to_rev)
{
  svn_revnum_t *mapped_revs = apr_palloc(apr_hash_pool_get(rev_map),
                                         sizeof(svn_revnum_t) * 2);
  mapped_revs[0] = from_rev;
  mapped_revs[1] = to_rev;
  apr_hash_set(rev_map, mapped_revs,
               sizeof(svn_revnum_t), mapped_revs + 1);
}

/* Return the revision to which FROM_REV maps in REV_MAP, or
   SVN_INVALID_REVNUM if no such mapping exists. */
static svn_revnum_t
get_revision_mapping(apr_hash_t *rev_map,
                     svn_revnum_t from_rev)
{
  svn_revnum_t *to_rev = apr_hash_get(rev_map, &from_rev,
                                      sizeof(from_rev));
  return to_rev ? *to_rev : SVN_INVALID_REVNUM;
}


/* Change revision property NAME to VALUE for REVISION in REPOS.  If
   VALIDATE_PROPS is set, use functions which perform validation of
   the property value.  Otherwise, bypass those checks. */
static svn_error_t *
change_rev_prop(svn_repos_t *repos,
                svn_revnum_t revision,
                const char *name,
                const svn_string_t *value,
                svn_boolean_t validate_props,
                svn_boolean_t normalize_props,
                apr_pool_t *pool)
{
  if (normalize_props)
    SVN_ERR(svn_repos__normalize_prop(&value, NULL, name, value, pool, pool));

  if (validate_props)
    return svn_repos_fs_change_rev_prop4(repos, revision, NULL, name,
                                         NULL, value, FALSE, FALSE,
                                         NULL, NULL, pool);
  else
    return svn_fs_change_rev_prop2(svn_repos_fs(repos), revision, name,
                                   NULL, value, pool);
}

/* Change property NAME to VALUE for PATH in TXN_ROOT.  If
   VALIDATE_PROPS is set, use functions which perform validation of
   the property value.  Otherwise, bypass those checks. */
static svn_error_t *
change_node_prop(svn_fs_root_t *txn_root,
                 const char *path,
                 const char *name,
                 const svn_string_t *value,
                 svn_boolean_t validate_props,
                 apr_pool_t *pool)
{
  if (validate_props)
    return svn_repos_fs_change_node_prop(txn_root, path, name, value, pool);
  else
    return svn_fs_change_node_prop(txn_root, path, name, value, pool);
}

/* Prepend the mergeinfo source paths in MERGEINFO_ORIG with PARENT_DIR, and
   return it in *MERGEINFO_VAL. */
static svn_error_t *
prefix_mergeinfo_paths(svn_string_t **mergeinfo_val,
                       const svn_string_t *mergeinfo_orig,
                       const char *parent_dir,
                       apr_pool_t *pool)
{
  apr_hash_t *prefixed_mergeinfo, *mergeinfo;
  apr_hash_index_t *hi;

  SVN_ERR(svn_mergeinfo_parse(&mergeinfo, mergeinfo_orig->data, pool));
  prefixed_mergeinfo = apr_hash_make(pool);
  for (hi = apr_hash_first(pool, mergeinfo); hi; hi = apr_hash_next(hi))
    {
      const char *merge_source = apr_hash_this_key(hi);
      svn_rangelist_t *rangelist = apr_hash_this_val(hi);
      const char *path;

      merge_source = svn_relpath_canonicalize(merge_source, pool);

      /* The svn:mergeinfo property syntax demands a repos abspath */
      path = svn_fspath__canonicalize(svn_relpath_join(parent_dir,
                                                       merge_source, pool),
                                      pool);
      svn_hash_sets(prefixed_mergeinfo, path, rangelist);
    }
  return svn_mergeinfo_to_string(mergeinfo_val, prefixed_mergeinfo, pool);
}


/* Examine the mergeinfo in INITIAL_VAL, renumber revisions in rangelists
   as appropriate, and return the (possibly new) mergeinfo in *FINAL_VAL
   (allocated from POOL).

   Adjust any mergeinfo revisions not older than OLDEST_DUMPSTREAM_REV by
   using REV_MAP which maps (svn_revnum_t) old rev to (svn_revnum_t) new rev.

   Adjust any mergeinfo revisions older than OLDEST_DUMPSTREAM_REV by
   (-OLDER_REVS_OFFSET), dropping any that become <= 0.
 */
static svn_error_t *
renumber_mergeinfo_revs(svn_string_t **final_val,
                        const svn_string_t *initial_val,
                        apr_hash_t *rev_map,
                        svn_revnum_t oldest_dumpstream_rev,
                        apr_int32_t older_revs_offset,
                        apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_mergeinfo_t mergeinfo, predates_stream_mergeinfo;
  svn_mergeinfo_t final_mergeinfo = apr_hash_make(subpool);
  apr_hash_index_t *hi;

  SVN_ERR(svn_mergeinfo_parse(&mergeinfo, initial_val->data, subpool));

  /* Issue #3020
     http://subversion.tigris.org/issues/show_bug.cgi?id=3020#desc16
     Remove mergeinfo older than the oldest revision in the dump stream
     and adjust its revisions by the difference between the head rev of
     the target repository and the current dump stream rev. */
  if (oldest_dumpstream_rev > 1)
    {
      /* predates_stream_mergeinfo := mergeinfo that refers to revs before
         oldest_dumpstream_rev */
      SVN_ERR(svn_mergeinfo__filter_mergeinfo_by_ranges(
        &predates_stream_mergeinfo, mergeinfo,
        oldest_dumpstream_rev - 1, 0,
        TRUE, subpool, subpool));
      /* mergeinfo := mergeinfo that refers to revs >= oldest_dumpstream_rev */
      SVN_ERR(svn_mergeinfo__filter_mergeinfo_by_ranges(
        &mergeinfo, mergeinfo,
        oldest_dumpstream_rev - 1, 0,
        FALSE, subpool, subpool));
      SVN_ERR(svn_mergeinfo__adjust_mergeinfo_rangelists(
        &predates_stream_mergeinfo, predates_stream_mergeinfo,
        -older_revs_offset, subpool, subpool));
    }
  else
    {
      predates_stream_mergeinfo = NULL;
    }

  for (hi = apr_hash_first(subpool, mergeinfo); hi; hi = apr_hash_next(hi))
    {
      const char *merge_source = apr_hash_this_key(hi);
      svn_rangelist_t *rangelist = apr_hash_this_val(hi);
      int i;

      /* Possibly renumber revisions in merge source's rangelist. */
      for (i = 0; i < rangelist->nelts; i++)
        {
          svn_revnum_t rev_from_map;
          svn_merge_range_t *range = APR_ARRAY_IDX(rangelist, i,
                                                   svn_merge_range_t *);
          rev_from_map = get_revision_mapping(rev_map, range->start);
          if (SVN_IS_VALID_REVNUM(rev_from_map))
            {
              range->start = rev_from_map;
            }
          else if (range->start == oldest_dumpstream_rev - 1)
            {
              /* Since the start revision of svn_merge_range_t are not
                 inclusive there is one possible valid start revision that
                 won't be found in the REV_MAP mapping of load stream
                 revsions to loaded revisions: The revision immediately
                 preceding the oldest revision from the load stream.
                 This is a valid revision for mergeinfo, but not a valid
                 copy from revision (which REV_MAP also maps for) so it
                 will never be in the mapping.

                 If that is what we have here, then find the mapping for the
                 oldest rev from the load stream and subtract 1 to get the
                 renumbered, non-inclusive, start revision. */
              rev_from_map = get_revision_mapping(rev_map,
                                                  oldest_dumpstream_rev);
              if (SVN_IS_VALID_REVNUM(rev_from_map))
                range->start = rev_from_map - 1;
            }
          else
            {
              /* If we can't remap the start revision then don't even bother
                 trying to remap the end revision.  It's possible we might
                 actually succeed at the latter, which can result in invalid
                 mergeinfo with a start rev > end rev.  If that gets into the
                 repository then a world of bustage breaks loose anytime that
                 bogus mergeinfo is parsed.  See
                 http://subversion.tigris.org/issues/show_bug.cgi?id=3020#desc16.
                 */
              continue;
            }

          rev_from_map = get_revision_mapping(rev_map, range->end);
          if (SVN_IS_VALID_REVNUM(rev_from_map))
            range->end = rev_from_map;
        }
      svn_hash_sets(final_mergeinfo, merge_source, rangelist);
    }

  if (predates_stream_mergeinfo)
    {
      SVN_ERR(svn_mergeinfo_merge2(final_mergeinfo, predates_stream_mergeinfo,
                                   subpool, subpool));
    }

  SVN_ERR(svn_mergeinfo__canonicalize_ranges(final_mergeinfo, subpool));

  SVN_ERR(svn_mergeinfo_to_string(final_val, final_mergeinfo, pool));
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/*----------------------------------------------------------------------*/

/** vtable for doing commits to a fs **/


/* Make a node baton, parsing the relevant HEADERS.
 *
 * If RB->pb->parent_dir:
 *   prefix it to NB->path
 *   prefix it to NB->copyfrom_path (if present)
 */
static svn_error_t *
make_node_baton(struct node_baton **node_baton_p,
                apr_hash_t *headers,
                struct revision_baton *rb,
                apr_pool_t *pool)
{
  struct node_baton *nb = apr_pcalloc(pool, sizeof(*nb));
  const char *val;

  /* Start with sensible defaults. */
  nb->rb = rb;
  nb->pool = pool;
  nb->kind = svn_node_unknown;

  /* Then add info from the headers.  */
  if ((val = svn_hash_gets(headers, SVN_REPOS_DUMPFILE_NODE_PATH)))
  {
    val = svn_relpath_canonicalize(val, pool);
    if (rb->pb->parent_dir)
      nb->path = svn_relpath_join(rb->pb->parent_dir, val, pool);
    else
      nb->path = val;
  }

  if ((val = svn_hash_gets(headers, SVN_REPOS_DUMPFILE_NODE_KIND)))
    {
      if (! strcmp(val, "file"))
        nb->kind = svn_node_file;
      else if (! strcmp(val, "dir"))
        nb->kind = svn_node_dir;
    }

  nb->action = (enum svn_node_action)(-1);  /* an invalid action code */
  if ((val = svn_hash_gets(headers, SVN_REPOS_DUMPFILE_NODE_ACTION)))
    {
      if (! strcmp(val, "change"))
        nb->action = svn_node_action_change;
      else if (! strcmp(val, "add"))
        nb->action = svn_node_action_add;
      else if (! strcmp(val, "delete"))
        nb->action = svn_node_action_delete;
      else if (! strcmp(val, "replace"))
        nb->action = svn_node_action_replace;
    }

  nb->copyfrom_rev = SVN_INVALID_REVNUM;
  if ((val = svn_hash_gets(headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV)))
    {
      nb->copyfrom_rev = SVN_STR_TO_REV(val);
    }
  if ((val = svn_hash_gets(headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH)))
    {
      val = svn_relpath_canonicalize(val, pool);
      if (rb->pb->parent_dir)
        nb->copyfrom_path = svn_relpath_join(rb->pb->parent_dir, val, pool);
      else
        nb->copyfrom_path = val;
    }

  if ((val = svn_hash_gets(headers, SVN_REPOS_DUMPFILE_TEXT_CONTENT_CHECKSUM)))
    {
      SVN_ERR(svn_checksum_parse_hex(&nb->result_checksum, svn_checksum_md5,
                                     val, pool));
    }

  if ((val = svn_hash_gets(headers,
                           SVN_REPOS_DUMPFILE_TEXT_DELTA_BASE_CHECKSUM)))
    {
      SVN_ERR(svn_checksum_parse_hex(&nb->base_checksum, svn_checksum_md5, val,
                                     pool));
    }

  if ((val = svn_hash_gets(headers,
                           SVN_REPOS_DUMPFILE_TEXT_COPY_SOURCE_CHECKSUM)))
    {
      SVN_ERR(svn_checksum_parse_hex(&nb->copy_source_checksum,
                                     svn_checksum_md5, val, pool));
    }

  /* What's cool about this dump format is that the parser just
     ignores any unrecognized headers.  :-)  */

  *node_baton_p = nb;
  return SVN_NO_ERROR;
}

/* Make a revision baton, parsing the relevant HEADERS.
 *
 * Set RB->skipped iff the revision number is outside the range given in PB.
 */
static struct revision_baton *
make_revision_baton(apr_hash_t *headers,
                    struct parse_baton *pb,
                    apr_pool_t *pool)
{
  struct revision_baton *rb = apr_pcalloc(pool, sizeof(*rb));
  const char *val;

  rb->pb = pb;
  rb->pool = pool;
  rb->rev = SVN_INVALID_REVNUM;
  rb->revprops = apr_array_make(rb->pool, 8, sizeof(svn_prop_t));

  if ((val = svn_hash_gets(headers, SVN_REPOS_DUMPFILE_REVISION_NUMBER)))
    {
      rb->rev = SVN_STR_TO_REV(val);

      /* If we're filtering revisions, is this one we'll skip? */
      rb->skipped = (SVN_IS_VALID_REVNUM(pb->start_rev)
                     && ((rb->rev < pb->start_rev) ||
                         (rb->rev > pb->end_rev)));
    }

  return rb;
}


static svn_error_t *
new_revision_record(void **revision_baton,
                    apr_hash_t *headers,
                    void *parse_baton,
                    apr_pool_t *pool)
{
  struct parse_baton *pb = parse_baton;
  struct revision_baton *rb;
  svn_revnum_t head_rev;

  rb = make_revision_baton(headers, pb, pool);

  /* ### If we're filtering revisions, and this is one we've skipped,
     ### and we've skipped it because it has a revision number younger
     ### than the youngest in our acceptable range, then should we
     ### just bail out here? */
  /*
  if (rb->skipped && (rb->rev > pb->end_rev))
    return svn_error_createf(SVN_ERR_CEASE_INVOCATION, 0,
                             _("Finished processing acceptable load "
                               "revision range"));
  */

  SVN_ERR(svn_fs_youngest_rev(&head_rev, pb->fs, pool));

  /* FIXME: This is a lame fallback loading multiple segments of dump in
     several separate operations. It is highly susceptible to race conditions.
     Calculate the revision 'offset' for finding copyfrom sources.
     It might be positive or negative. */
  rb->rev_offset = (apr_int32_t) ((rb->rev) - (head_rev + 1));

  if ((rb->rev > 0) && (! rb->skipped))
    {
      /* Create a new fs txn. */
      SVN_ERR(svn_fs_begin_txn2(&(rb->txn), pb->fs, head_rev,
                                SVN_FS_TXN_CLIENT_DATE, pool));
      SVN_ERR(svn_fs_txn_root(&(rb->txn_root), rb->txn, pool));

      if (pb->notify_func)
        {
          /* ### TODO: Use proper scratch pool instead of pb->notify_pool */
          svn_repos_notify_t *notify = svn_repos_notify_create(
                                            svn_repos_notify_load_txn_start,
                                            pb->notify_pool);

          notify->old_revision = rb->rev;
          pb->notify_func(pb->notify_baton, notify, pb->notify_pool);
          svn_pool_clear(pb->notify_pool);
        }

      /* Stash the oldest "old" revision committed from the load stream. */
      if (!SVN_IS_VALID_REVNUM(pb->oldest_dumpstream_rev))
        pb->oldest_dumpstream_rev = rb->rev;
    }

  /* If we're skipping this revision, try to notify someone. */
  if (rb->skipped && pb->notify_func)
    {
      /* ### TODO: Use proper scratch pool instead of pb->notify_pool */
      svn_repos_notify_t *notify = svn_repos_notify_create(
                                        svn_repos_notify_load_skipped_rev,
                                        pb->notify_pool);

      notify->old_revision = rb->rev;
      pb->notify_func(pb->notify_baton, notify, pb->notify_pool);
      svn_pool_clear(pb->notify_pool);
    }

  /* If we're parsing revision 0, only the revision props are (possibly)
     interesting to us: when loading the stream into an empty
     filesystem, then we want new filesystem's revision 0 to have the
     same props.  Otherwise, we just ignore revision 0 in the stream. */

  *revision_baton = rb;
  return SVN_NO_ERROR;
}



/* Perform a copy or a plain add.
 *
 * For a copy, also adjust the copy-from rev, check any copy-source checksum,
 * and send a notification.
 */
static svn_error_t *
maybe_add_with_history(struct node_baton *nb,
                       struct revision_baton *rb,
                       apr_pool_t *pool)
{
  struct parse_baton *pb = rb->pb;

  if ((nb->copyfrom_path == NULL) || (! pb->use_history))
    {
      /* Add empty file or dir, without history. */
      if (nb->kind == svn_node_file)
        SVN_ERR(svn_fs_make_file(rb->txn_root, nb->path, pool));

      else if (nb->kind == svn_node_dir)
        SVN_ERR(svn_fs_make_dir(rb->txn_root, nb->path, pool));
    }
  else
    {
      /* Hunt down the source revision in this fs. */
      svn_fs_root_t *copy_root;
      svn_revnum_t copyfrom_rev;

      /* Try to find the copyfrom revision in the revision map;
         failing that, fall back to the revision offset approach. */
      copyfrom_rev = get_revision_mapping(rb->pb->rev_map, nb->copyfrom_rev);
      if (! SVN_IS_VALID_REVNUM(copyfrom_rev))
        copyfrom_rev = nb->copyfrom_rev - rb->rev_offset;

      if (! SVN_IS_VALID_REVNUM(copyfrom_rev))
        return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                                 _("Relative source revision %ld is not"
                                   " available in current repository"),
                                 copyfrom_rev);

      SVN_ERR(svn_fs_revision_root(&copy_root, pb->fs, copyfrom_rev, pool));

      if (nb->copy_source_checksum)
        {
          svn_checksum_t *checksum;
          SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_md5, copy_root,
                                       nb->copyfrom_path, TRUE, pool));
          if (!svn_checksum_match(nb->copy_source_checksum, checksum))
            return svn_checksum_mismatch_err(nb->copy_source_checksum,
                      checksum, pool,
                      _("Copy source checksum mismatch on copy from '%s'@%ld\n"
                        "to '%s' in rev based on r%ld"),
                      nb->copyfrom_path, copyfrom_rev, nb->path, rb->rev);
        }

      SVN_ERR(svn_fs_copy(copy_root, nb->copyfrom_path,
                          rb->txn_root, nb->path, pool));

      if (pb->notify_func)
        {
          /* ### TODO: Use proper scratch pool instead of pb->notify_pool */
          svn_repos_notify_t *notify = svn_repos_notify_create(
                                            svn_repos_notify_load_copied_node,
                                            pb->notify_pool);

          pb->notify_func(pb->notify_baton, notify, pb->notify_pool);
          svn_pool_clear(pb->notify_pool);
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
uuid_record(const char *uuid,
            void *parse_baton,
            apr_pool_t *pool)
{
  struct parse_baton *pb = parse_baton;
  svn_revnum_t youngest_rev;

  if (pb->uuid_action == svn_repos_load_uuid_ignore)
    return SVN_NO_ERROR;

  if (pb->uuid_action != svn_repos_load_uuid_force)
    {
      SVN_ERR(svn_fs_youngest_rev(&youngest_rev, pb->fs, pool));
      if (youngest_rev != 0)
        return SVN_NO_ERROR;
    }

  return svn_fs_set_uuid(pb->fs, uuid, pool);
}

static svn_error_t *
new_node_record(void **node_baton,
                apr_hash_t *headers,
                void *revision_baton,
                apr_pool_t *pool)
{
  struct revision_baton *rb = revision_baton;
  struct parse_baton *pb = rb->pb;
  struct node_baton *nb;

  if (rb->rev == 0)
    return svn_error_create(SVN_ERR_STREAM_MALFORMED_DATA, NULL,
                            _("Malformed dumpstream: "
                              "Revision 0 must not contain node records"));

  SVN_ERR(make_node_baton(&nb, headers, rb, pool));

  /* If we're skipping this revision, we're done here. */
  if (rb->skipped)
    {
      *node_baton = nb;
      return SVN_NO_ERROR;
    }

  /* Make sure we have an action we recognize. */
  if (nb->action < svn_node_action_change
        || nb->action > svn_node_action_replace)
      return svn_error_createf(SVN_ERR_STREAM_UNRECOGNIZED_DATA, NULL,
                               _("Unrecognized node-action on node '%s'"),
                               nb->path);

  if (pb->notify_func)
    {
      /* ### TODO: Use proper scratch pool instead of pb->notify_pool */
      svn_repos_notify_t *notify = svn_repos_notify_create(
                                        svn_repos_notify_load_node_start,
                                        pb->notify_pool);

      notify->path = nb->path;
      pb->notify_func(pb->notify_baton, notify, pb->notify_pool);
      svn_pool_clear(pb->notify_pool);
    }

  switch (nb->action)
    {
    case svn_node_action_change:
      break;

    case svn_node_action_delete:
      SVN_ERR(svn_fs_delete(rb->txn_root, nb->path, pool));
      break;

    case svn_node_action_add:
      SVN_ERR(maybe_add_with_history(nb, rb, pool));
      break;

    case svn_node_action_replace:
      SVN_ERR(svn_fs_delete(rb->txn_root, nb->path, pool));
      SVN_ERR(maybe_add_with_history(nb, rb, pool));
      break;
    }

  *node_baton = nb;
  return SVN_NO_ERROR;
}

static svn_error_t *
set_revision_property(void *baton,
                      const char *name,
                      const svn_string_t *value)
{
  struct revision_baton *rb = baton;
  struct parse_baton *pb = rb->pb;
  svn_boolean_t is_date = strcmp(name, SVN_PROP_REVISION_DATE) == 0;
  svn_prop_t *prop;

  /* If we're skipping this revision, we're done here. */
  if (rb->skipped)
    return SVN_NO_ERROR;

  /* If we're ignoring dates, and this is one, we're done here. */
  if (is_date && pb->ignore_dates)
    return SVN_NO_ERROR;

  /* Collect property changes to apply them in one FS call in
     close_revision. */
  prop = &APR_ARRAY_PUSH(rb->revprops, svn_prop_t);
  prop->name = apr_pstrdup(rb->pool, name);
  prop->value = svn_string_dup(value, rb->pool);

  /* Remember any datestamp that passes through!  (See comment in
     close_revision() below.) */
  if (is_date)
    rb->datestamp = svn_string_dup(value, rb->pool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos__adjust_mergeinfo_property(svn_string_t **new_value_p,
                                     const svn_string_t *old_value,
                                     const char *parent_dir,
                                     apr_hash_t *rev_map,
                                     svn_revnum_t oldest_dumpstream_rev,
                                     apr_int32_t older_revs_offset,
                                     svn_repos_notify_func_t notify_func,
                                     void *notify_baton,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool)
{
  svn_string_t prop_val = *old_value;

  /* Tolerate mergeinfo with "\r\n" line endings because some
     dumpstream sources might contain as much.  If so normalize
     the line endings to '\n' and notify that we have made this
     correction. */
  if (strstr(prop_val.data, "\r"))
    {
      const char *prop_eol_normalized;

      SVN_ERR(svn_subst_translate_cstring2(prop_val.data,
                                           &prop_eol_normalized,
                                           "\n",  /* translate to LF */
                                           FALSE, /* no repair */
                                           NULL,  /* no keywords */
                                           FALSE, /* no expansion */
                                           result_pool));
      prop_val.data = prop_eol_normalized;
      prop_val.len = strlen(prop_eol_normalized);

      if (notify_func)
        {
          svn_repos_notify_t *notify
                  = svn_repos_notify_create(
                                svn_repos_notify_load_normalized_mergeinfo,
                                scratch_pool);

          notify_func(notify_baton, notify, scratch_pool);
        }
    }

  /* Renumber mergeinfo as appropriate. */
  SVN_ERR(renumber_mergeinfo_revs(new_value_p, &prop_val,
                                  rev_map, oldest_dumpstream_rev,
                                  older_revs_offset,
                                  result_pool));

  if (parent_dir)
    {
      /* Prefix the merge source paths with PARENT_DIR. */
      /* ASSUMPTION: All source paths are included in the dump stream. */
      SVN_ERR(prefix_mergeinfo_paths(new_value_p, *new_value_p,
                                     parent_dir, result_pool));
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
set_node_property(void *baton,
                  const char *name,
                  const svn_string_t *value)
{
  struct node_baton *nb = baton;
  struct revision_baton *rb = nb->rb;
  struct parse_baton *pb = rb->pb;

  /* If we're skipping this revision, we're done here. */
  if (rb->skipped)
    return SVN_NO_ERROR;

  /* Adjust mergeinfo. If this fails, presumably because the mergeinfo
     property has an ill-formed value, then we must not fail to load
     the repository (at least if it's a simple load with no revision
     offset adjustments, path changes, etc.) so just warn and leave it
     as it is. */
  if (strcmp(name, SVN_PROP_MERGEINFO) == 0)
    {
      svn_string_t *new_value;
      svn_error_t *err;

      err = svn_repos__adjust_mergeinfo_property(&new_value, value,
                                                 pb->parent_dir,
                                                 pb->rev_map,
                                                 pb->oldest_dumpstream_rev,
                                                 rb->rev_offset,
                                                 pb->notify_func, pb->notify_baton,
                                                 nb->pool, pb->notify_pool);
      svn_pool_clear(pb->notify_pool);
      if (err)
        {
          if (pb->validate_props)
            {
              return svn_error_quick_wrap(
                       err,
                       _("Invalid svn:mergeinfo value"));
            }
          if (pb->notify_func)
            {
              svn_repos_notify_t *notify
                = svn_repos_notify_create(svn_repos_notify_warning,
                                          pb->notify_pool);

              notify->warning = svn_repos_notify_warning_invalid_mergeinfo;
              notify->warning_str = _("Invalid svn:mergeinfo value; "
                                      "leaving unchanged");
              pb->notify_func(pb->notify_baton, notify, pb->notify_pool);
              svn_pool_clear(pb->notify_pool);
            }
          svn_error_clear(err);
        }
      else
        {
          value = new_value;
        }
    }

  return change_node_prop(rb->txn_root, nb->path, name, value,
                          pb->validate_props, nb->pool);
}


static svn_error_t *
delete_node_property(void *baton,
                     const char *name)
{
  struct node_baton *nb = baton;
  struct revision_baton *rb = nb->rb;

  /* If we're skipping this revision, we're done here. */
  if (rb->skipped)
    return SVN_NO_ERROR;

  return change_node_prop(rb->txn_root, nb->path, name, NULL,
                          rb->pb->validate_props, nb->pool);
}


static svn_error_t *
remove_node_props(void *baton)
{
  struct node_baton *nb = baton;
  struct revision_baton *rb = nb->rb;
  apr_hash_t *proplist;
  apr_hash_index_t *hi;

  /* If we're skipping this revision, we're done here. */
  if (rb->skipped)
    return SVN_NO_ERROR;

  SVN_ERR(svn_fs_node_proplist(&proplist,
                               rb->txn_root, nb->path, nb->pool));

  for (hi = apr_hash_first(nb->pool, proplist); hi; hi = apr_hash_next(hi))
    {
      const char *key = apr_hash_this_key(hi);

      SVN_ERR(change_node_prop(rb->txn_root, nb->path, key, NULL,
                               rb->pb->validate_props, nb->pool));
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta(svn_txdelta_window_handler_t *handler,
                void **handler_baton,
                void *node_baton)
{
  struct node_baton *nb = node_baton;
  struct revision_baton *rb = nb->rb;

  /* If we're skipping this revision, we're done here. */
  if (rb->skipped)
    {
      *handler = NULL;
      return SVN_NO_ERROR;
    }

  return svn_fs_apply_textdelta(handler, handler_baton,
                                rb->txn_root, nb->path,
                                svn_checksum_to_cstring(nb->base_checksum,
                                                        nb->pool),
                                svn_checksum_to_cstring(nb->result_checksum,
                                                        nb->pool),
                                nb->pool);
}


static svn_error_t *
set_fulltext(svn_stream_t **stream,
             void *node_baton)
{
  struct node_baton *nb = node_baton;
  struct revision_baton *rb = nb->rb;

  /* If we're skipping this revision, we're done here. */
  if (rb->skipped)
    {
      *stream = NULL;
      return SVN_NO_ERROR;
    }

  return svn_fs_apply_text(stream,
                           rb->txn_root, nb->path,
                           svn_checksum_to_cstring(nb->result_checksum,
                                                   nb->pool),
                           nb->pool);
}


static svn_error_t *
close_node(void *baton)
{
  struct node_baton *nb = baton;
  struct revision_baton *rb = nb->rb;
  struct parse_baton *pb = rb->pb;

  /* If we're skipping this revision, we're done here. */
  if (rb->skipped)
    return SVN_NO_ERROR;

  if (pb->notify_func)
    {
      /* ### TODO: Use proper scratch pool instead of pb->notify_pool */
      svn_repos_notify_t *notify = svn_repos_notify_create(
                                            svn_repos_notify_load_node_done,
                                            pb->notify_pool);

      pb->notify_func(pb->notify_baton, notify, pb->notify_pool);
      svn_pool_clear(pb->notify_pool);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
close_revision(void *baton)
{
  struct revision_baton *rb = baton;
  struct parse_baton *pb = rb->pb;
  const char *conflict_msg = NULL;
  svn_revnum_t committed_rev;
  svn_error_t *err;
  const char *txn_name = NULL;
  apr_hash_t *hooks_env;

  /* If we're skipping this revision we're done here. */
  if (rb->skipped)
    return SVN_NO_ERROR;

  if (rb->rev == 0)
    {
      /* Special case: set revision 0 properties when loading into an
         'empty' filesystem. */
      svn_revnum_t youngest_rev;

      SVN_ERR(svn_fs_youngest_rev(&youngest_rev, pb->fs, rb->pool));

      if (youngest_rev == 0)
        {
          apr_hash_t *orig_props;
          apr_hash_t *new_props;
          apr_array_header_t *diff;
          int i;

          SVN_ERR(svn_fs_revision_proplist2(&orig_props, pb->fs, 0, TRUE,
                                            rb->pool, rb->pool));
          new_props = svn_prop_array_to_hash(rb->revprops, rb->pool);
          SVN_ERR(svn_prop_diffs(&diff, new_props, orig_props, rb->pool));

          for (i = 0; i < diff->nelts; i++)
          {
              const svn_prop_t *prop = &APR_ARRAY_IDX(diff, i, svn_prop_t);

              SVN_ERR(change_rev_prop(pb->repos, 0, prop->name, prop->value,
                                      pb->validate_props, pb->normalize_props,
                                      rb->pool));
          }
        }

      return SVN_NO_ERROR;
    }

  /* If the dumpstream doesn't have an 'svn:date' property and we
     aren't ignoring the dates in the dumpstream altogether, remove
     any 'svn:date' revision property that was set by FS layer when
     the TXN was created.  */
  if (! (pb->ignore_dates || rb->datestamp))
    {
      svn_prop_t *prop = &APR_ARRAY_PUSH(rb->revprops, svn_prop_t);
      prop->name = SVN_PROP_REVISION_DATE;
      prop->value = NULL;
    }

  if (rb->pb->normalize_props)
    {
      apr_pool_t *iterpool;
      int i;

      iterpool = svn_pool_create(rb->pool);
      for (i = 0; i < rb->revprops->nelts; i++)
        {
          svn_prop_t *prop = &APR_ARRAY_IDX(rb->revprops, i, svn_prop_t);

          svn_pool_clear(iterpool);
          SVN_ERR(svn_repos__normalize_prop(&prop->value, NULL, prop->name,
                                            prop->value, rb->pool, iterpool));
        }
      svn_pool_destroy(iterpool);
    }

  /* Apply revision property changes. */
  if (rb->pb->validate_props)
    SVN_ERR(svn_repos_fs_change_txn_props(rb->txn, rb->revprops, rb->pool));
  else
    SVN_ERR(svn_fs_change_txn_props(rb->txn, rb->revprops, rb->pool));

  /* Get the txn name and hooks environment if they will be needed. */
  if (pb->use_pre_commit_hook || pb->use_post_commit_hook)
    {
      SVN_ERR(svn_repos__parse_hooks_env(&hooks_env, pb->repos->hooks_env_path,
                                         rb->pool, rb->pool));

      err = svn_fs_txn_name(&txn_name, rb->txn, rb->pool);
      if (err)
        {
          svn_error_clear(svn_fs_abort_txn(rb->txn, rb->pool));
          return svn_error_trace(err);
        }
    }

  /* Run the pre-commit hook, if so commanded. */
  if (pb->use_pre_commit_hook)
    {
      err = svn_repos__hooks_pre_commit(pb->repos, hooks_env,
                                        txn_name, rb->pool);
      if (err)
        {
          svn_error_clear(svn_fs_abort_txn(rb->txn, rb->pool));
          return svn_error_trace(err);
        }
    }

  /* Commit. */
  err = svn_fs_commit_txn(&conflict_msg, &committed_rev, rb->txn, rb->pool);
  if (SVN_IS_VALID_REVNUM(committed_rev))
    {
      if (err)
        {
          /* ### Log any error, but better yet is to rev
             ### close_revision()'s API to allow both committed_rev and err
             ### to be returned, see #3768. */
          svn_error_clear(err);
        }
    }
  else
    {
      svn_error_clear(svn_fs_abort_txn(rb->txn, rb->pool));
      if (conflict_msg)
        return svn_error_quick_wrap(err, conflict_msg);
      else
        return svn_error_trace(err);
    }

  /* Run post-commit hook, if so commanded.  */
  if (pb->use_post_commit_hook)
    {
      if ((err = svn_repos__hooks_post_commit(pb->repos, hooks_env,
                                              committed_rev, txn_name,
                                              rb->pool)))
        return svn_error_create
          (SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED, err,
           _("Commit succeeded, but post-commit hook failed"));
    }

  /* After a successful commit, must record the dump-rev -> in-repos-rev
     mapping, so that copyfrom instructions in the dump file can look up the
     correct repository revision to copy from. */
  set_revision_mapping(pb->rev_map, rb->rev, committed_rev);

  /* If the incoming dump stream has non-contiguous revisions (e.g. from
     using svndumpfilter --drop-empty-revs without --renumber-revs) then
     we must account for the missing gaps in PB->REV_MAP.  Otherwise we
     might not be able to map all mergeinfo source revisions to the correct
     revisions in the target repos. */
  if ((pb->last_rev_mapped != SVN_INVALID_REVNUM)
      && (rb->rev != pb->last_rev_mapped + 1))
    {
      svn_revnum_t i;

      for (i = pb->last_rev_mapped + 1; i < rb->rev; i++)
        {
          set_revision_mapping(pb->rev_map, i, pb->last_rev_mapped);
        }
    }

  /* Update our "last revision mapped". */
  pb->last_rev_mapped = rb->rev;

  /* Deltify the predecessors of paths changed in this revision. */
  SVN_ERR(svn_fs_deltify_revision(pb->fs, committed_rev, rb->pool));

  if (pb->notify_func)
    {
      /* ### TODO: Use proper scratch pool instead of pb->notify_pool */
      svn_repos_notify_t *notify = svn_repos_notify_create(
                                        svn_repos_notify_load_txn_committed,
                                        pb->notify_pool);

      notify->new_revision = committed_rev;
      notify->old_revision = ((committed_rev == rb->rev)
                                    ? SVN_INVALID_REVNUM
                                    : rb->rev);
      pb->notify_func(pb->notify_baton, notify, pb->notify_pool);
      svn_pool_clear(pb->notify_pool);
    }

  return SVN_NO_ERROR;
}


/*----------------------------------------------------------------------*/

/** The public routines **/


svn_error_t *
svn_repos_get_fs_build_parser6(const svn_repos_parse_fns3_t **callbacks,
                               void **parse_baton,
                               svn_repos_t *repos,
                               svn_revnum_t start_rev,
                               svn_revnum_t end_rev,
                               svn_boolean_t use_history,
                               svn_boolean_t validate_props,
                               enum svn_repos_load_uuid uuid_action,
                               const char *parent_dir,
                               svn_boolean_t use_pre_commit_hook,
                               svn_boolean_t use_post_commit_hook,
                               svn_boolean_t ignore_dates,
                               svn_boolean_t normalize_props,
                               svn_repos_notify_func_t notify_func,
                               void *notify_baton,
                               apr_pool_t *pool)
{
  svn_repos_parse_fns3_t *parser = apr_pcalloc(pool, sizeof(*parser));
  struct parse_baton *pb = apr_pcalloc(pool, sizeof(*pb));

  if (parent_dir)
    parent_dir = svn_relpath_canonicalize(parent_dir, pool);

  SVN_ERR_ASSERT((SVN_IS_VALID_REVNUM(start_rev) &&
                  SVN_IS_VALID_REVNUM(end_rev))
                 || ((! SVN_IS_VALID_REVNUM(start_rev)) &&
                     (! SVN_IS_VALID_REVNUM(end_rev))));
  if (SVN_IS_VALID_REVNUM(start_rev))
    SVN_ERR_ASSERT(start_rev <= end_rev);

  parser->magic_header_record = NULL;
  parser->uuid_record = uuid_record;
  parser->new_revision_record = new_revision_record;
  parser->new_node_record = new_node_record;
  parser->set_revision_property = set_revision_property;
  parser->set_node_property = set_node_property;
  parser->remove_node_props = remove_node_props;
  parser->set_fulltext = set_fulltext;
  parser->close_node = close_node;
  parser->close_revision = close_revision;
  parser->delete_node_property = delete_node_property;
  parser->apply_textdelta = apply_textdelta;

  pb->repos = repos;
  pb->fs = svn_repos_fs(repos);
  pb->use_history = use_history;
  pb->validate_props = validate_props;
  pb->notify_func = notify_func;
  pb->notify_baton = notify_baton;
  pb->uuid_action = uuid_action;
  pb->parent_dir = parent_dir;
  pb->pool = pool;
  pb->notify_pool = svn_pool_create(pool);
  pb->rev_map = apr_hash_make(pool);
  pb->oldest_dumpstream_rev = SVN_INVALID_REVNUM;
  pb->last_rev_mapped = SVN_INVALID_REVNUM;
  pb->start_rev = start_rev;
  pb->end_rev = end_rev;
  pb->use_pre_commit_hook = use_pre_commit_hook;
  pb->use_post_commit_hook = use_post_commit_hook;
  pb->ignore_dates = ignore_dates;
  pb->normalize_props = normalize_props;

  *callbacks = parser;
  *parse_baton = pb;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_load_fs6(svn_repos_t *repos,
                   svn_stream_t *dumpstream,
                   svn_revnum_t start_rev,
                   svn_revnum_t end_rev,
                   enum svn_repos_load_uuid uuid_action,
                   const char *parent_dir,
                   svn_boolean_t use_pre_commit_hook,
                   svn_boolean_t use_post_commit_hook,
                   svn_boolean_t validate_props,
                   svn_boolean_t ignore_dates,
                   svn_boolean_t normalize_props,
                   svn_repos_notify_func_t notify_func,
                   void *notify_baton,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *pool)
{
  const svn_repos_parse_fns3_t *parser;
  void *parse_baton;

  /* This is really simple. */

  SVN_ERR(svn_repos_get_fs_build_parser6(&parser, &parse_baton,
                                         repos,
                                         start_rev, end_rev,
                                         TRUE, /* look for copyfrom revs */
                                         validate_props,
                                         uuid_action,
                                         parent_dir,
                                         use_pre_commit_hook,
                                         use_post_commit_hook,
                                         ignore_dates,
                                         normalize_props,
                                         notify_func,
                                         notify_baton,
                                         pool));

  return svn_repos_parse_dumpstream3(dumpstream, parser, parse_baton, FALSE,
                                     cancel_func, cancel_baton, pool);
}

/*----------------------------------------------------------------------*/

/** The same functionality for revprops only **/

/* Implement svn_repos_parse_fns3_t.new_revision_record.
 *
 * Because the revision is supposed to already exist, we don't need to
 * start transactions etc. */
static svn_error_t *
revprops_new_revision_record(void **revision_baton,
                             apr_hash_t *headers,
                             void *parse_baton,
                             apr_pool_t *pool)
{
  struct parse_baton *pb = parse_baton;
  struct revision_baton *rb;

  rb = make_revision_baton(headers, pb, pool);

  /* If we're skipping this revision, try to notify someone. */
  if (rb->skipped && pb->notify_func)
    {
      /* ### TODO: Use proper scratch pool instead of pb->notify_pool */
      svn_repos_notify_t *notify = svn_repos_notify_create(
                                        svn_repos_notify_load_skipped_rev,
                                        pb->notify_pool);

      notify->old_revision = rb->rev;
      pb->notify_func(pb->notify_baton, notify, pb->notify_pool);
      svn_pool_clear(pb->notify_pool);
    }

  /* If we're parsing revision 0, only the revision props are (possibly)
     interesting to us: when loading the stream into an empty
     filesystem, then we want new filesystem's revision 0 to have the
     same props.  Otherwise, we just ignore revision 0 in the stream. */

  *revision_baton = rb;
  return SVN_NO_ERROR;
}

/* Implement svn_repos_parse_fns3_t.close_revision.
 *
 * Simply set the revprops we previously parsed and send notifications.
 * This is the place where we will detect missing revisions. */
static svn_error_t *
revprops_close_revision(void *baton)
{
  struct revision_baton *rb = baton;
  struct parse_baton *pb = rb->pb;
  apr_hash_t *orig_props;
  apr_hash_t *new_props;
  apr_array_header_t *diff;
  int i;

  /* If we're skipping this revision we're done here. */
  if (rb->skipped)
    return SVN_NO_ERROR;

  /* If the dumpstream doesn't have an 'svn:date' property and we
     aren't ignoring the dates in the dumpstream altogether, remove
     any 'svn:date' revision property that was set by FS layer when
     the TXN was created.  */
  if (! (pb->ignore_dates || rb->datestamp))
    {
      svn_prop_t *prop = &APR_ARRAY_PUSH(rb->revprops, svn_prop_t);
      prop->name = SVN_PROP_REVISION_DATE;
      prop->value = NULL;
    }

  SVN_ERR(svn_fs_revision_proplist2(&orig_props, pb->fs, rb->rev, TRUE,
                                    rb->pool, rb->pool));
  new_props = svn_prop_array_to_hash(rb->revprops, rb->pool);
  SVN_ERR(svn_prop_diffs(&diff, new_props, orig_props, rb->pool));

  for (i = 0; i < diff->nelts; i++)
    {
      const svn_prop_t *prop = &APR_ARRAY_IDX(diff, i, svn_prop_t);

      SVN_ERR(change_rev_prop(pb->repos, rb->rev, prop->name, prop->value,
                              pb->validate_props, pb->normalize_props,
                              rb->pool));
    }

  if (pb->notify_func)
    {
      /* ### TODO: Use proper scratch pool instead of pb->notify_pool */
      svn_repos_notify_t *notify = svn_repos_notify_create(
                                        svn_repos_notify_load_revprop_set,
                                        pb->notify_pool);

      notify->new_revision = rb->rev;
      notify->old_revision = SVN_INVALID_REVNUM;
      pb->notify_func(pb->notify_baton, notify, pb->notify_pool);
      svn_pool_clear(pb->notify_pool);
    }

  return SVN_NO_ERROR;
}

/* Set *CALLBACKS and *PARSE_BATON to a vtable parser which commits new
 * revisions to the fs in REPOS.  Allocate the objects in RESULT_POOL.
 *
 * START_REV and END_REV act as filters, the lower and upper (inclusive)
 * range values of revisions in DUMPSTREAM which will be loaded.  Either
 * both of these values are #SVN_INVALID_REVNUM (in  which case no
 * revision-based filtering occurs at all), or both are valid revisions
 * (where START_REV is older than or equivalent to END_REV).
 * 
 * START_REV and END_REV act as filters, the lower and upper (inclusive)
 * range values of revisions which will
 * be loaded.  Either both of these values are #SVN_INVALID_REVNUM (in
 * which case no revision-based filtering occurs at all), or both are
 * valid revisions (where START_REV is older than or equivalent to
 * END_REV).  They refer to dump stream revision numbers rather than
 * committed revision numbers.
 *
 * If VALIDATE_PROPS is set, then validate Subversion revision properties
 * (those in the svn: namespace) against established rules for those things.
 *
 * If IGNORE_DATES is set, ignore any revision datestamps found in
 * DUMPSTREAM, keeping whatever timestamps the revisions currently have.
 *
 * If NORMALIZE_PROPS is set, attempt to normalize invalid Subversion
 * revision and node properties (those in the svn: namespace) so that
 * their values would follow the established rules for them.  Currently,
 * this means translating non-LF line endings in the property values to LF.
 */
static svn_error_t *
build_revprop_parser(const svn_repos_parse_fns3_t **callbacks,
                     void **parse_baton,
                     svn_repos_t *repos,
                     svn_revnum_t start_rev,
                     svn_revnum_t end_rev,
                     svn_boolean_t validate_props,
                     svn_boolean_t ignore_dates,
                     svn_boolean_t normalize_props,
                     svn_repos_notify_func_t notify_func,
                     void *notify_baton,
                     apr_pool_t *result_pool)
{
  svn_repos_parse_fns3_t *parser = apr_pcalloc(result_pool, sizeof(*parser));
  struct parse_baton *pb = apr_pcalloc(result_pool, sizeof(*pb));

  SVN_ERR_ASSERT((SVN_IS_VALID_REVNUM(start_rev) &&
                  SVN_IS_VALID_REVNUM(end_rev))
                 || ((! SVN_IS_VALID_REVNUM(start_rev)) &&
                     (! SVN_IS_VALID_REVNUM(end_rev))));
  if (SVN_IS_VALID_REVNUM(start_rev))
    SVN_ERR_ASSERT(start_rev <= end_rev);

  parser->magic_header_record = NULL;
  parser->uuid_record = uuid_record;
  parser->new_revision_record = revprops_new_revision_record;
  parser->new_node_record = NULL;
  parser->set_revision_property = set_revision_property;
  parser->set_node_property = NULL;
  parser->remove_node_props = NULL;
  parser->set_fulltext = NULL;
  parser->close_node = NULL;
  parser->close_revision = revprops_close_revision;
  parser->delete_node_property = NULL;
  parser->apply_textdelta = NULL;

  pb->repos = repos;
  pb->fs = svn_repos_fs(repos);
  pb->use_history = FALSE;
  pb->validate_props = validate_props;
  pb->notify_func = notify_func;
  pb->notify_baton = notify_baton;
  pb->uuid_action = svn_repos_load_uuid_ignore; /* Never touch the UUID. */
  pb->parent_dir = NULL;
  pb->pool = result_pool;
  pb->notify_pool = svn_pool_create(result_pool);
  pb->rev_map = NULL;
  pb->oldest_dumpstream_rev = SVN_INVALID_REVNUM;
  pb->last_rev_mapped = SVN_INVALID_REVNUM;
  pb->start_rev = start_rev;
  pb->end_rev = end_rev;
  pb->use_pre_commit_hook = FALSE;
  pb->use_post_commit_hook = FALSE;
  pb->ignore_dates = ignore_dates;
  pb->normalize_props = normalize_props;

  *callbacks = parser;
  *parse_baton = pb;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_load_fs_revprops(svn_repos_t *repos,
                           svn_stream_t *dumpstream,
                           svn_revnum_t start_rev,
                           svn_revnum_t end_rev,
                           svn_boolean_t validate_props,
                           svn_boolean_t ignore_dates,
                           svn_boolean_t normalize_props,
                           svn_repos_notify_func_t notify_func,
                           void *notify_baton,
                           svn_cancel_func_t cancel_func,
                           void *cancel_baton,
                           apr_pool_t *scratch_pool)
{
  const svn_repos_parse_fns3_t *parser;
  void *parse_baton;

  /* This is really simple. */

  SVN_ERR(build_revprop_parser(&parser, &parse_baton,
                               repos,
                               start_rev, end_rev,
                               validate_props,
                               ignore_dates,
                               normalize_props,
                               notify_func,
                               notify_baton,
                               scratch_pool));

  return svn_repos_parse_dumpstream3(dumpstream, parser, parse_baton, FALSE,
                                     cancel_func, cancel_baton, scratch_pool);
}
