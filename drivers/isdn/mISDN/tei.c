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
#include "layer2.h"
#include <linux/random.h>
#include <linux/slab.h>
#include "core.h"

#define ID_REQUEST	1
#define ID_ASSIGNED	2
#define ID_DENIED	3
#define ID_CHK_REQ	4
#define ID_CHK_RES	5
#define ID_REMOVE	6
#define ID_VERIFY	7

#define TEI_ENTITY_ID	0xf

#define MGR_PH_ACTIVE	16
#define MGR_PH_NOTREADY	17

#define DATIMER_VAL	10000

static	u_int	*debug;

static struct Fsm deactfsm = {NULL, 0, 0, NULL, NULL};
static struct Fsm teifsmu = {NULL, 0, 0, NULL, NULL};
static struct Fsm teifsmn = {NULL, 0, 0, NULL, NULL};

enum {
	ST_L1_DEACT,
	ST_L1_DEACT_PENDING,
	ST_L1_ACTIV,
};
#define DEACT_STATE_COUNT (ST_L1_ACTIV + 1)

static char *strDeactState[] =
{
	"ST_L1_DEACT",
	"ST_L1_DEACT_PENDING",
	"ST_L1_ACTIV",
};

enum {
	EV_ACTIVATE,
	EV_ACTIVATE_IND,
	EV_DEACTIVATE,
	EV_DEACTIVATE_IND,
	EV_UI,
	EV_DATIMER,
};

#define DEACT_EVENT_COUNT (EV_DATIMER + 1)

static char *strDeactEvent[] =
{
	"EV_ACTIVATE",
	"EV_ACTIVATE_IND",
	"EV_DEACTIVATE",
	"EV_DEACTIVATE_IND",
	"EV_UI",
	"EV_DATIMER",
};

static void
da_debug(struct FsmInst *fi, char *fmt, ...)
{
	struct manager	*mgr = fi->userdata;
	struct va_format vaf;
	va_list va;

	if (!(*debug & DEBUG_L2_TEIFSM))
		return;

	va_start(va, fmt);

	vaf.fmt = fmt;
	vaf.va = &va;

	printk(KERN_DEBUG "mgr(%d): %pV\n", mgr->ch.st->dev->id, &vaf);

	va_end(va);
}

static void
da_activate(struct FsmInst *fi, int event, void *arg)
{
	struct manager	*mgr = fi->userdata;

	if (fi->state == ST_L1_DEACT_PENDING)
		mISDN_FsmDelTimer(&mgr->datimer, 1);
	mISDN_FsmChangeState(fi, ST_L1_ACTIV);
}

static void
da_deactivate_ind(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_L1_DEACT);
}

static void
da_deactivate(struct FsmInst *fi, int event, void *arg)
{
	struct manager	*mgr = fi->userdata;
	struct layer2	*l2;
	u_long		flags;

	read_lock_irqsave(&mgr->lock, flags);
	list_for_each_entry(l2, &mgr->layer2, list) {
		if (l2->l2m.state > ST_L2_4) {
			/* have still activ TEI */
			read_unlock_irqrestore(&mgr->lock, flags);
			return;
		}
	}
	read_unlock_irqrestore(&mgr->lock, flags);
	/* All TEI are inactiv */
	if (!test_bit(OPTION_L1_HOLD, &mgr->options)) {
		mISDN_FsmAddTimer(&mgr->datimer, DATIMER_VAL, EV_DATIMER,
				  NULL, 1);
		mISDN_FsmChangeState(fi, ST_L1_DEACT_PENDING);
	}
}

static void
da_ui(struct FsmInst *fi, int event, void *arg)
{
	struct manager	*mgr = fi->userdata;

	/* restart da timer */
	if (!test_bit(OPTION_L1_HOLD, &mgr->options)) {
		mISDN_FsmDelTimer(&mgr->datimer, 2);
		mISDN_FsmAddTimer(&mgr->datimer, DATIMER_VAL, EV_DATIMER,
				  NULL, 2);
	}
}

static void
da_timer(struct FsmInst *fi, int event, void *arg)
{
	struct manager	*mgr = fi->userdata;
	struct layer2	*l2;
	u_long		flags;

	/* check again */
	read_lock_irqsave(&mgr->lock, flags);
	list_for_each_entry(l2, &mgr->layer2, list) {
		if (l2->l2m.state > ST_L2_4) {
			/* have still activ TEI */
			read_unlock_irqrestore(&mgr->lock, flags);
			mISDN_FsmChangeState(fi, ST_L1_ACTIV);
			return;
		}
	}
	read_unlock_irqrestore(&mgr->lock, flags);
	/* All TEI are inactiv */
	mISDN_FsmChangeState(fi, ST_L1_DEACT);
	_queue_data(&mgr->ch, PH_DEACTIVATE_REQ, MISDN_ID_ANY, 0, NULL,
		    GFP_ATOMIC);
}

static struct FsmNode DeactFnList[] =
{
	{ST_L1_DEACT, EV_ACTIVATE_IND, da_activate},
	{ST_L1_ACTIV, EV_DEACTIVATE_IND, da_deactivate_ind},
	{ST_L1_ACTIV, EV_DEACTIVATE, da_deactivate},
	{ST_L1_DEACT_PENDING, EV_ACTIVATE, da_activate},
	{ST_L1_DEACT_PENDING, EV_UI, da_ui},
	{ST_L1_DEACT_PENDING, EV_DATIMER, da_timer},
};

enum {
	ST_TEI_NOP,
	ST_TEI_IDREQ,
	ST_TEI_IDVERIFY,
};

#define TEI_STATE_COUNT (ST_TEI_IDVERIFY + 1)

static char *strTeiState[] =
{
	"ST_TEI_NOP",
	"ST_TEI_IDREQ",
	"ST_TEI_IDVERIFY",
};

enum {
	EV_IDREQ,
	EV_ASSIGN,
	EV_ASSIGN_REQ,
	EV_DENIED,
	EV_CHKREQ,
	EV_CHKRESP,
	EV_REMOVE,
	EV_VERIFY,
	EV_TIMER,
};

#define TEI_EVENT_COUNT (EV_TIMER + 1)

static char *strTeiEvent[] =
{
	"EV_IDREQ",
	"EV_ASSIGN",
	"EV_ASSIGN_REQ",
	"EV_DENIED",
	"EV_CHKREQ",
	"EV_CHKRESP",
	"EV_REMOVE",
	"EV_VERIFY",
	"EV_TIMER",
};

