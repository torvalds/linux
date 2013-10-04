/*
 * Sony NFC Port-100 Series driver
 * Copyright (c) 2013, Intel Corporation.
 *
 * Partly based/Inspired by Stephen Tiedemann's nfcpy
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/module.h>
#include <linux/usb.h>
#include <net/nfc/digital.h>

#define VERSION "0.1"

#define SONY_VENDOR_ID    0x054c
#define RCS380_PRODUCT_ID 0x06c1

#define PORT100_PROTOCOLS (NFC_PROTO_JEWEL_MASK    | \
			   NFC_PROTO_MIFARE_MASK   | \
			   NFC_PROTO_FELICA_MASK   | \
			   NFC_PROTO_NFC_DEP_MASK)

#define PORT100_CAPABILITIES (NFC_DIGITAL_DRV_CAPS_IN_CRC | \
			      NFC_DIGITAL_DRV_CAPS_TG_CRC)

/* Standard port100 frame definitions */
#define PORT100_FRAME_HEADER_LEN (sizeof(struct port100_frame) \
				  + 2) /* data[0] CC, data[1] SCC */
#define PORT100_FRAME_TAIL_LEN 2 /* data[len] DCS, data[len + 1] postamble*/

#define PORT100_COMM_RF_HEAD_MAX_LEN (sizeof(struct port100_tg_comm_rf_cmd))

/*
 * Max extended frame payload len, excluding CC and SCC
 * which are already in PORT100_FRAME_HEADER_LEN.
 */
#define PORT100_FRAME_MAX_PAYLOAD_LEN 1001

#define PORT100_FRAME_ACK_SIZE 6 /* Preamble (1), SoPC (2), ACK Code (2),
				    Postamble (1) */
static u8 ack_frame[PORT100_FRAME_ACK_SIZE] = {
	0x00, 0x00, 0xff, 0x00, 0xff, 0x00
};

#define PORT100_FRAME_CHECKSUM(f) (f->data[le16_to_cpu(f->datalen)])
#define PORT100_FRAME_POSTAMBLE(f) (f->data[le16_to_cpu(f->datalen) + 1])

/* start of frame */
#define PORT100_FRAME_SOF	0x00FF
#define PORT100_FRAME_EXT	0xFFFF
#define PORT100_FRAME_ACK	0x00FF

/* Port-100 command: in or out */
#define PORT100_FRAME_DIRECTION(f) (f->data[0]) /* CC */
#define PORT100_FRAME_DIR_OUT 0xD6
#define PORT100_FRAME_DIR_IN  0xD7

/* Port-100 sub-command */
#define PORT100_FRAME_CMD(f) (f->data[1]) /* SCC */

#define PORT100_CMD_GET_FIRMWARE_VERSION 0x20
#define PORT100_CMD_GET_COMMAND_TYPE     0x28
#define PORT100_CMD_SET_COMMAND_TYPE     0x2A

#define PORT100_CMD_RESPONSE(cmd) (cmd + 1)

#define PORT100_CMD_TYPE_IS_SUPPORTED(mask, cmd_type) \
	((mask) & (0x01 << (cmd_type)))
#define PORT100_CMD_TYPE_0	0
#define PORT100_CMD_TYPE_1	1

struct port100;

typedef void (*port100_send_async_complete_t)(struct port100 *dev, void *arg,
					      struct sk_buff *resp);

struct port100 {
	struct nfc_digital_dev *nfc_digital_dev;

	int skb_headroom;
	int skb_tailroom;

	struct usb_device *udev;
	struct usb_interface *interface;

	struct urb *out_urb;
	struct urb *in_urb;

	struct work_struct cmd_complete_work;

	u8 cmd_type;

	/* The digital stack serializes commands to be sent. There is no need
	 * for any queuing/locking mechanism at driver level.
	 */
	struct port100_cmd *cmd;
};

struct port100_cmd {
	u8 code;
	int status;
	struct sk_buff *req;
	struct sk_buff *resp;
	int resp_len;
	port100_send_async_complete_t  complete_cb;
	void *complete_cb_context;
};

struct port100_frame {
	u8 preamble;
	__be16 start_frame;
	__be16 extended_frame;
	__le16 datalen;
	u8 datalen_checksum;
	u8 data[];
} __packed;

