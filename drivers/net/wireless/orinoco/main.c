/* main.c - (formerly known as dldwd_cs.c, orinoco_cs.c and orinoco.c)
 *
 * A driver for Hermes or Prism 2 chipset based PCMCIA wireless
 * adaptors, with Lucent/Agere, Intersil or Symbol firmware.
 *
 * Current maintainers (as of 29 September 2003) are:
 * 	Pavel Roskin <proski AT gnu.org>
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
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/suspend.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <net/iw_handler.h>

#include "hermes_rid.h"
#include "hermes_dld.h"
#include "hw.h"
#include "scan.h"
#include "mic.h"
#include "fw.h"
#include "wext.h"
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

#define SYMBOL_MAX_VER_LEN	(14)
#define MAX_IRQLOOPS_PER_IRQ	10
#define MAX_IRQLOOPS_PER_JIFFY	(20000/HZ) /* Based on a guestimate of
					    * how many events the
					    * device could
					    * legitimately generate */
#define TX_NICBUF_SIZE_BUG	1585		/* Bug in Symbol firmware */

#define DUMMY_FID		0xFFFF

/*#define MAX_MULTICAST(priv)	(priv->firmware_type == FIRMWARE_TYPE_AGERE ? \
  HERMES_MAX_MULTICAST : 0)*/
#define MAX_MULTICAST(priv)	(HERMES_MAX_MULTICAST)

#define ORINOCO_INTEN	 	(HERMES_EV_RX | HERMES_EV_ALLOC \
				 | HERMES_EV_TX | HERMES_EV_TXEXC \
				 | HERMES_EV_WTERR | HERMES_EV_INFO \
				 | HERMES_EV_INFDROP)

static const struct ethtool_ops orinoco_ethtool_ops;

/********************************************************************/
/* Data types                                                       */
/********************************************************************/

/* Beginning of the Tx descriptor, used in TxExc handling */
struct hermes_txexc_data {
	struct hermes_tx_descriptor desc;
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
} __attribute__ ((packed));

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
} __attribute__ ((packed));

struct orinoco_rx_data {
	struct hermes_rx_descriptor *desc;
	struct sk_buff *skb;
	struct list_head list;
};

/********************************************************************/
/* Function prototypes                                              */
/********************************************************************/

static void __orinoco_set_multicast_list(struct net_device *dev);

/********************************************************************/
/* Internal helper functions                                        */
/********************************************************************/

