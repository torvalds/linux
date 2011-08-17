/*
 *
 * Author	Karsten Keil <kkeil@novell.com>
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
#include <linux/slab.h>
#include "core.h"
#include "fsm.h"
#include "layer2.h"

static u_int *debug;

static
struct Fsm l2fsm = {NULL, 0, 0, NULL, NULL};

static char *strL2State[] =
{
	"ST_L2_1",
	"ST_L2_2",
	"ST_L2_3",
	"ST_L2_4",
	"ST_L2_5",
	"ST_L2_6",
	"ST_L2_7",
	"ST_L2_8",
};

enum {
	EV_L2_UI,
	EV_L2_SABME,
	EV_L2_DISC,
	EV_L2_DM,
	EV_L2_UA,
	EV_L2_FRMR,
	EV_L2_SUPER,
	EV_L2_I,
	EV_L2_DL_DATA,
	EV_L2_ACK_PULL,
	EV_L2_DL_UNITDATA,
	EV_L2_DL_ESTABLISH_REQ,
	EV_L2_DL_RELEASE_REQ,
	EV_L2_MDL_ASSIGN,
	EV_L2_MDL_REMOVE,
	EV_L2_MDL_ERROR,
	EV_L1_DEACTIVATE,
	EV_L2_T200,
	EV_L2_T203,
	EV_L2_SET_OWN_BUSY,
	EV_L2_CLEAR_OWN_BUSY,
	EV_L2_FRAME_ERROR,
};

#define L2_EVENT_COUNT (EV_L2_FRAME_ERROR+1)

static char *strL2Event[] =
{
	"EV_L2_UI",
	"EV_L2_SABME",
	"EV_L2_DISC",
	"EV_L2_DM",
	"EV_L2_UA",
	"EV_L2_FRMR",
	"EV_L2_SUPER",
	"EV_L2_I",
	"EV_L2_DL_DATA",
	"EV_L2_ACK_PULL",
	"EV_L2_DL_UNITDATA",
	"EV_L2_DL_ESTABLISH_REQ",
	"EV_L2_DL_RELEASE_REQ",
	"EV_L2_MDL_ASSIGN",
	"EV_L2_MDL_REMOVE",
	"EV_L2_MDL_ERROR",
	"EV_L1_DEACTIVATE",
	"EV_L2_T200",
	"EV_L2_T203",
	"EV_L2_SET_OWN_BUSY",
	"EV_L2_CLEAR_OWN_BUSY",
	"EV_L2_FRAME_ERROR",
};

static void
l2m_debug(struct FsmInst *fi, char *fmt, ...)
{
	struct layer2 *l2 = fi->userdata;
	struct va_format vaf;
	va_list va;

	if (!(*debug & DEBUG_L2_FSM))
		return;

	va_start(va, fmt);

	vaf.fmt = fmt;
	vaf.va = &va;

	printk(KERN_DEBUG "l2 (sapi %d tei %d): %pV\n",
	       l2->sapi, l2->tei, &vaf);

	va_end(va);
}

inline u_int
l2headersize(struct layer2 *l2, int ui)
{
	return ((test_bit(FLG_MOD128, &l2->flag) && (!ui)) ? 2 : 1) +
		(test_bit(FLG_LAPD, &l2->flag) ? 2 : 1);
}

inline u_int
l2addrsize(struct layer2 *l2)
{
	return test_bit(FLG_LAPD, &l2->flag) ? 2 : 1;
}

static u_int
l2_newid(struct layer2 *l2)
{
	u_int	id;

	id = l2->next_id++;
	if (id == 0x7fff)
		l2->next_id = 1;
	id <<= 16;
	id |= l2->tei << 8;
	id |= l2->sapi;
	return id;
}

static void
l2up(struct layer2 *l2, u_int prim, struct sk_buff *skb)
{
	int	err;

	if (!l2->up)
		return;
	mISDN_HEAD_PRIM(skb) = prim;
	mISDN_HEAD_ID(skb) = (l2->ch.nr << 16) | l2->ch.addr;
	err = l2->up->send(l2->up, skb);
	if (err) {
		printk(KERN_WARNING "%s: err=%d\n", __func__, err);
		dev_kfree_skb(skb);
	}
}

static void
l2up_create(struct layer2 *l2, u_int prim, int len, void *arg)
{
	struct sk_buff	*skb;
	struct mISDNhead *hh;
	int		err;

	if (!l2->up)
		return;
	skb = mI_alloc_skb(len, GFP_ATOMIC);
	if (!skb)
		return;
	hh = mISDN_HEAD_P(skb);
	hh->prim = prim;
	hh->id = (l2->ch.nr << 16) | l2->ch.addr;
	if (len)
		memcpy(skb_put(skb, len), arg, len);
	err = l2->up->send(l2->up, skb);
	if (err) {
		printk(KERN_WARNING "%s: err=%d\n", __func__, err);
		dev_kfree_skb(skb);
	}
}

static int
l2down_skb(struct layer2 *l2, struct sk_buff *skb) {
	int ret;

	ret = l2->ch.recv(l2->ch.peer, skb);
	if (ret && (*debug & DEBUG_L2_RECV))
		printk(KERN_DEBUG "l2down_skb: ret(%d)\n", ret);
	return ret;
}

static int
l2down_raw(struct layer2 *l2, struct sk_buff *skb)
{
	struct mISDNhead *hh = mISDN_HEAD_P(skb);

	if (hh->prim == PH_DATA_REQ) {
		if (test_and_set_bit(FLG_L1_NOTREADY, &l2->flag)) {
			skb_queue_tail(&l2->down_queue, skb);
			return 0;
		}
		l2->down_id = mISDN_HEAD_ID(skb);
	}
	return l2down_skb(l2, skb);
}

static int
l2down(struct layer2 *l2, u_int prim, u_int id, struct sk_buff *skb)
{
	struct mISDNhead *hh = mISDN_HEAD_P(skb);

	hh->prim = prim;
	hh->id = id;
	return l2down_raw(l2, skb);
}

static int
l2down_create(struct layer2 *l2, u_int prim, u_int id, int len, void *arg)
{
	struct sk_buff	*skb;
	int		err;
	struct mISDNhead *hh;

	skb = mI_alloc_skb(len, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;
	hh = mISDN_HEAD_P(skb);
	hh->prim = prim;
	hh->id = id;
	if (len)
		memcpy(skb_put(skb, len), arg, len);
	err = l2down_raw(l2, skb);
	if (err)
		dev_kfree_skb(skb);
	return err;
}

static int
ph_data_confirm(struct layer2 *l2, struct mISDNhead *hh, struct sk_buff *skb) {
	struct sk_buff *nskb = skb;
	int ret = -EAGAIN;

	if (test_bit(FLG_L1_NOTREADY, &l2->flag)) {
		if (hh->id == l2->down_id) {
			nskb = skb_dequeue(&l2->down_queue);
			if (nskb) {
				l2->down_id = mISDN_HEAD_ID(nskb);
				if (l2down_skb(l2, nskb)) {
					dev_kfree_skb(nskb);
					l2->down_id = MISDN_ID_NONE;
				}
			} else
				l2->down_id = MISDN_ID_NONE;
			if (ret) {
				dev_kfree_skb(skb);
				ret = 0;
			}
			if (l2->down_id == MISDN_ID_NONE) {
				test_and_clear_bit(FLG_L1_NOTREADY, &l2->flag);
				mISDN_FsmEvent(&l2->l2m, EV_L2_ACK_PULL, NULL);
			}
		}
	}
	if (!test_and_set_bit(FLG_L1_NOTREADY, &l2->flag)) {
		nskb = skb_dequeue(&l2->down_queue);
		if (nskb) {
			l2->down_id = mISDN_HEAD_ID(nskb);
			if (l2down_skb(l2, nskb)) {
				dev_kfree_skb(nskb);
				l2->down_id = MISDN_ID_NONE;
				test_and_clear_bit(FLG_L1_NOTREADY, &l2->flag);
			}
		} else
			test_and_clear_bit(FLG_L1_NOTREADY, &l2->flag);
	}
	return ret;
}

static int
l2mgr(struct layer2 *l2, u_int prim, void *arg) {
	long c = (long)arg;

	printk(KERN_WARNING
	    "l2mgr: addr:%x prim %x %c\n", l2->id, prim, (char)c);
	if (test_bit(FLG_LAPD, &l2->flag) &&
		!test_bit(FLG_FIXED_TEI, &l2->flag)) {
		switch (c) {
		case 'C':
		case 'D':
		case 'G':
		case 'H':
			l2_tei(l2, prim, (u_long)arg);
			break;
		}
	}
	return 0;
}

static void
set_peer_busy(struct layer2 *l2) {
	test_and_set_bit(FLG_PEER_BUSY, &l2->flag);
	if (skb_queue_len(&l2->i_queue) || skb_queue_len(&l2->ui_queue))
		test_and_set_bit(FLG_L2BLOCK, &l2->flag);
}

static void
clear_peer_busy(struct layer2 *l2) {
	if (test_and_clear_bit(FLG_PEER_BUSY, &l2->flag))
		test_and_clear_bit(FLG_L2BLOCK, &l2->flag);
}

static void
InitWin(struct layer2 *l2)
{
	int i;

	for (i = 0; i < MAX_WINDOW; i++)
		l2->windowar[i] = NULL;
}

static int
freewin(struct layer2 *l2)
{
	int i, cnt = 0;

	for (i = 0; i < MAX_WINDOW; i++) {
		if (l2->windowar[i]) {
			cnt++;
			dev_kfree_skb(l2->windowar[i]);
			l2->windowar[i] = NULL;
		}
	}
	return cnt;
}

static void
ReleaseWin(struct layer2 *l2)
{
	int cnt = freewin(l2);

	if (cnt)
		printk(KERN_WARNING
		    "isdnl2 freed %d skbuffs in release\n", cnt);
}

inline unsigned int
cansend(struct layer2 *l2)
{
	unsigned int p1;

	if (test_bit(FLG_MOD128, &l2->flag))
		p1 = (l2->vs - l2->va) % 128;
	else
		p1 = (l2->vs - l2->va) % 8;
	return (p1 < l2->window) && !test_bit(FLG_PEER_BUSY, &l2->flag);
}

inline void
clear_exception(struct layer2 *l2)
{
	test_and_clear_bit(FLG_ACK_PEND, &l2->flag);
	test_and_clear_bit(FLG_REJEXC, &l2->flag);
	test_and_clear_bit(FLG_OWN_BUSY, &l2->flag);
	clear_peer_busy(l2);
}

static int
sethdraddr(struct layer2 *l2, u_char *header, int rsp)
{
	u_char *ptr = header;
	int crbit = rsp;

	if (test_bit(FLG_LAPD, &l2->flag)) {
		if (test_bit(FLG_LAPD_NET, &l2->flag))
			crbit = !crbit;
		*ptr++ = (l2->sapi << 2) | (crbit ? 2 : 0);
		*ptr++ = (l2->tei << 1) | 1;
		return 2;
	} else {
		if (test_bit(FLG_ORIG, &l2->flag))
			crbit = !crbit;
		if (crbit)
			*ptr++ = l2->addr.B;
		else
			*ptr++ = l2->addr.A;
		return 1;
	}
}

static inline void
enqueue_super(struct layer2 *l2, struct sk_buff *skb)
{
	if (l2down(l2, PH_DATA_REQ, l2_newid(l2), skb))
		dev_kfree_skb(skb);
}

static inline void
enqueue_ui(struct layer2 *l2, struct sk_buff *skb)
{
	if (l2->tm)
		l2_tei(l2, MDL_STATUS_UI_IND, 0);
	if (l2down(l2, PH_DATA_REQ, l2_newid(l2), skb))
		dev_kfree_skb(skb);
}

inline int
IsUI(u_char *data)
{
	return (data[0] & 0xef) == UI;
}

inline int
IsUA(u_char *data)
{
	return (data[0] & 0xef) == UA;
}

inline int
IsDM(u_char *data)
{
	return (data[0] & 0xef) == DM;
}

inline int
IsDISC(u_char *data)
{
	return (data[0] & 0xef) == DISC;
}

inline int
IsRR(u_char *data, struct layer2 *l2)
{
	if (test_bit(FLG_MOD128, &l2->flag))
		return data[0] == RR;
	else
		return (data[0] & 0xf) == 1;
}

inline int
IsSFrame(u_char *data, struct layer2 *l2)
{
	register u_char d = *data;

	if (!test_bit(FLG_MOD128, &l2->flag))
		d &= 0xf;
	return ((d & 0xf3) == 1) && ((d & 0x0c) != 0x0c);
}

inline int
IsSABME(u_char *data, struct layer2 *l2)
{
	u_char d = data[0] & ~0x10;

	return test_bit(FLG_MOD128, &l2->flag) ? d == SABME : d == SABM;
}

inline int
IsREJ(u_char *data, struct layer2 *l2)
{
	return test_bit(FLG_MOD128, &l2->flag) ?
		data[0] == REJ : (data[0] & 0xf) == REJ;
}

inline int
IsFRMR(u_char *data)
{
	return (data[0] & 0xef) == FRMR;
}

inline int
IsRNR(u_char *data, struct layer2 *l2)
{
	return test_bit(FLG_MOD128, &l2->flag) ?
	    data[0] == RNR : (data[0] & 0xf) == RNR;
}

static int
iframe_error(struct layer2 *l2, struct sk_buff *skb)
{
	u_int	i;
	int	rsp = *skb->data & 0x2;

	i = l2addrsize(l2) + (test_bit(FLG_MOD128, &l2->flag) ? 2 : 1);
	if (test_bit(FLG_ORIG, &l2->flag))
		rsp = !rsp;
	if (rsp)
		return 'L';
	if (skb->len < i)
		return 'N';
	if ((skb->len - i) > l2->maxlen)
		return 'O';
	return 0;
}

static int
super_error(struct layer2 *l2, struct sk_buff *skb)
{
	if (skb->len != l2addrsize(l2) +
	    (test_bit(FLG_MOD128, &l2->flag) ? 2 : 1))
		return 'N';
	return 0;
}

static int
unnum_error(struct layer2 *l2, struct sk_buff *skb, int wantrsp)
{
	int rsp = (*skb->data & 0x2) >> 1;
	if (test_bit(FLG_ORIG, &l2->flag))
		rsp = !rsp;
	if (rsp != wantrsp)
		return 'L';
	if (skb->len != l2addrsize(l2) + 1)
		return 'N';
	return 0;
}

static int
UI_error(struct layer2 *l2, struct sk_buff *skb)
{
	int rsp = *skb->data & 0x2;
	if (test_bit(FLG_ORIG, &l2->flag))
		rsp = !rsp;
	if (rsp)
		return 'L';
	if (skb->len > l2->maxlen + l2addrsize(l2) + 1)
		return 'O';
	return 0;
}

static int
FRMR_error(struct layer2 *l2, struct sk_buff *skb)
{
	u_int	headers = l2addrsize(l2) + 1;
	u_char	*datap = skb->data + headers;
	int	rsp = *skb->data & 0x2;

	if (test_bit(FLG_ORIG, &l2->flag))
		rsp = !rsp;
	if (!rsp)
		return 'L';
	if (test_bit(FLG_MOD128, &l2->flag)) {
		if (skb->len < headers + 5)
			return 'N';
		else if (*debug & DEBUG_L2)
			l2m_debug(&l2->l2m,
			    "FRMR information %2x %2x %2x %2x %2x",
			    datap[0], datap[1], datap[2], datap[3], datap[4]);
	} else {
		if (skb->len < headers + 3)
			return 'N';
		else if (*debug & DEBUG_L2)
			l2m_debug(&l2->l2m,
			    "FRMR information %2x %2x %2x",
			    datap[0], datap[1], datap[2]);
	}
	return 0;
}

static unsigned int
legalnr(struct layer2 *l2, unsigned int nr)
{
	if (test_bit(FLG_MOD128, &l2->flag))
		return ((nr - l2->va) % 128) <= ((l2->vs - l2->va) % 128);
	else
		return ((nr - l2->va) % 8) <= ((l2->vs - l2->va) % 8);
}

static void
setva(struct layer2 *l2, unsigned int nr)
{
	struct sk_buff	*skb;

	while (l2->va != nr) {
		l2->va++;
		if (test_bit(FLG_MOD128, &l2->flag))
			l2->va %= 128;
		else
			l2->va %= 8;
		if (l2->windowar[l2->sow]) {
			skb_trim(l2->windowar[l2->sow], 0);
			skb_queue_tail(&l2->tmp_queue, l2->windowar[l2->sow]);
			l2->windowar[l2->sow] = NULL;
		}
		l2->sow = (l2->sow + 1) % l2->window;
	}
	skb = skb_dequeue(&l2->tmp_queue);
	while (skb) {
		dev_kfree_skb(skb);
		skb = skb_dequeue(&l2->tmp_queue);
	}
}

static void
send_uframe(struct layer2 *l2, struct sk_buff *skb, u_char cmd, u_char cr)
{
	u_char tmp[MAX_L2HEADER_LEN];
	int i;

	i = sethdraddr(l2, tmp, cr);
	tmp[i++] = cmd;
	if (skb)
		skb_trim(skb, 0);
	else {
		skb = mI_alloc_skb(i, GFP_ATOMIC);
		if (!skb) {
			printk(KERN_WARNING "%s: can't alloc skbuff\n",
				__func__);
			return;
		}
	}
	memcpy(skb_put(skb, i), tmp, i);
	enqueue_super(l2, skb);
}


inline u_char
get_PollFlag(struct layer2 *l2, struct sk_buff *skb)
{
	return skb->data[l2addrsize(l2)] & 0x10;
}

inline u_char
get_PollFlagFree(struct layer2 *l2, struct sk_buff *skb)
{
	u_char PF;

	PF = get_PollFlag(l2, skb);
	dev_kfree_skb(skb);
	return PF;
}

inline void
start_t200(struct layer2 *l2, int i)
{
	mISDN_FsmAddTimer(&l2->t200, l2->T200, EV_L2_T200, NULL, i);
	test_and_set_bit(FLG_T200_RUN, &l2->flag);
}

inline void
restart_t200(struct layer2 *l2, int i)
{
	mISDN_FsmRestartTimer(&l2->t200, l2->T200, EV_L2_T200, NULL, i);
	test_and_set_bit(FLG_T200_RUN, &l2->flag);
}

inline void
stop_t200(struct layer2 *l2, int i)
{
	if (test_and_clear_bit(FLG_T200_RUN, &l2->flag))
		mISDN_FsmDelTimer(&l2->t200, i);
}

inline void
st5_dl_release_l2l3(struct layer2 *l2)
{
	int pr;

	if (test_and_clear_bit(FLG_PEND_REL, &l2->flag))
		pr = DL_RELEASE_CNF;
	else
		pr = DL_RELEASE_IND;
	l2up_create(l2, pr, 0, NULL);
}

inline void
lapb_dl_release_l2l3(struct layer2 *l2, int f)
{
	if (test_bit(FLG_LAPB, &l2->flag))
		l2down_create(l2, PH_DEACTIVATE_REQ, l2_newid(l2), 0, NULL);
	l2up_create(l2, f, 0, NULL);
}

static void
establishlink(struct FsmInst *fi)
{
	struct layer2 *l2 = fi->userdata;
	u_char cmd;

	clear_exception(l2);
	l2->rc = 0;
	cmd = (test_bit(FLG_MOD128, &l2->flag) ? SABME : SABM) | 0x10;
	send_uframe(l2, NULL, cmd, CMD);
	mISDN_FsmDelTimer(&l2->t203, 1);
	restart_t200(l2, 1);
	test_and_clear_bit(FLG_PEND_REL, &l2->flag);
	freewin(l2);
	mISDN_FsmChangeState(fi, ST_L2_5);
}

static void
l2_mdl_error_ua(struct FsmInst *fi, int event, void *arg)
{
	struct sk_buff *skb = arg;
	struct layer2 *l2 = fi->userdata;

	if (get_PollFlagFree(l2, skb))
		l2mgr(l2, MDL_ERROR_IND, (void *) 'C');
	else
		l2mgr(l2, MDL_ERROR_IND, (void *) 'D');

}

static void
l2_mdl_error_dm(struct FsmInst *fi, int event, void *arg)
{
	struct sk_buff *skb = arg;
	struct layer2 *l2 = fi->userdata;

	if (get_PollFlagFree(l2, skb))
		l2mgr(l2, MDL_ERROR_IND, (void *) 'B');
	else {
		l2mgr(l2, MDL_ERROR_IND, (void *) 'E');
		establishlink(fi);
		test_and_clear_bit(FLG_L3_INIT, &l2->flag);
	}
}

static void
l2_st8_mdl_error_dm(struct FsmInst *fi, int event, void *arg)
{
	struct sk_buff *skb = arg;
	struct layer2 *l2 = fi->userdata;

	if (get_PollFlagFree(l2, skb))
		l2mgr(l2, MDL_ERROR_IND, (void *) 'B');
	else
		l2mgr(l2, MDL_ERROR_IND, (void *) 'E');
	establishlink(fi);
	test_and_clear_bit(FLG_L3_INIT, &l2->flag);
}

static void
l2_go_st3(struct FsmInst *fi, int event, void *arg)
{
	dev_kfree_skb((struct sk_buff *)arg);
	mISDN_FsmChangeState(fi, ST_L2_3);
}

static void
l2_mdl_assign(struct FsmInst *fi, int event, void *arg)
{
	struct layer2	*l2 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_L2_3);
	dev_kfree_skb((struct sk_buff *)arg);
	l2_tei(l2, MDL_ASSIGN_IND, 0);
}

static void
l2_queue_ui_assign(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	skb_queue_tail(&l2->ui_queue, skb);
	mISDN_FsmChangeState(fi, ST_L2_2);
	l2_tei(l2, MDL_ASSIGN_IND, 0);
}

static void
l2_queue_ui(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	skb_queue_tail(&l2->ui_queue, skb);
}

static void
tx_ui(struct layer2 *l2)
{
	struct sk_buff *skb;
	u_char header[MAX_L2HEADER_LEN];
	int i;

	i = sethdraddr(l2, header, CMD);
	if (test_bit(FLG_LAPD_NET, &l2->flag))
		header[1] = 0xff; /* tei 127 */
	header[i++] = UI;
	while ((skb = skb_dequeue(&l2->ui_queue))) {
		memcpy(skb_push(skb, i), header, i);
		enqueue_ui(l2, skb);
	}
}

