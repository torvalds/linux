/* $Id: c4.c,v 1.1.2.2 2004/01/16 21:09:27 keil Exp $
 * 
 * Module for AVM C4 & C2 card.
 * 
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/capi.h>
#include <linux/kernelcapi.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/netdevice.h>
#include <linux/isdn/capicmd.h>
#include <linux/isdn/capiutil.h>
#include <linux/isdn/capilli.h>
#include "avmcard.h"

#undef AVM_C4_DEBUG
#undef AVM_C4_POLLDEBUG

/* ------------------------------------------------------------- */

static char *revision = "$Revision: 1.1.2.2 $";

/* ------------------------------------------------------------- */

static int suppress_pollack;

static struct pci_device_id c4_pci_tbl[] = {
	{ PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_21285, PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_C4, 0, 0, (unsigned long)4 },
	{ PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_21285, PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_C2, 0, 0, (unsigned long)2 },
	{ }			/* Terminating entry */
};

MODULE_DEVICE_TABLE(pci, c4_pci_tbl);
MODULE_DESCRIPTION("CAPI4Linux: Driver for AVM C2/C4 cards");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");
module_param(suppress_pollack, bool, 0);

/* ------------------------------------------------------------- */

static void c4_dispatch_tx(avmcard *card);

/* ------------------------------------------------------------- */

#define DC21285_DRAM_A0MR	0x40000000
#define DC21285_DRAM_A1MR	0x40004000
#define DC21285_DRAM_A2MR	0x40008000
#define DC21285_DRAM_A3MR	0x4000C000

#define	CAS_OFFSET	0x88

#define DC21285_ARMCSR_BASE	0x42000000

#define	PCI_OUT_INT_STATUS	0x30
#define	PCI_OUT_INT_MASK	0x34
#define	MAILBOX_0		0x50
#define	MAILBOX_1		0x54
#define	MAILBOX_2		0x58
#define	MAILBOX_3		0x5C
#define	DOORBELL		0x60
#define	DOORBELL_SETUP		0x64

#define CHAN_1_CONTROL		0x90
#define CHAN_2_CONTROL		0xB0
#define DRAM_TIMING		0x10C
#define DRAM_ADDR_SIZE_0	0x110
#define DRAM_ADDR_SIZE_1	0x114
#define DRAM_ADDR_SIZE_2	0x118
#define DRAM_ADDR_SIZE_3	0x11C
#define	SA_CONTROL		0x13C
#define	XBUS_CYCLE		0x148
#define	XBUS_STROBE		0x14C
#define	DBELL_PCI_MASK		0x150
#define DBELL_SA_MASK		0x154

#define SDRAM_SIZE		0x1000000

/* ------------------------------------------------------------- */

#define	MBOX_PEEK_POKE		MAILBOX_0

#define DBELL_ADDR		0x01
#define DBELL_DATA		0x02
#define DBELL_RNWR		0x40
#define DBELL_INIT		0x80

/* ------------------------------------------------------------- */

#define	MBOX_UP_ADDR		MAILBOX_0
#define	MBOX_UP_LEN		MAILBOX_1
#define	MBOX_DOWN_ADDR		MAILBOX_2
#define	MBOX_DOWN_LEN		MAILBOX_3

#define	DBELL_UP_HOST		0x00000100
#define	DBELL_UP_ARM		0x00000200
#define	DBELL_DOWN_HOST		0x00000400
#define	DBELL_DOWN_ARM		0x00000800
#define	DBELL_RESET_HOST	0x40000000
#define	DBELL_RESET_ARM		0x80000000

/* ------------------------------------------------------------- */

#define	DRAM_TIMING_DEF		0x001A01A5
#define DRAM_AD_SZ_DEF0		0x00000045
#define DRAM_AD_SZ_NULL		0x00000000

#define SA_CTL_ALLRIGHT		0x64AA0271

#define	INIT_XBUS_CYCLE		0x100016DB
#define	INIT_XBUS_STROBE	0xF1F1F1F1

/* ------------------------------------------------------------- */

#define	RESET_TIMEOUT		(15*HZ)	/* 15 sec */
#define	PEEK_POKE_TIMEOUT	(HZ/10)	/* 0.1 sec */

/* ------------------------------------------------------------- */

#define c4outmeml(addr, value)	writel(value, addr)
#define c4inmeml(addr)	readl(addr)
#define c4outmemw(addr, value)	writew(value, addr)
#define c4inmemw(addr)	readw(addr)
#define c4outmemb(addr, value)	writeb(value, addr)
#define c4inmemb(addr)	readb(addr)

/* ------------------------------------------------------------- */

static inline int wait_for_doorbell(avmcard *card, unsigned long t)
{
	unsigned long stop;

	stop = jiffies + t;
	while (c4inmeml(card->mbase+DOORBELL) != 0xffffffff) {
		if (!time_before(jiffies, stop))
			return -1;
		mb();
	}
	return 0;
}

static int c4_poke(avmcard *card,  unsigned long off, unsigned long value)
{

	if (wait_for_doorbell(card, HZ/10) < 0)
		return -1;
	
	c4outmeml(card->mbase+MBOX_PEEK_POKE, off);
	c4outmeml(card->mbase+DOORBELL, DBELL_ADDR);

	if (wait_for_doorbell(card, HZ/10) < 0)
		return -1;

	c4outmeml(card->mbase+MBOX_PEEK_POKE, value);
	c4outmeml(card->mbase+DOORBELL, DBELL_DATA | DBELL_ADDR);

	return 0;
}

