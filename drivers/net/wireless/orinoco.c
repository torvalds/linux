/* orinoco.c - (formerly known as dldwd_cs.c and orinoco_cs.c)
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <net/ieee80211.h>

#include "hermes_rid.h"
#include "orinoco.h"

/********************************************************************/
/* Module information                                               */
/********************************************************************/

MODULE_AUTHOR("Pavel Roskin <proski@gnu.org> & David Gibson <hermes@gibson.dropbear.id.au>");
MODULE_DESCRIPTION("Driver for Lucent Orinoco, Prism II based and similar wireless cards");
MODULE_LICENSE("Dual MPL/GPL");

/* Level of debugging. Used in the macros in orinoco.h */
#ifdef ORINOCO_DEBUG
int orinoco_debug = ORINOCO_DEBUG;
module_param(orinoco_debug, int, 0644);
MODULE_PARM_DESC(orinoco_debug, "Debug level");
EXPORT_SYMBOL(orinoco_debug);
#endif

static int suppress_linkstatus; /* = 0 */
module_param(suppress_linkstatus, bool, 0644);
MODULE_PARM_DESC(suppress_linkstatus, "Don't log link status changes");
static int ignore_disconnect; /* = 0 */
module_param(ignore_disconnect, int, 0644);
MODULE_PARM_DESC(ignore_disconnect, "Don't report lost link to the network layer");

static int force_monitor; /* = 0 */
module_param(force_monitor, int, 0644);
MODULE_PARM_DESC(force_monitor, "Allow monitor mode for all firmware versions");

/********************************************************************/
/* Compile time configuration and compatibility stuff               */
/********************************************************************/

/* We do this this way to avoid ifdefs in the actual code */
#ifdef WIRELESS_SPY
#define SPY_NUMBER(priv)	(priv->spy_data.spy_number)
#else
#define SPY_NUMBER(priv)	0
#endif /* WIRELESS_SPY */

/********************************************************************/
/* Internal constants                                               */
/********************************************************************/

/* 802.2 LLC/SNAP header used for Ethernet encapsulation over 802.11 */
static const u8 encaps_hdr[] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};
#define ENCAPS_OVERHEAD		(sizeof(encaps_hdr) + 2)

#define ORINOCO_MIN_MTU		256
#define ORINOCO_MAX_MTU		(IEEE80211_DATA_LEN - ENCAPS_OVERHEAD)

#define SYMBOL_MAX_VER_LEN	(14)
#define USER_BAP		0
#define IRQ_BAP			1
#define MAX_IRQLOOPS_PER_IRQ	10
#define MAX_IRQLOOPS_PER_JIFFY	(20000/HZ) /* Based on a guestimate of
					    * how many events the
					    * device could
					    * legitimately generate */
#define SMALL_KEY_SIZE		5
#define LARGE_KEY_SIZE		13
#define TX_NICBUF_SIZE_BUG	1585		/* Bug in Symbol firmware */

#define DUMMY_FID		0xFFFF

/*#define MAX_MULTICAST(priv)	(priv->firmware_type == FIRMWARE_TYPE_AGERE ? \
  HERMES_MAX_MULTICAST : 0)*/
#define MAX_MULTICAST(priv)	(HERMES_MAX_MULTICAST)

#define ORINOCO_INTEN	 	(HERMES_EV_RX | HERMES_EV_ALLOC \
				 | HERMES_EV_TX | HERMES_EV_TXEXC \
				 | HERMES_EV_WTERR | HERMES_EV_INFO \
				 | HERMES_EV_INFDROP )

#define MAX_RID_LEN 1024

static const struct iw_handler_def orinoco_handler_def;
static struct ethtool_ops orinoco_ethtool_ops;

/********************************************************************/
/* Data tables                                                      */
/********************************************************************/

/* The frequency of each channel in MHz */
static const long channel_frequency[] = {
	2412, 2417, 2422, 2427, 2432, 2437, 2442,
	2447, 2452, 2457, 2462, 2467, 2472, 2484
};
#define NUM_CHANNELS ARRAY_SIZE(channel_frequency)

/* This tables gives the actual meanings of the bitrate IDs returned
 * by the firmware. */
static struct {
	int bitrate; /* in 100s of kilobits */
	int automatic;
	u16 agere_txratectrl;
	u16 intersil_txratectrl;
} bitrate_table[] = {
	{110, 1,  3, 15}, /* Entry 0 is the default */
	{10,  0,  1,  1},
	{10,  1,  1,  1},
	{20,  0,  2,  2},
	{20,  1,  6,  3},
	{55,  0,  4,  4},
	{55,  1,  7,  7},
	{110, 0,  5,  8},
};
#define BITRATE_TABLE_SIZE ARRAY_SIZE(bitrate_table)

/********************************************************************/
/* Data types                                                       */
/********************************************************************/

/* Used in Event handling.
 * We avoid nested structures as they break on ARM -- Moustafa */
struct hermes_tx_descriptor_802_11 {
	/* hermes_tx_descriptor */
	__le16 status;
	__le16 reserved1;
	__le16 reserved2;
	__le32 sw_support;
	u8 retry_count;
	u8 tx_rate;
	__le16 tx_control;

	/* ieee80211_hdr */
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 addr4[ETH_ALEN];

	__le16 data_len;

	/* ethhdr */
	u8 h_dest[ETH_ALEN];	/* destination eth addr */
	u8 h_source[ETH_ALEN];	/* source ether addr    */
	__be16 h_proto;		/* packet type ID field */

	/* p8022_hdr */
	u8 dsap;
	u8 ssap;
	u8 ctrl;
	u8 oui[3];

	__be16 ethertype;
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

/********************************************************************/
/* Function prototypes                                              */
/********************************************************************/

static int __orinoco_program_rids(struct net_device *dev);
static void __orinoco_set_multicast_list(struct net_device *dev);

/********************************************************************/
/* Internal helper functions                                        */
/********************************************************************/

static inline void set_port_type(struct orinoco_private *priv)
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

