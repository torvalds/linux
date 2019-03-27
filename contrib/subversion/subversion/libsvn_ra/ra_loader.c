/*
 * ra_loader.c:  logic for loading different RA library implementations
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

/* ==================================================================== */

/*** Includes. ***/
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <apr.h>
#include <apr_strings.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_uri.h>

#include "svn_hash.h"
#include "svn_version.h"
#include "svn_time.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_ra.h"
#include "svn_xml.h"
#include "svn_path.h"
#include "svn_dso.h"
#include "svn_props.h"
#include "svn_sorts.h"

#include "svn_config.h"
#include "ra_loader.h"
#include "deprecated.h"

#include "private/svn_auth_private.h"
#include "private/svn_ra_private.h"
#include "svn_private_config.h"




/* These are the URI schemes that the respective libraries *may* support.
 * The schemes actually supported may be a subset of the schemes listed below.
 * This can't be determine until the library is loaded.
 * (Currently, this applies to the https scheme, which is only
 * available if SSL is supported.) */
static const char * const dav_schemes[] = { "http", "https", NULL };
static const char * const svn_schemes[] = { "svn", NULL };
static const char * const local_schemes[] = { "file", NULL };

static const struct ra_lib_defn {
  /* the name of this RA library (e.g. "neon" or "local") */
  const char *ra_name;

  const char * const *schemes;
  /* the initialization function if linked in; otherwise, NULL */
  svn_ra__init_func_t initfunc;
  svn_ra_init_func_t compat_initfunc;
} ra_libraries[] = {
  {
    "svn",
    svn_schemes,
#ifdef SVN_LIBSVN_RA_LINKS_RA_SVN
    svn_ra_svn__init,
    svn_ra_svn__deprecated_init
#endif
  },

  {
    "local",
    local_schemes,
#ifdef SVN_LIBSVN_RA_LINKS_RA_LOCAL
    svn_ra_local__init,
    svn_ra_local__deprecated_init
#endif
  },

  {
    "serf",
    dav_schemes,
#ifdef SVN_LIBSVN_RA_LINKS_RA_SERF
    svn_ra_serf__init,
    svn_ra_serf__deprecated_init
#endif
  },

  /* ADD NEW RA IMPLEMENTATIONS HERE (as they're written) */

  /* sentinel */
  { NULL }
};

/* Ensure that the RA library NAME is loaded.
 *
 * If FUNC is non-NULL, set *FUNC to the address of the svn_ra_NAME__init
 * function of the library.
 *
 * If COMPAT_FUNC is non-NULL, set *COMPAT_FUNC to the address of the
 * svn_ra_NAME_init compatibility init function of the library.
 *
 * ### todo: Any RA libraries implemented from this point forward
 * ### don't really need an svn_ra_NAME_init compatibility function.
 * ### Currently, load_ra_module() will error if no such function is
 * ### found, but it might be more friendly to simply set *COMPAT_FUNC
 * ### to null (assuming COMPAT_FUNC itself is non-null).
 */
static svn_error_t *
load_ra_module(svn_ra__init_func_t *func,
               svn_ra_init_func_t *compat_func,
               const char *ra_name, apr_pool_t *pool)
{
  if (func)
    *func = NULL;
  if (compat_func)
    *compat_func = NULL;

#if defined(SVN_USE_DSO) && APR_HAS_DSO
  {
    apr_dso_handle_t *dso;
    apr_dso_handle_sym_t symbol;
    const char *libname;
    const char *funcname;
    const char *compat_funcname;
    apr_status_t status;

    libname = apr_psprintf(pool, "libsvn_ra_%s-" SVN_DSO_SUFFIX_FMT,
                           ra_name, SVN_VER_MAJOR, SVN_SOVERSION);
    funcname = apr_psprintf(pool, "svn_ra_%s__init", ra_name);
    compat_funcname = apr_psprintf(pool, "svn_ra_%s_init", ra_name);

    /* find/load the specified library */
    SVN_ERR(svn_dso_load(&dso, libname));
    if (! dso)
      return SVN_NO_ERROR;

    /* find the initialization routines */
    if (func)
      {
        status = apr_dso_sym(&symbol, dso, funcname);
        if (status)
          {
            return svn_error_wrap_apr(status,
                                      _("'%s' does not define '%s()'"),
                                      libname, funcname);
          }

        *func = (svn_ra__init_func_t) symbol;
      }

    if (compat_func)
      {
        status = apr_dso_sym(&symbol, dso, compat_funcname);
        if (status)
          {
            return svn_error_wrap_apr(status,
                                      _("'%s' does not define '%s()'"),
                                      libname, compat_funcname);
          }

        *compat_func = (svn_ra_init_func_t) symbol;
      }
  }
#endif /* APR_HAS_DSO */

  return SVN_NO_ERROR;
}

/* If SCHEMES contains URL, return the scheme.  Else, return NULL. */
static const char *
has_scheme_of(const char * const *schemes, const char *url)
{
  apr_size_t len;

  for ( ; *schemes != NULL; ++schemes)
    {
      const char *scheme = *schemes;
      len = strlen(scheme);
      /* Case-insensitive comparison, per RFC 2396 section 3.1.  Allow
         URL to contain a trailing "+foo" section in the scheme, since
         that's how we specify tunnel schemes in ra_svn. */
      if (strncasecmp(scheme, url, len) == 0 &&
          (url[len] == ':' || url[len] == '+'))
        return scheme;
    }

  return NULL;
}

/* Return an error if RA_VERSION doesn't match the version of this library.
   Use SCHEME in the error message to describe the library that was loaded. */
static svn_error_t *
check_ra_version(const svn_version_t *ra_version, const char *scheme)
{
  const svn_version_t *my_version = svn_ra_version();
  if (!svn_ver_equal(my_version, ra_version))
    return svn_error_createf(SVN_ERR_VERSION_MISMATCH, NULL,
                             _("Mismatched RA version for '%s':"
                               " found %d.%d.%d%s,"
                               " expected %d.%d.%d%s"),
                             scheme,
                             my_version->major, my_version->minor,
                             my_version->patch, my_version->tag,
                             ra_version->major, ra_version->minor,
                             ra_version->patch, ra_version->tag);

  return SVN_NO_ERROR;
}

