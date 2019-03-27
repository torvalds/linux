/*
 * stat.c :  file and directory stat and read functions
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



#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <serf.h>

#include "svn_private_config.h"
#include "svn_pools.h"
#include "svn_xml.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_config.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_time.h"
#include "svn_version.h"

#include "private/svn_dav_protocol.h"
#include "private/svn_dep_compat.h"
#include "private/svn_fspath.h"

#include "ra_serf.h"



/* Implements svn_ra__vtable_t.check_path(). */
svn_error_t *
svn_ra_serf__check_path(svn_ra_session_t *ra_session,
                        const char *relpath,
                        svn_revnum_t revision,
                        svn_node_kind_t *kind,
                        apr_pool_t *scratch_pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_t *props;
  svn_error_t *err;
  const char *url;

  url = session->session_url.path;

  /* If we have a relative path, append it. */
  if (relpath)
    url = svn_path_url_add_component2(url, relpath, scratch_pool);

  /* If we were given a specific revision, get a URL that refers to that
     specific revision (rather than floating with HEAD).  */
  if (SVN_IS_VALID_REVNUM(revision))
    {
      SVN_ERR(svn_ra_serf__get_stable_url(&url, NULL /* latest_revnum */,
                                          session,
                                          url, revision,
                                          scratch_pool, scratch_pool));
    }

  /* URL is stable, so we use SVN_INVALID_REVNUM since it is now irrelevant.
     Or we started with SVN_INVALID_REVNUM and URL may be floating.  */
  err = svn_ra_serf__fetch_node_props(&props, session,
                                      url, SVN_INVALID_REVNUM,
                                      check_path_props,
                                      scratch_pool, scratch_pool);

  if (err && err->apr_err == SVN_ERR_FS_NOT_FOUND)
    {
      svn_error_clear(err);
      *kind = svn_node_none;
    }
  else
    {
      apr_hash_t *dav_props;
      const char *res_type;

      /* Any other error, raise to caller. */
      SVN_ERR(err);

      dav_props = apr_hash_get(props, "DAV:", 4);
      res_type = svn_prop_get_value(dav_props, "resourcetype");
      if (!res_type)
        {
          /* How did this happen? */
          return svn_error_create(SVN_ERR_RA_DAV_PROPS_NOT_FOUND, NULL,
                                 _("The PROPFIND response did not include the "
                                   "requested resourcetype value"));
        }

      if (strcmp(res_type, "collection") == 0)
        *kind = svn_node_dir;
      else
        *kind = svn_node_file;
    }

  return SVN_NO_ERROR;
}


/* Baton for fill_dirent_propfunc() */
struct fill_dirent_baton_t
{
  /* Update the fields in this entry.  */
  svn_dirent_t *entry;

  svn_tristate_t *supports_deadprop_count;

  /* If allocations are necessary, then use this pool.  */
  apr_pool_t *result_pool;
};

