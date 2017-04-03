/*
 * Host Side support for RNDIS Networking Links
 * Copyright (C) 2005 by David Brownell
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/usb/usbnet.h>
#include <linux/usb/rndis_host.h>


/*
 * RNDIS is NDIS remoted over USB.  It's a MSFT variant of CDC ACM ... of
 * course ACM was intended for modems, not Ethernet links!  USB's standard
 * for Ethernet links is "CDC Ethernet", which is significantly simpler.
 *
 * NOTE that Microsoft's "RNDIS 1.0" specification is incomplete.  Issues
 * include:
 *    - Power management in particular relies on information that's scattered
 *	through other documentation, and which is incomplete or incorrect even
 *	there.
 *    - There are various undocumented protocol requirements, such as the
 *	need to send unused garbage in control-OUT messages.
 *    - In some cases, MS-Windows will emit undocumented requests; this
 *	matters more to peripheral implementations than host ones.
 *
 * Moreover there's a no-open-specs variant of RNDIS called "ActiveSync".
 *
 * For these reasons and others, ** USE OF RNDIS IS STRONGLY DISCOURAGED ** in
 * favor of such non-proprietary alternatives as CDC Ethernet or the newer (and
 * currently rare) "Ethernet Emulation Model" (EEM).
 */

/*
 * RNDIS notifications from device: command completion; "reverse"
 * keepalives; etc
 */
void rndis_status(struct usbnet *dev, struct urb *urb)
{
	netdev_dbg(dev->net, "rndis status urb, len %d stat %d\n",
		   urb->actual_length, urb->status);
	// FIXME for keepalives, respond immediately (asynchronously)
	// if not an RNDIS status, do like cdc_status(dev,urb) does
}
EXPORT_SYMBOL_GPL(rndis_status);

/*
 * RNDIS indicate messages.
 */
static void rndis_msg_indicate(struct usbnet *dev, struct rndis_indicate *msg,
				int buflen)
{
	struct cdc_state *info = (void *)&dev->data;
	struct device *udev = &info->control->dev;

	if (dev->driver_info->indication) {
		dev->driver_info->indication(dev, msg, buflen);
	} else {
		u32 status = le32_to_cpu(msg->status);

		switch (status) {
		case RNDIS_STATUS_MEDIA_CONNECT:
			dev_info(udev, "rndis media connect\n");
			break;
		case RNDIS_STATUS_MEDIA_DISCONNECT:
			dev_info(udev, "rndis media disconnect\n");
			break;
		default:
			dev_info(udev, "rndis indication: 0x%08x\n", status);
		}
	}
}

/*
 * RPC done RNDIS-style.  Caller guarantees:
 * - message is properly byteswapped
 * - there's no other request pending
 * - buf can hold up to 1KB response (required by RNDIS spec)
 * On return, the first few entries are already byteswapped.
 *
 * Call context is likely probe(), before interface name is known,
 * which is why we won't try to use it in the diagnostics.
 */
