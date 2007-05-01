/**
  * This file contains functions used in USB interface module.
  */
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/netdevice.h>
#include <linux/usb.h>

#include "host.h"
#include "sbi.h"
#include "decl.h"
#include "defs.h"
#include "dev.h"
#include "if_usb.h"

#define MESSAGE_HEADER_LEN	4

static const char usbdriver_name[] = "usb8xxx";

static struct usb_device_id if_usb_table[] = {
	/* Enter the device signature inside */
	{
		USB_DEVICE(USB8388_VID_1, USB8388_PID_1),
	},
	{
		USB_DEVICE(USB8388_VID_2, USB8388_PID_2),
	},
	{}	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, if_usb_table);

static void if_usb_receive(struct urb *urb);
static void if_usb_receive_fwload(struct urb *urb);

/**
 *  @brief  call back function to handle the status of the URB
 *  @param urb 		pointer to urb structure
 *  @return 	   	N/A
 */
static void if_usb_write_bulk_callback(struct urb *urb)
{
	wlan_private *priv = (wlan_private *) (urb->context);
	wlan_adapter *adapter = priv->adapter;
	struct net_device *dev = priv->wlan_dev.netdev;

	/* handle the transmission complete validations */

	if (urb->status != 0) {
		/* print the failure status number for debug */
		lbs_pr_info("URB in failure status\n");
	} else {
		lbs_dev_dbg(2, &urb->dev->dev, "URB status is successfull\n");
		lbs_dev_dbg(2, &urb->dev->dev, "Actual length transmitted %d\n",
		       urb->actual_length);
		priv->wlan_dev.dnld_sent = DNLD_RES_RECEIVED;
		/* Wake main thread if commands are pending */
		if (!adapter->cur_cmd)
			wake_up_interruptible(&priv->mainthread.waitq);
		if ((adapter->connect_status == libertas_connected))
			netif_wake_queue(dev);
	}

	return;
}

/**
 *  @brief  free tx/rx urb, skb and rx buffer
 *  @param cardp	pointer usb_card_rec
 *  @return 	   	N/A
 */
void if_usb_free(struct usb_card_rec *cardp)
{
	ENTER();

	/* Unlink tx & rx urb */
	usb_kill_urb(cardp->tx_urb);
	usb_kill_urb(cardp->rx_urb);

	usb_free_urb(cardp->tx_urb);
	cardp->tx_urb = NULL;

	usb_free_urb(cardp->rx_urb);
	cardp->rx_urb = NULL;

	kfree(cardp->bulk_out_buffer);
	cardp->bulk_out_buffer = NULL;

	LEAVE();
	return;
}

/**
 *  @brief sets the configuration values
 *  @param ifnum	interface number
 *  @param id		pointer to usb_device_id
 *  @return 	   	0 on success, error code on failure
 */
