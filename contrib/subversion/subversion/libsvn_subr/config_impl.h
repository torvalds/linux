/*
 * config_impl.h :  private header for the config file implementation.
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



#ifndef SVN_LIBSVN_SUBR_CONFIG_IMPL_H
#define SVN_LIBSVN_SUBR_CONFIG_IMPL_H

#define APR_WANT_STDIO
#include <apr_want.h>

#include <apr_hash.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_io.h"
#include "svn_config.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* The configuration data. This is a superhash of sections and options. */
struct svn_config_t
{
  /* Table of cfg_section_t's. */
  apr_hash_t *sections;

  /* Pool for hash tables, table entries and unexpanded values.
     Also, parent pool for temporary pools. */
  apr_pool_t *pool;

  /* Pool for expanded values -- this is separate, so that we can
     clear it when modifying the config data. */
  apr_pool_t *x_pool;

  /* Indicates that some values in the configuration have been expanded. */
  svn_boolean_t x_values;

  /* Temporary string used for lookups.  (Using a stringbuf so that
     frequent resetting is efficient.) */
  svn_stringbuf_t *tmp_key;

  /* Temporary value used for expanded default values in svn_config_get.
     (Using a stringbuf so that frequent resetting is efficient.) */
  svn_stringbuf_t *tmp_value;

  /* Specifies whether section names are populated case sensitively. */
  svn_boolean_t section_names_case_sensitive;

  /* Specifies whether option names are populated case sensitively. */
  svn_boolean_t option_names_case_sensitive;

  /* When set, all modification attempts will be ignored.
   * In debug mode, we will trigger an assertion. */
  svn_boolean_t read_only;
};

/* The default add-value constructor callback, used by the default
   config parser that populates an svn_config_t. */
svn_error_t *svn_config__default_add_value_fn(
    void *baton, svn_stringbuf_t *section,
    svn_stringbuf_t *option, svn_stringbuf_t *value);

/* Read sections and options from a file. */
svn_error_t *svn_config__parse_file(svn_config_t *cfg,
                                    const char *file,
                                    svn_boolean_t must_exist,
                                    apr_pool_t *pool);

/* The name of the magic [DEFAULT] section. */
#define SVN_CONFIG__DEFAULT_SECTION "DEFAULT"


#ifdef WIN32
/* Get the common or user-specific AppData folder */
svn_error_t *svn_config__win_config_path(const char **folder,
                                         svn_boolean_t system_path,
                                         apr_pool_t *result_pool,
                                         apr_pool_t *scratch_pool);

/* Read sections and options from the Windows Registry. */
svn_error_t *svn_config__parse_registry(svn_config_t *cfg,
                                        const char *file,
                                        svn_boolean_t must_exist,
                                        apr_pool_t *pool);

/* ### It's unclear to me whether this registry stuff should get the
   double underscore or not, and if so, where the extra underscore
   would go.  Thoughts?  -kff */
#  define SVN_REGISTRY_PREFIX "REGISTRY:"
#  define SVN_REGISTRY_PREFIX_LEN ((sizeof(SVN_REGISTRY_PREFIX)) - 1)
#  define SVN_REGISTRY_HKLM "HKLM\\"
#  define SVN_REGISTRY_HKLM_LEN ((sizeof(SVN_REGISTRY_HKLM)) - 1)
#  define SVN_REGISTRY_HKCU "HKCU\\"
#  define SVN_REGISTRY_HKCU_LEN ((sizeof(SVN_REGISTRY_HKCU)) - 1)
#  define SVN_REGISTRY_PATH "Software\\Tigris.org\\Subversion\\"
#  define SVN_REGISTRY_PATH_LEN ((sizeof(SVN_REGISTRY_PATH)) - 1)
#  define SVN_REGISTRY_SYS_CONFIG_PATH \
                               SVN_REGISTRY_PREFIX     \
                               SVN_REGISTRY_HKLM       \
                               SVN_REGISTRY_PATH
#  define SVN_REGISTRY_USR_CONFIG_PATH \
                               SVN_REGISTRY_PREFIX     \
                               SVN_REGISTRY_HKCU       \
                               SVN_REGISTRY_PATH
#endif /* WIN32 */

/* System-wide and configuration subdirectory names.
   NOTE: Don't use these directly; call svn_config__sys_config_path()
   or svn_config_get_user_config_path() instead. */
#ifdef WIN32
#  define SVN_CONFIG__SUBDIRECTORY    "Subversion"
#elif defined __HAIKU__ /* HAIKU */
#  define SVN_CONFIG__SYS_DIRECTORY   "subversion"
#  define SVN_CONFIG__USR_DIRECTORY   "subversion"
#else  /* ! WIN32 && ! __HAIKU__ */
#  define SVN_CONFIG__SYS_DIRECTORY   "/etc/subversion"
#  define SVN_CONFIG__USR_DIRECTORY   ".subversion"
#endif /* WIN32 */

/* The description/instructions file in the config directory. */
#define SVN_CONFIG__USR_README_FILE    "README.txt"

/* The name of the main authentication subdir in the config directory */
#define SVN_CONFIG__AUTH_SUBDIR        "auth"

/* Set *PATH_P to the path to config file FNAME in the system
   configuration area, allocated in POOL.  If FNAME is NULL, set
   *PATH_P to the directory name of the system config area, either
   allocated in POOL or a static constant string.

   If the system configuration area cannot be located (possible under
   Win32), set *PATH_P to NULL regardless of FNAME.  */
svn_error_t *
svn_config__sys_config_path(const char **path_p,
                            const char *fname,
                            apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_SUBR_CONFIG_IMPL_H */
