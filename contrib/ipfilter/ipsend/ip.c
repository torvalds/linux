/*	$FreeBSD$	*/

/*
 * ip.c (C) 1995-1998 Darren Reed
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
#if !defined(lint)
static const char sccsid[] = "%W% %G% (C)1995";
static const char rcsid[] = "@(#)$Id$";
#endif
#include <sys/param.h>
#include <sys/types.h>
#include <netinet/in_systm.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/param.h>
# include <net/route.h>
# include <netinet/if_ether.h>
# include <netinet/ip_var.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "ipsend.h"


static	char	*ipbuf = NULL, *ethbuf = NULL;


u_short	chksum(buf,len)
	u_short	*buf;
	int	len;
{
	u_long	sum = 0;
	int	nwords = len >> 1;

	for(; nwords > 0; nwords--)
		sum += *buf++;
	sum = (sum>>16) + (sum & 0xffff);
	sum += (sum >>16);
	return (~sum);
}


int	send_ether(nfd, buf, len, gwip)
	int	nfd, len;
	char	*buf;
	struct	in_addr	gwip;
{
	static	struct	in_addr	last_gw;
	static	char	last_arp[6] = { 0, 0, 0, 0, 0, 0};
	ether_header_t	*eh;
	char	*s;
	int	err;

	if (!ethbuf)
		ethbuf = (char *)calloc(1, 65536+1024);
	s = ethbuf;
	eh = (ether_header_t *)s;

	bcopy((char *)buf, s + sizeof(*eh), len);
	if (gwip.s_addr == last_gw.s_addr)
	    {
		bcopy(last_arp, (char *) &eh->ether_dhost, 6);
	    }
	else if (arp((char *)&gwip, (char *) &eh->ether_dhost) == -1)
	    {
		perror("arp");
		return -2;
	    }
	eh->ether_type = htons(ETHERTYPE_IP);
	last_gw.s_addr = gwip.s_addr;
	err = sendip(nfd, s, sizeof(*eh) + len);
	return err;
}


/*
 */
