/*
 * USB Networking Links
 * Copyright (C) 2000-2005 by David Brownell <dbrownell@users.sourceforge.net>
 * Copyright (C) 2002 Pavel Machek <pavel@ucw.cz>
 * Copyright (C) 2003-2005 David Hollis <dhollis@davehollis.com>
 * Copyright (C) 2005 Phil Chang <pchang23@sbcglobal.net>
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

/*
 * This is a generic "USB networking" framework that works with several
 * kinds of full and high speed networking devices:
 *
 *   + USB host-to-host "network cables", used for IP-over-USB links.
 *     These are often used for Laplink style connectivity products.
 *	- AnchorChip 2720
 *	- Belkin, eTEK (interops with Win32 drivers)
 *	- GeneSys GL620USB-A
 *	- NetChip 1080 (interoperates with NetChip Win32 drivers)
 *	- Prolific PL-2301/2302 (replaces "plusb" driver)
 *	- KC Technology KC2190
 *
 *   + Smart USB devices can support such links directly, using Internet
 *     standard protocols instead of proprietary host-to-device links.
 *	- Linux PDAs like iPaq, Yopy, and Zaurus
 *	- The BLOB boot loader (for diskless booting)
 *	- Linux "gadgets", perhaps using PXA-2xx or Net2280 controllers
 *	- Devices using EPSON's sample USB firmware
 *	- CDC-Ethernet class devices, such as many cable modems
 *
 *   + Adapters to networks such as Ethernet.
 *	- AX8817X based USB 2.0 products
 *
 * Links to these devices can be bridged using Linux Ethernet bridging.
 * With minor exceptions, these all use similar USB framing for network
 * traffic, but need different protocols for control traffic.
 *
 * USB devices can implement their side of this protocol at the cost
 * of two bulk endpoints; it's not restricted to "cable" applications.
 * See the SA1110, Zaurus, or EPSON device/client support in this driver;
 * slave/target drivers such as "usb-eth" (on most SA-1100 PDAs) or
 * "g_ether" (in the Linux "gadget" framework) implement that behavior
 * within devices.
 *
 *
 * CHANGELOG:
 *
 * 13-sep-2000	experimental, new
 * 10-oct-2000	usb_device_id table created. 
 * 28-oct-2000	misc fixes; mostly, discard more TTL-mangled rx packets.
 * 01-nov-2000	usb_device_id table and probing api update by
 *		Adam J. Richter <adam@yggdrasil.com>.
 * 18-dec-2000	(db) tx watchdog, "net1080" renaming to "usbnet", device_info
 *		and prolific support, isolate net1080-specific bits, cleanup.
 *		fix unlink_urbs oops in D3 PM resume code path.
 *
 * 02-feb-2001	(db) fix tx skb sharing, packet length, match_flags, ...
 * 08-feb-2001	stubbed in "linuxdev", maybe the SA-1100 folk can use it;
 *		AnchorChips 2720 support (from spec) for testing;
 *		fix bit-ordering problem with ethernet multicast addr
 * 19-feb-2001  Support for clearing halt conditions. SA1100 UDC support
 *		updates. Oleg Drokin (green@iXcelerator.com)
 * 25-mar-2001	More SA-1100 updates, including workaround for ip problem
 *		expecting cleared skb->cb and framing change to match latest
 *		handhelds.org version (Oleg).  Enable device IDs from the
 *		Win32 Belkin driver; other cleanups (db).
 * 16-jul-2001	Bugfixes for uhci oops-on-unplug, Belkin support, various
 *		cleanups for problems not yet seen in the field. (db)
 * 17-oct-2001	Handle "Advance USBNET" product, like Belkin/eTEK devices,
 *		from Ioannis Mavroukakis <i.mavroukakis@btinternet.com>;
 *		rx unlinks somehow weren't async; minor cleanup.
 * 03-nov-2001	Merged GeneSys driver; original code from Jiun-Jie Huang
 *		<huangjj@genesyslogic.com.tw>, updated by Stanislav Brabec
 *		<utx@penguin.cz>.  Made framing options (NetChip/GeneSys)
 *		tie mostly to (sub)driver info.  Workaround some PL-2302
 *		chips that seem to reject SET_INTERFACE requests.
 *
 * 06-apr-2002	Added ethtool support, based on a patch from Brad Hards.
 *		Level of diagnostics is more configurable; they use device
 *		location (usb_device->devpath) instead of address (2.5).
 *		For tx_fixup, memflags can't be NOIO.
 * 07-may-2002	Generalize/cleanup keventd support, handling rx stalls (mostly
 *		for USB 2.0 TTs) and memory shortages (potential) too. (db)
 *		Use "locally assigned" IEEE802 address space. (Brad Hards)
 * 18-oct-2002	Support for Zaurus (Pavel Machek), related cleanup (db).
 * 14-dec-2002	Remove Zaurus-private crc32 code (Pavel); 2.5 oops fix,
 * 		cleanups and stubbed PXA-250 support (db), fix for framing
 * 		issues on Z, net1080, and gl620a (Toby Milne)
 *
 * 31-mar-2003	Use endpoint descriptors:  high speed support, simpler sa1100
 * 		vs pxa25x, and CDC Ethernet.  Throttle down log floods on
 * 		disconnect; other cleanups. (db)  Flush net1080 fifos
 * 		after several sequential framing errors. (Johannes Erdfelt)
 * 22-aug-2003	AX8817X support (Dave Hollis).
 * 14-jun-2004  Trivial patch for AX8817X based Buffalo LUA-U2-KTX in Japan
 *		(Neil Bortnak)
 * 03-nov-2004	Trivial patch for KC2190 (KC-190) chip. (Jonathan McDowell)
 *
 * 01-feb-2005	AX88772 support (Phil Chang & Dave Hollis)
 *-------------------------------------------------------------------------*/

// #define	DEBUG			// error path messages, extra info
// #define	VERBOSE			// more; success messages

#include <linux/config.h>
#ifdef	CONFIG_USB_DEBUG
#   define DEBUG
#endif
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/random.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mii.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <linux/usb.h>
#include <asm/io.h>
#include <asm/scatterlist.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>

#define DRIVER_VERSION		"03-Nov-2004"


/*-------------------------------------------------------------------------*/

/*
 * Nineteen USB 1.1 max size bulk transactions per frame (ms), max.
 * Several dozen bytes of IPv4 data can fit in two such transactions.
 * One maximum size Ethernet packet takes twenty four of them.
 * For high speed, each frame comfortably fits almost 36 max size
 * Ethernet packets (so queues should be bigger).
 */
#define	RX_QLEN(dev) (((dev)->udev->speed == USB_SPEED_HIGH) ? 60 : 4)
#define	TX_QLEN(dev) (((dev)->udev->speed == USB_SPEED_HIGH) ? 60 : 4)

// packets are always ethernet inside
// ... except they can be bigger (limit of 64K with NetChip framing)
#define MIN_PACKET	sizeof(struct ethhdr)
#define MAX_PACKET	32768

// reawaken network queue this soon after stopping; else watchdog barks
#define TX_TIMEOUT_JIFFIES	(5*HZ)

// throttle rx/tx briefly after some faults, so khubd might disconnect()
// us (it polls at HZ/4 usually) before we report too many false errors.
#define THROTTLE_JIFFIES	(HZ/8)

// for vendor-specific control operations
#define	CONTROL_TIMEOUT_MS	500

// between wakeups
#define UNLINK_TIMEOUT_MS	3

/*-------------------------------------------------------------------------*/

// randomly generated ethernet address
static u8	node_id [ETH_ALEN];

// state we keep for each device we handle
struct usbnet {
	// housekeeping
	struct usb_device	*udev;
	struct driver_info	*driver_info;
	wait_queue_head_t	*wait;

	// i/o info: pipes etc
	unsigned		in, out;
	struct usb_host_endpoint *status;
	unsigned		maxpacket;
	struct timer_list	delay;

	// protocol/interface state
	struct net_device	*net;
	struct net_device_stats	stats;
	int			msg_enable;
	unsigned long		data [5];

	struct mii_if_info	mii;

	// various kinds of pending driver work
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

// device-specific info used by the driver
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
				struct sk_buff *skb, int flags);

	// FIXME -- also an interrupt mechanism
	// useful for at least PL2301/2302 and GL620USB-A
	// and CDC use them to report 'is it connected' changes

	/* for new devices, use the descriptor-reading code instead */
	int		in;		/* rx endpoint */
	int		out;		/* tx endpoint */

	unsigned long	data;		/* Misc driver specific data */
};

// we record the state for each of our queued skbs
enum skb_state {
	illegal = 0,
	tx_start, tx_done,
	rx_start, rx_done, rx_cleanup
};

struct skb_data {	// skb->cb is one of these
	struct urb		*urb;
	struct usbnet		*dev;
	enum skb_state		state;
	size_t			length;
};

static const char driver_name [] = "usbnet";

/* use ethtool to change the level for any given device */
static int msg_level = -1;
module_param (msg_level, int, 0);
MODULE_PARM_DESC (msg_level, "Override default message level");


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

/*-------------------------------------------------------------------------*/

static void usbnet_get_drvinfo (struct net_device *, struct ethtool_drvinfo *);
static u32 usbnet_get_link (struct net_device *);
static u32 usbnet_get_msglevel (struct net_device *);
static void usbnet_set_msglevel (struct net_device *, u32);
static void defer_kevent (struct usbnet *, int);

/* mostly for PDA style devices, which are always connected if present */
static int always_connected (struct usbnet *dev)
{
	return 0;
}