static int if_usb_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct usb_device *udev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	wlan_private *pwlanpriv;
	struct usb_card_rec *usb_cardp;
	int i;

	udev = interface_to_usbdev(intf);

	usb_cardp = kzalloc(sizeof(struct usb_card_rec), GFP_KERNEL);
	if (!usb_cardp) {
		lbs_pr_err("Out of memory allocating private data.\n");
		goto error;
	}

	usb_cardp->udev = udev;
	iface_desc = intf->cur_altsetting;

	lbs_dev_dbg(1, &udev->dev, "bcdUSB = 0x%X bDeviceClass = 0x%X"
	       " bDeviceSubClass = 0x%X, bDeviceProtocol = 0x%X\n",
	       udev->descriptor.bcdUSB,
	       udev->descriptor.bDeviceClass,
	       udev->descriptor.bDeviceSubClass,
	       udev->descriptor.bDeviceProtocol);

	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
		    && ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
			USB_ENDPOINT_XFER_BULK)) {
			/* we found a bulk in endpoint */
			lbs_dev_dbg(1, &udev->dev, "Bulk in size is %d\n",
			       endpoint->wMaxPacketSize);
			if (!
			    (usb_cardp->rx_urb =
			     usb_alloc_urb(0, GFP_KERNEL))) {
				lbs_dev_dbg(1, &udev->dev,
				       "Rx URB allocation failed\n");
				goto dealloc;
			}
			usb_cardp->rx_urb_recall = 0;

			usb_cardp->bulk_in_size =
			    endpoint->wMaxPacketSize;
			usb_cardp->bulk_in_endpointAddr =
			    (endpoint->
			     bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
			lbs_dev_dbg(1, &udev->dev, "in_endpoint = %d\n",
			       endpoint->bEndpointAddress);
		}

		if (((endpoint->
		      bEndpointAddress & USB_ENDPOINT_DIR_MASK) ==
		     USB_DIR_OUT)
		    && ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
			USB_ENDPOINT_XFER_BULK)) {
			/* We found bulk out endpoint */
			if (!
			    (usb_cardp->tx_urb =
			     usb_alloc_urb(0, GFP_KERNEL))) {
				lbs_dev_dbg(1,&udev->dev,
				       "Tx URB allocation failed\n");
				goto dealloc;
			}

			usb_cardp->bulk_out_size =
			    endpoint->wMaxPacketSize;
			lbs_dev_dbg(1, &udev->dev,
				    "Bulk out size is %d\n",
				    endpoint->wMaxPacketSize);
			usb_cardp->bulk_out_endpointAddr =
			    endpoint->bEndpointAddress;
			lbs_dev_dbg(1, &udev->dev, "out_endpoint = %d\n",
				    endpoint->bEndpointAddress);
			usb_cardp->bulk_out_buffer =
			    kmalloc(MRVDRV_ETH_TX_PACKET_BUFFER_SIZE,
				    GFP_KERNEL);

			if (!usb_cardp->bulk_out_buffer) {
				lbs_dev_dbg(1, &udev->dev,
				       "Could not allocate buffer\n");
				goto dealloc;
			}
		}
	}


	/* At this point wlan_add_card() will be called.  Don't worry
	 * about keeping pwlanpriv around since it will be set on our
	 * usb device data in -> add() -> libertas_sbi_register_dev().
	 */
	if (!(pwlanpriv = wlan_add_card(usb_cardp)))
		goto dealloc;

	usb_get_dev(udev);
	usb_set_intfdata(intf, usb_cardp);

	/*
	 * return card structure, which can be got back in the
	 * diconnect function as the ptr
	 * argument.
	 */
	return 0;

dealloc:
	if_usb_free(usb_cardp);

error:
	return -ENOMEM;
}

/**
 *  @brief free resource and cleanup
 *  @param udev		pointer to usb_device
 *  @param ptr		pointer to usb_cardp
 *  @return 	   	N/A
 */
static void if_usb_disconnect(struct usb_interface *intf)
{
	struct usb_card_rec *cardp = usb_get_intfdata(intf);
	wlan_private *priv = (wlan_private *) cardp->priv;
	wlan_adapter *adapter = NULL;

	adapter = priv->adapter;

	/*
	 * Update Surprise removed to TRUE
	 */
	adapter->surpriseremoved = 1;

	/* card is removed and we can call wlan_remove_card */
	lbs_dev_dbg(1, &cardp->udev->dev, "call remove card\n");
	wlan_remove_card(cardp);

	/* Unlink and free urb */
	if_usb_free(cardp);

	usb_set_intfdata(intf, NULL);
	usb_put_dev(interface_to_usbdev(intf));

	return;
}

/**
 *  @brief  This function download FW
 *  @param priv		pointer to wlan_private
 *  @return 	   	0
 */
