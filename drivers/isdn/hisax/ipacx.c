/*
 *
 * IPACX specific routines
 *
 * Author       Joerg Petersohn
 * Derived from hisax_isac.c, isac.c, hscx.c and others
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include "hisax_if.h"
#include "hisax.h"
#include "isdnl1.h"
#include "ipacx.h"

#define DBUSY_TIMER_VALUE 80
#define TIMER3_VALUE      7000
#define MAX_DFRAME_LEN_L1 300
#define B_FIFO_SIZE       64
#define D_FIFO_SIZE       32


// ipacx interrupt mask values
#define _MASK_IMASK     0x2E  // global mask
#define _MASKB_IMASK    0x0B
#define _MASKD_IMASK    0x03  // all on

//----------------------------------------------------------
// local function declarations
//----------------------------------------------------------
static void ph_command(struct IsdnCardState *cs, unsigned int command);
static inline void cic_int(struct IsdnCardState *cs);
static void dch_l2l1(struct PStack *st, int pr, void *arg);
static void dbusy_timer_handler(struct IsdnCardState *cs);
static void dch_empty_fifo(struct IsdnCardState *cs, int count);
static void dch_fill_fifo(struct IsdnCardState *cs);
static inline void dch_int(struct IsdnCardState *cs);
static void dch_setstack(struct PStack *st, struct IsdnCardState *cs);
static void dch_init(struct IsdnCardState *cs);
static void bch_l2l1(struct PStack *st, int pr, void *arg);
static void bch_empty_fifo(struct BCState *bcs, int count);
static void bch_fill_fifo(struct BCState *bcs);
static void bch_int(struct IsdnCardState *cs, u_char hscx);
static void bch_mode(struct BCState *bcs, int mode, int bc);
static void bch_close_state(struct BCState *bcs);
static int bch_open_state(struct IsdnCardState *cs, struct BCState *bcs);
static int bch_setstack(struct PStack *st, struct BCState *bcs);
static void bch_init(struct IsdnCardState *cs, int hscx);
static void clear_pending_ints(struct IsdnCardState *cs);

//----------------------------------------------------------
// Issue Layer 1 command to chip
//----------------------------------------------------------
static void
ph_command(struct IsdnCardState *cs, unsigned int command)
{
	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "ph_command (%#x) in (%#x)", command,
			cs->dc.isac.ph_state);
//###################################
//	printk(KERN_INFO "ph_command (%#x)\n", command);
//###################################
	cs->writeisac(cs, IPACX_CIX0, (command << 4) | 0x0E);
}

//----------------------------------------------------------
// Transceiver interrupt handler
//----------------------------------------------------------
static inline void
cic_int(struct IsdnCardState *cs)
{
	u_char event;

	event = cs->readisac(cs, IPACX_CIR0) >> 4;
	if (cs->debug & L1_DEB_ISAC) debugl1(cs, "cic_int(event=%#x)", event);
//#########################################
//	printk(KERN_INFO "cic_int(%x)\n", event);
//#########################################
	cs->dc.isac.ph_state = event;
	schedule_event(cs, D_L1STATECHANGE);
}

//==========================================================
// D channel functions
//==========================================================

//----------------------------------------------------------
// Command entry point
//----------------------------------------------------------
static void
dch_l2l1(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb = arg;
	u_char cda1_cr;

	switch (pr) {
	case (PH_DATA | REQUEST):
		if (cs->debug & DEB_DLOG_HEX) LogFrame(cs, skb->data, skb->len);
		if (cs->debug & DEB_DLOG_VERBOSE) dlogframe(cs, skb, 0);
		if (cs->tx_skb) {
			skb_queue_tail(&cs->sq, skb);
#ifdef L2FRAME_DEBUG
			if (cs->debug & L1_DEB_LAPD) Logl2Frame(cs, skb, "PH_DATA Queued", 0);
#endif
		} else {
			cs->tx_skb = skb;
			cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG
			if (cs->debug & L1_DEB_LAPD) Logl2Frame(cs, skb, "PH_DATA", 0);
#endif
			dch_fill_fifo(cs);
		}
		break;

	case (PH_PULL | INDICATION):
		if (cs->tx_skb) {
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, " l2l1 tx_skb exist this shouldn't happen");
			skb_queue_tail(&cs->sq, skb);
			break;
		}
		if (cs->debug & DEB_DLOG_HEX)     LogFrame(cs, skb->data, skb->len);
		if (cs->debug & DEB_DLOG_VERBOSE) dlogframe(cs, skb, 0);
		cs->tx_skb = skb;
		cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG
		if (cs->debug & L1_DEB_LAPD) Logl2Frame(cs, skb, "PH_DATA_PULLED", 0);
#endif
		dch_fill_fifo(cs);
		break;

	case (PH_PULL | REQUEST):
#ifdef L2FRAME_DEBUG
		if (cs->debug & L1_DEB_LAPD) debugl1(cs, "-> PH_REQUEST_PULL");
#endif
		if (!cs->tx_skb) {
			clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
		} else
			set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
		break;

	case (HW_RESET | REQUEST):
	case (HW_ENABLE | REQUEST):
		if ((cs->dc.isac.ph_state == IPACX_IND_RES) ||
		    (cs->dc.isac.ph_state == IPACX_IND_DR) ||
		    (cs->dc.isac.ph_state == IPACX_IND_DC))
			ph_command(cs, IPACX_CMD_TIM);
		else
			ph_command(cs, IPACX_CMD_RES);
		break;

	case (HW_INFO3 | REQUEST):
		ph_command(cs, IPACX_CMD_AR8);
		break;

	case (HW_TESTLOOP | REQUEST):
		cs->writeisac(cs, IPACX_CDA_TSDP10, 0x80); // Timeslot 0 is B1
		cs->writeisac(cs, IPACX_CDA_TSDP11, 0x81); // Timeslot 0 is B1
		cda1_cr = cs->readisac(cs, IPACX_CDA1_CR);
		(void) cs->readisac(cs, IPACX_CDA2_CR);
		if ((long)arg & 1) { // loop B1
			cs->writeisac(cs, IPACX_CDA1_CR, cda1_cr | 0x0a);
		}
		else {  // B1 off
			cs->writeisac(cs, IPACX_CDA1_CR, cda1_cr & ~0x0a);
		}
		if ((long)arg & 2) { // loop B2
			cs->writeisac(cs, IPACX_CDA1_CR, cda1_cr | 0x14);
		}
		else {  // B2 off
			cs->writeisac(cs, IPACX_CDA1_CR, cda1_cr & ~0x14);
		}
		break;

	case (HW_DEACTIVATE | RESPONSE):
		skb_queue_purge(&cs->rq);
		skb_queue_purge(&cs->sq);
		if (cs->tx_skb) {
			dev_kfree_skb_any(cs->tx_skb);
			cs->tx_skb = NULL;
		}
		if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
			del_timer(&cs->dbusytimer);
		break;

	default:
		if (cs->debug & L1_DEB_WARN) debugl1(cs, "dch_l2l1 unknown %04x", pr);
		break;
	}
}

//----------------------------------------------------------
//----------------------------------------------------------
static void
dbusy_timer_handler(struct IsdnCardState *cs)
{
	struct PStack *st;
	int	rbchd, stard;

	if (test_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		rbchd = cs->readisac(cs, IPACX_RBCHD);
		stard = cs->readisac(cs, IPACX_STARD);
		if (cs->debug)
			debugl1(cs, "D-Channel Busy RBCHD %02x STARD %02x", rbchd, stard);
		if (!(stard & 0x40)) { // D-Channel Busy
			set_bit(FLG_L1_DBUSY, &cs->HW_Flags);
			for (st = cs->stlist; st; st = st->next) {
				st->l1.l1l2(st, PH_PAUSE | INDICATION, NULL); // flow control on
			}
		} else {
			// seems we lost an interrupt; reset transceiver */
			clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags);
			if (cs->tx_skb) {
				dev_kfree_skb_any(cs->tx_skb);
				cs->tx_cnt = 0;
				cs->tx_skb = NULL;
			} else {
				printk(KERN_WARNING "HiSax: ISAC D-Channel Busy no skb\n");
				debugl1(cs, "D-Channel Busy no skb");
			}
			cs->writeisac(cs, IPACX_CMDRD, 0x01); // Tx reset, generates XPR
		}
	}
}

