/*
 * USB-to-WWAN Driver for Sierra Wireless modems
 *
 * Copyright (C) 2008, 2009, 2010 Paxton Smith, Matthew Safar, Rory Filer
 *                          <linux@sierrawireless.com>
 *
 * Portions of this based on the cdc_ether driver by David Brownell (2003-2005)
 * and Ole Andre Vadla Ravnas (ActiveSync) (2006).
 *
 * IMPORTANT DISCLAIMER: This driver is not commercially supported by
 * Sierra Wireless. Use at your own risk.
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

#define DRIVER_VERSION "v.2.0"
#define DRIVER_AUTHOR "Paxton Smith, Matthew Safar, Rory Filer"
#define DRIVER_DESC "USB-to-WWAN Driver for Sierra Wireless modems"
static const char driver_name[] = "sierra_net";

/* if defined debug messages enabled */
/*#define	DEBUG*/

#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <net/ip.h>
#include <net/udp.h>
#include <asm/unaligned.h>
#include <linux/usb/usbnet.h>

#define SWI_USB_REQUEST_GET_FW_ATTR	0x06
#define SWI_GET_FW_ATTR_MASK		0x08

/* atomic counter partially included in MAC address to make sure 2 devices
 * do not end up with the same MAC - concept breaks in case of > 255 ifaces
 */
static	atomic_t iface_counter = ATOMIC_INIT(0);

/*
 * SYNC Timer Delay definition used to set the expiry time
 */
#define SIERRA_NET_SYNCDELAY (2*HZ)

/* Max. MTU supported. The modem buffers are limited to 1500 */
#define SIERRA_NET_MAX_SUPPORTED_MTU	1500

/* The SIERRA_NET_USBCTL_BUF_LEN defines a buffer size allocated for control
 * message reception ... and thus the max. received packet.
 * (May be the cause for parse_hip returning -EINVAL)
 */
#define SIERRA_NET_USBCTL_BUF_LEN	1024

/* Overriding the default usbnet rx_urb_size */
#define SIERRA_NET_RX_URB_SIZE		(8 * 1024)

/* Private data structure */
struct sierra_net_data {

	u16 link_up;		/* air link up or down */
	u8 tx_hdr_template[4];	/* part of HIP hdr for tx'd packets */

	u8 sync_msg[4];		/* SYNC message */
	u8 shdwn_msg[4];	/* Shutdown message */

	/* Backpointer to the container */
	struct usbnet *usbnet;

	u8 ifnum;	/* interface number */

/* Bit masks, must be a power of 2 */
#define SIERRA_NET_EVENT_RESP_AVAIL    0x01
#define SIERRA_NET_TIMER_EXPIRY        0x02
	unsigned long kevent_flags;
	struct work_struct sierra_net_kevent;
	struct timer_list sync_timer; /* For retrying SYNC sequence */
};

struct param {
	int is_present;
	union {
		void  *ptr;
		u32    dword;
		u16    word;
		u8     byte;
	};
};

/* HIP message type */
#define SIERRA_NET_HIP_EXTENDEDID	0x7F
#define SIERRA_NET_HIP_HSYNC_ID		0x60	/* Modem -> host */
#define SIERRA_NET_HIP_RESTART_ID	0x62	/* Modem -> host */
#define SIERRA_NET_HIP_MSYNC_ID		0x20	/* Host -> modem */
#define SIERRA_NET_HIP_SHUTD_ID		0x26	/* Host -> modem */

#define SIERRA_NET_HIP_EXT_IP_IN_ID   0x0202
#define SIERRA_NET_HIP_EXT_IP_OUT_ID  0x0002

/* 3G UMTS Link Sense Indication definitions */
#define SIERRA_NET_HIP_LSI_UMTSID	0x78

/* Reverse Channel Grant Indication HIP message */
#define SIERRA_NET_HIP_RCGI		0x64

/* LSI Protocol types */
#define SIERRA_NET_PROTOCOL_UMTS      0x01
#define SIERRA_NET_PROTOCOL_UMTS_DS   0x04
/* LSI Coverage */
#define SIERRA_NET_COVERAGE_NONE      0x00
#define SIERRA_NET_COVERAGE_NOPACKET  0x01

/* LSI Session */
#define SIERRA_NET_SESSION_IDLE       0x00
/* LSI Link types */
#define SIERRA_NET_AS_LINK_TYPE_IPV4  0x00
#define SIERRA_NET_AS_LINK_TYPE_IPV6  0x02

struct lsi_umts {
	u8 protocol;
	u8 unused1;
	__be16 length;
	/* eventually use a union for the rest - assume umts for now */
	u8 coverage;
	u8 network_len; /* network name len */
	u8 network[40]; /* network name (UCS2, bigendian) */
	u8 session_state;
	u8 unused3[33];
} __packed;

