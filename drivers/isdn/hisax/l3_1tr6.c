/* $Id: l3_1tr6.c,v 2.15.2.3 2004/01/13 14:31:25 keil Exp $
 *
 * German 1TR6 D-channel protocol
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * For changes and modifications please read
 * Documentation/isdn/HiSax.cert
 *
 */

#include "hisax.h"
#include "l3_1tr6.h"
#include "isdnl3.h"
#include <linux/ctype.h>

extern char *HiSax_getrev(const char *revision);
static const char *l3_1tr6_revision = "$Revision: 2.15.2.3 $";

#define MsgHead(ptr, cref, mty, dis) \
	*ptr++ = dis; \
	*ptr++ = 0x1; \
	*ptr++ = cref ^ 0x80; \
	*ptr++ = mty

static void
l3_1TR6_message(struct l3_process *pc, u_char mt, u_char pd)
{
	struct sk_buff *skb;
	u_char *p;

	if (!(skb = l3_alloc_skb(4)))
		return;
	p = skb_put(skb, 4);
	MsgHead(p, pc->callref, mt, pd);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
}

static void
l3_1tr6_release_req(struct l3_process *pc, u_char pr, void *arg)
{
	StopAllL3Timer(pc);
	newl3state(pc, 19);
	l3_1TR6_message(pc, MT_N1_REL, PROTO_DIS_N1);
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3_1tr6_invalid(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb);
	l3_1tr6_release_req(pc, 0, NULL);
}

static void
l3_1tr6_error(struct l3_process *pc, u_char *msg, struct sk_buff *skb)
{
	dev_kfree_skb(skb);
	if (pc->st->l3.debug & L3_DEB_WARN)
		l3_debug(pc->st, msg);
	l3_1tr6_release_req(pc, 0, NULL);
}

static void
l3_1tr6_setup_req(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[128];
	u_char *p = tmp;
	u_char *teln;
	u_char *eaz;
	u_char channel = 0;
	int l;

	MsgHead(p, pc->callref, MT_N1_SETUP, PROTO_DIS_N1);
	teln = pc->para.setup.phone;
	pc->para.spv = 0;
	if (!isdigit(*teln)) {
		switch (0x5f & *teln) {
			case 'S':
				pc->para.spv = 1;
				break;
			case 'C':
				channel = 0x08;
			case 'P':
				channel |= 0x80;
				teln++;
				if (*teln == '1')
					channel |= 0x01;
				else
					channel |= 0x02;
				break;
			default:
				if (pc->st->l3.debug & L3_DEB_WARN)
					l3_debug(pc->st, "Wrong MSN Code");
				break;
		}
		teln++;
	}
	if (channel) {
		*p++ = 0x18;	/* channel indicator */
		*p++ = 1;
		*p++ = channel;
	}
	if (pc->para.spv) {	/* SPV ? */
		/* NSF SPV */
		*p++ = WE0_netSpecFac;
		*p++ = 4;	/* Laenge */
		*p++ = 0;
		*p++ = FAC_SPV;	/* SPV */
		*p++ = pc->para.setup.si1;	/* 0 for all Services */
		*p++ = pc->para.setup.si2;	/* 0 for all Services */
		*p++ = WE0_netSpecFac;
		*p++ = 4;	/* Laenge */
		*p++ = 0;
		*p++ = FAC_Activate;	/* aktiviere SPV (default) */
		*p++ = pc->para.setup.si1;	/* 0 for all Services */
		*p++ = pc->para.setup.si2;	/* 0 for all Services */
	}
	eaz = pc->para.setup.eazmsn;
	if (*eaz) {
		*p++ = WE0_origAddr;
		*p++ = strlen(eaz) + 1;
		/* Classify as AnyPref. */
		*p++ = 0x81;	/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */
		while (*eaz)
			*p++ = *eaz++ & 0x7f;
	}
	*p++ = WE0_destAddr;
	*p++ = strlen(teln) + 1;
	/* Classify as AnyPref. */
	*p++ = 0x81;		/* Ext = '1'B, Type = '000'B, Plan = '0001'B. */
	while (*teln)
		*p++ = *teln++ & 0x7f;

	*p++ = WE_Shift_F6;
	/* Codesatz 6 fuer Service */
	*p++ = WE6_serviceInd;
	*p++ = 2;		/* len=2 info,info2 */
	*p++ = pc->para.setup.si1;
	*p++ = pc->para.setup.si2;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T303, CC_T303);
	newl3state(pc, 1);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
}

