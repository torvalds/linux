/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */
#if !defined(lint)
static const char sccsid[] = "@(#)ipft_tx.c	1.7 6/5/96 (C) 1993 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif

#include <ctype.h>

#include "ipf.h"
#include "ipt.h"

extern	int	opts;

static	char	*tx_proto = "";

static	int	text_open __P((char *)), text_close __P((void));
static	int	text_readip __P((mb_t *, char **, int *));
static	int	parseline __P((char *, ip_t *, char **, int *));

static	char	myflagset[] = "FSRPAUEC";
static	u_char	myflags[] = { TH_FIN, TH_SYN, TH_RST, TH_PUSH,
				TH_ACK, TH_URG, TH_ECN, TH_CWR };

struct	ipread	iptext = { text_open, text_close, text_readip, R_DO_CKSUM };
static	FILE	*tfp = NULL;
static	int	tfd = -1;

static	u_32_t	tx_hostnum __P((char *, int *));
static	u_short	tx_portnum __P((char *));

#ifdef USE_INET6
int parseipv6 __P((char **, ip6_t *, char **, int *));
#endif

/*
 * returns an ip address as a long var as a result of either a DNS lookup or
 * straight inet_addr() call
 */
static	u_32_t	tx_hostnum(host, resolved)
	char	*host;
	int	*resolved;
{
	i6addr_t	ipa;

	*resolved = 0;
	if (!strcasecmp("any", host))
		return 0L;
	if (ISDIGIT(*host))
		return inet_addr(host);

	if (gethost(AF_INET, host, &ipa) == -1) {
		*resolved = -1;
		fprintf(stderr, "can't resolve hostname: %s\n", host);
		return 0;
	}
	return ipa.in4.s_addr;
}


/*
 * find the port number given by the name, either from getservbyname() or
 * straight atoi()
 */
static	u_short	tx_portnum(name)
	char	*name;
{
	struct	servent	*sp;

	if (ISDIGIT(*name))
		return (u_short)atoi(name);
	sp = getservbyname(name, tx_proto);
	if (sp)
		return ntohs(sp->s_port);
	(void) fprintf(stderr, "unknown service \"%s\".\n", name);
	return 0;
}


static	int	text_open(fname)
	char	*fname;
{
	if (tfp && tfd != -1) {
		rewind(tfp);
		return tfd;
	}

	if (!strcmp(fname, "-")) {
		tfd = 0;
		tfp = stdin;
	} else {
		tfd = open(fname, O_RDONLY);
		if (tfd != -1)
			tfp = fdopen(tfd, "r");
	}
	return tfd;
}


static	int	text_close()
{
	int	cfd = tfd;

	tfd = -1;
	return close(cfd);
}


static	int	text_readip(mb, ifn, dir)
	mb_t	*mb;
	char	**ifn;
	int	*dir;
{
	register char *s;
	char	line[513];
	ip_t	*ip;
	char	*buf;
	int	cnt;

	buf = (char *)mb->mb_buf;
	cnt = sizeof(mb->mb_buf);

	*ifn = NULL;
	while (fgets(line, sizeof(line)-1, tfp)) {
		if ((s = strchr(line, '\n')))
			*s = '\0';
		if ((s = strchr(line, '\r')))
			*s = '\0';
		if ((s = strchr(line, '#')))
			*s = '\0';
		if (!*line)
			continue;
		if ((opts & OPT_DEBUG) != 0)
			printf("input: %s\n", line);
		*ifn = NULL;
		*dir = 0;
		if (!parseline(line, (ip_t *)buf, ifn, dir)) {
			ip = (ip_t *)buf;
			if (IP_V(ip) == 6) {
#ifdef USE_INET6
				mb->mb_len = ntohs(((ip6_t *)ip)->ip6_plen) +
				       sizeof(ip6_t);
#else
				mb->mb_len = 0;
#endif
			} else {
				mb->mb_len = ntohs(ip->ip_len);
			}
			return mb->mb_len;
		}
	}
	if (feof(tfp))
		return 0;
	return -1;
}

