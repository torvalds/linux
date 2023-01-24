// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for NXP PN533 NFC Chip - USB transport layer
 *
 * Copyright (C) 2011 Instituto Nokia de Tecnologia
 * Copyright (C) 2012-2013 Tieto Poland
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/nfc.h>
#include <linux/netdevice.h>
#include <net/nfc/nfc.h>
#include "pn533.h"

#define VERSION "0.1"

#define PN533_VENDOR_ID 0x4CC
#define PN533_PRODUCT_ID 0x2533

#define SCM_VENDOR_ID 0x4E6
#define SCL3711_PRODUCT_ID 0x5591

#define SONY_VENDOR_ID         0x054c
#define PASORI_PRODUCT_ID      0x02e1

#define ACS_VENDOR_ID 0x072f
#define ACR122U_PRODUCT_ID 0x2200

static const struct usb_device_id pn533_usb_table[] = {
	{ USB_DEVICE(PN533_VENDOR_ID, PN533_PRODUCT_ID),
	  .driver_info = PN533_DEVICE_STD },
	{ USB_DEVICE(SCM_VENDOR_ID, SCL3711_PRODUCT_ID),
	  .driver_info = PN533_DEVICE_STD },
	{ USB_DEVICE(SONY_VENDOR_ID, PASORI_PRODUCT_ID),
	  .driver_info = PN533_DEVICE_PASORI },
	{ USB_DEVICE(ACS_VENDOR_ID, ACR122U_PRODUCT_ID),
	  .driver_info = PN533_DEVICE_ACR122U },
	{ }
};
MODULE_DEVICE_TABLE(usb, pn533_usb_table);

struct pn533_usb_phy {
	struct usb_device *udev;
	struct usb_interface *interface;

	struct urb *out_urb;
	struct urb *in_urb;

	struct urb *ack_urb;
	u8 *ack_buffer;

	struct pn533 *priv;
};

static void pn533_recv_response(struct urb *urb)
{
	struct pn533_usb_phy *phy = urb->context;
	struct sk_buff *skb = NULL;

	if (!urb->status) {
		skb = alloc_skb(urb->actual_length, GFP_ATOMIC);
		if (!skb) {
			nfc_err(&phy->udev->dev, "failed to alloc memory\n");
		} else {
			skb_put_data(skb, urb->transfer_buffer,
				     urb->actual_length);
		}
	}

	pn533_recv_frame(phy->priv, skb, urb->status);
}

static int pn533_submit_urb_for_response(struct pn533_usb_phy *phy, gfp_t flags)
{
	phy->in_urb->complete = pn533_recv_response;

	return usb_submit_urb(phy->in_urb, flags);
}

static void pn533_recv_ack(struct urb *urb)
{
	struct pn533_usb_phy *phy = urb->context;
	struct pn533 *priv = phy->priv;
	struct pn533_cmd *cmd = priv->cmd;
	struct pn533_std_frame *in_frame;
	int rc;

	cmd->status = urb->status;

	switch (urb->status) {
	case 0:
		break; /* success */
	case -ECONNRESET:
	case -ENOENT:
		dev_dbg(&phy->udev->dev,
			"The urb has been stopped (status %d)\n",
			urb->status);
		goto sched_wq;
	case -ESHUTDOWN:
	default:
		nfc_err(&phy->udev->dev,
			"Urb failure (status %d)\n", urb->status);
		goto sched_wq;
	}

	in_frame = phy->in_urb->transfer_buffer;

	if (!pn533_rx_frame_is_ack(in_frame)) {
		nfc_err(&phy->udev->dev, "Received an invalid ack\n");
		cmd->status = -EIO;
		goto sched_wq;
	}

	rc = pn533_submit_urb_for_response(phy, GFP_ATOMIC);
	if (rc) {
		nfc_err(&phy->udev->dev,
			"usb_submit_urb failed with result %d\n", rc);
		cmd->status = rc;
		goto sched_wq;
	}

	return;

sched_wq:
	queue_work(priv->wq, &priv->cmd_complete_work);
}

static int pn533_submit_urb_for_ack(struct pn533_usb_phy *phy, gfp_t flags)
{
	phy->in_urb->complete = pn533_recv_ack;

	return usb_submit_urb(phy->in_urb, flags);
}

