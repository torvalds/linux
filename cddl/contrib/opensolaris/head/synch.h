/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2014 Garrett D'Amore <garrett@damore.org>
 * Copyright (c) 1992, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYNCH_H
#define	_SYNCH_H

/*
 * synch.h:
 * definitions needed to use the thread synchronization interface
 */

#ifndef _ASM
#include <sys/machlock.h>
#include <sys/time_impl.h>
#include <sys/synch.h>
#endif /* _ASM */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _ASM

/*
 * Semaphores
 */
typedef struct _sema {
	/* this structure must be the same as sem_t in <semaphore.h> */
	uint32_t	count;		/* semaphore count */
	uint16_t	type;
	uint16_t	magic;
	upad64_t	pad1[3];	/* reserved for a mutex_t */
	upad64_t 	pad2[2];	/* reserved for a cond_t */
} sema_t;

/*
 * POSIX.1c Note:
 * POSIX.1c requires that <pthread.h> define the structures pthread_mutex_t
 * and pthread_cond_t.  These structures are identical to mutex_t (lwp_mutex_t)
 * and cond_t (lwp_cond_t) which are defined in <synch.h>.  A nested included
 * of <synch.h> (to allow a "#typedef mutex_t  pthread_mutex_t") would pull in
 * non-posix symbols/constants violating the namespace restrictions.  Hence,
 * pthread_mutex_t/pthread_cond_t have been redefined in <pthread.h> (actually
 * in <sys/types.h>).  Any modifications done to mutex_t/lwp_mutex_t or
 * cond_t/lwp_cond_t should also be done to pthread_mutex_t/pthread_cond_t.
 */
typedef lwp_mutex_t mutex_t;
typedef lwp_cond_t cond_t;

/*
 * Readers/writer locks
 *
 * NOTE: The layout of this structure should be kept in sync with the layout
 * of the correponding structure of pthread_rwlock_t in sys/types.h.
 * Also, there is an identical structure for lwp_rwlock_t in <sys/synch.h>.
 * Because we have to deal with C++, we cannot redefine this one as that one.
 */
typedef struct _rwlock {
	int32_t		readers;	/* rwstate word */
	uint16_t	type;
	uint16_t	magic;
	mutex_t		mutex;		/* used with process-shared rwlocks */
	cond_t		readercv;	/* used only to indicate ownership */
	cond_t		writercv;	/* used only to indicate ownership */
} rwlock_t;

int	_lwp_mutex_lock(lwp_mutex_t *);
int	_lwp_mutex_unlock(lwp_mutex_t *);
int	_lwp_mutex_trylock(lwp_mutex_t *);
int	_lwp_cond_wait(lwp_cond_t *, lwp_mutex_t *);
int	_lwp_cond_timedwait(lwp_cond_t *, lwp_mutex_t *, timespec_t *);
int	_lwp_cond_reltimedwait(lwp_cond_t *, lwp_mutex_t *, timespec_t *);
int	_lwp_cond_signal(lwp_cond_t *);
int	_lwp_cond_broadcast(lwp_cond_t *);
int	_lwp_sema_init(lwp_sema_t *, int);
int	_lwp_sema_wait(lwp_sema_t *);
int	_lwp_sema_trywait(lwp_sema_t *);
int	_lwp_sema_post(lwp_sema_t *);
int	cond_init(cond_t *, int, void *);
int	cond_destroy(cond_t *);
int	cond_wait(cond_t *, mutex_t *);
int	cond_timedwait(cond_t *, mutex_t *, const timespec_t *);
int	cond_reltimedwait(cond_t *, mutex_t *, const timespec_t *);
int	cond_signal(cond_t *);
int	cond_broadcast(cond_t *);
int	mutex_init(mutex_t *, int, void *);
int	mutex_destroy(mutex_t *);
int	mutex_consistent(mutex_t *);
int	mutex_lock(mutex_t *);
int	mutex_trylock(mutex_t *);
int	mutex_unlock(mutex_t *);
int	rwlock_init(rwlock_t *, int, void *);
int	rwlock_destroy(rwlock_t *);
int	rw_rdlock(rwlock_t *);
int	rw_wrlock(rwlock_t *);
int	rw_unlock(rwlock_t *);
int	rw_tryrdlock(rwlock_t *);
int	rw_trywrlock(rwlock_t *);
int	sema_init(sema_t *, unsigned int, int, void *);
int	sema_destroy(sema_t *);
int	sema_wait(sema_t *);
int	sema_timedwait(sema_t *, const timespec_t *);
int	sema_reltimedwait(sema_t *, const timespec_t *);
int	sema_post(sema_t *);
int	sema_trywait(sema_t *);

