/* commit.c --- editor for committing changes to a filesystem.
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
#include <apr_file_io.h>

#include "svn_hash.h"
#include "svn_compat.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_checksum.h"
#include "svn_ctype.h"
#include "svn_props.h"
#include "svn_mergeinfo.h"
#include "svn_private_config.h"

#include "repos.h"

#include "private/svn_fspath.h"
#include "private/svn_fs_private.h"
#include "private/svn_repos_private.h"
#include "private/svn_editor.h"



/*** Editor batons. ***/

struct edit_baton
{
  apr_pool_t *pool;

  /** Supplied when the editor is created: **/

  /* Revision properties to set for this commit. */
  apr_hash_t *revprop_table;

  /* Callback to run when the commit is done. */
  svn_commit_callback2_t commit_callback;
  void *commit_callback_baton;

  /* Callback to check authorizations on paths. */
  svn_repos_authz_callback_t authz_callback;
  void *authz_baton;

  /* The already-open svn repository to commit to. */
  svn_repos_t *repos;

  /* URL to the root of the open repository. */
  const char *repos_url_decoded;

  /* The name of the repository (here for convenience). */
  const char *repos_name;

  /* The filesystem associated with the REPOS above (here for
     convenience). */
  svn_fs_t *fs;

  /* Location in fs where the edit will begin. */
  const char *base_path;

  /* Does this set of interfaces 'own' the commit transaction? */
  svn_boolean_t txn_owner;

  /* svn transaction associated with this edit (created in
     open_root, or supplied by the public API caller). */
  svn_fs_txn_t *txn;

  /** Filled in during open_root: **/

  /* The name of the transaction. */
  const char *txn_name;

  /* The object representing the root directory of the svn txn. */
  svn_fs_root_t *txn_root;

  /* Avoid aborting an fs transaction more than once */
  svn_boolean_t txn_aborted;

  /** Filled in when the edit is closed: **/

  /* The new revision created by this commit. */
  svn_revnum_t *new_rev;

  /* The date (according to the repository) of this commit. */
  const char **committed_date;

  /* The author (also according to the repository) of this commit. */
  const char **committed_author;
};


struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent;
  const char *path; /* the -absolute- path to this dir in the fs */
  svn_revnum_t base_rev;        /* the revision I'm based on  */
  svn_boolean_t was_copied; /* was this directory added with history? */
  apr_pool_t *pool; /* my personal pool, in which I am allocated. */
  svn_boolean_t checked_write; /* TRUE after successfull write check */
};


struct file_baton
{
  struct edit_baton *edit_baton;
  const char *path; /* the -absolute- path to this file in the fs */
  svn_boolean_t checked_write; /* TRUE after successfull write check */
};


struct ev2_baton
{
  /* The repository we are editing.  */
  svn_repos_t *repos;

  /* The authz baton for checks; NULL to skip authz.  */
  svn_authz_t *authz;

  /* The repository name and user for performing authz checks.  */
  const char *authz_repos_name;
  const char *authz_user;

  /* Callback to provide info about the committed revision.  */
  svn_commit_callback2_t commit_cb;
  void *commit_baton;

  /* The FS txn editor  */
  svn_editor_t *inner;

  /* The name of the open transaction (so we know what to commit)  */
  const char *txn_name;
};


/* Create and return a generic out-of-dateness error. */
static svn_error_t *
out_of_date(const char *path, svn_node_kind_t kind)
{
  return svn_error_createf(SVN_ERR_FS_TXN_OUT_OF_DATE, NULL,
                           (kind == svn_node_dir
                            ? _("Directory '%s' is out of date")
                            : kind == svn_node_file
                            ? _("File '%s' is out of date")
                            : _("'%s' is out of date")),
                           path);
}

/* Perform an out of date check for base_rev against created rev,
   and a sanity check of base_rev. */
