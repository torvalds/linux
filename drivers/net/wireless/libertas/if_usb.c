/**
  * This file contains functions used in USB interface module.
  */
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <linux/netdevice.h>
#include <linux/list.h>
#include <linux/usb.h>

#define DRV_NAME "usb8xxx"

#include "host.h"
#include "decl.h"
#include "defs.h"
#include "dev.h"
#include "if_usb.h"

#define MESSAGE_HEADER_LEN	4

static const char usbdriver_name[] = "usb8xxx";
static u8 *default_fw_name = "usb8388.bin";

static char *libertas_fw_name = NULL;
module_param_named(fw_name, libertas_fw_name, charp, 0644);

/*
 * We need to send a RESET command to all USB devices before
 * we tear down the USB connection. Otherwise we would not
 * be able to re-init device the device if the module gets
 * loaded again. This is a list of all initialized USB devices,
 * for the reset code see if_usb_reset_device()
*/
static LIST_HEAD(usb_devices);

static struct usb_device_id if_usb_table[] = {
	/* Enter the device signature inside */
	{ USB_DEVICE(0x1286, 0x2001) },
	{ USB_DEVICE(0x05a3, 0x8388) },
	{}	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, if_usb_table);

static void if_usb_receive(struct urb *urb);
static void if_usb_receive_fwload(struct urb *urb);
static int if_usb_prog_firmware(struct usb_card_rec *cardp);
static int if_usb_host_to_card(wlan_private * priv, u8 type, u8 * payload, u16 nb);
static int if_usb_get_int_status(wlan_private * priv, u8 *);
static int if_usb_read_event_cause(wlan_private *);
static int usb_tx_block(struct usb_card_rec *cardp, u8 *payload, u16 nb);
static void if_usb_free(struct usb_card_rec *cardp);
static int if_usb_submit_rx_urb(struct usb_card_rec *cardp);
static int if_usb_reset_device(struct usb_card_rec *cardp);

/**
 *  @brief  call back function to handle the status of the URB
 *  @param urb 		pointer to urb structure
 *  @return 	   	N/A
 */
static void if_usb_write_bulk_callback(struct urb *urb)
{
	struct usb_card_rec *cardp = (struct usb_card_rec *) urb->context;

	/* handle the transmission complete validations */

	if (urb->status == 0) {
		wlan_private *priv = cardp->priv;

		/*
		lbs_deb_usbd(&urb->dev->dev, "URB status is successfull\n");
		lbs_deb_usbd(&urb->dev->dev, "Actual length transmitted %d\n",
		       urb->actual_length);
		*/

		/* Used for both firmware TX and regular TX.  priv isn't
		 * valid at firmware load time.
		 */
		if (priv) {
			wlan_adapter *adapter = priv->adapter;
			struct net_device *dev = priv->dev;

			priv->dnld_sent = DNLD_RES_RECEIVED;

			/* Wake main thread if commands are pending */
			if (!adapter->cur_cmd)
				wake_up_interruptible(&priv->waitq);

			if ((adapter->connect_status == LIBERTAS_CONNECTED)) {
				netif_wake_queue(dev);
				netif_wake_queue(priv->mesh_dev);
			}
		}
	} else {
		/* print the failure status number for debug */
		lbs_pr_info("URB in failure status: %d\n", urb->status);
	}

	return;
}

/**
 *  @brief  free tx/rx urb, skb and rx buffer
 *  @param cardp	pointer usb_card_rec
 *  @return 	   	N/A
 */
