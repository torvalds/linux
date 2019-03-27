/*
 * version.c:  library version number and utilities
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



#include "svn_error.h"
#include "svn_version.h"

#include "sysinfo.h"
#include "svn_private_config.h"
#include "private/svn_subr_private.h"

const svn_version_t *
svn_subr_version(void)
{
  SVN_VERSION_BODY;
}


svn_boolean_t svn_ver_compatible(const svn_version_t *my_version,
                                 const svn_version_t *lib_version)
{
  /* With normal development builds the matching rules are stricter
     that for release builds, to avoid inadvertantly using the wrong
     libraries.  For backward compatibility testing of development
     builds one can use --disable-full-version-match to cause a
     development build to use the release build rules.  This allows
     the libraries from the newer development build to be used by an
     older development build. */

#ifndef SVN_DISABLE_FULL_VERSION_MATCH
  if (lib_version->tag[0] != '\0')
    /* Development library; require exact match. */
    return svn_ver_equal(my_version, lib_version);
  else if (my_version->tag[0] != '\0')
    /* Development client; must be newer than the library
       and have the same major and minor version. */
    return (my_version->major == lib_version->major
            && my_version->minor == lib_version->minor
            && my_version->patch > lib_version->patch);
#endif

  /* General compatibility rules for released versions. */
  return (my_version->major == lib_version->major
          && my_version->minor <= lib_version->minor);
}


svn_boolean_t svn_ver_equal(const svn_version_t *my_version,
                            const svn_version_t *lib_version)
{
  return (my_version->major == lib_version->major
          && my_version->minor == lib_version->minor
          && my_version->patch == lib_version->patch
          && 0 == strcmp(my_version->tag, lib_version->tag));
}


svn_error_t *
svn_ver_check_list2(const svn_version_t *my_version,
                    const svn_version_checklist_t *checklist,
                    svn_boolean_t (*comparator)(const svn_version_t *,
                                                const svn_version_t *))
{
  svn_error_t *err = SVN_NO_ERROR;
  int i;

#ifdef SVN_DISABLE_FULL_VERSION_MATCH
  /* Force more relaxed check for --disable-full-version-match. */
  comparator = svn_ver_compatible;
#endif

  for (i = 0; checklist[i].label != NULL; ++i)
    {
      const svn_version_t *lib_version = checklist[i].version_query();
      if (!comparator(my_version, lib_version))
        err = svn_error_createf(SVN_ERR_VERSION_MISMATCH, err,
                                _("Version mismatch in '%s'%s:"
                                  " found %d.%d.%d%s,"
                                  " expected %d.%d.%d%s"),
                                checklist[i].label,
                                comparator == svn_ver_equal
                                ? _(" (expecting equality)")
                                : comparator == svn_ver_compatible
                                ? _(" (expecting compatibility)")
                                : "",
                                lib_version->major, lib_version->minor,
                                lib_version->patch, lib_version->tag,
                                my_version->major, my_version->minor,
                                my_version->patch, my_version->tag);
    }

  return err;
}


struct svn_version_extended_t
{
  const char *build_date;       /* Compilation date */
  const char *build_time;       /* Compilation time */
  const char *build_host;       /* Build canonical host name */
  const char *copyright;        /* Copyright notice (localized) */
  const char *runtime_host;     /* Runtime canonical host name */
  const char *runtime_osname;   /* Running OS release name */

  /* Array of svn_version_ext_linked_lib_t describing dependent
     libraries. */
  const apr_array_header_t *linked_libs;

  /* Array of svn_version_ext_loaded_lib_t describing loaded shared
     libraries. */
  const apr_array_header_t *loaded_libs;
};


const svn_version_extended_t *
svn_version_extended(svn_boolean_t verbose,
                     apr_pool_t *pool)
{
  svn_version_extended_t *info = apr_pcalloc(pool, sizeof(*info));

  info->build_date = NULL;
  info->build_time = NULL;
  info->build_host = SVN_BUILD_HOST;
  info->copyright = apr_pstrdup
    (pool, _("Copyright (C) 2018 The Apache Software Foundation.\n"
             "This software consists of contributions made by many people;\n"
             "see the NOTICE file for more information.\n"
             "Subversion is open source software, see "
             "http://subversion.apache.org/\n"));

  if (verbose)
    {
      info->runtime_host = svn_sysinfo__canonical_host(pool);
      info->runtime_osname = svn_sysinfo__release_name(pool);
      info->linked_libs = svn_sysinfo__linked_libs(pool);
      info->loaded_libs = svn_sysinfo__loaded_libs(pool);
    }

  return info;
}