static void
tei_debug(struct FsmInst *fi, char *fmt, ...)
{
	struct teimgr	*tm = fi->userdata;
	struct va_format vaf;
	va_list va;

	if (!(*debug & DEBUG_L2_TEIFSM))
		return;

	va_start(va, fmt);

	vaf.fmt = fmt;
	vaf.va = &va;

	printk(KERN_DEBUG "sapi(%d) tei(%d): %pV\n",
	       tm->l2->sapi, tm->l2->tei, &vaf);

	va_end(va);
}



static int
get_free_id(struct manager *mgr)
{
	DECLARE_BITMAP(ids, 64) = { [0 ... BITS_TO_LONGS(64) - 1] = 0 };
	int		i;
	struct layer2	*l2;

	list_for_each_entry(l2, &mgr->layer2, list) {
		if (l2->ch.nr > 63) {
			printk(KERN_WARNING
			       "%s: more as 63 layer2 for one device\n",
			       __func__);
			return -EBUSY;
		}
		__set_bit(l2->ch.nr, ids);
	}
	i = find_next_zero_bit(ids, 64, 1);
	if (i < 64)
		return i;
	printk(KERN_WARNING "%s: more as 63 layer2 for one device\n",
	       __func__);
	return -EBUSY;
}

static int
get_free_tei(struct manager *mgr)
{
	DECLARE_BITMAP(ids, 64) = { [0 ... BITS_TO_LONGS(64) - 1] = 0 };
	int		i;
	struct layer2	*l2;

	list_for_each_entry(l2, &mgr->layer2, list) {
		if (l2->ch.nr == 0)
			continue;
		if ((l2->ch.addr & 0xff) != 0)
			continue;
		i = l2->ch.addr >> 8;
		if (i < 64)
			continue;
		i -= 64;

		__set_bit(i, ids);
	}
	i = find_first_zero_bit(ids, 64);
	if (i < 64)
		return i + 64;
	printk(KERN_WARNING "%s: more as 63 dynamic tei for one device\n",
	       __func__);
	return -1;
}

static void
teiup_create(struct manager *mgr, u_int prim, int len, void *arg)
{
	struct sk_buff	*skb;
	struct mISDNhead *hh;
	int		err;

	skb = mI_alloc_skb(len, GFP_ATOMIC);
	if (!skb)
		return;
	hh = mISDN_HEAD_P(skb);
	hh->prim = prim;
	hh->id = (mgr->ch.nr << 16) | mgr->ch.addr;
	if (len)
		memcpy(skb_put(skb, len), arg, len);
	err = mgr->up->send(mgr->up, skb);
	if (err) {
		printk(KERN_WARNING "%s: err=%d\n", __func__, err);
		dev_kfree_skb(skb);
	}
}

static u_int
new_id(struct manager *mgr)
{
	u_int	id;

	id = mgr->nextid++;
	if (id == 0x7fff)
		mgr->nextid = 1;
	id <<= 16;
	id |= GROUP_TEI << 8;
	id |= TEI_SAPI;
	return id;
}

static void
do_send(struct manager *mgr)
{
	if (!test_bit(MGR_PH_ACTIVE, &mgr->options))
		return;

	if (!test_and_set_bit(MGR_PH_NOTREADY, &mgr->options)) {
		struct sk_buff	*skb = skb_dequeue(&mgr->sendq);

		if (!skb) {
			test_and_clear_bit(MGR_PH_NOTREADY, &mgr->options);
			return;
		}
		mgr->lastid = mISDN_HEAD_ID(skb);
		mISDN_FsmEvent(&mgr->deact, EV_UI, NULL);
		if (mgr->ch.recv(mgr->ch.peer, skb)) {
			dev_kfree_skb(skb);
			test_and_clear_bit(MGR_PH_NOTREADY, &mgr->options);
			mgr->lastid = MISDN_ID_NONE;
		}
	}
}

static void
do_ack(struct manager *mgr, u_int id)
{
	if (test_bit(MGR_PH_NOTREADY, &mgr->options)) {
		if (id == mgr->lastid) {
			if (test_bit(MGR_PH_ACTIVE, &mgr->options)) {
				struct sk_buff	*skb;

				skb = skb_dequeue(&mgr->sendq);
				if (skb) {
					mgr->lastid = mISDN_HEAD_ID(skb);
					if (!mgr->ch.recv(mgr->ch.peer, skb))
						return;
					dev_kfree_skb(skb);
				}
			}
			mgr->lastid = MISDN_ID_NONE;
			test_and_clear_bit(MGR_PH_NOTREADY, &mgr->options);
		}
	}
}

static void
mgr_send_down(struct manager *mgr, struct sk_buff *skb)
{
	skb_queue_tail(&mgr->sendq, skb);
	if (!test_bit(MGR_PH_ACTIVE, &mgr->options)) {
		_queue_data(&mgr->ch, PH_ACTIVATE_REQ, MISDN_ID_ANY, 0,
			    NULL, GFP_KERNEL);
	} else {
		do_send(mgr);
	}
}

static int
dl_unit_data(struct manager *mgr, struct sk_buff *skb)
{
	if (!test_bit(MGR_OPT_NETWORK, &mgr->options)) /* only net send UI */
		return -EINVAL;
	if (!test_bit(MGR_PH_ACTIVE, &mgr->options))
		_queue_data(&mgr->ch, PH_ACTIVATE_REQ, MISDN_ID_ANY, 0,
			    NULL, GFP_KERNEL);
	skb_push(skb, 3);
	skb->data[0] = 0x02; /* SAPI 0 C/R = 1 */
	skb->data[1] = 0xff; /* TEI 127 */
	skb->data[2] = UI;   /* UI frame */
	mISDN_HEAD_PRIM(skb) = PH_DATA_REQ;
	mISDN_HEAD_ID(skb) = new_id(mgr);
	skb_queue_tail(&mgr->sendq, skb);
	do_send(mgr);
	return 0;
}

static unsigned int
random_ri(void)
{
	u16 x;

	get_random_bytes(&x, sizeof(x));
	return x;
}

static struct layer2 *
findtei(struct manager *mgr, int tei)
{
	struct layer2	*l2;
	u_long		flags;

	read_lock_irqsave(&mgr->lock, flags);
	list_for_each_entry(l2, &mgr->layer2, list) {
		if ((l2->sapi == 0) && (l2->tei > 0) &&
		    (l2->tei != GROUP_TEI) && (l2->tei == tei))
			goto done;
	}
	l2 = NULL;
done:
	read_unlock_irqrestore(&mgr->lock, flags);
	return l2;
}

