/*
 * USB RedRat3 IR Transceiver rc-core driver
 *
 * Copyright (c) 2011 by Jarod Wilson <jarod@redhat.com>
 *  based heavily on the work of Stephen Cox, with additional
 *  help from RedRat Ltd.
 *
 * This driver began life based an an old version of the first-generation
 * lirc_mceusb driver from the lirc 0.7.2 distribution. It was then
 * significantly rewritten by Stephen Cox with the aid of RedRat Ltd's
 * Chris Dodge.
 *
 * The driver was then ported to rc-core and significantly rewritten again,
 * by Jarod, using the in-kernel mceusb driver as a guide, after an initial
 * port effort was started by Stephen.
 *
 * TODO LIST:
 * - fix lirc not showing repeats properly
 * --
 *
 * The RedRat3 is a USB transceiver with both send & receive,
 * with 2 separate sensors available for receive to enable
 * both good long range reception for general use, and good
 * short range reception when required for learning a signal.
 *
 * http://www.redrat.co.uk/
 *
 * It uses its own little protocol to communicate, the required
 * parts of which are embedded within this driver.
 * --
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <asm/unaligned.h>
#include <linux/device.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <media/rc-core.h>

/* Driver Information */
#define DRIVER_AUTHOR "Jarod Wilson <jarod@redhat.com>"
#define DRIVER_AUTHOR2 "The Dweller, Stephen Cox"
#define DRIVER_DESC "RedRat3 USB IR Transceiver Driver"
#define DRIVER_NAME "redrat3"

/* bulk data transfer types */
#define RR3_ERROR		0x01
#define RR3_MOD_SIGNAL_IN	0x20
#define RR3_MOD_SIGNAL_OUT	0x21

/* Get the RR firmware version */
#define RR3_FW_VERSION		0xb1
#define RR3_FW_VERSION_LEN	64
/* Send encoded signal bulk-sent earlier*/
#define RR3_TX_SEND_SIGNAL	0xb3
#define RR3_SET_IR_PARAM	0xb7
#define RR3_GET_IR_PARAM	0xb8
/* Blink the red LED on the device */
#define RR3_BLINK_LED		0xb9
/* Read serial number of device */
#define RR3_READ_SER_NO		0xba
#define RR3_SER_NO_LEN		4
/* Start capture with the RC receiver */
#define RR3_RC_DET_ENABLE	0xbb
/* Stop capture with the RC receiver */
#define RR3_RC_DET_DISABLE	0xbc
/* Return the status of RC detector capture */
#define RR3_RC_DET_STATUS	0xbd
/* Reset redrat */
#define RR3_RESET		0xa0

/* Max number of lengths in the signal. */
#define RR3_IR_IO_MAX_LENGTHS	0x01
/* Periods to measure mod. freq. */
#define RR3_IR_IO_PERIODS_MF	0x02
/* Size of memory for main signal data */
#define RR3_IR_IO_SIG_MEM_SIZE	0x03
/* Delta value when measuring lengths */
#define RR3_IR_IO_LENGTH_FUZZ	0x04
/* Timeout for end of signal detection */
#define RR3_IR_IO_SIG_TIMEOUT	0x05
/* Minimum value for pause recognition. */
#define RR3_IR_IO_MIN_PAUSE	0x06

/* Clock freq. of EZ-USB chip */
#define RR3_CLK			24000000
/* Clock periods per timer count */
#define RR3_CLK_PER_COUNT	12
/* (RR3_CLK / RR3_CLK_PER_COUNT) */
#define RR3_CLK_CONV_FACTOR	2000000
/* USB bulk-in IR data endpoint address */
#define RR3_BULK_IN_EP_ADDR	0x82

/* Size of the fixed-length portion of the signal */
#define RR3_DRIVER_MAXLENS	128
#define RR3_MAX_SIG_SIZE	512
#define RR3_TIME_UNIT		50
#define RR3_END_OF_SIGNAL	0x7f
#define RR3_TX_TRAILER_LEN	2
#define RR3_RX_MIN_TIMEOUT	5
#define RR3_RX_MAX_TIMEOUT	2000

/* The 8051's CPUCS Register address */
#define RR3_CPUCS_REG_ADDR	0x7f92

#define USB_RR3USB_VENDOR_ID	0x112a
#define USB_RR3USB_PRODUCT_ID	0x0001
#define USB_RR3IIUSB_PRODUCT_ID	0x0005

struct redrat3_header {
	__be16 length;
	__be16 transfer_type;
} __packed;