/* handles CDC Ethernet and many other network "bulk data" interfaces */
static int
get_endpoints (struct usbnet *dev, struct usb_interface *intf)
{
	int				tmp;
	struct usb_host_interface	*alt = NULL;
	struct usb_host_endpoint	*in = NULL, *out = NULL;
	struct usb_host_endpoint	*status = NULL;

	for (tmp = 0; tmp < intf->num_altsetting; tmp++) {
		unsigned	ep;

		in = out = status = NULL;
		alt = intf->altsetting + tmp;

		/* take the first altsetting with in-bulk + out-bulk;
		 * remember any status endpoint, just in case;
		 * ignore other endpoints and altsetttings.
		 */
		for (ep = 0; ep < alt->desc.bNumEndpoints; ep++) {
			struct usb_host_endpoint	*e;
			int				intr = 0;

			e = alt->endpoint + ep;
			switch (e->desc.bmAttributes) {
			case USB_ENDPOINT_XFER_INT:
				if (!(e->desc.bEndpointAddress & USB_DIR_IN))
					continue;
				intr = 1;
				/* FALLTHROUGH */
			case USB_ENDPOINT_XFER_BULK:
				break;
			default:
				continue;
			}
			if (e->desc.bEndpointAddress & USB_DIR_IN) {
				if (!intr && !in)
					in = e;
				else if (intr && !status)
					status = e;
			} else {
				if (!out)
					out = e;
			}
		}
		if (in && out)
			break;
	}
	if (!alt || !in || !out)
		return -EINVAL;

	if (alt->desc.bAlternateSetting != 0
			|| !(dev->driver_info->flags & FLAG_NO_SETINT)) {
		tmp = usb_set_interface (dev->udev, alt->desc.bInterfaceNumber,
				alt->desc.bAlternateSetting);
		if (tmp < 0)
			return tmp;
	}
	
	dev->in = usb_rcvbulkpipe (dev->udev,
			in->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	dev->out = usb_sndbulkpipe (dev->udev,
			out->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	dev->status = status;
	return 0;
}

static void intr_complete (struct urb *urb, struct pt_regs *regs);

static int init_status (struct usbnet *dev, struct usb_interface *intf)
{
	char		*buf = NULL;
	unsigned	pipe = 0;
	unsigned	maxp;
	unsigned	period;

	if (!dev->driver_info->status)
		return 0;

	pipe = usb_rcvintpipe (dev->udev,
			dev->status->desc.bEndpointAddress
				& USB_ENDPOINT_NUMBER_MASK);
	maxp = usb_maxpacket (dev->udev, pipe, 0);

	/* avoid 1 msec chatter:  min 8 msec poll rate */
	period = max ((int) dev->status->desc.bInterval,
		(dev->udev->speed == USB_SPEED_HIGH) ? 7 : 3);

	buf = kmalloc (maxp, SLAB_KERNEL);
	if (buf) {
		dev->interrupt = usb_alloc_urb (0, SLAB_KERNEL);
		if (!dev->interrupt) {
			kfree (buf);
			return -ENOMEM;
		} else {
			usb_fill_int_urb(dev->interrupt, dev->udev, pipe,
				buf, maxp, intr_complete, dev, period);
			dev_dbg(&intf->dev,
				"status ep%din, %d bytes period %d\n",
				usb_pipeendpoint(pipe), maxp, period);
		}
	}
	return  0;
}

static void skb_return (struct usbnet *dev, struct sk_buff *skb)
{
	int	status;

	skb->dev = dev->net;
	skb->protocol = eth_type_trans (skb, dev->net);
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += skb->len;

	if (netif_msg_rx_status (dev))
		devdbg (dev, "< rx, len %zu, type 0x%x",
			skb->len + sizeof (struct ethhdr), skb->protocol);
	memset (skb->cb, 0, sizeof (struct skb_data));
	status = netif_rx (skb);
	if (status != NET_RX_SUCCESS && netif_msg_rx_err (dev))
		devdbg (dev, "netif_rx status %d", status);
}


#ifdef	CONFIG_USB_ALI_M5632
#define	HAVE_HARDWARE

/*-------------------------------------------------------------------------
 *
 * ALi M5632 driver ... does high speed
 *
 *-------------------------------------------------------------------------*/

static const struct driver_info	ali_m5632_info = {
	.description =	"ALi M5632",
};


#endif


#ifdef	CONFIG_USB_AN2720
#define	HAVE_HARDWARE

/*-------------------------------------------------------------------------
 *
 * AnchorChips 2720 driver ... http://www.cypress.com
 *
 * This doesn't seem to have a way to detect whether the peer is
 * connected, or need any reset handshaking.  It's got pretty big
 * internal buffers (handles most of a frame's worth of data).
 * Chip data sheets don't describe any vendor control messages.
 *
 *-------------------------------------------------------------------------*/

static const struct driver_info	an2720_info = {
	.description =	"AnchorChips/Cypress 2720",
	// no reset available!
	// no check_connect available!

	.in = 2, .out = 2,		// direction distinguishes these
};

#endif	/* CONFIG_USB_AN2720 */


#ifdef CONFIG_USB_AX8817X
/* ASIX AX8817X based USB 2.0 Ethernet Devices */

#define HAVE_HARDWARE
#define NEED_MII

#include <linux/crc32.h>

#define AX_CMD_SET_SW_MII		0x06
#define AX_CMD_READ_MII_REG		0x07
#define AX_CMD_WRITE_MII_REG		0x08
#define AX_CMD_SET_HW_MII		0x0a
#define AX_CMD_READ_EEPROM		0x0b
#define AX_CMD_WRITE_EEPROM		0x0c
#define AX_CMD_WRITE_ENABLE		0x0d
#define AX_CMD_WRITE_DISABLE		0x0e
#define AX_CMD_WRITE_RX_CTL		0x10
#define AX_CMD_READ_IPG012		0x11
#define AX_CMD_WRITE_IPG0		0x12
#define AX_CMD_WRITE_IPG1		0x13
#define AX_CMD_WRITE_IPG2		0x14
#define AX_CMD_WRITE_MULTI_FILTER	0x16
#define AX_CMD_READ_NODE_ID		0x17
#define AX_CMD_READ_PHY_ID		0x19
#define AX_CMD_READ_MEDIUM_STATUS	0x1a
#define AX_CMD_WRITE_MEDIUM_MODE	0x1b
#define AX_CMD_READ_MONITOR_MODE	0x1c
#define AX_CMD_WRITE_MONITOR_MODE	0x1d
#define AX_CMD_WRITE_GPIOS		0x1f
#define AX_CMD_SW_RESET			0x20
#define AX_CMD_SW_PHY_STATUS		0x21
#define AX_CMD_SW_PHY_SELECT		0x22
#define AX88772_CMD_READ_NODE_ID	0x13

#define AX_MONITOR_MODE			0x01
#define AX_MONITOR_LINK			0x02
#define AX_MONITOR_MAGIC		0x04
#define AX_MONITOR_HSFS			0x10

/* AX88172 Medium Status Register values */
#define AX_MEDIUM_FULL_DUPLEX		0x02
#define AX_MEDIUM_TX_ABORT_ALLOW	0x04
#define AX_MEDIUM_FLOW_CONTROL_EN	0x10

#define AX_MCAST_FILTER_SIZE		8
#define AX_MAX_MCAST			64

#define AX_EEPROM_LEN			0x40

#define AX_SWRESET_CLEAR		0x00
#define AX_SWRESET_RR			0x01
#define AX_SWRESET_RT			0x02
#define AX_SWRESET_PRTE			0x04
#define AX_SWRESET_PRL			0x08
#define AX_SWRESET_BZ			0x10
#define AX_SWRESET_IPRL			0x20
#define AX_SWRESET_IPPD			0x40

#define AX88772_IPG0_DEFAULT		0x15
#define AX88772_IPG1_DEFAULT		0x0c
#define AX88772_IPG2_DEFAULT		0x12

#define AX88772_MEDIUM_FULL_DUPLEX	0x0002
#define AX88772_MEDIUM_RESERVED		0x0004
#define AX88772_MEDIUM_RX_FC_ENABLE	0x0010
#define AX88772_MEDIUM_TX_FC_ENABLE	0x0020
#define AX88772_MEDIUM_PAUSE_FORMAT	0x0080
#define AX88772_MEDIUM_RX_ENABLE	0x0100
#define AX88772_MEDIUM_100MB		0x0200
#define AX88772_MEDIUM_DEFAULT	\
	(AX88772_MEDIUM_FULL_DUPLEX | AX88772_MEDIUM_RX_FC_ENABLE | \
	 AX88772_MEDIUM_TX_FC_ENABLE | AX88772_MEDIUM_100MB | \
	 AX88772_MEDIUM_RESERVED | AX88772_MEDIUM_RX_ENABLE )

#define AX_EEPROM_MAGIC			0xdeadbeef

/* This structure cannot exceed sizeof(unsigned long [5]) AKA 20 bytes */
struct ax8817x_data {
	u8 multi_filter[AX_MCAST_FILTER_SIZE];
};

struct ax88172_int_data {
	u16 res1;
	u8 link;
	u16 res2;
	u8 status;
	u16 res3;
} __attribute__ ((packed));

static int ax8817x_read_cmd(struct usbnet *dev, u8 cmd, u16 value, u16 index,
			    u16 size, void *data)
{
	return usb_control_msg(
		dev->udev,
		usb_rcvctrlpipe(dev->udev, 0),
		cmd,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		value,
		index,
		data,
		size,
		CONTROL_TIMEOUT_MS);
}

static int ax8817x_write_cmd(struct usbnet *dev, u8 cmd, u16 value, u16 index,
			     u16 size, void *data)
{
	return usb_control_msg(
		dev->udev,
		usb_sndctrlpipe(dev->udev, 0),
		cmd,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		value,
		index,
		data,
		size,
		CONTROL_TIMEOUT_MS);
}

static void ax8817x_async_cmd_callback(struct urb *urb, struct pt_regs *regs)
{
	struct usb_ctrlrequest *req = (struct usb_ctrlrequest *)urb->context;

	if (urb->status < 0)
		printk(KERN_DEBUG "ax8817x_async_cmd_callback() failed with %d",
			urb->status);

	kfree(req);
	usb_free_urb(urb);
}

static void ax8817x_status(struct usbnet *dev, struct urb *urb)
{
	struct ax88172_int_data *event;
	int link;

	if (urb->actual_length < 8)
		return;

	event = urb->transfer_buffer;
	link = event->link & 0x01;
	if (netif_carrier_ok(dev->net) != link) {
		if (link) {
			netif_carrier_on(dev->net);
			defer_kevent (dev, EVENT_LINK_RESET );
		} else
			netif_carrier_off(dev->net);
		devdbg(dev, "ax8817x - Link Status is: %d", link);
	}
}

static void ax8817x_write_cmd_async(struct usbnet *dev, u8 cmd, u16 value, u16 index,
				    u16 size, void *data)
{
	struct usb_ctrlrequest *req;
	int status;
	struct urb *urb;

	if ((urb = usb_alloc_urb(0, GFP_ATOMIC)) == NULL) {
		devdbg(dev, "Error allocating URB in write_cmd_async!");
		return;
	}

	if ((req = kmalloc(sizeof(struct usb_ctrlrequest), GFP_ATOMIC)) == NULL) {
		deverr(dev, "Failed to allocate memory for control request");
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
			     ax8817x_async_cmd_callback, req);

	if((status = usb_submit_urb(urb, GFP_ATOMIC)) < 0) {
		deverr(dev, "Error submitting the control message: status=%d", status);
		kfree(req);
		usb_free_urb(urb);
	}
}

static void ax8817x_set_multicast(struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);
	struct ax8817x_data *data = (struct ax8817x_data *)&dev->data;
	u8 rx_ctl = 0x8c;

	if (net->flags & IFF_PROMISC) {
		rx_ctl |= 0x01;
	} else if (net->flags & IFF_ALLMULTI
		   || net->mc_count > AX_MAX_MCAST) {
		rx_ctl |= 0x02;
	} else if (net->mc_count == 0) {
		/* just broadcast and directed */
	} else {
		/* We use the 20 byte dev->data
		 * for our 8 byte filter buffer
		 * to avoid allocating memory that
		 * is tricky to free later */
		struct dev_mc_list *mc_list = net->mc_list;
		u32 crc_bits;
		int i;

		memset(data->multi_filter, 0, AX_MCAST_FILTER_SIZE);

		/* Build the multicast hash filter. */
		for (i = 0; i < net->mc_count; i++) {
			crc_bits =
			    ether_crc(ETH_ALEN,
				      mc_list->dmi_addr) >> 26;
			data->multi_filter[crc_bits >> 3] |=
			    1 << (crc_bits & 7);
			mc_list = mc_list->next;
		}

		ax8817x_write_cmd_async(dev, AX_CMD_WRITE_MULTI_FILTER, 0, 0,
				   AX_MCAST_FILTER_SIZE, data->multi_filter);

		rx_ctl |= 0x10;
	}

	ax8817x_write_cmd_async(dev, AX_CMD_WRITE_RX_CTL, rx_ctl, 0, 0, NULL);
}

static int ax8817x_mdio_read(struct net_device *netdev, int phy_id, int loc)
{
	struct usbnet *dev = netdev_priv(netdev);
	u16 res;
	u8 buf[1];

	ax8817x_write_cmd(dev, AX_CMD_SET_SW_MII, 0, 0, 0, &buf);
	ax8817x_read_cmd(dev, AX_CMD_READ_MII_REG, phy_id, (__u16)loc, 2, (u16 *)&res);
	ax8817x_write_cmd(dev, AX_CMD_SET_HW_MII, 0, 0, 0, &buf);

	return res & 0xffff;
}

static void ax8817x_mdio_write(struct net_device *netdev, int phy_id, int loc, int val)
{
	struct usbnet *dev = netdev_priv(netdev);
	u16 res = val;
	u8 buf[1];

	ax8817x_write_cmd(dev, AX_CMD_SET_SW_MII, 0, 0, 0, &buf);
	ax8817x_write_cmd(dev, AX_CMD_WRITE_MII_REG, phy_id, (__u16)loc, 2, (u16 *)&res);
	ax8817x_write_cmd(dev, AX_CMD_SET_HW_MII, 0, 0, 0, &buf);
}

static int ax88172_link_reset(struct usbnet *dev)
{
	u16 lpa;
	u8 mode;

	mode = AX_MEDIUM_TX_ABORT_ALLOW | AX_MEDIUM_FLOW_CONTROL_EN;
	lpa = ax8817x_mdio_read(dev->net, dev->mii.phy_id, MII_LPA);
	if (lpa & LPA_DUPLEX)
		mode |= AX_MEDIUM_FULL_DUPLEX;
	ax8817x_write_cmd(dev, AX_CMD_WRITE_MEDIUM_MODE, mode, 0, 0, NULL);

	return 0;
}

static void ax8817x_get_wol(struct net_device *net, struct ethtool_wolinfo *wolinfo)
{
	struct usbnet *dev = netdev_priv(net);
	u8 opt;

	if (ax8817x_read_cmd(dev, AX_CMD_READ_MONITOR_MODE, 0, 0, 1, &opt) < 0) {
		wolinfo->supported = 0;
		wolinfo->wolopts = 0;
		return;
	}
	wolinfo->supported = WAKE_PHY | WAKE_MAGIC;
	wolinfo->wolopts = 0;
	if (opt & AX_MONITOR_MODE) {
		if (opt & AX_MONITOR_LINK)
			wolinfo->wolopts |= WAKE_PHY;
		if (opt & AX_MONITOR_MAGIC)
			wolinfo->wolopts |= WAKE_MAGIC;
	}
}

static int ax8817x_set_wol(struct net_device *net, struct ethtool_wolinfo *wolinfo)
{
	struct usbnet *dev = netdev_priv(net);
	u8 opt = 0;
	u8 buf[1];

	if (wolinfo->wolopts & WAKE_PHY)
		opt |= AX_MONITOR_LINK;
	if (wolinfo->wolopts & WAKE_MAGIC)
		opt |= AX_MONITOR_MAGIC;
	if (opt != 0)
		opt |= AX_MONITOR_MODE;

	if (ax8817x_write_cmd(dev, AX_CMD_WRITE_MONITOR_MODE,
			      opt, 0, 0, &buf) < 0)
		return -EINVAL;

	return 0;
}

static int ax8817x_get_eeprom_len(struct net_device *net)
{
	return AX_EEPROM_LEN;
}

static int ax8817x_get_eeprom(struct net_device *net,
			      struct ethtool_eeprom *eeprom, u8 *data)
{
	struct usbnet *dev = netdev_priv(net);
	u16 *ebuf = (u16 *)data;
	int i;

	/* Crude hack to ensure that we don't overwrite memory
	 * if an odd length is supplied
	 */
	if (eeprom->len % 2)
		return -EINVAL;

	eeprom->magic = AX_EEPROM_MAGIC;

	/* ax8817x returns 2 bytes from eeprom on read */
	for (i=0; i < eeprom->len / 2; i++) {
		if (ax8817x_read_cmd(dev, AX_CMD_READ_EEPROM, 
			eeprom->offset + i, 0, 2, &ebuf[i]) < 0)
			return -EINVAL;
	}
	return 0;
}

static void ax8817x_get_drvinfo (struct net_device *net,
				 struct ethtool_drvinfo *info)
{
	/* Inherit standard device info */
	usbnet_get_drvinfo(net, info);
	info->eedump_len = 0x3e;
}

static int ax8817x_get_settings(struct net_device *net, struct ethtool_cmd *cmd)
{
	struct usbnet *dev = netdev_priv(net);

	return mii_ethtool_gset(&dev->mii,cmd);
}

static int ax8817x_set_settings(struct net_device *net, struct ethtool_cmd *cmd)
{
	struct usbnet *dev = netdev_priv(net);

	return mii_ethtool_sset(&dev->mii,cmd);
}

/* We need to override some ethtool_ops so we require our
   own structure so we don't interfere with other usbnet
   devices that may be connected at the same time. */
static struct ethtool_ops ax8817x_ethtool_ops = {
	.get_drvinfo		= ax8817x_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_msglevel		= usbnet_get_msglevel,
	.set_msglevel		= usbnet_set_msglevel,
	.get_wol		= ax8817x_get_wol,
	.set_wol		= ax8817x_set_wol,
	.get_eeprom_len		= ax8817x_get_eeprom_len,
	.get_eeprom		= ax8817x_get_eeprom,
	.get_settings		= ax8817x_get_settings,
	.set_settings		= ax8817x_set_settings,
};

static int ax8817x_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int ret = 0;
	void *buf;
	int i;
	unsigned long gpio_bits = dev->driver_info->data;

	get_endpoints(dev,intf);

	buf = kmalloc(ETH_ALEN, GFP_KERNEL);
	if(!buf) {
		ret = -ENOMEM;
		goto out1;
	}

	/* Toggle the GPIOs in a manufacturer/model specific way */
	for (i = 2; i >= 0; i--) {
		if ((ret = ax8817x_write_cmd(dev, AX_CMD_WRITE_GPIOS,
					(gpio_bits >> (i * 8)) & 0xff, 0, 0,
					buf)) < 0)
			goto out2;
		msleep(5);
	}

	if ((ret = ax8817x_write_cmd(dev, AX_CMD_WRITE_RX_CTL, 0x80, 0, 0, buf)) < 0) {
		dbg("send AX_CMD_WRITE_RX_CTL failed: %d", ret);
		goto out2;
	}

	/* Get the MAC address */
	memset(buf, 0, ETH_ALEN);
	if ((ret = ax8817x_read_cmd(dev, AX_CMD_READ_NODE_ID, 0, 0, 6, buf)) < 0) {
		dbg("read AX_CMD_READ_NODE_ID failed: %d", ret);
		goto out2;
	}
	memcpy(dev->net->dev_addr, buf, ETH_ALEN);

	/* Get the PHY id */
	if ((ret = ax8817x_read_cmd(dev, AX_CMD_READ_PHY_ID, 0, 0, 2, buf)) < 0) {
		dbg("error on read AX_CMD_READ_PHY_ID: %02x", ret);
		goto out2;
	} else if (ret < 2) {
		/* this should always return 2 bytes */
		dbg("AX_CMD_READ_PHY_ID returned less than 2 bytes: ret=%02x", ret);
		ret = -EIO;
		goto out2;
	}

	/* Initialize MII structure */
	dev->mii.dev = dev->net;
	dev->mii.mdio_read = ax8817x_mdio_read;
	dev->mii.mdio_write = ax8817x_mdio_write;
	dev->mii.phy_id_mask = 0x3f;
	dev->mii.reg_num_mask = 0x1f;
	dev->mii.phy_id = *((u8 *)buf + 1);

	dev->net->set_multicast_list = ax8817x_set_multicast;
	dev->net->ethtool_ops = &ax8817x_ethtool_ops;

	ax8817x_mdio_write(dev->net, dev->mii.phy_id, MII_BMCR, BMCR_RESET);
	ax8817x_mdio_write(dev->net, dev->mii.phy_id, MII_ADVERTISE,
		ADVERTISE_ALL | ADVERTISE_CSMA | ADVERTISE_PAUSE_CAP);
	mii_nway_restart(&dev->mii);

	return 0;
out2:
	kfree(buf);
out1:
	return ret;
}

static struct ethtool_ops ax88772_ethtool_ops = {
	.get_drvinfo		= ax8817x_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_msglevel		= usbnet_get_msglevel,
	.set_msglevel		= usbnet_set_msglevel,
	.get_wol		= ax8817x_get_wol,
	.set_wol		= ax8817x_set_wol,
	.get_eeprom_len		= ax8817x_get_eeprom_len,
	.get_eeprom		= ax8817x_get_eeprom,
	.get_settings		= ax8817x_get_settings,
	.set_settings		= ax8817x_set_settings,
};

static int ax88772_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int ret;
	void *buf;

	get_endpoints(dev,intf);

	buf = kmalloc(6, GFP_KERNEL);
	if(!buf) {
		dbg ("Cannot allocate memory for buffer");
		ret = -ENOMEM;
		goto out1;
	}

	if ((ret = ax8817x_write_cmd(dev, AX_CMD_WRITE_GPIOS,
				     0x00B0, 0, 0, buf)) < 0)
		goto out2;

	msleep(5);
	if ((ret = ax8817x_write_cmd(dev, AX_CMD_SW_PHY_SELECT, 0x0001, 0, 0, buf)) < 0) {
		dbg("Select PHY #1 failed: %d", ret);
		goto out2;
	}

	if ((ret =
	     ax8817x_write_cmd(dev, AX_CMD_SW_RESET, AX_SWRESET_IPPD, 0, 0, buf)) < 0) {
		dbg("Failed to power down internal PHY: %d", ret);
		goto out2;
	}

	msleep(150);
	if ((ret =
	     ax8817x_write_cmd(dev, AX_CMD_SW_RESET, AX_SWRESET_CLEAR, 0, 0, buf)) < 0) {
		dbg("Failed to perform software reset: %d", ret);
		goto out2;
	}

	msleep(150);
	if ((ret =
	     ax8817x_write_cmd(dev, AX_CMD_SW_RESET, AX_SWRESET_IPRL | AX_SWRESET_PRL, 0, 0, buf)) < 0) {
		dbg("Failed to set Internal/External PHY reset control: %d", ret);
		goto out2;
	}

	msleep(150);
	if ((ret =
	     ax8817x_write_cmd(dev, AX_CMD_WRITE_RX_CTL, 0x0000, 0, 0,
			       buf)) < 0) {
		dbg("Failed to reset RX_CTL: %d", ret);
		goto out2;
	}

	/* Get the MAC address */
	memset(buf, 0, ETH_ALEN);
	if ((ret = ax8817x_read_cmd(dev, AX88772_CMD_READ_NODE_ID, 0, 0, ETH_ALEN, buf)) < 0) {
		dbg("Failed to read MAC address: %d", ret);
		goto out2;
	}
	memcpy(dev->net->dev_addr, buf, ETH_ALEN);

	if ((ret = ax8817x_write_cmd(dev, AX_CMD_SET_SW_MII, 0, 0, 0, buf)) < 0) {
		dbg("Enabling software MII failed: %d", ret);
		goto out2;
	}

	if (((ret =
	      ax8817x_read_cmd(dev, AX_CMD_READ_MII_REG, 0x0010, 2, 2, buf)) < 0)
	    || (*((u16 *)buf) != 0x003b)) {
		dbg("Read PHY register 2 must be 0x3b00: %d", ret);
		goto out2;
	}

	/* Initialize MII structure */
	dev->mii.dev = dev->net;
	dev->mii.mdio_read = ax8817x_mdio_read;
	dev->mii.mdio_write = ax8817x_mdio_write;
	dev->mii.phy_id_mask = 0xff;
	dev->mii.reg_num_mask = 0xff;

	/* Get the PHY id */
	if ((ret = ax8817x_read_cmd(dev, AX_CMD_READ_PHY_ID, 0, 0, 2, buf)) < 0) {
		dbg("Error reading PHY ID: %02x", ret);
		goto out2;
	} else if (ret < 2) {
		/* this should always return 2 bytes */
		dbg("AX_CMD_READ_PHY_ID returned less than 2 bytes: ret=%02x",
		    ret);
		ret = -EIO;
		goto out2;
	}
	dev->mii.phy_id = *((u8 *)buf + 1);

	if ((ret =
	     ax8817x_write_cmd(dev, AX_CMD_SW_RESET, AX_SWRESET_PRL, 0, 0, buf)) < 0) {
		dbg("Set external PHY reset pin level: %d", ret);
		goto out2;
	}
	msleep(150);
	if ((ret =
	     ax8817x_write_cmd(dev, AX_CMD_SW_RESET, AX_SWRESET_IPRL | AX_SWRESET_PRL, 0, 0, buf)) < 0) {
		dbg("Set Internal/External PHY reset control: %d", ret);
		goto out2;
	}
	msleep(150);


	dev->net->set_multicast_list = ax8817x_set_multicast;
	dev->net->ethtool_ops = &ax88772_ethtool_ops;

	ax8817x_mdio_write(dev->net, dev->mii.phy_id, MII_BMCR, BMCR_RESET);
	ax8817x_mdio_write(dev->net, dev->mii.phy_id, MII_ADVERTISE,
			ADVERTISE_ALL | ADVERTISE_CSMA);
	mii_nway_restart(&dev->mii);

	if ((ret = ax8817x_write_cmd(dev, AX_CMD_WRITE_MEDIUM_MODE, AX88772_MEDIUM_DEFAULT, 0, 0, buf)) < 0) {
		dbg("Write medium mode register: %d", ret);
		goto out2;
	}

	if ((ret = ax8817x_write_cmd(dev, AX_CMD_WRITE_IPG0, AX88772_IPG0_DEFAULT | AX88772_IPG1_DEFAULT,AX88772_IPG2_DEFAULT, 0, buf)) < 0) {
		dbg("Write IPG,IPG1,IPG2 failed: %d", ret);
		goto out2;
	}
	if ((ret =
	     ax8817x_write_cmd(dev, AX_CMD_SET_HW_MII, 0, 0, 0, &buf)) < 0) {
		dbg("Failed to set hardware MII: %02x", ret);
		goto out2;
	}

	/* Set RX_CTL to default values with 2k buffer, and enable cactus */
	if ((ret =
	     ax8817x_write_cmd(dev, AX_CMD_WRITE_RX_CTL, 0x0088, 0, 0,
			       buf)) < 0) {
		dbg("Reset RX_CTL failed: %d", ret);
		goto out2;
	}

	kfree(buf);

	return 0;

