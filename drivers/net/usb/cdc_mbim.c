// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012  Smith Micro Software, Inc.
 * Copyright (c) 2012  Bjørn Mork <bjorn@mork.no>
 *
 * This driver is based on and reuse most of cdc_ncm, which is
 * Copyright (C) ST-Ericsson 2010-2012
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/usb/usbnet.h>
#include <linux/usb/cdc-wdm.h>
#include <linux/usb/cdc_ncm.h>
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/ipv6_stubs.h>

/* alternative VLAN for IP session 0 if not untagged */
#define MBIM_IPS0_VID	4094

/* driver specific data - must match cdc_ncm usage */
struct cdc_mbim_state {
	struct cdc_ncm_ctx *ctx;
	atomic_t pmcount;
	struct usb_driver *subdriver;
	unsigned long _unused;
	unsigned long flags;
};

/* flags for the cdc_mbim_state.flags field */
enum cdc_mbim_flags {
	FLAG_IPS0_VLAN = 1 << 0,	/* IP session 0 is tagged  */
};

/* using a counter to merge subdriver requests with our own into a combined state */
static int cdc_mbim_manage_power(struct usbnet *dev, int on)
{
	struct cdc_mbim_state *info = (void *)&dev->data;
	int rv = 0;

	dev_dbg(&dev->intf->dev, "%s() pmcount=%d, on=%d\n", __func__, atomic_read(&info->pmcount), on);

	if ((on && atomic_add_return(1, &info->pmcount) == 1) || (!on && atomic_dec_and_test(&info->pmcount))) {
		/* need autopm_get/put here to ensure the usbcore sees the new value */
		rv = usb_autopm_get_interface(dev->intf);
		dev->intf->needs_remote_wakeup = on;
		if (!rv)
			usb_autopm_put_interface(dev->intf);
	}
	return 0;
}

static int cdc_mbim_wdm_manage_power(struct usb_interface *intf, int status)
{
	struct usbnet *dev = usb_get_intfdata(intf);

	/* can be called while disconnecting */
	if (!dev)
		return 0;

	return cdc_mbim_manage_power(dev, status);
}

static int cdc_mbim_rx_add_vid(struct net_device *netdev, __be16 proto, u16 vid)
{
	struct usbnet *dev = netdev_priv(netdev);
	struct cdc_mbim_state *info = (void *)&dev->data;

	/* creation of this VLAN is a request to tag IP session 0 */
	if (vid == MBIM_IPS0_VID)
		info->flags |= FLAG_IPS0_VLAN;
	else
		if (vid >= 512)	/* we don't map these to MBIM session */
			return -EINVAL;
	return 0;
}

static int cdc_mbim_rx_kill_vid(struct net_device *netdev, __be16 proto, u16 vid)
{
	struct usbnet *dev = netdev_priv(netdev);
	struct cdc_mbim_state *info = (void *)&dev->data;

	/* this is a request for an untagged IP session 0 */
	if (vid == MBIM_IPS0_VID)
		info->flags &= ~FLAG_IPS0_VLAN;
	return 0;
}

static const struct net_device_ops cdc_mbim_netdev_ops = {
	.ndo_open             = usbnet_open,
	.ndo_stop             = usbnet_stop,
	.ndo_start_xmit       = usbnet_start_xmit,
	.ndo_tx_timeout       = usbnet_tx_timeout,
	.ndo_get_stats64      = dev_get_tstats64,
	.ndo_change_mtu       = cdc_ncm_change_mtu,
	.ndo_set_mac_address  = eth_mac_addr,
	.ndo_validate_addr    = eth_validate_addr,
	.ndo_vlan_rx_add_vid  = cdc_mbim_rx_add_vid,
	.ndo_vlan_rx_kill_vid = cdc_mbim_rx_kill_vid,
};

/* Change the control interface altsetting and update the .driver_info
 * pointer if the matching entry after changing class codes points to
 * a different struct
 */
