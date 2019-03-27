/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Begemot: bsnmp/snmp_mibII/mibII_route.c,v 1.9 2005/10/06 07:15:00 brandt_h Exp $
 *
 * Routing table
 */

#ifdef HAVE_SYS_TREE_H
#include <sys/tree.h>
#else
#include "tree.h"
#endif

#include "mibII.h"
#include "mibII_oid.h"

struct sroute {
	RB_ENTRY(sroute) link;
	uint32_t	ifindex;
	uint8_t		index[13];
	uint8_t		type;
	uint8_t		proto;
};
static RB_HEAD(sroutes, sroute) sroutes = RB_INITIALIZER(&sroutes);

RB_PROTOTYPE(sroutes, sroute, link, sroute_compare);

#define	ROUTE_UPDATE_INTERVAL	(100 * 60 * 10)	/* 10 min */
static uint64_t route_tick;
static u_int route_total;

/*
 * Compare two routes
 */
static int
sroute_compare(struct sroute *s1, struct sroute *s2)
{

	return (memcmp(s1->index, s2->index, 13));
}

static void
sroute_index_append(struct asn_oid *oid, u_int sub, const struct sroute *s)
{
	int i;

	oid->len = sub + 13;
	for (i = 0; i < 13; i++)
		oid->subs[sub + i] = s->index[i];
}

#if 0
static void
sroute_print(const struct sroute *r)
{
	u_int i;

	for (i = 0; i < 13 - 1; i++)
		printf("%u.", r->index[i]);
	printf("%u proto=%u type=%u", r->index[i], r->proto, r->type);
}
#endif

/*
 * process routing message
 */
void
mib_sroute_process(struct rt_msghdr *rtm, struct sockaddr *gw,
    struct sockaddr *dst, struct sockaddr *mask)
{
	struct sockaddr_in *in_dst, *in_gw;
	struct in_addr in_mask;
	struct mibif *ifp;
	struct sroute key;
	struct sroute *r, *r1;
	in_addr_t ha;

	if (dst == NULL || gw == NULL || dst->sa_family != AF_INET ||
	    gw->sa_family != AF_INET)
		return;

	in_dst = (struct sockaddr_in *)(void *)dst;
	in_gw = (struct sockaddr_in *)(void *)gw;

	if (rtm->rtm_flags & RTF_HOST)
		in_mask.s_addr = 0xffffffff;
	else if (mask == NULL || mask->sa_len == 0)
		in_mask.s_addr = 0;
	else
		in_mask = ((struct sockaddr_in *)(void *)mask)->sin_addr;

	/* build the index */
	ha = ntohl(in_dst->sin_addr.s_addr);
	key.index[0] = (ha >> 24) & 0xff;
	key.index[1] = (ha >> 16) & 0xff;
	key.index[2] = (ha >>  8) & 0xff;
	key.index[3] = (ha >>  0) & 0xff;

	ha = ntohl(in_mask.s_addr);
	key.index[4] = (ha >> 24) & 0xff;
	key.index[5] = (ha >> 16) & 0xff;
	key.index[6] = (ha >>  8) & 0xff;
	key.index[7] = (ha >>  0) & 0xff;

	/* ToS */
	key.index[8] = 0;

	ha = ntohl(in_gw->sin_addr.s_addr);
	key.index[9] = (ha >> 24) & 0xff;
	key.index[10] = (ha >> 16) & 0xff;
	key.index[11] = (ha >>  8) & 0xff;
	key.index[12] = (ha >>  0) & 0xff;

	if (rtm->rtm_type == RTM_DELETE) {
		r = RB_FIND(sroutes, &sroutes, &key);
		if (r == 0) {
#ifdef DEBUG_ROUTE
			syslog(LOG_WARNING, "%s: DELETE: %u.%u.%u.%u "
			    "%u.%u.%u.%u %u %u.%u.%u.%u not found", __func__,
			    key.index[0], key.index[1], key.index[2],
			    key.index[3], key.index[4], key.index[5],
			    key.index[6], key.index[7], key.index[8],
			    key.index[9], key.index[10], key.index[11],
			    key.index[12]);
#endif
			return;
		}
		RB_REMOVE(sroutes, &sroutes, r);
		free(r);
		route_total--;
#ifdef DEBUG_ROUTE
		printf("%s: DELETE: %u.%u.%u.%u "
		    "%u.%u.%u.%u %u %u.%u.%u.%u\n", __func__,
		    key.index[0], key.index[1], key.index[2],
		    key.index[3], key.index[4], key.index[5],
		    key.index[6], key.index[7], key.index[8],
		    key.index[9], key.index[10], key.index[11],
		    key.index[12]);
#endif
		return;
	}

