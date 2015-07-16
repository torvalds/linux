/*****************************************************************************
*
* Filename:      ks959-sir.c
* Version:       0.1.2
* Description:   Irda KingSun KS-959 USB Dongle
* Status:        Experimental
* Author:        Alex Villacís Lasso <a_villacis@palosanto.com>
*         with help from Domen Puncer <domen@coderock.org>
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
 * Following is my most current (2007-07-17) understanding of how the Kingsun
 * KS-959 dongle is supposed to work. This information was deduced by
 * reverse-engineering and examining the USB traffic captured with USBSnoopy
 * from the WinXP driver. Feel free to update here as more of the dongle is
 * known.
 *
 * My most sincere thanks must go to Domen Puncer <domen@coderock.org> for
 * invaluable help in cracking the obfuscation and padding required for this
 * dongle.
 *
 * General: This dongle exposes one interface with one interrupt IN endpoint.
 * However, the interrupt endpoint is NOT used at all for this dongle. Instead,
 * this dongle uses control transfers for everything, including sending and
 * receiving the IrDA frame data. Apparently the interrupt endpoint is just a
 * dummy to ensure the dongle has a valid interface to present to the PC.And I
 * thought the DonShine dongle was weird... In addition, this dongle uses
 * obfuscation (?!?!), applied at the USB level, to hide the traffic, both sent
 * and received, from the dongle. I call it obfuscation because the XOR keying
 * and padding required to produce an USB traffic acceptable for the dongle can
 * not be explained by any other technical requirement.
 *
 * Transmission: To transmit an IrDA frame, the driver must prepare a control
 * URB with the following as a setup packet:
 *    bRequestType    USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE
 *    bRequest        0x09
 *    wValue          <length of valid data before padding, little endian>
 *    wIndex          0x0000
 *    wLength         <length of padded data>
 * The payload packet must be manually wrapped and escaped (as in stir4200.c),
 * then padded and obfuscated before being sent. Both padding and obfuscation
 * are implemented in the procedure obfuscate_tx_buffer(). Suffice to say, the
 * designer/programmer of the dongle used his name as a source for the
 * obfuscation. WTF?!
 * Apparently the dongle cannot handle payloads larger than 256 bytes. The
 * driver has to perform fragmentation in order to send anything larger than
 * this limit.
 *
 * Reception: To receive data, the driver must poll the dongle regularly (like
 * kingsun-sir.c) with control URBs and the following as a setup packet:
 *    bRequestType    USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE
 *    bRequest        0x01
 *    wValue          0x0200
 *    wIndex          0x0000
 *    wLength         0x0800 (size of available buffer)
 * If there is data to be read, it will be returned as the response payload.
 * This data is (apparently) not padded, but it is obfuscated. To de-obfuscate
 * it, the driver must XOR every byte, in sequence, with a value that starts at
 * 1 and is incremented with each byte processed, and then with 0x55. The value
 * incremented with each byte processed overflows as an unsigned char. The
 * resulting bytes form a wrapped SIR frame that is unwrapped and unescaped
 * as in stir4200.c The incremented value is NOT reset with each frame, but is
 * kept across the entire session with the dongle. Also, the dongle inserts an
 * extra garbage byte with value 0x95 (after decoding) every 0xff bytes, which
 * must be skipped.
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
 * necessary and announces support for all the way to 57600 bps. Although the
 * package announces support for up to 4MBps, tests with a Sony Ericcson K300
 * phone show corruption when receiving large frames at 115200 bps, the highest
 * speed announced by the phone. However, transmission at 115200 bps is OK. Go
 * figure. Since I don't know whether the phone or the dongle is at fault, max
 * announced speed is 57600 bps until someone produces a device that can run
 * at higher speeds with this dongle.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
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

#define KS959_VENDOR_ID 0x07d0
#define KS959_PRODUCT_ID 0x4959

/* These are the currently known USB ids */
static struct usb_device_id dongles[] = {
	/* KingSun Co,Ltd  IrDA/USB Bridge */
	{USB_DEVICE(KS959_VENDOR_ID, KS959_PRODUCT_ID)},
	{}
};