static int if_prog_firmware(wlan_private * priv)
{
	struct usb_card_rec *cardp = priv->wlan_dev.card;
	struct FWData *fwdata;
	struct fwheader *fwheader;
	u8 *firmware = priv->firmware->data;

	fwdata = kmalloc(sizeof(struct FWData), GFP_ATOMIC);

	if (!fwdata)
		return -1;

	fwheader = &fwdata->fwheader;

	if (!cardp->CRC_OK) {
		cardp->totalbytes = cardp->fwlastblksent;
		cardp->fwseqnum = cardp->lastseqnum - 1;
	}

	lbs_dev_dbg(2, &cardp->udev->dev, "totalbytes = %d\n",
		    cardp->totalbytes);

	memcpy(fwheader, &firmware[cardp->totalbytes],
	       sizeof(struct fwheader));

	cardp->fwlastblksent = cardp->totalbytes;
	cardp->totalbytes += sizeof(struct fwheader);

	lbs_dev_dbg(2, &cardp->udev->dev,"Copy Data\n");
	memcpy(fwdata->data, &firmware[cardp->totalbytes],
	       fwdata->fwheader.datalength);

	lbs_dev_dbg(2, &cardp->udev->dev,
		    "Data length = %d\n", fwdata->fwheader.datalength);

	cardp->fwseqnum = cardp->fwseqnum + 1;

	fwdata->seqnum = cardp->fwseqnum;
	cardp->lastseqnum = fwdata->seqnum;
	cardp->totalbytes += fwdata->fwheader.datalength;

	if (fwheader->dnldcmd == FW_HAS_DATA_TO_RECV) {
		lbs_dev_dbg(2, &cardp->udev->dev, "There is data to follow\n");
		lbs_dev_dbg(2, &cardp->udev->dev,
			    "seqnum = %d totalbytes = %d\n", cardp->fwseqnum,
			    cardp->totalbytes);
		memcpy(cardp->bulk_out_buffer, fwheader, FW_DATA_XMIT_SIZE);
		usb_tx_block(priv, cardp->bulk_out_buffer, FW_DATA_XMIT_SIZE);

	} else if (fwdata->fwheader.dnldcmd == FW_HAS_LAST_BLOCK) {
		lbs_dev_dbg(2, &cardp->udev->dev,
			    "Host has finished FW downloading\n");
		lbs_dev_dbg(2, &cardp->udev->dev,
			    "Donwloading FW JUMP BLOCK\n");
		memcpy(cardp->bulk_out_buffer, fwheader, FW_DATA_XMIT_SIZE);
		usb_tx_block(priv, cardp->bulk_out_buffer, FW_DATA_XMIT_SIZE);
		cardp->fwfinalblk = 1;
	}

	lbs_dev_dbg(2, &cardp->udev->dev,
		    "The firmware download is done size is %d\n",
		    cardp->totalbytes);

	kfree(fwdata);

	return 0;
}

static int libertas_do_reset(wlan_private *priv)
{
	int ret;
	struct usb_card_rec *cardp = priv->wlan_dev.card;

	ret = usb_reset_device(cardp->udev);
	if (!ret) {
		msleep(10);
		reset_device(priv);
		msleep(10);
	}
	return ret;
}

/**
 *  @brief This function transfer the data to the device.
 *  @param priv 	pointer to wlan_private
 *  @param payload	pointer to payload data
 *  @param nb		data length
 *  @return 	   	0 or -1
 */
int usb_tx_block(wlan_private * priv, u8 * payload, u16 nb)
{
	/* pointer to card structure */
	struct usb_card_rec *cardp = priv->wlan_dev.card;
	int ret = -1;

	/* check if device is removed */
	if (priv->adapter->surpriseremoved) {
		lbs_dev_dbg(1, &cardp->udev->dev, "Device removed\n");
		goto tx_ret;
	}

	usb_fill_bulk_urb(cardp->tx_urb, cardp->udev,
			  usb_sndbulkpipe(cardp->udev,
					  cardp->bulk_out_endpointAddr),
			  payload, nb, if_usb_write_bulk_callback, priv);

	cardp->tx_urb->transfer_flags |= URB_ZERO_PACKET;

	if ((ret = usb_submit_urb(cardp->tx_urb, GFP_ATOMIC))) {
		/*  transfer failed */
		lbs_dev_dbg(1, &cardp->udev->dev, "usb_submit_urb failed\n");
		ret = -1;
	} else {
		lbs_dev_dbg(2, &cardp->udev->dev, "usb_submit_urb success\n");
		ret = 0;
	}

tx_ret:
	return ret;
}

