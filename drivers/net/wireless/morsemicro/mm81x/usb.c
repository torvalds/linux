// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2026 Morse Micro
 */
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/usb.h>
#include "hif.h"
#include "bus.h"
#include "mac.h"
#include "core.h"

/*
 * URB timeout in milliseconds. If an URB does not complete within this
 * time, it will be killed. This timeout needs to account for USB suspendand
 * resume occurring before the URB can be transferred, and it also needs to
 * account for transferring USB_MAX_TRANSFER_SIZE bytes over a potentially
 * slow, congested USB Full Speed link.
 */
#define URB_TIMEOUT_MS 250

/* High speed USB 2^(4-1) * 125usec = 1msec */
#define MM81X_USB_INTERRUPT_INTERVAL 4

/* Max bytes per USB read/write */
#define USB_MAX_TRANSFER_SIZE (16 * 1024)

/* INT EP buffer size */
#define MM81X_EP_INT_BUFFER_SIZE 8

/* Morse vendor IDs*/
#define MM81X_VENDOR_ID 0x325b
#define MM81X_MM810X_PRODUCT_ID 0x8100

/* Power management runtime auto-suspend delay value in milliseconds */
#define PM_RUNTIME_AUTOSUSPEND_DELAY_MS 100

enum mm81x_usb_endpoints {
	MM81X_EP_CMD = 0,
	MM81X_EP_INT,
	MM81X_EP_MEM_RD,
	MM81X_EP_MEM_WR,
	MM81X_EP_REG_RD,
	MM81X_EP_REG_WR,
	MM81X_EP_EP_MAX,
};

struct mm81x_usb_endpoint {
	unsigned char *buffer;
	struct urb *urb;
	__u8 addr;
	int size;
};

enum mm81x_usb_flags { MM81X_USB_FLAG_ATTACHED, MM81X_USB_FLAG_SUSPENDED };

struct mm81x_usb {
	struct usb_device *udev;
	struct usb_interface *interface;
	struct mm81x_usb_endpoint endpoints[MM81X_EP_EP_MAX];
	int errors;

	/* serialise USB device struct */
	struct mutex lock;

	/* serialise USB bus access */
	struct mutex bus_lock;

	bool ongoing_cmd;
	bool ongoing_rw;
	wait_queue_head_t rw_in_wait;
	unsigned long flags;
};

enum mm81x_usb_command_direction {
	MM81X_USB_WRITE = 0x00,
	MM81X_USB_READ = 0x80,
	MM81X_USB_RESET = 0x02,
};

struct mm81x_usb_command {
	__le32 dir; /* Next BULK direction */
	__le32 address; /* Next BULK address */
	__le32 length; /* Next BULK size */
};

static const struct usb_device_id mm81x_usb_table[] = {
	{ USB_DEVICE(MM81X_VENDOR_ID, MM81X_MM810X_PRODUCT_ID) },
	{} /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, mm81x_usb_table);

static void mm81x_usb_irq_work(struct work_struct *work)
{
	struct mm81x *mors = container_of(work, struct mm81x, usb_irq_work);

	mm81x_claim_bus(mors);
	mm81x_hw_irq_handle(mors);
	mm81x_release_bus(mors);
}

static bool mm81x_usb_urb_status_is_disconnect(const struct urb *urb)
{
	return ((urb->status == -EPROTO) || (urb->status == -EILSEQ) ||
		(urb->status == -ETIME) || (urb->status == -EPIPE));
}

static void mm81x_usb_int_handler(struct urb *urb)
{
	int ret;
	struct mm81x *mors = urb->context;
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;

	if (!test_bit(MM81X_USB_FLAG_ATTACHED, &musb->flags))
		return;

	if (urb->status) {
		if (mm81x_usb_urb_status_is_disconnect(urb)) {
			clear_bit(MM81X_USB_FLAG_ATTACHED, &musb->flags);
			set_bit(MM81X_STATE_CHIP_UNRESPONSIVE,
				&mors->state_flags);
			dev_dbg(mors->dev,
				"USB sudden disconnect detected in %s",
				__func__);
			return;
		}

		if (!(urb->status == -ENOENT || urb->status == -ECONNRESET ||
		      urb->status == -ESHUTDOWN))
			dev_err(mors->dev, "- nonzero read status received: %d",
				urb->status);
	}

	ret = usb_submit_urb(urb, GFP_ATOMIC);

	/* usb_kill_urb has been called */
	if (ret == -EPERM)
		return;
	else if (ret)
		dev_err(mors->dev, "error: resubmit urb %p err code %d", urb,
			ret);

	queue_work(mors->chip_wq, &mors->usb_irq_work);
}

