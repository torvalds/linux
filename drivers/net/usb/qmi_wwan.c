// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012  Bjørn Mork <bjorn@mork.no>
 *
 * The probing code is heavily inspired by cdc_ether, which is:
 * Copyright (C) 2003-2005 by David Brownell
 * Copyright (C) 2006 by Ole Andre Vadla Ravnas (ActiveSync)
 */

#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/kstrtox.h>
#include <linux/mii.h>
#include <linux/rtnetlink.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/usb/usbnet.h>
#include <linux/usb/cdc-wdm.h>
#include <linux/u64_stats_sync.h>

/* This driver supports wwan (3G/LTE/?) devices using a vendor
 * specific management protocol called Qualcomm MSM Interface (QMI) -
 * in addition to the more common AT commands over serial interface
 * management
 *
 * QMI is wrapped in CDC, using CDC encapsulated commands on the
 * control ("master") interface of a two-interface CDC Union
 * resembling standard CDC ECM.  The devices do not use the control
 * interface for any other CDC messages.  Most likely because the
 * management protocol is used in place of the standard CDC
 * notifications NOTIFY_NETWORK_CONNECTION and NOTIFY_SPEED_CHANGE
 *
 * Alternatively, control and data functions can be combined in a
 * single USB interface.
 *
 * Handling a protocol like QMI is out of the scope for any driver.
 * It is exported as a character device using the cdc-wdm driver as
 * a subdriver, enabling userspace applications ("modem managers") to
 * handle it.
 *
 * These devices may alternatively/additionally be configured using AT
 * commands on a serial interface
 */

/* driver specific data */
struct qmi_wwan_state {
	struct usb_driver *subdriver;
	atomic_t pmcount;
	unsigned long flags;
	struct usb_interface *control;
	struct usb_interface *data;
};

enum qmi_wwan_flags {
	QMI_WWAN_FLAG_RAWIP = 1 << 0,
	QMI_WWAN_FLAG_MUX = 1 << 1,
	QMI_WWAN_FLAG_PASS_THROUGH = 1 << 2,
};

enum qmi_wwan_quirks {
	QMI_WWAN_QUIRK_DTR = 1 << 0,	/* needs "set DTR" request */
};

struct qmimux_hdr {
	u8 pad;
	u8 mux_id;
	__be16 pkt_len;
};

struct qmimux_priv {
	struct net_device *real_dev;
	u8 mux_id;
};

static int qmimux_open(struct net_device *dev)
{
	struct qmimux_priv *priv = netdev_priv(dev);
	struct net_device *real_dev = priv->real_dev;

	if (!(priv->real_dev->flags & IFF_UP))
		return -ENETDOWN;

	if (netif_carrier_ok(real_dev))
		netif_carrier_on(dev);
	return 0;
}

static int qmimux_stop(struct net_device *dev)
{
	netif_carrier_off(dev);
	return 0;
}

static netdev_tx_t qmimux_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct qmimux_priv *priv = netdev_priv(dev);
	unsigned int len = skb->len;
	struct qmimux_hdr *hdr;
	netdev_tx_t ret;

	hdr = skb_push(skb, sizeof(struct qmimux_hdr));
	hdr->pad = 0;
	hdr->mux_id = priv->mux_id;
	hdr->pkt_len = cpu_to_be16(len);
	skb->dev = priv->real_dev;
	ret = dev_queue_xmit(skb);

	if (likely(ret == NET_XMIT_SUCCESS || ret == NET_XMIT_CN))
		dev_sw_netstats_tx_add(dev, 1, len);
	else
		dev->stats.tx_dropped++;

	return ret;
}

static const struct net_device_ops qmimux_netdev_ops = {
	.ndo_open        = qmimux_open,
	.ndo_stop        = qmimux_stop,
	.ndo_start_xmit  = qmimux_start_xmit,
	.ndo_get_stats64 = dev_get_tstats64,
};

static void qmimux_setup(struct net_device *dev)
{
	dev->header_ops      = NULL;  /* No header */
	dev->type            = ARPHRD_NONE;
	dev->hard_header_len = 0;
	dev->addr_len        = 0;
	dev->flags           = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
	dev->netdev_ops      = &qmimux_netdev_ops;
	dev->mtu             = 1500;
	dev->needs_free_netdev = true;
}

static struct net_device *qmimux_find_dev(struct usbnet *dev, u8 mux_id)
{
	struct qmimux_priv *priv;
	struct list_head *iter;
	struct net_device *ldev;

	rcu_read_lock();
	netdev_for_each_upper_dev_rcu(dev->net, ldev, iter) {
		priv = netdev_priv(ldev);
		if (priv->mux_id == mux_id) {
			rcu_read_unlock();
			return ldev;
		}
	}
	rcu_read_unlock();
	return NULL;
}

static bool qmimux_has_slaves(struct usbnet *dev)
{
	return !list_empty(&dev->net->adj_list.upper);
}

static int qmimux_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	unsigned int len, offset = 0, pad_len, pkt_len;
	struct qmimux_hdr *hdr;
	struct net_device *net;
	struct sk_buff *skbn;
	u8 qmimux_hdr_sz = sizeof(*hdr);

	while (offset + qmimux_hdr_sz < skb->len) {
		hdr = (struct qmimux_hdr *)(skb->data + offset);
		len = be16_to_cpu(hdr->pkt_len);

		/* drop the packet, bogus length */
		if (offset + len + qmimux_hdr_sz > skb->len)
			return 0;

		/* control packet, we do not know what to do */
		if (hdr->pad & 0x80)
			goto skip;

		/* extract padding length and check for valid length info */
		pad_len = hdr->pad & 0x3f;
		if (len == 0 || pad_len >= len)
			goto skip;
		pkt_len = len - pad_len;

		net = qmimux_find_dev(dev, hdr->mux_id);
		if (!net)
			goto skip;
		skbn = netdev_alloc_skb(net, pkt_len + LL_MAX_HEADER);
		if (!skbn)
			return 0;

		switch (skb->data[offset + qmimux_hdr_sz] & 0xf0) {
		case 0x40:
			skbn->protocol = htons(ETH_P_IP);
			break;
		case 0x60:
			skbn->protocol = htons(ETH_P_IPV6);
			break;
		default:
			/* not ip - do not know what to do */
			goto skip;
		}

		skb_reserve(skbn, LL_MAX_HEADER);
		skb_put_data(skbn, skb->data + offset + qmimux_hdr_sz, pkt_len);
		if (netif_rx(skbn) != NET_RX_SUCCESS) {
			net->stats.rx_errors++;
			return 0;
		} else {
			dev_sw_netstats_rx_add(net, pkt_len);
		}

skip:
		offset += len + qmimux_hdr_sz;
	}
	return 1;
}

static ssize_t mux_id_show(struct device *d, struct device_attribute *attr, char *buf)
{
	struct net_device *dev = to_net_dev(d);
	struct qmimux_priv *priv;

	priv = netdev_priv(dev);

	return sysfs_emit(buf, "0x%02x\n", priv->mux_id);
}

static DEVICE_ATTR_RO(mux_id);

static struct attribute *qmi_wwan_sysfs_qmimux_attrs[] = {
	&dev_attr_mux_id.attr,
	NULL,
};

static struct attribute_group qmi_wwan_sysfs_qmimux_attr_group = {
	.name = "qmap",
	.attrs = qmi_wwan_sysfs_qmimux_attrs,
};

static int qmimux_register_device(struct net_device *real_dev, u8 mux_id)
{
	struct net_device *new_dev;
	struct qmimux_priv *priv;
	int err;

	new_dev = alloc_netdev(sizeof(struct qmimux_priv),
			       "qmimux%d", NET_NAME_UNKNOWN, qmimux_setup);
	if (!new_dev)
		return -ENOBUFS;

	dev_net_set(new_dev, dev_net(real_dev));
	priv = netdev_priv(new_dev);
	priv->mux_id = mux_id;
	priv->real_dev = real_dev;

	new_dev->tstats = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
	if (!new_dev->tstats) {
		err = -ENOBUFS;
		goto out_free_newdev;
	}

	new_dev->sysfs_groups[0] = &qmi_wwan_sysfs_qmimux_attr_group;

	err = register_netdevice(new_dev);
	if (err < 0)
		goto out_free_newdev;

	/* Account for reference in struct qmimux_priv_priv */
	dev_hold(real_dev);

	err = netdev_upper_dev_link(real_dev, new_dev, NULL);
	if (err)
		goto out_unregister_netdev;

	netif_stacked_transfer_operstate(real_dev, new_dev);

	return 0;

out_unregister_netdev:
	unregister_netdevice(new_dev);
	dev_put(real_dev);

out_free_newdev:
	free_netdev(new_dev);
	return err;
}

static void qmimux_unregister_device(struct net_device *dev,
				     struct list_head *head)
{
	struct qmimux_priv *priv = netdev_priv(dev);
	struct net_device *real_dev = priv->real_dev;

	free_percpu(dev->tstats);
	netdev_upper_dev_unlink(real_dev, dev);
	unregister_netdevice_queue(dev, head);

	/* Get rid of the reference to real_dev */
	dev_put(real_dev);
}