struct port100_ack_frame {
	u8 preamble;
	__be16 start_frame;
	__be16 ack_frame;
	u8 postambule;
} __packed;

struct port100_cb_arg {
	nfc_digital_cmd_complete_t complete_cb;
	void *complete_arg;
	u8 mdaa;
};

struct port100_tg_comm_rf_cmd {
	__le16 guard_time;
	__le16 send_timeout;
	u8 mdaa;
	u8 nfca_param[6];
	u8 nfcf_param[18];
	u8 mf_halted;
	u8 arae_flag;
	__le16 recv_timeout;
	u8 data[];
} __packed;

/* The rule: value + checksum = 0 */
static inline u8 port100_checksum(u16 value)
{
	return ~(((u8 *)&value)[0] + ((u8 *)&value)[1]) + 1;
}

/* The rule: sum(data elements) + checksum = 0 */
static u8 port100_data_checksum(u8 *data, int datalen)
{
	u8 sum = 0;
	int i;

	for (i = 0; i < datalen; i++)
		sum += data[i];

	return port100_checksum(sum);
}

static void port100_tx_frame_init(void *_frame, u8 cmd_code)
{
	struct port100_frame *frame = _frame;

	frame->preamble = 0;
	frame->start_frame = cpu_to_be16(PORT100_FRAME_SOF);
	frame->extended_frame = cpu_to_be16(PORT100_FRAME_EXT);
	PORT100_FRAME_DIRECTION(frame) = PORT100_FRAME_DIR_OUT;
	PORT100_FRAME_CMD(frame) = cmd_code;
	frame->datalen = cpu_to_le16(2);
}

static void port100_tx_frame_finish(void *_frame)
{
	struct port100_frame *frame = _frame;

	frame->datalen_checksum = port100_checksum(le16_to_cpu(frame->datalen));

	PORT100_FRAME_CHECKSUM(frame) =
		port100_data_checksum(frame->data, le16_to_cpu(frame->datalen));

	PORT100_FRAME_POSTAMBLE(frame) = 0;
}

static void port100_tx_update_payload_len(void *_frame, int len)
{
	struct port100_frame *frame = _frame;

	frame->datalen = cpu_to_le16(le16_to_cpu(frame->datalen) + len);
}

static bool port100_rx_frame_is_valid(void *_frame)
{
	u8 checksum;
	struct port100_frame *frame = _frame;

	if (frame->start_frame != cpu_to_be16(PORT100_FRAME_SOF) ||
	    frame->extended_frame != cpu_to_be16(PORT100_FRAME_EXT))
		return false;

	checksum = port100_checksum(le16_to_cpu(frame->datalen));
	if (checksum != frame->datalen_checksum)
		return false;

	checksum = port100_data_checksum(frame->data,
					 le16_to_cpu(frame->datalen));
	if (checksum != PORT100_FRAME_CHECKSUM(frame))
		return false;

	return true;
}

static bool port100_rx_frame_is_ack(struct port100_ack_frame *frame)
{
	return (frame->start_frame == cpu_to_be16(PORT100_FRAME_SOF) &&
		frame->ack_frame == cpu_to_be16(PORT100_FRAME_ACK));
}

static inline int port100_rx_frame_size(void *frame)
{
	struct port100_frame *f = frame;

	return sizeof(struct port100_frame) + le16_to_cpu(f->datalen) +
	       PORT100_FRAME_TAIL_LEN;
}

static bool port100_rx_frame_is_cmd_response(struct port100 *dev, void *frame)
{
	struct port100_frame *f = frame;

	return (PORT100_FRAME_CMD(f) == PORT100_CMD_RESPONSE(dev->cmd->code));
}

