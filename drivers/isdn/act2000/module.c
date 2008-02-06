/* $Id: module.c,v 1.14.6.4 2001/09/23 22:24:32 kai Exp $
 *
 * ISDN lowlevel-module for the IBM ISDN-S0 Active 2000.
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
#include <linux/module.h>
#include <linux/init.h>

static unsigned short act2000_isa_ports[] =
{
        0x0200, 0x0240, 0x0280, 0x02c0, 0x0300, 0x0340, 0x0380,
        0xcfe0, 0xcfa0, 0xcf60, 0xcf20, 0xcee0, 0xcea0, 0xce60,
};
#define ISA_NRPORTS (sizeof(act2000_isa_ports)/sizeof(unsigned short))

static act2000_card *cards = (act2000_card *) NULL;

/* Parameters to be set by insmod */
static int   act_bus  =  0;
static int   act_port = -1;  /* -1 = Autoprobe  */
static int   act_irq  = -1;
static char *act_id   = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

MODULE_DESCRIPTION(       "ISDN4Linux: Driver for IBM Active 2000 ISDN card");
MODULE_AUTHOR(            "Fritz Elfert");
MODULE_LICENSE(           "GPL");
MODULE_PARM_DESC(act_bus, "BusType of first card, 1=ISA, 2=MCA, 3=PCMCIA, currently only ISA");
MODULE_PARM_DESC(membase, "Base port address of first card");
MODULE_PARM_DESC(act_irq, "IRQ of first card");
MODULE_PARM_DESC(act_id,  "ID-String of first card");
module_param(act_bus,  int, 0);
module_param(act_port, int, 0);
module_param(act_irq, int, 0);
module_param(act_id, charp, 0);

static int act2000_addcard(int, int, int, char *);

static act2000_chan *
find_channel(act2000_card *card, int channel)
{
	if ((channel >= 0) && (channel < ACT2000_BCH))
        	return &(card->bch[channel]);
	printk(KERN_WARNING "act2000: Invalid channel %d\n", channel);
	return NULL;
}

/*
 * Free MSN list
 */
static void
act2000_clear_msn(act2000_card *card)
{
	struct msn_entry *p = card->msn_list;
	struct msn_entry *q;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	card->msn_list = NULL;
	spin_unlock_irqrestore(&card->lock, flags);
	while (p) {
		q  = p->next;
		kfree(p);
		p = q;
	}
}

/*
 * Find an MSN entry in the list.
 * If ia5 != 0, return IA5-encoded EAZ, else
 * return a bitmask with corresponding bit set.
 */
static __u16
act2000_find_msn(act2000_card *card, char *msn, int ia5)
{
        struct msn_entry *p = card->msn_list;
	__u8 eaz = '0';

	while (p) {
		if (!strcmp(p->msn, msn)) {
			eaz = p->eaz;
			break;
		}
		p = p->next;
	}
	if (!ia5)
		return (1 << (eaz - '0'));
	else
		return eaz;
}

/*
 * Find an EAZ entry in the list.
 * return a string with corresponding msn.
 */
char *
act2000_find_eaz(act2000_card *card, char eaz)
{
        struct msn_entry *p = card->msn_list;

	while (p) {
		if (p->eaz == eaz)
			return(p->msn);
		p = p->next;
	}
	return("\0");
}

/*
 * Add or delete an MSN to the MSN list
 *
 * First character of msneaz is EAZ, rest is MSN.
 * If length of eazmsn is 1, delete that entry.
 */
