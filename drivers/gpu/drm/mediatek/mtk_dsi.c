// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <video/mipi_display.h>
#include <video/videomode.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "mtk_disp_drv.h"
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_drv.h"

#define DSI_START		0x00

#define DSI_INTEN		0x08

#define DSI_INTSTA		0x0c
#define LPRX_RD_RDY_INT_FLAG		BIT(0)
#define CMD_DONE_INT_FLAG		BIT(1)
#define TE_RDY_INT_FLAG			BIT(2)
#define VM_DONE_INT_FLAG		BIT(3)
#define EXT_TE_RDY_INT_FLAG		BIT(4)
#define DSI_BUSY			BIT(31)

#define DSI_CON_CTRL		0x10
#define DSI_RESET			BIT(0)
#define DSI_EN				BIT(1)
#define DPHY_RESET			BIT(2)

#define DSI_MODE_CTRL		0x14
#define MODE				(3)
#define CMD_MODE			0
#define SYNC_PULSE_MODE			1
#define SYNC_EVENT_MODE			2
#define BURST_MODE			3
#define FRM_MODE			BIT(16)
#define MIX_MODE			BIT(17)

#define DSI_TXRX_CTRL		0x18
#define VC_NUM				BIT(1)
#define LANE_NUM			(0xf << 2)
#define DIS_EOT				BIT(6)
#define NULL_EN				BIT(7)
#define TE_FREERUN			BIT(8)
#define EXT_TE_EN			BIT(9)
#define EXT_TE_EDGE			BIT(10)
#define MAX_RTN_SIZE			(0xf << 12)
#define HSTX_CKLP_EN			BIT(16)

#define DSI_PSCTRL		0x1c
#define DSI_PS_WC			0x3fff
#define DSI_PS_SEL			(3 << 16)
#define PACKED_PS_16BIT_RGB565		(0 << 16)
#define LOOSELY_PS_18BIT_RGB666		(1 << 16)
#define PACKED_PS_18BIT_RGB666		(2 << 16)
#define PACKED_PS_24BIT_RGB888		(3 << 16)

#define DSI_VSA_NL		0x20
#define DSI_VBP_NL		0x24
#define DSI_VFP_NL		0x28
#define DSI_VACT_NL		0x2C
#define DSI_SIZE_CON		0x38
#define DSI_HSA_WC		0x50
#define DSI_HBP_WC		0x54
#define DSI_HFP_WC		0x58

#define DSI_CMDQ_SIZE		0x60
#define CMDQ_SIZE			0x3f
#define CMDQ_SIZE_SEL		BIT(15)

#define DSI_HSTX_CKL_WC		0x64

#define DSI_RX_DATA0		0x74
#define DSI_RX_DATA1		0x78
#define DSI_RX_DATA2		0x7c
#define DSI_RX_DATA3		0x80

#define DSI_RACK		0x84
#define RACK				BIT(0)

#define DSI_PHY_LCCON		0x104
#define LC_HS_TX_EN			BIT(0)
#define LC_ULPM_EN			BIT(1)
#define LC_WAKEUP_EN			BIT(2)

#define DSI_PHY_LD0CON		0x108
#define LD0_HS_TX_EN			BIT(0)
#define LD0_ULPM_EN			BIT(1)
#define LD0_WAKEUP_EN			BIT(2)

#define DSI_PHY_TIMECON0	0x110
#define LPX				(0xff << 0)
#define HS_PREP				(0xff << 8)
#define HS_ZERO				(0xff << 16)
#define HS_TRAIL			(0xff << 24)

#define DSI_PHY_TIMECON1	0x114
#define TA_GO				(0xff << 0)
#define TA_SURE				(0xff << 8)
#define TA_GET				(0xff << 16)
#define DA_HS_EXIT			(0xff << 24)

#define DSI_PHY_TIMECON2	0x118
#define CONT_DET			(0xff << 0)
#define CLK_ZERO			(0xff << 16)
#define CLK_TRAIL			(0xff << 24)

#define DSI_PHY_TIMECON3	0x11c
#define CLK_HS_PREP			(0xff << 0)
#define CLK_HS_POST			(0xff << 8)
#define CLK_HS_EXIT			(0xff << 16)

#define DSI_VM_CMD_CON		0x130
#define VM_CMD_EN			BIT(0)
#define TS_VFP_EN			BIT(5)

#define DSI_SHADOW_DEBUG	0x190U
#define FORCE_COMMIT			BIT(0)
#define BYPASS_SHADOW			BIT(1)

#define CONFIG				(0xff << 0)
#define SHORT_PACKET			0
#define LONG_PACKET			2
#define BTA				BIT(2)
#define DATA_ID				(0xff << 8)
#define DATA_0				(0xff << 16)
#define DATA_1				(0xff << 24)

#define NS_TO_CYCLE(n, c)    ((n) / (c) + (((n) % (c)) ? 1 : 0))

#define MTK_DSI_HOST_IS_READ(type) \
	((type == MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM) || \
	(type == MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM) || \
	(type == MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM) || \
	(type == MIPI_DSI_DCS_READ))

struct mtk_phy_timing {
	u32 lpx;
	u32 da_hs_prepare;
	u32 da_hs_zero;
	u32 da_hs_trail;

	u32 ta_go;
	u32 ta_sure;
	u32 ta_get;
	u32 da_hs_exit;

	u32 clk_hs_zero;
	u32 clk_hs_trail;

	u32 clk_hs_prepare;
	u32 clk_hs_post;
	u32 clk_hs_exit;
};

struct phy;