static int pn533_usb_send_ack(struct pn533 *dev, gfp_t flags)
{
	struct pn533_usb_phy *phy = dev->phy;
	static const u8 ack[6] = {0x00, 0x00, 0xff, 0x00, 0xff, 0x00};
	/* spec 7.1.1.3:  Preamble, SoPC (2), ACK Code (2), Postamble */

	if (!phy->ack_buffer) {
		phy->ack_buffer = kmemdup(ack, sizeof(ack), flags);
		if (!phy->ack_buffer)
			return -ENOMEM;
	}

	phy->ack_urb->transfer_buffer = phy->ack_buffer;
	phy->ack_urb->transfer_buffer_length = sizeof(ack);
	return usb_submit_urb(phy->ack_urb, flags);
}

struct pn533_out_arg {
	struct pn533_usb_phy *phy;
	struct completion done;
};

static int pn533_usb_send_frame(struct pn533 *dev,
				struct sk_buff *out)
{
	struct pn533_usb_phy *phy = dev->phy;
	struct pn533_out_arg arg;
	void *cntx;
	int rc;

	if (phy->priv == NULL)
		phy->priv = dev;

	phy->out_urb->transfer_buffer = out->data;
	phy->out_urb->transfer_buffer_length = out->len;

	print_hex_dump_debug("PN533 TX: ", DUMP_PREFIX_NONE, 16, 1,
			     out->data, out->len, false);

	init_completion(&arg.done);
	cntx = phy->out_urb->context;
	phy->out_urb->context = &arg;

	rc = usb_submit_urb(phy->out_urb, GFP_KERNEL);
	if (rc)
		return rc;

	wait_for_completion(&arg.done);
	phy->out_urb->context = cntx;

	if (dev->protocol_type == PN533_PROTO_REQ_RESP) {
		/* request for response for sent packet directly */
		rc = pn533_submit_urb_for_response(phy, GFP_KERNEL);
		if (rc)
			goto error;
	} else if (dev->protocol_type == PN533_PROTO_REQ_ACK_RESP) {
		/* request for ACK if that's the case */
		rc = pn533_submit_urb_for_ack(phy, GFP_KERNEL);
		if (rc)
			goto error;
	}

	return 0;

error:
	usb_unlink_urb(phy->out_urb);
	return rc;
}

static void pn533_usb_abort_cmd(struct pn533 *dev, gfp_t flags)
{
	struct pn533_usb_phy *phy = dev->phy;

	/* ACR122U does not support any command which aborts last
	 * issued command i.e. as ACK for standard PN533. Additionally,
	 * it behaves stange, sending broken or incorrect responses,
	 * when we cancel urb before the chip will send response.
	 */
	if (dev->device_type == PN533_DEVICE_ACR122U)
		return;

	/* An ack will cancel the last issued command */
	pn533_usb_send_ack(dev, flags);

	/* cancel the urb request */
	usb_kill_urb(phy->in_urb);
}

/* ACR122 specific structs and functions */

/* ACS ACR122 pn533 frame definitions */
#define PN533_ACR122_TX_FRAME_HEADER_LEN (sizeof(struct pn533_acr122_tx_frame) \
					  + 2)
#define PN533_ACR122_TX_FRAME_TAIL_LEN 0
#define PN533_ACR122_RX_FRAME_HEADER_LEN (sizeof(struct pn533_acr122_rx_frame) \
					  + 2)
#define PN533_ACR122_RX_FRAME_TAIL_LEN 2
#define PN533_ACR122_FRAME_MAX_PAYLOAD_LEN PN533_STD_FRAME_MAX_PAYLOAD_LEN

/* CCID messages types */
#define PN533_ACR122_PC_TO_RDR_ICCPOWERON 0x62
#define PN533_ACR122_PC_TO_RDR_ESCAPE 0x6B

#define PN533_ACR122_RDR_TO_PC_ESCAPE 0x83


struct pn533_acr122_ccid_hdr {
	u8 type;
	u32 datalen;
	u8 slot;
	u8 seq;

	/*
	 * 3 msg specific bytes or status, error and 1 specific
	 * byte for reposnse msg
	 */
	u8 params[3];
	u8 data[]; /* payload */
} __packed;

struct pn533_acr122_apdu_hdr {
	u8 class;
	u8 ins;
	u8 p1;
	u8 p2;
} __packed;

