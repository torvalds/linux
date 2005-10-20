/*
 *	NET3:	A (fairly minimal) implementation of synchronous PPP for Linux
 *		as well as a CISCO HDLC implementation. See the copyright 
 *		message below for the original source.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the license, or (at your option) any later version.
 *
 *	Note however. This code is also used in a different form by FreeBSD.
 *	Therefore when making any non OS specific change please consider
 *	contributing it back to the original author under the terms
 *	below in addition.
 *		-- Alan
 *
 *	Port for Linux-2.1 by Jan "Yenya" Kasprzak <kas@fi.muni.cz>
 */

/*
 * Synchronous PPP/Cisco link level subroutines.
 * Keepalive protocol implemented in both Cisco and PPP modes.
 *
 * Copyright (C) 1994 Cronyx Ltd.
 * Author: Serge Vakulenko, <vak@zebub.msk.su>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * Version 1.9, Wed Oct  4 18:58:15 MSK 1995
 *
 * $Id: syncppp.c,v 1.18 2000/04/11 05:25:31 asj Exp $
 */
#undef DEBUG

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/route.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/random.h>
#include <linux/pkt_sched.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>

#include <net/syncppp.h>

#include <asm/byteorder.h>
#include <asm/uaccess.h>

#define MAXALIVECNT     6               /* max. alive packets */

#define PPP_ALLSTATIONS 0xff            /* All-Stations broadcast address */
#define PPP_UI          0x03            /* Unnumbered Information */
#define PPP_IP          0x0021          /* Internet Protocol */
#define PPP_ISO         0x0023          /* ISO OSI Protocol */
#define PPP_XNS         0x0025          /* Xerox NS Protocol */
#define PPP_IPX         0x002b          /* Novell IPX Protocol */
#define PPP_LCP         0xc021          /* Link Control Protocol */
#define PPP_IPCP        0x8021          /* Internet Protocol Control Protocol */

#define LCP_CONF_REQ    1               /* PPP LCP configure request */
#define LCP_CONF_ACK    2               /* PPP LCP configure acknowledge */
#define LCP_CONF_NAK    3               /* PPP LCP configure negative ack */
#define LCP_CONF_REJ    4               /* PPP LCP configure reject */
#define LCP_TERM_REQ    5               /* PPP LCP terminate request */
#define LCP_TERM_ACK    6               /* PPP LCP terminate acknowledge */
#define LCP_CODE_REJ    7               /* PPP LCP code reject */
#define LCP_PROTO_REJ   8               /* PPP LCP protocol reject */
#define LCP_ECHO_REQ    9               /* PPP LCP echo request */
#define LCP_ECHO_REPLY  10              /* PPP LCP echo reply */
#define LCP_DISC_REQ    11              /* PPP LCP discard request */

#define LCP_OPT_MRU             1       /* maximum receive unit */
#define LCP_OPT_ASYNC_MAP       2       /* async control character map */
#define LCP_OPT_AUTH_PROTO      3       /* authentication protocol */
#define LCP_OPT_QUAL_PROTO      4       /* quality protocol */
#define LCP_OPT_MAGIC           5       /* magic number */
#define LCP_OPT_RESERVED        6       /* reserved */
#define LCP_OPT_PROTO_COMP      7       /* protocol field compression */
#define LCP_OPT_ADDR_COMP       8       /* address/control field compression */

#define IPCP_CONF_REQ   LCP_CONF_REQ    /* PPP IPCP configure request */
#define IPCP_CONF_ACK   LCP_CONF_ACK    /* PPP IPCP configure acknowledge */
#define IPCP_CONF_NAK   LCP_CONF_NAK    /* PPP IPCP configure negative ack */
#define IPCP_CONF_REJ   LCP_CONF_REJ    /* PPP IPCP configure reject */
#define IPCP_TERM_REQ   LCP_TERM_REQ    /* PPP IPCP terminate request */
#define IPCP_TERM_ACK   LCP_TERM_ACK    /* PPP IPCP terminate acknowledge */
#define IPCP_CODE_REJ   LCP_CODE_REJ    /* PPP IPCP code reject */

#define CISCO_MULTICAST         0x8f    /* Cisco multicast address */
#define CISCO_UNICAST           0x0f    /* Cisco unicast address */
#define CISCO_KEEPALIVE         0x8035  /* Cisco keepalive protocol */
#define CISCO_ADDR_REQ          0       /* Cisco address request */
#define CISCO_ADDR_REPLY        1       /* Cisco address reply */
#define CISCO_KEEPALIVE_REQ     2       /* Cisco keepalive request */

struct ppp_header {
	u8 address;
	u8 control;
	u16 protocol;
};
#define PPP_HEADER_LEN          sizeof (struct ppp_header)

struct lcp_header {
	u8 type;
	u8 ident;
	u16 len;
};
#define LCP_HEADER_LEN          sizeof (struct lcp_header)

struct cisco_packet {
	u32 type;
	u32 par1;
	u32 par2;
	u16 rel;
	u16 time0;
	u16 time1;
};
#define CISCO_PACKET_LEN 18
#define CISCO_BIG_PACKET_LEN 20

static struct sppp *spppq;
static struct timer_list sppp_keepalive_timer;
static DEFINE_SPINLOCK(spppq_lock);

/* global xmit queue for sending packets while spinlock is held */
static struct sk_buff_head tx_queue;

static void sppp_keepalive (unsigned long dummy);
static void sppp_cp_send (struct sppp *sp, u16 proto, u8 type,
	u8 ident, u16 len, void *data);
static void sppp_cisco_send (struct sppp *sp, int type, long par1, long par2);
static void sppp_lcp_input (struct sppp *sp, struct sk_buff *m);
static void sppp_cisco_input (struct sppp *sp, struct sk_buff *m);
static void sppp_ipcp_input (struct sppp *sp, struct sk_buff *m);
static void sppp_lcp_open (struct sppp *sp);
static void sppp_ipcp_open (struct sppp *sp);
static int sppp_lcp_conf_parse_options (struct sppp *sp, struct lcp_header *h,
	int len, u32 *magic);
static void sppp_cp_timeout (unsigned long arg);
static char *sppp_lcp_type_name (u8 type);
static char *sppp_ipcp_type_name (u8 type);
static void sppp_print_bytes (u8 *p, u16 len);

static int debug;

/* Flush global outgoing packet queue to dev_queue_xmit().
 *
 * dev_queue_xmit() must be called with interrupts enabled
 * which means it can't be called with spinlocks held.
 * If a packet needs to be sent while a spinlock is held,
 * then put the packet into tx_queue, and call sppp_flush_xmit()
 * after spinlock is released.
 */