//----------------------------------------------------------
// Fill buffer from receive FIFO
//----------------------------------------------------------
static void
dch_empty_fifo(struct IsdnCardState *cs, int count)
{
	u_char *ptr;

	if ((cs->debug & L1_DEB_ISAC) && !(cs->debug & L1_DEB_ISAC_FIFO))
		debugl1(cs, "dch_empty_fifo()");

	// message too large, remove
	if ((cs->rcvidx + count) >= MAX_DFRAME_LEN_L1) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "dch_empty_fifo() incoming message too large");
		cs->writeisac(cs, IPACX_CMDRD, 0x80); // RMC
		cs->rcvidx = 0;
		return;
	}

	ptr = cs->rcvbuf + cs->rcvidx;
	cs->rcvidx += count;

	cs->readisacfifo(cs, ptr, count);
	cs->writeisac(cs, IPACX_CMDRD, 0x80); // RMC

	if (cs->debug & L1_DEB_ISAC_FIFO) {
		char *t = cs->dlog;

		t += sprintf(t, "dch_empty_fifo() cnt %d", count);
		QuickHex(t, ptr, count);
		debugl1(cs, cs->dlog);
	}
}

//----------------------------------------------------------
// Fill transmit FIFO
//----------------------------------------------------------
static void
dch_fill_fifo(struct IsdnCardState *cs)
{
	int count;
	u_char cmd, *ptr;

	if ((cs->debug & L1_DEB_ISAC) && !(cs->debug & L1_DEB_ISAC_FIFO))
		debugl1(cs, "dch_fill_fifo()");

	if (!cs->tx_skb) return;
	count = cs->tx_skb->len;
	if (count <= 0) return;

	if (count > D_FIFO_SIZE) {
		count = D_FIFO_SIZE;
		cmd   = 0x08; // XTF
	} else {
		cmd   = 0x0A; // XTF | XME
	}

	ptr = cs->tx_skb->data;
	skb_pull(cs->tx_skb, count);
	cs->tx_cnt += count;
	cs->writeisacfifo(cs, ptr, count);
	cs->writeisac(cs, IPACX_CMDRD, cmd);

	// set timeout for transmission contol
	if (test_and_set_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		debugl1(cs, "dch_fill_fifo dbusytimer running");
		del_timer(&cs->dbusytimer);
	}
	init_timer(&cs->dbusytimer);
	cs->dbusytimer.expires = jiffies + ((DBUSY_TIMER_VALUE * HZ)/1000);
	add_timer(&cs->dbusytimer);

	if (cs->debug & L1_DEB_ISAC_FIFO) {
		char *t = cs->dlog;

		t += sprintf(t, "dch_fill_fifo() cnt %d", count);
		QuickHex(t, ptr, count);
		debugl1(cs, cs->dlog);
	}
}

