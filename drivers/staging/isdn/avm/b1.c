/* $Id: b1.c,v 1.1.2.2 2004/01/16 21:09:27 keil Exp $
 *
 * Common module for AVM B1 cards.
 *
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/capi.h>
#include <linux/kernelcapi.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/netdevice.h>
#include <linux/isdn/capilli.h>
#include "avmcard.h"
#include <linux/isdn/capicmd.h>
#include <linux/isdn/capiutil.h>

static char *revision = "$Revision: 1.1.2.2 $";

/* ------------------------------------------------------------- */

MODULE_DESCRIPTION("CAPI4Linux: Common support for active AVM cards");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------- */

int b1_irq_table[16] =
{0,
 0,
 0,
 192,				/* irq 3 */
 32,				/* irq 4 */
 160,				/* irq 5 */
 96,				/* irq 6 */
 224,				/* irq 7 */
 0,
 64,				/* irq 9 */
 80,				/* irq 10 */
 208,				/* irq 11 */
 48,				/* irq 12 */
 0,
 0,
 112,				/* irq 15 */
};

/* ------------------------------------------------------------- */

avmcard *b1_alloc_card(int nr_controllers)
{
	avmcard *card;
	avmctrl_info *cinfo;
	int i;

	card = kzalloc(sizeof(*card), GFP_KERNEL);
	if (!card)
		return NULL;

	cinfo = kcalloc(nr_controllers, sizeof(*cinfo), GFP_KERNEL);
	if (!cinfo) {
		kfree(card);
		return NULL;
	}

	card->ctrlinfo = cinfo;
	for (i = 0; i < nr_controllers; i++) {
		INIT_LIST_HEAD(&cinfo[i].ncci_head);
		cinfo[i].card = card;
	}
	spin_lock_init(&card->lock);
	card->nr_controllers = nr_controllers;

	return card;
}

/* ------------------------------------------------------------- */

void b1_free_card(avmcard *card)
{
	kfree(card->ctrlinfo);
	kfree(card);
}

/* ------------------------------------------------------------- */

int b1_detect(unsigned int base, enum avmcardtype cardtype)
{
	int onoff, i;

	/*
	 * Statusregister 0000 00xx
	 */
	if ((inb(base + B1_INSTAT) & 0xfc)
	    || (inb(base + B1_OUTSTAT) & 0xfc))
		return 1;
	/*
	 * Statusregister 0000 001x
	 */
	b1outp(base, B1_INSTAT, 0x2);	/* enable irq */
	/* b1outp(base, B1_OUTSTAT, 0x2); */
	if ((inb(base + B1_INSTAT) & 0xfe) != 0x2
	    /* || (inb(base + B1_OUTSTAT) & 0xfe) != 0x2 */)
		return 2;
	/*
	 * Statusregister 0000 000x
	 */
	b1outp(base, B1_INSTAT, 0x0);	/* disable irq */
	b1outp(base, B1_OUTSTAT, 0x0);
	if ((inb(base + B1_INSTAT) & 0xfe)
	    || (inb(base + B1_OUTSTAT) & 0xfe))
		return 3;

	for (onoff = !0, i = 0; i < 10; i++) {
		b1_set_test_bit(base, cardtype, onoff);
		if (b1_get_test_bit(base, cardtype) != onoff)
			return 4;
		onoff = !onoff;
	}

	if (cardtype == avm_m1)
		return 0;

	if ((b1_rd_reg(base, B1_STAT1(cardtype)) & 0x0f) != 0x01)
		return 5;

	return 0;
}

void b1_getrevision(avmcard *card)
{
	card->class = inb(card->port + B1_ANALYSE);
	card->revision = inb(card->port + B1_REVISION);
}

#define FWBUF_SIZE	256
int b1_load_t4file(avmcard *card, capiloaddatapart *t4file)
{
	unsigned char buf[FWBUF_SIZE];
	unsigned char *dp;
	int i, left;
	unsigned int base = card->port;

	dp = t4file->data;
	left = t4file->len;
	while (left > FWBUF_SIZE) {
		if (t4file->user) {
			if (copy_from_user(buf, dp, FWBUF_SIZE))
				return -EFAULT;
		} else {
			memcpy(buf, dp, FWBUF_SIZE);
		}
		for (i = 0; i < FWBUF_SIZE; i++)
			if (b1_save_put_byte(base, buf[i]) < 0) {
				printk(KERN_ERR "%s: corrupted firmware file ?\n",
				       card->name);
				return -EIO;
			}
		left -= FWBUF_SIZE;
		dp += FWBUF_SIZE;
	}
	if (left) {
		if (t4file->user) {
			if (copy_from_user(buf, dp, left))
				return -EFAULT;
		} else {
			memcpy(buf, dp, left);
		}
		for (i = 0; i < left; i++)
			if (b1_save_put_byte(base, buf[i]) < 0) {
				printk(KERN_ERR "%s: corrupted firmware file ?\n",
				       card->name);
				return -EIO;
			}
	}
	return 0;
}

