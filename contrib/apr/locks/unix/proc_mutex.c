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
#include "apr_strings.h"
#include "apr_arch_proc_mutex.h"
#include "apr_arch_file_io.h" /* for apr_mkstemp() */
#include "apr_hash.h"

APR_DECLARE(apr_status_t) apr_proc_mutex_destroy(apr_proc_mutex_t *mutex)
{
    return apr_pool_cleanup_run(mutex->pool, mutex, apr_proc_mutex_cleanup);
}

#if APR_HAS_POSIXSEM_SERIALIZE || APR_HAS_FCNTL_SERIALIZE || \
    APR_HAS_PROC_PTHREAD_SERIALIZE || APR_HAS_SYSVSEM_SERIALIZE
static apr_status_t proc_mutex_no_child_init(apr_proc_mutex_t **mutex,
                                             apr_pool_t *cont,
                                             const char *fname)
{
    return APR_SUCCESS;
}
#endif    

#if APR_HAS_POSIXSEM_SERIALIZE

#ifndef SEM_FAILED
#define SEM_FAILED (-1)
#endif

static apr_status_t proc_mutex_posix_cleanup(void *mutex_)
{
    apr_proc_mutex_t *mutex = mutex_;
    
    if (sem_close(mutex->psem_interproc) < 0) {
        return errno;
    }

    return APR_SUCCESS;
}    

static unsigned int rshash (char *p) {
    /* hash function from Robert Sedgwicks 'Algorithms in C' book */
   unsigned int b    = 378551;
   unsigned int a    = 63689;
   unsigned int retval = 0;

   for( ; *p; p++)
   {
      retval = retval * a + (*p);
      a *= b;
   }

   return retval;
}

static apr_status_t proc_mutex_posix_create(apr_proc_mutex_t *new_mutex,
                                            const char *fname)
{
    #define APR_POSIXSEM_NAME_MIN 13
    sem_t *psem;
    char semname[32];
    
    new_mutex->interproc = apr_palloc(new_mutex->pool,
                                      sizeof(*new_mutex->interproc));
    /*
     * This bogusness is to follow what appears to be the
     * lowest common denominator in Posix semaphore naming:
     *   - start with '/'
     *   - be at most 14 chars
     *   - be unique and not match anything on the filesystem
     *
     * Because of this, we use fname to generate a (unique) hash
     * and use that as the name of the semaphore. If no filename was
     * given, we create one based on the time. We tuck the name
     * away, since it might be useful for debugging. We use 2 hashing
     * functions to try to avoid collisions.
     *
     * To  make this as robust as possible, we initially try something
     * larger (and hopefully more unique) and gracefully fail down to the
     * LCD above.
     *
     * NOTE: Darwin (Mac OS X) seems to be the most restrictive
     * implementation. Versions previous to Darwin 6.2 had the 14
     * char limit, but later rev's allow up to 31 characters.
     *
     */
    if (fname) {
        apr_ssize_t flen = strlen(fname);
        char *p = apr_pstrndup(new_mutex->pool, fname, strlen(fname));
        unsigned int h1, h2;
        h1 = (apr_hashfunc_default((const char *)p, &flen) & 0xffffffff);
        h2 = (rshash(p) & 0xffffffff);
        apr_snprintf(semname, sizeof(semname), "/ApR.%xH%x", h1, h2);
    } else {
        apr_time_t now;
        unsigned long sec;
        unsigned long usec;
        now = apr_time_now();
        sec = apr_time_sec(now);
        usec = apr_time_usec(now);
        apr_snprintf(semname, sizeof(semname), "/ApR.%lxZ%lx", sec, usec);
    }
    do {
        psem = sem_open(semname, O_CREAT | O_EXCL, 0644, 1);
    } while (psem == (sem_t *)SEM_FAILED && errno == EINTR);
    if (psem == (sem_t *)SEM_FAILED) {
        if (errno == ENAMETOOLONG) {
            /* Oh well, good try */
            semname[APR_POSIXSEM_NAME_MIN] = '\0';
        } else {
            return errno;
        }
        do {
            psem = sem_open(semname, O_CREAT | O_EXCL, 0644, 1);
        } while (psem == (sem_t *)SEM_FAILED && errno == EINTR);
    }

    if (psem == (sem_t *)SEM_FAILED) {
        return errno;
    }
    /* Ahhh. The joys of Posix sems. Predelete it... */
    sem_unlink(semname);
    new_mutex->psem_interproc = psem;
    new_mutex->fname = apr_pstrdup(new_mutex->pool, semname);
    apr_pool_cleanup_register(new_mutex->pool, (void *)new_mutex,
                              apr_proc_mutex_cleanup, 
                              apr_pool_cleanup_null);
    return APR_SUCCESS;
}