static void
l3_1tr6_setup(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	int bcfound = 0;
	char tmp[80];
	struct sk_buff *skb = arg;

	p = skb->data;

	/* Channel Identification */
	p = skb->data;
	if ((p = findie(p, skb->len, WE0_chanID, 0))) {
		if (p[1] != 1) {
			l3_1tr6_error(pc, "setup wrong chanID len", skb);
			return;
		}
		if ((p[2] & 0xf4) != 0x80) {
			l3_1tr6_error(pc, "setup wrong WE0_chanID", skb);
			return;
		}
		if ((pc->para.bchannel = p[2] & 0x3))
				bcfound++;
	} else {
		l3_1tr6_error(pc, "missing setup chanID", skb);
		return;
	}

	p = skb->data;
	if ((p = findie(p, skb->len, WE6_serviceInd, 6))) {
		pc->para.setup.si1 = p[2];
		pc->para.setup.si2 = p[3];
	} else {
		l3_1tr6_error(pc, "missing setup SI", skb);
		return;
	}

	p = skb->data;
	if ((p = findie(p, skb->len, WE0_destAddr, 0)))
		iecpy(pc->para.setup.eazmsn, p, 1);
	else
		pc->para.setup.eazmsn[0] = 0;

	p = skb->data;
	if ((p = findie(p, skb->len, WE0_origAddr, 0))) {
		iecpy(pc->para.setup.phone, p, 1);
	} else
		pc->para.setup.phone[0] = 0;

	p = skb->data;
	pc->para.spv = 0;
	if ((p = findie(p, skb->len, WE0_netSpecFac, 0))) {
		if ((FAC_SPV == p[3]) || (FAC_Activate == p[3]))
			pc->para.spv = 1;
	}
	dev_kfree_skb(skb);

	/* Signal all services, linklevel takes care of Service-Indicator */
	if (bcfound) {
		if ((pc->para.setup.si1 != 7) && (pc->st->l3.debug & L3_DEB_WARN)) {
			sprintf(tmp, "non-digital call: %s -> %s",
				pc->para.setup.phone,
				pc->para.setup.eazmsn);
			l3_debug(pc->st, tmp);
		}
		newl3state(pc, 6);
		pc->st->l3.l3l4(pc->st, CC_SETUP | INDICATION, pc);
	} else
		release_l3_process(pc);
}

static void
l3_1tr6_setup_ack(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;

	L3DelTimer(&pc->timer);
	p = skb->data;
	newl3state(pc, 2);
	if ((p = findie(p, skb->len, WE0_chanID, 0))) {
		if (p[1] != 1) {
			l3_1tr6_error(pc, "setup_ack wrong chanID len", skb);
			return;
		}
		if ((p[2] & 0xf4) != 0x80) {
			l3_1tr6_error(pc, "setup_ack wrong WE0_chanID", skb);
			return;
		}
		pc->para.bchannel = p[2] & 0x3;
	} else {
		l3_1tr6_error(pc, "missing setup_ack WE0_chanID", skb);
		return;
	}
	dev_kfree_skb(skb);
	L3AddTimer(&pc->timer, T304, CC_T304);
	pc->st->l3.l3l4(pc->st, CC_MORE_INFO | INDICATION, pc);
}

static void
l3_1tr6_call_sent(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;

	L3DelTimer(&pc->timer);
	p = skb->data;
	if ((p = findie(p, skb->len, WE0_chanID, 0))) {
		if (p[1] != 1) {
			l3_1tr6_error(pc, "call sent wrong chanID len", skb);
			return;
		}
		if ((p[2] & 0xf4) != 0x80) {
			l3_1tr6_error(pc, "call sent wrong WE0_chanID", skb);
			return;
		}
		if ((pc->state == 2) && (pc->para.bchannel != (p[2] & 0x3))) {
			l3_1tr6_error(pc, "call sent wrong chanID value", skb);
			return;
		}
		pc->para.bchannel = p[2] & 0x3;
	} else {
		l3_1tr6_error(pc, "missing call sent WE0_chanID", skb);
		return;
	}
	dev_kfree_skb(skb);
	L3AddTimer(&pc->timer, T310, CC_T310);
	newl3state(pc, 3);
	pc->st->l3.l3l4(pc->st, CC_PROCEEDING | INDICATION, pc);
}

