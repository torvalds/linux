// SPDX-License-Identifier: GPL-2.0-or-later
/* Aquantia Corp. Aquantia AQtion USB to 5GbE Controller
 * Copyright (C) 2003-2005 David Hollis <dhollis@davehollis.com>
 * Copyright (C) 2005 Phil Chang <pchang23@sbcglobal.net>
 * Copyright (C) 2002-2003 TiVo Inc.
 * Copyright (C) 2017-2018 ASIX
 * Copyright (C) 2018 Aquantia Corp.
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/crc32.h>
#include <linux/if_vlan.h>
#include <linux/usb/cdc.h>
#include <linux/usb/usbnet.h>
#include <linux/linkmode.h>

#include "aqc111.h"

#define DRIVER_NAME "aqc111"

static int aqc111_read_cmd_nopm(struct usbnet *dev, u8 cmd, u16 value,
				u16 index, u16 size, void *data)
{
	int ret;

	ret = usbnet_read_cmd_nopm(dev, cmd, USB_DIR_IN | USB_TYPE_VENDOR |
				   USB_RECIP_DEVICE, value, index, data, size);

	if (unlikely(ret < 0))
		netdev_warn(dev->net,
			    "Failed to read(0x%x) reg index 0x%04x: %d\n",
			    cmd, index, ret);

	return ret;
}

static int aqc111_read_cmd(struct usbnet *dev, u8 cmd, u16 value,
			   u16 index, u16 size, void *data)
{
	int ret;

	ret = usbnet_read_cmd(dev, cmd, USB_DIR_IN | USB_TYPE_VENDOR |
			      USB_RECIP_DEVICE, value, index, data, size);

	if (unlikely(ret < 0))
		netdev_warn(dev->net,
			    "Failed to read(0x%x) reg index 0x%04x: %d\n",
			    cmd, index, ret);

	return ret;
}

static int aqc111_read16_cmd_nopm(struct usbnet *dev, u8 cmd, u16 value,
				  u16 index, u16 *data)
{
	int ret = 0;

	ret = aqc111_read_cmd_nopm(dev, cmd, value, index, sizeof(*data), data);
	le16_to_cpus(data);

	return ret;
}

static int aqc111_read16_cmd(struct usbnet *dev, u8 cmd, u16 value,
			     u16 index, u16 *data)
{
	int ret = 0;

	ret = aqc111_read_cmd(dev, cmd, value, index, sizeof(*data), data);
	le16_to_cpus(data);

	return ret;
}

static int __aqc111_write_cmd(struct usbnet *dev, u8 cmd, u8 reqtype,
			      u16 value, u16 index, u16 size, const void *data)
{
	int err = -ENOMEM;
	void *buf = NULL;

	netdev_dbg(dev->net,
		   "%s cmd=%#x reqtype=%#x value=%#x index=%#x size=%d\n",
		   __func__, cmd, reqtype, value, index, size);

	if (data) {
		buf = kmemdup(data, size, GFP_KERNEL);
		if (!buf)
			goto out;
	}

	err = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
			      cmd, reqtype, value, index, buf, size,
			      (cmd == AQ_PHY_POWER) ? AQ_USB_PHY_SET_TIMEOUT :
			      AQ_USB_SET_TIMEOUT);

	if (unlikely(err < 0))
		netdev_warn(dev->net,
			    "Failed to write(0x%x) reg index 0x%04x: %d\n",
			    cmd, index, err);
	kfree(buf);

out:
	return err;
}

static int aqc111_write_cmd_nopm(struct usbnet *dev, u8 cmd, u16 value,
				 u16 index, u16 size, void *data)
{
	int ret;

	ret = __aqc111_write_cmd(dev, cmd, USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_DEVICE, value, index, size, data);

	return ret;
}

static int aqc111_write_cmd(struct usbnet *dev, u8 cmd, u16 value,
			    u16 index, u16 size, void *data)
{
	int ret;

	if (usb_autopm_get_interface(dev->intf) < 0)
		return -ENODEV;

	ret = __aqc111_write_cmd(dev, cmd, USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_DEVICE, value, index, size, data);

	usb_autopm_put_interface(dev->intf);

	return ret;
}

static int aqc111_write16_cmd_nopm(struct usbnet *dev, u8 cmd, u16 value,
				   u16 index, u16 *data)
{
	u16 tmp = *data;

	cpu_to_le16s(&tmp);

	return aqc111_write_cmd_nopm(dev, cmd, value, index, sizeof(tmp), &tmp);
}

static int aqc111_write16_cmd(struct usbnet *dev, u8 cmd, u16 value,
			      u16 index, u16 *data)
{
	u16 tmp = *data;

	cpu_to_le16s(&tmp);

	return aqc111_write_cmd(dev, cmd, value, index, sizeof(tmp), &tmp);
}

static int aqc111_write32_cmd_nopm(struct usbnet *dev, u8 cmd, u16 value,
				   u16 index, u32 *data)
{
	u32 tmp = *data;

	cpu_to_le32s(&tmp);

	return aqc111_write_cmd_nopm(dev, cmd, value, index, sizeof(tmp), &tmp);
}

static int aqc111_write32_cmd(struct usbnet *dev, u8 cmd, u16 value,
			      u16 index, u32 *data)
{
	u32 tmp = *data;

	cpu_to_le32s(&tmp);

	return aqc111_write_cmd(dev, cmd, value, index, sizeof(tmp), &tmp);
}

static int aqc111_write_cmd_async(struct usbnet *dev, u8 cmd, u16 value,
				  u16 index, u16 size, void *data)
{
	return usbnet_write_cmd_async(dev, cmd, USB_DIR_OUT | USB_TYPE_VENDOR |
				      USB_RECIP_DEVICE, value, index, data,
				      size);
}

static int aqc111_write16_cmd_async(struct usbnet *dev, u8 cmd, u16 value,
				    u16 index, u16 *data)
{
	u16 tmp = *data;

	cpu_to_le16s(&tmp);

	return aqc111_write_cmd_async(dev, cmd, value, index,
				      sizeof(tmp), &tmp);
}

static void aqc111_get_drvinfo(struct net_device *net,
			       struct ethtool_drvinfo *info)
{
	struct usbnet *dev = netdev_priv(net);
	struct aqc111_data *aqc111_data = dev->driver_priv;

	/* Inherit standard device info */
	usbnet_get_drvinfo(net, info);
	strlcpy(info->driver, DRIVER_NAME, sizeof(info->driver));
	snprintf(info->fw_version, sizeof(info->fw_version), "%u.%u.%u",
		 aqc111_data->fw_ver.major,
		 aqc111_data->fw_ver.minor,
		 aqc111_data->fw_ver.rev);
	info->eedump_len = 0x00;
	info->regdump_len = 0x00;
}

static void aqc111_get_wol(struct net_device *net,
			   struct ethtool_wolinfo *wolinfo)
{
	struct usbnet *dev = netdev_priv(net);
	struct aqc111_data *aqc111_data = dev->driver_priv;

