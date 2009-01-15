/*
 *  Copyright (c) 1999-2005 Petko Manolov (petkan@users.sourceforge.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *	ChangeLog:
 *		....	Most of the time spent on reading sources & docs.
 *		v0.2.x	First official release for the Linux kernel.
 *		v0.3.0	Beutified and structured, some bugs fixed.
 *		v0.3.x	URBifying bulk requests and bugfixing. First relatively
 *			stable release. Still can touch device's registers only
 *			from top-halves.
 *		v0.4.0	Control messages remained unurbified are now URBs.
 *			Now we can touch the HW at any time.
 *		v0.4.9	Control urbs again use process context to wait. Argh...
 *			Some long standing bugs (enable_net_traffic) fixed.
 *			Also nasty trick about resubmiting control urb from
 *			interrupt context used. Please let me know how it
 *			behaves. Pegasus II support added since this version.
 *			TODO: suppressing HCD warnings spewage on disconnect.
 *		v0.4.13	Ethernet address is now set at probe(), not at open()
 *			time as this seems to break dhcpd. 
 *		v0.5.0	branch to 2.5.x kernels
 *		v0.5.1	ethtool support added
 *		v0.5.5	rx socket buffers are in a pool and the their allocation
 * 			is out of the interrupt routine.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/module.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>
#include "pegasus.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.6.14 (2006/09/27)"
#define DRIVER_AUTHOR "Petko Manolov <petkan@users.sourceforge.net>"
#define DRIVER_DESC "Pegasus/Pegasus II USB Ethernet driver"

static const char driver_name[] = "pegasus";

#undef	PEGASUS_WRITE_EEPROM
#define	BMSR_MEDIA	(BMSR_10HALF | BMSR_10FULL | BMSR_100HALF | \
			BMSR_100FULL | BMSR_ANEGCAPABLE)

static int loopback = 0;
static int mii_mode = 0;
static char *devid=NULL;

static struct usb_eth_dev usb_dev_id[] = {
#define	PEGASUS_DEV(pn, vid, pid, flags)	\
	{.name = pn, .vendor = vid, .device = pid, .private = flags},
#include "pegasus.h"
#undef	PEGASUS_DEV
	{NULL, 0, 0, 0},
	{NULL, 0, 0, 0}
};

static struct usb_device_id pegasus_ids[] = {
#define	PEGASUS_DEV(pn, vid, pid, flags) \
	{.match_flags = USB_DEVICE_ID_MATCH_DEVICE, .idVendor = vid, .idProduct = pid},
#include "pegasus.h"
#undef	PEGASUS_DEV
	{},
	{}
};

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
module_param(loopback, bool, 0);
module_param(mii_mode, bool, 0);
module_param(devid, charp, 0);
MODULE_PARM_DESC(loopback, "Enable MAC loopback mode (bit 0)");
MODULE_PARM_DESC(mii_mode, "Enable HomePNA mode (bit 0),default=MII mode = 0");
MODULE_PARM_DESC(devid, "The format is: 'DEV_name:VendorID:DeviceID:Flags'");

/* use ethtool to change the level for any given device */
static int msg_level = -1;
module_param (msg_level, int, 0);
MODULE_PARM_DESC (msg_level, "Override default message level");

MODULE_DEVICE_TABLE(usb, pegasus_ids);
static const struct net_device_ops pegasus_netdev_ops;

static int update_eth_regs_async(pegasus_t *);
/* Aargh!!! I _really_ hate such tweaks */
static void ctrl_callback(struct urb *urb)
{
	pegasus_t *pegasus = urb->context;
	int status = urb->status;

	if (!pegasus)
		return;

	switch (status) {
	case 0:
		if (pegasus->flags & ETH_REGS_CHANGE) {
			pegasus->flags &= ~ETH_REGS_CHANGE;
			pegasus->flags |= ETH_REGS_CHANGED;
			update_eth_regs_async(pegasus);
			return;
		}
		break;
	case -EINPROGRESS:
		return;
	case -ENOENT:
		break;
	default:
		if (netif_msg_drv(pegasus) && printk_ratelimit())
			dev_dbg(&pegasus->intf->dev, "%s, status %d\n",
				__func__, status);
	}
	pegasus->flags &= ~ETH_REGS_CHANGED;
	wake_up(&pegasus->ctrl_wait);
}

static int get_registers(pegasus_t * pegasus, __u16 indx, __u16 size,
			 void *data)
{
	int ret;
	char *buffer;
	DECLARE_WAITQUEUE(wait, current);

	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer) {
		if (netif_msg_drv(pegasus))
			dev_warn(&pegasus->intf->dev, "out of memory in %s\n",
					__func__);
		return -ENOMEM;
	}
	add_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);
	while (pegasus->flags & ETH_REGS_CHANGED)
		schedule();
	remove_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_RUNNING);

	pegasus->dr.bRequestType = PEGASUS_REQT_READ;
	pegasus->dr.bRequest = PEGASUS_REQ_GET_REGS;
	pegasus->dr.wValue = cpu_to_le16(0);
	pegasus->dr.wIndex = cpu_to_le16(indx);
	pegasus->dr.wLength = cpu_to_le16(size);
	pegasus->ctrl_urb->transfer_buffer_length = size;

	usb_fill_control_urb(pegasus->ctrl_urb, pegasus->usb,
			     usb_rcvctrlpipe(pegasus->usb, 0),
			     (char *) &pegasus->dr,
			     buffer, size, ctrl_callback, pegasus);

	add_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);

	/* using ATOMIC, we'd never wake up if we slept */
	if ((ret = usb_submit_urb(pegasus->ctrl_urb, GFP_ATOMIC))) {
		set_current_state(TASK_RUNNING);
		if (ret == -ENODEV)
			netif_device_detach(pegasus->net);
		if (netif_msg_drv(pegasus) && printk_ratelimit())
			dev_err(&pegasus->intf->dev, "%s, status %d\n",
					__func__, ret);
		goto out;
	}

	schedule();
out:
	remove_wait_queue(&pegasus->ctrl_wait, &wait);
	memcpy(data, buffer, size);
	kfree(buffer);

	return ret;
}