struct lsi_umts_single {
	struct lsi_umts lsi;
	u8 link_type;
	u8 pdp_addr_len; /* NW-supplied PDP address len */
	u8 pdp_addr[16]; /* NW-supplied PDP address (bigendian)) */
	u8 unused4[23];
	u8 dns1_addr_len; /* NW-supplied 1st DNS address len (bigendian) */
	u8 dns1_addr[16]; /* NW-supplied 1st DNS address */
	u8 dns2_addr_len; /* NW-supplied 2nd DNS address len */
	u8 dns2_addr[16]; /* NW-supplied 2nd DNS address (bigendian)*/
	u8 wins1_addr_len; /* NW-supplied 1st Wins address len */
	u8 wins1_addr[16]; /* NW-supplied 1st Wins address (bigendian)*/
	u8 wins2_addr_len; /* NW-supplied 2nd Wins address len */
	u8 wins2_addr[16]; /* NW-supplied 2nd Wins address (bigendian) */
	u8 unused5[4];
	u8 gw_addr_len; /* NW-supplied GW address len */
	u8 gw_addr[16]; /* NW-supplied GW address (bigendian) */
	u8 reserved[8];
} __packed;

struct lsi_umts_dual {
	struct lsi_umts lsi;
	u8 pdp_addr4_len; /* NW-supplied PDP IPv4 address len */
	u8 pdp_addr4[4];  /* NW-supplied PDP IPv4 address (bigendian)) */
	u8 pdp_addr6_len; /* NW-supplied PDP IPv6 address len */
	u8 pdp_addr6[16]; /* NW-supplied PDP IPv6 address (bigendian)) */
	u8 unused4[23];
	u8 dns1_addr4_len; /* NW-supplied 1st DNS v4 address len (bigendian) */
	u8 dns1_addr4[4];  /* NW-supplied 1st DNS v4 address */
	u8 dns1_addr6_len; /* NW-supplied 1st DNS v6 address len */
	u8 dns1_addr6[16]; /* NW-supplied 1st DNS v6 address (bigendian)*/
	u8 dns2_addr4_len; /* NW-supplied 2nd DNS v4 address len (bigendian) */
	u8 dns2_addr4[4];  /* NW-supplied 2nd DNS v4 address */
	u8 dns2_addr6_len; /* NW-supplied 2nd DNS v6 address len */
	u8 dns2_addr6[16]; /* NW-supplied 2nd DNS v6 address (bigendian)*/
	u8 unused5[68];
} __packed;

#define SIERRA_NET_LSI_COMMON_LEN      4
#define SIERRA_NET_LSI_UMTS_LEN        (sizeof(struct lsi_umts_single))
#define SIERRA_NET_LSI_UMTS_STATUS_LEN \
	(SIERRA_NET_LSI_UMTS_LEN - SIERRA_NET_LSI_COMMON_LEN)
#define SIERRA_NET_LSI_UMTS_DS_LEN     (sizeof(struct lsi_umts_dual))
#define SIERRA_NET_LSI_UMTS_DS_STATUS_LEN \
	(SIERRA_NET_LSI_UMTS_DS_LEN - SIERRA_NET_LSI_COMMON_LEN)

/* Our own net device operations structure */
static const struct net_device_ops sierra_net_device_ops = {
	.ndo_open               = usbnet_open,
	.ndo_stop               = usbnet_stop,
	.ndo_start_xmit         = usbnet_start_xmit,
	.ndo_tx_timeout         = usbnet_tx_timeout,
	.ndo_change_mtu         = usbnet_change_mtu,
	.ndo_get_stats64        = usbnet_get_stats64,
	.ndo_set_mac_address    = eth_mac_addr,
	.ndo_validate_addr      = eth_validate_addr,
};

/* get private data associated with passed in usbnet device */
static inline struct sierra_net_data *sierra_net_get_private(struct usbnet *dev)
{
	return (struct sierra_net_data *)dev->data[0];
}

/* set private data associated with passed in usbnet device */
static inline void sierra_net_set_private(struct usbnet *dev,
			struct sierra_net_data *priv)
{
	dev->data[0] = (unsigned long)priv;
}

/* is packet IPv4/IPv6 */
static inline int is_ip(struct sk_buff *skb)
{
	return skb->protocol == cpu_to_be16(ETH_P_IP) ||
	       skb->protocol == cpu_to_be16(ETH_P_IPV6);
}

/*
 * check passed in packet and make sure that:
 *  - it is linear (no scatter/gather)
 *  - it is ethernet (mac_header properly set)
 */
