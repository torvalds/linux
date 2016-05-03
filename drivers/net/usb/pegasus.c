/*
 *  Copyright (c) 1999-2013 Petko Manolov (petkan@nucleusys.com)
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
 *			is out of the interrupt routine.
 *		...
 *		v0.9.3	simplified [get|set]_register(s), async update registers
 *			logic revisited, receive skb_pool removed.
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
#define DRIVER_VERSION "v0.9.3 (2013/04/25)"
#define DRIVER_AUTHOR "Petko Manolov <petkan@nucleusys.com>"
#define DRIVER_DESC "Pegasus/Pegasus II USB Ethernet driver"

static const char driver_name[] = "pegasus";

#undef	PEGASUS_WRITE_EEPROM
#define	BMSR_MEDIA	(BMSR_10HALF | BMSR_10FULL | BMSR_100HALF | \
			BMSR_100FULL | BMSR_ANEGCAPABLE)

static bool loopback;
static bool mii_mode;
static char *devid;

static struct usb_eth_dev usb_dev_id[] = {
#define	PEGASUS_DEV(pn, vid, pid, flags)	\
	{.name = pn, .vendor = vid, .device = pid, .private = flags},
#define PEGASUS_DEV_CLASS(pn, vid, pid, dclass, flags) \
	PEGASUS_DEV(pn, vid, pid, flags)
#include "pegasus.h"
#undef	PEGASUS_DEV
#undef	PEGASUS_DEV_CLASS
	{NULL, 0, 0, 0},
	{NULL, 0, 0, 0}
};

static struct usb_device_id pegasus_ids[] = {
#define	PEGASUS_DEV(pn, vid, pid, flags) \
	{.match_flags = USB_DEVICE_ID_MATCH_DEVICE, .idVendor = vid, .idProduct = pid},
/*
 * The Belkin F8T012xx1 bluetooth adaptor has the same vendor and product
 * IDs as the Belkin F5D5050, so we need to teach the pegasus driver to
 * ignore adaptors belonging to the "Wireless" class 0xE0. For this one
 * case anyway, seeing as the pegasus is for "Wired" adaptors.
 */
#define PEGASUS_DEV_CLASS(pn, vid, pid, dclass, flags) \
	{.match_flags = (USB_DEVICE_ID_MATCH_DEVICE | USB_DEVICE_ID_MATCH_DEV_CLASS), \
	.idVendor = vid, .idProduct = pid, .bDeviceClass = dclass},
#include "pegasus.h"
#undef	PEGASUS_DEV
#undef	PEGASUS_DEV_CLASS
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
module_param(msg_level, int, 0);
MODULE_PARM_DESC(msg_level, "Override default message level");

MODULE_DEVICE_TABLE(usb, pegasus_ids);
static const struct net_device_ops pegasus_netdev_ops;

/*****/

static void async_ctrl_callback(struct urb *urb)
{
	struct usb_ctrlrequest *req = (struct usb_ctrlrequest *)urb->context;
	int status = urb->status;

	if (status < 0)
		dev_dbg(&urb->dev->dev, "%s failed with %d", __func__, status);
	kfree(req);
	usb_free_urb(urb);
}

static int get_registers(pegasus_t *pegasus, __u16 indx, __u16 size, void *data)
{
	int ret;

	ret = usb_control_msg(pegasus->usb, usb_rcvctrlpipe(pegasus->usb, 0),
			      PEGASUS_REQ_GET_REGS, PEGASUS_REQT_READ, 0,
			      indx, data, size, 1000);
	if (ret < 0)
		netif_dbg(pegasus, drv, pegasus->net,
			  "%s returned %d\n", __func__, ret);
	return ret;
}

static int set_registers(pegasus_t *pegasus, __u16 indx, __u16 size, void *data)
{
	int ret;

	ret = usb_control_msg(pegasus->usb, usb_sndctrlpipe(pegasus->usb, 0),
			      PEGASUS_REQ_SET_REGS, PEGASUS_REQT_WRITE, 0,
			      indx, data, size, 100);
	if (ret < 0)
		netif_dbg(pegasus, drv, pegasus->net,
			  "%s returned %d\n", __func__, ret);
	return ret;
}