	if (! err)
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

static struct iw_statistics *orinoco_get_wireless_stats(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	struct iw_statistics *wstats = &priv->wstats;
	int err;
	unsigned long flags;

	if (! netif_device_present(dev)) {
		printk(KERN_WARNING "%s: get_wireless_stats() called while device not present\n",
		       dev->name);
		return NULL; /* FIXME: Can we do better than this? */
	}

	/* If busy, return the old stats.  Returning NULL may cause
	 * the interface to disappear from /proc/net/wireless */
	if (orinoco_lock(priv, &flags) != 0)
		return wstats;

	/* We can't really wait for the tallies inquiry command to
	 * complete, so we just use the previous results and trigger
	 * a new tallies inquiry command for next time - Jean II */
	/* FIXME: Really we should wait for the inquiry to come back -
	 * as it is the stats we give don't make a whole lot of sense.
	 * Unfortunately, it's not clear how to do that within the
	 * wireless extensions framework: I think we're in user
	 * context, but a lock seems to be held by the time we get in
	 * here so we're not safe to sleep here. */
	hermes_inquire(hw, HERMES_INQ_TALLIES);

	if (priv->iw_mode == IW_MODE_ADHOC) {
		memset(&wstats->qual, 0, sizeof(wstats->qual));
		/* If a spy address is defined, we report stats of the
		 * first spy address - Jean II */
		if (SPY_NUMBER(priv)) {
			wstats->qual.qual = priv->spy_data.spy_stat[0].qual;
			wstats->qual.level = priv->spy_data.spy_stat[0].level;
			wstats->qual.noise = priv->spy_data.spy_stat[0].noise;
			wstats->qual.updated = priv->spy_data.spy_stat[0].updated;
		}
	} else {
		struct {
			__le16 qual, signal, noise, unused;
		} __attribute__ ((packed)) cq;

		err = HERMES_READ_RECORD(hw, USER_BAP,
					 HERMES_RID_COMMSQUALITY, &cq);

		if (!err) {
			wstats->qual.qual = (int)le16_to_cpu(cq.qual);
			wstats->qual.level = (int)le16_to_cpu(cq.signal) - 0x95;
			wstats->qual.noise = (int)le16_to_cpu(cq.noise) - 0x95;
			wstats->qual.updated = 7;
		}
	}

	orinoco_unlock(priv, &flags);
	return wstats;
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

	if ( (new_mtu < ORINOCO_MIN_MTU) || (new_mtu > ORINOCO_MAX_MTU) )
		return -EINVAL;

	if ( (new_mtu + ENCAPS_OVERHEAD + IEEE80211_HLEN) >
	     (priv->nicbuf_size - ETH_HLEN) )
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
	char *p;
	struct ethhdr *eh;
	int len, data_len, data_off;
	struct hermes_tx_descriptor desc;
	unsigned long flags;

	if (! netif_running(dev)) {
		printk(KERN_ERR "%s: Tx on stopped device!\n",
		       dev->name);
		return 1;
	}
	
	if (netif_queue_stopped(dev)) {
		printk(KERN_DEBUG "%s: Tx while transmitter busy!\n", 
		       dev->name);
		return 1;
	}
	
	if (orinoco_lock(priv, &flags) != 0) {
		printk(KERN_ERR "%s: orinoco_xmit() called while hw_unavailable\n",
		       dev->name);
		return 1;
	}

	if (! netif_carrier_ok(dev) || (priv->iw_mode == IW_MODE_MONITOR)) {
		/* Oops, the firmware hasn't established a connection,
                   silently drop the packet (this seems to be the
                   safest approach). */
		stats->tx_errors++;
		orinoco_unlock(priv, &flags);
		dev_kfree_skb(skb);
		return 0;
	}

	/* Length of the packet body */
	/* FIXME: what if the skb is smaller than this? */
	len = max_t(int, ALIGN(skb->len, 2), ETH_ZLEN);
	skb = skb_padto(skb, len);
	if (skb == NULL)
		goto fail;
	len -= ETH_HLEN;

	eh = (struct ethhdr *)skb->data;

	memset(&desc, 0, sizeof(desc));
 	desc.tx_control = cpu_to_le16(HERMES_TXCTRL_TX_OK | HERMES_TXCTRL_TX_EX);
	err = hermes_bap_pwrite(hw, USER_BAP, &desc, sizeof(desc), txfid, 0);
	if (err) {
		if (net_ratelimit())
			printk(KERN_ERR "%s: Error %d writing Tx descriptor "
			       "to BAP\n", dev->name, err);
		stats->tx_errors++;
		goto fail;
	}

	/* Clear the 802.11 header and data length fields - some
	 * firmwares (e.g. Lucent/Agere 8.xx) appear to get confused
	 * if this isn't done. */
	hermes_clear_words(hw, HERMES_DATA0,
			   HERMES_802_3_OFFSET - HERMES_802_11_OFFSET);

	/* Encapsulate Ethernet-II frames */
	if (ntohs(eh->h_proto) > ETH_DATA_LEN) { /* Ethernet-II frame */
		struct header_struct hdr;
		data_len = len;
		data_off = HERMES_802_3_OFFSET + sizeof(hdr);
		p = skb->data + ETH_HLEN;

		/* 802.3 header */
		memcpy(hdr.dest, eh->h_dest, ETH_ALEN);
		memcpy(hdr.src, eh->h_source, ETH_ALEN);
		hdr.len = htons(data_len + ENCAPS_OVERHEAD);
		
		/* 802.2 header */
		memcpy(&hdr.dsap, &encaps_hdr, sizeof(encaps_hdr));
			
		hdr.ethertype = eh->h_proto;
		err  = hermes_bap_pwrite(hw, USER_BAP, &hdr, sizeof(hdr),
					 txfid, HERMES_802_3_OFFSET);
		if (err) {
			if (net_ratelimit())
				printk(KERN_ERR "%s: Error %d writing packet "
				       "header to BAP\n", dev->name, err);
			stats->tx_errors++;
			goto fail;
		}
		/* Actual xfer length - allow for padding */
		len = ALIGN(data_len, 2);
		if (len < ETH_ZLEN - ETH_HLEN)
			len = ETH_ZLEN - ETH_HLEN;
	} else { /* IEEE 802.3 frame */
		data_len = len + ETH_HLEN;
		data_off = HERMES_802_3_OFFSET;
		p = skb->data;
		/* Actual xfer length - round up for odd length packets */
		len = ALIGN(data_len, 2);
		if (len < ETH_ZLEN)
			len = ETH_ZLEN;
	}

	err = hermes_bap_pwrite_pad(hw, USER_BAP, p, data_len, len,
				txfid, data_off);
	if (err) {
		printk(KERN_ERR "%s: Error %d writing packet to BAP\n",
		       dev->name, err);
		stats->tx_errors++;
		goto fail;
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
		stats->tx_errors++;
		goto fail;
	}

	dev->trans_start = jiffies;
	stats->tx_bytes += data_off + data_len;

	orinoco_unlock(priv, &flags);

	dev_kfree_skb(skb);

	return 0;
 fail:

	orinoco_unlock(priv, &flags);
	return err;
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
	struct hermes_tx_descriptor_802_11 hdr;
	int err = 0;

	if (fid == DUMMY_FID)
		return; /* Nothing's really happened */

	/* Read part of the frame header - we need status and addr1 */
	err = hermes_bap_pread(hw, IRQ_BAP, &hdr,
			       offsetof(struct hermes_tx_descriptor_802_11,
					addr2),
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
	status = le16_to_cpu(hdr.status);
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
		&& ( (hdr[5] == 0x00) || (hdr[5] == 0xf8) );
}

static inline void orinoco_spy_gather(struct net_device *dev, u_char *mac,
				      int level, int noise)
{
	struct iw_quality wstats;
	wstats.level = level - 0x95;
	wstats.noise = noise - 0x95;
	wstats.qual = (level > noise) ? (level - noise) : 0;
	wstats.updated = 7;
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
		orinoco_spy_gather(dev, skb->mac.raw + ETH_ALEN,
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
	if (datalen > IEEE80211_DATA_LEN + 12) {
		printk(KERN_DEBUG "%s: oversized monitor frame, "
		       "data length = %d\n", dev->name, datalen);
		err = -EIO;
		stats->rx_length_errors++;
		goto update_stats;
	}

	skb = dev_alloc_skb(hdrlen + datalen);
	if (!skb) {
		printk(KERN_WARNING "%s: Cannot allocate skb for monitor frame\n",
		       dev->name);
		err = -ENOMEM;
		goto drop;
	}

	/* Copy the 802.11 header to the skb */
	memcpy(skb_put(skb, hdrlen), &(desc->frame_ctl), hdrlen);
	skb->mac.raw = skb->data;

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
	skb->protocol = __constant_htons(ETH_P_802_2);
	
	dev->last_rx = jiffies;
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
	u16 rxfid, status, fc;
	int length;
	struct hermes_rx_descriptor desc;
	struct ethhdr *hdr;
	int err;

	rxfid = hermes_read_regn(hw, RXFID);

	err = hermes_bap_pread(hw, IRQ_BAP, &desc, sizeof(desc),
			       rxfid, 0);
	if (err) {
		printk(KERN_ERR "%s: error %d reading Rx descriptor. "
		       "Frame dropped.\n", dev->name, err);
		goto update_stats;
	}

	status = le16_to_cpu(desc.status);

	if (status & HERMES_RXSTAT_BADCRC) {
		DEBUG(1, "%s: Bad CRC on Rx. Frame dropped.\n",
		      dev->name);
		stats->rx_crc_errors++;
		goto update_stats;
	}

	/* Handle frames in monitor mode */
	if (priv->iw_mode == IW_MODE_MONITOR) {
		orinoco_rx_monitor(dev, rxfid, &desc);
		return;
	}

	if (status & HERMES_RXSTAT_UNDECRYPTABLE) {
		DEBUG(1, "%s: Undecryptable frame on Rx. Frame dropped.\n",
		      dev->name);
		wstats->discard.code++;
		goto update_stats;
	}

	length = le16_to_cpu(desc.data_len);
	fc = le16_to_cpu(desc.frame_ctl);

	/* Sanity checks */
	if (length < 3) { /* No for even an 802.2 LLC header */
		/* At least on Symbol firmware with PCF we get quite a
                   lot of these legitimately - Poll frames with no
                   data. */
		return;
	}
	if (length > IEEE80211_DATA_LEN) {
		printk(KERN_WARNING "%s: Oversized frame received (%d bytes)\n",
		       dev->name, length);
		stats->rx_length_errors++;
		goto update_stats;
	}

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
		hdr = (struct ethhdr *)skb_push(skb, ETH_HLEN - ENCAPS_OVERHEAD);
	} else {
		/* 802.3 frame - prepend 802.3 header as is */
		hdr = (struct ethhdr *)skb_push(skb, ETH_HLEN);
		hdr->h_proto = htons(length);
	}
	memcpy(hdr->h_dest, desc.addr1, ETH_ALEN);
	if (fc & IEEE80211_FCTL_FROMDS)
		memcpy(hdr->h_source, desc.addr3, ETH_ALEN);
	else
		memcpy(hdr->h_source, desc.addr2, ETH_ALEN);

	dev->last_rx = jiffies;
	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_NONE;
	if (fc & IEEE80211_FCTL_TODS)
		skb->pkt_type = PACKET_OTHERHOST;
	
	/* Process the wireless stats if needed */
	orinoco_stat_gather(dev, skb, &desc);

	/* Pass the packet to the networking stack */
	netif_rx(skb);
	stats->rx_packets++;
	stats->rx_bytes += length;

	return;

 drop:	
	dev_kfree_skb_irq(skb);
 update_stats:
	stats->rx_errors++;
	stats->rx_dropped++;
}

/********************************************************************/
/* Rx path (info frames)                                            */
/********************************************************************/

static void print_linkstatus(struct net_device *dev, u16 status)
{
	char * s;

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
	
	printk(KERN_INFO "%s: New link status: %s (%04x)\n",
	       dev->name, s, status);
}

/* Search scan results for requested BSSID, join it if found */
static void orinoco_join_ap(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
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
	if (! buf)
		return;

	if (orinoco_lock(priv, &flags) != 0)
		goto fail_lock;

	/* Sanity checks in case user changed something in the meantime */
	if (! priv->bssid_fixed)
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

	if (! found) {
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
static void orinoco_send_wevents(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct hermes *hw = &priv->hw;
	union iwreq_data wrqu;
	int err;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return;

	err = hermes_read_ltv(hw, IRQ_BAP, HERMES_RID_CURRENTBSSID,
			      ETH_ALEN, NULL, wrqu.ap_addr.sa_data);
	if (err != 0)
		goto out;

	wrqu.ap_addr.sa_family = ARPHRD_ETHER;

	/* Send event to user space */
	wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);

 out:
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

		/* Sanity check */
		if (len > 4096) {
			printk(KERN_WARNING "%s: Scan results too large (%d bytes)\n",
			       dev->name, len);
			break;
		}

		/* We are a strict producer. If the previous scan results
		 * have not been consumed, we just have to drop this
		 * frame. We can't remove the previous results ourselves,
		 * that would be *very* racy... Jean II */
		if (priv->scan_result != NULL) {
			printk(KERN_WARNING "%s: Previous scan results not consumed, dropping info frame.\n", dev->name);
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
			for(i = 1; i < (len * 2); i++)
				printk(":%02X", buf[i]);
			printk("]\n");
		}
#endif	/* ORINOCO_DEBUG */

		/* Allow the clients to access the results */
		priv->scan_len = len;
		priv->scan_result = buf;

		/* Send an empty event to user space.
		 * We don't send the received data on the event because
		 * it would require us to do complex transcoding, and
		 * we want to minimise the work done in the irq handler
		 * Use a request to extract the data - Jean II */
		wrqu.data.length = 0;
		wrqu.data.flags = 0;
		wireless_send_event(dev, SIOCGIWSCAN, &wrqu, NULL);
	}
	break;
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

int __orinoco_down(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct hermes *hw = &priv->hw;
	int err;

	netif_stop_queue(dev);

	if (! priv->hw_unavailable) {
		if (! priv->broken_disableport) {
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

int orinoco_reinit_firmware(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct hermes *hw = &priv->hw;
	int err;

	err = hermes_init(hw);
	if (err)
		return err;

	err = hermes_allocate(hw, priv->nicbuf_size, &priv->txfid);
	if (err == -EIO && priv->nicbuf_size > TX_NICBUF_SIZE_BUG) {
		/* Try workaround for old Symbol firmware bug */
		printk(KERN_WARNING "%s: firmware ALLOC bug detected "
		       "(old Symbol firmware?). Trying to work around... ",
		       dev->name);
		
		priv->nicbuf_size = TX_NICBUF_SIZE_BUG;
		err = hermes_allocate(hw, priv->nicbuf_size, &priv->txfid);
		if (err)
			printk("failed!\n");
		else
			printk("ok.\n");
	}

	return err;
}

static int __orinoco_hw_set_bitrate(struct orinoco_private *priv)
{
	hermes_t *hw = &priv->hw;
	int err = 0;

	if (priv->bitratemode >= BITRATE_TABLE_SIZE) {
		printk(KERN_ERR "%s: BUG: Invalid bitrate mode %d\n",
		       priv->ndev->name, priv->bitratemode);
		return -EINVAL;
	}

	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE:
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFTXRATECONTROL,
					   bitrate_table[priv->bitratemode].agere_txratectrl);
		break;
	case FIRMWARE_TYPE_INTERSIL:
	case FIRMWARE_TYPE_SYMBOL:
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFTXRATECONTROL,
					   bitrate_table[priv->bitratemode].intersil_txratectrl);
		break;
	default:
		BUG();
	}

	return err;
}

/* Set fixed AP address */
static int __orinoco_hw_set_wap(struct orinoco_private *priv)
{
	int roaming_flag;
	int err = 0;
	hermes_t *hw = &priv->hw;

	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE:
		/* not supported */
		break;
	case FIRMWARE_TYPE_INTERSIL:
		if (priv->bssid_fixed)
			roaming_flag = 2;
		else
			roaming_flag = 1;

		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFROAMINGMODE,
					   roaming_flag);
		break;
	case FIRMWARE_TYPE_SYMBOL:
		err = HERMES_WRITE_RECORD(hw, USER_BAP,
					  HERMES_RID_CNFMANDATORYBSSID_SYMBOL,
					  &priv->desired_bssid);
		break;
	}
	return err;
}

/* Change the WEP keys and/or the current keys.  Can be called
 * either from __orinoco_hw_setup_wep() or directly from
 * orinoco_ioctl_setiwencode().  In the later case the association
 * with the AP is not broken (if the firmware can handle it),
 * which is needed for 802.1x implementations. */
static int __orinoco_hw_setup_wepkeys(struct orinoco_private *priv)
{
	hermes_t *hw = &priv->hw;
	int err = 0;

	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE:
		err = HERMES_WRITE_RECORD(hw, USER_BAP,
					  HERMES_RID_CNFWEPKEYS_AGERE,
					  &priv->keys);
		if (err)
			return err;
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFTXKEY_AGERE,
					   priv->tx_key);
		if (err)
			return err;
		break;
	case FIRMWARE_TYPE_INTERSIL:
	case FIRMWARE_TYPE_SYMBOL:
		{
			int keylen;
			int i;

			/* Force uniform key length to work around firmware bugs */
			keylen = le16_to_cpu(priv->keys[priv->tx_key].len);
			
			if (keylen > LARGE_KEY_SIZE) {
				printk(KERN_ERR "%s: BUG: Key %d has oversize length %d.\n",
				       priv->ndev->name, priv->tx_key, keylen);
				return -E2BIG;
			}

			/* Write all 4 keys */
			for(i = 0; i < ORINOCO_MAX_KEYS; i++) {
				err = hermes_write_ltv(hw, USER_BAP,
						       HERMES_RID_CNFDEFAULTKEY0 + i,
						       HERMES_BYTES_TO_RECLEN(keylen),
						       priv->keys[i].data);
				if (err)
					return err;
			}

			/* Write the index of the key used in transmission */
			err = hermes_write_wordrec(hw, USER_BAP,
						   HERMES_RID_CNFWEPDEFAULTKEYID,
						   priv->tx_key);
			if (err)
				return err;
		}
		break;
	}

	return 0;
}