//----------------------------------------------------------
// D channel interrupt handler
//----------------------------------------------------------
static inline void
dch_int(struct IsdnCardState *cs)
{
	struct sk_buff *skb;
	u_char istad, rstad;
	int count;

	istad = cs->readisac(cs, IPACX_ISTAD);
//##############################################
//	printk(KERN_WARNING "dch_int(istad=%02x)\n", istad);
//##############################################

	if (istad & 0x80) {  // RME
		rstad = cs->readisac(cs, IPACX_RSTAD);
		if ((rstad & 0xf0) != 0xa0) { // !(VFR && !RDO && CRC && !RAB)
			if (!(rstad & 0x80))
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "dch_int(): invalid frame");
			if ((rstad & 0x40))
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "dch_int(): RDO");
			if (!(rstad & 0x20))
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "dch_int(): CRC error");
			cs->writeisac(cs, IPACX_CMDRD, 0x80);  // RMC
		} else {  // received frame ok
			count = cs->readisac(cs, IPACX_RBCLD);
			if (count) count--; // RSTAB is last byte
			count &= D_FIFO_SIZE - 1;
			if (count == 0) count = D_FIFO_SIZE;
			dch_empty_fifo(cs, count);
			if ((count = cs->rcvidx) > 0) {
				cs->rcvidx = 0;
				if (!(skb = dev_alloc_skb(count)))
					printk(KERN_WARNING "HiSax dch_int(): receive out of memory\n");
				else {
					memcpy(skb_put(skb, count), cs->rcvbuf, count);
					skb_queue_tail(&cs->rq, skb);
				}
			}
		}
		cs->rcvidx = 0;
		schedule_event(cs, D_RCVBUFREADY);
	}

	if (istad & 0x40) {  // RPF
		dch_empty_fifo(cs, D_FIFO_SIZE);
	}

	if (istad & 0x20) {  // RFO
		if (cs->debug & L1_DEB_WARN) debugl1(cs, "dch_int(): RFO");
		cs->writeisac(cs, IPACX_CMDRD, 0x40); //RRES
	}

	if (istad & 0x10) {  // XPR
		if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
			del_timer(&cs->dbusytimer);
		if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
			schedule_event(cs, D_CLEARBUSY);
		if (cs->tx_skb) {
			if (cs->tx_skb->len) {
				dch_fill_fifo(cs);
				goto afterXPR;
			}
			else {
				dev_kfree_skb_irq(cs->tx_skb);
				cs->tx_skb = NULL;
				cs->tx_cnt = 0;
			}
		}
		if ((cs->tx_skb = skb_dequeue(&cs->sq))) {
			cs->tx_cnt = 0;
			dch_fill_fifo(cs);
		}
		else {
			schedule_event(cs, D_XMTBUFREADY);
		}
	}
