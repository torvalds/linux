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
#include "apr_strings.h"
#include "apr_portable.h"

#include "apr_arch_inherit.h"

/* Figure out how to get pipe block/nonblock on BeOS...
 * Basically, BONE7 changed things again so that ioctl didn't work,
 * but now fcntl does, hence we need to do this extra checking.
 * The joys of beta programs. :-)
 */
#if defined(BEOS)
#if !defined(BONE7)
# define BEOS_BLOCKING 1
#else
# define BEOS_BLOCKING 0
#endif
#endif

static apr_status_t pipeblock(apr_file_t *thepipe)
{
#if !defined(BEOS) || !BEOS_BLOCKING
      int fd_flags;

      fd_flags = fcntl(thepipe->filedes, F_GETFL, 0);
#  if defined(O_NONBLOCK)
      fd_flags &= ~O_NONBLOCK;
#  elif defined(O_NDELAY)
      fd_flags &= ~O_NDELAY;
#  elif defined(O_FNDELAY)
      fd_flags &= ~O_FNDELAY;
#  else 
      /* XXXX: this breaks things, but an alternative isn't obvious...*/
      return APR_ENOTIMPL;
#  endif
      if (fcntl(thepipe->filedes, F_SETFL, fd_flags) == -1) {
          return errno;
      }
#else /* BEOS_BLOCKING */

#  if BEOS_BONE /* This only works on BONE 0-6 */
      int on = 0;
      if (ioctl(thepipe->filedes, FIONBIO, &on, sizeof(on)) < 0) {
          return errno;
      }
#  else /* "classic" BeOS doesn't support this at all */
      return APR_ENOTIMPL;
#  endif 
 
#endif /* !BEOS_BLOCKING */

    thepipe->blocking = BLK_ON;
    return APR_SUCCESS;
}

