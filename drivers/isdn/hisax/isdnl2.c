/* $Id: isdnl2.c,v 2.30.2.4 2004/02/11 13:21:34 keil Exp $
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
#include "hisax.h"
#include "isdnl2.h"

const char *l2_revision = "$Revision: 2.30.2.4 $";

static void l2m_debug(struct FsmInst *fi, char *fmt, ...);

static struct Fsm l2fsm;

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

#define L2_STATE_COUNT (ST_L2_8+1)

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
	EV_L2_DL_UNIT_DATA,
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
	"EV_L2_DL_UNIT_DATA",
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

static int l2addrsize(struct Layer2 *l2);

static void
set_peer_busy(struct Layer2 *l2) {
	test_and_set_bit(FLG_PEER_BUSY, &l2->flag);
	if (!skb_queue_empty(&l2->i_queue) ||
	    !skb_queue_empty(&l2->ui_queue))
		test_and_set_bit(FLG_L2BLOCK, &l2->flag);
}

static void
clear_peer_busy(struct Layer2 *l2) {
	if (test_and_clear_bit(FLG_PEER_BUSY, &l2->flag))
		test_and_clear_bit(FLG_L2BLOCK, &l2->flag);
}

static void
InitWin(struct Layer2 *l2)
{
	int i;

	for (i = 0; i < MAX_WINDOW; i++)
		l2->windowar[i] = NULL;
}

static int
freewin1(struct Layer2 *l2)
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

static inline void
freewin(struct PStack *st)
{
	freewin1(&st->l2);
}

static void
ReleaseWin(struct Layer2 *l2)
{
	int cnt;

	if((cnt = freewin1(l2)))
		printk(KERN_WARNING "isdl2 freed %d skbuffs in release\n", cnt);
}

static inline unsigned int
cansend(struct PStack *st)
{
	unsigned int p1;

	if(test_bit(FLG_MOD128, &st->l2.flag))
		p1 = (st->l2.vs - st->l2.va) % 128;
	else
		p1 = (st->l2.vs - st->l2.va) % 8;
	return ((p1 < st->l2.window) && !test_bit(FLG_PEER_BUSY, &st->l2.flag));
}

static inline void
clear_exception(struct Layer2 *l2)
{
	test_and_clear_bit(FLG_ACK_PEND, &l2->flag);
	test_and_clear_bit(FLG_REJEXC, &l2->flag);
	test_and_clear_bit(FLG_OWN_BUSY, &l2->flag);
	clear_peer_busy(l2);
}

static inline int
l2headersize(struct Layer2 *l2, int ui)
{
	return (((test_bit(FLG_MOD128, &l2->flag) && (!ui)) ? 2 : 1) +
		(test_bit(FLG_LAPD, &l2->flag) ? 2 : 1));
}

inline int
l2addrsize(struct Layer2 *l2)
{
	return (test_bit(FLG_LAPD, &l2->flag) ? 2 : 1);
}

static int
sethdraddr(struct Layer2 *l2, u_char * header, int rsp)
{
	u_char *ptr = header;
	int crbit = rsp;

	if (test_bit(FLG_LAPD, &l2->flag)) {
		*ptr++ = (l2->sap << 2) | (rsp ? 2 : 0);
		*ptr++ = (l2->tei << 1) | 1;
		return (2);
	} else {
		if (test_bit(FLG_ORIG, &l2->flag))
			crbit = !crbit;
		if (crbit)
			*ptr++ = 1;
		else
			*ptr++ = 3;
		return (1);
	}
}

static inline void
enqueue_super(struct PStack *st,
	      struct sk_buff *skb)
{
	if (test_bit(FLG_LAPB, &st->l2.flag))
		st->l1.bcs->tx_cnt += skb->len;
	st->l2.l2l1(st, PH_DATA | REQUEST, skb);
}

#define enqueue_ui(a, b) enqueue_super(a, b)

static inline int
IsUI(u_char * data)
{
	return ((data[0] & 0xef) == UI);
}

static inline int
IsUA(u_char * data)
{
	return ((data[0] & 0xef) == UA);
}

static inline int
IsDM(u_char * data)
{
	return ((data[0] & 0xef) == DM);
}

static inline int
IsDISC(u_char * data)
{
	return ((data[0] & 0xef) == DISC);
}

static inline int
IsSFrame(u_char * data, struct PStack *st)
{
	register u_char d = *data;
	
	if (!test_bit(FLG_MOD128, &st->l2.flag))
		d &= 0xf;
	return(((d & 0xf3) == 1) && ((d & 0x0c) != 0x0c));
}

static inline int
IsSABME(u_char * data, struct PStack *st)
{
	u_char d = data[0] & ~0x10;

	return (test_bit(FLG_MOD128, &st->l2.flag) ? d == SABME : d == SABM);
}

static inline int
IsREJ(u_char * data, struct PStack *st)
{
	return (test_bit(FLG_MOD128, &st->l2.flag) ? data[0] == REJ : (data[0] & 0xf) == REJ);
}

static inline int
IsFRMR(u_char * data)
{
	return ((data[0] & 0xef) == FRMR);
}

static inline int
IsRNR(u_char * data, struct PStack *st)
{
	return (test_bit(FLG_MOD128, &st->l2.flag) ? data[0] == RNR : (data[0] & 0xf) == RNR);
}

static int
iframe_error(struct PStack *st, struct sk_buff *skb)
{
	int i = l2addrsize(&st->l2) + (test_bit(FLG_MOD128, &st->l2.flag) ? 2 : 1);
	int rsp = *skb->data & 0x2;

	if (test_bit(FLG_ORIG, &st->l2.flag))
		rsp = !rsp;

	if (rsp)
		return 'L';


	if (skb->len < i)
		return 'N';

	if ((skb->len - i) > st->l2.maxlen)
		return 'O';


	return 0;
}

static int
super_error(struct PStack *st, struct sk_buff *skb)
{
	if (skb->len != l2addrsize(&st->l2) +
	    (test_bit(FLG_MOD128, &st->l2.flag) ? 2 : 1))
		return 'N';

	return 0;
}

static int
unnum_error(struct PStack *st, struct sk_buff *skb, int wantrsp)
{
	int rsp = (*skb->data & 0x2) >> 1;
	if (test_bit(FLG_ORIG, &st->l2.flag))
		rsp = !rsp;

	if (rsp != wantrsp)
		return 'L';

	if (skb->len != l2addrsize(&st->l2) + 1)
		return 'N';

	return 0;
}

static int
UI_error(struct PStack *st, struct sk_buff *skb)
{
	int rsp = *skb->data & 0x2;
	if (test_bit(FLG_ORIG, &st->l2.flag))
		rsp = !rsp;

	if (rsp)
		return 'L';

	if (skb->len > st->l2.maxlen + l2addrsize(&st->l2) + 1)
		return 'O';

	return 0;
}

static int
FRMR_error(struct PStack *st, struct sk_buff *skb)
{
	int headers = l2addrsize(&st->l2) + 1;
	u_char *datap = skb->data + headers;
	int rsp = *skb->data & 0x2;

	if (test_bit(FLG_ORIG, &st->l2.flag))
		rsp = !rsp;

	if (!rsp)
		return 'L';

	if (test_bit(FLG_MOD128, &st->l2.flag)) {
		if (skb->len < headers + 5)
			return 'N';
		else
			l2m_debug(&st->l2.l2m, "FRMR information %2x %2x %2x %2x %2x",
				datap[0], datap[1], datap[2],
				datap[3], datap[4]);
	} else {
		if (skb->len < headers + 3)
			return 'N';
		else
			l2m_debug(&st->l2.l2m, "FRMR information %2x %2x %2x",
				datap[0], datap[1], datap[2]);
	}

	return 0;
}

static unsigned int
legalnr(struct PStack *st, unsigned int nr)
{
        struct Layer2 *l2 = &st->l2;

	if(test_bit(FLG_MOD128, &l2->flag))
		return ((nr - l2->va) % 128) <= ((l2->vs - l2->va) % 128);
	else
		return ((nr - l2->va) % 8) <= ((l2->vs - l2->va) % 8);
}

static void
setva(struct PStack *st, unsigned int nr)
{
	struct Layer2 *l2 = &st->l2;
	int len;
	u_long flags;

	spin_lock_irqsave(&l2->lock, flags);
	while (l2->va != nr) {
		(l2->va)++;
		if(test_bit(FLG_MOD128, &l2->flag))
			l2->va %= 128;
		else
			l2->va %= 8;
		len = l2->windowar[l2->sow]->len;
		if (PACKET_NOACK == l2->windowar[l2->sow]->pkt_type)
			len = -1;
		dev_kfree_skb(l2->windowar[l2->sow]);
		l2->windowar[l2->sow] = NULL;
		l2->sow = (l2->sow + 1) % l2->window;
		spin_unlock_irqrestore(&l2->lock, flags);
		if (test_bit(FLG_LLI_L2WAKEUP, &st->lli.flag) && (len >=0))
			lli_writewakeup(st, len);
		spin_lock_irqsave(&l2->lock, flags);
	}
	spin_unlock_irqrestore(&l2->lock, flags);
}

static void
send_uframe(struct PStack *st, u_char cmd, u_char cr)
{
	struct sk_buff *skb;
	u_char tmp[MAX_HEADER_LEN];
	int i;

	i = sethdraddr(&st->l2, tmp, cr);
	tmp[i++] = cmd;
	if (!(skb = alloc_skb(i, GFP_ATOMIC))) {
		printk(KERN_WARNING "isdl2 can't alloc sbbuff for send_uframe\n");
		return;
	}
	memcpy(skb_put(skb, i), tmp, i);
	enqueue_super(st, skb);
}

static inline u_char
get_PollFlag(struct PStack * st, struct sk_buff * skb)
{
	return (skb->data[l2addrsize(&(st->l2))] & 0x10);
}

static inline u_char
get_PollFlagFree(struct PStack *st, struct sk_buff *skb)
{
	u_char PF;

	PF = get_PollFlag(st, skb);
	dev_kfree_skb(skb);
	return (PF);
}

static inline void
start_t200(struct PStack *st, int i)
{
	FsmAddTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, i);
	test_and_set_bit(FLG_T200_RUN, &st->l2.flag);
}

static inline void
restart_t200(struct PStack *st, int i)
{
	FsmRestartTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, i);
	test_and_set_bit(FLG_T200_RUN, &st->l2.flag);
}

static inline void
stop_t200(struct PStack *st, int i)
{
	if(test_and_clear_bit(FLG_T200_RUN, &st->l2.flag))
		FsmDelTimer(&st->l2.t200, i);
}

static inline void
st5_dl_release_l2l3(struct PStack *st)
{
		int pr;

		if(test_and_clear_bit(FLG_PEND_REL, &st->l2.flag))
			pr = DL_RELEASE | CONFIRM;
		else
			pr = DL_RELEASE | INDICATION;

		st->l2.l2l3(st, pr, NULL);
}

static inline void
lapb_dl_release_l2l3(struct PStack *st, int f)
{
		if (test_bit(FLG_LAPB, &st->l2.flag))
			st->l2.l2l1(st, PH_DEACTIVATE | REQUEST, NULL);
		st->l2.l2l3(st, DL_RELEASE | f, NULL);
}

static void
establishlink(struct FsmInst *fi)
{
	struct PStack *st = fi->userdata;
	u_char cmd;

	clear_exception(&st->l2);
	st->l2.rc = 0;
	cmd = (test_bit(FLG_MOD128, &st->l2.flag) ? SABME : SABM) | 0x10;
	send_uframe(st, cmd, CMD);
	FsmDelTimer(&st->l2.t203, 1);
	restart_t200(st, 1);
	test_and_clear_bit(FLG_PEND_REL, &st->l2.flag);
	freewin(st);
	FsmChangeState(fi, ST_L2_5);
}

static void
l2_mdl_error_ua(struct FsmInst *fi, int event, void *arg)
{
	struct sk_buff *skb = arg;
	struct PStack *st = fi->userdata;

	if (get_PollFlagFree(st, skb))
		st->ma.layer(st, MDL_ERROR | INDICATION, (void *) 'C');
	else
		st->ma.layer(st, MDL_ERROR | INDICATION, (void *) 'D');
}

static void
l2_mdl_error_dm(struct FsmInst *fi, int event, void *arg)
{
	struct sk_buff *skb = arg;
	struct PStack *st = fi->userdata;

	if (get_PollFlagFree(st, skb))
		st->ma.layer(st, MDL_ERROR | INDICATION, (void *) 'B');
	else {
		st->ma.layer(st, MDL_ERROR | INDICATION, (void *) 'E');
		establishlink(fi);
		test_and_clear_bit(FLG_L3_INIT, &st->l2.flag);
	}
}

static void
l2_st8_mdl_error_dm(struct FsmInst *fi, int event, void *arg)
{
	struct sk_buff *skb = arg;
	struct PStack *st = fi->userdata;

	if (get_PollFlagFree(st, skb))
		st->ma.layer(st, MDL_ERROR | INDICATION, (void *) 'B');
	else {
		st->ma.layer(st, MDL_ERROR | INDICATION, (void *) 'E');
	}
	establishlink(fi);
	test_and_clear_bit(FLG_L3_INIT, &st->l2.flag);
}

static void
l2_go_st3(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L2_3); 
}

static void
l2_mdl_assign(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	FsmChangeState(fi, ST_L2_3); 
	st->l2.l2tei(st, MDL_ASSIGN | INDICATION, NULL);
}

static void
l2_queue_ui_assign(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	skb_queue_tail(&st->l2.ui_queue, skb);
	FsmChangeState(fi, ST_L2_2);
	st->l2.l2tei(st, MDL_ASSIGN | INDICATION, NULL);
}

static void
l2_queue_ui(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	skb_queue_tail(&st->l2.ui_queue, skb);
}

static void
tx_ui(struct PStack *st)
{
	struct sk_buff *skb;
	u_char header[MAX_HEADER_LEN];
	int i;

	i = sethdraddr(&(st->l2), header, CMD);
	header[i++] = UI;
	while ((skb = skb_dequeue(&st->l2.ui_queue))) {
		memcpy(skb_push(skb, i), header, i);
		enqueue_ui(st, skb);
	}
}

static void
l2_send_ui(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	skb_queue_tail(&st->l2.ui_queue, skb);
	tx_ui(st);
}

static void
l2_got_ui(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	skb_pull(skb, l2headersize(&st->l2, 1));
	st->l2.l2l3(st, DL_UNIT_DATA | INDICATION, skb);
/*	^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *		in states 1-3 for broadcast
 */


}