static void qmi_wwan_netdev_setup(struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);
	struct qmi_wwan_state *info = (void *)&dev->data;

	if (info->flags & QMI_WWAN_FLAG_RAWIP) {
		net->header_ops      = NULL;  /* No header */
		net->type            = ARPHRD_NONE;
		net->hard_header_len = 0;
		net->addr_len        = 0;
		net->flags           = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
		set_bit(EVENT_NO_IP_ALIGN, &dev->flags);
		netdev_dbg(net, "mode: raw IP\n");
	} else if (!net->header_ops) { /* don't bother if already set */
		ether_setup(net);
		/* Restoring min/max mtu values set originally by usbnet */
		net->min_mtu = 0;
		net->max_mtu = ETH_MAX_MTU;
		clear_bit(EVENT_NO_IP_ALIGN, &dev->flags);
		netdev_dbg(net, "mode: Ethernet\n");
	}

	/* recalculate buffers after changing hard_header_len */
	usbnet_change_mtu(net, net->mtu);
}

static ssize_t raw_ip_show(struct device *d, struct device_attribute *attr, char *buf)
{
	struct usbnet *dev = netdev_priv(to_net_dev(d));
	struct qmi_wwan_state *info = (void *)&dev->data;

	return sprintf(buf, "%c\n", info->flags & QMI_WWAN_FLAG_RAWIP ? 'Y' : 'N');
}

static ssize_t raw_ip_store(struct device *d,  struct device_attribute *attr, const char *buf, size_t len)
{
	struct usbnet *dev = netdev_priv(to_net_dev(d));
	struct qmi_wwan_state *info = (void *)&dev->data;
	bool enable;
	int ret;

	if (kstrtobool(buf, &enable))
		return -EINVAL;

	/* no change? */
	if (enable == (info->flags & QMI_WWAN_FLAG_RAWIP))
		return len;

	/* ip mode cannot be cleared when pass through mode is set */
	if (!enable && (info->flags & QMI_WWAN_FLAG_PASS_THROUGH)) {
		netdev_err(dev->net,
			   "Cannot clear ip mode on pass through device\n");
		return -EINVAL;
	}

	if (!rtnl_trylock())
		return restart_syscall();

	/* we don't want to modify a running netdev */
	if (netif_running(dev->net)) {
		netdev_err(dev->net, "Cannot change a running device\n");
		ret = -EBUSY;
		goto err;
	}

	/* let other drivers deny the change */
	ret = call_netdevice_notifiers(NETDEV_PRE_TYPE_CHANGE, dev->net);
	ret = notifier_to_errno(ret);
	if (ret) {
		netdev_err(dev->net, "Type change was refused\n");
		goto err;
	}

	if (enable)
		info->flags |= QMI_WWAN_FLAG_RAWIP;
	else
		info->flags &= ~QMI_WWAN_FLAG_RAWIP;
	qmi_wwan_netdev_setup(dev->net);
	call_netdevice_notifiers(NETDEV_POST_TYPE_CHANGE, dev->net);
	ret = len;
err:
	rtnl_unlock();
	return ret;
}

static ssize_t add_mux_show(struct device *d, struct device_attribute *attr, char *buf)
{
	struct net_device *dev = to_net_dev(d);
	struct qmimux_priv *priv;
	struct list_head *iter;
	struct net_device *ldev;
	ssize_t count = 0;

	rcu_read_lock();
	netdev_for_each_upper_dev_rcu(dev, ldev, iter) {
		priv = netdev_priv(ldev);
		count += scnprintf(&buf[count], PAGE_SIZE - count,
				   "0x%02x\n", priv->mux_id);
	}
	rcu_read_unlock();
	return count;
}

static ssize_t add_mux_store(struct device *d,  struct device_attribute *attr, const char *buf, size_t len)
{
	struct usbnet *dev = netdev_priv(to_net_dev(d));
	struct qmi_wwan_state *info = (void *)&dev->data;
	u8 mux_id;
	int ret;

	if (kstrtou8(buf, 0, &mux_id))
		return -EINVAL;

	/* mux_id [1 - 254] for compatibility with ip(8) and the rmnet driver */
	if (mux_id < 1 || mux_id > 254)
		return -EINVAL;

	if (!rtnl_trylock())
		return restart_syscall();

	if (qmimux_find_dev(dev, mux_id)) {
		netdev_err(dev->net, "mux_id already present\n");
		ret = -EINVAL;
		goto err;
	}

	ret = qmimux_register_device(dev->net, mux_id);
	if (!ret) {
		info->flags |= QMI_WWAN_FLAG_MUX;
		ret = len;
	}
err:
	rtnl_unlock();
	return ret;
}

static ssize_t del_mux_show(struct device *d, struct device_attribute *attr, char *buf)
{
	return add_mux_show(d, attr, buf);
}

static ssize_t del_mux_store(struct device *d,  struct device_attribute *attr, const char *buf, size_t len)
{
	struct usbnet *dev = netdev_priv(to_net_dev(d));
	struct qmi_wwan_state *info = (void *)&dev->data;
	struct net_device *del_dev;
	u8 mux_id;
	int ret = 0;

	if (kstrtou8(buf, 0, &mux_id))
		return -EINVAL;

	if (!rtnl_trylock())
		return restart_syscall();

	del_dev = qmimux_find_dev(dev, mux_id);
	if (!del_dev) {
		netdev_err(dev->net, "mux_id not present\n");
		ret = -EINVAL;
		goto err;
	}
	qmimux_unregister_device(del_dev, NULL);

	if (!qmimux_has_slaves(dev))
		info->flags &= ~QMI_WWAN_FLAG_MUX;
	ret = len;
err:
	rtnl_unlock();
	return ret;
}

static ssize_t pass_through_show(struct device *d,
				 struct device_attribute *attr, char *buf)
{
	struct usbnet *dev = netdev_priv(to_net_dev(d));
	struct qmi_wwan_state *info;

	info = (void *)&dev->data;
	return sprintf(buf, "%c\n",
		       info->flags & QMI_WWAN_FLAG_PASS_THROUGH ? 'Y' : 'N');
}

static ssize_t pass_through_store(struct device *d,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct usbnet *dev = netdev_priv(to_net_dev(d));
	struct qmi_wwan_state *info;
	bool enable;

	if (kstrtobool(buf, &enable))
		return -EINVAL;

	info = (void *)&dev->data;

	/* no change? */
	if (enable == (info->flags & QMI_WWAN_FLAG_PASS_THROUGH))
		return len;

	/* pass through mode can be set for raw ip devices only */
	if (!(info->flags & QMI_WWAN_FLAG_RAWIP)) {
		netdev_err(dev->net,
			   "Cannot set pass through mode on non ip device\n");
		return -EINVAL;
	}

	if (enable)
		info->flags |= QMI_WWAN_FLAG_PASS_THROUGH;
	else
		info->flags &= ~QMI_WWAN_FLAG_PASS_THROUGH;

	return len;
}

static DEVICE_ATTR_RW(raw_ip);
static DEVICE_ATTR_RW(add_mux);
static DEVICE_ATTR_RW(del_mux);
static DEVICE_ATTR_RW(pass_through);

static struct attribute *qmi_wwan_sysfs_attrs[] = {
	&dev_attr_raw_ip.attr,
	&dev_attr_add_mux.attr,
	&dev_attr_del_mux.attr,
	&dev_attr_pass_through.attr,
	NULL,
};

static struct attribute_group qmi_wwan_sysfs_attr_group = {
	.name = "qmi",
	.attrs = qmi_wwan_sysfs_attrs,
};

/* default ethernet address used by the modem */
static const u8 default_modem_addr[ETH_ALEN] = {0x02, 0x50, 0xf3};

static const u8 buggy_fw_addr[ETH_ALEN] = {0x00, 0xa0, 0xc6, 0x00, 0x00, 0x00};

/* Make up an ethernet header if the packet doesn't have one.
 *
 * A firmware bug common among several devices cause them to send raw
 * IP packets under some circumstances.  There is no way for the
 * driver/host to know when this will happen.  And even when the bug
 * hits, some packets will still arrive with an intact header.
 *
 * The supported devices are only capably of sending IPv4, IPv6 and
 * ARP packets on a point-to-point link. Any packet with an ethernet
 * header will have either our address or a broadcast/multicast
 * address as destination.  ARP packets will always have a header.
 *
 * This means that this function will reliably add the appropriate
 * header iff necessary, provided our hardware address does not start
 * with 4 or 6.
 *
 * Another common firmware bug results in all packets being addressed
 * to 00:a0:c6:00:00:00 despite the host address being different.
 * This function will also fixup such packets.
 */
