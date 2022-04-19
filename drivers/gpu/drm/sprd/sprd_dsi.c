// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_graph.h>
#include <video/mipi_display.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>

#include "sprd_drm.h"
#include "sprd_dpu.h"
#include "sprd_dsi.h"

#define SOFT_RESET 0x04
#define MASK_PROTOCOL_INT 0x0C
#define MASK_INTERNAL_INT 0x14
#define DSI_MODE_CFG 0x18

#define VIRTUAL_CHANNEL_ID 0x1C
#define GEN_RX_VCID GENMASK(1, 0)
#define VIDEO_PKT_VCID GENMASK(3, 2)

#define DPI_VIDEO_FORMAT 0x20
#define DPI_VIDEO_MODE_FORMAT GENMASK(5, 0)
#define LOOSELY18_EN BIT(6)

#define VIDEO_PKT_CONFIG 0x24
#define VIDEO_PKT_SIZE GENMASK(15, 0)
#define VIDEO_LINE_CHUNK_NUM GENMASK(31, 16)

#define VIDEO_LINE_HBLK_TIME 0x28
#define VIDEO_LINE_HBP_TIME GENMASK(15, 0)
#define VIDEO_LINE_HSA_TIME GENMASK(31, 16)

#define VIDEO_LINE_TIME 0x2C

#define VIDEO_VBLK_LINES 0x30
#define VFP_LINES GENMASK(9, 0)
#define VBP_LINES GENMASK(19, 10)
#define VSA_LINES GENMASK(29, 20)

#define VIDEO_VACTIVE_LINES 0x34

#define VID_MODE_CFG 0x38
#define VID_MODE_TYPE GENMASK(1, 0)
#define LP_VSA_EN BIT(8)
#define LP_VBP_EN BIT(9)
#define LP_VFP_EN BIT(10)
#define LP_VACT_EN BIT(11)
#define LP_HBP_EN BIT(12)
#define LP_HFP_EN BIT(13)
#define FRAME_BTA_ACK_EN BIT(14)

#define TIMEOUT_CNT_CLK_CONFIG 0x40
#define HTX_TO_CONFIG 0x44
#define LRX_H_TO_CONFIG 0x48

#define TX_ESC_CLK_CONFIG 0x5C

#define CMD_MODE_CFG 0x68
#define TEAR_FX_EN BIT(0)

#define GEN_HDR 0x6C
#define GEN_DT GENMASK(5, 0)
#define GEN_VC GENMASK(7, 6)

#define GEN_PLD_DATA 0x70

#define PHY_CLK_LANE_LP_CTRL 0x74
#define PHY_CLKLANE_TX_REQ_HS BIT(0)
#define AUTO_CLKLANE_CTRL_EN BIT(1)

#define PHY_INTERFACE_CTRL 0x78
#define RF_PHY_SHUTDOWN BIT(0)
#define RF_PHY_RESET_N BIT(1)
#define RF_PHY_CLK_EN BIT(2)

#define CMD_MODE_STATUS 0x98
#define GEN_CMD_RDATA_FIFO_EMPTY BIT(1)
#define GEN_CMD_WDATA_FIFO_EMPTY BIT(3)
#define GEN_CMD_CMD_FIFO_EMPTY BIT(5)
#define GEN_CMD_RDCMD_DONE BIT(7)

#define PHY_STATUS 0x9C
#define PHY_LOCK BIT(1)

#define PHY_MIN_STOP_TIME 0xA0
#define PHY_LANE_NUM_CONFIG 0xA4

#define PHY_CLKLANE_TIME_CONFIG 0xA8
#define PHY_CLKLANE_LP_TO_HS_TIME GENMASK(15, 0)
#define PHY_CLKLANE_HS_TO_LP_TIME GENMASK(31, 16)

#define PHY_DATALANE_TIME_CONFIG 0xAC
#define PHY_DATALANE_LP_TO_HS_TIME GENMASK(15, 0)
#define PHY_DATALANE_HS_TO_LP_TIME GENMASK(31, 16)

#define MAX_READ_TIME 0xB0

#define RX_PKT_CHECK_CONFIG 0xB4
#define RX_PKT_ECC_EN BIT(0)
#define RX_PKT_CRC_EN BIT(1)

#define TA_EN 0xB8

#define EOTP_EN 0xBC
#define TX_EOTP_EN BIT(0)
#define RX_EOTP_EN BIT(1)

#define VIDEO_NULLPKT_SIZE 0xC0
#define DCS_WM_PKT_SIZE 0xC4

#define VIDEO_SIG_DELAY_CONFIG 0xD0
#define VIDEO_SIG_DELAY GENMASK(23, 0)

#define PHY_TST_CTRL0 0xF0
#define PHY_TESTCLR BIT(0)
#define PHY_TESTCLK BIT(1)

#define PHY_TST_CTRL1 0xF4
#define PHY_TESTDIN GENMASK(7, 0)
#define PHY_TESTDOUT GENMASK(15, 8)
#define PHY_TESTEN BIT(16)

#define host_to_dsi(host) \
	container_of(host, struct sprd_dsi, host)

static inline u32
dsi_reg_rd(struct dsi_context *ctx, u32 offset, u32 mask,
	   u32 shift)
{
	return (readl(ctx->base + offset) & mask) >> shift;
}