static int __orinoco_hw_setup_wep(struct orinoco_private *priv)
{
	hermes_t *hw = &priv->hw;
	int err = 0;
	int master_wep_flag;
	int auth_flag;

	if (priv->wep_on)
		__orinoco_hw_setup_wepkeys(priv);

	if (priv->wep_restrict)
		auth_flag = HERMES_AUTH_SHARED_KEY;
	else
		auth_flag = HERMES_AUTH_OPEN;

	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE: /* Agere style WEP */
		if (priv->wep_on) {
			/* Enable the shared-key authentication. */
			err = hermes_write_wordrec(hw, USER_BAP,
						   HERMES_RID_CNFAUTHENTICATION_AGERE,
						   auth_flag);
		}
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFWEPENABLED_AGERE,
					   priv->wep_on);
		if (err)
			return err;
		break;

	case FIRMWARE_TYPE_INTERSIL: /* Intersil style WEP */
	case FIRMWARE_TYPE_SYMBOL: /* Symbol style WEP */
		if (priv->wep_on) {
			if (priv->wep_restrict ||
			    (priv->firmware_type == FIRMWARE_TYPE_SYMBOL))
				master_wep_flag = HERMES_WEP_PRIVACY_INVOKED |
						  HERMES_WEP_EXCL_UNENCRYPTED;
			else
				master_wep_flag = HERMES_WEP_PRIVACY_INVOKED;

			err = hermes_write_wordrec(hw, USER_BAP,
						   HERMES_RID_CNFAUTHENTICATION,
						   auth_flag);
			if (err)
				return err;
		} else
			master_wep_flag = 0;

		if (priv->iw_mode == IW_MODE_MONITOR)
			master_wep_flag |= HERMES_WEP_HOST_DECRYPT;

		/* Master WEP setting : on/off */
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFWEPFLAGS_INTERSIL,
					   master_wep_flag);
		if (err)
			return err;	

		break;
	}

	return 0;
}

static int __orinoco_program_rids(struct net_device *dev)
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
			printk(KERN_WARNING "%s: Error %d setting SYSTEMSCALE.  "
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
	if (priv->has_wep) {
		err = __orinoco_hw_setup_wep(priv);
		if (err) {
			printk(KERN_ERR "%s: Error %d activating WEP\n",
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
	__orinoco_set_multicast_list(dev); /* FIXME: what about the xmit_lock */

	return 0;
}

/* FIXME: return int? */
static void
__orinoco_set_multicast_list(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err = 0;
	int promisc, mc_count;

	/* The Hermes doesn't seem to have an allmulti mode, so we go
	 * into promiscuous mode and let the upper levels deal. */
	if ( (dev->flags & IFF_PROMISC) || (dev->flags & IFF_ALLMULTI) ||
	     (dev->mc_count > MAX_MULTICAST(priv)) ) {
		promisc = 1;
		mc_count = 0;
	} else {
		promisc = 0;
		mc_count = dev->mc_count;
	}

	if (promisc != priv->promiscuous) {
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFPROMISCUOUSMODE,
					   promisc);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting PROMISCUOUSMODE to 1.\n",
			       dev->name, err);
		} else 
			priv->promiscuous = promisc;
	}

	if (! promisc && (mc_count || priv->mc_count) ) {
		struct dev_mc_list *p = dev->mc_list;
		struct hermes_multicast mclist;
		int i;

		for (i = 0; i < mc_count; i++) {
			/* paranoia: is list shorter than mc_count? */
			BUG_ON(! p);
			/* paranoia: bad address size in list? */
			BUG_ON(p->dmi_addrlen != ETH_ALEN);
			
			memcpy(mclist.addr[i], p->dmi_addr, ETH_ALEN);
			p = p->next;
		}
		
		if (p)
			printk(KERN_WARNING "%s: Multicast list is "
			       "longer than mc_count\n", dev->name);

		err = hermes_write_ltv(hw, USER_BAP, HERMES_RID_CNFGROUPADDRESSES,
				       HERMES_BYTES_TO_RECLEN(priv->mc_count * ETH_ALEN),
				       &mclist);
		if (err)
			printk(KERN_ERR "%s: Error %d setting multicast list.\n",
			       dev->name, err);
		else
			priv->mc_count = mc_count;
	}

	/* Since we can set the promiscuous flag when it wasn't asked
	   for, make sure the net_device knows about it. */
	if (priv->promiscuous)
		dev->flags |= IFF_PROMISC;
	else
		dev->flags &= ~IFF_PROMISC;
}

/* This must be called from user context, without locks held - use
 * schedule_work() */
static void orinoco_reset(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
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
	kfree(priv->scan_result);
	priv->scan_result = NULL;
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

	spin_lock_irq(&priv->lock); /* This has to be called from user context */

	priv->hw_unavailable--;

	/* priv->open or priv->hw_unavailable might have changed while
	 * we dropped the lock */
	if (priv->open && (! priv->hw_unavailable)) {
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

irqreturn_t orinoco_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int count = MAX_IRQLOOPS_PER_IRQ;
	u16 evstat, events;
	/* These are used to detect a runaway interrupt situation */
	/* If we get more than MAX_IRQLOOPS_PER_JIFFY iterations in a jiffy,
	 * we panic and shut down the hardware */
	static int last_irq_jiffy = 0; /* jiffies value the last time
					* we were called */
	static int loops_this_jiffy = 0;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0) {
		/* If hw is unavailable - we don't know if the irq was
		 * for us or not */
		return IRQ_HANDLED;
	}

	evstat = hermes_read_regn(hw, EVSTAT);
	events = evstat & hw->inten;
	if (! events) {
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
		if (! hermes_present(hw)) {
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
	char tmp[SYMBOL_MAX_VER_LEN+1];

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
		priv->broken_monitor = (firmver >= 0x80000);

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
			       "%s: Error %d reading Symbol firmware info. Wildly guessing capabilities...\n",
			       dev->name, err);
			firmver = 0;
			tmp[0] = '\0';
		} else {
			/* The firmware revision is a string, the format is
			 * something like : "V2.20-01".
			 * Quick and dirty parsing... - Jean II
			 */
			firmver = ((tmp[1] - '0') << 16) | ((tmp[3] - '0') << 12)
				| ((tmp[4] - '0') << 8) | ((tmp[6] - '0') << 4)
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
	priv->nicbuf_size = IEEE80211_FRAME_LEN + ETH_HLEN;

	/* Initialize the firmware */
	err = orinoco_reinit_firmware(dev);
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

	if (priv->has_port3)
		printk(KERN_DEBUG "%s: Ad-hoc demo mode supported\n", dev->name);
	if (priv->has_ibss)
		printk(KERN_DEBUG "%s: IEEE standard IBSS ad-hoc mode supported\n",
		       dev->name);
	if (priv->has_wep) {
		printk(KERN_DEBUG "%s: WEP supported, ", dev->name);
		if (priv->has_big_wep)
			printk("104-bit key\n");
		else
			printk("40-bit key\n");
	}

	/* Get the MAC address */
	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CNFOWNMACADDR,
			      ETH_ALEN, NULL, dev->dev_addr);
	if (err) {
		printk(KERN_WARNING "%s: failed to read MAC address!\n",
		       dev->name);
		goto out;
	}

	printk(KERN_DEBUG "%s: MAC address %02X:%02X:%02X:%02X:%02X:%02X\n",
	       dev->name, dev->dev_addr[0], dev->dev_addr[1],
	       dev->dev_addr[2], dev->dev_addr[3], dev->dev_addr[4],
	       dev->dev_addr[5]);

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
	if (err || priv->ap_density < 1 || priv->ap_density > 3) {
		priv->has_sensitivity = 0;
	}

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
		err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNFFRAGMENTATIONTHRESHOLD,
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
	priv->prefer_port3 = priv->has_port3 && (! priv->has_ibss);
	set_port_type(priv);
	priv->channel = 0; /* use firmware default */

	priv->promiscuous = 0;
	priv->wep_on = 0;
	priv->tx_key = 0;

	/* Make the hardware available, as long as it hasn't been
	 * removed elsewhere (e.g. by PCMCIA hot unplug) */
	spin_lock_irq(&priv->lock);
	priv->hw_unavailable--;
	spin_unlock_irq(&priv->lock);

	printk(KERN_DEBUG "%s: ready\n", dev->name);

 out:
	return err;
}

