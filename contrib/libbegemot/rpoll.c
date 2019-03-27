/*
 * Copyright (c)1996-2002 by Hartmut Brandt
 *	All rights reserved.
 *
 * Author: Hartmut Brandt
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 * 
 * 1. Redistributions of source code or documentation must retain the above
 *   copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE AND DOCUMENTATION IS PROVIDED BY THE AUTHOR 
 * AND ITS CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR OR ITS CONTRIBUTORS  BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * These functions try to hide the poll/select/setitimer interface from the
 * user. You associate callback functions with file descriptors and timers.
 *
 * $Begemot: libbegemot/rpoll.c,v 1.14 2004/09/21 15:59:00 brandt Exp $
 */
# include <stdio.h>
# include <stdlib.h>
# include <stddef.h>
# include <stdarg.h>
# include <signal.h>
# include <string.h>
# include <errno.h>
# include <time.h>
# include <assert.h>
# include <unistd.h>
# include <sys/time.h>

# include "rpoll.h"

/*
# define DEBUG
*/

# ifdef USE_POLL
#  ifdef NEED_POLL_XOPEN_TWIDDLE
#   define __USE_XOPEN
#  endif
#  include <poll.h>
#  ifdef NEED_POLL_XOPEN_TWIDDLE
#   undef __USE_XOPEN
#  endif
#  include <stropts.h>
# endif

/*
 * the second define is for Linux, which sometimes fails to
 * declare INFTIM.
 */
# if defined(USE_SELECT) || !defined(INFTIM)
#  define INFTIM (-1)
# endif

# if defined(SIGPOLL)
#  define SIGNAL	SIGPOLL
# else
#  if defined(SIGIO)
#   define SIGNAL	SIGIO
#  endif
# endif

# ifdef USE_POLL
#  define poll_in	(POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI)
#  define poll_out	(POLLOUT | POLLWRNORM | POLLWRBAND)
#  define poll_except	(POLLERR | POLLHUP)
# endif

# ifdef BROKEN_SELECT_PROTO
#  define SELECT_CAST(P)	(int *)P
# else
#  define SELECT_CAST(P)	P
# endif


typedef int64_t tval_t;

static inline tval_t GETUSECS(void);

static inline tval_t
GETUSECS(void) {
	struct timeval tval;

	(void)gettimeofday(&tval, NULL);
	return (tval_t)tval.tv_sec * 1000000 + tval.tv_usec;
}

/*
 * Simple fatal exit.
 */
static void
_panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "panic: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);

	exit(1);
}

static void *
_xrealloc(void *p, size_t s)
{
	void *ptr;

	if(p == NULL) {
		if((ptr=malloc(s)) == NULL && (s!=0 || (ptr=malloc(1)) == NULL))
			_panic("out of memory: xrealloc(%lx, %lu)",
				(unsigned long)p, (unsigned long)s);
	} else if(s == 0) {
		free(p);
		if((ptr=malloc(s)) == NULL && (ptr=malloc(1)) == NULL)
			_panic("out of memory: xrealloc(%lx, %lu)",
				(unsigned long)p, (unsigned long)s);
	} else {
		if((ptr = realloc(p, s)) == NULL)
			_panic("out of memory: xrealloc(%lx, %lu)",
				(unsigned long)p, (unsigned long)s);
	}

	return ptr;
}

/*
 * This structure holds one registration record for files
 */
typedef struct {
	int	fd;		/* file descriptor (-1 if struct unused) */
	int	mask;		/* event flags */
	void *	arg;		/* client arg */
	poll_f	func;		/* handler */
# ifdef USE_POLL
	struct pollfd *pfd;	/* pointer to corresponding poll() structure */
# endif
} PollReg_t;

/*
 * Now for timers
 */
typedef struct {
	uint64_t usecs;		/* microsecond value of the timer */
	int	repeat;		/* one shot or repeat? */
	void	*arg;		/* client arg */
	timer_f	func;		/* handler, 0 means disfunct */
	tval_t	when;		/* next time to trigger in usecs! */
} PollTim_t;