static int c4_peek(avmcard *card,  unsigned long off, unsigned long *valuep)
{
	if (wait_for_doorbell(card, HZ/10) < 0)
		return -1;

	c4outmeml(card->mbase+MBOX_PEEK_POKE, off);
	c4outmeml(card->mbase+DOORBELL, DBELL_RNWR | DBELL_ADDR);

	if (wait_for_doorbell(card, HZ/10) < 0)
		return -1;

	*valuep = c4inmeml(card->mbase+MBOX_PEEK_POKE);

	return 0;
}

/* ------------------------------------------------------------- */

static int c4_load_t4file(avmcard *card, capiloaddatapart * t4file)
{
	u32 val;
	unsigned char *dp;
	u_int left;
	u32 loadoff = 0;

	dp = t4file->data;
	left = t4file->len;
	while (left >= sizeof(u32)) {
	        if (t4file->user) {
			if (copy_from_user(&val, dp, sizeof(val)))
				return -EFAULT;
		} else {
			memcpy(&val, dp, sizeof(val));
		}
		if (c4_poke(card, loadoff, val)) {
			printk(KERN_ERR "%s: corrupted firmware file ?\n",
					card->name);
			return -EIO;
		}
		left -= sizeof(u32);
		dp += sizeof(u32);
		loadoff += sizeof(u32);
	}
	if (left) {
		val = 0;
		if (t4file->user) {
			if (copy_from_user(&val, dp, left))
				return -EFAULT;
		} else {
			memcpy(&val, dp, left);
		}
		if (c4_poke(card, loadoff, val)) {
			printk(KERN_ERR "%s: corrupted firmware file ?\n",
					card->name);
			return -EIO;
		}
	}
	return 0;
}

/* ------------------------------------------------------------- */

static inline void _put_byte(void **pp, u8 val)
{
	u8 *s = *pp;
	*s++ = val;
	*pp = s;
}

static inline void _put_word(void **pp, u32 val)
{
	u8 *s = *pp;
	*s++ = val & 0xff;
	*s++ = (val >> 8) & 0xff;
	*s++ = (val >> 16) & 0xff;
	*s++ = (val >> 24) & 0xff;
	*pp = s;
}

static inline void _put_slice(void **pp, unsigned char *dp, unsigned int len)
{
	unsigned i = len;
	_put_word(pp, i);
	while (i-- > 0)
		_put_byte(pp, *dp++);
}

static inline u8 _get_byte(void **pp)
{
	u8 *s = *pp;
	u8 val;
	val = *s++;
	*pp = s;
	return val;
}

static inline u32 _get_word(void **pp)
{
	u8 *s = *pp;
	u32 val;
	val = *s++;
	val |= (*s++ << 8);
	val |= (*s++ << 16);
	val |= (*s++ << 24);
	*pp = s;
	return val;
}

static inline u32 _get_slice(void **pp, unsigned char *dp)
{
	unsigned int len, i;

	len = i = _get_word(pp);
	while (i-- > 0) *dp++ = _get_byte(pp);
	return len;
}

/* ------------------------------------------------------------- */

static void c4_reset(avmcard *card)
{
	unsigned long stop;

	c4outmeml(card->mbase+DOORBELL, DBELL_RESET_ARM);

	stop = jiffies + HZ*10;
	while (c4inmeml(card->mbase+DOORBELL) != 0xffffffff) {
		if (!time_before(jiffies, stop))
			return;
		c4outmeml(card->mbase+DOORBELL, DBELL_ADDR);
		mb();
	}

	c4_poke(card, DC21285_ARMCSR_BASE + CHAN_1_CONTROL, 0);
	c4_poke(card, DC21285_ARMCSR_BASE + CHAN_2_CONTROL, 0);
}

/* ------------------------------------------------------------- */

