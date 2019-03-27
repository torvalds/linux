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
 * $Begemot: bsnmp/snmp_mibII/mibII_ip.c,v 1.11 2005/05/23 09:03:40 brandt_h Exp $
 *
 * ip group scalars.
 */
#include "mibII.h"
#include "mibII_oid.h"
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp_var.h>

static struct ipstat ipstat;
static u_int	ip_idrop;
static struct icmpstat icmpstat;

static int	ip_forwarding;
static int	ip_defttl;
static uint64_t ip_tick;

static uint64_t ipstat_tick;

static int
fetch_ipstat(void)
{
	size_t len;

	len = sizeof(ipstat);
	if (sysctlbyname("net.inet.ip.stats", &ipstat, &len, NULL, 0) == -1) {
		syslog(LOG_ERR, "net.inet.ip.stats: %m");
		return (-1);
	}
	if (len != sizeof(ipstat)) {
		syslog(LOG_ERR, "net.inet.ip.stats: wrong size");
		return (-1);
	}
	len = sizeof(ip_idrop);
	if (sysctlbyname("net.inet.ip.intr_queue_drops", &ip_idrop, &len, NULL, 0) == -1)
		syslog(LOG_WARNING, "net.inet.ip.intr_queue_drops: %m");
	if (len != sizeof(ip_idrop)) {
		syslog(LOG_WARNING, "net.inet.ip.intr_queue_drops: wrong size");
		ip_idrop = 0;
	}
	len = sizeof(icmpstat);
	if (sysctlbyname("net.inet.icmp.stats", &icmpstat, &len, NULL, 0) == -1) {
		syslog(LOG_ERR, "net.inet.icmp.stats: %m");
		return (-1);
	}
	if (len != sizeof(icmpstat)) {
		syslog(LOG_ERR, "net.inet.icmp.stats: wrong size");
		return (-1);
	}

	ipstat_tick = get_ticks();
	return (0);
}

static int
fetch_ip(void)
{
	size_t len;

	len = sizeof(ip_forwarding);
	if (sysctlbyname("net.inet.ip.forwarding", &ip_forwarding, &len,
	    NULL, 0) == -1) {
		syslog(LOG_ERR, "net.inet.ip.forwarding: %m");
		return (-1);
	}
	if (len != sizeof(ip_forwarding)) {
		syslog(LOG_ERR, "net.inet.ip.forwarding: wrong size");
		return (-1);
	}

	len = sizeof(ip_defttl);
	if (sysctlbyname("net.inet.ip.ttl", &ip_defttl, &len,
	    NULL, 0) == -1) {
		syslog(LOG_ERR, "net.inet.ip.ttl: %m");
		return (-1);
	}
	if (len != sizeof(ip_defttl)) {
		syslog(LOG_ERR, "net.inet.ip.ttl: wrong size");
		return (-1);
	}

	ip_tick = get_ticks();
	return (0);
}

static int
ip_forward(int forw, int *old)
{
	size_t olen;

	olen = sizeof(*old);
	if (sysctlbyname("net.inet.ip.forwarding", old, old ? &olen : NULL,
	    &forw, sizeof(forw)) == -1) {
		syslog(LOG_ERR, "set net.inet.ip.forwarding: %m");
		return (-1);
	}
	ip_forwarding = forw;
	return (0);
}

static int
ip_setttl(int ttl, int *old)
{
	size_t olen;

	olen = sizeof(*old);
	if (sysctlbyname("net.inet.ip.ttl", old, old ? &olen : NULL,
	    &ttl, sizeof(ttl)) == -1) {
		syslog(LOG_ERR, "set net.inet.ip.ttl: %m");
		return (-1);
	}
	ip_defttl = ttl;
	return (0);
}

/*
 * READ/WRITE ip group.
 */
