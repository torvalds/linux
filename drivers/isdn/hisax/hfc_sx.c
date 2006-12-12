/* $Id: hfc_sx.c,v 1.12.2.5 2004/02/11 13:21:33 keil Exp $
 *
 * level driver for Cologne Chip Designs hfc-s+/sp based cards
 *
 * Author       Werner Cornelius
 *              based on existing driver for CCD HFC PCI cards
 * Copyright    by Werner Cornelius  <werner@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "hfc_sx.h"
#include "isdnl1.h"
#include <linux/interrupt.h>
#include <linux/isapnp.h>

extern const char *CardType[];

static const char *hfcsx_revision = "$Revision: 1.12.2.5 $";

/***************************************/
/* IRQ-table for CCDs demo board       */
/* IRQs 6,5,10,11,12,15 are supported  */
/***************************************/

/* Teles 16.3c Vendor Id TAG2620, Version 1.0, Vendor version 2.1
 *
 * Thanks to Uwe Wisniewski
 *
 * ISA-SLOT  Signal      PIN
 * B25        IRQ3     92 IRQ_G
 * B23        IRQ5     94 IRQ_A
 * B4         IRQ2/9   95 IRQ_B
 * D3         IRQ10    96 IRQ_C
 * D4         IRQ11    97 IRQ_D
 * D5         IRQ12    98 IRQ_E
 * D6         IRQ15    99 IRQ_F
 */

#undef CCD_DEMO_BOARD
#ifdef CCD_DEMO_BOARD
static u_char ccd_sp_irqtab[16] = {
  0,0,0,0,0,2,1,0,0,0,3,4,5,0,0,6
};
#else /* Teles 16.3c */
static u_char ccd_sp_irqtab[16] = {
  0,0,0,7,0,1,0,0,0,2,3,4,5,0,0,6
};
#endif
#define NT_T1_COUNT 20		/* number of 3.125ms interrupts for G2 timeout */

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

/******************************/
/* In/Out access to registers */
/******************************/
static inline void
Write_hfc(struct IsdnCardState *cs, u_char regnum, u_char val)
{
        byteout(cs->hw.hfcsx.base+1, regnum);
	byteout(cs->hw.hfcsx.base, val);
} 

static inline u_char
Read_hfc(struct IsdnCardState *cs, u_char regnum)
{
        u_char ret; 

        byteout(cs->hw.hfcsx.base+1, regnum);
	ret = bytein(cs->hw.hfcsx.base);
	return(ret);
} 


/**************************************************/
/* select a fifo and remember which one for reuse */
/**************************************************/
static void
fifo_select(struct IsdnCardState *cs, u_char fifo)
{
        if (fifo == cs->hw.hfcsx.last_fifo) 
	  return; /* still valid */

        byteout(cs->hw.hfcsx.base+1, HFCSX_FIF_SEL);
	byteout(cs->hw.hfcsx.base, fifo);
	while (bytein(cs->hw.hfcsx.base+1) & 1); /* wait for busy */
	udelay(4);
	byteout(cs->hw.hfcsx.base, fifo);
	while (bytein(cs->hw.hfcsx.base+1) & 1); /* wait for busy */
}

/******************************************/
/* reset the specified fifo to defaults.  */
/* If its a send fifo init needed markers */
/******************************************/
static void
reset_fifo(struct IsdnCardState *cs, u_char fifo)
{
	fifo_select(cs, fifo); /* first select the fifo */
	byteout(cs->hw.hfcsx.base+1, HFCSX_CIRM);
	byteout(cs->hw.hfcsx.base, cs->hw.hfcsx.cirm | 0x80); /* reset cmd */
	udelay(1);
	while (bytein(cs->hw.hfcsx.base+1) & 1); /* wait for busy */
} 


/*************************************************************/
/* write_fifo writes the skb contents to the desired fifo    */
/* if no space is available or an error occurs 0 is returned */
/* the skb is not released in any way.                       */
/*************************************************************/
static int
write_fifo(struct IsdnCardState *cs, struct sk_buff *skb, u_char fifo, int trans_max)
{
       unsigned short *msp;
        int fifo_size, count, z1, z2;
	u_char f_msk, f1, f2, *src;

	if (skb->len <= 0) return(0);
        if (fifo & 1) return(0); /* no write fifo */

	fifo_select(cs, fifo);
	if (fifo & 4) {
	  fifo_size = D_FIFO_SIZE; /* D-channel */
	  f_msk = MAX_D_FRAMES;
	  if (trans_max) return(0); /* only HDLC */
	}
	else {
	  fifo_size = cs->hw.hfcsx.b_fifo_size; /* B-channel */
	  f_msk = MAX_B_FRAMES;
	}

        z1 = Read_hfc(cs, HFCSX_FIF_Z1H);
	z1 = ((z1 << 8) | Read_hfc(cs, HFCSX_FIF_Z1L));

	/* Check for transparent mode */
	if (trans_max) {
	  z2 = Read_hfc(cs, HFCSX_FIF_Z2H);
	  z2 = ((z2 << 8) | Read_hfc(cs, HFCSX_FIF_Z2L));
	  count = z2 - z1;
	  if (count <= 0)
	    count += fifo_size; /* free bytes */
	  if (count < skb->len+1) return(0); /* no room */
	  count = fifo_size - count; /* bytes still not send */
	  if (count > 2 * trans_max) return(0); /* delay to long */
	  count = skb->len;
	  src = skb->data;
	  while (count--)
	    Write_hfc(cs, HFCSX_FIF_DWR, *src++);
	  return(1); /* success */
	}

        msp = ((struct hfcsx_extra *)(cs->hw.hfcsx.extra))->marker;
	msp += (((fifo >> 1) & 3) * (MAX_B_FRAMES+1));
	f1 = Read_hfc(cs, HFCSX_FIF_F1) & f_msk;
	f2 = Read_hfc(cs, HFCSX_FIF_F2) & f_msk;

	count = f1 - f2; /* frame count actually buffered */
	if (count < 0)
		count += (f_msk + 1);	/* if wrap around */
	if (count > f_msk-1) {
	  if (cs->debug & L1_DEB_ISAC_FIFO)
	    debugl1(cs, "hfcsx_write_fifo %d more as %d frames",fifo,f_msk-1);
	  return(0);
	}

	*(msp + f1) = z1; /* remember marker */

	if (cs->debug & L1_DEB_ISAC_FIFO)
		debugl1(cs, "hfcsx_write_fifo %d f1(%x) f2(%x) z1(f1)(%x)",
			fifo, f1, f2, z1);
	/* now determine free bytes in FIFO buffer */
	count = *(msp + f2) - z1;
	if (count <= 0)
	  count += fifo_size;	/* count now contains available bytes */

	if (cs->debug & L1_DEB_ISAC_FIFO)
	  debugl1(cs, "hfcsx_write_fifo %d count(%ld/%d)",
		  fifo, skb->len, count);
	if (count < skb->len) {
	  if (cs->debug & L1_DEB_ISAC_FIFO)
	    debugl1(cs, "hfcsx_write_fifo %d no fifo mem", fifo);
	  return(0);
	}
	
	count = skb->len; /* get frame len */
	src = skb->data;	/* source pointer */
	while (count--)
	  Write_hfc(cs, HFCSX_FIF_DWR, *src++);
	
	Read_hfc(cs, HFCSX_FIF_INCF1); /* increment F1 */
	udelay(1);
	while (bytein(cs->hw.hfcsx.base+1) & 1); /* wait for busy */
	return(1);
} 