out2:
	kfree(buf);
out1:
	return ret;
}

static int ax88772_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	u32 *header;
	char *packet;
	struct sk_buff *ax_skb;
	u16 size;

	header = (u32 *) skb->data;
	le32_to_cpus(header);
	packet = (char *)(header + 1);

	skb_pull(skb, 4);

	while (skb->len > 0) {
		if ((short)(*header & 0x0000ffff) !=
		    ~((short)((*header & 0xffff0000) >> 16))) {
			devdbg(dev,"header length data is error");
		}
		/* get the packet length */
		size = (u16) (*header & 0x0000ffff);

		if ((skb->len) - ((size + 1) & 0xfffe) == 0)
			return 2;
		if (size > ETH_FRAME_LEN) {
			devdbg(dev,"invalid rx length %d", size);
			return 0;
		}
		ax_skb = skb_clone(skb, GFP_ATOMIC);
		if (ax_skb) {
			ax_skb->len = size;
			ax_skb->data = packet;
			ax_skb->tail = packet + size;
			skb_return(dev, ax_skb);
		} else {
			return 0;
		}

		skb_pull(skb, (size + 1) & 0xfffe);

		if (skb->len == 0)
			break;

		header = (u32 *) skb->data;
		le32_to_cpus(header);
		packet = (char *)(header + 1);
		skb_pull(skb, 4);
	}

	if (skb->len < 0) {
		devdbg(dev,"invalid rx length %d", skb->len);
		return 0;
	}
	return 1;
}

static struct sk_buff *ax88772_tx_fixup(struct usbnet *dev, struct sk_buff *skb,
					int flags)
{
	int padlen;
	int headroom = skb_headroom(skb);
	int tailroom = skb_tailroom(skb);
	u32 *packet_len;
	u32 *padbytes_ptr;

	padlen = ((skb->len + 4) % 512) ? 0 : 4;

	if ((!skb_cloned(skb))
	    && ((headroom + tailroom) >= (4 + padlen))) {
		if ((headroom < 4) || (tailroom < padlen)) {
			skb->data = memmove(skb->head + 4, skb->data, skb->len);
			skb->tail = skb->data + skb->len;
		}
	} else {
		struct sk_buff *skb2;
		skb2 = skb_copy_expand(skb, 4, padlen, flags);
		dev_kfree_skb_any(skb);
		skb = skb2;
		if (!skb)
			return NULL;
	}

	packet_len = (u32 *) skb_push(skb, 4);

	packet_len = (u32 *) skb->data;
	*packet_len = (((skb->len - 4) ^ 0x0000ffff) << 16) + (skb->len - 4);

	if ((skb->len % 512) == 0) {
		padbytes_ptr = (u32 *) skb->tail;
		*padbytes_ptr = 0xffff0000;
		skb_put(skb, padlen);
	}
	return skb;
}

static int ax88772_link_reset(struct usbnet *dev)
{
	u16 lpa;
	u16 mode;

	mode = AX88772_MEDIUM_DEFAULT;
	lpa = ax8817x_mdio_read(dev->net, dev->mii.phy_id, MII_LPA);

	if ((lpa & LPA_DUPLEX) == 0)
		mode &= ~AX88772_MEDIUM_FULL_DUPLEX;
	if ((lpa & LPA_100) == 0)
		mode &= ~AX88772_MEDIUM_100MB;
	ax8817x_write_cmd(dev, AX_CMD_WRITE_MEDIUM_MODE, mode, 0, 0, NULL);

	return 0;
}

static const struct driver_info ax8817x_info = {
	.description = "ASIX AX8817x USB 2.0 Ethernet",
	.bind = ax8817x_bind,
	.status = ax8817x_status,
	.link_reset = ax88172_link_reset,
	.reset = ax88172_link_reset,
	.flags =  FLAG_ETHER,
	.data = 0x00130103,
};

static const struct driver_info dlink_dub_e100_info = {
	.description = "DLink DUB-E100 USB Ethernet",
	.bind = ax8817x_bind,
	.status = ax8817x_status,
	.link_reset = ax88172_link_reset,
	.reset = ax88172_link_reset,
	.flags =  FLAG_ETHER,
	.data = 0x009f9d9f,
};

static const struct driver_info netgear_fa120_info = {
	.description = "Netgear FA-120 USB Ethernet",
	.bind = ax8817x_bind,
	.status = ax8817x_status,
	.link_reset = ax88172_link_reset,
	.reset = ax88172_link_reset,
	.flags =  FLAG_ETHER,
	.data = 0x00130103,
};

static const struct driver_info hawking_uf200_info = {
	.description = "Hawking UF200 USB Ethernet",
	.bind = ax8817x_bind,
	.status = ax8817x_status,
	.link_reset = ax88172_link_reset,
	.reset = ax88172_link_reset,
	.flags =  FLAG_ETHER,
	.data = 0x001f1d1f,
};

static const struct driver_info ax88772_info = {
	.description = "ASIX AX88772 USB 2.0 Ethernet",
	.bind = ax88772_bind,
	.status = ax8817x_status,
	.link_reset = ax88772_link_reset,
	.reset = ax88772_link_reset,
	.flags = FLAG_ETHER | FLAG_FRAMING_AX,
	.rx_fixup = ax88772_rx_fixup,
	.tx_fixup = ax88772_tx_fixup,
	.data = 0x00130103,
};

#endif /* CONFIG_USB_AX8817X */



#ifdef	CONFIG_USB_BELKIN
#define	HAVE_HARDWARE

/*-------------------------------------------------------------------------
 *
 * Belkin F5U104 ... two NetChip 2280 devices + Atmel microcontroller
 *
 * ... also two eTEK designs, including one sold as "Advance USBNET"
 *
 *-------------------------------------------------------------------------*/

static const struct driver_info	belkin_info = {
	.description =	"Belkin, eTEK, or compatible",
};

#endif	/* CONFIG_USB_BELKIN */



/*-------------------------------------------------------------------------
 *
 * Communications Device Class declarations.
 * Used by CDC Ethernet, and some CDC variants
 *
 *-------------------------------------------------------------------------*/

#ifdef	CONFIG_USB_CDCETHER
#define NEED_GENERIC_CDC
#endif

#ifdef	CONFIG_USB_ZAURUS
/* Ethernet variant uses funky framing, broken ethernet addressing */
#define NEED_GENERIC_CDC
#endif

#ifdef	CONFIG_USB_RNDIS
/* ACM variant uses even funkier framing, complex control RPC scheme */
#define NEED_GENERIC_CDC
#endif


#ifdef	NEED_GENERIC_CDC

#include <linux/usb_cdc.h>

struct cdc_state {
	struct usb_cdc_header_desc	*header;
	struct usb_cdc_union_desc	*u;
	struct usb_cdc_ether_desc	*ether;
	struct usb_interface		*control;
	struct usb_interface		*data;
};

static struct usb_driver usbnet_driver;

/*
 * probes control interface, claims data interface, collects the bulk
 * endpoints, activates data interface (if needed), maybe sets MTU.
 * all pure cdc, except for certain firmware workarounds.
 */
static int generic_cdc_bind (struct usbnet *dev, struct usb_interface *intf)
{
	u8				*buf = intf->cur_altsetting->extra;
	int				len = intf->cur_altsetting->extralen;
	struct usb_interface_descriptor	*d;
	struct cdc_state		*info = (void *) &dev->data;
	int				status;
	int				rndis;

	if (sizeof dev->data < sizeof *info)
		return -EDOM;

	/* expect strict spec conformance for the descriptors, but
	 * cope with firmware which stores them in the wrong place
	 */
	if (len == 0 && dev->udev->actconfig->extralen) {
		/* Motorola SB4100 (and others: Brad Hards says it's
		 * from a Broadcom design) put CDC descriptors here
		 */
		buf = dev->udev->actconfig->extra;
		len = dev->udev->actconfig->extralen;
		if (len)
			dev_dbg (&intf->dev,
				"CDC descriptors on config\n");
	}

	/* this assumes that if there's a non-RNDIS vendor variant
	 * of cdc-acm, it'll fail RNDIS requests cleanly.
	 */
	rndis = (intf->cur_altsetting->desc.bInterfaceProtocol == 0xff);

	memset (info, 0, sizeof *info);
	info->control = intf;
	while (len > 3) {
		if (buf [1] != USB_DT_CS_INTERFACE)
			goto next_desc;

		/* use bDescriptorSubType to identify the CDC descriptors.
		 * We expect devices with CDC header and union descriptors.
		 * For CDC Ethernet we need the ethernet descriptor.
		 * For RNDIS, ignore two (pointless) CDC modem descriptors
		 * in favor of a complicated OID-based RPC scheme doing what
		 * CDC Ethernet achieves with a simple descriptor.
		 */
		switch (buf [2]) {
		case USB_CDC_HEADER_TYPE:
			if (info->header) {
				dev_dbg (&intf->dev, "extra CDC header\n");
				goto bad_desc;
			}
			info->header = (void *) buf;
			if (info->header->bLength != sizeof *info->header) {
				dev_dbg (&intf->dev, "CDC header len %u\n",
					info->header->bLength);
				goto bad_desc;
			}
			break;
		case USB_CDC_UNION_TYPE:
			if (info->u) {
				dev_dbg (&intf->dev, "extra CDC union\n");
				goto bad_desc;
			}
			info->u = (void *) buf;
			if (info->u->bLength != sizeof *info->u) {
				dev_dbg (&intf->dev, "CDC union len %u\n",
					info->u->bLength);
				goto bad_desc;
			}

			/* we need a master/control interface (what we're
			 * probed with) and a slave/data interface; union
			 * descriptors sort this all out.
			 */
			info->control = usb_ifnum_to_if(dev->udev,
						info->u->bMasterInterface0);
			info->data = usb_ifnum_to_if(dev->udev,
						info->u->bSlaveInterface0);
			if (!info->control || !info->data) {
				dev_dbg (&intf->dev,
					"master #%u/%p slave #%u/%p\n",
					info->u->bMasterInterface0,
					info->control,
					info->u->bSlaveInterface0,
					info->data);
				goto bad_desc;
			}
			if (info->control != intf) {
				dev_dbg (&intf->dev, "bogus CDC Union\n");
				/* Ambit USB Cable Modem (and maybe others)
				 * interchanges master and slave interface.
				 */
				if (info->data == intf) {
					info->data = info->control;
					info->control = intf;
				} else
					goto bad_desc;
			}

			/* a data interface altsetting does the real i/o */
			d = &info->data->cur_altsetting->desc;
			if (d->bInterfaceClass != USB_CLASS_CDC_DATA) {
				dev_dbg (&intf->dev, "slave class %u\n",
					d->bInterfaceClass);
				goto bad_desc;
			}
			break;
		case USB_CDC_ETHERNET_TYPE:
			if (info->ether) {
				dev_dbg (&intf->dev, "extra CDC ether\n");
				goto bad_desc;
			}
			info->ether = (void *) buf;
			if (info->ether->bLength != sizeof *info->ether) {
				dev_dbg (&intf->dev, "CDC ether len %u\n",
					info->ether->bLength);
				goto bad_desc;
			}
			dev->net->mtu = le16_to_cpup (
						&info->ether->wMaxSegmentSize)
					- ETH_HLEN;
			/* because of Zaurus, we may be ignoring the host
			 * side link address we were given.
			 */
			break;
		}
next_desc:
		len -= buf [0];	/* bLength */
		buf += buf [0];
	}

	if (!info->header || !info->u || (!rndis && !info->ether)) {
		dev_dbg (&intf->dev, "missing cdc %s%s%sdescriptor\n",
			info->header ? "" : "header ",
			info->u ? "" : "union ",
			info->ether ? "" : "ether ");
		goto bad_desc;
	}

	/* claim data interface and set it up ... with side effects.
	 * network traffic can't flow until an altsetting is enabled.
	 */
	status = usb_driver_claim_interface (&usbnet_driver, info->data, dev);
	if (status < 0)
		return status;
	status = get_endpoints (dev, info->data);
	if (status < 0) {
		/* ensure immediate exit from usbnet_disconnect */
		usb_set_intfdata(info->data, NULL);
		usb_driver_release_interface (&usbnet_driver, info->data);
		return status;
	}

	/* status endpoint: optional for CDC Ethernet, not RNDIS (or ACM) */
	dev->status = NULL;
	if (info->control->cur_altsetting->desc.bNumEndpoints == 1) {
		struct usb_endpoint_descriptor	*desc;

		dev->status = &info->control->cur_altsetting->endpoint [0];
		desc = &dev->status->desc;
		if (desc->bmAttributes != USB_ENDPOINT_XFER_INT
				|| !(desc->bEndpointAddress & USB_DIR_IN)
				|| (le16_to_cpu(desc->wMaxPacketSize)
					< sizeof (struct usb_cdc_notification))
				|| !desc->bInterval) {
			dev_dbg (&intf->dev, "bad notification endpoint\n");
			dev->status = NULL;
		}
	}
	if (rndis && !dev->status) {
		dev_dbg (&intf->dev, "missing RNDIS status endpoint\n");
		usb_set_intfdata(info->data, NULL);
		usb_driver_release_interface (&usbnet_driver, info->data);
		return -ENODEV;
	}
	return 0;

bad_desc:
	dev_info (&dev->udev->dev, "bad CDC descriptors\n");
	return -ENODEV;
}

static void cdc_unbind (struct usbnet *dev, struct usb_interface *intf)
{
	struct cdc_state		*info = (void *) &dev->data;

	/* disconnect master --> disconnect slave */
	if (intf == info->control && info->data) {
		/* ensure immediate exit from usbnet_disconnect */
		usb_set_intfdata(info->data, NULL);
		usb_driver_release_interface (&usbnet_driver, info->data);
		info->data = NULL;
	}

	/* and vice versa (just in case) */
	else if (intf == info->data && info->control) {
		/* ensure immediate exit from usbnet_disconnect */
		usb_set_intfdata(info->control, NULL);
		usb_driver_release_interface (&usbnet_driver, info->control);
		info->control = NULL;
	}
}

#endif	/* NEED_GENERIC_CDC */


#ifdef	CONFIG_USB_CDCETHER
#define	HAVE_HARDWARE

/*-------------------------------------------------------------------------
 *
 * Communications Device Class, Ethernet Control model
 *
 * Takes two interfaces.  The DATA interface is inactive till an altsetting
 * is selected.  Configuration data includes class descriptors.
 *
 * This should interop with whatever the 2.4 "CDCEther.c" driver
 * (by Brad Hards) talked with.
 *
 *-------------------------------------------------------------------------*/

#include <linux/ctype.h>


static void dumpspeed (struct usbnet *dev, __le32 *speeds)
{
	if (netif_msg_timer (dev))
		devinfo (dev, "link speeds: %u kbps up, %u kbps down",
			__le32_to_cpu(speeds[0]) / 1000,
		__le32_to_cpu(speeds[1]) / 1000);
}