/* -------------------------------------------------------------- */

/*** Public Interfaces ***/

svn_error_t *svn_ra_initialize(apr_pool_t *pool)
{
#if defined(SVN_USE_DSO) && APR_HAS_DSO
  /* Ensure that DSO subsystem is initialized early as possible if
     we're going to use it. */
  SVN_ERR(svn_dso_initialize2());
#endif
  return SVN_NO_ERROR;
}

/* Please note: the implementation of svn_ra_create_callbacks is
 * duplicated in libsvn_ra/wrapper_template.h:compat_open() .  This
 * duplication is intentional, is there to avoid a circular
 * dependancy, and is justified in great length in the code of
 * compat_open() in libsvn_ra/wrapper_template.h.  If you modify the
 * implementation of svn_ra_create_callbacks(), be sure to keep the
 * code in wrapper_template.h:compat_open() in sync with your
 * changes. */
svn_error_t *
svn_ra_create_callbacks(svn_ra_callbacks2_t **callbacks,
                        apr_pool_t *pool)
{
  *callbacks = apr_pcalloc(pool, sizeof(**callbacks));
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_open4(svn_ra_session_t **session_p,
                          const char **corrected_url_p,
                          const char *repos_URL,
                          const char *uuid,
                          const svn_ra_callbacks2_t *callbacks,
                          void *callback_baton,
                          apr_hash_t *config,
                          apr_pool_t *pool)
{
  apr_pool_t *sesspool = svn_pool_create(pool);
  apr_pool_t *scratch_pool = svn_pool_create(sesspool);
  svn_ra_session_t *session;
  const struct ra_lib_defn *defn;
  const svn_ra__vtable_t *vtable = NULL;
  apr_uri_t repos_URI;
  apr_status_t apr_err;
  svn_error_t *err;
#ifdef CHOOSABLE_DAV_MODULE
  const char *http_library = DEFAULT_HTTP_LIBRARY;
#endif
  svn_auth_baton_t *auth_baton;

  /* Initialize the return variable. */
  *session_p = NULL;

  apr_err = apr_uri_parse(sesspool, repos_URL, &repos_URI);
  /* ### Should apr_uri_parse leave hostname NULL?  It doesn't
   * for "file:///" URLs, only for bogus URLs like "bogus".
   * If this is the right behavior for apr_uri_parse, maybe we
   * should have a svn_uri_parse wrapper. */
  if (apr_err != APR_SUCCESS || repos_URI.hostname == NULL)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("Illegal repository URL '%s'"),
                             repos_URL);

  if (callbacks->auth_baton)
    SVN_ERR(svn_auth__make_session_auth(&auth_baton,
                                        callbacks->auth_baton, config,
                                        repos_URI.hostname,
                                        sesspool, scratch_pool));
  else
    auth_baton = NULL;

#ifdef CHOOSABLE_DAV_MODULE
  if (config)
    {
      svn_config_t *servers = NULL;
      const char *server_group = NULL;

      /* Grab the 'servers' config. */
      servers = svn_hash_gets(config, SVN_CONFIG_CATEGORY_SERVERS);
      if (servers)
        {
          /* First, look in the global section. */

          /* Find out where we're about to connect to, and
           * try to pick a server group based on the destination. */
          server_group = svn_config_find_group(servers, repos_URI.hostname,
                                               SVN_CONFIG_SECTION_GROUPS,
                                               sesspool);

          /* Now, which DAV-based RA method do we want to use today? */
          http_library
            = svn_config_get_server_setting(servers,
                                            server_group, /* NULL is OK */
                                            SVN_CONFIG_OPTION_HTTP_LIBRARY,
                                            DEFAULT_HTTP_LIBRARY);

          if (strcmp(http_library, "serf") != 0)
            return svn_error_createf(SVN_ERR_BAD_CONFIG_VALUE, NULL,
                                     _("Invalid config: unknown HTTP library "
                                       "'%s'"),
                                     http_library);
        }
    }
#endif

  /* Find the library. */
  for (defn = ra_libraries; defn->ra_name != NULL; ++defn)
    {
      const char *scheme;

      if ((scheme = has_scheme_of(defn->schemes, repos_URL)))
        {
          svn_ra__init_func_t initfunc = defn->initfunc;

#ifdef CHOOSABLE_DAV_MODULE
          if (defn->schemes == dav_schemes
              && strcmp(defn->ra_name, http_library) != 0)
            continue;
#endif

          if (! initfunc)
            SVN_ERR(load_ra_module(&initfunc, NULL, defn->ra_name,
                                   scratch_pool));
          if (! initfunc)
            /* Library not found. */
            continue;

          SVN_ERR(initfunc(svn_ra_version(), &vtable, scratch_pool));

          SVN_ERR(check_ra_version(vtable->get_version(), scheme));

          if (! has_scheme_of(vtable->get_schemes(scratch_pool), repos_URL))
            /* Library doesn't support the scheme at runtime. */
            continue;


          break;
        }
    }

  if (vtable == NULL)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("Unrecognized URL scheme for '%s'"),
                             repos_URL);

  /* Create the session object. */
  session = apr_pcalloc(sesspool, sizeof(*session));
  session->cancel_func = callbacks->cancel_func;
  session->cancel_baton = callback_baton;
  session->vtable = vtable;
  session->pool = sesspool;

  /* Ask the library to open the session. */
  err = vtable->open_session(session, corrected_url_p,
                             repos_URL,
                             callbacks, callback_baton, auth_baton,
                             config, sesspool, scratch_pool);

  if (err)
    {
      svn_pool_destroy(sesspool); /* Includes scratch_pool */
      if (err->apr_err == SVN_ERR_RA_SESSION_URL_MISMATCH)
        return svn_error_trace(err);

      return svn_error_createf(
                SVN_ERR_RA_CANNOT_CREATE_SESSION, err,
                _("Unable to connect to a repository at URL '%s'"),
                repos_URL);
    }

  /* If the session open stuff detected a server-provided URL
     correction (a 301 or 302 redirect response during the initial
     OPTIONS request), then kill the session so the caller can decide
     what to do. */
  if (corrected_url_p && *corrected_url_p)
    {
      /* *session_p = NULL; */
      *corrected_url_p = apr_pstrdup(pool, *corrected_url_p);
      svn_pool_destroy(sesspool); /* Includes scratch_pool */
      return SVN_NO_ERROR;
    }

  if (vtable->set_svn_ra_open)
    SVN_ERR(vtable->set_svn_ra_open(session, svn_ra_open4));

  /* Check the UUID. */
  if (uuid)
    {
      const char *repository_uuid;

      SVN_ERR(vtable->get_uuid(session, &repository_uuid, pool));
      if (strcmp(uuid, repository_uuid) != 0)
        {
          /* Duplicate the uuid as it is allocated in sesspool */
          repository_uuid = apr_pstrdup(pool, repository_uuid);
          svn_pool_destroy(sesspool); /* includes scratch_pool */
          return svn_error_createf(SVN_ERR_RA_UUID_MISMATCH, NULL,
                                   _("Repository UUID '%s' doesn't match "
                                     "expected UUID '%s'"),
                                   repository_uuid, uuid);
        }
    }

  svn_pool_destroy(scratch_pool);
  *session_p = session;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra__dup_session(svn_ra_session_t **new_session,
                    svn_ra_session_t *old_session,
                    const char *session_url,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  svn_ra_session_t *session;

  if (session_url)
    {
      const char *dummy;

      /* This verifies in new_session_url is in the repository */
      SVN_ERR(svn_ra_get_path_relative_to_root(old_session,
                                               &dummy,
                                               session_url,
                                               scratch_pool));
    }
  else
    SVN_ERR(svn_ra_get_session_url(old_session, &session_url, scratch_pool));

  /* Create the session object. */
  session = apr_pcalloc(result_pool, sizeof(*session));
  session->cancel_func = old_session->cancel_func;
  session->cancel_baton = old_session->cancel_baton;
  session->vtable = old_session->vtable;
  session->pool = result_pool;

  SVN_ERR(old_session->vtable->dup_session(session,
                                           old_session,
                                           session_url,
                                           result_pool,
                                           scratch_pool));

  if (session->vtable->set_svn_ra_open)
    SVN_ERR(session->vtable->set_svn_ra_open(session, svn_ra_open4));

  *new_session = session;
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_reparent(svn_ra_session_t *session,
                             const char *url,
                             apr_pool_t *pool)
{
  const char *repos_root;

  /* Make sure the new URL is in the same repository, so that the
     implementations don't have to do it. */
  SVN_ERR(svn_ra_get_repos_root2(session, &repos_root, pool));
  if (! svn_uri__is_ancestor(repos_root, url))
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("'%s' isn't in the same repository as '%s'"),
                             url, repos_root);

  return session->vtable->reparent(session, url, pool);
}