static void
l2_send_ui(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	skb_queue_tail(&l2->ui_queue, skb);
	tx_ui(l2);
}

static void
l2_got_ui(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	skb_pull(skb, l2headersize(l2, 1));
/*
 *		in states 1-3 for broadcast
 */

	if (l2->tm)
		l2_tei(l2, MDL_STATUS_UI_IND, 0);
	l2up(l2, DL_UNITDATA_IND, skb);
}

static void
l2_establish(struct FsmInst *fi, int event, void *arg)
{
	struct sk_buff *skb = arg;
	struct layer2 *l2 = fi->userdata;

	establishlink(fi);
	test_and_set_bit(FLG_L3_INIT, &l2->flag);
	dev_kfree_skb(skb);
}

static void
l2_discard_i_setl3(struct FsmInst *fi, int event, void *arg)
{
	struct sk_buff *skb = arg;
	struct layer2 *l2 = fi->userdata;

	skb_queue_purge(&l2->i_queue);
	test_and_set_bit(FLG_L3_INIT, &l2->flag);
	test_and_clear_bit(FLG_PEND_REL, &l2->flag);
	dev_kfree_skb(skb);
}

static void
l2_l3_reestablish(struct FsmInst *fi, int event, void *arg)
{
	struct sk_buff *skb = arg;
	struct layer2 *l2 = fi->userdata;

	skb_queue_purge(&l2->i_queue);
	establishlink(fi);
	test_and_set_bit(FLG_L3_INIT, &l2->flag);
	dev_kfree_skb(skb);
}