static void
l3_1tr6_alert(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb);
	L3DelTimer(&pc->timer);	/* T304 */
	newl3state(pc, 4);
	pc->st->l3.l3l4(pc->st, CC_ALERTING | INDICATION, pc);
}

static void
l3_1tr6_info(struct l3_process *pc, u_char pr, void *arg)
{
	u_char *p;
	int i, tmpcharge = 0;
	char a_charge[8], tmp[32];
	struct sk_buff *skb = arg;

	p = skb->data;
	if ((p = findie(p, skb->len, WE6_chargingInfo, 6))) {
		iecpy(a_charge, p, 1);
		for (i = 0; i < strlen(a_charge); i++) {
			tmpcharge *= 10;
			tmpcharge += a_charge[i] & 0xf;
		}
		if (tmpcharge > pc->para.chargeinfo) {
			pc->para.chargeinfo = tmpcharge;
			pc->st->l3.l3l4(pc->st, CC_CHARGE | INDICATION, pc);
		}
		if (pc->st->l3.debug & L3_DEB_CHARGE) {
			sprintf(tmp, "charging info %d", pc->para.chargeinfo);
			l3_debug(pc->st, tmp);
		}
	} else if (pc->st->l3.debug & L3_DEB_CHARGE)
		l3_debug(pc->st, "charging info not found");
	dev_kfree_skb(skb);

}

static void
l3_1tr6_info_s2(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb);
}

static void
l3_1tr6_connect(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	L3DelTimer(&pc->timer);	/* T310 */
	if (!findie(skb->data, skb->len, WE6_date, 6)) {
		l3_1tr6_error(pc, "missing connect date", skb);
		return;
	}
	newl3state(pc, 10);
	dev_kfree_skb(skb);
	pc->para.chargeinfo = 0;
	pc->st->l3.l3l4(pc->st, CC_SETUP | CONFIRM, pc);
}

static void
l3_1tr6_rel(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	u_char *p;

	p = skb->data;
	if ((p = findie(p, skb->len, WE0_cause, 0))) {
		if (p[1] > 0) {
			pc->para.cause = p[2];
			if (p[1] > 1)
				pc->para.loc = p[3];
			else
				pc->para.loc = 0;
		} else {
			pc->para.cause = 0;
			pc->para.loc = 0;
		}
	} else {
		pc->para.cause = NO_CAUSE;
		l3_1tr6_error(pc, "missing REL cause", skb);
		return;
	}
	dev_kfree_skb(skb);
	StopAllL3Timer(pc);
	newl3state(pc, 0);
	l3_1TR6_message(pc, MT_N1_REL_ACK, PROTO_DIS_N1);
	pc->st->l3.l3l4(pc->st, CC_RELEASE | INDICATION, pc);
	release_l3_process(pc);
}

static void
l3_1tr6_rel_ack(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	dev_kfree_skb(skb);
	StopAllL3Timer(pc);
	newl3state(pc, 0);
	pc->para.cause = NO_CAUSE;
	pc->st->l3.l3l4(pc->st, CC_RELEASE | CONFIRM, pc);
	release_l3_process(pc);
}

