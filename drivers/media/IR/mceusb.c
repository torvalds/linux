/*
 * Driver for USB Windows Media Center Ed. eHome Infrared Transceivers
 *
 * Copyright (c) 2010 by Jarod Wilson <jarod@redhat.com>
 *
 * Based on the original lirc_mceusb and lirc_mceusb2 drivers, by Dan
 * Conti, Martin Blatter and Daniel Melander, the latter of which was
 * in turn also based on the lirc_atiusb driver by Paul Miller. The
 * two mce drivers were merged into one by Jarod Wilson, with transmit
 * support for the 1st-gen device added primarily by Patrick Calhoun,
 * with a bit of tweaks by Jarod. Debugging improvements and proper
 * support for what appears to be 3rd-gen hardware added by Jarod.
 * Initial port from lirc driver to ir-core drivery by Jarod, based
 * partially on a port to an earlier proposed IR infrastructure by
 * Jon Smirl, which included enhancements and simplifications to the
 * incoming IR buffer parsing routines.
 *
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

#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/input.h>
#include <media/ir-core.h>
#include <media/ir-common.h>

#define DRIVER_VERSION	"1.91"
#define DRIVER_AUTHOR	"Jarod Wilson <jarod@wilsonet.com>"
#define DRIVER_DESC	"Windows Media Center Ed. eHome Infrared Transceiver " \
			"device driver"
#define DRIVER_NAME	"mceusb"

#define USB_BUFLEN	32	/* USB reception buffer length */
#define USB_CTRL_MSG_SZ	2	/* Size of usb ctrl msg on gen1 hw */
#define MCE_G1_INIT_MSGS 40	/* Init messages on gen1 hw to throw out */

/* MCE constants */
#define MCE_CMDBUF_SIZE	384 /* MCE Command buffer length */
#define MCE_TIME_UNIT	50 /* Approx 50us resolution */
#define MCE_CODE_LENGTH	5 /* Normal length of packet (with header) */
#define MCE_PACKET_SIZE	4 /* Normal length of packet (without header) */
#define MCE_PACKET_HEADER 0x84 /* Actual header format is 0x80 + num_bytes */
#define MCE_CONTROL_HEADER 0x9F /* MCE status header */
#define MCE_TX_HEADER_LENGTH 3 /* # of bytes in the initializing tx header */
#define MCE_MAX_CHANNELS 2 /* Two transmitters, hardware dependent? */
#define MCE_DEFAULT_TX_MASK 0x03 /* Val opts: TX1=0x01, TX2=0x02, ALL=0x03 */
#define MCE_PULSE_BIT	0x80 /* Pulse bit, MSB set == PULSE else SPACE */
#define MCE_PULSE_MASK	0x7F /* Pulse mask */
#define MCE_MAX_PULSE_LENGTH 0x7F /* Longest transmittable pulse symbol */
#define MCE_PACKET_LENGTH_MASK  0x1F /* Packet length mask */


/* module parameters */
#ifdef CONFIG_USB_DEBUG
static int debug = 1;
#else
static int debug;
#endif

/* general constants */
#define SEND_FLAG_IN_PROGRESS	1
#define SEND_FLAG_COMPLETE	2
#define RECV_FLAG_IN_PROGRESS	3
#define RECV_FLAG_COMPLETE	4

#define MCEUSB_RX		1
#define MCEUSB_TX		2

#define VENDOR_PHILIPS		0x0471
#define VENDOR_SMK		0x0609
#define VENDOR_TATUNG		0x1460
#define VENDOR_GATEWAY		0x107b
#define VENDOR_SHUTTLE		0x1308
#define VENDOR_SHUTTLE2		0x051c
#define VENDOR_MITSUMI		0x03ee
#define VENDOR_TOPSEED		0x1784
#define VENDOR_RICAVISION	0x179d
#define VENDOR_ITRON		0x195d
#define VENDOR_FIC		0x1509
#define VENDOR_LG		0x043e
#define VENDOR_MICROSOFT	0x045e
#define VENDOR_FORMOSA		0x147a
#define VENDOR_FINTEK		0x1934
#define VENDOR_PINNACLE		0x2304
#define VENDOR_ECS		0x1019
#define VENDOR_WISTRON		0x0fb8
#define VENDOR_COMPRO		0x185b
#define VENDOR_NORTHSTAR	0x04eb
#define VENDOR_REALTEK		0x0bda
#define VENDOR_TIVO		0x105a