static int set_registers(pegasus_t * pegasus, __u16 indx, __u16 size,
			 void *data)
{
	int ret;
	char *buffer;
	DECLARE_WAITQUEUE(wait, current);

	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer) {
		if (netif_msg_drv(pegasus))
			dev_warn(&pegasus->intf->dev, "out of memory in %s\n",
					__func__);
		return -ENOMEM;
	}
	memcpy(buffer, data, size);

	add_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);
	while (pegasus->flags & ETH_REGS_CHANGED)
		schedule();
	remove_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_RUNNING);

	pegasus->dr.bRequestType = PEGASUS_REQT_WRITE;
	pegasus->dr.bRequest = PEGASUS_REQ_SET_REGS;
	pegasus->dr.wValue = cpu_to_le16(0);
	pegasus->dr.wIndex = cpu_to_le16(indx);
	pegasus->dr.wLength = cpu_to_le16(size);
	pegasus->ctrl_urb->transfer_buffer_length = size;

	usb_fill_control_urb(pegasus->ctrl_urb, pegasus->usb,
			     usb_sndctrlpipe(pegasus->usb, 0),
			     (char *) &pegasus->dr,
			     buffer, size, ctrl_callback, pegasus);

	add_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);

	if ((ret = usb_submit_urb(pegasus->ctrl_urb, GFP_ATOMIC))) {
		if (ret == -ENODEV)
			netif_device_detach(pegasus->net);
		if (netif_msg_drv(pegasus))
			dev_err(&pegasus->intf->dev, "%s, status %d\n",
					__func__, ret);
		goto out;
	}

	schedule();
out:
	remove_wait_queue(&pegasus->ctrl_wait, &wait);
	kfree(buffer);

	return ret;
}

static int set_register(pegasus_t * pegasus, __u16 indx, __u8 data)
{
	int ret;
	char *tmp;
	DECLARE_WAITQUEUE(wait, current);

	tmp = kmalloc(1, GFP_KERNEL);
	if (!tmp) {
		if (netif_msg_drv(pegasus))
			dev_warn(&pegasus->intf->dev, "out of memory in %s\n",
					__func__);
		return -ENOMEM;
	}
	memcpy(tmp, &data, 1);
	add_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);
	while (pegasus->flags & ETH_REGS_CHANGED)
		schedule();
	remove_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_RUNNING);

	pegasus->dr.bRequestType = PEGASUS_REQT_WRITE;
	pegasus->dr.bRequest = PEGASUS_REQ_SET_REG;
	pegasus->dr.wValue = cpu_to_le16(data);
	pegasus->dr.wIndex = cpu_to_le16(indx);
	pegasus->dr.wLength = cpu_to_le16(1);
	pegasus->ctrl_urb->transfer_buffer_length = 1;

	usb_fill_control_urb(pegasus->ctrl_urb, pegasus->usb,
			     usb_sndctrlpipe(pegasus->usb, 0),
			     (char *) &pegasus->dr,
			     tmp, 1, ctrl_callback, pegasus);

	add_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);

	if ((ret = usb_submit_urb(pegasus->ctrl_urb, GFP_ATOMIC))) {
		if (ret == -ENODEV)
			netif_device_detach(pegasus->net);
		if (netif_msg_drv(pegasus) && printk_ratelimit())
			dev_err(&pegasus->intf->dev, "%s, status %d\n",
					__func__, ret);
		goto out;
	}

	schedule();
out:
	remove_wait_queue(&pegasus->ctrl_wait, &wait);
	kfree(tmp);

	return ret;
}

static int update_eth_regs_async(pegasus_t * pegasus)
{
	int ret;

	pegasus->dr.bRequestType = PEGASUS_REQT_WRITE;
	pegasus->dr.bRequest = PEGASUS_REQ_SET_REGS;
	pegasus->dr.wValue = 0;
	pegasus->dr.wIndex = cpu_to_le16(EthCtrl0);
	pegasus->dr.wLength = cpu_to_le16(3);
	pegasus->ctrl_urb->transfer_buffer_length = 3;

	usb_fill_control_urb(pegasus->ctrl_urb, pegasus->usb,
			     usb_sndctrlpipe(pegasus->usb, 0),
			     (char *) &pegasus->dr,
			     pegasus->eth_regs, 3, ctrl_callback, pegasus);

	if ((ret = usb_submit_urb(pegasus->ctrl_urb, GFP_ATOMIC))) {
		if (ret == -ENODEV)
			netif_device_detach(pegasus->net);
		if (netif_msg_drv(pegasus))
			dev_err(&pegasus->intf->dev, "%s, status %d\n",
					__func__, ret);
	}

	return ret;
}

/* Returns 0 on success, error on failure */
static int read_mii_word(pegasus_t * pegasus, __u8 phy, __u8 indx, __u16 * regd)
{
	int i;
	__u8 data[4] = { phy, 0, 0, indx };
	__le16 regdi;
	int ret;

	set_register(pegasus, PhyCtrl, 0);
	set_registers(pegasus, PhyAddr, sizeof (data), data);
	set_register(pegasus, PhyCtrl, (indx | PHY_READ));
	for (i = 0; i < REG_TIMEOUT; i++) {
		ret = get_registers(pegasus, PhyCtrl, 1, data);
		if (ret == -ESHUTDOWN)
			goto fail;
		if (data[0] & PHY_DONE)
			break;
	}
	if (i < REG_TIMEOUT) {
		ret = get_registers(pegasus, PhyData, 2, &regdi);
		*regd = le16_to_cpu(regdi);
		return ret;
	}
fail:
	if (netif_msg_drv(pegasus))
		dev_warn(&pegasus->intf->dev, "%s failed\n", __func__);

	return ret;
}

static int mdio_read(struct net_device *dev, int phy_id, int loc)
{
	pegasus_t *pegasus = (pegasus_t *) netdev_priv(dev);
	u16 res;

	read_mii_word(pegasus, phy_id, loc, &res);
	return (int)res;
}

static int write_mii_word(pegasus_t * pegasus, __u8 phy, __u8 indx, __u16 regd)
{
	int i;
	__u8 data[4] = { phy, 0, 0, indx };
	int ret;

	data[1] = (u8) regd;
	data[2] = (u8) (regd >> 8);
	set_register(pegasus, PhyCtrl, 0);
	set_registers(pegasus, PhyAddr, sizeof(data), data);
	set_register(pegasus, PhyCtrl, (indx | PHY_WRITE));
	for (i = 0; i < REG_TIMEOUT; i++) {
		ret = get_registers(pegasus, PhyCtrl, 1, data);
		if (ret == -ESHUTDOWN)
			goto fail;
		if (data[0] & PHY_DONE)
			break;
	}
	if (i < REG_TIMEOUT)
		return ret;

fail:
	if (netif_msg_drv(pegasus))
		dev_warn(&pegasus->intf->dev, "%s failed\n", __func__);
	return -ETIMEDOUT;
}