static int check_ethip_packet(struct sk_buff *skb, struct usbnet *dev)
{
	skb_reset_mac_header(skb); /* ethernet header */

	if (skb_is_nonlinear(skb)) {
		netdev_err(dev->net, "Non linear buffer-dropping\n");
		return 0;
	}

	if (!pskb_may_pull(skb, ETH_HLEN))
		return 0;
	skb->protocol = eth_hdr(skb)->h_proto;

	return 1;
}

static const u8 *save16bit(struct param *p, const u8 *datap)
{
	p->is_present = 1;
	p->word = get_unaligned_be16(datap);
	return datap + sizeof(p->word);
}

static const u8 *save8bit(struct param *p, const u8 *datap)
{
	p->is_present = 1;
	p->byte = *datap;
	return datap + sizeof(p->byte);
}

/*----------------------------------------------------------------------------*
 *                              BEGIN HIP                                     *
 *----------------------------------------------------------------------------*/
/* HIP header */
#define SIERRA_NET_HIP_HDR_LEN 4
/* Extended HIP header */
#define SIERRA_NET_HIP_EXT_HDR_LEN 6

struct hip_hdr {
	int    hdrlen;
	struct param payload_len;
	struct param msgid;
	struct param msgspecific;
	struct param extmsgid;
};

static int parse_hip(const u8 *buf, const u32 buflen, struct hip_hdr *hh)
{
	const u8 *curp = buf;
	int    padded;

	if (buflen < SIERRA_NET_HIP_HDR_LEN)
		return -EPROTO;

	curp = save16bit(&hh->payload_len, curp);
	curp = save8bit(&hh->msgid, curp);
	curp = save8bit(&hh->msgspecific, curp);

	padded = hh->msgid.byte & 0x80;
	hh->msgid.byte &= 0x7F;			/* 7 bits */

	hh->extmsgid.is_present = (hh->msgid.byte == SIERRA_NET_HIP_EXTENDEDID);
	if (hh->extmsgid.is_present) {
		if (buflen < SIERRA_NET_HIP_EXT_HDR_LEN)
			return -EPROTO;

		hh->payload_len.word &= 0x3FFF; /* 14 bits */

		curp = save16bit(&hh->extmsgid, curp);
		hh->extmsgid.word &= 0x03FF;	/* 10 bits */

		hh->hdrlen = SIERRA_NET_HIP_EXT_HDR_LEN;
	} else {
		hh->payload_len.word &= 0x07FF;	/* 11 bits */
		hh->hdrlen = SIERRA_NET_HIP_HDR_LEN;
	}

	if (padded) {
		hh->hdrlen++;
		hh->payload_len.word--;
	}

	/* if real packet shorter than the claimed length */
	if (buflen < (hh->hdrlen + hh->payload_len.word))
		return -EINVAL;

	return 0;
}

static void build_hip(u8 *buf, const u16 payloadlen,
		struct sierra_net_data *priv)
{
	/* the following doesn't have the full functionality. We
	 * currently build only one kind of header, so it is faster this way
	 */
	put_unaligned_be16(payloadlen, buf);
	memcpy(buf+2, priv->tx_hdr_template, sizeof(priv->tx_hdr_template));
}
/*----------------------------------------------------------------------------*
 *                              END HIP                                       *
 *----------------------------------------------------------------------------*/

static int sierra_net_send_cmd(struct usbnet *dev,
		u8 *cmd, int cmdlen, const char * cmd_name)
{
	struct sierra_net_data *priv = sierra_net_get_private(dev);
	int  status;

	status = usbnet_write_cmd(dev, USB_CDC_SEND_ENCAPSULATED_COMMAND,
				  USB_DIR_OUT|USB_TYPE_CLASS|USB_RECIP_INTERFACE,
				  0, priv->ifnum, cmd, cmdlen);

	if (status != cmdlen && status != -ENODEV)
		netdev_err(dev->net, "Submit %s failed %d\n", cmd_name, status);

	return status;
}

static int sierra_net_send_sync(struct usbnet *dev)
{
	int  status;
	struct sierra_net_data *priv = sierra_net_get_private(dev);

	dev_dbg(&dev->udev->dev, "%s", __func__);

	status = sierra_net_send_cmd(dev, priv->sync_msg,
			sizeof(priv->sync_msg), "SYNC");

	return status;
}

static void sierra_net_set_ctx_index(struct sierra_net_data *priv, u8 ctx_ix)
{
	dev_dbg(&(priv->usbnet->udev->dev), "%s %d", __func__, ctx_ix);
	priv->tx_hdr_template[0] = 0x3F;
	priv->tx_hdr_template[1] = ctx_ix;
	*((__be16 *)&priv->tx_hdr_template[2]) =
		cpu_to_be16(SIERRA_NET_HIP_EXT_IP_OUT_ID);
}

