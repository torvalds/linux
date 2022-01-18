// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MOSCHIP MCS7830 based (7730/7830/7832) USB 2.0 Ethernet Devices
 *
 * based on usbnet.c, asix.c and the vendor provided mcs7830 driver
 *
 * Copyright (C) 2010 Andreas Mohr <andi@lisas.de>
 * Copyright (C) 2006 Arnd Bergmann <arnd@arndb.de>
 * Copyright (C) 2003-2005 David Hollis <dhollis@davehollis.com>
 * Copyright (C) 2005 Phil Chang <pchang23@sbcglobal.net>
 * Copyright (c) 2002-2003 TiVo Inc.
 *
 * Definitions gathered from MOSCHIP, Data Sheet_7830DA.pdf (thanks!).
 *
 * 2010-12-19: add 7832 USB PID ("functionality same as MCS7830"),
 *             per active notification by manufacturer
 *
 * TODO:
 * - support HIF_REG_CONFIG_SLEEPMODE/HIF_REG_CONFIG_TXENABLE (via autopm?)
 * - implement ethtool_ops get_pauseparam/set_pauseparam
 *   via HIF_REG_PAUSE_THRESHOLD (>= revision C only!)
 * - implement get_eeprom/[set_eeprom]
 * - switch PHY on/off on ifup/ifdown (perhaps in usbnet.c, via MII)
 * - mcs7830_get_regs() handling is weird: for rev 2 we return 32 regs,
 *   can access only ~ 24, remaining user buffer is uninitialized garbage
 * - anything else?
 */

#include <linux/crc32.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/usbnet.h>

/* requests */
#define MCS7830_RD_BMREQ	(USB_DIR_IN  | USB_TYPE_VENDOR | \
				 USB_RECIP_DEVICE)
#define MCS7830_WR_BMREQ	(USB_DIR_OUT | USB_TYPE_VENDOR | \
				 USB_RECIP_DEVICE)
#define MCS7830_RD_BREQ		0x0E
#define MCS7830_WR_BREQ		0x0D

#define MCS7830_CTRL_TIMEOUT	1000
#define MCS7830_MAX_MCAST	64

#define MCS7830_VENDOR_ID	0x9710
#define MCS7832_PRODUCT_ID	0x7832
#define MCS7830_PRODUCT_ID	0x7830
#define MCS7730_PRODUCT_ID	0x7730

#define SITECOM_VENDOR_ID	0x0DF6
#define LN_030_PRODUCT_ID	0x0021

#define MCS7830_MII_ADVERTISE	(ADVERTISE_PAUSE_CAP | ADVERTISE_100FULL | \
				 ADVERTISE_100HALF | ADVERTISE_10FULL | \
				 ADVERTISE_10HALF | ADVERTISE_CSMA)

/* HIF_REG_XX corresponding index value */
enum {
	HIF_REG_MULTICAST_HASH			= 0x00,
	HIF_REG_PACKET_GAP1			= 0x08,
	HIF_REG_PACKET_GAP2			= 0x09,
	HIF_REG_PHY_DATA			= 0x0a,
	HIF_REG_PHY_CMD1			= 0x0c,
	   HIF_REG_PHY_CMD1_READ		= 0x40,
	   HIF_REG_PHY_CMD1_WRITE		= 0x20,
	   HIF_REG_PHY_CMD1_PHYADDR		= 0x01,
	HIF_REG_PHY_CMD2			= 0x0d,
	   HIF_REG_PHY_CMD2_PEND_FLAG_BIT	= 0x80,
	   HIF_REG_PHY_CMD2_READY_FLAG_BIT	= 0x40,
	HIF_REG_CONFIG				= 0x0e,
	/* hmm, spec sez: "R/W", "Except bit 3" (likely TXENABLE). */
	   HIF_REG_CONFIG_CFG			= 0x80,
	   HIF_REG_CONFIG_SPEED100		= 0x40,
	   HIF_REG_CONFIG_FULLDUPLEX_ENABLE	= 0x20,
	   HIF_REG_CONFIG_RXENABLE		= 0x10,
	   HIF_REG_CONFIG_TXENABLE		= 0x08,
	   HIF_REG_CONFIG_SLEEPMODE		= 0x04,
	   HIF_REG_CONFIG_ALLMULTICAST		= 0x02,
	   HIF_REG_CONFIG_PROMISCUOUS		= 0x01,
	HIF_REG_ETHERNET_ADDR			= 0x0f,
	HIF_REG_FRAME_DROP_COUNTER		= 0x15, /* 0..ff; reset: 0 */
	HIF_REG_PAUSE_THRESHOLD			= 0x16,
	   HIF_REG_PAUSE_THRESHOLD_DEFAULT	= 0,
};