static void
l2_establish(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	establishlink(fi);
	test_and_set_bit(FLG_L3_INIT, &st->l2.flag);
}

static void
l2_discard_i_setl3(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	skb_queue_purge(&st->l2.i_queue);
	test_and_set_bit(FLG_L3_INIT, &st->l2.flag);
	test_and_clear_bit(FLG_PEND_REL, &st->l2.flag);
}

static void
l2_l3_reestablish(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	skb_queue_purge(&st->l2.i_queue);
	establishlink(fi);
	test_and_set_bit(FLG_L3_INIT, &st->l2.flag);
}

static void
l2_release(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	st->l2.l2l3(st, DL_RELEASE | CONFIRM, NULL);
}

static void
l2_pend_rel(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	test_and_set_bit(FLG_PEND_REL, &st->l2.flag);
}

static void
l2_disconnect(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	skb_queue_purge(&st->l2.i_queue);
	freewin(st);
	FsmChangeState(fi, ST_L2_6);
	st->l2.rc = 0;
	send_uframe(st, DISC | 0x10, CMD);
	FsmDelTimer(&st->l2.t203, 1);
	restart_t200(st, 2);
}

static void
l2_start_multi(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	send_uframe(st, UA | get_PollFlagFree(st, skb), RSP);

	clear_exception(&st->l2);
	st->l2.vs = 0;
	st->l2.va = 0;
	st->l2.vr = 0;
	st->l2.sow = 0;
	FsmChangeState(fi, ST_L2_7);
	FsmAddTimer(&st->l2.t203, st->l2.T203, EV_L2_T203, NULL, 3);

	st->l2.l2l3(st, DL_ESTABLISH | INDICATION, NULL);
}