static void sppp_flush_xmit(void)
{
	struct sk_buff *skb;
	while ((skb = skb_dequeue(&tx_queue)) != NULL)
		dev_queue_xmit(skb);
}

/*
 *	Interface down stub
 */	

static void if_down(struct net_device *dev)
{
	struct sppp *sp = (struct sppp *)sppp_of(dev);

	sp->pp_link_state=SPPP_LINK_DOWN;
}

/*
 * Timeout routine activations.
 */

static void sppp_set_timeout(struct sppp *p,int s) 
{
	if (! (p->pp_flags & PP_TIMO)) 
	{
		init_timer(&p->pp_timer);
		p->pp_timer.function=sppp_cp_timeout;
		p->pp_timer.expires=jiffies+s*HZ;
		p->pp_timer.data=(unsigned long)p;
		p->pp_flags |= PP_TIMO;
		add_timer(&p->pp_timer);
	}
}

static void sppp_clear_timeout(struct sppp *p)
{
	if (p->pp_flags & PP_TIMO) 
	{
		del_timer(&p->pp_timer);
		p->pp_flags &= ~PP_TIMO; 
	}
}

/**
 *	sppp_input -	receive and process a WAN PPP frame
 *	@skb:	The buffer to process
 *	@dev:	The device it arrived on
 *
 *	This can be called directly by cards that do not have
 *	timing constraints but is normally called from the network layer
 *	after interrupt servicing to process frames queued via netif_rx().
 *
 *	We process the options in the card. If the frame is destined for
 *	the protocol stacks then it requeues the frame for the upper level
 *	protocol. If it is a control from it is processed and discarded
 *	here.
 */
 
void sppp_input (struct net_device *dev, struct sk_buff *skb)
{
	struct ppp_header *h;
	struct sppp *sp = (struct sppp *)sppp_of(dev);
	unsigned long flags;

	skb->dev=dev;
	skb->mac.raw=skb->data;

	if (dev->flags & IFF_RUNNING)
	{
		/* Count received bytes, add FCS and one flag */
		sp->ibytes+= skb->len + 3;
		sp->ipkts++;
	}

	if (!pskb_may_pull(skb, PPP_HEADER_LEN)) {
		/* Too small packet, drop it. */
		if (sp->pp_flags & PP_DEBUG)
			printk (KERN_DEBUG "%s: input packet is too small, %d bytes\n",
				dev->name, skb->len);
		kfree_skb(skb);
		return;
	}

	/* Get PPP header. */
	h = (struct ppp_header *)skb->data;
	skb_pull(skb,sizeof(struct ppp_header));

	spin_lock_irqsave(&sp->lock, flags);
	
	switch (h->address) {
	default:        /* Invalid PPP packet. */
		goto invalid;
	case PPP_ALLSTATIONS:
		if (h->control != PPP_UI)
			goto invalid;
		if (sp->pp_flags & PP_CISCO) {
			if (sp->pp_flags & PP_DEBUG)
				printk (KERN_WARNING "%s: PPP packet in Cisco mode <0x%x 0x%x 0x%x>\n",
					dev->name,
					h->address, h->control, ntohs (h->protocol));
			goto drop;
		}
		switch (ntohs (h->protocol)) {
		default:
			if (sp->lcp.state == LCP_STATE_OPENED)
				sppp_cp_send (sp, PPP_LCP, LCP_PROTO_REJ,
					++sp->pp_seq, skb->len + 2,
					&h->protocol);
			if (sp->pp_flags & PP_DEBUG)
				printk (KERN_WARNING "%s: invalid input protocol <0x%x 0x%x 0x%x>\n",
					dev->name,
					h->address, h->control, ntohs (h->protocol));
			goto drop;
		case PPP_LCP:
			sppp_lcp_input (sp, skb);
			goto drop;
		case PPP_IPCP:
			if (sp->lcp.state == LCP_STATE_OPENED)
				sppp_ipcp_input (sp, skb);
			else
				printk(KERN_DEBUG "IPCP when still waiting LCP finish.\n");
			goto drop;
		case PPP_IP:
			if (sp->ipcp.state == IPCP_STATE_OPENED) {
				if(sp->pp_flags&PP_DEBUG)
					printk(KERN_DEBUG "Yow an IP frame.\n");
				skb->protocol=htons(ETH_P_IP);
				netif_rx(skb);
				dev->last_rx = jiffies;
				goto done;
			}
			break;
#ifdef IPX
		case PPP_IPX:
			/* IPX IPXCP not implemented yet */
			if (sp->lcp.state == LCP_STATE_OPENED) {
				skb->protocol=htons(ETH_P_IPX);
				netif_rx(skb);
				dev->last_rx = jiffies;
				goto done;
			}
			break;
#endif
		}
		break;
	case CISCO_MULTICAST:
	case CISCO_UNICAST:
		/* Don't check the control field here (RFC 1547). */
		if (! (sp->pp_flags & PP_CISCO)) {
			if (sp->pp_flags & PP_DEBUG)
				printk (KERN_WARNING "%s: Cisco packet in PPP mode <0x%x 0x%x 0x%x>\n",
					dev->name,
					h->address, h->control, ntohs (h->protocol));
			goto drop;
		}
		switch (ntohs (h->protocol)) {
		default:
			goto invalid;
		case CISCO_KEEPALIVE:
			sppp_cisco_input (sp, skb);
			goto drop;
#ifdef CONFIG_INET
		case ETH_P_IP:
			skb->protocol=htons(ETH_P_IP);
			netif_rx(skb);
			dev->last_rx = jiffies;
			goto done;
#endif
#ifdef CONFIG_IPX
		case ETH_P_IPX:
			skb->protocol=htons(ETH_P_IPX);
			netif_rx(skb);
			dev->last_rx = jiffies;
			goto done;
#endif
		}
		break;
	}
	goto drop;

invalid:
	if (sp->pp_flags & PP_DEBUG)
		printk (KERN_WARNING "%s: invalid input packet <0x%x 0x%x 0x%x>\n",
			dev->name, h->address, h->control, ntohs (h->protocol));
drop:
	kfree_skb(skb);
done:
	spin_unlock_irqrestore(&sp->lock, flags);
	sppp_flush_xmit();
	return;
}

EXPORT_SYMBOL(sppp_input);

/*
 *	Handle transmit packets.
 */
 
