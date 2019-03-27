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

#ifndef APR_SIGNAL_H
#define APR_SIGNAL_H

/**
 * @file apr_signal.h
 * @brief APR Signal Handling
 */

#include "apr.h"
#include "apr_pools.h"

#if APR_HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup apr_signal Signal Handling
 * @ingroup APR
 * @{
 */

#if APR_HAVE_SIGACTION || defined(DOXYGEN)

#if defined(DARWIN) && !defined(__cplusplus) && !defined(_ANSI_SOURCE)
/* work around Darwin header file bugs
 *   http://www.opensource.apple.com/bugs/X/BSD%20Kernel/2657228.html
 */
#undef SIG_DFL
#undef SIG_IGN
#undef SIG_ERR
#define SIG_DFL (void (*)(int))0
#define SIG_IGN (void (*)(int))1
#define SIG_ERR (void (*)(int))-1
#endif

/** Function prototype for signal handlers */
typedef void apr_sigfunc_t(int);

/**
 * Set the signal handler function for a given signal
 * @param signo The signal (eg... SIGWINCH)
 * @param func the function to get called
 */
APR_DECLARE(apr_sigfunc_t *) apr_signal(int signo, apr_sigfunc_t * func);

#if defined(SIG_IGN) && !defined(SIG_ERR)
#define SIG_ERR ((apr_sigfunc_t *) -1)
#endif

#else /* !APR_HAVE_SIGACTION */
#define apr_signal(a, b) signal(a, b)
#endif


/**
 * Get the description for a specific signal number
 * @param signum The signal number
 * @return The description of the signal
 */
APR_DECLARE(const char *) apr_signal_description_get(int signum);

/**
 * APR-private function for initializing the signal package
 * @internal
 * @param pglobal The internal, global pool
 */
void apr_signal_init(apr_pool_t *pglobal);

/**
 * Block the delivery of a particular signal
 * @param signum The signal number
 * @return status
 */
APR_DECLARE(apr_status_t) apr_signal_block(int signum);

/**
 * Enable the delivery of a particular signal
 * @param signum The signal number
 * @return status
 */
APR_DECLARE(apr_status_t) apr_signal_unblock(int signum);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* APR_SIGNAL_H */
