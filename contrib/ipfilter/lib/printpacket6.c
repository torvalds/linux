/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */

#include "ipf.h"

/*
 * This is meant to work without the IPv6 header files being present or
 * the inet_ntop() library.
 */
void
printpacket6(dir, m)
	int dir;
	mb_t *m;
{
	u_char *buf, p;
	u_short plen, *addrs;
	tcphdr_t *tcp;
	u_32_t flow;

	buf = (u_char *)m->mb_data;
	tcp = (tcphdr_t *)(buf + 40);
	p = buf[6];
	flow = ntohl(*(u_32_t *)buf);
	flow &= 0xfffff;
	plen = ntohs(*((u_short *)buf +2));
	addrs = (u_short *)buf + 4;

	if (dir)
		PRINTF("> ");
	else
		PRINTF("< ");

	PRINTF("%s ", IFNAME(m->mb_ifp));

	PRINTF("ip6/%d %d %#x %d", buf[0] & 0xf, plen, flow, p);
	PRINTF(" %x:%x:%x:%x:%x:%x:%x:%x",
		ntohs(addrs[0]), ntohs(addrs[1]), ntohs(addrs[2]),
		ntohs(addrs[3]), ntohs(addrs[4]), ntohs(addrs[5]),
		ntohs(addrs[6]), ntohs(addrs[7]));
	if (plen >= 4)
		if (p == IPPROTO_TCP || p == IPPROTO_UDP)
			(void)PRINTF(",%d", ntohs(tcp->th_sport));
	PRINTF(" >");
	addrs += 8;
	PRINTF(" %x:%x:%x:%x:%x:%x:%x:%x",
		ntohs(addrs[0]), ntohs(addrs[1]), ntohs(addrs[2]),
		ntohs(addrs[3]), ntohs(addrs[4]), ntohs(addrs[5]),
		ntohs(addrs[6]), ntohs(addrs[7]));
	if (plen >= 4)
		if (p == IPPROTO_TCP || p == IPPROTO_UDP)
			PRINTF(",%d", ntohs(tcp->th_dport));
	putchar('\n');
}
