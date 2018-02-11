/*
 * Copyright © 2010 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 * Jackie Li<yaodong.li@intel.com>
 */

#include <linux/freezer.h>
#include <video/mipi_display.h>

#include "mdfld_dsi_output.h"
#include "mdfld_dsi_pkg_sender.h"
#include "mdfld_dsi_dpi.h"

#define MDFLD_DSI_READ_MAX_COUNT		5000

enum {
	MDFLD_DSI_PANEL_MODE_SLEEP = 0x1,
};

enum {
	MDFLD_DSI_PKG_SENDER_FREE = 0x0,
	MDFLD_DSI_PKG_SENDER_BUSY = 0x1,
};

static const char *const dsi_errors[] = {
	"RX SOT Error",
	"RX SOT Sync Error",
	"RX EOT Sync Error",
	"RX Escape Mode Entry Error",
	"RX LP TX Sync Error",
	"RX HS Receive Timeout Error",
	"RX False Control Error",
	"RX ECC Single Bit Error",
	"RX ECC Multibit Error",
	"RX Checksum Error",
	"RX DSI Data Type Not Recognised",
	"RX DSI VC ID Invalid",
	"TX False Control Error",
	"TX ECC Single Bit Error",
	"TX ECC Multibit Error",
	"TX Checksum Error",
	"TX DSI Data Type Not Recognised",
	"TX DSI VC ID invalid",
	"High Contention",
	"Low contention",
	"DPI FIFO Under run",
	"HS TX Timeout",
	"LP RX Timeout",
	"Turn Around ACK Timeout",
	"ACK With No Error",
	"RX Invalid TX Length",
	"RX Prot Violation",
	"HS Generic Write FIFO Full",
	"LP Generic Write FIFO Full",
	"Generic Read Data Avail",
	"Special Packet Sent",
	"Tearing Effect",
};

static inline int wait_for_gen_fifo_empty(struct mdfld_dsi_pkg_sender *sender,
						u32 mask)
{
	struct drm_device *dev = sender->dev;
	u32 gen_fifo_stat_reg = sender->mipi_gen_fifo_stat_reg;
	int retry = 0xffff;

	while (retry--) {
		if ((mask & REG_READ(gen_fifo_stat_reg)) == mask)
			return 0;
		udelay(100);
	}
	DRM_ERROR("fifo is NOT empty 0x%08x\n", REG_READ(gen_fifo_stat_reg));
	return -EIO;
}

static int wait_for_all_fifos_empty(struct mdfld_dsi_pkg_sender *sender)
{
	return wait_for_gen_fifo_empty(sender, (BIT(2) | BIT(10) | BIT(18) |
						BIT(26) | BIT(27) | BIT(28)));
}

static int wait_for_lp_fifos_empty(struct mdfld_dsi_pkg_sender *sender)
{
	return wait_for_gen_fifo_empty(sender, (BIT(10) | BIT(26)));
}

static int wait_for_hs_fifos_empty(struct mdfld_dsi_pkg_sender *sender)
{
	return wait_for_gen_fifo_empty(sender, (BIT(2) | BIT(18)));
}