static inline int sierra_net_is_valid_addrlen(u8 len)
{
	return len == sizeof(struct in_addr);
}

static int sierra_net_parse_lsi(struct usbnet *dev, char *data, int datalen)
{
	struct lsi_umts *lsi = (struct lsi_umts *)data;
	u32 expected_length;

	if (datalen < sizeof(struct lsi_umts_single)) {
		netdev_err(dev->net, "%s: Data length %d, exp >= %zu\n",
			   __func__, datalen, sizeof(struct lsi_umts_single));
		return -1;
	}

	/* Validate the session state */
	if (lsi->session_state == SIERRA_NET_SESSION_IDLE) {
		netdev_err(dev->net, "Session idle, 0x%02x\n",
			   lsi->session_state);
		return 0;
	}

	/* Validate the protocol  - only support UMTS for now */
	if (lsi->protocol == SIERRA_NET_PROTOCOL_UMTS) {
		struct lsi_umts_single *single = (struct lsi_umts_single *)lsi;

		/* Validate the link type */
		if (single->link_type != SIERRA_NET_AS_LINK_TYPE_IPV4 &&
		    single->link_type != SIERRA_NET_AS_LINK_TYPE_IPV6) {
			netdev_err(dev->net, "Link type unsupported: 0x%02x\n",
				   single->link_type);
			return -1;
		}
		expected_length = SIERRA_NET_LSI_UMTS_STATUS_LEN;
	} else if (lsi->protocol == SIERRA_NET_PROTOCOL_UMTS_DS) {
		expected_length = SIERRA_NET_LSI_UMTS_DS_STATUS_LEN;
	} else {
		netdev_err(dev->net, "Protocol unsupported, 0x%02x\n",
			   lsi->protocol);
		return -1;
	}

	if (be16_to_cpu(lsi->length) != expected_length) {
		netdev_err(dev->net, "%s: LSI_UMTS_STATUS_LEN %d, exp %u\n",
			   __func__, be16_to_cpu(lsi->length), expected_length);
		return -1;
	}

	/* Validate the coverage */
	if (lsi->coverage == SIERRA_NET_COVERAGE_NONE ||
	    lsi->coverage == SIERRA_NET_COVERAGE_NOPACKET) {
		netdev_err(dev->net, "No coverage, 0x%02x\n", lsi->coverage);
		return 0;
	}

	/* Set link_sense true */
	return 1;
}

static void sierra_net_handle_lsi(struct usbnet *dev, char *data,
		struct hip_hdr	*hh)
{
	struct sierra_net_data *priv = sierra_net_get_private(dev);
	int link_up;

	link_up = sierra_net_parse_lsi(dev, data + hh->hdrlen,
					hh->payload_len.word);
	if (link_up < 0) {
		netdev_err(dev->net, "Invalid LSI\n");
		return;
	}
	if (link_up) {
		sierra_net_set_ctx_index(priv, hh->msgspecific.byte);
		priv->link_up = 1;
	} else {
		priv->link_up = 0;
	}
	usbnet_link_change(dev, link_up, 0);
}

static void sierra_net_dosync(struct usbnet *dev)
{
	int status;
	struct sierra_net_data *priv = sierra_net_get_private(dev);

	dev_dbg(&dev->udev->dev, "%s", __func__);

	/* The SIERRA_NET_HIP_MSYNC_ID command appears to request that the
	 * firmware restart itself.  After restarting, the modem will respond
	 * with the SIERRA_NET_HIP_RESTART_ID indication.  The driver continues
	 * sending MSYNC commands every few seconds until it receives the
	 * RESTART event from the firmware
	 */

	/* tell modem we are ready */
	status = sierra_net_send_sync(dev);
	if (status < 0)
		netdev_err(dev->net,
			"Send SYNC failed, status %d\n", status);
	status = sierra_net_send_sync(dev);
	if (status < 0)
		netdev_err(dev->net,
			"Send SYNC failed, status %d\n", status);

	/* Now, start a timer and make sure we get the Restart Indication */
	priv->sync_timer.expires = jiffies + SIERRA_NET_SYNCDELAY;
	add_timer(&priv->sync_timer);
}

