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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013 Mark Johnston <markj@freebsd.org>
 */

#pragma D depends_on library ip.d
#pragma D depends_on module kernel
#pragma D depends_on provider tcp

/*
 * Convert a TCP state value to a string.
 */
#pragma D binding "1.6.3" TCPS_CLOSED
inline int TCPS_CLOSED =	0;
#pragma D binding "1.6.3" TCPS_LISTEN
inline int TCPS_LISTEN =	1;
#pragma D binding "1.6.3" TCPS_SYN_SENT
inline int TCPS_SYN_SENT =	2;
#pragma D binding "1.6.3" TCPS_SYN_RECEIVED
inline int TCPS_SYN_RECEIVED =	3;
#pragma D binding "1.6.3" TCPS_ESTABLISHED
inline int TCPS_ESTABLISHED =	4;
#pragma D binding "1.6.3" TCPS_CLOSE_WAIT
inline int TCPS_CLOSE_WAIT =	5;
#pragma D binding "1.6.3" TCPS_FIN_WAIT_1
inline int TCPS_FIN_WAIT_1 =	6;
#pragma D binding "1.6.3" TCPS_CLOSING
inline int TCPS_CLOSING =	7;
#pragma D binding "1.6.3" TCPS_LAST_ACK
inline int TCPS_LAST_ACK =	8;
#pragma D binding "1.6.3" TCPS_FIN_WAIT_2
inline int TCPS_FIN_WAIT_2 =	9;
#pragma D binding "1.6.3" TCPS_TIME_WAIT
inline int TCPS_TIME_WAIT =	10;

/*
 * For compatibility also provide the names used by Solaris.
 */
#pragma D binding "1.13" TCP_STATE_CLOSED
inline int TCP_STATE_CLOSED =		TCPS_CLOSED;
#pragma D binding "1.13" TCP_STATE_LISTEN
inline int TCP_STATE_LISTEN =		TCPS_LISTEN;
#pragma D binding "1.13" TCP_STATE_SYN_SENT
inline int TCP_STATE_SYN_SENT =		TCPS_SYN_SENT;
#pragma D binding "1.13" TCP_STATE_SYN_RECEIVED
inline int TCP_STATE_SYN_RECEIVED =	TCPS_SYN_RECEIVED;
#pragma D binding "1.13" TCP_STATE_ESTABLISHED
inline int TCP_STATE_ESTABLISHED =	TCPS_ESTABLISHED;
#pragma D binding "1.13" TCP_STATE_CLOSE_WAIT
inline int TCP_STATE_CLOSE_WAIT =	TCPS_CLOSE_WAIT;
#pragma D binding "1.13" TCP_STATE_FIN_WAIT_1
inline int TCP_STATE_FIN_WAIT_1 =	TCPS_FIN_WAIT_1;
#pragma D binding "1.13" TCP_STATE_CLOSING
inline int TCP_STATE_CLOSING =		TCPS_CLOSING;
#pragma D binding "1.13" TCP_STATE_LAST_ACK
inline int TCP_STATE_LAST_ACK =		TCPS_LAST_ACK;
#pragma D binding "1.13" TCP_STATE_FIN_WAIT_2
inline int TCP_STATE_FIN_WAIT_2 =	TCPS_FIN_WAIT_2;
#pragma D binding "1.13" TCP_STATE_TIME_WAIT
inline int TCP_STATE_TIME_WAIT =	TCPS_TIME_WAIT;

/* TCP segment flags. */
#pragma D binding "1.6.3" TH_FIN
inline uint8_t TH_FIN =		0x01;
#pragma D binding "1.6.3" TH_SYN
inline uint8_t TH_SYN =		0x02;
#pragma D binding "1.6.3" TH_RST
inline uint8_t TH_RST =		0x04;
#pragma D binding "1.6.3" TH_PUSH
inline uint8_t TH_PUSH =	0x08;
#pragma D binding "1.6.3" TH_ACK
inline uint8_t TH_ACK =		0x10;
#pragma D binding "1.6.3" TH_URG
inline uint8_t TH_URG =		0x20;
#pragma D binding "1.6.3" TH_ECE
inline uint8_t TH_ECE =		0x40;
#pragma D binding "1.6.3" TH_CWR
inline uint8_t TH_CWR =		0x80;