static int
act2000_set_msn(act2000_card *card, char *eazmsn)
{
        struct msn_entry *p = card->msn_list;
        struct msn_entry *q = NULL;
	unsigned long flags;
	int i;
	
	if (!strlen(eazmsn))
		return 0;
	if (strlen(eazmsn) > 16)
		return -EINVAL;
	for (i = 0; i < strlen(eazmsn); i++)
		if (!isdigit(eazmsn[i]))
			return -EINVAL;
        if (strlen(eazmsn) == 1) {
		/* Delete a single MSN */
		while (p) {
			if (p->eaz == eazmsn[0]) {
				spin_lock_irqsave(&card->lock, flags);
				if (q)
					q->next = p->next;
				else
					card->msn_list = p->next;
				spin_unlock_irqrestore(&card->lock, flags);
				kfree(p);
				printk(KERN_DEBUG
				       "Mapping for EAZ %c deleted\n",
				       eazmsn[0]);
				return 0;
			}
			q = p;
			p = p->next;
		}
		return 0;
        }
	/* Add a single MSN */
	while (p) {
		/* Found in list, replace MSN */
		if (p->eaz == eazmsn[0]) {
			spin_lock_irqsave(&card->lock, flags);
			strcpy(p->msn, &eazmsn[1]);
			spin_unlock_irqrestore(&card->lock, flags);
			printk(KERN_DEBUG
			       "Mapping for EAZ %c changed to %s\n",
			       eazmsn[0],
			       &eazmsn[1]);
			return 0;
		}
		p = p->next;
	}
	/* Not found in list, add new entry */
	p = kmalloc(sizeof(msn_entry), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->eaz = eazmsn[0];
	strcpy(p->msn, &eazmsn[1]);
	p->next = card->msn_list;
	spin_lock_irqsave(&card->lock, flags);
	card->msn_list = p;
	spin_unlock_irqrestore(&card->lock, flags);
	printk(KERN_DEBUG
	       "Mapping %c -> %s added\n",
	       eazmsn[0],
	       &eazmsn[1]);
	return 0;
}

static void
act2000_transmit(struct work_struct *work)
{
	struct act2000_card *card =
		container_of(work, struct act2000_card, snd_tq);

	switch (card->bus) {
		case ACT2000_BUS_ISA:
			act2000_isa_send(card);
			break;
		case ACT2000_BUS_PCMCIA:
		case ACT2000_BUS_MCA:
		default:
			printk(KERN_WARNING
			       "act2000_transmit: Illegal bustype %d\n", card->bus);
	}
}

static void
act2000_receive(struct work_struct *work)
{
	struct act2000_card *card =
		container_of(work, struct act2000_card, poll_tq);

	switch (card->bus) {
		case ACT2000_BUS_ISA:
			act2000_isa_receive(card);
			break;
		case ACT2000_BUS_PCMCIA:
		case ACT2000_BUS_MCA:
		default:
			printk(KERN_WARNING
			       "act2000_receive: Illegal bustype %d\n", card->bus);
	}
}

static void
act2000_poll(unsigned long data)
{
	act2000_card * card = (act2000_card *)data;
	unsigned long flags;

	act2000_receive(&card->poll_tq);
	spin_lock_irqsave(&card->lock, flags);
	mod_timer(&card->ptimer, jiffies+3);
	spin_unlock_irqrestore(&card->lock, flags);
}

static int
act2000_command(act2000_card * card, isdn_ctrl * c)
{
        ulong a;
        act2000_chan *chan;
	act2000_cdef cdef;
	isdn_ctrl cmd;
	char tmp[17];
	int ret;
	unsigned long flags;
	void __user *arg;
 
        switch (c->command) {
		case ISDN_CMD_IOCTL:
			memcpy(&a, c->parm.num, sizeof(ulong));
			arg = (void __user *)a;
			switch (c->arg) {
				case ACT2000_IOCTL_LOADBOOT:
					switch (card->bus) {
						case ACT2000_BUS_ISA:
							ret = act2000_isa_download(card,
									   arg);
							if (!ret) {
								card->flags |= ACT2000_FLAGS_LOADED;
								if (!(card->flags & ACT2000_FLAGS_IVALID)) {
									card->ptimer.expires = jiffies + 3;
									card->ptimer.function = act2000_poll;
									card->ptimer.data = (unsigned long)card;
									add_timer(&card->ptimer);
								}
								actcapi_manufacturer_req_errh(card);
							}
							break;
						default:
							printk(KERN_WARNING
							       "act2000: Illegal BUS type %d\n",
							       card->bus);
							ret = -EIO;
					}
					return ret;
				case ACT2000_IOCTL_SETPROTO:
					card->ptype = a?ISDN_PTYPE_EURO:ISDN_PTYPE_1TR6;
					if (!(card->flags & ACT2000_FLAGS_RUNNING))
						return 0;
					actcapi_manufacturer_req_net(card);
					return 0;
				case ACT2000_IOCTL_SETMSN:
					if (copy_from_user(tmp, arg,
							   sizeof(tmp)))
						return -EFAULT;
					if ((ret = act2000_set_msn(card, tmp)))
						return ret;
					if (card->flags & ACT2000_FLAGS_RUNNING)
						return(actcapi_manufacturer_req_msn(card));
					return 0;
				case ACT2000_IOCTL_ADDCARD:
					if (copy_from_user(&cdef, arg,
							   sizeof(cdef)))
						return -EFAULT;
					if (act2000_addcard(cdef.bus, cdef.port, cdef.irq, cdef.id))
						return -EIO;
					return 0;
				case ACT2000_IOCTL_TEST:
					if (!(card->flags & ACT2000_FLAGS_RUNNING))
						return -ENODEV;
					return 0;
				default:
					return -EINVAL;
			}
			break;
		case ISDN_CMD_DIAL:
			if (!(card->flags & ACT2000_FLAGS_RUNNING))
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			spin_lock_irqsave(&card->lock, flags);
			if (chan->fsm_state != ACT2000_STATE_NULL) {
				spin_unlock_irqrestore(&card->lock, flags);
				printk(KERN_WARNING "Dial on channel with state %d\n",
					chan->fsm_state);
				return -EBUSY;
			}
			if (card->ptype == ISDN_PTYPE_EURO)
				tmp[0] = act2000_find_msn(card, c->parm.setup.eazmsn, 1);
			else
				tmp[0] = c->parm.setup.eazmsn[0];
			chan->fsm_state = ACT2000_STATE_OCALL;
			chan->callref = 0xffff;
			spin_unlock_irqrestore(&card->lock, flags);
			ret = actcapi_connect_req(card, chan, c->parm.setup.phone,
						  tmp[0], c->parm.setup.si1,
						  c->parm.setup.si2);
			if (ret) {
				cmd.driver = card->myid;
				cmd.command = ISDN_STAT_DHUP;
				cmd.arg &= 0x0f;
				card->interface.statcallb(&cmd);
			}
			return ret;
		case ISDN_CMD_ACCEPTD:
			if (!(card->flags & ACT2000_FLAGS_RUNNING))
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			if (chan->fsm_state == ACT2000_STATE_ICALL)
				actcapi_select_b2_protocol_req(card, chan);
			return 0;
		case ISDN_CMD_ACCEPTB:
			if (!(card->flags & ACT2000_FLAGS_RUNNING))
				return -ENODEV;
			return 0;
		case ISDN_CMD_HANGUP:
			if (!(card->flags & ACT2000_FLAGS_RUNNING))
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			switch (chan->fsm_state) {
				case ACT2000_STATE_ICALL:
				case ACT2000_STATE_BSETUP:
					actcapi_connect_resp(card, chan, 0x15);
					break;
				case ACT2000_STATE_ACTIVE:
					actcapi_disconnect_b3_req(card, chan);
					break;
			}
			return 0;
		case ISDN_CMD_SETEAZ:
			if (!(card->flags & ACT2000_FLAGS_RUNNING))
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			if (strlen(c->parm.num)) {
				if (card->ptype == ISDN_PTYPE_EURO) {
					chan->eazmask = act2000_find_msn(card, c->parm.num, 0);
				}
				if (card->ptype == ISDN_PTYPE_1TR6) {
					int i;
					chan->eazmask = 0;
					for (i = 0; i < strlen(c->parm.num); i++)
						if (isdigit(c->parm.num[i]))
							chan->eazmask |= (1 << (c->parm.num[i] - '0'));
				}
			} else
				chan->eazmask = 0x3ff;
			actcapi_listen_req(card);
			return 0;
		case ISDN_CMD_CLREAZ:
			if (!(card->flags & ACT2000_FLAGS_RUNNING))
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			chan->eazmask = 0;
			actcapi_listen_req(card);
			return 0;
		case ISDN_CMD_SETL2:
			if (!(card->flags & ACT2000_FLAGS_RUNNING))
				return -ENODEV;
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			chan->l2prot = (c->arg >> 8);
			return 0;
		case ISDN_CMD_SETL3:
			if (!(card->flags & ACT2000_FLAGS_RUNNING))
				return -ENODEV;
			if ((c->arg >> 8) != ISDN_PROTO_L3_TRANS) {
				printk(KERN_WARNING "L3 protocol unknown\n");
				return -1;
			}
			if (!(chan = find_channel(card, c->arg & 0x0f)))
				break;
			chan->l3prot = (c->arg >> 8);
			return 0;
        }
	
        return -EINVAL;
}

static int
act2000_sendbuf(act2000_card *card, int channel, int ack, struct sk_buff *skb)
{
        struct sk_buff *xmit_skb;
        int len;
        act2000_chan *chan;
	actcapi_msg *msg;

        if (!(chan = find_channel(card, channel)))
		return -1;
        if (chan->fsm_state != ACT2000_STATE_ACTIVE)
                return -1;
        len = skb->len;
        if ((chan->queued + len) >= ACT2000_MAX_QUEUED)
                return 0;
	if (!len)
		return 0;
	if (skb_headroom(skb) < 19) {
		printk(KERN_WARNING "act2000_sendbuf: Headroom only %d\n",
		       skb_headroom(skb));
		xmit_skb = alloc_skb(len + 19, GFP_ATOMIC);
		if (!xmit_skb) {
			printk(KERN_WARNING "act2000_sendbuf: Out of memory\n");
			return 0;
		}
		skb_reserve(xmit_skb, 19);
		skb_copy_from_linear_data(skb, skb_put(xmit_skb, len), len);
	} else {
		xmit_skb = skb_clone(skb, GFP_ATOMIC);
		if (!xmit_skb) {
			printk(KERN_WARNING "act2000_sendbuf: Out of memory\n");
			return 0;
		}
	}
	dev_kfree_skb(skb);
	msg = (actcapi_msg *)skb_push(xmit_skb, 19);
	msg->hdr.len = 19 + len;
	msg->hdr.applicationID = 1;
	msg->hdr.cmd.cmd = 0x86;
	msg->hdr.cmd.subcmd = 0x00;
	msg->hdr.msgnum = actcapi_nextsmsg(card);
	msg->msg.data_b3_req.datalen = len;
	msg->msg.data_b3_req.blocknr = (msg->hdr.msgnum & 0xff);
	msg->msg.data_b3_req.fakencci = MAKE_NCCI(chan->plci, 0, chan->ncci);
	msg->msg.data_b3_req.flags = ack; /* Will be set to 0 on actual sending */
	actcapi_debug_msg(xmit_skb, 1);
        chan->queued += len;
	skb_queue_tail(&card->sndq, xmit_skb);
	act2000_schedule_tx(card);
        return len;
}


/* Read the Status-replies from the Interface */
static int
act2000_readstatus(u_char __user * buf, int len, act2000_card * card)
{
        int count;
        u_char __user *p;

        for (p = buf, count = 0; count < len; p++, count++) {
                if (card->status_buf_read == card->status_buf_write)
                        return count;
		put_user(*card->status_buf_read++, p);
                if (card->status_buf_read > card->status_buf_end)
                        card->status_buf_read = card->status_buf;
        }
        return count;
}

/*
 * Find card with given driverId
 */
static inline act2000_card *
act2000_findcard(int driverid)
{
        act2000_card *p = cards;

        while (p) {
                if (p->myid == driverid)
                        return p;
                p = p->next;
        }
        return (act2000_card *) 0;
}

/*
 * Wrapper functions for interface to linklevel
 */
static int
if_command(isdn_ctrl * c)
{
        act2000_card *card = act2000_findcard(c->driver);

        if (card)
                return (act2000_command(card, c));
        printk(KERN_ERR
             "act2000: if_command %d called with invalid driverId %d!\n",
               c->command, c->driver);
        return -ENODEV;
}

static int
if_writecmd(const u_char __user *buf, int len, int id, int channel)
{
        act2000_card *card = act2000_findcard(id);

        if (card) {
                if (!(card->flags & ACT2000_FLAGS_RUNNING))
                        return -ENODEV;
                return (len);
        }
        printk(KERN_ERR
               "act2000: if_writecmd called with invalid driverId!\n");
        return -ENODEV;
}

static int
if_readstatus(u_char __user * buf, int len, int id, int channel)
{
        act2000_card *card = act2000_findcard(id);
	
        if (card) {
                if (!(card->flags & ACT2000_FLAGS_RUNNING))
                        return -ENODEV;
                return (act2000_readstatus(buf, len, card));
        }
        printk(KERN_ERR
               "act2000: if_readstatus called with invalid driverId!\n");
        return -ENODEV;
}

static int
if_sendbuf(int id, int channel, int ack, struct sk_buff *skb)
{
        act2000_card *card = act2000_findcard(id);
	
        if (card) {
                if (!(card->flags & ACT2000_FLAGS_RUNNING))
                        return -ENODEV;
		return (act2000_sendbuf(card, channel, ack, skb));
        }
        printk(KERN_ERR
               "act2000: if_sendbuf called with invalid driverId!\n");
        return -ENODEV;
}


/*
 * Allocate a new card-struct, initialize it
 * link it into cards-list.
 */
static void
act2000_alloccard(int bus, int port, int irq, char *id)
{
	int i;
        act2000_card *card;
        if (!(card = kzalloc(sizeof(act2000_card), GFP_KERNEL))) {
                printk(KERN_WARNING
		       "act2000: (%s) Could not allocate card-struct.\n", id);
                return;
        }
        spin_lock_init(&card->lock);
        spin_lock_init(&card->mnlock);
	skb_queue_head_init(&card->sndq);
	skb_queue_head_init(&card->rcvq);
	skb_queue_head_init(&card->ackq);
	INIT_WORK(&card->snd_tq, act2000_transmit);
	INIT_WORK(&card->rcv_tq, actcapi_dispatch);
	INIT_WORK(&card->poll_tq, act2000_receive);
	init_timer(&card->ptimer);
	card->interface.owner = THIS_MODULE;
        card->interface.channels = ACT2000_BCH;
        card->interface.maxbufsize = 4000;
        card->interface.command = if_command;
        card->interface.writebuf_skb = if_sendbuf;
        card->interface.writecmd = if_writecmd;
        card->interface.readstat = if_readstatus;
        card->interface.features =
		ISDN_FEATURE_L2_X75I |
		ISDN_FEATURE_L2_HDLC |
		ISDN_FEATURE_L3_TRANS |
		ISDN_FEATURE_P_UNKNOWN;
        card->interface.hl_hdrlen = 20;
        card->ptype = ISDN_PTYPE_EURO;
        strlcpy(card->interface.id, id, sizeof(card->interface.id));
        for (i=0; i<ACT2000_BCH; i++) {
                card->bch[i].plci = 0x8000;
                card->bch[i].ncci = 0x8000;
                card->bch[i].l2prot = ISDN_PROTO_L2_X75I;
                card->bch[i].l3prot = ISDN_PROTO_L3_TRANS;
        }
        card->myid = -1;
        card->bus = bus;
        card->port = port;
        card->irq = irq;
        card->next = cards;
        cards = card;
}

/*
 * register card at linklevel
 */
static int
act2000_registercard(act2000_card * card)
{
        switch (card->bus) {
		case ACT2000_BUS_ISA:
			break;
		case ACT2000_BUS_MCA:
		case ACT2000_BUS_PCMCIA:
		default:
			printk(KERN_WARNING
			       "act2000: Illegal BUS type %d\n",
			       card->bus);
			return -1;
        }
        if (!register_isdn(&card->interface)) {
                printk(KERN_WARNING
                       "act2000: Unable to register %s\n",
                       card->interface.id);
                return -1;
        }
        card->myid = card->interface.channels;
        sprintf(card->regname, "act2000-isdn (%s)", card->interface.id);
        return 0;
}

static void
unregister_card(act2000_card * card)
{
        isdn_ctrl cmd;

        cmd.command = ISDN_STAT_UNLOAD;
        cmd.driver = card->myid;
        card->interface.statcallb(&cmd);
        switch (card->bus) {
		case ACT2000_BUS_ISA:
			act2000_isa_release(card);
			break;
		case ACT2000_BUS_MCA:
		case ACT2000_BUS_PCMCIA:
		default:
			printk(KERN_WARNING
			       "act2000: Invalid BUS type %d\n",
			       card->bus);
			break;
        }
}

static int
act2000_addcard(int bus, int port, int irq, char *id)
{
	act2000_card *p;
	act2000_card *q = NULL;
	int initialized;
	int added = 0;
	int failed = 0;
	int i;

	if (!bus)
		bus = ACT2000_BUS_ISA;
	if (port != -1) {
		/* Port defined, do fixed setup */
		act2000_alloccard(bus, port, irq, id);
	} else {
		/* No port defined, perform autoprobing.
		 * This may result in more than one card detected.
		 */
		switch (bus) {
			case ACT2000_BUS_ISA:
				for (i = 0; i < ISA_NRPORTS; i++)
					if (act2000_isa_detect(act2000_isa_ports[i])) {
						printk(KERN_INFO
						       "act2000: Detected ISA card at port 0x%x\n",
						       act2000_isa_ports[i]);
						act2000_alloccard(bus, act2000_isa_ports[i], irq, id);
					}
				break;
			case ACT2000_BUS_MCA:
			case ACT2000_BUS_PCMCIA:
			default:
				printk(KERN_WARNING
				       "act2000: addcard: Invalid BUS type %d\n",
				       bus);
		}
	}
	if (!cards)
		return 1;
        p = cards;
        while (p) {
		initialized = 0;
		if (!p->interface.statcallb) {
			/* Not yet registered.
			 * Try to register and activate it.
			 */
			added++;
			switch (p->bus) {
				case ACT2000_BUS_ISA:
					if (act2000_isa_detect(p->port)) {
						if (act2000_registercard(p))
							break;
						if (act2000_isa_config_port(p, p->port)) {
							printk(KERN_WARNING
							       "act2000: Could not request port 0x%04x\n",
							       p->port);
							unregister_card(p);
							p->interface.statcallb = NULL;
							break;
						}
						if (act2000_isa_config_irq(p, p->irq)) {
							printk(KERN_INFO
							       "act2000: No IRQ available, fallback to polling\n");
							/* Fall back to polled operation */
							p->irq = 0;
						}
						printk(KERN_INFO
						       "act2000: ISA"
						       "-type card at port "
						       "0x%04x ",
						       p->port);
						if (p->irq)
							printk("irq %d\n", p->irq);
						else
							printk("polled\n");
						initialized = 1;
					}
					break;
				case ACT2000_BUS_MCA:
				case ACT2000_BUS_PCMCIA:
				default:
					printk(KERN_WARNING
					       "act2000: addcard: Invalid BUS type %d\n",
					       p->bus);
			}
		} else
			/* Card already initialized */
			initialized = 1;
                if (initialized) {
			/* Init OK, next card ... */
                        q = p;
                        p = p->next;
                } else {
                        /* Init failed, remove card from list, free memory */
                        printk(KERN_WARNING
                               "act2000: Initialization of %s failed\n",
                               p->interface.id);
                        if (q) {
                                q->next = p->next;
                                kfree(p);
                                p = q->next;
                        } else {
                                cards = p->next;
                                kfree(p);
                                p = cards;
                        }
			failed++;
                }
	}
        return (added - failed);
}

#define DRIVERNAME "IBM Active 2000 ISDN driver"

static int __init act2000_init(void)
{
        printk(KERN_INFO "%s\n", DRIVERNAME);
        if (!cards)
		act2000_addcard(act_bus, act_port, act_irq, act_id);
        if (!cards)
                printk(KERN_INFO "act2000: No cards defined yet\n");
        return 0;
}

static void __exit act2000_exit(void)
{
        act2000_card *card = cards;
        act2000_card *last;
        while (card) {
                unregister_card(card);
		del_timer(&card->ptimer);
                card = card->next;
        }
        card = cards;
        while (card) {
                last = card;
                card = card->next;
		act2000_clear_msn(last);
                kfree(last);
        }
        printk(KERN_INFO "%s unloaded\n", DRIVERNAME);
}

module_init(act2000_init);
module_exit(act2000_exit);