static void if_usb_free(struct usb_card_rec *cardp)
{
	lbs_deb_enter(LBS_DEB_USB);

	/* Unlink tx & rx urb */
	usb_kill_urb(cardp->tx_urb);
	usb_kill_urb(cardp->rx_urb);

	usb_free_urb(cardp->tx_urb);
	cardp->tx_urb = NULL;

	usb_free_urb(cardp->rx_urb);
	cardp->rx_urb = NULL;

	kfree(cardp->bulk_out_buffer);
	cardp->bulk_out_buffer = NULL;

	lbs_deb_leave(LBS_DEB_USB);
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
	wlan_private *priv;
	struct usb_card_rec *cardp;
	int i;

	udev = interface_to_usbdev(intf);

	cardp = kzalloc(sizeof(struct usb_card_rec), GFP_KERNEL);
	if (!cardp) {
		lbs_pr_err("Out of memory allocating private data.\n");
		goto error;
	}

	cardp->udev = udev;
	iface_desc = intf->cur_altsetting;

	lbs_deb_usbd(&udev->dev, "bcdUSB = 0x%X bDeviceClass = 0x%X"
	       " bDeviceSubClass = 0x%X, bDeviceProtocol = 0x%X\n",
		     le16_to_cpu(udev->descriptor.bcdUSB),
		     udev->descriptor.bDeviceClass,
		     udev->descriptor.bDeviceSubClass,
		     udev->descriptor.bDeviceProtocol);

	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
		    && ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
			USB_ENDPOINT_XFER_BULK)) {
			/* we found a bulk in endpoint */
			lbs_deb_usbd(&udev->dev, "Bulk in size is %d\n",
				     le16_to_cpu(endpoint->wMaxPacketSize));
			if (!(cardp->rx_urb = usb_alloc_urb(0, GFP_KERNEL))) {
				lbs_deb_usbd(&udev->dev,
				       "Rx URB allocation failed\n");
				goto dealloc;
			}
			cardp->rx_urb_recall = 0;

			cardp->bulk_in_size =
				le16_to_cpu(endpoint->wMaxPacketSize);
			cardp->bulk_in_endpointAddr =
			    (endpoint->
			     bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
			lbs_deb_usbd(&udev->dev, "in_endpoint = %d\n",
			       endpoint->bEndpointAddress);
		}

		if (((endpoint->
		      bEndpointAddress & USB_ENDPOINT_DIR_MASK) ==
		     USB_DIR_OUT)
		    && ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
			USB_ENDPOINT_XFER_BULK)) {
			/* We found bulk out endpoint */
			if (!(cardp->tx_urb = usb_alloc_urb(0, GFP_KERNEL))) {
				lbs_deb_usbd(&udev->dev,
				       "Tx URB allocation failed\n");
				goto dealloc;
			}

			cardp->bulk_out_size =
				le16_to_cpu(endpoint->wMaxPacketSize);
			lbs_deb_usbd(&udev->dev,
				     "Bulk out size is %d\n",
				     le16_to_cpu(endpoint->wMaxPacketSize));
			cardp->bulk_out_endpointAddr =
			    endpoint->bEndpointAddress;
			lbs_deb_usbd(&udev->dev, "out_endpoint = %d\n",
				    endpoint->bEndpointAddress);
			cardp->bulk_out_buffer =
			    kmalloc(MRVDRV_ETH_TX_PACKET_BUFFER_SIZE,
				    GFP_KERNEL);

			if (!cardp->bulk_out_buffer) {
				lbs_deb_usbd(&udev->dev,
				       "Could not allocate buffer\n");
				goto dealloc;
			}
		}
	}

	/* Upload firmware */
	cardp->rinfo.cardp = cardp;
	if (if_usb_prog_firmware(cardp)) {
		lbs_deb_usbd(&udev->dev, "FW upload failed");
		goto err_prog_firmware;
	}

	if (!(priv = libertas_add_card(cardp, &udev->dev)))
		goto err_prog_firmware;

	cardp->priv = priv;

	if (libertas_add_mesh(priv, &udev->dev))
		goto err_add_mesh;

	cardp->eth_dev = priv->dev;

	priv->hw_host_to_card = if_usb_host_to_card;
	priv->hw_get_int_status = if_usb_get_int_status;
	priv->hw_read_event_cause = if_usb_read_event_cause;
	priv->boot2_version = udev->descriptor.bcdDevice;

	/* Delay 200 ms to waiting for the FW ready */
	if_usb_submit_rx_urb(cardp);
	msleep_interruptible(200);
	priv->adapter->fw_ready = 1;

	if (libertas_start_card(priv))
		goto err_start_card;

	list_add_tail(&cardp->list, &usb_devices);

	usb_get_dev(udev);
	usb_set_intfdata(intf, cardp);

	return 0;