	wolinfo->supported = WAKE_MAGIC;
	wolinfo->wolopts = 0;

	if (aqc111_data->wol_flags & AQ_WOL_FLAG_MP)
		wolinfo->wolopts |= WAKE_MAGIC;
}

static int aqc111_set_wol(struct net_device *net,
			  struct ethtool_wolinfo *wolinfo)
{
	struct usbnet *dev = netdev_priv(net);
	struct aqc111_data *aqc111_data = dev->driver_priv;

	if (wolinfo->wolopts & ~WAKE_MAGIC)
		return -EINVAL;

	aqc111_data->wol_flags = 0;
	if (wolinfo->wolopts & WAKE_MAGIC)
		aqc111_data->wol_flags |= AQ_WOL_FLAG_MP;

	return 0;
}

static void aqc111_speed_to_link_mode(u32 speed,
				      struct ethtool_link_ksettings *elk)
{
	switch (speed) {
	case SPEED_5000:
		ethtool_link_ksettings_add_link_mode(elk, advertising,
						     5000baseT_Full);
		break;
	case SPEED_2500:
		ethtool_link_ksettings_add_link_mode(elk, advertising,
						     2500baseT_Full);
		break;
	case SPEED_1000:
		ethtool_link_ksettings_add_link_mode(elk, advertising,
						     1000baseT_Full);
		break;
	case SPEED_100:
		ethtool_link_ksettings_add_link_mode(elk, advertising,
						     100baseT_Full);
		break;
	}
}

static int aqc111_get_link_ksettings(struct net_device *net,
				     struct ethtool_link_ksettings *elk)
{
	struct usbnet *dev = netdev_priv(net);
	struct aqc111_data *aqc111_data = dev->driver_priv;
	enum usb_device_speed usb_speed = dev->udev->speed;
	u32 speed = SPEED_UNKNOWN;

	ethtool_link_ksettings_zero_link_mode(elk, supported);
	ethtool_link_ksettings_add_link_mode(elk, supported,
					     100baseT_Full);
	ethtool_link_ksettings_add_link_mode(elk, supported,
					     1000baseT_Full);
	if (usb_speed == USB_SPEED_SUPER) {
		ethtool_link_ksettings_add_link_mode(elk, supported,
						     2500baseT_Full);
		ethtool_link_ksettings_add_link_mode(elk, supported,
						     5000baseT_Full);
	}
	ethtool_link_ksettings_add_link_mode(elk, supported, TP);
	ethtool_link_ksettings_add_link_mode(elk, supported, Autoneg);

	elk->base.port = PORT_TP;
	elk->base.transceiver = XCVR_INTERNAL;

	elk->base.mdio_support = 0x00; /*Not supported*/

	if (aqc111_data->autoneg)
		linkmode_copy(elk->link_modes.advertising,
			      elk->link_modes.supported);
	else
		aqc111_speed_to_link_mode(aqc111_data->advertised_speed, elk);

	elk->base.autoneg = aqc111_data->autoneg;

	switch (aqc111_data->link_speed) {
	case AQ_INT_SPEED_5G:
		speed = SPEED_5000;
		break;
	case AQ_INT_SPEED_2_5G:
		speed = SPEED_2500;
		break;
	case AQ_INT_SPEED_1G:
		speed = SPEED_1000;
		break;
	case AQ_INT_SPEED_100M:
		speed = SPEED_100;
		break;
	}
	elk->base.duplex = DUPLEX_FULL;
	elk->base.speed = speed;

	return 0;
}

static void aqc111_set_phy_speed(struct usbnet *dev, u8 autoneg, u16 speed)
{
	struct aqc111_data *aqc111_data = dev->driver_priv;

	aqc111_data->phy_cfg &= ~AQ_ADV_MASK;
	aqc111_data->phy_cfg |= AQ_PAUSE;
	aqc111_data->phy_cfg |= AQ_ASYM_PAUSE;
	aqc111_data->phy_cfg |= AQ_DOWNSHIFT;
	aqc111_data->phy_cfg &= ~AQ_DSH_RETRIES_MASK;
	aqc111_data->phy_cfg |= (3 << AQ_DSH_RETRIES_SHIFT) &
				AQ_DSH_RETRIES_MASK;

	if (autoneg == AUTONEG_ENABLE) {
		switch (speed) {
		case SPEED_5000:
			aqc111_data->phy_cfg |= AQ_ADV_5G;
			/* fall-through */
		case SPEED_2500:
			aqc111_data->phy_cfg |= AQ_ADV_2G5;
			/* fall-through */
		case SPEED_1000:
			aqc111_data->phy_cfg |= AQ_ADV_1G;
			/* fall-through */
		case SPEED_100:
			aqc111_data->phy_cfg |= AQ_ADV_100M;
			/* fall-through */
		}
	} else {
		switch (speed) {
		case SPEED_5000:
			aqc111_data->phy_cfg |= AQ_ADV_5G;
			break;
		case SPEED_2500:
			aqc111_data->phy_cfg |= AQ_ADV_2G5;
			break;
		case SPEED_1000:
			aqc111_data->phy_cfg |= AQ_ADV_1G;
			break;
		case SPEED_100:
			aqc111_data->phy_cfg |= AQ_ADV_100M;
			break;
		}
	}

	aqc111_write32_cmd(dev, AQ_PHY_OPS, 0, 0, &aqc111_data->phy_cfg);
}

static int aqc111_set_link_ksettings(struct net_device *net,
				     const struct ethtool_link_ksettings *elk)
{
	struct usbnet *dev = netdev_priv(net);
	struct aqc111_data *aqc111_data = dev->driver_priv;
	enum usb_device_speed usb_speed = dev->udev->speed;
	u8 autoneg = elk->base.autoneg;
	u32 speed = elk->base.speed;

	if (autoneg == AUTONEG_ENABLE) {
		if (aqc111_data->autoneg != AUTONEG_ENABLE) {
			aqc111_data->autoneg = AUTONEG_ENABLE;
			aqc111_data->advertised_speed =
					(usb_speed == USB_SPEED_SUPER) ?
					 SPEED_5000 : SPEED_1000;
			aqc111_set_phy_speed(dev, aqc111_data->autoneg,
					     aqc111_data->advertised_speed);
		}
	} else {
		if (speed != SPEED_100 &&
		    speed != SPEED_1000 &&
		    speed != SPEED_2500 &&
		    speed != SPEED_5000 &&
		    speed != SPEED_UNKNOWN)
			return -EINVAL;

		if (elk->base.duplex != DUPLEX_FULL)
			return -EINVAL;

		if (usb_speed != USB_SPEED_SUPER && speed > SPEED_1000)
			return -EINVAL;

		aqc111_data->autoneg = AUTONEG_DISABLE;
		if (speed != SPEED_UNKNOWN)
			aqc111_data->advertised_speed = speed;

		aqc111_set_phy_speed(dev, aqc111_data->autoneg,
				     aqc111_data->advertised_speed);
	}

	return 0;
}

