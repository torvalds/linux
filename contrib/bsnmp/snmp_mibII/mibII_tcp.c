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
 * $Begemot: bsnmp/snmp_mibII/mibII_tcp.c,v 1.7 2005/05/23 09:03:42 brandt_h Exp $
 *
 * tcp
 */
#include "mibII.h"
#include "mibII_oid.h"
#include <sys/socketvar.h>
#include <netinet/in_pcb.h>
#include <netinet/tcp.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_fsm.h>

struct tcp_index {
	struct asn_oid	index;
	struct xtcpcb	*tp;
};

static uint64_t tcp_tick;
static uint64_t tcp_stats_tick;
static struct tcpstat tcpstat;
static uint64_t tcps_states[TCP_NSTATES];
static struct xinpgen *xinpgen;
static size_t xinpgen_len;
static u_int tcp_total;

static u_int oidnum;
static struct tcp_index *tcpoids;

static int
tcp_compare(const void *p1, const void *p2)
{
	const struct tcp_index *t1 = p1;
	const struct tcp_index *t2 = p2;

	return (asn_compare_oid(&t1->index, &t2->index));
}

static int
fetch_tcp_stats(void)
{
	size_t len;

	len = sizeof(tcpstat);
	if (sysctlbyname("net.inet.tcp.stats", &tcpstat, &len, NULL, 0) == -1) {
		syslog(LOG_ERR, "net.inet.tcp.stats: %m");
		return (-1);
	}
	if (len != sizeof(tcpstat)) {
		syslog(LOG_ERR, "net.inet.tcp.stats: wrong size");
		return (-1);
	}

	len = sizeof(tcps_states);
	if (sysctlbyname("net.inet.tcp.states", &tcps_states, &len, NULL,
	    0) == -1) {
		syslog(LOG_ERR, "net.inet.tcp.states: %m");
		return (-1);
	}
	if (len != sizeof(tcps_states)) {
		syslog(LOG_ERR, "net.inet.tcp.states: wrong size");
		return (-1);
	}

	tcp_stats_tick = get_ticks();

	return (0);
}

static int
fetch_tcp(void)
{
	size_t len;
	struct xinpgen *ptr;
	struct xtcpcb *tp;
	struct tcp_index *oid;
	in_addr_t inaddr;

	len = 0;
	if (sysctlbyname("net.inet.tcp.pcblist", NULL, &len, NULL, 0) == -1) {
		syslog(LOG_ERR, "net.inet.tcp.pcblist: %m");
		return (-1);
	}
	if (len > xinpgen_len) {
		if ((ptr = realloc(xinpgen, len)) == NULL) {
			syslog(LOG_ERR, "%zu: %m", len);
			return (-1);
		}
		xinpgen = ptr;
		xinpgen_len = len;
	}
	if (sysctlbyname("net.inet.tcp.pcblist", xinpgen, &len, NULL, 0) == -1) {
		syslog(LOG_ERR, "net.inet.tcp.pcblist: %m");
		return (-1);
	}

	tcp_tick = get_ticks();

	tcp_total = 0;
	for (ptr = (struct xinpgen *)(void *)((char *)xinpgen + xinpgen->xig_len);
	     ptr->xig_len > sizeof(struct xinpgen);
             ptr = (struct xinpgen *)(void *)((char *)ptr + ptr->xig_len)) {
		tp = (struct xtcpcb *)ptr;
		if (tp->xt_inp.inp_gencnt > xinpgen->xig_gen ||
		    (tp->xt_inp.inp_vflag & (INP_IPV4|INP_IPV6)) == 0)
			continue;

		if (tp->xt_inp.inp_vflag & INP_IPV4)
			tcp_total++;
	}

	if (oidnum < tcp_total) {
		oid = realloc(tcpoids, tcp_total * sizeof(tcpoids[0]));
		if (oid == NULL) {
			free(tcpoids);
			oidnum = 0;
			return (0);
		}
		tcpoids = oid;
		oidnum = tcp_total;
	}

	oid = tcpoids;
	for (ptr = (struct xinpgen *)(void *)((char *)xinpgen + xinpgen->xig_len);
	     ptr->xig_len > sizeof(struct xinpgen);
             ptr = (struct xinpgen *)(void *)((char *)ptr + ptr->xig_len)) {
		tp = (struct xtcpcb *)ptr;
		if (tp->xt_inp.inp_gencnt > xinpgen->xig_gen ||
		    (tp->xt_inp.inp_vflag & INP_IPV4) == 0)
			continue;
		oid->tp = tp;
		oid->index.len = 10;
		inaddr = ntohl(tp->xt_inp.inp_laddr.s_addr);
		oid->index.subs[0] = (inaddr >> 24) & 0xff;
		oid->index.subs[1] = (inaddr >> 16) & 0xff;
		oid->index.subs[2] = (inaddr >>  8) & 0xff;
		oid->index.subs[3] = (inaddr >>  0) & 0xff;
		oid->index.subs[4] = ntohs(tp->xt_inp.inp_lport);
		inaddr = ntohl(tp->xt_inp.inp_faddr.s_addr);
		oid->index.subs[5] = (inaddr >> 24) & 0xff;
		oid->index.subs[6] = (inaddr >> 16) & 0xff;
		oid->index.subs[7] = (inaddr >>  8) & 0xff;
		oid->index.subs[8] = (inaddr >>  0) & 0xff;
		oid->index.subs[9] = ntohs(tp->xt_inp.inp_fport);
		oid++;
	}

	qsort(tcpoids, tcp_total, sizeof(tcpoids[0]), tcp_compare);

	return (0);
}

