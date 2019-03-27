/*
 * nls.c :  Helpers for NLS programs.
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

#include <stdlib.h>

#ifndef WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <apr_errno.h>

#include "svn_nls.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_path.h"

#include "private/svn_utf_private.h"

#include "svn_private_config.h"

svn_error_t *
svn_nls_init(void)
{
  svn_error_t *err = SVN_NO_ERROR;

#ifdef ENABLE_NLS
  if (getenv("SVN_LOCALE_DIR"))
    {
      bindtextdomain(PACKAGE_NAME, getenv("SVN_LOCALE_DIR"));
    }
  else
    {
#ifdef WIN32
      WCHAR ucs2_path[MAX_PATH];
      const char* utf8_path;
      const char* internal_path;
      apr_pool_t* scratch_pool;

      scratch_pool = svn_pool_create(NULL);
      /* get exe name - our locale info will be in '../share/locale' */
      GetModuleFileNameW(NULL, ucs2_path,
          sizeof(ucs2_path) / sizeof(ucs2_path[0]));
      if (apr_get_os_error())
        {
          err = svn_error_wrap_apr(apr_get_os_error(),
                                   _("Can't get module file name"));
        }

      if (! err)
        err = svn_utf__win32_utf16_to_utf8(&utf8_path, ucs2_path,
                                           NULL, scratch_pool);

      if (! err)
        {
          internal_path = svn_dirent_internal_style(utf8_path, scratch_pool);
          /* get base path name */
          internal_path = svn_dirent_dirname(internal_path, scratch_pool);
          internal_path = svn_dirent_join(internal_path,
                                          SVN_LOCALE_RELATIVE_PATH,
                                          scratch_pool);
          SVN_ERR(svn_dirent_get_absolute(&internal_path, internal_path,
                                          scratch_pool));
          bindtextdomain(PACKAGE_NAME, internal_path);
        }

      svn_pool_destroy(scratch_pool);
    }
#else /* ! WIN32 */
      bindtextdomain(PACKAGE_NAME, SVN_LOCALE_DIR);
    }
#endif /* WIN32 */

#ifdef HAVE_BIND_TEXTDOMAIN_CODESET
  bind_textdomain_codeset(PACKAGE_NAME, "UTF-8");
#endif /* HAVE_BIND_TEXTDOMAIN_CODESET */

#endif /* ENABLE_NLS */

  return err;
}