int rndis_command(struct usbnet *dev, struct rndis_msg_hdr *buf, int buflen)
{
	struct cdc_state	*info = (void *) &dev->data;
	struct usb_cdc_notification notification;
	int			master_ifnum;
	int			retval;
	int			partial;
	unsigned		count;
	u32			xid = 0, msg_len, request_id, msg_type, rsp,
				status;

	/* REVISIT when this gets called from contexts other than probe() or
	 * disconnect(): either serialize, or dispatch responses on xid
	 */

	msg_type = le32_to_cpu(buf->msg_type);

	/* Issue the request; xid is unique, don't bother byteswapping it */
	if (likely(msg_type != RNDIS_MSG_HALT && msg_type != RNDIS_MSG_RESET)) {
		xid = dev->xid++;
		if (!xid)
			xid = dev->xid++;
		buf->request_id = (__force __le32) xid;
	}
	master_ifnum = info->control->cur_altsetting->desc.bInterfaceNumber;
	retval = usb_control_msg(dev->udev,
		usb_sndctrlpipe(dev->udev, 0),
		USB_CDC_SEND_ENCAPSULATED_COMMAND,
		USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		0, master_ifnum,
		buf, le32_to_cpu(buf->msg_len),
		RNDIS_CONTROL_TIMEOUT_MS);
	if (unlikely(retval < 0 || xid == 0))
		return retval;

	/* Some devices don't respond on the control channel until
	 * polled on the status channel, so do that first. */
	if (dev->driver_info->data & RNDIS_DRIVER_DATA_POLL_STATUS) {
		retval = usb_interrupt_msg(
			dev->udev,
			usb_rcvintpipe(dev->udev,
				       dev->status->desc.bEndpointAddress),
			&notification, sizeof(notification), &partial,
			RNDIS_CONTROL_TIMEOUT_MS);
		if (unlikely(retval < 0))
			return retval;
	}

	/* Poll the control channel; the request probably completed immediately */
	rsp = le32_to_cpu(buf->msg_type) | RNDIS_MSG_COMPLETION;
	for (count = 0; count < 10; count++) {
		memset(buf, 0, CONTROL_BUFFER_SIZE);
		retval = usb_control_msg(dev->udev,
			usb_rcvctrlpipe(dev->udev, 0),
			USB_CDC_GET_ENCAPSULATED_RESPONSE,
			USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			0, master_ifnum,
			buf, buflen,
			RNDIS_CONTROL_TIMEOUT_MS);
		if (likely(retval >= 8)) {
			msg_type = le32_to_cpu(buf->msg_type);
			msg_len = le32_to_cpu(buf->msg_len);
			status = le32_to_cpu(buf->status);
			request_id = (__force u32) buf->request_id;
			if (likely(msg_type == rsp)) {
				if (likely(request_id == xid)) {
					if (unlikely(rsp == RNDIS_MSG_RESET_C))
						return 0;
					if (likely(RNDIS_STATUS_SUCCESS ==
							status))
						return 0;
					dev_dbg(&info->control->dev,
						"rndis reply status %08x\n",
						status);
					return -EL3RST;
				}
				dev_dbg(&info->control->dev,
					"rndis reply id %d expected %d\n",
					request_id, xid);
				/* then likely retry */
			} else switch (msg_type) {
			case RNDIS_MSG_INDICATE: /* fault/event */
				rndis_msg_indicate(dev, (void *)buf, buflen);
				break;
			case RNDIS_MSG_KEEPALIVE: { /* ping */
				struct rndis_keepalive_c *msg = (void *)buf;

				msg->msg_type = cpu_to_le32(RNDIS_MSG_KEEPALIVE_C);
				msg->msg_len = cpu_to_le32(sizeof *msg);
				msg->status = cpu_to_le32(RNDIS_STATUS_SUCCESS);
				retval = usb_control_msg(dev->udev,
					usb_sndctrlpipe(dev->udev, 0),
					USB_CDC_SEND_ENCAPSULATED_COMMAND,
					USB_TYPE_CLASS | USB_RECIP_INTERFACE,
					0, master_ifnum,
					msg, sizeof *msg,
					RNDIS_CONTROL_TIMEOUT_MS);
				if (unlikely(retval < 0))
					dev_dbg(&info->control->dev,
						"rndis keepalive err %d\n",
						retval);
				}
				break;
			default:
				dev_dbg(&info->control->dev,
					"unexpected rndis msg %08x len %d\n",
					le32_to_cpu(buf->msg_type), msg_len);
			}
		} else {
			/* device probably issued a protocol stall; ignore */
			dev_dbg(&info->control->dev,
				"rndis response error, code %d\n", retval);
		}
		msleep(20);
	}
	dev_dbg(&info->control->dev, "rndis response timeout\n");
	return -ETIMEDOUT;
}
EXPORT_SYMBOL_GPL(rndis_command);

/*
 * rndis_query:
 *
 * Performs a query for @oid along with 0 or more bytes of payload as
 * specified by @in_len. If @reply_len is not set to -1 then the reply
 * length is checked against this value, resulting in an error if it
 * doesn't match.
 *
 * NOTE: Adding a payload exactly or greater than the size of the expected
 * response payload is an evident requirement MSFT added for ActiveSync.
 *
 * The only exception is for OIDs that return a variably sized response,
 * in which case no payload should be added.  This undocumented (and
 * nonsensical!) issue was found by sniffing protocol requests from the
 * ActiveSync 4.1 Windows driver.
 */
