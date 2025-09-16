// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic HDLC support routines for Linux
 * Frame Relay support
 *
 * Copyright (C) 1999 - 2006 Krzysztof Halasa <khc@pm.waw.pl>
 *

	Theory of PVC state

 DCE mode:

 (exist,new) -> 0,0 when "PVC create" or if "link unreliable"
	 0,x -> 1,1 if "link reliable" when sending FULL STATUS
	 1,1 -> 1,0 if received FULL STATUS ACK

 (active)    -> 0 when "ifconfig PVC down" or "link unreliable" or "PVC create"
	     -> 1 when "PVC up" and (exist,new) = 1,0

 DTE mode:
 (exist,new,active) = FULL STATUS if "link reliable"
		    = 0, 0, 0 if "link unreliable"
 No LMI:
 active = open and "link reliable"
 exist = new = not used

 CCITT LMI: ITU-T Q.933 Annex A
 ANSI LMI: ANSI T1.617 Annex D
 CISCO LMI: the original, aka "Gang of Four" LMI

*/

#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/hdlc.h>
#include <linux/if_arp.h>
#include <linux/inetdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pkt_sched.h>
#include <linux/poll.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/slab.h>

#undef DEBUG_PKT
#undef DEBUG_ECN
#undef DEBUG_LINK
#undef DEBUG_PROTO
#undef DEBUG_PVC

#define FR_UI			0x03
#define FR_PAD			0x00

#define NLPID_IP		0xCC
#define NLPID_IPV6		0x8E
#define NLPID_SNAP		0x80
#define NLPID_PAD		0x00
#define NLPID_CCITT_ANSI_LMI	0x08
#define NLPID_CISCO_LMI		0x09

#define LMI_CCITT_ANSI_DLCI	   0 /* LMI DLCI */
#define LMI_CISCO_DLCI		1023

#define LMI_CALLREF		0x00 /* Call Reference */
#define LMI_ANSI_LOCKSHIFT	0x95 /* ANSI locking shift */
#define LMI_ANSI_CISCO_REPTYPE	0x01 /* report type */
#define LMI_CCITT_REPTYPE	0x51
#define LMI_ANSI_CISCO_ALIVE	0x03 /* keep alive */
#define LMI_CCITT_ALIVE		0x53
#define LMI_ANSI_CISCO_PVCSTAT	0x07 /* PVC status */
#define LMI_CCITT_PVCSTAT	0x57

#define LMI_FULLREP		0x00 /* full report  */
#define LMI_INTEGRITY		0x01 /* link integrity report */
#define LMI_SINGLE		0x02 /* single PVC report */

#define LMI_STATUS_ENQUIRY      0x75
#define LMI_STATUS              0x7D /* reply */

#define LMI_REPT_LEN               1 /* report type element length */
#define LMI_INTEG_LEN              2 /* link integrity element length */

#define LMI_CCITT_CISCO_LENGTH	  13 /* LMI frame lengths */
#define LMI_ANSI_LENGTH		  14

struct fr_hdr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned ea1:	1;
	unsigned cr:	1;
	unsigned dlcih:	6;

	unsigned ea2:	1;
	unsigned de:	1;
	unsigned becn:	1;
	unsigned fecn:	1;
	unsigned dlcil:	4;
#else
	unsigned dlcih:	6;
	unsigned cr:	1;
	unsigned ea1:	1;

	unsigned dlcil:	4;
	unsigned fecn:	1;
	unsigned becn:	1;
	unsigned de:	1;
	unsigned ea2:	1;
#endif
} __packed;

struct pvc_device {
	struct net_device *frad;
	struct net_device *main;
	struct net_device *ether;	/* bridged Ethernet interface	*/
	struct pvc_device *next;	/* Sorted in ascending DLCI order */
	int dlci;
	int open_count;

	struct {
		unsigned int new: 1;
		unsigned int active: 1;
		unsigned int exist: 1;
		unsigned int deleted: 1;
		unsigned int fecn: 1;
		unsigned int becn: 1;
		unsigned int bandwidth;	/* Cisco LMI reporting only */
	} state;
};

struct frad_state {
	fr_proto settings;
	struct pvc_device *first_pvc;
	int dce_pvc_count;

	struct timer_list timer;
	struct net_device *dev;
	unsigned long last_poll;
	int reliable;
	int dce_changed;
	int request;
	int fullrep_sent;
	u32 last_errors; /* last errors bit list */
	u8 n391cnt;
	u8 txseq; /* TX sequence number */
	u8 rxseq; /* RX sequence number */
};

static int fr_ioctl(struct net_device *dev, struct if_settings *ifs);

static inline u16 q922_to_dlci(u8 *hdr)
{
	return ((hdr[0] & 0xFC) << 2) | ((hdr[1] & 0xF0) >> 4);
}

static inline void dlci_to_q922(u8 *hdr, u16 dlci)
{
	hdr[0] = (dlci >> 2) & 0xFC;
	hdr[1] = ((dlci << 4) & 0xF0) | 0x01;
}

static inline struct frad_state *state(hdlc_device *hdlc)
{
	return (struct frad_state *)(hdlc->state);
}