static const struct ethtool_ops aqc111_ethtool_ops = {
	.get_drvinfo = aqc111_get_drvinfo,
	.get_wol = aqc111_get_wol,
	.set_wol = aqc111_set_wol,
	.get_msglevel = usbnet_get_msglevel,
	.set_msglevel = usbnet_set_msglevel,
	.get_link = ethtool_op_get_link,
	.get_link_ksettings = aqc111_get_link_ksettings,
	.set_link_ksettings = aqc111_set_link_ksettings
};

static int aqc111_change_mtu(struct net_device *net, int new_mtu)
{
	struct usbnet *dev = netdev_priv(net);
	u16 reg16 = 0;
	u8 buf[5];

	net->mtu = new_mtu;
	dev->hard_mtu = net->mtu + net->hard_header_len;

	aqc111_read16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
			  2, &reg16);
	if (net->mtu > 1500)
		reg16 |= SFR_MEDIUM_JUMBO_EN;
	else
		reg16 &= ~SFR_MEDIUM_JUMBO_EN;

	aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
			   2, &reg16);

	if (dev->net->mtu > 12500 && dev->net->mtu <= 16334) {
		memcpy(buf, &AQC111_BULKIN_SIZE[2], 5);
		/* RX bulk configuration */
		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_RX_BULKIN_QCTRL,
				 5, 5, buf);
	}

	/* Set high low water level */
	if (dev->net->mtu <= 4500)
		reg16 = 0x0810;
	else if (dev->net->mtu <= 9500)
		reg16 = 0x1020;
	else if (dev->net->mtu <= 12500)
		reg16 = 0x1420;
	else if (dev->net->mtu <= 16334)
		reg16 = 0x1A20;

	aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_PAUSE_WATERLVL_LOW,
			   2, &reg16);

	return 0;
}

static int aqc111_set_mac_addr(struct net_device *net, void *p)
{
	struct usbnet *dev = netdev_priv(net);
	int ret = 0;

	ret = eth_mac_addr(net, p);
	if (ret < 0)
		return ret;

	/* Set the MAC address */
	return aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_NODE_ID, ETH_ALEN,
				ETH_ALEN, net->dev_addr);
}

static int aqc111_vlan_rx_kill_vid(struct net_device *net,
				   __be16 proto, u16 vid)
{
	struct usbnet *dev = netdev_priv(net);
	u8 vlan_ctrl = 0;
	u16 reg16 = 0;
	u8 reg8 = 0;

	aqc111_read_cmd(dev, AQ_ACCESS_MAC, SFR_VLAN_ID_CONTROL, 1, 1, &reg8);
	vlan_ctrl = reg8;

	/* Address */
	reg8 = (vid / 16);
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_VLAN_ID_ADDRESS, 1, 1, &reg8);
	/* Data */
	reg8 = vlan_ctrl | SFR_VLAN_CONTROL_RD;
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_VLAN_ID_CONTROL, 1, 1, &reg8);
	aqc111_read16_cmd(dev, AQ_ACCESS_MAC, SFR_VLAN_ID_DATA0, 2, &reg16);
	reg16 &= ~(1 << (vid % 16));
	aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_VLAN_ID_DATA0, 2, &reg16);
	reg8 = vlan_ctrl | SFR_VLAN_CONTROL_WE;
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_VLAN_ID_CONTROL, 1, 1, &reg8);

	return 0;
}

static int aqc111_vlan_rx_add_vid(struct net_device *net, __be16 proto, u16 vid)
{
	struct usbnet *dev = netdev_priv(net);
	u8 vlan_ctrl = 0;
	u16 reg16 = 0;
	u8 reg8 = 0;

	aqc111_read_cmd(dev, AQ_ACCESS_MAC, SFR_VLAN_ID_CONTROL, 1, 1, &reg8);
	vlan_ctrl = reg8;

	/* Address */
	reg8 = (vid / 16);
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_VLAN_ID_ADDRESS, 1, 1, &reg8);
	/* Data */
	reg8 = vlan_ctrl | SFR_VLAN_CONTROL_RD;
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_VLAN_ID_CONTROL, 1, 1, &reg8);
	aqc111_read16_cmd(dev, AQ_ACCESS_MAC, SFR_VLAN_ID_DATA0, 2, &reg16);
	reg16 |= (1 << (vid % 16));
	aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_VLAN_ID_DATA0, 2, &reg16);
	reg8 = vlan_ctrl | SFR_VLAN_CONTROL_WE;
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_VLAN_ID_CONTROL, 1, 1, &reg8);

	return 0;
}

static void aqc111_set_rx_mode(struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);
	struct aqc111_data *aqc111_data = dev->driver_priv;
	int mc_count = 0;

	mc_count = netdev_mc_count(net);

	aqc111_data->rxctl &= ~(SFR_RX_CTL_PRO | SFR_RX_CTL_AMALL |
				SFR_RX_CTL_AM);

	if (net->flags & IFF_PROMISC) {
		aqc111_data->rxctl |= SFR_RX_CTL_PRO;
	} else if ((net->flags & IFF_ALLMULTI) || mc_count > AQ_MAX_MCAST) {
		aqc111_data->rxctl |= SFR_RX_CTL_AMALL;
	} else if (!netdev_mc_empty(net)) {
		u8 m_filter[AQ_MCAST_FILTER_SIZE] = { 0 };
		struct netdev_hw_addr *ha = NULL;
		u32 crc_bits = 0;

		netdev_for_each_mc_addr(ha, net) {
			crc_bits = ether_crc(ETH_ALEN, ha->addr) >> 26;
			m_filter[crc_bits >> 3] |= BIT(crc_bits & 7);
		}

		aqc111_write_cmd_async(dev, AQ_ACCESS_MAC,
				       SFR_MULTI_FILTER_ARRY,
				       AQ_MCAST_FILTER_SIZE,
				       AQ_MCAST_FILTER_SIZE, m_filter);

		aqc111_data->rxctl |= SFR_RX_CTL_AM;
	}

	aqc111_write16_cmd_async(dev, AQ_ACCESS_MAC, SFR_RX_CTL,
				 2, &aqc111_data->rxctl);
}

