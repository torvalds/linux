/*****************************************************************************
*
* Filename:      ksdazzle.c
* Version:       0.1.2
* Description:   Irda KingSun Dazzle USB Dongle
* Status:        Experimental
* Author:        Alex Villacís Lasso <a_villacis@palosanto.com>
*
*    Based on stir4200, mcs7780, kingsun-sir drivers.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/

/*
 * Following is my most current (2007-07-26) understanding of how the Kingsun
 * 07D0:4100 dongle (sometimes known as the MA-660) is supposed to work. This
 * information was deduced by examining the USB traffic captured with USBSnoopy
 * from the WinXP driver. Feel free to update here as more of the dongle is
 * known.
 *
 * General: This dongle exposes one interface with two interrupt endpoints, one
 * IN and one OUT. In this regard, it is similar to what the Kingsun/Donshine
 * dongle (07c0:4200) exposes. Traffic is raw and needs to be wrapped and
 * unwrapped manually as in stir4200, kingsun-sir, and ks959-sir.
 *
 * Transmission: To transmit an IrDA frame, it is necessary to wrap it, then
 * split it into multiple segments of up to 7 bytes each, and transmit each in
 * sequence. It seems that sending a single big block (like kingsun-sir does)
 * won't work with this dongle. Each segment needs to be prefixed with a value
 * equal to (unsigned char)0xF8 + <number of bytes in segment>, inside a payload
 * of exactly 8 bytes. For example, a segment of 1 byte gets prefixed by 0xF9,
 * and one of 7 bytes gets prefixed by 0xFF. The bytes at the end of the
 * payload, not considered by the prefix, are ignored (set to 0 by this
 * implementation).
 *
 * Reception: To receive data, the driver must poll the dongle regularly (like
 * kingsun-sir.c) with interrupt URBs. If data is available, it will be returned
 * in payloads from 0 to 8 bytes long. When concatenated, these payloads form
 * a raw IrDA stream that needs to be unwrapped as in stir4200 and kingsun-sir
 *
 * Speed change: To change the speed of the dongle, the driver prepares a
 * control URB with the following as a setup packet:
 *    bRequestType    USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE
 *    bRequest        0x09
 *    wValue          0x0200
 *    wIndex          0x0001
 *    wLength         0x0008 (length of the payload)
 * The payload is a 8-byte record, apparently identical to the one used in
 * drivers/usb/serial/cypress_m8.c to change speed:
 *     __u32 baudSpeed;
 *    unsigned int dataBits : 2;    // 0 - 5 bits 3 - 8 bits
 *    unsigned int : 1;
 *    unsigned int stopBits : 1;
 *    unsigned int parityEnable : 1;
 *    unsigned int parityType : 1;
 *    unsigned int : 1;
 *    unsigned int reset : 1;
 *    unsigned char reserved[3];    // set to 0
 *
 * For now only SIR speeds have been observed with this dongle. Therefore,
 * nothing is known on what changes (if any) must be done to frame wrapping /
 * unwrapping for higher than SIR speeds. This driver assumes no change is
 * necessary and announces support for all the way to 115200 bps.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/device.h>
#include <linux/crc32.h>

#include <asm/unaligned.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>

#include <net/irda/irda.h>
#include <net/irda/wrapper.h>
#include <net/irda/crc.h>

#define KSDAZZLE_VENDOR_ID 0x07d0
#define KSDAZZLE_PRODUCT_ID 0x4100

/* These are the currently known USB ids */
static struct usb_device_id dongles[] = {
	/* KingSun Co,Ltd  IrDA/USB Bridge */
	{USB_DEVICE(KSDAZZLE_VENDOR_ID, KSDAZZLE_PRODUCT_ID)},
	{}
};

MODULE_DEVICE_TABLE(usb, dongles);

#define KINGSUN_MTT 0x07
#define KINGSUN_REQ_RECV 0x01
#define KINGSUN_REQ_SEND 0x09

#define KINGSUN_SND_FIFO_SIZE    2048	/* Max packet we can send */
#define KINGSUN_RCV_MAX 2048	/* Max transfer we can receive */

