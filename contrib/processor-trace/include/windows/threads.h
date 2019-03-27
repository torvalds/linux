/*
 * Copyright (c) 2014-2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * It looks like there is still no support for C11's threads.h.
 *
 * We implement the few features we actually need hoping that this file will
 * soon go away.
 */

#ifndef THREADS_H
#define THREADS_H

#include "windows.h"


enum {
	thrd_success	= 1,
	thrd_error
};


struct pt_thread {
	HANDLE handle;
};
typedef struct pt_thread thrd_t;

typedef int (*thrd_start_t)(void *);


struct thrd_args {
	thrd_start_t fun;
	void *arg;
};

static DWORD WINAPI thrd_routine(void *arg)
{
	struct thrd_args *args;
	int result;

	args = (struct thrd_args *) arg;
	if (!args)
		return (DWORD) -1;

	result = -1;
	if (args->fun)
		result = args->fun(args->arg);

	free(args);

	return (DWORD) result;
}

static inline int thrd_create(thrd_t *thrd, thrd_start_t fun, void *arg)
{
	struct thrd_args *args;
	HANDLE handle;

	if (!thrd || !fun)
		return thrd_error;

	args = malloc(sizeof(*args));
	if (!args)
		return thrd_error;

	args->fun = fun;
	args->arg = arg;

	handle = CreateThread(NULL, 0, thrd_routine, args, 0, NULL);
	if (!handle) {
		free(args);
		return thrd_error;
	}

	thrd->handle = handle;
	return thrd_success;
}

static inline int thrd_join(thrd_t *thrd, int *res)
{
	DWORD status;
	BOOL success;

	if (!thrd)
		return thrd_error;

	status = WaitForSingleObject(thrd->handle, INFINITE);
	if (status)
		return thrd_error;

	if (res) {
		DWORD result;

		success = GetExitCodeThread(thrd->handle, &result);
		if (!success) {
			(void) CloseHandle(thrd->handle);
			return thrd_error;
		}

		*res = (int) result;
	}

	success = CloseHandle(thrd->handle);
	if (!success)
		return thrd_error;

	return thrd_success;
}

struct pt_mutex {
	CRITICAL_SECTION cs;
};
typedef struct pt_mutex mtx_t;

enum {
	mtx_plain
};

static inline int mtx_init(mtx_t *mtx, int type)
{
	if (!mtx || type != mtx_plain)
		return thrd_error;

	InitializeCriticalSection(&mtx->cs);

	return thrd_success;
}

static inline void mtx_destroy(mtx_t *mtx)
{
	if (mtx)
		DeleteCriticalSection(&mtx->cs);
}

static inline int mtx_lock(mtx_t *mtx)
{
	if (!mtx)
		return thrd_error;

	EnterCriticalSection(&mtx->cs);

	return thrd_success;
}

static inline int mtx_unlock(mtx_t *mtx)
{
	if (!mtx)
		return thrd_error;

	LeaveCriticalSection(&mtx->cs);

	return thrd_success;
}


struct pt_cond {
	CONDITION_VARIABLE cond;
};
typedef struct pt_cond cnd_t;

static inline int cnd_init(cnd_t *cnd)
{
	if (!cnd)
		return thrd_error;

	InitializeConditionVariable(&cnd->cond);

	return thrd_success;
}

static inline int cnd_destroy(cnd_t *cnd)
{
	if (!cnd)
		return thrd_error;

	/* Nothing to do. */

	return thrd_success;
}

static inline int cnd_signal(cnd_t *cnd)
{
	if (!cnd)
		return thrd_error;

	WakeConditionVariable(&cnd->cond);

	return thrd_success;
}

static inline int cnd_broadcast(cnd_t *cnd)
{
	if (!cnd)
		return thrd_error;

	WakeAllConditionVariable(&cnd->cond);

	return thrd_success;
}

static inline int cnd_wait(cnd_t *cnd, mtx_t *mtx)
{
	BOOL success;

	if (!cnd || !mtx)
		return thrd_error;

	success = SleepConditionVariableCS(&cnd->cond, &mtx->cs, INFINITE);
	if (!success)
		return thrd_error;

	return thrd_success;
}

#endif /* THREADS_H */
