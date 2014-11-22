/*
 * Sample driver for HardMAC IEEE 802.15.4 devices
 *
 * Copyright (C) 2009 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Written by:
 * Dmitry Eremin-Solenikov <dmitry.baryshkov@siemens.com>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>

#include <net/af_ieee802154.h>
#include <net/ieee802154_netdev.h>
#include <net/ieee802154.h>
#include <net/nl802154.h>
#include <net/wpan-phy.h>

struct fakehard_priv {
	struct wpan_phy *phy;
};

static struct wpan_phy *fake_to_phy(const struct net_device *dev)
{
	struct fakehard_priv *priv = netdev_priv(dev);
	return priv->phy;
}

/**
 * fake_get_phy - Return a phy corresponding to this device.
 * @dev: The network device for which to return the wan-phy object
 *
 * This function returns a wpan-phy object corresponding to the passed
 * network device. Reference counter for wpan-phy object is incremented,
 * so when the wpan-phy isn't necessary, you should drop the reference
 * via @wpan_phy_put() call.
 */
static struct wpan_phy *fake_get_phy(const struct net_device *dev)
{
	struct wpan_phy *phy = fake_to_phy(dev);
	return to_phy(get_device(&phy->dev));
}

/**
 * fake_get_pan_id - Retrieve the PAN ID of the device.
 * @dev: The network device to retrieve the PAN of.
 *
 * Return the ID of the PAN from the PIB.
 */
static __le16 fake_get_pan_id(const struct net_device *dev)
{
	BUG_ON(dev->type != ARPHRD_IEEE802154);

	return cpu_to_le16(0xeba1);
}

/**
 * fake_get_short_addr - Retrieve the short address of the device.
 * @dev: The network device to retrieve the short address of.
 *
 * Returns the IEEE 802.15.4 short-form address cached for this
 * device. If the device has not yet had a short address assigned
 * then this should return 0xFFFF to indicate a lack of association.
 */
static __le16 fake_get_short_addr(const struct net_device *dev)
{
	BUG_ON(dev->type != ARPHRD_IEEE802154);

	return cpu_to_le16(0x1);
}

/**
 * fake_get_dsn - Retrieve the DSN of the device.
 * @dev: The network device to retrieve the DSN for.
 *
 * Returns the IEEE 802.15.4 DSN for the network device.
 * The DSN is the sequence number which will be added to each
 * packet or MAC command frame by the MAC during transmission.
 *
 * DSN means 'Data Sequence Number'.
 *
 * Note: This is in section 7.2.1.2 of the IEEE 802.15.4-2006
 *       document.
 */
static u8 fake_get_dsn(const struct net_device *dev)
{
	BUG_ON(dev->type != ARPHRD_IEEE802154);

	return 0x00; /* DSN are implemented in HW, so return just 0 */
}

/**
 * fake_assoc_req - Make an association request to the HW.
 * @dev: The network device which we are associating to a network.
 * @addr: The coordinator with which we wish to associate.
 * @channel: The channel on which to associate.
 * @cap: The capability information field to use in the association.
 *
 * Start an association with a coordinator. The coordinator's address
 * and PAN ID can be found in @addr.
 *
 * Note: This is in section 7.3.1 and 7.5.3.1 of the IEEE
 *       802.15.4-2006 document.
 */
static int fake_assoc_req(struct net_device *dev,
		struct ieee802154_addr *addr, u8 channel, u8 page, u8 cap)
{
	struct wpan_phy *phy = fake_to_phy(dev);

	mutex_lock(&phy->pib_lock);
	phy->current_channel = channel;
	phy->current_page = page;
	mutex_unlock(&phy->pib_lock);

	/* We simply emulate it here */
	return ieee802154_nl_assoc_confirm(dev, fake_get_short_addr(dev),
			IEEE802154_SUCCESS);
}

/**
 * fake_assoc_resp - Send an association response to a device.
 * @dev: The network device on which to send the response.
 * @addr: The address of the device to respond to.
 * @short_addr: The assigned short address for the device (if any).
 * @status: The result of the association request.
 *
 * Queue the association response of the coordinator to another
 * device's attempt to associate with the network which we
 * coordinate. This is then added to the indirect-send queue to be
 * transmitted to the end device when it polls for data.
 *
 * Note: This is in section 7.3.2 and 7.5.3.1 of the IEEE
 *       802.15.4-2006 document.
 */
static int fake_assoc_resp(struct net_device *dev,
		struct ieee802154_addr *addr, __le16 short_addr, u8 status)
{
	return 0;
}

