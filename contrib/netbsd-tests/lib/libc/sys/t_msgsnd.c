/* $NetBSD: t_msgsnd.c,v 1.3 2017/01/13 20:44:45 christos Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
#include <sys/cdefs.h>
__RCSID("$NetBSD: t_msgsnd.c,v 1.3 2017/01/13 20:44:45 christos Exp $");

#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#define MSG_KEY		1234
#define MSG_MTYPE_1	0x41
#define	MSG_MTYPE_2	0x42
#define MSG_MTYPE_3	0x43

struct msg {
	long		 mtype;
	char		 buf[3];
};

static void		clean(void);

static void
clean(void)
{
	int id;

	if ((id = msgget(MSG_KEY, 0)) != -1)
		(void)msgctl(id, IPC_RMID, 0);
}

ATF_TC_WITH_CLEANUP(msgsnd_block);
ATF_TC_HEAD(msgsnd_block, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that msgsnd(2) blocks");
	atf_tc_set_md_var(tc, "timeout", "10");
}

ATF_TC_BODY(msgsnd_block, tc)
{
	struct msg msg = { MSG_MTYPE_1, { 'a', 'b', 'c' } };
	int id, sta;
	pid_t pid;

	id = msgget(MSG_KEY, IPC_CREAT | 0600);
	ATF_REQUIRE(id != -1);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		/*
		 * Enqueue messages until some limit (e.g. the maximum
		 * number of messages in the queue or the maximum number
		 * of bytes in the queue) is reached. After this the call
		 * should block when the IPC_NOWAIT is not set.
		 */
		for (;;) {

#ifdef __FreeBSD__
			if (msgsnd(id, &msg, sizeof(msg.buf), 0) < 0)
#else
			if (msgsnd(id, &msg, sizeof(struct msg), 0) < 0)
#endif
				_exit(EXIT_FAILURE);
		}
	}

	(void)sleep(2);
	(void)kill(pid, SIGKILL);
	(void)wait(&sta);

	if (WIFEXITED(sta) != 0 || WIFSIGNALED(sta) == 0)
		atf_tc_fail("msgsnd(2) did not block");

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgsnd_block, tc)
{
	clean();
}

ATF_TC_WITH_CLEANUP(msgsnd_count);
ATF_TC_HEAD(msgsnd_count, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that msgsnd(2) increments the amount of "
	    "message in the queue, as given by msgctl(2)");
	atf_tc_set_md_var(tc, "timeout", "10");
}

ATF_TC_BODY(msgsnd_count, tc)
{
	struct msg msg = { MSG_MTYPE_1, { 'a', 'b', 'c' } };
	struct msqid_ds ds;
	size_t i = 0;
	int id, rv;

	id = msgget(MSG_KEY, IPC_CREAT | 0600);
	ATF_REQUIRE(id != -1);

	for (;;) {

		errno = 0;
#ifdef	__FreeBSD__
		rv = msgsnd(id, &msg, sizeof(msg.buf), IPC_NOWAIT);
#else
		rv = msgsnd(id, &msg, sizeof(struct msg), IPC_NOWAIT);
#endif

		if (rv == 0) {
			i++;
			continue;
		}

		if (rv == -1 && errno == EAGAIN)
			break;

		atf_tc_fail("failed to enqueue a message");
	}

	(void)memset(&ds, 0, sizeof(struct msqid_ds));
	(void)msgctl(id, IPC_STAT, &ds);

	if (ds.msg_qnum != i)
		atf_tc_fail("incorrect message count");

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgsnd_count, tc)
{
	clean();
}

ATF_TC_WITH_CLEANUP(msgsnd_err);
ATF_TC_HEAD(msgsnd_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from msgsnd(2)");
}

ATF_TC_BODY(msgsnd_err, tc)
{
	struct msg msg = { MSG_MTYPE_1, { 'a', 'b', 'c' } };
	int id;

	id = msgget(MSG_KEY, IPC_CREAT | 0600);
	ATF_REQUIRE(id != -1);

	errno = 0;

	ATF_REQUIRE_ERRNO(EFAULT, msgsnd(id, (void *)-1,
#ifdef	__FreeBSD__
		sizeof(msg.buf), IPC_NOWAIT) == -1);
#else
		sizeof(struct msg), IPC_NOWAIT) == -1);