static apr_status_t proc_mutex_posix_acquire(apr_proc_mutex_t *mutex)
{
    int rc;

    do {
        rc = sem_wait(mutex->psem_interproc);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0) {
        return errno;
    }
    mutex->curr_locked = 1;
    return APR_SUCCESS;
}

static apr_status_t proc_mutex_posix_tryacquire(apr_proc_mutex_t *mutex)
{
    int rc;

    do {
        rc = sem_trywait(mutex->psem_interproc);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0) {
        if (errno == EAGAIN) {
            return APR_EBUSY;
        }
        return errno;
    }
    mutex->curr_locked = 1;
    return APR_SUCCESS;
}

static apr_status_t proc_mutex_posix_release(apr_proc_mutex_t *mutex)
{
    mutex->curr_locked = 0;
    if (sem_post(mutex->psem_interproc) < 0) {
        /* any failure is probably fatal, so no big deal to leave
         * ->curr_locked at 0. */
        return errno;
    }
    return APR_SUCCESS;
}

static const apr_proc_mutex_unix_lock_methods_t mutex_posixsem_methods =
{
#if APR_PROCESS_LOCK_IS_GLOBAL || !APR_HAS_THREADS || defined(POSIXSEM_IS_GLOBAL)
    APR_PROCESS_LOCK_MECH_IS_GLOBAL,
#else
    0,
#endif
    proc_mutex_posix_create,
    proc_mutex_posix_acquire,
    proc_mutex_posix_tryacquire,
    proc_mutex_posix_release,
    proc_mutex_posix_cleanup,
    proc_mutex_no_child_init,
    "posixsem"
};

#endif /* Posix sem implementation */

#if APR_HAS_SYSVSEM_SERIALIZE

static struct sembuf proc_mutex_op_on;
static struct sembuf proc_mutex_op_try;
static struct sembuf proc_mutex_op_off;

static void proc_mutex_sysv_setup(void)
{
    proc_mutex_op_on.sem_num = 0;
    proc_mutex_op_on.sem_op = -1;
    proc_mutex_op_on.sem_flg = SEM_UNDO;
    proc_mutex_op_try.sem_num = 0;
    proc_mutex_op_try.sem_op = -1;
    proc_mutex_op_try.sem_flg = SEM_UNDO | IPC_NOWAIT;
    proc_mutex_op_off.sem_num = 0;
    proc_mutex_op_off.sem_op = 1;
    proc_mutex_op_off.sem_flg = SEM_UNDO;
}

static apr_status_t proc_mutex_sysv_cleanup(void *mutex_)
{
    apr_proc_mutex_t *mutex=mutex_;
    union semun ick;
    
    if (mutex->interproc->filedes != -1) {
        ick.val = 0;
        semctl(mutex->interproc->filedes, 0, IPC_RMID, ick);
    }
    return APR_SUCCESS;
}    

static apr_status_t proc_mutex_sysv_create(apr_proc_mutex_t *new_mutex,
                                           const char *fname)
{
    union semun ick;
    apr_status_t rv;
    
    new_mutex->interproc = apr_palloc(new_mutex->pool, sizeof(*new_mutex->interproc));
    new_mutex->interproc->filedes = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);

    if (new_mutex->interproc->filedes < 0) {
        rv = errno;
        proc_mutex_sysv_cleanup(new_mutex);
        return rv;
    }
    ick.val = 1;
    if (semctl(new_mutex->interproc->filedes, 0, SETVAL, ick) < 0) {
        rv = errno;
        proc_mutex_sysv_cleanup(new_mutex);
        return rv;
    }
    new_mutex->curr_locked = 0;
    apr_pool_cleanup_register(new_mutex->pool,
                              (void *)new_mutex, apr_proc_mutex_cleanup, 
                              apr_pool_cleanup_null);
    return APR_SUCCESS;
}