static void
put_tei_msg(struct manager *mgr, u_char m_id, unsigned int ri, int tei)
{
	struct sk_buff *skb;
	u_char bp[8];

	bp[0] = (TEI_SAPI << 2);
	if (test_bit(MGR_OPT_NETWORK, &mgr->options))
		bp[0] |= 2; /* CR:=1 for net command */
	bp[1] = (GROUP_TEI << 1) | 0x1;
	bp[2] = UI;
	bp[3] = TEI_ENTITY_ID;
	bp[4] = ri >> 8;
	bp[5] = ri & 0xff;
	bp[6] = m_id;
	bp[7] = ((tei << 1) & 0xff) | 1;
	skb = _alloc_mISDN_skb(PH_DATA_REQ, new_id(mgr), 8, bp, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_WARNING "%s: no skb for tei msg\n", __func__);
		return;
	}
	mgr_send_down(mgr, skb);
}

static void
tei_id_request(struct FsmInst *fi, int event, void *arg)
{
	struct teimgr *tm = fi->userdata;

	if (tm->l2->tei != GROUP_TEI) {
		tm->tei_m.printdebug(&tm->tei_m,
				     "assign request for already assigned tei %d",
				     tm->l2->tei);
		return;
	}
	tm->ri = random_ri();
	if (*debug & DEBUG_L2_TEI)
		tm->tei_m.printdebug(&tm->tei_m,
				     "assign request ri %d", tm->ri);
	put_tei_msg(tm->mgr, ID_REQUEST, tm->ri, GROUP_TEI);
	mISDN_FsmChangeState(fi, ST_TEI_IDREQ);
	mISDN_FsmAddTimer(&tm->timer, tm->tval, EV_TIMER, NULL, 1);
	tm->nval = 3;
}

static void
tei_id_assign(struct FsmInst *fi, int event, void *arg)
{
	struct teimgr	*tm = fi->userdata;
	struct layer2	*l2;
	u_char *dp = arg;
	int ri, tei;

	ri = ((unsigned int) *dp++ << 8);
	ri += *dp++;
	dp++;
	tei = *dp >> 1;
	if (*debug & DEBUG_L2_TEI)
		tm->tei_m.printdebug(fi, "identity assign ri %d tei %d",
				     ri, tei);
	l2 = findtei(tm->mgr, tei);
	if (l2) {	/* same tei is in use */
		if (ri != l2->tm->ri) {
			tm->tei_m.printdebug(fi,
					     "possible duplicate assignment tei %d", tei);
			tei_l2(l2, MDL_ERROR_RSP, 0);
		}
	} else if (ri == tm->ri) {
		mISDN_FsmDelTimer(&tm->timer, 1);
		mISDN_FsmChangeState(fi, ST_TEI_NOP);
		tei_l2(tm->l2, MDL_ASSIGN_REQ, tei);
	}
}

static void
tei_id_test_dup(struct FsmInst *fi, int event, void *arg)
{
	struct teimgr	*tm = fi->userdata;
	struct layer2	*l2;
	u_char *dp = arg;
	int tei, ri;

	ri = ((unsigned int) *dp++ << 8);
	ri += *dp++;
	dp++;
	tei = *dp >> 1;
	if (*debug & DEBUG_L2_TEI)
		tm->tei_m.printdebug(fi, "foreign identity assign ri %d tei %d",
				     ri, tei);
	l2 = findtei(tm->mgr, tei);
	if (l2) {	/* same tei is in use */
		if (ri != l2->tm->ri) {	/* and it wasn't our request */
			tm->tei_m.printdebug(fi,
					     "possible duplicate assignment tei %d", tei);
			mISDN_FsmEvent(&l2->tm->tei_m, EV_VERIFY, NULL);
		}
	}
}

static void
tei_id_denied(struct FsmInst *fi, int event, void *arg)
{
	struct teimgr *tm = fi->userdata;
	u_char *dp = arg;
	int ri, tei;

	ri = ((unsigned int) *dp++ << 8);
	ri += *dp++;
	dp++;
	tei = *dp >> 1;
	if (*debug & DEBUG_L2_TEI)
		tm->tei_m.printdebug(fi, "identity denied ri %d tei %d",
				     ri, tei);
}

static void
tei_id_chk_req(struct FsmInst *fi, int event, void *arg)
{
	struct teimgr *tm = fi->userdata;
	u_char *dp = arg;
	int tei;

	tei = *(dp + 3) >> 1;
	if (*debug & DEBUG_L2_TEI)
		tm->tei_m.printdebug(fi, "identity check req tei %d", tei);
	if ((tm->l2->tei != GROUP_TEI) && ((tei == GROUP_TEI) ||
					   (tei == tm->l2->tei))) {
		mISDN_FsmDelTimer(&tm->timer, 4);
		mISDN_FsmChangeState(&tm->tei_m, ST_TEI_NOP);
		put_tei_msg(tm->mgr, ID_CHK_RES, random_ri(), tm->l2->tei);
	}
}

static void
tei_id_remove(struct FsmInst *fi, int event, void *arg)
{
	struct teimgr *tm = fi->userdata;
	u_char *dp = arg;
	int tei;

	tei = *(dp + 3) >> 1;
	if (*debug & DEBUG_L2_TEI)
		tm->tei_m.printdebug(fi, "identity remove tei %d", tei);
	if ((tm->l2->tei != GROUP_TEI) &&
	    ((tei == GROUP_TEI) || (tei == tm->l2->tei))) {
		mISDN_FsmDelTimer(&tm->timer, 5);
		mISDN_FsmChangeState(&tm->tei_m, ST_TEI_NOP);
		tei_l2(tm->l2, MDL_REMOVE_REQ, 0);
	}
}

static void
tei_id_verify(struct FsmInst *fi, int event, void *arg)
{
	struct teimgr *tm = fi->userdata;

	if (*debug & DEBUG_L2_TEI)
		tm->tei_m.printdebug(fi, "id verify request for tei %d",
				     tm->l2->tei);
	put_tei_msg(tm->mgr, ID_VERIFY, 0, tm->l2->tei);
	mISDN_FsmChangeState(&tm->tei_m, ST_TEI_IDVERIFY);
	mISDN_FsmAddTimer(&tm->timer, tm->tval, EV_TIMER, NULL, 2);
	tm->nval = 2;
}