struct ksdazzle_speedparams {
	__le32 baudrate;	/* baud rate, little endian */
	__u8 flags;
	__u8 reserved[3];
} __attribute__ ((packed));

#define KS_DATA_5_BITS 0x00
#define KS_DATA_6_BITS 0x01
#define KS_DATA_7_BITS 0x02
#define KS_DATA_8_BITS 0x03

#define KS_STOP_BITS_1 0x00
#define KS_STOP_BITS_2 0x08

#define KS_PAR_DISABLE    0x00
#define KS_PAR_EVEN    0x10
#define KS_PAR_ODD    0x30
#define KS_RESET    0x80

#define KINGSUN_EP_IN			0
#define KINGSUN_EP_OUT			1

struct ksdazzle_cb {
	struct usb_device *usbdev;	/* init: probe_irda */
	struct net_device *netdev;	/* network layer */
	struct irlap_cb *irlap;	/* The link layer we are binded to */

	struct qos_info qos;

	struct urb *tx_urb;
	__u8 *tx_buf_clear;
	unsigned int tx_buf_clear_used;
	unsigned int tx_buf_clear_sent;
	__u8 tx_payload[8];

	struct urb *rx_urb;
	__u8 *rx_buf;
	iobuff_t rx_unwrap_buff;

	struct usb_ctrlrequest *speed_setuprequest;
	struct urb *speed_urb;
	struct ksdazzle_speedparams speedparams;
	unsigned int new_speed;

	__u8 ep_in;
	__u8 ep_out;

	spinlock_t lock;
	int receiving;
};

/* Callback transmission routine */
static void ksdazzle_speed_irq(struct urb *urb)
{
	/* unlink, shutdown, unplug, other nasties */
	if (urb->status != 0) {
		err("ksdazzle_speed_irq: urb asynchronously failed - %d",
		    urb->status);
	}
}

/* Send a control request to change speed of the dongle */
static int ksdazzle_change_speed(struct ksdazzle_cb *kingsun, unsigned speed)
{
	static unsigned int supported_speeds[] = { 2400, 9600, 19200, 38400,
		57600, 115200, 576000, 1152000, 4000000, 0
	};
	int err;
	unsigned int i;

	if (kingsun->speed_setuprequest == NULL || kingsun->speed_urb == NULL)
		return -ENOMEM;

	/* Check that requested speed is among the supported ones */
	for (i = 0; supported_speeds[i] && supported_speeds[i] != speed; i++) ;
	if (supported_speeds[i] == 0)
		return -EOPNOTSUPP;

	memset(&(kingsun->speedparams), 0, sizeof(struct ksdazzle_speedparams));
	kingsun->speedparams.baudrate = cpu_to_le32(speed);
	kingsun->speedparams.flags = KS_DATA_8_BITS;

	/* speed_setuprequest pre-filled in ksdazzle_probe */
	usb_fill_control_urb(kingsun->speed_urb, kingsun->usbdev,
			     usb_sndctrlpipe(kingsun->usbdev, 0),
			     (unsigned char *)kingsun->speed_setuprequest,
			     &(kingsun->speedparams),
			     sizeof(struct ksdazzle_speedparams),
			     ksdazzle_speed_irq, kingsun);
	kingsun->speed_urb->status = 0;
	err = usb_submit_urb(kingsun->speed_urb, GFP_ATOMIC);

	return err;
}

/* Submit one fragment of an IrDA frame to the dongle */
static void ksdazzle_send_irq(struct urb *urb);
static int ksdazzle_submit_tx_fragment(struct ksdazzle_cb *kingsun)
{
	unsigned int wraplen;
	int ret;

	/* We can send at most 7 bytes of payload at a time */
	wraplen = 7;
	if (wraplen > kingsun->tx_buf_clear_used)
		wraplen = kingsun->tx_buf_clear_used;

	/* Prepare payload prefix with used length */
	memset(kingsun->tx_payload, 0, 8);
	kingsun->tx_payload[0] = (unsigned char)0xf8 + wraplen;
	memcpy(kingsun->tx_payload + 1, kingsun->tx_buf_clear, wraplen);

	usb_fill_int_urb(kingsun->tx_urb, kingsun->usbdev,
			 usb_sndintpipe(kingsun->usbdev, kingsun->ep_out),
			 kingsun->tx_payload, 8, ksdazzle_send_irq, kingsun, 1);
	kingsun->tx_urb->status = 0;
	ret = usb_submit_urb(kingsun->tx_urb, GFP_ATOMIC);

	/* Remember how much data was sent, in order to update at callback */
	kingsun->tx_buf_clear_sent = (ret == 0) ? wraplen : 0;
	return ret;
}

