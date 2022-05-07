// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 *
 * refer to bridge/synopsys/dw-mipi-dsi.c
 */

#include <linux/version.h>
#include <linux/component.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <linux/iopoll.h>
#include <linux/phy.h>
#include <linux/phy/phy-mipi-dphy.h>
#include <linux/delay.h>

#include <drm/drm_bridge.h>
#include <drm/drm_encoder.h>
#include <drm/drm_modes.h>
#include <drm/drm_of.h>
#include <drm/drm_mipi_dsi.h>
#if KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE
#include <drm/drm_print.h>
#else
#include <drm/drmP.h>
#endif

#include "dw_mipi_dsi.h"

#define DSI_VERSION			0x00

#define DSI_PWR_UP			0x04
#define RESET				0
#define POWERUP				BIT(0)

#define DSI_CLKMGR_CFG			0x08
#define TO_CLK_DIVISION(div)		(((div) & 0xff) << 8)
#define TX_ESC_CLK_DIVISION(div)	((div) & 0xff)

#define DSI_DPI_VCID			0x0c
#define DPI_VCID(vcid)			((vcid) & 0x3)

#define DSI_DPI_COLOR_CODING		0x10
#define LOOSELY18_EN			BIT(8)
#define DPI_COLOR_CODING_16BIT_1	0x0
#define DPI_COLOR_CODING_16BIT_2	0x1
#define DPI_COLOR_CODING_16BIT_3	0x2
#define DPI_COLOR_CODING_18BIT_1	0x3
#define DPI_COLOR_CODING_18BIT_2	0x4
#define DPI_COLOR_CODING_24BIT		0x5

#define DSI_DPI_CFG_POL			0x14
#define COLORM_ACTIVE_LOW		BIT(4)
#define SHUTD_ACTIVE_LOW		BIT(3)
#define HSYNC_ACTIVE_LOW		BIT(2)
#define VSYNC_ACTIVE_LOW		BIT(1)
#define DATAEN_ACTIVE_LOW		BIT(0)

#define DSI_DPI_LP_CMD_TIM		0x18
#define OUTVACT_LPCMD_TIME(p)		(((p) & 0xff) << 16)
#define INVACT_LPCMD_TIME(p)		((p) & 0xff)

#define DSI_DBI_VCID			0x1c
#define DSI_DBI_CFG				0x20
#define DSI_DBI_PARTITIONING_EN	0x24
#define DSI_DBI_CMDSIZE			0x28

#define DSI_PCKHDL_CFG			0x2c
#define EOTP_TX_LP_EN			BIT(5)
#define CRC_RX_EN			BIT(4)
#define ECC_RX_EN			BIT(3)
#define BTA_EN				BIT(2)
#define EOTP_RX_EN			BIT(1)
#define EOTP_TX_EN			BIT(0)

#define DSI_GEN_VCID			0x30

#define DSI_MODE_CFG			0x34
#define ENABLE_VIDEO_MODE		0
#define ENABLE_CMD_MODE			BIT(0)

#define DSI_VID_MODE_CFG		0x38
#define ENABLE_LOW_POWER		(0x3f << 8)
#define ENABLE_LOW_POWER_MASK		(0x3f << 8)
#define VID_MODE_TYPE_NON_BURST_SYNC_PULSES 0x0
#define VID_MODE_TYPE_NON_BURST_SYNC_EVENTS 0x1
#define VID_MODE_TYPE_BURST			0x2
#define VID_MODE_TYPE_MASK			0x3

#define DSI_VID_PKT_SIZE		0x3c
#define VID_PKT_SIZE(p)			((p) & 0x3fff)

#define DSI_VID_NUM_CHUNKS		0x40
#define VID_NUM_CHUNKS(c)		((c) & 0x1fff)

#define DSI_VID_NULL_SIZE		0x44
#define VID_NULL_SIZE(b)		((b) & 0x1fff)

#define DSI_VID_HSA_TIME		0x48
#define DSI_VID_HBP_TIME		0x4c
#define DSI_VID_HLINE_TIME		0x50
#define DSI_VID_VSA_LINES		0x54
#define DSI_VID_VBP_LINES		0x58
#define DSI_VID_VFP_LINES		0x5c
#define DSI_VID_VACTIVE_LINES		0x60
#define DSI_EDPI_CMD_SIZE		0x64

#define DSI_CMD_MODE_CFG		0x68
#define MAX_RD_PKT_SIZE_LP		BIT(24)
#define DCS_LW_TX_LP			BIT(19)
#define DCS_SR_0P_TX_LP			BIT(18)
#define DCS_SW_1P_TX_LP			BIT(17)
#define DCS_SW_0P_TX_LP			BIT(16)
#define GEN_LW_TX_LP			BIT(14)
#define GEN_SR_2P_TX_LP			BIT(13)
#define GEN_SR_1P_TX_LP			BIT(12)
#define GEN_SR_0P_TX_LP			BIT(11)
#define GEN_SW_2P_TX_LP			BIT(10)
#define GEN_SW_1P_TX_LP			BIT(9)
#define GEN_SW_0P_TX_LP			BIT(8)
#define ACK_RQST_EN				BIT(1)
#define TEAR_FX_EN				BIT(0)

