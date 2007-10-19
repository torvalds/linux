/* $Id: act2000_isa.c,v 1.11.6.3 2001/09/23 22:24:32 kai Exp $
 *
 * ISDN lowlevel-module for the IBM ISDN-S0 Active 2000 (ISA-Version).
 *
 * Author       Fritz Elfert
 * Copyright    by Fritz Elfert      <fritz@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Thanks to Friedemann Baitinger and IBM Germany
 *
 */

#include "act2000.h"
#include "act2000_isa.h"
#include "capi.h"

/*
 * Reset Controller, then try to read the Card's signature.
 + Return:
 *   1 = Signature found.
 *   0 = Signature not found.
 */
static int
act2000_isa_reset(unsigned short portbase)
{
        unsigned char reg;
        int i;
        int found;
        int serial = 0;

        found = 0;
        if ((reg = inb(portbase + ISA_COR)) != 0xff) {
                outb(reg | ISA_COR_RESET, portbase + ISA_COR);
                mdelay(10);
                outb(reg, portbase + ISA_COR);
                mdelay(10);

                for (i = 0; i < 16; i++) {
                        if (inb(portbase + ISA_ISR) & ISA_ISR_SERIAL)
                                serial |= 0x10000;
                        serial >>= 1;
                }
                if (serial == ISA_SER_ID)
                        found++;
        }
        return found;
}

int
act2000_isa_detect(unsigned short portbase)
{
        int ret = 0;

	if (request_region(portbase, ACT2000_PORTLEN, "act2000isa")) {
                ret = act2000_isa_reset(portbase);
		release_region(portbase, ISA_REGION);
	}
        return ret;
}

static irqreturn_t
act2000_isa_interrupt(int irq, void *dev_id)
{
        act2000_card *card = dev_id;
        u_char istatus;

        istatus = (inb(ISA_PORT_ISR) & 0x07);
        if (istatus & ISA_ISR_OUT) {
                /* RX fifo has data */
		istatus &= ISA_ISR_OUT_MASK;
		outb(0, ISA_PORT_SIS);
		act2000_isa_receive(card);
		outb(ISA_SIS_INT, ISA_PORT_SIS);
        }
        if (istatus & ISA_ISR_ERR) {
                /* Error Interrupt */
		istatus &= ISA_ISR_ERR_MASK;
                printk(KERN_WARNING "act2000: errIRQ\n");
        }
	if (istatus)
		printk(KERN_DEBUG "act2000: ?IRQ %d %02x\n", irq, istatus);
	return IRQ_HANDLED;
}

static void
act2000_isa_select_irq(act2000_card * card)
{
	unsigned char reg;

	reg = (inb(ISA_PORT_COR) & ~ISA_COR_IRQOFF) | ISA_COR_PERR;
	switch (card->irq) {
		case 3:
			reg = ISA_COR_IRQ03;
			break;
		case 5:
			reg = ISA_COR_IRQ05;
			break;
		case 7:
			reg = ISA_COR_IRQ07;
			break;
		case 10:
			reg = ISA_COR_IRQ10;
			break;
		case 11:
			reg = ISA_COR_IRQ11;
			break;
		case 12:
			reg = ISA_COR_IRQ12;
			break;
		case 15:
			reg = ISA_COR_IRQ15;
			break;
	}
	outb(reg, ISA_PORT_COR);
}

static void
act2000_isa_enable_irq(act2000_card * card)
{
	act2000_isa_select_irq(card);
	/* Enable READ irq */
	outb(ISA_SIS_INT, ISA_PORT_SIS);
}

/*
 * Install interrupt handler, enable irq on card.
 * If irq is -1, choose next free irq, else irq is given explicitly.
 */
int
act2000_isa_config_irq(act2000_card * card, short irq)
{
        if (card->flags & ACT2000_FLAGS_IVALID) {
                free_irq(card->irq, card);
        }
        card->flags &= ~ACT2000_FLAGS_IVALID;
        outb(ISA_COR_IRQOFF, ISA_PORT_COR);
        if (!irq)
                return 0;

	if (!request_irq(irq, &act2000_isa_interrupt, 0, card->regname, card)) {
		card->irq = irq;
		card->flags |= ACT2000_FLAGS_IVALID;
                printk(KERN_WARNING
                       "act2000: Could not request irq %d\n",irq);
                return -EBUSY;
        } else {
		act2000_isa_select_irq(card);
                /* Disable READ and WRITE irq */
                outb(0, ISA_PORT_SIS);
                outb(0, ISA_PORT_SOS);
        }
        return 0;
}

