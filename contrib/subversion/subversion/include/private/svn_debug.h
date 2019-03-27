/* svn_debug.h : handy little debug tools for the SVN developers
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

#ifndef SVN_DEBUG_H
#define SVN_DEBUG_H

#ifdef SVN_DEBUG
#define SVN_DBG__PROTOTYPES
#endif

#ifdef SVN_DBG__PROTOTYPES
#define APR_WANT_STDIO
#include <apr_want.h>
#include <apr_hash.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef SVN_DBG__PROTOTYPES
/* A few helper functions for the macros below.  */
void
svn_dbg__preamble(const char *file, long line, FILE *output);
void
svn_dbg__printf(const char *fmt, ...)
  __attribute__((format(printf, 1, 2)));
void
svn_dbg__print_props(apr_hash_t *props,
                     const char *header_fmt,
                     ...)
  __attribute__((format(printf, 2, 3)));
#endif

/* Only available when SVN_DEBUG is defined (ie. svn developers). Note that
   we do *not* provide replacement macros/functions for proper releases.
   The debug stuff should be removed before a commit.

   ### maybe we will eventually decide to allow certain debug stuff to
   ### remain in the code. at that point, we can rejigger this header.  */
#ifdef SVN_DEBUG

/* Print to stdout. Edit this line if you need stderr.  */
#define SVN_DBG_OUTPUT stdout


/* Defining this symbol in the source file, BEFORE INCLUDING THIS HEADER,
   will switch off the output. Calls will still be made to svn_dbg__preamble()
   for breakpoints.  */
#ifdef SVN_DBG_QUIET

#define SVN_DBG(ARGS) svn_dbg__preamble(__FILE__, __LINE__, NULL)
#define SVN_DBG_PROPS(ARGS) svn_dbg__preamble(__FILE__, __LINE__, NULL)

#else

/** Debug aid macro that prints the file:line of the call and printf-like
 * arguments to the #SVN_DBG_OUTPUT stdio stream (#stdout by default).  Typical
 * usage:
 *
 * <pre>
 *   SVN_DBG(("rev=%ld kind=%s\n", revnum, svn_node_kind_to_word(kind)));
 * </pre>
 *
 * outputs:
 *
 * <pre>
 *   DBG: kitchensink.c: 42: rev=3141592 kind=file
 * </pre>
 *
 * Note that these output lines are filtered by our test suite automatically,
 * so you don't have to worry about throwing off expected output.
 */
#define SVN_DBG(ARGS) (svn_dbg__preamble(__FILE__, __LINE__, SVN_DBG_OUTPUT), \
                       svn_dbg__printf ARGS)
#define SVN_DBG_PROPS(ARGS) (svn_dbg__preamble(__FILE__, __LINE__, \
                                               SVN_DBG_OUTPUT), \
                             svn_dbg__print_props ARGS)

#endif

#endif /* SVN_DEBUG */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DEBUG_H */