void set_port_type(struct orinoco_private *priv)
{
	switch (priv->iw_mode) {
	case IW_MODE_INFRA:
		priv->port_type = 1;
		priv->createibss = 0;
		break;
	case IW_MODE_ADHOC:
		if (priv->prefer_port3) {
			priv->port_type = 3;
			priv->createibss = 0;
		} else {
			priv->port_type = priv->ibss_port;
			priv->createibss = 1;
		}
		break;
	case IW_MODE_MONITOR:
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

static int orinoco_open(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;
	int err;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	err = __orinoco_up(dev);

	if (!err)
		priv->open = 1;

	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_stop(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = 0;

	/* We mustn't use orinoco_lock() here, because we need to be
	   able to close the interface even if hw_unavailable is set
	   (e.g. as we're released after a PC Card removal) */
	spin_lock_irq(&priv->lock);

	priv->open = 0;

	err = __orinoco_down(dev);

	spin_unlock_irq(&priv->lock);

	return err;
}

static struct net_device_stats *orinoco_get_stats(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);

	return &priv->stats;
}

static void orinoco_set_multicast_list(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0) {
		printk(KERN_DEBUG "%s: orinoco_set_multicast_list() "
		       "called when hw_unavailable\n", dev->name);
		return;
	}

	__orinoco_set_multicast_list(dev);
	orinoco_unlock(priv, &flags);
}

static int orinoco_change_mtu(struct net_device *dev, int new_mtu)
{
	struct orinoco_private *priv = netdev_priv(dev);

	if ((new_mtu < ORINOCO_MIN_MTU) || (new_mtu > ORINOCO_MAX_MTU))
		return -EINVAL;

	/* MTU + encapsulation + header length */
	if ((new_mtu + ENCAPS_OVERHEAD + sizeof(struct ieee80211_hdr)) >
	     (priv->nicbuf_size - ETH_HLEN))
		return -EINVAL;

	dev->mtu = new_mtu;

	return 0;
}

/********************************************************************/
/* Tx path                                                          */
/********************************************************************/

static int orinoco_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct net_device_stats *stats = &priv->stats;
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 txfid = priv->txfid;
	struct ethhdr *eh;
	int tx_control;
	unsigned long flags;

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

	if (!netif_carrier_ok(dev) || (priv->iw_mode == IW_MODE_MONITOR)) {
		/* Oops, the firmware hasn't established a connection,
		   silently drop the packet (this seems to be the
		   safest approach). */
		goto drop;
	}

	/* Check packet length */
	if (skb->len < ETH_HLEN)
		goto drop;

	tx_control = HERMES_TXCTRL_TX_OK | HERMES_TXCTRL_TX_EX;

	if (priv->encode_alg == IW_ENCODE_ALG_TKIP)
		tx_control |= (priv->tx_key << HERMES_MIC_KEY_ID_SHIFT) |
			HERMES_TXCTRL_MIC;

	if (priv->has_alt_txcntl) {
		/* WPA enabled firmwares have tx_cntl at the end of
		 * the 802.11 header.  So write zeroed descriptor and
		 * 802.11 header at the same time
		 */
		char desc[HERMES_802_3_OFFSET];
		__le16 *txcntl = (__le16 *) &desc[HERMES_TXCNTL2_OFFSET];

		memset(&desc, 0, sizeof(desc));

		*txcntl = cpu_to_le16(tx_control);
		err = hermes_bap_pwrite(hw, USER_BAP, &desc, sizeof(desc),
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
		err = hermes_bap_pwrite(hw, USER_BAP, &desc, sizeof(desc),
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

	eh = (struct ethhdr *)skb->data;

	/* Encapsulate Ethernet-II frames */
	if (ntohs(eh->h_proto) > ETH_DATA_LEN) { /* Ethernet-II frame */
		struct header_struct {
			struct ethhdr eth;	/* 802.3 header */
			u8 encap[6];		/* 802.2 header */
		} __attribute__ ((packed)) hdr;

		/* Strip destination and source from the data */
		skb_pull(skb, 2 * ETH_ALEN);

		/* And move them to a separate header */
		memcpy(&hdr.eth, eh, 2 * ETH_ALEN);
		hdr.eth.h_proto = htons(sizeof(encaps_hdr) + skb->len);
		memcpy(hdr.encap, encaps_hdr, sizeof(encaps_hdr));

		/* Insert the SNAP header */
		if (skb_headroom(skb) < sizeof(hdr)) {
			printk(KERN_ERR
			       "%s: Not enough headroom for 802.2 headers %d\n",
			       dev->name, skb_headroom(skb));
			goto drop;
		}
		eh = (struct ethhdr *) skb_push(skb, sizeof(hdr));
		memcpy(eh, &hdr, sizeof(hdr));
	}

	err = hermes_bap_pwrite(hw, USER_BAP, skb->data, skb->len,
				txfid, HERMES_802_3_OFFSET);
	if (err) {
		printk(KERN_ERR "%s: Error %d writing packet to BAP\n",
		       dev->name, err);
		goto busy;
	}

	/* Calculate Michael MIC */
	if (priv->encode_alg == IW_ENCODE_ALG_TKIP) {
		u8 mic_buf[MICHAEL_MIC_LEN + 1];
		u8 *mic;
		size_t offset;
		size_t len;

		if (skb->len % 2) {
			/* MIC start is on an odd boundary */
			mic_buf[0] = skb->data[skb->len - 1];
			mic = &mic_buf[1];
			offset = skb->len - 1;
			len = MICHAEL_MIC_LEN + 1;
		} else {
			mic = &mic_buf[0];
			offset = skb->len;
			len = MICHAEL_MIC_LEN;
		}

		orinoco_mic(priv->tx_tfm_mic,
			    priv->tkip_key[priv->tx_key].tx_mic,
			    eh->h_dest, eh->h_source, 0 /* priority */,
			    skb->data + ETH_HLEN, skb->len - ETH_HLEN, mic);

		/* Write the MIC */
		err = hermes_bap_pwrite(hw, USER_BAP, &mic_buf[0], len,
					txfid, HERMES_802_3_OFFSET + offset);
		if (err) {
			printk(KERN_ERR "%s: Error %d writing MIC to BAP\n",
			       dev->name, err);
			goto busy;
		}
	}

	/* Finally, we actually initiate the send */
	netif_stop_queue(dev);

	err = hermes_docmd_wait(hw, HERMES_CMD_TX | HERMES_CMD_RECL,
				txfid, NULL);
	if (err) {
		netif_start_queue(dev);
		if (net_ratelimit())
			printk(KERN_ERR "%s: Error %d transmitting packet\n",
				dev->name, err);
		goto busy;
	}

	dev->trans_start = jiffies;
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

static void __orinoco_ev_alloc(struct net_device *dev, hermes_t *hw)
{
	struct orinoco_private *priv = netdev_priv(dev);
	u16 fid = hermes_read_regn(hw, ALLOCFID);

	if (fid != priv->txfid) {
		if (fid != DUMMY_FID)
			printk(KERN_WARNING "%s: Allocate event on unexpected fid (%04X)\n",
			       dev->name, fid);
		return;
	}

	hermes_write_regn(hw, ALLOCFID, DUMMY_FID);
}

static void __orinoco_ev_tx(struct net_device *dev, hermes_t *hw)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct net_device_stats *stats = &priv->stats;

	stats->tx_packets++;

	netif_wake_queue(dev);

	hermes_write_regn(hw, TXCOMPLFID, DUMMY_FID);
}

static void __orinoco_ev_txexc(struct net_device *dev, hermes_t *hw)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct net_device_stats *stats = &priv->stats;
	u16 fid = hermes_read_regn(hw, TXCOMPLFID);
	u16 status;
	struct hermes_txexc_data hdr;
	int err = 0;

	if (fid == DUMMY_FID)
		return; /* Nothing's really happened */

	/* Read part of the frame header - we need status and addr1 */
	err = hermes_bap_pread(hw, IRQ_BAP, &hdr,
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

static void orinoco_tx_timeout(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct net_device_stats *stats = &priv->stats;
	struct hermes *hw = &priv->hw;

	printk(KERN_WARNING "%s: Tx timeout! "
	       "ALLOCFID=%04x, TXCOMPLFID=%04x, EVSTAT=%04x\n",
	       dev->name, hermes_read_regn(hw, ALLOCFID),
	       hermes_read_regn(hw, TXCOMPLFID), hermes_read_regn(hw, EVSTAT));

	stats->tx_errors++;

	schedule_work(&priv->reset_work);
}

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
	struct orinoco_private *priv = netdev_priv(dev);

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
	struct orinoco_private *priv = netdev_priv(dev);
	struct net_device_stats *stats = &priv->stats;
	hermes_t *hw = &priv->hw;

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
		err = hermes_bap_pread(hw, IRQ_BAP, skb_put(skb, datalen),
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

static void __orinoco_ev_rx(struct net_device *dev, hermes_t *hw)
{
	struct orinoco_private *priv = netdev_priv(dev);
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

	err = hermes_bap_pread(hw, IRQ_BAP, desc, sizeof(*desc),
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
	if (priv->iw_mode == IW_MODE_MONITOR) {
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
	skb = dev_alloc_skb(length+ETH_HLEN+2+1);
	if (!skb) {
		printk(KERN_WARNING "%s: Can't allocate skb for Rx\n",
		       dev->name);
		goto update_stats;
	}

	/* We'll prepend the header, so reserve space for it.  The worst
	   case is no decapsulation, when 802.3 header is prepended and
	   nothing is removed.  2 is for aligning the IP header.  */
	skb_reserve(skb, ETH_HLEN + 2);

	err = hermes_bap_pread(hw, IRQ_BAP, skb_put(skb, length),
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

static void orinoco_rx(struct net_device *dev,
		       struct hermes_rx_descriptor *desc,
		       struct sk_buff *skb)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct net_device_stats *stats = &priv->stats;
	u16 status, fc;
	int length;
	struct ethhdr *hdr;

	status = le16_to_cpu(desc->status);
	length = le16_to_cpu(desc->data_len);
	fc = le16_to_cpu(desc->frame_ctl);

	/* Calculate and check MIC */
	if (status & HERMES_RXSTAT_MIC) {
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

		orinoco_mic(priv->rx_tfm_mic,
			    priv->tkip_key[key_id].rx_mic,
			    desc->addr1,
			    src,
			    0, /* priority or QoS? */
			    skb->data,
			    skb->len,
			    &mic[0]);

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
	struct net_device *dev = (struct net_device *) data;
	struct orinoco_private *priv = netdev_priv(dev);
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
	} __attribute__ ((packed)) req;
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
	err = hermes_read_ltv(hw, USER_BAP,
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

	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CURRENTBSSID,
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

	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CURRENT_ASSOC_REQ_INFO,
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

	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CURRENT_ASSOC_RESP_INFO,
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

static void __orinoco_ev_info(struct net_device *dev, hermes_t *hw)
{
	struct orinoco_private *priv = netdev_priv(dev);
	u16 infofid;
	struct {
		__le16 len;
		__le16 type;
	} __attribute__ ((packed)) info;
	int len, type;
	int err;

	/* This is an answer to an INQUIRE command that we did earlier,
	 * or an information "event" generated by the card
	 * The controller return to us a pseudo frame containing
	 * the information in question - Jean II */
	infofid = hermes_read_regn(hw, INFOFID);

	/* Read the info frame header - don't try too hard */
	err = hermes_bap_pread(hw, IRQ_BAP, &info, sizeof(info),
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

		err = hermes_bap_pread(hw, IRQ_BAP, &tallies, len,
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

		if (priv->iw_mode == IW_MODE_MONITOR)
			break;

		if (len != sizeof(linkstatus)) {
			printk(KERN_WARNING "%s: Unexpected size for linkstatus frame (%d bytes)\n",
			       dev->name, len);
			break;
		}

		err = hermes_bap_pread(hw, IRQ_BAP, &linkstatus, len,
				       infofid, sizeof(info));
		if (err)
			break;
		newstatus = le16_to_cpu(linkstatus.linkstatus);

		/* Symbol firmware uses "out of range" to signal that
		 * the hostscan frame can be requested.  */
		if (newstatus == HERMES_LINKSTATUS_AP_OUT_OF_RANGE &&
		    priv->firmware_type == FIRMWARE_TYPE_SYMBOL &&
		    priv->has_hostscan && priv->scan_inprogress) {
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
		if (!priv->scan_inprogress && priv->bssid_fixed &&
		    priv->firmware_type == FIRMWARE_TYPE_INTERSIL) {
			schedule_work(&priv->join_work);
			break;
		}
		/* fall through */
	case HERMES_INQ_HOSTSCAN:
	case HERMES_INQ_HOSTSCAN_SYMBOL: {
		/* Result of a scanning. Contains information about
		 * cells in the vicinity - Jean II */
		union iwreq_data	wrqu;
		unsigned char *buf;

		/* Scan is no longer in progress */
		priv->scan_inprogress = 0;

		/* Sanity check */
		if (len > 4096) {
			printk(KERN_WARNING "%s: Scan results too large (%d bytes)\n",
			       dev->name, len);
			break;
		}

		/* Allocate buffer for results */
		buf = kmalloc(len, GFP_ATOMIC);
		if (buf == NULL)
			/* No memory, so can't printk()... */
			break;

		/* Read scan data */
		err = hermes_bap_pread(hw, IRQ_BAP, (void *) buf, len,
				       infofid, sizeof(info));
		if (err) {
			kfree(buf);
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

		if (orinoco_process_scan_results(priv, buf, len) == 0) {
			/* Send an empty event to user space.
			 * We don't send the received data on the event because
			 * it would require us to do complex transcoding, and
			 * we want to minimise the work done in the irq handler
			 * Use a request to extract the data - Jean II */
			wrqu.data.length = 0;
			wrqu.data.flags = 0;
			wireless_send_event(dev, SIOCGIWSCAN, &wrqu, NULL);
		}
		kfree(buf);
	}
	break;
	case HERMES_INQ_CHANNELINFO:
	{
		struct agere_ext_scan_info *bss;

		if (!priv->scan_inprogress) {
			printk(KERN_DEBUG "%s: Got chaninfo without scan, "
			       "len=%d\n", dev->name, len);
			break;
		}

		/* An empty result indicates that the scan is complete */
		if (len == 0) {
			union iwreq_data	wrqu;

			/* Scan is no longer in progress */
			priv->scan_inprogress = 0;

			wrqu.data.length = 0;
			wrqu.data.flags = 0;
			wireless_send_event(dev, SIOCGIWSCAN, &wrqu, NULL);
			break;
		}

		/* Sanity check */
		else if (len > sizeof(*bss)) {
			printk(KERN_WARNING
			       "%s: Ext scan results too large (%d bytes). "
			       "Truncating results to %zd bytes.\n",
			       dev->name, len, sizeof(*bss));
			len = sizeof(*bss);
		} else if (len < (offsetof(struct agere_ext_scan_info,
					   data) + 2)) {
			/* Drop this result now so we don't have to
			 * keep checking later */
			printk(KERN_WARNING
			       "%s: Ext scan results too short (%d bytes)\n",
			       dev->name, len);
			break;
		}

		bss = kmalloc(sizeof(*bss), GFP_ATOMIC);
		if (bss == NULL)
			break;

		/* Read scan data */
		err = hermes_bap_pread(hw, IRQ_BAP, (void *) bss, len,
				       infofid, sizeof(info));
		if (err) {
			kfree(bss);
			break;
		}

		orinoco_add_ext_scan_result(priv, bss);

		kfree(bss);
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

static void __orinoco_ev_infdrop(struct net_device *dev, hermes_t *hw)
{
	if (net_ratelimit())
		printk(KERN_DEBUG "%s: Information frame lost.\n", dev->name);
}

/********************************************************************/
/* Internal hardware control routines                               */
/********************************************************************/

int __orinoco_up(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct hermes *hw = &priv->hw;
	int err;

	netif_carrier_off(dev); /* just to make sure */

	err = __orinoco_program_rids(dev);
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
EXPORT_SYMBOL(__orinoco_up);

int __orinoco_down(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
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

	/* firmware will have to reassociate */
	netif_carrier_off(dev);
	priv->last_linkstatus = 0xffff;

	return 0;
}
EXPORT_SYMBOL(__orinoco_down);

static int orinoco_allocate_fid(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct hermes *hw = &priv->hw;
	int err;

	err = hermes_allocate(hw, priv->nicbuf_size, &priv->txfid);
	if (err == -EIO && priv->nicbuf_size > TX_NICBUF_SIZE_BUG) {
		/* Try workaround for old Symbol firmware bug */
		priv->nicbuf_size = TX_NICBUF_SIZE_BUG;
		err = hermes_allocate(hw, priv->nicbuf_size, &priv->txfid);

		printk(KERN_WARNING "%s: firmware ALLOC bug detected "
		       "(old Symbol firmware?). Work around %s\n",
		       dev->name, err ? "failed!" : "ok.");
	}

	return err;
}

int orinoco_reinit_firmware(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct hermes *hw = &priv->hw;
	int err;

	err = hermes_init(hw);
	if (priv->do_fw_download && !err) {
		err = orinoco_download(priv);
		if (err)
			priv->do_fw_download = 0;
	}
	if (!err)
		err = orinoco_allocate_fid(dev);

	return err;
}
EXPORT_SYMBOL(orinoco_reinit_firmware);

int __orinoco_program_rids(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err;
	struct hermes_idstring idbuf;

	/* Set the MAC address */
	err = hermes_write_ltv(hw, USER_BAP, HERMES_RID_CNFOWNMACADDR,
			       HERMES_BYTES_TO_RECLEN(ETH_ALEN), dev->dev_addr);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting MAC address\n",
		       dev->name, err);
		return err;
	}

	/* Set up the link mode */
	err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNFPORTTYPE,
				   priv->port_type);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting port type\n",
		       dev->name, err);
		return err;
	}
	/* Set the channel/frequency */
	if (priv->channel != 0 && priv->iw_mode != IW_MODE_INFRA) {
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFOWNCHANNEL,
					   priv->channel);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting channel %d\n",
			       dev->name, err, priv->channel);
			return err;
		}
	}

	if (priv->has_ibss) {
		u16 createibss;

		if ((strlen(priv->desired_essid) == 0) && (priv->createibss)) {
			printk(KERN_WARNING "%s: This firmware requires an "
			       "ESSID in IBSS-Ad-Hoc mode.\n", dev->name);
			/* With wvlan_cs, in this case, we would crash.
			 * hopefully, this driver will behave better...
			 * Jean II */
			createibss = 0;
		} else {
			createibss = priv->createibss;
		}

		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFCREATEIBSS,
					   createibss);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting CREATEIBSS\n",
			       dev->name, err);
			return err;
		}
	}

	/* Set the desired BSSID */
	err = __orinoco_hw_set_wap(priv);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting AP address\n",
		       dev->name, err);
		return err;
	}
	/* Set the desired ESSID */
	idbuf.len = cpu_to_le16(strlen(priv->desired_essid));
	memcpy(&idbuf.val, priv->desired_essid, sizeof(idbuf.val));
	/* WinXP wants partner to configure OWNSSID even in IBSS mode. (jimc) */
	err = hermes_write_ltv(hw, USER_BAP, HERMES_RID_CNFOWNSSID,
			HERMES_BYTES_TO_RECLEN(strlen(priv->desired_essid)+2),
			&idbuf);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting OWNSSID\n",
		       dev->name, err);
		return err;
	}
	err = hermes_write_ltv(hw, USER_BAP, HERMES_RID_CNFDESIREDSSID,
			HERMES_BYTES_TO_RECLEN(strlen(priv->desired_essid)+2),
			&idbuf);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting DESIREDSSID\n",
		       dev->name, err);
		return err;
	}

	/* Set the station name */
	idbuf.len = cpu_to_le16(strlen(priv->nick));
	memcpy(&idbuf.val, priv->nick, sizeof(idbuf.val));
	err = hermes_write_ltv(hw, USER_BAP, HERMES_RID_CNFOWNNAME,
			       HERMES_BYTES_TO_RECLEN(strlen(priv->nick)+2),
			       &idbuf);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting nickname\n",
		       dev->name, err);
		return err;
	}

	/* Set AP density */
	if (priv->has_sensitivity) {
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFSYSTEMSCALE,
					   priv->ap_density);
		if (err) {
			printk(KERN_WARNING "%s: Error %d setting SYSTEMSCALE. "
			       "Disabling sensitivity control\n",
			       dev->name, err);

			priv->has_sensitivity = 0;
		}
	}

	/* Set RTS threshold */
	err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNFRTSTHRESHOLD,
				   priv->rts_thresh);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting RTS threshold\n",
		       dev->name, err);
		return err;
	}

	/* Set fragmentation threshold or MWO robustness */
	if (priv->has_mwo)
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFMWOROBUST_AGERE,
					   priv->mwo_robust);
	else
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFFRAGMENTATIONTHRESHOLD,
					   priv->frag_thresh);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting fragmentation\n",
		       dev->name, err);
		return err;
	}

	/* Set bitrate */
	err = __orinoco_hw_set_bitrate(priv);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting bitrate\n",
		       dev->name, err);
		return err;
	}

	/* Set power management */
	if (priv->has_pm) {
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFPMENABLED,
					   priv->pm_on);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting up PM\n",
			       dev->name, err);
			return err;
		}

		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFMULTICASTRECEIVE,
					   priv->pm_mcast);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting up PM\n",
			       dev->name, err);
			return err;
		}
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFMAXSLEEPDURATION,
					   priv->pm_period);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting up PM\n",
			       dev->name, err);
			return err;
		}
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFPMHOLDOVERDURATION,
					   priv->pm_timeout);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting up PM\n",
			       dev->name, err);
			return err;
		}
	}

	/* Set preamble - only for Symbol so far... */
	if (priv->has_preamble) {
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFPREAMBLE_SYMBOL,
					   priv->preamble);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting preamble\n",
			       dev->name, err);
			return err;
		}
	}

	/* Set up encryption */
	if (priv->has_wep || priv->has_wpa) {
		err = __orinoco_hw_setup_enc(priv);
		if (err) {
			printk(KERN_ERR "%s: Error %d activating encryption\n",
			       dev->name, err);
			return err;
		}
	}

	if (priv->iw_mode == IW_MODE_MONITOR) {
		/* Enable monitor mode */
		dev->type = ARPHRD_IEEE80211;
		err = hermes_docmd_wait(hw, HERMES_CMD_TEST |
					    HERMES_TEST_MONITOR, 0, NULL);
	} else {
		/* Disable monitor mode */
		dev->type = ARPHRD_ETHER;
		err = hermes_docmd_wait(hw, HERMES_CMD_TEST |
					    HERMES_TEST_STOP, 0, NULL);
	}
	if (err)
		return err;

	/* Set promiscuity / multicast*/
	priv->promiscuous = 0;
	priv->mc_count = 0;

	/* FIXME: what about netif_tx_lock */
	__orinoco_set_multicast_list(dev);

	return 0;
}

