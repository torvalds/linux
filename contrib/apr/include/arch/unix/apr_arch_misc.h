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

#ifndef MISC_H
#define MISC_H

#include "apr.h"
#include "apr_portable.h"
#include "apr_private.h"
#include "apr_general.h"
#include "apr_pools.h"
#include "apr_getopt.h"
#include "apr_thread_proc.h"
#include "apr_file_io.h"
#include "apr_errno.h"
#include "apr_getopt.h"

#if APR_HAVE_STDIO_H
#include <stdio.h>
#endif
#if APR_HAVE_SIGNAL_H
#include <signal.h>
#endif
#if APR_HAVE_PTHREAD_H
#include <pthread.h>
#endif

#if APR_HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if APR_HAVE_STRING_H
#include <string.h>
#endif

#ifdef BEOS
#include <kernel/OS.h>
#endif

struct apr_other_child_rec_t {
    apr_pool_t *p;
    struct apr_other_child_rec_t *next;
    apr_proc_t *proc;
    void (*maintenance) (int, void *, int);
    void *data;
    apr_os_file_t write_fd;
};

#if defined(WIN32) || defined(NETWARE)
#define WSAHighByte 2
#define WSALowByte 0
#endif

#endif  /* ! MISC_H */