/***************************************************************/
/* read_fifo reads data to an skb from the desired fifo        */
/* if no data is available or an error occurs NULL is returned */
/* the skb is not released in any way.                         */
/***************************************************************/
static struct sk_buff * 
read_fifo(struct IsdnCardState *cs, u_char fifo, int trans_max)
{       int fifo_size, count, z1, z2;
	u_char f_msk, f1, f2, *dst;
	struct sk_buff *skb;

        if (!(fifo & 1)) return(NULL); /* no read fifo */
	fifo_select(cs, fifo);
	if (fifo & 4) {
	  fifo_size = D_FIFO_SIZE; /* D-channel */
	  f_msk = MAX_D_FRAMES;
	  if (trans_max) return(NULL); /* only hdlc */
	}
	else {
	  fifo_size = cs->hw.hfcsx.b_fifo_size; /* B-channel */
	  f_msk = MAX_B_FRAMES;
	}

	/* transparent mode */
	if (trans_max) {
	  z1 = Read_hfc(cs, HFCSX_FIF_Z1H);
	  z1 = ((z1 << 8) | Read_hfc(cs, HFCSX_FIF_Z1L));
	  z2 = Read_hfc(cs, HFCSX_FIF_Z2H);
	  z2 = ((z2 << 8) | Read_hfc(cs, HFCSX_FIF_Z2L));
	  /* now determine bytes in actual FIFO buffer */
	  count = z1 - z2;
	  if (count <= 0)
	    count += fifo_size;	/* count now contains buffered bytes */
	  count++;
	  if (count > trans_max) 
	    count = trans_max; /* limit length */
	    if ((skb = dev_alloc_skb(count))) {
	      dst = skb_put(skb, count);
	      while (count--) 
		*dst++ = Read_hfc(cs, HFCSX_FIF_DRD);
	      return(skb);
	    }
	    else return(NULL); /* no memory */
	}

	do {
	  f1 = Read_hfc(cs, HFCSX_FIF_F1) & f_msk;
	  f2 = Read_hfc(cs, HFCSX_FIF_F2) & f_msk;

	  if (f1 == f2) return(NULL); /* no frame available */

	  z1 = Read_hfc(cs, HFCSX_FIF_Z1H);
	  z1 = ((z1 << 8) | Read_hfc(cs, HFCSX_FIF_Z1L));
	  z2 = Read_hfc(cs, HFCSX_FIF_Z2H);
	  z2 = ((z2 << 8) | Read_hfc(cs, HFCSX_FIF_Z2L));

	  if (cs->debug & L1_DEB_ISAC_FIFO)
	    debugl1(cs, "hfcsx_read_fifo %d f1(%x) f2(%x) z1(f2)(%x) z2(f2)(%x)",
			fifo, f1, f2, z1, z2);
	  /* now determine bytes in actual FIFO buffer */
	  count = z1 - z2;
	  if (count <= 0)
	    count += fifo_size;	/* count now contains buffered bytes */
	  count++;

	  if (cs->debug & L1_DEB_ISAC_FIFO)
	    debugl1(cs, "hfcsx_read_fifo %d count %ld)",
		    fifo, count);

	  if ((count > fifo_size) || (count < 4)) {
	    if (cs->debug & L1_DEB_WARN)
	      debugl1(cs, "hfcsx_read_fifo %d paket inv. len %d ", fifo , count);
	    while (count) {
	      count--; /* empty fifo */
	      Read_hfc(cs, HFCSX_FIF_DRD);
	    }
	    skb = NULL;
	  } else 
	    if ((skb = dev_alloc_skb(count - 3))) {
	      count -= 3;
	      dst = skb_put(skb, count);

	      while (count--) 
		*dst++ = Read_hfc(cs, HFCSX_FIF_DRD);
		    
	      Read_hfc(cs, HFCSX_FIF_DRD); /* CRC 1 */
	      Read_hfc(cs, HFCSX_FIF_DRD); /* CRC 2 */
	      if (Read_hfc(cs, HFCSX_FIF_DRD)) {
		dev_kfree_skb_irq(skb);
		if (cs->debug & L1_DEB_ISAC_FIFO)
		  debugl1(cs, "hfcsx_read_fifo %d crc error", fifo);
		skb = NULL;
	      }
	    } else {
	      printk(KERN_WARNING "HFC-SX: receive out of memory\n");
	      return(NULL);
	    }

	  Read_hfc(cs, HFCSX_FIF_INCF2); /* increment F2 */
	  udelay(1);
	  while (bytein(cs->hw.hfcsx.base+1) & 1); /* wait for busy */
	  udelay(1);
	} while (!skb); /* retry in case of crc error */
	return(skb);
} 

/******************************************/
/* free hardware resources used by driver */
/******************************************/
static void
release_io_hfcsx(struct IsdnCardState *cs)
{
	cs->hw.hfcsx.int_m2 = 0;	/* interrupt output off ! */
	Write_hfc(cs, HFCSX_INT_M2, cs->hw.hfcsx.int_m2);
	Write_hfc(cs, HFCSX_CIRM, HFCSX_RESET);	/* Reset On */
	msleep(30);				/* Timeout 30ms */
	Write_hfc(cs, HFCSX_CIRM, 0);	/* Reset Off */
	del_timer(&cs->hw.hfcsx.timer);
	release_region(cs->hw.hfcsx.base, 2); /* release IO-Block */
	kfree(cs->hw.hfcsx.extra);
	cs->hw.hfcsx.extra = NULL;
}

/**********************************************************/
/* set_fifo_size determines the size of the RAM and FIFOs */
/* returning 0 -> need to reset the chip again.           */
/**********************************************************/
static int set_fifo_size(struct IsdnCardState *cs)
{
        
        if (cs->hw.hfcsx.b_fifo_size) return(1); /* already determined */

	if ((cs->hw.hfcsx.chip >> 4) == 9) {
	  cs->hw.hfcsx.b_fifo_size = B_FIFO_SIZE_32K;
	  return(1);
	}

	  cs->hw.hfcsx.b_fifo_size = B_FIFO_SIZE_8K;
	  cs->hw.hfcsx.cirm |= 0x10; /* only 8K of ram */
	  return(0);

}

