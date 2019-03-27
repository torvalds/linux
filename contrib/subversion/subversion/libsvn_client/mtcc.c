/*
 * mtcc.c -- Multi Command Context implementation. This allows
 *           performing many operations without a working copy.
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
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_pools.h"
#include "svn_subst.h"

#include "private/svn_client_mtcc.h"


#include "svn_private_config.h"

#include "client.h"

#include <assert.h>

#define SVN_PATH_IS_EMPTY(s) ((s)[0] == '\0')

/* The kind of operation to perform in an mtcc_op_t */
typedef enum mtcc_kind_t
{
  OP_OPEN_DIR,
  OP_OPEN_FILE,
  OP_ADD_DIR,
  OP_ADD_FILE,
  OP_DELETE
} mtcc_kind_t;

typedef struct mtcc_op_t
{
  const char *name;                 /* basename of operation */
  mtcc_kind_t kind;                 /* editor operation */

  apr_array_header_t *children;     /* List of mtcc_op_t * */

  const char *src_relpath;              /* For ADD_DIR, ADD_FILE */
  svn_revnum_t src_rev;                 /* For ADD_DIR, ADD_FILE */
  svn_stream_t *src_stream;             /* For ADD_FILE, OPEN_FILE */
  svn_checksum_t *src_checksum;         /* For ADD_FILE, OPEN_FILE */
  svn_stream_t *base_stream;            /* For ADD_FILE, OPEN_FILE */
  const svn_checksum_t *base_checksum;  /* For ADD_FILE, OPEN_FILE */

  apr_array_header_t *prop_mods;        /* For all except DELETE
                                           List of svn_prop_t */

  svn_boolean_t performed_stat;         /* Verified kind with repository */
} mtcc_op_t;

/* Check if the mtcc doesn't contain any modifications yet */
#define MTCC_UNMODIFIED(mtcc)                                               \
    ((mtcc->root_op->kind == OP_OPEN_DIR                                    \
                            || mtcc->root_op->kind == OP_OPEN_FILE)         \
     && (mtcc->root_op->prop_mods == NULL                                   \
                            || !mtcc->root_op->prop_mods->nelts)            \
     && (mtcc->root_op->children == NULL                                    \
                            || !mtcc->root_op->children->nelts))

struct svn_client__mtcc_t
{
  apr_pool_t *pool;
  svn_revnum_t head_revision;
  svn_revnum_t base_revision;

  svn_ra_session_t *ra_session;
  svn_client_ctx_t *ctx;

  mtcc_op_t *root_op;
};

static mtcc_op_t *
mtcc_op_create(const char *name,
               svn_boolean_t add,
               svn_boolean_t directory,
               apr_pool_t *result_pool)
{
  mtcc_op_t *op;

  op = apr_pcalloc(result_pool, sizeof(*op));
  op->name = name ? apr_pstrdup(result_pool, name) : "";

  if (add)
    op->kind = directory ? OP_ADD_DIR : OP_ADD_FILE;
  else
    op->kind = directory ? OP_OPEN_DIR : OP_OPEN_FILE;

  if (directory)
    op->children = apr_array_make(result_pool, 4, sizeof(mtcc_op_t *));

  op->src_rev = SVN_INVALID_REVNUM;

  return op;
}

static svn_error_t *
mtcc_op_find(mtcc_op_t **op,
             svn_boolean_t *created,
             const char *relpath,
             mtcc_op_t *base_op,
             svn_boolean_t find_existing,
             svn_boolean_t find_deletes,
             svn_boolean_t create_file,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  const char *name;
  const char *child;
  int i;

  assert(svn_relpath_is_canonical(relpath));
  if (created)
    *created = FALSE;

  if (SVN_PATH_IS_EMPTY(relpath))
    {
      if (find_existing)
        *op = base_op;
      else
        *op = NULL;

      return SVN_NO_ERROR;
    }

  child = strchr(relpath, '/');

  if (child)
    {
      name = apr_pstrmemdup(scratch_pool, relpath, (child-relpath));
      child++; /* Skip '/' */
    }
  else
    name = relpath;

  if (!base_op->children)
    {
      if (!created)
        {
          *op = NULL;
           return SVN_NO_ERROR;
        }
      else
        return svn_error_createf(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                                 _("Can't operate on '%s' because '%s' is not a "
                                   "directory"),
                                 name, base_op->name);
    }

  for (i = base_op->children->nelts-1; i >= 0 ; i--)
    {
      mtcc_op_t *cop;

      cop = APR_ARRAY_IDX(base_op->children, i, mtcc_op_t *);

      if (! strcmp(cop->name, name)
          && (find_deletes || cop->kind != OP_DELETE))
        {
          return svn_error_trace(
                        mtcc_op_find(op, created, child ? child : "", cop,
                                     find_existing, find_deletes, create_file,
                                     result_pool, scratch_pool));
        }
    }

  if (!created)
    {
      *op = NULL;
      return SVN_NO_ERROR;
    }

  {
    mtcc_op_t *cop;

    cop = mtcc_op_create(name, FALSE, child || !create_file, result_pool);

    APR_ARRAY_PUSH(base_op->children, mtcc_op_t *) = cop;

    if (!child)
      {
        *op = cop;
        *created = TRUE;
        return SVN_NO_ERROR;
      }

    return svn_error_trace(
                mtcc_op_find(op, created, child, cop, find_existing,
                             find_deletes, create_file,
                             result_pool, scratch_pool));
  }
}

/* Gets the original repository location of RELPATH, checking things
   like copies, moves, etc.  */