static int cdc_mbim_set_ctrlalt(struct usbnet *dev, struct usb_interface *intf, u8 alt)
{
	struct usb_driver *driver = to_usb_driver(intf->dev.driver);
	const struct usb_device_id *id;
	struct driver_info *info;
	int ret;

	ret = usb_set_interface(dev->udev,
				intf->cur_altsetting->desc.bInterfaceNumber,
				alt);
	if (ret)
		return ret;

	id = usb_match_id(intf, driver->id_table);
	if (!id)
		return -ENODEV;

	info = (struct driver_info *)id->driver_info;
	if (info != dev->driver_info) {
		dev_dbg(&intf->dev, "driver_info updated to '%s'\n",
			info->description);
		dev->driver_info = info;
	}
	return 0;
}

static int cdc_mbim_bind(struct usbnet *dev, struct usb_interface *intf)
{
	struct cdc_ncm_ctx *ctx;
	struct usb_driver *subdriver = ERR_PTR(-ENODEV);
	int ret = -ENODEV;
	u8 data_altsetting = 1;
	struct cdc_mbim_state *info = (void *)&dev->data;

	/* should we change control altsetting on a NCM/MBIM function? */
	if (cdc_ncm_select_altsetting(intf) == CDC_NCM_COMM_ALTSETTING_MBIM) {
		data_altsetting = CDC_NCM_DATA_ALTSETTING_MBIM;
		ret = cdc_mbim_set_ctrlalt(dev, intf, CDC_NCM_COMM_ALTSETTING_MBIM);
		if (ret)
			goto err;
		ret = -ENODEV;
	}

	/* we will hit this for NCM/MBIM functions if prefer_mbim is false */
	if (!cdc_ncm_comm_intf_is_mbim(intf->cur_altsetting))
		goto err;

	ret = cdc_ncm_bind_common(dev, intf, data_altsetting, dev->driver_info->data);
	if (ret)
		goto err;

	ctx = info->ctx;

	/* The MBIM descriptor and the status endpoint are required */
	if (ctx->mbim_desc && dev->status)
		subdriver = usb_cdc_wdm_register(ctx->control,
						 &dev->status->desc,
						 le16_to_cpu(ctx->mbim_desc->wMaxControlMessage),
						 WWAN_PORT_MBIM,
						 cdc_mbim_wdm_manage_power);
	if (IS_ERR(subdriver)) {
		ret = PTR_ERR(subdriver);
		cdc_ncm_unbind(dev, intf);
		goto err;
	}

	/* can't let usbnet use the interrupt endpoint */
	dev->status = NULL;
	info->subdriver = subdriver;

	/* MBIM cannot do ARP */
	dev->net->flags |= IFF_NOARP;

	/* no need to put the VLAN tci in the packet headers */
	dev->net->features |= NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_FILTER;

	/* monitor VLAN additions and removals */
	dev->net->netdev_ops = &cdc_mbim_netdev_ops;
err:
	return ret;
}

static void cdc_mbim_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	struct cdc_mbim_state *info = (void *)&dev->data;
	struct cdc_ncm_ctx *ctx = info->ctx;

	/* disconnect subdriver from control interface */
	if (info->subdriver && info->subdriver->disconnect)
		info->subdriver->disconnect(ctx->control);
	info->subdriver = NULL;

	/* let NCM unbind clean up both control and data interface */
	cdc_ncm_unbind(dev, intf);
}

/* verify that the ethernet protocol is IPv4 or IPv6 */
static bool is_ip_proto(__be16 proto)
{
	switch (proto) {
	case htons(ETH_P_IP):
	case htons(ETH_P_IPV6):
		return true;
	}
	return false;
}

static struct sk_buff *cdc_mbim_tx_fixup(struct usbnet *dev, struct sk_buff *skb, gfp_t flags)
{
	struct sk_buff *skb_out;
	struct cdc_mbim_state *info = (void *)&dev->data;
	struct cdc_ncm_ctx *ctx = info->ctx;
	__le32 sign = cpu_to_le32(USB_CDC_MBIM_NDP16_IPS_SIGN);
	u16 tci = 0;
	bool is_ip;
	u8 *c;

	if (!ctx)
		goto error;

