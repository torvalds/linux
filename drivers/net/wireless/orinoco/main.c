/* main.c - (formerly known as dldwd_cs.c, orinoco_cs.c and orinoco.c)
 *
 * A driver for Hermes or Prism 2 chipset based PCMCIA wireless
 * adaptors, with Lucent/Agere, Intersil or Symbol firmware.
 *
 * Current maintainers (as of 29 September 2003) are:
 *	Pavel Roskin <proski AT gnu.org>
 * and	David Gibson <hermes AT gibson.dropbear.id.au>
 *
 * (C) Copyright David Gibson, IBM Corporation 2001-2003.
 * Copyright (C) 2000 David Gibson, Linuxcare Australia.
 *	With some help from :
 * Copyright (C) 2001 Jean Tourrilhes, HP Labs
 * Copyright (C) 2001 Benjamin Herrenschmidt
 *
 * Based on dummy_cs.c 1.27 2000/06/12 21:27:25
 *
 * Portions based on wvlan_cs.c 1.0.6, Copyright Andreas Neuhaus <andy
 * AT fasta.fh-dortmund.de>
 *      http://www.stud.fh-dortmund.de/~andy/wvlan/
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds AT users.sourceforge.net>.  Portions created by David
 * A. Hinds are Copyright (C) 1999 David A. Hinds.  All Rights
 * Reserved.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.  */

/*
 * TODO
 *	o Handle de-encapsulation within network layer, provide 802.11
 *	  headers (patch from Thomas 'Dent' Mirlacher)
 *	o Fix possible races in SPY handling.
 *	o Disconnect wireless extensions from fundamental configuration.
 *	o (maybe) Software WEP support (patch from Stano Meduna).
 *	o (maybe) Use multiple Tx buffers - driver handling queue
 *	  rather than firmware.
 */

/* Locking and synchronization:
 *
 * The basic principle is that everything is serialized through a
 * single spinlock, priv->lock.  The lock is used in user, bh and irq
 * context, so when taken outside hardirq context it should always be
 * taken with interrupts disabled.  The lock protects both the
 * hardware and the struct orinoco_private.
 *
 * Another flag, priv->hw_unavailable indicates that the hardware is
 * unavailable for an extended period of time (e.g. suspended, or in
 * the middle of a hard reset).  This flag is protected by the
 * spinlock.  All code which touches the hardware should check the
 * flag after taking the lock, and if it is set, give up on whatever
 * they are doing and drop the lock again.  The orinoco_lock()
 * function handles this (it unlocks and returns -EBUSY if
 * hw_unavailable is non-zero).
 */

#define DRIVER_NAME "orinoco"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/suspend.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <net/iw_handler.h>
#include <net/cfg80211.h>

#include "hermes_rid.h"
#include "hermes_dld.h"
#include "hw.h"
#include "scan.h"
#include "mic.h"
#include "fw.h"
#include "wext.h"
#include "cfg.h"
#include "main.h"

#include "orinoco.h"

/********************************************************************/
/* Module information                                               */
/********************************************************************/

MODULE_AUTHOR("Pavel Roskin <proski@gnu.org> & "
	      "David Gibson <hermes@gibson.dropbear.id.au>");
MODULE_DESCRIPTION("Driver for Lucent Orinoco, Prism II based "
		   "and similar wireless cards");
MODULE_LICENSE("Dual MPL/GPL");

/* Level of debugging. Used in the macros in orinoco.h */
#ifdef ORINOCO_DEBUG
int orinoco_debug = ORINOCO_DEBUG;
EXPORT_SYMBOL(orinoco_debug);
module_param(orinoco_debug, int, 0644);
MODULE_PARM_DESC(orinoco_debug, "Debug level");
#endif

static int suppress_linkstatus; /* = 0 */
module_param(suppress_linkstatus, bool, 0644);
MODULE_PARM_DESC(suppress_linkstatus, "Don't log link status changes");

static int ignore_disconnect; /* = 0 */
module_param(ignore_disconnect, int, 0644);
MODULE_PARM_DESC(ignore_disconnect,
		 "Don't report lost link to the network layer");

int force_monitor; /* = 0 */
module_param(force_monitor, int, 0644);
MODULE_PARM_DESC(force_monitor, "Allow monitor mode for all firmware versions");

/********************************************************************/
/* Internal constants                                               */
/********************************************************************/

/* 802.2 LLC/SNAP header used for Ethernet encapsulation over 802.11 */
static const u8 encaps_hdr[] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};
#define ENCAPS_OVERHEAD		(sizeof(encaps_hdr) + 2)

#define ORINOCO_MIN_MTU		256
#define ORINOCO_MAX_MTU		(IEEE80211_MAX_DATA_LEN - ENCAPS_OVERHEAD)

#define MAX_IRQLOOPS_PER_IRQ	10
#define MAX_IRQLOOPS_PER_JIFFY	(20000 / HZ)	/* Based on a guestimate of
						 * how many events the
						 * device could
						 * legitimately generate */

#define DUMMY_FID		0xFFFF

/*#define MAX_MULTICAST(priv)	(priv->firmware_type == FIRMWARE_TYPE_AGERE ? \
  HERMES_MAX_MULTICAST : 0)*/
#define MAX_MULTICAST(priv)	(HERMES_MAX_MULTICAST)

#define ORINOCO_INTEN		(HERMES_EV_RX | HERMES_EV_ALLOC \
				 | HERMES_EV_TX | HERMES_EV_TXEXC \
				 | HERMES_EV_WTERR | HERMES_EV_INFO \
				 | HERMES_EV_INFDROP)

/********************************************************************/
/* Data types                                                       */
/********************************************************************/

/* Beginning of the Tx descriptor, used in TxExc handling */
struct hermes_txexc_data {
	struct hermes_tx_descriptor desc;
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
} __packed;

/* Rx frame header except compatibility 802.3 header */
struct hermes_rx_descriptor {
	/* Control */
	__le16 status;
	__le32 time;
	u8 silence;
	u8 signal;
	u8 rate;
	u8 rxflow;
	__le32 reserved;

	/* 802.11 header */
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 addr4[ETH_ALEN];

	/* Data length */
	__le16 data_len;
} __packed;

struct orinoco_rx_data {
	struct hermes_rx_descriptor *desc;
	struct sk_buff *skb;
	struct list_head list;
};

struct orinoco_scan_data {
	void *buf;
	size_t len;
	int type;
	struct list_head list;
};

/********************************************************************/
/* Function prototypes                                              */
/********************************************************************/

static int __orinoco_set_multicast_list(struct net_device *dev);
static int __orinoco_up(struct orinoco_private *priv);
static int __orinoco_down(struct orinoco_private *priv);
static int __orinoco_commit(struct orinoco_private *priv);

/********************************************************************/
/* Internal helper functions                                        */
/********************************************************************/

void set_port_type(struct orinoco_private *priv)
{
	switch (priv->iw_mode) {
	case NL80211_IFTYPE_STATION:
		priv->port_type = 1;
		priv->createibss = 0;
		break;
	case NL80211_IFTYPE_ADHOC:
		if (priv->prefer_port3) {
			priv->port_type = 3;
			priv->createibss = 0;
		} else {
			priv->port_type = priv->ibss_port;
			priv->createibss = 1;
		}
		break;
	case NL80211_IFTYPE_MONITOR:
		priv->port_type = 3;
		priv->createibss = 0;
		break;
	default:
		printk(KERN_ERR "%s: Invalid priv->iw_mode in set_port_type()\n",
		       priv->ndev->name);
	}
}

/********************************************************************/
/* Device methods                                                   */
/********************************************************************/

int orinoco_open(struct net_device *dev)
{
	struct orinoco_private *priv = ndev_priv(dev);
	unsigned long flags;
	int err;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	err = __orinoco_up(priv);

	if (!err)
		priv->open = 1;

	orinoco_unlock(priv, &flags);

	return err;
}
EXPORT_SYMBOL(orinoco_open);

int orinoco_stop(struct net_device *dev)
{
	struct orinoco_private *priv = ndev_priv(dev);
	int err = 0;

	/* We mustn't use orinoco_lock() here, because we need to be
	   able to close the interface even if hw_unavailable is set
	   (e.g. as we're released after a PC Card removal) */
	orinoco_lock_irq(priv);

	priv->open = 0;

	err = __orinoco_down(priv);

	orinoco_unlock_irq(priv);

	return err;
}
EXPORT_SYMBOL(orinoco_stop);

struct net_device_stats *orinoco_get_stats(struct net_device *dev)
{
	struct orinoco_private *priv = ndev_priv(dev);

	return &priv->stats;
}
EXPORT_SYMBOL(orinoco_get_stats);

void orinoco_set_multicast_list(struct net_device *dev)
{
	struct orinoco_private *priv = ndev_priv(dev);
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0) {
		printk(KERN_DEBUG "%s: orinoco_set_multicast_list() "
		       "called when hw_unavailable\n", dev->name);
		return;
	}

	__orinoco_set_multicast_list(dev);
	orinoco_unlock(priv, &flags);
}
EXPORT_SYMBOL(orinoco_set_multicast_list);

int orinoco_change_mtu(struct net_device *dev, int new_mtu)
{
	struct orinoco_private *priv = ndev_priv(dev);

	if ((new_mtu < ORINOCO_MIN_MTU) || (new_mtu > ORINOCO_MAX_MTU))
		return -EINVAL;

	/* MTU + encapsulation + header length */
	if ((new_mtu + ENCAPS_OVERHEAD + sizeof(struct ieee80211_hdr)) >
	     (priv->nicbuf_size - ETH_HLEN))
		return -EINVAL;

	dev->mtu = new_mtu;

	return 0;
}
EXPORT_SYMBOL(orinoco_change_mtu);

