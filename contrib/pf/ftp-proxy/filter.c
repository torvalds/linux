/*	$OpenBSD: filter.c,v 1.8 2008/06/13 07:25:26 claudio Exp $ */

/*
 * Copyright (c) 2004, 2005 Camiel Dobbelaar, <cd@sentia.nl>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/pfvar.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "filter.h"

/* From netinet/in.h, but only _KERNEL_ gets them. */
#define satosin(sa)	((struct sockaddr_in *)(sa))
#define satosin6(sa)	((struct sockaddr_in6 *)(sa))

enum { TRANS_FILTER = 0, TRANS_NAT, TRANS_RDR, TRANS_SIZE };

int prepare_rule(u_int32_t, int, struct sockaddr *, struct sockaddr *,
    u_int16_t);
int server_lookup4(struct sockaddr_in *, struct sockaddr_in *,
    struct sockaddr_in *);
int server_lookup6(struct sockaddr_in6 *, struct sockaddr_in6 *,
    struct sockaddr_in6 *);

static struct pfioc_pooladdr	pfp;
static struct pfioc_rule	pfr;
static struct pfioc_trans	pft;
static struct pfioc_trans_e	pfte[TRANS_SIZE];
static int dev, rule_log;
static const char *qname, *tagname;

int
add_filter(u_int32_t id, u_int8_t dir, struct sockaddr *src,
    struct sockaddr *dst, u_int16_t d_port)
{
	if (!src || !dst || !d_port) {
		errno = EINVAL;
		return (-1);
	}

	if (prepare_rule(id, PF_RULESET_FILTER, src, dst, d_port) == -1)
		return (-1);

	pfr.rule.direction = dir;
	if (ioctl(dev, DIOCADDRULE, &pfr) == -1)
		return (-1);

	return (0);
}

int
add_nat(u_int32_t id, struct sockaddr *src, struct sockaddr *dst,
    u_int16_t d_port, struct sockaddr *nat, u_int16_t nat_range_low,
    u_int16_t nat_range_high)
{
	if (!src || !dst || !d_port || !nat || !nat_range_low ||
	    (src->sa_family != nat->sa_family)) {
		errno = EINVAL;
		return (-1);
	}

	if (prepare_rule(id, PF_RULESET_NAT, src, dst, d_port) == -1)
		return (-1);

	if (nat->sa_family == AF_INET) {
		memcpy(&pfp.addr.addr.v.a.addr.v4,
		    &satosin(nat)->sin_addr.s_addr, 4);
		memset(&pfp.addr.addr.v.a.mask.addr8, 255, 4);
	} else {
		memcpy(&pfp.addr.addr.v.a.addr.v6,
		    &satosin6(nat)->sin6_addr.s6_addr, 16);
		memset(&pfp.addr.addr.v.a.mask.addr8, 255, 16);
	}
	if (ioctl(dev, DIOCADDADDR, &pfp) == -1)
		return (-1);

	pfr.rule.rpool.proxy_port[0] = nat_range_low;
	pfr.rule.rpool.proxy_port[1] = nat_range_high;
	if (ioctl(dev, DIOCADDRULE, &pfr) == -1)
		return (-1);

	return (0);
}

int
add_rdr(u_int32_t id, struct sockaddr *src, struct sockaddr *dst,
    u_int16_t d_port, struct sockaddr *rdr, u_int16_t rdr_port)
{
	if (!src || !dst || !d_port || !rdr || !rdr_port ||
	    (src->sa_family != rdr->sa_family)) {
		errno = EINVAL;
		return (-1);
	}

	if (prepare_rule(id, PF_RULESET_RDR, src, dst, d_port) == -1)
		return (-1);

	if (rdr->sa_family == AF_INET) {
		memcpy(&pfp.addr.addr.v.a.addr.v4,
		    &satosin(rdr)->sin_addr.s_addr, 4);
		memset(&pfp.addr.addr.v.a.mask.addr8, 255, 4);
	} else {
		memcpy(&pfp.addr.addr.v.a.addr.v6,
		    &satosin6(rdr)->sin6_addr.s6_addr, 16);
		memset(&pfp.addr.addr.v.a.mask.addr8, 255, 16);
	}
	if (ioctl(dev, DIOCADDADDR, &pfp) == -1)
		return (-1);

	pfr.rule.rpool.proxy_port[0] = rdr_port;
	if (ioctl(dev, DIOCADDRULE, &pfr) == -1)
		return (-1);

	return (0);
}

int
do_commit(void)
{
	if (ioctl(dev, DIOCXCOMMIT, &pft) == -1)
		return (-1);

	return (0);
}

