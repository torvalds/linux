/*
 * cdc_ncm.c
 *
 * Copyright (C) ST-Ericsson 2010-2011
 * Contact: Alexey Orishko <alexey.orishko@stericsson.com>
 * Original author: Hans Petter Selasky <hans.petter.selasky@stericsson.com>
 *
 * USB Host Driver for Network Control Model (NCM)
 * http://www.usb.org/developers/devclass_docs/NCM10.zip
 *
 * The NCM encoding, decoding and initialization logic
 * derives from FreeBSD 8.x. if_cdce.c and if_cdcereg.h
 *
 * This software is available to you under a choice of one of two
 * licenses. You may choose this file to be licensed under the terms
 * of the GNU General Public License (GPL) Version 2 or the 2-clause
 * BSD license listed below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/ctype.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mii.h>
#include <linux/crc32.h>
#include <linux/usb.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/usb/usbnet.h>
#include <linux/usb/cdc.h>

#define	DRIVER_VERSION				"04-Aug-2011"

/* CDC NCM subclass 3.2.1 */
#define USB_CDC_NCM_NDP16_LENGTH_MIN		0x10

/* Maximum NTB length */
#define	CDC_NCM_NTB_MAX_SIZE_TX			16384	/* bytes */
#define	CDC_NCM_NTB_MAX_SIZE_RX			16384	/* bytes */

/* Minimum value for MaxDatagramSize, ch. 6.2.9 */
#define	CDC_NCM_MIN_DATAGRAM_SIZE		1514	/* bytes */

#define	CDC_NCM_MIN_TX_PKT			512	/* bytes */

/* Default value for MaxDatagramSize */
#define	CDC_NCM_MAX_DATAGRAM_SIZE		2048	/* bytes */

/*
 * Maximum amount of datagrams in NCM Datagram Pointer Table, not counting
 * the last NULL entry. Any additional datagrams in NTB would be discarded.
 */
#define	CDC_NCM_DPT_DATAGRAMS_MAX		32

/* Maximum amount of IN datagrams in NTB */
#define	CDC_NCM_DPT_DATAGRAMS_IN_MAX		0 /* unlimited */

/* Restart the timer, if amount of datagrams is less than given value */
#define	CDC_NCM_RESTART_TIMER_DATAGRAM_CNT	3

/* The following macro defines the minimum header space */
#define	CDC_NCM_MIN_HDR_SIZE \
	(sizeof(struct usb_cdc_ncm_nth16) + sizeof(struct usb_cdc_ncm_ndp16) + \
	(CDC_NCM_DPT_DATAGRAMS_MAX + 1) * sizeof(struct usb_cdc_ncm_dpe16))

struct cdc_ncm_data {
	struct usb_cdc_ncm_nth16 nth16;
	struct usb_cdc_ncm_ndp16 ndp16;
	struct usb_cdc_ncm_dpe16 dpe16[CDC_NCM_DPT_DATAGRAMS_MAX + 1];
};

struct cdc_ncm_ctx {
	struct cdc_ncm_data rx_ncm;
	struct cdc_ncm_data tx_ncm;
	struct usb_cdc_ncm_ntb_parameters ncm_parm;
	struct timer_list tx_timer;

	const struct usb_cdc_ncm_desc *func_desc;
	const struct usb_cdc_header_desc *header_desc;
	const struct usb_cdc_union_desc *union_desc;
	const struct usb_cdc_ether_desc *ether_desc;

	struct net_device *netdev;
	struct usb_device *udev;
	struct usb_host_endpoint *in_ep;
	struct usb_host_endpoint *out_ep;
	struct usb_host_endpoint *status_ep;
	struct usb_interface *intf;
	struct usb_interface *control;
	struct usb_interface *data;

	struct sk_buff *tx_curr_skb;
	struct sk_buff *tx_rem_skb;

	spinlock_t mtx;

	u32 tx_timer_pending;
	u32 tx_curr_offset;
	u32 tx_curr_last_offset;
	u32 tx_curr_frame_num;
	u32 rx_speed;
	u32 tx_speed;
	u32 rx_max;
	u32 tx_max;
	u32 max_datagram_size;
	u16 tx_max_datagrams;
	u16 tx_remainder;
	u16 tx_modulus;
	u16 tx_ndp_modulus;
	u16 tx_seq;
	u16 connected;
};

static void cdc_ncm_tx_timeout(unsigned long arg);
static const struct driver_info cdc_ncm_info;
static struct usb_driver cdc_ncm_driver;
static struct ethtool_ops cdc_ncm_ethtool_ops;

static const struct usb_device_id cdc_devs[] = {
	{ USB_INTERFACE_INFO(USB_CLASS_COMM,
		USB_CDC_SUBCLASS_NCM, USB_CDC_PROTO_NONE),
		.driver_info = (unsigned long)&cdc_ncm_info,
	},
	{
	},
};

MODULE_DEVICE_TABLE(usb, cdc_devs);

static void
cdc_ncm_get_drvinfo(struct net_device *net, struct ethtool_drvinfo *info)
{
	struct usbnet *dev = netdev_priv(net);

	strncpy(info->driver, dev->driver_name, sizeof(info->driver));
	strncpy(info->version, DRIVER_VERSION, sizeof(info->version));
	strncpy(info->fw_version, dev->driver_info->description,
		sizeof(info->fw_version));
	usb_make_path(dev->udev, info->bus_info, sizeof(info->bus_info));
}