static void
l2_release(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	skb_trim(skb, 0);
	l2up(l2, DL_RELEASE_CNF, skb);
}

static void
l2_pend_rel(struct FsmInst *fi, int event, void *arg)
{
	struct sk_buff *skb = arg;
	struct layer2 *l2 = fi->userdata;

	test_and_set_bit(FLG_PEND_REL, &l2->flag);
	dev_kfree_skb(skb);
}

static void
l2_disconnect(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	skb_queue_purge(&l2->i_queue);
	freewin(l2);
	mISDN_FsmChangeState(fi, ST_L2_6);
	l2->rc = 0;
	send_uframe(l2, NULL, DISC | 0x10, CMD);
	mISDN_FsmDelTimer(&l2->t203, 1);
	restart_t200(l2, 2);
	if (skb)
		dev_kfree_skb(skb);
}

static void
l2_start_multi(struct FsmInst *fi, int event, void *arg)
{
	struct layer2	*l2 = fi->userdata;
	struct sk_buff	*skb = arg;

	l2->vs = 0;
	l2->va = 0;
	l2->vr = 0;
	l2->sow = 0;
	clear_exception(l2);
	send_uframe(l2, NULL, UA | get_PollFlag(l2, skb), RSP);
	mISDN_FsmChangeState(fi, ST_L2_7);
	mISDN_FsmAddTimer(&l2->t203, l2->T203, EV_L2_T203, NULL, 3);
	skb_trim(skb, 0);
	l2up(l2, DL_ESTABLISH_IND, skb);
	if (l2->tm)
		l2_tei(l2, MDL_STATUS_UP_IND, 0);
}

static void
l2_send_UA(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	send_uframe(l2, skb, UA | get_PollFlag(l2, skb), RSP);
}

static void
l2_send_DM(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	send_uframe(l2, skb, DM | get_PollFlag(l2, skb), RSP);
}

static void
l2_restart_multi(struct FsmInst *fi, int event, void *arg)
{
	struct layer2	*l2 = fi->userdata;
	struct sk_buff	*skb = arg;
	int		est = 0;

	send_uframe(l2, skb, UA | get_PollFlag(l2, skb), RSP);

	l2mgr(l2, MDL_ERROR_IND, (void *) 'F');

	if (l2->vs != l2->va) {
		skb_queue_purge(&l2->i_queue);
		est = 1;
	}

	clear_exception(l2);
	l2->vs = 0;
	l2->va = 0;
	l2->vr = 0;
	l2->sow = 0;
	mISDN_FsmChangeState(fi, ST_L2_7);
	stop_t200(l2, 3);
	mISDN_FsmRestartTimer(&l2->t203, l2->T203, EV_L2_T203, NULL, 3);

	if (est)
		l2up_create(l2, DL_ESTABLISH_IND, 0, NULL);
/*		mISDN_queue_data(&l2->inst, l2->inst.id | MSG_BROADCAST,
 *		    MGR_SHORTSTATUS | INDICATION, SSTATUS_L2_ESTABLISHED,
 *		    0, NULL, 0);
 */
	if (skb_queue_len(&l2->i_queue) && cansend(l2))
		mISDN_FsmEvent(fi, EV_L2_ACK_PULL, NULL);
}

static void
l2_stop_multi(struct FsmInst *fi, int event, void *arg)
{
	struct layer2	*l2 = fi->userdata;
	struct sk_buff	*skb = arg;

	mISDN_FsmChangeState(fi, ST_L2_4);
	mISDN_FsmDelTimer(&l2->t203, 3);
	stop_t200(l2, 4);

	send_uframe(l2, skb, UA | get_PollFlag(l2, skb), RSP);
	skb_queue_purge(&l2->i_queue);
	freewin(l2);
	lapb_dl_release_l2l3(l2, DL_RELEASE_IND);
	if (l2->tm)
		l2_tei(l2, MDL_STATUS_DOWN_IND, 0);
}

