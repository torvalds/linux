/*
 * Generic HDLC support routines for Linux
 * Frame Relay support
 *
 * Copyright (C) 1999 - 2006 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/pkt_sched.h>
#include <linux/random.h>
#include <linux/inetdevice.h>
#include <linux/lapb.h>
#include <linux/rtnetlink.h>
#include <linux/etherdevice.h>
#include <linux/hdlc.h>

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


typedef struct {
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
}__attribute__ ((packed)) fr_hdr;


typedef struct pvc_device_struct {
	struct net_device *frad;
	struct net_device *main;
	struct net_device *ether;	/* bridged Ethernet interface	*/
	struct pvc_device_struct *next;	/* Sorted in ascending DLCI order */
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
	}state;
}pvc_device;


struct frad_state {
	fr_proto settings;
	pvc_device *first_pvc;
	int dce_pvc_count;

	struct timer_list timer;
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


static int fr_ioctl(struct net_device *dev, struct ifreq *ifr);


static inline u16 q922_to_dlci(u8 *hdr)
{
	return ((hdr[0] & 0xFC) << 2) | ((hdr[1] & 0xF0) >> 4);
}


static inline void dlci_to_q922(u8 *hdr, u16 dlci)
{
	hdr[0] = (dlci >> 2) & 0xFC;
	hdr[1] = ((dlci << 4) & 0xF0) | 0x01;
}


static inline struct frad_state * state(hdlc_device *hdlc)
{
	return(struct frad_state *)(hdlc->state);
}


static __inline__ pvc_device* dev_to_pvc(struct net_device *dev)
{
	return dev->priv;
}


static inline pvc_device* find_pvc(hdlc_device *hdlc, u16 dlci)
{
	pvc_device *pvc = state(hdlc)->first_pvc;

	while (pvc) {
		if (pvc->dlci == dlci)
			return pvc;
		if (pvc->dlci > dlci)
			return NULL; /* the listed is sorted */
		pvc = pvc->next;
	}

	return NULL;
}


static pvc_device* add_pvc(struct net_device *dev, u16 dlci)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	pvc_device *pvc, **pvc_p = &state(hdlc)->first_pvc;

	while (*pvc_p) {
		if ((*pvc_p)->dlci == dlci)
			return *pvc_p;
		if ((*pvc_p)->dlci > dlci)
			break;	/* the list is sorted */
		pvc_p = &(*pvc_p)->next;
	}

	pvc = kmalloc(sizeof(pvc_device), GFP_ATOMIC);
#ifdef DEBUG_PVC
	printk(KERN_DEBUG "add_pvc: allocated pvc %p, frad %p\n", pvc, dev);
#endif
	if (!pvc)
		return NULL;

	memset(pvc, 0, sizeof(pvc_device));
	pvc->dlci = dlci;
	pvc->frad = dev;
	pvc->next = *pvc_p;	/* Put it in the chain */
	*pvc_p = pvc;
	return pvc;
}


static inline int pvc_is_used(pvc_device *pvc)
{
	return pvc->main || pvc->ether;
}