static void cdc_status (struct usbnet *dev, struct urb *urb)
{
	struct usb_cdc_notification	*event;

	if (urb->actual_length < sizeof *event)
		return;
	
	/* SPEED_CHANGE can get split into two 8-byte packets */
	if (test_and_clear_bit (EVENT_STS_SPLIT, &dev->flags)) {
		dumpspeed (dev, (__le32 *) urb->transfer_buffer);
		return;
	}

	event = urb->transfer_buffer;
	switch (event->bNotificationType) {
	case USB_CDC_NOTIFY_NETWORK_CONNECTION:
		if (netif_msg_timer (dev))
			devdbg (dev, "CDC: carrier %s",
					event->wValue ? "on" : "off");
		if (event->wValue)
			netif_carrier_on(dev->net);
		else
			netif_carrier_off(dev->net);
		break;
	case USB_CDC_NOTIFY_SPEED_CHANGE:	/* tx/rx rates */
		if (netif_msg_timer (dev))
			devdbg (dev, "CDC: speed change (len %d)",
					urb->actual_length);
		if (urb->actual_length != (sizeof *event + 8))
			set_bit (EVENT_STS_SPLIT, &dev->flags);
		else
			dumpspeed (dev, (__le32 *) &event[1]);
		break;
	// case USB_CDC_NOTIFY_RESPONSE_AVAILABLE:	/* RNDIS; or unsolicited */
	default:
		deverr (dev, "CDC: unexpected notification %02x!",
				 event->bNotificationType);
		break;
	}
}

static u8 nibble (unsigned char c)
{
	if (likely (isdigit (c)))
		return c - '0';
	c = toupper (c);
	if (likely (isxdigit (c)))
		return 10 + c - 'A';
	return 0;
}

static inline int
get_ethernet_addr (struct usbnet *dev, struct usb_cdc_ether_desc *e)
{
	int 		tmp, i;
	unsigned char	buf [13];

	tmp = usb_string (dev->udev, e->iMACAddress, buf, sizeof buf);
	if (tmp != 12) {
		dev_dbg (&dev->udev->dev,
			"bad MAC string %d fetch, %d\n", e->iMACAddress, tmp);
		if (tmp >= 0)
			tmp = -EINVAL;
		return tmp;
	}
	for (i = tmp = 0; i < 6; i++, tmp += 2)
		dev->net->dev_addr [i] =
			 (nibble (buf [tmp]) << 4) + nibble (buf [tmp + 1]);
	return 0;
}

static int cdc_bind (struct usbnet *dev, struct usb_interface *intf)
{
	int				status;
	struct cdc_state		*info = (void *) &dev->data;

	status = generic_cdc_bind (dev, intf);
	if (status < 0)
		return status;

	status = get_ethernet_addr (dev, info->ether);
	if (status < 0) {
		usb_set_intfdata(info->data, NULL);
		usb_driver_release_interface (&usbnet_driver, info->data);
		return status;
	}

	/* FIXME cdc-ether has some multicast code too, though it complains
	 * in routine cases.  info->ether describes the multicast support.
	 */
	return 0;
}

static const struct driver_info	cdc_info = {
	.description =	"CDC Ethernet Device",
	.flags =	FLAG_ETHER,
	// .check_connect = cdc_check_connect,
	.bind =		cdc_bind,
	.unbind =	cdc_unbind,
	.status =	cdc_status,
};

#endif	/* CONFIG_USB_CDCETHER */



#ifdef	CONFIG_USB_EPSON2888
#define	HAVE_HARDWARE

/*-------------------------------------------------------------------------
 *
 * EPSON USB clients
 *
 * This is the same idea as Linux PDAs (below) except the firmware in the
 * device might not be Tux-powered.  Epson provides reference firmware that
 * implements this interface.  Product developers can reuse or modify that
 * code, such as by using their own product and vendor codes.
 *
 * Support was from Juro Bystricky <bystricky.juro@erd.epson.com>
 *
 *-------------------------------------------------------------------------*/

static const struct driver_info	epson2888_info = {
	.description =	"Epson USB Device",
	.check_connect = always_connected,

	.in = 4, .out = 3,
};

#endif	/* CONFIG_USB_EPSON2888 */


#ifdef CONFIG_USB_GENESYS
#define	HAVE_HARDWARE

/*-------------------------------------------------------------------------
 *
 * GeneSys GL620USB-A (www.genesyslogic.com.tw)
 *
 * ... should partially interop with the Win32 driver for this hardware
 * The GeneSys docs imply there's some NDIS issue motivating this framing.
 *
 * Some info from GeneSys:
 *  - GL620USB-A is full duplex; GL620USB is only half duplex for bulk.
 *    (Some cables, like the BAFO-100c, use the half duplex version.)
 *  - For the full duplex model, the low bit of the version code says
 *    which side is which ("left/right").
 *  - For the half duplex type, a control/interrupt handshake settles
 *    the transfer direction.  (That's disabled here, partially coded.)
 *    A control URB would block until other side writes an interrupt.
 *
 * Original code from Jiun-Jie Huang <huangjj@genesyslogic.com.tw>
 * and merged into "usbnet" by Stanislav Brabec <utx@penguin.cz>.
 *
 *-------------------------------------------------------------------------*/

// control msg write command
#define GENELINK_CONNECT_WRITE			0xF0
// interrupt pipe index
#define GENELINK_INTERRUPT_PIPE			0x03
// interrupt read buffer size
#define INTERRUPT_BUFSIZE			0x08
// interrupt pipe interval value
#define GENELINK_INTERRUPT_INTERVAL		0x10
// max transmit packet number per transmit
#define GL_MAX_TRANSMIT_PACKETS			32
// max packet length
#define GL_MAX_PACKET_LEN			1514
// max receive buffer size 
#define GL_RCV_BUF_SIZE		\
	(((GL_MAX_PACKET_LEN + 4) * GL_MAX_TRANSMIT_PACKETS) + 4)

struct gl_packet {
	u32		packet_length;
	char		packet_data [1];
};

struct gl_header {
	u32			packet_count;
	struct gl_packet	packets;
};

#ifdef	GENELINK_ACK

// FIXME:  this code is incomplete, not debugged; it doesn't
// handle interrupts correctly.  interrupts should be generic
// code like all other device I/O, anyway.

struct gl_priv { 
	struct urb	*irq_urb;
	char		irq_buf [INTERRUPT_BUFSIZE];
};

static inline int gl_control_write (struct usbnet *dev, u8 request, u16 value)
{
	int retval;

	retval = usb_control_msg (dev->udev,
		      usb_sndctrlpipe (dev->udev, 0),
		      request,
		      USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		      value, 
		      0,			// index
		      0,			// data buffer
		      0,			// size
		      CONTROL_TIMEOUT_MS);
	return retval;
}

static void gl_interrupt_complete (struct urb *urb, struct pt_regs *regs)
{
	int status = urb->status;
	
	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d",
				__FUNCTION__, status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d",
				__FUNCTION__, urb->status);
	}

	status = usb_submit_urb (urb, GFP_ATOMIC);
	if (status)
		err ("%s - usb_submit_urb failed with result %d",
		     __FUNCTION__, status);
}

static int gl_interrupt_read (struct usbnet *dev)
{
	struct gl_priv	*priv = dev->priv_data;
	int		retval;

	// issue usb interrupt read
	if (priv && priv->irq_urb) {
		// submit urb
		if ((retval = usb_submit_urb (priv->irq_urb, GFP_KERNEL)) != 0)
			dbg ("gl_interrupt_read: submit fail - %X...", retval);
		else
			dbg ("gl_interrupt_read: submit success...");
	}

	return 0;
}

// check whether another side is connected
static int genelink_check_connect (struct usbnet *dev)
{
	int			retval;

	dbg ("genelink_check_connect...");

	// detect whether another side is connected
	if ((retval = gl_control_write (dev, GENELINK_CONNECT_WRITE, 0)) != 0) {
		dbg ("%s: genelink_check_connect write fail - %X",
			dev->net->name, retval);
		return retval;
	}

	// usb interrupt read to ack another side 
	if ((retval = gl_interrupt_read (dev)) != 0) {
		dbg ("%s: genelink_check_connect read fail - %X",
			dev->net->name, retval);
		return retval;
	}

	dbg ("%s: genelink_check_connect read success", dev->net->name);
	return 0;
}

// allocate and initialize the private data for genelink
static int genelink_init (struct usbnet *dev)
{
	struct gl_priv *priv;

	// allocate the private data structure
	if ((priv = kmalloc (sizeof *priv, GFP_KERNEL)) == 0) {
		dbg ("%s: cannot allocate private data per device",
			dev->net->name);
		return -ENOMEM;
	}

	// allocate irq urb
	if ((priv->irq_urb = usb_alloc_urb (0, GFP_KERNEL)) == 0) {
		dbg ("%s: cannot allocate private irq urb per device",
			dev->net->name);
		kfree (priv);
		return -ENOMEM;
	}

	// fill irq urb
	usb_fill_int_urb (priv->irq_urb, dev->udev,
		usb_rcvintpipe (dev->udev, GENELINK_INTERRUPT_PIPE),
		priv->irq_buf, INTERRUPT_BUFSIZE,
		gl_interrupt_complete, 0,
		GENELINK_INTERRUPT_INTERVAL);

	// set private data pointer
	dev->priv_data = priv;

	return 0;
}

// release the private data
static int genelink_free (struct usbnet *dev)
{
	struct gl_priv	*priv = dev->priv_data;

	if (!priv) 
		return 0;

// FIXME:  can't cancel here; it's synchronous, and
// should have happened earlier in any case (interrupt
// handling needs to be generic)

	// cancel irq urb first
	usb_kill_urb (priv->irq_urb);

	// free irq urb
	usb_free_urb (priv->irq_urb);

	// free the private data structure
	kfree (priv);

	return 0;
}

#endif

static int genelink_rx_fixup (struct usbnet *dev, struct sk_buff *skb)
{
	struct gl_header	*header;
	struct gl_packet	*packet;
	struct sk_buff		*gl_skb;
	u32			size;

	header = (struct gl_header *) skb->data;

	// get the packet count of the received skb
	le32_to_cpus (&header->packet_count);
	if ((header->packet_count > GL_MAX_TRANSMIT_PACKETS)
			|| (header->packet_count < 0)) {
		dbg ("genelink: invalid received packet count %d",
			header->packet_count);
		return 0;
	}

	// set the current packet pointer to the first packet
	packet = &header->packets;

	// decrement the length for the packet count size 4 bytes
	skb_pull (skb, 4);

	while (header->packet_count > 1) {
		// get the packet length
		size = packet->packet_length;

		// this may be a broken packet
		if (size > GL_MAX_PACKET_LEN) {
			dbg ("genelink: invalid rx length %d", size);
			return 0;
		}

		// allocate the skb for the individual packet
		gl_skb = alloc_skb (size, GFP_ATOMIC);
		if (gl_skb) {

			// copy the packet data to the new skb
			memcpy(skb_put(gl_skb, size), packet->packet_data, size);
			skb_return (dev, gl_skb);
		}

		// advance to the next packet
		packet = (struct gl_packet *)
			&packet->packet_data [size];
		header->packet_count--;

		// shift the data pointer to the next gl_packet
		skb_pull (skb, size + 4);
	}

	// skip the packet length field 4 bytes
	skb_pull (skb, 4);

	if (skb->len > GL_MAX_PACKET_LEN) {
		dbg ("genelink: invalid rx length %d", skb->len);
		return 0;
	}
	return 1;
}

static struct sk_buff *
genelink_tx_fixup (struct usbnet *dev, struct sk_buff *skb, int flags)
{
	int 	padlen;
	int	length = skb->len;
	int	headroom = skb_headroom (skb);
	int	tailroom = skb_tailroom (skb);
	u32	*packet_count;
	u32	*packet_len;

	// FIXME:  magic numbers, bleech
	padlen = ((skb->len + (4 + 4*1)) % 64) ? 0 : 1;

	if ((!skb_cloned (skb))
			&& ((headroom + tailroom) >= (padlen + (4 + 4*1)))) {
		if ((headroom < (4 + 4*1)) || (tailroom < padlen)) {
			skb->data = memmove (skb->head + (4 + 4*1),
					     skb->data, skb->len);
			skb->tail = skb->data + skb->len;
		}
	} else {
		struct sk_buff	*skb2;
		skb2 = skb_copy_expand (skb, (4 + 4*1) , padlen, flags);
		dev_kfree_skb_any (skb);
		skb = skb2;
		if (!skb)
			return NULL;
	}

	// attach the packet count to the header
	packet_count = (u32 *) skb_push (skb, (4 + 4*1));
	packet_len = packet_count + 1;

	// FIXME little endian?
	*packet_count = 1;
	*packet_len = length;

	// add padding byte
	if ((skb->len % dev->maxpacket) == 0)
		skb_put (skb, 1);

	return skb;
}

static const struct driver_info	genelink_info = {
	.description =	"Genesys GeneLink",
	.flags =	FLAG_FRAMING_GL | FLAG_NO_SETINT,
	.rx_fixup =	genelink_rx_fixup,
	.tx_fixup =	genelink_tx_fixup,

	.in = 1, .out = 2,

#ifdef	GENELINK_ACK
	.check_connect =genelink_check_connect,
#endif
};

#endif /* CONFIG_USB_GENESYS */



#ifdef	CONFIG_USB_NET1080
#define	HAVE_HARDWARE

/*-------------------------------------------------------------------------
 *
 * Netchip 1080 driver ... http://www.netchip.com
 * Used in LapLink cables
 *
 *-------------------------------------------------------------------------*/

#define dev_packet_id	data[0]
#define frame_errors	data[1]

/*
 * NetChip framing of ethernet packets, supporting additional error
 * checks for links that may drop bulk packets from inside messages.
 * Odd USB length == always short read for last usb packet.
 *	- nc_header
 *	- Ethernet header (14 bytes)
 *	- payload
 *	- (optional padding byte, if needed so length becomes odd)
 *	- nc_trailer
 *
 * This framing is to be avoided for non-NetChip devices.
 */

struct nc_header {		// packed:
	__le16	hdr_len;		// sizeof nc_header (LE, all)
	__le16	packet_len;		// payload size (including ethhdr)
	__le16	packet_id;		// detects dropped packets
#define MIN_HEADER	6

	// all else is optional, and must start with:
	// u16	vendorId;		// from usb-if
	// u16	productId;
} __attribute__((__packed__));

#define	PAD_BYTE	((unsigned char)0xAC)

struct nc_trailer {
	__le16	packet_id;
} __attribute__((__packed__));

// packets may use FLAG_FRAMING_NC and optional pad
#define FRAMED_SIZE(mtu) (sizeof (struct nc_header) \
				+ sizeof (struct ethhdr) \
				+ (mtu) \
				+ 1 \
				+ sizeof (struct nc_trailer))

#define MIN_FRAMED	FRAMED_SIZE(0)


/*
 * Zero means no timeout; else, how long a 64 byte bulk packet may be queued
 * before the hardware drops it.  If that's done, the driver will need to
 * frame network packets to guard against the dropped USB packets.  The win32
 * driver sets this for both sides of the link.
 */
#define	NC_READ_TTL_MS	((u8)255)	// ms

/*
 * We ignore most registers and EEPROM contents.
 */
#define	REG_USBCTL	((u8)0x04)
#define REG_TTL		((u8)0x10)
#define REG_STATUS	((u8)0x11)

/*
 * Vendor specific requests to read/write data
 */
#define	REQUEST_REGISTER	((u8)0x10)
#define	REQUEST_EEPROM		((u8)0x11)

static int
nc_vendor_read (struct usbnet *dev, u8 req, u8 regnum, u16 *retval_ptr)
{
	int status = usb_control_msg (dev->udev,
		usb_rcvctrlpipe (dev->udev, 0),
		req,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		0, regnum,
		retval_ptr, sizeof *retval_ptr,
		CONTROL_TIMEOUT_MS);
	if (status > 0)
		status = 0;
	if (!status)
		le16_to_cpus (retval_ptr);
	return status;
}

static inline int
nc_register_read (struct usbnet *dev, u8 regnum, u16 *retval_ptr)
{
	return nc_vendor_read (dev, REQUEST_REGISTER, regnum, retval_ptr);
}

// no retval ... can become async, usable in_interrupt()
static void
nc_vendor_write (struct usbnet *dev, u8 req, u8 regnum, u16 value)
{
	usb_control_msg (dev->udev,
		usb_sndctrlpipe (dev->udev, 0),
		req,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		value, regnum,
		NULL, 0,			// data is in setup packet
		CONTROL_TIMEOUT_MS);
}

static inline void
nc_register_write (struct usbnet *dev, u8 regnum, u16 value)
{
	nc_vendor_write (dev, REQUEST_REGISTER, regnum, value);
}


#if 0
static void nc_dump_registers (struct usbnet *dev)
{
	u8	reg;
	u16	*vp = kmalloc (sizeof (u16));

	if (!vp) {
		dbg ("no memory?");
		return;
	}

	dbg ("%s registers:", dev->net->name);
	for (reg = 0; reg < 0x20; reg++) {
		int retval;

		// reading some registers is trouble
		if (reg >= 0x08 && reg <= 0xf)
			continue;
		if (reg >= 0x12 && reg <= 0x1e)
			continue;

		retval = nc_register_read (dev, reg, vp);
		if (retval < 0)
			dbg ("%s reg [0x%x] ==> error %d",
				dev->net->name, reg, retval);
		else
			dbg ("%s reg [0x%x] = 0x%x",
				dev->net->name, reg, *vp);
	}
	kfree (vp);
}
#endif


/*-------------------------------------------------------------------------*/

/*
 * Control register
 */

#define	USBCTL_WRITABLE_MASK	0x1f0f
// bits 15-13 reserved, r/o
#define	USBCTL_ENABLE_LANG	(1 << 12)
#define	USBCTL_ENABLE_MFGR	(1 << 11)
#define	USBCTL_ENABLE_PROD	(1 << 10)
#define	USBCTL_ENABLE_SERIAL	(1 << 9)
#define	USBCTL_ENABLE_DEFAULTS	(1 << 8)
// bits 7-4 reserved, r/o
#define	USBCTL_FLUSH_OTHER	(1 << 3)
#define	USBCTL_FLUSH_THIS	(1 << 2)
#define	USBCTL_DISCONN_OTHER	(1 << 1)
#define	USBCTL_DISCONN_THIS	(1 << 0)

