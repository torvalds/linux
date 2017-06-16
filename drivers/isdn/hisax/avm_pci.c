/* $Id: avm_pci.c,v 1.29.2.4 2004/02/11 13:21:32 keil Exp $
 *
 * low level stuff for AVM Fritz!PCI and ISA PnP isdn cards
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Thanks to AVM, Berlin for information
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/isapnp.h>
#include <linux/interrupt.h>

static const char *avm_pci_rev = "$Revision: 1.29.2.4 $";

#define  AVM_FRITZ_PCI		1
#define  AVM_FRITZ_PNP		2

#define  HDLC_FIFO		0x0
#define  HDLC_STATUS		0x4

#define	 AVM_HDLC_1		0x00
#define	 AVM_HDLC_2		0x01
#define	 AVM_ISAC_FIFO		0x02
#define	 AVM_ISAC_REG_LOW	0x04
#define	 AVM_ISAC_REG_HIGH	0x06

#define  AVM_STATUS0_IRQ_ISAC	0x01
#define  AVM_STATUS0_IRQ_HDLC	0x02
#define  AVM_STATUS0_IRQ_TIMER	0x04
#define  AVM_STATUS0_IRQ_MASK	0x07

#define  AVM_STATUS0_RESET	0x01
#define  AVM_STATUS0_DIS_TIMER	0x02
#define  AVM_STATUS0_RES_TIMER	0x04
#define  AVM_STATUS0_ENA_IRQ	0x08
#define  AVM_STATUS0_TESTBIT	0x10

#define  AVM_STATUS1_INT_SEL	0x0f
#define  AVM_STATUS1_ENA_IOM	0x80

#define  HDLC_MODE_ITF_FLG	0x01
#define  HDLC_MODE_TRANS	0x02
#define  HDLC_MODE_CCR_7	0x04
#define  HDLC_MODE_CCR_16	0x08
#define  HDLC_MODE_TESTLOOP	0x80

#define  HDLC_INT_XPR		0x80
#define  HDLC_INT_XDU		0x40
#define  HDLC_INT_RPR		0x20
#define  HDLC_INT_MASK		0xE0

#define  HDLC_STAT_RME		0x01
#define  HDLC_STAT_RDO		0x10
#define  HDLC_STAT_CRCVFRRAB	0x0E
#define  HDLC_STAT_CRCVFR	0x06
#define  HDLC_STAT_RML_MASK	0x3f00

#define  HDLC_CMD_XRS		0x80
#define  HDLC_CMD_XME		0x01
#define  HDLC_CMD_RRS		0x20
#define  HDLC_CMD_XML_MASK	0x3f00


/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	register u_char idx = (offset > 0x2f) ? AVM_ISAC_REG_HIGH : AVM_ISAC_REG_LOW;
	register u_char val;

	outb(idx, cs->hw.avm.cfg_reg + 4);
	val = inb(cs->hw.avm.isac + (offset & 0xf));
	return (val);
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	register u_char idx = (offset > 0x2f) ? AVM_ISAC_REG_HIGH : AVM_ISAC_REG_LOW;

	outb(idx, cs->hw.avm.cfg_reg + 4);
	outb(value, cs->hw.avm.isac + (offset & 0xf));
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	outb(AVM_ISAC_FIFO, cs->hw.avm.cfg_reg + 4);
	insb(cs->hw.avm.isac, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char *data, int size)
{
	outb(AVM_ISAC_FIFO, cs->hw.avm.cfg_reg + 4);
	outsb(cs->hw.avm.isac, data, size);
}

static inline u_int
ReadHDLCPCI(struct IsdnCardState *cs, int chan, u_char offset)
{
	register u_int idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;
	register u_int val;

	outl(idx, cs->hw.avm.cfg_reg + 4);
	val = inl(cs->hw.avm.isac + offset);
	return (val);
}

static inline void
WriteHDLCPCI(struct IsdnCardState *cs, int chan, u_char offset, u_int value)
{
	register u_int idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;

	outl(idx, cs->hw.avm.cfg_reg + 4);
	outl(value, cs->hw.avm.isac + offset);
}

static inline u_char
ReadHDLCPnP(struct IsdnCardState *cs, int chan, u_char offset)
{
	register u_char idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;
	register u_char val;

	outb(idx, cs->hw.avm.cfg_reg + 4);
	val = inb(cs->hw.avm.isac + offset);
	return (val);
}

static inline void
WriteHDLCPnP(struct IsdnCardState *cs, int chan, u_char offset, u_char value)
{
	register u_char idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;

	outb(idx, cs->hw.avm.cfg_reg + 4);
	outb(value, cs->hw.avm.isac + offset);
}

static u_char
ReadHDLC_s(struct IsdnCardState *cs, int chan, u_char offset)
{
	return (0xff & ReadHDLCPCI(cs, chan, offset));
}

static void
WriteHDLC_s(struct IsdnCardState *cs, int chan, u_char offset, u_char value)
{
	WriteHDLCPCI(cs, chan, offset, value);
}

static inline
struct BCState *Sel_BCS(struct IsdnCardState *cs, int channel)
{
	if (cs->bcs[0].mode && (cs->bcs[0].channel == channel))
		return (&cs->bcs[0]);
	else if (cs->bcs[1].mode && (cs->bcs[1].channel == channel))
		return (&cs->bcs[1]);
	else
		return (NULL);
}

static void
write_ctrl(struct BCState *bcs, int which) {

	if (bcs->cs->debug & L1_DEB_HSCX)
		debugl1(bcs->cs, "hdlc %c wr%x ctrl %x",
			'A' + bcs->channel, which, bcs->hw.hdlc.ctrl.ctrl);
	if (bcs->cs->subtyp == AVM_FRITZ_PCI) {
		WriteHDLCPCI(bcs->cs, bcs->channel, HDLC_STATUS, bcs->hw.hdlc.ctrl.ctrl);
	} else {
		if (which & 4)
			WriteHDLCPnP(bcs->cs, bcs->channel, HDLC_STATUS + 2,
				     bcs->hw.hdlc.ctrl.sr.mode);
		if (which & 2)
			WriteHDLCPnP(bcs->cs, bcs->channel, HDLC_STATUS + 1,
				     bcs->hw.hdlc.ctrl.sr.xml);
		if (which & 1)
			WriteHDLCPnP(bcs->cs, bcs->channel, HDLC_STATUS,
				     bcs->hw.hdlc.ctrl.sr.cmd);
	}
}

static void
modehdlc(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;
	int hdlc = bcs->channel;

	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "hdlc %c mode %d --> %d ichan %d --> %d",
			'A' + hdlc, bcs->mode, mode, hdlc, bc);
	bcs->hw.hdlc.ctrl.ctrl = 0;
	switch (mode) {
	case (-1): /* used for init */
		bcs->mode = 1;
		bcs->channel = bc;
		bc = 0;
	case (L1_MODE_NULL):
		if (bcs->mode == L1_MODE_NULL)
			return;
		bcs->hw.hdlc.ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
		bcs->hw.hdlc.ctrl.sr.mode = HDLC_MODE_TRANS;
		write_ctrl(bcs, 5);
		bcs->mode = L1_MODE_NULL;
		bcs->channel = bc;
		break;
	case (L1_MODE_TRANS):
		bcs->mode = mode;
		bcs->channel = bc;
		bcs->hw.hdlc.ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
		bcs->hw.hdlc.ctrl.sr.mode = HDLC_MODE_TRANS;
		write_ctrl(bcs, 5);
		bcs->hw.hdlc.ctrl.sr.cmd = HDLC_CMD_XRS;
		write_ctrl(bcs, 1);
		bcs->hw.hdlc.ctrl.sr.cmd = 0;
		schedule_event(bcs, B_XMTBUFREADY);
		break;
	case (L1_MODE_HDLC):
		bcs->mode = mode;
		bcs->channel = bc;
		bcs->hw.hdlc.ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
		bcs->hw.hdlc.ctrl.sr.mode = HDLC_MODE_ITF_FLG;
		write_ctrl(bcs, 5);
		bcs->hw.hdlc.ctrl.sr.cmd = HDLC_CMD_XRS;
		write_ctrl(bcs, 1);
		bcs->hw.hdlc.ctrl.sr.cmd = 0;
		schedule_event(bcs, B_XMTBUFREADY);
		break;
	}
}