static int handle_dsi_error(struct mdfld_dsi_pkg_sender *sender, u32 mask)
{
	u32 intr_stat_reg = sender->mipi_intr_stat_reg;
	struct drm_device *dev = sender->dev;

	dev_dbg(sender->dev->dev, "Handling error 0x%08x\n", mask);

	switch (mask) {
	case BIT(0):
	case BIT(1):
	case BIT(2):
	case BIT(3):
	case BIT(4):
	case BIT(5):
	case BIT(6):
	case BIT(7):
	case BIT(8):
	case BIT(9):
	case BIT(10):
	case BIT(11):
	case BIT(12):
	case BIT(13):
		dev_dbg(sender->dev->dev, "No Action required\n");
		break;
	case BIT(14):
		/*wait for all fifo empty*/
		/*wait_for_all_fifos_empty(sender)*/
		break;
	case BIT(15):
		dev_dbg(sender->dev->dev, "No Action required\n");
		break;
	case BIT(16):
		break;
	case BIT(17):
		break;
	case BIT(18):
	case BIT(19):
		dev_dbg(sender->dev->dev, "High/Low contention detected\n");
		/*wait for contention recovery time*/
		/*mdelay(10);*/
		/*wait for all fifo empty*/
		if (0)
			wait_for_all_fifos_empty(sender);
		break;
	case BIT(20):
		dev_dbg(sender->dev->dev, "No Action required\n");
		break;
	case BIT(21):
		/*wait for all fifo empty*/
		/*wait_for_all_fifos_empty(sender);*/
		break;
	case BIT(22):
		break;
	case BIT(23):
	case BIT(24):
	case BIT(25):
	case BIT(26):
	case BIT(27):
		dev_dbg(sender->dev->dev, "HS Gen fifo full\n");
		REG_WRITE(intr_stat_reg, mask);
		wait_for_hs_fifos_empty(sender);
		break;
	case BIT(28):
		dev_dbg(sender->dev->dev, "LP Gen fifo full\n");
		REG_WRITE(intr_stat_reg, mask);
		wait_for_lp_fifos_empty(sender);
		break;
	case BIT(29):
	case BIT(30):
	case BIT(31):
		dev_dbg(sender->dev->dev, "No Action required\n");
		break;
	}

	if (mask & REG_READ(intr_stat_reg))
		dev_dbg(sender->dev->dev,
				"Cannot clean interrupt 0x%08x\n", mask);
	return 0;
}

static int dsi_error_handler(struct mdfld_dsi_pkg_sender *sender)
{
	struct drm_device *dev = sender->dev;
	u32 intr_stat_reg = sender->mipi_intr_stat_reg;
	u32 mask;
	u32 intr_stat;
	int i;
	int err = 0;

	intr_stat = REG_READ(intr_stat_reg);

	for (i = 0; i < 32; i++) {
		mask = (0x00000001UL) << i;
		if (intr_stat & mask) {
			dev_dbg(sender->dev->dev, "[DSI]: %s\n", dsi_errors[i]);
			err = handle_dsi_error(sender, mask);
			if (err)
				DRM_ERROR("Cannot handle error\n");
		}
	}
	return err;
}

static int send_short_pkg(struct mdfld_dsi_pkg_sender *sender, u8 data_type,
			u8 cmd, u8 param, bool hs)
{
	struct drm_device *dev = sender->dev;
	u32 ctrl_reg;
	u32 val;
	u8 virtual_channel = 0;

	if (hs) {
		ctrl_reg = sender->mipi_hs_gen_ctrl_reg;

		/* FIXME: wait_for_hs_fifos_empty(sender); */
	} else {
		ctrl_reg = sender->mipi_lp_gen_ctrl_reg;

		/* FIXME: wait_for_lp_fifos_empty(sender); */
	}

	val = FLD_VAL(param, 23, 16) | FLD_VAL(cmd, 15, 8) |
		FLD_VAL(virtual_channel, 7, 6) | FLD_VAL(data_type, 5, 0);

	REG_WRITE(ctrl_reg, val);

	return 0;
}

static int send_long_pkg(struct mdfld_dsi_pkg_sender *sender, u8 data_type,
			u8 *data, int len, bool hs)
{
	struct drm_device *dev = sender->dev;
	u32 ctrl_reg;
	u32 data_reg;
	u32 val;
	u8 *p;
	u8 b1, b2, b3, b4;
	u8 virtual_channel = 0;
	int i;

	if (hs) {
		ctrl_reg = sender->mipi_hs_gen_ctrl_reg;
		data_reg = sender->mipi_hs_gen_data_reg;

		/* FIXME: wait_for_hs_fifos_empty(sender); */
	} else {
		ctrl_reg = sender->mipi_lp_gen_ctrl_reg;
		data_reg = sender->mipi_lp_gen_data_reg;

		/* FIXME: wait_for_lp_fifos_empty(sender); */
	}

	p = data;
	for (i = 0; i < len / 4; i++) {
		b1 = *p++;
		b2 = *p++;
		b3 = *p++;
		b4 = *p++;

		REG_WRITE(data_reg, b4 << 24 | b3 << 16 | b2 << 8 | b1);
	}

	i = len % 4;
	if (i) {
		b1 = 0; b2 = 0; b3 = 0;

		switch (i) {
		case 3:
			b1 = *p++;
			b2 = *p++;
			b3 = *p++;
			break;
		case 2:
			b1 = *p++;
			b2 = *p++;
			break;
		case 1:
			b1 = *p++;
			break;
		}

		REG_WRITE(data_reg, b3 << 16 | b2 << 8 | b1);
	}

	val = FLD_VAL(len, 23, 8) | FLD_VAL(virtual_channel, 7, 6) |
		FLD_VAL(data_type, 5, 0);

	REG_WRITE(ctrl_reg, val);

	return 0;
}