int b1_load_config(avmcard *card, capiloaddatapart *config)
{
	unsigned char buf[FWBUF_SIZE];
	unsigned char *dp;
	unsigned int base = card->port;
	int i, j, left;

	dp = config->data;
	left = config->len;
	if (left) {
		b1_put_byte(base, SEND_CONFIG);
		b1_put_word(base, 1);
		b1_put_byte(base, SEND_CONFIG);
		b1_put_word(base, left);
	}
	while (left > FWBUF_SIZE) {
		if (config->user) {
			if (copy_from_user(buf, dp, FWBUF_SIZE))
				return -EFAULT;
		} else {
			memcpy(buf, dp, FWBUF_SIZE);
		}
		for (i = 0; i < FWBUF_SIZE; ) {
			b1_put_byte(base, SEND_CONFIG);
			for (j = 0; j < 4; j++) {
				b1_put_byte(base, buf[i++]);
			}
		}
		left -= FWBUF_SIZE;
		dp += FWBUF_SIZE;
	}
	if (left) {
		if (config->user) {
			if (copy_from_user(buf, dp, left))
				return -EFAULT;
		} else {
			memcpy(buf, dp, left);
		}
		for (i = 0; i < left; ) {
			b1_put_byte(base, SEND_CONFIG);
			for (j = 0; j < 4; j++) {
				if (i < left)
					b1_put_byte(base, buf[i++]);
				else
					b1_put_byte(base, 0);
			}
		}
	}
	return 0;
}

int b1_loaded(avmcard *card)
{
	unsigned int base = card->port;
	unsigned long stop;
	unsigned char ans;
	unsigned long tout = 2;

	for (stop = jiffies + tout * HZ; time_before(jiffies, stop);) {
		if (b1_tx_empty(base))
			break;
	}
	if (!b1_tx_empty(base)) {
		printk(KERN_ERR "%s: b1_loaded: tx err, corrupted t4 file ?\n",
		       card->name);
		return 0;
	}
	b1_put_byte(base, SEND_POLL);
	for (stop = jiffies + tout * HZ; time_before(jiffies, stop);) {
		if (b1_rx_full(base)) {
			ans = b1_get_byte(base);
			if (ans == RECEIVE_POLL)
				return 1;

			printk(KERN_ERR "%s: b1_loaded: got 0x%x, firmware not running\n",
			       card->name, ans);
			return 0;
		}
	}
	printk(KERN_ERR "%s: b1_loaded: firmware not running\n", card->name);
	return 0;
}

/* ------------------------------------------------------------- */

int b1_load_firmware(struct capi_ctr *ctrl, capiloaddata *data)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;
	unsigned long flags;
	int retval;

	b1_reset(port);
	retval = b1_load_t4file(card, &data->firmware);

	if (retval) {
		b1_reset(port);
		printk(KERN_ERR "%s: failed to load t4file!!\n",
		       card->name);
		return retval;
	}

	b1_disable_irq(port);

	if (data->configuration.len > 0 && data->configuration.data) {
		retval = b1_load_config(card, &data->configuration);
		if (retval) {
			b1_reset(port);
			printk(KERN_ERR "%s: failed to load config!!\n",
			       card->name);
			return retval;
		}
	}

	if (!b1_loaded(card)) {
		printk(KERN_ERR "%s: failed to load t4file.\n", card->name);
		return -EIO;
	}

	spin_lock_irqsave(&card->lock, flags);
	b1_setinterrupt(port, card->irq, card->cardtype);
	b1_put_byte(port, SEND_INIT);
	b1_put_word(port, CAPI_MAXAPPL);
	b1_put_word(port, AVM_NCCI_PER_CHANNEL * 2);
	b1_put_word(port, ctrl->cnr - 1);
	spin_unlock_irqrestore(&card->lock, flags);

	return 0;
}