static void mdio_write(struct net_device *dev, int phy_id, int loc, int val)
{
	pegasus_t *pegasus = (pegasus_t *) netdev_priv(dev);

	write_mii_word(pegasus, phy_id, loc, val);
}

static int read_eprom_word(pegasus_t * pegasus, __u8 index, __u16 * retdata)
{
	int i;
	__u8 tmp;
	__le16 retdatai;
	int ret;

	set_register(pegasus, EpromCtrl, 0);
	set_register(pegasus, EpromOffset, index);
	set_register(pegasus, EpromCtrl, EPROM_READ);

	for (i = 0; i < REG_TIMEOUT; i++) {
		ret = get_registers(pegasus, EpromCtrl, 1, &tmp);
		if (tmp & EPROM_DONE)
			break;
		if (ret == -ESHUTDOWN)
			goto fail;
	}
	if (i < REG_TIMEOUT) {
		ret = get_registers(pegasus, EpromData, 2, &retdatai);
		*retdata = le16_to_cpu(retdatai);
		return ret;
	}

fail:
	if (netif_msg_drv(pegasus))
		dev_warn(&pegasus->intf->dev, "%s failed\n", __func__);
	return -ETIMEDOUT;
}

#ifdef	PEGASUS_WRITE_EEPROM
static inline void enable_eprom_write(pegasus_t * pegasus)
{
	__u8 tmp;
	int ret;

	get_registers(pegasus, EthCtrl2, 1, &tmp);
	set_register(pegasus, EthCtrl2, tmp | EPROM_WR_ENABLE);
}

static inline void disable_eprom_write(pegasus_t * pegasus)
{
	__u8 tmp;
	int ret;

	get_registers(pegasus, EthCtrl2, 1, &tmp);
	set_register(pegasus, EpromCtrl, 0);
	set_register(pegasus, EthCtrl2, tmp & ~EPROM_WR_ENABLE);
}

static int write_eprom_word(pegasus_t * pegasus, __u8 index, __u16 data)
{
	int i;
	__u8 tmp, d[4] = { 0x3f, 0, 0, EPROM_WRITE };
	int ret;

	set_registers(pegasus, EpromOffset, 4, d);
	enable_eprom_write(pegasus);
	set_register(pegasus, EpromOffset, index);
	set_registers(pegasus, EpromData, 2, &data);
	set_register(pegasus, EpromCtrl, EPROM_WRITE);

	for (i = 0; i < REG_TIMEOUT; i++) {
		ret = get_registers(pegasus, EpromCtrl, 1, &tmp);
		if (ret == -ESHUTDOWN)
			goto fail;
		if (tmp & EPROM_DONE)
			break;
	}
	disable_eprom_write(pegasus);
	if (i < REG_TIMEOUT)
		return ret;
fail:
	if (netif_msg_drv(pegasus))
		dev_warn(&pegasus->intf->dev, "%s failed\n", __func__);
	return -ETIMEDOUT;
}
#endif				/* PEGASUS_WRITE_EEPROM */

static inline void get_node_id(pegasus_t * pegasus, __u8 * id)
{
	int i;
	__u16 w16;

	for (i = 0; i < 3; i++) {
		read_eprom_word(pegasus, i, &w16);
		((__le16 *) id)[i] = cpu_to_le16(w16);
	}
}

static void set_ethernet_addr(pegasus_t * pegasus)
{
	__u8 node_id[6];

	if (pegasus->features & PEGASUS_II) {
		get_registers(pegasus, 0x10, sizeof(node_id), node_id);
	} else {
		get_node_id(pegasus, node_id);
		set_registers(pegasus, EthID, sizeof (node_id), node_id);
	}
	memcpy(pegasus->net->dev_addr, node_id, sizeof (node_id));
}

static inline int reset_mac(pegasus_t * pegasus)
{
	__u8 data = 0x8;
	int i;

	set_register(pegasus, EthCtrl1, data);
	for (i = 0; i < REG_TIMEOUT; i++) {
		get_registers(pegasus, EthCtrl1, 1, &data);
		if (~data & 0x08) {
			if (loopback & 1)
				break;
			if (mii_mode && (pegasus->features & HAS_HOME_PNA))
				set_register(pegasus, Gpio1, 0x34);
			else
				set_register(pegasus, Gpio1, 0x26);
			set_register(pegasus, Gpio0, pegasus->features);
			set_register(pegasus, Gpio0, DEFAULT_GPIO_SET);
			break;
		}
	}
	if (i == REG_TIMEOUT)
		return -ETIMEDOUT;

	if (usb_dev_id[pegasus->dev_index].vendor == VENDOR_LINKSYS ||
	    usb_dev_id[pegasus->dev_index].vendor == VENDOR_DLINK) {
		set_register(pegasus, Gpio0, 0x24);
		set_register(pegasus, Gpio0, 0x26);
	}
	if (usb_dev_id[pegasus->dev_index].vendor == VENDOR_ELCON) {
		__u16 auxmode;
		read_mii_word(pegasus, 3, 0x1b, &auxmode);
		write_mii_word(pegasus, 3, 0x1b, auxmode | 4);
	}

	return 0;
}

static int enable_net_traffic(struct net_device *dev, struct usb_device *usb)
{
	__u16 linkpart;
	__u8 data[4];
	pegasus_t *pegasus = netdev_priv(dev);
	int ret;

	read_mii_word(pegasus, pegasus->phy, MII_LPA, &linkpart);
	data[0] = 0xc9;
	data[1] = 0;
	if (linkpart & (ADVERTISE_100FULL | ADVERTISE_10FULL))
		data[1] |= 0x20;	/* set full duplex */
	if (linkpart & (ADVERTISE_100FULL | ADVERTISE_100HALF))
		data[1] |= 0x10;	/* set 100 Mbps */
	if (mii_mode)
		data[1] = 0;
	data[2] = (loopback & 1) ? 0x09 : 0x01;

	memcpy(pegasus->eth_regs, data, sizeof (data));
	ret = set_registers(pegasus, EthCtrl0, 3, data);

	if (usb_dev_id[pegasus->dev_index].vendor == VENDOR_LINKSYS ||
	    usb_dev_id[pegasus->dev_index].vendor == VENDOR_LINKSYS2 ||
	    usb_dev_id[pegasus->dev_index].vendor == VENDOR_DLINK) {
		u16 auxmode;
		read_mii_word(pegasus, 0, 0x1b, &auxmode);
		write_mii_word(pegasus, 0, 0x1b, auxmode | 4);
	}

	return ret;
}