static	int	parseline(line, ip, ifn, out)
	char	*line;
	ip_t	*ip;
	char	**ifn;
	int	*out;
{
	tcphdr_t	th, *tcp = &th;
	struct	icmp	icmp, *ic = &icmp;
	char	*cps[20], **cpp, c, ipopts[68];
	int	i, r;

	if (*ifn)
		free(*ifn);
	bzero((char *)ip, MAX(sizeof(*tcp), sizeof(*ic)) + sizeof(*ip));
	bzero((char *)tcp, sizeof(*tcp));
	bzero((char *)ic, sizeof(*ic));
	bzero(ipopts, sizeof(ipopts));
	IP_HL_A(ip, sizeof(*ip) >> 2);
	IP_V_A(ip, IPVERSION);
	ip->ip_ttl = 63;
	for (i = 0, cps[0] = strtok(line, " \b\t\r\n"); cps[i] && i < 19; )
		cps[++i] = strtok(NULL, " \b\t\r\n");

	cpp = cps;
	if (!*cpp)
		return 1;

	c = **cpp;
	if (!ISALPHA(c) || (TOLOWER(c) != 'o' && TOLOWER(c) != 'i')) {
		fprintf(stderr, "bad direction \"%s\"\n", *cpp);
		return 1;
	}

#ifdef USE_INET6
	if (!strcasecmp(*cpp, "out6") || !strcasecmp(*cpp, "in6")) {
		return parseipv6(cpp, (ip6_t *)ip, ifn, out);
	}
#endif

	*out = (TOLOWER(c) == 'o') ? 1 : 0;
	cpp++;
	if (!*cpp)
		return 1;

	if (!strcasecmp(*cpp, "on")) {
		cpp++;
		if (!*cpp)
			return 1;
		*ifn = strdup(*cpp++);
		if (!*cpp)
			return 1;
	}

	c = **cpp;
	ip->ip_len = sizeof(ip_t);
	if (!strcasecmp(*cpp, "tcp") || !strcasecmp(*cpp, "udp") ||
	    !strcasecmp(*cpp, "icmp")) {
		if (c == 't') {
			ip->ip_p = IPPROTO_TCP;
			ip->ip_len += sizeof(struct tcphdr);
			tx_proto = "tcp";
		} else if (c == 'u') {
			ip->ip_p = IPPROTO_UDP;
			ip->ip_len += sizeof(struct udphdr);
			tx_proto = "udp";
		} else {
			ip->ip_p = IPPROTO_ICMP;
			ip->ip_len += ICMPERR_IPICMPHLEN;
			tx_proto = "icmp";
		}
		cpp++;
	} else if (ISDIGIT(**cpp) && !index(*cpp, '.')) {
		ip->ip_p = atoi(*cpp);
		cpp++;
	} else
		ip->ip_p = IPPROTO_IP;

	if (!*cpp)
		return 1;
	if (ip->ip_p == IPPROTO_TCP || ip->ip_p == IPPROTO_UDP) {
		char	*last;

		last = strchr(*cpp, ',');
		if (!last) {
			fprintf(stderr, "tcp/udp with no source port\n");
			return 1;
		}
		*last++ = '\0';
		tcp->th_sport = htons(tx_portnum(last));
		if (ip->ip_p == IPPROTO_TCP) {
			tcp->th_win = htons(4096);
			TCP_OFF_A(tcp, sizeof(*tcp) >> 2);
		}
	}
	ip->ip_src.s_addr = tx_hostnum(*cpp, &r);
	cpp++;
	if (!*cpp)
		return 1;

	if (ip->ip_p == IPPROTO_TCP || ip->ip_p == IPPROTO_UDP) {
		char	*last;

		last = strchr(*cpp, ',');
		if (!last) {
			fprintf(stderr, "tcp/udp with no destination port\n");
			return 1;
		}
		*last++ = '\0';
		tcp->th_dport = htons(tx_portnum(last));
	}
	ip->ip_dst.s_addr = tx_hostnum(*cpp, &r);
	cpp++;
	if (ip->ip_p == IPPROTO_TCP) {
		if (*cpp != NULL) {
			char	*s, *t;

			tcp->th_flags = 0;
			for (s = *cpp; *s; s++)
				if ((t  = strchr(myflagset, *s)))
					tcp->th_flags |= myflags[t-myflagset];
			if (tcp->th_flags)
				cpp++;
		}

		if (tcp->th_flags & TH_URG)
			tcp->th_urp = htons(1);

		if (*cpp && !strncasecmp(*cpp, "seq=", 4)) {
			tcp->th_seq = htonl(atoi(*cpp + 4));
			cpp++;
		}

		if (*cpp && !strncasecmp(*cpp, "ack=", 4)) {
			tcp->th_ack = htonl(atoi(*cpp + 4));
			cpp++;
		}
	} else if (*cpp && ip->ip_p == IPPROTO_ICMP) {
		char	*t;

		t = strchr(*cpp, ',');
		if (t != NULL)
			*t = '\0';

		ic->icmp_type = geticmptype(AF_INET, *cpp);
		if (t != NULL)
			ic->icmp_code = atoi(t + 1);
		cpp++;

		if (ic->icmp_type == ICMP_ECHO ||
		    ic->icmp_type == ICMP_ECHOREPLY)
			ic->icmp_id = htons(getpid());
		if (t != NULL)
			*t = ',';
	}

	if (*cpp && !strcasecmp(*cpp, "opt")) {
		u_long	olen;

		cpp++;
		olen = buildopts(*cpp, ipopts, (IP_HL(ip) - 5) << 2);
		if (olen) {
			bcopy(ipopts, (char *)(ip + 1), olen);
			IP_HL_A(ip, IP_HL(ip) + (olen >> 2));
			ip->ip_len += olen;
		}
	}
	if (ip->ip_p == IPPROTO_TCP || ip->ip_p == IPPROTO_UDP)
		bcopy((char *)tcp, ((char *)ip) + (IP_HL(ip) << 2),
			sizeof(*tcp));
	else if (ip->ip_p == IPPROTO_ICMP)
		bcopy((char *)ic, ((char *)ip) + (IP_HL(ip) << 2),
			sizeof(*ic));
	ip->ip_len = htons(ip->ip_len);
	return 0;
}


