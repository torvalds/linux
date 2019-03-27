/**
 * util/locks.c - unbound locking primitives
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 * 
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 * Implementation of locking and threading support.
 * A place for locking debug code since most locking functions are macros.
 */

#include "config.h"
#include "util/locks.h"
#include <signal.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

/** block all signals, masks them away. */
void 
ub_thread_blocksigs(void)
{
#if defined(HAVE_PTHREAD) || defined(HAVE_SOLARIS_THREADS) || defined(HAVE_SIGPROCMASK)
#  if defined(HAVE_PTHREAD) || defined(HAVE_SOLARIS_THREADS)
	int err;
#  endif
	sigset_t sigset;
	sigfillset(&sigset);
#ifdef HAVE_PTHREAD
	if((err=pthread_sigmask(SIG_SETMASK, &sigset, NULL)))
		fatal_exit("pthread_sigmask: %s", strerror(err));
#else
#  ifdef HAVE_SOLARIS_THREADS
	if((err=thr_sigsetmask(SIG_SETMASK, &sigset, NULL)))
		fatal_exit("thr_sigsetmask: %s", strerror(err));
#  else 
	/* have nothing, do single process signal mask */
	if(sigprocmask(SIG_SETMASK, &sigset, NULL))
		fatal_exit("sigprocmask: %s", strerror(errno));
#  endif /* HAVE_SOLARIS_THREADS */
#endif /* HAVE_PTHREAD */
#endif /* have signal stuff */
}

/** unblock one signal, so we can catch it */
void ub_thread_sig_unblock(int sig)
{
#if defined(HAVE_PTHREAD) || defined(HAVE_SOLARIS_THREADS) || defined(HAVE_SIGPROCMASK)
#  if defined(HAVE_PTHREAD) || defined(HAVE_SOLARIS_THREADS)
	int err;
#  endif
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, sig);
#ifdef HAVE_PTHREAD
	if((err=pthread_sigmask(SIG_UNBLOCK, &sigset, NULL)))
		fatal_exit("pthread_sigmask: %s", strerror(err));
#else
#  ifdef HAVE_SOLARIS_THREADS
	if((err=thr_sigsetmask(SIG_UNBLOCK, &sigset, NULL)))
		fatal_exit("thr_sigsetmask: %s", strerror(err));
#  else 
	/* have nothing, do single thread case */
	if(sigprocmask(SIG_UNBLOCK, &sigset, NULL))
		fatal_exit("sigprocmask: %s", strerror(errno));
#  endif /* HAVE_SOLARIS_THREADS */
#endif /* HAVE_PTHREAD */
#else
	(void)sig;
#endif /* have signal stuff */
}

#if !defined(HAVE_PTHREAD) && !defined(HAVE_SOLARIS_THREADS) && !defined(HAVE_WINDOWS_THREADS)
/**
 * No threading available: fork a new process.
 * This means no shared data structure, and no locking.
 * Only the main thread ever returns. Exits on errors.
 * @param thr: the location where to store the thread-id.
 * @param func: function body of the thread. Return value of func is lost.
 * @param arg: user argument to func.
 */
void 
ub_thr_fork_create(ub_thread_type* thr, void* (*func)(void*), void* arg)
{
	pid_t pid = fork();
	switch(pid) {
	default:	/* main */
			*thr = (ub_thread_type)pid;
			return;
	case 0: 	/* child */
			*thr = (ub_thread_type)getpid();
			(void)(*func)(arg);
			exit(0);
	case -1:	/* error */
			fatal_exit("could not fork: %s", strerror(errno));
	}
}

/**
 * There is no threading. Wait for a process to terminate.
 * Note that ub_thread_type is defined as pid_t.
 * @param thread: the process id to wait for.
 */
void ub_thr_fork_wait(ub_thread_type thread)
{
	int status = 0;
	if(waitpid((pid_t)thread, &status, 0) == -1)
		log_err("waitpid(%d): %s", (int)thread, strerror(errno));
	if(status != 0)
		log_warn("process %d abnormal exit with status %d",
			(int)thread, status);
}
#endif /* !defined(HAVE_PTHREAD) && !defined(HAVE_SOLARIS_THREADS) && !defined(HAVE_WINDOWS_THREADS) */