static void fill_skb_pool(pegasus_t * pegasus)
{
	int i;

	for (i = 0; i < RX_SKBS; i++) {
		if (pegasus->rx_pool[i])
			continue;
		pegasus->rx_pool[i] = dev_alloc_skb(PEGASUS_MTU + 2);
		/*
		 ** we give up if the allocation fail. the tasklet will be
		 ** rescheduled again anyway...
		 */
		if (pegasus->rx_pool[i] == NULL)
			return;
		skb_reserve(pegasus->rx_pool[i], 2);
	}
}

static void free_skb_pool(pegasus_t * pegasus)
{
	int i;

	for (i = 0; i < RX_SKBS; i++) {
		if (pegasus->rx_pool[i]) {
			dev_kfree_skb(pegasus->rx_pool[i]);
			pegasus->rx_pool[i] = NULL;
		}
	}
}

static inline struct sk_buff *pull_skb(pegasus_t * pegasus)
{
	int i;
	struct sk_buff *skb;

	for (i = 0; i < RX_SKBS; i++) {
		if (likely(pegasus->rx_pool[i] != NULL)) {
			skb = pegasus->rx_pool[i];
			pegasus->rx_pool[i] = NULL;
			return skb;
		}
	}
	return NULL;
}

static void read_bulk_callback(struct urb *urb)
{
	pegasus_t *pegasus = urb->context;
	struct net_device *net;
	int rx_status, count = urb->actual_length;
	int status = urb->status;
	u8 *buf = urb->transfer_buffer;
	__u16 pkt_len;

	if (!pegasus)
		return;

	net = pegasus->net;
	if (!netif_device_present(net) || !netif_running(net))
		return;

	switch (status) {
	case 0:
		break;
	case -ETIME:
		if (netif_msg_rx_err(pegasus))
			pr_debug("%s: reset MAC\n", net->name);
		pegasus->flags &= ~PEGASUS_RX_BUSY;
		break;
	case -EPIPE:		/* stall, or disconnect from TT */
		/* FIXME schedule work to clear the halt */
		if (netif_msg_rx_err(pegasus))
			printk(KERN_WARNING "%s: no rx stall recovery\n",
					net->name);
		return;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		if (netif_msg_ifdown(pegasus))
			pr_debug("%s: rx unlink, %d\n", net->name, status);
		return;
	default:
		if (netif_msg_rx_err(pegasus))
			pr_debug("%s: RX status %d\n", net->name, status);
		goto goon;
	}

	if (!count || count < 4)
		goto goon;

	rx_status = buf[count - 2];
	if (rx_status & 0x1e) {
		if (netif_msg_rx_err(pegasus))
			pr_debug("%s: RX packet error %x\n",
					net->name, rx_status);
		pegasus->stats.rx_errors++;
		if (rx_status & 0x06)	// long or runt
			pegasus->stats.rx_length_errors++;
		if (rx_status & 0x08)
			pegasus->stats.rx_crc_errors++;
		if (rx_status & 0x10)	// extra bits
			pegasus->stats.rx_frame_errors++;
		goto goon;
	}
	if (pegasus->chip == 0x8513) {
		pkt_len = le32_to_cpu(*(__le32 *)urb->transfer_buffer);
		pkt_len &= 0x0fff;
		pegasus->rx_skb->data += 2;
	} else {
		pkt_len = buf[count - 3] << 8;
		pkt_len += buf[count - 4];
		pkt_len &= 0xfff;
		pkt_len -= 8;
	}

	/*
	 * If the packet is unreasonably long, quietly drop it rather than
	 * kernel panicing by calling skb_put.
	 */
	if (pkt_len > PEGASUS_MTU)
		goto goon;

	/*
	 * at this point we are sure pegasus->rx_skb != NULL
	 * so we go ahead and pass up the packet.
	 */
	skb_put(pegasus->rx_skb, pkt_len);
	pegasus->rx_skb->protocol = eth_type_trans(pegasus->rx_skb, net);
	netif_rx(pegasus->rx_skb);
	pegasus->stats.rx_packets++;
	pegasus->stats.rx_bytes += pkt_len;

	if (pegasus->flags & PEGASUS_UNPLUG)
		return;

	spin_lock(&pegasus->rx_pool_lock);
	pegasus->rx_skb = pull_skb(pegasus);
	spin_unlock(&pegasus->rx_pool_lock);

	if (pegasus->rx_skb == NULL)
		goto tl_sched;
goon:
	usb_fill_bulk_urb(pegasus->rx_urb, pegasus->usb,
			  usb_rcvbulkpipe(pegasus->usb, 1),
			  pegasus->rx_skb->data, PEGASUS_MTU + 8,
			  read_bulk_callback, pegasus);
	rx_status = usb_submit_urb(pegasus->rx_urb, GFP_ATOMIC);
	if (rx_status == -ENODEV)
		netif_device_detach(pegasus->net);
	else if (rx_status) {
		pegasus->flags |= PEGASUS_RX_URB_FAIL;
		goto tl_sched;
	} else {
		pegasus->flags &= ~PEGASUS_RX_URB_FAIL;
	}

	return;

tl_sched:
	tasklet_schedule(&pegasus->rx_tl);
}

static void rx_fixup(unsigned long data)
{
	pegasus_t *pegasus;
	unsigned long flags;
	int status;

	pegasus = (pegasus_t *) data;
	if (pegasus->flags & PEGASUS_UNPLUG)
		return;

	spin_lock_irqsave(&pegasus->rx_pool_lock, flags);
	fill_skb_pool(pegasus);
	if (pegasus->flags & PEGASUS_RX_URB_FAIL)
		if (pegasus->rx_skb)
			goto try_again;
	if (pegasus->rx_skb == NULL) {
		pegasus->rx_skb = pull_skb(pegasus);
	}
	if (pegasus->rx_skb == NULL) {
		if (netif_msg_rx_err(pegasus))
			printk(KERN_WARNING "%s: low on memory\n",
					pegasus->net->name);
		tasklet_schedule(&pegasus->rx_tl);
		goto done;
	}
	usb_fill_bulk_urb(pegasus->rx_urb, pegasus->usb,
			  usb_rcvbulkpipe(pegasus->usb, 1),
			  pegasus->rx_skb->data, PEGASUS_MTU + 8,
			  read_bulk_callback, pegasus);
try_again:
	status = usb_submit_urb(pegasus->rx_urb, GFP_ATOMIC);
	if (status == -ENODEV)
		netif_device_detach(pegasus->net);
	else if (status) {
		pegasus->flags |= PEGASUS_RX_URB_FAIL;
		tasklet_schedule(&pegasus->rx_tl);
	} else {
		pegasus->flags &= ~PEGASUS_RX_URB_FAIL;
	}
done:
	spin_unlock_irqrestore(&pegasus->rx_pool_lock, flags);
}

