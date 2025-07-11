// SPDX-License-Identifier: GPL-2.0-only
/* Driver for Realtek USB card reader
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * Author:
 *   Roger Tseng <rogerable@realtek.com>
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/usb.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/rtsx_usb.h>

static int polling_pipe = 1;
module_param(polling_pipe, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(polling_pipe, "polling pipe (0: ctl, 1: bulk)");

static const struct mfd_cell rtsx_usb_cells[] = {
	[RTSX_USB_SD_CARD] = {
		.name = DRV_NAME_RTSX_USB_SDMMC,
		.pdata_size = 0,
	},
	[RTSX_USB_MS_CARD] = {
		.name = DRV_NAME_RTSX_USB_MS,
		.pdata_size = 0,
	},
};

static void rtsx_usb_sg_timed_out(struct timer_list *t)
{
	struct rtsx_ucr *ucr = timer_container_of(ucr, t, sg_timer);

	dev_dbg(&ucr->pusb_intf->dev, "%s: sg transfer timed out", __func__);
	usb_sg_cancel(&ucr->current_sg);
}

static int rtsx_usb_bulk_transfer_sglist(struct rtsx_ucr *ucr,
		unsigned int pipe, struct scatterlist *sg, int num_sg,
		unsigned int length, unsigned int *act_len, int timeout)
{
	int ret;

	dev_dbg(&ucr->pusb_intf->dev, "%s: xfer %u bytes, %d entries\n",
			__func__, length, num_sg);
	ret = usb_sg_init(&ucr->current_sg, ucr->pusb_dev, pipe, 0,
			sg, num_sg, length, GFP_NOIO);
	if (ret)
		return ret;

	ucr->sg_timer.expires = jiffies + msecs_to_jiffies(timeout);
	add_timer(&ucr->sg_timer);
	usb_sg_wait(&ucr->current_sg);
	if (!timer_delete_sync(&ucr->sg_timer))
		ret = -ETIMEDOUT;
	else
		ret = ucr->current_sg.status;

	if (act_len)
		*act_len = ucr->current_sg.bytes;

	return ret;
}

int rtsx_usb_transfer_data(struct rtsx_ucr *ucr, unsigned int pipe,
			      void *buf, unsigned int len, int num_sg,
			      unsigned int *act_len, int timeout)
{
	if (timeout < 600)
		timeout = 600;

	if (num_sg)
		return rtsx_usb_bulk_transfer_sglist(ucr, pipe,
				(struct scatterlist *)buf, num_sg, len, act_len,
				timeout);
	else
		return usb_bulk_msg(ucr->pusb_dev, pipe, buf, len, act_len,
				timeout);
}
EXPORT_SYMBOL_GPL(rtsx_usb_transfer_data);

static inline void rtsx_usb_seq_cmd_hdr(struct rtsx_ucr *ucr,
		u16 addr, u16 len, u8 seq_type)
{
	rtsx_usb_cmd_hdr_tag(ucr);

	ucr->cmd_buf[PACKET_TYPE] = seq_type;
	ucr->cmd_buf[5] = (u8)(len >> 8);
	ucr->cmd_buf[6] = (u8)len;
	ucr->cmd_buf[8] = (u8)(addr >> 8);
	ucr->cmd_buf[9] = (u8)addr;

	if (seq_type == SEQ_WRITE)
		ucr->cmd_buf[STAGE_FLAG] = 0;
	else
		ucr->cmd_buf[STAGE_FLAG] = STAGE_R;
}

static int rtsx_usb_seq_write_register(struct rtsx_ucr *ucr,
		u16 addr, u16 len, u8 *data)
{
	u16 cmd_len = ALIGN(SEQ_WRITE_DATA_OFFSET + len, 4);

	if (!data)
		return -EINVAL;

	if (cmd_len > IOBUF_SIZE)
		return -EINVAL;

	rtsx_usb_seq_cmd_hdr(ucr, addr, len, SEQ_WRITE);
	memcpy(ucr->cmd_buf + SEQ_WRITE_DATA_OFFSET, data, len);

	return rtsx_usb_transfer_data(ucr,
			usb_sndbulkpipe(ucr->pusb_dev, EP_BULK_OUT),
			ucr->cmd_buf, cmd_len, 0, NULL, 100);
}

static int rtsx_usb_seq_read_register(struct rtsx_ucr *ucr,
		u16 addr, u16 len, u8 *data)
{
	int i, ret;
	u16 rsp_len = round_down(len, 4);
	u16 res_len = len - rsp_len;

	if (!data)
		return -EINVAL;

	/* 4-byte aligned part */
	if (rsp_len) {
		rtsx_usb_seq_cmd_hdr(ucr, addr, len, SEQ_READ);
		ret = rtsx_usb_transfer_data(ucr,
				usb_sndbulkpipe(ucr->pusb_dev, EP_BULK_OUT),
				ucr->cmd_buf, 12, 0, NULL, 100);
		if (ret)
			return ret;

		ret = rtsx_usb_transfer_data(ucr,
				usb_rcvbulkpipe(ucr->pusb_dev, EP_BULK_IN),
				data, rsp_len, 0, NULL, 100);
		if (ret)
			return ret;
	}

	/* unaligned part */
	for (i = 0; i < res_len; i++) {
		ret = rtsx_usb_read_register(ucr, addr + rsp_len + i,
				data + rsp_len + i);
		if (ret)
			return ret;
	}

	return 0;
}

