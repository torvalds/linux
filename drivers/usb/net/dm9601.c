/*
 * Davicom DM9601 USB 1.1 10/100Mbps ethernet devices
 *
 * Peter Korsgaard <jacmet@sunsite.dk>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

//#define DEBUG

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/crc32.h>

#include "usbnet.h"

/* datasheet:
 http://www.davicom.com.tw/big5/download/Data%20Sheet/DM9601-DS-P01-930914.pdf
*/

/* control requests */
#define DM_READ_REGS	0x00
#define DM_WRITE_REGS	0x01
#define DM_READ_MEMS	0x02
#define DM_WRITE_REG	0x03
#define DM_WRITE_MEMS	0x05
#define DM_WRITE_MEM	0x07

/* registers */
#define DM_NET_CTRL	0x00
#define DM_RX_CTRL	0x05
#define DM_SHARED_CTRL	0x0b
#define DM_SHARED_ADDR	0x0c
#define DM_SHARED_DATA	0x0d	/* low + high */
#define DM_PHY_ADDR	0x10	/* 6 bytes */
#define DM_MCAST_ADDR	0x16	/* 8 bytes */
#define DM_GPR_CTRL	0x1e
#define DM_GPR_DATA	0x1f

#define DM_MAX_MCAST	64
#define DM_MCAST_SIZE	8
#define DM_EEPROM_LEN	256
#define DM_TX_OVERHEAD	2	/* 2 byte header */
#define DM_RX_OVERHEAD	7	/* 3 byte header + 4 byte crc tail */
#define DM_TIMEOUT	1000


static int dm_read(struct usbnet *dev, u8 reg, u16 length, void *data)
{
	devdbg(dev, "dm_read() reg=0x%02x length=%d", reg, length);
	return usb_control_msg(dev->udev,
			       usb_rcvctrlpipe(dev->udev, 0),
			       DM_READ_REGS,
			       USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			       0, reg, data, length, USB_CTRL_SET_TIMEOUT);
}

static int dm_read_reg(struct usbnet *dev, u8 reg, u8 *value)
{
	return dm_read(dev, reg, 1, value);
}

static int dm_write(struct usbnet *dev, u8 reg, u16 length, void *data)
{
	devdbg(dev, "dm_write() reg=0x%02x, length=%d", reg, length);
	return usb_control_msg(dev->udev,
			       usb_sndctrlpipe(dev->udev, 0),
			       DM_WRITE_REGS,
			       USB_DIR_OUT | USB_TYPE_VENDOR |USB_RECIP_DEVICE,
			       0, reg, data, length, USB_CTRL_SET_TIMEOUT);
}

static int dm_write_reg(struct usbnet *dev, u8 reg, u8 value)
{
	devdbg(dev, "dm_write_reg() reg=0x%02x, value=0x%02x", reg, value);
	return usb_control_msg(dev->udev,
			       usb_sndctrlpipe(dev->udev, 0),
			       DM_WRITE_REG,
			       USB_DIR_OUT | USB_TYPE_VENDOR |USB_RECIP_DEVICE,
			       value, reg, 0, 0, USB_CTRL_SET_TIMEOUT);
}

static void dm_write_async_callback(struct urb *urb)
{
	struct usb_ctrlrequest *req = (struct usb_ctrlrequest *)urb->context;

	if (urb->status < 0)
		printk(KERN_DEBUG "dm_write_async_callback() failed with %d",
		       urb->status);

	kfree(req);
	usb_free_urb(urb);
}

