// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic HDLC support routines for Linux
 * Point-to-point protocol support
 *
 * Copyright (C) 1999 - 2008 Krzysztof Halasa <khc@pm.waw.pl>
 */

#include <linux/errno.h>
#include <linux/hdlc.h>
#include <linux/if_arp.h>
#include <linux/inetdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pkt_sched.h>
#include <linux/poll.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define DEBUG_CP		0 /* also bytes# to dump */
#define DEBUG_STATE		0
#define DEBUG_HARD_HEADER	0

#define HDLC_ADDR_ALLSTATIONS	0xFF
#define HDLC_CTRL_UI		0x03

#define PID_LCP			0xC021
#define PID_IP			0x0021
#define PID_IPCP		0x8021
#define PID_IPV6		0x0057
#define PID_IPV6CP		0x8057

enum {IDX_LCP = 0, IDX_IPCP, IDX_IPV6CP, IDX_COUNT};
enum {CP_CONF_REQ = 1, CP_CONF_ACK, CP_CONF_NAK, CP_CONF_REJ, CP_TERM_REQ,
      CP_TERM_ACK, CP_CODE_REJ, LCP_PROTO_REJ, LCP_ECHO_REQ, LCP_ECHO_REPLY,
      LCP_DISC_REQ, CP_CODES};
#if DEBUG_CP
static const char *const code_names[CP_CODES] = {
	"0", "ConfReq", "ConfAck", "ConfNak", "ConfRej", "TermReq",
	"TermAck", "CodeRej", "ProtoRej", "EchoReq", "EchoReply", "Discard"
};

static char debug_buffer[64 + 3 * DEBUG_CP];
#endif

enum {LCP_OPTION_MRU = 1, LCP_OPTION_ACCM, LCP_OPTION_MAGIC = 5};

struct hdlc_header {
	u8 address;
	u8 control;
	__be16 protocol;
};

struct cp_header {
	u8 code;
	u8 id;
	__be16 len;
};

struct proto {
	struct net_device *dev;
	struct timer_list timer;
	unsigned long timeout;
	u16 pid;		/* protocol ID */
	u8 state;
	u8 cr_id;		/* ID of last Configuration-Request */
	u8 restart_counter;
};

struct ppp {
	struct proto protos[IDX_COUNT];
	spinlock_t lock;
	unsigned long last_pong;
	unsigned int req_timeout, cr_retries, term_retries;
	unsigned int keepalive_interval, keepalive_timeout;
	u8 seq;			/* local sequence number for requests */
	u8 echo_id;		/* ID of last Echo-Request (LCP) */
};

enum {CLOSED = 0, STOPPED, STOPPING, REQ_SENT, ACK_RECV, ACK_SENT, OPENED,
      STATES, STATE_MASK = 0xF};
enum {START = 0, STOP, TO_GOOD, TO_BAD, RCR_GOOD, RCR_BAD, RCA, RCN, RTR, RTA,
      RUC, RXJ_GOOD, RXJ_BAD, EVENTS};
enum {INV = 0x10, IRC = 0x20, ZRC = 0x40, SCR = 0x80, SCA = 0x100,
      SCN = 0x200, STR = 0x400, STA = 0x800, SCJ = 0x1000};

#if DEBUG_STATE
static const char *const state_names[STATES] = {
	"Closed", "Stopped", "Stopping", "ReqSent", "AckRecv", "AckSent",
	"Opened"
};

static const char *const event_names[EVENTS] = {
	"Start", "Stop", "TO+", "TO-", "RCR+", "RCR-", "RCA", "RCN",
	"RTR", "RTA", "RUC", "RXJ+", "RXJ-"
};
#endif

static struct sk_buff_head tx_queue; /* used when holding the spin lock */

static int ppp_ioctl(struct net_device *dev, struct if_settings *ifs);

