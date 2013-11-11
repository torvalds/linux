/* $Id: isdnl3.c,v 2.22.2.3 2004/01/13 14:31:25 keil Exp $
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
 *
 */

#include <linux/init.h>
#include <linux/slab.h>
#include "hisax.h"
#include "isdnl3.h"

const char *l3_revision = "$Revision: 2.22.2.3 $";

static struct Fsm l3fsm;

enum {
	ST_L3_LC_REL,
	ST_L3_LC_ESTAB_WAIT,
	ST_L3_LC_REL_DELAY,
	ST_L3_LC_REL_WAIT,
	ST_L3_LC_ESTAB,
};

#define L3_STATE_COUNT (ST_L3_LC_ESTAB + 1)

static char *strL3State[] =
{
	"ST_L3_LC_REL",
	"ST_L3_LC_ESTAB_WAIT",
	"ST_L3_LC_REL_DELAY",
	"ST_L3_LC_REL_WAIT",
	"ST_L3_LC_ESTAB",
};

enum {
	EV_ESTABLISH_REQ,
	EV_ESTABLISH_IND,
	EV_ESTABLISH_CNF,
	EV_RELEASE_REQ,
	EV_RELEASE_CNF,
	EV_RELEASE_IND,
	EV_TIMEOUT,
};

#define L3_EVENT_COUNT (EV_TIMEOUT + 1)

static char *strL3Event[] =
{
	"EV_ESTABLISH_REQ",
	"EV_ESTABLISH_IND",
	"EV_ESTABLISH_CNF",
	"EV_RELEASE_REQ",
	"EV_RELEASE_CNF",
	"EV_RELEASE_IND",
	"EV_TIMEOUT",
};

static __printf(2, 3) void
	l3m_debug(struct FsmInst *fi, char *fmt, ...)
{
	va_list args;
	struct PStack *st = fi->userdata;

	va_start(args, fmt);
	VHiSax_putstatus(st->l1.hardware, st->l3.debug_id, fmt, args);
	va_end(args);
}

u_char *
findie(u_char *p, int size, u_char ie, int wanted_set)
{
	int l, codeset, maincodeset;
	u_char *pend = p + size;

	/* skip protocol discriminator, callref and message type */
	p++;
	l = (*p++) & 0xf;
	p += l;
	p++;
	codeset = 0;
	maincodeset = 0;
	/* while there are bytes left... */
	while (p < pend) {
		if ((*p & 0xf0) == 0x90) {
			codeset = *p & 0x07;
			if (!(*p & 0x08))
				maincodeset = codeset;
		}
		if (*p & 0x80)
			p++;
		else {
			if (codeset == wanted_set) {
				if (*p == ie)
				{ /* improved length check (Werner Cornelius) */
					if ((pend - p) < 2)
						return (NULL);
					if (*(p + 1) > (pend - (p + 2)))
						return (NULL);
					return (p);
				}

				if (*p > ie)
					return (NULL);
			}
			p++;
			l = *p++;
			p += l;
			codeset = maincodeset;
		}
	}
	return (NULL);
}

int
getcallref(u_char *p)
{
	int l, cr = 0;

	p++;			/* prot discr */
	if (*p & 0xfe)		/* wrong callref BRI only 1 octet*/
		return (-2);
	l = 0xf & *p++;		/* callref length */
	if (!l)			/* dummy CallRef */
		return (-1);
	cr = *p++;
	return (cr);
}

static int OrigCallRef = 0;

int
newcallref(void)
{
	if (OrigCallRef == 127)
		OrigCallRef = 1;
	else
		OrigCallRef++;
	return (OrigCallRef);
}

void
newl3state(struct l3_process *pc, int state)
{
	if (pc->debug & L3_DEB_STATE)
		l3_debug(pc->st, "newstate cr %d %d --> %d",
			 pc->callref & 0x7F,
			 pc->state, state);
	pc->state = state;
}

static void
L3ExpireTimer(struct L3Timer *t)
{
	t->pc->st->lli.l4l3(t->pc->st, t->event, t->pc);
}

void
L3InitTimer(struct l3_process *pc, struct L3Timer *t)
{
	t->pc = pc;
	t->tl.function = (void *) L3ExpireTimer;
	t->tl.data = (long) t;
	init_timer(&t->tl);
}

void
L3DelTimer(struct L3Timer *t)
{
	del_timer(&t->tl);
}