	/* GET or ADD */
	ifp = NULL;
	if ((ifp = mib_find_if_sys(rtm->rtm_index)) == NULL) {
		if (rtm->rtm_type == RTM_ADD) {
			/* make it a get so the kernel fills the index */
			mib_send_rtmsg(rtm, gw, dst, mask);
			return;
		}
		mib_iflist_bad = 1;
	}

	if ((r = malloc(sizeof(*r))) == NULL) {
		syslog(LOG_ERR, "%m");
		return;
	}

	memcpy(r->index, key.index, sizeof(r->index));
	r->ifindex = (ifp == NULL) ? 0 : ifp->index;

	r->type = (rtm->rtm_flags & RTF_REJECT) ? 2 : 4;

	/* cannot really know, what protocol it runs */
	r->proto = (rtm->rtm_flags & RTF_LOCAL) ? 2 :
	    (rtm->rtm_flags & RTF_STATIC) ? 3 :
	    (rtm->rtm_flags & RTF_DYNAMIC) ? 4 : 10;

	r1 = RB_INSERT(sroutes, &sroutes, r);
	if (r1 != NULL) {
#ifdef DEBUG_ROUTE
		syslog(LOG_WARNING, "%s: %u.%u.%u.%u "
		    "%u.%u.%u.%u %u %u.%u.%u.%u duplicate route", __func__,
		    key.index[0], key.index[1], key.index[2],
		    key.index[3], key.index[4], key.index[5],
		    key.index[6], key.index[7], key.index[8],
		    key.index[9], key.index[10], key.index[11],
		    key.index[12]);
#endif
		r1->ifindex = r->ifindex;
		r1->type = r->type;
		r1->proto = r->proto;
		free(r);
		return;
	}

	route_total++;
#ifdef DEBUG_ROUTE
	printf("%s: ADD/GET: %u.%u.%u.%u "
	    "%u.%u.%u.%u %u %u.%u.%u.%u\n", __func__,
	    key.index[0], key.index[1], key.index[2],
	    key.index[3], key.index[4], key.index[5],
	    key.index[6], key.index[7], key.index[8],
	    key.index[9], key.index[10], key.index[11],
	    key.index[12]);
#endif
}

int
mib_fetch_route(void)
{
	u_char *rtab, *next;
	size_t len;
	struct sroute *r, *r1;
	struct rt_msghdr *rtm;
	struct sockaddr *addrs[RTAX_MAX];

	if (route_tick != 0 && route_tick + ROUTE_UPDATE_INTERVAL > this_tick)
		return (0);

	/*
	 * Remove all routes
	 */
	r = RB_MIN(sroutes, &sroutes);
	while (r != NULL) {
		r1 = RB_NEXT(sroutes, &sroutes, r);
		RB_REMOVE(sroutes, &sroutes, r);
		free(r);
		r = r1;
	}
	route_total = 0;

	if ((rtab = mib_fetch_rtab(AF_INET, NET_RT_DUMP, 0, &len)) == NULL)
		return (-1);

	next = rtab;
	for (next = rtab; next < rtab + len; next += rtm->rtm_msglen) {
		rtm = (struct rt_msghdr *)(void *)next;
		if (rtm->rtm_type != RTM_GET ||
		    !(rtm->rtm_flags & RTF_UP))
			continue;
		mib_extract_addrs(rtm->rtm_addrs, (u_char *)(rtm + 1), addrs);


		mib_sroute_process(rtm, addrs[RTAX_GATEWAY], addrs[RTAX_DST],
		    addrs[RTAX_NETMASK]);
	}

#if 0
	u_int n = 0;
	r = RB_MIN(sroutes, &sroutes);
	while (r != NULL) {
		printf("%u: ", n++);
		sroute_print(r);
		printf("\n");
		r = RB_NEXT(sroutes, &sroutes, r);
	}
#endif
	free(rtab);
	route_tick = get_ticks();

	return (0);
}

/**
 * Find a route in the table.
 */
static struct sroute *
sroute_get(const struct asn_oid *oid, u_int sub)
{
	struct sroute key;
	int i;

	if (oid->len - sub != 13)
		return (NULL);
	for (i = 0; i < 13; i++)
		key.index[i] = oid->subs[sub + i];
	return (RB_FIND(sroutes, &sroutes, &key));
}

/**
 * Find next route in the table. There is no such RB_ macro, so must
 * dig into the innards of the RB stuff.
 */