static int c4_detect(avmcard *card)
{
	unsigned long stop, dummy;

	c4outmeml(card->mbase+PCI_OUT_INT_MASK, 0x0c);
	if (c4inmeml(card->mbase+PCI_OUT_INT_MASK) != 0x0c)
		return	1;

	c4outmeml(card->mbase+DOORBELL, DBELL_RESET_ARM);

	stop = jiffies + HZ*10;
	while (c4inmeml(card->mbase+DOORBELL) != 0xffffffff) {
		if (!time_before(jiffies, stop))
			return 2;
		c4outmeml(card->mbase+DOORBELL, DBELL_ADDR);
		mb();
	}

	c4_poke(card, DC21285_ARMCSR_BASE + CHAN_1_CONTROL, 0);
	c4_poke(card, DC21285_ARMCSR_BASE + CHAN_2_CONTROL, 0);

	c4outmeml(card->mbase+MAILBOX_0, 0x55aa55aa);
	if (c4inmeml(card->mbase+MAILBOX_0) != 0x55aa55aa) return 3;

	c4outmeml(card->mbase+MAILBOX_0, 0xaa55aa55);
	if (c4inmeml(card->mbase+MAILBOX_0) != 0xaa55aa55) return 4;

	if (c4_poke(card, DC21285_ARMCSR_BASE+DBELL_SA_MASK, 0)) return 5;
	if (c4_poke(card, DC21285_ARMCSR_BASE+DBELL_PCI_MASK, 0)) return 6;
	if (c4_poke(card, DC21285_ARMCSR_BASE+SA_CONTROL, SA_CTL_ALLRIGHT))
		return 7;
	if (c4_poke(card, DC21285_ARMCSR_BASE+XBUS_CYCLE, INIT_XBUS_CYCLE))
		return 8;
	if (c4_poke(card, DC21285_ARMCSR_BASE+XBUS_STROBE, INIT_XBUS_STROBE))
		return 8;
	if (c4_poke(card, DC21285_ARMCSR_BASE+DRAM_TIMING, 0)) return 9;

        mdelay(1);

	if (c4_peek(card, DC21285_DRAM_A0MR, &dummy)) return 10;
	if (c4_peek(card, DC21285_DRAM_A1MR, &dummy)) return 11;
	if (c4_peek(card, DC21285_DRAM_A2MR, &dummy)) return 12;
	if (c4_peek(card, DC21285_DRAM_A3MR, &dummy)) return 13;

	if (c4_poke(card, DC21285_DRAM_A0MR+CAS_OFFSET, 0)) return 14;
	if (c4_poke(card, DC21285_DRAM_A1MR+CAS_OFFSET, 0)) return 15;
	if (c4_poke(card, DC21285_DRAM_A2MR+CAS_OFFSET, 0)) return 16;
	if (c4_poke(card, DC21285_DRAM_A3MR+CAS_OFFSET, 0)) return 17;

        mdelay(1);

	if (c4_poke(card, DC21285_ARMCSR_BASE+DRAM_TIMING, DRAM_TIMING_DEF))
		return 18;

	if (c4_poke(card, DC21285_ARMCSR_BASE+DRAM_ADDR_SIZE_0,DRAM_AD_SZ_DEF0))
		return 19;
	if (c4_poke(card, DC21285_ARMCSR_BASE+DRAM_ADDR_SIZE_1,DRAM_AD_SZ_NULL))
		return 20;
	if (c4_poke(card, DC21285_ARMCSR_BASE+DRAM_ADDR_SIZE_2,DRAM_AD_SZ_NULL))
		return 21;
	if (c4_poke(card, DC21285_ARMCSR_BASE+DRAM_ADDR_SIZE_3,DRAM_AD_SZ_NULL))
		return 22;

	/* Transputer test */
	
	if (   c4_poke(card, 0x000000, 0x11111111)
	    || c4_poke(card, 0x400000, 0x22222222)
	    || c4_poke(card, 0x800000, 0x33333333)
	    || c4_poke(card, 0xC00000, 0x44444444))
		return 23;

	if (   c4_peek(card, 0x000000, &dummy) || dummy != 0x11111111
	    || c4_peek(card, 0x400000, &dummy) || dummy != 0x22222222
	    || c4_peek(card, 0x800000, &dummy) || dummy != 0x33333333
	    || c4_peek(card, 0xC00000, &dummy) || dummy != 0x44444444)
		return 24;

	if (   c4_poke(card, 0x000000, 0x55555555)
	    || c4_poke(card, 0x400000, 0x66666666)
	    || c4_poke(card, 0x800000, 0x77777777)
	    || c4_poke(card, 0xC00000, 0x88888888))
		return 25;

	if (   c4_peek(card, 0x000000, &dummy) || dummy != 0x55555555
	    || c4_peek(card, 0x400000, &dummy) || dummy != 0x66666666
	    || c4_peek(card, 0x800000, &dummy) || dummy != 0x77777777
	    || c4_peek(card, 0xC00000, &dummy) || dummy != 0x88888888)
		return 26;

	return 0;
}

/* ------------------------------------------------------------- */

static void c4_dispatch_tx(avmcard *card)
{
	avmcard_dmainfo *dma = card->dma;
	struct sk_buff *skb;
	u8 cmd, subcmd;
	u16 len;
	u32 txlen;
	void *p;


	if (card->csr & DBELL_DOWN_ARM) { /* tx busy */
		return;
	}

	skb = skb_dequeue(&dma->send_queue);
	if (!skb) {
#ifdef AVM_C4_DEBUG
		printk(KERN_DEBUG "%s: tx underrun\n", card->name);
#endif
		return;
	}

	len = CAPIMSG_LEN(skb->data);

	if (len) {
		cmd = CAPIMSG_COMMAND(skb->data);
		subcmd = CAPIMSG_SUBCOMMAND(skb->data);

		p = dma->sendbuf.dmabuf;

		if (CAPICMD(cmd, subcmd) == CAPI_DATA_B3_REQ) {
			u16 dlen = CAPIMSG_DATALEN(skb->data);
			_put_byte(&p, SEND_DATA_B3_REQ);
			_put_slice(&p, skb->data, len);
			_put_slice(&p, skb->data + len, dlen);
		} else {
			_put_byte(&p, SEND_MESSAGE);
			_put_slice(&p, skb->data, len);
		}
		txlen = (u8 *)p - (u8 *)dma->sendbuf.dmabuf;
#ifdef AVM_C4_DEBUG
		printk(KERN_DEBUG "%s: tx put msg len=%d\n", card->name, txlen);
#endif
	} else {
		txlen = skb->len-2;
#ifdef AVM_C4_POLLDEBUG
		if (skb->data[2] == SEND_POLLACK)
			printk(KERN_INFO "%s: ack to c4\n", card->name);
#endif
#ifdef AVM_C4_DEBUG
		printk(KERN_DEBUG "%s: tx put 0x%x len=%d\n",
				card->name, skb->data[2], txlen);
#endif
		skb_copy_from_linear_data_offset(skb, 2, dma->sendbuf.dmabuf,
						 skb->len - 2);
	}
	txlen = (txlen + 3) & ~3;

	c4outmeml(card->mbase+MBOX_DOWN_ADDR, dma->sendbuf.dmaaddr);
	c4outmeml(card->mbase+MBOX_DOWN_LEN, txlen);

	card->csr |= DBELL_DOWN_ARM;

	c4outmeml(card->mbase+DOORBELL, DBELL_DOWN_ARM);

	dev_kfree_skb_any(skb);
}

/* ------------------------------------------------------------- */

static void queue_pollack(avmcard *card)
{
	struct sk_buff *skb;
	void *p;

	skb = alloc_skb(3, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_CRIT "%s: no memory, lost poll ack\n",
					card->name);
		return;
	}
	p = skb->data;
	_put_byte(&p, 0);
	_put_byte(&p, 0);
	_put_byte(&p, SEND_POLLACK);
	skb_put(skb, (u8 *)p - (u8 *)skb->data);

	skb_queue_tail(&card->dma->send_queue, skb);
	c4_dispatch_tx(card);
}