static inline void
hdlc_empty_fifo(struct BCState *bcs, int count)
{
	register u_int *ptr;
	u_char *p;
	u_char idx = bcs->channel ? AVM_HDLC_2 : AVM_HDLC_1;
	int cnt = 0;
	struct IsdnCardState *cs = bcs->cs;

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hdlc_empty_fifo %d", count);
	if (bcs->hw.hdlc.rcvidx + count > HSCX_BUFMAX) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hdlc_empty_fifo: incoming packet too large");
		return;
	}
	p = bcs->hw.hdlc.rcvbuf + bcs->hw.hdlc.rcvidx;
	ptr = (u_int *)p;
	bcs->hw.hdlc.rcvidx += count;
	if (cs->subtyp == AVM_FRITZ_PCI) {
		outl(idx, cs->hw.avm.cfg_reg + 4);
		while (cnt < count) {
#ifdef __powerpc__
			*ptr++ = in_be32((unsigned *)(cs->hw.avm.isac + _IO_BASE));
#else
			*ptr++ = inl(cs->hw.avm.isac);
#endif /* __powerpc__ */
			cnt += 4;
		}
	} else {
		outb(idx, cs->hw.avm.cfg_reg + 4);
		while (cnt < count) {
			*p++ = inb(cs->hw.avm.isac);
			cnt++;
		}
	}
	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char *t = bcs->blog;

		if (cs->subtyp == AVM_FRITZ_PNP)
			p = (u_char *) ptr;
		t += sprintf(t, "hdlc_empty_fifo %c cnt %d",
			     bcs->channel ? 'B' : 'A', count);
		QuickHex(t, p, count);
		debugl1(cs, "%s", bcs->blog);
	}
}

