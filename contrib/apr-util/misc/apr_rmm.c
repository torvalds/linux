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

#include "apr_general.h"
#include "apr_rmm.h"
#include "apr_errno.h"
#include "apr_lib.h"
#include "apr_strings.h"

/* The RMM region is made up of two doubly-linked-list of blocks; the
 * list of used blocks, and the list of free blocks (either list may
 * be empty).  The base pointer, rmm->base, points at the beginning of
 * the shmem region in use.  Each block is addressable by an
 * apr_rmm_off_t value, which represents the offset from the base
 * pointer.  The term "address" is used here to mean such a value; an
 * "offset from rmm->base".
 *
 * The RMM region contains exactly one "rmm_hdr_block_t" structure,
 * the "header block", which is always stored at the base pointer.
 * The firstused field in this structure is the address of the first
 * block in the "used blocks" list; the firstfree field is the address
 * of the first block in the "free blocks" list.
 *
 * Each block is prefixed by an "rmm_block_t" structure, followed by
 * the caller-usable region represented by the block.  The next and
 * prev fields of the structure are zero if the block is at the end or
 * beginning of the linked-list respectively, or otherwise hold the
 * address of the next and previous blocks in the list.  ("address 0",
 * i.e. rmm->base is *not* a valid address for a block, since the
 * header block is always stored at that address).
 *
 * At creation, the RMM region is initialized to hold a single block
 * on the free list representing the entire available shm segment
 * (minus header block); subsequent allocation and deallocation of
 * blocks involves splitting blocks and coalescing adjacent blocks,
 * and switching them between the free and used lists as
 * appropriate. */

typedef struct rmm_block_t {
    apr_size_t size;
    apr_rmm_off_t prev;
    apr_rmm_off_t next;
} rmm_block_t;

/* Always at our apr_rmm_off(0):
 */
typedef struct rmm_hdr_block_t {
    apr_size_t abssize;
    apr_rmm_off_t /* rmm_block_t */ firstused;
    apr_rmm_off_t /* rmm_block_t */ firstfree;
} rmm_hdr_block_t;

#define RMM_HDR_BLOCK_SIZE (APR_ALIGN_DEFAULT(sizeof(rmm_hdr_block_t)))
#define RMM_BLOCK_SIZE (APR_ALIGN_DEFAULT(sizeof(rmm_block_t)))

struct apr_rmm_t {
    apr_pool_t *p;
    rmm_hdr_block_t *base;
    apr_size_t size;
    apr_anylock_t lock;
};

static apr_rmm_off_t find_block_by_offset(apr_rmm_t *rmm, apr_rmm_off_t next, 
                                          apr_rmm_off_t find, int includes)
{
    apr_rmm_off_t prev = 0;

    while (next) {
        struct rmm_block_t *blk = (rmm_block_t*)((char*)rmm->base + next);

        if (find == next)
            return next;

        /* Overshot? */
        if (find < next)
            return includes ? prev : 0;

        prev = next;
        next = blk->next;
    }
    return includes ? prev : 0;
}

static apr_rmm_off_t find_block_of_size(apr_rmm_t *rmm, apr_size_t size)
{
    apr_rmm_off_t next = rmm->base->firstfree;
    apr_rmm_off_t best = 0;
    apr_rmm_off_t bestsize = 0;

    while (next) {
        struct rmm_block_t *blk = (rmm_block_t*)((char*)rmm->base + next);

        if (blk->size == size)
            return next;

        if (blk->size >= size) {
            /* XXX: sub optimal algorithm 
             * We need the most thorough best-fit logic, since we can
             * never grow our rmm, we are SOL when we hit the wall.
             */
            if (!bestsize || (blk->size < bestsize)) {
                bestsize = blk->size;
                best = next;
            }
        }

        next = blk->next;
    }

    if (bestsize > RMM_BLOCK_SIZE + size) {
        struct rmm_block_t *blk = (rmm_block_t*)((char*)rmm->base + best);
        struct rmm_block_t *new = (rmm_block_t*)((char*)rmm->base + best + size);

        new->size = blk->size - size;
        new->next = blk->next;
        new->prev = best;

        blk->size = size;
        blk->next = best + size;

        if (new->next) {
            blk = (rmm_block_t*)((char*)rmm->base + new->next);
            blk->prev = best + size;
        }
    }

    return best;
}

