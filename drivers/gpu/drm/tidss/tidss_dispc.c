// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016-2018 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Jyri Sarha <jsarha@ti.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/sys_soc.h>

#include <drm/drm_blend.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_panel.h>

#include "tidss_crtc.h"
#include "tidss_dispc.h"
#include "tidss_drv.h"
#include "tidss_irq.h"
#include "tidss_plane.h"

#include "tidss_dispc_regs.h"
#include "tidss_scale_coefs.h"

static const u16 tidss_k2g_common_regs[DISPC_COMMON_REG_TABLE_LEN] = {
	[DSS_REVISION_OFF] =                    0x00,
	[DSS_SYSCONFIG_OFF] =                   0x04,
	[DSS_SYSSTATUS_OFF] =                   0x08,
	[DISPC_IRQ_EOI_OFF] =                   0x20,
	[DISPC_IRQSTATUS_RAW_OFF] =             0x24,
	[DISPC_IRQSTATUS_OFF] =                 0x28,
	[DISPC_IRQENABLE_SET_OFF] =             0x2c,
	[DISPC_IRQENABLE_CLR_OFF] =             0x30,

	[DISPC_GLOBAL_MFLAG_ATTRIBUTE_OFF] =    0x40,
	[DISPC_GLOBAL_BUFFER_OFF] =             0x44,

	[DISPC_DBG_CONTROL_OFF] =               0x4c,
	[DISPC_DBG_STATUS_OFF] =                0x50,

	[DISPC_CLKGATING_DISABLE_OFF] =         0x54,
};

const struct dispc_features dispc_k2g_feats = {
	.min_pclk_khz = 4375,

	.max_pclk_khz = {
		[DISPC_VP_DPI] = 150000,
	},

	/*
	 * XXX According TRM the RGB input buffer width up to 2560 should
	 *     work on 3 taps, but in practice it only works up to 1280.
	 */
	.scaling = {
		.in_width_max_5tap_rgb = 1280,
		.in_width_max_3tap_rgb = 1280,
		.in_width_max_5tap_yuv = 2560,
		.in_width_max_3tap_yuv = 2560,
		.upscale_limit = 16,
		.downscale_limit_5tap = 4,
		.downscale_limit_3tap = 2,
		/*
		 * The max supported pixel inc value is 255. The value
		 * of pixel inc is calculated like this: 1+(xinc-1)*bpp.
		 * The maximum bpp of all formats supported by the HW
		 * is 8. So the maximum supported xinc value is 32,
		 * because 1+(32-1)*8 < 255 < 1+(33-1)*4.
		 */
		.xinc_max = 32,
	},

	.subrev = DISPC_K2G,

	.common = "common",

	.common_regs = tidss_k2g_common_regs,

	.num_vps = 1,
	.vp_name = { "vp1" },
	.ovr_name = { "ovr1" },
	.vpclk_name =  { "vp1" },
	.vp_bus_type = { DISPC_VP_DPI },

	.vp_feat = { .color = {
			.has_ctm = true,
			.gamma_size = 256,
			.gamma_type = TIDSS_GAMMA_8BIT,
		},
	},

	.num_planes = 1,
	.vid_name = { "vid1" },
	.vid_lite = { false },
	.vid_order = { 0 },
};

static const u16 tidss_am65x_common_regs[DISPC_COMMON_REG_TABLE_LEN] = {
	[DSS_REVISION_OFF] =			0x4,
	[DSS_SYSCONFIG_OFF] =			0x8,
	[DSS_SYSSTATUS_OFF] =			0x20,
	[DISPC_IRQ_EOI_OFF] =			0x24,
	[DISPC_IRQSTATUS_RAW_OFF] =		0x28,
	[DISPC_IRQSTATUS_OFF] =			0x2c,
	[DISPC_IRQENABLE_SET_OFF] =		0x30,
	[DISPC_IRQENABLE_CLR_OFF] =		0x40,
	[DISPC_VID_IRQENABLE_OFF] =		0x44,
	[DISPC_VID_IRQSTATUS_OFF] =		0x58,
	[DISPC_VP_IRQENABLE_OFF] =		0x70,
	[DISPC_VP_IRQSTATUS_OFF] =		0x7c,

	[WB_IRQENABLE_OFF] =			0x88,
	[WB_IRQSTATUS_OFF] =			0x8c,

	[DISPC_GLOBAL_MFLAG_ATTRIBUTE_OFF] =	0x90,
	[DISPC_GLOBAL_OUTPUT_ENABLE_OFF] =	0x94,
	[DISPC_GLOBAL_BUFFER_OFF] =		0x98,
	[DSS_CBA_CFG_OFF] =			0x9c,
	[DISPC_DBG_CONTROL_OFF] =		0xa0,
	[DISPC_DBG_STATUS_OFF] =		0xa4,
	[DISPC_CLKGATING_DISABLE_OFF] =		0xa8,
	[DISPC_SECURE_DISABLE_OFF] =		0xac,
};

const struct dispc_features dispc_am65x_feats = {
	.max_pclk_khz = {
		[DISPC_VP_DPI] = 165000,
		[DISPC_VP_OLDI] = 165000,
	},

	.scaling = {
		.in_width_max_5tap_rgb = 1280,
		.in_width_max_3tap_rgb = 2560,
		.in_width_max_5tap_yuv = 2560,
		.in_width_max_3tap_yuv = 4096,
		.upscale_limit = 16,
		.downscale_limit_5tap = 4,
		.downscale_limit_3tap = 2,
		/*
		 * The max supported pixel inc value is 255. The value
		 * of pixel inc is calculated like this: 1+(xinc-1)*bpp.
		 * The maximum bpp of all formats supported by the HW
		 * is 8. So the maximum supported xinc value is 32,
		 * because 1+(32-1)*8 < 255 < 1+(33-1)*4.
		 */
		.xinc_max = 32,
	},

	.subrev = DISPC_AM65X,

	.common = "common",
	.common_regs = tidss_am65x_common_regs,

	.num_vps = 2,
	.vp_name = { "vp1", "vp2" },
	.ovr_name = { "ovr1", "ovr2" },
	.vpclk_name =  { "vp1", "vp2" },
	.vp_bus_type = { DISPC_VP_OLDI, DISPC_VP_DPI },

	.vp_feat = { .color = {
			.has_ctm = true,
			.gamma_size = 256,
			.gamma_type = TIDSS_GAMMA_8BIT,
		},
	},

	.num_planes = 2,
	/* note: vid is plane_id 0 and vidl1 is plane_id 1 */
	.vid_name = { "vid", "vidl1" },
	.vid_lite = { false, true, },
	.vid_order = { 1, 0 },
};

static const u16 tidss_j721e_common_regs[DISPC_COMMON_REG_TABLE_LEN] = {
	[DSS_REVISION_OFF] =			0x4,
	[DSS_SYSCONFIG_OFF] =			0x8,
	[DSS_SYSSTATUS_OFF] =			0x20,
	[DISPC_IRQ_EOI_OFF] =			0x80,
	[DISPC_IRQSTATUS_RAW_OFF] =		0x28,
	[DISPC_IRQSTATUS_OFF] =			0x2c,
	[DISPC_IRQENABLE_SET_OFF] =		0x30,
	[DISPC_IRQENABLE_CLR_OFF] =		0x34,
	[DISPC_VID_IRQENABLE_OFF] =		0x38,
	[DISPC_VID_IRQSTATUS_OFF] =		0x48,
	[DISPC_VP_IRQENABLE_OFF] =		0x58,
	[DISPC_VP_IRQSTATUS_OFF] =		0x68,

	[WB_IRQENABLE_OFF] =			0x78,
	[WB_IRQSTATUS_OFF] =			0x7c,

	[DISPC_GLOBAL_MFLAG_ATTRIBUTE_OFF] =	0x98,
	[DISPC_GLOBAL_OUTPUT_ENABLE_OFF] =	0x9c,
	[DISPC_GLOBAL_BUFFER_OFF] =		0xa0,
	[DSS_CBA_CFG_OFF] =			0xa4,
	[DISPC_DBG_CONTROL_OFF] =		0xa8,
	[DISPC_DBG_STATUS_OFF] =		0xac,
	[DISPC_CLKGATING_DISABLE_OFF] =		0xb0,
	[DISPC_SECURE_DISABLE_OFF] =		0x90,

	[FBDC_REVISION_1_OFF] =			0xb8,
	[FBDC_REVISION_2_OFF] =			0xbc,
	[FBDC_REVISION_3_OFF] =			0xc0,
	[FBDC_REVISION_4_OFF] =			0xc4,
	[FBDC_REVISION_5_OFF] =			0xc8,
	[FBDC_REVISION_6_OFF] =			0xcc,
	[FBDC_COMMON_CONTROL_OFF] =		0xd0,
	[FBDC_CONSTANT_COLOR_0_OFF] =		0xd4,
	[FBDC_CONSTANT_COLOR_1_OFF] =		0xd8,
	[DISPC_CONNECTIONS_OFF] =		0xe4,
	[DISPC_MSS_VP1_OFF] =			0xe8,
	[DISPC_MSS_VP3_OFF] =			0xec,
};

const struct dispc_features dispc_j721e_feats = {
	.max_pclk_khz = {
		[DISPC_VP_DPI] = 170000,
		[DISPC_VP_INTERNAL] = 600000,
	},

	.scaling = {
		.in_width_max_5tap_rgb = 2048,
		.in_width_max_3tap_rgb = 4096,
		.in_width_max_5tap_yuv = 4096,
		.in_width_max_3tap_yuv = 4096,
		.upscale_limit = 16,
		.downscale_limit_5tap = 4,
		.downscale_limit_3tap = 2,
		/*
		 * The max supported pixel inc value is 255. The value
		 * of pixel inc is calculated like this: 1+(xinc-1)*bpp.
		 * The maximum bpp of all formats supported by the HW
		 * is 8. So the maximum supported xinc value is 32,
		 * because 1+(32-1)*8 < 255 < 1+(33-1)*4.
		 */
		.xinc_max = 32,
	},

	.subrev = DISPC_J721E,

	.common = "common_m",
	.common_regs = tidss_j721e_common_regs,

	.num_vps = 4,
	.vp_name = { "vp1", "vp2", "vp3", "vp4" },
	.ovr_name = { "ovr1", "ovr2", "ovr3", "ovr4" },
	.vpclk_name = { "vp1", "vp2", "vp3", "vp4" },
	/* Currently hard coded VP routing (see dispc_initial_config()) */
	.vp_bus_type =	{ DISPC_VP_INTERNAL, DISPC_VP_DPI,
			  DISPC_VP_INTERNAL, DISPC_VP_DPI, },
	.vp_feat = { .color = {
			.has_ctm = true,
			.gamma_size = 1024,
			.gamma_type = TIDSS_GAMMA_10BIT,
		},
	},
	.num_planes = 4,
	.vid_name = { "vid1", "vidl1", "vid2", "vidl2" },
	.vid_lite = { 0, 1, 0, 1, },
	.vid_order = { 1, 3, 0, 2 },
};

static const u16 *dispc_common_regmap;

struct dss_vp_data {
	u32 *gamma_table;
};

struct dispc_device {
	struct tidss_device *tidss;
	struct device *dev;

	void __iomem *base_common;
	void __iomem *base_vid[TIDSS_MAX_PLANES];
	void __iomem *base_ovr[TIDSS_MAX_PORTS];
	void __iomem *base_vp[TIDSS_MAX_PORTS];

	struct regmap *oldi_io_ctrl;

	struct clk *vp_clk[TIDSS_MAX_PORTS];

	const struct dispc_features *feat;

	struct clk *fclk;

	bool is_enabled;

	struct dss_vp_data vp_data[TIDSS_MAX_PORTS];

	u32 *fourccs;
	u32 num_fourccs;

	u32 memory_bandwidth_limit;

	struct dispc_errata errata;
};

static void dispc_write(struct dispc_device *dispc, u16 reg, u32 val)
{
	iowrite32(val, dispc->base_common + reg);
}

static u32 dispc_read(struct dispc_device *dispc, u16 reg)
{
	return ioread32(dispc->base_common + reg);
}

static
void dispc_vid_write(struct dispc_device *dispc, u32 hw_plane, u16 reg, u32 val)
{
	void __iomem *base = dispc->base_vid[hw_plane];

	iowrite32(val, base + reg);
}

static u32 dispc_vid_read(struct dispc_device *dispc, u32 hw_plane, u16 reg)
{
	void __iomem *base = dispc->base_vid[hw_plane];

	return ioread32(base + reg);
}

static void dispc_ovr_write(struct dispc_device *dispc, u32 hw_videoport,
			    u16 reg, u32 val)
{
	void __iomem *base = dispc->base_ovr[hw_videoport];

	iowrite32(val, base + reg);
}

static u32 dispc_ovr_read(struct dispc_device *dispc, u32 hw_videoport, u16 reg)
{
	void __iomem *base = dispc->base_ovr[hw_videoport];

	return ioread32(base + reg);
}

static void dispc_vp_write(struct dispc_device *dispc, u32 hw_videoport,
			   u16 reg, u32 val)
{
	void __iomem *base = dispc->base_vp[hw_videoport];

	iowrite32(val, base + reg);
}

static u32 dispc_vp_read(struct dispc_device *dispc, u32 hw_videoport, u16 reg)
{
	void __iomem *base = dispc->base_vp[hw_videoport];

	return ioread32(base + reg);
}