MODULE_DEVICE_TABLE(usb, dongles);

#define KINGSUN_MTT 0x07
#define KINGSUN_REQ_RECV 0x01
#define KINGSUN_REQ_SEND 0x09

#define KINGSUN_RCV_FIFO_SIZE    2048	/* Max length we can receive */
#define KINGSUN_SND_FIFO_SIZE    2048	/* Max packet we can send */
#define KINGSUN_SND_PACKET_SIZE    256	/* Max packet dongle can handle */

struct ks959_speedparams {
	__le32 baudrate;	/* baud rate, little endian */
	__u8 flags;
	__u8 reserved[3];
} __packed;

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

struct ks959_cb {
	struct usb_device *usbdev;	/* init: probe_irda */
	struct net_device *netdev;	/* network layer */
	struct irlap_cb *irlap;	/* The link layer we are binded to */

	struct qos_info qos;

	struct usb_ctrlrequest *tx_setuprequest;
	struct urb *tx_urb;
	__u8 *tx_buf_clear;
	unsigned int tx_buf_clear_used;
	unsigned int tx_buf_clear_sent;
	__u8 *tx_buf_xored;

	struct usb_ctrlrequest *rx_setuprequest;
	struct urb *rx_urb;
	__u8 *rx_buf;
	__u8 rx_variable_xormask;
	iobuff_t rx_unwrap_buff;

	struct usb_ctrlrequest *speed_setuprequest;
	struct urb *speed_urb;
	struct ks959_speedparams speedparams;
	unsigned int new_speed;

	spinlock_t lock;
	int receiving;
};

/* Procedure to perform the obfuscation/padding expected by the dongle
 *
 * buf_cleartext    (IN) Cleartext version of the IrDA frame to transmit
 * len_cleartext    (IN) Length of the cleartext version of IrDA frame
 * buf_xoredtext    (OUT) Obfuscated version of frame built by proc
 * len_maxbuf        (OUT) Maximum space available at buf_xoredtext
 *
 * (return)         length of obfuscated frame with padding
 *
 * If not enough space (as indicated by len_maxbuf vs. required padding),
 * zero is returned
 *
 * The value of lookup_string is actually a required portion of the algorithm.
 * Seems the designer of the dongle wanted to state who exactly is responsible
 * for implementing obfuscation. Send your best (or other) wishes to him ]:-)
 */
static unsigned int obfuscate_tx_buffer(const __u8 * buf_cleartext,
					unsigned int len_cleartext,
					__u8 * buf_xoredtext,
					unsigned int len_maxbuf)
{
	unsigned int len_xoredtext;

	/* Calculate required length with padding, check for necessary space */
	len_xoredtext = ((len_cleartext + 7) & ~0x7) + 0x10;
	if (len_xoredtext <= len_maxbuf) {
		static const __u8 lookup_string[] = "wangshuofei19710";
		__u8 xor_mask;

		/* Unlike the WinXP driver, we *do* clear out the padding */
		memset(buf_xoredtext, 0, len_xoredtext);

		xor_mask = lookup_string[(len_cleartext & 0x0f) ^ 0x06] ^ 0x55;

		while (len_cleartext-- > 0) {
			*buf_xoredtext++ = *buf_cleartext++ ^ xor_mask;
		}
	} else {
		len_xoredtext = 0;
	}
	return len_xoredtext;
}

/* Callback transmission routine */
static void ks959_speed_irq(struct urb *urb)
{
	/* unlink, shutdown, unplug, other nasties */
	if (urb->status != 0) {
		dev_err(&urb->dev->dev,
			"ks959_speed_irq: urb asynchronously failed - %d\n",
			urb->status);
	}
}