static struct usb_device_id mceusb_dev_table[] = {
	/* Original Microsoft MCE IR Transceiver (often HP-branded) */
	{ USB_DEVICE(VENDOR_MICROSOFT, 0x006d) },
	/* Philips Infrared Transceiver - Sahara branded */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x0608) },
	/* Philips Infrared Transceiver - HP branded */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x060c) },
	/* Philips SRM5100 */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x060d) },
	/* Philips Infrared Transceiver - Omaura */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x060f) },
	/* Philips Infrared Transceiver - Spinel plus */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x0613) },
	/* Philips eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x0815) },
	/* Philips/Spinel plus IR transceiver for ASUS */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x206c) },
	/* Philips/Spinel plus IR transceiver for ASUS */
	{ USB_DEVICE(VENDOR_PHILIPS, 0x2088) },
	/* Realtek MCE IR Receiver */
	{ USB_DEVICE(VENDOR_REALTEK, 0x0161) },
	/* SMK/Toshiba G83C0004D410 */
	{ USB_DEVICE(VENDOR_SMK, 0x031d) },
	/* SMK eHome Infrared Transceiver (Sony VAIO) */
	{ USB_DEVICE(VENDOR_SMK, 0x0322) },
	/* bundled with Hauppauge PVR-150 */
	{ USB_DEVICE(VENDOR_SMK, 0x0334) },
	/* SMK eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_SMK, 0x0338) },
	/* Tatung eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_TATUNG, 0x9150) },
	/* Shuttle eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_SHUTTLE, 0xc001) },
	/* Shuttle eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_SHUTTLE2, 0xc001) },
	/* Gateway eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_GATEWAY, 0x3009) },
	/* Mitsumi */
	{ USB_DEVICE(VENDOR_MITSUMI, 0x2501) },
	/* Topseed eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_TOPSEED, 0x0001) },
	/* Topseed HP eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_TOPSEED, 0x0006) },
	/* Topseed eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_TOPSEED, 0x0007) },
	/* Topseed eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_TOPSEED, 0x0008) },
	/* Topseed eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_TOPSEED, 0x000a) },
	/* Topseed eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_TOPSEED, 0x0011) },
	/* Ricavision internal Infrared Transceiver */
	{ USB_DEVICE(VENDOR_RICAVISION, 0x0010) },
	/* Itron ione Libra Q-11 */
	{ USB_DEVICE(VENDOR_ITRON, 0x7002) },
	/* FIC eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_FIC, 0x9242) },
	/* LG eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_LG, 0x9803) },
	/* Microsoft MCE Infrared Transceiver */
	{ USB_DEVICE(VENDOR_MICROSOFT, 0x00a0) },
	/* Formosa eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_FORMOSA, 0xe015) },
	/* Formosa21 / eHome Infrared Receiver */
	{ USB_DEVICE(VENDOR_FORMOSA, 0xe016) },
	/* Formosa aim / Trust MCE Infrared Receiver */
	{ USB_DEVICE(VENDOR_FORMOSA, 0xe017) },
	/* Formosa Industrial Computing / Beanbag Emulation Device */
	{ USB_DEVICE(VENDOR_FORMOSA, 0xe018) },
	/* Formosa21 / eHome Infrared Receiver */
	{ USB_DEVICE(VENDOR_FORMOSA, 0xe03a) },
	/* Formosa Industrial Computing AIM IR605/A */
	{ USB_DEVICE(VENDOR_FORMOSA, 0xe03c) },
	/* Formosa Industrial Computing */
	{ USB_DEVICE(VENDOR_FORMOSA, 0xe03e) },
	/* Fintek eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_FINTEK, 0x0602) },
	/* Fintek eHome Infrared Transceiver (in the AOpen MP45) */
	{ USB_DEVICE(VENDOR_FINTEK, 0x0702) },
	/* Pinnacle Remote Kit */
	{ USB_DEVICE(VENDOR_PINNACLE, 0x0225) },
	/* Elitegroup Computer Systems IR */
	{ USB_DEVICE(VENDOR_ECS, 0x0f38) },
	/* Wistron Corp. eHome Infrared Receiver */
	{ USB_DEVICE(VENDOR_WISTRON, 0x0002) },
	/* Compro K100 */
	{ USB_DEVICE(VENDOR_COMPRO, 0x3020) },
	/* Compro K100 v2 */
	{ USB_DEVICE(VENDOR_COMPRO, 0x3082) },
	/* Northstar Systems, Inc. eHome Infrared Transceiver */
	{ USB_DEVICE(VENDOR_NORTHSTAR, 0xe004) },
	/* TiVo PC IR Receiver */
	{ USB_DEVICE(VENDOR_TIVO, 0x2000) },
	/* Terminating entry */
	{ }
};

static struct usb_device_id gen3_list[] = {
	{ USB_DEVICE(VENDOR_PINNACLE, 0x0225) },
	{ USB_DEVICE(VENDOR_TOPSEED, 0x0008) },
	{}
};

static struct usb_device_id microsoft_gen1_list[] = {
	{ USB_DEVICE(VENDOR_MICROSOFT, 0x006d) },
	{}
};

static struct usb_device_id std_tx_mask_list[] = {
	{ USB_DEVICE(VENDOR_MICROSOFT, 0x006d) },
	{ USB_DEVICE(VENDOR_PHILIPS, 0x060c) },
	{ USB_DEVICE(VENDOR_SMK, 0x031d) },
	{ USB_DEVICE(VENDOR_SMK, 0x0322) },
	{ USB_DEVICE(VENDOR_SMK, 0x0334) },
	{ USB_DEVICE(VENDOR_TOPSEED, 0x0001) },
	{ USB_DEVICE(VENDOR_TOPSEED, 0x0006) },
	{ USB_DEVICE(VENDOR_TOPSEED, 0x0007) },
	{ USB_DEVICE(VENDOR_TOPSEED, 0x0008) },
	{ USB_DEVICE(VENDOR_TOPSEED, 0x000a) },
	{ USB_DEVICE(VENDOR_TOPSEED, 0x0011) },
	{ USB_DEVICE(VENDOR_PINNACLE, 0x0225) },
	{}
};