static void
l2_send_UA(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	send_uframe(st, UA | get_PollFlagFree(st, skb), RSP);
}

static void
l2_send_DM(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	send_uframe(st, DM | get_PollFlagFree(st, skb), RSP);
}

static void
l2_restart_multi(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int est = 0, state;

	state = fi->state;

	send_uframe(st, UA | get_PollFlagFree(st, skb), RSP);

	st->ma.layer(st, MDL_ERROR | INDICATION, (void *) 'F');

	if (st->l2.vs != st->l2.va) {
		skb_queue_purge(&st->l2.i_queue);
		est = 1;
	}

	clear_exception(&st->l2);
	st->l2.vs = 0;
	st->l2.va = 0;
	st->l2.vr = 0;
	st->l2.sow = 0;
	FsmChangeState(fi, ST_L2_7);
	stop_t200(st, 3);
	FsmRestartTimer(&st->l2.t203, st->l2.T203, EV_L2_T203, NULL, 3);

	if (est)
		st->l2.l2l3(st, DL_ESTABLISH | INDICATION, NULL);

	if ((ST_L2_7==state) || (ST_L2_8 == state))
		if (!skb_queue_empty(&st->l2.i_queue) && cansend(st))
			st->l2.l2l1(st, PH_PULL | REQUEST, NULL);
}

static void
l2_stop_multi(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	FsmChangeState(fi, ST_L2_4);
	FsmDelTimer(&st->l2.t203, 3);
	stop_t200(st, 4);

	send_uframe(st, UA | get_PollFlagFree(st, skb), RSP);

	skb_queue_purge(&st->l2.i_queue);
	freewin(st);
	lapb_dl_release_l2l3(st, INDICATION);
}