/********************************************************************************/
/* function called to reset the HFC SX chip. A complete software reset of chip */
/* and fifos is done.                                                           */
/********************************************************************************/
static void
reset_hfcsx(struct IsdnCardState *cs)
{
	cs->hw.hfcsx.int_m2 = 0;	/* interrupt output off ! */
	Write_hfc(cs, HFCSX_INT_M2, cs->hw.hfcsx.int_m2);

	printk(KERN_INFO "HFC_SX: resetting card\n");
	while (1) {
	  Write_hfc(cs, HFCSX_CIRM, HFCSX_RESET | cs->hw.hfcsx.cirm ); /* Reset */
	  mdelay(30);
	  Write_hfc(cs, HFCSX_CIRM, cs->hw.hfcsx.cirm); /* Reset Off */
	  mdelay(20);
	  if (Read_hfc(cs, HFCSX_STATUS) & 2)
	    printk(KERN_WARNING "HFC-SX init bit busy\n");
	  cs->hw.hfcsx.last_fifo = 0xff; /* invalidate */
	  if (!set_fifo_size(cs)) continue;
	  break;
	}

	cs->hw.hfcsx.trm = 0 + HFCSX_BTRANS_THRESMASK;	/* no echo connect , threshold */
	Write_hfc(cs, HFCSX_TRM, cs->hw.hfcsx.trm);

	Write_hfc(cs, HFCSX_CLKDEL, 0x0e);	/* ST-Bit delay for TE-Mode */
	cs->hw.hfcsx.sctrl_e = HFCSX_AUTO_AWAKE;
	Write_hfc(cs, HFCSX_SCTRL_E, cs->hw.hfcsx.sctrl_e);	/* S/T Auto awake */
	cs->hw.hfcsx.bswapped = 0;	/* no exchange */
	cs->hw.hfcsx.nt_mode = 0;	/* we are in TE mode */
	cs->hw.hfcsx.ctmt = HFCSX_TIM3_125 | HFCSX_AUTO_TIMER;
	Write_hfc(cs, HFCSX_CTMT, cs->hw.hfcsx.ctmt);

	cs->hw.hfcsx.int_m1 = HFCSX_INTS_DTRANS | HFCSX_INTS_DREC | 
	    HFCSX_INTS_L1STATE | HFCSX_INTS_TIMER;
	Write_hfc(cs, HFCSX_INT_M1, cs->hw.hfcsx.int_m1);

	/* Clear already pending ints */
	if (Read_hfc(cs, HFCSX_INT_S1));

	Write_hfc(cs, HFCSX_STATES, HFCSX_LOAD_STATE | 2);	/* HFC ST 2 */
	udelay(10);
	Write_hfc(cs, HFCSX_STATES, 2);	/* HFC ST 2 */
	cs->hw.hfcsx.mst_m = HFCSX_MASTER;	/* HFC Master Mode */

	Write_hfc(cs, HFCSX_MST_MODE, cs->hw.hfcsx.mst_m);
	cs->hw.hfcsx.sctrl = 0x40;	/* set tx_lo mode, error in datasheet ! */
	Write_hfc(cs, HFCSX_SCTRL, cs->hw.hfcsx.sctrl);
	cs->hw.hfcsx.sctrl_r = 0;
	Write_hfc(cs, HFCSX_SCTRL_R, cs->hw.hfcsx.sctrl_r);

	/* Init GCI/IOM2 in master mode */
	/* Slots 0 and 1 are set for B-chan 1 and 2 */
	/* D- and monitor/CI channel are not enabled */
	/* STIO1 is used as output for data, B1+B2 from ST->IOM+HFC */
	/* STIO2 is used as data input, B1+B2 from IOM->ST */
	/* ST B-channel send disabled -> continous 1s */
	/* The IOM slots are always enabled */
	cs->hw.hfcsx.conn = 0x36;	/* set data flow directions */
	Write_hfc(cs, HFCSX_CONNECT, cs->hw.hfcsx.conn);
	Write_hfc(cs, HFCSX_B1_SSL, 0x80);	/* B1-Slot 0 STIO1 out enabled */
	Write_hfc(cs, HFCSX_B2_SSL, 0x81);	/* B2-Slot 1 STIO1 out enabled */
	Write_hfc(cs, HFCSX_B1_RSL, 0x80);	/* B1-Slot 0 STIO2 in enabled */
	Write_hfc(cs, HFCSX_B2_RSL, 0x81);	/* B2-Slot 1 STIO2 in enabled */

	/* Finally enable IRQ output */
	cs->hw.hfcsx.int_m2 = HFCSX_IRQ_ENABLE;
	Write_hfc(cs, HFCSX_INT_M2, cs->hw.hfcsx.int_m2);
	if (Read_hfc(cs, HFCSX_INT_S2));
}

/***************************************************/
/* Timer function called when kernel timer expires */
/***************************************************/
static void
hfcsx_Timer(struct IsdnCardState *cs)
{
	cs->hw.hfcsx.timer.expires = jiffies + 75;
	/* WD RESET */
/*      WriteReg(cs, HFCD_DATA, HFCD_CTMT, cs->hw.hfcsx.ctmt | 0x80);
   add_timer(&cs->hw.hfcsx.timer);
 */
}

/************************************************/
/* select a b-channel entry matching and active */
/************************************************/
static
struct BCState *
Sel_BCS(struct IsdnCardState *cs, int channel)
{
	if (cs->bcs[0].mode && (cs->bcs[0].channel == channel))
		return (&cs->bcs[0]);
	else if (cs->bcs[1].mode && (cs->bcs[1].channel == channel))
		return (&cs->bcs[1]);
	else
		return (NULL);
}

/*******************************/
/* D-channel receive procedure */
/*******************************/
static
int
receive_dmsg(struct IsdnCardState *cs)
{
	struct sk_buff *skb;
	int count = 5;

	if (test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
		debugl1(cs, "rec_dmsg blocked");
		return (1);
	}

	do {
	  skb = read_fifo(cs, HFCSX_SEL_D_RX, 0);
	  if (skb) {
	    skb_queue_tail(&cs->rq, skb);
	    schedule_event(cs, D_RCVBUFREADY);
	  }
	} while (--count && skb);

	test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	return (1);
}

/**********************************/
/* B-channel main receive routine */
/**********************************/
static void
main_rec_hfcsx(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int count = 5;
	struct sk_buff *skb;

      Begin:
	count--;
	if (test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
		debugl1(cs, "rec_data %d blocked", bcs->channel);
		return;
	}
	skb = read_fifo(cs, ((bcs->channel) && (!cs->hw.hfcsx.bswapped)) ? 
			HFCSX_SEL_B2_RX : HFCSX_SEL_B1_RX,
			(bcs->mode == L1_MODE_TRANS) ? 
			HFCSX_BTRANS_THRESHOLD : 0);

	if (skb) {
	  skb_queue_tail(&bcs->rqueue, skb);
	  schedule_event(bcs, B_RCVBUFREADY);
	}

	test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	if (count && skb)
		goto Begin;
	return;
}

/**************************/
/* D-channel send routine */
/**************************/
static void
hfcsx_fill_dfifo(struct IsdnCardState *cs)
{
	if (!cs->tx_skb)
		return;
	if (cs->tx_skb->len <= 0)
		return;

	if (write_fifo(cs, cs->tx_skb, HFCSX_SEL_D_TX, 0)) {
	  dev_kfree_skb_any(cs->tx_skb);
	  cs->tx_skb = NULL;
	}
	return;
}

/**************************/
/* B-channel send routine */
/**************************/
static void
hfcsx_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;

	if (!bcs->tx_skb)
		return;
	if (bcs->tx_skb->len <= 0)
		return;

	if (write_fifo(cs, bcs->tx_skb, 
		       ((bcs->channel) && (!cs->hw.hfcsx.bswapped)) ? 
		       HFCSX_SEL_B2_TX : HFCSX_SEL_B1_TX,
		       (bcs->mode == L1_MODE_TRANS) ? 
		       HFCSX_BTRANS_THRESHOLD : 0)) {

	  bcs->tx_cnt -= bcs->tx_skb->len;
	  if (test_bit(FLG_LLI_L1WAKEUP,&bcs->st->lli.flag) &&
		(PACKET_NOACK != bcs->tx_skb->pkt_type)) {
		u_long	flags;
		spin_lock_irqsave(&bcs->aclock, flags);
		bcs->ackcnt += bcs->tx_skb->len;
		spin_unlock_irqrestore(&bcs->aclock, flags);
		schedule_event(bcs, B_ACKPENDING);
	  }
	  dev_kfree_skb_any(bcs->tx_skb);
	  bcs->tx_skb = NULL;
	  test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	}
}