/* how many records should our table grow at once? */
# define POLL_REG_GROW	100

# ifdef USE_POLL
static struct pollfd *	pfd;		/* fd list for poll() */
# endif

# ifdef USE_SELECT
static fd_set rset, wset, xset;		/* file descriptor sets for select() */
static int maxfd;			/* maximum fd number */
# endif

static int		in_dispatch;

static PollReg_t *	regs;		/* registration records */
static u_int		regs_alloc;	/* how many are allocated */
static u_int		regs_used;	/* upper used limit */
static sigset_t		bset;		/* blocked signals */
static int 		rebuild;	/* rebuild table on next dispatch() */

static int *		tfd;		/* sorted entries */
static u_int		tfd_alloc;	/* number of entries allocated */
static u_int		tfd_used;	/* number of entries used */
static PollTim_t *	tims;		/* timer registration records */
static u_int		tims_alloc;	/* how many are allocated */
static u_int		tims_used;	/* how many are used */
static int		resort;		/* resort on next dispatch */

int	rpoll_trace;
int	rpoll_policy;	/* if 0 start sched callbacks from 0 else try round robin */

static void poll_build(void);
static void poll_blocksig(void);
static void poll_unblocksig(void);
static void sort_timers(void);


/*
 * Private function to block SIGPOLL or SIGIO for a short time.
 * Don't forget to call poll_unblock before return from the calling function.
 * Don't change the mask between this calls (your changes will be lost).
 */
static void
poll_blocksig(void)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SIGNAL);

	if(sigprocmask(SIG_BLOCK, &set, &bset))
		_panic("sigprocmask(SIG_BLOCK): %s", strerror(errno));
}

/*
 * unblock the previously blocked signal
 */
static void
poll_unblocksig(void)
{
	if(sigprocmask(SIG_SETMASK, &bset, NULL))
		_panic("sigprocmask(SIG_SETMASK): %s", strerror(errno));
}
 
/*
 * Register the file descriptor fd. If the event corresponding to
 * mask arrives func is called with arg.
 * If fd is already registered with that func and arg, only the mask
 * is changed.
 * We block the IO-signal, so the dispatch function can be called from
 * within the signal handler.
 */
int
poll_register(int fd, poll_f func, void *arg, int mask)
{
	PollReg_t * p;

	poll_blocksig();

	/* already registered? */
	for(p = regs; p < &regs[regs_alloc]; p++)
		if(p->fd == fd && p->func == func && p->arg == arg) {
			p->mask = mask;
			break;
		}

	if(p == &regs[regs_alloc]) {
		/* no - register */

		/* find a free slot */
		for(p = regs; p < &regs[regs_alloc]; p++)
			if(p->fd == -1)
				break;

		if(p == &regs[regs_alloc]) {
			size_t newsize = regs_alloc + POLL_REG_GROW;
			regs = _xrealloc(regs, sizeof(regs[0]) * newsize);
			for(p = &regs[regs_alloc]; p < &regs[newsize]; p++) {
				p->fd = -1;
# ifdef USE_POLL
				p->pfd = NULL;
# endif
			}
			p = &regs[regs_alloc];
			regs_alloc = newsize;
		}

		p->fd = fd;
		p->arg = arg;
		p->mask = mask;
		p->func = func;

		regs_used++;
		rebuild = 1;
	}

	poll_unblocksig();

	if(rpoll_trace)
		fprintf(stderr, "poll_register(%d, %p, %p, %#x)->%tu",
			fd, (void *)func, (void *)arg, mask, p - regs);
	return p - regs;
}

/*
 * remove registration
 */
void
poll_unregister(int handle)
{
	if(rpoll_trace)
		fprintf(stderr, "poll_unregister(%d)", handle);

	poll_blocksig();

	regs[handle].fd = -1;
# ifdef USE_POLL
	regs[handle].pfd = NULL;
# endif
	rebuild = 1;
	regs_used--;

	poll_unblocksig();
}