svn_error_t *svn_ra_get_session_url(svn_ra_session_t *session,
                                    const char **url,
                                    apr_pool_t *pool)
{
  return session->vtable->get_session_url(session, url, pool);
}

svn_error_t *svn_ra_get_path_relative_to_session(svn_ra_session_t *session,
                                                 const char **rel_path,
                                                 const char *url,
                                                 apr_pool_t *pool)
{
  const char *sess_url;

  SVN_ERR(session->vtable->get_session_url(session, &sess_url, pool));
  *rel_path = svn_uri_skip_ancestor(sess_url, url, pool);
  if (! *rel_path)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("'%s' isn't a child of session URL '%s'"),
                             url, sess_url);
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_get_path_relative_to_root(svn_ra_session_t *session,
                                              const char **rel_path,
                                              const char *url,
                                              apr_pool_t *pool)
{
  const char *root_url;

  SVN_ERR(session->vtable->get_repos_root(session, &root_url, pool));
  *rel_path = svn_uri_skip_ancestor(root_url, url, pool);
  if (! *rel_path)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("'%s' isn't a child of repository root "
                               "URL '%s'"),
                             url, root_url);
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_get_latest_revnum(svn_ra_session_t *session,
                                      svn_revnum_t *latest_revnum,
                                      apr_pool_t *pool)
{
  return session->vtable->get_latest_revnum(session, latest_revnum, pool);
}

svn_error_t *svn_ra_get_dated_revision(svn_ra_session_t *session,
                                       svn_revnum_t *revision,
                                       apr_time_t tm,
                                       apr_pool_t *pool)
{
  return session->vtable->get_dated_revision(session, revision, tm, pool);
}

svn_error_t *svn_ra_change_rev_prop2(svn_ra_session_t *session,
                                     svn_revnum_t rev,
                                     const char *name,
                                     const svn_string_t *const *old_value_p,
                                     const svn_string_t *value,
                                     apr_pool_t *pool)
{
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(rev));

  /* If an old value was specified, make sure the server supports
   * specifying it. */
  if (old_value_p)
    {
      svn_boolean_t has_atomic_revprops;

      SVN_ERR(svn_ra_has_capability(session, &has_atomic_revprops,
                                    SVN_RA_CAPABILITY_ATOMIC_REVPROPS,
                                    pool));

      if (!has_atomic_revprops)
        /* API violation.  (Should be an ASSERT, but gstein talked me
         * out of it.) */
        return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                 _("Specifying 'old_value_p' is not allowed when "
                                   "the '%s' capability is not advertised, and "
                                   "could indicate a bug in your client"),
                                   SVN_RA_CAPABILITY_ATOMIC_REVPROPS);
    }

  return session->vtable->change_rev_prop(session, rev, name,
                                          old_value_p, value, pool);
}

svn_error_t *svn_ra_rev_proplist(svn_ra_session_t *session,
                                 svn_revnum_t rev,
                                 apr_hash_t **props,
                                 apr_pool_t *pool)
{
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(rev));
  return session->vtable->rev_proplist(session, rev, props, pool);
}

svn_error_t *svn_ra_rev_prop(svn_ra_session_t *session,
                             svn_revnum_t rev,
                             const char *name,
                             svn_string_t **value,
                             apr_pool_t *pool)
{
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(rev));
  return session->vtable->rev_prop(session, rev, name, value, pool);
}