static void write_bulk_callback(struct urb *urb)
{
	pegasus_t *pegasus = urb->context;
	struct net_device *net;
	int status = urb->status;

	if (!pegasus)
		return;

	net = pegasus->net;

	if (!netif_device_present(net) || !netif_running(net))
		return;

	switch (status) {
	case -EPIPE:
		/* FIXME schedule_work() to clear the tx halt */
		netif_stop_queue(net);
		if (netif_msg_tx_err(pegasus))
			printk(KERN_WARNING "%s: no tx stall recovery\n",
					net->name);
		return;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		if (netif_msg_ifdown(pegasus))
			pr_debug("%s: tx unlink, %d\n", net->name, status);
		return;
	default:
		if (netif_msg_tx_err(pegasus))
			pr_info("%s: TX status %d\n", net->name, status);
		/* FALL THROUGH */
	case 0:
		break;
	}

	net->trans_start = jiffies;
	netif_wake_queue(net);
}

static void intr_callback(struct urb *urb)
{
	pegasus_t *pegasus = urb->context;
	struct net_device *net;
	int res, status = urb->status;

	if (!pegasus)
		return;
	net = pegasus->net;

	switch (status) {
	case 0:
		break;
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default:
		/* some Pegasus-I products report LOTS of data
		 * toggle errors... avoid log spamming
		 */
		if (netif_msg_timer(pegasus))
			pr_debug("%s: intr status %d\n", net->name,
					status);
	}

	if (urb->actual_length >= 6) {
		u8	* d = urb->transfer_buffer;

		/* byte 0 == tx_status1, reg 2B */
		if (d[0] & (TX_UNDERRUN|EXCESSIVE_COL
					|LATE_COL|JABBER_TIMEOUT)) {
			pegasus->stats.tx_errors++;
			if (d[0] & TX_UNDERRUN)
				pegasus->stats.tx_fifo_errors++;
			if (d[0] & (EXCESSIVE_COL | JABBER_TIMEOUT))
				pegasus->stats.tx_aborted_errors++;
			if (d[0] & LATE_COL)
				pegasus->stats.tx_window_errors++;
		}

		/* d[5].LINK_STATUS lies on some adapters.
		 * d[0].NO_CARRIER kicks in only with failed TX.
		 * ... so monitoring with MII may be safest.
		 */

		/* bytes 3-4 == rx_lostpkt, reg 2E/2F */
		pegasus->stats.rx_missed_errors += ((d[3] & 0x7f) << 8) | d[4];
	}

	res = usb_submit_urb(urb, GFP_ATOMIC);
	if (res == -ENODEV)
		netif_device_detach(pegasus->net);
	if (res && netif_msg_timer(pegasus))
		printk(KERN_ERR "%s: can't resubmit interrupt urb, %d\n",
				net->name, res);
}

static void pegasus_tx_timeout(struct net_device *net)
{
	pegasus_t *pegasus = netdev_priv(net);
	if (netif_msg_timer(pegasus))
		printk(KERN_WARNING "%s: tx timeout\n", net->name);
	usb_unlink_urb(pegasus->tx_urb);
	pegasus->stats.tx_errors++;
}

static int pegasus_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	pegasus_t *pegasus = netdev_priv(net);
	int count = ((skb->len + 2) & 0x3f) ? skb->len + 2 : skb->len + 3;
	int res;
	__u16 l16 = skb->len;

	netif_stop_queue(net);

	((__le16 *) pegasus->tx_buff)[0] = cpu_to_le16(l16);
	skb_copy_from_linear_data(skb, pegasus->tx_buff + 2, skb->len);
	usb_fill_bulk_urb(pegasus->tx_urb, pegasus->usb,
			  usb_sndbulkpipe(pegasus->usb, 2),
			  pegasus->tx_buff, count,
			  write_bulk_callback, pegasus);
	if ((res = usb_submit_urb(pegasus->tx_urb, GFP_ATOMIC))) {
		if (netif_msg_tx_err(pegasus))
			printk(KERN_WARNING "%s: fail tx, %d\n",
					net->name, res);
		switch (res) {
		case -EPIPE:		/* stall, or disconnect from TT */
			/* cleanup should already have been scheduled */
			break;
		case -ENODEV:		/* disconnect() upcoming */
			netif_device_detach(pegasus->net);
			break;
		default:
			pegasus->stats.tx_errors++;
			netif_start_queue(net);
		}
	} else {
		pegasus->stats.tx_packets++;
		pegasus->stats.tx_bytes += skb->len;
		net->trans_start = jiffies;
	}
	dev_kfree_skb(skb);

	return 0;
}

static struct net_device_stats *pegasus_netdev_stats(struct net_device *dev)
{
	return &((pegasus_t *) netdev_priv(dev))->stats;
}

static inline void disable_net_traffic(pegasus_t * pegasus)
{
	int tmp = 0;

	set_registers(pegasus, EthCtrl0, 2, &tmp);
}

static inline void get_interrupt_interval(pegasus_t * pegasus)
{
	__u8 data[2];

	read_eprom_word(pegasus, 4, (__u16 *) data);
	if (pegasus->usb->speed != USB_SPEED_HIGH) {
		if (data[1] < 0x80) {
			if (netif_msg_timer(pegasus))
				dev_info(&pegasus->intf->dev, "intr interval "
					"changed from %ums to %ums\n",
					data[1], 0x80);
			data[1] = 0x80;
#ifdef PEGASUS_WRITE_EEPROM
			write_eprom_word(pegasus, 4, *(__u16 *) data);
#endif
		}
	}
	pegasus->intr_interval = data[1];
}

static void set_carrier(struct net_device *net)
{
	pegasus_t *pegasus = netdev_priv(net);
	u16 tmp;

	if (read_mii_word(pegasus, pegasus->phy, MII_BMSR, &tmp))
		return;

	if (tmp & BMSR_LSTATUS)
		netif_carrier_on(net);
	else
		netif_carrier_off(net);
}

