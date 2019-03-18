/*
 *  Copyright (C) 2008, cozybit Inc.
 *  Copyright (C) 2003-2006, Marvell International Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 */
#define DRV_NAME "lbtf_usb"

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "libertas_tf.h"
#include "if_usb.h"

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/usb.h>

#define INSANEDEBUG	0
#define lbtf_deb_usb2(...) do { if (INSANEDEBUG) lbtf_deb_usbd(__VA_ARGS__); } while (0)

#define MESSAGE_HEADER_LEN	4

static char *lbtf_fw_name = "lbtf_usb.bin";
module_param_named(fw_name, lbtf_fw_name, charp, 0644);

MODULE_FIRMWARE("lbtf_usb.bin");

static const struct usb_device_id if_usb_table[] = {
	/* Enter the device signature inside */
	{ USB_DEVICE(0x1286, 0x2001) },
	{ USB_DEVICE(0x05a3, 0x8388) },
	{}	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, if_usb_table);

static void if_usb_receive(struct urb *urb);
static void if_usb_receive_fwload(struct urb *urb);
static int if_usb_prog_firmware(struct lbtf_private *priv);
static int if_usb_host_to_card(struct lbtf_private *priv, uint8_t type,
			       uint8_t *payload, uint16_t nb);
static int usb_tx_block(struct if_usb_card *cardp, uint8_t *payload,
			uint16_t nb, u8 data);
static void if_usb_free(struct if_usb_card *cardp);
static int if_usb_submit_rx_urb(struct if_usb_card *cardp);
static int if_usb_reset_device(struct lbtf_private *priv);

/**
 *  if_usb_wrike_bulk_callback -  call back to handle URB status
 *
 *  @param urb		pointer to urb structure
 */
static void if_usb_write_bulk_callback(struct urb *urb)
{
	if (urb->status != 0) {
		/* print the failure status number for debug */
		pr_info("URB in failure status: %d\n", urb->status);
	} else {
		lbtf_deb_usb2(&urb->dev->dev, "URB status is successful\n");
		lbtf_deb_usb2(&urb->dev->dev, "Actual length transmitted %d\n",
			     urb->actual_length);
	}
}

/**
 *  if_usb_free - free tx/rx urb, skb and rx buffer
 *
 *  @param cardp	pointer if_usb_card
 */
static void if_usb_free(struct if_usb_card *cardp)
{
	lbtf_deb_enter(LBTF_DEB_USB);

	/* Unlink tx & rx urb */
	usb_kill_urb(cardp->tx_urb);
	usb_kill_urb(cardp->rx_urb);
	usb_kill_urb(cardp->cmd_urb);

	usb_free_urb(cardp->tx_urb);
	cardp->tx_urb = NULL;

	usb_free_urb(cardp->rx_urb);
	cardp->rx_urb = NULL;

	usb_free_urb(cardp->cmd_urb);
	cardp->cmd_urb = NULL;

	kfree(cardp->ep_out_buf);
	cardp->ep_out_buf = NULL;

	lbtf_deb_leave(LBTF_DEB_USB);
}

static void if_usb_setup_firmware(struct lbtf_private *priv)
{
	struct if_usb_card *cardp = priv->card;
	struct cmd_ds_set_boot2_ver b2_cmd;

	lbtf_deb_enter(LBTF_DEB_USB);

	if_usb_submit_rx_urb(cardp);
	b2_cmd.hdr.size = cpu_to_le16(sizeof(b2_cmd));
	b2_cmd.action = 0;
	b2_cmd.version = cardp->boot2_version;

	if (lbtf_cmd_with_response(priv, CMD_SET_BOOT2_VER, &b2_cmd))
		lbtf_deb_usb("Setting boot2 version failed\n");

	lbtf_deb_leave(LBTF_DEB_USB);
}