/* ------------------------------------------------------------- */

static void c4_handle_rx(avmcard *card)
{
	avmcard_dmainfo *dma = card->dma;
	struct capi_ctr *ctrl;
	avmctrl_info *cinfo;
	struct sk_buff *skb;
	void *p = dma->recvbuf.dmabuf;
	u32 ApplId, MsgLen, DataB3Len, NCCI, WindowSize;
	u8 b1cmd =  _get_byte(&p);
	u32 cidx;


#ifdef AVM_C4_DEBUG
	printk(KERN_DEBUG "%s: rx 0x%x len=%lu\n", card->name,
				b1cmd, (unsigned long)dma->recvlen);
#endif
	
	switch (b1cmd) {
	case RECEIVE_DATA_B3_IND:

		ApplId = (unsigned) _get_word(&p);
		MsgLen = _get_slice(&p, card->msgbuf);
		DataB3Len = _get_slice(&p, card->databuf);
		cidx = CAPIMSG_CONTROLLER(card->msgbuf)-card->cardnr;
		if (cidx >= card->nlogcontr) cidx = 0;
		ctrl = &card->ctrlinfo[cidx].capi_ctrl;

		if (MsgLen < 30) { /* not CAPI 64Bit */
			memset(card->msgbuf+MsgLen, 0, 30-MsgLen);
			MsgLen = 30;
			CAPIMSG_SETLEN(card->msgbuf, 30);
		}
		if (!(skb = alloc_skb(DataB3Len+MsgLen, GFP_ATOMIC))) {
			printk(KERN_ERR "%s: incoming packet dropped\n",
					card->name);
		} else {
			memcpy(skb_put(skb, MsgLen), card->msgbuf, MsgLen);
			memcpy(skb_put(skb, DataB3Len), card->databuf, DataB3Len);
			capi_ctr_handle_message(ctrl, ApplId, skb);
		}
		break;

	case RECEIVE_MESSAGE:

		ApplId = (unsigned) _get_word(&p);
		MsgLen = _get_slice(&p, card->msgbuf);
		cidx = CAPIMSG_CONTROLLER(card->msgbuf)-card->cardnr;
		if (cidx >= card->nlogcontr) cidx = 0;
		cinfo = &card->ctrlinfo[cidx];
		ctrl = &card->ctrlinfo[cidx].capi_ctrl;

		if (!(skb = alloc_skb(MsgLen, GFP_ATOMIC))) {
			printk(KERN_ERR "%s: incoming packet dropped\n",
					card->name);
		} else {
			memcpy(skb_put(skb, MsgLen), card->msgbuf, MsgLen);
			if (CAPIMSG_CMD(skb->data) == CAPI_DATA_B3_CONF)
				capilib_data_b3_conf(&cinfo->ncci_head, ApplId,
						     CAPIMSG_NCCI(skb->data),
						     CAPIMSG_MSGID(skb->data));

			capi_ctr_handle_message(ctrl, ApplId, skb);
		}
		break;

	case RECEIVE_NEW_NCCI:

		ApplId = _get_word(&p);
		NCCI = _get_word(&p);
		WindowSize = _get_word(&p);
		cidx = (NCCI&0x7f) - card->cardnr;
		if (cidx >= card->nlogcontr) cidx = 0;

		capilib_new_ncci(&card->ctrlinfo[cidx].ncci_head, ApplId, NCCI, WindowSize);

		break;

	case RECEIVE_FREE_NCCI:

		ApplId = _get_word(&p);
		NCCI = _get_word(&p);

		if (NCCI != 0xffffffff) {
			cidx = (NCCI&0x7f) - card->cardnr;
			if (cidx >= card->nlogcontr) cidx = 0;
			capilib_free_ncci(&card->ctrlinfo[cidx].ncci_head, ApplId, NCCI);
		}
		break;

	case RECEIVE_START:
#ifdef AVM_C4_POLLDEBUG
		printk(KERN_INFO "%s: poll from c4\n", card->name);
#endif
		if (!suppress_pollack)
			queue_pollack(card);
		for (cidx=0; cidx < card->nr_controllers; cidx++) {
			ctrl = &card->ctrlinfo[cidx].capi_ctrl;
			capi_ctr_resume_output(ctrl);
		}
		break;

	case RECEIVE_STOP:
		for (cidx=0; cidx < card->nr_controllers; cidx++) {
			ctrl = &card->ctrlinfo[cidx].capi_ctrl;
			capi_ctr_suspend_output(ctrl);
		}
		break;

	case RECEIVE_INIT:

	        cidx = card->nlogcontr;
		if (cidx >= card->nr_controllers) {
			printk(KERN_ERR "%s: card with %d controllers ??\n",
					card->name, cidx+1);
			break;
		}
	        card->nlogcontr++;
	        cinfo = &card->ctrlinfo[cidx];
		ctrl = &cinfo->capi_ctrl;
		cinfo->versionlen = _get_slice(&p, cinfo->versionbuf);
		b1_parse_version(cinfo);
		printk(KERN_INFO "%s: %s-card (%s) now active\n",
		       card->name,
		       cinfo->version[VER_CARDTYPE],
		       cinfo->version[VER_DRIVER]);
		capi_ctr_ready(&cinfo->capi_ctrl);
		break;

	case RECEIVE_TASK_READY:
		ApplId = (unsigned) _get_word(&p);
		MsgLen = _get_slice(&p, card->msgbuf);
		card->msgbuf[MsgLen] = 0;
		while (    MsgLen > 0
		       && (   card->msgbuf[MsgLen-1] == '\n'
			   || card->msgbuf[MsgLen-1] == '\r')) {
			card->msgbuf[MsgLen-1] = 0;
			MsgLen--;
		}
		printk(KERN_INFO "%s: task %d \"%s\" ready.\n",
				card->name, ApplId, card->msgbuf);
		break;

	case RECEIVE_DEBUGMSG:
		MsgLen = _get_slice(&p, card->msgbuf);
		card->msgbuf[MsgLen] = 0;
		while (    MsgLen > 0
		       && (   card->msgbuf[MsgLen-1] == '\n'
			   || card->msgbuf[MsgLen-1] == '\r')) {
			card->msgbuf[MsgLen-1] = 0;
			MsgLen--;
		}
		printk(KERN_INFO "%s: DEBUG: %s\n", card->name, card->msgbuf);
		break;

	default:
		printk(KERN_ERR "%s: c4_interrupt: 0x%x ???\n",
				card->name, b1cmd);
		return;
	}
}

