// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2020, Analogix Semiconductor. All rights reserved.
 *
 */
#include <linux/gcd.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc_helper.h>
#include <drm/dp/drm_dp_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_hdcp.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include <media/v4l2-fwnode.h>
#include <sound/hdmi-codec.h>
#include <video/display_timing.h>

#include "anx7625.h"

/*
 * There is a sync issue while access I2C register between AP(CPU) and
 * internal firmware(OCM), to avoid the race condition, AP should access
 * the reserved slave address before slave address occurs changes.
 */
static int i2c_access_workaround(struct anx7625_data *ctx,
				 struct i2c_client *client)
{
	u8 offset;
	struct device *dev = &client->dev;
	int ret;

	if (client == ctx->last_client)
		return 0;

	ctx->last_client = client;

	if (client == ctx->i2c.tcpc_client)
		offset = RSVD_00_ADDR;
	else if (client == ctx->i2c.tx_p0_client)
		offset = RSVD_D1_ADDR;
	else if (client == ctx->i2c.tx_p1_client)
		offset = RSVD_60_ADDR;
	else if (client == ctx->i2c.rx_p0_client)
		offset = RSVD_39_ADDR;
	else if (client == ctx->i2c.rx_p1_client)
		offset = RSVD_7F_ADDR;
	else
		offset = RSVD_00_ADDR;

	ret = i2c_smbus_write_byte_data(client, offset, 0x00);
	if (ret < 0)
		DRM_DEV_ERROR(dev,
			      "fail to access i2c id=%x\n:%x",
			      client->addr, offset);

	return ret;
}

static int anx7625_reg_read(struct anx7625_data *ctx,
			    struct i2c_client *client, u8 reg_addr)
{
	int ret;
	struct device *dev = &client->dev;

	i2c_access_workaround(ctx, client);

	ret = i2c_smbus_read_byte_data(client, reg_addr);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "read i2c fail id=%x:%x\n",
			      client->addr, reg_addr);

	return ret;
}

static int anx7625_reg_block_read(struct anx7625_data *ctx,
				  struct i2c_client *client,
				  u8 reg_addr, u8 len, u8 *buf)
{
	int ret;
	struct device *dev = &client->dev;

	i2c_access_workaround(ctx, client);

	ret = i2c_smbus_read_i2c_block_data(client, reg_addr, len, buf);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "read i2c block fail id=%x:%x\n",
			      client->addr, reg_addr);

	return ret;
}

static int anx7625_reg_write(struct anx7625_data *ctx,
			     struct i2c_client *client,
			     u8 reg_addr, u8 reg_val)
{
	int ret;
	struct device *dev = &client->dev;

	i2c_access_workaround(ctx, client);

	ret = i2c_smbus_write_byte_data(client, reg_addr, reg_val);

	if (ret < 0)
		DRM_DEV_ERROR(dev, "fail to write i2c id=%x\n:%x",
			      client->addr, reg_addr);

	return ret;
}

static int anx7625_write_or(struct anx7625_data *ctx,
			    struct i2c_client *client,
			    u8 offset, u8 mask)
{
	int val;

	val = anx7625_reg_read(ctx, client, offset);
	if (val < 0)
		return val;

	return anx7625_reg_write(ctx, client, offset, (val | (mask)));
}

static int anx7625_write_and(struct anx7625_data *ctx,
			     struct i2c_client *client,
			     u8 offset, u8 mask)
{
	int val;

	val = anx7625_reg_read(ctx, client, offset);
	if (val < 0)
		return val;

	return anx7625_reg_write(ctx, client, offset, (val & (mask)));
}

static int anx7625_write_and_or(struct anx7625_data *ctx,
				struct i2c_client *client,
				u8 offset, u8 and_mask, u8 or_mask)
{
	int val;

	val = anx7625_reg_read(ctx, client, offset);
	if (val < 0)
		return val;

	return anx7625_reg_write(ctx, client,
				 offset, (val & and_mask) | (or_mask));
}

static int anx7625_config_bit_matrix(struct anx7625_data *ctx)
{
	int i, ret;

	ret = anx7625_write_or(ctx, ctx->i2c.tx_p2_client,
			       AUDIO_CONTROL_REGISTER, 0x80);
	for (i = 0; i < 13; i++)
		ret |= anx7625_reg_write(ctx, ctx->i2c.tx_p2_client,
					 VIDEO_BIT_MATRIX_12 + i,
					 0x18 + i);

	return ret;
}

static int anx7625_read_ctrl_status_p0(struct anx7625_data *ctx)
{
	return anx7625_reg_read(ctx, ctx->i2c.rx_p0_client, AP_AUX_CTRL_STATUS);
}

static int wait_aux_op_finish(struct anx7625_data *ctx)
{
	struct device *dev = &ctx->client->dev;
	int val;
	int ret;

	ret = readx_poll_timeout(anx7625_read_ctrl_status_p0,
				 ctx, val,
				 (!(val & AP_AUX_CTRL_OP_EN) || (val < 0)),
				 2000,
				 2000 * 150);
	if (ret) {
		DRM_DEV_ERROR(dev, "aux operation fail!\n");
		return -EIO;
	}

	val = anx7625_reg_read(ctx, ctx->i2c.rx_p0_client,
			       AP_AUX_CTRL_STATUS);
	if (val < 0 || (val & 0x0F)) {
		DRM_DEV_ERROR(dev, "aux status %02x\n", val);
		return -EIO;
	}

	return 0;
}

static int anx7625_aux_dpcd_read(struct anx7625_data *ctx,
				 u32 address, u8 len, u8 *buf)
{
	struct device *dev = &ctx->client->dev;
	int ret;
	u8 addrh, addrm, addrl;
	u8 cmd;

	if (len > MAX_DPCD_BUFFER_SIZE) {
		dev_err(dev, "exceed aux buffer len.\n");
		return -EINVAL;
	}

	addrl = address & 0xFF;
	addrm = (address >> 8) & 0xFF;
	addrh = (address >> 16) & 0xFF;

	cmd = DPCD_CMD(len, DPCD_READ);
	cmd = ((len - 1) << 4) | 0x09;

	/* Set command and length */
	ret = anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				AP_AUX_COMMAND, cmd);

	/* Set aux access address */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 AP_AUX_ADDR_7_0, addrl);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 AP_AUX_ADDR_15_8, addrm);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 AP_AUX_ADDR_19_16, addrh);

	/* Enable aux access */
	ret |= anx7625_write_or(ctx, ctx->i2c.rx_p0_client,
				AP_AUX_CTRL_STATUS, AP_AUX_CTRL_OP_EN);

	if (ret < 0) {
		dev_err(dev, "cannot access aux related register.\n");
		return -EIO;
	}

	usleep_range(2000, 2100);

	ret = wait_aux_op_finish(ctx);
	if (ret) {
		dev_err(dev, "aux IO error: wait aux op finish.\n");
		return ret;
	}

	ret = anx7625_reg_block_read(ctx, ctx->i2c.rx_p0_client,
				     AP_AUX_BUFF_START, len, buf);
	if (ret < 0) {
		dev_err(dev, "read dpcd register failed\n");
		return -EIO;
	}

	return 0;
}

static int anx7625_video_mute_control(struct anx7625_data *ctx,
				      u8 status)
{
	int ret;

	if (status) {
		/* Set mute on flag */
		ret = anx7625_write_or(ctx, ctx->i2c.rx_p0_client,
				       AP_AV_STATUS, AP_MIPI_MUTE);
		/* Clear mipi RX en */
		ret |= anx7625_write_and(ctx, ctx->i2c.rx_p0_client,
					 AP_AV_STATUS, (u8)~AP_MIPI_RX_EN);
	} else {
		/* Mute off flag */
		ret = anx7625_write_and(ctx, ctx->i2c.rx_p0_client,
					AP_AV_STATUS, (u8)~AP_MIPI_MUTE);
		/* Set MIPI RX EN */
		ret |= anx7625_write_or(ctx, ctx->i2c.rx_p0_client,
					AP_AV_STATUS, AP_MIPI_RX_EN);
	}

	return ret;
}

/* Reduction of fraction a/b */
static void anx7625_reduction_of_a_fraction(unsigned long *a, unsigned long *b)
{
	unsigned long gcd_num;
	unsigned long tmp_a, tmp_b;
	u32 i = 1;

	gcd_num = gcd(*a, *b);
	*a /= gcd_num;
	*b /= gcd_num;

	tmp_a = *a;
	tmp_b = *b;

	while ((*a > MAX_UNSIGNED_24BIT) || (*b > MAX_UNSIGNED_24BIT)) {
		i++;
		*a = tmp_a / i;
		*b = tmp_b / i;
	}

	/*
	 * In the end, make a, b larger to have higher ODFC PLL
	 * output frequency accuracy
	 */
	while ((*a < MAX_UNSIGNED_24BIT) && (*b < MAX_UNSIGNED_24BIT)) {
		*a <<= 1;
		*b <<= 1;
	}

	*a >>= 1;
	*b >>= 1;
}

static int anx7625_calculate_m_n(u32 pixelclock,
				 unsigned long *m,
				 unsigned long *n,
				 u8 *post_divider)
{
	if (pixelclock > PLL_OUT_FREQ_ABS_MAX / POST_DIVIDER_MIN) {
		/* Pixel clock frequency is too high */
		DRM_ERROR("pixelclock too high, act(%d), maximum(%lu)\n",
			  pixelclock,
			  PLL_OUT_FREQ_ABS_MAX / POST_DIVIDER_MIN);
		return -EINVAL;
	}

	if (pixelclock < PLL_OUT_FREQ_ABS_MIN / POST_DIVIDER_MAX) {
		/* Pixel clock frequency is too low */
		DRM_ERROR("pixelclock too low, act(%d), maximum(%lu)\n",
			  pixelclock,
			  PLL_OUT_FREQ_ABS_MIN / POST_DIVIDER_MAX);
		return -EINVAL;
	}

	for (*post_divider = 1;
		pixelclock < (PLL_OUT_FREQ_MIN / (*post_divider));)
		*post_divider += 1;

	if (*post_divider > POST_DIVIDER_MAX) {
		for (*post_divider = 1;
			(pixelclock <
			 (PLL_OUT_FREQ_ABS_MIN / (*post_divider)));)
			*post_divider += 1;

		if (*post_divider > POST_DIVIDER_MAX) {
			DRM_ERROR("cannot find property post_divider(%d)\n",
				  *post_divider);
			return -EDOM;
		}
	}

	/* Patch to improve the accuracy */
	if (*post_divider == 7) {
		/* 27,000,000 is not divisible by 7 */
		*post_divider = 8;
	} else if (*post_divider == 11) {
		/* 27,000,000 is not divisible by 11 */
		*post_divider = 12;
	} else if ((*post_divider == 13) || (*post_divider == 14)) {
		/* 27,000,000 is not divisible by 13 or 14 */
		*post_divider = 15;
	}

	if (pixelclock * (*post_divider) > PLL_OUT_FREQ_ABS_MAX) {
		DRM_ERROR("act clock(%u) large than maximum(%lu)\n",
			  pixelclock * (*post_divider),
			  PLL_OUT_FREQ_ABS_MAX);
		return -EDOM;
	}

	*m = pixelclock;
	*n = XTAL_FRQ / (*post_divider);

	anx7625_reduction_of_a_fraction(m, n);

	return 0;
}