static void move_block(apr_rmm_t *rmm, apr_rmm_off_t this, int free)
{   
    struct rmm_block_t *blk = (rmm_block_t*)((char*)rmm->base + this);

    /* close the gap */
    if (blk->prev) {
        struct rmm_block_t *prev = (rmm_block_t*)((char*)rmm->base + blk->prev);
        prev->next = blk->next;
    }
    else {
        if (free) {
            rmm->base->firstused = blk->next;
        }
        else {
            rmm->base->firstfree = blk->next;
        }
    }
    if (blk->next) {
        struct rmm_block_t *next = (rmm_block_t*)((char*)rmm->base + blk->next);
        next->prev = blk->prev;
    }

    /* now find it in the other list, pushing it to the head if required */
    if (free) {
        blk->prev = find_block_by_offset(rmm, rmm->base->firstfree, this, 1);
        if (!blk->prev) {
            blk->next = rmm->base->firstfree;
            rmm->base->firstfree = this;
        }
    }
    else {
        blk->prev = find_block_by_offset(rmm, rmm->base->firstused, this, 1);
        if (!blk->prev) {
            blk->next = rmm->base->firstused;
            rmm->base->firstused = this;
        }
    }

    /* and open it up */
    if (blk->prev) {
        struct rmm_block_t *prev = (rmm_block_t*)((char*)rmm->base + blk->prev);
        if (free && (blk->prev + prev->size == this)) {
            /* Collapse us into our predecessor */
            prev->size += blk->size;
            this = blk->prev;
            blk = prev;
        }
        else {
            blk->next = prev->next;
            prev->next = this;
        }
    }

    if (blk->next) {
        struct rmm_block_t *next = (rmm_block_t*)((char*)rmm->base + blk->next);
        if (free && (this + blk->size == blk->next)) {
            /* Collapse us into our successor */
            blk->size += next->size;
            blk->next = next->next;
            if (blk->next) {
                next = (rmm_block_t*)((char*)rmm->base + blk->next);
                next->prev = this;
            }
        }
        else {
            next->prev = this;
        }
    }
}

APU_DECLARE(apr_status_t) apr_rmm_init(apr_rmm_t **rmm, apr_anylock_t *lock, 
                                       void *base, apr_size_t size,
                                       apr_pool_t *p)
{
    apr_status_t rv;
    rmm_block_t *blk;
    apr_anylock_t nulllock;
    
    if (!lock) {
        nulllock.type = apr_anylock_none;
        nulllock.lock.pm = NULL;
        lock = &nulllock;
    }
    if ((rv = APR_ANYLOCK_LOCK(lock)) != APR_SUCCESS)
        return rv;

    (*rmm) = (apr_rmm_t *)apr_pcalloc(p, sizeof(apr_rmm_t));
    (*rmm)->p = p;
    (*rmm)->base = base;
    (*rmm)->size = size;
    (*rmm)->lock = *lock;

    (*rmm)->base->abssize = size;
    (*rmm)->base->firstused = 0;
    (*rmm)->base->firstfree = RMM_HDR_BLOCK_SIZE;

    blk = (rmm_block_t *)((char*)base + (*rmm)->base->firstfree);

    blk->size = size - (*rmm)->base->firstfree;
    blk->prev = 0;
    blk->next = 0;

    return APR_ANYLOCK_UNLOCK(lock);
}

APU_DECLARE(apr_status_t) apr_rmm_destroy(apr_rmm_t *rmm)
{
    apr_status_t rv;
    rmm_block_t *blk;

    if ((rv = APR_ANYLOCK_LOCK(&rmm->lock)) != APR_SUCCESS) {
        return rv;
    }
    /* Blast it all --- no going back :) */
    if (rmm->base->firstused) {
        apr_rmm_off_t this = rmm->base->firstused;
        do {
            blk = (rmm_block_t *)((char*)rmm->base + this);
            this = blk->next;
            blk->next = blk->prev = 0;
        } while (this);
        rmm->base->firstused = 0;
    }
    if (rmm->base->firstfree) {
        apr_rmm_off_t this = rmm->base->firstfree;
        do {
            blk = (rmm_block_t *)((char*)rmm->base + this);
            this = blk->next;
            blk->next = blk->prev = 0;
        } while (this);
        rmm->base->firstfree = 0;
    }
    rmm->base->abssize = 0;
    rmm->size = 0;

    return APR_ANYLOCK_UNLOCK(&rmm->lock);
}

APU_DECLARE(apr_status_t) apr_rmm_attach(apr_rmm_t **rmm, apr_anylock_t *lock,
                                         void *base, apr_pool_t *p)
{
    apr_anylock_t nulllock;

    if (!lock) {
        nulllock.type = apr_anylock_none;
        nulllock.lock.pm = NULL;
        lock = &nulllock;
    }

    /* sanity would be good here */
    (*rmm) = (apr_rmm_t *)apr_pcalloc(p, sizeof(apr_rmm_t));
    (*rmm)->p = p;
    (*rmm)->base = base;
    (*rmm)->size = (*rmm)->base->abssize;
    (*rmm)->lock = *lock;
    return APR_SUCCESS;
}

APU_DECLARE(apr_status_t) apr_rmm_detach(apr_rmm_t *rmm) 
{
    /* A noop until we introduce locked/refcounts */
    return APR_SUCCESS;
}

