/****************************************************************
 *
 *     kaweth.c - driver for KL5KUSB101 based USB->Ethernet
 *
 *     (c) 2000 Interlan Communications
 *     (c) 2000 Stephane Alnet
 *     (C) 2001 Brad Hards
 *     (C) 2002 Oliver Neukum
 *
 *     Original author: The Zapman <zapman@interlan.net>
 *     Inspired by, and much credit goes to Michael Rothwell
 *     <rothwell@interlan.net> for the test equipment, help, and patience
 *     Based off of (and with thanks to) Petko Manolov's pegaus.c driver.
 *     Also many thanks to Joel Silverman and Ed Surprenant at Kawasaki
 *     for providing the firmware and driver resources.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software Foundation,
 *     Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 ****************************************************************/

/* TODO:
 * Fix in_interrupt() problem
 * Develop test procedures for USB net interfaces
 * Run test procedures
 * Fix bugs from previous two steps
 * Snoop other OSs for any tricks we're not doing
 * SMP locking
 * Reduce arbitrary timeouts
 * Smart multicast support
 * Temporary MAC change support
 * Tunable SOFs parameter - ioctl()?
 * Ethernet stats collection
 * Code formatting improvements
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/usb.h>
#include <linux/types.h>
#include <linux/ethtool.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/wait.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <asm/byteorder.h>

#undef DEBUG

#ifdef DEBUG
#define kaweth_dbg(format, arg...) printk(KERN_DEBUG __FILE__ ": " format "\n" ,##arg)
#else
#define kaweth_dbg(format, arg...) do {} while (0)
#endif
#define kaweth_err(format, arg...) printk(KERN_ERR __FILE__ ": " format "\n" ,##arg)
#define kaweth_info(format, arg...) printk(KERN_INFO __FILE__ ": " format "\n" , ##arg)
#define kaweth_warn(format, arg...) printk(KERN_WARNING __FILE__ ": " format "\n" , ##arg)


#include "kawethfw.h"

#define KAWETH_MTU			1514
#define KAWETH_BUF_SIZE			1664
#define KAWETH_TX_TIMEOUT		(5 * HZ)
#define KAWETH_SCRATCH_SIZE		32
#define KAWETH_FIRMWARE_BUF_SIZE	4096
#define KAWETH_CONTROL_TIMEOUT		(30 * HZ)

#define KAWETH_STATUS_BROKEN		0x0000001
#define KAWETH_STATUS_CLOSING		0x0000002

#define KAWETH_PACKET_FILTER_PROMISCUOUS	0x01
#define KAWETH_PACKET_FILTER_ALL_MULTICAST	0x02
#define KAWETH_PACKET_FILTER_DIRECTED		0x04
#define KAWETH_PACKET_FILTER_BROADCAST		0x08
#define KAWETH_PACKET_FILTER_MULTICAST		0x10

/* Table 7 */
#define KAWETH_COMMAND_GET_ETHERNET_DESC	0x00
#define KAWETH_COMMAND_MULTICAST_FILTERS        0x01
#define KAWETH_COMMAND_SET_PACKET_FILTER	0x02
#define KAWETH_COMMAND_STATISTICS               0x03
#define KAWETH_COMMAND_SET_TEMP_MAC     	0x06
#define KAWETH_COMMAND_GET_TEMP_MAC             0x07
#define KAWETH_COMMAND_SET_URB_SIZE		0x08
#define KAWETH_COMMAND_SET_SOFS_WAIT		0x09
#define KAWETH_COMMAND_SCAN			0xFF

#define KAWETH_SOFS_TO_WAIT			0x05

#define INTBUFFERSIZE				4

#define STATE_OFFSET				0
#define STATE_MASK				0x40
#define	STATE_SHIFT				5


MODULE_AUTHOR("Michael Zappe <zapman@interlan.net>, Stephane Alnet <stephane@u-picardie.fr>, Brad Hards <bhards@bigpond.net.au> and Oliver Neukum <oliver@neukum.org>");
MODULE_DESCRIPTION("KL5USB101 USB Ethernet driver");
MODULE_LICENSE("GPL");

static const char driver_name[] = "kaweth";

static int kaweth_probe(
		struct usb_interface *intf,
		const struct usb_device_id *id	/* from id_table */
	);
static void kaweth_disconnect(struct usb_interface *intf);
static int kaweth_internal_control_msg(struct usb_device *usb_dev,
				       unsigned int pipe,
				       struct usb_ctrlrequest *cmd, void *data,
				       int len, int timeout);

/****************************************************************
 *     usb_device_id
 ****************************************************************/