static int anx7625_odfc_config(struct anx7625_data *ctx,
			       u8 post_divider)
{
	int ret;
	struct device *dev = &ctx->client->dev;

	/* Config input reference clock frequency 27MHz/19.2MHz */
	ret = anx7625_write_and(ctx, ctx->i2c.rx_p1_client, MIPI_DIGITAL_PLL_16,
				~(REF_CLK_27000KHZ << MIPI_FREF_D_IND));
	ret |= anx7625_write_or(ctx, ctx->i2c.rx_p1_client, MIPI_DIGITAL_PLL_16,
				(REF_CLK_27000KHZ << MIPI_FREF_D_IND));
	/* Post divider */
	ret |= anx7625_write_and(ctx, ctx->i2c.rx_p1_client,
				 MIPI_DIGITAL_PLL_8, 0x0f);
	ret |= anx7625_write_or(ctx, ctx->i2c.rx_p1_client, MIPI_DIGITAL_PLL_8,
				post_divider << 4);

	/* Add patch for MIS2-125 (5pcs ANX7625 fail ATE MBIST test) */
	ret |= anx7625_write_and(ctx, ctx->i2c.rx_p1_client, MIPI_DIGITAL_PLL_7,
				 ~MIPI_PLL_VCO_TUNE_REG_VAL);

	/* Reset ODFC PLL */
	ret |= anx7625_write_and(ctx, ctx->i2c.rx_p1_client, MIPI_DIGITAL_PLL_7,
				 ~MIPI_PLL_RESET_N);
	ret |= anx7625_write_or(ctx, ctx->i2c.rx_p1_client, MIPI_DIGITAL_PLL_7,
				MIPI_PLL_RESET_N);

	if (ret < 0)
		DRM_DEV_ERROR(dev, "IO error.\n");

	return ret;
}

/*
 * The MIPI source video data exist large variation (e.g. 59Hz ~ 61Hz),
 * anx7625 defined K ratio for matching MIPI input video clock and
 * DP output video clock. Increase K value can match bigger video data
 * variation. IVO panel has small variation than DP CTS spec, need
 * decrease the K value.
 */
static int anx7625_set_k_value(struct anx7625_data *ctx)
{
	struct edid *edid = (struct edid *)ctx->slimport_edid_p.edid_raw_data;

	if (edid->mfg_id[0] == IVO_MID0 && edid->mfg_id[1] == IVO_MID1)
		return anx7625_reg_write(ctx, ctx->i2c.rx_p1_client,
					 MIPI_DIGITAL_ADJ_1, 0x3B);

	return anx7625_reg_write(ctx, ctx->i2c.rx_p1_client,
				 MIPI_DIGITAL_ADJ_1, 0x3D);
}

static int anx7625_dsi_video_timing_config(struct anx7625_data *ctx)
{
	struct device *dev = &ctx->client->dev;
	unsigned long m, n;
	u16 htotal;
	int ret;
	u8 post_divider = 0;

	ret = anx7625_calculate_m_n(ctx->dt.pixelclock.min * 1000,
				    &m, &n, &post_divider);

	if (ret) {
		DRM_DEV_ERROR(dev, "cannot get property m n value.\n");
		return ret;
	}

	DRM_DEV_DEBUG_DRIVER(dev, "compute M(%lu), N(%lu), divider(%d).\n",
			     m, n, post_divider);

	/* Configure pixel clock */
	ret = anx7625_reg_write(ctx, ctx->i2c.rx_p0_client, PIXEL_CLOCK_L,
				(ctx->dt.pixelclock.min / 1000) & 0xFF);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client, PIXEL_CLOCK_H,
				 (ctx->dt.pixelclock.min / 1000) >> 8);
	/* Lane count */
	ret |= anx7625_write_and(ctx, ctx->i2c.rx_p1_client,
			MIPI_LANE_CTRL_0, 0xfc);
	ret |= anx7625_write_or(ctx, ctx->i2c.rx_p1_client,
				MIPI_LANE_CTRL_0, ctx->pdata.mipi_lanes - 1);

	/* Htotal */
	htotal = ctx->dt.hactive.min + ctx->dt.hfront_porch.min +
		ctx->dt.hback_porch.min + ctx->dt.hsync_len.min;
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p2_client,
			HORIZONTAL_TOTAL_PIXELS_L, htotal & 0xFF);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p2_client,
			HORIZONTAL_TOTAL_PIXELS_H, htotal >> 8);
	/* Hactive */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p2_client,
			HORIZONTAL_ACTIVE_PIXELS_L, ctx->dt.hactive.min & 0xFF);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p2_client,
			HORIZONTAL_ACTIVE_PIXELS_H, ctx->dt.hactive.min >> 8);
	/* HFP */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p2_client,
			HORIZONTAL_FRONT_PORCH_L, ctx->dt.hfront_porch.min);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p2_client,
			HORIZONTAL_FRONT_PORCH_H,
			ctx->dt.hfront_porch.min >> 8);
	/* HWS */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p2_client,
			HORIZONTAL_SYNC_WIDTH_L, ctx->dt.hsync_len.min);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p2_client,
			HORIZONTAL_SYNC_WIDTH_H, ctx->dt.hsync_len.min >> 8);
	/* HBP */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p2_client,
			HORIZONTAL_BACK_PORCH_L, ctx->dt.hback_porch.min);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p2_client,
			HORIZONTAL_BACK_PORCH_H, ctx->dt.hback_porch.min >> 8);
	/* Vactive */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p2_client, ACTIVE_LINES_L,
			ctx->dt.vactive.min);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p2_client, ACTIVE_LINES_H,
			ctx->dt.vactive.min >> 8);
	/* VFP */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p2_client,
			VERTICAL_FRONT_PORCH, ctx->dt.vfront_porch.min);
	/* VWS */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p2_client,
			VERTICAL_SYNC_WIDTH, ctx->dt.vsync_len.min);
	/* VBP */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p2_client,
			VERTICAL_BACK_PORCH, ctx->dt.vback_porch.min);
	/* M value */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p1_client,
			MIPI_PLL_M_NUM_23_16, (m >> 16) & 0xff);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p1_client,
			MIPI_PLL_M_NUM_15_8, (m >> 8) & 0xff);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p1_client,
			MIPI_PLL_M_NUM_7_0, (m & 0xff));
	/* N value */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p1_client,
			MIPI_PLL_N_NUM_23_16, (n >> 16) & 0xff);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p1_client,
			MIPI_PLL_N_NUM_15_8, (n >> 8) & 0xff);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p1_client, MIPI_PLL_N_NUM_7_0,
			(n & 0xff));

	anx7625_set_k_value(ctx);

	ret |= anx7625_odfc_config(ctx, post_divider - 1);

	if (ret < 0)
		DRM_DEV_ERROR(dev, "mipi dsi setup IO error.\n");

	return ret;
}

static int anx7625_swap_dsi_lane3(struct anx7625_data *ctx)
{
	int val;
	struct device *dev = &ctx->client->dev;

	/* Swap MIPI-DSI data lane 3 P and N */
	val = anx7625_reg_read(ctx, ctx->i2c.rx_p1_client, MIPI_SWAP);
	if (val < 0) {
		DRM_DEV_ERROR(dev, "IO error : access MIPI_SWAP.\n");
		return -EIO;
	}

	val |= (1 << MIPI_SWAP_CH3);
	return anx7625_reg_write(ctx, ctx->i2c.rx_p1_client, MIPI_SWAP, val);
}

static int anx7625_api_dsi_config(struct anx7625_data *ctx)

{
	int val, ret;
	struct device *dev = &ctx->client->dev;

	/* Swap MIPI-DSI data lane 3 P and N */
	ret = anx7625_swap_dsi_lane3(ctx);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "IO error : swap dsi lane 3 fail.\n");
		return ret;
	}

	/* DSI clock settings */
	val = (0 << MIPI_HS_PWD_CLK)		|
		(0 << MIPI_HS_RT_CLK)		|
		(0 << MIPI_PD_CLK)		|
		(1 << MIPI_CLK_RT_MANUAL_PD_EN)	|
		(1 << MIPI_CLK_HS_MANUAL_PD_EN)	|
		(0 << MIPI_CLK_DET_DET_BYPASS)	|
		(0 << MIPI_CLK_MISS_CTRL)	|
		(0 << MIPI_PD_LPTX_CH_MANUAL_PD_EN);
	ret = anx7625_reg_write(ctx, ctx->i2c.rx_p1_client,
				MIPI_PHY_CONTROL_3, val);

	/*
	 * Decreased HS prepare timing delay from 160ns to 80ns work with
	 *     a) Dragon board 810 series (Qualcomm AP)
	 *     b) Moving Pixel DSI source (PG3A pattern generator +
	 *	P332 D-PHY Probe) default D-PHY timing
	 *	5ns/step
	 */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p1_client,
				 MIPI_TIME_HS_PRPR, 0x10);

	/* Enable DSI mode*/
	ret |= anx7625_write_or(ctx, ctx->i2c.rx_p1_client, MIPI_DIGITAL_PLL_18,
				SELECT_DSI << MIPI_DPI_SELECT);

	ret |= anx7625_dsi_video_timing_config(ctx);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "dsi video timing config fail\n");
		return ret;
	}

	/* Toggle m, n ready */
	ret = anx7625_write_and(ctx, ctx->i2c.rx_p1_client, MIPI_DIGITAL_PLL_6,
				~(MIPI_M_NUM_READY | MIPI_N_NUM_READY));
	usleep_range(1000, 1100);
	ret |= anx7625_write_or(ctx, ctx->i2c.rx_p1_client, MIPI_DIGITAL_PLL_6,
				MIPI_M_NUM_READY | MIPI_N_NUM_READY);

	/* Configure integer stable register */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p1_client,
				 MIPI_VIDEO_STABLE_CNT, 0x02);
	/* Power on MIPI RX */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p1_client,
				 MIPI_LANE_CTRL_10, 0x00);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p1_client,
				 MIPI_LANE_CTRL_10, 0x80);

	if (ret < 0)
		DRM_DEV_ERROR(dev, "IO error : mipi dsi enable init fail.\n");

	return ret;
}

static int anx7625_dsi_config(struct anx7625_data *ctx)
{
	struct device *dev = &ctx->client->dev;
	int ret;

	DRM_DEV_DEBUG_DRIVER(dev, "config dsi.\n");

	/* DSC disable */
	ret = anx7625_write_and(ctx, ctx->i2c.rx_p0_client,
				R_DSC_CTRL_0, ~DSC_EN);

	ret |= anx7625_api_dsi_config(ctx);

	if (ret < 0) {
		DRM_DEV_ERROR(dev, "IO error : api dsi config error.\n");
		return ret;
	}

	/* Set MIPI RX EN */
	ret = anx7625_write_or(ctx, ctx->i2c.rx_p0_client,
			       AP_AV_STATUS, AP_MIPI_RX_EN);
	/* Clear mute flag */
	ret |= anx7625_write_and(ctx, ctx->i2c.rx_p0_client,
				 AP_AV_STATUS, (u8)~AP_MIPI_MUTE);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "IO error : enable mipi rx fail.\n");
	else
		DRM_DEV_DEBUG_DRIVER(dev, "success to config DSI\n");

	return ret;
}

