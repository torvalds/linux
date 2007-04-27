/* $Id: b1dma.c,v 1.1.2.3 2004/02/10 01:07:12 keil Exp $
 * 
 * Common module for AVM B1 cards that support dma with AMCC
 * 
 * Copyright 2000 by Carsten Paeth <calle@calle.de>
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
#include <linux/capi.h>
#include <linux/kernelcapi.h>
#include <asm/io.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/netdevice.h>
#include <linux/isdn/capilli.h>
#include "avmcard.h"
#include <linux/isdn/capicmd.h>
#include <linux/isdn/capiutil.h>

static char *revision = "$Revision: 1.1.2.3 $";

#undef AVM_B1DMA_DEBUG

/* ------------------------------------------------------------- */

MODULE_DESCRIPTION("CAPI4Linux: DMA support for active AVM cards");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");

static int suppress_pollack = 0;
module_param(suppress_pollack, bool, 0);

/* ------------------------------------------------------------- */

static void b1dma_dispatch_tx(avmcard *card);

/* ------------------------------------------------------------- */

/* S5933 */

#define	AMCC_RXPTR	0x24
#define	AMCC_RXLEN	0x28
#define	AMCC_TXPTR	0x2c
#define	AMCC_TXLEN	0x30

#define	AMCC_INTCSR	0x38
#	define EN_READ_TC_INT		0x00008000L
#	define EN_WRITE_TC_INT		0x00004000L
#	define EN_TX_TC_INT		EN_READ_TC_INT
#	define EN_RX_TC_INT		EN_WRITE_TC_INT
#	define AVM_FLAG			0x30000000L

#	define ANY_S5933_INT		0x00800000L
#	define READ_TC_INT		0x00080000L
#	define WRITE_TC_INT		0x00040000L
#	define	TX_TC_INT		READ_TC_INT
#	define	RX_TC_INT		WRITE_TC_INT
#	define MASTER_ABORT_INT		0x00100000L
#	define TARGET_ABORT_INT		0x00200000L
#	define BUS_MASTER_INT		0x00200000L
#	define ALL_INT			0x000C0000L

#define	AMCC_MCSR	0x3c
#	define A2P_HI_PRIORITY		0x00000100L
#	define EN_A2P_TRANSFERS		0x00000400L
#	define P2A_HI_PRIORITY		0x00001000L
#	define EN_P2A_TRANSFERS		0x00004000L
#	define RESET_A2P_FLAGS		0x04000000L
#	define RESET_P2A_FLAGS		0x02000000L

/* ------------------------------------------------------------- */

static inline void b1dma_writel(avmcard *card, u32 value, int off)
{
	writel(value, card->mbase + off);
}

static inline u32 b1dma_readl(avmcard *card, int off)
{
	return readl(card->mbase + off);
}

/* ------------------------------------------------------------- */

static inline int b1dma_tx_empty(unsigned int port)
{
	return inb(port + 0x03) & 0x1;
}

static inline int b1dma_rx_full(unsigned int port)
{
	return inb(port + 0x02) & 0x1;
}

static int b1dma_tolink(avmcard *card, void *buf, unsigned int len)
{
	unsigned long stop = jiffies + 1 * HZ;	/* maximum wait time 1 sec */
	unsigned char *s = (unsigned char *)buf;
	while (len--) {
		while (   !b1dma_tx_empty(card->port)
		       && time_before(jiffies, stop));
		if (!b1dma_tx_empty(card->port)) 
			return -1;
	        t1outp(card->port, 0x01, *s++);
	}
	return 0;
}

static int b1dma_fromlink(avmcard *card, void *buf, unsigned int len)
{
	unsigned long stop = jiffies + 1 * HZ;	/* maximum wait time 1 sec */
	unsigned char *s = (unsigned char *)buf;
	while (len--) {
		while (   !b1dma_rx_full(card->port)
		       && time_before(jiffies, stop));
		if (!b1dma_rx_full(card->port)) 
			return -1;
	        *s++ = t1inp(card->port, 0x00);
	}
	return 0;
}