/* ------------------------------------------------------------- */

static irqreturn_t c4_handle_interrupt(avmcard *card)
{
	unsigned long flags;
	u32 status;

	spin_lock_irqsave(&card->lock, flags);
	status = c4inmeml(card->mbase+DOORBELL);

	if (status & DBELL_RESET_HOST) {
		u_int i;
		c4outmeml(card->mbase+PCI_OUT_INT_MASK, 0x0c);
		spin_unlock_irqrestore(&card->lock, flags);
		if (card->nlogcontr == 0)
			return IRQ_HANDLED;
		printk(KERN_ERR "%s: unexpected reset\n", card->name);
                for (i=0; i < card->nr_controllers; i++) {
			avmctrl_info *cinfo = &card->ctrlinfo[i];
			memset(cinfo->version, 0, sizeof(cinfo->version));
			spin_lock_irqsave(&card->lock, flags);
			capilib_release(&cinfo->ncci_head);
			spin_unlock_irqrestore(&card->lock, flags);
			capi_ctr_reseted(&cinfo->capi_ctrl);
		}
		card->nlogcontr = 0;
		return IRQ_HANDLED;
	}

	status &= (DBELL_UP_HOST | DBELL_DOWN_HOST);
	if (!status) {
		spin_unlock_irqrestore(&card->lock, flags);
		return IRQ_HANDLED;
	}
	c4outmeml(card->mbase+DOORBELL, status);

	if ((status & DBELL_UP_HOST) != 0) {
		card->dma->recvlen = c4inmeml(card->mbase+MBOX_UP_LEN);
		c4outmeml(card->mbase+MBOX_UP_LEN, 0);
		c4_handle_rx(card);
		card->dma->recvlen = 0;
		c4outmeml(card->mbase+MBOX_UP_LEN, card->dma->recvbuf.size);
		c4outmeml(card->mbase+DOORBELL, DBELL_UP_ARM);
	}

	if ((status & DBELL_DOWN_HOST) != 0) {
		card->csr &= ~DBELL_DOWN_ARM;
	        c4_dispatch_tx(card);
	} else if (card->csr & DBELL_DOWN_HOST) {
		if (c4inmeml(card->mbase+MBOX_DOWN_LEN) == 0) {
		        card->csr &= ~DBELL_DOWN_ARM;
			c4_dispatch_tx(card);
		}
	}
	spin_unlock_irqrestore(&card->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t c4_interrupt(int interrupt, void *devptr)
{
	avmcard *card = devptr;

	return c4_handle_interrupt(card);
}

/* ------------------------------------------------------------- */

static void c4_send_init(avmcard *card)
{
	struct sk_buff *skb;
	void *p;
	unsigned long flags;

	skb = alloc_skb(15, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_CRIT "%s: no memory, lost register appl.\n",
					card->name);
		return;
	}
	p = skb->data;
	_put_byte(&p, 0);
	_put_byte(&p, 0);
	_put_byte(&p, SEND_INIT);
	_put_word(&p, CAPI_MAXAPPL);
	_put_word(&p, AVM_NCCI_PER_CHANNEL*30);
	_put_word(&p, card->cardnr - 1);
	skb_put(skb, (u8 *)p - (u8 *)skb->data);

	skb_queue_tail(&card->dma->send_queue, skb);
	spin_lock_irqsave(&card->lock, flags);
	c4_dispatch_tx(card);
	spin_unlock_irqrestore(&card->lock, flags);
}

static int queue_sendconfigword(avmcard *card, u32 val)
{
	struct sk_buff *skb;
	unsigned long flags;
	void *p;

	skb = alloc_skb(3+4, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_CRIT "%s: no memory, send config\n",
					card->name);
		return -ENOMEM;
	}
	p = skb->data;
	_put_byte(&p, 0);
	_put_byte(&p, 0);
	_put_byte(&p, SEND_CONFIG);
	_put_word(&p, val);
	skb_put(skb, (u8 *)p - (u8 *)skb->data);

	skb_queue_tail(&card->dma->send_queue, skb);
	spin_lock_irqsave(&card->lock, flags);
	c4_dispatch_tx(card);
	spin_unlock_irqrestore(&card->lock, flags);
	return 0;
}

static int queue_sendconfig(avmcard *card, char cval[4])
{
	struct sk_buff *skb;
	unsigned long flags;
	void *p;

	skb = alloc_skb(3+4, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_CRIT "%s: no memory, send config\n",
					card->name);
		return -ENOMEM;
	}
	p = skb->data;
	_put_byte(&p, 0);
	_put_byte(&p, 0);
	_put_byte(&p, SEND_CONFIG);
	_put_byte(&p, cval[0]);
	_put_byte(&p, cval[1]);
	_put_byte(&p, cval[2]);
	_put_byte(&p, cval[3]);
	skb_put(skb, (u8 *)p - (u8 *)skb->data);

	skb_queue_tail(&card->dma->send_queue, skb);
	
	spin_lock_irqsave(&card->lock, flags);
	c4_dispatch_tx(card);
	spin_unlock_irqrestore(&card->lock, flags);
	return 0;
}

