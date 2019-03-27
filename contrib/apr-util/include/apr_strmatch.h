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

#ifndef APR_STRMATCH_H
#define APR_STRMATCH_H
/**
 * @file apr_strmatch.h
 * @brief APR-UTIL string matching routines
 */

#include "apu.h"
#include "apr_pools.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup APR_Util_StrMatch String matching routines
 * @ingroup APR_Util
 * @{
 */

/** @see apr_strmatch_pattern */
typedef struct apr_strmatch_pattern apr_strmatch_pattern;

/**
 * Precompiled search pattern
 */
struct apr_strmatch_pattern {
    /** Function called to compare */
    const char *(*compare)(const apr_strmatch_pattern *this_pattern,
                           const char *s, apr_size_t slen);
    const char *pattern;    /**< Current pattern */
    apr_size_t length;      /**< Current length */
    void *context;          /**< hook to add precomputed metadata */
};

#if defined(DOXYGEN)
/**
 * Search for a precompiled pattern within a string
 * @param pattern The pattern
 * @param s The string in which to search for the pattern
 * @param slen The length of s (excluding null terminator)
 * @return A pointer to the first instance of the pattern in s, or
 *         NULL if not found
 */
APU_DECLARE(const char *) apr_strmatch(const apr_strmatch_pattern *pattern,
                                       const char *s, apr_size_t slen);
#else
#define apr_strmatch(pattern, s, slen) (*((pattern)->compare))((pattern), (s), (slen))
#endif

/**
 * Precompile a pattern for matching using the Boyer-Moore-Horspool algorithm
 * @param p The pool from which to allocate the pattern
 * @param s The pattern string
 * @param case_sensitive Whether the matching should be case-sensitive
 * @return a pointer to the compiled pattern, or NULL if compilation fails
 */
APU_DECLARE(const apr_strmatch_pattern *) apr_strmatch_precompile(apr_pool_t *p, const char *s, int case_sensitive);

/** @} */
#ifdef __cplusplus
}
#endif

#endif	/* !APR_STRMATCH_H */