static inline void
dsi_reg_wr(struct dsi_context *ctx, u32 offset, u32 mask,
	   u32 shift, u32 val)
{
	u32 ret;

	ret = readl(ctx->base + offset);
	ret &= ~mask;
	ret |= (val << shift) & mask;
	writel(ret, ctx->base + offset);
}

static inline void
dsi_reg_up(struct dsi_context *ctx, u32 offset, u32 mask,
	   u32 val)
{
	u32 ret = readl(ctx->base + offset);

	writel((ret & ~mask) | (val & mask), ctx->base + offset);
}

static int regmap_tst_io_write(void *context, u32 reg, u32 val)
{
	struct sprd_dsi *dsi = context;
	struct dsi_context *ctx = &dsi->ctx;

	if (val > 0xff || reg > 0xff)
		return -EINVAL;

	drm_dbg(dsi->drm, "reg = 0x%02x, val = 0x%02x\n", reg, val);

	dsi_reg_up(ctx, PHY_TST_CTRL1, PHY_TESTEN, PHY_TESTEN);
	dsi_reg_wr(ctx, PHY_TST_CTRL1, PHY_TESTDIN, 0, reg);
	dsi_reg_up(ctx, PHY_TST_CTRL0, PHY_TESTCLK, PHY_TESTCLK);
	dsi_reg_up(ctx, PHY_TST_CTRL0, PHY_TESTCLK, 0);
	dsi_reg_up(ctx, PHY_TST_CTRL1, PHY_TESTEN, 0);
	dsi_reg_wr(ctx, PHY_TST_CTRL1, PHY_TESTDIN, 0, val);
	dsi_reg_up(ctx, PHY_TST_CTRL0, PHY_TESTCLK, PHY_TESTCLK);
	dsi_reg_up(ctx, PHY_TST_CTRL0, PHY_TESTCLK, 0);

	return 0;
}

static int regmap_tst_io_read(void *context, u32 reg, u32 *val)
{
	struct sprd_dsi *dsi = context;
	struct dsi_context *ctx = &dsi->ctx;
	int ret;

	if (reg > 0xff)
		return -EINVAL;

	dsi_reg_up(ctx, PHY_TST_CTRL1, PHY_TESTEN, PHY_TESTEN);
	dsi_reg_wr(ctx, PHY_TST_CTRL1, PHY_TESTDIN, 0, reg);
	dsi_reg_up(ctx, PHY_TST_CTRL0, PHY_TESTCLK, PHY_TESTCLK);
	dsi_reg_up(ctx, PHY_TST_CTRL0, PHY_TESTCLK, 0);
	dsi_reg_up(ctx, PHY_TST_CTRL1, PHY_TESTEN, 0);

	udelay(1);

	ret = dsi_reg_rd(ctx, PHY_TST_CTRL1, PHY_TESTDOUT, 8);
	if (ret < 0)
		return ret;

	*val = ret;

	drm_dbg(dsi->drm, "reg = 0x%02x, val = 0x%02x\n", reg, *val);
	return 0;
}

static struct regmap_bus regmap_tst_io = {
	.reg_write = regmap_tst_io_write,
	.reg_read = regmap_tst_io_read,
};

static const struct regmap_config byte_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int dphy_wait_pll_locked(struct dsi_context *ctx)
{
	struct sprd_dsi *dsi = container_of(ctx, struct sprd_dsi, ctx);
	int i;

	for (i = 0; i < 50000; i++) {
		if (dsi_reg_rd(ctx, PHY_STATUS, PHY_LOCK, 1))
			return 0;
		udelay(3);
	}

	drm_err(dsi->drm, "dphy pll can not be locked\n");
	return -ETIMEDOUT;
}