static int mm81x_usb_int_enable(struct mm81x *mors)
{
	int ret = 0;
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;
	struct urb *urb;

	if (!test_bit(MM81X_USB_FLAG_ATTACHED, &musb->flags))
		return -ENODEV;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		ret = -ENOMEM;
		goto out;
	}

	musb->endpoints[MM81X_EP_INT].urb = urb;

	musb->endpoints[MM81X_EP_INT].buffer =
		usb_alloc_coherent(musb->udev, MM81X_EP_INT_BUFFER_SIZE,
				   GFP_KERNEL, &urb->transfer_dma);
	if (!musb->endpoints[MM81X_EP_INT].buffer) {
		dev_err(mors->dev, "couldn't allocate transfer_buffer");
		ret = -ENOMEM;
		goto error_set_urb_null;
	}

	usb_fill_int_urb(
		musb->endpoints[MM81X_EP_INT].urb, musb->udev,
		usb_rcvintpipe(musb->udev, musb->endpoints[MM81X_EP_INT].addr),
		musb->endpoints[MM81X_EP_INT].buffer, MM81X_EP_INT_BUFFER_SIZE,
		mm81x_usb_int_handler, mors, MM81X_USB_INTERRUPT_INTERVAL);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret) {
		dev_err(mors->dev, "Couldn't submit urb. Error number %d", ret);
		goto error;
	}

	return 0;

error:
	usb_free_coherent(musb->udev, MM81X_EP_INT_BUFFER_SIZE,
			  musb->endpoints[MM81X_EP_INT].buffer,
			  urb->transfer_dma);
error_set_urb_null:
	musb->endpoints[MM81X_EP_INT].urb = NULL;
	usb_free_urb(urb);
out:
	return ret;
}

static void mm81x_usb_int_stop(struct mm81x *mors)
{
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;

	usb_kill_urb(musb->endpoints[MM81X_EP_INT].urb);
	cancel_work_sync(&mors->usb_irq_work);
}

static void mm81x_usb_cmd_callback(struct urb *urb)
{
	struct mm81x *mors = urb->context;
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT || urb->status == -ECONNRESET ||
		      urb->status == -ESHUTDOWN))
			dev_err(mors->dev,
				"nonzero write bulk status received: %d",
				urb->status);

		musb->errors = urb->status;
	}

	musb->ongoing_cmd = false;
	wake_up(&musb->rw_in_wait);
}

static int mm81x_usb_cmd(struct mm81x_usb *musb,
			 const struct mm81x_usb_command *cmd)
{
	int retval = 0;
	struct mm81x *mors = usb_get_intfdata(musb->interface);
	struct mm81x_usb_endpoint *ep = &musb->endpoints[MM81X_EP_CMD];
	size_t writesize = sizeof(*cmd);

	if (!test_bit(MM81X_USB_FLAG_ATTACHED, &musb->flags))
		return -ENODEV;

	memcpy(ep->buffer, cmd, writesize);

	usb_fill_bulk_urb(ep->urb, musb->udev,
			  usb_sndbulkpipe(musb->udev, ep->addr), ep->buffer,
			  writesize, mm81x_usb_cmd_callback, mors);
	ep->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	musb->ongoing_cmd = true;

	retval = usb_submit_urb(ep->urb, GFP_KERNEL);
	if (retval) {
		dev_err(mors->dev, "- failed submitting write urb, error %d",
			retval);

		goto error;
	}

	retval = wait_event_interruptible_timeout(
		musb->rw_in_wait, (!musb->ongoing_cmd),
		msecs_to_jiffies(URB_TIMEOUT_MS));
	if (retval < 0) {
		dev_err(mors->dev, "error waiting for urb %d", retval);
		goto error;
	} else if (retval == 0) {
		dev_err(mors->dev, "timed out waiting for urb");
		usb_kill_urb(ep->urb);
		retval = -ETIMEDOUT;
		goto error;
	}

	musb->ongoing_cmd = false;
	return writesize;

error:
	musb->ongoing_cmd = false;
	return retval;
}