/* Callback transmission routine */
static void ksdazzle_send_irq(struct urb *urb)
{
	struct ksdazzle_cb *kingsun = urb->context;
	struct net_device *netdev = kingsun->netdev;
	int ret = 0;

	/* in process of stopping, just drop data */
	if (!netif_running(kingsun->netdev)) {
		err("ksdazzle_send_irq: Network not running!");
		return;
	}

	/* unlink, shutdown, unplug, other nasties */
	if (urb->status != 0) {
		err("ksdazzle_send_irq: urb asynchronously failed - %d",
		    urb->status);
		return;
	}

	if (kingsun->tx_buf_clear_used > 0) {
		/* Update data remaining to be sent */
		if (kingsun->tx_buf_clear_sent < kingsun->tx_buf_clear_used) {
			memmove(kingsun->tx_buf_clear,
				kingsun->tx_buf_clear +
				kingsun->tx_buf_clear_sent,
				kingsun->tx_buf_clear_used -
				kingsun->tx_buf_clear_sent);
		}
		kingsun->tx_buf_clear_used -= kingsun->tx_buf_clear_sent;
		kingsun->tx_buf_clear_sent = 0;

		if (kingsun->tx_buf_clear_used > 0) {
			/* There is more data to be sent */
			if ((ret = ksdazzle_submit_tx_fragment(kingsun)) != 0) {
				err("ksdazzle_send_irq: failed tx_urb submit: %d", ret);
				switch (ret) {
				case -ENODEV:
				case -EPIPE:
					break;
				default:
					netdev->stats.tx_errors++;
					netif_start_queue(netdev);
				}
			}
		} else {
			/* All data sent, send next speed && wake network queue */
			if (kingsun->new_speed != -1 &&
			    cpu_to_le32(kingsun->new_speed) !=
			    kingsun->speedparams.baudrate)
				ksdazzle_change_speed(kingsun,
						      kingsun->new_speed);

			netif_wake_queue(netdev);
		}
	}
}

/*
 * Called from net/core when new frame is available.
 */
static netdev_tx_t ksdazzle_hard_xmit(struct sk_buff *skb,
					    struct net_device *netdev)
{
	struct ksdazzle_cb *kingsun;
	unsigned int wraplen;
	int ret = 0;

	netif_stop_queue(netdev);

	/* the IRDA wrapping routines don't deal with non linear skb */
	SKB_LINEAR_ASSERT(skb);

	kingsun = netdev_priv(netdev);

	spin_lock(&kingsun->lock);
	kingsun->new_speed = irda_get_next_speed(skb);

	/* Append data to the end of whatever data remains to be transmitted */
	wraplen =
	    async_wrap_skb(skb, kingsun->tx_buf_clear, KINGSUN_SND_FIFO_SIZE);
	kingsun->tx_buf_clear_used = wraplen;

	if ((ret = ksdazzle_submit_tx_fragment(kingsun)) != 0) {
		err("ksdazzle_hard_xmit: failed tx_urb submit: %d", ret);
		switch (ret) {
		case -ENODEV:
		case -EPIPE:
			break;
		default:
			netdev->stats.tx_errors++;
			netif_start_queue(netdev);
		}
	} else {
		netdev->stats.tx_packets++;
		netdev->stats.tx_bytes += skb->len;

	}

	dev_kfree_skb(skb);
	spin_unlock(&kingsun->lock);

	return NETDEV_TX_OK;
}