afterXPR:

	if (istad & 0x0C) {  // XDU or XMR
		if (cs->debug & L1_DEB_WARN) debugl1(cs, "dch_int(): XDU");
		if (cs->tx_skb) {
			skb_push(cs->tx_skb, cs->tx_cnt); // retransmit
			cs->tx_cnt = 0;
			dch_fill_fifo(cs);
		} else {
			printk(KERN_WARNING "HiSax: ISAC XDU no skb\n");
			debugl1(cs, "ISAC XDU no skb");
		}
	}
}

//----------------------------------------------------------
//----------------------------------------------------------
static void
dch_setstack(struct PStack *st, struct IsdnCardState *cs)
{
	st->l1.l1hw = dch_l2l1;
}

//----------------------------------------------------------
//----------------------------------------------------------
static void
dch_init(struct IsdnCardState *cs)
{
	printk(KERN_INFO "HiSax: IPACX ISDN driver v0.1.0\n");

	cs->setstack_d      = dch_setstack;

	cs->dbusytimer.function = (void *) dbusy_timer_handler;
	cs->dbusytimer.data = (long) cs;
	init_timer(&cs->dbusytimer);

	cs->writeisac(cs, IPACX_TR_CONF0, 0x00);  // clear LDD
	cs->writeisac(cs, IPACX_TR_CONF2, 0x00);  // enable transmitter
	cs->writeisac(cs, IPACX_MODED,    0xC9);  // transparent mode 0, RAC, stop/go
	cs->writeisac(cs, IPACX_MON_CR,   0x00);  // disable monitor channel
}


//==========================================================
// B channel functions
//==========================================================