/*
 * Build the structures used by poll() or select() 
 */
static void
poll_build(void)
{
	PollReg_t * p;

# ifdef USE_POLL
	struct pollfd * f;

	f = pfd = _xrealloc(pfd, sizeof(pfd[0]) * regs_used);

	for(p = regs; p < &regs[regs_alloc]; p++)
		if(p->fd >= 0) {
			f->fd = p->fd;
			f->events = 0;
			if(p->mask & RPOLL_IN)
				f->events |= poll_in;
			if(p->mask & RPOLL_OUT)
				f->events |= poll_out;
			if(p->mask & RPOLL_EXCEPT)
				f->events |= poll_except;
			f->revents = 0;
			p->pfd = f++;
		}
	assert(f == &pfd[regs_used]);
# endif

# ifdef USE_SELECT
	FD_ZERO(&rset);
	FD_ZERO(&wset);
	FD_ZERO(&xset);
	maxfd = -1;
	for(p = regs; p < &regs[regs_alloc]; p++)
		if(p->fd >= 0) {
			if(p->fd > maxfd)
				maxfd = p->fd;
			if(p->mask & RPOLL_IN)
				FD_SET(p->fd, &rset);
			if(p->mask & RPOLL_OUT)
				FD_SET(p->fd, &wset);
			if(p->mask & RPOLL_EXCEPT)
				FD_SET(p->fd, &xset);
		}
# endif
}

int
poll_start_timer(u_int msecs, int repeat, timer_f func, void *arg)
{
	return (poll_start_utimer((unsigned long long)msecs * 1000,
	    repeat, func, arg));
}

int
poll_start_utimer(unsigned long long usecs, int repeat, timer_f func, void *arg)
{
	PollTim_t *p;

	/* find unused entry */
	for(p = tims; p < &tims[tims_alloc]; p++)
		if(p->func == NULL)
			break;

	if(p == &tims[tims_alloc]) {
		if(tims_alloc == tims_used) {
			size_t newsize = tims_alloc + POLL_REG_GROW;
			tims = _xrealloc(tims, sizeof(tims[0]) * newsize);
			for(p = &tims[tims_alloc]; p < &tims[newsize]; p++)
				p->func = NULL;
			p = &tims[tims_alloc];
			tims_alloc = newsize;
		}
	}

	/* create entry */
	p->usecs = usecs;
	p->repeat = repeat;
	p->arg = arg;
	p->func = func;
	p->when = GETUSECS() + usecs;

	tims_used++;

	resort = 1;

	if(rpoll_trace)
		fprintf(stderr, "poll_start_utimer(%llu, %d, %p, %p)->%tu",
			usecs, repeat, (void *)func, (void *)arg, p - tims);

	return p - tims;
}

/*
 * Here we have to look into the sorted table, whether any entry there points
 * into the registration table for the deleted entry. This is needed,
 * because a unregistration can occure while we are scanning through the
 * table in dispatch(). Do this only, if we are really there - resorting
 * will sort out things if we are called from outside the loop.
 */
void
poll_stop_timer(int handle)
{
	u_int i;

	if(rpoll_trace)
		fprintf(stderr, "poll_stop_timer(%d)", handle);

	tims[handle].func = NULL;
	tims_used--;

	resort = 1;

	if(!in_dispatch)
		return;

	for(i = 0; i < tfd_used; i++)
		if(tfd[i] == handle) {
			tfd[i] = -1;
			break;
		}
}

/*
 * Squeeze and sort timer table.
 * Should perhaps use a custom sort.
 */
static int
tim_cmp(const void *p1, const void *p2)
{
	int t1 = *(const int *)p1;
	int t2 = *(const int *)p2;

	return tims[t1].when < tims[t2].when ? -1
	     : tims[t1].when > tims[t2].when ? +1
	     :                        		  0;
}

