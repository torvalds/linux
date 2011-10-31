/*
 * This file contains functions used in USB interface module.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/usb.h>

#ifdef CONFIG_OLPC
#include <asm/olpc.h>
#endif

#define DRV_NAME "usb8xxx"

#include "host.h"
#include "decl.h"
#include "defs.h"
#include "dev.h"
#include "cmd.h"
#include "if_usb.h"

#define INSANEDEBUG	0
#define lbs_deb_usb2(...) do { if (INSANEDEBUG) lbs_deb_usbd(__VA_ARGS__); } while (0)

#define MESSAGE_HEADER_LEN	4

static char *lbs_fw_name = NULL;
module_param_named(fw_name, lbs_fw_name, charp, 0644);

MODULE_FIRMWARE("libertas/usb8388_v9.bin");
MODULE_FIRMWARE("libertas/usb8388_v5.bin");
MODULE_FIRMWARE("libertas/usb8388.bin");
MODULE_FIRMWARE("libertas/usb8682.bin");
MODULE_FIRMWARE("usb8388.bin");

enum {
	MODEL_UNKNOWN = 0x0,
	MODEL_8388 = 0x1,
	MODEL_8682 = 0x2
};

static struct usb_device_id if_usb_table[] = {
	/* Enter the device signature inside */
	{ USB_DEVICE(0x1286, 0x2001), .driver_info = MODEL_8388 },
	{ USB_DEVICE(0x05a3, 0x8388), .driver_info = MODEL_8388 },
	{}	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, if_usb_table);

static void if_usb_receive(struct urb *urb);
static void if_usb_receive_fwload(struct urb *urb);
static int __if_usb_prog_firmware(struct if_usb_card *cardp,
					const char *fwname, int cmd);
static int if_usb_prog_firmware(struct if_usb_card *cardp,
					const char *fwname, int cmd);
static int if_usb_host_to_card(struct lbs_private *priv, uint8_t type,
			       uint8_t *payload, uint16_t nb);
static int usb_tx_block(struct if_usb_card *cardp, uint8_t *payload,
			uint16_t nb);
static void if_usb_free(struct if_usb_card *cardp);
static int if_usb_submit_rx_urb(struct if_usb_card *cardp);
static int if_usb_reset_device(struct if_usb_card *cardp);

/* sysfs hooks */

/*
 *  Set function to write firmware to device's persistent memory
 */
static ssize_t if_usb_firmware_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct if_usb_card *cardp = priv->card;
	int ret;

	BUG_ON(buf == NULL);

	ret = if_usb_prog_firmware(cardp, buf, BOOT_CMD_UPDATE_FW);
	if (ret == 0)
		return count;

	return ret;
}

/*
 * lbs_flash_fw attribute to be exported per ethX interface through sysfs
 * (/sys/class/net/ethX/lbs_flash_fw).  Use this like so to write firmware to
 * the device's persistent memory:
 * echo usb8388-5.126.0.p5.bin > /sys/class/net/ethX/lbs_flash_fw
 */
static DEVICE_ATTR(lbs_flash_fw, 0200, NULL, if_usb_firmware_set);

/**
 * if_usb_boot2_set - write firmware to device's persistent memory
 *
 * @dev: target device
 * @attr: device attributes
 * @buf: firmware buffer to write
 * @count: number of bytes to write
 *
 * returns: number of bytes written or negative error code
 */
static ssize_t if_usb_boot2_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct if_usb_card *cardp = priv->card;
	int ret;

	BUG_ON(buf == NULL);

	ret = if_usb_prog_firmware(cardp, buf, BOOT_CMD_UPDATE_BOOT2);
	if (ret == 0)
		return count;

	return ret;
}

/*
 * lbs_flash_boot2 attribute to be exported per ethX interface through sysfs
 * (/sys/class/net/ethX/lbs_flash_boot2).  Use this like so to write firmware
 * to the device's persistent memory:
 * echo usb8388-5.126.0.p5.bin > /sys/class/net/ethX/lbs_flash_boot2
 */
static DEVICE_ATTR(lbs_flash_boot2, 0200, NULL, if_usb_boot2_set);

/**
 * if_usb_write_bulk_callback - callback function to handle the status
 * of the URB
 * @urb:	pointer to &urb structure
 * returns:	N/A
 */
static void if_usb_write_bulk_callback(struct urb *urb)
{
	struct if_usb_card *cardp = (struct if_usb_card *) urb->context;

	/* handle the transmission complete validations */

	if (urb->status == 0) {
		struct lbs_private *priv = cardp->priv;

		lbs_deb_usb2(&urb->dev->dev, "URB status is successful\n");
		lbs_deb_usb2(&urb->dev->dev, "Actual length transmitted %d\n",
			     urb->actual_length);

		/* Boot commands such as UPDATE_FW and UPDATE_BOOT2 are not
		 * passed up to the lbs level.
		 */
		if (priv && priv->dnld_sent != DNLD_BOOTCMD_SENT)
			lbs_host_to_card_done(priv);
	} else {
		/* print the failure status number for debug */
		pr_info("URB in failure status: %d\n", urb->status);
	}
}

