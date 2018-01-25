/*
 * ASIX AX8817X based USB 2.0 Ethernet Devices
 * Copyright (C) 2003-2006 David Hollis <dhollis@davehollis.com>
 * Copyright (C) 2005 Phil Chang <pchang23@sbcglobal.net>
 * Copyright (C) 2006 James Painter <jamie.painter@iname.com>
 * Copyright (c) 2002-2003 TiVo Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "asix.h"

#define PHY_MODE_MARVELL	0x0000
#define MII_MARVELL_LED_CTRL	0x0018
#define MII_MARVELL_STATUS	0x001b
#define MII_MARVELL_CTRL	0x0014

#define MARVELL_LED_MANUAL	0x0019

#define MARVELL_STATUS_HWCFG	0x0004

#define MARVELL_CTRL_TXDELAY	0x0002
#define MARVELL_CTRL_RXDELAY	0x0080

#define	PHY_MODE_RTL8211CL	0x000C

#define AX88772A_PHY14H		0x14
#define AX88772A_PHY14H_DEFAULT 0x442C

#define AX88772A_PHY15H		0x15
#define AX88772A_PHY15H_DEFAULT 0x03C8

#define AX88772A_PHY16H		0x16
#define AX88772A_PHY16H_DEFAULT 0x4044

struct ax88172_int_data {
	__le16 res1;
	u8 link;
	__le16 res2;
	u8 status;
	__le16 res3;
} __packed;

static void asix_status(struct usbnet *dev, struct urb *urb)
{
	struct ax88172_int_data *event;
	int link;

	if (urb->actual_length < 8)
		return;

	event = urb->transfer_buffer;
	link = event->link & 0x01;
	if (netif_carrier_ok(dev->net) != link) {
		usbnet_link_change(dev, link, 1);
		netdev_dbg(dev->net, "Link Status is: %d\n", link);
	}
}

static void asix_set_netdev_dev_addr(struct usbnet *dev, u8 *addr)
{
	if (is_valid_ether_addr(addr)) {
		memcpy(dev->net->dev_addr, addr, ETH_ALEN);
	} else {
		netdev_info(dev->net, "invalid hw address, using random\n");
		eth_hw_addr_random(dev->net);
	}
}

/* Get the PHY Identifier from the PHYSID1 & PHYSID2 MII registers */
static u32 asix_get_phyid(struct usbnet *dev)
{
	int phy_reg;
	u32 phy_id;
	int i;

	/* Poll for the rare case the FW or phy isn't ready yet.  */
	for (i = 0; i < 100; i++) {
		phy_reg = asix_mdio_read(dev->net, dev->mii.phy_id, MII_PHYSID1);
		if (phy_reg < 0)
			return 0;
		if (phy_reg != 0 && phy_reg != 0xFFFF)
			break;
		mdelay(1);
	}

	if (phy_reg <= 0 || phy_reg == 0xFFFF)
		return 0;

	phy_id = (phy_reg & 0xffff) << 16;

	phy_reg = asix_mdio_read(dev->net, dev->mii.phy_id, MII_PHYSID2);
	if (phy_reg < 0)
		return 0;

	phy_id |= (phy_reg & 0xffff);

	return phy_id;
}

static u32 asix_get_link(struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);

	return mii_link_ok(&dev->mii);
}

static int asix_ioctl (struct net_device *net, struct ifreq *rq, int cmd)
{
	struct usbnet *dev = netdev_priv(net);

	return generic_mii_ioctl(&dev->mii, if_mii(rq), cmd, NULL);
}

/* We need to override some ethtool_ops so we require our
   own structure so we don't interfere with other usbnet
   devices that may be connected at the same time. */
static const struct ethtool_ops ax88172_ethtool_ops = {
	.get_drvinfo		= asix_get_drvinfo,
	.get_link		= asix_get_link,
	.get_msglevel		= usbnet_get_msglevel,
	.set_msglevel		= usbnet_set_msglevel,
	.get_wol		= asix_get_wol,
	.set_wol		= asix_set_wol,
	.get_eeprom_len		= asix_get_eeprom_len,
	.get_eeprom		= asix_get_eeprom,
	.set_eeprom		= asix_set_eeprom,
	.nway_reset		= usbnet_nway_reset,
	.get_link_ksettings	= usbnet_get_link_ksettings,
	.set_link_ksettings	= usbnet_set_link_ksettings,
};

static void ax88172_set_multicast(struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);
	struct asix_data *data = (struct asix_data *)&dev->data;
	u8 rx_ctl = 0x8c;

	if (net->flags & IFF_PROMISC) {
		rx_ctl |= 0x01;
	} else if (net->flags & IFF_ALLMULTI ||
		   netdev_mc_count(net) > AX_MAX_MCAST) {
		rx_ctl |= 0x02;
	} else if (netdev_mc_empty(net)) {
		/* just broadcast and directed */
	} else {
		/* We use the 20 byte dev->data
		 * for our 8 byte filter buffer
		 * to avoid allocating memory that
		 * is tricky to free later */
		struct netdev_hw_addr *ha;
		u32 crc_bits;

		memset(data->multi_filter, 0, AX_MCAST_FILTER_SIZE);

		/* Build the multicast hash filter. */
		netdev_for_each_mc_addr(ha, net) {
			crc_bits = ether_crc(ETH_ALEN, ha->addr) >> 26;
			data->multi_filter[crc_bits >> 3] |=
			    1 << (crc_bits & 7);
		}

		asix_write_cmd_async(dev, AX_CMD_WRITE_MULTI_FILTER, 0, 0,
				   AX_MCAST_FILTER_SIZE, data->multi_filter);

		rx_ctl |= 0x10;
	}

	asix_write_cmd_async(dev, AX_CMD_WRITE_RX_CTL, rx_ctl, 0, 0, NULL);
}

static int ax88172_link_reset(struct usbnet *dev)
{
	u8 mode;
	struct ethtool_cmd ecmd = { .cmd = ETHTOOL_GSET };

	mii_check_media(&dev->mii, 1, 1);
	mii_ethtool_gset(&dev->mii, &ecmd);
	mode = AX88172_MEDIUM_DEFAULT;

	if (ecmd.duplex != DUPLEX_FULL)
		mode |= ~AX88172_MEDIUM_FD;

	netdev_dbg(dev->net, "ax88172_link_reset() speed: %u duplex: %d setting mode to 0x%04x\n",
		   ethtool_cmd_speed(&ecmd), ecmd.duplex, mode);

	asix_write_medium_mode(dev, mode, 0);

	return 0;
}