const char *
svn_version_ext_build_date(const svn_version_extended_t *ext_info)
{
  return ext_info->build_date;
}

const char *
svn_version_ext_build_time(const svn_version_extended_t *ext_info)
{
  return ext_info->build_time;
}

const char *
svn_version_ext_build_host(const svn_version_extended_t *ext_info)
{
  return ext_info->build_host;
}

const char *
svn_version_ext_copyright(const svn_version_extended_t *ext_info)
{
  return ext_info->copyright;
}

const char *
svn_version_ext_runtime_host(const svn_version_extended_t *ext_info)
{
  return ext_info->runtime_host;
}

const char *
svn_version_ext_runtime_osname(const svn_version_extended_t *ext_info)
{
  return ext_info->runtime_osname;
}

const apr_array_header_t *
svn_version_ext_linked_libs(const svn_version_extended_t *ext_info)
{
  return ext_info->linked_libs;
}

const apr_array_header_t *
svn_version_ext_loaded_libs(const svn_version_extended_t *ext_info)
{
  return ext_info->loaded_libs;
}

svn_error_t *
svn_version__parse_version_string(svn_version_t **version_p,
                                  const char *version_string,
                                  apr_pool_t *result_pool)
{
  svn_error_t *err;
  svn_version_t *version;
  apr_array_header_t *pieces =
    svn_cstring_split(version_string, ".", FALSE, result_pool);

  if ((pieces->nelts < 2) || (pieces->nelts > 3))
    return svn_error_createf(SVN_ERR_MALFORMED_VERSION_STRING, NULL,
                             _("Failed to parse version number string '%s'"),
                             version_string);

  version = apr_pcalloc(result_pool, sizeof(*version));
  version->tag = "";

  /* Parse the major and minor integers strictly. */
  err = svn_cstring_atoi(&(version->major),
                         APR_ARRAY_IDX(pieces, 0, const char *));
  if (err)
    return svn_error_createf(SVN_ERR_MALFORMED_VERSION_STRING, err,
                             _("Failed to parse version number string '%s'"),
                             version_string);
  err = svn_cstring_atoi(&(version->minor),
                         APR_ARRAY_IDX(pieces, 1, const char *));
  if (err)
    return svn_error_createf(SVN_ERR_MALFORMED_VERSION_STRING, err,
                             _("Failed to parse version number string '%s'"),
                             version_string);

  /* If there's a third component, we'll parse it, too.  But we don't
     require that it be present. */
  if (pieces->nelts == 3)
    {
      const char *piece = APR_ARRAY_IDX(pieces, 2, const char *);
      char *hyphen = strchr(piece, '-');
      if (hyphen)
        {
          version->tag = apr_pstrdup(result_pool, hyphen + 1);
          *hyphen = '\0';
        }
      err = svn_cstring_atoi(&(version->patch), piece);
      if (err)
        return svn_error_createf(SVN_ERR_MALFORMED_VERSION_STRING, err,
                                 _("Failed to parse version number string '%s'"
                                  ),
                                 version_string);
    }

  if (version->major < 0 || version->minor < 0 || version->patch < 0)
    return svn_error_createf(SVN_ERR_MALFORMED_VERSION_STRING, err,
                             _("Failed to parse version number string '%s'"),
                             version_string);

  *version_p = version;
  return SVN_NO_ERROR;
}


svn_boolean_t
svn_version__at_least(const svn_version_t *version,
                      int major,
                      int minor,
                      int patch)
{
  /* Compare major versions. */
  if (version->major < major)
    return FALSE;
  if (version->major > major)
    return TRUE;

  /* Major versions are the same.  Compare minor versions. */
  if (version->minor < minor)
    return FALSE;
  if (version->minor > minor)
    return TRUE;

  /* Major and minor versions are the same.  Compare patch
     versions. */
  if (version->patch < patch)
    return FALSE;
  if (version->patch > patch)
    return TRUE;

  /* Major, minor, and patch versions are identical matches.  But tags
     in our schema are always used for versions not yet quite at the
     given patch level. */
  if (version->tag && version->tag[0])
    return FALSE;

  return TRUE;
}