static int mm81x_usb_ndr_reset(struct mm81x *mors)
{
	int ret;
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;
	struct mm81x_usb_command cmd;

	mutex_lock(&musb->lock);

	musb->ongoing_rw = true;
	musb->errors = 0;

	cmd.dir = cpu_to_le32(MM81X_USB_RESET);
	cmd.address = cpu_to_le32(0);
	cmd.length = cpu_to_le32(0);

	ret = mm81x_usb_cmd(musb, &cmd);
	if (ret < 0)
		dev_err(mors->dev, "mm81x_usb_cmd (MM81X_USB_RESET) error %d\n",
			ret);
	else
		ret = 0;

	musb->ongoing_rw = false;
	mutex_unlock(&musb->lock);
	return ret;
}

static void mm81x_usb_mem_rw_callback(struct urb *urb)
{
	struct mm81x *mors = urb->context;
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT || urb->status == -ECONNRESET ||
		      urb->status == -ESHUTDOWN))
			dev_err(mors->dev,
				"nonzero write bulk status received: %d",
				urb->status);

		musb->errors = urb->status;
	}

	musb->ongoing_rw = false;
	wake_up(&musb->rw_in_wait);
}

static int mm81x_usb_mem_read(struct mm81x_usb *musb, u32 address, u8 *data,
			      ssize_t size)
{
	int ret;
	struct mm81x_usb_command cmd;
	struct mm81x *mors = usb_get_intfdata(musb->interface);

	if (!test_bit(MM81X_USB_FLAG_ATTACHED, &musb->flags))
		return -ENODEV;

	mutex_lock(&musb->lock);

	musb->ongoing_rw = true;
	musb->errors = 0;

	/* Send command ahead to prepare for Tokens */
	cmd.dir = cpu_to_le32(MM81X_USB_READ);
	cmd.address = cpu_to_le32(address);
	cmd.length = cpu_to_le32(size);

	ret = mm81x_usb_cmd(musb, &cmd);
	if (ret < 0) {
		dev_err(mors->dev, "mm81x_usb_cmd error %d", ret);
		goto error;
	}

	/* Let's be fast push the next URB, don't wait until command is done */
	usb_fill_bulk_urb(
		musb->endpoints[MM81X_EP_MEM_RD].urb, musb->udev,
		usb_rcvbulkpipe(musb->udev,
				musb->endpoints[MM81X_EP_MEM_RD].addr),
		musb->endpoints[MM81X_EP_MEM_RD].buffer, size,
		mm81x_usb_mem_rw_callback, mors);

	ret = usb_submit_urb(musb->endpoints[MM81X_EP_MEM_RD].urb, GFP_ATOMIC);
	if (ret < 0) {
		dev_err(mors->dev, "failed submitting read urb, error %d", ret);
		ret = (ret == -ENOMEM) ? ret : -EIO;
		goto error;
	}

	ret = wait_event_interruptible_timeout(
		musb->rw_in_wait, (!musb->ongoing_rw),
		msecs_to_jiffies(URB_TIMEOUT_MS));
	if (ret < 0) {
		dev_err(mors->dev, "wait_event_interruptible: error %d", ret);
		goto error;
	} else if (ret == 0) {
		/* Timed out. */
		usb_kill_urb(musb->endpoints[MM81X_EP_MEM_RD].urb);
	}

	if (musb->errors) {
		ret = musb->errors;
		dev_err(mors->dev, "mem read error %d", ret);
		goto error;
	}

	memcpy(data, musb->endpoints[MM81X_EP_MEM_RD].buffer, size);
	ret = size;

error:
	musb->ongoing_rw = false;
	mutex_unlock(&musb->lock);

	return ret;
}