static svn_error_t *
check_out_of_date(struct edit_baton *eb,
                  const char *path,
                  svn_node_kind_t kind,
                  svn_revnum_t base_rev,
                  svn_revnum_t created_rev)
{
  if (base_rev < created_rev)
    {
      return out_of_date(path, kind);
    }
  else if (base_rev > created_rev)
    {
      if (base_rev > svn_fs_txn_base_revision(eb->txn))
        return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL, 
                                 _("No such revision %ld"),
                                 base_rev);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
invoke_commit_cb(svn_commit_callback2_t commit_cb,
                 void *commit_baton,
                 svn_fs_t *fs,
                 svn_revnum_t revision,
                 const char *post_commit_errstr,
                 apr_pool_t *scratch_pool)
{
  /* FS interface returns non-const values.  */
  /* const */ svn_string_t *date;
  /* const */ svn_string_t *author;
  svn_commit_info_t *commit_info;
  apr_hash_t *revprops;

  if (commit_cb == NULL)
    return SVN_NO_ERROR;

  SVN_ERR(svn_fs_revision_proplist2(&revprops, fs, revision,
                                    TRUE, scratch_pool, scratch_pool));

  date = svn_hash_gets(revprops, SVN_PROP_REVISION_DATE);
  author = svn_hash_gets(revprops, SVN_PROP_REVISION_AUTHOR);

  commit_info = svn_create_commit_info(scratch_pool);

  /* fill up the svn_commit_info structure */
  commit_info->revision = revision;
  commit_info->date = date ? date->data : NULL;
  commit_info->author = author ? author->data : NULL;
  commit_info->post_commit_err = post_commit_errstr;
  /* commit_info->repos_root is not set by the repos layer, only by RA layers */

  return svn_error_trace(commit_cb(commit_info, commit_baton, scratch_pool));
}



/* If EDITOR_BATON contains a valid authz callback, verify that the
   REQUIRED access to PATH in ROOT is authorized.  Return an error
   appropriate for throwing out of the commit editor with SVN_ERR.  If
   no authz callback is present in EDITOR_BATON, then authorize all
   paths.  Use POOL for temporary allocation only. */
static svn_error_t *
check_authz(struct edit_baton *editor_baton, const char *path,
            svn_fs_root_t *root, svn_repos_authz_access_t required,
            apr_pool_t *pool)
{
  if (editor_baton->authz_callback)
    {
      svn_boolean_t allowed;

      SVN_ERR(editor_baton->authz_callback(required, &allowed, root, path,
                                           editor_baton->authz_baton, pool));
      if (!allowed)
        return svn_error_create(required & svn_authz_write ?
                                SVN_ERR_AUTHZ_UNWRITABLE :
                                SVN_ERR_AUTHZ_UNREADABLE,
                                NULL, "Access denied");
    }

  return SVN_NO_ERROR;
}


/* Return a directory baton allocated in POOL which represents
   FULL_PATH, which is the immediate directory child of the directory
   represented by PARENT_BATON.  EDIT_BATON is the commit editor
   baton.  WAS_COPIED reveals whether or not this directory is the
   result of a copy operation.  BASE_REVISION is the base revision of
   the directory. */
static struct dir_baton *
make_dir_baton(struct edit_baton *edit_baton,
               struct dir_baton *parent_baton,
               const char *full_path,
               svn_boolean_t was_copied,
               svn_revnum_t base_revision,
               apr_pool_t *pool)
{
  struct dir_baton *db;
  db = apr_pcalloc(pool, sizeof(*db));
  db->edit_baton = edit_baton;
  db->parent = parent_baton;
  db->pool = pool;
  db->path = full_path;
  db->was_copied = was_copied;
  db->base_rev = base_revision;
  return db;
}

/* This function is the shared guts of add_file() and add_directory(),
   which see for the meanings of the parameters.  The only extra
   parameter here is IS_DIR, which is TRUE when adding a directory,
   and FALSE when adding a file.

   COPY_PATH must be a full URL, not a relative path. */
static svn_error_t *
add_file_or_directory(const char *path,
                      void *parent_baton,
                      const char *copy_path,
                      svn_revnum_t copy_revision,
                      svn_boolean_t is_dir,
                      apr_pool_t *pool,
                      void **return_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_boolean_t was_copied = FALSE;
  const char *full_path;

  /* Reject paths which contain control characters (related to issue #4340). */
  SVN_ERR(svn_path_check_valid(path, pool));

  full_path = svn_fspath__join(eb->base_path,
                               svn_relpath_canonicalize(path, pool), pool);

  /* Sanity check. */
  if (copy_path && (! SVN_IS_VALID_REVNUM(copy_revision)))
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL,
       _("Got source path but no source revision for '%s'"), full_path);

  if (copy_path)
    {
      const char *fs_path;
      svn_fs_root_t *copy_root;
      svn_node_kind_t kind;
      svn_repos_authz_access_t required;

      /* Copy requires recursive write access to the destination path
         and write access to the parent path. */
      required = svn_authz_write | (is_dir ? svn_authz_recursive : 0);
      SVN_ERR(check_authz(eb, full_path, eb->txn_root,
                          required, subpool));
      SVN_ERR(check_authz(eb, pb->path, eb->txn_root,
                          svn_authz_write, subpool));

      /* Check PATH in our transaction.  Make sure it does not exist
         unless its parent directory was copied (in which case, the
         thing might have been copied in as well), else return an
         out-of-dateness error. */
      SVN_ERR(svn_fs_check_path(&kind, eb->txn_root, full_path, subpool));
      if ((kind != svn_node_none) && (! pb->was_copied))
        return svn_error_trace(out_of_date(full_path, kind));

      /* For now, require that the url come from the same repository
         that this commit is operating on. */
      copy_path = svn_path_uri_decode(copy_path, subpool);
      fs_path = svn_cstring_skip_prefix(copy_path, eb->repos_url_decoded);
      if (!fs_path)
        return svn_error_createf
          (SVN_ERR_FS_GENERAL, NULL,
           _("Source url '%s' is from different repository"), copy_path);

      /* Now use the "fs_path" as an absolute path within the
         repository to make the copy from. */
      SVN_ERR(svn_fs_revision_root(&copy_root, eb->fs,
                                   copy_revision, subpool));

      /* Copy also requires (recursive) read access to the source */
      required = svn_authz_read | (is_dir ? svn_authz_recursive : 0);
      SVN_ERR(check_authz(eb, fs_path, copy_root, required, subpool));

      SVN_ERR(svn_fs_copy(copy_root, fs_path,
                          eb->txn_root, full_path, subpool));
      was_copied = TRUE;
    }
  else
    {
      /* No ancestry given, just make a new directory or empty file.
         Note that we don't perform an existence check here like the
         copy-from case does -- that's because svn_fs_make_*()
         already errors out if the file already exists.  Verify write
         access to the full path and to the parent. */
      SVN_ERR(check_authz(eb, full_path, eb->txn_root,
                          svn_authz_write, subpool));
      SVN_ERR(check_authz(eb, pb->path, eb->txn_root,
                          svn_authz_write, subpool));
      if (is_dir)
        SVN_ERR(svn_fs_make_dir(eb->txn_root, full_path, subpool));
      else
        SVN_ERR(svn_fs_make_file(eb->txn_root, full_path, subpool));
    }

  /* Cleanup our temporary subpool. */
  svn_pool_destroy(subpool);

  /* Build a new child baton. */
  if (is_dir)
    {
      struct dir_baton *new_db = make_dir_baton(eb, pb, full_path, was_copied,
                                                SVN_INVALID_REVNUM, pool);

      new_db->checked_write = TRUE; /* Just created */
      *return_baton = new_db;
    }
  else
    {
      struct file_baton *new_fb = apr_pcalloc(pool, sizeof(*new_fb));
      new_fb->edit_baton = eb;
      new_fb->path = full_path;
      new_fb->checked_write = TRUE; /* Just created */
      *return_baton = new_fb;
    }

  return SVN_NO_ERROR;
}