static int c4_send_config(avmcard *card, capiloaddatapart * config)
{
	u8 val[4];
	unsigned char *dp;
	u_int left;
	int retval;
	
	if ((retval = queue_sendconfigword(card, 1)) != 0)
		return retval;
	if ((retval = queue_sendconfigword(card, config->len)) != 0)
		return retval;

	dp = config->data;
	left = config->len;
	while (left >= sizeof(u32)) {
	        if (config->user) {
			if (copy_from_user(val, dp, sizeof(val)))
				return -EFAULT;
		} else {
			memcpy(val, dp, sizeof(val));
		}
		if ((retval = queue_sendconfig(card, val)) != 0)
			return retval;
		left -= sizeof(val);
		dp += sizeof(val);
	}
	if (left) {
		memset(val, 0, sizeof(val));
		if (config->user) {
			if (copy_from_user(&val, dp, left))
				return -EFAULT;
		} else {
			memcpy(&val, dp, left);
		}
		if ((retval = queue_sendconfig(card, val)) != 0)
			return retval;
	}

	return 0;
}

static int c4_load_firmware(struct capi_ctr *ctrl, capiloaddata *data)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	int retval;

	if ((retval = c4_load_t4file(card, &data->firmware))) {
		printk(KERN_ERR "%s: failed to load t4file!!\n",
					card->name);
		c4_reset(card);
		return retval;
	}

	card->csr = 0;
	c4outmeml(card->mbase+MBOX_UP_LEN, 0);
	c4outmeml(card->mbase+MBOX_DOWN_LEN, 0);
	c4outmeml(card->mbase+DOORBELL, DBELL_INIT);
	mdelay(1);
	c4outmeml(card->mbase+DOORBELL,
			DBELL_UP_HOST | DBELL_DOWN_HOST | DBELL_RESET_HOST);

	c4outmeml(card->mbase+PCI_OUT_INT_MASK, 0x08);

	card->dma->recvlen = 0;
	c4outmeml(card->mbase+MBOX_UP_ADDR, card->dma->recvbuf.dmaaddr);
	c4outmeml(card->mbase+MBOX_UP_LEN, card->dma->recvbuf.size);
	c4outmeml(card->mbase+DOORBELL, DBELL_UP_ARM);

	if (data->configuration.len > 0 && data->configuration.data) {
		retval = c4_send_config(card, &data->configuration);
		if (retval) {
			printk(KERN_ERR "%s: failed to set config!!\n",
					card->name);
			c4_reset(card);
			return retval;
		}
	}

        c4_send_init(card);

	return 0;
}


static void c4_reset_ctr(struct capi_ctr *ctrl)
{
	avmcard *card = ((avmctrl_info *)(ctrl->driverdata))->card;
	avmctrl_info *cinfo;
	u_int i;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);

 	c4_reset(card);

	spin_unlock_irqrestore(&card->lock, flags);

        for (i=0; i < card->nr_controllers; i++) {
		cinfo = &card->ctrlinfo[i];
		memset(cinfo->version, 0, sizeof(cinfo->version));
		capi_ctr_reseted(&cinfo->capi_ctrl);
	}
	card->nlogcontr = 0;
}

static void c4_remove(struct pci_dev *pdev)
{
	avmcard *card = pci_get_drvdata(pdev);
	avmctrl_info *cinfo;
	u_int i;

	if (!card)
		return;

 	c4_reset(card);

        for (i=0; i < card->nr_controllers; i++) {
		cinfo = &card->ctrlinfo[i];
		detach_capi_ctr(&cinfo->capi_ctrl);
	}

	free_irq(card->irq, card);
	iounmap(card->mbase);
	release_region(card->port, AVMB1_PORTLEN);
        avmcard_dma_free(card->dma);
        pci_set_drvdata(pdev, NULL);
	b1_free_card(card);
}

/* ------------------------------------------------------------- */


static void c4_register_appl(struct capi_ctr *ctrl,
				u16 appl,
				capi_register_params *rp)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	struct sk_buff *skb;
	int want = rp->level3cnt;
	unsigned long flags;
	int nconn;
	void *p;

	if (ctrl->cnr == card->cardnr) {

		if (want > 0) nconn = want;
		else nconn = ctrl->profile.nbchannel * 4 * -want;
		if (nconn == 0) nconn = ctrl->profile.nbchannel * 4;

		skb = alloc_skb(23, GFP_ATOMIC);
		if (!skb) {
			printk(KERN_CRIT "%s: no memory, lost register appl.\n",
						card->name);
			return;
		}
		p = skb->data;
		_put_byte(&p, 0);
		_put_byte(&p, 0);
		_put_byte(&p, SEND_REGISTER);
		_put_word(&p, appl);
		_put_word(&p, 1024 * (nconn+1));
		_put_word(&p, nconn);
		_put_word(&p, rp->datablkcnt);
		_put_word(&p, rp->datablklen);
		skb_put(skb, (u8 *)p - (u8 *)skb->data);

		skb_queue_tail(&card->dma->send_queue, skb);
	
		spin_lock_irqsave(&card->lock, flags);
		c4_dispatch_tx(card);
		spin_unlock_irqrestore(&card->lock, flags);
	}
}

/* ------------------------------------------------------------- */