static int mm81x_usb_mem_write(struct mm81x_usb *musb, u32 address, u8 *data,
			       ssize_t size)
{
	int ret;
	struct mm81x_usb_command cmd;
	struct mm81x *mors = usb_get_intfdata(musb->interface);

	if (!test_bit(MM81X_USB_FLAG_ATTACHED, &musb->flags))
		return -ENODEV;

	mutex_lock(&musb->lock);

	musb->ongoing_rw = true;
	musb->errors = 0;

	/* Send command ahead to prepare for Tokens */
	cmd.dir = cpu_to_le32(MM81X_USB_WRITE);
	cmd.address = cpu_to_le32(address);
	cmd.length = cpu_to_le32(size);
	ret = mm81x_usb_cmd(musb, &cmd);
	if (ret < 0) {
		dev_err(mors->dev, "mm81x_usb_mem_read error %d", ret);
		goto error;
	}

	memcpy(musb->endpoints[MM81X_EP_MEM_WR].buffer, data, size);

	/* prepare a read */
	usb_fill_bulk_urb(
		musb->endpoints[MM81X_EP_MEM_WR].urb, musb->udev,
		usb_sndbulkpipe(musb->udev,
				musb->endpoints[MM81X_EP_MEM_WR].addr),
		musb->endpoints[MM81X_EP_MEM_WR].buffer, size,
		mm81x_usb_mem_rw_callback, mors);

	ret = usb_submit_urb(musb->endpoints[MM81X_EP_MEM_WR].urb, GFP_ATOMIC);
	if (ret < 0) {
		dev_err(mors->dev, "- failed submitting write urb, error %d",
			ret);
		ret = (ret == -ENOMEM) ? ret : -EIO;
		goto error;
	}

	ret = wait_event_interruptible_timeout(
		musb->rw_in_wait, (!musb->ongoing_rw),
		msecs_to_jiffies(URB_TIMEOUT_MS));
	if (ret < 0) {
		dev_err(mors->dev, "error %d", ret);
		goto error;
	} else if (ret == 0) {
		/* Timed out. */
		usb_kill_urb(musb->endpoints[MM81X_EP_MEM_WR].urb);
	}

	if (musb->errors) {
		ret = musb->errors;
		dev_err(mors->dev, "error %d", ret);
		goto error;
	}

	ret = size;

error:
	musb->ongoing_rw = false;
	mutex_unlock(&musb->lock);
	return ret;
}

static int mm81x_usb_dm_read(struct mm81x *mors, u32 address, u8 *data, int len)
{
	ssize_t offset = 0;
	int ret;
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;

	while (offset < len) {
		ret = mm81x_usb_mem_read(musb, address + offset,
					 (u8 *)(data + offset),
					 min((ssize_t)(len - offset),
					     (ssize_t)USB_MAX_TRANSFER_SIZE));
		if (ret < 0) {
			dev_err(mors->dev, "%s failed (errno=%d)", __func__,
				ret);
			return ret;
		}

		offset += ret;
	}

	return 0;
}

static int mm81x_usb_dm_write(struct mm81x *mors, u32 address, const u8 *data,
			      int len)
{
	ssize_t offset = 0;
	int ret;
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;

	while (offset < len) {
		ret = mm81x_usb_mem_write(musb, address + offset,
					  (u8 *)(data + offset),
					  min((ssize_t)(len - offset),
					      (ssize_t)USB_MAX_TRANSFER_SIZE));
		if (ret < 0) {
			dev_err(mors->dev, "%s failed (errno=%d)", __func__,
				ret);
			return ret;
		}

		offset += ret;
	}

	return 0;
}

static int mm81x_usb_reg32_read(struct mm81x *mors, u32 address, u32 *val)
{
	int ret = 0;
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;

	ret = mm81x_usb_mem_read(musb, address, (u8 *)val, sizeof(*val));
	if (ret == sizeof(*val)) {
		*val = le32_to_cpup((__le32 *)val);
		return 0;
	}

	dev_err(mors->dev, "usb reg32 read failed %d", ret);
	return ret;
}

static int mm81x_usb_reg32_write(struct mm81x *mors, u32 address, u32 val)
{
	int ret = 0;
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;
	__le32 val_le = cpu_to_le32(val);

	ret = mm81x_usb_mem_write(musb, address, (u8 *)&val_le, sizeof(val_le));
	if (ret == sizeof(val_le))
		return 0;

	dev_err(mors->dev, "usb reg32 write failed %d", ret);
	return ret;
}