/* FIXME: return int? */
static void
__orinoco_set_multicast_list(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = 0;
	int promisc, mc_count;

	/* The Hermes doesn't seem to have an allmulti mode, so we go
	 * into promiscuous mode and let the upper levels deal. */
	if ((dev->flags & IFF_PROMISC) || (dev->flags & IFF_ALLMULTI) ||
	    (dev->mc_count > MAX_MULTICAST(priv))) {
		promisc = 1;
		mc_count = 0;
	} else {
		promisc = 0;
		mc_count = dev->mc_count;
	}

	err = __orinoco_hw_set_multicast_list(priv, dev->mc_list, mc_count,
					      promisc);
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

	/* Scanning support: Cleanup of driver struct */
	orinoco_clear_scan_results(priv, 0);
	priv->scan_inprogress = 0;

	if (priv->hard_reset) {
		err = (*priv->hard_reset)(priv);
		if (err) {
			printk(KERN_ERR "%s: orinoco_reset: Error %d "
			       "performing hard reset\n", dev->name, err);
			goto disable;
		}
	}

	err = orinoco_reinit_firmware(dev);
	if (err) {
		printk(KERN_ERR "%s: orinoco_reset: Error %d re-initializing firmware\n",
		       dev->name, err);
		goto disable;
	}

	/* This has to be called from user context */
	spin_lock_irq(&priv->lock);

	priv->hw_unavailable--;

	/* priv->open or priv->hw_unavailable might have changed while
	 * we dropped the lock */
	if (priv->open && (!priv->hw_unavailable)) {
		err = __orinoco_up(dev);
		if (err) {
			printk(KERN_ERR "%s: orinoco_reset: Error %d reenabling card\n",
			       dev->name, err);
		} else
			dev->trans_start = jiffies;
	}

	spin_unlock_irq(&priv->lock);

	return;
 disable:
	hermes_set_irqmask(hw, 0);
	netif_device_detach(dev);
	printk(KERN_ERR "%s: Device has been disabled!\n", dev->name);
}

