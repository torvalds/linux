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

#include "apr.h"
#include "apr_general.h"
#include "apr_file_io.h"
#include "apr_buckets.h"

#if APR_HAS_MMAP
#include "apr_mmap.h"

/* mmap support for static files based on ideas from John Heidemann's
 * patch against 1.0.5.  See
 * <http://www.isi.edu/~johnh/SOFTWARE/APACHE/index.html>.
 */

#endif /* APR_HAS_MMAP */

static void file_bucket_destroy(void *data)
{
    apr_bucket_file *f = data;

    if (apr_bucket_shared_destroy(f)) {
        /* no need to close the file here; it will get
         * done automatically when the pool gets cleaned up */
        apr_bucket_free(f);
    }
}

#if APR_HAS_MMAP
static int file_make_mmap(apr_bucket *e, apr_size_t filelength,
                           apr_off_t fileoffset, apr_pool_t *p)
{
    apr_bucket_file *a = e->data;
    apr_mmap_t *mm;

    if (!a->can_mmap) {
        return 0;
    }

    if (filelength > APR_MMAP_LIMIT) {
        if (apr_mmap_create(&mm, a->fd, fileoffset, APR_MMAP_LIMIT,
                            APR_MMAP_READ, p) != APR_SUCCESS)
        {
            return 0;
        }
        apr_bucket_split(e, APR_MMAP_LIMIT);
        filelength = APR_MMAP_LIMIT;
    }
    else if ((filelength < APR_MMAP_THRESHOLD) ||
             (apr_mmap_create(&mm, a->fd, fileoffset, filelength,
                              APR_MMAP_READ, p) != APR_SUCCESS))
    {
        return 0;
    }
    apr_bucket_mmap_make(e, mm, 0, filelength);
    file_bucket_destroy(a);
    return 1;
}
#endif

static apr_status_t file_bucket_read(apr_bucket *e, const char **str,
                                     apr_size_t *len, apr_read_type_e block)
{
    apr_bucket_file *a = e->data;
    apr_file_t *f = a->fd;
    apr_bucket *b = NULL;
    char *buf;
    apr_status_t rv;
    apr_size_t filelength = e->length;  /* bytes remaining in file past offset */
    apr_off_t fileoffset = e->start;
#if APR_HAS_THREADS && !APR_HAS_XTHREAD_FILES
    apr_int32_t flags;
#endif

#if APR_HAS_MMAP
    if (file_make_mmap(e, filelength, fileoffset, a->readpool)) {
        return apr_bucket_read(e, str, len, block);
    }
#endif

#if APR_HAS_THREADS && !APR_HAS_XTHREAD_FILES
    if ((flags = apr_file_flags_get(f)) & APR_FOPEN_XTHREAD) {
        /* this file descriptor is shared across multiple threads and
         * this OS doesn't support that natively, so as a workaround
         * we must reopen the file into a->readpool */
        const char *fname;
        apr_file_name_get(&fname, f);

        rv = apr_file_open(&f, fname, (flags & ~APR_FOPEN_XTHREAD), 0, a->readpool);
        if (rv != APR_SUCCESS)
            return rv;

        a->fd = f;
    }
#endif

    *len = (filelength > APR_BUCKET_BUFF_SIZE)
               ? APR_BUCKET_BUFF_SIZE
               : filelength;
    *str = NULL;  /* in case we die prematurely */
    buf = apr_bucket_alloc(*len, e->list);

    /* Handle offset ... */
    rv = apr_file_seek(f, APR_SET, &fileoffset);
    if (rv != APR_SUCCESS) {
        apr_bucket_free(buf);
        return rv;
    }
    rv = apr_file_read(f, buf, len);
    if (rv != APR_SUCCESS && rv != APR_EOF) {
        apr_bucket_free(buf);
        return rv;
    }
    filelength -= *len;
    /*
     * Change the current bucket to refer to what we read,
     * even if we read nothing because we hit EOF.
     */
    apr_bucket_heap_make(e, buf, *len, apr_bucket_free);

    /* If we have more to read from the file, then create another bucket */
    if (filelength > 0 && rv != APR_EOF) {
        /* for efficiency, we can just build a new apr_bucket struct
         * to wrap around the existing file bucket */
        b = apr_bucket_alloc(sizeof(*b), e->list);
        b->start  = fileoffset + (*len);
        b->length = filelength;
        b->data   = a;
        b->type   = &apr_bucket_type_file;
        b->free   = apr_bucket_free;
        b->list   = e->list;
        APR_BUCKET_INSERT_AFTER(e, b);
    }
    else {
        file_bucket_destroy(a);
    }

    *str = buf;
    return rv;
}

APU_DECLARE(apr_bucket *) apr_bucket_file_make(apr_bucket *b, apr_file_t *fd,
                                               apr_off_t offset,
                                               apr_size_t len, apr_pool_t *p)
{
    apr_bucket_file *f;

    f = apr_bucket_alloc(sizeof(*f), b->list);
    f->fd = fd;
    f->readpool = p;
#if APR_HAS_MMAP
    f->can_mmap = 1;
#endif

    b = apr_bucket_shared_make(b, f, offset, len);
    b->type = &apr_bucket_type_file;

    return b;
}

APU_DECLARE(apr_bucket *) apr_bucket_file_create(apr_file_t *fd,
                                                 apr_off_t offset,
                                                 apr_size_t len, apr_pool_t *p,
                                                 apr_bucket_alloc_t *list)
{
    apr_bucket *b = apr_bucket_alloc(sizeof(*b), list);

    APR_BUCKET_INIT(b);
    b->free = apr_bucket_free;
    b->list = list;
    return apr_bucket_file_make(b, fd, offset, len, p);
}

APU_DECLARE(apr_status_t) apr_bucket_file_enable_mmap(apr_bucket *e,
                                                      int enabled)
{
#if APR_HAS_MMAP
    apr_bucket_file *a = e->data;
    a->can_mmap = enabled;
    return APR_SUCCESS;
#else
    return APR_ENOTIMPL;
#endif /* APR_HAS_MMAP */
}


static apr_status_t file_bucket_setaside(apr_bucket *data, apr_pool_t *reqpool)
{
    apr_bucket_file *a = data->data;
    apr_file_t *fd = NULL;
    apr_file_t *f = a->fd;
    apr_pool_t *curpool = apr_file_pool_get(f);

    if (apr_pool_is_ancestor(curpool, reqpool)) {
        return APR_SUCCESS;
    }

    if (!apr_pool_is_ancestor(a->readpool, reqpool)) {
        a->readpool = reqpool;
    }

    apr_file_setaside(&fd, f, reqpool);
    a->fd = fd;
    return APR_SUCCESS;
}

APU_DECLARE_DATA const apr_bucket_type_t apr_bucket_type_file = {
    "FILE", 5, APR_BUCKET_DATA,
    file_bucket_destroy,
    file_bucket_read,
    file_bucket_setaside,
    apr_bucket_shared_split,
    apr_bucket_shared_copy
};