static struct usb_device_id usb_klsi_table[] = {
	{ USB_DEVICE(0x03e8, 0x0008) }, /* AOX Endpoints USB Ethernet */
	{ USB_DEVICE(0x04bb, 0x0901) }, /* I-O DATA USB-ET/T */
	{ USB_DEVICE(0x0506, 0x03e8) }, /* 3Com 3C19250 */
	{ USB_DEVICE(0x0506, 0x11f8) }, /* 3Com 3C460 */
	{ USB_DEVICE(0x0557, 0x2002) }, /* ATEN USB Ethernet */
	{ USB_DEVICE(0x0557, 0x4000) }, /* D-Link DSB-650C */
	{ USB_DEVICE(0x0565, 0x0002) }, /* Peracom Enet */
	{ USB_DEVICE(0x0565, 0x0003) }, /* Optus@Home UEP1045A */
	{ USB_DEVICE(0x0565, 0x0005) }, /* Peracom Enet2 */
	{ USB_DEVICE(0x05e9, 0x0008) }, /* KLSI KL5KUSB101B */
	{ USB_DEVICE(0x05e9, 0x0009) }, /* KLSI KL5KUSB101B (Board change) */
	{ USB_DEVICE(0x066b, 0x2202) }, /* Linksys USB10T */
	{ USB_DEVICE(0x06e1, 0x0008) }, /* ADS USB-10BT */
	{ USB_DEVICE(0x06e1, 0x0009) }, /* ADS USB-10BT */
	{ USB_DEVICE(0x0707, 0x0100) }, /* SMC 2202USB */
	{ USB_DEVICE(0x07aa, 0x0001) }, /* Correga K.K. */
	{ USB_DEVICE(0x07b8, 0x4000) }, /* D-Link DU-E10 */
	{ USB_DEVICE(0x0846, 0x1001) }, /* NetGear EA-101 */
	{ USB_DEVICE(0x0846, 0x1002) }, /* NetGear EA-101 */
	{ USB_DEVICE(0x085a, 0x0008) }, /* PortGear Ethernet Adapter */
	{ USB_DEVICE(0x085a, 0x0009) }, /* PortGear Ethernet Adapter */
	{ USB_DEVICE(0x087d, 0x5704) }, /* Jaton USB Ethernet Device Adapter */
	{ USB_DEVICE(0x0951, 0x0008) }, /* Kingston Technology USB Ethernet Adapter */
	{ USB_DEVICE(0x095a, 0x3003) }, /* Portsmith Express Ethernet Adapter */
	{ USB_DEVICE(0x10bd, 0x1427) }, /* ASANTE USB To Ethernet Adapter */
	{ USB_DEVICE(0x1342, 0x0204) }, /* Mobility USB-Ethernet Adapter */
	{ USB_DEVICE(0x13d2, 0x0400) }, /* Shark Pocket Adapter */
	{ USB_DEVICE(0x1485, 0x0001) },	/* Silicom U2E */
	{ USB_DEVICE(0x1485, 0x0002) }, /* Psion Dacom Gold Port Ethernet */
	{ USB_DEVICE(0x1645, 0x0005) }, /* Entrega E45 */
	{ USB_DEVICE(0x1645, 0x0008) }, /* Entrega USB Ethernet Adapter */
	{ USB_DEVICE(0x1645, 0x8005) }, /* PortGear Ethernet Adapter */
	{ USB_DEVICE(0x2001, 0x4000) }, /* D-link DSB-650C */
	{} /* Null terminator */
};

MODULE_DEVICE_TABLE (usb, usb_klsi_table);

/****************************************************************
 *     kaweth_driver
 ****************************************************************/
static struct usb_driver kaweth_driver = {
	.owner =	THIS_MODULE,
	.name =		driver_name,
	.probe =	kaweth_probe,
	.disconnect =	kaweth_disconnect,
	.id_table =     usb_klsi_table,
};

typedef __u8 eth_addr_t[6];

/****************************************************************
 *     usb_eth_dev
 ****************************************************************/
struct usb_eth_dev {
	char *name;
	__u16 vendor;
	__u16 device;
	void *pdata;
};

/****************************************************************
 *     kaweth_ethernet_configuration
 *     Refer Table 8
 ****************************************************************/
struct kaweth_ethernet_configuration
{
	__u8 size;
	__u8 reserved1;
	__u8 reserved2;
	eth_addr_t hw_addr;
	__u32 statistics_mask;
	__le16 segment_size;
	__u16 max_multicast_filters;
	__u8 reserved3;
} __attribute__ ((packed));

/****************************************************************
 *     kaweth_device
 ****************************************************************/
struct kaweth_device
{
	spinlock_t device_lock;

	__u32 status;
	int end;
	int suspend_lowmem_rx;
	int suspend_lowmem_ctrl;
	int linkstate;
	struct work_struct lowmem_work;

	struct usb_device *dev;
	struct net_device *net;
	wait_queue_head_t term_wait;

	struct urb *rx_urb;
	struct urb *tx_urb;
	struct urb *irq_urb;

	dma_addr_t intbufferhandle;
	__u8 *intbuffer;
	dma_addr_t rxbufferhandle;
	__u8 *rx_buf;

	
	struct sk_buff *tx_skb;

	__u8 *firmware_buf;
	__u8 scratch[KAWETH_SCRATCH_SIZE];
	__u16 packet_filter_bitmap;

	struct kaweth_ethernet_configuration configuration;

	struct net_device_stats stats;
};


/****************************************************************
 *     kaweth_control
 ****************************************************************/