#define CMD_MODE_ALL_LP			(MAX_RD_PKT_SIZE_LP | \
					 DCS_LW_TX_LP | \
					 DCS_SR_0P_TX_LP | \
					 DCS_SW_1P_TX_LP | \
					 DCS_SW_0P_TX_LP | \
					 GEN_LW_TX_LP | \
					 GEN_SR_2P_TX_LP | \
					 GEN_SR_1P_TX_LP | \
					 GEN_SR_0P_TX_LP | \
					 GEN_SW_2P_TX_LP | \
					 GEN_SW_1P_TX_LP | \
					 GEN_SW_0P_TX_LP)

#define DSI_GEN_HDR				0x6c
#define DSI_GEN_PLD_DATA		0x70

#define DSI_CMD_PKT_STATUS		0x74
#define GEN_RD_CMD_BUSY			BIT(6)
#define GEN_PLD_R_FULL			BIT(5)
#define GEN_PLD_R_EMPTY			BIT(4)
#define GEN_PLD_W_FULL			BIT(3)
#define GEN_PLD_W_EMPTY			BIT(2)
#define GEN_CMD_FULL			BIT(1)
#define GEN_CMD_EMPTY			BIT(0)

#define DSI_TO_CNT_CFG			0x78
#define HSTX_TO_CNT(p)			(((p) & 0xffff) << 16)
#define LPRX_TO_CNT(p)			((p) & 0xffff)

#define DSI_HS_RD_TO_CNT		0x7c
#define DSI_LP_RD_TO_CNT		0x80
#define DSI_HS_WR_TO_CNT		0x84
#define DSI_LP_WR_TO_CNT		0x88
#define DSI_BTA_TO_CNT			0x8c

#define DSI_LPCLK_CTRL			0x94
#define AUTO_CLKLANE_CTRL		BIT(1)
#define PHY_TXREQUESTCLKHS		BIT(0)

#define DSI_PHY_TMR_LPCLK_CFG		0x98
#define PHY_CLKHS2LP_TIME(lbcc)		(((lbcc) & 0x3ff) << 16)
#define PHY_CLKLP2HS_TIME(lbcc)		((lbcc) & 0x3ff)

#define DSI_PHY_TMR_CFG				0x9c
#define PHY_HS2LP_TIME(lbcc)		(((lbcc) & 0x3ff) << 16)
#define PHY_LP2HS_TIME(lbcc)		((lbcc) & 0x3ff)

#define DSI_PHY_RSTZ			0xa0
#define PHY_DISFORCEPLL			0
#define PHY_ENFORCEPLL			BIT(3)
#define PHY_DISABLECLK			0
#define PHY_ENABLECLK			BIT(2)
#define PHY_RSTZ				0
#define PHY_UNRSTZ				BIT(1)
#define PHY_SHUTDOWNZ			0
#define PHY_UNSHUTDOWNZ			BIT(0)

#define DSI_PHY_IF_CFG			0xa4
#define PHY_STOP_WAIT_TIME(cycle)	(((cycle) & 0xff) << 8)
#define N_LANES(n)			(((n) - 1) & 0x3)

#define DSI_PHY_ULPS_CTRL		0xa8
#define DSI_PHY_TX_TRIGGERS		0xac

#define DSI_PHY_STATUS			0xb0
#define PHY_STOP_STATE_CLK_LANE	BIT(2)
#define PHY_LOCK			BIT(0)

#define DSI_INT_ST0			0xbc
#define DSI_INT_ST1			0xc0
#define DSI_INT_MSK0			0xc4
#define DSI_INT_MSK1			0xc8

#define DSI_PHY_TMR_RD_CFG		0xf4
#define MAX_RD_TIME(lbcc)		((lbcc) & 0x7fff)

#define PHY_STATUS_TIMEOUT_US		10000
#define CMD_PKT_STATUS_TIMEOUT_US	20000

#define MAX_LANE_COUNT			4

struct dw_mipi_dsi;

struct dw_mipi_dsi_funcs {
	struct dw_mipi_dsi *(*get_dsi)(struct device *dev);
	int (*bind)(struct dw_mipi_dsi *dsi);
	void (*unbind)(struct dw_mipi_dsi *dsi);
};

struct dw_mipi_dsi {
	struct device *dev;
	void __iomem *base;
	struct clk *pclk;
	struct phy *dphy;

	struct mipi_dsi_host host;
	unsigned int channel;
	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	unsigned long mode_flags;

	unsigned int lane_link_rate; /* kHz */

	const struct dw_mipi_dsi_funcs *funcs;
};

struct dw_mipi_dsi_primary {
	struct dw_mipi_dsi dsi;
	struct dw_mipi_dsi *secondary_dsi;