static int WriteReg(avmcard *card, u32 reg, u8 val)
{
	u8 cmd = 0x00;
	if (   b1dma_tolink(card, &cmd, 1) == 0
	    && b1dma_tolink(card, &reg, 4) == 0) {
		u32 tmp = val;
		return b1dma_tolink(card, &tmp, 4);
	}
	return -1;
}

static u8 ReadReg(avmcard *card, u32 reg)
{
	u8 cmd = 0x01;
	if (   b1dma_tolink(card, &cmd, 1) == 0
	    && b1dma_tolink(card, &reg, 4) == 0) {
		u32 tmp;
		if (b1dma_fromlink(card, &tmp, 4) == 0)
			return (u8)tmp;
	}
	return 0xff;
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

void b1dma_reset(avmcard *card)
{
	card->csr = 0x0;
	b1dma_writel(card, card->csr, AMCC_INTCSR);
	b1dma_writel(card, 0, AMCC_MCSR);
	b1dma_writel(card, 0, AMCC_RXLEN);
	b1dma_writel(card, 0, AMCC_TXLEN);

	t1outp(card->port, 0x10, 0x00);
	t1outp(card->port, 0x07, 0x00);

	b1dma_writel(card, 0, AMCC_MCSR);
	mdelay(10);
	b1dma_writel(card, 0x0f000000, AMCC_MCSR); /* reset all */
	mdelay(10);
	b1dma_writel(card, 0, AMCC_MCSR);
	if (card->cardtype == avm_t1pci)
		mdelay(42);
	else
		mdelay(10);
}

/* ------------------------------------------------------------- */

static int b1dma_detect(avmcard *card)
{
	b1dma_writel(card, 0, AMCC_MCSR);
	mdelay(10);
	b1dma_writel(card, 0x0f000000, AMCC_MCSR); /* reset all */
	mdelay(10);
	b1dma_writel(card, 0, AMCC_MCSR);
	mdelay(42);

	b1dma_writel(card, 0, AMCC_RXLEN);
	b1dma_writel(card, 0, AMCC_TXLEN);
	card->csr = 0x0;
	b1dma_writel(card, card->csr, AMCC_INTCSR);

	if (b1dma_readl(card, AMCC_MCSR) != 0x000000E6)
		return 1;

	b1dma_writel(card, 0xffffffff, AMCC_RXPTR);
	b1dma_writel(card, 0xffffffff, AMCC_TXPTR);
	if (   b1dma_readl(card, AMCC_RXPTR) != 0xfffffffc
	    || b1dma_readl(card, AMCC_TXPTR) != 0xfffffffc)
		return 2;

	b1dma_writel(card, 0x0, AMCC_RXPTR);
	b1dma_writel(card, 0x0, AMCC_TXPTR);
	if (   b1dma_readl(card, AMCC_RXPTR) != 0x0
	    || b1dma_readl(card, AMCC_TXPTR) != 0x0)
		return 3;

	t1outp(card->port, 0x10, 0x00);
	t1outp(card->port, 0x07, 0x00);
	
	t1outp(card->port, 0x02, 0x02);
	t1outp(card->port, 0x03, 0x02);

	if (   (t1inp(card->port, 0x02) & 0xFE) != 0x02
	    || t1inp(card->port, 0x3) != 0x03)
		return 4;

	t1outp(card->port, 0x02, 0x00);
	t1outp(card->port, 0x03, 0x00);

	if (   (t1inp(card->port, 0x02) & 0xFE) != 0x00
	    || t1inp(card->port, 0x3) != 0x01)
		return 5;

	return 0;
}

int t1pci_detect(avmcard *card)
{
	int ret;

	if ((ret = b1dma_detect(card)) != 0)
		return ret;
	
	/* Transputer test */
	
	if (   WriteReg(card, 0x80001000, 0x11) != 0
	    || WriteReg(card, 0x80101000, 0x22) != 0
	    || WriteReg(card, 0x80201000, 0x33) != 0
	    || WriteReg(card, 0x80301000, 0x44) != 0)
		return 6;

	if (   ReadReg(card, 0x80001000) != 0x11
	    || ReadReg(card, 0x80101000) != 0x22
	    || ReadReg(card, 0x80201000) != 0x33
	    || ReadReg(card, 0x80301000) != 0x44)
		return 7;

	if (   WriteReg(card, 0x80001000, 0x55) != 0
	    || WriteReg(card, 0x80101000, 0x66) != 0
	    || WriteReg(card, 0x80201000, 0x77) != 0
	    || WriteReg(card, 0x80301000, 0x88) != 0)
		return 8;

	if (   ReadReg(card, 0x80001000) != 0x55
	    || ReadReg(card, 0x80101000) != 0x66
	    || ReadReg(card, 0x80201000) != 0x77
	    || ReadReg(card, 0x80301000) != 0x88)
		return 9;

	return 0;
}

int b1pciv4_detect(avmcard *card)
{
	int ret, i;

	if ((ret = b1dma_detect(card)) != 0)
		return ret;
	
	for (i=0; i < 5 ; i++) {
		if (WriteReg(card, 0x80A00000, 0x21) != 0)
			return 6;
		if ((ReadReg(card, 0x80A00000) & 0x01) != 0x01)
			return 7;
	}
	for (i=0; i < 5 ; i++) {
		if (WriteReg(card, 0x80A00000, 0x20) != 0)
			return 8;
		if ((ReadReg(card, 0x80A00000) & 0x01) != 0x00)
			return 9;
	}
	
	return 0;
}

static void b1dma_queue_tx(avmcard *card, struct sk_buff *skb)
{
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);

	skb_queue_tail(&card->dma->send_queue, skb);

	if (!(card->csr & EN_TX_TC_INT)) {
		b1dma_dispatch_tx(card);
		b1dma_writel(card, card->csr, AMCC_INTCSR);
	}

	spin_unlock_irqrestore(&card->lock, flags);
}

