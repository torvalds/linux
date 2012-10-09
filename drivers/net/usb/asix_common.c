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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "asix.h"

int asix_read_cmd(struct usbnet *dev, u8 cmd, u16 value, u16 index,
		  u16 size, void *data)
{
	void *buf;
	int err = -ENOMEM;

	netdev_dbg(dev->net, "asix_read_cmd() cmd=0x%02x value=0x%04x index=0x%04x size=%d\n",
		   cmd, value, index, size);

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		goto out;

	err = usb_control_msg(
		dev->udev,
		usb_rcvctrlpipe(dev->udev, 0),
		cmd,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		value,
		index,
		buf,
		size,
		USB_CTRL_GET_TIMEOUT);
	if (err == size)
		memcpy(data, buf, size);
	else if (err >= 0)
		err = -EINVAL;
	kfree(buf);

out:
	return err;
}

int asix_write_cmd(struct usbnet *dev, u8 cmd, u16 value, u16 index,
		   u16 size, void *data)
{
	void *buf = NULL;
	int err = -ENOMEM;

	netdev_dbg(dev->net, "asix_write_cmd() cmd=0x%02x value=0x%04x index=0x%04x size=%d\n",
		   cmd, value, index, size);

	if (data) {
		buf = kmemdup(data, size, GFP_KERNEL);
		if (!buf)
			goto out;
	}

	err = usb_control_msg(
		dev->udev,
		usb_sndctrlpipe(dev->udev, 0),
		cmd,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		value,
		index,
		buf,
		size,
		USB_CTRL_SET_TIMEOUT);
	kfree(buf);

out:
	return err;
}

static void asix_async_cmd_callback(struct urb *urb)
{
	struct usb_ctrlrequest *req = (struct usb_ctrlrequest *)urb->context;
	int status = urb->status;

	if (status < 0)
		printk(KERN_DEBUG "asix_async_cmd_callback() failed with %d",
			status);

	kfree(req);
	usb_free_urb(urb);
}

void asix_write_cmd_async(struct usbnet *dev, u8 cmd, u16 value, u16 index,
			  u16 size, void *data)
{
	struct usb_ctrlrequest *req;
	int status;
	struct urb *urb;

	netdev_dbg(dev->net, "asix_write_cmd_async() cmd=0x%02x value=0x%04x index=0x%04x size=%d\n",
		   cmd, value, index, size);

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		netdev_err(dev->net, "Error allocating URB in write_cmd_async!\n");
		return;
	}

	req = kmalloc(sizeof(struct usb_ctrlrequest), GFP_ATOMIC);
	if (!req) {
		netdev_err(dev->net, "Failed to allocate memory for control request\n");
		usb_free_urb(urb);
		return;
	}

	req->bRequestType = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE;
	req->bRequest = cmd;
	req->wValue = cpu_to_le16(value);
	req->wIndex = cpu_to_le16(index);
	req->wLength = cpu_to_le16(size);

	usb_fill_control_urb(urb, dev->udev,
			     usb_sndctrlpipe(dev->udev, 0),
			     (void *)req, data, size,
			     asix_async_cmd_callback, req);

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status < 0) {
		netdev_err(dev->net, "Error submitting the control message: status=%d\n",
			   status);
		kfree(req);
		usb_free_urb(urb);
	}
}