int rtsx_usb_read_ppbuf(struct rtsx_ucr *ucr, u8 *buf, int buf_len)
{
	return rtsx_usb_seq_read_register(ucr, PPBUF_BASE2, (u16)buf_len, buf);
}
EXPORT_SYMBOL_GPL(rtsx_usb_read_ppbuf);

int rtsx_usb_write_ppbuf(struct rtsx_ucr *ucr, u8 *buf, int buf_len)
{
	return rtsx_usb_seq_write_register(ucr, PPBUF_BASE2, (u16)buf_len, buf);
}
EXPORT_SYMBOL_GPL(rtsx_usb_write_ppbuf);

int rtsx_usb_ep0_write_register(struct rtsx_ucr *ucr, u16 addr,
		u8 mask, u8 data)
{
	u16 value, index;

	addr |= EP0_WRITE_REG_CMD << EP0_OP_SHIFT;
	value = swab16(addr);
	index = mask | data << 8;

	return usb_control_msg(ucr->pusb_dev,
			usb_sndctrlpipe(ucr->pusb_dev, 0), RTSX_USB_REQ_REG_OP,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, index, NULL, 0, 100);
}
EXPORT_SYMBOL_GPL(rtsx_usb_ep0_write_register);

int rtsx_usb_ep0_read_register(struct rtsx_ucr *ucr, u16 addr, u8 *data)
{
	u16 value;
	u8 *buf;
	int ret;

	if (!data)
		return -EINVAL;

	buf = kzalloc(sizeof(u8), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	addr |= EP0_READ_REG_CMD << EP0_OP_SHIFT;
	value = swab16(addr);

	ret = usb_control_msg(ucr->pusb_dev,
			usb_rcvctrlpipe(ucr->pusb_dev, 0), RTSX_USB_REQ_REG_OP,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, 0, buf, 1, 100);
	*data = *buf;

	kfree(buf);
	return ret;
}
EXPORT_SYMBOL_GPL(rtsx_usb_ep0_read_register);

void rtsx_usb_add_cmd(struct rtsx_ucr *ucr, u8 cmd_type, u16 reg_addr,
		u8 mask, u8 data)
{
	int i;

	if (ucr->cmd_idx < (IOBUF_SIZE - CMD_OFFSET) / 4) {
		i = CMD_OFFSET + ucr->cmd_idx * 4;

		ucr->cmd_buf[i++] = ((cmd_type & 0x03) << 6) |
			(u8)((reg_addr >> 8) & 0x3F);
		ucr->cmd_buf[i++] = (u8)reg_addr;
		ucr->cmd_buf[i++] = mask;
		ucr->cmd_buf[i++] = data;

		ucr->cmd_idx++;
	}
}
EXPORT_SYMBOL_GPL(rtsx_usb_add_cmd);

int rtsx_usb_send_cmd(struct rtsx_ucr *ucr, u8 flag, int timeout)
{
	int ret;

	ucr->cmd_buf[CNT_H] = (u8)(ucr->cmd_idx >> 8);
	ucr->cmd_buf[CNT_L] = (u8)(ucr->cmd_idx);
	ucr->cmd_buf[STAGE_FLAG] = flag;

	ret = rtsx_usb_transfer_data(ucr,
			usb_sndbulkpipe(ucr->pusb_dev, EP_BULK_OUT),
			ucr->cmd_buf, ucr->cmd_idx * 4 + CMD_OFFSET,
			0, NULL, timeout);
	if (ret) {
		rtsx_usb_clear_fsm_err(ucr);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rtsx_usb_send_cmd);

int rtsx_usb_get_rsp(struct rtsx_ucr *ucr, int rsp_len, int timeout)
{
	if (rsp_len <= 0)
		return -EINVAL;

	rsp_len = ALIGN(rsp_len, 4);

	return rtsx_usb_transfer_data(ucr,
			usb_rcvbulkpipe(ucr->pusb_dev, EP_BULK_IN),
			ucr->rsp_buf, rsp_len, 0, NULL, timeout);
}
EXPORT_SYMBOL_GPL(rtsx_usb_get_rsp);

static int rtsx_usb_get_status_with_bulk(struct rtsx_ucr *ucr, u16 *status)
{
	int ret;

	rtsx_usb_init_cmd(ucr);
	rtsx_usb_add_cmd(ucr, READ_REG_CMD, CARD_EXIST, 0x00, 0x00);
	rtsx_usb_add_cmd(ucr, READ_REG_CMD, OCPSTAT, 0x00, 0x00);
	ret = rtsx_usb_send_cmd(ucr, MODE_CR, 100);
	if (ret)
		return ret;

	ret = rtsx_usb_get_rsp(ucr, 2, 100);
	if (ret)
		return ret;

	*status = ((ucr->rsp_buf[0] >> 2) & 0x0f) |
		  ((ucr->rsp_buf[1] & 0x03) << 4);

	return 0;
}

int rtsx_usb_get_card_status(struct rtsx_ucr *ucr, u16 *status)
{
	int ret;
	u16 *buf;

	if (!status)
		return -EINVAL;

	if (polling_pipe == 0) {
		buf = kzalloc(sizeof(u16), GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		ret = usb_control_msg(ucr->pusb_dev,
				usb_rcvctrlpipe(ucr->pusb_dev, 0),
				RTSX_USB_REQ_POLL,
				USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				0, 0, buf, 2, 100);
		*status = *buf;

		kfree(buf);
	} else {
		ret = rtsx_usb_get_status_with_bulk(ucr, status);
	}

	/* usb_control_msg may return positive when success */
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(rtsx_usb_get_card_status);

static int rtsx_usb_write_phy_register(struct rtsx_ucr *ucr, u8 addr, u8 val)
{
	dev_dbg(&ucr->pusb_intf->dev, "Write 0x%x to phy register 0x%x\n",
			val, addr);

	rtsx_usb_init_cmd(ucr);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, HS_VSTAIN, 0xFF, val);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, HS_VCONTROL, 0xFF, addr & 0x0F);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x00);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x00);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x01);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, HS_VCONTROL,
			0xFF, (addr >> 4) & 0x0F);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x00);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x00);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, HS_VLOADM, 0xFF, 0x01);

	return rtsx_usb_send_cmd(ucr, MODE_C, 100);
}