/**
 * if_usb_free - free tx/rx urb, skb and rx buffer
 * @cardp:	pointer to &if_usb_card
 * returns:	N/A
 */
static void if_usb_free(struct if_usb_card *cardp)
{
	lbs_deb_enter(LBS_DEB_USB);

	/* Unlink tx & rx urb */
	usb_kill_urb(cardp->tx_urb);
	usb_kill_urb(cardp->rx_urb);

	usb_free_urb(cardp->tx_urb);
	cardp->tx_urb = NULL;

	usb_free_urb(cardp->rx_urb);
	cardp->rx_urb = NULL;

	kfree(cardp->ep_out_buf);
	cardp->ep_out_buf = NULL;

	lbs_deb_leave(LBS_DEB_USB);
}

static void if_usb_setup_firmware(struct lbs_private *priv)
{
	struct if_usb_card *cardp = priv->card;
	struct cmd_ds_set_boot2_ver b2_cmd;
	struct cmd_ds_802_11_fw_wake_method wake_method;

	b2_cmd.hdr.size = cpu_to_le16(sizeof(b2_cmd));
	b2_cmd.action = 0;
	b2_cmd.version = cardp->boot2_version;

	if (lbs_cmd_with_response(priv, CMD_SET_BOOT2_VER, &b2_cmd))
		lbs_deb_usb("Setting boot2 version failed\n");

	priv->wol_gpio = 2; /* Wake via GPIO2... */
	priv->wol_gap = 20; /* ... after 20ms    */
	lbs_host_sleep_cfg(priv, EHS_WAKE_ON_UNICAST_DATA,
			(struct wol_config *) NULL);

	wake_method.hdr.size = cpu_to_le16(sizeof(wake_method));
	wake_method.action = cpu_to_le16(CMD_ACT_GET);
	if (lbs_cmd_with_response(priv, CMD_802_11_FW_WAKE_METHOD, &wake_method)) {
		netdev_info(priv->dev, "Firmware does not seem to support PS mode\n");
		priv->fwcapinfo &= ~FW_CAPINFO_PS;
	} else {
		if (le16_to_cpu(wake_method.method) == CMD_WAKE_METHOD_COMMAND_INT) {
			lbs_deb_usb("Firmware seems to support PS with wake-via-command\n");
		} else {
			/* The versions which boot up this way don't seem to
			   work even if we set it to the command interrupt */
			priv->fwcapinfo &= ~FW_CAPINFO_PS;
			netdev_info(priv->dev,
				    "Firmware doesn't wake via command interrupt; disabling PS mode\n");
		}
	}
}

static void if_usb_fw_timeo(unsigned long priv)
{
	struct if_usb_card *cardp = (void *)priv;

	if (cardp->fwdnldover) {
		lbs_deb_usb("Download complete, no event. Assuming success\n");
	} else {
		pr_err("Download timed out\n");
		cardp->surprise_removed = 1;
	}
	wake_up(&cardp->fw_wq);
}

#ifdef CONFIG_OLPC
static void if_usb_reset_olpc_card(struct lbs_private *priv)
{
	printk(KERN_CRIT "Resetting OLPC wireless via EC...\n");
	olpc_ec_cmd(0x25, NULL, 0, NULL, 0);
}
#endif

/**
 * if_usb_probe - sets the configuration values
 * @intf:	&usb_interface pointer
 * @id:	pointer to usb_device_id
 * returns:	0 on success, error code on failure
 */