static apr_status_t pipenonblock(apr_file_t *thepipe)
{
#if !defined(BEOS) || !BEOS_BLOCKING
      int fd_flags = fcntl(thepipe->filedes, F_GETFL, 0);

#  if defined(O_NONBLOCK)
      fd_flags |= O_NONBLOCK;
#  elif defined(O_NDELAY)
      fd_flags |= O_NDELAY;
#  elif defined(O_FNDELAY)
      fd_flags |= O_FNDELAY;
#  else
      /* XXXX: this breaks things, but an alternative isn't obvious...*/
      return APR_ENOTIMPL;
#  endif
      if (fcntl(thepipe->filedes, F_SETFL, fd_flags) == -1) {
          return errno;
      }
    
#else /* BEOS_BLOCKING */

#  if BEOS_BONE /* This only works on BONE 0-6 */
      int on = 1;
      if (ioctl(thepipe->filedes, FIONBIO, &on, sizeof(on)) < 0) {
          return errno;
      }
#  else /* "classic" BeOS doesn't support this at all */
      return APR_ENOTIMPL;
#  endif

#endif /* !BEOS_BLOCKING */

    thepipe->blocking = BLK_OFF;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_file_pipe_timeout_set(apr_file_t *thepipe, apr_interval_time_t timeout)
{
    if (thepipe->is_pipe == 1) {
        thepipe->timeout = timeout;
        if (timeout >= 0) {
            if (thepipe->blocking != BLK_OFF) { /* blocking or unknown state */
                return pipenonblock(thepipe);
            }
        }
        else {
            if (thepipe->blocking != BLK_ON) { /* non-blocking or unknown state */
                return pipeblock(thepipe);
            }
        }
        return APR_SUCCESS;
    }
    return APR_EINVAL;
}

APR_DECLARE(apr_status_t) apr_file_pipe_timeout_get(apr_file_t *thepipe, apr_interval_time_t *timeout)
{
    if (thepipe->is_pipe == 1) {
        *timeout = thepipe->timeout;
        return APR_SUCCESS;
    }
    return APR_EINVAL;
}

APR_DECLARE(apr_status_t) apr_os_pipe_put_ex(apr_file_t **file,
                                             apr_os_file_t *thefile,
                                             int register_cleanup,
                                             apr_pool_t *pool)
{
    int *dafile = thefile;
    
    (*file) = apr_pcalloc(pool, sizeof(apr_file_t));
    (*file)->pool = pool;
    (*file)->eof_hit = 0;
    (*file)->is_pipe = 1;
    (*file)->blocking = BLK_UNKNOWN; /* app needs to make a timeout call */
    (*file)->timeout = -1;
    (*file)->ungetchar = -1; /* no char avail */
    (*file)->filedes = *dafile;
    if (!register_cleanup) {
        (*file)->flags = APR_FOPEN_NOCLEANUP;
    }
    (*file)->buffered = 0;
#if APR_HAS_THREADS
    (*file)->thlock = NULL;
#endif
    if (register_cleanup) {
        apr_pool_cleanup_register((*file)->pool, (void *)(*file),
                                  apr_unix_file_cleanup,
                                  apr_pool_cleanup_null);
    }
#ifndef WAITIO_USES_POLL
    /* Start out with no pollset.  apr_wait_for_io_or_timeout() will
     * initialize the pollset if needed.
     */
    (*file)->pollset = NULL;
#endif
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_os_pipe_put(apr_file_t **file,
                                          apr_os_file_t *thefile,
                                          apr_pool_t *pool)
{
    return apr_os_pipe_put_ex(file, thefile, 0, pool);
}

APR_DECLARE(apr_status_t) apr_file_pipe_create(apr_file_t **in, apr_file_t **out, apr_pool_t *pool)
{
    int filedes[2];

    if (pipe(filedes) == -1) {
        return errno;
    }
    
    (*in) = (apr_file_t *)apr_pcalloc(pool, sizeof(apr_file_t));
    (*in)->pool = pool;
    (*in)->filedes = filedes[0];
    (*in)->is_pipe = 1;
    (*in)->fname = NULL;
    (*in)->buffered = 0;
    (*in)->blocking = BLK_ON;
    (*in)->timeout = -1;
    (*in)->ungetchar = -1;
    (*in)->flags = APR_INHERIT;
#if APR_HAS_THREADS
    (*in)->thlock = NULL;
#endif
#ifndef WAITIO_USES_POLL
    (*in)->pollset = NULL;
#endif
    (*out) = (apr_file_t *)apr_pcalloc(pool, sizeof(apr_file_t));
    (*out)->pool = pool;
    (*out)->filedes = filedes[1];
    (*out)->is_pipe = 1;
    (*out)->fname = NULL;
    (*out)->buffered = 0;
    (*out)->blocking = BLK_ON;
    (*out)->flags = APR_INHERIT;
    (*out)->timeout = -1;
#if APR_HAS_THREADS
    (*out)->thlock = NULL;
#endif
#ifndef WAITIO_USES_POLL
    (*out)->pollset = NULL;
#endif
    apr_pool_cleanup_register((*in)->pool, (void *)(*in), apr_unix_file_cleanup,
                         apr_pool_cleanup_null);
    apr_pool_cleanup_register((*out)->pool, (void *)(*out), apr_unix_file_cleanup,
                         apr_pool_cleanup_null);
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_file_pipe_create_ex(apr_file_t **in, 
                                                  apr_file_t **out, 
                                                  apr_int32_t blocking,
                                                  apr_pool_t *pool)
{
    apr_status_t status;

    if ((status = apr_file_pipe_create(in, out, pool)) != APR_SUCCESS)
        return status;

    switch (blocking) {
        case APR_FULL_BLOCK:
            break;
        case APR_READ_BLOCK:
            apr_file_pipe_timeout_set(*out, 0);
            break;
        case APR_WRITE_BLOCK:
            apr_file_pipe_timeout_set(*in, 0);
            break;
        default:
            apr_file_pipe_timeout_set(*out, 0);
            apr_file_pipe_timeout_set(*in, 0);
    }

    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_file_namedpipe_create(const char *filename, 
                                                    apr_fileperms_t perm, apr_pool_t *pool)
{
    mode_t mode = apr_unix_perms2mode(perm);

    if (mkfifo(filename, mode) == -1) {
        return errno;
    }
    return APR_SUCCESS;
} 

    