static inline struct pvc_device *find_pvc(hdlc_device *hdlc, u16 dlci)
{
	struct pvc_device *pvc = state(hdlc)->first_pvc;

	while (pvc) {
		if (pvc->dlci == dlci)
			return pvc;
		if (pvc->dlci > dlci)
			return NULL; /* the list is sorted */
		pvc = pvc->next;
	}

	return NULL;
}

static struct pvc_device *add_pvc(struct net_device *dev, u16 dlci)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	struct pvc_device *pvc, **pvc_p = &state(hdlc)->first_pvc;

	while (*pvc_p) {
		if ((*pvc_p)->dlci == dlci)
			return *pvc_p;
		if ((*pvc_p)->dlci > dlci)
			break;	/* the list is sorted */
		pvc_p = &(*pvc_p)->next;
	}

	pvc = kzalloc(sizeof(*pvc), GFP_ATOMIC);
#ifdef DEBUG_PVC
	printk(KERN_DEBUG "add_pvc: allocated pvc %p, frad %p\n", pvc, dev);
#endif
	if (!pvc)
		return NULL;

	pvc->dlci = dlci;
	pvc->frad = dev;
	pvc->next = *pvc_p;	/* Put it in the chain */
	*pvc_p = pvc;
	return pvc;
}

static inline int pvc_is_used(struct pvc_device *pvc)
{
	return pvc->main || pvc->ether;
}

static inline void pvc_carrier(int on, struct pvc_device *pvc)
{
	if (on) {
		if (pvc->main)
			if (!netif_carrier_ok(pvc->main))
				netif_carrier_on(pvc->main);
		if (pvc->ether)
			if (!netif_carrier_ok(pvc->ether))
				netif_carrier_on(pvc->ether);
	} else {
		if (pvc->main)
			if (netif_carrier_ok(pvc->main))
				netif_carrier_off(pvc->main);
		if (pvc->ether)
			if (netif_carrier_ok(pvc->ether))
				netif_carrier_off(pvc->ether);
	}
}

static inline void delete_unused_pvcs(hdlc_device *hdlc)
{
	struct pvc_device **pvc_p = &state(hdlc)->first_pvc;

	while (*pvc_p) {
		if (!pvc_is_used(*pvc_p)) {
			struct pvc_device *pvc = *pvc_p;
#ifdef DEBUG_PVC
			printk(KERN_DEBUG "freeing unused pvc: %p\n", pvc);
#endif
			*pvc_p = pvc->next;
			kfree(pvc);
			continue;
		}
		pvc_p = &(*pvc_p)->next;
	}
}

static inline struct net_device **get_dev_p(struct pvc_device *pvc,
					    int type)
{
	if (type == ARPHRD_ETHER)
		return &pvc->ether;
	else
		return &pvc->main;
}

static int fr_hard_header(struct sk_buff *skb, u16 dlci)
{
	if (!skb->dev) { /* Control packets */
		switch (dlci) {
		case LMI_CCITT_ANSI_DLCI:
			skb_push(skb, 4);
			skb->data[3] = NLPID_CCITT_ANSI_LMI;
			break;

		case LMI_CISCO_DLCI:
			skb_push(skb, 4);
			skb->data[3] = NLPID_CISCO_LMI;
			break;

		default:
			return -EINVAL;
		}

	} else if (skb->dev->type == ARPHRD_DLCI) {
		switch (skb->protocol) {
		case htons(ETH_P_IP):
			skb_push(skb, 4);
			skb->data[3] = NLPID_IP;
			break;

		case htons(ETH_P_IPV6):
			skb_push(skb, 4);
			skb->data[3] = NLPID_IPV6;
			break;

		default:
			skb_push(skb, 10);
			skb->data[3] = FR_PAD;
			skb->data[4] = NLPID_SNAP;
			/* OUI 00-00-00 indicates an Ethertype follows */
			skb->data[5] = 0x00;
			skb->data[6] = 0x00;
			skb->data[7] = 0x00;
			/* This should be an Ethertype: */
			*(__be16 *)(skb->data + 8) = skb->protocol;
		}

	} else if (skb->dev->type == ARPHRD_ETHER) {
		skb_push(skb, 10);
		skb->data[3] = FR_PAD;
		skb->data[4] = NLPID_SNAP;
		/* OUI 00-80-C2 stands for the 802.1 organization */
		skb->data[5] = 0x00;
		skb->data[6] = 0x80;
		skb->data[7] = 0xC2;
		/* PID 00-07 stands for Ethernet frames without FCS */
		skb->data[8] = 0x00;
		skb->data[9] = 0x07;

	} else {
		return -EINVAL;
	}

	dlci_to_q922(skb->data, dlci);
	skb->data[2] = FR_UI;
	return 0;
}

static int pvc_open(struct net_device *dev)
{
	struct pvc_device *pvc = dev->ml_priv;

	if ((pvc->frad->flags & IFF_UP) == 0)
		return -EIO;  /* Frad must be UP in order to activate PVC */

	if (pvc->open_count++ == 0) {
		hdlc_device *hdlc = dev_to_hdlc(pvc->frad);

		if (state(hdlc)->settings.lmi == LMI_NONE)
			pvc->state.active = netif_carrier_ok(pvc->frad);

		pvc_carrier(pvc->state.active, pvc);
		state(hdlc)->dce_changed = 1;
	}
	return 0;
}

