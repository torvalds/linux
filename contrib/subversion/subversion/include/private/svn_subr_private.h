/*
 * svn_subr_private.h : private definitions from libsvn_subr
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

#ifndef SVN_SUBR_PRIVATE_H
#define SVN_SUBR_PRIVATE_H

#include "svn_types.h"
#include "svn_io.h"
#include "svn_config.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Spill-to-file Buffers
 *
 * @defgroup svn_spillbuf_t Spill-to-file Buffers
 * @{
 */

/** A buffer that collects blocks of content, possibly using a file.
 *
 * The spill-buffer is created with two basic parameters: the size of the
 * blocks that will be written into the spill-buffer ("blocksize"), and
 * the (approximate) maximum size that will be allowed in memory ("maxsize").
 * Once the maxsize is reached, newly written content will be "spilled"
 * into a temporary file.
 *
 * When writing, content will be buffered into memory unless a given write
 * will cause the amount of in-memory content to exceed the specified
 * maxsize. At that point, the file is created, and the content will be
 * written to that file.
 *
 * To read information back out of a spill buffer, there are two approaches
 * available to the application:
 *
 *   *) reading blocks using svn_spillbuf_read() (a "pull" model)
 *   *) having blocks passed to a callback via svn_spillbuf_process()
 *      (a "push" model to your application)
 *
 * In both cases, the spill-buffer will provide you with a block of N bytes
 * that you must fully consume before asking for more data. The callback
 * style provides for a "stop" parameter to temporarily pause the reading
 * until another read is desired. The two styles of reading may be mixed,
 * as the caller desires. Generally, N will be the blocksize, and will be
 * less when the end of the content is reached.
 *
 * For a more stream-oriented style of reading, where the caller specifies
 * the number of bytes to read into a caller-provided buffer, please see
 * svn_spillbuf_reader_t. That overlaid type will cause more memory copies
 * to be performed (whereas the bare spill-buffer type hands you a buffer
 * to consume).
 *
 * Writes may be interleaved with reading, and content will be returned
 * in a FIFO manner. Thus, if content has been placed into the spill-buffer
 * you will always read the earliest-written data, and any newly-written
 * content will be appended to the buffer.
 *
 * Note: the file is created in the same pool where the spill-buffer was
 * created. If the content is completely read from that file, it will be
 * closed and deleted. Should writing further content cause another spill
 * file to be created, that will increase the size of the pool. There is
 * no bound on the amount of file-related resources that may be consumed
 * from the pool. It is entirely related to the read/write pattern and
 * whether spill files are repeatedly created.
 */
typedef struct svn_spillbuf_t svn_spillbuf_t;


/* Create a spill buffer.  */
svn_spillbuf_t *
svn_spillbuf__create(apr_size_t blocksize,
                     apr_size_t maxsize,
                     apr_pool_t *result_pool);

/* Create a spill buffer, with extra parameters.  */
svn_spillbuf_t *
svn_spillbuf__create_extended(apr_size_t blocksize,
                              apr_size_t maxsize,
                              svn_boolean_t delete_on_close,
                              svn_boolean_t spill_all_contents,
                              const char* dirpath,
                              apr_pool_t *result_pool);

/* Determine how much content is stored in the spill buffer.  */
svn_filesize_t
svn_spillbuf__get_size(const svn_spillbuf_t *buf);

/* Determine how much content the spill buffer is caching in memory.  */
svn_filesize_t
svn_spillbuf__get_memory_size(const svn_spillbuf_t *buf);

/* Retrieve the name of the spill file. The returned value will be
   NULL if the file has not been created yet. */
const char *
svn_spillbuf__get_filename(const svn_spillbuf_t *buf);

/* Retrieve the handle of the spill file. The returned value will be
   NULL if the file has not been created yet. */
apr_file_t *
svn_spillbuf__get_file(const svn_spillbuf_t *buf);

/* Write some data into the spill buffer.  */
svn_error_t *
svn_spillbuf__write(svn_spillbuf_t *buf,
                    const char *data,
                    apr_size_t len,
                    apr_pool_t *scratch_pool);