void b1_reset_ctr(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;
	unsigned long flags;

	b1_reset(port);
	b1_reset(port);

	memset(cinfo->version, 0, sizeof(cinfo->version));
	spin_lock_irqsave(&card->lock, flags);
	capilib_release(&cinfo->ncci_head);
	spin_unlock_irqrestore(&card->lock, flags);
	capi_ctr_down(ctrl);
}

void b1_register_appl(struct capi_ctr *ctrl,
		      u16 appl,
		      capi_register_params *rp)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;
	unsigned long flags;
	int nconn, want = rp->level3cnt;

	if (want > 0) nconn = want;
	else nconn = ctrl->profile.nbchannel * -want;
	if (nconn == 0) nconn = ctrl->profile.nbchannel;

	spin_lock_irqsave(&card->lock, flags);
	b1_put_byte(port, SEND_REGISTER);
	b1_put_word(port, appl);
	b1_put_word(port, 1024 * (nconn + 1));
	b1_put_word(port, nconn);
	b1_put_word(port, rp->datablkcnt);
	b1_put_word(port, rp->datablklen);
	spin_unlock_irqrestore(&card->lock, flags);
}

void b1_release_appl(struct capi_ctr *ctrl, u16 appl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	capilib_release_appl(&cinfo->ncci_head, appl);
	b1_put_byte(port, SEND_RELEASE);
	b1_put_word(port, appl);
	spin_unlock_irqrestore(&card->lock, flags);
}

u16 b1_send_message(struct capi_ctr *ctrl, struct sk_buff *skb)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;
	unsigned long flags;
	u16 len = CAPIMSG_LEN(skb->data);
	u8 cmd = CAPIMSG_COMMAND(skb->data);
	u8 subcmd = CAPIMSG_SUBCOMMAND(skb->data);
	u16 dlen, retval;

	spin_lock_irqsave(&card->lock, flags);
	if (CAPICMD(cmd, subcmd) == CAPI_DATA_B3_REQ) {
		retval = capilib_data_b3_req(&cinfo->ncci_head,
					     CAPIMSG_APPID(skb->data),
					     CAPIMSG_NCCI(skb->data),
					     CAPIMSG_MSGID(skb->data));
		if (retval != CAPI_NOERROR) {
			spin_unlock_irqrestore(&card->lock, flags);
			return retval;
		}

		dlen = CAPIMSG_DATALEN(skb->data);

		b1_put_byte(port, SEND_DATA_B3_REQ);
		b1_put_slice(port, skb->data, len);
		b1_put_slice(port, skb->data + len, dlen);
	} else {
		b1_put_byte(port, SEND_MESSAGE);
		b1_put_slice(port, skb->data, len);
	}
	spin_unlock_irqrestore(&card->lock, flags);

	dev_kfree_skb_any(skb);
	return CAPI_NOERROR;
}

/* ------------------------------------------------------------- */

