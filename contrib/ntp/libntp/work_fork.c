/*
 * work_fork.c - fork implementation for blocking worker child.
 */
#include <config.h>
#include "ntp_workimpl.h"

#ifdef WORK_FORK
#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>

#include "iosignal.h"
#include "ntp_stdlib.h"
#include "ntp_malloc.h"
#include "ntp_syslog.h"
#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_assert.h"
#include "ntp_unixtime.h"
#include "ntp_worker.h"

/* === variables === */
	int			worker_process;
	addremove_io_fd_func	addremove_io_fd;
static	volatile int		worker_sighup_received;
int	saved_argc = 0;
char	**saved_argv;

/* === function prototypes === */
static	void		fork_blocking_child(blocking_child *);
static	RETSIGTYPE	worker_sighup(int);
static	void		send_worker_home_atexit(void);
static	void		cleanup_after_child(blocking_child *);

/* === I/O helpers === */
/* Since we have signals enabled, there's a good chance that blocking IO
 * via pipe suffers from EINTR -- and this goes for both directions.
 * The next two wrappers will loop until either all the data is written
 * or read, plus handling the EOF condition on read. They may return
 * zero if no data was transferred at all, and effectively every return
 * value that differs from the given transfer length signifies an error
 * condition.
 */

static size_t
netread(
	int		fd,
	void *		vb,
	size_t		l
	)
{
	char *		b = vb;
	ssize_t		r;

	while (l) {
		r = read(fd, b, l);
		if (r > 0) {
			l -= r;
			b += r;
		} else if (r == 0 || errno != EINTR) {
			l = 0;
		}
	}
	return (size_t)(b - (char *)vb);
}


static size_t
netwrite(
	int		fd,
	const void *	vb,
	size_t		l
	)
{
	const char *	b = vb;
	ssize_t		w;

	while (l) {
		w = write(fd, b, l);
		if (w > 0) {
			l -= w;
			b += w;
		} else if (errno != EINTR) {
			l = 0;
		}
	}
	return (size_t)(b - (const char *)vb);
}


#if defined(HAVE_DROPROOT)
extern int set_user_group_ids(void);
#endif

/* === functions === */
/*
 * exit_worker()
 *
 * On some systems _exit() is preferred to exit() for forked children.
 * For example, http://netbsd.gw.com/cgi-bin/man-cgi?fork++NetBSD-5.0
 * recommends _exit() to avoid double-flushing C runtime stream buffers
 * and also to avoid calling the parent's atexit() routines in the
 * child.  On those systems WORKER_CHILD_EXIT is _exit.  Since _exit
 * bypasses CRT cleanup, fflush() files we know might have output
 * buffered.
 */
void
exit_worker(
	int	exitcode
	)
{
	if (syslog_file != NULL)
		fflush(syslog_file);
	fflush(stdout);
	fflush(stderr);
	WORKER_CHILD_EXIT (exitcode);	/* space before ( required */
}


static RETSIGTYPE
worker_sighup(
	int sig
	)
{
	if (SIGHUP == sig)
		worker_sighup_received = 1;
}


int
worker_sleep(
	blocking_child *	c,
	time_t			seconds
	)
{
	u_int sleep_remain;

	sleep_remain = (u_int)seconds;
	do {
		if (!worker_sighup_received)
			sleep_remain = sleep(sleep_remain);
		if (worker_sighup_received) {
			TRACE(1, ("worker SIGHUP with %us left to sleep",
				  sleep_remain));
			worker_sighup_received = 0;
			return -1;
		}
	} while (sleep_remain);

	return 0;
}


void
interrupt_worker_sleep(void)
{
	u_int			idx;
	blocking_child *	c;
	int			rc;

	for (idx = 0; idx < blocking_children_alloc; idx++) {
		c = blocking_children[idx];

		if (NULL == c || c->reusable == TRUE)
			continue;

		rc = kill(c->pid, SIGHUP);
		if (rc < 0)
			msyslog(LOG_ERR,
				"Unable to signal HUP to wake child pid %d: %m",
				c->pid);
	}
}


