/* $Id: t1isa.c,v 1.1.2.3 2004/02/10 01:07:12 keil Exp $
 * 
 * Module for AVM T1 HEMA-card.
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
#include <linux/capi.h>
#include <linux/netdevice.h>
#include <linux/kernelcapi.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <linux/isdn/capicmd.h>
#include <linux/isdn/capiutil.h>
#include <linux/isdn/capilli.h>
#include "avmcard.h"

/* ------------------------------------------------------------- */

static char *revision = "$Revision: 1.1.2.3 $";

/* ------------------------------------------------------------- */

MODULE_DESCRIPTION("CAPI4Linux: Driver for AVM T1 HEMA ISA card");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------- */

static int hema_irq_table[16] =
{0,
 0,
 0,
 0x80,				/* irq 3 */
 0,
 0x90,				/* irq 5 */
 0,
 0xA0,				/* irq 7 */
 0,
 0xB0,				/* irq 9 */
 0xC0,				/* irq 10 */
 0xD0,				/* irq 11 */
 0xE0,				/* irq 12 */
 0,
 0,
 0xF0,				/* irq 15 */
};

static int t1_detectandinit(unsigned int base, unsigned irq, int cardnr)
{
	unsigned char cregs[8];
	unsigned char reverse_cardnr;
	unsigned char dummy;
	int i;

	reverse_cardnr =   ((cardnr & 0x01) << 3) | ((cardnr & 0x02) << 1)
		         | ((cardnr & 0x04) >> 1) | ((cardnr & 0x08) >> 3);
	cregs[0] = (HEMA_VERSION_ID << 4) | (reverse_cardnr & 0xf);
	cregs[1] = 0x00; /* fast & slow link connected to CON1 */
	cregs[2] = 0x05; /* fast link 20MBit, slow link 20 MBit */
	cregs[3] = 0;
	cregs[4] = 0x11; /* zero wait state */
	cregs[5] = hema_irq_table[irq & 0xf];
	cregs[6] = 0;
	cregs[7] = 0;

	/*
	 * no one else should use the ISA bus in this moment,
	 * but no function there to prevent this :-(
	 * save_flags(flags); cli();
	 */

	/* board reset */
	t1outp(base, T1_RESETBOARD, 0xf);
	mdelay(100);
	dummy = t1inp(base, T1_FASTLINK+T1_OUTSTAT); /* first read */

	/* write config */
	dummy = (base >> 4) & 0xff;
	for (i=1;i<=0xf;i++) t1outp(base, i, dummy);
	t1outp(base, HEMA_PAL_ID & 0xf, dummy);
	t1outp(base, HEMA_PAL_ID >> 4, cregs[0]);
	for(i=1;i<7;i++) t1outp(base, 0, cregs[i]);
	t1outp(base, ((base >> 4)) & 0x3, cregs[7]);
	/* restore_flags(flags); */

	mdelay(100);
	t1outp(base, T1_FASTLINK+T1_RESETLINK, 0);
	t1outp(base, T1_SLOWLINK+T1_RESETLINK, 0);
	mdelay(10);
	t1outp(base, T1_FASTLINK+T1_RESETLINK, 1);
	t1outp(base, T1_SLOWLINK+T1_RESETLINK, 1);
	mdelay(100);
	t1outp(base, T1_FASTLINK+T1_RESETLINK, 0);
	t1outp(base, T1_SLOWLINK+T1_RESETLINK, 0);
	mdelay(10);
	t1outp(base, T1_FASTLINK+T1_ANALYSE, 0);
	mdelay(5);
	t1outp(base, T1_SLOWLINK+T1_ANALYSE, 0);

	if (t1inp(base, T1_FASTLINK+T1_OUTSTAT) != 0x1) /* tx empty */
		return 1;
	if (t1inp(base, T1_FASTLINK+T1_INSTAT) != 0x0) /* rx empty */
		return 2;
	if (t1inp(base, T1_FASTLINK+T1_IRQENABLE) != 0x0)
		return 3;
	if ((t1inp(base, T1_FASTLINK+T1_FIFOSTAT) & 0xf0) != 0x70)
		return 4;
	if ((t1inp(base, T1_FASTLINK+T1_IRQMASTER) & 0x0e) != 0)
		return 5;
	if ((t1inp(base, T1_FASTLINK+T1_IDENT) & 0x7d) != 1)
		return 6;
	if (t1inp(base, T1_SLOWLINK+T1_OUTSTAT) != 0x1) /* tx empty */
		return 7;
	if ((t1inp(base, T1_SLOWLINK+T1_IRQMASTER) & 0x0e) != 0)
		return 8;
	if ((t1inp(base, T1_SLOWLINK+T1_IDENT) & 0x7d) != 0)
		return 9;
        return 0;
}