/* sending and receiving irdata */
struct redrat3_irdata {
	struct redrat3_header header;
	__be32 pause;
	__be16 mod_freq_count;
	__be16 num_periods;
	__u8 max_lengths;
	__u8 no_lengths;
	__be16 max_sig_size;
	__be16 sig_size;
	__u8 no_repeats;
	__be16 lens[RR3_DRIVER_MAXLENS]; /* not aligned */
	__u8 sigdata[RR3_MAX_SIG_SIZE];
} __packed;

/* firmware errors */
struct redrat3_error {
	struct redrat3_header header;
	__be16 fw_error;
} __packed;

/* table of devices that work with this driver */
static struct usb_device_id redrat3_dev_table[] = {
	/* Original version of the RedRat3 */
	{USB_DEVICE(USB_RR3USB_VENDOR_ID, USB_RR3USB_PRODUCT_ID)},
	/* Second Version/release of the RedRat3 - RetRat3-II */
	{USB_DEVICE(USB_RR3USB_VENDOR_ID, USB_RR3IIUSB_PRODUCT_ID)},
	{}			/* Terminating entry */
};

/* Structure to hold all of our device specific stuff */
struct redrat3_dev {
	/* core device bits */
	struct rc_dev *rc;
	struct device *dev;

	/* led control */
	struct led_classdev led;
	atomic_t flash;
	struct usb_ctrlrequest flash_control;
	struct urb *flash_urb;
	u8 flash_in_buf;

	/* save off the usb device pointer */
	struct usb_device *udev;

	/* the receive endpoint */
	struct usb_endpoint_descriptor *ep_in;
	/* the buffer to receive data */
	void *bulk_in_buf;
	/* urb used to read ir data */
	struct urb *read_urb;

	/* the send endpoint */
	struct usb_endpoint_descriptor *ep_out;

	/* usb dma */
	dma_addr_t dma_in;

	/* rx signal timeout timer */
	struct timer_list rx_timeout;
	u32 hw_timeout;

	/* Is the device currently transmitting?*/
	bool transmitting;

	/* store for current packet */
	struct redrat3_irdata irdata;
	u16 bytes_read;

	u32 carrier;

	char name[64];
	char phys[64];
};

/*
 * redrat3_issue_async
 *
 *  Issues an async read to the ir data in port..
 *  sets the callback to be redrat3_handle_async
 */
static void redrat3_issue_async(struct redrat3_dev *rr3)
{
	int res;

	res = usb_submit_urb(rr3->read_urb, GFP_ATOMIC);
	if (res)
		dev_dbg(rr3->dev,
			"%s: receive request FAILED! (res %d, len %d)\n",
			__func__, res, rr3->read_urb->transfer_buffer_length);
}

static void redrat3_dump_fw_error(struct redrat3_dev *rr3, int code)
{
	if (!rr3->transmitting && (code != 0x40))
		dev_info(rr3->dev, "fw error code 0x%02x: ", code);

	switch (code) {
	case 0x00:
		pr_cont("No Error\n");
		break;

	/* Codes 0x20 through 0x2f are IR Firmware Errors */
	case 0x20:
		pr_cont("Initial signal pulse not long enough "
			"to measure carrier frequency\n");
		break;
	case 0x21:
		pr_cont("Not enough length values allocated for signal\n");
		break;
	case 0x22:
		pr_cont("Not enough memory allocated for signal data\n");
		break;
	case 0x23:
		pr_cont("Too many signal repeats\n");
		break;
	case 0x28:
		pr_cont("Insufficient memory available for IR signal "
			"data memory allocation\n");
		break;
	case 0x29:
		pr_cont("Insufficient memory available "
			"for IrDa signal data memory allocation\n");
		break;

	/* Codes 0x30 through 0x3f are USB Firmware Errors */
	case 0x30:
		pr_cont("Insufficient memory available for bulk "
			"transfer structure\n");
		break;

	/*
	 * Other error codes... These are primarily errors that can occur in
	 * the control messages sent to the redrat
	 */
	case 0x40:
		if (!rr3->transmitting)
			pr_cont("Signal capture has been terminated\n");
		break;
	case 0x41:
		pr_cont("Attempt to set/get and unknown signal I/O "
			"algorithm parameter\n");
		break;
	case 0x42:
		pr_cont("Signal capture already started\n");
		break;

	default:
		pr_cont("Unknown Error\n");
		break;
	}
}