struct mtk_dsi_driver_data {
	const u32 reg_cmdq_off;
	bool has_shadow_ctl;
	bool has_size_ctl;
	bool cmdq_long_packet_ctl;
};

struct mtk_dsi {
	struct device *dev;
	struct mipi_dsi_host host;
	struct drm_encoder encoder;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_connector *connector;
	struct phy *phy;

	void __iomem *regs;

	struct clk *engine_clk;
	struct clk *digital_clk;
	struct clk *hs_clk;

	u32 data_rate;

	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	unsigned int lanes;
	struct videomode vm;
	struct mtk_phy_timing phy_timing;
	int refcount;
	bool enabled;
	bool lanes_ready;
	u32 irq_data;
	wait_queue_head_t irq_wait_queue;
	const struct mtk_dsi_driver_data *driver_data;
};

static inline struct mtk_dsi *bridge_to_dsi(struct drm_bridge *b)
{
	return container_of(b, struct mtk_dsi, bridge);
}

static inline struct mtk_dsi *host_to_dsi(struct mipi_dsi_host *h)
{
	return container_of(h, struct mtk_dsi, host);
}

static void mtk_dsi_mask(struct mtk_dsi *dsi, u32 offset, u32 mask, u32 data)
{
	u32 temp = readl(dsi->regs + offset);

	writel((temp & ~mask) | (data & mask), dsi->regs + offset);
}

static void mtk_dsi_phy_timconfig(struct mtk_dsi *dsi)
{
	u32 timcon0, timcon1, timcon2, timcon3;
	u32 data_rate_mhz = DIV_ROUND_UP(dsi->data_rate, 1000000);
	struct mtk_phy_timing *timing = &dsi->phy_timing;

	timing->lpx = (60 * data_rate_mhz / (8 * 1000)) + 1;
	timing->da_hs_prepare = (80 * data_rate_mhz + 4 * 1000) / 8000;
	timing->da_hs_zero = (170 * data_rate_mhz + 10 * 1000) / 8000 + 1 -
			     timing->da_hs_prepare;
	timing->da_hs_trail = timing->da_hs_prepare + 1;

	timing->ta_go = 4 * timing->lpx - 2;
	timing->ta_sure = timing->lpx + 2;
	timing->ta_get = 4 * timing->lpx;
	timing->da_hs_exit = 2 * timing->lpx + 1;

	timing->clk_hs_prepare = 70 * data_rate_mhz / (8 * 1000);
	timing->clk_hs_post = timing->clk_hs_prepare + 8;
	timing->clk_hs_trail = timing->clk_hs_prepare;
	timing->clk_hs_zero = timing->clk_hs_trail * 4;
	timing->clk_hs_exit = 2 * timing->clk_hs_trail;

	timcon0 = timing->lpx | timing->da_hs_prepare << 8 |
		  timing->da_hs_zero << 16 | timing->da_hs_trail << 24;
	timcon1 = timing->ta_go | timing->ta_sure << 8 |
		  timing->ta_get << 16 | timing->da_hs_exit << 24;
	timcon2 = 1 << 8 | timing->clk_hs_zero << 16 |
		  timing->clk_hs_trail << 24;
	timcon3 = timing->clk_hs_prepare | timing->clk_hs_post << 8 |
		  timing->clk_hs_exit << 16;

	writel(timcon0, dsi->regs + DSI_PHY_TIMECON0);
	writel(timcon1, dsi->regs + DSI_PHY_TIMECON1);
	writel(timcon2, dsi->regs + DSI_PHY_TIMECON2);
	writel(timcon3, dsi->regs + DSI_PHY_TIMECON3);
}

static void mtk_dsi_enable(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_CON_CTRL, DSI_EN, DSI_EN);
}

static void mtk_dsi_disable(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_CON_CTRL, DSI_EN, 0);
}

static void mtk_dsi_reset_engine(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_CON_CTRL, DSI_RESET, DSI_RESET);
	mtk_dsi_mask(dsi, DSI_CON_CTRL, DSI_RESET, 0);
}

static void mtk_dsi_reset_dphy(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_CON_CTRL, DPHY_RESET, DPHY_RESET);
	mtk_dsi_mask(dsi, DSI_CON_CTRL, DPHY_RESET, 0);
}

static void mtk_dsi_clk_ulp_mode_enter(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_PHY_LCCON, LC_HS_TX_EN, 0);
	mtk_dsi_mask(dsi, DSI_PHY_LCCON, LC_ULPM_EN, 0);
}

static void mtk_dsi_clk_ulp_mode_leave(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_PHY_LCCON, LC_ULPM_EN, 0);
	mtk_dsi_mask(dsi, DSI_PHY_LCCON, LC_WAKEUP_EN, LC_WAKEUP_EN);
	mtk_dsi_mask(dsi, DSI_PHY_LCCON, LC_WAKEUP_EN, 0);
}

static void mtk_dsi_lane0_ulp_mode_enter(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_PHY_LD0CON, LD0_HS_TX_EN, 0);
	mtk_dsi_mask(dsi, DSI_PHY_LD0CON, LD0_ULPM_EN, 0);
}

static void mtk_dsi_lane0_ulp_mode_leave(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_PHY_LD0CON, LD0_ULPM_EN, 0);
	mtk_dsi_mask(dsi, DSI_PHY_LD0CON, LD0_WAKEUP_EN, LD0_WAKEUP_EN);
	mtk_dsi_mask(dsi, DSI_PHY_LD0CON, LD0_WAKEUP_EN, 0);
}

static bool mtk_dsi_clk_hs_state(struct mtk_dsi *dsi)
{
	return readl(dsi->regs + DSI_PHY_LCCON) & LC_HS_TX_EN;
}

