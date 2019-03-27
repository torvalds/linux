/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef APR_VERSION_H
#define APR_VERSION_H

/**
 * @file apr_version.h
 * @brief APR Versioning Interface
 * 
 * APR's Version
 *
 * There are several different mechanisms for accessing the version. There
 * is a string form, and a set of numbers; in addition, there are constants
 * which can be compiled into your application, and you can query the library
 * being used for its actual version.
 *
 * Note that it is possible for an application to detect that it has been
 * compiled against a different version of APR by use of the compile-time
 * constants and the use of the run-time query function.
 *
 * APR version numbering follows the guidelines specified in:
 *
 *     http://apr.apache.org/versioning.html
 */


#define APR_COPYRIGHT "Copyright (c) 2000-2015 The Apache Software " \
                      "Foundation or its licensors, as applicable."

/* The numeric compile-time version constants. These constants are the
 * authoritative version numbers for APR. 
 */

/** major version 
 * Major API changes that could cause compatibility problems for older
 * programs such as structure size changes.  No binary compatibility is
 * possible across a change in the major version.
 */
#define APR_MAJOR_VERSION       1

/** minor version
 * Minor API changes that do not cause binary compatibility problems.
 * Reset to 0 when upgrading APR_MAJOR_VERSION
 */
#define APR_MINOR_VERSION       5

/** patch level 
 * The Patch Level never includes API changes, simply bug fixes.
 * Reset to 0 when upgrading APR_MINOR_VERSION
 */
#define APR_PATCH_VERSION       2

/** 
 * The symbol APR_IS_DEV_VERSION is only defined for internal,
 * "development" copies of APR.  It is undefined for released versions
 * of APR.
 */
/* #define APR_IS_DEV_VERSION */

/**
 * Check at compile time if the APR version is at least a certain
 * level.
 * @param major The major version component of the version checked
 * for (e.g., the "1" of "1.3.0").
 * @param minor The minor version component of the version checked
 * for (e.g., the "3" of "1.3.0").
 * @param patch The patch level component of the version checked
 * for (e.g., the "0" of "1.3.0").
 * @remark This macro is available with APR versions starting with
 * 1.3.0.
 */
#define APR_VERSION_AT_LEAST(major,minor,patch)                    \
(((major) < APR_MAJOR_VERSION)                                     \
 || ((major) == APR_MAJOR_VERSION && (minor) < APR_MINOR_VERSION) \
 || ((major) == APR_MAJOR_VERSION && (minor) == APR_MINOR_VERSION && (patch) <= APR_PATCH_VERSION))

#if defined(APR_IS_DEV_VERSION) || defined(DOXYGEN)
/** Internal: string form of the "is dev" flag */
#ifndef APR_IS_DEV_STRING
#define APR_IS_DEV_STRING "-dev"
#endif
#else
#define APR_IS_DEV_STRING ""
#endif

/* APR_STRINGIFY is defined here, and also in apr_general.h, so wrap it */
#ifndef APR_STRINGIFY
/** Properly quote a value as a string in the C preprocessor */
#define APR_STRINGIFY(n) APR_STRINGIFY_HELPER(n)
/** Helper macro for APR_STRINGIFY */
#define APR_STRINGIFY_HELPER(n) #n
#endif

/** The formatted string of APR's version */
#define APR_VERSION_STRING \
     APR_STRINGIFY(APR_MAJOR_VERSION) "." \
     APR_STRINGIFY(APR_MINOR_VERSION) "." \
     APR_STRINGIFY(APR_PATCH_VERSION) \
     APR_IS_DEV_STRING

/** An alternative formatted string of APR's version */
/* macro for Win32 .rc files using numeric csv representation */
#define APR_VERSION_STRING_CSV APR_MAJOR_VERSION ##, \
                             ##APR_MINOR_VERSION ##, \
                             ##APR_PATCH_VERSION


#ifndef APR_VERSION_ONLY

/* The C language API to access the version at run time, 
 * as opposed to compile time.  APR_VERSION_ONLY may be defined 
 * externally when preprocessing apr_version.h to obtain strictly 
 * the C Preprocessor macro declarations.
 */

#include "apr.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 
 * The numeric version information is broken out into fields within this 
 * structure. 
 */
typedef struct {
    int major;      /**< major number */
    int minor;      /**< minor number */
    int patch;      /**< patch number */
    int is_dev;     /**< is development (1 or 0) */
} apr_version_t;

/**
 * Return APR's version information information in a numeric form.
 *
 *  @param pvsn Pointer to a version structure for returning the version
 *              information.
 */
APR_DECLARE(void) apr_version(apr_version_t *pvsn);

/** Return APR's version information as a string. */
APR_DECLARE(const char *) apr_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* ndef APR_VERSION_ONLY */

#endif /* ndef APR_VERSION_H */