	struct drm_bridge bridge;
	struct drm_bridge *panel_bridge;
	u32 bus_format;
};

static inline struct dw_mipi_dsi *host_to_dsi(struct mipi_dsi_host *host)
{
	return container_of(host, struct dw_mipi_dsi, host);
}

static inline struct
dw_mipi_dsi_primary *dsi_to_primary(struct dw_mipi_dsi *dsi)
{
	return container_of(dsi, struct dw_mipi_dsi_primary, dsi);
}

static inline struct
dw_mipi_dsi_primary *bridge_to_primary(struct drm_bridge *bridge)
{
	return container_of(bridge, struct dw_mipi_dsi_primary, bridge);
}

static inline void dsi_write(struct dw_mipi_dsi *dsi, u32 reg, u32 val)
{
	writel(val, dsi->base + reg);
}

static inline u32 dsi_read(struct dw_mipi_dsi *dsi, u32 reg)
{
	return readl(dsi->base + reg);
}

static int dsi_host_attach(struct mipi_dsi_host *host,
			   struct mipi_dsi_device *device)
{
	struct dw_mipi_dsi *dsi = host_to_dsi(host);

	if (device->lanes > MAX_LANE_COUNT) {
		DRM_ERROR("the number of data lanes(%u) is too many\n",
			  device->lanes);
		return -EINVAL;
	}

	if (!(device->mode_flags & MIPI_DSI_MODE_VIDEO_BURST))
		DRM_WARN("This DSI driver only support burst mode\n");

	dsi->lanes = device->lanes;
	dsi->channel = device->channel;
	dsi->format = device->format;
	dsi->mode_flags = device->mode_flags;

	return 0;
}

static int dsi_host_detach(struct mipi_dsi_host *host,
			   struct mipi_dsi_device *device)
{
	return 0;
}

static void dw_mipi_message_config(struct dw_mipi_dsi *dsi,
				   const struct mipi_dsi_msg *msg)
{
	bool lpm = msg->flags & MIPI_DSI_MSG_USE_LPM;
	u32 val = 0;

	if (msg->flags & MIPI_DSI_MSG_REQ_ACK)
		val |= ACK_RQST_EN;
	if (lpm)
		val |= CMD_MODE_ALL_LP;

	dsi_write(dsi, DSI_LPCLK_CTRL, lpm ? 0 : PHY_TXREQUESTCLKHS);
	dsi_write(dsi, DSI_CMD_MODE_CFG, val);
}

static int dw_mipi_dsi_gen_pkt_hdr_write(struct dw_mipi_dsi *dsi, u32 hdr_val)
{
	int ret;
	u32 val, mask;

	ret = readl_poll_timeout(dsi->base + DSI_CMD_PKT_STATUS,
				 val, !(val & GEN_CMD_FULL), 1000,
				 CMD_PKT_STATUS_TIMEOUT_US);
	if (ret) {
		DRM_ERROR("failed to get available command FIFO\n");
		return ret;
	}

	dsi_write(dsi, DSI_GEN_HDR, hdr_val);

	mask = GEN_CMD_EMPTY | GEN_PLD_W_EMPTY;
	ret = readl_poll_timeout(dsi->base + DSI_CMD_PKT_STATUS,
				 val, (val & mask) == mask,
				 1000, CMD_PKT_STATUS_TIMEOUT_US);
	if (ret) {
		DRM_ERROR("failed to write command FIFO\n");
		return ret;
	}

	return 0;
}

static int dw_mipi_dsi_write(struct dw_mipi_dsi *dsi,
				 const struct mipi_dsi_packet *packet)
{
	const u8 *tx_buf = packet->payload;
	int len = packet->payload_length, pld_data_bytes = sizeof(u32), ret;
	__le32 word;
	u32 val;

	while (len) {
		if (len < pld_data_bytes) {
			word = 0;
			memcpy(&word, tx_buf, len);
			dsi_write(dsi, DSI_GEN_PLD_DATA, le32_to_cpu(word));
			len = 0;
		} else {
			memcpy(&word, tx_buf, pld_data_bytes);
			dsi_write(dsi, DSI_GEN_PLD_DATA, le32_to_cpu(word));
			tx_buf += pld_data_bytes;
			len -= pld_data_bytes;
		}

		ret = readl_poll_timeout(dsi->base + DSI_CMD_PKT_STATUS,
					 val, !(val & GEN_PLD_W_FULL), 1000,
					 CMD_PKT_STATUS_TIMEOUT_US);
		if (ret) {
			DRM_ERROR("failed to get write payload FIFO\n");
			return ret;
		}
	}

	word = 0;
	memcpy(&word, packet->header, sizeof(packet->header));
	return dw_mipi_dsi_gen_pkt_hdr_write(dsi, le32_to_cpu(word));
}

