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

#include "apr_arch_shm.h"

#include "apr_general.h"
#include "apr_errno.h"
#include "apr_user.h"
#include "apr_strings.h"
#include "apr_hash.h"

#if APR_USE_SHMEM_MMAP_SHM
/* 
 *   For portable use, a shared memory object should be identified by a name of
 *   the form /somename; that is, a null-terminated string of up to NAME_MAX
 *   (i.e., 255) characters consisting of an initial slash, followed by one or
 *   more characters, none of which are slashes.
 */
#ifndef NAME_MAX
#define NAME_MAX 255
#endif

/* See proc_mutex.c and sem_open for the reason for all this! */
static unsigned int rshash (const char *p) {
    /* hash function from Robert Sedgwicks 'Algorithms in C' book */
    unsigned int b    = 378551;
    unsigned int a    = 63689;
    unsigned int retval = 0;

    for( ; *p; p++) {
        retval = retval * a + (*p);
        a *= b;
    }

    return retval;
}

static const char *make_shm_open_safe_name(const char *filename,
                                           apr_pool_t *pool)
{
    apr_ssize_t flen;
    unsigned int h1, h2;

    if (filename == NULL) {
        return NULL;
    }

    flen = strlen(filename);
    h1 = (apr_hashfunc_default(filename, &flen) & 0xffffffff);
    h2 = (rshash(filename) & 0xffffffff);
    return apr_psprintf(pool, "/ShM.%xH%x", h1, h2);

}
#endif

#if APR_USE_SHMEM_SHMGET
static key_t our_ftok(const char *filename)
{
    /* to help avoid collisions while still using
     * an easily recreated proj_id */
    apr_ssize_t slen = strlen(filename);
    return ftok(filename,
                (int)apr_hashfunc_default(filename, &slen));
}
#endif

static apr_status_t shm_cleanup_owner(void *m_)
{
    apr_shm_t *m = (apr_shm_t *)m_;

    /* anonymous shared memory */
    if (m->filename == NULL) {
#if APR_USE_SHMEM_MMAP_ZERO || APR_USE_SHMEM_MMAP_ANON
        if (munmap(m->base, m->realsize) == -1) {
            return errno;
        }
        return APR_SUCCESS;
#elif APR_USE_SHMEM_SHMGET_ANON
        if (shmdt(m->base) == -1) {
            return errno;
        }
        /* This segment will automatically remove itself after all
         * references have detached. */
        return APR_SUCCESS;
#endif
    }

    /* name-based shared memory */
    else {
#if APR_USE_SHMEM_MMAP_TMP
        if (munmap(m->base, m->realsize) == -1) {
            return errno;
        }
        if (access(m->filename, F_OK)) {
            return APR_SUCCESS;
        }
        else {
            return apr_file_remove(m->filename, m->pool);
        }
#elif APR_USE_SHMEM_MMAP_SHM
        if (munmap(m->base, m->realsize) == -1) {
            return errno;
        }
        if (shm_unlink(make_shm_open_safe_name(m->filename, m->pool)) == -1 && errno != ENOENT) {
            return errno;
        }
        return APR_SUCCESS;
#elif APR_USE_SHMEM_SHMGET
        /* Indicate that the segment is to be destroyed as soon
         * as all processes have detached. This also disallows any
         * new attachments to the segment. */
        if (shmctl(m->shmid, IPC_RMID, NULL) == -1 && errno != EINVAL) {
            return errno;
        }
        if (shmdt(m->base) == -1) {
            return errno;
        }
        if (access(m->filename, F_OK)) {
            return APR_SUCCESS;
        }
        else {
            return apr_file_remove(m->filename, m->pool);
        }
#else
        return APR_ENOTIMPL;
#endif
    }
}

