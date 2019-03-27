/* $NetBSD: t_msgget.c,v 1.2 2014/02/27 00:59:50 joerg Exp $ */

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
__RCSID("$NetBSD: t_msgget.c,v 1.2 2014/02/27 00:59:50 joerg Exp $");

#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#define MSG_KEY		12345689

static void		clean(void);

static void
clean(void)
{
	int id;

	if ((id = msgget(MSG_KEY, 0)) != -1)
		(void)msgctl(id, IPC_RMID, 0);
}

ATF_TC_WITH_CLEANUP(msgget_excl);
ATF_TC_HEAD(msgget_excl, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test msgget(2) with IPC_EXCL");
}

ATF_TC_BODY(msgget_excl, tc)
{
	int id;

	/*
	 * Create a message queue and re-open it with
	 * O_CREAT and IPC_EXCL set. This should fail.
	 */
	id = msgget(MSG_KEY, IPC_CREAT | 0600);

	if (id < 0)
		atf_tc_fail("failed to create message queue");

	errno = 0;

	if (msgget(MSG_KEY, IPC_CREAT | IPC_EXCL | 0600) != -1)
		atf_tc_fail("msgget(2) failed for IPC_EXCL");

	ATF_REQUIRE(errno == EEXIST);

	/*
	 * However, the same call should succeed
	 * when IPC_EXCL is not set in the flags.
	 */
	id = msgget(MSG_KEY, IPC_CREAT | 0600);

	if (id < 0)
		atf_tc_fail("msgget(2) failed to re-open");

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgget_excl, tc)
{
	clean();
}

ATF_TC_WITH_CLEANUP(msgget_exit);
ATF_TC_HEAD(msgget_exit, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that XSI message queues are "
	    "not removed when the process exits");
}

ATF_TC_BODY(msgget_exit, tc)
{
	int id, sta;
	pid_t pid;

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		if (msgget(MSG_KEY, IPC_CREAT | IPC_EXCL | 0600) == -1)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("failed to create message queue");

	id = msgget(MSG_KEY, 0);

	if (id == -1)
		atf_tc_fail("message queue was removed on process exit");

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgget_exit, tc)
{
	clean();
}

ATF_TC_WITH_CLEANUP(msgget_init);
ATF_TC_HEAD(msgget_init, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that msgget(2) initializes data structures properly");
}

ATF_TC_BODY(msgget_init, tc)
{
	const uid_t uid = geteuid();
	const gid_t gid = getegid();
	struct msqid_ds msgds;
	time_t t;
	int id;

	(void)memset(&msgds, 0x9, sizeof(struct msqid_ds));

	t = time(NULL);
	id = msgget(MSG_KEY, IPC_CREAT | 0600);

	ATF_REQUIRE(id !=-1);
	ATF_REQUIRE(msgctl(id, IPC_STAT, &msgds) == 0);

	ATF_CHECK(msgds.msg_qnum == 0);
	ATF_CHECK(msgds.msg_lspid == 0);
	ATF_CHECK(msgds.msg_lrpid == 0);
	ATF_CHECK(msgds.msg_rtime == 0);
	ATF_CHECK(msgds.msg_stime == 0);
	ATF_CHECK(msgds.msg_perm.uid == uid);
	ATF_CHECK(msgds.msg_perm.gid == gid);
	ATF_CHECK(msgds.msg_perm.cuid == uid);
	ATF_CHECK(msgds.msg_perm.cgid == gid);
	ATF_CHECK(msgds.msg_perm.mode == 0600);

	if (llabs(t - msgds.msg_ctime) > 5)
		atf_tc_fail("msgget(2) initialized current time incorrectly");

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgget_init, tc)
{
	clean();
}

ATF_TC(msgget_limit);
ATF_TC_HEAD(msgget_limit, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test msgget(2) against system limits");
}

ATF_TC_BODY(msgget_limit, tc)
{
	size_t len = sizeof(int);
	bool fail = false;
	int i, lim = 0;
	int *buf;

	if (sysctlbyname("kern.ipc.msgmni", &lim, &len, NULL, 0) != 0)
		atf_tc_skip("failed to read kern.ipc.msgmni sysctl");

	buf = calloc(lim + 1, sizeof(*buf));
	ATF_REQUIRE(buf != NULL);

	for (i = 0; i < lim; i++) {

		buf[i] = msgget(MSG_KEY + i, IPC_CREAT | IPC_EXCL | 0600);

		(void)fprintf(stderr, "key[%d] = %d\n", i, buf[i]);

		/*
		 * This test only works when there are zero existing
		 * message queues. Thus, bypass the unit test when
		 * this precondition is not met, for reason or another.
		 */
		if (buf[i] == -1)
			goto out;
	}

	errno = 0;

	buf[i] = msgget(MSG_KEY + i, IPC_CREAT | IPC_EXCL | 0600);

	if (buf[i] != -1 || errno != ENOSPC)
		fail = true;

out:	/* Remember to clean-up. */
	for (i = 0; i < lim; i++)
		(void)msgctl(buf[i], IPC_RMID, 0);

	free(buf);

	if (fail != false)
		atf_tc_fail("msgget(2) opened more than %d queues", lim);
}

ATF_TC_WITH_CLEANUP(msgget_mode);
ATF_TC_HEAD(msgget_mode, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test different modes with msgget(2)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(msgget_mode, tc)
{
	static const mode_t mode[] = {
		S_IRWXU, S_IRUSR, S_IWUSR, S_IXUSR, S_IRWXG, S_IRGRP,
		S_IWGRP, S_IXGRP, S_IRWXO, S_IROTH, S_IWOTH, S_IXOTH
	};

	struct msqid_ds msgds;
	size_t i;
	int id;

	for (i = 0; i < __arraycount(mode); i++) {

		(void)fprintf(stderr, "testing mode %d\n", mode[i]);
		(void)memset(&msgds, 0, sizeof(struct msqid_ds));

		id = msgget(MSG_KEY, IPC_CREAT | IPC_EXCL | (int)mode[i]);

		ATF_REQUIRE(id != -1);
		ATF_REQUIRE(msgctl(id, IPC_STAT, &msgds) == 0);
		ATF_REQUIRE(msgds.msg_perm.mode == mode[i]);
		ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
	}
}

ATF_TC_CLEANUP(msgget_mode, tc)
{
	clean();
}


ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, msgget_excl);
	ATF_TP_ADD_TC(tp, msgget_exit);
	ATF_TP_ADD_TC(tp, msgget_init);
	ATF_TP_ADD_TC(tp, msgget_limit);
	ATF_TP_ADD_TC(tp, msgget_mode);

	return atf_no_error();
}
