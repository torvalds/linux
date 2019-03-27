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

#include "apr_arch_file_io.h"

#if APR_HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif

APR_DECLARE(apr_status_t) apr_file_lock(apr_file_t *thefile, int type)
{
    int rc;

#if defined(HAVE_FCNTL_H)
    {
        struct flock l = { 0 };
        int fc;

        l.l_whence = SEEK_SET;  /* lock from current point */
        l.l_start = 0;          /* begin lock at this offset */
        l.l_len = 0;            /* lock to end of file */
        if ((type & APR_FLOCK_TYPEMASK) == APR_FLOCK_SHARED)
            l.l_type = F_RDLCK;
        else
            l.l_type = F_WRLCK;

        fc = (type & APR_FLOCK_NONBLOCK) ? F_SETLK : F_SETLKW;

        /* keep trying if fcntl() gets interrupted (by a signal) */
        while ((rc = fcntl(thefile->filedes, fc, &l)) < 0 && errno == EINTR)
            continue;

        if (rc == -1) {
            /* on some Unix boxes (e.g., Tru64), we get EACCES instead
             * of EAGAIN; we don't want APR_STATUS_IS_EAGAIN() matching EACCES
             * since that breaks other things, so fix up the retcode here
             */
            if (errno == EACCES) {
                return EAGAIN;
            }
            return errno;
        }
    }
#elif defined(HAVE_SYS_FILE_H)
    {
        int ltype;

        if ((type & APR_FLOCK_TYPEMASK) == APR_FLOCK_SHARED)
            ltype = LOCK_SH;
        else
            ltype = LOCK_EX;
        if ((type & APR_FLOCK_NONBLOCK) != 0)
            ltype |= LOCK_NB;

        /* keep trying if flock() gets interrupted (by a signal) */
        while ((rc = flock(thefile->filedes, ltype)) < 0 && errno == EINTR)
            continue;

        if (rc == -1)
            return errno;
    }
#else
#error No file locking mechanism is available.
#endif

    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_file_unlock(apr_file_t *thefile)
{
    int rc;

#if defined(HAVE_FCNTL_H)
    {
        struct flock l = { 0 };

        l.l_whence = SEEK_SET;  /* lock from current point */
        l.l_start = 0;          /* begin lock at this offset */
        l.l_len = 0;            /* lock to end of file */
        l.l_type = F_UNLCK;

        /* keep trying if fcntl() gets interrupted (by a signal) */
        while ((rc = fcntl(thefile->filedes, F_SETLKW, &l)) < 0
               && errno == EINTR)
            continue;

        if (rc == -1)
            return errno;
    }
#elif defined(HAVE_SYS_FILE_H)
    {
        /* keep trying if flock() gets interrupted (by a signal) */
        while ((rc = flock(thefile->filedes, LOCK_UN)) < 0 && errno == EINTR)
            continue;

        if (rc == -1)
            return errno;
    }
#else
#error No file locking mechanism is available.
#endif

    return APR_SUCCESS;
}