static void
tei_id_req_tout(struct FsmInst *fi, int event, void *arg)
{
	struct teimgr *tm = fi->userdata;

	if (--tm->nval) {
		tm->ri = random_ri();
		if (*debug & DEBUG_L2_TEI)
			tm->tei_m.printdebug(fi, "assign req(%d) ri %d",
					     4 - tm->nval, tm->ri);
		put_tei_msg(tm->mgr, ID_REQUEST, tm->ri, GROUP_TEI);
		mISDN_FsmAddTimer(&tm->timer, tm->tval, EV_TIMER, NULL, 3);
	} else {
		tm->tei_m.printdebug(fi, "assign req failed");
		tei_l2(tm->l2, MDL_ERROR_RSP, 0);
		mISDN_FsmChangeState(fi, ST_TEI_NOP);
	}
}

static void
tei_id_ver_tout(struct FsmInst *fi, int event, void *arg)
{
	struct teimgr *tm = fi->userdata;

	if (--tm->nval) {
		if (*debug & DEBUG_L2_TEI)
			tm->tei_m.printdebug(fi,
					     "id verify req(%d) for tei %d",
					     3 - tm->nval, tm->l2->tei);
		put_tei_msg(tm->mgr, ID_VERIFY, 0, tm->l2->tei);
		mISDN_FsmAddTimer(&tm->timer, tm->tval, EV_TIMER, NULL, 4);
	} else {
		tm->tei_m.printdebug(fi, "verify req for tei %d failed",
				     tm->l2->tei);
		tei_l2(tm->l2, MDL_REMOVE_REQ, 0);
		mISDN_FsmChangeState(fi, ST_TEI_NOP);
	}
}

static struct FsmNode TeiFnListUser[] =
{
	{ST_TEI_NOP, EV_IDREQ, tei_id_request},
	{ST_TEI_NOP, EV_ASSIGN, tei_id_test_dup},
	{ST_TEI_NOP, EV_VERIFY, tei_id_verify},
	{ST_TEI_NOP, EV_REMOVE, tei_id_remove},
	{ST_TEI_NOP, EV_CHKREQ, tei_id_chk_req},
	{ST_TEI_IDREQ, EV_TIMER, tei_id_req_tout},
	{ST_TEI_IDREQ, EV_ASSIGN, tei_id_assign},
	{ST_TEI_IDREQ, EV_DENIED, tei_id_denied},
	{ST_TEI_IDVERIFY, EV_TIMER, tei_id_ver_tout},
	{ST_TEI_IDVERIFY, EV_REMOVE, tei_id_remove},
	{ST_TEI_IDVERIFY, EV_CHKREQ, tei_id_chk_req},
};

static void
tei_l2remove(struct layer2 *l2)
{
	put_tei_msg(l2->tm->mgr, ID_REMOVE, 0, l2->tei);
	tei_l2(l2, MDL_REMOVE_REQ, 0);
	list_del(&l2->ch.list);
	l2->ch.ctrl(&l2->ch, CLOSE_CHANNEL, NULL);
}

static void
tei_assign_req(struct FsmInst *fi, int event, void *arg)
{
	struct teimgr *tm = fi->userdata;
	u_char *dp = arg;

	if (tm->l2->tei == GROUP_TEI) {
		tm->tei_m.printdebug(&tm->tei_m,
				     "net tei assign request without tei");
		return;
	}
	tm->ri = ((unsigned int) *dp++ << 8);
	tm->ri += *dp++;
	if (*debug & DEBUG_L2_TEI)
		tm->tei_m.printdebug(&tm->tei_m,
				     "net assign request ri %d teim %d", tm->ri, *dp);
	put_tei_msg(tm->mgr, ID_ASSIGNED, tm->ri, tm->l2->tei);
	mISDN_FsmChangeState(fi, ST_TEI_NOP);
}

static void
tei_id_chk_req_net(struct FsmInst *fi, int event, void *arg)
{
	struct teimgr	*tm = fi->userdata;

	if (*debug & DEBUG_L2_TEI)
		tm->tei_m.printdebug(fi, "id check request for tei %d",
				     tm->l2->tei);
	tm->rcnt = 0;
	put_tei_msg(tm->mgr, ID_CHK_REQ, 0, tm->l2->tei);
	mISDN_FsmChangeState(&tm->tei_m, ST_TEI_IDVERIFY);
	mISDN_FsmAddTimer(&tm->timer, tm->tval, EV_TIMER, NULL, 2);
	tm->nval = 2;
}

static void
tei_id_chk_resp(struct FsmInst *fi, int event, void *arg)
{
	struct teimgr *tm = fi->userdata;
	u_char *dp = arg;
	int tei;

	tei = dp[3] >> 1;
	if (*debug & DEBUG_L2_TEI)
		tm->tei_m.printdebug(fi, "identity check resp tei %d", tei);
	if (tei == tm->l2->tei)
		tm->rcnt++;
}

static void
tei_id_verify_net(struct FsmInst *fi, int event, void *arg)
{
	struct teimgr *tm = fi->userdata;
	u_char *dp = arg;
	int tei;

	tei = dp[3] >> 1;
	if (*debug & DEBUG_L2_TEI)
		tm->tei_m.printdebug(fi, "identity verify req tei %d/%d",
				     tei, tm->l2->tei);
	if (tei == tm->l2->tei)
		tei_id_chk_req_net(fi, event, arg);
}

static void
tei_id_ver_tout_net(struct FsmInst *fi, int event, void *arg)
{
	struct teimgr *tm = fi->userdata;

	if (tm->rcnt == 1) {
		if (*debug & DEBUG_L2_TEI)
			tm->tei_m.printdebug(fi,
					     "check req for tei %d successful\n", tm->l2->tei);
		mISDN_FsmChangeState(fi, ST_TEI_NOP);
	} else if (tm->rcnt > 1) {
		/* duplicate assignment; remove */
		tei_l2remove(tm->l2);
	} else if (--tm->nval) {
		if (*debug & DEBUG_L2_TEI)
			tm->tei_m.printdebug(fi,
					     "id check req(%d) for tei %d",
					     3 - tm->nval, tm->l2->tei);
		put_tei_msg(tm->mgr, ID_CHK_REQ, 0, tm->l2->tei);
		mISDN_FsmAddTimer(&tm->timer, tm->tval, EV_TIMER, NULL, 4);
	} else {
		tm->tei_m.printdebug(fi, "check req for tei %d failed",
				     tm->l2->tei);
		mISDN_FsmChangeState(fi, ST_TEI_NOP);
		tei_l2remove(tm->l2);
	}
}

