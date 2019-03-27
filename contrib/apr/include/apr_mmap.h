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

#ifndef APR_MMAP_H
#define APR_MMAP_H

/**
 * @file apr_mmap.h
 * @brief APR MMAP routines
 */

#include "apr.h"
#include "apr_pools.h"
#include "apr_errno.h"
#include "apr_ring.h"
#include "apr_file_io.h"        /* for apr_file_t */

#ifdef BEOS
#include <kernel/OS.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup apr_mmap MMAP (Memory Map) Routines
 * @ingroup APR 
 * @{
 */

/** MMap opened for reading */
#define APR_MMAP_READ    1
/** MMap opened for writing */
#define APR_MMAP_WRITE   2

/** @see apr_mmap_t */
typedef struct apr_mmap_t            apr_mmap_t;

/**
 * @remark
 * As far as I can tell the only really sane way to store an MMAP is as a
 * void * and a length.  BeOS requires this area_id, but that's just a little
 * something extra.  I am exposing this type, because it doesn't make much
 * sense to keep it private, and opening it up makes some stuff easier in
 * Apache.
 */
/** The MMAP structure */
struct apr_mmap_t {
    /** The pool the mmap structure was allocated out of. */
    apr_pool_t *cntxt;
#ifdef BEOS
    /** An area ID.  Only valid on BeOS */
    area_id area;
#endif
#ifdef WIN32
    /** The handle of the file mapping */
    HANDLE mhandle;
    /** The start of the real memory page area (mapped view) */
    void *mv;
    /** The physical start, size and offset */
    apr_off_t  pstart;
    apr_size_t psize;
    apr_off_t  poffset;
#endif
    /** The start of the memory mapped area */
    void *mm;
    /** The amount of data in the mmap */
    apr_size_t size;
    /** ring of apr_mmap_t's that reference the same
     * mmap'ed region; acts in place of a reference count */
    APR_RING_ENTRY(apr_mmap_t) link;
};

#if APR_HAS_MMAP || defined(DOXYGEN)

/** @def APR_MMAP_THRESHOLD 
 * Files have to be at least this big before they're mmap()d.  This is to deal
 * with systems where the expense of doing an mmap() and an munmap() outweighs
 * the benefit for small files.  It shouldn't be set lower than 1.
 */
#ifdef MMAP_THRESHOLD
#  define APR_MMAP_THRESHOLD              MMAP_THRESHOLD
#else
#  ifdef SUNOS4
#    define APR_MMAP_THRESHOLD            (8*1024)
#  else
#    define APR_MMAP_THRESHOLD            1
#  endif /* SUNOS4 */
#endif /* MMAP_THRESHOLD */

/** @def APR_MMAP_LIMIT
 * Maximum size of MMap region
 */
#ifdef MMAP_LIMIT
#  define APR_MMAP_LIMIT                  MMAP_LIMIT
#else
#  define APR_MMAP_LIMIT                  (4*1024*1024)
#endif /* MMAP_LIMIT */

/** Can this file be MMaped */
#define APR_MMAP_CANDIDATE(filelength) \
    ((filelength >= APR_MMAP_THRESHOLD) && (filelength < APR_MMAP_LIMIT))

/*   Function definitions */

/** 
 * Create a new mmap'ed file out of an existing APR file.
 * @param newmmap The newly created mmap'ed file.
 * @param file The file to turn into an mmap.
 * @param offset The offset into the file to start the data pointer at.
 * @param size The size of the file
 * @param flag bit-wise or of:
 * <PRE>
 *          APR_MMAP_READ       MMap opened for reading
 *          APR_MMAP_WRITE      MMap opened for writing
 * </PRE>
 * @param cntxt The pool to use when creating the mmap.
 */
APR_DECLARE(apr_status_t) apr_mmap_create(apr_mmap_t **newmmap, 
                                          apr_file_t *file, apr_off_t offset,
                                          apr_size_t size, apr_int32_t flag,
                                          apr_pool_t *cntxt);

/**
 * Duplicate the specified MMAP.
 * @param new_mmap The structure to duplicate into. 
 * @param old_mmap The mmap to duplicate.
 * @param p The pool to use for new_mmap.
 */         
APR_DECLARE(apr_status_t) apr_mmap_dup(apr_mmap_t **new_mmap,
                                       apr_mmap_t *old_mmap,
                                       apr_pool_t *p);

/**
 * Remove a mmap'ed.
 * @param mm The mmap'ed file.
 */
APR_DECLARE(apr_status_t) apr_mmap_delete(apr_mmap_t *mm);

/** 
 * Move the pointer into the mmap'ed file to the specified offset.
 * @param addr The pointer to the offset specified.
 * @param mm The mmap'ed file.
 * @param offset The offset to move to.
 */
APR_DECLARE(apr_status_t) apr_mmap_offset(void **addr, apr_mmap_t *mm, 
                                          apr_off_t offset);

#endif /* APR_HAS_MMAP */

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* ! APR_MMAP_H */