/********************************************************************/
/* Tx path                                                          */
/********************************************************************/

/* Add encapsulation and MIC to the existing SKB.
 * The main xmit routine will then send the whole lot to the card.
 * Need 8 bytes headroom
 * Need 8 bytes tailroom
 *
 *                          With encapsulated ethernet II frame
 *                          --------
 *                          803.3 header (14 bytes)
 *                           dst[6]
 * --------                  src[6]
 * 803.3 header (14 bytes)   len[2]
 *  dst[6]                  803.2 header (8 bytes)
 *  src[6]                   encaps[6]
 *  len[2] <- leave alone -> len[2]
 * --------                 -------- <-- 0
 * Payload                  Payload
 * ...                      ...
 *
 * --------                 --------
 *                          MIC (8 bytes)
 *                          --------
 *
 * returns 0 on success, -ENOMEM on error.
 */
int orinoco_process_xmit_skb(struct sk_buff *skb,
			     struct net_device *dev,
			     struct orinoco_private *priv,
			     int *tx_control,
			     u8 *mic_buf)
{
	struct orinoco_tkip_key *key;
	struct ethhdr *eh;
	int do_mic;

	key = (struct orinoco_tkip_key *) priv->keys[priv->tx_key].key;

	do_mic = ((priv->encode_alg == ORINOCO_ALG_TKIP) &&
		  (key != NULL));

	if (do_mic)
		*tx_control |= (priv->tx_key << HERMES_MIC_KEY_ID_SHIFT) |
			HERMES_TXCTRL_MIC;

	eh = (struct ethhdr *)skb->data;

	/* Encapsulate Ethernet-II frames */
	if (ntohs(eh->h_proto) > ETH_DATA_LEN) { /* Ethernet-II frame */
		struct header_struct {
			struct ethhdr eth;	/* 802.3 header */
			u8 encap[6];		/* 802.2 header */
		} __packed hdr;
		int len = skb->len + sizeof(encaps_hdr) - (2 * ETH_ALEN);

		if (skb_headroom(skb) < ENCAPS_OVERHEAD) {
			if (net_ratelimit())
				printk(KERN_ERR
				       "%s: Not enough headroom for 802.2 headers %d\n",
				       dev->name, skb_headroom(skb));
			return -ENOMEM;
		}

		/* Fill in new header */
		memcpy(&hdr.eth, eh, 2 * ETH_ALEN);
		hdr.eth.h_proto = htons(len);
		memcpy(hdr.encap, encaps_hdr, sizeof(encaps_hdr));

		/* Make room for the new header, and copy it in */
		eh = (struct ethhdr *) skb_push(skb, ENCAPS_OVERHEAD);
		memcpy(eh, &hdr, sizeof(hdr));
	}

	/* Calculate Michael MIC */
	if (do_mic) {
		size_t len = skb->len - ETH_HLEN;
		u8 *mic = &mic_buf[0];

		/* Have to write to an even address, so copy the spare
		 * byte across */
		if (skb->len % 2) {
			*mic = skb->data[skb->len - 1];
			mic++;
		}

		orinoco_mic(priv->tx_tfm_mic, key->tx_mic,
			    eh->h_dest, eh->h_source, 0 /* priority */,
			    skb->data + ETH_HLEN,
			    len, mic);
	}

	return 0;
}
EXPORT_SYMBOL(orinoco_process_xmit_skb);

static netdev_tx_t orinoco_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct orinoco_private *priv = ndev_priv(dev);
	struct net_device_stats *stats = &priv->stats;
	struct hermes *hw = &priv->hw;
	int err = 0;
	u16 txfid = priv->txfid;
	int tx_control;
	unsigned long flags;
	u8 mic_buf[MICHAEL_MIC_LEN + 1];

	if (!netif_running(dev)) {
		printk(KERN_ERR "%s: Tx on stopped device!\n",
		       dev->name);
		return NETDEV_TX_BUSY;
	}

	if (netif_queue_stopped(dev)) {
		printk(KERN_DEBUG "%s: Tx while transmitter busy!\n",
		       dev->name);
		return NETDEV_TX_BUSY;
	}

	if (orinoco_lock(priv, &flags) != 0) {
		printk(KERN_ERR "%s: orinoco_xmit() called while hw_unavailable\n",
		       dev->name);
		return NETDEV_TX_BUSY;
	}

	if (!netif_carrier_ok(dev) ||
	    (priv->iw_mode == NL80211_IFTYPE_MONITOR)) {
		/* Oops, the firmware hasn't established a connection,
		   silently drop the packet (this seems to be the
		   safest approach). */
		goto drop;
	}

	/* Check packet length */
	if (skb->len < ETH_HLEN)
		goto drop;

	tx_control = HERMES_TXCTRL_TX_OK | HERMES_TXCTRL_TX_EX;

	err = orinoco_process_xmit_skb(skb, dev, priv, &tx_control,
				       &mic_buf[0]);
	if (err)
		goto drop;

	if (priv->has_alt_txcntl) {
		/* WPA enabled firmwares have tx_cntl at the end of
		 * the 802.11 header.  So write zeroed descriptor and
		 * 802.11 header at the same time
		 */
		char desc[HERMES_802_3_OFFSET];
		__le16 *txcntl = (__le16 *) &desc[HERMES_TXCNTL2_OFFSET];

		memset(&desc, 0, sizeof(desc));

		*txcntl = cpu_to_le16(tx_control);
		err = hw->ops->bap_pwrite(hw, USER_BAP, &desc, sizeof(desc),
					  txfid, 0);
		if (err) {
			if (net_ratelimit())
				printk(KERN_ERR "%s: Error %d writing Tx "
				       "descriptor to BAP\n", dev->name, err);
			goto busy;
		}
	} else {
		struct hermes_tx_descriptor desc;

		memset(&desc, 0, sizeof(desc));

		desc.tx_control = cpu_to_le16(tx_control);
		err = hw->ops->bap_pwrite(hw, USER_BAP, &desc, sizeof(desc),
					  txfid, 0);
		if (err) {
			if (net_ratelimit())
				printk(KERN_ERR "%s: Error %d writing Tx "
				       "descriptor to BAP\n", dev->name, err);
			goto busy;
		}

		/* Clear the 802.11 header and data length fields - some
		 * firmwares (e.g. Lucent/Agere 8.xx) appear to get confused
		 * if this isn't done. */
		hermes_clear_words(hw, HERMES_DATA0,
				   HERMES_802_3_OFFSET - HERMES_802_11_OFFSET);
	}

	err = hw->ops->bap_pwrite(hw, USER_BAP, skb->data, skb->len,
				  txfid, HERMES_802_3_OFFSET);
	if (err) {
		printk(KERN_ERR "%s: Error %d writing packet to BAP\n",
		       dev->name, err);
		goto busy;
	}

	if (tx_control & HERMES_TXCTRL_MIC) {
		size_t offset = HERMES_802_3_OFFSET + skb->len;
		size_t len = MICHAEL_MIC_LEN;

		if (offset % 2) {
			offset--;
			len++;
		}
		err = hw->ops->bap_pwrite(hw, USER_BAP, &mic_buf[0], len,
					  txfid, offset);
		if (err) {
			printk(KERN_ERR "%s: Error %d writing MIC to BAP\n",
			       dev->name, err);
			goto busy;
		}
	}

	/* Finally, we actually initiate the send */
	netif_stop_queue(dev);

	err = hw->ops->cmd_wait(hw, HERMES_CMD_TX | HERMES_CMD_RECL,
				txfid, NULL);
	if (err) {
		netif_start_queue(dev);
		if (net_ratelimit())
			printk(KERN_ERR "%s: Error %d transmitting packet\n",
				dev->name, err);
		goto busy;
	}

	stats->tx_bytes += HERMES_802_3_OFFSET + skb->len;
	goto ok;

 drop:
	stats->tx_errors++;
	stats->tx_dropped++;

 ok:
	orinoco_unlock(priv, &flags);
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;

 busy:
	if (err == -EIO)
		schedule_work(&priv->reset_work);
	orinoco_unlock(priv, &flags);
	return NETDEV_TX_BUSY;
}

static void __orinoco_ev_alloc(struct net_device *dev, struct hermes *hw)
{
	struct orinoco_private *priv = ndev_priv(dev);
	u16 fid = hermes_read_regn(hw, ALLOCFID);

	if (fid != priv->txfid) {
		if (fid != DUMMY_FID)
			printk(KERN_WARNING "%s: Allocate event on unexpected fid (%04X)\n",
			       dev->name, fid);
		return;
	}

	hermes_write_regn(hw, ALLOCFID, DUMMY_FID);
}

static void __orinoco_ev_tx(struct net_device *dev, struct hermes *hw)
{
	struct orinoco_private *priv = ndev_priv(dev);
	struct net_device_stats *stats = &priv->stats;

	stats->tx_packets++;

	netif_wake_queue(dev);

	hermes_write_regn(hw, TXCOMPLFID, DUMMY_FID);
}