static inline void pvc_carrier(int on, pvc_device *pvc)
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
	pvc_device **pvc_p = &state(hdlc)->first_pvc;

	while (*pvc_p) {
		if (!pvc_is_used(*pvc_p)) {
			pvc_device *pvc = *pvc_p;
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


static inline struct net_device** get_dev_p(pvc_device *pvc, int type)
{
	if (type == ARPHRD_ETHER)
		return &pvc->ether;
	else
		return &pvc->main;
}


static int fr_hard_header(struct sk_buff **skb_p, u16 dlci)
{
	u16 head_len;
	struct sk_buff *skb = *skb_p;

	switch (skb->protocol) {
	case __constant_ntohs(NLPID_CCITT_ANSI_LMI):
		head_len = 4;
		skb_push(skb, head_len);
		skb->data[3] = NLPID_CCITT_ANSI_LMI;
		break;

	case __constant_ntohs(NLPID_CISCO_LMI):
		head_len = 4;
		skb_push(skb, head_len);
		skb->data[3] = NLPID_CISCO_LMI;
		break;

	case __constant_ntohs(ETH_P_IP):
		head_len = 4;
		skb_push(skb, head_len);
		skb->data[3] = NLPID_IP;
		break;

	case __constant_ntohs(ETH_P_IPV6):
		head_len = 4;
		skb_push(skb, head_len);
		skb->data[3] = NLPID_IPV6;
		break;

	case __constant_ntohs(ETH_P_802_3):
		head_len = 10;
		if (skb_headroom(skb) < head_len) {
			struct sk_buff *skb2 = skb_realloc_headroom(skb,
								    head_len);
			if (!skb2)
				return -ENOBUFS;
			dev_kfree_skb(skb);
			skb = *skb_p = skb2;
		}
		skb_push(skb, head_len);
		skb->data[3] = FR_PAD;
		skb->data[4] = NLPID_SNAP;
		skb->data[5] = FR_PAD;
		skb->data[6] = 0x80;
		skb->data[7] = 0xC2;
		skb->data[8] = 0x00;
		skb->data[9] = 0x07; /* bridged Ethernet frame w/out FCS */
		break;

	default:
		head_len = 10;
		skb_push(skb, head_len);
		skb->data[3] = FR_PAD;
		skb->data[4] = NLPID_SNAP;
		skb->data[5] = FR_PAD;
		skb->data[6] = FR_PAD;
		skb->data[7] = FR_PAD;
		*(u16*)(skb->data + 8) = skb->protocol;
	}

	dlci_to_q922(skb->data, dlci);
	skb->data[2] = FR_UI;
	return 0;
}



static int pvc_open(struct net_device *dev)
{
	pvc_device *pvc = dev_to_pvc(dev);

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
	pvc_device *pvc = dev_to_pvc(dev);

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



static int pvc_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	pvc_device *pvc = dev_to_pvc(dev);
	fr_proto_pvc_info info;

	if (ifr->ifr_settings.type == IF_GET_PROTO) {
		if (dev->type == ARPHRD_ETHER)
			ifr->ifr_settings.type = IF_PROTO_FR_ETH_PVC;
		else
			ifr->ifr_settings.type = IF_PROTO_FR_PVC;

		if (ifr->ifr_settings.size < sizeof(info)) {
			/* data size wanted */
			ifr->ifr_settings.size = sizeof(info);
			return -ENOBUFS;
		}

		info.dlci = pvc->dlci;
		memcpy(info.master, pvc->frad->name, IFNAMSIZ);
		if (copy_to_user(ifr->ifr_settings.ifs_ifsu.fr_pvc_info,
				 &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}

	return -EINVAL;
}


static inline struct net_device_stats *pvc_get_stats(struct net_device *dev)
{
	return &dev_to_desc(dev)->stats;
}



static int pvc_xmit(struct sk_buff *skb, struct net_device *dev)
{
	pvc_device *pvc = dev_to_pvc(dev);
	struct net_device_stats *stats = pvc_get_stats(dev);

	if (pvc->state.active) {
		if (dev->type == ARPHRD_ETHER) {
			int pad = ETH_ZLEN - skb->len;
			if (pad > 0) { /* Pad the frame with zeros */
				int len = skb->len;
				if (skb_tailroom(skb) < pad)
					if (pskb_expand_head(skb, 0, pad,
							     GFP_ATOMIC)) {
						stats->tx_dropped++;
						dev_kfree_skb(skb);
						return 0;
					}
				skb_put(skb, pad);
				memset(skb->data + len, 0, pad);
			}
			skb->protocol = __constant_htons(ETH_P_802_3);
		}
		if (!fr_hard_header(&skb, pvc->dlci)) {
			stats->tx_bytes += skb->len;
			stats->tx_packets++;
			if (pvc->state.fecn) /* TX Congestion counter */
				stats->tx_compressed++;
			skb->dev = pvc->frad;
			dev_queue_xmit(skb);
			return 0;
		}
	}

	stats->tx_dropped++;
	dev_kfree_skb(skb);
	return 0;
}



static int pvc_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < 68) || (new_mtu > HDLC_MAX_MTU))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}



static inline void fr_log_dlci_active(pvc_device *pvc)
{
	printk(KERN_INFO "%s: DLCI %d [%s%s%s]%s %s\n",
	       pvc->frad->name,
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
	pvc_device *pvc = state(hdlc)->first_pvc;
	int lmi = state(hdlc)->settings.lmi;
	int dce = state(hdlc)->settings.dce;
	int len = lmi == LMI_ANSI ? LMI_ANSI_LENGTH : LMI_CCITT_CISCO_LENGTH;
	int stat_len = (lmi == LMI_CISCO) ? 6 : 3;
	u8 *data;
	int i = 0;

	if (dce && fullrep) {
		len += state(hdlc)->dce_pvc_count * (2 + stat_len);
		if (len > HDLC_MAX_MRU) {
			printk(KERN_WARNING "%s: Too many PVCs while sending "
			       "LMI full report\n", dev->name);
			return;
		}
	}

	skb = dev_alloc_skb(len);
	if (!skb) {
		printk(KERN_WARNING "%s: Memory squeeze on fr_lmi_send()\n",
		       dev->name);
		return;
	}
	memset(skb->data, 0, len);
	skb_reserve(skb, 4);
	if (lmi == LMI_CISCO) {
		skb->protocol = __constant_htons(NLPID_CISCO_LMI);
		fr_hard_header(&skb, LMI_CISCO_DLCI);
	} else {
		skb->protocol = __constant_htons(NLPID_CCITT_ANSI_LMI);
		fr_hard_header(&skb, LMI_CCITT_ANSI_DLCI);
	}
	data = skb->tail;
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
	skb->nh.raw = skb->data;

	dev_queue_xmit(skb);
}



static void fr_set_link_state(int reliable, struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	pvc_device *pvc = state(hdlc)->first_pvc;

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


static void fr_timer(unsigned long arg)
{
	struct net_device *dev = (struct net_device *)arg;
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
				printk(KERN_INFO "%s: No LMI status reply "
				       "received\n", dev->name);
			state(hdlc)->last_errors |= 1;
		}

		list = state(hdlc)->last_errors;
		for (i = 0; i < state(hdlc)->settings.n393; i++, list >>= 1)
			cnt += (list & 1);	/* errors count */

		reliable = (cnt < state(hdlc)->settings.n392);
	}

	if (state(hdlc)->reliable != reliable) {
		printk(KERN_INFO "%s: Link %sreliable\n", dev->name,
		       reliable ? "" : "un");
		fr_set_link_state(reliable, dev);
	}

	if (state(hdlc)->settings.dce)
		state(hdlc)->timer.expires = jiffies +
			state(hdlc)->settings.t392 * HZ;
	else {
		if (state(hdlc)->n391cnt)
			state(hdlc)->n391cnt--;

		fr_lmi_send(dev, state(hdlc)->n391cnt == 0);

		state(hdlc)->last_poll = jiffies;
		state(hdlc)->request = 1;
		state(hdlc)->timer.expires = jiffies +
			state(hdlc)->settings.t391 * HZ;
	}

	state(hdlc)->timer.function = fr_timer;
	state(hdlc)->timer.data = arg;
	add_timer(&state(hdlc)->timer);
}


static int fr_lmi_recv(struct net_device *dev, struct sk_buff *skb)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	pvc_device *pvc;
	u8 rxseq, txseq;
	int lmi = state(hdlc)->settings.lmi;
	int dce = state(hdlc)->settings.dce;
	int stat_len = (lmi == LMI_CISCO) ? 6 : 3, reptype, error, no_ram, i;

	if (skb->len < (lmi == LMI_ANSI ? LMI_ANSI_LENGTH :
			LMI_CCITT_CISCO_LENGTH)) {
		printk(KERN_INFO "%s: Short LMI frame\n", dev->name);
		return 1;
	}

	if (skb->data[3] != (lmi == LMI_CISCO ? NLPID_CISCO_LMI :
			     NLPID_CCITT_ANSI_LMI)) {
		printk(KERN_INFO "%s: Received non-LMI frame with LMI DLCI\n",
		       dev->name);
		return 1;
	}

	if (skb->data[4] != LMI_CALLREF) {
		printk(KERN_INFO "%s: Invalid LMI Call reference (0x%02X)\n",
		       dev->name, skb->data[4]);
		return 1;
	}

	if (skb->data[5] != (dce ? LMI_STATUS_ENQUIRY : LMI_STATUS)) {
		printk(KERN_INFO "%s: Invalid LMI Message type (0x%02X)\n",
		       dev->name, skb->data[5]);
		return 1;
	}

	if (lmi == LMI_ANSI) {
		if (skb->data[6] != LMI_ANSI_LOCKSHIFT) {
			printk(KERN_INFO "%s: Not ANSI locking shift in LMI"
			       " message (0x%02X)\n", dev->name, skb->data[6]);
			return 1;
		}
		i = 7;
	} else
		i = 6;

	if (skb->data[i] != (lmi == LMI_CCITT ? LMI_CCITT_REPTYPE :
			     LMI_ANSI_CISCO_REPTYPE)) {
		printk(KERN_INFO "%s: Not an LMI Report type IE (0x%02X)\n",
		       dev->name, skb->data[i]);
		return 1;
	}

	if (skb->data[++i] != LMI_REPT_LEN) {
		printk(KERN_INFO "%s: Invalid LMI Report type IE length"
		       " (%u)\n", dev->name, skb->data[i]);
		return 1;
	}

	reptype = skb->data[++i];
	if (reptype != LMI_INTEGRITY && reptype != LMI_FULLREP) {
		printk(KERN_INFO "%s: Unsupported LMI Report type (0x%02X)\n",
		       dev->name, reptype);
		return 1;
	}

	if (skb->data[++i] != (lmi == LMI_CCITT ? LMI_CCITT_ALIVE :
			       LMI_ANSI_CISCO_ALIVE)) {
		printk(KERN_INFO "%s: Not an LMI Link integrity verification"
		       " IE (0x%02X)\n", dev->name, skb->data[i]);
		return 1;
	}

	if (skb->data[++i] != LMI_INTEG_LEN) {
		printk(KERN_INFO "%s: Invalid LMI Link integrity verification"
		       " IE length (%u)\n", dev->name, skb->data[i]);
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
			printk(KERN_INFO "%s: Not an LMI PVC status IE"
			       " (0x%02X)\n", dev->name, skb->data[i]);
			return 1;
		}

		if (skb->data[++i] != stat_len) {
			printk(KERN_INFO "%s: Invalid LMI PVC status IE length"
			       " (%u)\n", dev->name, skb->data[i]);
			return 1;
		}
		i++;

		new = !! (skb->data[i + 2] & 0x08);
		active = !! (skb->data[i + 2] & 0x02);
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
			printk(KERN_WARNING
			       "%s: Memory squeeze on fr_lmi_recv()\n",
			       dev->name);
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


static int fr_rx(struct sk_buff *skb)
{
	struct net_device *frad = skb->dev;
	hdlc_device *hdlc = dev_to_hdlc(frad);
	fr_hdr *fh = (fr_hdr*)skb->data;
	u8 *data = skb->data;
	u16 dlci;
	pvc_device *pvc;
	struct net_device *dev = NULL;

	if (skb->len <= 4 || fh->ea1 || data[2] != FR_UI)
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
		printk(KERN_INFO "%s: No PVC for received frame's DLCI %d\n",
		       frad->name, dlci);
#endif
		dev_kfree_skb_any(skb);
		return NET_RX_DROP;
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


	if ((skb = skb_share_check(skb, GFP_ATOMIC)) == NULL) {
		dev_to_desc(frad)->stats.rx_dropped++;
		return NET_RX_DROP;
	}

	if (data[3] == NLPID_IP) {
		skb_pull(skb, 4); /* Remove 4-byte header (hdr, UI, NLPID) */
		dev = pvc->main;
		skb->protocol = htons(ETH_P_IP);

	} else if (data[3] == NLPID_IPV6) {
		skb_pull(skb, 4); /* Remove 4-byte header (hdr, UI, NLPID) */
		dev = pvc->main;
		skb->protocol = htons(ETH_P_IPV6);

	} else if (skb->len > 10 && data[3] == FR_PAD &&
		   data[4] == NLPID_SNAP && data[5] == FR_PAD) {
		u16 oui = ntohs(*(u16*)(data + 6));
		u16 pid = ntohs(*(u16*)(data + 8));
		skb_pull(skb, 10);

		switch ((((u32)oui) << 16) | pid) {
		case ETH_P_ARP: /* routed frame with SNAP */
		case ETH_P_IPX:
		case ETH_P_IP:	/* a long variant */
		case ETH_P_IPV6:
			dev = pvc->main;
			skb->protocol = htons(pid);
			break;

		case 0x80C20007: /* bridged Ethernet frame */
			if ((dev = pvc->ether) != NULL)
				skb->protocol = eth_type_trans(skb, dev);
			break;

		default:
			printk(KERN_INFO "%s: Unsupported protocol, OUI=%x "
			       "PID=%x\n", frad->name, oui, pid);
			dev_kfree_skb_any(skb);
			return NET_RX_DROP;
		}
	} else {
		printk(KERN_INFO "%s: Unsupported protocol, NLPID=%x "
		       "length = %i\n", frad->name, data[3], skb->len);
		dev_kfree_skb_any(skb);
		return NET_RX_DROP;
	}

	if (dev) {
		struct net_device_stats *stats = pvc_get_stats(dev);
		stats->rx_packets++; /* PVC traffic */
		stats->rx_bytes += skb->len;
		if (pvc->state.becn)
			stats->rx_compressed++;
		skb->dev = dev;
		netif_rx(skb);
		return NET_RX_SUCCESS;
	} else {
		dev_kfree_skb_any(skb);
		return NET_RX_DROP;
	}

 rx_error:
	dev_to_desc(frad)->stats.rx_errors++; /* Mark error */
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

		init_timer(&state(hdlc)->timer);
		/* First poll after 1 s */
		state(hdlc)->timer.expires = jiffies + HZ;
		state(hdlc)->timer.function = fr_timer;
		state(hdlc)->timer.data = (unsigned long)dev;
		add_timer(&state(hdlc)->timer);
	} else
		fr_set_link_state(1, dev);
}