static void sierra_net_kevent(struct work_struct *work)
{
	struct sierra_net_data *priv =
		container_of(work, struct sierra_net_data, sierra_net_kevent);
	struct usbnet *dev = priv->usbnet;
	int  len;
	int  err;
	u8  *buf;
	u8   ifnum;

	if (test_bit(SIERRA_NET_EVENT_RESP_AVAIL, &priv->kevent_flags)) {
		clear_bit(SIERRA_NET_EVENT_RESP_AVAIL, &priv->kevent_flags);

		/* Query the modem for the LSI message */
		buf = kzalloc(SIERRA_NET_USBCTL_BUF_LEN, GFP_KERNEL);
		if (!buf)
			return;

		ifnum = priv->ifnum;
		len = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0),
				USB_CDC_GET_ENCAPSULATED_RESPONSE,
				USB_DIR_IN|USB_TYPE_CLASS|USB_RECIP_INTERFACE,
				0, ifnum, buf, SIERRA_NET_USBCTL_BUF_LEN,
				USB_CTRL_SET_TIMEOUT);

		if (len < 0) {
			netdev_err(dev->net,
				"usb_control_msg failed, status %d\n", len);
		} else {
			struct hip_hdr	hh;

			dev_dbg(&dev->udev->dev, "%s: Received status message,"
				" %04x bytes", __func__, len);

			err = parse_hip(buf, len, &hh);
			if (err) {
				netdev_err(dev->net, "%s: Bad packet,"
					" parse result %d\n", __func__, err);
				kfree(buf);
				return;
			}

			/* Validate packet length */
			if (len != hh.hdrlen + hh.payload_len.word) {
				netdev_err(dev->net, "%s: Bad packet, received"
					" %d, expected %d\n",	__func__, len,
					hh.hdrlen + hh.payload_len.word);
				kfree(buf);
				return;
			}

			/* Switch on received message types */
			switch (hh.msgid.byte) {
			case SIERRA_NET_HIP_LSI_UMTSID:
				dev_dbg(&dev->udev->dev, "LSI for ctx:%d",
					hh.msgspecific.byte);
				sierra_net_handle_lsi(dev, buf, &hh);
				break;
			case SIERRA_NET_HIP_RESTART_ID:
				dev_dbg(&dev->udev->dev, "Restart reported: %d,"
						" stopping sync timer",
						hh.msgspecific.byte);
				/* Got sync resp - stop timer & clear mask */
				del_timer_sync(&priv->sync_timer);
				clear_bit(SIERRA_NET_TIMER_EXPIRY,
					  &priv->kevent_flags);
				break;
			case SIERRA_NET_HIP_HSYNC_ID:
				dev_dbg(&dev->udev->dev, "SYNC received");
				err = sierra_net_send_sync(dev);
				if (err < 0)
					netdev_err(dev->net,
						"Send SYNC failed %d\n", err);
				break;
			case SIERRA_NET_HIP_EXTENDEDID:
				netdev_err(dev->net, "Unrecognized HIP msg, "
					"extmsgid 0x%04x\n", hh.extmsgid.word);
				break;
			case SIERRA_NET_HIP_RCGI:
				/* Ignored */
				break;
			default:
				netdev_err(dev->net, "Unrecognized HIP msg, "
					"msgid 0x%02x\n", hh.msgid.byte);
				break;
			}
		}
		kfree(buf);
	}
	/* The sync timer bit might be set */
	if (test_bit(SIERRA_NET_TIMER_EXPIRY, &priv->kevent_flags)) {
		clear_bit(SIERRA_NET_TIMER_EXPIRY, &priv->kevent_flags);
		dev_dbg(&dev->udev->dev, "Deferred sync timer expiry");
		sierra_net_dosync(priv->usbnet);
	}

	if (priv->kevent_flags)
		dev_dbg(&dev->udev->dev, "sierra_net_kevent done, "
			"kevent_flags = 0x%lx", priv->kevent_flags);
}

static void sierra_net_defer_kevent(struct usbnet *dev, int work)
{
	struct sierra_net_data *priv = sierra_net_get_private(dev);

	set_bit(work, &priv->kevent_flags);
	schedule_work(&priv->sierra_net_kevent);
}

/*
 * Sync Retransmit Timer Handler. On expiry, kick the work queue
 */
static void sierra_sync_timer(struct timer_list *t)
{
	struct sierra_net_data *priv = from_timer(priv, t, sync_timer);
	struct usbnet *dev = priv->usbnet;

	dev_dbg(&dev->udev->dev, "%s", __func__);
	/* Kick the tasklet */
	sierra_net_defer_kevent(dev, SIERRA_NET_TIMER_EXPIRY);
}

