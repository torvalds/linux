/*
 * work_thread.c - threads implementation for blocking worker child.
 */
#include <config.h>
#include "ntp_workimpl.h"

#ifdef WORK_THREAD

#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#ifndef SYS_WINNT
#include <pthread.h>
#endif

#include "ntp_stdlib.h"
#include "ntp_malloc.h"
#include "ntp_syslog.h"
#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_assert.h"
#include "ntp_unixtime.h"
#include "timespecops.h"
#include "ntp_worker.h"

#define CHILD_EXIT_REQ	((blocking_pipe_header *)(intptr_t)-1)
#define CHILD_GONE_RESP	CHILD_EXIT_REQ
/* Queue size increments:
 * The request queue grows a bit faster than the response queue -- the
 * daemon can push requests and pull results faster on avarage than the
 * worker can process requests and push results...  If this really pays
 * off is debatable.
 */
#define WORKITEMS_ALLOC_INC	16
#define RESPONSES_ALLOC_INC	4

/* Fiddle with min/max stack sizes. 64kB minimum seems to work, so we
 * set the maximum to 256kB. If the minimum goes below the
 * system-defined minimum stack size, we have to adjust accordingly.
 */
#ifndef THREAD_MINSTACKSIZE
# define THREAD_MINSTACKSIZE	(64U * 1024)
#endif
#ifndef __sun
#if defined(PTHREAD_STACK_MIN) && THREAD_MINSTACKSIZE < PTHREAD_STACK_MIN
# undef THREAD_MINSTACKSIZE
# define THREAD_MINSTACKSIZE PTHREAD_STACK_MIN
#endif
#endif

#ifndef THREAD_MAXSTACKSIZE
# define THREAD_MAXSTACKSIZE	(256U * 1024)
#endif
#if THREAD_MAXSTACKSIZE < THREAD_MINSTACKSIZE
# undef  THREAD_MAXSTACKSIZE
# define THREAD_MAXSTACKSIZE THREAD_MINSTACKSIZE
#endif

/* need a good integer to store a pointer... */
#ifndef UINTPTR_T
# if defined(UINTPTR_MAX)
#  define UINTPTR_T uintptr_t
# elif defined(UINT_PTR)
#  define UINTPTR_T UINT_PTR
# else
#  define UINTPTR_T size_t
# endif
#endif


#ifdef SYS_WINNT

# define thread_exit(c)	_endthreadex(c)
# define tickle_sem(sh) ReleaseSemaphore((sh->shnd), 1, NULL)
u_int	WINAPI	blocking_thread(void *);
static BOOL	same_os_sema(const sem_ref obj, void * osobj);

#else

# define thread_exit(c)	pthread_exit((void*)(UINTPTR_T)(c))
# define tickle_sem	sem_post
void *		blocking_thread(void *);
static	void	block_thread_signals(sigset_t *);

#endif

#ifdef WORK_PIPE
addremove_io_fd_func		addremove_io_fd;
#else
addremove_io_semaphore_func	addremove_io_semaphore;
#endif

static	void	start_blocking_thread(blocking_child *);
static	void	start_blocking_thread_internal(blocking_child *);
static	void	prepare_child_sems(blocking_child *);
static	int	wait_for_sem(sem_ref, struct timespec *);
static	int	ensure_workitems_empty_slot(blocking_child *);
static	int	ensure_workresp_empty_slot(blocking_child *);
static	int	queue_req_pointer(blocking_child *, blocking_pipe_header *);
static	void	cleanup_after_child(blocking_child *);

static sema_type worker_mmutex;
static sem_ref   worker_memlock;

/* --------------------------------------------------------------------
 * locking the global worker state table (and other global stuff)
 */
void
worker_global_lock(
	int inOrOut)
{
	if (worker_memlock) {
		if (inOrOut)
			wait_for_sem(worker_memlock, NULL);
		else
			tickle_sem(worker_memlock);
	}
}

/* --------------------------------------------------------------------
 * implementation isolation wrapper
 */
void
exit_worker(
	int	exitcode
	)
{
	thread_exit(exitcode);	/* see #define thread_exit */
}

/* --------------------------------------------------------------------
 * sleep for a given time or until the wakup semaphore is tickled.
 */