static void mtk_dsi_clk_hs_mode(struct mtk_dsi *dsi, bool enter)
{
	if (enter && !mtk_dsi_clk_hs_state(dsi))
		mtk_dsi_mask(dsi, DSI_PHY_LCCON, LC_HS_TX_EN, LC_HS_TX_EN);
	else if (!enter && mtk_dsi_clk_hs_state(dsi))
		mtk_dsi_mask(dsi, DSI_PHY_LCCON, LC_HS_TX_EN, 0);
}

static void mtk_dsi_set_mode(struct mtk_dsi *dsi)
{
	u32 vid_mode = CMD_MODE;

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO) {
		if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
			vid_mode = BURST_MODE;
		else if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
			vid_mode = SYNC_PULSE_MODE;
		else
			vid_mode = SYNC_EVENT_MODE;
	}

	writel(vid_mode, dsi->regs + DSI_MODE_CTRL);
}

static void mtk_dsi_set_vm_cmd(struct mtk_dsi *dsi)
{
	mtk_dsi_mask(dsi, DSI_VM_CMD_CON, VM_CMD_EN, VM_CMD_EN);
	mtk_dsi_mask(dsi, DSI_VM_CMD_CON, TS_VFP_EN, TS_VFP_EN);
}

static void mtk_dsi_ps_control_vact(struct mtk_dsi *dsi)
{
	struct videomode *vm = &dsi->vm;
	u32 dsi_buf_bpp, ps_wc;
	u32 ps_bpp_mode;

	if (dsi->format == MIPI_DSI_FMT_RGB565)
		dsi_buf_bpp = 2;
	else
		dsi_buf_bpp = 3;

	ps_wc = vm->hactive * dsi_buf_bpp;
	ps_bpp_mode = ps_wc;

	switch (dsi->format) {
	case MIPI_DSI_FMT_RGB888:
		ps_bpp_mode |= PACKED_PS_24BIT_RGB888;
		break;
	case MIPI_DSI_FMT_RGB666:
		ps_bpp_mode |= PACKED_PS_18BIT_RGB666;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		ps_bpp_mode |= LOOSELY_PS_18BIT_RGB666;
		break;
	case MIPI_DSI_FMT_RGB565:
		ps_bpp_mode |= PACKED_PS_16BIT_RGB565;
		break;
	}

	writel(vm->vactive, dsi->regs + DSI_VACT_NL);
	writel(ps_bpp_mode, dsi->regs + DSI_PSCTRL);
	writel(ps_wc, dsi->regs + DSI_HSTX_CKL_WC);
}

static void mtk_dsi_rxtx_control(struct mtk_dsi *dsi)
{
	u32 tmp_reg;

	switch (dsi->lanes) {
	case 1:
		tmp_reg = 1 << 2;
		break;
	case 2:
		tmp_reg = 3 << 2;
		break;
	case 3:
		tmp_reg = 7 << 2;
		break;
	case 4:
		tmp_reg = 0xf << 2;
		break;
	default:
		tmp_reg = 0xf << 2;
		break;
	}

	if (dsi->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS)
		tmp_reg |= HSTX_CKLP_EN;

	if (dsi->mode_flags & MIPI_DSI_MODE_NO_EOT_PACKET)
		tmp_reg |= DIS_EOT;

	writel(tmp_reg, dsi->regs + DSI_TXRX_CTRL);
}

static void mtk_dsi_ps_control(struct mtk_dsi *dsi)
{
	u32 dsi_tmp_buf_bpp;
	u32 tmp_reg;

	switch (dsi->format) {
	case MIPI_DSI_FMT_RGB888:
		tmp_reg = PACKED_PS_24BIT_RGB888;
		dsi_tmp_buf_bpp = 3;
		break;
	case MIPI_DSI_FMT_RGB666:
		tmp_reg = LOOSELY_PS_18BIT_RGB666;
		dsi_tmp_buf_bpp = 3;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		tmp_reg = PACKED_PS_18BIT_RGB666;
		dsi_tmp_buf_bpp = 3;
		break;
	case MIPI_DSI_FMT_RGB565:
		tmp_reg = PACKED_PS_16BIT_RGB565;
		dsi_tmp_buf_bpp = 2;
		break;
	default:
		tmp_reg = PACKED_PS_24BIT_RGB888;
		dsi_tmp_buf_bpp = 3;
		break;
	}

	tmp_reg += dsi->vm.hactive * dsi_tmp_buf_bpp & DSI_PS_WC;
	writel(tmp_reg, dsi->regs + DSI_PSCTRL);
}