APU_DECLARE(apr_rmm_off_t) apr_rmm_malloc(apr_rmm_t *rmm, apr_size_t reqsize)
{
    apr_size_t size;
    apr_rmm_off_t this;
    
    size = APR_ALIGN_DEFAULT(reqsize) + RMM_BLOCK_SIZE;
    if (size < reqsize) {
        return 0;
    }

    APR_ANYLOCK_LOCK(&rmm->lock);

    this = find_block_of_size(rmm, size);

    if (this) {
        move_block(rmm, this, 0);
        this += RMM_BLOCK_SIZE;
    }

    APR_ANYLOCK_UNLOCK(&rmm->lock);
    return this;
}

APU_DECLARE(apr_rmm_off_t) apr_rmm_calloc(apr_rmm_t *rmm, apr_size_t reqsize)
{
    apr_size_t size;
    apr_rmm_off_t this;
        
    size = APR_ALIGN_DEFAULT(reqsize) + RMM_BLOCK_SIZE;
    if (size < reqsize) {
        return 0;
    }

    APR_ANYLOCK_LOCK(&rmm->lock);

    this = find_block_of_size(rmm, size);

    if (this) {
        move_block(rmm, this, 0);
        this += RMM_BLOCK_SIZE;
        memset((char*)rmm->base + this, 0, size - RMM_BLOCK_SIZE);
    }

    APR_ANYLOCK_UNLOCK(&rmm->lock);
    return this;
}

APU_DECLARE(apr_rmm_off_t) apr_rmm_realloc(apr_rmm_t *rmm, void *entity,
                                           apr_size_t reqsize)
{
    apr_rmm_off_t this;
    apr_rmm_off_t old;
    struct rmm_block_t *blk;
    apr_size_t size, oldsize;

    if (!entity) {
        return apr_rmm_malloc(rmm, reqsize);
    }

    size = APR_ALIGN_DEFAULT(reqsize);
    if (size < reqsize) {
        return 0;
    }
    old = apr_rmm_offset_get(rmm, entity);

    if ((this = apr_rmm_malloc(rmm, size)) == 0) {
        return 0;
    }

    blk = (rmm_block_t*)((char*)rmm->base + old - RMM_BLOCK_SIZE);
    oldsize = blk->size;

    memcpy(apr_rmm_addr_get(rmm, this),
           apr_rmm_addr_get(rmm, old), oldsize < size ? oldsize : size);
    apr_rmm_free(rmm, old);

    return this;
}

APU_DECLARE(apr_status_t) apr_rmm_free(apr_rmm_t *rmm, apr_rmm_off_t this)
{
    apr_status_t rv;
    struct rmm_block_t *blk;

    /* A little sanity check is always healthy, especially here.
     * If we really cared, we could make this compile-time
     */
    if (this < RMM_HDR_BLOCK_SIZE + RMM_BLOCK_SIZE) {
        return APR_EINVAL;
    }

    this -= RMM_BLOCK_SIZE;

    blk = (rmm_block_t*)((char*)rmm->base + this);

    if ((rv = APR_ANYLOCK_LOCK(&rmm->lock)) != APR_SUCCESS) {
        return rv;
    }
    if (blk->prev) {
        struct rmm_block_t *prev = (rmm_block_t*)((char*)rmm->base + blk->prev);
        if (prev->next != this) {
            APR_ANYLOCK_UNLOCK(&rmm->lock);
            return APR_EINVAL;
        }
    }
    else {
        if (rmm->base->firstused != this) {
            APR_ANYLOCK_UNLOCK(&rmm->lock);
            return APR_EINVAL;
        }
    }

    if (blk->next) {
        struct rmm_block_t *next = (rmm_block_t*)((char*)rmm->base + blk->next);
        if (next->prev != this) {
            APR_ANYLOCK_UNLOCK(&rmm->lock);
            return APR_EINVAL;
        }
    }

    /* Ok, it remained [apparently] sane, so unlink it
     */
    move_block(rmm, this, 1);
    
    return APR_ANYLOCK_UNLOCK(&rmm->lock);
}

APU_DECLARE(void *) apr_rmm_addr_get(apr_rmm_t *rmm, apr_rmm_off_t entity) 
{
    /* debug-sanity checking here would be good
     */
    return (void*)((char*)rmm->base + entity);
}

APU_DECLARE(apr_rmm_off_t) apr_rmm_offset_get(apr_rmm_t *rmm, void* entity)
{
    /* debug, or always, sanity checking here would be good
     * since the primitive is apr_rmm_off_t, I don't mind penalizing
     * inverse conversions for safety, unless someone can prove that
     * there is no choice in some cases.
     */
    return ((char*)entity - (char*)rmm->base);
}

APU_DECLARE(apr_size_t) apr_rmm_overhead_get(int n) 
{
    /* overhead per block is at most APR_ALIGN_DEFAULT(1) wasted bytes
     * for alignment overhead, plus the size of the rmm_block_t
     * structure. */
    return RMM_HDR_BLOCK_SIZE + n * (RMM_BLOCK_SIZE + APR_ALIGN_DEFAULT(1));
}