static void dm_write_async(struct usbnet *dev, u8 reg, u16 length, void *data)
{
	struct usb_ctrlrequest *req;
	struct urb *urb;
	int status;

	devdbg(dev, "dm_write_async() reg=0x%02x length=%d", reg, length);

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		deverr(dev, "Error allocating URB in dm_write_async!");
		return;
	}

	req = kmalloc(sizeof(struct usb_ctrlrequest), GFP_ATOMIC);
	if (!req) {
		deverr(dev, "Failed to allocate memory for control request");
		usb_free_urb(urb);
		return;
	}

	req->bRequestType = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE;
	req->bRequest = DM_WRITE_REGS;
	req->wValue = 0;
	req->wIndex = cpu_to_le16(reg);
	req->wLength = cpu_to_le16(length);

	usb_fill_control_urb(urb, dev->udev,
			     usb_sndctrlpipe(dev->udev, 0),
			     (void *)req, data, length,
			     dm_write_async_callback, req);

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status < 0) {
		deverr(dev, "Error submitting the control message: status=%d",
		       status);
		kfree(req);
		usb_free_urb(urb);
	}
}

static void dm_write_reg_async(struct usbnet *dev, u8 reg, u8 value)
{
	struct usb_ctrlrequest *req;
	struct urb *urb;
	int status;

	devdbg(dev, "dm_write_reg_async() reg=0x%02x value=0x%02x",
	       reg, value);

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		deverr(dev, "Error allocating URB in dm_write_async!");
		return;
	}

	req = kmalloc(sizeof(struct usb_ctrlrequest), GFP_ATOMIC);
	if (!req) {
		deverr(dev, "Failed to allocate memory for control request");
		usb_free_urb(urb);
		return;
	}

	req->bRequestType = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE;
	req->bRequest = DM_WRITE_REG;
	req->wValue = cpu_to_le16(value);
	req->wIndex = cpu_to_le16(reg);
	req->wLength = 0;

	usb_fill_control_urb(urb, dev->udev,
			     usb_sndctrlpipe(dev->udev, 0),
			     (void *)req, 0, 0, dm_write_async_callback, req);

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status < 0) {
		deverr(dev, "Error submitting the control message: status=%d",
		       status);
		kfree(req);
		usb_free_urb(urb);
	}
}

static int dm_read_shared_word(struct usbnet *dev, int phy, u8 reg, u16 *value)
{
	int ret, i;

	mutex_lock(&dev->phy_mutex);

	dm_write_reg(dev, DM_SHARED_ADDR, phy ? (reg | 0x40) : reg);
	dm_write_reg(dev, DM_SHARED_CTRL, phy ? 0xc : 0x4);

	for (i = 0; i < DM_TIMEOUT; i++) {
		u8 tmp;

		udelay(1);
		ret = dm_read_reg(dev, DM_SHARED_CTRL, &tmp);
		if (ret < 0)
			goto out;

		/* ready */
		if ((tmp & 1) == 0)
			break;
	}

	if (i == DM_TIMEOUT) {
		deverr(dev, "%s read timed out!", phy ? "phy" : "eeprom");
		ret = -EIO;
		goto out;
	}

	dm_write_reg(dev, DM_SHARED_CTRL, 0x0);
	ret = dm_read(dev, DM_SHARED_DATA, 2, value);

	devdbg(dev, "read shared %d 0x%02x returned 0x%04x, %d",
	       phy, reg, *value, ret);

 out:
	mutex_unlock(&dev->phy_mutex);
	return ret;
}

static int dm_write_shared_word(struct usbnet *dev, int phy, u8 reg, u16 value)
{
	int ret, i;

	mutex_lock(&dev->phy_mutex);

	ret = dm_write(dev, DM_SHARED_DATA, 2, &value);
	if (ret < 0)
		goto out;

	dm_write_reg(dev, DM_SHARED_ADDR, phy ? (reg | 0x40) : reg);
	dm_write_reg(dev, DM_SHARED_CTRL, phy ? 0x1c : 0x14);

	for (i = 0; i < DM_TIMEOUT; i++) {
		u8 tmp;

		udelay(1);
		ret = dm_read_reg(dev, DM_SHARED_CTRL, &tmp);
		if (ret < 0)
			goto out;

		/* ready */
		if ((tmp & 1) == 0)
			break;
	}

	if (i == DM_TIMEOUT) {
		deverr(dev, "%s write timed out!", phy ? "phy" : "eeprom");
		ret = -EIO;
		goto out;
	}

	dm_write_reg(dev, DM_SHARED_CTRL, 0x0);

out:
	mutex_unlock(&dev->phy_mutex);
	return ret;
}