static inline void nc_dump_usbctl (struct usbnet *dev, u16 usbctl)
{
	if (!netif_msg_link (dev))
		return;
	devdbg (dev, "net1080 %s-%s usbctl 0x%x:%s%s%s%s%s;"
			" this%s%s;"
			" other%s%s; r/o 0x%x",
		dev->udev->bus->bus_name, dev->udev->devpath,
		usbctl,
		(usbctl & USBCTL_ENABLE_LANG) ? " lang" : "",
		(usbctl & USBCTL_ENABLE_MFGR) ? " mfgr" : "",
		(usbctl & USBCTL_ENABLE_PROD) ? " prod" : "",
		(usbctl & USBCTL_ENABLE_SERIAL) ? " serial" : "",
		(usbctl & USBCTL_ENABLE_DEFAULTS) ? " defaults" : "",

		(usbctl & USBCTL_FLUSH_OTHER) ? " FLUSH" : "",
		(usbctl & USBCTL_DISCONN_OTHER) ? " DIS" : "",
		(usbctl & USBCTL_FLUSH_THIS) ? " FLUSH" : "",
		(usbctl & USBCTL_DISCONN_THIS) ? " DIS" : "",
		usbctl & ~USBCTL_WRITABLE_MASK
		);
}

/*-------------------------------------------------------------------------*/

/*
 * Status register
 */

#define	STATUS_PORT_A		(1 << 15)

#define	STATUS_CONN_OTHER	(1 << 14)
#define	STATUS_SUSPEND_OTHER	(1 << 13)
#define	STATUS_MAILBOX_OTHER	(1 << 12)
#define	STATUS_PACKETS_OTHER(n)	(((n) >> 8) && 0x03)

#define	STATUS_CONN_THIS	(1 << 6)
#define	STATUS_SUSPEND_THIS	(1 << 5)
#define	STATUS_MAILBOX_THIS	(1 << 4)
#define	STATUS_PACKETS_THIS(n)	(((n) >> 0) && 0x03)

#define	STATUS_UNSPEC_MASK	0x0c8c
#define	STATUS_NOISE_MASK 	((u16)~(0x0303|STATUS_UNSPEC_MASK))


static inline void nc_dump_status (struct usbnet *dev, u16 status)
{
	if (!netif_msg_link (dev))
		return;
	devdbg (dev, "net1080 %s-%s status 0x%x:"
			" this (%c) PKT=%d%s%s%s;"
			" other PKT=%d%s%s%s; unspec 0x%x",
		dev->udev->bus->bus_name, dev->udev->devpath,
		status,

		// XXX the packet counts don't seem right
		// (1 at reset, not 0); maybe UNSPEC too

		(status & STATUS_PORT_A) ? 'A' : 'B',
		STATUS_PACKETS_THIS (status),
		(status & STATUS_CONN_THIS) ? " CON" : "",
		(status & STATUS_SUSPEND_THIS) ? " SUS" : "",
		(status & STATUS_MAILBOX_THIS) ? " MBOX" : "",

		STATUS_PACKETS_OTHER (status),
		(status & STATUS_CONN_OTHER) ? " CON" : "",
		(status & STATUS_SUSPEND_OTHER) ? " SUS" : "",
		(status & STATUS_MAILBOX_OTHER) ? " MBOX" : "",

		status & STATUS_UNSPEC_MASK
		);
}

/*-------------------------------------------------------------------------*/

/*
 * TTL register
 */

#define	TTL_THIS(ttl)	(0x00ff & ttl)
#define	TTL_OTHER(ttl)	(0x00ff & (ttl >> 8))
#define MK_TTL(this,other)	((u16)(((other)<<8)|(0x00ff&(this))))

static inline void nc_dump_ttl (struct usbnet *dev, u16 ttl)
{
	if (netif_msg_link (dev))
		devdbg (dev, "net1080 %s-%s ttl 0x%x this = %d, other = %d",
			dev->udev->bus->bus_name, dev->udev->devpath,
			ttl, TTL_THIS (ttl), TTL_OTHER (ttl));
}

/*-------------------------------------------------------------------------*/

static int net1080_reset (struct usbnet *dev)
{
	u16		usbctl, status, ttl;
	u16		*vp = kmalloc (sizeof (u16), GFP_KERNEL);
	int		retval;

	if (!vp)
		return -ENOMEM;

	// nc_dump_registers (dev);

	if ((retval = nc_register_read (dev, REG_STATUS, vp)) < 0) {
		dbg ("can't read %s-%s status: %d",
			dev->udev->bus->bus_name, dev->udev->devpath, retval);
		goto done;
	}
	status = *vp;
	nc_dump_status (dev, status);

	if ((retval = nc_register_read (dev, REG_USBCTL, vp)) < 0) {
		dbg ("can't read USBCTL, %d", retval);
		goto done;
	}
	usbctl = *vp;
	nc_dump_usbctl (dev, usbctl);

	nc_register_write (dev, REG_USBCTL,
			USBCTL_FLUSH_THIS | USBCTL_FLUSH_OTHER);

	if ((retval = nc_register_read (dev, REG_TTL, vp)) < 0) {
		dbg ("can't read TTL, %d", retval);
		goto done;
	}
	ttl = *vp;
	// nc_dump_ttl (dev, ttl);

	nc_register_write (dev, REG_TTL,
			MK_TTL (NC_READ_TTL_MS, TTL_OTHER (ttl)) );
	dbg ("%s: assigned TTL, %d ms", dev->net->name, NC_READ_TTL_MS);

	if (netif_msg_link (dev))
		devinfo (dev, "port %c, peer %sconnected",
			(status & STATUS_PORT_A) ? 'A' : 'B',
			(status & STATUS_CONN_OTHER) ? "" : "dis"
			);
	retval = 0;

done:
	kfree (vp);
	return retval;
}

static int net1080_check_connect (struct usbnet *dev)
{
	int			retval;
	u16			status;
	u16			*vp = kmalloc (sizeof (u16), GFP_KERNEL);

	if (!vp)
		return -ENOMEM;
	retval = nc_register_read (dev, REG_STATUS, vp);
	status = *vp;
	kfree (vp);
	if (retval != 0) {
		dbg ("%s net1080_check_conn read - %d", dev->net->name, retval);
		return retval;
	}
	if ((status & STATUS_CONN_OTHER) != STATUS_CONN_OTHER)
		return -ENOLINK;
	return 0;
}

static void nc_flush_complete (struct urb *urb, struct pt_regs *regs)
{
	kfree (urb->context);
	usb_free_urb(urb);
}

static void nc_ensure_sync (struct usbnet *dev)
{
	dev->frame_errors++;
	if (dev->frame_errors > 5) {
		struct urb		*urb;
		struct usb_ctrlrequest	*req;
		int			status;

		/* Send a flush */
		urb = usb_alloc_urb (0, SLAB_ATOMIC);
		if (!urb)
			return;

		req = kmalloc (sizeof *req, GFP_ATOMIC);
		if (!req) {
			usb_free_urb (urb);
			return;
		}

		req->bRequestType = USB_DIR_OUT
			| USB_TYPE_VENDOR
			| USB_RECIP_DEVICE;
		req->bRequest = REQUEST_REGISTER;
		req->wValue = cpu_to_le16 (USBCTL_FLUSH_THIS
				| USBCTL_FLUSH_OTHER);
		req->wIndex = cpu_to_le16 (REG_USBCTL);
		req->wLength = cpu_to_le16 (0);

		/* queue an async control request, we don't need
		 * to do anything when it finishes except clean up.
		 */
		usb_fill_control_urb (urb, dev->udev,
			usb_sndctrlpipe (dev->udev, 0),
			(unsigned char *) req,
			NULL, 0,
			nc_flush_complete, req);
		status = usb_submit_urb (urb, GFP_ATOMIC);
		if (status) {
			kfree (req);
			usb_free_urb (urb);
			return;
		}

		if (netif_msg_rx_err (dev))
			devdbg (dev, "flush net1080; too many framing errors");
		dev->frame_errors = 0;
	}
}

static int net1080_rx_fixup (struct usbnet *dev, struct sk_buff *skb)
{
	struct nc_header	*header;
	struct nc_trailer	*trailer;
	u16			hdr_len, packet_len;

	if (!(skb->len & 0x01)
			|| MIN_FRAMED > skb->len
			|| skb->len > FRAMED_SIZE (dev->net->mtu)) {
		dev->stats.rx_frame_errors++;
		dbg ("rx framesize %d range %d..%d mtu %d", skb->len,
			(int)MIN_FRAMED, (int)FRAMED_SIZE (dev->net->mtu),
			dev->net->mtu);
		nc_ensure_sync (dev);
		return 0;
	}

	header = (struct nc_header *) skb->data;
	hdr_len = le16_to_cpup (&header->hdr_len);
	packet_len = le16_to_cpup (&header->packet_len);
	if (FRAMED_SIZE (packet_len) > MAX_PACKET) {
		dev->stats.rx_frame_errors++;
		dbg ("packet too big, %d", packet_len);
		nc_ensure_sync (dev);
		return 0;
	} else if (hdr_len < MIN_HEADER) {
		dev->stats.rx_frame_errors++;
		dbg ("header too short, %d", hdr_len);
		nc_ensure_sync (dev);
		return 0;
	} else if (hdr_len > MIN_HEADER) {
		// out of band data for us?
		dbg ("header OOB, %d bytes", hdr_len - MIN_HEADER);
		nc_ensure_sync (dev);
		// switch (vendor/product ids) { ... }
	}
	skb_pull (skb, hdr_len);

	trailer = (struct nc_trailer *)
		(skb->data + skb->len - sizeof *trailer);
	skb_trim (skb, skb->len - sizeof *trailer);

	if ((packet_len & 0x01) == 0) {
		if (skb->data [packet_len] != PAD_BYTE) {
			dev->stats.rx_frame_errors++;
			dbg ("bad pad");
			return 0;
		}
		skb_trim (skb, skb->len - 1);
	}
	if (skb->len != packet_len) {
		dev->stats.rx_frame_errors++;
		dbg ("bad packet len %d (expected %d)",
			skb->len, packet_len);
		nc_ensure_sync (dev);
		return 0;
	}
	if (header->packet_id != get_unaligned (&trailer->packet_id)) {
		dev->stats.rx_fifo_errors++;
		dbg ("(2+ dropped) rx packet_id mismatch 0x%x 0x%x",
			le16_to_cpu (header->packet_id),
			le16_to_cpu (trailer->packet_id));
		return 0;
	}
#if 0
	devdbg (dev, "frame <rx h %d p %d id %d", header->hdr_len,
		header->packet_len, header->packet_id);
#endif
	dev->frame_errors = 0;
	return 1;
}

static struct sk_buff *
net1080_tx_fixup (struct usbnet *dev, struct sk_buff *skb, int flags)
{
	int			padlen;
	struct sk_buff		*skb2;

	padlen = ((skb->len + sizeof (struct nc_header)
			+ sizeof (struct nc_trailer)) & 0x01) ? 0 : 1;
	if (!skb_cloned (skb)) {
		int	headroom = skb_headroom (skb);
		int	tailroom = skb_tailroom (skb);

		if ((padlen + sizeof (struct nc_trailer)) <= tailroom
			    && sizeof (struct nc_header) <= headroom)
			/* There's enough head and tail room */
			return skb;

		if ((sizeof (struct nc_header) + padlen
					+ sizeof (struct nc_trailer)) <
				(headroom + tailroom)) {
			/* There's enough total room, so just readjust */
			skb->data = memmove (skb->head
						+ sizeof (struct nc_header),
					    skb->data, skb->len);
			skb->tail = skb->data + skb->len;
			return skb;
		}
	}

	/* Create a new skb to use with the correct size */
	skb2 = skb_copy_expand (skb,
				sizeof (struct nc_header),
				sizeof (struct nc_trailer) + padlen,
				flags);
	dev_kfree_skb_any (skb);
	return skb2;
}

static const struct driver_info	net1080_info = {
	.description =	"NetChip TurboCONNECT",
	.flags =	FLAG_FRAMING_NC,
	.reset =	net1080_reset,
	.check_connect =net1080_check_connect,
	.rx_fixup =	net1080_rx_fixup,
	.tx_fixup =	net1080_tx_fixup,
};

#endif /* CONFIG_USB_NET1080 */



#ifdef CONFIG_USB_PL2301
#define	HAVE_HARDWARE

/*-------------------------------------------------------------------------
 *
 * Prolific PL-2301/PL-2302 driver ... http://www.prolifictech.com
 *
 * The protocol and handshaking used here should be bug-compatible
 * with the Linux 2.2 "plusb" driver, by Deti Fliegl.
 *
 *-------------------------------------------------------------------------*/

/*
 * Bits 0-4 can be used for software handshaking; they're set from
 * one end, cleared from the other, "read" with the interrupt byte.
 */
#define	PL_S_EN		(1<<7)		/* (feature only) suspend enable */
/* reserved bit -- rx ready (6) ? */
#define	PL_TX_READY	(1<<5)		/* (interrupt only) transmit ready */
#define	PL_RESET_OUT	(1<<4)		/* reset output pipe */
#define	PL_RESET_IN	(1<<3)		/* reset input pipe */
#define	PL_TX_C		(1<<2)		/* transmission complete */
#define	PL_TX_REQ	(1<<1)		/* transmission received */
#define	PL_PEER_E	(1<<0)		/* peer exists */

static inline int
pl_vendor_req (struct usbnet *dev, u8 req, u8 val, u8 index)
{
	return usb_control_msg (dev->udev,
		usb_rcvctrlpipe (dev->udev, 0),
		req,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		val, index,
		NULL, 0,
		CONTROL_TIMEOUT_MS);
}

static inline int
pl_clear_QuickLink_features (struct usbnet *dev, int val)
{
	return pl_vendor_req (dev, 1, (u8) val, 0);
}

static inline int
pl_set_QuickLink_features (struct usbnet *dev, int val)
{
	return pl_vendor_req (dev, 3, (u8) val, 0);
}

/*-------------------------------------------------------------------------*/

static int pl_reset (struct usbnet *dev)
{
	/* some units seem to need this reset, others reject it utterly.
	 * FIXME be more like "naplink" or windows drivers.
	 */
	(void) pl_set_QuickLink_features (dev,
		PL_S_EN|PL_RESET_OUT|PL_RESET_IN|PL_PEER_E);
	return 0;
}

static const struct driver_info	prolific_info = {
	.description =	"Prolific PL-2301/PL-2302",
	.flags =	FLAG_NO_SETINT,
		/* some PL-2302 versions seem to fail usb_set_interface() */
	.reset =	pl_reset,
};

#endif /* CONFIG_USB_PL2301 */


#ifdef CONFIG_USB_KC2190
#define HAVE_HARDWARE
static const struct driver_info kc2190_info = {
	.description =  "KC Technology KC-190",
};
#endif /* CONFIG_USB_KC2190 */


#ifdef	CONFIG_USB_ARMLINUX
#define	HAVE_HARDWARE

/*-------------------------------------------------------------------------
 *
 * Intel's SA-1100 chip integrates basic USB support, and is used
 * in PDAs like some iPaqs, the Yopy, some Zaurus models, and more.
 * When they run Linux, arch/arm/mach-sa1100/usb-eth.c may be used to
 * network using minimal USB framing data.
 *
 * This describes the driver currently in standard ARM Linux kernels.
 * The Zaurus uses a different driver (see later).
 *
 * PXA25x and PXA210 use XScale cores (ARM v5TE) with better USB support
 * and different USB endpoint numbering than the SA1100 devices.  The
 * mach-pxa/usb-eth.c driver re-uses the device ids from mach-sa1100
 * so we rely on the endpoint descriptors.
 *
 *-------------------------------------------------------------------------*/

static const struct driver_info	linuxdev_info = {
	.description =	"Linux Device",
	.check_connect = always_connected,
};

static const struct driver_info	yopy_info = {
	.description =	"Yopy",
	.check_connect = always_connected,
};

static const struct driver_info	blob_info = {
	.description =	"Boot Loader OBject",
	.check_connect = always_connected,
};

#endif	/* CONFIG_USB_ARMLINUX */


#ifdef CONFIG_USB_ZAURUS
#define	HAVE_HARDWARE

#include <linux/crc32.h>

/*-------------------------------------------------------------------------
 *
 * Zaurus is also a SA-1110 based PDA, but one using a different driver
 * (and framing) for its USB slave/gadget controller than the case above.
 *
 * For the current version of that driver, the main way that framing is
 * nonstandard (also from perspective of the CDC ethernet model!) is a
 * crc32, added to help detect when some sa1100 usb-to-memory DMA errata
 * haven't been fully worked around.  Also, all Zaurii use the same
 * default Ethernet address.
 *
 * PXA based models use the same framing, and also can't implement
 * set_interface properly.
 *
 * All known Zaurii lie about their standards conformance.  Most lie by
 * saying they support CDC Ethernet.  Some lie and say they support CDC
 * MDLM (as if for access to cell phone modems).  Someone, please beat 
 * on Sharp (and other such vendors) for a while with a cluestick.
 *
 *-------------------------------------------------------------------------*/