/* ------------------------------------------------------------- */

static void b1dma_dispatch_tx(avmcard *card)
{
	avmcard_dmainfo *dma = card->dma;
	struct sk_buff *skb;
	u8 cmd, subcmd;
	u16 len;
	u32 txlen;
	void *p;
	
	skb = skb_dequeue(&dma->send_queue);

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
#ifdef AVM_B1DMA_DEBUG
		printk(KERN_DEBUG "tx: put msg len=%d\n", txlen);
#endif
	} else {
		txlen = skb->len-2;
#ifdef AVM_B1DMA_POLLDEBUG
		if (skb->data[2] == SEND_POLLACK)
			printk(KERN_INFO "%s: send ack\n", card->name);
#endif
#ifdef AVM_B1DMA_DEBUG
		printk(KERN_DEBUG "tx: put 0x%x len=%d\n", 
		       skb->data[2], txlen);
#endif
		skb_copy_from_linear_data_offset(skb, 2, dma->sendbuf.dmabuf,
						 skb->len - 2);
	}
	txlen = (txlen + 3) & ~3;

	b1dma_writel(card, dma->sendbuf.dmaaddr, AMCC_TXPTR);
	b1dma_writel(card, txlen, AMCC_TXLEN);

	card->csr |= EN_TX_TC_INT;

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

	b1dma_queue_tx(card, skb);
}

/* ------------------------------------------------------------- */