static void
l2_connected(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int pr=-1;

	if (!get_PollFlag(st, skb)) {
		l2_mdl_error_ua(fi, event, arg);
		return;
	}
	dev_kfree_skb(skb);

	if (test_and_clear_bit(FLG_PEND_REL, &st->l2.flag))
		l2_disconnect(fi, event, arg);

	if (test_and_clear_bit(FLG_L3_INIT, &st->l2.flag)) {
		pr = DL_ESTABLISH | CONFIRM;
	} else if (st->l2.vs != st->l2.va) {
		skb_queue_purge(&st->l2.i_queue);
		pr = DL_ESTABLISH | INDICATION;
	}

	stop_t200(st, 5);

	st->l2.vr = 0;
	st->l2.vs = 0;
	st->l2.va = 0;
	st->l2.sow = 0;
	FsmChangeState(fi, ST_L2_7);
	FsmAddTimer(&st->l2.t203, st->l2.T203, EV_L2_T203, NULL, 4);

	if (pr != -1)
		st->l2.l2l3(st, pr, NULL);

	if (!skb_queue_empty(&st->l2.i_queue) && cansend(st))
		st->l2.l2l1(st, PH_PULL | REQUEST, NULL);
}

static void
l2_released(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	if (!get_PollFlag(st, skb)) {
		l2_mdl_error_ua(fi, event, arg);
		return;
	}
	dev_kfree_skb(skb);

	stop_t200(st, 6);
	lapb_dl_release_l2l3(st, CONFIRM);
	FsmChangeState(fi, ST_L2_4);
}

static void
l2_reestablish(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	if (!get_PollFlagFree(st, skb)) {
		establishlink(fi);
		test_and_set_bit(FLG_L3_INIT, &st->l2.flag);
	}
}

static void
l2_st5_dm_release(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	if (get_PollFlagFree(st, skb)) {
		stop_t200(st, 7);
	 	if (!test_bit(FLG_L3_INIT, &st->l2.flag))
			skb_queue_purge(&st->l2.i_queue);
		if (test_bit(FLG_LAPB, &st->l2.flag))
			st->l2.l2l1(st, PH_DEACTIVATE | REQUEST, NULL);
		st5_dl_release_l2l3(st);
		FsmChangeState(fi, ST_L2_4);
	}
}

static void
l2_st6_dm_release(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	if (get_PollFlagFree(st, skb)) {
		stop_t200(st, 8);
		lapb_dl_release_l2l3(st, CONFIRM);
		FsmChangeState(fi, ST_L2_4);
	}
}

static inline void
enquiry_cr(struct PStack *st, u_char typ, u_char cr, u_char pf)
{
	struct sk_buff *skb;
	struct Layer2 *l2;
	u_char tmp[MAX_HEADER_LEN];
	int i;

	l2 = &st->l2;
	i = sethdraddr(l2, tmp, cr);
	if (test_bit(FLG_MOD128, &l2->flag)) {
		tmp[i++] = typ;
		tmp[i++] = (l2->vr << 1) | (pf ? 1 : 0);
	} else
		tmp[i++] = (l2->vr << 5) | typ | (pf ? 0x10 : 0);
	if (!(skb = alloc_skb(i, GFP_ATOMIC))) {
		printk(KERN_WARNING "isdl2 can't alloc sbbuff for enquiry_cr\n");
		return;
	}
	memcpy(skb_put(skb, i), tmp, i);
	enqueue_super(st, skb);
}

static inline void
enquiry_response(struct PStack *st)
{
	if (test_bit(FLG_OWN_BUSY, &st->l2.flag))
		enquiry_cr(st, RNR, RSP, 1);
	else
		enquiry_cr(st, RR, RSP, 1);
	test_and_clear_bit(FLG_ACK_PEND, &st->l2.flag);
}

static inline void
transmit_enquiry(struct PStack *st)
{
	if (test_bit(FLG_OWN_BUSY, &st->l2.flag))
		enquiry_cr(st, RNR, CMD, 1);
	else
		enquiry_cr(st, RR, CMD, 1);
	test_and_clear_bit(FLG_ACK_PEND, &st->l2.flag);
	start_t200(st, 9);
}


static void
nrerrorrecovery(struct FsmInst *fi)
{
	struct PStack *st = fi->userdata;

	st->ma.layer(st, MDL_ERROR | INDICATION, (void *) 'J');
	establishlink(fi);
	test_and_clear_bit(FLG_L3_INIT, &st->l2.flag);
}

static void
invoke_retransmission(struct PStack *st, unsigned int nr)
{
	struct Layer2 *l2 = &st->l2;
	u_int p1;
	u_long flags;

	spin_lock_irqsave(&l2->lock, flags);
	if (l2->vs != nr) {
		while (l2->vs != nr) {
			(l2->vs)--;
			if(test_bit(FLG_MOD128, &l2->flag)) {
				l2->vs %= 128;
				p1 = (l2->vs - l2->va) % 128;
			} else {
				l2->vs %= 8;
				p1 = (l2->vs - l2->va) % 8;
			}
			p1 = (p1 + l2->sow) % l2->window;
			if (test_bit(FLG_LAPB, &l2->flag))
				st->l1.bcs->tx_cnt += l2->windowar[p1]->len + l2headersize(l2, 0);
			skb_queue_head(&l2->i_queue, l2->windowar[p1]);
			l2->windowar[p1] = NULL;
		}
		spin_unlock_irqrestore(&l2->lock, flags);
		st->l2.l2l1(st, PH_PULL | REQUEST, NULL);
		return;
	}
	spin_unlock_irqrestore(&l2->lock, flags);
}

static void
l2_st7_got_super(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int PollFlag, rsp, typ = RR;
	unsigned int nr;
	struct Layer2 *l2 = &st->l2;

	rsp = *skb->data & 0x2;
	if (test_bit(FLG_ORIG, &l2->flag))
		rsp = !rsp;

	skb_pull(skb, l2addrsize(l2));
	if (IsRNR(skb->data, st)) {
		set_peer_busy(l2);
		typ = RNR;
	} else
		clear_peer_busy(l2);
	if (IsREJ(skb->data, st))
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
			st->ma.layer(st, MDL_ERROR | INDICATION, (void *) 'A');
		else
			enquiry_response(st);
	}
	if (legalnr(st, nr)) {
		if (typ == REJ) {
			setva(st, nr);
			invoke_retransmission(st, nr);
			stop_t200(st, 10);
			if (FsmAddTimer(&st->l2.t203, st->l2.T203,
					EV_L2_T203, NULL, 6))
				l2m_debug(&st->l2.l2m, "Restart T203 ST7 REJ");
		} else if ((nr == l2->vs) && (typ == RR)) {
			setva(st, nr);
			stop_t200(st, 11);
			FsmRestartTimer(&st->l2.t203, st->l2.T203,
					EV_L2_T203, NULL, 7);
		} else if ((l2->va != nr) || (typ == RNR)) {
			setva(st, nr);
			if(typ != RR) FsmDelTimer(&st->l2.t203, 9);
			restart_t200(st, 12);
		}
		if (!skb_queue_empty(&st->l2.i_queue) && (typ == RR))
			st->l2.l2l1(st, PH_PULL | REQUEST, NULL);
	} else
		nrerrorrecovery(fi);
}