static int dm_read_eeprom_word(struct usbnet *dev, u8 offset, void *value)
{
	return dm_read_shared_word(dev, 0, offset, value);
}



static int dm9601_get_eeprom_len(struct net_device *dev)
{
	return DM_EEPROM_LEN;
}

static int dm9601_get_eeprom(struct net_device *net,
			     struct ethtool_eeprom *eeprom, u8 * data)
{
	struct usbnet *dev = netdev_priv(net);
	u16 *ebuf = (u16 *) data;
	int i;

	/* access is 16bit */
	if ((eeprom->offset % 2) || (eeprom->len % 2))
		return -EINVAL;

	for (i = 0; i < eeprom->len / 2; i++) {
		if (dm_read_eeprom_word(dev, eeprom->offset / 2 + i,
					&ebuf[i]) < 0)
			return -EINVAL;
	}
	return 0;
}

static int dm9601_mdio_read(struct net_device *netdev, int phy_id, int loc)
{
	struct usbnet *dev = netdev_priv(netdev);

	u16 res;

	if (phy_id) {
		devdbg(dev, "Only internal phy supported");
		return 0;
	}

	dm_read_shared_word(dev, 1, loc, &res);

	devdbg(dev,
	       "dm9601_mdio_read() phy_id=0x%02x, loc=0x%02x, returns=0x%04x",
	       phy_id, loc, le16_to_cpu(res));

	return le16_to_cpu(res);
}

static void dm9601_mdio_write(struct net_device *netdev, int phy_id, int loc,
			      int val)
{
	struct usbnet *dev = netdev_priv(netdev);
	u16 res = cpu_to_le16(val);

	if (phy_id) {
		devdbg(dev, "Only internal phy supported");
		return;
	}

	devdbg(dev,"dm9601_mdio_write() phy_id=0x%02x, loc=0x%02x, val=0x%04x",
	       phy_id, loc, val);

	dm_write_shared_word(dev, 1, loc, res);
}

static void dm9601_get_drvinfo(struct net_device *net,
			       struct ethtool_drvinfo *info)
{
	/* Inherit standard device info */
	usbnet_get_drvinfo(net, info);
	info->eedump_len = DM_EEPROM_LEN;
}

static u32 dm9601_get_link(struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);

	return mii_link_ok(&dev->mii);
}

static int dm9601_ioctl(struct net_device *net, struct ifreq *rq, int cmd)
{
	struct usbnet *dev = netdev_priv(net);

	return generic_mii_ioctl(&dev->mii, if_mii(rq), cmd, NULL);
}

static struct ethtool_ops dm9601_ethtool_ops = {
	.get_drvinfo	= dm9601_get_drvinfo,
	.get_link	= dm9601_get_link,
	.get_msglevel	= usbnet_get_msglevel,
	.set_msglevel	= usbnet_set_msglevel,
	.get_eeprom_len	= dm9601_get_eeprom_len,
	.get_eeprom	= dm9601_get_eeprom,
	.get_settings	= usbnet_get_settings,
	.set_settings	= usbnet_set_settings,
	.nway_reset	= usbnet_nway_reset,
};