static void sierra_net_status(struct usbnet *dev, struct urb *urb)
{
	struct usb_cdc_notification *event;

	dev_dbg(&dev->udev->dev, "%s", __func__);

	if (urb->actual_length < sizeof *event)
		return;

	/* Add cases to handle other standard notifications. */
	event = urb->transfer_buffer;
	switch (event->bNotificationType) {
	case USB_CDC_NOTIFY_NETWORK_CONNECTION:
	case USB_CDC_NOTIFY_SPEED_CHANGE:
		/* USB 305 sends those */
		break;
	case USB_CDC_NOTIFY_RESPONSE_AVAILABLE:
		sierra_net_defer_kevent(dev, SIERRA_NET_EVENT_RESP_AVAIL);
		break;
	default:
		netdev_err(dev->net, ": unexpected notification %02x!\n",
				event->bNotificationType);
		break;
	}
}

static void sierra_net_get_drvinfo(struct net_device *net,
		struct ethtool_drvinfo *info)
{
	/* Inherit standard device info */
	usbnet_get_drvinfo(net, info);
	strlcpy(info->driver, driver_name, sizeof(info->driver));
	strlcpy(info->version, DRIVER_VERSION, sizeof(info->version));
}

static u32 sierra_net_get_link(struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);
	/* Report link is down whenever the interface is down */
	return sierra_net_get_private(dev)->link_up && netif_running(net);
}

static const struct ethtool_ops sierra_net_ethtool_ops = {
	.get_drvinfo = sierra_net_get_drvinfo,
	.get_link = sierra_net_get_link,
	.get_msglevel = usbnet_get_msglevel,
	.set_msglevel = usbnet_set_msglevel,
	.nway_reset = usbnet_nway_reset,
	.get_link_ksettings = usbnet_get_link_ksettings,
	.set_link_ksettings = usbnet_set_link_ksettings,
};

static int sierra_net_get_fw_attr(struct usbnet *dev, u16 *datap)
{
	int result = 0;
	__le16 attrdata;

	result = usbnet_read_cmd(dev,
				/* _u8 vendor specific request */
				SWI_USB_REQUEST_GET_FW_ATTR,
				USB_DIR_IN | USB_TYPE_VENDOR,	/* __u8 request type */
				0x0000,		/* __u16 value not used */
				0x0000,		/* __u16 index  not used */
				&attrdata,	/* char *data */
				sizeof(attrdata)	/* __u16 size */
				);

	if (result < 0)
		return -EIO;

	*datap = le16_to_cpu(attrdata);
	return result;
}

/*
 * collects the bulk endpoints, the status endpoint.
 */