static int dw_mipi_dsi_read(struct dw_mipi_dsi *dsi,
				const struct mipi_dsi_msg *msg)
{
	int i, j, ret, len = msg->rx_len;
	u8 *buf = msg->rx_buf;
	u32 val;

	/* Wait end of the read operation */
	ret = readl_poll_timeout(dsi->base + DSI_CMD_PKT_STATUS,
				 val, !(val & GEN_RD_CMD_BUSY),
				 1000, CMD_PKT_STATUS_TIMEOUT_US);
	if (ret) {
		DRM_ERROR("Timeout during read operation\n");
		return ret;
	}

	for (i = 0; i < len; i += 4) {
		/* Read fifo must not be empty before all bytes are read */
		ret = readl_poll_timeout(dsi->base + DSI_CMD_PKT_STATUS,
					 val, !(val & GEN_PLD_R_EMPTY),
					 1000, CMD_PKT_STATUS_TIMEOUT_US);
		if (ret) {
			DRM_ERROR("Read payload FIFO is empty\n");
			return ret;
		}

		val = dsi_read(dsi, DSI_GEN_PLD_DATA);
		for (j = 0; j < 4 && j + i < len; j++)
			buf[i + j] = val >> (8 * j);
	}

	return ret;
}

static ssize_t dsi_host_transfer(struct mipi_dsi_host *host,
				 const struct mipi_dsi_msg *msg)
{
	struct dw_mipi_dsi *dsi = host_to_dsi(host);
	struct mipi_dsi_packet packet;
	int ret, nb_bytes;

	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret) {
		DRM_ERROR("failed to create packet: %d\n", ret);
		return ret;
	}

	dw_mipi_message_config(dsi, msg);

	ret = dw_mipi_dsi_write(dsi, &packet);
	if (ret)
		return ret;

	if (msg->rx_buf && msg->rx_len) {
		ret = dw_mipi_dsi_read(dsi, msg);
		if (ret)
			return ret;
		nb_bytes = msg->rx_len;
	} else {
		nb_bytes = packet.size;
	}

	return nb_bytes;
}

static const struct mipi_dsi_host_ops dw_mipi_dsi_host_ops = {
	.attach = dsi_host_attach,
	.detach = dsi_host_detach,
	.transfer = dsi_host_transfer,
};

static void dw_mipi_dsi_video_config(struct dw_mipi_dsi *dsi)
{
	dsi_write(dsi, DSI_VID_MODE_CFG,
				ENABLE_LOW_POWER | VID_MODE_TYPE_BURST);
}

static void dw_mipi_dsi_set_mode(struct dw_mipi_dsi *dsi,
				 unsigned long mode_flags)
{
	dsi_write(dsi, DSI_PWR_UP, RESET);

	if (mode_flags & MIPI_DSI_MODE_VIDEO) {
		dsi_write(dsi, DSI_MODE_CFG, ENABLE_VIDEO_MODE);
		dsi_write(dsi, DSI_LPCLK_CTRL, PHY_TXREQUESTCLKHS);
	} else {
		dsi_write(dsi, DSI_MODE_CFG, ENABLE_CMD_MODE);
	}

	dsi_write(dsi, DSI_PWR_UP, POWERUP);
}

static void dw_mipi_dsi_init(struct dw_mipi_dsi *dsi)
{
	u8 esc_clk;
	u32 esc_clk_division;

	/* limit esc clk on FPGA */
	if (!dsi->pclk)
		esc_clk = 5; /* 5MHz */
	else
		esc_clk = 20; /* 20MHz */

	/*
	 * The maximum permitted escape clock is 20MHz and it is derived from
	 * lanebyteclk, which is running at "lane_link_rate / 8".  Thus we want:
	 *
	 *	   (lane_link_rate >> 3) / esc_clk_division < 20
	 * which is:
	 *	   (lane_link_rate >> 3) / 20 > esc_clk_division
	 */
	esc_clk_division = ((dsi->lane_link_rate / 1000) >> 3) / esc_clk + 1;

	dsi_write(dsi, DSI_PWR_UP, RESET);

	/*
	 * TODO dw drv improvements
	 * timeout clock division should be computed with the
	 * high speed transmission counter timeout and byte lane...
	 */
	dsi_write(dsi, DSI_CLKMGR_CFG, TO_CLK_DIVISION(10) |
			TX_ESC_CLK_DIVISION(esc_clk_division));
}