static int pvc_close(struct net_device *dev)
{
	struct pvc_device *pvc = dev->ml_priv;

	if (--pvc->open_count == 0) {
		hdlc_device *hdlc = dev_to_hdlc(pvc->frad);

		if (state(hdlc)->settings.lmi == LMI_NONE)
			pvc->state.active = 0;

		if (state(hdlc)->settings.dce) {
			state(hdlc)->dce_changed = 1;
			pvc->state.active = 0;
		}
	}
	return 0;
}

static int pvc_ioctl(struct net_device *dev, struct if_settings *ifs)
{
	struct pvc_device *pvc = dev->ml_priv;
	fr_proto_pvc_info info;

	if (ifs->type == IF_GET_PROTO) {
		if (dev->type == ARPHRD_ETHER)
			ifs->type = IF_PROTO_FR_ETH_PVC;
		else
			ifs->type = IF_PROTO_FR_PVC;

		if (ifs->size < sizeof(info)) {
			/* data size wanted */
			ifs->size = sizeof(info);
			return -ENOBUFS;
		}

		info.dlci = pvc->dlci;
		memcpy(info.master, pvc->frad->name, IFNAMSIZ);
		if (copy_to_user(ifs->ifs_ifsu.fr_pvc_info,
				 &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}

	return -EINVAL;
}

static netdev_tx_t pvc_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct pvc_device *pvc = dev->ml_priv;

	if (!pvc->state.active)
		goto drop;

	if (dev->type == ARPHRD_ETHER) {
		int pad = ETH_ZLEN - skb->len;

		if (pad > 0) { /* Pad the frame with zeros */
			if (__skb_pad(skb, pad, false))
				goto drop;
			skb_put(skb, pad);
		}
	}

	/* We already requested the header space with dev->needed_headroom.
	 * So this is just a protection in case the upper layer didn't take
	 * dev->needed_headroom into consideration.
	 */
	if (skb_headroom(skb) < 10) {
		struct sk_buff *skb2 = skb_realloc_headroom(skb, 10);

		if (!skb2)
			goto drop;
		dev_kfree_skb(skb);
		skb = skb2;
	}

	skb->dev = dev;
	if (fr_hard_header(skb, pvc->dlci))
		goto drop;

	dev->stats.tx_bytes += skb->len;
	dev->stats.tx_packets++;
	if (pvc->state.fecn) /* TX Congestion counter */
		dev->stats.tx_compressed++;
	skb->dev = pvc->frad;
	skb->protocol = htons(ETH_P_HDLC);
	skb_reset_network_header(skb);
	dev_queue_xmit(skb);
	return NETDEV_TX_OK;

drop:
	dev->stats.tx_dropped++;
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static inline void fr_log_dlci_active(struct pvc_device *pvc)
{
	netdev_info(pvc->frad, "DLCI %d [%s%s%s]%s %s\n",
		    pvc->dlci,
		    pvc->main ? pvc->main->name : "",
		    pvc->main && pvc->ether ? " " : "",
		    pvc->ether ? pvc->ether->name : "",
		    pvc->state.new ? " new" : "",
		    !pvc->state.exist ? "deleted" :
		    pvc->state.active ? "active" : "inactive");
}

static inline u8 fr_lmi_nextseq(u8 x)
{
	x++;
	return x ? x : 1;
}

static void fr_lmi_send(struct net_device *dev, int fullrep)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	struct sk_buff *skb;
	struct pvc_device *pvc = state(hdlc)->first_pvc;
	int lmi = state(hdlc)->settings.lmi;
	int dce = state(hdlc)->settings.dce;
	int len = lmi == LMI_ANSI ? LMI_ANSI_LENGTH : LMI_CCITT_CISCO_LENGTH;
	int stat_len = (lmi == LMI_CISCO) ? 6 : 3;
	u8 *data;
	int i = 0;

	if (dce && fullrep) {
		len += state(hdlc)->dce_pvc_count * (2 + stat_len);
		if (len > HDLC_MAX_MRU) {
			netdev_warn(dev, "Too many PVCs while sending LMI full report\n");
			return;
		}
	}

	skb = dev_alloc_skb(len);
	if (!skb)
		return;

	memset(skb->data, 0, len);
	skb_reserve(skb, 4);
	if (lmi == LMI_CISCO)
		fr_hard_header(skb, LMI_CISCO_DLCI);
	else
		fr_hard_header(skb, LMI_CCITT_ANSI_DLCI);

	data = skb_tail_pointer(skb);
	data[i++] = LMI_CALLREF;
	data[i++] = dce ? LMI_STATUS : LMI_STATUS_ENQUIRY;
	if (lmi == LMI_ANSI)
		data[i++] = LMI_ANSI_LOCKSHIFT;
	data[i++] = lmi == LMI_CCITT ? LMI_CCITT_REPTYPE :
		LMI_ANSI_CISCO_REPTYPE;
	data[i++] = LMI_REPT_LEN;
	data[i++] = fullrep ? LMI_FULLREP : LMI_INTEGRITY;
	data[i++] = lmi == LMI_CCITT ? LMI_CCITT_ALIVE : LMI_ANSI_CISCO_ALIVE;
	data[i++] = LMI_INTEG_LEN;
	data[i++] = state(hdlc)->txseq =
		fr_lmi_nextseq(state(hdlc)->txseq);
	data[i++] = state(hdlc)->rxseq;

	if (dce && fullrep) {
		while (pvc) {
			data[i++] = lmi == LMI_CCITT ? LMI_CCITT_PVCSTAT :
				LMI_ANSI_CISCO_PVCSTAT;
			data[i++] = stat_len;

			/* LMI start/restart */
			if (state(hdlc)->reliable && !pvc->state.exist) {
				pvc->state.exist = pvc->state.new = 1;
				fr_log_dlci_active(pvc);
			}

			/* ifconfig PVC up */
			if (pvc->open_count && !pvc->state.active &&
			    pvc->state.exist && !pvc->state.new) {
				pvc_carrier(1, pvc);
				pvc->state.active = 1;
				fr_log_dlci_active(pvc);
			}

			if (lmi == LMI_CISCO) {
				data[i] = pvc->dlci >> 8;
				data[i + 1] = pvc->dlci & 0xFF;
			} else {
				data[i] = (pvc->dlci >> 4) & 0x3F;
				data[i + 1] = ((pvc->dlci << 3) & 0x78) | 0x80;
				data[i + 2] = 0x80;
			}

			if (pvc->state.new)
				data[i + 2] |= 0x08;
			else if (pvc->state.active)
				data[i + 2] |= 0x02;

			i += stat_len;
			pvc = pvc->next;
		}
	}

	skb_put(skb, i);
	skb->priority = TC_PRIO_CONTROL;
	skb->dev = dev;
	skb->protocol = htons(ETH_P_HDLC);
	skb_reset_network_header(skb);

	dev_queue_xmit(skb);
}

static void fr_set_link_state(int reliable, struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	struct pvc_device *pvc = state(hdlc)->first_pvc;

	state(hdlc)->reliable = reliable;
	if (reliable) {
		netif_dormant_off(dev);
		state(hdlc)->n391cnt = 0; /* Request full status */
		state(hdlc)->dce_changed = 1;

		if (state(hdlc)->settings.lmi == LMI_NONE) {
			while (pvc) {	/* Activate all PVCs */
				pvc_carrier(1, pvc);
				pvc->state.exist = pvc->state.active = 1;
				pvc->state.new = 0;
				pvc = pvc->next;
			}
		}
	} else {
		netif_dormant_on(dev);
		while (pvc) {		/* Deactivate all PVCs */
			pvc_carrier(0, pvc);
			pvc->state.exist = pvc->state.active = 0;
			pvc->state.new = 0;
			if (!state(hdlc)->settings.dce)
				pvc->state.bandwidth = 0;
			pvc = pvc->next;
		}
	}
}

static void fr_timer(struct timer_list *t)
{
	struct frad_state *st = timer_container_of(st, t, timer);
	struct net_device *dev = st->dev;
	hdlc_device *hdlc = dev_to_hdlc(dev);
	int i, cnt = 0, reliable;
	u32 list;

	if (state(hdlc)->settings.dce) {
		reliable = state(hdlc)->request &&
			time_before(jiffies, state(hdlc)->last_poll +
				    state(hdlc)->settings.t392 * HZ);
		state(hdlc)->request = 0;
	} else {
		state(hdlc)->last_errors <<= 1; /* Shift the list */
		if (state(hdlc)->request) {
			if (state(hdlc)->reliable)
				netdev_info(dev, "No LMI status reply received\n");
			state(hdlc)->last_errors |= 1;
		}

		list = state(hdlc)->last_errors;
		for (i = 0; i < state(hdlc)->settings.n393; i++, list >>= 1)
			cnt += (list & 1);	/* errors count */

		reliable = (cnt < state(hdlc)->settings.n392);
	}

	if (state(hdlc)->reliable != reliable) {
		netdev_info(dev, "Link %sreliable\n", reliable ? "" : "un");
		fr_set_link_state(reliable, dev);
	}

	if (state(hdlc)->settings.dce) {
		state(hdlc)->timer.expires = jiffies +
			state(hdlc)->settings.t392 * HZ;
	} else {
		if (state(hdlc)->n391cnt)
			state(hdlc)->n391cnt--;

		fr_lmi_send(dev, state(hdlc)->n391cnt == 0);

		state(hdlc)->last_poll = jiffies;
		state(hdlc)->request = 1;
		state(hdlc)->timer.expires = jiffies +
			state(hdlc)->settings.t391 * HZ;
	}

	add_timer(&state(hdlc)->timer);
}

static int fr_lmi_recv(struct net_device *dev, struct sk_buff *skb)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	struct pvc_device *pvc;
	u8 rxseq, txseq;
	int lmi = state(hdlc)->settings.lmi;
	int dce = state(hdlc)->settings.dce;
	int stat_len = (lmi == LMI_CISCO) ? 6 : 3, reptype, error, no_ram, i;

	if (skb->len < (lmi == LMI_ANSI ? LMI_ANSI_LENGTH :
			LMI_CCITT_CISCO_LENGTH)) {
		netdev_info(dev, "Short LMI frame\n");
		return 1;
	}

	if (skb->data[3] != (lmi == LMI_CISCO ? NLPID_CISCO_LMI :
			     NLPID_CCITT_ANSI_LMI)) {
		netdev_info(dev, "Received non-LMI frame with LMI DLCI\n");
		return 1;
	}

	if (skb->data[4] != LMI_CALLREF) {
		netdev_info(dev, "Invalid LMI Call reference (0x%02X)\n",
			    skb->data[4]);
		return 1;
	}

	if (skb->data[5] != (dce ? LMI_STATUS_ENQUIRY : LMI_STATUS)) {
		netdev_info(dev, "Invalid LMI Message type (0x%02X)\n",
			    skb->data[5]);
		return 1;
	}

	if (lmi == LMI_ANSI) {
		if (skb->data[6] != LMI_ANSI_LOCKSHIFT) {
			netdev_info(dev, "Not ANSI locking shift in LMI message (0x%02X)\n",
				    skb->data[6]);
			return 1;
		}
		i = 7;
	} else {
		i = 6;
	}

	if (skb->data[i] != (lmi == LMI_CCITT ? LMI_CCITT_REPTYPE :
			     LMI_ANSI_CISCO_REPTYPE)) {
		netdev_info(dev, "Not an LMI Report type IE (0x%02X)\n",
			    skb->data[i]);
		return 1;
	}

	if (skb->data[++i] != LMI_REPT_LEN) {
		netdev_info(dev, "Invalid LMI Report type IE length (%u)\n",
			    skb->data[i]);
		return 1;
	}

	reptype = skb->data[++i];
	if (reptype != LMI_INTEGRITY && reptype != LMI_FULLREP) {
		netdev_info(dev, "Unsupported LMI Report type (0x%02X)\n",
			    reptype);
		return 1;
	}

	if (skb->data[++i] != (lmi == LMI_CCITT ? LMI_CCITT_ALIVE :
			       LMI_ANSI_CISCO_ALIVE)) {
		netdev_info(dev, "Not an LMI Link integrity verification IE (0x%02X)\n",
			    skb->data[i]);
		return 1;
	}

	if (skb->data[++i] != LMI_INTEG_LEN) {
		netdev_info(dev, "Invalid LMI Link integrity verification IE length (%u)\n",
			    skb->data[i]);
		return 1;
	}
	i++;

	state(hdlc)->rxseq = skb->data[i++]; /* TX sequence from peer */
	rxseq = skb->data[i++];	/* Should confirm our sequence */

	txseq = state(hdlc)->txseq;

	if (dce)
		state(hdlc)->last_poll = jiffies;

	error = 0;
	if (!state(hdlc)->reliable)
		error = 1;

	if (rxseq == 0 || rxseq != txseq) { /* Ask for full report next time */
		state(hdlc)->n391cnt = 0;
		error = 1;
	}

	if (dce) {
		if (state(hdlc)->fullrep_sent && !error) {
/* Stop sending full report - the last one has been confirmed by DTE */
			state(hdlc)->fullrep_sent = 0;
			pvc = state(hdlc)->first_pvc;
			while (pvc) {
				if (pvc->state.new) {
					pvc->state.new = 0;

/* Tell DTE that new PVC is now active */
					state(hdlc)->dce_changed = 1;
				}
				pvc = pvc->next;
			}
		}

		if (state(hdlc)->dce_changed) {
			reptype = LMI_FULLREP;
			state(hdlc)->fullrep_sent = 1;
			state(hdlc)->dce_changed = 0;
		}

		state(hdlc)->request = 1; /* got request */
		fr_lmi_send(dev, reptype == LMI_FULLREP ? 1 : 0);
		return 0;
	}

	/* DTE */

	state(hdlc)->request = 0; /* got response, no request pending */

	if (error)
		return 0;

	if (reptype != LMI_FULLREP)
		return 0;

	pvc = state(hdlc)->first_pvc;

	while (pvc) {
		pvc->state.deleted = 1;
		pvc = pvc->next;
	}

	no_ram = 0;
	while (skb->len >= i + 2 + stat_len) {
		u16 dlci;
		u32 bw;
		unsigned int active, new;

		if (skb->data[i] != (lmi == LMI_CCITT ? LMI_CCITT_PVCSTAT :
				       LMI_ANSI_CISCO_PVCSTAT)) {
			netdev_info(dev, "Not an LMI PVC status IE (0x%02X)\n",
				    skb->data[i]);
			return 1;
		}

		if (skb->data[++i] != stat_len) {
			netdev_info(dev, "Invalid LMI PVC status IE length (%u)\n",
				    skb->data[i]);
			return 1;
		}
		i++;

		new = !!(skb->data[i + 2] & 0x08);
		active = !!(skb->data[i + 2] & 0x02);
		if (lmi == LMI_CISCO) {
			dlci = (skb->data[i] << 8) | skb->data[i + 1];
			bw = (skb->data[i + 3] << 16) |
				(skb->data[i + 4] << 8) |
				(skb->data[i + 5]);
		} else {
			dlci = ((skb->data[i] & 0x3F) << 4) |
				((skb->data[i + 1] & 0x78) >> 3);
			bw = 0;
		}

		pvc = add_pvc(dev, dlci);

		if (!pvc && !no_ram) {
			netdev_warn(dev, "Memory squeeze on fr_lmi_recv()\n");
			no_ram = 1;
		}

		if (pvc) {
			pvc->state.exist = 1;
			pvc->state.deleted = 0;
			if (active != pvc->state.active ||
			    new != pvc->state.new ||
			    bw != pvc->state.bandwidth ||
			    !pvc->state.exist) {
				pvc->state.new = new;
				pvc->state.active = active;
				pvc->state.bandwidth = bw;
				pvc_carrier(active, pvc);
				fr_log_dlci_active(pvc);
			}
		}

		i += stat_len;
	}

	pvc = state(hdlc)->first_pvc;

	while (pvc) {
		if (pvc->state.deleted && pvc->state.exist) {
			pvc_carrier(0, pvc);
			pvc->state.active = pvc->state.new = 0;
			pvc->state.exist = 0;
			pvc->state.bandwidth = 0;
			fr_log_dlci_active(pvc);
		}
		pvc = pvc->next;
	}

	/* Next full report after N391 polls */
	state(hdlc)->n391cnt = state(hdlc)->settings.n391;

	return 0;
}