/*
 * harvest_child_status() runs in the parent.
 *
 * Note the error handling -- this is an interaction with SIGCHLD.
 * SIG_IGN on SIGCHLD on some OSes means do not wait but reap
 * automatically. Since we're not really interested in the result code,
 * we simply ignore the error.
 */
static void
harvest_child_status(
	blocking_child *	c
	)
{
	if (c->pid) {
		/* Wait on the child so it can finish terminating */
		if (waitpid(c->pid, NULL, 0) == c->pid)
			TRACE(4, ("harvested child %d\n", c->pid));
		else if (errno != ECHILD)
			msyslog(LOG_ERR, "error waiting on child %d: %m", c->pid);
		c->pid = 0;
	}
}

/*
 * req_child_exit() runs in the parent.
 */
int
req_child_exit(
	blocking_child *	c
	)
{
	if (-1 != c->req_write_pipe) {
		close(c->req_write_pipe);
		c->req_write_pipe = -1;
		return 0;
	}
	/* Closing the pipe forces the child to exit */
	harvest_child_status(c);
	return -1;
}


/*
 * cleanup_after_child() runs in parent.
 */
static void
cleanup_after_child(
	blocking_child *	c
	)
{
	harvest_child_status(c);
	if (-1 != c->resp_read_pipe) {
		(*addremove_io_fd)(c->resp_read_pipe, c->ispipe, TRUE);
		close(c->resp_read_pipe);
		c->resp_read_pipe = -1;
	}
	c->resp_read_ctx = NULL;
	DEBUG_INSIST(-1 == c->req_read_pipe);
	DEBUG_INSIST(-1 == c->resp_write_pipe);
	c->reusable = TRUE;
}


static void
send_worker_home_atexit(void)
{
	u_int			idx;
	blocking_child *	c;

	if (worker_process)
		return;

	for (idx = 0; idx < blocking_children_alloc; idx++) {
		c = blocking_children[idx];
		if (NULL == c)
			continue;
		req_child_exit(c);
	}
}


int
send_blocking_req_internal(
	blocking_child *	c,
	blocking_pipe_header *	hdr,
	void *			data
	)
{
	size_t	octets;
	size_t	rc;

	DEBUG_REQUIRE(hdr != NULL);
	DEBUG_REQUIRE(data != NULL);
	DEBUG_REQUIRE(BLOCKING_REQ_MAGIC == hdr->magic_sig);

	if (-1 == c->req_write_pipe) {
		fork_blocking_child(c);
		DEBUG_INSIST(-1 != c->req_write_pipe);
	}

	octets = sizeof(*hdr);
	rc = netwrite(c->req_write_pipe, hdr, octets);

	if (rc == octets) {
		octets = hdr->octets - sizeof(*hdr);
		rc = netwrite(c->req_write_pipe, data, octets);
		if (rc == octets)
			return 0;
	}

	msyslog(LOG_ERR,
		"send_blocking_req_internal: short write (%zu of %zu), %m",
		rc, octets);

	/* Fatal error.  Clean up the child process.  */
	req_child_exit(c);
	exit(1);	/* otherwise would be return -1 */
}


blocking_pipe_header *
receive_blocking_req_internal(
	blocking_child *	c
	)
{
	blocking_pipe_header	hdr;
	blocking_pipe_header *	req;
	size_t			rc;
	size_t			octets;

	DEBUG_REQUIRE(-1 != c->req_read_pipe);

	req = NULL;
	rc = netread(c->req_read_pipe, &hdr, sizeof(hdr));

	if (0 == rc) {
		TRACE(4, ("parent closed request pipe, child %d terminating\n",
			  c->pid));
	} else if (rc != sizeof(hdr)) {
		msyslog(LOG_ERR,
			"receive_blocking_req_internal: short header read (%zu of %zu), %m",
			rc, sizeof(hdr));
	} else {
		INSIST(sizeof(hdr) < hdr.octets && hdr.octets < 4 * 1024);
		req = emalloc(hdr.octets);
		memcpy(req, &hdr, sizeof(*req));
		octets = hdr.octets - sizeof(hdr);
		rc = netread(c->req_read_pipe, (char *)(req + 1),
			     octets);

		if (rc != octets)
			msyslog(LOG_ERR,
				"receive_blocking_req_internal: short read (%zu of %zu), %m",
				rc, octets);
		else if (BLOCKING_REQ_MAGIC != req->magic_sig)
			msyslog(LOG_ERR,
				"receive_blocking_req_internal: packet header mismatch (0x%x)",
				req->magic_sig);
		else
			return req;
	}

	if (req != NULL)
		free(req);

	return NULL;
}