static irqreturn_t t1isa_interrupt(int interrupt, void *devptr, struct pt_regs *regs)
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

	while (b1_rx_full(card->port)) {

		b1cmd = b1_get_byte(card->port);

		switch (b1cmd) {

		case RECEIVE_DATA_B3_IND:

			ApplId = (unsigned) b1_get_word(card->port);
			MsgLen = t1_get_slice(card->port, card->msgbuf);
			DataB3Len = t1_get_slice(card->port, card->databuf);
			spin_unlock_irqrestore(&card->lock, flags);

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

			ApplId = (unsigned) b1_get_word(card->port);
			MsgLen = t1_get_slice(card->port, card->msgbuf);
			spin_unlock_irqrestore(&card->lock, flags);
			if (!(skb = alloc_skb(MsgLen, GFP_ATOMIC))) {
				printk(KERN_ERR "%s: incoming packet dropped\n",
						card->name);
			} else {
				memcpy(skb_put(skb, MsgLen), card->msgbuf, MsgLen);
				if (CAPIMSG_CMD(skb->data) == CAPI_DATA_B3)
					capilib_data_b3_conf(&cinfo->ncci_head, ApplId,
							     CAPIMSG_NCCI(skb->data),
							     CAPIMSG_MSGID(skb->data));

				capi_ctr_handle_message(ctrl, ApplId, skb);
			}
			break;

		case RECEIVE_NEW_NCCI:

			ApplId = b1_get_word(card->port);
			NCCI = b1_get_word(card->port);
			WindowSize = b1_get_word(card->port);
			spin_unlock_irqrestore(&card->lock, flags);

			capilib_new_ncci(&cinfo->ncci_head, ApplId, NCCI, WindowSize);

			break;

		case RECEIVE_FREE_NCCI:

			ApplId = b1_get_word(card->port);
			NCCI = b1_get_word(card->port);
			spin_unlock_irqrestore(&card->lock, flags);

			if (NCCI != 0xffffffff)
				capilib_free_ncci(&cinfo->ncci_head, ApplId, NCCI);

			break;

		case RECEIVE_START:
			b1_put_byte(card->port, SEND_POLLACK);
			spin_unlock_irqrestore(&card->lock, flags);
			capi_ctr_resume_output(ctrl);
			break;

		case RECEIVE_STOP:
			spin_unlock_irqrestore(&card->lock, flags);
			capi_ctr_suspend_output(ctrl);
			break;

		case RECEIVE_INIT:

			cinfo->versionlen = t1_get_slice(card->port, cinfo->versionbuf);
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
			MsgLen = t1_get_slice(card->port, card->msgbuf);
			spin_unlock_irqrestore(&card->lock, flags);
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
			MsgLen = t1_get_slice(card->port, card->msgbuf);
			spin_unlock_irqrestore(&card->lock, flags);
			card->msgbuf[MsgLen] = 0;
			while (    MsgLen > 0
			       && (   card->msgbuf[MsgLen-1] == '\n'
				   || card->msgbuf[MsgLen-1] == '\r')) {
				card->msgbuf[MsgLen-1] = 0;
				MsgLen--;
			}
			printk(KERN_INFO "%s: DEBUG: %s\n", card->name, card->msgbuf);
			break;


		case 0xff:
			spin_unlock_irqrestore(&card->lock, flags);
			printk(KERN_ERR "%s: card reseted ?\n", card->name);
			return IRQ_HANDLED;
		default:
			spin_unlock_irqrestore(&card->lock, flags);
			printk(KERN_ERR "%s: b1_interrupt: 0x%x ???\n",
					card->name, b1cmd);
			return IRQ_NONE;
		}
	}
	return IRQ_HANDLED;
}

/* ------------------------------------------------------------- */