static void
l2_connected(struct FsmInst *fi, int event, void *arg)
{
	struct layer2	*l2 = fi->userdata;
	struct sk_buff	*skb = arg;
	int pr = -1;

	if (!get_PollFlag(l2, skb)) {
		l2_mdl_error_ua(fi, event, arg);
		return;
	}
	dev_kfree_skb(skb);
	if (test_and_clear_bit(FLG_PEND_REL, &l2->flag))
		l2_disconnect(fi, event, NULL);
	if (test_and_clear_bit(FLG_L3_INIT, &l2->flag)) {
		pr = DL_ESTABLISH_CNF;
	} else if (l2->vs != l2->va) {
		skb_queue_purge(&l2->i_queue);
		pr = DL_ESTABLISH_IND;
	}
	stop_t200(l2, 5);
	l2->vr = 0;
	l2->vs = 0;
	l2->va = 0;
	l2->sow = 0;
	mISDN_FsmChangeState(fi, ST_L2_7);
	mISDN_FsmAddTimer(&l2->t203, l2->T203, EV_L2_T203, NULL, 4);
	if (pr != -1)
		l2up_create(l2, pr, 0, NULL);

	if (skb_queue_len(&l2->i_queue) && cansend(l2))
		mISDN_FsmEvent(fi, EV_L2_ACK_PULL, NULL);

	if (l2->tm)
		l2_tei(l2, MDL_STATUS_UP_IND, 0);
}

static void
l2_released(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	if (!get_PollFlag(l2, skb)) {
		l2_mdl_error_ua(fi, event, arg);
		return;
	}
	dev_kfree_skb(skb);
	stop_t200(l2, 6);
	lapb_dl_release_l2l3(l2, DL_RELEASE_CNF);
	mISDN_FsmChangeState(fi, ST_L2_4);
	if (l2->tm)
		l2_tei(l2, MDL_STATUS_DOWN_IND, 0);
}

static void
l2_reestablish(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	if (!get_PollFlagFree(l2, skb)) {
		establishlink(fi);
		test_and_set_bit(FLG_L3_INIT, &l2->flag);
	}
}

static void
l2_st5_dm_release(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	if (get_PollFlagFree(l2, skb)) {
		stop_t200(l2, 7);
		if (!test_bit(FLG_L3_INIT, &l2->flag))
			skb_queue_purge(&l2->i_queue);
		if (test_bit(FLG_LAPB, &l2->flag))
			l2down_create(l2, PH_DEACTIVATE_REQ,
				l2_newid(l2), 0, NULL);
		st5_dl_release_l2l3(l2);
		mISDN_FsmChangeState(fi, ST_L2_4);
		if (l2->tm)
			l2_tei(l2, MDL_STATUS_DOWN_IND, 0);
	}
}

static void
l2_st6_dm_release(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	if (get_PollFlagFree(l2, skb)) {
		stop_t200(l2, 8);
		lapb_dl_release_l2l3(l2, DL_RELEASE_CNF);
		mISDN_FsmChangeState(fi, ST_L2_4);
		if (l2->tm)
			l2_tei(l2, MDL_STATUS_DOWN_IND, 0);
	}
}

static void
enquiry_cr(struct layer2 *l2, u_char typ, u_char cr, u_char pf)
{
	struct sk_buff *skb;
	u_char tmp[MAX_L2HEADER_LEN];
	int i;

	i = sethdraddr(l2, tmp, cr);
	if (test_bit(FLG_MOD128, &l2->flag)) {
		tmp[i++] = typ;
		tmp[i++] = (l2->vr << 1) | (pf ? 1 : 0);
	} else
		tmp[i++] = (l2->vr << 5) | typ | (pf ? 0x10 : 0);
	skb = mI_alloc_skb(i, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_WARNING
		    "isdnl2 can't alloc sbbuff for enquiry_cr\n");
		return;
	}
	memcpy(skb_put(skb, i), tmp, i);
	enqueue_super(l2, skb);
}

inline void
enquiry_response(struct layer2 *l2)
{
	if (test_bit(FLG_OWN_BUSY, &l2->flag))
		enquiry_cr(l2, RNR, RSP, 1);
	else
		enquiry_cr(l2, RR, RSP, 1);
	test_and_clear_bit(FLG_ACK_PEND, &l2->flag);
}

inline void
transmit_enquiry(struct layer2 *l2)
{
	if (test_bit(FLG_OWN_BUSY, &l2->flag))
		enquiry_cr(l2, RNR, CMD, 1);
	else
		enquiry_cr(l2, RR, CMD, 1);
	test_and_clear_bit(FLG_ACK_PEND, &l2->flag);
	start_t200(l2, 9);
}


static void
nrerrorrecovery(struct FsmInst *fi)
{
	struct layer2 *l2 = fi->userdata;

	l2mgr(l2, MDL_ERROR_IND, (void *) 'J');
	establishlink(fi);
	test_and_clear_bit(FLG_L3_INIT, &l2->flag);
}

static void
invoke_retransmission(struct layer2 *l2, unsigned int nr)
{
	u_int	p1;

	if (l2->vs != nr) {
		while (l2->vs != nr) {
			(l2->vs)--;
			if (test_bit(FLG_MOD128, &l2->flag)) {
				l2->vs %= 128;
				p1 = (l2->vs - l2->va) % 128;
			} else {
				l2->vs %= 8;
				p1 = (l2->vs - l2->va) % 8;
			}
			p1 = (p1 + l2->sow) % l2->window;
			if (l2->windowar[p1])
				skb_queue_head(&l2->i_queue, l2->windowar[p1]);
			else
				printk(KERN_WARNING
				    "%s: windowar[%d] is NULL\n",
				    __func__, p1);
			l2->windowar[p1] = NULL;
		}
		mISDN_FsmEvent(&l2->l2m, EV_L2_ACK_PULL, NULL);
	}
}

static void
l2_st7_got_super(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;
	int PollFlag, rsp, typ = RR;
	unsigned int nr;

	rsp = *skb->data & 0x2;
	if (test_bit(FLG_ORIG, &l2->flag))
		rsp = !rsp;

	skb_pull(skb, l2addrsize(l2));
	if (IsRNR(skb->data, l2)) {
		set_peer_busy(l2);
		typ = RNR;
	} else
		clear_peer_busy(l2);
	if (IsREJ(skb->data, l2))
		typ = REJ;

	if (test_bit(FLG_MOD128, &l2->flag)) {
		PollFlag = (skb->data[1] & 0x1) == 0x1;
		nr = skb->data[1] >> 1;
	} else {
		PollFlag = (skb->data[0] & 0x10);
		nr = (skb->data[0] >> 5) & 0x7;
	}
	dev_kfree_skb(skb);

	if (PollFlag) {
		if (rsp)
			l2mgr(l2, MDL_ERROR_IND, (void *) 'A');
		else
			enquiry_response(l2);
	}
	if (legalnr(l2, nr)) {
		if (typ == REJ) {
			setva(l2, nr);
			invoke_retransmission(l2, nr);
			stop_t200(l2, 10);
			if (mISDN_FsmAddTimer(&l2->t203, l2->T203,
					EV_L2_T203, NULL, 6))
				l2m_debug(&l2->l2m, "Restart T203 ST7 REJ");
		} else if ((nr == l2->vs) && (typ == RR)) {
			setva(l2, nr);
			stop_t200(l2, 11);
			mISDN_FsmRestartTimer(&l2->t203, l2->T203,
					EV_L2_T203, NULL, 7);
		} else if ((l2->va != nr) || (typ == RNR)) {
			setva(l2, nr);
			if (typ != RR)
				mISDN_FsmDelTimer(&l2->t203, 9);
			restart_t200(l2, 12);
		}
		if (skb_queue_len(&l2->i_queue) && (typ == RR))
			mISDN_FsmEvent(fi, EV_L2_ACK_PULL, NULL);
	} else
		nrerrorrecovery(fi);
}

static void
l2_feed_i_if_reest(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	if (!test_bit(FLG_L3_INIT, &l2->flag))
		skb_queue_tail(&l2->i_queue, skb);
	else
		dev_kfree_skb(skb);
}

static void
l2_feed_i_pull(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	skb_queue_tail(&l2->i_queue, skb);
	mISDN_FsmEvent(fi, EV_L2_ACK_PULL, NULL);
}

static void
l2_feed_iqueue(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	skb_queue_tail(&l2->i_queue, skb);
}