static void __orinoco_ev_txexc(struct net_device *dev, struct hermes *hw)
{
	struct orinoco_private *priv = ndev_priv(dev);
	struct net_device_stats *stats = &priv->stats;
	u16 fid = hermes_read_regn(hw, TXCOMPLFID);
	u16 status;
	struct hermes_txexc_data hdr;
	int err = 0;

	if (fid == DUMMY_FID)
		return; /* Nothing's really happened */

	/* Read part of the frame header - we need status and addr1 */
	err = hw->ops->bap_pread(hw, IRQ_BAP, &hdr,
				 sizeof(struct hermes_txexc_data),
				 fid, 0);

	hermes_write_regn(hw, TXCOMPLFID, DUMMY_FID);
	stats->tx_errors++;

	if (err) {
		printk(KERN_WARNING "%s: Unable to read descriptor on Tx error "
		       "(FID=%04X error %d)\n",
		       dev->name, fid, err);
		return;
	}

	DEBUG(1, "%s: Tx error, err %d (FID=%04X)\n", dev->name,
	      err, fid);

	/* We produce a TXDROP event only for retry or lifetime
	 * exceeded, because that's the only status that really mean
	 * that this particular node went away.
	 * Other errors means that *we* screwed up. - Jean II */
	status = le16_to_cpu(hdr.desc.status);
	if (status & (HERMES_TXSTAT_RETRYERR | HERMES_TXSTAT_AGEDERR)) {
		union iwreq_data	wrqu;

		/* Copy 802.11 dest address.
		 * We use the 802.11 header because the frame may
		 * not be 802.3 or may be mangled...
		 * In Ad-Hoc mode, it will be the node address.
		 * In managed mode, it will be most likely the AP addr
		 * User space will figure out how to convert it to
		 * whatever it needs (IP address or else).
		 * - Jean II */
		memcpy(wrqu.addr.sa_data, hdr.addr1, ETH_ALEN);
		wrqu.addr.sa_family = ARPHRD_ETHER;

		/* Send event to user space */
		wireless_send_event(dev, IWEVTXDROP, &wrqu, NULL);
	}

	netif_wake_queue(dev);
}

void orinoco_tx_timeout(struct net_device *dev)
{
	struct orinoco_private *priv = ndev_priv(dev);
	struct net_device_stats *stats = &priv->stats;
	struct hermes *hw = &priv->hw;

	printk(KERN_WARNING "%s: Tx timeout! "
	       "ALLOCFID=%04x, TXCOMPLFID=%04x, EVSTAT=%04x\n",
	       dev->name, hermes_read_regn(hw, ALLOCFID),
	       hermes_read_regn(hw, TXCOMPLFID), hermes_read_regn(hw, EVSTAT));

	stats->tx_errors++;

	schedule_work(&priv->reset_work);
}
EXPORT_SYMBOL(orinoco_tx_timeout);

/********************************************************************/
/* Rx path (data frames)                                            */
/********************************************************************/

/* Does the frame have a SNAP header indicating it should be
 * de-encapsulated to Ethernet-II? */
static inline int is_ethersnap(void *_hdr)
{
	u8 *hdr = _hdr;

	/* We de-encapsulate all packets which, a) have SNAP headers
	 * (i.e. SSAP=DSAP=0xaa and CTRL=0x3 in the 802.2 LLC header
	 * and where b) the OUI of the SNAP header is 00:00:00 or
	 * 00:00:f8 - we need both because different APs appear to use
	 * different OUIs for some reason */
	return (memcmp(hdr, &encaps_hdr, 5) == 0)
		&& ((hdr[5] == 0x00) || (hdr[5] == 0xf8));
}

static inline void orinoco_spy_gather(struct net_device *dev, u_char *mac,
				      int level, int noise)
{
	struct iw_quality wstats;
	wstats.level = level - 0x95;
	wstats.noise = noise - 0x95;
	wstats.qual = (level > noise) ? (level - noise) : 0;
	wstats.updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
	/* Update spy records */
	wireless_spy_update(dev, mac, &wstats);
}

static void orinoco_stat_gather(struct net_device *dev,
				struct sk_buff *skb,
				struct hermes_rx_descriptor *desc)
{
	struct orinoco_private *priv = ndev_priv(dev);

	/* Using spy support with lots of Rx packets, like in an
	 * infrastructure (AP), will really slow down everything, because
	 * the MAC address must be compared to each entry of the spy list.
	 * If the user really asks for it (set some address in the
	 * spy list), we do it, but he will pay the price.
	 * Note that to get here, you need both WIRELESS_SPY
	 * compiled in AND some addresses in the list !!!
	 */
	/* Note : gcc will optimise the whole section away if
	 * WIRELESS_SPY is not defined... - Jean II */
	if (SPY_NUMBER(priv)) {
		orinoco_spy_gather(dev, skb_mac_header(skb) + ETH_ALEN,
				   desc->signal, desc->silence);
	}
}

/*
 * orinoco_rx_monitor - handle received monitor frames.
 *
 * Arguments:
 *	dev		network device
 *	rxfid		received FID
 *	desc		rx descriptor of the frame
 *
 * Call context: interrupt
 */
static void orinoco_rx_monitor(struct net_device *dev, u16 rxfid,
			       struct hermes_rx_descriptor *desc)
{
	u32 hdrlen = 30;	/* return full header by default */
	u32 datalen = 0;
	u16 fc;
	int err;
	int len;
	struct sk_buff *skb;
	struct orinoco_private *priv = ndev_priv(dev);
	struct net_device_stats *stats = &priv->stats;
	struct hermes *hw = &priv->hw;

	len = le16_to_cpu(desc->data_len);

	/* Determine the size of the header and the data */
	fc = le16_to_cpu(desc->frame_ctl);
	switch (fc & IEEE80211_FCTL_FTYPE) {
	case IEEE80211_FTYPE_DATA:
		if ((fc & IEEE80211_FCTL_TODS)
		    && (fc & IEEE80211_FCTL_FROMDS))
			hdrlen = 30;
		else
			hdrlen = 24;
		datalen = len;
		break;
	case IEEE80211_FTYPE_MGMT:
		hdrlen = 24;
		datalen = len;
		break;
	case IEEE80211_FTYPE_CTL:
		switch (fc & IEEE80211_FCTL_STYPE) {
		case IEEE80211_STYPE_PSPOLL:
		case IEEE80211_STYPE_RTS:
		case IEEE80211_STYPE_CFEND:
		case IEEE80211_STYPE_CFENDACK:
			hdrlen = 16;
			break;
		case IEEE80211_STYPE_CTS:
		case IEEE80211_STYPE_ACK:
			hdrlen = 10;
			break;
		}
		break;
	default:
		/* Unknown frame type */
		break;
	}

	/* sanity check the length */
	if (datalen > IEEE80211_MAX_DATA_LEN + 12) {
		printk(KERN_DEBUG "%s: oversized monitor frame, "
		       "data length = %d\n", dev->name, datalen);
		stats->rx_length_errors++;
		goto update_stats;
	}

	skb = dev_alloc_skb(hdrlen + datalen);
	if (!skb) {
		printk(KERN_WARNING "%s: Cannot allocate skb for monitor frame\n",
		       dev->name);
		goto update_stats;
	}

	/* Copy the 802.11 header to the skb */
	memcpy(skb_put(skb, hdrlen), &(desc->frame_ctl), hdrlen);
	skb_reset_mac_header(skb);

	/* If any, copy the data from the card to the skb */
	if (datalen > 0) {
		err = hw->ops->bap_pread(hw, IRQ_BAP, skb_put(skb, datalen),
					 ALIGN(datalen, 2), rxfid,
					 HERMES_802_2_OFFSET);
		if (err) {
			printk(KERN_ERR "%s: error %d reading monitor frame\n",
			       dev->name, err);
			goto drop;
		}
	}

	skb->dev = dev;
	skb->ip_summed = CHECKSUM_NONE;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = cpu_to_be16(ETH_P_802_2);

	stats->rx_packets++;
	stats->rx_bytes += skb->len;

	netif_rx(skb);
	return;

 drop:
	dev_kfree_skb_irq(skb);
 update_stats:
	stats->rx_errors++;
	stats->rx_dropped++;
}

void __orinoco_ev_rx(struct net_device *dev, struct hermes *hw)
{
	struct orinoco_private *priv = ndev_priv(dev);
	struct net_device_stats *stats = &priv->stats;
	struct iw_statistics *wstats = &priv->wstats;
	struct sk_buff *skb = NULL;
	u16 rxfid, status;
	int length;
	struct hermes_rx_descriptor *desc;
	struct orinoco_rx_data *rx_data;
	int err;

	desc = kmalloc(sizeof(*desc), GFP_ATOMIC);
	if (!desc) {
		printk(KERN_WARNING
		       "%s: Can't allocate space for RX descriptor\n",
		       dev->name);
		goto update_stats;
	}

	rxfid = hermes_read_regn(hw, RXFID);

	err = hw->ops->bap_pread(hw, IRQ_BAP, desc, sizeof(*desc),
				 rxfid, 0);
	if (err) {
		printk(KERN_ERR "%s: error %d reading Rx descriptor. "
		       "Frame dropped.\n", dev->name, err);
		goto update_stats;
	}

	status = le16_to_cpu(desc->status);

	if (status & HERMES_RXSTAT_BADCRC) {
		DEBUG(1, "%s: Bad CRC on Rx. Frame dropped.\n",
		      dev->name);
		stats->rx_crc_errors++;
		goto update_stats;
	}

	/* Handle frames in monitor mode */
	if (priv->iw_mode == NL80211_IFTYPE_MONITOR) {
		orinoco_rx_monitor(dev, rxfid, desc);
		goto out;
	}

	if (status & HERMES_RXSTAT_UNDECRYPTABLE) {
		DEBUG(1, "%s: Undecryptable frame on Rx. Frame dropped.\n",
		      dev->name);
		wstats->discard.code++;
		goto update_stats;
	}

	length = le16_to_cpu(desc->data_len);

	/* Sanity checks */
	if (length < 3) { /* No for even an 802.2 LLC header */
		/* At least on Symbol firmware with PCF we get quite a
		   lot of these legitimately - Poll frames with no
		   data. */
		goto out;
	}
	if (length > IEEE80211_MAX_DATA_LEN) {
		printk(KERN_WARNING "%s: Oversized frame received (%d bytes)\n",
		       dev->name, length);
		stats->rx_length_errors++;
		goto update_stats;
	}

	/* Payload size does not include Michael MIC. Increase payload
	 * size to read it together with the data. */
	if (status & HERMES_RXSTAT_MIC)
		length += MICHAEL_MIC_LEN;

	/* We need space for the packet data itself, plus an ethernet
	   header, plus 2 bytes so we can align the IP header on a
	   32bit boundary, plus 1 byte so we can read in odd length
	   packets from the card, which has an IO granularity of 16
	   bits */
	skb = dev_alloc_skb(length + ETH_HLEN + 2 + 1);
	if (!skb) {
		printk(KERN_WARNING "%s: Can't allocate skb for Rx\n",
		       dev->name);
		goto update_stats;
	}

	/* We'll prepend the header, so reserve space for it.  The worst
	   case is no decapsulation, when 802.3 header is prepended and
	   nothing is removed.  2 is for aligning the IP header.  */
	skb_reserve(skb, ETH_HLEN + 2);

	err = hw->ops->bap_pread(hw, IRQ_BAP, skb_put(skb, length),
				 ALIGN(length, 2), rxfid,
				 HERMES_802_2_OFFSET);
	if (err) {
		printk(KERN_ERR "%s: error %d reading frame. "
		       "Frame dropped.\n", dev->name, err);
		goto drop;
	}

	/* Add desc and skb to rx queue */
	rx_data = kzalloc(sizeof(*rx_data), GFP_ATOMIC);
	if (!rx_data) {
		printk(KERN_WARNING "%s: Can't allocate RX packet\n",
			dev->name);
		goto drop;
	}
	rx_data->desc = desc;
	rx_data->skb = skb;
	list_add_tail(&rx_data->list, &priv->rx_list);
	tasklet_schedule(&priv->rx_tasklet);

	return;

drop:
	dev_kfree_skb_irq(skb);
update_stats:
	stats->rx_errors++;
	stats->rx_dropped++;
out:
	kfree(desc);
}
EXPORT_SYMBOL(__orinoco_ev_rx);

