/*
 * USB SD Host Controller (USHC) controller driver.
 *
 * Copyright (C) 2010 Cambridge Silicon Radio Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * Notes:
 *   - Only version 2 devices are supported.
 *   - Version 2 devices only support SDIO cards/devices (R2 response is
 *     unsupported).
 *
 * References:
 *   [USHC] USB SD Host Controller specification (CS-118793-SP)
 */
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/mmc/host.h>

enum ushc_request {
	USHC_GET_CAPS  = 0x00,
	USHC_HOST_CTRL = 0x01,
	USHC_PWR_CTRL  = 0x02,
	USHC_CLK_FREQ  = 0x03,
	USHC_EXEC_CMD  = 0x04,
	USHC_READ_RESP = 0x05,
	USHC_RESET     = 0x06,
};

enum ushc_request_type {
	USHC_GET_CAPS_TYPE  = USB_DIR_IN  | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
	USHC_HOST_CTRL_TYPE = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
	USHC_PWR_CTRL_TYPE  = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
	USHC_CLK_FREQ_TYPE  = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
	USHC_EXEC_CMD_TYPE  = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
	USHC_READ_RESP_TYPE = USB_DIR_IN  | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
	USHC_RESET_TYPE     = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
};

#define USHC_GET_CAPS_VERSION_MASK 0xff
#define USHC_GET_CAPS_3V3      (1 << 8)
#define USHC_GET_CAPS_3V0      (1 << 9)
#define USHC_GET_CAPS_1V8      (1 << 10)
#define USHC_GET_CAPS_HIGH_SPD (1 << 16)

#define USHC_HOST_CTRL_4BIT     (1 << 1)
#define USHC_HOST_CTRL_HIGH_SPD (1 << 0)

#define USHC_PWR_CTRL_OFF 0x00
#define USHC_PWR_CTRL_3V3 0x01
#define USHC_PWR_CTRL_3V0 0x02
#define USHC_PWR_CTRL_1V8 0x03

#define USHC_READ_RESP_BUSY        (1 << 4)
#define USHC_READ_RESP_ERR_TIMEOUT (1 << 3)
#define USHC_READ_RESP_ERR_CRC     (1 << 2)
#define USHC_READ_RESP_ERR_DAT     (1 << 1)
#define USHC_READ_RESP_ERR_CMD     (1 << 0)
#define USHC_READ_RESP_ERR_MASK    0x0f

struct ushc_cbw {
	__u8 signature;
	__u8 cmd_idx;
	__le16 block_size;
	__le32 arg;
} __attribute__((packed));

#define USHC_CBW_SIGNATURE 'C'

struct ushc_csw {
	__u8 signature;
	__u8 status;
	__le32 response;
} __attribute__((packed));

#define USHC_CSW_SIGNATURE 'S'

struct ushc_int_data {
	u8 status;
	u8 reserved[3];
};

#define USHC_INT_STATUS_SDIO_INT     (1 << 1)
#define USHC_INT_STATUS_CARD_PRESENT (1 << 0)


struct ushc_data {
	struct usb_device *usb_dev;
	struct mmc_host *mmc;

	struct urb *int_urb;
	struct ushc_int_data *int_data;

	struct urb *cbw_urb;
	struct ushc_cbw *cbw;

	struct urb *data_urb;

	struct urb *csw_urb;
	struct ushc_csw *csw;

	spinlock_t lock;
	struct mmc_request *current_req;
	u32 caps;
	u16 host_ctrl;
	unsigned long flags;
	u8 last_status;
	int clock_freq;
};

#define DISCONNECTED    0
#define INT_EN          1
#define IGNORE_NEXT_INT 2

static void data_callback(struct urb *urb);

static int ushc_hw_reset(struct ushc_data *ushc)
{
	return usb_control_msg(ushc->usb_dev, usb_sndctrlpipe(ushc->usb_dev, 0),
			       USHC_RESET, USHC_RESET_TYPE,
			       0, 0, NULL, 0, 100);
}

static int ushc_hw_get_caps(struct ushc_data *ushc)
{
	int ret;
	int version;

	ret = usb_control_msg(ushc->usb_dev, usb_rcvctrlpipe(ushc->usb_dev, 0),
			      USHC_GET_CAPS, USHC_GET_CAPS_TYPE,
			      0, 0, &ushc->caps, sizeof(ushc->caps), 100);
	if (ret < 0)
		return ret;

	ushc->caps = le32_to_cpu(ushc->caps);

	version = ushc->caps & USHC_GET_CAPS_VERSION_MASK;
	if (version != 0x02) {
		dev_err(&ushc->usb_dev->dev, "controller version %d is not supported\n", version);
		return -EINVAL;
	}

	return 0;
}