static struct FsmNode TeiFnListNet[] =
{
	{ST_TEI_NOP, EV_ASSIGN_REQ, tei_assign_req},
	{ST_TEI_NOP, EV_VERIFY, tei_id_verify_net},
	{ST_TEI_NOP, EV_CHKREQ, tei_id_chk_req_net},
	{ST_TEI_IDVERIFY, EV_TIMER, tei_id_ver_tout_net},
	{ST_TEI_IDVERIFY, EV_CHKRESP, tei_id_chk_resp},
};

static void
tei_ph_data_ind(struct teimgr *tm, u_int mt, u_char *dp, int len)
{
	if (test_bit(FLG_FIXED_TEI, &tm->l2->flag))
		return;
	if (*debug & DEBUG_L2_TEI)
		tm->tei_m.printdebug(&tm->tei_m, "tei handler mt %x", mt);
	if (mt == ID_ASSIGNED)
		mISDN_FsmEvent(&tm->tei_m, EV_ASSIGN, dp);
	else if (mt == ID_DENIED)
		mISDN_FsmEvent(&tm->tei_m, EV_DENIED, dp);
	else if (mt == ID_CHK_REQ)
		mISDN_FsmEvent(&tm->tei_m, EV_CHKREQ, dp);
	else if (mt == ID_REMOVE)
		mISDN_FsmEvent(&tm->tei_m, EV_REMOVE, dp);
	else if (mt == ID_VERIFY)
		mISDN_FsmEvent(&tm->tei_m, EV_VERIFY, dp);
	else if (mt == ID_CHK_RES)
		mISDN_FsmEvent(&tm->tei_m, EV_CHKRESP, dp);
}

static struct layer2 *
create_new_tei(struct manager *mgr, int tei, int sapi)
{
	unsigned long		opt = 0;
	unsigned long		flags;
	int			id;
	struct layer2		*l2;
	struct channel_req	rq;

	if (!mgr->up)
		return NULL;
	if ((tei >= 0) && (tei < 64))
		test_and_set_bit(OPTION_L2_FIXEDTEI, &opt);
	if (mgr->ch.st->dev->Dprotocols & ((1 << ISDN_P_TE_E1) |
	    (1 << ISDN_P_NT_E1))) {
		test_and_set_bit(OPTION_L2_PMX, &opt);
		rq.protocol = ISDN_P_NT_E1;
	} else {
		rq.protocol = ISDN_P_NT_S0;
	}
	l2 = create_l2(mgr->up, ISDN_P_LAPD_NT, opt, tei, sapi);
	if (!l2) {
		printk(KERN_WARNING "%s:no memory for layer2\n", __func__);
		return NULL;
	}
	l2->tm = kzalloc(sizeof(struct teimgr), GFP_KERNEL);
	if (!l2->tm) {
		kfree(l2);
		printk(KERN_WARNING "%s:no memory for teimgr\n", __func__);
		return NULL;
	}
	l2->tm->mgr = mgr;
	l2->tm->l2 = l2;
	l2->tm->tei_m.debug = *debug & DEBUG_L2_TEIFSM;
	l2->tm->tei_m.userdata = l2->tm;
	l2->tm->tei_m.printdebug = tei_debug;
	l2->tm->tei_m.fsm = &teifsmn;
	l2->tm->tei_m.state = ST_TEI_NOP;
	l2->tm->tval = 2000; /* T202  2 sec */
	mISDN_FsmInitTimer(&l2->tm->tei_m, &l2->tm->timer);
	write_lock_irqsave(&mgr->lock, flags);
	id = get_free_id(mgr);
	list_add_tail(&l2->list, &mgr->layer2);
	write_unlock_irqrestore(&mgr->lock, flags);
	if (id < 0) {
		l2->ch.ctrl(&l2->ch, CLOSE_CHANNEL, NULL);
		printk(KERN_WARNING "%s:no free id\n", __func__);
		return NULL;
	} else {
		l2->ch.nr = id;
		__add_layer2(&l2->ch, mgr->ch.st);
		l2->ch.recv = mgr->ch.recv;
		l2->ch.peer = mgr->ch.peer;
		l2->ch.ctrl(&l2->ch, OPEN_CHANNEL, NULL);
		/* We need open here L1 for the manager as well (refcounting) */
		rq.adr.dev = mgr->ch.st->dev->id;
		id = mgr->ch.st->own.ctrl(&mgr->ch.st->own, OPEN_CHANNEL, &rq);
		if (id < 0) {
			printk(KERN_WARNING "%s: cannot open L1\n", __func__);
			l2->ch.ctrl(&l2->ch, CLOSE_CHANNEL, NULL);
			l2 = NULL;
		}
	}
	return l2;
}

static void
new_tei_req(struct manager *mgr, u_char *dp)
{
	int		tei, ri;
	struct layer2	*l2;

	ri = dp[0] << 8;
	ri += dp[1];
	if (!mgr->up)
		goto denied;
	if (!(dp[3] & 1)) /* Extension bit != 1 */
		goto denied;
	if (dp[3] != 0xff)
		tei = dp[3] >> 1; /* 3GPP TS 08.56 6.1.11.2 */
	else
		tei = get_free_tei(mgr);
	if (tei < 0) {
		printk(KERN_WARNING "%s:No free tei\n", __func__);
		goto denied;
	}
	l2 = create_new_tei(mgr, tei, CTRL_SAPI);
	if (!l2)
		goto denied;
	else
		mISDN_FsmEvent(&l2->tm->tei_m, EV_ASSIGN_REQ, dp);
	return;
denied:
	put_tei_msg(mgr, ID_DENIED, ri, GROUP_TEI);
}

static int
ph_data_ind(struct manager *mgr, struct sk_buff *skb)
{
	int		ret = -EINVAL;
	struct layer2	*l2, *nl2;
	u_char		mt;

	if (skb->len < 8) {
		if (*debug  & DEBUG_L2_TEI)
			printk(KERN_DEBUG "%s: short mgr frame %d/8\n",
			       __func__, skb->len);
		goto done;
	}

	if ((skb->data[0] >> 2) != TEI_SAPI) /* not for us */
		goto done;
	if (skb->data[0] & 1) /* EA0 formal error */
		goto done;
	if (!(skb->data[1] & 1)) /* EA1 formal error */
		goto done;
	if ((skb->data[1] >> 1) != GROUP_TEI) /* not for us */
		goto done;
	if ((skb->data[2] & 0xef) != UI) /* not UI */
		goto done;
	if (skb->data[3] != TEI_ENTITY_ID) /* not tei entity */
		goto done;
	mt = skb->data[6];
	switch (mt) {
	case ID_REQUEST:
	case ID_CHK_RES:
	case ID_VERIFY:
		if (!test_bit(MGR_OPT_NETWORK, &mgr->options))
			goto done;
		break;
	case ID_ASSIGNED:
	case ID_DENIED:
	case ID_CHK_REQ:
	case ID_REMOVE:
		if (test_bit(MGR_OPT_NETWORK, &mgr->options))
			goto done;
		break;
	default:
		goto done;
	}
	ret = 0;
	if (mt == ID_REQUEST) {
		new_tei_req(mgr, &skb->data[4]);
		goto done;
	}
	list_for_each_entry_safe(l2, nl2, &mgr->layer2, list) {
		tei_ph_data_ind(l2->tm, mt, &skb->data[4], skb->len - 4);
	}
done:
	return ret;
}