int
do_rollback(void)
{
	if (ioctl(dev, DIOCXROLLBACK, &pft) == -1)
		return (-1);
	
	return (0);
}

void
init_filter(const char *opt_qname, const char *opt_tagname, int opt_verbose)
{
	struct pf_status status;

	qname = opt_qname;
	tagname = opt_tagname;

	if (opt_verbose == 1)
		rule_log = PF_LOG;
	else if (opt_verbose == 2)
		rule_log = PF_LOG_ALL;

	dev = open("/dev/pf", O_RDWR);	
	if (dev == -1)
		err(1, "open /dev/pf");
	if (ioctl(dev, DIOCGETSTATUS, &status) == -1)
		err(1, "DIOCGETSTATUS");
	if (!status.running)
		errx(1, "pf is disabled");
}

int
prepare_commit(u_int32_t id)
{
	char an[PF_ANCHOR_NAME_SIZE];
	int i;

	memset(&pft, 0, sizeof pft);
	pft.size = TRANS_SIZE;
	pft.esize = sizeof pfte[0];
	pft.array = pfte;

	snprintf(an, PF_ANCHOR_NAME_SIZE, "%s/%d.%d", FTP_PROXY_ANCHOR,
	    getpid(), id);
	for (i = 0; i < TRANS_SIZE; i++) {
		memset(&pfte[i], 0, sizeof pfte[0]);
		strlcpy(pfte[i].anchor, an, PF_ANCHOR_NAME_SIZE);
		switch (i) {
		case TRANS_FILTER:
			pfte[i].rs_num = PF_RULESET_FILTER;
			break;
		case TRANS_NAT:
			pfte[i].rs_num = PF_RULESET_NAT;
			break;
		case TRANS_RDR:
			pfte[i].rs_num = PF_RULESET_RDR;
			break;
		default:
			errno = EINVAL;
			return (-1);
		}
	}

	if (ioctl(dev, DIOCXBEGIN, &pft) == -1)
		return (-1);

	return (0);
}
	
int
prepare_rule(u_int32_t id, int rs_num, struct sockaddr *src,
    struct sockaddr *dst, u_int16_t d_port)
{
	char an[PF_ANCHOR_NAME_SIZE];

	if ((src->sa_family != AF_INET && src->sa_family != AF_INET6) ||
	    (src->sa_family != dst->sa_family)) {
	    	errno = EPROTONOSUPPORT;
		return (-1);
	}

	memset(&pfp, 0, sizeof pfp);
	memset(&pfr, 0, sizeof pfr);
	snprintf(an, PF_ANCHOR_NAME_SIZE, "%s/%d.%d", FTP_PROXY_ANCHOR,
	    getpid(), id);
	strlcpy(pfp.anchor, an, PF_ANCHOR_NAME_SIZE);
	strlcpy(pfr.anchor, an, PF_ANCHOR_NAME_SIZE);

	switch (rs_num) {
	case PF_RULESET_FILTER:
		pfr.ticket = pfte[TRANS_FILTER].ticket;
		break;
	case PF_RULESET_NAT:
		pfr.ticket = pfte[TRANS_NAT].ticket;
		break;
	case PF_RULESET_RDR:
		pfr.ticket = pfte[TRANS_RDR].ticket;
		break;
	default:
		errno = EINVAL;
		return (-1);
	}
	if (ioctl(dev, DIOCBEGINADDRS, &pfp) == -1)
		return (-1);
	pfr.pool_ticket = pfp.ticket;

	/* Generic for all rule types. */
	pfr.rule.af = src->sa_family;
	pfr.rule.proto = IPPROTO_TCP;
	pfr.rule.src.addr.type = PF_ADDR_ADDRMASK;
	pfr.rule.dst.addr.type = PF_ADDR_ADDRMASK;
	if (src->sa_family == AF_INET) {
		memcpy(&pfr.rule.src.addr.v.a.addr.v4,
		    &satosin(src)->sin_addr.s_addr, 4);
		memset(&pfr.rule.src.addr.v.a.mask.addr8, 255, 4);
		memcpy(&pfr.rule.dst.addr.v.a.addr.v4,
		    &satosin(dst)->sin_addr.s_addr, 4);
		memset(&pfr.rule.dst.addr.v.a.mask.addr8, 255, 4);
	} else {
		memcpy(&pfr.rule.src.addr.v.a.addr.v6,
		    &satosin6(src)->sin6_addr.s6_addr, 16);
		memset(&pfr.rule.src.addr.v.a.mask.addr8, 255, 16);
		memcpy(&pfr.rule.dst.addr.v.a.addr.v6,
		    &satosin6(dst)->sin6_addr.s6_addr, 16);
		memset(&pfr.rule.dst.addr.v.a.mask.addr8, 255, 16);
	}
	pfr.rule.dst.port_op = PF_OP_EQ;
	pfr.rule.dst.port[0] = htons(d_port);