/* Read a block of memory from the spill buffer. @a *data will be set to
   NULL if no content remains. Otherwise, @a data and @a len will point to
   data that must be fully-consumed by the caller. This data will remain
   valid until another call to svn_spillbuf__write(), svn_spillbuf__read(),
   or svn_spillbuf__process(), or if the spill buffer's pool is cleared.  */
svn_error_t *
svn_spillbuf__read(const char **data,
                   apr_size_t *len,
                   svn_spillbuf_t *buf,
                   apr_pool_t *scratch_pool);


/* Callback for reading content out of the spill buffer. Set @a stop if
   you want to stop the processing (and will call svn_spillbuf__process
   again, at a later time).  */
typedef svn_error_t * (*svn_spillbuf_read_t)(svn_boolean_t *stop,
                                             void *baton,
                                             const char *data,
                                             apr_size_t len,
                                             apr_pool_t *scratch_pool);


/* Process the content stored in the spill buffer. @a exhausted will be
   set to TRUE if all of the content is processed by @a read_func. This
   function may return early if the callback returns TRUE for its 'stop'
   parameter.  */
svn_error_t *
svn_spillbuf__process(svn_boolean_t *exhausted,
                      svn_spillbuf_t *buf,
                      svn_spillbuf_read_t read_func,
                      void *read_baton,
                      apr_pool_t *scratch_pool);


/** Classic stream reading layer on top of spill-buffers.
 *
 * This type layers upon a spill-buffer to enable a caller to read a
 * specified number of bytes into the caller's provided buffer. This
 * implies more memory copies than the standard spill-buffer reading
 * interface, but is sometimes required by spill-buffer users.
 */
typedef struct svn_spillbuf_reader_t svn_spillbuf_reader_t;


/* Create a spill-buffer and a reader for it, using the same arguments as
   svn_spillbuf__create().  */
svn_spillbuf_reader_t *
svn_spillbuf__reader_create(apr_size_t blocksize,
                            apr_size_t maxsize,
                            apr_pool_t *result_pool);

/* Read @a len bytes from @a reader into @a data. The number of bytes
   actually read is stored in @a amt. If the content is exhausted, then
   @a amt is set to zero. It will always be non-zero if the spill-buffer
   contains content.

   If @a len is zero, then SVN_ERR_INCORRECT_PARAMS is returned.  */
svn_error_t *
svn_spillbuf__reader_read(apr_size_t *amt,
                          svn_spillbuf_reader_t *reader,
                          char *data,
                          apr_size_t len,
                          apr_pool_t *scratch_pool);


/* Read a single character from @a reader, and place it in @a c. If there
   is no content in the spill-buffer, then SVN_ERR_STREAM_UNEXPECTED_EOF
   is returned.  */
svn_error_t *
svn_spillbuf__reader_getc(char *c,
                          svn_spillbuf_reader_t *reader,
                          apr_pool_t *scratch_pool);


/* Write @a len bytes from @a data into the spill-buffer in @a reader.  */
svn_error_t *
svn_spillbuf__reader_write(svn_spillbuf_reader_t *reader,
                           const char *data,
                           apr_size_t len,
                           apr_pool_t *scratch_pool);


/* Return a stream built on top of a spillbuf.

   This stream can be used for reading and writing, but implements the
   same basic semantics of a spillbuf for the underlying storage. */
svn_stream_t *
svn_stream__from_spillbuf(svn_spillbuf_t *buf,
                          apr_pool_t *result_pool);

/** @} */

/*----------------------------------------------------*/

/**
 * @defgroup svn_checksum_private Checksumming helper APIs
 * @{
 */

/**
 * Internal function for creating a MD5 checksum from a binary digest.
 *
 * @since New in 1.8
 */
svn_checksum_t *
svn_checksum__from_digest_md5(const unsigned char *digest,
                              apr_pool_t *result_pool);