static void dw_mipi_dsi_dpi_config(struct dw_mipi_dsi *dsi,
				   const struct drm_display_mode *mode)
{
	u32 val = 0, color = 0;

	switch (dsi->format) {
	case MIPI_DSI_FMT_RGB888:
		color = DPI_COLOR_CODING_24BIT;
		break;
	case MIPI_DSI_FMT_RGB666:
		color = DPI_COLOR_CODING_18BIT_2 | LOOSELY18_EN;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		color = DPI_COLOR_CODING_18BIT_1;
		break;
	case MIPI_DSI_FMT_RGB565:
		color = DPI_COLOR_CODING_16BIT_1;
		break;
	}

	if (!(mode->flags & DRM_MODE_FLAG_PVSYNC))
		val |= VSYNC_ACTIVE_LOW;
	if (!(mode->flags & DRM_MODE_FLAG_PHSYNC))
		val |= HSYNC_ACTIVE_LOW;

	dsi_write(dsi, DSI_DPI_VCID, DPI_VCID(dsi->channel));
	dsi_write(dsi, DSI_DPI_COLOR_CODING, color);
	dsi_write(dsi, DSI_DPI_CFG_POL, val);
	/*
	 * TODO dw drv improvements
	 * largest packet sizes during hfp or during vsa/vpb/vfp
	 * should be computed according to byte lane, lane number and only
	 * if sending lp cmds in high speed is enable (PHY_TXREQUESTCLKHS)
	 */
	dsi_write(dsi, DSI_DPI_LP_CMD_TIM, OUTVACT_LPCMD_TIME(4)
		  | INVACT_LPCMD_TIME(4));
}

static void dw_mipi_dsi_packet_handler_config(struct dw_mipi_dsi *dsi)
{
	dsi_write(dsi, DSI_PCKHDL_CFG, CRC_RX_EN | ECC_RX_EN | BTA_EN);
}

static void dw_mipi_dsi_command_mode_config(struct dw_mipi_dsi *dsi)
{

	/*
	 * TODO dw drv improvements
	 * compute high speed transmission counter timeout according
	 * to the timeout clock division (TO_CLK_DIVISION) and byte lane...
	 */
	dsi_write(dsi, DSI_TO_CNT_CFG,
			HSTX_TO_CNT(1000) | LPRX_TO_CNT(1000));
	/*
	 * TODO dw drv improvements
	 * the Bus-Turn-Around Timeout Counter should be computed
	 * according to byte lane...
	 */
	dsi_write(dsi, DSI_BTA_TO_CNT, 0xd00);

}

/* Get lane byte clock cycles. */
static u32 dw_mipi_dsi_get_hcomponent_lbcc(struct dw_mipi_dsi *dsi,
					   const struct drm_display_mode *mode,
					   u32 hcomponent)
{
	u32 frac, lbcc;

	lbcc = hcomponent * dsi->lane_link_rate / 8;

	frac = lbcc % mode->clock;
	lbcc = lbcc / mode->clock;
	if (frac)
		lbcc++;

	return lbcc;
}

static void dw_mipi_dsi_timing_config(struct dw_mipi_dsi *dsi,
					  const struct drm_display_mode *mode)
{
	u32 htotal, hsa, hbp, lbcc;

	htotal = mode->htotal;
	hsa = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;

	lbcc = dw_mipi_dsi_get_hcomponent_lbcc(dsi, mode, htotal);
	dsi_write(dsi, DSI_VID_HLINE_TIME, lbcc);

	lbcc = dw_mipi_dsi_get_hcomponent_lbcc(dsi, mode, hsa);
	dsi_write(dsi, DSI_VID_HSA_TIME, lbcc);

	lbcc = dw_mipi_dsi_get_hcomponent_lbcc(dsi, mode, hbp);
	dsi_write(dsi, DSI_VID_HBP_TIME, lbcc);

	dsi_write(dsi, DSI_VID_PKT_SIZE, VID_PKT_SIZE(mode->hdisplay));

	/* vertical */
	dsi_write(dsi, DSI_VID_VACTIVE_LINES, mode->vdisplay);
	dsi_write(dsi, DSI_VID_VSA_LINES, mode->vsync_end - mode->vsync_start);
	dsi_write(dsi, DSI_VID_VFP_LINES, mode->vsync_start - mode->vdisplay);
	dsi_write(dsi, DSI_VID_VBP_LINES, mode->vtotal - mode->vsync_end);
}

static void dw_mipi_dsi_dphy_timing_config(struct dw_mipi_dsi *dsi)
{
	/*
	 * TODO dw drv improvements
	 * data & clock lane timers should be computed according to panel
	 * blankings and to the automatic clock lane control mode...
	 * note: DSI_PHY_TMR_CFG.MAX_RD_TIME should be in line with
	 * DSI_CMD_MODE_CFG.MAX_RD_PKT_SIZE_LP (see CMD_MODE_ALL_LP)
	 */

	dsi_write(dsi, DSI_PHY_TMR_CFG, PHY_HS2LP_TIME(0x40) |
			PHY_LP2HS_TIME(0x40));
	dsi_write(dsi, DSI_PHY_TMR_RD_CFG, MAX_RD_TIME(10000));

	dsi_write(dsi, DSI_PHY_TMR_LPCLK_CFG, PHY_CLKHS2LP_TIME(0x40)
		  | PHY_CLKLP2HS_TIME(0x40));
}

static void dw_mipi_dsi_dphy_interface_config(struct dw_mipi_dsi *dsi)
{
	/*
	 * TODO dw drv improvements
	 * stop wait time should be the maximum between host dsi
	 * and panel stop wait times
	 */
	dsi_write(dsi, DSI_PHY_IF_CFG, PHY_STOP_WAIT_TIME(0x20) |
		  N_LANES(dsi->lanes));
}

