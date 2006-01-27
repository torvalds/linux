/* $Id: jade_irq.c,v 1.7.2.4 2004/02/11 13:21:34 keil Exp $
 *
 * Low level JADE IRQ stuff (derived from original hscx_irq.c)
 *
 * Author       Roland Klabunde
 * Copyright    by Roland Klabunde   <R.Klabunde@Berkom.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

static inline void
waitforCEC(struct IsdnCardState *cs, int jade, int reg)
{
  	int to = 50;
  	int mask = (reg == jade_HDLC_XCMD ? jadeSTAR_XCEC : jadeSTAR_RCEC);
  	while ((READJADE(cs, jade, jade_HDLC_STAR) & mask) && to) {
  		udelay(1);
  		to--;
  	}
  	if (!to)
  		printk(KERN_WARNING "HiSax: waitforCEC (jade) timeout\n");
}


static inline void
waitforXFW(struct IsdnCardState *cs, int jade)
{
  	/* Does not work on older jade versions, don't care */
}

static inline void
WriteJADECMDR(struct IsdnCardState *cs, int jade, int reg, u_char data)
{
	waitforCEC(cs, jade, reg);
	WRITEJADE(cs, jade, reg, data);
}



static void
jade_empty_fifo(struct BCState *bcs, int count)
{
	u_char *ptr;
	struct IsdnCardState *cs = bcs->cs;

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "jade_empty_fifo");

	if (bcs->hw.hscx.rcvidx + count > HSCX_BUFMAX) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "jade_empty_fifo: incoming packet too large");
		WriteJADECMDR(cs, bcs->hw.hscx.hscx, jade_HDLC_RCMD, jadeRCMD_RMC);
		bcs->hw.hscx.rcvidx = 0;
		return;
	}
	ptr = bcs->hw.hscx.rcvbuf + bcs->hw.hscx.rcvidx;
	bcs->hw.hscx.rcvidx += count;
	READJADEFIFO(cs, bcs->hw.hscx.hscx, ptr, count);
	WriteJADECMDR(cs, bcs->hw.hscx.hscx, jade_HDLC_RCMD, jadeRCMD_RMC);
	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char *t = bcs->blog;

		t += sprintf(t, "jade_empty_fifo %c cnt %d",
			     bcs->hw.hscx.hscx ? 'B' : 'A', count);
		QuickHex(t, ptr, count);
		debugl1(cs, bcs->blog);
	}
}

static void
jade_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int more, count;
	int fifo_size = 32;
	u_char *ptr;

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "jade_fill_fifo");

	if (!bcs->tx_skb)
		return;
	if (bcs->tx_skb->len <= 0)
		return;

	more = (bcs->mode == L1_MODE_TRANS) ? 1 : 0;
	if (bcs->tx_skb->len > fifo_size) {
		more = !0;
		count = fifo_size;
	} else
		count = bcs->tx_skb->len;

	waitforXFW(cs, bcs->hw.hscx.hscx);
	ptr = bcs->tx_skb->data;
	skb_pull(bcs->tx_skb, count);
	bcs->tx_cnt -= count;
	bcs->hw.hscx.count += count;
	WRITEJADEFIFO(cs, bcs->hw.hscx.hscx, ptr, count);
	WriteJADECMDR(cs, bcs->hw.hscx.hscx, jade_HDLC_XCMD, more ? jadeXCMD_XF : (jadeXCMD_XF|jadeXCMD_XME));
	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char *t = bcs->blog;

		t += sprintf(t, "jade_fill_fifo %c cnt %d",
			     bcs->hw.hscx.hscx ? 'B' : 'A', count);
		QuickHex(t, ptr, count);
		debugl1(cs, bcs->blog);
	}
}