static int send_pkg_prepare(struct mdfld_dsi_pkg_sender *sender, u8 data_type,
			u8 *data, u16 len)
{
	u8 cmd;

	switch (data_type) {
	case MIPI_DSI_DCS_SHORT_WRITE:
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
	case MIPI_DSI_DCS_LONG_WRITE:
		cmd = *data;
		break;
	default:
		return 0;
	}

	/*this prevents other package sending while doing msleep*/
	sender->status = MDFLD_DSI_PKG_SENDER_BUSY;

	/*wait for 120 milliseconds in case exit_sleep_mode just be sent*/
	if (unlikely(cmd == MIPI_DCS_ENTER_SLEEP_MODE)) {
		/*TODO: replace it with msleep later*/
		mdelay(120);
	}

	if (unlikely(cmd == MIPI_DCS_EXIT_SLEEP_MODE)) {
		/*TODO: replace it with msleep later*/
		mdelay(120);
	}
	return 0;
}

static int send_pkg_done(struct mdfld_dsi_pkg_sender *sender, u8 data_type,
			u8 *data, u16 len)
{
	u8 cmd;

	switch (data_type) {
	case MIPI_DSI_DCS_SHORT_WRITE:
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
	case MIPI_DSI_DCS_LONG_WRITE:
		cmd = *data;
		break;
	default:
		return 0;
	}

	/*update panel status*/
	if (unlikely(cmd == MIPI_DCS_ENTER_SLEEP_MODE)) {
		sender->panel_mode |= MDFLD_DSI_PANEL_MODE_SLEEP;
		/*TODO: replace it with msleep later*/
		mdelay(120);
	} else if (unlikely(cmd == MIPI_DCS_EXIT_SLEEP_MODE)) {
		sender->panel_mode &= ~MDFLD_DSI_PANEL_MODE_SLEEP;
		/*TODO: replace it with msleep later*/
		mdelay(120);
	} else if (unlikely(cmd == MIPI_DCS_SOFT_RESET)) {
		/*TODO: replace it with msleep later*/
		mdelay(5);
	}

	sender->status = MDFLD_DSI_PKG_SENDER_FREE;

	return 0;
}

static int send_pkg(struct mdfld_dsi_pkg_sender *sender, u8 data_type,
		u8 *data, u16 len, bool hs)
{
	int ret;

	/*handle DSI error*/
	ret = dsi_error_handler(sender);
	if (ret) {
		DRM_ERROR("Error handling failed\n");
		return -EAGAIN;
	}

	/* send pkg */
	if (sender->status == MDFLD_DSI_PKG_SENDER_BUSY) {
		DRM_ERROR("sender is busy\n");
		return -EAGAIN;
	}

	ret = send_pkg_prepare(sender, data_type, data, len);
	if (ret) {
		DRM_ERROR("send_pkg_prepare error\n");
		return ret;
	}

	switch (data_type) {
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
	case MIPI_DSI_DCS_SHORT_WRITE:
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
	case MIPI_DSI_DCS_READ:
		ret = send_short_pkg(sender, data_type, data[0], data[1], hs);
		break;
	case MIPI_DSI_GENERIC_LONG_WRITE:
	case MIPI_DSI_DCS_LONG_WRITE:
		ret = send_long_pkg(sender, data_type, data, len, hs);
		break;
	}

	send_pkg_done(sender, data_type, data, len);

	/*FIXME: should I query complete and fifo empty here?*/

	return ret;
}