/**********************************************/
/* D-channel l1 state call for leased NT-mode */
/**********************************************/
static void
dch_nt_l2l1(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;

	switch (pr) {
		case (PH_DATA | REQUEST):
		case (PH_PULL | REQUEST):
		case (PH_PULL | INDICATION):
			st->l1.l1hw(st, pr, arg);
			break;
		case (PH_ACTIVATE | REQUEST):
			st->l1.l1l2(st, PH_ACTIVATE | CONFIRM, NULL);
			break;
		case (PH_TESTLOOP | REQUEST):
			if (1 & (long) arg)
				debugl1(cs, "PH_TEST_LOOP B1");
			if (2 & (long) arg)
				debugl1(cs, "PH_TEST_LOOP B2");
			if (!(3 & (long) arg))
				debugl1(cs, "PH_TEST_LOOP DISABLED");
			st->l1.l1hw(st, HW_TESTLOOP | REQUEST, arg);
			break;
		default:
			if (cs->debug)
				debugl1(cs, "dch_nt_l2l1 msg %04X unhandled", pr);
			break;
	}
}



/***********************/
/* set/reset echo mode */
/***********************/
static int
hfcsx_auxcmd(struct IsdnCardState *cs, isdn_ctrl * ic)
{
	unsigned long flags;
	int i = *(unsigned int *) ic->parm.num;

	if ((ic->arg == 98) &&
	    (!(cs->hw.hfcsx.int_m1 & (HFCSX_INTS_B2TRANS + HFCSX_INTS_B2REC + HFCSX_INTS_B1TRANS + HFCSX_INTS_B1REC)))) {
	    	spin_lock_irqsave(&cs->lock, flags);
		Write_hfc(cs, HFCSX_STATES, HFCSX_LOAD_STATE | 0);	/* HFC ST G0 */
		udelay(10);
		cs->hw.hfcsx.sctrl |= SCTRL_MODE_NT;
		Write_hfc(cs, HFCSX_SCTRL, cs->hw.hfcsx.sctrl);	/* set NT-mode */
		udelay(10);
		Write_hfc(cs, HFCSX_STATES, HFCSX_LOAD_STATE | 1);	/* HFC ST G1 */
		udelay(10);
		Write_hfc(cs, HFCSX_STATES, 1 | HFCSX_ACTIVATE | HFCSX_DO_ACTION);
		cs->dc.hfcsx.ph_state = 1;
		cs->hw.hfcsx.nt_mode = 1;
		cs->hw.hfcsx.nt_timer = 0;
		spin_unlock_irqrestore(&cs->lock, flags);
		cs->stlist->l2.l2l1 = dch_nt_l2l1;
		debugl1(cs, "NT mode activated");
		return (0);
	}
	if ((cs->chanlimit > 1) || (cs->hw.hfcsx.bswapped) ||
	    (cs->hw.hfcsx.nt_mode) || (ic->arg != 12))
		return (-EINVAL);

	if (i) {
		cs->logecho = 1;
		cs->hw.hfcsx.trm |= 0x20;	/* enable echo chan */
		cs->hw.hfcsx.int_m1 |= HFCSX_INTS_B2REC;
		/* reset Channel !!!!! */
	} else {
		cs->logecho = 0;
		cs->hw.hfcsx.trm &= ~0x20;	/* disable echo chan */
		cs->hw.hfcsx.int_m1 &= ~HFCSX_INTS_B2REC;
	}
	cs->hw.hfcsx.sctrl_r &= ~SCTRL_B2_ENA;
	cs->hw.hfcsx.sctrl &= ~SCTRL_B2_ENA;
	cs->hw.hfcsx.conn |= 0x10;	/* B2-IOM -> B2-ST */
	cs->hw.hfcsx.ctmt &= ~2;
	spin_lock_irqsave(&cs->lock, flags);
	Write_hfc(cs, HFCSX_CTMT, cs->hw.hfcsx.ctmt);
	Write_hfc(cs, HFCSX_SCTRL_R, cs->hw.hfcsx.sctrl_r);
	Write_hfc(cs, HFCSX_SCTRL, cs->hw.hfcsx.sctrl);
	Write_hfc(cs, HFCSX_CONNECT, cs->hw.hfcsx.conn);
	Write_hfc(cs, HFCSX_TRM, cs->hw.hfcsx.trm);
	Write_hfc(cs, HFCSX_INT_M1, cs->hw.hfcsx.int_m1);
	spin_unlock_irqrestore(&cs->lock, flags);
	return (0);
}				/* hfcsx_auxcmd */

/*****************************/
/* E-channel receive routine */
/*****************************/
static void
receive_emsg(struct IsdnCardState *cs)
{
	int count = 5;
	u_char *ptr;
	struct sk_buff *skb;

	if (test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
		debugl1(cs, "echo_rec_data blocked");
		return;
	}
	do {
	  skb = read_fifo(cs, HFCSX_SEL_B2_RX, 0);
	  if (skb) {
	    if (cs->debug & DEB_DLOG_HEX) {
	      ptr = cs->dlog;
	      if ((skb->len) < MAX_DLOG_SPACE / 3 - 10) {
		*ptr++ = 'E';
		*ptr++ = 'C';
		*ptr++ = 'H';
		*ptr++ = 'O';
		*ptr++ = ':';
		ptr += QuickHex(ptr, skb->data, skb->len);
		ptr--;
		*ptr++ = '\n';
		*ptr = 0;
		HiSax_putstatus(cs, NULL, cs->dlog);
	      } else
		HiSax_putstatus(cs, "LogEcho: ", "warning Frame too big (%d)", skb->len);
	    }
	    dev_kfree_skb_any(skb);
	  }
	} while (--count && skb);

	test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	return;
}				/* receive_emsg */