static u8 cdc_ncm_setup(struct cdc_ncm_ctx *ctx)
{
	u32 val;
	u8 flags;
	u8 iface_no;
	int err;
	u16 ntb_fmt_supported;

	iface_no = ctx->control->cur_altsetting->desc.bInterfaceNumber;

	err = usb_control_msg(ctx->udev,
				usb_rcvctrlpipe(ctx->udev, 0),
				USB_CDC_GET_NTB_PARAMETERS,
				USB_TYPE_CLASS | USB_DIR_IN
				 | USB_RECIP_INTERFACE,
				0, iface_no, &ctx->ncm_parm,
				sizeof(ctx->ncm_parm), 10000);
	if (err < 0) {
		pr_debug("failed GET_NTB_PARAMETERS\n");
		return 1;
	}

	/* read correct set of parameters according to device mode */
	ctx->rx_max = le32_to_cpu(ctx->ncm_parm.dwNtbInMaxSize);
	ctx->tx_max = le32_to_cpu(ctx->ncm_parm.dwNtbOutMaxSize);
	ctx->tx_remainder = le16_to_cpu(ctx->ncm_parm.wNdpOutPayloadRemainder);
	ctx->tx_modulus = le16_to_cpu(ctx->ncm_parm.wNdpOutDivisor);
	ctx->tx_ndp_modulus = le16_to_cpu(ctx->ncm_parm.wNdpOutAlignment);
	/* devices prior to NCM Errata shall set this field to zero */
	ctx->tx_max_datagrams = le16_to_cpu(ctx->ncm_parm.wNtbOutMaxDatagrams);
	ntb_fmt_supported = le16_to_cpu(ctx->ncm_parm.bmNtbFormatsSupported);

	if (ctx->func_desc != NULL)
		flags = ctx->func_desc->bmNetworkCapabilities;
	else
		flags = 0;

	pr_debug("dwNtbInMaxSize=%u dwNtbOutMaxSize=%u "
		 "wNdpOutPayloadRemainder=%u wNdpOutDivisor=%u "
		 "wNdpOutAlignment=%u wNtbOutMaxDatagrams=%u flags=0x%x\n",
		 ctx->rx_max, ctx->tx_max, ctx->tx_remainder, ctx->tx_modulus,
		 ctx->tx_ndp_modulus, ctx->tx_max_datagrams, flags);

	/* max count of tx datagrams */
	if ((ctx->tx_max_datagrams == 0) ||
			(ctx->tx_max_datagrams > CDC_NCM_DPT_DATAGRAMS_MAX))
		ctx->tx_max_datagrams = CDC_NCM_DPT_DATAGRAMS_MAX;

	/* verify maximum size of received NTB in bytes */
	if (ctx->rx_max < USB_CDC_NCM_NTB_MIN_IN_SIZE) {
		pr_debug("Using min receive length=%d\n",
						USB_CDC_NCM_NTB_MIN_IN_SIZE);
		ctx->rx_max = USB_CDC_NCM_NTB_MIN_IN_SIZE;
	}

	if (ctx->rx_max > CDC_NCM_NTB_MAX_SIZE_RX) {
		pr_debug("Using default maximum receive length=%d\n",
						CDC_NCM_NTB_MAX_SIZE_RX);
		ctx->rx_max = CDC_NCM_NTB_MAX_SIZE_RX;
	}

	/* inform device about NTB input size changes */
	if (ctx->rx_max != le32_to_cpu(ctx->ncm_parm.dwNtbInMaxSize)) {

		if (flags & USB_CDC_NCM_NCAP_NTB_INPUT_SIZE) {
			struct usb_cdc_ncm_ndp_input_size ndp_in_sz;
			err = usb_control_msg(ctx->udev,
					usb_sndctrlpipe(ctx->udev, 0),
					USB_CDC_SET_NTB_INPUT_SIZE,
					USB_TYPE_CLASS | USB_DIR_OUT
					 | USB_RECIP_INTERFACE,
					0, iface_no, &ndp_in_sz, 8, 1000);
		} else {
			__le32 dwNtbInMaxSize = cpu_to_le32(ctx->rx_max);
			err = usb_control_msg(ctx->udev,
					usb_sndctrlpipe(ctx->udev, 0),
					USB_CDC_SET_NTB_INPUT_SIZE,
					USB_TYPE_CLASS | USB_DIR_OUT
					 | USB_RECIP_INTERFACE,
					0, iface_no, &dwNtbInMaxSize, 4, 1000);
		}

		if (err < 0)
			pr_debug("Setting NTB Input Size failed\n");
	}

	/* verify maximum size of transmitted NTB in bytes */
	if ((ctx->tx_max <
	    (CDC_NCM_MIN_HDR_SIZE + CDC_NCM_MIN_DATAGRAM_SIZE)) ||
	    (ctx->tx_max > CDC_NCM_NTB_MAX_SIZE_TX)) {
		pr_debug("Using default maximum transmit length=%d\n",
						CDC_NCM_NTB_MAX_SIZE_TX);
		ctx->tx_max = CDC_NCM_NTB_MAX_SIZE_TX;
	}

	/*
	 * verify that the structure alignment is:
	 * - power of two
	 * - not greater than the maximum transmit length
	 * - not less than four bytes
	 */
	val = ctx->tx_ndp_modulus;

	if ((val < USB_CDC_NCM_NDP_ALIGN_MIN_SIZE) ||
	    (val != ((-val) & val)) || (val >= ctx->tx_max)) {
		pr_debug("Using default alignment: 4 bytes\n");
		ctx->tx_ndp_modulus = USB_CDC_NCM_NDP_ALIGN_MIN_SIZE;
	}

	/*
	 * verify that the payload alignment is:
	 * - power of two
	 * - not greater than the maximum transmit length
	 * - not less than four bytes
	 */
	val = ctx->tx_modulus;

	if ((val < USB_CDC_NCM_NDP_ALIGN_MIN_SIZE) ||
	    (val != ((-val) & val)) || (val >= ctx->tx_max)) {
		pr_debug("Using default transmit modulus: 4 bytes\n");
		ctx->tx_modulus = USB_CDC_NCM_NDP_ALIGN_MIN_SIZE;
	}

	/* verify the payload remainder */
	if (ctx->tx_remainder >= ctx->tx_modulus) {
		pr_debug("Using default transmit remainder: 0 bytes\n");
		ctx->tx_remainder = 0;
	}

	/* adjust TX-remainder according to NCM specification. */
	ctx->tx_remainder = ((ctx->tx_remainder - ETH_HLEN) &
						(ctx->tx_modulus - 1));

	/* additional configuration */

	/* set CRC Mode */
	if (flags & USB_CDC_NCM_NCAP_CRC_MODE) {
		err = usb_control_msg(ctx->udev, usb_sndctrlpipe(ctx->udev, 0),
				USB_CDC_SET_CRC_MODE,
				USB_TYPE_CLASS | USB_DIR_OUT
				 | USB_RECIP_INTERFACE,
				USB_CDC_NCM_CRC_NOT_APPENDED,
				iface_no, NULL, 0, 1000);
		if (err < 0)
			pr_debug("Setting CRC mode off failed\n");
	}

	/* set NTB format, if both formats are supported */
	if (ntb_fmt_supported & USB_CDC_NCM_NTH32_SIGN) {
		err = usb_control_msg(ctx->udev, usb_sndctrlpipe(ctx->udev, 0),
				USB_CDC_SET_NTB_FORMAT, USB_TYPE_CLASS
				 | USB_DIR_OUT | USB_RECIP_INTERFACE,
				USB_CDC_NCM_NTB16_FORMAT,
				iface_no, NULL, 0, 1000);
		if (err < 0)
			pr_debug("Setting NTB format to 16-bit failed\n");
	}

	ctx->max_datagram_size = CDC_NCM_MIN_DATAGRAM_SIZE;

	/* set Max Datagram Size (MTU) */
	if (flags & USB_CDC_NCM_NCAP_MAX_DATAGRAM_SIZE) {
		__le16 max_datagram_size;
		u16 eth_max_sz = le16_to_cpu(ctx->ether_desc->wMaxSegmentSize);
		err = usb_control_msg(ctx->udev, usb_rcvctrlpipe(ctx->udev, 0),
				USB_CDC_GET_MAX_DATAGRAM_SIZE,
				USB_TYPE_CLASS | USB_DIR_IN
				 | USB_RECIP_INTERFACE,
				0, iface_no, &max_datagram_size,
				2, 1000);
		if (err < 0) {
			pr_debug("GET_MAX_DATAGRAM_SIZE failed, use size=%u\n",
						CDC_NCM_MIN_DATAGRAM_SIZE);
		} else {
			ctx->max_datagram_size = le16_to_cpu(max_datagram_size);
			/* Check Eth descriptor value */
			if (eth_max_sz < CDC_NCM_MAX_DATAGRAM_SIZE) {
				if (ctx->max_datagram_size > eth_max_sz)
					ctx->max_datagram_size = eth_max_sz;
			} else {
				if (ctx->max_datagram_size >
						CDC_NCM_MAX_DATAGRAM_SIZE)
					ctx->max_datagram_size =
						CDC_NCM_MAX_DATAGRAM_SIZE;
			}

			if (ctx->max_datagram_size < CDC_NCM_MIN_DATAGRAM_SIZE)
				ctx->max_datagram_size =
					CDC_NCM_MIN_DATAGRAM_SIZE;

			/* if value changed, update device */
			err = usb_control_msg(ctx->udev,
						usb_sndctrlpipe(ctx->udev, 0),
						USB_CDC_SET_MAX_DATAGRAM_SIZE,
						USB_TYPE_CLASS | USB_DIR_OUT
						 | USB_RECIP_INTERFACE,
						0,
						iface_no, &max_datagram_size,
						2, 1000);
			if (err < 0)
				pr_debug("SET_MAX_DATAGRAM_SIZE failed\n");
		}

	}

	if (ctx->netdev->mtu != (ctx->max_datagram_size - ETH_HLEN))
		ctx->netdev->mtu = ctx->max_datagram_size - ETH_HLEN;

	return 0;
}