/* Trailing status byte in Ethernet Rx frame */
enum {
	MCS7830_RX_SHORT_FRAME		= 0x01, /* < 64 bytes */
	MCS7830_RX_LENGTH_ERROR		= 0x02, /* framelen != Ethernet length field */
	MCS7830_RX_ALIGNMENT_ERROR	= 0x04, /* non-even number of nibbles */
	MCS7830_RX_CRC_ERROR		= 0x08,
	MCS7830_RX_LARGE_FRAME		= 0x10, /* > 1518 bytes */
	MCS7830_RX_FRAME_CORRECT	= 0x20, /* frame is correct */
	/* [7:6] reserved */
};

struct mcs7830_data {
	u8 multi_filter[8];
	u8 config;
};

static const char driver_name[] = "MOSCHIP usb-ethernet driver";

static int mcs7830_get_reg(struct usbnet *dev, u16 index, u16 size, void *data)
{
	return usbnet_read_cmd(dev, MCS7830_RD_BREQ, MCS7830_RD_BMREQ,
				0x0000, index, data, size);
}

static int mcs7830_set_reg(struct usbnet *dev, u16 index, u16 size, const void *data)
{
	return usbnet_write_cmd(dev, MCS7830_WR_BREQ, MCS7830_WR_BMREQ,
				0x0000, index, data, size);
}

static void mcs7830_set_reg_async(struct usbnet *dev, u16 index, u16 size, void *data)
{
	usbnet_write_cmd_async(dev, MCS7830_WR_BREQ, MCS7830_WR_BMREQ,
				0x0000, index, data, size);
}

static int mcs7830_hif_get_mac_address(struct usbnet *dev, unsigned char *addr)
{
	int ret = mcs7830_get_reg(dev, HIF_REG_ETHERNET_ADDR, ETH_ALEN, addr);
	if (ret < 0)
		return ret;
	return 0;
}

static int mcs7830_hif_set_mac_address(struct usbnet *dev,
				       const unsigned char *addr)
{
	int ret = mcs7830_set_reg(dev, HIF_REG_ETHERNET_ADDR, ETH_ALEN, addr);

	if (ret < 0)
		return ret;
	return 0;
}

static int mcs7830_set_mac_address(struct net_device *netdev, void *p)
{
	int ret;
	struct usbnet *dev = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (netif_running(netdev))
		return -EBUSY;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	ret = mcs7830_hif_set_mac_address(dev, addr->sa_data);

	if (ret < 0)
		return ret;

	/* it worked --> adopt it on netdev side */
	eth_hw_addr_set(netdev, addr->sa_data);

	return 0;
}

static int mcs7830_read_phy(struct usbnet *dev, u8 index)
{
	int ret;
	int i;
	__le16 val;

	u8 cmd[2] = {
		HIF_REG_PHY_CMD1_READ | HIF_REG_PHY_CMD1_PHYADDR,
		HIF_REG_PHY_CMD2_PEND_FLAG_BIT | index,
	};

	mutex_lock(&dev->phy_mutex);
	/* write the MII command */
	ret = mcs7830_set_reg(dev, HIF_REG_PHY_CMD1, 2, cmd);
	if (ret < 0)
		goto out;

	/* wait for the data to become valid, should be within < 1ms */
	for (i = 0; i < 10; i++) {
		ret = mcs7830_get_reg(dev, HIF_REG_PHY_CMD1, 2, cmd);
		if ((ret < 0) || (cmd[1] & HIF_REG_PHY_CMD2_READY_FLAG_BIT))
			break;
		ret = -EIO;
		msleep(1);
	}
	if (ret < 0)
		goto out;

	/* read actual register contents */
	ret = mcs7830_get_reg(dev, HIF_REG_PHY_DATA, 2, &val);
	if (ret < 0)
		goto out;
	ret = le16_to_cpu(val);
	dev_dbg(&dev->udev->dev, "read PHY reg %02x: %04x (%d tries)\n",
		index, val, i);
out:
	mutex_unlock(&dev->phy_mutex);
	return ret;
}