int
l2_tei(struct layer2 *l2, u_int cmd, u_long arg)
{
	struct teimgr	*tm = l2->tm;

	if (test_bit(FLG_FIXED_TEI, &l2->flag))
		return 0;
	if (*debug & DEBUG_L2_TEI)
		printk(KERN_DEBUG "%s: cmd(%x)\n", __func__, cmd);
	switch (cmd) {
	case MDL_ASSIGN_IND:
		mISDN_FsmEvent(&tm->tei_m, EV_IDREQ, NULL);
		break;
	case MDL_ERROR_IND:
		if (test_bit(MGR_OPT_NETWORK, &tm->mgr->options))
			mISDN_FsmEvent(&tm->tei_m, EV_CHKREQ, &l2->tei);
		if (test_bit(MGR_OPT_USER, &tm->mgr->options))
			mISDN_FsmEvent(&tm->tei_m, EV_VERIFY, NULL);
		break;
	case MDL_STATUS_UP_IND:
		if (test_bit(MGR_OPT_NETWORK, &tm->mgr->options))
			mISDN_FsmEvent(&tm->mgr->deact, EV_ACTIVATE, NULL);
		break;
	case MDL_STATUS_DOWN_IND:
		if (test_bit(MGR_OPT_NETWORK, &tm->mgr->options))
			mISDN_FsmEvent(&tm->mgr->deact, EV_DEACTIVATE, NULL);
		break;
	case MDL_STATUS_UI_IND:
		if (test_bit(MGR_OPT_NETWORK, &tm->mgr->options))
			mISDN_FsmEvent(&tm->mgr->deact, EV_UI, NULL);
		break;
	}
	return 0;
}

void
TEIrelease(struct layer2 *l2)
{
	struct teimgr	*tm = l2->tm;
	u_long		flags;

	mISDN_FsmDelTimer(&tm->timer, 1);
	write_lock_irqsave(&tm->mgr->lock, flags);
	list_del(&l2->list);
	write_unlock_irqrestore(&tm->mgr->lock, flags);
	l2->tm = NULL;
	kfree(tm);
}

static int
create_teimgr(struct manager *mgr, struct channel_req *crq)
{
	struct layer2		*l2;
	unsigned long		opt = 0;
	unsigned long		flags;
	int			id;
	struct channel_req	l1rq;

	if (*debug & DEBUG_L2_TEI)
		printk(KERN_DEBUG "%s: %s proto(%x) adr(%d %d %d %d)\n",
		       __func__, dev_name(&mgr->ch.st->dev->dev),
		       crq->protocol, crq->adr.dev, crq->adr.channel,
		       crq->adr.sapi, crq->adr.tei);
	if (crq->adr.tei > GROUP_TEI)
		return -EINVAL;
	if (crq->adr.tei < 64)
		test_and_set_bit(OPTION_L2_FIXEDTEI, &opt);
	if (crq->adr.tei == 0)
		test_and_set_bit(OPTION_L2_PTP, &opt);
	if (test_bit(MGR_OPT_NETWORK, &mgr->options)) {
		if (crq->protocol == ISDN_P_LAPD_TE)
			return -EPROTONOSUPPORT;
		if ((crq->adr.tei != 0) && (crq->adr.tei != 127))
			return -EINVAL;
		if (mgr->up) {
			printk(KERN_WARNING
			       "%s: only one network manager is allowed\n",
			       __func__);
			return -EBUSY;
		}
	} else if (test_bit(MGR_OPT_USER, &mgr->options)) {
		if (crq->protocol == ISDN_P_LAPD_NT)
			return -EPROTONOSUPPORT;
		if ((crq->adr.tei >= 64) && (crq->adr.tei < GROUP_TEI))
			return -EINVAL; /* dyn tei */
	} else {
		if (crq->protocol == ISDN_P_LAPD_NT)
			test_and_set_bit(MGR_OPT_NETWORK, &mgr->options);
		if (crq->protocol == ISDN_P_LAPD_TE)
			test_and_set_bit(MGR_OPT_USER, &mgr->options);
	}
	l1rq.adr = crq->adr;
	if (mgr->ch.st->dev->Dprotocols
	    & ((1 << ISDN_P_TE_E1) | (1 << ISDN_P_NT_E1)))
		test_and_set_bit(OPTION_L2_PMX, &opt);
	if ((crq->protocol == ISDN_P_LAPD_NT) && (crq->adr.tei == 127)) {
		mgr->up = crq->ch;
		id = DL_INFO_L2_CONNECT;
		teiup_create(mgr, DL_INFORMATION_IND, sizeof(id), &id);
		if (test_bit(MGR_PH_ACTIVE, &mgr->options))
			teiup_create(mgr, PH_ACTIVATE_IND, 0, NULL);
		crq->ch = NULL;
		if (!list_empty(&mgr->layer2)) {
			read_lock_irqsave(&mgr->lock, flags);
			list_for_each_entry(l2, &mgr->layer2, list) {
				l2->up = mgr->up;
				l2->ch.ctrl(&l2->ch, OPEN_CHANNEL, NULL);
			}
			read_unlock_irqrestore(&mgr->lock, flags);
		}
		return 0;
	}
	l2 = create_l2(crq->ch, crq->protocol, opt,
		       crq->adr.tei, crq->adr.sapi);
	if (!l2)
		return -ENOMEM;
	l2->tm = kzalloc(sizeof(struct teimgr), GFP_KERNEL);
	if (!l2->tm) {
		kfree(l2);
		printk(KERN_ERR "kmalloc teimgr failed\n");
		return -ENOMEM;
	}
	l2->tm->mgr = mgr;
	l2->tm->l2 = l2;
	l2->tm->tei_m.debug = *debug & DEBUG_L2_TEIFSM;
	l2->tm->tei_m.userdata = l2->tm;
	l2->tm->tei_m.printdebug = tei_debug;
	if (crq->protocol == ISDN_P_LAPD_TE) {
		l2->tm->tei_m.fsm = &teifsmu;
		l2->tm->tei_m.state = ST_TEI_NOP;
		l2->tm->tval = 1000; /* T201  1 sec */
		if (test_bit(OPTION_L2_PMX, &opt))
			l1rq.protocol = ISDN_P_TE_E1;
		else
			l1rq.protocol = ISDN_P_TE_S0;
	} else {
		l2->tm->tei_m.fsm = &teifsmn;
		l2->tm->tei_m.state = ST_TEI_NOP;
		l2->tm->tval = 2000; /* T202  2 sec */
		if (test_bit(OPTION_L2_PMX, &opt))
			l1rq.protocol = ISDN_P_NT_E1;
		else
			l1rq.protocol = ISDN_P_NT_S0;
	}
	mISDN_FsmInitTimer(&l2->tm->tei_m, &l2->tm->timer);
	write_lock_irqsave(&mgr->lock, flags);
	id = get_free_id(mgr);
	list_add_tail(&l2->list, &mgr->layer2);
	write_unlock_irqrestore(&mgr->lock, flags);
	if (id >= 0) {
		l2->ch.nr = id;
		l2->up->nr = id;
		crq->ch = &l2->ch;
		/* We need open here L1 for the manager as well (refcounting) */
		id = mgr->ch.st->own.ctrl(&mgr->ch.st->own, OPEN_CHANNEL,
					  &l1rq);
	}
	if (id < 0)
		l2->ch.ctrl(&l2->ch, CLOSE_CHANNEL, NULL);
	return id;
}