static void mm81x_usb_bus_enable(struct mm81x *mors, bool enable)
{
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;

	if (enable)
		usb_autopm_get_interface(musb->interface);
	else
		usb_autopm_put_interface(musb->interface);
}

static void mm81x_usb_claim_bus(struct mm81x *mors)
{
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;

	mutex_lock(&musb->bus_lock);
}

static void mm81x_usb_release_bus(struct mm81x *mors)
{
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;

	mutex_unlock(&musb->bus_lock);
}

static void mm81x_usb_set_irq(struct mm81x *mors, bool enable)
{
}

static const struct mm81x_bus_ops mm81x_usb_ops = {
	.dm_read = mm81x_usb_dm_read,
	.dm_write = mm81x_usb_dm_write,
	.reg32_read = mm81x_usb_reg32_read,
	.reg32_write = mm81x_usb_reg32_write,
	.digital_reset = mm81x_usb_ndr_reset,
	.set_bus_enable = mm81x_usb_bus_enable,
	.claim = mm81x_usb_claim_bus,
	.release = mm81x_usb_release_bus,
	.set_irq = mm81x_usb_set_irq,
	.bulk_alignment = MM81X_BUS_DEFAULT_BULK_ALIGNMENT,
};

static int mm81x_usb_detect_endpoints(struct mm81x *mors,
				      const struct usb_interface *intf)
{
	int ret;
	unsigned int i;
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;
	struct usb_endpoint_descriptor *ep_desc;
	struct usb_host_interface *intf_desc = intf->cur_altsetting;

	for (i = 0; i < intf_desc->desc.bNumEndpoints; i++) {
		ep_desc = &intf_desc->endpoint[i].desc;

		if (usb_endpoint_is_bulk_in(ep_desc)) {
			if (!musb->endpoints[MM81X_EP_MEM_RD].addr) {
				musb->endpoints[MM81X_EP_MEM_RD].addr =
					usb_endpoint_num(ep_desc);
				musb->endpoints[MM81X_EP_MEM_RD].size =
					usb_endpoint_maxp(ep_desc);
			} else if (!musb->endpoints[MM81X_EP_REG_RD].addr) {
				musb->endpoints[MM81X_EP_REG_RD].addr =
					usb_endpoint_num(ep_desc);
				musb->endpoints[MM81X_EP_REG_RD].size =
					usb_endpoint_maxp(ep_desc);
			}
		} else if (usb_endpoint_is_bulk_out(ep_desc)) {
			if (!musb->endpoints[MM81X_EP_MEM_WR].addr) {
				musb->endpoints[MM81X_EP_MEM_WR].addr =
					usb_endpoint_num(ep_desc);
				musb->endpoints[MM81X_EP_MEM_WR].size =
					usb_endpoint_maxp(ep_desc);
			} else if (!musb->endpoints[MM81X_EP_REG_WR].addr) {
				musb->endpoints[MM81X_EP_REG_WR].addr =
					usb_endpoint_num(ep_desc);
				musb->endpoints[MM81X_EP_REG_WR].size =
					usb_endpoint_maxp(ep_desc);
			}
		} else if (usb_endpoint_is_int_in(ep_desc)) {
			musb->endpoints[MM81X_EP_INT].addr =
				usb_endpoint_num(ep_desc);
			musb->endpoints[MM81X_EP_INT].size =
				usb_endpoint_maxp(ep_desc);
		}
	}

	dev_dbg(mors->dev, "\tMemory Endpoint IN %s detected: %u size %u",
		musb->endpoints[MM81X_EP_MEM_RD].addr ? "" : "not",
		musb->endpoints[MM81X_EP_MEM_RD].addr,
		musb->endpoints[MM81X_EP_MEM_RD].size);
	dev_dbg(mors->dev, "\tMemory Endpoint OUT %s detected: %u size %u",
		musb->endpoints[MM81X_EP_MEM_WR].addr ? "" : "not",
		musb->endpoints[MM81X_EP_MEM_WR].addr,
		musb->endpoints[MM81X_EP_MEM_WR].size);
	dev_dbg(mors->dev, "\tRegister Endpoint IN %s detected: %u",
		musb->endpoints[MM81X_EP_REG_RD].addr ? "" : "not",
		musb->endpoints[MM81X_EP_REG_RD].addr);
	dev_dbg(mors->dev, "\tRegister Endpoint OUT %s detected: %u",
		musb->endpoints[MM81X_EP_REG_WR].addr ? "" : "not",
		musb->endpoints[MM81X_EP_REG_WR].addr);
	dev_dbg(mors->dev, "\tStats IN endpoint %s detected: %u",
		musb->endpoints[MM81X_EP_INT].addr ? "" : "not",
		musb->endpoints[MM81X_EP_INT].addr);

	/* Verify we have an IN and OUT */
	if (!(musb->endpoints[MM81X_EP_MEM_RD].addr &&
	      musb->endpoints[MM81X_EP_MEM_WR].addr))
		return -ENODEV;

	/* Verify the stats MM81X_EP_INT is detected */
	if (!musb->endpoints[MM81X_EP_INT].addr)
		return -ENODEV;

	/* Verify minimum interrupt status read */
	if (musb->endpoints[MM81X_EP_INT].size < 8)
		return -ENODEV;

	musb->endpoints[MM81X_EP_CMD].urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!musb->endpoints[MM81X_EP_CMD].urb) {
		ret = -ENOMEM;
		goto err_ep;
	}

	musb->endpoints[MM81X_EP_MEM_RD].urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!musb->endpoints[MM81X_EP_MEM_RD].urb) {
		ret = -ENOMEM;
		goto err_ep;
	}

	musb->endpoints[MM81X_EP_MEM_WR].urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!musb->endpoints[MM81X_EP_MEM_WR].urb) {
		ret = -ENOMEM;
		goto err_ep;
	}

	musb->endpoints[MM81X_EP_MEM_RD].buffer =
		kmalloc(USB_MAX_TRANSFER_SIZE, GFP_KERNEL);
	if (!musb->endpoints[MM81X_EP_MEM_RD].buffer) {
		ret = -ENOMEM;
		goto err_ep;
	}

	musb->endpoints[MM81X_EP_MEM_WR].buffer =
		kmalloc(USB_MAX_TRANSFER_SIZE, GFP_KERNEL);
	if (!musb->endpoints[MM81X_EP_MEM_WR].buffer) {
		ret = -ENOMEM;
		goto err_ep;
	}

	musb->endpoints[MM81X_EP_CMD].buffer = usb_alloc_coherent(
		musb->udev, sizeof(struct mm81x_usb_command), GFP_KERNEL,
		&musb->endpoints[MM81X_EP_CMD].urb->transfer_dma);

	if (!musb->endpoints[MM81X_EP_CMD].buffer) {
		ret = -ENOMEM;
		goto err_ep;
	}

	/* Assign command to memory out end point */
	musb->endpoints[MM81X_EP_CMD].addr =
		musb->endpoints[MM81X_EP_MEM_WR].addr;
	musb->endpoints[MM81X_EP_CMD].size =
		musb->endpoints[MM81X_EP_MEM_WR].size;

	return 0;