int rtsx_usb_write_register(struct rtsx_ucr *ucr, u16 addr, u8 mask, u8 data)
{
	rtsx_usb_init_cmd(ucr);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, addr, mask, data);
	return rtsx_usb_send_cmd(ucr, MODE_C, 100);
}
EXPORT_SYMBOL_GPL(rtsx_usb_write_register);

int rtsx_usb_read_register(struct rtsx_ucr *ucr, u16 addr, u8 *data)
{
	int ret;

	if (data != NULL)
		*data = 0;

	rtsx_usb_init_cmd(ucr);
	rtsx_usb_add_cmd(ucr, READ_REG_CMD, addr, 0, 0);
	ret = rtsx_usb_send_cmd(ucr, MODE_CR, 100);
	if (ret)
		return ret;

	ret = rtsx_usb_get_rsp(ucr, 1, 100);
	if (ret)
		return ret;

	if (data != NULL)
		*data = ucr->rsp_buf[0];

	return 0;
}
EXPORT_SYMBOL_GPL(rtsx_usb_read_register);

static inline u8 double_ssc_depth(u8 depth)
{
	return (depth > 1) ? (depth - 1) : depth;
}

static u8 revise_ssc_depth(u8 ssc_depth, u8 div)
{
	if (div > CLK_DIV_1) {
		if (ssc_depth > div - 1)
			ssc_depth -= (div - 1);
		else
			ssc_depth = SSC_DEPTH_2M;
	}

	return ssc_depth;
}