	if (skb) {
		if (skb->len <= ETH_HLEN)
			goto error;

		/* Some applications using e.g. packet sockets will
		 * bypass the VLAN acceleration and create tagged
		 * ethernet frames directly.  We primarily look for
		 * the accelerated out-of-band tag, but fall back if
		 * required
		 */
		skb_reset_mac_header(skb);
		if (vlan_get_tag(skb, &tci) < 0 && skb->len > VLAN_ETH_HLEN &&
		    __vlan_get_tag(skb, &tci) == 0) {
			is_ip = is_ip_proto(vlan_eth_hdr(skb)->h_vlan_encapsulated_proto);
			skb_pull(skb, VLAN_ETH_HLEN);
		} else {
			is_ip = is_ip_proto(eth_hdr(skb)->h_proto);
			skb_pull(skb, ETH_HLEN);
		}

		/* Is IP session <0> tagged too? */
		if (info->flags & FLAG_IPS0_VLAN) {
			/* drop all untagged packets */
			if (!tci)
				goto error;
			/* map MBIM_IPS0_VID to IPS<0> */
			if (tci == MBIM_IPS0_VID)
				tci = 0;
		}

		/* mapping VLANs to MBIM sessions:
		 *   no tag     => IPS session <0> if !FLAG_IPS0_VLAN
		 *   1 - 255    => IPS session <vlanid>
		 *   256 - 511  => DSS session <vlanid - 256>
		 *   512 - 4093 => unsupported, drop
		 *   4094       => IPS session <0> if FLAG_IPS0_VLAN
		 */

		switch (tci & 0x0f00) {
		case 0x0000: /* VLAN ID 0 - 255 */
			if (!is_ip)
				goto error;
			c = (u8 *)&sign;
			c[3] = tci;
			break;
		case 0x0100: /* VLAN ID 256 - 511 */
			if (is_ip)
				goto error;
			sign = cpu_to_le32(USB_CDC_MBIM_NDP16_DSS_SIGN);
			c = (u8 *)&sign;
			c[3] = tci;
			break;
		default:
			netif_err(dev, tx_err, dev->net,
				  "unsupported tci=0x%04x\n", tci);
			goto error;
		}
	}

	spin_lock_bh(&ctx->mtx);
	skb_out = cdc_ncm_fill_tx_frame(dev, skb, sign);
	spin_unlock_bh(&ctx->mtx);
	return skb_out;

error:
	if (skb)
		dev_kfree_skb_any(skb);

	return NULL;
}

/* Some devices are known to send Neighbor Solicitation messages and
 * require Neighbor Advertisement replies.  The IPv6 core will not
 * respond since IFF_NOARP is set, so we must handle them ourselves.
 */
static void do_neigh_solicit(struct usbnet *dev, u8 *buf, u16 tci)
{
	struct ipv6hdr *iph = (void *)buf;
	struct nd_msg *msg = (void *)(iph + 1);
	struct net_device *netdev;
	struct inet6_dev *in6_dev;
	bool is_router;

	/* we'll only respond to requests from unicast addresses to
	 * our solicited node addresses.
	 */
	if (!ipv6_addr_is_solict_mult(&iph->daddr) ||
	    !(ipv6_addr_type(&iph->saddr) & IPV6_ADDR_UNICAST))
		return;

	/* need to send the NA on the VLAN dev, if any */
	rcu_read_lock();
	if (tci) {
		netdev = __vlan_find_dev_deep_rcu(dev->net, htons(ETH_P_8021Q),
						  tci);
		if (!netdev) {
			rcu_read_unlock();
			return;
		}
	} else {
		netdev = dev->net;
	}
	dev_hold(netdev);
	rcu_read_unlock();

	in6_dev = in6_dev_get(netdev);
	if (!in6_dev)
		goto out;
	is_router = !!in6_dev->cnf.forwarding;
	in6_dev_put(in6_dev);

	/* ipv6_stub != NULL if in6_dev_get returned an inet6_dev */
	ipv6_stub->ndisc_send_na(netdev, &iph->saddr, &msg->target,
				 is_router /* router */,
				 true /* solicited */,
				 false /* override */,
				 true /* inc_opt */);
out:
	dev_put(netdev);
}

static bool is_neigh_solicit(u8 *buf, size_t len)
{
	struct ipv6hdr *iph = (void *)buf;
	struct nd_msg *msg = (void *)(iph + 1);

	return (len >= sizeof(struct ipv6hdr) + sizeof(struct nd_msg) &&
		iph->nexthdr == IPPROTO_ICMPV6 &&
		msg->icmph.icmp6_code == 0 &&
		msg->icmph.icmp6_type == NDISC_NEIGHBOUR_SOLICITATION);
}


