/**
 * Copyright (c) 2014 Redpine Signals Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <linux/module.h>
#include "rsi_usb.h"

/**
 * rsi_usb_card_write() - This function writes to the USB Card.
 * @adapter: Pointer to the adapter structure.
 * @buf: Pointer to the buffer from where the data has to be taken.
 * @len: Length to be written.
 * @endpoint: Type of endpoint.
 *
 * Return: status: 0 on success, a negative error code on failure.
 */
static int rsi_usb_card_write(struct rsi_hw *adapter,
			      void *buf,
			      u16 len,
			      u8 endpoint)
{
	struct rsi_91x_usbdev *dev = (struct rsi_91x_usbdev *)adapter->rsi_dev;
	int status;
	s32 transfer;

	status = usb_bulk_msg(dev->usbdev,
			      usb_sndbulkpipe(dev->usbdev,
			      dev->bulkout_endpoint_addr[endpoint - 1]),
			      buf,
			      len,
			      &transfer,
			      HZ * 5);

	if (status < 0) {
		rsi_dbg(ERR_ZONE,
			"Card write failed with error code :%10d\n", status);
		dev->write_fail = 1;
	}
	return status;
}

/**
 * rsi_write_multiple() - This function writes multiple bytes of information
 *			  to the USB card.
 * @adapter: Pointer to the adapter structure.
 * @addr: Address of the register.
 * @data: Pointer to the data that has to be written.
 * @count: Number of multiple bytes to be written.
 *
 * Return: 0 on success, a negative error code on failure.
 */
static int rsi_write_multiple(struct rsi_hw *adapter,
			      u8 endpoint,
			      u8 *data,
			      u32 count)
{
	struct rsi_91x_usbdev *dev = (struct rsi_91x_usbdev *)adapter->rsi_dev;
	u8 *seg = dev->tx_buffer;

	if (dev->write_fail)
		return 0;

	if (endpoint == MGMT_EP) {
		memset(seg, 0, RSI_USB_TX_HEAD_ROOM);
		memcpy(seg + RSI_USB_TX_HEAD_ROOM, data, count);
	} else {
		seg = ((u8 *)data - RSI_USB_TX_HEAD_ROOM);
	}

	return rsi_usb_card_write(adapter,
				  seg,
				  count + RSI_USB_TX_HEAD_ROOM,
				  endpoint);
}

/**
 * rsi_find_bulk_in_and_out_endpoints() - This function initializes the bulk
 *					  endpoints to the device.
 * @interface: Pointer to the USB interface structure.
 * @adapter: Pointer to the adapter structure.
 *
 * Return: ret_val: 0 on success, -ENOMEM on failure.
 */
static int rsi_find_bulk_in_and_out_endpoints(struct usb_interface *interface,
					      struct rsi_hw *adapter)
{
	struct rsi_91x_usbdev *dev = (struct rsi_91x_usbdev *)adapter->rsi_dev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	__le16 buffer_size;
	int ii, bep_found = 0;

	iface_desc = &(interface->altsetting[0]);

	for (ii = 0; ii < iface_desc->desc.bNumEndpoints; ++ii) {
		endpoint = &(iface_desc->endpoint[ii].desc);

		if ((!(dev->bulkin_endpoint_addr)) &&
		    (endpoint->bEndpointAddress & USB_DIR_IN) &&
		    ((endpoint->bmAttributes &
		    USB_ENDPOINT_XFERTYPE_MASK) ==
		    USB_ENDPOINT_XFER_BULK)) {
			buffer_size = endpoint->wMaxPacketSize;
			dev->bulkin_size = buffer_size;
			dev->bulkin_endpoint_addr =
				endpoint->bEndpointAddress;
		}

		if (!dev->bulkout_endpoint_addr[bep_found] &&
		    !(endpoint->bEndpointAddress & USB_DIR_IN) &&
		    ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
		      USB_ENDPOINT_XFER_BULK)) {
			dev->bulkout_endpoint_addr[bep_found] =
				endpoint->bEndpointAddress;
			buffer_size = endpoint->wMaxPacketSize;
			dev->bulkout_size[bep_found] = buffer_size;
			bep_found++;
		}

		if (bep_found >= MAX_BULK_EP)
			break;
	}

	if (!(dev->bulkin_endpoint_addr) &&
	    (dev->bulkout_endpoint_addr[0]))
		return -EINVAL;

	return 0;
}

/* rsi_usb_reg_read() - This function reads data from given register address.
 * @usbdev: Pointer to the usb_device structure.
 * @reg: Address of the register to be read.
 * @value: Value to be read.
 * @len: length of data to be read.
 *
 * Return: status: 0 on success, a negative error code on failure.
 */