svn_error_t *svn_ra_get_commit_editor3(svn_ra_session_t *session,
                                       const svn_delta_editor_t **editor,
                                       void **edit_baton,
                                       apr_hash_t *revprop_table,
                                       svn_commit_callback2_t commit_callback,
                                       void *commit_baton,
                                       apr_hash_t *lock_tokens,
                                       svn_boolean_t keep_locks,
                                       apr_pool_t *pool)
{
  return session->vtable->get_commit_editor(session, editor, edit_baton,
                                            revprop_table,
                                            commit_callback, commit_baton,
                                            lock_tokens, keep_locks, pool);
}

svn_error_t *svn_ra_get_file(svn_ra_session_t *session,
                             const char *path,
                             svn_revnum_t revision,
                             svn_stream_t *stream,
                             svn_revnum_t *fetched_rev,
                             apr_hash_t **props,
                             apr_pool_t *pool)
{
  SVN_ERR_ASSERT(svn_relpath_is_canonical(path));
  return session->vtable->get_file(session, path, revision, stream,
                                   fetched_rev, props, pool);
}

svn_error_t *svn_ra_get_dir2(svn_ra_session_t *session,
                             apr_hash_t **dirents,
                             svn_revnum_t *fetched_rev,
                             apr_hash_t **props,
                             const char *path,
                             svn_revnum_t revision,
                             apr_uint32_t dirent_fields,
                             apr_pool_t *pool)
{
  SVN_ERR_ASSERT(svn_relpath_is_canonical(path));
  return session->vtable->get_dir(session, dirents, fetched_rev, props,
                                  path, revision, dirent_fields, pool);
}

svn_error_t *
svn_ra_list(svn_ra_session_t *session,
            const char *path,
            svn_revnum_t revision,
            const apr_array_header_t *patterns,
            svn_depth_t depth,
            apr_uint32_t dirent_fields,
            svn_ra_dirent_receiver_t receiver,
            void *receiver_baton,
            apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_relpath_is_canonical(path));
  if (!session->vtable->list)
    return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL, NULL);

  SVN_ERR(svn_ra__assert_capable_server(session, SVN_RA_CAPABILITY_LIST,
                                        NULL, scratch_pool));

  return session->vtable->list(session, path, revision, patterns, depth,
                               dirent_fields, receiver, receiver_baton,
                               scratch_pool);
}

svn_error_t *svn_ra_get_mergeinfo(svn_ra_session_t *session,
                                  svn_mergeinfo_catalog_t *catalog,
                                  const apr_array_header_t *paths,
                                  svn_revnum_t revision,
                                  svn_mergeinfo_inheritance_t inherit,
                                  svn_boolean_t include_descendants,
                                  apr_pool_t *pool)
{
  svn_error_t *err;
  int i;

  /* Validate path format. */
  for (i = 0; i < paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);
      SVN_ERR_ASSERT(svn_relpath_is_canonical(path));
    }

  /* Check server Merge Tracking capability. */
  err = svn_ra__assert_mergeinfo_capable_server(session, NULL, pool);
  if (err)
    {
      *catalog = NULL;
      return err;
    }

  return session->vtable->get_mergeinfo(session, catalog, paths,
                                        revision, inherit,
                                        include_descendants, pool);
}

svn_error_t *
svn_ra_do_update3(svn_ra_session_t *session,
                  const svn_ra_reporter3_t **reporter,
                  void **report_baton,
                  svn_revnum_t revision_to_update_to,
                  const char *update_target,
                  svn_depth_t depth,
                  svn_boolean_t send_copyfrom_args,
                  svn_boolean_t ignore_ancestry,
                  const svn_delta_editor_t *update_editor,
                  void *update_baton,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_path_is_empty(update_target)
                 || svn_path_is_single_path_component(update_target));
  return session->vtable->do_update(session,
                                    reporter, report_baton,
                                    revision_to_update_to, update_target,
                                    depth, send_copyfrom_args,
                                    ignore_ancestry,
                                    update_editor, update_baton,
                                    result_pool, scratch_pool);
}

svn_error_t *
svn_ra_do_switch3(svn_ra_session_t *session,
                  const svn_ra_reporter3_t **reporter,
                  void **report_baton,
                  svn_revnum_t revision_to_switch_to,
                  const char *switch_target,
                  svn_depth_t depth,
                  const char *switch_url,
                  svn_boolean_t send_copyfrom_args,
                  svn_boolean_t ignore_ancestry,
                  const svn_delta_editor_t *switch_editor,
                  void *switch_baton,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_path_is_empty(switch_target)
                 || svn_path_is_single_path_component(switch_target));
  return session->vtable->do_switch(session,
                                    reporter, report_baton,
                                    revision_to_switch_to, switch_target,
                                    depth, switch_url,
                                    send_copyfrom_args,
                                    ignore_ancestry,
                                    switch_editor,
                                    switch_baton,
                                    result_pool, scratch_pool);
}

svn_error_t *svn_ra_do_status2(svn_ra_session_t *session,
                               const svn_ra_reporter3_t **reporter,
                               void **report_baton,
                               const char *status_target,
                               svn_revnum_t revision,
                               svn_depth_t depth,
                               const svn_delta_editor_t *status_editor,
                               void *status_baton,
                               apr_pool_t *pool)
{
  SVN_ERR_ASSERT(svn_path_is_empty(status_target)
                 || svn_path_is_single_path_component(status_target));
  return session->vtable->do_status(session,
                                    reporter, report_baton,
                                    status_target, revision, depth,
                                    status_editor, status_baton, pool);
}

svn_error_t *svn_ra_do_diff3(svn_ra_session_t *session,
                             const svn_ra_reporter3_t **reporter,
                             void **report_baton,
                             svn_revnum_t revision,
                             const char *diff_target,
                             svn_depth_t depth,
                             svn_boolean_t ignore_ancestry,
                             svn_boolean_t text_deltas,
                             const char *versus_url,
                             const svn_delta_editor_t *diff_editor,
                             void *diff_baton,
                             apr_pool_t *pool)
{
  SVN_ERR_ASSERT(svn_path_is_empty(diff_target)
                 || svn_path_is_single_path_component(diff_target));
  return session->vtable->do_diff(session,
                                  reporter, report_baton,
                                  revision, diff_target,
                                  depth, ignore_ancestry,
                                  text_deltas, versus_url, diff_editor,
                                  diff_baton, pool);
}