int
worker_sleep(
	blocking_child *	c,
	time_t			seconds
	)
{
	struct timespec	until;
	int		rc;

# ifdef HAVE_CLOCK_GETTIME
	if (0 != clock_gettime(CLOCK_REALTIME, &until)) {
		msyslog(LOG_ERR, "worker_sleep: clock_gettime() failed: %m");
		return -1;
	}
# else
	if (0 != getclock(TIMEOFDAY, &until)) {
		msyslog(LOG_ERR, "worker_sleep: getclock() failed: %m");
		return -1;
	}
# endif
	until.tv_sec += seconds;
	rc = wait_for_sem(c->wake_scheduled_sleep, &until);
	if (0 == rc)
		return -1;
	if (-1 == rc && ETIMEDOUT == errno)
		return 0;
	msyslog(LOG_ERR, "worker_sleep: sem_timedwait: %m");
	return -1;
}


/* --------------------------------------------------------------------
 * Wake up a worker that takes a nap.
 */
void
interrupt_worker_sleep(void)
{
	u_int			idx;
	blocking_child *	c;

	for (idx = 0; idx < blocking_children_alloc; idx++) {
		c = blocking_children[idx];
		if (NULL == c || NULL == c->wake_scheduled_sleep)
			continue;
		tickle_sem(c->wake_scheduled_sleep);
	}
}

/* --------------------------------------------------------------------
 * Make sure there is an empty slot at the head of the request
 * queue. Tell if the queue is currently empty.
 */
static int
ensure_workitems_empty_slot(
	blocking_child *c
	)
{
	/*
	** !!! PRECONDITION: caller holds access lock!
	**
	** This simply tries to increase the size of the buffer if it
	** becomes full. The resize operation does *not* maintain the
	** order of requests, but that should be irrelevant since the
	** processing is considered asynchronous anyway.
	**
	** Return if the buffer is currently empty.
	*/
	
	static const size_t each =
	    sizeof(blocking_children[0]->workitems[0]);

	size_t	new_alloc;
	size_t  slots_used;
	size_t	sidx;

	slots_used = c->head_workitem - c->tail_workitem;
	if (slots_used >= c->workitems_alloc) {
		new_alloc  = c->workitems_alloc + WORKITEMS_ALLOC_INC;
		c->workitems = erealloc(c->workitems, new_alloc * each);
		for (sidx = c->workitems_alloc; sidx < new_alloc; ++sidx)
		    c->workitems[sidx] = NULL;
		c->tail_workitem   = 0;
		c->head_workitem   = c->workitems_alloc;
		c->workitems_alloc = new_alloc;
	}
	INSIST(NULL == c->workitems[c->head_workitem % c->workitems_alloc]);
	return (0 == slots_used);
}

/* --------------------------------------------------------------------
 * Make sure there is an empty slot at the head of the response
 * queue. Tell if the queue is currently empty.
 */
static int
ensure_workresp_empty_slot(
	blocking_child *c
	)
{
	/*
	** !!! PRECONDITION: caller holds access lock!
	**
	** Works like the companion function above.
	*/
	
	static const size_t each =
	    sizeof(blocking_children[0]->responses[0]);

	size_t	new_alloc;
	size_t  slots_used;
	size_t	sidx;

	slots_used = c->head_response - c->tail_response;
	if (slots_used >= c->responses_alloc) {
		new_alloc  = c->responses_alloc + RESPONSES_ALLOC_INC;
		c->responses = erealloc(c->responses, new_alloc * each);
		for (sidx = c->responses_alloc; sidx < new_alloc; ++sidx)
		    c->responses[sidx] = NULL;
		c->tail_response   = 0;
		c->head_response   = c->responses_alloc;
		c->responses_alloc = new_alloc;
	}
	INSIST(NULL == c->responses[c->head_response % c->responses_alloc]);
	return (0 == slots_used);
}


/* --------------------------------------------------------------------
 * queue_req_pointer() - append a work item or idle exit request to
 *			 blocking_workitems[]. Employ proper locking.
 */