int
act2000_isa_config_port(act2000_card * card, unsigned short portbase)
{
        if (card->flags & ACT2000_FLAGS_PVALID) {
                release_region(card->port, ISA_REGION);
                card->flags &= ~ACT2000_FLAGS_PVALID;
        }
	if (request_region(portbase, ACT2000_PORTLEN, card->regname) == NULL)
		return -EBUSY;
	else {
                card->port = portbase;
                card->flags |= ACT2000_FLAGS_PVALID;
                return 0;
        }
}

/*
 * Release ressources, used by an adaptor.
 */
void
act2000_isa_release(act2000_card * card)
{
        unsigned long flags;

        spin_lock_irqsave(&card->lock, flags);
        if (card->flags & ACT2000_FLAGS_IVALID)
                free_irq(card->irq, card);

        card->flags &= ~ACT2000_FLAGS_IVALID;
        if (card->flags & ACT2000_FLAGS_PVALID)
                release_region(card->port, ISA_REGION);
        card->flags &= ~ACT2000_FLAGS_PVALID;
        spin_unlock_irqrestore(&card->lock, flags);
}

static int
act2000_isa_writeb(act2000_card * card, u_char data)
{
        u_char timeout = 40;

        while (timeout) {
                if (inb(ISA_PORT_SOS) & ISA_SOS_READY) {
                        outb(data, ISA_PORT_SDO);
                        return 0;
                } else {
                        timeout--;
                        udelay(10);
                }
        }
        return 1;
}

static int
act2000_isa_readb(act2000_card * card, u_char * data)
{
        u_char timeout = 40;

        while (timeout) {
                if (inb(ISA_PORT_SIS) & ISA_SIS_READY) {
                        *data = inb(ISA_PORT_SDI);
                        return 0;
                } else {
                        timeout--;
                        udelay(10);
                }
        }
        return 1;
}

void
act2000_isa_receive(act2000_card *card)
{
	u_char c;

        if (test_and_set_bit(ACT2000_LOCK_RX, (void *) &card->ilock) != 0)
		return;
	while (!act2000_isa_readb(card, &c)) {
		if (card->idat.isa.rcvidx < 8) {
                        card->idat.isa.rcvhdr[card->idat.isa.rcvidx++] = c;
			if (card->idat.isa.rcvidx == 8) {
				int valid = actcapi_chkhdr(card, (actcapi_msghdr *)&card->idat.isa.rcvhdr);

				if (valid) {
					card->idat.isa.rcvlen = ((actcapi_msghdr *)&card->idat.isa.rcvhdr)->len;
					card->idat.isa.rcvskb = dev_alloc_skb(card->idat.isa.rcvlen);
					if (card->idat.isa.rcvskb == NULL) {
						card->idat.isa.rcvignore = 1;
						printk(KERN_WARNING
						       "act2000_isa_receive: no memory\n");
						test_and_clear_bit(ACT2000_LOCK_RX, (void *) &card->ilock);
						return;
					}
					memcpy(skb_put(card->idat.isa.rcvskb, 8), card->idat.isa.rcvhdr, 8);
					card->idat.isa.rcvptr = skb_put(card->idat.isa.rcvskb, card->idat.isa.rcvlen - 8);
				} else {
					card->idat.isa.rcvidx = 0;
					printk(KERN_WARNING
					       "act2000_isa_receive: Invalid CAPI msg\n");
					{
						int i; __u8 *p; __u8 *c; __u8 tmp[30];
						for (i = 0, p = (__u8 *)&card->idat.isa.rcvhdr, c = tmp; i < 8; i++)
							c += sprintf(c, "%02x ", *(p++));
						printk(KERN_WARNING "act2000_isa_receive: %s\n", tmp);
					}
				}
			}
		} else {
			if (!card->idat.isa.rcvignore)
				*card->idat.isa.rcvptr++ = c;
			if (++card->idat.isa.rcvidx >= card->idat.isa.rcvlen) {
				if (!card->idat.isa.rcvignore) {
					skb_queue_tail(&card->rcvq, card->idat.isa.rcvskb);
					act2000_schedule_rx(card);
				}
				card->idat.isa.rcvidx = 0;
				card->idat.isa.rcvlen = 8;
				card->idat.isa.rcvignore = 0;
				card->idat.isa.rcvskb = NULL;
				card->idat.isa.rcvptr = card->idat.isa.rcvhdr;
			}
		}
	}
	if (!(card->flags & ACT2000_FLAGS_IVALID)) {
		/* In polling mode, schedule myself */
		if ((card->idat.isa.rcvidx) &&
		    (card->idat.isa.rcvignore ||
		     (card->idat.isa.rcvidx < card->idat.isa.rcvlen)))
			act2000_schedule_poll(card);
	}
	test_and_clear_bit(ACT2000_LOCK_RX, (void *) &card->ilock);
}

