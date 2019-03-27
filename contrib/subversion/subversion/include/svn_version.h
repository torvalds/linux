/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_version.h
 * @brief Version information.
 */

#ifndef SVN_VERSION_H
#define SVN_VERSION_H

/* Hack to prevent the resource compiler from including
   apr and other headers. */
#ifndef SVN_WIN32_RESOURCE_COMPILATION
#include <apr_general.h>
#include <apr_tables.h>

#include "svn_types.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Symbols that define the version number. */

/* Version numbers: <major>.<minor>.<micro>
 *
 * The version numbers in this file follow the rules established by:
 *
 *   http://apr.apache.org/versioning.html
 */

/** Major version number.
 *
 * Modify when incompatible changes are made to published interfaces.
 */
#define SVN_VER_MAJOR      1

/** Minor version number.
 *
 * Modify when new functionality is added or new interfaces are
 * defined, but all changes are backward compatible.
 */
#define SVN_VER_MINOR      10

/**
 * Patch number.
 *
 * Modify for every released patch.
 *
 * @since New in 1.1.
 */
#define SVN_VER_PATCH      0


/** @deprecated Provided for backward compatibility with the 1.0 API. */
#define SVN_VER_MICRO      SVN_VER_PATCH

/** @deprecated Provided for backward compatibility with the 1.0 API. */
#define SVN_VER_LIBRARY    SVN_VER_MAJOR


/** Version tag: a string describing the version.
 *
 * This tag remains " (under development)" in the repository so that we can
 * always see from "svn --version" that the software has been built
 * from the repository rather than a "blessed" distribution.
 *
 * When rolling a tarball, we automatically replace this text with " (r1234)"
 * (where 1234 is the last revision on the branch prior to the release)
 * for final releases; in prereleases, it becomes " (Alpha 1)",
 * " (Beta 1)", etc., as appropriate.
 *
 * Always change this at the same time as SVN_VER_NUMTAG.
 */
#define SVN_VER_TAG        " (r1827917)"


/** Number tag: a string describing the version.
 *
 * This tag is used to generate a version number string to identify
 * the client and server in HTTP requests, for example. It must not
 * contain any spaces. This value remains "-dev" in the repository.
 *
 * When rolling a tarball, we automatically replace this text with ""
 * for final releases; in prereleases, it becomes "-alpha1, "-beta1",
 * etc., as appropriate.
 *
 * Always change this at the same time as SVN_VER_TAG.
 */
#define SVN_VER_NUMTAG     ""


/** Revision number: The repository revision number of this release.
 *
 * This constant is used to generate the build number part of the Windows
 * file version. Its value remains 0 in the repository except in release
 * tags where it is the revision from which the tag was created.
 */
#define SVN_VER_REVISION   1827917


/* Version strings composed from the above definitions. */

/** Version number */
#define SVN_VER_NUM        APR_STRINGIFY(SVN_VER_MAJOR) \
                           "." APR_STRINGIFY(SVN_VER_MINOR) \
                           "." APR_STRINGIFY(SVN_VER_PATCH)

/** Version number with tag (contains no whitespace) */
#define SVN_VER_NUMBER     SVN_VER_NUM SVN_VER_NUMTAG

/** Complete version string */
#define SVN_VERSION        SVN_VER_NUMBER SVN_VER_TAG



/* Version queries and compatibility checks */

/**
 * Version information. Each library contains a function called
 * svn_<i>libname</i>_version() that returns a pointer to a statically
 * allocated object of this type.
 *
 * @since New in 1.1.
 */
struct svn_version_t
{
  int major;                    /**< Major version number */
  int minor;                    /**< Minor version number */
  int patch;                    /**< Patch number */

  /**
   * The version tag (#SVN_VER_NUMTAG). Must always point to a
   * statically allocated string.
   */
  const char *tag;
};

/**
 * Define a static svn_version_t object.
 *
 * @since New in 1.1.
 */