static int sppp_hard_header(struct sk_buff *skb, struct net_device *dev, __u16 type,
		void *daddr, void *saddr, unsigned int len)
{
	struct sppp *sp = (struct sppp *)sppp_of(dev);
	struct ppp_header *h;
	skb_push(skb,sizeof(struct ppp_header));
	h=(struct ppp_header *)skb->data;
	if(sp->pp_flags&PP_CISCO)
	{
		h->address = CISCO_UNICAST;
		h->control = 0;
	}
	else
	{
		h->address = PPP_ALLSTATIONS;
		h->control = PPP_UI;
	}
	if(sp->pp_flags & PP_CISCO)
	{
		h->protocol = htons(type);
	}
	else switch(type)
	{
		case ETH_P_IP:
			h->protocol = htons(PPP_IP);
			break;
		case ETH_P_IPX:
			h->protocol = htons(PPP_IPX);
			break;
	}
	return sizeof(struct ppp_header);
}

static int sppp_rebuild_header(struct sk_buff *skb)
{
	return 0;
}

/*
 * Send keepalive packets, every 10 seconds.
 */

static void sppp_keepalive (unsigned long dummy)
{
	struct sppp *sp;
	unsigned long flags;

	spin_lock_irqsave(&spppq_lock, flags);

	for (sp=spppq; sp; sp=sp->pp_next) 
	{
		struct net_device *dev = sp->pp_if;

		/* Keepalive mode disabled or channel down? */
		if (! (sp->pp_flags & PP_KEEPALIVE) ||
		    ! (dev->flags & IFF_UP))
			continue;

		spin_lock(&sp->lock);

		/* No keepalive in PPP mode if LCP not opened yet. */
		if (! (sp->pp_flags & PP_CISCO) &&
		    sp->lcp.state != LCP_STATE_OPENED) {
			spin_unlock(&sp->lock);
			continue;
		}

		if (sp->pp_alivecnt == MAXALIVECNT) {
			/* No keepalive packets got.  Stop the interface. */
			printk (KERN_WARNING "%s: protocol down\n", dev->name);
			if_down (dev);
			if (! (sp->pp_flags & PP_CISCO)) {
				/* Shut down the PPP link. */
				sp->lcp.magic = jiffies;
				sp->lcp.state = LCP_STATE_CLOSED;
				sp->ipcp.state = IPCP_STATE_CLOSED;
				sppp_clear_timeout (sp);
				/* Initiate negotiation. */
				sppp_lcp_open (sp);
			}
		}
		if (sp->pp_alivecnt <= MAXALIVECNT)
			++sp->pp_alivecnt;
		if (sp->pp_flags & PP_CISCO)
			sppp_cisco_send (sp, CISCO_KEEPALIVE_REQ, ++sp->pp_seq,
				sp->pp_rseq);
		else if (sp->lcp.state == LCP_STATE_OPENED) {
			long nmagic = htonl (sp->lcp.magic);
			sp->lcp.echoid = ++sp->pp_seq;
			sppp_cp_send (sp, PPP_LCP, LCP_ECHO_REQ,
				sp->lcp.echoid, 4, &nmagic);
		}

		spin_unlock(&sp->lock);
	}
	spin_unlock_irqrestore(&spppq_lock, flags);
	sppp_flush_xmit();
	sppp_keepalive_timer.expires=jiffies+10*HZ;
	add_timer(&sppp_keepalive_timer);
}

/*
 * Handle incoming PPP Link Control Protocol packets.
 */
 