svn_error_t *svn_ra_get_log2(svn_ra_session_t *session,
                             const apr_array_header_t *paths,
                             svn_revnum_t start,
                             svn_revnum_t end,
                             int limit,
                             svn_boolean_t discover_changed_paths,
                             svn_boolean_t strict_node_history,
                             svn_boolean_t include_merged_revisions,
                             const apr_array_header_t *revprops,
                             svn_log_entry_receiver_t receiver,
                             void *receiver_baton,
                             apr_pool_t *pool)
{
  if (paths)
    {
      int i;
      for (i = 0; i < paths->nelts; i++)
        {
          const char *path = APR_ARRAY_IDX(paths, i, const char *);
          SVN_ERR_ASSERT(svn_relpath_is_canonical(path));
        }
    }

  if (include_merged_revisions)
    SVN_ERR(svn_ra__assert_mergeinfo_capable_server(session, NULL, pool));

  return session->vtable->get_log(session, paths, start, end, limit,
                                  discover_changed_paths, strict_node_history,
                                  include_merged_revisions, revprops,
                                  receiver, receiver_baton, pool);
}

svn_error_t *svn_ra_check_path(svn_ra_session_t *session,
                               const char *path,
                               svn_revnum_t revision,
                               svn_node_kind_t *kind,
                               apr_pool_t *pool)
{
  SVN_ERR_ASSERT(svn_relpath_is_canonical(path));
  return session->vtable->check_path(session, path, revision, kind, pool);
}

svn_error_t *svn_ra_stat(svn_ra_session_t *session,
                         const char *path,
                         svn_revnum_t revision,
                         svn_dirent_t **dirent,
                         apr_pool_t *pool)
{
  svn_error_t *err;
  SVN_ERR_ASSERT(svn_relpath_is_canonical(path));
  err = session->vtable->stat(session, path, revision, dirent, pool);

  /* svnserve before 1.2 doesn't support the above, so fall back on
     a far less efficient, but still correct method. */
  if (err && err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED)
    {
      /* ### TODO: Find out if we can somehow move this code in libsvn_ra_svn.
       */
      apr_pool_t *scratch_pool = svn_pool_create(pool);
      svn_node_kind_t kind;

      svn_error_clear(err);

      SVN_ERR(svn_ra_check_path(session, path, revision, &kind, scratch_pool));

      if (kind != svn_node_none)
        {
          const char *repos_root_url;
          const char *session_url;

          SVN_ERR(svn_ra_get_repos_root2(session, &repos_root_url,
                                         scratch_pool));
          SVN_ERR(svn_ra_get_session_url(session, &session_url,
                                         scratch_pool));

          if (!svn_path_is_empty(path))
            session_url = svn_path_url_add_component2(session_url, path,
                                                      scratch_pool);

          if (strcmp(session_url, repos_root_url) != 0)
            {
              svn_ra_session_t *parent_session;
              apr_hash_t *parent_ents;
              const char *parent_url, *base_name;

              /* Open another session to the path's parent.  This server
                 doesn't support svn_ra_reparent anyway, so don't try it. */
              svn_uri_split(&parent_url, &base_name, session_url,
                            scratch_pool);

              SVN_ERR(svn_ra__dup_session(&parent_session, session, parent_url,
                                          scratch_pool, scratch_pool));

              /* Get all parent's entries, no props. */
              SVN_ERR(svn_ra_get_dir2(parent_session, &parent_ents, NULL,
                                      NULL, "", revision, SVN_DIRENT_ALL,
                                      scratch_pool));

              /* Get the relevant entry. */
              *dirent = svn_hash_gets(parent_ents, base_name);

              if (*dirent)
                *dirent = svn_dirent_dup(*dirent, pool);
            }
          else
            {
              apr_hash_t *props;
              const svn_string_t *val;

              /* We can't get the directory entry for the repository root,
                 but we can still get the information we want.
                 The created-rev of the repository root must, by definition,
                 be rev. */
              *dirent = apr_pcalloc(pool, sizeof(**dirent));
              (*dirent)->kind = kind;
              (*dirent)->size = SVN_INVALID_FILESIZE;

              SVN_ERR(svn_ra_get_dir2(session, NULL, NULL, &props,
                                      "", revision, 0 /* no dirent fields */,
                                      scratch_pool));
              (*dirent)->has_props = (apr_hash_count(props) != 0);

              (*dirent)->created_rev = revision;

              SVN_ERR(svn_ra_rev_proplist(session, revision, &props,
                                          scratch_pool));

              val = svn_hash_gets(props, SVN_PROP_REVISION_DATE);
              if (val)
                SVN_ERR(svn_time_from_cstring(&(*dirent)->time, val->data,
                                              scratch_pool));

              val = svn_hash_gets(props, SVN_PROP_REVISION_AUTHOR);
              (*dirent)->last_author = val ? apr_pstrdup(pool, val->data)
                                           : NULL;
            }
        }
      else
        *dirent = NULL;

      svn_pool_clear(scratch_pool);
    }
  else
    SVN_ERR(err);

  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_get_uuid2(svn_ra_session_t *session,
                              const char **uuid,
                              apr_pool_t *pool)
{
  SVN_ERR(session->vtable->get_uuid(session, uuid, pool));
  *uuid = *uuid ? apr_pstrdup(pool, *uuid) : NULL;
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_get_uuid(svn_ra_session_t *session,
                             const char **uuid,
                             apr_pool_t *pool)
{
  return session->vtable->get_uuid(session, uuid, pool);
}

svn_error_t *svn_ra_get_repos_root2(svn_ra_session_t *session,
                                    const char **url,
                                    apr_pool_t *pool)
{
  SVN_ERR(session->vtable->get_repos_root(session, url, pool));
  *url = *url ? apr_pstrdup(pool, *url) : NULL;
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_get_repos_root(svn_ra_session_t *session,
                                   const char **url,
                                   apr_pool_t *pool)
{
  return session->vtable->get_repos_root(session, url, pool);
}

svn_error_t *svn_ra_get_locations(svn_ra_session_t *session,
                                  apr_hash_t **locations,
                                  const char *path,
                                  svn_revnum_t peg_revision,
                                  const apr_array_header_t *location_revisions,
                                  apr_pool_t *pool)
{
  svn_error_t *err;

  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(peg_revision));
  SVN_ERR_ASSERT(svn_relpath_is_canonical(path));
  err = session->vtable->get_locations(session, locations, path,
                                       peg_revision, location_revisions, pool);
  if (err && (err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED))
    {
      svn_error_clear(err);

      /* Do it the slow way, using get-logs, for older servers. */
      err = svn_ra__locations_from_log(session, locations, path,
                                       peg_revision, location_revisions,
                                       pool);
    }
  return err;
}

svn_error_t *
svn_ra_get_location_segments(svn_ra_session_t *session,
                             const char *path,
                             svn_revnum_t peg_revision,
                             svn_revnum_t start_rev,
                             svn_revnum_t end_rev,
                             svn_location_segment_receiver_t receiver,
                             void *receiver_baton,
                             apr_pool_t *pool)
{
  svn_error_t *err;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(path));
  err = session->vtable->get_location_segments(session, path, peg_revision,
                                               start_rev, end_rev,
                                               receiver, receiver_baton, pool);
  if (err && (err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED))
    {
      svn_error_clear(err);

      /* Do it the slow way, using get-logs, for older servers. */
      err = svn_ra__location_segments_from_log(session, path,
                                               peg_revision, start_rev,
                                               end_rev, receiver,
                                               receiver_baton, pool);
    }
  return err;
}

svn_error_t *svn_ra_get_file_revs2(svn_ra_session_t *session,
                                   const char *path,
                                   svn_revnum_t start,
                                   svn_revnum_t end,
                                   svn_boolean_t include_merged_revisions,
                                   svn_file_rev_handler_t handler,
                                   void *handler_baton,
                                   apr_pool_t *pool)
{
  svn_error_t *err;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(path));

  if (include_merged_revisions)
    SVN_ERR(svn_ra__assert_mergeinfo_capable_server(session, NULL, pool));

  if (start > end || !SVN_IS_VALID_REVNUM(start))
    SVN_ERR(
     svn_ra__assert_capable_server(session,
                                   SVN_RA_CAPABILITY_GET_FILE_REVS_REVERSE,
                                   NULL,
                                   pool));

  err = session->vtable->get_file_revs(session, path, start, end,
                                       include_merged_revisions,
                                       handler, handler_baton, pool);
  if (err && (err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED)
      && !include_merged_revisions)
    {
      svn_error_clear(err);

      /* Do it the slow way, using get-logs, for older servers. */
      err = svn_ra__file_revs_from_log(session, path, start, end,
                                       handler, handler_baton, pool);
    }
  return svn_error_trace(err);
}