void
act2000_isa_send(act2000_card * card)
{
	unsigned long flags;
	struct sk_buff *skb;
	actcapi_msg *msg;
	int l;

        if (test_and_set_bit(ACT2000_LOCK_TX, (void *) &card->ilock) != 0)
		return;
	while (1) {
		spin_lock_irqsave(&card->lock, flags);
		if (!(card->sbuf)) {
			if ((card->sbuf = skb_dequeue(&card->sndq))) {
				card->ack_msg = card->sbuf->data;
				msg = (actcapi_msg *)card->sbuf->data;
				if ((msg->hdr.cmd.cmd == 0x86) &&
				    (msg->hdr.cmd.subcmd == 0)   ) {
					/* Save flags in message */
					card->need_b3ack = msg->msg.data_b3_req.flags;
					msg->msg.data_b3_req.flags = 0;
				}
			}
		}
		spin_unlock_irqrestore(&card->lock, flags);
		if (!(card->sbuf)) {
			/* No more data to send */
			test_and_clear_bit(ACT2000_LOCK_TX, (void *) &card->ilock);
			return;
		}
		skb = card->sbuf;
		l = 0;
		while (skb->len) {
			if (act2000_isa_writeb(card, *(skb->data))) {
				/* Fifo is full, but more data to send */
				test_and_clear_bit(ACT2000_LOCK_TX, (void *) &card->ilock);
				/* Schedule myself */
				act2000_schedule_tx(card);
				return;
			}
			skb_pull(skb, 1);
			l++;
		}
		msg = (actcapi_msg *)card->ack_msg;
		if ((msg->hdr.cmd.cmd == 0x86) &&
		    (msg->hdr.cmd.subcmd == 0)   ) {
			/*
			 * If it's user data, reset data-ptr
			 * and put skb into ackq.
			 */
			skb->data = card->ack_msg;
			/* Restore flags in message */
			msg->msg.data_b3_req.flags = card->need_b3ack;
			skb_queue_tail(&card->ackq, skb);
		} else
			dev_kfree_skb(skb);
		card->sbuf = NULL;
	}
}

/*
 * Get firmware ID, check for 'ISDN' signature.
 */
static int
act2000_isa_getid(act2000_card * card)
{

        act2000_fwid fid;
        u_char *p = (u_char *) & fid;
        int count = 0;

        while (1) {
                if (count > 510)
                        return -EPROTO;
                if (act2000_isa_readb(card, p++))
                        break;
                count++;
        }
        if (count <= 20) {
                printk(KERN_WARNING "act2000: No Firmware-ID!\n");
                return -ETIME;
        }
        *p = '\0';
        fid.revlen[0] = '\0';
        if (strcmp(fid.isdn, "ISDN")) {
                printk(KERN_WARNING "act2000: Wrong Firmware-ID!\n");
                return -EPROTO;
        }
	if ((p = strchr(fid.revision, '\n')))
		*p = '\0';
        printk(KERN_INFO "act2000: Firmware-ID: %s\n", fid.revision);
	if (card->flags & ACT2000_FLAGS_IVALID) {
		printk(KERN_DEBUG "Enabling Interrupts ...\n");
		act2000_isa_enable_irq(card);
	}
        return 0;
}

/*
 * Download microcode into card, check Firmware signature.
 */
int
act2000_isa_download(act2000_card * card, act2000_ddef __user * cb)
{
        unsigned int length;
        int l;
        int c;
        long timeout;
        u_char *b;
        u_char __user *p;
        u_char *buf;
        act2000_ddef cblock;

        if (!act2000_isa_reset(card->port))
                return -ENXIO;
        msleep_interruptible(500);
        if (copy_from_user(&cblock, cb, sizeof(cblock)))
        	return -EFAULT;
        length = cblock.length;
        p = cblock.buffer;
        if (!access_ok(VERIFY_READ, p, length))
                return -EFAULT;
        buf = kmalloc(1024, GFP_KERNEL);
        if (!buf)
                return -ENOMEM;
        timeout = 0;
        while (length) {
                l = (length > 1024) ? 1024 : length;
                c = 0;
                b = buf;
                if (copy_from_user(buf, p, l)) {
                        kfree(buf);
                        return -EFAULT;
                }
                while (c < l) {
                        if (act2000_isa_writeb(card, *b++)) {
                                printk(KERN_WARNING
                                       "act2000: loader timed out"
                                       " len=%d c=%d\n", length, c);
                                kfree(buf);
                                return -ETIME;
                        }
                        c++;
                }
                length -= l;
                p += l;
        }
        kfree(buf);
        msleep_interruptible(500);
        return (act2000_isa_getid(card));
}