static int qmi_wwan_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	struct qmi_wwan_state *info = (void *)&dev->data;
	bool rawip = info->flags & QMI_WWAN_FLAG_RAWIP;
	__be16 proto;

	/* This check is no longer done by usbnet */
	if (skb->len < dev->net->hard_header_len)
		return 0;

	if (info->flags & QMI_WWAN_FLAG_MUX)
		return qmimux_rx_fixup(dev, skb);

	if (info->flags & QMI_WWAN_FLAG_PASS_THROUGH) {
		skb->protocol = htons(ETH_P_MAP);
		return 1;
	}

	switch (skb->data[0] & 0xf0) {
	case 0x40:
		proto = htons(ETH_P_IP);
		break;
	case 0x60:
		proto = htons(ETH_P_IPV6);
		break;
	case 0x00:
		if (rawip)
			return 0;
		if (is_multicast_ether_addr(skb->data))
			return 1;
		/* possibly bogus destination - rewrite just in case */
		skb_reset_mac_header(skb);
		goto fix_dest;
	default:
		if (rawip)
			return 0;
		/* pass along other packets without modifications */
		return 1;
	}
	if (rawip) {
		skb_reset_mac_header(skb);
		skb->dev = dev->net; /* normally set by eth_type_trans */
		skb->protocol = proto;
		return 1;
	}

	if (skb_headroom(skb) < ETH_HLEN)
		return 0;
	skb_push(skb, ETH_HLEN);
	skb_reset_mac_header(skb);
	eth_hdr(skb)->h_proto = proto;
	eth_zero_addr(eth_hdr(skb)->h_source);
fix_dest:
	memcpy(eth_hdr(skb)->h_dest, dev->net->dev_addr, ETH_ALEN);
	return 1;
}

/* very simplistic detection of IPv4 or IPv6 headers */
static bool possibly_iphdr(const char *data)
{
	return (data[0] & 0xd0) == 0x40;
}

/* disallow addresses which may be confused with IP headers */
static int qmi_wwan_mac_addr(struct net_device *dev, void *p)
{
	int ret;
	struct sockaddr *addr = p;

	ret = eth_prepare_mac_addr_change(dev, p);
	if (ret < 0)
		return ret;
	if (possibly_iphdr(addr->sa_data))
		return -EADDRNOTAVAIL;
	eth_commit_mac_addr_change(dev, p);
	return 0;
}

static const struct net_device_ops qmi_wwan_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
	.ndo_start_xmit		= usbnet_start_xmit,
	.ndo_tx_timeout		= usbnet_tx_timeout,
	.ndo_change_mtu		= usbnet_change_mtu,
	.ndo_get_stats64	= dev_get_tstats64,
	.ndo_set_mac_address	= qmi_wwan_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

/* using a counter to merge subdriver requests with our own into a
 * combined state
 */
static int qmi_wwan_manage_power(struct usbnet *dev, int on)
{
	struct qmi_wwan_state *info = (void *)&dev->data;
	int rv;

	dev_dbg(&dev->intf->dev, "%s() pmcount=%d, on=%d\n", __func__,
		atomic_read(&info->pmcount), on);

	if ((on && atomic_add_return(1, &info->pmcount) == 1) ||
	    (!on && atomic_dec_and_test(&info->pmcount))) {
		/* need autopm_get/put here to ensure the usbcore sees
		 * the new value
		 */
		rv = usb_autopm_get_interface(dev->intf);
		dev->intf->needs_remote_wakeup = on;
		if (!rv)
			usb_autopm_put_interface(dev->intf);
	}
	return 0;
}

static int qmi_wwan_cdc_wdm_manage_power(struct usb_interface *intf, int on)
{
	struct usbnet *dev = usb_get_intfdata(intf);

	/* can be called while disconnecting */
	if (!dev)
		return 0;
	return qmi_wwan_manage_power(dev, on);
}

/* collect all three endpoints and register subdriver */
static int qmi_wwan_register_subdriver(struct usbnet *dev)
{
	int rv;
	struct usb_driver *subdriver = NULL;
	struct qmi_wwan_state *info = (void *)&dev->data;

	/* collect bulk endpoints */
	rv = usbnet_get_endpoints(dev, info->data);
	if (rv < 0)
		goto err;

	/* update status endpoint if separate control interface */
	if (info->control != info->data)
		dev->status = &info->control->cur_altsetting->endpoint[0];

	/* require interrupt endpoint for subdriver */
	if (!dev->status) {
		rv = -EINVAL;
		goto err;
	}

	/* for subdriver power management */
	atomic_set(&info->pmcount, 0);

	/* register subdriver */
	subdriver = usb_cdc_wdm_register(info->control, &dev->status->desc,
					 4096, WWAN_PORT_QMI,
					 &qmi_wwan_cdc_wdm_manage_power);
	if (IS_ERR(subdriver)) {
		dev_err(&info->control->dev, "subdriver registration failed\n");
		rv = PTR_ERR(subdriver);
		goto err;
	}

	/* prevent usbnet from using status endpoint */
	dev->status = NULL;

	/* save subdriver struct for suspend/resume wrappers */
	info->subdriver = subdriver;

err:
	return rv;
}

/* Send CDC SetControlLineState request, setting or clearing the DTR.
 * "Required for Autoconnect and 9x30 to wake up" according to the
 * GobiNet driver. The requirement has been verified on an MDM9230
 * based Sierra Wireless MC7455
 */
static int qmi_wwan_change_dtr(struct usbnet *dev, bool on)
{
	u8 intf = dev->intf->cur_altsetting->desc.bInterfaceNumber;

	return usbnet_write_cmd(dev, USB_CDC_REQ_SET_CONTROL_LINE_STATE,
				USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				on ? 0x01 : 0x00, intf, NULL, 0);
}

static int qmi_wwan_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int status;
	u8 *buf = intf->cur_altsetting->extra;
	int len = intf->cur_altsetting->extralen;
	struct usb_interface_descriptor *desc = &intf->cur_altsetting->desc;
	struct usb_cdc_union_desc *cdc_union;
	struct usb_cdc_ether_desc *cdc_ether;
	struct usb_driver *driver = driver_of(intf);
	struct qmi_wwan_state *info = (void *)&dev->data;
	struct usb_cdc_parsed_header hdr;

	BUILD_BUG_ON((sizeof(((struct usbnet *)0)->data) <
		      sizeof(struct qmi_wwan_state)));

	/* set up initial state */
	info->control = intf;
	info->data = intf;

	/* and a number of CDC descriptors */
	cdc_parse_cdc_header(&hdr, intf, buf, len);
	cdc_union = hdr.usb_cdc_union_desc;
	cdc_ether = hdr.usb_cdc_ether_desc;

	/* Use separate control and data interfaces if we found a CDC Union */
	if (cdc_union) {
		info->data = usb_ifnum_to_if(dev->udev,
					     cdc_union->bSlaveInterface0);
		if (desc->bInterfaceNumber != cdc_union->bMasterInterface0 ||
		    !info->data) {
			dev_err(&intf->dev,
				"bogus CDC Union: master=%u, slave=%u\n",
				cdc_union->bMasterInterface0,
				cdc_union->bSlaveInterface0);

			/* ignore and continue... */
			cdc_union = NULL;
			info->data = intf;
		}
	}

	/* errors aren't fatal - we can live with the dynamic address */
	if (cdc_ether && cdc_ether->wMaxSegmentSize) {
		dev->hard_mtu = le16_to_cpu(cdc_ether->wMaxSegmentSize);
		usbnet_get_ethernet_addr(dev, cdc_ether->iMACAddress);
	}

	/* claim data interface and set it up */
	if (info->control != info->data) {
		status = usb_driver_claim_interface(driver, info->data, dev);
		if (status < 0)
			goto err;
	}

	status = qmi_wwan_register_subdriver(dev);
	if (status < 0 && info->control != info->data) {
		usb_set_intfdata(info->data, NULL);
		usb_driver_release_interface(driver, info->data);
	}

	/* disabling remote wakeup on MDM9x30 devices has the same
	 * effect as clearing DTR. The device will not respond to QMI
	 * requests until we set DTR again.  This is similar to a
	 * QMI_CTL SYNC request, clearing a lot of firmware state
	 * including the client ID allocations.
	 *
	 * Our usage model allows a session to span multiple
	 * open/close events, so we must prevent the firmware from
	 * clearing out state the clients might need.
	 *
	 * MDM9x30 is the first QMI chipset with USB3 support. Abuse
	 * this fact to enable the quirk for all USB3 devices.
	 *
	 * There are also chipsets with the same "set DTR" requirement
	 * but without USB3 support.  Devices based on these chips
	 * need a quirk flag in the device ID table.
	 */
	if (dev->driver_info->data & QMI_WWAN_QUIRK_DTR ||
	    le16_to_cpu(dev->udev->descriptor.bcdUSB) >= 0x0201) {
		qmi_wwan_manage_power(dev, 1);
		qmi_wwan_change_dtr(dev, true);
	}

	/* Never use the same address on both ends of the link, even if the
	 * buggy firmware told us to. Or, if device is assigned the well-known
	 * buggy firmware MAC address, replace it with a random address,
	 */
	if (ether_addr_equal(dev->net->dev_addr, default_modem_addr) ||
	    ether_addr_equal(dev->net->dev_addr, buggy_fw_addr))
		eth_hw_addr_random(dev->net);

	/* make MAC addr easily distinguishable from an IP header */
	if (possibly_iphdr(dev->net->dev_addr)) {
		u8 addr = dev->net->dev_addr[0];

		addr |= 0x02;	/* set local assignment bit */
		addr &= 0xbf;	/* clear "IP" bit */
		dev_addr_mod(dev->net, 0, &addr, 1);
	}
	dev->net->netdev_ops = &qmi_wwan_netdev_ops;
	dev->net->sysfs_groups[0] = &qmi_wwan_sysfs_attr_group;