static int kaweth_control(struct kaweth_device *kaweth,
			  unsigned int pipe,
			  __u8 request,
			  __u8 requesttype,
			  __u16 value,
			  __u16 index,
			  void *data,
			  __u16 size,
			  int timeout)
{
	struct usb_ctrlrequest *dr;

	kaweth_dbg("kaweth_control()");

	if(in_interrupt()) {
		kaweth_dbg("in_interrupt()");
		return -EBUSY;
	}

	dr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_ATOMIC);

	if (!dr) {
		kaweth_dbg("kmalloc() failed");
		return -ENOMEM;
	}

	dr->bRequestType= requesttype;
	dr->bRequest = request;
	dr->wValue = cpu_to_le16p(&value);
	dr->wIndex = cpu_to_le16p(&index);
	dr->wLength = cpu_to_le16p(&size);

	return kaweth_internal_control_msg(kaweth->dev,
					pipe,
					dr,
					data,
					size,
					timeout);
}

/****************************************************************
 *     kaweth_read_configuration
 ****************************************************************/
static int kaweth_read_configuration(struct kaweth_device *kaweth)
{
	int retval;

	kaweth_dbg("Reading kaweth configuration");

	retval = kaweth_control(kaweth,
				usb_rcvctrlpipe(kaweth->dev, 0),
				KAWETH_COMMAND_GET_ETHERNET_DESC,
				USB_TYPE_VENDOR | USB_DIR_IN | USB_RECIP_DEVICE,
				0,
				0,
				(void *)&kaweth->configuration,
				sizeof(kaweth->configuration),
				KAWETH_CONTROL_TIMEOUT);

	return retval;
}

/****************************************************************
 *     kaweth_set_urb_size
 ****************************************************************/
static int kaweth_set_urb_size(struct kaweth_device *kaweth, __u16 urb_size)
{
	int retval;

	kaweth_dbg("Setting URB size to %d", (unsigned)urb_size);

	retval = kaweth_control(kaweth,
				usb_sndctrlpipe(kaweth->dev, 0),
				KAWETH_COMMAND_SET_URB_SIZE,
				USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
				urb_size,
				0,
				(void *)&kaweth->scratch,
				0,
				KAWETH_CONTROL_TIMEOUT);

	return retval;
}

/****************************************************************
 *     kaweth_set_sofs_wait
 ****************************************************************/
static int kaweth_set_sofs_wait(struct kaweth_device *kaweth, __u16 sofs_wait)
{
	int retval;

	kaweth_dbg("Set SOFS wait to %d", (unsigned)sofs_wait);

	retval = kaweth_control(kaweth,
				usb_sndctrlpipe(kaweth->dev, 0),
				KAWETH_COMMAND_SET_SOFS_WAIT,
				USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
				sofs_wait,
				0,
				(void *)&kaweth->scratch,
				0,
				KAWETH_CONTROL_TIMEOUT);

	return retval;
}

/****************************************************************
 *     kaweth_set_receive_filter
 ****************************************************************/
static int kaweth_set_receive_filter(struct kaweth_device *kaweth,
				     __u16 receive_filter)
{
	int retval;

	kaweth_dbg("Set receive filter to %d", (unsigned)receive_filter);

	retval = kaweth_control(kaweth,
				usb_sndctrlpipe(kaweth->dev, 0),
				KAWETH_COMMAND_SET_PACKET_FILTER,
				USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
				receive_filter,
				0,
				(void *)&kaweth->scratch,
				0,
				KAWETH_CONTROL_TIMEOUT);

	return retval;
}

/****************************************************************
 *     kaweth_download_firmware
 ****************************************************************/
static int kaweth_download_firmware(struct kaweth_device *kaweth,
				    __u8 *data,
				    __u16 data_len,
				    __u8 interrupt,
				    __u8 type)
{
	if(data_len > KAWETH_FIRMWARE_BUF_SIZE)	{
		kaweth_err("Firmware too big: %d", data_len);
		return -ENOSPC;
	}

	memcpy(kaweth->firmware_buf, data, data_len);

	kaweth->firmware_buf[2] = (data_len & 0xFF) - 7;
	kaweth->firmware_buf[3] = data_len >> 8;
	kaweth->firmware_buf[4] = type;
	kaweth->firmware_buf[5] = interrupt;

	kaweth_dbg("High: %i, Low:%i", kaweth->firmware_buf[3],
		   kaweth->firmware_buf[2]);

	kaweth_dbg("Downloading firmware at %p to kaweth device at %p",
	    data,
	    kaweth);
	kaweth_dbg("Firmware length: %d", data_len);

	return kaweth_control(kaweth,
		              usb_sndctrlpipe(kaweth->dev, 0),
			      KAWETH_COMMAND_SCAN,
			      USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
			      0,
			      0,
			      (void *)kaweth->firmware_buf,
			      data_len,
			      KAWETH_CONTROL_TIMEOUT);
}

/****************************************************************
 *     kaweth_trigger_firmware
 ****************************************************************/