/********************************************************************/
/* Interrupt handler                                                */
/********************************************************************/

static void __orinoco_ev_tick(struct net_device *dev, hermes_t *hw)
{
	printk(KERN_DEBUG "%s: TICK\n", dev->name);
}

static void __orinoco_ev_wterr(struct net_device *dev, hermes_t *hw)
{
	/* This seems to happen a fair bit under load, but ignoring it
	   seems to work fine...*/
	printk(KERN_DEBUG "%s: MAC controller error (WTERR). Ignoring.\n",
	       dev->name);
}

irqreturn_t orinoco_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
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
	};

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
#define orinoco_register_pm_notifier(priv) do { } while(0)
#define orinoco_unregister_pm_notifier(priv) do { } while(0)
#endif

/********************************************************************/
/* Initialization                                                   */
/********************************************************************/

struct comp_id {
	u16 id, variant, major, minor;
} __attribute__ ((packed));

static inline fwtype_t determine_firmware_type(struct comp_id *nic_id)
{
	if (nic_id->id < 0x8000)
		return FIRMWARE_TYPE_AGERE;
	else if (nic_id->id == 0x8000 && nic_id->major == 0)
		return FIRMWARE_TYPE_SYMBOL;
	else
		return FIRMWARE_TYPE_INTERSIL;
}