static int rndis_query(struct usbnet *dev, struct usb_interface *intf,
		void *buf, u32 oid, u32 in_len,
		void **reply, int *reply_len)
{
	int retval;
	union {
		void			*buf;
		struct rndis_msg_hdr	*header;
		struct rndis_query	*get;
		struct rndis_query_c	*get_c;
	} u;
	u32 off, len;

	u.buf = buf;

	memset(u.get, 0, sizeof *u.get + in_len);
	u.get->msg_type = cpu_to_le32(RNDIS_MSG_QUERY);
	u.get->msg_len = cpu_to_le32(sizeof *u.get + in_len);
	u.get->oid = cpu_to_le32(oid);
	u.get->len = cpu_to_le32(in_len);
	u.get->offset = cpu_to_le32(20);

	retval = rndis_command(dev, u.header, CONTROL_BUFFER_SIZE);
	if (unlikely(retval < 0)) {
		dev_err(&intf->dev, "RNDIS_MSG_QUERY(0x%08x) failed, %d\n",
				oid, retval);
		return retval;
	}

	off = le32_to_cpu(u.get_c->offset);
	len = le32_to_cpu(u.get_c->len);
	if (unlikely((8 + off + len) > CONTROL_BUFFER_SIZE))
		goto response_error;

	if (*reply_len != -1 && len != *reply_len)
		goto response_error;

	*reply = (unsigned char *) &u.get_c->request_id + off;
	*reply_len = len;

	return retval;

response_error:
	dev_err(&intf->dev, "RNDIS_MSG_QUERY(0x%08x) "
			"invalid response - off %d len %d\n",
		oid, off, len);
	return -EDOM;
}

/* same as usbnet_netdev_ops but MTU change not allowed */
static const struct net_device_ops rndis_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
	.ndo_start_xmit		= usbnet_start_xmit,
	.ndo_tx_timeout		= usbnet_tx_timeout,
	.ndo_get_stats64	= usbnet_get_stats64,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

