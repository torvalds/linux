/*
 * linux/drivers/video/omap2/dss/dispc.c
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define DSS_SUBSYS_NAME "DISPC"

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/export.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/hardirq.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sizes.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/component.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_blend.h>

#include "omapdss.h"
#include "dss.h"
#include "dss_features.h"
#include "dispc.h"

/* DISPC */
#define DISPC_SZ_REGS			SZ_4K

enum omap_burst_size {
	BURST_SIZE_X2 = 0,
	BURST_SIZE_X4 = 1,
	BURST_SIZE_X8 = 2,
};

#define REG_GET(idx, start, end) \
	FLD_GET(dispc_read_reg(idx), start, end)

#define REG_FLD_MOD(idx, val, start, end)				\
	dispc_write_reg(idx, FLD_MOD(dispc_read_reg(idx), val, start, end))

struct dispc_features {
	u8 sw_start;
	u8 fp_start;
	u8 bp_start;
	u16 sw_max;
	u16 vp_max;
	u16 hp_max;
	u8 mgr_width_start;
	u8 mgr_height_start;
	u16 mgr_width_max;
	u16 mgr_height_max;
	unsigned long max_lcd_pclk;
	unsigned long max_tv_pclk;
	int (*calc_scaling) (unsigned long pclk, unsigned long lclk,
		const struct videomode *vm,
		u16 width, u16 height, u16 out_width, u16 out_height,
		u32 fourcc, bool *five_taps,
		int *x_predecim, int *y_predecim, int *decim_x, int *decim_y,
		u16 pos_x, unsigned long *core_clk, bool mem_to_mem);
	unsigned long (*calc_core_clk) (unsigned long pclk,
		u16 width, u16 height, u16 out_width, u16 out_height,
		bool mem_to_mem);
	u8 num_fifos;

	/* swap GFX & WB fifos */
	bool gfx_fifo_workaround:1;

	/* no DISPC_IRQ_FRAMEDONETV on this SoC */
	bool no_framedone_tv:1;

	/* revert to the OMAP4 mechanism of DISPC Smart Standby operation */
	bool mstandby_workaround:1;

	bool set_max_preload:1;

	/* PIXEL_INC is not added to the last pixel of a line */
	bool last_pixel_inc_missing:1;

	/* POL_FREQ has ALIGN bit */
	bool supports_sync_align:1;

	bool has_writeback:1;

	bool supports_double_pixel:1;

	/*
	 * Field order for VENC is different than HDMI. We should handle this in
	 * some intelligent manner, but as the SoCs have either HDMI or VENC,
	 * never both, we can just use this flag for now.
	 */
	bool reverse_ilace_field_order:1;

	bool has_gamma_table:1;

	bool has_gamma_i734_bug:1;
};

#define DISPC_MAX_NR_FIFOS 5
#define DISPC_MAX_CHANNEL_GAMMA 4

static struct {
	struct platform_device *pdev;
	void __iomem    *base;

	int irq;
	irq_handler_t user_handler;
	void *user_data;

	unsigned long core_clk_rate;
	unsigned long tv_pclk_rate;

	u32 fifo_size[DISPC_MAX_NR_FIFOS];
	/* maps which plane is using a fifo. fifo-id -> plane-id */
	int fifo_assignment[DISPC_MAX_NR_FIFOS];

	bool		ctx_valid;
	u32		ctx[DISPC_SZ_REGS / sizeof(u32)];

	u32 *gamma_table[DISPC_MAX_CHANNEL_GAMMA];

	const struct dispc_features *feat;

	bool is_enabled;

	struct regmap *syscon_pol;
	u32 syscon_pol_offset;

	/* DISPC_CONTROL & DISPC_CONFIG lock*/
	spinlock_t control_lock;
} dispc;

enum omap_color_component {
	/* used for all color formats for OMAP3 and earlier
	 * and for RGB and Y color component on OMAP4
	 */
	DISPC_COLOR_COMPONENT_RGB_Y		= 1 << 0,
	/* used for UV component for
	 * DRM_FORMAT_YUYV, DRM_FORMAT_UYVY, DRM_FORMAT_NV12
	 * color formats on OMAP4
	 */
	DISPC_COLOR_COMPONENT_UV		= 1 << 1,
};

enum mgr_reg_fields {
	DISPC_MGR_FLD_ENABLE,
	DISPC_MGR_FLD_STNTFT,
	DISPC_MGR_FLD_GO,
	DISPC_MGR_FLD_TFTDATALINES,
	DISPC_MGR_FLD_STALLMODE,
	DISPC_MGR_FLD_TCKENABLE,
	DISPC_MGR_FLD_TCKSELECTION,
	DISPC_MGR_FLD_CPR,
	DISPC_MGR_FLD_FIFOHANDCHECK,
	/* used to maintain a count of the above fields */
	DISPC_MGR_FLD_NUM,
};

struct dispc_reg_field {
	u16 reg;
	u8 high;
	u8 low;
};

struct dispc_gamma_desc {
	u32 len;
	u32 bits;
	u16 reg;
	bool has_index;
};

static const struct {
	const char *name;
	u32 vsync_irq;
	u32 framedone_irq;
	u32 sync_lost_irq;
	struct dispc_gamma_desc gamma;
	struct dispc_reg_field reg_desc[DISPC_MGR_FLD_NUM];
} mgr_desc[] = {
	[OMAP_DSS_CHANNEL_LCD] = {
		.name		= "LCD",
		.vsync_irq	= DISPC_IRQ_VSYNC,
		.framedone_irq	= DISPC_IRQ_FRAMEDONE,
		.sync_lost_irq	= DISPC_IRQ_SYNC_LOST,
		.gamma		= {
			.len	= 256,
			.bits	= 8,
			.reg	= DISPC_GAMMA_TABLE0,
			.has_index = true,
		},
		.reg_desc	= {
			[DISPC_MGR_FLD_ENABLE]		= { DISPC_CONTROL,  0,  0 },
			[DISPC_MGR_FLD_STNTFT]		= { DISPC_CONTROL,  3,  3 },
			[DISPC_MGR_FLD_GO]		= { DISPC_CONTROL,  5,  5 },
			[DISPC_MGR_FLD_TFTDATALINES]	= { DISPC_CONTROL,  9,  8 },
			[DISPC_MGR_FLD_STALLMODE]	= { DISPC_CONTROL, 11, 11 },
			[DISPC_MGR_FLD_TCKENABLE]	= { DISPC_CONFIG,  10, 10 },
			[DISPC_MGR_FLD_TCKSELECTION]	= { DISPC_CONFIG,  11, 11 },
			[DISPC_MGR_FLD_CPR]		= { DISPC_CONFIG,  15, 15 },
			[DISPC_MGR_FLD_FIFOHANDCHECK]	= { DISPC_CONFIG,  16, 16 },
		},
	},
	[OMAP_DSS_CHANNEL_DIGIT] = {
		.name		= "DIGIT",
		.vsync_irq	= DISPC_IRQ_EVSYNC_ODD | DISPC_IRQ_EVSYNC_EVEN,
		.framedone_irq	= DISPC_IRQ_FRAMEDONETV,
		.sync_lost_irq	= DISPC_IRQ_SYNC_LOST_DIGIT,
		.gamma		= {
			.len	= 1024,
			.bits	= 10,
			.reg	= DISPC_GAMMA_TABLE2,
			.has_index = false,
		},
		.reg_desc	= {
			[DISPC_MGR_FLD_ENABLE]		= { DISPC_CONTROL,  1,  1 },
			[DISPC_MGR_FLD_STNTFT]		= { },
			[DISPC_MGR_FLD_GO]		= { DISPC_CONTROL,  6,  6 },
			[DISPC_MGR_FLD_TFTDATALINES]	= { },
			[DISPC_MGR_FLD_STALLMODE]	= { },
			[DISPC_MGR_FLD_TCKENABLE]	= { DISPC_CONFIG,  12, 12 },
			[DISPC_MGR_FLD_TCKSELECTION]	= { DISPC_CONFIG,  13, 13 },
			[DISPC_MGR_FLD_CPR]		= { },
			[DISPC_MGR_FLD_FIFOHANDCHECK]	= { DISPC_CONFIG,  16, 16 },
		},
	},
	[OMAP_DSS_CHANNEL_LCD2] = {
		.name		= "LCD2",
		.vsync_irq	= DISPC_IRQ_VSYNC2,
		.framedone_irq	= DISPC_IRQ_FRAMEDONE2,
		.sync_lost_irq	= DISPC_IRQ_SYNC_LOST2,
		.gamma		= {
			.len	= 256,
			.bits	= 8,
			.reg	= DISPC_GAMMA_TABLE1,
			.has_index = true,
		},
		.reg_desc	= {
			[DISPC_MGR_FLD_ENABLE]		= { DISPC_CONTROL2,  0,  0 },
			[DISPC_MGR_FLD_STNTFT]		= { DISPC_CONTROL2,  3,  3 },
			[DISPC_MGR_FLD_GO]		= { DISPC_CONTROL2,  5,  5 },
			[DISPC_MGR_FLD_TFTDATALINES]	= { DISPC_CONTROL2,  9,  8 },
			[DISPC_MGR_FLD_STALLMODE]	= { DISPC_CONTROL2, 11, 11 },
			[DISPC_MGR_FLD_TCKENABLE]	= { DISPC_CONFIG2,  10, 10 },
			[DISPC_MGR_FLD_TCKSELECTION]	= { DISPC_CONFIG2,  11, 11 },
			[DISPC_MGR_FLD_CPR]		= { DISPC_CONFIG2,  15, 15 },
			[DISPC_MGR_FLD_FIFOHANDCHECK]	= { DISPC_CONFIG2,  16, 16 },
		},
	},
	[OMAP_DSS_CHANNEL_LCD3] = {
		.name		= "LCD3",
		.vsync_irq	= DISPC_IRQ_VSYNC3,
		.framedone_irq	= DISPC_IRQ_FRAMEDONE3,
		.sync_lost_irq	= DISPC_IRQ_SYNC_LOST3,
		.gamma		= {
			.len	= 256,
			.bits	= 8,
			.reg	= DISPC_GAMMA_TABLE3,
			.has_index = true,
		},
		.reg_desc	= {
			[DISPC_MGR_FLD_ENABLE]		= { DISPC_CONTROL3,  0,  0 },
			[DISPC_MGR_FLD_STNTFT]		= { DISPC_CONTROL3,  3,  3 },
			[DISPC_MGR_FLD_GO]		= { DISPC_CONTROL3,  5,  5 },
			[DISPC_MGR_FLD_TFTDATALINES]	= { DISPC_CONTROL3,  9,  8 },
			[DISPC_MGR_FLD_STALLMODE]	= { DISPC_CONTROL3, 11, 11 },
			[DISPC_MGR_FLD_TCKENABLE]	= { DISPC_CONFIG3,  10, 10 },
			[DISPC_MGR_FLD_TCKSELECTION]	= { DISPC_CONFIG3,  11, 11 },
			[DISPC_MGR_FLD_CPR]		= { DISPC_CONFIG3,  15, 15 },
			[DISPC_MGR_FLD_FIFOHANDCHECK]	= { DISPC_CONFIG3,  16, 16 },
		},
	},
};

struct color_conv_coef {
	int ry, rcr, rcb, gy, gcr, gcb, by, bcr, bcb;
	int full_range;
};

static unsigned long dispc_fclk_rate(void);
static unsigned long dispc_core_clk_rate(void);
static unsigned long dispc_mgr_lclk_rate(enum omap_channel channel);
static unsigned long dispc_mgr_pclk_rate(enum omap_channel channel);

static unsigned long dispc_plane_pclk_rate(enum omap_plane_id plane);
static unsigned long dispc_plane_lclk_rate(enum omap_plane_id plane);

static void dispc_clear_irqstatus(u32 mask);
static bool dispc_mgr_is_enabled(enum omap_channel channel);
static void dispc_clear_irqstatus(u32 mask);

static inline void dispc_write_reg(const u16 idx, u32 val)
{
	__raw_writel(val, dispc.base + idx);
}

static inline u32 dispc_read_reg(const u16 idx)
{
	return __raw_readl(dispc.base + idx);
}

static u32 mgr_fld_read(enum omap_channel channel, enum mgr_reg_fields regfld)
{
	const struct dispc_reg_field rfld = mgr_desc[channel].reg_desc[regfld];
	return REG_GET(rfld.reg, rfld.high, rfld.low);
}

static void mgr_fld_write(enum omap_channel channel,
					enum mgr_reg_fields regfld, int val) {
	const struct dispc_reg_field rfld = mgr_desc[channel].reg_desc[regfld];
	const bool need_lock = rfld.reg == DISPC_CONTROL || rfld.reg == DISPC_CONFIG;
	unsigned long flags;

	if (need_lock)
		spin_lock_irqsave(&dispc.control_lock, flags);

	REG_FLD_MOD(rfld.reg, val, rfld.high, rfld.low);

	if (need_lock)
		spin_unlock_irqrestore(&dispc.control_lock, flags);
}