/*********************/
/* Interrupt handler */
/*********************/
static irqreturn_t
hfcsx_interrupt(int intno, void *dev_id)
{
	struct IsdnCardState *cs = dev_id;
	u_char exval;
	struct BCState *bcs;
	int count = 15;
	u_long flags;
	u_char val, stat;

	if (!(cs->hw.hfcsx.int_m2 & 0x08))
		return IRQ_NONE;		/* not initialised */

	spin_lock_irqsave(&cs->lock, flags);
	if (HFCSX_ANYINT & (stat = Read_hfc(cs, HFCSX_STATUS))) {
		val = Read_hfc(cs, HFCSX_INT_S1);
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "HFC-SX: stat(%02x) s1(%02x)", stat, val);
	} else {
		spin_unlock_irqrestore(&cs->lock, flags);
		return IRQ_NONE;
	}
	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "HFC-SX irq %x %s", val,
			test_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags) ?
			"locked" : "unlocked");
	val &= cs->hw.hfcsx.int_m1;
	if (val & 0x40) {	/* state machine irq */
		exval = Read_hfc(cs, HFCSX_STATES) & 0xf;
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ph_state chg %d->%d", cs->dc.hfcsx.ph_state,
				exval);
		cs->dc.hfcsx.ph_state = exval;
		schedule_event(cs, D_L1STATECHANGE);
		val &= ~0x40;
	}
	if (val & 0x80) {	/* timer irq */
		if (cs->hw.hfcsx.nt_mode) {
			if ((--cs->hw.hfcsx.nt_timer) < 0)
				schedule_event(cs, D_L1STATECHANGE);
		}
		val &= ~0x80;
		Write_hfc(cs, HFCSX_CTMT, cs->hw.hfcsx.ctmt | HFCSX_CLTIMER);
	}
	while (val) {
		if (test_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
			cs->hw.hfcsx.int_s1 |= val;
			spin_unlock_irqrestore(&cs->lock, flags);
			return IRQ_HANDLED;
		}
		if (cs->hw.hfcsx.int_s1 & 0x18) {
			exval = val;
			val = cs->hw.hfcsx.int_s1;
			cs->hw.hfcsx.int_s1 = exval;
		}
		if (val & 0x08) {
			if (!(bcs = Sel_BCS(cs, cs->hw.hfcsx.bswapped ? 1 : 0))) {
				if (cs->debug)
					debugl1(cs, "hfcsx spurious 0x08 IRQ");
			} else
				main_rec_hfcsx(bcs);
		}
		if (val & 0x10) {
			if (cs->logecho)
				receive_emsg(cs);
			else if (!(bcs = Sel_BCS(cs, 1))) {
				if (cs->debug)
					debugl1(cs, "hfcsx spurious 0x10 IRQ");
			} else
				main_rec_hfcsx(bcs);
		}
		if (val & 0x01) {
			if (!(bcs = Sel_BCS(cs, cs->hw.hfcsx.bswapped ? 1 : 0))) {
				if (cs->debug)
					debugl1(cs, "hfcsx spurious 0x01 IRQ");
			} else {
				if (bcs->tx_skb) {
					if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
						hfcsx_fill_fifo(bcs);
						test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
					} else
						debugl1(cs, "fill_data %d blocked", bcs->channel);
				} else {
					if ((bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
						if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
							hfcsx_fill_fifo(bcs);
							test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
						} else
							debugl1(cs, "fill_data %d blocked", bcs->channel);
					} else {
						schedule_event(bcs, B_XMTBUFREADY);
					}
				}
			}
		}
		if (val & 0x02) {
			if (!(bcs = Sel_BCS(cs, 1))) {
				if (cs->debug)
					debugl1(cs, "hfcsx spurious 0x02 IRQ");
			} else {
				if (bcs->tx_skb) {
					if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
						hfcsx_fill_fifo(bcs);
						test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
					} else
						debugl1(cs, "fill_data %d blocked", bcs->channel);
				} else {
					if ((bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
						if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
							hfcsx_fill_fifo(bcs);
							test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
						} else
							debugl1(cs, "fill_data %d blocked", bcs->channel);
					} else {
						schedule_event(bcs, B_XMTBUFREADY);
					}
				}
			}
		}
		if (val & 0x20) {	/* receive dframe */
			receive_dmsg(cs);
		}
		if (val & 0x04) {	/* dframe transmitted */
			if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
				del_timer(&cs->dbusytimer);
			if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
				schedule_event(cs, D_CLEARBUSY);
			if (cs->tx_skb) {
				if (cs->tx_skb->len) {
					if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
						hfcsx_fill_dfifo(cs);
						test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
					} else {
						debugl1(cs, "hfcsx_fill_dfifo irq blocked");
					}
					goto afterXPR;
				} else {
					dev_kfree_skb_irq(cs->tx_skb);
					cs->tx_cnt = 0;
					cs->tx_skb = NULL;
				}
			}
			if ((cs->tx_skb = skb_dequeue(&cs->sq))) {
				cs->tx_cnt = 0;
				if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
					hfcsx_fill_dfifo(cs);
					test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
				} else {
					debugl1(cs, "hfcsx_fill_dfifo irq blocked");
				}
			} else
				schedule_event(cs, D_XMTBUFREADY);
		}
	      afterXPR:
		if (cs->hw.hfcsx.int_s1 && count--) {
			val = cs->hw.hfcsx.int_s1;
			cs->hw.hfcsx.int_s1 = 0;
			if (cs->debug & L1_DEB_ISAC)
				debugl1(cs, "HFC-SX irq %x loop %d", val, 15 - count);
		} else
			val = 0;
	}
	spin_unlock_irqrestore(&cs->lock, flags);
	return IRQ_HANDLED;
}

/********************************************************************/
/* timer callback for D-chan busy resolution. Currently no function */
/********************************************************************/
static void
hfcsx_dbusy_timer(struct IsdnCardState *cs)
{
}

/*************************************/
/* Layer 1 D-channel hardware access */
/*************************************/
static void
HFCSX_l1hw(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb = arg;
	u_long flags;

	switch (pr) {
		case (PH_DATA | REQUEST):
			if (cs->debug & DEB_DLOG_HEX)
				LogFrame(cs, skb->data, skb->len);
			if (cs->debug & DEB_DLOG_VERBOSE)
				dlogframe(cs, skb, 0);
			spin_lock_irqsave(&cs->lock, flags);
			if (cs->tx_skb) {
				skb_queue_tail(&cs->sq, skb);
#ifdef L2FRAME_DEBUG		/* psa */
				if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "PH_DATA Queued", 0);
#endif
			} else {
				cs->tx_skb = skb;
				cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
				if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "PH_DATA", 0);
#endif
				if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
				        hfcsx_fill_dfifo(cs); 
					test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
				} else
					debugl1(cs, "hfcsx_fill_dfifo blocked");

			}
			spin_unlock_irqrestore(&cs->lock, flags);
			break;
		case (PH_PULL | INDICATION):
			spin_lock_irqsave(&cs->lock, flags);
			if (cs->tx_skb) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, " l2l1 tx_skb exist this shouldn't happen");
				skb_queue_tail(&cs->sq, skb);
				spin_unlock_irqrestore(&cs->lock, flags);
				break;
			}
			if (cs->debug & DEB_DLOG_HEX)
				LogFrame(cs, skb->data, skb->len);
			if (cs->debug & DEB_DLOG_VERBOSE)
				dlogframe(cs, skb, 0);
			cs->tx_skb = skb;
			cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				Logl2Frame(cs, skb, "PH_DATA_PULLED", 0);