static const struct net_device_ops ax88172_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
	.ndo_start_xmit		= usbnet_start_xmit,
	.ndo_tx_timeout		= usbnet_tx_timeout,
	.ndo_change_mtu		= usbnet_change_mtu,
	.ndo_get_stats64	= usbnet_get_stats64,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= asix_ioctl,
	.ndo_set_rx_mode	= ax88172_set_multicast,
};

static void asix_phy_reset(struct usbnet *dev, unsigned int reset_bits)
{
	unsigned int timeout = 5000;

	asix_mdio_write(dev->net, dev->mii.phy_id, MII_BMCR, reset_bits);

	/* give phy_id a chance to process reset */
	udelay(500);

	/* See IEEE 802.3 "22.2.4.1.1 Reset": 500ms max */
	while (timeout--) {
		if (asix_mdio_read(dev->net, dev->mii.phy_id, MII_BMCR)
							& BMCR_RESET)
			udelay(100);
		else
			return;
	}

	netdev_err(dev->net, "BMCR_RESET timeout on phy_id %d\n",
		   dev->mii.phy_id);
}

static int ax88172_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int ret = 0;
	u8 buf[ETH_ALEN];
	int i;
	unsigned long gpio_bits = dev->driver_info->data;

	usbnet_get_endpoints(dev,intf);

	/* Toggle the GPIOs in a manufacturer/model specific way */
	for (i = 2; i >= 0; i--) {
		ret = asix_write_cmd(dev, AX_CMD_WRITE_GPIOS,
				(gpio_bits >> (i * 8)) & 0xff, 0, 0, NULL, 0);
		if (ret < 0)
			goto out;
		msleep(5);
	}

	ret = asix_write_rx_ctl(dev, 0x80, 0);
	if (ret < 0)
		goto out;

	/* Get the MAC address */
	ret = asix_read_cmd(dev, AX88172_CMD_READ_NODE_ID,
			    0, 0, ETH_ALEN, buf, 0);
	if (ret < 0) {
		netdev_dbg(dev->net, "read AX_CMD_READ_NODE_ID failed: %d\n",
			   ret);
		goto out;
	}

	asix_set_netdev_dev_addr(dev, buf);

	/* Initialize MII structure */
	dev->mii.dev = dev->net;
	dev->mii.mdio_read = asix_mdio_read;
	dev->mii.mdio_write = asix_mdio_write;
	dev->mii.phy_id_mask = 0x3f;
	dev->mii.reg_num_mask = 0x1f;
	dev->mii.phy_id = asix_get_phy_addr(dev);

	dev->net->netdev_ops = &ax88172_netdev_ops;
	dev->net->ethtool_ops = &ax88172_ethtool_ops;
	dev->net->needed_headroom = 4; /* cf asix_tx_fixup() */
	dev->net->needed_tailroom = 4; /* cf asix_tx_fixup() */

	asix_phy_reset(dev, BMCR_RESET);
	asix_mdio_write(dev->net, dev->mii.phy_id, MII_ADVERTISE,
		ADVERTISE_ALL | ADVERTISE_CSMA | ADVERTISE_PAUSE_CAP);
	mii_nway_restart(&dev->mii);

	return 0;

out:
	return ret;
}

static const struct ethtool_ops ax88772_ethtool_ops = {
	.get_drvinfo		= asix_get_drvinfo,
	.get_link		= asix_get_link,
	.get_msglevel		= usbnet_get_msglevel,
	.set_msglevel		= usbnet_set_msglevel,
	.get_wol		= asix_get_wol,
	.set_wol		= asix_set_wol,
	.get_eeprom_len		= asix_get_eeprom_len,
	.get_eeprom		= asix_get_eeprom,
	.set_eeprom		= asix_set_eeprom,
	.nway_reset		= usbnet_nway_reset,
	.get_link_ksettings	= usbnet_get_link_ksettings,
	.set_link_ksettings	= usbnet_set_link_ksettings,
};

static int ax88772_link_reset(struct usbnet *dev)
{
	u16 mode;
	struct ethtool_cmd ecmd = { .cmd = ETHTOOL_GSET };

	mii_check_media(&dev->mii, 1, 1);
	mii_ethtool_gset(&dev->mii, &ecmd);
	mode = AX88772_MEDIUM_DEFAULT;

	if (ethtool_cmd_speed(&ecmd) != SPEED_100)
		mode &= ~AX_MEDIUM_PS;

	if (ecmd.duplex != DUPLEX_FULL)
		mode &= ~AX_MEDIUM_FD;

	netdev_dbg(dev->net, "ax88772_link_reset() speed: %u duplex: %d setting mode to 0x%04x\n",
		   ethtool_cmd_speed(&ecmd), ecmd.duplex, mode);

	asix_write_medium_mode(dev, mode, 0);

	return 0;
}

static int ax88772_reset(struct usbnet *dev)
{
	struct asix_data *data = (struct asix_data *)&dev->data;
	int ret;

	/* Rewrite MAC address */
	ether_addr_copy(data->mac_addr, dev->net->dev_addr);
	ret = asix_write_cmd(dev, AX_CMD_WRITE_NODE_ID, 0, 0,
			     ETH_ALEN, data->mac_addr, 0);
	if (ret < 0)
		goto out;

	/* Set RX_CTL to default values with 2k buffer, and enable cactus */
	ret = asix_write_rx_ctl(dev, AX_DEFAULT_RX_CTL, 0);
	if (ret < 0)
		goto out;

	ret = asix_write_medium_mode(dev, AX88772_MEDIUM_DEFAULT, 0);
	if (ret < 0)
		goto out;

	return 0;

out:
	return ret;
}