/* Set priv->firmware type, determine firmware properties */
static int determine_firmware(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err;
	struct comp_id nic_id, sta_id;
	unsigned int firmver;
	char tmp[SYMBOL_MAX_VER_LEN+1] __attribute__((aligned(2)));

	/* Get the hardware version */
	err = HERMES_READ_RECORD(hw, USER_BAP, HERMES_RID_NICID, &nic_id);
	if (err) {
		printk(KERN_ERR "%s: Cannot read hardware identity: error %d\n",
		       dev->name, err);
		return err;
	}

	le16_to_cpus(&nic_id.id);
	le16_to_cpus(&nic_id.variant);
	le16_to_cpus(&nic_id.major);
	le16_to_cpus(&nic_id.minor);
	printk(KERN_DEBUG "%s: Hardware identity %04x:%04x:%04x:%04x\n",
	       dev->name, nic_id.id, nic_id.variant,
	       nic_id.major, nic_id.minor);

	priv->firmware_type = determine_firmware_type(&nic_id);

	/* Get the firmware version */
	err = HERMES_READ_RECORD(hw, USER_BAP, HERMES_RID_STAID, &sta_id);
	if (err) {
		printk(KERN_ERR "%s: Cannot read station identity: error %d\n",
		       dev->name, err);
		return err;
	}

	le16_to_cpus(&sta_id.id);
	le16_to_cpus(&sta_id.variant);
	le16_to_cpus(&sta_id.major);
	le16_to_cpus(&sta_id.minor);
	printk(KERN_DEBUG "%s: Station identity  %04x:%04x:%04x:%04x\n",
	       dev->name, sta_id.id, sta_id.variant,
	       sta_id.major, sta_id.minor);

	switch (sta_id.id) {
	case 0x15:
		printk(KERN_ERR "%s: Primary firmware is active\n",
		       dev->name);
		return -ENODEV;
	case 0x14b:
		printk(KERN_ERR "%s: Tertiary firmware is active\n",
		       dev->name);
		return -ENODEV;
	case 0x1f:	/* Intersil, Agere, Symbol Spectrum24 */
	case 0x21:	/* Symbol Spectrum24 Trilogy */
		break;
	default:
		printk(KERN_NOTICE "%s: Unknown station ID, please report\n",
		       dev->name);
		break;
	}

	/* Default capabilities */
	priv->has_sensitivity = 1;
	priv->has_mwo = 0;
	priv->has_preamble = 0;
	priv->has_port3 = 1;
	priv->has_ibss = 1;
	priv->has_wep = 0;
	priv->has_big_wep = 0;
	priv->has_alt_txcntl = 0;
	priv->has_ext_scan = 0;
	priv->has_wpa = 0;
	priv->do_fw_download = 0;

	/* Determine capabilities from the firmware version */
	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE:
		/* Lucent Wavelan IEEE, Lucent Orinoco, Cabletron RoamAbout,
		   ELSA, Melco, HP, IBM, Dell 1150, Compaq 110/210 */
		snprintf(priv->fw_name, sizeof(priv->fw_name) - 1,
			 "Lucent/Agere %d.%02d", sta_id.major, sta_id.minor);

		firmver = ((unsigned long)sta_id.major << 16) | sta_id.minor;

		priv->has_ibss = (firmver >= 0x60006);
		priv->has_wep = (firmver >= 0x40020);
		priv->has_big_wep = 1; /* FIXME: this is wrong - how do we tell
					  Gold cards from the others? */
		priv->has_mwo = (firmver >= 0x60000);
		priv->has_pm = (firmver >= 0x40020); /* Don't work in 7.52 ? */
		priv->ibss_port = 1;
		priv->has_hostscan = (firmver >= 0x8000a);
		priv->do_fw_download = 1;
		priv->broken_monitor = (firmver >= 0x80000);
		priv->has_alt_txcntl = (firmver >= 0x90000); /* All 9.x ? */
		priv->has_ext_scan = (firmver >= 0x90000); /* All 9.x ? */
		priv->has_wpa = (firmver >= 0x9002a);
		/* Tested with Agere firmware :
		 *	1.16 ; 4.08 ; 4.52 ; 6.04 ; 6.16 ; 7.28 => Jean II
		 * Tested CableTron firmware : 4.32 => Anton */
		break;
	case FIRMWARE_TYPE_SYMBOL:
		/* Symbol , 3Com AirConnect, Intel, Ericsson WLAN */
		/* Intel MAC : 00:02:B3:* */
		/* 3Com MAC : 00:50:DA:* */
		memset(tmp, 0, sizeof(tmp));
		/* Get the Symbol firmware version */
		err = hermes_read_ltv(hw, USER_BAP,
				      HERMES_RID_SECONDARYVERSION_SYMBOL,
				      SYMBOL_MAX_VER_LEN, NULL, &tmp);
		if (err) {
			printk(KERN_WARNING
			       "%s: Error %d reading Symbol firmware info. "
			       "Wildly guessing capabilities...\n",
			       dev->name, err);
			firmver = 0;
			tmp[0] = '\0';
		} else {
			/* The firmware revision is a string, the format is
			 * something like : "V2.20-01".
			 * Quick and dirty parsing... - Jean II
			 */
			firmver = ((tmp[1] - '0') << 16)
				| ((tmp[3] - '0') << 12)
				| ((tmp[4] - '0') << 8)
				| ((tmp[6] - '0') << 4)
				| (tmp[7] - '0');

			tmp[SYMBOL_MAX_VER_LEN] = '\0';
		}

		snprintf(priv->fw_name, sizeof(priv->fw_name) - 1,
			 "Symbol %s", tmp);

		priv->has_ibss = (firmver >= 0x20000);
		priv->has_wep = (firmver >= 0x15012);
		priv->has_big_wep = (firmver >= 0x20000);
		priv->has_pm = (firmver >= 0x20000 && firmver < 0x22000) ||
			       (firmver >= 0x29000 && firmver < 0x30000) ||
			       firmver >= 0x31000;
		priv->has_preamble = (firmver >= 0x20000);
		priv->ibss_port = 4;

		/* Symbol firmware is found on various cards, but
		 * there has been no attempt to check firmware
		 * download on non-spectrum_cs based cards.
		 *
		 * Given that the Agere firmware download works
		 * differently, we should avoid doing a firmware
		 * download with the Symbol algorithm on non-spectrum
		 * cards.
		 *
		 * For now we can identify a spectrum_cs based card
		 * because it has a firmware reset function.
		 */
		priv->do_fw_download = (priv->stop_fw != NULL);

		priv->broken_disableport = (firmver == 0x25013) ||
				(firmver >= 0x30000 && firmver <= 0x31000);
		priv->has_hostscan = (firmver >= 0x31001) ||
				     (firmver >= 0x29057 && firmver < 0x30000);
		/* Tested with Intel firmware : 0x20015 => Jean II */
		/* Tested with 3Com firmware : 0x15012 & 0x22001 => Jean II */
		break;
	case FIRMWARE_TYPE_INTERSIL:
		/* D-Link, Linksys, Adtron, ZoomAir, and many others...
		 * Samsung, Compaq 100/200 and Proxim are slightly
		 * different and less well tested */
		/* D-Link MAC : 00:40:05:* */
		/* Addtron MAC : 00:90:D1:* */
		snprintf(priv->fw_name, sizeof(priv->fw_name) - 1,
			 "Intersil %d.%d.%d", sta_id.major, sta_id.minor,
			 sta_id.variant);

		firmver = ((unsigned long)sta_id.major << 16) |
			((unsigned long)sta_id.minor << 8) | sta_id.variant;

		priv->has_ibss = (firmver >= 0x000700); /* FIXME */
		priv->has_big_wep = priv->has_wep = (firmver >= 0x000800);
		priv->has_pm = (firmver >= 0x000700);
		priv->has_hostscan = (firmver >= 0x010301);

		if (firmver >= 0x000800)
			priv->ibss_port = 0;
		else {
			printk(KERN_NOTICE "%s: Intersil firmware earlier "
			       "than v0.8.x - several features not supported\n",
			       dev->name);
			priv->ibss_port = 1;
		}
		break;
	}
	printk(KERN_DEBUG "%s: Firmware determined as %s\n", dev->name,
	       priv->fw_name);

	return 0;
}