err_start_card:
	libertas_remove_mesh(priv);
err_add_mesh:
	libertas_remove_card(priv);
err_prog_firmware:
	if_usb_reset_device(cardp);
dealloc:
	if_usb_free(cardp);

error:
	return -ENOMEM;
}

/**
 *  @brief free resource and cleanup
 *  @param intf		USB interface structure
 *  @return 	   	N/A
 */
static void if_usb_disconnect(struct usb_interface *intf)
{
	struct usb_card_rec *cardp = usb_get_intfdata(intf);
	wlan_private *priv = (wlan_private *) cardp->priv;

	lbs_deb_enter(LBS_DEB_MAIN);

	/* Update Surprise removed to TRUE */
	cardp->surprise_removed = 1;

	list_del(&cardp->list);

	if (priv) {
		wlan_adapter *adapter = priv->adapter;

		adapter->surpriseremoved = 1;
		libertas_stop_card(priv);
		libertas_remove_mesh(priv);
		libertas_remove_card(priv);
	}

	/* Unlink and free urb */
	if_usb_free(cardp);

	usb_set_intfdata(intf, NULL);
	usb_put_dev(interface_to_usbdev(intf));

	lbs_deb_leave(LBS_DEB_MAIN);
}

/**
 *  @brief  This function download FW
 *  @param priv		pointer to wlan_private
 *  @return 	   	0
 */
static int if_prog_firmware(struct usb_card_rec *cardp)
{
	struct FWData *fwdata;
	struct fwheader *fwheader;
	u8 *firmware = cardp->fw->data;

	fwdata = kmalloc(sizeof(struct FWData), GFP_ATOMIC);

	if (!fwdata)
		return -1;

	fwheader = &fwdata->fwheader;

	if (!cardp->CRC_OK) {
		cardp->totalbytes = cardp->fwlastblksent;
		cardp->fwseqnum = cardp->lastseqnum - 1;
	}

	/*
	lbs_deb_usbd(&cardp->udev->dev, "totalbytes = %d\n",
		    cardp->totalbytes);
	*/

	memcpy(fwheader, &firmware[cardp->totalbytes],
	       sizeof(struct fwheader));

	cardp->fwlastblksent = cardp->totalbytes;
	cardp->totalbytes += sizeof(struct fwheader);

	/* lbs_deb_usbd(&cardp->udev->dev,"Copy Data\n"); */
	memcpy(fwdata->data, &firmware[cardp->totalbytes],
	       le32_to_cpu(fwdata->fwheader.datalength));

	/*
	lbs_deb_usbd(&cardp->udev->dev,
		    "Data length = %d\n", le32_to_cpu(fwdata->fwheader.datalength));
	*/

	cardp->fwseqnum = cardp->fwseqnum + 1;

	fwdata->seqnum = cpu_to_le32(cardp->fwseqnum);
	cardp->lastseqnum = cardp->fwseqnum;
	cardp->totalbytes += le32_to_cpu(fwdata->fwheader.datalength);

	if (fwheader->dnldcmd == cpu_to_le32(FW_HAS_DATA_TO_RECV)) {
		/*
		lbs_deb_usbd(&cardp->udev->dev, "There are data to follow\n");
		lbs_deb_usbd(&cardp->udev->dev,
			    "seqnum = %d totalbytes = %d\n", cardp->fwseqnum,
			    cardp->totalbytes);
		*/
		memcpy(cardp->bulk_out_buffer, fwheader, FW_DATA_XMIT_SIZE);
		usb_tx_block(cardp, cardp->bulk_out_buffer, FW_DATA_XMIT_SIZE);

	} else if (fwdata->fwheader.dnldcmd == cpu_to_le32(FW_HAS_LAST_BLOCK)) {
		/*
		lbs_deb_usbd(&cardp->udev->dev,
			    "Host has finished FW downloading\n");
		lbs_deb_usbd(&cardp->udev->dev,
			    "Donwloading FW JUMP BLOCK\n");
		*/
		memcpy(cardp->bulk_out_buffer, fwheader, FW_DATA_XMIT_SIZE);
		usb_tx_block(cardp, cardp->bulk_out_buffer, FW_DATA_XMIT_SIZE);
		cardp->fwfinalblk = 1;
	}

	/*
	lbs_deb_usbd(&cardp->udev->dev,
		    "The firmware download is done size is %d\n",
		    cardp->totalbytes);
	*/

	kfree(fwdata);

	return 0;
}