/*** Editor functions ***/

static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  struct dir_baton *dirb;
  struct edit_baton *eb = edit_baton;
  svn_revnum_t youngest;

  /* We always build our transaction against HEAD.  However, we will
     sanity-check BASE_REVISION and keep it in our dir baton for out
     of dateness checks.  */
  SVN_ERR(svn_fs_youngest_rev(&youngest, eb->fs, eb->pool));

  if (base_revision > youngest)
    return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                             _("No such revision %ld (HEAD is %ld)"),
                             base_revision, youngest);

  /* Unless we've been instructed to use a specific transaction, we'll
     make our own. */
  if (eb->txn_owner)
    {
      SVN_ERR(svn_repos_fs_begin_txn_for_commit2(&(eb->txn),
                                                 eb->repos,
                                                 youngest,
                                                 eb->revprop_table,
                                                 eb->pool));
    }
  else /* Even if we aren't the owner of the transaction, we might
          have been instructed to set some properties. */
    {
      apr_array_header_t *props = svn_prop_hash_to_array(eb->revprop_table,
                                                         pool);
      SVN_ERR(svn_repos_fs_change_txn_props(eb->txn, props, pool));
    }
  SVN_ERR(svn_fs_txn_name(&(eb->txn_name), eb->txn, eb->pool));
  SVN_ERR(svn_fs_txn_root(&(eb->txn_root), eb->txn, eb->pool));

  /* Create a root dir baton.  The `base_path' field is an -absolute-
     path in the filesystem, upon which all further editor paths are
     based. */
  dirb = apr_pcalloc(pool, sizeof(*dirb));
  dirb->edit_baton = edit_baton;
  dirb->parent = NULL;
  dirb->pool = pool;
  dirb->was_copied = FALSE;
  dirb->path = apr_pstrdup(pool, eb->base_path);
  dirb->base_rev = base_revision;

  *root_baton = dirb;
  return SVN_NO_ERROR;
}



static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton *parent = parent_baton;
  struct edit_baton *eb = parent->edit_baton;
  svn_node_kind_t kind;
  svn_repos_authz_access_t required = svn_authz_write;
  const char *full_path;

  full_path = svn_fspath__join(eb->base_path,
                               svn_relpath_canonicalize(path, pool), pool);

  /* Check PATH in our transaction.  */
  SVN_ERR(svn_fs_check_path(&kind, eb->txn_root, full_path, pool));

  /* Deletion requires a recursive write access, as well as write
     access to the parent directory. */
  if (kind == svn_node_dir)
    required |= svn_authz_recursive;
  SVN_ERR(check_authz(eb, full_path, eb->txn_root,
                      required, pool));
  SVN_ERR(check_authz(eb, parent->path, eb->txn_root,
                      svn_authz_write, pool));

  /* If PATH doesn't exist in the txn, the working copy is out of date. */
  if (kind == svn_node_none)
    return svn_error_trace(out_of_date(full_path, kind));

  /* Now, make sure we're deleting the node we *think* we're
     deleting, else return an out-of-dateness error. */
  if (SVN_IS_VALID_REVNUM(revision))
    {
      svn_revnum_t cr_rev;

      SVN_ERR(svn_fs_node_created_rev(&cr_rev, eb->txn_root, full_path, pool));
      SVN_ERR(check_out_of_date(eb, full_path, kind, revision, cr_rev));
    }

  /* This routine is a mindless wrapper.  We call svn_fs_delete()
     because that will delete files and recursively delete
     directories.  */
  return svn_error_trace(svn_fs_delete(eb->txn_root, full_path, pool));
}