static void
l2_got_iframe(struct FsmInst *fi, int event, void *arg)
{
	struct layer2	*l2 = fi->userdata;
	struct sk_buff	*skb = arg;
	int		PollFlag, i;
	u_int		ns, nr;

	i = l2addrsize(l2);
	if (test_bit(FLG_MOD128, &l2->flag)) {
		PollFlag = ((skb->data[i + 1] & 0x1) == 0x1);
		ns = skb->data[i] >> 1;
		nr = (skb->data[i + 1] >> 1) & 0x7f;
	} else {
		PollFlag = (skb->data[i] & 0x10);
		ns = (skb->data[i] >> 1) & 0x7;
		nr = (skb->data[i] >> 5) & 0x7;
	}
	if (test_bit(FLG_OWN_BUSY, &l2->flag)) {
		dev_kfree_skb(skb);
		if (PollFlag)
			enquiry_response(l2);
	} else {
		if (l2->vr == ns) {
			l2->vr++;
			if (test_bit(FLG_MOD128, &l2->flag))
				l2->vr %= 128;
			else
				l2->vr %= 8;
			test_and_clear_bit(FLG_REJEXC, &l2->flag);
			if (PollFlag)
				enquiry_response(l2);
			else
				test_and_set_bit(FLG_ACK_PEND, &l2->flag);
			skb_pull(skb, l2headersize(l2, 0));
			l2up(l2, DL_DATA_IND, skb);
		} else {
			/* n(s)!=v(r) */
			dev_kfree_skb(skb);
			if (test_and_set_bit(FLG_REJEXC, &l2->flag)) {
				if (PollFlag)
					enquiry_response(l2);
			} else {
				enquiry_cr(l2, REJ, RSP, PollFlag);
				test_and_clear_bit(FLG_ACK_PEND, &l2->flag);
			}
		}
	}
	if (legalnr(l2, nr)) {
		if (!test_bit(FLG_PEER_BUSY, &l2->flag) &&
		    (fi->state == ST_L2_7)) {
			if (nr == l2->vs) {
				stop_t200(l2, 13);
				mISDN_FsmRestartTimer(&l2->t203, l2->T203,
						EV_L2_T203, NULL, 7);
			} else if (nr != l2->va)
				restart_t200(l2, 14);
		}
		setva(l2, nr);
	} else {
		nrerrorrecovery(fi);
		return;
	}
	if (skb_queue_len(&l2->i_queue) && (fi->state == ST_L2_7))
		mISDN_FsmEvent(fi, EV_L2_ACK_PULL, NULL);
	if (test_and_clear_bit(FLG_ACK_PEND, &l2->flag))
		enquiry_cr(l2, RR, RSP, 0);
}

static void
l2_got_tei(struct FsmInst *fi, int event, void *arg)
{
	struct layer2	*l2 = fi->userdata;
	u_int		info;

	l2->tei = (signed char)(long)arg;
	set_channel_address(&l2->ch, l2->sapi, l2->tei);
	info = DL_INFO_L2_CONNECT;
	l2up_create(l2, DL_INFORMATION_IND, sizeof(info), &info);
	if (fi->state == ST_L2_3) {
		establishlink(fi);
		test_and_set_bit(FLG_L3_INIT, &l2->flag);
	} else
		mISDN_FsmChangeState(fi, ST_L2_4);
	if (skb_queue_len(&l2->ui_queue))
		tx_ui(l2);
}

static void
l2_st5_tout_200(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;

	if (test_bit(FLG_LAPD, &l2->flag) &&
		test_bit(FLG_DCHAN_BUSY, &l2->flag)) {
		mISDN_FsmAddTimer(&l2->t200, l2->T200, EV_L2_T200, NULL, 9);
	} else if (l2->rc == l2->N200) {
		mISDN_FsmChangeState(fi, ST_L2_4);
		test_and_clear_bit(FLG_T200_RUN, &l2->flag);
		skb_queue_purge(&l2->i_queue);
		l2mgr(l2, MDL_ERROR_IND, (void *) 'G');
		if (test_bit(FLG_LAPB, &l2->flag))
			l2down_create(l2, PH_DEACTIVATE_REQ,
				l2_newid(l2), 0, NULL);
		st5_dl_release_l2l3(l2);
		if (l2->tm)
			l2_tei(l2, MDL_STATUS_DOWN_IND, 0);
	} else {
		l2->rc++;
		mISDN_FsmAddTimer(&l2->t200, l2->T200, EV_L2_T200, NULL, 9);
		send_uframe(l2, NULL, (test_bit(FLG_MOD128, &l2->flag) ?
			SABME : SABM) | 0x10, CMD);
	}
}

static void
l2_st6_tout_200(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;

	if (test_bit(FLG_LAPD, &l2->flag) &&
		test_bit(FLG_DCHAN_BUSY, &l2->flag)) {
		mISDN_FsmAddTimer(&l2->t200, l2->T200, EV_L2_T200, NULL, 9);
	} else if (l2->rc == l2->N200) {
		mISDN_FsmChangeState(fi, ST_L2_4);
		test_and_clear_bit(FLG_T200_RUN, &l2->flag);
		l2mgr(l2, MDL_ERROR_IND, (void *) 'H');
		lapb_dl_release_l2l3(l2, DL_RELEASE_CNF);
		if (l2->tm)
			l2_tei(l2, MDL_STATUS_DOWN_IND, 0);
	} else {
		l2->rc++;
		mISDN_FsmAddTimer(&l2->t200, l2->T200, EV_L2_T200,
			    NULL, 9);
		send_uframe(l2, NULL, DISC | 0x10, CMD);
	}
}

static void
l2_st7_tout_200(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;

	if (test_bit(FLG_LAPD, &l2->flag) &&
		test_bit(FLG_DCHAN_BUSY, &l2->flag)) {
		mISDN_FsmAddTimer(&l2->t200, l2->T200, EV_L2_T200, NULL, 9);
		return;
	}
	test_and_clear_bit(FLG_T200_RUN, &l2->flag);
	l2->rc = 0;
	mISDN_FsmChangeState(fi, ST_L2_8);
	transmit_enquiry(l2);
	l2->rc++;
}

static void
l2_st8_tout_200(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;

	if (test_bit(FLG_LAPD, &l2->flag) &&
		test_bit(FLG_DCHAN_BUSY, &l2->flag)) {
		mISDN_FsmAddTimer(&l2->t200, l2->T200, EV_L2_T200, NULL, 9);
		return;
	}
	test_and_clear_bit(FLG_T200_RUN, &l2->flag);
	if (l2->rc == l2->N200) {
		l2mgr(l2, MDL_ERROR_IND, (void *) 'I');
		establishlink(fi);
		test_and_clear_bit(FLG_L3_INIT, &l2->flag);
	} else {
		transmit_enquiry(l2);
		l2->rc++;
	}
}

static void
l2_st7_tout_203(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;

	if (test_bit(FLG_LAPD, &l2->flag) &&
		test_bit(FLG_DCHAN_BUSY, &l2->flag)) {
		mISDN_FsmAddTimer(&l2->t203, l2->T203, EV_L2_T203, NULL, 9);
		return;
	}
	mISDN_FsmChangeState(fi, ST_L2_8);
	transmit_enquiry(l2);
	l2->rc = 0;
}

static void
l2_pull_iqueue(struct FsmInst *fi, int event, void *arg)
{
	struct layer2	*l2 = fi->userdata;
	struct sk_buff	*skb, *nskb, *oskb;
	u_char		header[MAX_L2HEADER_LEN];
	u_int		i, p1;

	if (!cansend(l2))
		return;

	skb = skb_dequeue(&l2->i_queue);
	if (!skb)
		return;

	if (test_bit(FLG_MOD128, &l2->flag))
		p1 = (l2->vs - l2->va) % 128;
	else
		p1 = (l2->vs - l2->va) % 8;
	p1 = (p1 + l2->sow) % l2->window;
	if (l2->windowar[p1]) {
		printk(KERN_WARNING "isdnl2 try overwrite ack queue entry %d\n",
		    p1);
		dev_kfree_skb(l2->windowar[p1]);
	}
	l2->windowar[p1] = skb;
	i = sethdraddr(l2, header, CMD);
	if (test_bit(FLG_MOD128, &l2->flag)) {
		header[i++] = l2->vs << 1;
		header[i++] = l2->vr << 1;
		l2->vs = (l2->vs + 1) % 128;
	} else {
		header[i++] = (l2->vr << 5) | (l2->vs << 1);
		l2->vs = (l2->vs + 1) % 8;
	}

	nskb = skb_clone(skb, GFP_ATOMIC);
	p1 = skb_headroom(nskb);
	if (p1 >= i)
		memcpy(skb_push(nskb, i), header, i);
	else {
		printk(KERN_WARNING
		    "isdnl2 pull_iqueue skb header(%d/%d) too short\n", i, p1);
		oskb = nskb;
		nskb = mI_alloc_skb(oskb->len + i, GFP_ATOMIC);
		if (!nskb) {
			dev_kfree_skb(oskb);
			printk(KERN_WARNING "%s: no skb mem\n", __func__);
			return;
		}
		memcpy(skb_put(nskb, i), header, i);
		memcpy(skb_put(nskb, oskb->len), oskb->data, oskb->len);
		dev_kfree_skb(oskb);
	}
	l2down(l2, PH_DATA_REQ, l2_newid(l2), nskb);
	test_and_clear_bit(FLG_ACK_PEND, &l2->flag);
	if (!test_and_set_bit(FLG_T200_RUN, &l2->flag)) {
		mISDN_FsmDelTimer(&l2->t203, 13);
		mISDN_FsmAddTimer(&l2->t200, l2->T200, EV_L2_T200, NULL, 11);
	}
}

static void
l2_st8_got_super(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;
	int PollFlag, rsp, rnr = 0;
	unsigned int nr;

	rsp = *skb->data & 0x2;
	if (test_bit(FLG_ORIG, &l2->flag))
		rsp = !rsp;

	skb_pull(skb, l2addrsize(l2));

	if (IsRNR(skb->data, l2)) {
		set_peer_busy(l2);
		rnr = 1;
	} else
		clear_peer_busy(l2);

	if (test_bit(FLG_MOD128, &l2->flag)) {
		PollFlag = (skb->data[1] & 0x1) == 0x1;
		nr = skb->data[1] >> 1;
	} else {
		PollFlag = (skb->data[0] & 0x10);
		nr = (skb->data[0] >> 5) & 0x7;
	}
	dev_kfree_skb(skb);
	if (rsp && PollFlag) {
		if (legalnr(l2, nr)) {
			if (rnr) {
				restart_t200(l2, 15);
			} else {
				stop_t200(l2, 16);
				mISDN_FsmAddTimer(&l2->t203, l2->T203,
					    EV_L2_T203, NULL, 5);
				setva(l2, nr);
			}
			invoke_retransmission(l2, nr);
			mISDN_FsmChangeState(fi, ST_L2_7);
			if (skb_queue_len(&l2->i_queue) && cansend(l2))
				mISDN_FsmEvent(fi, EV_L2_ACK_PULL, NULL);
		} else
			nrerrorrecovery(fi);
	} else {
		if (!rsp && PollFlag)
			enquiry_response(l2);
		if (legalnr(l2, nr))
			setva(l2, nr);
		else
			nrerrorrecovery(fi);
	}
}