/* data structure for each usb transceiver */
struct mceusb_dev {
	/* ir-core bits */
	struct ir_dev_props *props;
	struct ir_raw_event rawir;

	/* core device bits */
	struct device *dev;
	struct input_dev *idev;

	/* usb */
	struct usb_device *usbdev;
	struct urb *urb_in;
	struct usb_endpoint_descriptor *usb_ep_in;
	struct usb_endpoint_descriptor *usb_ep_out;

	/* buffers and dma */
	unsigned char *buf_in;
	unsigned int len_in;
	u8 cmd;		/* MCE command type */
	u8 rem;		/* Remaining IR data bytes in packet */
	dma_addr_t dma_in;
	dma_addr_t dma_out;

	struct {
		u32 connected:1;
		u32 tx_mask_inverted:1;
		u32 microsoft_gen1:1;
		u32 reserved:29;
	} flags;

	/* transmit support */
	int send_flags;
	u32 carrier;
	unsigned char tx_mask;

	char name[128];
	char phys[64];
};

/*
 * MCE Device Command Strings
 * Device command responses vary from device to device...
 * - DEVICE_RESET resets the hardware to its default state
 * - GET_REVISION fetches the hardware/software revision, common
 *   replies are ff 0b 45 ff 1b 08 and ff 0b 50 ff 1b 42
 * - GET_CARRIER_FREQ gets the carrier mode and frequency of the
 *   device, with replies in the form of 9f 06 MM FF, where MM is 0-3,
 *   meaning clk of 10000000, 2500000, 625000 or 156250, and FF is
 *   ((clk / frequency) - 1)
 * - GET_RX_TIMEOUT fetches the receiver timeout in units of 50us,
 *   response in the form of 9f 0c msb lsb
 * - GET_TX_BITMASK fetches the transmitter bitmask, replies in
 *   the form of 9f 08 bm, where bm is the bitmask
 * - GET_RX_SENSOR fetches the RX sensor setting -- long-range
 *   general use one or short-range learning one, in the form of
 *   9f 14 ss, where ss is either 01 for long-range or 02 for short
 * - SET_CARRIER_FREQ sets a new carrier mode and frequency
 * - SET_TX_BITMASK sets the transmitter bitmask
 * - SET_RX_TIMEOUT sets the receiver timeout
 * - SET_RX_SENSOR sets which receiver sensor to use
 */
static char DEVICE_RESET[]	= {0x00, 0xff, 0xaa};
static char GET_REVISION[]	= {0xff, 0x0b};
static char GET_UNKNOWN[]	= {0xff, 0x18};
static char GET_UNKNOWN2[]	= {0x9f, 0x05};
static char GET_CARRIER_FREQ[]	= {0x9f, 0x07};
static char GET_RX_TIMEOUT[]	= {0x9f, 0x0d};
static char GET_TX_BITMASK[]	= {0x9f, 0x13};
static char GET_RX_SENSOR[]	= {0x9f, 0x15};
/* sub in desired values in lower byte or bytes for full command */
/* FIXME: make use of these for transmit.
static char SET_CARRIER_FREQ[]	= {0x9f, 0x06, 0x00, 0x00};
static char SET_TX_BITMASK[]	= {0x9f, 0x08, 0x00};
static char SET_RX_TIMEOUT[]	= {0x9f, 0x0c, 0x00, 0x00};
static char SET_RX_SENSOR[]	= {0x9f, 0x14, 0x00};
*/