static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copy_path,
              svn_revnum_t copy_revision,
              apr_pool_t *pool,
              void **child_baton)
{
  return add_file_or_directory(path, parent_baton, copy_path, copy_revision,
                               TRUE /* is_dir */, pool, child_baton);
}


static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  svn_node_kind_t kind;
  const char *full_path;

  full_path = svn_fspath__join(eb->base_path,
                               svn_relpath_canonicalize(path, pool), pool);

  /* Check PATH in our transaction.  If it does not exist,
     return a 'Path not present' error. */
  SVN_ERR(svn_fs_check_path(&kind, eb->txn_root, full_path, pool));
  if (kind == svn_node_none)
    return svn_error_createf(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                             _("Path '%s' not present"),
                             path);

  /* Build a new dir baton for this directory. */
  *child_baton = make_dir_baton(eb, pb, full_path, pb->was_copied,
                                base_revision, pool);
  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;

  if (!fb->checked_write)
    {
      /* Check for write authorization. */
      SVN_ERR(check_authz(eb, fb->path, eb->txn_root,
                          svn_authz_write, pool));
      fb->checked_write = TRUE;
    }

  return svn_error_trace(
          svn_fs_apply_textdelta(handler, handler_baton,
                                 eb->txn_root,
                                 fb->path,
                                 base_checksum,
                                 NULL,
                                 pool));
}


static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copy_path,
         svn_revnum_t copy_revision,
         apr_pool_t *pool,
         void **file_baton)
{
  return add_file_or_directory(path, parent_baton, copy_path, copy_revision,
                               FALSE /* is_dir */, pool, file_baton);
}


static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct file_baton *new_fb;
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  svn_revnum_t cr_rev;
  apr_pool_t *subpool = svn_pool_create(pool);
  const char *full_path;

  full_path = svn_fspath__join(eb->base_path,
                               svn_relpath_canonicalize(path, pool), pool);

  /* Check for read authorization. */
  SVN_ERR(check_authz(eb, full_path, eb->txn_root,
                      svn_authz_read, subpool));

  /* Get this node's creation revision (doubles as an existence check). */
  SVN_ERR(svn_fs_node_created_rev(&cr_rev, eb->txn_root, full_path,
                                  subpool));

  /* If the node our caller has is an older revision number than the
     one in our transaction, return an out-of-dateness error. */
  if (SVN_IS_VALID_REVNUM(base_revision))
    SVN_ERR(check_out_of_date(eb, full_path, svn_node_file,
                              base_revision, cr_rev));

  /* Build a new file baton */
  new_fb = apr_pcalloc(pool, sizeof(*new_fb));
  new_fb->edit_baton = eb;
  new_fb->path = full_path;

  *file_baton = new_fb;

  /* Destory the work subpool. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;

  if (!fb->checked_write)
    {
      /* Check for write authorization. */
      SVN_ERR(check_authz(eb, fb->path, eb->txn_root,
                          svn_authz_write, pool));
      fb->checked_write = TRUE;
    }

  return svn_repos_fs_change_node_prop(eb->txn_root, fb->path,
                                       name, value, pool);
}


static svn_error_t *
close_file(void *file_baton,
           const char *text_digest,
           apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;

  if (text_digest)
    {
      svn_checksum_t *checksum;
      svn_checksum_t *text_checksum;

      SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_md5,
                                   fb->edit_baton->txn_root, fb->path,
                                   TRUE, pool));
      SVN_ERR(svn_checksum_parse_hex(&text_checksum, svn_checksum_md5,
                                     text_digest, pool));

      if (!svn_checksum_match(text_checksum, checksum))
        return svn_checksum_mismatch_err(text_checksum, checksum, pool,
                            _("Checksum mismatch for resulting fulltext\n(%s)"),
                            fb->path);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;

  /* Check for write authorization. */
  if (!db->checked_write)
    {
      SVN_ERR(check_authz(eb, db->path, eb->txn_root,
                          svn_authz_write, pool));

      if (SVN_IS_VALID_REVNUM(db->base_rev))
        {
          /* Subversion rule:  propchanges can only happen on a directory
             which is up-to-date. */
          svn_revnum_t created_rev;
          SVN_ERR(svn_fs_node_created_rev(&created_rev,
                                          eb->txn_root, db->path, pool));

          SVN_ERR(check_out_of_date(eb, db->path, svn_node_dir,
                                    db->base_rev, created_rev));
        }

      db->checked_write = TRUE; /* Skip on further prop changes */
    }

  return svn_repos_fs_change_node_prop(eb->txn_root, db->path,
                                       name, value, pool);
}