#define SR(reg) \
	dispc.ctx[DISPC_##reg / sizeof(u32)] = dispc_read_reg(DISPC_##reg)
#define RR(reg) \
	dispc_write_reg(DISPC_##reg, dispc.ctx[DISPC_##reg / sizeof(u32)])

static void dispc_save_context(void)
{
	int i, j;

	DSSDBG("dispc_save_context\n");

	SR(IRQENABLE);
	SR(CONTROL);
	SR(CONFIG);
	SR(LINE_NUMBER);
	if (dss_has_feature(FEAT_ALPHA_FIXED_ZORDER) ||
			dss_has_feature(FEAT_ALPHA_FREE_ZORDER))
		SR(GLOBAL_ALPHA);
	if (dss_has_feature(FEAT_MGR_LCD2)) {
		SR(CONTROL2);
		SR(CONFIG2);
	}
	if (dss_has_feature(FEAT_MGR_LCD3)) {
		SR(CONTROL3);
		SR(CONFIG3);
	}

	for (i = 0; i < dss_feat_get_num_mgrs(); i++) {
		SR(DEFAULT_COLOR(i));
		SR(TRANS_COLOR(i));
		SR(SIZE_MGR(i));
		if (i == OMAP_DSS_CHANNEL_DIGIT)
			continue;
		SR(TIMING_H(i));
		SR(TIMING_V(i));
		SR(POL_FREQ(i));
		SR(DIVISORo(i));

		SR(DATA_CYCLE1(i));
		SR(DATA_CYCLE2(i));
		SR(DATA_CYCLE3(i));

		if (dss_has_feature(FEAT_CPR)) {
			SR(CPR_COEF_R(i));
			SR(CPR_COEF_G(i));
			SR(CPR_COEF_B(i));
		}
	}

	for (i = 0; i < dss_feat_get_num_ovls(); i++) {
		SR(OVL_BA0(i));
		SR(OVL_BA1(i));
		SR(OVL_POSITION(i));
		SR(OVL_SIZE(i));
		SR(OVL_ATTRIBUTES(i));
		SR(OVL_FIFO_THRESHOLD(i));
		SR(OVL_ROW_INC(i));
		SR(OVL_PIXEL_INC(i));
		if (dss_has_feature(FEAT_PRELOAD))
			SR(OVL_PRELOAD(i));
		if (i == OMAP_DSS_GFX) {
			SR(OVL_WINDOW_SKIP(i));
			SR(OVL_TABLE_BA(i));
			continue;
		}
		SR(OVL_FIR(i));
		SR(OVL_PICTURE_SIZE(i));
		SR(OVL_ACCU0(i));
		SR(OVL_ACCU1(i));

		for (j = 0; j < 8; j++)
			SR(OVL_FIR_COEF_H(i, j));

		for (j = 0; j < 8; j++)
			SR(OVL_FIR_COEF_HV(i, j));

		for (j = 0; j < 5; j++)
			SR(OVL_CONV_COEF(i, j));

		if (dss_has_feature(FEAT_FIR_COEF_V)) {
			for (j = 0; j < 8; j++)
				SR(OVL_FIR_COEF_V(i, j));
		}

		if (dss_has_feature(FEAT_HANDLE_UV_SEPARATE)) {
			SR(OVL_BA0_UV(i));
			SR(OVL_BA1_UV(i));
			SR(OVL_FIR2(i));
			SR(OVL_ACCU2_0(i));
			SR(OVL_ACCU2_1(i));

			for (j = 0; j < 8; j++)
				SR(OVL_FIR_COEF_H2(i, j));

			for (j = 0; j < 8; j++)
				SR(OVL_FIR_COEF_HV2(i, j));

			for (j = 0; j < 8; j++)
				SR(OVL_FIR_COEF_V2(i, j));
		}
		if (dss_has_feature(FEAT_ATTR2))
			SR(OVL_ATTRIBUTES2(i));
	}

	if (dss_has_feature(FEAT_CORE_CLK_DIV))
		SR(DIVISOR);

	dispc.ctx_valid = true;

	DSSDBG("context saved\n");
}

static void dispc_restore_context(void)
{
	int i, j;

	DSSDBG("dispc_restore_context\n");

	if (!dispc.ctx_valid)
		return;

	/*RR(IRQENABLE);*/
	/*RR(CONTROL);*/
	RR(CONFIG);
	RR(LINE_NUMBER);
	if (dss_has_feature(FEAT_ALPHA_FIXED_ZORDER) ||
			dss_has_feature(FEAT_ALPHA_FREE_ZORDER))
		RR(GLOBAL_ALPHA);
	if (dss_has_feature(FEAT_MGR_LCD2))
		RR(CONFIG2);
	if (dss_has_feature(FEAT_MGR_LCD3))
		RR(CONFIG3);

	for (i = 0; i < dss_feat_get_num_mgrs(); i++) {
		RR(DEFAULT_COLOR(i));
		RR(TRANS_COLOR(i));
		RR(SIZE_MGR(i));
		if (i == OMAP_DSS_CHANNEL_DIGIT)
			continue;
		RR(TIMING_H(i));
		RR(TIMING_V(i));
		RR(POL_FREQ(i));
		RR(DIVISORo(i));

		RR(DATA_CYCLE1(i));
		RR(DATA_CYCLE2(i));
		RR(DATA_CYCLE3(i));

		if (dss_has_feature(FEAT_CPR)) {
			RR(CPR_COEF_R(i));
			RR(CPR_COEF_G(i));
			RR(CPR_COEF_B(i));
		}
	}

	for (i = 0; i < dss_feat_get_num_ovls(); i++) {
		RR(OVL_BA0(i));
		RR(OVL_BA1(i));
		RR(OVL_POSITION(i));
		RR(OVL_SIZE(i));
		RR(OVL_ATTRIBUTES(i));
		RR(OVL_FIFO_THRESHOLD(i));
		RR(OVL_ROW_INC(i));
		RR(OVL_PIXEL_INC(i));
		if (dss_has_feature(FEAT_PRELOAD))
			RR(OVL_PRELOAD(i));
		if (i == OMAP_DSS_GFX) {
			RR(OVL_WINDOW_SKIP(i));
			RR(OVL_TABLE_BA(i));
			continue;
		}
		RR(OVL_FIR(i));
		RR(OVL_PICTURE_SIZE(i));
		RR(OVL_ACCU0(i));
		RR(OVL_ACCU1(i));

		for (j = 0; j < 8; j++)
			RR(OVL_FIR_COEF_H(i, j));

		for (j = 0; j < 8; j++)
			RR(OVL_FIR_COEF_HV(i, j));

		for (j = 0; j < 5; j++)
			RR(OVL_CONV_COEF(i, j));

		if (dss_has_feature(FEAT_FIR_COEF_V)) {
			for (j = 0; j < 8; j++)
				RR(OVL_FIR_COEF_V(i, j));
		}

		if (dss_has_feature(FEAT_HANDLE_UV_SEPARATE)) {
			RR(OVL_BA0_UV(i));
			RR(OVL_BA1_UV(i));
			RR(OVL_FIR2(i));
			RR(OVL_ACCU2_0(i));
			RR(OVL_ACCU2_1(i));

			for (j = 0; j < 8; j++)
				RR(OVL_FIR_COEF_H2(i, j));

			for (j = 0; j < 8; j++)
				RR(OVL_FIR_COEF_HV2(i, j));

			for (j = 0; j < 8; j++)
				RR(OVL_FIR_COEF_V2(i, j));
		}
		if (dss_has_feature(FEAT_ATTR2))
			RR(OVL_ATTRIBUTES2(i));
	}

	if (dss_has_feature(FEAT_CORE_CLK_DIV))
		RR(DIVISOR);

	/* enable last, because LCD & DIGIT enable are here */
	RR(CONTROL);
	if (dss_has_feature(FEAT_MGR_LCD2))
		RR(CONTROL2);
	if (dss_has_feature(FEAT_MGR_LCD3))
		RR(CONTROL3);
	/* clear spurious SYNC_LOST_DIGIT interrupts */
	dispc_clear_irqstatus(DISPC_IRQ_SYNC_LOST_DIGIT);

	/*
	 * enable last so IRQs won't trigger before
	 * the context is fully restored
	 */
	RR(IRQENABLE);

	DSSDBG("context restored\n");
}

#undef SR
#undef RR

int dispc_runtime_get(void)
{
	int r;

	DSSDBG("dispc_runtime_get\n");

	r = pm_runtime_get_sync(&dispc.pdev->dev);
	WARN_ON(r < 0);
	return r < 0 ? r : 0;
}

void dispc_runtime_put(void)
{
	int r;

	DSSDBG("dispc_runtime_put\n");

	r = pm_runtime_put_sync(&dispc.pdev->dev);
	WARN_ON(r < 0 && r != -ENOSYS);
}

static u32 dispc_mgr_get_vsync_irq(enum omap_channel channel)
{
	return mgr_desc[channel].vsync_irq;
}

static u32 dispc_mgr_get_framedone_irq(enum omap_channel channel)
{
	if (channel == OMAP_DSS_CHANNEL_DIGIT && dispc.feat->no_framedone_tv)
		return 0;

	return mgr_desc[channel].framedone_irq;
}

static u32 dispc_mgr_get_sync_lost_irq(enum omap_channel channel)
{
	return mgr_desc[channel].sync_lost_irq;
}

u32 dispc_wb_get_framedone_irq(void)
{
	return DISPC_IRQ_FRAMEDONEWB;
}

static void dispc_mgr_enable(enum omap_channel channel, bool enable)
{
	mgr_fld_write(channel, DISPC_MGR_FLD_ENABLE, enable);
	/* flush posted write */
	mgr_fld_read(channel, DISPC_MGR_FLD_ENABLE);
}

static bool dispc_mgr_is_enabled(enum omap_channel channel)
{
	return !!mgr_fld_read(channel, DISPC_MGR_FLD_ENABLE);
}

static bool dispc_mgr_go_busy(enum omap_channel channel)
{
	return mgr_fld_read(channel, DISPC_MGR_FLD_GO) == 1;
}

static void dispc_mgr_go(enum omap_channel channel)
{
	WARN_ON(!dispc_mgr_is_enabled(channel));
	WARN_ON(dispc_mgr_go_busy(channel));

	DSSDBG("GO %s\n", mgr_desc[channel].name);

	mgr_fld_write(channel, DISPC_MGR_FLD_GO, 1);
}

bool dispc_wb_go_busy(void)
{
	return REG_GET(DISPC_CONTROL2, 6, 6) == 1;
}

void dispc_wb_go(void)
{
	enum omap_plane_id plane = OMAP_DSS_WB;
	bool enable, go;

	enable = REG_GET(DISPC_OVL_ATTRIBUTES(plane), 0, 0) == 1;

	if (!enable)
		return;

	go = REG_GET(DISPC_CONTROL2, 6, 6) == 1;
	if (go) {
		DSSERR("GO bit not down for WB\n");
		return;
	}

	REG_FLD_MOD(DISPC_CONTROL2, 1, 6, 6);
}

static void dispc_ovl_write_firh_reg(enum omap_plane_id plane, int reg,
				     u32 value)
{
	dispc_write_reg(DISPC_OVL_FIR_COEF_H(plane, reg), value);
}

static void dispc_ovl_write_firhv_reg(enum omap_plane_id plane, int reg,
				      u32 value)
{
	dispc_write_reg(DISPC_OVL_FIR_COEF_HV(plane, reg), value);
}

static void dispc_ovl_write_firv_reg(enum omap_plane_id plane, int reg,
				     u32 value)
{
	dispc_write_reg(DISPC_OVL_FIR_COEF_V(plane, reg), value);
}

static void dispc_ovl_write_firh2_reg(enum omap_plane_id plane, int reg,
				      u32 value)
{
	BUG_ON(plane == OMAP_DSS_GFX);

	dispc_write_reg(DISPC_OVL_FIR_COEF_H2(plane, reg), value);
}

static void dispc_ovl_write_firhv2_reg(enum omap_plane_id plane, int reg,
		u32 value)
{
	BUG_ON(plane == OMAP_DSS_GFX);

	dispc_write_reg(DISPC_OVL_FIR_COEF_HV2(plane, reg), value);
}

static void dispc_ovl_write_firv2_reg(enum omap_plane_id plane, int reg,
				      u32 value)
{
	BUG_ON(plane == OMAP_DSS_GFX);

	dispc_write_reg(DISPC_OVL_FIR_COEF_V2(plane, reg), value);
}

static void dispc_ovl_set_scale_coef(enum omap_plane_id plane, int fir_hinc,
				int fir_vinc, int five_taps,
				enum omap_color_component color_comp)
{
	const struct dispc_coef *h_coef, *v_coef;
	int i;

	h_coef = dispc_ovl_get_scale_coef(fir_hinc, true);
	v_coef = dispc_ovl_get_scale_coef(fir_vinc, five_taps);

	for (i = 0; i < 8; i++) {
		u32 h, hv;

		h = FLD_VAL(h_coef[i].hc0_vc00, 7, 0)
			| FLD_VAL(h_coef[i].hc1_vc0, 15, 8)
			| FLD_VAL(h_coef[i].hc2_vc1, 23, 16)
			| FLD_VAL(h_coef[i].hc3_vc2, 31, 24);
		hv = FLD_VAL(h_coef[i].hc4_vc22, 7, 0)
			| FLD_VAL(v_coef[i].hc1_vc0, 15, 8)
			| FLD_VAL(v_coef[i].hc2_vc1, 23, 16)
			| FLD_VAL(v_coef[i].hc3_vc2, 31, 24);

		if (color_comp == DISPC_COLOR_COMPONENT_RGB_Y) {
			dispc_ovl_write_firh_reg(plane, i, h);
			dispc_ovl_write_firhv_reg(plane, i, hv);
		} else {
			dispc_ovl_write_firh2_reg(plane, i, h);
			dispc_ovl_write_firhv2_reg(plane, i, hv);
		}

	}

	if (five_taps) {
		for (i = 0; i < 8; i++) {
			u32 v;
			v = FLD_VAL(v_coef[i].hc0_vc00, 7, 0)
				| FLD_VAL(v_coef[i].hc4_vc22, 15, 8);
			if (color_comp == DISPC_COLOR_COMPONENT_RGB_Y)
				dispc_ovl_write_firv_reg(plane, i, v);
			else
				dispc_ovl_write_firv2_reg(plane, i, v);
		}
	}
}


static void dispc_ovl_write_color_conv_coef(enum omap_plane_id plane,
		const struct color_conv_coef *ct)
{
#define CVAL(x, y) (FLD_VAL(x, 26, 16) | FLD_VAL(y, 10, 0))

	dispc_write_reg(DISPC_OVL_CONV_COEF(plane, 0), CVAL(ct->rcr, ct->ry));
	dispc_write_reg(DISPC_OVL_CONV_COEF(plane, 1), CVAL(ct->gy,  ct->rcb));
	dispc_write_reg(DISPC_OVL_CONV_COEF(plane, 2), CVAL(ct->gcb, ct->gcr));
	dispc_write_reg(DISPC_OVL_CONV_COEF(plane, 3), CVAL(ct->bcr, ct->by));
	dispc_write_reg(DISPC_OVL_CONV_COEF(plane, 4), CVAL(0, ct->bcb));

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), ct->full_range, 11, 11);

#undef CVAL
}

static void dispc_setup_color_conv_coef(void)
{
	int i;
	int num_ovl = dss_feat_get_num_ovls();
	const struct color_conv_coef ctbl_bt601_5_ovl = {
		/* YUV -> RGB */
		298, 409, 0, 298, -208, -100, 298, 0, 517, 0,
	};
	const struct color_conv_coef ctbl_bt601_5_wb = {
		/* RGB -> YUV */
		66, 129, 25, 112, -94, -18, -38, -74, 112, 0,
	};

	for (i = 1; i < num_ovl; i++)
		dispc_ovl_write_color_conv_coef(i, &ctbl_bt601_5_ovl);

	if (dispc.feat->has_writeback)
		dispc_ovl_write_color_conv_coef(OMAP_DSS_WB, &ctbl_bt601_5_wb);
}

static void dispc_ovl_set_ba0(enum omap_plane_id plane, u32 paddr)
{
	dispc_write_reg(DISPC_OVL_BA0(plane), paddr);
}

static void dispc_ovl_set_ba1(enum omap_plane_id plane, u32 paddr)
{
	dispc_write_reg(DISPC_OVL_BA1(plane), paddr);
}

static void dispc_ovl_set_ba0_uv(enum omap_plane_id plane, u32 paddr)
{
	dispc_write_reg(DISPC_OVL_BA0_UV(plane), paddr);
}

static void dispc_ovl_set_ba1_uv(enum omap_plane_id plane, u32 paddr)
{
	dispc_write_reg(DISPC_OVL_BA1_UV(plane), paddr);
}

static void dispc_ovl_set_pos(enum omap_plane_id plane,
		enum omap_overlay_caps caps, int x, int y)
{
	u32 val;

	if ((caps & OMAP_DSS_OVL_CAP_POS) == 0)
		return;

	val = FLD_VAL(y, 26, 16) | FLD_VAL(x, 10, 0);

	dispc_write_reg(DISPC_OVL_POSITION(plane), val);
}

static void dispc_ovl_set_input_size(enum omap_plane_id plane, int width,
		int height)
{
	u32 val = FLD_VAL(height - 1, 26, 16) | FLD_VAL(width - 1, 10, 0);

	if (plane == OMAP_DSS_GFX || plane == OMAP_DSS_WB)
		dispc_write_reg(DISPC_OVL_SIZE(plane), val);
	else
		dispc_write_reg(DISPC_OVL_PICTURE_SIZE(plane), val);
}

static void dispc_ovl_set_output_size(enum omap_plane_id plane, int width,
		int height)
{
	u32 val;

	BUG_ON(plane == OMAP_DSS_GFX);

	val = FLD_VAL(height - 1, 26, 16) | FLD_VAL(width - 1, 10, 0);

	if (plane == OMAP_DSS_WB)
		dispc_write_reg(DISPC_OVL_PICTURE_SIZE(plane), val);
	else
		dispc_write_reg(DISPC_OVL_SIZE(plane), val);
}

static void dispc_ovl_set_zorder(enum omap_plane_id plane,
		enum omap_overlay_caps caps, u8 zorder)
{
	if ((caps & OMAP_DSS_OVL_CAP_ZORDER) == 0)
		return;

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), zorder, 27, 26);
}

static void dispc_ovl_enable_zorder_planes(void)
{
	int i;

	if (!dss_has_feature(FEAT_ALPHA_FREE_ZORDER))
		return;

	for (i = 0; i < dss_feat_get_num_ovls(); i++)
		REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(i), 1, 25, 25);
}

static void dispc_ovl_set_pre_mult_alpha(enum omap_plane_id plane,
		enum omap_overlay_caps caps, bool enable)
{
	if ((caps & OMAP_DSS_OVL_CAP_PRE_MULT_ALPHA) == 0)
		return;

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), enable ? 1 : 0, 28, 28);
}

static void dispc_ovl_setup_global_alpha(enum omap_plane_id plane,
		enum omap_overlay_caps caps, u8 global_alpha)
{
	static const unsigned shifts[] = { 0, 8, 16, 24, };
	int shift;

	if ((caps & OMAP_DSS_OVL_CAP_GLOBAL_ALPHA) == 0)
		return;

	shift = shifts[plane];
	REG_FLD_MOD(DISPC_GLOBAL_ALPHA, global_alpha, shift + 7, shift);
}

static void dispc_ovl_set_pix_inc(enum omap_plane_id plane, s32 inc)
{
	dispc_write_reg(DISPC_OVL_PIXEL_INC(plane), inc);
}

static void dispc_ovl_set_row_inc(enum omap_plane_id plane, s32 inc)
{
	dispc_write_reg(DISPC_OVL_ROW_INC(plane), inc);
}

static void dispc_ovl_set_color_mode(enum omap_plane_id plane, u32 fourcc)
{
	u32 m = 0;
	if (plane != OMAP_DSS_GFX) {
		switch (fourcc) {
		case DRM_FORMAT_NV12:
			m = 0x0; break;
		case DRM_FORMAT_XRGB4444:
			m = 0x1; break;
		case DRM_FORMAT_RGBA4444:
			m = 0x2; break;
		case DRM_FORMAT_RGBX4444:
			m = 0x4; break;
		case DRM_FORMAT_ARGB4444:
			m = 0x5; break;
		case DRM_FORMAT_RGB565:
			m = 0x6; break;
		case DRM_FORMAT_ARGB1555:
			m = 0x7; break;
		case DRM_FORMAT_XRGB8888:
			m = 0x8; break;
		case DRM_FORMAT_RGB888:
			m = 0x9; break;
		case DRM_FORMAT_YUYV:
			m = 0xa; break;
		case DRM_FORMAT_UYVY:
			m = 0xb; break;
		case DRM_FORMAT_ARGB8888:
			m = 0xc; break;
		case DRM_FORMAT_RGBA8888:
			m = 0xd; break;
		case DRM_FORMAT_RGBX8888:
			m = 0xe; break;
		case DRM_FORMAT_XRGB1555:
			m = 0xf; break;
		default:
			BUG(); return;
		}
	} else {
		switch (fourcc) {
		case DRM_FORMAT_RGBX4444:
			m = 0x4; break;
		case DRM_FORMAT_ARGB4444:
			m = 0x5; break;
		case DRM_FORMAT_RGB565:
			m = 0x6; break;
		case DRM_FORMAT_ARGB1555:
			m = 0x7; break;
		case DRM_FORMAT_XRGB8888:
			m = 0x8; break;
		case DRM_FORMAT_RGB888:
			m = 0x9; break;
		case DRM_FORMAT_XRGB4444:
			m = 0xa; break;
		case DRM_FORMAT_RGBA4444:
			m = 0xb; break;
		case DRM_FORMAT_ARGB8888:
			m = 0xc; break;
		case DRM_FORMAT_RGBA8888:
			m = 0xd; break;
		case DRM_FORMAT_RGBX8888:
			m = 0xe; break;
		case DRM_FORMAT_XRGB1555:
			m = 0xf; break;
		default:
			BUG(); return;
		}
	}

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), m, 4, 1);
}

static bool format_is_yuv(u32 fourcc)
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

static void dispc_ovl_configure_burst_type(enum omap_plane_id plane,
		enum omap_dss_rotation_type rotation_type)
{
	if (dss_has_feature(FEAT_BURST_2D) == 0)
		return;

	if (rotation_type == OMAP_DSS_ROT_TILER)
		REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), 1, 29, 29);
	else
		REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), 0, 29, 29);
}

static void dispc_ovl_set_channel_out(enum omap_plane_id plane,
				      enum omap_channel channel)
{
	int shift;
	u32 val;
	int chan = 0, chan2 = 0;

	switch (plane) {
	case OMAP_DSS_GFX:
		shift = 8;
		break;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
	case OMAP_DSS_VIDEO3:
		shift = 16;
		break;
	default:
		BUG();
		return;
	}

	val = dispc_read_reg(DISPC_OVL_ATTRIBUTES(plane));
	if (dss_has_feature(FEAT_MGR_LCD2)) {
		switch (channel) {
		case OMAP_DSS_CHANNEL_LCD:
			chan = 0;
			chan2 = 0;
			break;
		case OMAP_DSS_CHANNEL_DIGIT:
			chan = 1;
			chan2 = 0;
			break;
		case OMAP_DSS_CHANNEL_LCD2:
			chan = 0;
			chan2 = 1;
			break;
		case OMAP_DSS_CHANNEL_LCD3:
			if (dss_has_feature(FEAT_MGR_LCD3)) {
				chan = 0;
				chan2 = 2;
			} else {
				BUG();
				return;
			}
			break;
		case OMAP_DSS_CHANNEL_WB:
			chan = 0;
			chan2 = 3;
			break;
		default:
			BUG();
			return;
		}

		val = FLD_MOD(val, chan, shift, shift);
		val = FLD_MOD(val, chan2, 31, 30);
	} else {
		val = FLD_MOD(val, channel, shift, shift);
	}
	dispc_write_reg(DISPC_OVL_ATTRIBUTES(plane), val);
}