//----------------------------------------------------------
// Entry point for commands
//----------------------------------------------------------
static void
bch_l2l1(struct PStack *st, int pr, void *arg)
{
	struct BCState *bcs = st->l1.bcs;
	struct sk_buff *skb = arg;
	u_long flags;

	switch (pr) {
	case (PH_DATA | REQUEST):
		spin_lock_irqsave(&bcs->cs->lock, flags);
		if (bcs->tx_skb) {
			skb_queue_tail(&bcs->squeue, skb);
		} else {
			bcs->tx_skb = skb;
			set_bit(BC_FLG_BUSY, &bcs->Flag);
			bcs->hw.hscx.count = 0;
			bch_fill_fifo(bcs);
		}
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
		break;
	case (PH_PULL | INDICATION):
		spin_lock_irqsave(&bcs->cs->lock, flags);
		if (bcs->tx_skb) {
			printk(KERN_WARNING "HiSax bch_l2l1(): this shouldn't happen\n");
		} else {
			set_bit(BC_FLG_BUSY, &bcs->Flag);
			bcs->tx_skb = skb;
			bcs->hw.hscx.count = 0;
			bch_fill_fifo(bcs);
		}
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
		break;
	case (PH_PULL | REQUEST):
		if (!bcs->tx_skb) {
			clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
		} else
			set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
		break;
	case (PH_ACTIVATE | REQUEST):
		spin_lock_irqsave(&bcs->cs->lock, flags);
		set_bit(BC_FLG_ACTIV, &bcs->Flag);
		bch_mode(bcs, st->l1.mode, st->l1.bc);
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
		l1_msg_b(st, pr, arg);
		break;
	case (PH_DEACTIVATE | REQUEST):
		l1_msg_b(st, pr, arg);
		break;
	case (PH_DEACTIVATE | CONFIRM):
		spin_lock_irqsave(&bcs->cs->lock, flags);
		clear_bit(BC_FLG_ACTIV, &bcs->Flag);
		clear_bit(BC_FLG_BUSY, &bcs->Flag);
		bch_mode(bcs, 0, st->l1.bc);
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
		st->l1.l1l2(st, PH_DEACTIVATE | CONFIRM, NULL);
		break;
	}
}

//----------------------------------------------------------
// Read B channel fifo to receive buffer
//----------------------------------------------------------
static void
bch_empty_fifo(struct BCState *bcs, int count)
{
	u_char *ptr, hscx;
	struct IsdnCardState *cs;
	int cnt;

	cs = bcs->cs;
	hscx = bcs->hw.hscx.hscx;
	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "bch_empty_fifo()");

	// message too large, remove
	if (bcs->hw.hscx.rcvidx + count > HSCX_BUFMAX) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "bch_empty_fifo() incoming packet too large");
		cs->BC_Write_Reg(cs, hscx, IPACX_CMDRB, 0x80);  // RMC
		bcs->hw.hscx.rcvidx = 0;
		return;
	}

	ptr = bcs->hw.hscx.rcvbuf + bcs->hw.hscx.rcvidx;
	cnt = count;
	while (cnt--) *ptr++ = cs->BC_Read_Reg(cs, hscx, IPACX_RFIFOB);
	cs->BC_Write_Reg(cs, hscx, IPACX_CMDRB, 0x80);  // RMC

	ptr = bcs->hw.hscx.rcvbuf + bcs->hw.hscx.rcvidx;
	bcs->hw.hscx.rcvidx += count;

	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char *t = bcs->blog;

		t += sprintf(t, "bch_empty_fifo() B-%d cnt %d", hscx, count);
		QuickHex(t, ptr, count);
		debugl1(cs, bcs->blog);
	}
}

//----------------------------------------------------------
// Fill buffer to transmit FIFO
//----------------------------------------------------------
static void
bch_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs;
	int more, count, cnt;
	u_char *ptr, *p, hscx;

	cs = bcs->cs;
	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "bch_fill_fifo()");

	if (!bcs->tx_skb)           return;
	if (bcs->tx_skb->len <= 0)  return;

	hscx = bcs->hw.hscx.hscx;
	more = (bcs->mode == L1_MODE_TRANS) ? 1 : 0;
	if (bcs->tx_skb->len > B_FIFO_SIZE) {
		more  = 1;
		count = B_FIFO_SIZE;
	} else {
		count = bcs->tx_skb->len;
	}
	cnt = count;

	p = ptr = bcs->tx_skb->data;
	skb_pull(bcs->tx_skb, count);
	bcs->tx_cnt -= count;
	bcs->hw.hscx.count += count;
	while (cnt--) cs->BC_Write_Reg(cs, hscx, IPACX_XFIFOB, *p++);
	cs->BC_Write_Reg(cs, hscx, IPACX_CMDRB, (more ? 0x08 : 0x0a));

	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char *t = bcs->blog;

		t += sprintf(t, "chb_fill_fifo() B-%d cnt %d", hscx, count);
		QuickHex(t, ptr, count);
		debugl1(cs, bcs->blog);
	}
}