static int sierra_net_bind(struct usbnet *dev, struct usb_interface *intf)
{
	u8	ifacenum;
	u8	numendpoints;
	u16	fwattr = 0;
	int	status;
	struct sierra_net_data *priv;
	static const u8 sync_tmplate[sizeof(priv->sync_msg)] = {
		0x00, 0x00, SIERRA_NET_HIP_MSYNC_ID, 0x00};
	static const u8 shdwn_tmplate[sizeof(priv->shdwn_msg)] = {
		0x00, 0x00, SIERRA_NET_HIP_SHUTD_ID, 0x00};

	dev_dbg(&dev->udev->dev, "%s", __func__);

	ifacenum = intf->cur_altsetting->desc.bInterfaceNumber;
	numendpoints = intf->cur_altsetting->desc.bNumEndpoints;
	/* We have three endpoints, bulk in and out, and a status */
	if (numendpoints != 3) {
		dev_err(&dev->udev->dev, "Expected 3 endpoints, found: %d",
			numendpoints);
		return -ENODEV;
	}
	/* Status endpoint set in usbnet_get_endpoints() */
	dev->status = NULL;
	status = usbnet_get_endpoints(dev, intf);
	if (status < 0) {
		dev_err(&dev->udev->dev, "Error in usbnet_get_endpoints (%d)",
			status);
		return -ENODEV;
	}
	/* Initialize sierra private data */
	priv = kzalloc(sizeof *priv, GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->usbnet = dev;
	priv->ifnum = ifacenum;
	dev->net->netdev_ops = &sierra_net_device_ops;

	/* change MAC addr to include, ifacenum, and to be unique */
	dev->net->dev_addr[ETH_ALEN-2] = atomic_inc_return(&iface_counter);
	dev->net->dev_addr[ETH_ALEN-1] = ifacenum;

	/* prepare shutdown message template */
	memcpy(priv->shdwn_msg, shdwn_tmplate, sizeof(priv->shdwn_msg));
	/* set context index initially to 0 - prepares tx hdr template */
	sierra_net_set_ctx_index(priv, 0);

	/* prepare sync message template */
	memcpy(priv->sync_msg, sync_tmplate, sizeof(priv->sync_msg));

	/* decrease the rx_urb_size and max_tx_size to 4k on USB 1.1 */
	dev->rx_urb_size  = SIERRA_NET_RX_URB_SIZE;
	if (dev->udev->speed != USB_SPEED_HIGH)
		dev->rx_urb_size  = min_t(size_t, 4096, SIERRA_NET_RX_URB_SIZE);

	dev->net->hard_header_len += SIERRA_NET_HIP_EXT_HDR_LEN;
	dev->hard_mtu = dev->net->mtu + dev->net->hard_header_len;
	dev->net->max_mtu = SIERRA_NET_MAX_SUPPORTED_MTU;

	/* Set up the netdev */
	dev->net->flags |= IFF_NOARP;
	dev->net->ethtool_ops = &sierra_net_ethtool_ops;
	netif_carrier_off(dev->net);

	sierra_net_set_private(dev, priv);

	priv->kevent_flags = 0;

	/* Use the shared workqueue */
	INIT_WORK(&priv->sierra_net_kevent, sierra_net_kevent);

	/* Only need to do this once */
	timer_setup(&priv->sync_timer, sierra_sync_timer, 0);

	/* verify fw attributes */
	status = sierra_net_get_fw_attr(dev, &fwattr);
	dev_dbg(&dev->udev->dev, "Fw attr: %x\n", fwattr);

	/* test whether firmware supports DHCP */
	if (!(status == sizeof(fwattr) && (fwattr & SWI_GET_FW_ATTR_MASK))) {
		/* found incompatible firmware version */
		dev_err(&dev->udev->dev, "Incompatible driver and firmware"
			" versions\n");
		kfree(priv);
		return -ENODEV;
	}

	return 0;
}

static void sierra_net_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	int status;
	struct sierra_net_data *priv = sierra_net_get_private(dev);

	dev_dbg(&dev->udev->dev, "%s", __func__);

	/* kill the timer and work */
	del_timer_sync(&priv->sync_timer);
	cancel_work_sync(&priv->sierra_net_kevent);

	/* tell modem we are going away */
	status = sierra_net_send_cmd(dev, priv->shdwn_msg,
			sizeof(priv->shdwn_msg), "Shutdown");
	if (status < 0)
		netdev_err(dev->net,
			"usb_control_msg failed, status %d\n", status);

	usbnet_status_stop(dev);

	sierra_net_set_private(dev, NULL);
	kfree(priv);
}

static struct sk_buff *sierra_net_skb_clone(struct usbnet *dev,
		struct sk_buff *skb, int len)
{
	struct sk_buff *new_skb;

	/* clone skb */
	new_skb = skb_clone(skb, GFP_ATOMIC);

	/* remove len bytes from original */
	skb_pull(skb, len);

	/* trim next packet to it's length */
	if (new_skb) {
		skb_trim(new_skb, len);
	} else {
		if (netif_msg_rx_err(dev))
			netdev_err(dev->net, "failed to get skb\n");
		dev->net->stats.rx_dropped++;
	}

	return new_skb;
}

/* ---------------------------- Receive data path ----------------------*/
static int sierra_net_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	int err;
	struct hip_hdr  hh;
	struct sk_buff *new_skb;

	dev_dbg(&dev->udev->dev, "%s", __func__);

	/* could contain multiple packets */
	while (likely(skb->len)) {
		err = parse_hip(skb->data, skb->len, &hh);
		if (err) {
			if (netif_msg_rx_err(dev))
				netdev_err(dev->net, "Invalid HIP header %d\n",
					err);
			/* dev->net->stats.rx_errors incremented by caller */
			dev->net->stats.rx_length_errors++;
			return 0;
		}

		/* Validate Extended HIP header */
		if (!hh.extmsgid.is_present
		    || hh.extmsgid.word != SIERRA_NET_HIP_EXT_IP_IN_ID) {
			if (netif_msg_rx_err(dev))
				netdev_err(dev->net, "HIP/ETH: Invalid pkt\n");

			dev->net->stats.rx_frame_errors++;
			/* dev->net->stats.rx_errors incremented by caller */
			return 0;
		}

		skb_pull(skb, hh.hdrlen);

		/* We are going to accept this packet, prepare it.
		 * In case protocol is IPv6, keep it, otherwise force IPv4.
		 */
		skb_reset_mac_header(skb);
		if (eth_hdr(skb)->h_proto != cpu_to_be16(ETH_P_IPV6))
			eth_hdr(skb)->h_proto = cpu_to_be16(ETH_P_IP);
		eth_zero_addr(eth_hdr(skb)->h_source);
		memcpy(eth_hdr(skb)->h_dest, dev->net->dev_addr, ETH_ALEN);

		/* Last packet in batch handled by usbnet */
		if (hh.payload_len.word == skb->len)
			return 1;

		new_skb = sierra_net_skb_clone(dev, skb, hh.payload_len.word);
		if (new_skb)
			usbnet_skb_return(dev, new_skb);

	} /* while */

	return 0;
}