static int
queue_req_pointer(
	blocking_child	*	c,
	blocking_pipe_header *	hdr
	)
{
	size_t qhead;
	
	/* >>>> ACCESS LOCKING STARTS >>>> */
	wait_for_sem(c->accesslock, NULL);
	ensure_workitems_empty_slot(c);
	qhead = c->head_workitem;
	c->workitems[qhead % c->workitems_alloc] = hdr;
	c->head_workitem = 1 + qhead;
	tickle_sem(c->accesslock);
	/* <<<< ACCESS LOCKING ENDS <<<< */

	/* queue consumer wake-up notification */
	tickle_sem(c->workitems_pending);

	return 0;
}

/* --------------------------------------------------------------------
 * API function to make sure a worker is running, a proper private copy
 * of the data is made, the data eneterd into the queue and the worker
 * is signalled.
 */
int
send_blocking_req_internal(
	blocking_child *	c,
	blocking_pipe_header *	hdr,
	void *			data
	)
{
	blocking_pipe_header *	threadcopy;
	size_t			payload_octets;

	REQUIRE(hdr != NULL);
	REQUIRE(data != NULL);
	DEBUG_REQUIRE(BLOCKING_REQ_MAGIC == hdr->magic_sig);

	if (hdr->octets <= sizeof(*hdr))
		return 1;	/* failure */
	payload_octets = hdr->octets - sizeof(*hdr);

	if (NULL == c->thread_ref)
		start_blocking_thread(c);
	threadcopy = emalloc(hdr->octets);
	memcpy(threadcopy, hdr, sizeof(*hdr));
	memcpy((char *)threadcopy + sizeof(*hdr), data, payload_octets);

	return queue_req_pointer(c, threadcopy);
}

/* --------------------------------------------------------------------
 * Wait for the 'incoming queue no longer empty' signal, lock the shared
 * structure and dequeue an item.
 */
blocking_pipe_header *
receive_blocking_req_internal(
	blocking_child *	c
	)
{
	blocking_pipe_header *	req;
	size_t			qhead, qtail;

	req = NULL;
	do {
		/* wait for tickle from the producer side */
		wait_for_sem(c->workitems_pending, NULL);

		/* >>>> ACCESS LOCKING STARTS >>>> */
		wait_for_sem(c->accesslock, NULL);
		qhead = c->head_workitem;
		do {
			qtail = c->tail_workitem;
			if (qhead == qtail)
				break;
			c->tail_workitem = qtail + 1;
			qtail %= c->workitems_alloc;
			req = c->workitems[qtail];
			c->workitems[qtail] = NULL;
		} while (NULL == req);
		tickle_sem(c->accesslock);
		/* <<<< ACCESS LOCKING ENDS <<<< */

	} while (NULL == req);

	INSIST(NULL != req);
	if (CHILD_EXIT_REQ == req) {	/* idled out */
		send_blocking_resp_internal(c, CHILD_GONE_RESP);
		req = NULL;
	}

	return req;
}

/* --------------------------------------------------------------------
 * Push a response into the return queue and eventually tickle the
 * receiver.
 */
int
send_blocking_resp_internal(
	blocking_child *	c,
	blocking_pipe_header *	resp
	)
{
	size_t	qhead;
	int	empty;
	
	/* >>>> ACCESS LOCKING STARTS >>>> */
	wait_for_sem(c->accesslock, NULL);
	empty = ensure_workresp_empty_slot(c);
	qhead = c->head_response;
	c->responses[qhead % c->responses_alloc] = resp;
	c->head_response = 1 + qhead;
	tickle_sem(c->accesslock);
	/* <<<< ACCESS LOCKING ENDS <<<< */

	/* queue consumer wake-up notification */
	if (empty)
	{
#	    ifdef WORK_PIPE
		if (1 != write(c->resp_write_pipe, "", 1))
			msyslog(LOG_WARNING, "async resolver: %s",
				"failed to notify main thread!");
#	    else
		tickle_sem(c->responses_pending);
#	    endif
	}
	return 0;
}


#ifndef WORK_PIPE

/* --------------------------------------------------------------------
 * Check if a (Windows-)hanndle to a semaphore is actually the same we
 * are using inside the sema wrapper.
 */
static BOOL
same_os_sema(
	const sem_ref	obj,
	void*		osh
	)
{
	return obj && osh && (obj->shnd == (HANDLE)osh);
}