static int anx7625_api_dpi_config(struct anx7625_data *ctx)
{
	struct device *dev = &ctx->client->dev;
	u16 freq = ctx->dt.pixelclock.min / 1000;
	int ret;

	/* configure pixel clock */
	ret = anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				PIXEL_CLOCK_L, freq & 0xFF);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 PIXEL_CLOCK_H, (freq >> 8));

	/* set DPI mode */
	/* set to DPI PLL module sel */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p1_client,
				 MIPI_DIGITAL_PLL_9, 0x20);
	/* power down MIPI */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p1_client,
				 MIPI_LANE_CTRL_10, 0x08);
	/* enable DPI mode */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p1_client,
				 MIPI_DIGITAL_PLL_18, 0x1C);
	/* set first edge */
	ret |= anx7625_reg_write(ctx, ctx->i2c.tx_p2_client,
				 VIDEO_CONTROL_0, 0x06);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "IO error : dpi phy set failed.\n");

	return ret;
}

static int anx7625_dpi_config(struct anx7625_data *ctx)
{
	struct device *dev = &ctx->client->dev;
	int ret;

	DRM_DEV_DEBUG_DRIVER(dev, "config dpi\n");

	/* DSC disable */
	ret = anx7625_write_and(ctx, ctx->i2c.rx_p0_client,
				R_DSC_CTRL_0, ~DSC_EN);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "IO error : disable dsc failed.\n");
		return ret;
	}

	ret = anx7625_config_bit_matrix(ctx);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "config bit matrix failed.\n");
		return ret;
	}

	ret = anx7625_api_dpi_config(ctx);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "mipi phy(dpi) setup failed.\n");
		return ret;
	}

	/* set MIPI RX EN */
	ret = anx7625_write_or(ctx, ctx->i2c.rx_p0_client,
			       AP_AV_STATUS, AP_MIPI_RX_EN);
	/* clear mute flag */
	ret |= anx7625_write_and(ctx, ctx->i2c.rx_p0_client,
				 AP_AV_STATUS, (u8)~AP_MIPI_MUTE);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "IO error : enable mipi rx failed.\n");

	return ret;
}

static int anx7625_read_flash_status(struct anx7625_data *ctx)
{
	return anx7625_reg_read(ctx, ctx->i2c.rx_p0_client, R_RAM_CTRL);
}

static int anx7625_hdcp_key_probe(struct anx7625_data *ctx)
{
	int ret, val;
	struct device *dev = &ctx->client->dev;
	u8 ident[FLASH_BUF_LEN];

	ret = anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				FLASH_ADDR_HIGH, 0x91);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 FLASH_ADDR_LOW, 0xA0);
	if (ret < 0) {
		dev_err(dev, "IO error : set key flash address.\n");
		return ret;
	}

	ret = anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				FLASH_LEN_HIGH, (FLASH_BUF_LEN - 1) >> 8);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 FLASH_LEN_LOW, (FLASH_BUF_LEN - 1) & 0xFF);
	if (ret < 0) {
		dev_err(dev, "IO error : set key flash len.\n");
		return ret;
	}

	ret = anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				R_FLASH_RW_CTRL, FLASH_READ);
	ret |= readx_poll_timeout(anx7625_read_flash_status,
				  ctx, val,
				  ((val & FLASH_DONE) || (val < 0)),
				  2000,
				  2000 * 150);
	if (ret) {
		dev_err(dev, "flash read access fail!\n");
		return -EIO;
	}

	ret = anx7625_reg_block_read(ctx, ctx->i2c.rx_p0_client,
				     FLASH_BUF_BASE_ADDR,
				     FLASH_BUF_LEN, ident);
	if (ret < 0) {
		dev_err(dev, "read flash data fail!\n");
		return -EIO;
	}

	if (ident[29] == 0xFF && ident[30] == 0xFF && ident[31] == 0xFF)
		return -EINVAL;

	return 0;
}

static int anx7625_hdcp_key_load(struct anx7625_data *ctx)
{
	int ret;
	struct device *dev = &ctx->client->dev;

	/* Select HDCP 1.4 KEY */
	ret = anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				R_BOOT_RETRY, 0x12);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 FLASH_ADDR_HIGH, HDCP14KEY_START_ADDR >> 8);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 FLASH_ADDR_LOW, HDCP14KEY_START_ADDR & 0xFF);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 R_RAM_LEN_H, HDCP14KEY_SIZE >> 12);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 R_RAM_LEN_L, HDCP14KEY_SIZE >> 4);

	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 R_RAM_ADDR_H, 0);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 R_RAM_ADDR_L, 0);
	/* Enable HDCP 1.4 KEY load */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 R_RAM_CTRL, DECRYPT_EN | LOAD_START);
	dev_dbg(dev, "load HDCP 1.4 key done\n");
	return ret;
}

static int anx7625_hdcp_disable(struct anx7625_data *ctx)
{
	int ret;
	struct device *dev = &ctx->client->dev;

	dev_dbg(dev, "disable HDCP 1.4\n");

	/* Disable HDCP */
	ret = anx7625_write_and(ctx, ctx->i2c.rx_p1_client, 0xee, 0x9f);
	/* Try auth flag */
	ret |= anx7625_write_or(ctx, ctx->i2c.rx_p1_client, 0xec, 0x10);
	/* Interrupt for DRM */
	ret |= anx7625_write_or(ctx, ctx->i2c.rx_p1_client, 0xff, 0x01);
	if (ret < 0)
		dev_err(dev, "fail to disable HDCP\n");

	return anx7625_write_and(ctx, ctx->i2c.tx_p0_client,
				 TX_HDCP_CTRL0, ~HARD_AUTH_EN & 0xFF);
}

static int anx7625_hdcp_enable(struct anx7625_data *ctx)
{
	u8 bcap;
	int ret;
	struct device *dev = &ctx->client->dev;

	ret = anx7625_hdcp_key_probe(ctx);
	if (ret) {
		dev_dbg(dev, "no key found, not to do hdcp\n");
		return ret;
	}

	/* Read downstream capability */
	anx7625_aux_dpcd_read(ctx, 0x68028, 1, &bcap);
	if (!(bcap & 0x01)) {
		pr_warn("downstream not support HDCP 1.4, cap(%x).\n", bcap);
		return 0;
	}

	dev_dbg(dev, "enable HDCP 1.4\n");

	/* First clear HDCP state */
	ret = anx7625_reg_write(ctx, ctx->i2c.tx_p0_client,
				TX_HDCP_CTRL0,
				KSVLIST_VLD | BKSV_SRM_PASS | RE_AUTHEN);
	usleep_range(1000, 1100);
	/* Second clear HDCP state */
	ret |= anx7625_reg_write(ctx, ctx->i2c.tx_p0_client,
				 TX_HDCP_CTRL0,
				 KSVLIST_VLD | BKSV_SRM_PASS | RE_AUTHEN);

	/* Set time for waiting KSVR */
	ret |= anx7625_reg_write(ctx, ctx->i2c.tx_p0_client,
				 SP_TX_WAIT_KSVR_TIME, 0xc8);
	/* Set time for waiting R0 */
	ret |= anx7625_reg_write(ctx, ctx->i2c.tx_p0_client,
				 SP_TX_WAIT_R0_TIME, 0xb0);
	ret |= anx7625_hdcp_key_load(ctx);
	if (ret) {
		pr_warn("prepare HDCP key failed.\n");
		return ret;
	}

	ret = anx7625_write_or(ctx, ctx->i2c.rx_p1_client, 0xee, 0x20);

	/* Try auth flag */
	ret |= anx7625_write_or(ctx, ctx->i2c.rx_p1_client, 0xec, 0x10);
	/* Interrupt for DRM */
	ret |= anx7625_write_or(ctx, ctx->i2c.rx_p1_client, 0xff, 0x01);
	if (ret < 0)
		dev_err(dev, "fail to enable HDCP\n");

	return anx7625_write_or(ctx, ctx->i2c.tx_p0_client,
				TX_HDCP_CTRL0, HARD_AUTH_EN);
}

static void anx7625_dp_start(struct anx7625_data *ctx)
{
	int ret;
	struct device *dev = &ctx->client->dev;

	if (!ctx->display_timing_valid) {
		DRM_DEV_ERROR(dev, "mipi not set display timing yet.\n");
		return;
	}

	/* Disable HDCP */
	anx7625_write_and(ctx, ctx->i2c.rx_p1_client, 0xee, 0x9f);

	if (ctx->pdata.is_dpi)
		ret = anx7625_dpi_config(ctx);
	else
		ret = anx7625_dsi_config(ctx);

	if (ret < 0)
		DRM_DEV_ERROR(dev, "MIPI phy setup error.\n");

	ctx->hdcp_cp = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;

	ctx->dp_en = 1;
}

static void anx7625_dp_stop(struct anx7625_data *ctx)
{
	struct device *dev = &ctx->client->dev;
	int ret;

	DRM_DEV_DEBUG_DRIVER(dev, "stop dp output\n");

	/*
	 * Video disable: 0x72:08 bit 7 = 0;
	 * Audio disable: 0x70:87 bit 0 = 0;
	 */
	ret = anx7625_write_and(ctx, ctx->i2c.tx_p0_client, 0x87, 0xfe);
	ret |= anx7625_write_and(ctx, ctx->i2c.tx_p2_client, 0x08, 0x7f);

	ret |= anx7625_video_mute_control(ctx, 1);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "IO error : mute video fail\n");

	ctx->hdcp_cp = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;

	ctx->dp_en = 0;
}

static int sp_tx_rst_aux(struct anx7625_data *ctx)
{
	int ret;

	ret = anx7625_write_or(ctx, ctx->i2c.tx_p2_client, RST_CTRL2,
			       AUX_RST);
	ret |= anx7625_write_and(ctx, ctx->i2c.tx_p2_client, RST_CTRL2,
				 ~AUX_RST);
	return ret;
}

static int sp_tx_aux_wr(struct anx7625_data *ctx, u8 offset)
{
	int ret;

	ret = anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				AP_AUX_BUFF_START, offset);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 AP_AUX_COMMAND, 0x04);
	ret |= anx7625_write_or(ctx, ctx->i2c.rx_p0_client,
				AP_AUX_CTRL_STATUS, AP_AUX_CTRL_OP_EN);
	return (ret | wait_aux_op_finish(ctx));
}

static int sp_tx_aux_rd(struct anx7625_data *ctx, u8 len_cmd)
{
	int ret;

	ret = anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				AP_AUX_COMMAND, len_cmd);
	ret |= anx7625_write_or(ctx, ctx->i2c.rx_p0_client,
				AP_AUX_CTRL_STATUS, AP_AUX_CTRL_OP_EN);
	return (ret | wait_aux_op_finish(ctx));
}

static int sp_tx_get_edid_block(struct anx7625_data *ctx)
{
	int c = 0;
	struct device *dev = &ctx->client->dev;

	sp_tx_aux_wr(ctx, 0x7e);
	sp_tx_aux_rd(ctx, 0x01);
	c = anx7625_reg_read(ctx, ctx->i2c.rx_p0_client, AP_AUX_BUFF_START);
	if (c < 0) {
		DRM_DEV_ERROR(dev, "IO error : access AUX BUFF.\n");
		return -EIO;
	}

	DRM_DEV_DEBUG_DRIVER(dev, " EDID Block = %d\n", c + 1);

	if (c > MAX_EDID_BLOCK)
		c = 1;

	return c;
}