#endif

	errno = 0;

	ATF_REQUIRE_ERRNO(EINVAL, msgsnd(-1, &msg,
#ifdef	__FreeBSD__
		sizeof(msg.buf), IPC_NOWAIT) == -1);
#else
		sizeof(struct msg), IPC_NOWAIT) == -1);
#endif

	errno = 0;

	ATF_REQUIRE_ERRNO(EINVAL, msgsnd(-1, &msg,
		SSIZE_MAX, IPC_NOWAIT) == -1);

	errno = 0;
	msg.mtype = 0;

	ATF_REQUIRE_ERRNO(EINVAL, msgsnd(id, &msg,
#ifdef	__FreeBSD__
		sizeof(msg.buf), IPC_NOWAIT) == -1);
#else
		sizeof(struct msg), IPC_NOWAIT) == -1);
#endif

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgsnd_err, tc)
{
	clean();
}

ATF_TC_WITH_CLEANUP(msgsnd_nonblock);
ATF_TC_HEAD(msgsnd_nonblock, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test msgsnd(2) with IPC_NOWAIT");
	atf_tc_set_md_var(tc, "timeout", "10");
}

ATF_TC_BODY(msgsnd_nonblock, tc)
{
	struct msg msg = { MSG_MTYPE_1, { 'a', 'b', 'c' } };
	int id, rv, sta;
	pid_t pid;

	id = msgget(MSG_KEY, IPC_CREAT | 0600);
	ATF_REQUIRE(id != -1);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		for (;;) {

			errno = 0;
#ifdef	__FreeBSD__
			rv = msgsnd(id, &msg, sizeof(msg.buf), IPC_NOWAIT);
#else
			rv = msgsnd(id, &msg, sizeof(struct msg), IPC_NOWAIT);
#endif

			if (rv == -1 && errno == EAGAIN)
				_exit(EXIT_SUCCESS);
		}
	}

	(void)sleep(2);
	(void)kill(pid, SIGKILL);
	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WIFSIGNALED(sta) != 0)
		atf_tc_fail("msgsnd(2) blocked with IPC_NOWAIT");

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgsnd_nonblock, tc)
{
	clean();
}

ATF_TC_WITH_CLEANUP(msgsnd_perm);
ATF_TC_HEAD(msgsnd_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test permissions with msgsnd(2)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(msgsnd_perm, tc)
{
	struct msg msg = { MSG_MTYPE_1, { 'a', 'b', 'c' } };
	struct passwd *pw;
	int id, sta;
	pid_t pid;
	uid_t uid;

	pw = getpwnam("nobody");
	id = msgget(MSG_KEY, IPC_CREAT | 0600);

	ATF_REQUIRE(id != -1);
	ATF_REQUIRE(pw != NULL);

	uid = pw->pw_uid;
	ATF_REQUIRE(uid != 0);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		/*
		 * Try to enqueue a message to the queue
		 * created by root as RW for owner only.
		 */
		if (setuid(uid) != 0)
			_exit(EX_OSERR);

		id = msgget(MSG_KEY, 0);

		if (id == -1)
			_exit(EX_OSERR);

		errno = 0;

#ifdef	__FreeBSD__
		if (msgsnd(id, &msg, sizeof(msg.buf), IPC_NOWAIT) == 0)
#else
		if (msgsnd(id, &msg, sizeof(struct msg), IPC_NOWAIT) == 0)
#endif
			_exit(EXIT_FAILURE);

		if (errno != EACCES)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS) {

		if (errno == EX_OSERR)
			atf_tc_fail("system call failed");

		atf_tc_fail("UID %u enqueued message to root's queue", uid);
	}

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgsnd_perm, tc)
{
	clean();
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, msgsnd_block);
	ATF_TP_ADD_TC(tp, msgsnd_count);
	ATF_TP_ADD_TC(tp, msgsnd_err);
	ATF_TP_ADD_TC(tp, msgsnd_nonblock);
	ATF_TP_ADD_TC(tp, msgsnd_perm);

	return atf_no_error();
}
