/*
 * Layer 2 defines
 *
 * Copyright 2008  by Karsten Keil <kkeil@novell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/mISDNif.h>
#include <linux/skbuff.h>
#include "fsm.h"

#define MAX_WINDOW	8

struct manager {
	struct mISDNchannel	ch;
	struct mISDNchannel	bcast;
	u_long			options;
	struct list_head	layer2;
	rwlock_t		lock;
	struct FsmInst		deact;
	struct FsmTimer		datimer;
	struct sk_buff_head	sendq;
	struct mISDNchannel	*up;
	u_int			nextid;
	u_int			lastid;
};

struct teimgr {
	int			ri;
	int			rcnt;
	struct FsmInst		tei_m;
	struct FsmTimer		timer;
	int			tval, nval;
	struct layer2		*l2;
	struct manager		*mgr;
};

struct laddr {
	u_char	A;
	u_char	B;
};

struct layer2 {
	struct list_head	list;
	struct mISDNchannel	ch;
	u_long			flag;
	int			id;
	struct mISDNchannel	*up;
	signed char		sapi;
	signed char		tei;
	struct laddr		addr;
	u_int			maxlen;
	struct teimgr		*tm;
	u_int			vs, va, vr;
	int			rc;
	u_int			window;
	u_int			sow;
	struct FsmInst		l2m;
	struct FsmTimer		t200, t203;
	int			T200, N200, T203;
	u_int			next_id;
	u_int			down_id;
	struct sk_buff		*windowar[MAX_WINDOW];
	struct sk_buff_head	i_queue;
	struct sk_buff_head	ui_queue;
	struct sk_buff_head	down_queue;
	struct sk_buff_head	tmp_queue;
};

enum {
	ST_L2_1,
	ST_L2_2,
	ST_L2_3,
	ST_L2_4,
	ST_L2_5,
	ST_L2_6,
	ST_L2_7,
	ST_L2_8,
};

#define L2_STATE_COUNT (ST_L2_8 + 1)

extern struct layer2	*create_l2(struct mISDNchannel *, u_int,
				   u_long, int, int);
extern int		tei_l2(struct layer2 *, u_int, u_long arg);


/* from tei.c */
extern int		l2_tei(struct layer2 *, u_int, u_long arg);
extern void		TEIrelease(struct layer2 *);
extern int		TEIInit(u_int *);
extern void		TEIFree(void);

#define MAX_L2HEADER_LEN 4

#define RR	0x01
#define RNR	0x05
#define REJ	0x09
#define SABME	0x6f
#define SABM	0x2f
#define DM	0x0f
#define UI	0x03
#define DISC	0x43
#define UA	0x63
#define FRMR	0x87
#define XID	0xaf

#define CMD	0
#define RSP	1

#define LC_FLUSH_WAIT 1

#define FLG_LAPB	0
#define FLG_LAPD	1
#define FLG_ORIG	2
#define FLG_MOD128	3
#define FLG_PEND_REL	4
#define FLG_L3_INIT	5
#define FLG_T200_RUN	6
#define FLG_ACK_PEND	7
#define FLG_REJEXC	8
#define FLG_OWN_BUSY	9
#define FLG_PEER_BUSY	10
#define FLG_DCHAN_BUSY	11
#define FLG_L1_ACTIV	12
#define FLG_ESTAB_PEND	13
#define FLG_PTP		14
#define FLG_FIXED_TEI	15
#define FLG_L2BLOCK	16
#define FLG_L1_NOTREADY	17
#define FLG_LAPD_NET	18