/*
 * TRM gives bitfields as start:end, where start is the higher bit
 * number. For example 7:0
 */

static u32 FLD_MASK(u32 start, u32 end)
{
	return ((1 << (start - end + 1)) - 1) << end;
}

static u32 FLD_VAL(u32 val, u32 start, u32 end)
{
	return (val << end) & FLD_MASK(start, end);
}

static u32 FLD_GET(u32 val, u32 start, u32 end)
{
	return (val & FLD_MASK(start, end)) >> end;
}

static u32 FLD_MOD(u32 orig, u32 val, u32 start, u32 end)
{
	return (orig & ~FLD_MASK(start, end)) | FLD_VAL(val, start, end);
}

static u32 REG_GET(struct dispc_device *dispc, u32 idx, u32 start, u32 end)
{
	return FLD_GET(dispc_read(dispc, idx), start, end);
}

static void REG_FLD_MOD(struct dispc_device *dispc, u32 idx, u32 val,
			u32 start, u32 end)
{
	dispc_write(dispc, idx, FLD_MOD(dispc_read(dispc, idx), val,
					start, end));
}

static u32 VID_REG_GET(struct dispc_device *dispc, u32 hw_plane, u32 idx,
		       u32 start, u32 end)
{
	return FLD_GET(dispc_vid_read(dispc, hw_plane, idx), start, end);
}

static void VID_REG_FLD_MOD(struct dispc_device *dispc, u32 hw_plane, u32 idx,
			    u32 val, u32 start, u32 end)
{
	dispc_vid_write(dispc, hw_plane, idx,
			FLD_MOD(dispc_vid_read(dispc, hw_plane, idx),
				val, start, end));
}

static u32 VP_REG_GET(struct dispc_device *dispc, u32 vp, u32 idx,
		      u32 start, u32 end)
{
	return FLD_GET(dispc_vp_read(dispc, vp, idx), start, end);
}

static void VP_REG_FLD_MOD(struct dispc_device *dispc, u32 vp, u32 idx, u32 val,
			   u32 start, u32 end)
{
	dispc_vp_write(dispc, vp, idx, FLD_MOD(dispc_vp_read(dispc, vp, idx),
					       val, start, end));
}

__maybe_unused
static u32 OVR_REG_GET(struct dispc_device *dispc, u32 ovr, u32 idx,
		       u32 start, u32 end)
{
	return FLD_GET(dispc_ovr_read(dispc, ovr, idx), start, end);
}

static void OVR_REG_FLD_MOD(struct dispc_device *dispc, u32 ovr, u32 idx,
			    u32 val, u32 start, u32 end)
{
	dispc_ovr_write(dispc, ovr, idx,
			FLD_MOD(dispc_ovr_read(dispc, ovr, idx),
				val, start, end));
}

static dispc_irq_t dispc_vp_irq_from_raw(u32 stat, u32 hw_videoport)
{
	dispc_irq_t vp_stat = 0;

	if (stat & BIT(0))
		vp_stat |= DSS_IRQ_VP_FRAME_DONE(hw_videoport);
	if (stat & BIT(1))
		vp_stat |= DSS_IRQ_VP_VSYNC_EVEN(hw_videoport);
	if (stat & BIT(2))
		vp_stat |= DSS_IRQ_VP_VSYNC_ODD(hw_videoport);
	if (stat & BIT(4))
		vp_stat |= DSS_IRQ_VP_SYNC_LOST(hw_videoport);

	return vp_stat;
}

static u32 dispc_vp_irq_to_raw(dispc_irq_t vpstat, u32 hw_videoport)
{
	u32 stat = 0;

	if (vpstat & DSS_IRQ_VP_FRAME_DONE(hw_videoport))
		stat |= BIT(0);
	if (vpstat & DSS_IRQ_VP_VSYNC_EVEN(hw_videoport))
		stat |= BIT(1);
	if (vpstat & DSS_IRQ_VP_VSYNC_ODD(hw_videoport))
		stat |= BIT(2);
	if (vpstat & DSS_IRQ_VP_SYNC_LOST(hw_videoport))
		stat |= BIT(4);

	return stat;
}

static dispc_irq_t dispc_vid_irq_from_raw(u32 stat, u32 hw_plane)
{
	dispc_irq_t vid_stat = 0;

	if (stat & BIT(0))
		vid_stat |= DSS_IRQ_PLANE_FIFO_UNDERFLOW(hw_plane);

	return vid_stat;
}

static u32 dispc_vid_irq_to_raw(dispc_irq_t vidstat, u32 hw_plane)
{
	u32 stat = 0;

	if (vidstat & DSS_IRQ_PLANE_FIFO_UNDERFLOW(hw_plane))
		stat |= BIT(0);

	return stat;
}

static dispc_irq_t dispc_k2g_vp_read_irqstatus(struct dispc_device *dispc,
					       u32 hw_videoport)
{
	u32 stat = dispc_vp_read(dispc, hw_videoport, DISPC_VP_K2G_IRQSTATUS);

	return dispc_vp_irq_from_raw(stat, hw_videoport);
}

static void dispc_k2g_vp_write_irqstatus(struct dispc_device *dispc,
					 u32 hw_videoport, dispc_irq_t vpstat)
{
	u32 stat = dispc_vp_irq_to_raw(vpstat, hw_videoport);

	dispc_vp_write(dispc, hw_videoport, DISPC_VP_K2G_IRQSTATUS, stat);
}

static dispc_irq_t dispc_k2g_vid_read_irqstatus(struct dispc_device *dispc,
						u32 hw_plane)
{
	u32 stat = dispc_vid_read(dispc, hw_plane, DISPC_VID_K2G_IRQSTATUS);

	return dispc_vid_irq_from_raw(stat, hw_plane);
}

static void dispc_k2g_vid_write_irqstatus(struct dispc_device *dispc,
					  u32 hw_plane, dispc_irq_t vidstat)
{
	u32 stat = dispc_vid_irq_to_raw(vidstat, hw_plane);

	dispc_vid_write(dispc, hw_plane, DISPC_VID_K2G_IRQSTATUS, stat);
}

static dispc_irq_t dispc_k2g_vp_read_irqenable(struct dispc_device *dispc,
					       u32 hw_videoport)
{
	u32 stat = dispc_vp_read(dispc, hw_videoport, DISPC_VP_K2G_IRQENABLE);

	return dispc_vp_irq_from_raw(stat, hw_videoport);
}

static void dispc_k2g_vp_set_irqenable(struct dispc_device *dispc,
				       u32 hw_videoport, dispc_irq_t vpstat)
{
	u32 stat = dispc_vp_irq_to_raw(vpstat, hw_videoport);

	dispc_vp_write(dispc, hw_videoport, DISPC_VP_K2G_IRQENABLE, stat);
}

static dispc_irq_t dispc_k2g_vid_read_irqenable(struct dispc_device *dispc,
						u32 hw_plane)
{
	u32 stat = dispc_vid_read(dispc, hw_plane, DISPC_VID_K2G_IRQENABLE);

	return dispc_vid_irq_from_raw(stat, hw_plane);
}

static void dispc_k2g_vid_set_irqenable(struct dispc_device *dispc,
					u32 hw_plane, dispc_irq_t vidstat)
{
	u32 stat = dispc_vid_irq_to_raw(vidstat, hw_plane);

	dispc_vid_write(dispc, hw_plane, DISPC_VID_K2G_IRQENABLE, stat);
}

static void dispc_k2g_clear_irqstatus(struct dispc_device *dispc,
				      dispc_irq_t mask)
{
	dispc_k2g_vp_write_irqstatus(dispc, 0, mask);
	dispc_k2g_vid_write_irqstatus(dispc, 0, mask);
}

static
dispc_irq_t dispc_k2g_read_and_clear_irqstatus(struct dispc_device *dispc)
{
	dispc_irq_t stat = 0;

	/* always clear the top level irqstatus */
	dispc_write(dispc, DISPC_IRQSTATUS,
		    dispc_read(dispc, DISPC_IRQSTATUS));

	stat |= dispc_k2g_vp_read_irqstatus(dispc, 0);
	stat |= dispc_k2g_vid_read_irqstatus(dispc, 0);

	dispc_k2g_clear_irqstatus(dispc, stat);

	return stat;
}

static dispc_irq_t dispc_k2g_read_irqenable(struct dispc_device *dispc)
{
	dispc_irq_t stat = 0;

	stat |= dispc_k2g_vp_read_irqenable(dispc, 0);
	stat |= dispc_k2g_vid_read_irqenable(dispc, 0);

	return stat;
}

static
void dispc_k2g_set_irqenable(struct dispc_device *dispc, dispc_irq_t mask)
{
	dispc_irq_t old_mask = dispc_k2g_read_irqenable(dispc);

	/* clear the irqstatus for newly enabled irqs */
	dispc_k2g_clear_irqstatus(dispc, (mask ^ old_mask) & mask);

	dispc_k2g_vp_set_irqenable(dispc, 0, mask);
	dispc_k2g_vid_set_irqenable(dispc, 0, mask);

	dispc_write(dispc, DISPC_IRQENABLE_SET, (1 << 0) | (1 << 7));

	/* flush posted write */
	dispc_k2g_read_irqenable(dispc);
}

static dispc_irq_t dispc_k3_vp_read_irqstatus(struct dispc_device *dispc,
					      u32 hw_videoport)
{
	u32 stat = dispc_read(dispc, DISPC_VP_IRQSTATUS(hw_videoport));

	return dispc_vp_irq_from_raw(stat, hw_videoport);
}

static void dispc_k3_vp_write_irqstatus(struct dispc_device *dispc,
					u32 hw_videoport, dispc_irq_t vpstat)
{
	u32 stat = dispc_vp_irq_to_raw(vpstat, hw_videoport);

	dispc_write(dispc, DISPC_VP_IRQSTATUS(hw_videoport), stat);
}

static dispc_irq_t dispc_k3_vid_read_irqstatus(struct dispc_device *dispc,
					       u32 hw_plane)
{
	u32 stat = dispc_read(dispc, DISPC_VID_IRQSTATUS(hw_plane));

	return dispc_vid_irq_from_raw(stat, hw_plane);
}

static void dispc_k3_vid_write_irqstatus(struct dispc_device *dispc,
					 u32 hw_plane, dispc_irq_t vidstat)
{
	u32 stat = dispc_vid_irq_to_raw(vidstat, hw_plane);

	dispc_write(dispc, DISPC_VID_IRQSTATUS(hw_plane), stat);
}

static dispc_irq_t dispc_k3_vp_read_irqenable(struct dispc_device *dispc,
					      u32 hw_videoport)
{
	u32 stat = dispc_read(dispc, DISPC_VP_IRQENABLE(hw_videoport));

	return dispc_vp_irq_from_raw(stat, hw_videoport);
}

static void dispc_k3_vp_set_irqenable(struct dispc_device *dispc,
				      u32 hw_videoport, dispc_irq_t vpstat)
{
	u32 stat = dispc_vp_irq_to_raw(vpstat, hw_videoport);

	dispc_write(dispc, DISPC_VP_IRQENABLE(hw_videoport), stat);
}

static dispc_irq_t dispc_k3_vid_read_irqenable(struct dispc_device *dispc,
					       u32 hw_plane)
{
	u32 stat = dispc_read(dispc, DISPC_VID_IRQENABLE(hw_plane));

	return dispc_vid_irq_from_raw(stat, hw_plane);
}

static void dispc_k3_vid_set_irqenable(struct dispc_device *dispc,
				       u32 hw_plane, dispc_irq_t vidstat)
{
	u32 stat = dispc_vid_irq_to_raw(vidstat, hw_plane);

	dispc_write(dispc, DISPC_VID_IRQENABLE(hw_plane), stat);
}

static
void dispc_k3_clear_irqstatus(struct dispc_device *dispc, dispc_irq_t clearmask)
{
	unsigned int i;
	u32 top_clear = 0;

	for (i = 0; i < dispc->feat->num_vps; ++i) {
		if (clearmask & DSS_IRQ_VP_MASK(i)) {
			dispc_k3_vp_write_irqstatus(dispc, i, clearmask);
			top_clear |= BIT(i);
		}
	}
	for (i = 0; i < dispc->feat->num_planes; ++i) {
		if (clearmask & DSS_IRQ_PLANE_MASK(i)) {
			dispc_k3_vid_write_irqstatus(dispc, i, clearmask);
			top_clear |= BIT(4 + i);
		}
	}
	if (dispc->feat->subrev == DISPC_K2G)
		return;

	dispc_write(dispc, DISPC_IRQSTATUS, top_clear);

	/* Flush posted writes */
	dispc_read(dispc, DISPC_IRQSTATUS);
}

static
dispc_irq_t dispc_k3_read_and_clear_irqstatus(struct dispc_device *dispc)
{
	dispc_irq_t status = 0;
	unsigned int i;

	for (i = 0; i < dispc->feat->num_vps; ++i)
		status |= dispc_k3_vp_read_irqstatus(dispc, i);

	for (i = 0; i < dispc->feat->num_planes; ++i)
		status |= dispc_k3_vid_read_irqstatus(dispc, i);

	dispc_k3_clear_irqstatus(dispc, status);

	return status;
}

