/* $Id: hscx.c,v 1.24.2.4 2004/01/24 20:47:23 keil Exp $
 *
 * HSCX specific routines
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "hscx.h"
#include "isac.h"
#include "isdnl1.h"
#include <linux/interrupt.h>

static char *HSCXVer[] =
{"A1", "?1", "A2", "?3", "A3", "V2.1", "?6", "?7",
 "?8", "?9", "?10", "?11", "?12", "?13", "?14", "???"};

int
HscxVersion(struct IsdnCardState *cs, char *s)
{
	int verA, verB;

	verA = cs->BC_Read_Reg(cs, 0, HSCX_VSTR) & 0xf;
	verB = cs->BC_Read_Reg(cs, 1, HSCX_VSTR) & 0xf;
	printk(KERN_INFO "%s HSCX version A: %s  B: %s\n", s,
	       HSCXVer[verA], HSCXVer[verB]);
	if ((verA == 0) | (verA == 0xf) | (verB == 0) | (verB == 0xf))
		return (1);
	else
		return (0);
}

void
modehscx(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;
	int hscx = bcs->hw.hscx.hscx;

	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "hscx %c mode %d ichan %d",
			'A' + hscx, mode, bc);
	bcs->mode = mode;
	bcs->channel = bc;
	cs->BC_Write_Reg(cs, hscx, HSCX_XAD1, 0xFF);
	cs->BC_Write_Reg(cs, hscx, HSCX_XAD2, 0xFF);
	cs->BC_Write_Reg(cs, hscx, HSCX_RAH2, 0xFF);
	cs->BC_Write_Reg(cs, hscx, HSCX_XBCH, 0x0);
	cs->BC_Write_Reg(cs, hscx, HSCX_RLCR, 0x0);
	cs->BC_Write_Reg(cs, hscx, HSCX_CCR1,
		test_bit(HW_IPAC, &cs->HW_Flags) ? 0x82 : 0x85);
	cs->BC_Write_Reg(cs, hscx, HSCX_CCR2, 0x30);
	cs->BC_Write_Reg(cs, hscx, HSCX_XCCR, 7);
	cs->BC_Write_Reg(cs, hscx, HSCX_RCCR, 7);

	/* Switch IOM 1 SSI */
	if (test_bit(HW_IOM1, &cs->HW_Flags) && (hscx == 0))
		bc = 1 - bc;

	if (bc == 0) {
		cs->BC_Write_Reg(cs, hscx, HSCX_TSAX,
			      test_bit(HW_IOM1, &cs->HW_Flags) ? 0x7 : bcs->hw.hscx.tsaxr0);
		cs->BC_Write_Reg(cs, hscx, HSCX_TSAR,
			      test_bit(HW_IOM1, &cs->HW_Flags) ? 0x7 : bcs->hw.hscx.tsaxr0);
	} else {
		cs->BC_Write_Reg(cs, hscx, HSCX_TSAX, bcs->hw.hscx.tsaxr1);
		cs->BC_Write_Reg(cs, hscx, HSCX_TSAR, bcs->hw.hscx.tsaxr1);
	}
	switch (mode) {
		case (L1_MODE_NULL):
			cs->BC_Write_Reg(cs, hscx, HSCX_TSAX, 0x1f);
			cs->BC_Write_Reg(cs, hscx, HSCX_TSAR, 0x1f);
			cs->BC_Write_Reg(cs, hscx, HSCX_MODE, 0x84);
			break;
		case (L1_MODE_TRANS):
			cs->BC_Write_Reg(cs, hscx, HSCX_MODE, 0xe4);
			break;
		case (L1_MODE_HDLC):
			cs->BC_Write_Reg(cs, hscx, HSCX_CCR1,
				test_bit(HW_IPAC, &cs->HW_Flags) ? 0x8a : 0x8d);
			cs->BC_Write_Reg(cs, hscx, HSCX_MODE, 0x8c);
			break;
	}
	if (mode)
		cs->BC_Write_Reg(cs, hscx, HSCX_CMDR, 0x41);
	cs->BC_Write_Reg(cs, hscx, HSCX_ISTA, 0x00);
}

void
hscx_l2l1(struct PStack *st, int pr, void *arg)
{
	struct BCState *bcs = st->l1.bcs;
	u_long flags;
	struct sk_buff *skb = arg;

	switch (pr) {
		case (PH_DATA | REQUEST):
			spin_lock_irqsave(&bcs->cs->lock, flags);
			if (bcs->tx_skb) {
				skb_queue_tail(&bcs->squeue, skb);
			} else {
				bcs->tx_skb = skb;
				test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
				bcs->hw.hscx.count = 0;
				bcs->cs->BC_Send_Data(bcs);
			}
			spin_unlock_irqrestore(&bcs->cs->lock, flags);
			break;
		case (PH_PULL | INDICATION):
			spin_lock_irqsave(&bcs->cs->lock, flags);
			if (bcs->tx_skb) {
				printk(KERN_WARNING "hscx_l2l1: this shouldn't happen\n");
			} else {
				test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
				bcs->tx_skb = skb;
				bcs->hw.hscx.count = 0;
				bcs->cs->BC_Send_Data(bcs);
			}
			spin_unlock_irqrestore(&bcs->cs->lock, flags);
			break;
		case (PH_PULL | REQUEST):
			if (!bcs->tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
		case (PH_ACTIVATE | REQUEST):
			spin_lock_irqsave(&bcs->cs->lock, flags);
			test_and_set_bit(BC_FLG_ACTIV, &bcs->Flag);
			modehscx(bcs, st->l1.mode, st->l1.bc);
			spin_unlock_irqrestore(&bcs->cs->lock, flags);
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | REQUEST):
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | CONFIRM):
			spin_lock_irqsave(&bcs->cs->lock, flags);
			test_and_clear_bit(BC_FLG_ACTIV, &bcs->Flag);
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
			modehscx(bcs, 0, st->l1.bc);
			spin_unlock_irqrestore(&bcs->cs->lock, flags);
			st->l1.l1l2(st, PH_DEACTIVATE | CONFIRM, NULL);
			break;
	}
}