/**
 * fake_disassoc_req - Disassociate a device from a network.
 * @dev: The network device on which we're disassociating a device.
 * @addr: The device to disassociate from the network.
 * @reason: The reason to give to the device for being disassociated.
 *
 * This sends a disassociation notification to the device being
 * disassociated from the network.
 *
 * Note: This is in section 7.5.3.2 of the IEEE 802.15.4-2006
 *       document, with the reason described in 7.3.3.2.
 */
static int fake_disassoc_req(struct net_device *dev,
		struct ieee802154_addr *addr, u8 reason)
{
	return ieee802154_nl_disassoc_confirm(dev, IEEE802154_SUCCESS);
}

/**
 * fake_start_req - Start an IEEE 802.15.4 PAN.
 * @dev: The network device on which to start the PAN.
 * @addr: The coordinator address to use when starting the PAN.
 * @channel: The channel on which to start the PAN.
 * @bcn_ord: Beacon order.
 * @sf_ord: Superframe order.
 * @pan_coord: Whether or not we are the PAN coordinator or just
 *             requesting a realignment perhaps?
 * @blx: Battery Life Extension feature bitfield.
 * @coord_realign: Something to realign something else.
 *
 * If pan_coord is non-zero then this starts a network with the
 * provided parameters, otherwise it attempts a coordinator
 * realignment of the stated network instead.
 *
 * Note: This is in section 7.5.2.3 of the IEEE 802.15.4-2006
 * document, with 7.3.8 describing coordinator realignment.
 */
static int fake_start_req(struct net_device *dev,
			  struct ieee802154_addr *addr, u8 channel, u8 page,
			  u8 bcn_ord, u8 sf_ord, u8 pan_coord, u8 blx,
			  u8 coord_realign)
{
	struct wpan_phy *phy = fake_to_phy(dev);

	mutex_lock(&phy->pib_lock);
	phy->current_channel = channel;
	phy->current_page = page;
	mutex_unlock(&phy->pib_lock);

	/* We don't emulate beacons here at all, so START should fail */
	ieee802154_nl_start_confirm(dev, IEEE802154_INVALID_PARAMETER);
	return 0;
}

/**
 * fake_scan_req - Start a channel scan.
 * @dev: The network device on which to perform a channel scan.
 * @type: The type of scan to perform.
 * @channels: The channel bitmask to scan.
 * @duration: How long to spend on each channel.
 *
 * This starts either a passive (energy) scan or an active (PAN) scan
 * on the channels indicated in the @channels bitmask. The duration of
 * the scan is measured in terms of superframe duration. Specifically,
 * the scan will spend aBaseSuperFrameDuration * ((2^n) + 1) on each
 * channel.
 *
 * Note: This is in section 7.5.2.1 of the IEEE 802.15.4-2006 document.
 */
static int fake_scan_req(struct net_device *dev, u8 type, u32 channels,
		u8 page, u8 duration)
{
	u8 edl[27] = {};
	return ieee802154_nl_scan_confirm(dev, IEEE802154_SUCCESS, type,
			channels, page,
			type == IEEE802154_MAC_SCAN_ED ? edl : NULL);
}

static struct ieee802154_mlme_ops fake_mlme = {
	.assoc_req = fake_assoc_req,
	.assoc_resp = fake_assoc_resp,
	.disassoc_req = fake_disassoc_req,
	.start_req = fake_start_req,
	.scan_req = fake_scan_req,

	.get_phy = fake_get_phy,

	.get_pan_id = fake_get_pan_id,
	.get_short_addr = fake_get_short_addr,
	.get_dsn = fake_get_dsn,
};

static int ieee802154_fake_open(struct net_device *dev)
{
	netif_start_queue(dev);
	return 0;
}

static int ieee802154_fake_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

static netdev_tx_t ieee802154_fake_xmit(struct sk_buff *skb,
					      struct net_device *dev)
{
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	/* FIXME: do hardware work here ... */

	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}


static int ieee802154_fake_ioctl(struct net_device *dev, struct ifreq *ifr,
		int cmd)
{
	struct sockaddr_ieee802154 *sa =
		(struct sockaddr_ieee802154 *)&ifr->ifr_addr;
	u16 pan_id, short_addr;

	switch (cmd) {
	case SIOCGIFADDR:
		/* FIXME: fixed here, get from device IRL */
		pan_id = le16_to_cpu(fake_get_pan_id(dev));
		short_addr = le16_to_cpu(fake_get_short_addr(dev));
		if (pan_id == IEEE802154_PANID_BROADCAST ||
		    short_addr == IEEE802154_ADDR_BROADCAST)
			return -EADDRNOTAVAIL;

		sa->family = AF_IEEE802154;
		sa->addr.addr_type = IEEE802154_ADDR_SHORT;
		sa->addr.pan_id = pan_id;
		sa->addr.short_addr = short_addr;
		return 0;
	}
	return -ENOIOCTLCMD;
}

