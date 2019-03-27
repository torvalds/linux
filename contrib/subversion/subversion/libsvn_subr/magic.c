/*
 * magic.c:  wrappers around libmagic
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

#include <apr_lib.h>
#include <apr_file_info.h>

#include "svn_io.h"
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_config.h"
#include "svn_hash.h"

#include "svn_private_config.h"

#include "private/svn_magic.h"

#ifdef SVN_HAVE_LIBMAGIC
#include <magic.h>
#endif

struct svn_magic__cookie_t {
#ifdef SVN_HAVE_LIBMAGIC
  magic_t magic;
#else
  char dummy;
#endif
};

#ifdef SVN_HAVE_LIBMAGIC
/* Close the magic database. */
static apr_status_t
close_magic_cookie(void *baton)
{
  svn_magic__cookie_t *mc = (svn_magic__cookie_t*)baton;
  magic_close(mc->magic);
  return APR_SUCCESS;
}
#endif

svn_error_t *
svn_magic__init(svn_magic__cookie_t **magic_cookie,
                apr_hash_t *config,
                apr_pool_t *result_pool)
{
  svn_magic__cookie_t *mc = NULL;

#ifdef SVN_HAVE_LIBMAGIC
  if (config)
    {
      svn_boolean_t enable;
      svn_config_t *cfg = svn_hash_gets(config, SVN_CONFIG_CATEGORY_CONFIG);

      SVN_ERR(svn_config_get_bool(cfg, &enable,
                                  SVN_CONFIG_SECTION_MISCELLANY,
                                  SVN_CONFIG_OPTION_ENABLE_MAGIC_FILE,
                                  TRUE));
      if (!enable)
        {
          *magic_cookie = NULL;
          return SVN_NO_ERROR;
        }
    }

  mc = apr_palloc(result_pool, sizeof(*mc));

  /* Initialise libmagic. */
#ifndef MAGIC_MIME_TYPE
  /* Some old versions of libmagic don't support MAGIC_MIME_TYPE.
   * We can use MAGIC_MIME instead. It returns more than we need
   * but we can work around that (see below). */
  mc->magic = magic_open(MAGIC_MIME | MAGIC_ERROR);
#else
  mc->magic = magic_open(MAGIC_MIME_TYPE | MAGIC_ERROR);
#endif
  if (mc->magic)
    {
      /* This loads the default magic database.
       * Point the MAGIC environment variable at your favourite .mgc
       * file to load a non-default database. */
      if (magic_load(mc->magic, NULL) == -1)
        {
          magic_close(mc->magic);
          mc = NULL;
        }
      else
        apr_pool_cleanup_register(result_pool, mc, close_magic_cookie,
                                  apr_pool_cleanup_null);
    }
#endif

  *magic_cookie = mc;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_magic__detect_binary_mimetype(const char **mimetype,
                                  const char *local_abspath,
                                  svn_magic__cookie_t *magic_cookie,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  const char *magic_mimetype = NULL;
#ifdef SVN_HAVE_LIBMAGIC
  apr_finfo_t finfo;

  /* Do not ask libmagic for the mime-types of empty files.
   * This prevents mime-types like "application/x-empty" from making
   * Subversion treat empty files as binary. */
  SVN_ERR(svn_io_stat(&finfo, local_abspath, APR_FINFO_SIZE, scratch_pool));
  if (finfo.size > 0)
    {
      magic_mimetype = magic_file(magic_cookie->magic, local_abspath);
      if (magic_mimetype)
        {
          /* Only return binary mime-types. */
          if (strncmp(magic_mimetype, "text/", 5) == 0)
            magic_mimetype = NULL;
          else
            {
              svn_error_t *err;
#ifndef MAGIC_MIME_TYPE
              char *p;

              /* Strip off trailing stuff like " charset=ascii". */
              p = strchr(magic_mimetype, ' ');
              if (p)
                *p = '\0';
#endif
              /* Make sure we got a valid mime type. */
              err = svn_mime_type_validate(magic_mimetype, scratch_pool);
              if (err)
                {
                  if (err->apr_err == SVN_ERR_BAD_MIME_TYPE)
                    {
                      svn_error_clear(err);
                      magic_mimetype = NULL;
                    }
                  else
                    return svn_error_trace(err);
                }
              else
                {
                  /* The string is allocated from memory managed by libmagic
                   * so we must copy it to the result pool. */
                  magic_mimetype = apr_pstrdup(result_pool, magic_mimetype);
                }
            }
        }
    }
#endif

  *mimetype = magic_mimetype;
  return SVN_NO_ERROR;
}