static inline void
hdlc_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int count, cnt = 0;
	int fifo_size = 32;
	u_char *p;
	u_int *ptr;

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hdlc_fill_fifo");
	if (!bcs->tx_skb)
		return;
	if (bcs->tx_skb->len <= 0)
		return;

	bcs->hw.hdlc.ctrl.sr.cmd &= ~HDLC_CMD_XME;
	if (bcs->tx_skb->len > fifo_size) {
		count = fifo_size;
	} else {
		count = bcs->tx_skb->len;
		if (bcs->mode != L1_MODE_TRANS)
			bcs->hw.hdlc.ctrl.sr.cmd |= HDLC_CMD_XME;
	}
	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hdlc_fill_fifo %d/%u", count, bcs->tx_skb->len);
	p = bcs->tx_skb->data;
	ptr = (u_int *)p;
	skb_pull(bcs->tx_skb, count);
	bcs->tx_cnt -= count;
	bcs->hw.hdlc.count += count;
	bcs->hw.hdlc.ctrl.sr.xml = ((count == fifo_size) ? 0 : count);
	write_ctrl(bcs, 3);  /* sets the correct index too */
	if (cs->subtyp == AVM_FRITZ_PCI) {
		while (cnt < count) {
#ifdef __powerpc__
			out_be32((unsigned *)(cs->hw.avm.isac + _IO_BASE), *ptr++);
#else
			outl(*ptr++, cs->hw.avm.isac);
#endif /* __powerpc__ */
			cnt += 4;
		}
	} else {
		while (cnt < count) {
			outb(*p++, cs->hw.avm.isac);
			cnt++;
		}
	}
	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char *t = bcs->blog;

		if (cs->subtyp == AVM_FRITZ_PNP)
			p = (u_char *) ptr;
		t += sprintf(t, "hdlc_fill_fifo %c cnt %d",
			     bcs->channel ? 'B' : 'A', count);
		QuickHex(t, p, count);
		debugl1(cs, "%s", bcs->blog);
	}
}