static int mcs7830_write_phy(struct usbnet *dev, u8 index, u16 val)
{
	int ret;
	int i;
	__le16 le_val;

	u8 cmd[2] = {
		HIF_REG_PHY_CMD1_WRITE | HIF_REG_PHY_CMD1_PHYADDR,
		HIF_REG_PHY_CMD2_PEND_FLAG_BIT | (index & 0x1F),
	};

	mutex_lock(&dev->phy_mutex);

	/* write the new register contents */
	le_val = cpu_to_le16(val);
	ret = mcs7830_set_reg(dev, HIF_REG_PHY_DATA, 2, &le_val);
	if (ret < 0)
		goto out;

	/* write the MII command */
	ret = mcs7830_set_reg(dev, HIF_REG_PHY_CMD1, 2, cmd);
	if (ret < 0)
		goto out;

	/* wait for the command to be accepted by the PHY */
	for (i = 0; i < 10; i++) {
		ret = mcs7830_get_reg(dev, HIF_REG_PHY_CMD1, 2, cmd);
		if ((ret < 0) || (cmd[1] & HIF_REG_PHY_CMD2_READY_FLAG_BIT))
			break;
		ret = -EIO;
		msleep(1);
	}
	if (ret < 0)
		goto out;

	ret = 0;
	dev_dbg(&dev->udev->dev, "write PHY reg %02x: %04x (%d tries)\n",
		index, val, i);
out:
	mutex_unlock(&dev->phy_mutex);
	return ret;
}

/*
 * This algorithm comes from the original mcs7830 version 1.4 driver,
 * not sure if it is needed.
 */
static int mcs7830_set_autoneg(struct usbnet *dev, int ptrUserPhyMode)
{
	int ret;
	/* Enable all media types */
	ret = mcs7830_write_phy(dev, MII_ADVERTISE, MCS7830_MII_ADVERTISE);

	/* First reset BMCR */
	if (!ret)
		ret = mcs7830_write_phy(dev, MII_BMCR, 0x0000);
	/* Enable Auto Neg */
	if (!ret)
		ret = mcs7830_write_phy(dev, MII_BMCR, BMCR_ANENABLE);
	/* Restart Auto Neg (Keep the Enable Auto Neg Bit Set) */
	if (!ret)
		ret = mcs7830_write_phy(dev, MII_BMCR,
				BMCR_ANENABLE | BMCR_ANRESTART	);
	return ret;
}


/*
 * if we can read register 22, the chip revision is C or higher
 */
static int mcs7830_get_rev(struct usbnet *dev)
{
	u8 dummy[2];
	int ret;
	ret = mcs7830_get_reg(dev, HIF_REG_FRAME_DROP_COUNTER, 2, dummy);
	if (ret > 0)
		return 2; /* Rev C or later */
	return 1; /* earlier revision */
}

/*
 * On rev. C we need to set the pause threshold
 */
static void mcs7830_rev_C_fixup(struct usbnet *dev)
{
	u8 pause_threshold = HIF_REG_PAUSE_THRESHOLD_DEFAULT;
	int retry;

	for (retry = 0; retry < 2; retry++) {
		if (mcs7830_get_rev(dev) == 2) {
			dev_info(&dev->udev->dev, "applying rev.C fixup\n");
			mcs7830_set_reg(dev, HIF_REG_PAUSE_THRESHOLD,
					1, &pause_threshold);
		}
		msleep(1);
	}
}

static int mcs7830_mdio_read(struct net_device *netdev, int phy_id,
			     int location)
{
	struct usbnet *dev = netdev_priv(netdev);
	return mcs7830_read_phy(dev, location);
}

static void mcs7830_mdio_write(struct net_device *netdev, int phy_id,
				int location, int val)
{
	struct usbnet *dev = netdev_priv(netdev);
	mcs7830_write_phy(dev, location, val);
}

static int mcs7830_ioctl(struct net_device *net, struct ifreq *rq, int cmd)
{
	struct usbnet *dev = netdev_priv(net);
	return generic_mii_ioctl(&dev->mii, if_mii(rq), cmd, NULL);
}

static inline struct mcs7830_data *mcs7830_get_data(struct usbnet *dev)
{
	return (struct mcs7830_data *)&dev->data;
}

static void mcs7830_hif_update_multicast_hash(struct usbnet *dev)
{
	struct mcs7830_data *data = mcs7830_get_data(dev);
	mcs7830_set_reg_async(dev, HIF_REG_MULTICAST_HASH,
				sizeof data->multi_filter,
				data->multi_filter);
}

static void mcs7830_hif_update_config(struct usbnet *dev)
{
	/* implementation specific to data->config
           (argument needs to be heap-based anyway - USB DMA!) */
	struct mcs7830_data *data = mcs7830_get_data(dev);
	mcs7830_set_reg_async(dev, HIF_REG_CONFIG, 1, &data->config);
}