static int rsi_usb_reg_read(struct usb_device *usbdev,
			    u32 reg,
			    u16 *value,
			    u16 len)
{
	u8 *buf;
	int status = -ENOMEM;

	buf  = kmalloc(0x04, GFP_KERNEL);
	if (!buf)
		return status;

	status = usb_control_msg(usbdev,
				 usb_rcvctrlpipe(usbdev, 0),
				 USB_VENDOR_REGISTER_READ,
				 USB_TYPE_VENDOR,
				 ((reg & 0xffff0000) >> 16), (reg & 0xffff),
				 (void *)buf,
				 len,
				 HZ * 5);

	*value = (buf[0] | (buf[1] << 8));
	if (status < 0) {
		rsi_dbg(ERR_ZONE,
			"%s: Reg read failed with error code :%d\n",
			__func__, status);
	}
	kfree(buf);

	return status;
}

/**
 * rsi_usb_reg_write() - This function writes the given data into the given
 *			 register address.
 * @usbdev: Pointer to the usb_device structure.
 * @reg: Address of the register.
 * @value: Value to write.
 * @len: Length of data to be written.
 *
 * Return: status: 0 on success, a negative error code on failure.
 */
static int rsi_usb_reg_write(struct usb_device *usbdev,
			     u32 reg,
			     u16 value,
			     u16 len)
{
	u8 *usb_reg_buf;
	int status = -ENOMEM;

	usb_reg_buf  = kmalloc(0x04, GFP_KERNEL);
	if (!usb_reg_buf)
		return status;

	usb_reg_buf[0] = (value & 0x00ff);
	usb_reg_buf[1] = (value & 0xff00) >> 8;
	usb_reg_buf[2] = 0x0;
	usb_reg_buf[3] = 0x0;

	status = usb_control_msg(usbdev,
				 usb_sndctrlpipe(usbdev, 0),
				 USB_VENDOR_REGISTER_WRITE,
				 USB_TYPE_VENDOR,
				 ((reg & 0xffff0000) >> 16),
				 (reg & 0xffff),
				 (void *)usb_reg_buf,
				 len,
				 HZ * 5);
	if (status < 0) {
		rsi_dbg(ERR_ZONE,
			"%s: Reg write failed with error code :%d\n",
			__func__, status);
	}
	kfree(usb_reg_buf);

	return status;
}

/**
 * rsi_rx_done_handler() - This function is called when a packet is received
 *			   from USB stack. This is callback to recieve done.
 * @urb: Received URB.
 *
 * Return: None.
 */
static void rsi_rx_done_handler(struct urb *urb)
{
	struct rsi_hw *adapter = urb->context;
	struct rsi_91x_usbdev *dev = (struct rsi_91x_usbdev *)adapter->rsi_dev;

	if (urb->status)
		return;

	rsi_set_event(&dev->rx_thread.event);
}

/**
 * rsi_rx_urb_submit() - This function submits the given URB to the USB stack.
 * @adapter: Pointer to the adapter structure.
 *
 * Return: 0 on success, a negative error code on failure.
 */
static int rsi_rx_urb_submit(struct rsi_hw *adapter)
{
	struct rsi_91x_usbdev *dev = (struct rsi_91x_usbdev *)adapter->rsi_dev;
	struct urb *urb = dev->rx_usb_urb[0];
	int status;

	usb_fill_bulk_urb(urb,
			  dev->usbdev,
			  usb_rcvbulkpipe(dev->usbdev,
				dev->bulkin_endpoint_addr),
			  urb->transfer_buffer,
			  3000,
			  rsi_rx_done_handler,
			  adapter);

	status = usb_submit_urb(urb, GFP_KERNEL);
	if (status)
		rsi_dbg(ERR_ZONE, "%s: Failed in urb submission\n", __func__);

	return status;
}

/**
 * rsi_usb_write_register_multiple() - This function writes multiple bytes of
 *				       information to multiple registers.
 * @adapter: Pointer to the adapter structure.
 * @addr: Address of the register.
 * @data: Pointer to the data that has to be written.
 * @count: Number of multiple bytes to be written on to the registers.
 *
 * Return: status: 0 on success, a negative error code on failure.
 */