static int edid_read(struct anx7625_data *ctx,
		     u8 offset, u8 *pblock_buf)
{
	int ret, cnt;
	struct device *dev = &ctx->client->dev;

	for (cnt = 0; cnt <= EDID_TRY_CNT; cnt++) {
		sp_tx_aux_wr(ctx, offset);
		/* Set I2C read com 0x01 mot = 0 and read 16 bytes */
		ret = sp_tx_aux_rd(ctx, 0xf1);

		if (ret) {
			ret = sp_tx_rst_aux(ctx);
			DRM_DEV_DEBUG_DRIVER(dev, "edid read fail, reset!\n");
		} else {
			ret = anx7625_reg_block_read(ctx, ctx->i2c.rx_p0_client,
						     AP_AUX_BUFF_START,
						     MAX_DPCD_BUFFER_SIZE,
						     pblock_buf);
			if (ret > 0)
				break;
		}
	}

	if (cnt > EDID_TRY_CNT)
		return -EIO;

	return ret;
}

static int segments_edid_read(struct anx7625_data *ctx,
			      u8 segment, u8 *buf, u8 offset)
{
	u8 cnt;
	int ret;
	struct device *dev = &ctx->client->dev;

	/* Write address only */
	ret = anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				AP_AUX_ADDR_7_0, 0x30);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 AP_AUX_COMMAND, 0x04);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 AP_AUX_CTRL_STATUS,
				 AP_AUX_CTRL_ADDRONLY | AP_AUX_CTRL_OP_EN);

	ret |= wait_aux_op_finish(ctx);
	/* Write segment address */
	ret |= sp_tx_aux_wr(ctx, segment);
	/* Data read */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 AP_AUX_ADDR_7_0, 0x50);
	if (ret) {
		DRM_DEV_ERROR(dev, "IO error : aux initial fail.\n");
		return ret;
	}

	for (cnt = 0; cnt <= EDID_TRY_CNT; cnt++) {
		sp_tx_aux_wr(ctx, offset);
		/* Set I2C read com 0x01 mot = 0 and read 16 bytes */
		ret = sp_tx_aux_rd(ctx, 0xf1);

		if (ret) {
			ret = sp_tx_rst_aux(ctx);
			DRM_DEV_ERROR(dev, "segment read fail, reset!\n");
		} else {
			ret = anx7625_reg_block_read(ctx, ctx->i2c.rx_p0_client,
						     AP_AUX_BUFF_START,
						     MAX_DPCD_BUFFER_SIZE, buf);
			if (ret > 0)
				break;
		}
	}

	if (cnt > EDID_TRY_CNT)
		return -EIO;

	return ret;
}

static int sp_tx_edid_read(struct anx7625_data *ctx,
			   u8 *pedid_blocks_buf)
{
	u8 offset, edid_pos;
	int count, blocks_num;
	u8 pblock_buf[MAX_DPCD_BUFFER_SIZE];
	u8 i, j;
	int g_edid_break = 0;
	int ret;
	struct device *dev = &ctx->client->dev;

	/* Address initial */
	ret = anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				AP_AUX_ADDR_7_0, 0x50);
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 AP_AUX_ADDR_15_8, 0);
	ret |= anx7625_write_and(ctx, ctx->i2c.rx_p0_client,
				 AP_AUX_ADDR_19_16, 0xf0);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "access aux channel IO error.\n");
		return -EIO;
	}

	blocks_num = sp_tx_get_edid_block(ctx);
	if (blocks_num < 0)
		return blocks_num;

	count = 0;

	do {
		switch (count) {
		case 0:
		case 1:
			for (i = 0; i < 8; i++) {
				offset = (i + count * 8) * MAX_DPCD_BUFFER_SIZE;
				g_edid_break = edid_read(ctx, offset,
							 pblock_buf);

				if (g_edid_break < 0)
					break;

				memcpy(&pedid_blocks_buf[offset],
				       pblock_buf,
				       MAX_DPCD_BUFFER_SIZE);
			}

			break;
		case 2:
			offset = 0x00;

			for (j = 0; j < 8; j++) {
				edid_pos = (j + count * 8) *
					MAX_DPCD_BUFFER_SIZE;

				if (g_edid_break == 1)
					break;

				ret = segments_edid_read(ctx, count / 2,
							 pblock_buf, offset);
				if (ret < 0)
					return ret;

				memcpy(&pedid_blocks_buf[edid_pos],
				       pblock_buf,
				       MAX_DPCD_BUFFER_SIZE);
				offset = offset + 0x10;
			}

			break;
		case 3:
			offset = 0x80;

			for (j = 0; j < 8; j++) {
				edid_pos = (j + count * 8) *
					MAX_DPCD_BUFFER_SIZE;
				if (g_edid_break == 1)
					break;

				ret = segments_edid_read(ctx, count / 2,
							 pblock_buf, offset);
				if (ret < 0)
					return ret;

				memcpy(&pedid_blocks_buf[edid_pos],
				       pblock_buf,
				       MAX_DPCD_BUFFER_SIZE);
				offset = offset + 0x10;
			}

			break;
		default:
			break;
		}

		count++;

	} while (blocks_num >= count);

	/* Check edid data */
	if (!drm_edid_is_valid((struct edid *)pedid_blocks_buf)) {
		DRM_DEV_ERROR(dev, "WARNING! edid check fail!\n");
		return -EINVAL;
	}

	/* Reset aux channel */
	ret = sp_tx_rst_aux(ctx);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to reset aux channel!\n");
		return ret;
	}

	return (blocks_num + 1);
}

static void anx7625_power_on(struct anx7625_data *ctx)
{
	struct device *dev = &ctx->client->dev;
	int ret, i;

	if (!ctx->pdata.low_power_mode) {
		DRM_DEV_DEBUG_DRIVER(dev, "not low power mode!\n");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(ctx->pdata.supplies); i++) {
		ret = regulator_enable(ctx->pdata.supplies[i].consumer);
		if (ret < 0) {
			DRM_DEV_DEBUG_DRIVER(dev, "cannot enable supply %d: %d\n",
					     i, ret);
			goto reg_err;
		}
		usleep_range(2000, 2100);
	}

	usleep_range(11000, 12000);

	/* Power on pin enable */
	gpiod_set_value(ctx->pdata.gpio_p_on, 1);
	usleep_range(10000, 11000);
	/* Power reset pin enable */
	gpiod_set_value(ctx->pdata.gpio_reset, 1);
	usleep_range(10000, 11000);

	DRM_DEV_DEBUG_DRIVER(dev, "power on !\n");
	return;
reg_err:
	for (--i; i >= 0; i--)
		regulator_disable(ctx->pdata.supplies[i].consumer);
}

static void anx7625_power_standby(struct anx7625_data *ctx)
{
	struct device *dev = &ctx->client->dev;
	int ret;

	if (!ctx->pdata.low_power_mode) {
		DRM_DEV_DEBUG_DRIVER(dev, "not low power mode!\n");
		return;
	}

	gpiod_set_value(ctx->pdata.gpio_reset, 0);
	usleep_range(1000, 1100);
	gpiod_set_value(ctx->pdata.gpio_p_on, 0);
	usleep_range(1000, 1100);

	ret = regulator_bulk_disable(ARRAY_SIZE(ctx->pdata.supplies),
				     ctx->pdata.supplies);
	if (ret < 0)
		DRM_DEV_DEBUG_DRIVER(dev, "cannot disable supplies %d\n", ret);

	DRM_DEV_DEBUG_DRIVER(dev, "power down\n");
}

/* Basic configurations of ANX7625 */
static void anx7625_config(struct anx7625_data *ctx)
{
	anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
			  XTAL_FRQ_SEL, XTAL_FRQ_27M);
}

static void anx7625_disable_pd_protocol(struct anx7625_data *ctx)
{
	struct device *dev = &ctx->client->dev;
	int ret;

	/* Reset main ocm */
	ret = anx7625_reg_write(ctx, ctx->i2c.rx_p0_client, 0x88, 0x40);
	/* Disable PD */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				 AP_AV_STATUS, AP_DISABLE_PD);
	/* Release main ocm */
	ret |= anx7625_reg_write(ctx, ctx->i2c.rx_p0_client, 0x88, 0x00);

	if (ret < 0)
		DRM_DEV_DEBUG_DRIVER(dev, "disable PD feature fail.\n");
	else
		DRM_DEV_DEBUG_DRIVER(dev, "disable PD feature succeeded.\n");
}

static int anx7625_ocm_loading_check(struct anx7625_data *ctx)
{
	int ret;
	struct device *dev = &ctx->client->dev;

	/* Check interface workable */
	ret = anx7625_reg_read(ctx, ctx->i2c.rx_p0_client,
			       FLASH_LOAD_STA);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "IO error : access flash load.\n");
		return ret;
	}
	if ((ret & FLASH_LOAD_STA_CHK) != FLASH_LOAD_STA_CHK)
		return -ENODEV;

	anx7625_disable_pd_protocol(ctx);

	DRM_DEV_DEBUG_DRIVER(dev, "Firmware ver %02x%02x,",
			     anx7625_reg_read(ctx,
					      ctx->i2c.rx_p0_client,
					      OCM_FW_VERSION),
			     anx7625_reg_read(ctx,
					      ctx->i2c.rx_p0_client,
					      OCM_FW_REVERSION));
	DRM_DEV_DEBUG_DRIVER(dev, "Driver version %s\n",
			     ANX7625_DRV_VERSION);

	return 0;
}

static void anx7625_power_on_init(struct anx7625_data *ctx)
{
	int retry_count, i;

	for (retry_count = 0; retry_count < 3; retry_count++) {
		anx7625_power_on(ctx);
		anx7625_config(ctx);

		for (i = 0; i < OCM_LOADING_TIME; i++) {
			if (!anx7625_ocm_loading_check(ctx))
				return;
			usleep_range(1000, 1100);
		}
		anx7625_power_standby(ctx);
	}
}

static void anx7625_init_gpio(struct anx7625_data *platform)
{
	struct device *dev = &platform->client->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "init gpio\n");

	/* Gpio for chip power enable */
	platform->pdata.gpio_p_on =
		devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(platform->pdata.gpio_p_on)) {
		DRM_DEV_DEBUG_DRIVER(dev, "no enable gpio found\n");
		platform->pdata.gpio_p_on = NULL;
	}

	/* Gpio for chip reset */
	platform->pdata.gpio_reset =
		devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(platform->pdata.gpio_reset)) {
		DRM_DEV_DEBUG_DRIVER(dev, "no reset gpio found\n");
		platform->pdata.gpio_reset = NULL;
	}

	if (platform->pdata.gpio_p_on && platform->pdata.gpio_reset) {
		platform->pdata.low_power_mode = 1;
		DRM_DEV_DEBUG_DRIVER(dev, "low power mode, pon %d, reset %d.\n",
				     desc_to_gpio(platform->pdata.gpio_p_on),
				     desc_to_gpio(platform->pdata.gpio_reset));
	} else {
		platform->pdata.low_power_mode = 0;
		DRM_DEV_DEBUG_DRIVER(dev, "not low power mode.\n");
	}
}

static void anx7625_stop_dp_work(struct anx7625_data *ctx)
{
	ctx->hpd_status = 0;
	ctx->hpd_high_cnt = 0;
	ctx->display_timing_valid = 0;
}