/*
 * Reconstruct the tfd-array. This will be an sorted array of indexes
 * to the used entries in tims. The next timer to expire will be infront
 * of the array. tfd_used is the number of used entries. The array is
 * re-allocated if needed.
 */
static void
sort_timers(void)
{
	int *pp;
	u_int i;

	if(tims_used > tfd_alloc) {
		tfd_alloc = tims_used;
		tfd  = _xrealloc(tfd, sizeof(int *) * tfd_alloc);
	}

	pp = tfd;

	for(i = 0; i < tims_alloc; i++)
		if(tims[i].func)
			*pp++ = i;
	assert(pp - tfd == (ptrdiff_t)tims_used);

	tfd_used = tims_used;
	if(tfd_used > 1)
		qsort(tfd, tfd_used, sizeof(int), tim_cmp);
}

/*
 * Poll the file descriptors and dispatch to the right function
 * If wait is true the poll blocks until somewhat happens.
 * Don't use a pointer here, because the called function may cause
 * a reallocation! The check for pfd != NULL is required, because
 * a sequence of unregister/register could make the wrong callback
 * to be called. So we clear pfd in unregister and check here.
 */
void
poll_dispatch(int wait)
{
	u_int i, idx;
	int ret;
	tval_t now;
	tval_t tout;
	static u_int last_index;

# ifdef USE_SELECT
	fd_set nrset, nwset, nxset;
	struct timeval tv;
# endif

	in_dispatch = 1;

	if(rebuild) {
		rebuild = 0;
		poll_build();
	}
	if(resort) {
		resort = 0;
		sort_timers();
	}

	/* in wait mode - compute the timeout */
	if(wait) {
		if(tfd_used) {
			now = GETUSECS();
# ifdef DEBUG
			{
				fprintf(stderr, "now=%llu", now);
				for(i = 0; i < tims_used; i++)
					fprintf(stderr, "timers[%2d] = %lld",
					    i, tfd[i]->when - now);
			}
# endif
			if((tout = tims[tfd[0]].when - now) < 0)
				tout = 0;
		} else
			tout = INFTIM;
	} else
		tout = 0;

# ifdef DEBUG
	fprintf(stderr, "rpoll -- selecting with tout=%u", tout);
# endif

# ifdef USE_POLL
	ret = poll(pfd, regs_used, tout == INFTIM ? INFTIM : (tout / 1000));
# endif

# ifdef USE_SELECT
	nrset = rset;
	nwset = wset;
	nxset = xset;
	if(tout != INFTIM) {
		tv.tv_sec = tout / 1000000;
		tv.tv_usec = tout % 1000000;
	}
	ret = select(maxfd+1,
		SELECT_CAST(&nrset),
		SELECT_CAST(&nwset),
		SELECT_CAST(&nxset), (tout==INFTIM) ? NULL : &tv);
# endif

	if(ret == -1) {
		if(errno == EINTR)
			return;
		_panic("poll/select: %s", strerror(errno));
	}

	/* dispatch files */
	if(ret > 0) {
		for(i = 0; i < regs_alloc; i++) {
			idx = rpoll_policy ? ((last_index+i) % regs_alloc) : i;

			assert(idx < regs_alloc);

			if(regs[idx].fd >= 0) {
				int mask = 0;

# ifdef USE_POLL
				if(regs[idx].pfd) {
					if ((regs[idx].mask & RPOLL_IN) &&
					    (regs[idx].pfd->revents & poll_in))
						mask |= RPOLL_IN;
					if ((regs[idx].mask & RPOLL_OUT) &&
					    (regs[idx].pfd->revents & poll_out))
						mask |= RPOLL_OUT;
					if((regs[idx].mask & RPOLL_EXCEPT) &&
					    (regs[idx].pfd->revents & poll_except))
						mask |= RPOLL_EXCEPT;
				}
# endif
# ifdef USE_SELECT
				if ((regs[idx].mask & RPOLL_IN) &&
				    FD_ISSET(regs[idx].fd, &nrset))
					mask |= RPOLL_IN;
				if ((regs[idx].mask & RPOLL_OUT) &&
				    FD_ISSET(regs[idx].fd, &nwset))
					mask |= RPOLL_OUT;
				if ((regs[idx].mask & RPOLL_EXCEPT) &&
				    FD_ISSET(regs[idx].fd, &nxset))
					mask |= RPOLL_EXCEPT;
# endif
				assert(idx < regs_alloc);

				if(mask) {
					if(rpoll_trace)
						fprintf(stderr, "poll_dispatch() -- "
						    "file %d/%d %x",
						    regs[idx].fd, idx, mask);
					(*regs[idx].func)(regs[idx].fd, mask, regs[idx].arg);
				}
			}

		}
		last_index++;
	}

	/* dispatch timeouts */
	if(tfd_used) {
		now = GETUSECS();
		for(i = 0; i < tfd_used; i++) {
			if(tfd[i] < 0)
				continue;
			if(tims[tfd[i]].when > now)
				break;
			if(rpoll_trace)
				fprintf(stderr, "rpoll_dispatch() -- timeout %d",tfd[i]);
			(*tims[tfd[i]].func)(tfd[i], tims[tfd[i]].arg);
			if(tfd[i] < 0)
				continue;
			if(tims[tfd[i]].repeat)
				tims[tfd[i]].when = now + tims[tfd[i]].usecs;
			else {
				tims[tfd[i]].func = NULL;
				tims_used--;
				tfd[i] = -1;
			}
			resort = 1;
		}
	}
	in_dispatch = 0;
}