static inline struct ppp *get_ppp(struct net_device *dev)
{
	return (struct ppp *)dev_to_hdlc(dev)->state;
}

static inline struct proto *get_proto(struct net_device *dev, u16 pid)
{
	struct ppp *ppp = get_ppp(dev);

	switch (pid) {
	case PID_LCP:
		return &ppp->protos[IDX_LCP];
	case PID_IPCP:
		return &ppp->protos[IDX_IPCP];
	case PID_IPV6CP:
		return &ppp->protos[IDX_IPV6CP];
	default:
		return NULL;
	}
}

static inline const char *proto_name(u16 pid)
{
	switch (pid) {
	case PID_LCP:
		return "LCP";
	case PID_IPCP:
		return "IPCP";
	case PID_IPV6CP:
		return "IPV6CP";
	default:
		return NULL;
	}
}

static __be16 ppp_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	struct hdlc_header *data = (struct hdlc_header *)skb->data;

	if (skb->len < sizeof(struct hdlc_header))
		return htons(ETH_P_HDLC);
	if (data->address != HDLC_ADDR_ALLSTATIONS ||
	    data->control != HDLC_CTRL_UI)
		return htons(ETH_P_HDLC);

	switch (data->protocol) {
	case cpu_to_be16(PID_IP):
		skb_pull(skb, sizeof(struct hdlc_header));
		return htons(ETH_P_IP);

	case cpu_to_be16(PID_IPV6):
		skb_pull(skb, sizeof(struct hdlc_header));
		return htons(ETH_P_IPV6);

	default:
		return htons(ETH_P_HDLC);
	}
}

static int ppp_hard_header(struct sk_buff *skb, struct net_device *dev,
			   u16 type, const void *daddr, const void *saddr,
			   unsigned int len)
{
	struct hdlc_header *data;
#if DEBUG_HARD_HEADER
	printk(KERN_DEBUG "%s: ppp_hard_header() called\n", dev->name);
#endif

	skb_push(skb, sizeof(struct hdlc_header));
	data = (struct hdlc_header *)skb->data;

	data->address = HDLC_ADDR_ALLSTATIONS;
	data->control = HDLC_CTRL_UI;
	switch (type) {
	case ETH_P_IP:
		data->protocol = htons(PID_IP);
		break;
	case ETH_P_IPV6:
		data->protocol = htons(PID_IPV6);
		break;
	case PID_LCP:
	case PID_IPCP:
	case PID_IPV6CP:
		data->protocol = htons(type);
		break;
	default:		/* unknown protocol */
		data->protocol = 0;
	}
	return sizeof(struct hdlc_header);
}

static void ppp_tx_flush(void)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&tx_queue)) != NULL)
		dev_queue_xmit(skb);
}

static void ppp_tx_cp(struct net_device *dev, u16 pid, u8 code,
		      u8 id, unsigned int len, const void *data)
{
	struct sk_buff *skb;
	struct cp_header *cp;
	unsigned int magic_len = 0;
	static u32 magic;

#if DEBUG_CP
	int i;
	char *ptr;
#endif

	if (pid == PID_LCP && (code == LCP_ECHO_REQ || code == LCP_ECHO_REPLY))
		magic_len = sizeof(magic);

	skb = dev_alloc_skb(sizeof(struct hdlc_header) +
			    sizeof(struct cp_header) + magic_len + len);
	if (!skb)
		return;

	skb_reserve(skb, sizeof(struct hdlc_header));

	cp = skb_put(skb, sizeof(struct cp_header));
	cp->code = code;
	cp->id = id;
	cp->len = htons(sizeof(struct cp_header) + magic_len + len);

	if (magic_len)
		skb_put_data(skb, &magic, magic_len);
	if (len)
		skb_put_data(skb, data, len);

#if DEBUG_CP
	BUG_ON(code >= CP_CODES);
	ptr = debug_buffer;
	*ptr = '\x0';
	for (i = 0; i < min_t(unsigned int, magic_len + len, DEBUG_CP); i++) {
		sprintf(ptr, " %02X", skb->data[sizeof(struct cp_header) + i]);
		ptr += strlen(ptr);
	}
	printk(KERN_DEBUG "%s: TX %s [%s id 0x%X]%s\n", dev->name,
	       proto_name(pid), code_names[code], id, debug_buffer);
#endif

	ppp_hard_header(skb, dev, pid, NULL, NULL, 0);

	skb->priority = TC_PRIO_CONTROL;
	skb->dev = dev;
	skb->protocol = htons(ETH_P_HDLC);
	skb_reset_network_header(skb);
	skb_queue_tail(&tx_queue, skb);
}