static void sppp_lcp_input (struct sppp *sp, struct sk_buff *skb)
{
	struct lcp_header *h;
	struct net_device *dev = sp->pp_if;
	int len = skb->len;
	u8 *p, opt[6];
	u32 rmagic;

	if (!pskb_may_pull(skb, sizeof(struct lcp_header))) {
		if (sp->pp_flags & PP_DEBUG)
			printk (KERN_WARNING "%s: invalid lcp packet length: %d bytes\n",
				dev->name, len);
		return;
	}
	h = (struct lcp_header *)skb->data;
	skb_pull(skb,sizeof(struct lcp_header *));
	
	if (sp->pp_flags & PP_DEBUG) 
	{
		char state = '?';
		switch (sp->lcp.state) {
		case LCP_STATE_CLOSED:   state = 'C'; break;
		case LCP_STATE_ACK_RCVD: state = 'R'; break;
		case LCP_STATE_ACK_SENT: state = 'S'; break;
		case LCP_STATE_OPENED:   state = 'O'; break;
		}
		printk (KERN_WARNING "%s: lcp input(%c): %d bytes <%s id=%xh len=%xh",
			dev->name, state, len,
			sppp_lcp_type_name (h->type), h->ident, ntohs (h->len));
		if (len > 4)
			sppp_print_bytes ((u8*) (h+1), len-4);
		printk (">\n");
	}
	if (len > ntohs (h->len))
		len = ntohs (h->len);
	switch (h->type) {
	default:
		/* Unknown packet type -- send Code-Reject packet. */
		sppp_cp_send (sp, PPP_LCP, LCP_CODE_REJ, ++sp->pp_seq,
			skb->len, h);
		break;
	case LCP_CONF_REQ:
		if (len < 4) {
			if (sp->pp_flags & PP_DEBUG)
				printk (KERN_DEBUG"%s: invalid lcp configure request packet length: %d bytes\n",
					dev->name, len);
			break;
		}
		if (len>4 && !sppp_lcp_conf_parse_options (sp, h, len, &rmagic))
			goto badreq;
		if (rmagic == sp->lcp.magic) {
			/* Local and remote magics equal -- loopback? */
			if (sp->pp_loopcnt >= MAXALIVECNT*5) {
				printk (KERN_WARNING "%s: loopback\n",
					dev->name);
				sp->pp_loopcnt = 0;
				if (dev->flags & IFF_UP) {
					if_down (dev);
				}
			} else if (sp->pp_flags & PP_DEBUG)
				printk (KERN_DEBUG "%s: conf req: magic glitch\n",
					dev->name);
			++sp->pp_loopcnt;

			/* MUST send Conf-Nack packet. */
			rmagic = ~sp->lcp.magic;
			opt[0] = LCP_OPT_MAGIC;
			opt[1] = sizeof (opt);
			opt[2] = rmagic >> 24;
			opt[3] = rmagic >> 16;
			opt[4] = rmagic >> 8;
			opt[5] = rmagic;
			sppp_cp_send (sp, PPP_LCP, LCP_CONF_NAK,
				h->ident, sizeof (opt), &opt);
badreq:
			switch (sp->lcp.state) {
			case LCP_STATE_OPENED:
				/* Initiate renegotiation. */
				sppp_lcp_open (sp);
				/* fall through... */
			case LCP_STATE_ACK_SENT:
				/* Go to closed state. */
				sp->lcp.state = LCP_STATE_CLOSED;
				sp->ipcp.state = IPCP_STATE_CLOSED;
			}
			break;
		}
		/* Send Configure-Ack packet. */
		sp->pp_loopcnt = 0;
		if (sp->lcp.state != LCP_STATE_OPENED) {
			sppp_cp_send (sp, PPP_LCP, LCP_CONF_ACK,
					h->ident, len-4, h+1);
		}
		/* Change the state. */
		switch (sp->lcp.state) {
		case LCP_STATE_CLOSED:
			sp->lcp.state = LCP_STATE_ACK_SENT;
			break;
		case LCP_STATE_ACK_RCVD:
			sp->lcp.state = LCP_STATE_OPENED;
			sppp_ipcp_open (sp);
			break;
		case LCP_STATE_OPENED:
			/* Remote magic changed -- close session. */
			sp->lcp.state = LCP_STATE_CLOSED;
			sp->ipcp.state = IPCP_STATE_CLOSED;
			/* Initiate renegotiation. */
			sppp_lcp_open (sp);
			/* Send ACK after our REQ in attempt to break loop */
			sppp_cp_send (sp, PPP_LCP, LCP_CONF_ACK,
					h->ident, len-4, h+1);
			sp->lcp.state = LCP_STATE_ACK_SENT;
			break;
		}
		break;
	case LCP_CONF_ACK:
		if (h->ident != sp->lcp.confid)
			break;
		sppp_clear_timeout (sp);
		if ((sp->pp_link_state != SPPP_LINK_UP) &&
		    (dev->flags & IFF_UP)) {
			/* Coming out of loopback mode. */
			sp->pp_link_state=SPPP_LINK_UP;
			printk (KERN_INFO "%s: protocol up\n", dev->name);
		}
		switch (sp->lcp.state) {
		case LCP_STATE_CLOSED:
			sp->lcp.state = LCP_STATE_ACK_RCVD;
			sppp_set_timeout (sp, 5);
			break;
		case LCP_STATE_ACK_SENT:
			sp->lcp.state = LCP_STATE_OPENED;
			sppp_ipcp_open (sp);
			break;
		}
		break;
	case LCP_CONF_NAK:
		if (h->ident != sp->lcp.confid)
			break;
		p = (u8*) (h+1);
		if (len>=10 && p[0] == LCP_OPT_MAGIC && p[1] >= 4) {
			rmagic = (u32)p[2] << 24 |
				(u32)p[3] << 16 | p[4] << 8 | p[5];
			if (rmagic == ~sp->lcp.magic) {
				int newmagic;
				if (sp->pp_flags & PP_DEBUG)
					printk (KERN_DEBUG "%s: conf nak: magic glitch\n",
						dev->name);
				get_random_bytes(&newmagic, sizeof(newmagic));
				sp->lcp.magic += newmagic;
			} else
				sp->lcp.magic = rmagic;
			}
		if (sp->lcp.state != LCP_STATE_ACK_SENT) {
			/* Go to closed state. */
			sp->lcp.state = LCP_STATE_CLOSED;
			sp->ipcp.state = IPCP_STATE_CLOSED;
		}
		/* The link will be renegotiated after timeout,
		 * to avoid endless req-nack loop. */
		sppp_clear_timeout (sp);
		sppp_set_timeout (sp, 2);
		break;
	case LCP_CONF_REJ:
		if (h->ident != sp->lcp.confid)
			break;
		sppp_clear_timeout (sp);
		/* Initiate renegotiation. */
		sppp_lcp_open (sp);
		if (sp->lcp.state != LCP_STATE_ACK_SENT) {
			/* Go to closed state. */
			sp->lcp.state = LCP_STATE_CLOSED;
			sp->ipcp.state = IPCP_STATE_CLOSED;
		}
		break;
	case LCP_TERM_REQ:
		sppp_clear_timeout (sp);
		/* Send Terminate-Ack packet. */
		sppp_cp_send (sp, PPP_LCP, LCP_TERM_ACK, h->ident, 0, NULL);
		/* Go to closed state. */
		sp->lcp.state = LCP_STATE_CLOSED;
		sp->ipcp.state = IPCP_STATE_CLOSED;
		/* Initiate renegotiation. */
		sppp_lcp_open (sp);
		break;
	case LCP_TERM_ACK:
	case LCP_CODE_REJ:
	case LCP_PROTO_REJ:
		/* Ignore for now. */
		break;
	case LCP_DISC_REQ:
		/* Discard the packet. */
		break;
	case LCP_ECHO_REQ:
		if (sp->lcp.state != LCP_STATE_OPENED)
			break;
		if (len < 8) {
			if (sp->pp_flags & PP_DEBUG)
				printk (KERN_WARNING "%s: invalid lcp echo request packet length: %d bytes\n",
					dev->name, len);
			break;
		}
		if (ntohl (*(long*)(h+1)) == sp->lcp.magic) {
			/* Line loopback mode detected. */
			printk (KERN_WARNING "%s: loopback\n", dev->name);
			if_down (dev);

			/* Shut down the PPP link. */
			sp->lcp.state = LCP_STATE_CLOSED;
			sp->ipcp.state = IPCP_STATE_CLOSED;
			sppp_clear_timeout (sp);
			/* Initiate negotiation. */
			sppp_lcp_open (sp);
			break;
		}
		*(long*)(h+1) = htonl (sp->lcp.magic);
		sppp_cp_send (sp, PPP_LCP, LCP_ECHO_REPLY, h->ident, len-4, h+1);
		break;
	case LCP_ECHO_REPLY:
		if (h->ident != sp->lcp.echoid)
			break;
		if (len < 8) {
			if (sp->pp_flags & PP_DEBUG)
				printk (KERN_WARNING "%s: invalid lcp echo reply packet length: %d bytes\n",
					dev->name, len);
			break;
		}
		if (ntohl (*(long*)(h+1)) != sp->lcp.magic)
		sp->pp_alivecnt = 0;
		break;
	}
}

/*
 * Handle incoming Cisco keepalive protocol packets.
 */