static int kaweth_trigger_firmware(struct kaweth_device *kaweth,
				   __u8 interrupt)
{
	kaweth->firmware_buf[0] = 0xB6;
	kaweth->firmware_buf[1] = 0xC3;
	kaweth->firmware_buf[2] = 0x01;
	kaweth->firmware_buf[3] = 0x00;
	kaweth->firmware_buf[4] = 0x06;
	kaweth->firmware_buf[5] = interrupt;
	kaweth->firmware_buf[6] = 0x00;
	kaweth->firmware_buf[7] = 0x00;

	kaweth_dbg("Triggering firmware");

	return kaweth_control(kaweth,
			      usb_sndctrlpipe(kaweth->dev, 0),
			      KAWETH_COMMAND_SCAN,
			      USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
			      0,
			      0,
			      (void *)kaweth->firmware_buf,
			      8,
			      KAWETH_CONTROL_TIMEOUT);
}

/****************************************************************
 *     kaweth_reset
 ****************************************************************/
static int kaweth_reset(struct kaweth_device *kaweth)
{
	int result;

	kaweth_dbg("kaweth_reset(%p)", kaweth);
	result = kaweth_control(kaweth,
				usb_sndctrlpipe(kaweth->dev, 0),
				USB_REQ_SET_CONFIGURATION,
				0,
				kaweth->dev->config[0].desc.bConfigurationValue,
				0,
				NULL,
				0,
				KAWETH_CONTROL_TIMEOUT);

	mdelay(10);

	kaweth_dbg("kaweth_reset() returns %d.",result);

	return result;
}

static void kaweth_usb_receive(struct urb *, struct pt_regs *regs);
static int kaweth_resubmit_rx_urb(struct kaweth_device *, gfp_t);

/****************************************************************
	int_callback
*****************************************************************/

static void kaweth_resubmit_int_urb(struct kaweth_device *kaweth, gfp_t mf)
{
	int status;

	status = usb_submit_urb (kaweth->irq_urb, mf);
	if (unlikely(status == -ENOMEM)) {
		kaweth->suspend_lowmem_ctrl = 1;
		schedule_delayed_work(&kaweth->lowmem_work, HZ/4);
	} else {
		kaweth->suspend_lowmem_ctrl = 0;
	}

	if (status)
		err ("can't resubmit intr, %s-%s, status %d",
				kaweth->dev->bus->bus_name,
				kaweth->dev->devpath, status);
}

static void int_callback(struct urb *u, struct pt_regs *regs)
{
	struct kaweth_device *kaweth = u->context;
	int act_state;

	switch (u->status) {
	case 0:			/* success */
		break;
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	/* -EPIPE:  should clear the halt */
	default:		/* error */
		goto resubmit;
	}

	/* we check the link state to report changes */
	if (kaweth->linkstate != (act_state = ( kaweth->intbuffer[STATE_OFFSET] | STATE_MASK) >> STATE_SHIFT)) {
		if (act_state)
			netif_carrier_on(kaweth->net);
		else
			netif_carrier_off(kaweth->net);

		kaweth->linkstate = act_state;
	}
resubmit:
	kaweth_resubmit_int_urb(kaweth, GFP_ATOMIC);
}

static void kaweth_resubmit_tl(void *d)
{
	struct kaweth_device *kaweth = (struct kaweth_device *)d;

	if (kaweth->status | KAWETH_STATUS_CLOSING)
		return;

	if (kaweth->suspend_lowmem_rx)
		kaweth_resubmit_rx_urb(kaweth, GFP_NOIO);

	if (kaweth->suspend_lowmem_ctrl)
		kaweth_resubmit_int_urb(kaweth, GFP_NOIO);
}


/****************************************************************
 *     kaweth_resubmit_rx_urb
 ****************************************************************/
static int kaweth_resubmit_rx_urb(struct kaweth_device *kaweth,
						gfp_t mem_flags)
{
	int result;

	usb_fill_bulk_urb(kaweth->rx_urb,
		      kaweth->dev,
		      usb_rcvbulkpipe(kaweth->dev, 1),
		      kaweth->rx_buf,
		      KAWETH_BUF_SIZE,
		      kaweth_usb_receive,
		      kaweth);
	kaweth->rx_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	kaweth->rx_urb->transfer_dma = kaweth->rxbufferhandle;

	if((result = usb_submit_urb(kaweth->rx_urb, mem_flags))) {
		if (result == -ENOMEM) {
			kaweth->suspend_lowmem_rx = 1;
			schedule_delayed_work(&kaweth->lowmem_work, HZ/4);
		}
		kaweth_err("resubmitting rx_urb %d failed", result);
	} else {
		kaweth->suspend_lowmem_rx = 0;
	}

	return result;
}

static void kaweth_async_set_rx_mode(struct kaweth_device *kaweth);

/****************************************************************
 *     kaweth_usb_receive
 ****************************************************************/