static void dw_mipi_dsi_dphy_init(struct dw_mipi_dsi *dsi)
{
	/* Clear PHY state */
	dsi_write(dsi, DSI_PHY_RSTZ, PHY_DISFORCEPLL | PHY_DISABLECLK
		  | PHY_RSTZ | PHY_SHUTDOWNZ);
}

static void dw_mipi_dsi_dphy_enable(struct dw_mipi_dsi *dsi)
{
	u32 val;
	int ret;

	phy_power_on(dsi->dphy);
	dsi_write(dsi, DSI_PHY_RSTZ, PHY_ENFORCEPLL | PHY_ENABLECLK |
		  PHY_UNSHUTDOWNZ);
	usleep_range(500, 600);
	dsi_write(dsi, DSI_PHY_RSTZ, PHY_ENFORCEPLL | PHY_ENABLECLK |
		  PHY_UNRSTZ | PHY_UNSHUTDOWNZ);

	ret = readl_poll_timeout(dsi->base + DSI_PHY_STATUS, val,
				 val & PHY_LOCK, 1000, PHY_STATUS_TIMEOUT_US);
	if (ret)
		DRM_DEBUG_DRIVER("failed to wait phy lock state\n");

	ret = readl_poll_timeout(dsi->base + DSI_PHY_STATUS,
				 val, val & PHY_STOP_STATE_CLK_LANE, 1000,
				 PHY_STATUS_TIMEOUT_US);
	if (ret)
		DRM_DEBUG_DRIVER("failed to wait phy clk lane stop state\n");
}

/*
 * The controller should generate 2 frames before
 * preparing the peripheral.
 */
static void dw_mipi_dsi_wait_for_two_frames(const struct drm_display_mode *mode)
{
	int refresh, two_frames;

	refresh = drm_mode_vrefresh(mode);
	two_frames = DIV_ROUND_UP(MSEC_PER_SEC, refresh) * 2;
	msleep(two_frames);
}

static void dw_mipi_dsi_mode_set(struct dw_mipi_dsi *dsi,
				 const struct drm_display_mode *mode)
{
	struct phy_configure_opts_mipi_dphy dphy_cfg;
	int bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);

	phy_init(dsi->dphy);
	phy_mipi_dphy_get_default_config(mode->clock * 1000, bpp,
					 dsi->lanes, &dphy_cfg);
	phy_validate(dsi->dphy, PHY_MODE_MIPI_DPHY, 0,
			(union phy_configure_opts *)&dphy_cfg);
	phy_configure(dsi->dphy, (union phy_configure_opts *)&dphy_cfg);

	dsi->lane_link_rate = dphy_cfg.hs_clk_rate / 1000;

	clk_prepare_enable(dsi->pclk);
	pm_runtime_get_sync(dsi->dev);

	dw_mipi_dsi_init(dsi);
	dw_mipi_dsi_dpi_config(dsi, mode);
	dw_mipi_dsi_packet_handler_config(dsi);
	dw_mipi_dsi_video_config(dsi);
	dw_mipi_dsi_command_mode_config(dsi);
	dw_mipi_dsi_timing_config(dsi, mode);

	dw_mipi_dsi_dphy_init(dsi);
	dw_mipi_dsi_dphy_timing_config(dsi);
	dw_mipi_dsi_dphy_interface_config(dsi);

	dw_mipi_dsi_dphy_enable(dsi);

	dw_mipi_dsi_wait_for_two_frames(mode);

	/* Switch to cmd mode for panel-bridge pre_enable & panel prepare */
	dw_mipi_dsi_set_mode(dsi, 0);
}

static void bridge_mode_set(struct drm_bridge *bridge,
				const struct drm_display_mode *mode,
				const struct drm_display_mode *adjusted_mode)
{
	struct dw_mipi_dsi_primary *primary = bridge_to_primary(bridge);
	struct drm_display_mode *new_mode =
				drm_mode_duplicate(bridge->dev, adjusted_mode);

	if (primary->secondary_dsi) {
		new_mode->hdisplay /= 2;
		new_mode->hsync_start /= 2;
		new_mode->hsync_end /= 2;
		new_mode->htotal /= 2;
		new_mode->clock /= 2;
	}

	dw_mipi_dsi_mode_set(&primary->dsi, new_mode);
	if (primary->secondary_dsi)
		dw_mipi_dsi_mode_set(primary->secondary_dsi, new_mode);

	drm_mode_destroy(bridge->dev, new_mode);
}

static void bridge_enable(struct drm_bridge *bridge)
{
	struct dw_mipi_dsi_primary *primary = bridge_to_primary(bridge);

	dw_mipi_dsi_set_mode(&primary->dsi, MIPI_DSI_MODE_VIDEO);
	if (primary->secondary_dsi)
		dw_mipi_dsi_set_mode(primary->secondary_dsi,
					 MIPI_DSI_MODE_VIDEO);
}