static void if_usb_fw_timeo(struct timer_list *t)
{
	struct if_usb_card *cardp = from_timer(cardp, t, fw_timeout);

	lbtf_deb_enter(LBTF_DEB_USB);
	if (!cardp->fwdnldover) {
		/* Download timed out */
		cardp->priv->surpriseremoved = 1;
		pr_err("Download timed out\n");
	} else {
		lbtf_deb_usb("Download complete, no event. Assuming success\n");
	}
	wake_up(&cardp->fw_wq);
	lbtf_deb_leave(LBTF_DEB_USB);
}

static const struct lbtf_ops if_usb_ops = {
	.hw_host_to_card = if_usb_host_to_card,
	.hw_prog_firmware = if_usb_prog_firmware,
	.hw_reset_device = if_usb_reset_device,
};

/**
 *  if_usb_probe - sets the configuration values
 *
 *  @ifnum	interface number
 *  @id		pointer to usb_device_id
 *
 *  Returns: 0 on success, error code on failure
 */
static int if_usb_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct usb_device *udev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	struct lbtf_private *priv;
	struct if_usb_card *cardp;
	int i;

	lbtf_deb_enter(LBTF_DEB_USB);
	udev = interface_to_usbdev(intf);

	cardp = kzalloc(sizeof(struct if_usb_card), GFP_KERNEL);
	if (!cardp)
		goto error;

	timer_setup(&cardp->fw_timeout, if_usb_fw_timeo, 0);
	init_waitqueue_head(&cardp->fw_wq);

	cardp->udev = udev;
	iface_desc = intf->cur_altsetting;

	lbtf_deb_usbd(&udev->dev, "bcdUSB = 0x%X bDeviceClass = 0x%X"
		     " bDeviceSubClass = 0x%X, bDeviceProtocol = 0x%X\n",
		     le16_to_cpu(udev->descriptor.bcdUSB),
		     udev->descriptor.bDeviceClass,
		     udev->descriptor.bDeviceSubClass,
		     udev->descriptor.bDeviceProtocol);

	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		if (usb_endpoint_is_bulk_in(endpoint)) {
			cardp->ep_in_size =
				le16_to_cpu(endpoint->wMaxPacketSize);
			cardp->ep_in = usb_endpoint_num(endpoint);

			lbtf_deb_usbd(&udev->dev, "in_endpoint = %d\n",
				cardp->ep_in);
			lbtf_deb_usbd(&udev->dev, "Bulk in size is %d\n",
				cardp->ep_in_size);
		} else if (usb_endpoint_is_bulk_out(endpoint)) {
			cardp->ep_out_size =
				le16_to_cpu(endpoint->wMaxPacketSize);
			cardp->ep_out = usb_endpoint_num(endpoint);

			lbtf_deb_usbd(&udev->dev, "out_endpoint = %d\n",
				cardp->ep_out);
			lbtf_deb_usbd(&udev->dev, "Bulk out size is %d\n",
				cardp->ep_out_size);
		}
	}
	if (!cardp->ep_out_size || !cardp->ep_in_size) {
		lbtf_deb_usbd(&udev->dev, "Endpoints not found\n");
		/* Endpoints not found */
		goto dealloc;
	}

	cardp->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!cardp->rx_urb)
		goto dealloc;

	cardp->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!cardp->tx_urb)
		goto dealloc;

	cardp->cmd_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!cardp->cmd_urb)
		goto dealloc;

	cardp->ep_out_buf = kmalloc(MRVDRV_ETH_TX_PACKET_BUFFER_SIZE,
				    GFP_KERNEL);
	if (!cardp->ep_out_buf) {
		lbtf_deb_usbd(&udev->dev, "Could not allocate buffer\n");
		goto dealloc;
	}

	cardp->boot2_version = udev->descriptor.bcdDevice;
	priv = lbtf_add_card(cardp, &udev->dev, &if_usb_ops);
	if (!priv)
		goto dealloc;

	usb_get_dev(udev);
	usb_set_intfdata(intf, cardp);

	return 0;

dealloc:
	if_usb_free(cardp);
error:
lbtf_deb_leave(LBTF_DEB_MAIN);
	return -ENOMEM;
}

