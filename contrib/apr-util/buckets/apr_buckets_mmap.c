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

#include "apr_buckets.h"

#if APR_HAS_MMAP

static apr_status_t mmap_bucket_read(apr_bucket *b, const char **str, 
                                     apr_size_t *length, apr_read_type_e block)
{
    apr_bucket_mmap *m = b->data;
    apr_status_t ok;
    void *addr;
   
    if (!m->mmap) {
        /* the apr_mmap_t was already cleaned up out from under us */
        return APR_EINVAL;
    }

    ok = apr_mmap_offset(&addr, m->mmap, b->start);
    if (ok != APR_SUCCESS) {
        return ok;
    }
    *str = addr;
    *length = b->length;
    return APR_SUCCESS;
}

static apr_status_t mmap_bucket_cleanup(void *data)
{
    /* the apr_mmap_t is about to disappear out from under us, so we
     * have no choice but to pretend it doesn't exist anymore.  the
     * refcount is now useless because there's nothing to refer to
     * anymore.  so the only valid action on any remaining referrer
     * is to delete it.  no more reads, no more anything. */
    apr_bucket_mmap *m = data;

    m->mmap = NULL;
    return APR_SUCCESS;
}

static void mmap_bucket_destroy(void *data)
{
    apr_bucket_mmap *m = data;

    if (apr_bucket_shared_destroy(m)) {
        if (m->mmap) {
            apr_pool_cleanup_kill(m->mmap->cntxt, m, mmap_bucket_cleanup);
            apr_mmap_delete(m->mmap);
        }
        apr_bucket_free(m);
    }
}

/*
 * XXX: are the start and length arguments useful?
 */
APU_DECLARE(apr_bucket *) apr_bucket_mmap_make(apr_bucket *b, apr_mmap_t *mm, 
                                               apr_off_t start, 
                                               apr_size_t length)
{
    apr_bucket_mmap *m;

    m = apr_bucket_alloc(sizeof(*m), b->list);
    m->mmap = mm;

    apr_pool_cleanup_register(mm->cntxt, m, mmap_bucket_cleanup,
                              apr_pool_cleanup_null);

    b = apr_bucket_shared_make(b, m, start, length);
    b->type = &apr_bucket_type_mmap;

    return b;
}


APU_DECLARE(apr_bucket *) apr_bucket_mmap_create(apr_mmap_t *mm, 
                                                 apr_off_t start, 
                                                 apr_size_t length,
                                                 apr_bucket_alloc_t *list)
{
    apr_bucket *b = apr_bucket_alloc(sizeof(*b), list);

    APR_BUCKET_INIT(b);
    b->free = apr_bucket_free;
    b->list = list;
    return apr_bucket_mmap_make(b, mm, start, length);
}

static apr_status_t mmap_bucket_setaside(apr_bucket *b, apr_pool_t *p)
{
    apr_bucket_mmap *m = b->data;
    apr_mmap_t *mm = m->mmap;
    apr_mmap_t *new_mm;
    apr_status_t ok;

    if (!mm) {
        /* the apr_mmap_t was already cleaned up out from under us */
        return APR_EINVAL;
    }

    /* shortcut if possible */
    if (apr_pool_is_ancestor(mm->cntxt, p)) {
        return APR_SUCCESS;
    }

    /* duplicate apr_mmap_t into new pool */
    ok = apr_mmap_dup(&new_mm, mm, p);
    if (ok != APR_SUCCESS) {
        return ok;
    }

    /* decrement refcount on old apr_bucket_mmap */
    mmap_bucket_destroy(m);

    /* create new apr_bucket_mmap pointing to new apr_mmap_t */
    apr_bucket_mmap_make(b, new_mm, b->start, b->length);

    return APR_SUCCESS;
}

APU_DECLARE_DATA const apr_bucket_type_t apr_bucket_type_mmap = {
    "MMAP", 5, APR_BUCKET_DATA,
    mmap_bucket_destroy,
    mmap_bucket_read,
    mmap_bucket_setaside,
    apr_bucket_shared_split,
    apr_bucket_shared_copy
};

#endif