static int ax88772_hw_reset(struct usbnet *dev, int in_pm)
{
	struct asix_data *data = (struct asix_data *)&dev->data;
	int ret, embd_phy;
	u16 rx_ctl;

	ret = asix_write_gpio(dev, AX_GPIO_RSE | AX_GPIO_GPO_2 |
			      AX_GPIO_GPO2EN, 5, in_pm);
	if (ret < 0)
		goto out;

	embd_phy = ((dev->mii.phy_id & 0x1f) == 0x10 ? 1 : 0);

	ret = asix_write_cmd(dev, AX_CMD_SW_PHY_SELECT, embd_phy,
			     0, 0, NULL, in_pm);
	if (ret < 0) {
		netdev_dbg(dev->net, "Select PHY #1 failed: %d\n", ret);
		goto out;
	}

	if (embd_phy) {
		ret = asix_sw_reset(dev, AX_SWRESET_IPPD, in_pm);
		if (ret < 0)
			goto out;

		usleep_range(10000, 11000);

		ret = asix_sw_reset(dev, AX_SWRESET_CLEAR, in_pm);
		if (ret < 0)
			goto out;

		msleep(60);

		ret = asix_sw_reset(dev, AX_SWRESET_IPRL | AX_SWRESET_PRL,
				    in_pm);
		if (ret < 0)
			goto out;
	} else {
		ret = asix_sw_reset(dev, AX_SWRESET_IPPD | AX_SWRESET_PRL,
				    in_pm);
		if (ret < 0)
			goto out;
	}

	msleep(150);

	if (in_pm && (!asix_mdio_read_nopm(dev->net, dev->mii.phy_id,
					   MII_PHYSID1))){
		ret = -EIO;
		goto out;
	}

	ret = asix_write_rx_ctl(dev, AX_DEFAULT_RX_CTL, in_pm);
	if (ret < 0)
		goto out;

	ret = asix_write_medium_mode(dev, AX88772_MEDIUM_DEFAULT, in_pm);
	if (ret < 0)
		goto out;

	ret = asix_write_cmd(dev, AX_CMD_WRITE_IPG0,
			     AX88772_IPG0_DEFAULT | AX88772_IPG1_DEFAULT,
			     AX88772_IPG2_DEFAULT, 0, NULL, in_pm);
	if (ret < 0) {
		netdev_dbg(dev->net, "Write IPG,IPG1,IPG2 failed: %d\n", ret);
		goto out;
	}

	/* Rewrite MAC address */
	ether_addr_copy(data->mac_addr, dev->net->dev_addr);
	ret = asix_write_cmd(dev, AX_CMD_WRITE_NODE_ID, 0, 0,
			     ETH_ALEN, data->mac_addr, in_pm);
	if (ret < 0)
		goto out;

	/* Set RX_CTL to default values with 2k buffer, and enable cactus */
	ret = asix_write_rx_ctl(dev, AX_DEFAULT_RX_CTL, in_pm);
	if (ret < 0)
		goto out;

	rx_ctl = asix_read_rx_ctl(dev, in_pm);
	netdev_dbg(dev->net, "RX_CTL is 0x%04x after all initializations\n",
		   rx_ctl);

	rx_ctl = asix_read_medium_status(dev, in_pm);
	netdev_dbg(dev->net,
		   "Medium Status is 0x%04x after all initializations\n",
		   rx_ctl);

	return 0;

out:
	return ret;
}

static int ax88772a_hw_reset(struct usbnet *dev, int in_pm)
{
	struct asix_data *data = (struct asix_data *)&dev->data;
	int ret, embd_phy;
	u16 rx_ctl, phy14h, phy15h, phy16h;
	u8 chipcode = 0;

	ret = asix_write_gpio(dev, AX_GPIO_RSE, 5, in_pm);
	if (ret < 0)
		goto out;

	embd_phy = ((dev->mii.phy_id & 0x1f) == 0x10 ? 1 : 0);

	ret = asix_write_cmd(dev, AX_CMD_SW_PHY_SELECT, embd_phy |
			     AX_PHYSEL_SSEN, 0, 0, NULL, in_pm);
	if (ret < 0) {
		netdev_dbg(dev->net, "Select PHY #1 failed: %d\n", ret);
		goto out;
	}
	usleep_range(10000, 11000);

	ret = asix_sw_reset(dev, AX_SWRESET_IPPD | AX_SWRESET_IPRL, in_pm);
	if (ret < 0)
		goto out;

	usleep_range(10000, 11000);

	ret = asix_sw_reset(dev, AX_SWRESET_IPRL, in_pm);
	if (ret < 0)
		goto out;

	msleep(160);

	ret = asix_sw_reset(dev, AX_SWRESET_CLEAR, in_pm);
	if (ret < 0)
		goto out;

	ret = asix_sw_reset(dev, AX_SWRESET_IPRL, in_pm);
	if (ret < 0)
		goto out;

	msleep(200);

	if (in_pm && (!asix_mdio_read_nopm(dev->net, dev->mii.phy_id,
					   MII_PHYSID1))) {
		ret = -1;
		goto out;
	}

	ret = asix_read_cmd(dev, AX_CMD_STATMNGSTS_REG, 0,
			    0, 1, &chipcode, in_pm);
	if (ret < 0)
		goto out;

	if ((chipcode & AX_CHIPCODE_MASK) == AX_AX88772B_CHIPCODE) {
		ret = asix_write_cmd(dev, AX_QCTCTRL, 0x8000, 0x8001,
				     0, NULL, in_pm);
		if (ret < 0) {
			netdev_dbg(dev->net, "Write BQ setting failed: %d\n",
				   ret);
			goto out;
		}
	} else if ((chipcode & AX_CHIPCODE_MASK) == AX_AX88772A_CHIPCODE) {
		/* Check if the PHY registers have default settings */
		phy14h = asix_mdio_read_nopm(dev->net, dev->mii.phy_id,
					     AX88772A_PHY14H);
		phy15h = asix_mdio_read_nopm(dev->net, dev->mii.phy_id,
					     AX88772A_PHY15H);
		phy16h = asix_mdio_read_nopm(dev->net, dev->mii.phy_id,
					     AX88772A_PHY16H);

		netdev_dbg(dev->net,
			   "772a_hw_reset: MR20=0x%x MR21=0x%x MR22=0x%x\n",
			   phy14h, phy15h, phy16h);

		/* Restore PHY registers default setting if not */
		if (phy14h != AX88772A_PHY14H_DEFAULT)
			asix_mdio_write_nopm(dev->net, dev->mii.phy_id,
					     AX88772A_PHY14H,
					     AX88772A_PHY14H_DEFAULT);
		if (phy15h != AX88772A_PHY15H_DEFAULT)
			asix_mdio_write_nopm(dev->net, dev->mii.phy_id,
					     AX88772A_PHY15H,
					     AX88772A_PHY15H_DEFAULT);
		if (phy16h != AX88772A_PHY16H_DEFAULT)
			asix_mdio_write_nopm(dev->net, dev->mii.phy_id,
					     AX88772A_PHY16H,
					     AX88772A_PHY16H_DEFAULT);
	}

	ret = asix_write_cmd(dev, AX_CMD_WRITE_IPG0,
				AX88772_IPG0_DEFAULT | AX88772_IPG1_DEFAULT,
				AX88772_IPG2_DEFAULT, 0, NULL, in_pm);
	if (ret < 0) {
		netdev_dbg(dev->net, "Write IPG,IPG1,IPG2 failed: %d\n", ret);
		goto out;
	}

	/* Rewrite MAC address */
	memcpy(data->mac_addr, dev->net->dev_addr, ETH_ALEN);
	ret = asix_write_cmd(dev, AX_CMD_WRITE_NODE_ID, 0, 0, ETH_ALEN,
							data->mac_addr, in_pm);
	if (ret < 0)
		goto out;

	/* Set RX_CTL to default values with 2k buffer, and enable cactus */
	ret = asix_write_rx_ctl(dev, AX_DEFAULT_RX_CTL, in_pm);
	if (ret < 0)
		goto out;

	ret = asix_write_medium_mode(dev, AX88772_MEDIUM_DEFAULT, in_pm);
	if (ret < 0)
		return ret;

	/* Set RX_CTL to default values with 2k buffer, and enable cactus */
	ret = asix_write_rx_ctl(dev, AX_DEFAULT_RX_CTL, in_pm);
	if (ret < 0)
		goto out;

	rx_ctl = asix_read_rx_ctl(dev, in_pm);
	netdev_dbg(dev->net, "RX_CTL is 0x%04x after all initializations\n",
		   rx_ctl);

	rx_ctl = asix_read_medium_status(dev, in_pm);
	netdev_dbg(dev->net,
		   "Medium Status is 0x%04x after all initializations\n",
		   rx_ctl);

	return 0;

out:
	return ret;
}