/* Receive callback function */
static void ksdazzle_rcv_irq(struct urb *urb)
{
	struct ksdazzle_cb *kingsun = urb->context;
	struct net_device *netdev = kingsun->netdev;

	/* in process of stopping, just drop data */
	if (!netif_running(netdev)) {
		kingsun->receiving = 0;
		return;
	}

	/* unlink, shutdown, unplug, other nasties */
	if (urb->status != 0) {
		err("ksdazzle_rcv_irq: urb asynchronously failed - %d",
		    urb->status);
		kingsun->receiving = 0;
		return;
	}

	if (urb->actual_length > 0) {
		__u8 *bytes = urb->transfer_buffer;
		unsigned int i;

		for (i = 0; i < urb->actual_length; i++) {
			async_unwrap_char(netdev, &netdev->stats,
					  &kingsun->rx_unwrap_buff, bytes[i]);
		}
		kingsun->receiving =
		    (kingsun->rx_unwrap_buff.state != OUTSIDE_FRAME) ? 1 : 0;
	}

	/* This urb has already been filled in ksdazzle_net_open. It is assumed that
	   urb keeps the pointer to the payload buffer.
	 */
	urb->status = 0;
	usb_submit_urb(urb, GFP_ATOMIC);
}

/*
 * Function ksdazzle_net_open (dev)
 *
 *    Network device is taken up. Usually this is done by "ifconfig irda0 up"
 */
static int ksdazzle_net_open(struct net_device *netdev)
{
	struct ksdazzle_cb *kingsun = netdev_priv(netdev);
	int err = -ENOMEM;
	char hwname[16];

	/* At this point, urbs are NULL, and skb is NULL (see ksdazzle_probe) */
	kingsun->receiving = 0;

	/* Initialize for SIR to copy data directly into skb.  */
	kingsun->rx_unwrap_buff.in_frame = FALSE;
	kingsun->rx_unwrap_buff.state = OUTSIDE_FRAME;
	kingsun->rx_unwrap_buff.truesize = IRDA_SKB_MAX_MTU;
	kingsun->rx_unwrap_buff.skb = dev_alloc_skb(IRDA_SKB_MAX_MTU);
	if (!kingsun->rx_unwrap_buff.skb)
		goto free_mem;

	skb_reserve(kingsun->rx_unwrap_buff.skb, 1);
	kingsun->rx_unwrap_buff.head = kingsun->rx_unwrap_buff.skb->data;

	kingsun->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!kingsun->rx_urb)
		goto free_mem;

	kingsun->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!kingsun->tx_urb)
		goto free_mem;

	kingsun->speed_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!kingsun->speed_urb)
		goto free_mem;

	/* Initialize speed for dongle */
	kingsun->new_speed = 9600;
	err = ksdazzle_change_speed(kingsun, 9600);
	if (err < 0)
		goto free_mem;

	/*
	 * Now that everything should be initialized properly,
	 * Open new IrLAP layer instance to take care of us...
	 */
	sprintf(hwname, "usb#%d", kingsun->usbdev->devnum);
	kingsun->irlap = irlap_open(netdev, &kingsun->qos, hwname);
	if (!kingsun->irlap) {
		err("ksdazzle-sir: irlap_open failed");
		goto free_mem;
	}

	/* Start reception. */
	usb_fill_int_urb(kingsun->rx_urb, kingsun->usbdev,
			 usb_rcvintpipe(kingsun->usbdev, kingsun->ep_in),
			 kingsun->rx_buf, KINGSUN_RCV_MAX, ksdazzle_rcv_irq,
			 kingsun, 1);
	kingsun->rx_urb->status = 0;
	err = usb_submit_urb(kingsun->rx_urb, GFP_KERNEL);
	if (err) {
		err("ksdazzle-sir: first urb-submit failed: %d", err);
		goto close_irlap;
	}

	netif_start_queue(netdev);

	/* Situation at this point:
	   - all work buffers allocated
	   - urbs allocated and ready to fill
	   - max rx packet known (in max_rx)
	   - unwrap state machine initialized, in state outside of any frame
	   - receive request in progress
	   - IrLAP layer started, about to hand over packets to send
	 */

	return 0;

      close_irlap:
	irlap_close(kingsun->irlap);
      free_mem:
	usb_free_urb(kingsun->speed_urb);
	kingsun->speed_urb = NULL;
	usb_free_urb(kingsun->tx_urb);
	kingsun->tx_urb = NULL;
	usb_free_urb(kingsun->rx_urb);
	kingsun->rx_urb = NULL;
	if (kingsun->rx_unwrap_buff.skb) {
		kfree_skb(kingsun->rx_unwrap_buff.skb);
		kingsun->rx_unwrap_buff.skb = NULL;
		kingsun->rx_unwrap_buff.head = NULL;
	}
	return err;
}