//----------------------------------------------------------
// B channel interrupt handler
//----------------------------------------------------------
static void
bch_int(struct IsdnCardState *cs, u_char hscx)
{
	u_char istab;
	struct BCState *bcs;
	struct sk_buff *skb;
	int count;
	u_char rstab;

	bcs = cs->bcs + hscx;
	istab = cs->BC_Read_Reg(cs, hscx, IPACX_ISTAB);
//##############################################
//	printk(KERN_WARNING "bch_int(istab=%02x)\n", istab);
//##############################################
	if (!test_bit(BC_FLG_INIT, &bcs->Flag)) return;

	if (istab & 0x80) {	// RME
		rstab = cs->BC_Read_Reg(cs, hscx, IPACX_RSTAB);
		if ((rstab & 0xf0) != 0xa0) { // !(VFR && !RDO && CRC && !RAB)
			if (!(rstab & 0x80))
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "bch_int() B-%d: invalid frame", hscx);
			if ((rstab & 0x40) && (bcs->mode != L1_MODE_NULL))
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "bch_int() B-%d: RDO mode=%d", hscx, bcs->mode);
			if (!(rstab & 0x20))
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "bch_int() B-%d: CRC error", hscx);
			cs->BC_Write_Reg(cs, hscx, IPACX_CMDRB, 0x80);  // RMC
		}
		else {  // received frame ok
			count = cs->BC_Read_Reg(cs, hscx, IPACX_RBCLB) & (B_FIFO_SIZE - 1);
			if (count == 0) count = B_FIFO_SIZE;
			bch_empty_fifo(bcs, count);
			if ((count = bcs->hw.hscx.rcvidx - 1) > 0) {
				if (cs->debug & L1_DEB_HSCX_FIFO)
					debugl1(cs, "bch_int Frame %d", count);
				if (!(skb = dev_alloc_skb(count)))
					printk(KERN_WARNING "HiSax bch_int(): receive frame out of memory\n");
				else {
					memcpy(skb_put(skb, count), bcs->hw.hscx.rcvbuf, count);
					skb_queue_tail(&bcs->rqueue, skb);
				}
			}
		}
		bcs->hw.hscx.rcvidx = 0;
		schedule_event(bcs, B_RCVBUFREADY);
	}

	if (istab & 0x40) {	// RPF
		bch_empty_fifo(bcs, B_FIFO_SIZE);

		if (bcs->mode == L1_MODE_TRANS) { // queue every chunk
			// receive transparent audio data
			if (!(skb = dev_alloc_skb(B_FIFO_SIZE)))
				printk(KERN_WARNING "HiSax bch_int(): receive transparent out of memory\n");
			else {
				memcpy(skb_put(skb, B_FIFO_SIZE), bcs->hw.hscx.rcvbuf, B_FIFO_SIZE);
				skb_queue_tail(&bcs->rqueue, skb);
			}
			bcs->hw.hscx.rcvidx = 0;
			schedule_event(bcs, B_RCVBUFREADY);
		}
	}

	if (istab & 0x20) {	// RFO
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "bch_int() B-%d: RFO error", hscx);
		cs->BC_Write_Reg(cs, hscx, IPACX_CMDRB, 0x40);  // RRES
	}

	if (istab & 0x10) {	// XPR
		if (bcs->tx_skb) {
			if (bcs->tx_skb->len) {
				bch_fill_fifo(bcs);
				goto afterXPR;
			} else {
				if (test_bit(FLG_LLI_L1WAKEUP, &bcs->st->lli.flag) &&
				    (PACKET_NOACK != bcs->tx_skb->pkt_type)) {
					u_long	flags;
					spin_lock_irqsave(&bcs->aclock, flags);
					bcs->ackcnt += bcs->hw.hscx.count;
					spin_unlock_irqrestore(&bcs->aclock, flags);
					schedule_event(bcs, B_ACKPENDING);
				}
			}
			dev_kfree_skb_irq(bcs->tx_skb);
			bcs->hw.hscx.count = 0;
			bcs->tx_skb = NULL;
		}
		if ((bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
			bcs->hw.hscx.count = 0;
			set_bit(BC_FLG_BUSY, &bcs->Flag);
			bch_fill_fifo(bcs);
		} else {
			clear_bit(BC_FLG_BUSY, &bcs->Flag);
			schedule_event(bcs, B_XMTBUFREADY);
		}
	}
afterXPR:

	if (istab & 0x04) {	// XDU
		if (bcs->mode == L1_MODE_TRANS) {
			bch_fill_fifo(bcs);
		}
		else {
			if (bcs->tx_skb) {  // restart transmitting the whole frame
				skb_push(bcs->tx_skb, bcs->hw.hscx.count);
				bcs->tx_cnt += bcs->hw.hscx.count;
				bcs->hw.hscx.count = 0;
			}
			cs->BC_Write_Reg(cs, hscx, IPACX_CMDRB, 0x01);  // XRES
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "bch_int() B-%d XDU error", hscx);
		}
	}
}