svn_error_t *svn_ra_lock(svn_ra_session_t *session,
                         apr_hash_t *path_revs,
                         const char *comment,
                         svn_boolean_t steal_lock,
                         svn_ra_lock_callback_t lock_func,
                         void *lock_baton,
                         apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, path_revs); hi; hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);

      SVN_ERR_ASSERT(svn_relpath_is_canonical(path));
    }

  if (comment && ! svn_xml_is_xml_safe(comment, strlen(comment)))
    return svn_error_create
      (SVN_ERR_XML_UNESCAPABLE_DATA, NULL,
       _("Lock comment contains illegal characters"));

  return session->vtable->lock(session, path_revs, comment, steal_lock,
                               lock_func, lock_baton, pool);
}

svn_error_t *svn_ra_unlock(svn_ra_session_t *session,
                           apr_hash_t *path_tokens,
                           svn_boolean_t break_lock,
                           svn_ra_lock_callback_t lock_func,
                           void *lock_baton,
                           apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, path_tokens); hi; hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);

      SVN_ERR_ASSERT(svn_relpath_is_canonical(path));
    }

  return session->vtable->unlock(session, path_tokens, break_lock,
                                 lock_func, lock_baton, pool);
}

svn_error_t *svn_ra_get_lock(svn_ra_session_t *session,
                             svn_lock_t **lock,
                             const char *path,
                             apr_pool_t *pool)
{
  SVN_ERR_ASSERT(svn_relpath_is_canonical(path));
  return session->vtable->get_lock(session, lock, path, pool);
}

svn_error_t *svn_ra_get_locks2(svn_ra_session_t *session,
                               apr_hash_t **locks,
                               const char *path,
                               svn_depth_t depth,
                               apr_pool_t *pool)
{
  SVN_ERR_ASSERT(svn_relpath_is_canonical(path));
  SVN_ERR_ASSERT((depth == svn_depth_empty) ||
                 (depth == svn_depth_files) ||
                 (depth == svn_depth_immediates) ||
                 (depth == svn_depth_infinity));
  return session->vtable->get_locks(session, locks, path, depth, pool);
}

svn_error_t *svn_ra_get_locks(svn_ra_session_t *session,
                              apr_hash_t **locks,
                              const char *path,
                              apr_pool_t *pool)
{
  return svn_ra_get_locks2(session, locks, path, svn_depth_infinity, pool);
}

svn_error_t *svn_ra_replay(svn_ra_session_t *session,
                           svn_revnum_t revision,
                           svn_revnum_t low_water_mark,
                           svn_boolean_t text_deltas,
                           const svn_delta_editor_t *editor,
                           void *edit_baton,
                           apr_pool_t *pool)
{
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision)
                 && SVN_IS_VALID_REVNUM(low_water_mark));
  return session->vtable->replay(session, revision, low_water_mark,
                                 text_deltas, editor, edit_baton, pool);
}

svn_error_t *
svn_ra__replay_ev2(svn_ra_session_t *session,
                   svn_revnum_t revision,
                   svn_revnum_t low_water_mark,
                   svn_boolean_t send_deltas,
                   svn_editor_t *editor,
                   apr_pool_t *scratch_pool)
{
  SVN__NOT_IMPLEMENTED();
}