/**
 *  if_usb_disconnect -  free resource and cleanup
 *
 *  @intf	USB interface structure
 */
static void if_usb_disconnect(struct usb_interface *intf)
{
	struct if_usb_card *cardp = usb_get_intfdata(intf);
	struct lbtf_private *priv = cardp->priv;

	lbtf_deb_enter(LBTF_DEB_MAIN);

	if_usb_reset_device(priv);

	if (priv)
		lbtf_remove_card(priv);

	/* Unlink and free urb */
	if_usb_free(cardp);

	usb_set_intfdata(intf, NULL);
	usb_put_dev(interface_to_usbdev(intf));

	lbtf_deb_leave(LBTF_DEB_MAIN);
}

/**
 *  if_usb_send_fw_pkt -  This function downloads the FW
 *
 *  @priv	pointer to struct lbtf_private
 *
 *  Returns: 0
 */
static int if_usb_send_fw_pkt(struct if_usb_card *cardp)
{
	struct fwdata *fwdata = cardp->ep_out_buf;
	u8 *firmware = (u8 *) cardp->fw->data;

	lbtf_deb_enter(LBTF_DEB_FW);

	/* If we got a CRC failure on the last block, back
	   up and retry it */
	if (!cardp->CRC_OK) {
		cardp->totalbytes = cardp->fwlastblksent;
		cardp->fwseqnum--;
	}

	lbtf_deb_usb2(&cardp->udev->dev, "totalbytes = %d\n",
		     cardp->totalbytes);

	/* struct fwdata (which we sent to the card) has an
	   extra __le32 field in between the header and the data,
	   which is not in the struct fwheader in the actual
	   firmware binary. Insert the seqnum in the middle... */
	memcpy(&fwdata->hdr, &firmware[cardp->totalbytes],
	       sizeof(struct fwheader));

	cardp->fwlastblksent = cardp->totalbytes;
	cardp->totalbytes += sizeof(struct fwheader);

	memcpy(fwdata->data, &firmware[cardp->totalbytes],
	       le32_to_cpu(fwdata->hdr.datalength));

	lbtf_deb_usb2(&cardp->udev->dev, "Data length = %d\n",
		     le32_to_cpu(fwdata->hdr.datalength));

	fwdata->seqnum = cpu_to_le32(++cardp->fwseqnum);
	cardp->totalbytes += le32_to_cpu(fwdata->hdr.datalength);

	usb_tx_block(cardp, cardp->ep_out_buf, sizeof(struct fwdata) +
		     le32_to_cpu(fwdata->hdr.datalength), 0);

	if (fwdata->hdr.dnldcmd == cpu_to_le32(FW_HAS_DATA_TO_RECV)) {
		lbtf_deb_usb2(&cardp->udev->dev, "There are data to follow\n");
		lbtf_deb_usb2(&cardp->udev->dev,
			"seqnum = %d totalbytes = %d\n",
			cardp->fwseqnum, cardp->totalbytes);
	} else if (fwdata->hdr.dnldcmd == cpu_to_le32(FW_HAS_LAST_BLOCK)) {
		lbtf_deb_usb2(&cardp->udev->dev,
			"Host has finished FW downloading\n");
		lbtf_deb_usb2(&cardp->udev->dev, "Donwloading FW JUMP BLOCK\n");

		/* Host has finished FW downloading
		 * Donwloading FW JUMP BLOCK
		 */
		cardp->fwfinalblk = 1;
	}

	lbtf_deb_usb2(&cardp->udev->dev, "Firmware download done; size %d\n",
		     cardp->totalbytes);

	lbtf_deb_leave(LBTF_DEB_FW);
	return 0;
}