/* Implements svn_ra_serf__prop_func_t */
static svn_error_t *
fill_dirent_propfunc(void *baton,
                     const char *path,
                     const char *ns,
                     const char *name,
                     const svn_string_t *val,
                     apr_pool_t *scratch_pool)
{
  struct fill_dirent_baton_t *fdb = baton;

  if (strcmp(ns, "DAV:") == 0)
    {
      if (strcmp(name, SVN_DAV__VERSION_NAME) == 0)
        {
          apr_int64_t rev;
          SVN_ERR(svn_cstring_atoi64(&rev, val->data));

          fdb->entry->created_rev = (svn_revnum_t)rev;
        }
      else if (strcmp(name, "creator-displayname") == 0)
        {
          fdb->entry->last_author = apr_pstrdup(fdb->result_pool, val->data);
        }
      else if (strcmp(name, SVN_DAV__CREATIONDATE) == 0)
        {
          SVN_ERR(svn_time_from_cstring(&fdb->entry->time,
                                        val->data,
                                        fdb->result_pool));
        }
      else if (strcmp(name, "getcontentlength") == 0)
        {
          /* 'getcontentlength' property is empty for directories. */
          if (val->len)
            {
              SVN_ERR(svn_cstring_atoi64(&fdb->entry->size, val->data));
            }
        }
      else if (strcmp(name, "resourcetype") == 0)
        {
          if (strcmp(val->data, "collection") == 0)
            {
              fdb->entry->kind = svn_node_dir;
            }
          else
            {
              fdb->entry->kind = svn_node_file;
            }
        }
    }
  else if (strcmp(ns, SVN_DAV_PROP_NS_CUSTOM) == 0)
    {
      fdb->entry->has_props = TRUE;
    }
  else if (strcmp(ns, SVN_DAV_PROP_NS_SVN) == 0)
    {
      fdb->entry->has_props = TRUE;
    }
  else if (strcmp(ns, SVN_DAV_PROP_NS_DAV) == 0)
    {
      if(strcmp(name, "deadprop-count") == 0)
        {
          if (*val->data)
            {
              /* Note: 1.8.x and earlier servers send the count proper; 1.9.0
               * and newer send "1" if there are properties and "0" otherwise.
               */
              apr_int64_t deadprop_count;
              SVN_ERR(svn_cstring_atoi64(&deadprop_count, val->data));
              fdb->entry->has_props = deadprop_count > 0;
              if (fdb->supports_deadprop_count)
                *fdb->supports_deadprop_count = svn_tristate_true;
            }
          else if (fdb->supports_deadprop_count)
            *fdb->supports_deadprop_count = svn_tristate_false;
        }
    }

  return SVN_NO_ERROR;
}

static const svn_ra_serf__dav_props_t *
get_dirent_props(apr_uint32_t dirent_fields,
                 svn_ra_serf__session_t *session,
                 apr_pool_t *pool)
{
  svn_ra_serf__dav_props_t *prop;
  apr_array_header_t *props = svn_ra_serf__get_dirent_props(dirent_fields,
                                                            session, pool);

  prop = apr_array_push(props);
  prop->xmlns = NULL;
  prop->name = NULL;

  return (svn_ra_serf__dav_props_t *) props->elts;
}

/* Implements svn_ra__vtable_t.stat(). */
svn_error_t *
svn_ra_serf__stat(svn_ra_session_t *ra_session,
                  const char *relpath,
                  svn_revnum_t revision,
                  svn_dirent_t **dirent,
                  apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_error_t *err;
  struct fill_dirent_baton_t fdb;
  svn_tristate_t deadprop_count = svn_tristate_unknown;
  svn_ra_serf__handler_t *handler;
  const char *url;

  url = session->session_url.path;

  /* If we have a relative path, append it. */
  if (relpath)
    url = svn_path_url_add_component2(url, relpath, pool);

  /* If we were given a specific revision, get a URL that refers to that
     specific revision (rather than floating with HEAD).  */
  if (SVN_IS_VALID_REVNUM(revision))
    {
      SVN_ERR(svn_ra_serf__get_stable_url(&url, NULL /* latest_revnum */,
                                          session,
                                          url, revision,
                                          pool, pool));
    }

  fdb.entry = svn_dirent_create(pool);
  fdb.supports_deadprop_count = &deadprop_count;
  fdb.result_pool = pool;

  SVN_ERR(svn_ra_serf__create_propfind_handler(&handler, session, url,
                                               SVN_INVALID_REVNUM, "0",
                                               get_dirent_props(SVN_DIRENT_ALL,
                                                                session,
                                                                pool),
                                               fill_dirent_propfunc, &fdb, pool));

  err = svn_ra_serf__context_run_one(handler, pool);

  if (err)
    {
      if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
        {
          svn_error_clear(err);
          *dirent = NULL;
          return SVN_NO_ERROR;
        }
      else
        return svn_error_trace(err);
    }

  if (deadprop_count == svn_tristate_false
      && session->supports_deadprop_count == svn_tristate_unknown
      && !fdb.entry->has_props)
    {
      /* We have to requery as the server didn't give us the right
         information */
      session->supports_deadprop_count = svn_tristate_false;

      /* Run the same handler again */
      SVN_ERR(svn_ra_serf__context_run_one(handler, pool));
    }

  if (deadprop_count != svn_tristate_unknown)
    session->supports_deadprop_count = deadprop_count;

  *dirent = fdb.entry;

  return SVN_NO_ERROR;
}