const char *
svn_repos__post_commit_error_str(svn_error_t *err,
                                 apr_pool_t *pool)
{
  svn_error_t *hook_err1, *hook_err2;
  const char *msg;

  if (! err)
    return _("(no error)");

  err = svn_error_purge_tracing(err);

  /* hook_err1 is the SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED wrapped
     error from the post-commit script, if any, and hook_err2 should
     be the original error, but be defensive and handle a case where
     SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED doesn't wrap an error. */
  hook_err1 = svn_error_find_cause(err, SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED);
  if (hook_err1 && hook_err1->child)
    hook_err2 = hook_err1->child;
  else
    hook_err2 = hook_err1;

  /* This implementation counts on svn_repos_fs_commit_txn() and
     libsvn_repos/commit.c:complete_cb() returning
     svn_fs_commit_txn() as the parent error with a child
     SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED error.  If the parent error
     is SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED then there was no error
     in svn_fs_commit_txn().

     The post-commit hook error message is already self describing, so
     it can be dropped into an error message without any additional
     text. */
  if (hook_err1)
    {
      if (err == hook_err1)
        {
          if (hook_err2->message)
            msg = apr_pstrdup(pool, hook_err2->message);
          else
            msg = _("post-commit hook failed with no error message.");
        }
      else
        {
          msg = hook_err2->message
                  ? apr_pstrdup(pool, hook_err2->message)
                  : _("post-commit hook failed with no error message.");
          msg = apr_psprintf(
                  pool,
                  _("post commit FS processing had error:\n%s\n%s"),
                  err->message ? err->message : _("(no error message)"),
                  msg);
        }
    }
  else
    {
      msg = apr_psprintf(pool,
                         _("post commit FS processing had error:\n%s"),
                         err->message ? err->message
                                      : _("(no error message)"));
    }

  return msg;
}

static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  svn_revnum_t new_revision = SVN_INVALID_REVNUM;
  svn_error_t *err;
  const char *conflict;
  const char *post_commit_err = NULL;

  /* If no transaction has been created (ie. if open_root wasn't
     called before close_edit), abort the operation here with an
     error. */
  if (! eb->txn)
    return svn_error_create(SVN_ERR_REPOS_BAD_ARGS, NULL,
                            "No valid transaction supplied to close_edit");

  /* Commit. */
  err = svn_repos_fs_commit_txn(&conflict, eb->repos,
                                &new_revision, eb->txn, pool);

  if (SVN_IS_VALID_REVNUM(new_revision))
    {
      /* The actual commit succeeded, i.e. the transaction does no longer
         exist and we can't use txn_root for conflict resolution etc.

         Since close_edit is supposed to release resources, do it now. */
      if (eb->txn_root)
        svn_fs_close_root(eb->txn_root);

      if (err)
        {
          /* If the error was in post-commit, then the commit itself
             succeeded.  In which case, save the post-commit warning
             (to be reported back to the client, who will probably
             display it as a warning) and clear the error. */
          post_commit_err = svn_repos__post_commit_error_str(err, pool);
          svn_error_clear(err);
        }

      /* Make sure a future abort doesn't perform
         any work. This may occur if the commit
         callback returns an error! */

      eb->txn = NULL;
      eb->txn_root = NULL;
    }
  else
    {
      /* ### todo: we should check whether it really was a conflict,
         and return the conflict info if so? */

      /* If the commit failed, it's *probably* due to a conflict --
         that is, the txn being out-of-date.  The filesystem gives us
         the ability to continue diddling the transaction and try
         again; but let's face it: that's not how the cvs or svn works
         from a user interface standpoint.  Thus we don't make use of
         this fs feature (for now, at least.)

         So, in a nutshell: svn commits are an all-or-nothing deal.
         Each commit creates a new fs txn which either succeeds or is
         aborted completely.  No second chances;  the user simply
         needs to update and commit again  :) */

      eb->txn_aborted = TRUE;

      return svn_error_trace(
                svn_error_compose_create(err,
                                         svn_fs_abort_txn(eb->txn, pool)));
    }

  /* At this point, the post-commit error has been converted to a string.
     That information will be passed to a callback, if provided. If the
     callback invocation fails in some way, that failure is returned here.
     IOW, the post-commit error information is low priority compared to
     other gunk here.  */

  /* Pass new revision information to the caller's callback. */
  return svn_error_trace(invoke_commit_cb(eb->commit_callback,
                                          eb->commit_callback_baton,
                                          eb->repos->fs,
                                          new_revision,
                                          post_commit_err,
                                          pool));
}


static svn_error_t *
abort_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  if ((! eb->txn) || (! eb->txn_owner) || eb->txn_aborted)
    return SVN_NO_ERROR;

  eb->txn_aborted = TRUE;

  /* Since abort_edit is supposed to release resources, do it now. */
  if (eb->txn_root)
    svn_fs_close_root(eb->txn_root);

  return svn_error_trace(svn_fs_abort_txn(eb->txn, pool));
}