/* --------------------------------------------------------------------
 * Find the shared context that associates to an OS handle and make sure
 * the data is dequeued and processed.
 */
void
handle_blocking_resp_sem(
	void *	context
	)
{
	blocking_child *	c;
	u_int			idx;

	c = NULL;
	for (idx = 0; idx < blocking_children_alloc; idx++) {
		c = blocking_children[idx];
		if (c != NULL &&
			c->thread_ref != NULL &&
			same_os_sema(c->responses_pending, context))
			break;
	}
	if (idx < blocking_children_alloc)
		process_blocking_resp(c);
}
#endif	/* !WORK_PIPE */

/* --------------------------------------------------------------------
 * Fetch the next response from the return queue. In case of signalling
 * via pipe, make sure the pipe is flushed, too.
 */
blocking_pipe_header *
receive_blocking_resp_internal(
	blocking_child *	c
	)
{
	blocking_pipe_header *	removed;
	size_t			qhead, qtail, slot;

#ifdef WORK_PIPE
	int			rc;
	char			scratch[32];

	do
		rc = read(c->resp_read_pipe, scratch, sizeof(scratch));
	while (-1 == rc && EINTR == errno);
#endif

	/* >>>> ACCESS LOCKING STARTS >>>> */
	wait_for_sem(c->accesslock, NULL);
	qhead = c->head_response;
	qtail = c->tail_response;
	for (removed = NULL; !removed && (qhead != qtail); ++qtail) {
		slot = qtail % c->responses_alloc;
		removed = c->responses[slot];
		c->responses[slot] = NULL;
	}
	c->tail_response = qtail;
	tickle_sem(c->accesslock);
	/* <<<< ACCESS LOCKING ENDS <<<< */

	if (NULL != removed) {
		DEBUG_ENSURE(CHILD_GONE_RESP == removed ||
			     BLOCKING_RESP_MAGIC == removed->magic_sig);
	}
	if (CHILD_GONE_RESP == removed) {
		cleanup_after_child(c);
		removed = NULL;
	}

	return removed;
}

/* --------------------------------------------------------------------
 * Light up a new worker.
 */
static void
start_blocking_thread(
	blocking_child *	c
	)
{

	DEBUG_INSIST(!c->reusable);

	prepare_child_sems(c);
	start_blocking_thread_internal(c);
}

/* --------------------------------------------------------------------
 * Create a worker thread. There are several differences between POSIX
 * and Windows, of course -- most notably the Windows thread is no
 * detached thread, and we keep the handle around until we want to get
 * rid of the thread. The notification scheme also differs: Windows
 * makes use of semaphores in both directions, POSIX uses a pipe for
 * integration with 'select()' or alike.
 */
static void
start_blocking_thread_internal(
	blocking_child *	c
	)