static enum omap_channel dispc_ovl_get_channel_out(enum omap_plane_id plane)
{
	int shift;
	u32 val;

	switch (plane) {
	case OMAP_DSS_GFX:
		shift = 8;
		break;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
	case OMAP_DSS_VIDEO3:
		shift = 16;
		break;
	default:
		BUG();
		return 0;
	}

	val = dispc_read_reg(DISPC_OVL_ATTRIBUTES(plane));

	if (FLD_GET(val, shift, shift) == 1)
		return OMAP_DSS_CHANNEL_DIGIT;

	if (!dss_has_feature(FEAT_MGR_LCD2))
		return OMAP_DSS_CHANNEL_LCD;

	switch (FLD_GET(val, 31, 30)) {
	case 0:
	default:
		return OMAP_DSS_CHANNEL_LCD;
	case 1:
		return OMAP_DSS_CHANNEL_LCD2;
	case 2:
		return OMAP_DSS_CHANNEL_LCD3;
	case 3:
		return OMAP_DSS_CHANNEL_WB;
	}
}

void dispc_wb_set_channel_in(enum dss_writeback_channel channel)
{
	enum omap_plane_id plane = OMAP_DSS_WB;

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), channel, 18, 16);
}

static void dispc_ovl_set_burst_size(enum omap_plane_id plane,
		enum omap_burst_size burst_size)
{
	static const unsigned shifts[] = { 6, 14, 14, 14, 14, };
	int shift;

	shift = shifts[plane];
	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), burst_size, shift + 1, shift);
}

static void dispc_configure_burst_sizes(void)
{
	int i;
	const int burst_size = BURST_SIZE_X8;

	/* Configure burst size always to maximum size */
	for (i = 0; i < dss_feat_get_num_ovls(); ++i)
		dispc_ovl_set_burst_size(i, burst_size);
	if (dispc.feat->has_writeback)
		dispc_ovl_set_burst_size(OMAP_DSS_WB, burst_size);
}

static u32 dispc_ovl_get_burst_size(enum omap_plane_id plane)
{
	unsigned unit = dss_feat_get_burst_size_unit();
	/* burst multiplier is always x8 (see dispc_configure_burst_sizes()) */
	return unit * 8;
}

static const u32 *dispc_ovl_get_color_modes(enum omap_plane_id plane)
{
	return dss_feat_get_supported_color_modes(plane);
}

static int dispc_get_num_ovls(void)
{
	return dss_feat_get_num_ovls();
}

static void dispc_mgr_enable_cpr(enum omap_channel channel, bool enable)
{
	if (channel == OMAP_DSS_CHANNEL_DIGIT)
		return;

	mgr_fld_write(channel, DISPC_MGR_FLD_CPR, enable);
}

static void dispc_mgr_set_cpr_coef(enum omap_channel channel,
		const struct omap_dss_cpr_coefs *coefs)
{
	u32 coef_r, coef_g, coef_b;

	if (!dss_mgr_is_lcd(channel))
		return;

	coef_r = FLD_VAL(coefs->rr, 31, 22) | FLD_VAL(coefs->rg, 20, 11) |
		FLD_VAL(coefs->rb, 9, 0);
	coef_g = FLD_VAL(coefs->gr, 31, 22) | FLD_VAL(coefs->gg, 20, 11) |
		FLD_VAL(coefs->gb, 9, 0);
	coef_b = FLD_VAL(coefs->br, 31, 22) | FLD_VAL(coefs->bg, 20, 11) |
		FLD_VAL(coefs->bb, 9, 0);

	dispc_write_reg(DISPC_CPR_COEF_R(channel), coef_r);
	dispc_write_reg(DISPC_CPR_COEF_G(channel), coef_g);
	dispc_write_reg(DISPC_CPR_COEF_B(channel), coef_b);
}

static void dispc_ovl_set_vid_color_conv(enum omap_plane_id plane,
					 bool enable)
{
	u32 val;

	BUG_ON(plane == OMAP_DSS_GFX);

	val = dispc_read_reg(DISPC_OVL_ATTRIBUTES(plane));
	val = FLD_MOD(val, enable, 9, 9);
	dispc_write_reg(DISPC_OVL_ATTRIBUTES(plane), val);
}

static void dispc_ovl_enable_replication(enum omap_plane_id plane,
		enum omap_overlay_caps caps, bool enable)
{
	static const unsigned shifts[] = { 5, 10, 10, 10 };
	int shift;

	if ((caps & OMAP_DSS_OVL_CAP_REPLICATION) == 0)
		return;

	shift = shifts[plane];
	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), enable, shift, shift);
}

static void dispc_mgr_set_size(enum omap_channel channel, u16 width,
		u16 height)
{
	u32 val;

	val = FLD_VAL(height - 1, dispc.feat->mgr_height_start, 16) |
		FLD_VAL(width - 1, dispc.feat->mgr_width_start, 0);

	dispc_write_reg(DISPC_SIZE_MGR(channel), val);
}

static void dispc_init_fifos(void)
{
	u32 size;
	int fifo;
	u8 start, end;
	u32 unit;
	int i;

	unit = dss_feat_get_buffer_size_unit();

	dss_feat_get_reg_field(FEAT_REG_FIFOSIZE, &start, &end);

	for (fifo = 0; fifo < dispc.feat->num_fifos; ++fifo) {
		size = REG_GET(DISPC_OVL_FIFO_SIZE_STATUS(fifo), start, end);
		size *= unit;
		dispc.fifo_size[fifo] = size;

		/*
		 * By default fifos are mapped directly to overlays, fifo 0 to
		 * ovl 0, fifo 1 to ovl 1, etc.
		 */
		dispc.fifo_assignment[fifo] = fifo;
	}

	/*
	 * The GFX fifo on OMAP4 is smaller than the other fifos. The small fifo
	 * causes problems with certain use cases, like using the tiler in 2D
	 * mode. The below hack swaps the fifos of GFX and WB planes, thus
	 * giving GFX plane a larger fifo. WB but should work fine with a
	 * smaller fifo.
	 */
	if (dispc.feat->gfx_fifo_workaround) {
		u32 v;

		v = dispc_read_reg(DISPC_GLOBAL_BUFFER);

		v = FLD_MOD(v, 4, 2, 0); /* GFX BUF top to WB */
		v = FLD_MOD(v, 4, 5, 3); /* GFX BUF bottom to WB */
		v = FLD_MOD(v, 0, 26, 24); /* WB BUF top to GFX */
		v = FLD_MOD(v, 0, 29, 27); /* WB BUF bottom to GFX */

		dispc_write_reg(DISPC_GLOBAL_BUFFER, v);

		dispc.fifo_assignment[OMAP_DSS_GFX] = OMAP_DSS_WB;
		dispc.fifo_assignment[OMAP_DSS_WB] = OMAP_DSS_GFX;
	}

	/*
	 * Setup default fifo thresholds.
	 */
	for (i = 0; i < dss_feat_get_num_ovls(); ++i) {
		u32 low, high;
		const bool use_fifomerge = false;
		const bool manual_update = false;

		dispc_ovl_compute_fifo_thresholds(i, &low, &high,
			use_fifomerge, manual_update);

		dispc_ovl_set_fifo_threshold(i, low, high);
	}

	if (dispc.feat->has_writeback) {
		u32 low, high;
		const bool use_fifomerge = false;
		const bool manual_update = false;

		dispc_ovl_compute_fifo_thresholds(OMAP_DSS_WB, &low, &high,
			use_fifomerge, manual_update);

		dispc_ovl_set_fifo_threshold(OMAP_DSS_WB, low, high);
	}
}

static u32 dispc_ovl_get_fifo_size(enum omap_plane_id plane)
{
	int fifo;
	u32 size = 0;

	for (fifo = 0; fifo < dispc.feat->num_fifos; ++fifo) {
		if (dispc.fifo_assignment[fifo] == plane)
			size += dispc.fifo_size[fifo];
	}

	return size;
}

void dispc_ovl_set_fifo_threshold(enum omap_plane_id plane, u32 low,
				  u32 high)
{
	u8 hi_start, hi_end, lo_start, lo_end;
	u32 unit;

	unit = dss_feat_get_buffer_size_unit();

	WARN_ON(low % unit != 0);
	WARN_ON(high % unit != 0);

	low /= unit;
	high /= unit;

	dss_feat_get_reg_field(FEAT_REG_FIFOHIGHTHRESHOLD, &hi_start, &hi_end);
	dss_feat_get_reg_field(FEAT_REG_FIFOLOWTHRESHOLD, &lo_start, &lo_end);

	DSSDBG("fifo(%d) threshold (bytes), old %u/%u, new %u/%u\n",
			plane,
			REG_GET(DISPC_OVL_FIFO_THRESHOLD(plane),
				lo_start, lo_end) * unit,
			REG_GET(DISPC_OVL_FIFO_THRESHOLD(plane),
				hi_start, hi_end) * unit,
			low * unit, high * unit);

	dispc_write_reg(DISPC_OVL_FIFO_THRESHOLD(plane),
			FLD_VAL(high, hi_start, hi_end) |
			FLD_VAL(low, lo_start, lo_end));

	/*
	 * configure the preload to the pipeline's high threhold, if HT it's too
	 * large for the preload field, set the threshold to the maximum value
	 * that can be held by the preload register
	 */
	if (dss_has_feature(FEAT_PRELOAD) && dispc.feat->set_max_preload &&
			plane != OMAP_DSS_WB)
		dispc_write_reg(DISPC_OVL_PRELOAD(plane), min(high, 0xfffu));
}

void dispc_enable_fifomerge(bool enable)
{
	if (!dss_has_feature(FEAT_FIFO_MERGE)) {
		WARN_ON(enable);
		return;
	}

	DSSDBG("FIFO merge %s\n", enable ? "enabled" : "disabled");
	REG_FLD_MOD(DISPC_CONFIG, enable ? 1 : 0, 14, 14);
}

void dispc_ovl_compute_fifo_thresholds(enum omap_plane_id plane,
		u32 *fifo_low, u32 *fifo_high, bool use_fifomerge,
		bool manual_update)
{
	/*
	 * All sizes are in bytes. Both the buffer and burst are made of
	 * buffer_units, and the fifo thresholds must be buffer_unit aligned.
	 */

	unsigned buf_unit = dss_feat_get_buffer_size_unit();
	unsigned ovl_fifo_size, total_fifo_size, burst_size;
	int i;

	burst_size = dispc_ovl_get_burst_size(plane);
	ovl_fifo_size = dispc_ovl_get_fifo_size(plane);

	if (use_fifomerge) {
		total_fifo_size = 0;
		for (i = 0; i < dss_feat_get_num_ovls(); ++i)
			total_fifo_size += dispc_ovl_get_fifo_size(i);
	} else {
		total_fifo_size = ovl_fifo_size;
	}

	/*
	 * We use the same low threshold for both fifomerge and non-fifomerge
	 * cases, but for fifomerge we calculate the high threshold using the
	 * combined fifo size
	 */

	if (manual_update && dss_has_feature(FEAT_OMAP3_DSI_FIFO_BUG)) {
		*fifo_low = ovl_fifo_size - burst_size * 2;
		*fifo_high = total_fifo_size - burst_size;
	} else if (plane == OMAP_DSS_WB) {
		/*
		 * Most optimal configuration for writeback is to push out data
		 * to the interconnect the moment writeback pushes enough pixels
		 * in the FIFO to form a burst
		 */
		*fifo_low = 0;
		*fifo_high = burst_size;
	} else {
		*fifo_low = ovl_fifo_size - burst_size;
		*fifo_high = total_fifo_size - buf_unit;
	}
}

static void dispc_ovl_set_mflag(enum omap_plane_id plane, bool enable)
{
	int bit;

	if (plane == OMAP_DSS_GFX)
		bit = 14;
	else
		bit = 23;

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), enable, bit, bit);
}

static void dispc_ovl_set_mflag_threshold(enum omap_plane_id plane,
	int low, int high)
{
	dispc_write_reg(DISPC_OVL_MFLAG_THRESHOLD(plane),
		FLD_VAL(high, 31, 16) |	FLD_VAL(low, 15, 0));
}

static void dispc_init_mflag(void)
{
	int i;

	/*
	 * HACK: NV12 color format and MFLAG seem to have problems working
	 * together: using two displays, and having an NV12 overlay on one of
	 * the displays will cause underflows/synclosts when MFLAG_CTRL=2.
	 * Changing MFLAG thresholds and PRELOAD to certain values seem to
	 * remove the errors, but there doesn't seem to be a clear logic on
	 * which values work and which not.
	 *
	 * As a work-around, set force MFLAG to always on.
	 */
	dispc_write_reg(DISPC_GLOBAL_MFLAG_ATTRIBUTE,
		(1 << 0) |	/* MFLAG_CTRL = force always on */
		(0 << 2));	/* MFLAG_START = disable */

	for (i = 0; i < dss_feat_get_num_ovls(); ++i) {
		u32 size = dispc_ovl_get_fifo_size(i);
		u32 unit = dss_feat_get_buffer_size_unit();
		u32 low, high;

		dispc_ovl_set_mflag(i, true);

		/*
		 * Simulation team suggests below thesholds:
		 * HT = fifosize * 5 / 8;
		 * LT = fifosize * 4 / 8;
		 */

		low = size * 4 / 8 / unit;
		high = size * 5 / 8 / unit;

		dispc_ovl_set_mflag_threshold(i, low, high);
	}

	if (dispc.feat->has_writeback) {
		u32 size = dispc_ovl_get_fifo_size(OMAP_DSS_WB);
		u32 unit = dss_feat_get_buffer_size_unit();
		u32 low, high;

		dispc_ovl_set_mflag(OMAP_DSS_WB, true);

		/*
		 * Simulation team suggests below thesholds:
		 * HT = fifosize * 5 / 8;
		 * LT = fifosize * 4 / 8;
		 */

		low = size * 4 / 8 / unit;
		high = size * 5 / 8 / unit;

		dispc_ovl_set_mflag_threshold(OMAP_DSS_WB, low, high);
	}
}

static void dispc_ovl_set_fir(enum omap_plane_id plane,
				int hinc, int vinc,
				enum omap_color_component color_comp)
{
	u32 val;

	if (color_comp == DISPC_COLOR_COMPONENT_RGB_Y) {
		u8 hinc_start, hinc_end, vinc_start, vinc_end;

		dss_feat_get_reg_field(FEAT_REG_FIRHINC,
					&hinc_start, &hinc_end);
		dss_feat_get_reg_field(FEAT_REG_FIRVINC,
					&vinc_start, &vinc_end);
		val = FLD_VAL(vinc, vinc_start, vinc_end) |
				FLD_VAL(hinc, hinc_start, hinc_end);

		dispc_write_reg(DISPC_OVL_FIR(plane), val);
	} else {
		val = FLD_VAL(vinc, 28, 16) | FLD_VAL(hinc, 12, 0);
		dispc_write_reg(DISPC_OVL_FIR2(plane), val);
	}
}

static void dispc_ovl_set_vid_accu0(enum omap_plane_id plane, int haccu,
				    int vaccu)
{
	u32 val;
	u8 hor_start, hor_end, vert_start, vert_end;

	dss_feat_get_reg_field(FEAT_REG_HORIZONTALACCU, &hor_start, &hor_end);
	dss_feat_get_reg_field(FEAT_REG_VERTICALACCU, &vert_start, &vert_end);

	val = FLD_VAL(vaccu, vert_start, vert_end) |
			FLD_VAL(haccu, hor_start, hor_end);

	dispc_write_reg(DISPC_OVL_ACCU0(plane), val);
}

static void dispc_ovl_set_vid_accu1(enum omap_plane_id plane, int haccu,
				    int vaccu)
{
	u32 val;
	u8 hor_start, hor_end, vert_start, vert_end;

	dss_feat_get_reg_field(FEAT_REG_HORIZONTALACCU, &hor_start, &hor_end);
	dss_feat_get_reg_field(FEAT_REG_VERTICALACCU, &vert_start, &vert_end);

	val = FLD_VAL(vaccu, vert_start, vert_end) |
			FLD_VAL(haccu, hor_start, hor_end);

	dispc_write_reg(DISPC_OVL_ACCU1(plane), val);
}

static void dispc_ovl_set_vid_accu2_0(enum omap_plane_id plane, int haccu,
		int vaccu)
{
	u32 val;

	val = FLD_VAL(vaccu, 26, 16) | FLD_VAL(haccu, 10, 0);
	dispc_write_reg(DISPC_OVL_ACCU2_0(plane), val);
}

static void dispc_ovl_set_vid_accu2_1(enum omap_plane_id plane, int haccu,
		int vaccu)
{
	u32 val;

	val = FLD_VAL(vaccu, 26, 16) | FLD_VAL(haccu, 10, 0);
	dispc_write_reg(DISPC_OVL_ACCU2_1(plane), val);
}

static void dispc_ovl_set_scale_param(enum omap_plane_id plane,
		u16 orig_width, u16 orig_height,
		u16 out_width, u16 out_height,
		bool five_taps, u8 rotation,
		enum omap_color_component color_comp)
{
	int fir_hinc, fir_vinc;

	fir_hinc = 1024 * orig_width / out_width;
	fir_vinc = 1024 * orig_height / out_height;

	dispc_ovl_set_scale_coef(plane, fir_hinc, fir_vinc, five_taps,
				color_comp);
	dispc_ovl_set_fir(plane, fir_hinc, fir_vinc, color_comp);
}