#endif
			if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
				hfcsx_fill_dfifo(cs); 
				test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
			} else
				debugl1(cs, "hfcsx_fill_dfifo blocked");
			spin_unlock_irqrestore(&cs->lock, flags);
			break;
		case (PH_PULL | REQUEST):
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				debugl1(cs, "-> PH_REQUEST_PULL");
#endif
			if (!cs->tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
		case (HW_RESET | REQUEST):
			spin_lock_irqsave(&cs->lock, flags);
			Write_hfc(cs, HFCSX_STATES, HFCSX_LOAD_STATE | 3);	/* HFC ST 3 */
			udelay(6);
			Write_hfc(cs, HFCSX_STATES, 3);	/* HFC ST 2 */
			cs->hw.hfcsx.mst_m |= HFCSX_MASTER;
			Write_hfc(cs, HFCSX_MST_MODE, cs->hw.hfcsx.mst_m);
			Write_hfc(cs, HFCSX_STATES, HFCSX_ACTIVATE | HFCSX_DO_ACTION);
			spin_unlock_irqrestore(&cs->lock, flags);
			l1_msg(cs, HW_POWERUP | CONFIRM, NULL);
			break;
		case (HW_ENABLE | REQUEST):
			spin_lock_irqsave(&cs->lock, flags);
			Write_hfc(cs, HFCSX_STATES, HFCSX_ACTIVATE | HFCSX_DO_ACTION);
			spin_unlock_irqrestore(&cs->lock, flags);
			break;
		case (HW_DEACTIVATE | REQUEST):
			spin_lock_irqsave(&cs->lock, flags);
			cs->hw.hfcsx.mst_m &= ~HFCSX_MASTER;
			Write_hfc(cs, HFCSX_MST_MODE, cs->hw.hfcsx.mst_m);
			spin_unlock_irqrestore(&cs->lock, flags);
			break;
		case (HW_INFO3 | REQUEST):
			spin_lock_irqsave(&cs->lock, flags);
			cs->hw.hfcsx.mst_m |= HFCSX_MASTER;
			Write_hfc(cs, HFCSX_MST_MODE, cs->hw.hfcsx.mst_m);
			spin_unlock_irqrestore(&cs->lock, flags);
			break;
		case (HW_TESTLOOP | REQUEST):
			spin_lock_irqsave(&cs->lock, flags);
			switch ((long) arg) {
				case (1):
					Write_hfc(cs, HFCSX_B1_SSL, 0x80);	/* tx slot */
					Write_hfc(cs, HFCSX_B1_RSL, 0x80);	/* rx slot */
					cs->hw.hfcsx.conn = (cs->hw.hfcsx.conn & ~7) | 1;
					Write_hfc(cs, HFCSX_CONNECT, cs->hw.hfcsx.conn);
					break;
				case (2):
					Write_hfc(cs, HFCSX_B2_SSL, 0x81);	/* tx slot */
					Write_hfc(cs, HFCSX_B2_RSL, 0x81);	/* rx slot */
					cs->hw.hfcsx.conn = (cs->hw.hfcsx.conn & ~0x38) | 0x08;
					Write_hfc(cs, HFCSX_CONNECT, cs->hw.hfcsx.conn);
					break;
				default:
					spin_unlock_irqrestore(&cs->lock, flags);
					if (cs->debug & L1_DEB_WARN)
						debugl1(cs, "hfcsx_l1hw loop invalid %4lx", arg);
					return;
			}
			cs->hw.hfcsx.trm |= 0x80;	/* enable IOM-loop */
			Write_hfc(cs, HFCSX_TRM, cs->hw.hfcsx.trm);
			spin_unlock_irqrestore(&cs->lock, flags);
			break;
		default:
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "hfcsx_l1hw unknown pr %4x", pr);
			break;
	}
}

/***********************************************/
/* called during init setting l1 stack pointer */
/***********************************************/
static void
setstack_hfcsx(struct PStack *st, struct IsdnCardState *cs)
{
	st->l1.l1hw = HFCSX_l1hw;
}

/**************************************/
/* send B-channel data if not blocked */
/**************************************/
static void
hfcsx_send_data(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;

	if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
	  hfcsx_fill_fifo(bcs);
		test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	} else
		debugl1(cs, "send_data %d blocked", bcs->channel);
}

/***************************************************************/
/* activate/deactivate hardware for selected channels and mode */
/***************************************************************/
static void
mode_hfcsx(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;
	int fifo2;

	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "HFCSX bchannel mode %d bchan %d/%d",
			mode, bc, bcs->channel);
	bcs->mode = mode;
	bcs->channel = bc;
	fifo2 = bc;
	if (cs->chanlimit > 1) {
		cs->hw.hfcsx.bswapped = 0;	/* B1 and B2 normal mode */
		cs->hw.hfcsx.sctrl_e &= ~0x80;
	} else {
		if (bc) {
			if (mode != L1_MODE_NULL) {
				cs->hw.hfcsx.bswapped = 1;	/* B1 and B2 exchanged */
				cs->hw.hfcsx.sctrl_e |= 0x80;
			} else {
				cs->hw.hfcsx.bswapped = 0;	/* B1 and B2 normal mode */
				cs->hw.hfcsx.sctrl_e &= ~0x80;
			}
			fifo2 = 0;
		} else {
			cs->hw.hfcsx.bswapped = 0;	/* B1 and B2 normal mode */
			cs->hw.hfcsx.sctrl_e &= ~0x80;
		}
	}
	switch (mode) {
		case (L1_MODE_NULL):
			if (bc) {
				cs->hw.hfcsx.sctrl &= ~SCTRL_B2_ENA;
				cs->hw.hfcsx.sctrl_r &= ~SCTRL_B2_ENA;
			} else {
				cs->hw.hfcsx.sctrl &= ~SCTRL_B1_ENA;
				cs->hw.hfcsx.sctrl_r &= ~SCTRL_B1_ENA;
			}
			if (fifo2) {
				cs->hw.hfcsx.int_m1 &= ~(HFCSX_INTS_B2TRANS + HFCSX_INTS_B2REC);
			} else {
				cs->hw.hfcsx.int_m1 &= ~(HFCSX_INTS_B1TRANS + HFCSX_INTS_B1REC);
			}
			break;
		case (L1_MODE_TRANS):
			if (bc) {
				cs->hw.hfcsx.sctrl |= SCTRL_B2_ENA;
				cs->hw.hfcsx.sctrl_r |= SCTRL_B2_ENA;
			} else {
				cs->hw.hfcsx.sctrl |= SCTRL_B1_ENA;
				cs->hw.hfcsx.sctrl_r |= SCTRL_B1_ENA;
			}
			if (fifo2) {
				cs->hw.hfcsx.int_m1 |= (HFCSX_INTS_B2TRANS + HFCSX_INTS_B2REC);
				cs->hw.hfcsx.ctmt |= 2;
				cs->hw.hfcsx.conn &= ~0x18;
			} else {
				cs->hw.hfcsx.int_m1 |= (HFCSX_INTS_B1TRANS + HFCSX_INTS_B1REC);
				cs->hw.hfcsx.ctmt |= 1;
				cs->hw.hfcsx.conn &= ~0x03;
			}
			break;
		case (L1_MODE_HDLC):
			if (bc) {
				cs->hw.hfcsx.sctrl |= SCTRL_B2_ENA;
				cs->hw.hfcsx.sctrl_r |= SCTRL_B2_ENA;
			} else {
				cs->hw.hfcsx.sctrl |= SCTRL_B1_ENA;
				cs->hw.hfcsx.sctrl_r |= SCTRL_B1_ENA;
			}
			if (fifo2) {
				cs->hw.hfcsx.int_m1 |= (HFCSX_INTS_B2TRANS + HFCSX_INTS_B2REC);
				cs->hw.hfcsx.ctmt &= ~2;
				cs->hw.hfcsx.conn &= ~0x18;
			} else {
				cs->hw.hfcsx.int_m1 |= (HFCSX_INTS_B1TRANS + HFCSX_INTS_B1REC);
				cs->hw.hfcsx.ctmt &= ~1;
				cs->hw.hfcsx.conn &= ~0x03;
			}
			break;
		case (L1_MODE_EXTRN):
			if (bc) {
				cs->hw.hfcsx.conn |= 0x10;
				cs->hw.hfcsx.sctrl |= SCTRL_B2_ENA;
				cs->hw.hfcsx.sctrl_r |= SCTRL_B2_ENA;
				cs->hw.hfcsx.int_m1 &= ~(HFCSX_INTS_B2TRANS + HFCSX_INTS_B2REC);
			} else {
				cs->hw.hfcsx.conn |= 0x02;
				cs->hw.hfcsx.sctrl |= SCTRL_B1_ENA;
				cs->hw.hfcsx.sctrl_r |= SCTRL_B1_ENA;
				cs->hw.hfcsx.int_m1 &= ~(HFCSX_INTS_B1TRANS + HFCSX_INTS_B1REC);
			}
			break;
	}
	Write_hfc(cs, HFCSX_SCTRL_E, cs->hw.hfcsx.sctrl_e);
	Write_hfc(cs, HFCSX_INT_M1, cs->hw.hfcsx.int_m1);
	Write_hfc(cs, HFCSX_SCTRL, cs->hw.hfcsx.sctrl);
	Write_hfc(cs, HFCSX_SCTRL_R, cs->hw.hfcsx.sctrl_r);
	Write_hfc(cs, HFCSX_CTMT, cs->hw.hfcsx.ctmt);
	Write_hfc(cs, HFCSX_CONNECT, cs->hw.hfcsx.conn);
	if (mode != L1_MODE_EXTRN) {
	  reset_fifo(cs, fifo2 ? HFCSX_SEL_B2_RX : HFCSX_SEL_B1_RX);
	  reset_fifo(cs, fifo2 ? HFCSX_SEL_B2_TX : HFCSX_SEL_B1_TX);
	}
}