#ifdef USE_INET6
int parseipv6(cpp, ip6, ifn, out)
	char **cpp;
	ip6_t *ip6;
	char **ifn;
	int *out;
{
	tcphdr_t th, *tcp = &th;
	struct icmp6_hdr icmp, *ic6 = &icmp;

	bzero((char *)ip6, MAX(sizeof(*tcp), sizeof(*ic6)) + sizeof(*ip6));
	bzero((char *)tcp, sizeof(*tcp));
	bzero((char *)ic6, sizeof(*ic6));
	ip6->ip6_vfc = 0x60;

	*out = (**cpp == 'o') ? 1 : 0;
	cpp++;
	if (!*cpp)
		return 1;

	if (!strcasecmp(*cpp, "on")) {
		cpp++;
		if (!*cpp)
			return 1;
		*ifn = strdup(*cpp++);
		if (!*cpp)
			return 1;
	}

	if (!strcasecmp(*cpp, "tcp")) {
		ip6->ip6_nxt = IPPROTO_TCP;
		tx_proto = "tcp";
		cpp++;
	} else if (!strcasecmp(*cpp, "udp")) {
		ip6->ip6_nxt = IPPROTO_UDP;
		tx_proto = "udp";
		cpp++;
	} else if (!strcasecmp(*cpp, "icmpv6")) {
		ip6->ip6_nxt = IPPROTO_ICMPV6;
		tx_proto = "icmpv6";
		cpp++;
	} else if (ISDIGIT(**cpp) && !index(*cpp, ':')) {
		ip6->ip6_nxt = atoi(*cpp);
		cpp++;
	} else
		ip6->ip6_nxt = IPPROTO_IPV6;

	if (!*cpp)
		return 1;

	switch (ip6->ip6_nxt)
	{
	case IPPROTO_TCP :
		ip6->ip6_plen = sizeof(struct tcphdr);
		break;
	case IPPROTO_UDP :
		ip6->ip6_plen = sizeof(struct udphdr);
		break;
	case IPPROTO_ICMPV6 :
		ip6->ip6_plen = ICMP6ERR_IPICMPHLEN;
		break;
	default :
		break;
	}

	if (ip6->ip6_nxt == IPPROTO_TCP || ip6->ip6_nxt == IPPROTO_UDP) {
		char *last;

		last = strchr(*cpp, ',');
		if (!last) {
			fprintf(stderr, "tcp/udp with no source port\n");
			return 1;
		}
		*last++ = '\0';
		tcp->th_sport = htons(tx_portnum(last));
		if (ip6->ip6_nxt == IPPROTO_TCP) {
			tcp->th_win = htons(4096);
			TCP_OFF_A(tcp, sizeof(*tcp) >> 2);
		}
	}

	if (inet_pton(AF_INET6, *cpp, &ip6->ip6_src) != 1) {
		fprintf(stderr, "cannot parse source address '%s'\n", *cpp);
		return 1;
	}

	cpp++;
	if (!*cpp)
		return 1;

	if (ip6->ip6_nxt == IPPROTO_TCP || ip6->ip6_nxt == IPPROTO_UDP) {
		char *last;

		last = strchr(*cpp, ',');
		if (!last) {
			fprintf(stderr, "tcp/udp with no destination port\n");
			return 1;
		}
		*last++ = '\0';
		tcp->th_dport = htons(tx_portnum(last));
	}

	if (inet_pton(AF_INET6, *cpp, &ip6->ip6_dst) != 1) {
		fprintf(stderr, "cannot parse destination address '%s'\n",
			*cpp);
		return 1;
	}

	cpp++;
	if (ip6->ip6_nxt == IPPROTO_TCP) {
		if (*cpp != NULL) {
			char *s, *t;

			tcp->th_flags = 0;
			for (s = *cpp; *s; s++)
				if ((t  = strchr(myflagset, *s)))
					tcp->th_flags |= myflags[t-myflagset];
			if (tcp->th_flags)
				cpp++;
		}

		if (tcp->th_flags & TH_URG)
			tcp->th_urp = htons(1);

		if (*cpp && !strncasecmp(*cpp, "seq=", 4)) {
			tcp->th_seq = htonl(atoi(*cpp + 4));
			cpp++;
		}

		if (*cpp && !strncasecmp(*cpp, "ack=", 4)) {
			tcp->th_ack = htonl(atoi(*cpp + 4));
			cpp++;
		}
	} else if (*cpp && ip6->ip6_nxt == IPPROTO_ICMPV6) {
		char *t;

		t = strchr(*cpp, ',');
		if (t != NULL)
			*t = '\0';

		ic6->icmp6_type = geticmptype(AF_INET6, *cpp);
		if (t != NULL)
			ic6->icmp6_code = atoi(t + 1);

		if (ic6->icmp6_type == ICMP6_ECHO_REQUEST ||
		    ic6->icmp6_type == ICMP6_ECHO_REPLY)
			ic6->icmp6_id = htons(getpid());

		if (t != NULL)
			*t = ',';
	}

	if (ip6->ip6_nxt == IPPROTO_TCP || ip6->ip6_nxt == IPPROTO_UDP) {
		bcopy((char *)tcp, (char *)ip6 + sizeof(*ip6),
			sizeof(*tcp));
	} else if (ip6->ip6_nxt == IPPROTO_ICMPV6) {
		bcopy((char *)ic6, (char *)ip6 + sizeof(*ip6),
			sizeof(*ic6));
	}

	/*
	 * Because a length of 0 == jumbo gram...
	 */
	if (ip6->ip6_plen == 0) {
		ip6->ip6_plen++;
	}
	ip6->ip6_plen = htons(ip6->ip6_plen);
	return 0;
}
#endif