static void mtk_dsi_config_vdo_timing(struct mtk_dsi *dsi)
{
	u32 horizontal_sync_active_byte;
	u32 horizontal_backporch_byte;
	u32 horizontal_frontporch_byte;
	u32 horizontal_front_back_byte;
	u32 data_phy_cycles_byte;
	u32 dsi_tmp_buf_bpp, data_phy_cycles;
	u32 delta;
	struct mtk_phy_timing *timing = &dsi->phy_timing;

	struct videomode *vm = &dsi->vm;

	if (dsi->format == MIPI_DSI_FMT_RGB565)
		dsi_tmp_buf_bpp = 2;
	else
		dsi_tmp_buf_bpp = 3;

	writel(vm->vsync_len, dsi->regs + DSI_VSA_NL);
	writel(vm->vback_porch, dsi->regs + DSI_VBP_NL);
	writel(vm->vfront_porch, dsi->regs + DSI_VFP_NL);
	writel(vm->vactive, dsi->regs + DSI_VACT_NL);

	if (dsi->driver_data->has_size_ctl)
		writel(vm->vactive << 16 | vm->hactive,
		       dsi->regs + DSI_SIZE_CON);

	horizontal_sync_active_byte = (vm->hsync_len * dsi_tmp_buf_bpp - 10);

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
		horizontal_backporch_byte = vm->hback_porch * dsi_tmp_buf_bpp - 10;
	else
		horizontal_backporch_byte = (vm->hback_porch + vm->hsync_len) *
					    dsi_tmp_buf_bpp - 10;

	data_phy_cycles = timing->lpx + timing->da_hs_prepare +
			  timing->da_hs_zero + timing->da_hs_exit + 3;

	delta = dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST ? 18 : 12;
	delta += dsi->mode_flags & MIPI_DSI_MODE_NO_EOT_PACKET ? 0 : 2;

	horizontal_frontporch_byte = vm->hfront_porch * dsi_tmp_buf_bpp;
	horizontal_front_back_byte = horizontal_frontporch_byte + horizontal_backporch_byte;
	data_phy_cycles_byte = data_phy_cycles * dsi->lanes + delta;

	if (horizontal_front_back_byte > data_phy_cycles_byte) {
		horizontal_frontporch_byte -= data_phy_cycles_byte *
					      horizontal_frontporch_byte /
					      horizontal_front_back_byte;

		horizontal_backporch_byte -= data_phy_cycles_byte *
					     horizontal_backporch_byte /
					     horizontal_front_back_byte;
	} else {
		DRM_WARN("HFP + HBP less than d-phy, FPS will under 60Hz\n");
	}

	if ((dsi->mode_flags & MIPI_DSI_HS_PKT_END_ALIGNED) &&
	    (dsi->lanes == 4)) {
		horizontal_sync_active_byte =
			roundup(horizontal_sync_active_byte, dsi->lanes) - 2;
		horizontal_frontporch_byte =
			roundup(horizontal_frontporch_byte, dsi->lanes) - 2;
		horizontal_backporch_byte =
			roundup(horizontal_backporch_byte, dsi->lanes) - 2;
		horizontal_backporch_byte -=
			(vm->hactive * dsi_tmp_buf_bpp + 2) % dsi->lanes;
	}

	writel(horizontal_sync_active_byte, dsi->regs + DSI_HSA_WC);
	writel(horizontal_backporch_byte, dsi->regs + DSI_HBP_WC);
	writel(horizontal_frontporch_byte, dsi->regs + DSI_HFP_WC);

	mtk_dsi_ps_control(dsi);
}

static void mtk_dsi_start(struct mtk_dsi *dsi)
{
	writel(0, dsi->regs + DSI_START);
	writel(1, dsi->regs + DSI_START);
}

static void mtk_dsi_stop(struct mtk_dsi *dsi)
{
	writel(0, dsi->regs + DSI_START);
}

static void mtk_dsi_set_cmd_mode(struct mtk_dsi *dsi)
{
	writel(CMD_MODE, dsi->regs + DSI_MODE_CTRL);
}

static void mtk_dsi_set_interrupt_enable(struct mtk_dsi *dsi)
{
	u32 inten = LPRX_RD_RDY_INT_FLAG | CMD_DONE_INT_FLAG | VM_DONE_INT_FLAG;

	writel(inten, dsi->regs + DSI_INTEN);
}

static void mtk_dsi_irq_data_set(struct mtk_dsi *dsi, u32 irq_bit)
{
	dsi->irq_data |= irq_bit;
}

static void mtk_dsi_irq_data_clear(struct mtk_dsi *dsi, u32 irq_bit)
{
	dsi->irq_data &= ~irq_bit;
}

static s32 mtk_dsi_wait_for_irq_done(struct mtk_dsi *dsi, u32 irq_flag,
				     unsigned int timeout)
{
	s32 ret = 0;
	unsigned long jiffies = msecs_to_jiffies(timeout);

	ret = wait_event_interruptible_timeout(dsi->irq_wait_queue,
					       dsi->irq_data & irq_flag,
					       jiffies);
	if (ret == 0) {
		DRM_WARN("Wait DSI IRQ(0x%08x) Timeout\n", irq_flag);

		mtk_dsi_enable(dsi);
		mtk_dsi_reset_engine(dsi);
	}

	return ret;
}

static irqreturn_t mtk_dsi_irq(int irq, void *dev_id)
{
	struct mtk_dsi *dsi = dev_id;
	u32 status, tmp;
	u32 flag = LPRX_RD_RDY_INT_FLAG | CMD_DONE_INT_FLAG | VM_DONE_INT_FLAG;

	status = readl(dsi->regs + DSI_INTSTA) & flag;

	if (status) {
		do {
			mtk_dsi_mask(dsi, DSI_RACK, RACK, RACK);
			tmp = readl(dsi->regs + DSI_INTSTA);
		} while (tmp & DSI_BUSY);

		mtk_dsi_mask(dsi, DSI_INTSTA, status, 0);
		mtk_dsi_irq_data_set(dsi, status);
		wake_up_interruptible(&dsi->irq_wait_queue);
	}

	return IRQ_HANDLED;
}

static s32 mtk_dsi_switch_to_cmd_mode(struct mtk_dsi *dsi, u8 irq_flag, u32 t)
{
	mtk_dsi_irq_data_clear(dsi, irq_flag);
	mtk_dsi_set_cmd_mode(dsi);

	if (!mtk_dsi_wait_for_irq_done(dsi, irq_flag, t)) {
		DRM_ERROR("failed to switch cmd mode\n");
		return -ETIME;
	} else {
		return 0;
	}
}