static void orinoco_rx(struct net_device *dev,
		       struct hermes_rx_descriptor *desc,
		       struct sk_buff *skb)
{
	struct orinoco_private *priv = ndev_priv(dev);
	struct net_device_stats *stats = &priv->stats;
	u16 status, fc;
	int length;
	struct ethhdr *hdr;

	status = le16_to_cpu(desc->status);
	length = le16_to_cpu(desc->data_len);
	fc = le16_to_cpu(desc->frame_ctl);

	/* Calculate and check MIC */
	if (status & HERMES_RXSTAT_MIC) {
		struct orinoco_tkip_key *key;
		int key_id = ((status & HERMES_RXSTAT_MIC_KEY_ID) >>
			      HERMES_MIC_KEY_ID_SHIFT);
		u8 mic[MICHAEL_MIC_LEN];
		u8 *rxmic;
		u8 *src = (fc & IEEE80211_FCTL_FROMDS) ?
			desc->addr3 : desc->addr2;

		/* Extract Michael MIC from payload */
		rxmic = skb->data + skb->len - MICHAEL_MIC_LEN;

		skb_trim(skb, skb->len - MICHAEL_MIC_LEN);
		length -= MICHAEL_MIC_LEN;

		key = (struct orinoco_tkip_key *) priv->keys[key_id].key;

		if (!key) {
			printk(KERN_WARNING "%s: Received encrypted frame from "
			       "%pM using key %i, but key is not installed\n",
			       dev->name, src, key_id);
			goto drop;
		}

		orinoco_mic(priv->rx_tfm_mic, key->rx_mic, desc->addr1, src,
			    0, /* priority or QoS? */
			    skb->data, skb->len, &mic[0]);

		if (memcmp(mic, rxmic,
			   MICHAEL_MIC_LEN)) {
			union iwreq_data wrqu;
			struct iw_michaelmicfailure wxmic;

			printk(KERN_WARNING "%s: "
			       "Invalid Michael MIC in data frame from %pM, "
			       "using key %i\n",
			       dev->name, src, key_id);

			/* TODO: update stats */

			/* Notify userspace */
			memset(&wxmic, 0, sizeof(wxmic));
			wxmic.flags = key_id & IW_MICFAILURE_KEY_ID;
			wxmic.flags |= (desc->addr1[0] & 1) ?
				IW_MICFAILURE_GROUP : IW_MICFAILURE_PAIRWISE;
			wxmic.src_addr.sa_family = ARPHRD_ETHER;
			memcpy(wxmic.src_addr.sa_data, src, ETH_ALEN);

			(void) orinoco_hw_get_tkip_iv(priv, key_id,
						      &wxmic.tsc[0]);

			memset(&wrqu, 0, sizeof(wrqu));
			wrqu.data.length = sizeof(wxmic);
			wireless_send_event(dev, IWEVMICHAELMICFAILURE, &wrqu,
					    (char *) &wxmic);

			goto drop;
		}
	}

	/* Handle decapsulation
	 * In most cases, the firmware tell us about SNAP frames.
	 * For some reason, the SNAP frames sent by LinkSys APs
	 * are not properly recognised by most firmwares.
	 * So, check ourselves */
	if (length >= ENCAPS_OVERHEAD &&
	    (((status & HERMES_RXSTAT_MSGTYPE) == HERMES_RXSTAT_1042) ||
	     ((status & HERMES_RXSTAT_MSGTYPE) == HERMES_RXSTAT_TUNNEL) ||
	     is_ethersnap(skb->data))) {
		/* These indicate a SNAP within 802.2 LLC within
		   802.11 frame which we'll need to de-encapsulate to
		   the original EthernetII frame. */
		hdr = (struct ethhdr *)skb_push(skb,
						ETH_HLEN - ENCAPS_OVERHEAD);
	} else {
		/* 802.3 frame - prepend 802.3 header as is */
		hdr = (struct ethhdr *)skb_push(skb, ETH_HLEN);
		hdr->h_proto = htons(length);
	}
	memcpy(hdr->h_dest, desc->addr1, ETH_ALEN);
	if (fc & IEEE80211_FCTL_FROMDS)
		memcpy(hdr->h_source, desc->addr3, ETH_ALEN);
	else
		memcpy(hdr->h_source, desc->addr2, ETH_ALEN);

	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_NONE;
	if (fc & IEEE80211_FCTL_TODS)
		skb->pkt_type = PACKET_OTHERHOST;

	/* Process the wireless stats if needed */
	orinoco_stat_gather(dev, skb, desc);

	/* Pass the packet to the networking stack */
	netif_rx(skb);
	stats->rx_packets++;
	stats->rx_bytes += length;

	return;

 drop:
	dev_kfree_skb(skb);
	stats->rx_errors++;
	stats->rx_dropped++;
}

static void orinoco_rx_isr_tasklet(unsigned long data)
{
	struct orinoco_private *priv = (struct orinoco_private *) data;
	struct net_device *dev = priv->ndev;
	struct orinoco_rx_data *rx_data, *temp;
	struct hermes_rx_descriptor *desc;
	struct sk_buff *skb;
	unsigned long flags;

	/* orinoco_rx requires the driver lock, and we also need to
	 * protect priv->rx_list, so just hold the lock over the
	 * lot.
	 *
	 * If orinoco_lock fails, we've unplugged the card. In this
	 * case just abort. */
	if (orinoco_lock(priv, &flags) != 0)
		return;

	/* extract desc and skb from queue */
	list_for_each_entry_safe(rx_data, temp, &priv->rx_list, list) {
		desc = rx_data->desc;
		skb = rx_data->skb;
		list_del(&rx_data->list);
		kfree(rx_data);

		orinoco_rx(dev, desc, skb);

		kfree(desc);
	}

	orinoco_unlock(priv, &flags);
}

/********************************************************************/
/* Rx path (info frames)                                            */
/********************************************************************/

static void print_linkstatus(struct net_device *dev, u16 status)
{
	char *s;

	if (suppress_linkstatus)
		return;

	switch (status) {
	case HERMES_LINKSTATUS_NOT_CONNECTED:
		s = "Not Connected";
		break;
	case HERMES_LINKSTATUS_CONNECTED:
		s = "Connected";
		break;
	case HERMES_LINKSTATUS_DISCONNECTED:
		s = "Disconnected";
		break;
	case HERMES_LINKSTATUS_AP_CHANGE:
		s = "AP Changed";
		break;
	case HERMES_LINKSTATUS_AP_OUT_OF_RANGE:
		s = "AP Out of Range";
		break;
	case HERMES_LINKSTATUS_AP_IN_RANGE:
		s = "AP In Range";
		break;
	case HERMES_LINKSTATUS_ASSOC_FAILED:
		s = "Association Failed";
		break;
	default:
		s = "UNKNOWN";
	}

	printk(KERN_DEBUG "%s: New link status: %s (%04x)\n",
	       dev->name, s, status);
}