static void b1dma_handle_rx(avmcard *card)
{
	avmctrl_info *cinfo = &card->ctrlinfo[0];
	avmcard_dmainfo *dma = card->dma;
	struct capi_ctr *ctrl = &cinfo->capi_ctrl;
	struct sk_buff *skb;
	void *p = dma->recvbuf.dmabuf+4;
	u32 ApplId, MsgLen, DataB3Len, NCCI, WindowSize;
	u8 b1cmd =  _get_byte(&p);

#ifdef AVM_B1DMA_DEBUG
	printk(KERN_DEBUG "rx: 0x%x %lu\n", b1cmd, (unsigned long)dma->recvlen);
#endif
	
	switch (b1cmd) {
	case RECEIVE_DATA_B3_IND:

		ApplId = (unsigned) _get_word(&p);
		MsgLen = _get_slice(&p, card->msgbuf);
		DataB3Len = _get_slice(&p, card->databuf);

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

		capilib_new_ncci(&cinfo->ncci_head, ApplId, NCCI, WindowSize);

		break;

	case RECEIVE_FREE_NCCI:

		ApplId = _get_word(&p);
		NCCI = _get_word(&p);

		if (NCCI != 0xffffffff)
			capilib_free_ncci(&cinfo->ncci_head, ApplId, NCCI);

		break;

	case RECEIVE_START:
#ifdef AVM_B1DMA_POLLDEBUG
		printk(KERN_INFO "%s: receive poll\n", card->name);
#endif
		if (!suppress_pollack)
			queue_pollack(card);
		capi_ctr_resume_output(ctrl);
		break;

	case RECEIVE_STOP:
		capi_ctr_suspend_output(ctrl);
		break;

	case RECEIVE_INIT:

		cinfo->versionlen = _get_slice(&p, cinfo->versionbuf);
		b1_parse_version(cinfo);
		printk(KERN_INFO "%s: %s-card (%s) now active\n",
		       card->name,
		       cinfo->version[VER_CARDTYPE],
		       cinfo->version[VER_DRIVER]);
		capi_ctr_ready(ctrl);
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
		printk(KERN_ERR "%s: b1dma_interrupt: 0x%x ???\n",
				card->name, b1cmd);
		return;
	}
}

/* ------------------------------------------------------------- */

static void b1dma_handle_interrupt(avmcard *card)
{
	u32 status;
	u32 newcsr;

	spin_lock(&card->lock);

	status = b1dma_readl(card, AMCC_INTCSR);
	if ((status & ANY_S5933_INT) == 0) {
		spin_unlock(&card->lock);
		return;
	}

        newcsr = card->csr | (status & ALL_INT);
	if (status & TX_TC_INT) newcsr &= ~EN_TX_TC_INT;
	if (status & RX_TC_INT) newcsr &= ~EN_RX_TC_INT;
	b1dma_writel(card, newcsr, AMCC_INTCSR);

	if ((status & RX_TC_INT) != 0) {
		struct avmcard_dmainfo *dma = card->dma;
		u32 rxlen;
	   	if (card->dma->recvlen == 0) {
	        	rxlen = b1dma_readl(card, AMCC_RXLEN);
			if (rxlen == 0) {
				dma->recvlen = *((u32 *)dma->recvbuf.dmabuf);
				rxlen = (dma->recvlen + 3) & ~3;
				b1dma_writel(card, dma->recvbuf.dmaaddr+4, AMCC_RXPTR);
				b1dma_writel(card, rxlen, AMCC_RXLEN);
#ifdef AVM_B1DMA_DEBUG
			} else {
				printk(KERN_ERR "%s: rx not complete (%d).\n",
					card->name, rxlen);
#endif
			}
		} else {
			spin_unlock(&card->lock);
			b1dma_handle_rx(card);
	   		dma->recvlen = 0;
			spin_lock(&card->lock);
			b1dma_writel(card, dma->recvbuf.dmaaddr, AMCC_RXPTR);
			b1dma_writel(card, 4, AMCC_RXLEN);
		}
	}

	if ((status & TX_TC_INT) != 0) {
		if (skb_queue_empty(&card->dma->send_queue))
			card->csr &= ~EN_TX_TC_INT;
		else
			b1dma_dispatch_tx(card);
	}
	b1dma_writel(card, card->csr, AMCC_INTCSR);

	spin_unlock(&card->lock);
}

irqreturn_t b1dma_interrupt(int interrupt, void *devptr)
{
	avmcard *card = devptr;

	b1dma_handle_interrupt(card);
	return IRQ_HANDLED;
}

/* ------------------------------------------------------------- */

