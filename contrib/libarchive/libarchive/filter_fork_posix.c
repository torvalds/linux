/*-
 * Copyright (c) 2007 Joerg Sonnenberger
 * Copyright (c) 2012 Michihiro NAKAJIMA 
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "archive_platform.h"

/* This capability is only available on POSIX systems. */
#if defined(HAVE_PIPE) && defined(HAVE_FCNTL) && \
    (defined(HAVE_FORK) || defined(HAVE_VFORK) || defined(HAVE_POSIX_SPAWNP))

__FBSDID("$FreeBSD: head/lib/libarchive/filter_fork.c 182958 2008-09-12 05:33:00Z kientzle $");

#if defined(HAVE_SYS_TYPES_H)
#  include <sys/types.h>
#endif
#ifdef HAVE_ERRNO_H
#  include <errno.h>
#endif
#ifdef HAVE_STRING_H
#  include <string.h>
#endif
#if defined(HAVE_POLL) && (defined(HAVE_POLL_H) || defined(HAVE_SYS_POLL_H))
#  if defined(HAVE_POLL_H)
#    include <poll.h>
#  elif defined(HAVE_SYS_POLL_H)
#    include <sys/poll.h>
#  endif
#elif defined(HAVE_SELECT)
#  if defined(HAVE_SYS_SELECT_H)
#    include <sys/select.h>
#  elif defined(HAVE_UNISTD_H)
#    include <unistd.h>
#  endif
#endif
#ifdef HAVE_FCNTL_H
#  include <fcntl.h>
#endif
#ifdef HAVE_SPAWN_H
#  include <spawn.h>
#endif
#ifdef HAVE_STDLIB_H
#  include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include "archive.h"
#include "archive_cmdline_private.h"

#include "filter_fork.h"

pid_t
__archive_create_child(const char *cmd, int *child_stdin, int *child_stdout)
{
	pid_t child;
	int stdin_pipe[2], stdout_pipe[2], tmp;
#if HAVE_POSIX_SPAWNP
	posix_spawn_file_actions_t actions;
	int r;
#endif
	struct archive_cmdline *cmdline;

	cmdline = __archive_cmdline_allocate();
	if (cmdline == NULL)
		goto state_allocated;
	if (__archive_cmdline_parse(cmdline, cmd) != ARCHIVE_OK)
		goto state_allocated;

	if (pipe(stdin_pipe) == -1)
		goto state_allocated;
	if (stdin_pipe[0] == 1 /* stdout */) {
		if ((tmp = dup(stdin_pipe[0])) == -1)
			goto stdin_opened;
		close(stdin_pipe[0]);
		stdin_pipe[0] = tmp;
	}
	if (pipe(stdout_pipe) == -1)
		goto stdin_opened;
	if (stdout_pipe[1] == 0 /* stdin */) {
		if ((tmp = dup(stdout_pipe[1])) == -1)
			goto stdout_opened;
		close(stdout_pipe[1]);
		stdout_pipe[1] = tmp;
	}

#if HAVE_POSIX_SPAWNP

	r = posix_spawn_file_actions_init(&actions);
	if (r != 0) {
		errno = r;
		goto stdout_opened;
	}
	r = posix_spawn_file_actions_addclose(&actions, stdin_pipe[1]);
	if (r != 0)
		goto actions_inited;
	r = posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
	if (r != 0)
		goto actions_inited;
	/* Setup for stdin. */
	r = posix_spawn_file_actions_adddup2(&actions, stdin_pipe[0], 0);
	if (r != 0)
		goto actions_inited;
	if (stdin_pipe[0] != 0 /* stdin */) {
		r = posix_spawn_file_actions_addclose(&actions, stdin_pipe[0]);
		if (r != 0)
			goto actions_inited;
	}
	/* Setup for stdout. */
	r = posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], 1);
	if (r != 0)
		goto actions_inited;
	if (stdout_pipe[1] != 1 /* stdout */) {
		r = posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
		if (r != 0)
			goto actions_inited;
	}
	r = posix_spawnp(&child, cmdline->path, &actions, NULL,
		cmdline->argv, NULL);
	if (r != 0)
		goto actions_inited;
	posix_spawn_file_actions_destroy(&actions);

#else /* HAVE_POSIX_SPAWNP */

#if HAVE_VFORK
	child = vfork();
#else
	child = fork();
#endif
	if (child == -1)
		goto stdout_opened;
	if (child == 0) {
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		if (dup2(stdin_pipe[0], 0 /* stdin */) == -1)
			_exit(254);
		if (stdin_pipe[0] != 0 /* stdin */)
			close(stdin_pipe[0]);
		if (dup2(stdout_pipe[1], 1 /* stdout */) == -1)
			_exit(254);
		if (stdout_pipe[1] != 1 /* stdout */)
			close(stdout_pipe[1]);
		execvp(cmdline->path, cmdline->argv);
		_exit(254);
	}
#endif /* HAVE_POSIX_SPAWNP */

	close(stdin_pipe[0]);
	close(stdout_pipe[1]);

	*child_stdin = stdin_pipe[1];
	fcntl(*child_stdin, F_SETFL, O_NONBLOCK);
	*child_stdout = stdout_pipe[0];
	fcntl(*child_stdout, F_SETFL, O_NONBLOCK);
	__archive_cmdline_free(cmdline);

	return child;

#if HAVE_POSIX_SPAWNP
actions_inited:
	errno = r;
	posix_spawn_file_actions_destroy(&actions);
#endif
stdout_opened:
	close(stdout_pipe[0]);
	close(stdout_pipe[1]);
stdin_opened:
	close(stdin_pipe[0]);
	close(stdin_pipe[1]);
state_allocated:
	__archive_cmdline_free(cmdline);
	return -1;
}

void
__archive_check_child(int in, int out)
{
#if defined(HAVE_POLL) && (defined(HAVE_POLL_H) || defined(HAVE_SYS_POLL_H))
	struct pollfd fds[2];
	int idx;

	idx = 0;
	if (in != -1) {
		fds[idx].fd = in;
		fds[idx].events = POLLOUT;
		++idx;
	}
	if (out != -1) {
		fds[idx].fd = out;
		fds[idx].events = POLLIN;
		++idx;
	}

	poll(fds, idx, -1); /* -1 == INFTIM, wait forever */
#elif defined(HAVE_SELECT)
	fd_set fds_in, fds_out, fds_error;

	FD_ZERO(&fds_in);
	FD_ZERO(&fds_out);
	FD_ZERO(&fds_error);
	if (out != -1) {
		FD_SET(out, &fds_in);
		FD_SET(out, &fds_error);
	}
	if (in != -1) {
		FD_SET(in, &fds_out);
		FD_SET(in, &fds_error);
	}
	select(in < out ? out + 1 : in + 1, &fds_in, &fds_out, &fds_error, NULL);
#else
	sleep(1);
#endif
}

#endif /* defined(HAVE_PIPE) && defined(HAVE_VFORK) && defined(HAVE_FCNTL) */