/* State transition table (compare STD-51)
   Events                                   Actions
   TO+  = Timeout with counter > 0          irc = Initialize-Restart-Count
   TO-  = Timeout with counter expired      zrc = Zero-Restart-Count

   RCR+ = Receive-Configure-Request (Good)  scr = Send-Configure-Request
   RCR- = Receive-Configure-Request (Bad)
   RCA  = Receive-Configure-Ack             sca = Send-Configure-Ack
   RCN  = Receive-Configure-Nak/Rej         scn = Send-Configure-Nak/Rej

   RTR  = Receive-Terminate-Request         str = Send-Terminate-Request
   RTA  = Receive-Terminate-Ack             sta = Send-Terminate-Ack

   RUC  = Receive-Unknown-Code              scj = Send-Code-Reject
   RXJ+ = Receive-Code-Reject (permitted)
       or Receive-Protocol-Reject
   RXJ- = Receive-Code-Reject (catastrophic)
       or Receive-Protocol-Reject
*/
static int cp_table[EVENTS][STATES] = {
	/* CLOSED     STOPPED STOPPING REQ_SENT ACK_RECV ACK_SENT OPENED
	     0           1         2       3       4      5          6    */
	{IRC|SCR|3,     INV     , INV ,   INV   , INV ,  INV    ,   INV   }, /* START */
	{   INV   ,      0      ,  0  ,    0    ,  0  ,   0     ,    0    }, /* STOP */
	{   INV   ,     INV     ,STR|2,  SCR|3  ,SCR|3,  SCR|5  ,   INV   }, /* TO+ */
	{   INV   ,     INV     ,  1  ,    1    ,  1  ,    1    ,   INV   }, /* TO- */
	{  STA|0  ,IRC|SCR|SCA|5,  2  ,  SCA|5  ,SCA|6,  SCA|5  ,SCR|SCA|5}, /* RCR+ */
	{  STA|0  ,IRC|SCR|SCN|3,  2  ,  SCN|3  ,SCN|4,  SCN|3  ,SCR|SCN|3}, /* RCR- */
	{  STA|0  ,    STA|1    ,  2  ,  IRC|4  ,SCR|3,    6    , SCR|3   }, /* RCA */
	{  STA|0  ,    STA|1    ,  2  ,IRC|SCR|3,SCR|3,IRC|SCR|5, SCR|3   }, /* RCN */
	{  STA|0  ,    STA|1    ,STA|2,  STA|3  ,STA|3,  STA|3  ,ZRC|STA|2}, /* RTR */
	{    0    ,      1      ,  1  ,    3    ,  3  ,    5    ,  SCR|3  }, /* RTA */
	{  SCJ|0  ,    SCJ|1    ,SCJ|2,  SCJ|3  ,SCJ|4,  SCJ|5  ,  SCJ|6  }, /* RUC */
	{    0    ,      1      ,  2  ,    3    ,  3  ,    5    ,    6    }, /* RXJ+ */
	{    0    ,      1      ,  1  ,    1    ,  1  ,    1    ,IRC|STR|2}, /* RXJ- */
};

/* SCA: RCR+ must supply id, len and data
   SCN: RCR- must supply code, id, len and data
   STA: RTR must supply id
   SCJ: RUC must supply CP packet len and data */