/*
 * Scalars
 */
int
op_tcp(struct snmp_context *ctx __unused, struct snmp_value *value,
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

	if (tcp_stats_tick < this_tick)
		if (fetch_tcp_stats() == -1)
			return (SNMP_ERR_GENERR);

	switch (value->var.subs[sub - 1]) {

	  case LEAF_tcpRtoAlgorithm:
		value->v.integer = 4;	/* Van Jacobson */
		break;

#define hz clockinfo.hz

	  case LEAF_tcpRtoMin:
		value->v.integer = 1000 * TCPTV_MIN / hz;
		break;

	  case LEAF_tcpRtoMax:
		value->v.integer = 1000 * TCPTV_REXMTMAX / hz;
		break;
#undef hz

	  case LEAF_tcpMaxConn:
		value->v.integer = -1;
		break;

	  case LEAF_tcpActiveOpens:
		value->v.uint32 = tcpstat.tcps_connattempt;
		break;

	  case LEAF_tcpPassiveOpens:
		value->v.uint32 = tcpstat.tcps_accepts;
		break;

	  case LEAF_tcpAttemptFails:
		value->v.uint32 = tcpstat.tcps_conndrops;
		break;

	  case LEAF_tcpEstabResets:
		value->v.uint32 = tcpstat.tcps_drops;
		break;

	  case LEAF_tcpCurrEstab:
		value->v.uint32 = tcps_states[TCPS_ESTABLISHED] +
		    tcps_states[TCPS_CLOSE_WAIT];
		break;

	  case LEAF_tcpInSegs:
		value->v.uint32 = tcpstat.tcps_rcvtotal;
		break;

	  case LEAF_tcpOutSegs:
		value->v.uint32 = tcpstat.tcps_sndtotal -
		    tcpstat.tcps_sndrexmitpack;
		break;

	  case LEAF_tcpRetransSegs:
		value->v.uint32 = tcpstat.tcps_sndrexmitpack;
		break;

	  case LEAF_tcpInErrs:
		value->v.uint32 = tcpstat.tcps_rcvbadsum +
		    tcpstat.tcps_rcvbadoff +
		    tcpstat.tcps_rcvshort;
		break;
	}
	return (SNMP_ERR_NOERROR);
}

int
op_tcpconn(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	u_int i;

	if (tcp_tick < this_tick)
		if (fetch_tcp() == -1)
			return (SNMP_ERR_GENERR);

	switch (op) {

	  case SNMP_OP_GETNEXT:
		for (i = 0; i < tcp_total; i++)
			if (index_compare(&value->var, sub, &tcpoids[i].index) < 0)
				break;
		if (i == tcp_total)
			return (SNMP_ERR_NOSUCHNAME);
		index_append(&value->var, sub, &tcpoids[i].index);
		break;

	  case SNMP_OP_GET:
		for (i = 0; i < tcp_total; i++)
			if (index_compare(&value->var, sub, &tcpoids[i].index) == 0)
				break;
		if (i == tcp_total)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	  case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);

	  case SNMP_OP_ROLLBACK:
	  case SNMP_OP_COMMIT:
	  default:
		abort();
	}

	switch (value->var.subs[sub - 1]) {

	  case LEAF_tcpConnState:
		switch (tcpoids[i].tp->t_state) {

		  case TCPS_CLOSED:
			value->v.integer = 1;
			break;
		  case TCPS_LISTEN:
			value->v.integer = 2;
			break;
		  case TCPS_SYN_SENT:
			value->v.integer = 3;
			break;
		  case TCPS_SYN_RECEIVED:
			value->v.integer = 4;
			break;
		  case TCPS_ESTABLISHED:
			value->v.integer = 5;
			break;
		  case TCPS_CLOSE_WAIT:
			value->v.integer = 8;
			break;
		  case TCPS_FIN_WAIT_1:
			value->v.integer = 6;
			break;
		  case TCPS_CLOSING:
			value->v.integer = 10;
			break;
		  case TCPS_LAST_ACK:
			value->v.integer = 9;
			break;
		  case TCPS_FIN_WAIT_2:
			value->v.integer = 7;
			break;
		  case TCPS_TIME_WAIT:
			value->v.integer = 11;
			break;
		  default:
			value->v.integer = 0;
			break;
		}
		break;

	  case LEAF_tcpConnLocalAddress:
		value->v.ipaddress[0] = tcpoids[i].index.subs[0];
		value->v.ipaddress[1] = tcpoids[i].index.subs[1];
		value->v.ipaddress[2] = tcpoids[i].index.subs[2];
		value->v.ipaddress[3] = tcpoids[i].index.subs[3];
		break;

	  case LEAF_tcpConnLocalPort:
		value->v.integer = tcpoids[i].index.subs[4];
		break;

	  case LEAF_tcpConnRemAddress:
		value->v.ipaddress[0] = tcpoids[i].index.subs[5];
		value->v.ipaddress[1] = tcpoids[i].index.subs[6];
		value->v.ipaddress[2] = tcpoids[i].index.subs[7];
		value->v.ipaddress[3] = tcpoids[i].index.subs[8];
		break;

	  case LEAF_tcpConnRemPort:
		value->v.integer = tcpoids[i].index.subs[9];
		break;
	}
	return (SNMP_ERR_NOERROR);
}