/* ---------------------------- Transmit data path ----------------------*/
static struct sk_buff *sierra_net_tx_fixup(struct usbnet *dev,
					   struct sk_buff *skb, gfp_t flags)
{
	struct sierra_net_data *priv = sierra_net_get_private(dev);
	u16 len;
	bool need_tail;

	BUILD_BUG_ON(FIELD_SIZEOF(struct usbnet, data)
				< sizeof(struct cdc_state));

	dev_dbg(&dev->udev->dev, "%s", __func__);
	if (priv->link_up && check_ethip_packet(skb, dev) && is_ip(skb)) {
		/* enough head room as is? */
		if (SIERRA_NET_HIP_EXT_HDR_LEN <= skb_headroom(skb)) {
			/* Save the Eth/IP length and set up HIP hdr */
			len = skb->len;
			skb_push(skb, SIERRA_NET_HIP_EXT_HDR_LEN);
			/* Handle ZLP issue */
			need_tail = ((len + SIERRA_NET_HIP_EXT_HDR_LEN)
				% dev->maxpacket == 0);
			if (need_tail) {
				if (unlikely(skb_tailroom(skb) == 0)) {
					netdev_err(dev->net, "tx_fixup:"
						"no room for packet\n");
					dev_kfree_skb_any(skb);
					return NULL;
				} else {
					skb->data[skb->len] = 0;
					__skb_put(skb, 1);
					len = len + 1;
				}
			}
			build_hip(skb->data, len, priv);
			return skb;
		} else {
			/*
			 * compensate in the future if necessary
			 */
			netdev_err(dev->net, "tx_fixup: no room for HIP\n");
		} /* headroom */
	}

	if (!priv->link_up)
		dev->net->stats.tx_carrier_errors++;

	/* tx_dropped incremented by usbnet */

	/* filter the packet out, release it  */
	dev_kfree_skb_any(skb);
	return NULL;
}

static const struct driver_info sierra_net_info_direct_ip = {
	.description = "Sierra Wireless USB-to-WWAN Modem",
	.flags = FLAG_WWAN | FLAG_SEND_ZLP,
	.bind = sierra_net_bind,
	.unbind = sierra_net_unbind,
	.status = sierra_net_status,
	.rx_fixup = sierra_net_rx_fixup,
	.tx_fixup = sierra_net_tx_fixup,
};

static int
sierra_net_probe(struct usb_interface *udev, const struct usb_device_id *prod)
{
	int ret;

	ret = usbnet_probe(udev, prod);
	if (ret == 0) {
		struct usbnet *dev = usb_get_intfdata(udev);

		ret = usbnet_status_start(dev, GFP_KERNEL);
		if (ret == 0) {
			/* Interrupt URB now set up; initiate sync sequence */
			sierra_net_dosync(dev);
		}
	}
	return ret;
}

#define DIRECT_IP_DEVICE(vend, prod) \
	{USB_DEVICE_INTERFACE_NUMBER(vend, prod, 7), \
	.driver_info = (unsigned long)&sierra_net_info_direct_ip}, \
	{USB_DEVICE_INTERFACE_NUMBER(vend, prod, 10), \
	.driver_info = (unsigned long)&sierra_net_info_direct_ip}, \
	{USB_DEVICE_INTERFACE_NUMBER(vend, prod, 11), \
	.driver_info = (unsigned long)&sierra_net_info_direct_ip}

static const struct usb_device_id products[] = {
	DIRECT_IP_DEVICE(0x1199, 0x68A3), /* Sierra Wireless USB-to-WWAN modem */
	DIRECT_IP_DEVICE(0x0F3D, 0x68A3), /* AT&T Direct IP modem */
	DIRECT_IP_DEVICE(0x1199, 0x68AA), /* Sierra Wireless Direct IP LTE modem */
	DIRECT_IP_DEVICE(0x0F3D, 0x68AA), /* AT&T Direct IP LTE modem */

	{}, /* last item */
};
MODULE_DEVICE_TABLE(usb, products);

/* We are based on usbnet, so let it handle the USB driver specifics */
static struct usb_driver sierra_net_driver = {
	.name = "sierra_net",
	.id_table = products,
	.probe = sierra_net_probe,
	.disconnect = usbnet_disconnect,
	.suspend = usbnet_suspend,
	.resume = usbnet_resume,
	.no_dynamic_id = 1,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(sierra_net_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