static struct sk_buff *
zaurus_tx_fixup (struct usbnet *dev, struct sk_buff *skb, int flags)
{
	int			padlen;
	struct sk_buff		*skb2;

	padlen = 2;
	if (!skb_cloned (skb)) {
		int	tailroom = skb_tailroom (skb);
		if ((padlen + 4) <= tailroom)
			goto done;
	}
	skb2 = skb_copy_expand (skb, 0, 4 + padlen, flags);
	dev_kfree_skb_any (skb);
	skb = skb2;
	if (skb) {
		u32		fcs;
done:
		fcs = crc32_le (~0, skb->data, skb->len);
		fcs = ~fcs;

		*skb_put (skb, 1) = fcs       & 0xff;
		*skb_put (skb, 1) = (fcs>> 8) & 0xff;
		*skb_put (skb, 1) = (fcs>>16) & 0xff;
		*skb_put (skb, 1) = (fcs>>24) & 0xff;
	}
	return skb;
}

static const struct driver_info	zaurus_sl5x00_info = {
	.description =	"Sharp Zaurus SL-5x00",
	.flags =	FLAG_FRAMING_Z,
	.check_connect = always_connected,
	.bind =		generic_cdc_bind,
	.unbind =	cdc_unbind,
	.tx_fixup = 	zaurus_tx_fixup,
};
#define	ZAURUS_STRONGARM_INFO	((unsigned long)&zaurus_sl5x00_info)

static const struct driver_info	zaurus_pxa_info = {
	.description =	"Sharp Zaurus, PXA-2xx based",
	.flags =	FLAG_FRAMING_Z,
	.check_connect = always_connected,
	.bind =		generic_cdc_bind,
	.unbind =	cdc_unbind,
	.tx_fixup = 	zaurus_tx_fixup,
};
#define	ZAURUS_PXA_INFO		((unsigned long)&zaurus_pxa_info)

static const struct driver_info	olympus_mxl_info = {
	.description =	"Olympus R1000",
	.flags =	FLAG_FRAMING_Z,
	.check_connect = always_connected,
	.bind =		generic_cdc_bind,
	.unbind =	cdc_unbind,
	.tx_fixup = 	zaurus_tx_fixup,
};
#define	OLYMPUS_MXL_INFO	((unsigned long)&olympus_mxl_info)


/* Some more recent products using Lineo/Belcarra code will wrongly claim
 * CDC MDLM conformance.  They aren't conformant:  data endpoints live
 * in the control interface, there's no data interface, and it's not used
 * to talk to a cell phone radio.  But at least we can detect these two
 * pseudo-classes, rather than growing this product list with entries for
 * each new nonconformant product (sigh).
 */
static const u8 safe_guid[16] = {
	0x5d, 0x34, 0xcf, 0x66, 0x11, 0x18, 0x11, 0xd6,
	0xa2, 0x1a, 0x00, 0x01, 0x02, 0xca, 0x9a, 0x7f,
};
static const u8 blan_guid[16] = {
	0x74, 0xf0, 0x3d, 0xbd, 0x1e, 0xc1, 0x44, 0x70,
	0xa3, 0x67, 0x71, 0x34, 0xc9, 0xf5, 0x54, 0x37,
};

static int blan_mdlm_bind (struct usbnet *dev, struct usb_interface *intf)
{
	u8				*buf = intf->cur_altsetting->extra;
	int				len = intf->cur_altsetting->extralen;
	struct usb_cdc_mdlm_desc	*desc = NULL;
	struct usb_cdc_mdlm_detail_desc	*detail = NULL;

	while (len > 3) {
		if (buf [1] != USB_DT_CS_INTERFACE)
			goto next_desc;

		/* use bDescriptorSubType, and just verify that we get a
		 * "BLAN" (or "SAFE") descriptor.
		 */
		switch (buf [2]) {
		case USB_CDC_MDLM_TYPE:
			if (desc) {
				dev_dbg (&intf->dev, "extra MDLM\n");
				goto bad_desc;
			}
			desc = (void *) buf;
			if (desc->bLength != sizeof *desc) {
				dev_dbg (&intf->dev, "MDLM len %u\n",
					desc->bLength);
				goto bad_desc;
			}
			/* expect bcdVersion 1.0, ignore */
			if (memcmp(&desc->bGUID, blan_guid, 16)
				    && memcmp(&desc->bGUID, safe_guid, 16) ) {
				/* hey, this one might _really_ be MDLM! */
				dev_dbg (&intf->dev, "MDLM guid\n");
				goto bad_desc;
			}
			break;
		case USB_CDC_MDLM_DETAIL_TYPE:
			if (detail) {
				dev_dbg (&intf->dev, "extra MDLM detail\n");
				goto bad_desc;
			}
			detail = (void *) buf;
			switch (detail->bGuidDescriptorType) {
			case 0:			/* "SAFE" */
				if (detail->bLength != (sizeof *detail + 2))
					goto bad_detail;
				break;
			case 1:			/* "BLAN" */
				if (detail->bLength != (sizeof *detail + 3))
					goto bad_detail;
				break;
			default:
				goto bad_detail;
			}

			/* assuming we either noticed BLAN already, or will
			 * find it soon, there are some data bytes here:
			 *  - bmNetworkCapabilities (unused)
			 *  - bmDataCapabilities (bits, see below)
			 *  - bPad (ignored, for PADAFTER -- BLAN-only)
			 * bits are:
			 *  - 0x01 -- Zaurus framing (add CRC)
			 *  - 0x02 -- PADBEFORE (CRC includes some padding)
			 *  - 0x04 -- PADAFTER (some padding after CRC)
			 *  - 0x08 -- "fermat" packet mangling (for hw bugs)
			 * the PADBEFORE appears not to matter; we interop
			 * with devices that use it and those that don't.
			 */
			if ((detail->bDetailData[1] & ~02) != 0x01) {
				/* bmDataCapabilites == 0 would be fine too,
				 * but framing is minidriver-coupled for now.
				 */
bad_detail:
				dev_dbg (&intf->dev,
						"bad MDLM detail, %d %d %d\n",
						detail->bLength,
						detail->bDetailData[0],
						detail->bDetailData[2]);
				goto bad_desc;
			}
			break;
		}
next_desc:
		len -= buf [0];	/* bLength */
		buf += buf [0];
	}

	if (!desc || !detail) {
		dev_dbg (&intf->dev, "missing cdc mdlm %s%sdescriptor\n",
			desc ? "" : "func ",
			detail ? "" : "detail ");
		goto bad_desc;
	}

	/* There's probably a CDC Ethernet descriptor there, but we can't
	 * rely on the Ethernet address it provides since not all vendors
	 * bother to make it unique.  Likewise there's no point in tracking
	 * of the CDC event notifications.
	 */
	return get_endpoints (dev, intf);

bad_desc:
	dev_info (&dev->udev->dev, "unsupported MDLM descriptors\n");
	return -ENODEV;
}

static const struct driver_info	bogus_mdlm_info = {
	.description =	"pseudo-MDLM (BLAN) device",
	.flags =	FLAG_FRAMING_Z,
	.check_connect = always_connected,
	.tx_fixup = 	zaurus_tx_fixup,
	.bind =		blan_mdlm_bind,
};

#else

/* blacklist all those devices */
#define	ZAURUS_STRONGARM_INFO	0
#define	ZAURUS_PXA_INFO		0
#define	OLYMPUS_MXL_INFO	0

#endif


/*-------------------------------------------------------------------------
 *
 * Network Device Driver (peer link to "Host Device", from USB host)
 *
 *-------------------------------------------------------------------------*/

static int usbnet_change_mtu (struct net_device *net, int new_mtu)
{
	struct usbnet	*dev = netdev_priv(net);

	if (new_mtu <= MIN_PACKET || new_mtu > MAX_PACKET)
		return -EINVAL;
#ifdef	CONFIG_USB_NET1080
	if (((dev->driver_info->flags) & FLAG_FRAMING_NC)) {
		if (FRAMED_SIZE (new_mtu) > MAX_PACKET)
			return -EINVAL;
	}
#endif
#ifdef	CONFIG_USB_GENESYS
	if (((dev->driver_info->flags) & FLAG_FRAMING_GL)
			&& new_mtu > GL_MAX_PACKET_LEN)
		return -EINVAL;
#endif
	// no second zero-length packet read wanted after mtu-sized packets
	if (((new_mtu + sizeof (struct ethhdr)) % dev->maxpacket) == 0)
		return -EDOM;
	net->mtu = new_mtu;
	return 0;
}

/*-------------------------------------------------------------------------*/

static struct net_device_stats *usbnet_get_stats (struct net_device *net)
{
	struct usbnet	*dev = netdev_priv(net);
	return &dev->stats;
}

/*-------------------------------------------------------------------------*/

/* some LK 2.4 HCDs oopsed if we freed or resubmitted urbs from
 * completion callbacks.  2.5 should have fixed those bugs...
 */

static void defer_bh (struct usbnet *dev, struct sk_buff *skb)
{
	struct sk_buff_head	*list = skb->list;
	unsigned long		flags;

	spin_lock_irqsave (&list->lock, flags);
	__skb_unlink (skb, list);
	spin_unlock (&list->lock);
	spin_lock (&dev->done.lock);
	__skb_queue_tail (&dev->done, skb);
	if (dev->done.qlen == 1)
		tasklet_schedule (&dev->bh);
	spin_unlock_irqrestore (&dev->done.lock, flags);
}

/* some work can't be done in tasklets, so we use keventd
 *
 * NOTE:  annoying asymmetry:  if it's active, schedule_work() fails,
 * but tasklet_schedule() doesn't.  hope the failure is rare.
 */
static void defer_kevent (struct usbnet *dev, int work)
{
	set_bit (work, &dev->flags);
	if (!schedule_work (&dev->kevent))
		deverr (dev, "kevent %d may have been dropped", work);
	else
		devdbg (dev, "kevent %d scheduled", work);
}

/*-------------------------------------------------------------------------*/

static void rx_complete (struct urb *urb, struct pt_regs *regs);

static void rx_submit (struct usbnet *dev, struct urb *urb, int flags)
{
	struct sk_buff		*skb;
	struct skb_data		*entry;
	int			retval = 0;
	unsigned long		lockflags;
	size_t			size;

#ifdef CONFIG_USB_NET1080
	if (dev->driver_info->flags & FLAG_FRAMING_NC)
		size = FRAMED_SIZE (dev->net->mtu);
	else
#endif
#ifdef CONFIG_USB_GENESYS
	if (dev->driver_info->flags & FLAG_FRAMING_GL)
		size = GL_RCV_BUF_SIZE;
	else
#endif
#ifdef CONFIG_USB_ZAURUS
	if (dev->driver_info->flags & FLAG_FRAMING_Z)
		size = 6 + (sizeof (struct ethhdr) + dev->net->mtu);
	else
#endif
#ifdef CONFIG_USB_RNDIS
	if (dev->driver_info->flags & FLAG_FRAMING_RN)
		size = RNDIS_MAX_TRANSFER;
	else
#endif
#ifdef CONFIG_USB_AX8817X
	if (dev->driver_info->flags & FLAG_FRAMING_AX)
		size = 2048;
	else
#endif
		size = (sizeof (struct ethhdr) + dev->net->mtu);

	if ((skb = alloc_skb (size + NET_IP_ALIGN, flags)) == NULL) {
		if (netif_msg_rx_err (dev))
			devdbg (dev, "no rx skb");
		defer_kevent (dev, EVENT_RX_MEMORY);
		usb_free_urb (urb);
		return;
	}
	skb_reserve (skb, NET_IP_ALIGN);

	entry = (struct skb_data *) skb->cb;
	entry->urb = urb;
	entry->dev = dev;
	entry->state = rx_start;
	entry->length = 0;

	usb_fill_bulk_urb (urb, dev->udev, dev->in,
		skb->data, size, rx_complete, skb);
	urb->transfer_flags |= URB_ASYNC_UNLINK;

	spin_lock_irqsave (&dev->rxq.lock, lockflags);

	if (netif_running (dev->net)
			&& netif_device_present (dev->net)
			&& !test_bit (EVENT_RX_HALT, &dev->flags)) {
		switch (retval = usb_submit_urb (urb, GFP_ATOMIC)){ 
		case -EPIPE:
			defer_kevent (dev, EVENT_RX_HALT);
			break;
		case -ENOMEM:
			defer_kevent (dev, EVENT_RX_MEMORY);
			break;
		case -ENODEV:
			if (netif_msg_ifdown (dev))
				devdbg (dev, "device gone");
			netif_device_detach (dev->net);
			break;
		default:
			if (netif_msg_rx_err (dev))
				devdbg (dev, "rx submit, %d", retval);
			tasklet_schedule (&dev->bh);
			break;
		case 0:
			__skb_queue_tail (&dev->rxq, skb);
		}
	} else {
		if (netif_msg_ifdown (dev))
			devdbg (dev, "rx: stopped");
		retval = -ENOLINK;
	}
	spin_unlock_irqrestore (&dev->rxq.lock, lockflags);
	if (retval) {
		dev_kfree_skb_any (skb);
		usb_free_urb (urb);
	}
}


/*-------------------------------------------------------------------------*/

static inline void rx_process (struct usbnet *dev, struct sk_buff *skb)
{
	if (dev->driver_info->rx_fixup
			&& !dev->driver_info->rx_fixup (dev, skb))
		goto error;
	// else network stack removes extra byte if we forced a short packet

	if (skb->len)
		skb_return (dev, skb);
	else {
		if (netif_msg_rx_err (dev))
			devdbg (dev, "drop");
error:
		dev->stats.rx_errors++;
		skb_queue_tail (&dev->done, skb);
	}
}

/*-------------------------------------------------------------------------*/

static void rx_complete (struct urb *urb, struct pt_regs *regs)
{
	struct sk_buff		*skb = (struct sk_buff *) urb->context;
	struct skb_data		*entry = (struct skb_data *) skb->cb;
	struct usbnet		*dev = entry->dev;
	int			urb_status = urb->status;

	skb_put (skb, urb->actual_length);
	entry->state = rx_done;
	entry->urb = NULL;

	switch (urb_status) {
	    // success
	    case 0:
		if (MIN_PACKET > skb->len || skb->len > MAX_PACKET) {
			entry->state = rx_cleanup;
			dev->stats.rx_errors++;
			dev->stats.rx_length_errors++;
			if (netif_msg_rx_err (dev))
				devdbg (dev, "rx length %d", skb->len);
		}
		break;

	    // stalls need manual reset. this is rare ... except that
	    // when going through USB 2.0 TTs, unplug appears this way.
	    // we avoid the highspeed version of the ETIMEOUT/EILSEQ
	    // storm, recovering as needed.
	    case -EPIPE:
		dev->stats.rx_errors++;
		defer_kevent (dev, EVENT_RX_HALT);
		// FALLTHROUGH

	    // software-driven interface shutdown
	    case -ECONNRESET:		// async unlink
	    case -ESHUTDOWN:		// hardware gone
		if (netif_msg_ifdown (dev))
			devdbg (dev, "rx shutdown, code %d", urb_status);
		goto block;

	    // we get controller i/o faults during khubd disconnect() delays.
	    // throttle down resubmits, to avoid log floods; just temporarily,
	    // so we still recover when the fault isn't a khubd delay.
	    case -EPROTO:		// ehci
	    case -ETIMEDOUT:		// ohci
	    case -EILSEQ:		// uhci
		dev->stats.rx_errors++;
		if (!timer_pending (&dev->delay)) {
			mod_timer (&dev->delay, jiffies + THROTTLE_JIFFIES);
			if (netif_msg_link (dev))
				devdbg (dev, "rx throttle %d", urb_status);
		}
block:
		entry->state = rx_cleanup;
		entry->urb = urb;
		urb = NULL;
		break;

	    // data overrun ... flush fifo?
	    case -EOVERFLOW:
		dev->stats.rx_over_errors++;
		// FALLTHROUGH
	    
	    default:
		entry->state = rx_cleanup;
		dev->stats.rx_errors++;
		if (netif_msg_rx_err (dev))
			devdbg (dev, "rx status %d", urb_status);
		break;
	}

	defer_bh (dev, skb);

	if (urb) {
		if (netif_running (dev->net)
				&& !test_bit (EVENT_RX_HALT, &dev->flags)) {
			rx_submit (dev, urb, GFP_ATOMIC);
			return;
		}
		usb_free_urb (urb);
	}
	if (netif_msg_rx_err (dev))
		devdbg (dev, "no read resubmitted");
}

static void intr_complete (struct urb *urb, struct pt_regs *regs)
{
	struct usbnet	*dev = urb->context;
	int		status = urb->status;

	switch (status) {
	    /* success */
	    case 0:
		dev->driver_info->status(dev, urb);
		break;

	    /* software-driven interface shutdown */
	    case -ENOENT:		// urb killed
	    case -ESHUTDOWN:		// hardware gone
		if (netif_msg_ifdown (dev))
			devdbg (dev, "intr shutdown, code %d", status);
		return;

	    /* NOTE:  not throttling like RX/TX, since this endpoint
	     * already polls infrequently
	     */
	    default:
		devdbg (dev, "intr status %d", status);
		break;
	}

	if (!netif_running (dev->net))
		return;

	memset(urb->transfer_buffer, 0, urb->transfer_buffer_length);
	status = usb_submit_urb (urb, GFP_ATOMIC);
	if (status != 0 && netif_msg_timer (dev))
		deverr(dev, "intr resubmit --> %d", status);
}

/*-------------------------------------------------------------------------*/

// unlink pending rx/tx; completion handlers do all other cleanup