static struct sroute *
sroute_getnext(struct asn_oid *oid, u_int sub)
{
	u_int i;
	int comp;
	struct sroute key;
	struct sroute *best;
	struct sroute *s;

	/*
	 * We now, that the OID is at least the tableEntry OID. If it is,
	 * the user wants the first route.
	 */
	if (oid->len == sub)
		return (RB_MIN(sroutes, &sroutes));

	/*
	 * This is also true for any index that consists of zeros and is
	 * shorter than the full index.
	 */
	if (oid->len < sub + 13) {
		for (i = sub; i < oid->len; i++)
			if (oid->subs[i] != 0)
				break;
		if (i == oid->len)
			return (RB_MIN(sroutes, &sroutes));

		/*
		 * Now if the index is too short, we fill it with zeros and then
		 * subtract one from the index. We can do this, because we now,
		 * that there is at least one index element that is not zero.
		 */
		for (i = oid->len; i < sub + 13; i++)
			oid->subs[i] = 0;

		for (i = sub + 13 - 1; i >= sub; i--) {
			if (oid->subs[i] != 0) {
				oid->subs[i]--;
				break;
			}
			oid->subs[i] = ASN_MAXID;
		}
		oid->len = sub + 13;
	}

	/* build the index */
	for (i = sub; i < sub + 13; i++)
		key.index[i - sub] = oid->subs[i];

	/* now find the element */
	best = NULL;
	s = RB_ROOT(&sroutes);

	while (s != NULL) {
		comp = sroute_compare(&key, s);
		if (comp >= 0) {
			/* The current element is smaller than what we search.
			 * Forget about it and move to the right subtree. */
			s = RB_RIGHT(s, link);
			continue;
		}
		/* the current element is larger than what we search.
		 * forget about the right subtree (its even larger), but
		 * the current element may be what we need. */
		if (best == NULL || sroute_compare(s, best) < 0)
			/* this one's better */
			best = s;

		s = RB_LEFT(s, link);
	}
	return (best);
}

/*
 * Table
 */
int
op_route_table(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	struct sroute *r;

	if (mib_fetch_route() == -1)
		return (SNMP_ERR_GENERR);

	switch (op) {

	  case SNMP_OP_GETNEXT:
		if ((r = sroute_getnext(&value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		sroute_index_append(&value->var, sub, r);
		break;

	  case SNMP_OP_GET:
		if ((r = sroute_get(&value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	  case SNMP_OP_SET:
		if ((r = sroute_get(&value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		return (SNMP_ERR_NOT_WRITEABLE);

	  case SNMP_OP_ROLLBACK:
	  case SNMP_OP_COMMIT:
		abort();

	  default:
		abort();
	}

	switch (value->var.subs[sub - 1]) {

	  case LEAF_ipCidrRouteDest:
		value->v.ipaddress[0] = r->index[0];
		value->v.ipaddress[1] = r->index[1];
		value->v.ipaddress[2] = r->index[2];
		value->v.ipaddress[3] = r->index[3];
		break;

	  case LEAF_ipCidrRouteMask:
		value->v.ipaddress[0] = r->index[4];
		value->v.ipaddress[1] = r->index[5];
		value->v.ipaddress[2] = r->index[6];
		value->v.ipaddress[3] = r->index[7];
		break;

	  case LEAF_ipCidrRouteTos:
		value->v.integer = r->index[8];
		break;

	  case LEAF_ipCidrRouteNextHop:
		value->v.ipaddress[0] = r->index[9];
		value->v.ipaddress[1] = r->index[10];
		value->v.ipaddress[2] = r->index[11];
		value->v.ipaddress[3] = r->index[12];
		break;

	  case LEAF_ipCidrRouteIfIndex:
		value->v.integer = r->ifindex;
		break;

	  case LEAF_ipCidrRouteType:
		value->v.integer = r->type;
		break;

	  case LEAF_ipCidrRouteProto:
		value->v.integer = r->proto;
		break;

	  case LEAF_ipCidrRouteAge:
		value->v.integer = 0;
		break;

	  case LEAF_ipCidrRouteInfo:
		value->v.oid = oid_zeroDotZero;
		break;

	  case LEAF_ipCidrRouteNextHopAS:
		value->v.integer = 0;
		break;

	  case LEAF_ipCidrRouteMetric1:
	  case LEAF_ipCidrRouteMetric2:
	  case LEAF_ipCidrRouteMetric3:
	  case LEAF_ipCidrRouteMetric4:
	  case LEAF_ipCidrRouteMetric5:
		value->v.integer = -1;
		break;

	  case LEAF_ipCidrRouteStatus:
		value->v.integer = 1;
		break;
	}
	return (SNMP_ERR_NOERROR);
}

/*
 * scalars
 */
int
op_route(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	switch (op) {

	  case SNMP_OP_GETNEXT:
		abort();

	  case SNMP_OP_GET:
		break;

	  case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);

	  case SNMP_OP_ROLLBACK:
	  case SNMP_OP_COMMIT:
		abort();
	}

	if (mib_fetch_route() == -1)
		return (SNMP_ERR_GENERR);

	switch (value->var.subs[sub - 1]) {

	  case LEAF_ipCidrRouteNumber:
		value->v.uint32 = route_total;
		break;

	}
	return (SNMP_ERR_NOERROR);
}

RB_GENERATE(sroutes, sroute, link, sroute_compare);