/**
 * Internal function for creating a SHA1 checksum from a binary
 * digest.
 *
 * @since New in 1.8
 */
svn_checksum_t *
svn_checksum__from_digest_sha1(const unsigned char *digest,
                               apr_pool_t *result_pool);

/**
 * Internal function for creating a 32 bit FNV-1a checksum from a binary
 * digest.
 *
 * @since New in 1.9
 */
svn_checksum_t *
svn_checksum__from_digest_fnv1a_32(const unsigned char *digest,
                                   apr_pool_t *result_pool);

/**
 * Internal function for creating a modified 32 bit FNV-1a checksum from
 * a binary digest.
 *
 * @since New in 1.9
 */
svn_checksum_t *
svn_checksum__from_digest_fnv1a_32x4(const unsigned char *digest,
                                     apr_pool_t *result_pool);


/**
 * Return a stream that calculates a checksum of type @a kind over all
 * data written to the @a inner_stream.  When the returned stream gets
 * closed, write the checksum to @a *checksum.
 * Allocate the result in @a pool.
 *
 * @note The stream returned only supports #svn_stream_write and
 * #svn_stream_close.
 */
svn_stream_t *
svn_checksum__wrap_write_stream(svn_checksum_t **checksum,
                                svn_stream_t *inner_stream,
                                svn_checksum_kind_t kind,
                                apr_pool_t *pool);

/**
 * Return a stream that calculates a 32 bit modified FNV-1a checksum
 * over all data written to the @a inner_stream and writes the digest
 * to @a *digest when the returned stream gets closed.
 * Allocate the stream in @a pool.
 */
svn_stream_t *
svn_checksum__wrap_write_stream_fnv1a_32x4(apr_uint32_t *digest,
                                           svn_stream_t *inner_stream,
                                           apr_pool_t *pool);

/**
 * Return a 32 bit FNV-1a checksum for the first @a len bytes in @a input.
 *
 * @since New in 1.9
 */
apr_uint32_t
svn__fnv1a_32(const void *input, apr_size_t len);

/**
 * Return a 32 bit modified FNV-1a checksum for the first @a len bytes in
 * @a input.
 *
 * @note This is a proprietary checksumming algorithm based FNV-1a with
 *       approximately the same strength.  It is up to 4 times faster
 *       than plain FNV-1a for longer data blocks.
 *
 * @since New in 1.9
 */
apr_uint32_t
svn__fnv1a_32x4(const void *input, apr_size_t len);

/** @} */


/**
 * @defgroup svn_hash_support Hash table serialization support
 * @{
 */

/*----------------------------------------------------*/

/**
 * @defgroup svn_hash_misc Miscellaneous hash APIs
 * @{
 */

/** @} */


/**
 * @defgroup svn_hash_getters Specialized getter APIs for hashes
 * @{
 */

/** Find the value of a @a key in @a hash, return the value.
 *
 * If @a hash is @c NULL or if the @a key cannot be found, the
 * @a default_value will be returned.
 *
 * @since New in 1.7.
 */
const char *
svn_hash__get_cstring(apr_hash_t *hash,
                      const char *key,
                      const char *default_value);

/** Like svn_hash_get_cstring(), but for boolean values.
 *
 * Parses the value as a boolean value. The recognized representations
 * are 'TRUE'/'FALSE', 'yes'/'no', 'on'/'off', '1'/'0'; case does not
 * matter.
 *
 * @since New in 1.7.
 */
svn_boolean_t
svn_hash__get_bool(apr_hash_t *hash,
                   const char *key,
                   svn_boolean_t default_value);

/** @} */

/**
 * @defgroup svn_hash_create Create optimized APR hash tables
 * @{
 */

/** Returns a hash table, allocated in @a pool, with the same ordering of
 * elements as APR 1.4.5 or earlier (using apr_hashfunc_default) but uses
 * a faster hash function implementation.
 *
 * @since New in 1.8.
 */
apr_hash_t *
svn_hash__make(apr_pool_t *pool);

