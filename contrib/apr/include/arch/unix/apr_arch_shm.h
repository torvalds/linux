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

#ifndef SHM_H
#define SHM_H

#include "apr.h"
#include "apr_private.h"
#include "apr_general.h"
#include "apr_lib.h"
#include "apr_shm.h"
#include "apr_pools.h"
#include "apr_file_io.h"
#include "apr_network_io.h"
#include "apr_portable.h"

#if APR_HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#ifdef HAVE_SYS_IPC_H
#include <sys/ipc.h>
#endif
#ifdef HAVE_SYS_MUTEX_H
#include <sys/mutex.h>
#endif
#ifdef HAVE_SYS_SHM_H
#include <sys/shm.h>
#endif
#if !defined(SHM_R)
#define SHM_R 0400
#endif
#if !defined(SHM_W)
#define SHM_W 0200
#endif
#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif

/* Not all systems seem to have MAP_FAILED defined, but it should always
 * just be (void *)-1. */
#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

struct apr_shm_t {
    apr_pool_t *pool;
    void *base;          /* base real address */
    void *usable;        /* base usable address */
    apr_size_t reqsize;  /* requested segment size */
    apr_size_t realsize; /* actual segment size */
    const char *filename;      /* NULL if anonymous */
#if APR_USE_SHMEM_SHMGET || APR_USE_SHMEM_SHMGET_ANON
    int shmid;          /* shmem ID returned from shmget() */
#endif
};

#endif /* SHM_H */
