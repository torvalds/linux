/* $Id: isdnl1.c,v 2.46.2.5 2004/02/11 13:21:34 keil Exp $
 *
 * common low level stuff for Siemens Chipsetbased isdn cards
 *
 * Author       Karsten Keil
 *              based on the teles driver from Jan den Ouden
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * For changes and modifications please read
 * Documentation/isdn/HiSax.cert
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *              Beat Doebeli
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "isdnl1.h"

const char *l1_revision = "$Revision: 2.46.2.5 $";

#define TIMER3_VALUE 7000

static struct Fsm l1fsm_b;
static struct Fsm l1fsm_s;

enum {
	ST_L1_F2,
	ST_L1_F3,
	ST_L1_F4,
	ST_L1_F5,
	ST_L1_F6,
	ST_L1_F7,
	ST_L1_F8,
};

#define L1S_STATE_COUNT (ST_L1_F8+1)

static char *strL1SState[] =
{
	"ST_L1_F2",
	"ST_L1_F3",
	"ST_L1_F4",
	"ST_L1_F5",
	"ST_L1_F6",
	"ST_L1_F7",
	"ST_L1_F8",
};

#ifdef HISAX_UINTERFACE
static
struct Fsm l1fsm_u =
{NULL, 0, 0, NULL, NULL};

enum {
	ST_L1_RESET,
	ST_L1_DEACT,
	ST_L1_SYNC2,
	ST_L1_TRANS,
};

#define L1U_STATE_COUNT (ST_L1_TRANS+1)

static char *strL1UState[] =
{
	"ST_L1_RESET",
	"ST_L1_DEACT",
	"ST_L1_SYNC2",
	"ST_L1_TRANS",
};
#endif

enum {
	ST_L1_NULL,
	ST_L1_WAIT_ACT,
	ST_L1_WAIT_DEACT,
	ST_L1_ACTIV,
};

#define L1B_STATE_COUNT (ST_L1_ACTIV+1)

static char *strL1BState[] =
{
	"ST_L1_NULL",
	"ST_L1_WAIT_ACT",
	"ST_L1_WAIT_DEACT",
	"ST_L1_ACTIV",
};

enum {
	EV_PH_ACTIVATE,
	EV_PH_DEACTIVATE,
	EV_RESET_IND,
	EV_DEACT_CNF,
	EV_DEACT_IND,
	EV_POWER_UP,
	EV_RSYNC_IND, 
	EV_INFO2_IND,
	EV_INFO4_IND,
	EV_TIMER_DEACT,
	EV_TIMER_ACT,
	EV_TIMER3,
};

#define L1_EVENT_COUNT (EV_TIMER3 + 1)

static char *strL1Event[] =
{
	"EV_PH_ACTIVATE",
	"EV_PH_DEACTIVATE",
	"EV_RESET_IND",
	"EV_DEACT_CNF",
	"EV_DEACT_IND",
	"EV_POWER_UP",
	"EV_RSYNC_IND", 
	"EV_INFO2_IND",
	"EV_INFO4_IND",
	"EV_TIMER_DEACT",
	"EV_TIMER_ACT",
	"EV_TIMER3",
};

void
debugl1(struct IsdnCardState *cs, char *fmt, ...)
{
	va_list args;
	char tmp[8];
	
	va_start(args, fmt);
	sprintf(tmp, "Card%d ", cs->cardnr + 1);
	VHiSax_putstatus(cs, tmp, fmt, args);
	va_end(args);
}

static void
l1m_debug(struct FsmInst *fi, char *fmt, ...)
{
	va_list args;
	struct PStack *st = fi->userdata;
	struct IsdnCardState *cs = st->l1.hardware;
	char tmp[8];
	
	va_start(args, fmt);
	sprintf(tmp, "Card%d ", cs->cardnr + 1);
	VHiSax_putstatus(cs, tmp, fmt, args);
	va_end(args);
}

static void
L1activated(struct IsdnCardState *cs)
{
	struct PStack *st;

	st = cs->stlist;
	while (st) {
		if (test_and_clear_bit(FLG_L1_ACTIVATING, &st->l1.Flags))
			st->l1.l1l2(st, PH_ACTIVATE | CONFIRM, NULL);
		else
			st->l1.l1l2(st, PH_ACTIVATE | INDICATION, NULL);
		st = st->next;
	}
}

static void
L1deactivated(struct IsdnCardState *cs)
{
	struct PStack *st;

	st = cs->stlist;
	while (st) {
		if (test_bit(FLG_L1_DBUSY, &cs->HW_Flags))
			st->l1.l1l2(st, PH_PAUSE | CONFIRM, NULL);
		st->l1.l1l2(st, PH_DEACTIVATE | INDICATION, NULL);
		st = st->next;
	}
	test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags);
}

void
DChannel_proc_xmt(struct IsdnCardState *cs)
{
	struct PStack *stptr;

	if (cs->tx_skb)
		return;

	stptr = cs->stlist;
	while (stptr != NULL) {
		if (test_and_clear_bit(FLG_L1_PULL_REQ, &stptr->l1.Flags)) {
			stptr->l1.l1l2(stptr, PH_PULL | CONFIRM, NULL);
			break;
		} else
			stptr = stptr->next;
	}
}

void
DChannel_proc_rcv(struct IsdnCardState *cs)
{
	struct sk_buff *skb, *nskb;
	struct PStack *stptr = cs->stlist;
	int found, tei, sapi;

	if (stptr)
		if (test_bit(FLG_L1_ACTTIMER, &stptr->l1.Flags))
			FsmEvent(&stptr->l1.l1m, EV_TIMER_ACT, NULL);	
	while ((skb = skb_dequeue(&cs->rq))) {
#ifdef L2FRAME_DEBUG		/* psa */
		if (cs->debug & L1_DEB_LAPD)
			Logl2Frame(cs, skb, "PH_DATA", 1);
