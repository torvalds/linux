/*
 *  Copyright (c) 2003-2004, 2007, 2009-2012 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 * Contributed by Jose Marcio Martins da Cruz - Ecole des Mines de Paris
 *   Jose-Marcio.Martins@ensmp.fr
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: worker.c,v 8.25 2013-11-22 20:51:37 ca Exp $")

#include "libmilter.h"

#if _FFR_WORKERS_POOL

typedef struct taskmgr_S taskmgr_T;

#define TM_SIGNATURE		0x23021957

struct taskmgr_S
{
	long		tm_signature; /* has the controller been initialized */
	sthread_t	tm_tid;	/* thread id of controller */
	smfi_hd_T	tm_ctx_head; /* head of the linked list of contexts */

	int		tm_nb_workers;	/* number of workers in the pool */
	int		tm_nb_idle;	/* number of workers waiting */

	int		tm_p[2];	/* poll control pipe */

	smutex_t	tm_w_mutex;	/* linked list access mutex */
	scond_t		tm_w_cond;	/* */
};

static taskmgr_T     Tskmgr = {0};

#define WRK_CTX_HEAD	Tskmgr.tm_ctx_head

#define RD_PIPE	(Tskmgr.tm_p[0])
#define WR_PIPE	(Tskmgr.tm_p[1])

#define PIPE_SEND_SIGNAL()						\
	do								\
	{								\
		char evt = 0x5a;					\
		int fd = WR_PIPE;					\
		if (write(fd, &evt, sizeof(evt)) != sizeof(evt))	\
			smi_log(SMI_LOG_ERR,				\
				"Error writing to event pipe: %s",	\
				sm_errstring(errno));			\
	} while (0)

#ifndef USE_PIPE_WAKE_POLL
# define USE_PIPE_WAKE_POLL 1
#endif /* USE_PIPE_WAKE_POLL */

/* poll check periodicity (default 10000 - 10 s) */
#define POLL_TIMEOUT   10000

/* worker conditional wait timeout (default 10 s) */
#define COND_TIMEOUT     10

/* functions */
static int mi_close_session __P((SMFICTX_PTR));

static void *mi_worker __P((void *));
static void *mi_pool_controller __P((void *));

static int mi_list_add_ctx __P((SMFICTX_PTR));
static int mi_list_del_ctx __P((SMFICTX_PTR));

/*
**  periodicity of cleaning up old sessions (timedout)
**	sessions list will be checked to find old inactive
**	sessions each DT_CHECK_OLD_SESSIONS sec
*/

#define DT_CHECK_OLD_SESSIONS   600

#ifndef OLD_SESSION_TIMEOUT
# define OLD_SESSION_TIMEOUT      ctx->ctx_timeout
#endif /* OLD_SESSION_TIMEOUT */

/* session states - with respect to the pool of workers */
#define WKST_INIT		0	/* initial state */
#define WKST_READY_TO_RUN	1	/* command ready do be read */
#define WKST_RUNNING		2	/* session running on a worker */
#define WKST_READY_TO_WAIT	3	/* session just finished by a worker */
#define WKST_WAITING		4	/* waiting for new command */
#define WKST_CLOSING		5	/* session finished */

#ifndef MIN_WORKERS
# define MIN_WORKERS	2  /* minimum number of threads to keep around */
#endif

#define MIN_IDLE	1  /* minimum number of idle threads */


/*
**  Macros for threads and mutex management
*/

#define TASKMGR_LOCK()							\
	do								\
	{								\
		if (!smutex_lock(&Tskmgr.tm_w_mutex))			\
			smi_log(SMI_LOG_ERR, "TASKMGR_LOCK error");	\
	} while (0)

#define TASKMGR_UNLOCK()						\
	do								\
	{								\
		if (!smutex_unlock(&Tskmgr.tm_w_mutex))			\
			smi_log(SMI_LOG_ERR, "TASKMGR_UNLOCK error");	\
	} while (0)

