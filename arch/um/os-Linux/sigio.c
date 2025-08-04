// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 - 2008 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <asm/unistd.h>
#include <kern_util.h>
#include <init.h>
#include <os.h>
#include <sigio.h>
#include <um_malloc.h>

/*
 * Protected by sigio_lock(), also used by sigio_cleanup, which is an
 * exitcall.
 */
static struct os_helper_thread *write_sigio_td;

static int epollfd = -1;

#define MAX_EPOLL_EVENTS 64

static struct epoll_event epoll_events[MAX_EPOLL_EVENTS];

static void *write_sigio_thread(void *unused)
{
	int pid = getpid();
	int r;

	os_fix_helper_thread_signals();

	while (1) {
		r = epoll_wait(epollfd, epoll_events, MAX_EPOLL_EVENTS, -1);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			printk(UM_KERN_ERR "%s: epoll_wait failed, errno = %d\n",
			       __func__, errno);
		}

		CATCH_EINTR(r = syscall(__NR_tgkill, pid, pid, SIGIO));
		if (r < 0)
			printk(UM_KERN_ERR "%s: tgkill failed, errno = %d\n",
			       __func__, errno);
	}

	return NULL;
}

int __add_sigio_fd(int fd)
{
	struct epoll_event event = {
		.data.fd = fd,
		.events = EPOLLIN | EPOLLET,
	};
	int r;

	CATCH_EINTR(r = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event));
	return r < 0 ? -errno : 0;
}

int add_sigio_fd(int fd)
{
	int err;

	sigio_lock();
	err = __add_sigio_fd(fd);
	sigio_unlock();

	return err;
}

int __ignore_sigio_fd(int fd)
{
	struct epoll_event event;
	int r;

	CATCH_EINTR(r = epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &event));
	return r < 0 ? -errno : 0;
}

int ignore_sigio_fd(int fd)
{
	int err;

	sigio_lock();
	err = __ignore_sigio_fd(fd);
	sigio_unlock();

	return err;
}

static void write_sigio_workaround(void)
{
	int err;

	sigio_lock();
	if (write_sigio_td)
		goto out;

	epollfd = epoll_create(MAX_EPOLL_EVENTS);
	if (epollfd < 0) {
		printk(UM_KERN_ERR "%s: epoll_create failed, errno = %d\n",
		       __func__, errno);
		goto out;
	}

	err = os_run_helper_thread(&write_sigio_td, write_sigio_thread, NULL);
	if (err < 0) {
		printk(UM_KERN_ERR "%s: os_run_helper_thread failed, errno = %d\n",
		       __func__, -err);
		close(epollfd);
		epollfd = -1;
		goto out;
	}

out:
	sigio_unlock();
}

void sigio_broken(void)
{
	write_sigio_workaround();
}

/* Changed during early boot */
static int pty_output_sigio;

void maybe_sigio_broken(int fd)
{
	if (!isatty(fd))
		return;

	if (pty_output_sigio)
		return;

	sigio_broken();
}

static void sigio_cleanup(void)
{
	if (!write_sigio_td)
		return;

	os_kill_helper_thread(write_sigio_td);
	write_sigio_td = NULL;
}

__uml_exitcall(sigio_cleanup);

/* Used as a flag during SIGIO testing early in boot */
static int got_sigio;

static void __init handler(int sig)
{
	got_sigio = 1;
}

struct openpty_arg {
	int master;
	int slave;
	int err;
};

static void openpty_cb(void *arg)
{
	struct openpty_arg *info = arg;

	info->err = 0;
	if (openpty(&info->master, &info->slave, NULL, NULL, NULL))
		info->err = -errno;
}

static int async_pty(int master, int slave)
{
	int flags;

	flags = fcntl(master, F_GETFL);
	if (flags < 0)
		return -errno;

	if ((fcntl(master, F_SETFL, flags | O_NONBLOCK | O_ASYNC) < 0) ||
	    (fcntl(master, F_SETOWN, os_getpid()) < 0))
		return -errno;

	if ((fcntl(slave, F_SETFL, flags | O_NONBLOCK) < 0))
		return -errno;

	return 0;
}

static void __init check_one_sigio(void (*proc)(int, int))
{
	struct sigaction old, new;
	struct openpty_arg pty = { .master = -1, .slave = -1 };
	int master, slave, err;

	initial_thread_cb(openpty_cb, &pty);
	if (pty.err) {
		printk(UM_KERN_ERR "check_one_sigio failed, errno = %d\n",
		       -pty.err);
		return;
	}

	master = pty.master;
	slave = pty.slave;

	if ((master == -1) || (slave == -1)) {
		printk(UM_KERN_ERR "check_one_sigio failed to allocate a "
		       "pty\n");
		return;
	}

	/* Not now, but complain so we now where we failed. */
	err = raw(master);
	if (err < 0) {
		printk(UM_KERN_ERR "check_one_sigio : raw failed, errno = %d\n",
		      -err);
		return;
	}

	err = async_pty(master, slave);
	if (err < 0) {
		printk(UM_KERN_ERR "check_one_sigio : sigio_async failed, "
		       "err = %d\n", -err);
		return;
	}

	if (sigaction(SIGIO, NULL, &old) < 0) {
		printk(UM_KERN_ERR "check_one_sigio : sigaction 1 failed, "
		       "errno = %d\n", errno);
		return;
	}

	new = old;
	new.sa_handler = handler;
	if (sigaction(SIGIO, &new, NULL) < 0) {
		printk(UM_KERN_ERR "check_one_sigio : sigaction 2 failed, "
		       "errno = %d\n", errno);
		return;
	}

	got_sigio = 0;
	(*proc)(master, slave);

	close(master);
	close(slave);

	if (sigaction(SIGIO, &old, NULL) < 0)
		printk(UM_KERN_ERR "check_one_sigio : sigaction 3 failed, "
		       "errno = %d\n", errno);
}

static void tty_output(int master, int slave)
{
	int n;
	char buf[512];

	printk(UM_KERN_INFO "Checking that host ptys support output SIGIO...");

	memset(buf, 0, sizeof(buf));

	while (write(master, buf, sizeof(buf)) > 0) ;
	if (errno != EAGAIN)
		printk(UM_KERN_ERR "tty_output : write failed, errno = %d\n",
		       errno);
	while (((n = read(slave, buf, sizeof(buf))) > 0) &&
	       !({ barrier(); got_sigio; }))
		;

	if (got_sigio) {
		printk(UM_KERN_CONT "Yes\n");
		pty_output_sigio = 1;
	} else if (n == -EAGAIN)
		printk(UM_KERN_CONT "No, enabling workaround\n");
	else
		printk(UM_KERN_CONT "tty_output : read failed, err = %d\n", n);
}

static void __init check_sigio(void)
{
	if ((access("/dev/ptmx", R_OK) < 0) &&
	    (access("/dev/ptyp0", R_OK) < 0)) {
		printk(UM_KERN_WARNING "No pseudo-terminals available - "
		       "skipping pty SIGIO check\n");
		return;
	}
	check_one_sigio(tty_output);
}

/* Here because it only does the SIGIO testing for now */
void __init os_check_bugs(void)
{
	check_sigio();
}