static void mceusb_dev_printdata(struct mceusb_dev *ir, char *buf,
				 int len, bool out)
{
	char codes[USB_BUFLEN * 3 + 1];
	char inout[9];
	int i;
	u8 cmd, subcmd, data1, data2;
	struct device *dev = ir->dev;
	int idx = 0;

	/* skip meaningless 0xb1 0x60 header bytes on orig receiver */
	if (ir->flags.microsoft_gen1 && !out)
		idx = 2;

	if (len <= idx)
		return;

	for (i = 0; i < len && i < USB_BUFLEN; i++)
		snprintf(codes + i * 3, 4, "%02x ", buf[i] & 0xFF);

	dev_info(dev, "%sx data: %s (length=%d)\n",
		 (out ? "t" : "r"), codes, len);

	if (out)
		strcpy(inout, "Request\0");
	else
		strcpy(inout, "Got\0");

	cmd    = buf[idx] & 0xff;
	subcmd = buf[idx + 1] & 0xff;
	data1  = buf[idx + 2] & 0xff;
	data2  = buf[idx + 3] & 0xff;

	switch (cmd) {
	case 0x00:
		if (subcmd == 0xff && data1 == 0xaa)
			dev_info(dev, "Device reset requested\n");
		else
			dev_info(dev, "Unknown command 0x%02x 0x%02x\n",
				 cmd, subcmd);
		break;
	case 0xff:
		switch (subcmd) {
		case 0x0b:
			if (len == 2)
				dev_info(dev, "Get hw/sw rev?\n");
			else
				dev_info(dev, "hw/sw rev 0x%02x 0x%02x "
					 "0x%02x 0x%02x\n", data1, data2,
					 buf[idx + 4], buf[idx + 5]);
			break;
		case 0xaa:
			dev_info(dev, "Device reset requested\n");
			break;
		case 0xfe:
			dev_info(dev, "Previous command not supported\n");
			break;
		case 0x18:
		case 0x1b:
		default:
			dev_info(dev, "Unknown command 0x%02x 0x%02x\n",
				 cmd, subcmd);
			break;
		}
		break;
	case 0x9f:
		switch (subcmd) {
		case 0x03:
			dev_info(dev, "Ping\n");
			break;
		case 0x04:
			dev_info(dev, "Resp to 9f 05 of 0x%02x 0x%02x\n",
				 data1, data2);
			break;
		case 0x06:
			dev_info(dev, "%s carrier mode and freq of "
				 "0x%02x 0x%02x\n", inout, data1, data2);
			break;
		case 0x07:
			dev_info(dev, "Get carrier mode and freq\n");
			break;
		case 0x08:
			dev_info(dev, "%s transmit blaster mask of 0x%02x\n",
				 inout, data1);
			break;
		case 0x0c:
			/* value is in units of 50us, so x*50/100 or x/2 ms */
			dev_info(dev, "%s receive timeout of %d ms\n",
				 inout, ((data1 << 8) | data2) / 2);
			break;
		case 0x0d:
			dev_info(dev, "Get receive timeout\n");
			break;
		case 0x13:
			dev_info(dev, "Get transmit blaster mask\n");
			break;
		case 0x14:
			dev_info(dev, "%s %s-range receive sensor in use\n",
				 inout, data1 == 0x02 ? "short" : "long");
			break;
		case 0x15:
			if (len == 2)
				dev_info(dev, "Get receive sensor\n");
			else
				dev_info(dev, "Received pulse count is %d\n",
					 ((data1 << 8) | data2));
			break;
		case 0xfe:
			dev_info(dev, "Error! Hardware is likely wedged...\n");
			break;
		case 0x05:
		case 0x09:
		case 0x0f:
		default:
			dev_info(dev, "Unknown command 0x%02x 0x%02x\n",
				 cmd, subcmd);
			break;
		}
		break;
	default:
		break;
	}
}

static void mce_async_callback(struct urb *urb, struct pt_regs *regs)
{
	struct mceusb_dev *ir;
	int len;

	if (!urb)
		return;

	ir = urb->context;
	if (ir) {
		len = urb->actual_length;

		dev_dbg(ir->dev, "callback called (status=%d len=%d)\n",
			urb->status, len);

		if (debug)
			mceusb_dev_printdata(ir, urb->transfer_buffer,
					     len, true);
	}

}

/* request incoming or send outgoing usb packet - used to initialize remote */
static void mce_request_packet(struct mceusb_dev *ir,
			       struct usb_endpoint_descriptor *ep,
			       unsigned char *data, int size, int urb_type)
{
	int res;
	struct urb *async_urb;
	struct device *dev = ir->dev;
	unsigned char *async_buf;

	if (urb_type == MCEUSB_TX) {
		async_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (unlikely(!async_urb)) {
			dev_err(dev, "Error, couldn't allocate urb!\n");
			return;
		}

		async_buf = kzalloc(size, GFP_KERNEL);
		if (!async_buf) {
			dev_err(dev, "Error, couldn't allocate buf!\n");
			usb_free_urb(async_urb);
			return;
		}

		/* outbound data */
		usb_fill_int_urb(async_urb, ir->usbdev,
			usb_sndintpipe(ir->usbdev, ep->bEndpointAddress),
			async_buf, size, (usb_complete_t)mce_async_callback,
			ir, ep->bInterval);
		memcpy(async_buf, data, size);

	} else if (urb_type == MCEUSB_RX) {
		/* standard request */
		async_urb = ir->urb_in;
		ir->send_flags = RECV_FLAG_IN_PROGRESS;

	} else {
		dev_err(dev, "Error! Unknown urb type %d\n", urb_type);
		return;
	}

	dev_dbg(dev, "receive request called (size=%#x)\n", size);

	async_urb->transfer_buffer_length = size;
	async_urb->dev = ir->usbdev;

	res = usb_submit_urb(async_urb, GFP_ATOMIC);
	if (res) {
		dev_dbg(dev, "receive request FAILED! (res=%d)\n", res);
		return;
	}
	dev_dbg(dev, "receive request complete (res=%d)\n", res);
}

static void mce_async_out(struct mceusb_dev *ir, unsigned char *data, int size)
{
	mce_request_packet(ir, ir->usb_ep_out, data, size, MCEUSB_TX);
}

static void mce_sync_in(struct mceusb_dev *ir, unsigned char *data, int size)
{
	mce_request_packet(ir, ir->usb_ep_in, data, size, MCEUSB_RX);
}