static void port100_recv_response(struct urb *urb)
{
	struct port100 *dev = urb->context;
	struct port100_cmd *cmd = dev->cmd;
	u8 *in_frame;

	cmd->status = urb->status;

	switch (urb->status) {
	case 0:
		break; /* success */
	case -ECONNRESET:
	case -ENOENT:
		nfc_err(&dev->interface->dev,
			"The urb has been canceled (status %d)", urb->status);
		goto sched_wq;
	case -ESHUTDOWN:
	default:
		nfc_err(&dev->interface->dev, "Urb failure (status %d)",
			urb->status);
		goto sched_wq;
	}

	in_frame = dev->in_urb->transfer_buffer;

	if (!port100_rx_frame_is_valid(in_frame)) {
		nfc_err(&dev->interface->dev, "Received an invalid frame");
		cmd->status = -EIO;
		goto sched_wq;
	}

	print_hex_dump_debug("PORT100 RX: ", DUMP_PREFIX_NONE, 16, 1, in_frame,
			     port100_rx_frame_size(in_frame), false);

	if (!port100_rx_frame_is_cmd_response(dev, in_frame)) {
		nfc_err(&dev->interface->dev,
			"It's not the response to the last command");
		cmd->status = -EIO;
		goto sched_wq;
	}

sched_wq:
	schedule_work(&dev->cmd_complete_work);
}

static int port100_submit_urb_for_response(struct port100 *dev, gfp_t flags)
{
	dev->in_urb->complete = port100_recv_response;

	return usb_submit_urb(dev->in_urb, flags);
}

static void port100_recv_ack(struct urb *urb)
{
	struct port100 *dev = urb->context;
	struct port100_cmd *cmd = dev->cmd;
	struct port100_ack_frame *in_frame;
	int rc;

	cmd->status = urb->status;

	switch (urb->status) {
	case 0:
		break; /* success */
	case -ECONNRESET:
	case -ENOENT:
		nfc_err(&dev->interface->dev,
			"The urb has been stopped (status %d)", urb->status);
		goto sched_wq;
	case -ESHUTDOWN:
	default:
		nfc_err(&dev->interface->dev, "Urb failure (status %d)",
			urb->status);
		goto sched_wq;
	}

	in_frame = dev->in_urb->transfer_buffer;

	if (!port100_rx_frame_is_ack(in_frame)) {
		nfc_err(&dev->interface->dev, "Received an invalid ack");
		cmd->status = -EIO;
		goto sched_wq;
	}

	rc = port100_submit_urb_for_response(dev, GFP_ATOMIC);
	if (rc) {
		nfc_err(&dev->interface->dev,
			"usb_submit_urb failed with result %d", rc);
		cmd->status = rc;
		goto sched_wq;
	}

	return;

sched_wq:
	schedule_work(&dev->cmd_complete_work);
}

static int port100_submit_urb_for_ack(struct port100 *dev, gfp_t flags)
{
	dev->in_urb->complete = port100_recv_ack;

	return usb_submit_urb(dev->in_urb, flags);
}

static int port100_send_ack(struct port100 *dev)
{
	int rc;

	dev->out_urb->transfer_buffer = ack_frame;
	dev->out_urb->transfer_buffer_length = sizeof(ack_frame);
	rc = usb_submit_urb(dev->out_urb, GFP_KERNEL);

	return rc;
}

static int port100_send_frame_async(struct port100 *dev, struct sk_buff *out,
				    struct sk_buff *in, int in_len)
{
	int rc;

	dev->out_urb->transfer_buffer = out->data;
	dev->out_urb->transfer_buffer_length = out->len;

	dev->in_urb->transfer_buffer = in->data;
	dev->in_urb->transfer_buffer_length = in_len;

	print_hex_dump_debug("PORT100 TX: ", DUMP_PREFIX_NONE, 16, 1,
			     out->data, out->len, false);

	rc = usb_submit_urb(dev->out_urb, GFP_KERNEL);
	if (rc)
		return rc;

	rc = port100_submit_urb_for_ack(dev, GFP_KERNEL);
	if (rc)
		goto error;

	return 0;

error:
	usb_unlink_urb(dev->out_urb);
	return rc;
}

static void port100_build_cmd_frame(struct port100 *dev, u8 cmd_code,
				    struct sk_buff *skb)
{
	/* payload is already there, just update datalen */
	int payload_len = skb->len;

	skb_push(skb, PORT100_FRAME_HEADER_LEN);
	skb_put(skb, PORT100_FRAME_TAIL_LEN);

	port100_tx_frame_init(skb->data, cmd_code);
	port100_tx_update_payload_len(skb->data, payload_len);
	port100_tx_frame_finish(skb->data);
}

