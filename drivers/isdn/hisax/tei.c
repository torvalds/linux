/* $Id: tei.c,v 2.20.2.3 2004/01/13 14:31:26 keil Exp $
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

#include "hisax.h"
#include "isdnl2.h"
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/random.h>

const char *tei_revision = "$Revision: 2.20.2.3 $";

#define ID_REQUEST	1
#define ID_ASSIGNED	2
#define ID_DENIED	3
#define ID_CHK_REQ	4
#define ID_CHK_RES	5
#define ID_REMOVE	6
#define ID_VERIFY	7

#define TEI_ENTITY_ID	0xf

static struct Fsm teifsm;

void tei_handler(struct PStack *st, u_char pr, struct sk_buff *skb);

enum {
	ST_TEI_NOP,
	ST_TEI_IDREQ,
	ST_TEI_IDVERIFY,
};

#define TEI_STATE_COUNT (ST_TEI_IDVERIFY+1)

static char *strTeiState[] =
{
	"ST_TEI_NOP",
	"ST_TEI_IDREQ",
	"ST_TEI_IDVERIFY",
};

enum {
	EV_IDREQ,
	EV_ASSIGN,
	EV_DENIED,
	EV_CHKREQ,
	EV_REMOVE,
	EV_VERIFY,
	EV_T202,
};

#define TEI_EVENT_COUNT (EV_T202+1)

static char *strTeiEvent[] =
{
	"EV_IDREQ",
	"EV_ASSIGN",
	"EV_DENIED",
	"EV_CHKREQ",
	"EV_REMOVE",
	"EV_VERIFY",
	"EV_T202",
};

static unsigned int
random_ri(void)
{
	unsigned int x;

	get_random_bytes(&x, sizeof(x));
	return (x & 0xffff);
}

static struct PStack *
findtei(struct PStack *st, int tei)
{
	struct PStack *ptr = *(st->l1.stlistp);

	if (tei == 127)
		return (NULL);

	while (ptr)
		if (ptr->l2.tei == tei)
			return (ptr);
		else
			ptr = ptr->next;
	return (NULL);
}

static void
put_tei_msg(struct PStack *st, u_char m_id, unsigned int ri, u_char tei)
{
	struct sk_buff *skb;
	u_char *bp;

	if (!(skb = alloc_skb(8, GFP_ATOMIC))) {
		printk(KERN_WARNING "HiSax: No skb for TEI manager\n");
		return;
	}
	bp = skb_put(skb, 3);
	bp[0] = (TEI_SAPI << 2);
	bp[1] = (GROUP_TEI << 1) | 0x1;
	bp[2] = UI;
	bp = skb_put(skb, 5);
	bp[0] = TEI_ENTITY_ID;
	bp[1] = ri >> 8;
	bp[2] = ri & 0xff;
	bp[3] = m_id;
	bp[4] = (tei << 1) | 1;
	st->l2.l2l1(st, PH_DATA | REQUEST, skb);
}

static void
tei_id_request(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (st->l2.tei != -1) {
		st->ma.tei_m.printdebug(&st->ma.tei_m,
			"assign request for already asigned tei %d",
			st->l2.tei);
		return;
	}
	st->ma.ri = random_ri();
	if (st->ma.debug)
		st->ma.tei_m.printdebug(&st->ma.tei_m,
			"assign request ri %d", st->ma.ri);
	put_tei_msg(st, ID_REQUEST, st->ma.ri, 127);
	FsmChangeState(&st->ma.tei_m, ST_TEI_IDREQ);
	FsmAddTimer(&st->ma.t202, st->ma.T202, EV_T202, NULL, 1);
	st->ma.N202 = 3;
}

static void
tei_id_assign(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *ost, *st = fi->userdata;
	struct sk_buff *skb = arg;
	struct IsdnCardState *cs;
	int ri, tei;

	ri = ((unsigned int) skb->data[1] << 8) + skb->data[2];
	tei = skb->data[4] >> 1;
	if (st->ma.debug)
		st->ma.tei_m.printdebug(&st->ma.tei_m,
			"identity assign ri %d tei %d", ri, tei);
	if ((ost = findtei(st, tei))) {	/* same tei is in use */
		if (ri != ost->ma.ri) {
			st->ma.tei_m.printdebug(&st->ma.tei_m,
				"possible duplicate assignment tei %d", tei);
			ost->l2.l2tei(ost, MDL_ERROR | RESPONSE, NULL);
		}
	} else if (ri == st->ma.ri) {
		FsmDelTimer(&st->ma.t202, 1);
		FsmChangeState(&st->ma.tei_m, ST_TEI_NOP);
		st->l3.l3l2(st, MDL_ASSIGN | REQUEST, (void *) (long) tei);
		cs = (struct IsdnCardState *) st->l1.hardware;
		cs->cardmsg(cs, MDL_ASSIGN | REQUEST, NULL);
	}
}