static int orinoco_init(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err = 0;
	struct hermes_idstring nickbuf;
	u16 reclen;
	int len;

	/* No need to lock, the hw_unavailable flag is already set in
	 * alloc_orinocodev() */
	priv->nicbuf_size = IEEE80211_MAX_FRAME_LEN + ETH_HLEN;

	/* Initialize the firmware */
	err = hermes_init(hw);
	if (err != 0) {
		printk(KERN_ERR "%s: failed to initialize firmware (err = %d)\n",
		       dev->name, err);
		goto out;
	}

	err = determine_firmware(dev);
	if (err != 0) {
		printk(KERN_ERR "%s: Incompatible firmware, aborting\n",
		       dev->name);
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
		err = determine_firmware(dev);
		if (err != 0) {
			printk(KERN_ERR "%s: Incompatible firmware, aborting\n",
			       dev->name);
			goto out;
		}
	}

	if (priv->has_port3)
		printk(KERN_DEBUG "%s: Ad-hoc demo mode supported\n",
		       dev->name);
	if (priv->has_ibss)
		printk(KERN_DEBUG "%s: IEEE standard IBSS ad-hoc mode supported\n",
		       dev->name);
	if (priv->has_wep) {
		printk(KERN_DEBUG "%s: WEP supported, %s-bit key\n", dev->name,
		       priv->has_big_wep ? "104" : "40");
	}
	if (priv->has_wpa) {
		printk(KERN_DEBUG "%s: WPA-PSK supported\n", dev->name);
		if (orinoco_mic_init(priv)) {
			printk(KERN_ERR "%s: Failed to setup MIC crypto "
			       "algorithm. Disabling WPA support\n", dev->name);
			priv->has_wpa = 0;
		}
	}

	/* Now we have the firmware capabilities, allocate appropiate
	 * sized scan buffers */
	if (orinoco_bss_data_allocate(priv))
		goto out;
	orinoco_bss_data_init(priv);

	/* Get the MAC address */
	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CNFOWNMACADDR,
			      ETH_ALEN, NULL, dev->dev_addr);
	if (err) {
		printk(KERN_WARNING "%s: failed to read MAC address!\n",
		       dev->name);
		goto out;
	}

	printk(KERN_DEBUG "%s: MAC address %pM\n",
	       dev->name, dev->dev_addr);

	/* Get the station name */
	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CNFOWNNAME,
			      sizeof(nickbuf), &reclen, &nickbuf);
	if (err) {
		printk(KERN_ERR "%s: failed to read station name\n",
		       dev->name);
		goto out;
	}
	if (nickbuf.len)
		len = min(IW_ESSID_MAX_SIZE, (int)le16_to_cpu(nickbuf.len));
	else
		len = min(IW_ESSID_MAX_SIZE, 2 * reclen);
	memcpy(priv->nick, &nickbuf.val, len);
	priv->nick[len] = '\0';

	printk(KERN_DEBUG "%s: Station name \"%s\"\n", dev->name, priv->nick);

	err = orinoco_allocate_fid(dev);
	if (err) {
		printk(KERN_ERR "%s: failed to allocate NIC buffer!\n",
		       dev->name);
		goto out;
	}

	/* Get allowed channels */
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CHANNELLIST,
				  &priv->channel_mask);
	if (err) {
		printk(KERN_ERR "%s: failed to read channel list!\n",
		       dev->name);
		goto out;
	}

	/* Get initial AP density */
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNFSYSTEMSCALE,
				  &priv->ap_density);
	if (err || priv->ap_density < 1 || priv->ap_density > 3)
		priv->has_sensitivity = 0;

	/* Get initial RTS threshold */
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNFRTSTHRESHOLD,
				  &priv->rts_thresh);
	if (err) {
		printk(KERN_ERR "%s: failed to read RTS threshold!\n",
		       dev->name);
		goto out;
	}

	/* Get initial fragmentation settings */
	if (priv->has_mwo)
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFMWOROBUST_AGERE,
					  &priv->mwo_robust);
	else
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFFRAGMENTATIONTHRESHOLD,
					  &priv->frag_thresh);
	if (err) {
		printk(KERN_ERR "%s: failed to read fragmentation settings!\n",
		       dev->name);
		goto out;
	}

	/* Power management setup */
	if (priv->has_pm) {
		priv->pm_on = 0;
		priv->pm_mcast = 1;
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFMAXSLEEPDURATION,
					  &priv->pm_period);
		if (err) {
			printk(KERN_ERR "%s: failed to read power management period!\n",
			       dev->name);
			goto out;
		}
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFPMHOLDOVERDURATION,
					  &priv->pm_timeout);
		if (err) {
			printk(KERN_ERR "%s: failed to read power management timeout!\n",
			       dev->name);
			goto out;
		}
	}

	/* Preamble setup */
	if (priv->has_preamble) {
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFPREAMBLE_SYMBOL,
					  &priv->preamble);
		if (err)
			goto out;
	}

	/* Set up the default configuration */
	priv->iw_mode = IW_MODE_INFRA;
	/* By default use IEEE/IBSS ad-hoc mode if we have it */
	priv->prefer_port3 = priv->has_port3 && (!priv->has_ibss);
	set_port_type(priv);
	priv->channel = 0; /* use firmware default */

	priv->promiscuous = 0;
	priv->encode_alg = IW_ENCODE_ALG_NONE;
	priv->tx_key = 0;
	priv->wpa_enabled = 0;
	priv->tkip_cm_active = 0;
	priv->key_mgmt = 0;
	priv->wpa_ie_len = 0;
	priv->wpa_ie = NULL;

	/* Make the hardware available, as long as it hasn't been
	 * removed elsewhere (e.g. by PCMCIA hot unplug) */
	spin_lock_irq(&priv->lock);
	priv->hw_unavailable--;
	spin_unlock_irq(&priv->lock);

	printk(KERN_DEBUG "%s: ready\n", dev->name);

 out:
	return err;
}