/******************************/
/* Layer2 -> Layer 1 Transfer */
/******************************/
static void
hfcsx_l2l1(struct PStack *st, int pr, void *arg)
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
//                              test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
				bcs->cs->BC_Send_Data(bcs);
			}
			spin_unlock_irqrestore(&bcs->cs->lock, flags);
			break;
		case (PH_PULL | INDICATION):
			spin_lock_irqsave(&bcs->cs->lock, flags);
			if (bcs->tx_skb) {
				printk(KERN_WARNING "hfc_l2l1: this shouldn't happen\n");
			} else {
//				test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
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
			mode_hfcsx(bcs, st->l1.mode, st->l1.bc);
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
			mode_hfcsx(bcs, 0, st->l1.bc);
			spin_unlock_irqrestore(&bcs->cs->lock, flags);
			st->l1.l1l2(st, PH_DEACTIVATE | CONFIRM, NULL);
			break;
	}
}

/******************************************/
/* deactivate B-channel access and queues */
/******************************************/
static void
close_hfcsx(struct BCState *bcs)
{
	mode_hfcsx(bcs, 0, bcs->channel);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		skb_queue_purge(&bcs->rqueue);
		skb_queue_purge(&bcs->squeue);
		if (bcs->tx_skb) {
			dev_kfree_skb_any(bcs->tx_skb);
			bcs->tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
}

/*************************************/
/* init B-channel queues and control */
/*************************************/
static int
open_hfcsxstate(struct IsdnCardState *cs, struct BCState *bcs)
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

/*********************************/
/* inits the stack for B-channel */
/*********************************/
static int
setstack_2b(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	if (open_hfcsxstate(st->l1.hardware, bcs))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = hfcsx_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

/***************************/
/* handle L1 state changes */
/***************************/
static void
hfcsx_bh(struct work_struct *work)
{
	struct IsdnCardState *cs =
		container_of(work, struct IsdnCardState, tqueue);
	u_long flags;

	if (!cs)
		return;
	if (test_and_clear_bit(D_L1STATECHANGE, &cs->event)) {
		if (!cs->hw.hfcsx.nt_mode)
			switch (cs->dc.hfcsx.ph_state) {
				case (0):
					l1_msg(cs, HW_RESET | INDICATION, NULL);
					break;
				case (3):
					l1_msg(cs, HW_DEACTIVATE | INDICATION, NULL);
					break;
				case (8):
					l1_msg(cs, HW_RSYNC | INDICATION, NULL);
					break;
				case (6):
					l1_msg(cs, HW_INFO2 | INDICATION, NULL);
					break;
				case (7):
					l1_msg(cs, HW_INFO4_P8 | INDICATION, NULL);
					break;
				default:
					break;
		} else {
			switch (cs->dc.hfcsx.ph_state) {
				case (2):
					spin_lock_irqsave(&cs->lock, flags);
					if (cs->hw.hfcsx.nt_timer < 0) {
						cs->hw.hfcsx.nt_timer = 0;
						cs->hw.hfcsx.int_m1 &= ~HFCSX_INTS_TIMER;
						Write_hfc(cs, HFCSX_INT_M1, cs->hw.hfcsx.int_m1);
						/* Clear already pending ints */
						if (Read_hfc(cs, HFCSX_INT_S1));

						Write_hfc(cs, HFCSX_STATES, 4 | HFCSX_LOAD_STATE);
						udelay(10);
						Write_hfc(cs, HFCSX_STATES, 4);
						cs->dc.hfcsx.ph_state = 4;
					} else {
						cs->hw.hfcsx.int_m1 |= HFCSX_INTS_TIMER;
						Write_hfc(cs, HFCSX_INT_M1, cs->hw.hfcsx.int_m1);
						cs->hw.hfcsx.ctmt &= ~HFCSX_AUTO_TIMER;
						cs->hw.hfcsx.ctmt |= HFCSX_TIM3_125;
						Write_hfc(cs, HFCSX_CTMT, cs->hw.hfcsx.ctmt | HFCSX_CLTIMER);
						Write_hfc(cs, HFCSX_CTMT, cs->hw.hfcsx.ctmt | HFCSX_CLTIMER);
						cs->hw.hfcsx.nt_timer = NT_T1_COUNT;
						Write_hfc(cs, HFCSX_STATES, 2 | HFCSX_NT_G2_G3);	/* allow G2 -> G3 transition */
					}
					spin_unlock_irqrestore(&cs->lock, flags);
					break;
				case (1):
				case (3):
				case (4):
					spin_lock_irqsave(&cs->lock, flags);
					cs->hw.hfcsx.nt_timer = 0;
					cs->hw.hfcsx.int_m1 &= ~HFCSX_INTS_TIMER;
					Write_hfc(cs, HFCSX_INT_M1, cs->hw.hfcsx.int_m1);
					spin_unlock_irqrestore(&cs->lock, flags);
					break;
				default:
					break;
			}
		}
	}
	if (test_and_clear_bit(D_RCVBUFREADY, &cs->event))
		DChannel_proc_rcv(cs);
	if (test_and_clear_bit(D_XMTBUFREADY, &cs->event))
		DChannel_proc_xmt(cs);
}


/********************************/
/* called for card init message */
/********************************/
static void __devinit
inithfcsx(struct IsdnCardState *cs)
{
	cs->setstack_d = setstack_hfcsx;
	cs->BC_Send_Data = &hfcsx_send_data;
	cs->bcs[0].BC_SetStack = setstack_2b;
	cs->bcs[1].BC_SetStack = setstack_2b;
	cs->bcs[0].BC_Close = close_hfcsx;
	cs->bcs[1].BC_Close = close_hfcsx;
	mode_hfcsx(cs->bcs, 0, 0);
	mode_hfcsx(cs->bcs + 1, 0, 1);
}



/*******************************************/
/* handle card messages from control layer */
/*******************************************/
static int
hfcsx_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	u_long flags;

	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "HFCSX: card_msg %x", mt);
	switch (mt) {
		case CARD_RESET:
			spin_lock_irqsave(&cs->lock, flags);
			reset_hfcsx(cs);
			spin_unlock_irqrestore(&cs->lock, flags);
			return (0);
		case CARD_RELEASE:
			release_io_hfcsx(cs);
			return (0);
		case CARD_INIT:
			spin_lock_irqsave(&cs->lock, flags);
			inithfcsx(cs);
			spin_unlock_irqrestore(&cs->lock, flags);
			msleep(80);				/* Timeout 80ms */
			/* now switch timer interrupt off */
			spin_lock_irqsave(&cs->lock, flags);
			cs->hw.hfcsx.int_m1 &= ~HFCSX_INTS_TIMER;
			Write_hfc(cs, HFCSX_INT_M1, cs->hw.hfcsx.int_m1);
			/* reinit mode reg */
			Write_hfc(cs, HFCSX_MST_MODE, cs->hw.hfcsx.mst_m);
			spin_unlock_irqrestore(&cs->lock, flags);
			return (0);
		case CARD_TEST:
			return (0);
	}
	return (0);
}