struct net_device *alloc_orinocodev(int sizeof_card,
				    int (*hard_reset)(struct orinoco_private *))
{
	struct net_device *dev;
	struct orinoco_private *priv;

	dev = alloc_etherdev(sizeof(struct orinoco_private) + sizeof_card);
	if (! dev)
		return NULL;
	priv = netdev_priv(dev);
	priv->ndev = dev;
	if (sizeof_card)
		priv->card = (void *)((unsigned long)priv
				      + sizeof(struct orinoco_private));
	else
		priv->card = NULL;

	/* Setup / override net_device fields */
	dev->init = orinoco_init;
	dev->hard_start_xmit = orinoco_xmit;
	dev->tx_timeout = orinoco_tx_timeout;
	dev->watchdog_timeo = HZ; /* 1 second timeout */
	dev->get_stats = orinoco_get_stats;
	dev->ethtool_ops = &orinoco_ethtool_ops;
	dev->wireless_handlers = (struct iw_handler_def *)&orinoco_handler_def;
#ifdef WIRELESS_SPY
	priv->wireless_data.spy_data = &priv->spy_data;
	dev->wireless_data = &priv->wireless_data;
#endif
	dev->change_mtu = orinoco_change_mtu;
	dev->set_multicast_list = orinoco_set_multicast_list;
	/* we use the default eth_mac_addr for setting the MAC addr */

	/* Set up default callbacks */
	dev->open = orinoco_open;
	dev->stop = orinoco_stop;
	priv->hard_reset = hard_reset;

	spin_lock_init(&priv->lock);
	priv->open = 0;
	priv->hw_unavailable = 1; /* orinoco_init() must clear this
				   * before anything else touches the
				   * hardware */
	INIT_WORK(&priv->reset_work, (void (*)(void *))orinoco_reset, dev);
	INIT_WORK(&priv->join_work, (void (*)(void *))orinoco_join_ap, dev);
	INIT_WORK(&priv->wevent_work, (void (*)(void *))orinoco_send_wevents, dev);

	netif_carrier_off(dev);
	priv->last_linkstatus = 0xffff;

	return dev;

}

void free_orinocodev(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);

	kfree(priv->scan_result);
	free_netdev(dev);
}

/********************************************************************/
/* Wireless extensions                                              */
/********************************************************************/

static int orinoco_hw_get_essid(struct orinoco_private *priv, int *active,
				char buf[IW_ESSID_MAX_SIZE+1])
{
	hermes_t *hw = &priv->hw;
	int err = 0;
	struct hermes_idstring essidbuf;
	char *p = (char *)(&essidbuf.val);
	int len;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if (strlen(priv->desired_essid) > 0) {
		/* We read the desired SSID from the hardware rather
		   than from priv->desired_essid, just in case the
		   firmware is allowed to change it on us. I'm not
		   sure about this */
		/* My guess is that the OWNSSID should always be whatever
		 * we set to the card, whereas CURRENT_SSID is the one that
		 * may change... - Jean II */
		u16 rid;

		*active = 1;

		rid = (priv->port_type == 3) ? HERMES_RID_CNFOWNSSID :
			HERMES_RID_CNFDESIREDSSID;
		
		err = hermes_read_ltv(hw, USER_BAP, rid, sizeof(essidbuf),
				      NULL, &essidbuf);
		if (err)
			goto fail_unlock;
	} else {
		*active = 0;

		err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CURRENTSSID,
				      sizeof(essidbuf), NULL, &essidbuf);
		if (err)
			goto fail_unlock;
	}

	len = le16_to_cpu(essidbuf.len);
	BUG_ON(len > IW_ESSID_MAX_SIZE);

	memset(buf, 0, IW_ESSID_MAX_SIZE+1);
	memcpy(buf, p, len);
	buf[len] = '\0';

 fail_unlock:
	orinoco_unlock(priv, &flags);

	return err;       
}

static long orinoco_hw_get_freq(struct orinoco_private *priv)
{
	
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 channel;
	long freq = 0;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CURRENTCHANNEL, &channel);
	if (err)
		goto out;

	/* Intersil firmware 1.3.5 returns 0 when the interface is down */
	if (channel == 0) {
		err = -EBUSY;
		goto out;
	}

	if ( (channel < 1) || (channel > NUM_CHANNELS) ) {
		printk(KERN_WARNING "%s: Channel out of range (%d)!\n",
		       priv->ndev->name, channel);
		err = -EBUSY;
		goto out;

	}
	freq = channel_frequency[channel-1] * 100000;

 out:
	orinoco_unlock(priv, &flags);

	if (err > 0)
		err = -EBUSY;
	return err ? err : freq;
}

static int orinoco_hw_get_bitratelist(struct orinoco_private *priv,
				      int *numrates, s32 *rates, int max)
{
	hermes_t *hw = &priv->hw;
	struct hermes_idstring list;
	unsigned char *p = (unsigned char *)&list.val;
	int err = 0;
	int num;
	int i;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_SUPPORTEDDATARATES,
			      sizeof(list), NULL, &list);
	orinoco_unlock(priv, &flags);

	if (err)
		return err;
	
	num = le16_to_cpu(list.len);
	*numrates = num;
	num = min(num, max);

	for (i = 0; i < num; i++) {
		rates[i] = (p[i] & 0x7f) * 500000; /* convert to bps */
	}

	return 0;
}

static int orinoco_ioctl_getname(struct net_device *dev,
				 struct iw_request_info *info,
				 char *name,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int numrates;
	int err;

	err = orinoco_hw_get_bitratelist(priv, &numrates, NULL, 0);

	if (!err && (numrates > 2))
		strcpy(name, "IEEE 802.11b");
	else
		strcpy(name, "IEEE 802.11-DS");

	return 0;
}