static void anx7625_start_dp_work(struct anx7625_data *ctx)
{
	int ret;
	struct device *dev = &ctx->client->dev;

	if (ctx->hpd_high_cnt >= 2) {
		DRM_DEV_DEBUG_DRIVER(dev, "filter useless HPD\n");
		return;
	}

	ctx->hpd_status = 1;
	ctx->hpd_high_cnt++;

	/* Not support HDCP */
	ret = anx7625_write_and(ctx, ctx->i2c.rx_p1_client, 0xee, 0x9f);

	/* Try auth flag */
	ret |= anx7625_write_or(ctx, ctx->i2c.rx_p1_client, 0xec, 0x10);
	/* Interrupt for DRM */
	ret |= anx7625_write_or(ctx, ctx->i2c.rx_p1_client, 0xff, 0x01);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "fail to setting HDCP/auth\n");
		return;
	}

	ret = anx7625_reg_read(ctx, ctx->i2c.rx_p1_client, 0x86);
	if (ret < 0)
		return;

	DRM_DEV_DEBUG_DRIVER(dev, "Secure OCM version=%02x\n", ret);
}

static int anx7625_read_hpd_status_p0(struct anx7625_data *ctx)
{
	return anx7625_reg_read(ctx, ctx->i2c.rx_p0_client, SYSTEM_STSTUS);
}

static void anx7625_hpd_polling(struct anx7625_data *ctx)
{
	int ret, val;
	struct device *dev = &ctx->client->dev;

	/* Interrupt mode, no need poll HPD status, just return */
	if (ctx->pdata.intp_irq)
		return;

	ret = readx_poll_timeout(anx7625_read_hpd_status_p0,
				 ctx, val,
				 ((val & HPD_STATUS) || (val < 0)),
				 5000,
				 5000 * 100);
	if (ret) {
		DRM_DEV_ERROR(dev, "no hpd.\n");
		return;
	}

	DRM_DEV_DEBUG_DRIVER(dev, "system status: 0x%x. HPD raise up.\n", val);
	anx7625_reg_write(ctx, ctx->i2c.tcpc_client,
			  INTR_ALERT_1, 0xFF);
	anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
			  INTERFACE_CHANGE_INT, 0);

	anx7625_start_dp_work(ctx);

	if (!ctx->pdata.panel_bridge && ctx->bridge_attached)
		drm_helper_hpd_irq_event(ctx->bridge.dev);
}

static void anx7625_remove_edid(struct anx7625_data *ctx)
{
	ctx->slimport_edid_p.edid_block_num = -1;
}

static void anx7625_dp_adjust_swing(struct anx7625_data *ctx)
{
	int i;

	for (i = 0; i < ctx->pdata.dp_lane0_swing_reg_cnt; i++)
		anx7625_reg_write(ctx, ctx->i2c.tx_p1_client,
				  DP_TX_LANE0_SWING_REG0 + i,
				  ctx->pdata.lane0_reg_data[i] & 0xFF);

	for (i = 0; i < ctx->pdata.dp_lane1_swing_reg_cnt; i++)
		anx7625_reg_write(ctx, ctx->i2c.tx_p1_client,
				  DP_TX_LANE1_SWING_REG0 + i,
				  ctx->pdata.lane1_reg_data[i] & 0xFF);
}

static void dp_hpd_change_handler(struct anx7625_data *ctx, bool on)
{
	struct device *dev = &ctx->client->dev;

	/* HPD changed */
	DRM_DEV_DEBUG_DRIVER(dev, "dp_hpd_change_default_func: %d\n",
			     (u32)on);

	if (on == 0) {
		DRM_DEV_DEBUG_DRIVER(dev, " HPD low\n");
		anx7625_remove_edid(ctx);
		anx7625_stop_dp_work(ctx);
	} else {
		DRM_DEV_DEBUG_DRIVER(dev, " HPD high\n");
		anx7625_start_dp_work(ctx);
		anx7625_dp_adjust_swing(ctx);
	}
}

static int anx7625_hpd_change_detect(struct anx7625_data *ctx)
{
	int intr_vector, status;
	struct device *dev = &ctx->client->dev;

	status = anx7625_reg_write(ctx, ctx->i2c.tcpc_client,
				   INTR_ALERT_1, 0xFF);
	if (status < 0) {
		DRM_DEV_ERROR(dev, "cannot clear alert reg.\n");
		return status;
	}

	intr_vector = anx7625_reg_read(ctx, ctx->i2c.rx_p0_client,
				       INTERFACE_CHANGE_INT);
	if (intr_vector < 0) {
		DRM_DEV_ERROR(dev, "cannot access interrupt change reg.\n");
		return intr_vector;
	}
	DRM_DEV_DEBUG_DRIVER(dev, "0x7e:0x44=%x\n", intr_vector);
	status = anx7625_reg_write(ctx, ctx->i2c.rx_p0_client,
				   INTERFACE_CHANGE_INT,
				   intr_vector & (~intr_vector));
	if (status < 0) {
		DRM_DEV_ERROR(dev, "cannot clear interrupt change reg.\n");
		return status;
	}

	if (!(intr_vector & HPD_STATUS_CHANGE))
		return -ENOENT;

	status = anx7625_reg_read(ctx, ctx->i2c.rx_p0_client,
				  SYSTEM_STSTUS);
	if (status < 0) {
		DRM_DEV_ERROR(dev, "cannot clear interrupt status.\n");
		return status;
	}

	DRM_DEV_DEBUG_DRIVER(dev, "0x7e:0x45=%x\n", status);
	dp_hpd_change_handler(ctx, status & HPD_STATUS);

	return 0;
}

static void anx7625_work_func(struct work_struct *work)
{
	int event;
	struct anx7625_data *ctx = container_of(work,
						struct anx7625_data, work);

	mutex_lock(&ctx->lock);

	if (pm_runtime_suspended(&ctx->client->dev))
		goto unlock;

	event = anx7625_hpd_change_detect(ctx);
	if (event < 0)
		goto unlock;

	if (ctx->bridge_attached)
		drm_helper_hpd_irq_event(ctx->bridge.dev);

unlock:
	mutex_unlock(&ctx->lock);
}

static irqreturn_t anx7625_intr_hpd_isr(int irq, void *data)
{
	struct anx7625_data *ctx = (struct anx7625_data *)data;

	queue_work(ctx->workqueue, &ctx->work);

	return IRQ_HANDLED;
}

static int anx7625_get_swing_setting(struct device *dev,
				     struct anx7625_platform_data *pdata)
{
	int num_regs;

	if (of_get_property(dev->of_node,
			    "analogix,lane0-swing", &num_regs)) {
		if (num_regs > DP_TX_SWING_REG_CNT)
			num_regs = DP_TX_SWING_REG_CNT;

		pdata->dp_lane0_swing_reg_cnt = num_regs;
		of_property_read_u32_array(dev->of_node, "analogix,lane0-swing",
					   pdata->lane0_reg_data, num_regs);
	}

	if (of_get_property(dev->of_node,
			    "analogix,lane1-swing", &num_regs)) {
		if (num_regs > DP_TX_SWING_REG_CNT)
			num_regs = DP_TX_SWING_REG_CNT;

		pdata->dp_lane1_swing_reg_cnt = num_regs;
		of_property_read_u32_array(dev->of_node, "analogix,lane1-swing",
					   pdata->lane1_reg_data, num_regs);
	}

	return 0;
}

static int anx7625_parse_dt(struct device *dev,
			    struct anx7625_platform_data *pdata)
{
	struct device_node *np = dev->of_node, *ep0;
	struct drm_panel *panel;
	int ret;
	int bus_type, mipi_lanes;

	anx7625_get_swing_setting(dev, pdata);

	pdata->is_dpi = 1; /* default dpi mode */
	pdata->mipi_host_node = of_graph_get_remote_node(np, 0, 0);
	if (!pdata->mipi_host_node) {
		DRM_DEV_ERROR(dev, "fail to get internal panel.\n");
		return -ENODEV;
	}

	bus_type = V4L2_FWNODE_BUS_TYPE_PARALLEL;
	mipi_lanes = MAX_LANES_SUPPORT;
	ep0 = of_graph_get_endpoint_by_regs(np, 0, 0);
	if (ep0) {
		if (of_property_read_u32(ep0, "bus-type", &bus_type))
			bus_type = 0;

		mipi_lanes = of_property_count_u32_elems(ep0, "data-lanes");
	}

	if (bus_type == V4L2_FWNODE_BUS_TYPE_PARALLEL) /* bus type is Parallel(DSI) */
		pdata->is_dpi = 0;

	pdata->mipi_lanes = mipi_lanes;
	if (pdata->mipi_lanes > MAX_LANES_SUPPORT || pdata->mipi_lanes <= 0)
		pdata->mipi_lanes = MAX_LANES_SUPPORT;

	if (pdata->is_dpi)
		DRM_DEV_DEBUG_DRIVER(dev, "found MIPI DPI host node.\n");
	else
		DRM_DEV_DEBUG_DRIVER(dev, "found MIPI DSI host node.\n");

	if (of_property_read_bool(np, "analogix,audio-enable"))
		pdata->audio_en = 1;

	ret = drm_of_find_panel_or_bridge(np, 1, 0, &panel, NULL);
	if (ret < 0) {
		if (ret == -ENODEV)
			return 0;
		return ret;
	}
	if (!panel)
		return -ENODEV;

	pdata->panel_bridge = devm_drm_panel_bridge_add(dev, panel);
	if (IS_ERR(pdata->panel_bridge))
		return PTR_ERR(pdata->panel_bridge);
	DRM_DEV_DEBUG_DRIVER(dev, "get panel node.\n");

	return 0;
}

static inline struct anx7625_data *bridge_to_anx7625(struct drm_bridge *bridge)
{
	return container_of(bridge, struct anx7625_data, bridge);
}

static struct edid *anx7625_get_edid(struct anx7625_data *ctx)
{
	struct device *dev = &ctx->client->dev;
	struct s_edid_data *p_edid = &ctx->slimport_edid_p;
	int edid_num;
	u8 *edid;

	edid = kmalloc(FOUR_BLOCK_SIZE, GFP_KERNEL);
	if (!edid) {
		DRM_DEV_ERROR(dev, "Fail to allocate buffer\n");
		return NULL;
	}

	if (ctx->slimport_edid_p.edid_block_num > 0) {
		memcpy(edid, ctx->slimport_edid_p.edid_raw_data,
		       FOUR_BLOCK_SIZE);
		return (struct edid *)edid;
	}

	pm_runtime_get_sync(dev);
	edid_num = sp_tx_edid_read(ctx, p_edid->edid_raw_data);
	pm_runtime_put_sync(dev);

	if (edid_num < 1) {
		DRM_DEV_ERROR(dev, "Fail to read EDID: %d\n", edid_num);
		kfree(edid);
		return NULL;
	}

	p_edid->edid_block_num = edid_num;

	memcpy(edid, ctx->slimport_edid_p.edid_raw_data, FOUR_BLOCK_SIZE);
	return (struct edid *)edid;
}

static enum drm_connector_status anx7625_sink_detect(struct anx7625_data *ctx)
{
	struct device *dev = &ctx->client->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "sink detect\n");

	if (ctx->pdata.panel_bridge)
		return connector_status_connected;

	return ctx->hpd_status ? connector_status_connected :
				     connector_status_disconnected;
}