static int if_usb_reset_device(struct lbtf_private *priv)
{
	struct if_usb_card *cardp = priv->card;
	struct cmd_ds_802_11_reset *cmd = cardp->ep_out_buf + 4;
	int ret;

	lbtf_deb_enter(LBTF_DEB_USB);

	*(__le32 *)cardp->ep_out_buf = cpu_to_le32(CMD_TYPE_REQUEST);

	cmd->hdr.command = cpu_to_le16(CMD_802_11_RESET);
	cmd->hdr.size = cpu_to_le16(sizeof(struct cmd_ds_802_11_reset));
	cmd->hdr.result = cpu_to_le16(0);
	cmd->hdr.seqnum = cpu_to_le16(0x5a5a);
	cmd->action = cpu_to_le16(CMD_ACT_HALT);
	usb_tx_block(cardp, cardp->ep_out_buf,
		     4 + sizeof(struct cmd_ds_802_11_reset), 0);

	msleep(100);
	ret = usb_reset_device(cardp->udev);
	msleep(100);

	lbtf_deb_leave_args(LBTF_DEB_USB, "ret %d", ret);

	return ret;
}

/**
 *  usb_tx_block - transfer data to the device
 *
 *  @priv	pointer to struct lbtf_private
 *  @payload	pointer to payload data
 *  @nb		data length
 *  @data	non-zero for data, zero for commands
 *
 *  Returns: 0 on success, nonzero otherwise.
 */
static int usb_tx_block(struct if_usb_card *cardp, uint8_t *payload,
			uint16_t nb, u8 data)
{
	int ret = -1;
	struct urb *urb;

	lbtf_deb_enter(LBTF_DEB_USB);
	/* check if device is removed */
	if (cardp->priv->surpriseremoved) {
		lbtf_deb_usbd(&cardp->udev->dev, "Device removed\n");
		goto tx_ret;
	}

	if (data)
		urb = cardp->tx_urb;
	else
		urb = cardp->cmd_urb;

	usb_fill_bulk_urb(urb, cardp->udev,
			  usb_sndbulkpipe(cardp->udev,
					  cardp->ep_out),
			  payload, nb, if_usb_write_bulk_callback, cardp);

	urb->transfer_flags |= URB_ZERO_PACKET;

	if (usb_submit_urb(urb, GFP_ATOMIC)) {
		lbtf_deb_usbd(&cardp->udev->dev,
			"usb_submit_urb failed: %d\n", ret);
		goto tx_ret;
	}

	lbtf_deb_usb2(&cardp->udev->dev, "usb_submit_urb success\n");

	ret = 0;

tx_ret:
	lbtf_deb_leave(LBTF_DEB_USB);
	return ret;
}

static int __if_usb_submit_rx_urb(struct if_usb_card *cardp,
				  void (*callbackfn)(struct urb *urb))
{
	struct sk_buff *skb;
	int ret = -1;

	lbtf_deb_enter(LBTF_DEB_USB);

	skb = dev_alloc_skb(MRVDRV_ETH_RX_PACKET_BUFFER_SIZE);
	if (!skb) {
		pr_err("No free skb\n");
		lbtf_deb_leave(LBTF_DEB_USB);
		return -1;
	}

	cardp->rx_skb = skb;

	/* Fill the receive configuration URB and initialise the Rx call back */
	usb_fill_bulk_urb(cardp->rx_urb, cardp->udev,
			  usb_rcvbulkpipe(cardp->udev, cardp->ep_in),
			  skb_tail_pointer(skb),
			  MRVDRV_ETH_RX_PACKET_BUFFER_SIZE, callbackfn, cardp);

	lbtf_deb_usb2(&cardp->udev->dev, "Pointer for rx_urb %p\n",
		cardp->rx_urb);
	ret = usb_submit_urb(cardp->rx_urb, GFP_ATOMIC);
	if (ret) {
		lbtf_deb_usbd(&cardp->udev->dev,
			"Submit Rx URB failed: %d\n", ret);
		kfree_skb(skb);
		cardp->rx_skb = NULL;
		lbtf_deb_leave(LBTF_DEB_USB);
		return -1;
	} else {
		lbtf_deb_usb2(&cardp->udev->dev, "Submit Rx URB success\n");
		lbtf_deb_leave(LBTF_DEB_USB);
		return 0;
	}
}

