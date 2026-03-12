/*
 * CoreChip-sz SR9700 one chip USB 1.1 Ethernet Devices
 *
 * Author : Liu Junliang <liujunliang_ljl@163.com>
 *
 * Based on dm9601.c
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/stddef.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/usb.h>
#include <linux/usb/usbnet.h>

#include "sr9700.h"

static int sr_read(struct usbnet *dev, u8 reg, u16 length, void *data)
{
	int err;

	err = usbnet_read_cmd(dev, SR_RD_REGS, SR_REQ_RD_REG, 0, reg, data,
			      length);
	if ((err != length) && (err >= 0))
		err = -EINVAL;
	return err;
}

static int sr_write(struct usbnet *dev, u8 reg, u16 length, void *data)
{
	int err;

	err = usbnet_write_cmd(dev, SR_WR_MULTIPLE_REGS, SR_REQ_WR_REG, 0, reg, data,
			       length);
	if ((err >= 0) && (err < length))
		err = -EINVAL;
	return err;
}

static int sr_read_reg(struct usbnet *dev, u8 reg, u8 *value)
{
	return sr_read(dev, reg, 1, value);
}

static int sr_write_reg(struct usbnet *dev, u8 reg, u8 value)
{
	return usbnet_write_cmd(dev, SR_WR_SINGLE_REG, SR_REQ_WR_REG,
				value, reg, NULL, 0);
}

static void sr_write_async(struct usbnet *dev, u8 reg, u16 length,
			   const void *data)
{
	usbnet_write_cmd_async(dev, SR_WR_MULTIPLE_REGS, SR_REQ_WR_REG,
			       0, reg, data, length);
}

static void sr_write_reg_async(struct usbnet *dev, u8 reg, u8 value)
{
	usbnet_write_cmd_async(dev, SR_WR_SINGLE_REG, SR_REQ_WR_REG,
			       value, reg, NULL, 0);
}

static int wait_eeprom_ready(struct usbnet *dev)
{
	int i;

	for (i = 0; i < SR_EEPROM_TIMEOUT; i++) {
		u8 tmp = 0;
		int ret;

		udelay(1);
		ret = sr_read_reg(dev, SR_EPCR, &tmp);
		if (ret < 0)
			return ret;

		/* ready */
		if (!(tmp & EPCR_ERRE))
			return 0;
	}

	netdev_err(dev->net, "eeprom write timed out!\n");

	return -EIO;
}

static int sr_read_eeprom_word(struct usbnet *dev, u8 reg, __le16 *value)
{
	int ret;

	mutex_lock(&dev->phy_mutex);

	sr_write_reg(dev, SR_EPAR, reg);
	sr_write_reg(dev, SR_EPCR, EPCR_ERPRR);

	ret = wait_eeprom_ready(dev);
	if (ret < 0)
		goto out_unlock;

	sr_write_reg(dev, SR_EPCR, 0x0);
	ret = sr_read(dev, SR_EPDR, 2, value);

	netdev_dbg(dev->net, "read eeprom 0x%02x returned 0x%04x, %d\n",
		   reg, *value, ret);

out_unlock:
	mutex_unlock(&dev->phy_mutex);
	return ret;
}

static int __maybe_unused sr_write_eeprom_word(struct usbnet *dev, u8 reg,
					       __le16 value)
{
	int ret;

	mutex_lock(&dev->phy_mutex);

	ret = sr_write(dev, SR_EPDR, 2, &value);
	if (ret < 0)
		goto out_unlock;

	sr_write_reg(dev, SR_EPAR, reg);
	sr_write_reg(dev, SR_EPCR, EPCR_WEP | EPCR_ERPRW);

	ret = wait_eeprom_ready(dev);
	if (ret < 0)
		goto out_unlock;

	sr_write_reg(dev, SR_EPCR, 0x0);

out_unlock:
	mutex_unlock(&dev->phy_mutex);
	return ret;
}

static int sr9700_get_eeprom_len(struct net_device *netdev)
{
	return SR_EEPROM_LEN;
}

static int sr9700_get_eeprom(struct net_device *netdev,
			     struct ethtool_eeprom *eeprom, u8 *data)
{
	struct usbnet *dev = netdev_priv(netdev);
	__le16 *buf = (__le16 *)data;
	int ret = 0;
	int i;

	/* access is 16bit */
	if ((eeprom->offset & 0x01) || (eeprom->len & 0x01))
		return -EINVAL;

	for (i = 0; i < eeprom->len / 2; i++) {
		ret = sr_read_eeprom_word(dev, eeprom->offset / 2 + i, buf + i);
		if (ret < 0)
			break;
	}

	return ret;
}

static void sr9700_handle_link_change(struct net_device *netdev, bool link)
{
	if (netif_carrier_ok(netdev) != link) {
		if (link) {
			netif_carrier_on(netdev);
			netdev_info(netdev, "link up, 10Mbps, half-duplex\n");
		} else {
			netif_carrier_off(netdev);
			netdev_info(netdev, "link down\n");
		}
	}
}

