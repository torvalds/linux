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
#include <kern_util.h>
#include <init.h>
#include <os.h>
#include <sigio.h>
#include <um_malloc.h>

/*
 * Protected by sigio_lock(), also used by sigio_cleanup, which is an
 * exitcall.
 */
static int write_sigio_pid = -1;
static unsigned long write_sigio_stack;

/*
 * These arrays are initialized before the sigio thread is started, and
 * the descriptors closed after it is killed.  So, it can't see them change.
 * On the UML side, they are changed under the sigio_lock.
 */
#define SIGIO_FDS_INIT {-1, -1}

static int write_sigio_fds[2] = SIGIO_FDS_INIT;
static int sigio_private[2] = SIGIO_FDS_INIT;

struct pollfds {
	struct pollfd *poll;
	int size;
	int used;
};

/*
 * Protected by sigio_lock().  Used by the sigio thread, but the UML thread
 * synchronizes with it.
 */
static struct pollfds current_poll;
static struct pollfds next_poll;
static struct pollfds all_sigio_fds;

static int write_sigio_thread(void *unused)
{
	struct pollfds *fds, tmp;
	struct pollfd *p;
	int i, n, respond_fd;
	char c;

	os_fix_helper_signals();
	fds = &current_poll;
	while (1) {
		n = poll(fds->poll, fds->used, -1);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			printk(UM_KERN_ERR "write_sigio_thread : poll returned "
			       "%d, errno = %d\n", n, errno);
		}
		for (i = 0; i < fds->used; i++) {
			p = &fds->poll[i];
			if (p->revents == 0)
				continue;
			if (p->fd == sigio_private[1]) {
				CATCH_EINTR(n = read(sigio_private[1], &c,
						     sizeof(c)));
				if (n != sizeof(c))
					printk(UM_KERN_ERR
					       "write_sigio_thread : "
					       "read on socket failed, "
					       "err = %d\n", errno);
				tmp = current_poll;
				current_poll = next_poll;
				next_poll = tmp;
				respond_fd = sigio_private[1];
			}
			else {
				respond_fd = write_sigio_fds[1];
				fds->used--;
				memmove(&fds->poll[i], &fds->poll[i + 1],
					(fds->used - i) * sizeof(*fds->poll));
			}

			CATCH_EINTR(n = write(respond_fd, &c, sizeof(c)));
			if (n != sizeof(c))
				printk(UM_KERN_ERR "write_sigio_thread : "
				       "write on socket failed, err = %d\n",
				       errno);
		}
	}

	return 0;
}

static int need_poll(struct pollfds *polls, int n)
{
	struct pollfd *new;

	if (n <= polls->size)
		return 0;

	new = uml_kmalloc(n * sizeof(struct pollfd), UM_GFP_ATOMIC);
	if (new == NULL) {
		printk(UM_KERN_ERR "need_poll : failed to allocate new "
		       "pollfds\n");
		return -ENOMEM;
	}

	memcpy(new, polls->poll, polls->used * sizeof(struct pollfd));
	kfree(polls->poll);

	polls->poll = new;
	polls->size = n;
	return 0;
}

/*
 * Must be called with sigio_lock held, because it's needed by the marked
 * critical section.
 */
static void update_thread(void)
{
	unsigned long flags;
	int n;
	char c;

	flags = set_signals_trace(0);
	CATCH_EINTR(n = write(sigio_private[0], &c, sizeof(c)));
	if (n != sizeof(c)) {
		printk(UM_KERN_ERR "update_thread : write failed, err = %d\n",
		       errno);
		goto fail;
	}

	CATCH_EINTR(n = read(sigio_private[0], &c, sizeof(c)));
	if (n != sizeof(c)) {
		printk(UM_KERN_ERR "update_thread : read failed, err = %d\n",
		       errno);
		goto fail;
	}

	set_signals_trace(flags);
	return;
 fail:
	/* Critical section start */
	if (write_sigio_pid != -1) {
		os_kill_process(write_sigio_pid, 1);
		free_stack(write_sigio_stack, 0);
	}
	write_sigio_pid = -1;
	close(sigio_private[0]);
	close(sigio_private[1]);
	close(write_sigio_fds[0]);
	close(write_sigio_fds[1]);
	/* Critical section end */
	set_signals_trace(flags);
}

