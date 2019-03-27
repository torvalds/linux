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

#ifndef FILE_IO_H
#define FILE_IO_H

#include "apr.h"
#include "apr_private.h"
#include "apr_general.h"
#include "apr_tables.h"
#include "apr_file_io.h"
#include "apr_file_info.h"
#include "apr_errno.h"
#include "apr_lib.h"
#include "apr_thread_mutex.h"
#ifndef WAITIO_USES_POLL
#include "apr_poll.h"
#endif

/* System headers the file I/O library needs */
#if APR_HAVE_FCNTL_H
#include <fcntl.h>
#endif
#if APR_HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if APR_HAVE_ERRNO_H
#include <errno.h>
#endif
#if APR_HAVE_STRING_H
#include <string.h>
#endif
#if APR_HAVE_STRINGS_H
#include <strings.h>
#endif
#if APR_HAVE_DIRENT_H
#include <dirent.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if APR_HAVE_UNISTD_H
#include <unistd.h>
#endif
#if APR_HAVE_STDIO_H
#include <stdio.h>
#endif
#if APR_HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if APR_HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#if APR_HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef BEOS
#include <kernel/OS.h>
#endif
/* Hunting down DEV_BSIZE if not from dirent.h, sys/stat.h etc */
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#if BEOS_BONE
# ifndef BONE7
  /* prior to BONE/7 fd_set & select were defined in sys/socket.h */
#  include <sys/socket.h>
# else
  /* Be moved the fd_set stuff and also the FIONBIO definition... */
#  include <sys/ioctl.h>
# endif
#endif
/* End System headers */

#define APR_FILE_DEFAULT_BUFSIZE 4096
/* For backwards-compat */
#define APR_FILE_BUFSIZE  APR_FILE_DEFAULT_BUFSIZE

struct apr_file_t {
    apr_pool_t *pool;
    int filedes;
    char *fname;
    apr_int32_t flags;
    int eof_hit;
    int is_pipe;
    apr_interval_time_t timeout;
    int buffered;
    enum {BLK_UNKNOWN, BLK_OFF, BLK_ON } blocking;
    int ungetchar;    /* Last char provided by an unget op. (-1 = no char)*/
#ifndef WAITIO_USES_POLL
    /* if there is a timeout set, then this pollset is used */
    apr_pollset_t *pollset;
#endif
    /* Stuff for buffered mode */
    char *buffer;
    apr_size_t bufpos;        /* Read/Write position in buffer */
    apr_size_t bufsize;       /* The size of the buffer */
    unsigned long dataRead;   /* amount of valid data read into buffer */
    int direction;            /* buffer being used for 0 = read, 1 = write */
    apr_off_t filePtr;        /* position in file of handle */
#if APR_HAS_THREADS
    struct apr_thread_mutex_t *thlock;
#endif
};

#if APR_HAS_THREADS
#define file_lock(f)   do { \
                           if ((f)->thlock) \
                               apr_thread_mutex_lock((f)->thlock); \
                       } while (0)
#define file_unlock(f) do { \
                           if ((f)->thlock) \
                               apr_thread_mutex_unlock((f)->thlock); \
                       } while (0)
#else
#define file_lock(f)   do {} while (0)
#define file_unlock(f) do {} while (0)
#endif

#if APR_HAS_LARGE_FILES && defined(_LARGEFILE64_SOURCE)
#define stat(f,b) stat64(f,b)
#define lstat(f,b) lstat64(f,b)
#define fstat(f,b) fstat64(f,b)
#define lseek(f,o,w) lseek64(f,o,w)
#define ftruncate(f,l) ftruncate64(f,l)
typedef struct stat64 struct_stat;
#else
typedef struct stat struct_stat;
#endif

/* readdir64_r is only used in specific cases: */
#if APR_HAS_THREADS && defined(_POSIX_THREAD_SAFE_FUNCTIONS) \
    && !defined(READDIR_IS_THREAD_SAFE) && defined(HAVE_READDIR64_R)
#define APR_USE_READDIR64_R
#endif

struct apr_dir_t {
    apr_pool_t *pool;
    char *dirname;
    DIR *dirstruct;
#ifdef APR_USE_READDIR64_R
    struct dirent64 *entry;
#else
    struct dirent *entry;
#endif
};

apr_status_t apr_unix_file_cleanup(void *);
apr_status_t apr_unix_child_file_cleanup(void *);

mode_t apr_unix_perms2mode(apr_fileperms_t perms);
apr_fileperms_t apr_unix_mode2perms(mode_t mode);

apr_status_t apr_file_flush_locked(apr_file_t *thefile);
apr_status_t apr_file_info_get_locked(apr_finfo_t *finfo, apr_int32_t wanted,
                                      apr_file_t *thefile);


#endif  /* ! FILE_IO_H */