static u32 sr9700_get_link(struct net_device *netdev)
{
	struct usbnet *dev = netdev_priv(netdev);
	u8 value = 0;
	u32 link = 0;

	sr_read_reg(dev, SR_NSR, &value);
	link = !!(value & NSR_LINKST);

	sr9700_handle_link_change(netdev, link);

	return link;
}

/*
 * The device supports only 10Mbps half-duplex operation. It implements the
 * DM9601 speed/duplex status registers, but as the values are always the same,
 * using them would add unnecessary complexity.
 */
static int sr9700_get_link_ksettings(struct net_device *dev,
				     struct ethtool_link_ksettings *cmd)
{
	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 10baseT_Half);
	ethtool_link_ksettings_add_link_mode(cmd, supported, TP);

	ethtool_link_ksettings_zero_link_mode(cmd, advertising);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, 10baseT_Half);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, TP);

	cmd->base.speed = SPEED_10;
	cmd->base.duplex = DUPLEX_HALF;
	cmd->base.port = PORT_TP;
	cmd->base.phy_address = 0;
	cmd->base.autoneg = AUTONEG_DISABLE;

	return 0;
}

static const struct ethtool_ops sr9700_ethtool_ops = {
	.get_drvinfo	= usbnet_get_drvinfo,
	.get_link	= sr9700_get_link,
	.get_msglevel	= usbnet_get_msglevel,
	.set_msglevel	= usbnet_set_msglevel,
	.get_eeprom_len	= sr9700_get_eeprom_len,
	.get_eeprom	= sr9700_get_eeprom,
	.get_link_ksettings	= sr9700_get_link_ksettings,
};

static void sr9700_set_multicast(struct net_device *netdev)
{
	struct usbnet *dev = netdev_priv(netdev);
	/* rx_ctl setting : enable, disable_long, disable_crc */
	u8 rx_ctl = RCR_RXEN | RCR_DIS_CRC | RCR_DIS_LONG;

	if (netdev->flags & IFF_PROMISC)
		rx_ctl |= RCR_PRMSC;
	else if (netdev->flags & IFF_ALLMULTI || !netdev_mc_empty(netdev))
		/* The chip has no multicast filter */
		rx_ctl |= RCR_ALL;

	sr_write_reg_async(dev, SR_RCR, rx_ctl);
}

static int sr9700_set_mac_address(struct net_device *netdev, void *p)
{
	struct usbnet *dev = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data)) {
		netdev_err(netdev, "not setting invalid mac address %pM\n",
			   addr->sa_data);
		return -EINVAL;
	}

	eth_hw_addr_set(netdev, addr->sa_data);
	sr_write_async(dev, SR_PAR, ETH_ALEN, netdev->dev_addr);

	return 0;
}

static const struct net_device_ops sr9700_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
	.ndo_start_xmit		= usbnet_start_xmit,
	.ndo_tx_timeout		= usbnet_tx_timeout,
	.ndo_change_mtu		= usbnet_change_mtu,
	.ndo_get_stats64	= dev_get_tstats64,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= sr9700_set_multicast,
	.ndo_set_mac_address	= sr9700_set_mac_address,
};

static int sr9700_bind(struct usbnet *dev, struct usb_interface *intf)
{
	struct net_device *netdev;
	u8 addr[ETH_ALEN];
	int ret;

	ret = usbnet_get_endpoints(dev, intf);
	if (ret)
		goto out;

	netdev = dev->net;

	netdev->netdev_ops = &sr9700_netdev_ops;
	netdev->ethtool_ops = &sr9700_ethtool_ops;
	netdev->hard_header_len += SR_TX_OVERHEAD;
	dev->hard_mtu = netdev->mtu + netdev->hard_header_len;
	/* bulkin buffer is preferably not less than 3K */
	dev->rx_urb_size = 3072;

	sr_write_reg(dev, SR_NCR, NCR_RST);
	udelay(20);

	/* read MAC
	 * After Chip Power on, the Chip will reload the MAC from
	 * EEPROM automatically to PAR. In case there is no EEPROM externally,
	 * a default MAC address is stored in PAR for making chip work properly.
	 */
	if (sr_read(dev, SR_PAR, ETH_ALEN, addr) < 0) {
		netdev_err(netdev, "Error reading MAC address\n");
		ret = -ENODEV;
		goto out;
	}
	eth_hw_addr_set(netdev, addr);

	/* power up and reset phy */
	sr_write_reg(dev, SR_PRR, PRR_PHY_RST);
	/* at least 10ms, here 20ms for safe */
	msleep(20);
	sr_write_reg(dev, SR_PRR, 0);
	/* at least 1ms, here 2ms for reading right register */
	udelay(2 * 1000);

	/* receive broadcast packets */
	sr9700_set_multicast(netdev);

out:
	return ret;
}