#endif
		stptr = cs->stlist;
		if (skb->len<3) {
			debugl1(cs, "D-channel frame too short(%d)",skb->len);
			dev_kfree_skb(skb);
			return;
		}
		if ((skb->data[0] & 1) || !(skb->data[1] &1)) {
			debugl1(cs, "D-channel frame wrong EA0/EA1");
			dev_kfree_skb(skb);
			return;
		}
		sapi = skb->data[0] >> 2;
		tei = skb->data[1] >> 1;
		if (cs->debug & DEB_DLOG_HEX)
			LogFrame(cs, skb->data, skb->len);
		if (cs->debug & DEB_DLOG_VERBOSE)
			dlogframe(cs, skb, 1);
		if (tei == GROUP_TEI) {
			if (sapi == CTRL_SAPI) { /* sapi 0 */
				while (stptr != NULL) {
					if ((nskb = skb_clone(skb, GFP_ATOMIC)))
						stptr->l1.l1l2(stptr, PH_DATA | INDICATION, nskb);
					else
						printk(KERN_WARNING "HiSax: isdn broadcast buffer shortage\n");
					stptr = stptr->next;
				}
			} else if (sapi == TEI_SAPI) {
				while (stptr != NULL) {
					if ((nskb = skb_clone(skb, GFP_ATOMIC)))
						stptr->l1.l1tei(stptr, PH_DATA | INDICATION, nskb);
					else
						printk(KERN_WARNING "HiSax: tei broadcast buffer shortage\n");
					stptr = stptr->next;
				}
			}
			dev_kfree_skb(skb);
		} else if (sapi == CTRL_SAPI) { /* sapi 0 */
			found = 0;
			while (stptr != NULL)
				if (tei == stptr->l2.tei) {
					stptr->l1.l1l2(stptr, PH_DATA | INDICATION, skb);
					found = !0;
					break;
				} else
					stptr = stptr->next;
			if (!found)
				dev_kfree_skb(skb);
		} else
			dev_kfree_skb(skb);
	}
}

static void
BChannel_proc_xmt(struct BCState *bcs)
{
	struct PStack *st = bcs->st;

	if (test_bit(BC_FLG_BUSY, &bcs->Flag)) {
		debugl1(bcs->cs, "BC_BUSY Error");
		return;
	}

	if (test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags))
		st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
	if (!test_bit(BC_FLG_ACTIV, &bcs->Flag)) {
		if (!test_bit(BC_FLG_BUSY, &bcs->Flag) &&
		    skb_queue_empty(&bcs->squeue)) {
			st->l2.l2l1(st, PH_DEACTIVATE | CONFIRM, NULL);
		}
	}
}