static int mtk_dsi_poweron(struct mtk_dsi *dsi)
{
	struct device *dev = dsi->host.dev;
	int ret;
	u32 bit_per_pixel;

	if (++dsi->refcount != 1)
		return 0;

	switch (dsi->format) {
	case MIPI_DSI_FMT_RGB565:
		bit_per_pixel = 16;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		bit_per_pixel = 18;
		break;
	case MIPI_DSI_FMT_RGB666:
	case MIPI_DSI_FMT_RGB888:
	default:
		bit_per_pixel = 24;
		break;
	}

	dsi->data_rate = DIV_ROUND_UP_ULL(dsi->vm.pixelclock * bit_per_pixel,
					  dsi->lanes);

	ret = clk_set_rate(dsi->hs_clk, dsi->data_rate);
	if (ret < 0) {
		dev_err(dev, "Failed to set data rate: %d\n", ret);
		goto err_refcount;
	}

	phy_power_on(dsi->phy);

	ret = clk_prepare_enable(dsi->engine_clk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable engine clock: %d\n", ret);
		goto err_phy_power_off;
	}

	ret = clk_prepare_enable(dsi->digital_clk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable digital clock: %d\n", ret);
		goto err_disable_engine_clk;
	}

	mtk_dsi_enable(dsi);

	if (dsi->driver_data->has_shadow_ctl)
		writel(FORCE_COMMIT | BYPASS_SHADOW,
		       dsi->regs + DSI_SHADOW_DEBUG);

	mtk_dsi_reset_engine(dsi);
	mtk_dsi_phy_timconfig(dsi);

	mtk_dsi_ps_control_vact(dsi);
	mtk_dsi_set_vm_cmd(dsi);
	mtk_dsi_config_vdo_timing(dsi);
	mtk_dsi_set_interrupt_enable(dsi);

	return 0;
err_disable_engine_clk:
	clk_disable_unprepare(dsi->engine_clk);
err_phy_power_off:
	phy_power_off(dsi->phy);
err_refcount:
	dsi->refcount--;
	return ret;
}

static void mtk_dsi_poweroff(struct mtk_dsi *dsi)
{
	if (WARN_ON(dsi->refcount == 0))
		return;

	if (--dsi->refcount != 0)
		return;

	/*
	 * mtk_dsi_stop() and mtk_dsi_start() is asymmetric, since
	 * mtk_dsi_stop() should be called after mtk_drm_crtc_atomic_disable(),
	 * which needs irq for vblank, and mtk_dsi_stop() will disable irq.
	 * mtk_dsi_start() needs to be called in mtk_output_dsi_enable(),
	 * after dsi is fully set.
	 */
	mtk_dsi_stop(dsi);

	mtk_dsi_switch_to_cmd_mode(dsi, VM_DONE_INT_FLAG, 500);
	mtk_dsi_reset_engine(dsi);
	mtk_dsi_lane0_ulp_mode_enter(dsi);
	mtk_dsi_clk_ulp_mode_enter(dsi);
	/* set the lane number as 0 to pull down mipi */
	writel(0, dsi->regs + DSI_TXRX_CTRL);

	mtk_dsi_disable(dsi);

	clk_disable_unprepare(dsi->engine_clk);
	clk_disable_unprepare(dsi->digital_clk);

	phy_power_off(dsi->phy);

	dsi->lanes_ready = false;
}

static void mtk_dsi_lane_ready(struct mtk_dsi *dsi)
{
	if (!dsi->lanes_ready) {
		dsi->lanes_ready = true;
		mtk_dsi_rxtx_control(dsi);
		usleep_range(30, 100);
		mtk_dsi_reset_dphy(dsi);
		mtk_dsi_clk_ulp_mode_leave(dsi);
		mtk_dsi_lane0_ulp_mode_leave(dsi);
		mtk_dsi_clk_hs_mode(dsi, 0);
		usleep_range(1000, 3000);
		/* The reaction time after pulling up the mipi signal for dsi_rx */
	}
}

static void mtk_output_dsi_enable(struct mtk_dsi *dsi)
{
	if (dsi->enabled)
		return;

	mtk_dsi_lane_ready(dsi);
	mtk_dsi_set_mode(dsi);
	mtk_dsi_clk_hs_mode(dsi, 1);

	mtk_dsi_start(dsi);

	dsi->enabled = true;
}

static void mtk_output_dsi_disable(struct mtk_dsi *dsi)
{
	if (!dsi->enabled)
		return;

	dsi->enabled = false;
}

static int mtk_dsi_bridge_attach(struct drm_bridge *bridge,
				 enum drm_bridge_attach_flags flags)
{
	struct mtk_dsi *dsi = bridge_to_dsi(bridge);

	/* Attach the panel or bridge to the dsi bridge */
	return drm_bridge_attach(bridge->encoder, dsi->next_bridge,
				 &dsi->bridge, flags);
}

static void mtk_dsi_bridge_mode_set(struct drm_bridge *bridge,
				    const struct drm_display_mode *mode,
				    const struct drm_display_mode *adjusted)
{
	struct mtk_dsi *dsi = bridge_to_dsi(bridge);

	drm_display_mode_to_videomode(adjusted, &dsi->vm);
}

static void mtk_dsi_bridge_atomic_disable(struct drm_bridge *bridge,
					  struct drm_bridge_state *old_bridge_state)
{
	struct mtk_dsi *dsi = bridge_to_dsi(bridge);

	mtk_output_dsi_disable(dsi);
}