int
send_blocking_resp_internal(
	blocking_child *	c,
	blocking_pipe_header *	resp
	)
{
	size_t	octets;
	size_t	rc;

	DEBUG_REQUIRE(-1 != c->resp_write_pipe);

	octets = resp->octets;
	rc = netwrite(c->resp_write_pipe, resp, octets);
	free(resp);

	if (octets == rc)
		return 0;

	TRACE(1, ("send_blocking_resp_internal: short write (%zu of %zu), %m\n",
		  rc, octets));
	return -1;
}


blocking_pipe_header *
receive_blocking_resp_internal(
	blocking_child *	c
	)
{
	blocking_pipe_header	hdr;
	blocking_pipe_header *	resp;
	size_t			rc;
	size_t			octets;

	DEBUG_REQUIRE(c->resp_read_pipe != -1);

	resp = NULL;
	rc = netread(c->resp_read_pipe, &hdr, sizeof(hdr));

	if (0 == rc) {
		/* this is the normal child exited indication */
	} else if (rc != sizeof(hdr)) {
		TRACE(1, ("receive_blocking_resp_internal: short header read (%zu of %zu), %m\n",
			  rc, sizeof(hdr)));
	} else if (BLOCKING_RESP_MAGIC != hdr.magic_sig) {
		TRACE(1, ("receive_blocking_resp_internal: header mismatch (0x%x)\n",
			  hdr.magic_sig));
	} else {
		INSIST(sizeof(hdr) < hdr.octets &&
		       hdr.octets < 16 * 1024);
		resp = emalloc(hdr.octets);
		memcpy(resp, &hdr, sizeof(*resp));
		octets = hdr.octets - sizeof(hdr);
		rc = netread(c->resp_read_pipe, (char *)(resp + 1),
			     octets);

		if (rc != octets)
			TRACE(1, ("receive_blocking_resp_internal: short read (%zu of %zu), %m\n",
				  rc, octets));
		else
			return resp;
	}

	cleanup_after_child(c);
	
	if (resp != NULL)
		free(resp);
	
	return NULL;
}


#if defined(HAVE_DROPROOT) && defined(WORK_FORK)
void
fork_deferred_worker(void)
{
	u_int			idx;
	blocking_child *	c;

	REQUIRE(droproot && root_dropped);

	for (idx = 0; idx < blocking_children_alloc; idx++) {
		c = blocking_children[idx];
		if (NULL == c)
			continue;
		if (-1 != c->req_write_pipe && 0 == c->pid)
			fork_blocking_child(c);
	}
}
#endif


