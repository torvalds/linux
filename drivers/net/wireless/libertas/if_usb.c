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

char *libertas_fw_name = NULL;
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
static int if_usb_reset_device(wlan_private *priv);
static int if_usb_register_dev(wlan_private * priv);
static int if_usb_unregister_dev(wlan_private *);
static int if_usb_prog_firmware(wlan_private *);
static int if_usb_host_to_card(wlan_private * priv, u8 type, u8 * payload, u16 nb);
static int if_usb_get_int_status(wlan_private * priv, u8 *);
static int if_usb_read_event_cause(wlan_private *);

/**
 *  @brief  call back function to handle the status of the URB
 *  @param urb 		pointer to urb structure
 *  @return 	   	N/A
 */
static void if_usb_write_bulk_callback(struct urb *urb)
{
	wlan_private *priv = (wlan_private *) (urb->context);
	wlan_adapter *adapter = priv->adapter;
	struct net_device *dev = priv->dev;

	/* handle the transmission complete validations */

	if (urb->status != 0) {
		/* print the failure status number for debug */
		lbs_pr_info("URB in failure status: %d\n", urb->status);
	} else {
		/*
		lbs_deb_usbd(&urb->dev->dev, "URB status is successfull\n");
		lbs_deb_usbd(&urb->dev->dev, "Actual length transmitted %d\n",
		       urb->actual_length);
		*/
		priv->dnld_sent = DNLD_RES_RECEIVED;
		/* Wake main thread if commands are pending */
		if (!adapter->cur_cmd)
			wake_up_interruptible(&priv->mainthread.waitq);
		if ((adapter->connect_status == libertas_connected)) {
			netif_wake_queue(dev);
			netif_wake_queue(priv->mesh_dev);
		}
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

	if (!(priv = libertas_add_card(cardp, &udev->dev)))
		goto dealloc;

	if (libertas_add_mesh(priv, &udev->dev))
		goto err_add_mesh;

	priv->hw_register_dev = if_usb_register_dev;
	priv->hw_unregister_dev = if_usb_unregister_dev;
	priv->hw_prog_firmware = if_usb_prog_firmware;
	priv->hw_host_to_card = if_usb_host_to_card;
	priv->hw_get_int_status = if_usb_get_int_status;
	priv->hw_read_event_cause = if_usb_read_event_cause;

	if (libertas_activate_card(priv, libertas_fw_name))
		goto err_activate_card;

	list_add_tail(&cardp->list, &usb_devices);

	usb_get_dev(udev);
	usb_set_intfdata(intf, cardp);

	return 0;

err_activate_card:
	libertas_remove_mesh(priv);
err_add_mesh:
	free_netdev(priv->dev);
	kfree(priv->adapter);
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
	wlan_adapter *adapter = NULL;

	adapter = priv->adapter;

	/*
	 * Update Surprise removed to TRUE
	 */
	adapter->surpriseremoved = 1;

	list_del(&cardp->list);

	/* card is removed and we can call wlan_remove_card */
	lbs_deb_usbd(&cardp->udev->dev, "call remove card\n");
	libertas_remove_mesh(priv);
	libertas_remove_card(priv);

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
	struct usb_card_rec *cardp = priv->card;
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
		usb_tx_block(priv, cardp->bulk_out_buffer, FW_DATA_XMIT_SIZE);

	} else if (fwdata->fwheader.dnldcmd == cpu_to_le32(FW_HAS_LAST_BLOCK)) {
		/*
		lbs_deb_usbd(&cardp->udev->dev,
			    "Host has finished FW downloading\n");
		lbs_deb_usbd(&cardp->udev->dev,
			    "Donwloading FW JUMP BLOCK\n");
		*/
		memcpy(cardp->bulk_out_buffer, fwheader, FW_DATA_XMIT_SIZE);
		usb_tx_block(priv, cardp->bulk_out_buffer, FW_DATA_XMIT_SIZE);
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

static int libertas_do_reset(wlan_private *priv)
{
	int ret;
	struct usb_card_rec *cardp = priv->card;

	lbs_deb_enter(LBS_DEB_USB);

	ret = usb_reset_device(cardp->udev);
	if (!ret) {
		msleep(10);
		if_usb_reset_device(priv);
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
int usb_tx_block(wlan_private * priv, u8 * payload, u16 nb)
{
	/* pointer to card structure */
	struct usb_card_rec *cardp = priv->card;
	int ret = -1;

	/* check if device is removed */
	if (priv->adapter->surpriseremoved) {
		lbs_deb_usbd(&cardp->udev->dev, "Device removed\n");
		goto tx_ret;
	}

	usb_fill_bulk_urb(cardp->tx_urb, cardp->udev,
			  usb_sndbulkpipe(cardp->udev,
					  cardp->bulk_out_endpointAddr),
			  payload, nb, if_usb_write_bulk_callback, priv);

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

static int __if_usb_submit_rx_urb(wlan_private * priv,
				  void (*callbackfn)
				  (struct urb *urb))
{
	struct usb_card_rec *cardp = priv->card;
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
	struct usb_card_rec *cardp = (struct usb_card_rec *)priv->card;
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
			if_usb_submit_rx_urb_fwload(priv);
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
		if_usb_submit_rx_urb_fwload(priv);
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
		priv->adapter->hisregcpy &= ~his_cmdupldrdy;
	} else
		cmdbuf = priv->adapter->cur_cmd->bufvirtualaddr;

	cardp->usb_int_cause |= his_cmdupldrdy;
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
	wlan_private *priv = rinfo->priv;
	struct sk_buff *skb = rinfo->skb;
	struct usb_card_rec *cardp = (struct usb_card_rec *)priv->card;

	int recvlength = urb->actual_length;
	u8 *recvbuff = NULL;
	u32 recvtype;

	lbs_deb_enter(LBS_DEB_USB);

	if (recvlength) {
		if (urb->status) {
			lbs_deb_usbd(&cardp->udev->dev,
				    "URB status is failed\n");
			kfree_skb(skb);
			goto setup_for_next;
		}

		recvbuff = skb->data + IPFIELD_ALIGN_OFFSET;
		memcpy(&recvtype, recvbuff, sizeof(u32));
		lbs_deb_usbd(&cardp->udev->dev,
			    "Recv length = 0x%x\n", recvlength);
		lbs_deb_usbd(&cardp->udev->dev,
			    "Receive type = 0x%X\n", recvtype);
		recvtype = le32_to_cpu(recvtype);
		lbs_deb_usbd(&cardp->udev->dev,
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
		cardp->usb_event_cause = le32_to_cpu(*(__le32 *) (recvbuff + MESSAGE_HEADER_LEN));
		lbs_deb_usbd(&cardp->udev->dev,"**EVENT** 0x%X\n",
			    cardp->usb_event_cause);
		if (cardp->usb_event_cause & 0xffff0000) {
			libertas_send_tx_feedback(priv);
			spin_unlock(&priv->adapter->driver_lock);
			break;
		}
		cardp->usb_event_cause <<= 3;
		cardp->usb_int_cause |= his_cardevent;
		kfree_skb(skb);
		libertas_interrupt(priv->dev);
		spin_unlock(&priv->adapter->driver_lock);
		goto rx_exit;
	default:
		kfree_skb(skb);
		break;
	}

setup_for_next:
	if_usb_submit_rx_urb(priv);
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
	int ret = -1;
	u32 tmp;
	struct usb_card_rec *cardp = (struct usb_card_rec *)priv->card;

	lbs_deb_usbd(&cardp->udev->dev,"*** type = %u\n", type);
	lbs_deb_usbd(&cardp->udev->dev,"size after = %d\n", nb);

	if (type == MVMS_CMD) {
		tmp = cpu_to_le32(CMD_TYPE_REQUEST);
		priv->dnld_sent = DNLD_CMD_SENT;
		memcpy(cardp->bulk_out_buffer, (u8 *) & tmp,
		       MESSAGE_HEADER_LEN);

	} else {
		tmp = cpu_to_le32(CMD_TYPE_DATA);
		priv->dnld_sent = DNLD_DATA_SENT;
		memcpy(cardp->bulk_out_buffer, (u8 *) & tmp,
		       MESSAGE_HEADER_LEN);
	}

	memcpy((cardp->bulk_out_buffer + MESSAGE_HEADER_LEN), payload, nb);

	ret =
	    usb_tx_block(priv, cardp->bulk_out_buffer, nb + MESSAGE_HEADER_LEN);

	return ret;
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
	if_usb_submit_rx_urb(priv);
	return 0;
}

static int if_usb_reset_device(wlan_private *priv)
{
	int ret;

	lbs_deb_enter(LBS_DEB_USB);
	ret = libertas_prepare_and_send_command(priv, cmd_802_11_reset,
				    cmd_act_halt, 0, 0, NULL);
	msleep_interruptible(10);

	lbs_deb_leave_args(LBS_DEB_USB, "ret %d", ret);
	return ret;
}

static int if_usb_unregister_dev(wlan_private * priv)
{
	int ret = 0;

	/* Need to send a Reset command to device before USB resources freed
	 * and wlan_remove_card() called, then device can handle FW download
	 * again.
	 */
	if (priv)
		if_usb_reset_device(priv);

	return ret;
}


/**
 *  @brief  This function register usb device and initialize parameter
 *  @param		priv pointer to wlan_private
 *  @return		0 or -1
 */
static int if_usb_register_dev(wlan_private * priv)
{
	struct usb_card_rec *cardp = (struct usb_card_rec *)priv->card;

	lbs_deb_enter(LBS_DEB_USB);

	cardp->priv = priv;
	cardp->eth_dev = priv->dev;
	priv->hotplug_device = &(cardp->udev->dev);

	lbs_deb_usbd(&cardp->udev->dev, "udev pointer is at %p\n",
		    cardp->udev);

	lbs_deb_leave(LBS_DEB_USB);
	return 0;
}



static int if_usb_prog_firmware(wlan_private * priv)
{
	struct usb_card_rec *cardp = priv->card;
	int i = 0;
	static int reset_count = 10;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_USB);

	cardp->rinfo.priv = priv;

restart:
	if (if_usb_submit_rx_urb_fwload(priv) < 0) {
		lbs_deb_usbd(&cardp->udev->dev, "URB submission is failed\n");
		ret = -1;
		goto done;
	}

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
		lbs_deb_usbd(&cardp->udev->dev,"Wlan sched timeout\n");
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
		ret = -1;
		goto done;
	}

	if_usb_submit_rx_urb(priv);

	/* Delay 200 ms to waiting for the FW ready */
	msleep_interruptible(200);

	priv->adapter->fw_ready = 1;

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

	list_for_each_entry_safe(cardp, cardp_temp, &usb_devices, list)
		if_usb_reset_device((wlan_private *) cardp->priv);

	/* API unregisters the driver from USB subsystem */
	usb_deregister(&if_usb_driver);

	lbs_deb_leave(LBS_DEB_MAIN);
}

module_init(if_usb_init_module);
module_exit(if_usb_exit_module);

MODULE_DESCRIPTION("8388 USB WLAN Driver");
MODULE_AUTHOR("Marvell International Ltd.");
MODULE_LICENSE("GPL");