static svn_error_t *
get_origin(svn_boolean_t *done,
           const char **origin_relpath,
           svn_revnum_t *rev,
           mtcc_op_t *op,
           const char *relpath,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool)
{
  const char *child;
  const char *name;
  if (SVN_PATH_IS_EMPTY(relpath))
    {
      if (op->kind == OP_ADD_DIR || op->kind == OP_ADD_FILE)
        *done = TRUE;
      *origin_relpath = op->src_relpath
                                ? apr_pstrdup(result_pool, op->src_relpath)
                                : NULL;
      *rev = op->src_rev;
      return SVN_NO_ERROR;
    }

  child = strchr(relpath, '/');
  if (child)
    {
      name = apr_pstrmemdup(scratch_pool, relpath, child-relpath);
      child++; /* Skip '/' */
    }
  else
    name = relpath;

  if (op->children && op->children->nelts)
    {
      int i;

      for (i = op->children->nelts-1; i >= 0; i--)
        {
           mtcc_op_t *cop;

           cop = APR_ARRAY_IDX(op->children, i, mtcc_op_t *);

           if (! strcmp(cop->name, name))
            {
              if (cop->kind == OP_DELETE)
                {
                  *done = TRUE;
                  return SVN_NO_ERROR;
                }

              SVN_ERR(get_origin(done, origin_relpath, rev,
                                 cop, child ? child : "",
                                 result_pool, scratch_pool));

              if (*origin_relpath || *done)
                return SVN_NO_ERROR;

              break;
            }
        }
    }

  if (op->kind == OP_ADD_DIR || op->kind == OP_ADD_FILE)
    {
      *done = TRUE;
      if (op->src_relpath)
        {
          *origin_relpath = svn_relpath_join(op->src_relpath, relpath,
                                             result_pool);
          *rev = op->src_rev;
        }
    }

  return SVN_NO_ERROR;
}

/* Obtains the original repository location for an mtcc relpath as
   *ORIGIN_RELPATH @ *REV, if it has one. If it has not and IGNORE_ENOENT
   is TRUE report *ORIGIN_RELPATH as NULL, otherwise return an error */
static svn_error_t *
mtcc_get_origin(const char **origin_relpath,
                svn_revnum_t *rev,
                const char *relpath,
                svn_boolean_t ignore_enoent,
                svn_client__mtcc_t *mtcc,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  svn_boolean_t done = FALSE;

  *origin_relpath = NULL;
  *rev = SVN_INVALID_REVNUM;

  SVN_ERR(get_origin(&done, origin_relpath, rev, mtcc->root_op, relpath,
                     result_pool, scratch_pool));

  if (!*origin_relpath && !done)
    {
      *origin_relpath = apr_pstrdup(result_pool, relpath);
      *rev = mtcc->base_revision;
    }
  else if (!ignore_enoent)
    {
      return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                               _("No origin found for node at '%s'"),
                               relpath);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__mtcc_create(svn_client__mtcc_t **mtcc,
                        const char *anchor_url,
                        svn_revnum_t base_revision,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  apr_pool_t *mtcc_pool;

  mtcc_pool = svn_pool_create(result_pool);

  *mtcc = apr_pcalloc(mtcc_pool, sizeof(**mtcc));
  (*mtcc)->pool = mtcc_pool;

  (*mtcc)->root_op = mtcc_op_create(NULL, FALSE, TRUE, mtcc_pool);

  (*mtcc)->ctx = ctx;

  SVN_ERR(svn_client_open_ra_session2(&(*mtcc)->ra_session, anchor_url,
                                      NULL /* wri_abspath */, ctx,
                                      mtcc_pool, scratch_pool));

  SVN_ERR(svn_ra_get_latest_revnum((*mtcc)->ra_session, &(*mtcc)->head_revision,
                                   scratch_pool));

  if (SVN_IS_VALID_REVNUM(base_revision))
    (*mtcc)->base_revision = base_revision;
  else
    (*mtcc)->base_revision = (*mtcc)->head_revision;

  if ((*mtcc)->base_revision > (*mtcc)->head_revision)
    return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                             _("No such revision %ld (HEAD is %ld)"),
                             base_revision, (*mtcc)->head_revision);

  return SVN_NO_ERROR;
}