static void
tei_id_test_dup(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *ost, *st = fi->userdata;
	struct sk_buff *skb = arg;
	int tei, ri;

	ri = ((unsigned int) skb->data[1] << 8) + skb->data[2];
	tei = skb->data[4] >> 1;
	if (st->ma.debug)
		st->ma.tei_m.printdebug(&st->ma.tei_m,
			"foreign identity assign ri %d tei %d", ri, tei);
	if ((ost = findtei(st, tei))) {	/* same tei is in use */
		if (ri != ost->ma.ri) {	/* and it wasn't our request */
			st->ma.tei_m.printdebug(&st->ma.tei_m,
				"possible duplicate assignment tei %d", tei);
			FsmEvent(&ost->ma.tei_m, EV_VERIFY, NULL);
		}
	} 
}

static void
tei_id_denied(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int ri, tei;

	ri = ((unsigned int) skb->data[1] << 8) + skb->data[2];
	tei = skb->data[4] >> 1;
	if (st->ma.debug)
		st->ma.tei_m.printdebug(&st->ma.tei_m,
			"identity denied ri %d tei %d", ri, tei);
}

static void
tei_id_chk_req(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int tei;

	tei = skb->data[4] >> 1;
	if (st->ma.debug)
		st->ma.tei_m.printdebug(&st->ma.tei_m,
			"identity check req tei %d", tei);
	if ((st->l2.tei != -1) && ((tei == GROUP_TEI) || (tei == st->l2.tei))) {
		FsmDelTimer(&st->ma.t202, 4);
		FsmChangeState(&st->ma.tei_m, ST_TEI_NOP);
		put_tei_msg(st, ID_CHK_RES, random_ri(), st->l2.tei);
	}
}

static void
tei_id_remove(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	struct IsdnCardState *cs;
	int tei;

	tei = skb->data[4] >> 1;
	if (st->ma.debug)
		st->ma.tei_m.printdebug(&st->ma.tei_m,
			"identity remove tei %d", tei);
	if ((st->l2.tei != -1) && ((tei == GROUP_TEI) || (tei == st->l2.tei))) {
		FsmDelTimer(&st->ma.t202, 5);
		FsmChangeState(&st->ma.tei_m, ST_TEI_NOP);
		st->l3.l3l2(st, MDL_REMOVE | REQUEST, NULL);
		cs = (struct IsdnCardState *) st->l1.hardware;
		cs->cardmsg(cs, MDL_REMOVE | REQUEST, NULL);
	}
}

static void
tei_id_verify(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (st->ma.debug)
		st->ma.tei_m.printdebug(&st->ma.tei_m,
			"id verify request for tei %d", st->l2.tei);
	put_tei_msg(st, ID_VERIFY, 0, st->l2.tei);
	FsmChangeState(&st->ma.tei_m, ST_TEI_IDVERIFY);
	FsmAddTimer(&st->ma.t202, st->ma.T202, EV_T202, NULL, 2);
	st->ma.N202 = 2;
}