static struct sk_buff *cdc_mbim_process_dgram(struct usbnet *dev, u8 *buf, size_t len, u16 tci)
{
	__be16 proto = htons(ETH_P_802_3);
	struct sk_buff *skb = NULL;

	if (tci < 256 || tci == MBIM_IPS0_VID) { /* IPS session? */
		if (len < sizeof(struct iphdr))
			goto err;

		switch (*buf & 0xf0) {
		case 0x40:
			proto = htons(ETH_P_IP);
			break;
		case 0x60:
			if (is_neigh_solicit(buf, len))
				do_neigh_solicit(dev, buf, tci);
			proto = htons(ETH_P_IPV6);
			break;
		default:
			goto err;
		}
	}

	skb = netdev_alloc_skb_ip_align(dev->net,  len + ETH_HLEN);
	if (!skb)
		goto err;

	/* add an ethernet header */
	skb_put(skb, ETH_HLEN);
	skb_reset_mac_header(skb);
	eth_hdr(skb)->h_proto = proto;
	eth_zero_addr(eth_hdr(skb)->h_source);
	memcpy(eth_hdr(skb)->h_dest, dev->net->dev_addr, ETH_ALEN);

	/* add datagram */
	skb_put_data(skb, buf, len);

	/* map MBIM session to VLAN */
	if (tci)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), tci);
err:
	return skb;
}

static int cdc_mbim_rx_fixup(struct usbnet *dev, struct sk_buff *skb_in)
{
	struct sk_buff *skb;
	struct cdc_mbim_state *info = (void *)&dev->data;
	struct cdc_ncm_ctx *ctx = info->ctx;
	int len;
	int nframes;
	int x;
	int offset;
	struct usb_cdc_ncm_ndp16 *ndp16;
	struct usb_cdc_ncm_dpe16 *dpe16;
	int ndpoffset;
	int loopcount = 50; /* arbitrary max preventing infinite loop */
	u32 payload = 0;
	u8 *c;
	u16 tci;

	ndpoffset = cdc_ncm_rx_verify_nth16(ctx, skb_in);
	if (ndpoffset < 0)
		goto error;

next_ndp:
	nframes = cdc_ncm_rx_verify_ndp16(skb_in, ndpoffset);
	if (nframes < 0)
		goto error;

	ndp16 = (struct usb_cdc_ncm_ndp16 *)(skb_in->data + ndpoffset);

	switch (ndp16->dwSignature & cpu_to_le32(0x00ffffff)) {
	case cpu_to_le32(USB_CDC_MBIM_NDP16_IPS_SIGN):
		c = (u8 *)&ndp16->dwSignature;
		tci = c[3];
		/* tag IPS<0> packets too if MBIM_IPS0_VID exists */
		if (!tci && info->flags & FLAG_IPS0_VLAN)
			tci = MBIM_IPS0_VID;
		break;
	case cpu_to_le32(USB_CDC_MBIM_NDP16_DSS_SIGN):
		c = (u8 *)&ndp16->dwSignature;
		tci = c[3] + 256;
		break;
	default:
		netif_dbg(dev, rx_err, dev->net,
			  "unsupported NDP signature <0x%08x>\n",
			  le32_to_cpu(ndp16->dwSignature));
		goto err_ndp;

	}

	dpe16 = ndp16->dpe16;
	for (x = 0; x < nframes; x++, dpe16++) {
		offset = le16_to_cpu(dpe16->wDatagramIndex);
		len = le16_to_cpu(dpe16->wDatagramLength);

		/*
		 * CDC NCM ch. 3.7
		 * All entries after first NULL entry are to be ignored
		 */
		if ((offset == 0) || (len == 0)) {
			if (!x)
				goto err_ndp; /* empty NTB */
			break;
		}

		/* sanity checking */
		if (((offset + len) > skb_in->len) || (len > ctx->rx_max)) {
			netif_dbg(dev, rx_err, dev->net,
				  "invalid frame detected (ignored) offset[%u]=%u, length=%u, skb=%p\n",
				  x, offset, len, skb_in);
			if (!x)
				goto err_ndp;
			break;
		} else {
			skb = cdc_mbim_process_dgram(dev, skb_in->data + offset, len, tci);
			if (!skb)
				goto error;
			usbnet_skb_return(dev, skb);
			payload += len;	/* count payload bytes in this NTB */
		}
	}
err_ndp:
	/* are there more NDPs to process? */
	ndpoffset = le16_to_cpu(ndp16->wNextNdpIndex);
	if (ndpoffset && loopcount--)
		goto next_ndp;

	/* update stats */
	ctx->rx_overhead += skb_in->len - payload;
	ctx->rx_ntbs++;

	return 1;
error:
	return 0;
}