int
L3AddTimer(struct L3Timer *t,
	   int millisec, int event)
{
	if (timer_pending(&t->tl)) {
		printk(KERN_WARNING "L3AddTimer: timer already active!\n");
		return -1;
	}
	init_timer(&t->tl);
	t->event = event;
	t->tl.expires = jiffies + (millisec * HZ) / 1000;
	add_timer(&t->tl);
	return 0;
}

void
StopAllL3Timer(struct l3_process *pc)
{
	L3DelTimer(&pc->timer);
}

struct sk_buff *
l3_alloc_skb(int len)
{
	struct sk_buff *skb;

	if (!(skb = alloc_skb(len + MAX_HEADER_LEN, GFP_ATOMIC))) {
		printk(KERN_WARNING "HiSax: No skb for D-channel\n");
		return (NULL);
	}
	skb_reserve(skb, MAX_HEADER_LEN);
	return (skb);
}

static void
no_l3_proto(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;

	HiSax_putstatus(st->l1.hardware, "L3", "no D protocol");
	if (skb) {
		dev_kfree_skb(skb);
	}
}

static int
no_l3_proto_spec(struct PStack *st, isdn_ctrl *ic)
{
	printk(KERN_WARNING "HiSax: no specific protocol handler for proto %lu\n", ic->arg & 0xFF);
	return (-1);
}

struct l3_process
*getl3proc(struct PStack *st, int cr)
{
	struct l3_process *p = st->l3.proc;

	while (p)
		if (p->callref == cr)
			return (p);
		else
			p = p->next;
	return (NULL);
}

struct l3_process
*new_l3_process(struct PStack *st, int cr)
{
	struct l3_process *p, *np;

	if (!(p = kmalloc(sizeof(struct l3_process), GFP_ATOMIC))) {
		printk(KERN_ERR "HiSax can't get memory for cr %d\n", cr);
		return (NULL);
	}
	if (!st->l3.proc)
		st->l3.proc = p;
	else {
		np = st->l3.proc;
		while (np->next)
			np = np->next;
		np->next = p;
	}
	p->next = NULL;
	p->debug = st->l3.debug;
	p->callref = cr;
	p->state = 0;
	p->chan = NULL;
	p->st = st;
	p->N303 = st->l3.N303;
	L3InitTimer(p, &p->timer);
	return (p);
};

void
release_l3_process(struct l3_process *p)
{
	struct l3_process *np, *pp = NULL;

	if (!p)
		return;
	np = p->st->l3.proc;
	while (np) {
		if (np == p) {
			StopAllL3Timer(p);
			if (pp)
				pp->next = np->next;
			else if (!(p->st->l3.proc = np->next) &&
				 !test_bit(FLG_PTP, &p->st->l2.flag)) {
				if (p->debug)
					l3_debug(p->st, "release_l3_process: last process");
				if (skb_queue_empty(&p->st->l3.squeue)) {
					if (p->debug)
						l3_debug(p->st, "release_l3_process: release link");
					if (p->st->protocol != ISDN_PTYPE_NI1)
						FsmEvent(&p->st->l3.l3m, EV_RELEASE_REQ, NULL);
					else
						FsmEvent(&p->st->l3.l3m, EV_RELEASE_IND, NULL);
				} else {
					if (p->debug)
						l3_debug(p->st, "release_l3_process: not release link");
				}
			}
			kfree(p);
			return;
		}
		pp = np;
		np = np->next;
	}
	printk(KERN_ERR "HiSax internal L3 error CR(%d) not in list\n", p->callref);
	l3_debug(p->st, "HiSax internal L3 error CR(%d) not in list", p->callref);
};

static void
l3ml3p(struct PStack *st, int pr)
{
	struct l3_process *p = st->l3.proc;
	struct l3_process *np;

	while (p) {
		/* p might be kfreed under us, so we need to save where we want to go on */
		np = p->next;
		st->l3.l3ml3(st, pr, p);
		p = np;
	}
}