static const struct net_device_ops ax88772_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
	.ndo_start_xmit		= usbnet_start_xmit,
	.ndo_tx_timeout		= usbnet_tx_timeout,
	.ndo_change_mtu		= usbnet_change_mtu,
	.ndo_get_stats64	= usbnet_get_stats64,
	.ndo_set_mac_address 	= asix_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= asix_ioctl,
	.ndo_set_rx_mode        = asix_set_multicast,
};

static void ax88772_suspend(struct usbnet *dev)
{
	struct asix_common_private *priv = dev->driver_priv;
	u16 medium;

	/* Stop MAC operation */
	medium = asix_read_medium_status(dev, 1);
	medium &= ~AX_MEDIUM_RE;
	asix_write_medium_mode(dev, medium, 1);

	netdev_dbg(dev->net, "ax88772_suspend: medium=0x%04x\n",
		   asix_read_medium_status(dev, 1));

	/* Preserve BMCR for restoring */
	priv->presvd_phy_bmcr =
		asix_mdio_read_nopm(dev->net, dev->mii.phy_id, MII_BMCR);

	/* Preserve ANAR for restoring */
	priv->presvd_phy_advertise =
		asix_mdio_read_nopm(dev->net, dev->mii.phy_id, MII_ADVERTISE);
}

static int asix_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usbnet *dev = usb_get_intfdata(intf);
	struct asix_common_private *priv = dev->driver_priv;

	if (priv && priv->suspend)
		priv->suspend(dev);

	return usbnet_suspend(intf, message);
}

static void ax88772_restore_phy(struct usbnet *dev)
{
	struct asix_common_private *priv = dev->driver_priv;

	if (priv->presvd_phy_advertise) {
		/* Restore Advertisement control reg */
		asix_mdio_write_nopm(dev->net, dev->mii.phy_id, MII_ADVERTISE,
				     priv->presvd_phy_advertise);

		/* Restore BMCR */
		asix_mdio_write_nopm(dev->net, dev->mii.phy_id, MII_BMCR,
				     priv->presvd_phy_bmcr);

		mii_nway_restart(&dev->mii);
		priv->presvd_phy_advertise = 0;
		priv->presvd_phy_bmcr = 0;
	}
}

static void ax88772_resume(struct usbnet *dev)
{
	int i;

	for (i = 0; i < 3; i++)
		if (!ax88772_hw_reset(dev, 1))
			break;
	ax88772_restore_phy(dev);
}

static void ax88772a_resume(struct usbnet *dev)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (!ax88772a_hw_reset(dev, 1))
			break;
	}

	ax88772_restore_phy(dev);
}

static int asix_resume(struct usb_interface *intf)
{
	struct usbnet *dev = usb_get_intfdata(intf);
	struct asix_common_private *priv = dev->driver_priv;

	if (priv && priv->resume)
		priv->resume(dev);

	return usbnet_resume(intf);
}

static int ax88772_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int ret, i;
	u8 buf[ETH_ALEN], chipcode = 0;
	u32 phyid;
	struct asix_common_private *priv;

	usbnet_get_endpoints(dev,intf);

	/* Get the MAC address */
	if (dev->driver_info->data & FLAG_EEPROM_MAC) {
		for (i = 0; i < (ETH_ALEN >> 1); i++) {
			ret = asix_read_cmd(dev, AX_CMD_READ_EEPROM, 0x04 + i,
					    0, 2, buf + i * 2, 0);
			if (ret < 0)
				break;
		}
	} else {
		ret = asix_read_cmd(dev, AX_CMD_READ_NODE_ID,
				0, 0, ETH_ALEN, buf, 0);
	}

	if (ret < 0) {
		netdev_dbg(dev->net, "Failed to read MAC address: %d\n", ret);
		return ret;
	}

	asix_set_netdev_dev_addr(dev, buf);

	/* Initialize MII structure */
	dev->mii.dev = dev->net;
	dev->mii.mdio_read = asix_mdio_read;
	dev->mii.mdio_write = asix_mdio_write;
	dev->mii.phy_id_mask = 0x1f;
	dev->mii.reg_num_mask = 0x1f;
	dev->mii.phy_id = asix_get_phy_addr(dev);

	dev->net->netdev_ops = &ax88772_netdev_ops;
	dev->net->ethtool_ops = &ax88772_ethtool_ops;
	dev->net->needed_headroom = 4; /* cf asix_tx_fixup() */
	dev->net->needed_tailroom = 4; /* cf asix_tx_fixup() */

	asix_read_cmd(dev, AX_CMD_STATMNGSTS_REG, 0, 0, 1, &chipcode, 0);
	chipcode &= AX_CHIPCODE_MASK;

	(chipcode == AX_AX88772_CHIPCODE) ? ax88772_hw_reset(dev, 0) :
					    ax88772a_hw_reset(dev, 0);

	/* Read PHYID register *AFTER* the PHY was reset properly */
	phyid = asix_get_phyid(dev);
	netdev_dbg(dev->net, "PHYID=0x%08x\n", phyid);

	/* Asix framing packs multiple eth frames into a 2K usb bulk transfer */
	if (dev->driver_info->flags & FLAG_FRAMING_AX) {
		/* hard_mtu  is still the default - the device does not support
		   jumbo eth frames */
		dev->rx_urb_size = 2048;
	}

	dev->driver_priv = kzalloc(sizeof(struct asix_common_private), GFP_KERNEL);
	if (!dev->driver_priv)
		return -ENOMEM;

	priv = dev->driver_priv;

	priv->presvd_phy_bmcr = 0;
	priv->presvd_phy_advertise = 0;
	if (chipcode == AX_AX88772_CHIPCODE) {
		priv->resume = ax88772_resume;
		priv->suspend = ax88772_suspend;
	} else {
		priv->resume = ax88772a_resume;
		priv->suspend = ax88772_suspend;
	}

	return 0;
}