int
generic_rndis_bind(struct usbnet *dev, struct usb_interface *intf, int flags)
{
	int			retval;
	struct net_device	*net = dev->net;
	struct cdc_state	*info = (void *) &dev->data;
	union {
		void			*buf;
		struct rndis_msg_hdr	*header;
		struct rndis_init	*init;
		struct rndis_init_c	*init_c;
		struct rndis_query	*get;
		struct rndis_query_c	*get_c;
		struct rndis_set	*set;
		struct rndis_set_c	*set_c;
		struct rndis_halt	*halt;
	} u;
	u32			tmp;
	__le32			phym_unspec, *phym;
	int			reply_len;
	unsigned char		*bp;

	/* we can't rely on i/o from stack working, or stack allocation */
	u.buf = kmalloc(CONTROL_BUFFER_SIZE, GFP_KERNEL);
	if (!u.buf)
		return -ENOMEM;
	retval = usbnet_generic_cdc_bind(dev, intf);
	if (retval < 0)
		goto fail;

	u.init->msg_type = cpu_to_le32(RNDIS_MSG_INIT);
	u.init->msg_len = cpu_to_le32(sizeof *u.init);
	u.init->major_version = cpu_to_le32(1);
	u.init->minor_version = cpu_to_le32(0);

	/* max transfer (in spec) is 0x4000 at full speed, but for
	 * TX we'll stick to one Ethernet packet plus RNDIS framing.
	 * For RX we handle drivers that zero-pad to end-of-packet.
	 * Don't let userspace change these settings.
	 *
	 * NOTE: there still seems to be wierdness here, as if we need
	 * to do some more things to make sure WinCE targets accept this.
	 * They default to jumbograms of 8KB or 16KB, which is absurd
	 * for such low data rates and which is also more than Linux
	 * can usually expect to allocate for SKB data...
	 */
	net->hard_header_len += sizeof (struct rndis_data_hdr);
	dev->hard_mtu = net->mtu + net->hard_header_len;

	dev->maxpacket = usb_maxpacket(dev->udev, dev->out, 1);
	if (dev->maxpacket == 0) {
		netif_dbg(dev, probe, dev->net,
			  "dev->maxpacket can't be 0\n");
		retval = -EINVAL;
		goto fail_and_release;
	}

	dev->rx_urb_size = dev->hard_mtu + (dev->maxpacket + 1);
	dev->rx_urb_size &= ~(dev->maxpacket - 1);
	u.init->max_transfer_size = cpu_to_le32(dev->rx_urb_size);

	net->netdev_ops = &rndis_netdev_ops;

	retval = rndis_command(dev, u.header, CONTROL_BUFFER_SIZE);
	if (unlikely(retval < 0)) {
		/* it might not even be an RNDIS device!! */
		dev_err(&intf->dev, "RNDIS init failed, %d\n", retval);
		goto fail_and_release;
	}
	tmp = le32_to_cpu(u.init_c->max_transfer_size);
	if (tmp < dev->hard_mtu) {
		if (tmp <= net->hard_header_len) {
			dev_err(&intf->dev,
				"dev can't take %u byte packets (max %u)\n",
				dev->hard_mtu, tmp);
			retval = -EINVAL;
			goto halt_fail_and_release;
		}
		dev_warn(&intf->dev,
			 "dev can't take %u byte packets (max %u), "
			 "adjusting MTU to %u\n",
			 dev->hard_mtu, tmp, tmp - net->hard_header_len);
		dev->hard_mtu = tmp;
		net->mtu = dev->hard_mtu - net->hard_header_len;
	}

	/* REVISIT:  peripheral "alignment" request is ignored ... */
	dev_dbg(&intf->dev,
		"hard mtu %u (%u from dev), rx buflen %zu, align %d\n",
		dev->hard_mtu, tmp, dev->rx_urb_size,
		1 << le32_to_cpu(u.init_c->packet_alignment));

	/* module has some device initialization code needs to be done right
	 * after RNDIS_INIT */
	if (dev->driver_info->early_init &&
			dev->driver_info->early_init(dev) != 0)
		goto halt_fail_and_release;

	/* Check physical medium */
	phym = NULL;
	reply_len = sizeof *phym;
	retval = rndis_query(dev, intf, u.buf,
			     RNDIS_OID_GEN_PHYSICAL_MEDIUM,
			     0, (void **) &phym, &reply_len);
	if (retval != 0 || !phym) {
		/* OID is optional so don't fail here. */
		phym_unspec = cpu_to_le32(RNDIS_PHYSICAL_MEDIUM_UNSPECIFIED);
		phym = &phym_unspec;
	}
	if ((flags & FLAG_RNDIS_PHYM_WIRELESS) &&
	    le32_to_cpup(phym) != RNDIS_PHYSICAL_MEDIUM_WIRELESS_LAN) {
		netif_dbg(dev, probe, dev->net,
			  "driver requires wireless physical medium, but device is not\n");
		retval = -ENODEV;
		goto halt_fail_and_release;
	}
	if ((flags & FLAG_RNDIS_PHYM_NOT_WIRELESS) &&
	    le32_to_cpup(phym) == RNDIS_PHYSICAL_MEDIUM_WIRELESS_LAN) {
		netif_dbg(dev, probe, dev->net,
			  "driver requires non-wireless physical medium, but device is wireless.\n");
		retval = -ENODEV;
		goto halt_fail_and_release;
	}

	/* Get designated host ethernet address */
	reply_len = ETH_ALEN;
	retval = rndis_query(dev, intf, u.buf,
			     RNDIS_OID_802_3_PERMANENT_ADDRESS,
			     48, (void **) &bp, &reply_len);
	if (unlikely(retval< 0)) {
		dev_err(&intf->dev, "rndis get ethaddr, %d\n", retval);
		goto halt_fail_and_release;
	}

	if (bp[0] & 0x02)
		eth_hw_addr_random(net);
	else
		ether_addr_copy(net->dev_addr, bp);

	/* set a nonzero filter to enable data transfers */
	memset(u.set, 0, sizeof *u.set);
	u.set->msg_type = cpu_to_le32(RNDIS_MSG_SET);
	u.set->msg_len = cpu_to_le32(4 + sizeof *u.set);
	u.set->oid = cpu_to_le32(RNDIS_OID_GEN_CURRENT_PACKET_FILTER);
	u.set->len = cpu_to_le32(4);
	u.set->offset = cpu_to_le32((sizeof *u.set) - 8);
	*(__le32 *)(u.buf + sizeof *u.set) = cpu_to_le32(RNDIS_DEFAULT_FILTER);

	retval = rndis_command(dev, u.header, CONTROL_BUFFER_SIZE);
	if (unlikely(retval < 0)) {
		dev_err(&intf->dev, "rndis set packet filter, %d\n", retval);
		goto halt_fail_and_release;
	}

	retval = 0;

	kfree(u.buf);
	return retval;

halt_fail_and_release:
	memset(u.halt, 0, sizeof *u.halt);
	u.halt->msg_type = cpu_to_le32(RNDIS_MSG_HALT);
	u.halt->msg_len = cpu_to_le32(sizeof *u.halt);
	(void) rndis_command(dev, (void *)u.halt, CONTROL_BUFFER_SIZE);
fail_and_release:
	usb_set_intfdata(info->data, NULL);
	usb_driver_release_interface(driver_of(intf), info->data);
	info->data = NULL;
fail:
	kfree(u.buf);
	return retval;
}
EXPORT_SYMBOL_GPL(generic_rndis_bind);