static u32 redrat3_val_to_mod_freq(struct redrat3_irdata *irdata)
{
	u32 mod_freq = 0;
	u16 mod_freq_count = be16_to_cpu(irdata->mod_freq_count);

	if (mod_freq_count != 0)
		mod_freq = (RR3_CLK * be16_to_cpu(irdata->num_periods)) /
			(mod_freq_count * RR3_CLK_PER_COUNT);

	return mod_freq;
}

/* this function scales down the figures for the same result... */
static u32 redrat3_len_to_us(u32 length)
{
	u32 biglen = length * 1000;
	u32 divisor = (RR3_CLK_CONV_FACTOR) / 1000;
	u32 result = (u32) (biglen / divisor);

	/* don't allow zero lengths to go back, breaks lirc */
	return result ? result : 1;
}

/*
 * convert us back into redrat3 lengths
 *
 * length * 1000   length * 1000000
 * ------------- = ---------------- = micro
 * rr3clk / 1000       rr3clk

 * 6 * 2       4 * 3        micro * rr3clk          micro * rr3clk / 1000
 * ----- = 4   ----- = 6    -------------- = len    ---------------------
 *   3           2             1000000                    1000
 */
static u32 redrat3_us_to_len(u32 microsec)
{
	u32 result;
	u32 divisor;

	microsec &= IR_MAX_DURATION;
	divisor = (RR3_CLK_CONV_FACTOR / 1000);
	result = (u32)(microsec * divisor) / 1000;

	/* don't allow zero lengths to go back, breaks lirc */
	return result ? result : 1;
}

/* timer callback to send reset event */
static void redrat3_rx_timeout(unsigned long data)
{
	struct redrat3_dev *rr3 = (struct redrat3_dev *)data;

	dev_dbg(rr3->dev, "calling ir_raw_event_reset\n");
	ir_raw_event_reset(rr3->rc);
}

static void redrat3_process_ir_data(struct redrat3_dev *rr3)
{
	DEFINE_IR_RAW_EVENT(rawir);
	struct device *dev;
	unsigned i, trailer = 0;
	unsigned sig_size, single_len, offset, val;
	unsigned long delay;
	u32 mod_freq;

	if (!rr3) {
		pr_err("%s called with no context!\n", __func__);
		return;
	}

	dev = rr3->dev;

	/* Make sure we reset the IR kfifo after a bit of inactivity */
	delay = usecs_to_jiffies(rr3->hw_timeout);
	mod_timer(&rr3->rx_timeout, jiffies + delay);

	mod_freq = redrat3_val_to_mod_freq(&rr3->irdata);
	dev_dbg(dev, "Got mod_freq of %u\n", mod_freq);

	/* process each rr3 encoded byte into an int */
	sig_size = be16_to_cpu(rr3->irdata.sig_size);
	for (i = 0; i < sig_size; i++) {
		offset = rr3->irdata.sigdata[i];
		val = get_unaligned_be16(&rr3->irdata.lens[offset]);
		single_len = redrat3_len_to_us(val);

		/* we should always get pulse/space/pulse/space samples */
		if (i % 2)
			rawir.pulse = false;
		else
			rawir.pulse = true;

		rawir.duration = US_TO_NS(single_len);
		/* Save initial pulse length to fudge trailer */
		if (i == 0)
			trailer = rawir.duration;
		/* cap the value to IR_MAX_DURATION */
		rawir.duration &= IR_MAX_DURATION;

		dev_dbg(dev, "storing %s with duration %d (i: %d)\n",
			rawir.pulse ? "pulse" : "space", rawir.duration, i);
		ir_raw_event_store_with_filter(rr3->rc, &rawir);
	}

	/* add a trailing space, if need be */
	if (i % 2) {
		rawir.pulse = false;
		/* this duration is made up, and may not be ideal... */
		if (trailer < US_TO_NS(1000))
			rawir.duration = US_TO_NS(2800);
		else
			rawir.duration = trailer;
		dev_dbg(dev, "storing trailing space with duration %d\n",
			rawir.duration);
		ir_raw_event_store_with_filter(rr3->rc, &rawir);
	}

	dev_dbg(dev, "calling ir_raw_event_handle\n");
	ir_raw_event_handle(rr3->rc);
}

/* Util fn to send rr3 cmds */
static u8 redrat3_send_cmd(int cmd, struct redrat3_dev *rr3)
{
	struct usb_device *udev;
	u8 *data;
	int res;

	data = kzalloc(sizeof(u8), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	udev = rr3->udev;
	res = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0), cmd,
			      USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			      0x0000, 0x0000, data, sizeof(u8), HZ * 10);

	if (res < 0) {
		dev_err(rr3->dev, "%s: Error sending rr3 cmd res %d, data %d",
			__func__, res, *data);
		res = -EIO;
	} else
		res = data[0];

	kfree(data);

	return res;
}