static void
BChannel_proc_rcv(struct BCState *bcs)
{
	struct sk_buff *skb;

	if (bcs->st->l1.l1m.state == ST_L1_WAIT_ACT) {
		FsmDelTimer(&bcs->st->l1.timer, 4);
		FsmEvent(&bcs->st->l1.l1m, EV_TIMER_ACT, NULL);
	}
	while ((skb = skb_dequeue(&bcs->rqueue))) {
		bcs->st->l1.l1l2(bcs->st, PH_DATA | INDICATION, skb);
	}
}

static void
BChannel_proc_ack(struct BCState *bcs)
{
	u_long	flags;
	int	ack;

	spin_lock_irqsave(&bcs->aclock, flags);
	ack = bcs->ackcnt;
	bcs->ackcnt = 0;
	spin_unlock_irqrestore(&bcs->aclock, flags);
	if (ack)
		lli_writewakeup(bcs->st, ack);
}

void
BChannel_bh(struct work_struct *work)
{
	struct BCState *bcs = container_of(work, struct BCState, tqueue);

	if (!bcs)
		return;
	if (test_and_clear_bit(B_RCVBUFREADY, &bcs->event))
		BChannel_proc_rcv(bcs);
	if (test_and_clear_bit(B_XMTBUFREADY, &bcs->event))
		BChannel_proc_xmt(bcs);
	if (test_and_clear_bit(B_ACKPENDING, &bcs->event))
		BChannel_proc_ack(bcs);
}

void
HiSax_addlist(struct IsdnCardState *cs,
	      struct PStack *st)
{
	st->next = cs->stlist;
	cs->stlist = st;
}

void
HiSax_rmlist(struct IsdnCardState *cs,
	     struct PStack *st)
{
	struct PStack *p;

	FsmDelTimer(&st->l1.timer, 0);
	if (cs->stlist == st)
		cs->stlist = st->next;
	else {
		p = cs->stlist;
		while (p)
			if (p->next == st) {
				p->next = st->next;
				return;
			} else
				p = p->next;
	}
}

void
init_bcstate(struct IsdnCardState *cs, int bc)
{
	struct BCState *bcs = cs->bcs + bc;

	bcs->cs = cs;
	bcs->channel = bc;
	INIT_WORK(&bcs->tqueue, BChannel_bh);
	spin_lock_init(&bcs->aclock);
	bcs->BC_SetStack = NULL;
	bcs->BC_Close = NULL;
	bcs->Flag = 0;
}

#ifdef L2FRAME_DEBUG		/* psa */

static char *
l2cmd(u_char cmd)
{
	switch (cmd & ~0x10) {
		case 1:
			return "RR";
		case 5:
			return "RNR";
		case 9:
			return "REJ";
		case 0x6f:
			return "SABME";
		case 0x0f:
			return "DM";
		case 3:
			return "UI";
		case 0x43:
			return "DISC";
		case 0x63:
			return "UA";
		case 0x87:
			return "FRMR";
		case 0xaf:
			return "XID";
		default:
			if (!(cmd & 1))
				return "I";
			else
				return "invalid command";
	}
}

static char tmpdeb[32];

static char *
l2frames(u_char * ptr)
{
	switch (ptr[2] & ~0x10) {
		case 1:
		case 5:
		case 9:
			sprintf(tmpdeb, "%s[%d](nr %d)", l2cmd(ptr[2]), ptr[3] & 1, ptr[3] >> 1);
			break;
		case 0x6f:
		case 0x0f:
		case 3:
		case 0x43:
		case 0x63:
		case 0x87:
		case 0xaf:
			sprintf(tmpdeb, "%s[%d]", l2cmd(ptr[2]), (ptr[2] & 0x10) >> 4);
			break;
		default:
			if (!(ptr[2] & 1)) {
				sprintf(tmpdeb, "I[%d](ns %d, nr %d)", ptr[3] & 1, ptr[2] >> 1, ptr[3] >> 1);
				break;
			} else
				return "invalid command";
	}


	return tmpdeb;
}

