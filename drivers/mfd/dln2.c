// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the Diolan DLN-2 USB adapter
 *
 * Copyright (c) 2014 Intel Corporation
 *
 * Derived from:
 *  i2c-diolan-u2c.c
 *  Copyright (c) 2010-2011 Ericsson AB
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/dln2.h>
#include <linux/rculist.h>

struct dln2_header {
	__le16 size;
	__le16 id;
	__le16 echo;
	__le16 handle;
};

struct dln2_response {
	struct dln2_header hdr;
	__le16 result;
};

#define DLN2_GENERIC_MODULE_ID		0x00
#define DLN2_GENERIC_CMD(cmd)		DLN2_CMD(cmd, DLN2_GENERIC_MODULE_ID)
#define CMD_GET_DEVICE_VER		DLN2_GENERIC_CMD(0x30)
#define CMD_GET_DEVICE_SN		DLN2_GENERIC_CMD(0x31)

#define DLN2_HW_ID			0x200
#define DLN2_USB_TIMEOUT		200	/* in ms */
#define DLN2_MAX_RX_SLOTS		16
#define DLN2_MAX_URBS			16
#define DLN2_RX_BUF_SIZE		512

enum dln2_handle {
	DLN2_HANDLE_EVENT = 0,		/* don't change, hardware defined */
	DLN2_HANDLE_CTRL,
	DLN2_HANDLE_GPIO,
	DLN2_HANDLE_I2C,
	DLN2_HANDLE_SPI,
	DLN2_HANDLE_ADC,
	DLN2_HANDLES
};

/*
 * Receive context used between the receive demultiplexer and the transfer
 * routine. While sending a request the transfer routine will look for a free
 * receive context and use it to wait for a response and to receive the URB and
 * thus the response data.
 */
struct dln2_rx_context {
	/* completion used to wait for a response */
	struct completion done;

	/* if non-NULL the URB contains the response */
	struct urb *urb;

	/* if true then this context is used to wait for a response */
	bool in_use;
};

/*
 * Receive contexts for a particular DLN2 module (i2c, gpio, etc.). We use the
 * handle header field to identify the module in dln2_dev.mod_rx_slots and then
 * the echo header field to index the slots field and find the receive context
 * for a particular request.
 */
struct dln2_mod_rx_slots {
	/* RX slots bitmap */
	DECLARE_BITMAP(bmap, DLN2_MAX_RX_SLOTS);

	/* used to wait for a free RX slot */
	wait_queue_head_t wq;

	/* used to wait for an RX operation to complete */
	struct dln2_rx_context slots[DLN2_MAX_RX_SLOTS];

	/* avoid races between alloc/free_rx_slot and dln2_rx_transfer */
	spinlock_t lock;
};

struct dln2_dev {
	struct usb_device *usb_dev;
	struct usb_interface *interface;
	u8 ep_in;
	u8 ep_out;

	struct urb *rx_urb[DLN2_MAX_URBS];
	void *rx_buf[DLN2_MAX_URBS];

	struct dln2_mod_rx_slots mod_rx_slots[DLN2_HANDLES];

	struct list_head event_cb_list;
	spinlock_t event_cb_lock;

	bool disconnect;
	int active_transfers;
	wait_queue_head_t disconnect_wq;
	spinlock_t disconnect_lock;
};

struct dln2_event_cb_entry {
	struct list_head list;
	u16 id;
	struct platform_device *pdev;
	dln2_event_cb_t callback;
};

int dln2_register_event_cb(struct platform_device *pdev, u16 id,
			   dln2_event_cb_t event_cb)
{
	struct dln2_dev *dln2 = dev_get_drvdata(pdev->dev.parent);
	struct dln2_event_cb_entry *i, *entry;
	unsigned long flags;
	int ret = 0;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->id = id;
	entry->callback = event_cb;
	entry->pdev = pdev;

	spin_lock_irqsave(&dln2->event_cb_lock, flags);

	list_for_each_entry(i, &dln2->event_cb_list, list) {
		if (i->id == id) {
			ret = -EBUSY;
			break;
		}
	}

	if (!ret)
		list_add_rcu(&entry->list, &dln2->event_cb_list);

	spin_unlock_irqrestore(&dln2->event_cb_lock, flags);

	if (ret)
		kfree(entry);

	return ret;
}
EXPORT_SYMBOL(dln2_register_event_cb);