static void dm9601_set_multicast(struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);
	/* We use the 20 byte dev->data for our 8 byte filter buffer
	 * to avoid allocating memory that is tricky to free later */
	u8 *hashes = (u8 *) & dev->data;
	u8 rx_ctl = 0x01;

	memset(hashes, 0x00, DM_MCAST_SIZE);
	hashes[DM_MCAST_SIZE - 1] |= 0x80;	/* broadcast address */

	if (net->flags & IFF_PROMISC) {
		rx_ctl |= 0x02;
	} else if (net->flags & IFF_ALLMULTI || net->mc_count > DM_MAX_MCAST) {
		rx_ctl |= 0x04;
	} else if (net->mc_count) {
		struct dev_mc_list *mc_list = net->mc_list;
		int i;

		for (i = 0; i < net->mc_count; i++) {
			u32 crc = ether_crc(ETH_ALEN, mc_list->dmi_addr) >> 26;
			hashes[crc >> 3] |= 1 << (crc & 0x7);
		}
	}

	dm_write_async(dev, DM_MCAST_ADDR, DM_MCAST_SIZE, hashes);
	dm_write_reg_async(dev, DM_RX_CTRL, rx_ctl);
}

static int dm9601_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int ret;

	ret = usbnet_get_endpoints(dev, intf);
	if (ret)
		goto out;

	dev->net->do_ioctl = dm9601_ioctl;
	dev->net->set_multicast_list = dm9601_set_multicast;
	dev->net->ethtool_ops = &dm9601_ethtool_ops;
	dev->net->hard_header_len += DM_TX_OVERHEAD;
	dev->hard_mtu = dev->net->mtu + dev->net->hard_header_len;
	dev->rx_urb_size = dev->net->mtu + DM_RX_OVERHEAD;

	dev->mii.dev = dev->net;
	dev->mii.mdio_read = dm9601_mdio_read;
	dev->mii.mdio_write = dm9601_mdio_write;
	dev->mii.phy_id_mask = 0x1f;
	dev->mii.reg_num_mask = 0x1f;

	/* reset */
	ret = dm_write_reg(dev, DM_NET_CTRL, 1);
	udelay(20);

	/* read MAC */
	ret = dm_read(dev, DM_PHY_ADDR, ETH_ALEN, dev->net->dev_addr);
	if (ret < 0) {
		printk(KERN_ERR "Error reading MAC address\n");
		ret = -ENODEV;
		goto out;
	}


	/* power up phy */
	dm_write_reg(dev, DM_GPR_CTRL, 1);
	dm_write_reg(dev, DM_GPR_DATA, 0);

	/* receive broadcast packets */
	dm9601_set_multicast(dev->net);

	dm9601_mdio_write(dev->net, dev->mii.phy_id, MII_BMCR, BMCR_RESET);
	dm9601_mdio_write(dev->net, dev->mii.phy_id, MII_ADVERTISE,
			  ADVERTISE_ALL | ADVERTISE_CSMA | ADVERTISE_PAUSE_CAP);
	mii_nway_restart(&dev->mii);

out:
	return ret;
}

static int dm9601_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	u8 status;
	int len;

	/* format:
	   b0: rx status
	   b1: packet length (incl crc) low
	   b2: packet length (incl crc) high
	   b3..n-4: packet data
	   bn-3..bn: ethernet crc
	 */

	if (unlikely(skb->len < DM_RX_OVERHEAD)) {
		dev_err(&dev->udev->dev, "unexpected tiny rx frame\n");
		return 0;
	}

	status = skb->data[0];
	len = (skb->data[1] | (skb->data[2] << 8)) - 4;

	if (unlikely(status & 0xbf)) {
		if (status & 0x01) dev->stats.rx_fifo_errors++;
		if (status & 0x02) dev->stats.rx_crc_errors++;
		if (status & 0x04) dev->stats.rx_frame_errors++;
		if (status & 0x20) dev->stats.rx_missed_errors++;
		if (status & 0x90) dev->stats.rx_length_errors++;
		return 0;
	}

	skb_pull(skb, 3);
	skb_trim(skb, len);

	return 1;
}