static void port100_send_async_complete(struct port100 *dev)
{
	struct port100_cmd *cmd = dev->cmd;
	int status = cmd->status;

	struct sk_buff *req = cmd->req;
	struct sk_buff *resp = cmd->resp;

	dev_kfree_skb(req);

	dev->cmd = NULL;

	if (status < 0) {
		cmd->complete_cb(dev, cmd->complete_cb_context,
				 ERR_PTR(status));
		dev_kfree_skb(resp);
		goto done;
	}

	skb_put(resp, port100_rx_frame_size(resp->data));
	skb_pull(resp, PORT100_FRAME_HEADER_LEN);
	skb_trim(resp, resp->len - PORT100_FRAME_TAIL_LEN);

	cmd->complete_cb(dev, cmd->complete_cb_context, resp);

done:
	kfree(cmd);
}

static int port100_send_cmd_async(struct port100 *dev, u8 cmd_code,
				struct sk_buff *req,
				port100_send_async_complete_t complete_cb,
				void *complete_cb_context)
{
	struct port100_cmd *cmd;
	struct sk_buff *resp;
	int rc;
	int  resp_len = PORT100_FRAME_HEADER_LEN +
			PORT100_FRAME_MAX_PAYLOAD_LEN +
			PORT100_FRAME_TAIL_LEN;

	resp = alloc_skb(resp_len, GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		dev_kfree_skb(resp);
		return -ENOMEM;
	}

	cmd->code = cmd_code;
	cmd->req = req;
	cmd->resp = resp;
	cmd->resp_len = resp_len;
	cmd->complete_cb = complete_cb;
	cmd->complete_cb_context = complete_cb_context;

	port100_build_cmd_frame(dev, cmd_code, req);

	dev->cmd = cmd;

	rc = port100_send_frame_async(dev, req, resp, resp_len);
	if (rc) {
		kfree(cmd);
		dev_kfree_skb(resp);
		dev->cmd = NULL;
	}

	return rc;
}

struct port100_sync_cmd_response {
	struct sk_buff *resp;
	struct completion done;
};

static void port100_wq_cmd_complete(struct work_struct *work)
{
	struct port100 *dev = container_of(work, struct port100,
					   cmd_complete_work);

	port100_send_async_complete(dev);
}

static void port100_send_sync_complete(struct port100 *dev, void *_arg,
				      struct sk_buff *resp)
{
	struct port100_sync_cmd_response *arg = _arg;

	arg->resp = resp;
	complete(&arg->done);
}

static struct sk_buff *port100_send_cmd_sync(struct port100 *dev, u8 cmd_code,
					     struct sk_buff *req)
{
	int rc;
	struct port100_sync_cmd_response arg;

	init_completion(&arg.done);

	rc = port100_send_cmd_async(dev, cmd_code, req,
				    port100_send_sync_complete, &arg);
	if (rc) {
		dev_kfree_skb(req);
		return ERR_PTR(rc);
	}

	wait_for_completion(&arg.done);

	return arg.resp;
}

static void port100_send_complete(struct urb *urb)
{
	struct port100 *dev = urb->context;

	switch (urb->status) {
	case 0:
		break; /* success */
	case -ECONNRESET:
	case -ENOENT:
		nfc_err(&dev->interface->dev,
			"The urb has been stopped (status %d)", urb->status);
		break;
	case -ESHUTDOWN:
	default:
		nfc_err(&dev->interface->dev, "Urb failure (status %d)",
			urb->status);
	}
}

static void port100_abort_cmd(struct nfc_digital_dev *ddev)
{
	struct port100 *dev = nfc_digital_get_drvdata(ddev);

	/* An ack will cancel the last issued command */
	port100_send_ack(dev);

	/* cancel the urb request */
	usb_kill_urb(dev->in_urb);
}

static struct sk_buff *port100_alloc_skb(struct port100 *dev, unsigned int size)
{
	struct sk_buff *skb;

	skb = alloc_skb(dev->skb_headroom + dev->skb_tailroom + size,
			GFP_KERNEL);
	if (skb)
		skb_reserve(skb, dev->skb_headroom);

	return skb;
}