/* Send a control request to change speed of the dongle */
static int ks959_change_speed(struct ks959_cb *kingsun, unsigned speed)
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

	memset(&(kingsun->speedparams), 0, sizeof(struct ks959_speedparams));
	kingsun->speedparams.baudrate = cpu_to_le32(speed);
	kingsun->speedparams.flags = KS_DATA_8_BITS;

	/* speed_setuprequest pre-filled in ks959_probe */
	usb_fill_control_urb(kingsun->speed_urb, kingsun->usbdev,
			     usb_sndctrlpipe(kingsun->usbdev, 0),
			     (unsigned char *)kingsun->speed_setuprequest,
			     &(kingsun->speedparams),
			     sizeof(struct ks959_speedparams), ks959_speed_irq,
			     kingsun);
	kingsun->speed_urb->status = 0;
	err = usb_submit_urb(kingsun->speed_urb, GFP_ATOMIC);

	return err;
}

/* Submit one fragment of an IrDA frame to the dongle */
static void ks959_send_irq(struct urb *urb);
static int ks959_submit_tx_fragment(struct ks959_cb *kingsun)
{
	unsigned int padlen;
	unsigned int wraplen;
	int ret;

	/* Check whether current plaintext can produce a padded buffer that fits
	   within the range handled by the dongle */
	wraplen = (KINGSUN_SND_PACKET_SIZE & ~0x7) - 0x10;
	if (wraplen > kingsun->tx_buf_clear_used)
		wraplen = kingsun->tx_buf_clear_used;

	/* Perform dongle obfuscation. Also remove the portion of the frame that
	   was just obfuscated and will now be sent to the dongle. */
	padlen = obfuscate_tx_buffer(kingsun->tx_buf_clear, wraplen,
				     kingsun->tx_buf_xored,
				     KINGSUN_SND_PACKET_SIZE);

	/* Calculate how much data can be transmitted in this urb */
	kingsun->tx_setuprequest->wValue = cpu_to_le16(wraplen);
	kingsun->tx_setuprequest->wLength = cpu_to_le16(padlen);
	/* Rest of the fields were filled in ks959_probe */
	usb_fill_control_urb(kingsun->tx_urb, kingsun->usbdev,
			     usb_sndctrlpipe(kingsun->usbdev, 0),
			     (unsigned char *)kingsun->tx_setuprequest,
			     kingsun->tx_buf_xored, padlen,
			     ks959_send_irq, kingsun);
	kingsun->tx_urb->status = 0;
	ret = usb_submit_urb(kingsun->tx_urb, GFP_ATOMIC);

	/* Remember how much data was sent, in order to update at callback */
	kingsun->tx_buf_clear_sent = (ret == 0) ? wraplen : 0;
	return ret;
}