static int b1dma_loaded(avmcard *card)
{
	unsigned long stop;
	unsigned char ans;
	unsigned long tout = 2;
	unsigned int base = card->port;

	for (stop = jiffies + tout * HZ; time_before(jiffies, stop);) {
		if (b1_tx_empty(base))
			break;
	}
	if (!b1_tx_empty(base)) {
		printk(KERN_ERR "%s: b1dma_loaded: tx err, corrupted t4 file ?\n",
				card->name);
		return 0;
	}
	b1_put_byte(base, SEND_POLLACK);
	for (stop = jiffies + tout * HZ; time_before(jiffies, stop);) {
		if (b1_rx_full(base)) {
			if ((ans = b1_get_byte(base)) == RECEIVE_POLLDWORD) {
				return 1;
			}
			printk(KERN_ERR "%s: b1dma_loaded: got 0x%x, firmware not running in dword mode\n", card->name, ans);
			return 0;
		}
	}
	printk(KERN_ERR "%s: b1dma_loaded: firmware not running\n", card->name);
	return 0;
}

/* ------------------------------------------------------------- */

static void b1dma_send_init(avmcard *card)
{
	struct sk_buff *skb;
	void *p;

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

	b1dma_queue_tx(card, skb);
}

int b1dma_load_firmware(struct capi_ctr *ctrl, capiloaddata *data)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	int retval;

	b1dma_reset(card);

	if ((retval = b1_load_t4file(card, &data->firmware))) {
		b1dma_reset(card);
		printk(KERN_ERR "%s: failed to load t4file!!\n",
					card->name);
		return retval;
	}

	if (data->configuration.len > 0 && data->configuration.data) {
		if ((retval = b1_load_config(card, &data->configuration))) {
			b1dma_reset(card);
			printk(KERN_ERR "%s: failed to load config!!\n",
					card->name);
			return retval;
		}
	}

	if (!b1dma_loaded(card)) {
		b1dma_reset(card);
		printk(KERN_ERR "%s: failed to load t4file.\n", card->name);
		return -EIO;
	}

	card->csr = AVM_FLAG;
	b1dma_writel(card, card->csr, AMCC_INTCSR);
	b1dma_writel(card, EN_A2P_TRANSFERS|EN_P2A_TRANSFERS|A2P_HI_PRIORITY|
		     P2A_HI_PRIORITY|RESET_A2P_FLAGS|RESET_P2A_FLAGS, 
		     AMCC_MCSR);
	t1outp(card->port, 0x07, 0x30);
	t1outp(card->port, 0x10, 0xF0);

	card->dma->recvlen = 0;
	b1dma_writel(card, card->dma->recvbuf.dmaaddr, AMCC_RXPTR);
	b1dma_writel(card, 4, AMCC_RXLEN);
	card->csr |= EN_RX_TC_INT;
	b1dma_writel(card, card->csr, AMCC_INTCSR);

        b1dma_send_init(card);

	return 0;
}

void b1dma_reset_ctr(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
 	b1dma_reset(card);
	spin_unlock_irqrestore(&card->lock, flags);

	memset(cinfo->version, 0, sizeof(cinfo->version));
	capilib_release(&cinfo->ncci_head);
	capi_ctr_reseted(ctrl);
}

/* ------------------------------------------------------------- */

void b1dma_register_appl(struct capi_ctr *ctrl,
				u16 appl,
				capi_register_params *rp)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	struct sk_buff *skb;
	int want = rp->level3cnt;
	int nconn;
	void *p;

	if (want > 0) nconn = want;
	else nconn = ctrl->profile.nbchannel * -want;
	if (nconn == 0) nconn = ctrl->profile.nbchannel;

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

	b1dma_queue_tx(card, skb);
}

/* ------------------------------------------------------------- */

void b1dma_release_appl(struct capi_ctr *ctrl, u16 appl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	struct sk_buff *skb;
	void *p;

	capilib_release_appl(&cinfo->ncci_head, appl);

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

	b1dma_queue_tx(card, skb);
}