static struct sk_buff *dm9601_tx_fixup(struct usbnet *dev, struct sk_buff *skb,
				       gfp_t flags)
{
	int len;

	/* format:
	   b0: packet length low
	   b1: packet length high
	   b3..n: packet data
	*/

	if (skb_headroom(skb) < DM_TX_OVERHEAD) {
		struct sk_buff *skb2;

		skb2 = skb_copy_expand(skb, DM_TX_OVERHEAD, 0, flags);
		dev_kfree_skb_any(skb);
		skb = skb2;
		if (!skb)
			return NULL;
	}

	__skb_push(skb, DM_TX_OVERHEAD);

	len = skb->len;
	/* usbnet adds padding if length is a multiple of packet size
	   if so, adjust length value in header */
	if ((len % dev->maxpacket) == 0)
		len++;

	skb->data[0] = len;
	skb->data[1] = len >> 8;

	return skb;
}

static void dm9601_status(struct usbnet *dev, struct urb *urb)
{
	int link;
	u8 *buf;

	/* format:
	   b0: net status
	   b1: tx status 1
	   b2: tx status 2
	   b3: rx status
	   b4: rx overflow
	   b5: rx count
	   b6: tx count
	   b7: gpr
	*/

	if (urb->actual_length < 8)
		return;

	buf = urb->transfer_buffer;

	link = !!(buf[0] & 0x40);
	if (netif_carrier_ok(dev->net) != link) {
		if (link) {
			netif_carrier_on(dev->net);
			usbnet_defer_kevent (dev, EVENT_LINK_RESET);
		}
		else
			netif_carrier_off(dev->net);
		devdbg(dev, "Link Status is: %d", link);
	}
}

static int dm9601_link_reset(struct usbnet *dev)
{
	struct ethtool_cmd ecmd;

	mii_check_media(&dev->mii, 1, 1);
	mii_ethtool_gset(&dev->mii, &ecmd);

	devdbg(dev, "link_reset() speed: %d duplex: %d",
	       ecmd.speed, ecmd.duplex);

	return 0;
}

static const struct driver_info dm9601_info = {
	.description	= "Davicom DM9601 USB Ethernet",
	.flags		= FLAG_ETHER,
	.bind		= dm9601_bind,
	.rx_fixup	= dm9601_rx_fixup,
	.tx_fixup	= dm9601_tx_fixup,
	.status		= dm9601_status,
	.link_reset	= dm9601_link_reset,
	.reset		= dm9601_link_reset,
};

static const struct usb_device_id products[] = {
	{
	 USB_DEVICE(0x07aa, 0x9601),	/* Corega FEther USB-TXC */
	 .driver_info = (unsigned long)&dm9601_info,
	 },
	{
	 USB_DEVICE(0x0a46, 0x9601),	/* Davicom USB-100 */
	 .driver_info = (unsigned long)&dm9601_info,
	 },
	{
	 USB_DEVICE(0x0a46, 0x6688),	/* ZT6688 USB NIC */
	 .driver_info = (unsigned long)&dm9601_info,
	 },
	{
	 USB_DEVICE(0x0a46, 0x0268),	/* ShanTou ST268 USB NIC */
	 .driver_info = (unsigned long)&dm9601_info,
	 },
	{},			// END
};

MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver dm9601_driver = {
	.name = "dm9601",
	.id_table = products,
	.probe = usbnet_probe,
	.disconnect = usbnet_disconnect,
	.suspend = usbnet_suspend,
	.resume = usbnet_resume,
};

static int __init dm9601_init(void)
{
	return usb_register(&dm9601_driver);
}

static void __exit dm9601_exit(void)
{
	usb_deregister(&dm9601_driver);
}

module_init(dm9601_init);
module_exit(dm9601_exit);

MODULE_AUTHOR("Peter Korsgaard <jacmet@sunsite.dk>");
MODULE_DESCRIPTION("Davicom DM9601 USB 1.1 ethernet devices");
MODULE_LICENSE("GPL");