static dispc_irq_t dispc_k3_read_irqenable(struct dispc_device *dispc)
{
	dispc_irq_t enable = 0;
	unsigned int i;

	for (i = 0; i < dispc->feat->num_vps; ++i)
		enable |= dispc_k3_vp_read_irqenable(dispc, i);

	for (i = 0; i < dispc->feat->num_planes; ++i)
		enable |= dispc_k3_vid_read_irqenable(dispc, i);

	return enable;
}

static void dispc_k3_set_irqenable(struct dispc_device *dispc,
				   dispc_irq_t mask)
{
	unsigned int i;
	u32 main_enable = 0, main_disable = 0;
	dispc_irq_t old_mask;

	old_mask = dispc_k3_read_irqenable(dispc);

	/* clear the irqstatus for newly enabled irqs */
	dispc_k3_clear_irqstatus(dispc, (old_mask ^ mask) & mask);

	for (i = 0; i < dispc->feat->num_vps; ++i) {
		dispc_k3_vp_set_irqenable(dispc, i, mask);
		if (mask & DSS_IRQ_VP_MASK(i))
			main_enable |= BIT(i);		/* VP IRQ */
		else
			main_disable |= BIT(i);		/* VP IRQ */
	}

	for (i = 0; i < dispc->feat->num_planes; ++i) {
		dispc_k3_vid_set_irqenable(dispc, i, mask);
		if (mask & DSS_IRQ_PLANE_MASK(i))
			main_enable |= BIT(i + 4);	/* VID IRQ */
		else
			main_disable |= BIT(i + 4);	/* VID IRQ */
	}

	if (main_enable)
		dispc_write(dispc, DISPC_IRQENABLE_SET, main_enable);

	if (main_disable)
		dispc_write(dispc, DISPC_IRQENABLE_CLR, main_disable);

	/* Flush posted writes */
	dispc_read(dispc, DISPC_IRQENABLE_SET);
}

dispc_irq_t dispc_read_and_clear_irqstatus(struct dispc_device *dispc)
{
	switch (dispc->feat->subrev) {
	case DISPC_K2G:
		return dispc_k2g_read_and_clear_irqstatus(dispc);
	case DISPC_AM65X:
	case DISPC_J721E:
		return dispc_k3_read_and_clear_irqstatus(dispc);
	default:
		WARN_ON(1);
		return 0;
	}
}

void dispc_set_irqenable(struct dispc_device *dispc, dispc_irq_t mask)
{
	switch (dispc->feat->subrev) {
	case DISPC_K2G:
		dispc_k2g_set_irqenable(dispc, mask);
		break;
	case DISPC_AM65X:
	case DISPC_J721E:
		dispc_k3_set_irqenable(dispc, mask);
		break;
	default:
		WARN_ON(1);
		break;
	}
}

enum dispc_oldi_mode_reg_val { SPWG_18 = 0, JEIDA_24 = 1, SPWG_24 = 2 };

struct dispc_bus_format {
	u32 bus_fmt;
	u32 data_width;
	bool is_oldi_fmt;
	enum dispc_oldi_mode_reg_val oldi_mode_reg_val;
};

static const struct dispc_bus_format dispc_bus_formats[] = {
	{ MEDIA_BUS_FMT_RGB444_1X12,		12, false, 0 },
	{ MEDIA_BUS_FMT_RGB565_1X16,		16, false, 0 },
	{ MEDIA_BUS_FMT_RGB666_1X18,		18, false, 0 },
	{ MEDIA_BUS_FMT_RGB888_1X24,		24, false, 0 },
	{ MEDIA_BUS_FMT_RGB101010_1X30,		30, false, 0 },
	{ MEDIA_BUS_FMT_RGB121212_1X36,		36, false, 0 },
	{ MEDIA_BUS_FMT_RGB666_1X7X3_SPWG,	18, true, SPWG_18 },
	{ MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,	24, true, SPWG_24 },
	{ MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA,	24, true, JEIDA_24 },
};

static const
struct dispc_bus_format *dispc_vp_find_bus_fmt(struct dispc_device *dispc,
					       u32 hw_videoport,
					       u32 bus_fmt, u32 bus_flags)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dispc_bus_formats); ++i) {
		if (dispc_bus_formats[i].bus_fmt == bus_fmt)
			return &dispc_bus_formats[i];
	}

	return NULL;
}

int dispc_vp_bus_check(struct dispc_device *dispc, u32 hw_videoport,
		       const struct drm_crtc_state *state)
{
	const struct tidss_crtc_state *tstate = to_tidss_crtc_state(state);
	const struct dispc_bus_format *fmt;

	fmt = dispc_vp_find_bus_fmt(dispc, hw_videoport, tstate->bus_format,
				    tstate->bus_flags);
	if (!fmt) {
		dev_dbg(dispc->dev, "%s: Unsupported bus format: %u\n",
			__func__, tstate->bus_format);
		return -EINVAL;
	}

	if (dispc->feat->vp_bus_type[hw_videoport] != DISPC_VP_OLDI &&
	    fmt->is_oldi_fmt) {
		dev_dbg(dispc->dev, "%s: %s is not OLDI-port\n",
			__func__, dispc->feat->vp_name[hw_videoport]);
		return -EINVAL;
	}

	return 0;
}

static void dispc_oldi_tx_power(struct dispc_device *dispc, bool power)
{
	u32 val = power ? 0 : OLDI_PWRDN_TX;

	if (WARN_ON(!dispc->oldi_io_ctrl))
		return;

	regmap_update_bits(dispc->oldi_io_ctrl, OLDI_DAT0_IO_CTRL,
			   OLDI_PWRDN_TX, val);
	regmap_update_bits(dispc->oldi_io_ctrl, OLDI_DAT1_IO_CTRL,
			   OLDI_PWRDN_TX, val);
	regmap_update_bits(dispc->oldi_io_ctrl, OLDI_DAT2_IO_CTRL,
			   OLDI_PWRDN_TX, val);
	regmap_update_bits(dispc->oldi_io_ctrl, OLDI_DAT3_IO_CTRL,
			   OLDI_PWRDN_TX, val);
	regmap_update_bits(dispc->oldi_io_ctrl, OLDI_CLK_IO_CTRL,
			   OLDI_PWRDN_TX, val);
}

static void dispc_set_num_datalines(struct dispc_device *dispc,
				    u32 hw_videoport, int num_lines)
{
	int v;

	switch (num_lines) {
	case 12:
		v = 0; break;
	case 16:
		v = 1; break;
	case 18:
		v = 2; break;
	case 24:
		v = 3; break;
	case 30:
		v = 4; break;
	case 36:
		v = 5; break;
	default:
		WARN_ON(1);
		v = 3;
	}

	VP_REG_FLD_MOD(dispc, hw_videoport, DISPC_VP_CONTROL, v, 10, 8);
}

static void dispc_enable_oldi(struct dispc_device *dispc, u32 hw_videoport,
			      const struct dispc_bus_format *fmt)
{
	u32 oldi_cfg = 0;
	u32 oldi_reset_bit = BIT(5 + hw_videoport);
	int count = 0;

	/*
	 * For the moment DUALMODESYNC, MASTERSLAVE, MODE, and SRC
	 * bits of DISPC_VP_DSS_OLDI_CFG are set statically to 0.
	 */

	if (fmt->data_width == 24)
		oldi_cfg |= BIT(8); /* MSB */
	else if (fmt->data_width != 18)
		dev_warn(dispc->dev, "%s: %d port width not supported\n",
			 __func__, fmt->data_width);

	oldi_cfg |= BIT(7); /* DEPOL */

	oldi_cfg = FLD_MOD(oldi_cfg, fmt->oldi_mode_reg_val, 3, 1);

	oldi_cfg |= BIT(12); /* SOFTRST */

	oldi_cfg |= BIT(0); /* ENABLE */

	dispc_vp_write(dispc, hw_videoport, DISPC_VP_DSS_OLDI_CFG, oldi_cfg);

	while (!(oldi_reset_bit & dispc_read(dispc, DSS_SYSSTATUS)) &&
	       count < 10000)
		count++;

	if (!(oldi_reset_bit & dispc_read(dispc, DSS_SYSSTATUS)))
		dev_warn(dispc->dev, "%s: timeout waiting OLDI reset done\n",
			 __func__);
}

void dispc_vp_prepare(struct dispc_device *dispc, u32 hw_videoport,
		      const struct drm_crtc_state *state)
{
	const struct tidss_crtc_state *tstate = to_tidss_crtc_state(state);
	const struct dispc_bus_format *fmt;

	fmt = dispc_vp_find_bus_fmt(dispc, hw_videoport, tstate->bus_format,
				    tstate->bus_flags);

	if (WARN_ON(!fmt))
		return;

	if (dispc->feat->vp_bus_type[hw_videoport] == DISPC_VP_OLDI) {
		dispc_oldi_tx_power(dispc, true);

		dispc_enable_oldi(dispc, hw_videoport, fmt);
	}
}

void dispc_vp_enable(struct dispc_device *dispc, u32 hw_videoport,
		     const struct drm_crtc_state *state)
{
	const struct drm_display_mode *mode = &state->adjusted_mode;
	const struct tidss_crtc_state *tstate = to_tidss_crtc_state(state);
	bool align, onoff, rf, ieo, ipc, ihs, ivs;
	const struct dispc_bus_format *fmt;
	u32 hsw, hfp, hbp, vsw, vfp, vbp;

	fmt = dispc_vp_find_bus_fmt(dispc, hw_videoport, tstate->bus_format,
				    tstate->bus_flags);

	if (WARN_ON(!fmt))
		return;

	dispc_set_num_datalines(dispc, hw_videoport, fmt->data_width);

	hfp = mode->hsync_start - mode->hdisplay;
	hsw = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;

	vfp = mode->vsync_start - mode->vdisplay;
	vsw = mode->vsync_end - mode->vsync_start;
	vbp = mode->vtotal - mode->vsync_end;

	dispc_vp_write(dispc, hw_videoport, DISPC_VP_TIMING_H,
		       FLD_VAL(hsw - 1, 7, 0) |
		       FLD_VAL(hfp - 1, 19, 8) |
		       FLD_VAL(hbp - 1, 31, 20));

	dispc_vp_write(dispc, hw_videoport, DISPC_VP_TIMING_V,
		       FLD_VAL(vsw - 1, 7, 0) |
		       FLD_VAL(vfp, 19, 8) |
		       FLD_VAL(vbp, 31, 20));

	ivs = !!(mode->flags & DRM_MODE_FLAG_NVSYNC);

	ihs = !!(mode->flags & DRM_MODE_FLAG_NHSYNC);

	ieo = !!(tstate->bus_flags & DRM_BUS_FLAG_DE_LOW);

	ipc = !!(tstate->bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE);

	/* always use the 'rf' setting */
	onoff = true;

	rf = !!(tstate->bus_flags & DRM_BUS_FLAG_SYNC_DRIVE_POSEDGE);

	/* always use aligned syncs */
	align = true;

	/* always use DE_HIGH for OLDI */
	if (dispc->feat->vp_bus_type[hw_videoport] == DISPC_VP_OLDI)
		ieo = false;

	dispc_vp_write(dispc, hw_videoport, DISPC_VP_POL_FREQ,
		       FLD_VAL(align, 18, 18) |
		       FLD_VAL(onoff, 17, 17) |
		       FLD_VAL(rf, 16, 16) |
		       FLD_VAL(ieo, 15, 15) |
		       FLD_VAL(ipc, 14, 14) |
		       FLD_VAL(ihs, 13, 13) |
		       FLD_VAL(ivs, 12, 12));

	dispc_vp_write(dispc, hw_videoport, DISPC_VP_SIZE_SCREEN,
		       FLD_VAL(mode->hdisplay - 1, 11, 0) |
		       FLD_VAL(mode->vdisplay - 1, 27, 16));

	VP_REG_FLD_MOD(dispc, hw_videoport, DISPC_VP_CONTROL, 1, 0, 0);
}

void dispc_vp_disable(struct dispc_device *dispc, u32 hw_videoport)
{
	VP_REG_FLD_MOD(dispc, hw_videoport, DISPC_VP_CONTROL, 0, 0, 0);
}

void dispc_vp_unprepare(struct dispc_device *dispc, u32 hw_videoport)
{
	if (dispc->feat->vp_bus_type[hw_videoport] == DISPC_VP_OLDI) {
		dispc_vp_write(dispc, hw_videoport, DISPC_VP_DSS_OLDI_CFG, 0);

		dispc_oldi_tx_power(dispc, false);
	}
}

bool dispc_vp_go_busy(struct dispc_device *dispc, u32 hw_videoport)
{
	return VP_REG_GET(dispc, hw_videoport, DISPC_VP_CONTROL, 5, 5);
}

void dispc_vp_go(struct dispc_device *dispc, u32 hw_videoport)
{
	WARN_ON(VP_REG_GET(dispc, hw_videoport, DISPC_VP_CONTROL, 5, 5));
	VP_REG_FLD_MOD(dispc, hw_videoport, DISPC_VP_CONTROL, 1, 5, 5);
}

enum c8_to_c12_mode { C8_TO_C12_REPLICATE, C8_TO_C12_MAX, C8_TO_C12_MIN };