static int set_register(pegasus_t *pegasus, __u16 indx, __u8 data)
{
	int ret;

	ret = usb_control_msg(pegasus->usb, usb_sndctrlpipe(pegasus->usb, 0),
			      PEGASUS_REQ_SET_REG, PEGASUS_REQT_WRITE, data,
			      indx, &data, 1, 1000);
	if (ret < 0)
		netif_dbg(pegasus, drv, pegasus->net,
			  "%s returned %d\n", __func__, ret);
	return ret;
}

static int update_eth_regs_async(pegasus_t *pegasus)
{
	int ret = -ENOMEM;
	struct urb *async_urb;
	struct usb_ctrlrequest *req;

	req = kmalloc(sizeof(struct usb_ctrlrequest), GFP_ATOMIC);
	if (req == NULL)
		return ret;

	async_urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (async_urb == NULL) {
		kfree(req);
		return ret;
	}
	req->bRequestType = PEGASUS_REQT_WRITE;
	req->bRequest = PEGASUS_REQ_SET_REGS;
	req->wValue = cpu_to_le16(0);
	req->wIndex = cpu_to_le16(EthCtrl0);
	req->wLength = cpu_to_le16(3);

	usb_fill_control_urb(async_urb, pegasus->usb,
			     usb_sndctrlpipe(pegasus->usb, 0), (void *)req,
			     pegasus->eth_regs, 3, async_ctrl_callback, req);

	ret = usb_submit_urb(async_urb, GFP_ATOMIC);
	if (ret) {
		if (ret == -ENODEV)
			netif_device_detach(pegasus->net);
		netif_err(pegasus, drv, pegasus->net,
			  "%s returned %d\n", __func__, ret);
	}
	return ret;
}

static int __mii_op(pegasus_t *p, __u8 phy, __u8 indx, __u16 *regd, __u8 cmd)
{
	int i;
	__u8 data[4] = { phy, 0, 0, indx };
	__le16 regdi;
	int ret = -ETIMEDOUT;

	if (cmd & PHY_WRITE) {
		__le16 *t = (__le16 *) & data[1];
		*t = cpu_to_le16(*regd);
	}
	set_register(p, PhyCtrl, 0);
	set_registers(p, PhyAddr, sizeof(data), data);
	set_register(p, PhyCtrl, (indx | cmd));
	for (i = 0; i < REG_TIMEOUT; i++) {
		ret = get_registers(p, PhyCtrl, 1, data);
		if (ret < 0)
			goto fail;
		if (data[0] & PHY_DONE)
			break;
	}
	if (i >= REG_TIMEOUT)
		goto fail;
	if (cmd & PHY_READ) {
		ret = get_registers(p, PhyData, 2, &regdi);
		*regd = le16_to_cpu(regdi);
		return ret;
	}
	return 0;
fail:
	netif_dbg(p, drv, p->net, "%s failed\n", __func__);
	return ret;
}

/* Returns non-negative int on success, error on failure */
static int read_mii_word(pegasus_t *pegasus, __u8 phy, __u8 indx, __u16 *regd)
{
	return __mii_op(pegasus, phy, indx, regd, PHY_READ);
}

/* Returns zero on success, error on failure */
static int write_mii_word(pegasus_t *pegasus, __u8 phy, __u8 indx, __u16 *regd)
{
	return __mii_op(pegasus, phy, indx, regd, PHY_WRITE);
}

static int mdio_read(struct net_device *dev, int phy_id, int loc)
{
	pegasus_t *pegasus = netdev_priv(dev);
	u16 res;

	read_mii_word(pegasus, phy_id, loc, &res);
	return (int)res;
}

static void mdio_write(struct net_device *dev, int phy_id, int loc, int val)
{
	pegasus_t *pegasus = netdev_priv(dev);
	u16 data = val;

	write_mii_word(pegasus, phy_id, loc, &data);
}

static int read_eprom_word(pegasus_t *pegasus, __u8 index, __u16 *retdata)
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
	if (i >= REG_TIMEOUT)
		goto fail;

	ret = get_registers(pegasus, EpromData, 2, &retdatai);
	*retdata = le16_to_cpu(retdatai);
	return ret;

fail:
	netif_warn(pegasus, drv, pegasus->net, "%s failed\n", __func__);
	return -ETIMEDOUT;
}

#ifdef	PEGASUS_WRITE_EEPROM
static inline void enable_eprom_write(pegasus_t *pegasus)
{
	__u8 tmp;

	get_registers(pegasus, EthCtrl2, 1, &tmp);
	set_register(pegasus, EthCtrl2, tmp | EPROM_WR_ENABLE);
}