/* TCP connection state strings. */
#pragma D binding "1.6.3" tcp_state_string
inline string tcp_state_string[int32_t state] =
	state == TCPS_CLOSED ?		"state-closed" :
	state == TCPS_LISTEN ?		"state-listen" :
	state == TCPS_SYN_SENT ?	"state-syn-sent" :
	state == TCPS_SYN_RECEIVED ?	"state-syn-received" :
	state == TCPS_ESTABLISHED ?	"state-established" :
	state == TCPS_CLOSE_WAIT ?	"state-close-wait" :
	state == TCPS_FIN_WAIT_1 ?	"state-fin-wait-1" :
	state == TCPS_CLOSING ?		"state-closing" :
	state == TCPS_LAST_ACK ?	"state-last-ack" :
	state == TCPS_FIN_WAIT_2 ?	"state-fin-wait-2" :
	state == TCPS_TIME_WAIT ?	"state-time-wait" :
	"<unknown>";

/*
 * tcpsinfo contains stable TCP details from tcp_t.
 */
typedef struct tcpsinfo {
	uintptr_t tcps_addr;
	int tcps_local;			/* is delivered locally, boolean */
	int tcps_active;		/* active open (from here), boolean */
	uint16_t tcps_lport;		/* local port */
	uint16_t tcps_rport;		/* remote port */
	string tcps_laddr;		/* local address, as a string */
	string tcps_raddr;		/* remote address, as a string */
	int32_t tcps_state;		/* TCP state */
	uint32_t tcps_iss;		/* Initial sequence # sent */
	uint32_t tcps_irs;		/* Initial sequence # received */
	uint32_t tcps_suna;		/* sequence # sent but unacked */
	uint32_t tcps_smax;		/* highest sequence number sent */
	uint32_t tcps_snxt;		/* next sequence # to send */
	uint32_t tcps_rack;		/* sequence # we have acked */
	uint32_t tcps_rnxt;		/* next sequence # expected */
	u_long tcps_swnd;		/* send window size */
	int32_t tcps_snd_ws;		/* send window scaling */
	uint32_t tcps_swl1;		/* window update seg seq number */
	uint32_t tcps_swl2;		/* window update seg ack number */
	uint32_t tcps_rup;		/* receive urgent pointer */
	uint32_t tcps_radv;		/* advertised window */
	u_long tcps_rwnd;		/* receive window size */
	int32_t tcps_rcv_ws;		/* receive window scaling */
	u_long tcps_cwnd;		/* congestion window */
	u_long tcps_cwnd_ssthresh;	/* threshold for congestion avoidance */
	uint32_t tcps_srecover;		/* for use in NewReno Fast Recovery */
	uint32_t tcps_sack_fack;	/* SACK sequence # we have acked */
	uint32_t tcps_sack_snxt;	/* next SACK seq # for retransmission */
	uint32_t tcps_rto;		/* round-trip timeout, msec */
	uint32_t tcps_mss;		/* max segment size */
	int tcps_retransmit;		/* retransmit send event, boolean */
	int tcps_srtt;			/* smoothed RTT in units of (TCP_RTT_SCALE*hz) */
	int tcps_debug;			/* socket has SO_DEBUG set */
	int tcps_cookie;		/* expose the socket's SO_USER_COOKIE */
	int32_t tcps_dupacks;		/* consecutive dup acks received */
	uint32_t tcps_rtttime;		/* RTT measurement start time */
	uint32_t tcps_rtseq;		/* sequence # being timed */
	uint32_t tcps_ts_recent;	/* timestamp echo data */
} tcpsinfo_t;

/*
 * tcplsinfo provides the old tcp state for state changes.
 */
typedef struct tcplsinfo {
	int32_t tcps_state;		/* previous TCP state */
} tcplsinfo_t;

/*
 * tcpinfo is the TCP header fields.
 */
typedef struct tcpinfo {
	uint16_t tcp_sport;		/* source port */
	uint16_t tcp_dport;		/* destination port */
	uint32_t tcp_seq;		/* sequence number */
	uint32_t tcp_ack;		/* acknowledgment number */
	uint8_t tcp_offset;		/* data offset, in bytes */
	uint8_t tcp_flags;		/* flags */
	uint16_t tcp_window;		/* window size */
	uint16_t tcp_checksum;		/* checksum */
	uint16_t tcp_urgent;		/* urgent data pointer */
	struct tcphdr *tcp_hdr;		/* raw TCP header */
} tcpinfo_t;