static void fr_stop(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
#ifdef DEBUG_LINK
	printk(KERN_DEBUG "fr_stop\n");
#endif
	if (state(hdlc)->settings.lmi != LMI_NONE)
		del_timer_sync(&state(hdlc)->timer);
	fr_set_link_state(0, dev);
}


static void fr_close(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	pvc_device *pvc = state(hdlc)->first_pvc;

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
	dev->hard_header_len = 10;
	dev->addr_len = 2;
}

static int fr_add_pvc(struct net_device *frad, unsigned int dlci, int type)
{
	hdlc_device *hdlc = dev_to_hdlc(frad);
	pvc_device *pvc = NULL;
	struct net_device *dev;
	int result, used;
	char * prefix = "pvc%d";

	if (type == ARPHRD_ETHER)
		prefix = "pvceth%d";

	if ((pvc = add_pvc(frad, dlci)) == NULL) {
		printk(KERN_WARNING "%s: Memory squeeze on fr_add_pvc()\n",
		       frad->name);
		return -ENOBUFS;
	}

	if (*get_dev_p(pvc, type))
		return -EEXIST;

	used = pvc_is_used(pvc);

	if (type == ARPHRD_ETHER)
		dev = alloc_netdev(sizeof(struct net_device_stats),
				   "pvceth%d", ether_setup);
	else
		dev = alloc_netdev(sizeof(struct net_device_stats),
				   "pvc%d", pvc_setup);

	if (!dev) {
		printk(KERN_WARNING "%s: Memory squeeze on fr_pvc()\n",
		       frad->name);
		delete_unused_pvcs(hdlc);
		return -ENOBUFS;
	}

	if (type == ARPHRD_ETHER) {
		memcpy(dev->dev_addr, "\x00\x01", 2);
                get_random_bytes(dev->dev_addr + 2, ETH_ALEN - 2);
	} else {
		*(u16*)dev->dev_addr = htons(dlci);
		dlci_to_q922(dev->broadcast, dlci);
	}
	dev->hard_start_xmit = pvc_xmit;
	dev->get_stats = pvc_get_stats;
	dev->open = pvc_open;
	dev->stop = pvc_close;
	dev->do_ioctl = pvc_ioctl;
	dev->change_mtu = pvc_change_mtu;
	dev->mtu = HDLC_MAX_MTU;
	dev->tx_queue_len = 0;
	dev->priv = pvc;

	result = dev_alloc_name(dev, dev->name);
	if (result < 0) {
		free_netdev(dev);
		delete_unused_pvcs(hdlc);
		return result;
	}

	if (register_netdevice(dev) != 0) {
		free_netdev(dev);
		delete_unused_pvcs(hdlc);
		return -EIO;
	}

	dev->destructor = free_netdev;
	*get_dev_p(pvc, type) = dev;
	if (!used) {
		state(hdlc)->dce_changed = 1;
		state(hdlc)->dce_pvc_count++;
	}
	return 0;
}



