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


#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mISDNhw.h>
#include "core.h"
#include "layer1.h"
#include "fsm.h"

static u_int *debug;

struct layer1 {
	u_long			Flags;
	struct FsmInst		l1m;
	struct FsmTimer 	timer;
	int			delay;
	struct dchannel		*dch;
	dchannel_l1callback	*dcb;
};

#define TIMER3_VALUE 7000

static
struct Fsm l1fsm_s = {NULL, 0, 0, NULL, NULL};

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

enum {
	EV_PH_ACTIVATE,
	EV_PH_DEACTIVATE,
	EV_RESET_IND,
	EV_DEACT_CNF,
	EV_DEACT_IND,
	EV_POWER_UP,
	EV_ANYSIG_IND,
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
	"EV_ANYSIG_IND",
	"EV_INFO2_IND",
	"EV_INFO4_IND",
	"EV_TIMER_DEACT",
	"EV_TIMER_ACT",
	"EV_TIMER3",
};

static void
l1m_debug(struct FsmInst *fi, char *fmt, ...)
{
	struct layer1 *l1 = fi->userdata;
	struct va_format vaf;
	va_list va;

	va_start(va, fmt);

	vaf.fmt = fmt;
	vaf.va = &va;

	printk(KERN_DEBUG "%s: %pV\n", dev_name(&l1->dch->dev.dev), &vaf);

	va_end(va);
}

static void
l1_reset(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_L1_F3);
}

static void
l1_deact_cnf(struct FsmInst *fi, int event, void *arg)
{
	struct layer1 *l1 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_L1_F3);
	if (test_bit(FLG_L1_ACTIVATING, &l1->Flags))
		l1->dcb(l1->dch, HW_POWERUP_REQ);
}

static void
l1_deact_req_s(struct FsmInst *fi, int event, void *arg)
{
	struct layer1 *l1 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_L1_F3);
	mISDN_FsmRestartTimer(&l1->timer, 550, EV_TIMER_DEACT, NULL, 2);
	test_and_set_bit(FLG_L1_DEACTTIMER, &l1->Flags);
}

static void
l1_power_up_s(struct FsmInst *fi, int event, void *arg)
{
	struct layer1 *l1 = fi->userdata;

	if (test_bit(FLG_L1_ACTIVATING, &l1->Flags)) {
		mISDN_FsmChangeState(fi, ST_L1_F4);
		l1->dcb(l1->dch, INFO3_P8);
	} else
		mISDN_FsmChangeState(fi, ST_L1_F3);
}

static void
l1_go_F5(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_L1_F5);
}

static void
l1_go_F8(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_L1_F8);
}

static void
l1_info2_ind(struct FsmInst *fi, int event, void *arg)
{
	struct layer1 *l1 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_L1_F6);
	l1->dcb(l1->dch, INFO3_P8);
}

static void
l1_info4_ind(struct FsmInst *fi, int event, void *arg)
{
	struct layer1 *l1 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_L1_F7);
	l1->dcb(l1->dch, INFO3_P8);
	if (test_and_clear_bit(FLG_L1_DEACTTIMER, &l1->Flags))
		mISDN_FsmDelTimer(&l1->timer, 4);
	if (!test_bit(FLG_L1_ACTIVATED, &l1->Flags)) {
		if (test_and_clear_bit(FLG_L1_T3RUN, &l1->Flags))
			mISDN_FsmDelTimer(&l1->timer, 3);
		mISDN_FsmRestartTimer(&l1->timer, 110, EV_TIMER_ACT, NULL, 2);
		test_and_set_bit(FLG_L1_ACTTIMER, &l1->Flags);
	}
}

static void
l1_timer3(struct FsmInst *fi, int event, void *arg)
{
	struct layer1 *l1 = fi->userdata;

	test_and_clear_bit(FLG_L1_T3RUN, &l1->Flags);
	if (test_and_clear_bit(FLG_L1_ACTIVATING, &l1->Flags)) {
		if (test_and_clear_bit(FLG_L1_DBLOCKED, &l1->Flags))
			l1->dcb(l1->dch, HW_D_NOBLOCKED);
		l1->dcb(l1->dch, PH_DEACTIVATE_IND);
	}
	if (l1->l1m.state != ST_L1_F6) {
		mISDN_FsmChangeState(fi, ST_L1_F3);
		l1->dcb(l1->dch, HW_POWERUP_REQ);
	}
}

static void
l1_timer_act(struct FsmInst *fi, int event, void *arg)
{
	struct layer1 *l1 = fi->userdata;

	test_and_clear_bit(FLG_L1_ACTTIMER, &l1->Flags);
	test_and_set_bit(FLG_L1_ACTIVATED, &l1->Flags);
	l1->dcb(l1->dch, PH_ACTIVATE_IND);
}

static void
l1_timer_deact(struct FsmInst *fi, int event, void *arg)
{
	struct layer1 *l1 = fi->userdata;

	test_and_clear_bit(FLG_L1_DEACTTIMER, &l1->Flags);
	test_and_clear_bit(FLG_L1_ACTIVATED, &l1->Flags);
	if (test_and_clear_bit(FLG_L1_DBLOCKED, &l1->Flags))
		l1->dcb(l1->dch, HW_D_NOBLOCKED);
	l1->dcb(l1->dch, PH_DEACTIVATE_IND);
	l1->dcb(l1->dch, HW_DEACT_REQ);
}