static void
tei_id_req_tout(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct IsdnCardState *cs;

	if (--st->ma.N202) {
		st->ma.ri = random_ri();
		if (st->ma.debug)
			st->ma.tei_m.printdebug(&st->ma.tei_m,
				"assign req(%d) ri %d", 4 - st->ma.N202,
				st->ma.ri);
		put_tei_msg(st, ID_REQUEST, st->ma.ri, 127);
		FsmAddTimer(&st->ma.t202, st->ma.T202, EV_T202, NULL, 3);
	} else {
		st->ma.tei_m.printdebug(&st->ma.tei_m, "assign req failed");
		st->l3.l3l2(st, MDL_ERROR | RESPONSE, NULL);
		cs = (struct IsdnCardState *) st->l1.hardware;
		cs->cardmsg(cs, MDL_REMOVE | REQUEST, NULL);
		FsmChangeState(fi, ST_TEI_NOP);
	}
}

static void
tei_id_ver_tout(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct IsdnCardState *cs;

	if (--st->ma.N202) {
		if (st->ma.debug)
			st->ma.tei_m.printdebug(&st->ma.tei_m,
				"id verify req(%d) for tei %d",
				3 - st->ma.N202, st->l2.tei);
		put_tei_msg(st, ID_VERIFY, 0, st->l2.tei);
		FsmAddTimer(&st->ma.t202, st->ma.T202, EV_T202, NULL, 4);
	} else {
		st->ma.tei_m.printdebug(&st->ma.tei_m,
			"verify req for tei %d failed", st->l2.tei);
		st->l3.l3l2(st, MDL_REMOVE | REQUEST, NULL);
		cs = (struct IsdnCardState *) st->l1.hardware;
		cs->cardmsg(cs, MDL_REMOVE | REQUEST, NULL);
		FsmChangeState(fi, ST_TEI_NOP);
	}
}

static void
tei_l1l2(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	int mt;

	if (test_bit(FLG_FIXED_TEI, &st->l2.flag)) {
		dev_kfree_skb(skb);
		return;
	}

	if (pr == (PH_DATA | INDICATION)) {
		if (skb->len < 3) {
			st->ma.tei_m.printdebug(&st->ma.tei_m,
				"short mgr frame %ld/3", skb->len);
		} else if ((skb->data[0] != ((TEI_SAPI << 2) | 2)) ||
			   (skb->data[1] != ((GROUP_TEI << 1) | 1))) {
			st->ma.tei_m.printdebug(&st->ma.tei_m,
				"wrong mgr sapi/tei %x/%x",
				skb->data[0], skb->data[1]);
		} else if ((skb->data[2] & 0xef) != UI) {
			st->ma.tei_m.printdebug(&st->ma.tei_m,
				"mgr frame is not ui %x", skb->data[2]);
		} else {
			skb_pull(skb, 3);
			if (skb->len < 5) {
				st->ma.tei_m.printdebug(&st->ma.tei_m,
					"short mgr frame %ld/5", skb->len);
			} else if (skb->data[0] != TEI_ENTITY_ID) {
				/* wrong management entity identifier, ignore */
				st->ma.tei_m.printdebug(&st->ma.tei_m,
					"tei handler wrong entity id %x",
					skb->data[0]);
			} else {
				mt = skb->data[3];
				if (mt == ID_ASSIGNED)
					FsmEvent(&st->ma.tei_m, EV_ASSIGN, skb);
				else if (mt == ID_DENIED)
					FsmEvent(&st->ma.tei_m, EV_DENIED, skb);
				else if (mt == ID_CHK_REQ)
					FsmEvent(&st->ma.tei_m, EV_CHKREQ, skb);
				else if (mt == ID_REMOVE)
					FsmEvent(&st->ma.tei_m, EV_REMOVE, skb);
				else {
					st->ma.tei_m.printdebug(&st->ma.tei_m,
						"tei handler wrong mt %x\n", mt);
				}
			}
		}
	} else {
		st->ma.tei_m.printdebug(&st->ma.tei_m,
			"tei handler wrong pr %x\n", pr);
	}
	dev_kfree_skb(skb);
}