void
Logl2Frame(struct IsdnCardState *cs, struct sk_buff *skb, char *buf, int dir)
{
	u_char *ptr;

	ptr = skb->data;

	if (ptr[0] & 1 || !(ptr[1] & 1))
		debugl1(cs, "Address not LAPD");
	else
		debugl1(cs, "%s %s: %s%c (sapi %d, tei %d)",
			(dir ? "<-" : "->"), buf, l2frames(ptr),
			((ptr[0] & 2) >> 1) == dir ? 'C' : 'R', ptr[0] >> 2, ptr[1] >> 1);
}
#endif

static void
l1_reset(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_F3);
}

static void
l1_deact_cnf(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L1_F3);
	if (test_bit(FLG_L1_ACTIVATING, &st->l1.Flags))
		st->l1.l1hw(st, HW_ENABLE | REQUEST, NULL);
}

static void
l1_deact_req_s(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L1_F3);
	FsmRestartTimer(&st->l1.timer, 550, EV_TIMER_DEACT, NULL, 2);
	test_and_set_bit(FLG_L1_DEACTTIMER, &st->l1.Flags);
}

static void
l1_power_up_s(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (test_bit(FLG_L1_ACTIVATING, &st->l1.Flags)) {
		FsmChangeState(fi, ST_L1_F4);
		st->l1.l1hw(st, HW_INFO3 | REQUEST, NULL);
		FsmRestartTimer(&st->l1.timer, TIMER3_VALUE, EV_TIMER3, NULL, 2);
		test_and_set_bit(FLG_L1_T3RUN, &st->l1.Flags);
	} else
		FsmChangeState(fi, ST_L1_F3);
}

static void
l1_go_F5(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_F5);
}

static void
l1_go_F8(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_F8);
}

static void
l1_info2_ind(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

#ifdef HISAX_UINTERFACE
	if (test_bit(FLG_L1_UINT, &st->l1.Flags))
		FsmChangeState(fi, ST_L1_SYNC2);
	else
#endif
		FsmChangeState(fi, ST_L1_F6);
	st->l1.l1hw(st, HW_INFO3 | REQUEST, NULL);
}

static void
l1_info4_ind(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

#ifdef HISAX_UINTERFACE
	if (test_bit(FLG_L1_UINT, &st->l1.Flags))
		FsmChangeState(fi, ST_L1_TRANS);
	else
#endif
		FsmChangeState(fi, ST_L1_F7);
	st->l1.l1hw(st, HW_INFO3 | REQUEST, NULL);
	if (test_and_clear_bit(FLG_L1_DEACTTIMER, &st->l1.Flags))
		FsmDelTimer(&st->l1.timer, 4);
	if (!test_bit(FLG_L1_ACTIVATED, &st->l1.Flags)) {
		if (test_and_clear_bit(FLG_L1_T3RUN, &st->l1.Flags))
			FsmDelTimer(&st->l1.timer, 3);
		FsmRestartTimer(&st->l1.timer, 110, EV_TIMER_ACT, NULL, 2);
		test_and_set_bit(FLG_L1_ACTTIMER, &st->l1.Flags);
	}
}

static void
l1_timer3(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	test_and_clear_bit(FLG_L1_T3RUN, &st->l1.Flags);	
	if (test_and_clear_bit(FLG_L1_ACTIVATING, &st->l1.Flags))
		L1deactivated(st->l1.hardware);

#ifdef HISAX_UINTERFACE
	if (!test_bit(FLG_L1_UINT, &st->l1.Flags))
#endif
	if (st->l1.l1m.state != ST_L1_F6) {
		FsmChangeState(fi, ST_L1_F3);
		st->l1.l1hw(st, HW_ENABLE | REQUEST, NULL);
	}
}

static void
l1_timer_act(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	
	test_and_clear_bit(FLG_L1_ACTTIMER, &st->l1.Flags);
	test_and_set_bit(FLG_L1_ACTIVATED, &st->l1.Flags);
	L1activated(st->l1.hardware);
}

static void
l1_timer_deact(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	
	test_and_clear_bit(FLG_L1_DEACTTIMER, &st->l1.Flags);
	test_and_clear_bit(FLG_L1_ACTIVATED, &st->l1.Flags);
	L1deactivated(st->l1.hardware);
	st->l1.l1hw(st, HW_DEACTIVATE | RESPONSE, NULL);
}