static void sppp_cisco_input (struct sppp *sp, struct sk_buff *skb)
{
	struct cisco_packet *h;
	struct net_device *dev = sp->pp_if;

	if (!pskb_may_pull(skb, sizeof(struct cisco_packet))
	    || (skb->len != CISCO_PACKET_LEN
		&& skb->len != CISCO_BIG_PACKET_LEN)) {
		if (sp->pp_flags & PP_DEBUG)
			printk (KERN_WARNING "%s: invalid cisco packet length: %d bytes\n",
				dev->name,  skb->len);
		return;
	}
	h = (struct cisco_packet *)skb->data;
	skb_pull(skb, sizeof(struct cisco_packet*));
	if (sp->pp_flags & PP_DEBUG)
		printk (KERN_WARNING "%s: cisco input: %d bytes <%xh %xh %xh %xh %xh-%xh>\n",
			dev->name,  skb->len,
			ntohl (h->type), h->par1, h->par2, h->rel,
			h->time0, h->time1);
	switch (ntohl (h->type)) {
	default:
		if (sp->pp_flags & PP_DEBUG)
			printk (KERN_WARNING "%s: unknown cisco packet type: 0x%x\n",
				dev->name,  ntohl (h->type));
		break;
	case CISCO_ADDR_REPLY:
		/* Reply on address request, ignore */
		break;
	case CISCO_KEEPALIVE_REQ:
		sp->pp_alivecnt = 0;
		sp->pp_rseq = ntohl (h->par1);
		if (sp->pp_seq == sp->pp_rseq) {
			/* Local and remote sequence numbers are equal.
			 * Probably, the line is in loopback mode. */
			int newseq;
			if (sp->pp_loopcnt >= MAXALIVECNT) {
				printk (KERN_WARNING "%s: loopback\n",
					dev->name);
				sp->pp_loopcnt = 0;
				if (dev->flags & IFF_UP) {
					if_down (dev);
				}
			}
			++sp->pp_loopcnt;

			/* Generate new local sequence number */
			get_random_bytes(&newseq, sizeof(newseq));
			sp->pp_seq ^= newseq;
			break;
		}
		sp->pp_loopcnt = 0;
		if (sp->pp_link_state==SPPP_LINK_DOWN &&
		    (dev->flags & IFF_UP)) {
			sp->pp_link_state=SPPP_LINK_UP;
			printk (KERN_INFO "%s: protocol up\n", dev->name);
		}
		break;
	case CISCO_ADDR_REQ:
		/* Stolen from net/ipv4/devinet.c -- SIOCGIFADDR ioctl */
		{
		struct in_device *in_dev;
		struct in_ifaddr *ifa;
		u32 addr = 0, mask = ~0; /* FIXME: is the mask correct? */
#ifdef CONFIG_INET
		rcu_read_lock();
		if ((in_dev = __in_dev_get_rcu(dev)) != NULL)
		{
			for (ifa=in_dev->ifa_list; ifa != NULL;
				ifa=ifa->ifa_next) {
				if (strcmp(dev->name, ifa->ifa_label) == 0) 
				{
					addr = ifa->ifa_local;
					mask = ifa->ifa_mask;
					break;
				}
			}
		}
		rcu_read_unlock();
#endif		
		/* I hope both addr and mask are in the net order */
		sppp_cisco_send (sp, CISCO_ADDR_REPLY, addr, mask);
		break;
		}
	}
}


/*
 * Send PPP LCP packet.
 */

static void sppp_cp_send (struct sppp *sp, u16 proto, u8 type,
	u8 ident, u16 len, void *data)
{
	struct ppp_header *h;
	struct lcp_header *lh;
	struct sk_buff *skb;
	struct net_device *dev = sp->pp_if;

	skb=alloc_skb(dev->hard_header_len+PPP_HEADER_LEN+LCP_HEADER_LEN+len,
		GFP_ATOMIC);
	if (skb==NULL)
		return;

	skb_reserve(skb,dev->hard_header_len);
	
	h = (struct ppp_header *)skb_put(skb, sizeof(struct ppp_header));
	h->address = PPP_ALLSTATIONS;        /* broadcast address */
	h->control = PPP_UI;                 /* Unnumbered Info */
	h->protocol = htons (proto);         /* Link Control Protocol */

	lh = (struct lcp_header *)skb_put(skb, sizeof(struct lcp_header));
	lh->type = type;
	lh->ident = ident;
	lh->len = htons (LCP_HEADER_LEN + len);

	if (len)
		memcpy(skb_put(skb,len),data, len);

	if (sp->pp_flags & PP_DEBUG) {
		printk (KERN_WARNING "%s: %s output <%s id=%xh len=%xh",
			dev->name, 
			proto==PPP_LCP ? "lcp" : "ipcp",
			proto==PPP_LCP ? sppp_lcp_type_name (lh->type) :
			sppp_ipcp_type_name (lh->type), lh->ident,
			ntohs (lh->len));
		if (len)
			sppp_print_bytes ((u8*) (lh+1), len);
		printk (">\n");
	}
	sp->obytes += skb->len;
	/* Control is high priority so it doesn't get queued behind data */
	skb->priority=TC_PRIO_CONTROL;
	skb->dev = dev;
	skb_queue_tail(&tx_queue, skb);
}

/*
 * Send Cisco keepalive packet.
 */

static void sppp_cisco_send (struct sppp *sp, int type, long par1, long par2)
{
	struct ppp_header *h;
	struct cisco_packet *ch;
	struct sk_buff *skb;
	struct net_device *dev = sp->pp_if;
	u32 t = jiffies * 1000/HZ;

	skb=alloc_skb(dev->hard_header_len+PPP_HEADER_LEN+CISCO_PACKET_LEN,
		GFP_ATOMIC);

	if(skb==NULL)
		return;
		
	skb_reserve(skb, dev->hard_header_len);
	h = (struct ppp_header *)skb_put (skb, sizeof(struct ppp_header));
	h->address = CISCO_MULTICAST;
	h->control = 0;
	h->protocol = htons (CISCO_KEEPALIVE);

	ch = (struct cisco_packet*)skb_put(skb, CISCO_PACKET_LEN);
	ch->type = htonl (type);
	ch->par1 = htonl (par1);
	ch->par2 = htonl (par2);
	ch->rel = -1;
	ch->time0 = htons ((u16) (t >> 16));
	ch->time1 = htons ((u16) t);

	if (sp->pp_flags & PP_DEBUG)
		printk (KERN_WARNING "%s: cisco output: <%xh %xh %xh %xh %xh-%xh>\n",
			dev->name,  ntohl (ch->type), ch->par1,
			ch->par2, ch->rel, ch->time0, ch->time1);
	sp->obytes += skb->len;
	skb->priority=TC_PRIO_CONTROL;
	skb->dev = dev;
	skb_queue_tail(&tx_queue, skb);
}

/**
 *	sppp_close - close down a synchronous PPP or Cisco HDLC link
 *	@dev: The network device to drop the link of
 *
 *	This drops the logical interface to the channel. It is not
 *	done politely as we assume we will also be dropping DTR. Any
 *	timeouts are killed.
 */

int sppp_close (struct net_device *dev)
{
	struct sppp *sp = (struct sppp *)sppp_of(dev);
	unsigned long flags;

	spin_lock_irqsave(&sp->lock, flags);
	sp->pp_link_state = SPPP_LINK_DOWN;
	sp->lcp.state = LCP_STATE_CLOSED;
	sp->ipcp.state = IPCP_STATE_CLOSED;
	sppp_clear_timeout (sp);
	spin_unlock_irqrestore(&sp->lock, flags);

	return 0;
}