static svn_error_t *
fetch_props_func(apr_hash_t **props,
                 void *baton,
                 const char *path,
                 svn_revnum_t base_revision,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  svn_fs_root_t *fs_root;
  svn_error_t *err;

  SVN_ERR(svn_fs_revision_root(&fs_root, eb->fs,
                               svn_fs_txn_base_revision(eb->txn),
                               scratch_pool));
  err = svn_fs_node_proplist(props, fs_root, path, result_pool);
  if (err && err->apr_err == SVN_ERR_FS_NOT_FOUND)
    {
      svn_error_clear(err);
      *props = apr_hash_make(result_pool);
      return SVN_NO_ERROR;
    }
  else if (err)
    return svn_error_trace(err);

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_kind_func(svn_node_kind_t *kind,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  svn_fs_root_t *fs_root;

  if (!SVN_IS_VALID_REVNUM(base_revision))
    base_revision = svn_fs_txn_base_revision(eb->txn);

  SVN_ERR(svn_fs_revision_root(&fs_root, eb->fs, base_revision, scratch_pool));

  SVN_ERR(svn_fs_check_path(kind, fs_root, path, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_base_func(const char **filename,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  svn_stream_t *contents;
  svn_stream_t *file_stream;
  const char *tmp_filename;
  svn_fs_root_t *fs_root;
  svn_error_t *err;

  if (!SVN_IS_VALID_REVNUM(base_revision))
    base_revision = svn_fs_txn_base_revision(eb->txn);

  SVN_ERR(svn_fs_revision_root(&fs_root, eb->fs, base_revision, scratch_pool));

  err = svn_fs_file_contents(&contents, fs_root, path, scratch_pool);
  if (err && err->apr_err == SVN_ERR_FS_NOT_FOUND)
    {
      svn_error_clear(err);
      *filename = NULL;
      return SVN_NO_ERROR;
    }
  else if (err)
    return svn_error_trace(err);
  SVN_ERR(svn_stream_open_unique(&file_stream, &tmp_filename, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 scratch_pool, scratch_pool));
  SVN_ERR(svn_stream_copy3(contents, file_stream, NULL, NULL, scratch_pool));

  *filename = apr_pstrdup(result_pool, tmp_filename);

  return SVN_NO_ERROR;
}



/*** Public interfaces. ***/

svn_error_t *
svn_repos_get_commit_editor5(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             svn_repos_t *repos,
                             svn_fs_txn_t *txn,
                             const char *repos_url_decoded,
                             const char *base_path,
                             apr_hash_t *revprop_table,
                             svn_commit_callback2_t commit_callback,
                             void *commit_baton,
                             svn_repos_authz_callback_t authz_callback,
                             void *authz_baton,
                             apr_pool_t *pool)
{
  svn_delta_editor_t *e;
  apr_pool_t *subpool = svn_pool_create(pool);
  struct edit_baton *eb;
  svn_delta_shim_callbacks_t *shim_callbacks =
                                    svn_delta_shim_callbacks_default(pool);
  const char *repos_url = svn_path_uri_encode(repos_url_decoded, pool);

  /* Do a global authz access lookup.  Users with no write access
     whatsoever to the repository don't get a commit editor. */
  if (authz_callback)
    {
      svn_boolean_t allowed;

      SVN_ERR(authz_callback(svn_authz_write, &allowed, NULL, NULL,
                             authz_baton, pool));
      if (!allowed)
        return svn_error_create(SVN_ERR_AUTHZ_UNWRITABLE, NULL,
                                "Not authorized to open a commit editor.");
    }

  /* Allocate the structures. */
  e = svn_delta_default_editor(pool);
  eb = apr_pcalloc(subpool, sizeof(*eb));

  /* Set up the editor. */
  e->open_root         = open_root;
  e->delete_entry      = delete_entry;
  e->add_directory     = add_directory;
  e->open_directory    = open_directory;
  e->change_dir_prop   = change_dir_prop;
  e->add_file          = add_file;
  e->open_file         = open_file;
  e->close_file        = close_file;
  e->apply_textdelta   = apply_textdelta;
  e->change_file_prop  = change_file_prop;
  e->close_edit        = close_edit;
  e->abort_edit        = abort_edit;

  /* Set up the edit baton. */
  eb->pool = subpool;
  eb->revprop_table = svn_prop_hash_dup(revprop_table, subpool);
  eb->commit_callback = commit_callback;
  eb->commit_callback_baton = commit_baton;
  eb->authz_callback = authz_callback;
  eb->authz_baton = authz_baton;
  eb->base_path = svn_fspath__canonicalize(base_path, subpool);
  eb->repos = repos;
  eb->repos_url_decoded = repos_url_decoded;
  eb->repos_name = svn_dirent_basename(svn_repos_path(repos, subpool),
                                       subpool);
  eb->fs = svn_repos_fs(repos);
  eb->txn = txn;
  eb->txn_owner = txn == NULL;

  *edit_baton = eb;
  *editor = e;

  shim_callbacks->fetch_props_func = fetch_props_func;
  shim_callbacks->fetch_kind_func = fetch_kind_func;
  shim_callbacks->fetch_base_func = fetch_base_func;
  shim_callbacks->fetch_baton = eb;

  SVN_ERR(svn_editor__insert_shims(editor, edit_baton, *editor, *edit_baton,
                                   repos_url, eb->base_path,
                                   shim_callbacks, pool, pool));

  return SVN_NO_ERROR;
}


#if 0
static svn_error_t *
ev2_check_authz(const struct ev2_baton *eb,
                const char *relpath,
                svn_repos_authz_access_t required,
                apr_pool_t *scratch_pool)
{
  const char *fspath;
  svn_boolean_t allowed;

  if (eb->authz == NULL)
    return SVN_NO_ERROR;

  if (relpath)
    fspath = apr_pstrcat(scratch_pool, "/", relpath, SVN_VA_NULL);
  else
    fspath = NULL;

  SVN_ERR(svn_repos_authz_check_access(eb->authz, eb->authz_repos_name, fspath,
                                       eb->authz_user, required,
                                       &allowed, scratch_pool));
  if (!allowed)
    return svn_error_create(required & svn_authz_write
                            ? SVN_ERR_AUTHZ_UNWRITABLE
                            : SVN_ERR_AUTHZ_UNREADABLE,
                            NULL, "Access denied");

  return SVN_NO_ERROR;
}
#endif


/* This implements svn_editor_cb_add_directory_t */
static svn_error_t *
add_directory_cb(void *baton,
                 const char *relpath,
                 const apr_array_header_t *children,
                 apr_hash_t *props,
                 svn_revnum_t replaces_rev,
                 apr_pool_t *scratch_pool)
{
  struct ev2_baton *eb = baton;

  SVN_ERR(svn_editor_add_directory(eb->inner, relpath, children, props,
                                   replaces_rev));
  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_add_file_t */
static svn_error_t *
add_file_cb(void *baton,
            const char *relpath,
            const svn_checksum_t *checksum,
            svn_stream_t *contents,
            apr_hash_t *props,
            svn_revnum_t replaces_rev,
            apr_pool_t *scratch_pool)
{
  struct ev2_baton *eb = baton;

  SVN_ERR(svn_editor_add_file(eb->inner, relpath, checksum, contents, props,
                              replaces_rev));
  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_add_symlink_t */
static svn_error_t *
add_symlink_cb(void *baton,
               const char *relpath,
               const char *target,
               apr_hash_t *props,
               svn_revnum_t replaces_rev,
               apr_pool_t *scratch_pool)
{
  struct ev2_baton *eb = baton;

  SVN_ERR(svn_editor_add_symlink(eb->inner, relpath, target, props,
                                 replaces_rev));
  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_add_absent_t */
static svn_error_t *
add_absent_cb(void *baton,
              const char *relpath,
              svn_node_kind_t kind,
              svn_revnum_t replaces_rev,
              apr_pool_t *scratch_pool)
{
  struct ev2_baton *eb = baton;

  SVN_ERR(svn_editor_add_absent(eb->inner, relpath, kind, replaces_rev));
  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_alter_directory_t */
static svn_error_t *
alter_directory_cb(void *baton,
                   const char *relpath,
                   svn_revnum_t revision,
                   const apr_array_header_t *children,
                   apr_hash_t *props,
                   apr_pool_t *scratch_pool)
{
  struct ev2_baton *eb = baton;

  SVN_ERR(svn_editor_alter_directory(eb->inner, relpath, revision,
                                     children, props));
  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_alter_file_t */
static svn_error_t *
alter_file_cb(void *baton,
              const char *relpath,
              svn_revnum_t revision,
              const svn_checksum_t *checksum,
              svn_stream_t *contents,
              apr_hash_t *props,
              apr_pool_t *scratch_pool)
{
  struct ev2_baton *eb = baton;

  SVN_ERR(svn_editor_alter_file(eb->inner, relpath, revision,
                                checksum, contents, props));
  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_alter_symlink_t */
static svn_error_t *
alter_symlink_cb(void *baton,
                 const char *relpath,
                 svn_revnum_t revision,
                 const char *target,
                 apr_hash_t *props,
                 apr_pool_t *scratch_pool)
{
  struct ev2_baton *eb = baton;

  SVN_ERR(svn_editor_alter_symlink(eb->inner, relpath, revision,
                                   target, props));
  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_delete_t */
static svn_error_t *
delete_cb(void *baton,
          const char *relpath,
          svn_revnum_t revision,
          apr_pool_t *scratch_pool)
{
  struct ev2_baton *eb = baton;

  SVN_ERR(svn_editor_delete(eb->inner, relpath, revision));
  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_copy_t */
static svn_error_t *
copy_cb(void *baton,
        const char *src_relpath,
        svn_revnum_t src_revision,
        const char *dst_relpath,
        svn_revnum_t replaces_rev,
        apr_pool_t *scratch_pool)
{
  struct ev2_baton *eb = baton;

  SVN_ERR(svn_editor_copy(eb->inner, src_relpath, src_revision, dst_relpath,
                          replaces_rev));
  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_move_t */
static svn_error_t *
move_cb(void *baton,
        const char *src_relpath,
        svn_revnum_t src_revision,
        const char *dst_relpath,
        svn_revnum_t replaces_rev,
        apr_pool_t *scratch_pool)
{
  struct ev2_baton *eb = baton;

  SVN_ERR(svn_editor_move(eb->inner, src_relpath, src_revision, dst_relpath,
                          replaces_rev));
  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_complete_t */
static svn_error_t *
complete_cb(void *baton,
            apr_pool_t *scratch_pool)
{
  struct ev2_baton *eb = baton;
  svn_revnum_t revision;
  svn_error_t *post_commit_err;
  const char *conflict_path;
  svn_error_t *err;
  const char *post_commit_errstr;
  apr_hash_t *hooks_env;

  /* Parse the hooks-env file (if any). */
  SVN_ERR(svn_repos__parse_hooks_env(&hooks_env, eb->repos->hooks_env_path,
                                     scratch_pool, scratch_pool));

  /* The transaction has been fully edited. Let the pre-commit hook
     have a look at the thing.  */
  SVN_ERR(svn_repos__hooks_pre_commit(eb->repos, hooks_env,
                                      eb->txn_name, scratch_pool));

  /* Hook is done. Let's do the actual commit.  */
  SVN_ERR(svn_fs__editor_commit(&revision, &post_commit_err, &conflict_path,
                                eb->inner, scratch_pool, scratch_pool));

  /* Did a conflict occur during the commit process?  */
  if (conflict_path != NULL)
    return svn_error_createf(SVN_ERR_FS_CONFLICT, NULL,
                             _("Conflict at '%s'"), conflict_path);

  /* Since did not receive an error during the commit process, and no
     conflict was specified... we committed a revision. Run the hooks.
     Other errors may have occurred within the FS (specified by the
     POST_COMMIT_ERR localvar), but we need to run the hooks.  */
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  err = svn_repos__hooks_post_commit(eb->repos, hooks_env, revision,
                                     eb->txn_name, scratch_pool);
  if (err)
    err = svn_error_create(SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED, err,
                           _("Commit succeeded, but post-commit hook failed"));

  /* Combine the FS errors with the hook errors, and stringify.  */
  err = svn_error_compose_create(post_commit_err, err);
  if (err)
    {
      post_commit_errstr = svn_repos__post_commit_error_str(err, scratch_pool);
      svn_error_clear(err);
    }
  else
    {
      post_commit_errstr = NULL;
    }

  return svn_error_trace(invoke_commit_cb(eb->commit_cb, eb->commit_baton,
                                          eb->repos->fs, revision,
                                          post_commit_errstr,
                                          scratch_pool));
}


/* This implements svn_editor_cb_abort_t */
static svn_error_t *
abort_cb(void *baton,
         apr_pool_t *scratch_pool)
{
  struct ev2_baton *eb = baton;

  SVN_ERR(svn_editor_abort(eb->inner));
  return SVN_NO_ERROR;
}


static svn_error_t *
apply_revprops(svn_fs_t *fs,
               const char *txn_name,
               apr_hash_t *revprops,
               apr_pool_t *scratch_pool)
{
  svn_fs_txn_t *txn;
  const apr_array_header_t *revprops_array;

  /* The FS editor has a TXN inside it, but we can't access it. Open another
     based on the TXN_NAME.  */
  SVN_ERR(svn_fs_open_txn(&txn, fs, txn_name, scratch_pool));

  /* Validate and apply the revision properties.  */
  revprops_array = svn_prop_hash_to_array(revprops, scratch_pool);
  SVN_ERR(svn_repos_fs_change_txn_props(txn, revprops_array, scratch_pool));

  /* ### do we need to force the txn to close, or is it enough to wait
     ### for the pool to be cleared?  */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos__get_commit_ev2(svn_editor_t **editor,
                          svn_repos_t *repos,
                          svn_authz_t *authz,
                          const char *authz_repos_name,
                          const char *authz_user,
                          apr_hash_t *revprops,
                          svn_commit_callback2_t commit_cb,
                          void *commit_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  static const svn_editor_cb_many_t editor_cbs = {
    add_directory_cb,
    add_file_cb,
    add_symlink_cb,
    add_absent_cb,
    alter_directory_cb,
    alter_file_cb,
    alter_symlink_cb,
    delete_cb,
    copy_cb,
    move_cb,
    complete_cb,
    abort_cb
  };
  struct ev2_baton *eb;
  const svn_string_t *author;
  apr_hash_t *hooks_env;

  /* Parse the hooks-env file (if any). */
  SVN_ERR(svn_repos__parse_hooks_env(&hooks_env, repos->hooks_env_path,
                                     scratch_pool, scratch_pool));

  /* Can the user modify the repository at all?  */
  /* ### check against AUTHZ.  */

  author = svn_hash_gets(revprops, SVN_PROP_REVISION_AUTHOR);

  eb = apr_palloc(result_pool, sizeof(*eb));
  eb->repos = repos;
  eb->authz = authz;
  eb->authz_repos_name = authz_repos_name;
  eb->authz_user = authz_user;
  eb->commit_cb = commit_cb;
  eb->commit_baton = commit_baton;

  SVN_ERR(svn_fs__editor_create(&eb->inner, &eb->txn_name,
                                repos->fs, SVN_FS_TXN_CHECK_LOCKS,
                                cancel_func, cancel_baton,
                                result_pool, scratch_pool));

  /* The TXN has been created. Go ahead and apply all revision properties.  */
  SVN_ERR(apply_revprops(repos->fs, eb->txn_name, revprops, scratch_pool));

  /* Okay... some access is allowed. Let's run the start-commit hook.  */
  SVN_ERR(svn_repos__hooks_start_commit(repos, hooks_env,
                                        author ? author->data : NULL,
                                        repos->client_capabilities,
                                        eb->txn_name, scratch_pool));

  /* Wrap the FS editor within our editor.  */
  SVN_ERR(svn_editor_create(editor, eb, cancel_func, cancel_baton,
                            result_pool, scratch_pool));
  SVN_ERR(svn_editor_setcb_many(*editor, &editor_cbs, scratch_pool));

  return SVN_NO_ERROR;
}
