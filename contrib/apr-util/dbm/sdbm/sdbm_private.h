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

/*
 * sdbm - ndbm work-alike hashed database library
 * based on Per-Ake Larson's Dynamic Hashing algorithms. BIT 18 (1978).
 * author: oz@nexus.yorku.ca
 */

#ifndef SDBM_PRIVATE_H
#define SDBM_PRIVATE_H

#include "apr.h"
#include "apr_pools.h"
#include "apr_file_io.h"
#include "apr_errno.h" /* for apr_status_t */

#if 0
/* if the block/page size is increased, it breaks perl apr_sdbm_t compatibility */
#define DBLKSIZ 16384
#define PBLKSIZ 8192
#define PAIRMAX 8008			/* arbitrary on PBLKSIZ-N */
#else
#define DBLKSIZ 4096
#define PBLKSIZ 1024
#define PAIRMAX 1008			/* arbitrary on PBLKSIZ-N */
#endif
#define SPLTMAX	10			/* maximum allowed splits */

/* for apr_sdbm_t.flags */
#define SDBM_RDONLY	        0x1    /* data base open read-only */
#define SDBM_SHARED	        0x2    /* data base open for sharing */
#define SDBM_SHARED_LOCK	0x4    /* data base locked for shared read */
#define SDBM_EXCLUSIVE_LOCK	0x8    /* data base locked for write */

struct apr_sdbm_t {
    apr_pool_t *pool;
    apr_file_t *dirf;		       /* directory file descriptor */
    apr_file_t *pagf;		       /* page file descriptor */
    apr_int32_t flags;		       /* status/error flags, see below */
    long maxbno;		       /* size of dirfile in bits */
    long curbit;		       /* current bit number */
    long hmask;			       /* current hash mask */
    long blkptr;		       /* current block for nextkey */
    int  keyptr;		       /* current key for nextkey */
    long blkno;			       /* current page to read/write */
    long pagbno;		       /* current page in pagbuf */
    char pagbuf[PBLKSIZ];	       /* page file block buffer */
    long dirbno;		       /* current block in dirbuf */
    char dirbuf[DBLKSIZ];	       /* directory file block buffer */
    int  lckcnt;                       /* number of calls to sdbm_lock */
};


#define sdbm_hash apu__sdbm_hash
#define sdbm_nullitem apu__sdbm_nullitem

extern const apr_sdbm_datum_t sdbm_nullitem;

long sdbm_hash(const char *str, int len);

/*
 * zero the cache
 */
#define SDBM_INVALIDATE_CACHE(db, finfo) \
    do { db->dirbno = (!finfo.size) ? 0 : -1; \
         db->pagbno = -1; \
         db->maxbno = (long)(finfo.size * BYTESIZ); \
    } while (0);

#endif /* SDBM_PRIVATE_H */