static void
tei_l2tei(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs;

	if (test_bit(FLG_FIXED_TEI, &st->l2.flag)) {
		if (pr == (MDL_ASSIGN | INDICATION)) {
			if (st->ma.debug)
				st->ma.tei_m.printdebug(&st->ma.tei_m,
					"fixed assign tei %d", st->l2.tei);
			st->l3.l3l2(st, MDL_ASSIGN | REQUEST, (void *) (long) st->l2.tei);
			cs = (struct IsdnCardState *) st->l1.hardware;
			cs->cardmsg(cs, MDL_ASSIGN | REQUEST, NULL);
		}
		return;
	}
	switch (pr) {
		case (MDL_ASSIGN | INDICATION):
			FsmEvent(&st->ma.tei_m, EV_IDREQ, arg);
			break;
		case (MDL_ERROR | REQUEST):
			FsmEvent(&st->ma.tei_m, EV_VERIFY, arg);
			break;
		default:
			break;
	}
}

static void
tei_debug(struct FsmInst *fi, char *fmt, ...)
{
	va_list args;
	struct PStack *st = fi->userdata;

	va_start(args, fmt);
	VHiSax_putstatus(st->l1.hardware, "tei ", fmt, args);
	va_end(args);
}

void
setstack_tei(struct PStack *st)
{
	st->l2.l2tei = tei_l2tei;
	st->ma.T202 = 2000;	/* T202  2000 milliseconds */
	st->l1.l1tei = tei_l1l2;
	st->ma.debug = 1;
	st->ma.tei_m.fsm = &teifsm;
	st->ma.tei_m.state = ST_TEI_NOP;
	st->ma.tei_m.debug = 1;
	st->ma.tei_m.userdata = st;
	st->ma.tei_m.userint = 0;
	st->ma.tei_m.printdebug = tei_debug;
	FsmInitTimer(&st->ma.tei_m, &st->ma.t202);
}

void
init_tei(struct IsdnCardState *cs, int protocol)
{
}

void
release_tei(struct IsdnCardState *cs)
{
	struct PStack *st = cs->stlist;

	while (st) {
		FsmDelTimer(&st->ma.t202, 1);
		st = st->next;
	}
}

static struct FsmNode TeiFnList[] __initdata =
{
	{ST_TEI_NOP, EV_IDREQ, tei_id_request},
	{ST_TEI_NOP, EV_ASSIGN, tei_id_test_dup},
	{ST_TEI_NOP, EV_VERIFY, tei_id_verify},
	{ST_TEI_NOP, EV_REMOVE, tei_id_remove},
	{ST_TEI_NOP, EV_CHKREQ, tei_id_chk_req},
	{ST_TEI_IDREQ, EV_T202, tei_id_req_tout},
	{ST_TEI_IDREQ, EV_ASSIGN, tei_id_assign},
	{ST_TEI_IDREQ, EV_DENIED, tei_id_denied},
	{ST_TEI_IDVERIFY, EV_T202, tei_id_ver_tout},
	{ST_TEI_IDVERIFY, EV_REMOVE, tei_id_remove},
	{ST_TEI_IDVERIFY, EV_CHKREQ, tei_id_chk_req},
};

int __init
TeiNew(void)
{
	teifsm.state_count = TEI_STATE_COUNT;
	teifsm.event_count = TEI_EVENT_COUNT;
	teifsm.strEvent = strTeiEvent;
	teifsm.strState = strTeiState;
	return FsmNew(&teifsm, TeiFnList, ARRAY_SIZE(TeiFnList));
}

void
TeiFree(void)
{
	FsmFree(&teifsm);
}