struct pn533_acr122_tx_frame {
	struct pn533_acr122_ccid_hdr ccid;
	struct pn533_acr122_apdu_hdr apdu;
	u8 datalen;
	u8 data[]; /* pn533 frame: TFI ... */
} __packed;

struct pn533_acr122_rx_frame {
	struct pn533_acr122_ccid_hdr ccid;
	u8 data[]; /* pn533 frame : TFI ... */
} __packed;

static void pn533_acr122_tx_frame_init(void *_frame, u8 cmd_code)
{
	struct pn533_acr122_tx_frame *frame = _frame;

	frame->ccid.type = PN533_ACR122_PC_TO_RDR_ESCAPE;
	/* sizeof(apdu_hdr) + sizeof(datalen) */
	frame->ccid.datalen = sizeof(frame->apdu) + 1;
	frame->ccid.slot = 0;
	frame->ccid.seq = 0;
	frame->ccid.params[0] = 0;
	frame->ccid.params[1] = 0;
	frame->ccid.params[2] = 0;

	frame->data[0] = PN533_STD_FRAME_DIR_OUT;
	frame->data[1] = cmd_code;
	frame->datalen = 2;  /* data[0] + data[1] */

	frame->apdu.class = 0xFF;
	frame->apdu.ins = 0;
	frame->apdu.p1 = 0;
	frame->apdu.p2 = 0;
}

static void pn533_acr122_tx_frame_finish(void *_frame)
{
	struct pn533_acr122_tx_frame *frame = _frame;

	frame->ccid.datalen += frame->datalen;
}

static void pn533_acr122_tx_update_payload_len(void *_frame, int len)
{
	struct pn533_acr122_tx_frame *frame = _frame;

	frame->datalen += len;
}

static bool pn533_acr122_is_rx_frame_valid(void *_frame, struct pn533 *dev)
{
	struct pn533_acr122_rx_frame *frame = _frame;

	if (frame->ccid.type != 0x83)
		return false;

	if (!frame->ccid.datalen)
		return false;

	if (frame->data[frame->ccid.datalen - 2] == 0x63)
		return false;

	return true;
}

static int pn533_acr122_rx_frame_size(void *frame)
{
	struct pn533_acr122_rx_frame *f = frame;

	/* f->ccid.datalen already includes tail length */
	return sizeof(struct pn533_acr122_rx_frame) + f->ccid.datalen;
}

static u8 pn533_acr122_get_cmd_code(void *frame)
{
	struct pn533_acr122_rx_frame *f = frame;

	return PN533_FRAME_CMD(f);
}

static struct pn533_frame_ops pn533_acr122_frame_ops = {
	.tx_frame_init = pn533_acr122_tx_frame_init,
	.tx_frame_finish = pn533_acr122_tx_frame_finish,
	.tx_update_payload_len = pn533_acr122_tx_update_payload_len,
	.tx_header_len = PN533_ACR122_TX_FRAME_HEADER_LEN,
	.tx_tail_len = PN533_ACR122_TX_FRAME_TAIL_LEN,

	.rx_is_frame_valid = pn533_acr122_is_rx_frame_valid,
	.rx_header_len = PN533_ACR122_RX_FRAME_HEADER_LEN,
	.rx_tail_len = PN533_ACR122_RX_FRAME_TAIL_LEN,
	.rx_frame_size = pn533_acr122_rx_frame_size,

	.max_payload_len = PN533_ACR122_FRAME_MAX_PAYLOAD_LEN,
	.get_cmd_code = pn533_acr122_get_cmd_code,
};

struct pn533_acr122_poweron_rdr_arg {
	int rc;
	struct completion done;
};

static void pn533_acr122_poweron_rdr_resp(struct urb *urb)
{
	struct pn533_acr122_poweron_rdr_arg *arg = urb->context;

	print_hex_dump_debug("ACR122 RX: ", DUMP_PREFIX_NONE, 16, 1,
		       urb->transfer_buffer, urb->transfer_buffer_length,
		       false);

	arg->rc = urb->status;
	complete(&arg->done);
}