static void ppp_cp_event(struct net_device *dev, u16 pid, u16 event, u8 code,
			 u8 id, unsigned int len, const void *data)
{
	int old_state, action;
	struct ppp *ppp = get_ppp(dev);
	struct proto *proto = get_proto(dev, pid);

	old_state = proto->state;
	BUG_ON(old_state >= STATES);
	BUG_ON(event >= EVENTS);

#if DEBUG_STATE
	printk(KERN_DEBUG "%s: %s ppp_cp_event(%s) %s ...\n", dev->name,
	       proto_name(pid), event_names[event], state_names[proto->state]);
#endif

	action = cp_table[event][old_state];

	proto->state = action & STATE_MASK;
	if (action & (SCR | STR)) /* set Configure-Req/Terminate-Req timer */
		mod_timer(&proto->timer, proto->timeout =
			  jiffies + ppp->req_timeout * HZ);
	if (action & ZRC)
		proto->restart_counter = 0;
	if (action & IRC)
		proto->restart_counter = (proto->state == STOPPING) ?
			ppp->term_retries : ppp->cr_retries;

	if (action & SCR)	/* send Configure-Request */
		ppp_tx_cp(dev, pid, CP_CONF_REQ, proto->cr_id = ++ppp->seq,
			  0, NULL);
	if (action & SCA)	/* send Configure-Ack */
		ppp_tx_cp(dev, pid, CP_CONF_ACK, id, len, data);
	if (action & SCN)	/* send Configure-Nak/Reject */
		ppp_tx_cp(dev, pid, code, id, len, data);
	if (action & STR)	/* send Terminate-Request */
		ppp_tx_cp(dev, pid, CP_TERM_REQ, ++ppp->seq, 0, NULL);
	if (action & STA)	/* send Terminate-Ack */
		ppp_tx_cp(dev, pid, CP_TERM_ACK, id, 0, NULL);
	if (action & SCJ)	/* send Code-Reject */
		ppp_tx_cp(dev, pid, CP_CODE_REJ, ++ppp->seq, len, data);

	if (old_state != OPENED && proto->state == OPENED) {
		netdev_info(dev, "%s up\n", proto_name(pid));
		if (pid == PID_LCP) {
			netif_dormant_off(dev);
			ppp_cp_event(dev, PID_IPCP, START, 0, 0, 0, NULL);
			ppp_cp_event(dev, PID_IPV6CP, START, 0, 0, 0, NULL);
			ppp->last_pong = jiffies;
			mod_timer(&proto->timer, proto->timeout =
				  jiffies + ppp->keepalive_interval * HZ);
		}
	}
	if (old_state == OPENED && proto->state != OPENED) {
		netdev_info(dev, "%s down\n", proto_name(pid));
		if (pid == PID_LCP) {
			netif_dormant_on(dev);
			ppp_cp_event(dev, PID_IPCP, STOP, 0, 0, 0, NULL);
			ppp_cp_event(dev, PID_IPV6CP, STOP, 0, 0, 0, NULL);
		}
	}
	if (old_state != CLOSED && proto->state == CLOSED)
		timer_delete(&proto->timer);

#if DEBUG_STATE
	printk(KERN_DEBUG "%s: %s ppp_cp_event(%s) ... %s\n", dev->name,
	       proto_name(pid), event_names[event], state_names[proto->state]);
#endif
}