static void
HDLC_irq(struct BCState *bcs, u_int stat) {
	int len;
	struct sk_buff *skb;

	if (bcs->cs->debug & L1_DEB_HSCX)
		debugl1(bcs->cs, "ch%d stat %#x", bcs->channel, stat);
	if (stat & HDLC_INT_RPR) {
		if (stat & HDLC_STAT_RDO) {
			if (bcs->cs->debug & L1_DEB_HSCX)
				debugl1(bcs->cs, "RDO");
			else
				debugl1(bcs->cs, "ch%d stat %#x", bcs->channel, stat);
			bcs->hw.hdlc.ctrl.sr.xml = 0;
			bcs->hw.hdlc.ctrl.sr.cmd |= HDLC_CMD_RRS;
			write_ctrl(bcs, 1);
			bcs->hw.hdlc.ctrl.sr.cmd &= ~HDLC_CMD_RRS;
			write_ctrl(bcs, 1);
			bcs->hw.hdlc.rcvidx = 0;
		} else {
			if (!(len = (stat & HDLC_STAT_RML_MASK) >> 8))
				len = 32;
			hdlc_empty_fifo(bcs, len);
			if ((stat & HDLC_STAT_RME) || (bcs->mode == L1_MODE_TRANS)) {
				if (((stat & HDLC_STAT_CRCVFRRAB) == HDLC_STAT_CRCVFR) ||
				    (bcs->mode == L1_MODE_TRANS)) {
					if (!(skb = dev_alloc_skb(bcs->hw.hdlc.rcvidx)))
						printk(KERN_WARNING "HDLC: receive out of memory\n");
					else {
						skb_put_data(skb,
							     bcs->hw.hdlc.rcvbuf,
							     bcs->hw.hdlc.rcvidx);
						skb_queue_tail(&bcs->rqueue, skb);
					}
					bcs->hw.hdlc.rcvidx = 0;
					schedule_event(bcs, B_RCVBUFREADY);
				} else {
					if (bcs->cs->debug & L1_DEB_HSCX)
						debugl1(bcs->cs, "invalid frame");
					else
						debugl1(bcs->cs, "ch%d invalid frame %#x", bcs->channel, stat);
					bcs->hw.hdlc.rcvidx = 0;
				}
			}
		}
	}
	if (stat & HDLC_INT_XDU) {
		/* Here we lost an TX interrupt, so
		 * restart transmitting the whole frame.
		 */
		if (bcs->tx_skb) {
			skb_push(bcs->tx_skb, bcs->hw.hdlc.count);
			bcs->tx_cnt += bcs->hw.hdlc.count;
			bcs->hw.hdlc.count = 0;
			if (bcs->cs->debug & L1_DEB_WARN)
				debugl1(bcs->cs, "ch%d XDU", bcs->channel);
		} else if (bcs->cs->debug & L1_DEB_WARN)
			debugl1(bcs->cs, "ch%d XDU without skb", bcs->channel);
		bcs->hw.hdlc.ctrl.sr.xml = 0;
		bcs->hw.hdlc.ctrl.sr.cmd |= HDLC_CMD_XRS;
		write_ctrl(bcs, 1);
		bcs->hw.hdlc.ctrl.sr.cmd &= ~HDLC_CMD_XRS;
		write_ctrl(bcs, 1);
		hdlc_fill_fifo(bcs);
	} else if (stat & HDLC_INT_XPR) {
		if (bcs->tx_skb) {
			if (bcs->tx_skb->len) {
				hdlc_fill_fifo(bcs);
				return;
			} else {
				if (test_bit(FLG_LLI_L1WAKEUP, &bcs->st->lli.flag) &&
				    (PACKET_NOACK != bcs->tx_skb->pkt_type)) {
					u_long flags;
					spin_lock_irqsave(&bcs->aclock, flags);
					bcs->ackcnt += bcs->hw.hdlc.count;
					spin_unlock_irqrestore(&bcs->aclock, flags);
					schedule_event(bcs, B_ACKPENDING);
				}
				dev_kfree_skb_irq(bcs->tx_skb);
				bcs->hw.hdlc.count = 0;
				bcs->tx_skb = NULL;
			}
		}
		if ((bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
			bcs->hw.hdlc.count = 0;
			test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
			hdlc_fill_fifo(bcs);
		} else {
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
			schedule_event(bcs, B_XMTBUFREADY);
		}
	}
}

static inline void
HDLC_irq_main(struct IsdnCardState *cs)
{
	u_int stat;
	struct BCState *bcs;

	if (cs->subtyp == AVM_FRITZ_PCI) {
		stat = ReadHDLCPCI(cs, 0, HDLC_STATUS);
	} else {
		stat = ReadHDLCPnP(cs, 0, HDLC_STATUS);
		if (stat & HDLC_INT_RPR)
			stat |= (ReadHDLCPnP(cs, 0, HDLC_STATUS + 1)) << 8;
	}
	if (stat & HDLC_INT_MASK) {
		if (!(bcs = Sel_BCS(cs, 0))) {
			if (cs->debug)
				debugl1(cs, "hdlc spurious channel 0 IRQ");
		} else
			HDLC_irq(bcs, stat);
	}
	if (cs->subtyp == AVM_FRITZ_PCI) {
		stat = ReadHDLCPCI(cs, 1, HDLC_STATUS);
	} else {
		stat = ReadHDLCPnP(cs, 1, HDLC_STATUS);
		if (stat & HDLC_INT_RPR)
			stat |= (ReadHDLCPnP(cs, 1, HDLC_STATUS + 1)) << 8;
	}
	if (stat & HDLC_INT_MASK) {
		if (!(bcs = Sel_BCS(cs, 1))) {
			if (cs->debug)
				debugl1(cs, "hdlc spurious channel 1 IRQ");
		} else
			HDLC_irq(bcs, stat);
	}
}

static void
hdlc_l2l1(struct PStack *st, int pr, void *arg)
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
			test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
			bcs->hw.hdlc.count = 0;
			bcs->cs->BC_Send_Data(bcs);
		}
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
		break;
	case (PH_PULL | INDICATION):
		spin_lock_irqsave(&bcs->cs->lock, flags);
		if (bcs->tx_skb) {
			printk(KERN_WARNING "hdlc_l2l1: this shouldn't happen\n");
		} else {
			test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
			bcs->tx_skb = skb;
			bcs->hw.hdlc.count = 0;
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
		modehdlc(bcs, st->l1.mode, st->l1.bc);
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
		modehdlc(bcs, 0, st->l1.bc);
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
		st->l1.l1l2(st, PH_DEACTIVATE | CONFIRM, NULL);
		break;
	}
}