static void
l3_1tr6_disc(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	u_char *p;
	int i, tmpcharge = 0;
	char a_charge[8], tmp[32];

	StopAllL3Timer(pc);
	p = skb->data;
	if ((p = findie(p, skb->len, WE6_chargingInfo, 6))) {
		iecpy(a_charge, p, 1);
		for (i = 0; i < strlen(a_charge); i++) {
			tmpcharge *= 10;
			tmpcharge += a_charge[i] & 0xf;
		}
		if (tmpcharge > pc->para.chargeinfo) {
			pc->para.chargeinfo = tmpcharge;
			pc->st->l3.l3l4(pc->st, CC_CHARGE | INDICATION, pc);
		}
		if (pc->st->l3.debug & L3_DEB_CHARGE) {
			sprintf(tmp, "charging info %d", pc->para.chargeinfo);
			l3_debug(pc->st, tmp);
		}
	} else if (pc->st->l3.debug & L3_DEB_CHARGE)
		l3_debug(pc->st, "charging info not found");


	p = skb->data;
	if ((p = findie(p, skb->len, WE0_cause, 0))) {
		if (p[1] > 0) {
			pc->para.cause = p[2];
			if (p[1] > 1)
				pc->para.loc = p[3];
			else
				pc->para.loc = 0;
		} else {
			pc->para.cause = 0;
			pc->para.loc = 0;
		}
	} else {
		if (pc->st->l3.debug & L3_DEB_WARN)
			l3_debug(pc->st, "cause not found");
		pc->para.cause = NO_CAUSE;
	}
	if (!findie(skb->data, skb->len, WE6_date, 6)) {
		l3_1tr6_error(pc, "missing connack date", skb);
		return;
	}
	dev_kfree_skb(skb);
	newl3state(pc, 12);
	pc->st->l3.l3l4(pc->st, CC_DISCONNECT | INDICATION, pc);
}


static void
l3_1tr6_connect_ack(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	if (!findie(skb->data, skb->len, WE6_date, 6)) {
		l3_1tr6_error(pc, "missing connack date", skb);
		return;
	}
	dev_kfree_skb(skb);
	newl3state(pc, 10);
	pc->para.chargeinfo = 0;
	L3DelTimer(&pc->timer);
	pc->st->l3.l3l4(pc->st, CC_SETUP_COMPL | INDICATION, pc);
}

static void
l3_1tr6_alert_req(struct l3_process *pc, u_char pr, void *arg)
{
	newl3state(pc, 7);
	l3_1TR6_message(pc, MT_N1_ALERT, PROTO_DIS_N1);
}

static void
l3_1tr6_setup_rsp(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[24];
	u_char *p = tmp;
	int l;

	MsgHead(p, pc->callref, MT_N1_CONN, PROTO_DIS_N1);
	if (pc->para.spv) {	/* SPV ? */
		/* NSF SPV */
		*p++ = WE0_netSpecFac;
		*p++ = 4;	/* Laenge */
		*p++ = 0;
		*p++ = FAC_SPV;	/* SPV */
		*p++ = pc->para.setup.si1;
		*p++ = pc->para.setup.si2;
		*p++ = WE0_netSpecFac;
		*p++ = 4;	/* Laenge */
		*p++ = 0;
		*p++ = FAC_Activate;	/* aktiviere SPV */
		*p++ = pc->para.setup.si1;
		*p++ = pc->para.setup.si2;
	}
	newl3state(pc, 8);
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T313, CC_T313);
}

static void
l3_1tr6_reset(struct l3_process *pc, u_char pr, void *arg)
{
	release_l3_process(pc);
}

static void
l3_1tr6_disconnect_req(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	u_char cause = 0x10;
	u_char clen = 1;

	if (pc->para.cause > 0)
		cause = pc->para.cause;
	/* Map DSS1 causes */
	switch (cause & 0x7f) {
		case 0x10:
			clen = 0;
			break;
                case 0x11:
                        cause = CAUSE_UserBusy;
                        break;
		case 0x15:
			cause = CAUSE_CallRejected;
			break;
	}
	StopAllL3Timer(pc);
	MsgHead(p, pc->callref, MT_N1_DISC, PROTO_DIS_N1);
	*p++ = WE0_cause;
	*p++ = clen;		/* Laenge */
	if (clen)
		*p++ = cause | 0x80;
	newl3state(pc, 11);
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
	L3AddTimer(&pc->timer, T305, CC_T305);
}

static void
l3_1tr6_t303(struct l3_process *pc, u_char pr, void *arg)
{
	if (pc->N303 > 0) {
		pc->N303--;
		L3DelTimer(&pc->timer);
		l3_1tr6_setup_req(pc, pr, arg);
	} else {
		L3DelTimer(&pc->timer);
		pc->para.cause = 0;
		l3_1tr6_disconnect_req(pc, 0, NULL);
	}
}

static void
l3_1tr6_t304(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.cause = 0xE6;
	l3_1tr6_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc->st, CC_SETUP_ERR, pc);
}