EXPORT_SYMBOL(sppp_close);

/**
 *	sppp_open - open a synchronous PPP or Cisco HDLC link
 *	@dev:	Network device to activate
 *	
 *	Close down any existing synchronous session and commence
 *	from scratch. In the PPP case this means negotiating LCP/IPCP
 *	and friends, while for Cisco HDLC we simply need to start sending
 *	keepalives
 */

int sppp_open (struct net_device *dev)
{
	struct sppp *sp = (struct sppp *)sppp_of(dev);
	unsigned long flags;

	sppp_close(dev);

	spin_lock_irqsave(&sp->lock, flags);
	if (!(sp->pp_flags & PP_CISCO)) {
		sppp_lcp_open (sp);
	}
	sp->pp_link_state = SPPP_LINK_DOWN;
	spin_unlock_irqrestore(&sp->lock, flags);
	sppp_flush_xmit();

	return 0;
}

EXPORT_SYMBOL(sppp_open);

/**
 *	sppp_reopen - notify of physical link loss
 *	@dev: Device that lost the link
 *
 *	This function informs the synchronous protocol code that
 *	the underlying link died (for example a carrier drop on X.21)
 *
 *	We increment the magic numbers to ensure that if the other end
 *	failed to notice we will correctly start a new session. It happens
 *	do to the nature of telco circuits is that you can lose carrier on
 *	one endonly.
 *
 *	Having done this we go back to negotiating. This function may
 *	be called from an interrupt context.
 */
 
int sppp_reopen (struct net_device *dev)
{
	struct sppp *sp = (struct sppp *)sppp_of(dev);
	unsigned long flags;

	sppp_close(dev);

	spin_lock_irqsave(&sp->lock, flags);
	if (!(sp->pp_flags & PP_CISCO))
	{
		sp->lcp.magic = jiffies;
		++sp->pp_seq;
		sp->lcp.state = LCP_STATE_CLOSED;
		sp->ipcp.state = IPCP_STATE_CLOSED;
		/* Give it a moment for the line to settle then go */
		sppp_set_timeout (sp, 1);
	} 
	sp->pp_link_state=SPPP_LINK_DOWN;
	spin_unlock_irqrestore(&sp->lock, flags);

	return 0;
}

EXPORT_SYMBOL(sppp_reopen);

/**
 *	sppp_change_mtu - Change the link MTU
 *	@dev:	Device to change MTU on
 *	@new_mtu: New MTU
 *
 *	Change the MTU on the link. This can only be called with
 *	the link down. It returns an error if the link is up or
 *	the mtu is out of range.
 */
 
int sppp_change_mtu(struct net_device *dev, int new_mtu)
{
	if(new_mtu<128||new_mtu>PPP_MTU||(dev->flags&IFF_UP))
		return -EINVAL;
	dev->mtu=new_mtu;
	return 0;
}

EXPORT_SYMBOL(sppp_change_mtu);

/**
 *	sppp_do_ioctl - Ioctl handler for ppp/hdlc
 *	@dev: Device subject to ioctl
 *	@ifr: Interface request block from the user
 *	@cmd: Command that is being issued
 *	
 *	This function handles the ioctls that may be issued by the user
 *	to control the settings of a PPP/HDLC link. It does both busy
 *	and security checks. This function is intended to be wrapped by
 *	callers who wish to add additional ioctl calls of their own.
 */
 