static void ax88772_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	asix_rx_fixup_common_free(dev->driver_priv);
	kfree(dev->driver_priv);
}

static const struct ethtool_ops ax88178_ethtool_ops = {
	.get_drvinfo		= asix_get_drvinfo,
	.get_link		= asix_get_link,
	.get_msglevel		= usbnet_get_msglevel,
	.set_msglevel		= usbnet_set_msglevel,
	.get_wol		= asix_get_wol,
	.set_wol		= asix_set_wol,
	.get_eeprom_len		= asix_get_eeprom_len,
	.get_eeprom		= asix_get_eeprom,
	.set_eeprom		= asix_set_eeprom,
	.nway_reset		= usbnet_nway_reset,
	.get_link_ksettings	= usbnet_get_link_ksettings,
	.set_link_ksettings	= usbnet_set_link_ksettings,
};

static int marvell_phy_init(struct usbnet *dev)
{
	struct asix_data *data = (struct asix_data *)&dev->data;
	u16 reg;

	netdev_dbg(dev->net, "marvell_phy_init()\n");

	reg = asix_mdio_read(dev->net, dev->mii.phy_id, MII_MARVELL_STATUS);
	netdev_dbg(dev->net, "MII_MARVELL_STATUS = 0x%04x\n", reg);

	asix_mdio_write(dev->net, dev->mii.phy_id, MII_MARVELL_CTRL,
			MARVELL_CTRL_RXDELAY | MARVELL_CTRL_TXDELAY);

	if (data->ledmode) {
		reg = asix_mdio_read(dev->net, dev->mii.phy_id,
			MII_MARVELL_LED_CTRL);
		netdev_dbg(dev->net, "MII_MARVELL_LED_CTRL (1) = 0x%04x\n", reg);

		reg &= 0xf8ff;
		reg |= (1 + 0x0100);
		asix_mdio_write(dev->net, dev->mii.phy_id,
			MII_MARVELL_LED_CTRL, reg);

		reg = asix_mdio_read(dev->net, dev->mii.phy_id,
			MII_MARVELL_LED_CTRL);
		netdev_dbg(dev->net, "MII_MARVELL_LED_CTRL (2) = 0x%04x\n", reg);
		reg &= 0xfc0f;
	}

	return 0;
}

static int rtl8211cl_phy_init(struct usbnet *dev)
{
	struct asix_data *data = (struct asix_data *)&dev->data;

	netdev_dbg(dev->net, "rtl8211cl_phy_init()\n");

	asix_mdio_write (dev->net, dev->mii.phy_id, 0x1f, 0x0005);
	asix_mdio_write (dev->net, dev->mii.phy_id, 0x0c, 0);
	asix_mdio_write (dev->net, dev->mii.phy_id, 0x01,
		asix_mdio_read (dev->net, dev->mii.phy_id, 0x01) | 0x0080);
	asix_mdio_write (dev->net, dev->mii.phy_id, 0x1f, 0);

	if (data->ledmode == 12) {
		asix_mdio_write (dev->net, dev->mii.phy_id, 0x1f, 0x0002);
		asix_mdio_write (dev->net, dev->mii.phy_id, 0x1a, 0x00cb);
		asix_mdio_write (dev->net, dev->mii.phy_id, 0x1f, 0);
	}

	return 0;
}

static int marvell_led_status(struct usbnet *dev, u16 speed)
{
	u16 reg = asix_mdio_read(dev->net, dev->mii.phy_id, MARVELL_LED_MANUAL);

	netdev_dbg(dev->net, "marvell_led_status() read 0x%04x\n", reg);

	/* Clear out the center LED bits - 0x03F0 */
	reg &= 0xfc0f;

	switch (speed) {
		case SPEED_1000:
			reg |= 0x03e0;
			break;
		case SPEED_100:
			reg |= 0x03b0;
			break;
		default:
			reg |= 0x02f0;
	}

	netdev_dbg(dev->net, "marvell_led_status() writing 0x%04x\n", reg);
	asix_mdio_write(dev->net, dev->mii.phy_id, MARVELL_LED_MANUAL, reg);

	return 0;
}