//----------------------------------------------------------
//----------------------------------------------------------
static void
bch_mode(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;
	int hscx = bcs->hw.hscx.hscx;

	bc = bc ? 1 : 0;  // in case bc is greater than 1
	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "mode_bch() switch B-%d mode %d chan %d", hscx, mode, bc);
	bcs->mode = mode;
	bcs->channel = bc;

	// map controller to according timeslot
	if (!hscx)
	{
		cs->writeisac(cs, IPACX_BCHA_TSDP_BC1, 0x80 | bc);
		cs->writeisac(cs, IPACX_BCHA_CR,       0x88);
	}
	else
	{
		cs->writeisac(cs, IPACX_BCHB_TSDP_BC1, 0x80 | bc);
		cs->writeisac(cs, IPACX_BCHB_CR,       0x88);
	}

	switch (mode) {
	case (L1_MODE_NULL):
		cs->BC_Write_Reg(cs, hscx, IPACX_MODEB, 0xC0);  // rec off
		cs->BC_Write_Reg(cs, hscx, IPACX_EXMB,  0x30);  // std adj.
		cs->BC_Write_Reg(cs, hscx, IPACX_MASKB, 0xFF);  // ints off
		cs->BC_Write_Reg(cs, hscx, IPACX_CMDRB, 0x41);  // validate adjustments
		break;
	case (L1_MODE_TRANS):
		cs->BC_Write_Reg(cs, hscx, IPACX_MODEB, 0x88);  // ext transp mode
		cs->BC_Write_Reg(cs, hscx, IPACX_EXMB,  0x00);  // xxx00000
		cs->BC_Write_Reg(cs, hscx, IPACX_CMDRB, 0x41);  // validate adjustments
		cs->BC_Write_Reg(cs, hscx, IPACX_MASKB, _MASKB_IMASK);
		break;
	case (L1_MODE_HDLC):
		cs->BC_Write_Reg(cs, hscx, IPACX_MODEB, 0xC8);  // transp mode 0
		cs->BC_Write_Reg(cs, hscx, IPACX_EXMB,  0x01);  // idle=hdlc flags crc enabled
		cs->BC_Write_Reg(cs, hscx, IPACX_CMDRB, 0x41);  // validate adjustments
		cs->BC_Write_Reg(cs, hscx, IPACX_MASKB, _MASKB_IMASK);
		break;
	}
}

