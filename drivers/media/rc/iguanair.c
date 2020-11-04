// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * IguanaWorks USB IR Transceiver support
 *
 * Copyright (C) 2012 Sean Young <sean@mess.org>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <media/rc-core.h>

#define BUF_SIZE 152

struct iguanair {
	struct rc_dev *rc;

	struct device *dev;
	struct usb_device *udev;

	uint16_t version;
	uint8_t bufsize;
	uint8_t cycle_overhead;

	/* receiver support */
	bool receiver_on;
	dma_addr_t dma_in, dma_out;
	uint8_t *buf_in;
	struct urb *urb_in, *urb_out;
	struct completion completion;

	/* transmit support */
	bool tx_overflow;
	uint32_t carrier;
	struct send_packet *packet;

	char name[64];
	char phys[64];
};

#define CMD_NOP			0x00
#define CMD_GET_VERSION		0x01
#define CMD_GET_BUFSIZE		0x11
#define CMD_GET_FEATURES	0x10
#define CMD_SEND		0x15
#define CMD_EXECUTE		0x1f
#define CMD_RX_OVERFLOW		0x31
#define CMD_TX_OVERFLOW		0x32
#define CMD_RECEIVER_ON		0x12
#define CMD_RECEIVER_OFF	0x14

#define DIR_IN			0xdc
#define DIR_OUT			0xcd

#define MAX_IN_PACKET		8u
#define MAX_OUT_PACKET		(sizeof(struct send_packet) + BUF_SIZE)
#define TIMEOUT			1000
#define RX_RESOLUTION		21

struct packet {
	uint16_t start;
	uint8_t direction;
	uint8_t cmd;
};

struct send_packet {
	struct packet header;
	uint8_t length;
	uint8_t channels;
	uint8_t busy7;
	uint8_t busy4;
	uint8_t payload[];
};

static void process_ir_data(struct iguanair *ir, unsigned len)
{
	if (len >= 4 && ir->buf_in[0] == 0 && ir->buf_in[1] == 0) {
		switch (ir->buf_in[3]) {
		case CMD_GET_VERSION:
			if (len == 6) {
				ir->version = (ir->buf_in[5] << 8) |
							ir->buf_in[4];
				complete(&ir->completion);
			}
			break;
		case CMD_GET_BUFSIZE:
			if (len >= 5) {
				ir->bufsize = ir->buf_in[4];
				complete(&ir->completion);
			}
			break;
		case CMD_GET_FEATURES:
			if (len > 5) {
				ir->cycle_overhead = ir->buf_in[5];
				complete(&ir->completion);
			}
			break;
		case CMD_TX_OVERFLOW:
			ir->tx_overflow = true;
			fallthrough;
		case CMD_RECEIVER_OFF:
		case CMD_RECEIVER_ON:
		case CMD_SEND:
			complete(&ir->completion);
			break;
		case CMD_RX_OVERFLOW:
			dev_warn(ir->dev, "receive overflow\n");
			ir_raw_event_reset(ir->rc);
			break;
		default:
			dev_warn(ir->dev, "control code %02x received\n",
							ir->buf_in[3]);
			break;
		}
	} else if (len >= 7) {
		struct ir_raw_event rawir = {};
		unsigned i;
		bool event = false;

		for (i = 0; i < 7; i++) {
			if (ir->buf_in[i] == 0x80) {
				rawir.pulse = false;
				rawir.duration = 21845;
			} else {
				rawir.pulse = (ir->buf_in[i] & 0x80) == 0;
				rawir.duration = ((ir->buf_in[i] & 0x7f) + 1) *
								 RX_RESOLUTION;
			}

			if (ir_raw_event_store_with_filter(ir->rc, &rawir))
				event = true;
		}

		if (event)
			ir_raw_event_handle(ir->rc);
	}
}

static void iguanair_rx(struct urb *urb)
{
	struct iguanair *ir;
	int rc;

	if (!urb)
		return;

	ir = urb->context;
	if (!ir) {
		usb_unlink_urb(urb);
		return;
	}

	switch (urb->status) {
	case 0:
		process_ir_data(ir, urb->actual_length);
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		usb_unlink_urb(urb);
		return;
	case -EPIPE:
	default:
		dev_dbg(ir->dev, "Error: urb status = %d\n", urb->status);
		break;
	}

	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (rc && rc != -ENODEV)
		dev_warn(ir->dev, "failed to resubmit urb: %d\n", rc);
}