static int cdc_mbim_suspend(struct usb_interface *intf, pm_message_t message)
{
	int ret = -ENODEV;
	struct usbnet *dev = usb_get_intfdata(intf);
	struct cdc_mbim_state *info = (void *)&dev->data;
	struct cdc_ncm_ctx *ctx = info->ctx;

	if (!ctx)
		goto error;

	/*
	 * Both usbnet_suspend() and subdriver->suspend() MUST return 0
	 * in system sleep context, otherwise, the resume callback has
	 * to recover device from previous suspend failure.
	 */
	ret = usbnet_suspend(intf, message);
	if (ret < 0)
		goto error;

	if (intf == ctx->control && info->subdriver && info->subdriver->suspend)
		ret = info->subdriver->suspend(intf, message);
	if (ret < 0)
		usbnet_resume(intf);

error:
	return ret;
}

static int cdc_mbim_resume(struct usb_interface *intf)
{
	int  ret = 0;
	struct usbnet *dev = usb_get_intfdata(intf);
	struct cdc_mbim_state *info = (void *)&dev->data;
	struct cdc_ncm_ctx *ctx = info->ctx;
	bool callsub = (intf == ctx->control && info->subdriver && info->subdriver->resume);

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

static const struct driver_info cdc_mbim_info = {
	.description = "CDC MBIM",
	.flags = FLAG_NO_SETINT | FLAG_MULTI_PACKET | FLAG_WWAN,
	.bind = cdc_mbim_bind,
	.unbind = cdc_mbim_unbind,
	.manage_power = cdc_mbim_manage_power,
	.rx_fixup = cdc_mbim_rx_fixup,
	.tx_fixup = cdc_mbim_tx_fixup,
};

/* MBIM and NCM devices should not need a ZLP after NTBs with
 * dwNtbOutMaxSize length. Nevertheless, a number of devices from
 * different vendor IDs will fail unless we send ZLPs, forcing us
 * to make this the default.
 *
 * This default may cause a performance penalty for spec conforming
 * devices wanting to take advantage of optimizations possible without
 * ZLPs.  A whitelist is added in an attempt to avoid this for devices
 * known to conform to the MBIM specification.
 *
 * All known devices supporting NCM compatibility mode are also
 * conforming to the NCM and MBIM specifications. For this reason, the
 * NCM subclass entry is also in the ZLP whitelist.
 */
static const struct driver_info cdc_mbim_info_zlp = {
	.description = "CDC MBIM",
	.flags = FLAG_NO_SETINT | FLAG_MULTI_PACKET | FLAG_WWAN | FLAG_SEND_ZLP,
	.bind = cdc_mbim_bind,
	.unbind = cdc_mbim_unbind,
	.manage_power = cdc_mbim_manage_power,
	.rx_fixup = cdc_mbim_rx_fixup,
	.tx_fixup = cdc_mbim_tx_fixup,
};

/* The spefication explicitly allows NDPs to be placed anywhere in the
 * frame, but some devices fail unless the NDP is placed after the IP
 * packets.  Using the CDC_NCM_FLAG_NDP_TO_END flags to force this
 * behaviour.
 *
 * Note: The current implementation of this feature restricts each NTB
 * to a single NDP, implying that multiplexed sessions cannot share an
 * NTB. This might affect performance for multiplexed sessions.
 */
static const struct driver_info cdc_mbim_info_ndp_to_end = {
	.description = "CDC MBIM",
	.flags = FLAG_NO_SETINT | FLAG_MULTI_PACKET | FLAG_WWAN,
	.bind = cdc_mbim_bind,
	.unbind = cdc_mbim_unbind,
	.manage_power = cdc_mbim_manage_power,
	.rx_fixup = cdc_mbim_rx_fixup,
	.tx_fixup = cdc_mbim_tx_fixup,
	.data = CDC_NCM_FLAG_NDP_TO_END,
};

/* Some modems (e.g. Telit LE922A6) do not work properly with altsetting
 * toggle done in cdc_ncm_bind_common. CDC_MBIM_FLAG_AVOID_ALTSETTING_TOGGLE
 * flag is used to avoid this procedure.
 */
static const struct driver_info cdc_mbim_info_avoid_altsetting_toggle = {
	.description = "CDC MBIM",
	.flags = FLAG_NO_SETINT | FLAG_MULTI_PACKET | FLAG_WWAN | FLAG_SEND_ZLP,
	.bind = cdc_mbim_bind,
	.unbind = cdc_mbim_unbind,
	.manage_power = cdc_mbim_manage_power,
	.rx_fixup = cdc_mbim_rx_fixup,
	.tx_fixup = cdc_mbim_tx_fixup,
	.data = CDC_MBIM_FLAG_AVOID_ALTSETTING_TOGGLE,
};

static const struct usb_device_id mbim_devs[] = {
	/* This duplicate NCM entry is intentional. MBIM devices can
	 * be disguised as NCM by default, and this is necessary to
	 * allow us to bind the correct driver_info to such devices.
	 *
	 * bind() will sort out this for us, selecting the correct
	 * entry and reject the other
	 */
	{ USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_NCM, USB_CDC_PROTO_NONE),
	  .driver_info = (unsigned long)&cdc_mbim_info,
	},
	/* ZLP conformance whitelist: All Ericsson MBIM devices */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0bdb, USB_CLASS_COMM, USB_CDC_SUBCLASS_MBIM, USB_CDC_PROTO_NONE),
	  .driver_info = (unsigned long)&cdc_mbim_info,
	},

	/* Some Huawei devices, ME906s-158 (12d1:15c1) and E3372
	 * (12d1:157d), are known to fail unless the NDP is placed
	 * after the IP packets.  Applying the quirk to all Huawei
	 * devices is broader than necessary, but harmless.
	 */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x12d1, USB_CLASS_COMM, USB_CDC_SUBCLASS_MBIM, USB_CDC_PROTO_NONE),
	  .driver_info = (unsigned long)&cdc_mbim_info_ndp_to_end,
	},

	/* The HP lt4132 (03f0:a31d) is a rebranded Huawei ME906s-158,
	 * therefore it too requires the above "NDP to end" quirk.
	 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x03f0, 0xa31d, USB_CLASS_COMM, USB_CDC_SUBCLASS_MBIM, USB_CDC_PROTO_NONE),
	  .driver_info = (unsigned long)&cdc_mbim_info_ndp_to_end,
	},

	/* Telit LE922A6 in MBIM composition */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x1bc7, 0x1041, USB_CLASS_COMM, USB_CDC_SUBCLASS_MBIM, USB_CDC_PROTO_NONE),
	  .driver_info = (unsigned long)&cdc_mbim_info_avoid_altsetting_toggle,
	},

	/* Telit LN920 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x1bc7, 0x1061, USB_CLASS_COMM, USB_CDC_SUBCLASS_MBIM, USB_CDC_PROTO_NONE),
	  .driver_info = (unsigned long)&cdc_mbim_info_avoid_altsetting_toggle,
	},

	/* default entry */
	{ USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_MBIM, USB_CDC_PROTO_NONE),
	  .driver_info = (unsigned long)&cdc_mbim_info_zlp,
	},
	{
	},
};
MODULE_DEVICE_TABLE(usb, mbim_devs);

static struct usb_driver cdc_mbim_driver = {
	.name = "cdc_mbim",
	.id_table = mbim_devs,
	.probe = usbnet_probe,
	.disconnect = usbnet_disconnect,
	.suspend = cdc_mbim_suspend,
	.resume = cdc_mbim_resume,
	.reset_resume =	cdc_mbim_resume,
	.supports_autosuspend = 1,
	.disable_hub_initiated_lpm = 1,
};
module_usb_driver(cdc_mbim_driver);

MODULE_AUTHOR("Greg Suarez <gsuarez@smithmicro.com>");
MODULE_AUTHOR("Bjørn Mork <bjorn@mork.no>");
MODULE_DESCRIPTION("USB CDC MBIM host driver");
MODULE_LICENSE("GPL");