void b1_parse_version(avmctrl_info *cinfo)
{
	struct capi_ctr *ctrl = &cinfo->capi_ctrl;
	avmcard *card = cinfo->card;
	capi_profile *profp;
	u8 *dversion;
	u8 flag;
	int i, j;

	for (j = 0; j < AVM_MAXVERSION; j++)
		cinfo->version[j] = "";
	for (i = 0, j = 0;
	     j < AVM_MAXVERSION && i < cinfo->versionlen;
	     j++, i += cinfo->versionbuf[i] + 1)
		cinfo->version[j] = &cinfo->versionbuf[i + 1];

	strlcpy(ctrl->serial, cinfo->version[VER_SERIAL], sizeof(ctrl->serial));
	memcpy(&ctrl->profile, cinfo->version[VER_PROFILE], sizeof(capi_profile));
	strlcpy(ctrl->manu, "AVM GmbH", sizeof(ctrl->manu));
	dversion = cinfo->version[VER_DRIVER];
	ctrl->version.majorversion = 2;
	ctrl->version.minorversion = 0;
	ctrl->version.majormanuversion = (((dversion[0] - '0') & 0xf) << 4);
	ctrl->version.majormanuversion |= ((dversion[2] - '0') & 0xf);
	ctrl->version.minormanuversion = (dversion[3] - '0') << 4;
	ctrl->version.minormanuversion |=
		(dversion[5] - '0') * 10 + ((dversion[6] - '0') & 0xf);

	profp = &ctrl->profile;

	flag = ((u8 *)(profp->manu))[1];
	switch (flag) {
	case 0: if (cinfo->version[VER_CARDTYPE])
			strcpy(cinfo->cardname, cinfo->version[VER_CARDTYPE]);
		else strcpy(cinfo->cardname, "B1");
		break;
	case 3: strcpy(cinfo->cardname, "PCMCIA B"); break;
	case 4: strcpy(cinfo->cardname, "PCMCIA M1"); break;
	case 5: strcpy(cinfo->cardname, "PCMCIA M2"); break;
	case 6: strcpy(cinfo->cardname, "B1 V3.0"); break;
	case 7: strcpy(cinfo->cardname, "B1 PCI"); break;
	default: sprintf(cinfo->cardname, "AVM?%u", (unsigned int)flag); break;
	}
	printk(KERN_NOTICE "%s: card %d \"%s\" ready.\n",
	       card->name, ctrl->cnr, cinfo->cardname);

	flag = ((u8 *)(profp->manu))[3];
	if (flag)
		printk(KERN_NOTICE "%s: card %d Protocol:%s%s%s%s%s%s%s\n",
		       card->name,
		       ctrl->cnr,
		       (flag & 0x01) ? " DSS1" : "",
		       (flag & 0x02) ? " CT1" : "",
		       (flag & 0x04) ? " VN3" : "",
		       (flag & 0x08) ? " NI1" : "",
		       (flag & 0x10) ? " AUSTEL" : "",
		       (flag & 0x20) ? " ESS" : "",
		       (flag & 0x40) ? " 1TR6" : ""
			);

	flag = ((u8 *)(profp->manu))[5];
	if (flag)
		printk(KERN_NOTICE "%s: card %d Linetype:%s%s%s%s\n",
		       card->name,
		       ctrl->cnr,
		       (flag & 0x01) ? " point to point" : "",
		       (flag & 0x02) ? " point to multipoint" : "",
		       (flag & 0x08) ? " leased line without D-channel" : "",
		       (flag & 0x04) ? " leased line with D-channel" : ""
			);
}

/* ------------------------------------------------------------- */