static int if_usb_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct usb_device *udev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	struct lbs_private *priv;
	struct if_usb_card *cardp;
	int i;

	udev = interface_to_usbdev(intf);

	cardp = kzalloc(sizeof(struct if_usb_card), GFP_KERNEL);
	if (!cardp) {
		pr_err("Out of memory allocating private data\n");
		goto error;
	}

	setup_timer(&cardp->fw_timeout, if_usb_fw_timeo, (unsigned long)cardp);
	init_waitqueue_head(&cardp->fw_wq);

	cardp->udev = udev;
	cardp->model = (uint32_t) id->driver_info;
	iface_desc = intf->cur_altsetting;

	lbs_deb_usbd(&udev->dev, "bcdUSB = 0x%X bDeviceClass = 0x%X"
		     " bDeviceSubClass = 0x%X, bDeviceProtocol = 0x%X\n",
		     le16_to_cpu(udev->descriptor.bcdUSB),
		     udev->descriptor.bDeviceClass,
		     udev->descriptor.bDeviceSubClass,
		     udev->descriptor.bDeviceProtocol);

	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		if (usb_endpoint_is_bulk_in(endpoint)) {
			cardp->ep_in_size = le16_to_cpu(endpoint->wMaxPacketSize);
			cardp->ep_in = usb_endpoint_num(endpoint);

			lbs_deb_usbd(&udev->dev, "in_endpoint = %d\n", cardp->ep_in);
			lbs_deb_usbd(&udev->dev, "Bulk in size is %d\n", cardp->ep_in_size);

		} else if (usb_endpoint_is_bulk_out(endpoint)) {
			cardp->ep_out_size = le16_to_cpu(endpoint->wMaxPacketSize);
			cardp->ep_out = usb_endpoint_num(endpoint);

			lbs_deb_usbd(&udev->dev, "out_endpoint = %d\n", cardp->ep_out);
			lbs_deb_usbd(&udev->dev, "Bulk out size is %d\n", cardp->ep_out_size);
		}
	}
	if (!cardp->ep_out_size || !cardp->ep_in_size) {
		lbs_deb_usbd(&udev->dev, "Endpoints not found\n");
		goto dealloc;
	}
	if (!(cardp->rx_urb = usb_alloc_urb(0, GFP_KERNEL))) {
		lbs_deb_usbd(&udev->dev, "Rx URB allocation failed\n");
		goto dealloc;
	}
	if (!(cardp->tx_urb = usb_alloc_urb(0, GFP_KERNEL))) {
		lbs_deb_usbd(&udev->dev, "Tx URB allocation failed\n");
		goto dealloc;
	}
	cardp->ep_out_buf = kmalloc(MRVDRV_ETH_TX_PACKET_BUFFER_SIZE, GFP_KERNEL);
	if (!cardp->ep_out_buf) {
		lbs_deb_usbd(&udev->dev, "Could not allocate buffer\n");
		goto dealloc;
	}

	/* Upload firmware */
	kparam_block_sysfs_write(fw_name);
	if (__if_usb_prog_firmware(cardp, lbs_fw_name, BOOT_CMD_FW_BY_USB)) {
		kparam_unblock_sysfs_write(fw_name);
		lbs_deb_usbd(&udev->dev, "FW upload failed\n");
		goto err_prog_firmware;
	}
	kparam_unblock_sysfs_write(fw_name);

	if (!(priv = lbs_add_card(cardp, &intf->dev)))
		goto err_prog_firmware;

	cardp->priv = priv;
	cardp->priv->fw_ready = 1;

	priv->hw_host_to_card = if_usb_host_to_card;
	priv->enter_deep_sleep = NULL;
	priv->exit_deep_sleep = NULL;
	priv->reset_deep_sleep_wakeup = NULL;
#ifdef CONFIG_OLPC
	if (machine_is_olpc())
		priv->reset_card = if_usb_reset_olpc_card;
#endif

	cardp->boot2_version = udev->descriptor.bcdDevice;

	if_usb_submit_rx_urb(cardp);

	if (lbs_start_card(priv))
		goto err_start_card;

	if_usb_setup_firmware(priv);

	usb_get_dev(udev);
	usb_set_intfdata(intf, cardp);

	if (device_create_file(&priv->dev->dev, &dev_attr_lbs_flash_fw))
		netdev_err(priv->dev,
			   "cannot register lbs_flash_fw attribute\n");

	if (device_create_file(&priv->dev->dev, &dev_attr_lbs_flash_boot2))
		netdev_err(priv->dev,
			   "cannot register lbs_flash_boot2 attribute\n");

	/*
	 * EHS_REMOVE_WAKEUP is not supported on all versions of the firmware.
	 */
	priv->wol_criteria = EHS_REMOVE_WAKEUP;
	if (lbs_host_sleep_cfg(priv, priv->wol_criteria, NULL))
		priv->ehs_remove_supported = false;

	return 0;

err_start_card:
	lbs_remove_card(priv);
err_prog_firmware:
	if_usb_reset_device(cardp);
dealloc:
	if_usb_free(cardp);

error:
	return -ENOMEM;
}

/**
 * if_usb_disconnect - free resource and cleanup
 * @intf:	USB interface structure
 * returns:	N/A
 */
static void if_usb_disconnect(struct usb_interface *intf)
{
	struct if_usb_card *cardp = usb_get_intfdata(intf);
	struct lbs_private *priv = (struct lbs_private *) cardp->priv;

	lbs_deb_enter(LBS_DEB_MAIN);

	device_remove_file(&priv->dev->dev, &dev_attr_lbs_flash_boot2);
	device_remove_file(&priv->dev->dev, &dev_attr_lbs_flash_fw);

	cardp->surprise_removed = 1;

	if (priv) {
		priv->surpriseremoved = 1;
		lbs_stop_card(priv);
		lbs_remove_card(priv);
	}

	/* Unlink and free urb */
	if_usb_free(cardp);

	usb_set_intfdata(intf, NULL);
	usb_put_dev(interface_to_usbdev(intf));

	lbs_deb_leave(LBS_DEB_MAIN);
}

/**
 * if_usb_send_fw_pkt - download FW
 * @cardp:	pointer to &struct if_usb_card
 * returns:	0
 */