static int unlink_urbs (struct usbnet *dev, struct sk_buff_head *q)
{
	unsigned long		flags;
	struct sk_buff		*skb, *skbnext;
	int			count = 0;

	spin_lock_irqsave (&q->lock, flags);
	for (skb = q->next; skb != (struct sk_buff *) q; skb = skbnext) {
		struct skb_data		*entry;
		struct urb		*urb;
		int			retval;

		entry = (struct skb_data *) skb->cb;
		urb = entry->urb;
		skbnext = skb->next;

		// during some PM-driven resume scenarios,
		// these (async) unlinks complete immediately
		retval = usb_unlink_urb (urb);
		if (retval != -EINPROGRESS && retval != 0)
			devdbg (dev, "unlink urb err, %d", retval);
		else
			count++;
	}
	spin_unlock_irqrestore (&q->lock, flags);
	return count;
}


/*-------------------------------------------------------------------------*/

// precondition: never called in_interrupt

static int usbnet_stop (struct net_device *net)
{
	struct usbnet		*dev = netdev_priv(net);
	int			temp;
	DECLARE_WAIT_QUEUE_HEAD (unlink_wakeup); 
	DECLARE_WAITQUEUE (wait, current);

	netif_stop_queue (net);

	if (netif_msg_ifdown (dev))
		devinfo (dev, "stop stats: rx/tx %ld/%ld, errs %ld/%ld",
			dev->stats.rx_packets, dev->stats.tx_packets, 
			dev->stats.rx_errors, dev->stats.tx_errors
			);

	// ensure there are no more active urbs
	add_wait_queue (&unlink_wakeup, &wait);
	dev->wait = &unlink_wakeup;
	temp = unlink_urbs (dev, &dev->txq) + unlink_urbs (dev, &dev->rxq);

	// maybe wait for deletions to finish.
	while (!skb_queue_empty(&dev->rxq) &&
	       !skb_queue_empty(&dev->txq) &&
	       !skb_queue_empty(&dev->done)) {
		msleep(UNLINK_TIMEOUT_MS);
		if (netif_msg_ifdown (dev))
			devdbg (dev, "waited for %d urb completions", temp);
	}
	dev->wait = NULL;
	remove_wait_queue (&unlink_wakeup, &wait); 

	usb_kill_urb(dev->interrupt);

	/* deferred work (task, timer, softirq) must also stop.
	 * can't flush_scheduled_work() until we drop rtnl (later),
	 * else workers could deadlock; so make workers a NOP.
	 */
	dev->flags = 0;
	del_timer_sync (&dev->delay);
	tasklet_kill (&dev->bh);

	return 0;
}

/*-------------------------------------------------------------------------*/

// posts reads, and enables write queuing

// precondition: never called in_interrupt

static int usbnet_open (struct net_device *net)
{
	struct usbnet		*dev = netdev_priv(net);
	int			retval = 0;
	struct driver_info	*info = dev->driver_info;

	// put into "known safe" state
	if (info->reset && (retval = info->reset (dev)) < 0) {
		if (netif_msg_ifup (dev))
			devinfo (dev,
				"open reset fail (%d) usbnet usb-%s-%s, %s",
				retval,
				dev->udev->bus->bus_name, dev->udev->devpath,
			info->description);
		goto done;
	}

	// insist peer be connected
	if (info->check_connect && (retval = info->check_connect (dev)) < 0) {
		if (netif_msg_ifup (dev))
			devdbg (dev, "can't open; %d", retval);
		goto done;
	}

	/* start any status interrupt transfer */
	if (dev->interrupt) {
		retval = usb_submit_urb (dev->interrupt, GFP_KERNEL);
		if (retval < 0) {
			if (netif_msg_ifup (dev))
				deverr (dev, "intr submit %d", retval);
			goto done;
		}
	}

	netif_start_queue (net);
	if (netif_msg_ifup (dev)) {
		char	*framing;

		if (dev->driver_info->flags & FLAG_FRAMING_NC)
			framing = "NetChip";
		else if (dev->driver_info->flags & FLAG_FRAMING_GL)
			framing = "GeneSys";
		else if (dev->driver_info->flags & FLAG_FRAMING_Z)
			framing = "Zaurus";
		else if (dev->driver_info->flags & FLAG_FRAMING_RN)
			framing = "RNDIS";
		else if (dev->driver_info->flags & FLAG_FRAMING_AX)
			framing = "ASIX";
		else
			framing = "simple";

		devinfo (dev, "open: enable queueing "
				"(rx %d, tx %d) mtu %d %s framing",
			RX_QLEN (dev), TX_QLEN (dev), dev->net->mtu,
			framing);
	}

	// delay posting reads until we're fully open
	tasklet_schedule (&dev->bh);
done:
	return retval;
}

/*-------------------------------------------------------------------------*/

static void usbnet_get_drvinfo (struct net_device *net, struct ethtool_drvinfo *info)
{
	struct usbnet *dev = netdev_priv(net);

	strncpy (info->driver, driver_name, sizeof info->driver);
	strncpy (info->version, DRIVER_VERSION, sizeof info->version);
	strncpy (info->fw_version, dev->driver_info->description,
		sizeof info->fw_version);
	usb_make_path (dev->udev, info->bus_info, sizeof info->bus_info);
}

static u32 usbnet_get_link (struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);

	/* If a check_connect is defined, return it's results */
	if (dev->driver_info->check_connect)
		return dev->driver_info->check_connect (dev) == 0;

	/* Otherwise, we're up to avoid breaking scripts */
	return 1;
}

static u32 usbnet_get_msglevel (struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);

	return dev->msg_enable;
}

static void usbnet_set_msglevel (struct net_device *net, u32 level)
{
	struct usbnet *dev = netdev_priv(net);

	dev->msg_enable = level;
}

static int usbnet_ioctl (struct net_device *net, struct ifreq *rq, int cmd)
{
#ifdef NEED_MII
	{
	struct usbnet *dev = netdev_priv(net);

	if (dev->mii.mdio_read != NULL && dev->mii.mdio_write != NULL)
		return generic_mii_ioctl(&dev->mii, if_mii(rq), cmd, NULL);
	}
#endif
	return -EOPNOTSUPP;
}

/*-------------------------------------------------------------------------*/

/* work that cannot be done in interrupt context uses keventd.
 *
 * NOTE:  with 2.5 we could do more of this using completion callbacks,
 * especially now that control transfers can be queued.
 */
static void
kevent (void *data)
{
	struct usbnet		*dev = data;
	int			status;

	/* usb_clear_halt() needs a thread context */
	if (test_bit (EVENT_TX_HALT, &dev->flags)) {
		unlink_urbs (dev, &dev->txq);
		status = usb_clear_halt (dev->udev, dev->out);
		if (status < 0 && status != -EPIPE) {
			if (netif_msg_tx_err (dev))
				deverr (dev, "can't clear tx halt, status %d",
					status);
		} else {
			clear_bit (EVENT_TX_HALT, &dev->flags);
			netif_wake_queue (dev->net);
		}
	}
	if (test_bit (EVENT_RX_HALT, &dev->flags)) {
		unlink_urbs (dev, &dev->rxq);
		status = usb_clear_halt (dev->udev, dev->in);
		if (status < 0 && status != -EPIPE) {
			if (netif_msg_rx_err (dev))
				deverr (dev, "can't clear rx halt, status %d",
					status);
		} else {
			clear_bit (EVENT_RX_HALT, &dev->flags);
			tasklet_schedule (&dev->bh);
		}
	}

	/* tasklet could resubmit itself forever if memory is tight */
	if (test_bit (EVENT_RX_MEMORY, &dev->flags)) {
		struct urb	*urb = NULL;

		if (netif_running (dev->net))
			urb = usb_alloc_urb (0, GFP_KERNEL);
		else
			clear_bit (EVENT_RX_MEMORY, &dev->flags);
		if (urb != NULL) {
			clear_bit (EVENT_RX_MEMORY, &dev->flags);
			rx_submit (dev, urb, GFP_KERNEL);
			tasklet_schedule (&dev->bh);
		}
	}

	if (test_bit (EVENT_LINK_RESET, &dev->flags)) {
		struct driver_info 	*info = dev->driver_info;
		int			retval = 0;

		clear_bit (EVENT_LINK_RESET, &dev->flags);
		if(info->link_reset && (retval = info->link_reset(dev)) < 0) {
			devinfo(dev, "link reset failed (%d) usbnet usb-%s-%s, %s",
				retval,
				dev->udev->bus->bus_name, dev->udev->devpath,
				info->description);
		}
	}

	if (dev->flags)
		devdbg (dev, "kevent done, flags = 0x%lx",
			dev->flags);
}

/*-------------------------------------------------------------------------*/

static void tx_complete (struct urb *urb, struct pt_regs *regs)
{
	struct sk_buff		*skb = (struct sk_buff *) urb->context;
	struct skb_data		*entry = (struct skb_data *) skb->cb;
	struct usbnet		*dev = entry->dev;

	if (urb->status == 0) {
		dev->stats.tx_packets++;
		dev->stats.tx_bytes += entry->length;
	} else {
		dev->stats.tx_errors++;

		switch (urb->status) {
		case -EPIPE:
			defer_kevent (dev, EVENT_TX_HALT);
			break;

		/* software-driven interface shutdown */
		case -ECONNRESET:		// async unlink
		case -ESHUTDOWN:		// hardware gone
			break;

		// like rx, tx gets controller i/o faults during khubd delays
		// and so it uses the same throttling mechanism.
		case -EPROTO:		// ehci
		case -ETIMEDOUT:	// ohci
		case -EILSEQ:		// uhci
			if (!timer_pending (&dev->delay)) {
				mod_timer (&dev->delay,
					jiffies + THROTTLE_JIFFIES);
				if (netif_msg_link (dev))
					devdbg (dev, "tx throttle %d",
							urb->status);
			}
			netif_stop_queue (dev->net);
			break;
		default:
			if (netif_msg_tx_err (dev))
				devdbg (dev, "tx err %d", entry->urb->status);
			break;
		}
	}

	urb->dev = NULL;
	entry->state = tx_done;
	defer_bh (dev, skb);
}

/*-------------------------------------------------------------------------*/

static void usbnet_tx_timeout (struct net_device *net)
{
	struct usbnet		*dev = netdev_priv(net);

	unlink_urbs (dev, &dev->txq);
	tasklet_schedule (&dev->bh);

	// FIXME: device recovery -- reset?
}

/*-------------------------------------------------------------------------*/

static int usbnet_start_xmit (struct sk_buff *skb, struct net_device *net)
{
	struct usbnet		*dev = netdev_priv(net);
	int			length;
	int			retval = NET_XMIT_SUCCESS;
	struct urb		*urb = NULL;
	struct skb_data		*entry;
	struct driver_info	*info = dev->driver_info;
	unsigned long		flags;
#ifdef	CONFIG_USB_NET1080
	struct nc_header	*header = NULL;
	struct nc_trailer	*trailer = NULL;
#endif	/* CONFIG_USB_NET1080 */

	// some devices want funky USB-level framing, for
	// win32 driver (usually) and/or hardware quirks
	if (info->tx_fixup) {
		skb = info->tx_fixup (dev, skb, GFP_ATOMIC);
		if (!skb) {
			if (netif_msg_tx_err (dev))
				devdbg (dev, "can't tx_fixup skb");
			goto drop;
		}
	}
	length = skb->len;

	if (!(urb = usb_alloc_urb (0, GFP_ATOMIC))) {
		if (netif_msg_tx_err (dev))
			devdbg (dev, "no urb");
		goto drop;
	}

	entry = (struct skb_data *) skb->cb;
	entry->urb = urb;
	entry->dev = dev;
	entry->state = tx_start;
	entry->length = length;

	// FIXME: reorganize a bit, so that fixup() fills out NetChip
	// framing too. (Packet ID update needs the spinlock...)
	// [ BETTER:  we already own net->xmit_lock, that's enough ]

#ifdef	CONFIG_USB_NET1080
	if (info->flags & FLAG_FRAMING_NC) {
		header = (struct nc_header *) skb_push (skb, sizeof *header);
		header->hdr_len = cpu_to_le16 (sizeof (*header));
		header->packet_len = cpu_to_le16 (length);
		if (!((skb->len + sizeof *trailer) & 0x01))
			*skb_put (skb, 1) = PAD_BYTE;
		trailer = (struct nc_trailer *) skb_put (skb, sizeof *trailer);
	}
#endif	/* CONFIG_USB_NET1080 */

	usb_fill_bulk_urb (urb, dev->udev, dev->out,
			skb->data, skb->len, tx_complete, skb);
	urb->transfer_flags |= URB_ASYNC_UNLINK;

	/* don't assume the hardware handles USB_ZERO_PACKET
	 * NOTE:  strictly conforming cdc-ether devices should expect
	 * the ZLP here, but ignore the one-byte packet.
	 *
	 * FIXME zero that byte, if it doesn't require a new skb.
	 */
	if ((length % dev->maxpacket) == 0)
		urb->transfer_buffer_length++;

	spin_lock_irqsave (&dev->txq.lock, flags);

#ifdef	CONFIG_USB_NET1080
	if (info->flags & FLAG_FRAMING_NC) {
		header->packet_id = cpu_to_le16 ((u16)dev->dev_packet_id++);
		put_unaligned (header->packet_id, &trailer->packet_id);
#if 0
		devdbg (dev, "frame >tx h %d p %d id %d",
			header->hdr_len, header->packet_len,
			header->packet_id);
#endif
	}
#endif	/* CONFIG_USB_NET1080 */

	switch ((retval = usb_submit_urb (urb, GFP_ATOMIC))) {
	case -EPIPE:
		netif_stop_queue (net);
		defer_kevent (dev, EVENT_TX_HALT);
		break;
	default:
		if (netif_msg_tx_err (dev))
			devdbg (dev, "tx: submit urb err %d", retval);
		break;
	case 0:
		net->trans_start = jiffies;
		__skb_queue_tail (&dev->txq, skb);
		if (dev->txq.qlen >= TX_QLEN (dev))
			netif_stop_queue (net);
	}
	spin_unlock_irqrestore (&dev->txq.lock, flags);

	if (retval) {
		if (netif_msg_tx_err (dev))
			devdbg (dev, "drop, code %d", retval);
drop:
		retval = NET_XMIT_SUCCESS;
		dev->stats.tx_dropped++;
		if (skb)
			dev_kfree_skb_any (skb);
		usb_free_urb (urb);
	} else if (netif_msg_tx_queued (dev)) {
		devdbg (dev, "> tx, len %d, type 0x%x",
			length, skb->protocol);
	}
	return retval;
}


/*-------------------------------------------------------------------------*/

// tasklet (work deferred from completions, in_irq) or timer

static void usbnet_bh (unsigned long param)
{
	struct usbnet		*dev = (struct usbnet *) param;
	struct sk_buff		*skb;
	struct skb_data		*entry;

	while ((skb = skb_dequeue (&dev->done))) {
		entry = (struct skb_data *) skb->cb;
		switch (entry->state) {
		    case rx_done:
			entry->state = rx_cleanup;
			rx_process (dev, skb);
			continue;
		    case tx_done:
		    case rx_cleanup:
			usb_free_urb (entry->urb);
			dev_kfree_skb (skb);
			continue;
		    default:
			devdbg (dev, "bogus skb state %d", entry->state);
		}
	}

	// waiting for all pending urbs to complete?
	if (dev->wait) {
		if ((dev->txq.qlen + dev->rxq.qlen + dev->done.qlen) == 0) {
			wake_up (dev->wait);
		}

	// or are we maybe short a few urbs?
	} else if (netif_running (dev->net)
			&& netif_device_present (dev->net)
			&& !timer_pending (&dev->delay)
			&& !test_bit (EVENT_RX_HALT, &dev->flags)) {
		int	temp = dev->rxq.qlen;
		int	qlen = RX_QLEN (dev);

		if (temp < qlen) {
			struct urb	*urb;
			int		i;

			// don't refill the queue all at once
			for (i = 0; i < 10 && dev->rxq.qlen < qlen; i++) {
				urb = usb_alloc_urb (0, GFP_ATOMIC);
				if (urb != NULL)
					rx_submit (dev, urb, GFP_ATOMIC);
			}
			if (temp != dev->rxq.qlen && netif_msg_link (dev))
				devdbg (dev, "rxqlen %d --> %d",
						temp, dev->rxq.qlen);
			if (dev->rxq.qlen < qlen)
				tasklet_schedule (&dev->bh);
		}
		if (dev->txq.qlen < TX_QLEN (dev))
			netif_wake_queue (dev->net);
	}
}



/*-------------------------------------------------------------------------
 *
 * USB Device Driver support
 *
 *-------------------------------------------------------------------------*/
 
// precondition: never called in_interrupt

static void usbnet_disconnect (struct usb_interface *intf)
{
	struct usbnet		*dev;
	struct usb_device	*xdev;
	struct net_device	*net;

	dev = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);
	if (!dev)
		return;

	xdev = interface_to_usbdev (intf);

	if (netif_msg_probe (dev))
		devinfo (dev, "unregister usbnet usb-%s-%s, %s",
			xdev->bus->bus_name, xdev->devpath,
			dev->driver_info->description);
	
	net = dev->net;
	unregister_netdev (net);

	/* we don't hold rtnl here ... */
	flush_scheduled_work ();

	if (dev->driver_info->unbind)
		dev->driver_info->unbind (dev, intf);

	free_netdev(net);
	usb_put_dev (xdev);
}


/*-------------------------------------------------------------------------*/