static int
mgr_send(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct manager	*mgr;
	struct mISDNhead	*hh =  mISDN_HEAD_P(skb);
	int			ret = -EINVAL;

	mgr = container_of(ch, struct manager, ch);
	if (*debug & DEBUG_L2_RECV)
		printk(KERN_DEBUG "%s: prim(%x) id(%x)\n",
		       __func__, hh->prim, hh->id);
	switch (hh->prim) {
	case PH_DATA_IND:
		mISDN_FsmEvent(&mgr->deact, EV_UI, NULL);
		ret = ph_data_ind(mgr, skb);
		break;
	case PH_DATA_CNF:
		do_ack(mgr, hh->id);
		ret = 0;
		break;
	case PH_ACTIVATE_IND:
		test_and_set_bit(MGR_PH_ACTIVE, &mgr->options);
		if (mgr->up)
			teiup_create(mgr, PH_ACTIVATE_IND, 0, NULL);
		mISDN_FsmEvent(&mgr->deact, EV_ACTIVATE_IND, NULL);
		do_send(mgr);
		ret = 0;
		break;
	case PH_DEACTIVATE_IND:
		test_and_clear_bit(MGR_PH_ACTIVE, &mgr->options);
		if (mgr->up)
			teiup_create(mgr, PH_DEACTIVATE_IND, 0, NULL);
		mISDN_FsmEvent(&mgr->deact, EV_DEACTIVATE_IND, NULL);
		ret = 0;
		break;
	case DL_UNITDATA_REQ:
		return dl_unit_data(mgr, skb);
	}
	if (!ret)
		dev_kfree_skb(skb);
	return ret;
}

static int
free_teimanager(struct manager *mgr)
{
	struct layer2	*l2, *nl2;

	test_and_clear_bit(OPTION_L1_HOLD, &mgr->options);
	if (test_bit(MGR_OPT_NETWORK, &mgr->options)) {
		/* not locked lock is taken in release tei */
		mgr->up = NULL;
		if (test_bit(OPTION_L2_CLEANUP, &mgr->options)) {
			list_for_each_entry_safe(l2, nl2, &mgr->layer2, list) {
				put_tei_msg(mgr, ID_REMOVE, 0, l2->tei);
				mutex_lock(&mgr->ch.st->lmutex);
				list_del(&l2->ch.list);
				mutex_unlock(&mgr->ch.st->lmutex);
				l2->ch.ctrl(&l2->ch, CLOSE_CHANNEL, NULL);
			}
			test_and_clear_bit(MGR_OPT_NETWORK, &mgr->options);
		} else {
			list_for_each_entry_safe(l2, nl2, &mgr->layer2, list) {
				l2->up = NULL;
			}
		}
	}
	if (test_bit(MGR_OPT_USER, &mgr->options)) {
		if (list_empty(&mgr->layer2))
			test_and_clear_bit(MGR_OPT_USER, &mgr->options);
	}
	mgr->ch.st->dev->D.ctrl(&mgr->ch.st->dev->D, CLOSE_CHANNEL, NULL);
	return 0;
}