//----------------------------------------------------------
//----------------------------------------------------------
static void
bch_close_state(struct BCState *bcs)
{
	bch_mode(bcs, 0, bcs->channel);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		kfree(bcs->hw.hscx.rcvbuf);
		bcs->hw.hscx.rcvbuf = NULL;
		kfree(bcs->blog);
		bcs->blog = NULL;
		skb_queue_purge(&bcs->rqueue);
		skb_queue_purge(&bcs->squeue);
		if (bcs->tx_skb) {
			dev_kfree_skb_any(bcs->tx_skb);
			bcs->tx_skb = NULL;
			clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
}

//----------------------------------------------------------
//----------------------------------------------------------
static int
bch_open_state(struct IsdnCardState *cs, struct BCState *bcs)
{
	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (!(bcs->hw.hscx.rcvbuf = kmalloc(HSCX_BUFMAX, GFP_ATOMIC))) {
			printk(KERN_WARNING
			       "HiSax open_bchstate(): No memory for hscx.rcvbuf\n");
			clear_bit(BC_FLG_INIT, &bcs->Flag);
			return (1);
		}
		if (!(bcs->blog = kmalloc(MAX_BLOG_SPACE, GFP_ATOMIC))) {
			printk(KERN_WARNING
			       "HiSax open_bchstate: No memory for bcs->blog\n");
			clear_bit(BC_FLG_INIT, &bcs->Flag);
			kfree(bcs->hw.hscx.rcvbuf);
			bcs->hw.hscx.rcvbuf = NULL;
			return (2);
		}
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->tx_skb = NULL;
	clear_bit(BC_FLG_BUSY, &bcs->Flag);
	bcs->event = 0;
	bcs->hw.hscx.rcvidx = 0;
	bcs->tx_cnt = 0;
	return (0);
}

//----------------------------------------------------------
//----------------------------------------------------------
static int
bch_setstack(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	if (bch_open_state(st->l1.hardware, bcs)) return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = bch_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

//----------------------------------------------------------
//----------------------------------------------------------
static void
bch_init(struct IsdnCardState *cs, int hscx)
{
	cs->bcs[hscx].BC_SetStack   = bch_setstack;
	cs->bcs[hscx].BC_Close      = bch_close_state;
	cs->bcs[hscx].hw.hscx.hscx  = hscx;
	cs->bcs[hscx].cs            = cs;
	bch_mode(cs->bcs + hscx, 0, hscx);
}


//==========================================================
// Shared functions
//==========================================================

//----------------------------------------------------------
// Main interrupt handler
//----------------------------------------------------------
void
interrupt_ipacx(struct IsdnCardState *cs)
{
	u_char ista;

	while ((ista = cs->readisac(cs, IPACX_ISTA))) {
//#################################################
//		printk(KERN_WARNING "interrupt_ipacx(ista=%02x)\n", ista);
//#################################################
		if (ista & 0x80) bch_int(cs, 0); // B channel interrupts
		if (ista & 0x40) bch_int(cs, 1);

		if (ista & 0x01) dch_int(cs);    // D channel
		if (ista & 0x10) cic_int(cs);    // Layer 1 state
	}
}

//----------------------------------------------------------
// Clears chip interrupt status
//----------------------------------------------------------
static void
clear_pending_ints(struct IsdnCardState *cs)
{
	int ista;

	// all interrupts off
	cs->writeisac(cs, IPACX_MASK, 0xff);
	cs->writeisac(cs, IPACX_MASKD, 0xff);
	cs->BC_Write_Reg(cs, 0, IPACX_MASKB, 0xff);
	cs->BC_Write_Reg(cs, 1, IPACX_MASKB, 0xff);

	ista = cs->readisac(cs, IPACX_ISTA);
	if (ista & 0x80) cs->BC_Read_Reg(cs, 0, IPACX_ISTAB);
	if (ista & 0x40) cs->BC_Read_Reg(cs, 1, IPACX_ISTAB);
	if (ista & 0x10) cs->readisac(cs, IPACX_CIR0);
	if (ista & 0x01) cs->readisac(cs, IPACX_ISTAD);
}

//----------------------------------------------------------
// Does chip configuration work
// Work to do depends on bit mask in part
//----------------------------------------------------------
void
init_ipacx(struct IsdnCardState *cs, int part)
{
	if (part & 1) {  // initialise chip
//##################################################
//	printk(KERN_INFO "init_ipacx(%x)\n", part);
//##################################################
		clear_pending_ints(cs);
		bch_init(cs, 0);
		bch_init(cs, 1);
		dch_init(cs);
	}
	if (part & 2) {  // reenable all interrupts and start chip
		cs->BC_Write_Reg(cs, 0, IPACX_MASKB, _MASKB_IMASK);
		cs->BC_Write_Reg(cs, 1, IPACX_MASKB, _MASKB_IMASK);
		cs->writeisac(cs, IPACX_MASKD, _MASKD_IMASK);
		cs->writeisac(cs, IPACX_MASK, _MASK_IMASK); // global mask register

		// reset HDLC Transmitters/receivers
		cs->writeisac(cs, IPACX_CMDRD, 0x41);
		cs->BC_Write_Reg(cs, 0, IPACX_CMDRB, 0x41);
		cs->BC_Write_Reg(cs, 1, IPACX_CMDRB, 0x41);
		ph_command(cs, IPACX_CMD_RES);
	}
}

//----------------- end of file -----------------------