#ifdef HAVE_SOLARIS_THREADS
void* ub_thread_key_get(ub_thread_key_type key)
{
	void* ret=NULL;
	LOCKRET(thr_getspecific(key, &ret));
	return ret;
}
#endif

#ifdef HAVE_WINDOWS_THREADS
/** log a windows GetLastError message */
static void log_win_err(const char* str, DWORD err)
{
	LPTSTR buf;
	if(FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | 
		FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER, 
		NULL, err, 0, (LPTSTR)&buf, 0, NULL) == 0) {
		/* could not format error message */
		log_err("%s, GetLastError=%d", str, (int)err);
		return;
	}
	log_err("%s, (err=%d): %s", str, (int)err, buf);
	LocalFree(buf);
}

void lock_basic_init(lock_basic_type* lock)
{
	/* implement own lock, because windows HANDLE as Mutex usage
	 * uses too many handles and would bog down the whole system. */
	(void)InterlockedExchange(lock, 0);
}

void lock_basic_destroy(lock_basic_type* lock)
{
	(void)InterlockedExchange(lock, 0);
}

void lock_basic_lock(lock_basic_type* lock)
{
	LONG wait = 1; /* wait 1 msec at first */

	while(InterlockedExchange(lock, 1)) {
		/* if the old value was 1 then if was already locked */
		Sleep(wait); /* wait with sleep */
		wait *= 2;   /* exponential backoff for waiting */
	}
	/* the old value was 0, but we inserted 1, we locked it! */
}

void lock_basic_unlock(lock_basic_type* lock)
{
	/* unlock it by inserting the value of 0. xchg for cache coherency. */
	(void)InterlockedExchange(lock, 0);
}

void ub_thread_key_create(ub_thread_key_type* key, void* f)
{
	*key = TlsAlloc();
	if(*key == TLS_OUT_OF_INDEXES) {
		*key = 0;
		log_win_err("TlsAlloc Failed(OUT_OF_INDEXES)", GetLastError());
	}
	else ub_thread_key_set(*key, f);
}

void ub_thread_key_set(ub_thread_key_type key, void* v)
{
	if(!TlsSetValue(key, v)) {
		log_win_err("TlsSetValue failed", GetLastError());
	}
}

void* ub_thread_key_get(ub_thread_key_type key)
{
	void* ret = (void*)TlsGetValue(key);
	if(ret == NULL && GetLastError() != ERROR_SUCCESS) {
		log_win_err("TlsGetValue failed", GetLastError());
	}
	return ret;
}

void ub_thread_create(ub_thread_type* thr, void* (*func)(void*), void* arg)
{
#ifndef HAVE__BEGINTHREADEX
	*thr = CreateThread(NULL, /* default security (no inherit handle) */
		0, /* default stack size */
		(LPTHREAD_START_ROUTINE)func, arg,
		0, /* default flags, run immediately */
		NULL); /* do not store thread identifier anywhere */
#else
	/* the beginthreadex routine setups for the C lib; aligns stack */
	*thr=(ub_thread_type)_beginthreadex(NULL, 0, (void*)func, arg, 0, NULL);
#endif
	if(*thr == NULL) {
		log_win_err("CreateThread failed", GetLastError());
		fatal_exit("thread create failed");
	}
}

ub_thread_type ub_thread_self(void)
{
	return GetCurrentThread();
}

void ub_thread_join(ub_thread_type thr)
{
	DWORD ret = WaitForSingleObject(thr, INFINITE);
	if(ret == WAIT_FAILED) {
		log_win_err("WaitForSingleObject(Thread):WAIT_FAILED", 
			GetLastError());
	} else if(ret == WAIT_TIMEOUT) {
		log_win_err("WaitForSingleObject(Thread):WAIT_TIMEOUT", 
			GetLastError());
	}
	/* and close the handle to the thread */
	if(!CloseHandle(thr)) {
		log_win_err("CloseHandle(Thread) failed", GetLastError());
	}
}
#endif /* HAVE_WINDOWS_THREADS */
