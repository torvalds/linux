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
 * @file svn_checksum.h
 * @brief Subversion checksum routines
 */

#ifndef SVN_CHECKSUM_H
#define SVN_CHECKSUM_H

#include <apr.h>        /* for apr_size_t */
#include <apr_pools.h>  /* for apr_pool_t */

#include "svn_types.h"  /* for svn_boolean_t, svn_error_t */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/**
 * Various types of checksums.
 *
 * @since New in 1.6.
 */
typedef enum svn_checksum_kind_t
{
  /** The checksum is (or should be set to) an MD5 checksum. */
  svn_checksum_md5,

  /** The checksum is (or should be set to) a SHA1 checksum. */
  svn_checksum_sha1,

  /** The checksum is (or should be set to) a FNV-1a 32 bit checksum,
   * in big endian byte order.
   * @since New in 1.9. */
  svn_checksum_fnv1a_32,

  /** The checksum is (or should be set to) a modified FNV-1a 32 bit,
   * in big endian byte order.
   * @since New in 1.9. */
  svn_checksum_fnv1a_32x4
} svn_checksum_kind_t;

/**
 * A generic checksum representation.
 *
 * @since New in 1.6.
 */
typedef struct svn_checksum_t
{
  /** The bytes of the checksum. */
  const unsigned char *digest;

  /** The type of the checksum.  This should never be changed by consumers
      of the APIs. */
  svn_checksum_kind_t kind;
} svn_checksum_t;

/**
 * Opaque type for creating checksums of data.
 */
typedef struct svn_checksum_ctx_t svn_checksum_ctx_t;

/** Return a new checksum structure of type @a kind, initialized to the all-
 * zeros value, allocated in @a pool.
 *
 * @since New in 1.6.
 */
svn_checksum_t *
svn_checksum_create(svn_checksum_kind_t kind,
                    apr_pool_t *pool);

/** Set @a checksum->digest to all zeros, which, by convention, matches
 * all other checksums.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_checksum_clear(svn_checksum_t *checksum);

/** Compare checksums @a checksum1 and @a checksum2.  If their kinds do not
 * match or if neither is all zeros, and their content does not match, then
 * return FALSE; else return TRUE.
 *
 * @since New in 1.6.
 */
svn_boolean_t
svn_checksum_match(const svn_checksum_t *checksum1,
                   const svn_checksum_t *checksum2);


/**
 * Return a deep copy of @a checksum, allocated in @a pool.  If @a
 * checksum is NULL then NULL is returned.
 *
 * @since New in 1.6.
 */
svn_checksum_t *
svn_checksum_dup(const svn_checksum_t *checksum,
                 apr_pool_t *pool);


/** Return the hex representation of @a checksum, allocating the string
 * in @a pool.
 *
 * @since New in 1.6.
 */
const char *
svn_checksum_to_cstring_display(const svn_checksum_t *checksum,
                                apr_pool_t *pool);


/** Return the hex representation of @a checksum, allocating the
 * string in @a pool.  If @a checksum->digest is all zeros (that is,
 * 0, not '0') then return NULL. In 1.7+, @a checksum may be NULL
 * and NULL will be returned in that case.
 *
 * @since New in 1.6.
 * @note Passing NULL for @a checksum in 1.6 will cause a segfault.
 */
const char *
svn_checksum_to_cstring(const svn_checksum_t *checksum,
                        apr_pool_t *pool);


/** Return a serialized representation of @a checksum, allocated in
 * @a result_pool. Temporary allocations are performed in @a scratch_pool.
 *
 * Note that @a checksum may not be NULL.
 *
 * @since New in 1.7.
 */
const char *
svn_checksum_serialize(const svn_checksum_t *checksum,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);


/** Return @a checksum from the serialized format at @a data. The checksum
 * will be allocated in @a result_pool, with any temporary allocations
 * performed in @a scratch_pool.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_checksum_deserialize(const svn_checksum_t **checksum,
                         const char *data,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);


/** Parse the hex representation @a hex of a checksum of kind @a kind and
 * set @a *checksum to the result, allocating in @a pool.
 *
 * If @a hex is @c NULL or is the all-zeros checksum, then set @a *checksum
 * to @c NULL.
 *
 * @since New in 1.6.
 */
/* ### TODO: When revving this, make it set @a *checksum to a non-NULL struct
 * ###       when @a hex is the all-zeroes checksum.  See
 * ### http://mail-archives.apache.org/mod_mbox/subversion-dev/201609.mbox/%3c00cd26ab-bdb3-67b4-ca6b-063266493874%40apache.org%3e
 */
svn_error_t *
svn_checksum_parse_hex(svn_checksum_t **checksum,
                       svn_checksum_kind_t kind,
                       const char *hex,
                       apr_pool_t *pool);

/**
 * Return in @a *checksum the checksum of type @a kind for the bytes beginning
 * at @a data, and going for @a len.  @a *checksum is allocated in @a pool.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_checksum(svn_checksum_t **checksum,
             svn_checksum_kind_t kind,
             const void *data,
             apr_size_t len,
             apr_pool_t *pool);


/**
 * Return in @a pool a newly allocated checksum populated with the checksum
 * of type @a kind for the empty string.
 *
 * @since New in 1.6.
 */
svn_checksum_t *
svn_checksum_empty_checksum(svn_checksum_kind_t kind,
                            apr_pool_t *pool);


/**
 * Create a new @c svn_checksum_ctx_t structure, allocated from @a pool for
 * calculating checksums of type @a kind.  @see svn_checksum_final()
 *
 * @since New in 1.6.
 */
svn_checksum_ctx_t *
svn_checksum_ctx_create(svn_checksum_kind_t kind,
                        apr_pool_t *pool);

/**
 * Reset an existing checksum @a ctx to initial state.
 * @see svn_checksum_ctx_create()
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_checksum_ctx_reset(svn_checksum_ctx_t *ctx);

/**
 * Update the checksum represented by @a ctx, with @a len bytes starting at
 * @a data.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_checksum_update(svn_checksum_ctx_t *ctx,
                    const void *data,
                    apr_size_t len);


/**
 * Finalize the checksum used when creating @a ctx, and put the resultant
 * checksum in @a *checksum, allocated in @a pool.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_checksum_final(svn_checksum_t **checksum,
                   const svn_checksum_ctx_t *ctx,
                   apr_pool_t *pool);


/**
 * Return the digest size of @a checksum.
 *
 * @since New in 1.6.
 */
apr_size_t
svn_checksum_size(const svn_checksum_t *checksum);

/**
 * Return @c TRUE iff @a checksum matches the checksum for the empty
 * string.
 *
 * @since New in 1.8.
 */
svn_boolean_t
svn_checksum_is_empty_checksum(svn_checksum_t *checksum);


/**
 * Return an error of type #SVN_ERR_CHECKSUM_MISMATCH for @a actual and
 * @a expected checksums which do not match.  Use @a fmt, and the following
 * parameters to populate the error message.
 *
 * @note This function does not actually check for the mismatch, it just
 * constructs the error.
 *
 * @a scratch_pool is used for temporary allocations; the returned error
 * will be allocated in its own pool (as is typical).
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_checksum_mismatch_err(const svn_checksum_t *expected,
                          const svn_checksum_t *actual,
                          apr_pool_t *scratch_pool,
                          const char *fmt,
                          ...)
  __attribute__ ((format(printf, 4, 5)));

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CHECKSUM_H */