/* Enables the long range detector and starts async receive */
static int redrat3_enable_detector(struct redrat3_dev *rr3)
{
	struct device *dev = rr3->dev;
	u8 ret;

	ret = redrat3_send_cmd(RR3_RC_DET_ENABLE, rr3);
	if (ret != 0)
		dev_dbg(dev, "%s: unexpected ret of %d\n",
			__func__, ret);

	ret = redrat3_send_cmd(RR3_RC_DET_STATUS, rr3);
	if (ret != 1) {
		dev_err(dev, "%s: detector status: %d, should be 1\n",
			__func__, ret);
		return -EIO;
	}

	redrat3_issue_async(rr3);

	return 0;
}

static inline void redrat3_delete(struct redrat3_dev *rr3,
				  struct usb_device *udev)
{
	usb_kill_urb(rr3->read_urb);
	usb_kill_urb(rr3->flash_urb);
	usb_free_urb(rr3->read_urb);
	usb_free_urb(rr3->flash_urb);
	usb_free_coherent(udev, le16_to_cpu(rr3->ep_in->wMaxPacketSize),
			  rr3->bulk_in_buf, rr3->dma_in);

	kfree(rr3);
}

static u32 redrat3_get_timeout(struct redrat3_dev *rr3)
{
	__be32 *tmp;
	u32 timeout = MS_TO_US(150); /* a sane default, if things go haywire */
	int len, ret, pipe;

	len = sizeof(*tmp);
	tmp = kzalloc(len, GFP_KERNEL);
	if (!tmp) {
		dev_warn(rr3->dev, "Memory allocation faillure\n");
		return timeout;
	}

	pipe = usb_rcvctrlpipe(rr3->udev, 0);
	ret = usb_control_msg(rr3->udev, pipe, RR3_GET_IR_PARAM,
			      USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			      RR3_IR_IO_SIG_TIMEOUT, 0, tmp, len, HZ * 5);
	if (ret != len)
		dev_warn(rr3->dev, "Failed to read timeout from hardware\n");
	else {
		timeout = redrat3_len_to_us(be32_to_cpup(tmp));

		dev_dbg(rr3->dev, "Got timeout of %d ms\n", timeout / 1000);
	}

	kfree(tmp);

	return timeout;
}

static void redrat3_reset(struct redrat3_dev *rr3)
{
	struct usb_device *udev = rr3->udev;
	struct device *dev = rr3->dev;
	int rc, rxpipe, txpipe;
	u8 *val;
	int len = sizeof(u8);

	rxpipe = usb_rcvctrlpipe(udev, 0);
	txpipe = usb_sndctrlpipe(udev, 0);

	val = kmalloc(len, GFP_KERNEL);
	if (!val) {
		dev_err(dev, "Memory allocation failure\n");
		return;
	}

	*val = 0x01;
	rc = usb_control_msg(udev, rxpipe, RR3_RESET,
			     USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			     RR3_CPUCS_REG_ADDR, 0, val, len, HZ * 25);
	dev_dbg(dev, "reset returned 0x%02x\n", rc);

	*val = 5;
	rc = usb_control_msg(udev, txpipe, RR3_SET_IR_PARAM,
			     USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
			     RR3_IR_IO_LENGTH_FUZZ, 0, val, len, HZ * 25);
	dev_dbg(dev, "set ir parm len fuzz %d rc 0x%02x\n", *val, rc);

	*val = RR3_DRIVER_MAXLENS;
	rc = usb_control_msg(udev, txpipe, RR3_SET_IR_PARAM,
			     USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
			     RR3_IR_IO_MAX_LENGTHS, 0, val, len, HZ * 25);
	dev_dbg(dev, "set ir parm max lens %d rc 0x%02x\n", *val, rc);

	kfree(val);
}

static void redrat3_get_firmware_rev(struct redrat3_dev *rr3)
{
	int rc = 0;
	char *buffer;

	buffer = kzalloc(sizeof(char) * (RR3_FW_VERSION_LEN + 1), GFP_KERNEL);
	if (!buffer) {
		dev_err(rr3->dev, "Memory allocation failure\n");
		return;
	}

	rc = usb_control_msg(rr3->udev, usb_rcvctrlpipe(rr3->udev, 0),
			     RR3_FW_VERSION,
			     USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			     0, 0, buffer, RR3_FW_VERSION_LEN, HZ * 5);

	if (rc >= 0)
		dev_info(rr3->dev, "Firmware rev: %s", buffer);
	else
		dev_err(rr3->dev, "Problem fetching firmware ID\n");

	kfree(buffer);
}

