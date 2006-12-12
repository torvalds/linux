/*
 * USB Networking Link Interface
 *
 * Copyright (C) 2000-2005 by David Brownell <dbrownell@users.sourceforge.net>
 * Copyright (C) 2003-2005 David Hollis <dhollis@davehollis.com>
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


#ifndef	__USBNET_H
#define	__USBNET_H


/* interface from usbnet core to each USB networking link we handle */
struct usbnet {
	/* housekeeping */
	struct usb_device	*udev;
	struct driver_info	*driver_info;
	wait_queue_head_t	*wait;
	struct mutex		phy_mutex;

	/* i/o info: pipes etc */
	unsigned		in, out;
	struct usb_host_endpoint *status;
	unsigned		maxpacket;
	struct timer_list	delay;

	/* protocol/interface state */
	struct net_device	*net;
	struct net_device_stats	stats;
	int			msg_enable;
	unsigned long		data [5];
	u32			xid;
	u32			hard_mtu;	/* count any extra framing */
	size_t		        rx_urb_size;    /* size for rx urbs  */
	struct mii_if_info	mii;

	/* various kinds of pending driver work */
	struct sk_buff_head	rxq;
	struct sk_buff_head	txq;
	struct sk_buff_head	done;
	struct urb		*interrupt;
	struct tasklet_struct	bh;

	struct work_struct	kevent;
	unsigned long		flags;
#		define EVENT_TX_HALT	0
#		define EVENT_RX_HALT	1
#		define EVENT_RX_MEMORY	2
#		define EVENT_STS_SPLIT	3
#		define EVENT_LINK_RESET	4
};

static inline struct usb_driver *driver_of(struct usb_interface *intf)
{
	return to_usb_driver(intf->dev.driver);
}

/* interface from the device/framing level "minidriver" to core */
struct driver_info {
	char		*description;

	int		flags;
/* framing is CDC Ethernet, not writing ZLPs (hw issues), or optionally: */
#define FLAG_FRAMING_NC	0x0001		/* guard against device dropouts */
#define FLAG_FRAMING_GL	0x0002		/* genelink batches packets */
#define FLAG_FRAMING_Z	0x0004		/* zaurus adds a trailer */
#define FLAG_FRAMING_RN	0x0008		/* RNDIS batches, plus huge header */

#define FLAG_NO_SETINT	0x0010		/* device can't set_interface() */
#define FLAG_ETHER	0x0020		/* maybe use "eth%d" names */

#define FLAG_FRAMING_AX 0x0040          /* AX88772/178 packets */

	/* init device ... can sleep, or cause probe() failure */
	int	(*bind)(struct usbnet *, struct usb_interface *);

	/* cleanup device ... can sleep, but can't fail */
	void	(*unbind)(struct usbnet *, struct usb_interface *);

	/* reset device ... can sleep */
	int	(*reset)(struct usbnet *);

	/* see if peer is connected ... can sleep */
	int	(*check_connect)(struct usbnet *);

	/* for status polling */
	void	(*status)(struct usbnet *, struct urb *);

	/* link reset handling, called from defer_kevent */
	int	(*link_reset)(struct usbnet *);

	/* fixup rx packet (strip framing) */
	int	(*rx_fixup)(struct usbnet *dev, struct sk_buff *skb);

	/* fixup tx packet (add framing) */
	struct sk_buff	*(*tx_fixup)(struct usbnet *dev,
				struct sk_buff *skb, gfp_t flags);

	/* for new devices, use the descriptor-reading code instead */
	int		in;		/* rx endpoint */
	int		out;		/* tx endpoint */

	unsigned long	data;		/* Misc driver specific data */
};

/* Minidrivers are just drivers using the "usbnet" core as a powerful
 * network-specific subroutine library ... that happens to do pretty
 * much everything except custom framing and chip-specific stuff.
 */
extern int usbnet_probe(struct usb_interface *, const struct usb_device_id *);
extern int usbnet_suspend (struct usb_interface *, pm_message_t );
extern int usbnet_resume (struct usb_interface *);
extern void usbnet_disconnect(struct usb_interface *);


/* Drivers that reuse some of the standard USB CDC infrastructure
 * (notably, using multiple interfaces according to the the CDC
 * union descriptor) get some helper code.
 */
struct cdc_state {
	struct usb_cdc_header_desc	*header;
	struct usb_cdc_union_desc	*u;
	struct usb_cdc_ether_desc	*ether;
	struct usb_interface		*control;
	struct usb_interface		*data;
};

extern int usbnet_generic_cdc_bind (struct usbnet *, struct usb_interface *);
extern void usbnet_cdc_unbind (struct usbnet *, struct usb_interface *);

/* CDC and RNDIS support the same host-chosen packet filters for IN transfers */
#define	DEFAULT_FILTER	(USB_CDC_PACKET_TYPE_BROADCAST \
 			|USB_CDC_PACKET_TYPE_ALL_MULTICAST \
 			|USB_CDC_PACKET_TYPE_PROMISCUOUS \
 			|USB_CDC_PACKET_TYPE_DIRECTED)


/* we record the state for each of our queued skbs */
enum skb_state {
	illegal = 0,
	tx_start, tx_done,
	rx_start, rx_done, rx_cleanup
};

struct skb_data {	/* skb->cb is one of these */
	struct urb		*urb;
	struct usbnet		*dev;
	enum skb_state		state;
	size_t			length;
};


extern int usbnet_get_endpoints(struct usbnet *, struct usb_interface *);
extern void usbnet_defer_kevent (struct usbnet *, int);
extern void usbnet_skb_return (struct usbnet *, struct sk_buff *);
extern void usbnet_unlink_rx_urbs(struct usbnet *);

extern int usbnet_get_settings (struct net_device *net, struct ethtool_cmd *cmd);
extern int usbnet_set_settings (struct net_device *net, struct ethtool_cmd *cmd);
extern u32 usbnet_get_link (struct net_device *net);
extern u32 usbnet_get_msglevel (struct net_device *);
extern void usbnet_set_msglevel (struct net_device *, u32);
extern void usbnet_get_drvinfo (struct net_device *, struct ethtool_drvinfo *);
extern int usbnet_nway_reset(struct net_device *net);

/* messaging support includes the interface name, so it must not be
 * used before it has one ... notably, in minidriver bind() calls.
 */
#ifdef DEBUG
#define devdbg(usbnet, fmt, arg...) \
	printk(KERN_DEBUG "%s: " fmt "\n" , (usbnet)->net->name , ## arg)
#else
#define devdbg(usbnet, fmt, arg...) do {} while(0)
#endif

#define deverr(usbnet, fmt, arg...) \
	printk(KERN_ERR "%s: " fmt "\n" , (usbnet)->net->name , ## arg)
#define devwarn(usbnet, fmt, arg...) \
	printk(KERN_WARNING "%s: " fmt "\n" , (usbnet)->net->name , ## arg)

#define devinfo(usbnet, fmt, arg...) \
	printk(KERN_INFO "%s: " fmt "\n" , (usbnet)->net->name , ## arg); \


#endif	/* __USBNET_H */