/*
 * Function ksdazzle_net_close (dev)
 *
 *    Network device is taken down. Usually this is done by
 *    "ifconfig irda0 down"
 */
static int ksdazzle_net_close(struct net_device *netdev)
{
	struct ksdazzle_cb *kingsun = netdev_priv(netdev);

	/* Stop transmit processing */
	netif_stop_queue(netdev);

	/* Mop up receive && transmit urb's */
	usb_kill_urb(kingsun->tx_urb);
	usb_free_urb(kingsun->tx_urb);
	kingsun->tx_urb = NULL;

	usb_kill_urb(kingsun->speed_urb);
	usb_free_urb(kingsun->speed_urb);
	kingsun->speed_urb = NULL;

	usb_kill_urb(kingsun->rx_urb);
	usb_free_urb(kingsun->rx_urb);
	kingsun->rx_urb = NULL;

	kfree_skb(kingsun->rx_unwrap_buff.skb);
	kingsun->rx_unwrap_buff.skb = NULL;
	kingsun->rx_unwrap_buff.head = NULL;
	kingsun->rx_unwrap_buff.in_frame = FALSE;
	kingsun->rx_unwrap_buff.state = OUTSIDE_FRAME;
	kingsun->receiving = 0;

	/* Stop and remove instance of IrLAP */
	irlap_close(kingsun->irlap);

	kingsun->irlap = NULL;

	return 0;
}

/*
 * IOCTLs : Extra out-of-band network commands...
 */