static int __if_usb_submit_rx_urb(wlan_private * priv,
				  void (*callbackfn)
				  (struct urb *urb))
{
	struct usb_card_rec *cardp = priv->wlan_dev.card;
	struct sk_buff *skb;
	struct read_cb_info *rinfo = &cardp->rinfo;
	int ret = -1;

	if (!(skb = dev_alloc_skb(MRVDRV_ETH_RX_PACKET_BUFFER_SIZE))) {
		lbs_pr_err("No free skb\n");
		goto rx_ret;
	}

	rinfo->skb = skb;

	/* Fill the receive configuration URB and initialise the Rx call back */
	usb_fill_bulk_urb(cardp->rx_urb, cardp->udev,
			  usb_rcvbulkpipe(cardp->udev,
					  cardp->bulk_in_endpointAddr),
			  skb->tail + IPFIELD_ALIGN_OFFSET,
			  MRVDRV_ETH_RX_PACKET_BUFFER_SIZE, callbackfn,
			  rinfo);

	cardp->rx_urb->transfer_flags |= URB_ZERO_PACKET;

	lbs_dev_dbg(2, &cardp->udev->dev, "Pointer for rx_urb %p\n", cardp->rx_urb);
	if ((ret = usb_submit_urb(cardp->rx_urb, GFP_ATOMIC))) {
		/* handle failure conditions */
		lbs_dev_dbg(1, &cardp->udev->dev, "Submit Rx URB failed\n");
		ret = -1;
	} else {
		lbs_dev_dbg(2, &cardp->udev->dev, "Submit Rx URB success\n");
		ret = 0;
	}

rx_ret:
	return ret;
}

static inline int if_usb_submit_rx_urb_fwload(wlan_private * priv)
{
	return __if_usb_submit_rx_urb(priv, &if_usb_receive_fwload);
}

static inline int if_usb_submit_rx_urb(wlan_private * priv)
{
	return __if_usb_submit_rx_urb(priv, &if_usb_receive);
}