static int if_usb_submit_rx_urb_fwload(struct if_usb_card *cardp)
{
	return __if_usb_submit_rx_urb(cardp, &if_usb_receive_fwload);
}

static int if_usb_submit_rx_urb(struct if_usb_card *cardp)
{
	return __if_usb_submit_rx_urb(cardp, &if_usb_receive);
}

static void if_usb_receive_fwload(struct urb *urb)
{
	struct if_usb_card *cardp = urb->context;
	struct sk_buff *skb = cardp->rx_skb;
	struct fwsyncheader *syncfwheader;
	struct bootcmdresp bcmdresp;

	lbtf_deb_enter(LBTF_DEB_USB);
	if (urb->status) {
		lbtf_deb_usbd(&cardp->udev->dev,
			     "URB status is failed during fw load\n");
		kfree_skb(skb);
		lbtf_deb_leave(LBTF_DEB_USB);
		return;
	}

	if (cardp->fwdnldover) {
		__le32 *tmp = (__le32 *)(skb->data);

		if (tmp[0] == cpu_to_le32(CMD_TYPE_INDICATION) &&
		    tmp[1] == cpu_to_le32(MACREG_INT_CODE_FIRMWARE_READY)) {
			/* Firmware ready event received */
			pr_info("Firmware ready event received\n");
			wake_up(&cardp->fw_wq);
		} else {
			lbtf_deb_usb("Waiting for confirmation; got %x %x\n",
				    le32_to_cpu(tmp[0]), le32_to_cpu(tmp[1]));
			if_usb_submit_rx_urb_fwload(cardp);
		}
		kfree_skb(skb);
		lbtf_deb_leave(LBTF_DEB_USB);
		return;
	}
	if (cardp->bootcmdresp <= 0) {
		memcpy(&bcmdresp, skb->data, sizeof(bcmdresp));

		if (le16_to_cpu(cardp->udev->descriptor.bcdDevice) < 0x3106) {
			kfree_skb(skb);
			if_usb_submit_rx_urb_fwload(cardp);
			cardp->bootcmdresp = 1;
			/* Received valid boot command response */
			lbtf_deb_usbd(&cardp->udev->dev,
				     "Received valid boot command response\n");
			lbtf_deb_leave(LBTF_DEB_USB);
			return;
		}
		if (bcmdresp.magic != cpu_to_le32(BOOT_CMD_MAGIC_NUMBER)) {
			if (bcmdresp.magic == cpu_to_le32(CMD_TYPE_REQUEST) ||
			    bcmdresp.magic == cpu_to_le32(CMD_TYPE_DATA) ||
			    bcmdresp.magic == cpu_to_le32(CMD_TYPE_INDICATION)) {
				if (!cardp->bootcmdresp)
					pr_info("Firmware already seems alive; resetting\n");
				cardp->bootcmdresp = -1;
			} else {
				pr_info("boot cmd response wrong magic number (0x%x)\n",
					    le32_to_cpu(bcmdresp.magic));
			}
		} else if (bcmdresp.cmd != BOOT_CMD_FW_BY_USB) {
			pr_info("boot cmd response cmd_tag error (%d)\n",
				bcmdresp.cmd);
		} else if (bcmdresp.result != BOOT_CMD_RESP_OK) {
			pr_info("boot cmd response result error (%d)\n",
				bcmdresp.result);
		} else {
			cardp->bootcmdresp = 1;
			lbtf_deb_usbd(&cardp->udev->dev,
				"Received valid boot command response\n");
		}

		kfree_skb(skb);
		if_usb_submit_rx_urb_fwload(cardp);
		lbtf_deb_leave(LBTF_DEB_USB);
		return;
	}

	syncfwheader = kmemdup(skb->data, sizeof(struct fwsyncheader),
			       GFP_ATOMIC);
	if (!syncfwheader) {
		lbtf_deb_usbd(&cardp->udev->dev,
			"Failure to allocate syncfwheader\n");
		kfree_skb(skb);
		lbtf_deb_leave(LBTF_DEB_USB);
		return;
	}

	if (!syncfwheader->cmd) {
		lbtf_deb_usb2(&cardp->udev->dev,
			"FW received Blk with correct CRC\n");
		lbtf_deb_usb2(&cardp->udev->dev,
			"FW received Blk seqnum = %d\n",
			le32_to_cpu(syncfwheader->seqnum));
		cardp->CRC_OK = 1;
	} else {
		lbtf_deb_usbd(&cardp->udev->dev,
			"FW received Blk with CRC error\n");
		cardp->CRC_OK = 0;
	}

	kfree_skb(skb);

	/* reschedule timer for 200ms hence */
	mod_timer(&cardp->fw_timeout, jiffies + (HZ/5));

	if (cardp->fwfinalblk) {
		cardp->fwdnldover = 1;
		goto exit;
	}

	if_usb_send_fw_pkt(cardp);

 exit:
	if_usb_submit_rx_urb_fwload(cardp);

	kfree(syncfwheader);

	lbtf_deb_leave(LBTF_DEB_USB);
}