static int anx7625_audio_hw_params(struct device *dev, void *data,
				   struct hdmi_codec_daifmt *fmt,
				   struct hdmi_codec_params *params)
{
	struct anx7625_data *ctx = dev_get_drvdata(dev);
	int wl, ch, rate;
	int ret = 0;

	if (fmt->fmt != HDMI_DSP_A) {
		DRM_DEV_ERROR(dev, "only supports DSP_A\n");
		return -EINVAL;
	}

	DRM_DEV_DEBUG_DRIVER(dev, "setting %d Hz, %d bit, %d channels\n",
			     params->sample_rate, params->sample_width,
			     params->cea.channels);

	ret |= anx7625_write_and_or(ctx, ctx->i2c.tx_p2_client,
				    AUDIO_CHANNEL_STATUS_6,
				    ~I2S_SLAVE_MODE,
				    TDM_SLAVE_MODE);

	/* Word length */
	switch (params->sample_width) {
	case 16:
		wl = AUDIO_W_LEN_16_20MAX;
		break;
	case 18:
		wl = AUDIO_W_LEN_18_20MAX;
		break;
	case 20:
		wl = AUDIO_W_LEN_20_20MAX;
		break;
	case 24:
		wl = AUDIO_W_LEN_24_24MAX;
		break;
	default:
		DRM_DEV_DEBUG_DRIVER(dev, "wordlength: %d bit not support",
				     params->sample_width);
		return -EINVAL;
	}
	ret |= anx7625_write_and_or(ctx, ctx->i2c.tx_p2_client,
				    AUDIO_CHANNEL_STATUS_5,
				    0xf0, wl);

	/* Channel num */
	switch (params->cea.channels) {
	case 2:
		ch = I2S_CH_2;
		break;
	case 4:
		ch = TDM_CH_4;
		break;
	case 6:
		ch = TDM_CH_6;
		break;
	case 8:
		ch = TDM_CH_8;
		break;
	default:
		DRM_DEV_DEBUG_DRIVER(dev, "channel number: %d not support",
				     params->cea.channels);
		return -EINVAL;
	}
	ret |= anx7625_write_and_or(ctx, ctx->i2c.tx_p2_client,
			       AUDIO_CHANNEL_STATUS_6, 0x1f, ch << 5);
	if (ch > I2S_CH_2)
		ret |= anx7625_write_or(ctx, ctx->i2c.tx_p2_client,
				AUDIO_CHANNEL_STATUS_6, AUDIO_LAYOUT);
	else
		ret |= anx7625_write_and(ctx, ctx->i2c.tx_p2_client,
				AUDIO_CHANNEL_STATUS_6, ~AUDIO_LAYOUT);

	/* FS */
	switch (params->sample_rate) {
	case 32000:
		rate = AUDIO_FS_32K;
		break;
	case 44100:
		rate = AUDIO_FS_441K;
		break;
	case 48000:
		rate = AUDIO_FS_48K;
		break;
	case 88200:
		rate = AUDIO_FS_882K;
		break;
	case 96000:
		rate = AUDIO_FS_96K;
		break;
	case 176400:
		rate = AUDIO_FS_1764K;
		break;
	case 192000:
		rate = AUDIO_FS_192K;
		break;
	default:
		DRM_DEV_DEBUG_DRIVER(dev, "sample rate: %d not support",
				     params->sample_rate);
		return -EINVAL;
	}
	ret |= anx7625_write_and_or(ctx, ctx->i2c.tx_p2_client,
				    AUDIO_CHANNEL_STATUS_4,
				    0xf0, rate);
	ret |= anx7625_write_or(ctx, ctx->i2c.rx_p0_client,
				AP_AV_STATUS, AP_AUDIO_CHG);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "IO error : config audio.\n");
		return -EIO;
	}

	return 0;
}

static void anx7625_audio_shutdown(struct device *dev, void *data)
{
	DRM_DEV_DEBUG_DRIVER(dev, "stop audio\n");
}

static int anx7625_hdmi_i2s_get_dai_id(struct snd_soc_component *component,
				       struct device_node *endpoint)
{
	struct of_endpoint of_ep;
	int ret;

	ret = of_graph_parse_endpoint(endpoint, &of_ep);
	if (ret < 0)
		return ret;

	/*
	 * HDMI sound should be located at external DPI port
	 * Didn't have good way to check where is internal(DSI)
	 * or external(DPI) bridge
	 */
	return 0;
}

static void
anx7625_audio_update_connector_status(struct anx7625_data *ctx,
				      enum drm_connector_status status)
{
	if (ctx->plugged_cb && ctx->codec_dev) {
		ctx->plugged_cb(ctx->codec_dev,
				status == connector_status_connected);
	}
}

static int anx7625_audio_hook_plugged_cb(struct device *dev, void *data,
					 hdmi_codec_plugged_cb fn,
					 struct device *codec_dev)
{
	struct anx7625_data *ctx = data;

	ctx->plugged_cb = fn;
	ctx->codec_dev = codec_dev;
	anx7625_audio_update_connector_status(ctx, anx7625_sink_detect(ctx));

	return 0;
}

static int anx7625_audio_get_eld(struct device *dev, void *data,
				 u8 *buf, size_t len)
{
	struct anx7625_data *ctx = dev_get_drvdata(dev);

	if (!ctx->connector) {
		dev_err(dev, "connector not initial\n");
		return -EINVAL;
	}

	dev_dbg(dev, "audio copy eld\n");
	memcpy(buf, ctx->connector->eld,
	       min(sizeof(ctx->connector->eld), len));

	return 0;
}

static const struct hdmi_codec_ops anx7625_codec_ops = {
	.hw_params	= anx7625_audio_hw_params,
	.audio_shutdown = anx7625_audio_shutdown,
	.get_eld	= anx7625_audio_get_eld,
	.get_dai_id	= anx7625_hdmi_i2s_get_dai_id,
	.hook_plugged_cb = anx7625_audio_hook_plugged_cb,
};

static void anx7625_unregister_audio(struct anx7625_data *ctx)
{
	struct device *dev = &ctx->client->dev;

	if (ctx->audio_pdev) {
		platform_device_unregister(ctx->audio_pdev);
		ctx->audio_pdev = NULL;
	}

	DRM_DEV_DEBUG_DRIVER(dev, "unbound to %s", HDMI_CODEC_DRV_NAME);
}

static int anx7625_register_audio(struct device *dev, struct anx7625_data *ctx)
{
	struct hdmi_codec_pdata codec_data = {
		.ops = &anx7625_codec_ops,
		.max_i2s_channels = 8,
		.i2s = 1,
		.data = ctx,
	};

	ctx->audio_pdev = platform_device_register_data(dev,
							HDMI_CODEC_DRV_NAME,
							PLATFORM_DEVID_AUTO,
							&codec_data,
							sizeof(codec_data));

	if (IS_ERR(ctx->audio_pdev))
		return PTR_ERR(ctx->audio_pdev);

	DRM_DEV_DEBUG_DRIVER(dev, "bound to %s", HDMI_CODEC_DRV_NAME);

	return 0;
}

static int anx7625_attach_dsi(struct anx7625_data *ctx)
{
	struct mipi_dsi_device *dsi;
	struct device *dev = &ctx->client->dev;
	struct mipi_dsi_host *host;
	const struct mipi_dsi_device_info info = {
		.type = "anx7625",
		.channel = 0,
		.node = NULL,
	};
	int ret;

	DRM_DEV_DEBUG_DRIVER(dev, "attach dsi\n");

	host = of_find_mipi_dsi_host_by_node(ctx->pdata.mipi_host_node);
	if (!host) {
		DRM_DEV_ERROR(dev, "fail to find dsi host.\n");
		return -EPROBE_DEFER;
	}

	dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
	if (IS_ERR(dsi)) {
		DRM_DEV_ERROR(dev, "fail to create dsi device.\n");
		return -EINVAL;
	}

	dsi->lanes = ctx->pdata.mipi_lanes;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO	|
		MIPI_DSI_MODE_VIDEO_SYNC_PULSE	|
		MIPI_DSI_MODE_VIDEO_HSE;

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret) {
		DRM_DEV_ERROR(dev, "fail to attach dsi to host.\n");
		return ret;
	}

	ctx->dsi = dsi;

	DRM_DEV_DEBUG_DRIVER(dev, "attach dsi succeeded.\n");

	return 0;
}

static void hdcp_check_work_func(struct work_struct *work)
{
	u8 status;
	struct delayed_work *dwork;
	struct anx7625_data *ctx;
	struct device *dev;
	struct drm_device *drm_dev;

	dwork = to_delayed_work(work);
	ctx = container_of(dwork, struct anx7625_data, hdcp_work);
	dev = &ctx->client->dev;

	if (!ctx->connector) {
		dev_err(dev, "HDCP connector is null!");
		return;
	}

	drm_dev = ctx->connector->dev;
	drm_modeset_lock(&drm_dev->mode_config.connection_mutex, NULL);
	mutex_lock(&ctx->hdcp_wq_lock);

	status = anx7625_reg_read(ctx, ctx->i2c.tx_p0_client, 0);
	dev_dbg(dev, "sink HDCP status check: %.02x\n", status);
	if (status & BIT(1)) {
		ctx->hdcp_cp = DRM_MODE_CONTENT_PROTECTION_ENABLED;
		drm_hdcp_update_content_protection(ctx->connector,
						   ctx->hdcp_cp);
		dev_dbg(dev, "update CP to ENABLE\n");
	}

	mutex_unlock(&ctx->hdcp_wq_lock);
	drm_modeset_unlock(&drm_dev->mode_config.connection_mutex);
}

static int anx7625_connector_atomic_check(struct anx7625_data *ctx,
					  struct drm_connector_state *state)
{
	struct device *dev = &ctx->client->dev;
	int cp;

	dev_dbg(dev, "hdcp state check\n");
	cp = state->content_protection;

	if (cp == ctx->hdcp_cp)
		return 0;

	if (cp == DRM_MODE_CONTENT_PROTECTION_DESIRED) {
		if (ctx->dp_en) {
			dev_dbg(dev, "enable HDCP\n");
			anx7625_hdcp_enable(ctx);

			queue_delayed_work(ctx->hdcp_workqueue,
					   &ctx->hdcp_work,
					   msecs_to_jiffies(2000));
		}
	}

	if (cp == DRM_MODE_CONTENT_PROTECTION_UNDESIRED) {
		if (ctx->hdcp_cp != DRM_MODE_CONTENT_PROTECTION_ENABLED) {
			dev_err(dev, "current CP is not ENABLED\n");
			return -EINVAL;
		}
		anx7625_hdcp_disable(ctx);
		ctx->hdcp_cp = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;
		drm_hdcp_update_content_protection(ctx->connector,
						   ctx->hdcp_cp);
		dev_dbg(dev, "update CP to UNDESIRE\n");
	}

	if (cp == DRM_MODE_CONTENT_PROTECTION_ENABLED) {
		dev_err(dev, "Userspace illegal set to PROTECTION ENABLE\n");
		return -EINVAL;
	}

	return 0;
}