/*
 * A clone of tcpinfo_t used to handle the fact that the TCP input path
 * overwrites some fields of the TCP header with their host-order equivalents.
 * Unfortunately, DTrace doesn't let us simply typedef a new name for struct
 * tcpinfo and define a separate translator for it.
 */
typedef struct tcpinfoh {
	uint16_t tcp_sport;		/* source port */
	uint16_t tcp_dport;		/* destination port */
	uint32_t tcp_seq;		/* sequence number */
	uint32_t tcp_ack;		/* acknowledgment number */
	uint8_t tcp_offset;		/* data offset, in bytes */
	uint8_t tcp_flags;		/* flags */
	uint16_t tcp_window;		/* window size */
	uint16_t tcp_checksum;		/* checksum */
	uint16_t tcp_urgent;		/* urgent data pointer */
	struct tcphdr *tcp_hdr;		/* raw TCP header */
} tcpinfoh_t;

#pragma D binding "1.6.3" translator
translator csinfo_t < struct tcpcb *p > {
	cs_addr =	NULL;
	cs_cid =	(uint64_t)(p == NULL ? 0 : p->t_inpcb);
	cs_pid =	0;
	cs_zoneid =	0;
};

#pragma D binding "1.6.3" translator
translator tcpsinfo_t < struct tcpcb *p > {
	tcps_addr =		(uintptr_t)p;
	tcps_local =		-1; /* XXX */
	tcps_active =		-1; /* XXX */
	tcps_lport =		p == NULL ? 0 : ntohs(p->t_inpcb->inp_inc.inc_ie.ie_lport);
	tcps_rport =		p == NULL ? 0 : ntohs(p->t_inpcb->inp_inc.inc_ie.ie_fport);
	tcps_laddr =		p == NULL ? "<unknown>" :
	    p->t_inpcb->inp_vflag == INP_IPV4 ?
	    inet_ntoa(&p->t_inpcb->inp_inc.inc_ie.ie_dependladdr.id46_addr.ia46_addr4.s_addr) :
	    inet_ntoa6(&p->t_inpcb->inp_inc.inc_ie.ie_dependladdr.id6_addr);
	tcps_raddr =		p == NULL ? "<unknown>" :
	    p->t_inpcb->inp_vflag == INP_IPV4 ?
	    inet_ntoa(&p->t_inpcb->inp_inc.inc_ie.ie_dependfaddr.id46_addr.ia46_addr4.s_addr) :
	    inet_ntoa6(&p->t_inpcb->inp_inc.inc_ie.ie_dependfaddr.id6_addr);
	tcps_state =		p == NULL ? -1 : p->t_state;
	tcps_iss =		p == NULL ? 0  : p->iss;
	tcps_irs =		p == NULL ? 0  : p->irs;
	tcps_suna =		p == NULL ? 0  : p->snd_una;
	tcps_smax =		p == NULL ? 0  : p->snd_max;
	tcps_snxt =		p == NULL ? 0  : p->snd_nxt;
	tcps_rack =		p == NULL ? 0  : p->last_ack_sent;
	tcps_rnxt =		p == NULL ? 0  : p->rcv_nxt;
	tcps_swnd =		p == NULL ? -1 : p->snd_wnd;
	tcps_snd_ws =		p == NULL ? -1 : p->snd_scale;
	tcps_swl1 =		p == NULL ? -1 : p->snd_wl1;
	tcps_swl2 = 		p == NULL ? -1 : p->snd_wl2;
	tcps_radv =		p == NULL ? -1 : p->rcv_adv;
	tcps_rwnd =		p == NULL ? -1 : p->rcv_wnd;
	tcps_rup =		p == NULL ? -1 : p->rcv_up;
	tcps_rcv_ws =		p == NULL ? -1 : p->rcv_scale;
	tcps_cwnd =		p == NULL ? -1 : p->snd_cwnd;
	tcps_cwnd_ssthresh =	p == NULL ? -1 : p->snd_ssthresh;
	tcps_srecover =		p == NULL ? -1 : p->snd_recover;
	tcps_sack_fack =	p == NULL ? 0  : p->snd_fack;
	tcps_sack_snxt =	p == NULL ? 0  : p->sack_newdata;
	tcps_rto =		p == NULL ? -1 : (p->t_rxtcur * 1000) / `hz;
	tcps_mss =		p == NULL ? -1 : p->t_maxseg;
	tcps_retransmit =	p == NULL ? -1 : p->t_rxtshift > 0 ? 1 : 0;
	tcps_srtt =		p == NULL ? -1 : p->t_srtt;   /* smoothed RTT in units of (TCP_RTT_SCALE*hz) */
	tcps_debug =		p == NULL ? 0 :
	    p->t_inpcb->inp_socket->so_options & 1;
	tcps_cookie =		p == NULL ? -1 :
	    p->t_inpcb->inp_socket->so_user_cookie;
	tcps_dupacks =		p == NULL ? -1 : p->t_dupacks;
	tcps_rtttime =		p == NULL ? -1 : p->t_rtttime;
	tcps_rtseq =		p == NULL ? -1 : p->t_rtseq;
	tcps_ts_recent =	p == NULL ? -1 : p->ts_recent;
};

#pragma D binding "1.6.3" translator
translator tcpinfo_t < struct tcphdr *p > {
	tcp_sport =	p == NULL ? 0  : ntohs(p->th_sport);
	tcp_dport =	p == NULL ? 0  : ntohs(p->th_dport);
	tcp_seq =	p == NULL ? -1 : ntohl(p->th_seq);
	tcp_ack =	p == NULL ? -1 : ntohl(p->th_ack);
	tcp_offset =	p == NULL ? -1 : (p->th_off >> 2);
	tcp_flags =	p == NULL ? 0  : p->th_flags;
	tcp_window =	p == NULL ? 0  : ntohs(p->th_win);
	tcp_checksum =	p == NULL ? 0  : ntohs(p->th_sum);
	tcp_urgent =	p == NULL ? 0  : ntohs(p->th_urp);
	tcp_hdr =	(struct tcphdr *)p;
};

/*
 * This translator differs from the one for tcpinfo_t in that the sequence
 * number, acknowledgement number, window size and urgent pointer are already
 * in host order and thus don't need to be converted.
 */
#pragma D binding "1.6.3" translator
translator tcpinfoh_t < struct tcphdr *p > {
	tcp_sport =	p == NULL ? 0  : ntohs(p->th_sport);
	tcp_dport =	p == NULL ? 0  : ntohs(p->th_dport);
	tcp_seq =	p == NULL ? -1 : p->th_seq;
	tcp_ack =	p == NULL ? -1 : p->th_ack;
	tcp_offset =	p == NULL ? -1 : (p->th_off >> 2);
	tcp_flags =	p == NULL ? 0  : p->th_flags;
	tcp_window =	p == NULL ? 0  : p->th_win;
	tcp_checksum =	p == NULL ? 0  : ntohs(p->th_sum);
	tcp_urgent =	p == NULL ? 0  : p->th_urp;
	tcp_hdr =	(struct tcphdr *)p;
};

#pragma D binding "1.6.3" translator
translator tcplsinfo_t < int s > {
	tcps_state =	s;
};


/* Support for TCP debug */

#pragma D binding "1.12.1" TA_INPUT
inline int TA_INPUT =	0;
#pragma D binding "1.12.1" TA_OUTPUT
inline int TA_OUTPUT =	1;
#pragma D binding "1.12.1" TA_USER
inline int TA_USER =	2;
#pragma D binding "1.12.1" TA_RESPOND
inline int TA_RESPOND =	3;
#pragma D binding "1.12.1" TA_DROP
inline int TA_DROP =	4;

/* direction strings. */

#pragma D binding "1.12.1" tcpdebug_dir_string
inline string tcpdebug_dir_string[uint8_t direction] =
	direction == TA_INPUT ?	"input" :
	direction == TA_OUTPUT ? "output" :
	direction == TA_USER ? "user" :
	direction == TA_RESPOND ? "respond" :
	direction == TA_OUTPUT ? "drop" :
	"unknown" ;

#pragma D binding "1.12.1" tcpflag_string
inline string tcpflag_string[uint8_t flags] =
	flags & TH_FIN ?	"FIN" :
	flags & TH_SYN ?	"SYN" :
	flags & TH_RST ?	"RST" :
	flags & TH_PUSH ?	"PUSH" :
	flags & TH_ACK ?	"ACK" :
	flags & TH_URG ?	"URG" :
	flags & TH_ECE ?	"ECE" :
	flags & TH_CWR ?	"CWR" :
	"unknown" ;

#pragma D binding "1.12.1" PRU_ATTACH
inline int PRU_ATTACH		= 0;
#pragma D binding "1.12.1" PRU_DETACH
inline int PRU_DETACH		= 1;
#pragma D binding "1.12.1" PRU_BIND
inline int PRU_BIND		= 2;
#pragma D binding "1.12.1" PRU_LISTEN
inline int PRU_LISTEN		= 3;
#pragma D binding "1.12.1" PRU_CONNECT
inline int PRU_CONNECT		= 4;
#pragma D binding "1.12.1" PRU_ACCEPT
inline int PRU_ACCEPT		= 5 ;
#pragma D binding "1.12.1" PRU_DISCONNECT
inline int PRU_DISCONNECT	= 6;
#pragma D binding "1.12.1" PRU_SHUTDOWN
inline int PRU_SHUTDOWN		= 7;
#pragma D binding "1.12.1" PRU_RCVD
inline int PRU_RCVD		= 8;
#pragma D binding "1.12.1" PRU_SEND
inline int PRU_SEND		= 9;
#pragma D binding "1.12.1" PRU_ABORT
inline int PRU_ABORT		= 10;
#pragma D binding "1.12.1" PRU_CONTROL
inline int PRU_CONTROL		= 11;
#pragma D binding "1.12.1" PRU_SENSE
inline int PRU_SENSE		= 12;
#pragma D binding "1.12.1" PRU_RCVOOB
inline int PRU_RCVOOB		= 13;
#pragma D binding "1.12.1" PRU_SENDOOB
inline int PRU_SENDOOB		= 14;
#pragma D binding "1.12.1" PRU_SOCKADDR
inline int PRU_SOCKADDR		= 15;
#pragma D binding "1.12.1" PRU_PEERADDR
inline int PRU_PEERADDR		= 16;
#pragma D binding "1.12.1" PRU_CONNECT2
inline int PRU_CONNECT2		= 17;
#pragma D binding "1.12.1" PRU_FASTTIMO
inline int PRU_FASTTIMO		= 18;
#pragma D binding "1.12.1" PRU_SLOWTIMO
inline int PRU_SLOWTIMO		= 19;
#pragma D binding "1.12.1" PRU_PROTORCV
inline int PRU_PROTORCV		= 20;
#pragma D binding "1.12.1" PRU_PROTOSEND
inline int PRU_PROTOSEND	= 21;
#pragma D binding "1.12.1" PRU_SEND_EOF
inline int PRU_SEND_EOF		= 22;
#pragma D binding "1.12.1" PRU_SOSETLABEL
inline int PRU_SOSETLABEL	= 23;
#pragma D binding "1.12.1" PRU_CLOSE
inline int PRU_CLOSE		= 24;
#pragma D binding "1.12.1" PRU_FLUSH
inline int PRU_FLUSH		= 25;

#pragma D binding "1.12.1" prureq_string
inline string prureq_string[uint8_t req] =
	req == PRU_ATTACH ?	"ATTACH" :
	req == PRU_DETACH ?	"DETACH" :
	req == PRU_BIND ?	"BIND" :
	req == PRU_LISTEN ?	"LISTEN" :
	req == PRU_CONNECT ?	"CONNECT" :
	req == PRU_ACCEPT ?	"ACCEPT" :
	req == PRU_DISCONNECT ?	"DISCONNECT" :
	req == PRU_SHUTDOWN ?	"SHUTDOWN" :
	req == PRU_RCVD ?	"RCVD" :
	req == PRU_SEND ?	"SEND" :
	req == PRU_ABORT ?	"ABORT" :
	req == PRU_CONTROL ?	"CONTROL" :
	req == PRU_SENSE ?	"SENSE" :
	req == PRU_RCVOOB ?	"RCVOOB" :
	req == PRU_SENDOOB ?	"SENDOOB" :
	req == PRU_SOCKADDR ?	"SOCKADDR" :
	req == PRU_PEERADDR ?	"PEERADDR" :
	req == PRU_CONNECT2 ?	"CONNECT2" :
	req == PRU_FASTTIMO ?	"FASTTIMO" :
	req == PRU_SLOWTIMO ?	"SLOWTIMO" :
	req == PRU_PROTORCV ?	"PROTORCV" :
	req == PRU_PROTOSEND ?	"PROTOSEND" :
	req == PRU_SEND ?	"SEND_EOF" :
	req == PRU_SOSETLABEL ?	"SOSETLABEL" :
	req == PRU_CLOSE ?	"CLOSE" :
	req == PRU_FLUSH ?	"FLUSE" :
	"unknown" ;