static const struct net_device_ops orinoco_netdev_ops = {
	.ndo_init		= orinoco_init,
	.ndo_open		= orinoco_open,
	.ndo_stop		= orinoco_stop,
	.ndo_start_xmit		= orinoco_xmit,
	.ndo_set_multicast_list	= orinoco_set_multicast_list,
	.ndo_change_mtu		= orinoco_change_mtu,
	.ndo_tx_timeout		= orinoco_tx_timeout,
	.ndo_get_stats		= orinoco_get_stats,
};

struct net_device
*alloc_orinocodev(int sizeof_card,
		  struct device *device,
		  int (*hard_reset)(struct orinoco_private *),
		  int (*stop_fw)(struct orinoco_private *, int))
{
	struct net_device *dev;
	struct orinoco_private *priv;

	dev = alloc_etherdev(sizeof(struct orinoco_private) + sizeof_card);
	if (!dev)
		return NULL;
	priv = netdev_priv(dev);
	priv->ndev = dev;
	if (sizeof_card)
		priv->card = (void *)((unsigned long)priv
				      + sizeof(struct orinoco_private));
	else
		priv->card = NULL;
	priv->dev = device;

	/* Setup / override net_device fields */
	dev->netdev_ops = &orinoco_netdev_ops;
	dev->watchdog_timeo = HZ; /* 1 second timeout */
	dev->ethtool_ops = &orinoco_ethtool_ops;
	dev->wireless_handlers = &orinoco_handler_def;
#ifdef WIRELESS_SPY
	priv->wireless_data.spy_data = &priv->spy_data;
	dev->wireless_data = &priv->wireless_data;
#endif
	/* we use the default eth_mac_addr for setting the MAC addr */

	/* Reserve space in skb for the SNAP header */
	dev->hard_header_len += ENCAPS_OVERHEAD;

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
		     (unsigned long) dev);

	netif_carrier_off(dev);
	priv->last_linkstatus = 0xffff;