int sppp_do_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct sppp *sp = (struct sppp *)sppp_of(dev);

	if(dev->flags&IFF_UP)
		return -EBUSY;
		
	if(!capable(CAP_NET_ADMIN))
		return -EPERM;
	
	switch(cmd)
	{
		case SPPPIOCCISCO:
			sp->pp_flags|=PP_CISCO;
			dev->type = ARPHRD_HDLC;
			break;
		case SPPPIOCPPP:
			sp->pp_flags&=~PP_CISCO;
			dev->type = ARPHRD_PPP;
			break;
		case SPPPIOCDEBUG:
			sp->pp_flags&=~PP_DEBUG;
			if(ifr->ifr_flags)
				sp->pp_flags|=PP_DEBUG;
			break;
		case SPPPIOCGFLAGS:
			if(copy_to_user(ifr->ifr_data, &sp->pp_flags, sizeof(sp->pp_flags)))
				return -EFAULT;
			break;
		case SPPPIOCSFLAGS:
			if(copy_from_user(&sp->pp_flags, ifr->ifr_data, sizeof(sp->pp_flags)))
				return -EFAULT;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

EXPORT_SYMBOL(sppp_do_ioctl);

/**
 *	sppp_attach - attach synchronous PPP/HDLC to a device
 *	@pd:	PPP device to initialise
 *
 *	This initialises the PPP/HDLC support on an interface. At the
 *	time of calling the dev element must point to the network device
 *	that this interface is attached to. The interface should not yet
 *	be registered. 
 */
 
void sppp_attach(struct ppp_device *pd)
{
	struct net_device *dev = pd->dev;
	struct sppp *sp = &pd->sppp;
	unsigned long flags;

	/* Make sure embedding is safe for sppp_of */
	BUG_ON(sppp_of(dev) != sp);

	spin_lock_irqsave(&spppq_lock, flags);
	/* Initialize keepalive handler. */
	if (! spppq)
	{
		init_timer(&sppp_keepalive_timer);
		sppp_keepalive_timer.expires=jiffies+10*HZ;
		sppp_keepalive_timer.function=sppp_keepalive;
		add_timer(&sppp_keepalive_timer);
	}
	/* Insert new entry into the keepalive list. */
	sp->pp_next = spppq;
	spppq = sp;
	spin_unlock_irqrestore(&spppq_lock, flags);

	sp->pp_loopcnt = 0;
	sp->pp_alivecnt = 0;
	sp->pp_seq = 0;
	sp->pp_rseq = 0;
	sp->pp_flags = PP_KEEPALIVE|PP_CISCO|debug;/*PP_DEBUG;*/
	sp->lcp.magic = 0;
	sp->lcp.state = LCP_STATE_CLOSED;
	sp->ipcp.state = IPCP_STATE_CLOSED;
	sp->pp_if = dev;
	spin_lock_init(&sp->lock);
	
	/* 
	 *	Device specific setup. All but interrupt handler and
	 *	hard_start_xmit.
	 */
	 
	dev->hard_header = sppp_hard_header;
	dev->rebuild_header = sppp_rebuild_header;
	dev->tx_queue_len = 10;
	dev->type = ARPHRD_HDLC;
	dev->addr_len = 0;
	dev->hard_header_len = sizeof(struct ppp_header);
	dev->mtu = PPP_MTU;
	/*
	 *	These 4 are callers but MUST also call sppp_ functions
	 */
	dev->do_ioctl = sppp_do_ioctl;
#if 0
	dev->get_stats = NULL;		/* Let the driver override these */
	dev->open = sppp_open;
	dev->stop = sppp_close;
#endif	
	dev->change_mtu = sppp_change_mtu;
	dev->hard_header_cache = NULL;
	dev->header_cache_update = NULL;
	dev->flags = IFF_MULTICAST|IFF_POINTOPOINT|IFF_NOARP;
}

EXPORT_SYMBOL(sppp_attach);

/**
 *	sppp_detach - release PPP resources from a device
 *	@dev:	Network device to release
 *
 *	Stop and free up any PPP/HDLC resources used by this
 *	interface. This must be called before the device is
 *	freed.
 */
 
void sppp_detach (struct net_device *dev)
{
	struct sppp **q, *p, *sp = (struct sppp *)sppp_of(dev);
	unsigned long flags;

	spin_lock_irqsave(&spppq_lock, flags);
	/* Remove the entry from the keepalive list. */
	for (q = &spppq; (p = *q); q = &p->pp_next)
		if (p == sp) {
			*q = p->pp_next;
			break;
		}

	/* Stop keepalive handler. */
	if (! spppq)
		del_timer(&sppp_keepalive_timer);
	sppp_clear_timeout (sp);
	spin_unlock_irqrestore(&spppq_lock, flags);
}

EXPORT_SYMBOL(sppp_detach);

/*
 * Analyze the LCP Configure-Request options list
 * for the presence of unknown options.
 * If the request contains unknown options, build and
 * send Configure-reject packet, containing only unknown options.
 */
static int
sppp_lcp_conf_parse_options (struct sppp *sp, struct lcp_header *h,
	int len, u32 *magic)
{
	u8 *buf, *r, *p;
	int rlen;

	len -= 4;
	buf = r = kmalloc (len, GFP_ATOMIC);
	if (! buf)
		return (0);

	p = (void*) (h+1);
	for (rlen=0; len>1 && p[1]; len-=p[1], p+=p[1]) {
		switch (*p) {
		case LCP_OPT_MAGIC:
			/* Magic number -- extract. */
			if (len >= 6 && p[1] == 6) {
				*magic = (u32)p[2] << 24 |
					(u32)p[3] << 16 | p[4] << 8 | p[5];
				continue;
			}
			break;
		case LCP_OPT_ASYNC_MAP:
			/* Async control character map -- check to be zero. */
			if (len >= 6 && p[1] == 6 && ! p[2] && ! p[3] &&
			    ! p[4] && ! p[5])
				continue;
			break;
		case LCP_OPT_MRU:
			/* Maximum receive unit -- always OK. */
			continue;
		default:
			/* Others not supported. */
			break;
		}
		/* Add the option to rejected list. */
		memcpy(r, p, p[1]);
		r += p[1];
		rlen += p[1];
	}
	if (rlen)
		sppp_cp_send (sp, PPP_LCP, LCP_CONF_REJ, h->ident, rlen, buf);
	kfree(buf);
	return (rlen == 0);
}

static void sppp_ipcp_input (struct sppp *sp, struct sk_buff *skb)
{
	struct lcp_header *h;
	struct net_device *dev = sp->pp_if;
	int len = skb->len;

	if (!pskb_may_pull(skb, sizeof(struct lcp_header))) {
		if (sp->pp_flags & PP_DEBUG)
			printk (KERN_WARNING "%s: invalid ipcp packet length: %d bytes\n",
				dev->name,  len);
		return;
	}
	h = (struct lcp_header *)skb->data;
	skb_pull(skb,sizeof(struct lcp_header));
	if (sp->pp_flags & PP_DEBUG) {
		printk (KERN_WARNING "%s: ipcp input: %d bytes <%s id=%xh len=%xh",
			dev->name,  len,
			sppp_ipcp_type_name (h->type), h->ident, ntohs (h->len));
		if (len > 4)
			sppp_print_bytes ((u8*) (h+1), len-4);
		printk (">\n");
	}
	if (len > ntohs (h->len))
		len = ntohs (h->len);
	switch (h->type) {
	default:
		/* Unknown packet type -- send Code-Reject packet. */
		sppp_cp_send (sp, PPP_IPCP, IPCP_CODE_REJ, ++sp->pp_seq, len, h);
		break;
	case IPCP_CONF_REQ:
		if (len < 4) {
			if (sp->pp_flags & PP_DEBUG)
				printk (KERN_WARNING "%s: invalid ipcp configure request packet length: %d bytes\n",
					dev->name, len);
			return;
		}
		if (len > 4) {
			sppp_cp_send (sp, PPP_IPCP, LCP_CONF_REJ, h->ident,
				len-4, h+1);

			switch (sp->ipcp.state) {
			case IPCP_STATE_OPENED:
				/* Initiate renegotiation. */
				sppp_ipcp_open (sp);
				/* fall through... */
			case IPCP_STATE_ACK_SENT:
				/* Go to closed state. */
				sp->ipcp.state = IPCP_STATE_CLOSED;
			}
		} else {
			/* Send Configure-Ack packet. */
			sppp_cp_send (sp, PPP_IPCP, IPCP_CONF_ACK, h->ident,
				0, NULL);
			/* Change the state. */
			if (sp->ipcp.state == IPCP_STATE_ACK_RCVD)
				sp->ipcp.state = IPCP_STATE_OPENED;
			else
				sp->ipcp.state = IPCP_STATE_ACK_SENT;
		}
		break;
	case IPCP_CONF_ACK:
		if (h->ident != sp->ipcp.confid)
			break;
		sppp_clear_timeout (sp);
		switch (sp->ipcp.state) {
		case IPCP_STATE_CLOSED:
			sp->ipcp.state = IPCP_STATE_ACK_RCVD;
			sppp_set_timeout (sp, 5);
			break;
		case IPCP_STATE_ACK_SENT:
			sp->ipcp.state = IPCP_STATE_OPENED;
			break;
		}
		break;
	case IPCP_CONF_NAK:
	case IPCP_CONF_REJ:
		if (h->ident != sp->ipcp.confid)
			break;
		sppp_clear_timeout (sp);
			/* Initiate renegotiation. */
		sppp_ipcp_open (sp);
		if (sp->ipcp.state != IPCP_STATE_ACK_SENT)
			/* Go to closed state. */
			sp->ipcp.state = IPCP_STATE_CLOSED;
		break;
	case IPCP_TERM_REQ:
		/* Send Terminate-Ack packet. */
		sppp_cp_send (sp, PPP_IPCP, IPCP_TERM_ACK, h->ident, 0, NULL);
		/* Go to closed state. */
		sp->ipcp.state = IPCP_STATE_CLOSED;
		/* Initiate renegotiation. */
		sppp_ipcp_open (sp);
		break;
	case IPCP_TERM_ACK:
		/* Ignore for now. */
	case IPCP_CODE_REJ:
		/* Ignore for now. */
		break;
	}
}

static void sppp_lcp_open (struct sppp *sp)
{
	char opt[6];

	if (! sp->lcp.magic)
		sp->lcp.magic = jiffies;
	opt[0] = LCP_OPT_MAGIC;
	opt[1] = sizeof (opt);
	opt[2] = sp->lcp.magic >> 24;
	opt[3] = sp->lcp.magic >> 16;
	opt[4] = sp->lcp.magic >> 8;
	opt[5] = sp->lcp.magic;
	sp->lcp.confid = ++sp->pp_seq;
	sppp_cp_send (sp, PPP_LCP, LCP_CONF_REQ, sp->lcp.confid,
		sizeof (opt), &opt);
	sppp_set_timeout (sp, 2);
}

static void sppp_ipcp_open (struct sppp *sp)
{
	sp->ipcp.confid = ++sp->pp_seq;
	sppp_cp_send (sp, PPP_IPCP, IPCP_CONF_REQ, sp->ipcp.confid, 0, NULL);
	sppp_set_timeout (sp, 2);
}

/*
 * Process PPP control protocol timeouts.
 */
 
static void sppp_cp_timeout (unsigned long arg)
{
	struct sppp *sp = (struct sppp*) arg;
	unsigned long flags;

	spin_lock_irqsave(&sp->lock, flags);

	sp->pp_flags &= ~PP_TIMO;
	if (! (sp->pp_if->flags & IFF_UP) || (sp->pp_flags & PP_CISCO)) {
		spin_unlock_irqrestore(&sp->lock, flags);
		return;
	}
	switch (sp->lcp.state) {
	case LCP_STATE_CLOSED:
		/* No ACK for Configure-Request, retry. */
		sppp_lcp_open (sp);
		break;
	case LCP_STATE_ACK_RCVD:
		/* ACK got, but no Configure-Request for peer, retry. */
		sppp_lcp_open (sp);
		sp->lcp.state = LCP_STATE_CLOSED;
		break;
	case LCP_STATE_ACK_SENT:
		/* ACK sent but no ACK for Configure-Request, retry. */
		sppp_lcp_open (sp);
		break;
	case LCP_STATE_OPENED:
		/* LCP is already OK, try IPCP. */
		switch (sp->ipcp.state) {
		case IPCP_STATE_CLOSED:
			/* No ACK for Configure-Request, retry. */
			sppp_ipcp_open (sp);
			break;
		case IPCP_STATE_ACK_RCVD:
			/* ACK got, but no Configure-Request for peer, retry. */
			sppp_ipcp_open (sp);
			sp->ipcp.state = IPCP_STATE_CLOSED;
			break;
		case IPCP_STATE_ACK_SENT:
			/* ACK sent but no ACK for Configure-Request, retry. */
			sppp_ipcp_open (sp);
			break;
		case IPCP_STATE_OPENED:
			/* IPCP is OK. */
			break;
		}
		break;
	}
	spin_unlock_irqrestore(&sp->lock, flags);
	sppp_flush_xmit();
}

static char *sppp_lcp_type_name (u8 type)
{
	static char buf [8];
	switch (type) {
	case LCP_CONF_REQ:   return ("conf-req");
	case LCP_CONF_ACK:   return ("conf-ack");
	case LCP_CONF_NAK:   return ("conf-nack");
	case LCP_CONF_REJ:   return ("conf-rej");
	case LCP_TERM_REQ:   return ("term-req");
	case LCP_TERM_ACK:   return ("term-ack");
	case LCP_CODE_REJ:   return ("code-rej");
	case LCP_PROTO_REJ:  return ("proto-rej");
	case LCP_ECHO_REQ:   return ("echo-req");
	case LCP_ECHO_REPLY: return ("echo-reply");
	case LCP_DISC_REQ:   return ("discard-req");
	}
	sprintf (buf, "%xh", type);
	return (buf);
}

static char *sppp_ipcp_type_name (u8 type)
{
	static char buf [8];
	switch (type) {
	case IPCP_CONF_REQ:   return ("conf-req");
	case IPCP_CONF_ACK:   return ("conf-ack");
	case IPCP_CONF_NAK:   return ("conf-nack");
	case IPCP_CONF_REJ:   return ("conf-rej");
	case IPCP_TERM_REQ:   return ("term-req");
	case IPCP_TERM_ACK:   return ("term-ack");
	case IPCP_CODE_REJ:   return ("code-rej");
	}
	sprintf (buf, "%xh", type);
	return (buf);
}

static void sppp_print_bytes (u_char *p, u16 len)
{
	printk (" %x", *p++);
	while (--len > 0)
		printk ("-%x", *p++);
}

/**
 *	sppp_rcv -	receive and process a WAN PPP frame
 *	@skb:	The buffer to process
 *	@dev:	The device it arrived on
 *	@p: Unused
 *	@orig_dev: Unused
 *
 *	Protocol glue. This drives the deferred processing mode the poorer
 *	cards use. This can be called directly by cards that do not have
 *	timing constraints but is normally called from the network layer
 *	after interrupt servicing to process frames queued via netif_rx.
 */

static int sppp_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *p, struct net_device *orig_dev)
{
	if ((skb = skb_share_check(skb, GFP_ATOMIC)) == NULL)
		return NET_RX_DROP;
	sppp_input(dev,skb);
	return 0;
}

struct packet_type sppp_packet_type = {
	.type	= __constant_htons(ETH_P_WAN_PPP),
	.func	= sppp_rcv,
};

static char banner[] __initdata = 
	KERN_INFO "Cronyx Ltd, Synchronous PPP and CISCO HDLC (c) 1994\n"
	KERN_INFO "Linux port (c) 1998 Building Number Three Ltd & "
		  "Jan \"Yenya\" Kasprzak.\n";

static int __init sync_ppp_init(void)
{
	if(debug)
		debug=PP_DEBUG;
	printk(banner);
	skb_queue_head_init(&tx_queue);
	dev_add_pack(&sppp_packet_type);
	return 0;
}


static void __exit sync_ppp_cleanup(void)
{
	dev_remove_pack(&sppp_packet_type);
}

module_init(sync_ppp_init);
module_exit(sync_ppp_cleanup);
module_param(debug, int, 0);
MODULE_LICENSE("GPL");