/** @} */

/**
 * @defgroup svn_hash_read Reading serialized hash tables
 * @{
 */

/** Struct that represents a key value pair read from a serialized hash
 * representation.  There are special cases that can also be represented:
 * a #NULL @a key signifies the end of the hash, a #NULL @a val for non-
 * NULL keys is only possible in incremental mode describes a deletion.
 *
 * @since New in 1.9.
 */
typedef struct svn_hash__entry_t
{
  /** 0-terminated Key.  #NULL if this contains no data at all because we
   * encountered the end of the hash. */
  char *key;

  /** Length of @a key.  Must be 0 if @a key is #NULL. */
  apr_size_t keylen;

  /** 0-terminated value stored with the key.  If this is #NULL for a
   * non-NULL @a key, then this means that the key shall be removed from
   * the hash (only used in incremental mode).  Must be #NULL if @a key is
   * #NULL. */
  char *val;

  /** Length of @a val.  Must be 0 if @a val is #NULL. */
  apr_size_t vallen;
} svn_hash__entry_t;

/** Reads a single key-value pair from @a stream and returns it in the
 * caller-provided @a *entry (members don't need to be pre-initialized).
 * @a pool is used to allocate members of @a *entry and for tempoaries.
 *
 * @see #svn_hash_read2 for more details.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_hash__read_entry(svn_hash__entry_t *entry,
                     svn_stream_t *stream,
                     const char *terminator,
                     svn_boolean_t incremental,
                     apr_pool_t *pool);

/** @} */

/** @} */


/** Apply the changes described by @a prop_changes to @a original_props and
 * return the result.  The inverse of svn_prop_diffs().
 *
 * Allocate the resulting hash from @a pool, but allocate its keys and
 * values from @a pool and/or by reference to the storage of the inputs.
 *
 * Note: some other APIs use an array of pointers to svn_prop_t.
 *
 * @since New in 1.8.
 */
apr_hash_t *
svn_prop__patch(const apr_hash_t *original_props,
                const apr_array_header_t *prop_changes,
                apr_pool_t *pool);


/**
 * @defgroup svn_version Version number dotted triplet parsing
 * @{
 */

/* Set @a *version to a version structure parsed from the version
 * string representation in @a version_string.  Return
 * @c SVN_ERR_MALFORMED_VERSION_STRING if the string fails to parse
 * cleanly.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_version__parse_version_string(svn_version_t **version,
                                  const char *version_string,
                                  apr_pool_t *result_pool);

/* Return true iff @a version represents a version number of at least
 * the level represented by @a major, @a minor, and @a patch.
 *
 * @since New in 1.8.
 */
svn_boolean_t
svn_version__at_least(const svn_version_t *version,
                      int major,
                      int minor,
                      int patch);

/** @} */

/**
 * @defgroup svn_compress Data (de-)compression API
 * @{
 */

/* This is at least as big as the largest size of an integer that
   svn__encode_uint() can generate; it is sufficient for creating buffers
   for it to write into.  This assumes that integers are at most 64 bits,
   and so 10 bytes (with 7 bits of information each) are sufficient to
   represent them. */
#define SVN__MAX_ENCODED_UINT_LEN 10

/* Compression method parameters for svn__encode_uint. */

/* No compression (but a length prefix will still be added to the buffer) */
#define SVN__COMPRESSION_NONE         0

/* Fastest, least effective compression method & level provided by zlib. */
#define SVN__COMPRESSION_ZLIB_MIN     1

/* Default compression method & level provided by zlib. */
#define SVN__COMPRESSION_ZLIB_DEFAULT 5

/* Slowest, best compression method & level provided by zlib. */
#define SVN__COMPRESSION_ZLIB_MAX     9