err_ep:
	if (musb->endpoints[MM81X_EP_CMD].urb &&
	    musb->endpoints[MM81X_EP_CMD].buffer)
		usb_free_coherent(
			musb->udev, sizeof(struct mm81x_usb_command),
			musb->endpoints[MM81X_EP_CMD].buffer,
			musb->endpoints[MM81X_EP_CMD].urb->transfer_dma);
	usb_free_urb(musb->endpoints[MM81X_EP_MEM_RD].urb);
	usb_free_urb(musb->endpoints[MM81X_EP_CMD].urb);
	usb_free_urb(musb->endpoints[MM81X_EP_MEM_WR].urb);
	kfree(musb->endpoints[MM81X_EP_MEM_RD].buffer);
	kfree(musb->endpoints[MM81X_EP_MEM_WR].buffer);

	return ret;
}

static void mm81x_urb_cleanup(struct mm81x *mors)
{
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;
	struct mm81x_usb_endpoint *int_ep = &musb->endpoints[MM81X_EP_INT];
	struct mm81x_usb_endpoint *rd_ep = &musb->endpoints[MM81X_EP_MEM_RD];
	struct mm81x_usb_endpoint *wr_ep = &musb->endpoints[MM81X_EP_MEM_WR];
	struct mm81x_usb_endpoint *cmd_ep = &musb->endpoints[MM81X_EP_CMD];

	usb_kill_urb(rd_ep->urb);
	usb_kill_urb(wr_ep->urb);
	usb_kill_urb(cmd_ep->urb);

	if (int_ep->urb)
		usb_free_coherent(musb->udev, MM81X_EP_INT_BUFFER_SIZE,
				  int_ep->buffer, int_ep->urb->transfer_dma);

	if (cmd_ep->urb)
		usb_free_coherent(musb->udev, sizeof(struct mm81x_usb_command),
				  cmd_ep->buffer, cmd_ep->urb->transfer_dma);

	kfree(wr_ep->buffer);
	kfree(rd_ep->buffer);

	usb_free_urb(int_ep->urb);
	usb_free_urb(wr_ep->urb);
	usb_free_urb(rd_ep->urb);
	usb_free_urb(cmd_ep->urb);
}