#define MRVDRV_MIN_PKT_LEN	30

static inline void process_cmdtypedata(int recvlength, struct sk_buff *skb,
				       struct if_usb_card *cardp,
				       struct lbtf_private *priv)
{
	if (recvlength > MRVDRV_ETH_RX_PACKET_BUFFER_SIZE + MESSAGE_HEADER_LEN
	    || recvlength < MRVDRV_MIN_PKT_LEN) {
		lbtf_deb_usbd(&cardp->udev->dev, "Packet length is Invalid\n");
		kfree_skb(skb);
		return;
	}

	skb_put(skb, recvlength);
	skb_pull(skb, MESSAGE_HEADER_LEN);
	lbtf_rx(priv, skb);
}

static inline void process_cmdrequest(int recvlength, uint8_t *recvbuff,
				      struct sk_buff *skb,
				      struct if_usb_card *cardp,
				      struct lbtf_private *priv)
{
	unsigned long flags;

	if (recvlength < MESSAGE_HEADER_LEN ||
	    recvlength > LBS_CMD_BUFFER_SIZE) {
		lbtf_deb_usbd(&cardp->udev->dev,
			     "The receive buffer is invalid: %d\n", recvlength);
		kfree_skb(skb);
		return;
	}

	spin_lock_irqsave(&priv->driver_lock, flags);
	memcpy(priv->cmd_resp_buff, recvbuff + MESSAGE_HEADER_LEN,
	       recvlength - MESSAGE_HEADER_LEN);
	kfree_skb(skb);
	lbtf_cmd_response_rx(priv);
	spin_unlock_irqrestore(&priv->driver_lock, flags);
}

/**
 *  if_usb_receive - read data received from the device.
 *
 *  @urb		pointer to struct urb
 */
