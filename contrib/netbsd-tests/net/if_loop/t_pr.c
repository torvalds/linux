/*	$NetBSD: t_pr.c,v 1.8 2017/01/13 21:30:42 christos Exp $	*/

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#ifndef lint
__RCSID("$NetBSD: t_pr.c,v 1.8 2017/01/13 21:30:42 christos Exp $");
#endif /* not lint */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <netinet/in.h>
#include <net/route.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <atf-c.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../config/netconfig.c"
#include "h_macros.h"

/*
 * Prepare rump, configure interface and route to cause fragmentation
 */
static void
setup(void)
{
	char ifname[IFNAMSIZ];
	struct {
		struct rt_msghdr m_rtm;
		struct sockaddr_in m_sin;
	} m_rtmsg;
#define rtm m_rtmsg.m_rtm
#define rsin m_rtmsg.m_sin
	struct ifreq ifr;
	int s;

	rump_init();

	/* first, config lo0 & route */
	strcpy(ifname, "lo0");
	netcfg_rump_if(ifname, "127.0.0.1", "255.0.0.0");
	netcfg_rump_route("127.0.0.1", "255.0.0.0", "127.0.0.1");

	if ((s = rump_sys_socket(PF_ROUTE, SOCK_RAW, 0)) == -1)
		atf_tc_fail_errno("routing socket");

	/*
	 * set MTU for interface so that route MTU doesn't
	 * get overridden by it.
	 */
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, "lo0");
	ifr.ifr_mtu = 1300;
	if (rump_sys_ioctl(s, SIOCSIFMTU, &ifr) == -1)
		atf_tc_fail_errno("set mtu");

	/* change route MTU to 100 */
	memset(&m_rtmsg, 0, sizeof(m_rtmsg));
	rtm.rtm_type = RTM_CHANGE;
	rtm.rtm_flags = RTF_STATIC;
	rtm.rtm_version = RTM_VERSION;
	rtm.rtm_seq = 3;
	rtm.rtm_inits = RTV_MTU;
	rtm.rtm_addrs = RTA_DST;
	rtm.rtm_rmx.rmx_mtu = 100;
	rtm.rtm_msglen = sizeof(m_rtmsg);

	memset(&rsin, 0, sizeof(rsin));
	rsin.sin_family = AF_INET;
	rsin.sin_len = sizeof(rsin);
	rsin.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (rump_sys_write(s, &m_rtmsg, sizeof(m_rtmsg)) != sizeof(m_rtmsg))
		atf_tc_fail_errno("set route mtu");
	rump_sys_close(s);
}

/*
 * Turn on checksums on loopback interfaces
 */
static int
enable_locsums(void)
{
	struct sysctlnode q, ans[256];
	int mib[5], enable;
	size_t alen;
	unsigned i;

	mib[0] = CTL_NET;
	mib[1] = PF_INET;
	mib[2] = IPPROTO_IP;
	mib[3] = CTL_QUERY;
	alen = sizeof(ans);

	memset(&q, 0, sizeof(q));
	q.sysctl_flags = SYSCTL_VERSION;

	if (rump_sys___sysctl(mib, 4, ans, &alen, &q, sizeof(q)) == -1)
		return -1;

	for (i = 0; i < __arraycount(ans); i++)
		if (strcmp("do_loopback_cksum", ans[i].sysctl_name) == 0)
			break;
	if (i == __arraycount(ans)) {
		errno = ENOENT;
		return -1;
	}

	mib[3] = ans[i].sysctl_num;

	enable = 1;
	if (rump_sys___sysctl(mib, 4, NULL, NULL, &enable,
	    sizeof(enable)) == -1)
		return errno;

	return 0;
}

ATF_TC(loopmtu);
ATF_TC_HEAD(loopmtu, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "test lo0 fragmentation (PR kern/43664)");
}

ATF_TC_BODY(loopmtu, tc)
{
	struct sockaddr_in sin;
	char data[2000];
	int s;

	setup();

	/* open raw socket */
	s = rump_sys_socket(PF_INET, SOCK_RAW, 0);
	if (s == -1)
		atf_tc_fail_errno("raw socket");

	/* then, send data */
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_len = sizeof(sin);
	sin.sin_port = htons(12345);
	sin.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (rump_sys_sendto(s, data, sizeof(data), 0,
	    (struct sockaddr *)&sin, sizeof(sin)) == -1)
		atf_tc_fail_errno("sendto failed");
}

ATF_TC(loopmtu_csum);
ATF_TC_HEAD(loopmtu_csum, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "test lo0 fragmentation with checksums (PR kern/43664)");
}

ATF_TC_BODY(loopmtu_csum, tc)
{
	struct sockaddr_in sin;
	char data[2000];
	int s;

	setup();

	ATF_CHECK(enable_locsums() == 0);

	/* open raw socket */
	s = rump_sys_socket(PF_INET, SOCK_RAW, 0);
	if (s == -1)
		atf_tc_fail_errno("raw socket");

	/* then, send data */
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_len = sizeof(sin);
	sin.sin_port = htons(12345);
	sin.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (rump_sys_sendto(s, data, sizeof(data), 0,
	    (struct sockaddr *)&sin, sizeof(sin)) == -1)
		atf_tc_fail_errno("sendto failed");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, loopmtu);
	ATF_TP_ADD_TC(tp, loopmtu_csum);

	return atf_no_error();
}