static void
l2_feed_i_if_reest(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	if (test_bit(FLG_LAPB, &st->l2.flag))
		st->l1.bcs->tx_cnt += skb->len + l2headersize(&st->l2, 0);
	if (!test_bit(FLG_L3_INIT, &st->l2.flag))
		skb_queue_tail(&st->l2.i_queue, skb);
	else
		dev_kfree_skb(skb);
}

static void
l2_feed_i_pull(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	if (test_bit(FLG_LAPB, &st->l2.flag))
		st->l1.bcs->tx_cnt += skb->len + l2headersize(&st->l2, 0);
	skb_queue_tail(&st->l2.i_queue, skb);
	st->l2.l2l1(st, PH_PULL | REQUEST, NULL);
}

static void
l2_feed_iqueue(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	if (test_bit(FLG_LAPB, &st->l2.flag))
		st->l1.bcs->tx_cnt += skb->len + l2headersize(&st->l2, 0);
	skb_queue_tail(&st->l2.i_queue, skb);
}

static void
l2_got_iframe(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	struct Layer2 *l2 = &(st->l2);
	int PollFlag, ns, i;
	unsigned int nr;

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
		if(PollFlag) enquiry_response(st);
	} else if (l2->vr == ns) {
		(l2->vr)++;
		if(test_bit(FLG_MOD128, &l2->flag))
			l2->vr %= 128;
		else
			l2->vr %= 8;
		test_and_clear_bit(FLG_REJEXC, &l2->flag);

		if (PollFlag)
			enquiry_response(st);
		else
			test_and_set_bit(FLG_ACK_PEND, &l2->flag);
		skb_pull(skb, l2headersize(l2, 0));
		st->l2.l2l3(st, DL_DATA | INDICATION, skb);
	} else {
		/* n(s)!=v(r) */
		dev_kfree_skb(skb);
		if (test_and_set_bit(FLG_REJEXC, &l2->flag)) {
			if (PollFlag)
				enquiry_response(st);
		} else {
			enquiry_cr(st, REJ, RSP, PollFlag);
			test_and_clear_bit(FLG_ACK_PEND, &l2->flag);
		}
	}

	if (legalnr(st, nr)) {
		if (!test_bit(FLG_PEER_BUSY, &st->l2.flag) && (fi->state == ST_L2_7)) {
			if (nr == st->l2.vs) {
				stop_t200(st, 13);
				FsmRestartTimer(&st->l2.t203, st->l2.T203,
						EV_L2_T203, NULL, 7);
			} else if (nr != st->l2.va)
				restart_t200(st, 14);
		}
		setva(st, nr);
	} else {
		nrerrorrecovery(fi);
		return;
	}

	if (!skb_queue_empty(&st->l2.i_queue) && (fi->state == ST_L2_7))
		st->l2.l2l1(st, PH_PULL | REQUEST, NULL);
	if (test_and_clear_bit(FLG_ACK_PEND, &st->l2.flag))
		enquiry_cr(st, RR, RSP, 0);
}

static void
l2_got_tei(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	st->l2.tei = (long) arg;

	if (fi->state == ST_L2_3) {
		establishlink(fi);
		test_and_set_bit(FLG_L3_INIT, &st->l2.flag);
	} else
		FsmChangeState(fi, ST_L2_4);
	if (!skb_queue_empty(&st->l2.ui_queue))
		tx_ui(st);
}

static void
l2_st5_tout_200(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (test_bit(FLG_LAPD, &st->l2.flag) &&
		test_bit(FLG_DCHAN_BUSY, &st->l2.flag)) {
		FsmAddTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, 9);
	} else if (st->l2.rc == st->l2.N200) {
		FsmChangeState(fi, ST_L2_4);
		test_and_clear_bit(FLG_T200_RUN, &st->l2.flag);
		skb_queue_purge(&st->l2.i_queue);
		st->ma.layer(st, MDL_ERROR | INDICATION, (void *) 'G');
		if (test_bit(FLG_LAPB, &st->l2.flag))
			st->l2.l2l1(st, PH_DEACTIVATE | REQUEST, NULL);
		st5_dl_release_l2l3(st);
	} else {
		st->l2.rc++;
		FsmAddTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, 9);
		send_uframe(st, (test_bit(FLG_MOD128, &st->l2.flag) ? SABME : SABM)
			    | 0x10, CMD);
	}
}

static void
l2_st6_tout_200(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (test_bit(FLG_LAPD, &st->l2.flag) &&
		test_bit(FLG_DCHAN_BUSY, &st->l2.flag)) {
		FsmAddTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, 9);
	} else if (st->l2.rc == st->l2.N200) {
		FsmChangeState(fi, ST_L2_4);
		test_and_clear_bit(FLG_T200_RUN, &st->l2.flag);
		st->ma.layer(st, MDL_ERROR | INDICATION, (void *) 'H');
		lapb_dl_release_l2l3(st, CONFIRM);
	} else {
		st->l2.rc++;
		FsmAddTimer(&st->l2.t200, st->l2.T200, EV_L2_T200,
			    NULL, 9);
		send_uframe(st, DISC | 0x10, CMD);
	}
}

static void
l2_st7_tout_200(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (test_bit(FLG_LAPD, &st->l2.flag) &&
		test_bit(FLG_DCHAN_BUSY, &st->l2.flag)) {
		FsmAddTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, 9);
		return;
	}
	test_and_clear_bit(FLG_T200_RUN, &st->l2.flag);
	st->l2.rc = 0;
	FsmChangeState(fi, ST_L2_8);

	transmit_enquiry(st);
	st->l2.rc++;
}