static void c4_release_appl(struct capi_ctr *ctrl, u16 appl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned long flags;
	struct sk_buff *skb;
	void *p;

	spin_lock_irqsave(&card->lock, flags);
	capilib_release_appl(&cinfo->ncci_head, appl);
	spin_unlock_irqrestore(&card->lock, flags);

	if (ctrl->cnr == card->cardnr) {
		skb = alloc_skb(7, GFP_ATOMIC);
		if (!skb) {
			printk(KERN_CRIT "%s: no memory, lost release appl.\n",
						card->name);
			return;
		}
		p = skb->data;
		_put_byte(&p, 0);
		_put_byte(&p, 0);
		_put_byte(&p, SEND_RELEASE);
		_put_word(&p, appl);

		skb_put(skb, (u8 *)p - (u8 *)skb->data);
		skb_queue_tail(&card->dma->send_queue, skb);
		spin_lock_irqsave(&card->lock, flags);
		c4_dispatch_tx(card);
		spin_unlock_irqrestore(&card->lock, flags);
	}
}

/* ------------------------------------------------------------- */


static u16 c4_send_message(struct capi_ctr *ctrl, struct sk_buff *skb)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	u16 retval = CAPI_NOERROR;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	if (CAPIMSG_CMD(skb->data) == CAPI_DATA_B3_REQ) {
		retval = capilib_data_b3_req(&cinfo->ncci_head,
					     CAPIMSG_APPID(skb->data),
					     CAPIMSG_NCCI(skb->data),
					     CAPIMSG_MSGID(skb->data));
	}
	if (retval == CAPI_NOERROR) {
		skb_queue_tail(&card->dma->send_queue, skb);
		c4_dispatch_tx(card);
	}
	spin_unlock_irqrestore(&card->lock, flags);
	return retval;
}

/* ------------------------------------------------------------- */

static char *c4_procinfo(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);

	if (!cinfo)
		return "";
	sprintf(cinfo->infobuf, "%s %s 0x%x %d 0x%lx",
		cinfo->cardname[0] ? cinfo->cardname : "-",
		cinfo->version[VER_DRIVER] ? cinfo->version[VER_DRIVER] : "-",
		cinfo->card ? cinfo->card->port : 0x0,
		cinfo->card ? cinfo->card->irq : 0,
		cinfo->card ? cinfo->card->membase : 0
		);
	return cinfo->infobuf;
}

static int c4_read_proc(char *page, char **start, off_t off,
        		int count, int *eof, struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	u8 flag;
	int len = 0;
	char *s;

	len += sprintf(page+len, "%-16s %s\n", "name", card->name);
	len += sprintf(page+len, "%-16s 0x%x\n", "io", card->port);
	len += sprintf(page+len, "%-16s %d\n", "irq", card->irq);
	len += sprintf(page+len, "%-16s 0x%lx\n", "membase", card->membase);
	switch (card->cardtype) {
	case avm_b1isa: s = "B1 ISA"; break;
	case avm_b1pci: s = "B1 PCI"; break;
	case avm_b1pcmcia: s = "B1 PCMCIA"; break;
	case avm_m1: s = "M1"; break;
	case avm_m2: s = "M2"; break;
	case avm_t1isa: s = "T1 ISA (HEMA)"; break;
	case avm_t1pci: s = "T1 PCI"; break;
	case avm_c4: s = "C4"; break;
	case avm_c2: s = "C2"; break;
	default: s = "???"; break;
	}
	len += sprintf(page+len, "%-16s %s\n", "type", s);
	if ((s = cinfo->version[VER_DRIVER]) != NULL)
	   len += sprintf(page+len, "%-16s %s\n", "ver_driver", s);
	if ((s = cinfo->version[VER_CARDTYPE]) != NULL)
	   len += sprintf(page+len, "%-16s %s\n", "ver_cardtype", s);
	if ((s = cinfo->version[VER_SERIAL]) != NULL)
	   len += sprintf(page+len, "%-16s %s\n", "ver_serial", s);

	if (card->cardtype != avm_m1) {
        	flag = ((u8 *)(ctrl->profile.manu))[3];
        	if (flag)
			len += sprintf(page+len, "%-16s%s%s%s%s%s%s%s\n",
			"protocol",
			(flag & 0x01) ? " DSS1" : "",
			(flag & 0x02) ? " CT1" : "",
			(flag & 0x04) ? " VN3" : "",
			(flag & 0x08) ? " NI1" : "",
			(flag & 0x10) ? " AUSTEL" : "",
			(flag & 0x20) ? " ESS" : "",
			(flag & 0x40) ? " 1TR6" : ""
			);
	}
	if (card->cardtype != avm_m1) {
        	flag = ((u8 *)(ctrl->profile.manu))[5];
		if (flag)
			len += sprintf(page+len, "%-16s%s%s%s%s\n",
			"linetype",
			(flag & 0x01) ? " point to point" : "",
			(flag & 0x02) ? " point to multipoint" : "",
			(flag & 0x08) ? " leased line without D-channel" : "",
			(flag & 0x04) ? " leased line with D-channel" : ""
			);
	}
	len += sprintf(page+len, "%-16s %s\n", "cardname", cinfo->cardname);

	if (off+count >= len)
	   *eof = 1;
	if (len < off)
           return 0;
	*start = page + off;
	return ((count < len-off) ? count : len-off);
}

/* ------------------------------------------------------------- */