#define	TASKMGR_COND_WAIT()						\
	scond_timedwait(&Tskmgr.tm_w_cond, &Tskmgr.tm_w_mutex, COND_TIMEOUT)

#define	TASKMGR_COND_SIGNAL()						\
	do								\
	{								\
		if (scond_signal(&Tskmgr.tm_w_cond) != 0)		\
			smi_log(SMI_LOG_ERR, "TASKMGR_COND_SIGNAL error"); \
	} while (0)

#define LAUNCH_WORKER(ctx)						\
	do								\
	{								\
		int r;							\
		sthread_t tid;						\
									\
		if ((r = thread_create(&tid, mi_worker, ctx)) != 0)	\
			smi_log(SMI_LOG_ERR, "LAUNCH_WORKER error: %s",\
				sm_errstring(r));			\
	} while (0)

#if POOL_DEBUG
# define POOL_LEV_DPRINTF(lev, x)					\
	do								\
	{								\
		if ((lev) < ctx->ctx_dbg)				\
			sm_dprintf x;					\
	} while (0)
#else /* POOL_DEBUG */
# define POOL_LEV_DPRINTF(lev, x)
#endif /* POOL_DEBUG */

/*
**  MI_START_SESSION -- Start a session in the pool of workers
**
**	Parameters:
**		ctx -- context structure
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
mi_start_session(ctx)
	SMFICTX_PTR ctx;
{
	static long id = 0;

	/* this can happen if the milter is shutting down */
	if (Tskmgr.tm_signature != TM_SIGNATURE)
		return MI_FAILURE;
	SM_ASSERT(ctx != NULL);
	POOL_LEV_DPRINTF(4, ("PIPE r=[%d] w=[%d]", RD_PIPE, WR_PIPE));
	TASKMGR_LOCK();

	if (mi_list_add_ctx(ctx) != MI_SUCCESS)
	{
		TASKMGR_UNLOCK();
		return MI_FAILURE;
	}

	ctx->ctx_sid = id++;

	/* if there is an idle worker, signal it, otherwise start new worker */
	if (Tskmgr.tm_nb_idle > 0)
	{
		ctx->ctx_wstate = WKST_READY_TO_RUN;
		TASKMGR_COND_SIGNAL();
	}
	else
	{
		ctx->ctx_wstate = WKST_RUNNING;
		LAUNCH_WORKER(ctx);
	}
	TASKMGR_UNLOCK();
	return MI_SUCCESS;
}