static apr_status_t proc_mutex_sysv_acquire(apr_proc_mutex_t *mutex)
{
    int rc;

    do {
        rc = semop(mutex->interproc->filedes, &proc_mutex_op_on, 1);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0) {
        return errno;
    }
    mutex->curr_locked = 1;
    return APR_SUCCESS;
}

static apr_status_t proc_mutex_sysv_tryacquire(apr_proc_mutex_t *mutex)
{
    int rc;

    do {
        rc = semop(mutex->interproc->filedes, &proc_mutex_op_try, 1);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0) {
        if (errno == EAGAIN) {
            return APR_EBUSY;
        }
        return errno;
    }
    mutex->curr_locked = 1;
    return APR_SUCCESS;
}

static apr_status_t proc_mutex_sysv_release(apr_proc_mutex_t *mutex)
{
    int rc;

    mutex->curr_locked = 0;
    do {
        rc = semop(mutex->interproc->filedes, &proc_mutex_op_off, 1);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0) {
        return errno;
    }
    return APR_SUCCESS;
}

static const apr_proc_mutex_unix_lock_methods_t mutex_sysv_methods =
{
#if APR_PROCESS_LOCK_IS_GLOBAL || !APR_HAS_THREADS || defined(SYSVSEM_IS_GLOBAL)
    APR_PROCESS_LOCK_MECH_IS_GLOBAL,
#else
    0,
#endif
    proc_mutex_sysv_create,
    proc_mutex_sysv_acquire,
    proc_mutex_sysv_tryacquire,
    proc_mutex_sysv_release,
    proc_mutex_sysv_cleanup,
    proc_mutex_no_child_init,
    "sysvsem"
};

#endif /* SysV sem implementation */

#if APR_HAS_PROC_PTHREAD_SERIALIZE

static apr_status_t proc_mutex_proc_pthread_cleanup(void *mutex_)
{
    apr_proc_mutex_t *mutex=mutex_;
    apr_status_t rv;

    if (mutex->curr_locked == 1) {
        if ((rv = pthread_mutex_unlock(mutex->pthread_interproc))) {
#ifdef HAVE_ZOS_PTHREADS
            rv = errno;
#endif
            return rv;
        }
    }
    /* curr_locked is set to -1 until the mutex has been created */
    if (mutex->curr_locked != -1) {
        if ((rv = pthread_mutex_destroy(mutex->pthread_interproc))) {
#ifdef HAVE_ZOS_PTHREADS
            rv = errno;
#endif
            return rv;
        }
    }
    if (munmap((caddr_t)mutex->pthread_interproc, sizeof(pthread_mutex_t))) {
        return errno;
    }
    return APR_SUCCESS;
}

static apr_status_t proc_mutex_proc_pthread_create(apr_proc_mutex_t *new_mutex,
                                                   const char *fname)
{
    apr_status_t rv;
    int fd;
    pthread_mutexattr_t mattr;

    fd = open("/dev/zero", O_RDWR);
    if (fd < 0) {
        return errno;
    }

    new_mutex->pthread_interproc = (pthread_mutex_t *)mmap(
                                       (caddr_t) 0, 
                                       sizeof(pthread_mutex_t), 
                                       PROT_READ | PROT_WRITE, MAP_SHARED,
                                       fd, 0); 
    if (new_mutex->pthread_interproc == (pthread_mutex_t *) (caddr_t) -1) {
        close(fd);
        return errno;
    }
    close(fd);

    new_mutex->curr_locked = -1; /* until the mutex has been created */

    if ((rv = pthread_mutexattr_init(&mattr))) {
#ifdef HAVE_ZOS_PTHREADS
        rv = errno;
#endif
        proc_mutex_proc_pthread_cleanup(new_mutex);
        return rv;
    }
    if ((rv = pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED))) {
#ifdef HAVE_ZOS_PTHREADS
        rv = errno;
#endif
        proc_mutex_proc_pthread_cleanup(new_mutex);
        pthread_mutexattr_destroy(&mattr);
        return rv;
    }