int asix_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	int offset = 0;

	while (offset + sizeof(u32) < skb->len) {
		struct sk_buff *ax_skb;
		u16 size;
		u32 header = get_unaligned_le32(skb->data + offset);

		offset += sizeof(u32);

		/* get the packet length */
		size = (u16) (header & 0x7ff);
		if (size != ((~header >> 16) & 0x07ff)) {
			netdev_err(dev->net, "asix_rx_fixup() Bad Header Length\n");
			return 0;
		}

		if ((size > dev->net->mtu + ETH_HLEN + VLAN_HLEN) ||
		    (size + offset > skb->len)) {
			netdev_err(dev->net, "asix_rx_fixup() Bad RX Length %d\n",
				   size);
			return 0;
		}
		ax_skb = netdev_alloc_skb_ip_align(dev->net, size);
		if (!ax_skb)
			return 0;

		skb_put(ax_skb, size);
		memcpy(ax_skb->data, skb->data + offset, size);
		usbnet_skb_return(dev, ax_skb);

		offset += (size + 1) & 0xfffe;
	}

	if (skb->len != offset) {
		netdev_err(dev->net, "asix_rx_fixup() Bad SKB Length %d\n",
			   skb->len);
		return 0;
	}
	return 1;
}

struct sk_buff *asix_tx_fixup(struct usbnet *dev, struct sk_buff *skb,
			      gfp_t flags)
{
	int padlen;
	int headroom = skb_headroom(skb);
	int tailroom = skb_tailroom(skb);
	u32 packet_len;
	u32 padbytes = 0xffff0000;

	padlen = ((skb->len + 4) & (dev->maxpacket - 1)) ? 0 : 4;

	/* We need to push 4 bytes in front of frame (packet_len)
	 * and maybe add 4 bytes after the end (if padlen is 4)
	 *
	 * Avoid skb_copy_expand() expensive call, using following rules :
	 * - We are allowed to push 4 bytes in headroom if skb_header_cloned()
	 *   is false (and if we have 4 bytes of headroom)
	 * - We are allowed to put 4 bytes at tail if skb_cloned()
	 *   is false (and if we have 4 bytes of tailroom)
	 *
	 * TCP packets for example are cloned, but skb_header_release()
	 * was called in tcp stack, allowing us to use headroom for our needs.
	 */
	if (!skb_header_cloned(skb) &&
	    !(padlen && skb_cloned(skb)) &&
	    headroom + tailroom >= 4 + padlen) {
		/* following should not happen, but better be safe */
		if (headroom < 4 ||
		    tailroom < padlen) {
			skb->data = memmove(skb->head + 4, skb->data, skb->len);
			skb_set_tail_pointer(skb, skb->len);
		}
	} else {
		struct sk_buff *skb2;

		skb2 = skb_copy_expand(skb, 4, padlen, flags);
		dev_kfree_skb_any(skb);
		skb = skb2;
		if (!skb)
			return NULL;
	}

	packet_len = ((skb->len ^ 0x0000ffff) << 16) + skb->len;
	skb_push(skb, 4);
	cpu_to_le32s(&packet_len);
	skb_copy_to_linear_data(skb, &packet_len, sizeof(packet_len));

	if (padlen) {
		cpu_to_le32s(&padbytes);
		memcpy(skb_tail_pointer(skb), &padbytes, sizeof(padbytes));
		skb_put(skb, sizeof(padbytes));
	}
	return skb;
}

int asix_set_sw_mii(struct usbnet *dev)
{
	int ret;
	ret = asix_write_cmd(dev, AX_CMD_SET_SW_MII, 0x0000, 0, 0, NULL);
	if (ret < 0)
		netdev_err(dev->net, "Failed to enable software MII access\n");
	return ret;
}

int asix_set_hw_mii(struct usbnet *dev)
{
	int ret;
	ret = asix_write_cmd(dev, AX_CMD_SET_HW_MII, 0x0000, 0, 0, NULL);
	if (ret < 0)
		netdev_err(dev->net, "Failed to enable hardware MII access\n");
	return ret;
}

int asix_read_phy_addr(struct usbnet *dev, int internal)
{
	int offset = (internal ? 1 : 0);
	u8 buf[2];
	int ret = asix_read_cmd(dev, AX_CMD_READ_PHY_ID, 0, 0, 2, buf);

	netdev_dbg(dev->net, "asix_get_phy_addr()\n");

	if (ret < 0) {
		netdev_err(dev->net, "Error reading PHYID register: %02x\n", ret);
		goto out;
	}
	netdev_dbg(dev->net, "asix_get_phy_addr() returning 0x%04x\n",
		   *((__le16 *)buf));
	ret = buf[offset];

out:
	return ret;
}