static svn_error_t *
update_copy_src(mtcc_op_t *op,
                const char *add_relpath,
                apr_pool_t *result_pool)
{
  int i;

  if (op->src_relpath)
    op->src_relpath = svn_relpath_join(add_relpath, op->src_relpath,
                                       result_pool);

  if (!op->children)
    return SVN_NO_ERROR;

  for (i = 0; i < op->children->nelts; i++)
    {
      mtcc_op_t *cop;

      cop = APR_ARRAY_IDX(op->children, i, mtcc_op_t *);

      SVN_ERR(update_copy_src(cop, add_relpath, result_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
mtcc_reparent(const char *new_anchor_url,
              svn_client__mtcc_t *mtcc,
              apr_pool_t *scratch_pool)
{
  const char *session_url;
  const char *up;

  SVN_ERR(svn_ra_get_session_url(mtcc->ra_session, &session_url,
                                 scratch_pool));

  up = svn_uri_skip_ancestor(new_anchor_url, session_url, scratch_pool);

  if (! up)
    {
      return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                               _("'%s' is not an ancestor of  '%s'"),
                               new_anchor_url, session_url);
    }
  else if (!*up)
    {
      return SVN_NO_ERROR; /* Same url */
    }

  /* Update copy origins recursively...:( */
  SVN_ERR(update_copy_src(mtcc->root_op, up, mtcc->pool));

  SVN_ERR(svn_ra_reparent(mtcc->ra_session, new_anchor_url, scratch_pool));

  /* Create directory open operations for new ancestors */
  while (*up)
    {
      mtcc_op_t *root_op;

      mtcc->root_op->name = svn_relpath_basename(up, mtcc->pool);
      up = svn_relpath_dirname(up, scratch_pool);

      root_op = mtcc_op_create(NULL, FALSE, TRUE, mtcc->pool);

      APR_ARRAY_PUSH(root_op->children, mtcc_op_t *) = mtcc->root_op;

      mtcc->root_op = root_op;
    }

  return SVN_NO_ERROR;
}

/* Check if it is safe to create a new node at NEW_RELPATH. Return a proper
   error if it is not */
static svn_error_t *
mtcc_verify_create(svn_client__mtcc_t *mtcc,
                   const char *new_relpath,
                   apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;

  if (*new_relpath || !MTCC_UNMODIFIED(mtcc))
    {
      mtcc_op_t *op;

      SVN_ERR(mtcc_op_find(&op, NULL, new_relpath, mtcc->root_op, TRUE, FALSE,
                           FALSE, mtcc->pool, scratch_pool));

      if (op)
        return svn_error_createf(SVN_ERR_FS_ALREADY_EXISTS, NULL,
                                 _("Path '%s' already exists"),
                                 new_relpath);

      SVN_ERR(mtcc_op_find(&op, NULL, new_relpath, mtcc->root_op, TRUE, TRUE,
                           FALSE, mtcc->pool, scratch_pool));

      if (op)
        return SVN_NO_ERROR; /* Node is explicitly deleted. We can replace */
    }

  /* mod_dav_svn used to allow overwriting existing directories. Let's hide
     that for users of this api */
  SVN_ERR(svn_client__mtcc_check_path(&kind, new_relpath, FALSE,
                                      mtcc, scratch_pool));

  if (kind != svn_node_none)
    return svn_error_createf(SVN_ERR_FS_ALREADY_EXISTS, NULL,
                             _("Path '%s' already exists"),
                             new_relpath);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__mtcc_add_add_file(const char *relpath,
                              svn_stream_t *src_stream,
                              const svn_checksum_t *src_checksum,
                              svn_client__mtcc_t *mtcc,
                              apr_pool_t *scratch_pool)
{
  mtcc_op_t *op;
  svn_boolean_t created;
  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath) && src_stream);

  SVN_ERR(mtcc_verify_create(mtcc, relpath, scratch_pool));

  if (SVN_PATH_IS_EMPTY(relpath) && MTCC_UNMODIFIED(mtcc))
    {
      /* Turn the root operation into a file addition */
      op = mtcc->root_op;
    }
  else
    {
      SVN_ERR(mtcc_op_find(&op, &created, relpath, mtcc->root_op, FALSE, FALSE,
                           TRUE, mtcc->pool, scratch_pool));

      if (!op || !created)
        {
          return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                   _("Can't add file at '%s'"),
                                   relpath);
        }
    }

  op->kind = OP_ADD_FILE;
  op->src_stream = src_stream;
  op->src_checksum = src_checksum ? svn_checksum_dup(src_checksum, mtcc->pool)
                                  : NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__mtcc_add_copy(const char *src_relpath,
                          svn_revnum_t revision,
                          const char *dst_relpath,
                          svn_client__mtcc_t *mtcc,
                          apr_pool_t *scratch_pool)
{
  mtcc_op_t *op;
  svn_boolean_t created;
  svn_node_kind_t kind;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(src_relpath)
                 && svn_relpath_is_canonical(dst_relpath));

  if (! SVN_IS_VALID_REVNUM(revision))
    revision = mtcc->head_revision;
  else if (revision > mtcc->head_revision)
    {
      return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                               _("No such revision %ld"), revision);
    }

  SVN_ERR(mtcc_verify_create(mtcc, dst_relpath, scratch_pool));

  /* Subversion requires the kind of a copy */
  SVN_ERR(svn_ra_check_path(mtcc->ra_session, src_relpath, revision, &kind,
                            scratch_pool));

  if (kind != svn_node_dir && kind != svn_node_file)
    {
      return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                               _("Path '%s' not found in revision %ld"),
                               src_relpath, revision);
    }

  SVN_ERR(mtcc_op_find(&op, &created, dst_relpath, mtcc->root_op, FALSE, FALSE,
                       (kind == svn_node_file), mtcc->pool, scratch_pool));

  if (!op || !created)
    {
      return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                               _("Can't add node at '%s'"),
                               dst_relpath);
    }

  op->kind = (kind == svn_node_file) ? OP_ADD_FILE : OP_ADD_DIR;
  op->src_relpath = apr_pstrdup(mtcc->pool, src_relpath);
  op->src_rev = revision;

  return SVN_NO_ERROR;
}

/* Check if this operation contains at least one change that is not a
   plain delete */
static svn_boolean_t
mtcc_op_contains_non_delete(const mtcc_op_t *op)
{
  if (op->kind != OP_OPEN_DIR && op->kind != OP_OPEN_FILE
      && op->kind != OP_DELETE)
    {
      return TRUE;
    }

  if (op->prop_mods && op->prop_mods->nelts)
    return TRUE;

  if (op->src_stream)
    return TRUE;

  if (op->children)
    {
      int i;

      for (i = 0; i < op->children->nelts; i++)
        {
          const mtcc_op_t *c_op = APR_ARRAY_IDX(op->children, i,
                                                const mtcc_op_t *);

          if (mtcc_op_contains_non_delete(c_op))
            return TRUE;
        }
    }
  return FALSE;
}