static void mcs7830_data_set_multicast(struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);
	struct mcs7830_data *data = mcs7830_get_data(dev);

	memset(data->multi_filter, 0, sizeof data->multi_filter);

	data->config = HIF_REG_CONFIG_TXENABLE;

	/* this should not be needed, but it doesn't work otherwise */
	data->config |= HIF_REG_CONFIG_ALLMULTICAST;

	if (net->flags & IFF_PROMISC) {
		data->config |= HIF_REG_CONFIG_PROMISCUOUS;
	} else if (net->flags & IFF_ALLMULTI ||
		   netdev_mc_count(net) > MCS7830_MAX_MCAST) {
		data->config |= HIF_REG_CONFIG_ALLMULTICAST;
	} else if (netdev_mc_empty(net)) {
		/* just broadcast and directed */
	} else {
		/* We use the 20 byte dev->data
		 * for our 8 byte filter buffer
		 * to avoid allocating memory that
		 * is tricky to free later */
		struct netdev_hw_addr *ha;
		u32 crc_bits;

		/* Build the multicast hash filter. */
		netdev_for_each_mc_addr(ha, net) {
			crc_bits = ether_crc(ETH_ALEN, ha->addr) >> 26;
			data->multi_filter[crc_bits >> 3] |= 1 << (crc_bits & 7);
		}
	}
}

static int mcs7830_apply_base_config(struct usbnet *dev)
{
	int ret;

	/* re-configure known MAC (suspend case etc.) */
	ret = mcs7830_hif_set_mac_address(dev, dev->net->dev_addr);
	if (ret) {
		dev_info(&dev->udev->dev, "Cannot set MAC address\n");
		goto out;
	}

	/* Set up PHY */
	ret = mcs7830_set_autoneg(dev, 0);
	if (ret) {
		dev_info(&dev->udev->dev, "Cannot set autoneg\n");
		goto out;
	}

	mcs7830_hif_update_multicast_hash(dev);
	mcs7830_hif_update_config(dev);

	mcs7830_rev_C_fixup(dev);
	ret = 0;
out:
	return ret;
}

/* credits go to asix_set_multicast */
static void mcs7830_set_multicast(struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);

	mcs7830_data_set_multicast(net);

	mcs7830_hif_update_multicast_hash(dev);
	mcs7830_hif_update_config(dev);
}

static int mcs7830_get_regs_len(struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);

	switch (mcs7830_get_rev(dev)) {
	case 1:
		return 21;
	case 2:
		return 32;
	}
	return 0;
}

static void mcs7830_get_drvinfo(struct net_device *net, struct ethtool_drvinfo *drvinfo)
{
	usbnet_get_drvinfo(net, drvinfo);
}

static void mcs7830_get_regs(struct net_device *net, struct ethtool_regs *regs, void *data)
{
	struct usbnet *dev = netdev_priv(net);

	regs->version = mcs7830_get_rev(dev);
	mcs7830_get_reg(dev, 0, regs->len, data);
}

static const struct ethtool_ops mcs7830_ethtool_ops = {
	.get_drvinfo		= mcs7830_get_drvinfo,
	.get_regs_len		= mcs7830_get_regs_len,
	.get_regs		= mcs7830_get_regs,

	/* common usbnet calls */
	.get_link		= usbnet_get_link,
	.get_msglevel		= usbnet_get_msglevel,
	.set_msglevel		= usbnet_set_msglevel,
	.nway_reset		= usbnet_nway_reset,
	.get_link_ksettings	= usbnet_get_link_ksettings_mii,
	.set_link_ksettings	= usbnet_set_link_ksettings_mii,
};

static const struct net_device_ops mcs7830_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
	.ndo_start_xmit		= usbnet_start_xmit,
	.ndo_tx_timeout		= usbnet_tx_timeout,
	.ndo_change_mtu		= usbnet_change_mtu,
	.ndo_get_stats64	= dev_get_tstats64,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_eth_ioctl		= mcs7830_ioctl,
	.ndo_set_rx_mode	= mcs7830_set_multicast,
	.ndo_set_mac_address	= mcs7830_set_mac_address,
};

static int mcs7830_bind(struct usbnet *dev, struct usb_interface *udev)
{
	struct net_device *net = dev->net;
	u8 addr[ETH_ALEN];
	int ret;
	int retry;

	/* Initial startup: Gather MAC address setting from EEPROM */
	ret = -EINVAL;
	for (retry = 0; retry < 5 && ret; retry++)
		ret = mcs7830_hif_get_mac_address(dev, addr);
	if (ret) {
		dev_warn(&dev->udev->dev, "Cannot read MAC address\n");
		goto out;
	}
	eth_hw_addr_set(net, addr);

	mcs7830_data_set_multicast(net);

	ret = mcs7830_apply_base_config(dev);
	if (ret)
		goto out;

	net->ethtool_ops = &mcs7830_ethtool_ops;
	net->netdev_ops = &mcs7830_netdev_ops;

	/* reserve space for the status byte on rx */
	dev->rx_urb_size = ETH_FRAME_LEN + 1;

	dev->mii.mdio_read = mcs7830_mdio_read;
	dev->mii.mdio_write = mcs7830_mdio_write;
	dev->mii.dev = net;
	dev->mii.phy_id_mask = 0x3f;
	dev->mii.reg_num_mask = 0x1f;
	dev->mii.phy_id = *((u8 *) net->dev_addr + 1);

	ret = usbnet_get_endpoints(dev, udev);
out:
	return ret;
}