static void ppp_cp_parse_cr(struct net_device *dev, u16 pid, u8 id,
			    unsigned int req_len, const u8 *data)
{
	static u8 const valid_accm[6] = { LCP_OPTION_ACCM, 6, 0, 0, 0, 0 };
	const u8 *opt;
	u8 *out;
	unsigned int len = req_len, nak_len = 0, rej_len = 0;

	out = kmalloc(len, GFP_ATOMIC);
	if (!out) {
		dev->stats.rx_dropped++;
		return;	/* out of memory, ignore CR packet */
	}

	for (opt = data; len; len -= opt[1], opt += opt[1]) {
		if (len < 2 || opt[1] < 2 || len < opt[1])
			goto err_out;

		if (pid == PID_LCP)
			switch (opt[0]) {
			case LCP_OPTION_MRU:
				continue; /* MRU always OK and > 1500 bytes? */

			case LCP_OPTION_ACCM: /* async control character map */
				if (opt[1] < sizeof(valid_accm))
					goto err_out;
				if (!memcmp(opt, valid_accm,
					    sizeof(valid_accm)))
					continue;
				if (!rej_len) { /* NAK it */
					memcpy(out + nak_len, valid_accm,
					       sizeof(valid_accm));
					nak_len += sizeof(valid_accm);
					continue;
				}
				break;
			case LCP_OPTION_MAGIC:
				if (len < 6)
					goto err_out;
				if (opt[1] != 6 || (!opt[2] && !opt[3] &&
						    !opt[4] && !opt[5]))
					break; /* reject invalid magic number */
				continue;
			}
		/* reject this option */
		memcpy(out + rej_len, opt, opt[1]);
		rej_len += opt[1];
	}

	if (rej_len)
		ppp_cp_event(dev, pid, RCR_BAD, CP_CONF_REJ, id, rej_len, out);
	else if (nak_len)
		ppp_cp_event(dev, pid, RCR_BAD, CP_CONF_NAK, id, nak_len, out);
	else
		ppp_cp_event(dev, pid, RCR_GOOD, CP_CONF_ACK, id, req_len, data);

	kfree(out);
	return;

err_out:
	dev->stats.rx_errors++;
	kfree(out);
}

