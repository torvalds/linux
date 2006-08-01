/* $Id: jade.c,v 1.9.2.4 2004/01/14 16:04:48 keil Exp $
 *
 * JADE stuff (derived from original hscx.c)
 *
 * Author       Roland Klabunde
 * Copyright    by Roland Klabunde   <R.Klabunde@Berkom.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */


#include <linux/init.h>
#include "hisax.h"
#include "hscx.h"
#include "jade.h"
#include "isdnl1.h"
#include <linux/interrupt.h>


int
JadeVersion(struct IsdnCardState *cs, char *s)
{
    int ver,i;
    int to = 50;
    cs->BC_Write_Reg(cs, -1, 0x50, 0x19);
    i=0;
    while (to) {
    	udelay(1);
	ver = cs->BC_Read_Reg(cs, -1, 0x60);
	to--;
	if (ver)
    	    break;
	if (!to) {
	    printk(KERN_INFO "%s JADE version not obtainable\n", s);
    	    return (0);
        }
    }
    /* Wait for the JADE */
    udelay(10);
    /* Read version */
    ver = cs->BC_Read_Reg(cs, -1, 0x60);
    printk(KERN_INFO "%s JADE version: %d\n", s, ver);
    return (1);
}

/* Write to indirect accessible jade register set */
static void
jade_write_indirect(struct IsdnCardState *cs, u_char reg, u_char value)
{
    int to = 50;
    u_char ret;

    /* Write the data */
    cs->BC_Write_Reg(cs, -1, COMM_JADE+1, value);
    /* Say JADE we wanna write indirect reg 'reg' */
    cs->BC_Write_Reg(cs, -1, COMM_JADE, reg);
    to = 50;
    /* Wait for RDY goes high */
    while (to) {
    	udelay(1);
	ret = cs->BC_Read_Reg(cs, -1, COMM_JADE);
	to--;
	if (ret & 1)
	    /* Got acknowledge */
	    break;
	if (!to) {
    	    printk(KERN_INFO "Can not see ready bit from JADE DSP (reg=0x%X, value=0x%X)\n", reg, value);
	    return;
	}
    }
}



static void
modejade(struct BCState *bcs, int mode, int bc)
{
    struct IsdnCardState *cs = bcs->cs;
    int jade = bcs->hw.hscx.hscx;

    if (cs->debug & L1_DEB_HSCX) {
	char tmp[40];
	sprintf(tmp, "jade %c mode %d ichan %d",
		'A' + jade, mode, bc);
	debugl1(cs, tmp);
    }
    bcs->mode = mode;
    bcs->channel = bc;
	
    cs->BC_Write_Reg(cs, jade, jade_HDLC_MODE, (mode == L1_MODE_TRANS ? jadeMODE_TMO:0x00));
    cs->BC_Write_Reg(cs, jade, jade_HDLC_CCR0, (jadeCCR0_PU|jadeCCR0_ITF));
    cs->BC_Write_Reg(cs, jade, jade_HDLC_CCR1, 0x00);

    jade_write_indirect(cs, jade_HDLC1SERRXPATH, 0x08);
    jade_write_indirect(cs, jade_HDLC2SERRXPATH, 0x08);
    jade_write_indirect(cs, jade_HDLC1SERTXPATH, 0x00);
    jade_write_indirect(cs, jade_HDLC2SERTXPATH, 0x00);

    cs->BC_Write_Reg(cs, jade, jade_HDLC_XCCR, 0x07);
    cs->BC_Write_Reg(cs, jade, jade_HDLC_RCCR, 0x07);

    if (bc == 0) {
	cs->BC_Write_Reg(cs, jade, jade_HDLC_TSAX, 0x00);
	cs->BC_Write_Reg(cs, jade, jade_HDLC_TSAR, 0x00);
    } else {
	cs->BC_Write_Reg(cs, jade, jade_HDLC_TSAX, 0x04);
	cs->BC_Write_Reg(cs, jade, jade_HDLC_TSAR, 0x04);
    }
    switch (mode) {
	case (L1_MODE_NULL):
		cs->BC_Write_Reg(cs, jade, jade_HDLC_MODE, jadeMODE_TMO);
		break;
	case (L1_MODE_TRANS):
		cs->BC_Write_Reg(cs, jade, jade_HDLC_MODE, (jadeMODE_TMO|jadeMODE_RAC|jadeMODE_XAC));
		break;
	case (L1_MODE_HDLC):
		cs->BC_Write_Reg(cs, jade, jade_HDLC_MODE, (jadeMODE_RAC|jadeMODE_XAC));
		break;
    }
    if (mode) {
	cs->BC_Write_Reg(cs, jade, jade_HDLC_RCMD, (jadeRCMD_RRES|jadeRCMD_RMC));
	cs->BC_Write_Reg(cs, jade, jade_HDLC_XCMD, jadeXCMD_XRES);
	/* Unmask ints */
	cs->BC_Write_Reg(cs, jade, jade_HDLC_IMR, 0xF8);
    }
    else
	/* Mask ints */
	cs->BC_Write_Reg(cs, jade, jade_HDLC_IMR, 0x00);
}