	switch (rs_num) {
	case PF_RULESET_FILTER:
		/*
		 * pass [quick] [log] inet[6] proto tcp \
		 *     from $src to $dst port = $d_port flags S/SA keep state
		 *     (max 1) [queue qname] [tag tagname]
		 */
		pfr.rule.action = PF_PASS;
		pfr.rule.quick = 1;
		pfr.rule.log = rule_log;
		pfr.rule.keep_state = 1;
		pfr.rule.flags = TH_SYN;
		pfr.rule.flagset = (TH_SYN|TH_ACK);
		pfr.rule.max_states = 1;
		if (qname != NULL)
			strlcpy(pfr.rule.qname, qname, sizeof pfr.rule.qname);
		if (tagname != NULL) {
			pfr.rule.quick = 0;
			strlcpy(pfr.rule.tagname, tagname,
                                sizeof pfr.rule.tagname);
		}
		break;
	case PF_RULESET_NAT:
		/*
		 * nat inet[6] proto tcp from $src to $dst port $d_port -> $nat
		 */
		pfr.rule.action = PF_NAT;
		break;
	case PF_RULESET_RDR:
		/*
		 * rdr inet[6] proto tcp from $src to $dst port $d_port -> $rdr
		 */
		pfr.rule.action = PF_RDR;
		break;
	default:
		errno = EINVAL;
		return (-1);
	}

	return (0);
}

int
server_lookup(struct sockaddr *client, struct sockaddr *proxy,
    struct sockaddr *server)
{
	if (client->sa_family == AF_INET)
		return (server_lookup4(satosin(client), satosin(proxy),
		    satosin(server)));
	
	if (client->sa_family == AF_INET6)
		return (server_lookup6(satosin6(client), satosin6(proxy),
		    satosin6(server)));

	errno = EPROTONOSUPPORT;
	return (-1);
}

int
server_lookup4(struct sockaddr_in *client, struct sockaddr_in *proxy,
    struct sockaddr_in *server)
{
	struct pfioc_natlook pnl;

	memset(&pnl, 0, sizeof pnl);
	pnl.direction = PF_OUT;
	pnl.af = AF_INET;
	pnl.proto = IPPROTO_TCP;
	memcpy(&pnl.saddr.v4, &client->sin_addr.s_addr, sizeof pnl.saddr.v4);
	memcpy(&pnl.daddr.v4, &proxy->sin_addr.s_addr, sizeof pnl.daddr.v4);
	pnl.sport = client->sin_port;
	pnl.dport = proxy->sin_port;
	
	if (ioctl(dev, DIOCNATLOOK, &pnl) == -1)
		return (-1);

	memset(server, 0, sizeof(struct sockaddr_in));
	server->sin_len = sizeof(struct sockaddr_in);
	server->sin_family = AF_INET;
	memcpy(&server->sin_addr.s_addr, &pnl.rdaddr.v4,
	    sizeof server->sin_addr.s_addr);
	server->sin_port = pnl.rdport;
		
	return (0);
}

int
server_lookup6(struct sockaddr_in6 *client, struct sockaddr_in6 *proxy,
    struct sockaddr_in6 *server)
{
	struct pfioc_natlook pnl;

	memset(&pnl, 0, sizeof pnl);
	pnl.direction = PF_OUT;
	pnl.af = AF_INET6;
	pnl.proto = IPPROTO_TCP;
	memcpy(&pnl.saddr.v6, &client->sin6_addr.s6_addr, sizeof pnl.saddr.v6);
	memcpy(&pnl.daddr.v6, &proxy->sin6_addr.s6_addr, sizeof pnl.daddr.v6);
	pnl.sport = client->sin6_port;
	pnl.dport = proxy->sin6_port;
	
	if (ioctl(dev, DIOCNATLOOK, &pnl) == -1)
		return (-1);

	memset(server, 0, sizeof(struct sockaddr_in6));
	server->sin6_len = sizeof(struct sockaddr_in6);
	server->sin6_family = AF_INET6;
	memcpy(&server->sin6_addr.s6_addr, &pnl.rdaddr.v6,
	    sizeof server->sin6_addr);
	server->sin6_port = pnl.rdport;

	return (0);
}
