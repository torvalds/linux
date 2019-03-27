/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * $FreeBSD$
 */
/*
 * Copyright (c) 2018 Michael Tuexen <tuexen@FreeBSD.org>
 */

#pragma D depends_on library ip.d
#pragma D depends_on library socket.d
#pragma D depends_on module kernel
#pragma D depends_on provider sctp

#pragma D binding "1.13" SCTP_STATE_MASK
inline int32_t SCTP_STATE_MASK =		0x0000007f;
#pragma D binding "1.13" SCTP_STATE_SHUTDOWN_PENDING
inline int32_t SCTP_STATE_SHUTDOWN_PENDING =	0x00000080;
#pragma D binding "1.13" SCTP_STATE_CLOSED_SOCKET
inline int32_t SCTP_STATE_CLOSED_SOCKET =	0x00000100;
#pragma D binding "1.13" SCTP_STATE_ABOUT_TO_BE_FREED
inline int32_t SCTP_STATE_ABOUT_TO_BE_FREED =	0x00000200;
#pragma D binding "1.13" SCTP_STATE_ABOUT_TO_BE_FREED
inline int32_t SCTP_STATE_PARTIAL_MSG_LEFT =	0x00000400;
#pragma D binding "1.13" SCTP_STATE_PARTIAL_MSG_LEFT
inline int32_t SCTP_STATE_WAS_ABORTED =		0x00000800;
#pragma D binding "1.13" SCTP_STATE_IN_ACCEPT_QUEUE
inline int32_t SCTP_STATE_IN_ACCEPT_QUEUE =	0x00001000;
#pragma D binding "1.13" SCTP_STATE_BOUND
inline int32_t SCTP_STATE_BOUND =		0x00001000;
#pragma D binding "1.13" SCTP_STATE_EMPTY
inline int32_t SCTP_STATE_EMPTY =		0x00000000;
#pragma D binding "1.13" SCTP_STATE_CLOSED
inline int32_t SCTP_STATE_CLOSED =		0x00000000;
#pragma D binding "1.13" SCTP_STATE_INUSE
inline int32_t SCTP_STATE_INUSE =		0x00000001;
#pragma D binding "1.13" SCTP_STATE_COOKIE_WAIT
inline int32_t SCTP_STATE_COOKIE_WAIT =		0x00000002;
#pragma D binding "1.13" SCTP_STATE_COOKIE_ECHOED
inline int32_t SCTP_STATE_COOKIE_ECHOED =	0x00000004;
#pragma D binding "1.13" SCTP_STATE_ESTABLISHED
inline int32_t SCTP_STATE_ESTABLISHED =		0x00000008;
#pragma D binding "1.13" SCTP_STATE_OPEN
inline int32_t SCTP_STATE_OPEN =		0x00000008;
#pragma D binding "1.13" SCTP_STATE_SHUTDOWN_SENT
inline int32_t SCTP_STATE_SHUTDOWN_SENT =	0x00000010;
#pragma D binding "1.13" SCTP_STATE_SHUTDOWN_RECEIVED
inline int32_t SCTP_STATE_SHUTDOWN_RECEIVED =	0x00000020;
#pragma D binding "1.13" SCTP_STATE_SHUTDOWN_ACK_SENT
inline int32_t SCTP_STATE_SHUTDOWN_ACK_SENT =	0x00000040;

/* SCTP association state strings. */
#pragma D binding "1.13" sctp_state_string
inline string sctp_state_string[int32_t state] =
	state & SCTP_STATE_ABOUT_TO_BE_FREED ?				"state-closed" :
	state & SCTP_STATE_SHUTDOWN_PENDING ?				"state-shutdown-pending" :
	(state & SCTP_STATE_MASK) == SCTP_STATE_EMPTY ?			"state-closed" :
	(state & SCTP_STATE_MASK) == SCTP_STATE_INUSE ?			"state-closed" :
	(state & SCTP_STATE_MASK) == SCTP_STATE_COOKIE_WAIT ?		"state-cookie-wait" :
	(state & SCTP_STATE_MASK) == SCTP_STATE_COOKIE_ECHOED ?		"state-cookie-echoed" :
	(state & SCTP_STATE_MASK) == SCTP_STATE_OPEN ?			"state-established" :
	(state & SCTP_STATE_MASK) == SCTP_STATE_SHUTDOWN_SENT ?		"state-shutdown-sent" :
	(state & SCTP_STATE_MASK) == SCTP_STATE_SHUTDOWN_RECEIVED ?	"state-shutdown-received" :
	(state & SCTP_STATE_MASK) == SCTP_STATE_SHUTDOWN_ACK_SENT ?	"state-shutdown-ack-sent" :
	"<unknown>";