static void
cdc_ncm_find_endpoints(struct cdc_ncm_ctx *ctx, struct usb_interface *intf)
{
	struct usb_host_endpoint *e;
	u8 ep;

	for (ep = 0; ep < intf->cur_altsetting->desc.bNumEndpoints; ep++) {

		e = intf->cur_altsetting->endpoint + ep;
		switch (e->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
		case USB_ENDPOINT_XFER_INT:
			if (usb_endpoint_dir_in(&e->desc)) {
				if (ctx->status_ep == NULL)
					ctx->status_ep = e;
			}
			break;

		case USB_ENDPOINT_XFER_BULK:
			if (usb_endpoint_dir_in(&e->desc)) {
				if (ctx->in_ep == NULL)
					ctx->in_ep = e;
			} else {
				if (ctx->out_ep == NULL)
					ctx->out_ep = e;
			}
			break;

		default:
			break;
		}
	}
}

static void cdc_ncm_free(struct cdc_ncm_ctx *ctx)
{
	if (ctx == NULL)
		return;

	del_timer_sync(&ctx->tx_timer);

	if (ctx->tx_rem_skb != NULL) {
		dev_kfree_skb_any(ctx->tx_rem_skb);
		ctx->tx_rem_skb = NULL;
	}

	if (ctx->tx_curr_skb != NULL) {
		dev_kfree_skb_any(ctx->tx_curr_skb);
		ctx->tx_curr_skb = NULL;
	}

	kfree(ctx);
}