/* Search scan results for requested BSSID, join it if found */
static void orinoco_join_ap(struct work_struct *work)
{
	struct orinoco_private *priv =
		container_of(work, struct orinoco_private, join_work);
	struct net_device *dev = priv->ndev;
	struct hermes *hw = &priv->hw;
	int err;
	unsigned long flags;
	struct join_req {
		u8 bssid[ETH_ALEN];
		__le16 channel;
	} __packed req;
	const int atom_len = offsetof(struct prism2_scan_apinfo, atim);
	struct prism2_scan_apinfo *atom = NULL;
	int offset = 4;
	int found = 0;
	u8 *buf;
	u16 len;

	/* Allocate buffer for scan results */
	buf = kmalloc(MAX_SCAN_LEN, GFP_KERNEL);
	if (!buf)
		return;

	if (orinoco_lock(priv, &flags) != 0)
		goto fail_lock;

	/* Sanity checks in case user changed something in the meantime */
	if (!priv->bssid_fixed)
		goto out;

	if (strlen(priv->desired_essid) == 0)
		goto out;

	/* Read scan results from the firmware */
	err = hw->ops->read_ltv(hw, USER_BAP,
				HERMES_RID_SCANRESULTSTABLE,
				MAX_SCAN_LEN, &len, buf);
	if (err) {
		printk(KERN_ERR "%s: Cannot read scan results\n",
		       dev->name);
		goto out;
	}

	len = HERMES_RECLEN_TO_BYTES(len);

	/* Go through the scan results looking for the channel of the AP
	 * we were requested to join */
	for (; offset + atom_len <= len; offset += atom_len) {
		atom = (struct prism2_scan_apinfo *) (buf + offset);
		if (memcmp(&atom->bssid, priv->desired_bssid, ETH_ALEN) == 0) {
			found = 1;
			break;
		}
	}

	if (!found) {
		DEBUG(1, "%s: Requested AP not found in scan results\n",
		      dev->name);
		goto out;
	}

	memcpy(req.bssid, priv->desired_bssid, ETH_ALEN);
	req.channel = atom->channel;	/* both are little-endian */
	err = HERMES_WRITE_RECORD(hw, USER_BAP, HERMES_RID_CNFJOINREQUEST,
				  &req);
	if (err)
		printk(KERN_ERR "%s: Error issuing join request\n", dev->name);

 out:
	orinoco_unlock(priv, &flags);

 fail_lock:
	kfree(buf);
}

/* Send new BSSID to userspace */
static void orinoco_send_bssid_wevent(struct orinoco_private *priv)
{
	struct net_device *dev = priv->ndev;
	struct hermes *hw = &priv->hw;
	union iwreq_data wrqu;
	int err;

	err = hw->ops->read_ltv(hw, USER_BAP, HERMES_RID_CURRENTBSSID,
				ETH_ALEN, NULL, wrqu.ap_addr.sa_data);
	if (err != 0)
		return;

	wrqu.ap_addr.sa_family = ARPHRD_ETHER;

	/* Send event to user space */
	wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);
}

static void orinoco_send_assocreqie_wevent(struct orinoco_private *priv)
{
	struct net_device *dev = priv->ndev;
	struct hermes *hw = &priv->hw;
	union iwreq_data wrqu;
	int err;
	u8 buf[88];
	u8 *ie;

	if (!priv->has_wpa)
		return;

	err = hw->ops->read_ltv(hw, USER_BAP, HERMES_RID_CURRENT_ASSOC_REQ_INFO,
				sizeof(buf), NULL, &buf);
	if (err != 0)
		return;

	ie = orinoco_get_wpa_ie(buf, sizeof(buf));
	if (ie) {
		int rem = sizeof(buf) - (ie - &buf[0]);
		wrqu.data.length = ie[1] + 2;
		if (wrqu.data.length > rem)
			wrqu.data.length = rem;

		if (wrqu.data.length)
			/* Send event to user space */
			wireless_send_event(dev, IWEVASSOCREQIE, &wrqu, ie);
	}
}

static void orinoco_send_assocrespie_wevent(struct orinoco_private *priv)
{
	struct net_device *dev = priv->ndev;
	struct hermes *hw = &priv->hw;
	union iwreq_data wrqu;
	int err;
	u8 buf[88]; /* TODO: verify max size or IW_GENERIC_IE_MAX */
	u8 *ie;

	if (!priv->has_wpa)
		return;

	err = hw->ops->read_ltv(hw, USER_BAP,
				HERMES_RID_CURRENT_ASSOC_RESP_INFO,
				sizeof(buf), NULL, &buf);
	if (err != 0)
		return;

	ie = orinoco_get_wpa_ie(buf, sizeof(buf));
	if (ie) {
		int rem = sizeof(buf) - (ie - &buf[0]);
		wrqu.data.length = ie[1] + 2;
		if (wrqu.data.length > rem)
			wrqu.data.length = rem;

		if (wrqu.data.length)
			/* Send event to user space */
			wireless_send_event(dev, IWEVASSOCRESPIE, &wrqu, ie);
	}
}

static void orinoco_send_wevents(struct work_struct *work)
{
	struct orinoco_private *priv =
		container_of(work, struct orinoco_private, wevent_work);
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return;

	orinoco_send_assocreqie_wevent(priv);
	orinoco_send_assocrespie_wevent(priv);
	orinoco_send_bssid_wevent(priv);

	orinoco_unlock(priv, &flags);
}

static void qbuf_scan(struct orinoco_private *priv, void *buf,
		      int len, int type)
{
	struct orinoco_scan_data *sd;
	unsigned long flags;

	sd = kmalloc(sizeof(*sd), GFP_ATOMIC);
	sd->buf = buf;
	sd->len = len;
	sd->type = type;

	spin_lock_irqsave(&priv->scan_lock, flags);
	list_add_tail(&sd->list, &priv->scan_list);
	spin_unlock_irqrestore(&priv->scan_lock, flags);

	schedule_work(&priv->process_scan);
}

static void qabort_scan(struct orinoco_private *priv)
{
	struct orinoco_scan_data *sd;
	unsigned long flags;

	sd = kmalloc(sizeof(*sd), GFP_ATOMIC);
	sd->len = -1; /* Abort */

	spin_lock_irqsave(&priv->scan_lock, flags);
	list_add_tail(&sd->list, &priv->scan_list);
	spin_unlock_irqrestore(&priv->scan_lock, flags);

	schedule_work(&priv->process_scan);
}

static void orinoco_process_scan_results(struct work_struct *work)
{
	struct orinoco_private *priv =
		container_of(work, struct orinoco_private, process_scan);
	struct orinoco_scan_data *sd, *temp;
	unsigned long flags;
	void *buf;
	int len;
	int type;

	spin_lock_irqsave(&priv->scan_lock, flags);
	list_for_each_entry_safe(sd, temp, &priv->scan_list, list) {

		buf = sd->buf;
		len = sd->len;
		type = sd->type;

		list_del(&sd->list);
		spin_unlock_irqrestore(&priv->scan_lock, flags);
		kfree(sd);

		if (len > 0) {
			if (type == HERMES_INQ_CHANNELINFO)
				orinoco_add_extscan_result(priv, buf, len);
			else
				orinoco_add_hostscan_results(priv, buf, len);

			kfree(buf);
		} else {
			/* Either abort or complete the scan */
			orinoco_scan_done(priv, (len < 0));
		}

		spin_lock_irqsave(&priv->scan_lock, flags);
	}
	spin_unlock_irqrestore(&priv->scan_lock, flags);
}