/* Baton for get_dir_dirents_cb and get_dir_props_cb */
struct get_dir_baton_t
{
  apr_pool_t *result_pool;
  apr_hash_t *dirents;
  apr_hash_t *ret_props;
  svn_boolean_t is_directory;
  svn_tristate_t supports_deadprop_count;
  const char *path;
};

/* Implements svn_ra_serf__prop_func_t */
static svn_error_t *
get_dir_dirents_cb(void *baton,
                   const char *path,
                   const char *ns,
                   const char *name,
                   const svn_string_t *value,
                   apr_pool_t *scratch_pool)
{
  struct get_dir_baton_t *db = baton;
  const char *relpath;

  relpath = svn_fspath__skip_ancestor(db->path, path);

  if (relpath && relpath[0] != '\0')
    {
      struct fill_dirent_baton_t fdb;

      relpath = svn_path_uri_decode(relpath, scratch_pool);
      fdb.entry = svn_hash_gets(db->dirents, relpath);

      if (!fdb.entry)
        {
          fdb.entry = svn_dirent_create(db->result_pool);
          svn_hash_sets(db->dirents,
                        apr_pstrdup(db->result_pool, relpath),
                        fdb.entry);
        }

      fdb.result_pool = db->result_pool;
      fdb.supports_deadprop_count = &db->supports_deadprop_count;
      SVN_ERR(fill_dirent_propfunc(&fdb, path, ns, name, value, scratch_pool));
    }
  else if (relpath && !db->is_directory)
    {
      if (strcmp(ns, "DAV:") == 0 && strcmp(name, "resourcetype") == 0)
        {
          if (strcmp(value->data, "collection") != 0)
            {
              /* Tell a lie to exit early */
              return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                                      _("Can't get properties of non-directory"));
            }
          else
            db->is_directory = TRUE;
        }
    }

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__prop_func */
static svn_error_t *
get_dir_props_cb(void *baton,
                 const char *path,
                 const char *ns,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *scratch_pool)
{
  struct get_dir_baton_t *db = baton;
  const char *propname;

  propname = svn_ra_serf__svnname_from_wirename(ns, name, db->result_pool);
  if (propname)
    {
      svn_hash_sets(db->ret_props, propname,
                    svn_string_dup(value, db->result_pool));
      return SVN_NO_ERROR;
    }

  if (!db->is_directory)
    {
      if (strcmp(ns, "DAV:") == 0 && strcmp(name, "resourcetype") == 0)
        {
          if (strcmp(value->data, "collection") != 0)
            {
              /* Tell a lie to exit early */
              return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                                      _("Can't get properties of non-directory"));
            }
          else
            db->is_directory = TRUE;
        }
    }

  return SVN_NO_ERROR;
}