static void if_usb_receive(struct urb *urb)
{
	struct if_usb_card *cardp = urb->context;
	struct sk_buff *skb = cardp->rx_skb;
	struct lbtf_private *priv = cardp->priv;
	int recvlength = urb->actual_length;
	uint8_t *recvbuff = NULL;
	uint32_t recvtype = 0;
	__le32 *pkt = (__le32 *) skb->data;

	lbtf_deb_enter(LBTF_DEB_USB);

	if (recvlength) {
		if (urb->status) {
			lbtf_deb_usbd(&cardp->udev->dev, "RX URB failed: %d\n",
				     urb->status);
			kfree_skb(skb);
			goto setup_for_next;
		}

		recvbuff = skb->data;
		recvtype = le32_to_cpu(pkt[0]);
		lbtf_deb_usbd(&cardp->udev->dev,
			    "Recv length = 0x%x, Recv type = 0x%X\n",
			    recvlength, recvtype);
	} else if (urb->status) {
		kfree_skb(skb);
		lbtf_deb_leave(LBTF_DEB_USB);
		return;
	}

	switch (recvtype) {
	case CMD_TYPE_DATA:
		process_cmdtypedata(recvlength, skb, cardp, priv);
		break;

	case CMD_TYPE_REQUEST:
		process_cmdrequest(recvlength, recvbuff, skb, cardp, priv);
		break;

	case CMD_TYPE_INDICATION:
	{
		/* Event cause handling */
		u32 event_cause = le32_to_cpu(pkt[1]);
		lbtf_deb_usbd(&cardp->udev->dev, "**EVENT** 0x%X\n",
			event_cause);

		/* Icky undocumented magic special case */
		if (event_cause & 0xffff0000) {
			u16 tmp;
			u8 retrycnt;
			u8 failure;

			tmp = event_cause >> 16;
			retrycnt = tmp & 0x00ff;
			failure = (tmp & 0xff00) >> 8;
			lbtf_send_tx_feedback(priv, retrycnt, failure);
		} else if (event_cause == LBTF_EVENT_BCN_SENT)
			lbtf_bcn_sent(priv);
		else
			lbtf_deb_usbd(&cardp->udev->dev,
			       "Unsupported notification %d received\n",
			       event_cause);
		kfree_skb(skb);
		break;
	}
	default:
		lbtf_deb_usbd(&cardp->udev->dev,
			"libertastf: unknown command type 0x%X\n", recvtype);
		kfree_skb(skb);
		break;
	}

setup_for_next:
	if_usb_submit_rx_urb(cardp);
	lbtf_deb_leave(LBTF_DEB_USB);
}

/**
 *  if_usb_host_to_card -  Download data to the device
 *
 *  @priv		pointer to struct lbtf_private structure
 *  @type		type of data
 *  @buf		pointer to data buffer
 *  @len		number of bytes
 *
 *  Returns: 0 on success, nonzero otherwise
 */
static int if_usb_host_to_card(struct lbtf_private *priv, uint8_t type,
			       uint8_t *payload, uint16_t nb)
{
	struct if_usb_card *cardp = priv->card;
	u8 data = 0;

	lbtf_deb_usbd(&cardp->udev->dev, "*** type = %u\n", type);
	lbtf_deb_usbd(&cardp->udev->dev, "size after = %d\n", nb);

	if (type == MVMS_CMD) {
		*(__le32 *)cardp->ep_out_buf = cpu_to_le32(CMD_TYPE_REQUEST);
	} else {
		*(__le32 *)cardp->ep_out_buf = cpu_to_le32(CMD_TYPE_DATA);
		data = 1;
	}

	memcpy((cardp->ep_out_buf + MESSAGE_HEADER_LEN), payload, nb);

	return usb_tx_block(cardp, cardp->ep_out_buf, nb + MESSAGE_HEADER_LEN,
			    data);
}

/**
 *  if_usb_issue_boot_command - Issue boot command to Boot2.
 *
 *  @ivalue   1 boots from FW by USB-Download, 2 boots from FW in EEPROM.
 *
 *  Returns: 0
 */
static int if_usb_issue_boot_command(struct if_usb_card *cardp, int ivalue)
{
	struct bootcmd *bootcmd = cardp->ep_out_buf;

	/* Prepare command */
	bootcmd->magic = cpu_to_le32(BOOT_CMD_MAGIC_NUMBER);
	bootcmd->cmd = ivalue;
	memset(bootcmd->pad, 0, sizeof(bootcmd->pad));

	/* Issue command */
	usb_tx_block(cardp, cardp->ep_out_buf, sizeof(*bootcmd), 0);

	return 0;
}


/**
 *  check_fwfile_format - Check the validity of Boot2/FW image.
 *
 *  @data	pointer to image
 *  @totlen	image length
 *
 *  Returns: 0 if the image is valid, nonzero otherwise.
 */