static void dispc_ovl_set_accu_uv(enum omap_plane_id plane,
		u16 orig_width,	u16 orig_height, u16 out_width, u16 out_height,
		bool ilace, u32 fourcc, u8 rotation)
{
	int h_accu2_0, h_accu2_1;
	int v_accu2_0, v_accu2_1;
	int chroma_hinc, chroma_vinc;
	int idx;

	struct accu {
		s8 h0_m, h0_n;
		s8 h1_m, h1_n;
		s8 v0_m, v0_n;
		s8 v1_m, v1_n;
	};

	const struct accu *accu_table;
	const struct accu *accu_val;

	static const struct accu accu_nv12[4] = {
		{  0, 1,  0, 1 , -1, 2, 0, 1 },
		{  1, 2, -3, 4 ,  0, 1, 0, 1 },
		{ -1, 1,  0, 1 , -1, 2, 0, 1 },
		{ -1, 2, -1, 2 , -1, 1, 0, 1 },
	};

	static const struct accu accu_nv12_ilace[4] = {
		{  0, 1,  0, 1 , -3, 4, -1, 4 },
		{ -1, 4, -3, 4 ,  0, 1,  0, 1 },
		{ -1, 1,  0, 1 , -1, 4, -3, 4 },
		{ -3, 4, -3, 4 , -1, 1,  0, 1 },
	};

	static const struct accu accu_yuv[4] = {
		{  0, 1, 0, 1,  0, 1, 0, 1 },
		{  0, 1, 0, 1,  0, 1, 0, 1 },
		{ -1, 1, 0, 1,  0, 1, 0, 1 },
		{  0, 1, 0, 1, -1, 1, 0, 1 },
	};

	/* Note: DSS HW rotates clockwise, DRM_MODE_ROTATE_* counter-clockwise */
	switch (rotation & DRM_MODE_ROTATE_MASK) {
	default:
	case DRM_MODE_ROTATE_0:
		idx = 0;
		break;
	case DRM_MODE_ROTATE_90:
		idx = 3;
		break;
	case DRM_MODE_ROTATE_180:
		idx = 2;
		break;
	case DRM_MODE_ROTATE_270:
		idx = 1;
		break;
	}

	switch (fourcc) {
	case DRM_FORMAT_NV12:
		if (ilace)
			accu_table = accu_nv12_ilace;
		else
			accu_table = accu_nv12;
		break;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
		accu_table = accu_yuv;
		break;
	default:
		BUG();
		return;
	}

	accu_val = &accu_table[idx];

	chroma_hinc = 1024 * orig_width / out_width;
	chroma_vinc = 1024 * orig_height / out_height;

	h_accu2_0 = (accu_val->h0_m * chroma_hinc / accu_val->h0_n) % 1024;
	h_accu2_1 = (accu_val->h1_m * chroma_hinc / accu_val->h1_n) % 1024;
	v_accu2_0 = (accu_val->v0_m * chroma_vinc / accu_val->v0_n) % 1024;
	v_accu2_1 = (accu_val->v1_m * chroma_vinc / accu_val->v1_n) % 1024;

	dispc_ovl_set_vid_accu2_0(plane, h_accu2_0, v_accu2_0);
	dispc_ovl_set_vid_accu2_1(plane, h_accu2_1, v_accu2_1);
}

static void dispc_ovl_set_scaling_common(enum omap_plane_id plane,
		u16 orig_width, u16 orig_height,
		u16 out_width, u16 out_height,
		bool ilace, bool five_taps,
		bool fieldmode, u32 fourcc,
		u8 rotation)
{
	int accu0 = 0;
	int accu1 = 0;
	u32 l;

	dispc_ovl_set_scale_param(plane, orig_width, orig_height,
				out_width, out_height, five_taps,
				rotation, DISPC_COLOR_COMPONENT_RGB_Y);
	l = dispc_read_reg(DISPC_OVL_ATTRIBUTES(plane));

	/* RESIZEENABLE and VERTICALTAPS */
	l &= ~((0x3 << 5) | (0x1 << 21));
	l |= (orig_width != out_width) ? (1 << 5) : 0;
	l |= (orig_height != out_height) ? (1 << 6) : 0;
	l |= five_taps ? (1 << 21) : 0;

	/* VRESIZECONF and HRESIZECONF */
	if (dss_has_feature(FEAT_RESIZECONF)) {
		l &= ~(0x3 << 7);
		l |= (orig_width <= out_width) ? 0 : (1 << 7);
		l |= (orig_height <= out_height) ? 0 : (1 << 8);
	}

	/* LINEBUFFERSPLIT */
	if (dss_has_feature(FEAT_LINEBUFFERSPLIT)) {
		l &= ~(0x1 << 22);
		l |= five_taps ? (1 << 22) : 0;
	}

	dispc_write_reg(DISPC_OVL_ATTRIBUTES(plane), l);

	/*
	 * field 0 = even field = bottom field
	 * field 1 = odd field = top field
	 */
	if (ilace && !fieldmode) {
		accu1 = 0;
		accu0 = ((1024 * orig_height / out_height) / 2) & 0x3ff;
		if (accu0 >= 1024/2) {
			accu1 = 1024/2;
			accu0 -= accu1;
		}
	}

	dispc_ovl_set_vid_accu0(plane, 0, accu0);
	dispc_ovl_set_vid_accu1(plane, 0, accu1);
}

static void dispc_ovl_set_scaling_uv(enum omap_plane_id plane,
		u16 orig_width, u16 orig_height,
		u16 out_width, u16 out_height,
		bool ilace, bool five_taps,
		bool fieldmode, u32 fourcc,
		u8 rotation)
{
	int scale_x = out_width != orig_width;
	int scale_y = out_height != orig_height;
	bool chroma_upscale = plane != OMAP_DSS_WB;

	if (!dss_has_feature(FEAT_HANDLE_UV_SEPARATE))
		return;

	if (!format_is_yuv(fourcc)) {
		/* reset chroma resampling for RGB formats  */
		if (plane != OMAP_DSS_WB)
			REG_FLD_MOD(DISPC_OVL_ATTRIBUTES2(plane), 0, 8, 8);
		return;
	}

	dispc_ovl_set_accu_uv(plane, orig_width, orig_height, out_width,
			out_height, ilace, fourcc, rotation);

	switch (fourcc) {
	case DRM_FORMAT_NV12:
		if (chroma_upscale) {
			/* UV is subsampled by 2 horizontally and vertically */
			orig_height >>= 1;
			orig_width >>= 1;
		} else {
			/* UV is downsampled by 2 horizontally and vertically */
			orig_height <<= 1;
			orig_width <<= 1;
		}

		break;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
		/* For YUV422 with 90/270 rotation, we don't upsample chroma */
		if (!drm_rotation_90_or_270(rotation)) {
			if (chroma_upscale)
				/* UV is subsampled by 2 horizontally */
				orig_width >>= 1;
			else
				/* UV is downsampled by 2 horizontally */
				orig_width <<= 1;
		}

		/* must use FIR for YUV422 if rotated */
		if ((rotation & DRM_MODE_ROTATE_MASK) != DRM_MODE_ROTATE_0)
			scale_x = scale_y = true;

		break;
	default:
		BUG();
		return;
	}

	if (out_width != orig_width)
		scale_x = true;
	if (out_height != orig_height)
		scale_y = true;

	dispc_ovl_set_scale_param(plane, orig_width, orig_height,
			out_width, out_height, five_taps,
				rotation, DISPC_COLOR_COMPONENT_UV);

	if (plane != OMAP_DSS_WB)
		REG_FLD_MOD(DISPC_OVL_ATTRIBUTES2(plane),
			(scale_x || scale_y) ? 1 : 0, 8, 8);

	/* set H scaling */
	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), scale_x ? 1 : 0, 5, 5);
	/* set V scaling */
	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), scale_y ? 1 : 0, 6, 6);
}

static void dispc_ovl_set_scaling(enum omap_plane_id plane,
		u16 orig_width, u16 orig_height,
		u16 out_width, u16 out_height,
		bool ilace, bool five_taps,
		bool fieldmode, u32 fourcc,
		u8 rotation)
{
	BUG_ON(plane == OMAP_DSS_GFX);

	dispc_ovl_set_scaling_common(plane,
			orig_width, orig_height,
			out_width, out_height,
			ilace, five_taps,
			fieldmode, fourcc,
			rotation);

	dispc_ovl_set_scaling_uv(plane,
		orig_width, orig_height,
		out_width, out_height,
		ilace, five_taps,
		fieldmode, fourcc,
		rotation);
}

static void dispc_ovl_set_rotation_attrs(enum omap_plane_id plane, u8 rotation,
		enum omap_dss_rotation_type rotation_type, u32 fourcc)
{
	bool row_repeat = false;
	int vidrot = 0;

	/* Note: DSS HW rotates clockwise, DRM_MODE_ROTATE_* counter-clockwise */
	if (fourcc == DRM_FORMAT_YUYV || fourcc == DRM_FORMAT_UYVY) {

		if (rotation & DRM_MODE_REFLECT_X) {
			switch (rotation & DRM_MODE_ROTATE_MASK) {
			case DRM_MODE_ROTATE_0:
				vidrot = 2;
				break;
			case DRM_MODE_ROTATE_90:
				vidrot = 3;
				break;
			case DRM_MODE_ROTATE_180:
				vidrot = 0;
				break;
			case DRM_MODE_ROTATE_270:
				vidrot = 1;
				break;
			}
		} else {
			switch (rotation & DRM_MODE_ROTATE_MASK) {
			case DRM_MODE_ROTATE_0:
				vidrot = 0;
				break;
			case DRM_MODE_ROTATE_90:
				vidrot = 3;
				break;
			case DRM_MODE_ROTATE_180:
				vidrot = 2;
				break;
			case DRM_MODE_ROTATE_270:
				vidrot = 1;
				break;
			}
		}

		if (drm_rotation_90_or_270(rotation))
			row_repeat = true;
		else
			row_repeat = false;
	}

	/*
	 * OMAP4/5 Errata i631:
	 * NV12 in 1D mode must use ROTATION=1. Otherwise DSS will fetch extra
	 * rows beyond the framebuffer, which may cause OCP error.
	 */
	if (fourcc == DRM_FORMAT_NV12 && rotation_type != OMAP_DSS_ROT_TILER)
		vidrot = 1;

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), vidrot, 13, 12);
	if (dss_has_feature(FEAT_ROWREPEATENABLE))
		REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane),
			row_repeat ? 1 : 0, 18, 18);

	if (dss_feat_color_mode_supported(plane, DRM_FORMAT_NV12)) {
		bool doublestride =
			fourcc == DRM_FORMAT_NV12 &&
			rotation_type == OMAP_DSS_ROT_TILER &&
			!drm_rotation_90_or_270(rotation);

		/* DOUBLESTRIDE */
		REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), doublestride, 22, 22);
	}
}

static int color_mode_to_bpp(u32 fourcc)
{
	switch (fourcc) {
	case DRM_FORMAT_NV12:
		return 8;
	case DRM_FORMAT_RGBX4444:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_ARGB4444:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_RGBA4444:
	case DRM_FORMAT_XRGB4444:
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_XRGB1555:
		return 16;
	case DRM_FORMAT_RGB888:
		return 24;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_RGBX8888:
		return 32;
	default:
		BUG();
		return 0;
	}
}

static s32 pixinc(int pixels, u8 ps)
{
	if (pixels == 1)
		return 1;
	else if (pixels > 1)
		return 1 + (pixels - 1) * ps;
	else if (pixels < 0)
		return 1 - (-pixels + 1) * ps;
	else
		BUG();
		return 0;
}

static void calc_offset(u16 screen_width, u16 width,
		u32 fourcc, bool fieldmode,
		unsigned int field_offset, unsigned *offset0, unsigned *offset1,
		s32 *row_inc, s32 *pix_inc, int x_predecim, int y_predecim)
{
	u8 ps;

	ps = color_mode_to_bpp(fourcc) / 8;

	DSSDBG("scrw %d, width %d\n", screen_width, width);

	/*
	 * field 0 = even field = bottom field
	 * field 1 = odd field = top field
	 */
	*offset0 = field_offset * screen_width * ps;
	*offset1 = 0;

	*row_inc = pixinc(1 + (y_predecim * screen_width - width * x_predecim) +
			(fieldmode ? screen_width : 0), ps);
	if (fourcc == DRM_FORMAT_YUYV || fourcc == DRM_FORMAT_UYVY)
		*pix_inc = pixinc(x_predecim, 2 * ps);
	else
		*pix_inc = pixinc(x_predecim, ps);
}

/*
 * This function is used to avoid synclosts in OMAP3, because of some
 * undocumented horizontal position and timing related limitations.
 */
static int check_horiz_timing_omap3(unsigned long pclk, unsigned long lclk,
		const struct videomode *vm, u16 pos_x,
		u16 width, u16 height, u16 out_width, u16 out_height,
		bool five_taps)
{
	const int ds = DIV_ROUND_UP(height, out_height);
	unsigned long nonactive;
	static const u8 limits[3] = { 8, 10, 20 };
	u64 val, blank;
	int i;

	nonactive = vm->hactive + vm->hfront_porch + vm->hsync_len +
		    vm->hback_porch - out_width;

	i = 0;
	if (out_height < height)
		i++;
	if (out_width < width)
		i++;
	blank = div_u64((u64)(vm->hback_porch + vm->hsync_len + vm->hfront_porch) *
			lclk, pclk);
	DSSDBG("blanking period + ppl = %llu (limit = %u)\n", blank, limits[i]);
	if (blank <= limits[i])
		return -EINVAL;

	/* FIXME add checks for 3-tap filter once the limitations are known */
	if (!five_taps)
		return 0;

	/*
	 * Pixel data should be prepared before visible display point starts.
	 * So, atleast DS-2 lines must have already been fetched by DISPC
	 * during nonactive - pos_x period.
	 */
	val = div_u64((u64)(nonactive - pos_x) * lclk, pclk);
	DSSDBG("(nonactive - pos_x) * pcd = %llu max(0, DS - 2) * width = %d\n",
		val, max(0, ds - 2) * width);
	if (val < max(0, ds - 2) * width)
		return -EINVAL;

	/*
	 * All lines need to be refilled during the nonactive period of which
	 * only one line can be loaded during the active period. So, atleast
	 * DS - 1 lines should be loaded during nonactive period.
	 */
	val =  div_u64((u64)nonactive * lclk, pclk);
	DSSDBG("nonactive * pcd  = %llu, max(0, DS - 1) * width = %d\n",
		val, max(0, ds - 1) * width);
	if (val < max(0, ds - 1) * width)
		return -EINVAL;

	return 0;
}

static unsigned long calc_core_clk_five_taps(unsigned long pclk,
		const struct videomode *vm, u16 width,
		u16 height, u16 out_width, u16 out_height,
		u32 fourcc)
{
	u32 core_clk = 0;
	u64 tmp;

	if (height <= out_height && width <= out_width)
		return (unsigned long) pclk;

	if (height > out_height) {
		unsigned int ppl = vm->hactive;

		tmp = (u64)pclk * height * out_width;
		do_div(tmp, 2 * out_height * ppl);
		core_clk = tmp;

		if (height > 2 * out_height) {
			if (ppl == out_width)
				return 0;

			tmp = (u64)pclk * (height - 2 * out_height) * out_width;
			do_div(tmp, 2 * out_height * (ppl - out_width));
			core_clk = max_t(u32, core_clk, tmp);
		}
	}

	if (width > out_width) {
		tmp = (u64)pclk * width;
		do_div(tmp, out_width);
		core_clk = max_t(u32, core_clk, tmp);

		if (fourcc == DRM_FORMAT_XRGB8888)
			core_clk <<= 1;
	}

	return core_clk;
}

static unsigned long calc_core_clk_24xx(unsigned long pclk, u16 width,
		u16 height, u16 out_width, u16 out_height, bool mem_to_mem)
{
	if (height > out_height && width > out_width)
		return pclk * 4;
	else
		return pclk * 2;
}

static unsigned long calc_core_clk_34xx(unsigned long pclk, u16 width,
		u16 height, u16 out_width, u16 out_height, bool mem_to_mem)
{
	unsigned int hf, vf;

	/*
	 * FIXME how to determine the 'A' factor
	 * for the no downscaling case ?
	 */

	if (width > 3 * out_width)
		hf = 4;
	else if (width > 2 * out_width)
		hf = 3;
	else if (width > out_width)
		hf = 2;
	else
		hf = 1;
	if (height > out_height)
		vf = 2;
	else
		vf = 1;

	return pclk * vf * hf;
}

static unsigned long calc_core_clk_44xx(unsigned long pclk, u16 width,
		u16 height, u16 out_width, u16 out_height, bool mem_to_mem)
{
	/*
	 * If the overlay/writeback is in mem to mem mode, there are no
	 * downscaling limitations with respect to pixel clock, return 1 as
	 * required core clock to represent that we have sufficient enough
	 * core clock to do maximum downscaling
	 */
	if (mem_to_mem)
		return 1;

	if (width > out_width)
		return DIV_ROUND_UP(pclk, out_width) * width;
	else
		return pclk;
}

static int dispc_ovl_calc_scaling_24xx(unsigned long pclk, unsigned long lclk,
		const struct videomode *vm,
		u16 width, u16 height, u16 out_width, u16 out_height,
		u32 fourcc, bool *five_taps,
		int *x_predecim, int *y_predecim, int *decim_x, int *decim_y,
		u16 pos_x, unsigned long *core_clk, bool mem_to_mem)
{
	int error;
	u16 in_width, in_height;
	int min_factor = min(*decim_x, *decim_y);
	const int maxsinglelinewidth =
			dss_feat_get_param_max(FEAT_PARAM_LINEWIDTH);

	*five_taps = false;

	do {
		in_height = height / *decim_y;
		in_width = width / *decim_x;
		*core_clk = dispc.feat->calc_core_clk(pclk, in_width,
				in_height, out_width, out_height, mem_to_mem);
		error = (in_width > maxsinglelinewidth || !*core_clk ||
			*core_clk > dispc_core_clk_rate());
		if (error) {
			if (*decim_x == *decim_y) {
				*decim_x = min_factor;
				++*decim_y;
			} else {
				swap(*decim_x, *decim_y);
				if (*decim_x < *decim_y)
					++*decim_x;
			}
		}
	} while (*decim_x <= *x_predecim && *decim_y <= *y_predecim && error);

	if (error) {
		DSSERR("failed to find scaling settings\n");
		return -EINVAL;
	}

	if (in_width > maxsinglelinewidth) {
		DSSERR("Cannot scale max input width exceeded");
		return -EINVAL;
	}
	return 0;
}