static int ksdazzle_net_ioctl(struct net_device *netdev, struct ifreq *rq,
			      int cmd)
{
	struct if_irda_req *irq = (struct if_irda_req *)rq;
	struct ksdazzle_cb *kingsun = netdev_priv(netdev);
	int ret = 0;

	switch (cmd) {
	case SIOCSBANDWIDTH:	/* Set bandwidth */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		/* Check if the device is still there */
		if (netif_device_present(kingsun->netdev))
			return ksdazzle_change_speed(kingsun,
						     irq->ifr_baudrate);
		break;

	case SIOCSMEDIABUSY:	/* Set media busy */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		/* Check if the IrDA stack is still there */
		if (netif_running(kingsun->netdev))
			irda_device_set_media_busy(kingsun->netdev, TRUE);
		break;

	case SIOCGRECEIVING:
		/* Only approximately true */
		irq->ifr_receiving = kingsun->receiving;
		break;

	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static const struct net_device_ops ksdazzle_ops = {
	.ndo_start_xmit	= ksdazzle_hard_xmit,
	.ndo_open	= ksdazzle_net_open,
	.ndo_stop	= ksdazzle_net_close,
	.ndo_do_ioctl	= ksdazzle_net_ioctl,
};

/*
 * This routine is called by the USB subsystem for each new device
 * in the system. We need to check if the device is ours, and in
 * this case start handling it.
 */
static int ksdazzle_probe(struct usb_interface *intf,
			  const struct usb_device_id *id)
{
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;

	struct usb_device *dev = interface_to_usbdev(intf);
	struct ksdazzle_cb *kingsun = NULL;
	struct net_device *net = NULL;
	int ret = -ENOMEM;
	int pipe, maxp_in, maxp_out;
	__u8 ep_in;
	__u8 ep_out;

	/* Check that there really are two interrupt endpoints. Check based on the
	   one in drivers/usb/input/usbmouse.c
	 */
	interface = intf->cur_altsetting;
	if (interface->desc.bNumEndpoints != 2) {
		err("ksdazzle: expected 2 endpoints, found %d",
		    interface->desc.bNumEndpoints);
		return -ENODEV;
	}
	endpoint = &interface->endpoint[KINGSUN_EP_IN].desc;
	if (!usb_endpoint_is_int_in(endpoint)) {
		err("ksdazzle: endpoint 0 is not interrupt IN");
		return -ENODEV;
	}

	ep_in = endpoint->bEndpointAddress;
	pipe = usb_rcvintpipe(dev, ep_in);
	maxp_in = usb_maxpacket(dev, pipe, usb_pipeout(pipe));
	if (maxp_in > 255 || maxp_in <= 1) {
		err("ksdazzle: endpoint 0 has max packet size %d not in range [2..255]", maxp_in);
		return -ENODEV;
	}

	endpoint = &interface->endpoint[KINGSUN_EP_OUT].desc;
	if (!usb_endpoint_is_int_out(endpoint)) {
		err("ksdazzle: endpoint 1 is not interrupt OUT");
		return -ENODEV;
	}

	ep_out = endpoint->bEndpointAddress;
	pipe = usb_sndintpipe(dev, ep_out);
	maxp_out = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

	/* Allocate network device container. */
	net = alloc_irdadev(sizeof(*kingsun));
	if (!net)
		goto err_out1;

	SET_NETDEV_DEV(net, &intf->dev);
	kingsun = netdev_priv(net);
	kingsun->netdev = net;
	kingsun->usbdev = dev;
	kingsun->ep_in = ep_in;
	kingsun->ep_out = ep_out;
	kingsun->irlap = NULL;
	kingsun->tx_urb = NULL;
	kingsun->tx_buf_clear = NULL;
	kingsun->tx_buf_clear_used = 0;
	kingsun->tx_buf_clear_sent = 0;

	kingsun->rx_urb = NULL;
	kingsun->rx_buf = NULL;
	kingsun->rx_unwrap_buff.in_frame = FALSE;
	kingsun->rx_unwrap_buff.state = OUTSIDE_FRAME;
	kingsun->rx_unwrap_buff.skb = NULL;
	kingsun->receiving = 0;
	spin_lock_init(&kingsun->lock);

	kingsun->speed_setuprequest = NULL;
	kingsun->speed_urb = NULL;
	kingsun->speedparams.baudrate = 0;

	/* Allocate input buffer */
	kingsun->rx_buf = kmalloc(KINGSUN_RCV_MAX, GFP_KERNEL);
	if (!kingsun->rx_buf)
		goto free_mem;

	/* Allocate output buffer */
	kingsun->tx_buf_clear = kmalloc(KINGSUN_SND_FIFO_SIZE, GFP_KERNEL);
	if (!kingsun->tx_buf_clear)
		goto free_mem;

	/* Allocate and initialize speed setup packet */
	kingsun->speed_setuprequest =
	    kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
	if (!kingsun->speed_setuprequest)
		goto free_mem;
	kingsun->speed_setuprequest->bRequestType =
	    USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
	kingsun->speed_setuprequest->bRequest = KINGSUN_REQ_SEND;
	kingsun->speed_setuprequest->wValue = cpu_to_le16(0x0200);
	kingsun->speed_setuprequest->wIndex = cpu_to_le16(0x0001);
	kingsun->speed_setuprequest->wLength =
	    cpu_to_le16(sizeof(struct ksdazzle_speedparams));

	printk(KERN_INFO "KingSun/Dazzle IRDA/USB found at address %d, "
	       "Vendor: %x, Product: %x\n",
	       dev->devnum, le16_to_cpu(dev->descriptor.idVendor),
	       le16_to_cpu(dev->descriptor.idProduct));

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&kingsun->qos);

	/* Baud rates known to be supported. Please uncomment if devices (other
	   than a SonyEriccson K300 phone) can be shown to support higher speeds
	   with this dongle.
	 */
	kingsun->qos.baud_rate.bits =
	    IR_2400 | IR_9600 | IR_19200 | IR_38400 | IR_57600 | IR_115200;
	kingsun->qos.min_turn_time.bits &= KINGSUN_MTT;
	irda_qos_bits_to_value(&kingsun->qos);

	/* Override the network functions we need to use */
	net->netdev_ops = &ksdazzle_ops;

	ret = register_netdev(net);
	if (ret != 0)
		goto free_mem;

	dev_info(&net->dev, "IrDA: Registered KingSun/Dazzle device %s\n",
		 net->name);

	usb_set_intfdata(intf, kingsun);

	/* Situation at this point:
	   - all work buffers allocated
	   - setup requests pre-filled
	   - urbs not allocated, set to NULL
	   - max rx packet known (is KINGSUN_FIFO_SIZE)
	   - unwrap state machine (partially) initialized, but skb == NULL
	 */

	return 0;

      free_mem:
	kfree(kingsun->speed_setuprequest);
	kfree(kingsun->tx_buf_clear);
	kfree(kingsun->rx_buf);
	free_netdev(net);
      err_out1:
	return ret;
}

/*
 * The current device is removed, the USB layer tell us to shut it down...
 */
static void ksdazzle_disconnect(struct usb_interface *intf)
{
	struct ksdazzle_cb *kingsun = usb_get_intfdata(intf);

	if (!kingsun)
		return;

	unregister_netdev(kingsun->netdev);

	/* Mop up receive && transmit urb's */
	usb_kill_urb(kingsun->speed_urb);
	usb_free_urb(kingsun->speed_urb);
	kingsun->speed_urb = NULL;

	usb_kill_urb(kingsun->tx_urb);
	usb_free_urb(kingsun->tx_urb);
	kingsun->tx_urb = NULL;

	usb_kill_urb(kingsun->rx_urb);
	usb_free_urb(kingsun->rx_urb);
	kingsun->rx_urb = NULL;

	kfree(kingsun->speed_setuprequest);
	kfree(kingsun->tx_buf_clear);
	kfree(kingsun->rx_buf);
	free_netdev(kingsun->netdev);

	usb_set_intfdata(intf, NULL);
}

#ifdef CONFIG_PM
/* USB suspend, so power off the transmitter/receiver */
static int ksdazzle_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct ksdazzle_cb *kingsun = usb_get_intfdata(intf);

	netif_device_detach(kingsun->netdev);
	if (kingsun->speed_urb != NULL)
		usb_kill_urb(kingsun->speed_urb);
	if (kingsun->tx_urb != NULL)
		usb_kill_urb(kingsun->tx_urb);
	if (kingsun->rx_urb != NULL)
		usb_kill_urb(kingsun->rx_urb);
	return 0;
}