static int fr_snap_parse(struct sk_buff *skb, struct pvc_device *pvc)
{
	/* OUI 00-00-00 indicates an Ethertype follows */
	if (skb->data[0] == 0x00 &&
	    skb->data[1] == 0x00 &&
	    skb->data[2] == 0x00) {
		if (!pvc->main)
			return -1;
		skb->dev = pvc->main;
		skb->protocol = *(__be16 *)(skb->data + 3); /* Ethertype */
		skb_pull(skb, 5);
		skb_reset_mac_header(skb);
		return 0;

	/* OUI 00-80-C2 stands for the 802.1 organization */
	} else if (skb->data[0] == 0x00 &&
		   skb->data[1] == 0x80 &&
		   skb->data[2] == 0xC2) {
		/* PID 00-07 stands for Ethernet frames without FCS */
		if (skb->data[3] == 0x00 &&
		    skb->data[4] == 0x07) {
			if (!pvc->ether)
				return -1;
			skb_pull(skb, 5);
			if (skb->len < ETH_HLEN)
				return -1;
			skb->protocol = eth_type_trans(skb, pvc->ether);
			return 0;

		/* PID unsupported */
		} else {
			return -1;
		}

	/* OUI unsupported */
	} else {
		return -1;
	}
}

static int fr_rx(struct sk_buff *skb)
{
	struct net_device *frad = skb->dev;
	hdlc_device *hdlc = dev_to_hdlc(frad);
	struct fr_hdr *fh = (struct fr_hdr *)skb->data;
	u8 *data = skb->data;
	u16 dlci;
	struct pvc_device *pvc;
	struct net_device *dev;

	if (skb->len < 4 || fh->ea1 || !fh->ea2 || data[2] != FR_UI)
		goto rx_error;

	dlci = q922_to_dlci(skb->data);

	if ((dlci == LMI_CCITT_ANSI_DLCI &&
	     (state(hdlc)->settings.lmi == LMI_ANSI ||
	      state(hdlc)->settings.lmi == LMI_CCITT)) ||
	    (dlci == LMI_CISCO_DLCI &&
	     state(hdlc)->settings.lmi == LMI_CISCO)) {
		if (fr_lmi_recv(frad, skb))
			goto rx_error;
		dev_kfree_skb_any(skb);
		return NET_RX_SUCCESS;
	}

	pvc = find_pvc(hdlc, dlci);
	if (!pvc) {
#ifdef DEBUG_PKT
		netdev_info(frad, "No PVC for received frame's DLCI %d\n",
			    dlci);
#endif
		goto rx_drop;
	}

	if (pvc->state.fecn != fh->fecn) {
#ifdef DEBUG_ECN
		printk(KERN_DEBUG "%s: DLCI %d FECN O%s\n", frad->name,
		       dlci, fh->fecn ? "N" : "FF");
#endif
		pvc->state.fecn ^= 1;
	}

	if (pvc->state.becn != fh->becn) {
#ifdef DEBUG_ECN
		printk(KERN_DEBUG "%s: DLCI %d BECN O%s\n", frad->name,
		       dlci, fh->becn ? "N" : "FF");
#endif
		pvc->state.becn ^= 1;
	}

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb) {
		frad->stats.rx_dropped++;
		return NET_RX_DROP;
	}

	if (data[3] == NLPID_IP) {
		if (!pvc->main)
			goto rx_drop;
		skb_pull(skb, 4); /* Remove 4-byte header (hdr, UI, NLPID) */
		skb->dev = pvc->main;
		skb->protocol = htons(ETH_P_IP);
		skb_reset_mac_header(skb);

	} else if (data[3] == NLPID_IPV6) {
		if (!pvc->main)
			goto rx_drop;
		skb_pull(skb, 4); /* Remove 4-byte header (hdr, UI, NLPID) */
		skb->dev = pvc->main;
		skb->protocol = htons(ETH_P_IPV6);
		skb_reset_mac_header(skb);

	} else if (data[3] == FR_PAD) {
		if (skb->len < 5)
			goto rx_error;
		if (data[4] == NLPID_SNAP) { /* A SNAP header follows */
			skb_pull(skb, 5);
			if (skb->len < 5) /* Incomplete SNAP header */
				goto rx_error;
			if (fr_snap_parse(skb, pvc))
				goto rx_drop;
		} else {
			goto rx_drop;
		}

	} else {
		netdev_info(frad, "Unsupported protocol, NLPID=%x length=%i\n",
			    data[3], skb->len);
		goto rx_drop;
	}

	dev = skb->dev;
	dev->stats.rx_packets++; /* PVC traffic */
	dev->stats.rx_bytes += skb->len;
	if (pvc->state.becn)
		dev->stats.rx_compressed++;
	netif_rx(skb);
	return NET_RX_SUCCESS;