static int orinoco_ioctl_setwap(struct net_device *dev,
				struct iw_request_info *info,
				struct sockaddr *ap_addr,
				char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = -EINPROGRESS;		/* Call commit handler */
	unsigned long flags;
	static const u8 off_addr[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	static const u8 any_addr[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	/* Enable automatic roaming - no sanity checks are needed */
	if (memcmp(&ap_addr->sa_data, off_addr, ETH_ALEN) == 0 ||
	    memcmp(&ap_addr->sa_data, any_addr, ETH_ALEN) == 0) {
		priv->bssid_fixed = 0;
		memset(priv->desired_bssid, 0, ETH_ALEN);

		/* "off" means keep existing connection */
		if (ap_addr->sa_data[0] == 0) {
			__orinoco_hw_set_wap(priv);
			err = 0;
		}
		goto out;
	}

	if (priv->firmware_type == FIRMWARE_TYPE_AGERE) {
		printk(KERN_WARNING "%s: Lucent/Agere firmware doesn't "
		       "support manual roaming\n",
		       dev->name);
		err = -EOPNOTSUPP;
		goto out;
	}

	if (priv->iw_mode != IW_MODE_INFRA) {
		printk(KERN_WARNING "%s: Manual roaming supported only in "
		       "managed mode\n", dev->name);
		err = -EOPNOTSUPP;
		goto out;
	}

	/* Intersil firmware hangs without Desired ESSID */
	if (priv->firmware_type == FIRMWARE_TYPE_INTERSIL &&
	    strlen(priv->desired_essid) == 0) {
		printk(KERN_WARNING "%s: Desired ESSID must be set for "
		       "manual roaming\n", dev->name);
		err = -EOPNOTSUPP;
		goto out;
	}

	/* Finally, enable manual roaming */
	priv->bssid_fixed = 1;
	memcpy(priv->desired_bssid, &ap_addr->sa_data, ETH_ALEN);

 out:
	orinoco_unlock(priv, &flags);
	return err;
}

static int orinoco_ioctl_getwap(struct net_device *dev,
				struct iw_request_info *info,
				struct sockaddr *ap_addr,
				char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);

	hermes_t *hw = &priv->hw;
	int err = 0;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	ap_addr->sa_family = ARPHRD_ETHER;
	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CURRENTBSSID,
			      ETH_ALEN, NULL, ap_addr->sa_data);

	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_setmode(struct net_device *dev,
				 struct iw_request_info *info,
				 u32 *mode,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = -EINPROGRESS;		/* Call commit handler */
	unsigned long flags;

	if (priv->iw_mode == *mode)
		return 0;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	switch (*mode) {
	case IW_MODE_ADHOC:
		if (!priv->has_ibss && !priv->has_port3)
			err = -EOPNOTSUPP;
		break;

	case IW_MODE_INFRA:
		break;

	case IW_MODE_MONITOR:
		if (priv->broken_monitor && !force_monitor) {
			printk(KERN_WARNING "%s: Monitor mode support is "
			       "buggy in this firmware, not enabling\n",
			       dev->name);
			err = -EOPNOTSUPP;
		}
		break;

	default:
		err = -EOPNOTSUPP;
		break;
	}

	if (err == -EINPROGRESS) {
		priv->iw_mode = *mode;
		set_port_type(priv);
	}

	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getmode(struct net_device *dev,
				 struct iw_request_info *info,
				 u32 *mode,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);

	*mode = priv->iw_mode;
	return 0;
}

static int orinoco_ioctl_getiwrange(struct net_device *dev,
				    struct iw_request_info *info,
				    struct iw_point *rrq,
				    char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = 0;
	struct iw_range *range = (struct iw_range *) extra;
	int numrates;
	int i, k;

	rrq->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 14;

	/* Set available channels/frequencies */
	range->num_channels = NUM_CHANNELS;
	k = 0;
	for (i = 0; i < NUM_CHANNELS; i++) {
		if (priv->channel_mask & (1 << i)) {
			range->freq[k].i = i + 1;
			range->freq[k].m = channel_frequency[i] * 100000;
			range->freq[k].e = 1;
			k++;
		}
		
		if (k >= IW_MAX_FREQUENCIES)
			break;
	}
	range->num_frequency = k;
	range->sensitivity = 3;

	if (priv->has_wep) {
		range->max_encoding_tokens = ORINOCO_MAX_KEYS;
		range->encoding_size[0] = SMALL_KEY_SIZE;
		range->num_encoding_sizes = 1;

		if (priv->has_big_wep) {
			range->encoding_size[1] = LARGE_KEY_SIZE;
			range->num_encoding_sizes = 2;
		}
	}

	if ((priv->iw_mode == IW_MODE_ADHOC) && (!SPY_NUMBER(priv))){
		/* Quality stats meaningless in ad-hoc mode */
	} else {
		range->max_qual.qual = 0x8b - 0x2f;
		range->max_qual.level = 0x2f - 0x95 - 1;
		range->max_qual.noise = 0x2f - 0x95 - 1;
		/* Need to get better values */
		range->avg_qual.qual = 0x24;
		range->avg_qual.level = 0xC2;
		range->avg_qual.noise = 0x9E;
	}

	err = orinoco_hw_get_bitratelist(priv, &numrates,
					 range->bitrate, IW_MAX_BITRATES);
	if (err)
		return err;
	range->num_bitrates = numrates;

	/* Set an indication of the max TCP throughput in bit/s that we can
	 * expect using this interface. May be use for QoS stuff...
	 * Jean II */
	if (numrates > 2)
		range->throughput = 5 * 1000 * 1000;	/* ~5 Mb/s */
	else
		range->throughput = 1.5 * 1000 * 1000;	/* ~1.5 Mb/s */

	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;

	range->min_pmp = 0;
	range->max_pmp = 65535000;
	range->min_pmt = 0;
	range->max_pmt = 65535 * 1000;	/* ??? */
	range->pmp_flags = IW_POWER_PERIOD;
	range->pmt_flags = IW_POWER_TIMEOUT;
	range->pm_capa = IW_POWER_PERIOD | IW_POWER_TIMEOUT | IW_POWER_UNICAST_R;

	range->retry_capa = IW_RETRY_LIMIT | IW_RETRY_LIFETIME;
	range->retry_flags = IW_RETRY_LIMIT;
	range->r_time_flags = IW_RETRY_LIFETIME;
	range->min_retry = 0;
	range->max_retry = 65535;	/* ??? */
	range->min_r_time = 0;
	range->max_r_time = 65535 * 1000;	/* ??? */

	/* Event capability (kernel) */
	IW_EVENT_CAPA_SET_KERNEL(range->event_capa);
	/* Event capability (driver) */
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWTHRSPY);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWAP);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWSCAN);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVTXDROP);

	return 0;
}

static int orinoco_ioctl_setiwencode(struct net_device *dev,
				     struct iw_request_info *info,
				     struct iw_point *erq,
				     char *keybuf)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int index = (erq->flags & IW_ENCODE_INDEX) - 1;
	int setindex = priv->tx_key;
	int enable = priv->wep_on;
	int restricted = priv->wep_restrict;
	u16 xlen = 0;
	int err = -EINPROGRESS;		/* Call commit handler */
	unsigned long flags;

	if (! priv->has_wep)
		return -EOPNOTSUPP;

	if (erq->pointer) {
		/* We actually have a key to set - check its length */
		if (erq->length > LARGE_KEY_SIZE)
			return -E2BIG;

		if ( (erq->length > SMALL_KEY_SIZE) && !priv->has_big_wep )
			return -E2BIG;
	}

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if (erq->pointer) {
		if ((index < 0) || (index >= ORINOCO_MAX_KEYS))
			index = priv->tx_key;

		/* Adjust key length to a supported value */
		if (erq->length > SMALL_KEY_SIZE) {
			xlen = LARGE_KEY_SIZE;
		} else if (erq->length > 0) {
			xlen = SMALL_KEY_SIZE;
		} else
			xlen = 0;

		/* Switch on WEP if off */
		if ((!enable) && (xlen > 0)) {
			setindex = index;
			enable = 1;
		}
	} else {
		/* Important note : if the user do "iwconfig eth0 enc off",
		 * we will arrive there with an index of -1. This is valid
		 * but need to be taken care off... Jean II */
		if ((index < 0) || (index >= ORINOCO_MAX_KEYS)) {
			if((index != -1) || (erq->flags == 0)) {
				err = -EINVAL;
				goto out;
			}
		} else {
			/* Set the index : Check that the key is valid */
			if(priv->keys[index].len == 0) {
				err = -EINVAL;
				goto out;
			}
			setindex = index;
		}
	}

	if (erq->flags & IW_ENCODE_DISABLED)
		enable = 0;
	if (erq->flags & IW_ENCODE_OPEN)
		restricted = 0;
	if (erq->flags & IW_ENCODE_RESTRICTED)
		restricted = 1;

	if (erq->pointer) {
		priv->keys[index].len = cpu_to_le16(xlen);
		memset(priv->keys[index].data, 0,
		       sizeof(priv->keys[index].data));
		memcpy(priv->keys[index].data, keybuf, erq->length);
	}
	priv->tx_key = setindex;

	/* Try fast key change if connected and only keys are changed */
	if (priv->wep_on && enable && (priv->wep_restrict == restricted) &&
	    netif_carrier_ok(dev)) {
		err = __orinoco_hw_setup_wepkeys(priv);
		/* No need to commit if successful */
		goto out;
	}

	priv->wep_on = enable;
	priv->wep_restrict = restricted;

 out:
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getiwencode(struct net_device *dev,
				     struct iw_request_info *info,
				     struct iw_point *erq,
				     char *keybuf)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int index = (erq->flags & IW_ENCODE_INDEX) - 1;
	u16 xlen = 0;
	unsigned long flags;

	if (! priv->has_wep)
		return -EOPNOTSUPP;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if ((index < 0) || (index >= ORINOCO_MAX_KEYS))
		index = priv->tx_key;

	erq->flags = 0;
	if (! priv->wep_on)
		erq->flags |= IW_ENCODE_DISABLED;
	erq->flags |= index + 1;

	if (priv->wep_restrict)
		erq->flags |= IW_ENCODE_RESTRICTED;
	else
		erq->flags |= IW_ENCODE_OPEN;

	xlen = le16_to_cpu(priv->keys[index].len);

	erq->length = xlen;

	memcpy(keybuf, priv->keys[index].data, ORINOCO_MAX_KEY_SIZE);

	orinoco_unlock(priv, &flags);
	return 0;
}

static int orinoco_ioctl_setessid(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *erq,
				  char *essidbuf)
{
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;

	/* Note : ESSID is ignored in Ad-Hoc demo mode, but we can set it
	 * anyway... - Jean II */

	/* Hum... Should not use Wireless Extension constant (may change),
	 * should use our own... - Jean II */
	if (erq->length > IW_ESSID_MAX_SIZE)
		return -E2BIG;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	/* NULL the string (for NULL termination & ESSID = ANY) - Jean II */
	memset(priv->desired_essid, 0, sizeof(priv->desired_essid));

	/* If not ANY, get the new ESSID */
	if (erq->flags) {
		memcpy(priv->desired_essid, essidbuf, erq->length);
	}

	orinoco_unlock(priv, &flags);

	return -EINPROGRESS;		/* Call commit handler */
}

static int orinoco_ioctl_getessid(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *erq,
				  char *essidbuf)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int active;
	int err = 0;
	unsigned long flags;

	if (netif_running(dev)) {
		err = orinoco_hw_get_essid(priv, &active, essidbuf);
		if (err)
			return err;
	} else {
		if (orinoco_lock(priv, &flags) != 0)
			return -EBUSY;
		memcpy(essidbuf, priv->desired_essid, IW_ESSID_MAX_SIZE + 1);
		orinoco_unlock(priv, &flags);
	}

	erq->flags = 1;
	erq->length = strlen(essidbuf) + 1;

	return 0;
}

static int orinoco_ioctl_setnick(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *nrq,
				 char *nickbuf)
{
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;

	if (nrq->length > IW_ESSID_MAX_SIZE)
		return -E2BIG;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	memset(priv->nick, 0, sizeof(priv->nick));
	memcpy(priv->nick, nickbuf, nrq->length);

	orinoco_unlock(priv, &flags);

	return -EINPROGRESS;		/* Call commit handler */
}

static int orinoco_ioctl_getnick(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *nrq,
				 char *nickbuf)
{
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	memcpy(nickbuf, priv->nick, IW_ESSID_MAX_SIZE+1);
	orinoco_unlock(priv, &flags);

	nrq->length = strlen(nickbuf)+1;

	return 0;
}