int asix_get_phy_addr(struct usbnet *dev)
{
	/* return the address of the internal phy */
	return asix_read_phy_addr(dev, 1);
}


int asix_sw_reset(struct usbnet *dev, u8 flags)
{
	int ret;

        ret = asix_write_cmd(dev, AX_CMD_SW_RESET, flags, 0, 0, NULL);
	if (ret < 0)
		netdev_err(dev->net, "Failed to send software reset: %02x\n", ret);

	return ret;
}

u16 asix_read_rx_ctl(struct usbnet *dev)
{
	__le16 v;
	int ret = asix_read_cmd(dev, AX_CMD_READ_RX_CTL, 0, 0, 2, &v);

	if (ret < 0) {
		netdev_err(dev->net, "Error reading RX_CTL register: %02x\n", ret);
		goto out;
	}
	ret = le16_to_cpu(v);
out:
	return ret;
}

int asix_write_rx_ctl(struct usbnet *dev, u16 mode)
{
	int ret;

	netdev_dbg(dev->net, "asix_write_rx_ctl() - mode = 0x%04x\n", mode);
	ret = asix_write_cmd(dev, AX_CMD_WRITE_RX_CTL, mode, 0, 0, NULL);
	if (ret < 0)
		netdev_err(dev->net, "Failed to write RX_CTL mode to 0x%04x: %02x\n",
			   mode, ret);

	return ret;
}

u16 asix_read_medium_status(struct usbnet *dev)
{
	__le16 v;
	int ret = asix_read_cmd(dev, AX_CMD_READ_MEDIUM_STATUS, 0, 0, 2, &v);

	if (ret < 0) {
		netdev_err(dev->net, "Error reading Medium Status register: %02x\n",
			   ret);
		return ret;	/* TODO: callers not checking for error ret */
	}

	return le16_to_cpu(v);

}

int asix_write_medium_mode(struct usbnet *dev, u16 mode)
{
	int ret;

	netdev_dbg(dev->net, "asix_write_medium_mode() - mode = 0x%04x\n", mode);
	ret = asix_write_cmd(dev, AX_CMD_WRITE_MEDIUM_MODE, mode, 0, 0, NULL);
	if (ret < 0)
		netdev_err(dev->net, "Failed to write Medium Mode mode to 0x%04x: %02x\n",
			   mode, ret);

	return ret;
}

int asix_write_gpio(struct usbnet *dev, u16 value, int sleep)
{
	int ret;

	netdev_dbg(dev->net, "asix_write_gpio() - value = 0x%04x\n", value);
	ret = asix_write_cmd(dev, AX_CMD_WRITE_GPIOS, value, 0, 0, NULL);
	if (ret < 0)
		netdev_err(dev->net, "Failed to write GPIO value 0x%04x: %02x\n",
			   value, ret);

	if (sleep)
		msleep(sleep);

	return ret;
}

/*
 * AX88772 & AX88178 have a 16-bit RX_CTL value
 */
void asix_set_multicast(struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);
	struct asix_data *data = (struct asix_data *)&dev->data;
	u16 rx_ctl = AX_DEFAULT_RX_CTL;

	if (net->flags & IFF_PROMISC) {
		rx_ctl |= AX_RX_CTL_PRO;
	} else if (net->flags & IFF_ALLMULTI ||
		   netdev_mc_count(net) > AX_MAX_MCAST) {
		rx_ctl |= AX_RX_CTL_AMALL;
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

		rx_ctl |= AX_RX_CTL_AM;
	}

	asix_write_cmd_async(dev, AX_CMD_WRITE_RX_CTL, rx_ctl, 0, 0, NULL);
}