void __orinoco_ev_info(struct net_device *dev, struct hermes *hw)
{
	struct orinoco_private *priv = ndev_priv(dev);
	u16 infofid;
	struct {
		__le16 len;
		__le16 type;
	} __packed info;
	int len, type;
	int err;

	/* This is an answer to an INQUIRE command that we did earlier,
	 * or an information "event" generated by the card
	 * The controller return to us a pseudo frame containing
	 * the information in question - Jean II */
	infofid = hermes_read_regn(hw, INFOFID);

	/* Read the info frame header - don't try too hard */
	err = hw->ops->bap_pread(hw, IRQ_BAP, &info, sizeof(info),
				 infofid, 0);
	if (err) {
		printk(KERN_ERR "%s: error %d reading info frame. "
		       "Frame dropped.\n", dev->name, err);
		return;
	}

	len = HERMES_RECLEN_TO_BYTES(le16_to_cpu(info.len));
	type = le16_to_cpu(info.type);

	switch (type) {
	case HERMES_INQ_TALLIES: {
		struct hermes_tallies_frame tallies;
		struct iw_statistics *wstats = &priv->wstats;

		if (len > sizeof(tallies)) {
			printk(KERN_WARNING "%s: Tallies frame too long (%d bytes)\n",
			       dev->name, len);
			len = sizeof(tallies);
		}

		err = hw->ops->bap_pread(hw, IRQ_BAP, &tallies, len,
					 infofid, sizeof(info));
		if (err)
			break;

		/* Increment our various counters */
		/* wstats->discard.nwid - no wrong BSSID stuff */
		wstats->discard.code +=
			le16_to_cpu(tallies.RxWEPUndecryptable);
		if (len == sizeof(tallies))
			wstats->discard.code +=
				le16_to_cpu(tallies.RxDiscards_WEPICVError) +
				le16_to_cpu(tallies.RxDiscards_WEPExcluded);
		wstats->discard.misc +=
			le16_to_cpu(tallies.TxDiscardsWrongSA);
		wstats->discard.fragment +=
			le16_to_cpu(tallies.RxMsgInBadMsgFragments);
		wstats->discard.retries +=
			le16_to_cpu(tallies.TxRetryLimitExceeded);
		/* wstats->miss.beacon - no match */
	}
	break;
	case HERMES_INQ_LINKSTATUS: {
		struct hermes_linkstatus linkstatus;
		u16 newstatus;
		int connected;

		if (priv->iw_mode == NL80211_IFTYPE_MONITOR)
			break;

		if (len != sizeof(linkstatus)) {
			printk(KERN_WARNING "%s: Unexpected size for linkstatus frame (%d bytes)\n",
			       dev->name, len);
			break;
		}

		err = hw->ops->bap_pread(hw, IRQ_BAP, &linkstatus, len,
					 infofid, sizeof(info));
		if (err)
			break;
		newstatus = le16_to_cpu(linkstatus.linkstatus);

		/* Symbol firmware uses "out of range" to signal that
		 * the hostscan frame can be requested.  */
		if (newstatus == HERMES_LINKSTATUS_AP_OUT_OF_RANGE &&
		    priv->firmware_type == FIRMWARE_TYPE_SYMBOL &&
		    priv->has_hostscan && priv->scan_request) {
			hermes_inquire(hw, HERMES_INQ_HOSTSCAN_SYMBOL);
			break;
		}

		connected = (newstatus == HERMES_LINKSTATUS_CONNECTED)
			|| (newstatus == HERMES_LINKSTATUS_AP_CHANGE)
			|| (newstatus == HERMES_LINKSTATUS_AP_IN_RANGE);

		if (connected)
			netif_carrier_on(dev);
		else if (!ignore_disconnect)
			netif_carrier_off(dev);

		if (newstatus != priv->last_linkstatus) {
			priv->last_linkstatus = newstatus;
			print_linkstatus(dev, newstatus);
			/* The info frame contains only one word which is the
			 * status (see hermes.h). The status is pretty boring
			 * in itself, that's why we export the new BSSID...
			 * Jean II */
			schedule_work(&priv->wevent_work);
		}
	}
	break;
	case HERMES_INQ_SCAN:
		if (!priv->scan_request && priv->bssid_fixed &&
		    priv->firmware_type == FIRMWARE_TYPE_INTERSIL) {
			schedule_work(&priv->join_work);
			break;
		}
		/* fall through */
	case HERMES_INQ_HOSTSCAN:
	case HERMES_INQ_HOSTSCAN_SYMBOL: {
		/* Result of a scanning. Contains information about
		 * cells in the vicinity - Jean II */
		unsigned char *buf;

		/* Sanity check */
		if (len > 4096) {
			printk(KERN_WARNING "%s: Scan results too large (%d bytes)\n",
			       dev->name, len);
			qabort_scan(priv);
			break;
		}

		/* Allocate buffer for results */
		buf = kmalloc(len, GFP_ATOMIC);
		if (buf == NULL) {
			/* No memory, so can't printk()... */
			qabort_scan(priv);
			break;
		}

		/* Read scan data */
		err = hw->ops->bap_pread(hw, IRQ_BAP, (void *) buf, len,
					 infofid, sizeof(info));
		if (err) {
			kfree(buf);
			qabort_scan(priv);
			break;
		}

#ifdef ORINOCO_DEBUG
		{
			int	i;
			printk(KERN_DEBUG "Scan result [%02X", buf[0]);
			for (i = 1; i < (len * 2); i++)
				printk(":%02X", buf[i]);
			printk("]\n");
		}
#endif	/* ORINOCO_DEBUG */

		qbuf_scan(priv, buf, len, type);
	}
	break;
	case HERMES_INQ_CHANNELINFO:
	{
		struct agere_ext_scan_info *bss;

		if (!priv->scan_request) {
			printk(KERN_DEBUG "%s: Got chaninfo without scan, "
			       "len=%d\n", dev->name, len);
			break;
		}

		/* An empty result indicates that the scan is complete */
		if (len == 0) {
			qbuf_scan(priv, NULL, len, type);
			break;
		}

		/* Sanity check */
		else if (len < (offsetof(struct agere_ext_scan_info,
					   data) + 2)) {
			/* Drop this result now so we don't have to
			 * keep checking later */
			printk(KERN_WARNING
			       "%s: Ext scan results too short (%d bytes)\n",
			       dev->name, len);
			break;
		}

		bss = kmalloc(len, GFP_ATOMIC);
		if (bss == NULL)
			break;

		/* Read scan data */
		err = hw->ops->bap_pread(hw, IRQ_BAP, (void *) bss, len,
					 infofid, sizeof(info));
		if (err)
			kfree(bss);
		else
			qbuf_scan(priv, bss, len, type);

		break;
	}
	case HERMES_INQ_SEC_STAT_AGERE:
		/* Security status (Agere specific) */
		/* Ignore this frame for now */
		if (priv->firmware_type == FIRMWARE_TYPE_AGERE)
			break;
		/* fall through */
	default:
		printk(KERN_DEBUG "%s: Unknown information frame received: "
		       "type 0x%04x, length %d\n", dev->name, type, len);
		/* We don't actually do anything about it */
		break;
	}
}
EXPORT_SYMBOL(__orinoco_ev_info);

static void __orinoco_ev_infdrop(struct net_device *dev, struct hermes *hw)
{
	if (net_ratelimit())
		printk(KERN_DEBUG "%s: Information frame lost.\n", dev->name);
}

/********************************************************************/
/* Internal hardware control routines                               */
/********************************************************************/

static int __orinoco_up(struct orinoco_private *priv)
{
	struct net_device *dev = priv->ndev;
	struct hermes *hw = &priv->hw;
	int err;

	netif_carrier_off(dev); /* just to make sure */

	err = __orinoco_commit(priv);
	if (err) {
		printk(KERN_ERR "%s: Error %d configuring card\n",
		       dev->name, err);
		return err;
	}

	/* Fire things up again */
	hermes_set_irqmask(hw, ORINOCO_INTEN);
	err = hermes_enable_port(hw, 0);
	if (err) {
		printk(KERN_ERR "%s: Error %d enabling MAC port\n",
		       dev->name, err);
		return err;
	}

	netif_start_queue(dev);

	return 0;
}

static int __orinoco_down(struct orinoco_private *priv)
{
	struct net_device *dev = priv->ndev;
	struct hermes *hw = &priv->hw;
	int err;

	netif_stop_queue(dev);

	if (!priv->hw_unavailable) {
		if (!priv->broken_disableport) {
			err = hermes_disable_port(hw, 0);
			if (err) {
				/* Some firmwares (e.g. Intersil 1.3.x) seem
				 * to have problems disabling the port, oh
				 * well, too bad. */
				printk(KERN_WARNING "%s: Error %d disabling MAC port\n",
				       dev->name, err);
				priv->broken_disableport = 1;
			}
		}
		hermes_set_irqmask(hw, 0);
		hermes_write_regn(hw, EVACK, 0xffff);
	}

	orinoco_scan_done(priv, true);

	/* firmware will have to reassociate */
	netif_carrier_off(dev);
	priv->last_linkstatus = 0xffff;

	return 0;
}

static int orinoco_reinit_firmware(struct orinoco_private *priv)
{
	struct hermes *hw = &priv->hw;
	int err;

	err = hw->ops->init(hw);
	if (priv->do_fw_download && !err) {
		err = orinoco_download(priv);
		if (err)
			priv->do_fw_download = 0;
	}
	if (!err)
		err = orinoco_hw_allocate_fid(priv);

	return err;
}

static int
__orinoco_set_multicast_list(struct net_device *dev)
{
	struct orinoco_private *priv = ndev_priv(dev);
	int err = 0;
	int promisc, mc_count;

	/* The Hermes doesn't seem to have an allmulti mode, so we go
	 * into promiscuous mode and let the upper levels deal. */
	if ((dev->flags & IFF_PROMISC) || (dev->flags & IFF_ALLMULTI) ||
	    (netdev_mc_count(dev) > MAX_MULTICAST(priv))) {
		promisc = 1;
		mc_count = 0;
	} else {
		promisc = 0;
		mc_count = netdev_mc_count(dev);
	}

	err = __orinoco_hw_set_multicast_list(priv, dev, mc_count, promisc);

	return err;
}

/* This must be called from user context, without locks held - use
 * schedule_work() */
void orinoco_reset(struct work_struct *work)
{
	struct orinoco_private *priv =
		container_of(work, struct orinoco_private, reset_work);
	struct net_device *dev = priv->ndev;
	struct hermes *hw = &priv->hw;
	int err;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		/* When the hardware becomes available again, whatever
		 * detects that is responsible for re-initializing
		 * it. So no need for anything further */
		return;

	netif_stop_queue(dev);

	/* Shut off interrupts.  Depending on what state the hardware
	 * is in, this might not work, but we'll try anyway */
	hermes_set_irqmask(hw, 0);
	hermes_write_regn(hw, EVACK, 0xffff);

	priv->hw_unavailable++;
	priv->last_linkstatus = 0xffff; /* firmware will have to reassociate */
	netif_carrier_off(dev);

	orinoco_unlock(priv, &flags);

	/* Scanning support: Notify scan cancellation */
	orinoco_scan_done(priv, true);

	if (priv->hard_reset) {
		err = (*priv->hard_reset)(priv);
		if (err) {
			printk(KERN_ERR "%s: orinoco_reset: Error %d "
			       "performing hard reset\n", dev->name, err);
			goto disable;
		}
	}

	err = orinoco_reinit_firmware(priv);
	if (err) {
		printk(KERN_ERR "%s: orinoco_reset: Error %d re-initializing firmware\n",
		       dev->name, err);
		goto disable;
	}

	/* This has to be called from user context */
	orinoco_lock_irq(priv);

	priv->hw_unavailable--;

	/* priv->open or priv->hw_unavailable might have changed while
	 * we dropped the lock */
	if (priv->open && (!priv->hw_unavailable)) {
		err = __orinoco_up(priv);
		if (err) {
			printk(KERN_ERR "%s: orinoco_reset: Error %d reenabling card\n",
			       dev->name, err);
		} else
			dev->trans_start = jiffies;
	}

	orinoco_unlock_irq(priv);

	return;
 disable:
	hermes_set_irqmask(hw, 0);
	netif_device_detach(dev);
	printk(KERN_ERR "%s: Device has been disabled!\n", dev->name);
}