static int cdc_ncm_bind(struct usbnet *dev, struct usb_interface *intf)
{
	struct cdc_ncm_ctx *ctx;
	struct usb_driver *driver;
	u8 *buf;
	int len;
	int temp;
	u8 iface_no;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL)
		return -ENODEV;

	memset(ctx, 0, sizeof(*ctx));

	init_timer(&ctx->tx_timer);
	spin_lock_init(&ctx->mtx);
	ctx->netdev = dev->net;

	/* store ctx pointer in device data field */
	dev->data[0] = (unsigned long)ctx;

	/* get some pointers */
	driver = driver_of(intf);
	buf = intf->cur_altsetting->extra;
	len = intf->cur_altsetting->extralen;

	ctx->udev = dev->udev;
	ctx->intf = intf;

	/* parse through descriptors associated with control interface */
	while ((len > 0) && (buf[0] > 2) && (buf[0] <= len)) {

		if (buf[1] != USB_DT_CS_INTERFACE)
			goto advance;

		switch (buf[2]) {
		case USB_CDC_UNION_TYPE:
			if (buf[0] < sizeof(*(ctx->union_desc)))
				break;

			ctx->union_desc =
					(const struct usb_cdc_union_desc *)buf;

			ctx->control = usb_ifnum_to_if(dev->udev,
					ctx->union_desc->bMasterInterface0);
			ctx->data = usb_ifnum_to_if(dev->udev,
					ctx->union_desc->bSlaveInterface0);
			break;

		case USB_CDC_ETHERNET_TYPE:
			if (buf[0] < sizeof(*(ctx->ether_desc)))
				break;

			ctx->ether_desc =
					(const struct usb_cdc_ether_desc *)buf;
			dev->hard_mtu =
				le16_to_cpu(ctx->ether_desc->wMaxSegmentSize);

			if (dev->hard_mtu < CDC_NCM_MIN_DATAGRAM_SIZE)
				dev->hard_mtu =	CDC_NCM_MIN_DATAGRAM_SIZE;
			else if (dev->hard_mtu > CDC_NCM_MAX_DATAGRAM_SIZE)
				dev->hard_mtu =	CDC_NCM_MAX_DATAGRAM_SIZE;
			break;

		case USB_CDC_NCM_TYPE:
			if (buf[0] < sizeof(*(ctx->func_desc)))
				break;

			ctx->func_desc = (const struct usb_cdc_ncm_desc *)buf;
			break;

		default:
			break;
		}
advance:
		/* advance to next descriptor */
		temp = buf[0];
		buf += temp;
		len -= temp;
	}

	/* check if we got everything */
	if ((ctx->control == NULL) || (ctx->data == NULL) ||
	    (ctx->ether_desc == NULL) || (ctx->control != intf))
		goto error;

	/* claim interfaces, if any */
	temp = usb_driver_claim_interface(driver, ctx->data, dev);
	if (temp)
		goto error;

	iface_no = ctx->data->cur_altsetting->desc.bInterfaceNumber;

	/* reset data interface */
	temp = usb_set_interface(dev->udev, iface_no, 0);
	if (temp)
		goto error2;

	/* initialize data interface */
	if (cdc_ncm_setup(ctx))
		goto error2;

	/* configure data interface */
	temp = usb_set_interface(dev->udev, iface_no, 1);
	if (temp)
		goto error2;

	cdc_ncm_find_endpoints(ctx, ctx->data);
	cdc_ncm_find_endpoints(ctx, ctx->control);

	if ((ctx->in_ep == NULL) || (ctx->out_ep == NULL) ||
	    (ctx->status_ep == NULL))
		goto error2;

	dev->net->ethtool_ops = &cdc_ncm_ethtool_ops;

	usb_set_intfdata(ctx->data, dev);
	usb_set_intfdata(ctx->control, dev);
	usb_set_intfdata(ctx->intf, dev);

	temp = usbnet_get_ethernet_addr(dev, ctx->ether_desc->iMACAddress);
	if (temp)
		goto error2;

	dev_info(&dev->udev->dev, "MAC-Address: "
				"0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x\n",
				dev->net->dev_addr[0], dev->net->dev_addr[1],
				dev->net->dev_addr[2], dev->net->dev_addr[3],
				dev->net->dev_addr[4], dev->net->dev_addr[5]);

	dev->in = usb_rcvbulkpipe(dev->udev,
		ctx->in_ep->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	dev->out = usb_sndbulkpipe(dev->udev,
		ctx->out_ep->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	dev->status = ctx->status_ep;
	dev->rx_urb_size = ctx->rx_max;

	/*
	 * We should get an event when network connection is "connected" or
	 * "disconnected". Set network connection in "disconnected" state
	 * (carrier is OFF) during attach, so the IP network stack does not
	 * start IPv6 negotiation and more.
	 */
	netif_carrier_off(dev->net);
	ctx->tx_speed = ctx->rx_speed = 0;
	return 0;

error2:
	usb_set_intfdata(ctx->control, NULL);
	usb_set_intfdata(ctx->data, NULL);
	usb_driver_release_interface(driver, ctx->data);
error:
	cdc_ncm_free((struct cdc_ncm_ctx *)dev->data[0]);
	dev->data[0] = 0;
	dev_info(&dev->udev->dev, "bind() failure\n");
	return -ENODEV;
}

static void cdc_ncm_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	struct cdc_ncm_ctx *ctx = (struct cdc_ncm_ctx *)dev->data[0];
	struct usb_driver *driver = driver_of(intf);

	if (ctx == NULL)
		return;		/* no setup */

	/* disconnect master --> disconnect slave */
	if (intf == ctx->control && ctx->data) {
		usb_set_intfdata(ctx->data, NULL);
		usb_driver_release_interface(driver, ctx->data);
		ctx->data = NULL;

	} else if (intf == ctx->data && ctx->control) {
		usb_set_intfdata(ctx->control, NULL);
		usb_driver_release_interface(driver, ctx->control);
		ctx->control = NULL;
	}

	usb_set_intfdata(ctx->intf, NULL);
	cdc_ncm_free(ctx);
}