static void
l2_st8_tout_200(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (test_bit(FLG_LAPD, &st->l2.flag) &&
		test_bit(FLG_DCHAN_BUSY, &st->l2.flag)) {
		FsmAddTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, 9);
		return;
	}
	test_and_clear_bit(FLG_T200_RUN, &st->l2.flag);
	if (st->l2.rc == st->l2.N200) {
		st->ma.layer(st, MDL_ERROR | INDICATION, (void *) 'I');
		establishlink(fi);
		test_and_clear_bit(FLG_L3_INIT, &st->l2.flag);
	} else {
		transmit_enquiry(st);
		st->l2.rc++;
	}
}

static void
l2_st7_tout_203(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if (test_bit(FLG_LAPD, &st->l2.flag) &&
		test_bit(FLG_DCHAN_BUSY, &st->l2.flag)) {
		FsmAddTimer(&st->l2.t203, st->l2.T203, EV_L2_T203, NULL, 9);
		return;
	}
	FsmChangeState(fi, ST_L2_8);
	transmit_enquiry(st);
	st->l2.rc = 0;
}

static void
l2_pull_iqueue(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb, *oskb;
	struct Layer2 *l2 = &st->l2;
	u_char header[MAX_HEADER_LEN];
	int i;
	int unsigned p1;
	u_long flags;

	if (!cansend(st))
		return;

	skb = skb_dequeue(&l2->i_queue);
	if (!skb)
		return;

	spin_lock_irqsave(&l2->lock, flags);
	if(test_bit(FLG_MOD128, &l2->flag))
		p1 = (l2->vs - l2->va) % 128;
	else
		p1 = (l2->vs - l2->va) % 8;
	p1 = (p1 + l2->sow) % l2->window;
	if (l2->windowar[p1]) {
		printk(KERN_WARNING "isdnl2 try overwrite ack queue entry %d\n",
		       p1);
		dev_kfree_skb(l2->windowar[p1]);
	}
	l2->windowar[p1] = skb_clone(skb, GFP_ATOMIC);

	i = sethdraddr(&st->l2, header, CMD);

	if (test_bit(FLG_MOD128, &l2->flag)) {
		header[i++] = l2->vs << 1;
		header[i++] = l2->vr << 1;
		l2->vs = (l2->vs + 1) % 128;
	} else {
		header[i++] = (l2->vr << 5) | (l2->vs << 1);
		l2->vs = (l2->vs + 1) % 8;
	}
	spin_unlock_irqrestore(&l2->lock, flags);
	p1 = skb->data - skb->head;
	if (p1 >= i)
		memcpy(skb_push(skb, i), header, i);
	else {
		printk(KERN_WARNING
		"isdl2 pull_iqueue skb header(%d/%d) too short\n", i, p1);
		oskb = skb;
		skb = alloc_skb(oskb->len + i, GFP_ATOMIC);
		memcpy(skb_put(skb, i), header, i);
		memcpy(skb_put(skb, oskb->len), oskb->data, oskb->len);
		dev_kfree_skb(oskb);
	}
	st->l2.l2l1(st, PH_PULL | INDICATION, skb);
	test_and_clear_bit(FLG_ACK_PEND, &st->l2.flag);
	if (!test_and_set_bit(FLG_T200_RUN, &st->l2.flag)) {
		FsmDelTimer(&st->l2.t203, 13);
		FsmAddTimer(&st->l2.t200, st->l2.T200, EV_L2_T200, NULL, 11);
	}
	if (!skb_queue_empty(&l2->i_queue) && cansend(st))
		st->l2.l2l1(st, PH_PULL | REQUEST, NULL);
}

static void
l2_st8_got_super(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;
	int PollFlag, rsp, rnr = 0;
	unsigned int nr;
	struct Layer2 *l2 = &st->l2;

	rsp = *skb->data & 0x2;
	if (test_bit(FLG_ORIG, &l2->flag))
		rsp = !rsp;

	skb_pull(skb, l2addrsize(l2));

	if (IsRNR(skb->data, st)) {
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
		if (legalnr(st, nr)) {
			if (rnr) {
				restart_t200(st, 15);
			} else {
				stop_t200(st, 16);
				FsmAddTimer(&l2->t203, l2->T203,
					    EV_L2_T203, NULL, 5);
				setva(st, nr);
			}
			invoke_retransmission(st, nr);
			FsmChangeState(fi, ST_L2_7);
			if (!skb_queue_empty(&l2->i_queue) && cansend(st))
				st->l2.l2l1(st, PH_PULL | REQUEST, NULL);
		} else
			nrerrorrecovery(fi);
	} else {
		if (!rsp && PollFlag)
			enquiry_response(st);
		if (legalnr(st, nr)) {
			setva(st, nr);
		} else
			nrerrorrecovery(fi);
	}
}

static void
l2_got_FRMR(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	struct sk_buff *skb = arg;

	skb_pull(skb, l2addrsize(&st->l2) + 1);

	if (!(skb->data[0] & 1) || ((skb->data[0] & 3) == 1) ||		/* I or S */
	    (IsUA(skb->data) && (fi->state == ST_L2_7))) {
		st->ma.layer(st, MDL_ERROR | INDICATION, (void *) 'K');
		establishlink(fi);
		test_and_clear_bit(FLG_L3_INIT, &st->l2.flag);
	}
	dev_kfree_skb(skb);
}

static void
l2_st24_tei_remove(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	skb_queue_purge(&st->l2.ui_queue);
	st->l2.tei = -1;
	FsmChangeState(fi, ST_L2_1);
}

static void
l2_st3_tei_remove(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	skb_queue_purge(&st->l2.ui_queue);
	st->l2.tei = -1;
	st->l2.l2l3(st, DL_RELEASE | INDICATION, NULL);
	FsmChangeState(fi, ST_L2_1);
}

static void
l2_st5_tei_remove(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	skb_queue_purge(&st->l2.i_queue);
	skb_queue_purge(&st->l2.ui_queue);
	freewin(st);
	st->l2.tei = -1;
	stop_t200(st, 17);
	st5_dl_release_l2l3(st);
	FsmChangeState(fi, ST_L2_1);
}