static void redrat3_read_packet_start(struct redrat3_dev *rr3, unsigned len)
{
	struct redrat3_header *header = rr3->bulk_in_buf;
	unsigned pktlen, pkttype;

	/* grab the Length and type of transfer */
	pktlen = be16_to_cpu(header->length);
	pkttype = be16_to_cpu(header->transfer_type);

	if (pktlen > sizeof(rr3->irdata)) {
		dev_warn(rr3->dev, "packet length %u too large\n", pktlen);
		return;
	}

	switch (pkttype) {
	case RR3_ERROR:
		if (len >= sizeof(struct redrat3_error)) {
			struct redrat3_error *error = rr3->bulk_in_buf;
			unsigned fw_error = be16_to_cpu(error->fw_error);
			redrat3_dump_fw_error(rr3, fw_error);
		}
		break;

	case RR3_MOD_SIGNAL_IN:
		memcpy(&rr3->irdata, rr3->bulk_in_buf, len);
		rr3->bytes_read = len;
		dev_dbg(rr3->dev, "bytes_read %d, pktlen %d\n",
			rr3->bytes_read, pktlen);
		break;

	default:
		dev_dbg(rr3->dev, "ignoring packet with type 0x%02x, len of %d, 0x%02x\n",
						pkttype, len, pktlen);
		break;
	}
}

static void redrat3_read_packet_continue(struct redrat3_dev *rr3, unsigned len)
{
	void *irdata = &rr3->irdata;

	if (len + rr3->bytes_read > sizeof(rr3->irdata)) {
		dev_warn(rr3->dev, "too much data for packet\n");
		rr3->bytes_read = 0;
		return;
	}

	memcpy(irdata + rr3->bytes_read, rr3->bulk_in_buf, len);

	rr3->bytes_read += len;
	dev_dbg(rr3->dev, "bytes_read %d, pktlen %d\n", rr3->bytes_read,
				 be16_to_cpu(rr3->irdata.header.length));
}

/* gather IR data from incoming urb, process it when we have enough */
static int redrat3_get_ir_data(struct redrat3_dev *rr3, unsigned len)
{
	struct device *dev = rr3->dev;
	unsigned pkttype;
	int ret = 0;

	if (rr3->bytes_read == 0 && len >= sizeof(struct redrat3_header)) {
		redrat3_read_packet_start(rr3, len);
	} else if (rr3->bytes_read != 0) {
		redrat3_read_packet_continue(rr3, len);
	} else if (rr3->bytes_read == 0) {
		dev_err(dev, "error: no packet data read\n");
		ret = -ENODATA;
		goto out;
	}

	if (rr3->bytes_read < be16_to_cpu(rr3->irdata.header.length) +
						sizeof(struct redrat3_header))
		/* we're still accumulating data */
		return 0;

	/* if we get here, we've got IR data to decode */
	pkttype = be16_to_cpu(rr3->irdata.header.transfer_type);
	if (pkttype == RR3_MOD_SIGNAL_IN)
		redrat3_process_ir_data(rr3);
	else
		dev_dbg(dev, "discarding non-signal data packet (type 0x%02x)\n",
								pkttype);

out:
	rr3->bytes_read = 0;
	return ret;
}

/* callback function from USB when async USB request has completed */
static void redrat3_handle_async(struct urb *urb)
{
	struct redrat3_dev *rr3;
	int ret;

	if (!urb)
		return;

	rr3 = urb->context;
	if (!rr3) {
		pr_err("%s called with invalid context!\n", __func__);
		usb_unlink_urb(urb);
		return;
	}

	switch (urb->status) {
	case 0:
		ret = redrat3_get_ir_data(rr3, urb->actual_length);
		if (!ret) {
			/* no error, prepare to read more */
			redrat3_issue_async(rr3);
		}
		break;

	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		usb_unlink_urb(urb);
		return;

	case -EPIPE:
	default:
		dev_warn(rr3->dev, "Error: urb status = %d\n", urb->status);
		rr3->bytes_read = 0;
		break;
	}
}

static u16 mod_freq_to_val(unsigned int mod_freq)
{
	int mult = 6000000;

	/* Clk used in mod. freq. generation is CLK24/4. */
	return 65536 - (mult / mod_freq);
}

static int redrat3_set_tx_carrier(struct rc_dev *rcdev, u32 carrier)
{
	struct redrat3_dev *rr3 = rcdev->priv;
	struct device *dev = rr3->dev;

	dev_dbg(dev, "Setting modulation frequency to %u", carrier);
	if (carrier == 0)
		return -EINVAL;

	rr3->carrier = carrier;

	return carrier;
}