static int pn533_acr122_poweron_rdr(struct pn533_usb_phy *phy)
{
	/* Power on th reader (CCID cmd) */
	u8 cmd[10] = {PN533_ACR122_PC_TO_RDR_ICCPOWERON,
		      0, 0, 0, 0, 0, 0, 3, 0, 0};
	char *buffer;
	int transferred;
	int rc;
	void *cntx;
	struct pn533_acr122_poweron_rdr_arg arg;

	buffer = kmemdup(cmd, sizeof(cmd), GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	init_completion(&arg.done);
	cntx = phy->in_urb->context;  /* backup context */

	phy->in_urb->complete = pn533_acr122_poweron_rdr_resp;
	phy->in_urb->context = &arg;

	print_hex_dump_debug("ACR122 TX: ", DUMP_PREFIX_NONE, 16, 1,
		       cmd, sizeof(cmd), false);

	rc = usb_bulk_msg(phy->udev, phy->out_urb->pipe, buffer, sizeof(cmd),
			  &transferred, 5000);
	kfree(buffer);
	if (rc || (transferred != sizeof(cmd))) {
		nfc_err(&phy->udev->dev,
			"Reader power on cmd error %d\n", rc);
		return rc;
	}

	rc =  usb_submit_urb(phy->in_urb, GFP_KERNEL);
	if (rc) {
		nfc_err(&phy->udev->dev,
			"Can't submit reader poweron cmd response %d\n", rc);
		return rc;
	}

	wait_for_completion(&arg.done);
	phy->in_urb->context = cntx; /* restore context */

	return arg.rc;
}

static void pn533_out_complete(struct urb *urb)
{
	struct pn533_out_arg *arg = urb->context;
	struct pn533_usb_phy *phy = arg->phy;

	switch (urb->status) {
	case 0:
		break; /* success */
	case -ECONNRESET:
	case -ENOENT:
		dev_dbg(&phy->udev->dev,
			"The urb has been stopped (status %d)\n",
			urb->status);
		break;
	case -ESHUTDOWN:
	default:
		nfc_err(&phy->udev->dev,
			"Urb failure (status %d)\n",
			urb->status);
	}

	complete(&arg->done);
}

static void pn533_ack_complete(struct urb *urb)
{
	struct pn533_usb_phy *phy = urb->context;

	switch (urb->status) {
	case 0:
		break; /* success */
	case -ECONNRESET:
	case -ENOENT:
		dev_dbg(&phy->udev->dev,
			"The urb has been stopped (status %d)\n",
			urb->status);
		break;
	case -ESHUTDOWN:
	default:
		nfc_err(&phy->udev->dev,
			"Urb failure (status %d)\n",
			urb->status);
	}
}

static const struct pn533_phy_ops usb_phy_ops = {
	.send_frame = pn533_usb_send_frame,
	.send_ack = pn533_usb_send_ack,
	.abort_cmd = pn533_usb_abort_cmd,
};

static int pn533_usb_probe(struct usb_interface *interface,
			const struct usb_device_id *id)
{
	struct pn533 *priv;
	struct pn533_usb_phy *phy;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int in_endpoint = 0;
	int out_endpoint = 0;
	int rc = -ENOMEM;
	int i;
	u32 protocols;
	enum pn533_protocol_type protocol_type = PN533_PROTO_REQ_ACK_RESP;
	struct pn533_frame_ops *fops = NULL;
	unsigned char *in_buf;
	int in_buf_len = PN533_EXT_FRAME_HEADER_LEN +
			 PN533_STD_FRAME_MAX_PAYLOAD_LEN +
			 PN533_STD_FRAME_TAIL_LEN;

	phy = devm_kzalloc(&interface->dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	in_buf = kzalloc(in_buf_len, GFP_KERNEL);
	if (!in_buf)
		return -ENOMEM;

	phy->udev = usb_get_dev(interface_to_usbdev(interface));
	phy->interface = interface;

	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!in_endpoint && usb_endpoint_is_bulk_in(endpoint))
			in_endpoint = endpoint->bEndpointAddress;

		if (!out_endpoint && usb_endpoint_is_bulk_out(endpoint))
			out_endpoint = endpoint->bEndpointAddress;
	}

	if (!in_endpoint || !out_endpoint) {
		nfc_err(&interface->dev,
			"Could not find bulk-in or bulk-out endpoint\n");
		rc = -ENODEV;
		goto error;
	}

	phy->in_urb = usb_alloc_urb(0, GFP_KERNEL);
	phy->out_urb = usb_alloc_urb(0, GFP_KERNEL);
	phy->ack_urb = usb_alloc_urb(0, GFP_KERNEL);

	if (!phy->in_urb || !phy->out_urb || !phy->ack_urb)
		goto error;

	usb_fill_bulk_urb(phy->in_urb, phy->udev,
			  usb_rcvbulkpipe(phy->udev, in_endpoint),
			  in_buf, in_buf_len, NULL, phy);

	usb_fill_bulk_urb(phy->out_urb, phy->udev,
			  usb_sndbulkpipe(phy->udev, out_endpoint),
			  NULL, 0, pn533_out_complete, phy);
	usb_fill_bulk_urb(phy->ack_urb, phy->udev,
			  usb_sndbulkpipe(phy->udev, out_endpoint),
			  NULL, 0, pn533_ack_complete, phy);

	switch (id->driver_info) {
	case PN533_DEVICE_STD:
		protocols = PN533_ALL_PROTOCOLS;
		break;

	case PN533_DEVICE_PASORI:
		protocols = PN533_NO_TYPE_B_PROTOCOLS;
		break;

	case PN533_DEVICE_ACR122U:
		protocols = PN533_NO_TYPE_B_PROTOCOLS;
		fops = &pn533_acr122_frame_ops;
		protocol_type = PN533_PROTO_REQ_RESP;

		rc = pn533_acr122_poweron_rdr(phy);
		if (rc < 0) {
			nfc_err(&interface->dev,
				"Couldn't poweron the reader (error %d)\n", rc);
			goto error;
		}
		break;

	default:
		nfc_err(&interface->dev, "Unknown device type %lu\n",
			id->driver_info);
		rc = -EINVAL;
		goto error;
	}

	priv = pn53x_common_init(id->driver_info, protocol_type,
					phy, &usb_phy_ops, fops,
					&phy->udev->dev);

	if (IS_ERR(priv)) {
		rc = PTR_ERR(priv);
		goto error;
	}

	phy->priv = priv;

	rc = pn533_finalize_setup(priv);
	if (rc)
		goto err_clean;

	usb_set_intfdata(interface, phy);
	rc = pn53x_register_nfc(priv, protocols, &interface->dev);
	if (rc)
		goto err_clean;

	return 0;

err_clean:
	pn53x_common_clean(priv);
error:
	usb_kill_urb(phy->in_urb);
	usb_kill_urb(phy->out_urb);
	usb_kill_urb(phy->ack_urb);

	usb_free_urb(phy->in_urb);
	usb_free_urb(phy->out_urb);
	usb_free_urb(phy->ack_urb);
	usb_put_dev(phy->udev);
	kfree(in_buf);
	kfree(phy->ack_buffer);

	return rc;
}