int	send_ip(nfd, mtu, ip, gwip, frag)
	int	nfd, mtu;
	ip_t	*ip;
	struct	in_addr	gwip;
	int	frag;
{
	static	struct	in_addr	last_gw, local_ip;
	static	char	local_arp[6] = { 0, 0, 0, 0, 0, 0};
	static	char	last_arp[6] = { 0, 0, 0, 0, 0, 0};
	static	u_short	id = 0;
	ether_header_t	*eh;
	ip_t	ipsv;
	int	err, iplen;

	if (!ipbuf)
	  {
		ipbuf = (char *)malloc(65536);
		if (!ipbuf)
		  {
			perror("malloc failed");
			return -2;
		  }
	  }

	eh = (ether_header_t *)ipbuf;

	bzero((char *) &eh->ether_shost, sizeof(eh->ether_shost));
	if (last_gw.s_addr && (gwip.s_addr == last_gw.s_addr))
	    {
		bcopy(last_arp, (char *) &eh->ether_dhost, 6);
	    }
	else if (arp((char *)&gwip, (char *) &eh->ether_dhost) == -1)
	    {
		perror("arp");
		return -2;
	    }
	bcopy((char *) &eh->ether_dhost, last_arp, sizeof(last_arp));
	eh->ether_type = htons(ETHERTYPE_IP);

	bcopy((char *)ip, (char *)&ipsv, sizeof(*ip));
	last_gw.s_addr = gwip.s_addr;
	iplen = ip->ip_len;
	ip->ip_len = htons(iplen);
	if (!(frag & 2)) {
		if (!IP_V(ip))
			IP_V_A(ip, IPVERSION);
		if (!ip->ip_id)
			ip->ip_id  = htons(id++);
		if (!ip->ip_ttl)
			ip->ip_ttl = 60;
	}

	if (ip->ip_src.s_addr != local_ip.s_addr) {
		(void) arp((char *)&ip->ip_src, (char *) &local_arp);
		bcopy(local_arp, (char *) &eh->ether_shost,sizeof(last_arp));
		local_ip = ip->ip_src;
	} else
		bcopy(local_arp, (char *) &eh->ether_shost, 6);

	if (!frag || (sizeof(*eh) + iplen < mtu))
	    {
		ip->ip_sum = 0;
		ip->ip_sum = chksum((u_short *)ip, IP_HL(ip) << 2);

		bcopy((char *)ip, ipbuf + sizeof(*eh), iplen);
		err =  sendip(nfd, ipbuf, sizeof(*eh) + iplen);
	    }
	else
	    {
		/*
		 * Actually, this is bogus because we're putting all IP
		 * options in every packet, which isn't always what should be
		 * done.  Will do for now.
		 */
		ether_header_t	eth;
		char	optcpy[48], ol;
		char	*s;
		int	i, sent = 0, ts, hlen, olen;

		hlen = IP_HL(ip) << 2;
		if (mtu < (hlen + 8)) {
			fprintf(stderr, "mtu (%d) < ip header size (%d) + 8\n",
				mtu, hlen);
			fprintf(stderr, "can't fragment data\n");
			return -2;
		}
		ol = (IP_HL(ip) << 2) - sizeof(*ip);
		for (i = 0, s = (char*)(ip + 1); ol > 0; )
			if (*s == IPOPT_EOL) {
				optcpy[i++] = *s;
				break;
			} else if (*s == IPOPT_NOP) {
				s++;
				ol--;
			} else
			    {
				olen = (int)(*(u_char *)(s + 1));
				ol -= olen;
				if (IPOPT_COPIED(*s))
				    {
					bcopy(s, optcpy + i, olen);
					i += olen;
					s += olen;
				    }
			    }
		if (i)
		    {
			/*
			 * pad out
			 */
			while ((i & 3) && (i & 3) != 3)
				optcpy[i++] = IPOPT_NOP;
			if ((i & 3) == 3)
				optcpy[i++] = IPOPT_EOL;
		    }

		bcopy((char *)eh, (char *)&eth, sizeof(eth));
		s = (char *)ip + hlen;
		iplen = ntohs(ip->ip_len) - hlen;
		ip->ip_off |= htons(IP_MF);

		while (1)
		    {
			if ((sent + (mtu - hlen)) >= iplen)
			    {
				ip->ip_off ^= htons(IP_MF);
				ts = iplen - sent;
			    }
			else
				ts = (mtu - hlen);
			ip->ip_off &= htons(0xe000);
			ip->ip_off |= htons(sent >> 3);
			ts += hlen;
			ip->ip_len = htons(ts);
			ip->ip_sum = 0;
			ip->ip_sum = chksum((u_short *)ip, hlen);
			bcopy((char *)ip, ipbuf + sizeof(*eh), hlen);
			bcopy(s + sent, ipbuf + sizeof(*eh) + hlen, ts - hlen);
			err =  sendip(nfd, ipbuf, sizeof(*eh) + ts);

			bcopy((char *)&eth, ipbuf, sizeof(eth));
			sent += (ts - hlen);
			if (!(ntohs(ip->ip_off) & IP_MF))
				break;
			else if (!(ip->ip_off & htons(0x1fff)))
			    {
				hlen = i + sizeof(*ip);
				IP_HL_A(ip, (sizeof(*ip) + i) >> 2);
				bcopy(optcpy, (char *)(ip + 1), i);
			    }
		    }
	    }

	bcopy((char *)&ipsv, (char *)ip, sizeof(*ip));
	return err;
}


/*
 * send a tcp packet.
 */
int	send_tcp(nfd, mtu, ip, gwip)
	int	nfd, mtu;
	ip_t	*ip;
	struct	in_addr	gwip;
{
	static	tcp_seq	iss = 2;
	tcphdr_t *t, *t2;
	int	thlen, i, iplen, hlen;
	u_32_t	lbuf[20];
	ip_t	*ip2;