static int c4_add_card(struct capicardparams *p, struct pci_dev *dev,
		       int nr_controllers)
{
	avmcard *card;
	avmctrl_info *cinfo;
	int retval;
	int i;

	card = b1_alloc_card(nr_controllers);
	if (!card) {
		printk(KERN_WARNING "c4: no memory.\n");
		retval = -ENOMEM;
		goto err;
	}
        card->dma = avmcard_dma_alloc("c4", dev, 2048+128, 2048+128);
	if (!card->dma) {
		printk(KERN_WARNING "c4: no memory.\n");
		retval = -ENOMEM;
		goto err_free;
	}

	sprintf(card->name, "c%d-%x", nr_controllers, p->port);
	card->port = p->port;
	card->irq = p->irq;
	card->membase = p->membase;
	card->cardtype = (nr_controllers == 4) ? avm_c4 : avm_c2;

	if (!request_region(card->port, AVMB1_PORTLEN, card->name)) {
		printk(KERN_WARNING "c4: ports 0x%03x-0x%03x in use.\n",
		       card->port, card->port + AVMB1_PORTLEN);
		retval = -EBUSY;
		goto err_free_dma;
	}

	card->mbase = ioremap(card->membase, 128);
	if (card->mbase == NULL) {
		printk(KERN_NOTICE "c4: can't remap memory at 0x%lx\n",
		       card->membase);
		retval = -EIO;
		goto err_release_region;
	}

	retval = c4_detect(card);
	if (retval != 0) {
		printk(KERN_NOTICE "c4: NO card at 0x%x error(%d)\n",
		       card->port, retval);
		retval = -EIO;
		goto err_unmap;
	}
	c4_reset(card);

	retval = request_irq(card->irq, c4_interrupt, IRQF_SHARED, card->name, card);
	if (retval) {
		printk(KERN_ERR "c4: unable to get IRQ %d.\n",card->irq);
		retval = -EBUSY;
		goto err_unmap;
	}

	for (i=0; i < nr_controllers ; i++) {
		cinfo = &card->ctrlinfo[i];
		cinfo->capi_ctrl.owner = THIS_MODULE;
		cinfo->capi_ctrl.driver_name   = "c4";
		cinfo->capi_ctrl.driverdata    = cinfo;
		cinfo->capi_ctrl.register_appl = c4_register_appl;
		cinfo->capi_ctrl.release_appl  = c4_release_appl;
		cinfo->capi_ctrl.send_message  = c4_send_message;
		cinfo->capi_ctrl.load_firmware = c4_load_firmware;
		cinfo->capi_ctrl.reset_ctr     = c4_reset_ctr;
		cinfo->capi_ctrl.procinfo      = c4_procinfo;
		cinfo->capi_ctrl.ctr_read_proc = c4_read_proc;
		strcpy(cinfo->capi_ctrl.name, card->name);

		retval = attach_capi_ctr(&cinfo->capi_ctrl);
		if (retval) {
			printk(KERN_ERR "c4: attach controller failed (%d).\n", i);
			for (i--; i >= 0; i--) {
				cinfo = &card->ctrlinfo[i];
				detach_capi_ctr(&cinfo->capi_ctrl);
			}
			goto err_free_irq;
		}
		if (i == 0)
			card->cardnr = cinfo->capi_ctrl.cnr;
	}

	printk(KERN_INFO "c4: AVM C%d at i/o %#x, irq %d, mem %#lx\n",
	       nr_controllers, card->port, card->irq,
	       card->membase);
	pci_set_drvdata(dev, card);
	return 0;

 err_free_irq:
	free_irq(card->irq, card);
 err_unmap:
	iounmap(card->mbase);
 err_release_region:
	release_region(card->port, AVMB1_PORTLEN);
 err_free_dma:
	avmcard_dma_free(card->dma);
 err_free:
	b1_free_card(card);
 err:
	return retval;
}

/* ------------------------------------------------------------- */

static int __devinit c4_probe(struct pci_dev *dev,
			      const struct pci_device_id *ent)
{
	int nr = ent->driver_data;
	int retval = 0;
	struct capicardparams param;

	if (pci_enable_device(dev) < 0) {
		printk(KERN_ERR "c4: failed to enable AVM-C%d\n", nr);
		return -ENODEV;
	}
	pci_set_master(dev);

	param.port = pci_resource_start(dev, 1);
	param.irq = dev->irq;
	param.membase = pci_resource_start(dev, 0);
	
	printk(KERN_INFO "c4: PCI BIOS reports AVM-C%d at i/o %#x, irq %d, mem %#x\n",
	       nr, param.port, param.irq, param.membase);
	
	retval = c4_add_card(&param, dev, nr);
	if (retval != 0) {
		printk(KERN_ERR "c4: no AVM-C%d at i/o %#x, irq %d detected, mem %#x\n",
		       nr, param.port, param.irq, param.membase);
		return -ENODEV;
	}
	return 0;
}

static struct pci_driver c4_pci_driver = {
       .name           = "c4",
       .id_table       = c4_pci_tbl,
       .probe          = c4_probe,
       .remove         = c4_remove,
};

static struct capi_driver capi_driver_c2 = {
	.name		= "c2",
	.revision	= "1.0",
};

static struct capi_driver capi_driver_c4 = {
	.name		= "c4",
	.revision	= "1.0",
};

static int __init c4_init(void)
{
	char *p;
	char rev[32];
	int err;

	if ((p = strchr(revision, ':')) != NULL && p[1]) {
		strlcpy(rev, p + 2, 32);
		if ((p = strchr(rev, '$')) != NULL && p > rev)
		   *(p-1) = 0;
	} else
		strcpy(rev, "1.0");

	err = pci_register_driver(&c4_pci_driver);
	if (!err) {
		strlcpy(capi_driver_c2.revision, rev, 32);
		register_capi_driver(&capi_driver_c2);
		strlcpy(capi_driver_c4.revision, rev, 32);
		register_capi_driver(&capi_driver_c4);
		printk(KERN_INFO "c4: revision %s\n", rev);
	}
	return err;
}

static void __exit c4_exit(void)
{
	unregister_capi_driver(&capi_driver_c2);
	unregister_capi_driver(&capi_driver_c4);
	pci_unregister_driver(&c4_pci_driver);
}

module_init(c4_init);
module_exit(c4_exit);