/* Callback transmission routine */
static void ks959_send_irq(struct urb *urb)
{
	struct ks959_cb *kingsun = urb->context;
	struct net_device *netdev = kingsun->netdev;
	int ret = 0;

	/* in process of stopping, just drop data */
	if (!netif_running(kingsun->netdev)) {
		dev_err(&kingsun->usbdev->dev,
			"ks959_send_irq: Network not running!\n");
		return;
	}

	/* unlink, shutdown, unplug, other nasties */
	if (urb->status != 0) {
		dev_err(&kingsun->usbdev->dev,
			"ks959_send_irq: urb asynchronously failed - %d\n",
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
			if ((ret = ks959_submit_tx_fragment(kingsun)) != 0) {
				dev_err(&kingsun->usbdev->dev,
					"ks959_send_irq: failed tx_urb submit: %d\n",
					ret);
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
				ks959_change_speed(kingsun, kingsun->new_speed);

			netif_wake_queue(netdev);
		}
	}
}

/*
 * Called from net/core when new frame is available.
 */
static netdev_tx_t ks959_hard_xmit(struct sk_buff *skb,
					 struct net_device *netdev)
{
	struct ks959_cb *kingsun;
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

	if ((ret = ks959_submit_tx_fragment(kingsun)) != 0) {
		dev_err(&kingsun->usbdev->dev,
			"ks959_hard_xmit: failed tx_urb submit: %d\n", ret);
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
static void ks959_rcv_irq(struct urb *urb)
{
	struct ks959_cb *kingsun = urb->context;
	int ret;

	/* in process of stopping, just drop data */
	if (!netif_running(kingsun->netdev)) {
		kingsun->receiving = 0;
		return;
	}

	/* unlink, shutdown, unplug, other nasties */
	if (urb->status != 0) {
		dev_err(&kingsun->usbdev->dev,
			"kingsun_rcv_irq: urb asynchronously failed - %d\n",
			urb->status);
		kingsun->receiving = 0;
		return;
	}

	if (urb->actual_length > 0) {
		__u8 *bytes = urb->transfer_buffer;
		unsigned int i;

		for (i = 0; i < urb->actual_length; i++) {
			/* De-obfuscation implemented here: variable portion of
			   xormask is incremented, and then used with the encoded
			   byte for the XOR. The result of the operation is used
			   to unwrap the SIR frame. */
			kingsun->rx_variable_xormask++;
			bytes[i] =
			    bytes[i] ^ kingsun->rx_variable_xormask ^ 0x55u;

			/* rx_variable_xormask doubles as an index counter so we
			   can skip the byte at 0xff (wrapped around to 0).
			 */
			if (kingsun->rx_variable_xormask != 0) {
				async_unwrap_char(kingsun->netdev,
						  &kingsun->netdev->stats,
						  &kingsun->rx_unwrap_buff,
						  bytes[i]);
			}
		}
		kingsun->receiving =
		    (kingsun->rx_unwrap_buff.state != OUTSIDE_FRAME) ? 1 : 0;
	}

	/* This urb has already been filled in kingsun_net_open. Setup
	   packet must be re-filled, but it is assumed that urb keeps the
	   pointer to the initial setup packet, as well as the payload buffer.
	   Setup packet is already pre-filled at ks959_probe.
	 */
	urb->status = 0;
	ret = usb_submit_urb(urb, GFP_ATOMIC);
}

/*
 * Function kingsun_net_open (dev)
 *
 *    Network device is taken up. Usually this is done by "ifconfig irda0 up"
 */
static int ks959_net_open(struct net_device *netdev)
{
	struct ks959_cb *kingsun = netdev_priv(netdev);
	int err = -ENOMEM;
	char hwname[16];

	/* At this point, urbs are NULL, and skb is NULL (see kingsun_probe) */
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
	err = ks959_change_speed(kingsun, 9600);
	if (err < 0)
		goto free_mem;

	/*
	 * Now that everything should be initialized properly,
	 * Open new IrLAP layer instance to take care of us...
	 */
	sprintf(hwname, "usb#%d", kingsun->usbdev->devnum);
	kingsun->irlap = irlap_open(netdev, &kingsun->qos, hwname);
	if (!kingsun->irlap) {
		err = -ENOMEM;
		dev_err(&kingsun->usbdev->dev, "irlap_open failed\n");
		goto free_mem;
	}

	/* Start reception. Setup request already pre-filled in ks959_probe */
	usb_fill_control_urb(kingsun->rx_urb, kingsun->usbdev,
			     usb_rcvctrlpipe(kingsun->usbdev, 0),
			     (unsigned char *)kingsun->rx_setuprequest,
			     kingsun->rx_buf, KINGSUN_RCV_FIFO_SIZE,
			     ks959_rcv_irq, kingsun);
	kingsun->rx_urb->status = 0;
	err = usb_submit_urb(kingsun->rx_urb, GFP_KERNEL);
	if (err) {
		dev_err(&kingsun->usbdev->dev,
			"first urb-submit failed: %d\n", err);
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
 * Function kingsun_net_close (kingsun)
 *
 *    Network device is taken down. Usually this is done by
 *    "ifconfig irda0 down"
 */
static int ks959_net_close(struct net_device *netdev)
{
	struct ks959_cb *kingsun = netdev_priv(netdev);

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
	if (kingsun->irlap)
		irlap_close(kingsun->irlap);

	kingsun->irlap = NULL;

	return 0;
}

/*
 * IOCTLs : Extra out-of-band network commands...
 */
static int ks959_net_ioctl(struct net_device *netdev, struct ifreq *rq, int cmd)
{
	struct if_irda_req *irq = (struct if_irda_req *)rq;
	struct ks959_cb *kingsun = netdev_priv(netdev);
	int ret = 0;

	switch (cmd) {
	case SIOCSBANDWIDTH:	/* Set bandwidth */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		/* Check if the device is still there */
		if (netif_device_present(kingsun->netdev))
			return ks959_change_speed(kingsun, irq->ifr_baudrate);
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

static const struct net_device_ops ks959_ops = {
	.ndo_start_xmit	= ks959_hard_xmit,
	.ndo_open	= ks959_net_open,
	.ndo_stop	= ks959_net_close,
	.ndo_do_ioctl	= ks959_net_ioctl,
};
/*
 * This routine is called by the USB subsystem for each new device
 * in the system. We need to check if the device is ours, and in
 * this case start handling it.
 */
static int ks959_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct ks959_cb *kingsun = NULL;
	struct net_device *net = NULL;
	int ret = -ENOMEM;

	/* Allocate network device container. */
	net = alloc_irdadev(sizeof(*kingsun));
	if (!net)
		goto err_out1;

	SET_NETDEV_DEV(net, &intf->dev);
	kingsun = netdev_priv(net);
	kingsun->netdev = net;
	kingsun->usbdev = dev;
	kingsun->irlap = NULL;
	kingsun->tx_setuprequest = NULL;
	kingsun->tx_urb = NULL;
	kingsun->tx_buf_clear = NULL;
	kingsun->tx_buf_xored = NULL;
	kingsun->tx_buf_clear_used = 0;
	kingsun->tx_buf_clear_sent = 0;

	kingsun->rx_setuprequest = NULL;
	kingsun->rx_urb = NULL;
	kingsun->rx_buf = NULL;
	kingsun->rx_variable_xormask = 0;
	kingsun->rx_unwrap_buff.in_frame = FALSE;
	kingsun->rx_unwrap_buff.state = OUTSIDE_FRAME;
	kingsun->rx_unwrap_buff.skb = NULL;
	kingsun->receiving = 0;
	spin_lock_init(&kingsun->lock);

	kingsun->speed_setuprequest = NULL;
	kingsun->speed_urb = NULL;
	kingsun->speedparams.baudrate = 0;

	/* Allocate input buffer */
	kingsun->rx_buf = kmalloc(KINGSUN_RCV_FIFO_SIZE, GFP_KERNEL);
	if (!kingsun->rx_buf)
		goto free_mem;

	/* Allocate input setup packet */
	kingsun->rx_setuprequest =
	    kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
	if (!kingsun->rx_setuprequest)
		goto free_mem;
	kingsun->rx_setuprequest->bRequestType =
	    USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
	kingsun->rx_setuprequest->bRequest = KINGSUN_REQ_RECV;
	kingsun->rx_setuprequest->wValue = cpu_to_le16(0x0200);
	kingsun->rx_setuprequest->wIndex = 0;
	kingsun->rx_setuprequest->wLength = cpu_to_le16(KINGSUN_RCV_FIFO_SIZE);

	/* Allocate output buffer */
	kingsun->tx_buf_clear = kmalloc(KINGSUN_SND_FIFO_SIZE, GFP_KERNEL);
	if (!kingsun->tx_buf_clear)
		goto free_mem;
	kingsun->tx_buf_xored = kmalloc(KINGSUN_SND_PACKET_SIZE, GFP_KERNEL);
	if (!kingsun->tx_buf_xored)
		goto free_mem;

	/* Allocate and initialize output setup packet */
	kingsun->tx_setuprequest =
	    kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
	if (!kingsun->tx_setuprequest)
		goto free_mem;
	kingsun->tx_setuprequest->bRequestType =
	    USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
	kingsun->tx_setuprequest->bRequest = KINGSUN_REQ_SEND;
	kingsun->tx_setuprequest->wValue = 0;
	kingsun->tx_setuprequest->wIndex = 0;
	kingsun->tx_setuprequest->wLength = 0;

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
	    cpu_to_le16(sizeof(struct ks959_speedparams));

	printk(KERN_INFO "KingSun KS-959 IRDA/USB found at address %d, "
	       "Vendor: %x, Product: %x\n",
	       dev->devnum, le16_to_cpu(dev->descriptor.idVendor),
	       le16_to_cpu(dev->descriptor.idProduct));

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&kingsun->qos);

	/* Baud rates known to be supported. Please uncomment if devices (other
	   than a SonyEriccson K300 phone) can be shown to support higher speed
	   with this dongle.
	 */
	kingsun->qos.baud_rate.bits =
	    IR_2400 | IR_9600 | IR_19200 | IR_38400 | IR_57600;
	kingsun->qos.min_turn_time.bits &= KINGSUN_MTT;
	irda_qos_bits_to_value(&kingsun->qos);

	/* Override the network functions we need to use */
	net->netdev_ops = &ks959_ops;

	ret = register_netdev(net);
	if (ret != 0)
		goto free_mem;

	dev_info(&net->dev, "IrDA: Registered KingSun KS-959 device %s\n",
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
	kfree(kingsun->tx_setuprequest);
	kfree(kingsun->tx_buf_xored);
	kfree(kingsun->tx_buf_clear);
	kfree(kingsun->rx_setuprequest);
	kfree(kingsun->rx_buf);
	free_netdev(net);
      err_out1:
	return ret;
}

/*
 * The current device is removed, the USB layer tell us to shut it down...
 */
static void ks959_disconnect(struct usb_interface *intf)
{
	struct ks959_cb *kingsun = usb_get_intfdata(intf);

	if (!kingsun)
		return;

	unregister_netdev(kingsun->netdev);

	/* Mop up receive && transmit urb's */
	if (kingsun->speed_urb != NULL) {
		usb_kill_urb(kingsun->speed_urb);
		usb_free_urb(kingsun->speed_urb);
		kingsun->speed_urb = NULL;
	}
	if (kingsun->tx_urb != NULL) {
		usb_kill_urb(kingsun->tx_urb);
		usb_free_urb(kingsun->tx_urb);
		kingsun->tx_urb = NULL;
	}
	if (kingsun->rx_urb != NULL) {
		usb_kill_urb(kingsun->rx_urb);
		usb_free_urb(kingsun->rx_urb);
		kingsun->rx_urb = NULL;
	}

	kfree(kingsun->speed_setuprequest);
	kfree(kingsun->tx_setuprequest);
	kfree(kingsun->tx_buf_xored);
	kfree(kingsun->tx_buf_clear);
	kfree(kingsun->rx_setuprequest);
	kfree(kingsun->rx_buf);
	free_netdev(kingsun->netdev);

	usb_set_intfdata(intf, NULL);
}

#ifdef CONFIG_PM
/* USB suspend, so power off the transmitter/receiver */
static int ks959_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct ks959_cb *kingsun = usb_get_intfdata(intf);

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
static int ks959_resume(struct usb_interface *intf)
{
	struct ks959_cb *kingsun = usb_get_intfdata(intf);

	if (kingsun->rx_urb != NULL) {
		/* Setup request already filled in ks959_probe */
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
	.name = "ks959-sir",
	.probe = ks959_probe,
	.disconnect = ks959_disconnect,
	.id_table = dongles,
#ifdef CONFIG_PM
	.suspend = ks959_suspend,
	.resume = ks959_resume,
#endif
};

module_usb_driver(irda_driver);

MODULE_AUTHOR("Alex Villacís Lasso <a_villacis@palosanto.com>");
MODULE_DESCRIPTION("IrDA-USB Dongle Driver for KingSun KS-959");
MODULE_LICENSE("GPL");