/*
**  MI_CLOSE_SESSION -- Close a session and clean up data structures
**
**	Parameters:
**		ctx -- context structure
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

static int
mi_close_session(ctx)
	SMFICTX_PTR ctx;
{
	SM_ASSERT(ctx != NULL);

	(void) mi_list_del_ctx(ctx);
	mi_clr_ctx(ctx);

	return MI_SUCCESS;
}

/*
**  NONBLOCKING -- set nonblocking mode for a file descriptor.
**
**	Parameters:
**		fd -- file descriptor
**		name -- name for (error) logging
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

static int
nonblocking(int fd, const char *name)
{
	int r;

	errno = 0;
	r = fcntl(fd, F_GETFL, 0);
	if (r == -1)
	{
		smi_log(SMI_LOG_ERR, "fcntl(%s, F_GETFL)=%s",
			name, sm_errstring(errno));
		return MI_FAILURE;
	}
	errno = 0;
	r = fcntl(fd, F_SETFL, r | O_NONBLOCK);
	if (r == -1)
	{
		smi_log(SMI_LOG_ERR, "fcntl(%s, F_SETFL, O_NONBLOCK)=%s",
			name, sm_errstring(errno));
		return MI_FAILURE;
	}
	return MI_SUCCESS;
}

/*
**  MI_POOL_CONTROLLER_INIT -- Launch the worker pool controller
**		Must be called before starting sessions.
**
**	Parameters:
**		none
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
mi_pool_controller_init()
{
	sthread_t tid;
	int r, i;

	if (Tskmgr.tm_signature == TM_SIGNATURE)
		return MI_SUCCESS;

	SM_TAILQ_INIT(&WRK_CTX_HEAD);
	Tskmgr.tm_tid = (sthread_t) -1;
	Tskmgr.tm_nb_workers = 0;
	Tskmgr.tm_nb_idle = 0;

	if (pipe(Tskmgr.tm_p) != 0)
	{
		smi_log(SMI_LOG_ERR, "can't create event pipe: %s",
			sm_errstring(errno));
		return MI_FAILURE;
	}
	r = nonblocking(WR_PIPE, "WR_PIPE");
	if (r != MI_SUCCESS)
		return r;
	r = nonblocking(RD_PIPE, "RD_PIPE");
	if (r != MI_SUCCESS)
		return r;

	(void) smutex_init(&Tskmgr.tm_w_mutex);
	(void) scond_init(&Tskmgr.tm_w_cond);

	/* Launch the pool controller */
	if ((r = thread_create(&tid, mi_pool_controller, (void *) NULL)) != 0)
	{
		smi_log(SMI_LOG_ERR, "can't create controller thread: %s",
			sm_errstring(r));
		return MI_FAILURE;
	}
	Tskmgr.tm_tid = tid;
	Tskmgr.tm_signature = TM_SIGNATURE;

	/* Create the pool of workers */
	for (i = 0; i < MIN_WORKERS; i++)
	{
		if ((r = thread_create(&tid, mi_worker, (void *) NULL)) != 0)
		{
			smi_log(SMI_LOG_ERR, "can't create workers crew: %s",
				sm_errstring(r));
			return MI_FAILURE;
		}
	}

	return MI_SUCCESS;
}

/*
**  MI_POOL_CONTROLLER -- manage the pool of workers
**	This thread must be running when listener begins
**	starting sessions
**
**	Parameters:
**		arg -- unused
**
**	Returns:
**		NULL
**
**	Control flow:
**		for (;;)
**			Look for timed out sessions
**			Select sessions to wait for sendmail command
**			Poll set of file descriptors
**			if timeout
**				continue
**			For each file descriptor ready
**				launch new thread if no worker available
**				else
**				signal waiting worker
*/

/* Poll structure array (pollfd) size step */
#define PFD_STEP	256

#define WAIT_FD(i)	(pfd[i].fd)
#define WAITFN		"POLL"

