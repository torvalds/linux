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

#ifndef ATOMIC_H
#define ATOMIC_H

#include "apr.h"
#include "apr_private.h"
#include "apr_atomic.h"
#include "apr_thread_mutex.h"

#if defined(USE_ATOMICS_GENERIC)
/* noop */
#elif defined(__GNUC__) && defined(__STRICT_ANSI__)
/* force use of generic atomics if building e.g. with -std=c89, which
 * doesn't allow inline asm */
#   define USE_ATOMICS_GENERIC
#elif HAVE_ATOMIC_BUILTINS
#   define USE_ATOMICS_BUILTINS
#elif defined(SOLARIS2) && SOLARIS2 >= 10
#   define USE_ATOMICS_SOLARIS
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
#   define USE_ATOMICS_IA32
#elif defined(__GNUC__) && (defined(__PPC__) || defined(__ppc__))
#   define USE_ATOMICS_PPC
#elif defined(__GNUC__) && (defined(__s390__) || defined(__s390x__))
#   define USE_ATOMICS_S390
#else
#   define USE_ATOMICS_GENERIC
#endif

#endif /* ATOMIC_H */