irqreturn_t b1_interrupt(int interrupt, void *devptr)
{
	avmcard *card = devptr;
	avmctrl_info *cinfo = &card->ctrlinfo[0];
	struct capi_ctr *ctrl = &cinfo->capi_ctrl;
	unsigned char b1cmd;
	struct sk_buff *skb;

	unsigned ApplId;
	unsigned MsgLen;
	unsigned DataB3Len;
	unsigned NCCI;
	unsigned WindowSize;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);

	if (!b1_rx_full(card->port)) {
		spin_unlock_irqrestore(&card->lock, flags);
		return IRQ_NONE;
	}

	b1cmd = b1_get_byte(card->port);

	switch (b1cmd) {

	case RECEIVE_DATA_B3_IND:

		ApplId = (unsigned) b1_get_word(card->port);
		MsgLen = b1_get_slice(card->port, card->msgbuf);
		DataB3Len = b1_get_slice(card->port, card->databuf);
		spin_unlock_irqrestore(&card->lock, flags);

		if (MsgLen < 30) { /* not CAPI 64Bit */
			memset(card->msgbuf + MsgLen, 0, 30-MsgLen);
			MsgLen = 30;
			CAPIMSG_SETLEN(card->msgbuf, 30);
		}

		skb = alloc_skb(DataB3Len + MsgLen, GFP_ATOMIC);
		if (!skb) {
			printk(KERN_ERR "%s: incoming packet dropped\n",
			       card->name);
		} else {
			skb_put_data(skb, card->msgbuf, MsgLen);
			skb_put_data(skb, card->databuf, DataB3Len);
			capi_ctr_handle_message(ctrl, ApplId, skb);
		}
		break;

	case RECEIVE_MESSAGE:

		ApplId = (unsigned) b1_get_word(card->port);
		MsgLen = b1_get_slice(card->port, card->msgbuf);
		skb = alloc_skb(MsgLen, GFP_ATOMIC);

		if (!skb) {
			printk(KERN_ERR "%s: incoming packet dropped\n",
			       card->name);
			spin_unlock_irqrestore(&card->lock, flags);
		} else {
			skb_put_data(skb, card->msgbuf, MsgLen);
			if (CAPIMSG_CMD(skb->data) == CAPI_DATA_B3_CONF)
				capilib_data_b3_conf(&cinfo->ncci_head, ApplId,
						     CAPIMSG_NCCI(skb->data),
						     CAPIMSG_MSGID(skb->data));
			spin_unlock_irqrestore(&card->lock, flags);
			capi_ctr_handle_message(ctrl, ApplId, skb);
		}
		break;

	case RECEIVE_NEW_NCCI:

		ApplId = b1_get_word(card->port);
		NCCI = b1_get_word(card->port);
		WindowSize = b1_get_word(card->port);
		capilib_new_ncci(&cinfo->ncci_head, ApplId, NCCI, WindowSize);
		spin_unlock_irqrestore(&card->lock, flags);
		break;

	case RECEIVE_FREE_NCCI:

		ApplId = b1_get_word(card->port);
		NCCI = b1_get_word(card->port);
		if (NCCI != 0xffffffff)
			capilib_free_ncci(&cinfo->ncci_head, ApplId, NCCI);
		spin_unlock_irqrestore(&card->lock, flags);
		break;

	case RECEIVE_START:
		/* b1_put_byte(card->port, SEND_POLLACK); */
		spin_unlock_irqrestore(&card->lock, flags);
		capi_ctr_resume_output(ctrl);
		break;

	case RECEIVE_STOP:
		spin_unlock_irqrestore(&card->lock, flags);
		capi_ctr_suspend_output(ctrl);
		break;

	case RECEIVE_INIT:

		cinfo->versionlen = b1_get_slice(card->port, cinfo->versionbuf);
		spin_unlock_irqrestore(&card->lock, flags);
		b1_parse_version(cinfo);
		printk(KERN_INFO "%s: %s-card (%s) now active\n",
		       card->name,
		       cinfo->version[VER_CARDTYPE],
		       cinfo->version[VER_DRIVER]);
		capi_ctr_ready(ctrl);
		break;

	case RECEIVE_TASK_READY:
		ApplId = (unsigned) b1_get_word(card->port);
		MsgLen = b1_get_slice(card->port, card->msgbuf);
		spin_unlock_irqrestore(&card->lock, flags);
		card->msgbuf[MsgLen] = 0;
		while (MsgLen > 0
		       && (card->msgbuf[MsgLen - 1] == '\n'
			   || card->msgbuf[MsgLen - 1] == '\r')) {
			card->msgbuf[MsgLen - 1] = 0;
			MsgLen--;
		}
		printk(KERN_INFO "%s: task %d \"%s\" ready.\n",
		       card->name, ApplId, card->msgbuf);
		break;

	case RECEIVE_DEBUGMSG:
		MsgLen = b1_get_slice(card->port, card->msgbuf);
		spin_unlock_irqrestore(&card->lock, flags);
		card->msgbuf[MsgLen] = 0;
		while (MsgLen > 0
		       && (card->msgbuf[MsgLen - 1] == '\n'
			   || card->msgbuf[MsgLen - 1] == '\r')) {
			card->msgbuf[MsgLen - 1] = 0;
			MsgLen--;
		}
		printk(KERN_INFO "%s: DEBUG: %s\n", card->name, card->msgbuf);
		break;

	case 0xff:
		spin_unlock_irqrestore(&card->lock, flags);
		printk(KERN_ERR "%s: card removed ?\n", card->name);
		return IRQ_NONE;
	default:
		spin_unlock_irqrestore(&card->lock, flags);
		printk(KERN_ERR "%s: b1_interrupt: 0x%x ???\n",
		       card->name, b1cmd);
		return IRQ_HANDLED;
	}
	return IRQ_HANDLED;
}