int rtsx_usb_switch_clock(struct rtsx_ucr *ucr, unsigned int card_clock,
		u8 ssc_depth, bool initial_mode, bool double_clk, bool vpclk)
{
	int ret;
	u8 n, clk_divider, mcu_cnt, div;

	if (!card_clock) {
		ucr->cur_clk = 0;
		return 0;
	}

	if (initial_mode) {
		/* We use 250k(around) here, in initial stage */
		clk_divider = SD_CLK_DIVIDE_128;
		card_clock = 30000000;
	} else {
		clk_divider = SD_CLK_DIVIDE_0;
	}

	ret = rtsx_usb_write_register(ucr, SD_CFG1,
			SD_CLK_DIVIDE_MASK, clk_divider);
	if (ret < 0)
		return ret;

	card_clock /= 1000000;
	dev_dbg(&ucr->pusb_intf->dev,
			"Switch card clock to %dMHz\n", card_clock);

	if (!initial_mode && double_clk)
		card_clock *= 2;
	dev_dbg(&ucr->pusb_intf->dev,
			"Internal SSC clock: %dMHz (cur_clk = %d)\n",
			card_clock, ucr->cur_clk);

	if (card_clock == ucr->cur_clk)
		return 0;

	/* Converting clock value into internal settings: n and div */
	n = card_clock - 2;
	if ((card_clock <= 2) || (n > MAX_DIV_N))
		return -EINVAL;

	mcu_cnt = 60/card_clock + 3;
	if (mcu_cnt > 15)
		mcu_cnt = 15;

	/* Make sure that the SSC clock div_n is not less than MIN_DIV_N */

	div = CLK_DIV_1;
	while (n < MIN_DIV_N && div < CLK_DIV_4) {
		n = (n + 2) * 2 - 2;
		div++;
	}
	dev_dbg(&ucr->pusb_intf->dev, "n = %d, div = %d\n", n, div);

	if (double_clk)
		ssc_depth = double_ssc_depth(ssc_depth);

	ssc_depth = revise_ssc_depth(ssc_depth, div);
	dev_dbg(&ucr->pusb_intf->dev, "ssc_depth = %d\n", ssc_depth);

	rtsx_usb_init_cmd(ucr);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CLK_DIV, CLK_CHANGE, CLK_CHANGE);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CLK_DIV,
			0x3F, (div << 4) | mcu_cnt);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SSC_CTL1, SSC_RSTB, 0);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SSC_CTL2,
			SSC_DEPTH_MASK, ssc_depth);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SSC_DIV_N_0, 0xFF, n);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SSC_CTL1, SSC_RSTB, SSC_RSTB);
	if (vpclk) {
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_VPCLK0_CTL,
				PHASE_NOT_RESET, 0);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_VPCLK0_CTL,
				PHASE_NOT_RESET, PHASE_NOT_RESET);
	}

	ret = rtsx_usb_send_cmd(ucr, MODE_C, 2000);
	if (ret < 0)
		return ret;

	ret = rtsx_usb_write_register(ucr, SSC_CTL1, 0xff,
			SSC_RSTB | SSC_8X_EN | SSC_SEL_4M);
	if (ret < 0)
		return ret;

	/* Wait SSC clock stable */
	usleep_range(100, 1000);

	ret = rtsx_usb_write_register(ucr, CLK_DIV, CLK_CHANGE, 0);
	if (ret < 0)
		return ret;

	ucr->cur_clk = card_clock;

	return 0;
}
EXPORT_SYMBOL_GPL(rtsx_usb_switch_clock);

int rtsx_usb_card_exclusive_check(struct rtsx_ucr *ucr, int card)
{
	int ret;
	u16 val;
	u16 cd_mask[] = {
		[RTSX_USB_SD_CARD] = (CD_MASK & ~SD_CD),
		[RTSX_USB_MS_CARD] = (CD_MASK & ~MS_CD)
	};

	ret = rtsx_usb_get_card_status(ucr, &val);
	/*
	 * If get status fails, return 0 (ok) for the exclusive check
	 * and let the flow fail at somewhere else.
	 */
	if (ret)
		return 0;

	if (val & cd_mask[card])
		return -EIO;

	return 0;
}
EXPORT_SYMBOL_GPL(rtsx_usb_card_exclusive_check);