static void
l2_st6_tei_remove(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	skb_queue_purge(&st->l2.ui_queue);
	st->l2.tei = -1;
	stop_t200(st, 18);
	st->l2.l2l3(st, DL_RELEASE | CONFIRM, NULL);
	FsmChangeState(fi, ST_L2_1);
}

static void
l2_tei_remove(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	skb_queue_purge(&st->l2.i_queue);
	skb_queue_purge(&st->l2.ui_queue);
	freewin(st);
	st->l2.tei = -1;
	stop_t200(st, 17);
	FsmDelTimer(&st->l2.t203, 19);
	st->l2.l2l3(st, DL_RELEASE | INDICATION, NULL);
	FsmChangeState(fi, ST_L2_1);
}

static void
l2_st14_persistent_da(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;
	
	skb_queue_purge(&st->l2.i_queue);
	skb_queue_purge(&st->l2.ui_queue);
	if (test_and_clear_bit(FLG_ESTAB_PEND, &st->l2.flag))
		st->l2.l2l3(st, DL_RELEASE | INDICATION, NULL);
}

static void
l2_st5_persistent_da(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	skb_queue_purge(&st->l2.i_queue);
	skb_queue_purge(&st->l2.ui_queue);
	freewin(st);
	stop_t200(st, 19);
	st5_dl_release_l2l3(st);
	FsmChangeState(fi, ST_L2_4);
}

static void
l2_st6_persistent_da(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	skb_queue_purge(&st->l2.ui_queue);
	stop_t200(st, 20);
	st->l2.l2l3(st, DL_RELEASE | CONFIRM, NULL);
	FsmChangeState(fi, ST_L2_4);
}

static void
l2_persistent_da(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	skb_queue_purge(&st->l2.i_queue);
	skb_queue_purge(&st->l2.ui_queue);
	freewin(st);
	stop_t200(st, 19);
	FsmDelTimer(&st->l2.t203, 19);
	st->l2.l2l3(st, DL_RELEASE | INDICATION, NULL);
	FsmChangeState(fi, ST_L2_4);
}

static void
l2_set_own_busy(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if(!test_and_set_bit(FLG_OWN_BUSY, &st->l2.flag)) {
		enquiry_cr(st, RNR, RSP, 0);
		test_and_clear_bit(FLG_ACK_PEND, &st->l2.flag);
	}
}

static void
l2_clear_own_busy(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	if(!test_and_clear_bit(FLG_OWN_BUSY, &st->l2.flag)) {
		enquiry_cr(st, RR, RSP, 0);
		test_and_clear_bit(FLG_ACK_PEND, &st->l2.flag);
	}
}

static void
l2_frame_error(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	st->ma.layer(st, MDL_ERROR | INDICATION, arg);
}

static void
l2_frame_error_reest(struct FsmInst *fi, int event, void *arg)
{
	struct PStack *st = fi->userdata;

	st->ma.layer(st, MDL_ERROR | INDICATION, arg);
	establishlink(fi);
	test_and_clear_bit(FLG_L3_INIT, &st->l2.flag);
}

static struct FsmNode L2FnList[] __initdata =
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
	{ST_L2_1, EV_L2_DL_UNIT_DATA, l2_queue_ui_assign},
	{ST_L2_2, EV_L2_DL_UNIT_DATA, l2_queue_ui},
	{ST_L2_3, EV_L2_DL_UNIT_DATA, l2_queue_ui},
	{ST_L2_4, EV_L2_DL_UNIT_DATA, l2_send_ui},
	{ST_L2_5, EV_L2_DL_UNIT_DATA, l2_send_ui},
	{ST_L2_6, EV_L2_DL_UNIT_DATA, l2_send_ui},
	{ST_L2_7, EV_L2_DL_UNIT_DATA, l2_send_ui},
	{ST_L2_8, EV_L2_DL_UNIT_DATA, l2_send_ui},
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

#define L2_FN_COUNT (sizeof(L2FnList)/sizeof(struct FsmNode))

static void
isdnl2_l1l2(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	u_char *datap;
	int ret = 1, len;
	int c = 0;

	switch (pr) {
		case (PH_DATA | INDICATION):
			datap = skb->data;
			len = l2addrsize(&st->l2);
			if (skb->len > len)
				datap += len;
			else {
				FsmEvent(&st->l2.l2m, EV_L2_FRAME_ERROR, (void *) 'N');
				dev_kfree_skb(skb);
				return;
			}
			if (!(*datap & 1)) {	/* I-Frame */
				if(!(c = iframe_error(st, skb)))
					ret = FsmEvent(&st->l2.l2m, EV_L2_I, skb);
			} else if (IsSFrame(datap, st)) {	/* S-Frame */
				if(!(c = super_error(st, skb)))
					ret = FsmEvent(&st->l2.l2m, EV_L2_SUPER, skb);
			} else if (IsUI(datap)) {
				if(!(c = UI_error(st, skb)))
					ret = FsmEvent(&st->l2.l2m, EV_L2_UI, skb);
			} else if (IsSABME(datap, st)) {
				if(!(c = unnum_error(st, skb, CMD)))
					ret = FsmEvent(&st->l2.l2m, EV_L2_SABME, skb);
			} else if (IsUA(datap)) {
				if(!(c = unnum_error(st, skb, RSP)))
					ret = FsmEvent(&st->l2.l2m, EV_L2_UA, skb);
			} else if (IsDISC(datap)) {
				if(!(c = unnum_error(st, skb, CMD)))
					ret = FsmEvent(&st->l2.l2m, EV_L2_DISC, skb);
			} else if (IsDM(datap)) {
				if(!(c = unnum_error(st, skb, RSP)))
					ret = FsmEvent(&st->l2.l2m, EV_L2_DM, skb);
			} else if (IsFRMR(datap)) {
				if(!(c = FRMR_error(st,skb)))
					ret = FsmEvent(&st->l2.l2m, EV_L2_FRMR, skb);
			} else {
				FsmEvent(&st->l2.l2m, EV_L2_FRAME_ERROR, (void *) 'L');
				dev_kfree_skb(skb);
				ret = 0;
			}
			if(c) {
				dev_kfree_skb(skb);
				FsmEvent(&st->l2.l2m, EV_L2_FRAME_ERROR, (void *)(long)c);
				ret = 0;
			}
			if (ret)
				dev_kfree_skb(skb);
			break;
		case (PH_PULL | CONFIRM):
			FsmEvent(&st->l2.l2m, EV_L2_ACK_PULL, arg);
			break;
		case (PH_PAUSE | INDICATION):
			test_and_set_bit(FLG_DCHAN_BUSY, &st->l2.flag);
			break;
		case (PH_PAUSE | CONFIRM):
			test_and_clear_bit(FLG_DCHAN_BUSY, &st->l2.flag);
			break;
		case (PH_ACTIVATE | CONFIRM):
		case (PH_ACTIVATE | INDICATION):
			test_and_set_bit(FLG_L1_ACTIV, &st->l2.flag);
			if (test_and_clear_bit(FLG_ESTAB_PEND, &st->l2.flag))
				FsmEvent(&st->l2.l2m, EV_L2_DL_ESTABLISH_REQ, arg);
			break;
		case (PH_DEACTIVATE | INDICATION):
		case (PH_DEACTIVATE | CONFIRM):
			test_and_clear_bit(FLG_L1_ACTIV, &st->l2.flag);
			FsmEvent(&st->l2.l2m, EV_L1_DEACTIVATE, arg);
			break;
		default:
			l2m_debug(&st->l2.l2m, "l2 unknown pr %04x", pr);
			break;
	}
}