static svn_error_t *
mtcc_add_delete(const char *relpath,
                svn_boolean_t for_move,
                svn_client__mtcc_t *mtcc,                
                apr_pool_t *scratch_pool)
{
  mtcc_op_t *op;
  svn_boolean_t created;
  svn_node_kind_t kind;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath));

  SVN_ERR(svn_client__mtcc_check_path(&kind, relpath, FALSE,
                                      mtcc, scratch_pool));

  if (kind == svn_node_none)
    return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                             _("Can't delete node at '%s' as it "
                                "does not exist"),
                             relpath);

  if (SVN_PATH_IS_EMPTY(relpath) && MTCC_UNMODIFIED(mtcc))
    {
      /* Turn root operation into delete */
      op = mtcc->root_op;
    }
  else
    {
      SVN_ERR(mtcc_op_find(&op, &created, relpath, mtcc->root_op, FALSE, TRUE,
                           TRUE, mtcc->pool, scratch_pool));

      if (!for_move && !op && !created)
        {
          /* Allow deleting directories, that are unmodified except for
              one or more deleted descendants */
          
          SVN_ERR(mtcc_op_find(&op, &created, relpath, mtcc->root_op, TRUE,
                  FALSE, FALSE, mtcc->pool, scratch_pool));

          if (op && mtcc_op_contains_non_delete(op))
            op = NULL;
          else
            created = TRUE;
        }

      if (!op || !created)
        {
          return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                   _("Can't delete node at '%s'"),
                                   relpath);
        }
    }

  op->kind = OP_DELETE;
  op->children = NULL;
  op->prop_mods = NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__mtcc_add_delete(const char *relpath,
                            svn_client__mtcc_t *mtcc,
                            apr_pool_t *scratch_pool)
{
  return svn_error_trace(mtcc_add_delete(relpath, FALSE, mtcc, scratch_pool));
}

svn_error_t *
svn_client__mtcc_add_mkdir(const char *relpath,
                           svn_client__mtcc_t *mtcc,
                           apr_pool_t *scratch_pool)
{
  mtcc_op_t *op;
  svn_boolean_t created;
  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath));

  SVN_ERR(mtcc_verify_create(mtcc, relpath, scratch_pool));

  if (SVN_PATH_IS_EMPTY(relpath) && MTCC_UNMODIFIED(mtcc))
    {
      /* Turn the root of the operation in an MKDIR */
      mtcc->root_op->kind = OP_ADD_DIR;

      return SVN_NO_ERROR;
    }

  SVN_ERR(mtcc_op_find(&op, &created, relpath, mtcc->root_op, FALSE, FALSE,
                       FALSE, mtcc->pool, scratch_pool));

  if (!op || !created)
    {
      return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                               _("Can't create directory at '%s'"),
                               relpath);
    }

  op->kind = OP_ADD_DIR;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__mtcc_add_move(const char *src_relpath,
                          const char *dst_relpath,
                          svn_client__mtcc_t *mtcc,
                          apr_pool_t *scratch_pool)
{
  const char *origin_relpath;
  svn_revnum_t origin_rev;

  SVN_ERR(mtcc_get_origin(&origin_relpath, &origin_rev,
                          src_relpath, FALSE, mtcc,
                          scratch_pool, scratch_pool));

  SVN_ERR(svn_client__mtcc_add_copy(src_relpath, mtcc->base_revision,
                                    dst_relpath, mtcc, scratch_pool));
  SVN_ERR(mtcc_add_delete(src_relpath, TRUE, mtcc, scratch_pool));

  return SVN_NO_ERROR;
}

/* Baton for mtcc_prop_getter */
struct mtcc_prop_get_baton
{
  svn_client__mtcc_t *mtcc;
  const char *relpath;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
};