static void
l1_activate_s(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
                
	st->l1.l1hw(st, HW_RESET | REQUEST, NULL);
}

static void
l1_activate_no(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if ((!test_bit(FLG_L1_DEACTTIMER, &st->l1.Flags)) && (!test_bit(FLG_L1_T3RUN, &st->l1.Flags))) {
		test_and_clear_bit(FLG_L1_ACTIVATING, &st->l1.Flags);
		L1deactivated(st->l1.hardware);
	}
}

static struct FsmNode L1SFnList[] __initdata =
{
	{ST_L1_F3, EV_PH_ACTIVATE, l1_activate_s},
	{ST_L1_F6, EV_PH_ACTIVATE, l1_activate_no},
	{ST_L1_F8, EV_PH_ACTIVATE, l1_activate_no},
	{ST_L1_F3, EV_RESET_IND, l1_reset},
	{ST_L1_F4, EV_RESET_IND, l1_reset},
	{ST_L1_F5, EV_RESET_IND, l1_reset},
	{ST_L1_F6, EV_RESET_IND, l1_reset},
	{ST_L1_F7, EV_RESET_IND, l1_reset},
	{ST_L1_F8, EV_RESET_IND, l1_reset},
	{ST_L1_F3, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F4, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F5, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F6, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F7, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F8, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F6, EV_DEACT_IND, l1_deact_req_s},
	{ST_L1_F7, EV_DEACT_IND, l1_deact_req_s},
	{ST_L1_F8, EV_DEACT_IND, l1_deact_req_s},
	{ST_L1_F3, EV_POWER_UP, l1_power_up_s},
	{ST_L1_F4, EV_RSYNC_IND, l1_go_F5},
	{ST_L1_F6, EV_RSYNC_IND, l1_go_F8},
	{ST_L1_F7, EV_RSYNC_IND, l1_go_F8},
	{ST_L1_F3, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_F4, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_F5, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_F7, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_F8, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_F3, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_F4, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_F5, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_F6, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_F8, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_F3, EV_TIMER3, l1_timer3},
	{ST_L1_F4, EV_TIMER3, l1_timer3},
	{ST_L1_F5, EV_TIMER3, l1_timer3},
	{ST_L1_F6, EV_TIMER3, l1_timer3},
	{ST_L1_F8, EV_TIMER3, l1_timer3},
	{ST_L1_F7, EV_TIMER_ACT, l1_timer_act},
	{ST_L1_F3, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_F4, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_F5, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_F6, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_F7, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_F8, EV_TIMER_DEACT, l1_timer_deact},
};

#define L1S_FN_COUNT (sizeof(L1SFnList)/sizeof(struct FsmNode))

#ifdef HISAX_UINTERFACE
static void
l1_deact_req_u(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L1_RESET);
	FsmRestartTimer(&st->l1.timer, 550, EV_TIMER_DEACT, NULL, 2);
	test_and_set_bit(FLG_L1_DEACTTIMER, &st->l1.Flags);
	st->l1.l1hw(st, HW_ENABLE | REQUEST, NULL);
}

static void
l1_power_up_u(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmRestartTimer(&st->l1.timer, TIMER3_VALUE, EV_TIMER3, NULL, 2);
	test_and_set_bit(FLG_L1_T3RUN, &st->l1.Flags);
}

static void
l1_info0_ind(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_DEACT);
}

static void
l1_activate_u(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
                
	st->l1.l1hw(st, HW_INFO1 | REQUEST, NULL);
}

static struct FsmNode L1UFnList[] __initdata =
{
	{ST_L1_RESET, EV_DEACT_IND, l1_deact_req_u},
	{ST_L1_DEACT, EV_DEACT_IND, l1_deact_req_u},
	{ST_L1_SYNC2, EV_DEACT_IND, l1_deact_req_u},
	{ST_L1_TRANS, EV_DEACT_IND, l1_deact_req_u},
	{ST_L1_DEACT, EV_PH_ACTIVATE, l1_activate_u},
	{ST_L1_DEACT, EV_POWER_UP, l1_power_up_u},
	{ST_L1_DEACT, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_TRANS, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_RESET, EV_DEACT_CNF, l1_info0_ind},
	{ST_L1_DEACT, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_SYNC2, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_RESET, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_DEACT, EV_TIMER3, l1_timer3},
	{ST_L1_SYNC2, EV_TIMER3, l1_timer3},
	{ST_L1_TRANS, EV_TIMER_ACT, l1_timer_act},
	{ST_L1_DEACT, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_SYNC2, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_RESET, EV_TIMER_DEACT, l1_timer_deact},
};