static int rtsx_usb_reset_chip(struct rtsx_ucr *ucr)
{
	int ret;
	u8 val;

	rtsx_usb_init_cmd(ucr);

	if (CHECK_PKG(ucr, LQFP48)) {
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PWR_CTL,
				LDO3318_PWR_MASK, LDO_SUSPEND);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PWR_CTL,
				FORCE_LDO_POWERB, FORCE_LDO_POWERB);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL1,
				0x30, 0x10);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL5,
				0x03, 0x01);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL6,
				0x0C, 0x04);
	}

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SYS_DUMMY0, NYET_MSAK, NYET_EN);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CD_DEGLITCH_WIDTH, 0xFF, 0x08);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
			CD_DEGLITCH_EN, XD_CD_DEGLITCH_EN, 0x0);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD30_DRIVE_SEL,
			SD30_DRIVE_MASK, DRIVER_TYPE_D);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
			CARD_DRIVE_SEL, SD20_DRIVE_MASK, 0x0);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, LDO_POWER_CFG, 0xE0, 0x0);

	if (ucr->is_rts5179)
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
				CARD_PULL_CTL5, 0x03, 0x01);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_DMA1_CTL,
		       EXTEND_DMA1_ASYNC_SIGNAL, EXTEND_DMA1_ASYNC_SIGNAL);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_INT_PEND,
			XD_INT | MS_INT | SD_INT,
			XD_INT | MS_INT | SD_INT);

	ret = rtsx_usb_send_cmd(ucr, MODE_C, 100);
	if (ret)
		return ret;

	/* config non-crystal mode */
	rtsx_usb_read_register(ucr, CFG_MODE, &val);
	if ((val & XTAL_FREE) || ((val & CLK_MODE_MASK) == CLK_MODE_NON_XTAL)) {
		ret = rtsx_usb_write_phy_register(ucr, 0xC2, 0x7C);
		if (ret)
			return ret;
	}

	return 0;
}

static int rtsx_usb_init_chip(struct rtsx_ucr *ucr)
{
	int ret;
	u8 val;

	rtsx_usb_clear_fsm_err(ucr);

	/* power on SSC */
	ret = rtsx_usb_write_register(ucr,
			FPDCTL, SSC_POWER_MASK, SSC_POWER_ON);
	if (ret)
		return ret;

	usleep_range(100, 1000);
	ret = rtsx_usb_write_register(ucr, CLK_DIV, CLK_CHANGE, 0x00);
	if (ret)
		return ret;

	/* determine IC version */
	ret = rtsx_usb_read_register(ucr, HW_VERSION, &val);
	if (ret)
		return ret;

	ucr->ic_version = val & HW_VER_MASK;

	/* determine package */
	ret = rtsx_usb_read_register(ucr, CARD_SHARE_MODE, &val);
	if (ret)
		return ret;

	if (val & CARD_SHARE_LQFP_SEL) {
		ucr->package = LQFP48;
		dev_dbg(&ucr->pusb_intf->dev, "Package: LQFP48\n");
	} else {
		ucr->package = QFN24;
		dev_dbg(&ucr->pusb_intf->dev, "Package: QFN24\n");
	}

	/* determine IC variations */
	rtsx_usb_read_register(ucr, CFG_MODE_1, &val);
	if (val & RTS5179) {
		ucr->is_rts5179 = true;
		dev_dbg(&ucr->pusb_intf->dev, "Device is rts5179\n");
	} else {
		ucr->is_rts5179 = false;
	}

	return rtsx_usb_reset_chip(ucr);
}

static int rtsx_usb_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct rtsx_ucr *ucr;
	int ret;

	dev_dbg(&intf->dev,
		": Realtek USB Card Reader found at bus %03d address %03d\n",
		 usb_dev->bus->busnum, usb_dev->devnum);

	ucr = devm_kzalloc(&intf->dev, sizeof(*ucr), GFP_KERNEL);
	if (!ucr)
		return -ENOMEM;

	ucr->pusb_dev = usb_dev;

	ucr->cmd_buf = kmalloc(IOBUF_SIZE, GFP_KERNEL);
	if (!ucr->cmd_buf)
		return -ENOMEM;

	ucr->rsp_buf = kmalloc(IOBUF_SIZE, GFP_KERNEL);
	if (!ucr->rsp_buf) {
		ret = -ENOMEM;
		goto out_free_cmd_buf;
	}

	usb_set_intfdata(intf, ucr);

	ucr->vendor_id = id->idVendor;
	ucr->product_id = id->idProduct;

	mutex_init(&ucr->dev_mutex);

	ucr->pusb_intf = intf;

	/* initialize */
	ret = rtsx_usb_init_chip(ucr);
	if (ret)
		goto out_init_fail;

	/* initialize USB SG transfer timer */
	timer_setup(&ucr->sg_timer, rtsx_usb_sg_timed_out, 0);

	ret = mfd_add_hotplug_devices(&intf->dev, rtsx_usb_cells,
				      ARRAY_SIZE(rtsx_usb_cells));
	if (ret)
		goto out_init_fail;