static svn_error_t *
replay_range_from_replays(svn_ra_session_t *session,
                          svn_revnum_t start_revision,
                          svn_revnum_t end_revision,
                          svn_revnum_t low_water_mark,
                          svn_boolean_t text_deltas,
                          svn_ra_replay_revstart_callback_t revstart_func,
                          svn_ra_replay_revfinish_callback_t revfinish_func,
                          void *replay_baton,
                          apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_revnum_t rev;

  for (rev = start_revision ; rev <= end_revision ; rev++)
    {
      const svn_delta_editor_t *editor;
      void *edit_baton;
      apr_hash_t *rev_props;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_ra_rev_proplist(session, rev, &rev_props, iterpool));

      SVN_ERR(revstart_func(rev, replay_baton,
                            &editor, &edit_baton,
                            rev_props,
                            iterpool));
      SVN_ERR(svn_ra_replay(session, rev, low_water_mark,
                            text_deltas, editor, edit_baton,
                            iterpool));
      SVN_ERR(revfinish_func(rev, replay_baton,
                             editor, edit_baton,
                             rev_props,
                             iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_replay_range(svn_ra_session_t *session,
                    svn_revnum_t start_revision,
                    svn_revnum_t end_revision,
                    svn_revnum_t low_water_mark,
                    svn_boolean_t text_deltas,
                    svn_ra_replay_revstart_callback_t revstart_func,
                    svn_ra_replay_revfinish_callback_t revfinish_func,
                    void *replay_baton,
                    apr_pool_t *pool)
{
  svn_error_t *err;

  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(start_revision)
                 && SVN_IS_VALID_REVNUM(end_revision)
                 && start_revision <= end_revision
                 && SVN_IS_VALID_REVNUM(low_water_mark));

  err =
    session->vtable->replay_range(session, start_revision, end_revision,
                                  low_water_mark, text_deltas,
                                  revstart_func, revfinish_func,
                                  replay_baton, pool);

  if (!err || (err && (err->apr_err != SVN_ERR_RA_NOT_IMPLEMENTED)))
    return svn_error_trace(err);

  svn_error_clear(err);
  return svn_error_trace(replay_range_from_replays(session, start_revision,
                                                   end_revision,
                                                   low_water_mark,
                                                   text_deltas,
                                                   revstart_func,
                                                   revfinish_func,
                                                   replay_baton, pool));
}

svn_error_t *
svn_ra__replay_range_ev2(svn_ra_session_t *session,
                         svn_revnum_t start_revision,
                         svn_revnum_t end_revision,
                         svn_revnum_t low_water_mark,
                         svn_boolean_t send_deltas,
                         svn_ra__replay_revstart_ev2_callback_t revstart_func,
                         svn_ra__replay_revfinish_ev2_callback_t revfinish_func,
                         void *replay_baton,
                         svn_ra__provide_base_cb_t provide_base_cb,
                         svn_ra__provide_props_cb_t provide_props_cb,
                         svn_ra__get_copysrc_kind_cb_t get_copysrc_kind_cb,
                         void *cb_baton,
                         apr_pool_t *scratch_pool)
{
  if (session->vtable->replay_range_ev2 == NULL)
    {
      /* The specific RA layer does not have an implementation. Use our
         default shim over the normal replay editor.  */

      /* This will call the Ev1 replay range handler with modified
         callbacks. */
      return svn_error_trace(svn_ra__use_replay_range_shim(
                                session,
                                start_revision,
                                end_revision,
                                low_water_mark,
                                send_deltas,
                                revstart_func,
                                revfinish_func,
                                replay_baton,
                                provide_base_cb,
                                provide_props_cb,
                                cb_baton,
                                scratch_pool));
    }

  return svn_error_trace(session->vtable->replay_range_ev2(
                            session, start_revision, end_revision,
                            low_water_mark, send_deltas, revstart_func,
                            revfinish_func, replay_baton, scratch_pool));
}

svn_error_t *svn_ra_has_capability(svn_ra_session_t *session,
                                   svn_boolean_t *has,
                                   const char *capability,
                                   apr_pool_t *pool)
{
  return session->vtable->has_capability(session, has, capability, pool);
}

svn_error_t *
svn_ra_get_deleted_rev(svn_ra_session_t *session,
                       const char *path,
                       svn_revnum_t peg_revision,
                       svn_revnum_t end_revision,
                       svn_revnum_t *revision_deleted,
                       apr_pool_t *pool)
{
  svn_error_t *err;

  /* Path must be relative. */
  SVN_ERR_ASSERT(svn_relpath_is_canonical(path));

  if (!SVN_IS_VALID_REVNUM(peg_revision))
    return svn_error_createf(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                             _("Invalid peg revision %ld"), peg_revision);
  if (!SVN_IS_VALID_REVNUM(end_revision))
    return svn_error_createf(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                             _("Invalid end revision %ld"), end_revision);
  if (end_revision <= peg_revision)
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                            _("Peg revision must precede end revision"));
  err = session->vtable->get_deleted_rev(session, path,
                                         peg_revision,
                                         end_revision,
                                         revision_deleted,
                                         pool);
  if (err && (err->apr_err == SVN_ERR_UNSUPPORTED_FEATURE))
    {
      svn_error_clear(err);

      /* Do it the slow way, using get-logs, for older servers. */
      err = svn_ra__get_deleted_rev_from_log(session, path, peg_revision,
                                             end_revision, revision_deleted,
                                             pool);
    }
  return err;
}

svn_error_t *
svn_ra_get_inherited_props(svn_ra_session_t *session,
                           apr_array_header_t **iprops,
                           const char *path,
                           svn_revnum_t revision,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  /* Path must be relative. */
  SVN_ERR_ASSERT(svn_relpath_is_canonical(path));

  err = session->vtable->get_inherited_props(session, iprops, path,
                                             revision, result_pool,
                                             scratch_pool);

  if (err && err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED)
    {
      svn_error_clear(err);

      /* Fallback for legacy servers. */
      SVN_ERR(svn_ra__get_inherited_props_walk(session, path, revision, iprops,
                                               result_pool, scratch_pool));
    }
  else
    SVN_ERR(err);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra__get_commit_ev2(svn_editor_t **editor,
                       svn_ra_session_t *session,
                       apr_hash_t *revprop_table,
                       svn_commit_callback2_t commit_callback,
                       void *commit_baton,
                       apr_hash_t *lock_tokens,
                       svn_boolean_t keep_locks,
                       svn_ra__provide_base_cb_t provide_base_cb,
                       svn_ra__provide_props_cb_t provide_props_cb,
                       svn_ra__get_copysrc_kind_cb_t get_copysrc_kind_cb,
                       void *cb_baton,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  if (session->vtable->get_commit_ev2 == NULL)
    {
      /* The specific RA layer does not have an implementation. Use our
         default shim over the normal commit editor.  */

      return svn_error_trace(svn_ra__use_commit_shim(
                               editor,
                               session,
                               revprop_table,
                               commit_callback, commit_baton,
                               lock_tokens,
                               keep_locks,
                               provide_base_cb,
                               provide_props_cb,
                               get_copysrc_kind_cb,
                               cb_baton,
                               session->cancel_func, session->cancel_baton,
                               result_pool, scratch_pool));
    }

  /* Note: no need to remap the callback for Ev2. RA layers providing this
     vtable entry should completely fill in commit_info.  */

  return svn_error_trace(session->vtable->get_commit_ev2(
                           editor,
                           session,
                           revprop_table,
                           commit_callback, commit_baton,
                           lock_tokens,
                           keep_locks,
                           provide_base_cb,
                           provide_props_cb,
                           get_copysrc_kind_cb,
                           cb_baton,
                           session->cancel_func, session->cancel_baton,
                           result_pool, scratch_pool));
}


svn_error_t *
svn_ra_print_modules(svn_stringbuf_t *output,
                     apr_pool_t *pool)
{
  const struct ra_lib_defn *defn;
  const char * const *schemes;
  svn_ra__init_func_t initfunc;
  const svn_ra__vtable_t *vtable;
  apr_pool_t *iterpool = svn_pool_create(pool);

  for (defn = ra_libraries; defn->ra_name != NULL; ++defn)
    {
      char *line;

      svn_pool_clear(iterpool);

      initfunc = defn->initfunc;
      if (! initfunc)
        SVN_ERR(load_ra_module(&initfunc, NULL, defn->ra_name,
                               iterpool));

      if (initfunc)
        {
          SVN_ERR(initfunc(svn_ra_version(), &vtable, iterpool));

          SVN_ERR(check_ra_version(vtable->get_version(), defn->ra_name));

          /* Note: if you change the formatting of the description,
             bear in mind that ra_svn's description has multiple lines when
             built with SASL. */
          line = apr_psprintf(iterpool, "* ra_%s : %s\n",
                              defn->ra_name,
                              vtable->get_description(iterpool));
          svn_stringbuf_appendcstr(output, line);

          for (schemes = vtable->get_schemes(iterpool); *schemes != NULL;
               ++schemes)
            {
              line = apr_psprintf(iterpool, _("  - handles '%s' scheme\n"),
                                  *schemes);
              svn_stringbuf_appendcstr(output, line);
            }
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_ra_print_ra_libraries(svn_stringbuf_t **descriptions,
                          void *ra_baton,
                          apr_pool_t *pool)
{
  *descriptions = svn_stringbuf_create_empty(pool);
  return svn_ra_print_modules(*descriptions, pool);
}


svn_error_t *
svn_ra__register_editor_shim_callbacks(svn_ra_session_t *session,
                                       svn_delta_shim_callbacks_t *callbacks)
{
  SVN_ERR(session->vtable->register_editor_shim_callbacks(session, callbacks));
  return SVN_NO_ERROR;
}


/* Return the library version number. */
const svn_version_t *
svn_ra_version(void)
{
  SVN_VERSION_BODY;
}


/*** Compatibility Interfaces **/
svn_error_t *
svn_ra_init_ra_libs(void **ra_baton,
                    apr_pool_t *pool)
{
  *ra_baton = pool;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_get_ra_library(svn_ra_plugin_t **library,
                      void *ra_baton,
                      const char *url,
                      apr_pool_t *pool)
{
  const struct ra_lib_defn *defn;
  apr_pool_t *load_pool = ra_baton;
  apr_hash_t *ht = apr_hash_make(pool);

  /* Figure out which RA library key matches URL. */
  for (defn = ra_libraries; defn->ra_name != NULL; ++defn)
    {
      const char *scheme;
      if ((scheme = has_scheme_of(defn->schemes, url)))
        {
          svn_ra_init_func_t compat_initfunc = defn->compat_initfunc;

          if (! compat_initfunc)
            {
              SVN_ERR(load_ra_module
                      (NULL, &compat_initfunc, defn->ra_name, load_pool));
            }
          if (! compat_initfunc)
            {
              continue;
            }

          SVN_ERR(compat_initfunc(SVN_RA_ABI_VERSION, load_pool, ht));

          *library = svn_hash_gets(ht, scheme);

          /* The library may support just a subset of the schemes listed,
             so we have to check here too. */
          if (! *library)
            break;

          return check_ra_version((*library)->get_version(), scheme);
        }
    }

  /* Couldn't find a match... */
  *library = NULL;
  return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                           _("Unrecognized URL scheme '%s'"), url);
}

/* For each libsvn_ra_foo library that is not linked in, provide a default
   implementation for svn_ra_foo_init which returns a "not implemented"
   error. */

#ifndef SVN_LIBSVN_RA_LINKS_RA_NEON
svn_error_t *
svn_ra_dav_init(int abi_version,
                apr_pool_t *pool,
                apr_hash_t *hash)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}
#endif /* ! SVN_LIBSVN_RA_LINKS_RA_NEON */

#ifndef SVN_LIBSVN_RA_LINKS_RA_SVN
svn_error_t *
svn_ra_svn_init(int abi_version,
                apr_pool_t *pool,
                apr_hash_t *hash)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}
#endif /* ! SVN_LIBSVN_RA_LINKS_RA_SVN */

#ifndef SVN_LIBSVN_RA_LINKS_RA_LOCAL
svn_error_t *
svn_ra_local_init(int abi_version,
                  apr_pool_t *pool,
                  apr_hash_t *hash)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}
#endif /* ! SVN_LIBSVN_RA_LINKS_RA_LOCAL */

#ifndef SVN_LIBSVN_RA_LINKS_RA_SERF
svn_error_t *
svn_ra_serf_init(int abi_version,
                 apr_pool_t *pool,
                 apr_hash_t *hash)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}
#endif /* ! SVN_LIBSVN_RA_LINKS_RA_SERF */
