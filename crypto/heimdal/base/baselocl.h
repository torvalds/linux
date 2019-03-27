/*
 * Copyright (c) 2010 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2010 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "heimqueue.h"
#include "heim_threads.h"
#include "heimbase.h"
#include "heimbasepriv.h"

#ifdef HAVE_DISPATCH_DISPATCH_H
#include <dispatch/dispatch.h>
#endif

#if defined(__GNUC__) && defined(HAVE___SYNC_ADD_AND_FETCH)

#define heim_base_atomic_inc(x) __sync_add_and_fetch((x), 1)
#define heim_base_atomic_dec(x) __sync_sub_and_fetch((x), 1)
#define heim_base_atomic_type	unsigned int
#define heim_base_atomic_max    UINT_MAX

#define heim_base_exchange_pointer(t,v) __sync_lock_test_and_set((t), (v))

#elif defined(_WIN32)

#define heim_base_atomic_inc(x) InterlockedIncrement(x)
#define heim_base_atomic_dec(x) InterlockedDecrement(x)
#define heim_base_atomic_type	LONG
#define heim_base_atomic_max    MAXLONG

#define heim_base_exchange_pointer(t,v) InterlockedExchangePointer((t),(v))

#else

#define HEIM_BASE_NEED_ATOMIC_MUTEX 1
extern HEIMDAL_MUTEX _heim_base_mutex;

#define heim_base_atomic_type	unsigned int

static inline heim_base_atomic_type
heim_base_atomic_inc(heim_base_atomic_type *x)
{
    heim_base_atomic_type t;
    HEIMDAL_MUTEX_lock(&_heim_base_mutex);
    t = ++(*x);
    HEIMDAL_MUTEX_unlock(&_heim_base_mutex);
    return t;
}

static inline heim_base_atomic_type
heim_base_atomic_dec(heim_base_atomic_type *x)
{
    heim_base_atomic_type t;
    HEIMDAL_MUTEX_lock(&_heim_base_mutex);
    t = --(*x);
    HEIMDAL_MUTEX_unlock(&_heim_base_mutex);
    return t;
}

#define heim_base_atomic_max    UINT_MAX

#endif

/* tagged strings/object/XXX */
#define heim_base_is_tagged(x) (((uintptr_t)(x)) & 0x3)

#define heim_base_is_tagged_object(x) ((((uintptr_t)(x)) & 0x3) == 1)
#define heim_base_make_tagged_object(x, tid) \
    ((heim_object_t)((((uintptr_t)(x)) << 5) | ((tid) << 2) | 0x1))
#define heim_base_tagged_object_tid(x) ((((uintptr_t)(x)) & 0x1f) >> 2)
#define heim_base_tagged_object_value(x) (((uintptr_t)(x)) >> 5)

/*
 *
 */

#undef HEIMDAL_NORETURN_ATTRIBUTE
#define HEIMDAL_NORETURN_ATTRIBUTE
#undef HEIMDAL_PRINTF_ATTRIBUTE
#define HEIMDAL_PRINTF_ATTRIBUTE(x)