static int ax88178_reset(struct usbnet *dev)
{
	struct asix_data *data = (struct asix_data *)&dev->data;
	int ret;
	__le16 eeprom;
	u8 status;
	int gpio0 = 0;
	u32 phyid;

	asix_read_cmd(dev, AX_CMD_READ_GPIOS, 0, 0, 1, &status, 0);
	netdev_dbg(dev->net, "GPIO Status: 0x%04x\n", status);

	asix_write_cmd(dev, AX_CMD_WRITE_ENABLE, 0, 0, 0, NULL, 0);
	asix_read_cmd(dev, AX_CMD_READ_EEPROM, 0x0017, 0, 2, &eeprom, 0);
	asix_write_cmd(dev, AX_CMD_WRITE_DISABLE, 0, 0, 0, NULL, 0);

	netdev_dbg(dev->net, "EEPROM index 0x17 is 0x%04x\n", eeprom);

	if (eeprom == cpu_to_le16(0xffff)) {
		data->phymode = PHY_MODE_MARVELL;
		data->ledmode = 0;
		gpio0 = 1;
	} else {
		data->phymode = le16_to_cpu(eeprom) & 0x7F;
		data->ledmode = le16_to_cpu(eeprom) >> 8;
		gpio0 = (le16_to_cpu(eeprom) & 0x80) ? 0 : 1;
	}
	netdev_dbg(dev->net, "GPIO0: %d, PhyMode: %d\n", gpio0, data->phymode);

	/* Power up external GigaPHY through AX88178 GPIO pin */
	asix_write_gpio(dev, AX_GPIO_RSE | AX_GPIO_GPO_1 |
			AX_GPIO_GPO1EN, 40, 0);
	if ((le16_to_cpu(eeprom) >> 8) != 1) {
		asix_write_gpio(dev, 0x003c, 30, 0);
		asix_write_gpio(dev, 0x001c, 300, 0);
		asix_write_gpio(dev, 0x003c, 30, 0);
	} else {
		netdev_dbg(dev->net, "gpio phymode == 1 path\n");
		asix_write_gpio(dev, AX_GPIO_GPO1EN, 30, 0);
		asix_write_gpio(dev, AX_GPIO_GPO1EN | AX_GPIO_GPO_1, 30, 0);
	}

	/* Read PHYID register *AFTER* powering up PHY */
	phyid = asix_get_phyid(dev);
	netdev_dbg(dev->net, "PHYID=0x%08x\n", phyid);

	/* Set AX88178 to enable MII/GMII/RGMII interface for external PHY */
	asix_write_cmd(dev, AX_CMD_SW_PHY_SELECT, 0, 0, 0, NULL, 0);

	asix_sw_reset(dev, 0, 0);
	msleep(150);

	asix_sw_reset(dev, AX_SWRESET_PRL | AX_SWRESET_IPPD, 0);
	msleep(150);

	asix_write_rx_ctl(dev, 0, 0);

	if (data->phymode == PHY_MODE_MARVELL) {
		marvell_phy_init(dev);
		msleep(60);
	} else if (data->phymode == PHY_MODE_RTL8211CL)
		rtl8211cl_phy_init(dev);

	asix_phy_reset(dev, BMCR_RESET | BMCR_ANENABLE);
	asix_mdio_write(dev->net, dev->mii.phy_id, MII_ADVERTISE,
			ADVERTISE_ALL | ADVERTISE_CSMA | ADVERTISE_PAUSE_CAP);
	asix_mdio_write(dev->net, dev->mii.phy_id, MII_CTRL1000,
			ADVERTISE_1000FULL);

	asix_write_medium_mode(dev, AX88178_MEDIUM_DEFAULT, 0);
	mii_nway_restart(&dev->mii);

	/* Rewrite MAC address */
	memcpy(data->mac_addr, dev->net->dev_addr, ETH_ALEN);
	ret = asix_write_cmd(dev, AX_CMD_WRITE_NODE_ID, 0, 0, ETH_ALEN,
							data->mac_addr, 0);
	if (ret < 0)
		return ret;

	ret = asix_write_rx_ctl(dev, AX_DEFAULT_RX_CTL, 0);
	if (ret < 0)
		return ret;

	return 0;
}

static int ax88178_link_reset(struct usbnet *dev)
{
	u16 mode;
	struct ethtool_cmd ecmd = { .cmd = ETHTOOL_GSET };
	struct asix_data *data = (struct asix_data *)&dev->data;
	u32 speed;

	netdev_dbg(dev->net, "ax88178_link_reset()\n");

	mii_check_media(&dev->mii, 1, 1);
	mii_ethtool_gset(&dev->mii, &ecmd);
	mode = AX88178_MEDIUM_DEFAULT;
	speed = ethtool_cmd_speed(&ecmd);

	if (speed == SPEED_1000)
		mode |= AX_MEDIUM_GM;
	else if (speed == SPEED_100)
		mode |= AX_MEDIUM_PS;
	else
		mode &= ~(AX_MEDIUM_PS | AX_MEDIUM_GM);

	mode |= AX_MEDIUM_ENCK;

	if (ecmd.duplex == DUPLEX_FULL)
		mode |= AX_MEDIUM_FD;
	else
		mode &= ~AX_MEDIUM_FD;

	netdev_dbg(dev->net, "ax88178_link_reset() speed: %u duplex: %d setting mode to 0x%04x\n",
		   speed, ecmd.duplex, mode);

	asix_write_medium_mode(dev, mode, 0);

	if (data->phymode == PHY_MODE_MARVELL && data->ledmode)
		marvell_led_status(dev, speed);

	return 0;
}

static void ax88178_set_mfb(struct usbnet *dev)
{
	u16 mfb = AX_RX_CTL_MFB_16384;
	u16 rxctl;
	u16 medium;
	int old_rx_urb_size = dev->rx_urb_size;

	if (dev->hard_mtu < 2048) {
		dev->rx_urb_size = 2048;
		mfb = AX_RX_CTL_MFB_2048;
	} else if (dev->hard_mtu < 4096) {
		dev->rx_urb_size = 4096;
		mfb = AX_RX_CTL_MFB_4096;
	} else if (dev->hard_mtu < 8192) {
		dev->rx_urb_size = 8192;
		mfb = AX_RX_CTL_MFB_8192;
	} else if (dev->hard_mtu < 16384) {
		dev->rx_urb_size = 16384;
		mfb = AX_RX_CTL_MFB_16384;
	}

	rxctl = asix_read_rx_ctl(dev, 0);
	asix_write_rx_ctl(dev, (rxctl & ~AX_RX_CTL_MFB_16384) | mfb, 0);

	medium = asix_read_medium_status(dev, 0);
	if (dev->net->mtu > 1500)
		medium |= AX_MEDIUM_JFE;
	else
		medium &= ~AX_MEDIUM_JFE;
	asix_write_medium_mode(dev, medium, 0);

	if (dev->rx_urb_size > old_rx_urb_size)
		usbnet_unlink_rx_urbs(dev);
}

static int ax88178_change_mtu(struct net_device *net, int new_mtu)
{
	struct usbnet *dev = netdev_priv(net);
	int ll_mtu = new_mtu + net->hard_header_len + 4;

	netdev_dbg(dev->net, "ax88178_change_mtu() new_mtu=%d\n", new_mtu);

	if ((ll_mtu % dev->maxpacket) == 0)
		return -EDOM;

	net->mtu = new_mtu;
	dev->hard_mtu = net->mtu + net->hard_header_len;
	ax88178_set_mfb(dev);

	/* max qlen depend on hard_mtu and rx_urb_size */
	usbnet_update_max_qlen(dev);

	return 0;
}

static const struct net_device_ops ax88178_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
	.ndo_start_xmit		= usbnet_start_xmit,
	.ndo_tx_timeout		= usbnet_tx_timeout,
	.ndo_get_stats64	= usbnet_get_stats64,
	.ndo_set_mac_address 	= asix_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= asix_set_multicast,
	.ndo_do_ioctl 		= asix_ioctl,
	.ndo_change_mtu 	= ax88178_change_mtu,
};

