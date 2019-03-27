/*	$FreeBSD$	*/

/*
 * resend.c (C) 1995-1998 Darren Reed
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 */
#if !defined(lint)
static const char sccsid[] = "@(#)resend.c	1.3 1/11/96 (C)1995 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif
#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
# include <netinet/ip_var.h>
# include <netinet/if_ether.h>
#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "ipsend.h"

extern	int	opts;

void	dumppacket __P((ip_t *));


void dumppacket(ip)
	ip_t	*ip;
{
	tcphdr_t *t;
	int i, j;

	t = (tcphdr_t *)((char *)ip + (IP_HL(ip) << 2));
	if (ip->ip_tos)
		printf("tos %#x ", ip->ip_tos);
	if (ip->ip_off & 0x3fff)
		printf("frag @%#x ", (ip->ip_off & 0x1fff) << 3);
	printf("len %d id %d ", ip->ip_len, ip->ip_id);
	printf("ttl %d p %d src %s", ip->ip_ttl, ip->ip_p,
		inet_ntoa(ip->ip_src));
	if (ip->ip_p == IPPROTO_TCP || ip->ip_p == IPPROTO_UDP)
		printf(",%d", t->th_sport);
	printf(" dst %s", inet_ntoa(ip->ip_dst));
	if (ip->ip_p == IPPROTO_TCP || ip->ip_p == IPPROTO_UDP)
		printf(",%d", t->th_dport);
	if (ip->ip_p == IPPROTO_TCP) {
		printf(" seq %lu:%lu flags ",
			(u_long)t->th_seq, (u_long)t->th_ack);
		for (j = 0, i = 1; i < 256; i *= 2, j++)
			if (t->th_flags & i)
				printf("%c", "FSRPAU--"[j]);
	}
	putchar('\n');
}


int	ip_resend(dev, mtu, r, gwip, datain)
	char	*dev;
	int	mtu;
	struct	in_addr	gwip;
	struct	ipread	*r;
	char	*datain;
{
	ether_header_t	*eh;
	char	dhost[6];
	ip_t	*ip;
	int	fd, wfd = initdevice(dev, 5), len, i;
	mb_t	mb;

	if (wfd == -1)
		return -1;

	if (datain)
		fd = (*r->r_open)(datain);
	else
		fd = (*r->r_open)("-");

	if (fd < 0)
		exit(-1);

	ip = (struct ip *)mb.mb_buf;
	eh = (ether_header_t *)malloc(sizeof(*eh));
	if(!eh)
	    {
		perror("malloc failed");
		return -2;
	    }

	bzero((char *) &eh->ether_shost, sizeof(eh->ether_shost));
	if (gwip.s_addr && (arp((char *)&gwip, dhost) == -1))
	    {
		perror("arp");
		free(eh);
		return -2;
	    }

	while ((i = (*r->r_readip)(&mb, NULL, NULL)) > 0)
	    {
		if (!(opts & OPT_RAW)) {
			len = ntohs(ip->ip_len);
			eh = (ether_header_t *)realloc((char *)eh, sizeof(*eh) + len);
			eh->ether_type = htons((u_short)ETHERTYPE_IP);
			if (!gwip.s_addr) {
				if (arp((char *)&gwip,
					(char *) &eh->ether_dhost) == -1) {
					perror("arp");
					continue;
				}
			} else
				bcopy(dhost, (char *) &eh->ether_dhost,
				      sizeof(dhost));
			if (!ip->ip_sum)
				ip->ip_sum = chksum((u_short *)ip,
						    IP_HL(ip) << 2);
			bcopy(ip, (char *)(eh + 1), len);
			len += sizeof(*eh);
			dumppacket(ip);
		} else {
			eh = (ether_header_t *)mb.mb_buf;
			len = i;
		}

		if (sendip(wfd, (char *)eh, len) == -1)
		    {
			perror("send_packet");
			break;
		    }
	    }
	(*r->r_close)();
	free(eh);
	return 0;
}
