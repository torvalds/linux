/* $Id: hfc_2bs0.c,v 1.20.2.6 2004/02/11 13:21:33 keil Exp $
 *
 * specific routines for CCD's HFC 2BS0
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
#include "hfc_2bs0.h"
#include "isac.h"
#include "isdnl1.h"
#include <linux/interrupt.h>
#include <linux/slab.h>

static inline int
WaitForBusy(struct IsdnCardState *cs)
{
	int to = 130;
	u_char val;

	while (!(cs->BC_Read_Reg(cs, HFC_STATUS, 0) & HFC_BUSY) && to) {
		val = cs->BC_Read_Reg(cs, HFC_DATA, HFC_CIP | HFC_F2 |
				      (cs->hw.hfc.cip & 3));
		udelay(1);
		to--;
	}
	if (!to) {
		printk(KERN_WARNING "HiSax: waitforBusy timeout\n");
		return (0);
	} else
		return (to);
}

static inline int
WaitNoBusy(struct IsdnCardState *cs)
{
	int to = 125;

	while ((cs->BC_Read_Reg(cs, HFC_STATUS, 0) & HFC_BUSY) && to) {
		udelay(1);
		to--;
	}
	if (!to) {
		printk(KERN_WARNING "HiSax: waitforBusy timeout\n");
		return (0);
	} else
		return (to);
}

static int
GetFreeFifoBytes(struct BCState *bcs)
{
	int s;

	if (bcs->hw.hfc.f1 == bcs->hw.hfc.f2)
		return (bcs->cs->hw.hfc.fifosize);
	s = bcs->hw.hfc.send[bcs->hw.hfc.f1] - bcs->hw.hfc.send[bcs->hw.hfc.f2];
	if (s <= 0)
		s += bcs->cs->hw.hfc.fifosize;
	s = bcs->cs->hw.hfc.fifosize - s;
	return (s);
}

static int
ReadZReg(struct BCState *bcs, u_char reg)
{
	int val;

	WaitNoBusy(bcs->cs);
	val = 256 * bcs->cs->BC_Read_Reg(bcs->cs, HFC_DATA, reg | HFC_CIP | HFC_Z_HIGH);
	WaitNoBusy(bcs->cs);
	val += bcs->cs->BC_Read_Reg(bcs->cs, HFC_DATA, reg | HFC_CIP | HFC_Z_LOW);
	return (val);
}

static void
hfc_clear_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int idx, cnt;
	int rcnt, z1, z2;
	u_char cip, f1, f2;

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hfc_clear_fifo");
	cip = HFC_CIP | HFC_F1 | HFC_REC | HFC_CHANNEL(bcs->channel);
	if ((cip & 0xc3) != (cs->hw.hfc.cip & 0xc3)) {
		cs->BC_Write_Reg(cs, HFC_STATUS, cip, cip);
		WaitForBusy(cs);
	}
	WaitNoBusy(cs);
	f1 = cs->BC_Read_Reg(cs, HFC_DATA, cip);
	cip = HFC_CIP | HFC_F2 | HFC_REC | HFC_CHANNEL(bcs->channel);
	WaitNoBusy(cs);
	f2 = cs->BC_Read_Reg(cs, HFC_DATA, cip);
	z1 = ReadZReg(bcs, HFC_Z1 | HFC_REC | HFC_CHANNEL(bcs->channel));
	z2 = ReadZReg(bcs, HFC_Z2 | HFC_REC | HFC_CHANNEL(bcs->channel));
	cnt = 32;
	while (((f1 != f2) || (z1 != z2)) && cnt--) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfc clear %d f1(%d) f2(%d)",
				bcs->channel, f1, f2);
		rcnt = z1 - z2;
		if (rcnt < 0)
			rcnt += cs->hw.hfc.fifosize;
		if (rcnt)
			rcnt++;
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfc clear %d z1(%x) z2(%x) cnt(%d)",
				bcs->channel, z1, z2, rcnt);
		cip = HFC_CIP | HFC_FIFO_OUT | HFC_REC | HFC_CHANNEL(bcs->channel);
		idx = 0;
		while ((idx < rcnt) && WaitNoBusy(cs)) {
			cs->BC_Read_Reg(cs, HFC_DATA_NODEB, cip);
			idx++;
		}
		if (f1 != f2) {
			WaitNoBusy(cs);
			cs->BC_Read_Reg(cs, HFC_DATA, HFC_CIP | HFC_F2_INC | HFC_REC |
					HFC_CHANNEL(bcs->channel));
			WaitForBusy(cs);
		}
		cip = HFC_CIP | HFC_F1 | HFC_REC | HFC_CHANNEL(bcs->channel);
		WaitNoBusy(cs);
		f1 = cs->BC_Read_Reg(cs, HFC_DATA, cip);
		cip = HFC_CIP | HFC_F2 | HFC_REC | HFC_CHANNEL(bcs->channel);
		WaitNoBusy(cs);
		f2 = cs->BC_Read_Reg(cs, HFC_DATA, cip);
		z1 = ReadZReg(bcs, HFC_Z1 | HFC_REC | HFC_CHANNEL(bcs->channel));
		z2 = ReadZReg(bcs, HFC_Z2 | HFC_REC | HFC_CHANNEL(bcs->channel));
	}
	return;
}


static struct sk_buff
*
hfc_empty_fifo(struct BCState *bcs, int count)
{
	u_char *ptr;
	struct sk_buff *skb;
	struct IsdnCardState *cs = bcs->cs;
	int idx;
	int chksum;
	u_char stat, cip;

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hfc_empty_fifo");
	idx = 0;
	if (count > HSCX_BUFMAX + 3) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hfc_empty_fifo: incoming packet too large");
		cip = HFC_CIP | HFC_FIFO_OUT | HFC_REC | HFC_CHANNEL(bcs->channel);
		while ((idx++ < count) && WaitNoBusy(cs))
			cs->BC_Read_Reg(cs, HFC_DATA_NODEB, cip);
		WaitNoBusy(cs);
		stat = cs->BC_Read_Reg(cs, HFC_DATA, HFC_CIP | HFC_F2_INC | HFC_REC |
				       HFC_CHANNEL(bcs->channel));
		WaitForBusy(cs);
		return (NULL);
	}
	if ((count < 4) && (bcs->mode != L1_MODE_TRANS)) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hfc_empty_fifo: incoming packet too small");
		cip = HFC_CIP | HFC_FIFO_OUT | HFC_REC | HFC_CHANNEL(bcs->channel);
		while ((idx++ < count) && WaitNoBusy(cs))
			cs->BC_Read_Reg(cs, HFC_DATA_NODEB, cip);
		WaitNoBusy(cs);
		stat = cs->BC_Read_Reg(cs, HFC_DATA, HFC_CIP | HFC_F2_INC | HFC_REC |
				       HFC_CHANNEL(bcs->channel));
		WaitForBusy(cs);
#ifdef ERROR_STATISTIC
		bcs->err_inv++;
#endif
		return (NULL);
	}
	if (bcs->mode == L1_MODE_TRANS)
	  count -= 1;
	else
	  count -= 3;
	if (!(skb = dev_alloc_skb(count)))
		printk(KERN_WARNING "HFC: receive out of memory\n");
	else {
		ptr = skb_put(skb, count);
		idx = 0;
		cip = HFC_CIP | HFC_FIFO_OUT | HFC_REC | HFC_CHANNEL(bcs->channel);
		while ((idx < count) && WaitNoBusy(cs)) {
			*ptr++ = cs->BC_Read_Reg(cs, HFC_DATA_NODEB, cip);
			idx++;
		}
		if (idx != count) {
			debugl1(cs, "RFIFO BUSY error");
			printk(KERN_WARNING "HFC FIFO channel %d BUSY Error\n", bcs->channel);
			dev_kfree_skb_any(skb);
			if (bcs->mode != L1_MODE_TRANS) {
			  WaitNoBusy(cs);
			  stat = cs->BC_Read_Reg(cs, HFC_DATA, HFC_CIP | HFC_F2_INC | HFC_REC |
						 HFC_CHANNEL(bcs->channel));
			  WaitForBusy(cs);
			}
			return (NULL);
		}
		if (bcs->mode != L1_MODE_TRANS) {
		  WaitNoBusy(cs);
		  chksum = (cs->BC_Read_Reg(cs, HFC_DATA, cip) << 8);
		  WaitNoBusy(cs);
		  chksum += cs->BC_Read_Reg(cs, HFC_DATA, cip);
		  WaitNoBusy(cs);
		  stat = cs->BC_Read_Reg(cs, HFC_DATA, cip);
		  if (cs->debug & L1_DEB_HSCX)
		    debugl1(cs, "hfc_empty_fifo %d chksum %x stat %x",
			    bcs->channel, chksum, stat);
		  if (stat) {
		    debugl1(cs, "FIFO CRC error");
		    dev_kfree_skb_any(skb);
		    skb = NULL;
#ifdef ERROR_STATISTIC
		    bcs->err_crc++;
#endif
		  }
		  WaitNoBusy(cs);
		  stat = cs->BC_Read_Reg(cs, HFC_DATA, HFC_CIP | HFC_F2_INC | HFC_REC |
					 HFC_CHANNEL(bcs->channel));
		  WaitForBusy(cs);
		}
	}
	return (skb);
}

static void
hfc_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int idx, fcnt;
	int count;
	int z1, z2;
	u_char cip;

	if (!bcs->tx_skb)
		return;
	if (bcs->tx_skb->len <= 0)
		return;

	cip = HFC_CIP | HFC_F1 | HFC_SEND | HFC_CHANNEL(bcs->channel);
	if ((cip & 0xc3) != (cs->hw.hfc.cip & 0xc3)) {
	  cs->BC_Write_Reg(cs, HFC_STATUS, cip, cip);
	  WaitForBusy(cs);
	}
	WaitNoBusy(cs);
	if (bcs->mode != L1_MODE_TRANS) {
	  bcs->hw.hfc.f1 = cs->BC_Read_Reg(cs, HFC_DATA, cip);
	  cip = HFC_CIP | HFC_F2 | HFC_SEND | HFC_CHANNEL(bcs->channel);
	  WaitNoBusy(cs);
	  bcs->hw.hfc.f2 = cs->BC_Read_Reg(cs, HFC_DATA, cip);
	  bcs->hw.hfc.send[bcs->hw.hfc.f1] = ReadZReg(bcs, HFC_Z1 | HFC_SEND | HFC_CHANNEL(bcs->channel));
	  if (cs->debug & L1_DEB_HSCX)
	    debugl1(cs, "hfc_fill_fifo %d f1(%d) f2(%d) z1(%x)",
		    bcs->channel, bcs->hw.hfc.f1, bcs->hw.hfc.f2,
		    bcs->hw.hfc.send[bcs->hw.hfc.f1]);
	  fcnt = bcs->hw.hfc.f1 - bcs->hw.hfc.f2;
	  if (fcnt < 0)
	    fcnt += 32;
	  if (fcnt > 30) {
	    if (cs->debug & L1_DEB_HSCX)
	      debugl1(cs, "hfc_fill_fifo more as 30 frames");
	    return;
	  }
	  count = GetFreeFifoBytes(bcs);
	} 
	else {
	  WaitForBusy(cs);
	  z1 = ReadZReg(bcs, HFC_Z1 | HFC_REC | HFC_CHANNEL(bcs->channel));
	  z2 = ReadZReg(bcs, HFC_Z2 | HFC_REC | HFC_CHANNEL(bcs->channel));
	  count = z1 - z2;
	  if (count < 0)
	    count += cs->hw.hfc.fifosize; 
	} /* L1_MODE_TRANS */
	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "hfc_fill_fifo %d count(%u/%d)",
			bcs->channel, bcs->tx_skb->len,
			count);
	if (count < bcs->tx_skb->len) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfc_fill_fifo no fifo mem");
		return;
	}
	cip = HFC_CIP | HFC_FIFO_IN | HFC_SEND | HFC_CHANNEL(bcs->channel);
	idx = 0;
	while ((idx < bcs->tx_skb->len) && WaitNoBusy(cs))
		cs->BC_Write_Reg(cs, HFC_DATA_NODEB, cip, bcs->tx_skb->data[idx++]);
	if (idx != bcs->tx_skb->len) {
		debugl1(cs, "FIFO Send BUSY error");
		printk(KERN_WARNING "HFC S FIFO channel %d BUSY Error\n", bcs->channel);
	} else {
		count =  bcs->tx_skb->len;
		bcs->tx_cnt -= count;
		if (PACKET_NOACK == bcs->tx_skb->pkt_type)
			count = -1;
		dev_kfree_skb_any(bcs->tx_skb);
		bcs->tx_skb = NULL;
		if (bcs->mode != L1_MODE_TRANS) {
		  WaitForBusy(cs);
		  WaitNoBusy(cs);
		  cs->BC_Read_Reg(cs, HFC_DATA, HFC_CIP | HFC_F1_INC | HFC_SEND | HFC_CHANNEL(bcs->channel));
		}
		if (test_bit(FLG_LLI_L1WAKEUP,&bcs->st->lli.flag) &&
			(count >= 0)) {
			u_long	flags;
			spin_lock_irqsave(&bcs->aclock, flags);
			bcs->ackcnt += count;
			spin_unlock_irqrestore(&bcs->aclock, flags);
			schedule_event(bcs, B_ACKPENDING);
		}
		test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	}
	return;
}