static int __orinoco_commit(struct orinoco_private *priv)
{
	struct net_device *dev = priv->ndev;
	int err = 0;

	/* If we've called commit, we are reconfiguring or bringing the
	 * interface up. Maintaining countermeasures across this would
	 * be confusing, so note that we've disabled them. The port will
	 * be enabled later in orinoco_commit or __orinoco_up. */
	priv->tkip_cm_active = 0;

	err = orinoco_hw_program_rids(priv);

	/* FIXME: what about netif_tx_lock */
	(void) __orinoco_set_multicast_list(dev);

	return err;
}

/* Ensures configuration changes are applied. May result in a reset.
 * The caller should hold priv->lock
 */
int orinoco_commit(struct orinoco_private *priv)
{
	struct net_device *dev = priv->ndev;
	struct hermes *hw = &priv->hw;
	int err;

	if (priv->broken_disableport) {
		schedule_work(&priv->reset_work);
		return 0;
	}

	err = hermes_disable_port(hw, 0);
	if (err) {
		printk(KERN_WARNING "%s: Unable to disable port "
		       "while reconfiguring card\n", dev->name);
		priv->broken_disableport = 1;
		goto out;
	}

	err = __orinoco_commit(priv);
	if (err) {
		printk(KERN_WARNING "%s: Unable to reconfigure card\n",
		       dev->name);
		goto out;
	}

	err = hermes_enable_port(hw, 0);
	if (err) {
		printk(KERN_WARNING "%s: Unable to enable port while reconfiguring card\n",
		       dev->name);
		goto out;
	}

 out:
	if (err) {
		printk(KERN_WARNING "%s: Resetting instead...\n", dev->name);
		schedule_work(&priv->reset_work);
		err = 0;
	}
	return err;
}

/********************************************************************/
/* Interrupt handler                                                */
/********************************************************************/

static void __orinoco_ev_tick(struct net_device *dev, struct hermes *hw)
{
	printk(KERN_DEBUG "%s: TICK\n", dev->name);
}

static void __orinoco_ev_wterr(struct net_device *dev, struct hermes *hw)
{
	/* This seems to happen a fair bit under load, but ignoring it
	   seems to work fine...*/
	printk(KERN_DEBUG "%s: MAC controller error (WTERR). Ignoring.\n",
	       dev->name);
}

irqreturn_t orinoco_interrupt(int irq, void *dev_id)
{
	struct orinoco_private *priv = dev_id;
	struct net_device *dev = priv->ndev;
	struct hermes *hw = &priv->hw;
	int count = MAX_IRQLOOPS_PER_IRQ;
	u16 evstat, events;
	/* These are used to detect a runaway interrupt situation.
	 *
	 * If we get more than MAX_IRQLOOPS_PER_JIFFY iterations in a jiffy,
	 * we panic and shut down the hardware
	 */
	/* jiffies value the last time we were called */
	static int last_irq_jiffy; /* = 0 */
	static int loops_this_jiffy; /* = 0 */
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0) {
		/* If hw is unavailable - we don't know if the irq was
		 * for us or not */
		return IRQ_HANDLED;
	}

	evstat = hermes_read_regn(hw, EVSTAT);
	events = evstat & hw->inten;
	if (!events) {
		orinoco_unlock(priv, &flags);
		return IRQ_NONE;
	}

	if (jiffies != last_irq_jiffy)
		loops_this_jiffy = 0;
	last_irq_jiffy = jiffies;

	while (events && count--) {
		if (++loops_this_jiffy > MAX_IRQLOOPS_PER_JIFFY) {
			printk(KERN_WARNING "%s: IRQ handler is looping too "
			       "much! Resetting.\n", dev->name);
			/* Disable interrupts for now */
			hermes_set_irqmask(hw, 0);
			schedule_work(&priv->reset_work);
			break;
		}

		/* Check the card hasn't been removed */
		if (!hermes_present(hw)) {
			DEBUG(0, "orinoco_interrupt(): card removed\n");
			break;
		}

		if (events & HERMES_EV_TICK)
			__orinoco_ev_tick(dev, hw);
		if (events & HERMES_EV_WTERR)
			__orinoco_ev_wterr(dev, hw);
		if (events & HERMES_EV_INFDROP)
			__orinoco_ev_infdrop(dev, hw);
		if (events & HERMES_EV_INFO)
			__orinoco_ev_info(dev, hw);
		if (events & HERMES_EV_RX)
			__orinoco_ev_rx(dev, hw);
		if (events & HERMES_EV_TXEXC)
			__orinoco_ev_txexc(dev, hw);
		if (events & HERMES_EV_TX)
			__orinoco_ev_tx(dev, hw);
		if (events & HERMES_EV_ALLOC)
			__orinoco_ev_alloc(dev, hw);

		hermes_write_regn(hw, EVACK, evstat);

		evstat = hermes_read_regn(hw, EVSTAT);
		events = evstat & hw->inten;
	}

	orinoco_unlock(priv, &flags);
	return IRQ_HANDLED;
}
EXPORT_SYMBOL(orinoco_interrupt);

/********************************************************************/
/* Power management                                                 */
/********************************************************************/
#if defined(CONFIG_PM_SLEEP) && !defined(CONFIG_HERMES_CACHE_FW_ON_INIT)
static int orinoco_pm_notifier(struct notifier_block *notifier,
			       unsigned long pm_event,
			       void *unused)
{
	struct orinoco_private *priv = container_of(notifier,
						    struct orinoco_private,
						    pm_notifier);

	/* All we need to do is cache the firmware before suspend, and
	 * release it when we come out.
	 *
	 * Only need to do this if we're downloading firmware. */
	if (!priv->do_fw_download)
		return NOTIFY_DONE;

	switch (pm_event) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		orinoco_cache_fw(priv, 0);
		break;

	case PM_POST_RESTORE:
		/* Restore from hibernation failed. We need to clean
		 * up in exactly the same way, so fall through. */
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		orinoco_uncache_fw(priv);
		break;

	case PM_RESTORE_PREPARE:
	default:
		break;
	}

	return NOTIFY_DONE;
}

static void orinoco_register_pm_notifier(struct orinoco_private *priv)
{
	priv->pm_notifier.notifier_call = orinoco_pm_notifier;
	register_pm_notifier(&priv->pm_notifier);
}

static void orinoco_unregister_pm_notifier(struct orinoco_private *priv)
{
	unregister_pm_notifier(&priv->pm_notifier);
}
#else /* !PM_SLEEP || HERMES_CACHE_FW_ON_INIT */
#define orinoco_register_pm_notifier(priv) do { } while (0)
#define orinoco_unregister_pm_notifier(priv) do { } while (0)
#endif

/********************************************************************/
/* Initialization                                                   */
/********************************************************************/

int orinoco_init(struct orinoco_private *priv)
{
	struct device *dev = priv->dev;
	struct wiphy *wiphy = priv_to_wiphy(priv);
	struct hermes *hw = &priv->hw;
	int err = 0;

	/* No need to lock, the hw_unavailable flag is already set in
	 * alloc_orinocodev() */
	priv->nicbuf_size = IEEE80211_MAX_FRAME_LEN + ETH_HLEN;

	/* Initialize the firmware */
	err = hw->ops->init(hw);
	if (err != 0) {
		dev_err(dev, "Failed to initialize firmware (err = %d)\n",
			err);
		goto out;
	}

	err = determine_fw_capabilities(priv, wiphy->fw_version,
					sizeof(wiphy->fw_version),
					&wiphy->hw_version);
	if (err != 0) {
		dev_err(dev, "Incompatible firmware, aborting\n");
		goto out;
	}

	if (priv->do_fw_download) {
#ifdef CONFIG_HERMES_CACHE_FW_ON_INIT
		orinoco_cache_fw(priv, 0);
#endif

		err = orinoco_download(priv);
		if (err)
			priv->do_fw_download = 0;

		/* Check firmware version again */
		err = determine_fw_capabilities(priv, wiphy->fw_version,
						sizeof(wiphy->fw_version),
						&wiphy->hw_version);
		if (err != 0) {
			dev_err(dev, "Incompatible firmware, aborting\n");
			goto out;
		}
	}

	if (priv->has_port3)
		dev_info(dev, "Ad-hoc demo mode supported\n");
	if (priv->has_ibss)
		dev_info(dev, "IEEE standard IBSS ad-hoc mode supported\n");
	if (priv->has_wep)
		dev_info(dev, "WEP supported, %s-bit key\n",
			 priv->has_big_wep ? "104" : "40");
	if (priv->has_wpa) {
		dev_info(dev, "WPA-PSK supported\n");
		if (orinoco_mic_init(priv)) {
			dev_err(dev, "Failed to setup MIC crypto algorithm. "
				"Disabling WPA support\n");
			priv->has_wpa = 0;
		}
	}

	err = orinoco_hw_read_card_settings(priv, wiphy->perm_addr);
	if (err)
		goto out;

	err = orinoco_hw_allocate_fid(priv);
	if (err) {
		dev_err(dev, "Failed to allocate NIC buffer!\n");
		goto out;
	}

	/* Set up the default configuration */
	priv->iw_mode = NL80211_IFTYPE_STATION;
	/* By default use IEEE/IBSS ad-hoc mode if we have it */
	priv->prefer_port3 = priv->has_port3 && (!priv->has_ibss);
	set_port_type(priv);
	priv->channel = 0; /* use firmware default */

	priv->promiscuous = 0;
	priv->encode_alg = ORINOCO_ALG_NONE;
	priv->tx_key = 0;
	priv->wpa_enabled = 0;
	priv->tkip_cm_active = 0;
	priv->key_mgmt = 0;
	priv->wpa_ie_len = 0;
	priv->wpa_ie = NULL;

	if (orinoco_wiphy_register(wiphy)) {
		err = -ENODEV;
		goto out;
	}

	/* Make the hardware available, as long as it hasn't been
	 * removed elsewhere (e.g. by PCMCIA hot unplug) */
	orinoco_lock_irq(priv);
	priv->hw_unavailable--;
	orinoco_unlock_irq(priv);

	dev_dbg(dev, "Ready\n");

 out:
	return err;
}
EXPORT_SYMBOL(orinoco_init);