static int redrat3_transmit_ir(struct rc_dev *rcdev, unsigned *txbuf,
				unsigned count)
{
	struct redrat3_dev *rr3 = rcdev->priv;
	struct device *dev = rr3->dev;
	struct redrat3_irdata *irdata = NULL;
	int ret, ret_len;
	int lencheck, cur_sample_len, pipe;
	int *sample_lens = NULL;
	u8 curlencheck = 0;
	unsigned i, sendbuf_len;

	if (rr3->transmitting) {
		dev_warn(dev, "%s: transmitter already in use\n", __func__);
		return -EAGAIN;
	}

	if (count > RR3_MAX_SIG_SIZE - RR3_TX_TRAILER_LEN)
		return -EINVAL;

	/* rr3 will disable rc detector on transmit */
	rr3->transmitting = true;

	sample_lens = kzalloc(sizeof(int) * RR3_DRIVER_MAXLENS, GFP_KERNEL);
	if (!sample_lens) {
		ret = -ENOMEM;
		goto out;
	}

	irdata = kzalloc(sizeof(*irdata), GFP_KERNEL);
	if (!irdata) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < count; i++) {
		cur_sample_len = redrat3_us_to_len(txbuf[i]);
		if (cur_sample_len > 0xffff) {
			dev_warn(dev, "transmit period of %uus truncated to %uus\n",
					txbuf[i], redrat3_len_to_us(0xffff));
			cur_sample_len = 0xffff;
		}
		for (lencheck = 0; lencheck < curlencheck; lencheck++) {
			if (sample_lens[lencheck] == cur_sample_len)
				break;
		}
		if (lencheck == curlencheck) {
			dev_dbg(dev, "txbuf[%d]=%u, pos %d, enc %u\n",
				i, txbuf[i], curlencheck, cur_sample_len);
			if (curlencheck < RR3_DRIVER_MAXLENS) {
				/* now convert the value to a proper
				 * rr3 value.. */
				sample_lens[curlencheck] = cur_sample_len;
				put_unaligned_be16(cur_sample_len,
						&irdata->lens[curlencheck]);
				curlencheck++;
			} else {
				ret = -EINVAL;
				goto out;
			}
		}
		irdata->sigdata[i] = lencheck;
	}

	irdata->sigdata[count] = RR3_END_OF_SIGNAL;
	irdata->sigdata[count + 1] = RR3_END_OF_SIGNAL;

	sendbuf_len = offsetof(struct redrat3_irdata,
					sigdata[count + RR3_TX_TRAILER_LEN]);
	/* fill in our packet header */
	irdata->header.length = cpu_to_be16(sendbuf_len -
						sizeof(struct redrat3_header));
	irdata->header.transfer_type = cpu_to_be16(RR3_MOD_SIGNAL_OUT);
	irdata->pause = cpu_to_be32(redrat3_len_to_us(100));
	irdata->mod_freq_count = cpu_to_be16(mod_freq_to_val(rr3->carrier));
	irdata->no_lengths = curlencheck;
	irdata->sig_size = cpu_to_be16(count + RR3_TX_TRAILER_LEN);

	pipe = usb_sndbulkpipe(rr3->udev, rr3->ep_out->bEndpointAddress);
	ret = usb_bulk_msg(rr3->udev, pipe, irdata,
			    sendbuf_len, &ret_len, 10 * HZ);
	dev_dbg(dev, "sent %d bytes, (ret %d)\n", ret_len, ret);

	/* now tell the hardware to transmit what we sent it */
	pipe = usb_rcvctrlpipe(rr3->udev, 0);
	ret = usb_control_msg(rr3->udev, pipe, RR3_TX_SEND_SIGNAL,
			      USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			      0, 0, irdata, 2, HZ * 10);

	if (ret < 0)
		dev_err(dev, "Error: control msg send failed, rc %d\n", ret);
	else
		ret = count;

out:
	kfree(sample_lens);
	kfree(irdata);

	rr3->transmitting = false;
	/* rr3 re-enables rc detector because it was enabled before */

	return ret;
}

static void redrat3_brightness_set(struct led_classdev *led_dev, enum
						led_brightness brightness)
{
	struct redrat3_dev *rr3 = container_of(led_dev, struct redrat3_dev,
									led);

	if (brightness != LED_OFF && atomic_cmpxchg(&rr3->flash, 0, 1) == 0) {
		int ret = usb_submit_urb(rr3->flash_urb, GFP_ATOMIC);
		if (ret != 0) {
			dev_dbg(rr3->dev, "%s: unexpected ret of %d\n",
				__func__, ret);
			atomic_set(&rr3->flash, 0);
		}
	}
}

