/*
 * delta.h:  private delta library things
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

/* ==================================================================== */


#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_delta.h"

#ifndef SVN_LIBSVN_DELTA_H
#define SVN_LIBSVN_DELTA_H


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Private interface for text deltas. */

/* The standard size of one svndiff window. */

#define SVN_DELTA_WINDOW_SIZE 102400


/* Context/baton for building an operation sequence. */

typedef struct svn_txdelta__ops_baton_t {
  int num_ops;                  /* current number of ops */
  int src_ops;                  /* current number of source copy ops */
  int ops_size;                 /* number of ops allocated */
  svn_txdelta_op_t *ops;        /* the operations */

  svn_stringbuf_t *new_data;    /* any new data used by the operations */
} svn_txdelta__ops_baton_t;


/* Insert a delta op into the delta window being built via BUILD_BATON. If
   OPCODE is svn_delta_new, bytes from NEW_DATA are copied into the window
   data and OFFSET is ignored.  Otherwise NEW_DATA is ignored. All
   allocations are performed in POOL. */
void svn_txdelta__insert_op(svn_txdelta__ops_baton_t *build_baton,
                            enum svn_delta_action opcode,
                            apr_size_t offset,
                            apr_size_t length,
                            const char *new_data,
                            apr_pool_t *pool);

/* Remove / truncate the last delta ops spanning the last MAX_LEN bytes
   from the delta window being built via BUILD_BATON starting.  Return the
   number of bytes that were actually removed. */
apr_size_t
svn_txdelta__remove_copy(svn_txdelta__ops_baton_t *build_baton,
                         apr_size_t max_len);

/* Allocate a delta window from POOL. */
svn_txdelta_window_t *
svn_txdelta__make_window(const svn_txdelta__ops_baton_t *build_baton,
                         apr_pool_t *pool);


/* Create xdelta window data. Allocate temporary data from POOL. */
void svn_txdelta__xdelta(svn_txdelta__ops_baton_t *build_baton,
                         const char *start,
                         apr_size_t source_len,
                         apr_size_t target_len,
                         apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_LIBSVN_DELTA_H */