void dln2_unregister_event_cb(struct platform_device *pdev, u16 id)
{
	struct dln2_dev *dln2 = dev_get_drvdata(pdev->dev.parent);
	struct dln2_event_cb_entry *i;
	unsigned long flags;
	bool found = false;

	spin_lock_irqsave(&dln2->event_cb_lock, flags);

	list_for_each_entry(i, &dln2->event_cb_list, list) {
		if (i->id == id) {
			list_del_rcu(&i->list);
			found = true;
			break;
		}
	}

	spin_unlock_irqrestore(&dln2->event_cb_lock, flags);

	if (found) {
		synchronize_rcu();
		kfree(i);
	}
}
EXPORT_SYMBOL(dln2_unregister_event_cb);

/*
 * Returns true if a valid transfer slot is found. In this case the URB must not
 * be resubmitted immediately in dln2_rx as we need the data when dln2_transfer
 * is woke up. It will be resubmitted there.
 */
static bool dln2_transfer_complete(struct dln2_dev *dln2, struct urb *urb,
				   u16 handle, u16 rx_slot)
{
	struct device *dev = &dln2->interface->dev;
	struct dln2_mod_rx_slots *rxs = &dln2->mod_rx_slots[handle];
	struct dln2_rx_context *rxc;
	unsigned long flags;
	bool valid_slot = false;

	if (rx_slot >= DLN2_MAX_RX_SLOTS)
		goto out;

	rxc = &rxs->slots[rx_slot];

	spin_lock_irqsave(&rxs->lock, flags);
	if (rxc->in_use && !rxc->urb) {
		rxc->urb = urb;
		complete(&rxc->done);
		valid_slot = true;
	}
	spin_unlock_irqrestore(&rxs->lock, flags);

out:
	if (!valid_slot)
		dev_warn(dev, "bad/late response %d/%d\n", handle, rx_slot);

	return valid_slot;
}

static void dln2_run_event_callbacks(struct dln2_dev *dln2, u16 id, u16 echo,
				     void *data, int len)
{
	struct dln2_event_cb_entry *i;

	rcu_read_lock();

	list_for_each_entry_rcu(i, &dln2->event_cb_list, list) {
		if (i->id == id) {
			i->callback(i->pdev, echo, data, len);
			break;
		}
	}

	rcu_read_unlock();
}

static void dln2_rx(struct urb *urb)
{
	struct dln2_dev *dln2 = urb->context;
	struct dln2_header *hdr = urb->transfer_buffer;
	struct device *dev = &dln2->interface->dev;
	u16 id, echo, handle, size;
	u8 *data;
	int len;
	int err;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -EPIPE:
		/* this urb is terminated, clean up */
		dev_dbg(dev, "urb shutting down with status %d\n", urb->status);
		return;
	default:
		dev_dbg(dev, "nonzero urb status received %d\n", urb->status);
		goto out;
	}

	if (urb->actual_length < sizeof(struct dln2_header)) {
		dev_err(dev, "short response: %d\n", urb->actual_length);
		goto out;
	}

	handle = le16_to_cpu(hdr->handle);
	id = le16_to_cpu(hdr->id);
	echo = le16_to_cpu(hdr->echo);
	size = le16_to_cpu(hdr->size);

	if (size != urb->actual_length) {
		dev_err(dev, "size mismatch: handle %x cmd %x echo %x size %d actual %d\n",
			handle, id, echo, size, urb->actual_length);
		goto out;
	}

	if (handle >= DLN2_HANDLES) {
		dev_warn(dev, "invalid handle %d\n", handle);
		goto out;
	}

	data = urb->transfer_buffer + sizeof(struct dln2_header);
	len = urb->actual_length - sizeof(struct dln2_header);

	if (handle == DLN2_HANDLE_EVENT) {
		unsigned long flags;

		spin_lock_irqsave(&dln2->event_cb_lock, flags);
		dln2_run_event_callbacks(dln2, id, echo, data, len);
		spin_unlock_irqrestore(&dln2->event_cb_lock, flags);
	} else {
		/* URB will be re-submitted in _dln2_transfer (free_rx_slot) */
		if (dln2_transfer_complete(dln2, urb, handle, echo))
			return;
	}

out:
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0)
		dev_err(dev, "failed to resubmit RX URB: %d\n", err);
}