static void cdc_ncm_zero_fill(u8 *ptr, u32 first, u32 end, u32 max)
{
	if (first >= max)
		return;
	if (first >= end)
		return;
	if (end > max)
		end = max;
	memset(ptr + first, 0, end - first);
}

static struct sk_buff *
cdc_ncm_fill_tx_frame(struct cdc_ncm_ctx *ctx, struct sk_buff *skb)
{
	struct sk_buff *skb_out;
	u32 rem;
	u32 offset;
	u32 last_offset;
	u16 n = 0, index;
	u8 ready2send = 0;

	/* if there is a remaining skb, it gets priority */
	if (skb != NULL)
		swap(skb, ctx->tx_rem_skb);
	else
		ready2send = 1;

	/*
	 * +----------------+
	 * | skb_out        |
	 * +----------------+
	 *           ^ offset
	 *        ^ last_offset
	 */

	/* check if we are resuming an OUT skb */
	if (ctx->tx_curr_skb != NULL) {
		/* pop variables */
		skb_out = ctx->tx_curr_skb;
		offset = ctx->tx_curr_offset;
		last_offset = ctx->tx_curr_last_offset;
		n = ctx->tx_curr_frame_num;

	} else {
		/* reset variables */
		skb_out = alloc_skb((ctx->tx_max + 1), GFP_ATOMIC);
		if (skb_out == NULL) {
			if (skb != NULL) {
				dev_kfree_skb_any(skb);
				ctx->netdev->stats.tx_dropped++;
			}
			goto exit_no_skb;
		}

		/* make room for NTH and NDP */
		offset = ALIGN(sizeof(struct usb_cdc_ncm_nth16),
					ctx->tx_ndp_modulus) +
					sizeof(struct usb_cdc_ncm_ndp16) +
					(ctx->tx_max_datagrams + 1) *
					sizeof(struct usb_cdc_ncm_dpe16);

		/* store last valid offset before alignment */
		last_offset = offset;
		/* align first Datagram offset correctly */
		offset = ALIGN(offset, ctx->tx_modulus) + ctx->tx_remainder;
		/* zero buffer till the first IP datagram */
		cdc_ncm_zero_fill(skb_out->data, 0, offset, offset);
		n = 0;
		ctx->tx_curr_frame_num = 0;
	}

	for (; n < ctx->tx_max_datagrams; n++) {
		/* check if end of transmit buffer is reached */
		if (offset >= ctx->tx_max) {
			ready2send = 1;
			break;
		}
		/* compute maximum buffer size */
		rem = ctx->tx_max - offset;

		if (skb == NULL) {
			skb = ctx->tx_rem_skb;
			ctx->tx_rem_skb = NULL;

			/* check for end of skb */
			if (skb == NULL)
				break;
		}

		if (skb->len > rem) {
			if (n == 0) {
				/* won't fit, MTU problem? */
				dev_kfree_skb_any(skb);
				skb = NULL;
				ctx->netdev->stats.tx_dropped++;
			} else {
				/* no room for skb - store for later */
				if (ctx->tx_rem_skb != NULL) {
					dev_kfree_skb_any(ctx->tx_rem_skb);
					ctx->netdev->stats.tx_dropped++;
				}
				ctx->tx_rem_skb = skb;
				skb = NULL;
				ready2send = 1;
			}
			break;
		}

		memcpy(((u8 *)skb_out->data) + offset, skb->data, skb->len);

		ctx->tx_ncm.dpe16[n].wDatagramLength = cpu_to_le16(skb->len);
		ctx->tx_ncm.dpe16[n].wDatagramIndex = cpu_to_le16(offset);

		/* update offset */
		offset += skb->len;

		/* store last valid offset before alignment */
		last_offset = offset;

		/* align offset correctly */
		offset = ALIGN(offset, ctx->tx_modulus) + ctx->tx_remainder;

		/* zero padding */
		cdc_ncm_zero_fill(skb_out->data, last_offset, offset,
								ctx->tx_max);
		dev_kfree_skb_any(skb);
		skb = NULL;
	}

	/* free up any dangling skb */
	if (skb != NULL) {
		dev_kfree_skb_any(skb);
		skb = NULL;
		ctx->netdev->stats.tx_dropped++;
	}

	ctx->tx_curr_frame_num = n;

	if (n == 0) {
		/* wait for more frames */
		/* push variables */
		ctx->tx_curr_skb = skb_out;
		ctx->tx_curr_offset = offset;
		ctx->tx_curr_last_offset = last_offset;
		goto exit_no_skb;

	} else if ((n < ctx->tx_max_datagrams) && (ready2send == 0)) {
		/* wait for more frames */
		/* push variables */
		ctx->tx_curr_skb = skb_out;
		ctx->tx_curr_offset = offset;
		ctx->tx_curr_last_offset = last_offset;
		/* set the pending count */
		if (n < CDC_NCM_RESTART_TIMER_DATAGRAM_CNT)
			ctx->tx_timer_pending = 2;
		goto exit_no_skb;

	} else {
		/* frame goes out */
		/* variables will be reset at next call */
	}

	/* check for overflow */
	if (last_offset > ctx->tx_max)
		last_offset = ctx->tx_max;

	/* revert offset */
	offset = last_offset;

	/*
	 * If collected data size is less or equal CDC_NCM_MIN_TX_PKT bytes,
	 * we send buffers as it is. If we get more data, it would be more
	 * efficient for USB HS mobile device with DMA engine to receive a full
	 * size NTB, than canceling DMA transfer and receiving a short packet.
	 */
	if (offset > CDC_NCM_MIN_TX_PKT)
		offset = ctx->tx_max;

	/* final zero padding */
	cdc_ncm_zero_fill(skb_out->data, last_offset, offset, ctx->tx_max);

	/* store last offset */
	last_offset = offset;

	if (((last_offset < ctx->tx_max) && ((last_offset %
			le16_to_cpu(ctx->out_ep->desc.wMaxPacketSize)) == 0)) ||
	    (((last_offset == ctx->tx_max) && ((ctx->tx_max %
		le16_to_cpu(ctx->out_ep->desc.wMaxPacketSize)) == 0)) &&
		(ctx->tx_max < le32_to_cpu(ctx->ncm_parm.dwNtbOutMaxSize)))) {
		/* force short packet */
		*(((u8 *)skb_out->data) + last_offset) = 0;
		last_offset++;
	}

	/* zero the rest of the DPEs plus the last NULL entry */
	for (; n <= CDC_NCM_DPT_DATAGRAMS_MAX; n++) {
		ctx->tx_ncm.dpe16[n].wDatagramLength = 0;
		ctx->tx_ncm.dpe16[n].wDatagramIndex = 0;
	}

	/* fill out 16-bit NTB header */
	ctx->tx_ncm.nth16.dwSignature = cpu_to_le32(USB_CDC_NCM_NTH16_SIGN);
	ctx->tx_ncm.nth16.wHeaderLength =
					cpu_to_le16(sizeof(ctx->tx_ncm.nth16));
	ctx->tx_ncm.nth16.wSequence = cpu_to_le16(ctx->tx_seq);
	ctx->tx_ncm.nth16.wBlockLength = cpu_to_le16(last_offset);
	index = ALIGN(sizeof(struct usb_cdc_ncm_nth16), ctx->tx_ndp_modulus);
	ctx->tx_ncm.nth16.wNdpIndex = cpu_to_le16(index);

	memcpy(skb_out->data, &(ctx->tx_ncm.nth16), sizeof(ctx->tx_ncm.nth16));
	ctx->tx_seq++;

	/* fill out 16-bit NDP table */
	ctx->tx_ncm.ndp16.dwSignature =
				cpu_to_le32(USB_CDC_NCM_NDP16_NOCRC_SIGN);
	rem = sizeof(ctx->tx_ncm.ndp16) + ((ctx->tx_curr_frame_num + 1) *
					sizeof(struct usb_cdc_ncm_dpe16));
	ctx->tx_ncm.ndp16.wLength = cpu_to_le16(rem);
	ctx->tx_ncm.ndp16.wNextNdpIndex = 0; /* reserved */

	memcpy(((u8 *)skb_out->data) + index,
						&(ctx->tx_ncm.ndp16),
						sizeof(ctx->tx_ncm.ndp16));

	memcpy(((u8 *)skb_out->data) + index + sizeof(ctx->tx_ncm.ndp16),
					&(ctx->tx_ncm.dpe16),
					(ctx->tx_curr_frame_num + 1) *
					sizeof(struct usb_cdc_ncm_dpe16));

	/* set frame length */
	skb_put(skb_out, last_offset);

	/* return skb */
	ctx->tx_curr_skb = NULL;
	return skb_out;

exit_no_skb:
	return NULL;
}