/* Send data out the IR blaster port(s) */
static int mceusb_tx_ir(void *priv, int *txbuf, u32 n)
{
	struct mceusb_dev *ir = priv;
	int i, ret = 0;
	int count, cmdcount = 0;
	unsigned char *cmdbuf; /* MCE command buffer */
	long signal_duration = 0; /* Singnal length in us */
	struct timeval start_time, end_time;

	do_gettimeofday(&start_time);

	count = n / sizeof(int);

	cmdbuf = kzalloc(sizeof(int) * MCE_CMDBUF_SIZE, GFP_KERNEL);
	if (!cmdbuf)
		return -ENOMEM;

	/* MCE tx init header */
	cmdbuf[cmdcount++] = MCE_CONTROL_HEADER;
	cmdbuf[cmdcount++] = 0x08;
	cmdbuf[cmdcount++] = ir->tx_mask;

	/* Generate mce packet data */
	for (i = 0; (i < count) && (cmdcount < MCE_CMDBUF_SIZE); i++) {
		signal_duration += txbuf[i];
		txbuf[i] = txbuf[i] / MCE_TIME_UNIT;

		do { /* loop to support long pulses/spaces > 127*50us=6.35ms */

			/* Insert mce packet header every 4th entry */
			if ((cmdcount < MCE_CMDBUF_SIZE) &&
			    (cmdcount - MCE_TX_HEADER_LENGTH) %
			     MCE_CODE_LENGTH == 0)
				cmdbuf[cmdcount++] = MCE_PACKET_HEADER;

			/* Insert mce packet data */
			if (cmdcount < MCE_CMDBUF_SIZE)
				cmdbuf[cmdcount++] =
					(txbuf[i] < MCE_PULSE_BIT ?
					 txbuf[i] : MCE_MAX_PULSE_LENGTH) |
					 (i & 1 ? 0x00 : MCE_PULSE_BIT);
			else {
				ret = -EINVAL;
				goto out;
			}

		} while ((txbuf[i] > MCE_MAX_PULSE_LENGTH) &&
			 (txbuf[i] -= MCE_MAX_PULSE_LENGTH));
	}

	/* Fix packet length in last header */
	cmdbuf[cmdcount - (cmdcount - MCE_TX_HEADER_LENGTH) % MCE_CODE_LENGTH] =
		0x80 + (cmdcount - MCE_TX_HEADER_LENGTH) % MCE_CODE_LENGTH - 1;

	/* Check if we have room for the empty packet at the end */
	if (cmdcount >= MCE_CMDBUF_SIZE) {
		ret = -EINVAL;
		goto out;
	}

	/* All mce commands end with an empty packet (0x80) */
	cmdbuf[cmdcount++] = 0x80;

	/* Transmit the command to the mce device */
	mce_async_out(ir, cmdbuf, cmdcount);

	/*
	 * The lircd gap calculation expects the write function to
	 * wait the time it takes for the ircommand to be sent before
	 * it returns.
	 */
	do_gettimeofday(&end_time);
	signal_duration -= (end_time.tv_usec - start_time.tv_usec) +
			   (end_time.tv_sec - start_time.tv_sec) * 1000000;

	/* delay with the closest number of ticks */
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(usecs_to_jiffies(signal_duration));

out:
	kfree(cmdbuf);
	return ret ? ret : n;
}

/* Sets active IR outputs -- mce devices typically (all?) have two */
static int mceusb_set_tx_mask(void *priv, u32 mask)
{
	struct mceusb_dev *ir = priv;

	if (ir->flags.tx_mask_inverted)
		ir->tx_mask = (mask != 0x03 ? mask ^ 0x03 : mask) << 1;
	else
		ir->tx_mask = mask;

	return 0;
}

/* Sets the send carrier frequency and mode */
static int mceusb_set_tx_carrier(void *priv, u32 carrier)
{
	struct mceusb_dev *ir = priv;
	int clk = 10000000;
	int prescaler = 0, divisor = 0;
	unsigned char cmdbuf[4] = { 0x9f, 0x06, 0x00, 0x00 };

	/* Carrier has changed */
	if (ir->carrier != carrier) {

		if (carrier == 0) {
			ir->carrier = carrier;
			cmdbuf[2] = 0x01;
			cmdbuf[3] = 0x80;
			dev_dbg(ir->dev, "%s: disabling carrier "
				"modulation\n", __func__);
			mce_async_out(ir, cmdbuf, sizeof(cmdbuf));
			return carrier;
		}

		for (prescaler = 0; prescaler < 4; ++prescaler) {
			divisor = (clk >> (2 * prescaler)) / carrier;
			if (divisor <= 0xFF) {
				ir->carrier = carrier;
				cmdbuf[2] = prescaler;
				cmdbuf[3] = divisor;
				dev_dbg(ir->dev, "%s: requesting %u HZ "
					"carrier\n", __func__, carrier);

				/* Transmit new carrier to mce device */
				mce_async_out(ir, cmdbuf, sizeof(cmdbuf));
				return carrier;
			}
		}

		return -EINVAL;

	}

	return carrier;
}