static void *dln2_prep_buf(u16 handle, u16 cmd, u16 echo, const void *obuf,
			   int *obuf_len, gfp_t gfp)
{
	int len;
	void *buf;
	struct dln2_header *hdr;

	len = *obuf_len + sizeof(*hdr);
	buf = kmalloc(len, gfp);
	if (!buf)
		return NULL;

	hdr = (struct dln2_header *)buf;
	hdr->id = cpu_to_le16(cmd);
	hdr->size = cpu_to_le16(len);
	hdr->echo = cpu_to_le16(echo);
	hdr->handle = cpu_to_le16(handle);

	memcpy(buf + sizeof(*hdr), obuf, *obuf_len);

	*obuf_len = len;

	return buf;
}

static int dln2_send_wait(struct dln2_dev *dln2, u16 handle, u16 cmd, u16 echo,
			  const void *obuf, int obuf_len)
{
	int ret = 0;
	int len = obuf_len;
	void *buf;
	int actual;

	buf = dln2_prep_buf(handle, cmd, echo, obuf, &len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = usb_bulk_msg(dln2->usb_dev,
			   usb_sndbulkpipe(dln2->usb_dev, dln2->ep_out),
			   buf, len, &actual, DLN2_USB_TIMEOUT);

	kfree(buf);

	return ret;
}

static bool find_free_slot(struct dln2_dev *dln2, u16 handle, int *slot)
{
	struct dln2_mod_rx_slots *rxs;
	unsigned long flags;

	if (dln2->disconnect) {
		*slot = -ENODEV;
		return true;
	}

	rxs = &dln2->mod_rx_slots[handle];

	spin_lock_irqsave(&rxs->lock, flags);

	*slot = find_first_zero_bit(rxs->bmap, DLN2_MAX_RX_SLOTS);

	if (*slot < DLN2_MAX_RX_SLOTS) {
		struct dln2_rx_context *rxc = &rxs->slots[*slot];

		set_bit(*slot, rxs->bmap);
		rxc->in_use = true;
	}

	spin_unlock_irqrestore(&rxs->lock, flags);

	return *slot < DLN2_MAX_RX_SLOTS;
}

static int alloc_rx_slot(struct dln2_dev *dln2, u16 handle)
{
	int ret;
	int slot;

	/*
	 * No need to timeout here, the wait is bounded by the timeout in
	 * _dln2_transfer.
	 */
	ret = wait_event_interruptible(dln2->mod_rx_slots[handle].wq,
				       find_free_slot(dln2, handle, &slot));
	if (ret < 0)
		return ret;

	return slot;
}

static void free_rx_slot(struct dln2_dev *dln2, u16 handle, int slot)
{
	struct dln2_mod_rx_slots *rxs;
	struct urb *urb = NULL;
	unsigned long flags;
	struct dln2_rx_context *rxc;

	rxs = &dln2->mod_rx_slots[handle];

	spin_lock_irqsave(&rxs->lock, flags);

	clear_bit(slot, rxs->bmap);

	rxc = &rxs->slots[slot];
	rxc->in_use = false;
	urb = rxc->urb;
	rxc->urb = NULL;
	reinit_completion(&rxc->done);

	spin_unlock_irqrestore(&rxs->lock, flags);

	if (urb) {
		int err;
		struct device *dev = &dln2->interface->dev;

		err = usb_submit_urb(urb, GFP_KERNEL);
		if (err < 0)
			dev_err(dev, "failed to resubmit RX URB: %d\n", err);
	}

	wake_up_interruptible(&rxs->wq);
}

static int _dln2_transfer(struct dln2_dev *dln2, u16 handle, u16 cmd,
			  const void *obuf, unsigned obuf_len,
			  void *ibuf, unsigned *ibuf_len)
{
	int ret = 0;
	int rx_slot;
	struct dln2_response *rsp;
	struct dln2_rx_context *rxc;
	struct device *dev = &dln2->interface->dev;
	const unsigned long timeout = msecs_to_jiffies(DLN2_USB_TIMEOUT);
	struct dln2_mod_rx_slots *rxs = &dln2->mod_rx_slots[handle];
	int size;

	spin_lock(&dln2->disconnect_lock);
	if (!dln2->disconnect)
		dln2->active_transfers++;
	else
		ret = -ENODEV;
	spin_unlock(&dln2->disconnect_lock);

	if (ret)
		return ret;

	rx_slot = alloc_rx_slot(dln2, handle);
	if (rx_slot < 0) {
		ret = rx_slot;
		goto out_decr;
	}

	ret = dln2_send_wait(dln2, handle, cmd, rx_slot, obuf, obuf_len);
	if (ret < 0) {
		dev_err(dev, "USB write failed: %d\n", ret);
		goto out_free_rx_slot;
	}

	rxc = &rxs->slots[rx_slot];

	ret = wait_for_completion_interruptible_timeout(&rxc->done, timeout);
	if (ret <= 0) {
		if (!ret)
			ret = -ETIMEDOUT;
		goto out_free_rx_slot;
	} else {
		ret = 0;
	}

	if (dln2->disconnect) {
		ret = -ENODEV;
		goto out_free_rx_slot;
	}

	/* if we got here we know that the response header has been checked */
	rsp = rxc->urb->transfer_buffer;
	size = le16_to_cpu(rsp->hdr.size);

	if (size < sizeof(*rsp)) {
		ret = -EPROTO;
		goto out_free_rx_slot;
	}

	if (le16_to_cpu(rsp->result) > 0x80) {
		dev_dbg(dev, "%d received response with error %d\n",
			handle, le16_to_cpu(rsp->result));
		ret = -EREMOTEIO;
		goto out_free_rx_slot;
	}

	if (!ibuf)
		goto out_free_rx_slot;

	if (*ibuf_len > size - sizeof(*rsp))
		*ibuf_len = size - sizeof(*rsp);

	memcpy(ibuf, rsp + 1, *ibuf_len);

out_free_rx_slot:
	free_rx_slot(dln2, handle, rx_slot);
out_decr:
	spin_lock(&dln2->disconnect_lock);
	dln2->active_transfers--;
	spin_unlock(&dln2->disconnect_lock);
	if (dln2->disconnect)
		wake_up(&dln2->disconnect_wq);

	return ret;
}

int dln2_transfer(struct platform_device *pdev, u16 cmd,
		  const void *obuf, unsigned obuf_len,
		  void *ibuf, unsigned *ibuf_len)
{
	struct dln2_platform_data *dln2_pdata;
	struct dln2_dev *dln2;
	u16 handle;

	dln2 = dev_get_drvdata(pdev->dev.parent);
	dln2_pdata = dev_get_platdata(&pdev->dev);
	handle = dln2_pdata->handle;

	return _dln2_transfer(dln2, handle, cmd, obuf, obuf_len, ibuf,
			      ibuf_len);
}
EXPORT_SYMBOL(dln2_transfer);

static int dln2_check_hw(struct dln2_dev *dln2)
{
	int ret;
	__le32 hw_type;
	int len = sizeof(hw_type);

	ret = _dln2_transfer(dln2, DLN2_HANDLE_CTRL, CMD_GET_DEVICE_VER,
			     NULL, 0, &hw_type, &len);
	if (ret < 0)
		return ret;
	if (len < sizeof(hw_type))
		return -EREMOTEIO;

	if (le32_to_cpu(hw_type) != DLN2_HW_ID) {
		dev_err(&dln2->interface->dev, "Device ID 0x%x not supported\n",
			le32_to_cpu(hw_type));
		return -ENODEV;
	}

	return 0;
}

static int dln2_print_serialno(struct dln2_dev *dln2)
{
	int ret;
	__le32 serial_no;
	int len = sizeof(serial_no);
	struct device *dev = &dln2->interface->dev;

	ret = _dln2_transfer(dln2, DLN2_HANDLE_CTRL, CMD_GET_DEVICE_SN, NULL, 0,
			     &serial_no, &len);
	if (ret < 0)
		return ret;
	if (len < sizeof(serial_no))
		return -EREMOTEIO;

	dev_info(dev, "Diolan DLN2 serial %u\n", le32_to_cpu(serial_no));

	return 0;
}

static int dln2_hw_init(struct dln2_dev *dln2)
{
	int ret;

	ret = dln2_check_hw(dln2);
	if (ret < 0)
		return ret;

	return dln2_print_serialno(dln2);
}

static void dln2_free_rx_urbs(struct dln2_dev *dln2)
{
	int i;

	for (i = 0; i < DLN2_MAX_URBS; i++) {
		usb_free_urb(dln2->rx_urb[i]);
		kfree(dln2->rx_buf[i]);
	}
}

static void dln2_stop_rx_urbs(struct dln2_dev *dln2)
{
	int i;

	for (i = 0; i < DLN2_MAX_URBS; i++)
		usb_kill_urb(dln2->rx_urb[i]);
}

static void dln2_free(struct dln2_dev *dln2)
{
	dln2_free_rx_urbs(dln2);
	usb_put_dev(dln2->usb_dev);
	kfree(dln2);
}

static int dln2_setup_rx_urbs(struct dln2_dev *dln2,
			      struct usb_host_interface *hostif)
{
	int i;
	const int rx_max_size = DLN2_RX_BUF_SIZE;

	for (i = 0; i < DLN2_MAX_URBS; i++) {
		dln2->rx_buf[i] = kmalloc(rx_max_size, GFP_KERNEL);
		if (!dln2->rx_buf[i])
			return -ENOMEM;

		dln2->rx_urb[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!dln2->rx_urb[i])
			return -ENOMEM;

		usb_fill_bulk_urb(dln2->rx_urb[i], dln2->usb_dev,
				  usb_rcvbulkpipe(dln2->usb_dev, dln2->ep_in),
				  dln2->rx_buf[i], rx_max_size, dln2_rx, dln2);
	}

	return 0;
}

static int dln2_start_rx_urbs(struct dln2_dev *dln2, gfp_t gfp)
{
	struct device *dev = &dln2->interface->dev;
	int ret;
	int i;

	for (i = 0; i < DLN2_MAX_URBS; i++) {
		ret = usb_submit_urb(dln2->rx_urb[i], gfp);
		if (ret < 0) {
			dev_err(dev, "failed to submit RX URB: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

enum {
	DLN2_ACPI_MATCH_GPIO	= 0,
	DLN2_ACPI_MATCH_I2C	= 1,
	DLN2_ACPI_MATCH_SPI	= 2,
	DLN2_ACPI_MATCH_ADC	= 3,
};

static struct dln2_platform_data dln2_pdata_gpio = {
	.handle = DLN2_HANDLE_GPIO,
};

static struct mfd_cell_acpi_match dln2_acpi_match_gpio = {
	.adr = DLN2_ACPI_MATCH_GPIO,
};

/* Only one I2C port seems to be supported on current hardware */
static struct dln2_platform_data dln2_pdata_i2c = {
	.handle = DLN2_HANDLE_I2C,
	.port = 0,
};

static struct mfd_cell_acpi_match dln2_acpi_match_i2c = {
	.adr = DLN2_ACPI_MATCH_I2C,
};

/* Only one SPI port supported */
static struct dln2_platform_data dln2_pdata_spi = {
	.handle = DLN2_HANDLE_SPI,
	.port = 0,
};

static struct mfd_cell_acpi_match dln2_acpi_match_spi = {
	.adr = DLN2_ACPI_MATCH_SPI,
};

/* Only one ADC port supported */
static struct dln2_platform_data dln2_pdata_adc = {
	.handle = DLN2_HANDLE_ADC,
	.port = 0,
};

static struct mfd_cell_acpi_match dln2_acpi_match_adc = {
	.adr = DLN2_ACPI_MATCH_ADC,
};

static const struct mfd_cell dln2_devs[] = {
	{
		.name = "dln2-gpio",
		.acpi_match = &dln2_acpi_match_gpio,
		.platform_data = &dln2_pdata_gpio,
		.pdata_size = sizeof(struct dln2_platform_data),
	},
	{
		.name = "dln2-i2c",
		.acpi_match = &dln2_acpi_match_i2c,
		.platform_data = &dln2_pdata_i2c,
		.pdata_size = sizeof(struct dln2_platform_data),
	},
	{
		.name = "dln2-spi",
		.acpi_match = &dln2_acpi_match_spi,
		.platform_data = &dln2_pdata_spi,
		.pdata_size = sizeof(struct dln2_platform_data),
	},
	{
		.name = "dln2-adc",
		.acpi_match = &dln2_acpi_match_adc,
		.platform_data = &dln2_pdata_adc,
		.pdata_size = sizeof(struct dln2_platform_data),
	},
};

static void dln2_stop(struct dln2_dev *dln2)
{
	int i, j;

	/* don't allow starting new transfers */
	spin_lock(&dln2->disconnect_lock);
	dln2->disconnect = true;
	spin_unlock(&dln2->disconnect_lock);

	/* cancel in progress transfers */
	for (i = 0; i < DLN2_HANDLES; i++) {
		struct dln2_mod_rx_slots *rxs = &dln2->mod_rx_slots[i];
		unsigned long flags;

		spin_lock_irqsave(&rxs->lock, flags);

		/* cancel all response waiters */
		for (j = 0; j < DLN2_MAX_RX_SLOTS; j++) {
			struct dln2_rx_context *rxc = &rxs->slots[j];

			if (rxc->in_use)
				complete(&rxc->done);
		}

		spin_unlock_irqrestore(&rxs->lock, flags);
	}

	/* wait for transfers to end */
	wait_event(dln2->disconnect_wq, !dln2->active_transfers);

	dln2_stop_rx_urbs(dln2);
}

static void dln2_disconnect(struct usb_interface *interface)
{
	struct dln2_dev *dln2 = usb_get_intfdata(interface);

	dln2_stop(dln2);

	mfd_remove_devices(&interface->dev);

	dln2_free(dln2);
}

static int dln2_probe(struct usb_interface *interface,
		      const struct usb_device_id *usb_id)
{
	struct usb_host_interface *hostif = interface->cur_altsetting;
	struct usb_endpoint_descriptor *epin;
	struct usb_endpoint_descriptor *epout;
	struct device *dev = &interface->dev;
	struct dln2_dev *dln2;
	int ret;
	int i, j;

	if (hostif->desc.bInterfaceNumber != 0)
		return -ENODEV;

	ret = usb_find_common_endpoints(hostif, &epin, &epout, NULL, NULL);
	if (ret)
		return ret;

	dln2 = kzalloc(sizeof(*dln2), GFP_KERNEL);
	if (!dln2)
		return -ENOMEM;

	dln2->ep_out = epout->bEndpointAddress;
	dln2->ep_in = epin->bEndpointAddress;
	dln2->usb_dev = usb_get_dev(interface_to_usbdev(interface));
	dln2->interface = interface;
	usb_set_intfdata(interface, dln2);
	init_waitqueue_head(&dln2->disconnect_wq);

	for (i = 0; i < DLN2_HANDLES; i++) {
		init_waitqueue_head(&dln2->mod_rx_slots[i].wq);
		spin_lock_init(&dln2->mod_rx_slots[i].lock);
		for (j = 0; j < DLN2_MAX_RX_SLOTS; j++)
			init_completion(&dln2->mod_rx_slots[i].slots[j].done);
	}

	spin_lock_init(&dln2->event_cb_lock);
	spin_lock_init(&dln2->disconnect_lock);
	INIT_LIST_HEAD(&dln2->event_cb_list);

	ret = dln2_setup_rx_urbs(dln2, hostif);
	if (ret)
		goto out_free;

	ret = dln2_start_rx_urbs(dln2, GFP_KERNEL);
	if (ret)
		goto out_stop_rx;

	ret = dln2_hw_init(dln2);
	if (ret < 0) {
		dev_err(dev, "failed to initialize hardware\n");
		goto out_stop_rx;
	}

	ret = mfd_add_hotplug_devices(dev, dln2_devs, ARRAY_SIZE(dln2_devs));
	if (ret != 0) {
		dev_err(dev, "failed to add mfd devices to core\n");
		goto out_stop_rx;
	}

	return 0;

out_stop_rx:
	dln2_stop_rx_urbs(dln2);

out_free:
	dln2_free(dln2);

	return ret;
}

static int dln2_suspend(struct usb_interface *iface, pm_message_t message)
{
	struct dln2_dev *dln2 = usb_get_intfdata(iface);

	dln2_stop(dln2);

	return 0;
}

static int dln2_resume(struct usb_interface *iface)
{
	struct dln2_dev *dln2 = usb_get_intfdata(iface);

	dln2->disconnect = false;

	return dln2_start_rx_urbs(dln2, GFP_NOIO);
}

static const struct usb_device_id dln2_table[] = {
	{ USB_DEVICE(0xa257, 0x2013) },
	{ }
};

MODULE_DEVICE_TABLE(usb, dln2_table);

static struct usb_driver dln2_driver = {
	.name = "dln2",
	.probe = dln2_probe,
	.disconnect = dln2_disconnect,
	.id_table = dln2_table,
	.suspend = dln2_suspend,
	.resume = dln2_resume,
};

module_usb_driver(dln2_driver);

MODULE_AUTHOR("Octavian Purdila <octavian.purdila@intel.com>");
MODULE_DESCRIPTION("Core driver for the Diolan DLN2 interface adapter");
MODULE_LICENSE("GPL v2");