#ifdef HAVE_PTHREAD_MUTEX_ROBUST
    if ((rv = pthread_mutexattr_setrobust_np(&mattr, 
                                               PTHREAD_MUTEX_ROBUST_NP))) {
#ifdef HAVE_ZOS_PTHREADS
        rv = errno;
#endif
        proc_mutex_proc_pthread_cleanup(new_mutex);
        pthread_mutexattr_destroy(&mattr);
        return rv;
    }
    if ((rv = pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT))) {
#ifdef HAVE_ZOS_PTHREADS
        rv = errno;
#endif
        proc_mutex_proc_pthread_cleanup(new_mutex);
        pthread_mutexattr_destroy(&mattr);
        return rv;
    }
#endif /* HAVE_PTHREAD_MUTEX_ROBUST */

    if ((rv = pthread_mutex_init(new_mutex->pthread_interproc, &mattr))) {
#ifdef HAVE_ZOS_PTHREADS
        rv = errno;
#endif
        proc_mutex_proc_pthread_cleanup(new_mutex);
        pthread_mutexattr_destroy(&mattr);
        return rv;
    }

    new_mutex->curr_locked = 0; /* mutex created now */

    if ((rv = pthread_mutexattr_destroy(&mattr))) {
#ifdef HAVE_ZOS_PTHREADS
        rv = errno;
#endif
        proc_mutex_proc_pthread_cleanup(new_mutex);
        return rv;
    }

    apr_pool_cleanup_register(new_mutex->pool,
                              (void *)new_mutex,
                              apr_proc_mutex_cleanup, 
                              apr_pool_cleanup_null);
    return APR_SUCCESS;
}

static apr_status_t proc_mutex_proc_pthread_acquire(apr_proc_mutex_t *mutex)
{
    apr_status_t rv;

    if ((rv = pthread_mutex_lock(mutex->pthread_interproc))) {
#ifdef HAVE_ZOS_PTHREADS
        rv = errno;
#endif
#ifdef HAVE_PTHREAD_MUTEX_ROBUST
        /* Okay, our owner died.  Let's try to make it consistent again. */
        if (rv == EOWNERDEAD) {
            pthread_mutex_consistent_np(mutex->pthread_interproc);
        }
        else
            return rv;
#else
        return rv;
#endif
    }
    mutex->curr_locked = 1;
    return APR_SUCCESS;
}

static apr_status_t proc_mutex_proc_pthread_tryacquire(apr_proc_mutex_t *mutex)
{
    apr_status_t rv;
 
    if ((rv = pthread_mutex_trylock(mutex->pthread_interproc))) {
#ifdef HAVE_ZOS_PTHREADS 
        rv = errno;
#endif
        if (rv == EBUSY) {
            return APR_EBUSY;
        }
#ifdef HAVE_PTHREAD_MUTEX_ROBUST
        /* Okay, our owner died.  Let's try to make it consistent again. */
        if (rv == EOWNERDEAD) {
            pthread_mutex_consistent_np(mutex->pthread_interproc);
            rv = APR_SUCCESS;
        }
        else
            return rv;
#else
        return rv;
#endif
    }
    mutex->curr_locked = 1;
    return rv;
}

static apr_status_t proc_mutex_proc_pthread_release(apr_proc_mutex_t *mutex)
{
    apr_status_t rv;

    mutex->curr_locked = 0;
    if ((rv = pthread_mutex_unlock(mutex->pthread_interproc))) {
#ifdef HAVE_ZOS_PTHREADS
        rv = errno;
#endif
        return rv;
    }
    return APR_SUCCESS;
}

static const apr_proc_mutex_unix_lock_methods_t mutex_proc_pthread_methods =
{
    APR_PROCESS_LOCK_MECH_IS_GLOBAL,
    proc_mutex_proc_pthread_create,
    proc_mutex_proc_pthread_acquire,
    proc_mutex_proc_pthread_tryacquire,
    proc_mutex_proc_pthread_release,
    proc_mutex_proc_pthread_cleanup,
    proc_mutex_no_child_init,
    "pthread"
};