static inline void disable_eprom_write(pegasus_t *pegasus)
{
	__u8 tmp;

	get_registers(pegasus, EthCtrl2, 1, &tmp);
	set_register(pegasus, EpromCtrl, 0);
	set_register(pegasus, EthCtrl2, tmp & ~EPROM_WR_ENABLE);
}

static int write_eprom_word(pegasus_t *pegasus, __u8 index, __u16 data)
{
	int i;
	__u8 tmp, d[4] = { 0x3f, 0, 0, EPROM_WRITE };
	int ret;
	__le16 le_data = cpu_to_le16(data);

	set_registers(pegasus, EpromOffset, 4, d);
	enable_eprom_write(pegasus);
	set_register(pegasus, EpromOffset, index);
	set_registers(pegasus, EpromData, 2, &le_data);
	set_register(pegasus, EpromCtrl, EPROM_WRITE);

	for (i = 0; i < REG_TIMEOUT; i++) {
		ret = get_registers(pegasus, EpromCtrl, 1, &tmp);
		if (ret == -ESHUTDOWN)
			goto fail;
		if (tmp & EPROM_DONE)
			break;
	}
	disable_eprom_write(pegasus);
	if (i >= REG_TIMEOUT)
		goto fail;

	return ret;

fail:
	netif_warn(pegasus, drv, pegasus->net, "%s failed\n", __func__);
	return -ETIMEDOUT;
}
#endif				/* PEGASUS_WRITE_EEPROM */

static inline void get_node_id(pegasus_t *pegasus, __u8 *id)
{
	int i;
	__u16 w16;

	for (i = 0; i < 3; i++) {
		read_eprom_word(pegasus, i, &w16);
		((__le16 *) id)[i] = cpu_to_le16(w16);
	}
}

static void set_ethernet_addr(pegasus_t *pegasus)
{
	__u8 node_id[6];

	if (pegasus->features & PEGASUS_II) {
		get_registers(pegasus, 0x10, sizeof(node_id), node_id);
	} else {
		get_node_id(pegasus, node_id);
		set_registers(pegasus, EthID, sizeof(node_id), node_id);
	}
	memcpy(pegasus->net->dev_addr, node_id, sizeof(node_id));
}