static int
ctrl_teimanager(struct manager *mgr, void *arg)
{
	/* currently we only have one option */
	int	*val = (int *)arg;
	int	ret = 0;

	switch (val[0]) {
	case IMCLEAR_L2:
		if (val[1])
			test_and_set_bit(OPTION_L2_CLEANUP, &mgr->options);
		else
			test_and_clear_bit(OPTION_L2_CLEANUP, &mgr->options);
		break;
	case IMHOLD_L1:
		if (val[1])
			test_and_set_bit(OPTION_L1_HOLD, &mgr->options);
		else
			test_and_clear_bit(OPTION_L1_HOLD, &mgr->options);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

/* This function does create a L2 for fixed TEI in NT Mode */
static int
check_data(struct manager *mgr, struct sk_buff *skb)
{
	struct mISDNhead	*hh =  mISDN_HEAD_P(skb);
	int			ret, tei, sapi;
	struct layer2		*l2;

	if (*debug & DEBUG_L2_CTRL)
		printk(KERN_DEBUG "%s: prim(%x) id(%x)\n",
		       __func__, hh->prim, hh->id);
	if (test_bit(MGR_OPT_USER, &mgr->options))
		return -ENOTCONN;
	if (hh->prim != PH_DATA_IND)
		return -ENOTCONN;
	if (skb->len != 3)
		return -ENOTCONN;
	if (skb->data[0] & 3) /* EA0 and CR must be  0 */
		return -EINVAL;
	sapi = skb->data[0] >> 2;
	if (!(skb->data[1] & 1)) /* invalid EA1 */
		return -EINVAL;
	tei = skb->data[1] >> 1;
	if (tei > 63) /* not a fixed tei */
		return -ENOTCONN;
	if ((skb->data[2] & ~0x10) != SABME)
		return -ENOTCONN;
	/* We got a SABME for a fixed TEI */
	if (*debug & DEBUG_L2_CTRL)
		printk(KERN_DEBUG "%s: SABME sapi(%d) tei(%d)\n",
		       __func__, sapi, tei);
	l2 = create_new_tei(mgr, tei, sapi);
	if (!l2) {
		if (*debug & DEBUG_L2_CTRL)
			printk(KERN_DEBUG "%s: failed to create new tei\n",
			       __func__);
		return -ENOMEM;
	}
	ret = l2->ch.send(&l2->ch, skb);
	return ret;
}

void
delete_teimanager(struct mISDNchannel *ch)
{
	struct manager	*mgr;
	struct layer2	*l2, *nl2;

	mgr = container_of(ch, struct manager, ch);
	/* not locked lock is taken in release tei */
	list_for_each_entry_safe(l2, nl2, &mgr->layer2, list) {
		mutex_lock(&mgr->ch.st->lmutex);
		list_del(&l2->ch.list);
		mutex_unlock(&mgr->ch.st->lmutex);
		l2->ch.ctrl(&l2->ch, CLOSE_CHANNEL, NULL);
	}
	list_del(&mgr->ch.list);
	list_del(&mgr->bcast.list);
	skb_queue_purge(&mgr->sendq);
	kfree(mgr);
}

static int
mgr_ctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
{
	struct manager	*mgr;
	int		ret = -EINVAL;

	mgr = container_of(ch, struct manager, ch);
	if (*debug & DEBUG_L2_CTRL)
		printk(KERN_DEBUG "%s(%x, %p)\n", __func__, cmd, arg);
	switch (cmd) {
	case OPEN_CHANNEL:
		ret = create_teimgr(mgr, arg);
		break;
	case CLOSE_CHANNEL:
		ret = free_teimanager(mgr);
		break;
	case CONTROL_CHANNEL:
		ret = ctrl_teimanager(mgr, arg);
		break;
	case CHECK_DATA:
		ret = check_data(mgr, arg);
		break;
	}
	return ret;
}

static int
mgr_bcast(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct manager		*mgr = container_of(ch, struct manager, bcast);
	struct mISDNhead	*hhc, *hh = mISDN_HEAD_P(skb);
	struct sk_buff		*cskb = NULL;
	struct layer2		*l2;
	u_long			flags;
	int			ret;

	read_lock_irqsave(&mgr->lock, flags);
	list_for_each_entry(l2, &mgr->layer2, list) {
		if ((hh->id & MISDN_ID_SAPI_MASK) ==
		    (l2->ch.addr & MISDN_ID_SAPI_MASK)) {
			if (list_is_last(&l2->list, &mgr->layer2)) {
				cskb = skb;
				skb = NULL;
			} else {
				if (!cskb)
					cskb = skb_copy(skb, GFP_ATOMIC);
			}
			if (cskb) {
				hhc = mISDN_HEAD_P(cskb);
				/* save original header behind normal header */
				hhc++;
				*hhc = *hh;
				hhc--;
				hhc->prim = DL_INTERN_MSG;
				hhc->id = l2->ch.nr;
				ret = ch->st->own.recv(&ch->st->own, cskb);
				if (ret) {
					if (*debug & DEBUG_SEND_ERR)
						printk(KERN_DEBUG
						       "%s ch%d prim(%x) addr(%x)"
						       " err %d\n",
						       __func__, l2->ch.nr,
						       hh->prim, l2->ch.addr, ret);
				} else
					cskb = NULL;
			} else {
				printk(KERN_WARNING "%s ch%d addr %x no mem\n",
				       __func__, ch->nr, ch->addr);
				goto out;
			}
		}
	}
out:
	read_unlock_irqrestore(&mgr->lock, flags);
	if (cskb)
		dev_kfree_skb(cskb);
	if (skb)
		dev_kfree_skb(skb);
	return 0;
}

static int
mgr_bcast_ctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
{

	return -EINVAL;
}

int
create_teimanager(struct mISDNdevice *dev)
{
	struct manager *mgr;

	mgr = kzalloc(sizeof(struct manager), GFP_KERNEL);
	if (!mgr)
		return -ENOMEM;
	INIT_LIST_HEAD(&mgr->layer2);
	rwlock_init(&mgr->lock);
	skb_queue_head_init(&mgr->sendq);
	mgr->nextid = 1;
	mgr->lastid = MISDN_ID_NONE;
	mgr->ch.send = mgr_send;
	mgr->ch.ctrl = mgr_ctrl;
	mgr->ch.st = dev->D.st;
	set_channel_address(&mgr->ch, TEI_SAPI, GROUP_TEI);
	add_layer2(&mgr->ch, dev->D.st);
	mgr->bcast.send = mgr_bcast;
	mgr->bcast.ctrl = mgr_bcast_ctrl;
	mgr->bcast.st = dev->D.st;
	set_channel_address(&mgr->bcast, 0, GROUP_TEI);
	add_layer2(&mgr->bcast, dev->D.st);
	mgr->deact.debug = *debug & DEBUG_MANAGER;
	mgr->deact.userdata = mgr;
	mgr->deact.printdebug = da_debug;
	mgr->deact.fsm = &deactfsm;
	mgr->deact.state = ST_L1_DEACT;
	mISDN_FsmInitTimer(&mgr->deact, &mgr->datimer);
	dev->teimgr = &mgr->ch;
	return 0;
}

int TEIInit(u_int *deb)
{
	debug = deb;
	teifsmu.state_count = TEI_STATE_COUNT;
	teifsmu.event_count = TEI_EVENT_COUNT;
	teifsmu.strEvent = strTeiEvent;
	teifsmu.strState = strTeiState;
	mISDN_FsmNew(&teifsmu, TeiFnListUser, ARRAY_SIZE(TeiFnListUser));
	teifsmn.state_count = TEI_STATE_COUNT;
	teifsmn.event_count = TEI_EVENT_COUNT;
	teifsmn.strEvent = strTeiEvent;
	teifsmn.strState = strTeiState;
	mISDN_FsmNew(&teifsmn, TeiFnListNet, ARRAY_SIZE(TeiFnListNet));
	deactfsm.state_count =  DEACT_STATE_COUNT;
	deactfsm.event_count = DEACT_EVENT_COUNT;
	deactfsm.strEvent = strDeactEvent;
	deactfsm.strState = strDeactState;
	mISDN_FsmNew(&deactfsm, DeactFnList, ARRAY_SIZE(DeactFnList));
	return 0;
}

void TEIFree(void)
{
	mISDN_FsmFree(&teifsmu);
	mISDN_FsmFree(&teifsmn);
	mISDN_FsmFree(&deactfsm);
}
