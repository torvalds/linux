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

#pragma D depends_on module kernel
#pragma D depends_on module siftr
#pragma D depends_on provider tcp

/*
 * Convert a SIFTR direction value to a string
 */
#pragma D binding "1.12.1" SIFTR_IN
inline int SIFTR_IN =	1;
#pragma D binding "1.12.1" SIFTR_OUT
inline int SIFTR_OUT =	2;

/* SIFTR direction strings. */
#pragma D binding "1.12.1" siftr_dir_string
inline string siftr_dir_string[uint8_t direction] =
	direction == SIFTR_IN ?	"in" :
	direction == SIFTR_OUT ? "out" :
	"unknown" ;

typedef struct siftrinfo {
	struct timeval		tval;
	uint8_t			direction;
	uint8_t			ipver;
	uint32_t		hash;
	uint16_t		tcp_localport;
	uint16_t		tcp_foreignport;
	uint64_t		snd_cwnd;
	u_long			snd_wnd;
	u_long			rcv_wnd;
	u_long			snd_bwnd;
	u_long			snd_ssthresh;
	int			conn_state;
	u_int			max_seg_size;
	int			smoothed_rtt;
	u_char			sack_enabled;
	u_char			snd_scale;
	u_char			rcv_scale;
	u_int			flags;
	int			rxt_length;
	u_int			snd_buf_hiwater;
	u_int			snd_buf_cc;
	u_int			rcv_buf_hiwater;
	u_int			rcv_buf_cc;
	u_int			sent_inflight_bytes;
	int			t_segqlen;
	u_int			flowid;
	u_int			flowtype;
} siftrinfo_t;

#pragma D binding "1.12.1" translator
translator siftrinfo_t < struct pkt_node *p > {
	direction = 		p == NULL ? 0 : p->direction;
	ipver =			p == NULL ? 0 : p->ipver;
	hash = 			p == NULL ? 0 : p->hash;
	tcp_localport =		p == NULL ? 0 : ntohs(p->tcp_localport);
	tcp_foreignport =	p == NULL ? 0 : ntohs(p->tcp_foreignport);
	snd_cwnd =		p == NULL ? 0 : p->snd_cwnd;
	snd_wnd =		p == NULL ? 0 : p->snd_wnd;
	rcv_wnd =		p == NULL ? 0 : p->rcv_wnd;
	snd_bwnd =		p == NULL ? 0 : p->snd_bwnd;
	snd_ssthresh =		p == NULL ? 0 : p->snd_ssthresh;
	conn_state =		p == NULL ? 0 : p->conn_state;
	max_seg_size = 		p == NULL ? 0 : p->max_seg_size;
	smoothed_rtt =		p == NULL ? 0 : p->smoothed_rtt;
	sack_enabled = 		p == NULL ? 0 : p->sack_enabled;
	snd_scale =		p == NULL ? 0 : p->snd_scale;
	rcv_scale =		p == NULL ? 0 : p->rcv_scale;
	flags =			p == NULL ? 0 : p->flags;
	rxt_length = 		p == NULL ? 0 : p->rxt_length;
	snd_buf_hiwater =	p == NULL ? 0 : p->snd_buf_hiwater;
	snd_buf_cc = 		p == NULL ? 0 : p->snd_buf_cc;
	rcv_buf_hiwater = 	p == NULL ? 0 : p->rcv_buf_hiwater;
	rcv_buf_cc = 		p == NULL ? 0 : p->rcv_buf_cc;
	sent_inflight_bytes = 	p == NULL ? 0 : p->sent_inflight_bytes;
	t_segqlen =		p == NULL ? 0 : p->t_segqlen;
	flowid = 		p == NULL ? 0 : p->flowid;
	flowtype = 		p == NULL ? 0 : p->flowtype;
};