static int if_usb_send_fw_pkt(struct if_usb_card *cardp)
{
	struct fwdata *fwdata = cardp->ep_out_buf;
	const uint8_t *firmware = cardp->fw->data;

	/* If we got a CRC failure on the last block, back
	   up and retry it */
	if (!cardp->CRC_OK) {
		cardp->totalbytes = cardp->fwlastblksent;
		cardp->fwseqnum--;
	}

	lbs_deb_usb2(&cardp->udev->dev, "totalbytes = %d\n",
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

	lbs_deb_usb2(&cardp->udev->dev, "Data length = %d\n",
		     le32_to_cpu(fwdata->hdr.datalength));

	fwdata->seqnum = cpu_to_le32(++cardp->fwseqnum);
	cardp->totalbytes += le32_to_cpu(fwdata->hdr.datalength);

	usb_tx_block(cardp, cardp->ep_out_buf, sizeof(struct fwdata) +
		     le32_to_cpu(fwdata->hdr.datalength));

	if (fwdata->hdr.dnldcmd == cpu_to_le32(FW_HAS_DATA_TO_RECV)) {
		lbs_deb_usb2(&cardp->udev->dev, "There are data to follow\n");
		lbs_deb_usb2(&cardp->udev->dev, "seqnum = %d totalbytes = %d\n",
			     cardp->fwseqnum, cardp->totalbytes);
	} else if (fwdata->hdr.dnldcmd == cpu_to_le32(FW_HAS_LAST_BLOCK)) {
		lbs_deb_usb2(&cardp->udev->dev, "Host has finished FW downloading\n");
		lbs_deb_usb2(&cardp->udev->dev, "Donwloading FW JUMP BLOCK\n");

		cardp->fwfinalblk = 1;
	}

	lbs_deb_usb2(&cardp->udev->dev, "Firmware download done; size %d\n",
		     cardp->totalbytes);

	return 0;
}

static int if_usb_reset_device(struct if_usb_card *cardp)
{
	struct cmd_header *cmd = cardp->ep_out_buf + 4;
	int ret;

	lbs_deb_enter(LBS_DEB_USB);

	*(__le32 *)cardp->ep_out_buf = cpu_to_le32(CMD_TYPE_REQUEST);

	cmd->command = cpu_to_le16(CMD_802_11_RESET);
	cmd->size = cpu_to_le16(sizeof(cmd));
	cmd->result = cpu_to_le16(0);
	cmd->seqnum = cpu_to_le16(0x5a5a);
	usb_tx_block(cardp, cardp->ep_out_buf, 4 + sizeof(struct cmd_header));

	msleep(100);
	ret = usb_reset_device(cardp->udev);
	msleep(100);

#ifdef CONFIG_OLPC
	if (ret && machine_is_olpc())
		if_usb_reset_olpc_card(NULL);
#endif

	lbs_deb_leave_args(LBS_DEB_USB, "ret %d", ret);

	return ret;
}

/**
 *  usb_tx_block - transfer the data to the device
 *  @cardp: 	pointer to &struct if_usb_card
 *  @payload:	pointer to payload data
 *  @nb:	data length
 *  returns:	0 for success or negative error code
 */
static int usb_tx_block(struct if_usb_card *cardp, uint8_t *payload, uint16_t nb)
{
	int ret;

	/* check if device is removed */
	if (cardp->surprise_removed) {
		lbs_deb_usbd(&cardp->udev->dev, "Device removed\n");
		ret = -ENODEV;
		goto tx_ret;
	}

	usb_fill_bulk_urb(cardp->tx_urb, cardp->udev,
			  usb_sndbulkpipe(cardp->udev,
					  cardp->ep_out),
			  payload, nb, if_usb_write_bulk_callback, cardp);

	cardp->tx_urb->transfer_flags |= URB_ZERO_PACKET;

	if ((ret = usb_submit_urb(cardp->tx_urb, GFP_ATOMIC))) {
		lbs_deb_usbd(&cardp->udev->dev, "usb_submit_urb failed: %d\n", ret);
	} else {
		lbs_deb_usb2(&cardp->udev->dev, "usb_submit_urb success\n");
		ret = 0;
	}

tx_ret:
	return ret;
}

static int __if_usb_submit_rx_urb(struct if_usb_card *cardp,
				  void (*callbackfn)(struct urb *urb))
{
	struct sk_buff *skb;
	int ret = -1;

	if (!(skb = dev_alloc_skb(MRVDRV_ETH_RX_PACKET_BUFFER_SIZE))) {
		pr_err("No free skb\n");
		goto rx_ret;
	}

	cardp->rx_skb = skb;

	/* Fill the receive configuration URB and initialise the Rx call back */
	usb_fill_bulk_urb(cardp->rx_urb, cardp->udev,
			  usb_rcvbulkpipe(cardp->udev, cardp->ep_in),
			  skb->data + IPFIELD_ALIGN_OFFSET,
			  MRVDRV_ETH_RX_PACKET_BUFFER_SIZE, callbackfn,
			  cardp);

	cardp->rx_urb->transfer_flags |= URB_ZERO_PACKET;

	lbs_deb_usb2(&cardp->udev->dev, "Pointer for rx_urb %p\n", cardp->rx_urb);
	if ((ret = usb_submit_urb(cardp->rx_urb, GFP_ATOMIC))) {
		lbs_deb_usbd(&cardp->udev->dev, "Submit Rx URB failed: %d\n", ret);
		kfree_skb(skb);
		cardp->rx_skb = NULL;
		ret = -1;
	} else {
		lbs_deb_usb2(&cardp->udev->dev, "Submit Rx URB success\n");
		ret = 0;
	}

rx_ret:
	return ret;
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
	struct bootcmdresp bootcmdresp;

	if (urb->status) {
		lbs_deb_usbd(&cardp->udev->dev,
			     "URB status is failed during fw load\n");
		kfree_skb(skb);
		return;
	}

	if (cardp->fwdnldover) {
		__le32 *tmp = (__le32 *)(skb->data + IPFIELD_ALIGN_OFFSET);

		if (tmp[0] == cpu_to_le32(CMD_TYPE_INDICATION) &&
		    tmp[1] == cpu_to_le32(MACREG_INT_CODE_FIRMWARE_READY)) {
			pr_info("Firmware ready event received\n");
			wake_up(&cardp->fw_wq);
		} else {
			lbs_deb_usb("Waiting for confirmation; got %x %x\n",
				    le32_to_cpu(tmp[0]), le32_to_cpu(tmp[1]));
			if_usb_submit_rx_urb_fwload(cardp);
		}
		kfree_skb(skb);
		return;
	}
	if (cardp->bootcmdresp <= 0) {
		memcpy (&bootcmdresp, skb->data + IPFIELD_ALIGN_OFFSET,
			sizeof(bootcmdresp));

		if (le16_to_cpu(cardp->udev->descriptor.bcdDevice) < 0x3106) {
			kfree_skb(skb);
			if_usb_submit_rx_urb_fwload(cardp);
			cardp->bootcmdresp = BOOT_CMD_RESP_OK;
			lbs_deb_usbd(&cardp->udev->dev,
				     "Received valid boot command response\n");
			return;
		}
		if (bootcmdresp.magic != cpu_to_le32(BOOT_CMD_MAGIC_NUMBER)) {
			if (bootcmdresp.magic == cpu_to_le32(CMD_TYPE_REQUEST) ||
			    bootcmdresp.magic == cpu_to_le32(CMD_TYPE_DATA) ||
			    bootcmdresp.magic == cpu_to_le32(CMD_TYPE_INDICATION)) {
				if (!cardp->bootcmdresp)
					pr_info("Firmware already seems alive; resetting\n");
				cardp->bootcmdresp = -1;
			} else {
				pr_info("boot cmd response wrong magic number (0x%x)\n",
					    le32_to_cpu(bootcmdresp.magic));
			}
		} else if ((bootcmdresp.cmd != BOOT_CMD_FW_BY_USB) &&
			   (bootcmdresp.cmd != BOOT_CMD_UPDATE_FW) &&
			   (bootcmdresp.cmd != BOOT_CMD_UPDATE_BOOT2)) {
			pr_info("boot cmd response cmd_tag error (%d)\n",
				bootcmdresp.cmd);
		} else if (bootcmdresp.result != BOOT_CMD_RESP_OK) {
			pr_info("boot cmd response result error (%d)\n",
				bootcmdresp.result);
		} else {
			cardp->bootcmdresp = 1;
			lbs_deb_usbd(&cardp->udev->dev,
				     "Received valid boot command response\n");
		}
		kfree_skb(skb);
		if_usb_submit_rx_urb_fwload(cardp);
		return;
	}

	syncfwheader = kmemdup(skb->data + IPFIELD_ALIGN_OFFSET,
			       sizeof(struct fwsyncheader), GFP_ATOMIC);
	if (!syncfwheader) {
		lbs_deb_usbd(&cardp->udev->dev, "Failure to allocate syncfwheader\n");
		kfree_skb(skb);
		return;
	}

	if (!syncfwheader->cmd) {
		lbs_deb_usb2(&cardp->udev->dev, "FW received Blk with correct CRC\n");
		lbs_deb_usb2(&cardp->udev->dev, "FW received Blk seqnum = %d\n",
			     le32_to_cpu(syncfwheader->seqnum));
		cardp->CRC_OK = 1;
	} else {
		lbs_deb_usbd(&cardp->udev->dev, "FW received Blk with CRC error\n");
		cardp->CRC_OK = 0;
	}

	kfree_skb(skb);

	/* Give device 5s to either write firmware to its RAM or eeprom */
	mod_timer(&cardp->fw_timeout, jiffies + (HZ*5));

	if (cardp->fwfinalblk) {
		cardp->fwdnldover = 1;
		goto exit;
	}

	if_usb_send_fw_pkt(cardp);

 exit:
	if_usb_submit_rx_urb_fwload(cardp);

	kfree(syncfwheader);
}

#define MRVDRV_MIN_PKT_LEN	30

static inline void process_cmdtypedata(int recvlength, struct sk_buff *skb,
				       struct if_usb_card *cardp,
				       struct lbs_private *priv)
{
	if (recvlength > MRVDRV_ETH_RX_PACKET_BUFFER_SIZE + MESSAGE_HEADER_LEN
	    || recvlength < MRVDRV_MIN_PKT_LEN) {
		lbs_deb_usbd(&cardp->udev->dev, "Packet length is Invalid\n");
		kfree_skb(skb);
		return;
	}

	skb_reserve(skb, IPFIELD_ALIGN_OFFSET);
	skb_put(skb, recvlength);
	skb_pull(skb, MESSAGE_HEADER_LEN);

	lbs_process_rxed_packet(priv, skb);
}

static inline void process_cmdrequest(int recvlength, uint8_t *recvbuff,
				      struct sk_buff *skb,
				      struct if_usb_card *cardp,
				      struct lbs_private *priv)
{
	u8 i;

	if (recvlength > LBS_CMD_BUFFER_SIZE) {
		lbs_deb_usbd(&cardp->udev->dev,
			     "The receive buffer is too large\n");
		kfree_skb(skb);
		return;
	}

	BUG_ON(!in_interrupt());

	spin_lock(&priv->driver_lock);

	i = (priv->resp_idx == 0) ? 1 : 0;
	BUG_ON(priv->resp_len[i]);
	priv->resp_len[i] = (recvlength - MESSAGE_HEADER_LEN);
	memcpy(priv->resp_buf[i], recvbuff + MESSAGE_HEADER_LEN,
		priv->resp_len[i]);
	kfree_skb(skb);
	lbs_notify_command_response(priv, i);

	spin_unlock(&priv->driver_lock);

	lbs_deb_usbd(&cardp->udev->dev,
		    "Wake up main thread to handle cmd response\n");
}

/**
 *  if_usb_receive - read the packet into the upload buffer,
 *  wake up the main thread and initialise the Rx callack
 *
 *  @urb:	pointer to &struct urb
 *  returns:	N/A
 */
static void if_usb_receive(struct urb *urb)
{
	struct if_usb_card *cardp = urb->context;
	struct sk_buff *skb = cardp->rx_skb;
	struct lbs_private *priv = cardp->priv;
	int recvlength = urb->actual_length;
	uint8_t *recvbuff = NULL;
	uint32_t recvtype = 0;
	__le32 *pkt = (__le32 *)(skb->data + IPFIELD_ALIGN_OFFSET);
	uint32_t event;

	lbs_deb_enter(LBS_DEB_USB);

	if (recvlength) {
		if (urb->status) {
			lbs_deb_usbd(&cardp->udev->dev, "RX URB failed: %d\n",
				     urb->status);
			kfree_skb(skb);
			goto setup_for_next;
		}

		recvbuff = skb->data + IPFIELD_ALIGN_OFFSET;
		recvtype = le32_to_cpu(pkt[0]);
		lbs_deb_usbd(&cardp->udev->dev,
			    "Recv length = 0x%x, Recv type = 0x%X\n",
			    recvlength, recvtype);
	} else if (urb->status) {
		kfree_skb(skb);
		goto rx_exit;
	}

	switch (recvtype) {
	case CMD_TYPE_DATA:
		process_cmdtypedata(recvlength, skb, cardp, priv);
		break;

	case CMD_TYPE_REQUEST:
		process_cmdrequest(recvlength, recvbuff, skb, cardp, priv);
		break;

	case CMD_TYPE_INDICATION:
		/* Event handling */
		event = le32_to_cpu(pkt[1]);
		lbs_deb_usbd(&cardp->udev->dev, "**EVENT** 0x%X\n", event);
		kfree_skb(skb);

		/* Icky undocumented magic special case */
		if (event & 0xffff0000) {
			u32 trycount = (event & 0xffff0000) >> 16;

			lbs_send_tx_feedback(priv, trycount);
		} else
			lbs_queue_event(priv, event & 0xFF);
		break;

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
 *  if_usb_host_to_card - downloads data to FW
 *  @priv:	pointer to &struct lbs_private structure
 *  @type:	type of data
 *  @payload:	pointer to data buffer
 *  @nb:	number of bytes
 *  returns:	0 for success or negative error code
 */
static int if_usb_host_to_card(struct lbs_private *priv, uint8_t type,
			       uint8_t *payload, uint16_t nb)
{
	struct if_usb_card *cardp = priv->card;

	lbs_deb_usbd(&cardp->udev->dev,"*** type = %u\n", type);
	lbs_deb_usbd(&cardp->udev->dev,"size after = %d\n", nb);

	if (type == MVMS_CMD) {
		*(__le32 *)cardp->ep_out_buf = cpu_to_le32(CMD_TYPE_REQUEST);
		priv->dnld_sent = DNLD_CMD_SENT;
	} else {
		*(__le32 *)cardp->ep_out_buf = cpu_to_le32(CMD_TYPE_DATA);
		priv->dnld_sent = DNLD_DATA_SENT;
	}

	memcpy((cardp->ep_out_buf + MESSAGE_HEADER_LEN), payload, nb);

	return usb_tx_block(cardp, cardp->ep_out_buf, nb + MESSAGE_HEADER_LEN);
}

/**
 *  if_usb_issue_boot_command - issues Boot command to the Boot2 code
 *  @cardp:	pointer to &if_usb_card
 *  @ivalue:	1:Boot from FW by USB-Download
 *		2:Boot from FW in EEPROM
 *  returns:	0 for success or negative error code
 */
static int if_usb_issue_boot_command(struct if_usb_card *cardp, int ivalue)
{
	struct bootcmd *bootcmd = cardp->ep_out_buf;

	/* Prepare command */
	bootcmd->magic = cpu_to_le32(BOOT_CMD_MAGIC_NUMBER);
	bootcmd->cmd = ivalue;
	memset(bootcmd->pad, 0, sizeof(bootcmd->pad));

	/* Issue command */
	usb_tx_block(cardp, cardp->ep_out_buf, sizeof(*bootcmd));

	return 0;
}


/**
 *  check_fwfile_format - check the validity of Boot2/FW image
 *
 *  @data:	pointer to image
 *  @totlen:	image length
 *  returns:     0 (good) or 1 (failure)
 */
static int check_fwfile_format(const uint8_t *data, uint32_t totlen)
{
	uint32_t bincmd, exit;
	uint32_t blksize, offset, len;
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
		pr_err("firmware file format check FAIL\n");
	else
		lbs_deb_fw("firmware file format check PASS\n");

	return ret;
}


/**
*  if_usb_prog_firmware - programs the firmware subject to cmd
*
*  @cardp:	the if_usb_card descriptor
*  @fwname:	firmware or boot2 image file name
*  @cmd:	either BOOT_CMD_FW_BY_USB, BOOT_CMD_UPDATE_FW,
*		or BOOT_CMD_UPDATE_BOOT2.
*  returns:	0 or error code
*/
static int if_usb_prog_firmware(struct if_usb_card *cardp,
				const char *fwname, int cmd)
{
	struct lbs_private *priv = cardp->priv;
	unsigned long flags, caps;
	int ret;

	caps = priv->fwcapinfo;
	if (((cmd == BOOT_CMD_UPDATE_FW) && !(caps & FW_CAPINFO_FIRMWARE_UPGRADE)) ||
	    ((cmd == BOOT_CMD_UPDATE_BOOT2) && !(caps & FW_CAPINFO_BOOT2_UPGRADE)))
		return -EOPNOTSUPP;

	/* Ensure main thread is idle. */
	spin_lock_irqsave(&priv->driver_lock, flags);
	while (priv->cur_cmd != NULL || priv->dnld_sent != DNLD_RES_RECEIVED) {
		spin_unlock_irqrestore(&priv->driver_lock, flags);
		if (wait_event_interruptible(priv->waitq,
				(priv->cur_cmd == NULL &&
				priv->dnld_sent == DNLD_RES_RECEIVED))) {
			return -ERESTARTSYS;
		}
		spin_lock_irqsave(&priv->driver_lock, flags);
	}
	priv->dnld_sent = DNLD_BOOTCMD_SENT;
	spin_unlock_irqrestore(&priv->driver_lock, flags);

	ret = __if_usb_prog_firmware(cardp, fwname, cmd);

	spin_lock_irqsave(&priv->driver_lock, flags);
	priv->dnld_sent = DNLD_RES_RECEIVED;
	spin_unlock_irqrestore(&priv->driver_lock, flags);

	wake_up(&priv->waitq);

	return ret;
}

/* table of firmware file names */
static const struct {
	u32 model;
	const char *fwname;
} fw_table[] = {
	{ MODEL_8388, "libertas/usb8388_v9.bin" },
	{ MODEL_8388, "libertas/usb8388_v5.bin" },
	{ MODEL_8388, "libertas/usb8388.bin" },
	{ MODEL_8388, "usb8388.bin" },
	{ MODEL_8682, "libertas/usb8682.bin" }
};

#ifdef CONFIG_OLPC

static int try_olpc_fw(struct if_usb_card *cardp)
{
	int retval = -ENOENT;

	/* try the OLPC firmware first; fall back to fw_table list */
	if (machine_is_olpc() && cardp->model == MODEL_8388)
		retval = request_firmware(&cardp->fw,
				"libertas/usb8388_olpc.bin", &cardp->udev->dev);
	return retval;
}

#else
static int try_olpc_fw(struct if_usb_card *cardp) { return -ENOENT; }
#endif /* !CONFIG_OLPC */

static int get_fw(struct if_usb_card *cardp, const char *fwname)
{
	int i;

	/* Try user-specified firmware first */
	if (fwname)
		return request_firmware(&cardp->fw, fwname, &cardp->udev->dev);

	/* Handle OLPC firmware */
	if (try_olpc_fw(cardp) == 0)
		return 0;

	/* Otherwise search for firmware to use */
	for (i = 0; i < ARRAY_SIZE(fw_table); i++) {
		if (fw_table[i].model != cardp->model)
			continue;
		if (request_firmware(&cardp->fw, fw_table[i].fwname,
					&cardp->udev->dev) == 0)
			return 0;
	}

	return -ENOENT;
}

static int __if_usb_prog_firmware(struct if_usb_card *cardp,
					const char *fwname, int cmd)
{
	int i = 0;
	static int reset_count = 10;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_USB);

	ret = get_fw(cardp, fwname);
	if (ret) {
		pr_err("failed to find firmware (%d)\n", ret);
		goto done;
	}

	if (check_fwfile_format(cardp->fw->data, cardp->fw->size)) {
		ret = -EINVAL;
		goto release_fw;
	}

	/* Cancel any pending usb business */
	usb_kill_urb(cardp->rx_urb);
	usb_kill_urb(cardp->tx_urb);

	cardp->fwlastblksent = 0;
	cardp->fwdnldover = 0;
	cardp->totalbytes = 0;
	cardp->fwfinalblk = 0;
	cardp->bootcmdresp = 0;

restart:
	if (if_usb_submit_rx_urb_fwload(cardp) < 0) {
		lbs_deb_usbd(&cardp->udev->dev, "URB submission is failed\n");
		ret = -EIO;
		goto release_fw;
	}

	cardp->bootcmdresp = 0;
	do {
		int j = 0;
		i++;
		if_usb_issue_boot_command(cardp, cmd);
		/* wait for command response */
		do {
			j++;
			msleep_interruptible(100);
		} while (cardp->bootcmdresp == 0 && j < 10);
	} while (cardp->bootcmdresp == 0 && i < 5);

	if (cardp->bootcmdresp == BOOT_CMD_RESP_NOT_SUPPORTED) {
		/* Return to normal operation */
		ret = -EOPNOTSUPP;
		usb_kill_urb(cardp->rx_urb);
		usb_kill_urb(cardp->tx_urb);
		if (if_usb_submit_rx_urb(cardp) < 0)
			ret = -EIO;
		goto release_fw;
	} else if (cardp->bootcmdresp <= 0) {
		if (--reset_count >= 0) {
			if_usb_reset_device(cardp);
			goto restart;
		}
		ret = -EIO;
		goto release_fw;
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
	wait_event_interruptible(cardp->fw_wq, cardp->surprise_removed || cardp->fwdnldover);

	del_timer_sync(&cardp->fw_timeout);
	usb_kill_urb(cardp->rx_urb);

	if (!cardp->fwdnldover) {
		pr_info("failed to load fw, resetting device!\n");
		if (--reset_count >= 0) {
			if_usb_reset_device(cardp);
			goto restart;
		}

		pr_info("FW download failure, time = %d ms\n", i * 100);
		ret = -EIO;
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
	struct if_usb_card *cardp = usb_get_intfdata(intf);
	struct lbs_private *priv = cardp->priv;
	int ret;

	lbs_deb_enter(LBS_DEB_USB);

	if (priv->psstate != PS_STATE_FULL_POWER)
		return -1;

#ifdef CONFIG_OLPC
	if (machine_is_olpc()) {
		if (priv->wol_criteria == EHS_REMOVE_WAKEUP)
			olpc_ec_wakeup_clear(EC_SCI_SRC_WLAN);
		else
			olpc_ec_wakeup_set(EC_SCI_SRC_WLAN);
	}
#endif

	ret = lbs_suspend(priv);
	if (ret)
		goto out;

	/* Unlink tx & rx urb */
	usb_kill_urb(cardp->tx_urb);
	usb_kill_urb(cardp->rx_urb);

 out:
	lbs_deb_leave(LBS_DEB_USB);
	return ret;
}

static int if_usb_resume(struct usb_interface *intf)
{
	struct if_usb_card *cardp = usb_get_intfdata(intf);
	struct lbs_private *priv = cardp->priv;

	lbs_deb_enter(LBS_DEB_USB);

	if_usb_submit_rx_urb(cardp);

	lbs_resume(priv);

	lbs_deb_leave(LBS_DEB_USB);
	return 0;
}
#else
#define if_usb_suspend NULL
#define if_usb_resume NULL
#endif

static struct usb_driver if_usb_driver = {
	.name = DRV_NAME,
	.probe = if_usb_probe,
	.disconnect = if_usb_disconnect,
	.id_table = if_usb_table,
	.suspend = if_usb_suspend,
	.resume = if_usb_resume,
	.reset_resume = if_usb_resume,
};

static int __init if_usb_init_module(void)
{
	int ret = 0;

	lbs_deb_enter(LBS_DEB_MAIN);

	ret = usb_register(&if_usb_driver);

	lbs_deb_leave_args(LBS_DEB_MAIN, "ret %d", ret);
	return ret;
}

static void __exit if_usb_exit_module(void)
{
	lbs_deb_enter(LBS_DEB_MAIN);

	usb_deregister(&if_usb_driver);

	lbs_deb_leave(LBS_DEB_MAIN);
}

module_init(if_usb_init_module);
module_exit(if_usb_exit_module);

MODULE_DESCRIPTION("8388 USB WLAN Driver");
MODULE_AUTHOR("Marvell International Ltd. and Red Hat, Inc.");
MODULE_LICENSE("GPL");