static void iguanair_irq_out(struct urb *urb)
{
	struct iguanair *ir = urb->context;

	if (urb->status)
		dev_dbg(ir->dev, "Error: out urb status = %d\n", urb->status);

	/* if we sent an nop packet, do not expect a response */
	if (urb->status == 0 && ir->packet->header.cmd == CMD_NOP)
		complete(&ir->completion);
}

static int iguanair_send(struct iguanair *ir, unsigned size)
{
	int rc;

	reinit_completion(&ir->completion);

	ir->urb_out->transfer_buffer_length = size;
	rc = usb_submit_urb(ir->urb_out, GFP_KERNEL);
	if (rc)
		return rc;

	if (wait_for_completion_timeout(&ir->completion, TIMEOUT) == 0)
		return -ETIMEDOUT;

	return rc;
}

static int iguanair_get_features(struct iguanair *ir)
{
	int rc;

	/*
	 * On cold boot, the iguanair initializes on the first packet
	 * received but does not process that packet. Send an empty
	 * packet.
	 */
	ir->packet->header.start = 0;
	ir->packet->header.direction = DIR_OUT;
	ir->packet->header.cmd = CMD_NOP;
	iguanair_send(ir, sizeof(ir->packet->header));

	ir->packet->header.cmd = CMD_GET_VERSION;
	rc = iguanair_send(ir, sizeof(ir->packet->header));
	if (rc) {
		dev_info(ir->dev, "failed to get version\n");
		goto out;
	}

	if (ir->version < 0x205) {
		dev_err(ir->dev, "firmware 0x%04x is too old\n", ir->version);
		rc = -ENODEV;
		goto out;
	}

	ir->bufsize = 150;
	ir->cycle_overhead = 65;

	ir->packet->header.cmd = CMD_GET_BUFSIZE;

	rc = iguanair_send(ir, sizeof(ir->packet->header));
	if (rc) {
		dev_info(ir->dev, "failed to get buffer size\n");
		goto out;
	}

	if (ir->bufsize > BUF_SIZE) {
		dev_info(ir->dev, "buffer size %u larger than expected\n",
								ir->bufsize);
		ir->bufsize = BUF_SIZE;
	}

	ir->packet->header.cmd = CMD_GET_FEATURES;

	rc = iguanair_send(ir, sizeof(ir->packet->header));
	if (rc)
		dev_info(ir->dev, "failed to get features\n");
out:
	return rc;
}

static int iguanair_receiver(struct iguanair *ir, bool enable)
{
	ir->packet->header.start = 0;
	ir->packet->header.direction = DIR_OUT;
	ir->packet->header.cmd = enable ? CMD_RECEIVER_ON : CMD_RECEIVER_OFF;

	if (enable)
		ir_raw_event_reset(ir->rc);

	return iguanair_send(ir, sizeof(ir->packet->header));
}

/*
 * The iguanair creates the carrier by busy spinning after each half period.
 * This is counted in CPU cycles, with the CPU running at 24MHz. It is
 * broken down into 7-cycles and 4-cyles delays, with a preference for
 * 4-cycle delays, minus the overhead of the loop itself (cycle_overhead).
 */
static int iguanair_set_tx_carrier(struct rc_dev *dev, uint32_t carrier)
{
	struct iguanair *ir = dev->priv;

	if (carrier < 25000 || carrier > 150000)
		return -EINVAL;

	if (carrier != ir->carrier) {
		uint32_t cycles, fours, sevens;

		ir->carrier = carrier;

		cycles = DIV_ROUND_CLOSEST(24000000, carrier * 2) -
							ir->cycle_overhead;

		/*
		 * Calculate minimum number of 7 cycles needed so
		 * we are left with a multiple of 4; so we want to have
		 * (sevens * 7) & 3 == cycles & 3
		 */
		sevens = (4 - cycles) & 3;
		fours = (cycles - sevens * 7) / 4;

		/*
		 * The firmware interprets these values as a relative offset
		 * for a branch. Immediately following the branches, there
		 * 4 instructions of 7 cycles (2 bytes each) and 110
		 * instructions of 4 cycles (1 byte each). A relative branch
		 * of 0 will execute all of them, branch further for less
		 * cycle burning.
		 */
		ir->packet->busy7 = (4 - sevens) * 2;
		ir->packet->busy4 = 110 - fours;
	}

	return 0;
}