static int check_fwfile_format(const u8 *data, u32 totlen)
{
	u32 bincmd, exit;
	u32 blksize, offset, len;
	int ret;

	ret = 1;
	exit = len = 0;

	do {
		struct fwheader *fwh = (void *) data;

		bincmd = le32_to_cpu(fwh->dnldcmd);
		blksize = le32_to_cpu(fwh->datalength);
		switch (bincmd) {
		case FW_HAS_DATA_TO_RECV:
			offset = sizeof(struct fwheader) + blksize;
			data += offset;
			len += offset;
			if (len >= totlen)
				exit = 1;
			break;
		case FW_HAS_LAST_BLOCK:
			exit = 1;
			ret = 0;
			break;
		default:
			exit = 1;
			break;
		}
	} while (!exit);

	if (ret)
		pr_err("firmware file format check FAIL\n");
	else
		lbtf_deb_fw("firmware file format check PASS\n");

	return ret;
}


static int if_usb_prog_firmware(struct lbtf_private *priv)
{
	struct if_usb_card *cardp = priv->card;
	int i = 0;
	static int reset_count = 10;
	int ret = 0;

	lbtf_deb_enter(LBTF_DEB_USB);

	cardp->priv = priv;

	kernel_param_lock(THIS_MODULE);
	ret = request_firmware(&cardp->fw, lbtf_fw_name, &cardp->udev->dev);
	if (ret < 0) {
		pr_err("request_firmware() failed with %#x\n", ret);
		pr_err("firmware %s not found\n", lbtf_fw_name);
		kernel_param_unlock(THIS_MODULE);
		goto done;
	}
	kernel_param_unlock(THIS_MODULE);

	if (check_fwfile_format(cardp->fw->data, cardp->fw->size))
		goto release_fw;

restart:
	if (if_usb_submit_rx_urb_fwload(cardp) < 0) {
		lbtf_deb_usbd(&cardp->udev->dev, "URB submission is failed\n");
		ret = -1;
		goto release_fw;
	}

	cardp->bootcmdresp = 0;
	do {
		int j = 0;
		i++;
		/* Issue Boot command = 1, Boot from Download-FW */
		if_usb_issue_boot_command(cardp, BOOT_CMD_FW_BY_USB);
		/* wait for command response */
		do {
			j++;
			msleep_interruptible(100);
		} while (cardp->bootcmdresp == 0 && j < 10);
	} while (cardp->bootcmdresp == 0 && i < 5);

	if (cardp->bootcmdresp <= 0) {
		if (--reset_count >= 0) {
			if_usb_reset_device(priv);
			goto restart;
		}
		return -1;
	}

	i = 0;

	cardp->totalbytes = 0;
	cardp->fwlastblksent = 0;
	cardp->CRC_OK = 1;
	cardp->fwdnldover = 0;
	cardp->fwseqnum = -1;
	cardp->totalbytes = 0;
	cardp->fwfinalblk = 0;

	/* Send the first firmware packet... */
	if_usb_send_fw_pkt(cardp);

	/* ... and wait for the process to complete */
	wait_event_interruptible(cardp->fw_wq, cardp->priv->surpriseremoved ||
					       cardp->fwdnldover);

	del_timer_sync(&cardp->fw_timeout);
	usb_kill_urb(cardp->rx_urb);

	if (!cardp->fwdnldover) {
		pr_info("failed to load fw, resetting device!\n");
		if (--reset_count >= 0) {
			if_usb_reset_device(priv);
			goto restart;
		}

		pr_info("FW download failure, time = %d ms\n", i * 100);
		ret = -1;
		goto release_fw;
	}

 release_fw:
	release_firmware(cardp->fw);
	cardp->fw = NULL;

	if_usb_setup_firmware(cardp->priv);

 done:
	lbtf_deb_leave_args(LBTF_DEB_USB, "ret %d", ret);
	return ret;
}


#define if_usb_suspend NULL
#define if_usb_resume NULL

static struct usb_driver if_usb_driver = {
	.name = DRV_NAME,
	.probe = if_usb_probe,
	.disconnect = if_usb_disconnect,
	.id_table = if_usb_table,
	.suspend = if_usb_suspend,
	.resume = if_usb_resume,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(if_usb_driver);

MODULE_DESCRIPTION("8388 USB WLAN Thinfirm Driver");
MODULE_AUTHOR("Cozybit Inc.");
MODULE_LICENSE("GPL");