static const struct net_device_ops orinoco_netdev_ops = {
	.ndo_open		= orinoco_open,
	.ndo_stop		= orinoco_stop,
	.ndo_start_xmit		= orinoco_xmit,
	.ndo_set_multicast_list	= orinoco_set_multicast_list,
	.ndo_change_mtu		= orinoco_change_mtu,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_tx_timeout		= orinoco_tx_timeout,
	.ndo_get_stats		= orinoco_get_stats,
};

/* Allocate private data.
 *
 * This driver has a number of structures associated with it
 *  netdev - Net device structure for each network interface
 *  wiphy - structure associated with wireless phy
 *  wireless_dev (wdev) - structure for each wireless interface
 *  hw - structure for hermes chip info
 *  card - card specific structure for use by the card driver
 *         (airport, orinoco_cs)
 *  priv - orinoco private data
 *  device - generic linux device structure
 *
 *  +---------+    +---------+
 *  |  wiphy  |    | netdev  |
 *  | +-------+    | +-------+
 *  | | priv  |    | | wdev  |
 *  | | +-----+    +-+-------+
 *  | | | hw  |
 *  | +-+-----+
 *  | | card  |
 *  +-+-------+
 *
 * priv has a link to netdev and device
 * wdev has a link to wiphy
 */
struct orinoco_private
*alloc_orinocodev(int sizeof_card,
		  struct device *device,
		  int (*hard_reset)(struct orinoco_private *),
		  int (*stop_fw)(struct orinoco_private *, int))
{
	struct orinoco_private *priv;
	struct wiphy *wiphy;

	/* allocate wiphy
	 * NOTE: We only support a single virtual interface
	 *       but this may change when monitor mode is added
	 */
	wiphy = wiphy_new(&orinoco_cfg_ops,
			  sizeof(struct orinoco_private) + sizeof_card);
	if (!wiphy)
		return NULL;

	priv = wiphy_priv(wiphy);
	priv->dev = device;

	if (sizeof_card)
		priv->card = (void *)((unsigned long)priv
				      + sizeof(struct orinoco_private));
	else
		priv->card = NULL;

	orinoco_wiphy_init(wiphy);

#ifdef WIRELESS_SPY
	priv->wireless_data.spy_data = &priv->spy_data;
#endif

	/* Set up default callbacks */
	priv->hard_reset = hard_reset;
	priv->stop_fw = stop_fw;

	spin_lock_init(&priv->lock);
	priv->open = 0;
	priv->hw_unavailable = 1; /* orinoco_init() must clear this
				   * before anything else touches the
				   * hardware */
	INIT_WORK(&priv->reset_work, orinoco_reset);
	INIT_WORK(&priv->join_work, orinoco_join_ap);
	INIT_WORK(&priv->wevent_work, orinoco_send_wevents);

	INIT_LIST_HEAD(&priv->rx_list);
	tasklet_init(&priv->rx_tasklet, orinoco_rx_isr_tasklet,
		     (unsigned long) priv);

	spin_lock_init(&priv->scan_lock);
	INIT_LIST_HEAD(&priv->scan_list);
	INIT_WORK(&priv->process_scan, orinoco_process_scan_results);

	priv->last_linkstatus = 0xffff;

#if defined(CONFIG_HERMES_CACHE_FW_ON_INIT) || defined(CONFIG_PM_SLEEP)
	priv->cached_pri_fw = NULL;
	priv->cached_fw = NULL;
#endif

	/* Register PM notifiers */
	orinoco_register_pm_notifier(priv);

	return priv;
}
EXPORT_SYMBOL(alloc_orinocodev);

/* We can only support a single interface. We provide a separate
 * function to set it up to distinguish between hardware
 * initialisation and interface setup.
 *
 * The base_addr and irq parameters are passed on to netdev for use
 * with SIOCGIFMAP.
 */
int orinoco_if_add(struct orinoco_private *priv,
		   unsigned long base_addr,
		   unsigned int irq,
		   const struct net_device_ops *ops)
{
	struct wiphy *wiphy = priv_to_wiphy(priv);
	struct wireless_dev *wdev;
	struct net_device *dev;
	int ret;

	dev = alloc_etherdev(sizeof(struct wireless_dev));

	if (!dev)
		return -ENOMEM;

	/* Initialise wireless_dev */
	wdev = netdev_priv(dev);
	wdev->wiphy = wiphy;
	wdev->iftype = NL80211_IFTYPE_STATION;

	/* Setup / override net_device fields */
	dev->ieee80211_ptr = wdev;
	dev->watchdog_timeo = HZ; /* 1 second timeout */
	dev->wireless_handlers = &orinoco_handler_def;
#ifdef WIRELESS_SPY
	dev->wireless_data = &priv->wireless_data;
#endif
	/* Default to standard ops if not set */
	if (ops)
		dev->netdev_ops = ops;
	else
		dev->netdev_ops = &orinoco_netdev_ops;

	/* we use the default eth_mac_addr for setting the MAC addr */

	/* Reserve space in skb for the SNAP header */
	dev->needed_headroom = ENCAPS_OVERHEAD;

	netif_carrier_off(dev);

	memcpy(dev->dev_addr, wiphy->perm_addr, ETH_ALEN);
	memcpy(dev->perm_addr, wiphy->perm_addr, ETH_ALEN);

	dev->base_addr = base_addr;
	dev->irq = irq;

	SET_NETDEV_DEV(dev, priv->dev);
	ret = register_netdev(dev);
	if (ret)
		goto fail;

	priv->ndev = dev;

	/* Report what we've done */
	dev_dbg(priv->dev, "Registerred interface %s.\n", dev->name);

	return 0;

 fail:
	free_netdev(dev);
	return ret;
}
EXPORT_SYMBOL(orinoco_if_add);

void orinoco_if_del(struct orinoco_private *priv)
{
	struct net_device *dev = priv->ndev;

	unregister_netdev(dev);
	free_netdev(dev);
}
EXPORT_SYMBOL(orinoco_if_del);

void free_orinocodev(struct orinoco_private *priv)
{
	struct wiphy *wiphy = priv_to_wiphy(priv);
	struct orinoco_rx_data *rx_data, *temp;
	struct orinoco_scan_data *sd, *sdtemp;

	wiphy_unregister(wiphy);

	/* If the tasklet is scheduled when we call tasklet_kill it
	 * will run one final time. However the tasklet will only
	 * drain priv->rx_list if the hw is still available. */
	tasklet_kill(&priv->rx_tasklet);

	/* Explicitly drain priv->rx_list */
	list_for_each_entry_safe(rx_data, temp, &priv->rx_list, list) {
		list_del(&rx_data->list);

		dev_kfree_skb(rx_data->skb);
		kfree(rx_data->desc);
		kfree(rx_data);
	}

	cancel_work_sync(&priv->process_scan);
	/* Explicitly drain priv->scan_list */
	list_for_each_entry_safe(sd, sdtemp, &priv->scan_list, list) {
		list_del(&sd->list);

		if ((sd->len > 0) && sd->buf)
			kfree(sd->buf);
		kfree(sd);
	}

	orinoco_unregister_pm_notifier(priv);
	orinoco_uncache_fw(priv);

	priv->wpa_ie_len = 0;
	kfree(priv->wpa_ie);
	orinoco_mic_free(priv);
	wiphy_free(wiphy);
}
EXPORT_SYMBOL(free_orinocodev);

int orinoco_up(struct orinoco_private *priv)
{
	struct net_device *dev = priv->ndev;
	unsigned long flags;
	int err;

	priv->hw.ops->lock_irqsave(&priv->lock, &flags);

	err = orinoco_reinit_firmware(priv);
	if (err) {
		printk(KERN_ERR "%s: Error %d re-initializing firmware\n",
		       dev->name, err);
		goto exit;
	}

	netif_device_attach(dev);
	priv->hw_unavailable--;

	if (priv->open && !priv->hw_unavailable) {
		err = __orinoco_up(priv);
		if (err)
			printk(KERN_ERR "%s: Error %d restarting card\n",
			       dev->name, err);
	}

exit:
	priv->hw.ops->unlock_irqrestore(&priv->lock, &flags);

	return 0;
}
EXPORT_SYMBOL(orinoco_up);

void orinoco_down(struct orinoco_private *priv)
{
	struct net_device *dev = priv->ndev;
	unsigned long flags;
	int err;

	priv->hw.ops->lock_irqsave(&priv->lock, &flags);
	err = __orinoco_down(priv);
	if (err)
		printk(KERN_WARNING "%s: Error %d downing interface\n",
		       dev->name, err);

	netif_device_detach(dev);
	priv->hw_unavailable++;
	priv->hw.ops->unlock_irqrestore(&priv->lock, &flags);
}
EXPORT_SYMBOL(orinoco_down);

/********************************************************************/
/* Module initialization                                            */
/********************************************************************/

/* Can't be declared "const" or the whole __initdata section will
 * become const */
static char version[] __initdata = DRIVER_NAME " " DRIVER_VERSION
	" (David Gibson <hermes@gibson.dropbear.id.au>, "
	"Pavel Roskin <proski@gnu.org>, et al)";

static int __init init_orinoco(void)
{
	printk(KERN_DEBUG "%s\n", version);
	return 0;
}

static void __exit exit_orinoco(void)
{
}

module_init(init_orinoco);
module_exit(exit_orinoco);