#define L1U_FN_COUNT (sizeof(L1UFnList)/sizeof(struct FsmNode))

#endif

static void
l1b_activate(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L1_WAIT_ACT);
	FsmRestartTimer(&st->l1.timer, st->l1.delay, EV_TIMER_ACT, NULL, 2);
}

static void
l1b_deactivate(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L1_WAIT_DEACT);
	FsmRestartTimer(&st->l1.timer, 10, EV_TIMER_DEACT, NULL, 2);
}

static void
l1b_timer_act(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L1_ACTIV);
	st->l1.l1l2(st, PH_ACTIVATE | CONFIRM, NULL);
}

static void
l1b_timer_deact(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L1_NULL);
	st->l2.l2l1(st, PH_DEACTIVATE | CONFIRM, NULL);
}

static struct FsmNode L1BFnList[] __initdata =
{
	{ST_L1_NULL, EV_PH_ACTIVATE, l1b_activate},
	{ST_L1_WAIT_ACT, EV_TIMER_ACT, l1b_timer_act},
	{ST_L1_ACTIV, EV_PH_DEACTIVATE, l1b_deactivate},
	{ST_L1_WAIT_DEACT, EV_TIMER_DEACT, l1b_timer_deact},
};

#define L1B_FN_COUNT (sizeof(L1BFnList)/sizeof(struct FsmNode))

int __init 
Isdnl1New(void)
{
	int retval;

	l1fsm_s.state_count = L1S_STATE_COUNT;
	l1fsm_s.event_count = L1_EVENT_COUNT;
	l1fsm_s.strEvent = strL1Event;
	l1fsm_s.strState = strL1SState;
	retval = FsmNew(&l1fsm_s, L1SFnList, L1S_FN_COUNT);
	if (retval)
		return retval;

	l1fsm_b.state_count = L1B_STATE_COUNT;
	l1fsm_b.event_count = L1_EVENT_COUNT;
	l1fsm_b.strEvent = strL1Event;
	l1fsm_b.strState = strL1BState;
	retval = FsmNew(&l1fsm_b, L1BFnList, L1B_FN_COUNT);
	if (retval) {
		FsmFree(&l1fsm_s);
		return retval;
	}
#ifdef HISAX_UINTERFACE
	l1fsm_u.state_count = L1U_STATE_COUNT;
	l1fsm_u.event_count = L1_EVENT_COUNT;
	l1fsm_u.strEvent = strL1Event;
	l1fsm_u.strState = strL1UState;
	retval = FsmNew(&l1fsm_u, L1UFnList, L1U_FN_COUNT);
	if (retval) {
		FsmFree(&l1fsm_s);
		FsmFree(&l1fsm_b);
		return retval;
	}
#endif
	return 0;
}

void Isdnl1Free(void)
{
#ifdef HISAX_UINTERFACE
	FsmFree(&l1fsm_u);
#endif
	FsmFree(&l1fsm_s);
	FsmFree(&l1fsm_b);
}

static void
dch_l2l1(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;

	switch (pr) {
		case (PH_DATA | REQUEST):
		case (PH_PULL | REQUEST):
		case (PH_PULL |INDICATION):
			st->l1.l1hw(st, pr, arg);
			break;
		case (PH_ACTIVATE | REQUEST):
			if (cs->debug)
				debugl1(cs, "PH_ACTIVATE_REQ %s",
					st->l1.l1m.fsm->strState[st->l1.l1m.state]);
			if (test_bit(FLG_L1_ACTIVATED, &st->l1.Flags))
				st->l1.l1l2(st, PH_ACTIVATE | CONFIRM, NULL);
			else {
				test_and_set_bit(FLG_L1_ACTIVATING, &st->l1.Flags);
				FsmEvent(&st->l1.l1m, EV_PH_ACTIVATE, arg);
			}
			break;
		case (PH_TESTLOOP | REQUEST):
			if (1 & (long) arg)
				debugl1(cs, "PH_TEST_LOOP B1");
			if (2 & (long) arg)
				debugl1(cs, "PH_TEST_LOOP B2");
			if (!(3 & (long) arg))
				debugl1(cs, "PH_TEST_LOOP DISABLED");
			st->l1.l1hw(st, HW_TESTLOOP | REQUEST, arg);
			break;
		default:
			if (cs->debug)
				debugl1(cs, "dch_l2l1 msg %04X unhandled", pr);
			break;
	}
}

