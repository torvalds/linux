/*
 * config_auth.c :  authentication files in the user config area
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
#include "svn_io.h"
#include "svn_pools.h"
#include "config_impl.h"

#include "auth.h"

#include "svn_private_config.h"

#include "private/svn_auth_private.h"

svn_error_t *
svn_auth__file_path(const char **path,
                    const char *cred_kind,
                    const char *realmstring,
                    const char *config_dir,
                    apr_pool_t *pool)
{
  const char *authdir_path, *hexname;
  svn_checksum_t *checksum;

  /* Construct the path to the directory containing the creds files,
     e.g. "~/.subversion/auth/svn.simple".  The last component is
     simply the cred_kind.  */
  SVN_ERR(svn_config_get_user_config_path(&authdir_path, config_dir,
                                          SVN_CONFIG__AUTH_SUBDIR, pool));
  if (authdir_path)
    {
      authdir_path = svn_dirent_join(authdir_path, cred_kind, pool);

      /* Construct the basename of the creds file.  It's just the
         realmstring converted into an md5 hex string.  */
      SVN_ERR(svn_checksum(&checksum, svn_checksum_md5, realmstring,
                           strlen(realmstring), pool));
      hexname = svn_checksum_to_cstring(checksum, pool);

      *path = svn_dirent_join(authdir_path, hexname, pool);
    }
  else
    *path = NULL;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_config_read_auth_data(apr_hash_t **hash,
                          const char *cred_kind,
                          const char *realmstring,
                          const char *config_dir,
                          apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *auth_path;

  *hash = NULL;

  SVN_ERR(svn_auth__file_path(&auth_path, cred_kind, realmstring, config_dir,
                              pool));
  if (! auth_path)
    return SVN_NO_ERROR;

  SVN_ERR(svn_io_check_path(auth_path, &kind, pool));
  if (kind == svn_node_file)
    {
      svn_stream_t *stream;
      svn_string_t *stored_realm;

      SVN_ERR_W(svn_stream_open_readonly(&stream, auth_path, pool, pool),
                _("Unable to open auth file for reading"));

      *hash = apr_hash_make(pool);

      SVN_ERR_W(svn_hash_read2(*hash, stream, SVN_HASH_TERMINATOR, pool),
                apr_psprintf(pool, _("Error parsing '%s'"),
                             svn_dirent_local_style(auth_path, pool)));

      stored_realm = svn_hash_gets(*hash, SVN_CONFIG_REALMSTRING_KEY);

      if (!stored_realm || strcmp(stored_realm->data, realmstring) != 0)
        *hash = NULL; /* Hash collision, or somebody tampering with storage */

      SVN_ERR(svn_stream_close(stream));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_config_write_auth_data(apr_hash_t *hash,
                           const char *cred_kind,
                           const char *realmstring,
                           const char *config_dir,
                           apr_pool_t *pool)
{
  svn_stream_t *stream;
  const char *auth_path, *tmp_path;

  SVN_ERR(svn_auth__file_path(&auth_path, cred_kind, realmstring, config_dir,
                              pool));
  if (! auth_path)
    return svn_error_create(SVN_ERR_NO_AUTH_FILE_PATH, NULL,
                            _("Unable to locate auth file"));

  /* Add the realmstring to the hash, so programs (or users) can
     verify exactly which set of credentials this file holds.
     ### What if realmstring key is already in the hash? */
  svn_hash_sets(hash, SVN_CONFIG_REALMSTRING_KEY,
                svn_string_create(realmstring, pool));

  SVN_ERR_W(svn_stream_open_unique(&stream, &tmp_path,
                                   svn_dirent_dirname(auth_path, pool),
                                   svn_io_file_del_on_pool_cleanup,
                                   pool, pool),
            _("Unable to open auth file for writing"));
  SVN_ERR_W(svn_hash_write2(hash, stream, SVN_HASH_TERMINATOR, pool),
            apr_psprintf(pool, _("Error writing hash to '%s'"),
                         svn_dirent_local_style(auth_path, pool)));
  SVN_ERR(svn_stream_close(stream));
  SVN_ERR(svn_io_file_rename2(tmp_path, auth_path, FALSE, pool));

  /* To be nice, remove the realmstring from the hash again, just in
     case the caller wants their hash unchanged.
     ### Should we also do this when a write error occurs? */
  svn_hash_sets(hash, SVN_CONFIG_REALMSTRING_KEY, NULL);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_config_walk_auth_data(const char *config_dir,
                          svn_config_auth_walk_func_t walk_func,
                          void *walk_baton,
                          apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool;
  svn_boolean_t finished = FALSE;
  const char *cred_kinds[] =
    {
      SVN_AUTH_CRED_SIMPLE,
      SVN_AUTH_CRED_USERNAME,
      SVN_AUTH_CRED_SSL_CLIENT_CERT,
      SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
      SVN_AUTH_CRED_SSL_SERVER_TRUST,
      NULL
    };

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; cred_kinds[i]; i++)
    {
      const char *item_path;
      const char *dir_path;
      apr_hash_t *nodes;
      svn_error_t *err;
      apr_pool_t *itempool;
      apr_hash_index_t *hi;

      svn_pool_clear(iterpool);

      if (finished)
        break;

      SVN_ERR(svn_auth__file_path(&item_path, cred_kinds[i], "!", config_dir,
                                  iterpool));

      dir_path = svn_dirent_dirname(item_path, iterpool);

      err = svn_io_get_dirents3(&nodes, dir_path, TRUE, iterpool, iterpool);
      if (err)
        {
          if (!APR_STATUS_IS_ENOENT(err->apr_err)
              && !SVN__APR_STATUS_IS_ENOTDIR(err->apr_err))
            return svn_error_trace(err);

          svn_error_clear(err);
          continue;
        }

      itempool = svn_pool_create(iterpool);
      for (hi = apr_hash_first(iterpool, nodes); hi; hi = apr_hash_next(hi))
        {
          svn_io_dirent2_t *dirent = apr_hash_this_val(hi);
          svn_stream_t *stream;
          apr_hash_t *creds_hash;
          const svn_string_t *realm;
          svn_boolean_t delete_file = FALSE;

          if (finished)
            break;

          if (dirent->kind != svn_node_file)
            continue;

          svn_pool_clear(itempool);

          item_path = svn_dirent_join(dir_path, apr_hash_this_key(hi),
                                      itempool);

          err = svn_stream_open_readonly(&stream, item_path,
                                         itempool, itempool);
          if (err)
            {
              /* Ignore this file. There are no credentials in it anyway */
              svn_error_clear(err);
              continue;
            }

          creds_hash = apr_hash_make(itempool);
          err = svn_hash_read2(creds_hash, stream,
                               SVN_HASH_TERMINATOR, itempool);
          err = svn_error_compose_create(err, svn_stream_close(stream));
          if (err)
            {
              /* Ignore this file. There are no credentials in it anyway */
              svn_error_clear(err);
              continue;
            }

          realm = svn_hash_gets(creds_hash, SVN_CONFIG_REALMSTRING_KEY);
          if (! realm)
            continue; /* Not an auth file */

          err = walk_func(&delete_file, walk_baton, cred_kinds[i],
                          realm->data, creds_hash, itempool);
          if (err && err->apr_err == SVN_ERR_CEASE_INVOCATION)
            {
              svn_error_clear(err);
              err = SVN_NO_ERROR;
              finished = TRUE;
            }
          SVN_ERR(err);

          if (delete_file)
            {
              /* Delete the file on disk */
              SVN_ERR(svn_io_remove_file2(item_path, TRUE, itempool));
            }
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}