static int if_usb_reset_device(struct usb_card_rec *cardp)
{
	int ret;
	wlan_private * priv = cardp->priv;

	lbs_deb_enter(LBS_DEB_USB);

	/* Try a USB port reset first, if that fails send the reset
	 * command to the firmware.
	 */
	ret = usb_reset_device(cardp->udev);
	if (!ret && priv) {
		msleep(10);
		ret = libertas_reset_device(priv);
		msleep(10);
	}

	lbs_deb_leave_args(LBS_DEB_USB, "ret %d", ret);

	return ret;
}

/**
 *  @brief This function transfer the data to the device.
 *  @param priv 	pointer to wlan_private
 *  @param payload	pointer to payload data
 *  @param nb		data length
 *  @return 	   	0 or -1
 */
static int usb_tx_block(struct usb_card_rec *cardp, u8 * payload, u16 nb)
{
	int ret = -1;

	/* check if device is removed */
	if (cardp->surprise_removed) {
		lbs_deb_usbd(&cardp->udev->dev, "Device removed\n");
		goto tx_ret;
	}

	usb_fill_bulk_urb(cardp->tx_urb, cardp->udev,
			  usb_sndbulkpipe(cardp->udev,
					  cardp->bulk_out_endpointAddr),
			  payload, nb, if_usb_write_bulk_callback, cardp);

	cardp->tx_urb->transfer_flags |= URB_ZERO_PACKET;

	if ((ret = usb_submit_urb(cardp->tx_urb, GFP_ATOMIC))) {
		/*  transfer failed */
		lbs_deb_usbd(&cardp->udev->dev, "usb_submit_urb failed\n");
		ret = -1;
	} else {
		/* lbs_deb_usbd(&cardp->udev->dev, "usb_submit_urb success\n"); */
		ret = 0;
	}

tx_ret:
	return ret;
}

static int __if_usb_submit_rx_urb(struct usb_card_rec *cardp,
				  void (*callbackfn)(struct urb *urb))
{
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
			  (void *) (skb->tail + (size_t) IPFIELD_ALIGN_OFFSET),
			  MRVDRV_ETH_RX_PACKET_BUFFER_SIZE, callbackfn,
			  rinfo);

	cardp->rx_urb->transfer_flags |= URB_ZERO_PACKET;

	/* lbs_deb_usbd(&cardp->udev->dev, "Pointer for rx_urb %p\n", cardp->rx_urb); */
	if ((ret = usb_submit_urb(cardp->rx_urb, GFP_ATOMIC))) {
		/* handle failure conditions */
		lbs_deb_usbd(&cardp->udev->dev, "Submit Rx URB failed\n");
		ret = -1;
	} else {
		/* lbs_deb_usbd(&cardp->udev->dev, "Submit Rx URB success\n"); */
		ret = 0;
	}

rx_ret:
	return ret;
}