static void if_usb_receive_fwload(struct urb *urb)
{
	struct read_cb_info *rinfo = (struct read_cb_info *)urb->context;
	wlan_private *priv = rinfo->priv;
	struct sk_buff *skb = rinfo->skb;
	struct usb_card_rec *cardp = (struct usb_card_rec *)priv->wlan_dev.card;
	struct fwsyncheader *syncfwheader;
	struct bootcmdrespStr bootcmdresp;

	if (urb->status) {
		lbs_dev_dbg(1, &cardp->udev->dev,
			    "URB status is failed during fw load\n");
		kfree_skb(skb);
		return;
	}

	if (cardp->bootcmdresp == 0) {
		memcpy (&bootcmdresp, skb->data + IPFIELD_ALIGN_OFFSET,
			sizeof(bootcmdresp));
		if (cardp->udev->descriptor.bcdDevice < 0x3106) {
			kfree_skb(skb);
			if_usb_submit_rx_urb_fwload(priv);
			cardp->bootcmdresp = 1;
			lbs_dev_dbg(1, &cardp->udev->dev,
				    "Received valid boot command response\n");
			return;
		}
		if (bootcmdresp.u32magicnumber != BOOT_CMD_MAGIC_NUMBER) {
			lbs_pr_info(
				"boot cmd response wrong magic number (0x%x)\n",
				bootcmdresp.u32magicnumber);
		} else if (bootcmdresp.u8cmd_tag != BOOT_CMD_FW_BY_USB) {
			lbs_pr_info(
				"boot cmd response cmd_tag error (%d)\n",
				bootcmdresp.u8cmd_tag);
		} else if (bootcmdresp.u8result != BOOT_CMD_RESP_OK) {
			lbs_pr_info(
				"boot cmd response result error (%d)\n",
				bootcmdresp.u8result);
		} else {
			cardp->bootcmdresp = 1;
			lbs_dev_dbg(1, &cardp->udev->dev,
				    "Received valid boot command response\n");
		}
		kfree_skb(skb);
		if_usb_submit_rx_urb_fwload(priv);
		return;
	}

	syncfwheader = kmalloc(sizeof(struct fwsyncheader), GFP_ATOMIC);
	if (!syncfwheader) {
		lbs_dev_dbg(1, &cardp->udev->dev, "Failure to allocate syncfwheader\n");
		kfree_skb(skb);
		return;
	}

	memcpy(syncfwheader, skb->data + IPFIELD_ALIGN_OFFSET,
			sizeof(struct fwsyncheader));

	if (!syncfwheader->cmd) {
		lbs_dev_dbg(2, &cardp->udev->dev,
			    "FW received Blk with correct CRC\n");
		lbs_dev_dbg(2, &cardp->udev->dev,
			    "FW received Blk seqnum = %d\n",
		       syncfwheader->seqnum);
		cardp->CRC_OK = 1;
	} else {
		lbs_dev_dbg(1, &cardp->udev->dev,
			    "FW received Blk with CRC error\n");
		cardp->CRC_OK = 0;
	}

	kfree_skb(skb);

	if (cardp->fwfinalblk) {
		cardp->fwdnldover = 1;
		goto exit;
	}

	if_prog_firmware(priv);

	if_usb_submit_rx_urb_fwload(priv);
exit:
	kfree(syncfwheader);

	return;

}

#define MRVDRV_MIN_PKT_LEN	30

static inline void process_cmdtypedata(int recvlength, struct sk_buff *skb,
				       struct usb_card_rec *cardp,
				       wlan_private *priv)
{
	if (recvlength > MRVDRV_ETH_RX_PACKET_BUFFER_SIZE +
	    MESSAGE_HEADER_LEN || recvlength < MRVDRV_MIN_PKT_LEN) {
		lbs_dev_dbg(1, &cardp->udev->dev,
			    "Packet length is Invalid\n");
		kfree_skb(skb);
		return;
	}

	skb_reserve(skb, IPFIELD_ALIGN_OFFSET);
	skb_put(skb, recvlength);
	skb_pull(skb, MESSAGE_HEADER_LEN);
	libertas_process_rxed_packet(priv, skb);
	priv->wlan_dev.upld_len = (recvlength - MESSAGE_HEADER_LEN);
}

static inline void process_cmdrequest(int recvlength, u8 *recvbuff,
				      struct sk_buff *skb,
				      struct usb_card_rec *cardp,
				      wlan_private *priv)
{
	u8 *cmdbuf;
	if (recvlength > MRVDRV_SIZE_OF_CMD_BUFFER) {
		lbs_dev_dbg(1, &cardp->udev->dev,
			    "The receive buffer is too large\n");
		kfree_skb(skb);
		return;
	}

	if (!in_interrupt())
		BUG();

	spin_lock(&priv->adapter->driver_lock);
	/* take care of cur_cmd = NULL case by reading the
	 * data to clear the interrupt */
	if (!priv->adapter->cur_cmd) {
		cmdbuf = priv->wlan_dev.upld_buf;
		priv->adapter->hisregcpy &= ~his_cmdupldrdy;
	} else
		cmdbuf = priv->adapter->cur_cmd->bufvirtualaddr;

	cardp->usb_int_cause |= his_cmdupldrdy;
	priv->wlan_dev.upld_len = (recvlength - MESSAGE_HEADER_LEN);
	memcpy(cmdbuf, recvbuff + MESSAGE_HEADER_LEN,
	       priv->wlan_dev.upld_len);

	kfree_skb(skb);
	libertas_interrupt(priv->wlan_dev.netdev);
	spin_unlock(&priv->adapter->driver_lock);

	lbs_dev_dbg(1, &cardp->udev->dev,
		    "Wake up main thread to handle cmd response\n");

	return;
}