#endif /* _ASM */

/* "Magic numbers" tagging synchronization object types */
#define	MUTEX_MAGIC	_MUTEX_MAGIC
#define	SEMA_MAGIC	_SEMA_MAGIC
#define	COND_MAGIC	_COND_MAGIC
#define	RWL_MAGIC	_RWL_MAGIC

/*
 * POSIX.1c Note:
 * DEFAULTMUTEX is defined same as PTHREAD_MUTEX_INITIALIZER in <pthread.h>.
 * DEFAULTCV is defined same as PTHREAD_COND_INITIALIZER in <pthread.h>.
 * DEFAULTRWLOCK is defined same as PTHREAD_RWLOCK_INITIALIZER in <pthread.h>.
 * Any changes to these macros should be reflected in <pthread.h>
 */
#define	DEFAULTMUTEX	\
	{{0, 0, 0, {USYNC_THREAD}, MUTEX_MAGIC}, \
	{{{0, 0, 0, 0, 0, 0, 0, 0}}}, 0}
#define	SHAREDMUTEX	\
	{{0, 0, 0, {USYNC_PROCESS}, MUTEX_MAGIC}, \
	{{{0, 0, 0, 0, 0, 0, 0, 0}}}, 0}
#define	RECURSIVEMUTEX	\
	{{0, 0, 0, {USYNC_THREAD|LOCK_RECURSIVE}, MUTEX_MAGIC}, \
	{{{0, 0, 0, 0, 0, 0, 0, 0}}}, 0}
#define	ERRORCHECKMUTEX	\
	{{0, 0, 0, {USYNC_THREAD|LOCK_ERRORCHECK}, MUTEX_MAGIC}, \
	{{{0, 0, 0, 0, 0, 0, 0, 0}}}, 0}
#define	RECURSIVE_ERRORCHECKMUTEX	\
	{{0, 0, 0, {USYNC_THREAD|LOCK_RECURSIVE|LOCK_ERRORCHECK}, \
	MUTEX_MAGIC}, {{{0, 0, 0, 0, 0, 0, 0, 0}}}, 0}
#define	DEFAULTCV	\
	{{{0, 0, 0, 0}, USYNC_THREAD, COND_MAGIC}, 0}
#define	SHAREDCV	\
	{{{0, 0, 0, 0}, USYNC_PROCESS, COND_MAGIC}, 0}
#define	DEFAULTSEMA	\
	{0, USYNC_THREAD, SEMA_MAGIC, {0, 0, 0}, {0, 0}}
#define	SHAREDSEMA	\
	{0, USYNC_PROCESS, SEMA_MAGIC, {0, 0, 0}, {0, 0}}
#define	DEFAULTRWLOCK	\
	{0, USYNC_THREAD, RWL_MAGIC, DEFAULTMUTEX, DEFAULTCV, DEFAULTCV}
#define	SHAREDRWLOCK	\
	{0, USYNC_PROCESS, RWL_MAGIC, SHAREDMUTEX, SHAREDCV, SHAREDCV}

/*
 * Tests on lock states.
 */
#define	SEMA_HELD(x)		_sema_held(x)
#define	RW_READ_HELD(x)		_rw_read_held(x)
#define	RW_WRITE_HELD(x)	_rw_write_held(x)
#define	RW_LOCK_HELD(x)		(RW_READ_HELD(x) || RW_WRITE_HELD(x))
#define	MUTEX_HELD(x)		_mutex_held(x)

/*
 * The following definitions are for assertions which can be checked
 * statically by tools like lock_lint.  You can also define your own
 * run-time test for each.  If you don't, we define them to 1 so that
 * such assertions simply pass.
 */
#ifndef NO_LOCKS_HELD
#define	NO_LOCKS_HELD	1
#endif
#ifndef NO_COMPETING_THREADS
#define	NO_COMPETING_THREADS	1
#endif

#ifndef _ASM

/*
 * The *_held() functions apply equally well to Solaris threads
 * and to Posix threads synchronization objects, but the formal
 * type declarations are different, so we just declare the argument
 * to each *_held() function to be a void *, expecting that they will
 * be called with the proper type of argument in each case.
 */
int _sema_held(void *);			/* sema_t or sem_t */
int _rw_read_held(void *);		/* rwlock_t or pthread_rwlock_t */
int _rw_write_held(void *);		/* rwlock_t or pthread_rwlock_t */
int _mutex_held(void *);		/* mutex_t or pthread_mutex_t */

/* Pause API */
void smt_pause(void);

#endif /* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYNCH_H */