static int t1isa_load_firmware(struct capi_ctr *ctrl, capiloaddata *data)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;
	unsigned long flags;
	int retval;

	t1_disable_irq(port);
	b1_reset(port);

	if ((retval = b1_load_t4file(card, &data->firmware))) {
		b1_reset(port);
		printk(KERN_ERR "%s: failed to load t4file!!\n",
					card->name);
		return retval;
	}

	if (data->configuration.len > 0 && data->configuration.data) {
		if ((retval = b1_load_config(card, &data->configuration))) {
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
	b1_put_word(port, AVM_NCCI_PER_CHANNEL*30);
	b1_put_word(port, ctrl->cnr - 1);
	spin_unlock_irqrestore(&card->lock, flags);

	return 0;
}

static void t1isa_reset_ctr(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;

	t1_disable_irq(port);
	b1_reset(port);
	b1_reset(port);

	memset(cinfo->version, 0, sizeof(cinfo->version));
	capilib_release(&cinfo->ncci_head);
	capi_ctr_reseted(ctrl);
}

static void t1isa_remove(struct pci_dev *pdev)
{
	avmctrl_info *cinfo = pci_get_drvdata(pdev);
	avmcard *card;
	
	if (!cinfo)
		return;

	card = cinfo->card;

	t1_disable_irq(card->port);
	b1_reset(card->port);
	b1_reset(card->port);
	t1_reset(card->port);

	detach_capi_ctr(&cinfo->capi_ctrl);
	free_irq(card->irq, card);
	release_region(card->port, AVMB1_PORTLEN);
	b1_free_card(card);
}

/* ------------------------------------------------------------- */

static u16 t1isa_send_message(struct capi_ctr *ctrl, struct sk_buff *skb);
static char *t1isa_procinfo(struct capi_ctr *ctrl);

static int t1isa_probe(struct pci_dev *pdev, int cardnr)
{
	avmctrl_info *cinfo;
	avmcard *card;
	int retval;

	card = b1_alloc_card(1);
	if (!card) {
		printk(KERN_WARNING "t1isa: no memory.\n");
		retval = -ENOMEM;
		goto err;
	}

	cinfo = card->ctrlinfo;
	card->port = pci_resource_start(pdev, 0);
	card->irq = pdev->irq;
	card->cardtype = avm_t1isa;
	card->cardnr = cardnr;
	sprintf(card->name, "t1isa-%x", card->port);

	if (!(((card->port & 0x7) == 0) && ((card->port & 0x30) != 0x30))) {
		printk(KERN_WARNING "t1isa: invalid port 0x%x.\n", card->port);
		retval = -EINVAL;
		goto err_free;
        }
	if (hema_irq_table[card->irq & 0xf] == 0) {
		printk(KERN_WARNING "t1isa: irq %d not valid.\n", card->irq);
		retval = -EINVAL;
		goto err_free;
	}
	if (!request_region(card->port, AVMB1_PORTLEN, card->name)) {
		printk(KERN_INFO "t1isa: ports 0x%03x-0x%03x in use.\n",
		       card->port, card->port + AVMB1_PORTLEN);
		retval = -EBUSY;
		goto err_free;
	}
	retval = request_irq(card->irq, t1isa_interrupt, 0, card->name, card);
	if (retval) {
		printk(KERN_INFO "t1isa: unable to get IRQ %d.\n", card->irq);
		retval = -EBUSY;
		goto err_release_region;
	}

        if ((retval = t1_detectandinit(card->port, card->irq, card->cardnr)) != 0) {
		printk(KERN_INFO "t1isa: NO card at 0x%x (%d)\n",
		       card->port, retval);
		retval = -ENODEV;
		goto err_free_irq;
	}
	t1_disable_irq(card->port);
	b1_reset(card->port);

	cinfo->capi_ctrl.owner = THIS_MODULE;
	cinfo->capi_ctrl.driver_name   = "t1isa";
	cinfo->capi_ctrl.driverdata    = cinfo;
	cinfo->capi_ctrl.register_appl = b1_register_appl;
	cinfo->capi_ctrl.release_appl  = b1_release_appl;
	cinfo->capi_ctrl.send_message  = t1isa_send_message;
	cinfo->capi_ctrl.load_firmware = t1isa_load_firmware;
	cinfo->capi_ctrl.reset_ctr     = t1isa_reset_ctr;
	cinfo->capi_ctrl.procinfo      = t1isa_procinfo;
	cinfo->capi_ctrl.ctr_read_proc = b1ctl_read_proc;
	strcpy(cinfo->capi_ctrl.name, card->name);

	retval = attach_capi_ctr(&cinfo->capi_ctrl);
	if (retval) {
		printk(KERN_INFO "t1isa: attach controller failed.\n");
		goto err_free_irq;
	}

	printk(KERN_INFO "t1isa: AVM T1 ISA at i/o %#x, irq %d, card %d\n",
	       card->port, card->irq, card->cardnr);

	pci_set_drvdata(pdev, cinfo);
	return 0;

 err_free_irq:
	free_irq(card->irq, card);
 err_release_region:
	release_region(card->port, AVMB1_PORTLEN);
 err_free:
	b1_free_card(card);
 err:
	return retval;
}

static u16 t1isa_send_message(struct capi_ctr *ctrl, struct sk_buff *skb)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;
	unsigned long flags;
	u16 len = CAPIMSG_LEN(skb->data);
	u8 cmd = CAPIMSG_COMMAND(skb->data);
	u8 subcmd = CAPIMSG_SUBCOMMAND(skb->data);
	u16 dlen, retval;

	if (CAPICMD(cmd, subcmd) == CAPI_DATA_B3_REQ) {
		retval = capilib_data_b3_req(&cinfo->ncci_head,
					     CAPIMSG_APPID(skb->data),
					     CAPIMSG_NCCI(skb->data),
					     CAPIMSG_MSGID(skb->data));
		if (retval != CAPI_NOERROR) 
			return retval;

		dlen = CAPIMSG_DATALEN(skb->data);

		spin_lock_irqsave(&card->lock, flags);
		b1_put_byte(port, SEND_DATA_B3_REQ);
		t1_put_slice(port, skb->data, len);
		t1_put_slice(port, skb->data + len, dlen);
		spin_unlock_irqrestore(&card->lock, flags);
	} else {

		spin_lock_irqsave(&card->lock, flags);
		b1_put_byte(port, SEND_MESSAGE);
		t1_put_slice(port, skb->data, len);
		spin_unlock_irqrestore(&card->lock, flags);
	}

	dev_kfree_skb_any(skb);
	return CAPI_NOERROR;
}
/* ------------------------------------------------------------- */