static int anx7625_bridge_attach(struct drm_bridge *bridge,
				 enum drm_bridge_attach_flags flags)
{
	struct anx7625_data *ctx = bridge_to_anx7625(bridge);
	int err;
	struct device *dev = &ctx->client->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "drm attach\n");
	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR))
		return -EINVAL;

	if (!bridge->encoder) {
		DRM_DEV_ERROR(dev, "Parent encoder object not found");
		return -ENODEV;
	}

	if (ctx->pdata.panel_bridge) {
		err = drm_bridge_attach(bridge->encoder,
					ctx->pdata.panel_bridge,
					&ctx->bridge, flags);
		if (err)
			return err;
	}

	ctx->bridge_attached = 1;

	return 0;
}

static enum drm_mode_status
anx7625_bridge_mode_valid(struct drm_bridge *bridge,
			  const struct drm_display_info *info,
			  const struct drm_display_mode *mode)
{
	struct anx7625_data *ctx = bridge_to_anx7625(bridge);
	struct device *dev = &ctx->client->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "drm mode checking\n");

	/* Max 1200p at 5.4 Ghz, one lane, pixel clock 300M */
	if (mode->clock > SUPPORT_PIXEL_CLOCK) {
		DRM_DEV_DEBUG_DRIVER(dev,
				     "drm mode invalid, pixelclock too high.\n");
		return MODE_CLOCK_HIGH;
	}

	DRM_DEV_DEBUG_DRIVER(dev, "drm mode valid.\n");

	return MODE_OK;
}

static void anx7625_bridge_mode_set(struct drm_bridge *bridge,
				    const struct drm_display_mode *old_mode,
				    const struct drm_display_mode *mode)
{
	struct anx7625_data *ctx = bridge_to_anx7625(bridge);
	struct device *dev = &ctx->client->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "drm mode set\n");

	ctx->dt.pixelclock.min = mode->clock;
	ctx->dt.hactive.min = mode->hdisplay;
	ctx->dt.hsync_len.min = mode->hsync_end - mode->hsync_start;
	ctx->dt.hfront_porch.min = mode->hsync_start - mode->hdisplay;
	ctx->dt.hback_porch.min = mode->htotal - mode->hsync_end;
	ctx->dt.vactive.min = mode->vdisplay;
	ctx->dt.vsync_len.min = mode->vsync_end - mode->vsync_start;
	ctx->dt.vfront_porch.min = mode->vsync_start - mode->vdisplay;
	ctx->dt.vback_porch.min = mode->vtotal - mode->vsync_end;

	ctx->display_timing_valid = 1;

	DRM_DEV_DEBUG_DRIVER(dev, "pixelclock(%d).\n", ctx->dt.pixelclock.min);
	DRM_DEV_DEBUG_DRIVER(dev, "hactive(%d), hsync(%d), hfp(%d), hbp(%d)\n",
			     ctx->dt.hactive.min,
			     ctx->dt.hsync_len.min,
			     ctx->dt.hfront_porch.min,
			     ctx->dt.hback_porch.min);
	DRM_DEV_DEBUG_DRIVER(dev, "vactive(%d), vsync(%d), vfp(%d), vbp(%d)\n",
			     ctx->dt.vactive.min,
			     ctx->dt.vsync_len.min,
			     ctx->dt.vfront_porch.min,
			     ctx->dt.vback_porch.min);
	DRM_DEV_DEBUG_DRIVER(dev, "hdisplay(%d),hsync_start(%d).\n",
			     mode->hdisplay,
			     mode->hsync_start);
	DRM_DEV_DEBUG_DRIVER(dev, "hsync_end(%d),htotal(%d).\n",
			     mode->hsync_end,
			     mode->htotal);
	DRM_DEV_DEBUG_DRIVER(dev, "vdisplay(%d),vsync_start(%d).\n",
			     mode->vdisplay,
			     mode->vsync_start);
	DRM_DEV_DEBUG_DRIVER(dev, "vsync_end(%d),vtotal(%d).\n",
			     mode->vsync_end,
			     mode->vtotal);
}

static bool anx7625_bridge_mode_fixup(struct drm_bridge *bridge,
				      const struct drm_display_mode *mode,
				      struct drm_display_mode *adj)
{
	struct anx7625_data *ctx = bridge_to_anx7625(bridge);
	struct device *dev = &ctx->client->dev;
	u32 hsync, hfp, hbp, hblanking;
	u32 adj_hsync, adj_hfp, adj_hbp, adj_hblanking, delta_adj;
	u32 vref, adj_clock;

	DRM_DEV_DEBUG_DRIVER(dev, "drm mode fixup set\n");

	/* No need fixup for external monitor */
	if (!ctx->pdata.panel_bridge)
		return true;

	hsync = mode->hsync_end - mode->hsync_start;
	hfp = mode->hsync_start - mode->hdisplay;
	hbp = mode->htotal - mode->hsync_end;
	hblanking = mode->htotal - mode->hdisplay;

	DRM_DEV_DEBUG_DRIVER(dev, "before mode fixup\n");
	DRM_DEV_DEBUG_DRIVER(dev, "hsync(%d), hfp(%d), hbp(%d), clock(%d)\n",
			     hsync, hfp, hbp, adj->clock);
	DRM_DEV_DEBUG_DRIVER(dev, "hsync_start(%d), hsync_end(%d), htot(%d)\n",
			     adj->hsync_start, adj->hsync_end, adj->htotal);

	adj_hfp = hfp;
	adj_hsync = hsync;
	adj_hbp = hbp;
	adj_hblanking = hblanking;

	/* HFP needs to be even */
	if (hfp & 0x1) {
		adj_hfp += 1;
		adj_hblanking += 1;
	}

	/* HBP needs to be even */
	if (hbp & 0x1) {
		adj_hbp -= 1;
		adj_hblanking -= 1;
	}

	/* HSYNC needs to be even */
	if (hsync & 0x1) {
		if (adj_hblanking < hblanking)
			adj_hsync += 1;
		else
			adj_hsync -= 1;
	}

	/*
	 * Once illegal timing detected, use default HFP, HSYNC, HBP
	 * This adjusting made for built-in eDP panel, for the externel
	 * DP monitor, may need return false.
	 */
	if (hblanking < HBLANKING_MIN || (hfp < HP_MIN && hbp < HP_MIN)) {
		adj_hsync = SYNC_LEN_DEF;
		adj_hfp = HFP_HBP_DEF;
		adj_hbp = HFP_HBP_DEF;
		vref = adj->clock * 1000 / (adj->htotal * adj->vtotal);
		if (hblanking < HBLANKING_MIN) {
			delta_adj = HBLANKING_MIN - hblanking;
			adj_clock = vref * delta_adj * adj->vtotal;
			adj->clock += DIV_ROUND_UP(adj_clock, 1000);
		} else {
			delta_adj = hblanking - HBLANKING_MIN;
			adj_clock = vref * delta_adj * adj->vtotal;
			adj->clock -= DIV_ROUND_UP(adj_clock, 1000);
		}

		DRM_WARN("illegal hblanking timing, use default.\n");
		DRM_WARN("hfp(%d), hbp(%d), hsync(%d).\n", hfp, hbp, hsync);
	} else if (adj_hfp < HP_MIN) {
		/* Adjust hfp if hfp less than HP_MIN */
		delta_adj = HP_MIN - adj_hfp;
		adj_hfp = HP_MIN;

		/*
		 * Balance total HBlanking pixel, if HBP does not have enough
		 * space, adjust HSYNC length, otherwise adjust HBP
		 */
		if ((adj_hbp - delta_adj) < HP_MIN)
			/* HBP not enough space */
			adj_hsync -= delta_adj;
		else
			adj_hbp -= delta_adj;
	} else if (adj_hbp < HP_MIN) {
		delta_adj = HP_MIN - adj_hbp;
		adj_hbp = HP_MIN;

		/*
		 * Balance total HBlanking pixel, if HBP hasn't enough space,
		 * adjust HSYNC length, otherwize adjust HBP
		 */
		if ((adj_hfp - delta_adj) < HP_MIN)
			/* HFP not enough space */
			adj_hsync -= delta_adj;
		else
			adj_hfp -= delta_adj;
	}

	DRM_DEV_DEBUG_DRIVER(dev, "after mode fixup\n");
	DRM_DEV_DEBUG_DRIVER(dev, "hsync(%d), hfp(%d), hbp(%d), clock(%d)\n",
			     adj_hsync, adj_hfp, adj_hbp, adj->clock);

	/* Reconstruct timing */
	adj->hsync_start = adj->hdisplay + adj_hfp;
	adj->hsync_end = adj->hsync_start + adj_hsync;
	adj->htotal = adj->hsync_end + adj_hbp;
	DRM_DEV_DEBUG_DRIVER(dev, "hsync_start(%d), hsync_end(%d), htot(%d)\n",
			     adj->hsync_start, adj->hsync_end, adj->htotal);

	return true;
}

static int anx7625_bridge_atomic_check(struct drm_bridge *bridge,
				       struct drm_bridge_state *bridge_state,
				       struct drm_crtc_state *crtc_state,
				       struct drm_connector_state *conn_state)
{
	struct anx7625_data *ctx = bridge_to_anx7625(bridge);
	struct device *dev = &ctx->client->dev;

	dev_dbg(dev, "drm bridge atomic check\n");

	anx7625_bridge_mode_fixup(bridge, &crtc_state->mode,
				  &crtc_state->adjusted_mode);

	return anx7625_connector_atomic_check(ctx, conn_state);
}

static void anx7625_bridge_atomic_enable(struct drm_bridge *bridge,
					 struct drm_bridge_state *state)
{
	struct anx7625_data *ctx = bridge_to_anx7625(bridge);
	struct device *dev = &ctx->client->dev;
	struct drm_connector *connector;

	dev_dbg(dev, "drm atomic enable\n");

	if (!bridge->encoder) {
		dev_err(dev, "Parent encoder object not found");
		return;
	}

	connector = drm_atomic_get_new_connector_for_encoder(state->base.state,
							     bridge->encoder);
	if (!connector)
		return;

	ctx->connector = connector;

	pm_runtime_get_sync(dev);

	anx7625_dp_start(ctx);
}

static void anx7625_bridge_atomic_disable(struct drm_bridge *bridge,
					  struct drm_bridge_state *old)
{
	struct anx7625_data *ctx = bridge_to_anx7625(bridge);
	struct device *dev = &ctx->client->dev;

	dev_dbg(dev, "drm atomic disable\n");

	ctx->connector = NULL;
	anx7625_dp_stop(ctx);

	pm_runtime_put_sync(dev);
}

static enum drm_connector_status
anx7625_bridge_detect(struct drm_bridge *bridge)
{
	struct anx7625_data *ctx = bridge_to_anx7625(bridge);
	struct device *dev = &ctx->client->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "drm bridge detect\n");

	return anx7625_sink_detect(ctx);
}

static struct edid *anx7625_bridge_get_edid(struct drm_bridge *bridge,
					    struct drm_connector *connector)
{
	struct anx7625_data *ctx = bridge_to_anx7625(bridge);
	struct device *dev = &ctx->client->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "drm bridge get edid\n");

	return anx7625_get_edid(ctx);
}

static const struct drm_bridge_funcs anx7625_bridge_funcs = {
	.attach = anx7625_bridge_attach,
	.mode_valid = anx7625_bridge_mode_valid,
	.mode_set = anx7625_bridge_mode_set,
	.atomic_check = anx7625_bridge_atomic_check,
	.atomic_enable = anx7625_bridge_atomic_enable,
	.atomic_disable = anx7625_bridge_atomic_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.detect = anx7625_bridge_detect,
	.get_edid = anx7625_bridge_get_edid,
};