int __add_sigio_fd(int fd)
{
	struct pollfd *p;
	int err, i, n;

	for (i = 0; i < all_sigio_fds.used; i++) {
		if (all_sigio_fds.poll[i].fd == fd)
			break;
	}
	if (i == all_sigio_fds.used)
		return -ENOSPC;

	p = &all_sigio_fds.poll[i];

	for (i = 0; i < current_poll.used; i++) {
		if (current_poll.poll[i].fd == fd)
			return 0;
	}

	n = current_poll.used;
	err = need_poll(&next_poll, n + 1);
	if (err)
		return err;

	memcpy(next_poll.poll, current_poll.poll,
	       current_poll.used * sizeof(struct pollfd));
	next_poll.poll[n] = *p;
	next_poll.used = n + 1;
	update_thread();

	return 0;
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
	struct pollfd *p;
	int err, i, n = 0;

	/*
	 * This is called from exitcalls elsewhere in UML - if
	 * sigio_cleanup has already run, then update_thread will hang
	 * or fail because the thread is no longer running.
	 */
	if (write_sigio_pid == -1)
		return -EIO;

	for (i = 0; i < current_poll.used; i++) {
		if (current_poll.poll[i].fd == fd)
			break;
	}
	if (i == current_poll.used)
		return -ENOENT;

	err = need_poll(&next_poll, current_poll.used - 1);
	if (err)
		return err;

	for (i = 0; i < current_poll.used; i++) {
		p = &current_poll.poll[i];
		if (p->fd != fd)
			next_poll.poll[n++] = *p;
	}
	next_poll.used = current_poll.used - 1;

	update_thread();

	return 0;
}

int ignore_sigio_fd(int fd)
{
	int err;

	sigio_lock();
	err = __ignore_sigio_fd(fd);
	sigio_unlock();

	return err;
}

static struct pollfd *setup_initial_poll(int fd)
{
	struct pollfd *p;

	p = uml_kmalloc(sizeof(struct pollfd), UM_GFP_KERNEL);
	if (p == NULL) {
		printk(UM_KERN_ERR "setup_initial_poll : failed to allocate "
		       "poll\n");
		return NULL;
	}
	*p = ((struct pollfd) { .fd		= fd,
				.events 	= POLLIN,
				.revents 	= 0 });
	return p;
}

static void write_sigio_workaround(void)
{
	struct pollfd *p;
	int err;
	int l_write_sigio_fds[2];
	int l_sigio_private[2];
	int l_write_sigio_pid;

	/* We call this *tons* of times - and most ones we must just fail. */
	sigio_lock();
	l_write_sigio_pid = write_sigio_pid;
	sigio_unlock();

	if (l_write_sigio_pid != -1)
		return;

	err = os_pipe(l_write_sigio_fds, 1, 1);
	if (err < 0) {
		printk(UM_KERN_ERR "write_sigio_workaround - os_pipe 1 failed, "
		       "err = %d\n", -err);
		return;
	}
	err = os_pipe(l_sigio_private, 1, 1);
	if (err < 0) {
		printk(UM_KERN_ERR "write_sigio_workaround - os_pipe 2 failed, "
		       "err = %d\n", -err);
		goto out_close1;
	}

	p = setup_initial_poll(l_sigio_private[1]);
	if (!p)
		goto out_close2;

	sigio_lock();

	/*
	 * Did we race? Don't try to optimize this, please, it's not so likely
	 * to happen, and no more than once at the boot.
	 */
	if (write_sigio_pid != -1)
		goto out_free;

	current_poll = ((struct pollfds) { .poll 	= p,
					   .used 	= 1,
					   .size 	= 1 });

	if (write_sigio_irq(l_write_sigio_fds[0]))
		goto out_clear_poll;

	memcpy(write_sigio_fds, l_write_sigio_fds, sizeof(l_write_sigio_fds));
	memcpy(sigio_private, l_sigio_private, sizeof(l_sigio_private));

	write_sigio_pid = run_helper_thread(write_sigio_thread, NULL,
					    CLONE_FILES | CLONE_VM,
					    &write_sigio_stack);

	if (write_sigio_pid < 0)
		goto out_clear;

	sigio_unlock();
	return;

out_clear:
	write_sigio_pid = -1;
	write_sigio_fds[0] = -1;
	write_sigio_fds[1] = -1;
	sigio_private[0] = -1;
	sigio_private[1] = -1;
out_clear_poll:
	current_poll = ((struct pollfds) { .poll	= NULL,
					   .size	= 0,
					   .used	= 0 });
out_free:
	sigio_unlock();
	kfree(p);
out_close2:
	close(l_sigio_private[0]);
	close(l_sigio_private[1]);
out_close1:
	close(l_write_sigio_fds[0]);
	close(l_write_sigio_fds[1]);
}

void sigio_broken(int fd)
{
	int err;

	write_sigio_workaround();

	sigio_lock();
	err = need_poll(&all_sigio_fds, all_sigio_fds.used + 1);
	if (err) {
		printk(UM_KERN_ERR "maybe_sigio_broken - failed to add pollfd "
		       "for descriptor %d\n", fd);
		goto out;
	}

	all_sigio_fds.poll[all_sigio_fds.used++] =
		((struct pollfd) { .fd  	= fd,
				   .events 	= POLLIN,
				   .revents 	= 0 });
out:
	sigio_unlock();
}

/* Changed during early boot */
static int pty_output_sigio;

void maybe_sigio_broken(int fd)
{
	if (!isatty(fd))
		return;

	if (pty_output_sigio)
		return;

	sigio_broken(fd);
}

static void sigio_cleanup(void)
{
	if (write_sigio_pid == -1)
		return;

	os_kill_process(write_sigio_pid, 1);
	free_stack(write_sigio_stack, 0);
	write_sigio_pid = -1;
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