static int ppp_rx(struct sk_buff *skb)
{
	struct hdlc_header *hdr = (struct hdlc_header *)skb->data;
	struct net_device *dev = skb->dev;
	struct ppp *ppp = get_ppp(dev);
	struct proto *proto;
	struct cp_header *cp;
	unsigned long flags;
	unsigned int len;
	u16 pid;
#if DEBUG_CP
	int i;
	char *ptr;
#endif

	spin_lock_irqsave(&ppp->lock, flags);
	/* Check HDLC header */
	if (skb->len < sizeof(struct hdlc_header))
		goto rx_error;
	cp = skb_pull(skb, sizeof(struct hdlc_header));
	if (hdr->address != HDLC_ADDR_ALLSTATIONS ||
	    hdr->control != HDLC_CTRL_UI)
		goto rx_error;

	pid = ntohs(hdr->protocol);
	proto = get_proto(dev, pid);
	if (!proto) {
		if (ppp->protos[IDX_LCP].state == OPENED)
			ppp_tx_cp(dev, PID_LCP, LCP_PROTO_REJ,
				  ++ppp->seq, skb->len + 2, &hdr->protocol);
		goto rx_error;
	}

	len = ntohs(cp->len);
	if (len < sizeof(struct cp_header) /* no complete CP header? */ ||
	    skb->len < len /* truncated packet? */)
		goto rx_error;
	skb_pull(skb, sizeof(struct cp_header));
	len -= sizeof(struct cp_header);

	/* HDLC and CP headers stripped from skb */
#if DEBUG_CP
	if (cp->code < CP_CODES)
		sprintf(debug_buffer, "[%s id 0x%X]", code_names[cp->code],
			cp->id);
	else
		sprintf(debug_buffer, "[code %u id 0x%X]", cp->code, cp->id);
	ptr = debug_buffer + strlen(debug_buffer);
	for (i = 0; i < min_t(unsigned int, len, DEBUG_CP); i++) {
		sprintf(ptr, " %02X", skb->data[i]);
		ptr += strlen(ptr);
	}
	printk(KERN_DEBUG "%s: RX %s %s\n", dev->name, proto_name(pid),
	       debug_buffer);
#endif

	/* LCP only */
	if (pid == PID_LCP)
		switch (cp->code) {
		case LCP_PROTO_REJ:
			pid = ntohs(*(__be16 *)skb->data);
			if (pid == PID_LCP || pid == PID_IPCP ||
			    pid == PID_IPV6CP)
				ppp_cp_event(dev, pid, RXJ_BAD, 0, 0,
					     0, NULL);
			goto out;

		case LCP_ECHO_REQ: /* send Echo-Reply */
			if (len >= 4 && proto->state == OPENED)
				ppp_tx_cp(dev, PID_LCP, LCP_ECHO_REPLY,
					  cp->id, len - 4, skb->data + 4);
			goto out;

		case LCP_ECHO_REPLY:
			if (cp->id == ppp->echo_id)
				ppp->last_pong = jiffies;
			goto out;

		case LCP_DISC_REQ: /* discard */
			goto out;
		}

	/* LCP, IPCP and IPV6CP */
	switch (cp->code) {
	case CP_CONF_REQ:
		ppp_cp_parse_cr(dev, pid, cp->id, len, skb->data);
		break;

	case CP_CONF_ACK:
		if (cp->id == proto->cr_id)
			ppp_cp_event(dev, pid, RCA, 0, 0, 0, NULL);
		break;

	case CP_CONF_REJ:
	case CP_CONF_NAK:
		if (cp->id == proto->cr_id)
			ppp_cp_event(dev, pid, RCN, 0, 0, 0, NULL);
		break;

	case CP_TERM_REQ:
		ppp_cp_event(dev, pid, RTR, 0, cp->id, 0, NULL);
		break;

	case CP_TERM_ACK:
		ppp_cp_event(dev, pid, RTA, 0, 0, 0, NULL);
		break;

	case CP_CODE_REJ:
		ppp_cp_event(dev, pid, RXJ_BAD, 0, 0, 0, NULL);
		break;

	default:
		len += sizeof(struct cp_header);
		if (len > dev->mtu)
			len = dev->mtu;
		ppp_cp_event(dev, pid, RUC, 0, 0, len, cp);
		break;
	}
	goto out;

rx_error:
	dev->stats.rx_errors++;
out:
	spin_unlock_irqrestore(&ppp->lock, flags);
	dev_kfree_skb_any(skb);
	ppp_tx_flush();
	return NET_RX_DROP;
}

static void ppp_timer(struct timer_list *t)
{
	struct proto *proto = timer_container_of(proto, t, timer);
	struct ppp *ppp = get_ppp(proto->dev);
	unsigned long flags;

	spin_lock_irqsave(&ppp->lock, flags);
	/* mod_timer could be called after we entered this function but
	 * before we got the lock.
	 */
	if (timer_pending(&proto->timer)) {
		spin_unlock_irqrestore(&ppp->lock, flags);
		return;
	}
	switch (proto->state) {
	case STOPPING:
	case REQ_SENT:
	case ACK_RECV:
	case ACK_SENT:
		if (proto->restart_counter) {
			ppp_cp_event(proto->dev, proto->pid, TO_GOOD, 0, 0,
				     0, NULL);
			proto->restart_counter--;
		} else if (netif_carrier_ok(proto->dev))
			ppp_cp_event(proto->dev, proto->pid, TO_GOOD, 0, 0,
				     0, NULL);
		else
			ppp_cp_event(proto->dev, proto->pid, TO_BAD, 0, 0,
				     0, NULL);
		break;

	case OPENED:
		if (proto->pid != PID_LCP)
			break;
		if (time_after(jiffies, ppp->last_pong +
			       ppp->keepalive_timeout * HZ)) {
			netdev_info(proto->dev, "Link down\n");
			ppp_cp_event(proto->dev, PID_LCP, STOP, 0, 0, 0, NULL);
			ppp_cp_event(proto->dev, PID_LCP, START, 0, 0, 0, NULL);
		} else {	/* send keep-alive packet */
			ppp->echo_id = ++ppp->seq;
			ppp_tx_cp(proto->dev, PID_LCP, LCP_ECHO_REQ,
				  ppp->echo_id, 0, NULL);
			proto->timer.expires = jiffies +
				ppp->keepalive_interval * HZ;
			add_timer(&proto->timer);
		}
		break;
	}
	spin_unlock_irqrestore(&ppp->lock, flags);
	ppp_tx_flush();
}