#ifdef CONFIG_PM
	intf->needs_remote_wakeup = 1;
	usb_enable_autosuspend(usb_dev);
#endif

	return 0;

out_init_fail:
	usb_set_intfdata(ucr->pusb_intf, NULL);
	kfree(ucr->rsp_buf);
	ucr->rsp_buf = NULL;
out_free_cmd_buf:
	kfree(ucr->cmd_buf);
	ucr->cmd_buf = NULL;
	return ret;
}

static void rtsx_usb_disconnect(struct usb_interface *intf)
{
	struct rtsx_ucr *ucr = (struct rtsx_ucr *)usb_get_intfdata(intf);

	dev_dbg(&intf->dev, "%s called\n", __func__);

	mfd_remove_devices(&intf->dev);

	usb_set_intfdata(ucr->pusb_intf, NULL);

	kfree(ucr->cmd_buf);
	ucr->cmd_buf = NULL;

	kfree(ucr->rsp_buf);
	ucr->rsp_buf = NULL;
}

#ifdef CONFIG_PM
static int rtsx_usb_resume_child(struct device *dev, void *data)
{
	pm_request_resume(dev);
	return 0;
}

static int rtsx_usb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct rtsx_ucr *ucr =
		(struct rtsx_ucr *)usb_get_intfdata(intf);
	u16 val = 0;

	dev_dbg(&intf->dev, "%s called with pm message 0x%04x\n",
			__func__, message.event);

	if (PMSG_IS_AUTO(message)) {
		if (mutex_trylock(&ucr->dev_mutex)) {
			rtsx_usb_get_card_status(ucr, &val);
			mutex_unlock(&ucr->dev_mutex);

			/* Defer the autosuspend if card exists */
			if (val & (SD_CD | MS_CD)) {
				device_for_each_child(&intf->dev, NULL, rtsx_usb_resume_child);
				return -EAGAIN;
			}
		} else {
			/* There is an ongoing operation*/
			return -EAGAIN;
		}
	}

	return 0;
}

static int rtsx_usb_resume(struct usb_interface *intf)
{
	device_for_each_child(&intf->dev, NULL, rtsx_usb_resume_child);
	return 0;
}

static int rtsx_usb_reset_resume(struct usb_interface *intf)
{
	struct rtsx_ucr *ucr =
		(struct rtsx_ucr *)usb_get_intfdata(intf);

	rtsx_usb_reset_chip(ucr);
	device_for_each_child(&intf->dev, NULL, rtsx_usb_resume_child);
	return 0;
}

#else /* CONFIG_PM */

#define rtsx_usb_suspend NULL
#define rtsx_usb_resume NULL
#define rtsx_usb_reset_resume NULL

#endif /* CONFIG_PM */


static int rtsx_usb_pre_reset(struct usb_interface *intf)
{
	struct rtsx_ucr *ucr = (struct rtsx_ucr *)usb_get_intfdata(intf);

	mutex_lock(&ucr->dev_mutex);
	return 0;
}

static int rtsx_usb_post_reset(struct usb_interface *intf)
{
	struct rtsx_ucr *ucr = (struct rtsx_ucr *)usb_get_intfdata(intf);

	mutex_unlock(&ucr->dev_mutex);
	return 0;
}

static const struct usb_device_id rtsx_usb_usb_ids[] = {
	{ USB_DEVICE(0x0BDA, 0x0129) },
	{ USB_DEVICE(0x0BDA, 0x0139) },
	{ USB_DEVICE(0x0BDA, 0x0140) },
	{ }
};
MODULE_DEVICE_TABLE(usb, rtsx_usb_usb_ids);

static struct usb_driver rtsx_usb_driver = {
	.name			= DRV_NAME_RTSX_USB,
	.probe			= rtsx_usb_probe,
	.disconnect		= rtsx_usb_disconnect,
	.suspend		= rtsx_usb_suspend,
	.resume			= rtsx_usb_resume,
	.reset_resume		= rtsx_usb_reset_resume,
	.pre_reset		= rtsx_usb_pre_reset,
	.post_reset		= rtsx_usb_post_reset,
	.id_table		= rtsx_usb_usb_ids,
	.supports_autosuspend	= 1,
	.soft_unbind		= 1,
};

module_usb_driver(rtsx_usb_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Roger Tseng <rogerable@realtek.com>");
MODULE_DESCRIPTION("Realtek USB Card Reader Driver");