static int aqc111_set_features(struct net_device *net,
			       netdev_features_t features)
{
	struct usbnet *dev = netdev_priv(net);
	struct aqc111_data *aqc111_data = dev->driver_priv;
	netdev_features_t changed = net->features ^ features;
	u16 reg16 = 0;
	u8 reg8 = 0;

	if (changed & NETIF_F_IP_CSUM) {
		aqc111_read_cmd(dev, AQ_ACCESS_MAC, SFR_TXCOE_CTL, 1, 1, &reg8);
		reg8 ^= SFR_TXCOE_TCP | SFR_TXCOE_UDP;
		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_TXCOE_CTL,
				 1, 1, &reg8);
	}

	if (changed & NETIF_F_IPV6_CSUM) {
		aqc111_read_cmd(dev, AQ_ACCESS_MAC, SFR_TXCOE_CTL, 1, 1, &reg8);
		reg8 ^= SFR_TXCOE_TCPV6 | SFR_TXCOE_UDPV6;
		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_TXCOE_CTL,
				 1, 1, &reg8);
	}

	if (changed & NETIF_F_RXCSUM) {
		aqc111_read_cmd(dev, AQ_ACCESS_MAC, SFR_RXCOE_CTL, 1, 1, &reg8);
		if (features & NETIF_F_RXCSUM) {
			aqc111_data->rx_checksum = 1;
			reg8 &= ~(SFR_RXCOE_IP | SFR_RXCOE_TCP | SFR_RXCOE_UDP |
				  SFR_RXCOE_TCPV6 | SFR_RXCOE_UDPV6);
		} else {
			aqc111_data->rx_checksum = 0;
			reg8 |= SFR_RXCOE_IP | SFR_RXCOE_TCP | SFR_RXCOE_UDP |
				SFR_RXCOE_TCPV6 | SFR_RXCOE_UDPV6;
		}

		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_RXCOE_CTL,
				 1, 1, &reg8);
	}
	if (changed & NETIF_F_HW_VLAN_CTAG_FILTER) {
		if (features & NETIF_F_HW_VLAN_CTAG_FILTER) {
			u16 i = 0;

			for (i = 0; i < 256; i++) {
				/* Address */
				reg8 = i;
				aqc111_write_cmd(dev, AQ_ACCESS_MAC,
						 SFR_VLAN_ID_ADDRESS,
						 1, 1, &reg8);
				/* Data */
				aqc111_write16_cmd(dev, AQ_ACCESS_MAC,
						   SFR_VLAN_ID_DATA0,
						   2, &reg16);
				reg8 = SFR_VLAN_CONTROL_WE;
				aqc111_write_cmd(dev, AQ_ACCESS_MAC,
						 SFR_VLAN_ID_CONTROL,
						 1, 1, &reg8);
			}
			aqc111_read_cmd(dev, AQ_ACCESS_MAC, SFR_VLAN_ID_CONTROL,
					1, 1, &reg8);
			reg8 |= SFR_VLAN_CONTROL_VFE;
			aqc111_write_cmd(dev, AQ_ACCESS_MAC,
					 SFR_VLAN_ID_CONTROL, 1, 1, &reg8);
		} else {
			aqc111_read_cmd(dev, AQ_ACCESS_MAC, SFR_VLAN_ID_CONTROL,
					1, 1, &reg8);
			reg8 &= ~SFR_VLAN_CONTROL_VFE;
			aqc111_write_cmd(dev, AQ_ACCESS_MAC,
					 SFR_VLAN_ID_CONTROL, 1, 1, &reg8);
		}
	}

	return 0;
}

static const struct net_device_ops aqc111_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
	.ndo_start_xmit		= usbnet_start_xmit,
	.ndo_tx_timeout		= usbnet_tx_timeout,
	.ndo_get_stats64	= usbnet_get_stats64,
	.ndo_change_mtu		= aqc111_change_mtu,
	.ndo_set_mac_address	= aqc111_set_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_vlan_rx_add_vid	= aqc111_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= aqc111_vlan_rx_kill_vid,
	.ndo_set_rx_mode	= aqc111_set_rx_mode,
	.ndo_set_features	= aqc111_set_features,
};

static int aqc111_read_perm_mac(struct usbnet *dev)
{
	u8 buf[ETH_ALEN];
	int ret;

	ret = aqc111_read_cmd(dev, AQ_FLASH_PARAMETERS, 0, 0, ETH_ALEN, buf);
	if (ret < 0)
		goto out;

	ether_addr_copy(dev->net->perm_addr, buf);

	return 0;
out:
	return ret;
}

static void aqc111_read_fw_version(struct usbnet *dev,
				   struct aqc111_data *aqc111_data)
{
	aqc111_read_cmd(dev, AQ_ACCESS_MAC, AQ_FW_VER_MAJOR,
			1, 1, &aqc111_data->fw_ver.major);
	aqc111_read_cmd(dev, AQ_ACCESS_MAC, AQ_FW_VER_MINOR,
			1, 1, &aqc111_data->fw_ver.minor);
	aqc111_read_cmd(dev, AQ_ACCESS_MAC, AQ_FW_VER_REV,
			1, 1, &aqc111_data->fw_ver.rev);

	if (aqc111_data->fw_ver.major & 0x80)
		aqc111_data->fw_ver.major &= ~0x80;
}

static int aqc111_bind(struct usbnet *dev, struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	enum usb_device_speed usb_speed = udev->speed;
	struct aqc111_data *aqc111_data;
	int ret;

	/* Check if vendor configuration */
	if (udev->actconfig->desc.bConfigurationValue != 1) {
		usb_driver_set_configuration(udev, 1);
		return -ENODEV;
	}

	usb_reset_configuration(dev->udev);

	ret = usbnet_get_endpoints(dev, intf);
	if (ret < 0) {
		netdev_dbg(dev->net, "usbnet_get_endpoints failed");
		return ret;
	}

	aqc111_data = kzalloc(sizeof(*aqc111_data), GFP_KERNEL);
	if (!aqc111_data)
		return -ENOMEM;

	/* store aqc111_data pointer in device data field */
	dev->driver_priv = aqc111_data;

	/* Init the MAC address */
	ret = aqc111_read_perm_mac(dev);
	if (ret)
		goto out;

	ether_addr_copy(dev->net->dev_addr, dev->net->perm_addr);

	/* Set Rx urb size */
	dev->rx_urb_size = URB_SIZE;

	/* Set TX needed headroom & tailroom */
	dev->net->needed_headroom += sizeof(u64);
	dev->net->needed_tailroom += sizeof(u64);

	dev->net->max_mtu = 16334;

	dev->net->netdev_ops = &aqc111_netdev_ops;
	dev->net->ethtool_ops = &aqc111_ethtool_ops;

	if (usb_device_no_sg_constraint(dev->udev))
		dev->can_dma_sg = 1;

	dev->net->hw_features |= AQ_SUPPORT_HW_FEATURE;
	dev->net->features |= AQ_SUPPORT_FEATURE;
	dev->net->vlan_features |= AQ_SUPPORT_VLAN_FEATURE;

	netif_set_gso_max_size(dev->net, 65535);

	aqc111_read_fw_version(dev, aqc111_data);
	aqc111_data->autoneg = AUTONEG_ENABLE;
	aqc111_data->advertised_speed = (usb_speed == USB_SPEED_SUPER) ?
					 SPEED_5000 : SPEED_1000;

	return 0;

out:
	kfree(aqc111_data);
	return ret;
}