static void mtk_dsi_bridge_atomic_enable(struct drm_bridge *bridge,
					 struct drm_bridge_state *old_bridge_state)
{
	struct mtk_dsi *dsi = bridge_to_dsi(bridge);

	if (dsi->refcount == 0)
		return;

	mtk_output_dsi_enable(dsi);
}

static void mtk_dsi_bridge_atomic_pre_enable(struct drm_bridge *bridge,
					     struct drm_bridge_state *old_bridge_state)
{
	struct mtk_dsi *dsi = bridge_to_dsi(bridge);
	int ret;

	ret = mtk_dsi_poweron(dsi);
	if (ret < 0)
		DRM_ERROR("failed to power on dsi\n");
}

static void mtk_dsi_bridge_atomic_post_disable(struct drm_bridge *bridge,
					       struct drm_bridge_state *old_bridge_state)
{
	struct mtk_dsi *dsi = bridge_to_dsi(bridge);

	mtk_dsi_poweroff(dsi);
}

static enum drm_mode_status
mtk_dsi_bridge_mode_valid(struct drm_bridge *bridge,
			  const struct drm_display_info *info,
			  const struct drm_display_mode *mode)
{
	struct mtk_dsi *dsi = bridge_to_dsi(bridge);
	u32 bpp;

	if (dsi->format == MIPI_DSI_FMT_RGB565)
		bpp = 16;
	else
		bpp = 24;

	if (mode->clock * bpp / dsi->lanes > 1500000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static const struct drm_bridge_funcs mtk_dsi_bridge_funcs = {
	.attach = mtk_dsi_bridge_attach,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_disable = mtk_dsi_bridge_atomic_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_enable = mtk_dsi_bridge_atomic_enable,
	.atomic_pre_enable = mtk_dsi_bridge_atomic_pre_enable,
	.atomic_post_disable = mtk_dsi_bridge_atomic_post_disable,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.mode_valid = mtk_dsi_bridge_mode_valid,
	.mode_set = mtk_dsi_bridge_mode_set,
};

void mtk_dsi_ddp_start(struct device *dev)
{
	struct mtk_dsi *dsi = dev_get_drvdata(dev);

	mtk_dsi_poweron(dsi);
}

void mtk_dsi_ddp_stop(struct device *dev)
{
	struct mtk_dsi *dsi = dev_get_drvdata(dev);

	mtk_dsi_poweroff(dsi);
}

static int mtk_dsi_encoder_init(struct drm_device *drm, struct mtk_dsi *dsi)
{
	int ret;

	ret = drm_simple_encoder_init(drm, &dsi->encoder,
				      DRM_MODE_ENCODER_DSI);
	if (ret) {
		DRM_ERROR("Failed to encoder init to drm\n");
		return ret;
	}

	dsi->encoder.possible_crtcs = mtk_drm_find_possible_crtc_by_comp(drm, dsi->host.dev);

	ret = drm_bridge_attach(&dsi->encoder, &dsi->bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret)
		goto err_cleanup_encoder;

	dsi->connector = drm_bridge_connector_init(drm, &dsi->encoder);
	if (IS_ERR(dsi->connector)) {
		DRM_ERROR("Unable to create bridge connector\n");
		ret = PTR_ERR(dsi->connector);
		goto err_cleanup_encoder;
	}
	drm_connector_attach_encoder(dsi->connector, &dsi->encoder);

	return 0;

err_cleanup_encoder:
	drm_encoder_cleanup(&dsi->encoder);
	return ret;
}

unsigned int mtk_dsi_encoder_index(struct device *dev)
{
	struct mtk_dsi *dsi = dev_get_drvdata(dev);
	unsigned int encoder_index = drm_encoder_index(&dsi->encoder);

	dev_dbg(dev, "encoder index:%d\n", encoder_index);
	return encoder_index;
}

static int mtk_dsi_bind(struct device *dev, struct device *master, void *data)
{
	int ret;
	struct drm_device *drm = data;
	struct mtk_dsi *dsi = dev_get_drvdata(dev);

	ret = mtk_dsi_encoder_init(drm, dsi);
	if (ret)
		return ret;

	return device_reset_optional(dev);
}

static void mtk_dsi_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct mtk_dsi *dsi = dev_get_drvdata(dev);

	drm_encoder_cleanup(&dsi->encoder);
}

static const struct component_ops mtk_dsi_component_ops = {
	.bind = mtk_dsi_bind,
	.unbind = mtk_dsi_unbind,
};

static int mtk_dsi_host_attach(struct mipi_dsi_host *host,
			       struct mipi_dsi_device *device)
{
	struct mtk_dsi *dsi = host_to_dsi(host);
	struct device *dev = host->dev;
	int ret;

	dsi->lanes = device->lanes;
	dsi->format = device->format;
	dsi->mode_flags = device->mode_flags;
	dsi->next_bridge = devm_drm_of_get_bridge(dev, dev->of_node, 0, 0);
	if (IS_ERR(dsi->next_bridge))
		return PTR_ERR(dsi->next_bridge);

	drm_bridge_add(&dsi->bridge);

	ret = component_add(host->dev, &mtk_dsi_component_ops);
	if (ret) {
		DRM_ERROR("failed to add dsi_host component: %d\n", ret);
		drm_bridge_remove(&dsi->bridge);
		return ret;
	}

	return 0;
}

static int mtk_dsi_host_detach(struct mipi_dsi_host *host,
			       struct mipi_dsi_device *device)
{
	struct mtk_dsi *dsi = host_to_dsi(host);

	component_del(host->dev, &mtk_dsi_component_ops);
	drm_bridge_remove(&dsi->bridge);
	return 0;
}

static void mtk_dsi_wait_for_idle(struct mtk_dsi *dsi)
{
	int ret;
	u32 val;

	ret = readl_poll_timeout(dsi->regs + DSI_INTSTA, val, !(val & DSI_BUSY),
				 4, 2000000);
	if (ret) {
		DRM_WARN("polling dsi wait not busy timeout!\n");

		mtk_dsi_enable(dsi);
		mtk_dsi_reset_engine(dsi);
	}
}

static u32 mtk_dsi_recv_cnt(u8 type, u8 *read_data)
{
	switch (type) {
	case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_1BYTE:
	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_1BYTE:
		return 1;
	case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_2BYTE:
	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_2BYTE:
		return 2;
	case MIPI_DSI_RX_GENERIC_LONG_READ_RESPONSE:
	case MIPI_DSI_RX_DCS_LONG_READ_RESPONSE:
		return read_data[1] + read_data[2] * 16;
	case MIPI_DSI_RX_ACKNOWLEDGE_AND_ERROR_REPORT:
		DRM_INFO("type is 0x02, try again\n");
		break;
	default:
		DRM_INFO("type(0x%x) not recognized\n", type);
		break;
	}

	return 0;
}

static void mtk_dsi_cmdq(struct mtk_dsi *dsi, const struct mipi_dsi_msg *msg)
{
	const char *tx_buf = msg->tx_buf;
	u8 config, cmdq_size, cmdq_off, type = msg->type;
	u32 reg_val, cmdq_mask, i;
	u32 reg_cmdq_off = dsi->driver_data->reg_cmdq_off;

	if (MTK_DSI_HOST_IS_READ(type))
		config = BTA;
	else
		config = (msg->tx_len > 2) ? LONG_PACKET : SHORT_PACKET;

	if (msg->tx_len > 2) {
		cmdq_size = 1 + (msg->tx_len + 3) / 4;
		cmdq_off = 4;
		cmdq_mask = CONFIG | DATA_ID | DATA_0 | DATA_1;
		reg_val = (msg->tx_len << 16) | (type << 8) | config;
	} else {
		cmdq_size = 1;
		cmdq_off = 2;
		cmdq_mask = CONFIG | DATA_ID;
		reg_val = (type << 8) | config;
	}

	for (i = 0; i < msg->tx_len; i++)
		mtk_dsi_mask(dsi, (reg_cmdq_off + cmdq_off + i) & (~0x3U),
			     (0xffUL << (((i + cmdq_off) & 3U) * 8U)),
			     tx_buf[i] << (((i + cmdq_off) & 3U) * 8U));

	mtk_dsi_mask(dsi, reg_cmdq_off, cmdq_mask, reg_val);
	mtk_dsi_mask(dsi, DSI_CMDQ_SIZE, CMDQ_SIZE, cmdq_size);
	if (dsi->driver_data->cmdq_long_packet_ctl) {
		/* Disable setting cmdq_size automatically for long packets */
		mtk_dsi_mask(dsi, DSI_CMDQ_SIZE, CMDQ_SIZE_SEL, CMDQ_SIZE_SEL);
	}
}

static ssize_t mtk_dsi_host_send_cmd(struct mtk_dsi *dsi,
				     const struct mipi_dsi_msg *msg, u8 flag)
{
	mtk_dsi_wait_for_idle(dsi);
	mtk_dsi_irq_data_clear(dsi, flag);
	mtk_dsi_cmdq(dsi, msg);
	mtk_dsi_start(dsi);

	if (!mtk_dsi_wait_for_irq_done(dsi, flag, 2000))
		return -ETIME;
	else
		return 0;
}

static ssize_t mtk_dsi_host_transfer(struct mipi_dsi_host *host,
				     const struct mipi_dsi_msg *msg)
{
	struct mtk_dsi *dsi = host_to_dsi(host);
	u32 recv_cnt, i;
	u8 read_data[16];
	void *src_addr;
	u8 irq_flag = CMD_DONE_INT_FLAG;
	u32 dsi_mode;
	int ret;

	dsi_mode = readl(dsi->regs + DSI_MODE_CTRL);
	if (dsi_mode & MODE) {
		mtk_dsi_stop(dsi);
		ret = mtk_dsi_switch_to_cmd_mode(dsi, VM_DONE_INT_FLAG, 500);
		if (ret)
			goto restore_dsi_mode;
	}

	if (MTK_DSI_HOST_IS_READ(msg->type))
		irq_flag |= LPRX_RD_RDY_INT_FLAG;

	mtk_dsi_lane_ready(dsi);

	ret = mtk_dsi_host_send_cmd(dsi, msg, irq_flag);
	if (ret)
		goto restore_dsi_mode;

	if (!MTK_DSI_HOST_IS_READ(msg->type)) {
		recv_cnt = 0;
		goto restore_dsi_mode;
	}

	if (!msg->rx_buf) {
		DRM_ERROR("dsi receive buffer size may be NULL\n");
		ret = -EINVAL;
		goto restore_dsi_mode;
	}

	for (i = 0; i < 16; i++)
		*(read_data + i) = readb(dsi->regs + DSI_RX_DATA0 + i);

	recv_cnt = mtk_dsi_recv_cnt(read_data[0], read_data);

	if (recv_cnt > 2)
		src_addr = &read_data[4];
	else
		src_addr = &read_data[1];

	if (recv_cnt > 10)
		recv_cnt = 10;

	if (recv_cnt > msg->rx_len)
		recv_cnt = msg->rx_len;

	if (recv_cnt)
		memcpy(msg->rx_buf, src_addr, recv_cnt);

	DRM_INFO("dsi get %d byte data from the panel address(0x%x)\n",
		 recv_cnt, *((u8 *)(msg->tx_buf)));

restore_dsi_mode:
	if (dsi_mode & MODE) {
		mtk_dsi_set_mode(dsi);
		mtk_dsi_start(dsi);
	}

	return ret < 0 ? ret : recv_cnt;
}

static const struct mipi_dsi_host_ops mtk_dsi_ops = {
	.attach = mtk_dsi_host_attach,
	.detach = mtk_dsi_host_detach,
	.transfer = mtk_dsi_host_transfer,
};

static int mtk_dsi_probe(struct platform_device *pdev)
{
	struct mtk_dsi *dsi;
	struct device *dev = &pdev->dev;
	struct resource *regs;
	int irq_num;
	int ret;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi->host.ops = &mtk_dsi_ops;
	dsi->host.dev = dev;
	ret = mipi_dsi_host_register(&dsi->host);
	if (ret < 0) {
		dev_err(dev, "failed to register DSI host: %d\n", ret);
		return ret;
	}

	dsi->driver_data = of_device_get_match_data(dev);

	dsi->engine_clk = devm_clk_get(dev, "engine");
	if (IS_ERR(dsi->engine_clk)) {
		ret = PTR_ERR(dsi->engine_clk);

		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get engine clock: %d\n", ret);
		goto err_unregister_host;
	}

	dsi->digital_clk = devm_clk_get(dev, "digital");
	if (IS_ERR(dsi->digital_clk)) {
		ret = PTR_ERR(dsi->digital_clk);

		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get digital clock: %d\n", ret);
		goto err_unregister_host;
	}

	dsi->hs_clk = devm_clk_get(dev, "hs");
	if (IS_ERR(dsi->hs_clk)) {
		ret = PTR_ERR(dsi->hs_clk);
		dev_err(dev, "Failed to get hs clock: %d\n", ret);
		goto err_unregister_host;
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dsi->regs = devm_ioremap_resource(dev, regs);
	if (IS_ERR(dsi->regs)) {
		ret = PTR_ERR(dsi->regs);
		dev_err(dev, "Failed to ioremap memory: %d\n", ret);
		goto err_unregister_host;
	}

	dsi->phy = devm_phy_get(dev, "dphy");
	if (IS_ERR(dsi->phy)) {
		ret = PTR_ERR(dsi->phy);
		dev_err(dev, "Failed to get MIPI-DPHY: %d\n", ret);
		goto err_unregister_host;
	}

	irq_num = platform_get_irq(pdev, 0);
	if (irq_num < 0) {
		ret = irq_num;
		goto err_unregister_host;
	}

	ret = devm_request_irq(&pdev->dev, irq_num, mtk_dsi_irq,
			       IRQF_TRIGGER_NONE, dev_name(&pdev->dev), dsi);
	if (ret) {
		dev_err(&pdev->dev, "failed to request mediatek dsi irq\n");
		goto err_unregister_host;
	}

	init_waitqueue_head(&dsi->irq_wait_queue);

	platform_set_drvdata(pdev, dsi);

	dsi->bridge.funcs = &mtk_dsi_bridge_funcs;
	dsi->bridge.of_node = dev->of_node;
	dsi->bridge.type = DRM_MODE_CONNECTOR_DSI;

	return 0;

err_unregister_host:
	mipi_dsi_host_unregister(&dsi->host);
	return ret;
}

static void mtk_dsi_remove(struct platform_device *pdev)
{
	struct mtk_dsi *dsi = platform_get_drvdata(pdev);

	mtk_output_dsi_disable(dsi);
	mipi_dsi_host_unregister(&dsi->host);
}

static const struct mtk_dsi_driver_data mt8173_dsi_driver_data = {
	.reg_cmdq_off = 0x200,
};

static const struct mtk_dsi_driver_data mt2701_dsi_driver_data = {
	.reg_cmdq_off = 0x180,
};

static const struct mtk_dsi_driver_data mt8183_dsi_driver_data = {
	.reg_cmdq_off = 0x200,
	.has_shadow_ctl = true,
	.has_size_ctl = true,
};

static const struct mtk_dsi_driver_data mt8186_dsi_driver_data = {
	.reg_cmdq_off = 0xd00,
	.has_shadow_ctl = true,
	.has_size_ctl = true,
};

static const struct mtk_dsi_driver_data mt8188_dsi_driver_data = {
	.reg_cmdq_off = 0xd00,
	.has_shadow_ctl = true,
	.has_size_ctl = true,
	.cmdq_long_packet_ctl = true,
};

static const struct of_device_id mtk_dsi_of_match[] = {
	{ .compatible = "mediatek,mt2701-dsi",
	  .data = &mt2701_dsi_driver_data },
	{ .compatible = "mediatek,mt8173-dsi",
	  .data = &mt8173_dsi_driver_data },
	{ .compatible = "mediatek,mt8183-dsi",
	  .data = &mt8183_dsi_driver_data },
	{ .compatible = "mediatek,mt8186-dsi",
	  .data = &mt8186_dsi_driver_data },
	{ .compatible = "mediatek,mt8188-dsi",
	  .data = &mt8188_dsi_driver_data },
	{ },
};
MODULE_DEVICE_TABLE(of, mtk_dsi_of_match);

struct platform_driver mtk_dsi_driver = {
	.probe = mtk_dsi_probe,
	.remove_new = mtk_dsi_remove,
	.driver = {
		.name = "mtk-dsi",
		.of_match_table = mtk_dsi_of_match,
	},
};
