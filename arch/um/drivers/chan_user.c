/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{linux.intel,addtoit}.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include "chan_user.h"
#include "os.h"
#include "um_malloc.h"
#include "user.h"

void generic_close(int fd, void *unused)
{
	close(fd);
}

int generic_read(int fd, char *c_out, void *unused)
{
	int n;

	n = read(fd, c_out, sizeof(*c_out));
	if (n > 0)
		return n;
	else if (errno == EAGAIN)
		return 0;
	else if (n == 0)
		return -EIO;
	return -errno;
}

/* XXX Trivial wrapper around write */

int generic_write(int fd, const char *buf, int n, void *unused)
{
	int err;

	err = write(fd, buf, n);
	if (err > 0)
		return err;
	else if (errno == EAGAIN)
		return 0;
	else if (err == 0)
		return -EIO;
	return -errno;
}

int generic_window_size(int fd, void *unused, unsigned short *rows_out,
			unsigned short *cols_out)
{
	struct winsize size;
	int ret;

	if (ioctl(fd, TIOCGWINSZ, &size) < 0)
		return -errno;

	ret = ((*rows_out != size.ws_row) || (*cols_out != size.ws_col));

	*rows_out = size.ws_row;
	*cols_out = size.ws_col;

	return ret;
}

void generic_free(void *data)
{
	kfree(data);
}

int generic_console_write(int fd, const char *buf, int n)
{
	sigset_t old, no_sigio;
	struct termios save, new;
	int err;

	if (isatty(fd)) {
		sigemptyset(&no_sigio);
		sigaddset(&no_sigio, SIGIO);
		if (sigprocmask(SIG_BLOCK, &no_sigio, &old))
			goto error;

		CATCH_EINTR(err = tcgetattr(fd, &save));
		if (err)
			goto error;
		new = save;
		/*
		 * The terminal becomes a bit less raw, to handle \n also as
		 * "Carriage Return", not only as "New Line". Otherwise, the new
		 * line won't start at the first column.
		 */
		new.c_oflag |= OPOST;
		CATCH_EINTR(err = tcsetattr(fd, TCSAFLUSH, &new));
		if (err)
			goto error;
	}
	err = generic_write(fd, buf, n, NULL);
	/*
	 * Restore raw mode, in any case; we *must* ignore any error apart
	 * EINTR, except for debug.
	 */
	if (isatty(fd)) {
		CATCH_EINTR(tcsetattr(fd, TCSAFLUSH, &save));
		sigprocmask(SIG_SETMASK, &old, NULL);
	}

	return err;
error:
	return -errno;
}

/*
 * UML SIGWINCH handling
 *
 * The point of this is to handle SIGWINCH on consoles which have host
 * ttys and relay them inside UML to whatever might be running on the
 * console and cares about the window size (since SIGWINCH notifies
 * about terminal size changes).
 *
 * So, we have a separate thread for each host tty attached to a UML
 * device (side-issue - I'm annoyed that one thread can't have
 * multiple controlling ttys for the purpose of handling SIGWINCH, but
 * I imagine there are other reasons that doesn't make any sense).
 *
 * SIGWINCH can't be received synchronously, so you have to set up to
 * receive it as a signal.  That being the case, if you are going to
 * wait for it, it is convenient to sit in sigsuspend() and wait for
 * the signal to bounce you out of it (see below for how we make sure
 * to exit only on SIGWINCH).
 */

static void winch_handler(int sig)
{
}

struct winch_data {
	int pty_fd;
	int pipe_fd;
};

