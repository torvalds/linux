/*	$NetBSD: t_socket.c,v 1.5 2017/01/13 21:30:41 christos Exp $	*/

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <atf-c.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "h_macros.h"

ATF_TC(cmsg_sendfd_bounds);
ATF_TC_HEAD(cmsg_sendfd_bounds, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks that attempting to pass an "
	    "invalid fd returns an error");
}

ATF_TC_BODY(cmsg_sendfd_bounds, tc)
{
	struct cmsghdr *cmp;
	struct msghdr msg;
	struct iovec iov;
	int s[2];
	int fd;

	rump_init();

	if (rump_sys_socketpair(AF_LOCAL, SOCK_STREAM, 0, s) == -1)
		atf_tc_fail("rump_sys_socket");

	cmp = malloc(CMSG_SPACE(sizeof(int)));

	iov.iov_base = &fd;
	iov.iov_len = sizeof(int);

	cmp->cmsg_level = SOL_SOCKET;
	cmp->cmsg_type = SCM_RIGHTS;
	cmp->cmsg_len = CMSG_LEN(sizeof(int));

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = cmp;
	msg.msg_controllen = CMSG_SPACE(sizeof(int));

	/*
	 * ERROR HERE: trying to pass invalid fd
	 * (This value was previously directly used to index the fd
	 *  array and therefore we are passing a hyperspace index)
	 */
	*(int *)CMSG_DATA(cmp) = 0x12345678;

	rump_sys_sendmsg(s[0], &msg, 0);
	if (errno != EBADF)
		atf_tc_fail("descriptor passing failed: expected EBADF (9), "
		    "got %d\n(%s)", errno, strerror(errno));
}


ATF_TC(cmsg_sendfd);
ATF_TC_HEAD(cmsg_sendfd, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks that fd passing works");
	atf_tc_set_md_var(tc, "timeout", "10");
}

ATF_TC_BODY(cmsg_sendfd, tc)
{
	char buf[128];
	struct cmsghdr *cmp;
	struct msghdr msg;
	struct sockaddr_un sun;
	struct lwp *l1;
	struct iovec iov;
	socklen_t sl;
	int s1, s2, sgot;
	int rfd, fd[2], storage;

	rump_init();

	RZ(rump_pub_lwproc_rfork(RUMP_RFCFDG));
	l1 = rump_pub_lwproc_curlwp();

	/* create unix socket and bind it to a path */
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_LOCAL;
#define SOCKPATH "/com"
	strncpy(sun.sun_path, SOCKPATH, sizeof(SOCKPATH));
	s1 = rump_sys_socket(AF_LOCAL, SOCK_STREAM, 0);
	if (s1 == -1)
		atf_tc_fail_errno("socket 1");
	if (rump_sys_bind(s1, (struct sockaddr *)&sun, SUN_LEN(&sun)) == -1)
		atf_tc_fail_errno("socket 1 bind");
	if (rump_sys_listen(s1, 1) == -1)
		atf_tc_fail_errno("socket 1 listen");

	/* create second process for test */
	RZ(rump_pub_lwproc_rfork(RUMP_RFCFDG));
	(void)rump_pub_lwproc_curlwp();

	/* connect to unix domain socket */
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_LOCAL;
	strncpy(sun.sun_path, SOCKPATH, sizeof(SOCKPATH));
	s2 = rump_sys_socket(AF_LOCAL, SOCK_STREAM, 0);
	if (s2 == -1)
		atf_tc_fail_errno("socket 2");
	if (rump_sys_connect(s2, (struct sockaddr *)&sun, SUN_LEN(&sun)) == -1)
		atf_tc_fail_errno("socket 2 connect");

	/* open a pipe and write stuff to it */
	if (rump_sys_pipe(fd) == -1)
		atf_tc_fail_errno("can't open pipe");
#define MAGICSTRING "duam xnaht"
	if (rump_sys_write(fd[1], MAGICSTRING, sizeof(MAGICSTRING)) !=
	    sizeof(MAGICSTRING))
		atf_tc_fail_errno("pipe write"); /* XXX: errno */

	cmp = malloc(CMSG_SPACE(sizeof(int)));

	iov.iov_base = &storage;
	iov.iov_len = sizeof(int);

	cmp->cmsg_level = SOL_SOCKET;
	cmp->cmsg_type = SCM_RIGHTS;
	cmp->cmsg_len = CMSG_LEN(sizeof(int));

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = cmp;
	msg.msg_controllen = CMSG_SPACE(sizeof(int));
	*(int *)CMSG_DATA(cmp) = fd[0];

	/* pass the fd */
	if (rump_sys_sendmsg(s2, &msg, 0) == -1)
		atf_tc_fail_errno("sendmsg failed");

	/*
	 * We will read to the same cmsg space.  Overwrite the space
	 * with an invalid fd to make sure we get an explicit error
	 * if we don't manage to read the fd.
	 */
	*(int *)CMSG_DATA(cmp) = -1;

	/* switch back to original proc */
	rump_pub_lwproc_switch(l1);

	/* accept connection and read fd */
	sl = sizeof(sun);
	sgot = rump_sys_accept(s1, (struct sockaddr *)&sun, &sl);
	if (sgot == -1)
		atf_tc_fail_errno("accept");
	if (rump_sys_recvmsg(sgot, &msg, 0) == -1)
		atf_tc_fail_errno("recvmsg failed");
	rfd = *(int *)CMSG_DATA(cmp);

	/* read from the fd */
	memset(buf, 0, sizeof(buf));
	if (rump_sys_read(rfd, buf, sizeof(buf)) == -1)
		atf_tc_fail_errno("read rfd");

	/* check that we got the right stuff */
	if (strcmp(buf, MAGICSTRING) != 0)
		atf_tc_fail("expected \"%s\", got \"%s\"", MAGICSTRING, buf);
}

ATF_TC(sock_cloexec);
ATF_TC_HEAD(sock_cloexec, tc)
{
	atf_tc_set_md_var(tc, "descr", "SOCK_CLOEXEC kernel invariant failure");
}

ATF_TC_BODY(sock_cloexec, tc)
{

	rump_init();
	rump_pub_lwproc_rfork(RUMP_RFFDG);
	if (rump_sys_socket(-1, SOCK_CLOEXEC, 0) != -1)
		atf_tc_fail("invalid socket parameters unexpectedly worked");
	rump_pub_lwproc_releaselwp();
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, cmsg_sendfd);
	ATF_TP_ADD_TC(tp, cmsg_sendfd_bounds);
	ATF_TP_ADD_TC(tp, sock_cloexec);

	return atf_no_error();
}
