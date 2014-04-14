/*
 * Copyright (c) 2012  Smith Micro Software, Inc.
 * Copyright (c) 2012  Bjørn Mork <bjorn@mork.no>
 *
 * This driver is based on and reuse most of cdc_ncm, which is
 * Copyright (C) ST-Ericsson 2010-2012
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
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

/* driver specific data - must match cdc_ncm usage */
struct cdc_mbim_state {
	struct cdc_ncm_ctx *ctx;
	atomic_t pmcount;
	struct usb_driver *subdriver;
	struct usb_interface *control;
	struct usb_interface *data;
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
		if (rv < 0)
			goto err;
		dev->intf->needs_remote_wakeup = on;
		usb_autopm_put_interface(dev->intf);
	}
err:
	return rv;
}

static int cdc_mbim_wdm_manage_power(struct usb_interface *intf, int status)
{
	struct usbnet *dev = usb_get_intfdata(intf);

	/* can be called while disconnecting */
	if (!dev)
		return 0;

	return cdc_mbim_manage_power(dev, status);
}


static int cdc_mbim_bind(struct usbnet *dev, struct usb_interface *intf)
{
	struct cdc_ncm_ctx *ctx;
	struct usb_driver *subdriver = ERR_PTR(-ENODEV);
	int ret = -ENODEV;
	u8 data_altsetting = cdc_ncm_select_altsetting(dev, intf);
	struct cdc_mbim_state *info = (void *)&dev->data;

	/* Probably NCM, defer for cdc_ncm_bind */
	if (!cdc_ncm_comm_intf_is_mbim(intf->cur_altsetting))
		goto err;

	ret = cdc_ncm_bind_common(dev, intf, data_altsetting);
	if (ret)
		goto err;

	ctx = info->ctx;

	/* The MBIM descriptor and the status endpoint are required */
	if (ctx->mbim_desc && dev->status)
		subdriver = usb_cdc_wdm_register(ctx->control,
						 &dev->status->desc,
						 le16_to_cpu(ctx->mbim_desc->wMaxControlMessage),
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
	dev->net->features |= NETIF_F_HW_VLAN_CTAG_TX;
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


static struct sk_buff *cdc_mbim_tx_fixup(struct usbnet *dev, struct sk_buff *skb, gfp_t flags)
{
	struct sk_buff *skb_out;
	struct cdc_mbim_state *info = (void *)&dev->data;
	struct cdc_ncm_ctx *ctx = info->ctx;
	__le32 sign = cpu_to_le32(USB_CDC_MBIM_NDP16_IPS_SIGN);
	u16 tci = 0;
	u8 *c;

	if (!ctx)
		goto error;

	if (skb) {
		if (skb->len <= ETH_HLEN)
			goto error;

		/* mapping VLANs to MBIM sessions:
		 *   no tag     => IPS session <0>
		 *   1 - 255    => IPS session <vlanid>
		 *   256 - 511  => DSS session <vlanid - 256>
		 *   512 - 4095 => unsupported, drop
		 */
		vlan_get_tag(skb, &tci);

		switch (tci & 0x0f00) {
		case 0x0000: /* VLAN ID 0 - 255 */
			/* verify that datagram is IPv4 or IPv6 */
			skb_reset_mac_header(skb);
			switch (eth_hdr(skb)->h_proto) {
			case htons(ETH_P_IP):
			case htons(ETH_P_IPV6):
				break;
			default:
				goto error;
			}
			c = (u8 *)&sign;
			c[3] = tci;
			break;
		case 0x0100: /* VLAN ID 256 - 511 */
			sign = cpu_to_le32(USB_CDC_MBIM_NDP16_DSS_SIGN);
			c = (u8 *)&sign;
			c[3] = tci;
			break;
		default:
			netif_err(dev, tx_err, dev->net,
				  "unsupported tci=0x%04x\n", tci);
			goto error;
		}
		skb_pull(skb, ETH_HLEN);
	}

	spin_lock_bh(&ctx->mtx);
	skb_out = cdc_ncm_fill_tx_frame(ctx, skb, sign);
	spin_unlock_bh(&ctx->mtx);
	return skb_out;

error:
	if (skb)
		dev_kfree_skb_any(skb);

	return NULL;
}

static struct sk_buff *cdc_mbim_process_dgram(struct usbnet *dev, u8 *buf, size_t len, u16 tci)
{
	__be16 proto = htons(ETH_P_802_3);
	struct sk_buff *skb = NULL;

	if (tci < 256) { /* IPS session? */
		if (len < sizeof(struct iphdr))
			goto err;

		switch (*buf & 0xf0) {
		case 0x40:
			proto = htons(ETH_P_IP);
			break;
		case 0x60:
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
	memset(eth_hdr(skb)->h_source, 0, ETH_ALEN);
	memcpy(eth_hdr(skb)->h_dest, dev->net->dev_addr, ETH_ALEN);

	/* add datagram */
	memcpy(skb_put(skb, len), buf, len);

	/* map MBIM session to VLAN */
	if (tci)
		vlan_put_tag(skb, htons(ETH_P_8021Q), tci);
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
		}
	}
err_ndp:
	/* are there more NDPs to process? */
	ndpoffset = le16_to_cpu(ndp16->wNextNdpIndex);
	if (ndpoffset && loopcount--)
		goto next_ndp;

	return 1;
error:
	return 0;
}

static int cdc_mbim_suspend(struct usb_interface *intf, pm_message_t message)
{
	int ret = 0;
	struct usbnet *dev = usb_get_intfdata(intf);
	struct cdc_mbim_state *info = (void *)&dev->data;
	struct cdc_ncm_ctx *ctx = info->ctx;

	if (ctx == NULL) {
		ret = -1;
		goto error;
	}

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
	if (ret < 0 && callsub && info->subdriver->suspend)
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
 * dwNtbOutMaxSize length. This driver_info is for the exceptional
 * devices requiring it anyway, allowing them to be supported without
 * forcing the performance penalty on all the sane devices.
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
	/* Sierra Wireless MC7710 need ZLPs */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x1199, 0x68a2, USB_CLASS_COMM, USB_CDC_SUBCLASS_MBIM, USB_CDC_PROTO_NONE),
	  .driver_info = (unsigned long)&cdc_mbim_info_zlp,
	},
	/* HP hs2434 Mobile Broadband Module needs ZLPs */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x3f0, 0x4b1d, USB_CLASS_COMM, USB_CDC_SUBCLASS_MBIM, USB_CDC_PROTO_NONE),
	  .driver_info = (unsigned long)&cdc_mbim_info_zlp,
	},
	{ USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_MBIM, USB_CDC_PROTO_NONE),
	  .driver_info = (unsigned long)&cdc_mbim_info,
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