static void dw_mipi_dsi_disable(struct dw_mipi_dsi *dsi)
{
	phy_power_off(dsi->dphy);

	dsi_write(dsi, DSI_PWR_UP, RESET);
	dsi_write(dsi, DSI_PHY_RSTZ, PHY_RSTZ);

	pm_runtime_put(dsi->dev);
	clk_disable_unprepare(dsi->pclk);
}

static void bridge_post_disable(struct drm_bridge *bridge)
{
	struct dw_mipi_dsi_primary *primary = bridge_to_primary(bridge);

	/*
	 * Switch to command mode before panel-bridge post_disable &
	 * panel unprepare.
	 * Note: panel-bridge disable & panel disable has been called
	 * before by the drm framework.
	 */
	dw_mipi_dsi_set_mode(&primary->dsi, 0);

	/*
	 * TODO Only way found to call panel-bridge post_disable &
	 * panel unprepare before the dsi "final" disable...
	 * This needs to be fixed in the drm_bridge framework and the API
	 * needs to be updated to manage our own call chains...
	 */
	primary->panel_bridge->funcs->post_disable(primary->panel_bridge);

	if (primary->secondary_dsi)
		dw_mipi_dsi_disable(primary->secondary_dsi);

	dw_mipi_dsi_disable(&primary->dsi);
}

#if KERNEL_VERSION(5, 7, 0) <= LINUX_VERSION_CODE
static int bridge_attach(struct drm_bridge *bridge,
						 enum drm_bridge_attach_flags flags)
#else
static int bridge_attach(struct drm_bridge *bridge)
#endif
{
	struct dw_mipi_dsi_primary *primary = bridge_to_primary(bridge);

	if (!bridge->encoder) {
		DRM_ERROR("Parent encoder object not found\n");
		return -ENODEV;
	}

	/* Attach the panel-bridge to the dsi bridge */
#if KERNEL_VERSION(5, 7, 0) <= LINUX_VERSION_CODE
	return drm_bridge_attach(bridge->encoder, primary->panel_bridge,
				 bridge, 0);
#else
	return drm_bridge_attach(bridge->encoder, primary->panel_bridge,
				 bridge);
#endif
}

static void bridge_update_bus_format(struct drm_bridge *bridge)
{
	struct dw_mipi_dsi_primary *primary = bridge_to_primary(bridge);
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct drm_encoder *encoder;
	u32 bus_format;
#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
	int i;
#endif

	/* set bus format according to DSI format */
	switch (primary->dsi.format) {
	case MIPI_DSI_FMT_RGB888:
		bus_format = MEDIA_BUS_FMT_RBG888_1X24;
		break;
	case MIPI_DSI_FMT_RGB666:
		bus_format = MEDIA_BUS_FMT_RGB666_1X24_CPADHI;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		bus_format = MEDIA_BUS_FMT_RGB666_1X18;
		break;
	case MIPI_DSI_FMT_RGB565:
		bus_format = MEDIA_BUS_FMT_RGB565_1X16;
		break;
	default:
		bus_format = MEDIA_BUS_FMT_RBG888_1X24;
	}

	if (bus_format == primary->bus_format)
		return;

	drm_connector_list_iter_begin(bridge->dev, &conn_iter);

	drm_for_each_connector_iter(connector, &conn_iter) {
#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
		drm_connector_for_each_possible_encoder(connector, encoder, i) {
#else
		drm_connector_for_each_possible_encoder(connector, encoder) {
#endif
			if (encoder == bridge->encoder) {
				drm_display_info_set_bus_formats(
						&connector->display_info,
						&bus_format, 1);
				primary->bus_format = bus_format;
				drm_connector_list_iter_end(&conn_iter);
				return;
			}
		}
	}
	drm_connector_list_iter_end(&conn_iter);
}

static bool bridge_mode_fixup(struct drm_bridge *bridge,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	bridge_update_bus_format(bridge);
	return true;
}

static const struct drm_bridge_funcs dw_mipi_dsi_bridge_funcs = {
	.mode_set	= bridge_mode_set,
	.enable		= bridge_enable,
	.post_disable	= bridge_post_disable,
	.attach		= bridge_attach,
	.mode_fixup = bridge_mode_fixup,
};

static int dsi_attach_primary(struct dw_mipi_dsi *secondary, struct device *dev)
{
	struct dw_mipi_dsi *dsi = dev_get_drvdata(dev);
	struct dw_mipi_dsi_primary *primary = dsi_to_primary(dsi);

	if (!of_device_is_compatible(dev->of_node, "verisilicon,dw-mipi-dsi"))
		return -EINVAL;

	primary->secondary_dsi = secondary;
	return 0;
}

struct dw_mipi_dsi *get_primary_dsi(struct device *dev)
{
	struct dw_mipi_dsi_primary *primary;

	primary = devm_kzalloc(dev, sizeof(*primary), GFP_KERNEL);
	if (!primary)
		return NULL;

	primary->dsi.dev = dev;

	return &primary->dsi;
}

static int primary_bind(struct dw_mipi_dsi *dsi)
{
	struct dw_mipi_dsi_primary *primary = dsi_to_primary(dsi);
	struct device *dev = primary->dsi.dev;
	struct drm_bridge *bridge;
	struct drm_panel *panel;
	int ret;

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, 0, &panel, &bridge);
	if (ret)
		return ret;

	if (panel) {
		bridge = drm_panel_bridge_add_typed(panel,
							DRM_MODE_CONNECTOR_DSI);
		if (IS_ERR(bridge))
			return PTR_ERR(bridge);
	}

	primary->panel_bridge = bridge;

	primary->bridge.funcs = &dw_mipi_dsi_bridge_funcs;
	primary->bridge.of_node = dev->of_node;
	drm_bridge_add(&primary->bridge);
	return 0;
}

