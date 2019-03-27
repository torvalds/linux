/*	$NetBSD: netconfig.c,v 1.9 2017/01/13 21:30:42 christos Exp $	*/

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
__RCSID("$NetBSD: netconfig.c,v 1.9 2017/01/13 21:30:42 christos Exp $");
#endif /* not lint */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <arpa/inet.h>

#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#include <atf-c.h>
#include <err.h>
#include <errno.h>
#include <string.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include "h_macros.h"

int noatf;

static void __unused
netcfg_rump_makeshmif(const char *busname, char *ifname)
{
	int rv, ifnum;

	if ((rv = rump_pub_shmif_create(busname, &ifnum)) != 0) {
		if (noatf)
			err(1, "makeshmif: rump_pub_shmif_create %d", rv);
		else
			atf_tc_fail("makeshmif: rump_pub_shmif_create %d", rv);
	}
	sprintf(ifname, "shmif%d", ifnum);
}

static void __unused
netcfg_rump_if(const char *ifname, const char *addr, const char *mask)
{
	struct ifaliasreq ia;
	struct sockaddr_in *sin;
	in_addr_t inaddr, inmask;
	int s, rv;

	s = -1;
	if ((s = rump_sys_socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
		if (noatf)
			err(1, "if config socket");
		else
			atf_tc_fail_errno("if config socket");
	}

	inaddr = inet_addr(addr);
	inmask = inet_addr(mask);

	/* Address */
	memset(&ia, 0, sizeof(ia));
	strcpy(ia.ifra_name, ifname);
	sin = (struct sockaddr_in *)&ia.ifra_addr;
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(struct sockaddr_in);
	sin->sin_addr.s_addr = inaddr;

	/* Netmask */
	sin = (struct sockaddr_in *)&ia.ifra_mask;
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(struct sockaddr_in);
	sin->sin_addr.s_addr = inmask;

	/* Broadcast address */
	sin = (struct sockaddr_in *)&ia.ifra_broadaddr;
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(struct sockaddr_in);
	sin->sin_addr.s_addr = inaddr | ~inmask;

	rv = rump_sys_ioctl(s, SIOCAIFADDR, &ia);
	if (rv == -1) {
		if (noatf)
			err(1, "SIOCAIFADDR");
		else
			atf_tc_fail_errno("SIOCAIFADDR");
	}
	rump_sys_close(s);
}

static void __unused
netcfg_rump_route(const char *dst, const char *mask, const char *gw)
{
	size_t len;
	struct {
		struct rt_msghdr m_rtm;
		uint8_t m_space[512];
	} m_rtmsg;
#define rtm m_rtmsg.m_rtm
	uint8_t *bp = m_rtmsg.m_space;
	struct sockaddr_in sinstore;
	int s, rv;

	s = rump_sys_socket(PF_ROUTE, SOCK_RAW, 0);
	if (s == -1) {
		if (noatf)
			err(1, "routing socket");
		else
			atf_tc_fail_errno("routing socket");
	}

	memset(&m_rtmsg, 0, sizeof(m_rtmsg));
	rtm.rtm_type = RTM_ADD;
	rtm.rtm_flags = RTF_UP | RTF_GATEWAY | RTF_STATIC;
	rtm.rtm_version = RTM_VERSION;
	rtm.rtm_seq = 2;
	rtm.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;

	/* dst */
	memset(&sinstore, 0, sizeof(sinstore));
	sinstore.sin_family = AF_INET;
	sinstore.sin_len = sizeof(sinstore);
	sinstore.sin_addr.s_addr = inet_addr(dst);
	memcpy(bp, &sinstore, sizeof(sinstore));
	bp += sizeof(sinstore);

	/* gw */
	memset(&sinstore, 0, sizeof(sinstore));
	sinstore.sin_family = AF_INET;
	sinstore.sin_len = sizeof(sinstore);
	sinstore.sin_addr.s_addr = inet_addr(gw);
	memcpy(bp, &sinstore, sizeof(sinstore));
	bp += sizeof(sinstore);

	/* netmask */
	memset(&sinstore, 0, sizeof(sinstore));
	sinstore.sin_family = AF_INET;
	sinstore.sin_len = sizeof(sinstore);
	sinstore.sin_addr.s_addr = inet_addr(mask);
	memcpy(bp, &sinstore, sizeof(sinstore));
	bp += sizeof(sinstore);

	len = bp - (uint8_t *)&m_rtmsg;
	rtm.rtm_msglen = len;

	rv = rump_sys_write(s, &m_rtmsg, len);
	if (rv != (int)len) {
		if (noatf)
			err(1, "write routing message");
		else
			atf_tc_fail_errno("write routing message");
	}
	rump_sys_close(s);
}

static bool __unused
netcfg_rump_pingtest(const char *dst, int ms_timo)
{
	struct timeval tv;
	struct sockaddr_in sin;
	struct icmp icmp;
	socklen_t slen;
	int s;
	bool rv = false;

	s = rump_sys_socket(PF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (s == -1)
		return false;
	tv.tv_sec = ms_timo / 1000;
	tv.tv_usec = 1000 * (ms_timo % 1000);
	if (rump_sys_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
	    &tv, sizeof(tv)) == -1)
		goto out;

	memset(&sin, 0, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = inet_addr(dst);

	memset(&icmp, 0, sizeof(icmp));
	icmp.icmp_type = ICMP_ECHO;
	icmp.icmp_id = htons(37); 
	icmp.icmp_cksum = htons(0xf7da); /* precalc */

	slen = sizeof(sin);
	if (rump_sys_sendto(s, &icmp, sizeof(icmp), 0,
	    (struct sockaddr *)&sin, slen) == -1) {
		goto out;
	}

	if (rump_sys_recvfrom(s, &icmp, sizeof(icmp), 0,
	    (struct sockaddr *)&sin, &slen) == -1)
		goto out;

	rv = true;
 out:
	rump_sys_close(s);
	return rv;
}