/* Encode VAL into the buffer P using the variable-length 7b/8b unsigned
   integer format.  Return the incremented value of P after the
   encoded bytes have been written.  P must point to a buffer of size
   at least SVN__MAX_ENCODED_UINT_LEN.

   This encoding uses the high bit of each byte as a continuation bit
   and the other seven bits as data bits.  High-order data bits are
   encoded first, followed by lower-order bits, so the value can be
   reconstructed by concatenating the data bits from left to right and
   interpreting the result as a binary number.  Examples (brackets
   denote byte boundaries, spaces are for clarity only):

           1 encodes as [0 0000001]
          33 encodes as [0 0100001]
         129 encodes as [1 0000001] [0 0000001]
        2000 encodes as [1 0001111] [0 1010000]
*/
unsigned char *
svn__encode_uint(unsigned char *p, apr_uint64_t val);

/* Wrapper around svn__encode_uint using the LSB to store the sign:
 *
 * If VAL >= 0
 *   UINT_VAL = 2 * VAL
 * else
 *   UINT_VAL = (- 2 * VAL) - 1
 */
unsigned char *
svn__encode_int(unsigned char *p, apr_int64_t val);

/* Decode an unsigned 7b/8b-encoded integer into *VAL and return a pointer
   to the byte after the integer.  The bytes to be decoded live in the
   range [P..END-1].  If these bytes do not contain a whole encoded
   integer, return NULL; in this case *VAL is undefined.

   See the comment for svn__encode_uint() earlier in this file for more
   detail on the encoding format.  */
const unsigned char *
svn__decode_uint(apr_uint64_t *val,
                 const unsigned char *p,
                 const unsigned char *end);

/* Wrapper around svn__decode_uint, reversing the transformation performed
 * by svn__encode_int.
 */
const unsigned char *
svn__decode_int(apr_int64_t *val,
                const unsigned char *p,
                const unsigned char *end);

/* Compress the data from DATA with length LEN, it according to the
 * specified COMPRESSION_METHOD and write the result to OUT.
 * SVN__COMPRESSION_NONE is valid for COMPRESSION_METHOD.
 */
svn_error_t *
svn__compress_zlib(const void *data, apr_size_t len,
                   svn_stringbuf_t *out,
                   int compression_method);

/* Decompress the compressed data from DATA with length LEN and write the
 * result to OUT.  Return an error if the decompressed size is larger than
 * LIMIT.
 */
svn_error_t *
svn__decompress_zlib(const void *data, apr_size_t len,
                     svn_stringbuf_t *out,
                     apr_size_t limit);

/* Same as svn__compress_zlib(), but use LZ4 compression.  Note that
 * while the declaration of this function uses apr_size_t, it expects
 * blocks of size not exceeding LZ4_MAX_INPUT_SIZE.  The caller should
 * ensure that the proper size is passed to this function.
 */
svn_error_t *
svn__compress_lz4(const void *data, apr_size_t len,
                  svn_stringbuf_t *out);

/* Same as svn__decompress_zlib(), but use LZ4 compression.  The caller
 * should ensure that the size and limit passed to this function do not
 * exceed INT_MAX.
 */
svn_error_t *
svn__decompress_lz4(const void *data, apr_size_t len,
                    svn_stringbuf_t *out,
                    apr_size_t limit);

/** @} */

/**
 * @defgroup svn_root_pools Recycle-able root pools API
 * @{
 */

/* Opaque thread-safe container for unused / recylcleable root pools.
 *
 * Recyling root pools (actually, their allocators) circumvents a
 * scalability bottleneck in the OS memory management when multi-threaded
 * applications frequently create and destroy allocators.
 */
typedef struct svn_root_pools__t svn_root_pools__t;

/* Create a new root pools container and return it in *POOLS.
 */
svn_error_t *
svn_root_pools__create(svn_root_pools__t **pools);

/* Return a currently unused pool from POOLS.  If POOLS is empty, create a
 * new root pool and return that.  The pool returned is not thread-safe.
 */
apr_pool_t *
svn_root_pools__acquire_pool(svn_root_pools__t *pools);

/* Clear and release the given root POOL and put it back into POOLS.
 * If that fails, destroy POOL.
 */
void
svn_root_pools__release_pool(apr_pool_t *pool,
                             svn_root_pools__t *pools);