rx_error:
	frad->stats.rx_errors++; /* Mark error */
rx_drop:
	dev_kfree_skb_any(skb);
	return NET_RX_DROP;
}

static void fr_start(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
#ifdef DEBUG_LINK
	printk(KERN_DEBUG "fr_start\n");
#endif
	if (state(hdlc)->settings.lmi != LMI_NONE) {
		state(hdlc)->reliable = 0;
		state(hdlc)->dce_changed = 1;
		state(hdlc)->request = 0;
		state(hdlc)->fullrep_sent = 0;
		state(hdlc)->last_errors = 0xFFFFFFFF;
		state(hdlc)->n391cnt = 0;
		state(hdlc)->txseq = state(hdlc)->rxseq = 0;

		state(hdlc)->dev = dev;
		timer_setup(&state(hdlc)->timer, fr_timer, 0);
		/* First poll after 1 s */
		state(hdlc)->timer.expires = jiffies + HZ;
		add_timer(&state(hdlc)->timer);
	} else {
		fr_set_link_state(1, dev);
	}
}

static void fr_stop(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
#ifdef DEBUG_LINK
	printk(KERN_DEBUG "fr_stop\n");
#endif
	if (state(hdlc)->settings.lmi != LMI_NONE)
		timer_delete_sync(&state(hdlc)->timer);
	fr_set_link_state(0, dev);
}