#endif

#if APR_HAS_FCNTL_SERIALIZE

static struct flock proc_mutex_lock_it;
static struct flock proc_mutex_unlock_it;

static apr_status_t proc_mutex_fcntl_release(apr_proc_mutex_t *);

static void proc_mutex_fcntl_setup(void)
{
    proc_mutex_lock_it.l_whence = SEEK_SET;   /* from current point */
    proc_mutex_lock_it.l_start = 0;           /* -"- */
    proc_mutex_lock_it.l_len = 0;             /* until end of file */
    proc_mutex_lock_it.l_type = F_WRLCK;      /* set exclusive/write lock */
    proc_mutex_lock_it.l_pid = 0;             /* pid not actually interesting */
    proc_mutex_unlock_it.l_whence = SEEK_SET; /* from current point */
    proc_mutex_unlock_it.l_start = 0;         /* -"- */
    proc_mutex_unlock_it.l_len = 0;           /* until end of file */
    proc_mutex_unlock_it.l_type = F_UNLCK;    /* set exclusive/write lock */
    proc_mutex_unlock_it.l_pid = 0;           /* pid not actually interesting */
}

static apr_status_t proc_mutex_fcntl_cleanup(void *mutex_)
{
    apr_status_t status;
    apr_proc_mutex_t *mutex=mutex_;

    if (mutex->curr_locked == 1) {
        status = proc_mutex_fcntl_release(mutex);
        if (status != APR_SUCCESS)
            return status;
    }
        
    return apr_file_close(mutex->interproc);
}    

static apr_status_t proc_mutex_fcntl_create(apr_proc_mutex_t *new_mutex,
                                            const char *fname)
{
    int rv;
 
    if (fname) {
        new_mutex->fname = apr_pstrdup(new_mutex->pool, fname);
        rv = apr_file_open(&new_mutex->interproc, new_mutex->fname,
                           APR_FOPEN_CREATE | APR_FOPEN_WRITE | APR_FOPEN_EXCL,
                           APR_UREAD | APR_UWRITE | APR_GREAD | APR_WREAD,
                           new_mutex->pool);
    }
    else {
        new_mutex->fname = apr_pstrdup(new_mutex->pool, "/tmp/aprXXXXXX");
        rv = apr_file_mktemp(&new_mutex->interproc, new_mutex->fname,
                             APR_FOPEN_CREATE | APR_FOPEN_WRITE | APR_FOPEN_EXCL,
                             new_mutex->pool);
    }
 
    if (rv != APR_SUCCESS) {
        return rv;
    }

    new_mutex->curr_locked = 0;
    unlink(new_mutex->fname);
    apr_pool_cleanup_register(new_mutex->pool,
                              (void*)new_mutex,
                              apr_proc_mutex_cleanup, 
                              apr_pool_cleanup_null);
    return APR_SUCCESS; 
}

static apr_status_t proc_mutex_fcntl_acquire(apr_proc_mutex_t *mutex)
{
    int rc;

    do {
        rc = fcntl(mutex->interproc->filedes, F_SETLKW, &proc_mutex_lock_it);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0) {
        return errno;
    }
    mutex->curr_locked=1;
    return APR_SUCCESS;
}

