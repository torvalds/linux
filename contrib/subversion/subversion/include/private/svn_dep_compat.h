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
 * @file svn_dep_compat.h
 * @brief Compatibility macros and functions.
 * @since New in 1.5.0.
 */

#ifndef SVN_DEP_COMPAT_H
#define SVN_DEP_COMPAT_H

#include <apr_version.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * We assume that 'int' and 'unsigned' are at least 32 bits wide.
 * This also implies that long (rev numbers) is 32 bits or wider.
 *
 * @since New in 1.9.
 */
#if    defined(APR_HAVE_LIMITS_H) \
    && !defined(SVN_ALLOW_SHORT_INTS) \
    && (INT_MAX < 0x7FFFFFFFl)
#error int is shorter than 32 bits and may break Subversion. Define SVN_ALLOW_SHORT_INTS to skip this check.
#endif

/**
 * We assume that 'char' is 8 bits wide.  The critical interfaces are
 * our repository formats and RA encodings.  E.g. a 32 bit wide char may
 * mess up UTF8 parsing, how we interpret size values etc.
 *
 * @since New in 1.9.
 */
#if    defined(CHAR_BIT) \
    && !defined(SVN_ALLOW_NON_8_BIT_CHARS) \
    && (CHAR_BIT != 8)
#error char is not 8 bits and may break Subversion. Define SVN_ALLOW_NON_8_BIT_CHARS to skip this check.
#endif

/**
 * Work around a platform dependency issue. apr_thread_rwlock_trywrlock()
 * will make APR_STATUS_IS_EBUSY() return TRUE if the lock could not be
 * acquired under Unix. Under Windows, this will not work. So, provide
 * a more portable substitute.
 *
 * @since New in 1.8.
 */
#ifdef WIN32
#define SVN_LOCK_IS_BUSY(x) \
    (APR_STATUS_IS_EBUSY(x) || (x) == APR_FROM_OS_ERROR(WAIT_TIMEOUT))
#else
#define SVN_LOCK_IS_BUSY(x) APR_STATUS_IS_EBUSY(x)
#endif

/**
 * Indicate whether we are running on a POSIX platform.  This has
 * implications on the way e.g. fsync() works.
 *
 * For details on this check, see
 * http://nadeausoftware.com/articles/2012/01/c_c_tip_how_use_compiler_predefined_macros_detect_operating_system#POSIX
 *
 * @since New in 1.10.
 */
#ifndef SVN_ON_POSIX
#if    !defined(_WIN32) \
    && (   defined(__unix__) \
        || defined(__unix) \
        || (defined(__APPLE__) && defined(__MACH__)))  /* UNIX-style OS? */
#  include <unistd.h>
#  if defined(_POSIX_VERSION)
#    define SVN_ON_POSIX
#  endif
#endif
#endif

/**
 * APR keeps a few interesting defines hidden away in its private
 * headers apr_arch_file_io.h, so we redefined them here.
 *
 * @since New in 1.9
 */
#ifndef APR_FREADONLY
#define APR_FREADONLY 0x10000000
#endif
#ifndef APR_OPENINFO
#define APR_OPENINFO  0x00100000
#endif

#if !APR_VERSION_AT_LEAST(1,4,0)
#ifndef apr_time_from_msec
#define apr_time_from_msec(msec) ((apr_time_t)(msec) * 1000)
#endif
#endif

/**
 * APR 1 has volatile qualifier bugs in some atomic prototypes that
 * are fixed in APR 2:
 *   https://issues.apache.org/bugzilla/show_bug.cgi?id=50731
 * Subversion code should put the volatile qualifier in the correct
 * place when declaring variables which means that casting at the call
 * site is necessary when using APR 1.  No casts should be used with
 * APR 2 as this allows the compiler to check that the variable has
 * the correct volatile qualifier.
 */
#if APR_VERSION_AT_LEAST(2,0,0)
#define svn_atomic_casptr(mem, with, cmp) \
  apr_atomic_casptr((mem), (with), (cmp))
#define svn_atomic_xchgptr(mem, val) \
  apr_atomic_xchgptr((mem), (val))
#else
#define svn_atomic_casptr(mem, with, cmp) \
  apr_atomic_casptr((void volatile **)(mem), (with), (cmp))
#define svn_atomic_xchgptr(mem, val) \
  apr_atomic_xchgptr((void volatile **)(mem), (val))
#endif

/**
 * Check at compile time if the Serf version is at least a certain
 * level.
 * @param major The major version component of the version checked
 * for (e.g., the "1" of "1.3.0").
 * @param minor The minor version component of the version checked
 * for (e.g., the "3" of "1.3.0").
 * @param patch The patch level component of the version checked
 * for (e.g., the "0" of "1.3.0").
 *
 * @since New in 1.5.
 */
#ifndef SERF_VERSION_AT_LEAST /* Introduced in Serf 0.1.1 */
#define SERF_VERSION_AT_LEAST(major,minor,patch)                       \
(((major) < SERF_MAJOR_VERSION)                                        \
 || ((major) == SERF_MAJOR_VERSION && (minor) < SERF_MINOR_VERSION)    \
 || ((major) == SERF_MAJOR_VERSION && (minor) == SERF_MINOR_VERSION && \
     (patch) <= SERF_PATCH_VERSION))
#endif /* SERF_VERSION_AT_LEAST */

/**
 * By default, if libsvn is built against one version of SQLite
 * and then run using an older version, svn will error out:
 *
 *     svn: Couldn't perform atomic initialization
 *     svn: SQLite compiled for 3.7.4, but running with 3.7.3
 *
 * That can be annoying when building on a modern system in order
 * to deploy on a less modern one.  So these constants allow one
 * to specify how old the system being deployed on might be.
 * For example,
 *
 *     EXTRA_CFLAGS += -DSVN_SQLITE_MIN_VERSION_NUMBER=3007003
 *     EXTRA_CFLAGS += '-DSVN_SQLITE_MIN_VERSION="3.7.3"'
 *
 * turns on code that works around infelicities in older versions
 * as far back as 3.7.3 and relaxes the check at initialization time
 * to permit them.
 *
 * @since New in 1.8.
 */
#ifndef SVN_SQLITE_MIN_VERSION_NUMBER
#define SVN_SQLITE_MIN_VERSION_NUMBER SQLITE_VERSION_NUMBER
#define SVN_SQLITE_MIN_VERSION SQLITE_VERSION
#endif /* SVN_SQLITE_MIN_VERSION_NUMBER */

/**
 * Check at compile time if the SQLite version is at least a certain
 * level.
 * @param major The major version component of the version checked
 * for (e.g., the "1" of "1.3.0").
 * @param minor The minor version component of the version checked
 * for (e.g., the "3" of "1.3.0").
 * @param patch The patch level component of the version checked
 * for (e.g., the "0" of "1.3.0").
 *
 * @since New in 1.6.
 */
#ifndef SQLITE_VERSION_AT_LEAST
#define SQLITE_VERSION_AT_LEAST(major,minor,patch)                     \
((major*1000000 + minor*1000 + patch) <= SVN_SQLITE_MIN_VERSION_NUMBER)
#endif /* SQLITE_VERSION_AT_LEAST */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DEP_COMPAT_H */