/**
 *  @brief This function reads of the packet into the upload buff,
 *  wake up the main thread and initialise the Rx callack.
 *
 *  @param urb		pointer to struct urb
 *  @return 	   	N/A
 */
static void if_usb_receive(struct urb *urb)
{
	struct read_cb_info *rinfo = (struct read_cb_info *)urb->context;
	wlan_private *priv = rinfo->priv;
	struct sk_buff *skb = rinfo->skb;
	struct usb_card_rec *cardp = (struct usb_card_rec *)priv->wlan_dev.card;

	int recvlength = urb->actual_length;
	u8 *recvbuff = NULL;
	u32 recvtype;

	ENTER();

	if (recvlength) {
		if (urb->status) {
			lbs_dev_dbg(1, &cardp->udev->dev,
				    "URB status is failed\n");
			kfree_skb(skb);
			goto setup_for_next;
		}

		recvbuff = skb->data + IPFIELD_ALIGN_OFFSET;
		memcpy(&recvtype, recvbuff, sizeof(u32));
		lbs_dev_dbg(1, &cardp->udev->dev,
			    "Recv length = 0x%x\n", recvlength);
		lbs_dev_dbg(1, &cardp->udev->dev,
			    "Receive type = 0x%X\n", recvtype);
		recvtype = le32_to_cpu(recvtype);
		lbs_dev_dbg(1, &cardp->udev->dev,
			    "Receive type after = 0x%X\n", recvtype);
	} else if (urb->status)
		goto rx_exit;


	switch (recvtype) {
	case CMD_TYPE_DATA:
		process_cmdtypedata(recvlength, skb, cardp, priv);
		break;

	case CMD_TYPE_REQUEST:
		process_cmdrequest(recvlength, recvbuff, skb, cardp, priv);
		break;

	case CMD_TYPE_INDICATION:
		/* Event cause handling */
		spin_lock(&priv->adapter->driver_lock);
		cardp->usb_event_cause = *(u32 *) (recvbuff + MESSAGE_HEADER_LEN);
		lbs_dev_dbg(1, &cardp->udev->dev,"**EVENT** 0x%X\n",
			    cardp->usb_event_cause);
		if (cardp->usb_event_cause & 0xffff0000) {
			libertas_send_tx_feedback(priv);
			break;
		}
		cardp->usb_event_cause = le32_to_cpu(cardp->usb_event_cause) << 3;
		cardp->usb_int_cause |= his_cardevent;
		kfree_skb(skb);
		libertas_interrupt(priv->wlan_dev.netdev);
		spin_unlock(&priv->adapter->driver_lock);
		goto rx_exit;
	default:
		kfree_skb(skb);
		break;
	}

setup_for_next:
	if_usb_submit_rx_urb(priv);
rx_exit:
	LEAVE();
	return;
}

/**
 *  @brief This function downloads data to FW
 *  @param priv		pointer to wlan_private structure
 *  @param type		type of data
 *  @param buf		pointer to data buffer
 *  @param len		number of bytes
 *  @return 	   	0 or -1
 */