static int rndis_bind(struct usbnet *dev, struct usb_interface *intf)
{
	return generic_rndis_bind(dev, intf, FLAG_RNDIS_PHYM_NOT_WIRELESS);
}

void rndis_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	struct rndis_halt	*halt;

	/* try to clear any rndis state/activity (no i/o from stack!) */
	halt = kzalloc(CONTROL_BUFFER_SIZE, GFP_KERNEL);
	if (halt) {
		halt->msg_type = cpu_to_le32(RNDIS_MSG_HALT);
		halt->msg_len = cpu_to_le32(sizeof *halt);
		(void) rndis_command(dev, (void *)halt, CONTROL_BUFFER_SIZE);
		kfree(halt);
	}

	usbnet_cdc_unbind(dev, intf);
}
EXPORT_SYMBOL_GPL(rndis_unbind);

/*
 * DATA -- host must not write zlps
 */
int rndis_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	/* This check is no longer done by usbnet */
	if (skb->len < dev->net->hard_header_len)
		return 0;

	/* peripheral may have batched packets to us... */
	while (likely(skb->len)) {
		struct rndis_data_hdr	*hdr = (void *)skb->data;
		struct sk_buff		*skb2;
		u32			msg_type, msg_len, data_offset, data_len;

		msg_type = le32_to_cpu(hdr->msg_type);
		msg_len = le32_to_cpu(hdr->msg_len);
		data_offset = le32_to_cpu(hdr->data_offset);
		data_len = le32_to_cpu(hdr->data_len);

		/* don't choke if we see oob, per-packet data, etc */
		if (unlikely(msg_type != RNDIS_MSG_PACKET || skb->len < msg_len
				|| (data_offset + data_len + 8) > msg_len)) {
			dev->net->stats.rx_frame_errors++;
			netdev_dbg(dev->net, "bad rndis message %d/%d/%d/%d, len %d\n",
				   le32_to_cpu(hdr->msg_type),
				   msg_len, data_offset, data_len, skb->len);
			return 0;
		}
		skb_pull(skb, 8 + data_offset);

		/* at most one packet left? */
		if (likely((data_len - skb->len) <= sizeof *hdr)) {
			skb_trim(skb, data_len);
			break;
		}

		/* try to return all the packets in the batch */
		skb2 = skb_clone(skb, GFP_ATOMIC);
		if (unlikely(!skb2))
			break;
		skb_pull(skb, msg_len - sizeof *hdr);
		skb_trim(skb2, data_len);
		usbnet_skb_return(dev, skb2);
	}

	/* caller will usbnet_skb_return the remaining packet */
	return 1;
}
EXPORT_SYMBOL_GPL(rndis_rx_fixup);