void
setstack_l3dc(struct PStack *st, struct Channel *chanp)
{
	char tmp[64];

	st->l3.proc   = NULL;
	st->l3.global = NULL;
	skb_queue_head_init(&st->l3.squeue);
	st->l3.l3m.fsm = &l3fsm;
	st->l3.l3m.state = ST_L3_LC_REL;
	st->l3.l3m.debug = 1;
	st->l3.l3m.userdata = st;
	st->l3.l3m.userint = 0;
	st->l3.l3m.printdebug = l3m_debug;
	FsmInitTimer(&st->l3.l3m, &st->l3.l3m_timer);
	strcpy(st->l3.debug_id, "L3DC ");
	st->lli.l4l3_proto = no_l3_proto_spec;

#ifdef CONFIG_HISAX_EURO
	if (st->protocol == ISDN_PTYPE_EURO) {
		setstack_dss1(st);
	} else
#endif
#ifdef CONFIG_HISAX_NI1
		if (st->protocol == ISDN_PTYPE_NI1) {
			setstack_ni1(st);
		} else
#endif
#ifdef CONFIG_HISAX_1TR6
			if (st->protocol == ISDN_PTYPE_1TR6) {
				setstack_1tr6(st);
			} else
#endif
				if (st->protocol == ISDN_PTYPE_LEASED) {
					st->lli.l4l3 = no_l3_proto;
					st->l2.l2l3 = no_l3_proto;
					st->l3.l3ml3 = no_l3_proto;
					printk(KERN_INFO "HiSax: Leased line mode\n");
				} else {
					st->lli.l4l3 = no_l3_proto;
					st->l2.l2l3 = no_l3_proto;
					st->l3.l3ml3 = no_l3_proto;
					sprintf(tmp, "protocol %s not supported",
						(st->protocol == ISDN_PTYPE_1TR6) ? "1tr6" :
						(st->protocol == ISDN_PTYPE_EURO) ? "euro" :
						(st->protocol == ISDN_PTYPE_NI1) ? "ni1" :
						"unknown");
					printk(KERN_WARNING "HiSax: %s\n", tmp);
					st->protocol = -1;
				}
}

static void
isdnl3_trans(struct PStack *st, int pr, void *arg) {
	st->l3.l3l2(st, pr, arg);
}

void
releasestack_isdnl3(struct PStack *st)
{
	while (st->l3.proc)
		release_l3_process(st->l3.proc);
	if (st->l3.global) {
		StopAllL3Timer(st->l3.global);
		kfree(st->l3.global);
		st->l3.global = NULL;
	}
	FsmDelTimer(&st->l3.l3m_timer, 54);
	skb_queue_purge(&st->l3.squeue);
}

void
setstack_l3bc(struct PStack *st, struct Channel *chanp)
{

	st->l3.proc   = NULL;
	st->l3.global = NULL;
	skb_queue_head_init(&st->l3.squeue);
	st->l3.l3m.fsm = &l3fsm;
	st->l3.l3m.state = ST_L3_LC_REL;
	st->l3.l3m.debug = 1;
	st->l3.l3m.userdata = st;
	st->l3.l3m.userint = 0;
	st->l3.l3m.printdebug = l3m_debug;
	strcpy(st->l3.debug_id, "L3BC ");
	st->lli.l4l3 = isdnl3_trans;
}

#define DREL_TIMER_VALUE 40000

static void
lc_activate(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L3_LC_ESTAB_WAIT);
	st->l3.l3l2(st, DL_ESTABLISH | REQUEST, NULL);
}

static void
lc_connect(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int dequeued = 0;

	FsmChangeState(fi, ST_L3_LC_ESTAB);
	while ((skb = skb_dequeue(&st->l3.squeue))) {
		st->l3.l3l2(st, DL_DATA | REQUEST, skb);
		dequeued++;
	}
	if ((!st->l3.proc) &&  dequeued) {
		if (st->l3.debug)
			l3_debug(st, "lc_connect: release link");
		FsmEvent(&st->l3.l3m, EV_RELEASE_REQ, NULL);
	} else
		l3ml3p(st, DL_ESTABLISH | INDICATION);
}

static void
lc_connected(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int dequeued = 0;

	FsmDelTimer(&st->l3.l3m_timer, 51);
	FsmChangeState(fi, ST_L3_LC_ESTAB);
	while ((skb = skb_dequeue(&st->l3.squeue))) {
		st->l3.l3l2(st, DL_DATA | REQUEST, skb);
		dequeued++;
	}
	if ((!st->l3.proc) &&  dequeued) {
		if (st->l3.debug)
			l3_debug(st, "lc_connected: release link");
		FsmEvent(&st->l3.l3m, EV_RELEASE_REQ, NULL);
	} else
		l3ml3p(st, DL_ESTABLISH | CONFIRM);
}

static void
lc_start_delay(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L3_LC_REL_DELAY);
	FsmAddTimer(&st->l3.l3m_timer, DREL_TIMER_VALUE, EV_TIMEOUT, NULL, 50);
}

static void
lc_start_delay_check(struct FsmInst *fi, int event, void *arg)
/* 20/09/00 - GE timer not user for NI-1 as layer 2 should stay up */
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L3_LC_REL_DELAY);
	/* 19/09/00 - GE timer not user for NI-1 */
	if (st->protocol != ISDN_PTYPE_NI1)
		FsmAddTimer(&st->l3.l3m_timer, DREL_TIMER_VALUE, EV_TIMEOUT, NULL, 50);
}