static void *
mi_pool_controller(arg)
	void *arg;
{
	struct pollfd *pfd = NULL;
	int dim_pfd = 0;
	bool rebuild_set = true;
	int pcnt = 0; /* error count for poll() failures */
	time_t lastcheck;

	Tskmgr.tm_tid = sthread_get_id();
	if (pthread_detach(Tskmgr.tm_tid) != 0)
	{
		smi_log(SMI_LOG_ERR, "Failed to detach pool controller thread");
		return NULL;
	}

	pfd = (struct pollfd *) malloc(PFD_STEP * sizeof(struct pollfd));
	if (pfd == NULL)
	{
		smi_log(SMI_LOG_ERR, "Failed to malloc pollfd array: %s",
			sm_errstring(errno));
		return NULL;
	}
	dim_pfd = PFD_STEP;

	lastcheck = time(NULL);
	for (;;)
	{
		SMFICTX_PTR ctx;
		int nfd, r, i;
		time_t now;

		POOL_LEV_DPRINTF(4, ("Let's %s again...", WAITFN));

		if (mi_stop() != MILTER_CONT)
			break;

		TASKMGR_LOCK();

		now = time(NULL);

		/* check for timed out sessions? */
		if (lastcheck + DT_CHECK_OLD_SESSIONS < now)
		{
			ctx = SM_TAILQ_FIRST(&WRK_CTX_HEAD);
			while (ctx != SM_TAILQ_END(&WRK_CTX_HEAD))
			{
				SMFICTX_PTR ctx_nxt;

				ctx_nxt = SM_TAILQ_NEXT(ctx, ctx_link);
				if (ctx->ctx_wstate == WKST_WAITING)
				{
					if (ctx->ctx_wait == 0)
						ctx->ctx_wait = now;
					else if (ctx->ctx_wait + OLD_SESSION_TIMEOUT
						 < now)
					{
						/* if session timed out, close it */
						sfsistat (*fi_close) __P((SMFICTX *));

						POOL_LEV_DPRINTF(4,
							("Closing old connection: sd=%d id=%d",
							ctx->ctx_sd,
							ctx->ctx_sid));

						if ((fi_close = ctx->ctx_smfi->xxfi_close) != NULL)
							(void) (*fi_close)(ctx);

						mi_close_session(ctx);
					}
				}
				ctx = ctx_nxt;
			}
			lastcheck = now;
		}

		if (rebuild_set)
		{
			/*
			**  Initialize poll set.
			**  Insert into the poll set the file descriptors of
			**  all sessions waiting for a command from sendmail.
			*/

			nfd = 0;

			/* begin with worker pipe */
			pfd[nfd].fd = RD_PIPE;
			pfd[nfd].events = MI_POLL_RD_FLAGS;
			pfd[nfd].revents = 0;
			nfd++;

			SM_TAILQ_FOREACH(ctx, &WRK_CTX_HEAD, ctx_link)
			{
				/*
				**  update ctx_wait - start of wait moment -
				**  for timeout
				*/

				if (ctx->ctx_wstate == WKST_READY_TO_WAIT)
					ctx->ctx_wait = now;

				/* add the session to the pollfd array? */
				if ((ctx->ctx_wstate == WKST_READY_TO_WAIT) ||
				    (ctx->ctx_wstate == WKST_WAITING))
				{
					/*
					**  Resize the pollfd array if it
					**  isn't large enough.
					*/

					if (nfd >= dim_pfd)
					{
						struct pollfd *tpfd;
						size_t new;

						new = (dim_pfd + PFD_STEP) *
							sizeof(*tpfd);
						tpfd = (struct pollfd *)
							realloc(pfd, new);
						if (tpfd != NULL)
						{
							pfd = tpfd;
							dim_pfd += PFD_STEP;
						}
						else
						{
							smi_log(SMI_LOG_ERR,
								"Failed to realloc pollfd array:%s",
								sm_errstring(errno));
						}
					}

					/* add the session to pollfd array */
					if (nfd < dim_pfd)
					{
						ctx->ctx_wstate = WKST_WAITING;
						pfd[nfd].fd = ctx->ctx_sd;
						pfd[nfd].events = MI_POLL_RD_FLAGS;
						pfd[nfd].revents = 0;
						nfd++;
					}
				}
			}
			rebuild_set = false;
		}

		TASKMGR_UNLOCK();

		/* Everything is ready, let's wait for an event */
		r = poll(pfd, nfd, POLL_TIMEOUT);

		POOL_LEV_DPRINTF(4, ("%s returned: at epoch %d value %d",
			WAITFN, now, nfd));

		/* timeout */
		if (r == 0)
			continue;

		rebuild_set = true;

		/* error */
		if (r < 0)
		{
			if (errno == EINTR)
				continue;
			pcnt++;
			smi_log(SMI_LOG_ERR,
				"%s() failed (%s), %s",
				WAITFN, sm_errstring(errno),
				pcnt >= MAX_FAILS_S ? "abort" : "try again");

			if (pcnt >= MAX_FAILS_S)
				goto err;
			continue;
		}
		pcnt = 0;

		/* something happened */
		for (i = 0; i < nfd; i++)
		{
			if (pfd[i].revents == 0)
				continue;

			POOL_LEV_DPRINTF(4, ("%s event on pfd[%d/%d]=%d ",
				WAITFN, i, nfd,
			WAIT_FD(i)));

			/* has a worker signaled an end of task? */
			if (WAIT_FD(i) == RD_PIPE)
			{
				char evts[256];
				ssize_t r;

				POOL_LEV_DPRINTF(4,
					("PIPE WILL READ evt = %08X %08X",
					pfd[i].events, pfd[i].revents));

				r = 1;
				while ((pfd[i].revents & MI_POLL_RD_FLAGS) != 0
					&& r != -1)
				{
					r = read(RD_PIPE, evts, sizeof(evts));
				}

				POOL_LEV_DPRINTF(4,
					("PIPE DONE READ i=[%d] fd=[%d] r=[%d] evt=[%d]",
					i, RD_PIPE, (int) r, evts[0]));

				if ((pfd[i].revents & ~MI_POLL_RD_FLAGS) != 0)
				{
					/* Exception handling */
				}
				continue;
			}

			/*
			**  Not the pipe for workers waking us,
			**  so must be something on an MTA connection.
			*/

			TASKMGR_LOCK();
			SM_TAILQ_FOREACH(ctx, &WRK_CTX_HEAD, ctx_link)
			{
				if (ctx->ctx_wstate != WKST_WAITING)
					continue;

				POOL_LEV_DPRINTF(4,
					("Checking context sd=%d - fd=%d ",
					ctx->ctx_sd , WAIT_FD(i)));

				if (ctx->ctx_sd == pfd[i].fd)
				{

					POOL_LEV_DPRINTF(4,
						("TASK: found %d for fd[%d]=%d",
						ctx->ctx_sid, i, WAIT_FD(i)));

					if (Tskmgr.tm_nb_idle > 0)
					{
						ctx->ctx_wstate = WKST_READY_TO_RUN;
						TASKMGR_COND_SIGNAL();
					}
					else
					{
						ctx->ctx_wstate = WKST_RUNNING;
						LAUNCH_WORKER(ctx);
					}
					break;
				}
			}
			TASKMGR_UNLOCK();

			POOL_LEV_DPRINTF(4,
				("TASK %s FOUND - Checking PIPE for fd[%d]",
				ctx != NULL ? "" : "NOT", WAIT_FD(i)));
		}
	}

  err:
	if (pfd != NULL)
		free(pfd);

	Tskmgr.tm_signature = 0;
#if 0
	/*
	**  Do not clean up ctx -- it can cause double-free()s.
	**  The program is shutting down anyway, so it's not worth the trouble.
	**  There is a more complex solution that prevents race conditions
	**  while accessing ctx, but that's maybe for a later version.
	*/

	for (;;)
	{
		SMFICTX_PTR ctx;

		ctx = SM_TAILQ_FIRST(&WRK_CTX_HEAD);
		if (ctx == NULL)
			break;
		mi_close_session(ctx);
	}
#endif

	(void) smutex_destroy(&Tskmgr.tm_w_mutex);
	(void) scond_destroy(&Tskmgr.tm_w_cond);

	return NULL;
}

