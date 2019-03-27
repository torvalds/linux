/* string_table.h : interface to string tables, private to libsvn_fs_x
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

#ifndef SVN_LIBSVN_FS_X_STRING_TABLE_H
#define SVN_LIBSVN_FS_X_STRING_TABLE_H

#include "svn_io.h"
#include "private/svn_temp_serializer.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* A string table is a very space efficient, read-only representation for
 * a set of strings with high degreed of prefix and postfix overhead.
 *
 * Creating a string table is a two-stage process:  Use a builder class,
 * stuff all the strings in there and let it then do the heavy lifting of
 * classification and compression to create the actual string table object.
 *
 * We will use this for the various path values in FSX change lists and
 * node revision items.
 */

/* the string table builder */
typedef struct string_table_builder_t string_table_builder_t;

/* the string table */
typedef struct string_table_t string_table_t;

/* Returns a new string table builder object, allocated in RESULT_POOL.
 */
string_table_builder_t *
svn_fs_x__string_table_builder_create(apr_pool_t *result_pool);

/* Add an arbitrary NUL-terminated C-string STRING of the given length LEN
 * to BUILDER.  Return the index of that string in the future string table.
 * If LEN is 0, determine the length of the C-string internally.
 */
apr_size_t
svn_fs_x__string_table_builder_add(string_table_builder_t *builder,
                                   const char *string,
                                   apr_size_t len);

/* Return an estimate for the on-disk size of the resulting string table.
 * The estimate may err in both directions but tends to overestimate the
 * space requirements for larger tables.
 */
apr_size_t
svn_fs_x__string_table_builder_estimate_size(string_table_builder_t *builder);

/* From the given BUILDER object, create a string table object allocated
 * in RESULT_POOL that contains all strings previously added to BUILDER.
 */
string_table_t *
svn_fs_x__string_table_create(const string_table_builder_t *builder,
                              apr_pool_t *result_pool);

/* Extract string number INDEX from TABLE and return a copy of it allocated
 * in RESULT_POOL.  If LENGTH is not NULL, set *LENGTH to strlen() of the
 * result string.  Returns an empty string for invalid indexes.
 */
const char*
svn_fs_x__string_table_get(const string_table_t *table,
                           apr_size_t index,
                           apr_size_t *length,
                           apr_pool_t *result_pool);

/* Write a serialized representation of the string table TABLE to STREAM.
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__write_string_table(svn_stream_t *stream,
                             const string_table_t *table,
                             apr_pool_t *scratch_pool);

/* Read the serialized string table representation from STREAM and return
 * the resulting runtime representation in *TABLE_P.  Allocate it in
 * RESULT_POOL and use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__read_string_table(string_table_t **table_p,
                            svn_stream_t *stream,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/* Serialize string table *ST within the serialization CONTEXT.
 */
void
svn_fs_x__serialize_string_table(svn_temp_serializer__context_t *context,
                                 string_table_t **st);

/* Deserialize string table *TABLE within the BUFFER.
 */
void
svn_fs_x__deserialize_string_table(void *buffer,
                                   string_table_t **table);

/* Extract string number INDEX from the cache serialized representation at
 * TABLE and return a copy of it allocated in RESULT_POOL.  If LENGTH is not
 * NULL, set *LENGTH to strlen() of the result string.  Returns an empty
 * string for invalid indexes.
 */
const char*
svn_fs_x__string_table_get_func(const string_table_t *table,
                                apr_size_t idx,
                                apr_size_t *length,
                                apr_pool_t *result_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_X_STRING_TABLE_H */