int libertas_sbi_host_to_card(wlan_private * priv, u8 type, u8 * payload, u16 nb)
{
	int ret = -1;
	u32 tmp;
	struct usb_card_rec *cardp = (struct usb_card_rec *)priv->wlan_dev.card;

	lbs_dev_dbg(1, &cardp->udev->dev,"*** type = %u\n", type);
	lbs_dev_dbg(1, &cardp->udev->dev,"size after = %d\n", nb);

	if (type == MVMS_CMD) {
		tmp = cpu_to_le32(CMD_TYPE_REQUEST);
		priv->wlan_dev.dnld_sent = DNLD_CMD_SENT;
		memcpy(cardp->bulk_out_buffer, (u8 *) & tmp,
		       MESSAGE_HEADER_LEN);

	} else {
		tmp = cpu_to_le32(CMD_TYPE_DATA);
		priv->wlan_dev.dnld_sent = DNLD_DATA_SENT;
		memcpy(cardp->bulk_out_buffer, (u8 *) & tmp,
		       MESSAGE_HEADER_LEN);
	}

	memcpy((cardp->bulk_out_buffer + MESSAGE_HEADER_LEN), payload, nb);

	ret =
	    usb_tx_block(priv, cardp->bulk_out_buffer, nb + MESSAGE_HEADER_LEN);

	return ret;
}

/* called with adapter->driver_lock held */
int libertas_sbi_get_int_status(wlan_private * priv, u8 * ireg)
{
	struct usb_card_rec *cardp = priv->wlan_dev.card;

	*ireg = cardp->usb_int_cause;
	cardp->usb_int_cause = 0;

	lbs_dev_dbg(1, &cardp->udev->dev,"Int cause is 0x%X\n", *ireg);

	return 0;
}

int libertas_sbi_read_event_cause(wlan_private * priv)
{
	struct usb_card_rec *cardp = priv->wlan_dev.card;
	priv->adapter->eventcause = cardp->usb_event_cause;
	/* Re-submit rx urb here to avoid event lost issue */
	if_usb_submit_rx_urb(priv);
	return 0;
}

int reset_device(wlan_private *priv)
{
	int ret;

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_reset,
				    cmd_act_halt, 0, 0, NULL);
	msleep_interruptible(10);

	return ret;
}

int libertas_sbi_unregister_dev(wlan_private * priv)
{
	int ret = 0;

	/* Need to send a Reset command to device before USB resources freed
	 * and wlan_remove_card() called, then device can handle FW download
	 * again.
	 */
	if (priv)
		reset_device(priv);

	return ret;
}


/**
 *  @brief  This function register usb device and initialize parameter
 *  @param		priv pointer to wlan_private
 *  @return		0 or -1
 */
int libertas_sbi_register_dev(wlan_private * priv)
{

	struct usb_card_rec *cardp = (struct usb_card_rec *)priv->wlan_dev.card;
	ENTER();

	cardp->priv = priv;
	cardp->eth_dev = priv->wlan_dev.netdev;
	priv->hotplug_device = &(cardp->udev->dev);

	SET_NETDEV_DEV(cardp->eth_dev, &(cardp->udev->dev));

	lbs_dev_dbg(1, &cardp->udev->dev, "udev pointer is at %p\n",
		    cardp->udev);

	LEAVE();
	return 0;
}