int
op_ip(struct snmp_context *ctx, struct snmp_value *value,
    u_int sub, u_int idx __unused, enum snmp_op op)
{
	int old = 0;

	switch (op) {

	  case SNMP_OP_GETNEXT:
		abort();

	  case SNMP_OP_GET:
		break;

	  case SNMP_OP_SET:
		if (ip_tick < this_tick)
			if (fetch_ip() == -1)
				return (SNMP_ERR_GENERR);

		switch (value->var.subs[sub - 1]) {

		  case LEAF_ipForwarding:
			ctx->scratch->int1 = ip_forwarding ? 1 : 2;
			ctx->scratch->int2 = value->v.integer;
			if (value->v.integer == 1) {
				if (!ip_forwarding && ip_forward(1, &old))
					return (SNMP_ERR_GENERR);
				ctx->scratch->int1 = old ? 1 : 2;
			} else if (value->v.integer == 2) {
				if (ip_forwarding && ip_forward(0, &old))
					return (SNMP_ERR_GENERR);
				ctx->scratch->int1 = old;
			} else
				return (SNMP_ERR_WRONG_VALUE);
			break;

		  case LEAF_ipDefaultTTL:
			ctx->scratch->int1 = ip_defttl;
			ctx->scratch->int2 = value->v.integer;
			if (value->v.integer < 1 || value->v.integer > 255)
				return (SNMP_ERR_WRONG_VALUE);
			if (ip_defttl != value->v.integer &&
			    ip_setttl(value->v.integer, &old))
				return (SNMP_ERR_GENERR);
			ctx->scratch->int1 = old;
			break;
		}
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_ROLLBACK:
		switch (value->var.subs[sub - 1]) {

		  case LEAF_ipForwarding:
			if (ctx->scratch->int1 == 1) {
				if (ctx->scratch->int2 == 2)
					(void)ip_forward(1, NULL);
			} else {
				if (ctx->scratch->int2 == 1)
					(void)ip_forward(0, NULL);
			}
			break;

		  case LEAF_ipDefaultTTL:
			if (ctx->scratch->int1 != ctx->scratch->int2)
				(void)ip_setttl(ctx->scratch->int1, NULL);
			break;
		}
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);
	}

	if (ip_tick < this_tick)
		if (fetch_ip() == -1)
			return (SNMP_ERR_GENERR);

	switch (value->var.subs[sub - 1]) {

	  case LEAF_ipForwarding:
		value->v.integer = ip_forwarding ? 1 : 2;
		break;

	  case LEAF_ipDefaultTTL:
		value->v.integer = ip_defttl;
		break;
	}
	return (SNMP_ERR_NOERROR);
}

/*
 * READ-ONLY statistics ip group.
 */
int
op_ipstat(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int idx __unused, enum snmp_op op)
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

	if (ipstat_tick < this_tick)
		fetch_ipstat();

	switch (value->var.subs[sub - 1]) {

	  case LEAF_ipInReceives:
		value->v.uint32 = ipstat.ips_total;
		break;

	  case LEAF_ipInHdrErrors:
		value->v.uint32 = ipstat.ips_badsum + ipstat.ips_tooshort
		    + ipstat.ips_toosmall + ipstat.ips_badhlen
		    + ipstat.ips_badlen + ipstat.ips_badvers +
		    + ipstat.ips_toolong;
		break;

	  case LEAF_ipInAddrErrors:
		value->v.uint32 = ipstat.ips_cantforward;
		break;

	  case LEAF_ipForwDatagrams:
		value->v.uint32 = ipstat.ips_forward;
		break;

	  case LEAF_ipInUnknownProtos:
		value->v.uint32 = ipstat.ips_noproto;
		break;

	  case LEAF_ipInDiscards:
		value->v.uint32 = ip_idrop;
		break;

	  case LEAF_ipInDelivers:
		value->v.uint32 = ipstat.ips_delivered;
		break;

	  case LEAF_ipOutRequests:
		value->v.uint32 = ipstat.ips_localout;
		break;

	  case LEAF_ipOutDiscards:
		value->v.uint32 = ipstat.ips_odropped;
		break;

	  case LEAF_ipOutNoRoutes:
		value->v.uint32 = ipstat.ips_noroute;
		break;

	  case LEAF_ipReasmTimeout:
		value->v.integer = IPFRAGTTL;
		break;

	  case LEAF_ipReasmReqds:
		value->v.uint32 = ipstat.ips_fragments;
		break;

	  case LEAF_ipReasmOKs:
		value->v.uint32 = ipstat.ips_reassembled;
		break;

	  case LEAF_ipReasmFails:
		value->v.uint32 = ipstat.ips_fragdropped
		    + ipstat.ips_fragtimeout;
		break;

	  case LEAF_ipFragOKs:
		value->v.uint32 = ipstat.ips_fragmented;
		break;

	  case LEAF_ipFragFails:
		value->v.uint32 = ipstat.ips_cantfrag;
		break;

	  case LEAF_ipFragCreates:
		value->v.uint32 = ipstat.ips_ofragments;
		break;
	}
	return (SNMP_ERR_NOERROR);
}

/*
 * READ-ONLY statistics icmp group.
 */