#if defined(CONFIG_HERMES_CACHE_FW_ON_INIT) || defined(CONFIG_PM_SLEEP)
	priv->cached_pri_fw = NULL;
	priv->cached_fw = NULL;
#endif

	/* Register PM notifiers */
	orinoco_register_pm_notifier(priv);

	return dev;
}
EXPORT_SYMBOL(alloc_orinocodev);

void free_orinocodev(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct orinoco_rx_data *rx_data, *temp;

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

	orinoco_unregister_pm_notifier(priv);
	orinoco_uncache_fw(priv);

	priv->wpa_ie_len = 0;
	kfree(priv->wpa_ie);
	orinoco_mic_free(priv);
	orinoco_bss_data_free(priv);
	free_netdev(dev);
}
EXPORT_SYMBOL(free_orinocodev);

static void orinoco_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	struct orinoco_private *priv = netdev_priv(dev);

	strncpy(info->driver, DRIVER_NAME, sizeof(info->driver) - 1);
	strncpy(info->version, DRIVER_VERSION, sizeof(info->version) - 1);
	strncpy(info->fw_version, priv->fw_name, sizeof(info->fw_version) - 1);
	if (dev->dev.parent)
		strncpy(info->bus_info, dev_name(dev->dev.parent),
			sizeof(info->bus_info) - 1);
	else
		snprintf(info->bus_info, sizeof(info->bus_info) - 1,
			 "PCMCIA %p", priv->hw.iobase);
}

static const struct ethtool_ops orinoco_ethtool_ops = {
	.get_drvinfo = orinoco_get_drvinfo,
	.get_link = ethtool_op_get_link,
};

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