/** @} */

/**
 * @defgroup svn_config_private Private configuration handling API
 * @{
 */

/* Future attempts to modify CFG will trigger an assertion. */
void
svn_config__set_read_only(svn_config_t *cfg,
                          apr_pool_t *scratch_pool);

/* Return TRUE, if CFG cannot be modified. */
svn_boolean_t
svn_config__is_read_only(svn_config_t *cfg);

/* Return TRUE, if OPTION in SECTION in CFG exists and does not require
 * further expansion (due to either containing no placeholders or already
 * having been expanded). */
svn_boolean_t
svn_config__is_expanded(svn_config_t *cfg,
                        const char *section,
                        const char *option);

/* Return a shallow copy of SCR in POOL.  If SRC is read-only, different
 * shallow copies may be used from different threads.
 *
 * Any single r/o svn_config_t or shallow copy is not thread-safe because
 * it contains shared buffers for tempoary data.
 */
svn_config_t *
svn_config__shallow_copy(svn_config_t *src,
                         apr_pool_t *pool);

/* Add / replace SECTION in TARGET with the same section from SOURCE by
 * simply adding a reference to it.  If TARGET is read-only, the sections
 * list in target gets duplicated before the modification.
 *
 * This is an API tailored for use by the svn_repos__authz_pool_t API to
 * prevent breach of encapsulation.
 */
void
svn_config__shallow_replace_section(svn_config_t *target,
                                    svn_config_t *source,
                                    const char *section);

/* Allocate *CFG_HASH and populate it with default, empty,
 * svn_config_t for the configuration categories (@c
 * SVN_CONFIG_CATEGORY_SERVERS, @c SVN_CONFIG_CATEGORY_CONFIG, etc.).
 * This returns a hash equivalent to svn_config_get_config when the
 * config files are empty.
 */
svn_error_t *
svn_config__get_default_config(apr_hash_t **cfg_hash,
                               apr_pool_t *pool);

/** @} */


/**
 * @defgroup svn_bit_array Packed bit array handling API
 * @{
 */

/* This opaque data struct is an alternative to an INT->VOID hash.
 *
 * Technically, it is an automatically growing packed bit array.
 * All indexes not previously set are implicitly 0 and setting it will
 * grow the array as needed.
 */
typedef struct svn_bit_array__t svn_bit_array__t;

/* Return a new bit array allocated in POOL.  MAX is a mere hint for
 * the initial size of the array in bits.
 */
svn_bit_array__t *
svn_bit_array__create(apr_size_t max,
                      apr_pool_t *pool);

/* Set bit at index IDX in ARRAY to VALUE.  If necessary, grow the
 * underlying data buffer, i.e. any IDX is valid unless we run OOM.
 */
void
svn_bit_array__set(svn_bit_array__t *array,
                   apr_size_t idx,
                   svn_boolean_t value);

/* Get the bit value at index IDX in ARRAY.  Bits not previously accessed
 * are implicitly 0 (or FALSE).  That implies IDX can never be out-of-range.
 */
svn_boolean_t
svn_bit_array__get(svn_bit_array__t *array,
                   apr_size_t idx);

/* Return the global pool used by the DSO loader, this may be NULL if
   no DSOs have been loaded. */
apr_pool_t *
svn_dso__pool(void);

/** @} */


/* Return the xml (expat) version we compiled against. */
const char *svn_xml__compiled_version(void);

/* Return the xml (expat) version we run against. */
const char *svn_xml__runtime_version(void);

/* Return the zlib version we compiled against. */
const char *svn_zlib__compiled_version(void);

/* Return the zlib version we run against. */
const char *svn_zlib__runtime_version(void);

/* Return the lz4 version we compiled against. */
const char *svn_lz4__compiled_version(void);

/* Return the lz4 version we run against as a composed value:
 * major * 100 * 100 + minor * 100 + release
 */
int svn_lz4__runtime_version(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_SUBR_PRIVATE_H */