static int ushc_hw_set_host_ctrl(struct ushc_data *ushc, u16 mask, u16 val)
{
	u16 host_ctrl;
	int ret;

	host_ctrl = (ushc->host_ctrl & ~mask) | val;
	ret = usb_control_msg(ushc->usb_dev, usb_sndctrlpipe(ushc->usb_dev, 0),
			      USHC_HOST_CTRL, USHC_HOST_CTRL_TYPE,
			      host_ctrl, 0, NULL, 0, 100);
	if (ret < 0)
		return ret;
	ushc->host_ctrl = host_ctrl;
	return 0;
}

static void int_callback(struct urb *urb)
{
	struct ushc_data *ushc = urb->context;
	u8 status, last_status;

	if (urb->status < 0)
		return;

	status = ushc->int_data->status;
	last_status = ushc->last_status;
	ushc->last_status = status;

	/*
	 * Ignore the card interrupt status on interrupt transfers that
	 * were submitted while card interrupts where disabled.
	 *
	 * This avoid occasional spurious interrupts when enabling
	 * interrupts immediately after clearing the source on the card.
	 */

	if (!test_and_clear_bit(IGNORE_NEXT_INT, &ushc->flags)
	    && test_bit(INT_EN, &ushc->flags)
	    && status & USHC_INT_STATUS_SDIO_INT) {
		mmc_signal_sdio_irq(ushc->mmc);
	}

	if ((status ^ last_status) & USHC_INT_STATUS_CARD_PRESENT)
		mmc_detect_change(ushc->mmc, msecs_to_jiffies(100));

	if (!test_bit(INT_EN, &ushc->flags))
		set_bit(IGNORE_NEXT_INT, &ushc->flags);
	usb_submit_urb(ushc->int_urb, GFP_ATOMIC);
}

static void cbw_callback(struct urb *urb)
{
	struct ushc_data *ushc = urb->context;

	if (urb->status != 0) {
		usb_unlink_urb(ushc->data_urb);
		usb_unlink_urb(ushc->csw_urb);
	}
}

static void data_callback(struct urb *urb)
{
	struct ushc_data *ushc = urb->context;

	if (urb->status != 0)
		usb_unlink_urb(ushc->csw_urb);
}

static void csw_callback(struct urb *urb)
{
	struct ushc_data *ushc = urb->context;
	struct mmc_request *req = ushc->current_req;
	int status;

	status = ushc->csw->status;

	if (urb->status != 0) {
		req->cmd->error = urb->status;
	} else if (status & USHC_READ_RESP_ERR_CMD) {
		if (status & USHC_READ_RESP_ERR_CRC)
			req->cmd->error = -EIO;
		else
			req->cmd->error = -ETIMEDOUT;
	}
	if (req->data) {
		if (status & USHC_READ_RESP_ERR_DAT) {
			if (status & USHC_READ_RESP_ERR_CRC)
				req->data->error = -EIO;
			else
				req->data->error = -ETIMEDOUT;
			req->data->bytes_xfered = 0;
		} else {
			req->data->bytes_xfered = req->data->blksz * req->data->blocks;
		}
	}

	req->cmd->resp[0] = le32_to_cpu(ushc->csw->response);

	mmc_request_done(ushc->mmc, req);
}

static void ushc_request(struct mmc_host *mmc, struct mmc_request *req)
{
	struct ushc_data *ushc = mmc_priv(mmc);
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&ushc->lock, flags);

	if (test_bit(DISCONNECTED, &ushc->flags)) {
		ret = -ENODEV;
		goto out;
	}

	/* Version 2 firmware doesn't support the R2 response format. */
	if (req->cmd->flags & MMC_RSP_136) {
		ret = -EINVAL;
		goto out;
	}

	/* The Astoria's data FIFOs don't work with clock speeds < 5MHz so
	   limit commands with data to 6MHz or more. */
	if (req->data && ushc->clock_freq < 6000000) {
		ret = -EINVAL;
		goto out;
	}

	ushc->current_req = req;

	/* Start cmd with CBW. */
	ushc->cbw->cmd_idx = cpu_to_le16(req->cmd->opcode);
	if (req->data)
		ushc->cbw->block_size = cpu_to_le16(req->data->blksz);
	else
		ushc->cbw->block_size = 0;
	ushc->cbw->arg = cpu_to_le32(req->cmd->arg);

	ret = usb_submit_urb(ushc->cbw_urb, GFP_ATOMIC);
	if (ret < 0)
		goto out;

	/* Submit data (if any). */
	if (req->data) {
		struct mmc_data *data = req->data;
		int pipe;

		if (data->flags & MMC_DATA_READ)
			pipe = usb_rcvbulkpipe(ushc->usb_dev, 6);
		else
			pipe = usb_sndbulkpipe(ushc->usb_dev, 2);

		usb_fill_bulk_urb(ushc->data_urb, ushc->usb_dev, pipe,
				  sg_virt(data->sg), data->sg->length,
				  data_callback, ushc);
		ret = usb_submit_urb(ushc->data_urb, GFP_ATOMIC);
		if (ret < 0)
			goto out;
	}

	/* Submit CSW. */
	ret = usb_submit_urb(ushc->csw_urb, GFP_ATOMIC);
	if (ret < 0)
		goto out;