static inline int reset_mac(pegasus_t *pegasus)
{
	__u8 data = 0x8;
	int i;

	set_register(pegasus, EthCtrl1, data);
	for (i = 0; i < REG_TIMEOUT; i++) {
		get_registers(pegasus, EthCtrl1, 1, &data);
		if (~data & 0x08) {
			if (loopback)
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
		auxmode |= 4;
		write_mii_word(pegasus, 3, 0x1b, &auxmode);
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
	data[0] = 0xc8; /* TX & RX enable, append status, no CRC */
	data[1] = 0;
	if (linkpart & (ADVERTISE_100FULL | ADVERTISE_10FULL))
		data[1] |= 0x20;	/* set full duplex */
	if (linkpart & (ADVERTISE_100FULL | ADVERTISE_100HALF))
		data[1] |= 0x10;	/* set 100 Mbps */
	if (mii_mode)
		data[1] = 0;
	data[2] = loopback ? 0x09 : 0x01;

	memcpy(pegasus->eth_regs, data, sizeof(data));
	ret = set_registers(pegasus, EthCtrl0, 3, data);

	if (usb_dev_id[pegasus->dev_index].vendor == VENDOR_LINKSYS ||
	    usb_dev_id[pegasus->dev_index].vendor == VENDOR_LINKSYS2 ||
	    usb_dev_id[pegasus->dev_index].vendor == VENDOR_DLINK) {
		u16 auxmode;
		read_mii_word(pegasus, 0, 0x1b, &auxmode);
		auxmode |= 4;
		write_mii_word(pegasus, 0, 0x1b, &auxmode);
	}

	return ret;
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
		netif_dbg(pegasus, rx_err, net, "reset MAC\n");
		pegasus->flags &= ~PEGASUS_RX_BUSY;
		break;
	case -EPIPE:		/* stall, or disconnect from TT */
		/* FIXME schedule work to clear the halt */
		netif_warn(pegasus, rx_err, net, "no rx stall recovery\n");
		return;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		netif_dbg(pegasus, ifdown, net, "rx unlink, %d\n", status);
		return;
	default:
		netif_dbg(pegasus, rx_err, net, "RX status %d\n", status);
		goto goon;
	}

	if (!count || count < 4)
		goto goon;

	rx_status = buf[count - 2];
	if (rx_status & 0x1e) {
		netif_dbg(pegasus, rx_err, net,
			  "RX packet error %x\n", rx_status);
		pegasus->stats.rx_errors++;
		if (rx_status & 0x06)	/* long or runt	*/
			pegasus->stats.rx_length_errors++;
		if (rx_status & 0x08)
			pegasus->stats.rx_crc_errors++;
		if (rx_status & 0x10)	/* extra bits	*/
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
		pkt_len -= 4;
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

	pegasus->rx_skb = __netdev_alloc_skb_ip_align(pegasus->net, PEGASUS_MTU,
						      GFP_ATOMIC);

	if (pegasus->rx_skb == NULL)
		goto tl_sched;
goon:
	usb_fill_bulk_urb(pegasus->rx_urb, pegasus->usb,
			  usb_rcvbulkpipe(pegasus->usb, 1),
			  pegasus->rx_skb->data, PEGASUS_MTU,
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
	int status;

	pegasus = (pegasus_t *) data;
	if (pegasus->flags & PEGASUS_UNPLUG)
		return;

	if (pegasus->flags & PEGASUS_RX_URB_FAIL)
		if (pegasus->rx_skb)
			goto try_again;
	if (pegasus->rx_skb == NULL)
		pegasus->rx_skb = __netdev_alloc_skb_ip_align(pegasus->net,
							      PEGASUS_MTU,
							      GFP_ATOMIC);
	if (pegasus->rx_skb == NULL) {
		netif_warn(pegasus, rx_err, pegasus->net, "low on memory\n");
		tasklet_schedule(&pegasus->rx_tl);
		return;
	}
	usb_fill_bulk_urb(pegasus->rx_urb, pegasus->usb,
			  usb_rcvbulkpipe(pegasus->usb, 1),
			  pegasus->rx_skb->data, PEGASUS_MTU,
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
		netif_warn(pegasus, tx_err, net, "no tx stall recovery\n");
		return;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		netif_dbg(pegasus, ifdown, net, "tx unlink, %d\n", status);
		return;
	default:
		netif_info(pegasus, tx_err, net, "TX status %d\n", status);
		/* FALL THROUGH */
	case 0:
		break;
	}

	netif_trans_update(net); /* prevent tx timeout */
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
		netif_dbg(pegasus, timer, net, "intr status %d\n", status);
	}

	if (urb->actual_length >= 6) {
		u8 *d = urb->transfer_buffer;

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
	if (res)
		netif_err(pegasus, timer, net,
			  "can't resubmit interrupt urb, %d\n", res);
}

static void pegasus_tx_timeout(struct net_device *net)
{
	pegasus_t *pegasus = netdev_priv(net);
	netif_warn(pegasus, timer, net, "tx timeout\n");
	usb_unlink_urb(pegasus->tx_urb);
	pegasus->stats.tx_errors++;
}

static netdev_tx_t pegasus_start_xmit(struct sk_buff *skb,
					    struct net_device *net)
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
		netif_warn(pegasus, tx_err, net, "fail tx, %d\n", res);
		switch (res) {
		case -EPIPE:		/* stall, or disconnect from TT */
			/* cleanup should already have been scheduled */
			break;
		case -ENODEV:		/* disconnect() upcoming */
		case -EPERM:
			netif_device_detach(pegasus->net);
			break;
		default:
			pegasus->stats.tx_errors++;
			netif_start_queue(net);
		}
	} else {
		pegasus->stats.tx_packets++;
		pegasus->stats.tx_bytes += skb->len;
	}
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

static struct net_device_stats *pegasus_netdev_stats(struct net_device *dev)
{
	return &((pegasus_t *) netdev_priv(dev))->stats;
}

static inline void disable_net_traffic(pegasus_t *pegasus)
{
	__le16 tmp = cpu_to_le16(0);

	set_registers(pegasus, EthCtrl0, sizeof(tmp), &tmp);
}

static inline void get_interrupt_interval(pegasus_t *pegasus)
{
	u16 data;
	u8 interval;

	read_eprom_word(pegasus, 4, &data);
	interval = data >> 8;
	if (pegasus->usb->speed != USB_SPEED_HIGH) {
		if (interval < 0x80) {
			netif_info(pegasus, timer, pegasus->net,
				   "intr interval changed from %ums to %ums\n",
				   interval, 0x80);
			interval = 0x80;
			data = (data & 0x00FF) | ((u16)interval << 8);
#ifdef PEGASUS_WRITE_EEPROM
			write_eprom_word(pegasus, 4, data);
#endif
		}
	}
	pegasus->intr_interval = interval;
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

static void free_all_urbs(pegasus_t *pegasus)
{
	usb_free_urb(pegasus->intr_urb);
	usb_free_urb(pegasus->tx_urb);
	usb_free_urb(pegasus->rx_urb);
}

static void unlink_all_urbs(pegasus_t *pegasus)
{
	usb_kill_urb(pegasus->intr_urb);
	usb_kill_urb(pegasus->tx_urb);
	usb_kill_urb(pegasus->rx_urb);
}

static int alloc_urbs(pegasus_t *pegasus)
{
	int res = -ENOMEM;

	pegasus->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pegasus->rx_urb) {
		return res;
	}
	pegasus->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pegasus->tx_urb) {
		usb_free_urb(pegasus->rx_urb);
		return res;
	}
	pegasus->intr_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pegasus->intr_urb) {
		usb_free_urb(pegasus->tx_urb);
		usb_free_urb(pegasus->rx_urb);
		return res;
	}

	return 0;
}

