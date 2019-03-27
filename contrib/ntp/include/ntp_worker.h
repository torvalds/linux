/*
 * ntp_worker.h
 */

#ifndef NTP_WORKER_H
#define NTP_WORKER_H

#include "ntp_workimpl.h"

#ifdef WORKER
# if defined(WORK_THREAD) && defined(WORK_PIPE)
#  ifdef HAVE_SEMAPHORE_H
#   include <semaphore.h>
#  endif
# endif
#include "ntp_stdlib.h"

/* #define TEST_BLOCKING_WORKER */	/* ntp_config.c ntp_intres.c */

typedef enum blocking_work_req_tag {
	BLOCKING_GETNAMEINFO,
	BLOCKING_GETADDRINFO,
} blocking_work_req;

typedef void (*blocking_work_callback)(blocking_work_req, void *, size_t, void *);

typedef enum blocking_magic_sig_e {
	BLOCKING_REQ_MAGIC  = 0x510c7ecf,
	BLOCKING_RESP_MAGIC = 0x510c7e54,
} blocking_magic_sig;

/*
 * The same header is used for both requests to and responses from
 * the child.  In the child, done_func and context are opaque.
 */
typedef struct blocking_pipe_header_tag {
	size_t			octets;
	blocking_magic_sig	magic_sig;
	blocking_work_req	rtype;
	u_int			child_idx;
	blocking_work_callback	done_func;
	void *			context;
} blocking_pipe_header;

# ifdef WORK_THREAD
#  ifdef SYS_WINNT
typedef struct { HANDLE thnd; } thread_type;
typedef struct { HANDLE shnd; } sema_type;
#  else
typedef pthread_t	thread_type;
typedef sem_t		sema_type;
#  endif
typedef thread_type	*thr_ref;
typedef sema_type	*sem_ref;
# endif

/*
 *
 */
#if defined(WORK_FORK)

typedef struct blocking_child_tag {
	int		reusable;
	int		pid;
	int		req_write_pipe;		/* parent */
	int		resp_read_pipe;
	void *		resp_read_ctx;
	int		req_read_pipe;		/* child */
	int		resp_write_pipe;
	int		ispipe;	
	volatile u_int	resp_ready_seen;	/* signal/scan */
	volatile u_int	resp_ready_done;	/* consumer/mainloop */
} blocking_child;

#elif defined(WORK_THREAD)

typedef struct blocking_child_tag {
	/*
	 * blocking workitems and blocking_responses are
	 * dynamically-sized one-dimensional arrays of pointers to
	 * blocking worker requests and responses.
	 *
	 * IMPORTANT: This structure is shared between threads, and all
	 * access that is not atomic (especially queue operations) must
	 * hold the 'accesslock' semaphore to avoid data races.
	 *
	 * The resource management (thread/semaphore
	 * creation/destruction) functions and functions just testing a
	 * handle are safe because these are only changed by the main
	 * thread when no worker is running on the same data structure.
	 */
	int			reusable;
	sem_ref			accesslock;	/* shared access lock */
	thr_ref			thread_ref;	/* thread 'handle' */

	/* the reuest queue */
	blocking_pipe_header ** volatile
				workitems;
	volatile size_t		workitems_alloc;
	size_t			head_workitem;		/* parent */
	size_t			tail_workitem;		/* child */
	sem_ref			workitems_pending;	/* signalling */

	/* the response queue */
	blocking_pipe_header ** volatile
				responses;
	volatile size_t		responses_alloc;
	size_t			head_response;		/* child */
	size_t			tail_response;		/* parent */

	/* event handles / sem_t pointers */
	sem_ref			wake_scheduled_sleep;

	/* some systems use a pipe for notification, others a semaphore.
	 * Both employ the queue above for the actual data transfer.
	 */
#ifdef WORK_PIPE
	int			resp_read_pipe;		/* parent */
	int			resp_write_pipe;	/* child */
	int			ispipe;
	void *			resp_read_ctx;		/* child */
#else
	sem_ref			responses_pending;	/* signalling */
#endif
	volatile u_int		resp_ready_seen;	/* signal/scan */
	volatile u_int		resp_ready_done;	/* consumer/mainloop */
	sema_type		sem_table[4];
	thread_type		thr_table[1];
} blocking_child;

#endif	/* WORK_THREAD */

/* we need some global tag to indicate any blocking child may be ready: */
extern volatile u_int		blocking_child_ready_seen;/* signal/scan */
extern volatile u_int		blocking_child_ready_done;/* consumer/mainloop */

extern	blocking_child **	blocking_children;
extern	size_t			blocking_children_alloc;
extern	int			worker_per_query;	/* boolean */
extern	int			intres_req_pending;

extern	u_int	available_blocking_child_slot(void);
extern	int	queue_blocking_request(blocking_work_req, void *,
				       size_t, blocking_work_callback,
				       void *);
extern	int	queue_blocking_response(blocking_child *,
					blocking_pipe_header *, size_t,
					const blocking_pipe_header *);
extern	void	process_blocking_resp(blocking_child *);
extern	void	harvest_blocking_responses(void);
extern	int	send_blocking_req_internal(blocking_child *,
					   blocking_pipe_header *,
					   void *);
extern	int	send_blocking_resp_internal(blocking_child *,
					    blocking_pipe_header *);
extern	blocking_pipe_header *
		receive_blocking_req_internal(blocking_child *);
extern	blocking_pipe_header *
		receive_blocking_resp_internal(blocking_child *);
extern	int	blocking_child_common(blocking_child *);
extern	void	exit_worker(int)
			__attribute__ ((__noreturn__));
extern	int	worker_sleep(blocking_child *, time_t);
extern	void	worker_idle_timer_fired(void);
extern	void	interrupt_worker_sleep(void);
extern	int	req_child_exit(blocking_child *);
#ifndef HAVE_IO_COMPLETION_PORT
extern	int	pipe_socketpair(int fds[2], int *is_pipe);
extern	void	close_all_beyond(int);
extern	void	close_all_except(int);
extern	void	kill_asyncio	(int);
#endif

extern void worker_global_lock(int inOrOut);

# ifdef WORK_PIPE
typedef	void	(*addremove_io_fd_func)(int, int, int);
extern	addremove_io_fd_func		addremove_io_fd;
# else
extern	void	handle_blocking_resp_sem(void *);
typedef	void	(*addremove_io_semaphore_func)(sem_ref, int);
extern	addremove_io_semaphore_func	addremove_io_semaphore;
# endif

# ifdef WORK_FORK
extern	int				worker_process;
# endif

#endif	/* WORKER */

#if defined(HAVE_DROPROOT) && defined(WORK_FORK)
extern void	fork_deferred_worker(void);
#else
# define	fork_deferred_worker()	do {} while (0)
#endif

#endif	/* !NTP_WORKER_H */