static void aqc111_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	struct aqc111_data *aqc111_data = dev->driver_priv;
	u16 reg16;

	/* Force bz */
	reg16 = SFR_PHYPWR_RSTCTL_BZ;
	aqc111_write16_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_PHYPWR_RSTCTL,
				2, &reg16);
	reg16 = 0;
	aqc111_write16_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_PHYPWR_RSTCTL,
				2, &reg16);

	/* Power down ethernet PHY */
	aqc111_data->phy_cfg &= ~AQ_ADV_MASK;
	aqc111_data->phy_cfg |= AQ_LOW_POWER;
	aqc111_data->phy_cfg &= ~AQ_PHY_POWER_EN;
	aqc111_write32_cmd_nopm(dev, AQ_PHY_OPS, 0, 0,
				&aqc111_data->phy_cfg);

	kfree(aqc111_data);
}

static void aqc111_status(struct usbnet *dev, struct urb *urb)
{
	struct aqc111_data *aqc111_data = dev->driver_priv;
	u64 *event_data = NULL;
	int link = 0;

	if (urb->actual_length < sizeof(*event_data))
		return;

	event_data = urb->transfer_buffer;
	le64_to_cpus(event_data);

	if (*event_data & AQ_LS_MASK)
		link = 1;
	else
		link = 0;

	aqc111_data->link_speed = (*event_data & AQ_SPEED_MASK) >>
				  AQ_SPEED_SHIFT;
	aqc111_data->link = link;

	if (netif_carrier_ok(dev->net) != link)
		usbnet_defer_kevent(dev, EVENT_LINK_RESET);
}

static void aqc111_configure_rx(struct usbnet *dev,
				struct aqc111_data *aqc111_data)
{
	enum usb_device_speed usb_speed = dev->udev->speed;
	u16 link_speed = 0, usb_host = 0;
	u8 buf[5] = { 0 };
	u8 queue_num = 0;
	u16 reg16 = 0;
	u8 reg8 = 0;

	buf[0] = 0x00;
	buf[1] = 0xF8;
	buf[2] = 0x07;
	switch (aqc111_data->link_speed) {
	case AQ_INT_SPEED_5G:
		link_speed = 5000;
		reg8 = 0x05;
		reg16 = 0x001F;
		break;
	case AQ_INT_SPEED_2_5G:
		link_speed = 2500;
		reg16 = 0x003F;
		break;
	case AQ_INT_SPEED_1G:
		link_speed = 1000;
		reg16 = 0x009F;
		break;
	case AQ_INT_SPEED_100M:
		link_speed = 100;
		queue_num = 1;
		reg16 = 0x063F;
		buf[1] = 0xFB;
		buf[2] = 0x4;
		break;
	}

	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_INTER_PACKET_GAP_0,
			 1, 1, &reg8);

	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_TX_PAUSE_RESEND_T, 3, 3, buf);

	switch (usb_speed) {
	case USB_SPEED_SUPER:
		usb_host = 3;
		break;
	case USB_SPEED_HIGH:
		usb_host = 2;
		break;
	case USB_SPEED_FULL:
	case USB_SPEED_LOW:
		usb_host = 1;
		queue_num = 0;
		break;
	default:
		usb_host = 0;
		break;
	}

	if (dev->net->mtu > 12500 && dev->net->mtu <= 16334)
		queue_num = 2; /* For Jumbo packet 16KB */

	memcpy(buf, &AQC111_BULKIN_SIZE[queue_num], 5);
	/* RX bulk configuration */
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_RX_BULKIN_QCTRL, 5, 5, buf);

	/* Set high low water level */
	if (dev->net->mtu <= 4500)
		reg16 = 0x0810;
	else if (dev->net->mtu <= 9500)
		reg16 = 0x1020;
	else if (dev->net->mtu <= 12500)
		reg16 = 0x1420;
	else if (dev->net->mtu <= 16334)
		reg16 = 0x1A20;

	aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_PAUSE_WATERLVL_LOW,
			   2, &reg16);
	netdev_info(dev->net, "Link Speed %d, USB %d", link_speed, usb_host);
}

static void aqc111_configure_csum_offload(struct usbnet *dev)
{
	u8 reg8 = 0;

	if (dev->net->features & NETIF_F_RXCSUM) {
		reg8 |= SFR_RXCOE_IP | SFR_RXCOE_TCP | SFR_RXCOE_UDP |
			SFR_RXCOE_TCPV6 | SFR_RXCOE_UDPV6;
	}
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_RXCOE_CTL, 1, 1, &reg8);

	reg8 = 0;
	if (dev->net->features & NETIF_F_IP_CSUM)
		reg8 |= SFR_TXCOE_IP | SFR_TXCOE_TCP | SFR_TXCOE_UDP;

	if (dev->net->features & NETIF_F_IPV6_CSUM)
		reg8 |= SFR_TXCOE_TCPV6 | SFR_TXCOE_UDPV6;

	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_TXCOE_CTL, 1, 1, &reg8);
}

static int aqc111_link_reset(struct usbnet *dev)
{
	struct aqc111_data *aqc111_data = dev->driver_priv;
	u16 reg16 = 0;
	u8 reg8 = 0;

	if (aqc111_data->link == 1) { /* Link up */
		aqc111_configure_rx(dev, aqc111_data);

		/* Vlan Tag Filter */
		reg8 = SFR_VLAN_CONTROL_VSO;
		if (dev->net->features & NETIF_F_HW_VLAN_CTAG_FILTER)
			reg8 |= SFR_VLAN_CONTROL_VFE;

		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_VLAN_ID_CONTROL,
				 1, 1, &reg8);

		reg8 = 0x0;
		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_BMRX_DMA_CONTROL,
				 1, 1, &reg8);

		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_BMTX_DMA_CONTROL,
				 1, 1, &reg8);

		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_ARC_CTRL, 1, 1, &reg8);

		reg16 = SFR_RX_CTL_IPE | SFR_RX_CTL_AB;
		aqc111_data->rxctl = reg16;
		aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_RX_CTL, 2, &reg16);

		reg8 = SFR_RX_PATH_READY;
		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_ETH_MAC_PATH,
				 1, 1, &reg8);

		reg8 = SFR_BULK_OUT_EFF_EN;
		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_BULK_OUT_CTRL,
				 1, 1, &reg8);

		reg16 = 0;
		aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
				   2, &reg16);

		reg16 = SFR_MEDIUM_XGMIIMODE | SFR_MEDIUM_FULL_DUPLEX;
		aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
				   2, &reg16);

		aqc111_configure_csum_offload(dev);

		aqc111_set_rx_mode(dev->net);

		aqc111_read16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
				  2, &reg16);

		if (dev->net->mtu > 1500)
			reg16 |= SFR_MEDIUM_JUMBO_EN;

		reg16 |= SFR_MEDIUM_RECEIVE_EN | SFR_MEDIUM_RXFLOW_CTRLEN |
			 SFR_MEDIUM_TXFLOW_CTRLEN;
		aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
				   2, &reg16);

		aqc111_data->rxctl |= SFR_RX_CTL_START;
		aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_RX_CTL,
				   2, &aqc111_data->rxctl);

		netif_carrier_on(dev->net);
	} else {
		aqc111_read16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
				  2, &reg16);
		reg16 &= ~SFR_MEDIUM_RECEIVE_EN;
		aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
				   2, &reg16);

		aqc111_data->rxctl &= ~SFR_RX_CTL_START;
		aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_RX_CTL,
				   2, &aqc111_data->rxctl);

		reg8 = SFR_BULK_OUT_FLUSH_EN | SFR_BULK_OUT_EFF_EN;
		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_BULK_OUT_CTRL,
				 1, 1, &reg8);
		reg8 = SFR_BULK_OUT_EFF_EN;
		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_BULK_OUT_CTRL,
				 1, 1, &reg8);

		netif_carrier_off(dev->net);
	}
	return 0;
}