int asix_mdio_read(struct net_device *netdev, int phy_id, int loc)
{
	struct usbnet *dev = netdev_priv(netdev);
	__le16 res;

	mutex_lock(&dev->phy_mutex);
	asix_set_sw_mii(dev);
	asix_read_cmd(dev, AX_CMD_READ_MII_REG, phy_id,
				(__u16)loc, 2, &res);
	asix_set_hw_mii(dev);
	mutex_unlock(&dev->phy_mutex);

	netdev_dbg(dev->net, "asix_mdio_read() phy_id=0x%02x, loc=0x%02x, returns=0x%04x\n",
		   phy_id, loc, le16_to_cpu(res));

	return le16_to_cpu(res);
}

void asix_mdio_write(struct net_device *netdev, int phy_id, int loc, int val)
{
	struct usbnet *dev = netdev_priv(netdev);
	__le16 res = cpu_to_le16(val);

	netdev_dbg(dev->net, "asix_mdio_write() phy_id=0x%02x, loc=0x%02x, val=0x%04x\n",
		   phy_id, loc, val);
	mutex_lock(&dev->phy_mutex);
	asix_set_sw_mii(dev);
	asix_write_cmd(dev, AX_CMD_WRITE_MII_REG, phy_id, (__u16)loc, 2, &res);
	asix_set_hw_mii(dev);
	mutex_unlock(&dev->phy_mutex);
}

void asix_get_wol(struct net_device *net, struct ethtool_wolinfo *wolinfo)
{
	struct usbnet *dev = netdev_priv(net);
	u8 opt;

	if (asix_read_cmd(dev, AX_CMD_READ_MONITOR_MODE, 0, 0, 1, &opt) < 0) {
		wolinfo->supported = 0;
		wolinfo->wolopts = 0;
		return;
	}
	wolinfo->supported = WAKE_PHY | WAKE_MAGIC;
	wolinfo->wolopts = 0;
	if (opt & AX_MONITOR_LINK)
		wolinfo->wolopts |= WAKE_PHY;
	if (opt & AX_MONITOR_MAGIC)
		wolinfo->wolopts |= WAKE_MAGIC;
}

int asix_set_wol(struct net_device *net, struct ethtool_wolinfo *wolinfo)
{
	struct usbnet *dev = netdev_priv(net);
	u8 opt = 0;

	if (wolinfo->wolopts & WAKE_PHY)
		opt |= AX_MONITOR_LINK;
	if (wolinfo->wolopts & WAKE_MAGIC)
		opt |= AX_MONITOR_MAGIC;

	if (asix_write_cmd(dev, AX_CMD_WRITE_MONITOR_MODE,
			      opt, 0, 0, NULL) < 0)
		return -EINVAL;

	return 0;
}

int asix_get_eeprom_len(struct net_device *net)
{
	return AX_EEPROM_LEN;
}