static void
jade_interrupt(struct IsdnCardState *cs, u_char val, u_char jade)
{
	u_char r;
	struct BCState *bcs = cs->bcs + jade;
	struct sk_buff *skb;
	int fifo_size = 32;
	int count;
	int i_jade = (int) jade; /* To satisfy the compiler */
	
	if (!test_bit(BC_FLG_INIT, &bcs->Flag))
		return;

	if (val & 0x80) {	/* RME */
		r = READJADE(cs, i_jade, jade_HDLC_RSTA);
		if ((r & 0xf0) != 0xa0) {
			if (!(r & 0x80))
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "JADE %s invalid frame", (jade ? "B":"A"));
			if ((r & 0x40) && bcs->mode)
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "JADE %c RDO mode=%d", 'A'+jade, bcs->mode);
			if (!(r & 0x20))
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "JADE %c CRC error", 'A'+jade);
			WriteJADECMDR(cs, jade, jade_HDLC_RCMD, jadeRCMD_RMC);
		} else {
			count = READJADE(cs, i_jade, jade_HDLC_RBCL) & 0x1F;
			if (count == 0)
				count = fifo_size;
			jade_empty_fifo(bcs, count);
			if ((count = bcs->hw.hscx.rcvidx - 1) > 0) {
				if (cs->debug & L1_DEB_HSCX_FIFO)
					debugl1(cs, "HX Frame %d", count);
				if (!(skb = dev_alloc_skb(count)))
					printk(KERN_WARNING "JADE %s receive out of memory\n", (jade ? "B":"A"));
				else {
					memcpy(skb_put(skb, count), bcs->hw.hscx.rcvbuf, count);
					skb_queue_tail(&bcs->rqueue, skb);
				}
			}
		}
		bcs->hw.hscx.rcvidx = 0;
		schedule_event(bcs, B_RCVBUFREADY);
	}
	if (val & 0x40) {	/* RPF */
		jade_empty_fifo(bcs, fifo_size);
		if (bcs->mode == L1_MODE_TRANS) {
			/* receive audio data */
			if (!(skb = dev_alloc_skb(fifo_size)))
				printk(KERN_WARNING "HiSax: receive out of memory\n");
			else {
				memcpy(skb_put(skb, fifo_size), bcs->hw.hscx.rcvbuf, fifo_size);
				skb_queue_tail(&bcs->rqueue, skb);
			}
			bcs->hw.hscx.rcvidx = 0;
			schedule_event(bcs, B_RCVBUFREADY);
		}
	}
	if (val & 0x10) {	/* XPR */
		if (bcs->tx_skb) {
			if (bcs->tx_skb->len) {
				jade_fill_fifo(bcs);
				return;
			} else {
				if (test_bit(FLG_LLI_L1WAKEUP,&bcs->st->lli.flag) &&
					(PACKET_NOACK != bcs->tx_skb->pkt_type)) {
					u_long	flags;
					spin_lock_irqsave(&bcs->aclock, flags);
					bcs->ackcnt += bcs->hw.hscx.count;
					spin_unlock_irqrestore(&bcs->aclock, flags);
					schedule_event(bcs, B_ACKPENDING);
				}
				dev_kfree_skb_irq(bcs->tx_skb);
				bcs->hw.hscx.count = 0;
				bcs->tx_skb = NULL;
			}
		}
		if ((bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
			bcs->hw.hscx.count = 0;
			test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
			jade_fill_fifo(bcs);
		} else {
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
			schedule_event(bcs, B_XMTBUFREADY);
		}
	}
}

static inline void
jade_int_main(struct IsdnCardState *cs, u_char val, int jade)
{
	struct BCState *bcs;
	bcs = cs->bcs + jade;
	
	if (val & jadeISR_RFO) {
		/* handled with RDO */
		val &= ~jadeISR_RFO;
	}
	if (val & jadeISR_XDU) {
		/* relevant in HDLC mode only */
		/* don't reset XPR here */
		if (bcs->mode == 1)
			jade_fill_fifo(bcs);
		else {
			/* Here we lost an TX interrupt, so
			   * restart transmitting the whole frame.
			 */
			if (bcs->tx_skb) {
			   	skb_push(bcs->tx_skb, bcs->hw.hscx.count);
				bcs->tx_cnt += bcs->hw.hscx.count;
				bcs->hw.hscx.count = 0;
			}
			WriteJADECMDR(cs, bcs->hw.hscx.hscx, jade_HDLC_XCMD, jadeXCMD_XRES);
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "JADE %c EXIR %x Lost TX", 'A'+jade, val);
		}
	}
	if (val & (jadeISR_RME|jadeISR_RPF|jadeISR_XPR)) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "JADE %c interrupt %x", 'A'+jade, val);
		jade_interrupt(cs, val, jade);
	}
}
