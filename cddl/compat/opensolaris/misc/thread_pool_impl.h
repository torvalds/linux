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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * $FreeBSD$
 */

#ifndef _THREAD_POOL_IMPL_H
#define	_THREAD_POOL_IMPL_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <thread_pool.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Thread pool implementation definitions.
 * See <thread_pool.h> for interface declarations.
 */

/*
 * FIFO queued job
 */
typedef struct tpool_job tpool_job_t;
struct tpool_job {
	tpool_job_t	*tpj_next;		/* list of jobs */
	void		(*tpj_func)(void *);	/* function to call */
	void		*tpj_arg;		/* its argument */
};

/*
 * List of active threads, linked through their stacks.
 */
typedef struct tpool_active tpool_active_t;
struct tpool_active {
	tpool_active_t	*tpa_next;	/* list of active threads */
	pthread_t	tpa_tid;	/* active thread id */
};

/*
 * The thread pool.
 */
struct tpool {
	tpool_t		*tp_forw;	/* circular list of all thread pools */
	tpool_t		*tp_back;
	mutex_t		tp_mutex;	/* protects the pool data */
	cond_t		tp_busycv;	/* synchronization in tpool_dispatch */
	cond_t		tp_workcv;	/* synchronization with workers */
	cond_t		tp_waitcv;	/* synchronization in tpool_wait() */
	tpool_active_t	*tp_active;	/* threads performing work */
	tpool_job_t	*tp_head;	/* FIFO job queue */
	tpool_job_t	*tp_tail;
	pthread_attr_t	tp_attr;	/* attributes of the workers */
	int		tp_flags;	/* see below */
	uint_t		tp_linger;	/* seconds before idle workers exit */
	int		tp_njobs;	/* number of jobs in job queue */
	int		tp_minimum;	/* minimum number of worker threads */
	int		tp_maximum;	/* maximum number of worker threads */
	int		tp_current;	/* current number of worker threads */
	int		tp_idle;	/* number of idle workers */
};

/* tp_flags */
#define	TP_WAIT		0x01		/* waiting in tpool_wait() */
#define	TP_SUSPEND	0x02		/* pool is being suspended */
#define	TP_DESTROY	0x04		/* pool is being destroyed */
#define	TP_ABANDON	0x08		/* pool is abandoned (auto-destroy) */

#ifdef	__cplusplus
}
#endif

#endif /* _THREAD_POOL_IMPL_H */
