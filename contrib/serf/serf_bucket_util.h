/* ====================================================================
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

#ifndef SERF_BUCKET_UTIL_H
#define SERF_BUCKET_UTIL_H

/**
 * @file serf_bucket_util.h
 * @brief This header defines a set of functions and other utilities
 * for implementing buckets. It is not needed by users of the bucket
 * system.
 */

#include "serf.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * Basic bucket creation function.
 *
 * This function will create a bucket of @a type, allocating the necessary
 * memory from @a allocator. The @a data bucket-private information will
 * be stored into the bucket.
 */
serf_bucket_t *serf_bucket_create(
    const serf_bucket_type_t *type,
    serf_bucket_alloc_t *allocator,
    void *data);

/**
 * Default implementation of the @see read_iovec functionality.
 *
 * This function will use the @see read function to get a block of memory,
 * then return it in the iovec.
 */
apr_status_t serf_default_read_iovec(
    serf_bucket_t *bucket,
    apr_size_t requested,
    int vecs_size,
    struct iovec *vecs,
    int *vecs_used);

/**
 * Default implementation of the @see read_for_sendfile functionality.
 *
 * This function will use the @see read function to get a block of memory,
 * then return it as a header. No file will be returned.
 */
apr_status_t serf_default_read_for_sendfile(
    serf_bucket_t *bucket,
    apr_size_t requested,
    apr_hdtr_t *hdtr,
    apr_file_t **file,
    apr_off_t *offset,
    apr_size_t *len);

/**
 * Default implementation of the @see read_bucket functionality.
 *
 * This function will always return NULL, indicating that the @a type
 * of bucket cannot be found within @a bucket.
 */
serf_bucket_t *serf_default_read_bucket(
    serf_bucket_t *bucket,
    const serf_bucket_type_t *type);

/**
 * Default implementation of the @see destroy functionality.
 *
 * This function will return the @a bucket to its allcoator.
 */
void serf_default_destroy(
    serf_bucket_t *bucket);


/**
 * Default implementation of the @see destroy functionality.
 *
 * This function will return the @a bucket, and the data member to its
 * allocator.
 */
void serf_default_destroy_and_data(
    serf_bucket_t *bucket);


/**
 * Allocate @a size bytes of memory using @a allocator.
 *
 * Returns NULL of the requested memory size could not be allocated.
 */
void *serf_bucket_mem_alloc(
    serf_bucket_alloc_t *allocator,
    apr_size_t size);

/**
 * Allocate @a size bytes of memory using @a allocator and set all of the
 * memory to 0.
 *
 * Returns NULL of the requested memory size could not be allocated.
 */
void *serf_bucket_mem_calloc(
    serf_bucket_alloc_t *allocator,
    apr_size_t size);

/**
 * Free the memory at @a block, returning it to @a allocator.
 */
void serf_bucket_mem_free(
    serf_bucket_alloc_t *allocator,
    void *block);


/**
 * Analogous to apr_pstrmemdup, using a bucket allocator instead.
 */
char *serf_bstrmemdup(
    serf_bucket_alloc_t *allocator,
    const char *str,
    apr_size_t size);

/**
 * Analogous to apr_pmemdup, using a bucket allocator instead.
 */
void * serf_bmemdup(
    serf_bucket_alloc_t *allocator,
    const void *mem,
    apr_size_t size);

/**
 * Analogous to apr_pstrdup, using a bucket allocator instead.
 */
char * serf_bstrdup(
    serf_bucket_alloc_t *allocator,
    const char *str);

/**
 * Analogous to apr_pstrcatv, using a bucket allocator instead.
 */
char * serf_bstrcatv(
    serf_bucket_alloc_t *allocator,
    struct iovec *vec,
    int vecs,
    apr_size_t *bytes_written);