static u16 c8_to_c12(u8 c8, enum c8_to_c12_mode mode)
{
	u16 c12;

	c12 = c8 << 4;

	switch (mode) {
	case C8_TO_C12_REPLICATE:
		/* Copy c8 4 MSB to 4 LSB for full scale c12 */
		c12 |= c8 >> 4;
		break;
	case C8_TO_C12_MAX:
		c12 |= 0xF;
		break;
	default:
	case C8_TO_C12_MIN:
		break;
	}

	return c12;
}

static u64 argb8888_to_argb12121212(u32 argb8888, enum c8_to_c12_mode m)
{
	u8 a, r, g, b;
	u64 v;

	a = (argb8888 >> 24) & 0xff;
	r = (argb8888 >> 16) & 0xff;
	g = (argb8888 >> 8) & 0xff;
	b = (argb8888 >> 0) & 0xff;

	v = ((u64)c8_to_c12(a, m) << 36) | ((u64)c8_to_c12(r, m) << 24) |
		((u64)c8_to_c12(g, m) << 12) | (u64)c8_to_c12(b, m);

	return v;
}

static void dispc_vp_set_default_color(struct dispc_device *dispc,
				       u32 hw_videoport, u32 default_color)
{
	u64 v;

	v = argb8888_to_argb12121212(default_color, C8_TO_C12_REPLICATE);

	dispc_ovr_write(dispc, hw_videoport,
			DISPC_OVR_DEFAULT_COLOR, v & 0xffffffff);
	dispc_ovr_write(dispc, hw_videoport,
			DISPC_OVR_DEFAULT_COLOR2, (v >> 32) & 0xffff);
}

enum drm_mode_status dispc_vp_mode_valid(struct dispc_device *dispc,
					 u32 hw_videoport,
					 const struct drm_display_mode *mode)
{
	u32 hsw, hfp, hbp, vsw, vfp, vbp;
	enum dispc_vp_bus_type bus_type;
	int max_pclk;

	bus_type = dispc->feat->vp_bus_type[hw_videoport];

	max_pclk = dispc->feat->max_pclk_khz[bus_type];

	if (WARN_ON(max_pclk == 0))
		return MODE_BAD;

	if (mode->clock < dispc->feat->min_pclk_khz)
		return MODE_CLOCK_LOW;

	if (mode->clock > max_pclk)
		return MODE_CLOCK_HIGH;

	if (mode->hdisplay > 4096)
		return MODE_BAD;

	if (mode->vdisplay > 4096)
		return MODE_BAD;

	/* TODO: add interlace support */
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;

	/*
	 * Enforce the output width is divisible by 2. Actually this
	 * is only needed in following cases:
	 * - YUV output selected (BT656, BT1120)
	 * - Dithering enabled
	 * - TDM with TDMCycleFormat == 3
	 * But for simplicity we enforce that always.
	 */
	if ((mode->hdisplay % 2) != 0)
		return MODE_BAD_HVALUE;

	hfp = mode->hsync_start - mode->hdisplay;
	hsw = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;

	vfp = mode->vsync_start - mode->vdisplay;
	vsw = mode->vsync_end - mode->vsync_start;
	vbp = mode->vtotal - mode->vsync_end;

	if (hsw < 1 || hsw > 256 ||
	    hfp < 1 || hfp > 4096 ||
	    hbp < 1 || hbp > 4096)
		return MODE_BAD_HVALUE;

	if (vsw < 1 || vsw > 256 ||
	    vfp > 4095 || vbp > 4095)
		return MODE_BAD_VVALUE;

	if (dispc->memory_bandwidth_limit) {
		const unsigned int bpp = 4;
		u64 bandwidth;

		bandwidth = 1000 * mode->clock;
		bandwidth = bandwidth * mode->hdisplay * mode->vdisplay * bpp;
		bandwidth = div_u64(bandwidth, mode->htotal * mode->vtotal);

		if (dispc->memory_bandwidth_limit < bandwidth)
			return MODE_BAD;
	}

	return MODE_OK;
}

int dispc_vp_enable_clk(struct dispc_device *dispc, u32 hw_videoport)
{
	int ret = clk_prepare_enable(dispc->vp_clk[hw_videoport]);

	if (ret)
		dev_err(dispc->dev, "%s: enabling clk failed: %d\n", __func__,
			ret);

	return ret;
}

void dispc_vp_disable_clk(struct dispc_device *dispc, u32 hw_videoport)
{
	clk_disable_unprepare(dispc->vp_clk[hw_videoport]);
}

/*
 * Calculate the percentage difference between the requested pixel clock rate
 * and the effective rate resulting from calculating the clock divider value.
 */
static
unsigned int dispc_pclk_diff(unsigned long rate, unsigned long real_rate)
{
	int r = rate / 100, rr = real_rate / 100;

	return (unsigned int)(abs(((rr - r) * 100) / r));
}

int dispc_vp_set_clk_rate(struct dispc_device *dispc, u32 hw_videoport,
			  unsigned long rate)
{
	int r;
	unsigned long new_rate;

	r = clk_set_rate(dispc->vp_clk[hw_videoport], rate);
	if (r) {
		dev_err(dispc->dev, "vp%d: failed to set clk rate to %lu\n",
			hw_videoport, rate);
		return r;
	}

	new_rate = clk_get_rate(dispc->vp_clk[hw_videoport]);

	if (dispc_pclk_diff(rate, new_rate) > 5)
		dev_warn(dispc->dev,
			 "vp%d: Clock rate %lu differs over 5%% from requested %lu\n",
			 hw_videoport, new_rate, rate);

	dev_dbg(dispc->dev, "vp%d: new rate %lu Hz (requested %lu Hz)\n",
		hw_videoport, clk_get_rate(dispc->vp_clk[hw_videoport]), rate);

	return 0;
}

/* OVR */
static void dispc_k2g_ovr_set_plane(struct dispc_device *dispc,
				    u32 hw_plane, u32 hw_videoport,
				    u32 x, u32 y, u32 layer)
{
	/* On k2g there is only one plane and no need for ovr */
	dispc_vid_write(dispc, hw_plane, DISPC_VID_K2G_POSITION,
			x | (y << 16));
}

static void dispc_am65x_ovr_set_plane(struct dispc_device *dispc,
				      u32 hw_plane, u32 hw_videoport,
				      u32 x, u32 y, u32 layer)
{
	OVR_REG_FLD_MOD(dispc, hw_videoport, DISPC_OVR_ATTRIBUTES(layer),
			hw_plane, 4, 1);
	OVR_REG_FLD_MOD(dispc, hw_videoport, DISPC_OVR_ATTRIBUTES(layer),
			x, 17, 6);
	OVR_REG_FLD_MOD(dispc, hw_videoport, DISPC_OVR_ATTRIBUTES(layer),
			y, 30, 19);
}

static void dispc_j721e_ovr_set_plane(struct dispc_device *dispc,
				      u32 hw_plane, u32 hw_videoport,
				      u32 x, u32 y, u32 layer)
{
	OVR_REG_FLD_MOD(dispc, hw_videoport, DISPC_OVR_ATTRIBUTES(layer),
			hw_plane, 4, 1);
	OVR_REG_FLD_MOD(dispc, hw_videoport, DISPC_OVR_ATTRIBUTES2(layer),
			x, 13, 0);
	OVR_REG_FLD_MOD(dispc, hw_videoport, DISPC_OVR_ATTRIBUTES2(layer),
			y, 29, 16);
}

void dispc_ovr_set_plane(struct dispc_device *dispc, u32 hw_plane,
			 u32 hw_videoport, u32 x, u32 y, u32 layer)
{
	switch (dispc->feat->subrev) {
	case DISPC_K2G:
		dispc_k2g_ovr_set_plane(dispc, hw_plane, hw_videoport,
					x, y, layer);
		break;
	case DISPC_AM65X:
		dispc_am65x_ovr_set_plane(dispc, hw_plane, hw_videoport,
					  x, y, layer);
		break;
	case DISPC_J721E:
		dispc_j721e_ovr_set_plane(dispc, hw_plane, hw_videoport,
					  x, y, layer);
		break;
	default:
		WARN_ON(1);
		break;
	}
}

void dispc_ovr_enable_layer(struct dispc_device *dispc,
			    u32 hw_videoport, u32 layer, bool enable)
{
	if (dispc->feat->subrev == DISPC_K2G)
		return;

	OVR_REG_FLD_MOD(dispc, hw_videoport, DISPC_OVR_ATTRIBUTES(layer),
			!!enable, 0, 0);
}

/* CSC */
enum csc_ctm {
	CSC_RR, CSC_RG, CSC_RB,
	CSC_GR, CSC_GG, CSC_GB,
	CSC_BR, CSC_BG, CSC_BB,
};

enum csc_yuv2rgb {
	CSC_RY, CSC_RCB, CSC_RCR,
	CSC_GY, CSC_GCB, CSC_GCR,
	CSC_BY, CSC_BCB, CSC_BCR,
};

enum csc_rgb2yuv {
	CSC_YR,  CSC_YG,  CSC_YB,
	CSC_CBR, CSC_CBG, CSC_CBB,
	CSC_CRR, CSC_CRG, CSC_CRB,
};

struct dispc_csc_coef {
	void (*to_regval)(const struct dispc_csc_coef *csc, u32 *regval);
	int m[9];
	int preoffset[3];
	int postoffset[3];
	enum { CLIP_LIMITED_RANGE = 0, CLIP_FULL_RANGE = 1, } cliping;
	const char *name;
};

#define DISPC_CSC_REGVAL_LEN 8

static
void dispc_csc_offset_regval(const struct dispc_csc_coef *csc, u32 *regval)
{
#define OVAL(x, y) (FLD_VAL(x, 15, 3) | FLD_VAL(y, 31, 19))
	regval[5] = OVAL(csc->preoffset[0], csc->preoffset[1]);
	regval[6] = OVAL(csc->preoffset[2], csc->postoffset[0]);
	regval[7] = OVAL(csc->postoffset[1], csc->postoffset[2]);
#undef OVAL
}

#define CVAL(x, y) (FLD_VAL(x, 10, 0) | FLD_VAL(y, 26, 16))
static
void dispc_csc_yuv2rgb_regval(const struct dispc_csc_coef *csc, u32 *regval)
{
	regval[0] = CVAL(csc->m[CSC_RY], csc->m[CSC_RCR]);
	regval[1] = CVAL(csc->m[CSC_RCB], csc->m[CSC_GY]);
	regval[2] = CVAL(csc->m[CSC_GCR], csc->m[CSC_GCB]);
	regval[3] = CVAL(csc->m[CSC_BY], csc->m[CSC_BCR]);
	regval[4] = CVAL(csc->m[CSC_BCB], 0);

	dispc_csc_offset_regval(csc, regval);
}

__maybe_unused static
void dispc_csc_rgb2yuv_regval(const struct dispc_csc_coef *csc, u32 *regval)
{
	regval[0] = CVAL(csc->m[CSC_YR], csc->m[CSC_YG]);
	regval[1] = CVAL(csc->m[CSC_YB], csc->m[CSC_CRR]);
	regval[2] = CVAL(csc->m[CSC_CRG], csc->m[CSC_CRB]);
	regval[3] = CVAL(csc->m[CSC_CBR], csc->m[CSC_CBG]);
	regval[4] = CVAL(csc->m[CSC_CBB], 0);

	dispc_csc_offset_regval(csc, regval);
}

static void dispc_csc_cpr_regval(const struct dispc_csc_coef *csc,
				 u32 *regval)
{
	regval[0] = CVAL(csc->m[CSC_RR], csc->m[CSC_RG]);
	regval[1] = CVAL(csc->m[CSC_RB], csc->m[CSC_GR]);
	regval[2] = CVAL(csc->m[CSC_GG], csc->m[CSC_GB]);
	regval[3] = CVAL(csc->m[CSC_BR], csc->m[CSC_BG]);
	regval[4] = CVAL(csc->m[CSC_BB], 0);

	dispc_csc_offset_regval(csc, regval);
}

#undef CVAL

static void dispc_k2g_vid_write_csc(struct dispc_device *dispc, u32 hw_plane,
				    const struct dispc_csc_coef *csc)
{
	static const u16 dispc_vid_csc_coef_reg[] = {
		DISPC_VID_CSC_COEF(0), DISPC_VID_CSC_COEF(1),
		DISPC_VID_CSC_COEF(2), DISPC_VID_CSC_COEF(3),
		DISPC_VID_CSC_COEF(4), DISPC_VID_CSC_COEF(5),
		DISPC_VID_CSC_COEF(6), /* K2G has no post offset support */
	};
	u32 regval[DISPC_CSC_REGVAL_LEN];
	unsigned int i;

	csc->to_regval(csc, regval);

	if (regval[7] != 0)
		dev_warn(dispc->dev, "%s: No post offset support for %s\n",
			 __func__, csc->name);

	for (i = 0; i < ARRAY_SIZE(dispc_vid_csc_coef_reg); i++)
		dispc_vid_write(dispc, hw_plane, dispc_vid_csc_coef_reg[i],
				regval[i]);
}