static void
l3_1tr6_t305(struct l3_process *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	u_char cause = 0x90;
	u_char clen = 1;

	L3DelTimer(&pc->timer);
	if (pc->para.cause != NO_CAUSE)
		cause = pc->para.cause;
	/* Map DSS1 causes */
	switch (cause & 0x7f) {
		case 0x10:
			clen = 0;
			break;
		case 0x15:
			cause = CAUSE_CallRejected;
			break;
	}
	MsgHead(p, pc->callref, MT_N1_REL, PROTO_DIS_N1);
	*p++ = WE0_cause;
	*p++ = clen;		/* Laenge */
	if (clen)
		*p++ = cause;
	newl3state(pc, 19);
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->st, DL_DATA | REQUEST, skb);
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3_1tr6_t310(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.cause = 0xE6;
	l3_1tr6_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc->st, CC_SETUP_ERR, pc);
}

static void
l3_1tr6_t313(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->para.cause = 0xE6;
	l3_1tr6_disconnect_req(pc, pr, NULL);
	pc->st->l3.l3l4(pc->st, CC_CONNECT_ERR, pc);
}

static void
l3_1tr6_t308_1(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	l3_1TR6_message(pc, MT_N1_REL, PROTO_DIS_N1);
	L3AddTimer(&pc->timer, T308, CC_T308_2);
	newl3state(pc, 19);
}

static void
l3_1tr6_t308_2(struct l3_process *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	pc->st->l3.l3l4(pc->st, CC_RELEASE_ERR, pc);
	release_l3_process(pc);
}

static void
l3_1tr6_dl_reset(struct l3_process *pc, u_char pr, void *arg)
{
        pc->para.cause = CAUSE_LocalProcErr;
        l3_1tr6_disconnect_req(pc, pr, NULL);
        pc->st->l3.l3l4(pc->st, CC_SETUP_ERR, pc);
}

static void
l3_1tr6_dl_release(struct l3_process *pc, u_char pr, void *arg)
{
        newl3state(pc, 0);
        pc->para.cause = 0x1b;          /* Destination out of order */
        pc->para.loc = 0;
        pc->st->l3.l3l4(pc->st, CC_RELEASE | INDICATION, pc);
        release_l3_process(pc);
}

/* *INDENT-OFF* */
static struct stateentry downstl[] =
{
	{SBIT(0),
	 CC_SETUP | REQUEST, l3_1tr6_setup_req},
   	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(6) | SBIT(7) | SBIT(8) |
    	 SBIT(10),
    	 CC_DISCONNECT | REQUEST, l3_1tr6_disconnect_req},
	{SBIT(12),
	 CC_RELEASE | REQUEST, l3_1tr6_release_req},
	{SBIT(6),
	 CC_IGNORE | REQUEST, l3_1tr6_reset},
	{SBIT(6),
	 CC_REJECT | REQUEST, l3_1tr6_disconnect_req},
	{SBIT(6),
	 CC_ALERTING | REQUEST, l3_1tr6_alert_req},
	{SBIT(6) | SBIT(7),
	 CC_SETUP | RESPONSE, l3_1tr6_setup_rsp},
	{SBIT(1),
	 CC_T303, l3_1tr6_t303},
	{SBIT(2),
	 CC_T304, l3_1tr6_t304},
	{SBIT(3),
	 CC_T310, l3_1tr6_t310},
	{SBIT(8),
	 CC_T313, l3_1tr6_t313},
	{SBIT(11),
	 CC_T305, l3_1tr6_t305},
	{SBIT(19),
	 CC_T308_1, l3_1tr6_t308_1},
	{SBIT(19),
	 CC_T308_2, l3_1tr6_t308_2},
};

#define DOWNSTL_LEN \
	(sizeof(downstl) / sizeof(struct stateentry))