static void redrat3_led_complete(struct urb *urb)
{
	struct redrat3_dev *rr3 = urb->context;

	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		usb_unlink_urb(urb);
		return;
	case -EPIPE:
	default:
		dev_dbg(rr3->dev, "Error: urb status = %d\n", urb->status);
		break;
	}

	rr3->led.brightness = LED_OFF;
	atomic_dec(&rr3->flash);
}

static struct rc_dev *redrat3_init_rc_dev(struct redrat3_dev *rr3)
{
	struct device *dev = rr3->dev;
	struct rc_dev *rc;
	int ret = -ENODEV;
	u16 prod = le16_to_cpu(rr3->udev->descriptor.idProduct);

	rc = rc_allocate_device();
	if (!rc) {
		dev_err(dev, "remote input dev allocation failed\n");
		goto out;
	}

	snprintf(rr3->name, sizeof(rr3->name), "RedRat3%s "
		 "Infrared Remote Transceiver (%04x:%04x)",
		 prod == USB_RR3IIUSB_PRODUCT_ID ? "-II" : "",
		 le16_to_cpu(rr3->udev->descriptor.idVendor), prod);

	usb_make_path(rr3->udev, rr3->phys, sizeof(rr3->phys));

	rc->input_name = rr3->name;
	rc->input_phys = rr3->phys;
	usb_to_input_id(rr3->udev, &rc->input_id);
	rc->dev.parent = dev;
	rc->priv = rr3;
	rc->driver_type = RC_DRIVER_IR_RAW;
	rc->allowed_protocols = RC_BIT_ALL;
	rc->timeout = US_TO_NS(2750);
	rc->tx_ir = redrat3_transmit_ir;
	rc->s_tx_carrier = redrat3_set_tx_carrier;
	rc->driver_name = DRIVER_NAME;
	rc->rx_resolution = US_TO_NS(2);
	rc->map_name = RC_MAP_HAUPPAUGE;

	ret = rc_register_device(rc);
	if (ret < 0) {
		dev_err(dev, "remote dev registration failed\n");
		goto out;
	}

	return rc;

out:
	rc_free_device(rc);
	return NULL;
}