err:
	return status;
}

static void qmi_wwan_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	struct qmi_wwan_state *info = (void *)&dev->data;
	struct usb_driver *driver = driver_of(intf);
	struct usb_interface *other;

	if (info->subdriver && info->subdriver->disconnect)
		info->subdriver->disconnect(info->control);

	/* disable MDM9x30 quirk */
	if (le16_to_cpu(dev->udev->descriptor.bcdUSB) >= 0x0201) {
		qmi_wwan_change_dtr(dev, false);
		qmi_wwan_manage_power(dev, 0);
	}

	/* allow user to unbind using either control or data */
	if (intf == info->control)
		other = info->data;
	else
		other = info->control;

	/* only if not shared */
	if (other && intf != other) {
		usb_set_intfdata(other, NULL);
		usb_driver_release_interface(driver, other);
	}

	info->subdriver = NULL;
	info->data = NULL;
	info->control = NULL;
}

/* suspend/resume wrappers calling both usbnet and the cdc-wdm
 * subdriver if present.
 *
 * NOTE: cdc-wdm also supports pre/post_reset, but we cannot provide
 * wrappers for those without adding usbnet reset support first.
 */
static int qmi_wwan_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usbnet *dev = usb_get_intfdata(intf);
	struct qmi_wwan_state *info = (void *)&dev->data;
	int ret;

	/* Both usbnet_suspend() and subdriver->suspend() MUST return 0
	 * in system sleep context, otherwise, the resume callback has
	 * to recover device from previous suspend failure.
	 */
	ret = usbnet_suspend(intf, message);
	if (ret < 0)
		goto err;

	if (intf == info->control && info->subdriver &&
	    info->subdriver->suspend)
		ret = info->subdriver->suspend(intf, message);
	if (ret < 0)
		usbnet_resume(intf);
err:
	return ret;
}

static int qmi_wwan_resume(struct usb_interface *intf)
{
	struct usbnet *dev = usb_get_intfdata(intf);
	struct qmi_wwan_state *info = (void *)&dev->data;
	int ret = 0;
	bool callsub = (intf == info->control && info->subdriver &&
			info->subdriver->resume);

	if (callsub)
		ret = info->subdriver->resume(intf);
	if (ret < 0)
		goto err;
	ret = usbnet_resume(intf);
	if (ret < 0 && callsub)
		info->subdriver->suspend(intf, PMSG_SUSPEND);
err:
	return ret;
}

static const struct driver_info	qmi_wwan_info = {
	.description	= "WWAN/QMI device",
	.flags		= FLAG_WWAN | FLAG_SEND_ZLP,
	.bind		= qmi_wwan_bind,
	.unbind		= qmi_wwan_unbind,
	.manage_power	= qmi_wwan_manage_power,
	.rx_fixup       = qmi_wwan_rx_fixup,
};

static const struct driver_info	qmi_wwan_info_quirk_dtr = {
	.description	= "WWAN/QMI device",
	.flags		= FLAG_WWAN | FLAG_SEND_ZLP,
	.bind		= qmi_wwan_bind,
	.unbind		= qmi_wwan_unbind,
	.manage_power	= qmi_wwan_manage_power,
	.rx_fixup       = qmi_wwan_rx_fixup,
	.data           = QMI_WWAN_QUIRK_DTR,
};

#define HUAWEI_VENDOR_ID	0x12D1

/* map QMI/wwan function by a fixed interface number */
#define QMI_FIXED_INTF(vend, prod, num) \
	USB_DEVICE_INTERFACE_NUMBER(vend, prod, num), \
	.driver_info = (unsigned long)&qmi_wwan_info

/* devices requiring "set DTR" quirk */
#define QMI_QUIRK_SET_DTR(vend, prod, num) \
	USB_DEVICE_INTERFACE_NUMBER(vend, prod, num), \
	.driver_info = (unsigned long)&qmi_wwan_info_quirk_dtr

/* Gobi 1000 QMI/wwan interface number is 3 according to qcserial */
#define QMI_GOBI1K_DEVICE(vend, prod) \
	QMI_FIXED_INTF(vend, prod, 3)

/* Gobi 2000/3000 QMI/wwan interface number is 0 according to qcserial */
#define QMI_GOBI_DEVICE(vend, prod) \
	QMI_FIXED_INTF(vend, prod, 0)

/* Many devices have QMI and DIAG functions which are distinguishable
 * from other vendor specific functions by class, subclass and
 * protocol all being 0xff. The DIAG function has exactly 2 endpoints
 * and is silently rejected when probed.
 *
 * This makes it possible to match dynamically numbered QMI functions
 * as seen on e.g. many Quectel modems.
 */
#define QMI_MATCH_FF_FF_FF(vend, prod) \
	USB_DEVICE_AND_INTERFACE_INFO(vend, prod, USB_CLASS_VENDOR_SPEC, \
				      USB_SUBCLASS_VENDOR_SPEC, 0xff), \
	.driver_info = (unsigned long)&qmi_wwan_info_quirk_dtr