/* ------------------------------------------------------------- */
int b1_proc_show(struct seq_file *m, void *v)
{
	struct capi_ctr *ctrl = m->private;
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	u8 flag;
	char *s;

	seq_printf(m, "%-16s %s\n", "name", card->name);
	seq_printf(m, "%-16s 0x%x\n", "io", card->port);
	seq_printf(m, "%-16s %d\n", "irq", card->irq);
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
	seq_printf(m, "%-16s %s\n", "type", s);
	if (card->cardtype == avm_t1isa)
		seq_printf(m, "%-16s %d\n", "cardnr", card->cardnr);

	s = cinfo->version[VER_DRIVER];
	if (s)
		seq_printf(m, "%-16s %s\n", "ver_driver", s);

	s = cinfo->version[VER_CARDTYPE];
	if (s)
		seq_printf(m, "%-16s %s\n", "ver_cardtype", s);

	s = cinfo->version[VER_SERIAL];
	if (s)
		seq_printf(m, "%-16s %s\n", "ver_serial", s);

	if (card->cardtype != avm_m1) {
		flag = ((u8 *)(ctrl->profile.manu))[3];
		if (flag)
			seq_printf(m, "%-16s%s%s%s%s%s%s%s\n",
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
			seq_printf(m, "%-16s%s%s%s%s\n",
				   "linetype",
				   (flag & 0x01) ? " point to point" : "",
				   (flag & 0x02) ? " point to multipoint" : "",
				   (flag & 0x08) ? " leased line without D-channel" : "",
				   (flag & 0x04) ? " leased line with D-channel" : ""
				);
	}
	seq_printf(m, "%-16s %s\n", "cardname", cinfo->cardname);

	return 0;
}
EXPORT_SYMBOL(b1_proc_show);

/* ------------------------------------------------------------- */

#ifdef CONFIG_PCI

avmcard_dmainfo *
avmcard_dma_alloc(char *name, struct pci_dev *pdev, long rsize, long ssize)
{
	avmcard_dmainfo *p;
	void *buf;

	p = kzalloc(sizeof(avmcard_dmainfo), GFP_KERNEL);
	if (!p) {
		printk(KERN_WARNING "%s: no memory.\n", name);
		goto err;
	}

	p->recvbuf.size = rsize;
	buf = pci_alloc_consistent(pdev, rsize, &p->recvbuf.dmaaddr);
	if (!buf) {
		printk(KERN_WARNING "%s: allocation of receive dma buffer failed.\n", name);
		goto err_kfree;
	}
	p->recvbuf.dmabuf = buf;

	p->sendbuf.size = ssize;
	buf = pci_alloc_consistent(pdev, ssize, &p->sendbuf.dmaaddr);
	if (!buf) {
		printk(KERN_WARNING "%s: allocation of send dma buffer failed.\n", name);
		goto err_free_consistent;
	}

	p->sendbuf.dmabuf = buf;
	skb_queue_head_init(&p->send_queue);

	return p;

err_free_consistent:
	pci_free_consistent(p->pcidev, p->recvbuf.size,
			    p->recvbuf.dmabuf, p->recvbuf.dmaaddr);
err_kfree:
	kfree(p);
err:
	return NULL;
}

void avmcard_dma_free(avmcard_dmainfo *p)
{
	pci_free_consistent(p->pcidev, p->recvbuf.size,
			    p->recvbuf.dmabuf, p->recvbuf.dmaaddr);
	pci_free_consistent(p->pcidev, p->sendbuf.size,
			    p->sendbuf.dmabuf, p->sendbuf.dmaaddr);
	skb_queue_purge(&p->send_queue);
	kfree(p);
}

EXPORT_SYMBOL(avmcard_dma_alloc);
EXPORT_SYMBOL(avmcard_dma_free);

#endif

EXPORT_SYMBOL(b1_irq_table);

EXPORT_SYMBOL(b1_alloc_card);
EXPORT_SYMBOL(b1_free_card);
EXPORT_SYMBOL(b1_detect);
EXPORT_SYMBOL(b1_getrevision);
EXPORT_SYMBOL(b1_load_t4file);
EXPORT_SYMBOL(b1_load_config);
EXPORT_SYMBOL(b1_loaded);
EXPORT_SYMBOL(b1_load_firmware);
EXPORT_SYMBOL(b1_reset_ctr);
EXPORT_SYMBOL(b1_register_appl);
EXPORT_SYMBOL(b1_release_appl);
EXPORT_SYMBOL(b1_send_message);

EXPORT_SYMBOL(b1_parse_version);
EXPORT_SYMBOL(b1_interrupt);

static int __init b1_init(void)
{
	char *p;
	char rev[32];

	p = strchr(revision, ':');
	if (p && p[1]) {
		strlcpy(rev, p + 2, 32);
		p = strchr(rev, '$');
		if (p && p > rev)
			*(p - 1) = 0;
	} else {
		strcpy(rev, "1.0");
	}
	printk(KERN_INFO "b1: revision %s\n", rev);

	return 0;
}

static void __exit b1_exit(void)
{
}

module_init(b1_init);
module_exit(b1_exit);
