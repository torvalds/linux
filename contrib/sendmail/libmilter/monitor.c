/*
 *  Copyright (c) 2006 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: monitor.c,v 8.8 2013-11-22 20:51:36 ca Exp $")
#include "libmilter.h"

#if _FFR_THREAD_MONITOR

/*
**  Thread Monitoring
**  Todo: more error checking (return code from function calls)
**  add comments.
*/

bool Monitor = false; /* use monitoring? */
static unsigned int Mon_exec_time = 0;

/* mutex protects Mon_cur_ctx, Mon_ctx_head, and ctx_start */
static smutex_t Mon_mutex;
static scond_t Mon_cv;

/*
**  Current ctx to monitor.
**  Invariant:
**  Mon_cur_ctx == NULL || Mon_cur_ctx is thread which was started the longest
**	time ago.
**
**  Basically the entries in the list are ordered by time because new
**	entries are appended at the end. However, due to the concurrent
**	execution (multi-threaded) and no guaranteed order of wakeups
**	after a mutex_lock() attempt, the order might not be strict,
**	i.e., if the list contains e1 and e2 (in that order) then
**	the the start time of e2 can be (slightly) smaller than that of e1.
**	However, this slight inaccurracy should not matter for the proper
**	working of this algorithm.
*/

static SMFICTX_PTR Mon_cur_ctx = NULL;
static smfi_hd_T Mon_ctx_head; /* head of the linked list of active contexts */

/*
**  SMFI_SET_MAX_EXEC_TIME -- set maximum execution time for a thread
**
**	Parameters:
**		tm -- maximum execution time for a thread
**
**	Returns:
**		MI_SUCCESS
*/

int
smfi_set_max_exec_time(tm)
	unsigned int tm;
{
	Mon_exec_time = tm;
	return MI_SUCCESS;
}

/*
**  MI_MONITOR_THREAD -- monitoring thread
**
**	Parameters:
**		arg -- ignored (required by pthread_create())
**
**	Returns:
**		NULL on termination.
*/

static void *
mi_monitor_thread(arg)
	void *arg;
{
	sthread_t tid;
	int r;
	time_t now, end;

	SM_ASSERT(Monitor);
	SM_ASSERT(Mon_exec_time > 0);
	tid = (sthread_t) sthread_get_id();
	if (pthread_detach(tid) != 0)
	{
		/* log an error */
		return (void *)1;
	}

/*
**  NOTE: this is "flow through" code,
**  do NOT use do { } while ("break" is used here!)
*/

#define MON_CHK_STOP							\
	now = time(NULL);						\
	end = Mon_cur_ctx->ctx_start + Mon_exec_time;			\
	if (now > end)							\
	{								\
		smi_log(SMI_LOG_ERR,					\
			"WARNING: monitor timeout triggered, now=%ld, end=%ld, tid=%ld, state=0x%x",\
			(long) now, (long) end,				\
			(long) Mon_cur_ctx->ctx_id, Mon_cur_ctx->ctx_state);\
		mi_stop_milters(MILTER_STOP);				\
		break;							\
	}

	(void) smutex_lock(&Mon_mutex);
	while (mi_stop() == MILTER_CONT)
	{
		if (Mon_cur_ctx != NULL && Mon_cur_ctx->ctx_start > 0)
		{
			struct timespec abstime;

			MON_CHK_STOP;
			abstime.tv_sec = end;
			abstime.tv_nsec = 0;
			r = pthread_cond_timedwait(&Mon_cv, &Mon_mutex,
					&abstime);
		}
		else
			r = pthread_cond_wait(&Mon_cv, &Mon_mutex);
		if (mi_stop() != MILTER_CONT)
			break;
		if (Mon_cur_ctx != NULL && Mon_cur_ctx->ctx_start > 0)
		{
			MON_CHK_STOP;
		}
	}
	(void) smutex_unlock(&Mon_mutex);

	return NULL;
}

/*
**  MI_MONITOR_INIT -- initialize monitoring thread
**
**	Parameters: none
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
mi_monitor_init()
{
	int r;
	sthread_t tid;

	SM_ASSERT(!Monitor);
	if (Mon_exec_time <= 0)
		return MI_SUCCESS;
	Monitor = true;
	if (!smutex_init(&Mon_mutex))
		return MI_FAILURE;
	if (scond_init(&Mon_cv) != 0)
		return MI_FAILURE;
	SM_TAILQ_INIT(&Mon_ctx_head);

	r = thread_create(&tid, mi_monitor_thread, (void *)NULL);
	if (r != 0)
		return r;
	return MI_SUCCESS;
}

/*
**  MI_MONITOR_WORK_BEGIN -- record start of thread execution
**
**	Parameters:
**		ctx -- session context
**		cmd -- milter command char
**
**	Returns:
**		0
*/

int
mi_monitor_work_begin(ctx, cmd)
	SMFICTX_PTR ctx;
	int cmd;
{
	(void) smutex_lock(&Mon_mutex);
	if (NULL == Mon_cur_ctx)
	{
		Mon_cur_ctx = ctx;
		(void) scond_signal(&Mon_cv);
	}
	ctx->ctx_start = time(NULL);
	SM_TAILQ_INSERT_TAIL(&Mon_ctx_head, ctx, ctx_mon_link);
	(void) smutex_unlock(&Mon_mutex);
	return 0;
}

/*
**  MI_MONITOR_WORK_END -- record end of thread execution
**
**	Parameters:
**		ctx -- session context
**		cmd -- milter command char
**
**	Returns:
**		0
*/

int
mi_monitor_work_end(ctx, cmd)
	SMFICTX_PTR ctx;
	int cmd;
{
	(void) smutex_lock(&Mon_mutex);
	ctx->ctx_start = 0;
	SM_TAILQ_REMOVE(&Mon_ctx_head, ctx, ctx_mon_link);
	if (Mon_cur_ctx == ctx)
	{
		if (SM_TAILQ_EMPTY(&Mon_ctx_head))
			Mon_cur_ctx = NULL;
		else
			Mon_cur_ctx = SM_TAILQ_FIRST(&Mon_ctx_head);
	}
	(void) smutex_unlock(&Mon_mutex);
	return 0;
}
#endif /* _FFR_THREAD_MONITOR */