void
close_hscxstate(struct BCState *bcs)
{
	modehscx(bcs, 0, bcs->channel);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (bcs->hw.hscx.rcvbuf) {
			kfree(bcs->hw.hscx.rcvbuf);
			bcs->hw.hscx.rcvbuf = NULL;
		}
		if (bcs->blog) {
			kfree(bcs->blog);
			bcs->blog = NULL;
		}
		skb_queue_purge(&bcs->rqueue);
		skb_queue_purge(&bcs->squeue);
		if (bcs->tx_skb) {
			dev_kfree_skb_any(bcs->tx_skb);
			bcs->tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
}

int
open_hscxstate(struct IsdnCardState *cs, struct BCState *bcs)
{
	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (!(bcs->hw.hscx.rcvbuf = kmalloc(HSCX_BUFMAX, GFP_ATOMIC))) {
			printk(KERN_WARNING
				"HiSax: No memory for hscx.rcvbuf\n");
			test_and_clear_bit(BC_FLG_INIT, &bcs->Flag);
			return (1);
		}
		if (!(bcs->blog = kmalloc(MAX_BLOG_SPACE, GFP_ATOMIC))) {
			printk(KERN_WARNING
				"HiSax: No memory for bcs->blog\n");
			test_and_clear_bit(BC_FLG_INIT, &bcs->Flag);
			kfree(bcs->hw.hscx.rcvbuf);
			bcs->hw.hscx.rcvbuf = NULL;
			return (2);
		}
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	bcs->event = 0;
	bcs->hw.hscx.rcvidx = 0;
	bcs->tx_cnt = 0;
	return (0);
}

int
setstack_hscx(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	if (open_hscxstate(st->l1.hardware, bcs))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = hscx_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

void
clear_pending_hscx_ints(struct IsdnCardState *cs)
{
	int val, eval;

	val = cs->BC_Read_Reg(cs, 1, HSCX_ISTA);
	debugl1(cs, "HSCX B ISTA %x", val);
	if (val & 0x01) {
		eval = cs->BC_Read_Reg(cs, 1, HSCX_EXIR);
		debugl1(cs, "HSCX B EXIR %x", eval);
	}
	if (val & 0x02) {
		eval = cs->BC_Read_Reg(cs, 0, HSCX_EXIR);
		debugl1(cs, "HSCX A EXIR %x", eval);
	}
	val = cs->BC_Read_Reg(cs, 0, HSCX_ISTA);
	debugl1(cs, "HSCX A ISTA %x", val);
	val = cs->BC_Read_Reg(cs, 1, HSCX_STAR);
	debugl1(cs, "HSCX B STAR %x", val);
	val = cs->BC_Read_Reg(cs, 0, HSCX_STAR);
	debugl1(cs, "HSCX A STAR %x", val);
	/* disable all IRQ */
	cs->BC_Write_Reg(cs, 0, HSCX_MASK, 0xFF);
	cs->BC_Write_Reg(cs, 1, HSCX_MASK, 0xFF);
}

void
inithscx(struct IsdnCardState *cs)
{
	cs->bcs[0].BC_SetStack = setstack_hscx;
	cs->bcs[1].BC_SetStack = setstack_hscx;
	cs->bcs[0].BC_Close = close_hscxstate;
	cs->bcs[1].BC_Close = close_hscxstate;
	cs->bcs[0].hw.hscx.hscx = 0;
	cs->bcs[1].hw.hscx.hscx = 1;
	cs->bcs[0].hw.hscx.tsaxr0 = 0x2f;
	cs->bcs[0].hw.hscx.tsaxr1 = 3;
	cs->bcs[1].hw.hscx.tsaxr0 = 0x2f;
	cs->bcs[1].hw.hscx.tsaxr1 = 3;
	modehscx(cs->bcs, 0, 0);
	modehscx(cs->bcs + 1, 0, 0);
}

void
inithscxisac(struct IsdnCardState *cs, int part)
{
	if (part & 1) {
		clear_pending_isac_ints(cs);
		clear_pending_hscx_ints(cs);
		initisac(cs);
		inithscx(cs);
	}
	if (part & 2) {
		/* Reenable all IRQ */
		cs->writeisac(cs, ISAC_MASK, 0);
		cs->BC_Write_Reg(cs, 0, HSCX_MASK, 0);
		cs->BC_Write_Reg(cs, 1, HSCX_MASK, 0);
		/* RESET Receiver and Transmitter */
		cs->writeisac(cs, ISAC_CMDR, 0x41);
	}
}