static int ax88178_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int ret;
	u8 buf[ETH_ALEN];

	usbnet_get_endpoints(dev,intf);

	/* Get the MAC address */
	ret = asix_read_cmd(dev, AX_CMD_READ_NODE_ID, 0, 0, ETH_ALEN, buf, 0);
	if (ret < 0) {
		netdev_dbg(dev->net, "Failed to read MAC address: %d\n", ret);
		return ret;
	}

	asix_set_netdev_dev_addr(dev, buf);

	/* Initialize MII structure */
	dev->mii.dev = dev->net;
	dev->mii.mdio_read = asix_mdio_read;
	dev->mii.mdio_write = asix_mdio_write;
	dev->mii.phy_id_mask = 0x1f;
	dev->mii.reg_num_mask = 0xff;
	dev->mii.supports_gmii = 1;
	dev->mii.phy_id = asix_get_phy_addr(dev);

	dev->net->netdev_ops = &ax88178_netdev_ops;
	dev->net->ethtool_ops = &ax88178_ethtool_ops;
	dev->net->max_mtu = 16384 - (dev->net->hard_header_len + 4);

	/* Blink LEDS so users know driver saw dongle */
	asix_sw_reset(dev, 0, 0);
	msleep(150);

	asix_sw_reset(dev, AX_SWRESET_PRL | AX_SWRESET_IPPD, 0);
	msleep(150);

	/* Asix framing packs multiple eth frames into a 2K usb bulk transfer */
	if (dev->driver_info->flags & FLAG_FRAMING_AX) {
		/* hard_mtu  is still the default - the device does not support
		   jumbo eth frames */
		dev->rx_urb_size = 2048;
	}

	dev->driver_priv = kzalloc(sizeof(struct asix_common_private), GFP_KERNEL);
	if (!dev->driver_priv)
			return -ENOMEM;

	return 0;
}

static const struct driver_info ax8817x_info = {
	.description = "ASIX AX8817x USB 2.0 Ethernet",
	.bind = ax88172_bind,
	.status = asix_status,
	.link_reset = ax88172_link_reset,
	.reset = ax88172_link_reset,
	.flags =  FLAG_ETHER | FLAG_LINK_INTR,
	.data = 0x00130103,
};

static const struct driver_info dlink_dub_e100_info = {
	.description = "DLink DUB-E100 USB Ethernet",
	.bind = ax88172_bind,
	.status = asix_status,
	.link_reset = ax88172_link_reset,
	.reset = ax88172_link_reset,
	.flags =  FLAG_ETHER | FLAG_LINK_INTR,
	.data = 0x009f9d9f,
};

static const struct driver_info netgear_fa120_info = {
	.description = "Netgear FA-120 USB Ethernet",
	.bind = ax88172_bind,
	.status = asix_status,
	.link_reset = ax88172_link_reset,
	.reset = ax88172_link_reset,
	.flags =  FLAG_ETHER | FLAG_LINK_INTR,
	.data = 0x00130103,
};

static const struct driver_info hawking_uf200_info = {
	.description = "Hawking UF200 USB Ethernet",
	.bind = ax88172_bind,
	.status = asix_status,
	.link_reset = ax88172_link_reset,
	.reset = ax88172_link_reset,
	.flags =  FLAG_ETHER | FLAG_LINK_INTR,
	.data = 0x001f1d1f,
};

static const struct driver_info ax88772_info = {
	.description = "ASIX AX88772 USB 2.0 Ethernet",
	.bind = ax88772_bind,
	.unbind = ax88772_unbind,
	.status = asix_status,
	.link_reset = ax88772_link_reset,
	.reset = ax88772_reset,
	.flags = FLAG_ETHER | FLAG_FRAMING_AX | FLAG_LINK_INTR | FLAG_MULTI_PACKET,
	.rx_fixup = asix_rx_fixup_common,
	.tx_fixup = asix_tx_fixup,
};

static const struct driver_info ax88772b_info = {
	.description = "ASIX AX88772B USB 2.0 Ethernet",
	.bind = ax88772_bind,
	.unbind = ax88772_unbind,
	.status = asix_status,
	.link_reset = ax88772_link_reset,
	.reset = ax88772_reset,
	.flags = FLAG_ETHER | FLAG_FRAMING_AX | FLAG_LINK_INTR |
	         FLAG_MULTI_PACKET,
	.rx_fixup = asix_rx_fixup_common,
	.tx_fixup = asix_tx_fixup,
	.data = FLAG_EEPROM_MAC,
};

static const struct driver_info ax88178_info = {
	.description = "ASIX AX88178 USB 2.0 Ethernet",
	.bind = ax88178_bind,
	.unbind = ax88772_unbind,
	.status = asix_status,
	.link_reset = ax88178_link_reset,
	.reset = ax88178_reset,
	.flags = FLAG_ETHER | FLAG_FRAMING_AX | FLAG_LINK_INTR |
		 FLAG_MULTI_PACKET,
	.rx_fixup = asix_rx_fixup_common,
	.tx_fixup = asix_tx_fixup,
};

/*
 * USBLINK 20F9 "USB 2.0 LAN" USB ethernet adapter, typically found in
 * no-name packaging.
 * USB device strings are:
 *   1: Manufacturer: USBLINK
 *   2: Product: HG20F9 USB2.0
 *   3: Serial: 000003
 * Appears to be compatible with Asix 88772B.
 */
static const struct driver_info hg20f9_info = {
	.description = "HG20F9 USB 2.0 Ethernet",
	.bind = ax88772_bind,
	.unbind = ax88772_unbind,
	.status = asix_status,
	.link_reset = ax88772_link_reset,
	.reset = ax88772_reset,
	.flags = FLAG_ETHER | FLAG_FRAMING_AX | FLAG_LINK_INTR |
	         FLAG_MULTI_PACKET,
	.rx_fixup = asix_rx_fixup_common,
	.tx_fixup = asix_tx_fixup,
	.data = FLAG_EEPROM_MAC,
};