# ifdef TESTME
struct timeval start, now;
int t0, t1;

double elaps(void);
void infunc(int fd, int mask, void *arg);

double
elaps(void)
{
	gettimeofday(&now, NULL);

	return (double)(10 * now.tv_sec + now.tv_usec / 100000 -
	    10 * start.tv_sec - start.tv_usec / 100000) / 10;
}

void
infunc(int fd, int mask, void *arg)
{
	char buf[1024];
	int ret;

	mask = mask;
	arg = arg;
	if((ret = read(fd, buf, sizeof(buf))) < 0)
		_panic("read: %s", strerror(errno));
	write(1, "stdin:", 6);
	write(1, buf, ret);
}

void tfunc0(int tid, void *arg);
void tfunc1(int tid, void *arg);

void
tfunc0(int tid, void *arg)
{
	printf("%4.1f -- %d: %s\n", elaps(), tid, (char *)arg);
}
void
tfunc1(int tid, void *arg)
{
	printf("%4.1f -- %d: %s\n", elaps(), tid, (char *)arg);
}
void
tfunc2(int tid, void *arg)
{
	static u_int count = 0;

	if (++count % 10000 == 0)
		printf("%4.1f -- %d\n", elaps(), tid);
}

void first(int tid, void *arg);
void second(int tid, void *arg);

void
second(int tid, void *arg)
{
	printf("%4.1f -- %d: %s\n", elaps(), tid, (char *)arg);
	poll_start_utimer(5500000, 0, first, "first");
	poll_stop_timer(t1);
	t0 = poll_start_timer(1000, 1, tfunc0, "1 second");
}
void
first(int tid, void *arg)
{
	printf("%4.1f -- %d: %s\n", elaps(), tid, (char *)arg);
	poll_start_timer(3700, 0, second, "second");
	poll_stop_timer(t0);
	t1 = poll_start_timer(250, 1, tfunc1, "1/4 second");
}

int
main(int argc, char *argv[])
{
	argv = argv;
	gettimeofday(&start, NULL);
	poll_register(0, infunc, NULL, RPOLL_IN);

	if (argc < 2) {
		t0 = poll_start_timer(1000, 1, tfunc0, "1 second");
		poll_start_timer(2500, 0, first, "first");
	} else {
		t0 = poll_start_utimer(300, 1, tfunc2, NULL);
	}

	while(1)
		poll_dispatch(1);

	return 0;
}
# endif