/* Implements svn_ra__vtable_t.get_dir(). */
svn_error_t *
svn_ra_serf__get_dir(svn_ra_session_t *ra_session,
                     apr_hash_t **dirents,
                     svn_revnum_t *fetched_rev,
                     apr_hash_t **ret_props,
                     const char *rel_path,
                     svn_revnum_t revision,
                     apr_uint32_t dirent_fields,
                     apr_pool_t *result_pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_pool_t *scratch_pool = svn_pool_create(result_pool);
  svn_ra_serf__handler_t *dirent_handler = NULL;
  svn_ra_serf__handler_t *props_handler = NULL;
  const char *path;
  struct get_dir_baton_t gdb;
  svn_error_t *err = SVN_NO_ERROR;

  gdb.result_pool = result_pool;
  gdb.is_directory = FALSE;
  gdb.supports_deadprop_count = svn_tristate_unknown;

  path = session->session_url.path;

  /* If we have a relative path, URI encode and append it. */
  if (rel_path)
    {
      path = svn_path_url_add_component2(path, rel_path, scratch_pool);
    }

  /* If the user specified a peg revision other than HEAD, we have to fetch
     the baseline collection url for that revision. If not, we can use the
     public url. */
  if (SVN_IS_VALID_REVNUM(revision) || fetched_rev)
    {
      SVN_ERR(svn_ra_serf__get_stable_url(&path, fetched_rev,
                                          session,
                                          path, revision,
                                          scratch_pool, scratch_pool));
      revision = SVN_INVALID_REVNUM;
    }
  /* REVISION is always SVN_INVALID_REVNUM  */
  SVN_ERR_ASSERT(!SVN_IS_VALID_REVNUM(revision));

  gdb.path = path;

  /* If we're asked for children, fetch them now. */
  if (dirents)
    {
      /* Always request node kind to check that path is really a
       * directory. */
      if (!ret_props)
        dirent_fields |= SVN_DIRENT_KIND;

      gdb.dirents = apr_hash_make(result_pool);

      SVN_ERR(svn_ra_serf__create_propfind_handler(
                                          &dirent_handler, session,
                                          path, SVN_INVALID_REVNUM, "1",
                                          get_dirent_props(dirent_fields,
                                                           session,
                                                           scratch_pool),
                                          get_dir_dirents_cb, &gdb,
                                          scratch_pool));

      svn_ra_serf__request_create(dirent_handler);
    }
  else
    gdb.dirents = NULL;

  if (ret_props)
    {
      gdb.ret_props = apr_hash_make(result_pool);
      SVN_ERR(svn_ra_serf__create_propfind_handler(
                                          &props_handler, session,
                                          path, SVN_INVALID_REVNUM, "0",
                                          all_props,
                                          get_dir_props_cb, &gdb,
                                          scratch_pool));

      svn_ra_serf__request_create(props_handler);
    }
  else
    gdb.ret_props = NULL;

  if (dirent_handler)
    {
      err = svn_error_trace(
              svn_ra_serf__context_run_wait(&dirent_handler->done,
                                            session,
                                            scratch_pool));

      if (err)
        {
          svn_pool_clear(scratch_pool); /* Unregisters outstanding requests */
          return err;
        }

      if (gdb.supports_deadprop_count == svn_tristate_false
          && session->supports_deadprop_count == svn_tristate_unknown
          && dirent_fields & SVN_DIRENT_HAS_PROPS)
        {
          /* We have to requery as the server didn't give us the right
             information */
          session->supports_deadprop_count = svn_tristate_false;

          apr_hash_clear(gdb.dirents);

          SVN_ERR(svn_ra_serf__create_propfind_handler(
                                              &dirent_handler, session,
                                              path, SVN_INVALID_REVNUM, "1",
                                              get_dirent_props(dirent_fields,
                                                               session,
                                                               scratch_pool),
                                              get_dir_dirents_cb, &gdb,
                                              scratch_pool));

          svn_ra_serf__request_create(dirent_handler);
        }
    }

  if (props_handler)
    {
      err = svn_error_trace(
              svn_ra_serf__context_run_wait(&props_handler->done,
                                            session,
                                            scratch_pool));
    }

  /* And dirent again for the case when we had to send the request again */
  if (! err && dirent_handler)
    {
      err = svn_error_trace(
              svn_ra_serf__context_run_wait(&dirent_handler->done,
                                            session,
                                            scratch_pool));
    }

  if (!err && gdb.supports_deadprop_count != svn_tristate_unknown)
    session->supports_deadprop_count = gdb.supports_deadprop_count;

  svn_pool_destroy(scratch_pool); /* Unregisters outstanding requests */

  SVN_ERR(err);

  if (!gdb.is_directory)
    return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                            _("Can't get entries of non-directory"));

  if (ret_props)
    *ret_props = gdb.ret_props;

  if (dirents)
    *dirents = gdb.dirents;

  return SVN_NO_ERROR;
}