static void fr_close(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	struct pvc_device *pvc = state(hdlc)->first_pvc;

	while (pvc) {		/* Shutdown all PVCs for this FRAD */
		if (pvc->main)
			dev_close(pvc->main);
		if (pvc->ether)
			dev_close(pvc->ether);
		pvc = pvc->next;
	}
}

static void pvc_setup(struct net_device *dev)
{
	dev->type = ARPHRD_DLCI;
	dev->flags = IFF_POINTOPOINT;
	dev->hard_header_len = 0;
	dev->addr_len = 2;
	netif_keep_dst(dev);
}

static const struct net_device_ops pvc_ops = {
	.ndo_open       = pvc_open,
	.ndo_stop       = pvc_close,
	.ndo_start_xmit = pvc_xmit,
	.ndo_siocwandev = pvc_ioctl,
};

static int fr_add_pvc(struct net_device *frad, unsigned int dlci, int type)
{
	hdlc_device *hdlc = dev_to_hdlc(frad);
	struct pvc_device *pvc;
	struct net_device *dev;
	int used;

	pvc = add_pvc(frad, dlci);
	if (!pvc) {
		netdev_warn(frad, "Memory squeeze on fr_add_pvc()\n");
		return -ENOBUFS;
	}

	if (*get_dev_p(pvc, type))
		return -EEXIST;

	used = pvc_is_used(pvc);

	if (type == ARPHRD_ETHER)
		dev = alloc_netdev(0, "pvceth%d", NET_NAME_UNKNOWN,
				   ether_setup);
	else
		dev = alloc_netdev(0, "pvc%d", NET_NAME_UNKNOWN, pvc_setup);

	if (!dev) {
		netdev_warn(frad, "Memory squeeze on fr_pvc()\n");
		delete_unused_pvcs(hdlc);
		return -ENOBUFS;
	}

	if (type == ARPHRD_ETHER) {
		dev->priv_flags &= ~IFF_TX_SKB_SHARING;
		eth_hw_addr_random(dev);
	} else {
		__be16 addr = htons(dlci);

		dev_addr_set(dev, (u8 *)&addr);
		dlci_to_q922(dev->broadcast, dlci);
	}
	dev->netdev_ops = &pvc_ops;
	dev->mtu = HDLC_MAX_MTU;
	dev->min_mtu = 68;
	dev->max_mtu = HDLC_MAX_MTU;
	dev->needed_headroom = 10;
	dev->priv_flags |= IFF_NO_QUEUE;
	dev->ml_priv = pvc;

	if (register_netdevice(dev) != 0) {
		free_netdev(dev);
		delete_unused_pvcs(hdlc);
		return -EIO;
	}

	dev->needs_free_netdev = true;
	*get_dev_p(pvc, type) = dev;
	if (!used) {
		state(hdlc)->dce_changed = 1;
		state(hdlc)->dce_pvc_count++;
	}
	return 0;
}

