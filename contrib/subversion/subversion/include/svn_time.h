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
 * @file svn_time.h
 * @brief Time/date utilities
 */

#ifndef SVN_TIME_H
#define SVN_TIME_H

#include <apr_pools.h>
#include <apr_time.h>

#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Convert @a when to a <tt>const char *</tt> representation allocated
 * in @a pool.  Use svn_time_from_cstring() for the reverse
 * conversion.
 */
const char *
svn_time_to_cstring(apr_time_t when,
                    apr_pool_t *pool);

/** Convert @a data to an @c apr_time_t @a when.
 * Use @a pool for temporary memory allocation.
 */
svn_error_t *
svn_time_from_cstring(apr_time_t *when,
                      const char *data,
                      apr_pool_t *pool);

/** Convert @a when to a <tt>const char *</tt> representation allocated
 * in @a pool, suitable for human display in UTF8.
 */
const char *
svn_time_to_human_cstring(apr_time_t when,
                          apr_pool_t *pool);


/** Convert a human-readable date @a text into an @c apr_time_t, using
 * @a now as the current time and storing the result in @a result.
 * The local time zone will be used to compute the appropriate GMT
 * offset if @a text contains a local time specification.  Set @a
 * matched to indicate whether or not @a text was parsed successfully.
 * Perform any allocation in @a pool.  Return an error iff an internal
 * error (rather than a simple parse error) occurs.
 */
svn_error_t *
svn_parse_date(svn_boolean_t *matched,
               apr_time_t *result,
               const char *text,
               apr_time_t now,
               apr_pool_t *pool);


/** Sleep until the next second, to ensure that any files modified
 * after we exit have a different timestamp than the one we recorded.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 * Use svn_io_sleep_for_timestamps() instead.
 */
SVN_DEPRECATED
void
svn_sleep_for_timestamps(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_TIME_H */