static int redrat3_dev_probe(struct usb_interface *intf,
			     const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct device *dev = &intf->dev;
	struct usb_host_interface *uhi;
	struct redrat3_dev *rr3;
	struct usb_endpoint_descriptor *ep;
	struct usb_endpoint_descriptor *ep_in = NULL;
	struct usb_endpoint_descriptor *ep_out = NULL;
	u8 addr, attrs;
	int pipe, i;
	int retval = -ENOMEM;

	uhi = intf->cur_altsetting;

	/* find our bulk-in and bulk-out endpoints */
	for (i = 0; i < uhi->desc.bNumEndpoints; ++i) {
		ep = &uhi->endpoint[i].desc;
		addr = ep->bEndpointAddress;
		attrs = ep->bmAttributes;

		if ((ep_in == NULL) &&
		    ((addr & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN) &&
		    ((attrs & USB_ENDPOINT_XFERTYPE_MASK) ==
		     USB_ENDPOINT_XFER_BULK)) {
			dev_dbg(dev, "found bulk-in endpoint at 0x%02x\n",
				ep->bEndpointAddress);
			/* data comes in on 0x82, 0x81 is for other data... */
			if (ep->bEndpointAddress == RR3_BULK_IN_EP_ADDR)
				ep_in = ep;
		}

		if ((ep_out == NULL) &&
		    ((addr & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT) &&
		    ((attrs & USB_ENDPOINT_XFERTYPE_MASK) ==
		     USB_ENDPOINT_XFER_BULK)) {
			dev_dbg(dev, "found bulk-out endpoint at 0x%02x\n",
				ep->bEndpointAddress);
			ep_out = ep;
		}
	}

	if (!ep_in || !ep_out) {
		dev_err(dev, "Couldn't find both in and out endpoints\n");
		retval = -ENODEV;
		goto no_endpoints;
	}

	/* allocate memory for our device state and initialize it */
	rr3 = kzalloc(sizeof(*rr3), GFP_KERNEL);
	if (rr3 == NULL) {
		dev_err(dev, "Memory allocation failure\n");
		goto no_endpoints;
	}

	rr3->dev = &intf->dev;

	/* set up bulk-in endpoint */
	rr3->read_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!rr3->read_urb) {
		dev_err(dev, "Read urb allocation failure\n");
		goto error;
	}

	rr3->ep_in = ep_in;
	rr3->bulk_in_buf = usb_alloc_coherent(udev,
		le16_to_cpu(ep_in->wMaxPacketSize), GFP_KERNEL, &rr3->dma_in);
	if (!rr3->bulk_in_buf) {
		dev_err(dev, "Read buffer allocation failure\n");
		goto error;
	}

	pipe = usb_rcvbulkpipe(udev, ep_in->bEndpointAddress);
	usb_fill_bulk_urb(rr3->read_urb, udev, pipe, rr3->bulk_in_buf,
		le16_to_cpu(ep_in->wMaxPacketSize), redrat3_handle_async, rr3);
	rr3->read_urb->transfer_dma = rr3->dma_in;
	rr3->read_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	rr3->ep_out = ep_out;
	rr3->udev = udev;

	redrat3_reset(rr3);
	redrat3_get_firmware_rev(rr3);

	/* might be all we need to do? */
	retval = redrat3_enable_detector(rr3);
	if (retval < 0)
		goto error;

	/* store current hardware timeout, in us, will use for kfifo resets */
	rr3->hw_timeout = redrat3_get_timeout(rr3);

	/* default.. will get overridden by any sends with a freq defined */
	rr3->carrier = 38000;

	/* led control */
	rr3->led.name = "redrat3:red:feedback";
	rr3->led.default_trigger = "rc-feedback";
	rr3->led.brightness_set = redrat3_brightness_set;
	retval = led_classdev_register(&intf->dev, &rr3->led);
	if (retval)
		goto error;

	atomic_set(&rr3->flash, 0);
	rr3->flash_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!rr3->flash_urb) {
		retval = -ENOMEM;
		goto led_free_error;
	}

	/* setup packet is 'c0 b9 0000 0000 0001' */
	rr3->flash_control.bRequestType = 0xc0;
	rr3->flash_control.bRequest = RR3_BLINK_LED;
	rr3->flash_control.wLength = cpu_to_le16(1);

	usb_fill_control_urb(rr3->flash_urb, udev, usb_rcvctrlpipe(udev, 0),
			(unsigned char *)&rr3->flash_control,
			&rr3->flash_in_buf, sizeof(rr3->flash_in_buf),
			redrat3_led_complete, rr3);

	rr3->rc = redrat3_init_rc_dev(rr3);
	if (!rr3->rc) {
		retval = -ENOMEM;
		goto led_free_error;
	}
	setup_timer(&rr3->rx_timeout, redrat3_rx_timeout, (unsigned long)rr3);

	/* we can register the device now, as it is ready */
	usb_set_intfdata(intf, rr3);

	return 0;

led_free_error:
	led_classdev_unregister(&rr3->led);
error:
	redrat3_delete(rr3, rr3->udev);

no_endpoints:
	dev_err(dev, "%s: retval = %x", __func__, retval);

	return retval;
}

static void redrat3_dev_disconnect(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct redrat3_dev *rr3 = usb_get_intfdata(intf);

	if (!rr3)
		return;

	usb_set_intfdata(intf, NULL);
	rc_unregister_device(rr3->rc);
	led_classdev_unregister(&rr3->led);
	del_timer_sync(&rr3->rx_timeout);
	redrat3_delete(rr3, udev);
}

static int redrat3_dev_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct redrat3_dev *rr3 = usb_get_intfdata(intf);

	led_classdev_suspend(&rr3->led);
	usb_kill_urb(rr3->read_urb);
	usb_kill_urb(rr3->flash_urb);
	return 0;
}

static int redrat3_dev_resume(struct usb_interface *intf)
{
	struct redrat3_dev *rr3 = usb_get_intfdata(intf);

	if (usb_submit_urb(rr3->read_urb, GFP_ATOMIC))
		return -EIO;
	led_classdev_resume(&rr3->led);
	return 0;
}

static struct usb_driver redrat3_dev_driver = {
	.name		= DRIVER_NAME,
	.probe		= redrat3_dev_probe,
	.disconnect	= redrat3_dev_disconnect,
	.suspend	= redrat3_dev_suspend,
	.resume		= redrat3_dev_resume,
	.reset_resume	= redrat3_dev_resume,
	.id_table	= redrat3_dev_table
};

module_usb_driver(redrat3_dev_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_AUTHOR(DRIVER_AUTHOR2);
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(usb, redrat3_dev_table);