static void mceusb_process_ir_data(struct mceusb_dev *ir, int buf_len)
{
	struct ir_raw_event rawir = { .pulse = false, .duration = 0 };
	int i, start_index = 0;
	u8 hdr = MCE_CONTROL_HEADER;

	/* skip meaningless 0xb1 0x60 header bytes on orig receiver */
	if (ir->flags.microsoft_gen1)
		start_index = 2;

	for (i = start_index; i < buf_len;) {
		if (ir->rem == 0) {
			/* decode mce packets of the form (84),AA,BB,CC,DD */
			/* IR data packets can span USB messages - rem */
			hdr = ir->buf_in[i];
			ir->rem = (hdr & MCE_PACKET_LENGTH_MASK);
			ir->cmd = (hdr & ~MCE_PACKET_LENGTH_MASK);
			dev_dbg(ir->dev, "New data. rem: 0x%02x, cmd: 0x%02x\n",
				ir->rem, ir->cmd);
			i++;
		}

		/* don't process MCE commands */
		if (hdr == MCE_CONTROL_HEADER || hdr == 0xff) {
			ir->rem = 0;
			return;
		}

		for (; (ir->rem > 0) && (i < buf_len); i++) {
			ir->rem--;

			rawir.pulse = ((ir->buf_in[i] & MCE_PULSE_BIT) != 0);
			rawir.duration = (ir->buf_in[i] & MCE_PULSE_MASK)
					 * MCE_TIME_UNIT * 1000;

			if ((ir->buf_in[i] & MCE_PULSE_MASK) == 0x7f) {
				if (ir->rawir.pulse == rawir.pulse)
					ir->rawir.duration += rawir.duration;
				else {
					ir->rawir.duration = rawir.duration;
					ir->rawir.pulse = rawir.pulse;
				}
				continue;
			}
			rawir.duration += ir->rawir.duration;
			ir->rawir.duration = 0;
			ir->rawir.pulse = rawir.pulse;

			dev_dbg(ir->dev, "Storing %s with duration %d\n",
				rawir.pulse ? "pulse" : "space",
				rawir.duration);

			ir_raw_event_store(ir->idev, &rawir);
		}

		if (ir->buf_in[i] == 0x80 || ir->buf_in[i] == 0x9f)
			ir->rem = 0;

		dev_dbg(ir->dev, "calling ir_raw_event_handle\n");
		ir_raw_event_handle(ir->idev);
	}
}

static void mceusb_dev_recv(struct urb *urb, struct pt_regs *regs)
{
	struct mceusb_dev *ir;
	int buf_len;

	if (!urb)
		return;

	ir = urb->context;
	if (!ir) {
		usb_unlink_urb(urb);
		return;
	}

	buf_len = urb->actual_length;

	if (debug)
		mceusb_dev_printdata(ir, urb->transfer_buffer, buf_len, false);

	if (ir->send_flags == RECV_FLAG_IN_PROGRESS) {
		ir->send_flags = SEND_FLAG_COMPLETE;
		dev_dbg(ir->dev, "setup answer received %d bytes\n",
			buf_len);
	}

	switch (urb->status) {
	/* success */
	case 0:
		mceusb_process_ir_data(ir, buf_len);
		break;

	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		usb_unlink_urb(urb);
		return;

	case -EPIPE:
	default:
		break;
	}

	usb_submit_urb(urb, GFP_ATOMIC);
}

static void mceusb_gen1_init(struct mceusb_dev *ir)
{
	int ret;
	int maxp = ir->len_in;
	struct device *dev = ir->dev;
	char *data;

	data = kzalloc(USB_CTRL_MSG_SZ, GFP_KERNEL);
	if (!data) {
		dev_err(dev, "%s: memory allocation failed!\n", __func__);
		return;
	}

	/*
	 * This is a strange one. Windows issues a set address to the device
	 * on the receive control pipe and expect a certain value pair back
	 */
	ret = usb_control_msg(ir->usbdev, usb_rcvctrlpipe(ir->usbdev, 0),
			      USB_REQ_SET_ADDRESS, USB_TYPE_VENDOR, 0, 0,
			      data, USB_CTRL_MSG_SZ, HZ * 3);
	dev_dbg(dev, "%s - ret = %d\n", __func__, ret);
	dev_dbg(dev, "%s - data[0] = %d, data[1] = %d\n",
		__func__, data[0], data[1]);

	/* set feature: bit rate 38400 bps */
	ret = usb_control_msg(ir->usbdev, usb_sndctrlpipe(ir->usbdev, 0),
			      USB_REQ_SET_FEATURE, USB_TYPE_VENDOR,
			      0xc04e, 0x0000, NULL, 0, HZ * 3);

	dev_dbg(dev, "%s - ret = %d\n", __func__, ret);

	/* bRequest 4: set char length to 8 bits */
	ret = usb_control_msg(ir->usbdev, usb_sndctrlpipe(ir->usbdev, 0),
			      4, USB_TYPE_VENDOR,
			      0x0808, 0x0000, NULL, 0, HZ * 3);
	dev_dbg(dev, "%s - retB = %d\n", __func__, ret);

	/* bRequest 2: set handshaking to use DTR/DSR */
	ret = usb_control_msg(ir->usbdev, usb_sndctrlpipe(ir->usbdev, 0),
			      2, USB_TYPE_VENDOR,
			      0x0000, 0x0100, NULL, 0, HZ * 3);
	dev_dbg(dev, "%s - retC = %d\n", __func__, ret);

	/* device reset */
	mce_async_out(ir, DEVICE_RESET, sizeof(DEVICE_RESET));
	mce_sync_in(ir, NULL, maxp);

	/* get hw/sw revision? */
	mce_async_out(ir, GET_REVISION, sizeof(GET_REVISION));
	mce_sync_in(ir, NULL, maxp);

	kfree(data);
};