static int orinoco_ioctl_setfreq(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_freq *frq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int chan = -1;
	unsigned long flags;
	int err = -EINPROGRESS;		/* Call commit handler */

	/* In infrastructure mode the AP sets the channel */
	if (priv->iw_mode == IW_MODE_INFRA)
		return -EBUSY;

	if ( (frq->e == 0) && (frq->m <= 1000) ) {
		/* Setting by channel number */
		chan = frq->m;
	} else {
		/* Setting by frequency - search the table */
		int mult = 1;
		int i;

		for (i = 0; i < (6 - frq->e); i++)
			mult *= 10;

		for (i = 0; i < NUM_CHANNELS; i++)
			if (frq->m == (channel_frequency[i] * mult))
				chan = i+1;
	}

	if ( (chan < 1) || (chan > NUM_CHANNELS) ||
	     ! (priv->channel_mask & (1 << (chan-1)) ) )
		return -EINVAL;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	priv->channel = chan;
	if (priv->iw_mode == IW_MODE_MONITOR) {
		/* Fast channel change - no commit if successful */
		hermes_t *hw = &priv->hw;
		err = hermes_docmd_wait(hw, HERMES_CMD_TEST |
					    HERMES_TEST_SET_CHANNEL,
					chan, NULL);
	}
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getfreq(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_freq *frq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int tmp;

	/* Locking done in there */
	tmp = orinoco_hw_get_freq(priv);
	if (tmp < 0) {
		return tmp;
	}

	frq->m = tmp;
	frq->e = 1;

	return 0;
}

static int orinoco_ioctl_getsens(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *srq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	u16 val;
	int err;
	unsigned long flags;

	if (!priv->has_sensitivity)
		return -EOPNOTSUPP;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	err = hermes_read_wordrec(hw, USER_BAP,
				  HERMES_RID_CNFSYSTEMSCALE, &val);
	orinoco_unlock(priv, &flags);

	if (err)
		return err;

	srq->value = val;
	srq->fixed = 0; /* auto */

	return 0;
}

static int orinoco_ioctl_setsens(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *srq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int val = srq->value;
	unsigned long flags;

	if (!priv->has_sensitivity)
		return -EOPNOTSUPP;

	if ((val < 1) || (val > 3))
		return -EINVAL;
	
	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	priv->ap_density = val;
	orinoco_unlock(priv, &flags);

	return -EINPROGRESS;		/* Call commit handler */
}

static int orinoco_ioctl_setrts(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_param *rrq,
				char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int val = rrq->value;
	unsigned long flags;

	if (rrq->disabled)
		val = 2347;

	if ( (val < 0) || (val > 2347) )
		return -EINVAL;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	priv->rts_thresh = val;
	orinoco_unlock(priv, &flags);

	return -EINPROGRESS;		/* Call commit handler */
}

static int orinoco_ioctl_getrts(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_param *rrq,
				char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);

	rrq->value = priv->rts_thresh;
	rrq->disabled = (rrq->value == 2347);
	rrq->fixed = 1;

	return 0;
}

static int orinoco_ioctl_setfrag(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *frq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = -EINPROGRESS;		/* Call commit handler */
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if (priv->has_mwo) {
		if (frq->disabled)
			priv->mwo_robust = 0;
		else {
			if (frq->fixed)
				printk(KERN_WARNING "%s: Fixed fragmentation is "
				       "not supported on this firmware. "
				       "Using MWO robust instead.\n", dev->name);
			priv->mwo_robust = 1;
		}
	} else {
		if (frq->disabled)
			priv->frag_thresh = 2346;
		else {
			if ( (frq->value < 256) || (frq->value > 2346) )
				err = -EINVAL;
			else
				priv->frag_thresh = frq->value & ~0x1; /* must be even */
		}
	}

	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getfrag(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *frq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err;
	u16 val;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	
	if (priv->has_mwo) {
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFMWOROBUST_AGERE,
					  &val);
		if (err)
			val = 0;

		frq->value = val ? 2347 : 0;
		frq->disabled = ! val;
		frq->fixed = 0;
	} else {
		err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNFFRAGMENTATIONTHRESHOLD,
					  &val);
		if (err)
			val = 0;

		frq->value = val;
		frq->disabled = (val >= 2346);
		frq->fixed = 1;
	}

	orinoco_unlock(priv, &flags);
	
	return err;
}

static int orinoco_ioctl_setrate(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *rrq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int ratemode = -1;
	int bitrate; /* 100s of kilobits */
	int i;
	unsigned long flags;
	
	/* As the user space doesn't know our highest rate, it uses -1
	 * to ask us to set the highest rate.  Test it using "iwconfig
	 * ethX rate auto" - Jean II */
	if (rrq->value == -1)
		bitrate = 110;
	else {
		if (rrq->value % 100000)
			return -EINVAL;
		bitrate = rrq->value / 100000;
	}

	if ( (bitrate != 10) && (bitrate != 20) &&
	     (bitrate != 55) && (bitrate != 110) )
		return -EINVAL;

	for (i = 0; i < BITRATE_TABLE_SIZE; i++)
		if ( (bitrate_table[i].bitrate == bitrate) &&
		     (bitrate_table[i].automatic == ! rrq->fixed) ) {
			ratemode = i;
			break;
		}
	
	if (ratemode == -1)
		return -EINVAL;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	priv->bitratemode = ratemode;
	orinoco_unlock(priv, &flags);

	return -EINPROGRESS;
}

static int orinoco_ioctl_getrate(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *rrq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err = 0;
	int ratemode;
	int i;
	u16 val;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	ratemode = priv->bitratemode;

	BUG_ON((ratemode < 0) || (ratemode >= BITRATE_TABLE_SIZE));

	rrq->value = bitrate_table[ratemode].bitrate * 100000;
	rrq->fixed = ! bitrate_table[ratemode].automatic;
	rrq->disabled = 0;

	/* If the interface is running we try to find more about the
	   current mode */
	if (netif_running(dev)) {
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CURRENTTXRATE, &val);
		if (err)
			goto out;
		
		switch (priv->firmware_type) {
		case FIRMWARE_TYPE_AGERE: /* Lucent style rate */
			/* Note : in Lucent firmware, the return value of
			 * HERMES_RID_CURRENTTXRATE is the bitrate in Mb/s,
			 * and therefore is totally different from the
			 * encoding of HERMES_RID_CNFTXRATECONTROL.
			 * Don't forget that 6Mb/s is really 5.5Mb/s */
			if (val == 6)
				rrq->value = 5500000;
			else
				rrq->value = val * 1000000;
			break;
		case FIRMWARE_TYPE_INTERSIL: /* Intersil style rate */
		case FIRMWARE_TYPE_SYMBOL: /* Symbol style rate */
			for (i = 0; i < BITRATE_TABLE_SIZE; i++)
				if (bitrate_table[i].intersil_txratectrl == val) {
					ratemode = i;
					break;
				}
			if (i >= BITRATE_TABLE_SIZE)
				printk(KERN_INFO "%s: Unable to determine current bitrate (0x%04hx)\n",
				       dev->name, val);

			rrq->value = bitrate_table[ratemode].bitrate * 100000;
			break;
		default:
			BUG();
		}
	}

 out:
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_setpower(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_param *prq,
				  char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = -EINPROGRESS;		/* Call commit handler */
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if (prq->disabled) {
		priv->pm_on = 0;
	} else {
		switch (prq->flags & IW_POWER_MODE) {
		case IW_POWER_UNICAST_R:
			priv->pm_mcast = 0;
			priv->pm_on = 1;
			break;
		case IW_POWER_ALL_R:
			priv->pm_mcast = 1;
			priv->pm_on = 1;
			break;
		case IW_POWER_ON:
			/* No flags : but we may have a value - Jean II */
			break;
		default:
			err = -EINVAL;
			goto out;
		}
		
		if (prq->flags & IW_POWER_TIMEOUT) {
			priv->pm_on = 1;
			priv->pm_timeout = prq->value / 1000;
		}
		if (prq->flags & IW_POWER_PERIOD) {
			priv->pm_on = 1;
			priv->pm_period = prq->value / 1000;
		}
		/* It's valid to not have a value if we are just toggling
		 * the flags... Jean II */
		if(!priv->pm_on) {
			err = -EINVAL;
			goto out;
		}			
	}

 out:
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getpower(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_param *prq,
				  char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 enable, period, timeout, mcast;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNFPMENABLED, &enable);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP,
				  HERMES_RID_CNFMAXSLEEPDURATION, &period);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNFPMHOLDOVERDURATION, &timeout);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNFMULTICASTRECEIVE, &mcast);
	if (err)
		goto out;

	prq->disabled = !enable;
	/* Note : by default, display the period */
	if ((prq->flags & IW_POWER_TYPE) == IW_POWER_TIMEOUT) {
		prq->flags = IW_POWER_TIMEOUT;
		prq->value = timeout * 1000;
	} else {
		prq->flags = IW_POWER_PERIOD;
		prq->value = period * 1000;
	}
	if (mcast)
		prq->flags |= IW_POWER_ALL_R;
	else
		prq->flags |= IW_POWER_UNICAST_R;

 out:
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getretry(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_param *rrq,
				  char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 short_limit, long_limit, lifetime;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_SHORTRETRYLIMIT,
				  &short_limit);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_LONGRETRYLIMIT,
				  &long_limit);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_MAXTRANSMITLIFETIME,
				  &lifetime);
	if (err)
		goto out;

	rrq->disabled = 0;		/* Can't be disabled */

	/* Note : by default, display the retry number */
	if ((rrq->flags & IW_RETRY_TYPE) == IW_RETRY_LIFETIME) {
		rrq->flags = IW_RETRY_LIFETIME;
		rrq->value = lifetime * 1000;	/* ??? */
	} else {
		/* By default, display the min number */
		if ((rrq->flags & IW_RETRY_MAX)) {
			rrq->flags = IW_RETRY_LIMIT | IW_RETRY_MAX;
			rrq->value = long_limit;
		} else {
			rrq->flags = IW_RETRY_LIMIT;
			rrq->value = short_limit;
			if(short_limit != long_limit)
				rrq->flags |= IW_RETRY_MIN;
		}
	}

 out:
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_reset(struct net_device *dev,
			       struct iw_request_info *info,
			       void *wrqu,
			       char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);

	if (! capable(CAP_NET_ADMIN))
		return -EPERM;

	if (info->cmd == (SIOCIWFIRSTPRIV + 0x1)) {
		printk(KERN_DEBUG "%s: Forcing reset!\n", dev->name);

		/* Firmware reset */
		orinoco_reset(dev);
	} else {
		printk(KERN_DEBUG "%s: Force scheduling reset!\n", dev->name);

		schedule_work(&priv->reset_work);
	}

	return 0;
}

static int orinoco_ioctl_setibssport(struct net_device *dev,
				     struct iw_request_info *info,
				     void *wrqu,
				     char *extra)

{
	struct orinoco_private *priv = netdev_priv(dev);
	int val = *( (int *) extra );
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	priv->ibss_port = val ;

	/* Actually update the mode we are using */
	set_port_type(priv);

	orinoco_unlock(priv, &flags);
	return -EINPROGRESS;		/* Call commit handler */
}

static int orinoco_ioctl_getibssport(struct net_device *dev,
				     struct iw_request_info *info,
				     void *wrqu,
				     char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int *val = (int *) extra;

	*val = priv->ibss_port;
	return 0;
}