static const struct usb_device_id products[] = {
	/* 1. CDC ECM like devices match on the control interface */
	{	/* Huawei E392, E398 and possibly others sharing both device id and more... */
		USB_VENDOR_AND_INTERFACE_INFO(HUAWEI_VENDOR_ID, USB_CLASS_VENDOR_SPEC, 1, 9),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Vodafone/Huawei K5005 (12d1:14c8) and similar modems */
		USB_VENDOR_AND_INTERFACE_INFO(HUAWEI_VENDOR_ID, USB_CLASS_VENDOR_SPEC, 1, 57),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* HUAWEI_INTERFACE_NDIS_CONTROL_QUALCOMM */
		USB_VENDOR_AND_INTERFACE_INFO(HUAWEI_VENDOR_ID, USB_CLASS_VENDOR_SPEC, 0x01, 0x69),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Motorola Mapphone devices with MDM6600 */
		USB_VENDOR_AND_INTERFACE_INFO(0x22b8, USB_CLASS_VENDOR_SPEC, 0xfb, 0xff),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},

	/* 2. Combined interface devices matching on class+protocol */
	{	/* Huawei E367 and possibly others in "Windows mode" */
		USB_VENDOR_AND_INTERFACE_INFO(HUAWEI_VENDOR_ID, USB_CLASS_VENDOR_SPEC, 1, 7),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Huawei E392, E398 and possibly others in "Windows mode" */
		USB_VENDOR_AND_INTERFACE_INFO(HUAWEI_VENDOR_ID, USB_CLASS_VENDOR_SPEC, 1, 17),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* HUAWEI_NDIS_SINGLE_INTERFACE_VDF */
		USB_VENDOR_AND_INTERFACE_INFO(HUAWEI_VENDOR_ID, USB_CLASS_VENDOR_SPEC, 0x01, 0x37),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* HUAWEI_INTERFACE_NDIS_HW_QUALCOMM */
		USB_VENDOR_AND_INTERFACE_INFO(HUAWEI_VENDOR_ID, USB_CLASS_VENDOR_SPEC, 0x01, 0x67),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Pantech UML290, P4200 and more */
		USB_VENDOR_AND_INTERFACE_INFO(0x106c, USB_CLASS_VENDOR_SPEC, 0xf0, 0xff),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Pantech UML290 - newer firmware */
		USB_VENDOR_AND_INTERFACE_INFO(0x106c, USB_CLASS_VENDOR_SPEC, 0xf1, 0xff),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Novatel USB551L and MC551 */
		USB_DEVICE_AND_INTERFACE_INFO(0x1410, 0xb001,
		                              USB_CLASS_COMM,
		                              USB_CDC_SUBCLASS_ETHERNET,
		                              USB_CDC_PROTO_NONE),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Novatel E362 */
		USB_DEVICE_AND_INTERFACE_INFO(0x1410, 0x9010,
		                              USB_CLASS_COMM,
		                              USB_CDC_SUBCLASS_ETHERNET,
		                              USB_CDC_PROTO_NONE),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Novatel Expedite E371 */
		USB_DEVICE_AND_INTERFACE_INFO(0x1410, 0x9011,
		                              USB_CLASS_COMM,
		                              USB_CDC_SUBCLASS_ETHERNET,
		                              USB_CDC_PROTO_NONE),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Dell Wireless 5800 (Novatel E362) */
		USB_DEVICE_AND_INTERFACE_INFO(0x413C, 0x8195,
					      USB_CLASS_COMM,
					      USB_CDC_SUBCLASS_ETHERNET,
					      USB_CDC_PROTO_NONE),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Dell Wireless 5800 V2 (Novatel E362) */
		USB_DEVICE_AND_INTERFACE_INFO(0x413C, 0x8196,
					      USB_CLASS_COMM,
					      USB_CDC_SUBCLASS_ETHERNET,
					      USB_CDC_PROTO_NONE),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Dell Wireless 5804 (Novatel E371) */
		USB_DEVICE_AND_INTERFACE_INFO(0x413C, 0x819b,
					      USB_CLASS_COMM,
					      USB_CDC_SUBCLASS_ETHERNET,
					      USB_CDC_PROTO_NONE),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* ADU960S */
		USB_DEVICE_AND_INTERFACE_INFO(0x16d5, 0x650a,
					      USB_CLASS_COMM,
					      USB_CDC_SUBCLASS_ETHERNET,
					      USB_CDC_PROTO_NONE),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* HP lt2523 (Novatel E371) */
		USB_DEVICE_AND_INTERFACE_INFO(0x03f0, 0x421d,
					      USB_CLASS_COMM,
					      USB_CDC_SUBCLASS_ETHERNET,
					      USB_CDC_PROTO_NONE),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* HP lt4112 LTE/HSPA+ Gobi 4G Module (Huawei me906e) */
		USB_DEVICE_AND_INTERFACE_INFO(0x03f0, 0x581d, USB_CLASS_VENDOR_SPEC, 1, 7),
		.driver_info = (unsigned long)&qmi_wwan_info,
	},
	{QMI_MATCH_FF_FF_FF(0x2c7c, 0x0125)},	/* Quectel EC25, EC20 R2.0  Mini PCIe */
	{QMI_MATCH_FF_FF_FF(0x2c7c, 0x0306)},	/* Quectel EP06/EG06/EM06 */
	{QMI_MATCH_FF_FF_FF(0x2c7c, 0x0512)},	/* Quectel EG12/EM12 */
	{QMI_MATCH_FF_FF_FF(0x2c7c, 0x0620)},	/* Quectel EM160R-GL */
	{QMI_MATCH_FF_FF_FF(0x2c7c, 0x0800)},	/* Quectel RM500Q-GL */
	{QMI_MATCH_FF_FF_FF(0x2c7c, 0x0801)},	/* Quectel RM520N */

	/* 3. Combined interface devices matching on interface number */
	{QMI_FIXED_INTF(0x0408, 0xea42, 4)},	/* Yota / Megafon M100-1 */
	{QMI_FIXED_INTF(0x05c6, 0x6001, 3)},	/* 4G LTE usb-modem U901 */
	{QMI_FIXED_INTF(0x05c6, 0x7000, 0)},
	{QMI_FIXED_INTF(0x05c6, 0x7001, 1)},
	{QMI_FIXED_INTF(0x05c6, 0x7002, 1)},
	{QMI_FIXED_INTF(0x05c6, 0x7101, 1)},
	{QMI_FIXED_INTF(0x05c6, 0x7101, 2)},
	{QMI_FIXED_INTF(0x05c6, 0x7101, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x7102, 1)},
	{QMI_FIXED_INTF(0x05c6, 0x7102, 2)},
	{QMI_FIXED_INTF(0x05c6, 0x7102, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x8000, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x8001, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9000, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9003, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9005, 2)},
	{QMI_FIXED_INTF(0x05c6, 0x900a, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x900b, 2)},
	{QMI_FIXED_INTF(0x05c6, 0x900c, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x900c, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x900c, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x900d, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x900f, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x900f, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x900f, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9010, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9010, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9011, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9011, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9021, 1)},
	{QMI_FIXED_INTF(0x05c6, 0x9022, 2)},
	{QMI_QUIRK_SET_DTR(0x05c6, 0x9025, 4)},	/* Alcatel-sbell ASB TL131 TDD LTE (China Mobile) */
	{QMI_FIXED_INTF(0x05c6, 0x9026, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x902e, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9031, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9032, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9033, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9033, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9033, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9033, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9034, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9034, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9034, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9034, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9034, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9035, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9036, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9037, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9038, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x903b, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x903c, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x903d, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x903e, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9043, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9046, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9046, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9046, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9047, 2)},
	{QMI_FIXED_INTF(0x05c6, 0x9047, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9047, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9048, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9048, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9048, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9048, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9048, 8)},
	{QMI_FIXED_INTF(0x05c6, 0x904c, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x904c, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x904c, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x904c, 8)},
	{QMI_FIXED_INTF(0x05c6, 0x9050, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9052, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9053, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9053, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9054, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9054, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9055, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9055, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9055, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9055, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9055, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9056, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9062, 2)},
	{QMI_FIXED_INTF(0x05c6, 0x9062, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9062, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9062, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9062, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9062, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9062, 8)},
	{QMI_FIXED_INTF(0x05c6, 0x9062, 9)},
	{QMI_FIXED_INTF(0x05c6, 0x9064, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9065, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9065, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9066, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9066, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9067, 1)},
	{QMI_FIXED_INTF(0x05c6, 0x9068, 2)},
	{QMI_FIXED_INTF(0x05c6, 0x9068, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9068, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9068, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9068, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9068, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9069, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9069, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9069, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9069, 8)},
	{QMI_FIXED_INTF(0x05c6, 0x9070, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9070, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9075, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9076, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9076, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9076, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9076, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9076, 8)},
	{QMI_FIXED_INTF(0x05c6, 0x9077, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9077, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9077, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9077, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9078, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9079, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9079, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9079, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9079, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9079, 8)},
	{QMI_FIXED_INTF(0x05c6, 0x9080, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9080, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9080, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9080, 8)},
	{QMI_FIXED_INTF(0x05c6, 0x9083, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9084, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x90b2, 3)},    /* ublox R410M */
	{QMI_FIXED_INTF(0x05c6, 0x920d, 0)},
	{QMI_FIXED_INTF(0x05c6, 0x920d, 5)},
	{QMI_QUIRK_SET_DTR(0x05c6, 0x9625, 4)},	/* YUGA CLM920-NC5 */
	{QMI_FIXED_INTF(0x0846, 0x68a2, 8)},
	{QMI_FIXED_INTF(0x0846, 0x68d3, 8)},	/* Netgear Aircard 779S */
	{QMI_FIXED_INTF(0x12d1, 0x140c, 1)},	/* Huawei E173 */
	{QMI_FIXED_INTF(0x12d1, 0x14ac, 1)},	/* Huawei E1820 */
	{QMI_FIXED_INTF(0x1435, 0x0918, 3)},	/* Wistron NeWeb D16Q1 */
	{QMI_FIXED_INTF(0x1435, 0x0918, 4)},	/* Wistron NeWeb D16Q1 */
	{QMI_FIXED_INTF(0x1435, 0x0918, 5)},	/* Wistron NeWeb D16Q1 */
	{QMI_FIXED_INTF(0x1435, 0x3185, 4)},	/* Wistron NeWeb M18Q5 */
	{QMI_FIXED_INTF(0x1435, 0xd111, 4)},	/* M9615A DM11-1 D51QC */
	{QMI_FIXED_INTF(0x1435, 0xd181, 3)},	/* Wistron NeWeb D18Q1 */
	{QMI_FIXED_INTF(0x1435, 0xd181, 4)},	/* Wistron NeWeb D18Q1 */
	{QMI_FIXED_INTF(0x1435, 0xd181, 5)},	/* Wistron NeWeb D18Q1 */
	{QMI_FIXED_INTF(0x1435, 0xd182, 4)},	/* Wistron NeWeb D18 */
	{QMI_FIXED_INTF(0x1435, 0xd182, 5)},	/* Wistron NeWeb D18 */
	{QMI_FIXED_INTF(0x1435, 0xd191, 4)},	/* Wistron NeWeb D19Q1 */
	{QMI_QUIRK_SET_DTR(0x1508, 0x1001, 4)},	/* Fibocom NL668 series */
	{QMI_FIXED_INTF(0x1690, 0x7588, 4)},    /* ASKEY WWHC050 */
	{QMI_FIXED_INTF(0x16d8, 0x6003, 0)},	/* CMOTech 6003 */
	{QMI_FIXED_INTF(0x16d8, 0x6007, 0)},	/* CMOTech CHE-628S */
	{QMI_FIXED_INTF(0x16d8, 0x6008, 0)},	/* CMOTech CMU-301 */
	{QMI_FIXED_INTF(0x16d8, 0x6280, 0)},	/* CMOTech CHU-628 */
	{QMI_FIXED_INTF(0x16d8, 0x7001, 0)},	/* CMOTech CHU-720S */
	{QMI_FIXED_INTF(0x16d8, 0x7002, 0)},	/* CMOTech 7002 */
	{QMI_FIXED_INTF(0x16d8, 0x7003, 4)},	/* CMOTech CHU-629K */
	{QMI_FIXED_INTF(0x16d8, 0x7004, 3)},	/* CMOTech 7004 */
	{QMI_FIXED_INTF(0x16d8, 0x7006, 5)},	/* CMOTech CGU-629 */
	{QMI_FIXED_INTF(0x16d8, 0x700a, 4)},	/* CMOTech CHU-629S */
	{QMI_FIXED_INTF(0x16d8, 0x7211, 0)},	/* CMOTech CHU-720I */
	{QMI_FIXED_INTF(0x16d8, 0x7212, 0)},	/* CMOTech 7212 */
	{QMI_FIXED_INTF(0x16d8, 0x7213, 0)},	/* CMOTech 7213 */
	{QMI_FIXED_INTF(0x16d8, 0x7251, 1)},	/* CMOTech 7251 */
	{QMI_FIXED_INTF(0x16d8, 0x7252, 1)},	/* CMOTech 7252 */
	{QMI_FIXED_INTF(0x16d8, 0x7253, 1)},	/* CMOTech 7253 */
	{QMI_FIXED_INTF(0x19d2, 0x0002, 1)},
	{QMI_FIXED_INTF(0x19d2, 0x0012, 1)},
	{QMI_FIXED_INTF(0x19d2, 0x0017, 3)},
	{QMI_FIXED_INTF(0x19d2, 0x0019, 3)},	/* ONDA MT689DC */
	{QMI_FIXED_INTF(0x19d2, 0x0021, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x0025, 1)},
	{QMI_FIXED_INTF(0x19d2, 0x0031, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x0042, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x0049, 5)},
	{QMI_FIXED_INTF(0x19d2, 0x0052, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x0055, 1)},	/* ZTE (Vodafone) K3520-Z */
	{QMI_FIXED_INTF(0x19d2, 0x0058, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x0063, 4)},	/* ZTE (Vodafone) K3565-Z */
	{QMI_FIXED_INTF(0x19d2, 0x0104, 4)},	/* ZTE (Vodafone) K4505-Z */
	{QMI_FIXED_INTF(0x19d2, 0x0113, 5)},
	{QMI_FIXED_INTF(0x19d2, 0x0118, 5)},
	{QMI_FIXED_INTF(0x19d2, 0x0121, 5)},
	{QMI_FIXED_INTF(0x19d2, 0x0123, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x0124, 5)},
	{QMI_FIXED_INTF(0x19d2, 0x0125, 6)},
	{QMI_FIXED_INTF(0x19d2, 0x0126, 5)},
	{QMI_FIXED_INTF(0x19d2, 0x0130, 1)},
	{QMI_FIXED_INTF(0x19d2, 0x0133, 3)},
	{QMI_FIXED_INTF(0x19d2, 0x0141, 5)},
	{QMI_FIXED_INTF(0x19d2, 0x0157, 5)},	/* ZTE MF683 */
	{QMI_FIXED_INTF(0x19d2, 0x0158, 3)},
	{QMI_FIXED_INTF(0x19d2, 0x0167, 4)},	/* ZTE MF820D */
	{QMI_FIXED_INTF(0x19d2, 0x0168, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x0176, 3)},
	{QMI_FIXED_INTF(0x19d2, 0x0178, 3)},
	{QMI_FIXED_INTF(0x19d2, 0x0191, 4)},	/* ZTE EuFi890 */
	{QMI_FIXED_INTF(0x19d2, 0x0199, 1)},	/* ZTE MF820S */
	{QMI_FIXED_INTF(0x19d2, 0x0200, 1)},
	{QMI_FIXED_INTF(0x19d2, 0x0257, 3)},	/* ZTE MF821 */
	{QMI_FIXED_INTF(0x19d2, 0x0265, 4)},	/* ONDA MT8205 4G LTE */
	{QMI_FIXED_INTF(0x19d2, 0x0284, 4)},	/* ZTE MF880 */
	{QMI_FIXED_INTF(0x19d2, 0x0326, 4)},	/* ZTE MF821D */
	{QMI_FIXED_INTF(0x19d2, 0x0396, 3)},	/* ZTE ZM8620 */
	{QMI_FIXED_INTF(0x19d2, 0x0412, 4)},	/* Telewell TW-LTE 4G */
	{QMI_FIXED_INTF(0x19d2, 0x1008, 4)},	/* ZTE (Vodafone) K3570-Z */
	{QMI_FIXED_INTF(0x19d2, 0x1010, 4)},	/* ZTE (Vodafone) K3571-Z */
	{QMI_FIXED_INTF(0x19d2, 0x1012, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x1018, 3)},	/* ZTE (Vodafone) K5006-Z */
	{QMI_FIXED_INTF(0x19d2, 0x1021, 2)},
	{QMI_FIXED_INTF(0x19d2, 0x1245, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x1247, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x1252, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x1254, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x1255, 3)},
	{QMI_FIXED_INTF(0x19d2, 0x1255, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x1256, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x1270, 5)},	/* ZTE MF667 */
	{QMI_FIXED_INTF(0x19d2, 0x1275, 3)},	/* ZTE P685M */
	{QMI_FIXED_INTF(0x19d2, 0x1401, 2)},
	{QMI_FIXED_INTF(0x19d2, 0x1402, 2)},	/* ZTE MF60 */
	{QMI_FIXED_INTF(0x19d2, 0x1424, 2)},
	{QMI_FIXED_INTF(0x19d2, 0x1425, 2)},
	{QMI_FIXED_INTF(0x19d2, 0x1426, 2)},	/* ZTE MF91 */
	{QMI_FIXED_INTF(0x19d2, 0x1428, 2)},	/* Telewell TW-LTE 4G v2 */
	{QMI_FIXED_INTF(0x19d2, 0x1432, 3)},	/* ZTE ME3620 */
	{QMI_FIXED_INTF(0x19d2, 0x1485, 5)},	/* ZTE MF286D */
	{QMI_FIXED_INTF(0x19d2, 0x2002, 4)},	/* ZTE (Vodafone) K3765-Z */
	{QMI_FIXED_INTF(0x2001, 0x7e16, 3)},	/* D-Link DWM-221 */
	{QMI_FIXED_INTF(0x2001, 0x7e19, 4)},	/* D-Link DWM-221 B1 */
	{QMI_FIXED_INTF(0x2001, 0x7e35, 4)},	/* D-Link DWM-222 */
	{QMI_FIXED_INTF(0x2001, 0x7e3d, 4)},	/* D-Link DWM-222 A2 */
	{QMI_FIXED_INTF(0x2020, 0x2031, 4)},	/* Olicard 600 */
	{QMI_FIXED_INTF(0x2020, 0x2033, 4)},	/* BroadMobi BM806U */
	{QMI_FIXED_INTF(0x2020, 0x2060, 4)},	/* BroadMobi BM818 */
	{QMI_FIXED_INTF(0x0f3d, 0x68a2, 8)},    /* Sierra Wireless MC7700 */
	{QMI_FIXED_INTF(0x114f, 0x68a2, 8)},    /* Sierra Wireless MC7750 */
	{QMI_FIXED_INTF(0x1199, 0x68a2, 8)},	/* Sierra Wireless MC7710 in QMI mode */
	{QMI_FIXED_INTF(0x1199, 0x68a2, 19)},	/* Sierra Wireless MC7710 in QMI mode */
	{QMI_QUIRK_SET_DTR(0x1199, 0x68c0, 8)},	/* Sierra Wireless MC7304/MC7354, WP76xx */
	{QMI_QUIRK_SET_DTR(0x1199, 0x68c0, 10)},/* Sierra Wireless MC7304/MC7354 */
	{QMI_FIXED_INTF(0x1199, 0x901c, 8)},    /* Sierra Wireless EM7700 */
	{QMI_FIXED_INTF(0x1199, 0x901f, 8)},    /* Sierra Wireless EM7355 */
	{QMI_FIXED_INTF(0x1199, 0x9041, 8)},	/* Sierra Wireless MC7305/MC7355 */
	{QMI_FIXED_INTF(0x1199, 0x9041, 10)},	/* Sierra Wireless MC7305/MC7355 */
	{QMI_FIXED_INTF(0x1199, 0x9051, 8)},	/* Netgear AirCard 340U */
	{QMI_FIXED_INTF(0x1199, 0x9053, 8)},	/* Sierra Wireless Modem */
	{QMI_FIXED_INTF(0x1199, 0x9054, 8)},	/* Sierra Wireless Modem */
	{QMI_FIXED_INTF(0x1199, 0x9055, 8)},	/* Netgear AirCard 341U */
	{QMI_FIXED_INTF(0x1199, 0x9056, 8)},	/* Sierra Wireless Modem */
	{QMI_FIXED_INTF(0x1199, 0x9057, 8)},
	{QMI_FIXED_INTF(0x1199, 0x9061, 8)},	/* Sierra Wireless Modem */
	{QMI_FIXED_INTF(0x1199, 0x9063, 8)},	/* Sierra Wireless EM7305 */
	{QMI_FIXED_INTF(0x1199, 0x9063, 10)},	/* Sierra Wireless EM7305 */
	{QMI_QUIRK_SET_DTR(0x1199, 0x9071, 8)},	/* Sierra Wireless MC74xx */
	{QMI_QUIRK_SET_DTR(0x1199, 0x9071, 10)},/* Sierra Wireless MC74xx */
	{QMI_QUIRK_SET_DTR(0x1199, 0x9079, 8)},	/* Sierra Wireless EM74xx */
	{QMI_QUIRK_SET_DTR(0x1199, 0x9079, 10)},/* Sierra Wireless EM74xx */
	{QMI_QUIRK_SET_DTR(0x1199, 0x907b, 8)},	/* Sierra Wireless EM74xx */
	{QMI_QUIRK_SET_DTR(0x1199, 0x907b, 10)},/* Sierra Wireless EM74xx */
	{QMI_QUIRK_SET_DTR(0x1199, 0x9091, 8)},	/* Sierra Wireless EM7565 */
	{QMI_QUIRK_SET_DTR(0x1199, 0xc081, 8)},	/* Sierra Wireless EM7590 */
	{QMI_FIXED_INTF(0x1bbb, 0x011e, 4)},	/* Telekom Speedstick LTE II (Alcatel One Touch L100V LTE) */
	{QMI_FIXED_INTF(0x1bbb, 0x0203, 2)},	/* Alcatel L800MA */
	{QMI_FIXED_INTF(0x2357, 0x0201, 4)},	/* TP-LINK HSUPA Modem MA180 */
	{QMI_FIXED_INTF(0x2357, 0x9000, 4)},	/* TP-LINK MA260 */
	{QMI_QUIRK_SET_DTR(0x1bc7, 0x1031, 3)}, /* Telit LE910C1-EUX */
	{QMI_QUIRK_SET_DTR(0x1bc7, 0x103a, 0)}, /* Telit LE910C4-WWX */
	{QMI_QUIRK_SET_DTR(0x1bc7, 0x1040, 2)},	/* Telit LE922A */
	{QMI_QUIRK_SET_DTR(0x1bc7, 0x1050, 2)},	/* Telit FN980 */
	{QMI_QUIRK_SET_DTR(0x1bc7, 0x1057, 2)},	/* Telit FN980 */
	{QMI_QUIRK_SET_DTR(0x1bc7, 0x1060, 2)},	/* Telit LN920 */
	{QMI_QUIRK_SET_DTR(0x1bc7, 0x1070, 2)},	/* Telit FN990 */
	{QMI_FIXED_INTF(0x1bc7, 0x1100, 3)},	/* Telit ME910 */
	{QMI_FIXED_INTF(0x1bc7, 0x1101, 3)},	/* Telit ME910 dual modem */
	{QMI_FIXED_INTF(0x1bc7, 0x1200, 5)},	/* Telit LE920 */
	{QMI_QUIRK_SET_DTR(0x1bc7, 0x1201, 2)},	/* Telit LE920, LE920A4 */
	{QMI_QUIRK_SET_DTR(0x1bc7, 0x1230, 2)},	/* Telit LE910Cx */
	{QMI_QUIRK_SET_DTR(0x1bc7, 0x1250, 0)},	/* Telit LE910Cx */
	{QMI_QUIRK_SET_DTR(0x1bc7, 0x1260, 2)},	/* Telit LE910Cx */
	{QMI_QUIRK_SET_DTR(0x1bc7, 0x1261, 2)},	/* Telit LE910Cx */
	{QMI_QUIRK_SET_DTR(0x1bc7, 0x1900, 1)},	/* Telit LN940 series */
	{QMI_FIXED_INTF(0x1c9e, 0x9801, 3)},	/* Telewell TW-3G HSPA+ */
	{QMI_FIXED_INTF(0x1c9e, 0x9803, 4)},	/* Telewell TW-3G HSPA+ */
	{QMI_FIXED_INTF(0x1c9e, 0x9b01, 3)},	/* XS Stick W100-2 from 4G Systems */
	{QMI_FIXED_INTF(0x0b3c, 0xc000, 4)},	/* Olivetti Olicard 100 */
	{QMI_FIXED_INTF(0x0b3c, 0xc001, 4)},	/* Olivetti Olicard 120 */
	{QMI_FIXED_INTF(0x0b3c, 0xc002, 4)},	/* Olivetti Olicard 140 */
	{QMI_FIXED_INTF(0x0b3c, 0xc004, 6)},	/* Olivetti Olicard 155 */
	{QMI_FIXED_INTF(0x0b3c, 0xc005, 6)},	/* Olivetti Olicard 200 */
	{QMI_FIXED_INTF(0x0b3c, 0xc00a, 6)},	/* Olivetti Olicard 160 */
	{QMI_FIXED_INTF(0x0b3c, 0xc00b, 4)},	/* Olivetti Olicard 500 */
	{QMI_FIXED_INTF(0x1e2d, 0x0060, 4)},	/* Cinterion PLxx */
	{QMI_QUIRK_SET_DTR(0x1e2d, 0x006f, 8)}, /* Cinterion PLS83/PLS63 */
	{QMI_FIXED_INTF(0x1e2d, 0x0053, 4)},	/* Cinterion PHxx,PXxx */
	{QMI_FIXED_INTF(0x1e2d, 0x0063, 10)},	/* Cinterion ALASxx (1 RmNet) */
	{QMI_FIXED_INTF(0x1e2d, 0x0082, 4)},	/* Cinterion PHxx,PXxx (2 RmNet) */
	{QMI_FIXED_INTF(0x1e2d, 0x0082, 5)},	/* Cinterion PHxx,PXxx (2 RmNet) */
	{QMI_FIXED_INTF(0x1e2d, 0x0083, 4)},	/* Cinterion PHxx,PXxx (1 RmNet + USB Audio)*/
	{QMI_QUIRK_SET_DTR(0x1e2d, 0x00b0, 4)},	/* Cinterion CLS8 */
	{QMI_FIXED_INTF(0x1e2d, 0x00b7, 0)},	/* Cinterion MV31 RmNet */
	{QMI_FIXED_INTF(0x1e2d, 0x00b9, 0)},	/* Cinterion MV31 RmNet based on new baseline */
	{QMI_FIXED_INTF(0x1e2d, 0x00f3, 0)},	/* Cinterion MV32-W-A RmNet */
	{QMI_FIXED_INTF(0x1e2d, 0x00f4, 0)},	/* Cinterion MV32-W-B RmNet */
	{QMI_FIXED_INTF(0x413c, 0x81a2, 8)},	/* Dell Wireless 5806 Gobi(TM) 4G LTE Mobile Broadband Card */
	{QMI_FIXED_INTF(0x413c, 0x81a3, 8)},	/* Dell Wireless 5570 HSPA+ (42Mbps) Mobile Broadband Card */
	{QMI_FIXED_INTF(0x413c, 0x81a4, 8)},	/* Dell Wireless 5570e HSPA+ (42Mbps) Mobile Broadband Card */
	{QMI_FIXED_INTF(0x413c, 0x81a8, 8)},	/* Dell Wireless 5808 Gobi(TM) 4G LTE Mobile Broadband Card */
	{QMI_FIXED_INTF(0x413c, 0x81a9, 8)},	/* Dell Wireless 5808e Gobi(TM) 4G LTE Mobile Broadband Card */
	{QMI_FIXED_INTF(0x413c, 0x81b1, 8)},	/* Dell Wireless 5809e Gobi(TM) 4G LTE Mobile Broadband Card */
	{QMI_FIXED_INTF(0x413c, 0x81b3, 8)},	/* Dell Wireless 5809e Gobi(TM) 4G LTE Mobile Broadband Card (rev3) */
	{QMI_FIXED_INTF(0x413c, 0x81b6, 8)},	/* Dell Wireless 5811e */
	{QMI_FIXED_INTF(0x413c, 0x81b6, 10)},	/* Dell Wireless 5811e */
	{QMI_FIXED_INTF(0x413c, 0x81c2, 8)},	/* Dell Wireless 5811e */
	{QMI_FIXED_INTF(0x413c, 0x81cc, 8)},	/* Dell Wireless 5816e */
	{QMI_FIXED_INTF(0x413c, 0x81d7, 0)},	/* Dell Wireless 5821e */
	{QMI_FIXED_INTF(0x413c, 0x81d7, 1)},	/* Dell Wireless 5821e preproduction config */
	{QMI_FIXED_INTF(0x413c, 0x81e0, 0)},	/* Dell Wireless 5821e with eSIM support*/
	{QMI_FIXED_INTF(0x413c, 0x81e4, 0)},	/* Dell Wireless 5829e with eSIM support*/
	{QMI_FIXED_INTF(0x413c, 0x81e6, 0)},	/* Dell Wireless 5829e */
	{QMI_FIXED_INTF(0x03f0, 0x4e1d, 8)},	/* HP lt4111 LTE/EV-DO/HSPA+ Gobi 4G Module */
	{QMI_FIXED_INTF(0x03f0, 0x9d1d, 1)},	/* HP lt4120 Snapdragon X5 LTE */
	{QMI_QUIRK_SET_DTR(0x22de, 0x9051, 2)}, /* Hucom Wireless HM-211S/K */
	{QMI_FIXED_INTF(0x22de, 0x9061, 3)},	/* WeTelecom WPD-600N */
	{QMI_QUIRK_SET_DTR(0x1e0e, 0x9001, 5)},	/* SIMCom 7100E, 7230E, 7600E ++ */
	{QMI_QUIRK_SET_DTR(0x2c7c, 0x0121, 4)},	/* Quectel EC21 Mini PCIe */
	{QMI_QUIRK_SET_DTR(0x2c7c, 0x0191, 4)},	/* Quectel EG91 */
	{QMI_QUIRK_SET_DTR(0x2c7c, 0x0195, 4)},	/* Quectel EG95 */
	{QMI_FIXED_INTF(0x2c7c, 0x0296, 4)},	/* Quectel BG96 */
	{QMI_QUIRK_SET_DTR(0x2cb7, 0x0104, 4)},	/* Fibocom NL678 series */
	{QMI_FIXED_INTF(0x0489, 0xe0b4, 0)},	/* Foxconn T77W968 LTE */
	{QMI_FIXED_INTF(0x0489, 0xe0b5, 0)},	/* Foxconn T77W968 LTE with eSIM support*/
	{QMI_FIXED_INTF(0x2692, 0x9025, 4)},    /* Cellient MPL200 (rebranded Qualcomm 05c6:9025) */
	{QMI_QUIRK_SET_DTR(0x1546, 0x1342, 4)},	/* u-blox LARA-L6 */

	/* 4. Gobi 1000 devices */
	{QMI_GOBI1K_DEVICE(0x05c6, 0x9212)},	/* Acer Gobi Modem Device */
	{QMI_GOBI1K_DEVICE(0x03f0, 0x1f1d)},	/* HP un2400 Gobi Modem Device */
	{QMI_GOBI1K_DEVICE(0x04da, 0x250d)},	/* Panasonic Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x413c, 0x8172)},	/* Dell Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x1410, 0xa001)},	/* Novatel/Verizon USB-1000 */
	{QMI_GOBI1K_DEVICE(0x1410, 0xa002)},	/* Novatel Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x1410, 0xa003)},	/* Novatel Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x1410, 0xa004)},	/* Novatel Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x1410, 0xa005)},	/* Novatel Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x1410, 0xa006)},	/* Novatel Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x1410, 0xa007)},	/* Novatel Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x0b05, 0x1776)},	/* Asus Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x19d2, 0xfff3)},	/* ONDA Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x05c6, 0x9001)},	/* Generic Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x05c6, 0x9002)},	/* Generic Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x05c6, 0x9202)},	/* Generic Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x05c6, 0x9203)},	/* Generic Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x05c6, 0x9222)},	/* Generic Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x05c6, 0x9009)},	/* Generic Gobi Modem device */

	/* 5. Gobi 2000 and 3000 devices */
	{QMI_GOBI_DEVICE(0x413c, 0x8186)},	/* Dell Gobi 2000 Modem device (N0218, VU936) */
	{QMI_GOBI_DEVICE(0x413c, 0x8194)},	/* Dell Gobi 3000 Composite */
	{QMI_GOBI_DEVICE(0x05c6, 0x920b)},	/* Generic Gobi 2000 Modem device */
	{QMI_GOBI_DEVICE(0x05c6, 0x9225)},	/* Sony Gobi 2000 Modem device (N0279, VU730) */
	{QMI_GOBI_DEVICE(0x05c6, 0x9245)},	/* Samsung Gobi 2000 Modem device (VL176) */
	{QMI_GOBI_DEVICE(0x03f0, 0x251d)},	/* HP Gobi 2000 Modem device (VP412) */
	{QMI_GOBI_DEVICE(0x05c6, 0x9215)},	/* Acer Gobi 2000 Modem device (VP413) */
	{QMI_FIXED_INTF(0x05c6, 0x9215, 4)},	/* Quectel EC20 Mini PCIe */
	{QMI_GOBI_DEVICE(0x05c6, 0x9265)},	/* Asus Gobi 2000 Modem device (VR305) */
	{QMI_GOBI_DEVICE(0x05c6, 0x9235)},	/* Top Global Gobi 2000 Modem device (VR306) */
	{QMI_GOBI_DEVICE(0x05c6, 0x9275)},	/* iRex Technologies Gobi 2000 Modem device (VR307) */
	{QMI_GOBI_DEVICE(0x0af0, 0x8120)},	/* Option GTM681W */
	{QMI_GOBI_DEVICE(0x1199, 0x68a5)},	/* Sierra Wireless Modem */
	{QMI_GOBI_DEVICE(0x1199, 0x68a9)},	/* Sierra Wireless Modem */
	{QMI_GOBI_DEVICE(0x1199, 0x9001)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9002)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9003)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9004)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9005)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9006)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9007)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9008)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9009)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x900a)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9011)},	/* Sierra Wireless Gobi 2000 Modem device (MC8305) */
	{QMI_GOBI_DEVICE(0x16d8, 0x8002)},	/* CMDTech Gobi 2000 Modem device (VU922) */
	{QMI_GOBI_DEVICE(0x05c6, 0x9205)},	/* Gobi 2000 Modem device */
	{QMI_GOBI_DEVICE(0x1199, 0x9013)},	/* Sierra Wireless Gobi 3000 Modem device (MC8355) */
	{QMI_GOBI_DEVICE(0x03f0, 0x371d)},	/* HP un2430 Mobile Broadband Module */
	{QMI_GOBI_DEVICE(0x1199, 0x9015)},	/* Sierra Wireless Gobi 3000 Modem device */
	{QMI_GOBI_DEVICE(0x1199, 0x9019)},	/* Sierra Wireless Gobi 3000 Modem device */
	{QMI_GOBI_DEVICE(0x1199, 0x901b)},	/* Sierra Wireless MC7770 */
	{QMI_GOBI_DEVICE(0x12d1, 0x14f1)},	/* Sony Gobi 3000 Composite */
	{QMI_GOBI_DEVICE(0x1410, 0xa021)},	/* Foxconn Gobi 3000 Modem device (Novatel E396) */

	{ }					/* END */
};
MODULE_DEVICE_TABLE(usb, products);