static int fr_del_pvc(hdlc_device *hdlc, unsigned int dlci, int type)
{
	pvc_device *pvc;
	struct net_device *dev;

	if ((pvc = find_pvc(hdlc, dlci)) == NULL)
		return -ENOENT;

	if ((dev = *get_dev_p(pvc, type)) == NULL)
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
	pvc_device *pvc = state(hdlc)->first_pvc;
	state(hdlc)->first_pvc = NULL; /* All PVCs destroyed */
	state(hdlc)->dce_pvc_count = 0;
	state(hdlc)->dce_changed = 1;

	while (pvc) {
		pvc_device *next = pvc->next;
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
	.module		= THIS_MODULE,
};


static int fr_ioctl(struct net_device *dev, struct ifreq *ifr)
{
	fr_proto __user *fr_s = ifr->ifr_settings.ifs_ifsu.fr;
	const size_t size = sizeof(fr_proto);
	fr_proto new_settings;
	hdlc_device *hdlc = dev_to_hdlc(dev);
	fr_proto_pvc pvc;
	int result;

	switch (ifr->ifr_settings.type) {
	case IF_GET_PROTO:
		if (dev_to_hdlc(dev)->proto != &proto) /* Different proto */
			return -EINVAL;
		ifr->ifr_settings.type = IF_PROTO_FR;
		if (ifr->ifr_settings.size < size) {
			ifr->ifr_settings.size = size; /* data size wanted */
			return -ENOBUFS;
		}
		if (copy_to_user(fr_s, &state(hdlc)->settings, size))
			return -EFAULT;
		return 0;

	case IF_PROTO_FR:
		if(!capable(CAP_NET_ADMIN))
			return -EPERM;

		if(dev->flags & IFF_UP)
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

		result=hdlc->attach(dev, ENCODING_NRZ,PARITY_CRC16_PR1_CCITT);
		if (result)
			return result;

		if (dev_to_hdlc(dev)->proto != &proto) { /* Different proto */
			result = attach_hdlc_protocol(dev, &proto, fr_rx,
						      sizeof(struct frad_state));
			if (result)
				return result;
			state(hdlc)->first_pvc = NULL;
			state(hdlc)->dce_pvc_count = 0;
		}
		memcpy(&state(hdlc)->settings, &new_settings, size);

		dev->hard_start_xmit = hdlc->xmit;
		dev->hard_header = NULL;
		dev->type = ARPHRD_FRAD;
		dev->flags = IFF_POINTOPOINT | IFF_NOARP;
		dev->addr_len = 0;
		return 0;

	case IF_PROTO_FR_ADD_PVC:
	case IF_PROTO_FR_DEL_PVC:
	case IF_PROTO_FR_ADD_ETH_PVC:
	case IF_PROTO_FR_DEL_ETH_PVC:
		if (dev_to_hdlc(dev)->proto != &proto) /* Different proto */
			return -EINVAL;

		if(!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (copy_from_user(&pvc, ifr->ifr_settings.ifs_ifsu.fr_pvc,
				   sizeof(fr_proto_pvc)))
			return -EFAULT;

		if (pvc.dlci <= 0 || pvc.dlci >= 1024)
			return -EINVAL;	/* Only 10 bits, DLCI 0 reserved */

		if (ifr->ifr_settings.type == IF_PROTO_FR_ADD_ETH_PVC ||
		    ifr->ifr_settings.type == IF_PROTO_FR_DEL_ETH_PVC)
			result = ARPHRD_ETHER; /* bridged Ethernet device */
		else
			result = ARPHRD_DLCI;

		if (ifr->ifr_settings.type == IF_PROTO_FR_ADD_PVC ||
		    ifr->ifr_settings.type == IF_PROTO_FR_ADD_ETH_PVC)
			return fr_add_pvc(dev, pvc.dlci, result);
		else
			return fr_del_pvc(hdlc, pvc.dlci, result);
	}

	return -EINVAL;
}


static int __init mod_init(void)
{
	register_hdlc_protocol(&proto);
	return 0;
}


static void __exit mod_exit(void)
{
	unregister_hdlc_protocol(&proto);
}


module_init(mod_init);
module_exit(mod_exit);

MODULE_AUTHOR("Krzysztof Halasa <khc@pm.waw.pl>");
MODULE_DESCRIPTION("Frame-Relay protocol support for generic HDLC");
MODULE_LICENSE("GPL v2");