static apr_status_t proc_mutex_fcntl_tryacquire(apr_proc_mutex_t *mutex)
{
    int rc;

    do {
        rc = fcntl(mutex->interproc->filedes, F_SETLK, &proc_mutex_lock_it);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0) {
#if FCNTL_TRYACQUIRE_EACCES
        if (errno == EACCES) {
#else
        if (errno == EAGAIN) {
#endif
            return APR_EBUSY;
        }
        return errno;
    }
    mutex->curr_locked = 1;
    return APR_SUCCESS;
}

static apr_status_t proc_mutex_fcntl_release(apr_proc_mutex_t *mutex)
{
    int rc;

    mutex->curr_locked=0;
    do {
        rc = fcntl(mutex->interproc->filedes, F_SETLKW, &proc_mutex_unlock_it);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0) {
        return errno;
    }
    return APR_SUCCESS;
}

static const apr_proc_mutex_unix_lock_methods_t mutex_fcntl_methods =
{
#if APR_PROCESS_LOCK_IS_GLOBAL || !APR_HAS_THREADS || defined(FCNTL_IS_GLOBAL)
    APR_PROCESS_LOCK_MECH_IS_GLOBAL,
#else
    0,
#endif
    proc_mutex_fcntl_create,
    proc_mutex_fcntl_acquire,
    proc_mutex_fcntl_tryacquire,
    proc_mutex_fcntl_release,
    proc_mutex_fcntl_cleanup,
    proc_mutex_no_child_init,
    "fcntl"
};

#endif /* fcntl implementation */

#if APR_HAS_FLOCK_SERIALIZE

static apr_status_t proc_mutex_flock_release(apr_proc_mutex_t *);

static apr_status_t proc_mutex_flock_cleanup(void *mutex_)
{
    apr_status_t status;
    apr_proc_mutex_t *mutex=mutex_;

    if (mutex->curr_locked == 1) {
        status = proc_mutex_flock_release(mutex);
        if (status != APR_SUCCESS)
            return status;
    }
    if (mutex->interproc) { /* if it was opened properly */
        apr_file_close(mutex->interproc);
    }
    unlink(mutex->fname);
    return APR_SUCCESS;
}    

static apr_status_t proc_mutex_flock_create(apr_proc_mutex_t *new_mutex,
                                            const char *fname)
{
    int rv;
 
    if (fname) {
        new_mutex->fname = apr_pstrdup(new_mutex->pool, fname);
        rv = apr_file_open(&new_mutex->interproc, new_mutex->fname,
                           APR_FOPEN_CREATE | APR_FOPEN_WRITE | APR_FOPEN_EXCL,
                           APR_UREAD | APR_UWRITE,
                           new_mutex->pool);
    }
    else {
        new_mutex->fname = apr_pstrdup(new_mutex->pool, "/tmp/aprXXXXXX");
        rv = apr_file_mktemp(&new_mutex->interproc, new_mutex->fname,
                             APR_FOPEN_CREATE | APR_FOPEN_WRITE | APR_FOPEN_EXCL,
                             new_mutex->pool);
    }
 
    if (rv != APR_SUCCESS) {
        proc_mutex_flock_cleanup(new_mutex);
        return errno;
    }
    new_mutex->curr_locked = 0;
    apr_pool_cleanup_register(new_mutex->pool, (void *)new_mutex,
                              apr_proc_mutex_cleanup,
                              apr_pool_cleanup_null);
    return APR_SUCCESS;
}

static apr_status_t proc_mutex_flock_acquire(apr_proc_mutex_t *mutex)
{
    int rc;

    do {
        rc = flock(mutex->interproc->filedes, LOCK_EX);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0) {
        return errno;
    }
    mutex->curr_locked = 1;
    return APR_SUCCESS;
}

static apr_status_t proc_mutex_flock_tryacquire(apr_proc_mutex_t *mutex)
{
    int rc;

    do {
        rc = flock(mutex->interproc->filedes, LOCK_EX | LOCK_NB);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return APR_EBUSY;
        }
        return errno;
    }
    mutex->curr_locked = 1;
    return APR_SUCCESS;
}

static apr_status_t proc_mutex_flock_release(apr_proc_mutex_t *mutex)
{
    int rc;

    mutex->curr_locked = 0;
    do {
        rc = flock(mutex->interproc->filedes, LOCK_UN);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0) {
        return errno;
    }
    return APR_SUCCESS;
}

static apr_status_t proc_mutex_flock_child_init(apr_proc_mutex_t **mutex,
                                                apr_pool_t *pool, 
                                                const char *fname)
{
    apr_proc_mutex_t *new_mutex;
    int rv;

    new_mutex = (apr_proc_mutex_t *)apr_palloc(pool, sizeof(apr_proc_mutex_t));

    memcpy(new_mutex, *mutex, sizeof *new_mutex);
    new_mutex->pool = pool;
    if (!fname) {
        fname = (*mutex)->fname;
    }
    new_mutex->fname = apr_pstrdup(pool, fname);
    rv = apr_file_open(&new_mutex->interproc, new_mutex->fname,
                       APR_FOPEN_WRITE, 0, new_mutex->pool);
    if (rv != APR_SUCCESS) {
        return rv;
    }
    *mutex = new_mutex;
    return APR_SUCCESS;
}