static void dispc_k3_vid_write_csc(struct dispc_device *dispc, u32 hw_plane,
				   const struct dispc_csc_coef *csc)
{
	static const u16 dispc_vid_csc_coef_reg[DISPC_CSC_REGVAL_LEN] = {
		DISPC_VID_CSC_COEF(0), DISPC_VID_CSC_COEF(1),
		DISPC_VID_CSC_COEF(2), DISPC_VID_CSC_COEF(3),
		DISPC_VID_CSC_COEF(4), DISPC_VID_CSC_COEF(5),
		DISPC_VID_CSC_COEF(6), DISPC_VID_CSC_COEF7,
	};
	u32 regval[DISPC_CSC_REGVAL_LEN];
	unsigned int i;

	csc->to_regval(csc, regval);

	for (i = 0; i < ARRAY_SIZE(dispc_vid_csc_coef_reg); i++)
		dispc_vid_write(dispc, hw_plane, dispc_vid_csc_coef_reg[i],
				regval[i]);
}

/* YUV -> RGB, ITU-R BT.601, full range */
static const struct dispc_csc_coef csc_yuv2rgb_bt601_full = {
	dispc_csc_yuv2rgb_regval,
	{ 256,   0,  358,	/* ry, rcb, rcr |1.000  0.000  1.402|*/
	  256, -88, -182,	/* gy, gcb, gcr |1.000 -0.344 -0.714|*/
	  256, 452,    0, },	/* by, bcb, bcr |1.000  1.772  0.000|*/
	{    0, -2048, -2048, },	/* full range */
	{    0,     0,     0, },
	CLIP_FULL_RANGE,
	"BT.601 Full",
};

/* YUV -> RGB, ITU-R BT.601, limited range */
static const struct dispc_csc_coef csc_yuv2rgb_bt601_lim = {
	dispc_csc_yuv2rgb_regval,
	{ 298,    0,  409,	/* ry, rcb, rcr |1.164  0.000  1.596|*/
	  298, -100, -208,	/* gy, gcb, gcr |1.164 -0.392 -0.813|*/
	  298,  516,    0, },	/* by, bcb, bcr |1.164  2.017  0.000|*/
	{ -256, -2048, -2048, },	/* limited range */
	{    0,     0,     0, },
	CLIP_FULL_RANGE,
	"BT.601 Limited",
};

/* YUV -> RGB, ITU-R BT.709, full range */
static const struct dispc_csc_coef csc_yuv2rgb_bt709_full = {
	dispc_csc_yuv2rgb_regval,
	{ 256,	  0,  402,	/* ry, rcb, rcr |1.000	0.000  1.570|*/
	  256,  -48, -120,	/* gy, gcb, gcr |1.000 -0.187 -0.467|*/
	  256,  475,    0, },	/* by, bcb, bcr |1.000	1.856  0.000|*/
	{    0, -2048, -2048, },	/* full range */
	{    0,     0,     0, },
	CLIP_FULL_RANGE,
	"BT.709 Full",
};

/* YUV -> RGB, ITU-R BT.709, limited range */
static const struct dispc_csc_coef csc_yuv2rgb_bt709_lim = {
	dispc_csc_yuv2rgb_regval,
	{ 298,    0,  459,	/* ry, rcb, rcr |1.164  0.000  1.793|*/
	  298,  -55, -136,	/* gy, gcb, gcr |1.164 -0.213 -0.533|*/
	  298,  541,    0, },	/* by, bcb, bcr |1.164  2.112  0.000|*/
	{ -256, -2048, -2048, },	/* limited range */
	{    0,     0,     0, },
	CLIP_FULL_RANGE,
	"BT.709 Limited",
};

static const struct {
	enum drm_color_encoding encoding;
	enum drm_color_range range;
	const struct dispc_csc_coef *csc;
} dispc_csc_table[] = {
	{ DRM_COLOR_YCBCR_BT601, DRM_COLOR_YCBCR_FULL_RANGE,
	  &csc_yuv2rgb_bt601_full, },
	{ DRM_COLOR_YCBCR_BT601, DRM_COLOR_YCBCR_LIMITED_RANGE,
	  &csc_yuv2rgb_bt601_lim, },
	{ DRM_COLOR_YCBCR_BT709, DRM_COLOR_YCBCR_FULL_RANGE,
	  &csc_yuv2rgb_bt709_full, },
	{ DRM_COLOR_YCBCR_BT709, DRM_COLOR_YCBCR_LIMITED_RANGE,
	  &csc_yuv2rgb_bt709_lim, },
};

static const
struct dispc_csc_coef *dispc_find_csc(enum drm_color_encoding encoding,
				      enum drm_color_range range)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dispc_csc_table); i++) {
		if (dispc_csc_table[i].encoding == encoding &&
		    dispc_csc_table[i].range == range) {
			return dispc_csc_table[i].csc;
		}
	}
	return NULL;
}

static void dispc_vid_csc_setup(struct dispc_device *dispc, u32 hw_plane,
				const struct drm_plane_state *state)
{
	const struct dispc_csc_coef *coef;

	coef = dispc_find_csc(state->color_encoding, state->color_range);
	if (!coef) {
		dev_err(dispc->dev, "%s: CSC (%u,%u) not found\n",
			__func__, state->color_encoding, state->color_range);
		return;
	}

	if (dispc->feat->subrev == DISPC_K2G)
		dispc_k2g_vid_write_csc(dispc, hw_plane, coef);
	else
		dispc_k3_vid_write_csc(dispc, hw_plane, coef);
}

static void dispc_vid_csc_enable(struct dispc_device *dispc, u32 hw_plane,
				 bool enable)
{
	VID_REG_FLD_MOD(dispc, hw_plane, DISPC_VID_ATTRIBUTES, !!enable, 9, 9);
}

/* SCALER */

static u32 dispc_calc_fir_inc(u32 in, u32 out)
{
	return (u32)div_u64(0x200000ull * in, out);
}

enum dispc_vid_fir_coef_set {
	DISPC_VID_FIR_COEF_HORIZ,
	DISPC_VID_FIR_COEF_HORIZ_UV,
	DISPC_VID_FIR_COEF_VERT,
	DISPC_VID_FIR_COEF_VERT_UV,
};

static void dispc_vid_write_fir_coefs(struct dispc_device *dispc,
				      u32 hw_plane,
				      enum dispc_vid_fir_coef_set coef_set,
				      const struct tidss_scale_coefs *coefs)
{
	static const u16 c0_regs[] = {
		[DISPC_VID_FIR_COEF_HORIZ] = DISPC_VID_FIR_COEFS_H0,
		[DISPC_VID_FIR_COEF_HORIZ_UV] = DISPC_VID_FIR_COEFS_H0_C,
		[DISPC_VID_FIR_COEF_VERT] = DISPC_VID_FIR_COEFS_V0,
		[DISPC_VID_FIR_COEF_VERT_UV] = DISPC_VID_FIR_COEFS_V0_C,
	};

	static const u16 c12_regs[] = {
		[DISPC_VID_FIR_COEF_HORIZ] = DISPC_VID_FIR_COEFS_H12,
		[DISPC_VID_FIR_COEF_HORIZ_UV] = DISPC_VID_FIR_COEFS_H12_C,
		[DISPC_VID_FIR_COEF_VERT] = DISPC_VID_FIR_COEFS_V12,
		[DISPC_VID_FIR_COEF_VERT_UV] = DISPC_VID_FIR_COEFS_V12_C,
	};

	const u16 c0_base = c0_regs[coef_set];
	const u16 c12_base = c12_regs[coef_set];
	int phase;

	if (!coefs) {
		dev_err(dispc->dev, "%s: No coefficients given.\n", __func__);
		return;
	}

	for (phase = 0; phase <= 8; ++phase) {
		u16 reg = c0_base + phase * 4;
		u16 c0 = coefs->c0[phase];

		dispc_vid_write(dispc, hw_plane, reg, c0);
	}

	for (phase = 0; phase <= 15; ++phase) {
		u16 reg = c12_base + phase * 4;
		s16 c1, c2;
		u32 c12;

		c1 = coefs->c1[phase];
		c2 = coefs->c2[phase];
		c12 = FLD_VAL(c1, 19, 10) | FLD_VAL(c2, 29, 20);

		dispc_vid_write(dispc, hw_plane, reg, c12);
	}
}

static bool dispc_fourcc_is_yuv(u32 fourcc)
{
	switch (fourcc) {
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_NV12:
		return true;
	default:
		return false;
	}
}

struct dispc_scaling_params {
	int xinc, yinc;
	u32 in_w, in_h, in_w_uv, in_h_uv;
	u32 fir_xinc, fir_yinc, fir_xinc_uv, fir_yinc_uv;
	bool scale_x, scale_y;
	const struct tidss_scale_coefs *xcoef, *ycoef, *xcoef_uv, *ycoef_uv;
	bool five_taps;
};

static int dispc_vid_calc_scaling(struct dispc_device *dispc,
				  const struct drm_plane_state *state,
				  struct dispc_scaling_params *sp,
				  bool lite_plane)
{
	const struct dispc_features_scaling *f = &dispc->feat->scaling;
	u32 fourcc = state->fb->format->format;
	u32 in_width_max_5tap = f->in_width_max_5tap_rgb;
	u32 in_width_max_3tap = f->in_width_max_3tap_rgb;
	u32 downscale_limit;
	u32 in_width_max;

	memset(sp, 0, sizeof(*sp));
	sp->xinc = 1;
	sp->yinc = 1;
	sp->in_w = state->src_w >> 16;
	sp->in_w_uv = sp->in_w;
	sp->in_h = state->src_h >> 16;
	sp->in_h_uv = sp->in_h;

	sp->scale_x = sp->in_w != state->crtc_w;
	sp->scale_y = sp->in_h != state->crtc_h;

	if (dispc_fourcc_is_yuv(fourcc)) {
		in_width_max_5tap = f->in_width_max_5tap_yuv;
		in_width_max_3tap = f->in_width_max_3tap_yuv;

		sp->in_w_uv >>= 1;
		sp->scale_x = true;

		if (fourcc == DRM_FORMAT_NV12) {
			sp->in_h_uv >>= 1;
			sp->scale_y = true;
		}
	}

	/* Skip the rest if no scaling is used */
	if ((!sp->scale_x && !sp->scale_y) || lite_plane)
		return 0;

	if (sp->in_w > in_width_max_5tap) {
		sp->five_taps = false;
		in_width_max = in_width_max_3tap;
		downscale_limit = f->downscale_limit_3tap;
	} else {
		sp->five_taps = true;
		in_width_max = in_width_max_5tap;
		downscale_limit = f->downscale_limit_5tap;
	}

	if (sp->scale_x) {
		sp->fir_xinc = dispc_calc_fir_inc(sp->in_w, state->crtc_w);

		if (sp->fir_xinc < dispc_calc_fir_inc(1, f->upscale_limit)) {
			dev_dbg(dispc->dev,
				"%s: X-scaling factor %u/%u > %u\n",
				__func__, state->crtc_w, state->src_w >> 16,
				f->upscale_limit);
			return -EINVAL;
		}

		if (sp->fir_xinc >= dispc_calc_fir_inc(downscale_limit, 1)) {
			sp->xinc = DIV_ROUND_UP(DIV_ROUND_UP(sp->in_w,
							     state->crtc_w),
						downscale_limit);

			if (sp->xinc > f->xinc_max) {
				dev_dbg(dispc->dev,
					"%s: X-scaling factor %u/%u < 1/%u\n",
					__func__, state->crtc_w,
					state->src_w >> 16,
					downscale_limit * f->xinc_max);
				return -EINVAL;
			}

			sp->in_w = (state->src_w >> 16) / sp->xinc;
		}

		while (sp->in_w > in_width_max) {
			sp->xinc++;
			sp->in_w = (state->src_w >> 16) / sp->xinc;
		}

		if (sp->xinc > f->xinc_max) {
			dev_dbg(dispc->dev,
				"%s: Too wide input buffer %u > %u\n", __func__,
				state->src_w >> 16, in_width_max * f->xinc_max);
			return -EINVAL;
		}

		/*
		 * We need even line length for YUV formats. Decimation
		 * can lead to odd length, so we need to make it even
		 * again.
		 */
		if (dispc_fourcc_is_yuv(fourcc))
			sp->in_w &= ~1;

		sp->fir_xinc = dispc_calc_fir_inc(sp->in_w, state->crtc_w);
	}

	if (sp->scale_y) {
		sp->fir_yinc = dispc_calc_fir_inc(sp->in_h, state->crtc_h);

		if (sp->fir_yinc < dispc_calc_fir_inc(1, f->upscale_limit)) {
			dev_dbg(dispc->dev,
				"%s: Y-scaling factor %u/%u > %u\n",
				__func__, state->crtc_h, state->src_h >> 16,
				f->upscale_limit);
			return -EINVAL;
		}

		if (sp->fir_yinc >= dispc_calc_fir_inc(downscale_limit, 1)) {
			sp->yinc = DIV_ROUND_UP(DIV_ROUND_UP(sp->in_h,
							     state->crtc_h),
						downscale_limit);

			sp->in_h /= sp->yinc;
			sp->fir_yinc = dispc_calc_fir_inc(sp->in_h,
							  state->crtc_h);
		}
	}

	dev_dbg(dispc->dev,
		"%s: %ux%u decim %ux%u -> %ux%u firinc %u.%03ux%u.%03u taps %u -> %ux%u\n",
		__func__, state->src_w >> 16, state->src_h >> 16,
		sp->xinc, sp->yinc, sp->in_w, sp->in_h,
		sp->fir_xinc / 0x200000u,
		((sp->fir_xinc & 0x1FFFFFu) * 999u) / 0x1FFFFFu,
		sp->fir_yinc / 0x200000u,
		((sp->fir_yinc & 0x1FFFFFu) * 999u) / 0x1FFFFFu,
		sp->five_taps ? 5 : 3,
		state->crtc_w, state->crtc_h);

	if (dispc_fourcc_is_yuv(fourcc)) {
		if (sp->scale_x) {
			sp->in_w_uv /= sp->xinc;
			sp->fir_xinc_uv = dispc_calc_fir_inc(sp->in_w_uv,
							     state->crtc_w);
			sp->xcoef_uv = tidss_get_scale_coefs(dispc->dev,
							     sp->fir_xinc_uv,
							     true);
		}
		if (sp->scale_y) {
			sp->in_h_uv /= sp->yinc;
			sp->fir_yinc_uv = dispc_calc_fir_inc(sp->in_h_uv,
							     state->crtc_h);
			sp->ycoef_uv = tidss_get_scale_coefs(dispc->dev,
							     sp->fir_yinc_uv,
							     sp->five_taps);
		}
	}

	if (sp->scale_x)
		sp->xcoef = tidss_get_scale_coefs(dispc->dev, sp->fir_xinc,
						  true);

	if (sp->scale_y)
		sp->ycoef = tidss_get_scale_coefs(dispc->dev, sp->fir_yinc,
						  sp->five_taps);

	return 0;
}