static struct stateentry datastln1[] =
{
	{SBIT(0),
	 MT_N1_INVALID, l3_1tr6_invalid},
	{SBIT(0),
	 MT_N1_SETUP, l3_1tr6_setup},
	{SBIT(1),
	 MT_N1_SETUP_ACK, l3_1tr6_setup_ack},
	{SBIT(1) | SBIT(2),
	 MT_N1_CALL_SENT, l3_1tr6_call_sent},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(10),
	 MT_N1_DISC, l3_1tr6_disc},
	{SBIT(2) | SBIT(3) | SBIT(4),
	 MT_N1_ALERT, l3_1tr6_alert},
	{SBIT(2) | SBIT(3) | SBIT(4),
	 MT_N1_CONN, l3_1tr6_connect},
	{SBIT(2),
	 MT_N1_INFO, l3_1tr6_info_s2},
	{SBIT(8),
	 MT_N1_CONN_ACK, l3_1tr6_connect_ack},
	{SBIT(10),
	 MT_N1_INFO, l3_1tr6_info},
	{SBIT(0) | SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) |
	 SBIT(10) | SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17),
	 MT_N1_REL, l3_1tr6_rel},
	{SBIT(19),
	 MT_N1_REL, l3_1tr6_rel_ack},
	{SBIT(0) | SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) |
	 SBIT(10) | SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17),
	 MT_N1_REL_ACK, l3_1tr6_invalid},
	{SBIT(19),
	 MT_N1_REL_ACK, l3_1tr6_rel_ack}
};

#define DATASTLN1_LEN \
	(sizeof(datastln1) / sizeof(struct stateentry))

static struct stateentry manstatelist[] =
{
        {SBIT(2),
         DL_ESTABLISH | INDICATION, l3_1tr6_dl_reset},
        {ALL_STATES,
         DL_RELEASE | INDICATION, l3_1tr6_dl_release},
};
 
#define MANSLLEN \
        (sizeof(manstatelist) / sizeof(struct stateentry))
/* *INDENT-ON* */