static void primary_unbind(struct dw_mipi_dsi *dsi)
{
	struct dw_mipi_dsi_primary *primary = dsi_to_primary(dsi);
	struct device *dev = primary->dsi.dev;

	drm_bridge_remove(&primary->bridge);
	drm_of_panel_bridge_remove(dev->of_node, 1, 0);
}

static const struct dw_mipi_dsi_funcs primary = {
	.get_dsi = &get_primary_dsi,
	.bind = &primary_bind,
	.unbind = &primary_unbind,
};

struct dw_mipi_dsi *get_secondary_dsi(struct device *dev)
{
	struct dw_mipi_dsi *dsi;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return NULL;

	dsi->dev = dev;
	return dsi;
}

static int secondary_bind(struct dw_mipi_dsi *dsi)
{
	struct device_node *np;
	struct platform_device *pdev;

	np = of_find_compatible_node(NULL, NULL, "verisilicon,dw-mipi-dsi");
	if (!np)
		return -ENODEV;

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return -ENODEV;

	return dsi_attach_primary(dsi, &pdev->dev);
}

static void secondary_unbind(struct dw_mipi_dsi *dsi)
{

}

static const struct dw_mipi_dsi_funcs secondary = {
	.get_dsi = &get_secondary_dsi,
	.bind = &secondary_bind,
	.unbind = &secondary_unbind,
};

static int dsi_bind(struct device *dev, struct device *primary, void *data)
{
	struct dw_mipi_dsi *dsi = dev_get_drvdata(dev);

	return dsi->funcs->bind(dsi);
}

static void dsi_unbind(struct device *dev, struct device *primary, void *data)
{
	struct dw_mipi_dsi *dsi = dev_get_drvdata(dev);

	dsi->funcs->unbind(dsi);
}

static const struct component_ops dsi_component_ops = {
	.bind = dsi_bind,
	.unbind = dsi_unbind,
};

static const struct of_device_id dw_mipi_dsi_dt_match[] = {
	{ .compatible = "verisilicon,dw-mipi-dsi", .data = &primary},
	{ .compatible = "verisilicon,dw-mipi-dsi-2nd", .data = &secondary},
	{},
};
MODULE_DEVICE_TABLE(of, dw_mipi_dsi_dt_match);

static int dsi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct dw_mipi_dsi_funcs *funcs;
	struct dw_mipi_dsi *dsi;
	struct resource *res;
	int ret;

	funcs = of_device_get_match_data(dev);
	dsi = funcs->get_dsi(dev);
	if (!dsi)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dsi->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(dsi->base))
		return -ENODEV;

	dsi->pclk = devm_clk_get_optional(dev, "pclk");
	if (IS_ERR(dsi->pclk))
		return PTR_ERR(dsi->pclk);

	dsi->dphy = devm_phy_get(dev, "dphy");
	if (IS_ERR(dsi->dphy))
		return PTR_ERR(dsi->dphy);

	dsi->host.ops = &dw_mipi_dsi_host_ops;
	dsi->host.dev = dev;
	ret = mipi_dsi_host_register(&dsi->host);
	if (ret)
		return ret;

	dsi->funcs = funcs;
	dev_set_drvdata(dev, dsi);

	pm_runtime_enable(dev);

	return component_add(dev, &dsi_component_ops);
}

static int dsi_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_mipi_dsi *dsi = dev_get_drvdata(dev);

	mipi_dsi_host_unregister(&dsi->host);

	pm_runtime_disable(dev);

	component_del(dev, &dsi_component_ops);

	dev_set_drvdata(dev, NULL);

	return 0;
}

struct platform_driver dw_mipi_dsi_driver = {
	.probe = dsi_probe,
	.remove = dsi_remove,
	.driver = {
		.name = "dw-mipi-dsi",
		.of_match_table = of_match_ptr(dw_mipi_dsi_dt_match),
	},
};

MODULE_DESCRIPTION("DW MIPI DSI Controller Driver");
MODULE_LICENSE("GPL v2");