static void mceusb_gen2_init(struct mceusb_dev *ir)
{
	int maxp = ir->len_in;

	/* device reset */
	mce_async_out(ir, DEVICE_RESET, sizeof(DEVICE_RESET));
	mce_sync_in(ir, NULL, maxp);

	/* get hw/sw revision? */
	mce_async_out(ir, GET_REVISION, sizeof(GET_REVISION));
	mce_sync_in(ir, NULL, maxp);

	/* unknown what the next two actually return... */
	mce_async_out(ir, GET_UNKNOWN, sizeof(GET_UNKNOWN));
	mce_sync_in(ir, NULL, maxp);
	mce_async_out(ir, GET_UNKNOWN2, sizeof(GET_UNKNOWN2));
	mce_sync_in(ir, NULL, maxp);
}

static void mceusb_get_parameters(struct mceusb_dev *ir)
{
	int maxp = ir->len_in;

	/* get the carrier and frequency */
	mce_async_out(ir, GET_CARRIER_FREQ, sizeof(GET_CARRIER_FREQ));
	mce_sync_in(ir, NULL, maxp);

	/* get the transmitter bitmask */
	mce_async_out(ir, GET_TX_BITMASK, sizeof(GET_TX_BITMASK));
	mce_sync_in(ir, NULL, maxp);

	/* get receiver timeout value */
	mce_async_out(ir, GET_RX_TIMEOUT, sizeof(GET_RX_TIMEOUT));
	mce_sync_in(ir, NULL, maxp);

	/* get receiver sensor setting */
	mce_async_out(ir, GET_RX_SENSOR, sizeof(GET_RX_SENSOR));
	mce_sync_in(ir, NULL, maxp);
}

static struct input_dev *mceusb_init_input_dev(struct mceusb_dev *ir)
{
	struct input_dev *idev;
	struct ir_dev_props *props;
	struct device *dev = ir->dev;
	int ret = -ENODEV;

	idev = input_allocate_device();
	if (!idev) {
		dev_err(dev, "remote input dev allocation failed\n");
		goto idev_alloc_failed;
	}

	ret = -ENOMEM;
	props = kzalloc(sizeof(struct ir_dev_props), GFP_KERNEL);
	if (!props) {
		dev_err(dev, "remote ir dev props allocation failed\n");
		goto props_alloc_failed;
	}

	snprintf(ir->name, sizeof(ir->name), "Media Center Ed. eHome "
		 "Infrared Remote Transceiver (%04x:%04x)",
		 le16_to_cpu(ir->usbdev->descriptor.idVendor),
		 le16_to_cpu(ir->usbdev->descriptor.idProduct));

	idev->name = ir->name;
	usb_make_path(ir->usbdev, ir->phys, sizeof(ir->phys));
	strlcat(ir->phys, "/input0", sizeof(ir->phys));
	idev->phys = ir->phys;

	props->priv = ir;
	props->driver_type = RC_DRIVER_IR_RAW;
	props->allowed_protos = IR_TYPE_ALL;
	props->s_tx_mask = mceusb_set_tx_mask;
	props->s_tx_carrier = mceusb_set_tx_carrier;
	props->tx_ir = mceusb_tx_ir;

	ir->props = props;

	ret = ir_input_register(idev, RC_MAP_RC6_MCE, props, DRIVER_NAME);
	if (ret < 0) {
		dev_err(dev, "remote input device register failed\n");
		goto irdev_failed;
	}

	return idev;

irdev_failed:
	kfree(props);
props_alloc_failed:
	input_free_device(idev);
idev_alloc_failed:
	return NULL;
}