static void free_all_urbs(pegasus_t * pegasus)
{
	usb_free_urb(pegasus->intr_urb);
	usb_free_urb(pegasus->tx_urb);
	usb_free_urb(pegasus->rx_urb);
	usb_free_urb(pegasus->ctrl_urb);
}

static void unlink_all_urbs(pegasus_t * pegasus)
{
	usb_kill_urb(pegasus->intr_urb);
	usb_kill_urb(pegasus->tx_urb);
	usb_kill_urb(pegasus->rx_urb);
	usb_kill_urb(pegasus->ctrl_urb);
}

static int alloc_urbs(pegasus_t * pegasus)
{
	pegasus->ctrl_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pegasus->ctrl_urb) {
		return 0;
	}
	pegasus->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pegasus->rx_urb) {
		usb_free_urb(pegasus->ctrl_urb);
		return 0;
	}
	pegasus->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pegasus->tx_urb) {
		usb_free_urb(pegasus->rx_urb);
		usb_free_urb(pegasus->ctrl_urb);
		return 0;
	}
	pegasus->intr_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pegasus->intr_urb) {
		usb_free_urb(pegasus->tx_urb);
		usb_free_urb(pegasus->rx_urb);
		usb_free_urb(pegasus->ctrl_urb);
		return 0;
	}

	return 1;
}

static int pegasus_open(struct net_device *net)
{
	pegasus_t *pegasus = netdev_priv(net);
	int res;

	if (pegasus->rx_skb == NULL)
		pegasus->rx_skb = pull_skb(pegasus);
	/*
	 ** Note: no point to free the pool.  it is empty :-)
	 */
	if (!pegasus->rx_skb)
		return -ENOMEM;

	res = set_registers(pegasus, EthID, 6, net->dev_addr);
	
	usb_fill_bulk_urb(pegasus->rx_urb, pegasus->usb,
			  usb_rcvbulkpipe(pegasus->usb, 1),
			  pegasus->rx_skb->data, PEGASUS_MTU + 8,
			  read_bulk_callback, pegasus);
	if ((res = usb_submit_urb(pegasus->rx_urb, GFP_KERNEL))) {
		if (res == -ENODEV)
			netif_device_detach(pegasus->net);
		if (netif_msg_ifup(pegasus))
			pr_debug("%s: failed rx_urb, %d", net->name, res);
		goto exit;
	}

	usb_fill_int_urb(pegasus->intr_urb, pegasus->usb,
			 usb_rcvintpipe(pegasus->usb, 3),
			 pegasus->intr_buff, sizeof (pegasus->intr_buff),
			 intr_callback, pegasus, pegasus->intr_interval);
	if ((res = usb_submit_urb(pegasus->intr_urb, GFP_KERNEL))) {
		if (res == -ENODEV)
			netif_device_detach(pegasus->net);
		if (netif_msg_ifup(pegasus))
			pr_debug("%s: failed intr_urb, %d\n", net->name, res);
		usb_kill_urb(pegasus->rx_urb);
		goto exit;
	}
	if ((res = enable_net_traffic(net, pegasus->usb))) {
		if (netif_msg_ifup(pegasus))
			pr_debug("%s: can't enable_net_traffic() - %d\n",
					net->name, res);
		res = -EIO;
		usb_kill_urb(pegasus->rx_urb);
		usb_kill_urb(pegasus->intr_urb);
		free_skb_pool(pegasus);
		goto exit;
	}
	set_carrier(net);
	netif_start_queue(net);
	if (netif_msg_ifup(pegasus))
		pr_debug("%s: open\n", net->name);
	res = 0;
exit:
	return res;
}

static int pegasus_close(struct net_device *net)
{
	pegasus_t *pegasus = netdev_priv(net);

	netif_stop_queue(net);
	if (!(pegasus->flags & PEGASUS_UNPLUG))
		disable_net_traffic(pegasus);
	tasklet_kill(&pegasus->rx_tl);
	unlink_all_urbs(pegasus);

	return 0;
}

static void pegasus_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	pegasus_t *pegasus = netdev_priv(dev);
	strncpy(info->driver, driver_name, sizeof (info->driver) - 1);
	strncpy(info->version, DRIVER_VERSION, sizeof (info->version) - 1);
	usb_make_path(pegasus->usb, info->bus_info, sizeof (info->bus_info));
}

/* also handles three patterns of some kind in hardware */
#define	WOL_SUPPORTED	(WAKE_MAGIC|WAKE_PHY)

static void
pegasus_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	pegasus_t	*pegasus = netdev_priv(dev);

	wol->supported = WAKE_MAGIC | WAKE_PHY;
	wol->wolopts = pegasus->wolopts;
}

static int
pegasus_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	pegasus_t	*pegasus = netdev_priv(dev);
	u8		reg78 = 0x04;
	
	if (wol->wolopts & ~WOL_SUPPORTED)
		return -EINVAL;

	if (wol->wolopts & WAKE_MAGIC)
		reg78 |= 0x80;
	if (wol->wolopts & WAKE_PHY)
		reg78 |= 0x40;
	/* FIXME this 0x10 bit still needs to get set in the chip... */
	if (wol->wolopts)
		pegasus->eth_regs[0] |= 0x10;
	else
		pegasus->eth_regs[0] &= ~0x10;
	pegasus->wolopts = wol->wolopts;
	return set_register(pegasus, WakeupControl, reg78);
}

static inline void pegasus_reset_wol(struct net_device *dev)
{
	struct ethtool_wolinfo wol;
	
	memset(&wol, 0, sizeof wol);
	(void) pegasus_set_wol(dev, &wol);
}

static int
pegasus_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	pegasus_t *pegasus;

	pegasus = netdev_priv(dev);
	mii_ethtool_gset(&pegasus->mii, ecmd);
	return 0;
}

static int
pegasus_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	pegasus_t *pegasus = netdev_priv(dev);
	return mii_ethtool_sset(&pegasus->mii, ecmd);
}

static int pegasus_nway_reset(struct net_device *dev)
{
	pegasus_t *pegasus = netdev_priv(dev);
	return mii_nway_restart(&pegasus->mii);
}

static u32 pegasus_get_link(struct net_device *dev)
{
	pegasus_t *pegasus = netdev_priv(dev);
	return mii_link_ok(&pegasus->mii);
}

static u32 pegasus_get_msglevel(struct net_device *dev)
{
	pegasus_t *pegasus = netdev_priv(dev);
	return pegasus->msg_enable;
}

static void pegasus_set_msglevel(struct net_device *dev, u32 v)
{
	pegasus_t *pegasus = netdev_priv(dev);
	pegasus->msg_enable = v;
}