static void kaweth_usb_receive(struct urb *urb, struct pt_regs *regs)
{
	struct kaweth_device *kaweth = urb->context;
	struct net_device *net = kaweth->net;

	int count = urb->actual_length;
	int count2 = urb->transfer_buffer_length;

	__u16 pkt_len = le16_to_cpup((__le16 *)kaweth->rx_buf);

	struct sk_buff *skb;

	if(unlikely(urb->status == -ECONNRESET || urb->status == -ESHUTDOWN))
	/* we are killed - set a flag and wake the disconnect handler */
	{
		kaweth->end = 1;
		wake_up(&kaweth->term_wait);
		return;
	}

	if (kaweth->status & KAWETH_STATUS_CLOSING)
		return;

	if(urb->status && urb->status != -EREMOTEIO && count != 1) {
		kaweth_err("%s RX status: %d count: %d packet_len: %d",
                           net->name,
			   urb->status,
			   count,
			   (int)pkt_len);
		kaweth_resubmit_rx_urb(kaweth, GFP_ATOMIC);
                return;
	}

	if(kaweth->net && (count > 2)) {
		if(pkt_len > (count - 2)) {
			kaweth_err("Packet length too long for USB frame (pkt_len: %x, count: %x)",pkt_len, count);
			kaweth_err("Packet len & 2047: %x", pkt_len & 2047);
			kaweth_err("Count 2: %x", count2);
		        kaweth_resubmit_rx_urb(kaweth, GFP_ATOMIC);
                        return;
                }

		if(!(skb = dev_alloc_skb(pkt_len+2))) {
		        kaweth_resubmit_rx_urb(kaweth, GFP_ATOMIC);
                        return;
		}

		skb_reserve(skb, 2);    /* Align IP on 16 byte boundaries */

		skb->dev = net;

		eth_copy_and_sum(skb, kaweth->rx_buf + 2, pkt_len, 0);

		skb_put(skb, pkt_len);

		skb->protocol = eth_type_trans(skb, net);

		netif_rx(skb);

		kaweth->stats.rx_packets++;
		kaweth->stats.rx_bytes += pkt_len;
	}

	kaweth_resubmit_rx_urb(kaweth, GFP_ATOMIC);
}

/****************************************************************
 *     kaweth_open
 ****************************************************************/
static int kaweth_open(struct net_device *net)
{
	struct kaweth_device *kaweth = netdev_priv(net);
	int res;

	kaweth_dbg("Opening network device.");

	res = kaweth_resubmit_rx_urb(kaweth, GFP_KERNEL);
	if (res)
		return -EIO;

	usb_fill_int_urb(
		kaweth->irq_urb,
		kaweth->dev,
		usb_rcvintpipe(kaweth->dev, 3),
		kaweth->intbuffer,
		INTBUFFERSIZE,
		int_callback,
		kaweth,
		250); /* overriding the descriptor */
	kaweth->irq_urb->transfer_dma = kaweth->intbufferhandle;
	kaweth->irq_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	res = usb_submit_urb(kaweth->irq_urb, GFP_KERNEL);
	if (res) {
		usb_kill_urb(kaweth->rx_urb);
		return -EIO;
	}

	netif_start_queue(net);

	kaweth_async_set_rx_mode(kaweth);
	return 0;
}

/****************************************************************
 *     kaweth_close
 ****************************************************************/
static int kaweth_close(struct net_device *net)
{
	struct kaweth_device *kaweth = netdev_priv(net);

	netif_stop_queue(net);

	kaweth->status |= KAWETH_STATUS_CLOSING;

	usb_kill_urb(kaweth->irq_urb);
	usb_kill_urb(kaweth->rx_urb);
	usb_kill_urb(kaweth->tx_urb);

	flush_scheduled_work();

	/* a scheduled work may have resubmitted,
	   we hit them again */
	usb_kill_urb(kaweth->irq_urb);
	usb_kill_urb(kaweth->rx_urb);

	kaweth->status &= ~KAWETH_STATUS_CLOSING;

	return 0;
}

static void kaweth_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{

	strlcpy(info->driver, driver_name, sizeof(info->driver));
}

static struct ethtool_ops ops = {
	.get_drvinfo = kaweth_get_drvinfo
};

/****************************************************************
 *     kaweth_usb_transmit_complete
 ****************************************************************/
static void kaweth_usb_transmit_complete(struct urb *urb, struct pt_regs *regs)
{
	struct kaweth_device *kaweth = urb->context;
	struct sk_buff *skb = kaweth->tx_skb;

	if (unlikely(urb->status != 0))
		if (urb->status != -ENOENT)
			kaweth_dbg("%s: TX status %d.", kaweth->net->name, urb->status);

	netif_wake_queue(kaweth->net);
	dev_kfree_skb_irq(skb);
}

/****************************************************************
 *     kaweth_start_xmit
 ****************************************************************/
static int kaweth_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	struct kaweth_device *kaweth = netdev_priv(net);
	__le16 *private_header;

	int res;

	spin_lock(&kaweth->device_lock);

	kaweth_async_set_rx_mode(kaweth);
	netif_stop_queue(net);

	/* We now decide whether we can put our special header into the sk_buff */
	if (skb_cloned(skb) || skb_headroom(skb) < 2) {
		/* no such luck - we make our own */
		struct sk_buff *copied_skb;
		copied_skb = skb_copy_expand(skb, 2, 0, GFP_ATOMIC);
		dev_kfree_skb_irq(skb);
		skb = copied_skb;
		if (!copied_skb) {
			kaweth->stats.tx_errors++;
			netif_start_queue(net);
			spin_unlock(&kaweth->device_lock);
			return 0;
		}
	}

	private_header = (__le16 *)__skb_push(skb, 2);
	*private_header = cpu_to_le16(skb->len-2);
	kaweth->tx_skb = skb;

	usb_fill_bulk_urb(kaweth->tx_urb,
		      kaweth->dev,
		      usb_sndbulkpipe(kaweth->dev, 2),
		      private_header,
		      skb->len,
		      kaweth_usb_transmit_complete,
		      kaweth);
	kaweth->end = 0;

	if((res = usb_submit_urb(kaweth->tx_urb, GFP_ATOMIC)))
	{
		kaweth_warn("kaweth failed tx_urb %d", res);
		kaweth->stats.tx_errors++;

		netif_start_queue(net);
		dev_kfree_skb_irq(skb);
	}
	else
	{
		kaweth->stats.tx_packets++;
		kaweth->stats.tx_bytes += skb->len;
		net->trans_start = jiffies;
	}

	spin_unlock(&kaweth->device_lock);

	return 0;
}