void
l1_msg(struct IsdnCardState *cs, int pr, void *arg) {
	struct PStack *st;

	st = cs->stlist;
	
	while (st) {
		switch(pr) {
			case (HW_RESET | INDICATION):
				FsmEvent(&st->l1.l1m, EV_RESET_IND, arg);
				break;
			case (HW_DEACTIVATE | CONFIRM):
				FsmEvent(&st->l1.l1m, EV_DEACT_CNF, arg);
				break;
			case (HW_DEACTIVATE | INDICATION):
				FsmEvent(&st->l1.l1m, EV_DEACT_IND, arg);
				break;
			case (HW_POWERUP | CONFIRM):
				FsmEvent(&st->l1.l1m, EV_POWER_UP, arg);
				break;
			case (HW_RSYNC | INDICATION):
				FsmEvent(&st->l1.l1m, EV_RSYNC_IND, arg);
				break;
			case (HW_INFO2 | INDICATION):
				FsmEvent(&st->l1.l1m, EV_INFO2_IND, arg);
				break;
			case (HW_INFO4_P8 | INDICATION):
			case (HW_INFO4_P10 | INDICATION):
				FsmEvent(&st->l1.l1m, EV_INFO4_IND, arg);
				break;
			default:
				if (cs->debug)
					debugl1(cs, "l1msg %04X unhandled", pr);
				break;
		}
		st = st->next;
	}
}

void
l1_msg_b(struct PStack *st, int pr, void *arg) {
	switch(pr) {
		case (PH_ACTIVATE | REQUEST):
			FsmEvent(&st->l1.l1m, EV_PH_ACTIVATE, NULL);
			break;
		case (PH_DEACTIVATE | REQUEST):
			FsmEvent(&st->l1.l1m, EV_PH_DEACTIVATE, NULL);
			break;
	}
}

void
setstack_HiSax(struct PStack *st, struct IsdnCardState *cs)
{
	st->l1.hardware = cs;
	st->protocol = cs->protocol;
	st->l1.l1m.fsm = &l1fsm_s;
	st->l1.l1m.state = ST_L1_F3;
	st->l1.Flags = 0;
#ifdef HISAX_UINTERFACE
	if (test_bit(FLG_HW_L1_UINT, &cs->HW_Flags)) {
		st->l1.l1m.fsm = &l1fsm_u;
		st->l1.l1m.state = ST_L1_RESET;
		st->l1.Flags = FLG_L1_UINT;
	}
#endif
	st->l1.l1m.debug = cs->debug;
	st->l1.l1m.userdata = st;
	st->l1.l1m.userint = 0;
	st->l1.l1m.printdebug = l1m_debug;
	FsmInitTimer(&st->l1.l1m, &st->l1.timer);
	setstack_tei(st);
	setstack_manager(st);
	st->l1.stlistp = &(cs->stlist);
	st->l2.l2l1  = dch_l2l1;
	if (cs->setstack_d)
		cs->setstack_d(st, cs);
}

void
setstack_l1_B(struct PStack *st)
{
	struct IsdnCardState *cs = st->l1.hardware;

	st->l1.l1m.fsm = &l1fsm_b;
	st->l1.l1m.state = ST_L1_NULL;
	st->l1.l1m.debug = cs->debug;
	st->l1.l1m.userdata = st;
	st->l1.l1m.userint = 0;
	st->l1.l1m.printdebug = l1m_debug;
	st->l1.Flags = 0;
	FsmInitTimer(&st->l1.l1m, &st->l1.timer);
}