static int aqc111_reset(struct usbnet *dev)
{
	struct aqc111_data *aqc111_data = dev->driver_priv;
	u8 reg8 = 0;

	dev->rx_urb_size = URB_SIZE;

	if (usb_device_no_sg_constraint(dev->udev))
		dev->can_dma_sg = 1;

	dev->net->hw_features |= AQ_SUPPORT_HW_FEATURE;
	dev->net->features |= AQ_SUPPORT_FEATURE;
	dev->net->vlan_features |= AQ_SUPPORT_VLAN_FEATURE;

	/* Power up ethernet PHY */
	aqc111_data->phy_cfg = AQ_PHY_POWER_EN;
	aqc111_write32_cmd(dev, AQ_PHY_OPS, 0, 0,
			   &aqc111_data->phy_cfg);

	/* Set the MAC address */
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_NODE_ID, ETH_ALEN,
			 ETH_ALEN, dev->net->dev_addr);

	reg8 = 0xFF;
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_BM_INT_MASK, 1, 1, &reg8);

	reg8 = 0x0;
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_SWP_CTRL, 1, 1, &reg8);

	aqc111_read_cmd(dev, AQ_ACCESS_MAC, SFR_MONITOR_MODE, 1, 1, &reg8);
	reg8 &= ~(SFR_MONITOR_MODE_EPHYRW | SFR_MONITOR_MODE_RWLC |
		  SFR_MONITOR_MODE_RWMP | SFR_MONITOR_MODE_RWWF |
		  SFR_MONITOR_MODE_RW_FLAG);
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_MONITOR_MODE, 1, 1, &reg8);

	netif_carrier_off(dev->net);

	/* Phy advertise */
	aqc111_set_phy_speed(dev, aqc111_data->autoneg,
			     aqc111_data->advertised_speed);

	return 0;
}

static int aqc111_stop(struct usbnet *dev)
{
	struct aqc111_data *aqc111_data = dev->driver_priv;
	u16 reg16 = 0;

	aqc111_read16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
			  2, &reg16);
	reg16 &= ~SFR_MEDIUM_RECEIVE_EN;
	aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
			   2, &reg16);
	reg16 = 0;
	aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_RX_CTL, 2, &reg16);

	/* Put PHY to low power*/
	aqc111_data->phy_cfg |= AQ_LOW_POWER;
	aqc111_write32_cmd(dev, AQ_PHY_OPS, 0, 0,
			   &aqc111_data->phy_cfg);

	netif_carrier_off(dev->net);

	return 0;
}

static void aqc111_rx_checksum(struct sk_buff *skb, u64 pkt_desc)
{
	u32 pkt_type = 0;

	skb->ip_summed = CHECKSUM_NONE;
	/* checksum error bit is set */
	if (pkt_desc & AQ_RX_PD_L4_ERR || pkt_desc & AQ_RX_PD_L3_ERR)
		return;

	pkt_type = pkt_desc & AQ_RX_PD_L4_TYPE_MASK;
	/* It must be a TCP or UDP packet with a valid checksum */
	if (pkt_type == AQ_RX_PD_L4_TCP || pkt_type == AQ_RX_PD_L4_UDP)
		skb->ip_summed = CHECKSUM_UNNECESSARY;
}

static int aqc111_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	struct aqc111_data *aqc111_data = dev->driver_priv;
	struct sk_buff *new_skb = NULL;
	u32 pkt_total_offset = 0;
	u64 *pkt_desc_ptr = NULL;
	u32 start_of_descs = 0;
	u32 desc_offset = 0; /*RX Header Offset*/
	u16 pkt_count = 0;
	u64 desc_hdr = 0;
	u16 vlan_tag = 0;
	u32 skb_len = 0;

	if (!skb)
		goto err;

	if (skb->len == 0)
		goto err;

	skb_len = skb->len;
	/* RX Descriptor Header */
	skb_trim(skb, skb->len - sizeof(desc_hdr));
	desc_hdr = le64_to_cpup((u64 *)skb_tail_pointer(skb));

	/* Check these packets */
	desc_offset = (desc_hdr & AQ_RX_DH_DESC_OFFSET_MASK) >>
		      AQ_RX_DH_DESC_OFFSET_SHIFT;
	pkt_count = desc_hdr & AQ_RX_DH_PKT_CNT_MASK;
	start_of_descs = skb_len - ((pkt_count + 1) *  sizeof(desc_hdr));

	/* self check descs position */
	if (start_of_descs != desc_offset)
		goto err;

	/* self check desc_offset from header*/
	if (desc_offset >= skb_len)
		goto err;

	if (pkt_count == 0)
		goto err;

	/* Get the first RX packet descriptor */
	pkt_desc_ptr = (u64 *)(skb->data + desc_offset);

	while (pkt_count--) {
		u64 pkt_desc = le64_to_cpup(pkt_desc_ptr);
		u32 pkt_len_with_padd = 0;
		u32 pkt_len = 0;

		pkt_len = (u32)((pkt_desc & AQ_RX_PD_LEN_MASK) >>
			  AQ_RX_PD_LEN_SHIFT);
		pkt_len_with_padd = ((pkt_len + 7) & 0x7FFF8);

		pkt_total_offset += pkt_len_with_padd;
		if (pkt_total_offset > desc_offset ||
		    (pkt_count == 0 && pkt_total_offset != desc_offset)) {
			goto err;
		}

		if (pkt_desc & AQ_RX_PD_DROP ||
		    !(pkt_desc & AQ_RX_PD_RX_OK) ||
		    pkt_len > (dev->hard_mtu + AQ_RX_HW_PAD)) {
			skb_pull(skb, pkt_len_with_padd);
			/* Next RX Packet Descriptor */
			pkt_desc_ptr++;
			continue;
		}

		/* Clone SKB */
		new_skb = skb_clone(skb, GFP_ATOMIC);

		if (!new_skb)
			goto err;

		new_skb->len = pkt_len;
		skb_pull(new_skb, AQ_RX_HW_PAD);
		skb_set_tail_pointer(new_skb, new_skb->len);

		new_skb->truesize = SKB_TRUESIZE(new_skb->len);
		if (aqc111_data->rx_checksum)
			aqc111_rx_checksum(new_skb, pkt_desc);

		if (pkt_desc & AQ_RX_PD_VLAN) {
			vlan_tag = pkt_desc >> AQ_RX_PD_VLAN_SHIFT;
			__vlan_hwaccel_put_tag(new_skb, htons(ETH_P_8021Q),
					       vlan_tag & VLAN_VID_MASK);
		}

		usbnet_skb_return(dev, new_skb);
		if (pkt_count == 0)
			break;

		skb_pull(skb, pkt_len_with_padd);

		/* Next RX Packet Header */
		pkt_desc_ptr++;

		new_skb = NULL;
	}

	return 1;