static int port100_set_command_type(struct port100 *dev, u8 command_type)
{
	struct sk_buff *skb;
	struct sk_buff *resp;
	int rc;

	skb = port100_alloc_skb(dev, 1);
	if (!skb)
		return -ENOMEM;

	*skb_put(skb, sizeof(u8)) = command_type;

	resp = port100_send_cmd_sync(dev, PORT100_CMD_SET_COMMAND_TYPE, skb);
	if (IS_ERR(resp))
		return PTR_ERR(resp);

	rc = resp->data[0];

	dev_kfree_skb(resp);

	return rc;
}

static u64 port100_get_command_type_mask(struct port100 *dev)
{
	struct sk_buff *skb;
	struct sk_buff *resp;
	u64 mask;

	skb = port100_alloc_skb(dev, 0);
	if (!skb)
		return -ENOMEM;

	resp = port100_send_cmd_sync(dev, PORT100_CMD_GET_COMMAND_TYPE, skb);
	if (IS_ERR(resp))
		return PTR_ERR(resp);

	if (resp->len < 8)
		mask = 0;
	else
		mask = be64_to_cpu(*(__be64 *)resp->data);

	dev_kfree_skb(resp);

	return mask;
}

static u16 port100_get_firmware_version(struct port100 *dev)
{
	struct sk_buff *skb;
	struct sk_buff *resp;
	u16 fw_ver;

	skb = port100_alloc_skb(dev, 0);
	if (!skb)
		return 0;

	resp = port100_send_cmd_sync(dev, PORT100_CMD_GET_FIRMWARE_VERSION,
				     skb);
	if (IS_ERR(resp))
		return 0;

	fw_ver = le16_to_cpu(*(__le16 *)resp->data);

	dev_kfree_skb(resp);

	return fw_ver;
}

static int port100_switch_rf(struct nfc_digital_dev *ddev, bool on)
{
	return -EOPNOTSUPP;
}

static int port100_in_configure_hw(struct nfc_digital_dev *ddev, int type,
				   int param)
{
	return -EOPNOTSUPP;
}

static int port100_in_send_cmd(struct nfc_digital_dev *ddev,
			       struct sk_buff *skb, u16 _timeout,
			       nfc_digital_cmd_complete_t cb, void *arg)
{
	return -EOPNOTSUPP;
}

static int port100_tg_configure_hw(struct nfc_digital_dev *ddev, int type,
				   int param)
{
	return -EOPNOTSUPP;
}

static int port100_tg_send_cmd(struct nfc_digital_dev *ddev,
			       struct sk_buff *skb, u16 timeout,
			       nfc_digital_cmd_complete_t cb, void *arg)
{
	return -EOPNOTSUPP;
}

static int port100_listen_mdaa(struct nfc_digital_dev *ddev,
			       struct digital_tg_mdaa_params *params,
			       u16 timeout,
			       nfc_digital_cmd_complete_t cb, void *arg)
{
	return -EOPNOTSUPP;
}

static int port100_listen(struct nfc_digital_dev *ddev, u16 timeout,
			  nfc_digital_cmd_complete_t cb, void *arg)
{
	return -EOPNOTSUPP;
}

static struct nfc_digital_ops port100_digital_ops = {
	.in_configure_hw = port100_in_configure_hw,
	.in_send_cmd = port100_in_send_cmd,

	.tg_listen_mdaa = port100_listen_mdaa,
	.tg_listen = port100_listen,
	.tg_configure_hw = port100_tg_configure_hw,
	.tg_send_cmd = port100_tg_send_cmd,

	.switch_rf = port100_switch_rf,
	.abort_cmd = port100_abort_cmd,
};

static const struct usb_device_id port100_table[] = {
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE,
	  .idVendor		= SONY_VENDOR_ID,
	  .idProduct		= RCS380_PRODUCT_ID,
	},
	{ }
};
MODULE_DEVICE_TABLE(usb, port100_table);