out:
	spin_unlock_irqrestore(&ushc->lock, flags);
	if (ret < 0) {
		usb_unlink_urb(ushc->cbw_urb);
		usb_unlink_urb(ushc->data_urb);
		req->cmd->error = ret;
		mmc_request_done(mmc, req);
	}
}

static int ushc_set_power(struct ushc_data *ushc, unsigned char power_mode)
{
	u16 voltage;

	switch (power_mode) {
	case MMC_POWER_OFF:
		voltage = USHC_PWR_CTRL_OFF;
		break;
	case MMC_POWER_UP:
	case MMC_POWER_ON:
		voltage = USHC_PWR_CTRL_3V3;
		break;
	default:
		return -EINVAL;
	}

	return usb_control_msg(ushc->usb_dev, usb_sndctrlpipe(ushc->usb_dev, 0),
			       USHC_PWR_CTRL, USHC_PWR_CTRL_TYPE,
			       voltage, 0, NULL, 0, 100);
}

static int ushc_set_bus_width(struct ushc_data *ushc, int bus_width)
{
	return ushc_hw_set_host_ctrl(ushc, USHC_HOST_CTRL_4BIT,
				     bus_width == 4 ? USHC_HOST_CTRL_4BIT : 0);
}

static int ushc_set_bus_freq(struct ushc_data *ushc, int clk, bool enable_hs)
{
	int ret;

	/* Hardware can't detect interrupts while the clock is off. */
	if (clk == 0)
		clk = 400000;

	ret = ushc_hw_set_host_ctrl(ushc, USHC_HOST_CTRL_HIGH_SPD,
				    enable_hs ? USHC_HOST_CTRL_HIGH_SPD : 0);
	if (ret < 0)
		return ret;

	ret = usb_control_msg(ushc->usb_dev, usb_sndctrlpipe(ushc->usb_dev, 0),
			      USHC_CLK_FREQ, USHC_CLK_FREQ_TYPE,
			      clk & 0xffff, (clk >> 16) & 0xffff, NULL, 0, 100);
	if (ret < 0)
		return ret;

	ushc->clock_freq = clk;
	return 0;
}

static void ushc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct ushc_data *ushc = mmc_priv(mmc);

	ushc_set_power(ushc, ios->power_mode);
	ushc_set_bus_width(ushc, 1 << ios->bus_width);
	ushc_set_bus_freq(ushc, ios->clock, ios->timing == MMC_TIMING_SD_HS);
}

static int ushc_get_cd(struct mmc_host *mmc)
{
	struct ushc_data *ushc = mmc_priv(mmc);

	return !!(ushc->last_status & USHC_INT_STATUS_CARD_PRESENT);
}

static void ushc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct ushc_data *ushc = mmc_priv(mmc);

	if (enable)
		set_bit(INT_EN, &ushc->flags);
	else
		clear_bit(INT_EN, &ushc->flags);
}

static void ushc_clean_up(struct ushc_data *ushc)
{
	usb_free_urb(ushc->int_urb);
	usb_free_urb(ushc->csw_urb);
	usb_free_urb(ushc->data_urb);
	usb_free_urb(ushc->cbw_urb);

	kfree(ushc->int_data);
	kfree(ushc->cbw);
	kfree(ushc->csw);

	mmc_free_host(ushc->mmc);
}

static const struct mmc_host_ops ushc_ops = {
	.request         = ushc_request,
	.set_ios         = ushc_set_ios,
	.get_cd          = ushc_get_cd,
	.enable_sdio_irq = ushc_enable_sdio_irq,
};