static int fr_del_pvc(hdlc_device *hdlc, unsigned int dlci, int type)
{
	struct pvc_device *pvc;
	struct net_device *dev;

	pvc = find_pvc(hdlc, dlci);
	if (!pvc)
		return -ENOENT;

	dev = *get_dev_p(pvc, type);
	if (!dev)
		return -ENOENT;

	if (dev->flags & IFF_UP)
		return -EBUSY;		/* PVC in use */

	unregister_netdevice(dev); /* the destructor will free_netdev(dev) */
	*get_dev_p(pvc, type) = NULL;

	if (!pvc_is_used(pvc)) {
		state(hdlc)->dce_pvc_count--;
		state(hdlc)->dce_changed = 1;
	}
	delete_unused_pvcs(hdlc);
	return 0;
}

static void fr_destroy(struct net_device *frad)
{
	hdlc_device *hdlc = dev_to_hdlc(frad);
	struct pvc_device *pvc = state(hdlc)->first_pvc;

	state(hdlc)->first_pvc = NULL; /* All PVCs destroyed */
	state(hdlc)->dce_pvc_count = 0;
	state(hdlc)->dce_changed = 1;

	while (pvc) {
		struct pvc_device *next = pvc->next;
		/* destructors will free_netdev() main and ether */
		if (pvc->main)
			unregister_netdevice(pvc->main);

		if (pvc->ether)
			unregister_netdevice(pvc->ether);

		kfree(pvc);
		pvc = next;
	}
}