/*
 * sctpsinfo contains stable SCTP details.
 */
typedef struct sctpsinfo {
	uintptr_t sctps_addr;			/* pointer to struct sctp_tcb */
	int sctps_num_raddrs;			/* number of remote addresses */
	uintptr_t sctps_raddrs;			/* pointer to struct sctp_nets */
	int sctps_num_laddrs;			/* number of local addresses */
	uintptr_t sctps_laddrs;			/* pointer to struct sctp_laddr */
	uint16_t sctps_lport;			/* local port */
	uint16_t sctps_rport;			/* remote port */
	string sctps_laddr;			/* local address, as a string */
	string sctps_raddr;			/* remote address, as a string */
	int32_t sctps_state;
} sctpsinfo_t;

/*
 * sctplsinfo provides the old SCTP state for state changes.
 */
typedef struct sctplsinfo {
	int32_t sctps_state;			/* previous SCTP state */
} sctplsinfo_t;

/*
 * sctpinfo is the SCTP header fields.
 */
typedef struct sctpinfo {
	uint16_t sctp_sport;			/* source port */
	uint16_t sctp_dport;			/* destination port */
	uint32_t sctp_verify;			/* verification tag */
	uint32_t sctp_checksum;			/* CRC32C of the SCTP packet */
	struct sctphdr *sctp_hdr;		/* raw SCTP header */
} sctpinfo_t;

#pragma D binding "1.13" translator
translator csinfo_t < struct sctp_tcb *p > {
	cs_addr =	NULL;
	cs_cid =	(uint64_t)p;
	cs_pid =	0;
	cs_zoneid =	0;
};

#pragma D binding "1.13" translator
translator sctpsinfo_t < struct sctp_tcb *p > {
	sctps_addr =		(uintptr_t)p;
	sctps_num_raddrs =	p == NULL ? -1 : p->asoc.numnets;
	sctps_raddrs =		p == NULL ? NULL : (uintptr_t)(p->asoc.nets.tqh_first);
	sctps_num_laddrs =	p == NULL ? -1 : 
	    p->sctp_ep == NULL ? -1 :
	    p->sctp_ep->laddr_count;
	sctps_laddrs =		p == NULL ? NULL :
	    p->sctp_ep == NULL ? NULL :
	    (uintptr_t)(p->sctp_ep->sctp_addr_list.lh_first);
	sctps_lport =		p == NULL ? 0 :
	    p->sctp_ep == NULL ? 0 :
	    ntohs(p->sctp_ep->ip_inp.inp.inp_inc.inc_ie.ie_lport);
	sctps_rport =		p == NULL ? 0 : ntohs(p->rport);
	sctps_laddr =		p == NULL ? "<unknown>" :
	    p->asoc.primary_destination == NULL ? "<unknown>" :
	    p->asoc.primary_destination->ro._s_addr == NULL ? "<unknown>" :
	    p->asoc.primary_destination->ro._s_addr->address.sa.sa_family == AF_INET ?
	    inet_ntoa(&p->asoc.primary_destination->ro._s_addr->address.sin.sin_addr.s_addr) :
	    p->asoc.primary_destination->ro._s_addr->address.sa.sa_family == AF_INET6 ?
	    inet_ntoa6(&p->asoc.primary_destination->ro._s_addr->address.sin6.sin6_addr) :
	    "<unknown>";
	sctps_raddr =		p == NULL ? "<unknown>" :
	    p->asoc.primary_destination == NULL ? "<unknown>" :
	    p->asoc.primary_destination->ro._l_addr.sa.sa_family == AF_INET ?
	    inet_ntoa(&p->asoc.primary_destination->ro._l_addr.sin.sin_addr.s_addr) :
	    p->asoc.primary_destination->ro._l_addr.sa.sa_family == AF_INET6 ?
	    inet_ntoa6(&p->asoc.primary_destination->ro._l_addr.sin6.sin6_addr) :
	    "<unknown>";
	sctps_state =		p == NULL ? SCTP_STATE_CLOSED : p->asoc.state;
};

#pragma D binding "1.13" translator
translator sctpinfo_t < struct sctphdr *p > {
	sctp_sport =		p == NULL ? 0 : ntohs(p->src_port);
	sctp_dport =		p == NULL ? 0 : ntohs(p->dest_port);
	sctp_verify =		p == NULL ? 0 : ntohl(p->v_tag);
	sctp_checksum =		p == NULL ? 0 : ntohl(p->checksum);
	sctp_hdr =		p;
};

#pragma D binding "1.13" translator
translator sctplsinfo_t < int state > {
	sctps_state = state;
};