static void
up1tr6(struct PStack *st, int pr, void *arg)
{
	int i, mt, cr;
	struct l3_process *proc;
	struct sk_buff *skb = arg;
	char tmp[80];

	switch (pr) {
		case (DL_DATA | INDICATION):
		case (DL_UNIT_DATA | INDICATION):
			break;
		case (DL_ESTABLISH | CONFIRM):
		case (DL_ESTABLISH | INDICATION):
		case (DL_RELEASE | INDICATION):
		case (DL_RELEASE | CONFIRM):
			l3_msg(st, pr, arg);
			return;
			break;
	}
	if (skb->len < 4) {
		if (st->l3.debug & L3_DEB_PROTERR) {
			sprintf(tmp, "up1tr6 len only %d", skb->len);
			l3_debug(st, tmp);
		}
		dev_kfree_skb(skb);
		return;
	}
	if ((skb->data[0] & 0xfe) != PROTO_DIS_N0) {
		if (st->l3.debug & L3_DEB_PROTERR) {
			sprintf(tmp, "up1tr6%sunexpected discriminator %x message len %d",
				(pr == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
				skb->data[0], skb->len);
			l3_debug(st, tmp);
		}
		dev_kfree_skb(skb);
		return;
	}
	if (skb->data[1] != 1) {
		if (st->l3.debug & L3_DEB_PROTERR) {
			sprintf(tmp, "up1tr6 CR len not 1");
			l3_debug(st, tmp);
		}
		dev_kfree_skb(skb);
		return;
	}
	cr = skb->data[2];
	mt = skb->data[3];
	if (skb->data[0] == PROTO_DIS_N0) {
		dev_kfree_skb(skb);
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "up1tr6%s N0 mt %x unhandled",
			     (pr == (DL_DATA | INDICATION)) ? " " : "(broadcast) ", mt);
			l3_debug(st, tmp);
		}
	} else if (skb->data[0] == PROTO_DIS_N1) {
		if (!(proc = getl3proc(st, cr))) {
			if (mt == MT_N1_SETUP) { 
				if (cr < 128) {
					if (!(proc = new_l3_process(st, cr))) {
						if (st->l3.debug & L3_DEB_PROTERR) {
							sprintf(tmp, "up1tr6 no roc mem");
							l3_debug(st, tmp);
						}
						dev_kfree_skb(skb);
						return;
					}
				} else {
					dev_kfree_skb(skb);
					return;
				}
			} else if ((mt == MT_N1_REL) || (mt == MT_N1_REL_ACK) ||
				(mt == MT_N1_CANC_ACK) || (mt == MT_N1_CANC_REJ) ||
				(mt == MT_N1_REG_ACK) || (mt == MT_N1_REG_REJ) ||
				(mt == MT_N1_SUSP_ACK) || (mt == MT_N1_RES_REJ) ||
				(mt == MT_N1_INFO)) {
				dev_kfree_skb(skb);
				return;
			} else {
				if (!(proc = new_l3_process(st, cr))) {
					if (st->l3.debug & L3_DEB_PROTERR) {
						sprintf(tmp, "up1tr6 no roc mem");
						l3_debug(st, tmp);
					}
					dev_kfree_skb(skb);
					return;
				}
				mt = MT_N1_INVALID;
			}
		}
		for (i = 0; i < DATASTLN1_LEN; i++)
			if ((mt == datastln1[i].primitive) &&
			    ((1 << proc->state) & datastln1[i].state))
				break;
		if (i == DATASTLN1_LEN) {
			dev_kfree_skb(skb);
			if (st->l3.debug & L3_DEB_STATE) {
				sprintf(tmp, "up1tr6%sstate %d mt %x unhandled",
				  (pr == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
					proc->state, mt);
				l3_debug(st, tmp);
			}
			return;
		} else {
			if (st->l3.debug & L3_DEB_STATE) {
				sprintf(tmp, "up1tr6%sstate %d mt %x",
				  (pr == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
					proc->state, mt);
				l3_debug(st, tmp);
			}
			datastln1[i].rout(proc, pr, skb);
		}
	}
}

static void
down1tr6(struct PStack *st, int pr, void *arg)
{
	int i, cr;
	struct l3_process *proc;
	struct Channel *chan;
	char tmp[80];

	if ((DL_ESTABLISH | REQUEST)== pr) {
		l3_msg(st, pr, NULL);
		return;
	} else if ((CC_SETUP | REQUEST) == pr) {
		chan = arg;
		cr = newcallref();
		cr |= 0x80;
		if (!(proc = new_l3_process(st, cr))) {
			return;
		} else {
			proc->chan = chan;
			chan->proc = proc;
			memcpy(&proc->para.setup, &chan->setup, sizeof(setup_parm));
			proc->callref = cr;
		}
	} else {
		proc = arg;
	}

	for (i = 0; i < DOWNSTL_LEN; i++)
		if ((pr == downstl[i].primitive) &&
		    ((1 << proc->state) & downstl[i].state))
			break;
	if (i == DOWNSTL_LEN) {
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "down1tr6 state %d prim %d unhandled",
				proc->state, pr);
			l3_debug(st, tmp);
		}
	} else {
		if (st->l3.debug & L3_DEB_STATE) {
			sprintf(tmp, "down1tr6 state %d prim %d",
				proc->state, pr);
			l3_debug(st, tmp);
		}
		downstl[i].rout(proc, pr, arg);
	}
}

static void
man1tr6(struct PStack *st, int pr, void *arg)
{
        int i;
        struct l3_process *proc = arg;
 
        if (!proc) {
                printk(KERN_ERR "HiSax man1tr6 without proc pr=%04x\n", pr);
                return;
        }
        for (i = 0; i < MANSLLEN; i++)
                if ((pr == manstatelist[i].primitive) &&
                    ((1 << proc->state) & manstatelist[i].state))
                        break;
        if (i == MANSLLEN) {
                if (st->l3.debug & L3_DEB_STATE) {
                        l3_debug(st, "cr %d man1tr6 state %d prim %d unhandled",
                                proc->callref & 0x7f, proc->state, pr);
                }
        } else {
                if (st->l3.debug & L3_DEB_STATE) {
                        l3_debug(st, "cr %d man1tr6 state %d prim %d",
                                proc->callref & 0x7f, proc->state, pr);
                }
                manstatelist[i].rout(proc, pr, arg);
        }
}
 
void
setstack_1tr6(struct PStack *st)
{
	char tmp[64];

	st->lli.l4l3 = down1tr6;
	st->l2.l2l3 = up1tr6;
	st->l3.l3ml3 = man1tr6;
	st->l3.N303 = 0;

	strcpy(tmp, l3_1tr6_revision);
	printk(KERN_INFO "HiSax: 1TR6 Rev. %s\n", HiSax_getrev(tmp));
}