static void cdc_ncm_tx_timeout_start(struct cdc_ncm_ctx *ctx)
{
	/* start timer, if not already started */
	if (timer_pending(&ctx->tx_timer) == 0) {
		ctx->tx_timer.function = &cdc_ncm_tx_timeout;
		ctx->tx_timer.data = (unsigned long)ctx;
		ctx->tx_timer.expires = jiffies + ((HZ + 999) / 1000);
		add_timer(&ctx->tx_timer);
	}
}

static void cdc_ncm_tx_timeout(unsigned long arg)
{
	struct cdc_ncm_ctx *ctx = (struct cdc_ncm_ctx *)arg;
	u8 restart;

	spin_lock(&ctx->mtx);
	if (ctx->tx_timer_pending != 0) {
		ctx->tx_timer_pending--;
		restart = 1;
	} else {
		restart = 0;
	}

	spin_unlock(&ctx->mtx);

	if (restart) {
		spin_lock(&ctx->mtx);
		cdc_ncm_tx_timeout_start(ctx);
		spin_unlock(&ctx->mtx);
	} else if (ctx->netdev != NULL) {
		usbnet_start_xmit(NULL, ctx->netdev);
	}
}

static struct sk_buff *
cdc_ncm_tx_fixup(struct usbnet *dev, struct sk_buff *skb, gfp_t flags)
{
	struct sk_buff *skb_out;
	struct cdc_ncm_ctx *ctx = (struct cdc_ncm_ctx *)dev->data[0];
	u8 need_timer = 0;

	/*
	 * The Ethernet API we are using does not support transmitting
	 * multiple Ethernet frames in a single call. This driver will
	 * accumulate multiple Ethernet frames and send out a larger
	 * USB frame when the USB buffer is full or when a single jiffies
	 * timeout happens.
	 */
	if (ctx == NULL)
		goto error;

	spin_lock(&ctx->mtx);
	skb_out = cdc_ncm_fill_tx_frame(ctx, skb);
	if (ctx->tx_curr_skb != NULL)
		need_timer = 1;

	/* Start timer, if there is a remaining skb */
	if (need_timer)
		cdc_ncm_tx_timeout_start(ctx);

	if (skb_out)
		dev->net->stats.tx_packets += ctx->tx_curr_frame_num;

	spin_unlock(&ctx->mtx);
	return skb_out;

error:
	if (skb != NULL)
		dev_kfree_skb_any(skb);

	return NULL;
}