static bool quectel_ec20_detected(struct usb_interface *intf)
{
	struct usb_device *dev = interface_to_usbdev(intf);

	if (dev->actconfig &&
	    le16_to_cpu(dev->descriptor.idVendor) == 0x05c6 &&
	    le16_to_cpu(dev->descriptor.idProduct) == 0x9215 &&
	    dev->actconfig->desc.bNumInterfaces == 5)
		return true;

	return false;
}

static int qmi_wwan_probe(struct usb_interface *intf,
			  const struct usb_device_id *prod)
{
	struct usb_device_id *id = (struct usb_device_id *)prod;
	struct usb_interface_descriptor *desc = &intf->cur_altsetting->desc;

	/* Workaround to enable dynamic IDs.  This disables usbnet
	 * blacklisting functionality.  Which, if required, can be
	 * reimplemented here by using a magic "blacklist" value
	 * instead of 0 in the static device id table
	 */
	if (!id->driver_info) {
		dev_dbg(&intf->dev, "setting defaults for dynamic device id\n");
		id->driver_info = (unsigned long)&qmi_wwan_info;
	}

	/* There are devices where the same interface number can be
	 * configured as different functions. We should only bind to
	 * vendor specific functions when matching on interface number
	 */
	if (id->match_flags & USB_DEVICE_ID_MATCH_INT_NUMBER &&
	    desc->bInterfaceClass != USB_CLASS_VENDOR_SPEC) {
		dev_dbg(&intf->dev,
			"Rejecting interface number match for class %02x\n",
			desc->bInterfaceClass);
		return -ENODEV;
	}

	/* Quectel EC20 quirk where we've QMI on interface 4 instead of 0 */
	if (quectel_ec20_detected(intf) && desc->bInterfaceNumber == 0) {
		dev_dbg(&intf->dev, "Quectel EC20 quirk, skipping interface 0\n");
		return -ENODEV;
	}

	/* Several Quectel modems supports dynamic interface configuration, so
	 * we need to match on class/subclass/protocol. These values are
	 * identical for the diagnostic- and QMI-interface, but bNumEndpoints is
	 * different. Ignore the current interface if the number of endpoints
	 * equals the number for the diag interface (two).
	 */
	if (desc->bNumEndpoints == 2)
		return -ENODEV;

	return usbnet_probe(intf, id);
}