static int anx7625_register_i2c_dummy_clients(struct anx7625_data *ctx,
					      struct i2c_client *client)
{
	int err = 0;

	ctx->i2c.tx_p0_client = i2c_new_dummy_device(client->adapter,
						     TX_P0_ADDR >> 1);
	if (IS_ERR(ctx->i2c.tx_p0_client))
		return PTR_ERR(ctx->i2c.tx_p0_client);

	ctx->i2c.tx_p1_client = i2c_new_dummy_device(client->adapter,
						     TX_P1_ADDR >> 1);
	if (IS_ERR(ctx->i2c.tx_p1_client)) {
		err = PTR_ERR(ctx->i2c.tx_p1_client);
		goto free_tx_p0;
	}

	ctx->i2c.tx_p2_client = i2c_new_dummy_device(client->adapter,
						     TX_P2_ADDR >> 1);
	if (IS_ERR(ctx->i2c.tx_p2_client)) {
		err = PTR_ERR(ctx->i2c.tx_p2_client);
		goto free_tx_p1;
	}

	ctx->i2c.rx_p0_client = i2c_new_dummy_device(client->adapter,
						     RX_P0_ADDR >> 1);
	if (IS_ERR(ctx->i2c.rx_p0_client)) {
		err = PTR_ERR(ctx->i2c.rx_p0_client);
		goto free_tx_p2;
	}

	ctx->i2c.rx_p1_client = i2c_new_dummy_device(client->adapter,
						     RX_P1_ADDR >> 1);
	if (IS_ERR(ctx->i2c.rx_p1_client)) {
		err = PTR_ERR(ctx->i2c.rx_p1_client);
		goto free_rx_p0;
	}

	ctx->i2c.rx_p2_client = i2c_new_dummy_device(client->adapter,
						     RX_P2_ADDR >> 1);
	if (IS_ERR(ctx->i2c.rx_p2_client)) {
		err = PTR_ERR(ctx->i2c.rx_p2_client);
		goto free_rx_p1;
	}

	ctx->i2c.tcpc_client = i2c_new_dummy_device(client->adapter,
						    TCPC_INTERFACE_ADDR >> 1);
	if (IS_ERR(ctx->i2c.tcpc_client)) {
		err = PTR_ERR(ctx->i2c.tcpc_client);
		goto free_rx_p2;
	}

	return 0;

free_rx_p2:
	i2c_unregister_device(ctx->i2c.rx_p2_client);
free_rx_p1:
	i2c_unregister_device(ctx->i2c.rx_p1_client);
free_rx_p0:
	i2c_unregister_device(ctx->i2c.rx_p0_client);
free_tx_p2:
	i2c_unregister_device(ctx->i2c.tx_p2_client);
free_tx_p1:
	i2c_unregister_device(ctx->i2c.tx_p1_client);
free_tx_p0:
	i2c_unregister_device(ctx->i2c.tx_p0_client);

	return err;
}

static void anx7625_unregister_i2c_dummy_clients(struct anx7625_data *ctx)
{
	i2c_unregister_device(ctx->i2c.tx_p0_client);
	i2c_unregister_device(ctx->i2c.tx_p1_client);
	i2c_unregister_device(ctx->i2c.tx_p2_client);
	i2c_unregister_device(ctx->i2c.rx_p0_client);
	i2c_unregister_device(ctx->i2c.rx_p1_client);
	i2c_unregister_device(ctx->i2c.rx_p2_client);
	i2c_unregister_device(ctx->i2c.tcpc_client);
}

static int __maybe_unused anx7625_runtime_pm_suspend(struct device *dev)
{
	struct anx7625_data *ctx = dev_get_drvdata(dev);

	mutex_lock(&ctx->lock);

	anx7625_stop_dp_work(ctx);
	anx7625_power_standby(ctx);

	mutex_unlock(&ctx->lock);

	return 0;
}

static int __maybe_unused anx7625_runtime_pm_resume(struct device *dev)
{
	struct anx7625_data *ctx = dev_get_drvdata(dev);

	mutex_lock(&ctx->lock);

	anx7625_power_on_init(ctx);
	anx7625_hpd_polling(ctx);

	mutex_unlock(&ctx->lock);

	return 0;
}

static int __maybe_unused anx7625_resume(struct device *dev)
{
	struct anx7625_data *ctx = dev_get_drvdata(dev);

	if (!ctx->pdata.intp_irq)
		return 0;

	if (!pm_runtime_enabled(dev) || !pm_runtime_suspended(dev)) {
		enable_irq(ctx->pdata.intp_irq);
		anx7625_runtime_pm_resume(dev);
	}

	return 0;
}

static int __maybe_unused anx7625_suspend(struct device *dev)
{
	struct anx7625_data *ctx = dev_get_drvdata(dev);

	if (!ctx->pdata.intp_irq)
		return 0;

	if (!pm_runtime_enabled(dev) || !pm_runtime_suspended(dev)) {
		anx7625_runtime_pm_suspend(dev);
		disable_irq(ctx->pdata.intp_irq);
	}

	return 0;
}

static const struct dev_pm_ops anx7625_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(anx7625_suspend, anx7625_resume)
	SET_RUNTIME_PM_OPS(anx7625_runtime_pm_suspend,
			   anx7625_runtime_pm_resume, NULL)
};

static int anx7625_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct anx7625_data *platform;
	struct anx7625_platform_data *pdata;
	int ret = 0;
	struct device *dev = &client->dev;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_I2C_BLOCK)) {
		DRM_DEV_ERROR(dev, "anx7625's i2c bus doesn't support\n");
		return -ENODEV;
	}

	platform = kzalloc(sizeof(*platform), GFP_KERNEL);
	if (!platform) {
		DRM_DEV_ERROR(dev, "fail to allocate driver data\n");
		return -ENOMEM;
	}

	pdata = &platform->pdata;

	ret = anx7625_parse_dt(dev, pdata);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev, "fail to parse DT : %d\n", ret);
		goto free_platform;
	}

	platform->client = client;
	i2c_set_clientdata(client, platform);

	pdata->supplies[0].supply = "vdd10";
	pdata->supplies[1].supply = "vdd18";
	pdata->supplies[2].supply = "vdd33";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(pdata->supplies),
				      pdata->supplies);
	if (ret) {
		DRM_DEV_ERROR(dev, "fail to get power supplies: %d\n", ret);
		return ret;
	}
	anx7625_init_gpio(platform);

	mutex_init(&platform->lock);
	mutex_init(&platform->hdcp_wq_lock);

	INIT_DELAYED_WORK(&platform->hdcp_work, hdcp_check_work_func);
	platform->hdcp_workqueue = create_workqueue("hdcp workqueue");
	if (!platform->hdcp_workqueue) {
		dev_err(dev, "fail to create work queue\n");
		ret = -ENOMEM;
		goto free_platform;
	}

	platform->pdata.intp_irq = client->irq;
	if (platform->pdata.intp_irq) {
		INIT_WORK(&platform->work, anx7625_work_func);
		platform->workqueue = alloc_workqueue("anx7625_work",
						      WQ_FREEZABLE | WQ_MEM_RECLAIM, 1);
		if (!platform->workqueue) {
			DRM_DEV_ERROR(dev, "fail to create work queue\n");
			ret = -ENOMEM;
			goto free_hdcp_wq;
		}

		ret = devm_request_threaded_irq(dev, platform->pdata.intp_irq,
						NULL, anx7625_intr_hpd_isr,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"anx7625-intp", platform);
		if (ret) {
			DRM_DEV_ERROR(dev, "fail to request irq\n");
			goto free_wq;
		}
	}

	if (anx7625_register_i2c_dummy_clients(platform, client) != 0) {
		ret = -ENOMEM;
		DRM_DEV_ERROR(dev, "fail to reserve I2C bus.\n");
		goto free_wq;
	}

	pm_runtime_enable(dev);

	if (!platform->pdata.low_power_mode) {
		anx7625_disable_pd_protocol(platform);
		pm_runtime_get_sync(dev);
	}

	/* Add work function */
	if (platform->pdata.intp_irq)
		queue_work(platform->workqueue, &platform->work);

	platform->bridge.funcs = &anx7625_bridge_funcs;
	platform->bridge.of_node = client->dev.of_node;
	platform->bridge.ops = DRM_BRIDGE_OP_EDID;
	if (!platform->pdata.panel_bridge)
		platform->bridge.ops |= DRM_BRIDGE_OP_HPD |
					DRM_BRIDGE_OP_DETECT;
	platform->bridge.type = platform->pdata.panel_bridge ?
				    DRM_MODE_CONNECTOR_eDP :
				    DRM_MODE_CONNECTOR_DisplayPort;

	drm_bridge_add(&platform->bridge);

	if (!platform->pdata.is_dpi) {
		ret = anx7625_attach_dsi(platform);
		if (ret) {
			DRM_DEV_ERROR(dev, "Fail to attach to dsi : %d\n", ret);
			goto unregister_bridge;
		}
	}

	if (platform->pdata.audio_en)
		anx7625_register_audio(dev, platform);

	DRM_DEV_DEBUG_DRIVER(dev, "probe done\n");

	return 0;

unregister_bridge:
	drm_bridge_remove(&platform->bridge);

	if (!platform->pdata.low_power_mode)
		pm_runtime_put_sync_suspend(&client->dev);

	anx7625_unregister_i2c_dummy_clients(platform);

free_wq:
	if (platform->workqueue)
		destroy_workqueue(platform->workqueue);

free_hdcp_wq:
	if (platform->hdcp_workqueue)
		destroy_workqueue(platform->hdcp_workqueue);

free_platform:
	kfree(platform);

	return ret;
}

static int anx7625_i2c_remove(struct i2c_client *client)
{
	struct anx7625_data *platform = i2c_get_clientdata(client);

	drm_bridge_remove(&platform->bridge);

	if (platform->pdata.intp_irq)
		destroy_workqueue(platform->workqueue);

	if (platform->hdcp_workqueue) {
		cancel_delayed_work(&platform->hdcp_work);
		flush_workqueue(platform->workqueue);
		destroy_workqueue(platform->workqueue);
	}

	if (!platform->pdata.low_power_mode)
		pm_runtime_put_sync_suspend(&client->dev);

	anx7625_unregister_i2c_dummy_clients(platform);

	if (platform->pdata.audio_en)
		anx7625_unregister_audio(platform);

	kfree(platform);
	return 0;
}

static const struct i2c_device_id anx7625_id[] = {
	{"anx7625", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, anx7625_id);

static const struct of_device_id anx_match_table[] = {
	{.compatible = "analogix,anx7625",},
	{},
};
MODULE_DEVICE_TABLE(of, anx_match_table);

static struct i2c_driver anx7625_driver = {
	.driver = {
		.name = "anx7625",
		.of_match_table = anx_match_table,
		.pm = &anx7625_pm_ops,
	},
	.probe = anx7625_i2c_probe,
	.remove = anx7625_i2c_remove,

	.id_table = anx7625_id,
};

module_i2c_driver(anx7625_driver);

MODULE_DESCRIPTION("MIPI2DP anx7625 driver");
MODULE_AUTHOR("Xin Ji <xji@analogixsemi.com>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(ANX7625_DRV_VERSION);