static const apr_proc_mutex_unix_lock_methods_t mutex_flock_methods =
{
#if APR_PROCESS_LOCK_IS_GLOBAL || !APR_HAS_THREADS || defined(FLOCK_IS_GLOBAL)
    APR_PROCESS_LOCK_MECH_IS_GLOBAL,
#else
    0,
#endif
    proc_mutex_flock_create,
    proc_mutex_flock_acquire,
    proc_mutex_flock_tryacquire,
    proc_mutex_flock_release,
    proc_mutex_flock_cleanup,
    proc_mutex_flock_child_init,
    "flock"
};

#endif /* flock implementation */

void apr_proc_mutex_unix_setup_lock(void)
{
    /* setup only needed for sysvsem and fnctl */
#if APR_HAS_SYSVSEM_SERIALIZE
    proc_mutex_sysv_setup();
#endif
#if APR_HAS_FCNTL_SERIALIZE
    proc_mutex_fcntl_setup();
#endif
}

static apr_status_t proc_mutex_choose_method(apr_proc_mutex_t *new_mutex, apr_lockmech_e mech)
{
    switch (mech) {
    case APR_LOCK_FCNTL:
#if APR_HAS_FCNTL_SERIALIZE
        new_mutex->inter_meth = &mutex_fcntl_methods;
#else
        return APR_ENOTIMPL;
#endif
        break;
    case APR_LOCK_FLOCK:
#if APR_HAS_FLOCK_SERIALIZE
        new_mutex->inter_meth = &mutex_flock_methods;
#else
        return APR_ENOTIMPL;
#endif
        break;
    case APR_LOCK_SYSVSEM:
#if APR_HAS_SYSVSEM_SERIALIZE
        new_mutex->inter_meth = &mutex_sysv_methods;
#else
        return APR_ENOTIMPL;
#endif
        break;
    case APR_LOCK_POSIXSEM:
#if APR_HAS_POSIXSEM_SERIALIZE
        new_mutex->inter_meth = &mutex_posixsem_methods;
#else
        return APR_ENOTIMPL;
#endif
        break;
    case APR_LOCK_PROC_PTHREAD:
#if APR_HAS_PROC_PTHREAD_SERIALIZE
        new_mutex->inter_meth = &mutex_proc_pthread_methods;
#else
        return APR_ENOTIMPL;
#endif
        break;
    case APR_LOCK_DEFAULT:
#if APR_USE_FLOCK_SERIALIZE
        new_mutex->inter_meth = &mutex_flock_methods;
#elif APR_USE_SYSVSEM_SERIALIZE
        new_mutex->inter_meth = &mutex_sysv_methods;
#elif APR_USE_FCNTL_SERIALIZE
        new_mutex->inter_meth = &mutex_fcntl_methods;
#elif APR_USE_PROC_PTHREAD_SERIALIZE
        new_mutex->inter_meth = &mutex_proc_pthread_methods;
#elif APR_USE_POSIXSEM_SERIALIZE
        new_mutex->inter_meth = &mutex_posixsem_methods;
#else
        return APR_ENOTIMPL;
#endif
        break;
    default:
        return APR_ENOTIMPL;
    }
    return APR_SUCCESS;
}

APR_DECLARE(const char *) apr_proc_mutex_defname(void)
{
    apr_status_t rv;
    apr_proc_mutex_t mutex;

    if ((rv = proc_mutex_choose_method(&mutex, APR_LOCK_DEFAULT)) != APR_SUCCESS) {
        return "unknown";
    }
    mutex.meth = mutex.inter_meth;

    return apr_proc_mutex_name(&mutex);
}
   