void
main_irq_hfc(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int z1, z2, rcnt;
	u_char f1, f2, cip;
	int receive, transmit, count = 5;
	struct sk_buff *skb;

      Begin:
	count--;
	cip = HFC_CIP | HFC_F1 | HFC_REC | HFC_CHANNEL(bcs->channel);
	if ((cip & 0xc3) != (cs->hw.hfc.cip & 0xc3)) {
		cs->BC_Write_Reg(cs, HFC_STATUS, cip, cip);
		WaitForBusy(cs);
	}
	WaitNoBusy(cs);
	receive = 0;
	if (bcs->mode == L1_MODE_HDLC) {
		f1 = cs->BC_Read_Reg(cs, HFC_DATA, cip);
		cip = HFC_CIP | HFC_F2 | HFC_REC | HFC_CHANNEL(bcs->channel);
		WaitNoBusy(cs);
		f2 = cs->BC_Read_Reg(cs, HFC_DATA, cip);
		if (f1 != f2) {
			if (cs->debug & L1_DEB_HSCX)
				debugl1(cs, "hfc rec %d f1(%d) f2(%d)",
					bcs->channel, f1, f2);
			receive = 1; 
		}
	}
	if (receive || (bcs->mode == L1_MODE_TRANS)) {
		WaitForBusy(cs);
		z1 = ReadZReg(bcs, HFC_Z1 | HFC_REC | HFC_CHANNEL(bcs->channel));
		z2 = ReadZReg(bcs, HFC_Z2 | HFC_REC | HFC_CHANNEL(bcs->channel));
		rcnt = z1 - z2;
		if (rcnt < 0)
			rcnt += cs->hw.hfc.fifosize;
		if ((bcs->mode == L1_MODE_HDLC) || (rcnt)) {
			rcnt++;
			if (cs->debug & L1_DEB_HSCX)
				debugl1(cs, "hfc rec %d z1(%x) z2(%x) cnt(%d)",
					bcs->channel, z1, z2, rcnt);
			/*              sti(); */
			if ((skb = hfc_empty_fifo(bcs, rcnt))) {
				skb_queue_tail(&bcs->rqueue, skb);
				schedule_event(bcs, B_RCVBUFREADY);
			}
		}
		receive = 1;
	}
	if (bcs->tx_skb) {
		transmit = 1;
		test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
		hfc_fill_fifo(bcs);
		if (test_bit(BC_FLG_BUSY, &bcs->Flag))
			transmit = 0;
	} else {
		if ((bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
			transmit = 1;
			test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
			hfc_fill_fifo(bcs);
			if (test_bit(BC_FLG_BUSY, &bcs->Flag))
				transmit = 0;
		} else {
			transmit = 0;
			schedule_event(bcs, B_XMTBUFREADY);
		}
	}
	if ((receive || transmit) && count)
		goto Begin;
	return;
}

static void
mode_hfc(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;

	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "HFC 2BS0 mode %d bchan %d/%d",
			mode, bc, bcs->channel);
	bcs->mode = mode;
	bcs->channel = bc;

	switch (mode) {
		case (L1_MODE_NULL):
		        if (bc) {
				cs->hw.hfc.ctmt &= ~1;
				cs->hw.hfc.isac_spcr &= ~0x03;
			}
			else {
				cs->hw.hfc.ctmt &= ~2;
				cs->hw.hfc.isac_spcr &= ~0x0c;
			}
			break;
		case (L1_MODE_TRANS):
		        cs->hw.hfc.ctmt &= ~(1 << bc); /* set HDLC mode */ 
			cs->BC_Write_Reg(cs, HFC_STATUS, cs->hw.hfc.ctmt, cs->hw.hfc.ctmt);
			hfc_clear_fifo(bcs); /* complete fifo clear */ 
			if (bc) {
				cs->hw.hfc.ctmt |= 1;
				cs->hw.hfc.isac_spcr &= ~0x03;
				cs->hw.hfc.isac_spcr |= 0x02;
			} else {
				cs->hw.hfc.ctmt |= 2;
				cs->hw.hfc.isac_spcr &= ~0x0c;
				cs->hw.hfc.isac_spcr |= 0x08;
			}
			break;
		case (L1_MODE_HDLC):
			if (bc) {
				cs->hw.hfc.ctmt &= ~1;
				cs->hw.hfc.isac_spcr &= ~0x03;
				cs->hw.hfc.isac_spcr |= 0x02;
			} else {
				cs->hw.hfc.ctmt &= ~2;
				cs->hw.hfc.isac_spcr &= ~0x0c;
				cs->hw.hfc.isac_spcr |= 0x08;
			}
			break;
	}
	cs->BC_Write_Reg(cs, HFC_STATUS, cs->hw.hfc.ctmt, cs->hw.hfc.ctmt);
	cs->writeisac(cs, ISAC_SPCR, cs->hw.hfc.isac_spcr);
	if (mode == L1_MODE_HDLC)
		hfc_clear_fifo(bcs);
}