static void
l1_activate_s(struct FsmInst *fi, int event, void *arg)
{
	struct layer1 *l1 = fi->userdata;

	mISDN_FsmRestartTimer(&l1->timer, TIMER3_VALUE, EV_TIMER3, NULL, 2);
	test_and_set_bit(FLG_L1_T3RUN, &l1->Flags);
	l1->dcb(l1->dch, HW_RESET_REQ);
}

static void
l1_activate_no(struct FsmInst *fi, int event, void *arg)
{
	struct layer1 *l1 = fi->userdata;

	if ((!test_bit(FLG_L1_DEACTTIMER, &l1->Flags)) &&
	    (!test_bit(FLG_L1_T3RUN, &l1->Flags))) {
		test_and_clear_bit(FLG_L1_ACTIVATING, &l1->Flags);
		if (test_and_clear_bit(FLG_L1_DBLOCKED, &l1->Flags))
			l1->dcb(l1->dch, HW_D_NOBLOCKED);
		l1->dcb(l1->dch, PH_DEACTIVATE_IND);
	}
}

static struct FsmNode L1SFnList[] =
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
	{ST_L1_F3, EV_POWER_UP,  l1_power_up_s},
	{ST_L1_F4, EV_ANYSIG_IND, l1_go_F5},
	{ST_L1_F6, EV_ANYSIG_IND, l1_go_F8},
	{ST_L1_F7, EV_ANYSIG_IND, l1_go_F8},
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

static void
release_l1(struct layer1 *l1) {
	mISDN_FsmDelTimer(&l1->timer, 0);
	if (l1->dch)
		l1->dch->l1 = NULL;
	module_put(THIS_MODULE);
	kfree(l1);
}

int
l1_event(struct layer1 *l1, u_int event)
{
	int		err = 0;

	if (!l1)
		return -EINVAL;
	switch (event) {
	case HW_RESET_IND:
		mISDN_FsmEvent(&l1->l1m, EV_RESET_IND, NULL);
		break;
	case HW_DEACT_IND:
		mISDN_FsmEvent(&l1->l1m, EV_DEACT_IND, NULL);
		break;
	case HW_POWERUP_IND:
		mISDN_FsmEvent(&l1->l1m, EV_POWER_UP, NULL);
		break;
	case HW_DEACT_CNF:
		mISDN_FsmEvent(&l1->l1m, EV_DEACT_CNF, NULL);
		break;
	case ANYSIGNAL:
		mISDN_FsmEvent(&l1->l1m, EV_ANYSIG_IND, NULL);
		break;
	case LOSTFRAMING:
		mISDN_FsmEvent(&l1->l1m, EV_ANYSIG_IND, NULL);
		break;
	case INFO2:
		mISDN_FsmEvent(&l1->l1m, EV_INFO2_IND, NULL);
		break;
	case INFO4_P8:
		mISDN_FsmEvent(&l1->l1m, EV_INFO4_IND, NULL);
		break;
	case INFO4_P10:
		mISDN_FsmEvent(&l1->l1m, EV_INFO4_IND, NULL);
		break;
	case PH_ACTIVATE_REQ:
		if (test_bit(FLG_L1_ACTIVATED, &l1->Flags))
			l1->dcb(l1->dch, PH_ACTIVATE_IND);
		else {
			test_and_set_bit(FLG_L1_ACTIVATING, &l1->Flags);
			mISDN_FsmEvent(&l1->l1m, EV_PH_ACTIVATE, NULL);
		}
		break;
	case CLOSE_CHANNEL:
		release_l1(l1);
		break;
	default:
		if (*debug & DEBUG_L1)
			printk(KERN_DEBUG "%s %x unhandled\n",
			    __func__, event);
		err = -EINVAL;
	}
	return err;
}
EXPORT_SYMBOL(l1_event);

int
create_l1(struct dchannel *dch, dchannel_l1callback *dcb) {
	struct layer1	*nl1;

	nl1 = kzalloc(sizeof(struct layer1), GFP_ATOMIC);
	if (!nl1) {
		printk(KERN_ERR "kmalloc struct layer1 failed\n");
		return -ENOMEM;
	}
	nl1->l1m.fsm = &l1fsm_s;
	nl1->l1m.state = ST_L1_F3;
	nl1->Flags = 0;
	nl1->l1m.debug = *debug & DEBUG_L1_FSM;
	nl1->l1m.userdata = nl1;
	nl1->l1m.userint = 0;
	nl1->l1m.printdebug = l1m_debug;
	nl1->dch = dch;
	nl1->dcb = dcb;
	mISDN_FsmInitTimer(&nl1->l1m, &nl1->timer);
	__module_get(THIS_MODULE);
	dch->l1 = nl1;
	return 0;
}
EXPORT_SYMBOL(create_l1);

int
l1_init(u_int *deb)
{
	debug = deb;
	l1fsm_s.state_count = L1S_STATE_COUNT;
	l1fsm_s.event_count = L1_EVENT_COUNT;
	l1fsm_s.strEvent = strL1Event;
	l1fsm_s.strState = strL1SState;
	mISDN_FsmNew(&l1fsm_s, L1SFnList, ARRAY_SIZE(L1SFnList));
	return 0;
}

void
l1_cleanup(void)
{
	mISDN_FsmFree(&l1fsm_s);
}