static int ieee802154_fake_mac_addr(struct net_device *dev, void *p)
{
	return -EBUSY; /* HW address is built into the device */
}

static const struct net_device_ops fake_ops = {
	.ndo_open		= ieee802154_fake_open,
	.ndo_stop		= ieee802154_fake_close,
	.ndo_start_xmit		= ieee802154_fake_xmit,
	.ndo_do_ioctl		= ieee802154_fake_ioctl,
	.ndo_set_mac_address	= ieee802154_fake_mac_addr,
};

static void ieee802154_fake_destruct(struct net_device *dev)
{
	struct wpan_phy *phy = fake_to_phy(dev);

	wpan_phy_unregister(phy);
	free_netdev(dev);
	wpan_phy_free(phy);
}

static void ieee802154_fake_setup(struct net_device *dev)
{
	dev->addr_len		= IEEE802154_ADDR_LEN;
	memset(dev->broadcast, 0xff, IEEE802154_ADDR_LEN);
	dev->features		= NETIF_F_HW_CSUM;
	dev->needed_tailroom	= 2; /* FCS */
	dev->mtu		= 127;
	dev->tx_queue_len	= 10;
	dev->type		= ARPHRD_IEEE802154;
	dev->flags		= IFF_NOARP | IFF_BROADCAST;
	dev->watchdog_timeo	= 0;
	dev->destructor		= ieee802154_fake_destruct;
}


static int ieee802154fake_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct fakehard_priv *priv;
	struct wpan_phy *phy = wpan_phy_alloc(0);
	int err;

	if (!phy)
		return -ENOMEM;

	dev = alloc_netdev(sizeof(struct fakehard_priv), "hardwpan%d",
			   NET_NAME_UNKNOWN, ieee802154_fake_setup);
	if (!dev) {
		wpan_phy_free(phy);
		return -ENOMEM;
	}

	memcpy(dev->dev_addr, "\xba\xbe\xca\xfe\xde\xad\xbe\xef",
			dev->addr_len);

	/*
	 * For now we'd like to emulate 2.4 GHz-only device,
	 * both O-QPSK and CSS
	 */
	/* 2.4 GHz O-QPSK 802.15.4-2003 */
	phy->channels_supported[0] |= 0x7FFF800;
	/* 2.4 GHz CSS 802.15.4a-2007 */
	phy->channels_supported[3] |= 0x3fff;

	phy->transmit_power = 0xbf;

	dev->netdev_ops = &fake_ops;
	dev->ml_priv = &fake_mlme;

	priv = netdev_priv(dev);
	priv->phy = phy;

	wpan_phy_set_dev(phy, &pdev->dev);
	SET_NETDEV_DEV(dev, &phy->dev);

	platform_set_drvdata(pdev, dev);

	err = wpan_phy_register(phy);
	if (err)
		goto err_phy_reg;

	err = register_netdev(dev);
	if (err)
		goto err_netdev_reg;

	dev_info(&pdev->dev, "Added ieee802154 HardMAC hardware\n");
	return 0;

err_netdev_reg:
	wpan_phy_unregister(phy);
err_phy_reg:
	free_netdev(dev);
	wpan_phy_free(phy);
	return err;
}

static int ieee802154fake_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	unregister_netdev(dev);
	return 0;
}

static struct platform_device *ieee802154fake_dev;

static struct platform_driver ieee802154fake_driver = {
	.probe = ieee802154fake_probe,
	.remove = ieee802154fake_remove,
	.driver = {
			.name = "ieee802154hardmac",
			.owner = THIS_MODULE,
	},
};

static __init int fake_init(void)
{
	ieee802154fake_dev = platform_device_register_simple(
			"ieee802154hardmac", -1, NULL, 0);
	return platform_driver_register(&ieee802154fake_driver);
}

static __exit void fake_exit(void)
{
	platform_driver_unregister(&ieee802154fake_driver);
	platform_device_unregister(ieee802154fake_dev);
}

module_init(fake_init);
module_exit(fake_exit);
MODULE_LICENSE("GPL");