static int winch_thread(void *arg)
{
	struct winch_data *data = arg;
	sigset_t sigs;
	int pty_fd, pipe_fd;
	int count;
	char c = 1;

	pty_fd = data->pty_fd;
	pipe_fd = data->pipe_fd;
	count = write(pipe_fd, &c, sizeof(c));
	if (count != sizeof(c))
		printk(UM_KERN_ERR "winch_thread : failed to write "
		       "synchronization byte, err = %d\n", -count);

	/*
	 * We are not using SIG_IGN on purpose, so don't fix it as I thought to
	 * do! If using SIG_IGN, the sigsuspend() call below would not stop on
	 * SIGWINCH.
	 */

	signal(SIGWINCH, winch_handler);
	sigfillset(&sigs);
	/* Block all signals possible. */
	if (sigprocmask(SIG_SETMASK, &sigs, NULL) < 0) {
		printk(UM_KERN_ERR "winch_thread : sigprocmask failed, "
		       "errno = %d\n", errno);
		exit(1);
	}
	/* In sigsuspend(), block anything else than SIGWINCH. */
	sigdelset(&sigs, SIGWINCH);

	if (setsid() < 0) {
		printk(UM_KERN_ERR "winch_thread : setsid failed, errno = %d\n",
		       errno);
		exit(1);
	}

	if (ioctl(pty_fd, TIOCSCTTY, 0) < 0) {
		printk(UM_KERN_ERR "winch_thread : TIOCSCTTY failed on "
		       "fd %d err = %d\n", pty_fd, errno);
		exit(1);
	}

	if (tcsetpgrp(pty_fd, os_getpid()) < 0) {
		printk(UM_KERN_ERR "winch_thread : tcsetpgrp failed on "
		       "fd %d err = %d\n", pty_fd, errno);
		exit(1);
	}

	/*
	 * These are synchronization calls between various UML threads on the
	 * host - since they are not different kernel threads, we cannot use
	 * kernel semaphores. We don't use SysV semaphores because they are
	 * persistent.
	 */
	count = read(pipe_fd, &c, sizeof(c));
	if (count != sizeof(c))
		printk(UM_KERN_ERR "winch_thread : failed to read "
		       "synchronization byte, err = %d\n", errno);

	while(1) {
		/*
		 * This will be interrupted by SIGWINCH only, since
		 * other signals are blocked.
		 */
		sigsuspend(&sigs);

		count = write(pipe_fd, &c, sizeof(c));
		if (count != sizeof(c))
			printk(UM_KERN_ERR "winch_thread : write failed, "
			       "err = %d\n", errno);
	}
}

static int winch_tramp(int fd, struct tty_struct *tty, int *fd_out,
		       unsigned long *stack_out)
{
	struct winch_data data;
	int fds[2], n, err;
	char c;

	err = os_pipe(fds, 1, 1);
	if (err < 0) {
		printk(UM_KERN_ERR "winch_tramp : os_pipe failed, err = %d\n",
		       -err);
		goto out;
	}

	data = ((struct winch_data) { .pty_fd 		= fd,
				      .pipe_fd 		= fds[1] } );
	/*
	 * CLONE_FILES so this thread doesn't hold open files which are open
	 * now, but later closed in a different thread.  This is a
	 * problem with /dev/net/tun, which if held open by this
	 * thread, prevents the TUN/TAP device from being reused.
	 */
	err = run_helper_thread(winch_thread, &data, CLONE_FILES, stack_out);
	if (err < 0) {
		printk(UM_KERN_ERR "fork of winch_thread failed - errno = %d\n",
		       -err);
		goto out_close;
	}

	*fd_out = fds[0];
	n = read(fds[0], &c, sizeof(c));
	if (n != sizeof(c)) {
		printk(UM_KERN_ERR "winch_tramp : failed to read "
		       "synchronization byte\n");
		printk(UM_KERN_ERR "read failed, err = %d\n", errno);
		printk(UM_KERN_ERR "fd %d will not support SIGWINCH\n", fd);
		err = -EINVAL;
		goto out_close;
	}

	if (os_set_fd_block(*fd_out, 0)) {
		printk(UM_KERN_ERR "winch_tramp: failed to set thread_fd "
		       "non-blocking.\n");
		goto out_close;
	}

	return err;

 out_close:
	close(fds[1]);
	close(fds[0]);
 out:
	return err;
}

void register_winch(int fd, struct tty_struct *tty)
{
	unsigned long stack;
	int pid, thread, count, thread_fd = -1;
	char c = 1;

	if (!isatty(fd))
		return;

	pid = tcgetpgrp(fd);
	if (!is_skas_winch(pid, fd, tty) && (pid == -1)) {
		thread = winch_tramp(fd, tty, &thread_fd, &stack);
		if (thread < 0)
			return;

		register_winch_irq(thread_fd, fd, thread, tty, stack);

		count = write(thread_fd, &c, sizeof(c));
		if (count != sizeof(c))
			printk(UM_KERN_ERR "register_winch : failed to write "
			       "synchronization byte, err = %d\n", errno);
	}
}