/* The chip always appends a status byte that we need to strip */
static int mcs7830_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	u8 status;

	/* This check is no longer done by usbnet */
	if (skb->len < dev->net->hard_header_len) {
		dev_err(&dev->udev->dev, "unexpected tiny rx frame\n");
		return 0;
	}

	skb_trim(skb, skb->len - 1);
	status = skb->data[skb->len];

	if (status != MCS7830_RX_FRAME_CORRECT) {
		dev_dbg(&dev->udev->dev, "rx fixup status %x\n", status);

		/* hmm, perhaps usbnet.c already sees a globally visible
		   frame error and increments rx_errors on its own already? */
		dev->net->stats.rx_errors++;

		if (status &	(MCS7830_RX_SHORT_FRAME
				|MCS7830_RX_LENGTH_ERROR
				|MCS7830_RX_LARGE_FRAME))
			dev->net->stats.rx_length_errors++;
		if (status & MCS7830_RX_ALIGNMENT_ERROR)
			dev->net->stats.rx_frame_errors++;
		if (status & MCS7830_RX_CRC_ERROR)
			dev->net->stats.rx_crc_errors++;
	}

	return skb->len > 0;
}

static void mcs7830_status(struct usbnet *dev, struct urb *urb)
{
	u8 *buf = urb->transfer_buffer;
	bool link, link_changed;

	if (urb->actual_length < 16)
		return;

	link = !(buf[1] == 0x20);
	link_changed = netif_carrier_ok(dev->net) != link;
	if (link_changed) {
		usbnet_link_change(dev, link, 0);
		netdev_dbg(dev->net, "Link Status is: %d\n", link);
	}
}

static const struct driver_info moschip_info = {
	.description	= "MOSCHIP 7830/7832/7730 usb-NET adapter",
	.bind		= mcs7830_bind,
	.rx_fixup	= mcs7830_rx_fixup,
	.flags		= FLAG_ETHER | FLAG_LINK_INTR,
	.status		= mcs7830_status,
	.in		= 1,
	.out		= 2,
};

static const struct driver_info sitecom_info = {
	.description    = "Sitecom LN-30 usb-NET adapter",
	.bind		= mcs7830_bind,
	.rx_fixup	= mcs7830_rx_fixup,
	.flags		= FLAG_ETHER | FLAG_LINK_INTR,
	.status		= mcs7830_status,
	.in		= 1,
	.out		= 2,
};

static const struct usb_device_id products[] = {
	{
		USB_DEVICE(MCS7830_VENDOR_ID, MCS7832_PRODUCT_ID),
		.driver_info = (unsigned long) &moschip_info,
	},
	{
		USB_DEVICE(MCS7830_VENDOR_ID, MCS7830_PRODUCT_ID),
		.driver_info = (unsigned long) &moschip_info,
	},
	{
		USB_DEVICE(MCS7830_VENDOR_ID, MCS7730_PRODUCT_ID),
		.driver_info = (unsigned long) &moschip_info,
	},
	{
		USB_DEVICE(SITECOM_VENDOR_ID, LN_030_PRODUCT_ID),
		.driver_info = (unsigned long) &sitecom_info,
	},
	{},
};
MODULE_DEVICE_TABLE(usb, products);

static int mcs7830_reset_resume (struct usb_interface *intf)
{
	/* YES, this function is successful enough that ethtool -d
           does show same output pre-/post-suspend */

	struct usbnet		*dev = usb_get_intfdata(intf);

	mcs7830_apply_base_config(dev);

	usbnet_resume(intf);

	return 0;
}

static struct usb_driver mcs7830_driver = {
	.name = driver_name,
	.id_table = products,
	.probe = usbnet_probe,
	.disconnect = usbnet_disconnect,
	.suspend = usbnet_suspend,
	.resume = usbnet_resume,
	.reset_resume = mcs7830_reset_resume,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(mcs7830_driver);

MODULE_DESCRIPTION("USB to network adapter MCS7830)");
MODULE_LICENSE("GPL");