int asix_get_eeprom(struct net_device *net, struct ethtool_eeprom *eeprom,
		    u8 *data)
{
	struct usbnet *dev = netdev_priv(net);
	u16 *eeprom_buff;
	int first_word, last_word;
	int i;

	if (eeprom->len == 0)
		return -EINVAL;

	eeprom->magic = AX_EEPROM_MAGIC;

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;

	eeprom_buff = kmalloc(sizeof(u16) * (last_word - first_word + 1),
			      GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	/* ax8817x returns 2 bytes from eeprom on read */
	for (i = first_word; i <= last_word; i++) {
		if (asix_read_cmd(dev, AX_CMD_READ_EEPROM, i, 0, 2,
				  &(eeprom_buff[i - first_word])) < 0) {
			kfree(eeprom_buff);
			return -EIO;
		}
	}

	memcpy(data, (u8 *)eeprom_buff + (eeprom->offset & 1), eeprom->len);
	kfree(eeprom_buff);
	return 0;
}

int asix_set_eeprom(struct net_device *net, struct ethtool_eeprom *eeprom,
		    u8 *data)
{
	struct usbnet *dev = netdev_priv(net);
	u16 *eeprom_buff;
	int first_word, last_word;
	int i;
	int ret;

	netdev_dbg(net, "write EEPROM len %d, offset %d, magic 0x%x\n",
		   eeprom->len, eeprom->offset, eeprom->magic);

	if (eeprom->len == 0)
		return -EINVAL;

	if (eeprom->magic != AX_EEPROM_MAGIC)
		return -EINVAL;

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;

	eeprom_buff = kmalloc(sizeof(u16) * (last_word - first_word + 1),
			      GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	/* align data to 16 bit boundaries, read the missing data from
	   the EEPROM */
	if (eeprom->offset & 1) {
		ret = asix_read_cmd(dev, AX_CMD_READ_EEPROM, first_word, 0, 2,
				    &(eeprom_buff[0]));
		if (ret < 0) {
			netdev_err(net, "Failed to read EEPROM at offset 0x%02x.\n", first_word);
			goto free;
		}
	}

	if ((eeprom->offset + eeprom->len) & 1) {
		ret = asix_read_cmd(dev, AX_CMD_READ_EEPROM, last_word, 0, 2,
				    &(eeprom_buff[last_word - first_word]));
		if (ret < 0) {
			netdev_err(net, "Failed to read EEPROM at offset 0x%02x.\n", last_word);
			goto free;
		}
	}

	memcpy((u8 *)eeprom_buff + (eeprom->offset & 1), data, eeprom->len);

	/* write data to EEPROM */
	ret = asix_write_cmd(dev, AX_CMD_WRITE_ENABLE, 0x0000, 0, 0, NULL);
	if (ret < 0) {
		netdev_err(net, "Failed to enable EEPROM write\n");
		goto free;
	}
	msleep(20);

	for (i = first_word; i <= last_word; i++) {
		netdev_dbg(net, "write to EEPROM at offset 0x%02x, data 0x%04x\n",
			   i, eeprom_buff[i - first_word]);
		ret = asix_write_cmd(dev, AX_CMD_WRITE_EEPROM, i,
				     eeprom_buff[i - first_word], 0, NULL);
		if (ret < 0) {
			netdev_err(net, "Failed to write EEPROM at offset 0x%02x.\n",
				   i);
			goto free;
		}
		msleep(20);
	}

	ret = asix_write_cmd(dev, AX_CMD_WRITE_DISABLE, 0x0000, 0, 0, NULL);
	if (ret < 0) {
		netdev_err(net, "Failed to disable EEPROM write\n");
		goto free;
	}

	ret = 0;
free:
	kfree(eeprom_buff);
	return ret;
}

void asix_get_drvinfo(struct net_device *net, struct ethtool_drvinfo *info)
{
	/* Inherit standard device info */
	usbnet_get_drvinfo(net, info);
	strncpy (info->driver, DRIVER_NAME, sizeof info->driver);
	strncpy (info->version, DRIVER_VERSION, sizeof info->version);
	info->eedump_len = AX_EEPROM_LEN;
}

int asix_set_mac_address(struct net_device *net, void *p)
{
	struct usbnet *dev = netdev_priv(net);
	struct asix_data *data = (struct asix_data *)&dev->data;
	struct sockaddr *addr = p;

	if (netif_running(net))
		return -EBUSY;
	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(net->dev_addr, addr->sa_data, ETH_ALEN);

	/* We use the 20 byte dev->data
	 * for our 6 byte mac buffer
	 * to avoid allocating memory that
	 * is tricky to free later */
	memcpy(data->mac_addr, addr->sa_data, ETH_ALEN);
	asix_write_cmd_async(dev, AX_CMD_WRITE_NODE_ID, 0, 0, ETH_ALEN,
							data->mac_addr);

	return 0;
}