static void
l2_got_FRMR(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	skb_pull(skb, l2addrsize(l2) + 1);

	if (!(skb->data[0] & 1) || ((skb->data[0] & 3) == 1) || /* I or S */
	    (IsUA(skb->data) && (fi->state == ST_L2_7))) {
		l2mgr(l2, MDL_ERROR_IND, (void *) 'K');
		establishlink(fi);
		test_and_clear_bit(FLG_L3_INIT, &l2->flag);
	}
	dev_kfree_skb(skb);
}

static void
l2_st24_tei_remove(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;

	skb_queue_purge(&l2->ui_queue);
	l2->tei = GROUP_TEI;
	mISDN_FsmChangeState(fi, ST_L2_1);
}

static void
l2_st3_tei_remove(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;

	skb_queue_purge(&l2->ui_queue);
	l2->tei = GROUP_TEI;
	l2up_create(l2, DL_RELEASE_IND, 0, NULL);
	mISDN_FsmChangeState(fi, ST_L2_1);
}

static void
l2_st5_tei_remove(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;

	skb_queue_purge(&l2->i_queue);
	skb_queue_purge(&l2->ui_queue);
	freewin(l2);
	l2->tei = GROUP_TEI;
	stop_t200(l2, 17);
	st5_dl_release_l2l3(l2);
	mISDN_FsmChangeState(fi, ST_L2_1);
}

static void
l2_st6_tei_remove(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;

	skb_queue_purge(&l2->ui_queue);
	l2->tei = GROUP_TEI;
	stop_t200(l2, 18);
	l2up_create(l2, DL_RELEASE_IND, 0, NULL);
	mISDN_FsmChangeState(fi, ST_L2_1);
}

static void
l2_tei_remove(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;

	skb_queue_purge(&l2->i_queue);
	skb_queue_purge(&l2->ui_queue);
	freewin(l2);
	l2->tei = GROUP_TEI;
	stop_t200(l2, 17);
	mISDN_FsmDelTimer(&l2->t203, 19);
	l2up_create(l2, DL_RELEASE_IND, 0, NULL);
/*	mISDN_queue_data(&l2->inst, l2->inst.id | MSG_BROADCAST,
 *		MGR_SHORTSTATUS_IND, SSTATUS_L2_RELEASED,
 *		0, NULL, 0);
 */
	mISDN_FsmChangeState(fi, ST_L2_1);
}

static void
l2_st14_persistent_da(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	skb_queue_purge(&l2->i_queue);
	skb_queue_purge(&l2->ui_queue);
	if (test_and_clear_bit(FLG_ESTAB_PEND, &l2->flag))
		l2up(l2, DL_RELEASE_IND, skb);
	else
		dev_kfree_skb(skb);
}

static void
l2_st5_persistent_da(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	skb_queue_purge(&l2->i_queue);
	skb_queue_purge(&l2->ui_queue);
	freewin(l2);
	stop_t200(l2, 19);
	st5_dl_release_l2l3(l2);
	mISDN_FsmChangeState(fi, ST_L2_4);
	if (l2->tm)
		l2_tei(l2, MDL_STATUS_DOWN_IND, 0);
	dev_kfree_skb(skb);
}

static void
l2_st6_persistent_da(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	skb_queue_purge(&l2->ui_queue);
	stop_t200(l2, 20);
	l2up(l2, DL_RELEASE_CNF, skb);
	mISDN_FsmChangeState(fi, ST_L2_4);
	if (l2->tm)
		l2_tei(l2, MDL_STATUS_DOWN_IND, 0);
}

static void
l2_persistent_da(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	skb_queue_purge(&l2->i_queue);
	skb_queue_purge(&l2->ui_queue);
	freewin(l2);
	stop_t200(l2, 19);
	mISDN_FsmDelTimer(&l2->t203, 19);
	l2up(l2, DL_RELEASE_IND, skb);
	mISDN_FsmChangeState(fi, ST_L2_4);
	if (l2->tm)
		l2_tei(l2, MDL_STATUS_DOWN_IND, 0);
}

static void
l2_set_own_busy(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	if (!test_and_set_bit(FLG_OWN_BUSY, &l2->flag)) {
		enquiry_cr(l2, RNR, RSP, 0);
		test_and_clear_bit(FLG_ACK_PEND, &l2->flag);
	}
	if (skb)
		dev_kfree_skb(skb);
}

static void
l2_clear_own_busy(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;
	struct sk_buff *skb = arg;

	if (!test_and_clear_bit(FLG_OWN_BUSY, &l2->flag)) {
		enquiry_cr(l2, RR, RSP, 0);
		test_and_clear_bit(FLG_ACK_PEND, &l2->flag);
	}
	if (skb)
		dev_kfree_skb(skb);
}

static void
l2_frame_error(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;

	l2mgr(l2, MDL_ERROR_IND, arg);
}

static void
l2_frame_error_reest(struct FsmInst *fi, int event, void *arg)
{
	struct layer2 *l2 = fi->userdata;

	l2mgr(l2, MDL_ERROR_IND, arg);
	establishlink(fi);
	test_and_clear_bit(FLG_L3_INIT, &l2->flag);
}

