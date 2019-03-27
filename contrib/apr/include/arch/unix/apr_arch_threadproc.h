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
#include "apr_private.h"
#include "apr_thread_proc.h"
#include "apr_file_io.h"
#include "apr_arch_file_io.h"

/* System headers required for thread/process library */
#if APR_HAVE_PTHREAD_H
#include <pthread.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#if APR_HAVE_SIGNAL_H
#include <signal.h>
#endif
#if APR_HAVE_STRING_H
#include <string.h>
#endif
#if APR_HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#if APR_HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_SCHED_H
#include <sched.h>
#endif
/* End System Headers */


#ifndef THREAD_PROC_H
#define THREAD_PROC_H

#define SHELL_PATH "/bin/sh"

#if APR_HAS_THREADS

struct apr_thread_t {
    apr_pool_t *pool;
    pthread_t *td;
    void *data;
    apr_thread_start_t func;
    apr_status_t exitval;
};

struct apr_threadattr_t {
    apr_pool_t *pool;
    pthread_attr_t attr;
};

struct apr_threadkey_t {
    apr_pool_t *pool;
    pthread_key_t key;
};

struct apr_thread_once_t {
    pthread_once_t once;
};

#endif

struct apr_procattr_t {
    apr_pool_t *pool;
    apr_file_t *parent_in;
    apr_file_t *child_in;
    apr_file_t *parent_out;
    apr_file_t *child_out;
    apr_file_t *parent_err;
    apr_file_t *child_err;
    char *currdir;
    apr_int32_t cmdtype;
    apr_int32_t detached;
#ifdef RLIMIT_CPU
    struct rlimit *limit_cpu;
#endif
#if defined (RLIMIT_DATA) || defined (RLIMIT_VMEM) || defined(RLIMIT_AS)
    struct rlimit *limit_mem;
#endif
#ifdef RLIMIT_NPROC
    struct rlimit *limit_nproc;
#endif
#ifdef RLIMIT_NOFILE
    struct rlimit *limit_nofile;
#endif
    apr_child_errfn_t *errfn;
    apr_int32_t errchk;
    apr_uid_t   uid;
    apr_gid_t   gid;
};

#endif  /* ! THREAD_PROC_H */