static int pegasus_open(struct net_device *net)
{
	pegasus_t *pegasus = netdev_priv(net);
	int res=-ENOMEM;

	if (pegasus->rx_skb == NULL)
		pegasus->rx_skb = __netdev_alloc_skb_ip_align(pegasus->net,
							      PEGASUS_MTU,
							      GFP_KERNEL);
	if (!pegasus->rx_skb)
		goto exit;

	res = set_registers(pegasus, EthID, 6, net->dev_addr);

	usb_fill_bulk_urb(pegasus->rx_urb, pegasus->usb,
			  usb_rcvbulkpipe(pegasus->usb, 1),
			  pegasus->rx_skb->data, PEGASUS_MTU,
			  read_bulk_callback, pegasus);
	if ((res = usb_submit_urb(pegasus->rx_urb, GFP_KERNEL))) {
		if (res == -ENODEV)
			netif_device_detach(pegasus->net);
		netif_dbg(pegasus, ifup, net, "failed rx_urb, %d\n", res);
		goto exit;
	}

	usb_fill_int_urb(pegasus->intr_urb, pegasus->usb,
			 usb_rcvintpipe(pegasus->usb, 3),
			 pegasus->intr_buff, sizeof(pegasus->intr_buff),
			 intr_callback, pegasus, pegasus->intr_interval);
	if ((res = usb_submit_urb(pegasus->intr_urb, GFP_KERNEL))) {
		if (res == -ENODEV)
			netif_device_detach(pegasus->net);
		netif_dbg(pegasus, ifup, net, "failed intr_urb, %d\n", res);
		usb_kill_urb(pegasus->rx_urb);
		goto exit;
	}
	res = enable_net_traffic(net, pegasus->usb);
	if (res < 0) {
		netif_dbg(pegasus, ifup, net,
			  "can't enable_net_traffic() - %d\n", res);
		res = -EIO;
		usb_kill_urb(pegasus->rx_urb);
		usb_kill_urb(pegasus->intr_urb);
		goto exit;
	}
	set_carrier(net);
	netif_start_queue(net);
	netif_dbg(pegasus, ifup, net, "open\n");
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

	strlcpy(info->driver, driver_name, sizeof(info->driver));
	strlcpy(info->version, DRIVER_VERSION, sizeof(info->version));
	usb_make_path(pegasus->usb, info->bus_info, sizeof(info->bus_info));
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
	int		ret;

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

	ret = set_register(pegasus, WakeupControl, reg78);
	if (!ret)
		ret = device_set_wakeup_enable(&pegasus->usb->dev,
						wol->wolopts);
	return ret;
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

static const struct ethtool_ops ops = {
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
	__u16 *data = (__u16 *) &rq->ifr_ifru;
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
		write_mii_word(pegasus, pegasus->phy, data[1] & 0x1f, &data[2]);
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
		netif_info(pegasus, link, net, "Promiscuous mode enabled\n");
	} else if (!netdev_mc_empty(net) || (net->flags & IFF_ALLMULTI)) {
		pegasus->eth_regs[EthCtrl0] |= RX_MULTICAST;
		pegasus->eth_regs[EthCtrl2] &= ~RX_PROMISCUOUS;
		netif_dbg(pegasus, link, net, "set allmulti\n");
	} else {
		pegasus->eth_regs[EthCtrl0] &= ~RX_MULTICAST;
		pegasus->eth_regs[EthCtrl2] &= ~RX_PROMISCUOUS;
	}
	update_eth_regs_async(pegasus);
}