static int dispc_ovl_calc_scaling_34xx(unsigned long pclk, unsigned long lclk,
		const struct videomode *vm,
		u16 width, u16 height, u16 out_width, u16 out_height,
		u32 fourcc, bool *five_taps,
		int *x_predecim, int *y_predecim, int *decim_x, int *decim_y,
		u16 pos_x, unsigned long *core_clk, bool mem_to_mem)
{
	int error;
	u16 in_width, in_height;
	const int maxsinglelinewidth =
			dss_feat_get_param_max(FEAT_PARAM_LINEWIDTH);

	do {
		in_height = height / *decim_y;
		in_width = width / *decim_x;
		*five_taps = in_height > out_height;

		if (in_width > maxsinglelinewidth)
			if (in_height > out_height &&
						in_height < out_height * 2)
				*five_taps = false;
again:
		if (*five_taps)
			*core_clk = calc_core_clk_five_taps(pclk, vm,
						in_width, in_height, out_width,
						out_height, fourcc);
		else
			*core_clk = dispc.feat->calc_core_clk(pclk, in_width,
					in_height, out_width, out_height,
					mem_to_mem);

		error = check_horiz_timing_omap3(pclk, lclk, vm,
				pos_x, in_width, in_height, out_width,
				out_height, *five_taps);
		if (error && *five_taps) {
			*five_taps = false;
			goto again;
		}

		error = (error || in_width > maxsinglelinewidth * 2 ||
			(in_width > maxsinglelinewidth && *five_taps) ||
			!*core_clk || *core_clk > dispc_core_clk_rate());

		if (!error) {
			/* verify that we're inside the limits of scaler */
			if (in_width / 4 > out_width)
					error = 1;

			if (*five_taps) {
				if (in_height / 4 > out_height)
					error = 1;
			} else {
				if (in_height / 2 > out_height)
					error = 1;
			}
		}

		if (error)
			++*decim_y;
	} while (*decim_x <= *x_predecim && *decim_y <= *y_predecim && error);

	if (error) {
		DSSERR("failed to find scaling settings\n");
		return -EINVAL;
	}

	if (check_horiz_timing_omap3(pclk, lclk, vm, pos_x, in_width,
				in_height, out_width, out_height, *five_taps)) {
			DSSERR("horizontal timing too tight\n");
			return -EINVAL;
	}

	if (in_width > (maxsinglelinewidth * 2)) {
		DSSERR("Cannot setup scaling");
		DSSERR("width exceeds maximum width possible");
		return -EINVAL;
	}

	if (in_width > maxsinglelinewidth && *five_taps) {
		DSSERR("cannot setup scaling with five taps");
		return -EINVAL;
	}
	return 0;
}

static int dispc_ovl_calc_scaling_44xx(unsigned long pclk, unsigned long lclk,
		const struct videomode *vm,
		u16 width, u16 height, u16 out_width, u16 out_height,
		u32 fourcc, bool *five_taps,
		int *x_predecim, int *y_predecim, int *decim_x, int *decim_y,
		u16 pos_x, unsigned long *core_clk, bool mem_to_mem)
{
	u16 in_width, in_width_max;
	int decim_x_min = *decim_x;
	u16 in_height = height / *decim_y;
	const int maxsinglelinewidth =
				dss_feat_get_param_max(FEAT_PARAM_LINEWIDTH);
	const int maxdownscale = dss_feat_get_param_max(FEAT_PARAM_DOWNSCALE);

	if (mem_to_mem) {
		in_width_max = out_width * maxdownscale;
	} else {
		in_width_max = dispc_core_clk_rate() /
					DIV_ROUND_UP(pclk, out_width);
	}

	*decim_x = DIV_ROUND_UP(width, in_width_max);

	*decim_x = *decim_x > decim_x_min ? *decim_x : decim_x_min;
	if (*decim_x > *x_predecim)
		return -EINVAL;

	do {
		in_width = width / *decim_x;
	} while (*decim_x <= *x_predecim &&
			in_width > maxsinglelinewidth && ++*decim_x);

	if (in_width > maxsinglelinewidth) {
		DSSERR("Cannot scale width exceeds max line width");
		return -EINVAL;
	}

	if (*decim_x > 4 && fourcc != DRM_FORMAT_NV12) {
		/*
		 * Let's disable all scaling that requires horizontal
		 * decimation with higher factor than 4, until we have
		 * better estimates of what we can and can not
		 * do. However, NV12 color format appears to work Ok
		 * with all decimation factors.
		 *
		 * When decimating horizontally by more that 4 the dss
		 * is not able to fetch the data in burst mode. When
		 * this happens it is hard to tell if there enough
		 * bandwidth. Despite what theory says this appears to
		 * be true also for 16-bit color formats.
		 */
		DSSERR("Not enough bandwidth, too much downscaling (x-decimation factor %d > 4)", *decim_x);

		return -EINVAL;
	}

	*core_clk = dispc.feat->calc_core_clk(pclk, in_width, in_height,
				out_width, out_height, mem_to_mem);
	return 0;
}

#define DIV_FRAC(dividend, divisor) \
	((dividend) * 100 / (divisor) - ((dividend) / (divisor) * 100))

static int dispc_ovl_calc_scaling(unsigned long pclk, unsigned long lclk,
		enum omap_overlay_caps caps,
		const struct videomode *vm,
		u16 width, u16 height, u16 out_width, u16 out_height,
		u32 fourcc, bool *five_taps,
		int *x_predecim, int *y_predecim, u16 pos_x,
		enum omap_dss_rotation_type rotation_type, bool mem_to_mem)
{
	const int maxdownscale = dss_feat_get_param_max(FEAT_PARAM_DOWNSCALE);
	const int max_decim_limit = 16;
	unsigned long core_clk = 0;
	int decim_x, decim_y, ret;

	if (width == out_width && height == out_height)
		return 0;

	if (!mem_to_mem && (pclk == 0 || vm->pixelclock == 0)) {
		DSSERR("cannot calculate scaling settings: pclk is zero\n");
		return -EINVAL;
	}

	if ((caps & OMAP_DSS_OVL_CAP_SCALE) == 0)
		return -EINVAL;

	if (mem_to_mem) {
		*x_predecim = *y_predecim = 1;
	} else {
		*x_predecim = max_decim_limit;
		*y_predecim = (rotation_type == OMAP_DSS_ROT_TILER &&
				dss_has_feature(FEAT_BURST_2D)) ?
				2 : max_decim_limit;
	}

	decim_x = DIV_ROUND_UP(DIV_ROUND_UP(width, out_width), maxdownscale);
	decim_y = DIV_ROUND_UP(DIV_ROUND_UP(height, out_height), maxdownscale);

	if (decim_x > *x_predecim || out_width > width * 8)
		return -EINVAL;

	if (decim_y > *y_predecim || out_height > height * 8)
		return -EINVAL;

	ret = dispc.feat->calc_scaling(pclk, lclk, vm, width, height,
		out_width, out_height, fourcc, five_taps,
		x_predecim, y_predecim, &decim_x, &decim_y, pos_x, &core_clk,
		mem_to_mem);
	if (ret)
		return ret;

	DSSDBG("%dx%d -> %dx%d (%d.%02d x %d.%02d), decim %dx%d %dx%d (%d.%02d x %d.%02d), taps %d, req clk %lu, cur clk %lu\n",
		width, height,
		out_width, out_height,
		out_width / width, DIV_FRAC(out_width, width),
		out_height / height, DIV_FRAC(out_height, height),

		decim_x, decim_y,
		width / decim_x, height / decim_y,
		out_width / (width / decim_x), DIV_FRAC(out_width, width / decim_x),
		out_height / (height / decim_y), DIV_FRAC(out_height, height / decim_y),

		*five_taps ? 5 : 3,
		core_clk, dispc_core_clk_rate());

	if (!core_clk || core_clk > dispc_core_clk_rate()) {
		DSSERR("failed to set up scaling, "
			"required core clk rate = %lu Hz, "
			"current core clk rate = %lu Hz\n",
			core_clk, dispc_core_clk_rate());
		return -EINVAL;
	}

	*x_predecim = decim_x;
	*y_predecim = decim_y;
	return 0;
}

static int dispc_ovl_setup_common(enum omap_plane_id plane,
		enum omap_overlay_caps caps, u32 paddr, u32 p_uv_addr,
		u16 screen_width, int pos_x, int pos_y, u16 width, u16 height,
		u16 out_width, u16 out_height, u32 fourcc,
		u8 rotation, u8 zorder, u8 pre_mult_alpha,
		u8 global_alpha, enum omap_dss_rotation_type rotation_type,
		bool replication, const struct videomode *vm,
		bool mem_to_mem)
{
	bool five_taps = true;
	bool fieldmode = false;
	int r, cconv = 0;
	unsigned offset0, offset1;
	s32 row_inc;
	s32 pix_inc;
	u16 frame_width, frame_height;
	unsigned int field_offset = 0;
	u16 in_height = height;
	u16 in_width = width;
	int x_predecim = 1, y_predecim = 1;
	bool ilace = !!(vm->flags & DISPLAY_FLAGS_INTERLACED);
	unsigned long pclk = dispc_plane_pclk_rate(plane);
	unsigned long lclk = dispc_plane_lclk_rate(plane);

	if (paddr == 0 && rotation_type != OMAP_DSS_ROT_TILER)
		return -EINVAL;

	if (format_is_yuv(fourcc) && (in_width & 1)) {
		DSSERR("input width %d is not even for YUV format\n", in_width);
		return -EINVAL;
	}

	out_width = out_width == 0 ? width : out_width;
	out_height = out_height == 0 ? height : out_height;

	if (ilace && height == out_height)
		fieldmode = true;

	if (ilace) {
		if (fieldmode)
			in_height /= 2;
		pos_y /= 2;
		out_height /= 2;

		DSSDBG("adjusting for ilace: height %d, pos_y %d, "
			"out_height %d\n", in_height, pos_y,
			out_height);
	}

	if (!dss_feat_color_mode_supported(plane, fourcc))
		return -EINVAL;

	r = dispc_ovl_calc_scaling(pclk, lclk, caps, vm, in_width,
			in_height, out_width, out_height, fourcc,
			&five_taps, &x_predecim, &y_predecim, pos_x,
			rotation_type, mem_to_mem);
	if (r)
		return r;

	in_width = in_width / x_predecim;
	in_height = in_height / y_predecim;

	if (x_predecim > 1 || y_predecim > 1)
		DSSDBG("predecimation %d x %x, new input size %d x %d\n",
			x_predecim, y_predecim, in_width, in_height);

	if (format_is_yuv(fourcc) && (in_width & 1)) {
		DSSDBG("predecimated input width is not even for YUV format\n");
		DSSDBG("adjusting input width %d -> %d\n",
			in_width, in_width & ~1);

		in_width &= ~1;
	}

	if (format_is_yuv(fourcc))
		cconv = 1;

	if (ilace && !fieldmode) {
		/*
		 * when downscaling the bottom field may have to start several
		 * source lines below the top field. Unfortunately ACCUI
		 * registers will only hold the fractional part of the offset
		 * so the integer part must be added to the base address of the
		 * bottom field.
		 */
		if (!in_height || in_height == out_height)
			field_offset = 0;
		else
			field_offset = in_height / out_height / 2;
	}

	/* Fields are independent but interleaved in memory. */
	if (fieldmode)
		field_offset = 1;

	offset0 = 0;
	offset1 = 0;
	row_inc = 0;
	pix_inc = 0;

	if (plane == OMAP_DSS_WB) {
		frame_width = out_width;
		frame_height = out_height;
	} else {
		frame_width = in_width;
		frame_height = height;
	}

	calc_offset(screen_width, frame_width,
			fourcc, fieldmode, field_offset,
			&offset0, &offset1, &row_inc, &pix_inc,
			x_predecim, y_predecim);

	DSSDBG("offset0 %u, offset1 %u, row_inc %d, pix_inc %d\n",
			offset0, offset1, row_inc, pix_inc);

	dispc_ovl_set_color_mode(plane, fourcc);

	dispc_ovl_configure_burst_type(plane, rotation_type);

	if (dispc.feat->reverse_ilace_field_order)
		swap(offset0, offset1);

	dispc_ovl_set_ba0(plane, paddr + offset0);
	dispc_ovl_set_ba1(plane, paddr + offset1);

	if (fourcc == DRM_FORMAT_NV12) {
		dispc_ovl_set_ba0_uv(plane, p_uv_addr + offset0);
		dispc_ovl_set_ba1_uv(plane, p_uv_addr + offset1);
	}

	if (dispc.feat->last_pixel_inc_missing)
		row_inc += pix_inc - 1;

	dispc_ovl_set_row_inc(plane, row_inc);
	dispc_ovl_set_pix_inc(plane, pix_inc);

	DSSDBG("%d,%d %dx%d -> %dx%d\n", pos_x, pos_y, in_width,
			in_height, out_width, out_height);

	dispc_ovl_set_pos(plane, caps, pos_x, pos_y);

	dispc_ovl_set_input_size(plane, in_width, in_height);

	if (caps & OMAP_DSS_OVL_CAP_SCALE) {
		dispc_ovl_set_scaling(plane, in_width, in_height, out_width,
				   out_height, ilace, five_taps, fieldmode,
				   fourcc, rotation);
		dispc_ovl_set_output_size(plane, out_width, out_height);
		dispc_ovl_set_vid_color_conv(plane, cconv);
	}

	dispc_ovl_set_rotation_attrs(plane, rotation, rotation_type, fourcc);

	dispc_ovl_set_zorder(plane, caps, zorder);
	dispc_ovl_set_pre_mult_alpha(plane, caps, pre_mult_alpha);
	dispc_ovl_setup_global_alpha(plane, caps, global_alpha);

	dispc_ovl_enable_replication(plane, caps, replication);

	return 0;
}

static int dispc_ovl_setup(enum omap_plane_id plane,
		const struct omap_overlay_info *oi,
		const struct videomode *vm, bool mem_to_mem,
		enum omap_channel channel)
{
	int r;
	enum omap_overlay_caps caps = dss_feat_get_overlay_caps(plane);
	const bool replication = true;

	DSSDBG("dispc_ovl_setup %d, pa %pad, pa_uv %pad, sw %d, %d,%d, %dx%d ->"
		" %dx%d, cmode %x, rot %d, chan %d repl %d\n",
		plane, &oi->paddr, &oi->p_uv_addr, oi->screen_width, oi->pos_x,
		oi->pos_y, oi->width, oi->height, oi->out_width, oi->out_height,
		oi->fourcc, oi->rotation, channel, replication);

	dispc_ovl_set_channel_out(plane, channel);

	r = dispc_ovl_setup_common(plane, caps, oi->paddr, oi->p_uv_addr,
		oi->screen_width, oi->pos_x, oi->pos_y, oi->width, oi->height,
		oi->out_width, oi->out_height, oi->fourcc, oi->rotation,
		oi->zorder, oi->pre_mult_alpha, oi->global_alpha,
		oi->rotation_type, replication, vm, mem_to_mem);

	return r;
}

int dispc_wb_setup(const struct omap_dss_writeback_info *wi,
		bool mem_to_mem, const struct videomode *vm)
{
	int r;
	u32 l;
	enum omap_plane_id plane = OMAP_DSS_WB;
	const int pos_x = 0, pos_y = 0;
	const u8 zorder = 0, global_alpha = 0;
	const bool replication = true;
	bool truncation;
	int in_width = vm->hactive;
	int in_height = vm->vactive;
	enum omap_overlay_caps caps =
		OMAP_DSS_OVL_CAP_SCALE | OMAP_DSS_OVL_CAP_PRE_MULT_ALPHA;

	DSSDBG("dispc_wb_setup, pa %x, pa_uv %x, %d,%d -> %dx%d, cmode %x, "
		"rot %d\n", wi->paddr, wi->p_uv_addr, in_width,
		in_height, wi->width, wi->height, wi->fourcc, wi->rotation);

	r = dispc_ovl_setup_common(plane, caps, wi->paddr, wi->p_uv_addr,
		wi->buf_width, pos_x, pos_y, in_width, in_height, wi->width,
		wi->height, wi->fourcc, wi->rotation, zorder,
		wi->pre_mult_alpha, global_alpha, wi->rotation_type,
		replication, vm, mem_to_mem);

	switch (wi->fourcc) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_ARGB4444:
	case DRM_FORMAT_RGBA4444:
	case DRM_FORMAT_RGBX4444:
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_XRGB4444:
		truncation = true;
		break;
	default:
		truncation = false;
		break;
	}

	/* setup extra DISPC_WB_ATTRIBUTES */
	l = dispc_read_reg(DISPC_OVL_ATTRIBUTES(plane));
	l = FLD_MOD(l, truncation, 10, 10);	/* TRUNCATIONENABLE */
	l = FLD_MOD(l, mem_to_mem, 19, 19);	/* WRITEBACKMODE */
	if (mem_to_mem)
		l = FLD_MOD(l, 1, 26, 24);	/* CAPTUREMODE */
	else
		l = FLD_MOD(l, 0, 26, 24);	/* CAPTUREMODE */
	dispc_write_reg(DISPC_OVL_ATTRIBUTES(plane), l);

	if (mem_to_mem) {
		/* WBDELAYCOUNT */
		REG_FLD_MOD(DISPC_OVL_ATTRIBUTES2(plane), 0, 7, 0);
	} else {
		int wbdelay;

		wbdelay = min(vm->vfront_porch +
			      vm->vsync_len + vm->vback_porch, (u32)255);

		/* WBDELAYCOUNT */
		REG_FLD_MOD(DISPC_OVL_ATTRIBUTES2(plane), wbdelay, 7, 0);
	}

	return r;
}

static int dispc_ovl_enable(enum omap_plane_id plane, bool enable)
{
	DSSDBG("dispc_enable_plane %d, %d\n", plane, enable);

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), enable ? 1 : 0, 0, 0);

	return 0;
}

static enum omap_dss_output_id dispc_mgr_get_supported_outputs(enum omap_channel channel)
{
	return dss_feat_get_supported_outputs(channel);
}

static void dispc_lcd_enable_signal_polarity(bool act_high)
{
	if (!dss_has_feature(FEAT_LCDENABLEPOL))
		return;

	REG_FLD_MOD(DISPC_CONTROL, act_high ? 1 : 0, 29, 29);
}

void dispc_lcd_enable_signal(bool enable)
{
	if (!dss_has_feature(FEAT_LCDENABLESIGNAL))
		return;

	REG_FLD_MOD(DISPC_CONTROL, enable ? 1 : 0, 28, 28);
}

void dispc_pck_free_enable(bool enable)
{
	if (!dss_has_feature(FEAT_PCKFREEENABLE))
		return;

	REG_FLD_MOD(DISPC_CONTROL, enable ? 1 : 0, 27, 27);
}

static int dispc_get_num_mgrs(void)
{
	return dss_feat_get_num_mgrs();
}

static void dispc_mgr_enable_fifohandcheck(enum omap_channel channel, bool enable)
{
	mgr_fld_write(channel, DISPC_MGR_FLD_FIFOHANDCHECK, enable);
}