/**
 * Read data up to a newline.
 *
 * @a acceptable contains the allowed forms of a newline, and @a found
 * will return the particular newline type that was found. If a newline
 * is not found, then SERF_NEWLINE_NONE will be placed in @a found.
 *
 * @a data should contain a pointer to the data to be scanned. @a len
 * should specify the length of that data buffer. On exit, @a data will
 * be advanced past the newline, and @a len will specify the remaining
 * amount of data in the buffer.
 *
 * Given this pattern of behavior, the caller should store the initial
 * value of @a data as the line start. The difference between the
 * returned value of @a data and the saved start is the length of the
 * line.
 *
 * Note that the newline character(s) will remain within the buffer.
 * This function scans at a byte level for the newline characters. Thus,
 * the data buffer may contain NUL characters. As a corollary, this
 * function only works on 8-bit character encodings.
 *
 * If the data is fully consumed (@a len gets set to zero) and a CR
 * character is found at the end and the CRLF sequence is allowed, then
 * this function may store SERF_NEWLINE_CRLF_SPLIT into @a found. The
 * caller should take particular consideration for the CRLF sequence
 * that may be split across data buffer boundaries.
 */
void serf_util_readline(
    const char **data,
    apr_size_t *len,
    int acceptable,
    int *found);


/** The buffer size used within @see serf_databuf_t. */
#define SERF_DATABUF_BUFSIZE 8000

/** Callback function which is used to refill the data buffer.
 *
 * The function takes @a baton, which is the @see read_baton value
 * from the serf_databuf_t structure. Data should be placed into
 * a buffer specified by @a buf, which is @a bufsize bytes long.
 * The amount of data read should be returned in @a len.
 *
 * APR_EOF should be returned if no more data is available. APR_EAGAIN
 * should be returned, rather than blocking. In both cases, @a buf
 * should be filled in and @a len set, as appropriate.
 */
typedef apr_status_t (*serf_databuf_reader_t)(
    void *baton,
    apr_size_t bufsize,
    char *buf,
    apr_size_t *len);

/**
 * This structure is used as an intermediate data buffer for some "external"
 * source of data. It works as a scratch pad area for incoming data to be
 * stored, and then returned as a ptr/len pair by the bucket read functions.
 *
 * This structure should be initialized by calling @see serf_databuf_init.
 * Users should not bother to zero the structure beforehand.
 */
typedef struct {
    /** The current data position within the buffer. */
    const char *current;

    /** Amount of data remaining in the buffer. */
    apr_size_t remaining;

    /** Callback function. */
    serf_databuf_reader_t read;

    /** A baton to hold context-specific data. */
    void *read_baton;

    /** Records the status from the last @see read operation. */
    apr_status_t status;

    /** Holds the data until it can be returned. */
    char buf[SERF_DATABUF_BUFSIZE];

} serf_databuf_t;

/**
 * Initialize the @see serf_databuf_t structure specified by @a databuf.
 */
void serf_databuf_init(
    serf_databuf_t *databuf);

/**
 * Implement a bucket-style read function from the @see serf_databuf_t
 * structure given by @a databuf.
 *
 * The @a requested, @a data, and @a len fields are interpreted and used
 * as in the read function of @see serf_bucket_t.
 */
apr_status_t serf_databuf_read(
    serf_databuf_t *databuf,
    apr_size_t requested,
    const char **data,
    apr_size_t *len);

/**
 * Implement a bucket-style readline function from the @see serf_databuf_t
 * structure given by @a databuf.
 *
 * The @a acceptable, @a found, @a data, and @a len fields are interpreted
 * and used as in the read function of @see serf_bucket_t.
 */
apr_status_t serf_databuf_readline(
    serf_databuf_t *databuf,
    int acceptable,
    int *found,
    const char **data,
    apr_size_t *len);

/**
 * Implement a bucket-style peek function from the @see serf_databuf_t
 * structure given by @a databuf.
 *
 * The @a data, and @a len fields are interpreted and used as in the
 * peek function of @see serf_bucket_t.
 */
apr_status_t serf_databuf_peek(
    serf_databuf_t *databuf,
    const char **data,
    apr_size_t *len);


#ifdef __cplusplus
}
#endif

#endif	/* !SERF_BUCKET_UTIL_H */