APR_DECLARE(apr_status_t) apr_shm_create(apr_shm_t **m,
                                         apr_size_t reqsize, 
                                         const char *filename,
                                         apr_pool_t *pool)
{
    apr_shm_t *new_m;
    apr_status_t status;
#if APR_USE_SHMEM_SHMGET || APR_USE_SHMEM_SHMGET_ANON
    struct shmid_ds shmbuf;
    apr_uid_t uid;
    apr_gid_t gid;
#endif
#if APR_USE_SHMEM_MMAP_TMP || APR_USE_SHMEM_MMAP_SHM || \
    APR_USE_SHMEM_MMAP_ZERO
    int tmpfd;
#endif
#if APR_USE_SHMEM_SHMGET
    apr_size_t nbytes;
    key_t shmkey;
#endif
#if APR_USE_SHMEM_MMAP_ZERO || APR_USE_SHMEM_SHMGET || \
    APR_USE_SHMEM_MMAP_TMP || APR_USE_SHMEM_MMAP_SHM
    apr_file_t *file;   /* file where metadata is stored */
#endif

    /* Check if they want anonymous or name-based shared memory */
    if (filename == NULL) {
#if APR_USE_SHMEM_MMAP_ZERO || APR_USE_SHMEM_MMAP_ANON
        new_m = apr_palloc(pool, sizeof(apr_shm_t));
        new_m->pool = pool;
        new_m->reqsize = reqsize;
        new_m->realsize = reqsize + 
            APR_ALIGN_DEFAULT(sizeof(apr_size_t)); /* room for metadata */
        new_m->filename = NULL;
    
#if APR_USE_SHMEM_MMAP_ZERO
        status = apr_file_open(&file, "/dev/zero", APR_READ | APR_WRITE, 
                               APR_OS_DEFAULT, pool);
        if (status != APR_SUCCESS) {
            return status;
        }
        status = apr_os_file_get(&tmpfd, file);
        if (status != APR_SUCCESS) {
            return status;
        }

        new_m->base = mmap(NULL, new_m->realsize, PROT_READ|PROT_WRITE,
                           MAP_SHARED, tmpfd, 0);
        if (new_m->base == (void *)MAP_FAILED) {
            return errno;
        }

        status = apr_file_close(file);
        if (status != APR_SUCCESS) {
            return status;
        }

        /* store the real size in the metadata */
        *(apr_size_t*)(new_m->base) = new_m->realsize;
        /* metadata isn't usable */
        new_m->usable = (char *)new_m->base + APR_ALIGN_DEFAULT(sizeof(apr_size_t));

        apr_pool_cleanup_register(new_m->pool, new_m, shm_cleanup_owner,
                                  apr_pool_cleanup_null);
        *m = new_m;
        return APR_SUCCESS;

#elif APR_USE_SHMEM_MMAP_ANON
        new_m->base = mmap(NULL, new_m->realsize, PROT_READ|PROT_WRITE,
                           MAP_ANON|MAP_SHARED, -1, 0);
        if (new_m->base == (void *)MAP_FAILED) {
            return errno;
        }

        /* store the real size in the metadata */
        *(apr_size_t*)(new_m->base) = new_m->realsize;
        /* metadata isn't usable */
        new_m->usable = (char *)new_m->base + APR_ALIGN_DEFAULT(sizeof(apr_size_t));

        apr_pool_cleanup_register(new_m->pool, new_m, shm_cleanup_owner,
                                  apr_pool_cleanup_null);
        *m = new_m;
        return APR_SUCCESS;

#endif /* APR_USE_SHMEM_MMAP_ZERO */
#elif APR_USE_SHMEM_SHMGET_ANON
        new_m = apr_palloc(pool, sizeof(apr_shm_t));
        new_m->pool = pool;
        new_m->reqsize = reqsize;
        new_m->realsize = reqsize;
        new_m->filename = NULL;

        if ((new_m->shmid = shmget(IPC_PRIVATE, new_m->realsize,
                                   SHM_R | SHM_W | IPC_CREAT)) < 0) {
            return errno;
        }

        if ((new_m->base = shmat(new_m->shmid, NULL, 0)) == (void *)-1) {
            return errno;
        }
        new_m->usable = new_m->base;

        if (shmctl(new_m->shmid, IPC_STAT, &shmbuf) == -1) {
            return errno;
        }
        apr_uid_current(&uid, &gid, pool);
        shmbuf.shm_perm.uid = uid;
        shmbuf.shm_perm.gid = gid;
        if (shmctl(new_m->shmid, IPC_SET, &shmbuf) == -1) {
            return errno;
        }

        /* Remove the segment once use count hits zero.
         * We will not attach to this segment again, since it is
         * anonymous memory, so it is ok to mark it for deletion.
         */
        if (shmctl(new_m->shmid, IPC_RMID, NULL) == -1) {
            return errno;
        }

        apr_pool_cleanup_register(new_m->pool, new_m, shm_cleanup_owner,
                                  apr_pool_cleanup_null);
        *m = new_m;
        return APR_SUCCESS;
#else
        /* It is an error if they want anonymous memory but we don't have it. */
        return APR_ENOTIMPL; /* requested anonymous but we don't have it */
#endif
    }

    /* Name-based shared memory */
    else {
        new_m = apr_palloc(pool, sizeof(apr_shm_t));
        new_m->pool = pool;
        new_m->reqsize = reqsize;
        new_m->filename = apr_pstrdup(pool, filename);
#if APR_USE_SHMEM_MMAP_SHM
        const char *shm_name = make_shm_open_safe_name(filename, pool);
#endif
#if APR_USE_SHMEM_MMAP_TMP || APR_USE_SHMEM_MMAP_SHM
        new_m->realsize = reqsize + 
            APR_ALIGN_DEFAULT(sizeof(apr_size_t)); /* room for metadata */
        /* FIXME: Ignore error for now. *
         * status = apr_file_remove(file, pool);*/
        status = APR_SUCCESS;
    
#if APR_USE_SHMEM_MMAP_TMP
        /* FIXME: Is APR_OS_DEFAULT sufficient? */
        status = apr_file_open(&file, filename, 
                               APR_READ | APR_WRITE | APR_CREATE | APR_EXCL,
                               APR_OS_DEFAULT, pool);
        if (status != APR_SUCCESS) {
            return status;
        }

        status = apr_os_file_get(&tmpfd, file);
        if (status != APR_SUCCESS) {
            apr_file_close(file); /* ignore errors, we're failing */
            apr_file_remove(new_m->filename, new_m->pool);
            return status;
        }

        status = apr_file_trunc(file, new_m->realsize);
        if (status != APR_SUCCESS && status != APR_ESPIPE) {
            apr_file_close(file); /* ignore errors, we're failing */
            apr_file_remove(new_m->filename, new_m->pool);
            return status;
        }

        new_m->base = mmap(NULL, new_m->realsize, PROT_READ | PROT_WRITE,
                           MAP_SHARED, tmpfd, 0);
        /* FIXME: check for errors */

        status = apr_file_close(file);
        if (status != APR_SUCCESS) {
            return status;
        }
#endif /* APR_USE_SHMEM_MMAP_TMP */
#if APR_USE_SHMEM_MMAP_SHM
        /* FIXME: SysV uses 0600... should we? */
        tmpfd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0644);
        if (tmpfd == -1) {
            return errno;
        }

        status = apr_os_file_put(&file, &tmpfd,
                                 APR_READ | APR_WRITE | APR_CREATE | APR_EXCL,
                                 pool); 
        if (status != APR_SUCCESS) {
            return status;
        }

        status = apr_file_trunc(file, new_m->realsize);
        if (status != APR_SUCCESS && status != APR_ESPIPE) {
            shm_unlink(shm_name); /* we're failing, remove the object */
            return status;
        }
        new_m->base = mmap(NULL, new_m->realsize, PROT_READ | PROT_WRITE,
                           MAP_SHARED, tmpfd, 0);

        /* FIXME: check for errors */

        status = apr_file_close(file);
        if (status != APR_SUCCESS) {
            return status;
        }
#endif /* APR_USE_SHMEM_MMAP_SHM */

        /* store the real size in the metadata */
        *(apr_size_t*)(new_m->base) = new_m->realsize;
        /* metadata isn't usable */
        new_m->usable = (char *)new_m->base + APR_ALIGN_DEFAULT(sizeof(apr_size_t));

        apr_pool_cleanup_register(new_m->pool, new_m, shm_cleanup_owner,
                                  apr_pool_cleanup_null);
        *m = new_m;
        return APR_SUCCESS;

#elif APR_USE_SHMEM_SHMGET
        new_m->realsize = reqsize;

        /* FIXME: APR_OS_DEFAULT is too permissive, switch to 600 I think. */
        status = apr_file_open(&file, filename, 
                               APR_FOPEN_WRITE | APR_FOPEN_CREATE | APR_FOPEN_EXCL,
                               APR_OS_DEFAULT, pool);
        if (status != APR_SUCCESS) {
            return status;
        }

        /* ftok() (on solaris at least) requires that the file actually
         * exist before calling ftok(). */
        shmkey = our_ftok(filename);
        if (shmkey == (key_t)-1) {
            apr_file_close(file);
            return errno;
        }

        if ((new_m->shmid = shmget(shmkey, new_m->realsize,
                                   SHM_R | SHM_W | IPC_CREAT | IPC_EXCL)) < 0) {
            apr_file_close(file);
            return errno;
        }

        if ((new_m->base = shmat(new_m->shmid, NULL, 0)) == (void *)-1) {
            apr_file_close(file);
            return errno;
        }
        new_m->usable = new_m->base;

        if (shmctl(new_m->shmid, IPC_STAT, &shmbuf) == -1) {
            apr_file_close(file);
            return errno;
        }
        apr_uid_current(&uid, &gid, pool);
        shmbuf.shm_perm.uid = uid;
        shmbuf.shm_perm.gid = gid;
        if (shmctl(new_m->shmid, IPC_SET, &shmbuf) == -1) {
            apr_file_close(file);
            return errno;
        }

        nbytes = sizeof(reqsize);
        status = apr_file_write(file, (const void *)&reqsize,
                                &nbytes);
        if (status != APR_SUCCESS) {
            apr_file_close(file);
            return status;
        }
        status = apr_file_close(file);
        if (status != APR_SUCCESS) {
            return status;
        }

        apr_pool_cleanup_register(new_m->pool, new_m, shm_cleanup_owner,
                                  apr_pool_cleanup_null);
        *m = new_m; 
        return APR_SUCCESS;

#else
        return APR_ENOTIMPL;
#endif
    }
}