static int port100_probe(struct usb_interface *interface,
			 const struct usb_device_id *id)
{
	struct port100 *dev;
	int rc;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int in_endpoint;
	int out_endpoint;
	u16 fw_version;
	u64 cmd_type_mask;
	int i;

	dev = devm_kzalloc(&interface->dev, sizeof(struct port100), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;
	usb_set_intfdata(interface, dev);

	in_endpoint = out_endpoint = 0;
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

	dev->in_urb = usb_alloc_urb(0, GFP_KERNEL);
	dev->out_urb = usb_alloc_urb(0, GFP_KERNEL);

	if (!dev->in_urb || !dev->out_urb) {
		nfc_err(&interface->dev, "Could not allocate USB URBs\n");
		rc = -ENOMEM;
		goto error;
	}

	usb_fill_bulk_urb(dev->in_urb, dev->udev,
			  usb_rcvbulkpipe(dev->udev, in_endpoint),
			  NULL, 0, NULL, dev);
	usb_fill_bulk_urb(dev->out_urb, dev->udev,
			  usb_sndbulkpipe(dev->udev, out_endpoint),
			  NULL, 0, port100_send_complete, dev);

	dev->skb_headroom = PORT100_FRAME_HEADER_LEN +
			    PORT100_COMM_RF_HEAD_MAX_LEN;
	dev->skb_tailroom = PORT100_FRAME_TAIL_LEN;

	INIT_WORK(&dev->cmd_complete_work, port100_wq_cmd_complete);

	/* The first thing to do with the Port-100 is to set the command type
	 * to be used. If supported we use command type 1. 0 otherwise.
	 */
	cmd_type_mask = port100_get_command_type_mask(dev);
	if (!cmd_type_mask) {
		nfc_err(&interface->dev,
			"Could not get supported command types.\n");
		rc = -ENODEV;
		goto error;
	}

	if (PORT100_CMD_TYPE_IS_SUPPORTED(cmd_type_mask, PORT100_CMD_TYPE_1))
		dev->cmd_type = PORT100_CMD_TYPE_1;
	else
		dev->cmd_type = PORT100_CMD_TYPE_0;

	rc = port100_set_command_type(dev, dev->cmd_type);
	if (rc) {
		nfc_err(&interface->dev,
			"The device does not support command type %u.\n",
			dev->cmd_type);
		goto error;
	}

	fw_version = port100_get_firmware_version(dev);
	if (!fw_version)
		nfc_err(&interface->dev,
			"Could not get device firmware version.\n");

	nfc_info(&interface->dev,
		 "Sony NFC Port-100 Series attached (firmware v%x.%02x)\n",
		 (fw_version & 0xFF00) >> 8, fw_version & 0xFF);

	dev->nfc_digital_dev = nfc_digital_allocate_device(&port100_digital_ops,
							   PORT100_PROTOCOLS,
							   PORT100_CAPABILITIES,
							   dev->skb_headroom,
							   dev->skb_tailroom);
	if (!dev->nfc_digital_dev) {
		nfc_err(&interface->dev,
			"Could not allocate nfc_digital_dev.\n");
		rc = -ENOMEM;
		goto error;
	}

	nfc_digital_set_parent_dev(dev->nfc_digital_dev, &interface->dev);
	nfc_digital_set_drvdata(dev->nfc_digital_dev, dev);

	rc = nfc_digital_register_device(dev->nfc_digital_dev);
	if (rc) {
		nfc_err(&interface->dev,
			"Could not register digital device.\n");
		goto free_nfc_dev;
	}

	return 0;

free_nfc_dev:
	nfc_digital_free_device(dev->nfc_digital_dev);

error:
	usb_free_urb(dev->in_urb);
	usb_free_urb(dev->out_urb);
	usb_put_dev(dev->udev);

	return rc;
}

static void port100_disconnect(struct usb_interface *interface)
{
	struct port100 *dev;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	nfc_digital_unregister_device(dev->nfc_digital_dev);
	nfc_digital_free_device(dev->nfc_digital_dev);

	usb_kill_urb(dev->in_urb);
	usb_kill_urb(dev->out_urb);

	usb_free_urb(dev->in_urb);
	usb_free_urb(dev->out_urb);

	kfree(dev->cmd);

	nfc_info(&interface->dev, "Sony Port-100 NFC device disconnected");
}

static struct usb_driver port100_driver = {
	.name =		"port100",
	.probe =	port100_probe,
	.disconnect =	port100_disconnect,
	.id_table =	port100_table,
};

module_usb_driver(port100_driver);

MODULE_DESCRIPTION("NFC Port-100 series usb driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