/* Coming out of suspend, so reset hardware */
static int ksdazzle_resume(struct usb_interface *intf)
{
	struct ksdazzle_cb *kingsun = usb_get_intfdata(intf);

	if (kingsun->rx_urb != NULL) {
		/* Setup request already filled in ksdazzle_probe */
		usb_submit_urb(kingsun->rx_urb, GFP_KERNEL);
	}
	netif_device_attach(kingsun->netdev);

	return 0;
}
#endif

/*
 * USB device callbacks
 */
static struct usb_driver irda_driver = {
	.name = "ksdazzle-sir",
	.probe = ksdazzle_probe,
	.disconnect = ksdazzle_disconnect,
	.id_table = dongles,
#ifdef CONFIG_PM
	.suspend = ksdazzle_suspend,
	.resume = ksdazzle_resume,
#endif
};

/*
 * Module insertion
 */
static int __init ksdazzle_init(void)
{
	return usb_register(&irda_driver);
}

module_init(ksdazzle_init);

/*
 * Module removal
 */
static void __exit ksdazzle_cleanup(void)
{
	/* Deregister the driver and remove all pending instances */
	usb_deregister(&irda_driver);
}

module_exit(ksdazzle_cleanup);

MODULE_AUTHOR("Alex Villacís Lasso <a_villacis@palosanto.com>");
MODULE_DESCRIPTION("IrDA-USB Dongle Driver for KingSun Dazzle");
MODULE_LICENSE("GPL");