#ifdef SYS_WINNT
{
	BOOL	resumed;

	c->thread_ref = NULL;
	(*addremove_io_semaphore)(c->responses_pending->shnd, FALSE);
	c->thr_table[0].thnd =
		(HANDLE)_beginthreadex(
			NULL,
			0,
			&blocking_thread,
			c,
			CREATE_SUSPENDED,
			NULL);

	if (NULL == c->thr_table[0].thnd) {
		msyslog(LOG_ERR, "start blocking thread failed: %m");
		exit(-1);
	}
	/* remember the thread priority is only within the process class */
	if (!SetThreadPriority(c->thr_table[0].thnd,
			       THREAD_PRIORITY_BELOW_NORMAL))
		msyslog(LOG_ERR, "Error lowering blocking thread priority: %m");

	resumed = ResumeThread(c->thr_table[0].thnd);
	DEBUG_INSIST(resumed);
	c->thread_ref = &c->thr_table[0];
}
#else	/* pthreads start_blocking_thread_internal() follows */
{
# ifdef NEED_PTHREAD_INIT
	static int	pthread_init_called;
# endif
	pthread_attr_t	thr_attr;
	int		rc;
	int		pipe_ends[2];	/* read then write */
	int		is_pipe;
	int		flags;
	size_t		ostacksize;
	size_t		nstacksize;
	sigset_t	saved_sig_mask;

	c->thread_ref = NULL;

# ifdef NEED_PTHREAD_INIT
	/*
	 * from lib/isc/unix/app.c:
	 * BSDI 3.1 seg faults in pthread_sigmask() if we don't do this.
	 */
	if (!pthread_init_called) {
		pthread_init();
		pthread_init_called = TRUE;
	}
# endif

	rc = pipe_socketpair(&pipe_ends[0], &is_pipe);
	if (0 != rc) {
		msyslog(LOG_ERR, "start_blocking_thread: pipe_socketpair() %m");
		exit(1);
	}
	c->resp_read_pipe = move_fd(pipe_ends[0]);
	c->resp_write_pipe = move_fd(pipe_ends[1]);
	c->ispipe = is_pipe;
	flags = fcntl(c->resp_read_pipe, F_GETFL, 0);
	if (-1 == flags) {
		msyslog(LOG_ERR, "start_blocking_thread: fcntl(F_GETFL) %m");
		exit(1);
	}
	rc = fcntl(c->resp_read_pipe, F_SETFL, O_NONBLOCK | flags);
	if (-1 == rc) {
		msyslog(LOG_ERR,
			"start_blocking_thread: fcntl(F_SETFL, O_NONBLOCK) %m");
		exit(1);
	}
	(*addremove_io_fd)(c->resp_read_pipe, c->ispipe, FALSE);
	pthread_attr_init(&thr_attr);
	pthread_attr_setdetachstate(&thr_attr, PTHREAD_CREATE_DETACHED);
#if defined(HAVE_PTHREAD_ATTR_GETSTACKSIZE) && \
    defined(HAVE_PTHREAD_ATTR_SETSTACKSIZE)
	rc = pthread_attr_getstacksize(&thr_attr, &ostacksize);
	if (0 != rc) {
		msyslog(LOG_ERR,
			"start_blocking_thread: pthread_attr_getstacksize() -> %s",
			strerror(rc));
	} else {
		if (ostacksize < THREAD_MINSTACKSIZE)
			nstacksize = THREAD_MINSTACKSIZE;
		else if (ostacksize > THREAD_MAXSTACKSIZE)
			nstacksize = THREAD_MAXSTACKSIZE;
		else
			nstacksize = ostacksize;
		if (nstacksize != ostacksize)
			rc = pthread_attr_setstacksize(&thr_attr, nstacksize);
		if (0 != rc)
			msyslog(LOG_ERR,
				"start_blocking_thread: pthread_attr_setstacksize(0x%lx -> 0x%lx) -> %s",
				(u_long)ostacksize, (u_long)nstacksize,
				strerror(rc));
	}
#else
	UNUSED_ARG(nstacksize);
	UNUSED_ARG(ostacksize);
#endif
#if defined(PTHREAD_SCOPE_SYSTEM) && defined(NEED_PTHREAD_SCOPE_SYSTEM)
	pthread_attr_setscope(&thr_attr, PTHREAD_SCOPE_SYSTEM);
#endif
	c->thread_ref = emalloc_zero(sizeof(*c->thread_ref));
	block_thread_signals(&saved_sig_mask);
	rc = pthread_create(&c->thr_table[0], &thr_attr,
			    &blocking_thread, c);
	pthread_sigmask(SIG_SETMASK, &saved_sig_mask, NULL);
	pthread_attr_destroy(&thr_attr);
	if (0 != rc) {
		msyslog(LOG_ERR, "start_blocking_thread: pthread_create() -> %s",
			strerror(rc));
		exit(1);
	}
	c->thread_ref = &c->thr_table[0];
}
#endif

/* --------------------------------------------------------------------
 * block_thread_signals()
 *
 * Temporarily block signals used by ntpd main thread, so that signal
 * mask inherited by child threads leaves them blocked.  Returns prior
 * active signal mask via pmask, to be restored by the main thread
 * after pthread_create().
 */