/* Implements svn_wc_canonicalize_svn_prop_get_file_t */
static svn_error_t *
mtcc_prop_getter(const svn_string_t **mime_type,
                 svn_stream_t *stream,
                 void *baton,
                 apr_pool_t *pool)
{
  struct mtcc_prop_get_baton *mpgb = baton;
  const char *origin_relpath;
  svn_revnum_t origin_rev;
  apr_hash_t *props = NULL;

  mtcc_op_t *op;

  if (mime_type)
    *mime_type = NULL;

  /* Check if we have the information locally */
  SVN_ERR(mtcc_op_find(&op, NULL, mpgb->relpath, mpgb->mtcc->root_op, TRUE,
                       FALSE, FALSE, pool, pool));

  if (op)
    {
      if (mime_type)
        {
          int i;

          for (i = 0; op->prop_mods && i < op->prop_mods->nelts; i++)
            {
              const svn_prop_t *mod = &APR_ARRAY_IDX(op->prop_mods, i,
                                                     svn_prop_t);

              if (! strcmp(mod->name, SVN_PROP_MIME_TYPE))
                {
                  *mime_type = svn_string_dup(mod->value, pool);
                  mime_type = NULL;
                  break;
                }
            }
        }

      if (stream && op->src_stream)
        {
          svn_stream_mark_t *mark;
          svn_error_t *err;

          /* Is the source stream capable of being read multiple times? */
          err = svn_stream_mark(op->src_stream, &mark, pool);

          if (err && err->apr_err != SVN_ERR_STREAM_SEEK_NOT_SUPPORTED)
            return svn_error_trace(err);
          svn_error_clear(err);

          if (!err)
            {
              err = svn_stream_copy3(svn_stream_disown(op->src_stream, pool),
                                     svn_stream_disown(stream, pool),
                                     mpgb->cancel_func, mpgb->cancel_baton,
                                     pool);

              SVN_ERR(svn_error_compose_create(
                            err,
                            svn_stream_seek(op->src_stream, mark)));
            }
          /* else: ### Create tempfile? */

          stream = NULL; /* Stream is handled */
        }
    }

  if (!stream && !mime_type)
    return SVN_NO_ERROR;

  SVN_ERR(mtcc_get_origin(&origin_relpath, &origin_rev, mpgb->relpath, TRUE,
                          mpgb->mtcc, pool, pool));

  if (!origin_relpath)
    return SVN_NO_ERROR; /* Nothing to fetch at repository */

  SVN_ERR(svn_ra_get_file(mpgb->mtcc->ra_session, origin_relpath, origin_rev,
                          stream, NULL, mime_type ? &props : NULL, pool));

  if (mime_type && props)
    *mime_type = svn_hash_gets(props, SVN_PROP_MIME_TYPE);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__mtcc_add_propset(const char *relpath,
                             const char *propname,
                             const svn_string_t *propval,
                             svn_boolean_t skip_checks,
                             svn_client__mtcc_t *mtcc,
                             apr_pool_t *scratch_pool)
{
  mtcc_op_t *op;
  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath));

  if (! svn_prop_name_is_valid(propname))
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("Bad property name: '%s'"), propname);

  if (svn_prop_is_known_svn_rev_prop(propname))
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("Revision property '%s' not allowed "
                               "in this context"), propname);

  if (svn_property_kind2(propname) == svn_prop_wc_kind)
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("'%s' is a wcprop, thus not accessible "
                               "to clients"), propname);

  if (!skip_checks && svn_prop_needs_translation(propname))
    {
      svn_string_t *translated_value;
      SVN_ERR_W(svn_subst_translate_string2(&translated_value, NULL,
                                            NULL, propval,
                                            NULL, FALSE,
                                            scratch_pool, scratch_pool),
                _("Error normalizing property value"));

      propval = translated_value;
    }

  if (propval && svn_prop_is_svn_prop(propname))
    {
      struct mtcc_prop_get_baton mpbg;
      svn_node_kind_t kind;
      SVN_ERR(svn_client__mtcc_check_path(&kind, relpath, FALSE, mtcc,
                                          scratch_pool));

      mpbg.mtcc = mtcc;
      mpbg.relpath = relpath;
      mpbg.cancel_func = mtcc->ctx->cancel_func;
      mpbg.cancel_baton = mtcc->ctx->cancel_baton;

      SVN_ERR(svn_wc_canonicalize_svn_prop(&propval, propname, propval,
                                           relpath, kind, skip_checks,
                                           mtcc_prop_getter, &mpbg,
                                           scratch_pool));
    }

  if (SVN_PATH_IS_EMPTY(relpath) && MTCC_UNMODIFIED(mtcc))
    {
      svn_node_kind_t kind;

      /* Probing the node for an unmodified root will fix the node type to
         a file if necessary */

      SVN_ERR(svn_client__mtcc_check_path(&kind, relpath, FALSE,
                                          mtcc, scratch_pool));

      if (kind == svn_node_none)
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("Can't set properties at not existing '%s'"),
                                   relpath);

      op = mtcc->root_op;
    }
  else
    {
      SVN_ERR(mtcc_op_find(&op, NULL, relpath, mtcc->root_op, TRUE, FALSE,
                           FALSE, mtcc->pool, scratch_pool));

      if (!op)
        {
          svn_node_kind_t kind;
          svn_boolean_t created;

          /* ### TODO: Check if this node is within a newly copied directory,
                       and update origin values accordingly */

          SVN_ERR(svn_client__mtcc_check_path(&kind, relpath, FALSE,
                                              mtcc, scratch_pool));

          if (kind == svn_node_none)
            return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                     _("Can't set properties at not existing '%s'"),
                                     relpath);

          SVN_ERR(mtcc_op_find(&op, &created, relpath, mtcc->root_op, TRUE, FALSE,
                               (kind != svn_node_dir),
                               mtcc->pool, scratch_pool));

          SVN_ERR_ASSERT(op != NULL);
        }
    }

  if (!op->prop_mods)
      op->prop_mods = apr_array_make(mtcc->pool, 4, sizeof(svn_prop_t));

  {
    svn_prop_t propchange;
    propchange.name = apr_pstrdup(mtcc->pool, propname);

    if (propval)
      propchange.value = svn_string_dup(propval, mtcc->pool);
    else
      propchange.value = NULL;

    APR_ARRAY_PUSH(op->prop_mods, svn_prop_t) = propchange;
  }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__mtcc_add_update_file(const char *relpath,
                                 svn_stream_t *src_stream,
                                 const svn_checksum_t *src_checksum,
                                 svn_stream_t *base_stream,
                                 const svn_checksum_t *base_checksum,
                                 svn_client__mtcc_t *mtcc,
                                 apr_pool_t *scratch_pool)
{
  mtcc_op_t *op;
  svn_boolean_t created;
  svn_node_kind_t kind;
  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath) && src_stream);

  SVN_ERR(svn_client__mtcc_check_path(&kind, relpath, FALSE,
                                      mtcc, scratch_pool));

  if (kind != svn_node_file)
    return svn_error_createf(SVN_ERR_FS_NOT_FILE, NULL,
                             _("Can't update '%s' because it is not a file"),
                             relpath);

  SVN_ERR(mtcc_op_find(&op, &created, relpath, mtcc->root_op, TRUE, FALSE,
                       TRUE, mtcc->pool, scratch_pool));

  if (!op
      || (op->kind != OP_OPEN_FILE && op->kind != OP_ADD_FILE)
      || (op->src_stream != NULL))
    {
      return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                               _("Can't update file at '%s'"), relpath);
    }

  op->src_stream = src_stream;
  op->src_checksum = src_checksum ? svn_checksum_dup(src_checksum, mtcc->pool)
                                  : NULL;

  op->base_stream = base_stream;
  op->base_checksum = base_checksum ? svn_checksum_dup(base_checksum,
                                                       mtcc->pool)
                                    : NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__mtcc_check_path(svn_node_kind_t *kind,
                            const char *relpath,
                            svn_boolean_t check_repository,
                            svn_client__mtcc_t *mtcc,
                            apr_pool_t *scratch_pool)
{
  const char *origin_relpath;
  svn_revnum_t origin_rev;
  mtcc_op_t *op;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath));

  if (SVN_PATH_IS_EMPTY(relpath) && MTCC_UNMODIFIED(mtcc)
      && !mtcc->root_op->performed_stat)
    {
      /* We know nothing about the root. Perhaps it is a file? */
      SVN_ERR(svn_ra_check_path(mtcc->ra_session, "", mtcc->base_revision,
                                kind, scratch_pool));

      mtcc->root_op->performed_stat = TRUE;
      if (*kind == svn_node_file)
        {
          mtcc->root_op->kind = OP_OPEN_FILE;
          mtcc->root_op->children = NULL;
        }
      return SVN_NO_ERROR;
    }

  SVN_ERR(mtcc_op_find(&op, NULL, relpath, mtcc->root_op, TRUE, FALSE,
                       FALSE, mtcc->pool, scratch_pool));

  if (!op || (check_repository && !op->performed_stat))
    {
      SVN_ERR(mtcc_get_origin(&origin_relpath, &origin_rev,
                              relpath, TRUE, mtcc,
                              scratch_pool, scratch_pool));

      if (!origin_relpath)
        *kind = svn_node_none;
      else
        SVN_ERR(svn_ra_check_path(mtcc->ra_session, origin_relpath,
                                  origin_rev, kind, scratch_pool));

      if (op && *kind == svn_node_dir)
        {
          if (op->kind == OP_OPEN_DIR || op->kind == OP_ADD_DIR)
            op->performed_stat = TRUE;
          else if (op->kind == OP_OPEN_FILE || op->kind == OP_ADD_FILE)
            return svn_error_createf(SVN_ERR_FS_NOT_FILE, NULL,
                                     _("Can't perform file operation "
                                       "on '%s' as it is not a file"),
                                     relpath);
        }
      else if (op && *kind == svn_node_file)
        {
          if (op->kind == OP_OPEN_FILE || op->kind == OP_ADD_FILE)
            op->performed_stat = TRUE;
          else if (op->kind == OP_OPEN_DIR || op->kind == OP_ADD_DIR)
            return svn_error_createf(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                                     _("Can't perform directory operation "
                                       "on '%s' as it is not a directory"),
                                     relpath);
        }
      else if (op && (op->kind == OP_OPEN_DIR || op->kind == OP_OPEN_FILE))
        {
          return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                   _("Can't open '%s' as it does not exist"),
                                   relpath);
        }

      return SVN_NO_ERROR;
    }

  /* op != NULL */
  if (op->kind == OP_OPEN_DIR || op->kind == OP_ADD_DIR)
    {
      *kind = svn_node_dir;
      return SVN_NO_ERROR;
    }
  else if (op->kind == OP_OPEN_FILE || op->kind == OP_ADD_FILE)
    {
      *kind = svn_node_file;
      return SVN_NO_ERROR;
    }
  SVN_ERR_MALFUNCTION(); /* No other kinds defined as delete is filtered */
}