static int __devinit mceusb_dev_probe(struct usb_interface *intf,
				      const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *idesc;
	struct usb_endpoint_descriptor *ep = NULL;
	struct usb_endpoint_descriptor *ep_in = NULL;
	struct usb_endpoint_descriptor *ep_out = NULL;
	struct mceusb_dev *ir = NULL;
	int pipe, maxp, i;
	char buf[63], name[128] = "";
	bool is_gen3;
	bool is_microsoft_gen1;
	bool tx_mask_inverted;

	dev_dbg(&intf->dev, ": %s called\n", __func__);

	idesc  = intf->cur_altsetting;

	is_gen3 = usb_match_id(intf, gen3_list) ? 1 : 0;
	is_microsoft_gen1 = usb_match_id(intf, microsoft_gen1_list) ? 1 : 0;
	tx_mask_inverted = usb_match_id(intf, std_tx_mask_list) ? 0 : 1;

	/* step through the endpoints to find first bulk in and out endpoint */
	for (i = 0; i < idesc->desc.bNumEndpoints; ++i) {
		ep = &idesc->endpoint[i].desc;

		if ((ep_in == NULL)
			&& ((ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
			    == USB_DIR_IN)
			&& (((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
			    == USB_ENDPOINT_XFER_BULK)
			|| ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
			    == USB_ENDPOINT_XFER_INT))) {

			ep_in = ep;
			ep_in->bmAttributes = USB_ENDPOINT_XFER_INT;
			ep_in->bInterval = 1;
			dev_dbg(&intf->dev, ": acceptable inbound endpoint "
				"found\n");
		}

		if ((ep_out == NULL)
			&& ((ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
			    == USB_DIR_OUT)
			&& (((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
			    == USB_ENDPOINT_XFER_BULK)
			|| ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
			    == USB_ENDPOINT_XFER_INT))) {

			ep_out = ep;
			ep_out->bmAttributes = USB_ENDPOINT_XFER_INT;
			ep_out->bInterval = 1;
			dev_dbg(&intf->dev, ": acceptable outbound endpoint "
				"found\n");
		}
	}
	if (ep_in == NULL) {
		dev_dbg(&intf->dev, ": inbound and/or endpoint not found\n");
		return -ENODEV;
	}

	pipe = usb_rcvintpipe(dev, ep_in->bEndpointAddress);
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

	ir = kzalloc(sizeof(struct mceusb_dev), GFP_KERNEL);
	if (!ir)
		goto mem_alloc_fail;

	ir->buf_in = usb_alloc_coherent(dev, maxp, GFP_ATOMIC, &ir->dma_in);
	if (!ir->buf_in)
		goto buf_in_alloc_fail;

	ir->urb_in = usb_alloc_urb(0, GFP_KERNEL);
	if (!ir->urb_in)
		goto urb_in_alloc_fail;

	ir->usbdev = dev;
	ir->dev = &intf->dev;
	ir->len_in = maxp;
	ir->flags.microsoft_gen1 = is_microsoft_gen1;
	ir->flags.tx_mask_inverted = tx_mask_inverted;

	/* Saving usb interface data for use by the transmitter routine */
	ir->usb_ep_in = ep_in;
	ir->usb_ep_out = ep_out;

	if (dev->descriptor.iManufacturer
	    && usb_string(dev, dev->descriptor.iManufacturer,
			  buf, sizeof(buf)) > 0)
		strlcpy(name, buf, sizeof(name));
	if (dev->descriptor.iProduct
	    && usb_string(dev, dev->descriptor.iProduct,
			  buf, sizeof(buf)) > 0)
		snprintf(name + strlen(name), sizeof(name) - strlen(name),
			 " %s", buf);

	ir->idev = mceusb_init_input_dev(ir);
	if (!ir->idev)
		goto input_dev_fail;

	/* flush buffers on the device */
	mce_sync_in(ir, NULL, maxp);
	mce_sync_in(ir, NULL, maxp);

	/* wire up inbound data handler */
	usb_fill_int_urb(ir->urb_in, dev, pipe, ir->buf_in,
		maxp, (usb_complete_t) mceusb_dev_recv, ir, ep_in->bInterval);
	ir->urb_in->transfer_dma = ir->dma_in;
	ir->urb_in->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* initialize device */
	if (ir->flags.microsoft_gen1)
		mceusb_gen1_init(ir);
	else if (!is_gen3)
		mceusb_gen2_init(ir);

	mceusb_get_parameters(ir);

	mceusb_set_tx_mask(ir, MCE_DEFAULT_TX_MASK);

	usb_set_intfdata(intf, ir);

	dev_info(&intf->dev, "Registered %s on usb%d:%d\n", name,
		 dev->bus->busnum, dev->devnum);

	return 0;

	/* Error-handling path */
input_dev_fail:
	usb_free_urb(ir->urb_in);
urb_in_alloc_fail:
	usb_free_coherent(dev, maxp, ir->buf_in, ir->dma_in);
buf_in_alloc_fail:
	kfree(ir);
mem_alloc_fail:
	dev_err(&intf->dev, "%s: device setup failed!\n", __func__);

	return -ENOMEM;
}


static void __devexit mceusb_dev_disconnect(struct usb_interface *intf)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct mceusb_dev *ir = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	if (!ir)
		return;

	ir->usbdev = NULL;
	ir_input_unregister(ir->idev);
	usb_kill_urb(ir->urb_in);
	usb_free_urb(ir->urb_in);
	usb_free_coherent(dev, ir->len_in, ir->buf_in, ir->dma_in);

	kfree(ir);
}

static int mceusb_dev_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct mceusb_dev *ir = usb_get_intfdata(intf);
	dev_info(ir->dev, "suspend\n");
	usb_kill_urb(ir->urb_in);
	return 0;
}

static int mceusb_dev_resume(struct usb_interface *intf)
{
	struct mceusb_dev *ir = usb_get_intfdata(intf);
	dev_info(ir->dev, "resume\n");
	if (usb_submit_urb(ir->urb_in, GFP_ATOMIC))
		return -EIO;
	return 0;
}

static struct usb_driver mceusb_dev_driver = {
	.name =		DRIVER_NAME,
	.probe =	mceusb_dev_probe,
	.disconnect =	mceusb_dev_disconnect,
	.suspend =	mceusb_dev_suspend,
	.resume =	mceusb_dev_resume,
	.reset_resume =	mceusb_dev_resume,
	.id_table =	mceusb_dev_table
};

static int __init mceusb_dev_init(void)
{
	int ret;

	ret = usb_register(&mceusb_dev_driver);
	if (ret < 0)
		printk(KERN_ERR DRIVER_NAME
		       ": usb register failed, result = %d\n", ret);

	return ret;
}

static void __exit mceusb_dev_exit(void)
{
	usb_deregister(&mceusb_dev_driver);
}

module_init(mceusb_dev_init);
module_exit(mceusb_dev_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(usb, mceusb_dev_table);

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