static void dispc_vid_set_scaling(struct dispc_device *dispc,
				  u32 hw_plane,
				  struct dispc_scaling_params *sp,
				  u32 fourcc)
{
	/* HORIZONTAL RESIZE ENABLE */
	VID_REG_FLD_MOD(dispc, hw_plane, DISPC_VID_ATTRIBUTES,
			sp->scale_x, 7, 7);

	/* VERTICAL RESIZE ENABLE */
	VID_REG_FLD_MOD(dispc, hw_plane, DISPC_VID_ATTRIBUTES,
			sp->scale_y, 8, 8);

	/* Skip the rest if no scaling is used */
	if (!sp->scale_x && !sp->scale_y)
		return;

	/* VERTICAL 5-TAPS  */
	VID_REG_FLD_MOD(dispc, hw_plane, DISPC_VID_ATTRIBUTES,
			sp->five_taps, 21, 21);

	if (dispc_fourcc_is_yuv(fourcc)) {
		if (sp->scale_x) {
			dispc_vid_write(dispc, hw_plane, DISPC_VID_FIRH2,
					sp->fir_xinc_uv);
			dispc_vid_write_fir_coefs(dispc, hw_plane,
						  DISPC_VID_FIR_COEF_HORIZ_UV,
						  sp->xcoef_uv);
		}
		if (sp->scale_y) {
			dispc_vid_write(dispc, hw_plane, DISPC_VID_FIRV2,
					sp->fir_yinc_uv);
			dispc_vid_write_fir_coefs(dispc, hw_plane,
						  DISPC_VID_FIR_COEF_VERT_UV,
						  sp->ycoef_uv);
		}
	}

	if (sp->scale_x) {
		dispc_vid_write(dispc, hw_plane, DISPC_VID_FIRH, sp->fir_xinc);
		dispc_vid_write_fir_coefs(dispc, hw_plane,
					  DISPC_VID_FIR_COEF_HORIZ,
					  sp->xcoef);
	}

	if (sp->scale_y) {
		dispc_vid_write(dispc, hw_plane, DISPC_VID_FIRV, sp->fir_yinc);
		dispc_vid_write_fir_coefs(dispc, hw_plane,
					  DISPC_VID_FIR_COEF_VERT, sp->ycoef);
	}
}

/* OTHER */

static const struct {
	u32 fourcc;
	u8 dss_code;
} dispc_color_formats[] = {
	{ DRM_FORMAT_ARGB4444, 0x0, },
	{ DRM_FORMAT_ABGR4444, 0x1, },
	{ DRM_FORMAT_RGBA4444, 0x2, },

	{ DRM_FORMAT_RGB565, 0x3, },
	{ DRM_FORMAT_BGR565, 0x4, },

	{ DRM_FORMAT_ARGB1555, 0x5, },
	{ DRM_FORMAT_ABGR1555, 0x6, },

	{ DRM_FORMAT_ARGB8888, 0x7, },
	{ DRM_FORMAT_ABGR8888, 0x8, },
	{ DRM_FORMAT_RGBA8888, 0x9, },
	{ DRM_FORMAT_BGRA8888, 0xa, },

	{ DRM_FORMAT_RGB888, 0xb, },
	{ DRM_FORMAT_BGR888, 0xc, },

	{ DRM_FORMAT_ARGB2101010, 0xe, },
	{ DRM_FORMAT_ABGR2101010, 0xf, },

	{ DRM_FORMAT_XRGB4444, 0x20, },
	{ DRM_FORMAT_XBGR4444, 0x21, },
	{ DRM_FORMAT_RGBX4444, 0x22, },

	{ DRM_FORMAT_XRGB1555, 0x25, },
	{ DRM_FORMAT_XBGR1555, 0x26, },

	{ DRM_FORMAT_XRGB8888, 0x27, },
	{ DRM_FORMAT_XBGR8888, 0x28, },
	{ DRM_FORMAT_RGBX8888, 0x29, },
	{ DRM_FORMAT_BGRX8888, 0x2a, },

	{ DRM_FORMAT_XRGB2101010, 0x2e, },
	{ DRM_FORMAT_XBGR2101010, 0x2f, },

	{ DRM_FORMAT_YUYV, 0x3e, },
	{ DRM_FORMAT_UYVY, 0x3f, },

	{ DRM_FORMAT_NV12, 0x3d, },
};

static void dispc_plane_set_pixel_format(struct dispc_device *dispc,
					 u32 hw_plane, u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dispc_color_formats); ++i) {
		if (dispc_color_formats[i].fourcc == fourcc) {
			VID_REG_FLD_MOD(dispc, hw_plane, DISPC_VID_ATTRIBUTES,
					dispc_color_formats[i].dss_code,
					6, 1);
			return;
		}
	}

	WARN_ON(1);
}

const u32 *dispc_plane_formats(struct dispc_device *dispc, unsigned int *len)
{
	WARN_ON(!dispc->fourccs);

	*len = dispc->num_fourccs;

	return dispc->fourccs;
}

static s32 pixinc(int pixels, u8 ps)
{
	if (pixels == 1)
		return 1;
	else if (pixels > 1)
		return 1 + (pixels - 1) * ps;
	else if (pixels < 0)
		return 1 - (-pixels + 1) * ps;

	WARN_ON(1);
	return 0;
}

int dispc_plane_check(struct dispc_device *dispc, u32 hw_plane,
		      const struct drm_plane_state *state,
		      u32 hw_videoport)
{
	bool lite = dispc->feat->vid_lite[hw_plane];
	u32 fourcc = state->fb->format->format;
	bool need_scaling = state->src_w >> 16 != state->crtc_w ||
		state->src_h >> 16 != state->crtc_h;
	struct dispc_scaling_params scaling;
	int ret;

	if (dispc_fourcc_is_yuv(fourcc)) {
		if (!dispc_find_csc(state->color_encoding,
				    state->color_range)) {
			dev_dbg(dispc->dev,
				"%s: Unsupported CSC (%u,%u) for HW plane %u\n",
				__func__, state->color_encoding,
				state->color_range, hw_plane);
			return -EINVAL;
		}
	}

	if (need_scaling) {
		if (lite) {
			dev_dbg(dispc->dev,
				"%s: Lite plane %u can't scale %ux%u!=%ux%u\n",
				__func__, hw_plane,
				state->src_w >> 16, state->src_h >> 16,
				state->crtc_w, state->crtc_h);
			return -EINVAL;
		}
		ret = dispc_vid_calc_scaling(dispc, state, &scaling, false);
		if (ret)
			return ret;
	}

	return 0;
}

static
dma_addr_t dispc_plane_state_dma_addr(const struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;
	struct drm_gem_dma_object *gem;
	u32 x = state->src_x >> 16;
	u32 y = state->src_y >> 16;

	gem = drm_fb_dma_get_gem_obj(state->fb, 0);

	return gem->dma_addr + fb->offsets[0] + x * fb->format->cpp[0] +
		y * fb->pitches[0];
}

static
dma_addr_t dispc_plane_state_p_uv_addr(const struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;
	struct drm_gem_dma_object *gem;
	u32 x = state->src_x >> 16;
	u32 y = state->src_y >> 16;

	if (WARN_ON(state->fb->format->num_planes != 2))
		return 0;

	gem = drm_fb_dma_get_gem_obj(fb, 1);

	return gem->dma_addr + fb->offsets[1] +
		(x * fb->format->cpp[1] / fb->format->hsub) +
		(y * fb->pitches[1] / fb->format->vsub);
}

int dispc_plane_setup(struct dispc_device *dispc, u32 hw_plane,
		      const struct drm_plane_state *state,
		      u32 hw_videoport)
{
	bool lite = dispc->feat->vid_lite[hw_plane];
	u32 fourcc = state->fb->format->format;
	u16 cpp = state->fb->format->cpp[0];
	u32 fb_width = state->fb->pitches[0] / cpp;
	dma_addr_t dma_addr = dispc_plane_state_dma_addr(state);
	struct dispc_scaling_params scale;

	dispc_vid_calc_scaling(dispc, state, &scale, lite);

	dispc_plane_set_pixel_format(dispc, hw_plane, fourcc);

	dispc_vid_write(dispc, hw_plane, DISPC_VID_BA_0, dma_addr & 0xffffffff);
	dispc_vid_write(dispc, hw_plane, DISPC_VID_BA_EXT_0, (u64)dma_addr >> 32);
	dispc_vid_write(dispc, hw_plane, DISPC_VID_BA_1, dma_addr & 0xffffffff);
	dispc_vid_write(dispc, hw_plane, DISPC_VID_BA_EXT_1, (u64)dma_addr >> 32);

	dispc_vid_write(dispc, hw_plane, DISPC_VID_PICTURE_SIZE,
			(scale.in_w - 1) | ((scale.in_h - 1) << 16));

	/* For YUV422 format we use the macropixel size for pixel inc */
	if (fourcc == DRM_FORMAT_YUYV || fourcc == DRM_FORMAT_UYVY)
		dispc_vid_write(dispc, hw_plane, DISPC_VID_PIXEL_INC,
				pixinc(scale.xinc, cpp * 2));
	else
		dispc_vid_write(dispc, hw_plane, DISPC_VID_PIXEL_INC,
				pixinc(scale.xinc, cpp));

	dispc_vid_write(dispc, hw_plane, DISPC_VID_ROW_INC,
			pixinc(1 + (scale.yinc * fb_width -
				    scale.xinc * scale.in_w),
			       cpp));

	if (state->fb->format->num_planes == 2) {
		u16 cpp_uv = state->fb->format->cpp[1];
		u32 fb_width_uv = state->fb->pitches[1] / cpp_uv;
		dma_addr_t p_uv_addr = dispc_plane_state_p_uv_addr(state);

		dispc_vid_write(dispc, hw_plane,
				DISPC_VID_BA_UV_0, p_uv_addr & 0xffffffff);
		dispc_vid_write(dispc, hw_plane,
				DISPC_VID_BA_UV_EXT_0, (u64)p_uv_addr >> 32);
		dispc_vid_write(dispc, hw_plane,
				DISPC_VID_BA_UV_1, p_uv_addr & 0xffffffff);
		dispc_vid_write(dispc, hw_plane,
				DISPC_VID_BA_UV_EXT_1, (u64)p_uv_addr >> 32);

		dispc_vid_write(dispc, hw_plane, DISPC_VID_ROW_INC_UV,
				pixinc(1 + (scale.yinc * fb_width_uv -
					    scale.xinc * scale.in_w_uv),
				       cpp_uv));
	}

	if (!lite) {
		dispc_vid_write(dispc, hw_plane, DISPC_VID_SIZE,
				(state->crtc_w - 1) |
				((state->crtc_h - 1) << 16));

		dispc_vid_set_scaling(dispc, hw_plane, &scale, fourcc);
	}

	/* enable YUV->RGB color conversion */
	if (dispc_fourcc_is_yuv(fourcc)) {
		dispc_vid_csc_setup(dispc, hw_plane, state);
		dispc_vid_csc_enable(dispc, hw_plane, true);
	} else {
		dispc_vid_csc_enable(dispc, hw_plane, false);
	}

	dispc_vid_write(dispc, hw_plane, DISPC_VID_GLOBAL_ALPHA,
			0xFF & (state->alpha >> 8));

	if (state->pixel_blend_mode == DRM_MODE_BLEND_PREMULTI)
		VID_REG_FLD_MOD(dispc, hw_plane, DISPC_VID_ATTRIBUTES, 1,
				28, 28);
	else
		VID_REG_FLD_MOD(dispc, hw_plane, DISPC_VID_ATTRIBUTES, 0,
				28, 28);

	return 0;
}

int dispc_plane_enable(struct dispc_device *dispc, u32 hw_plane, bool enable)
{
	VID_REG_FLD_MOD(dispc, hw_plane, DISPC_VID_ATTRIBUTES, !!enable, 0, 0);

	return 0;
}

static u32 dispc_vid_get_fifo_size(struct dispc_device *dispc, u32 hw_plane)
{
	return VID_REG_GET(dispc, hw_plane, DISPC_VID_BUF_SIZE_STATUS, 15, 0);
}