APR_DECLARE(apr_status_t) apr_shm_create_ex(apr_shm_t **m, 
                                            apr_size_t reqsize, 
                                            const char *filename, 
                                            apr_pool_t *p,
                                            apr_int32_t flags)
{
    return apr_shm_create(m, reqsize, filename, p);
}

APR_DECLARE(apr_status_t) apr_shm_remove(const char *filename,
                                         apr_pool_t *pool)
{
#if APR_USE_SHMEM_SHMGET
    apr_status_t status;
    apr_file_t *file;  
    key_t shmkey;
    int shmid;
#endif

#if APR_USE_SHMEM_MMAP_TMP
    return apr_file_remove(filename, pool);
#elif APR_USE_SHMEM_MMAP_SHM
    const char *shm_name = make_shm_open_safe_name(filename, pool);
    if (shm_unlink(shm_name) == -1) {
        return errno;
    }
    return APR_SUCCESS;
#elif APR_USE_SHMEM_SHMGET
    /* Presume that the file already exists; just open for writing */    
    status = apr_file_open(&file, filename, APR_FOPEN_WRITE,
                           APR_OS_DEFAULT, pool);
    if (status) {
        return status;
    }

    /* ftok() (on solaris at least) requires that the file actually
     * exist before calling ftok(). */
    shmkey = our_ftok(filename);
    if (shmkey == (key_t)-1) {
        goto shm_remove_failed;
    }

    apr_file_close(file);

    if ((shmid = shmget(shmkey, 0, SHM_R | SHM_W)) < 0) {
        goto shm_remove_failed;
    }

    /* Indicate that the segment is to be destroyed as soon
     * as all processes have detached. This also disallows any
     * new attachments to the segment. */
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        goto shm_remove_failed;
    }
    return apr_file_remove(filename, pool);