#ifndef SYS_WINNT
void
block_thread_signals(
	sigset_t *	pmask
	)
{
	sigset_t	block;

	sigemptyset(&block);
# ifdef HAVE_SIGNALED_IO
#  ifdef SIGIO
	sigaddset(&block, SIGIO);
#  endif
#  ifdef SIGPOLL
	sigaddset(&block, SIGPOLL);
#  endif
# endif	/* HAVE_SIGNALED_IO */
	sigaddset(&block, SIGALRM);
	sigaddset(&block, MOREDEBUGSIG);
	sigaddset(&block, LESSDEBUGSIG);
# ifdef SIGDIE1
	sigaddset(&block, SIGDIE1);
# endif
# ifdef SIGDIE2
	sigaddset(&block, SIGDIE2);
# endif
# ifdef SIGDIE3
	sigaddset(&block, SIGDIE3);
# endif
# ifdef SIGDIE4
	sigaddset(&block, SIGDIE4);
# endif
# ifdef SIGBUS
	sigaddset(&block, SIGBUS);
# endif
	sigemptyset(pmask);
	pthread_sigmask(SIG_BLOCK, &block, pmask);
}
#endif	/* !SYS_WINNT */


/* --------------------------------------------------------------------
 * Create & destroy semaphores. This is sufficiently different between
 * POSIX and Windows to warrant wrapper functions and close enough to
 * use the concept of synchronization via semaphore for all platforms.
 */
static sem_ref
create_sema(
	sema_type*	semptr,
	u_int		inival,
	u_int		maxval)
{
#ifdef SYS_WINNT
	
	long svini, svmax;
	if (NULL != semptr) {
		svini = (inival < LONG_MAX)
		    ? (long)inival : LONG_MAX;
		svmax = (maxval < LONG_MAX && maxval > 0)
		    ? (long)maxval : LONG_MAX;
		semptr->shnd = CreateSemaphore(NULL, svini, svmax, NULL);
		if (NULL == semptr->shnd)
			semptr = NULL;
	}
	
#else
	
	(void)maxval;
	if (semptr && sem_init(semptr, FALSE, inival))
		semptr = NULL;
	
#endif

	return semptr;
}

/* ------------------------------------------------------------------ */
static sem_ref
delete_sema(
	sem_ref obj)
{
		
#   ifdef SYS_WINNT
		
	if (obj) {
		if (obj->shnd)
			CloseHandle(obj->shnd);
		obj->shnd = NULL;
	}
	
#   else
		
	if (obj)
		sem_destroy(obj);
		
#   endif

	return NULL;
}

/* --------------------------------------------------------------------
 * prepare_child_sems()
 *
 * create sync & access semaphores
 *
 * All semaphores are cleared, only the access semaphore has 1 unit.
 * Childs wait on 'workitems_pending', then grabs 'sema_access'
 * and dequeues jobs. When done, 'sema_access' is given one unit back.
 *
 * The producer grabs 'sema_access', manages the queue, restores
 * 'sema_access' and puts one unit into 'workitems_pending'.
 *
 * The story goes the same for the response queue.
 */
static void
prepare_child_sems(
	blocking_child *c
	)
{
	if (NULL == worker_memlock)
		worker_memlock = create_sema(&worker_mmutex, 1, 1);
	
	c->accesslock           = create_sema(&c->sem_table[0], 1, 1);
	c->workitems_pending    = create_sema(&c->sem_table[1], 0, 0);
	c->wake_scheduled_sleep = create_sema(&c->sem_table[2], 0, 1);
#   ifndef WORK_PIPE
	c->responses_pending    = create_sema(&c->sem_table[3], 0, 0);
#   endif
}

/* --------------------------------------------------------------------
 * wait for semaphore. Where the wait can be interrupted, it will
 * internally resume -- When this function returns, there is either no
 * semaphore at all, a timeout occurred, or the caller could
 * successfully take a token from the semaphore.
 *
 * For untimed wait, not checking the result of this function at all is
 * definitely an option.
 */
static int
wait_for_sem(
	sem_ref			sem,
	struct timespec *	timeout		/* wall-clock */
	)
