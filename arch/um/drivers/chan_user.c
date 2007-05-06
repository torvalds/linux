/* 
 * Copyright (C) 2000 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#include <string.h>
#include <signal.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include "kern_util.h"
#include "user_util.h"
#include "chan_user.h"
#include "user.h"
#include "os.h"
#include "choose-mode.h"
#include "mode.h"

int generic_console_write(int fd, const char *buf, int n)
{
	struct termios save, new;
	int err;

	if(isatty(fd)){
		CATCH_EINTR(err = tcgetattr(fd, &save));
		if (err)
			goto error;
		new = save;
		/* The terminal becomes a bit less raw, to handle \n also as
		 * "Carriage Return", not only as "New Line". Otherwise, the new
		 * line won't start at the first column.*/
		new.c_oflag |= OPOST;
		CATCH_EINTR(err = tcsetattr(fd, TCSAFLUSH, &new));
		if (err)
			goto error;
	}
	err = generic_write(fd, buf, n, NULL);
	/* Restore raw mode, in any case; we *must* ignore any error apart
	 * EINTR, except for debug.*/
	if(isatty(fd))
		CATCH_EINTR(tcsetattr(fd, TCSAFLUSH, &save));
	return(err);
error:
	return(-errno);
}

/*
 * UML SIGWINCH handling
 *
 * The point of this is to handle SIGWINCH on consoles which have host ttys and
 * relay them inside UML to whatever might be running on the console and cares
 * about the window size (since SIGWINCH notifies about terminal size changes).
 *
 * So, we have a separate thread for each host tty attached to a UML device
 * (side-issue - I'm annoyed that one thread can't have multiple controlling
 * ttys for purposed of handling SIGWINCH, but I imagine there are other reasons
 * that doesn't make any sense).
 *
 * SIGWINCH can't be received synchronously, so you have to set up to receive it
 * as a signal.  That being the case, if you are going to wait for it, it is
 * convenient to sit in sigsuspend() and wait for the signal to bounce you out of
 * it (see below for how we make sure to exit only on SIGWINCH).
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
	int count, err;
	char c = 1;

	pty_fd = data->pty_fd;
	pipe_fd = data->pipe_fd;
	count = os_write_file(pipe_fd, &c, sizeof(c));
	if(count != sizeof(c))
		printk("winch_thread : failed to write synchronization "
		       "byte, err = %d\n", -count);

	/* We are not using SIG_IGN on purpose, so don't fix it as I thought to
	 * do! If using SIG_IGN, the sigsuspend() call below would not stop on
	 * SIGWINCH. */

	signal(SIGWINCH, winch_handler);
	sigfillset(&sigs);
	/* Block all signals possible. */
	if(sigprocmask(SIG_SETMASK, &sigs, NULL) < 0){
		printk("winch_thread : sigprocmask failed, errno = %d\n", 
		       errno);
		exit(1);
	}
	/* In sigsuspend(), block anything else than SIGWINCH. */
	sigdelset(&sigs, SIGWINCH);

	if(setsid() < 0){
		printk("winch_thread : setsid failed, errno = %d\n", errno);
		exit(1);
	}

	err = os_new_tty_pgrp(pty_fd, os_getpid());
	if(err < 0){
		printk("winch_thread : new_tty_pgrp failed, err = %d\n", -err);
		exit(1);
	}

	/* These are synchronization calls between various UML threads on the
	 * host - since they are not different kernel threads, we cannot use
	 * kernel semaphores. We don't use SysV semaphores because they are
	 * persistent. */
	count = os_read_file(pipe_fd, &c, sizeof(c));
	if(count != sizeof(c))
		printk("winch_thread : failed to read synchronization byte, "
		       "err = %d\n", -count);

	while(1){
		/* This will be interrupted by SIGWINCH only, since other signals
		 * are blocked.*/
		sigsuspend(&sigs);

		count = os_write_file(pipe_fd, &c, sizeof(c));
		if(count != sizeof(c))
			printk("winch_thread : write failed, err = %d\n",
			       -count);
	}
}

static int winch_tramp(int fd, struct tty_struct *tty, int *fd_out)
{
	struct winch_data data;
	unsigned long stack;
	int fds[2], n, err;
	char c;

	err = os_pipe(fds, 1, 1);
	if(err < 0){
		printk("winch_tramp : os_pipe failed, err = %d\n", -err);
		goto out;
	}

	data = ((struct winch_data) { .pty_fd 		= fd,
				      .pipe_fd 		= fds[1] } );
	/* CLONE_FILES so this thread doesn't hold open files which are open
	 * now, but later closed.  This is a problem with /dev/net/tun.
	 */
	err = run_helper_thread(winch_thread, &data, CLONE_FILES, &stack, 0);
	if(err < 0){
		printk("fork of winch_thread failed - errno = %d\n", -err);
		goto out_close;
	}

	*fd_out = fds[0];
	n = os_read_file(fds[0], &c, sizeof(c));
	if(n != sizeof(c)){
		printk("winch_tramp : failed to read synchronization byte\n");
		printk("read failed, err = %d\n", -n);
		printk("fd %d will not support SIGWINCH\n", fd);
                err = -EINVAL;
		goto out_close;
	}
	return err ;

 out_close:
	os_close_file(fds[1]);
	os_close_file(fds[0]);
 out:
	return err;
}

void register_winch(int fd, struct tty_struct *tty)
{
	int pid, thread, thread_fd = -1;
	int count;
	char c = 1;

	if(!isatty(fd))
		return;

	pid = tcgetpgrp(fd);
	if(!CHOOSE_MODE_PROC(is_tracer_winch, is_skas_winch, pid, fd,
			     tty) && (pid == -1)){
		thread = winch_tramp(fd, tty, &thread_fd);
		if(thread > 0){
			register_winch_irq(thread_fd, fd, thread, tty);

			count = os_write_file(thread_fd, &c, sizeof(c));
			if(count != sizeof(c))
				printk("register_winch : failed to write "
				       "synchronization byte, err = %d\n",
					-count);
		}
	}
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
