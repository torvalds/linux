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
 * @file svn_string_private.h
 * @brief Non-public string utility functions.
 */


#ifndef SVN_STRING_PRIVATE_H
#define SVN_STRING_PRIVATE_H

#include "svn_string.h"    /* for svn_boolean_t, svn_error_t */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup svn_string String handling
 * @{
 */


/** Private functions.
 *
 * @defgroup svn_string_private Private functions
 * @{
 */


/** A self-contained memory buffer of known size.
 *
 * Intended to be used where a single variable-sized buffer is needed
 * within an iteration, a scratch pool is available and we want to
 * avoid the cost of creating another pool just for the iteration.
 */
typedef struct svn_membuf_t
{
  /** The a pool from which this buffer was originally allocated, and is not
   * necessarily specific to this buffer.  This is used only for allocating
   * more memory from when the buffer needs to grow.
   */
  apr_pool_t *pool;

  /** pointer to the memory */
  void *data;

  /** total size of buffer allocated */
  apr_size_t size;
} svn_membuf_t;


/* Initialize a memory buffer of the given size */
void
svn_membuf__create(svn_membuf_t *membuf, apr_size_t size, apr_pool_t *pool);

/* Ensure that the given memory buffer has at least the given size */
void
svn_membuf__ensure(svn_membuf_t *membuf, apr_size_t size);

/* Resize the given memory buffer, preserving its contents. */
void
svn_membuf__resize(svn_membuf_t *membuf, apr_size_t size);

/* Zero-fill the given memory */
void
svn_membuf__zero(svn_membuf_t *membuf);

/* Zero-fill the given memory buffer up to the smaller of SIZE and the
   current buffer size. */
void
svn_membuf__nzero(svn_membuf_t *membuf, apr_size_t size);

/* Inline implementation of svn_membuf__zero.
 * Note that PMEMBUF is evaluated only once.
 */
#define SVN_MEMBUF__ZERO(pmembuf)                \
  do                                             \
    {                                            \
      svn_membuf_t *const _m_b_f_ = (pmembuf);   \
      memset(_m_b_f_->data, 0, _m_b_f_->size);   \
    }                                            \
  while(0)

/* Inline implementation of svn_membuf__nzero
 * Note that PMEMBUF and PSIZE are evaluated only once.
 */
#define SVN_MEMBUF__NZERO(pmembuf, psize)        \
  do                                             \
    {                                            \
      svn_membuf_t *const _m_b_f_ = (pmembuf);   \
      const apr_size_t _s_z_ = (psize);          \
      if (_s_z_ > _m_b_f_->size)                 \
        memset(_m_b_f_->data, 0, _m_b_f_->size); \
      else                                       \
        memset(_m_b_f_->data, 0, _s_z_);         \
    }                                            \
  while(0)

#ifndef SVN_DEBUG
/* In non-debug mode, just use these inlie replacements */
#define svn_membuf__zero(B) SVN_MEMBUF__ZERO((B))
#define svn_membuf__nzero(B, S) SVN_MEMBUF__NZERO((B), (S))
#endif


/** Returns the #svn_string_t information contained in the data and
 * len members of @a strbuf. This is effectively a typecast, converting
 * @a strbuf into an #svn_string_t. This first will become invalid and must
 * not be accessed after this function returned.
 */
svn_string_t *
svn_stringbuf__morph_into_string(svn_stringbuf_t *strbuf);

/** Utility macro to define static svn_string_t objects.  @a value must
 * be a static string; the "" in the macro declaration tries to ensure this.
 *
 * Usage:
 * static const svn_string_t my_string = SVN__STATIC_STRING("my text");
 */
#define SVN__STATIC_STRING(value) { value "", sizeof(value "") - 1 }

/** Like strtoul but with a fixed base of 10 and without overflow checks.
 * This allows the compiler to generate massively faster (4x on 64bit LINUX)
 * code.  Overflow checks may be added on the caller side where you might
 * want to test for a more specific value range anyway.
 */
unsigned long
svn__strtoul(const char *buffer, const char **end);

/** Number of chars needed to represent signed (19 places + sign + NUL) or
 * unsigned (20 places + NUL) integers as strings.
 */
#define SVN_INT64_BUFFER_SIZE 21

/** Writes the @a number as string into @a dest. The latter must provide
 * space for at least #SVN_INT64_BUFFER_SIZE characters. Returns the number
 * chars written excluding the terminating NUL.
 */
apr_size_t
svn__ui64toa(char * dest, apr_uint64_t number);

/** Writes the @a number as string into @a dest. The latter must provide
 * space for at least #SVN_INT64_BUFFER_SIZE characters. Returns the number
 * chars written excluding the terminating NUL.
 */
apr_size_t
svn__i64toa(char * dest, apr_int64_t number);

/** Returns a decimal string for @a number allocated in @a pool.  Put in
 * the @a separator at each third place.
 */
char *
svn__ui64toa_sep(apr_uint64_t number, char separator, apr_pool_t *pool);

/** Returns a decimal string for @a number allocated in @a pool.  Put in
 * the @a separator at each third place.
 */
char *
svn__i64toa_sep(apr_int64_t number, char separator, apr_pool_t *pool);


/** Writes the @a number as base36-encoded string into @a dest. The latter
 * must provide space for at least #SVN_INT64_BUFFER_SIZE characters.
 * Returns the number chars written excluding the terminating NUL.
 *
 * @note The actual maximum buffer requirement is much shorter than
 * #SVN_INT64_BUFFER_SIZE but introducing yet another constant is only
 * marginally useful and may open the door to security issues when e.g.
 * switching between base10 and base36 encoding.
 */
apr_size_t
svn__ui64tobase36(char *dest, apr_uint64_t number);

/** Returns the value of the base36 encoded unsigned integer starting at
 * @a source.  If @a next is not NULL, @a *next will be set to the first
 * position after the integer.
 *
 * The data in @a source will be considered part of the number to parse
 * as long as the characters are within the base36 range.  If there are
 * no such characters to begin with, 0 is returned.  Inputs with more than
 * #SVN_INT64_BUFFER_SIZE digits will not be fully parsed, i.e. the value
 * of @a *next as well as the return value are undefined.
 */
apr_uint64_t
svn__base36toui64(const char **next, const char *source);

/**
 * The upper limit of the similarity range returned by
 * svn_cstring__similarity() and svn_string__similarity().
 */
#define SVN_STRING__SIM_RANGE_MAX 1000000

/**
 * Computes the similarity score of STRA and STRB. Returns the ratio
 * of the length of their longest common subsequence and the average
 * length of the strings, normalized to the range
 * [0..SVN_STRING__SIM_RANGE_MAX]. The result is equivalent to
 * Python's
 *
 *   difflib.SequenceMatcher.ratio
 *
 * Optionally sets *RLCS to the length of the longest common
 * subsequence of STRA and STRB. Using BUFFER for temporary storage,
 * requires memory proportional to the length of the shorter string.
 *
 * The LCS algorithm used is described in, e.g.,
 *
 *   http://en.wikipedia.org/wiki/Longest_common_subsequence_problem
 *
 * Q: Why another LCS when we already have one in libsvn_diff?
 * A: svn_diff__lcs is too heavyweight and too generic for the
 *    purposes of similarity testing. Whilst it would be possible
 *    to use a character-based tokenizer with it, we really only need
 *    the *length* of the LCS for the similarity score, not all the
 *    other information that svn_diff__lcs produces in order to
 *    make printing diffs possible.
 *
 * Q: Is there a limit on the length of the string parameters?
 * A: Only available memory. But note that the LCS algorithm used
 *    has O(strlen(STRA) * strlen(STRB)) worst-case performance,
 *    so do keep a rein on your enthusiasm.
 */
apr_size_t
svn_cstring__similarity(const char *stra, const char *strb,
                        svn_membuf_t *buffer, apr_size_t *rlcs);

/**
 * Like svn_cstring__similarity, but accepts svn_string_t's instead
 * of NUL-terminated character strings.
 */
apr_size_t
svn_string__similarity(const svn_string_t *stringa,
                       const svn_string_t *stringb,
                       svn_membuf_t *buffer, apr_size_t *rlcs);


/* Return the lowest position at which A and B differ. If no difference
 * can be found in the first MAX_LEN characters, MAX_LEN will be returned.
 */
apr_size_t
svn_cstring__match_length(const char *a,
                          const char *b,
                          apr_size_t max_len);

/* Return the number of bytes before A and B that don't differ.  If no
 * difference can be found in the first MAX_LEN characters,  MAX_LEN will
 * be returned.  Please note that A-MAX_LEN and B-MAX_LEN must both be
 * valid addresses.
 */
apr_size_t
svn_cstring__reverse_match_length(const char *a,
                                  const char *b,
                                  apr_size_t max_len);

/** @} */

/** Prefix trees.
 *
 * Prefix trees allow for a space-efficient representation of a set of path-
 * like strings, i.e. those that share common prefixes.  Any given string
 * value will be stored only once, i.e. two strings stored in the same tree
 * are equal if and only if the point to the same #svn_prefix_string__t.
 *
 * @defgroup svn_prefix_string Strings in prefix trees.
* @{
 */

/**
 * Opaque data type for prefix-tree-based strings.
 */
typedef struct svn_prefix_string__t svn_prefix_string__t;

/**
 * Opaque data type representing a prefix tree
 */
typedef struct svn_prefix_tree__t svn_prefix_tree__t;

/**
 * Return a new prefix tree allocated in @a pool.
 */
svn_prefix_tree__t *
svn_prefix_tree__create(apr_pool_t *pool);

/**
 * Return a string with the value @a s stored in @a tree.  If no such string
 * exists yet, add it automatically.
 */
svn_prefix_string__t *
svn_prefix_string__create(svn_prefix_tree__t *tree,
                          const char *s);

/**
 * Return the contents of @a s as a new string object allocated in @a pool.
 */
svn_string_t *
svn_prefix_string__expand(const svn_prefix_string__t *s,
                          apr_pool_t *pool);

/**
 * Compare the two strings @a lhs and @a rhs that must be part of the same
 * tree.
 */
int
svn_prefix_string__compare(const svn_prefix_string__t *lhs,
                           const svn_prefix_string__t *rhs);

/** @} */

/** @} */


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_STRING_PRIVATE_H */