int rsi_usb_write_register_multiple(struct rsi_hw *adapter,
				    u32 addr,
				    u8 *data,
				    u32 count)
{
	struct rsi_91x_usbdev *dev = (struct rsi_91x_usbdev *)adapter->rsi_dev;
	u8 *buf;
	u8 transfer;
	int status = 0;

	buf = kzalloc(4096, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	while (count) {
		transfer = (u8)(min_t(u32, count, 4096));
		memcpy(buf, data, transfer);
		status = usb_control_msg(dev->usbdev,
					 usb_sndctrlpipe(dev->usbdev, 0),
					 USB_VENDOR_REGISTER_WRITE,
					 USB_TYPE_VENDOR,
					 ((addr & 0xffff0000) >> 16),
					 (addr & 0xffff),
					 (void *)buf,
					 transfer,
					 HZ * 5);
		if (status < 0) {
			rsi_dbg(ERR_ZONE,
				"Reg write failed with error code :%d\n",
				status);
		} else {
			count -= transfer;
			data += transfer;
			addr += transfer;
		}
	}

	kfree(buf);
	return 0;
}

/**
 *rsi_usb_host_intf_write_pkt() - This function writes the packet to the
 *				   USB card.
 * @adapter: Pointer to the adapter structure.
 * @pkt: Pointer to the data to be written on to the card.
 * @len: Length of the data to be written on to the card.
 *
 * Return: 0 on success, a negative error code on failure.
 */
static int rsi_usb_host_intf_write_pkt(struct rsi_hw *adapter,
				       u8 *pkt,
				       u32 len)
{
	u32 queueno = ((pkt[1] >> 4) & 0xf);
	u8 endpoint;

	endpoint = ((queueno == RSI_WIFI_MGMT_Q) ? MGMT_EP : DATA_EP);

	return rsi_write_multiple(adapter,
				  endpoint,
				  (u8 *)pkt,
				  len);
}

/**
 * rsi_deinit_usb_interface() - This function deinitializes the usb interface.
 * @adapter: Pointer to the adapter structure.
 *
 * Return: None.
 */
static void rsi_deinit_usb_interface(struct rsi_hw *adapter)
{
	struct rsi_91x_usbdev *dev = (struct rsi_91x_usbdev *)adapter->rsi_dev;

	rsi_kill_thread(&dev->rx_thread);
	usb_free_urb(dev->rx_usb_urb[0]);
	kfree(adapter->priv->rx_data_pkt);
	kfree(dev->tx_buffer);
}

/**
 * rsi_init_usb_interface() - This function initializes the usb interface.
 * @adapter: Pointer to the adapter structure.
 * @pfunction: Pointer to USB interface structure.
 *
 * Return: 0 on success, a negative error code on failure.
 */
static int rsi_init_usb_interface(struct rsi_hw *adapter,
				  struct usb_interface *pfunction)
{
	struct rsi_91x_usbdev *rsi_dev;
	struct rsi_common *common = adapter->priv;
	int status;

	rsi_dev = kzalloc(sizeof(*rsi_dev), GFP_KERNEL);
	if (!rsi_dev)
		return -ENOMEM;

	adapter->rsi_dev = rsi_dev;
	rsi_dev->usbdev = interface_to_usbdev(pfunction);

	if (rsi_find_bulk_in_and_out_endpoints(pfunction, adapter))
		return -EINVAL;

	adapter->device = &pfunction->dev;
	usb_set_intfdata(pfunction, adapter);

	common->rx_data_pkt = kmalloc(2048, GFP_KERNEL);
	if (!common->rx_data_pkt) {
		rsi_dbg(ERR_ZONE, "%s: Failed to allocate memory\n",
			__func__);
		return -ENOMEM;
	}

	rsi_dev->tx_buffer = kmalloc(2048, GFP_KERNEL);
	if (!rsi_dev->tx_buffer) {
		status = -ENOMEM;
		goto fail_tx;
	}
	rsi_dev->rx_usb_urb[0] = usb_alloc_urb(0, GFP_KERNEL);
	if (!rsi_dev->rx_usb_urb[0]) {
		status = -ENOMEM;
		goto fail_rx;
	}
	rsi_dev->rx_usb_urb[0]->transfer_buffer = adapter->priv->rx_data_pkt;
	rsi_dev->tx_blk_size = 252;

	/* Initializing function callbacks */
	adapter->rx_urb_submit = rsi_rx_urb_submit;
	adapter->host_intf_write_pkt = rsi_usb_host_intf_write_pkt;
	adapter->check_hw_queue_status = rsi_usb_check_queue_status;
	adapter->determine_event_timeout = rsi_usb_event_timeout;

	rsi_init_event(&rsi_dev->rx_thread.event);
	status = rsi_create_kthread(common, &rsi_dev->rx_thread,
				    rsi_usb_rx_thread, "RX-Thread");
	if (status) {
		rsi_dbg(ERR_ZONE, "%s: Unable to init rx thrd\n", __func__);
		goto fail_thread;
	}

#ifdef CONFIG_RSI_DEBUGFS
	/* In USB, one less than the MAX_DEBUGFS_ENTRIES entries is required */
	adapter->num_debugfs_entries = (MAX_DEBUGFS_ENTRIES - 1);
#endif

	rsi_dbg(INIT_ZONE, "%s: Enabled the interface\n", __func__);
	return 0;

fail_thread:
	usb_free_urb(rsi_dev->rx_usb_urb[0]);
fail_rx:
	kfree(rsi_dev->tx_buffer);
fail_tx:
	kfree(common->rx_data_pkt);
	return status;
}

/**
 * rsi_probe() - This function is called by kernel when the driver provided
 *		 Vendor and device IDs are matched. All the initialization
 *		 work is done here.
 * @pfunction: Pointer to the USB interface structure.
 * @id: Pointer to the usb_device_id structure.
 *
 * Return: 0 on success, a negative error code on failure.
 */
static int rsi_probe(struct usb_interface *pfunction,
		     const struct usb_device_id *id)
{
	struct rsi_hw *adapter;
	struct rsi_91x_usbdev *dev;
	u16 fw_status;
	int status;

	rsi_dbg(INIT_ZONE, "%s: Init function called\n", __func__);

	adapter = rsi_91x_init();
	if (!adapter) {
		rsi_dbg(ERR_ZONE, "%s: Failed to init os intf ops\n",
			__func__);
		return -ENOMEM;
	}

	status = rsi_init_usb_interface(adapter, pfunction);
	if (status) {
		rsi_dbg(ERR_ZONE, "%s: Failed to init usb interface\n",
			__func__);
		goto err;
	}

	rsi_dbg(ERR_ZONE, "%s: Initialized os intf ops\n", __func__);

	dev = (struct rsi_91x_usbdev *)adapter->rsi_dev;

	status = rsi_usb_reg_read(dev->usbdev, FW_STATUS_REG, &fw_status, 2);
	if (status)
		goto err1;
	else
		fw_status &= 1;

	if (!fw_status) {
		status = rsi_usb_device_init(adapter->priv);
		if (status) {
			rsi_dbg(ERR_ZONE, "%s: Failed in device init\n",
				__func__);
			goto err1;
		}

		status = rsi_usb_reg_write(dev->usbdev,
					   USB_INTERNAL_REG_1,
					   RSI_USB_READY_MAGIC_NUM, 1);
		if (status)
			goto err1;
		rsi_dbg(INIT_ZONE, "%s: Performed device init\n", __func__);
	}

	status = rsi_rx_urb_submit(adapter);
	if (status)
		goto err1;

	return 0;
err1:
	rsi_deinit_usb_interface(adapter);
err:
	rsi_91x_deinit(adapter);
	rsi_dbg(ERR_ZONE, "%s: Failed in probe...Exiting\n", __func__);
	return status;
}

/**
 * rsi_disconnect() - This function performs the reverse of the probe function,
 *		      it deinitialize the driver structure.
 * @pfunction: Pointer to the USB interface structure.
 *
 * Return: None.
 */
static void rsi_disconnect(struct usb_interface *pfunction)
{
	struct rsi_hw *adapter = usb_get_intfdata(pfunction);

	if (!adapter)
		return;

	rsi_mac80211_detach(adapter);
	rsi_deinit_usb_interface(adapter);
	rsi_91x_deinit(adapter);

	rsi_dbg(INFO_ZONE, "%s: Deinitialization completed\n", __func__);
}

#ifdef CONFIG_PM
static int rsi_suspend(struct usb_interface *intf, pm_message_t message)
{
	/* Not yet implemented */
	return -ENOSYS;
}

static int rsi_resume(struct usb_interface *intf)
{
	/* Not yet implemented */
	return -ENOSYS;
}
#endif

static const struct usb_device_id rsi_dev_table[] = {
	{ USB_DEVICE(0x0303, 0x0100) },
	{ USB_DEVICE(0x041B, 0x0301) },
	{ USB_DEVICE(0x041B, 0x0201) },
	{ USB_DEVICE(0x041B, 0x9330) },
	{ /* Blank */},
};

static struct usb_driver rsi_driver = {
	.name       = "RSI-USB WLAN",
	.probe      = rsi_probe,
	.disconnect = rsi_disconnect,
	.id_table   = rsi_dev_table,
#ifdef CONFIG_PM
	.suspend    = rsi_suspend,
	.resume     = rsi_resume,
#endif
};

module_usb_driver(rsi_driver);

MODULE_AUTHOR("Redpine Signals Inc");
MODULE_DESCRIPTION("Common USB layer for RSI drivers");
MODULE_SUPPORTED_DEVICE("RSI-91x");
MODULE_DEVICE_TABLE(usb, rsi_dev_table);
MODULE_FIRMWARE(FIRMWARE_RSI9113);
MODULE_VERSION("0.1");
MODULE_LICENSE("Dual BSD/GPL");