static void dispc_mgr_set_lcd_type_tft(enum omap_channel channel)
{
	mgr_fld_write(channel, DISPC_MGR_FLD_STNTFT, 1);
}

static void dispc_set_loadmode(enum omap_dss_load_mode mode)
{
	REG_FLD_MOD(DISPC_CONFIG, mode, 2, 1);
}


static void dispc_mgr_set_default_color(enum omap_channel channel, u32 color)
{
	dispc_write_reg(DISPC_DEFAULT_COLOR(channel), color);
}

static void dispc_mgr_set_trans_key(enum omap_channel ch,
		enum omap_dss_trans_key_type type,
		u32 trans_key)
{
	mgr_fld_write(ch, DISPC_MGR_FLD_TCKSELECTION, type);

	dispc_write_reg(DISPC_TRANS_COLOR(ch), trans_key);
}

static void dispc_mgr_enable_trans_key(enum omap_channel ch, bool enable)
{
	mgr_fld_write(ch, DISPC_MGR_FLD_TCKENABLE, enable);
}

static void dispc_mgr_enable_alpha_fixed_zorder(enum omap_channel ch,
		bool enable)
{
	if (!dss_has_feature(FEAT_ALPHA_FIXED_ZORDER))
		return;

	if (ch == OMAP_DSS_CHANNEL_LCD)
		REG_FLD_MOD(DISPC_CONFIG, enable, 18, 18);
	else if (ch == OMAP_DSS_CHANNEL_DIGIT)
		REG_FLD_MOD(DISPC_CONFIG, enable, 19, 19);
}

static void dispc_mgr_setup(enum omap_channel channel,
		const struct omap_overlay_manager_info *info)
{
	dispc_mgr_set_default_color(channel, info->default_color);
	dispc_mgr_set_trans_key(channel, info->trans_key_type, info->trans_key);
	dispc_mgr_enable_trans_key(channel, info->trans_enabled);
	dispc_mgr_enable_alpha_fixed_zorder(channel,
			info->partial_alpha_enabled);
	if (dss_has_feature(FEAT_CPR)) {
		dispc_mgr_enable_cpr(channel, info->cpr_enable);
		dispc_mgr_set_cpr_coef(channel, &info->cpr_coefs);
	}
}

static void dispc_mgr_set_tft_data_lines(enum omap_channel channel, u8 data_lines)
{
	int code;

	switch (data_lines) {
	case 12:
		code = 0;
		break;
	case 16:
		code = 1;
		break;
	case 18:
		code = 2;
		break;
	case 24:
		code = 3;
		break;
	default:
		BUG();
		return;
	}

	mgr_fld_write(channel, DISPC_MGR_FLD_TFTDATALINES, code);
}

static void dispc_mgr_set_io_pad_mode(enum dss_io_pad_mode mode)
{
	u32 l;
	int gpout0, gpout1;

	switch (mode) {
	case DSS_IO_PAD_MODE_RESET:
		gpout0 = 0;
		gpout1 = 0;
		break;
	case DSS_IO_PAD_MODE_RFBI:
		gpout0 = 1;
		gpout1 = 0;
		break;
	case DSS_IO_PAD_MODE_BYPASS:
		gpout0 = 1;
		gpout1 = 1;
		break;
	default:
		BUG();
		return;
	}

	l = dispc_read_reg(DISPC_CONTROL);
	l = FLD_MOD(l, gpout0, 15, 15);
	l = FLD_MOD(l, gpout1, 16, 16);
	dispc_write_reg(DISPC_CONTROL, l);
}

static void dispc_mgr_enable_stallmode(enum omap_channel channel, bool enable)
{
	mgr_fld_write(channel, DISPC_MGR_FLD_STALLMODE, enable);
}

static void dispc_mgr_set_lcd_config(enum omap_channel channel,
		const struct dss_lcd_mgr_config *config)
{
	dispc_mgr_set_io_pad_mode(config->io_pad_mode);

	dispc_mgr_enable_stallmode(channel, config->stallmode);
	dispc_mgr_enable_fifohandcheck(channel, config->fifohandcheck);

	dispc_mgr_set_clock_div(channel, &config->clock_info);

	dispc_mgr_set_tft_data_lines(channel, config->video_port_width);

	dispc_lcd_enable_signal_polarity(config->lcden_sig_polarity);

	dispc_mgr_set_lcd_type_tft(channel);
}

static bool _dispc_mgr_size_ok(u16 width, u16 height)
{
	return width <= dispc.feat->mgr_width_max &&
		height <= dispc.feat->mgr_height_max;
}

static bool _dispc_lcd_timings_ok(int hsync_len, int hfp, int hbp,
		int vsw, int vfp, int vbp)
{
	if (hsync_len < 1 || hsync_len > dispc.feat->sw_max ||
			hfp < 1 || hfp > dispc.feat->hp_max ||
			hbp < 1 || hbp > dispc.feat->hp_max ||
			vsw < 1 || vsw > dispc.feat->sw_max ||
			vfp < 0 || vfp > dispc.feat->vp_max ||
			vbp < 0 || vbp > dispc.feat->vp_max)
		return false;
	return true;
}

static bool _dispc_mgr_pclk_ok(enum omap_channel channel,
		unsigned long pclk)
{
	if (dss_mgr_is_lcd(channel))
		return pclk <= dispc.feat->max_lcd_pclk;
	else
		return pclk <= dispc.feat->max_tv_pclk;
}

bool dispc_mgr_timings_ok(enum omap_channel channel, const struct videomode *vm)
{
	if (!_dispc_mgr_size_ok(vm->hactive, vm->vactive))
		return false;

	if (!_dispc_mgr_pclk_ok(channel, vm->pixelclock))
		return false;

	if (dss_mgr_is_lcd(channel)) {
		/* TODO: OMAP4+ supports interlace for LCD outputs */
		if (vm->flags & DISPLAY_FLAGS_INTERLACED)
			return false;

		if (!_dispc_lcd_timings_ok(vm->hsync_len,
				vm->hfront_porch, vm->hback_porch,
				vm->vsync_len, vm->vfront_porch,
				vm->vback_porch))
			return false;
	}

	return true;
}

static void _dispc_mgr_set_lcd_timings(enum omap_channel channel,
				       const struct videomode *vm)
{
	u32 timing_h, timing_v, l;
	bool onoff, rf, ipc, vs, hs, de;

	timing_h = FLD_VAL(vm->hsync_len - 1, dispc.feat->sw_start, 0) |
		   FLD_VAL(vm->hfront_porch - 1, dispc.feat->fp_start, 8) |
		   FLD_VAL(vm->hback_porch - 1, dispc.feat->bp_start, 20);
	timing_v = FLD_VAL(vm->vsync_len - 1, dispc.feat->sw_start, 0) |
		   FLD_VAL(vm->vfront_porch, dispc.feat->fp_start, 8) |
		   FLD_VAL(vm->vback_porch, dispc.feat->bp_start, 20);

	dispc_write_reg(DISPC_TIMING_H(channel), timing_h);
	dispc_write_reg(DISPC_TIMING_V(channel), timing_v);

	if (vm->flags & DISPLAY_FLAGS_VSYNC_HIGH)
		vs = false;
	else
		vs = true;

	if (vm->flags & DISPLAY_FLAGS_HSYNC_HIGH)
		hs = false;
	else
		hs = true;

	if (vm->flags & DISPLAY_FLAGS_DE_HIGH)
		de = false;
	else
		de = true;

	if (vm->flags & DISPLAY_FLAGS_PIXDATA_POSEDGE)
		ipc = false;
	else
		ipc = true;

	/* always use the 'rf' setting */
	onoff = true;

	if (vm->flags & DISPLAY_FLAGS_SYNC_POSEDGE)
		rf = true;
	else
		rf = false;

	l = FLD_VAL(onoff, 17, 17) |
		FLD_VAL(rf, 16, 16) |
		FLD_VAL(de, 15, 15) |
		FLD_VAL(ipc, 14, 14) |
		FLD_VAL(hs, 13, 13) |
		FLD_VAL(vs, 12, 12);

	/* always set ALIGN bit when available */
	if (dispc.feat->supports_sync_align)
		l |= (1 << 18);

	dispc_write_reg(DISPC_POL_FREQ(channel), l);

	if (dispc.syscon_pol) {
		const int shifts[] = {
			[OMAP_DSS_CHANNEL_LCD] = 0,
			[OMAP_DSS_CHANNEL_LCD2] = 1,
			[OMAP_DSS_CHANNEL_LCD3] = 2,
		};

		u32 mask, val;

		mask = (1 << 0) | (1 << 3) | (1 << 6);
		val = (rf << 0) | (ipc << 3) | (onoff << 6);

		mask <<= 16 + shifts[channel];
		val <<= 16 + shifts[channel];

		regmap_update_bits(dispc.syscon_pol, dispc.syscon_pol_offset,
			mask, val);
	}
}

static int vm_flag_to_int(enum display_flags flags, enum display_flags high,
	enum display_flags low)
{
	if (flags & high)
		return 1;
	if (flags & low)
		return -1;
	return 0;
}

/* change name to mode? */
static void dispc_mgr_set_timings(enum omap_channel channel,
			   const struct videomode *vm)
{
	unsigned xtot, ytot;
	unsigned long ht, vt;
	struct videomode t = *vm;

	DSSDBG("channel %d xres %u yres %u\n", channel, t.hactive, t.vactive);

	if (!dispc_mgr_timings_ok(channel, &t)) {
		BUG();
		return;
	}

	if (dss_mgr_is_lcd(channel)) {
		_dispc_mgr_set_lcd_timings(channel, &t);

		xtot = t.hactive + t.hfront_porch + t.hsync_len + t.hback_porch;
		ytot = t.vactive + t.vfront_porch + t.vsync_len + t.vback_porch;

		ht = vm->pixelclock / xtot;
		vt = vm->pixelclock / xtot / ytot;

		DSSDBG("pck %lu\n", vm->pixelclock);
		DSSDBG("hsync_len %d hfp %d hbp %d vsw %d vfp %d vbp %d\n",
			t.hsync_len, t.hfront_porch, t.hback_porch,
			t.vsync_len, t.vfront_porch, t.vback_porch);
		DSSDBG("vsync_level %d hsync_level %d data_pclk_edge %d de_level %d sync_pclk_edge %d\n",
			vm_flag_to_int(t.flags, DISPLAY_FLAGS_VSYNC_HIGH, DISPLAY_FLAGS_VSYNC_LOW),
			vm_flag_to_int(t.flags, DISPLAY_FLAGS_HSYNC_HIGH, DISPLAY_FLAGS_HSYNC_LOW),
			vm_flag_to_int(t.flags, DISPLAY_FLAGS_PIXDATA_POSEDGE, DISPLAY_FLAGS_PIXDATA_NEGEDGE),
			vm_flag_to_int(t.flags, DISPLAY_FLAGS_DE_HIGH, DISPLAY_FLAGS_DE_LOW),
			vm_flag_to_int(t.flags, DISPLAY_FLAGS_SYNC_POSEDGE, DISPLAY_FLAGS_SYNC_NEGEDGE));

		DSSDBG("hsync %luHz, vsync %luHz\n", ht, vt);
	} else {
		if (t.flags & DISPLAY_FLAGS_INTERLACED)
			t.vactive /= 2;

		if (dispc.feat->supports_double_pixel)
			REG_FLD_MOD(DISPC_CONTROL,
				    !!(t.flags & DISPLAY_FLAGS_DOUBLECLK),
				    19, 17);
	}

	dispc_mgr_set_size(channel, t.hactive, t.vactive);
}

static void dispc_mgr_set_lcd_divisor(enum omap_channel channel, u16 lck_div,
		u16 pck_div)
{
	BUG_ON(lck_div < 1);
	BUG_ON(pck_div < 1);

	dispc_write_reg(DISPC_DIVISORo(channel),
			FLD_VAL(lck_div, 23, 16) | FLD_VAL(pck_div, 7, 0));

	if (!dss_has_feature(FEAT_CORE_CLK_DIV) &&
			channel == OMAP_DSS_CHANNEL_LCD)
		dispc.core_clk_rate = dispc_fclk_rate() / lck_div;
}

static void dispc_mgr_get_lcd_divisor(enum omap_channel channel, int *lck_div,
		int *pck_div)
{
	u32 l;
	l = dispc_read_reg(DISPC_DIVISORo(channel));
	*lck_div = FLD_GET(l, 23, 16);
	*pck_div = FLD_GET(l, 7, 0);
}

static unsigned long dispc_fclk_rate(void)
{
	unsigned long r;
	enum dss_clk_source src;

	src = dss_get_dispc_clk_source();

	if (src == DSS_CLK_SRC_FCK) {
		r = dss_get_dispc_clk_rate();
	} else {
		struct dss_pll *pll;
		unsigned clkout_idx;

		pll = dss_pll_find_by_src(src);
		clkout_idx = dss_pll_get_clkout_idx_for_src(src);

		r = pll->cinfo.clkout[clkout_idx];
	}

	return r;
}

static unsigned long dispc_mgr_lclk_rate(enum omap_channel channel)
{
	int lcd;
	unsigned long r;
	enum dss_clk_source src;

	/* for TV, LCLK rate is the FCLK rate */
	if (!dss_mgr_is_lcd(channel))
		return dispc_fclk_rate();

	src = dss_get_lcd_clk_source(channel);

	if (src == DSS_CLK_SRC_FCK) {
		r = dss_get_dispc_clk_rate();
	} else {
		struct dss_pll *pll;
		unsigned clkout_idx;

		pll = dss_pll_find_by_src(src);
		clkout_idx = dss_pll_get_clkout_idx_for_src(src);

		r = pll->cinfo.clkout[clkout_idx];
	}

	lcd = REG_GET(DISPC_DIVISORo(channel), 23, 16);

	return r / lcd;
}

static unsigned long dispc_mgr_pclk_rate(enum omap_channel channel)
{
	unsigned long r;

	if (dss_mgr_is_lcd(channel)) {
		int pcd;
		u32 l;

		l = dispc_read_reg(DISPC_DIVISORo(channel));

		pcd = FLD_GET(l, 7, 0);

		r = dispc_mgr_lclk_rate(channel);

		return r / pcd;
	} else {
		return dispc.tv_pclk_rate;
	}
}

void dispc_set_tv_pclk(unsigned long pclk)
{
	dispc.tv_pclk_rate = pclk;
}

static unsigned long dispc_core_clk_rate(void)
{
	return dispc.core_clk_rate;
}

static unsigned long dispc_plane_pclk_rate(enum omap_plane_id plane)
{
	enum omap_channel channel;

	if (plane == OMAP_DSS_WB)
		return 0;

	channel = dispc_ovl_get_channel_out(plane);

	return dispc_mgr_pclk_rate(channel);
}

static unsigned long dispc_plane_lclk_rate(enum omap_plane_id plane)
{
	enum omap_channel channel;

	if (plane == OMAP_DSS_WB)
		return 0;

	channel	= dispc_ovl_get_channel_out(plane);

	return dispc_mgr_lclk_rate(channel);
}

static void dispc_dump_clocks_channel(struct seq_file *s, enum omap_channel channel)
{
	int lcd, pcd;
	enum dss_clk_source lcd_clk_src;

	seq_printf(s, "- %s -\n", mgr_desc[channel].name);

	lcd_clk_src = dss_get_lcd_clk_source(channel);

	seq_printf(s, "%s clk source = %s\n", mgr_desc[channel].name,
		dss_get_clk_source_name(lcd_clk_src));

	dispc_mgr_get_lcd_divisor(channel, &lcd, &pcd);

	seq_printf(s, "lck\t\t%-16lulck div\t%u\n",
		dispc_mgr_lclk_rate(channel), lcd);
	seq_printf(s, "pck\t\t%-16lupck div\t%u\n",
		dispc_mgr_pclk_rate(channel), pcd);
}

void dispc_dump_clocks(struct seq_file *s)
{
	int lcd;
	u32 l;
	enum dss_clk_source dispc_clk_src = dss_get_dispc_clk_source();

	if (dispc_runtime_get())
		return;

	seq_printf(s, "- DISPC -\n");

	seq_printf(s, "dispc fclk source = %s\n",
			dss_get_clk_source_name(dispc_clk_src));

	seq_printf(s, "fck\t\t%-16lu\n", dispc_fclk_rate());

	if (dss_has_feature(FEAT_CORE_CLK_DIV)) {
		seq_printf(s, "- DISPC-CORE-CLK -\n");
		l = dispc_read_reg(DISPC_DIVISOR);
		lcd = FLD_GET(l, 23, 16);

		seq_printf(s, "lck\t\t%-16lulck div\t%u\n",
				(dispc_fclk_rate()/lcd), lcd);
	}

	dispc_dump_clocks_channel(s, OMAP_DSS_CHANNEL_LCD);

	if (dss_has_feature(FEAT_MGR_LCD2))
		dispc_dump_clocks_channel(s, OMAP_DSS_CHANNEL_LCD2);
	if (dss_has_feature(FEAT_MGR_LCD3))
		dispc_dump_clocks_channel(s, OMAP_DSS_CHANNEL_LCD3);

	dispc_runtime_put();
}