	iplen = ip->ip_len;
	hlen = IP_HL(ip) << 2;
	t = (tcphdr_t *)((char *)ip + hlen);
	ip2 = (struct ip *)lbuf;
	t2 = (tcphdr_t *)((char *)ip2 + hlen);
	thlen = TCP_OFF(t) << 2;
	if (!thlen)
		thlen = sizeof(tcphdr_t);
	bzero((char *)ip2, sizeof(*ip2) + sizeof(*t2));
	ip->ip_p = IPPROTO_TCP;
	ip2->ip_p = ip->ip_p;
	ip2->ip_src = ip->ip_src;
	ip2->ip_dst = ip->ip_dst;
	bcopy((char *)ip + hlen, (char *)t2, thlen);

	if (!t2->th_win)
		t2->th_win = htons(4096);
	iss += 63;

	i = sizeof(struct tcpiphdr) / sizeof(long);

	if ((t2->th_flags == TH_SYN) && !ntohs(ip->ip_off) &&
	    (lbuf[i] != htonl(0x020405b4))) {
		lbuf[i] = htonl(0x020405b4);
		bcopy((char *)ip + hlen + thlen, (char *)ip + hlen + thlen + 4,
		      iplen - thlen - hlen);
		thlen += 4;
	    }
	TCP_OFF_A(t2, thlen >> 2);
	ip2->ip_len = htons(thlen);
	ip->ip_len = hlen + thlen;
	t2->th_sum = 0;
	t2->th_sum = chksum((u_short *)ip2, thlen + sizeof(ip_t));

	bcopy((char *)t2, (char *)ip + hlen, thlen);
	return send_ip(nfd, mtu, ip, gwip, 1);
}


/*
 * send a udp packet.
 */
int	send_udp(nfd, mtu, ip, gwip)
	int	nfd, mtu;
	ip_t	*ip;
	struct	in_addr	gwip;
{
	struct	tcpiphdr *ti;
	int	thlen;
	u_long	lbuf[20];

	ti = (struct tcpiphdr *)lbuf;
	bzero((char *)ti, sizeof(*ti));
	thlen = sizeof(udphdr_t);
	ti->ti_pr = ip->ip_p;
	ti->ti_src = ip->ip_src;
	ti->ti_dst = ip->ip_dst;
	bcopy((char *)ip + (IP_HL(ip) << 2),
	      (char *)&ti->ti_sport, sizeof(udphdr_t));

	ti->ti_len = htons(thlen);
	ip->ip_len = (IP_HL(ip) << 2) + thlen;
	ti->ti_sum = 0;
	ti->ti_sum = chksum((u_short *)ti, thlen + sizeof(ip_t));

	bcopy((char *)&ti->ti_sport,
	      (char *)ip + (IP_HL(ip) << 2), sizeof(udphdr_t));
	return send_ip(nfd, mtu, ip, gwip, 1);
}


/*
 * send an icmp packet.
 */
int	send_icmp(nfd, mtu, ip, gwip)
	int	nfd, mtu;
	ip_t	*ip;
	struct	in_addr	gwip;
{
	struct	icmp	*ic;

	ic = (struct icmp *)((char *)ip + (IP_HL(ip) << 2));

	ic->icmp_cksum = 0;
	ic->icmp_cksum = chksum((u_short *)ic, sizeof(struct icmp));

	return send_ip(nfd, mtu, ip, gwip, 1);
}


int	send_packet(nfd, mtu, ip, gwip)
	int	nfd, mtu;
	ip_t	*ip;
	struct	in_addr	gwip;
{
        switch (ip->ip_p)
        {
        case IPPROTO_TCP :
                return send_tcp(nfd, mtu, ip, gwip);
        case IPPROTO_UDP :
                return send_udp(nfd, mtu, ip, gwip);
        case IPPROTO_ICMP :
                return send_icmp(nfd, mtu, ip, gwip);
        default :
                return send_ip(nfd, mtu, ip, gwip, 1);
        }
}