static void
jade_l2l1(struct PStack *st, int pr, void *arg)
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
			bcs->hw.hscx.count = 0;
			bcs->cs->BC_Send_Data(bcs);
		}
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
		break;
	case (PH_PULL | INDICATION):
		spin_lock_irqsave(&bcs->cs->lock, flags);
		if (bcs->tx_skb) {
			printk(KERN_WARNING "jade_l2l1: this shouldn't happen\n");
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
		modejade(bcs, st->l1.mode, st->l1.bc);
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
		modejade(bcs, 0, st->l1.bc);
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
		st->l1.l1l2(st, PH_DEACTIVATE | CONFIRM, NULL);
		break;
    }
}

static void
close_jadestate(struct BCState *bcs)
{
    modejade(bcs, 0, bcs->channel);
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
		test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	}
    }
}

static int
open_jadestate(struct IsdnCardState *cs, struct BCState *bcs)
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


static int
setstack_jade(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	if (open_jadestate(st->l1.hardware, bcs))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = jade_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

void
clear_pending_jade_ints(struct IsdnCardState *cs)
{
	int val;
	char tmp[64];

	cs->BC_Write_Reg(cs, 0, jade_HDLC_IMR, 0x00);
	cs->BC_Write_Reg(cs, 1, jade_HDLC_IMR, 0x00);

	val = cs->BC_Read_Reg(cs, 1, jade_HDLC_ISR);
	sprintf(tmp, "jade B ISTA %x", val);
	debugl1(cs, tmp);
	val = cs->BC_Read_Reg(cs, 0, jade_HDLC_ISR);
	sprintf(tmp, "jade A ISTA %x", val);
	debugl1(cs, tmp);
	val = cs->BC_Read_Reg(cs, 1, jade_HDLC_STAR);
	sprintf(tmp, "jade B STAR %x", val);
	debugl1(cs, tmp);
	val = cs->BC_Read_Reg(cs, 0, jade_HDLC_STAR);
	sprintf(tmp, "jade A STAR %x", val);
	debugl1(cs, tmp);
	/* Unmask ints */
	cs->BC_Write_Reg(cs, 0, jade_HDLC_IMR, 0xF8);
	cs->BC_Write_Reg(cs, 1, jade_HDLC_IMR, 0xF8);
}

void
initjade(struct IsdnCardState *cs)
{
	cs->bcs[0].BC_SetStack = setstack_jade;
	cs->bcs[1].BC_SetStack = setstack_jade;
	cs->bcs[0].BC_Close = close_jadestate;
	cs->bcs[1].BC_Close = close_jadestate;
	cs->bcs[0].hw.hscx.hscx = 0;
	cs->bcs[1].hw.hscx.hscx = 1;

	/* Stop DSP audio tx/rx */
	jade_write_indirect(cs, 0x11, 0x0f);
	jade_write_indirect(cs, 0x17, 0x2f);

	/* Transparent Mode, RxTx inactive, No Test, No RFS/TFS */
	cs->BC_Write_Reg(cs, 0, jade_HDLC_MODE, jadeMODE_TMO);
	cs->BC_Write_Reg(cs, 1, jade_HDLC_MODE, jadeMODE_TMO);
	/* Power down, 1-Idle, RxTx least significant bit first */
	cs->BC_Write_Reg(cs, 0, jade_HDLC_CCR0, 0x00);
	cs->BC_Write_Reg(cs, 1, jade_HDLC_CCR0, 0x00);
	/* Mask all interrupts */
	cs->BC_Write_Reg(cs, 0, jade_HDLC_IMR,  0x00);
	cs->BC_Write_Reg(cs, 1, jade_HDLC_IMR,  0x00);
	/* Setup host access to hdlc controller */
	jade_write_indirect(cs, jade_HDLCCNTRACCESS, (jadeINDIRECT_HAH1|jadeINDIRECT_HAH2));
	/* Unmask HDLC int (don´t forget DSP int later on)*/
	cs->BC_Write_Reg(cs, -1,jade_INT, (jadeINT_HDLC1|jadeINT_HDLC2));

	/* once again TRANSPARENT */	
	modejade(cs->bcs, 0, 0);
	modejade(cs->bcs + 1, 0, 0);
}