/****************************************************************
 *     kaweth_set_rx_mode
 ****************************************************************/
static void kaweth_set_rx_mode(struct net_device *net)
{
	struct kaweth_device *kaweth = netdev_priv(net);

	__u16 packet_filter_bitmap = KAWETH_PACKET_FILTER_DIRECTED |
                                     KAWETH_PACKET_FILTER_BROADCAST |
		                     KAWETH_PACKET_FILTER_MULTICAST;

	kaweth_dbg("Setting Rx mode to %d", packet_filter_bitmap);

	netif_stop_queue(net);

	if (net->flags & IFF_PROMISC) {
		packet_filter_bitmap |= KAWETH_PACKET_FILTER_PROMISCUOUS;
	}
	else if ((net->mc_count) || (net->flags & IFF_ALLMULTI)) {
		packet_filter_bitmap |= KAWETH_PACKET_FILTER_ALL_MULTICAST;
	}

	kaweth->packet_filter_bitmap = packet_filter_bitmap;
	netif_wake_queue(net);
}

/****************************************************************
 *     kaweth_async_set_rx_mode
 ****************************************************************/
static void kaweth_async_set_rx_mode(struct kaweth_device *kaweth)
{
	__u16 packet_filter_bitmap = kaweth->packet_filter_bitmap;
	kaweth->packet_filter_bitmap = 0;
	if (packet_filter_bitmap == 0)
		return;

	{
	int result;
	result = kaweth_control(kaweth,
				usb_sndctrlpipe(kaweth->dev, 0),
				KAWETH_COMMAND_SET_PACKET_FILTER,
				USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
				packet_filter_bitmap,
				0,
				(void *)&kaweth->scratch,
				0,
				KAWETH_CONTROL_TIMEOUT);

	if(result < 0) {
		kaweth_err("Failed to set Rx mode: %d", result);
	}
	else {
		kaweth_dbg("Set Rx mode to %d", packet_filter_bitmap);
	}
	}
}

/****************************************************************
 *     kaweth_netdev_stats
 ****************************************************************/
static struct net_device_stats *kaweth_netdev_stats(struct net_device *dev)
{
	struct kaweth_device *kaweth = netdev_priv(dev);
	return &kaweth->stats;
}

/****************************************************************
 *     kaweth_tx_timeout
 ****************************************************************/
static void kaweth_tx_timeout(struct net_device *net)
{
	struct kaweth_device *kaweth = netdev_priv(net);

	kaweth_warn("%s: Tx timed out. Resetting.", net->name);
	kaweth->stats.tx_errors++;
	net->trans_start = jiffies;

	usb_unlink_urb(kaweth->tx_urb);
}

/****************************************************************
 *     kaweth_probe
 ****************************************************************/