shm_remove_failed:
    status = errno;
    /* ensure the file has been removed anyway. */
    apr_file_remove(filename, pool);
    return status;
#else

    /* No support for anonymous shm */
    return APR_ENOTIMPL;
#endif
} 

APR_DECLARE(apr_status_t) apr_shm_destroy(apr_shm_t *m)
{
    return apr_pool_cleanup_run(m->pool, m, shm_cleanup_owner);
}

static apr_status_t shm_cleanup_attach(void *m_)
{
    apr_shm_t *m = (apr_shm_t *)m_;

    if (m->filename == NULL) {
        /* It doesn't make sense to detach from an anonymous memory segment. */
        return APR_EINVAL;
    }
    else {
#if APR_USE_SHMEM_MMAP_TMP || APR_USE_SHMEM_MMAP_SHM
        if (munmap(m->base, m->realsize) == -1) {
            return errno;
        }
        return APR_SUCCESS;
#elif APR_USE_SHMEM_SHMGET
        if (shmdt(m->base) == -1) {
            return errno;
        }
        return APR_SUCCESS;
#else
        return APR_ENOTIMPL;
#endif
    }
}

APR_DECLARE(apr_status_t) apr_shm_attach(apr_shm_t **m,
                                         const char *filename,
                                         apr_pool_t *pool)
{
    if (filename == NULL) {
        /* It doesn't make sense to attach to a segment if you don't know
         * the filename. */
        return APR_EINVAL;
    }
    else {
#if APR_USE_SHMEM_MMAP_TMP || APR_USE_SHMEM_MMAP_SHM
        apr_shm_t *new_m;
        apr_status_t status;
        int tmpfd;
        apr_file_t *file;   /* file where metadata is stored */
        apr_size_t nbytes;

        new_m = apr_palloc(pool, sizeof(apr_shm_t));
        new_m->pool = pool;
        new_m->filename = apr_pstrdup(pool, filename);
#if APR_USE_SHMEM_MMAP_SHM
        const char *shm_name = make_shm_open_safe_name(filename, pool);

        /* FIXME: SysV uses 0600... should we? */
        tmpfd = shm_open(shm_name, O_RDWR, 0644);
        if (tmpfd == -1) {
            return errno;
        }

        status = apr_os_file_put(&file, &tmpfd,
                                 APR_READ | APR_WRITE,
                                 pool); 
        if (status != APR_SUCCESS) {
            return status;
        }

#elif APR_USE_SHMEM_MMAP_TMP
        status = apr_file_open(&file, filename, 
                               APR_READ | APR_WRITE,
                               APR_OS_DEFAULT, pool);
        if (status != APR_SUCCESS) {
            return status;
        }
        status = apr_os_file_get(&tmpfd, file);
        if (status != APR_SUCCESS) {
            return status;
        }
#else
        return APR_ENOTIMPL;
#endif

        nbytes = sizeof(new_m->realsize);
        status = apr_file_read(file, (void *)&(new_m->realsize),
                               &nbytes);
        if (status != APR_SUCCESS) {
            return status;
        }

        status = apr_os_file_get(&tmpfd, file);
        if (status != APR_SUCCESS) {
            apr_file_close(file); /* ignore errors, we're failing */
            apr_file_remove(new_m->filename, new_m->pool);
            return status;
        }

        new_m->reqsize = new_m->realsize - sizeof(apr_size_t);

        new_m->base = mmap(NULL, new_m->realsize, PROT_READ | PROT_WRITE,
                           MAP_SHARED, tmpfd, 0);
        /* FIXME: check for errors */
        
        status = apr_file_close(file);
        if (status != APR_SUCCESS) {
            return status;
        }

        /* metadata isn't part of the usable segment */
        new_m->usable = (char *)new_m->base + APR_ALIGN_DEFAULT(sizeof(apr_size_t));

        apr_pool_cleanup_register(new_m->pool, new_m, shm_cleanup_attach,
                                  apr_pool_cleanup_null);
        *m = new_m;
        return APR_SUCCESS;

#elif APR_USE_SHMEM_SHMGET
        apr_shm_t *new_m;
        apr_status_t status;
        apr_file_t *file;   /* file where metadata is stored */
        apr_size_t nbytes;
        key_t shmkey;

        new_m = apr_palloc(pool, sizeof(apr_shm_t));

        status = apr_file_open(&file, filename, 
                               APR_FOPEN_READ, APR_OS_DEFAULT, pool);
        if (status != APR_SUCCESS) {
            return status;
        }

        nbytes = sizeof(new_m->reqsize);
        status = apr_file_read(file, (void *)&(new_m->reqsize),
                               &nbytes);
        if (status != APR_SUCCESS) {
            return status;
        }
        status = apr_file_close(file);
        if (status != APR_SUCCESS) {
            return status;
        }

        new_m->filename = apr_pstrdup(pool, filename);
        new_m->pool = pool;
        shmkey = our_ftok(filename);
        if (shmkey == (key_t)-1) {
            return errno;
        }
        if ((new_m->shmid = shmget(shmkey, 0, SHM_R | SHM_W)) == -1) {
            return errno;
        }
        if ((new_m->base = shmat(new_m->shmid, NULL, 0)) == (void *)-1) {
            return errno;
        }
        new_m->usable = new_m->base;
        new_m->realsize = new_m->reqsize;

        apr_pool_cleanup_register(new_m->pool, new_m, shm_cleanup_attach,
                                  apr_pool_cleanup_null);
        *m = new_m;
        return APR_SUCCESS;

#else
        return APR_ENOTIMPL;
#endif
    }
}

APR_DECLARE(apr_status_t) apr_shm_attach_ex(apr_shm_t **m,
                                            const char *filename,
                                            apr_pool_t *pool,
                                            apr_int32_t flags)
{
    return apr_shm_attach(m, filename, pool);
}

APR_DECLARE(apr_status_t) apr_shm_detach(apr_shm_t *m)
{
    apr_status_t rv = shm_cleanup_attach(m);
    apr_pool_cleanup_kill(m->pool, m, shm_cleanup_attach);
    return rv;
}

APR_DECLARE(void *) apr_shm_baseaddr_get(const apr_shm_t *m)
{
    return m->usable;
}

APR_DECLARE(apr_size_t) apr_shm_size_get(const apr_shm_t *m)
{
    return m->reqsize;
}

APR_POOL_IMPLEMENT_ACCESSOR(shm)

APR_DECLARE(apr_status_t) apr_os_shm_get(apr_os_shm_t *osshm,
                                         apr_shm_t *shm)
{
    return APR_ENOTIMPL;
}

APR_DECLARE(apr_status_t) apr_os_shm_put(apr_shm_t **m,
                                         apr_os_shm_t *osshm,
                                         apr_pool_t *pool)
{
    return APR_ENOTIMPL;
}    