static int mm81x_usb_probe(struct usb_interface *interface,
			   const struct usb_device_id *id)
{
	int ret;
	struct mm81x *mors;
	struct mm81x_usb *musb;

	mors = mm81x_core_alloc(sizeof(*musb), &interface->dev);
	if (!mors)
		return -ENOMEM;

	mors->bus_ops = &mm81x_usb_ops;
	mors->bus_type = MM81X_BUS_TYPE_USB;

	musb = (struct mm81x_usb *)mors->drv_priv;
	musb->udev = usb_get_dev(interface_to_usbdev(interface));
	musb->interface = usb_get_intf(interface);

	mutex_init(&musb->lock);
	mutex_init(&musb->bus_lock);
	init_waitqueue_head(&musb->rw_in_wait);
	usb_set_intfdata(interface, mors);

	ret = mm81x_usb_detect_endpoints(mors, interface);
	if (ret < 0)
		goto err_core_free;

	set_bit(MM81X_USB_FLAG_ATTACHED, &musb->flags);

	ret = mm81x_core_init(mors);
	if (ret)
		goto err_urb_cleanup;

	INIT_WORK(&mors->usb_irq_work, mm81x_usb_irq_work);

	ret = mm81x_usb_int_enable(mors);
	if (ret)
		goto err_core_deinit;

	ret = mm81x_core_register(mors);
	if (ret)
		goto err_usb_int_stop;

	/* USB requires remote wakeup functionality for suspend */
	clear_bit(MM81X_USB_FLAG_SUSPENDED, &musb->flags);
	musb->interface->needs_remote_wakeup = 1;
	usb_enable_autosuspend(musb->udev);
	pm_runtime_set_autosuspend_delay(&musb->udev->dev,
					 PM_RUNTIME_AUTOSUSPEND_DELAY_MS);

	usb_autopm_get_interface(interface);
	return 0;

err_usb_int_stop:
	mm81x_usb_int_stop(mors);
err_core_deinit:
	mm81x_core_deinit(mors);
err_urb_cleanup:
	mm81x_urb_cleanup(mors);
err_core_free:
	mm81x_core_free(mors);
	usb_put_intf(interface);
	usb_put_dev(interface_to_usbdev(interface));
	return ret;
}

static void mm81x_usb_disconnect(struct usb_interface *interface)
{
	struct mm81x *mors = usb_get_intfdata(interface);
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;
	int minor = interface->minor;
	struct usb_device *udev = interface_to_usbdev(interface);

	if (udev->state == USB_STATE_NOTATTACHED) {
		clear_bit(MM81X_USB_FLAG_ATTACHED, &musb->flags);
		set_bit(MM81X_STATE_CHIP_UNRESPONSIVE, &mors->state_flags);
		dev_dbg(mors->dev, "USB suddenly unplugged");
	}

	usb_disable_autosuspend(udev);

	if (test_bit(MM81X_USB_FLAG_SUSPENDED, &musb->flags)) {
		dev_dbg(mors->dev, "USB was suspended: release locks");
		mm81x_usb_release_bus(mors);
		mutex_unlock(&musb->lock);
	}

	clear_bit(MM81X_USB_FLAG_SUSPENDED, &musb->flags);

	mm81x_core_unregister(mors);
	mm81x_usb_int_stop(mors);
	mm81x_core_deinit(mors);
	mm81x_urb_cleanup(mors);
	mm81x_core_free(mors);

	usb_autopm_put_interface(interface);
	usb_set_intfdata(interface, NULL);
	dev_info(&interface->dev, "USB Morse #%d now disconnected", minor);
	usb_put_intf(interface);
	usb_put_dev(udev);
}