struct sk_buff *
rndis_tx_fixup(struct usbnet *dev, struct sk_buff *skb, gfp_t flags)
{
	struct rndis_data_hdr	*hdr;
	struct sk_buff		*skb2;
	unsigned		len = skb->len;

	if (likely(!skb_cloned(skb))) {
		int	room = skb_headroom(skb);

		/* enough head room as-is? */
		if (unlikely((sizeof *hdr) <= room))
			goto fill;

		/* enough room, but needs to be readjusted? */
		room += skb_tailroom(skb);
		if (likely((sizeof *hdr) <= room)) {
			skb->data = memmove(skb->head + sizeof *hdr,
					    skb->data, len);
			skb_set_tail_pointer(skb, len);
			goto fill;
		}
	}

	/* create a new skb, with the correct size (and tailpad) */
	skb2 = skb_copy_expand(skb, sizeof *hdr, 1, flags);
	dev_kfree_skb_any(skb);
	if (unlikely(!skb2))
		return skb2;
	skb = skb2;

	/* fill out the RNDIS header.  we won't bother trying to batch
	 * packets; Linux minimizes wasted bandwidth through tx queues.
	 */
fill:
	hdr = (void *) __skb_push(skb, sizeof *hdr);
	memset(hdr, 0, sizeof *hdr);
	hdr->msg_type = cpu_to_le32(RNDIS_MSG_PACKET);
	hdr->msg_len = cpu_to_le32(skb->len);
	hdr->data_offset = cpu_to_le32(sizeof(*hdr) - 8);
	hdr->data_len = cpu_to_le32(len);

	/* FIXME make the last packet always be short ... */
	return skb;
}
EXPORT_SYMBOL_GPL(rndis_tx_fixup);


static const struct driver_info	rndis_info = {
	.description =	"RNDIS device",
	.flags =	FLAG_ETHER | FLAG_POINTTOPOINT | FLAG_FRAMING_RN | FLAG_NO_SETINT,
	.bind =		rndis_bind,
	.unbind =	rndis_unbind,
	.status =	rndis_status,
	.rx_fixup =	rndis_rx_fixup,
	.tx_fixup =	rndis_tx_fixup,
};

static const struct driver_info	rndis_poll_status_info = {
	.description =	"RNDIS device (poll status before control)",
	.flags =	FLAG_ETHER | FLAG_POINTTOPOINT | FLAG_FRAMING_RN | FLAG_NO_SETINT,
	.data =		RNDIS_DRIVER_DATA_POLL_STATUS,
	.bind =		rndis_bind,
	.unbind =	rndis_unbind,
	.status =	rndis_status,
	.rx_fixup =	rndis_rx_fixup,
	.tx_fixup =	rndis_tx_fixup,
};

/*-------------------------------------------------------------------------*/

static const struct usb_device_id	products [] = {
{
	/* 2Wire HomePortal 1000SW */
	USB_DEVICE_AND_INTERFACE_INFO(0x1630, 0x0042,
				      USB_CLASS_COMM, 2 /* ACM */, 0x0ff),
	.driver_info = (unsigned long) &rndis_poll_status_info,
}, {
	/* RNDIS is MSFT's un-official variant of CDC ACM */
	USB_INTERFACE_INFO(USB_CLASS_COMM, 2 /* ACM */, 0x0ff),
	.driver_info = (unsigned long) &rndis_info,
}, {
	/* "ActiveSync" is an undocumented variant of RNDIS, used in WM5 */
	USB_INTERFACE_INFO(USB_CLASS_MISC, 1, 1),
	.driver_info = (unsigned long) &rndis_poll_status_info,
}, {
	/* RNDIS for tethering */
	USB_INTERFACE_INFO(USB_CLASS_WIRELESS_CONTROLLER, 1, 3),
	.driver_info = (unsigned long) &rndis_info,
},
	{ },		// END
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver rndis_driver = {
	.name =		"rndis_host",
	.id_table =	products,
	.probe =	usbnet_probe,
	.disconnect =	usbnet_disconnect,
	.suspend =	usbnet_suspend,
	.resume =	usbnet_resume,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(rndis_driver);

MODULE_AUTHOR("David Brownell");
MODULE_DESCRIPTION("USB Host side RNDIS driver");
MODULE_LICENSE("GPL");