static int ushc_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct mmc_host *mmc;
	struct ushc_data *ushc;
	int ret;

	if (intf->cur_altsetting->desc.bNumEndpoints < 1)
		return -ENODEV;

	mmc = mmc_alloc_host(sizeof(struct ushc_data), &intf->dev);
	if (mmc == NULL)
		return -ENOMEM;
	ushc = mmc_priv(mmc);
	usb_set_intfdata(intf, ushc);

	ushc->usb_dev = usb_dev;
	ushc->mmc = mmc;

	spin_lock_init(&ushc->lock);

	ret = ushc_hw_reset(ushc);
	if (ret < 0)
		goto err;

	/* Read capabilities. */
	ret = ushc_hw_get_caps(ushc);
	if (ret < 0)
		goto err;

	mmc->ops = &ushc_ops;

	mmc->f_min = 400000;
	mmc->f_max = 50000000;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_SDIO_IRQ;
	mmc->caps |= (ushc->caps & USHC_GET_CAPS_HIGH_SPD) ? MMC_CAP_SD_HIGHSPEED : 0;

	mmc->max_seg_size  = 512*511;
	mmc->max_segs      = 1;
	mmc->max_req_size  = 512*511;
	mmc->max_blk_size  = 512;
	mmc->max_blk_count = 511;

	ushc->int_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (ushc->int_urb == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	ushc->int_data = kzalloc(sizeof(struct ushc_int_data), GFP_KERNEL);
	if (ushc->int_data == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	usb_fill_int_urb(ushc->int_urb, ushc->usb_dev,
			 usb_rcvintpipe(usb_dev,
					intf->cur_altsetting->endpoint[0].desc.bEndpointAddress),
			 ushc->int_data, sizeof(struct ushc_int_data),
			 int_callback, ushc,
			 intf->cur_altsetting->endpoint[0].desc.bInterval);

	ushc->cbw_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (ushc->cbw_urb == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	ushc->cbw = kzalloc(sizeof(struct ushc_cbw), GFP_KERNEL);
	if (ushc->cbw == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	ushc->cbw->signature = USHC_CBW_SIGNATURE;

	usb_fill_bulk_urb(ushc->cbw_urb, ushc->usb_dev, usb_sndbulkpipe(usb_dev, 2),
			  ushc->cbw, sizeof(struct ushc_cbw),
			  cbw_callback, ushc);

	ushc->data_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (ushc->data_urb == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	ushc->csw_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (ushc->csw_urb == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	ushc->csw = kzalloc(sizeof(struct ushc_csw), GFP_KERNEL);
	if (ushc->csw == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	usb_fill_bulk_urb(ushc->csw_urb, ushc->usb_dev, usb_rcvbulkpipe(usb_dev, 6),
			  ushc->csw, sizeof(struct ushc_csw),
			  csw_callback, ushc);

	ret = mmc_add_host(ushc->mmc);
	if (ret)
		goto err;

	ret = usb_submit_urb(ushc->int_urb, GFP_KERNEL);
	if (ret < 0) {
		mmc_remove_host(ushc->mmc);
		goto err;
	}

	return 0;

err:
	ushc_clean_up(ushc);
	return ret;
}

static void ushc_disconnect(struct usb_interface *intf)
{
	struct ushc_data *ushc = usb_get_intfdata(intf);

	spin_lock_irq(&ushc->lock);
	set_bit(DISCONNECTED, &ushc->flags);
	spin_unlock_irq(&ushc->lock);

	usb_kill_urb(ushc->int_urb);
	usb_kill_urb(ushc->cbw_urb);
	usb_kill_urb(ushc->data_urb);
	usb_kill_urb(ushc->csw_urb);

	mmc_remove_host(ushc->mmc);

	ushc_clean_up(ushc);
}

static struct usb_device_id ushc_id_table[] = {
	/* CSR USB SD Host Controller */
	{ USB_DEVICE(0x0a12, 0x5d10) },
	{ },
};
MODULE_DEVICE_TABLE(usb, ushc_id_table);

static struct usb_driver ushc_driver = {
	.name       = "ushc",
	.id_table   = ushc_id_table,
	.probe      = ushc_probe,
	.disconnect = ushc_disconnect,
};

module_usb_driver(ushc_driver);

MODULE_DESCRIPTION("USB SD Host Controller driver");
MODULE_AUTHOR("David Vrabel <david.vrabel@csr.com>");
MODULE_LICENSE("GPL");