int mdfld_dsi_send_mcs_long(struct mdfld_dsi_pkg_sender *sender, u8 *data,
			u32 len, bool hs)
{
	unsigned long flags;

	if (!sender || !data || !len) {
		DRM_ERROR("Invalid parameters\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&sender->lock, flags);
	send_pkg(sender, MIPI_DSI_DCS_LONG_WRITE, data, len, hs);
	spin_unlock_irqrestore(&sender->lock, flags);

	return 0;
}

int mdfld_dsi_send_mcs_short(struct mdfld_dsi_pkg_sender *sender, u8 cmd,
			u8 param, u8 param_num, bool hs)
{
	u8 data[2];
	unsigned long flags;
	u8 data_type;

	if (!sender) {
		DRM_ERROR("Invalid parameter\n");
		return -EINVAL;
	}

	data[0] = cmd;

	if (param_num) {
		data_type = MIPI_DSI_DCS_SHORT_WRITE_PARAM;
		data[1] = param;
	} else {
		data_type = MIPI_DSI_DCS_SHORT_WRITE;
		data[1] = 0;
	}

	spin_lock_irqsave(&sender->lock, flags);
	send_pkg(sender, data_type, data, sizeof(data), hs);
	spin_unlock_irqrestore(&sender->lock, flags);

	return 0;
}

int mdfld_dsi_send_gen_short(struct mdfld_dsi_pkg_sender *sender, u8 param0,
			u8 param1, u8 param_num, bool hs)
{
	u8 data[2];
	unsigned long flags;
	u8 data_type;

	if (!sender || param_num > 2) {
		DRM_ERROR("Invalid parameter\n");
		return -EINVAL;
	}

	switch (param_num) {
	case 0:
		data_type = MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM;
		data[0] = 0;
		data[1] = 0;
		break;
	case 1:
		data_type = MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM;
		data[0] = param0;
		data[1] = 0;
		break;
	case 2:
		data_type = MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM;
		data[0] = param0;
		data[1] = param1;
		break;
	}

	spin_lock_irqsave(&sender->lock, flags);
	send_pkg(sender, data_type, data, sizeof(data), hs);
	spin_unlock_irqrestore(&sender->lock, flags);

	return 0;
}

int mdfld_dsi_send_gen_long(struct mdfld_dsi_pkg_sender *sender, u8 *data,
			u32 len, bool hs)
{
	unsigned long flags;

	if (!sender || !data || !len) {
		DRM_ERROR("Invalid parameters\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&sender->lock, flags);
	send_pkg(sender, MIPI_DSI_GENERIC_LONG_WRITE, data, len, hs);
	spin_unlock_irqrestore(&sender->lock, flags);

	return 0;
}

static int __read_panel_data(struct mdfld_dsi_pkg_sender *sender, u8 data_type,
			u8 *data, u16 len, u32 *data_out, u16 len_out, bool hs)
{
	unsigned long flags;
	struct drm_device *dev;
	int i;
	u32 gen_data_reg;
	int retry = MDFLD_DSI_READ_MAX_COUNT;

	if (!sender || !data_out || !len_out) {
		DRM_ERROR("Invalid parameters\n");
		return -EINVAL;
	}

	dev = sender->dev;

	/**
	 * do reading.
	 * 0) send out generic read request
	 * 1) polling read data avail interrupt
	 * 2) read data
	 */
	spin_lock_irqsave(&sender->lock, flags);

	REG_WRITE(sender->mipi_intr_stat_reg, BIT(29));

	if ((REG_READ(sender->mipi_intr_stat_reg) & BIT(29)))
		DRM_ERROR("Can NOT clean read data valid interrupt\n");

	/*send out read request*/
	send_pkg(sender, data_type, data, len, hs);

	/*polling read data avail interrupt*/
	while (retry && !(REG_READ(sender->mipi_intr_stat_reg) & BIT(29))) {
		udelay(100);
		retry--;
	}

	if (!retry) {
		spin_unlock_irqrestore(&sender->lock, flags);
		return -ETIMEDOUT;
	}

	REG_WRITE(sender->mipi_intr_stat_reg, BIT(29));

	/*read data*/
	if (hs)
		gen_data_reg = sender->mipi_hs_gen_data_reg;
	else
		gen_data_reg = sender->mipi_lp_gen_data_reg;

	for (i = 0; i < len_out; i++)
		*(data_out + i) = REG_READ(gen_data_reg);

	spin_unlock_irqrestore(&sender->lock, flags);

	return 0;
}

int mdfld_dsi_read_mcs(struct mdfld_dsi_pkg_sender *sender, u8 cmd,
		u32 *data, u16 len, bool hs)
{
	if (!sender || !data || !len) {
		DRM_ERROR("Invalid parameters\n");
		return -EINVAL;
	}

	return __read_panel_data(sender, MIPI_DSI_DCS_READ, &cmd, 1,
				data, len, hs);
}

int mdfld_dsi_pkg_sender_init(struct mdfld_dsi_connector *dsi_connector,
								int pipe)
{
	struct mdfld_dsi_pkg_sender *pkg_sender;
	struct mdfld_dsi_config *dsi_config =
				mdfld_dsi_get_config(dsi_connector);
	struct drm_device *dev = dsi_config->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	const struct psb_offset *map = &dev_priv->regmap[pipe];
	u32 mipi_val = 0;

	if (!dsi_connector) {
		DRM_ERROR("Invalid parameter\n");
		return -EINVAL;
	}

	pkg_sender = dsi_connector->pkg_sender;

	if (!pkg_sender || IS_ERR(pkg_sender)) {
		pkg_sender = kzalloc(sizeof(struct mdfld_dsi_pkg_sender),
								GFP_KERNEL);
		if (!pkg_sender) {
			DRM_ERROR("Create DSI pkg sender failed\n");
			return -ENOMEM;
		}
		dsi_connector->pkg_sender = (void *)pkg_sender;
	}

	pkg_sender->dev = dev;
	pkg_sender->dsi_connector = dsi_connector;
	pkg_sender->pipe = pipe;
	pkg_sender->pkg_num = 0;
	pkg_sender->panel_mode = 0;
	pkg_sender->status = MDFLD_DSI_PKG_SENDER_FREE;

	/*init regs*/
	/* FIXME: should just copy the regmap ptr ? */
	pkg_sender->dpll_reg = map->dpll;
	pkg_sender->dspcntr_reg = map->cntr;
	pkg_sender->pipeconf_reg = map->conf;
	pkg_sender->dsplinoff_reg = map->linoff;
	pkg_sender->dspsurf_reg = map->surf;
	pkg_sender->pipestat_reg = map->status;

	pkg_sender->mipi_intr_stat_reg = MIPI_INTR_STAT_REG(pipe);
	pkg_sender->mipi_lp_gen_data_reg = MIPI_LP_GEN_DATA_REG(pipe);
	pkg_sender->mipi_hs_gen_data_reg = MIPI_HS_GEN_DATA_REG(pipe);
	pkg_sender->mipi_lp_gen_ctrl_reg = MIPI_LP_GEN_CTRL_REG(pipe);
	pkg_sender->mipi_hs_gen_ctrl_reg = MIPI_HS_GEN_CTRL_REG(pipe);
	pkg_sender->mipi_gen_fifo_stat_reg = MIPI_GEN_FIFO_STAT_REG(pipe);
	pkg_sender->mipi_data_addr_reg = MIPI_DATA_ADD_REG(pipe);
	pkg_sender->mipi_data_len_reg = MIPI_DATA_LEN_REG(pipe);
	pkg_sender->mipi_cmd_addr_reg = MIPI_CMD_ADD_REG(pipe);
	pkg_sender->mipi_cmd_len_reg = MIPI_CMD_LEN_REG(pipe);

	/*init lock*/
	spin_lock_init(&pkg_sender->lock);

	if (mdfld_get_panel_type(dev, pipe) != TC35876X) {
		/**
		 * For video mode, don't enable DPI timing output here,
		 * will init the DPI timing output during mode setting.
		 */
		mipi_val = PASS_FROM_SPHY_TO_AFE | SEL_FLOPPED_HSTX;

		if (pipe == 0)
			mipi_val |= 0x2;

		REG_WRITE(MIPI_PORT_CONTROL(pipe), mipi_val);
		REG_READ(MIPI_PORT_CONTROL(pipe));

		/* do dsi controller init */
		mdfld_dsi_controller_init(dsi_config, pipe);
	}

	return 0;
}

void mdfld_dsi_pkg_sender_destroy(struct mdfld_dsi_pkg_sender *sender)
{
	if (!sender || IS_ERR(sender))
		return;

	/*free*/
	kfree(sender);
}