err:
	return 0;
}

static struct sk_buff *aqc111_tx_fixup(struct usbnet *dev, struct sk_buff *skb,
				       gfp_t flags)
{
	int frame_size = dev->maxpacket;
	struct sk_buff *new_skb = NULL;
	u64 *tx_desc_ptr = NULL;
	int padding_size = 0;
	int headroom = 0;
	int tailroom = 0;
	u64 tx_desc = 0;
	u16 tci = 0;

	/*Length of actual data*/
	tx_desc |= skb->len & AQ_TX_DESC_LEN_MASK;

	/* TSO MSS */
	tx_desc |= ((u64)(skb_shinfo(skb)->gso_size & AQ_TX_DESC_MSS_MASK)) <<
		   AQ_TX_DESC_MSS_SHIFT;

	headroom = (skb->len + sizeof(tx_desc)) % 8;
	if (headroom != 0)
		padding_size = 8 - headroom;

	if (((skb->len + sizeof(tx_desc) + padding_size) % frame_size) == 0) {
		padding_size += 8;
		tx_desc |= AQ_TX_DESC_DROP_PADD;
	}

	/* Vlan Tag */
	if (vlan_get_tag(skb, &tci) >= 0) {
		tx_desc |= AQ_TX_DESC_VLAN;
		tx_desc |= ((u64)tci & AQ_TX_DESC_VLAN_MASK) <<
			   AQ_TX_DESC_VLAN_SHIFT;
	}

	if (!dev->can_dma_sg && (dev->net->features & NETIF_F_SG) &&
	    skb_linearize(skb))
		return NULL;

	headroom = skb_headroom(skb);
	tailroom = skb_tailroom(skb);

	if (!(headroom >= sizeof(tx_desc) && tailroom >= padding_size)) {
		new_skb = skb_copy_expand(skb, sizeof(tx_desc),
					  padding_size, flags);
		dev_kfree_skb_any(skb);
		skb = new_skb;
		if (!skb)
			return NULL;
	}
	if (padding_size != 0)
		skb_put_zero(skb, padding_size);
	/* Copy TX header */
	tx_desc_ptr = skb_push(skb, sizeof(tx_desc));
	*tx_desc_ptr = cpu_to_le64(tx_desc);

	usbnet_set_skb_tx_stats(skb, 1, 0);

	return skb;
}

static const struct driver_info aqc111_info = {
	.description	= "Aquantia AQtion USB to 5GbE Controller",
	.bind		= aqc111_bind,
	.unbind		= aqc111_unbind,
	.status		= aqc111_status,
	.link_reset	= aqc111_link_reset,
	.reset		= aqc111_reset,
	.stop		= aqc111_stop,
	.flags		= FLAG_ETHER | FLAG_FRAMING_AX |
			  FLAG_AVOID_UNLINK_URBS | FLAG_MULTI_PACKET,
	.rx_fixup	= aqc111_rx_fixup,
	.tx_fixup	= aqc111_tx_fixup,
};

#define ASIX111_DESC \
"ASIX USB 3.1 Gen1 to 5G Multi-Gigabit Ethernet Adapter"

static const struct driver_info asix111_info = {
	.description	= ASIX111_DESC,
	.bind		= aqc111_bind,
	.unbind		= aqc111_unbind,
	.status		= aqc111_status,
	.link_reset	= aqc111_link_reset,
	.reset		= aqc111_reset,
	.stop		= aqc111_stop,
	.flags		= FLAG_ETHER | FLAG_FRAMING_AX |
			  FLAG_AVOID_UNLINK_URBS | FLAG_MULTI_PACKET,
	.rx_fixup	= aqc111_rx_fixup,
	.tx_fixup	= aqc111_tx_fixup,
};

#undef ASIX111_DESC

#define ASIX112_DESC \
"ASIX USB 3.1 Gen1 to 2.5G Multi-Gigabit Ethernet Adapter"

static const struct driver_info asix112_info = {
	.description	= ASIX112_DESC,
	.bind		= aqc111_bind,
	.unbind		= aqc111_unbind,
	.status		= aqc111_status,
	.link_reset	= aqc111_link_reset,
	.reset		= aqc111_reset,
	.stop		= aqc111_stop,
	.flags		= FLAG_ETHER | FLAG_FRAMING_AX |
			  FLAG_AVOID_UNLINK_URBS | FLAG_MULTI_PACKET,
	.rx_fixup	= aqc111_rx_fixup,
	.tx_fixup	= aqc111_tx_fixup,
};

#undef ASIX112_DESC

static const struct driver_info trendnet_info = {
	.description	= "USB-C 3.1 to 5GBASE-T Ethernet Adapter",
	.bind		= aqc111_bind,
	.unbind		= aqc111_unbind,
	.status		= aqc111_status,
	.link_reset	= aqc111_link_reset,
	.reset		= aqc111_reset,
	.stop		= aqc111_stop,
	.flags		= FLAG_ETHER | FLAG_FRAMING_AX |
			  FLAG_AVOID_UNLINK_URBS | FLAG_MULTI_PACKET,
	.rx_fixup	= aqc111_rx_fixup,
	.tx_fixup	= aqc111_tx_fixup,
};

static const struct driver_info qnap_info = {
	.description	= "QNAP QNA-UC5G1T USB to 5GbE Adapter",
	.bind		= aqc111_bind,
	.unbind		= aqc111_unbind,
	.status		= aqc111_status,
	.link_reset	= aqc111_link_reset,
	.reset		= aqc111_reset,
	.stop		= aqc111_stop,
	.flags		= FLAG_ETHER | FLAG_FRAMING_AX |
			  FLAG_AVOID_UNLINK_URBS | FLAG_MULTI_PACKET,
	.rx_fixup	= aqc111_rx_fixup,
	.tx_fixup	= aqc111_tx_fixup,
};