static int dsi_wait_tx_payload_fifo_empty(struct dsi_context *ctx)
{
	int i;

	for (i = 0; i < 5000; i++) {
		if (dsi_reg_rd(ctx, CMD_MODE_STATUS, GEN_CMD_WDATA_FIFO_EMPTY, 3))
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

static int dsi_wait_tx_cmd_fifo_empty(struct dsi_context *ctx)
{
	int i;

	for (i = 0; i < 5000; i++) {
		if (dsi_reg_rd(ctx, CMD_MODE_STATUS, GEN_CMD_CMD_FIFO_EMPTY, 5))
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

static int dsi_wait_rd_resp_completed(struct dsi_context *ctx)
{
	int i;

	for (i = 0; i < 10000; i++) {
		if (dsi_reg_rd(ctx, CMD_MODE_STATUS, GEN_CMD_RDCMD_DONE, 7))
			return 0;
		udelay(10);
	}

	return -ETIMEDOUT;
}

static u16 calc_bytes_per_pixel_x100(int coding)
{
	u16 bpp_x100;

	switch (coding) {
	case COLOR_CODE_16BIT_CONFIG1:
	case COLOR_CODE_16BIT_CONFIG2:
	case COLOR_CODE_16BIT_CONFIG3:
		bpp_x100 = 200;
		break;
	case COLOR_CODE_18BIT_CONFIG1:
	case COLOR_CODE_18BIT_CONFIG2:
		bpp_x100 = 225;
		break;
	case COLOR_CODE_24BIT:
		bpp_x100 = 300;
		break;
	case COLOR_CODE_COMPRESSTION:
		bpp_x100 = 100;
		break;
	case COLOR_CODE_20BIT_YCC422_LOOSELY:
		bpp_x100 = 250;
		break;
	case COLOR_CODE_24BIT_YCC422:
		bpp_x100 = 300;
		break;
	case COLOR_CODE_16BIT_YCC422:
		bpp_x100 = 200;
		break;
	case COLOR_CODE_30BIT:
		bpp_x100 = 375;
		break;
	case COLOR_CODE_36BIT:
		bpp_x100 = 450;
		break;
	case COLOR_CODE_12BIT_YCC420:
		bpp_x100 = 150;
		break;
	default:
		DRM_ERROR("invalid color coding");
		bpp_x100 = 0;
		break;
	}

	return bpp_x100;
}

static u8 calc_video_size_step(int coding)
{
	u8 video_size_step;

	switch (coding) {
	case COLOR_CODE_16BIT_CONFIG1:
	case COLOR_CODE_16BIT_CONFIG2:
	case COLOR_CODE_16BIT_CONFIG3:
	case COLOR_CODE_18BIT_CONFIG1:
	case COLOR_CODE_18BIT_CONFIG2:
	case COLOR_CODE_24BIT:
	case COLOR_CODE_COMPRESSTION:
		return video_size_step = 1;
	case COLOR_CODE_20BIT_YCC422_LOOSELY:
	case COLOR_CODE_24BIT_YCC422:
	case COLOR_CODE_16BIT_YCC422:
	case COLOR_CODE_30BIT:
	case COLOR_CODE_36BIT:
	case COLOR_CODE_12BIT_YCC420:
		return video_size_step = 2;
	default:
		DRM_ERROR("invalid color coding");
		return 0;
	}
}

static u16 round_video_size(int coding, u16 video_size)
{
	switch (coding) {
	case COLOR_CODE_16BIT_YCC422:
	case COLOR_CODE_24BIT_YCC422:
	case COLOR_CODE_20BIT_YCC422_LOOSELY:
	case COLOR_CODE_12BIT_YCC420:
		/* round up active H pixels to a multiple of 2 */
		if ((video_size % 2) != 0)
			video_size += 1;
		break;
	default:
		break;
	}

	return video_size;
}

#define SPRD_MIPI_DSI_FMT_DSC 0xff
static u32 fmt_to_coding(u32 fmt)
{
	switch (fmt) {
	case MIPI_DSI_FMT_RGB565:
		return COLOR_CODE_16BIT_CONFIG1;
	case MIPI_DSI_FMT_RGB666:
	case MIPI_DSI_FMT_RGB666_PACKED:
		return COLOR_CODE_18BIT_CONFIG1;
	case MIPI_DSI_FMT_RGB888:
		return COLOR_CODE_24BIT;
	case SPRD_MIPI_DSI_FMT_DSC:
		return COLOR_CODE_COMPRESSTION;
	default:
		DRM_ERROR("Unsupported format (%d)\n", fmt);
		return COLOR_CODE_24BIT;
	}
}

#define ns_to_cycle(ns, byte_clk) \
	DIV_ROUND_UP((ns) * (byte_clk), 1000000)

static void sprd_dsi_init(struct dsi_context *ctx)
{
	struct sprd_dsi *dsi = container_of(ctx, struct sprd_dsi, ctx);
	u32 byte_clk = dsi->slave->hs_rate / 8;
	u16 data_hs2lp, data_lp2hs, clk_hs2lp, clk_lp2hs;
	u16 max_rd_time;
	int div;

	writel(0, ctx->base + SOFT_RESET);
	writel(0xffffffff, ctx->base + MASK_PROTOCOL_INT);
	writel(0xffffffff, ctx->base + MASK_INTERNAL_INT);
	writel(1, ctx->base + DSI_MODE_CFG);
	dsi_reg_up(ctx, EOTP_EN, RX_EOTP_EN, 0);
	dsi_reg_up(ctx, EOTP_EN, TX_EOTP_EN, 0);
	dsi_reg_up(ctx, RX_PKT_CHECK_CONFIG, RX_PKT_ECC_EN, RX_PKT_ECC_EN);
	dsi_reg_up(ctx, RX_PKT_CHECK_CONFIG, RX_PKT_CRC_EN, RX_PKT_CRC_EN);
	writel(1, ctx->base + TA_EN);
	dsi_reg_up(ctx, VIRTUAL_CHANNEL_ID, VIDEO_PKT_VCID, 0);
	dsi_reg_up(ctx, VIRTUAL_CHANNEL_ID, GEN_RX_VCID, 0);

	div = DIV_ROUND_UP(byte_clk, dsi->slave->lp_rate);
	writel(div, ctx->base + TX_ESC_CLK_CONFIG);

	max_rd_time = ns_to_cycle(ctx->max_rd_time, byte_clk);
	writel(max_rd_time, ctx->base + MAX_READ_TIME);

	data_hs2lp = ns_to_cycle(ctx->data_hs2lp, byte_clk);
	data_lp2hs = ns_to_cycle(ctx->data_lp2hs, byte_clk);
	clk_hs2lp = ns_to_cycle(ctx->clk_hs2lp, byte_clk);
	clk_lp2hs = ns_to_cycle(ctx->clk_lp2hs, byte_clk);
	dsi_reg_wr(ctx, PHY_DATALANE_TIME_CONFIG,
		   PHY_DATALANE_HS_TO_LP_TIME, 16, data_hs2lp);
	dsi_reg_wr(ctx, PHY_DATALANE_TIME_CONFIG,
		   PHY_DATALANE_LP_TO_HS_TIME, 0, data_lp2hs);
	dsi_reg_wr(ctx, PHY_CLKLANE_TIME_CONFIG,
		   PHY_CLKLANE_HS_TO_LP_TIME, 16, clk_hs2lp);
	dsi_reg_wr(ctx, PHY_CLKLANE_TIME_CONFIG,
		   PHY_CLKLANE_LP_TO_HS_TIME, 0, clk_lp2hs);

	writel(1, ctx->base + SOFT_RESET);
}

/*
 * Free up resources and shutdown host controller and PHY
 */
static void sprd_dsi_fini(struct dsi_context *ctx)
{
	writel(0xffffffff, ctx->base + MASK_PROTOCOL_INT);
	writel(0xffffffff, ctx->base + MASK_INTERNAL_INT);
	writel(0, ctx->base + SOFT_RESET);
}

/*
 * If not in burst mode, it will compute the video and null packet sizes
 * according to necessity.
 * Configure timers for data lanes and/or clock lane to return to LP when
 * bandwidth is not filled by data.
 */
static int sprd_dsi_dpi_video(struct dsi_context *ctx)
{
	struct sprd_dsi *dsi = container_of(ctx, struct sprd_dsi, ctx);
	struct videomode *vm = &ctx->vm;
	u32 byte_clk = dsi->slave->hs_rate / 8;
	u16 bpp_x100;
	u16 video_size;
	u32 ratio_x1000;
	u16 null_pkt_size = 0;
	u8 video_size_step;
	u32 hs_to;
	u32 total_bytes;
	u32 bytes_per_chunk;
	u32 chunks = 0;
	u32 bytes_left = 0;
	u32 chunk_overhead;
	const u8 pkt_header = 6;
	u8 coding;
	int div;
	u16 hline;
	u16 byte_cycle;

	coding = fmt_to_coding(dsi->slave->format);
	video_size = round_video_size(coding, vm->hactive);
	bpp_x100 = calc_bytes_per_pixel_x100(coding);
	video_size_step = calc_video_size_step(coding);
	ratio_x1000 = byte_clk * 1000 / (vm->pixelclock / 1000);
	hline = vm->hactive + vm->hsync_len + vm->hfront_porch +
		vm->hback_porch;

	writel(0, ctx->base + SOFT_RESET);
	dsi_reg_wr(ctx, VID_MODE_CFG, FRAME_BTA_ACK_EN, 15, ctx->frame_ack_en);
	dsi_reg_wr(ctx, DPI_VIDEO_FORMAT, DPI_VIDEO_MODE_FORMAT, 0, coding);
	dsi_reg_wr(ctx, VID_MODE_CFG, VID_MODE_TYPE, 0, ctx->burst_mode);
	byte_cycle = 95 * hline * ratio_x1000 / 100000;
	dsi_reg_wr(ctx, VIDEO_SIG_DELAY_CONFIG, VIDEO_SIG_DELAY, 0, byte_cycle);
	byte_cycle = hline * ratio_x1000 / 1000;
	writel(byte_cycle, ctx->base + VIDEO_LINE_TIME);
	byte_cycle = vm->hsync_len * ratio_x1000 / 1000;
	dsi_reg_wr(ctx, VIDEO_LINE_HBLK_TIME, VIDEO_LINE_HSA_TIME, 16, byte_cycle);
	byte_cycle = vm->hback_porch * ratio_x1000 / 1000;
	dsi_reg_wr(ctx, VIDEO_LINE_HBLK_TIME, VIDEO_LINE_HBP_TIME, 0, byte_cycle);
	writel(vm->vactive, ctx->base + VIDEO_VACTIVE_LINES);
	dsi_reg_wr(ctx, VIDEO_VBLK_LINES, VFP_LINES, 0, vm->vfront_porch);
	dsi_reg_wr(ctx, VIDEO_VBLK_LINES, VBP_LINES, 10, vm->vback_porch);
	dsi_reg_wr(ctx, VIDEO_VBLK_LINES, VSA_LINES, 20, vm->vsync_len);
	dsi_reg_up(ctx, VID_MODE_CFG, LP_HBP_EN | LP_HFP_EN | LP_VACT_EN |
			LP_VFP_EN | LP_VBP_EN | LP_VSA_EN, LP_HBP_EN | LP_HFP_EN |
			LP_VACT_EN | LP_VFP_EN | LP_VBP_EN | LP_VSA_EN);

	hs_to = (hline * vm->vactive) + (2 * bpp_x100) / 100;
	for (div = 0x80; (div < hs_to) && (div > 2); div--) {
		if ((hs_to % div) == 0) {
			writel(div, ctx->base + TIMEOUT_CNT_CLK_CONFIG);
			writel(hs_to / div, ctx->base + LRX_H_TO_CONFIG);
			writel(hs_to / div, ctx->base + HTX_TO_CONFIG);
			break;
		}
	}

	if (ctx->burst_mode == VIDEO_BURST_WITH_SYNC_PULSES) {
		dsi_reg_wr(ctx, VIDEO_PKT_CONFIG, VIDEO_PKT_SIZE, 0, video_size);
		writel(0, ctx->base + VIDEO_NULLPKT_SIZE);
		dsi_reg_up(ctx, VIDEO_PKT_CONFIG, VIDEO_LINE_CHUNK_NUM, 0);
	} else {
		/* non burst transmission */
		null_pkt_size = 0;

		/* bytes to be sent - first as one chunk */
		bytes_per_chunk = vm->hactive * bpp_x100 / 100 + pkt_header;

		/* hline total bytes from the DPI interface */
		total_bytes = (vm->hactive + vm->hfront_porch) *
				ratio_x1000 / dsi->slave->lanes / 1000;

		/* check if the pixels actually fit on the DSI link */
		if (total_bytes < bytes_per_chunk) {
			drm_err(dsi->drm, "current resolution can not be set\n");
			return -EINVAL;
		}

		chunk_overhead = total_bytes - bytes_per_chunk;

		/* overhead higher than 1 -> enable multi packets */
		if (chunk_overhead > 1) {
			/* multi packets */
			for (video_size = video_size_step;
			     video_size < vm->hactive;
			     video_size += video_size_step) {
				if (vm->hactive * 1000 / video_size % 1000)
					continue;

				chunks = vm->hactive / video_size;
				bytes_per_chunk = bpp_x100 * video_size / 100
						  + pkt_header;
				if (total_bytes >= (bytes_per_chunk * chunks)) {
					bytes_left = total_bytes -
						     bytes_per_chunk * chunks;
					break;
				}
			}

			/* prevent overflow (unsigned - unsigned) */
			if (bytes_left > (pkt_header * chunks)) {
				null_pkt_size = (bytes_left -
						pkt_header * chunks) / chunks;
				/* avoid register overflow */
				if (null_pkt_size > 1023)
					null_pkt_size = 1023;
			}

		} else {
			/* single packet */
			chunks = 1;

			/* must be a multiple of 4 except 18 loosely */
			for (video_size = vm->hactive;
			    (video_size % video_size_step) != 0;
			     video_size++)
				;
		}

		dsi_reg_wr(ctx, VIDEO_PKT_CONFIG, VIDEO_PKT_SIZE, 0, video_size);
		writel(null_pkt_size, ctx->base + VIDEO_NULLPKT_SIZE);
		dsi_reg_wr(ctx, VIDEO_PKT_CONFIG, VIDEO_LINE_CHUNK_NUM, 16, chunks);
	}

	writel(ctx->int0_mask, ctx->base + MASK_PROTOCOL_INT);
	writel(ctx->int1_mask, ctx->base + MASK_INTERNAL_INT);
	writel(1, ctx->base + SOFT_RESET);

	return 0;
}

static void sprd_dsi_edpi_video(struct dsi_context *ctx)
{
	struct sprd_dsi *dsi = container_of(ctx, struct sprd_dsi, ctx);
	const u32 fifo_depth = 1096;
	const u32 word_length = 4;
	u32 hactive = ctx->vm.hactive;
	u32 bpp_x100;
	u32 max_fifo_len;
	u8 coding;

	coding = fmt_to_coding(dsi->slave->format);
	bpp_x100 = calc_bytes_per_pixel_x100(coding);
	max_fifo_len = word_length * fifo_depth * 100 / bpp_x100;

	writel(0, ctx->base + SOFT_RESET);
	dsi_reg_wr(ctx, DPI_VIDEO_FORMAT, DPI_VIDEO_MODE_FORMAT, 0, coding);
	dsi_reg_wr(ctx, CMD_MODE_CFG, TEAR_FX_EN, 0, ctx->te_ack_en);

	if (max_fifo_len > hactive)
		writel(hactive, ctx->base + DCS_WM_PKT_SIZE);
	else
		writel(max_fifo_len, ctx->base + DCS_WM_PKT_SIZE);

	writel(ctx->int0_mask, ctx->base + MASK_PROTOCOL_INT);
	writel(ctx->int1_mask, ctx->base + MASK_INTERNAL_INT);
	writel(1, ctx->base + SOFT_RESET);
}

/*
 * Send a packet on the generic interface,
 * this function has an active delay to wait for the buffer to clear.
 * The delay is limited to:
 * (param_length / 4) x DSIH_FIFO_ACTIVE_WAIT x register access time
 * the controller restricts the sending of.
 *
 * This function will not be able to send Null and Blanking packets due to
 * controller restriction
 */
static int sprd_dsi_wr_pkt(struct dsi_context *ctx, u8 vc, u8 type,
			   const u8 *param, u16 len)
{
	struct sprd_dsi *dsi = container_of(ctx, struct sprd_dsi, ctx);
	u8 wc_lsbyte, wc_msbyte;
	u32 payload;
	int i, j, ret;

	if (vc > 3)
		return -EINVAL;

	/* 1st: for long packet, must config payload first */
	ret = dsi_wait_tx_payload_fifo_empty(ctx);
	if (ret) {
		drm_err(dsi->drm, "tx payload fifo is not empty\n");
		return ret;
	}

	if (len > 2) {
		for (i = 0, j = 0; i < len; i += j) {
			payload = 0;
			for (j = 0; (j < 4) && ((j + i) < (len)); j++)
				payload |= param[i + j] << (j * 8);

			writel(payload, ctx->base + GEN_PLD_DATA);
		}
		wc_lsbyte = len & 0xff;
		wc_msbyte = len >> 8;
	} else {
		wc_lsbyte = (len > 0) ? param[0] : 0;
		wc_msbyte = (len > 1) ? param[1] : 0;
	}

	/* 2nd: then set packet header */
	ret = dsi_wait_tx_cmd_fifo_empty(ctx);
	if (ret) {
		drm_err(dsi->drm, "tx cmd fifo is not empty\n");
		return ret;
	}

	writel(type | (vc << 6) | (wc_lsbyte << 8) | (wc_msbyte << 16),
	       ctx->base + GEN_HDR);

	return 0;
}

/*
 * Send READ packet to peripheral using the generic interface,
 * this will force command mode and stop video mode (because of BTA).
 *
 * This function has an active delay to wait for the buffer to clear,
 * the delay is limited to 2 x DSIH_FIFO_ACTIVE_WAIT
 * (waiting for command buffer, and waiting for receiving)
 * @note this function will enable BTA
 */
static int sprd_dsi_rd_pkt(struct dsi_context *ctx, u8 vc, u8 type,
			   u8 msb_byte, u8 lsb_byte,
			   u8 *buffer, u8 bytes_to_read)
{
	struct sprd_dsi *dsi = container_of(ctx, struct sprd_dsi, ctx);
	int i, ret;
	int count = 0;
	u32 temp;

	if (vc > 3)
		return -EINVAL;

	/* 1st: send read command to peripheral */
	ret = dsi_reg_rd(ctx, CMD_MODE_STATUS, GEN_CMD_CMD_FIFO_EMPTY, 5);
	if (!ret)
		return -EIO;

	writel(type | (vc << 6) | (lsb_byte << 8) | (msb_byte << 16),
	       ctx->base + GEN_HDR);

	/* 2nd: wait peripheral response completed */
	ret = dsi_wait_rd_resp_completed(ctx);
	if (ret) {
		drm_err(dsi->drm, "wait read response time out\n");
		return ret;
	}

	/* 3rd: get data from rx payload fifo */
	ret = dsi_reg_rd(ctx, CMD_MODE_STATUS, GEN_CMD_RDATA_FIFO_EMPTY, 1);
	if (ret) {
		drm_err(dsi->drm, "rx payload fifo empty\n");
		return -EIO;
	}

	for (i = 0; i < 100; i++) {
		temp = readl(ctx->base + GEN_PLD_DATA);

		if (count < bytes_to_read)
			buffer[count++] = temp & 0xff;
		if (count < bytes_to_read)
			buffer[count++] = (temp >> 8) & 0xff;
		if (count < bytes_to_read)
			buffer[count++] = (temp >> 16) & 0xff;
		if (count < bytes_to_read)
			buffer[count++] = (temp >> 24) & 0xff;

		ret = dsi_reg_rd(ctx, CMD_MODE_STATUS, GEN_CMD_RDATA_FIFO_EMPTY, 1);
		if (ret)
			return count;
	}

	return 0;
}

static void sprd_dsi_set_work_mode(struct dsi_context *ctx, u8 mode)
{
	if (mode == DSI_MODE_CMD)
		writel(1, ctx->base + DSI_MODE_CFG);
	else
		writel(0, ctx->base + DSI_MODE_CFG);
}

static void sprd_dsi_state_reset(struct dsi_context *ctx)
{
	writel(0, ctx->base + SOFT_RESET);
	udelay(100);
	writel(1, ctx->base + SOFT_RESET);
}

static int sprd_dphy_init(struct dsi_context *ctx)
{
	struct sprd_dsi *dsi = container_of(ctx, struct sprd_dsi, ctx);
	int ret;

	dsi_reg_up(ctx, PHY_INTERFACE_CTRL, RF_PHY_RESET_N, 0);
	dsi_reg_up(ctx, PHY_INTERFACE_CTRL, RF_PHY_SHUTDOWN, 0);
	dsi_reg_up(ctx, PHY_INTERFACE_CTRL, RF_PHY_CLK_EN, 0);

	dsi_reg_up(ctx, PHY_TST_CTRL0, PHY_TESTCLR, 0);
	dsi_reg_up(ctx, PHY_TST_CTRL0, PHY_TESTCLR, PHY_TESTCLR);
	dsi_reg_up(ctx, PHY_TST_CTRL0, PHY_TESTCLR, 0);

	dphy_pll_config(ctx);
	dphy_timing_config(ctx);

	dsi_reg_up(ctx, PHY_INTERFACE_CTRL, RF_PHY_SHUTDOWN, RF_PHY_SHUTDOWN);
	dsi_reg_up(ctx, PHY_INTERFACE_CTRL, RF_PHY_RESET_N, RF_PHY_RESET_N);
	writel(0x1C, ctx->base + PHY_MIN_STOP_TIME);
	dsi_reg_up(ctx, PHY_INTERFACE_CTRL, RF_PHY_CLK_EN, RF_PHY_CLK_EN);
	writel(dsi->slave->lanes - 1, ctx->base + PHY_LANE_NUM_CONFIG);

	ret = dphy_wait_pll_locked(ctx);
	if (ret) {
		drm_err(dsi->drm, "dphy initial failed\n");
		return ret;
	}

	return 0;
}

static void sprd_dphy_fini(struct dsi_context *ctx)
{
	dsi_reg_up(ctx, PHY_INTERFACE_CTRL, RF_PHY_RESET_N, 0);
	dsi_reg_up(ctx, PHY_INTERFACE_CTRL, RF_PHY_SHUTDOWN, 0);
	dsi_reg_up(ctx, PHY_INTERFACE_CTRL, RF_PHY_RESET_N, RF_PHY_RESET_N);
}

static void sprd_dsi_encoder_mode_set(struct drm_encoder *encoder,
				      struct drm_display_mode *mode,
				 struct drm_display_mode *adj_mode)
{
	struct sprd_dsi *dsi = encoder_to_dsi(encoder);

	drm_display_mode_to_videomode(adj_mode, &dsi->ctx.vm);
}

static void sprd_dsi_encoder_enable(struct drm_encoder *encoder)
{
	struct sprd_dsi *dsi = encoder_to_dsi(encoder);
	struct sprd_dpu *dpu = to_sprd_crtc(encoder->crtc);
	struct dsi_context *ctx = &dsi->ctx;

	if (ctx->enabled) {
		drm_warn(dsi->drm, "dsi is initialized\n");
		return;
	}

	sprd_dsi_init(ctx);
	if (ctx->work_mode == DSI_MODE_VIDEO)
		sprd_dsi_dpi_video(ctx);
	else
		sprd_dsi_edpi_video(ctx);

	sprd_dphy_init(ctx);

	sprd_dsi_set_work_mode(ctx, ctx->work_mode);
	sprd_dsi_state_reset(ctx);

	if (dsi->slave->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS) {
		dsi_reg_up(ctx, PHY_CLK_LANE_LP_CTRL, AUTO_CLKLANE_CTRL_EN,
			   AUTO_CLKLANE_CTRL_EN);
	} else {
		dsi_reg_up(ctx, PHY_CLK_LANE_LP_CTRL, RF_PHY_CLK_EN, RF_PHY_CLK_EN);
		dsi_reg_up(ctx, PHY_CLK_LANE_LP_CTRL, PHY_CLKLANE_TX_REQ_HS,
			   PHY_CLKLANE_TX_REQ_HS);
		dphy_wait_pll_locked(ctx);
	}

	sprd_dpu_run(dpu);

	ctx->enabled = true;
}

static void sprd_dsi_encoder_disable(struct drm_encoder *encoder)
{
	struct sprd_dsi *dsi = encoder_to_dsi(encoder);
	struct sprd_dpu *dpu = to_sprd_crtc(encoder->crtc);
	struct dsi_context *ctx = &dsi->ctx;

	if (!ctx->enabled) {
		drm_warn(dsi->drm, "dsi isn't initialized\n");
		return;
	}

	sprd_dpu_stop(dpu);
	sprd_dphy_fini(ctx);
	sprd_dsi_fini(ctx);

	ctx->enabled = false;
}

static const struct drm_encoder_helper_funcs sprd_encoder_helper_funcs = {
	.mode_set	= sprd_dsi_encoder_mode_set,
	.enable		= sprd_dsi_encoder_enable,
	.disable	= sprd_dsi_encoder_disable
};

static const struct drm_encoder_funcs sprd_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int sprd_dsi_encoder_init(struct sprd_dsi *dsi,
				 struct device *dev)
{
	struct drm_encoder *encoder = &dsi->encoder;
	u32 crtc_mask;
	int ret;

	crtc_mask = drm_of_find_possible_crtcs(dsi->drm, dev->of_node);
	if (!crtc_mask) {
		drm_err(dsi->drm, "failed to find crtc mask\n");
		return -EINVAL;
	}

	drm_dbg(dsi->drm, "find possible crtcs: 0x%08x\n", crtc_mask);

	encoder->possible_crtcs = crtc_mask;
	ret = drm_encoder_init(dsi->drm, encoder, &sprd_encoder_funcs,
			       DRM_MODE_ENCODER_DSI, NULL);
	if (ret) {
		drm_err(dsi->drm, "failed to init dsi encoder\n");
		return ret;
	}

	drm_encoder_helper_add(encoder, &sprd_encoder_helper_funcs);

	return 0;
}

static int sprd_dsi_bridge_init(struct sprd_dsi *dsi,
				struct device *dev)
{
	int ret;

	dsi->panel_bridge = devm_drm_of_get_bridge(dev, dev->of_node, 1, 0);
	if (IS_ERR(dsi->panel_bridge))
		return PTR_ERR(dsi->panel_bridge);

	ret = drm_bridge_attach(&dsi->encoder, dsi->panel_bridge, NULL, 0);
	if (ret)
		return ret;

	return 0;
}

static int sprd_dsi_context_init(struct sprd_dsi *dsi,
				 struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dsi_context *ctx = &dsi->ctx;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to get I/O resource\n");
		return -EINVAL;
	}

	ctx->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!ctx->base) {
		drm_err(dsi->drm, "failed to map dsi host registers\n");
		return -ENXIO;
	}

	ctx->regmap = devm_regmap_init(dev, &regmap_tst_io, dsi, &byte_config);
	if (IS_ERR(ctx->regmap)) {
		drm_err(dsi->drm, "dphy regmap init failed\n");
		return PTR_ERR(ctx->regmap);
	}

	ctx->data_hs2lp = 120;
	ctx->data_lp2hs = 500;
	ctx->clk_hs2lp = 4;
	ctx->clk_lp2hs = 15;
	ctx->max_rd_time = 6000;
	ctx->int0_mask = 0xffffffff;
	ctx->int1_mask = 0xffffffff;
	ctx->enabled = true;

	return 0;
}

static int sprd_dsi_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = data;
	struct sprd_dsi *dsi = dev_get_drvdata(dev);
	int ret;

	dsi->drm = drm;

	ret = sprd_dsi_encoder_init(dsi, dev);
	if (ret)
		return ret;

	ret = sprd_dsi_bridge_init(dsi, dev);
	if (ret)
		return ret;

	ret = sprd_dsi_context_init(dsi, dev);
	if (ret)
		return ret;

	return 0;
}

static void sprd_dsi_unbind(struct device *dev,
			    struct device *master, void *data)
{
	struct sprd_dsi *dsi = dev_get_drvdata(dev);

	drm_of_panel_bridge_remove(dev->of_node, 1, 0);

	drm_encoder_cleanup(&dsi->encoder);
}

static const struct component_ops dsi_component_ops = {
	.bind	= sprd_dsi_bind,
	.unbind	= sprd_dsi_unbind,
};

static int sprd_dsi_host_attach(struct mipi_dsi_host *host,
				struct mipi_dsi_device *slave)
{
	struct sprd_dsi *dsi = host_to_dsi(host);
	struct dsi_context *ctx = &dsi->ctx;

	dsi->slave = slave;

	if (slave->mode_flags & MIPI_DSI_MODE_VIDEO)
		ctx->work_mode = DSI_MODE_VIDEO;
	else
		ctx->work_mode = DSI_MODE_CMD;

	if (slave->mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
		ctx->burst_mode = VIDEO_BURST_WITH_SYNC_PULSES;
	else if (slave->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
		ctx->burst_mode = VIDEO_NON_BURST_WITH_SYNC_PULSES;
	else
		ctx->burst_mode = VIDEO_NON_BURST_WITH_SYNC_EVENTS;

	return component_add(host->dev, &dsi_component_ops);
}

static int sprd_dsi_host_detach(struct mipi_dsi_host *host,
				struct mipi_dsi_device *slave)
{
	component_del(host->dev, &dsi_component_ops);

	return 0;
}

static ssize_t sprd_dsi_host_transfer(struct mipi_dsi_host *host,
				      const struct mipi_dsi_msg *msg)
{
	struct sprd_dsi *dsi = host_to_dsi(host);
	const u8 *tx_buf = msg->tx_buf;

	if (msg->rx_buf && msg->rx_len) {
		u8 lsb = (msg->tx_len > 0) ? tx_buf[0] : 0;
		u8 msb = (msg->tx_len > 1) ? tx_buf[1] : 0;

		return sprd_dsi_rd_pkt(&dsi->ctx, msg->channel, msg->type,
				msb, lsb, msg->rx_buf, msg->rx_len);
	}

	if (msg->tx_buf && msg->tx_len)
		return sprd_dsi_wr_pkt(&dsi->ctx, msg->channel, msg->type,
					tx_buf, msg->tx_len);

	return 0;
}

static const struct mipi_dsi_host_ops sprd_dsi_host_ops = {
	.attach = sprd_dsi_host_attach,
	.detach = sprd_dsi_host_detach,
	.transfer = sprd_dsi_host_transfer,
};

static const struct of_device_id dsi_match_table[] = {
	{ .compatible = "sprd,sharkl3-dsi-host" },
	{ /* sentinel */ },
};

static int sprd_dsi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sprd_dsi *dsi;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dev_set_drvdata(dev, dsi);

	dsi->host.ops = &sprd_dsi_host_ops;
	dsi->host.dev = dev;

	return mipi_dsi_host_register(&dsi->host);
}

static int sprd_dsi_remove(struct platform_device *pdev)
{
	struct sprd_dsi *dsi = dev_get_drvdata(&pdev->dev);

	mipi_dsi_host_unregister(&dsi->host);

	return 0;
}

struct platform_driver sprd_dsi_driver = {
	.probe = sprd_dsi_probe,
	.remove = sprd_dsi_remove,
	.driver = {
		.name = "sprd-dsi-drv",
		.of_match_table = dsi_match_table,
	},
};

MODULE_AUTHOR("Leon He <leon.he@unisoc.com>");
MODULE_AUTHOR("Kevin Tang <kevin.tang@unisoc.com>");
MODULE_DESCRIPTION("Unisoc MIPI DSI HOST Controller Driver");
MODULE_LICENSE("GPL v2");