static int cdc_ncm_rx_fixup(struct usbnet *dev, struct sk_buff *skb_in)
{
	struct sk_buff *skb;
	struct cdc_ncm_ctx *ctx;
	int sumlen;
	int actlen;
	int temp;
	int nframes;
	int x;
	int offset;

	ctx = (struct cdc_ncm_ctx *)dev->data[0];
	if (ctx == NULL)
		goto error;

	actlen = skb_in->len;
	sumlen = CDC_NCM_NTB_MAX_SIZE_RX;

	if (actlen < (sizeof(ctx->rx_ncm.nth16) + sizeof(ctx->rx_ncm.ndp16))) {
		pr_debug("frame too short\n");
		goto error;
	}

	memcpy(&(ctx->rx_ncm.nth16), ((u8 *)skb_in->data),
						sizeof(ctx->rx_ncm.nth16));

	if (le32_to_cpu(ctx->rx_ncm.nth16.dwSignature) !=
	    USB_CDC_NCM_NTH16_SIGN) {
		pr_debug("invalid NTH16 signature <%u>\n",
			 le32_to_cpu(ctx->rx_ncm.nth16.dwSignature));
		goto error;
	}

	temp = le16_to_cpu(ctx->rx_ncm.nth16.wBlockLength);
	if (temp > sumlen) {
		pr_debug("unsupported NTB block length %u/%u\n", temp, sumlen);
		goto error;
	}

	temp = le16_to_cpu(ctx->rx_ncm.nth16.wNdpIndex);
	if ((temp + sizeof(ctx->rx_ncm.ndp16)) > actlen) {
		pr_debug("invalid DPT16 index\n");
		goto error;
	}

	memcpy(&(ctx->rx_ncm.ndp16), ((u8 *)skb_in->data) + temp,
						sizeof(ctx->rx_ncm.ndp16));

	if (le32_to_cpu(ctx->rx_ncm.ndp16.dwSignature) !=
	    USB_CDC_NCM_NDP16_NOCRC_SIGN) {
		pr_debug("invalid DPT16 signature <%u>\n",
			 le32_to_cpu(ctx->rx_ncm.ndp16.dwSignature));
		goto error;
	}

	if (le16_to_cpu(ctx->rx_ncm.ndp16.wLength) <
	    USB_CDC_NCM_NDP16_LENGTH_MIN) {
		pr_debug("invalid DPT16 length <%u>\n",
			 le32_to_cpu(ctx->rx_ncm.ndp16.dwSignature));
		goto error;
	}

	nframes = ((le16_to_cpu(ctx->rx_ncm.ndp16.wLength) -
					sizeof(struct usb_cdc_ncm_ndp16)) /
					sizeof(struct usb_cdc_ncm_dpe16));
	nframes--; /* we process NDP entries except for the last one */

	pr_debug("nframes = %u\n", nframes);

	temp += sizeof(ctx->rx_ncm.ndp16);

	if ((temp + nframes * (sizeof(struct usb_cdc_ncm_dpe16))) > actlen) {
		pr_debug("Invalid nframes = %d\n", nframes);
		goto error;
	}

	if (nframes > CDC_NCM_DPT_DATAGRAMS_MAX) {
		pr_debug("Truncating number of frames from %u to %u\n",
					nframes, CDC_NCM_DPT_DATAGRAMS_MAX);
		nframes = CDC_NCM_DPT_DATAGRAMS_MAX;
	}

	memcpy(&(ctx->rx_ncm.dpe16), ((u8 *)skb_in->data) + temp,
				nframes * (sizeof(struct usb_cdc_ncm_dpe16)));

	for (x = 0; x < nframes; x++) {
		offset = le16_to_cpu(ctx->rx_ncm.dpe16[x].wDatagramIndex);
		temp = le16_to_cpu(ctx->rx_ncm.dpe16[x].wDatagramLength);

		/*
		 * CDC NCM ch. 3.7
		 * All entries after first NULL entry are to be ignored
		 */
		if ((offset == 0) || (temp == 0)) {
			if (!x)
				goto error; /* empty NTB */
			break;
		}

		/* sanity checking */
		if (((offset + temp) > actlen) ||
		    (temp > CDC_NCM_MAX_DATAGRAM_SIZE) || (temp < ETH_HLEN)) {
			pr_debug("invalid frame detected (ignored)"
					"offset[%u]=%u, length=%u, skb=%p\n",
					x, offset, temp, skb_in);
			if (!x)
				goto error;
			break;

		} else {
			skb = skb_clone(skb_in, GFP_ATOMIC);
			if (!skb)
				goto error;
			skb->len = temp;
			skb->data = ((u8 *)skb_in->data) + offset;
			skb_set_tail_pointer(skb, temp);
			usbnet_skb_return(dev, skb);
		}
	}
	return 1;
error:
	return 0;
}

static void
cdc_ncm_speed_change(struct cdc_ncm_ctx *ctx,
		     struct usb_cdc_speed_change *data)
{
	uint32_t rx_speed = le32_to_cpu(data->DLBitRRate);
	uint32_t tx_speed = le32_to_cpu(data->ULBitRate);