static int sr9700_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	struct sk_buff *sr_skb;
	int len;

	/* skb content (packets) format :
	 *                    p1            p2            p3    ......    pn
	 *                 /      \
	 *            /                \
	 *        /                            \
	 *  /                                        \
	 * p1b1 p1b2 p1b3 p1b4 ...... p1b(n-4) p1b(n-3)...p1bn
	 *
	 * p1 : packet 1
	 * p1b1 : packet 1 byte 1
	 *
	 * b1: rx status
	 * b2: packet length (incl crc) low
	 * b3: packet length (incl crc) high
	 * b4..n-4: packet data
	 * bn-3..bn: ethernet packet crc
	 */
	if (unlikely(skb->len < SR_RX_OVERHEAD)) {
		netdev_err(dev->net, "unexpected tiny rx frame\n");
		return 0;
	}

	/* one skb may contains multiple packets */
	while (skb->len > SR_RX_OVERHEAD) {
		if (skb->data[0] != RSR_MF)
			return 0;

		/* ignore the CRC length */
		len = (skb->data[1] | (skb->data[2] << 8)) - 4;

		if (len > ETH_FRAME_LEN || len > skb->len || len < 0)
			return 0;

		/* the last packet of current skb */
		if (skb->len == (len + SR_RX_OVERHEAD))	{
			skb_pull(skb, 3);
			skb->len = len;
			skb_set_tail_pointer(skb, len);
			return 2;
		}

		sr_skb = netdev_alloc_skb_ip_align(dev->net, len);
		if (!sr_skb)
			return 0;

		skb_put(sr_skb, len);
		memcpy(sr_skb->data, skb->data + 3, len);
		usbnet_skb_return(dev, sr_skb);

		skb_pull(skb, len + SR_RX_OVERHEAD);
	}

	return 0;
}

static struct sk_buff *sr9700_tx_fixup(struct usbnet *dev, struct sk_buff *skb,
				       gfp_t flags)
{
	int len;

	/* SR9700 can only send out one ethernet packet at once.
	 *
	 * b1 b2 b3 b4 ...... b(n-4) b(n-3)...bn
	 *
	 * b1: rx status
	 * b2: packet length (incl crc) low
	 * b3: packet length (incl crc) high
	 * b4..n-4: packet data
	 * bn-3..bn: ethernet packet crc
	 */

	len = skb->len;

	if (skb_cow_head(skb, SR_TX_OVERHEAD)) {
		dev_kfree_skb_any(skb);
		return NULL;
	}

	__skb_push(skb, SR_TX_OVERHEAD);

	/* usbnet adds padding if length is a multiple of packet size
	 * if so, adjust length value in header
	 */
	if ((skb->len % dev->maxpacket) == 0)
		len++;

	skb->data[0] = len;
	skb->data[1] = len >> 8;

	return skb;
}

static void sr9700_status(struct usbnet *dev, struct urb *urb)
{
	bool link;
	u8 *buf;

	/* format:
	   b1: net status
	   b2: tx status 1
	   b3: tx status 2
	   b4: rx status
	   b5: rx overflow
	   b6: rx count
	   b7: tx count
	   b8: gpr
	*/

	if (urb->actual_length < 8)
		return;

	buf = urb->transfer_buffer;

	link = !!(buf[0] & NSR_LINKST);
	sr9700_handle_link_change(dev->net, link);
}

static const struct driver_info sr9700_driver_info = {
	.description	= "CoreChip SR9700 USB Ethernet",
	.flags		= FLAG_ETHER,
	.bind		= sr9700_bind,
	.rx_fixup	= sr9700_rx_fixup,
	.tx_fixup	= sr9700_tx_fixup,
	.status		= sr9700_status,
};

static const struct usb_device_id products[] = {
	{
		USB_DEVICE(0x0fe6, 0x9700),	/* SR9700 device */
		.driver_info = (unsigned long)&sr9700_driver_info,
	},
	{
		/* SR9700 with virtual driver CD-ROM - interface 0 is the CD-ROM device */
		USB_DEVICE_INTERFACE_NUMBER(0x0fe6, 0x9702, 1),
		.driver_info = (unsigned long)&sr9700_driver_info,
	},
	{},			/* END */
};

MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver sr9700_usb_driver = {
	.name		= "sr9700",
	.id_table	= products,
	.probe		= usbnet_probe,
	.disconnect	= usbnet_disconnect,
	.suspend	= usbnet_suspend,
	.resume		= usbnet_resume,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(sr9700_usb_driver);

MODULE_AUTHOR("liujl <liujunliang_ljl@163.com>");
MODULE_DESCRIPTION("SR9700 one chip USB 1.1 USB to Ethernet device from http://www.corechip-sz.com/");
MODULE_LICENSE("GPL");