static void dispc_dump_regs(struct seq_file *s)
{
	int i, j;
	const char *mgr_names[] = {
		[OMAP_DSS_CHANNEL_LCD]		= "LCD",
		[OMAP_DSS_CHANNEL_DIGIT]	= "TV",
		[OMAP_DSS_CHANNEL_LCD2]		= "LCD2",
		[OMAP_DSS_CHANNEL_LCD3]		= "LCD3",
	};
	const char *ovl_names[] = {
		[OMAP_DSS_GFX]		= "GFX",
		[OMAP_DSS_VIDEO1]	= "VID1",
		[OMAP_DSS_VIDEO2]	= "VID2",
		[OMAP_DSS_VIDEO3]	= "VID3",
		[OMAP_DSS_WB]		= "WB",
	};
	const char **p_names;

#define DUMPREG(r) seq_printf(s, "%-50s %08x\n", #r, dispc_read_reg(r))

	if (dispc_runtime_get())
		return;

	/* DISPC common registers */
	DUMPREG(DISPC_REVISION);
	DUMPREG(DISPC_SYSCONFIG);
	DUMPREG(DISPC_SYSSTATUS);
	DUMPREG(DISPC_IRQSTATUS);
	DUMPREG(DISPC_IRQENABLE);
	DUMPREG(DISPC_CONTROL);
	DUMPREG(DISPC_CONFIG);
	DUMPREG(DISPC_CAPABLE);
	DUMPREG(DISPC_LINE_STATUS);
	DUMPREG(DISPC_LINE_NUMBER);
	if (dss_has_feature(FEAT_ALPHA_FIXED_ZORDER) ||
			dss_has_feature(FEAT_ALPHA_FREE_ZORDER))
		DUMPREG(DISPC_GLOBAL_ALPHA);
	if (dss_has_feature(FEAT_MGR_LCD2)) {
		DUMPREG(DISPC_CONTROL2);
		DUMPREG(DISPC_CONFIG2);
	}
	if (dss_has_feature(FEAT_MGR_LCD3)) {
		DUMPREG(DISPC_CONTROL3);
		DUMPREG(DISPC_CONFIG3);
	}
	if (dss_has_feature(FEAT_MFLAG))
		DUMPREG(DISPC_GLOBAL_MFLAG_ATTRIBUTE);

#undef DUMPREG

#define DISPC_REG(i, name) name(i)
#define DUMPREG(i, r) seq_printf(s, "%s(%s)%*s %08x\n", #r, p_names[i], \
	(int)(48 - strlen(#r) - strlen(p_names[i])), " ", \
	dispc_read_reg(DISPC_REG(i, r)))

	p_names = mgr_names;

	/* DISPC channel specific registers */
	for (i = 0; i < dss_feat_get_num_mgrs(); i++) {
		DUMPREG(i, DISPC_DEFAULT_COLOR);
		DUMPREG(i, DISPC_TRANS_COLOR);
		DUMPREG(i, DISPC_SIZE_MGR);

		if (i == OMAP_DSS_CHANNEL_DIGIT)
			continue;

		DUMPREG(i, DISPC_TIMING_H);
		DUMPREG(i, DISPC_TIMING_V);
		DUMPREG(i, DISPC_POL_FREQ);
		DUMPREG(i, DISPC_DIVISORo);

		DUMPREG(i, DISPC_DATA_CYCLE1);
		DUMPREG(i, DISPC_DATA_CYCLE2);
		DUMPREG(i, DISPC_DATA_CYCLE3);

		if (dss_has_feature(FEAT_CPR)) {
			DUMPREG(i, DISPC_CPR_COEF_R);
			DUMPREG(i, DISPC_CPR_COEF_G);
			DUMPREG(i, DISPC_CPR_COEF_B);
		}
	}

	p_names = ovl_names;

	for (i = 0; i < dss_feat_get_num_ovls(); i++) {
		DUMPREG(i, DISPC_OVL_BA0);
		DUMPREG(i, DISPC_OVL_BA1);
		DUMPREG(i, DISPC_OVL_POSITION);
		DUMPREG(i, DISPC_OVL_SIZE);
		DUMPREG(i, DISPC_OVL_ATTRIBUTES);
		DUMPREG(i, DISPC_OVL_FIFO_THRESHOLD);
		DUMPREG(i, DISPC_OVL_FIFO_SIZE_STATUS);
		DUMPREG(i, DISPC_OVL_ROW_INC);
		DUMPREG(i, DISPC_OVL_PIXEL_INC);

		if (dss_has_feature(FEAT_PRELOAD))
			DUMPREG(i, DISPC_OVL_PRELOAD);
		if (dss_has_feature(FEAT_MFLAG))
			DUMPREG(i, DISPC_OVL_MFLAG_THRESHOLD);

		if (i == OMAP_DSS_GFX) {
			DUMPREG(i, DISPC_OVL_WINDOW_SKIP);
			DUMPREG(i, DISPC_OVL_TABLE_BA);
			continue;
		}

		DUMPREG(i, DISPC_OVL_FIR);
		DUMPREG(i, DISPC_OVL_PICTURE_SIZE);
		DUMPREG(i, DISPC_OVL_ACCU0);
		DUMPREG(i, DISPC_OVL_ACCU1);
		if (dss_has_feature(FEAT_HANDLE_UV_SEPARATE)) {
			DUMPREG(i, DISPC_OVL_BA0_UV);
			DUMPREG(i, DISPC_OVL_BA1_UV);
			DUMPREG(i, DISPC_OVL_FIR2);
			DUMPREG(i, DISPC_OVL_ACCU2_0);
			DUMPREG(i, DISPC_OVL_ACCU2_1);
		}
		if (dss_has_feature(FEAT_ATTR2))
			DUMPREG(i, DISPC_OVL_ATTRIBUTES2);
	}

	if (dispc.feat->has_writeback) {
		i = OMAP_DSS_WB;
		DUMPREG(i, DISPC_OVL_BA0);
		DUMPREG(i, DISPC_OVL_BA1);
		DUMPREG(i, DISPC_OVL_SIZE);
		DUMPREG(i, DISPC_OVL_ATTRIBUTES);
		DUMPREG(i, DISPC_OVL_FIFO_THRESHOLD);
		DUMPREG(i, DISPC_OVL_FIFO_SIZE_STATUS);
		DUMPREG(i, DISPC_OVL_ROW_INC);
		DUMPREG(i, DISPC_OVL_PIXEL_INC);

		if (dss_has_feature(FEAT_MFLAG))
			DUMPREG(i, DISPC_OVL_MFLAG_THRESHOLD);

		DUMPREG(i, DISPC_OVL_FIR);
		DUMPREG(i, DISPC_OVL_PICTURE_SIZE);
		DUMPREG(i, DISPC_OVL_ACCU0);
		DUMPREG(i, DISPC_OVL_ACCU1);
		if (dss_has_feature(FEAT_HANDLE_UV_SEPARATE)) {
			DUMPREG(i, DISPC_OVL_BA0_UV);
			DUMPREG(i, DISPC_OVL_BA1_UV);
			DUMPREG(i, DISPC_OVL_FIR2);
			DUMPREG(i, DISPC_OVL_ACCU2_0);
			DUMPREG(i, DISPC_OVL_ACCU2_1);
		}
		if (dss_has_feature(FEAT_ATTR2))
			DUMPREG(i, DISPC_OVL_ATTRIBUTES2);
	}

#undef DISPC_REG
#undef DUMPREG

#define DISPC_REG(plane, name, i) name(plane, i)
#define DUMPREG(plane, name, i) \
	seq_printf(s, "%s_%d(%s)%*s %08x\n", #name, i, p_names[plane], \
	(int)(46 - strlen(#name) - strlen(p_names[plane])), " ", \
	dispc_read_reg(DISPC_REG(plane, name, i)))

	/* Video pipeline coefficient registers */

	/* start from OMAP_DSS_VIDEO1 */
	for (i = 1; i < dss_feat_get_num_ovls(); i++) {
		for (j = 0; j < 8; j++)
			DUMPREG(i, DISPC_OVL_FIR_COEF_H, j);

		for (j = 0; j < 8; j++)
			DUMPREG(i, DISPC_OVL_FIR_COEF_HV, j);

		for (j = 0; j < 5; j++)
			DUMPREG(i, DISPC_OVL_CONV_COEF, j);

		if (dss_has_feature(FEAT_FIR_COEF_V)) {
			for (j = 0; j < 8; j++)
				DUMPREG(i, DISPC_OVL_FIR_COEF_V, j);
		}

		if (dss_has_feature(FEAT_HANDLE_UV_SEPARATE)) {
			for (j = 0; j < 8; j++)
				DUMPREG(i, DISPC_OVL_FIR_COEF_H2, j);

			for (j = 0; j < 8; j++)
				DUMPREG(i, DISPC_OVL_FIR_COEF_HV2, j);

			for (j = 0; j < 8; j++)
				DUMPREG(i, DISPC_OVL_FIR_COEF_V2, j);
		}
	}

	dispc_runtime_put();

#undef DISPC_REG
#undef DUMPREG
}

/* calculate clock rates using dividers in cinfo */
int dispc_calc_clock_rates(unsigned long dispc_fclk_rate,
		struct dispc_clock_info *cinfo)
{
	if (cinfo->lck_div > 255 || cinfo->lck_div == 0)
		return -EINVAL;
	if (cinfo->pck_div < 1 || cinfo->pck_div > 255)
		return -EINVAL;

	cinfo->lck = dispc_fclk_rate / cinfo->lck_div;
	cinfo->pck = cinfo->lck / cinfo->pck_div;

	return 0;
}

bool dispc_div_calc(unsigned long dispc,
		unsigned long pck_min, unsigned long pck_max,
		dispc_div_calc_func func, void *data)
{
	int lckd, lckd_start, lckd_stop;
	int pckd, pckd_start, pckd_stop;
	unsigned long pck, lck;
	unsigned long lck_max;
	unsigned long pckd_hw_min, pckd_hw_max;
	unsigned min_fck_per_pck;
	unsigned long fck;

#ifdef CONFIG_OMAP2_DSS_MIN_FCK_PER_PCK
	min_fck_per_pck = CONFIG_OMAP2_DSS_MIN_FCK_PER_PCK;
#else
	min_fck_per_pck = 0;
#endif

	pckd_hw_min = dss_feat_get_param_min(FEAT_PARAM_DSS_PCD);
	pckd_hw_max = dss_feat_get_param_max(FEAT_PARAM_DSS_PCD);

	lck_max = dss_feat_get_param_max(FEAT_PARAM_DSS_FCK);

	pck_min = pck_min ? pck_min : 1;
	pck_max = pck_max ? pck_max : ULONG_MAX;

	lckd_start = max(DIV_ROUND_UP(dispc, lck_max), 1ul);
	lckd_stop = min(dispc / pck_min, 255ul);

	for (lckd = lckd_start; lckd <= lckd_stop; ++lckd) {
		lck = dispc / lckd;

		pckd_start = max(DIV_ROUND_UP(lck, pck_max), pckd_hw_min);
		pckd_stop = min(lck / pck_min, pckd_hw_max);

		for (pckd = pckd_start; pckd <= pckd_stop; ++pckd) {
			pck = lck / pckd;

			/*
			 * For OMAP2/3 the DISPC fclk is the same as LCD's logic
			 * clock, which means we're configuring DISPC fclk here
			 * also. Thus we need to use the calculated lck. For
			 * OMAP4+ the DISPC fclk is a separate clock.
			 */
			if (dss_has_feature(FEAT_CORE_CLK_DIV))
				fck = dispc_core_clk_rate();
			else
				fck = lck;

			if (fck < pck * min_fck_per_pck)
				continue;

			if (func(lckd, pckd, lck, pck, data))
				return true;
		}
	}

	return false;
}

void dispc_mgr_set_clock_div(enum omap_channel channel,
		const struct dispc_clock_info *cinfo)
{
	DSSDBG("lck = %lu (%u)\n", cinfo->lck, cinfo->lck_div);
	DSSDBG("pck = %lu (%u)\n", cinfo->pck, cinfo->pck_div);

	dispc_mgr_set_lcd_divisor(channel, cinfo->lck_div, cinfo->pck_div);
}

int dispc_mgr_get_clock_div(enum omap_channel channel,
		struct dispc_clock_info *cinfo)
{
	unsigned long fck;

	fck = dispc_fclk_rate();

	cinfo->lck_div = REG_GET(DISPC_DIVISORo(channel), 23, 16);
	cinfo->pck_div = REG_GET(DISPC_DIVISORo(channel), 7, 0);

	cinfo->lck = fck / cinfo->lck_div;
	cinfo->pck = cinfo->lck / cinfo->pck_div;

	return 0;
}

static u32 dispc_read_irqstatus(void)
{
	return dispc_read_reg(DISPC_IRQSTATUS);
}

static void dispc_clear_irqstatus(u32 mask)
{
	dispc_write_reg(DISPC_IRQSTATUS, mask);
}

static void dispc_write_irqenable(u32 mask)
{
	u32 old_mask = dispc_read_reg(DISPC_IRQENABLE);

	/* clear the irqstatus for newly enabled irqs */
	dispc_clear_irqstatus((mask ^ old_mask) & mask);

	dispc_write_reg(DISPC_IRQENABLE, mask);

	/* flush posted write */
	dispc_read_reg(DISPC_IRQENABLE);
}

void dispc_enable_sidle(void)
{
	REG_FLD_MOD(DISPC_SYSCONFIG, 2, 4, 3);	/* SIDLEMODE: smart idle */
}

void dispc_disable_sidle(void)
{
	REG_FLD_MOD(DISPC_SYSCONFIG, 1, 4, 3);	/* SIDLEMODE: no idle */
}

static u32 dispc_mgr_gamma_size(enum omap_channel channel)
{
	const struct dispc_gamma_desc *gdesc = &mgr_desc[channel].gamma;

	if (!dispc.feat->has_gamma_table)
		return 0;

	return gdesc->len;
}

static void dispc_mgr_write_gamma_table(enum omap_channel channel)
{
	const struct dispc_gamma_desc *gdesc = &mgr_desc[channel].gamma;
	u32 *table = dispc.gamma_table[channel];
	unsigned int i;

	DSSDBG("%s: channel %d\n", __func__, channel);

	for (i = 0; i < gdesc->len; ++i) {
		u32 v = table[i];

		if (gdesc->has_index)
			v |= i << 24;
		else if (i == 0)
			v |= 1 << 31;

		dispc_write_reg(gdesc->reg, v);
	}
}

static void dispc_restore_gamma_tables(void)
{
	DSSDBG("%s()\n", __func__);

	if (!dispc.feat->has_gamma_table)
		return;

	dispc_mgr_write_gamma_table(OMAP_DSS_CHANNEL_LCD);

	dispc_mgr_write_gamma_table(OMAP_DSS_CHANNEL_DIGIT);

	if (dss_has_feature(FEAT_MGR_LCD2))
		dispc_mgr_write_gamma_table(OMAP_DSS_CHANNEL_LCD2);

	if (dss_has_feature(FEAT_MGR_LCD3))
		dispc_mgr_write_gamma_table(OMAP_DSS_CHANNEL_LCD3);
}

static const struct drm_color_lut dispc_mgr_gamma_default_lut[] = {
	{ .red = 0, .green = 0, .blue = 0, },
	{ .red = U16_MAX, .green = U16_MAX, .blue = U16_MAX, },
};

static void dispc_mgr_set_gamma(enum omap_channel channel,
			 const struct drm_color_lut *lut,
			 unsigned int length)
{
	const struct dispc_gamma_desc *gdesc = &mgr_desc[channel].gamma;
	u32 *table = dispc.gamma_table[channel];
	uint i;

	DSSDBG("%s: channel %d, lut len %u, hw len %u\n", __func__,
	       channel, length, gdesc->len);

	if (!dispc.feat->has_gamma_table)
		return;

	if (lut == NULL || length < 2) {
		lut = dispc_mgr_gamma_default_lut;
		length = ARRAY_SIZE(dispc_mgr_gamma_default_lut);
	}

	for (i = 0; i < length - 1; ++i) {
		uint first = i * (gdesc->len - 1) / (length - 1);
		uint last = (i + 1) * (gdesc->len - 1) / (length - 1);
		uint w = last - first;
		u16 r, g, b;
		uint j;

		if (w == 0)
			continue;

		for (j = 0; j <= w; j++) {
			r = (lut[i].red * (w - j) + lut[i+1].red * j) / w;
			g = (lut[i].green * (w - j) + lut[i+1].green * j) / w;
			b = (lut[i].blue * (w - j) + lut[i+1].blue * j) / w;

			r >>= 16 - gdesc->bits;
			g >>= 16 - gdesc->bits;
			b >>= 16 - gdesc->bits;

			table[first + j] = (r << (gdesc->bits * 2)) |
				(g << gdesc->bits) | b;
		}
	}

	if (dispc.is_enabled)
		dispc_mgr_write_gamma_table(channel);
}

static int dispc_init_gamma_tables(void)
{
	int channel;

	if (!dispc.feat->has_gamma_table)
		return 0;

	for (channel = 0; channel < ARRAY_SIZE(dispc.gamma_table); channel++) {
		const struct dispc_gamma_desc *gdesc = &mgr_desc[channel].gamma;
		u32 *gt;

		if (channel == OMAP_DSS_CHANNEL_LCD2 &&
		    !dss_has_feature(FEAT_MGR_LCD2))
			continue;

		if (channel == OMAP_DSS_CHANNEL_LCD3 &&
		    !dss_has_feature(FEAT_MGR_LCD3))
			continue;

		gt = devm_kmalloc_array(&dispc.pdev->dev, gdesc->len,
					   sizeof(u32), GFP_KERNEL);
		if (!gt)
			return -ENOMEM;

		dispc.gamma_table[channel] = gt;

		dispc_mgr_set_gamma(channel, NULL, 0);
	}
	return 0;
}

static void _omap_dispc_initial_config(void)
{
	u32 l;

	/* Exclusively enable DISPC_CORE_CLK and set divider to 1 */
	if (dss_has_feature(FEAT_CORE_CLK_DIV)) {
		l = dispc_read_reg(DISPC_DIVISOR);
		/* Use DISPC_DIVISOR.LCD, instead of DISPC_DIVISOR1.LCD */
		l = FLD_MOD(l, 1, 0, 0);
		l = FLD_MOD(l, 1, 23, 16);
		dispc_write_reg(DISPC_DIVISOR, l);

		dispc.core_clk_rate = dispc_fclk_rate();
	}

	/* Use gamma table mode, instead of palette mode */
	if (dispc.feat->has_gamma_table)
		REG_FLD_MOD(DISPC_CONFIG, 1, 3, 3);

	/* For older DSS versions (FEAT_FUNCGATED) this enables
	 * func-clock auto-gating. For newer versions
	 * (dispc.feat->has_gamma_table) this enables tv-out gamma tables.
	 */
	if (dss_has_feature(FEAT_FUNCGATED) || dispc.feat->has_gamma_table)
		REG_FLD_MOD(DISPC_CONFIG, 1, 9, 9);

	dispc_setup_color_conv_coef();

	dispc_set_loadmode(OMAP_DSS_LOAD_FRAME_ONLY);

	dispc_init_fifos();

	dispc_configure_burst_sizes();

	dispc_ovl_enable_zorder_planes();

	if (dispc.feat->mstandby_workaround)
		REG_FLD_MOD(DISPC_MSTANDBY_CTRL, 1, 0, 0);

	if (dss_has_feature(FEAT_MFLAG))
		dispc_init_mflag();
}

static const struct dispc_features omap24xx_dispc_feats = {
	.sw_start		=	5,
	.fp_start		=	15,
	.bp_start		=	27,
	.sw_max			=	64,
	.vp_max			=	255,
	.hp_max			=	256,
	.mgr_width_start	=	10,
	.mgr_height_start	=	26,
	.mgr_width_max		=	2048,
	.mgr_height_max		=	2048,
	.max_lcd_pclk		=	66500000,
	.calc_scaling		=	dispc_ovl_calc_scaling_24xx,
	.calc_core_clk		=	calc_core_clk_24xx,
	.num_fifos		=	3,
	.no_framedone_tv	=	true,
	.set_max_preload	=	false,
	.last_pixel_inc_missing	=	true,
};

static const struct dispc_features omap34xx_rev1_0_dispc_feats = {
	.sw_start		=	5,
	.fp_start		=	15,
	.bp_start		=	27,
	.sw_max			=	64,
	.vp_max			=	255,
	.hp_max			=	256,
	.mgr_width_start	=	10,
	.mgr_height_start	=	26,
	.mgr_width_max		=	2048,
	.mgr_height_max		=	2048,
	.max_lcd_pclk		=	173000000,
	.max_tv_pclk		=	59000000,
	.calc_scaling		=	dispc_ovl_calc_scaling_34xx,
	.calc_core_clk		=	calc_core_clk_34xx,
	.num_fifos		=	3,
	.no_framedone_tv	=	true,
	.set_max_preload	=	false,
	.last_pixel_inc_missing	=	true,
};

static const struct dispc_features omap34xx_rev3_0_dispc_feats = {
	.sw_start		=	7,
	.fp_start		=	19,
	.bp_start		=	31,
	.sw_max			=	256,
	.vp_max			=	4095,
	.hp_max			=	4096,
	.mgr_width_start	=	10,
	.mgr_height_start	=	26,
	.mgr_width_max		=	2048,
	.mgr_height_max		=	2048,
	.max_lcd_pclk		=	173000000,
	.max_tv_pclk		=	59000000,
	.calc_scaling		=	dispc_ovl_calc_scaling_34xx,
	.calc_core_clk		=	calc_core_clk_34xx,
	.num_fifos		=	3,
	.no_framedone_tv	=	true,
	.set_max_preload	=	false,
	.last_pixel_inc_missing	=	true,
};

static const struct dispc_features omap44xx_dispc_feats = {
	.sw_start		=	7,
	.fp_start		=	19,
	.bp_start		=	31,
	.sw_max			=	256,
	.vp_max			=	4095,
	.hp_max			=	4096,
	.mgr_width_start	=	10,
	.mgr_height_start	=	26,
	.mgr_width_max		=	2048,
	.mgr_height_max		=	2048,
	.max_lcd_pclk		=	170000000,
	.max_tv_pclk		=	185625000,
	.calc_scaling		=	dispc_ovl_calc_scaling_44xx,
	.calc_core_clk		=	calc_core_clk_44xx,
	.num_fifos		=	5,
	.gfx_fifo_workaround	=	true,
	.set_max_preload	=	true,
	.supports_sync_align	=	true,
	.has_writeback		=	true,
	.supports_double_pixel	=	true,
	.reverse_ilace_field_order =	true,
	.has_gamma_table	=	true,
	.has_gamma_i734_bug	=	true,
};

static const struct dispc_features omap54xx_dispc_feats = {
	.sw_start		=	7,
	.fp_start		=	19,
	.bp_start		=	31,
	.sw_max			=	256,
	.vp_max			=	4095,
	.hp_max			=	4096,
	.mgr_width_start	=	11,
	.mgr_height_start	=	27,
	.mgr_width_max		=	4096,
	.mgr_height_max		=	4096,
	.max_lcd_pclk		=	170000000,
	.max_tv_pclk		=	186000000,
	.calc_scaling		=	dispc_ovl_calc_scaling_44xx,
	.calc_core_clk		=	calc_core_clk_44xx,
	.num_fifos		=	5,
	.gfx_fifo_workaround	=	true,
	.mstandby_workaround	=	true,
	.set_max_preload	=	true,
	.supports_sync_align	=	true,
	.has_writeback		=	true,
	.supports_double_pixel	=	true,
	.reverse_ilace_field_order =	true,
	.has_gamma_table	=	true,
	.has_gamma_i734_bug	=	true,
};

static int dispc_init_features(struct platform_device *pdev)
{
	const struct dispc_features *src;
	struct dispc_features *dst;

	dst = devm_kzalloc(&pdev->dev, sizeof(*dst), GFP_KERNEL);
	if (!dst) {
		dev_err(&pdev->dev, "Failed to allocate DISPC Features\n");
		return -ENOMEM;
	}

	switch (omapdss_get_version()) {
	case OMAPDSS_VER_OMAP24xx:
		src = &omap24xx_dispc_feats;
		break;

	case OMAPDSS_VER_OMAP34xx_ES1:
		src = &omap34xx_rev1_0_dispc_feats;
		break;

	case OMAPDSS_VER_OMAP34xx_ES3:
	case OMAPDSS_VER_OMAP3630:
	case OMAPDSS_VER_AM35xx:
	case OMAPDSS_VER_AM43xx:
		src = &omap34xx_rev3_0_dispc_feats;
		break;

	case OMAPDSS_VER_OMAP4430_ES1:
	case OMAPDSS_VER_OMAP4430_ES2:
	case OMAPDSS_VER_OMAP4:
		src = &omap44xx_dispc_feats;
		break;

	case OMAPDSS_VER_OMAP5:
	case OMAPDSS_VER_DRA7xx:
		src = &omap54xx_dispc_feats;
		break;

	default:
		return -ENODEV;
	}

	memcpy(dst, src, sizeof(*dst));
	dispc.feat = dst;

	return 0;
}

static irqreturn_t dispc_irq_handler(int irq, void *arg)
{
	if (!dispc.is_enabled)
		return IRQ_NONE;

	return dispc.user_handler(irq, dispc.user_data);
}

static int dispc_request_irq(irq_handler_t handler, void *dev_id)
{
	int r;

	if (dispc.user_handler != NULL)
		return -EBUSY;

	dispc.user_handler = handler;
	dispc.user_data = dev_id;

	/* ensure the dispc_irq_handler sees the values above */
	smp_wmb();

	r = devm_request_irq(&dispc.pdev->dev, dispc.irq, dispc_irq_handler,
			     IRQF_SHARED, "OMAP DISPC", &dispc);
	if (r) {
		dispc.user_handler = NULL;
		dispc.user_data = NULL;
	}

	return r;
}

static void dispc_free_irq(void *dev_id)
{
	devm_free_irq(&dispc.pdev->dev, dispc.irq, &dispc);

	dispc.user_handler = NULL;
	dispc.user_data = NULL;
}

/*
 * Workaround for errata i734 in DSS dispc
 *  - LCD1 Gamma Correction Is Not Working When GFX Pipe Is Disabled
 *
 * For gamma tables to work on LCD1 the GFX plane has to be used at
 * least once after DSS HW has come out of reset. The workaround
 * sets up a minimal LCD setup with GFX plane and waits for one
 * vertical sync irq before disabling the setup and continuing with
 * the context restore. The physical outputs are gated during the
 * operation. This workaround requires that gamma table's LOADMODE
 * is set to 0x2 in DISPC_CONTROL1 register.
 *
 * For details see:
 * OMAP543x Multimedia Device Silicon Revision 2.0 Silicon Errata
 * Literature Number: SWPZ037E
 * Or some other relevant errata document for the DSS IP version.
 */

static const struct dispc_errata_i734_data {
	struct videomode vm;
	struct omap_overlay_info ovli;
	struct omap_overlay_manager_info mgri;
	struct dss_lcd_mgr_config lcd_conf;
} i734 = {
	.vm = {
		.hactive = 8, .vactive = 1,
		.pixelclock = 16000000,
		.hsync_len = 8, .hfront_porch = 4, .hback_porch = 4,
		.vsync_len = 1, .vfront_porch = 1, .vback_porch = 1,

		.flags = DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW |
			 DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_SYNC_POSEDGE |
			 DISPLAY_FLAGS_PIXDATA_POSEDGE,
	},
	.ovli = {
		.screen_width = 1,
		.width = 1, .height = 1,
		.fourcc = DRM_FORMAT_XRGB8888,
		.rotation = DRM_MODE_ROTATE_0,
		.rotation_type = OMAP_DSS_ROT_NONE,
		.pos_x = 0, .pos_y = 0,
		.out_width = 0, .out_height = 0,
		.global_alpha = 0xff,
		.pre_mult_alpha = 0,
		.zorder = 0,
	},
	.mgri = {
		.default_color = 0,
		.trans_enabled = false,
		.partial_alpha_enabled = false,
		.cpr_enable = false,
	},
	.lcd_conf = {
		.io_pad_mode = DSS_IO_PAD_MODE_BYPASS,
		.stallmode = false,
		.fifohandcheck = false,
		.clock_info = {
			.lck_div = 1,
			.pck_div = 2,
		},
		.video_port_width = 24,
		.lcden_sig_polarity = 0,
	},
};

static struct i734_buf {
	size_t size;
	dma_addr_t paddr;
	void *vaddr;
} i734_buf;

static int dispc_errata_i734_wa_init(void)
{
	if (!dispc.feat->has_gamma_i734_bug)
		return 0;

	i734_buf.size = i734.ovli.width * i734.ovli.height *
		color_mode_to_bpp(i734.ovli.fourcc) / 8;

	i734_buf.vaddr = dma_alloc_writecombine(&dispc.pdev->dev, i734_buf.size,
						&i734_buf.paddr, GFP_KERNEL);
	if (!i734_buf.vaddr) {
		dev_err(&dispc.pdev->dev, "%s: dma_alloc_writecombine failed",
			__func__);
		return -ENOMEM;
	}

	return 0;
}

static void dispc_errata_i734_wa_fini(void)
{
	if (!dispc.feat->has_gamma_i734_bug)
		return;

	dma_free_writecombine(&dispc.pdev->dev, i734_buf.size, i734_buf.vaddr,
			      i734_buf.paddr);
}

static void dispc_errata_i734_wa(void)
{
	u32 framedone_irq = dispc_mgr_get_framedone_irq(OMAP_DSS_CHANNEL_LCD);
	struct omap_overlay_info ovli;
	struct dss_lcd_mgr_config lcd_conf;
	u32 gatestate;
	unsigned int count;

	if (!dispc.feat->has_gamma_i734_bug)
		return;

	gatestate = REG_GET(DISPC_CONFIG, 8, 4);

	ovli = i734.ovli;
	ovli.paddr = i734_buf.paddr;
	lcd_conf = i734.lcd_conf;

	/* Gate all LCD1 outputs */
	REG_FLD_MOD(DISPC_CONFIG, 0x1f, 8, 4);

	/* Setup and enable GFX plane */
	dispc_ovl_setup(OMAP_DSS_GFX, &ovli, &i734.vm, false,
		OMAP_DSS_CHANNEL_LCD);
	dispc_ovl_enable(OMAP_DSS_GFX, true);

	/* Set up and enable display manager for LCD1 */
	dispc_mgr_setup(OMAP_DSS_CHANNEL_LCD, &i734.mgri);
	dispc_calc_clock_rates(dss_get_dispc_clk_rate(),
			       &lcd_conf.clock_info);
	dispc_mgr_set_lcd_config(OMAP_DSS_CHANNEL_LCD, &lcd_conf);
	dispc_mgr_set_timings(OMAP_DSS_CHANNEL_LCD, &i734.vm);

	dispc_clear_irqstatus(framedone_irq);

	/* Enable and shut the channel to produce just one frame */
	dispc_mgr_enable(OMAP_DSS_CHANNEL_LCD, true);
	dispc_mgr_enable(OMAP_DSS_CHANNEL_LCD, false);

	/* Busy wait for framedone. We can't fiddle with irq handlers
	 * in PM resume. Typically the loop runs less than 5 times and
	 * waits less than a micro second.
	 */
	count = 0;
	while (!(dispc_read_irqstatus() & framedone_irq)) {
		if (count++ > 10000) {
			dev_err(&dispc.pdev->dev, "%s: framedone timeout\n",
				__func__);
			break;
		}
	}
	dispc_ovl_enable(OMAP_DSS_GFX, false);

	/* Clear all irq bits before continuing */
	dispc_clear_irqstatus(0xffffffff);

	/* Restore the original state to LCD1 output gates */
	REG_FLD_MOD(DISPC_CONFIG, gatestate, 8, 4);
}

static const struct dispc_ops dispc_ops = {
	.read_irqstatus = dispc_read_irqstatus,
	.clear_irqstatus = dispc_clear_irqstatus,
	.write_irqenable = dispc_write_irqenable,

	.request_irq = dispc_request_irq,
	.free_irq = dispc_free_irq,

	.runtime_get = dispc_runtime_get,
	.runtime_put = dispc_runtime_put,

	.get_num_ovls = dispc_get_num_ovls,
	.get_num_mgrs = dispc_get_num_mgrs,

	.mgr_enable = dispc_mgr_enable,
	.mgr_is_enabled = dispc_mgr_is_enabled,
	.mgr_get_vsync_irq = dispc_mgr_get_vsync_irq,
	.mgr_get_framedone_irq = dispc_mgr_get_framedone_irq,
	.mgr_get_sync_lost_irq = dispc_mgr_get_sync_lost_irq,
	.mgr_go_busy = dispc_mgr_go_busy,
	.mgr_go = dispc_mgr_go,
	.mgr_set_lcd_config = dispc_mgr_set_lcd_config,
	.mgr_set_timings = dispc_mgr_set_timings,
	.mgr_setup = dispc_mgr_setup,
	.mgr_get_supported_outputs = dispc_mgr_get_supported_outputs,
	.mgr_gamma_size = dispc_mgr_gamma_size,
	.mgr_set_gamma = dispc_mgr_set_gamma,

	.ovl_enable = dispc_ovl_enable,
	.ovl_setup = dispc_ovl_setup,
	.ovl_get_color_modes = dispc_ovl_get_color_modes,
};

/* DISPC HW IP initialisation */
static int dispc_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	u32 rev;
	int r = 0;
	struct resource *dispc_mem;
	struct device_node *np = pdev->dev.of_node;

	dispc.pdev = pdev;

	spin_lock_init(&dispc.control_lock);

	r = dispc_init_features(dispc.pdev);
	if (r)
		return r;

	r = dispc_errata_i734_wa_init();
	if (r)
		return r;

	dispc_mem = platform_get_resource(dispc.pdev, IORESOURCE_MEM, 0);
	dispc.base = devm_ioremap_resource(&pdev->dev, dispc_mem);
	if (IS_ERR(dispc.base))
		return PTR_ERR(dispc.base);

	dispc.irq = platform_get_irq(dispc.pdev, 0);
	if (dispc.irq < 0) {
		DSSERR("platform_get_irq failed\n");
		return -ENODEV;
	}

	if (np && of_property_read_bool(np, "syscon-pol")) {
		dispc.syscon_pol = syscon_regmap_lookup_by_phandle(np, "syscon-pol");
		if (IS_ERR(dispc.syscon_pol)) {
			dev_err(&pdev->dev, "failed to get syscon-pol regmap\n");
			return PTR_ERR(dispc.syscon_pol);
		}

		if (of_property_read_u32_index(np, "syscon-pol", 1,
				&dispc.syscon_pol_offset)) {
			dev_err(&pdev->dev, "failed to get syscon-pol offset\n");
			return -EINVAL;
		}
	}

	r = dispc_init_gamma_tables();
	if (r)
		return r;

	pm_runtime_enable(&pdev->dev);

	r = dispc_runtime_get();
	if (r)
		goto err_runtime_get;

	_omap_dispc_initial_config();

	rev = dispc_read_reg(DISPC_REVISION);
	dev_dbg(&pdev->dev, "OMAP DISPC rev %d.%d\n",
	       FLD_GET(rev, 7, 4), FLD_GET(rev, 3, 0));

	dispc_runtime_put();

	dispc_set_ops(&dispc_ops);

	dss_debugfs_create_file("dispc", dispc_dump_regs);

	return 0;

err_runtime_get:
	pm_runtime_disable(&pdev->dev);
	return r;
}

static void dispc_unbind(struct device *dev, struct device *master,
			       void *data)
{
	dispc_set_ops(NULL);

	pm_runtime_disable(dev);

	dispc_errata_i734_wa_fini();
}

static const struct component_ops dispc_component_ops = {
	.bind	= dispc_bind,
	.unbind	= dispc_unbind,
};

static int dispc_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &dispc_component_ops);
}