static const struct usb_device_id	products [] = {
{
	// Linksys USB200M
	USB_DEVICE (0x077b, 0x2226),
	.driver_info =	(unsigned long) &ax8817x_info,
}, {
	// Netgear FA120
	USB_DEVICE (0x0846, 0x1040),
	.driver_info =  (unsigned long) &netgear_fa120_info,
}, {
	// DLink DUB-E100
	USB_DEVICE (0x2001, 0x1a00),
	.driver_info =  (unsigned long) &dlink_dub_e100_info,
}, {
	// Intellinet, ST Lab USB Ethernet
	USB_DEVICE (0x0b95, 0x1720),
	.driver_info =  (unsigned long) &ax8817x_info,
}, {
	// Hawking UF200, TrendNet TU2-ET100
	USB_DEVICE (0x07b8, 0x420a),
	.driver_info =  (unsigned long) &hawking_uf200_info,
}, {
	// Billionton Systems, USB2AR
	USB_DEVICE (0x08dd, 0x90ff),
	.driver_info =  (unsigned long) &ax8817x_info,
}, {
	// Billionton Systems, GUSB2AM-1G-B
	USB_DEVICE(0x08dd, 0x0114),
	.driver_info =  (unsigned long) &ax88178_info,
}, {
	// ATEN UC210T
	USB_DEVICE (0x0557, 0x2009),
	.driver_info =  (unsigned long) &ax8817x_info,
}, {
	// Buffalo LUA-U2-KTX
	USB_DEVICE (0x0411, 0x003d),
	.driver_info =  (unsigned long) &ax8817x_info,
}, {
	// Buffalo LUA-U2-GT 10/100/1000
	USB_DEVICE (0x0411, 0x006e),
	.driver_info =  (unsigned long) &ax88178_info,
}, {
	// Sitecom LN-029 "USB 2.0 10/100 Ethernet adapter"
	USB_DEVICE (0x6189, 0x182d),
	.driver_info =  (unsigned long) &ax8817x_info,
}, {
	// Sitecom LN-031 "USB 2.0 10/100/1000 Ethernet adapter"
	USB_DEVICE (0x0df6, 0x0056),
	.driver_info =  (unsigned long) &ax88178_info,
}, {
	// Sitecom LN-028 "USB 2.0 10/100/1000 Ethernet adapter"
	USB_DEVICE (0x0df6, 0x061c),
	.driver_info =  (unsigned long) &ax88178_info,
}, {
	// corega FEther USB2-TX
	USB_DEVICE (0x07aa, 0x0017),
	.driver_info =  (unsigned long) &ax8817x_info,
}, {
	// Surecom EP-1427X-2
	USB_DEVICE (0x1189, 0x0893),
	.driver_info = (unsigned long) &ax8817x_info,
}, {
	// goodway corp usb gwusb2e
	USB_DEVICE (0x1631, 0x6200),
	.driver_info = (unsigned long) &ax8817x_info,
}, {
	// JVC MP-PRX1 Port Replicator
	USB_DEVICE (0x04f1, 0x3008),
	.driver_info = (unsigned long) &ax8817x_info,
}, {
	// Lenovo U2L100P 10/100
	USB_DEVICE (0x17ef, 0x7203),
	.driver_info = (unsigned long)&ax88772b_info,
}, {
	// ASIX AX88772B 10/100
	USB_DEVICE (0x0b95, 0x772b),
	.driver_info = (unsigned long) &ax88772b_info,
}, {
	// ASIX AX88772 10/100
	USB_DEVICE (0x0b95, 0x7720),
	.driver_info = (unsigned long) &ax88772_info,
}, {
	// ASIX AX88178 10/100/1000
	USB_DEVICE (0x0b95, 0x1780),
	.driver_info = (unsigned long) &ax88178_info,
}, {
	// Logitec LAN-GTJ/U2A
	USB_DEVICE (0x0789, 0x0160),
	.driver_info = (unsigned long) &ax88178_info,
}, {
	// Linksys USB200M Rev 2
	USB_DEVICE (0x13b1, 0x0018),
	.driver_info = (unsigned long) &ax88772_info,
}, {
	// 0Q0 cable ethernet
	USB_DEVICE (0x1557, 0x7720),
	.driver_info = (unsigned long) &ax88772_info,
}, {
	// DLink DUB-E100 H/W Ver B1
	USB_DEVICE (0x07d1, 0x3c05),
	.driver_info = (unsigned long) &ax88772_info,
}, {
	// DLink DUB-E100 H/W Ver B1 Alternate
	USB_DEVICE (0x2001, 0x3c05),
	.driver_info = (unsigned long) &ax88772_info,
}, {
       // DLink DUB-E100 H/W Ver C1
       USB_DEVICE (0x2001, 0x1a02),
       .driver_info = (unsigned long) &ax88772_info,
}, {
	// Linksys USB1000
	USB_DEVICE (0x1737, 0x0039),
	.driver_info = (unsigned long) &ax88178_info,
}, {
	// IO-DATA ETG-US2
	USB_DEVICE (0x04bb, 0x0930),
	.driver_info = (unsigned long) &ax88178_info,
}, {
	// Belkin F5D5055
	USB_DEVICE(0x050d, 0x5055),
	.driver_info = (unsigned long) &ax88178_info,
}, {
	// Apple USB Ethernet Adapter
	USB_DEVICE(0x05ac, 0x1402),
	.driver_info = (unsigned long) &ax88772_info,
}, {
	// Cables-to-Go USB Ethernet Adapter
	USB_DEVICE(0x0b95, 0x772a),
	.driver_info = (unsigned long) &ax88772_info,
}, {
	// ABOCOM for pci
	USB_DEVICE(0x14ea, 0xab11),
	.driver_info = (unsigned long) &ax88178_info,
}, {
	// ASIX 88772a
	USB_DEVICE(0x0db0, 0xa877),
	.driver_info = (unsigned long) &ax88772_info,
}, {
	// Asus USB Ethernet Adapter
	USB_DEVICE (0x0b95, 0x7e2b),
	.driver_info = (unsigned long)&ax88772b_info,
}, {
	/* ASIX 88172a demo board */
	USB_DEVICE(0x0b95, 0x172a),
	.driver_info = (unsigned long) &ax88172a_info,
}, {
	/*
	 * USBLINK HG20F9 "USB 2.0 LAN"
	 * Appears to have gazumped Linksys's manufacturer ID but
	 * doesn't (yet) conflict with any known Linksys product.
	 */
	USB_DEVICE(0x066b, 0x20f9),
	.driver_info = (unsigned long) &hg20f9_info,
},
	{ },		// END
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver asix_driver = {
	.name =		DRIVER_NAME,
	.id_table =	products,
	.probe =	usbnet_probe,
	.suspend =	asix_suspend,
	.resume =	asix_resume,
	.reset_resume =	asix_resume,
	.disconnect =	usbnet_disconnect,
	.supports_autosuspend = 1,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(asix_driver);

MODULE_AUTHOR("David Hollis");
MODULE_VERSION(DRIVER_VERSION);
MODULE_DESCRIPTION("ASIX AX8817X based USB 2.0 Ethernet Devices");
MODULE_LICENSE("GPL");