static int iguanair_set_tx_mask(struct rc_dev *dev, uint32_t mask)
{
	struct iguanair *ir = dev->priv;

	if (mask > 15)
		return 4;

	ir->packet->channels = mask << 4;

	return 0;
}

static int iguanair_tx(struct rc_dev *dev, unsigned *txbuf, unsigned count)
{
	struct iguanair *ir = dev->priv;
	unsigned int i, size, p, periods;
	int rc;

	/* convert from us to carrier periods */
	for (i = size = 0; i < count; i++) {
		periods = DIV_ROUND_CLOSEST(txbuf[i] * ir->carrier, 1000000);
		while (periods) {
			p = min(periods, 127u);
			if (size >= ir->bufsize) {
				rc = -EINVAL;
				goto out;
			}
			ir->packet->payload[size++] = p | ((i & 1) ? 0x80 : 0);
			periods -= p;
		}
	}

	ir->packet->header.start = 0;
	ir->packet->header.direction = DIR_OUT;
	ir->packet->header.cmd = CMD_SEND;
	ir->packet->length = size;

	ir->tx_overflow = false;

	rc = iguanair_send(ir, sizeof(*ir->packet) + size);

	if (rc == 0 && ir->tx_overflow)
		rc = -EOVERFLOW;

out:
	return rc ? rc : count;
}

static int iguanair_open(struct rc_dev *rdev)
{
	struct iguanair *ir = rdev->priv;
	int rc;

	rc = iguanair_receiver(ir, true);
	if (rc == 0)
		ir->receiver_on = true;

	return rc;
}

static void iguanair_close(struct rc_dev *rdev)
{
	struct iguanair *ir = rdev->priv;
	int rc;

	rc = iguanair_receiver(ir, false);
	ir->receiver_on = false;
	if (rc && rc != -ENODEV)
		dev_warn(ir->dev, "failed to disable receiver: %d\n", rc);
}

static int iguanair_probe(struct usb_interface *intf,
			  const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct iguanair *ir;
	struct rc_dev *rc;
	int ret, pipein, pipeout;
	struct usb_host_interface *idesc;

	idesc = intf->cur_altsetting;
	if (idesc->desc.bNumEndpoints < 2)
		return -ENODEV;

	ir = kzalloc(sizeof(*ir), GFP_KERNEL);
	rc = rc_allocate_device(RC_DRIVER_IR_RAW);
	if (!ir || !rc) {
		ret = -ENOMEM;
		goto out;
	}

	ir->buf_in = usb_alloc_coherent(udev, MAX_IN_PACKET, GFP_KERNEL,
								&ir->dma_in);
	ir->packet = usb_alloc_coherent(udev, MAX_OUT_PACKET, GFP_KERNEL,
								&ir->dma_out);
	ir->urb_in = usb_alloc_urb(0, GFP_KERNEL);
	ir->urb_out = usb_alloc_urb(0, GFP_KERNEL);

	if (!ir->buf_in || !ir->packet || !ir->urb_in || !ir->urb_out ||
	    !usb_endpoint_is_int_in(&idesc->endpoint[0].desc) ||
	    !usb_endpoint_is_int_out(&idesc->endpoint[1].desc)) {
		ret = -ENOMEM;
		goto out;
	}

	ir->rc = rc;
	ir->dev = &intf->dev;
	ir->udev = udev;

	init_completion(&ir->completion);
	pipeout = usb_sndintpipe(udev,
				idesc->endpoint[1].desc.bEndpointAddress);
	usb_fill_int_urb(ir->urb_out, udev, pipeout, ir->packet, MAX_OUT_PACKET,
						iguanair_irq_out, ir, 1);
	ir->urb_out->transfer_dma = ir->dma_out;
	ir->urb_out->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	pipein = usb_rcvintpipe(udev, idesc->endpoint[0].desc.bEndpointAddress);
	usb_fill_int_urb(ir->urb_in, udev, pipein, ir->buf_in, MAX_IN_PACKET,
							 iguanair_rx, ir, 1);
	ir->urb_in->transfer_dma = ir->dma_in;
	ir->urb_in->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	ret = usb_submit_urb(ir->urb_in, GFP_KERNEL);
	if (ret) {
		dev_warn(&intf->dev, "failed to submit urb: %d\n", ret);
		goto out;
	}

	ret = iguanair_get_features(ir);
	if (ret)
		goto out2;

	snprintf(ir->name, sizeof(ir->name),
		"IguanaWorks USB IR Transceiver version 0x%04x", ir->version);

	usb_make_path(ir->udev, ir->phys, sizeof(ir->phys));

	rc->device_name = ir->name;
	rc->input_phys = ir->phys;
	usb_to_input_id(ir->udev, &rc->input_id);
	rc->dev.parent = &intf->dev;
	rc->allowed_protocols = RC_PROTO_BIT_ALL_IR_DECODER;
	rc->priv = ir;
	rc->open = iguanair_open;
	rc->close = iguanair_close;
	rc->s_tx_mask = iguanair_set_tx_mask;
	rc->s_tx_carrier = iguanair_set_tx_carrier;
	rc->tx_ir = iguanair_tx;
	rc->driver_name = KBUILD_MODNAME;
	rc->map_name = RC_MAP_RC6_MCE;
	rc->min_timeout = 1;
	rc->timeout = IR_DEFAULT_TIMEOUT;
	rc->max_timeout = 10 * IR_DEFAULT_TIMEOUT;
	rc->rx_resolution = RX_RESOLUTION;

	iguanair_set_tx_carrier(rc, 38000);
	iguanair_set_tx_mask(rc, 0);

	ret = rc_register_device(rc);
	if (ret < 0) {
		dev_err(&intf->dev, "failed to register rc device %d", ret);
		goto out2;
	}

	usb_set_intfdata(intf, ir);

	return 0;
out2:
	usb_kill_urb(ir->urb_in);
	usb_kill_urb(ir->urb_out);
out:
	if (ir) {
		usb_free_urb(ir->urb_in);
		usb_free_urb(ir->urb_out);
		usb_free_coherent(udev, MAX_IN_PACKET, ir->buf_in, ir->dma_in);
		usb_free_coherent(udev, MAX_OUT_PACKET, ir->packet,
								ir->dma_out);
	}
	rc_free_device(rc);
	kfree(ir);
	return ret;
}