static void
lc_release_req(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (test_bit(FLG_L2BLOCK, &st->l2.flag)) {
		if (st->l3.debug)
			l3_debug(st, "lc_release_req: l2 blocked");
		/* restart release timer */
		FsmAddTimer(&st->l3.l3m_timer, DREL_TIMER_VALUE, EV_TIMEOUT, NULL, 51);
	} else {
		FsmChangeState(fi, ST_L3_LC_REL_WAIT);
		st->l3.l3l2(st, DL_RELEASE | REQUEST, NULL);
	}
}

static void
lc_release_ind(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmDelTimer(&st->l3.l3m_timer, 52);
	FsmChangeState(fi, ST_L3_LC_REL);
	skb_queue_purge(&st->l3.squeue);
	l3ml3p(st, DL_RELEASE | INDICATION);
}

static void
lc_release_cnf(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L3_LC_REL);
	skb_queue_purge(&st->l3.squeue);
	l3ml3p(st, DL_RELEASE | CONFIRM);
}


/* *INDENT-OFF* */
static struct FsmNode L3FnList[] __initdata =
{
	{ST_L3_LC_REL,		EV_ESTABLISH_REQ,	lc_activate},
	{ST_L3_LC_REL,		EV_ESTABLISH_IND,	lc_connect},
	{ST_L3_LC_REL,		EV_ESTABLISH_CNF,	lc_connect},
	{ST_L3_LC_ESTAB_WAIT,	EV_ESTABLISH_CNF,	lc_connected},
	{ST_L3_LC_ESTAB_WAIT,	EV_RELEASE_REQ,		lc_start_delay},
	{ST_L3_LC_ESTAB_WAIT,	EV_RELEASE_IND,		lc_release_ind},
	{ST_L3_LC_ESTAB,	EV_RELEASE_IND,		lc_release_ind},
	{ST_L3_LC_ESTAB,	EV_RELEASE_REQ,		lc_start_delay_check},
	{ST_L3_LC_REL_DELAY,    EV_RELEASE_IND,         lc_release_ind},
	{ST_L3_LC_REL_DELAY,    EV_ESTABLISH_REQ,       lc_connected},
	{ST_L3_LC_REL_DELAY,    EV_TIMEOUT,             lc_release_req},
	{ST_L3_LC_REL_WAIT,	EV_RELEASE_CNF,		lc_release_cnf},
	{ST_L3_LC_REL_WAIT,	EV_ESTABLISH_REQ,	lc_activate},
};
/* *INDENT-ON* */

void
l3_msg(struct PStack *st, int pr, void *arg)
{
	switch (pr) {
	case (DL_DATA | REQUEST):
		if (st->l3.l3m.state == ST_L3_LC_ESTAB) {
			st->l3.l3l2(st, pr, arg);
		} else {
			struct sk_buff *skb = arg;

			skb_queue_tail(&st->l3.squeue, skb);
			FsmEvent(&st->l3.l3m, EV_ESTABLISH_REQ, NULL);
		}
		break;
	case (DL_ESTABLISH | REQUEST):
		FsmEvent(&st->l3.l3m, EV_ESTABLISH_REQ, NULL);
		break;
	case (DL_ESTABLISH | CONFIRM):
		FsmEvent(&st->l3.l3m, EV_ESTABLISH_CNF, NULL);
		break;
	case (DL_ESTABLISH | INDICATION):
		FsmEvent(&st->l3.l3m, EV_ESTABLISH_IND, NULL);
		break;
	case (DL_RELEASE | INDICATION):
		FsmEvent(&st->l3.l3m, EV_RELEASE_IND, NULL);
		break;
	case (DL_RELEASE | CONFIRM):
		FsmEvent(&st->l3.l3m, EV_RELEASE_CNF, NULL);
		break;
	case (DL_RELEASE | REQUEST):
		FsmEvent(&st->l3.l3m, EV_RELEASE_REQ, NULL);
		break;
	}
}

int __init
Isdnl3New(void)
{
	l3fsm.state_count = L3_STATE_COUNT;
	l3fsm.event_count = L3_EVENT_COUNT;
	l3fsm.strEvent = strL3Event;
	l3fsm.strState = strL3State;
	return FsmNew(&l3fsm, L3FnList, ARRAY_SIZE(L3FnList));
}

void
Isdnl3Free(void)
{
	FsmFree(&l3fsm);
}