static void ppp_start(struct net_device *dev)
{
	struct ppp *ppp = get_ppp(dev);
	int i;

	for (i = 0; i < IDX_COUNT; i++) {
		struct proto *proto = &ppp->protos[i];

		proto->dev = dev;
		timer_setup(&proto->timer, ppp_timer, 0);
		proto->state = CLOSED;
	}
	ppp->protos[IDX_LCP].pid = PID_LCP;
	ppp->protos[IDX_IPCP].pid = PID_IPCP;
	ppp->protos[IDX_IPV6CP].pid = PID_IPV6CP;

	ppp_cp_event(dev, PID_LCP, START, 0, 0, 0, NULL);
}

static void ppp_stop(struct net_device *dev)
{
	ppp_cp_event(dev, PID_LCP, STOP, 0, 0, 0, NULL);
}

static void ppp_close(struct net_device *dev)
{
	ppp_tx_flush();
}

static struct hdlc_proto proto = {
	.start		= ppp_start,
	.stop		= ppp_stop,
	.close		= ppp_close,
	.type_trans	= ppp_type_trans,
	.ioctl		= ppp_ioctl,
	.netif_rx	= ppp_rx,
	.module		= THIS_MODULE,
};

static const struct header_ops ppp_header_ops = {
	.create = ppp_hard_header,
};

static int ppp_ioctl(struct net_device *dev, struct if_settings *ifs)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	struct ppp *ppp;
	int result;

	switch (ifs->type) {
	case IF_GET_PROTO:
		if (dev_to_hdlc(dev)->proto != &proto)
			return -EINVAL;
		ifs->type = IF_PROTO_PPP;
		return 0; /* return protocol only, no settable parameters */

	case IF_PROTO_PPP:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (dev->flags & IFF_UP)
			return -EBUSY;

		/* no settable parameters */

		result = hdlc->attach(dev, ENCODING_NRZ,
				      PARITY_CRC16_PR1_CCITT);
		if (result)
			return result;

		result = attach_hdlc_protocol(dev, &proto, sizeof(struct ppp));
		if (result)
			return result;

		ppp = get_ppp(dev);
		spin_lock_init(&ppp->lock);
		ppp->req_timeout = 2;
		ppp->cr_retries = 10;
		ppp->term_retries = 2;
		ppp->keepalive_interval = 10;
		ppp->keepalive_timeout = 60;

		dev->hard_header_len = sizeof(struct hdlc_header);
		dev->header_ops = &ppp_header_ops;
		dev->type = ARPHRD_PPP;
		call_netdevice_notifiers(NETDEV_POST_TYPE_CHANGE, dev);
		netif_dormant_on(dev);
		return 0;
	}

	return -EINVAL;
}

static int __init hdlc_ppp_init(void)
{
	skb_queue_head_init(&tx_queue);
	register_hdlc_protocol(&proto);
	return 0;
}

static void __exit hdlc_ppp_exit(void)
{
	unregister_hdlc_protocol(&proto);
}

module_init(hdlc_ppp_init);
module_exit(hdlc_ppp_exit);

MODULE_AUTHOR("Krzysztof Halasa <khc@pm.waw.pl>");
MODULE_DESCRIPTION("PPP protocol support for generic HDLC");
MODULE_LICENSE("GPL v2");