int libertas_sbi_prog_firmware(wlan_private * priv)
{
	struct usb_card_rec *cardp = priv->wlan_dev.card;
	int i = 0;
	static int reset_count = 10;

	ENTER();

	cardp->rinfo.priv = priv;

restart:
	if (if_usb_submit_rx_urb_fwload(priv) < 0) {
		lbs_dev_dbg(1, &cardp->udev->dev, "URB submission is failed\n");
		LEAVE();
		return -1;
	}

#ifdef SUPPORT_BOOT_COMMAND
	cardp->bootcmdresp = 0;
	do {
		int j = 0;
		i++;
		/* Issue Boot command = 1, Boot from Download-FW */
		if_usb_issue_boot_command(priv, BOOT_CMD_FW_BY_USB);
		/* wait for command response */
		do {
			j++;
			msleep_interruptible(100);
		} while (cardp->bootcmdresp == 0 && j < 10);
	} while (cardp->bootcmdresp == 0 && i < 5);

	if (cardp->bootcmdresp == 0) {
		if (--reset_count >= 0) {
			libertas_do_reset(priv);
			goto restart;
		}
		return -1;
	}
#endif

	i = 0;
	priv->adapter->fw_ready = 0;

	cardp->totalbytes = 0;
	cardp->fwlastblksent = 0;
	cardp->CRC_OK = 1;
	cardp->fwdnldover = 0;
	cardp->fwseqnum = -1;
	cardp->totalbytes = 0;
	cardp->fwfinalblk = 0;

	if_prog_firmware(priv);

	do {
		lbs_dev_dbg(1, &cardp->udev->dev,"Wlan sched timeout\n");
		i++;
		msleep_interruptible(100);
		if (priv->adapter->surpriseremoved || i >= 20)
			break;
	} while (!cardp->fwdnldover);

	if (!cardp->fwdnldover) {
		lbs_pr_info("failed to load fw, resetting device!\n");
		if (--reset_count >= 0) {
			libertas_do_reset(priv);
			goto restart;
		}

		lbs_pr_info("FW download failure, time = %d ms\n", i * 100);
		LEAVE();
		return -1;
	}

	if_usb_submit_rx_urb(priv);

	/* Delay 200 ms to waiting for the FW ready */
	msleep_interruptible(200);

	priv->adapter->fw_ready = 1;

	LEAVE();
	return 0;
}

/**
 *  @brief Given a usb_card_rec return its wlan_private
 *  @param card		pointer to a usb_card_rec
 *  @return 	   	pointer to wlan_private
 */
wlan_private *libertas_sbi_get_priv(void *card)
{
	struct usb_card_rec *cardp = card;
	return cardp->priv;
}

#ifdef ENABLE_PM
int libertas_sbi_suspend(wlan_private * priv)
{
	return 0;
}

int libertas_sbi_resume(wlan_private * priv)
{
	return 0;
}
#endif

#ifdef CONFIG_PM
static int if_usb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usb_card_rec *cardp = usb_get_intfdata(intf);
	wlan_private *priv = cardp->priv;

	ENTER();

	if (priv->adapter->psstate != PS_STATE_FULL_POWER)
		return -1;

	netif_device_detach(cardp->eth_dev);

	/* Unlink tx & rx urb */
	usb_kill_urb(cardp->tx_urb);
	usb_kill_urb(cardp->rx_urb);

	cardp->rx_urb_recall = 1;

	LEAVE();
	return 0;
}

static int if_usb_resume(struct usb_interface *intf)
{
	struct usb_card_rec *cardp = usb_get_intfdata(intf);

	ENTER();

	cardp->rx_urb_recall = 0;

	if_usb_submit_rx_urb(cardp->priv);

	netif_device_attach(cardp->eth_dev);

	LEAVE();
	return 0;
}
#else
#define if_usb_suspend NULL
#define if_usb_resume NULL
#endif

static struct usb_driver if_usb_driver = {
	/* driver name */
	.name = usbdriver_name,
	/* probe function name */
	.probe = if_usb_probe,
	/* disconnect function  name */
	.disconnect = if_usb_disconnect,
	/* device signature table */
	.id_table = if_usb_table,
	.suspend = if_usb_suspend,
	.resume = if_usb_resume,
};

/**
 *  @brief This function registers driver.
 *  @param add		pointer to add_card callback function
 *  @param remove	pointer to remove card callback function
 *  @param arg		pointer to call back function parameter
 *  @return 	   	dummy success variable
 */
int libertas_sbi_register(void)
{
	/*
	 * API registers the Marvell USB driver
	 * to the USB system
	 */
	usb_register(&if_usb_driver);

	/* Return success to wlan layer */
	return 0;
}

/**
 *  @brief This function removes usb driver.
 *  @return 	   	N/A
 */
void libertas_sbi_unregister(void)
{
	/* API unregisters the driver from USB subsystem */
	usb_deregister(&if_usb_driver);
	return;
}