static apr_status_t proc_mutex_create(apr_proc_mutex_t *new_mutex, apr_lockmech_e mech, const char *fname)
{
    apr_status_t rv;

    if ((rv = proc_mutex_choose_method(new_mutex, mech)) != APR_SUCCESS) {
        return rv;
    }

    new_mutex->meth = new_mutex->inter_meth;

    if ((rv = new_mutex->meth->create(new_mutex, fname)) != APR_SUCCESS) {
        return rv;
    }

    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_proc_mutex_create(apr_proc_mutex_t **mutex,
                                                const char *fname,
                                                apr_lockmech_e mech,
                                                apr_pool_t *pool)
{
    apr_proc_mutex_t *new_mutex;
    apr_status_t rv;

    new_mutex = apr_pcalloc(pool, sizeof(apr_proc_mutex_t));
    new_mutex->pool = pool;

    if ((rv = proc_mutex_create(new_mutex, mech, fname)) != APR_SUCCESS)
        return rv;

    *mutex = new_mutex;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_proc_mutex_child_init(apr_proc_mutex_t **mutex,
                                                    const char *fname,
                                                    apr_pool_t *pool)
{
    return (*mutex)->meth->child_init(mutex, pool, fname);
}

APR_DECLARE(apr_status_t) apr_proc_mutex_lock(apr_proc_mutex_t *mutex)
{
    return mutex->meth->acquire(mutex);
}

APR_DECLARE(apr_status_t) apr_proc_mutex_trylock(apr_proc_mutex_t *mutex)
{
    return mutex->meth->tryacquire(mutex);
}

APR_DECLARE(apr_status_t) apr_proc_mutex_unlock(apr_proc_mutex_t *mutex)
{
    return mutex->meth->release(mutex);
}

APR_DECLARE(apr_status_t) apr_proc_mutex_cleanup(void *mutex)
{
    return ((apr_proc_mutex_t *)mutex)->meth->cleanup(mutex);
}

APR_DECLARE(const char *) apr_proc_mutex_name(apr_proc_mutex_t *mutex)
{
    return mutex->meth->name;
}

APR_DECLARE(const char *) apr_proc_mutex_lockfile(apr_proc_mutex_t *mutex)
{
    /* POSIX sems use the fname field but don't use a file,
     * so be careful. */
#if APR_HAS_FLOCK_SERIALIZE
    if (mutex->meth == &mutex_flock_methods) {
        return mutex->fname;
    }
#endif
#if APR_HAS_FCNTL_SERIALIZE
    if (mutex->meth == &mutex_fcntl_methods) {
        return mutex->fname;
    }
#endif
    return NULL;
}

APR_POOL_IMPLEMENT_ACCESSOR(proc_mutex)

/* Implement OS-specific accessors defined in apr_portable.h */

APR_DECLARE(apr_status_t) apr_os_proc_mutex_get(apr_os_proc_mutex_t *ospmutex,
                                                apr_proc_mutex_t *pmutex)
{
#if APR_HAS_SYSVSEM_SERIALIZE || APR_HAS_FCNTL_SERIALIZE || APR_HAS_FLOCK_SERIALIZE || APR_HAS_POSIXSEM_SERIALIZE
    if (pmutex->interproc) {
        ospmutex->crossproc = pmutex->interproc->filedes;
    }
    else {
        ospmutex->crossproc = -1;
    }
#endif
#if APR_HAS_PROC_PTHREAD_SERIALIZE
    ospmutex->pthread_interproc = pmutex->pthread_interproc;
#endif
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_os_proc_mutex_put(apr_proc_mutex_t **pmutex,
                                                apr_os_proc_mutex_t *ospmutex,
                                                apr_pool_t *pool)
{
    if (pool == NULL) {
        return APR_ENOPOOL;
    }
    if ((*pmutex) == NULL) {
        (*pmutex) = (apr_proc_mutex_t *)apr_pcalloc(pool,
                                                    sizeof(apr_proc_mutex_t));
        (*pmutex)->pool = pool;
    }
#if APR_HAS_SYSVSEM_SERIALIZE || APR_HAS_FCNTL_SERIALIZE || APR_HAS_FLOCK_SERIALIZE || APR_HAS_POSIXSEM_SERIALIZE
    apr_os_file_put(&(*pmutex)->interproc, &ospmutex->crossproc, 0, pool);
#endif
#if APR_HAS_PROC_PTHREAD_SERIALIZE
    (*pmutex)->pthread_interproc = ospmutex->pthread_interproc;
#endif
    return APR_SUCCESS;
}