static struct ethtool_ops ops = {
	.get_drvinfo = pegasus_get_drvinfo,
	.get_settings = pegasus_get_settings,
	.set_settings = pegasus_set_settings,
	.nway_reset = pegasus_nway_reset,
	.get_link = pegasus_get_link,
	.get_msglevel = pegasus_get_msglevel,
	.set_msglevel = pegasus_set_msglevel,
	.get_wol = pegasus_get_wol,
	.set_wol = pegasus_set_wol,
};

static int pegasus_ioctl(struct net_device *net, struct ifreq *rq, int cmd)
{
	__u16 *data = (__u16 *) & rq->ifr_ifru;
	pegasus_t *pegasus = netdev_priv(net);
	int res;

	switch (cmd) {
	case SIOCDEVPRIVATE:
		data[0] = pegasus->phy;
	case SIOCDEVPRIVATE + 1:
		read_mii_word(pegasus, data[0], data[1] & 0x1f, &data[3]);
		res = 0;
		break;
	case SIOCDEVPRIVATE + 2:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		write_mii_word(pegasus, pegasus->phy, data[1] & 0x1f, data[2]);
		res = 0;
		break;
	default:
		res = -EOPNOTSUPP;
	}
	return res;
}

static void pegasus_set_multicast(struct net_device *net)
{
	pegasus_t *pegasus = netdev_priv(net);

	if (net->flags & IFF_PROMISC) {
		pegasus->eth_regs[EthCtrl2] |= RX_PROMISCUOUS;
		if (netif_msg_link(pegasus))
			pr_info("%s: Promiscuous mode enabled.\n", net->name);
	} else if (net->mc_count || (net->flags & IFF_ALLMULTI)) {
		pegasus->eth_regs[EthCtrl0] |= RX_MULTICAST;
		pegasus->eth_regs[EthCtrl2] &= ~RX_PROMISCUOUS;
		if (netif_msg_link(pegasus))
			pr_debug("%s: set allmulti\n", net->name);
	} else {
		pegasus->eth_regs[EthCtrl0] &= ~RX_MULTICAST;
		pegasus->eth_regs[EthCtrl2] &= ~RX_PROMISCUOUS;
	}

	pegasus->ctrl_urb->status = 0;

	pegasus->flags |= ETH_REGS_CHANGE;
	ctrl_callback(pegasus->ctrl_urb);
}

static __u8 mii_phy_probe(pegasus_t * pegasus)
{
	int i;
	__u16 tmp;

	for (i = 0; i < 32; i++) {
		read_mii_word(pegasus, i, MII_BMSR, &tmp);
		if (tmp == 0 || tmp == 0xffff || (tmp & BMSR_MEDIA) == 0)
			continue;
		else
			return i;
	}

	return 0xff;
}

static inline void setup_pegasus_II(pegasus_t * pegasus)
{
	__u8 data = 0xa5;
	
	set_register(pegasus, Reg1d, 0);
	set_register(pegasus, Reg7b, 1);
	mdelay(100);
	if ((pegasus->features & HAS_HOME_PNA) && mii_mode)
		set_register(pegasus, Reg7b, 0);
	else
		set_register(pegasus, Reg7b, 2);

	set_register(pegasus, 0x83, data);
	get_registers(pegasus, 0x83, 1, &data);

	if (data == 0xa5) {
		pegasus->chip = 0x8513;
	} else {
		pegasus->chip = 0;
	}

	set_register(pegasus, 0x80, 0xc0);
	set_register(pegasus, 0x83, 0xff);
	set_register(pegasus, 0x84, 0x01);
	
	if (pegasus->features & HAS_HOME_PNA && mii_mode)
		set_register(pegasus, Reg81, 6);
	else
		set_register(pegasus, Reg81, 2);
}


static int pegasus_count;
static struct workqueue_struct *pegasus_workqueue = NULL;
#define CARRIER_CHECK_DELAY (2 * HZ)

static void check_carrier(struct work_struct *work)
{
	pegasus_t *pegasus = container_of(work, pegasus_t, carrier_check.work);
	set_carrier(pegasus->net);
	if (!(pegasus->flags & PEGASUS_UNPLUG)) {
		queue_delayed_work(pegasus_workqueue, &pegasus->carrier_check,
			CARRIER_CHECK_DELAY);
	}
}

static int pegasus_blacklisted(struct usb_device *udev)
{
	struct usb_device_descriptor *udd = &udev->descriptor;

	/* Special quirk to keep the driver from handling the Belkin Bluetooth
	 * dongle which happens to have the same ID.
	 */
	if ((udd->idVendor == VENDOR_BELKIN && udd->idProduct == 0x0121) &&
	    (udd->bDeviceClass == USB_CLASS_WIRELESS_CONTROLLER) &&
	    (udd->bDeviceProtocol == 1))
		return 1;

	return 0;
}

/* we rely on probe() and remove() being serialized so we
 * don't need extra locking on pegasus_count.
 */
static void pegasus_dec_workqueue(void)
{
	pegasus_count--;
	if (pegasus_count == 0) {
		destroy_workqueue(pegasus_workqueue);
		pegasus_workqueue = NULL;
	}
}

