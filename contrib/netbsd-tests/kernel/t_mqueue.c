/*	$NetBSD: t_mqueue.c,v 1.6 2017/01/14 20:57:24 christos Exp $ */

/*
 * Test for POSIX message queue priority handling.
 *
 * This file is in the Public Domain.
 */

#include <sys/stat.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __FreeBSD__
#include "freebsd_test_suite/macros.h"
#endif

#define	MQ_PRIO_BASE	24

static void
send_msgs(mqd_t mqfd)
{
	char msg[2];

	msg[1] = '\0';

	msg[0] = 'a';
	ATF_REQUIRE_MSG(mq_send(mqfd, msg, sizeof(msg), MQ_PRIO_BASE) != -1,
	    "mq_send 1 failed: %d", errno);

	msg[0] = 'b';
	ATF_REQUIRE_MSG(mq_send(mqfd, msg, sizeof(msg), MQ_PRIO_BASE + 1) != -1,
	    "mq_send 2 failed: %d", errno);

	msg[0] = 'c';
	ATF_REQUIRE_MSG(mq_send(mqfd, msg, sizeof(msg), MQ_PRIO_BASE) != -1,
	    "mq_send 3 failed: %d", errno);

	msg[0] = 'd';
	ATF_REQUIRE_MSG(mq_send(mqfd, msg, sizeof(msg), MQ_PRIO_BASE - 1) != -1,
	    "mq_send 4 failed: %d", errno);

	msg[0] = 'e';
	ATF_REQUIRE_MSG(mq_send(mqfd, msg, sizeof(msg), 0) != -1,
	    "mq_send 5 failed: %d", errno);

	msg[0] = 'f';
	ATF_REQUIRE_MSG(mq_send(mqfd, msg, sizeof(msg), MQ_PRIO_BASE + 1) != -1,
	    "mq_send 6 failed: %d", errno);
}

static void
receive_msgs(mqd_t mqfd)
{
	struct mq_attr mqa;
	char *m;
	unsigned p;
	int len;

	ATF_REQUIRE_MSG(mq_getattr(mqfd, &mqa) != -1, "mq_getattr failed %d",
	    errno);

	len = mqa.mq_msgsize;
	m = calloc(1, len);
	ATF_REQUIRE_MSG(m != NULL, "calloc failed");

	ATF_REQUIRE_MSG(mq_receive(mqfd, m, len, &p) != -1,
	    "mq_receive 1 failed: %d", errno);
	ATF_REQUIRE_MSG(p == (MQ_PRIO_BASE + 1) && m[0] == 'b',
	    "mq_receive 1 prio/data mismatch");

	ATF_REQUIRE_MSG(mq_receive(mqfd, m, len, &p) != -1,
	    "mq_receive 2 failed: %d", errno);
	ATF_REQUIRE_MSG(p == (MQ_PRIO_BASE + 1) && m[0] == 'f',
	    "mq_receive 2 prio/data mismatch");

	ATF_REQUIRE_MSG(mq_receive(mqfd, m, len, &p) != -1,
	    "mq_receive 3 failed: %d", errno);
	ATF_REQUIRE_MSG(p == MQ_PRIO_BASE && m[0] == 'a',
	    "mq_receive 3 prio/data mismatch");

	ATF_REQUIRE_MSG(mq_receive(mqfd, m, len, &p) != -1,
	    "mq_receive 4 failed: %d", errno);
	ATF_REQUIRE_MSG(p == MQ_PRIO_BASE && m[0] == 'c',
	    "mq_receive 4 prio/data mismatch");

	ATF_REQUIRE_MSG(mq_receive(mqfd, m, len, &p) != -1,
	    "mq_receive 5 failed: %d", errno);
	ATF_REQUIRE_MSG(p == (MQ_PRIO_BASE - 1) && m[0] == 'd',
	    "mq_receive 5 prio/data mismatch");

	ATF_REQUIRE_MSG(mq_receive(mqfd, m, len, &p) != -1,
	    "mq_receive 6 failed: %d", errno);
	ATF_REQUIRE_MSG(p == 0 && m[0] == 'e',
	    "mq_receive 6 prio/data mismatch");
}

ATF_TC(mqueue);
ATF_TC_HEAD(mqueue, tc)
{

	atf_tc_set_md_var(tc, "timeout", "3");
	atf_tc_set_md_var(tc, "descr", "Checks mqueue send/receive");
}

ATF_TC_BODY(mqueue, tc)
{
	int status;
	char *tmpdir;
	char template[32];
	char mq_name[64];

#ifdef __FreeBSD__
	ATF_REQUIRE_KERNEL_MODULE("mqueuefs");
#endif

	strlcpy(template, "./t_mqueue.XXXXXX", sizeof(template));
	tmpdir = mkdtemp(template);
	ATF_REQUIRE_MSG(tmpdir != NULL, "mkdtemp failed: %d", errno);
#ifdef __FreeBSD__
	snprintf(mq_name, sizeof(mq_name), "/t_mqueue");
#else
	snprintf(mq_name, sizeof(mq_name), "%s/mq", tmpdir);
#endif

	mqd_t mqfd;

	mqfd = mq_open(mq_name, O_RDWR | O_CREAT,
	    S_IRUSR | S_IRWXG | S_IROTH, NULL);
#ifdef __FreeBSD__
	ATF_REQUIRE_MSG(mqfd != (mqd_t)-1, "mq_open failed: %d", errno);
#else
	ATF_REQUIRE_MSG(mqfd != -1, "mq_open failed: %d", errno);
#endif

	send_msgs(mqfd);
	receive_msgs(mqfd);

	status = mq_close(mqfd);
	ATF_REQUIRE_MSG(status == 0, "mq_close failed: %d", errno);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, mqueue); 

	return atf_no_error();
}