static int if_usb_submit_rx_urb_fwload(struct usb_card_rec *cardp)
{
	return __if_usb_submit_rx_urb(cardp, &if_usb_receive_fwload);
}

static int if_usb_submit_rx_urb(struct usb_card_rec *cardp)
{
	return __if_usb_submit_rx_urb(cardp, &if_usb_receive);
}

static void if_usb_receive_fwload(struct urb *urb)
{
	struct read_cb_info *rinfo = (struct read_cb_info *)urb->context;
	struct sk_buff *skb = rinfo->skb;
	struct usb_card_rec *cardp = (struct usb_card_rec *)rinfo->cardp;
	struct fwsyncheader *syncfwheader;
	struct bootcmdrespStr bootcmdresp;

	if (urb->status) {
		lbs_deb_usbd(&cardp->udev->dev,
			    "URB status is failed during fw load\n");
		kfree_skb(skb);
		return;
	}

	if (cardp->bootcmdresp == 0) {
		memcpy (&bootcmdresp, skb->data + IPFIELD_ALIGN_OFFSET,
			sizeof(bootcmdresp));
		if (le16_to_cpu(cardp->udev->descriptor.bcdDevice) < 0x3106) {
			kfree_skb(skb);
			if_usb_submit_rx_urb_fwload(cardp);
			cardp->bootcmdresp = 1;
			lbs_deb_usbd(&cardp->udev->dev,
				    "Received valid boot command response\n");
			return;
		}
		if (bootcmdresp.u32magicnumber != cpu_to_le32(BOOT_CMD_MAGIC_NUMBER)) {
			lbs_pr_info(
				"boot cmd response wrong magic number (0x%x)\n",
				le32_to_cpu(bootcmdresp.u32magicnumber));
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
			lbs_deb_usbd(&cardp->udev->dev,
				    "Received valid boot command response\n");
		}
		kfree_skb(skb);
		if_usb_submit_rx_urb_fwload(cardp);
		return;
	}

	syncfwheader = kmalloc(sizeof(struct fwsyncheader), GFP_ATOMIC);
	if (!syncfwheader) {
		lbs_deb_usbd(&cardp->udev->dev, "Failure to allocate syncfwheader\n");
		kfree_skb(skb);
		return;
	}

	memcpy(syncfwheader, skb->data + IPFIELD_ALIGN_OFFSET,
			sizeof(struct fwsyncheader));

	if (!syncfwheader->cmd) {
		/*
		lbs_deb_usbd(&cardp->udev->dev,
			    "FW received Blk with correct CRC\n");
		lbs_deb_usbd(&cardp->udev->dev,
			    "FW received Blk seqnum = %d\n",
		       syncfwheader->seqnum);
		*/
		cardp->CRC_OK = 1;
	} else {
		lbs_deb_usbd(&cardp->udev->dev,
			    "FW received Blk with CRC error\n");
		cardp->CRC_OK = 0;
	}

	kfree_skb(skb);

	if (cardp->fwfinalblk) {
		cardp->fwdnldover = 1;
		goto exit;
	}

	if_prog_firmware(cardp);

	if_usb_submit_rx_urb_fwload(cardp);
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
		lbs_deb_usbd(&cardp->udev->dev,
			    "Packet length is Invalid\n");
		kfree_skb(skb);
		return;
	}

	skb_reserve(skb, IPFIELD_ALIGN_OFFSET);
	skb_put(skb, recvlength);
	skb_pull(skb, MESSAGE_HEADER_LEN);
	libertas_process_rxed_packet(priv, skb);
	priv->upld_len = (recvlength - MESSAGE_HEADER_LEN);
}