static int kaweth_probe(
		struct usb_interface *intf,
		const struct usb_device_id *id      /* from id_table */
	)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct kaweth_device *kaweth;
	struct net_device *netdev;
	const eth_addr_t bcast_addr = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	int result = 0;

	kaweth_dbg("Kawasaki Device Probe (Device number:%d): 0x%4.4x:0x%4.4x:0x%4.4x",
		 dev->devnum,
		 le16_to_cpu(dev->descriptor.idVendor),
		 le16_to_cpu(dev->descriptor.idProduct),
		 le16_to_cpu(dev->descriptor.bcdDevice));

	kaweth_dbg("Device at %p", dev);

	kaweth_dbg("Descriptor length: %x type: %x",
		 (int)dev->descriptor.bLength,
		 (int)dev->descriptor.bDescriptorType);

	netdev = alloc_etherdev(sizeof(*kaweth));
	if (!netdev)
		return -ENOMEM;

	kaweth = netdev_priv(netdev);
	kaweth->dev = dev;
	kaweth->net = netdev;

	spin_lock_init(&kaweth->device_lock);
	init_waitqueue_head(&kaweth->term_wait);

	kaweth_dbg("Resetting.");

	kaweth_reset(kaweth);

	/*
	 * If high byte of bcdDevice is nonzero, firmware is already
	 * downloaded. Don't try to do it again, or we'll hang the device.
	 */

	if (le16_to_cpu(dev->descriptor.bcdDevice) >> 8) {
		kaweth_info("Firmware present in device.");
	} else {
		/* Download the firmware */
		kaweth_info("Downloading firmware...");
		kaweth->firmware_buf = (__u8 *)__get_free_page(GFP_KERNEL);
		if ((result = kaweth_download_firmware(kaweth,
						      kaweth_new_code,
						      len_kaweth_new_code,
						      100,
						      2)) < 0) {
			kaweth_err("Error downloading firmware (%d)", result);
			goto err_fw;
		}

		if ((result = kaweth_download_firmware(kaweth,
						      kaweth_new_code_fix,
						      len_kaweth_new_code_fix,
						      100,
						      3)) < 0) {
			kaweth_err("Error downloading firmware fix (%d)", result);
			goto err_fw;
		}

		if ((result = kaweth_download_firmware(kaweth,
						      kaweth_trigger_code,
						      len_kaweth_trigger_code,
						      126,
						      2)) < 0) {
			kaweth_err("Error downloading trigger code (%d)", result);
			goto err_fw;

		}

		if ((result = kaweth_download_firmware(kaweth,
						      kaweth_trigger_code_fix,
						      len_kaweth_trigger_code_fix,
						      126,
						      3)) < 0) {
			kaweth_err("Error downloading trigger code fix (%d)", result);
			goto err_fw;
		}


		if ((result = kaweth_trigger_firmware(kaweth, 126)) < 0) {
			kaweth_err("Error triggering firmware (%d)", result);
			goto err_fw;
		}

		/* Device will now disappear for a moment...  */
		kaweth_info("Firmware loaded.  I'll be back...");
err_fw:
		free_page((unsigned long)kaweth->firmware_buf);
		free_netdev(netdev);
		return -EIO;
	}

	result = kaweth_read_configuration(kaweth);

	if(result < 0) {
		kaweth_err("Error reading configuration (%d), no net device created", result);
		goto err_free_netdev;
	}

	kaweth_info("Statistics collection: %x", kaweth->configuration.statistics_mask);
	kaweth_info("Multicast filter limit: %x", kaweth->configuration.max_multicast_filters & ((1 << 15) - 1));
	kaweth_info("MTU: %d", le16_to_cpu(kaweth->configuration.segment_size));
	kaweth_info("Read MAC address %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x",
		 (int)kaweth->configuration.hw_addr[0],
		 (int)kaweth->configuration.hw_addr[1],
		 (int)kaweth->configuration.hw_addr[2],
		 (int)kaweth->configuration.hw_addr[3],
		 (int)kaweth->configuration.hw_addr[4],
		 (int)kaweth->configuration.hw_addr[5]);

	if(!memcmp(&kaweth->configuration.hw_addr,
                   &bcast_addr,
		   sizeof(bcast_addr))) {
		kaweth_err("Firmware not functioning properly, no net device created");
		goto err_free_netdev;
	}

	if(kaweth_set_urb_size(kaweth, KAWETH_BUF_SIZE) < 0) {
		kaweth_dbg("Error setting URB size");
		goto err_free_netdev;
	}

	if(kaweth_set_sofs_wait(kaweth, KAWETH_SOFS_TO_WAIT) < 0) {
		kaweth_err("Error setting SOFS wait");
		goto err_free_netdev;
	}

	result = kaweth_set_receive_filter(kaweth,
                                           KAWETH_PACKET_FILTER_DIRECTED |
                                           KAWETH_PACKET_FILTER_BROADCAST |
                                           KAWETH_PACKET_FILTER_MULTICAST);

	if(result < 0) {
		kaweth_err("Error setting receive filter");
		goto err_free_netdev;
	}

	kaweth_dbg("Initializing net device.");

	kaweth->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!kaweth->tx_urb)
		goto err_free_netdev;
	kaweth->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!kaweth->rx_urb)
		goto err_only_tx;
	kaweth->irq_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!kaweth->irq_urb)
		goto err_tx_and_rx;

	kaweth->intbuffer = usb_buffer_alloc(	kaweth->dev,
						INTBUFFERSIZE,
						GFP_KERNEL,
						&kaweth->intbufferhandle);
	if (!kaweth->intbuffer)
		goto err_tx_and_rx_and_irq;
	kaweth->rx_buf = usb_buffer_alloc(	kaweth->dev,
						KAWETH_BUF_SIZE,
						GFP_KERNEL,
						&kaweth->rxbufferhandle);
	if (!kaweth->rx_buf)
		goto err_all_but_rxbuf;

	memcpy(netdev->broadcast, &bcast_addr, sizeof(bcast_addr));
	memcpy(netdev->dev_addr, &kaweth->configuration.hw_addr,
               sizeof(kaweth->configuration.hw_addr));

	netdev->open = kaweth_open;
	netdev->stop = kaweth_close;

	netdev->watchdog_timeo = KAWETH_TX_TIMEOUT;
	netdev->tx_timeout = kaweth_tx_timeout;

	netdev->hard_start_xmit = kaweth_start_xmit;
	netdev->set_multicast_list = kaweth_set_rx_mode;
	netdev->get_stats = kaweth_netdev_stats;
	netdev->mtu = le16_to_cpu(kaweth->configuration.segment_size);
	SET_ETHTOOL_OPS(netdev, &ops);

	/* kaweth is zeroed as part of alloc_netdev */

	INIT_WORK(&kaweth->lowmem_work, kaweth_resubmit_tl, (void *)kaweth);

	SET_MODULE_OWNER(netdev);

	usb_set_intfdata(intf, kaweth);