static void
fork_blocking_child(
	blocking_child *	c
	)
{
	static int	atexit_installed;
	static int	blocking_pipes[4] = { -1, -1, -1, -1 };
	int		rc;
	int		was_pipe;
	int		is_pipe;
	int		saved_errno = 0;
	int		childpid;
	int		keep_fd;
	int		fd;

	/*
	 * parent and child communicate via a pair of pipes.
	 * 
	 * 0 child read request
	 * 1 parent write request
	 * 2 parent read response
	 * 3 child write response
	 */
	if (-1 == c->req_write_pipe) {
		rc = pipe_socketpair(&blocking_pipes[0], &was_pipe);
		if (0 != rc) {
			saved_errno = errno;
		} else {
			rc = pipe_socketpair(&blocking_pipes[2], &is_pipe);
			if (0 != rc) {
				saved_errno = errno;
				close(blocking_pipes[0]);
				close(blocking_pipes[1]);
			} else {
				INSIST(was_pipe == is_pipe);
			}
		}
		if (0 != rc) {
			errno = saved_errno;
			msyslog(LOG_ERR, "unable to create worker pipes: %m");
			exit(1);
		}

		/*
		 * Move the descriptors the parent will keep open out of the
		 * low descriptors preferred by C runtime buffered FILE *.
		 */
		c->req_write_pipe = move_fd(blocking_pipes[1]);
		c->resp_read_pipe = move_fd(blocking_pipes[2]);
		/*
		 * wake any worker child on orderly shutdown of the
		 * daemon so that it can notice the broken pipes and
		 * go away promptly.
		 */
		if (!atexit_installed) {
			atexit(&send_worker_home_atexit);
			atexit_installed = TRUE;
		}
	}

#if defined(HAVE_DROPROOT) && !defined(NEED_EARLY_FORK)
	/* defer the fork until after root is dropped */
	if (droproot && !root_dropped)
		return;
#endif
	if (syslog_file != NULL)
		fflush(syslog_file);
	fflush(stdout);
	fflush(stderr);

	/* [BUG 3050] setting SIGCHLD to SIG_IGN likely causes unwanted
	 * or undefined effects. We don't do it and leave SIGCHLD alone.
	 */
	/* signal_no_reset(SIGCHLD, SIG_IGN); */

	childpid = fork();
	if (-1 == childpid) {
		msyslog(LOG_ERR, "unable to fork worker: %m");
		exit(1);
	}

	if (childpid) {
		/* this is the parent */
		TRACE(1, ("forked worker child (pid %d)\n", childpid));
		c->pid = childpid;
		c->ispipe = is_pipe;

		/* close the child's pipe descriptors. */
		close(blocking_pipes[0]);
		close(blocking_pipes[3]);

		memset(blocking_pipes, -1, sizeof(blocking_pipes));

		/* wire into I/O loop */
		(*addremove_io_fd)(c->resp_read_pipe, is_pipe, FALSE);

		return;		/* parent returns */
	}

	/*
	 * The parent gets the child pid as the return value of fork().
	 * The child must work for it.
	 */
	c->pid = getpid();
	worker_process = TRUE;

	/*
	 * Change the process name of the child to avoid confusion
	 * about ntpd trunning twice.
	 */
	if (saved_argc != 0) {
		int argcc;
		int argvlen = 0;
		/* Clear argv */
		for (argcc = 0; argcc < saved_argc; argcc++) {
			int l = strlen(saved_argv[argcc]);
			argvlen += l + 1;
			memset(saved_argv[argcc], 0, l);
		}
		strlcpy(saved_argv[0], "ntpd: asynchronous dns resolver", argvlen);
	}

	/*
	 * In the child, close all files except stdin, stdout, stderr,
	 * and the two child ends of the pipes.
	 */
	DEBUG_INSIST(-1 == c->req_read_pipe);
	DEBUG_INSIST(-1 == c->resp_write_pipe);
	c->req_read_pipe = blocking_pipes[0];
	c->resp_write_pipe = blocking_pipes[3];

	kill_asyncio(0);
	closelog();
	if (syslog_file != NULL) {
		fclose(syslog_file);
		syslog_file = NULL;
		syslogit = TRUE;
	}
	keep_fd = max(c->req_read_pipe, c->resp_write_pipe);
	for (fd = 3; fd < keep_fd; fd++)
		if (fd != c->req_read_pipe && 
		    fd != c->resp_write_pipe)
			close(fd);
	close_all_beyond(keep_fd);
	/*
	 * We get signals from refclock serial I/O on NetBSD in the
	 * worker if we do not reset SIGIO's handler to the default.
	 * It is not conditionalized for NetBSD alone because on
	 * systems where it is not needed, it is harmless, and that
	 * allows us to handle unknown others with NetBSD behavior.
	 * [Bug 1386]
	 */
#if defined(USE_SIGIO)
	signal_no_reset(SIGIO, SIG_DFL);
#elif defined(USE_SIGPOLL)
	signal_no_reset(SIGPOLL, SIG_DFL);
#endif
	signal_no_reset(SIGHUP, worker_sighup);
	init_logging("ntp_intres", 0, FALSE);
	setup_logfile(NULL);

#ifdef HAVE_DROPROOT
	(void) set_user_group_ids();
#endif

	/*
	 * And now back to the portable code
	 */
	exit_worker(blocking_child_common(c));
}


void worker_global_lock(int inOrOut)
{
	(void)inOrOut;
}

#else	/* !WORK_FORK follows */
char work_fork_nonempty_compilation_unit;
#endif