static __u8 mii_phy_probe(pegasus_t *pegasus)
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

static inline void setup_pegasus_II(pegasus_t *pegasus)
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

	if (data == 0xa5)
		pegasus->chip = 0x8513;
	else
		pegasus->chip = 0;

	set_register(pegasus, 0x80, 0xc0);
	set_register(pegasus, 0x83, 0xff);
	set_register(pegasus, 0x84, 0x01);

	if (pegasus->features & HAS_HOME_PNA && mii_mode)
		set_register(pegasus, Reg81, 6);
	else
		set_register(pegasus, Reg81, 2);
}


static int pegasus_count;
static struct workqueue_struct *pegasus_workqueue;
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
	if ((udd->idVendor == cpu_to_le16(VENDOR_BELKIN)) &&
	    (udd->idProduct == cpu_to_le16(0x0121)) &&
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

	net = alloc_etherdev(sizeof(struct pegasus));
	if (!net)
		goto out;

	pegasus = netdev_priv(net);
	pegasus->dev_index = dev_index;

	res = alloc_urbs(pegasus);
	if (res < 0) {
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
	net->ethtool_ops = &ops;
	pegasus->mii.dev = net;
	pegasus->mii.mdio_read = mdio_read;
	pegasus->mii.mdio_write = mdio_write;
	pegasus->mii.phy_id_mask = 0x1f;
	pegasus->mii.reg_num_mask = 0x1f;
	pegasus->msg_enable = netif_msg_init(msg_level, NETIF_MSG_DRV
				| NETIF_MSG_PROBE | NETIF_MSG_LINK);

	pegasus->features = usb_dev_id[dev_index].private;
	get_interrupt_interval(pegasus);
	if (reset_mac(pegasus)) {
		dev_err(&intf->dev, "can't reset MAC\n");
		res = -EIO;
		goto out2;
	}
	set_ethernet_addr(pegasus);
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
	dev_info(&intf->dev, "%s, %s, %pM\n", net->name,
		 usb_dev_id[dev_index].name, net->dev_addr);
	return 0;

out3:
	usb_set_intfdata(intf, NULL);
out2:
	free_all_urbs(pegasus);
out1:
	free_netdev(net);
out:
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
	unlink_all_urbs(pegasus);
	free_all_urbs(pegasus);
	if (pegasus->rx_skb != NULL) {
		dev_kfree_skb(pegasus->rx_skb);
		pegasus->rx_skb = NULL;
	}
	free_netdev(pegasus->net);
	pegasus_dec_workqueue();
}

static int pegasus_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct pegasus *pegasus = usb_get_intfdata(intf);

	netif_device_detach(pegasus->net);
	cancel_delayed_work(&pegasus->carrier_check);
	if (netif_running(pegasus->net)) {
		usb_kill_urb(pegasus->rx_urb);
		usb_kill_urb(pegasus->intr_urb);
	}
	return 0;
}

static int pegasus_resume(struct usb_interface *intf)
{
	struct pegasus *pegasus = usb_get_intfdata(intf);

	netif_device_attach(pegasus->net);
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
	.ndo_set_rx_mode =		pegasus_set_multicast,
	.ndo_get_stats =		pegasus_netdev_stats,
	.ndo_tx_timeout =		pegasus_tx_timeout,
	.ndo_change_mtu =		eth_change_mtu,
	.ndo_set_mac_address =		eth_mac_addr,
	.ndo_validate_addr =		eth_validate_addr,
};

static struct usb_driver pegasus_driver = {
	.name = driver_name,
	.probe = pegasus_probe,
	.disconnect = pegasus_disconnect,
	.id_table = pegasus_ids,
	.suspend = pegasus_suspend,
	.resume = pegasus_resume,
	.disable_hub_initiated_lpm = 1,
};

static void __init parse_id(char *id)
{
	unsigned int vendor_id = 0, device_id = 0, flags = 0, i = 0;
	char *token, *name = NULL;

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

	for (i = 0; usb_dev_id[i].name; i++);
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