static int dispc_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dispc_component_ops);
	return 0;
}

static int dispc_runtime_suspend(struct device *dev)
{
	dispc.is_enabled = false;
	/* ensure the dispc_irq_handler sees the is_enabled value */
	smp_wmb();
	/* wait for current handler to finish before turning the DISPC off */
	synchronize_irq(dispc.irq);

	dispc_save_context();

	return 0;
}

static int dispc_runtime_resume(struct device *dev)
{
	/*
	 * The reset value for load mode is 0 (OMAP_DSS_LOAD_CLUT_AND_FRAME)
	 * but we always initialize it to 2 (OMAP_DSS_LOAD_FRAME_ONLY) in
	 * _omap_dispc_initial_config(). We can thus use it to detect if
	 * we have lost register context.
	 */
	if (REG_GET(DISPC_CONFIG, 2, 1) != OMAP_DSS_LOAD_FRAME_ONLY) {
		_omap_dispc_initial_config();

		dispc_errata_i734_wa();

		dispc_restore_context();

		dispc_restore_gamma_tables();
	}

	dispc.is_enabled = true;
	/* ensure the dispc_irq_handler sees the is_enabled value */
	smp_wmb();

	return 0;
}

static const struct dev_pm_ops dispc_pm_ops = {
	.runtime_suspend = dispc_runtime_suspend,
	.runtime_resume = dispc_runtime_resume,
};

static const struct of_device_id dispc_of_match[] = {
	{ .compatible = "ti,omap2-dispc", },
	{ .compatible = "ti,omap3-dispc", },
	{ .compatible = "ti,omap4-dispc", },
	{ .compatible = "ti,omap5-dispc", },
	{ .compatible = "ti,dra7-dispc", },
	{},
};

static struct platform_driver omap_dispchw_driver = {
	.probe		= dispc_probe,
	.remove         = dispc_remove,
	.driver         = {
		.name   = "omapdss_dispc",
		.pm	= &dispc_pm_ops,
		.of_match_table = dispc_of_match,
		.suppress_bind_attrs = true,
	},
};

int __init dispc_init_platform_driver(void)
{
	return platform_driver_register(&omap_dispchw_driver);
}

void dispc_uninit_platform_driver(void)
{
	platform_driver_unregister(&omap_dispchw_driver);
}