#define SVN_VERSION_DEFINE(name) \
  static const svn_version_t name = \
    { \
      SVN_VER_MAJOR, \
      SVN_VER_MINOR, \
      SVN_VER_PATCH, \
      SVN_VER_NUMTAG \
    } \

/**
 * Generate the implementation of a version query function.
 *
 * @since New in 1.1.
 * @since Since 1.9, embeds a string into the compiled object
 *        file that can be queried with the 'what' utility.
 */
#define SVN_VERSION_BODY            \
  static struct versioninfo_t       \
    {                               \
      const char *const str;        \
      const svn_version_t num;      \
    } const versioninfo =           \
    {                               \
      "@(#)" SVN_VERSION,           \
      {                             \
        SVN_VER_MAJOR,              \
        SVN_VER_MINOR,              \
        SVN_VER_PATCH,              \
        SVN_VER_NUMTAG              \
      }                             \
    };                              \
  return &versioninfo.num

/**
 * Check library version compatibility. Return #TRUE if the client's
 * version, given in @a my_version, is compatible with the library
 * version, provided in @a lib_version.
 *
 * This function checks for version compatibility as per our
 * guarantees, but requires an exact match when linking to an
 * unreleased library. A development client is always compatible with
 * a previous released library.
 *
 * @note Implements the #svn_ver_check_list2.@a comparator interface.
 *
 * @since New in 1.1.
 */
svn_boolean_t
svn_ver_compatible(const svn_version_t *my_version,
                   const svn_version_t *lib_version);

/**
 * Check if @a my_version and @a lib_version encode the same version number.
 *
 * @note Implements the #svn_ver_check_list2.@a comparator interface.
 *
 * @since New in 1.2.
 */
svn_boolean_t
svn_ver_equal(const svn_version_t *my_version,
              const svn_version_t *lib_version);


/**
 * An entry in the compatibility checklist.
 * @see svn_ver_check_list()
 *
 * @since New in 1.1.
 */
typedef struct svn_version_checklist_t
{
  const char *label;            /**< Entry label */

  /** Version query function for this entry */
  const svn_version_t *(*version_query)(void);
} svn_version_checklist_t;


/**
 * Perform a series of version compatibility checks. Checks if @a
 * my_version is compatible with each entry in @a checklist. @a
 * checklist must end with an entry whose label is @c NULL.
 *
 * @a my_version is considered to be compatible with a version in @a checklist
 * if @a comparator returns #TRUE when called with @a my_version as the first
 * parammeter and the @a checklist version as the second parameter.
 *
 * @see svn_ver_compatible(), svn_ver_equal()
 *
 * @note Subversion's own code invariably uses svn_ver_equal() as @a comparator,
 * since the cmdline tools sometimes use non-public APIs (such as utility
 * functions that haven't been promoted to svn_cmdline.h).  Third-party code
 * SHOULD use svn_ver_compatible() as @a comparator.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_ver_check_list2(const svn_version_t *my_version,
                    const svn_version_checklist_t *checklist,
                    svn_boolean_t (*comparator)(const svn_version_t *,
                                                const svn_version_t *));

/** Similar to svn_ver_check_list2(), with @a comparator set to
 * #svn_ver_compatible.
 *
 * @deprecated Provided for backward compatibility with 1.8 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_ver_check_list(const svn_version_t *my_version,
                   const svn_version_checklist_t *checklist);


/**
 * Type of function returning library version.
 *
 * @since New in 1.6.
 */
typedef const svn_version_t *(*svn_version_func_t)(void);


/* libsvn_subr doesn't have an svn_subr header, so put the prototype here. */
/**
 * Get libsvn_subr version information.
 *
 * @since New in 1.1.
 */
const svn_version_t *
svn_subr_version(void);


/**
 * Extended version information, including info about the running system.
 *
 * @since New in 1.8.
 */
typedef struct svn_version_extended_t svn_version_extended_t;