static struct hdlc_proto proto = {
	.close		= fr_close,
	.start		= fr_start,
	.stop		= fr_stop,
	.detach		= fr_destroy,
	.ioctl		= fr_ioctl,
	.netif_rx	= fr_rx,
	.module		= THIS_MODULE,
};

static int fr_ioctl(struct net_device *dev, struct if_settings *ifs)
{
	fr_proto __user *fr_s = ifs->ifs_ifsu.fr;
	const size_t size = sizeof(fr_proto);
	fr_proto new_settings;
	hdlc_device *hdlc = dev_to_hdlc(dev);
	fr_proto_pvc pvc;
	int result;

	switch (ifs->type) {
	case IF_GET_PROTO:
		if (dev_to_hdlc(dev)->proto != &proto) /* Different proto */
			return -EINVAL;
		ifs->type = IF_PROTO_FR;
		if (ifs->size < size) {
			ifs->size = size; /* data size wanted */
			return -ENOBUFS;
		}
		if (copy_to_user(fr_s, &state(hdlc)->settings, size))
			return -EFAULT;
		return 0;

	case IF_PROTO_FR:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (dev->flags & IFF_UP)
			return -EBUSY;

		if (copy_from_user(&new_settings, fr_s, size))
			return -EFAULT;

		if (new_settings.lmi == LMI_DEFAULT)
			new_settings.lmi = LMI_ANSI;

		if ((new_settings.lmi != LMI_NONE &&
		     new_settings.lmi != LMI_ANSI &&
		     new_settings.lmi != LMI_CCITT &&
		     new_settings.lmi != LMI_CISCO) ||
		    new_settings.t391 < 1 ||
		    new_settings.t392 < 2 ||
		    new_settings.n391 < 1 ||
		    new_settings.n392 < 1 ||
		    new_settings.n393 < new_settings.n392 ||
		    new_settings.n393 > 32 ||
		    (new_settings.dce != 0 &&
		     new_settings.dce != 1))
			return -EINVAL;

		result = hdlc->attach(dev, ENCODING_NRZ,
				      PARITY_CRC16_PR1_CCITT);
		if (result)
			return result;

		if (dev_to_hdlc(dev)->proto != &proto) { /* Different proto */
			result = attach_hdlc_protocol(dev, &proto,
						      sizeof(struct frad_state));
			if (result)
				return result;
			state(hdlc)->first_pvc = NULL;
			state(hdlc)->dce_pvc_count = 0;
		}
		memcpy(&state(hdlc)->settings, &new_settings, size);
		dev->type = ARPHRD_FRAD;
		call_netdevice_notifiers(NETDEV_POST_TYPE_CHANGE, dev);
		return 0;

	case IF_PROTO_FR_ADD_PVC:
	case IF_PROTO_FR_DEL_PVC:
	case IF_PROTO_FR_ADD_ETH_PVC:
	case IF_PROTO_FR_DEL_ETH_PVC:
		if (dev_to_hdlc(dev)->proto != &proto) /* Different proto */
			return -EINVAL;

		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (copy_from_user(&pvc, ifs->ifs_ifsu.fr_pvc,
				   sizeof(fr_proto_pvc)))
			return -EFAULT;

		if (pvc.dlci <= 0 || pvc.dlci >= 1024)
			return -EINVAL;	/* Only 10 bits, DLCI 0 reserved */

		if (ifs->type == IF_PROTO_FR_ADD_ETH_PVC ||
		    ifs->type == IF_PROTO_FR_DEL_ETH_PVC)
			result = ARPHRD_ETHER; /* bridged Ethernet device */
		else
			result = ARPHRD_DLCI;

		if (ifs->type == IF_PROTO_FR_ADD_PVC ||
		    ifs->type == IF_PROTO_FR_ADD_ETH_PVC)
			return fr_add_pvc(dev, pvc.dlci, result);
		else
			return fr_del_pvc(hdlc, pvc.dlci, result);
	}

	return -EINVAL;
}

static int __init hdlc_fr_init(void)
{
	register_hdlc_protocol(&proto);
	return 0;
}

static void __exit hdlc_fr_exit(void)
{
	unregister_hdlc_protocol(&proto);
}

module_init(hdlc_fr_init);
module_exit(hdlc_fr_exit);

MODULE_AUTHOR("Krzysztof Halasa <khc@pm.waw.pl>");
MODULE_DESCRIPTION("Frame-Relay protocol support for generic HDLC");
MODULE_LICENSE("GPL v2");