/* ------------------------------------------------------------- */

u16 b1dma_send_message(struct capi_ctr *ctrl, struct sk_buff *skb)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	u16 retval = CAPI_NOERROR;

 	if (CAPIMSG_CMD(skb->data) == CAPI_DATA_B3_REQ) {
		retval = capilib_data_b3_req(&cinfo->ncci_head,
					     CAPIMSG_APPID(skb->data),
					     CAPIMSG_NCCI(skb->data),
					     CAPIMSG_MSGID(skb->data));
	}
	if (retval == CAPI_NOERROR) 
		b1dma_queue_tx(card, skb);

	return retval;
}

/* ------------------------------------------------------------- */

int b1dmactl_read_proc(char *page, char **start, off_t off,
        		int count, int *eof, struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	u8 flag;
	int len = 0;
	char *s;
	u32 txoff, txlen, rxoff, rxlen, csr;
	unsigned long flags;

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
	if ((s = cinfo->version[VER_DRIVER]) != 0)
	   len += sprintf(page+len, "%-16s %s\n", "ver_driver", s);
	if ((s = cinfo->version[VER_CARDTYPE]) != 0)
	   len += sprintf(page+len, "%-16s %s\n", "ver_cardtype", s);
	if ((s = cinfo->version[VER_SERIAL]) != 0)
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


	spin_lock_irqsave(&card->lock, flags);

	txoff = (dma_addr_t)b1dma_readl(card, AMCC_TXPTR)-card->dma->sendbuf.dmaaddr;
	txlen = b1dma_readl(card, AMCC_TXLEN);

	rxoff = (dma_addr_t)b1dma_readl(card, AMCC_RXPTR)-card->dma->recvbuf.dmaaddr;
	rxlen = b1dma_readl(card, AMCC_RXLEN);

	csr  = b1dma_readl(card, AMCC_INTCSR);

	spin_unlock_irqrestore(&card->lock, flags);

        len += sprintf(page+len, "%-16s 0x%lx\n",
				"csr (cached)", (unsigned long)card->csr);
        len += sprintf(page+len, "%-16s 0x%lx\n",
				"csr", (unsigned long)csr);
        len += sprintf(page+len, "%-16s %lu\n",
				"txoff", (unsigned long)txoff);
        len += sprintf(page+len, "%-16s %lu\n",
				"txlen", (unsigned long)txlen);
        len += sprintf(page+len, "%-16s %lu\n",
				"rxoff", (unsigned long)rxoff);
        len += sprintf(page+len, "%-16s %lu\n",
				"rxlen", (unsigned long)rxlen);

	if (off+count >= len)
	   *eof = 1;
	if (len < off)
           return 0;
	*start = page + off;
	return ((count < len-off) ? count : len-off);
}

/* ------------------------------------------------------------- */

EXPORT_SYMBOL(b1dma_reset);
EXPORT_SYMBOL(t1pci_detect);
EXPORT_SYMBOL(b1pciv4_detect);
EXPORT_SYMBOL(b1dma_interrupt);

EXPORT_SYMBOL(b1dma_load_firmware);
EXPORT_SYMBOL(b1dma_reset_ctr);
EXPORT_SYMBOL(b1dma_register_appl);
EXPORT_SYMBOL(b1dma_release_appl);
EXPORT_SYMBOL(b1dma_send_message);
EXPORT_SYMBOL(b1dmactl_read_proc);

static int __init b1dma_init(void)
{
	char *p;
	char rev[32];

	if ((p = strchr(revision, ':')) != 0 && p[1]) {
		strlcpy(rev, p + 2, sizeof(rev));
		if ((p = strchr(rev, '$')) != 0 && p > rev)
		   *(p-1) = 0;
	} else
		strcpy(rev, "1.0");

	printk(KERN_INFO "b1dma: revision %s\n", rev);

	return 0;
}

static void __exit b1dma_exit(void)
{
}

module_init(b1dma_init);
module_exit(b1dma_exit);