/*
**  Look for a task ready to run.
**  Value of ctx is NULL or a pointer to a task ready to run.
*/

#define GET_TASK_READY_TO_RUN()					\
	SM_TAILQ_FOREACH(ctx, &WRK_CTX_HEAD, ctx_link)		\
	{							\
		if (ctx->ctx_wstate == WKST_READY_TO_RUN)	\
		{						\
			ctx->ctx_wstate = WKST_RUNNING;		\
			break;					\
		}						\
	}

/*
**  MI_WORKER -- worker thread
**	executes tasks distributed by the mi_pool_controller
**	or by mi_start_session
**
**	Parameters:
**		arg -- pointer to context structure
**
**	Returns:
**		NULL pointer
*/

static void *
mi_worker(arg)
	void *arg;
{
	SMFICTX_PTR ctx;
	bool done;
	sthread_t t_id;
	int r;

	ctx = (SMFICTX_PTR) arg;
	done = false;
	if (ctx != NULL)
		ctx->ctx_wstate = WKST_RUNNING;

	t_id = sthread_get_id();
	if (pthread_detach(t_id) != 0)
	{
		smi_log(SMI_LOG_ERR, "Failed to detach worker thread");
		if (ctx != NULL)
			ctx->ctx_wstate = WKST_READY_TO_RUN;
		return NULL;
	}

	TASKMGR_LOCK();
	Tskmgr.tm_nb_workers++;
	TASKMGR_UNLOCK();

	while (!done)
	{
		if (mi_stop() != MILTER_CONT)
			break;

		/* let's handle next task... */
		if (ctx != NULL)
		{
			int res;

			POOL_LEV_DPRINTF(4,
				("worker %d: new task -> let's handle it",
				t_id));
			res = mi_engine(ctx);
			POOL_LEV_DPRINTF(4,
				("worker %d: mi_engine returned %d", t_id, res));

			TASKMGR_LOCK();
			if (res != MI_CONTINUE)
			{
				ctx->ctx_wstate = WKST_CLOSING;

				/*
				**  Delete context from linked list of
				**  sessions and close session.
				*/

				mi_close_session(ctx);
			}
			else
			{
				ctx->ctx_wstate = WKST_READY_TO_WAIT;

				POOL_LEV_DPRINTF(4,
					("writing to event pipe..."));

				/*
				**  Signal task controller to add new session
				**  to poll set.
				*/

				PIPE_SEND_SIGNAL();
			}
			TASKMGR_UNLOCK();
			ctx = NULL;

		}

		/* check if there is any task waiting to be served */
		TASKMGR_LOCK();

		GET_TASK_READY_TO_RUN();

		/* Got a task? */
		if (ctx != NULL)
		{
			TASKMGR_UNLOCK();
			continue;
		}

		/*
		**  if not, let's check if there is enough idle workers
		**	if yes: quit
		*/

		if (Tskmgr.tm_nb_workers > MIN_WORKERS &&
		    Tskmgr.tm_nb_idle > MIN_IDLE)
			done = true;

		POOL_LEV_DPRINTF(4, ("worker %d: checking ... %d %d", t_id,
			Tskmgr.tm_nb_workers, Tskmgr.tm_nb_idle + 1));

		if (done)
		{
			POOL_LEV_DPRINTF(4, ("worker %d: quitting... ", t_id));
			Tskmgr.tm_nb_workers--;
			TASKMGR_UNLOCK();
			continue;
		}

		/*
		**  if no task ready to run, wait for another one
		*/

		Tskmgr.tm_nb_idle++;
		TASKMGR_COND_WAIT();
		Tskmgr.tm_nb_idle--;

		/* look for a task */
		GET_TASK_READY_TO_RUN();

		TASKMGR_UNLOCK();
	}
	return NULL;
}

/*
**  MI_LIST_ADD_CTX -- add new session to linked list
**
**	Parameters:
**		ctx -- context structure
**
**	Returns:
**		MI_FAILURE/MI_SUCCESS
*/

static int
mi_list_add_ctx(ctx)
	SMFICTX_PTR ctx;
{
	SM_ASSERT(ctx != NULL);
	SM_TAILQ_INSERT_TAIL(&WRK_CTX_HEAD, ctx, ctx_link);
	return MI_SUCCESS;
}

/*
**  MI_LIST_DEL_CTX -- remove session from linked list when finished
**
**	Parameters:
**		ctx -- context structure
**
**	Returns:
**		MI_FAILURE/MI_SUCCESS
*/

static int
mi_list_del_ctx(ctx)
	SMFICTX_PTR ctx;
{
	SM_ASSERT(ctx != NULL);
	if (SM_TAILQ_EMPTY(&WRK_CTX_HEAD))
		return MI_FAILURE;

	SM_TAILQ_REMOVE(&WRK_CTX_HEAD, ctx, ctx_link);
	return MI_SUCCESS;
}
#endif /* _FFR_WORKERS_POOL */