static int orinoco_ioctl_setport3(struct net_device *dev,
				  struct iw_request_info *info,
				  void *wrqu,
				  char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int val = *( (int *) extra );
	int err = 0;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	switch (val) {
	case 0: /* Try to do IEEE ad-hoc mode */
		if (! priv->has_ibss) {
			err = -EINVAL;
			break;
		}
		priv->prefer_port3 = 0;
			
		break;

	case 1: /* Try to do Lucent proprietary ad-hoc mode */
		if (! priv->has_port3) {
			err = -EINVAL;
			break;
		}
		priv->prefer_port3 = 1;
		break;

	default:
		err = -EINVAL;
	}

	if (! err) {
		/* Actually update the mode we are using */
		set_port_type(priv);
		err = -EINPROGRESS;
	}

	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getport3(struct net_device *dev,
				  struct iw_request_info *info,
				  void *wrqu,
				  char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int *val = (int *) extra;

	*val = priv->prefer_port3;
	return 0;
}

static int orinoco_ioctl_setpreamble(struct net_device *dev,
				     struct iw_request_info *info,
				     void *wrqu,
				     char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;
	int val;

	if (! priv->has_preamble)
		return -EOPNOTSUPP;

	/* 802.11b has recently defined some short preamble.
	 * Basically, the Phy header has been reduced in size.
	 * This increase performance, especially at high rates
	 * (the preamble is transmitted at 1Mb/s), unfortunately
	 * this give compatibility troubles... - Jean II */
	val = *( (int *) extra );

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if (val)
		priv->preamble = 1;
	else
		priv->preamble = 0;

	orinoco_unlock(priv, &flags);

	return -EINPROGRESS;		/* Call commit handler */
}

static int orinoco_ioctl_getpreamble(struct net_device *dev,
				     struct iw_request_info *info,
				     void *wrqu,
				     char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int *val = (int *) extra;

	if (! priv->has_preamble)
		return -EOPNOTSUPP;

	*val = priv->preamble;
	return 0;
}

/* ioctl interface to hermes_read_ltv()
 * To use with iwpriv, pass the RID as the token argument, e.g.
 * iwpriv get_rid [0xfc00]
 * At least Wireless Tools 25 is required to use iwpriv.
 * For Wireless Tools 25 and 26 append "dummy" are the end. */
static int orinoco_ioctl_getrid(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_point *data,
				char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int rid = data->flags;
	u16 length;
	int err;
	unsigned long flags;

	/* It's a "get" function, but we don't want users to access the
	 * WEP key and other raw firmware data */
	if (! capable(CAP_NET_ADMIN))
		return -EPERM;

	if (rid < 0xfc00 || rid > 0xffff)
		return -EINVAL;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	err = hermes_read_ltv(hw, USER_BAP, rid, MAX_RID_LEN, &length,
			      extra);
	if (err)
		goto out;

	data->length = min_t(u16, HERMES_RECLEN_TO_BYTES(length),
			     MAX_RID_LEN);

 out:
	orinoco_unlock(priv, &flags);
	return err;
}

/* Trigger a scan (look for other cells in the vicinity */
static int orinoco_ioctl_setscan(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *srq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err = 0;
	unsigned long flags;

	/* Note : you may have realised that, as this is a SET operation,
	 * this is privileged and therefore a normal user can't
	 * perform scanning.
	 * This is not an error, while the device perform scanning,
	 * traffic doesn't flow, so it's a perfect DoS...
	 * Jean II */

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	/* Scanning with port 0 disabled would fail */
	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	/* In monitor mode, the scan results are always empty.
	 * Probe responses are passed to the driver as received
	 * frames and could be processed in software. */
	if (priv->iw_mode == IW_MODE_MONITOR) {
		err = -EOPNOTSUPP;
		goto out;
	}

	/* Note : because we don't lock out the irq handler, the way
	 * we access scan variables in priv is critical.
	 *	o scan_inprogress : not touched by irq handler
	 *	o scan_mode : not touched by irq handler
	 *	o scan_result : irq is strict producer, non-irq is strict
	 *		consumer.
	 *	o scan_len : synchronised with scan_result
	 * Before modifying anything on those variables, please think hard !
	 * Jean II */

	/* If there is still some left-over scan results, get rid of it */
	if (priv->scan_result != NULL) {
		/* What's likely is that a client did crash or was killed
		 * between triggering the scan request and reading the
		 * results, so we need to reset everything.
		 * Some clients that are too slow may suffer from that...
		 * Jean II */
		kfree(priv->scan_result);
		priv->scan_result = NULL;
	}

	/* Save flags */
	priv->scan_mode = srq->flags;

	/* Always trigger scanning, even if it's in progress.
	 * This way, if the info frame get lost, we will recover somewhat
	 * gracefully  - Jean II */

	if (priv->has_hostscan) {
		switch (priv->firmware_type) {
		case FIRMWARE_TYPE_SYMBOL:
			err = hermes_write_wordrec(hw, USER_BAP,
						   HERMES_RID_CNFHOSTSCAN_SYMBOL,
						   HERMES_HOSTSCAN_SYMBOL_ONCE |
						   HERMES_HOSTSCAN_SYMBOL_BCAST);
			break;
		case FIRMWARE_TYPE_INTERSIL: {
			__le16 req[3];

			req[0] = cpu_to_le16(0x3fff);	/* All channels */
			req[1] = cpu_to_le16(0x0001);	/* rate 1 Mbps */
			req[2] = 0;			/* Any ESSID */
			err = HERMES_WRITE_RECORD(hw, USER_BAP,
						  HERMES_RID_CNFHOSTSCAN, &req);
		}
		break;
		case FIRMWARE_TYPE_AGERE:
			err = hermes_write_wordrec(hw, USER_BAP,
						   HERMES_RID_CNFSCANSSID_AGERE,
						   0);	/* Any ESSID */
			if (err)
				break;

			err = hermes_inquire(hw, HERMES_INQ_SCAN);
			break;
		}
	} else
		err = hermes_inquire(hw, HERMES_INQ_SCAN);

	/* One more client */
	if (! err)
		priv->scan_inprogress = 1;

 out:
	orinoco_unlock(priv, &flags);
	return err;
}

/* Translate scan data returned from the card to a card independant
 * format that the Wireless Tools will understand - Jean II
 * Return message length or -errno for fatal errors */
static inline int orinoco_translate_scan(struct net_device *dev,
					 char *buffer,
					 char *scan,
					 int scan_len)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int			offset;		/* In the scan data */
	union hermes_scan_info *atom;
	int			atom_len;
	u16			capabilities;
	u16			channel;
	struct iw_event		iwe;		/* Temporary buffer */
	char *			current_ev = buffer;
	char *			end_buf = buffer + IW_SCAN_MAX_DATA;

	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE:
		atom_len = sizeof(struct agere_scan_apinfo);
 		offset = 0;
		break;
	case FIRMWARE_TYPE_SYMBOL:
		/* Lack of documentation necessitates this hack.
		 * Different firmwares have 68 or 76 byte long atoms.
		 * We try modulo first.  If the length divides by both,
		 * we check what would be the channel in the second
		 * frame for a 68-byte atom.  76-byte atoms have 0 there.
		 * Valid channel cannot be 0.  */
		if (scan_len % 76)
			atom_len = 68;
		else if (scan_len % 68)
			atom_len = 76;
		else if (scan_len >= 1292 && scan[68] == 0)
			atom_len = 76;
		else
			atom_len = 68;
		offset = 0;
		break;
	case FIRMWARE_TYPE_INTERSIL:
		offset = 4;
		if (priv->has_hostscan) {
			atom_len = le16_to_cpup((__le16 *)scan);
			/* Sanity check for atom_len */
			if (atom_len < sizeof(struct prism2_scan_apinfo)) {
				printk(KERN_ERR "%s: Invalid atom_len in scan data: %d\n",
				dev->name, atom_len);
				return -EIO;
			}
		} else
			atom_len = offsetof(struct prism2_scan_apinfo, atim);
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* Check that we got an whole number of atoms */
	if ((scan_len - offset) % atom_len) {
		printk(KERN_ERR "%s: Unexpected scan data length %d, "
		       "atom_len %d, offset %d\n", dev->name, scan_len,
		       atom_len, offset);
		return -EIO;
	}

	/* Read the entries one by one */
	for (; offset + atom_len <= scan_len; offset += atom_len) {
		/* Get next atom */
		atom = (union hermes_scan_info *) (scan + offset);

		/* First entry *MUST* be the AP MAC address */
		iwe.cmd = SIOCGIWAP;
		iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
		memcpy(iwe.u.ap_addr.sa_data, atom->a.bssid, ETH_ALEN);
		current_ev = iwe_stream_add_event(current_ev, end_buf, &iwe, IW_EV_ADDR_LEN);

		/* Other entries will be displayed in the order we give them */

		/* Add the ESSID */
		iwe.u.data.length = le16_to_cpu(atom->a.essid_len);
		if (iwe.u.data.length > 32)
			iwe.u.data.length = 32;
		iwe.cmd = SIOCGIWESSID;
		iwe.u.data.flags = 1;
		current_ev = iwe_stream_add_point(current_ev, end_buf, &iwe, atom->a.essid);

		/* Add mode */
		iwe.cmd = SIOCGIWMODE;
		capabilities = le16_to_cpu(atom->a.capabilities);
		if (capabilities & 0x3) {
			if (capabilities & 0x1)
				iwe.u.mode = IW_MODE_MASTER;
			else
				iwe.u.mode = IW_MODE_ADHOC;
			current_ev = iwe_stream_add_event(current_ev, end_buf, &iwe, IW_EV_UINT_LEN);
		}

		channel = atom->s.channel;
		if ( (channel >= 1) && (channel <= NUM_CHANNELS) ) {
			/* Add frequency */
			iwe.cmd = SIOCGIWFREQ;
			iwe.u.freq.m = channel_frequency[channel-1] * 100000;
			iwe.u.freq.e = 1;
			current_ev = iwe_stream_add_event(current_ev, end_buf,
							  &iwe, IW_EV_FREQ_LEN);
		}

		/* Add quality statistics */
		iwe.cmd = IWEVQUAL;
		iwe.u.qual.updated = 0x10;	/* no link quality */
		iwe.u.qual.level = (__u8) le16_to_cpu(atom->a.level) - 0x95;
		iwe.u.qual.noise = (__u8) le16_to_cpu(atom->a.noise) - 0x95;
		/* Wireless tools prior to 27.pre22 will show link quality
		 * anyway, so we provide a reasonable value. */
		if (iwe.u.qual.level > iwe.u.qual.noise)
			iwe.u.qual.qual = iwe.u.qual.level - iwe.u.qual.noise;
		else
			iwe.u.qual.qual = 0;
		current_ev = iwe_stream_add_event(current_ev, end_buf, &iwe, IW_EV_QUAL_LEN);

		/* Add encryption capability */
		iwe.cmd = SIOCGIWENCODE;
		if (capabilities & 0x10)
			iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
		else
			iwe.u.data.flags = IW_ENCODE_DISABLED;
		iwe.u.data.length = 0;
		current_ev = iwe_stream_add_point(current_ev, end_buf, &iwe, atom->a.essid);

		/* Bit rate is not available in Lucent/Agere firmwares */
		if (priv->firmware_type != FIRMWARE_TYPE_AGERE) {
			char *	current_val = current_ev + IW_EV_LCP_LEN;
			int	i;
			int	step;

			if (priv->firmware_type == FIRMWARE_TYPE_SYMBOL)
				step = 2;
			else
				step = 1;

			iwe.cmd = SIOCGIWRATE;
			/* Those two flags are ignored... */
			iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;
			/* Max 10 values */
			for (i = 0; i < 10; i += step) {
				/* NULL terminated */
				if (atom->p.rates[i] == 0x0)
					break;
				/* Bit rate given in 500 kb/s units (+ 0x80) */
				iwe.u.bitrate.value = ((atom->p.rates[i] & 0x7f) * 500000);
				current_val = iwe_stream_add_value(current_ev, current_val,
								   end_buf, &iwe,
								   IW_EV_PARAM_LEN);
			}
			/* Check if we added any event */
			if ((current_val - current_ev) > IW_EV_LCP_LEN)
				current_ev = current_val;
		}

		/* The other data in the scan result are not really
		 * interesting, so for now drop it - Jean II */
	}
	return current_ev - buffer;
}