static int pegasus_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct net_device *net;
	pegasus_t *pegasus;
	int dev_index = id - pegasus_ids;
	int res = -ENOMEM;

	if (pegasus_blacklisted(dev))
		return -ENODEV;

	if (pegasus_count == 0) {
		pegasus_workqueue = create_singlethread_workqueue("pegasus");
		if (!pegasus_workqueue)
			return -ENOMEM;
	}
	pegasus_count++;

	usb_get_dev(dev);

	net = alloc_etherdev(sizeof(struct pegasus));
	if (!net) {
		dev_err(&intf->dev, "can't allocate %s\n", "device");
		goto out;
	}

	pegasus = netdev_priv(net);
	pegasus->dev_index = dev_index;
	init_waitqueue_head(&pegasus->ctrl_wait);

	if (!alloc_urbs(pegasus)) {
		dev_err(&intf->dev, "can't allocate %s\n", "urbs");
		goto out1;
	}

	tasklet_init(&pegasus->rx_tl, rx_fixup, (unsigned long) pegasus);

	INIT_DELAYED_WORK(&pegasus->carrier_check, check_carrier);

	pegasus->intf = intf;
	pegasus->usb = dev;
	pegasus->net = net;


	net->watchdog_timeo = PEGASUS_TX_TIMEOUT;
	net->netdev_ops = &pegasus_netdev_ops;
	SET_ETHTOOL_OPS(net, &ops);
	pegasus->mii.dev = net;
	pegasus->mii.mdio_read = mdio_read;
	pegasus->mii.mdio_write = mdio_write;
	pegasus->mii.phy_id_mask = 0x1f;
	pegasus->mii.reg_num_mask = 0x1f;
	spin_lock_init(&pegasus->rx_pool_lock);
	pegasus->msg_enable = netif_msg_init (msg_level, NETIF_MSG_DRV
				| NETIF_MSG_PROBE | NETIF_MSG_LINK);

	pegasus->features = usb_dev_id[dev_index].private;
	get_interrupt_interval(pegasus);
	if (reset_mac(pegasus)) {
		dev_err(&intf->dev, "can't reset MAC\n");
		res = -EIO;
		goto out2;
	}
	set_ethernet_addr(pegasus);
	fill_skb_pool(pegasus);
	if (pegasus->features & PEGASUS_II) {
		dev_info(&intf->dev, "setup Pegasus II specific registers\n");
		setup_pegasus_II(pegasus);
	}
	pegasus->phy = mii_phy_probe(pegasus);
	if (pegasus->phy == 0xff) {
		dev_warn(&intf->dev, "can't locate MII phy, using default\n");
		pegasus->phy = 1;
	}
	pegasus->mii.phy_id = pegasus->phy;
	usb_set_intfdata(intf, pegasus);
	SET_NETDEV_DEV(net, &intf->dev);
	pegasus_reset_wol(net);
	res = register_netdev(net);
	if (res)
		goto out3;
	queue_delayed_work(pegasus_workqueue, &pegasus->carrier_check,
				CARRIER_CHECK_DELAY);

	dev_info(&intf->dev, "%s, %s, %pM\n",
		 net->name,
		 usb_dev_id[dev_index].name,
		 net->dev_addr);
	return 0;

out3:
	usb_set_intfdata(intf, NULL);
	free_skb_pool(pegasus);
out2:
	free_all_urbs(pegasus);
out1:
	free_netdev(net);
out:
	usb_put_dev(dev);
	pegasus_dec_workqueue();
	return res;
}

static void pegasus_disconnect(struct usb_interface *intf)
{
	struct pegasus *pegasus = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	if (!pegasus) {
		dev_dbg(&intf->dev, "unregistering non-bound device?\n");
		return;
	}

	pegasus->flags |= PEGASUS_UNPLUG;
	cancel_delayed_work(&pegasus->carrier_check);
	unregister_netdev(pegasus->net);
	usb_put_dev(interface_to_usbdev(intf));
	unlink_all_urbs(pegasus);
	free_all_urbs(pegasus);
	free_skb_pool(pegasus);
	if (pegasus->rx_skb != NULL) {
		dev_kfree_skb(pegasus->rx_skb);
		pegasus->rx_skb = NULL;
	}
	free_netdev(pegasus->net);
	pegasus_dec_workqueue();
}

static int pegasus_suspend (struct usb_interface *intf, pm_message_t message)
{
	struct pegasus *pegasus = usb_get_intfdata(intf);
	
	netif_device_detach (pegasus->net);
	cancel_delayed_work(&pegasus->carrier_check);
	if (netif_running(pegasus->net)) {
		usb_kill_urb(pegasus->rx_urb);
		usb_kill_urb(pegasus->intr_urb);
	}
	return 0;
}

static int pegasus_resume (struct usb_interface *intf)
{
	struct pegasus *pegasus = usb_get_intfdata(intf);

	netif_device_attach (pegasus->net);
	if (netif_running(pegasus->net)) {
		pegasus->rx_urb->status = 0;
		pegasus->rx_urb->actual_length = 0;
		read_bulk_callback(pegasus->rx_urb);

		pegasus->intr_urb->status = 0;
		pegasus->intr_urb->actual_length = 0;
		intr_callback(pegasus->intr_urb);
	}
	queue_delayed_work(pegasus_workqueue, &pegasus->carrier_check,
				CARRIER_CHECK_DELAY);
	return 0;
}

static const struct net_device_ops pegasus_netdev_ops = {
	.ndo_open =			pegasus_open,
	.ndo_stop =			pegasus_close,
	.ndo_do_ioctl =			pegasus_ioctl,
	.ndo_start_xmit =		pegasus_start_xmit,
	.ndo_set_multicast_list =	pegasus_set_multicast,
	.ndo_get_stats =		pegasus_netdev_stats,
	.ndo_tx_timeout =		pegasus_tx_timeout,
};

static struct usb_driver pegasus_driver = {
	.name = driver_name,
	.probe = pegasus_probe,
	.disconnect = pegasus_disconnect,
	.id_table = pegasus_ids,
	.suspend = pegasus_suspend,
	.resume = pegasus_resume,
};

static void __init parse_id(char *id)
{
	unsigned int vendor_id=0, device_id=0, flags=0, i=0;
	char *token, *name=NULL;

	if ((token = strsep(&id, ":")) != NULL)
		name = token;
	/* name now points to a null terminated string*/
	if ((token = strsep(&id, ":")) != NULL)
		vendor_id = simple_strtoul(token, NULL, 16);
	if ((token = strsep(&id, ":")) != NULL)
		device_id = simple_strtoul(token, NULL, 16);
	flags = simple_strtoul(id, NULL, 16);
	pr_info("%s: new device %s, vendor ID 0x%04x, device ID 0x%04x, flags: 0x%x\n",
	        driver_name, name, vendor_id, device_id, flags);

	if (vendor_id > 0x10000 || vendor_id == 0)
		return;
	if (device_id > 0x10000 || device_id == 0)
		return;

	for (i=0; usb_dev_id[i].name; i++);
	usb_dev_id[i].name = name;
	usb_dev_id[i].vendor = vendor_id;
	usb_dev_id[i].device = device_id;
	usb_dev_id[i].private = flags;
	pegasus_ids[i].match_flags = USB_DEVICE_ID_MATCH_DEVICE;
	pegasus_ids[i].idVendor = vendor_id;
	pegasus_ids[i].idProduct = device_id;
}

static int __init pegasus_init(void)
{
	pr_info("%s: %s, " DRIVER_DESC "\n", driver_name, DRIVER_VERSION);
	if (devid)
		parse_id(devid);
	return usb_register(&pegasus_driver);
}

static void __exit pegasus_exit(void)
{
	usb_deregister(&pegasus_driver);
}

module_init(pegasus_init);
module_exit(pegasus_exit);