#if 0
// dma_supported() is deeply broken on almost all architectures
	if (dma_supported (&intf->dev, 0xffffffffffffffffULL))
		kaweth->net->features |= NETIF_F_HIGHDMA;
#endif

	SET_NETDEV_DEV(netdev, &intf->dev);
	if (register_netdev(netdev) != 0) {
		kaweth_err("Error registering netdev.");
		goto err_intfdata;
	}

	kaweth_info("kaweth interface created at %s", kaweth->net->name);

	kaweth_dbg("Kaweth probe returning.");

	return 0;

err_intfdata:
	usb_set_intfdata(intf, NULL);
	usb_buffer_free(kaweth->dev, KAWETH_BUF_SIZE, (void *)kaweth->rx_buf, kaweth->rxbufferhandle);
err_all_but_rxbuf:
	usb_buffer_free(kaweth->dev, INTBUFFERSIZE, (void *)kaweth->intbuffer, kaweth->intbufferhandle);
err_tx_and_rx_and_irq:
	usb_free_urb(kaweth->irq_urb);
err_tx_and_rx:
	usb_free_urb(kaweth->rx_urb);
err_only_tx:
	usb_free_urb(kaweth->tx_urb);
err_free_netdev:
	free_netdev(netdev);

	return -EIO;
}

/****************************************************************
 *     kaweth_disconnect
 ****************************************************************/
static void kaweth_disconnect(struct usb_interface *intf)
{
	struct kaweth_device *kaweth = usb_get_intfdata(intf);
	struct net_device *netdev;

	kaweth_info("Unregistering");

	usb_set_intfdata(intf, NULL);
	if (!kaweth) {
		kaweth_warn("unregistering non-existant device");
		return;
	}
	netdev = kaweth->net;

	kaweth_dbg("Unregistering net device");
	unregister_netdev(netdev);

	usb_free_urb(kaweth->rx_urb);
	usb_free_urb(kaweth->tx_urb);
	usb_free_urb(kaweth->irq_urb);

	usb_buffer_free(kaweth->dev, KAWETH_BUF_SIZE, (void *)kaweth->rx_buf, kaweth->rxbufferhandle);
	usb_buffer_free(kaweth->dev, INTBUFFERSIZE, (void *)kaweth->intbuffer, kaweth->intbufferhandle);

	free_netdev(netdev);
}


// FIXME this completion stuff is a modified clone of
// an OLD version of some stuff in usb.c ...
struct usb_api_data {
	wait_queue_head_t wqh;
	int done;
};

/*-------------------------------------------------------------------*
 * completion handler for compatibility wrappers (sync control/bulk) *
 *-------------------------------------------------------------------*/
static void usb_api_blocking_completion(struct urb *urb, struct pt_regs *regs)
{
        struct usb_api_data *awd = (struct usb_api_data *)urb->context;

	awd->done=1;
	wake_up(&awd->wqh);
}

/*-------------------------------------------------------------------*
 *                         COMPATIBILITY STUFF                       *
 *-------------------------------------------------------------------*/

// Starts urb and waits for completion or timeout
static int usb_start_wait_urb(struct urb *urb, int timeout, int* actual_length)
{
	struct usb_api_data awd;
        int status;

        init_waitqueue_head(&awd.wqh);
        awd.done = 0;

        urb->context = &awd;
        status = usb_submit_urb(urb, GFP_NOIO);
        if (status) {
                // something went wrong
                usb_free_urb(urb);
                return status;
        }

	if (!wait_event_timeout(awd.wqh, awd.done, timeout)) {
                // timeout
                kaweth_warn("usb_control/bulk_msg: timeout");
                usb_kill_urb(urb);  // remove urb safely
                status = -ETIMEDOUT;
        }
	else {
                status = urb->status;
	}

        if (actual_length) {
                *actual_length = urb->actual_length;
	}

        usb_free_urb(urb);
        return status;
}

/*-------------------------------------------------------------------*/
// returns status (negative) or length (positive)
static int kaweth_internal_control_msg(struct usb_device *usb_dev,
				       unsigned int pipe,
				       struct usb_ctrlrequest *cmd, void *data,
				       int len, int timeout)
{
        struct urb *urb;
        int retv;
        int length;

        urb = usb_alloc_urb(0, GFP_NOIO);
        if (!urb)
                return -ENOMEM;

        usb_fill_control_urb(urb, usb_dev, pipe, (unsigned char*)cmd, data,
			 len, usb_api_blocking_completion, NULL);

        retv = usb_start_wait_urb(urb, timeout, &length);
        if (retv < 0) {
                return retv;
	}
        else {
                return length;
	}
}


/****************************************************************
 *     kaweth_init
 ****************************************************************/
static int __init kaweth_init(void)
{
	kaweth_dbg("Driver loading");
	return usb_register(&kaweth_driver);
}

/****************************************************************
 *     kaweth_exit
 ****************************************************************/
static void __exit kaweth_exit(void)
{
	usb_deregister(&kaweth_driver);
}

module_init(kaweth_init);
module_exit(kaweth_exit);