int
op_icmpstat(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int idx __unused, enum snmp_op op)
{
	u_int i;

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

	if (ipstat_tick < this_tick)
		fetch_ipstat();

	switch (value->var.subs[sub - 1]) {

	  case LEAF_icmpInMsgs:
		value->v.integer = 0;
		for (i = 0; i <= ICMP_MAXTYPE; i++)
			value->v.integer += icmpstat.icps_inhist[i];
		value->v.integer += icmpstat.icps_tooshort +
		    icmpstat.icps_checksum;
		/* missing: bad type and packets on faith */
		break;

	  case LEAF_icmpInErrors:
		value->v.integer = icmpstat.icps_tooshort +
		    icmpstat.icps_checksum +
		    icmpstat.icps_badlen +
		    icmpstat.icps_badcode +
		    icmpstat.icps_bmcastecho +
		    icmpstat.icps_bmcasttstamp;
		break;

	  case LEAF_icmpInDestUnreachs:
		value->v.integer = icmpstat.icps_inhist[ICMP_UNREACH];
		break;

	  case LEAF_icmpInTimeExcds:
		value->v.integer = icmpstat.icps_inhist[ICMP_TIMXCEED];
		break;

	  case LEAF_icmpInParmProbs:
		value->v.integer = icmpstat.icps_inhist[ICMP_PARAMPROB];
		break;

	  case LEAF_icmpInSrcQuenchs:
		value->v.integer = icmpstat.icps_inhist[ICMP_SOURCEQUENCH];
		break;

	  case LEAF_icmpInRedirects:
		value->v.integer = icmpstat.icps_inhist[ICMP_REDIRECT];
		break;

	  case LEAF_icmpInEchos:
		value->v.integer = icmpstat.icps_inhist[ICMP_ECHO];
		break;

	  case LEAF_icmpInEchoReps:
		value->v.integer = icmpstat.icps_inhist[ICMP_ECHOREPLY];
		break;

	  case LEAF_icmpInTimestamps:
		value->v.integer = icmpstat.icps_inhist[ICMP_TSTAMP];
		break;

	  case LEAF_icmpInTimestampReps:
		value->v.integer = icmpstat.icps_inhist[ICMP_TSTAMPREPLY];
		break;

	  case LEAF_icmpInAddrMasks:
		value->v.integer = icmpstat.icps_inhist[ICMP_MASKREQ];
		break;

	  case LEAF_icmpInAddrMaskReps:
		value->v.integer = icmpstat.icps_inhist[ICMP_MASKREPLY];
		break;

	  case LEAF_icmpOutMsgs:
		value->v.integer = 0;
		for (i = 0; i <= ICMP_MAXTYPE; i++)
			value->v.integer += icmpstat.icps_outhist[i];
		value->v.integer += icmpstat.icps_badaddr +
		    icmpstat.icps_noroute;
		break;

	  case LEAF_icmpOutErrors:
		value->v.integer = icmpstat.icps_badaddr +
		    icmpstat.icps_noroute;
		break;

	  case LEAF_icmpOutDestUnreachs:
		value->v.integer = icmpstat.icps_outhist[ICMP_UNREACH];
		break;

	  case LEAF_icmpOutTimeExcds:
		value->v.integer = icmpstat.icps_outhist[ICMP_TIMXCEED];
		break;

	  case LEAF_icmpOutParmProbs:
		value->v.integer = icmpstat.icps_outhist[ICMP_PARAMPROB];
		break;

	  case LEAF_icmpOutSrcQuenchs:
		value->v.integer = icmpstat.icps_outhist[ICMP_SOURCEQUENCH];
		break;

	  case LEAF_icmpOutRedirects:
		value->v.integer = icmpstat.icps_outhist[ICMP_REDIRECT];
		break;

	  case LEAF_icmpOutEchos:
		value->v.integer = icmpstat.icps_outhist[ICMP_ECHO];
		break;

	  case LEAF_icmpOutEchoReps:
		value->v.integer = icmpstat.icps_outhist[ICMP_ECHOREPLY];
		break;

	  case LEAF_icmpOutTimestamps:
		value->v.integer = icmpstat.icps_outhist[ICMP_TSTAMP];
		break;

	  case LEAF_icmpOutTimestampReps:
		value->v.integer = icmpstat.icps_outhist[ICMP_TSTAMPREPLY];
		break;

	  case LEAF_icmpOutAddrMasks:
		value->v.integer = icmpstat.icps_outhist[ICMP_MASKREQ];
		break;

	  case LEAF_icmpOutAddrMaskReps:
		value->v.integer = icmpstat.icps_outhist[ICMP_MASKREPLY];
		break;
	}
	return (SNMP_ERR_NOERROR);
}