static struct FsmNode L2FnList[] =
{
	{ST_L2_1, EV_L2_DL_ESTABLISH_REQ, l2_mdl_assign},
	{ST_L2_2, EV_L2_DL_ESTABLISH_REQ, l2_go_st3},
	{ST_L2_4, EV_L2_DL_ESTABLISH_REQ, l2_establish},
	{ST_L2_5, EV_L2_DL_ESTABLISH_REQ, l2_discard_i_setl3},
	{ST_L2_7, EV_L2_DL_ESTABLISH_REQ, l2_l3_reestablish},
	{ST_L2_8, EV_L2_DL_ESTABLISH_REQ, l2_l3_reestablish},
	{ST_L2_4, EV_L2_DL_RELEASE_REQ, l2_release},
	{ST_L2_5, EV_L2_DL_RELEASE_REQ, l2_pend_rel},
	{ST_L2_7, EV_L2_DL_RELEASE_REQ, l2_disconnect},
	{ST_L2_8, EV_L2_DL_RELEASE_REQ, l2_disconnect},
	{ST_L2_5, EV_L2_DL_DATA, l2_feed_i_if_reest},
	{ST_L2_7, EV_L2_DL_DATA, l2_feed_i_pull},
	{ST_L2_8, EV_L2_DL_DATA, l2_feed_iqueue},
	{ST_L2_1, EV_L2_DL_UNITDATA, l2_queue_ui_assign},
	{ST_L2_2, EV_L2_DL_UNITDATA, l2_queue_ui},
	{ST_L2_3, EV_L2_DL_UNITDATA, l2_queue_ui},
	{ST_L2_4, EV_L2_DL_UNITDATA, l2_send_ui},
	{ST_L2_5, EV_L2_DL_UNITDATA, l2_send_ui},
	{ST_L2_6, EV_L2_DL_UNITDATA, l2_send_ui},
	{ST_L2_7, EV_L2_DL_UNITDATA, l2_send_ui},
	{ST_L2_8, EV_L2_DL_UNITDATA, l2_send_ui},
	{ST_L2_1, EV_L2_MDL_ASSIGN, l2_got_tei},
	{ST_L2_2, EV_L2_MDL_ASSIGN, l2_got_tei},
	{ST_L2_3, EV_L2_MDL_ASSIGN, l2_got_tei},
	{ST_L2_2, EV_L2_MDL_ERROR, l2_st24_tei_remove},
	{ST_L2_3, EV_L2_MDL_ERROR, l2_st3_tei_remove},
	{ST_L2_4, EV_L2_MDL_REMOVE, l2_st24_tei_remove},
	{ST_L2_5, EV_L2_MDL_REMOVE, l2_st5_tei_remove},
	{ST_L2_6, EV_L2_MDL_REMOVE, l2_st6_tei_remove},
	{ST_L2_7, EV_L2_MDL_REMOVE, l2_tei_remove},
	{ST_L2_8, EV_L2_MDL_REMOVE, l2_tei_remove},
	{ST_L2_4, EV_L2_SABME, l2_start_multi},
	{ST_L2_5, EV_L2_SABME, l2_send_UA},
	{ST_L2_6, EV_L2_SABME, l2_send_DM},
	{ST_L2_7, EV_L2_SABME, l2_restart_multi},
	{ST_L2_8, EV_L2_SABME, l2_restart_multi},
	{ST_L2_4, EV_L2_DISC, l2_send_DM},
	{ST_L2_5, EV_L2_DISC, l2_send_DM},
	{ST_L2_6, EV_L2_DISC, l2_send_UA},
	{ST_L2_7, EV_L2_DISC, l2_stop_multi},
	{ST_L2_8, EV_L2_DISC, l2_stop_multi},
	{ST_L2_4, EV_L2_UA, l2_mdl_error_ua},
	{ST_L2_5, EV_L2_UA, l2_connected},
	{ST_L2_6, EV_L2_UA, l2_released},
	{ST_L2_7, EV_L2_UA, l2_mdl_error_ua},
	{ST_L2_8, EV_L2_UA, l2_mdl_error_ua},
	{ST_L2_4, EV_L2_DM, l2_reestablish},
	{ST_L2_5, EV_L2_DM, l2_st5_dm_release},
	{ST_L2_6, EV_L2_DM, l2_st6_dm_release},
	{ST_L2_7, EV_L2_DM, l2_mdl_error_dm},
	{ST_L2_8, EV_L2_DM, l2_st8_mdl_error_dm},
	{ST_L2_1, EV_L2_UI, l2_got_ui},
	{ST_L2_2, EV_L2_UI, l2_got_ui},
	{ST_L2_3, EV_L2_UI, l2_got_ui},
	{ST_L2_4, EV_L2_UI, l2_got_ui},
	{ST_L2_5, EV_L2_UI, l2_got_ui},
	{ST_L2_6, EV_L2_UI, l2_got_ui},
	{ST_L2_7, EV_L2_UI, l2_got_ui},
	{ST_L2_8, EV_L2_UI, l2_got_ui},
	{ST_L2_7, EV_L2_FRMR, l2_got_FRMR},
	{ST_L2_8, EV_L2_FRMR, l2_got_FRMR},
	{ST_L2_7, EV_L2_SUPER, l2_st7_got_super},
	{ST_L2_8, EV_L2_SUPER, l2_st8_got_super},
	{ST_L2_7, EV_L2_I, l2_got_iframe},
	{ST_L2_8, EV_L2_I, l2_got_iframe},
	{ST_L2_5, EV_L2_T200, l2_st5_tout_200},
	{ST_L2_6, EV_L2_T200, l2_st6_tout_200},
	{ST_L2_7, EV_L2_T200, l2_st7_tout_200},
	{ST_L2_8, EV_L2_T200, l2_st8_tout_200},
	{ST_L2_7, EV_L2_T203, l2_st7_tout_203},
	{ST_L2_7, EV_L2_ACK_PULL, l2_pull_iqueue},
	{ST_L2_7, EV_L2_SET_OWN_BUSY, l2_set_own_busy},
	{ST_L2_8, EV_L2_SET_OWN_BUSY, l2_set_own_busy},
	{ST_L2_7, EV_L2_CLEAR_OWN_BUSY, l2_clear_own_busy},
	{ST_L2_8, EV_L2_CLEAR_OWN_BUSY, l2_clear_own_busy},
	{ST_L2_4, EV_L2_FRAME_ERROR, l2_frame_error},
	{ST_L2_5, EV_L2_FRAME_ERROR, l2_frame_error},
	{ST_L2_6, EV_L2_FRAME_ERROR, l2_frame_error},
	{ST_L2_7, EV_L2_FRAME_ERROR, l2_frame_error_reest},
	{ST_L2_8, EV_L2_FRAME_ERROR, l2_frame_error_reest},
	{ST_L2_1, EV_L1_DEACTIVATE, l2_st14_persistent_da},
	{ST_L2_2, EV_L1_DEACTIVATE, l2_st24_tei_remove},
	{ST_L2_3, EV_L1_DEACTIVATE, l2_st3_tei_remove},
	{ST_L2_4, EV_L1_DEACTIVATE, l2_st14_persistent_da},
	{ST_L2_5, EV_L1_DEACTIVATE, l2_st5_persistent_da},
	{ST_L2_6, EV_L1_DEACTIVATE, l2_st6_persistent_da},
	{ST_L2_7, EV_L1_DEACTIVATE, l2_persistent_da},
	{ST_L2_8, EV_L1_DEACTIVATE, l2_persistent_da},
};

static int
ph_data_indication(struct layer2 *l2, struct mISDNhead *hh, struct sk_buff *skb)
{
	u_char	*datap = skb->data;
	int	ret = -EINVAL;
	int	psapi, ptei;
	u_int	l;
	int	c = 0;

	l = l2addrsize(l2);
	if (skb->len <= l) {
		mISDN_FsmEvent(&l2->l2m, EV_L2_FRAME_ERROR, (void *) 'N');
		return ret;
	}
	if (test_bit(FLG_LAPD, &l2->flag)) { /* Maybe not needed */
		psapi = *datap++;
		ptei = *datap++;
		if ((psapi & 1) || !(ptei & 1)) {
			printk(KERN_WARNING
			    "l2 D-channel frame wrong EA0/EA1\n");
			return ret;
		}
		psapi >>= 2;
		ptei >>= 1;
		if (psapi != l2->sapi) {
			/* not our business */
			if (*debug & DEBUG_L2)
				printk(KERN_DEBUG "%s: sapi %d/%d mismatch\n",
					__func__, psapi, l2->sapi);
			dev_kfree_skb(skb);
			return 0;
		}
		if ((ptei != l2->tei) && (ptei != GROUP_TEI)) {
			/* not our business */
			if (*debug & DEBUG_L2)
				printk(KERN_DEBUG "%s: tei %d/%d mismatch\n",
					__func__, ptei, l2->tei);
			dev_kfree_skb(skb);
			return 0;
		}
	} else
		datap += l;
	if (!(*datap & 1)) {	/* I-Frame */
		c = iframe_error(l2, skb);
		if (!c)
			ret = mISDN_FsmEvent(&l2->l2m, EV_L2_I, skb);
	} else if (IsSFrame(datap, l2)) {	/* S-Frame */
		c = super_error(l2, skb);
		if (!c)
			ret = mISDN_FsmEvent(&l2->l2m, EV_L2_SUPER, skb);
	} else if (IsUI(datap)) {
		c = UI_error(l2, skb);
		if (!c)
			ret = mISDN_FsmEvent(&l2->l2m, EV_L2_UI, skb);
	} else if (IsSABME(datap, l2)) {
		c = unnum_error(l2, skb, CMD);
		if (!c)
			ret = mISDN_FsmEvent(&l2->l2m, EV_L2_SABME, skb);
	} else if (IsUA(datap)) {
		c = unnum_error(l2, skb, RSP);
		if (!c)
			ret = mISDN_FsmEvent(&l2->l2m, EV_L2_UA, skb);
	} else if (IsDISC(datap)) {
		c = unnum_error(l2, skb, CMD);
		if (!c)
			ret = mISDN_FsmEvent(&l2->l2m, EV_L2_DISC, skb);
	} else if (IsDM(datap)) {
		c = unnum_error(l2, skb, RSP);
		if (!c)
			ret = mISDN_FsmEvent(&l2->l2m, EV_L2_DM, skb);
	} else if (IsFRMR(datap)) {
		c = FRMR_error(l2, skb);
		if (!c)
			ret = mISDN_FsmEvent(&l2->l2m, EV_L2_FRMR, skb);
	} else
		c = 'L';
	if (c) {
		printk(KERN_WARNING "l2 D-channel frame error %c\n", c);
		mISDN_FsmEvent(&l2->l2m, EV_L2_FRAME_ERROR, (void *)(long)c);
	}
	return ret;
}

static int
l2_send(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct layer2		*l2 = container_of(ch, struct layer2, ch);
	struct mISDNhead	*hh =  mISDN_HEAD_P(skb);
	int 			ret = -EINVAL;

	if (*debug & DEBUG_L2_RECV)
		printk(KERN_DEBUG "%s: prim(%x) id(%x) sapi(%d) tei(%d)\n",
		    __func__, hh->prim, hh->id, l2->sapi, l2->tei);
	switch (hh->prim) {
	case PH_DATA_IND:
		ret = ph_data_indication(l2, hh, skb);
		break;
	case PH_DATA_CNF:
		ret = ph_data_confirm(l2, hh, skb);
		break;
	case PH_ACTIVATE_IND:
		test_and_set_bit(FLG_L1_ACTIV, &l2->flag);
		l2up_create(l2, MPH_ACTIVATE_IND, 0, NULL);
		if (test_and_clear_bit(FLG_ESTAB_PEND, &l2->flag))
			ret = mISDN_FsmEvent(&l2->l2m,
				EV_L2_DL_ESTABLISH_REQ, skb);
		break;
	case PH_DEACTIVATE_IND:
		test_and_clear_bit(FLG_L1_ACTIV, &l2->flag);
		l2up_create(l2, MPH_DEACTIVATE_IND, 0, NULL);
		ret = mISDN_FsmEvent(&l2->l2m, EV_L1_DEACTIVATE, skb);
		break;
	case MPH_INFORMATION_IND:
		if (!l2->up)
			break;
		ret = l2->up->send(l2->up, skb);
		break;
	case DL_DATA_REQ:
		ret = mISDN_FsmEvent(&l2->l2m, EV_L2_DL_DATA, skb);
		break;
	case DL_UNITDATA_REQ:
		ret = mISDN_FsmEvent(&l2->l2m, EV_L2_DL_UNITDATA, skb);
		break;
	case DL_ESTABLISH_REQ:
		if (test_bit(FLG_LAPB, &l2->flag))
			test_and_set_bit(FLG_ORIG, &l2->flag);
		if (test_bit(FLG_L1_ACTIV, &l2->flag)) {
			if (test_bit(FLG_LAPD, &l2->flag) ||
				test_bit(FLG_ORIG, &l2->flag))
				ret = mISDN_FsmEvent(&l2->l2m,
					EV_L2_DL_ESTABLISH_REQ, skb);
		} else {
			if (test_bit(FLG_LAPD, &l2->flag) ||
				test_bit(FLG_ORIG, &l2->flag)) {
				test_and_set_bit(FLG_ESTAB_PEND,
					&l2->flag);
			}
			ret = l2down(l2, PH_ACTIVATE_REQ, l2_newid(l2),
			    skb);
		}
		break;
	case DL_RELEASE_REQ:
		if (test_bit(FLG_LAPB, &l2->flag))
			l2down_create(l2, PH_DEACTIVATE_REQ,
				l2_newid(l2), 0, NULL);
		ret = mISDN_FsmEvent(&l2->l2m, EV_L2_DL_RELEASE_REQ,
		    skb);
		break;
	default:
		if (*debug & DEBUG_L2)
			l2m_debug(&l2->l2m, "l2 unknown pr %04x",
			    hh->prim);
	}
	if (ret) {
		dev_kfree_skb(skb);
		ret = 0;
	}
	return ret;
}