static inline void process_cmdrequest(int recvlength, u8 *recvbuff,
				      struct sk_buff *skb,
				      struct usb_card_rec *cardp,
				      wlan_private *priv)
{
	u8 *cmdbuf;
	if (recvlength > MRVDRV_SIZE_OF_CMD_BUFFER) {
		lbs_deb_usbd(&cardp->udev->dev,
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
		cmdbuf = priv->upld_buf;
		priv->adapter->hisregcpy &= ~MRVDRV_CMD_UPLD_RDY;
	} else
		cmdbuf = priv->adapter->cur_cmd->bufvirtualaddr;

	cardp->usb_int_cause |= MRVDRV_CMD_UPLD_RDY;
	priv->upld_len = (recvlength - MESSAGE_HEADER_LEN);
	memcpy(cmdbuf, recvbuff + MESSAGE_HEADER_LEN,
	       priv->upld_len);

	kfree_skb(skb);
	libertas_interrupt(priv->dev);
	spin_unlock(&priv->adapter->driver_lock);

	lbs_deb_usbd(&cardp->udev->dev,
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
	struct sk_buff *skb = rinfo->skb;
	struct usb_card_rec *cardp = (struct usb_card_rec *) rinfo->cardp;
	wlan_private * priv = cardp->priv;

	int recvlength = urb->actual_length;
	u8 *recvbuff = NULL;
	u32 recvtype = 0;

	lbs_deb_enter(LBS_DEB_USB);

	if (recvlength) {
		__le32 tmp;

		if (urb->status) {
			lbs_deb_usbd(&cardp->udev->dev,
				    "URB status is failed\n");
			kfree_skb(skb);
			goto setup_for_next;
		}

		recvbuff = skb->data + IPFIELD_ALIGN_OFFSET;
		memcpy(&tmp, recvbuff, sizeof(u32));
		recvtype = le32_to_cpu(tmp);
		lbs_deb_usbd(&cardp->udev->dev,
			    "Recv length = 0x%x, Recv type = 0x%X\n",
			    recvlength, recvtype);
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
		cardp->usb_event_cause = le32_to_cpu(*(__le32 *) (recvbuff + MESSAGE_HEADER_LEN));
		lbs_deb_usbd(&cardp->udev->dev,"**EVENT** 0x%X\n",
			    cardp->usb_event_cause);
		if (cardp->usb_event_cause & 0xffff0000) {
			libertas_send_tx_feedback(priv);
			spin_unlock(&priv->adapter->driver_lock);
			break;
		}
		cardp->usb_event_cause <<= 3;
		cardp->usb_int_cause |= MRVDRV_CARDEVENT;
		kfree_skb(skb);
		libertas_interrupt(priv->dev);
		spin_unlock(&priv->adapter->driver_lock);
		goto rx_exit;
	default:
		lbs_deb_usbd(&cardp->udev->dev, "Unknown command type 0x%X\n",
		             recvtype);
		kfree_skb(skb);
		break;
	}

setup_for_next:
	if_usb_submit_rx_urb(cardp);
rx_exit:
	lbs_deb_leave(LBS_DEB_USB);
}

/**
 *  @brief This function downloads data to FW
 *  @param priv		pointer to wlan_private structure
 *  @param type		type of data
 *  @param buf		pointer to data buffer
 *  @param len		number of bytes
 *  @return 	   	0 or -1
 */
static int if_usb_host_to_card(wlan_private * priv, u8 type, u8 * payload, u16 nb)
{
	struct usb_card_rec *cardp = (struct usb_card_rec *)priv->card;

	lbs_deb_usbd(&cardp->udev->dev,"*** type = %u\n", type);
	lbs_deb_usbd(&cardp->udev->dev,"size after = %d\n", nb);

	if (type == MVMS_CMD) {
		__le32 tmp = cpu_to_le32(CMD_TYPE_REQUEST);
		priv->dnld_sent = DNLD_CMD_SENT;
		memcpy(cardp->bulk_out_buffer, (u8 *) & tmp,
		       MESSAGE_HEADER_LEN);

	} else {
		__le32 tmp = cpu_to_le32(CMD_TYPE_DATA);
		priv->dnld_sent = DNLD_DATA_SENT;
		memcpy(cardp->bulk_out_buffer, (u8 *) & tmp,
		       MESSAGE_HEADER_LEN);
	}

	memcpy((cardp->bulk_out_buffer + MESSAGE_HEADER_LEN), payload, nb);

	return usb_tx_block(cardp, cardp->bulk_out_buffer,
	                    nb + MESSAGE_HEADER_LEN);
}

/* called with adapter->driver_lock held */
static int if_usb_get_int_status(wlan_private * priv, u8 * ireg)
{
	struct usb_card_rec *cardp = priv->card;

	*ireg = cardp->usb_int_cause;
	cardp->usb_int_cause = 0;

	lbs_deb_usbd(&cardp->udev->dev,"Int cause is 0x%X\n", *ireg);

	return 0;
}

static int if_usb_read_event_cause(wlan_private * priv)
{
	struct usb_card_rec *cardp = priv->card;

	priv->adapter->eventcause = cardp->usb_event_cause;
	/* Re-submit rx urb here to avoid event lost issue */
	if_usb_submit_rx_urb(cardp);
	return 0;
}

/**
 *  @brief This function issues Boot command to the Boot2 code
 *  @param ivalue   1:Boot from FW by USB-Download
 *                  2:Boot from FW in EEPROM
 *  @return 	   	0
 */
static int if_usb_issue_boot_command(struct usb_card_rec *cardp, int ivalue)
{
	struct bootcmdstr sbootcmd;
	int i;

	/* Prepare command */
	sbootcmd.u32magicnumber = cpu_to_le32(BOOT_CMD_MAGIC_NUMBER);
	sbootcmd.u8cmd_tag = ivalue;
	for (i=0; i<11; i++)
		sbootcmd.au8dumy[i]=0x00;
	memcpy(cardp->bulk_out_buffer, &sbootcmd, sizeof(struct bootcmdstr));

	/* Issue command */
	usb_tx_block(cardp, cardp->bulk_out_buffer, sizeof(struct bootcmdstr));

	return 0;
}


/**
 *  @brief This function checks the validity of Boot2/FW image.
 *
 *  @param data              pointer to image
 *         len               image length
 *  @return     0 or -1
 */
static int check_fwfile_format(u8 *data, u32 totlen)
{
	u32 bincmd, exit;
	u32 blksize, offset, len;
	int ret;

	ret = 1;
	exit = len = 0;

	do {
		struct fwheader *fwh = (void *)data;

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
		lbs_pr_err("firmware file format check FAIL\n");
	else
		lbs_deb_fw("firmware file format check PASS\n");

	return ret;
}


static int if_usb_prog_firmware(struct usb_card_rec *cardp)
{
	int i = 0;
	static int reset_count = 10;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_USB);

	if ((ret = request_firmware(&cardp->fw, libertas_fw_name,
				    &cardp->udev->dev)) < 0) {
		lbs_pr_err("request_firmware() failed with %#x\n", ret);
		lbs_pr_err("firmware %s not found\n", libertas_fw_name);
		goto done;
	}

	if (check_fwfile_format(cardp->fw->data, cardp->fw->size))
		goto release_fw;

restart:
	if (if_usb_submit_rx_urb_fwload(cardp) < 0) {
		lbs_deb_usbd(&cardp->udev->dev, "URB submission is failed\n");
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

	if (cardp->bootcmdresp == 0) {
		if (--reset_count >= 0) {
			if_usb_reset_device(cardp);
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

	if_prog_firmware(cardp);

	do {
		lbs_deb_usbd(&cardp->udev->dev,"Wlan sched timeout\n");
		i++;
		msleep_interruptible(100);
		if (cardp->surprise_removed || i >= 20)
			break;
	} while (!cardp->fwdnldover);

	if (!cardp->fwdnldover) {
		lbs_pr_info("failed to load fw, resetting device!\n");
		if (--reset_count >= 0) {
			if_usb_reset_device(cardp);
			goto restart;
		}

		lbs_pr_info("FW download failure, time = %d ms\n", i * 100);
		ret = -1;
		goto release_fw;
	}

release_fw:
	release_firmware(cardp->fw);
	cardp->fw = NULL;

done:
	lbs_deb_leave_args(LBS_DEB_USB, "ret %d", ret);
	return ret;
}


#ifdef CONFIG_PM
static int if_usb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usb_card_rec *cardp = usb_get_intfdata(intf);
	wlan_private *priv = cardp->priv;

	lbs_deb_enter(LBS_DEB_USB);

	if (priv->adapter->psstate != PS_STATE_FULL_POWER)
		return -1;

	if (priv->mesh_dev && !priv->mesh_autostart_enabled) {
		/* Mesh autostart must be activated while sleeping
		 * On resume it will go back to the current state
		 */
		struct cmd_ds_mesh_access mesh_access;
		memset(&mesh_access, 0, sizeof(mesh_access));
		mesh_access.data[0] = cpu_to_le32(1);
		libertas_prepare_and_send_command(priv,
				CMD_MESH_ACCESS,
				CMD_ACT_MESH_SET_AUTOSTART_ENABLED,
				CMD_OPTION_WAITFORRSP, 0, (void *)&mesh_access);
	}

	netif_device_detach(cardp->eth_dev);
	netif_device_detach(priv->mesh_dev);

	/* Unlink tx & rx urb */
	usb_kill_urb(cardp->tx_urb);
	usb_kill_urb(cardp->rx_urb);

	cardp->rx_urb_recall = 1;

	lbs_deb_leave(LBS_DEB_USB);
	return 0;
}

static int if_usb_resume(struct usb_interface *intf)
{
	struct usb_card_rec *cardp = usb_get_intfdata(intf);
	wlan_private *priv = cardp->priv;

	lbs_deb_enter(LBS_DEB_USB);

	cardp->rx_urb_recall = 0;

	if_usb_submit_rx_urb(cardp->priv);

	netif_device_attach(cardp->eth_dev);
	netif_device_attach(priv->mesh_dev);

	if (priv->mesh_dev && !priv->mesh_autostart_enabled) {
		/* Mesh autostart was activated while sleeping
		 * Disable it if appropriate
		 */
		struct cmd_ds_mesh_access mesh_access;
		memset(&mesh_access, 0, sizeof(mesh_access));
		mesh_access.data[0] = cpu_to_le32(0);
		libertas_prepare_and_send_command(priv,
				CMD_MESH_ACCESS,
				CMD_ACT_MESH_SET_AUTOSTART_ENABLED,
				CMD_OPTION_WAITFORRSP, 0, (void *)&mesh_access);
	}

	lbs_deb_leave(LBS_DEB_USB);
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

static int if_usb_init_module(void)
{
	int ret = 0;

	lbs_deb_enter(LBS_DEB_MAIN);

	if (libertas_fw_name == NULL) {
		libertas_fw_name = default_fw_name;
	}

	ret = usb_register(&if_usb_driver);

	lbs_deb_leave_args(LBS_DEB_MAIN, "ret %d", ret);
	return ret;
}

static void if_usb_exit_module(void)
{
	struct usb_card_rec *cardp, *cardp_temp;

	lbs_deb_enter(LBS_DEB_MAIN);

	list_for_each_entry_safe(cardp, cardp_temp, &usb_devices, list) {
		libertas_prepare_and_send_command(cardp->priv, CMD_802_11_RESET,
		                                  CMD_ACT_HALT, 0, 0, NULL);
	}

	/* API unregisters the driver from USB subsystem */
	usb_deregister(&if_usb_driver);

	lbs_deb_leave(LBS_DEB_MAIN);
}

module_init(if_usb_init_module);
module_exit(if_usb_exit_module);

MODULE_DESCRIPTION("8388 USB WLAN Driver");
MODULE_AUTHOR("Marvell International Ltd.");
MODULE_LICENSE("GPL");