/* Return results of a scan */
static int orinoco_ioctl_getscan(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *srq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = 0;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	/* If no results yet, ask to try again later */
	if (priv->scan_result == NULL) {
		if (priv->scan_inprogress)
			/* Important note : we don't want to block the caller
			 * until results are ready for various reasons.
			 * First, managing wait queues is complex and racy.
			 * Second, we grab some rtnetlink lock before comming
			 * here (in dev_ioctl()).
			 * Third, we generate an Wireless Event, so the
			 * caller can wait itself on that - Jean II */
			err = -EAGAIN;
		else
			/* Client error, no scan results...
			 * The caller need to restart the scan. */
			err = -ENODATA;
	} else {
		/* We have some results to push back to user space */

		/* Translate to WE format */
		int ret = orinoco_translate_scan(dev, extra,
						 priv->scan_result,
						 priv->scan_len);

		if (ret < 0) {
			err = ret;
			kfree(priv->scan_result);
			priv->scan_result = NULL;
		} else {
			srq->length = ret;

			/* Return flags */
			srq->flags = (__u16) priv->scan_mode;

			/* In any case, Scan results will be cleaned up in the
			 * reset function and when exiting the driver.
			 * The person triggering the scanning may never come to
			 * pick the results, so we need to do it in those places.
			 * Jean II */

#ifdef SCAN_SINGLE_READ
			/* If you enable this option, only one client (the first
			 * one) will be able to read the result (and only one
			 * time). If there is multiple concurent clients that
			 * want to read scan results, this behavior is not
			 * advisable - Jean II */
			kfree(priv->scan_result);
			priv->scan_result = NULL;
#endif /* SCAN_SINGLE_READ */
			/* Here, if too much time has elapsed since last scan,
			 * we may want to clean up scan results... - Jean II */
		}

		/* Scan is no longer in progress */
		priv->scan_inprogress = 0;
	}
	  
	orinoco_unlock(priv, &flags);
	return err;
}

/* Commit handler, called after set operations */
static int orinoco_ioctl_commit(struct net_device *dev,
				struct iw_request_info *info,
				void *wrqu,
				char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct hermes *hw = &priv->hw;
	unsigned long flags;
	int err = 0;

	if (!priv->open)
		return 0;

	if (priv->broken_disableport) {
		orinoco_reset(dev);
		return 0;
	}

	if (orinoco_lock(priv, &flags) != 0)
		return err;

	err = hermes_disable_port(hw, 0);
	if (err) {
		printk(KERN_WARNING "%s: Unable to disable port "
		       "while reconfiguring card\n", dev->name);
		priv->broken_disableport = 1;
		goto out;
	}

	err = __orinoco_program_rids(dev);
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

	orinoco_unlock(priv, &flags);
	return err;
}

static const struct iw_priv_args orinoco_privtab[] = {
	{ SIOCIWFIRSTPRIV + 0x0, 0, 0, "force_reset" },
	{ SIOCIWFIRSTPRIV + 0x1, 0, 0, "card_reset" },
	{ SIOCIWFIRSTPRIV + 0x2, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	  0, "set_port3" },
	{ SIOCIWFIRSTPRIV + 0x3, 0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	  "get_port3" },
	{ SIOCIWFIRSTPRIV + 0x4, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	  0, "set_preamble" },
	{ SIOCIWFIRSTPRIV + 0x5, 0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	  "get_preamble" },
	{ SIOCIWFIRSTPRIV + 0x6, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	  0, "set_ibssport" },
	{ SIOCIWFIRSTPRIV + 0x7, 0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	  "get_ibssport" },
	{ SIOCIWFIRSTPRIV + 0x9, 0, IW_PRIV_TYPE_BYTE | MAX_RID_LEN,
	  "get_rid" },
};


/*
 * Structures to export the Wireless Handlers
 */

static const iw_handler	orinoco_handler[] = {
	[SIOCSIWCOMMIT-SIOCIWFIRST] = (iw_handler) orinoco_ioctl_commit,
	[SIOCGIWNAME  -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_getname,
	[SIOCSIWFREQ  -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_setfreq,
	[SIOCGIWFREQ  -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_getfreq,
	[SIOCSIWMODE  -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_setmode,
	[SIOCGIWMODE  -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_getmode,
	[SIOCSIWSENS  -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_setsens,
	[SIOCGIWSENS  -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_getsens,
	[SIOCGIWRANGE -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_getiwrange,
	[SIOCSIWSPY   -SIOCIWFIRST] = (iw_handler) iw_handler_set_spy,
	[SIOCGIWSPY   -SIOCIWFIRST] = (iw_handler) iw_handler_get_spy,
	[SIOCSIWTHRSPY-SIOCIWFIRST] = (iw_handler) iw_handler_set_thrspy,
	[SIOCGIWTHRSPY-SIOCIWFIRST] = (iw_handler) iw_handler_get_thrspy,
	[SIOCSIWAP    -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_setwap,
	[SIOCGIWAP    -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_getwap,
	[SIOCSIWSCAN  -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_setscan,
	[SIOCGIWSCAN  -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_getscan,
	[SIOCSIWESSID -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_setessid,
	[SIOCGIWESSID -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_getessid,
	[SIOCSIWNICKN -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_setnick,
	[SIOCGIWNICKN -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_getnick,
	[SIOCSIWRATE  -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_setrate,
	[SIOCGIWRATE  -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_getrate,
	[SIOCSIWRTS   -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_setrts,
	[SIOCGIWRTS   -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_getrts,
	[SIOCSIWFRAG  -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_setfrag,
	[SIOCGIWFRAG  -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_getfrag,
	[SIOCGIWRETRY -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_getretry,
	[SIOCSIWENCODE-SIOCIWFIRST] = (iw_handler) orinoco_ioctl_setiwencode,
	[SIOCGIWENCODE-SIOCIWFIRST] = (iw_handler) orinoco_ioctl_getiwencode,
	[SIOCSIWPOWER -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_setpower,
	[SIOCGIWPOWER -SIOCIWFIRST] = (iw_handler) orinoco_ioctl_getpower,
};


/*
  Added typecasting since we no longer use iwreq_data -- Moustafa
 */
static const iw_handler	orinoco_private_handler[] = {
	[0] = (iw_handler) orinoco_ioctl_reset,
	[1] = (iw_handler) orinoco_ioctl_reset,
	[2] = (iw_handler) orinoco_ioctl_setport3,
	[3] = (iw_handler) orinoco_ioctl_getport3,
	[4] = (iw_handler) orinoco_ioctl_setpreamble,
	[5] = (iw_handler) orinoco_ioctl_getpreamble,
	[6] = (iw_handler) orinoco_ioctl_setibssport,
	[7] = (iw_handler) orinoco_ioctl_getibssport,
	[9] = (iw_handler) orinoco_ioctl_getrid,
};

static const struct iw_handler_def orinoco_handler_def = {
	.num_standard = ARRAY_SIZE(orinoco_handler),
	.num_private = ARRAY_SIZE(orinoco_private_handler),
	.num_private_args = ARRAY_SIZE(orinoco_privtab),
	.standard = orinoco_handler,
	.private = orinoco_private_handler,
	.private_args = orinoco_privtab,
	.get_wireless_stats = orinoco_get_wireless_stats,
};

static void orinoco_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	struct orinoco_private *priv = netdev_priv(dev);

	strncpy(info->driver, DRIVER_NAME, sizeof(info->driver) - 1);
	strncpy(info->version, DRIVER_VERSION, sizeof(info->version) - 1);
	strncpy(info->fw_version, priv->fw_name, sizeof(info->fw_version) - 1);
	if (dev->class_dev.dev)
		strncpy(info->bus_info, dev->class_dev.dev->bus_id,
			sizeof(info->bus_info) - 1);
	else
		snprintf(info->bus_info, sizeof(info->bus_info) - 1,
			 "PCMCIA %p", priv->hw.iobase);
}

static struct ethtool_ops orinoco_ethtool_ops = {
	.get_drvinfo = orinoco_get_drvinfo,
	.get_link = ethtool_op_get_link,
};

/********************************************************************/
/* Module initialization                                            */
/********************************************************************/

EXPORT_SYMBOL(alloc_orinocodev);
EXPORT_SYMBOL(free_orinocodev);

EXPORT_SYMBOL(__orinoco_up);
EXPORT_SYMBOL(__orinoco_down);
EXPORT_SYMBOL(orinoco_reinit_firmware);

EXPORT_SYMBOL(orinoco_interrupt);

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