static struct ethtool_ops usbnet_ethtool_ops;

// precondition: never called in_interrupt

static int
usbnet_probe (struct usb_interface *udev, const struct usb_device_id *prod)
{
	struct usbnet			*dev;
	struct net_device 		*net;
	struct usb_host_interface	*interface;
	struct driver_info		*info;
	struct usb_device		*xdev;
	int				status;

	info = (struct driver_info *) prod->driver_info;
	if (!info) {
		dev_dbg (&udev->dev, "blacklisted by %s\n", driver_name);
		return -ENODEV;
	}
	xdev = interface_to_usbdev (udev);
	interface = udev->cur_altsetting;

	usb_get_dev (xdev);

	status = -ENOMEM;

	// set up our own records
	net = alloc_etherdev(sizeof(*dev));
	if (!net) {
		dbg ("can't kmalloc dev");
		goto out;
	}

	dev = netdev_priv(net);
	dev->udev = xdev;
	dev->driver_info = info;
	dev->msg_enable = netif_msg_init (msg_level, NETIF_MSG_DRV
				| NETIF_MSG_PROBE | NETIF_MSG_LINK);
	skb_queue_head_init (&dev->rxq);
	skb_queue_head_init (&dev->txq);
	skb_queue_head_init (&dev->done);
	dev->bh.func = usbnet_bh;
	dev->bh.data = (unsigned long) dev;
	INIT_WORK (&dev->kevent, kevent, dev);
	dev->delay.function = usbnet_bh;
	dev->delay.data = (unsigned long) dev;
	init_timer (&dev->delay);

	SET_MODULE_OWNER (net);
	dev->net = net;
	strcpy (net->name, "usb%d");
	memcpy (net->dev_addr, node_id, sizeof node_id);

#if 0
// dma_supported() is deeply broken on almost all architectures
	// possible with some EHCI controllers
	if (dma_supported (&udev->dev, DMA_64BIT_MASK))
		net->features |= NETIF_F_HIGHDMA;
#endif

	net->change_mtu = usbnet_change_mtu;
	net->get_stats = usbnet_get_stats;
	net->hard_start_xmit = usbnet_start_xmit;
	net->open = usbnet_open;
	net->stop = usbnet_stop;
	net->watchdog_timeo = TX_TIMEOUT_JIFFIES;
	net->tx_timeout = usbnet_tx_timeout;
	net->do_ioctl = usbnet_ioctl;
	net->ethtool_ops = &usbnet_ethtool_ops;

	// allow device-specific bind/init procedures
	// NOTE net->name still not usable ...
	if (info->bind) {
		status = info->bind (dev, udev);
		// heuristic:  "usb%d" for links we know are two-host,
		// else "eth%d" when there's reasonable doubt.  userspace
		// can rename the link if it knows better.
		if ((dev->driver_info->flags & FLAG_ETHER) != 0
				&& (net->dev_addr [0] & 0x02) == 0)
			strcpy (net->name, "eth%d");
	} else if (!info->in || info->out)
		status = get_endpoints (dev, udev);
	else {
		dev->in = usb_rcvbulkpipe (xdev, info->in);
		dev->out = usb_sndbulkpipe (xdev, info->out);
		if (!(info->flags & FLAG_NO_SETINT))
			status = usb_set_interface (xdev,
				interface->desc.bInterfaceNumber,
				interface->desc.bAlternateSetting);
		else
			status = 0;

	}

	if (status == 0 && dev->status)
		status = init_status (dev, udev);
	if (status < 0)
		goto out1;

	dev->maxpacket = usb_maxpacket (dev->udev, dev->out, 1);
	
	SET_NETDEV_DEV(net, &udev->dev);
	status = register_netdev (net);
	if (status)
		goto out3;
	if (netif_msg_probe (dev))
		devinfo (dev, "register usbnet at usb-%s-%s, %s, "
				"%02x:%02x:%02x:%02x:%02x:%02x",
			xdev->bus->bus_name, xdev->devpath,
			dev->driver_info->description,
			net->dev_addr [0], net->dev_addr [1],
			net->dev_addr [2], net->dev_addr [3],
			net->dev_addr [4], net->dev_addr [5]);

	// ok, it's ready to go.
	usb_set_intfdata (udev, dev);

	// start as if the link is up
	netif_device_attach (net);

	return 0;

out3:
	if (info->unbind)
		info->unbind (dev, udev);
out1:
	free_netdev(net);
out:
	usb_put_dev(xdev);
	return status;
}

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_PM

static int usbnet_suspend (struct usb_interface *intf, pm_message_t message)
{
	struct usbnet		*dev = usb_get_intfdata(intf);
	
	/* accelerate emptying of the rx and queues, to avoid
	 * having everything error out.
	 */
	netif_device_detach (dev->net);
	(void) unlink_urbs (dev, &dev->rxq);
	(void) unlink_urbs (dev, &dev->txq);
	intf->dev.power.power_state = PMSG_SUSPEND;
	return 0;
}

static int usbnet_resume (struct usb_interface *intf)
{
	struct usbnet		*dev = usb_get_intfdata(intf);

	intf->dev.power.power_state = PMSG_ON;
	netif_device_attach (dev->net);
	tasklet_schedule (&dev->bh);
	return 0;
}

#else	/* !CONFIG_PM */

#define	usbnet_suspend	NULL
#define	usbnet_resume	NULL

#endif	/* CONFIG_PM */

/*-------------------------------------------------------------------------*/

#ifndef	HAVE_HARDWARE
#error You need to configure some hardware for this driver
#endif

/*
 * chip vendor names won't normally be on the cables, and
 * may not be on the device.
 */

static const struct usb_device_id	products [] = {

#ifdef	CONFIG_USB_ALI_M5632
{
	USB_DEVICE (0x0402, 0x5632),	// ALi defaults
	.driver_info =	(unsigned long) &ali_m5632_info,
},
#endif

#ifdef	CONFIG_USB_AN2720
{
	USB_DEVICE (0x0547, 0x2720),	// AnchorChips defaults
	.driver_info =	(unsigned long) &an2720_info,
}, {
	USB_DEVICE (0x0547, 0x2727),	// Xircom PGUNET
	.driver_info =	(unsigned long) &an2720_info,
},
#endif

#ifdef	CONFIG_USB_BELKIN
{
	USB_DEVICE (0x050d, 0x0004),	// Belkin
	.driver_info =	(unsigned long) &belkin_info,
}, {
	USB_DEVICE (0x056c, 0x8100),	// eTEK
	.driver_info =	(unsigned long) &belkin_info,
}, {
	USB_DEVICE (0x0525, 0x9901),	// Advance USBNET (eTEK)
	.driver_info =	(unsigned long) &belkin_info,
},
#endif

#ifdef CONFIG_USB_AX8817X
{
	// Linksys USB200M
	USB_DEVICE (0x077b, 0x2226),
	.driver_info =	(unsigned long) &ax8817x_info,
}, {
	// Netgear FA120
	USB_DEVICE (0x0846, 0x1040),
	.driver_info =  (unsigned long) &netgear_fa120_info,
}, {
	// DLink DUB-E100
	USB_DEVICE (0x2001, 0x1a00),
	.driver_info =  (unsigned long) &dlink_dub_e100_info,
}, {
	// Intellinet, ST Lab USB Ethernet
	USB_DEVICE (0x0b95, 0x1720),
	.driver_info =  (unsigned long) &ax8817x_info,
}, {
	// Hawking UF200, TrendNet TU2-ET100
	USB_DEVICE (0x07b8, 0x420a),
	.driver_info =  (unsigned long) &hawking_uf200_info,
}, {
        // Billionton Systems, USB2AR 
        USB_DEVICE (0x08dd, 0x90ff),
        .driver_info =  (unsigned long) &ax8817x_info,
}, {
	// ATEN UC210T
	USB_DEVICE (0x0557, 0x2009),
	.driver_info =  (unsigned long) &ax8817x_info,
}, {
	// Buffalo LUA-U2-KTX
	USB_DEVICE (0x0411, 0x003d),
	.driver_info =  (unsigned long) &ax8817x_info,
}, {
	// Sitecom LN-029 "USB 2.0 10/100 Ethernet adapter"
	USB_DEVICE (0x6189, 0x182d),
	.driver_info =  (unsigned long) &ax8817x_info,
}, {
	// corega FEther USB2-TX
	USB_DEVICE (0x07aa, 0x0017),
	.driver_info =  (unsigned long) &ax8817x_info,
}, {
	// Surecom EP-1427X-2
	USB_DEVICE (0x1189, 0x0893),
	.driver_info = (unsigned long) &ax8817x_info,
}, {
	// goodway corp usb gwusb2e
	USB_DEVICE (0x1631, 0x6200),
	.driver_info = (unsigned long) &ax8817x_info,
}, {
	// ASIX AX88772 10/100
        USB_DEVICE (0x0b95, 0x7720),
        .driver_info = (unsigned long) &ax88772_info,
},
#endif

#ifdef	CONFIG_USB_EPSON2888
{
	USB_DEVICE (0x0525, 0x2888),	// EPSON USB client
	.driver_info	= (unsigned long) &epson2888_info,
},
#endif

#ifdef	CONFIG_USB_GENESYS
{
	USB_DEVICE (0x05e3, 0x0502),	// GL620USB-A
	.driver_info =	(unsigned long) &genelink_info,
},
	/* NOT: USB_DEVICE (0x05e3, 0x0501),	// GL620USB
	 * that's half duplex, not currently supported
	 */
#endif

#ifdef	CONFIG_USB_NET1080
{
	USB_DEVICE (0x0525, 0x1080),	// NetChip ref design
	.driver_info =	(unsigned long) &net1080_info,
}, {
	USB_DEVICE (0x06D0, 0x0622),	// Laplink Gold
	.driver_info =	(unsigned long) &net1080_info,
},
#endif

#ifdef CONFIG_USB_PL2301
{
	USB_DEVICE (0x067b, 0x0000),	// PL-2301
	.driver_info =	(unsigned long) &prolific_info,
}, {
	USB_DEVICE (0x067b, 0x0001),	// PL-2302
	.driver_info =	(unsigned long) &prolific_info,
},
#endif

#ifdef CONFIG_USB_KC2190
{
	USB_DEVICE (0x050f, 0x0190),	// KC-190
	.driver_info =	(unsigned long) &kc2190_info,
},
#endif

#ifdef	CONFIG_USB_RNDIS
{
	/* RNDIS is MSFT's un-official variant of CDC ACM */
	USB_INTERFACE_INFO (USB_CLASS_COMM, 2 /* ACM */, 0x0ff),
	.driver_info = (unsigned long) &rndis_info,
},
#endif

#ifdef	CONFIG_USB_ARMLINUX
/*
 * SA-1100 using standard ARM Linux kernels, or compatible.
 * Often used when talking to Linux PDAs (iPaq, Yopy, etc).
 * The sa-1100 "usb-eth" driver handles the basic framing.
 *
 * PXA25x or PXA210 ...  these use a "usb-eth" driver much like
 * the sa1100 one, but hardware uses different endpoint numbers.
 *
 * Or the Linux "Ethernet" gadget on hardware that can't talk
 * CDC Ethernet (e.g., no altsettings), in either of two modes:
 *  - acting just like the old "usb-eth" firmware, though
 *    the implementation is different 
 *  - supporting RNDIS as the first/default configuration for
 *    MS-Windows interop; Linux needs to use the other config
 */
{
	// 1183 = 0x049F, both used as hex values?
	// Compaq "Itsy" vendor/product id
	USB_DEVICE (0x049F, 0x505A),	// usb-eth, or compatible
	.driver_info =	(unsigned long) &linuxdev_info,
}, {
	USB_DEVICE (0x0E7E, 0x1001),	// G.Mate "Yopy"
	.driver_info =	(unsigned long) &yopy_info,
}, {
	USB_DEVICE (0x8086, 0x07d3),	// "blob" bootloader
	.driver_info =	(unsigned long) &blob_info,
}, {
	// Linux Ethernet/RNDIS gadget on pxa210/25x/26x
	// e.g. Gumstix, current OpenZaurus, ...
	USB_DEVICE_VER (0x0525, 0xa4a2, 0x0203, 0x0203),
	.driver_info =	(unsigned long) &linuxdev_info,
}, 
#endif

#if	defined(CONFIG_USB_ZAURUS) || defined(CONFIG_USB_CDCETHER)
/*
 * SA-1100 based Sharp Zaurus ("collie"), or compatible.
 * Same idea as above, but different framing.
 *
 * PXA-2xx based models are also lying-about-cdc.
 * Some models don't even tell the same lies ...
 *
 * NOTE:  OpenZaurus versions with 2.6 kernels won't use these entries,
 * unlike the older ones with 2.4 "embedix" kernels.
 *
 * NOTE:  These entries do double-duty, serving as blacklist entries
 * whenever Zaurus support isn't enabled, but CDC Ethernet is.
 */
#define	ZAURUS_MASTER_INTERFACE \
	.bInterfaceClass	= USB_CLASS_COMM, \
	.bInterfaceSubClass	= USB_CDC_SUBCLASS_ETHERNET, \
	.bInterfaceProtocol	= USB_CDC_PROTO_NONE
{
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE, 
	.idVendor		= 0x04DD,
	.idProduct		= 0x8004,
	ZAURUS_MASTER_INTERFACE,
	.driver_info = ZAURUS_STRONGARM_INFO,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE, 
	.idVendor		= 0x04DD,
	.idProduct		= 0x8005,	/* A-300 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info = ZAURUS_PXA_INFO,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE, 
	.idVendor		= 0x04DD,
	.idProduct		= 0x8006,	/* B-500/SL-5600 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info = ZAURUS_PXA_INFO,
}, {
	.match_flags    =   USB_DEVICE_ID_MATCH_INT_INFO
	          | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x04DD,
	.idProduct		= 0x8007,	/* C-700 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info = ZAURUS_PXA_INFO,
}, {
	.match_flags    =   USB_DEVICE_ID_MATCH_INT_INFO
		 | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor               = 0x04DD,
	.idProduct              = 0x9031,	/* C-750 C-760 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info = ZAURUS_PXA_INFO,
}, {
	.match_flags    =   USB_DEVICE_ID_MATCH_INT_INFO
		 | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor               = 0x04DD,
	.idProduct              = 0x9032,	/* SL-6000 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info = ZAURUS_PXA_INFO,
}, {
	.match_flags    =   USB_DEVICE_ID_MATCH_INT_INFO
		 | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor               = 0x04DD,
	/* reported with some C860 units */
	.idProduct              = 0x9050,	/* C-860 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info = ZAURUS_PXA_INFO,
},

#ifdef	CONFIG_USB_ZAURUS
	/* At least some (reports vary) PXA units have very different lies
	 * about their standards support:  they claim to be cell phones with
	 * direct access to their radios.  (They don't conform to CDC MDLM.)
	 */
{
	USB_INTERFACE_INFO (USB_CLASS_COMM, USB_CDC_SUBCLASS_MDLM,
			USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long) &bogus_mdlm_info,
},
#endif

/* Olympus has some models with a Zaurus-compatible option.
 * R-1000 uses a FreeScale i.MXL cpu (ARMv4T)
 */
{
	.match_flags    =   USB_DEVICE_ID_MATCH_INT_INFO
		 | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor               = 0x07B4,
	.idProduct              = 0x0F02,	/* R-1000 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info = OLYMPUS_MXL_INFO,
},
#endif

#ifdef	CONFIG_USB_CDCETHER
{
	/* CDC Ether uses two interfaces, not necessarily consecutive.
	 * We match the main interface, ignoring the optional device
	 * class so we could handle devices that aren't exclusively
	 * CDC ether.
	 *
	 * NOTE:  this match must come AFTER entries working around
	 * bugs/quirks in a given product (like Zaurus, above).
	 */
	USB_INTERFACE_INFO (USB_CLASS_COMM, USB_CDC_SUBCLASS_ETHERNET,
			USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long) &cdc_info,
},
#endif

	{ },		// END
};
MODULE_DEVICE_TABLE (usb, products);

static struct usb_driver usbnet_driver = {
	.owner =	THIS_MODULE,
	.name =		driver_name,
	.id_table =	products,
	.probe =	usbnet_probe,
	.disconnect =	usbnet_disconnect,
	.suspend =	usbnet_suspend,
	.resume =	usbnet_resume,
};

/* Default ethtool_ops assigned.  Devices can override in their bind() routine */
static struct ethtool_ops usbnet_ethtool_ops = {
	.get_drvinfo		= usbnet_get_drvinfo,
	.get_link		= usbnet_get_link,
	.get_msglevel		= usbnet_get_msglevel,
	.set_msglevel		= usbnet_set_msglevel,
};

/*-------------------------------------------------------------------------*/

static int __init usbnet_init (void)
{
	// compiler should optimize these out
	BUG_ON (sizeof (((struct sk_buff *)0)->cb)
			< sizeof (struct skb_data));
#ifdef	CONFIG_USB_CDCETHER
	BUG_ON ((sizeof (((struct usbnet *)0)->data)
			< sizeof (struct cdc_state)));
#endif

	random_ether_addr(node_id);

 	return usb_register(&usbnet_driver);
}
module_init (usbnet_init);

static void __exit usbnet_exit (void)
{
 	usb_deregister (&usbnet_driver);
}
module_exit (usbnet_exit);

MODULE_AUTHOR ("David Brownell <dbrownell@users.sourceforge.net>");
MODULE_DESCRIPTION ("USB Host-to-Host Link Drivers (numerous vendors)");
MODULE_LICENSE ("GPL");