#ifdef SYS_WINNT
{
	struct timespec now;
	struct timespec delta;
	DWORD		msec;
	DWORD		rc;

	if (!(sem && sem->shnd)) {
		errno = EINVAL;
		return -1;
	}
	
	if (NULL == timeout) {
		msec = INFINITE;
	} else {
		getclock(TIMEOFDAY, &now);
		delta = sub_tspec(*timeout, now);
		if (delta.tv_sec < 0) {
			msec = 0;
		} else if ((delta.tv_sec + 1) >= (MAXDWORD / 1000)) {
			msec = INFINITE;
		} else {
			msec = 1000 * (DWORD)delta.tv_sec;
			msec += delta.tv_nsec / (1000 * 1000);
		}
	}
	rc = WaitForSingleObject(sem->shnd, msec);
	if (WAIT_OBJECT_0 == rc)
		return 0;
	if (WAIT_TIMEOUT == rc) {
		errno = ETIMEDOUT;
		return -1;
	}
	msyslog(LOG_ERR, "WaitForSingleObject unexpected 0x%x", rc);
	errno = EFAULT;
	return -1;
}
#else	/* pthreads wait_for_sem() follows */
{
	int rc = -1;

	if (sem) do {
			if (NULL == timeout)
				rc = sem_wait(sem);
			else
				rc = sem_timedwait(sem, timeout);
		} while (rc == -1 && errno == EINTR);
	else
		errno = EINVAL;
		
	return rc;
}
#endif

/* --------------------------------------------------------------------
 * blocking_thread - thread functions have WINAPI (aka 'stdcall')
 * calling conventions under Windows and POSIX-defined signature
 * otherwise.
 */
#ifdef SYS_WINNT
u_int WINAPI
#else
void *
#endif
blocking_thread(
	void *	ThreadArg
	)
{
	blocking_child *c;

	c = ThreadArg;
	exit_worker(blocking_child_common(c));

	/* NOTREACHED */
	return 0;
}

/* --------------------------------------------------------------------
 * req_child_exit() runs in the parent.
 *
 * This function is called from from the idle timer, too, and possibly
 * without a thread being there any longer. Since we have folded up our
 * tent in that case and all the semaphores are already gone, we simply
 * ignore this request in this case.
 *
 * Since the existence of the semaphores is controlled exclusively by
 * the parent, there's no risk of data race here.
 */
int
req_child_exit(
	blocking_child *c
	)
{
	return (c->accesslock)
	    ? queue_req_pointer(c, CHILD_EXIT_REQ)
	    : 0;
}

/* --------------------------------------------------------------------
 * cleanup_after_child() runs in parent.
 */
static void
cleanup_after_child(
	blocking_child *	c
	)
{
	DEBUG_INSIST(!c->reusable);
	
#   ifdef SYS_WINNT
	/* The thread was not created in detached state, so we better
	 * clean up.
	 */
	if (c->thread_ref && c->thread_ref->thnd) {
		WaitForSingleObject(c->thread_ref->thnd, INFINITE);
		INSIST(CloseHandle(c->thread_ref->thnd));
		c->thread_ref->thnd = NULL;
	}
#   endif
	c->thread_ref = NULL;

	/* remove semaphores and (if signalling vi IO) pipes */
	
	c->accesslock           = delete_sema(c->accesslock);
	c->workitems_pending    = delete_sema(c->workitems_pending);
	c->wake_scheduled_sleep = delete_sema(c->wake_scheduled_sleep);

#   ifdef WORK_PIPE
	DEBUG_INSIST(-1 != c->resp_read_pipe);
	DEBUG_INSIST(-1 != c->resp_write_pipe);
	(*addremove_io_fd)(c->resp_read_pipe, c->ispipe, TRUE);
	close(c->resp_write_pipe);
	close(c->resp_read_pipe);
	c->resp_write_pipe = -1;
	c->resp_read_pipe = -1;
#   else
	DEBUG_INSIST(NULL != c->responses_pending);
	(*addremove_io_semaphore)(c->responses_pending->shnd, TRUE);
	c->responses_pending = delete_sema(c->responses_pending);
#   endif

	/* Is it necessary to check if there are pending requests and
	 * responses? If so, and if there are, what to do with them?
	 */
	
	/* re-init buffer index sequencers */
	c->head_workitem = 0;
	c->tail_workitem = 0;
	c->head_response = 0;
	c->tail_response = 0;

	c->reusable = TRUE;
}


#else	/* !WORK_THREAD follows */
char work_thread_nonempty_compilation_unit;
#endif