static void
hfc_l2l1(struct PStack *st, int pr, void *arg)
{
	struct BCState	*bcs = st->l1.bcs;
	struct sk_buff	*skb = arg;
	u_long		flags;

	switch (pr) {
		case (PH_DATA | REQUEST):
			spin_lock_irqsave(&bcs->cs->lock, flags);
			if (bcs->tx_skb) {
				skb_queue_tail(&bcs->squeue, skb);
			} else {
				bcs->tx_skb = skb;
				test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
				bcs->cs->BC_Send_Data(bcs);
			}
			spin_unlock_irqrestore(&bcs->cs->lock, flags);
			break;
		case (PH_PULL | INDICATION):
			spin_lock_irqsave(&bcs->cs->lock, flags);
			if (bcs->tx_skb) {
				printk(KERN_WARNING "hfc_l2l1: this shouldn't happen\n");
			} else {
				test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
				bcs->tx_skb = skb;
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
			mode_hfc(bcs, st->l1.mode, st->l1.bc);
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
			mode_hfc(bcs, 0, st->l1.bc);
			spin_unlock_irqrestore(&bcs->cs->lock, flags);
			st->l1.l1l2(st, PH_DEACTIVATE | CONFIRM, NULL);
			break;
	}
}


static void
close_hfcstate(struct BCState *bcs)
{
	mode_hfc(bcs, 0, bcs->channel);
	if (test_bit(BC_FLG_INIT, &bcs->Flag)) {
		skb_queue_purge(&bcs->rqueue);
		skb_queue_purge(&bcs->squeue);
		if (bcs->tx_skb) {
			dev_kfree_skb_any(bcs->tx_skb);
			bcs->tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
	test_and_clear_bit(BC_FLG_INIT, &bcs->Flag);
}

static int
open_hfcstate(struct IsdnCardState *cs, struct BCState *bcs)
{
	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	bcs->event = 0;
	bcs->tx_cnt = 0;
	return (0);
}

static int
setstack_hfc(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	if (open_hfcstate(st->l1.hardware, bcs))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = hfc_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

static void
init_send(struct BCState *bcs)
{
	int i;

	if (!(bcs->hw.hfc.send = kmalloc(32 * sizeof(unsigned int), GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for hfc.send\n");
		return;
	}
	for (i = 0; i < 32; i++)
		bcs->hw.hfc.send[i] = 0x1fff;
}

void
inithfc(struct IsdnCardState *cs)
{
	init_send(&cs->bcs[0]);
	init_send(&cs->bcs[1]);
	cs->BC_Send_Data = &hfc_fill_fifo;
	cs->bcs[0].BC_SetStack = setstack_hfc;
	cs->bcs[1].BC_SetStack = setstack_hfc;
	cs->bcs[0].BC_Close = close_hfcstate;
	cs->bcs[1].BC_Close = close_hfcstate;
	mode_hfc(cs->bcs, 0, 0);
	mode_hfc(cs->bcs + 1, 0, 0);
}

void
releasehfc(struct IsdnCardState *cs)
{
	kfree(cs->bcs[0].hw.hfc.send);
	cs->bcs[0].hw.hfc.send = NULL;
	kfree(cs->bcs[1].hw.hfc.send);
	cs->bcs[1].hw.hfc.send = NULL;
}