static void pn533_usb_disconnect(struct usb_interface *interface)
{
	struct pn533_usb_phy *phy = usb_get_intfdata(interface);

	if (!phy)
		return;

	pn53x_unregister_nfc(phy->priv);
	pn53x_common_clean(phy->priv);

	usb_set_intfdata(interface, NULL);

	usb_kill_urb(phy->in_urb);
	usb_kill_urb(phy->out_urb);
	usb_kill_urb(phy->ack_urb);

	kfree(phy->in_urb->transfer_buffer);
	usb_free_urb(phy->in_urb);
	usb_free_urb(phy->out_urb);
	usb_free_urb(phy->ack_urb);
	kfree(phy->ack_buffer);

	nfc_info(&interface->dev, "NXP PN533 NFC device disconnected\n");
}

static struct usb_driver pn533_usb_driver = {
	.name =		"pn533_usb",
	.probe =	pn533_usb_probe,
	.disconnect =	pn533_usb_disconnect,
	.id_table =	pn533_usb_table,
};

module_usb_driver(pn533_usb_driver);

MODULE_AUTHOR("Lauro Ramos Venancio <lauro.venancio@openbossa.org>");
MODULE_AUTHOR("Aloisio Almeida Jr <aloisio.almeida@openbossa.org>");
MODULE_AUTHOR("Waldemar Rymarkiewicz <waldemar.rymarkiewicz@tieto.com>");
MODULE_DESCRIPTION("PN533 USB driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