	/*
	 * Currently the USB-NET API does not support reporting the actual
	 * device speed. Do print it instead.
	 */
	if ((tx_speed != ctx->tx_speed) || (rx_speed != ctx->rx_speed)) {
		ctx->tx_speed = tx_speed;
		ctx->rx_speed = rx_speed;

		if ((tx_speed > 1000000) && (rx_speed > 1000000)) {
			printk(KERN_INFO KBUILD_MODNAME
				": %s: %u mbit/s downlink "
				"%u mbit/s uplink\n",
				ctx->netdev->name,
				(unsigned int)(rx_speed / 1000000U),
				(unsigned int)(tx_speed / 1000000U));
		} else {
			printk(KERN_INFO KBUILD_MODNAME
				": %s: %u kbit/s downlink "
				"%u kbit/s uplink\n",
				ctx->netdev->name,
				(unsigned int)(rx_speed / 1000U),
				(unsigned int)(tx_speed / 1000U));
		}
	}
}

static void cdc_ncm_status(struct usbnet *dev, struct urb *urb)
{
	struct cdc_ncm_ctx *ctx;
	struct usb_cdc_notification *event;

	ctx = (struct cdc_ncm_ctx *)dev->data[0];

	if (urb->actual_length < sizeof(*event))
		return;

	/* test for split data in 8-byte chunks */
	if (test_and_clear_bit(EVENT_STS_SPLIT, &dev->flags)) {
		cdc_ncm_speed_change(ctx,
		      (struct usb_cdc_speed_change *)urb->transfer_buffer);
		return;
	}

	event = urb->transfer_buffer;

	switch (event->bNotificationType) {
	case USB_CDC_NOTIFY_NETWORK_CONNECTION:
		/*
		 * According to the CDC NCM specification ch.7.1
		 * USB_CDC_NOTIFY_NETWORK_CONNECTION notification shall be
		 * sent by device after USB_CDC_NOTIFY_SPEED_CHANGE.
		 */
		ctx->connected = event->wValue;

		printk(KERN_INFO KBUILD_MODNAME ": %s: network connection:"
			" %sconnected\n",
			ctx->netdev->name, ctx->connected ? "" : "dis");

		if (ctx->connected)
			netif_carrier_on(dev->net);
		else {
			netif_carrier_off(dev->net);
			ctx->tx_speed = ctx->rx_speed = 0;
		}
		break;

	case USB_CDC_NOTIFY_SPEED_CHANGE:
		if (urb->actual_length < (sizeof(*event) +
					sizeof(struct usb_cdc_speed_change)))
			set_bit(EVENT_STS_SPLIT, &dev->flags);
		else
			cdc_ncm_speed_change(ctx,
				(struct usb_cdc_speed_change *) &event[1]);
		break;

	default:
		dev_err(&dev->udev->dev, "NCM: unexpected "
			"notification 0x%02x!\n", event->bNotificationType);
		break;
	}
}

static int cdc_ncm_check_connect(struct usbnet *dev)
{
	struct cdc_ncm_ctx *ctx;

	ctx = (struct cdc_ncm_ctx *)dev->data[0];
	if (ctx == NULL)
		return 1;	/* disconnected */

	return !ctx->connected;
}

static int
cdc_ncm_probe(struct usb_interface *udev, const struct usb_device_id *prod)
{
	return usbnet_probe(udev, prod);
}

static void cdc_ncm_disconnect(struct usb_interface *intf)
{
	struct usbnet *dev = usb_get_intfdata(intf);

	if (dev == NULL)
		return;		/* already disconnected */

	usbnet_disconnect(intf);
}

static int cdc_ncm_manage_power(struct usbnet *dev, int status)
{
	dev->intf->needs_remote_wakeup = status;
	return 0;
}

static const struct driver_info cdc_ncm_info = {
	.description = "CDC NCM",
	.flags = FLAG_POINTTOPOINT | FLAG_NO_SETINT | FLAG_MULTI_PACKET,
	.bind = cdc_ncm_bind,
	.unbind = cdc_ncm_unbind,
	.check_connect = cdc_ncm_check_connect,
	.manage_power = cdc_ncm_manage_power,
	.status = cdc_ncm_status,
	.rx_fixup = cdc_ncm_rx_fixup,
	.tx_fixup = cdc_ncm_tx_fixup,
};

static struct usb_driver cdc_ncm_driver = {
	.name = "cdc_ncm",
	.id_table = cdc_devs,
	.probe = cdc_ncm_probe,
	.disconnect = cdc_ncm_disconnect,
	.suspend = usbnet_suspend,
	.resume = usbnet_resume,
	.reset_resume =	usbnet_resume,
	.supports_autosuspend = 1,
};

static struct ethtool_ops cdc_ncm_ethtool_ops = {
	.get_drvinfo = cdc_ncm_get_drvinfo,
	.get_link = usbnet_get_link,
	.get_msglevel = usbnet_get_msglevel,
	.set_msglevel = usbnet_set_msglevel,
	.get_settings = usbnet_get_settings,
	.set_settings = usbnet_set_settings,
	.nway_reset = usbnet_nway_reset,
};

static int __init cdc_ncm_init(void)
{
	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION "\n");
	return usb_register(&cdc_ncm_driver);
}

module_init(cdc_ncm_init);

static void __exit cdc_ncm_exit(void)
{
	usb_deregister(&cdc_ncm_driver);
}

module_exit(cdc_ncm_exit);

MODULE_AUTHOR("Hans Petter Selasky");
MODULE_DESCRIPTION("USB CDC NCM host driver");
MODULE_LICENSE("Dual BSD/GPL");