static void dispc_vid_set_mflag_threshold(struct dispc_device *dispc,
					  u32 hw_plane, u32 low, u32 high)
{
	dispc_vid_write(dispc, hw_plane, DISPC_VID_MFLAG_THRESHOLD,
			FLD_VAL(high, 31, 16) | FLD_VAL(low, 15, 0));
}

static void dispc_vid_set_buf_threshold(struct dispc_device *dispc,
					u32 hw_plane, u32 low, u32 high)
{
	dispc_vid_write(dispc, hw_plane, DISPC_VID_BUF_THRESHOLD,
			FLD_VAL(high, 31, 16) | FLD_VAL(low, 15, 0));
}

static void dispc_k2g_plane_init(struct dispc_device *dispc)
{
	unsigned int hw_plane;

	dev_dbg(dispc->dev, "%s()\n", __func__);

	/* MFLAG_CTRL = ENABLED */
	REG_FLD_MOD(dispc, DISPC_GLOBAL_MFLAG_ATTRIBUTE, 2, 1, 0);
	/* MFLAG_START = MFLAGNORMALSTARTMODE */
	REG_FLD_MOD(dispc, DISPC_GLOBAL_MFLAG_ATTRIBUTE, 0, 6, 6);

	for (hw_plane = 0; hw_plane < dispc->feat->num_planes; hw_plane++) {
		u32 size = dispc_vid_get_fifo_size(dispc, hw_plane);
		u32 thr_low, thr_high;
		u32 mflag_low, mflag_high;
		u32 preload;

		thr_high = size - 1;
		thr_low = size / 2;

		mflag_high = size * 2 / 3;
		mflag_low = size / 3;

		preload = thr_low;

		dev_dbg(dispc->dev,
			"%s: bufsize %u, buf_threshold %u/%u, mflag threshold %u/%u preload %u\n",
			dispc->feat->vid_name[hw_plane],
			size,
			thr_high, thr_low,
			mflag_high, mflag_low,
			preload);

		dispc_vid_set_buf_threshold(dispc, hw_plane,
					    thr_low, thr_high);
		dispc_vid_set_mflag_threshold(dispc, hw_plane,
					      mflag_low, mflag_high);

		dispc_vid_write(dispc, hw_plane, DISPC_VID_PRELOAD, preload);

		/*
		 * Prefetch up to fifo high-threshold value to minimize the
		 * possibility of underflows. Note that this means the PRELOAD
		 * register is ignored.
		 */
		VID_REG_FLD_MOD(dispc, hw_plane, DISPC_VID_ATTRIBUTES, 1,
				19, 19);
	}
}

static void dispc_k3_plane_init(struct dispc_device *dispc)
{
	unsigned int hw_plane;
	u32 cba_lo_pri = 1;
	u32 cba_hi_pri = 0;

	dev_dbg(dispc->dev, "%s()\n", __func__);

	REG_FLD_MOD(dispc, DSS_CBA_CFG, cba_lo_pri, 2, 0);
	REG_FLD_MOD(dispc, DSS_CBA_CFG, cba_hi_pri, 5, 3);

	/* MFLAG_CTRL = ENABLED */
	REG_FLD_MOD(dispc, DISPC_GLOBAL_MFLAG_ATTRIBUTE, 2, 1, 0);
	/* MFLAG_START = MFLAGNORMALSTARTMODE */
	REG_FLD_MOD(dispc, DISPC_GLOBAL_MFLAG_ATTRIBUTE, 0, 6, 6);

	for (hw_plane = 0; hw_plane < dispc->feat->num_planes; hw_plane++) {
		u32 size = dispc_vid_get_fifo_size(dispc, hw_plane);
		u32 thr_low, thr_high;
		u32 mflag_low, mflag_high;
		u32 preload;

		thr_high = size - 1;
		thr_low = size / 2;

		mflag_high = size * 2 / 3;
		mflag_low = size / 3;

		preload = thr_low;

		dev_dbg(dispc->dev,
			"%s: bufsize %u, buf_threshold %u/%u, mflag threshold %u/%u preload %u\n",
			dispc->feat->vid_name[hw_plane],
			size,
			thr_high, thr_low,
			mflag_high, mflag_low,
			preload);

		dispc_vid_set_buf_threshold(dispc, hw_plane,
					    thr_low, thr_high);
		dispc_vid_set_mflag_threshold(dispc, hw_plane,
					      mflag_low, mflag_high);

		dispc_vid_write(dispc, hw_plane, DISPC_VID_PRELOAD, preload);

		/* Prefech up to PRELOAD value */
		VID_REG_FLD_MOD(dispc, hw_plane, DISPC_VID_ATTRIBUTES, 0,
				19, 19);
	}
}

static void dispc_plane_init(struct dispc_device *dispc)
{
	switch (dispc->feat->subrev) {
	case DISPC_K2G:
		dispc_k2g_plane_init(dispc);
		break;
	case DISPC_AM65X:
	case DISPC_J721E:
		dispc_k3_plane_init(dispc);
		break;
	default:
		WARN_ON(1);
	}
}

static void dispc_vp_init(struct dispc_device *dispc)
{
	unsigned int i;

	dev_dbg(dispc->dev, "%s()\n", __func__);

	/* Enable the gamma Shadow bit-field for all VPs*/
	for (i = 0; i < dispc->feat->num_vps; i++)
		VP_REG_FLD_MOD(dispc, i, DISPC_VP_CONFIG, 1, 2, 2);
}

static void dispc_initial_config(struct dispc_device *dispc)
{
	dispc_plane_init(dispc);
	dispc_vp_init(dispc);

	/* Note: Hardcoded DPI routing on J721E for now */
	if (dispc->feat->subrev == DISPC_J721E) {
		dispc_write(dispc, DISPC_CONNECTIONS,
			    FLD_VAL(2, 3, 0) |		/* VP1 to DPI0 */
			    FLD_VAL(8, 7, 4)		/* VP3 to DPI1 */
			);
	}
}

static void dispc_k2g_vp_write_gamma_table(struct dispc_device *dispc,
					   u32 hw_videoport)
{
	u32 *table = dispc->vp_data[hw_videoport].gamma_table;
	u32 hwlen = dispc->feat->vp_feat.color.gamma_size;
	unsigned int i;

	dev_dbg(dispc->dev, "%s: hw_videoport %d\n", __func__, hw_videoport);

	if (WARN_ON(dispc->feat->vp_feat.color.gamma_type != TIDSS_GAMMA_8BIT))
		return;

	for (i = 0; i < hwlen; ++i) {
		u32 v = table[i];

		v |= i << 24;

		dispc_vp_write(dispc, hw_videoport, DISPC_VP_K2G_GAMMA_TABLE,
			       v);
	}
}

static void dispc_am65x_vp_write_gamma_table(struct dispc_device *dispc,
					     u32 hw_videoport)
{
	u32 *table = dispc->vp_data[hw_videoport].gamma_table;
	u32 hwlen = dispc->feat->vp_feat.color.gamma_size;
	unsigned int i;

	dev_dbg(dispc->dev, "%s: hw_videoport %d\n", __func__, hw_videoport);

	if (WARN_ON(dispc->feat->vp_feat.color.gamma_type != TIDSS_GAMMA_8BIT))
		return;

	for (i = 0; i < hwlen; ++i) {
		u32 v = table[i];

		v |= i << 24;

		dispc_vp_write(dispc, hw_videoport, DISPC_VP_GAMMA_TABLE, v);
	}
}

static void dispc_j721e_vp_write_gamma_table(struct dispc_device *dispc,
					     u32 hw_videoport)
{
	u32 *table = dispc->vp_data[hw_videoport].gamma_table;
	u32 hwlen = dispc->feat->vp_feat.color.gamma_size;
	unsigned int i;

	dev_dbg(dispc->dev, "%s: hw_videoport %d\n", __func__, hw_videoport);

	if (WARN_ON(dispc->feat->vp_feat.color.gamma_type != TIDSS_GAMMA_10BIT))
		return;

	for (i = 0; i < hwlen; ++i) {
		u32 v = table[i];

		if (i == 0)
			v |= 1 << 31;

		dispc_vp_write(dispc, hw_videoport, DISPC_VP_GAMMA_TABLE, v);
	}
}

static void dispc_vp_write_gamma_table(struct dispc_device *dispc,
				       u32 hw_videoport)
{
	switch (dispc->feat->subrev) {
	case DISPC_K2G:
		dispc_k2g_vp_write_gamma_table(dispc, hw_videoport);
		break;
	case DISPC_AM65X:
		dispc_am65x_vp_write_gamma_table(dispc, hw_videoport);
		break;
	case DISPC_J721E:
		dispc_j721e_vp_write_gamma_table(dispc, hw_videoport);
		break;
	default:
		WARN_ON(1);
		break;
	}
}

static const struct drm_color_lut dispc_vp_gamma_default_lut[] = {
	{ .red = 0, .green = 0, .blue = 0, },
	{ .red = U16_MAX, .green = U16_MAX, .blue = U16_MAX, },
};

static void dispc_vp_set_gamma(struct dispc_device *dispc,
			       u32 hw_videoport,
			       const struct drm_color_lut *lut,
			       unsigned int length)
{
	u32 *table = dispc->vp_data[hw_videoport].gamma_table;
	u32 hwlen = dispc->feat->vp_feat.color.gamma_size;
	u32 hwbits;
	unsigned int i;

	dev_dbg(dispc->dev, "%s: hw_videoport %d, lut len %u, hw len %u\n",
		__func__, hw_videoport, length, hwlen);

	if (dispc->feat->vp_feat.color.gamma_type == TIDSS_GAMMA_10BIT)
		hwbits = 10;
	else
		hwbits = 8;

	if (!lut || length < 2) {
		lut = dispc_vp_gamma_default_lut;
		length = ARRAY_SIZE(dispc_vp_gamma_default_lut);
	}

	for (i = 0; i < length - 1; ++i) {
		unsigned int first = i * (hwlen - 1) / (length - 1);
		unsigned int last = (i + 1) * (hwlen - 1) / (length - 1);
		unsigned int w = last - first;
		u16 r, g, b;
		unsigned int j;

		if (w == 0)
			continue;

		for (j = 0; j <= w; j++) {
			r = (lut[i].red * (w - j) + lut[i + 1].red * j) / w;
			g = (lut[i].green * (w - j) + lut[i + 1].green * j) / w;
			b = (lut[i].blue * (w - j) + lut[i + 1].blue * j) / w;

			r >>= 16 - hwbits;
			g >>= 16 - hwbits;
			b >>= 16 - hwbits;

			table[first + j] = (r << (hwbits * 2)) |
				(g << hwbits) | b;
		}
	}

	dispc_vp_write_gamma_table(dispc, hw_videoport);
}

static s16 dispc_S31_32_to_s2_8(s64 coef)
{
	u64 sign_bit = 1ULL << 63;
	u64 cbits = (u64)coef;
	s16 ret;

	if (cbits & sign_bit)
		ret = -clamp_val(((cbits & ~sign_bit) >> 24), 0, 0x200);
	else
		ret = clamp_val(((cbits & ~sign_bit) >> 24), 0, 0x1FF);

	return ret;
}

static void dispc_k2g_cpr_from_ctm(const struct drm_color_ctm *ctm,
				   struct dispc_csc_coef *cpr)
{
	memset(cpr, 0, sizeof(*cpr));

	cpr->to_regval = dispc_csc_cpr_regval;
	cpr->m[CSC_RR] = dispc_S31_32_to_s2_8(ctm->matrix[0]);
	cpr->m[CSC_RG] = dispc_S31_32_to_s2_8(ctm->matrix[1]);
	cpr->m[CSC_RB] = dispc_S31_32_to_s2_8(ctm->matrix[2]);
	cpr->m[CSC_GR] = dispc_S31_32_to_s2_8(ctm->matrix[3]);
	cpr->m[CSC_GG] = dispc_S31_32_to_s2_8(ctm->matrix[4]);
	cpr->m[CSC_GB] = dispc_S31_32_to_s2_8(ctm->matrix[5]);
	cpr->m[CSC_BR] = dispc_S31_32_to_s2_8(ctm->matrix[6]);
	cpr->m[CSC_BG] = dispc_S31_32_to_s2_8(ctm->matrix[7]);
	cpr->m[CSC_BB] = dispc_S31_32_to_s2_8(ctm->matrix[8]);
}

#define CVAL(xR, xG, xB) (FLD_VAL(xR, 9, 0) | FLD_VAL(xG, 20, 11) |	\
			  FLD_VAL(xB, 31, 22))

static void dispc_k2g_vp_csc_cpr_regval(const struct dispc_csc_coef *csc,
					u32 *regval)
{
	regval[0] = CVAL(csc->m[CSC_BB], csc->m[CSC_BG], csc->m[CSC_BR]);
	regval[1] = CVAL(csc->m[CSC_GB], csc->m[CSC_GG], csc->m[CSC_GR]);
	regval[2] = CVAL(csc->m[CSC_RB], csc->m[CSC_RG], csc->m[CSC_RR]);
}

#undef CVAL