static int mm81x_usb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct mm81x *mors = usb_get_intfdata(intf);
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;
	struct mm81x_usb_endpoint *int_ep = &musb->endpoints[MM81X_EP_INT];
	struct mm81x_usb_endpoint *rd_ep = &musb->endpoints[MM81X_EP_MEM_RD];
	struct mm81x_usb_endpoint *wr_ep = &musb->endpoints[MM81X_EP_MEM_WR];
	struct mm81x_usb_endpoint *cmd_ep = &musb->endpoints[MM81X_EP_CMD];

	if (!test_bit(MM81X_USB_FLAG_ATTACHED, &musb->flags))
		return -ENODEV;

	usb_kill_urb(int_ep->urb);
	usb_kill_urb(rd_ep->urb);
	usb_kill_urb(wr_ep->urb);
	usb_kill_urb(cmd_ep->urb);

	/* Locking the bus. No USB communication after this point */
	mm81x_usb_claim_bus(mors);
	mutex_lock(&musb->lock);

	set_bit(MM81X_USB_FLAG_SUSPENDED, &musb->flags);
	return 0;
}

static int mm81x_usb_resume(struct usb_interface *intf)
{
	struct mm81x *mors = usb_get_intfdata(intf);
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;
	int ret;
	struct mm81x_usb_endpoint *int_ep = &musb->endpoints[MM81X_EP_INT];

	if (!test_bit(MM81X_USB_FLAG_ATTACHED, &musb->flags))
		return -ENODEV;

	ret = usb_submit_urb(int_ep->urb, GFP_KERNEL);
	if (ret)
		dev_err(mors->dev, "Couldn't submit urb. Error number %d", ret);

	mm81x_usb_release_bus(mors);
	mutex_unlock(&musb->lock);

	clear_bit(MM81X_USB_FLAG_SUSPENDED, &musb->flags);
	return 0;
}

static int mm81x_usb_reset_resume(struct usb_interface *intf)
{
	struct mm81x *mors = usb_get_intfdata(intf);
	struct mm81x_usb *musb = (struct mm81x_usb *)mors->drv_priv;
	int ret;
	struct mm81x_usb_endpoint *int_ep = &musb->endpoints[MM81X_EP_INT];

	if (!test_bit(MM81X_USB_FLAG_ATTACHED, &musb->flags))
		return -ENODEV;

	ret = usb_submit_urb(int_ep->urb, GFP_KERNEL);
	if (ret)
		dev_err(mors->dev, "Couldn't submit urb. Error number %d", ret);

	mm81x_usb_release_bus(mors);
	mutex_unlock(&musb->lock);

	clear_bit(MM81X_USB_FLAG_SUSPENDED, &musb->flags);

	return 0;
}

static int mm81x_usb_pre_reset(struct usb_interface *intf)
{
	return 0;
}

static int mm81x_usb_post_reset(struct usb_interface *intf)
{
	return 0;
}

static struct usb_driver mm81x_usb_driver = {
	.name = "mm81x_usb",
	.probe = mm81x_usb_probe,
	.disconnect = mm81x_usb_disconnect,
	.suspend = mm81x_usb_suspend,
	.resume = mm81x_usb_resume,
	.reset_resume = mm81x_usb_reset_resume,
	.pre_reset = mm81x_usb_pre_reset,
	.post_reset = mm81x_usb_post_reset,
	.id_table = mm81x_usb_table,
	.supports_autosuspend = 1,
	.soft_unbind = 1,
};

module_usb_driver(mm81x_usb_driver);

MODULE_AUTHOR("Morse Micro");
MODULE_DESCRIPTION("Driver support for Morse Micro MM81X USB devices");
MODULE_LICENSE("Dual BSD/GPL");