static void qmi_wwan_disconnect(struct usb_interface *intf)
{
	struct usbnet *dev = usb_get_intfdata(intf);
	struct qmi_wwan_state *info;
	struct list_head *iter;
	struct net_device *ldev;
	LIST_HEAD(list);

	/* called twice if separate control and data intf */
	if (!dev)
		return;
	info = (void *)&dev->data;
	if (info->flags & QMI_WWAN_FLAG_MUX) {
		if (!rtnl_trylock()) {
			restart_syscall();
			return;
		}
		rcu_read_lock();
		netdev_for_each_upper_dev_rcu(dev->net, ldev, iter)
			qmimux_unregister_device(ldev, &list);
		rcu_read_unlock();
		unregister_netdevice_many(&list);
		rtnl_unlock();
		info->flags &= ~QMI_WWAN_FLAG_MUX;
	}
	usbnet_disconnect(intf);
}

static struct usb_driver qmi_wwan_driver = {
	.name		      = "qmi_wwan",
	.id_table	      = products,
	.probe		      = qmi_wwan_probe,
	.disconnect	      = qmi_wwan_disconnect,
	.suspend	      = qmi_wwan_suspend,
	.resume		      =	qmi_wwan_resume,
	.reset_resume         = qmi_wwan_resume,
	.supports_autosuspend = 1,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(qmi_wwan_driver);

MODULE_AUTHOR("Bjørn Mork <bjorn@mork.no>");
MODULE_DESCRIPTION("Qualcomm MSM Interface (QMI) WWAN driver");
MODULE_LICENSE("GPL");