static void dispc_k2g_vp_write_csc(struct dispc_device *dispc, u32 hw_videoport,
				   const struct dispc_csc_coef *csc)
{
	static const u16 dispc_vp_cpr_coef_reg[] = {
		DISPC_VP_CSC_COEF0, DISPC_VP_CSC_COEF1, DISPC_VP_CSC_COEF2,
		/* K2G CPR is packed to three registers. */
	};
	u32 regval[DISPC_CSC_REGVAL_LEN];
	unsigned int i;

	dispc_k2g_vp_csc_cpr_regval(csc, regval);

	for (i = 0; i < ARRAY_SIZE(dispc_vp_cpr_coef_reg); i++)
		dispc_vp_write(dispc, hw_videoport, dispc_vp_cpr_coef_reg[i],
			       regval[i]);
}

static void dispc_k2g_vp_set_ctm(struct dispc_device *dispc, u32 hw_videoport,
				 struct drm_color_ctm *ctm)
{
	u32 cprenable = 0;

	if (ctm) {
		struct dispc_csc_coef cpr;

		dispc_k2g_cpr_from_ctm(ctm, &cpr);
		dispc_k2g_vp_write_csc(dispc, hw_videoport, &cpr);
		cprenable = 1;
	}

	VP_REG_FLD_MOD(dispc, hw_videoport, DISPC_VP_CONFIG,
		       cprenable, 15, 15);
}

static s16 dispc_S31_32_to_s3_8(s64 coef)
{
	u64 sign_bit = 1ULL << 63;
	u64 cbits = (u64)coef;
	s16 ret;

	if (cbits & sign_bit)
		ret = -clamp_val(((cbits & ~sign_bit) >> 24), 0, 0x400);
	else
		ret = clamp_val(((cbits & ~sign_bit) >> 24), 0, 0x3FF);

	return ret;
}

static void dispc_csc_from_ctm(const struct drm_color_ctm *ctm,
			       struct dispc_csc_coef *cpr)
{
	memset(cpr, 0, sizeof(*cpr));

	cpr->to_regval = dispc_csc_cpr_regval;
	cpr->m[CSC_RR] = dispc_S31_32_to_s3_8(ctm->matrix[0]);
	cpr->m[CSC_RG] = dispc_S31_32_to_s3_8(ctm->matrix[1]);
	cpr->m[CSC_RB] = dispc_S31_32_to_s3_8(ctm->matrix[2]);
	cpr->m[CSC_GR] = dispc_S31_32_to_s3_8(ctm->matrix[3]);
	cpr->m[CSC_GG] = dispc_S31_32_to_s3_8(ctm->matrix[4]);
	cpr->m[CSC_GB] = dispc_S31_32_to_s3_8(ctm->matrix[5]);
	cpr->m[CSC_BR] = dispc_S31_32_to_s3_8(ctm->matrix[6]);
	cpr->m[CSC_BG] = dispc_S31_32_to_s3_8(ctm->matrix[7]);
	cpr->m[CSC_BB] = dispc_S31_32_to_s3_8(ctm->matrix[8]);
}

static void dispc_k3_vp_write_csc(struct dispc_device *dispc, u32 hw_videoport,
				  const struct dispc_csc_coef *csc)
{
	static const u16 dispc_vp_csc_coef_reg[DISPC_CSC_REGVAL_LEN] = {
		DISPC_VP_CSC_COEF0, DISPC_VP_CSC_COEF1, DISPC_VP_CSC_COEF2,
		DISPC_VP_CSC_COEF3, DISPC_VP_CSC_COEF4, DISPC_VP_CSC_COEF5,
		DISPC_VP_CSC_COEF6, DISPC_VP_CSC_COEF7,
	};
	u32 regval[DISPC_CSC_REGVAL_LEN];
	unsigned int i;

	csc->to_regval(csc, regval);

	for (i = 0; i < ARRAY_SIZE(regval); i++)
		dispc_vp_write(dispc, hw_videoport, dispc_vp_csc_coef_reg[i],
			       regval[i]);
}

static void dispc_k3_vp_set_ctm(struct dispc_device *dispc, u32 hw_videoport,
				struct drm_color_ctm *ctm)
{
	u32 colorconvenable = 0;

	if (ctm) {
		struct dispc_csc_coef csc;

		dispc_csc_from_ctm(ctm, &csc);
		dispc_k3_vp_write_csc(dispc, hw_videoport, &csc);
		colorconvenable = 1;
	}

	VP_REG_FLD_MOD(dispc, hw_videoport, DISPC_VP_CONFIG,
		       colorconvenable, 24, 24);
}

static void dispc_vp_set_color_mgmt(struct dispc_device *dispc,
				    u32 hw_videoport,
				    const struct drm_crtc_state *state,
				    bool newmodeset)
{
	struct drm_color_lut *lut = NULL;
	struct drm_color_ctm *ctm = NULL;
	unsigned int length = 0;

	if (!(state->color_mgmt_changed || newmodeset))
		return;

	if (state->gamma_lut) {
		lut = (struct drm_color_lut *)state->gamma_lut->data;
		length = state->gamma_lut->length / sizeof(*lut);
	}

	dispc_vp_set_gamma(dispc, hw_videoport, lut, length);

	if (state->ctm)
		ctm = (struct drm_color_ctm *)state->ctm->data;

	if (dispc->feat->subrev == DISPC_K2G)
		dispc_k2g_vp_set_ctm(dispc, hw_videoport, ctm);
	else
		dispc_k3_vp_set_ctm(dispc, hw_videoport, ctm);
}

void dispc_vp_setup(struct dispc_device *dispc, u32 hw_videoport,
		    const struct drm_crtc_state *state, bool newmodeset)
{
	dispc_vp_set_default_color(dispc, hw_videoport, 0);
	dispc_vp_set_color_mgmt(dispc, hw_videoport, state, newmodeset);
}

int dispc_runtime_suspend(struct dispc_device *dispc)
{
	dev_dbg(dispc->dev, "suspend\n");

	dispc->is_enabled = false;

	clk_disable_unprepare(dispc->fclk);

	return 0;
}

int dispc_runtime_resume(struct dispc_device *dispc)
{
	dev_dbg(dispc->dev, "resume\n");

	clk_prepare_enable(dispc->fclk);

	if (REG_GET(dispc, DSS_SYSSTATUS, 0, 0) == 0)
		dev_warn(dispc->dev, "DSS FUNC RESET not done!\n");

	dev_dbg(dispc->dev, "OMAP DSS7 rev 0x%x\n",
		dispc_read(dispc, DSS_REVISION));

	dev_dbg(dispc->dev, "VP RESETDONE %d,%d,%d\n",
		REG_GET(dispc, DSS_SYSSTATUS, 1, 1),
		REG_GET(dispc, DSS_SYSSTATUS, 2, 2),
		REG_GET(dispc, DSS_SYSSTATUS, 3, 3));

	if (dispc->feat->subrev == DISPC_AM65X)
		dev_dbg(dispc->dev, "OLDI RESETDONE %d,%d,%d\n",
			REG_GET(dispc, DSS_SYSSTATUS, 5, 5),
			REG_GET(dispc, DSS_SYSSTATUS, 6, 6),
			REG_GET(dispc, DSS_SYSSTATUS, 7, 7));

	dev_dbg(dispc->dev, "DISPC IDLE %d\n",
		REG_GET(dispc, DSS_SYSSTATUS, 9, 9));

	dispc_initial_config(dispc);

	dispc->is_enabled = true;

	tidss_irq_resume(dispc->tidss);

	return 0;
}

void dispc_remove(struct tidss_device *tidss)
{
	dev_dbg(tidss->dev, "%s\n", __func__);

	tidss->dispc = NULL;
}

static int dispc_iomap_resource(struct platform_device *pdev, const char *name,
				void __iomem **base)
{
	void __iomem *b;

	b = devm_platform_ioremap_resource_byname(pdev, name);
	if (IS_ERR(b)) {
		dev_err(&pdev->dev, "cannot ioremap resource '%s'\n", name);
		return PTR_ERR(b);
	}

	*base = b;

	return 0;
}

static int dispc_init_am65x_oldi_io_ctrl(struct device *dev,
					 struct dispc_device *dispc)
{
	dispc->oldi_io_ctrl =
		syscon_regmap_lookup_by_phandle(dev->of_node,
						"ti,am65x-oldi-io-ctrl");
	if (PTR_ERR(dispc->oldi_io_ctrl) == -ENODEV) {
		dispc->oldi_io_ctrl = NULL;
	} else if (IS_ERR(dispc->oldi_io_ctrl)) {
		dev_err(dev, "%s: syscon_regmap_lookup_by_phandle failed %ld\n",
			__func__, PTR_ERR(dispc->oldi_io_ctrl));
		return PTR_ERR(dispc->oldi_io_ctrl);
	}
	return 0;
}

static void dispc_init_errata(struct dispc_device *dispc)
{
	static const struct soc_device_attribute am65x_sr10_soc_devices[] = {
		{ .family = "AM65X", .revision = "SR1.0" },
		{ /* sentinel */ }
	};

	if (soc_device_match(am65x_sr10_soc_devices)) {
		dispc->errata.i2000 = true;
		dev_info(dispc->dev, "WA for erratum i2000: YUV formats disabled\n");
	}
}

static void dispc_softreset(struct dispc_device *dispc)
{
	u32 val;
	int ret = 0;

	/* Soft reset */
	REG_FLD_MOD(dispc, DSS_SYSCONFIG, 1, 1, 1);
	/* Wait for reset to complete */
	ret = readl_poll_timeout(dispc->base_common + DSS_SYSSTATUS,
				 val, val & 1, 100, 5000);
	if (ret)
		dev_warn(dispc->dev, "failed to reset dispc\n");
}

int dispc_init(struct tidss_device *tidss)
{
	struct device *dev = tidss->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct dispc_device *dispc;
	const struct dispc_features *feat;
	unsigned int i, num_fourccs;
	int r = 0;

	dev_dbg(dev, "%s\n", __func__);

	feat = tidss->feat;

	if (feat->subrev != DISPC_K2G) {
		r = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48));
		if (r)
			dev_warn(dev, "cannot set DMA masks to 48-bit\n");
	}

	dma_set_max_seg_size(dev, UINT_MAX);

	dispc = devm_kzalloc(dev, sizeof(*dispc), GFP_KERNEL);
	if (!dispc)
		return -ENOMEM;

	dispc->tidss = tidss;
	dispc->dev = dev;
	dispc->feat = feat;

	dispc_init_errata(dispc);

	dispc->fourccs = devm_kcalloc(dev, ARRAY_SIZE(dispc_color_formats),
				      sizeof(*dispc->fourccs), GFP_KERNEL);
	if (!dispc->fourccs)
		return -ENOMEM;

	num_fourccs = 0;
	for (i = 0; i < ARRAY_SIZE(dispc_color_formats); ++i) {
		if (dispc->errata.i2000 &&
		    dispc_fourcc_is_yuv(dispc_color_formats[i].fourcc)) {
			continue;
		}
		dispc->fourccs[num_fourccs++] = dispc_color_formats[i].fourcc;
	}

	dispc->num_fourccs = num_fourccs;

	dispc_common_regmap = dispc->feat->common_regs;

	r = dispc_iomap_resource(pdev, dispc->feat->common,
				 &dispc->base_common);
	if (r)
		return r;

	for (i = 0; i < dispc->feat->num_planes; i++) {
		r = dispc_iomap_resource(pdev, dispc->feat->vid_name[i],
					 &dispc->base_vid[i]);
		if (r)
			return r;
	}

	/* K2G display controller does not support soft reset */
	if (feat->subrev != DISPC_K2G)
		dispc_softreset(dispc);

	for (i = 0; i < dispc->feat->num_vps; i++) {
		u32 gamma_size = dispc->feat->vp_feat.color.gamma_size;
		u32 *gamma_table;
		struct clk *clk;

		r = dispc_iomap_resource(pdev, dispc->feat->ovr_name[i],
					 &dispc->base_ovr[i]);
		if (r)
			return r;

		r = dispc_iomap_resource(pdev, dispc->feat->vp_name[i],
					 &dispc->base_vp[i]);
		if (r)
			return r;

		clk = devm_clk_get(dev, dispc->feat->vpclk_name[i]);
		if (IS_ERR(clk)) {
			dev_err(dev, "%s: Failed to get clk %s:%ld\n", __func__,
				dispc->feat->vpclk_name[i], PTR_ERR(clk));
			return PTR_ERR(clk);
		}
		dispc->vp_clk[i] = clk;

		gamma_table = devm_kmalloc_array(dev, gamma_size,
						 sizeof(*gamma_table),
						 GFP_KERNEL);
		if (!gamma_table)
			return -ENOMEM;
		dispc->vp_data[i].gamma_table = gamma_table;
	}

	if (feat->subrev == DISPC_AM65X) {
		r = dispc_init_am65x_oldi_io_ctrl(dev, dispc);
		if (r)
			return r;
	}

	dispc->fclk = devm_clk_get(dev, "fck");
	if (IS_ERR(dispc->fclk)) {
		dev_err(dev, "%s: Failed to get fclk: %ld\n",
			__func__, PTR_ERR(dispc->fclk));
		return PTR_ERR(dispc->fclk);
	}
	dev_dbg(dev, "DSS fclk %lu Hz\n", clk_get_rate(dispc->fclk));

	of_property_read_u32(dispc->dev->of_node, "max-memory-bandwidth",
			     &dispc->memory_bandwidth_limit);

	tidss->dispc = dispc;

	return 0;
}