static void iguanair_disconnect(struct usb_interface *intf)
{
	struct iguanair *ir = usb_get_intfdata(intf);

	rc_unregister_device(ir->rc);
	usb_set_intfdata(intf, NULL);
	usb_kill_urb(ir->urb_in);
	usb_kill_urb(ir->urb_out);
	usb_free_urb(ir->urb_in);
	usb_free_urb(ir->urb_out);
	usb_free_coherent(ir->udev, MAX_IN_PACKET, ir->buf_in, ir->dma_in);
	usb_free_coherent(ir->udev, MAX_OUT_PACKET, ir->packet, ir->dma_out);
	kfree(ir);
}

static int iguanair_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct iguanair *ir = usb_get_intfdata(intf);
	int rc = 0;

	if (ir->receiver_on) {
		rc = iguanair_receiver(ir, false);
		if (rc)
			dev_warn(ir->dev, "failed to disable receiver for suspend\n");
	}

	usb_kill_urb(ir->urb_in);
	usb_kill_urb(ir->urb_out);

	return rc;
}

static int iguanair_resume(struct usb_interface *intf)
{
	struct iguanair *ir = usb_get_intfdata(intf);
	int rc;

	rc = usb_submit_urb(ir->urb_in, GFP_KERNEL);
	if (rc)
		dev_warn(&intf->dev, "failed to submit urb: %d\n", rc);

	if (ir->receiver_on) {
		rc = iguanair_receiver(ir, true);
		if (rc)
			dev_warn(ir->dev, "failed to enable receiver after resume\n");
	}

	return rc;
}

static const struct usb_device_id iguanair_table[] = {
	{ USB_DEVICE(0x1781, 0x0938) },
	{ }
};

static struct usb_driver iguanair_driver = {
	.name =	KBUILD_MODNAME,
	.probe = iguanair_probe,
	.disconnect = iguanair_disconnect,
	.suspend = iguanair_suspend,
	.resume = iguanair_resume,
	.reset_resume = iguanair_resume,
	.id_table = iguanair_table,
	.soft_unbind = 1	/* we want to disable receiver on unbind */
};

module_usb_driver(iguanair_driver);

MODULE_DESCRIPTION("IguanaWorks USB IR Transceiver");
MODULE_AUTHOR("Sean Young <sean@mess.org>");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(usb, iguanair_table);

