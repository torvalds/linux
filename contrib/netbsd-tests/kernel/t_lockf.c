/*	$NetBSD: t_lockf.c,v 1.9 2013/10/19 17:45:00 christos Exp $	*/

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <atf-c.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ptrace.h> 

/*
 * lockf1 regression test:
 *
 * Tests:
 * Fork N child processes, each of which gets M random byte range locks
 * on a common file.  We ignore all lock errors (practically speaking,
 * this means EDEADLK or ENOLOCK), but we make numerous passes over all
 * the children to make sure that they are still awake.  (We do this by
 * verifying that we can ptrace(ATTACH/DETACH) to the children and get
 * their status via waitpid().)
 * When finished, reap all the children.
 */

#define	nlocks		500	/* number of locks per thread */
#define	nprocs		10	/* number of processes to spawn */
#define	npasses		50	/* number of passes to make over the children */
#define	sleeptime	150000	/* sleep time between locks, usec */
#define	filesize 	8192	/* size of file to lock */

const char *lockfile = "lockf_test";

static u_int32_t
random_uint32(void)
{
	return lrand48();
}

static void
trylocks(int id)
{
	int i, fd;

	srand48(getpid());

	fd = open (lockfile, O_RDWR, 0);
        
	if (fd < 0)
		err(1, "%s", lockfile);

	printf("%d: start\n", id);

	for (i = 0; i < nlocks; i++) {
		struct flock fl;

		fl.l_start = random_uint32() % filesize;
		fl.l_len = random_uint32() % filesize;
		switch (random_uint32() % 3) {
		case 0:
			fl.l_type = F_RDLCK;
			break;
		case 1:
			fl.l_type = F_WRLCK;
			break;
		case 2:
			fl.l_type = F_UNLCK;
			break;
		}
		fl.l_whence = SEEK_SET;

		(void)fcntl(fd, F_SETLKW, &fl);

		if (usleep(sleeptime) < 0) 
#if defined(__FreeBSD__)
		  if (errno != EINTR)
#endif
		  err(1, "usleep");
	}
	printf("%d: done\n", id);
	close (fd);
}

ATF_TC(randlock);
ATF_TC_HEAD(randlock, tc)
{  

	atf_tc_set_md_var(tc, "timeout", "300");
	atf_tc_set_md_var(tc, "descr", "Checks fcntl(2) locking");
}

ATF_TC_BODY(randlock, tc)
{
	int i, j, fd;
	int pipe_fd[2];
	pid_t *pid;
	int status;
	char pipe_in, pipe_out;
	const char pipe_errmsg[] = "child: pipe write failed\n";

	(void)unlink(lockfile);

	fd = open (lockfile, O_RDWR|O_CREAT|O_EXCL|O_TRUNC, 0666);
	ATF_REQUIRE_MSG(fd >= 0, "open(%s): %s", lockfile, strerror(errno));

	ATF_REQUIRE_MSG(ftruncate(fd, filesize) >= 0,
	    "ftruncate(%s): %s", lockfile, strerror(errno));

	ATF_REQUIRE_MSG(pipe(pipe_fd) == 0, "pipe: %s", strerror(errno));

	fsync(fd);
	close(fd);

	pid = malloc(nprocs * sizeof(pid_t));
	
	for (i = 0; i < nprocs; i++) {
		pipe_out = (char)('A' + i);
		pid[i] = fork();
		switch (pid[i]) {
		case 0:
			if (write(pipe_fd[1], &pipe_out, 1) != 1)
				write(STDERR_FILENO, pipe_errmsg,
				    __arraycount(pipe_errmsg) - 1);
			else
				trylocks(i);
			_exit(0);
			break;
		case -1:
			atf_tc_fail("fork %d failed", i);
			break;
		default:
			ATF_REQUIRE_MSG(read(pipe_fd[0], &pipe_in, 1) == 1,
			    "parent: read_pipe(%i): %s", i, strerror(errno));
			ATF_REQUIRE_MSG(pipe_in == pipe_out,
			    "parent: pipe does not match");
			break;
		}
	}
	for (j = 0; j < npasses; j++) {
		printf("parent: run %i\n", j+1);
		for (i = 0; i < nprocs; i++) {
			ATF_REQUIRE_MSG(ptrace(PT_ATTACH, pid[i], 0, 0) >= 0,
			    "ptrace attach %d", pid[i]);
			ATF_REQUIRE_MSG(waitpid(pid[i], &status, WUNTRACED) >= 0,
			    "waitpid(ptrace)");
			usleep(sleeptime / 3);
			ATF_REQUIRE_MSG(ptrace(PT_DETACH, pid[i], (caddr_t)1,
					       0) >= 0,
			    "ptrace detach %d", pid[i]);
			usleep(sleeptime / 3);
		}
	}
	for (i = 0; i < nprocs; i++) {
		printf("reap %d: ", i);
		fflush(stdout);
		kill(pid[i], SIGINT);
		waitpid(pid[i], &status, 0);
		printf(" status %d\n", status);
	}
	atf_tc_pass();
}

static int
dolock(int fd, int op, off_t lk_off, off_t lk_size)
{
	off_t result;
	int ret;

	result = lseek(fd, lk_off, SEEK_SET);
	if (result == -1) {
		return errno;
	}
	ATF_REQUIRE_MSG(result == lk_off, "lseek to wrong offset");
	ret = lockf(fd, op, lk_size);
	if (ret == -1) {
		return errno;
	}
	return 0;
}

ATF_TC(deadlock);
ATF_TC_HEAD(deadlock, tc)
{  

	atf_tc_set_md_var(tc, "timeout", "30");
	atf_tc_set_md_var(tc, "descr", "Checks fcntl(2) deadlock detection");
}

ATF_TC_BODY(deadlock, tc)
{
	int fd;
	int error;
	int ret;
	pid_t pid;

	(void)unlink(lockfile);

	fd = open (lockfile, O_RDWR|O_CREAT|O_EXCL|O_TRUNC, 0666);
	ATF_REQUIRE_MSG(fd >= 0, "open(%s): %s", lockfile, strerror(errno));

	ATF_REQUIRE_MSG(ftruncate(fd, filesize) >= 0,
	    "ftruncate(%s): %s", lockfile, strerror(errno));

	fsync(fd);

	error = dolock(fd, F_LOCK, 0, 1);
	ATF_REQUIRE_MSG(error == 0, "initial dolock: %s", strerror(errno));

	pid = fork();
	ATF_REQUIRE_MSG(pid != -1, "fork failed: %s", strerror(errno));
	if (pid == 0) {
		error = dolock(fd, F_LOCK, 1, 1);
		ATF_REQUIRE_MSG(error == 0, "child dolock: %s",
		    strerror(errno));
		dolock(fd, F_LOCK, 0, 1);	/* will block */
		atf_tc_fail("child did not block");
	}
	sleep(1);	/* give child time to grab its lock then block */

	error = dolock(fd, F_LOCK, 1, 1);
	ATF_REQUIRE_MSG(error == EDEADLK, "parent did not detect deadlock: %s",
	    strerror(errno));
	ret = kill(pid, SIGKILL);
	ATF_REQUIRE_MSG(ret != -1, "failed to kill child: %s", strerror(errno));

	atf_tc_pass();
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, randlock); 
	ATF_TP_ADD_TC(tp, deadlock); 

	return atf_no_error();
}
