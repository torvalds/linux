/*
 * util.c: Subversion command line client utility functions. Any
 * functions that need to be shared across subcommands should be put
 * in here.
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

#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "svn_private_config.h"
#include "svn_error.h"
#include "svn_path.h"

#include "cl.h"



svn_error_t *
svn_cl__args_to_target_array_print_reserved(apr_array_header_t **targets,
                                            apr_getopt_t *os,
                                            const apr_array_header_t *known_targets,
                                            svn_client_ctx_t *ctx,
                                            svn_boolean_t keep_last_origpath_on_truepath_collision,
                                            apr_pool_t *pool)
{
  svn_error_t *err = svn_client_args_to_target_array2(targets,
                                                      os,
                                                      known_targets,
                                                      ctx,
                                                      keep_last_origpath_on_truepath_collision,
                                                      pool);
  if (err)
    {
      if (err->apr_err ==  SVN_ERR_RESERVED_FILENAME_SPECIFIED)
        {
          svn_handle_error2(err, stderr, FALSE, "svn: Skipping argument: ");
          svn_error_clear(err);
        }
      else
        return svn_error_trace(err);
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_cl__check_target_is_local_path(const char *target)
{
  if (svn_path_is_url(target))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("'%s' is not a local path"), target);
  return SVN_NO_ERROR;
}

const char *
svn_cl__local_style_skip_ancestor(const char *parent_path,
                                  const char *path,
                                  apr_pool_t *pool)
{
  const char *relpath = NULL;

  if (parent_path)
    relpath = svn_dirent_skip_ancestor(parent_path, path);

  return svn_dirent_local_style(relpath ? relpath : path, pool);
}