static int aqc111_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usbnet *dev = usb_get_intfdata(intf);
	struct aqc111_data *aqc111_data = dev->driver_priv;
	u16 temp_rx_ctrl = 0x00;
	u16 reg16;
	u8 reg8;

	usbnet_suspend(intf, message);

	aqc111_read16_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_RX_CTL, 2, &reg16);
	temp_rx_ctrl = reg16;
	/* Stop RX operations*/
	reg16 &= ~SFR_RX_CTL_START;
	aqc111_write16_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_RX_CTL, 2, &reg16);
	/* Force bz */
	aqc111_read16_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_PHYPWR_RSTCTL,
			       2, &reg16);
	reg16 |= SFR_PHYPWR_RSTCTL_BZ;
	aqc111_write16_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_PHYPWR_RSTCTL,
				2, &reg16);

	reg8 = SFR_BULK_OUT_EFF_EN;
	aqc111_write_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_BULK_OUT_CTRL,
			      1, 1, &reg8);

	temp_rx_ctrl &= ~(SFR_RX_CTL_START | SFR_RX_CTL_RF_WAK |
			  SFR_RX_CTL_AP | SFR_RX_CTL_AM);
	aqc111_write16_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_RX_CTL,
				2, &temp_rx_ctrl);

	reg8 = 0x00;
	aqc111_write_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_ETH_MAC_PATH,
			      1, 1, &reg8);

	if (aqc111_data->wol_flags) {
		struct aqc111_wol_cfg wol_cfg;

		memset(&wol_cfg, 0, sizeof(struct aqc111_wol_cfg));

		aqc111_data->phy_cfg |= AQ_WOL;
		ether_addr_copy(wol_cfg.hw_addr, dev->net->dev_addr);
		wol_cfg.flags = aqc111_data->wol_flags;

		temp_rx_ctrl |= (SFR_RX_CTL_AB | SFR_RX_CTL_START);
		aqc111_write16_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_RX_CTL,
					2, &temp_rx_ctrl);
		reg8 = 0x00;
		aqc111_write_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_BM_INT_MASK,
				      1, 1, &reg8);
		reg8 = SFR_BMRX_DMA_EN;
		aqc111_write_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_BMRX_DMA_CONTROL,
				      1, 1, &reg8);
		reg8 = SFR_RX_PATH_READY;
		aqc111_write_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_ETH_MAC_PATH,
				      1, 1, &reg8);
		reg8 = 0x07;
		aqc111_write_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_RX_BULKIN_QCTRL,
				      1, 1, &reg8);
		reg8 = 0x00;
		aqc111_write_cmd_nopm(dev, AQ_ACCESS_MAC,
				      SFR_RX_BULKIN_QTIMR_LOW, 1, 1, &reg8);
		aqc111_write_cmd_nopm(dev, AQ_ACCESS_MAC,
				      SFR_RX_BULKIN_QTIMR_HIGH, 1, 1, &reg8);
		reg8 = 0xFF;
		aqc111_write_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_RX_BULKIN_QSIZE,
				      1, 1, &reg8);
		aqc111_write_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_RX_BULKIN_QIFG,
				      1, 1, &reg8);

		aqc111_read16_cmd_nopm(dev, AQ_ACCESS_MAC,
				       SFR_MEDIUM_STATUS_MODE, 2, &reg16);
		reg16 |= SFR_MEDIUM_RECEIVE_EN;
		aqc111_write16_cmd_nopm(dev, AQ_ACCESS_MAC,
					SFR_MEDIUM_STATUS_MODE, 2, &reg16);

		aqc111_write_cmd(dev, AQ_WOL_CFG, 0, 0,
				 WOL_CFG_SIZE, &wol_cfg);
		aqc111_write32_cmd(dev, AQ_PHY_OPS, 0, 0,
				   &aqc111_data->phy_cfg);
	} else {
		aqc111_data->phy_cfg |= AQ_LOW_POWER;
		aqc111_write32_cmd(dev, AQ_PHY_OPS, 0, 0,
				   &aqc111_data->phy_cfg);

		/* Disable RX path */
		aqc111_read16_cmd_nopm(dev, AQ_ACCESS_MAC,
				       SFR_MEDIUM_STATUS_MODE, 2, &reg16);
		reg16 &= ~SFR_MEDIUM_RECEIVE_EN;
		aqc111_write16_cmd_nopm(dev, AQ_ACCESS_MAC,
					SFR_MEDIUM_STATUS_MODE, 2, &reg16);
	}

	return 0;
}

static int aqc111_resume(struct usb_interface *intf)
{
	struct usbnet *dev = usb_get_intfdata(intf);
	struct aqc111_data *aqc111_data = dev->driver_priv;
	u16 reg16;
	u8 reg8;

	netif_carrier_off(dev->net);

	/* Power up ethernet PHY */
	aqc111_data->phy_cfg |= AQ_PHY_POWER_EN;
	aqc111_data->phy_cfg &= ~AQ_LOW_POWER;
	aqc111_data->phy_cfg &= ~AQ_WOL;

	reg8 = 0xFF;
	aqc111_write_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_BM_INT_MASK,
			      1, 1, &reg8);
	/* Configure RX control register => start operation */
	reg16 = aqc111_data->rxctl;
	reg16 &= ~SFR_RX_CTL_START;
	aqc111_write16_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_RX_CTL, 2, &reg16);

	reg16 |= SFR_RX_CTL_START;
	aqc111_write16_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_RX_CTL, 2, &reg16);

	aqc111_set_phy_speed(dev, aqc111_data->autoneg,
			     aqc111_data->advertised_speed);

	aqc111_read16_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
			       2, &reg16);
	reg16 |= SFR_MEDIUM_RECEIVE_EN;
	aqc111_write16_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
				2, &reg16);
	reg8 = SFR_RX_PATH_READY;
	aqc111_write_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_ETH_MAC_PATH,
			      1, 1, &reg8);
	reg8 = 0x0;
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_BMRX_DMA_CONTROL, 1, 1, &reg8);

	return usbnet_resume(intf);
}

#define AQC111_USB_ETH_DEV(vid, pid, table) \
	USB_DEVICE_INTERFACE_CLASS((vid), (pid), USB_CLASS_VENDOR_SPEC), \
	.driver_info = (unsigned long)&(table) \
}, \
{ \
	USB_DEVICE_AND_INTERFACE_INFO((vid), (pid), \
				      USB_CLASS_COMM, \
				      USB_CDC_SUBCLASS_ETHERNET, \
				      USB_CDC_PROTO_NONE), \
	.driver_info = (unsigned long)&(table),

static const struct usb_device_id products[] = {
	{AQC111_USB_ETH_DEV(0x2eca, 0xc101, aqc111_info)},
	{AQC111_USB_ETH_DEV(0x0b95, 0x2790, asix111_info)},
	{AQC111_USB_ETH_DEV(0x0b95, 0x2791, asix112_info)},
	{AQC111_USB_ETH_DEV(0x20f4, 0xe05a, trendnet_info)},
	{AQC111_USB_ETH_DEV(0x1c04, 0x0015, qnap_info)},
	{ },/* END */
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver aq_driver = {
	.name		= "aqc111",
	.id_table	= products,
	.probe		= usbnet_probe,
	.suspend	= aqc111_suspend,
	.resume		= aqc111_resume,
	.disconnect	= usbnet_disconnect,
};

module_usb_driver(aq_driver);

MODULE_DESCRIPTION("Aquantia AQtion USB to 5/2.5GbE Controllers");
MODULE_LICENSE("GPL");