int
tei_l2(struct layer2 *l2, u_int cmd, u_long arg)
{
	int		ret = -EINVAL;

	if (*debug & DEBUG_L2_TEI)
		printk(KERN_DEBUG "%s: cmd(%x)\n", __func__, cmd);
	switch (cmd) {
	case (MDL_ASSIGN_REQ):
		ret = mISDN_FsmEvent(&l2->l2m, EV_L2_MDL_ASSIGN, (void *)arg);
		break;
	case (MDL_REMOVE_REQ):
		ret = mISDN_FsmEvent(&l2->l2m, EV_L2_MDL_REMOVE, NULL);
		break;
	case (MDL_ERROR_IND):
		ret = mISDN_FsmEvent(&l2->l2m, EV_L2_MDL_ERROR, NULL);
		break;
	case (MDL_ERROR_RSP):
		/* ETS 300-125 5.3.2.1 Test: TC13010 */
		printk(KERN_NOTICE "MDL_ERROR|REQ (tei_l2)\n");
		ret = mISDN_FsmEvent(&l2->l2m, EV_L2_MDL_ERROR, NULL);
		break;
	}
	return ret;
}

static void
release_l2(struct layer2 *l2)
{
	mISDN_FsmDelTimer(&l2->t200, 21);
	mISDN_FsmDelTimer(&l2->t203, 16);
	skb_queue_purge(&l2->i_queue);
	skb_queue_purge(&l2->ui_queue);
	skb_queue_purge(&l2->down_queue);
	ReleaseWin(l2);
	if (test_bit(FLG_LAPD, &l2->flag)) {
		TEIrelease(l2);
		if (l2->ch.st)
			l2->ch.st->dev->D.ctrl(&l2->ch.st->dev->D,
			    CLOSE_CHANNEL, NULL);
	}
	kfree(l2);
}

static int
l2_ctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
{
	struct layer2		*l2 = container_of(ch, struct layer2, ch);
	u_int			info;

	if (*debug & DEBUG_L2_CTRL)
		printk(KERN_DEBUG "%s:(%x)\n", __func__, cmd);

	switch (cmd) {
	case OPEN_CHANNEL:
		if (test_bit(FLG_LAPD, &l2->flag)) {
			set_channel_address(&l2->ch, l2->sapi, l2->tei);
			info = DL_INFO_L2_CONNECT;
			l2up_create(l2, DL_INFORMATION_IND,
			    sizeof(info), &info);
		}
		break;
	case CLOSE_CHANNEL:
		if (l2->ch.peer)
			l2->ch.peer->ctrl(l2->ch.peer, CLOSE_CHANNEL, NULL);
		release_l2(l2);
		break;
	}
	return 0;
}

struct layer2 *
create_l2(struct mISDNchannel *ch, u_int protocol, u_long options, int tei,
		int sapi)
{
	struct layer2		*l2;
	struct channel_req	rq;

	l2 = kzalloc(sizeof(struct layer2), GFP_KERNEL);
	if (!l2) {
		printk(KERN_ERR "kzalloc layer2 failed\n");
		return NULL;
	}
	l2->next_id = 1;
	l2->down_id = MISDN_ID_NONE;
	l2->up = ch;
	l2->ch.st = ch->st;
	l2->ch.send = l2_send;
	l2->ch.ctrl = l2_ctrl;
	switch (protocol) {
	case ISDN_P_LAPD_NT:
		test_and_set_bit(FLG_LAPD, &l2->flag);
		test_and_set_bit(FLG_LAPD_NET, &l2->flag);
		test_and_set_bit(FLG_MOD128, &l2->flag);
		l2->sapi = sapi;
		l2->maxlen = MAX_DFRAME_LEN;
		if (test_bit(OPTION_L2_PMX, &options))
			l2->window = 7;
		else
			l2->window = 1;
		if (test_bit(OPTION_L2_PTP, &options))
			test_and_set_bit(FLG_PTP, &l2->flag);
		if (test_bit(OPTION_L2_FIXEDTEI, &options))
			test_and_set_bit(FLG_FIXED_TEI, &l2->flag);
		l2->tei = tei;
		l2->T200 = 1000;
		l2->N200 = 3;
		l2->T203 = 10000;
		if (test_bit(OPTION_L2_PMX, &options))
			rq.protocol = ISDN_P_NT_E1;
		else
			rq.protocol = ISDN_P_NT_S0;
		rq.adr.channel = 0;
		l2->ch.st->dev->D.ctrl(&l2->ch.st->dev->D, OPEN_CHANNEL, &rq);
		break;
	case ISDN_P_LAPD_TE:
		test_and_set_bit(FLG_LAPD, &l2->flag);
		test_and_set_bit(FLG_MOD128, &l2->flag);
		test_and_set_bit(FLG_ORIG, &l2->flag);
		l2->sapi = sapi;
		l2->maxlen = MAX_DFRAME_LEN;
		if (test_bit(OPTION_L2_PMX, &options))
			l2->window = 7;
		else
			l2->window = 1;
		if (test_bit(OPTION_L2_PTP, &options))
			test_and_set_bit(FLG_PTP, &l2->flag);
		if (test_bit(OPTION_L2_FIXEDTEI, &options))
			test_and_set_bit(FLG_FIXED_TEI, &l2->flag);
		l2->tei = tei;
		l2->T200 = 1000;
		l2->N200 = 3;
		l2->T203 = 10000;
		if (test_bit(OPTION_L2_PMX, &options))
			rq.protocol = ISDN_P_TE_E1;
		else
			rq.protocol = ISDN_P_TE_S0;
		rq.adr.channel = 0;
		l2->ch.st->dev->D.ctrl(&l2->ch.st->dev->D, OPEN_CHANNEL, &rq);
		break;
	case ISDN_P_B_X75SLP:
		test_and_set_bit(FLG_LAPB, &l2->flag);
		l2->window = 7;
		l2->maxlen = MAX_DATA_SIZE;
		l2->T200 = 1000;
		l2->N200 = 4;
		l2->T203 = 5000;
		l2->addr.A = 3;
		l2->addr.B = 1;
		break;
	default:
		printk(KERN_ERR "layer2 create failed prt %x\n",
			protocol);
		kfree(l2);
		return NULL;
	}
	skb_queue_head_init(&l2->i_queue);
	skb_queue_head_init(&l2->ui_queue);
	skb_queue_head_init(&l2->down_queue);
	skb_queue_head_init(&l2->tmp_queue);
	InitWin(l2);
	l2->l2m.fsm = &l2fsm;
	if (test_bit(FLG_LAPB, &l2->flag) ||
		test_bit(FLG_PTP, &l2->flag) ||
		test_bit(FLG_LAPD_NET, &l2->flag))
		l2->l2m.state = ST_L2_4;
	else
		l2->l2m.state = ST_L2_1;
	l2->l2m.debug = *debug;
	l2->l2m.userdata = l2;
	l2->l2m.userint = 0;
	l2->l2m.printdebug = l2m_debug;

	mISDN_FsmInitTimer(&l2->l2m, &l2->t200);
	mISDN_FsmInitTimer(&l2->l2m, &l2->t203);
	return l2;
}

static int
x75create(struct channel_req *crq)
{
	struct layer2	*l2;

	if (crq->protocol != ISDN_P_B_X75SLP)
		return -EPROTONOSUPPORT;
	l2 = create_l2(crq->ch, crq->protocol, 0, 0, 0);
	if (!l2)
		return -ENOMEM;
	crq->ch = &l2->ch;
	crq->protocol = ISDN_P_B_HDLC;
	return 0;
}

static struct Bprotocol X75SLP = {
	.Bprotocols = (1 << (ISDN_P_B_X75SLP & ISDN_P_B_MASK)),
	.name = "X75SLP",
	.create = x75create
};

int
Isdnl2_Init(u_int *deb)
{
	debug = deb;
	mISDN_register_Bprotocol(&X75SLP);
	l2fsm.state_count = L2_STATE_COUNT;
	l2fsm.event_count = L2_EVENT_COUNT;
	l2fsm.strEvent = strL2Event;
	l2fsm.strState = strL2State;
	mISDN_FsmNew(&l2fsm, L2FnList, ARRAY_SIZE(L2FnList));
	TEIInit(deb);
	return 0;
}

void
Isdnl2_cleanup(void)
{
	mISDN_unregister_Bprotocol(&X75SLP);
	TEIFree();
	mISDN_FsmFree(&l2fsm);
}