static svn_error_t *
commit_properties(const svn_delta_editor_t *editor,
                  const mtcc_op_t *op,
                  void *node_baton,
                  apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool;

  if (!op->prop_mods || op->prop_mods->nelts == 0)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < op->prop_mods->nelts; i++)
    {
      const svn_prop_t *mod = &APR_ARRAY_IDX(op->prop_mods, i, svn_prop_t);

      svn_pool_clear(iterpool);

      if (op->kind == OP_ADD_DIR || op->kind == OP_OPEN_DIR)
        SVN_ERR(editor->change_dir_prop(node_baton, mod->name, mod->value,
                                        iterpool));
      else if (op->kind == OP_ADD_FILE || op->kind == OP_OPEN_FILE)
        SVN_ERR(editor->change_file_prop(node_baton, mod->name, mod->value,
                                         iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Handles updating a file to a delta editor and then closes it */
static svn_error_t *
commit_file(const svn_delta_editor_t *editor,
            mtcc_op_t *op,
            void *file_baton,
            const char *session_url,
            const char *relpath,
            svn_client_ctx_t *ctx,
            apr_pool_t *scratch_pool)
{
  const char *text_checksum = NULL;
  svn_checksum_t *src_checksum = op->src_checksum;
  SVN_ERR(commit_properties(editor, op, file_baton, scratch_pool));

  if (op->src_stream)
    {
      const char *base_checksum = NULL;
      apr_pool_t *txdelta_pool = scratch_pool;
      svn_txdelta_window_handler_t window_handler;
      void *handler_baton;
      svn_stream_t *src_stream = op->src_stream;

      if (op->base_checksum && op->base_checksum->kind == svn_checksum_md5)
        base_checksum = svn_checksum_to_cstring(op->base_checksum, scratch_pool);

      /* ### TODO: Future enhancement: Allocate in special pool and send
                   files after the true edit operation, like a wc commit */
      SVN_ERR(editor->apply_textdelta(file_baton, base_checksum, txdelta_pool,
                                      &window_handler, &handler_baton));

      if (ctx->notify_func2)
        {
          svn_wc_notify_t *notify;

          notify = svn_wc_create_notify_url(
                            svn_path_url_add_component2(session_url, relpath,
                                                        scratch_pool),
                            svn_wc_notify_commit_postfix_txdelta,
                            scratch_pool);

          notify->path = relpath;
          notify->kind = svn_node_file;

          ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
        }

      if (window_handler != svn_delta_noop_window_handler)
        {
          if (!src_checksum || src_checksum->kind != svn_checksum_md5)
            src_stream = svn_stream_checksummed2(src_stream, &src_checksum, NULL,
                                                 svn_checksum_md5,
                                                 TRUE, scratch_pool);

          if (!op->base_stream)
            SVN_ERR(svn_txdelta_send_stream(src_stream,
                                            window_handler, handler_baton, NULL,
                                            scratch_pool));
          else
            SVN_ERR(svn_txdelta_run(op->base_stream, src_stream,
                                    window_handler, handler_baton,
                                    svn_checksum_md5, NULL,
                                    ctx->cancel_func, ctx->cancel_baton,
                                    scratch_pool, scratch_pool));
        }

      SVN_ERR(svn_stream_close(src_stream));
      if (op->base_stream)
        SVN_ERR(svn_stream_close(op->base_stream));
    }

  if (src_checksum && src_checksum->kind == svn_checksum_md5)
    text_checksum = svn_checksum_to_cstring(src_checksum, scratch_pool);

  return svn_error_trace(editor->close_file(file_baton, text_checksum,
                                            scratch_pool));
}

/* Handles updating a directory to a delta editor and then closes it */
static svn_error_t *
commit_directory(const svn_delta_editor_t *editor,
                 mtcc_op_t *op,
                 const char *relpath,
                 svn_revnum_t base_rev,
                 void *dir_baton,
                 const char *session_url,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *scratch_pool)
{
  SVN_ERR(commit_properties(editor, op, dir_baton, scratch_pool));

  if (op->children && op->children->nelts > 0)
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      int i;

      for (i = 0; i < op->children->nelts; i++)
        {
          mtcc_op_t *cop;
          const char * child_relpath;
          void *child_baton;

          cop = APR_ARRAY_IDX(op->children, i, mtcc_op_t *);

          svn_pool_clear(iterpool);

          if (ctx->cancel_func)
            SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

          child_relpath = svn_relpath_join(relpath, cop->name, iterpool);

          switch (cop->kind)
            {
              case OP_DELETE:
                SVN_ERR(editor->delete_entry(child_relpath, base_rev,
                                             dir_baton, iterpool));
                break;

              case OP_ADD_DIR:
                SVN_ERR(editor->add_directory(child_relpath, dir_baton,
                                              cop->src_relpath
                                                ? svn_path_url_add_component2(
                                                              session_url,
                                                              cop->src_relpath,
                                                              iterpool)
                                                : NULL,
                                              cop->src_rev,
                                              iterpool, &child_baton));
                SVN_ERR(commit_directory(editor, cop, child_relpath,
                                         SVN_INVALID_REVNUM, child_baton,
                                         session_url, ctx, iterpool));
                break;
              case OP_OPEN_DIR:
                SVN_ERR(editor->open_directory(child_relpath, dir_baton,
                                               base_rev, iterpool, &child_baton));
                SVN_ERR(commit_directory(editor, cop, child_relpath,
                                         base_rev, child_baton,
                                         session_url, ctx, iterpool));
                break;

              case OP_ADD_FILE:
                SVN_ERR(editor->add_file(child_relpath, dir_baton,
                                         cop->src_relpath
                                            ? svn_path_url_add_component2(
                                                            session_url,
                                                            cop->src_relpath,
                                                            iterpool)
                                            : NULL,
                                         cop->src_rev,
                                         iterpool, &child_baton));
                SVN_ERR(commit_file(editor, cop, child_baton,
                                    session_url, child_relpath, ctx, iterpool));
                break;
              case OP_OPEN_FILE:
                SVN_ERR(editor->open_file(child_relpath, dir_baton, base_rev,
                                          iterpool, &child_baton));
                SVN_ERR(commit_file(editor, cop, child_baton,
                                    session_url, child_relpath, ctx, iterpool));
                break;

              default:
                SVN_ERR_MALFUNCTION();
            }
        }
    }

  return svn_error_trace(editor->close_directory(dir_baton, scratch_pool));
}


/* Helper function to recursively create svn_client_commit_item3_t items
   to provide to the log message callback */
static svn_error_t *
add_commit_items(mtcc_op_t *op,
                 const char *session_url,
                 const char *url,
                 apr_array_header_t *commit_items,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  if ((op->kind != OP_OPEN_DIR && op->kind != OP_OPEN_FILE)
      || (op->prop_mods && op->prop_mods->nelts)
      || (op->src_stream))
    {
      svn_client_commit_item3_t *item;

      item = svn_client_commit_item3_create(result_pool);

      item->path = NULL;
      if (op->kind == OP_OPEN_DIR || op->kind == OP_ADD_DIR)
        item->kind = svn_node_dir;
      else if (op->kind == OP_OPEN_FILE || op->kind == OP_ADD_FILE)
        item->kind = svn_node_file;
      else
        item->kind = svn_node_unknown;

      item->url = apr_pstrdup(result_pool, url);
      item->session_relpath = svn_uri_skip_ancestor(session_url, item->url,
                                                    result_pool);

      if (op->src_relpath)
        {
          item->copyfrom_url = svn_path_url_add_component2(session_url,
                                                           op->src_relpath,
                                                           result_pool);
          item->copyfrom_rev = op->src_rev;
          item->state_flags |= SVN_CLIENT_COMMIT_ITEM_IS_COPY;
        }
      else
        item->copyfrom_rev = SVN_INVALID_REVNUM;

      if (op->kind == OP_ADD_DIR || op->kind == OP_ADD_FILE)
        item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
      else if (op->kind == OP_DELETE)
        item->state_flags = SVN_CLIENT_COMMIT_ITEM_DELETE;
      /* else item->state_flags = 0; */

      if (op->prop_mods && op->prop_mods->nelts)
        item->state_flags |= SVN_CLIENT_COMMIT_ITEM_PROP_MODS;

      if (op->src_stream)
        item->state_flags |= SVN_CLIENT_COMMIT_ITEM_TEXT_MODS;

      APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;
    }

  if (op->children && op->children->nelts)
    {
      int i;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);

      for (i = 0; i < op->children->nelts; i++)
        {
          mtcc_op_t *cop;
          const char * child_url;

          cop = APR_ARRAY_IDX(op->children, i, mtcc_op_t *);

          svn_pool_clear(iterpool);

          child_url = svn_path_url_add_component2(url, cop->name, iterpool);

          SVN_ERR(add_commit_items(cop, session_url, child_url, commit_items,
                                   result_pool, iterpool));
        }

      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__mtcc_commit(apr_hash_t *revprop_table,
                        svn_commit_callback2_t commit_callback,
                        void *commit_baton,
                        svn_client__mtcc_t *mtcc,
                        apr_pool_t *scratch_pool)
{
  const svn_delta_editor_t *editor;
  void *edit_baton;
  void *root_baton;
  apr_hash_t *commit_revprops;
  svn_node_kind_t kind;
  svn_error_t *err;
  const char *session_url;
  const char *log_msg;

  if (MTCC_UNMODIFIED(mtcc))
    {
      /* No changes -> no revision. Easy out */
      svn_pool_destroy(mtcc->pool);
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_ra_get_session_url(mtcc->ra_session, &session_url, scratch_pool));

  if (mtcc->root_op->kind != OP_OPEN_DIR)
    {
      const char *name;

      svn_uri_split(&session_url, &name, session_url, scratch_pool);

      if (*name)
        {
          SVN_ERR(mtcc_reparent(session_url, mtcc, scratch_pool));

          SVN_ERR(svn_ra_reparent(mtcc->ra_session, session_url, scratch_pool));
        }
    }

    /* Create new commit items and add them to the array. */
  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(mtcc->ctx))
    {
      svn_client_commit_item3_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items
                = apr_array_make(scratch_pool, 32, sizeof(item));

      SVN_ERR(add_commit_items(mtcc->root_op, session_url, session_url,
                               commit_items, scratch_pool, scratch_pool));

      SVN_ERR(svn_client__get_log_msg(&log_msg, &tmp_file, commit_items,
                                      mtcc->ctx, scratch_pool));

      if (! log_msg)
        return SVN_NO_ERROR;
    }
  else
    log_msg = "";

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           log_msg, mtcc->ctx, scratch_pool));

  /* Ugly corner case: The ra session might have died while we were waiting
     for the callback */

  err = svn_ra_check_path(mtcc->ra_session, "", mtcc->base_revision, &kind,
                          scratch_pool);

  if (err)
    {
      svn_error_t *err2 = svn_client_open_ra_session2(&mtcc->ra_session,
                                                      session_url,
                                                      NULL, mtcc->ctx,
                                                      mtcc->pool,
                                                      scratch_pool);

      if (err2)
        {
          svn_pool_destroy(mtcc->pool);
          return svn_error_trace(svn_error_compose_create(err, err2));
        }
      svn_error_clear(err);

      SVN_ERR(svn_ra_check_path(mtcc->ra_session, "",
                                mtcc->base_revision, &kind, scratch_pool));
    }

  if (kind != svn_node_dir)
    return svn_error_createf(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                             _("Can't commit to '%s' because it "
                               "is not a directory"),
                             session_url);

  /* Beware that the editor object must not live longer than the MTCC.
     Otherwise, txn objects etc. in EDITOR may live longer than their
     respective FS objects.  So, we can't use SCRATCH_POOL here. */
  SVN_ERR(svn_ra_get_commit_editor3(mtcc->ra_session, &editor, &edit_baton,
                                    commit_revprops,
                                    commit_callback, commit_baton,
                                    NULL /* lock_tokens */,
                                    FALSE /* keep_locks */,
                                    mtcc->pool));

  err = editor->open_root(edit_baton, mtcc->base_revision, scratch_pool, &root_baton);

  if (!err)
    err = commit_directory(editor, mtcc->root_op, "", mtcc->base_revision,
                           root_baton, session_url, mtcc->ctx, scratch_pool);

  if (!err)
    {
      if (mtcc->ctx->notify_func2)
        {
          svn_wc_notify_t *notify;
          notify = svn_wc_create_notify_url(session_url,
                                            svn_wc_notify_commit_finalizing,
                                            scratch_pool);
          mtcc->ctx->notify_func2(mtcc->ctx->notify_baton2, notify,
                                  scratch_pool);
        }
      SVN_ERR(editor->close_edit(edit_baton, scratch_pool));
    }
  else
    err = svn_error_compose_create(err,
                                   editor->abort_edit(edit_baton, scratch_pool));

  svn_pool_destroy(mtcc->pool);

  return svn_error_trace(err);
}