/**
 * Return version information for the running program.  If @a verbose
 * is #TRUE, collect extra information that may be expensive to
 * retrieve (for example, the OS release name, list of shared
 * libraries, etc.).  Use @a pool for all allocations.
 *
 * @note This function may allocate significant auxiliary resources
 * (memory and file descriptors) in @a pool.  It is recommended to
 * copy the returned data to suitable longer-lived memory and clear
 * @a pool after calling this function.
 *
 * @since New in 1.8.
 */
const svn_version_extended_t *
svn_version_extended(svn_boolean_t verbose,
                     apr_pool_t *pool);


/**
 * Accessor for svn_version_extended_t.
 *
 * @return The date when the libsvn_subr library was compiled, in the
 * format defined by the C standard macro @c __DATE__.
 *
 * @since New in 1.8.
 */
const char *
svn_version_ext_build_date(const svn_version_extended_t *ext_info);

/**
 * Accessor for svn_version_extended_t.
 *
 * @return The time when the libsvn_subr library was compiled, in the
 * format defined by the C standard macro @c __TIME__.
 *
 * @since New in 1.8.
 */
const char *
svn_version_ext_build_time(const svn_version_extended_t *ext_info);

/**
 * Accessor for svn_version_extended_t.
 *
 * @return The canonical host triplet (arch-vendor-osname) of the
 * system where libsvn_subr was compiled.
 *
 * @note On Unix-like systems (includng Mac OS X), this string is the
 * same as the output of the config.guess script.
 *
 * @since New in 1.8.
 */
const char *
svn_version_ext_build_host(const svn_version_extended_t *ext_info);

/**
 * Accessor for svn_version_extended_t.
 *
 * @return The localized copyright notice.
 *
 * @since New in 1.8.
 */
const char *
svn_version_ext_copyright(const svn_version_extended_t *ext_info);

/**
 * Accessor for svn_version_extended_t.
 *
 * @return The canonical host triplet (arch-vendor-osname) of the
 * system where the current process is running.
 *
 * @note This string may not be the same as the output of config.guess
 * on the same system.
 *
 * @since New in 1.8.
 */
const char *
svn_version_ext_runtime_host(const svn_version_extended_t *ext_info);

/**
 * Accessor for svn_version_extended_t.
 *
 * @return The "commercial" release name of the running operating
 * system, if available.  Not to be confused with, e.g., the output of
 * "uname -v" or "uname -r".  The returned value may be @c NULL.
 *
 * @since New in 1.8.
 */
const char *
svn_version_ext_runtime_osname(const svn_version_extended_t *ext_info);

/**
 * Dependent library information.
 * Describes the name and versions of known dependencies
 * used by libsvn_subr.
 *
 * @since New in 1.8.
 */
typedef struct svn_version_ext_linked_lib_t
{
  const char *name;             /**< Library name */
  const char *compiled_version; /**< Compile-time version string */
  const char *runtime_version;  /**< Run-time version string (optional) */
} svn_version_ext_linked_lib_t;

/**
 * Accessor for svn_version_extended_t.
 *
 * @return Array of svn_version_ext_linked_lib_t describing dependent
 * libraries.  The returned value may be @c NULL.
 *
 * @since New in 1.8.
 */
const apr_array_header_t *
svn_version_ext_linked_libs(const svn_version_extended_t *ext_info);


/**
 * Loaded shared library information.
 * Describes the name and, where available, version of the shared libraries
 * loaded by the running program.
 *
 * @since New in 1.8.
 */
typedef struct svn_version_ext_loaded_lib_t
{
  const char *name;             /**< Library name */
  const char *version;          /**< Library version (optional) */
} svn_version_ext_loaded_lib_t;


/**
 * Accessor for svn_version_extended_t.
 *
 * @return Array of svn_version_ext_loaded_lib_t describing loaded
 * shared libraries.  The returned value may be @c NULL.
 *
 * @note On Mac OS X, the loaded frameworks, private frameworks and
 * system libraries will not be listed.
 *
 * @since New in 1.8.
 */
const apr_array_header_t *
svn_version_ext_loaded_libs(const svn_version_extended_t *ext_info);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_VERSION_H */