static void
close_hdlcstate(struct BCState *bcs)
{
	modehdlc(bcs, 0, 0);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		kfree(bcs->hw.hdlc.rcvbuf);
		bcs->hw.hdlc.rcvbuf = NULL;
		kfree(bcs->blog);
		bcs->blog = NULL;
		skb_queue_purge(&bcs->rqueue);
		skb_queue_purge(&bcs->squeue);
		if (bcs->tx_skb) {
			dev_kfree_skb_any(bcs->tx_skb);
			bcs->tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
}

static int
open_hdlcstate(struct IsdnCardState *cs, struct BCState *bcs)
{
	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (!(bcs->hw.hdlc.rcvbuf = kmalloc(HSCX_BUFMAX, GFP_ATOMIC))) {
			printk(KERN_WARNING
			       "HiSax: No memory for hdlc.rcvbuf\n");
			return (1);
		}
		if (!(bcs->blog = kmalloc(MAX_BLOG_SPACE, GFP_ATOMIC))) {
			printk(KERN_WARNING
			       "HiSax: No memory for bcs->blog\n");
			test_and_clear_bit(BC_FLG_INIT, &bcs->Flag);
			kfree(bcs->hw.hdlc.rcvbuf);
			bcs->hw.hdlc.rcvbuf = NULL;
			return (2);
		}
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	bcs->event = 0;
	bcs->hw.hdlc.rcvidx = 0;
	bcs->tx_cnt = 0;
	return (0);
}

static int
setstack_hdlc(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	if (open_hdlcstate(st->l1.hardware, bcs))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = hdlc_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

#if 0
void __init
clear_pending_hdlc_ints(struct IsdnCardState *cs)
{
	u_int val;

	if (cs->subtyp == AVM_FRITZ_PCI) {
		val = ReadHDLCPCI(cs, 0, HDLC_STATUS);
		debugl1(cs, "HDLC 1 STA %x", val);
		val = ReadHDLCPCI(cs, 1, HDLC_STATUS);
		debugl1(cs, "HDLC 2 STA %x", val);
	} else {
		val = ReadHDLCPnP(cs, 0, HDLC_STATUS);
		debugl1(cs, "HDLC 1 STA %x", val);
		val = ReadHDLCPnP(cs, 0, HDLC_STATUS + 1);
		debugl1(cs, "HDLC 1 RML %x", val);
		val = ReadHDLCPnP(cs, 0, HDLC_STATUS + 2);
		debugl1(cs, "HDLC 1 MODE %x", val);
		val = ReadHDLCPnP(cs, 0, HDLC_STATUS + 3);
		debugl1(cs, "HDLC 1 VIN %x", val);
		val = ReadHDLCPnP(cs, 1, HDLC_STATUS);
		debugl1(cs, "HDLC 2 STA %x", val);
		val = ReadHDLCPnP(cs, 1, HDLC_STATUS + 1);
		debugl1(cs, "HDLC 2 RML %x", val);
		val = ReadHDLCPnP(cs, 1, HDLC_STATUS + 2);
		debugl1(cs, "HDLC 2 MODE %x", val);
		val = ReadHDLCPnP(cs, 1, HDLC_STATUS + 3);
		debugl1(cs, "HDLC 2 VIN %x", val);
	}
}
#endif  /*  0  */

static void
inithdlc(struct IsdnCardState *cs)
{
	cs->bcs[0].BC_SetStack = setstack_hdlc;
	cs->bcs[1].BC_SetStack = setstack_hdlc;
	cs->bcs[0].BC_Close = close_hdlcstate;
	cs->bcs[1].BC_Close = close_hdlcstate;
	modehdlc(cs->bcs, -1, 0);
	modehdlc(cs->bcs + 1, -1, 1);
}

static irqreturn_t
avm_pcipnp_interrupt(int intno, void *dev_id)
{
	struct IsdnCardState *cs = dev_id;
	u_long flags;
	u_char val;
	u_char sval;

	spin_lock_irqsave(&cs->lock, flags);
	sval = inb(cs->hw.avm.cfg_reg + 2);
	if ((sval & AVM_STATUS0_IRQ_MASK) == AVM_STATUS0_IRQ_MASK) {
		/* possible a shared  IRQ reqest */
		spin_unlock_irqrestore(&cs->lock, flags);
		return IRQ_NONE;
	}
	if (!(sval & AVM_STATUS0_IRQ_ISAC)) {
		val = ReadISAC(cs, ISAC_ISTA);
		isac_interrupt(cs, val);
	}
	if (!(sval & AVM_STATUS0_IRQ_HDLC)) {
		HDLC_irq_main(cs);
	}
	WriteISAC(cs, ISAC_MASK, 0xFF);
	WriteISAC(cs, ISAC_MASK, 0x0);
	spin_unlock_irqrestore(&cs->lock, flags);
	return IRQ_HANDLED;
}

static void
reset_avmpcipnp(struct IsdnCardState *cs)
{
	printk(KERN_INFO "AVM PCI/PnP: reset\n");
	outb(AVM_STATUS0_RESET | AVM_STATUS0_DIS_TIMER, cs->hw.avm.cfg_reg + 2);
	mdelay(10);
	outb(AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER | AVM_STATUS0_ENA_IRQ, cs->hw.avm.cfg_reg + 2);
	outb(AVM_STATUS1_ENA_IOM | cs->irq, cs->hw.avm.cfg_reg + 3);
	mdelay(10);
	printk(KERN_INFO "AVM PCI/PnP: S1 %x\n", inb(cs->hw.avm.cfg_reg + 3));
}

static int
AVM_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	u_long flags;

	switch (mt) {
	case CARD_RESET:
		spin_lock_irqsave(&cs->lock, flags);
		reset_avmpcipnp(cs);
		spin_unlock_irqrestore(&cs->lock, flags);
		return (0);
	case CARD_RELEASE:
		outb(0, cs->hw.avm.cfg_reg + 2);
		release_region(cs->hw.avm.cfg_reg, 32);
		return (0);
	case CARD_INIT:
		spin_lock_irqsave(&cs->lock, flags);
		reset_avmpcipnp(cs);
		clear_pending_isac_ints(cs);
		initisac(cs);
		inithdlc(cs);
		outb(AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER,
		     cs->hw.avm.cfg_reg + 2);
		WriteISAC(cs, ISAC_MASK, 0);
		outb(AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER |
		     AVM_STATUS0_ENA_IRQ, cs->hw.avm.cfg_reg + 2);
		/* RESET Receiver and Transmitter */
		WriteISAC(cs, ISAC_CMDR, 0x41);
		spin_unlock_irqrestore(&cs->lock, flags);
		return (0);
	case CARD_TEST:
		return (0);
	}
	return (0);
}

static int avm_setup_rest(struct IsdnCardState *cs)
{
	u_int val, ver;

	cs->hw.avm.isac = cs->hw.avm.cfg_reg + 0x10;
	if (!request_region(cs->hw.avm.cfg_reg, 32,
			    (cs->subtyp == AVM_FRITZ_PCI) ? "avm PCI" : "avm PnP")) {
		printk(KERN_WARNING
		       "HiSax: Fritz!PCI/PNP config port %x-%x already in use\n",
		       cs->hw.avm.cfg_reg,
		       cs->hw.avm.cfg_reg + 31);
		return (0);
	}
	switch (cs->subtyp) {
	case AVM_FRITZ_PCI:
		val = inl(cs->hw.avm.cfg_reg);
		printk(KERN_INFO "AVM PCI: stat %#x\n", val);
		printk(KERN_INFO "AVM PCI: Class %X Rev %d\n",
		       val & 0xff, (val >> 8) & 0xff);
		cs->BC_Read_Reg = &ReadHDLC_s;
		cs->BC_Write_Reg = &WriteHDLC_s;
		break;
	case AVM_FRITZ_PNP:
		val = inb(cs->hw.avm.cfg_reg);
		ver = inb(cs->hw.avm.cfg_reg + 1);
		printk(KERN_INFO "AVM PnP: Class %X Rev %d\n", val, ver);
		cs->BC_Read_Reg = &ReadHDLCPnP;
		cs->BC_Write_Reg = &WriteHDLCPnP;
		break;
	default:
		printk(KERN_WARNING "AVM unknown subtype %d\n", cs->subtyp);
		return (0);
	}
	printk(KERN_INFO "HiSax: %s config irq:%d base:0x%X\n",
	       (cs->subtyp == AVM_FRITZ_PCI) ? "AVM Fritz!PCI" : "AVM Fritz!PnP",
	       cs->irq, cs->hw.avm.cfg_reg);

	setup_isac(cs);
	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Send_Data = &hdlc_fill_fifo;
	cs->cardmsg = &AVM_card_msg;
	cs->irq_func = &avm_pcipnp_interrupt;
	cs->writeisac(cs, ISAC_MASK, 0xFF);
	ISACVersion(cs, (cs->subtyp == AVM_FRITZ_PCI) ? "AVM PCI:" : "AVM PnP:");
	return (1);
}

#ifndef __ISAPNP__

static int avm_pnp_setup(struct IsdnCardState *cs)
{
	return (1);	/* no-op: success */
}

#else

static struct pnp_card *pnp_avm_c = NULL;

static int avm_pnp_setup(struct IsdnCardState *cs)
{
	struct pnp_dev *pnp_avm_d = NULL;

	if (!isapnp_present())
		return (1);	/* no-op: success */

	if ((pnp_avm_c = pnp_find_card(
		     ISAPNP_VENDOR('A', 'V', 'M'),
		     ISAPNP_FUNCTION(0x0900), pnp_avm_c))) {
		if ((pnp_avm_d = pnp_find_dev(pnp_avm_c,
					      ISAPNP_VENDOR('A', 'V', 'M'),
					      ISAPNP_FUNCTION(0x0900), pnp_avm_d))) {
			int err;

			pnp_disable_dev(pnp_avm_d);
			err = pnp_activate_dev(pnp_avm_d);
			if (err < 0) {
				printk(KERN_WARNING "%s: pnp_activate_dev ret(%d)\n",
				       __func__, err);
				return (0);
			}
			cs->hw.avm.cfg_reg =
				pnp_port_start(pnp_avm_d, 0);
			cs->irq = pnp_irq(pnp_avm_d, 0);
			if (!cs->irq) {
				printk(KERN_ERR "FritzPnP:No IRQ\n");
				return (0);
			}
			if (!cs->hw.avm.cfg_reg) {
				printk(KERN_ERR "FritzPnP:No IO address\n");
				return (0);
			}
			cs->subtyp = AVM_FRITZ_PNP;

			return (2);	/* goto 'ready' label */
		}
	}

	return (1);
}

#endif /* __ISAPNP__ */

#ifndef CONFIG_PCI

static int avm_pci_setup(struct IsdnCardState *cs)
{
	return (1);	/* no-op: success */
}

#else

static struct pci_dev *dev_avm = NULL;

static int avm_pci_setup(struct IsdnCardState *cs)
{
	if ((dev_avm = hisax_find_pci_device(PCI_VENDOR_ID_AVM,
					     PCI_DEVICE_ID_AVM_A1, dev_avm))) {

		if (pci_enable_device(dev_avm))
			return (0);

		cs->irq = dev_avm->irq;
		if (!cs->irq) {
			printk(KERN_ERR "FritzPCI: No IRQ for PCI card found\n");
			return (0);
		}

		cs->hw.avm.cfg_reg = pci_resource_start(dev_avm, 1);
		if (!cs->hw.avm.cfg_reg) {
			printk(KERN_ERR "FritzPCI: No IO-Adr for PCI card found\n");
			return (0);
		}

		cs->subtyp = AVM_FRITZ_PCI;
	} else {
		printk(KERN_WARNING "FritzPCI: No PCI card found\n");
		return (0);
	}

	cs->irq_flags |= IRQF_SHARED;

	return (1);
}

#endif /* CONFIG_PCI */

int setup_avm_pcipnp(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];
	int rc;

	strcpy(tmp, avm_pci_rev);
	printk(KERN_INFO "HiSax: AVM PCI driver Rev. %s\n", HiSax_getrev(tmp));

	if (cs->typ != ISDN_CTYPE_FRITZPCI)
		return (0);

	if (card->para[1]) {
		/* old manual method */
		cs->hw.avm.cfg_reg = card->para[1];
		cs->irq = card->para[0];
		cs->subtyp = AVM_FRITZ_PNP;
		goto ready;
	}

	rc = avm_pnp_setup(cs);
	if (rc < 1)
		return (0);
	if (rc == 2)
		goto ready;

	rc = avm_pci_setup(cs);
	if (rc < 1)
		return (0);

ready:
	return avm_setup_rest(cs);
}