static char *t1isa_procinfo(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);

	if (!cinfo)
		return "";
	sprintf(cinfo->infobuf, "%s %s 0x%x %d %d",
		cinfo->cardname[0] ? cinfo->cardname : "-",
		cinfo->version[VER_DRIVER] ? cinfo->version[VER_DRIVER] : "-",
		cinfo->card ? cinfo->card->port : 0x0,
		cinfo->card ? cinfo->card->irq : 0,
		cinfo->card ? cinfo->card->cardnr : 0
		);
	return cinfo->infobuf;
}


/* ------------------------------------------------------------- */

#define MAX_CARDS 4
static struct pci_dev isa_dev[MAX_CARDS];
static int io[MAX_CARDS];
static int irq[MAX_CARDS];
static int cardnr[MAX_CARDS];

module_param_array(io, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
module_param_array(cardnr, int, NULL, 0);
MODULE_PARM_DESC(io, "I/O base address(es)");
MODULE_PARM_DESC(irq, "IRQ number(s) (assigned)");
MODULE_PARM_DESC(cardnr, "Card number(s) (as jumpered)");

static int t1isa_add_card(struct capi_driver *driver, capicardparams *data)
{
	int i;

	for (i = 0; i < MAX_CARDS; i++) {
		if (isa_dev[i].resource[0].start)
			continue;

		isa_dev[i].resource[0].start = data->port;
		isa_dev[i].irq = data->irq;

		if (t1isa_probe(&isa_dev[i], data->cardnr) == 0)
			return 0;
	}
	return -ENODEV;
}

static struct capi_driver capi_driver_t1isa = {
	.name		= "t1isa",
	.revision	= "1.0",
	.add_card       = t1isa_add_card,
};

static int __init t1isa_init(void)
{
	char rev[32];
	char *p;
	int i;

	if ((p = strchr(revision, ':')) != 0 && p[1]) {
		strlcpy(rev, p + 2, 32);
		if ((p = strchr(rev, '$')) != 0 && p > rev)
		   *(p-1) = 0;
	} else
		strcpy(rev, "1.0");

	for (i = 0; i < MAX_CARDS; i++) {
		if (!io[i])
			break;

		isa_dev[i].resource[0].start = io[i];
		isa_dev[i].irq = irq[i];

		if (t1isa_probe(&isa_dev[i], cardnr[i]) != 0)
			return -ENODEV;
	}

	strlcpy(capi_driver_t1isa.revision, rev, 32);
	register_capi_driver(&capi_driver_t1isa);
	printk(KERN_INFO "t1isa: revision %s\n", rev);

	return 0;
}

static void __exit t1isa_exit(void)
{
	int i;

	for (i = 0; i < MAX_CARDS; i++) {
		if (!io[i])
			break;

		t1isa_remove(&isa_dev[i]);
	}
}

module_init(t1isa_init);
module_exit(t1isa_exit);