static void
isdnl2_l3l2(struct PStack *st, int pr, void *arg)
{
	switch (pr) {
		case (DL_DATA | REQUEST):
			if (FsmEvent(&st->l2.l2m, EV_L2_DL_DATA, arg)) {
				dev_kfree_skb((struct sk_buff *) arg);
			}
			break;
		case (DL_UNIT_DATA | REQUEST):
			if (FsmEvent(&st->l2.l2m, EV_L2_DL_UNIT_DATA, arg)) {
				dev_kfree_skb((struct sk_buff *) arg);
			}
			break;
		case (DL_ESTABLISH | REQUEST):
			if (test_bit(FLG_L1_ACTIV, &st->l2.flag)) {
				if (test_bit(FLG_LAPD, &st->l2.flag) ||
					test_bit(FLG_ORIG, &st->l2.flag)) {
					FsmEvent(&st->l2.l2m, EV_L2_DL_ESTABLISH_REQ, arg);
				}
			} else {
				if (test_bit(FLG_LAPD, &st->l2.flag) ||
					test_bit(FLG_ORIG, &st->l2.flag)) {
					test_and_set_bit(FLG_ESTAB_PEND, &st->l2.flag);
				}
				st->l2.l2l1(st, PH_ACTIVATE, NULL);
			}
			break;
		case (DL_RELEASE | REQUEST):
			if (test_bit(FLG_LAPB, &st->l2.flag)) {
				st->l2.l2l1(st, PH_DEACTIVATE, NULL);
			}
			FsmEvent(&st->l2.l2m, EV_L2_DL_RELEASE_REQ, arg);
			break;
		case (MDL_ASSIGN | REQUEST):
			FsmEvent(&st->l2.l2m, EV_L2_MDL_ASSIGN, arg);
			break;
		case (MDL_REMOVE | REQUEST):
			FsmEvent(&st->l2.l2m, EV_L2_MDL_REMOVE, arg);
			break;
		case (MDL_ERROR | RESPONSE):
			FsmEvent(&st->l2.l2m, EV_L2_MDL_ERROR, arg);
			break;
	}
}

void
releasestack_isdnl2(struct PStack *st)
{
	FsmDelTimer(&st->l2.t200, 21);
	FsmDelTimer(&st->l2.t203, 16);
	skb_queue_purge(&st->l2.i_queue);
	skb_queue_purge(&st->l2.ui_queue);
	ReleaseWin(&st->l2);
}

static void
l2m_debug(struct FsmInst *fi, char *fmt, ...)
{
	va_list args;
	struct PStack *st = fi->userdata;

	va_start(args, fmt);
	VHiSax_putstatus(st->l1.hardware, st->l2.debug_id, fmt, args);
	va_end(args);
}

void
setstack_isdnl2(struct PStack *st, char *debug_id)
{
	spin_lock_init(&st->l2.lock);
	st->l1.l1l2 = isdnl2_l1l2;
	st->l3.l3l2 = isdnl2_l3l2;

	skb_queue_head_init(&st->l2.i_queue);
	skb_queue_head_init(&st->l2.ui_queue);
	InitWin(&st->l2);
	st->l2.debug = 0;

	st->l2.l2m.fsm = &l2fsm;
	if (test_bit(FLG_LAPB, &st->l2.flag))
		st->l2.l2m.state = ST_L2_4;
	else
	st->l2.l2m.state = ST_L2_1;
	st->l2.l2m.debug = 0;
	st->l2.l2m.userdata = st;
	st->l2.l2m.userint = 0;
	st->l2.l2m.printdebug = l2m_debug;
	strcpy(st->l2.debug_id, debug_id);

	FsmInitTimer(&st->l2.l2m, &st->l2.t200);
	FsmInitTimer(&st->l2.l2m, &st->l2.t203);
}

static void
transl2_l3l2(struct PStack *st, int pr, void *arg)
{
	switch (pr) {
		case (DL_DATA | REQUEST):
		case (DL_UNIT_DATA | REQUEST):
			st->l2.l2l1(st, PH_DATA | REQUEST, arg);
			break;
		case (DL_ESTABLISH | REQUEST):
			st->l2.l2l1(st, PH_ACTIVATE | REQUEST, NULL);
			break;
		case (DL_RELEASE | REQUEST):
			st->l2.l2l1(st, PH_DEACTIVATE | REQUEST, NULL);
			break;
	}
}

void
setstack_transl2(struct PStack *st)
{
	st->l3.l3l2 = transl2_l3l2;
}

void
releasestack_transl2(struct PStack *st)
{
}

int __init
Isdnl2New(void)
{
	l2fsm.state_count = L2_STATE_COUNT;
	l2fsm.event_count = L2_EVENT_COUNT;
	l2fsm.strEvent = strL2Event;
	l2fsm.strState = strL2State;
	return FsmNew(&l2fsm, L2FnList, L2_FN_COUNT);
}

void
Isdnl2Free(void)
{
	FsmFree(&l2fsm);
}