#ifdef __ISAPNP__
static struct isapnp_device_id hfc_ids[] __devinitdata = {
	{ ISAPNP_VENDOR('T', 'A', 'G'), ISAPNP_FUNCTION(0x2620),
	  ISAPNP_VENDOR('T', 'A', 'G'), ISAPNP_FUNCTION(0x2620), 
	  (unsigned long) "Teles 16.3c2" },
	{ 0, }
};

static struct isapnp_device_id *ipid __devinitdata = &hfc_ids[0];
static struct pnp_card *pnp_c __devinitdata = NULL;
#endif

int __devinit
setup_hfcsx(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, hfcsx_revision);
	printk(KERN_INFO "HiSax: HFC-SX driver Rev. %s\n", HiSax_getrev(tmp));
#ifdef __ISAPNP__
	if (!card->para[1] && isapnp_present()) {
		struct pnp_dev *pnp_d;
		while(ipid->card_vendor) {
			if ((pnp_c = pnp_find_card(ipid->card_vendor,
				ipid->card_device, pnp_c))) {
				pnp_d = NULL;
				if ((pnp_d = pnp_find_dev(pnp_c,
					ipid->vendor, ipid->function, pnp_d))) {
					int err;

					printk(KERN_INFO "HiSax: %s detected\n",
						(char *)ipid->driver_data);
					pnp_disable_dev(pnp_d);
					err = pnp_activate_dev(pnp_d);
					if (err<0) {
						printk(KERN_WARNING "%s: pnp_activate_dev ret(%d)\n",
							__FUNCTION__, err);
						return(0);
					}
					card->para[1] = pnp_port_start(pnp_d, 0);
					card->para[0] = pnp_irq(pnp_d, 0);
					if (!card->para[0] || !card->para[1]) {
						printk(KERN_ERR "HFC PnP:some resources are missing %ld/%lx\n",
							card->para[0], card->para[1]);
						pnp_disable_dev(pnp_d);
						return(0);
					}
					break;
				} else {
					printk(KERN_ERR "HFC PnP: PnP error card found, no device\n");
				}
			}
			ipid++;
			pnp_c = NULL;
		} 
		if (!ipid->card_vendor) {
			printk(KERN_INFO "HFC PnP: no ISAPnP card found\n");
			return(0);
		}
	}
#endif
	cs->hw.hfcsx.base = card->para[1] & 0xfffe;
	cs->irq = card->para[0];
	cs->hw.hfcsx.int_s1 = 0;
	cs->dc.hfcsx.ph_state = 0;
	cs->hw.hfcsx.fifo = 255;
	if ((cs->typ == ISDN_CTYPE_HFC_SX) || 
	    (cs->typ == ISDN_CTYPE_HFC_SP_PCMCIA)) {
	        if ((!cs->hw.hfcsx.base) || !request_region(cs->hw.hfcsx.base, 2, "HFCSX isdn")) {
		  printk(KERN_WARNING
			 "HiSax: HFC-SX io-base %#lx already in use\n",
		          cs->hw.hfcsx.base);
		  return(0);
		}
		byteout(cs->hw.hfcsx.base, cs->hw.hfcsx.base & 0xFF);
		byteout(cs->hw.hfcsx.base + 1,
			((cs->hw.hfcsx.base >> 8) & 3) | 0x54);
		udelay(10);
	        cs->hw.hfcsx.chip = Read_hfc(cs,HFCSX_CHIP_ID);
                switch (cs->hw.hfcsx.chip >> 4) {
		  case 1: 
		    tmp[0] ='+';
		    break;
		  case 9: 
		    tmp[0] ='P';
		    break;
		  default:
		    printk(KERN_WARNING
			   "HFC-SX: invalid chip id 0x%x\n",
			   cs->hw.hfcsx.chip >> 4);
		    release_region(cs->hw.hfcsx.base, 2);
		    return(0);
		}  
		if (!ccd_sp_irqtab[cs->irq & 0xF]) {
		  printk(KERN_WARNING 
			 "HFC_SX: invalid irq %d specified\n",cs->irq & 0xF);
		  release_region(cs->hw.hfcsx.base, 2);
		  return(0);
		}  
		if (!(cs->hw.hfcsx.extra = (void *)
		      kmalloc(sizeof(struct hfcsx_extra), GFP_ATOMIC))) {
		  release_region(cs->hw.hfcsx.base, 2);
		  printk(KERN_WARNING "HFC-SX: unable to allocate memory\n");
		  return(0);
		}
		printk(KERN_INFO "HFC-S%c chip detected at base 0x%x IRQ %d HZ %d\n",
			tmp[0], (u_int) cs->hw.hfcsx.base, cs->irq, HZ);
		cs->hw.hfcsx.int_m2 = 0;	/* disable alle interrupts */
		cs->hw.hfcsx.int_m1 = 0;
		Write_hfc(cs, HFCSX_INT_M1, cs->hw.hfcsx.int_m1);
		Write_hfc(cs, HFCSX_INT_M2, cs->hw.hfcsx.int_m2);
	} else
		return (0);	/* no valid card type */

	cs->dbusytimer.function = (void *) hfcsx_dbusy_timer;
	cs->dbusytimer.data = (long) cs;
	init_timer(&cs->dbusytimer);
	INIT_WORK(&cs->tqueue, hfcsx_bh);
	cs->readisac = NULL;
	cs->writeisac = NULL;
	cs->readisacfifo = NULL;
	cs->writeisacfifo = NULL;
	cs->BC_Read_Reg = NULL;
	cs->BC_Write_Reg = NULL;
	cs->irq_func = &hfcsx_interrupt;

	cs->hw.hfcsx.timer.function = (void *) hfcsx_Timer;
	cs->hw.hfcsx.timer.data = (long) cs;
	cs->hw.hfcsx.b_fifo_size = 0; /* fifo size still unknown */
	cs->hw.hfcsx.cirm = ccd_sp_irqtab[cs->irq & 0xF]; /* RAM not evaluated */
	init_timer(&cs->hw.hfcsx.timer);

	reset_hfcsx(cs);
	cs->cardmsg = &hfcsx_card_msg;
	cs->auxcmd = &hfcsx_auxcmd;
	return (1);
}
