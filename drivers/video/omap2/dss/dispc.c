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
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sizes.h>

#include <video/omapdss.h>

#include "dss.h"
#include "dss_features.h"
#include "dispc.h"

/* DISPC */
#define DISPC_SZ_REGS			SZ_4K

#define DISPC_IRQ_MASK_ERROR            (DISPC_IRQ_GFX_FIFO_UNDERFLOW | \
					 DISPC_IRQ_OCP_ERR | \
					 DISPC_IRQ_VID1_FIFO_UNDERFLOW | \
					 DISPC_IRQ_VID2_FIFO_UNDERFLOW | \
					 DISPC_IRQ_SYNC_LOST | \
					 DISPC_IRQ_SYNC_LOST_DIGIT)

#define DISPC_MAX_NR_ISRS		8

struct omap_dispc_isr_data {
	omap_dispc_isr_t	isr;
	void			*arg;
	u32			mask;
};

enum omap_burst_size {
	BURST_SIZE_X2 = 0,
	BURST_SIZE_X4 = 1,
	BURST_SIZE_X8 = 2,
};

#define REG_GET(idx, start, end) \
	FLD_GET(dispc_read_reg(idx), start, end)

#define REG_FLD_MOD(idx, val, start, end)				\
	dispc_write_reg(idx, FLD_MOD(dispc_read_reg(idx), val, start, end))

struct dispc_irq_stats {
	unsigned long last_reset;
	unsigned irq_count;
	unsigned irqs[32];
};

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
	int (*calc_scaling) (enum omap_plane plane,
		const struct omap_video_timings *mgr_timings,
		u16 width, u16 height, u16 out_width, u16 out_height,
		enum omap_color_mode color_mode, bool *five_taps,
		int *x_predecim, int *y_predecim, int *decim_x, int *decim_y,
		u16 pos_x, unsigned long *core_clk, bool mem_to_mem);
	unsigned long (*calc_core_clk) (enum omap_plane plane,
		u16 width, u16 height, u16 out_width, u16 out_height,
		bool mem_to_mem);
	u8 num_fifos;

	/* swap GFX & WB fifos */
	bool gfx_fifo_workaround:1;
};

#define DISPC_MAX_NR_FIFOS 5

static struct {
	struct platform_device *pdev;
	void __iomem    *base;

	int		ctx_loss_cnt;

	int irq;
	struct clk *dss_clk;

	u32 fifo_size[DISPC_MAX_NR_FIFOS];
	/* maps which plane is using a fifo. fifo-id -> plane-id */
	int fifo_assignment[DISPC_MAX_NR_FIFOS];

	spinlock_t irq_lock;
	u32 irq_error_mask;
	struct omap_dispc_isr_data registered_isr[DISPC_MAX_NR_ISRS];
	u32 error_irqs;
	struct work_struct error_work;

	bool		ctx_valid;
	u32		ctx[DISPC_SZ_REGS / sizeof(u32)];

	const struct dispc_features *feat;

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
	spinlock_t irq_stats_lock;
	struct dispc_irq_stats irq_stats;
#endif
} dispc;

enum omap_color_component {
	/* used for all color formats for OMAP3 and earlier
	 * and for RGB and Y color component on OMAP4
	 */
	DISPC_COLOR_COMPONENT_RGB_Y		= 1 << 0,
	/* used for UV component for
	 * OMAP_DSS_COLOR_YUV2, OMAP_DSS_COLOR_UYVY, OMAP_DSS_COLOR_NV12
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

static const struct {
	const char *name;
	u32 vsync_irq;
	u32 framedone_irq;
	u32 sync_lost_irq;
	struct reg_field reg_desc[DISPC_MGR_FLD_NUM];
} mgr_desc[] = {
	[OMAP_DSS_CHANNEL_LCD] = {
		.name		= "LCD",
		.vsync_irq	= DISPC_IRQ_VSYNC,
		.framedone_irq	= DISPC_IRQ_FRAMEDONE,
		.sync_lost_irq	= DISPC_IRQ_SYNC_LOST,
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
		.framedone_irq	= 0,
		.sync_lost_irq	= DISPC_IRQ_SYNC_LOST_DIGIT,
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

static void _omap_dispc_set_irqs(void);
static unsigned long dispc_plane_pclk_rate(enum omap_plane plane);
static unsigned long dispc_plane_lclk_rate(enum omap_plane plane);

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
	const struct reg_field rfld = mgr_desc[channel].reg_desc[regfld];
	return REG_GET(rfld.reg, rfld.high, rfld.low);
}

static void mgr_fld_write(enum omap_channel channel,
					enum mgr_reg_fields regfld, int val) {
	const struct reg_field rfld = mgr_desc[channel].reg_desc[regfld];
	REG_FLD_MOD(rfld.reg, val, rfld.high, rfld.low);
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

	dispc.ctx_loss_cnt = dss_get_ctx_loss_count(&dispc.pdev->dev);
	dispc.ctx_valid = true;

	DSSDBG("context saved, ctx_loss_count %d\n", dispc.ctx_loss_cnt);
}

static void dispc_restore_context(void)
{
	int i, j, ctx;

	DSSDBG("dispc_restore_context\n");

	if (!dispc.ctx_valid)
		return;

	ctx = dss_get_ctx_loss_count(&dispc.pdev->dev);

	if (ctx >= 0 && ctx == dispc.ctx_loss_cnt)
		return;

	DSSDBG("ctx_loss_count: saved %d, current %d\n",
			dispc.ctx_loss_cnt, ctx);

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

u32 dispc_mgr_get_vsync_irq(enum omap_channel channel)
{
	return mgr_desc[channel].vsync_irq;
}

u32 dispc_mgr_get_framedone_irq(enum omap_channel channel)
{
	return mgr_desc[channel].framedone_irq;
}

u32 dispc_mgr_get_sync_lost_irq(enum omap_channel channel)
{
	return mgr_desc[channel].sync_lost_irq;
}

u32 dispc_wb_get_framedone_irq(void)
{
	return DISPC_IRQ_FRAMEDONEWB;
}

bool dispc_mgr_go_busy(enum omap_channel channel)
{
	return mgr_fld_read(channel, DISPC_MGR_FLD_GO) == 1;
}

void dispc_mgr_go(enum omap_channel channel)
{
	bool enable_bit, go_bit;

	/* if the channel is not enabled, we don't need GO */
	enable_bit = mgr_fld_read(channel, DISPC_MGR_FLD_ENABLE) == 1;

	if (!enable_bit)
		return;

	go_bit = mgr_fld_read(channel, DISPC_MGR_FLD_GO) == 1;

	if (go_bit) {
		DSSERR("GO bit not down for channel %d\n", channel);
		return;
	}

	DSSDBG("GO %s\n", mgr_desc[channel].name);

	mgr_fld_write(channel, DISPC_MGR_FLD_GO, 1);
}

bool dispc_wb_go_busy(void)
{
	return REG_GET(DISPC_CONTROL2, 6, 6) == 1;
}

void dispc_wb_go(void)
{
	enum omap_plane plane = OMAP_DSS_WB;
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

static void dispc_ovl_write_firh_reg(enum omap_plane plane, int reg, u32 value)
{
	dispc_write_reg(DISPC_OVL_FIR_COEF_H(plane, reg), value);
}

static void dispc_ovl_write_firhv_reg(enum omap_plane plane, int reg, u32 value)
{
	dispc_write_reg(DISPC_OVL_FIR_COEF_HV(plane, reg), value);
}

static void dispc_ovl_write_firv_reg(enum omap_plane plane, int reg, u32 value)
{
	dispc_write_reg(DISPC_OVL_FIR_COEF_V(plane, reg), value);
}

static void dispc_ovl_write_firh2_reg(enum omap_plane plane, int reg, u32 value)
{
	BUG_ON(plane == OMAP_DSS_GFX);

	dispc_write_reg(DISPC_OVL_FIR_COEF_H2(plane, reg), value);
}

static void dispc_ovl_write_firhv2_reg(enum omap_plane plane, int reg,
		u32 value)
{
	BUG_ON(plane == OMAP_DSS_GFX);

	dispc_write_reg(DISPC_OVL_FIR_COEF_HV2(plane, reg), value);
}

static void dispc_ovl_write_firv2_reg(enum omap_plane plane, int reg, u32 value)
{
	BUG_ON(plane == OMAP_DSS_GFX);

	dispc_write_reg(DISPC_OVL_FIR_COEF_V2(plane, reg), value);
}

static void dispc_ovl_set_scale_coef(enum omap_plane plane, int fir_hinc,
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


static void dispc_ovl_write_color_conv_coef(enum omap_plane plane,
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
	int num_wb = dss_feat_get_num_wbs();
	const struct color_conv_coef ctbl_bt601_5_ovl = {
		298, 409, 0, 298, -208, -100, 298, 0, 517, 0,
	};
	const struct color_conv_coef ctbl_bt601_5_wb = {
		66, 112, -38, 129, -94, -74, 25, -18, 112, 0,
	};

	for (i = 1; i < num_ovl; i++)
		dispc_ovl_write_color_conv_coef(i, &ctbl_bt601_5_ovl);

	for (; i < num_wb; i++)
		dispc_ovl_write_color_conv_coef(i, &ctbl_bt601_5_wb);
}

static void dispc_ovl_set_ba0(enum omap_plane plane, u32 paddr)
{
	dispc_write_reg(DISPC_OVL_BA0(plane), paddr);
}

static void dispc_ovl_set_ba1(enum omap_plane plane, u32 paddr)
{
	dispc_write_reg(DISPC_OVL_BA1(plane), paddr);
}

static void dispc_ovl_set_ba0_uv(enum omap_plane plane, u32 paddr)
{
	dispc_write_reg(DISPC_OVL_BA0_UV(plane), paddr);
}

static void dispc_ovl_set_ba1_uv(enum omap_plane plane, u32 paddr)
{
	dispc_write_reg(DISPC_OVL_BA1_UV(plane), paddr);
}

static void dispc_ovl_set_pos(enum omap_plane plane,
		enum omap_overlay_caps caps, int x, int y)
{
	u32 val;

	if ((caps & OMAP_DSS_OVL_CAP_POS) == 0)
		return;

	val = FLD_VAL(y, 26, 16) | FLD_VAL(x, 10, 0);

	dispc_write_reg(DISPC_OVL_POSITION(plane), val);
}

static void dispc_ovl_set_input_size(enum omap_plane plane, int width,
		int height)
{
	u32 val = FLD_VAL(height - 1, 26, 16) | FLD_VAL(width - 1, 10, 0);

	if (plane == OMAP_DSS_GFX || plane == OMAP_DSS_WB)
		dispc_write_reg(DISPC_OVL_SIZE(plane), val);
	else
		dispc_write_reg(DISPC_OVL_PICTURE_SIZE(plane), val);
}

static void dispc_ovl_set_output_size(enum omap_plane plane, int width,
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

static void dispc_ovl_set_zorder(enum omap_plane plane,
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

static void dispc_ovl_set_pre_mult_alpha(enum omap_plane plane,
		enum omap_overlay_caps caps, bool enable)
{
	if ((caps & OMAP_DSS_OVL_CAP_PRE_MULT_ALPHA) == 0)
		return;

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), enable ? 1 : 0, 28, 28);
}

static void dispc_ovl_setup_global_alpha(enum omap_plane plane,
		enum omap_overlay_caps caps, u8 global_alpha)
{
	static const unsigned shifts[] = { 0, 8, 16, 24, };
	int shift;

	if ((caps & OMAP_DSS_OVL_CAP_GLOBAL_ALPHA) == 0)
		return;

	shift = shifts[plane];
	REG_FLD_MOD(DISPC_GLOBAL_ALPHA, global_alpha, shift + 7, shift);
}

static void dispc_ovl_set_pix_inc(enum omap_plane plane, s32 inc)
{
	dispc_write_reg(DISPC_OVL_PIXEL_INC(plane), inc);
}

static void dispc_ovl_set_row_inc(enum omap_plane plane, s32 inc)
{
	dispc_write_reg(DISPC_OVL_ROW_INC(plane), inc);
}

static void dispc_ovl_set_color_mode(enum omap_plane plane,
		enum omap_color_mode color_mode)
{
	u32 m = 0;
	if (plane != OMAP_DSS_GFX) {
		switch (color_mode) {
		case OMAP_DSS_COLOR_NV12:
			m = 0x0; break;
		case OMAP_DSS_COLOR_RGBX16:
			m = 0x1; break;
		case OMAP_DSS_COLOR_RGBA16:
			m = 0x2; break;
		case OMAP_DSS_COLOR_RGB12U:
			m = 0x4; break;
		case OMAP_DSS_COLOR_ARGB16:
			m = 0x5; break;
		case OMAP_DSS_COLOR_RGB16:
			m = 0x6; break;
		case OMAP_DSS_COLOR_ARGB16_1555:
			m = 0x7; break;
		case OMAP_DSS_COLOR_RGB24U:
			m = 0x8; break;
		case OMAP_DSS_COLOR_RGB24P:
			m = 0x9; break;
		case OMAP_DSS_COLOR_YUV2:
			m = 0xa; break;
		case OMAP_DSS_COLOR_UYVY:
			m = 0xb; break;
		case OMAP_DSS_COLOR_ARGB32:
			m = 0xc; break;
		case OMAP_DSS_COLOR_RGBA32:
			m = 0xd; break;
		case OMAP_DSS_COLOR_RGBX32:
			m = 0xe; break;
		case OMAP_DSS_COLOR_XRGB16_1555:
			m = 0xf; break;
		default:
			BUG(); return;
		}
	} else {
		switch (color_mode) {
		case OMAP_DSS_COLOR_CLUT1:
			m = 0x0; break;
		case OMAP_DSS_COLOR_CLUT2:
			m = 0x1; break;
		case OMAP_DSS_COLOR_CLUT4:
			m = 0x2; break;
		case OMAP_DSS_COLOR_CLUT8:
			m = 0x3; break;
		case OMAP_DSS_COLOR_RGB12U:
			m = 0x4; break;
		case OMAP_DSS_COLOR_ARGB16:
			m = 0x5; break;
		case OMAP_DSS_COLOR_RGB16:
			m = 0x6; break;
		case OMAP_DSS_COLOR_ARGB16_1555:
			m = 0x7; break;
		case OMAP_DSS_COLOR_RGB24U:
			m = 0x8; break;
		case OMAP_DSS_COLOR_RGB24P:
			m = 0x9; break;
		case OMAP_DSS_COLOR_RGBX16:
			m = 0xa; break;
		case OMAP_DSS_COLOR_RGBA16:
			m = 0xb; break;
		case OMAP_DSS_COLOR_ARGB32:
			m = 0xc; break;
		case OMAP_DSS_COLOR_RGBA32:
			m = 0xd; break;
		case OMAP_DSS_COLOR_RGBX32:
			m = 0xe; break;
		case OMAP_DSS_COLOR_XRGB16_1555:
			m = 0xf; break;
		default:
			BUG(); return;
		}
	}

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), m, 4, 1);
}

static void dispc_ovl_configure_burst_type(enum omap_plane plane,
		enum omap_dss_rotation_type rotation_type)
{
	if (dss_has_feature(FEAT_BURST_2D) == 0)
		return;

	if (rotation_type == OMAP_DSS_ROT_TILER)
		REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), 1, 29, 29);
	else
		REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), 0, 29, 29);
}

void dispc_ovl_set_channel_out(enum omap_plane plane, enum omap_channel channel)
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

static enum omap_channel dispc_ovl_get_channel_out(enum omap_plane plane)
{
	int shift;
	u32 val;
	enum omap_channel channel;

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

	if (dss_has_feature(FEAT_MGR_LCD3)) {
		if (FLD_GET(val, 31, 30) == 0)
			channel = FLD_GET(val, shift, shift);
		else if (FLD_GET(val, 31, 30) == 1)
			channel = OMAP_DSS_CHANNEL_LCD2;
		else
			channel = OMAP_DSS_CHANNEL_LCD3;
	} else if (dss_has_feature(FEAT_MGR_LCD2)) {
		if (FLD_GET(val, 31, 30) == 0)
			channel = FLD_GET(val, shift, shift);
		else
			channel = OMAP_DSS_CHANNEL_LCD2;
	} else {
		channel = FLD_GET(val, shift, shift);
	}

	return channel;
}

void dispc_wb_set_channel_in(enum dss_writeback_channel channel)
{
	enum omap_plane plane = OMAP_DSS_WB;

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), channel, 18, 16);
}

static void dispc_ovl_set_burst_size(enum omap_plane plane,
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
}

static u32 dispc_ovl_get_burst_size(enum omap_plane plane)
{
	unsigned unit = dss_feat_get_burst_size_unit();
	/* burst multiplier is always x8 (see dispc_configure_burst_sizes()) */
	return unit * 8;
}

void dispc_enable_gamma_table(bool enable)
{
	/*
	 * This is partially implemented to support only disabling of
	 * the gamma table.
	 */
	if (enable) {
		DSSWARN("Gamma table enabling for TV not yet supported");
		return;
	}

	REG_FLD_MOD(DISPC_CONFIG, enable, 9, 9);
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

static void dispc_ovl_set_vid_color_conv(enum omap_plane plane, bool enable)
{
	u32 val;

	BUG_ON(plane == OMAP_DSS_GFX);

	val = dispc_read_reg(DISPC_OVL_ATTRIBUTES(plane));
	val = FLD_MOD(val, enable, 9, 9);
	dispc_write_reg(DISPC_OVL_ATTRIBUTES(plane), val);
}

static void dispc_ovl_enable_replication(enum omap_plane plane,
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
}

static u32 dispc_ovl_get_fifo_size(enum omap_plane plane)
{
	int fifo;
	u32 size = 0;

	for (fifo = 0; fifo < dispc.feat->num_fifos; ++fifo) {
		if (dispc.fifo_assignment[fifo] == plane)
			size += dispc.fifo_size[fifo];
	}

	return size;
}

void dispc_ovl_set_fifo_threshold(enum omap_plane plane, u32 low, u32 high)
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

void dispc_ovl_compute_fifo_thresholds(enum omap_plane plane,
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

static void dispc_ovl_set_fir(enum omap_plane plane,
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

static void dispc_ovl_set_vid_accu0(enum omap_plane plane, int haccu, int vaccu)
{
	u32 val;
	u8 hor_start, hor_end, vert_start, vert_end;

	dss_feat_get_reg_field(FEAT_REG_HORIZONTALACCU, &hor_start, &hor_end);
	dss_feat_get_reg_field(FEAT_REG_VERTICALACCU, &vert_start, &vert_end);

	val = FLD_VAL(vaccu, vert_start, vert_end) |
			FLD_VAL(haccu, hor_start, hor_end);

	dispc_write_reg(DISPC_OVL_ACCU0(plane), val);
}

static void dispc_ovl_set_vid_accu1(enum omap_plane plane, int haccu, int vaccu)
{
	u32 val;
	u8 hor_start, hor_end, vert_start, vert_end;

	dss_feat_get_reg_field(FEAT_REG_HORIZONTALACCU, &hor_start, &hor_end);
	dss_feat_get_reg_field(FEAT_REG_VERTICALACCU, &vert_start, &vert_end);

	val = FLD_VAL(vaccu, vert_start, vert_end) |
			FLD_VAL(haccu, hor_start, hor_end);

	dispc_write_reg(DISPC_OVL_ACCU1(plane), val);
}

static void dispc_ovl_set_vid_accu2_0(enum omap_plane plane, int haccu,
		int vaccu)
{
	u32 val;

	val = FLD_VAL(vaccu, 26, 16) | FLD_VAL(haccu, 10, 0);
	dispc_write_reg(DISPC_OVL_ACCU2_0(plane), val);
}

static void dispc_ovl_set_vid_accu2_1(enum omap_plane plane, int haccu,
		int vaccu)
{
	u32 val;

	val = FLD_VAL(vaccu, 26, 16) | FLD_VAL(haccu, 10, 0);
	dispc_write_reg(DISPC_OVL_ACCU2_1(plane), val);
}

static void dispc_ovl_set_scale_param(enum omap_plane plane,
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

static void dispc_ovl_set_accu_uv(enum omap_plane plane,
		u16 orig_width,	u16 orig_height, u16 out_width, u16 out_height,
		bool ilace, enum omap_color_mode color_mode, u8 rotation)
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

	switch (rotation) {
	case OMAP_DSS_ROT_0:
		idx = 0;
		break;
	case OMAP_DSS_ROT_90:
		idx = 1;
		break;
	case OMAP_DSS_ROT_180:
		idx = 2;
		break;
	case OMAP_DSS_ROT_270:
		idx = 3;
		break;
	default:
		BUG();
		return;
	}

	switch (color_mode) {
	case OMAP_DSS_COLOR_NV12:
		if (ilace)
			accu_table = accu_nv12_ilace;
		else
			accu_table = accu_nv12;
		break;
	case OMAP_DSS_COLOR_YUV2:
	case OMAP_DSS_COLOR_UYVY:
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

static void dispc_ovl_set_scaling_common(enum omap_plane plane,
		u16 orig_width, u16 orig_height,
		u16 out_width, u16 out_height,
		bool ilace, bool five_taps,
		bool fieldmode, enum omap_color_mode color_mode,
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

static void dispc_ovl_set_scaling_uv(enum omap_plane plane,
		u16 orig_width, u16 orig_height,
		u16 out_width, u16 out_height,
		bool ilace, bool five_taps,
		bool fieldmode, enum omap_color_mode color_mode,
		u8 rotation)
{
	int scale_x = out_width != orig_width;
	int scale_y = out_height != orig_height;
	bool chroma_upscale = plane != OMAP_DSS_WB ? true : false;

	if (!dss_has_feature(FEAT_HANDLE_UV_SEPARATE))
		return;
	if ((color_mode != OMAP_DSS_COLOR_YUV2 &&
			color_mode != OMAP_DSS_COLOR_UYVY &&
			color_mode != OMAP_DSS_COLOR_NV12)) {
		/* reset chroma resampling for RGB formats  */
		if (plane != OMAP_DSS_WB)
			REG_FLD_MOD(DISPC_OVL_ATTRIBUTES2(plane), 0, 8, 8);
		return;
	}

	dispc_ovl_set_accu_uv(plane, orig_width, orig_height, out_width,
			out_height, ilace, color_mode, rotation);

	switch (color_mode) {
	case OMAP_DSS_COLOR_NV12:
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
	case OMAP_DSS_COLOR_YUV2:
	case OMAP_DSS_COLOR_UYVY:
		/* For YUV422 with 90/270 rotation, we don't upsample chroma */
		if (rotation == OMAP_DSS_ROT_0 ||
				rotation == OMAP_DSS_ROT_180) {
			if (chroma_upscale)
				/* UV is subsampled by 2 horizontally */
				orig_width >>= 1;
			else
				/* UV is downsampled by 2 horizontally */
				orig_width <<= 1;
		}

		/* must use FIR for YUV422 if rotated */
		if (rotation != OMAP_DSS_ROT_0)
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

static void dispc_ovl_set_scaling(enum omap_plane plane,
		u16 orig_width, u16 orig_height,
		u16 out_width, u16 out_height,
		bool ilace, bool five_taps,
		bool fieldmode, enum omap_color_mode color_mode,
		u8 rotation)
{
	BUG_ON(plane == OMAP_DSS_GFX);

	dispc_ovl_set_scaling_common(plane,
			orig_width, orig_height,
			out_width, out_height,
			ilace, five_taps,
			fieldmode, color_mode,
			rotation);

	dispc_ovl_set_scaling_uv(plane,
		orig_width, orig_height,
		out_width, out_height,
		ilace, five_taps,
		fieldmode, color_mode,
		rotation);
}

static void dispc_ovl_set_rotation_attrs(enum omap_plane plane, u8 rotation,
		bool mirroring, enum omap_color_mode color_mode)
{
	bool row_repeat = false;
	int vidrot = 0;

	if (color_mode == OMAP_DSS_COLOR_YUV2 ||
			color_mode == OMAP_DSS_COLOR_UYVY) {

		if (mirroring) {
			switch (rotation) {
			case OMAP_DSS_ROT_0:
				vidrot = 2;
				break;
			case OMAP_DSS_ROT_90:
				vidrot = 1;
				break;
			case OMAP_DSS_ROT_180:
				vidrot = 0;
				break;
			case OMAP_DSS_ROT_270:
				vidrot = 3;
				break;
			}
		} else {
			switch (rotation) {
			case OMAP_DSS_ROT_0:
				vidrot = 0;
				break;
			case OMAP_DSS_ROT_90:
				vidrot = 1;
				break;
			case OMAP_DSS_ROT_180:
				vidrot = 2;
				break;
			case OMAP_DSS_ROT_270:
				vidrot = 3;
				break;
			}
		}

		if (rotation == OMAP_DSS_ROT_90 || rotation == OMAP_DSS_ROT_270)
			row_repeat = true;
		else
			row_repeat = false;
	}

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), vidrot, 13, 12);
	if (dss_has_feature(FEAT_ROWREPEATENABLE))
		REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane),
			row_repeat ? 1 : 0, 18, 18);
}

static int color_mode_to_bpp(enum omap_color_mode color_mode)
{
	switch (color_mode) {
	case OMAP_DSS_COLOR_CLUT1:
		return 1;
	case OMAP_DSS_COLOR_CLUT2:
		return 2;
	case OMAP_DSS_COLOR_CLUT4:
		return 4;
	case OMAP_DSS_COLOR_CLUT8:
	case OMAP_DSS_COLOR_NV12:
		return 8;
	case OMAP_DSS_COLOR_RGB12U:
	case OMAP_DSS_COLOR_RGB16:
	case OMAP_DSS_COLOR_ARGB16:
	case OMAP_DSS_COLOR_YUV2:
	case OMAP_DSS_COLOR_UYVY:
	case OMAP_DSS_COLOR_RGBA16:
	case OMAP_DSS_COLOR_RGBX16:
	case OMAP_DSS_COLOR_ARGB16_1555:
	case OMAP_DSS_COLOR_XRGB16_1555:
		return 16;
	case OMAP_DSS_COLOR_RGB24P:
		return 24;
	case OMAP_DSS_COLOR_RGB24U:
	case OMAP_DSS_COLOR_ARGB32:
	case OMAP_DSS_COLOR_RGBA32:
	case OMAP_DSS_COLOR_RGBX32:
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

static void calc_vrfb_rotation_offset(u8 rotation, bool mirror,
		u16 screen_width,
		u16 width, u16 height,
		enum omap_color_mode color_mode, bool fieldmode,
		unsigned int field_offset,
		unsigned *offset0, unsigned *offset1,
		s32 *row_inc, s32 *pix_inc, int x_predecim, int y_predecim)
{
	u8 ps;

	/* FIXME CLUT formats */
	switch (color_mode) {
	case OMAP_DSS_COLOR_CLUT1:
	case OMAP_DSS_COLOR_CLUT2:
	case OMAP_DSS_COLOR_CLUT4:
	case OMAP_DSS_COLOR_CLUT8:
		BUG();
		return;
	case OMAP_DSS_COLOR_YUV2:
	case OMAP_DSS_COLOR_UYVY:
		ps = 4;
		break;
	default:
		ps = color_mode_to_bpp(color_mode) / 8;
		break;
	}

	DSSDBG("calc_rot(%d): scrw %d, %dx%d\n", rotation, screen_width,
			width, height);

	/*
	 * field 0 = even field = bottom field
	 * field 1 = odd field = top field
	 */
	switch (rotation + mirror * 4) {
	case OMAP_DSS_ROT_0:
	case OMAP_DSS_ROT_180:
		/*
		 * If the pixel format is YUV or UYVY divide the width
		 * of the image by 2 for 0 and 180 degree rotation.
		 */
		if (color_mode == OMAP_DSS_COLOR_YUV2 ||
			color_mode == OMAP_DSS_COLOR_UYVY)
			width = width >> 1;
	case OMAP_DSS_ROT_90:
	case OMAP_DSS_ROT_270:
		*offset1 = 0;
		if (field_offset)
			*offset0 = field_offset * screen_width * ps;
		else
			*offset0 = 0;

		*row_inc = pixinc(1 +
			(y_predecim * screen_width - x_predecim * width) +
			(fieldmode ? screen_width : 0), ps);
		*pix_inc = pixinc(x_predecim, ps);
		break;

	case OMAP_DSS_ROT_0 + 4:
	case OMAP_DSS_ROT_180 + 4:
		/* If the pixel format is YUV or UYVY divide the width
		 * of the image by 2  for 0 degree and 180 degree
		 */
		if (color_mode == OMAP_DSS_COLOR_YUV2 ||
			color_mode == OMAP_DSS_COLOR_UYVY)
			width = width >> 1;
	case OMAP_DSS_ROT_90 + 4:
	case OMAP_DSS_ROT_270 + 4:
		*offset1 = 0;
		if (field_offset)
			*offset0 = field_offset * screen_width * ps;
		else
			*offset0 = 0;
		*row_inc = pixinc(1 -
			(y_predecim * screen_width + x_predecim * width) -
			(fieldmode ? screen_width : 0), ps);
		*pix_inc = pixinc(x_predecim, ps);
		break;

	default:
		BUG();
		return;
	}
}

static void calc_dma_rotation_offset(u8 rotation, bool mirror,
		u16 screen_width,
		u16 width, u16 height,
		enum omap_color_mode color_mode, bool fieldmode,
		unsigned int field_offset,
		unsigned *offset0, unsigned *offset1,
		s32 *row_inc, s32 *pix_inc, int x_predecim, int y_predecim)
{
	u8 ps;
	u16 fbw, fbh;

	/* FIXME CLUT formats */
	switch (color_mode) {
	case OMAP_DSS_COLOR_CLUT1:
	case OMAP_DSS_COLOR_CLUT2:
	case OMAP_DSS_COLOR_CLUT4:
	case OMAP_DSS_COLOR_CLUT8:
		BUG();
		return;
	default:
		ps = color_mode_to_bpp(color_mode) / 8;
		break;
	}

	DSSDBG("calc_rot(%d): scrw %d, %dx%d\n", rotation, screen_width,
			width, height);

	/* width & height are overlay sizes, convert to fb sizes */

	if (rotation == OMAP_DSS_ROT_0 || rotation == OMAP_DSS_ROT_180) {
		fbw = width;
		fbh = height;
	} else {
		fbw = height;
		fbh = width;
	}

	/*
	 * field 0 = even field = bottom field
	 * field 1 = odd field = top field
	 */
	switch (rotation + mirror * 4) {
	case OMAP_DSS_ROT_0:
		*offset1 = 0;
		if (field_offset)
			*offset0 = *offset1 + field_offset * screen_width * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(1 +
			(y_predecim * screen_width - fbw * x_predecim) +
			(fieldmode ? screen_width : 0),	ps);
		if (color_mode == OMAP_DSS_COLOR_YUV2 ||
			color_mode == OMAP_DSS_COLOR_UYVY)
			*pix_inc = pixinc(x_predecim, 2 * ps);
		else
			*pix_inc = pixinc(x_predecim, ps);
		break;
	case OMAP_DSS_ROT_90:
		*offset1 = screen_width * (fbh - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 + field_offset * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(screen_width * (fbh * x_predecim - 1) +
				y_predecim + (fieldmode ? 1 : 0), ps);
		*pix_inc = pixinc(-x_predecim * screen_width, ps);
		break;
	case OMAP_DSS_ROT_180:
		*offset1 = (screen_width * (fbh - 1) + fbw - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 - field_offset * screen_width * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(-1 -
			(y_predecim * screen_width - fbw * x_predecim) -
			(fieldmode ? screen_width : 0),	ps);
		if (color_mode == OMAP_DSS_COLOR_YUV2 ||
			color_mode == OMAP_DSS_COLOR_UYVY)
			*pix_inc = pixinc(-x_predecim, 2 * ps);
		else
			*pix_inc = pixinc(-x_predecim, ps);
		break;
	case OMAP_DSS_ROT_270:
		*offset1 = (fbw - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 - field_offset * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(-screen_width * (fbh * x_predecim - 1) -
				y_predecim - (fieldmode ? 1 : 0), ps);
		*pix_inc = pixinc(x_predecim * screen_width, ps);
		break;

	/* mirroring */
	case OMAP_DSS_ROT_0 + 4:
		*offset1 = (fbw - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 + field_offset * screen_width * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(y_predecim * screen_width * 2 - 1 +
				(fieldmode ? screen_width : 0),
				ps);
		if (color_mode == OMAP_DSS_COLOR_YUV2 ||
			color_mode == OMAP_DSS_COLOR_UYVY)
			*pix_inc = pixinc(-x_predecim, 2 * ps);
		else
			*pix_inc = pixinc(-x_predecim, ps);
		break;

	case OMAP_DSS_ROT_90 + 4:
		*offset1 = 0;
		if (field_offset)
			*offset0 = *offset1 + field_offset * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(-screen_width * (fbh * x_predecim - 1) +
				y_predecim + (fieldmode ? 1 : 0),
				ps);
		*pix_inc = pixinc(x_predecim * screen_width, ps);
		break;

	case OMAP_DSS_ROT_180 + 4:
		*offset1 = screen_width * (fbh - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 - field_offset * screen_width * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(1 - y_predecim * screen_width * 2 -
				(fieldmode ? screen_width : 0),
				ps);
		if (color_mode == OMAP_DSS_COLOR_YUV2 ||
			color_mode == OMAP_DSS_COLOR_UYVY)
			*pix_inc = pixinc(x_predecim, 2 * ps);
		else
			*pix_inc = pixinc(x_predecim, ps);
		break;

	case OMAP_DSS_ROT_270 + 4:
		*offset1 = (screen_width * (fbh - 1) + fbw - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 - field_offset * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(screen_width * (fbh * x_predecim - 1) -
				y_predecim - (fieldmode ? 1 : 0),
				ps);
		*pix_inc = pixinc(-x_predecim * screen_width, ps);
		break;

	default:
		BUG();
		return;
	}
}

static void calc_tiler_rotation_offset(u16 screen_width, u16 width,
		enum omap_color_mode color_mode, bool fieldmode,
		unsigned int field_offset, unsigned *offset0, unsigned *offset1,
		s32 *row_inc, s32 *pix_inc, int x_predecim, int y_predecim)
{
	u8 ps;

	switch (color_mode) {
	case OMAP_DSS_COLOR_CLUT1:
	case OMAP_DSS_COLOR_CLUT2:
	case OMAP_DSS_COLOR_CLUT4:
	case OMAP_DSS_COLOR_CLUT8:
		BUG();
		return;
	default:
		ps = color_mode_to_bpp(color_mode) / 8;
		break;
	}

	DSSDBG("scrw %d, width %d\n", screen_width, width);

	/*
	 * field 0 = even field = bottom field
	 * field 1 = odd field = top field
	 */
	*offset1 = 0;
	if (field_offset)
		*offset0 = *offset1 + field_offset * screen_width * ps;
	else
		*offset0 = *offset1;
	*row_inc = pixinc(1 + (y_predecim * screen_width - width * x_predecim) +
			(fieldmode ? screen_width : 0), ps);
	if (color_mode == OMAP_DSS_COLOR_YUV2 ||
		color_mode == OMAP_DSS_COLOR_UYVY)
		*pix_inc = pixinc(x_predecim, 2 * ps);
	else
		*pix_inc = pixinc(x_predecim, ps);
}

/*
 * This function is used to avoid synclosts in OMAP3, because of some
 * undocumented horizontal position and timing related limitations.
 */
static int check_horiz_timing_omap3(enum omap_plane plane,
		const struct omap_video_timings *t, u16 pos_x,
		u16 width, u16 height, u16 out_width, u16 out_height)
{
	const int ds = DIV_ROUND_UP(height, out_height);
	unsigned long nonactive;
	static const u8 limits[3] = { 8, 10, 20 };
	u64 val, blank;
	unsigned long pclk = dispc_plane_pclk_rate(plane);
	unsigned long lclk = dispc_plane_lclk_rate(plane);
	int i;

	nonactive = t->x_res + t->hfp + t->hsw + t->hbp - out_width;

	i = 0;
	if (out_height < height)
		i++;
	if (out_width < width)
		i++;
	blank = div_u64((u64)(t->hbp + t->hsw + t->hfp) * lclk, pclk);
	DSSDBG("blanking period + ppl = %llu (limit = %u)\n", blank, limits[i]);
	if (blank <= limits[i])
		return -EINVAL;

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

static unsigned long calc_core_clk_five_taps(enum omap_plane plane,
		const struct omap_video_timings *mgr_timings, u16 width,
		u16 height, u16 out_width, u16 out_height,
		enum omap_color_mode color_mode)
{
	u32 core_clk = 0;
	u64 tmp;
	unsigned long pclk = dispc_plane_pclk_rate(plane);

	if (height <= out_height && width <= out_width)
		return (unsigned long) pclk;

	if (height > out_height) {
		unsigned int ppl = mgr_timings->x_res;

		tmp = pclk * height * out_width;
		do_div(tmp, 2 * out_height * ppl);
		core_clk = tmp;

		if (height > 2 * out_height) {
			if (ppl == out_width)
				return 0;

			tmp = pclk * (height - 2 * out_height) * out_width;
			do_div(tmp, 2 * out_height * (ppl - out_width));
			core_clk = max_t(u32, core_clk, tmp);
		}
	}

	if (width > out_width) {
		tmp = pclk * width;
		do_div(tmp, out_width);
		core_clk = max_t(u32, core_clk, tmp);

		if (color_mode == OMAP_DSS_COLOR_RGB24U)
			core_clk <<= 1;
	}

	return core_clk;
}

static unsigned long calc_core_clk_24xx(enum omap_plane plane, u16 width,
		u16 height, u16 out_width, u16 out_height, bool mem_to_mem)
{
	unsigned long pclk = dispc_plane_pclk_rate(plane);

	if (height > out_height && width > out_width)
		return pclk * 4;
	else
		return pclk * 2;
}

static unsigned long calc_core_clk_34xx(enum omap_plane plane, u16 width,
		u16 height, u16 out_width, u16 out_height, bool mem_to_mem)
{
	unsigned int hf, vf;
	unsigned long pclk = dispc_plane_pclk_rate(plane);

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

static unsigned long calc_core_clk_44xx(enum omap_plane plane, u16 width,
		u16 height, u16 out_width, u16 out_height, bool mem_to_mem)
{
	unsigned long pclk;

	/*
	 * If the overlay/writeback is in mem to mem mode, there are no
	 * downscaling limitations with respect to pixel clock, return 1 as
	 * required core clock to represent that we have sufficient enough
	 * core clock to do maximum downscaling
	 */
	if (mem_to_mem)
		return 1;

	pclk = dispc_plane_pclk_rate(plane);

	if (width > out_width)
		return DIV_ROUND_UP(pclk, out_width) * width;
	else
		return pclk;
}

static int dispc_ovl_calc_scaling_24xx(enum omap_plane plane,
		const struct omap_video_timings *mgr_timings,
		u16 width, u16 height, u16 out_width, u16 out_height,
		enum omap_color_mode color_mode, bool *five_taps,
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
		in_height = DIV_ROUND_UP(height, *decim_y);
		in_width = DIV_ROUND_UP(width, *decim_x);
		*core_clk = dispc.feat->calc_core_clk(plane, in_width,
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

	if (in_width > maxsinglelinewidth) {
		DSSERR("Cannot scale max input width exceeded");
		return -EINVAL;
	}
	return 0;
}

static int dispc_ovl_calc_scaling_34xx(enum omap_plane plane,
		const struct omap_video_timings *mgr_timings,
		u16 width, u16 height, u16 out_width, u16 out_height,
		enum omap_color_mode color_mode, bool *five_taps,
		int *x_predecim, int *y_predecim, int *decim_x, int *decim_y,
		u16 pos_x, unsigned long *core_clk, bool mem_to_mem)
{
	int error;
	u16 in_width, in_height;
	int min_factor = min(*decim_x, *decim_y);
	const int maxsinglelinewidth =
			dss_feat_get_param_max(FEAT_PARAM_LINEWIDTH);

	do {
		in_height = DIV_ROUND_UP(height, *decim_y);
		in_width = DIV_ROUND_UP(width, *decim_x);
		*core_clk = calc_core_clk_five_taps(plane, mgr_timings,
			in_width, in_height, out_width, out_height, color_mode);

		error = check_horiz_timing_omap3(plane, mgr_timings,
				pos_x, in_width, in_height, out_width,
				out_height);

		if (in_width > maxsinglelinewidth)
			if (in_height > out_height &&
						in_height < out_height * 2)
				*five_taps = false;
		if (!*five_taps)
			*core_clk = dispc.feat->calc_core_clk(plane, in_width,
					in_height, out_width, out_height,
					mem_to_mem);

		error = (error || in_width > maxsinglelinewidth * 2 ||
			(in_width > maxsinglelinewidth && *five_taps) ||
			!*core_clk || *core_clk > dispc_core_clk_rate());
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

	if (check_horiz_timing_omap3(plane, mgr_timings, pos_x, width, height,
		out_width, out_height)){
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

static int dispc_ovl_calc_scaling_44xx(enum omap_plane plane,
		const struct omap_video_timings *mgr_timings,
		u16 width, u16 height, u16 out_width, u16 out_height,
		enum omap_color_mode color_mode, bool *five_taps,
		int *x_predecim, int *y_predecim, int *decim_x, int *decim_y,
		u16 pos_x, unsigned long *core_clk, bool mem_to_mem)
{
	u16 in_width, in_width_max;
	int decim_x_min = *decim_x;
	u16 in_height = DIV_ROUND_UP(height, *decim_y);
	const int maxsinglelinewidth =
				dss_feat_get_param_max(FEAT_PARAM_LINEWIDTH);
	const int maxdownscale = dss_feat_get_param_max(FEAT_PARAM_DOWNSCALE);

	if (mem_to_mem) {
		in_width_max = out_width * maxdownscale;
	} else {
		unsigned long pclk = dispc_plane_pclk_rate(plane);

		in_width_max = dispc_core_clk_rate() /
					DIV_ROUND_UP(pclk, out_width);
	}

	*decim_x = DIV_ROUND_UP(width, in_width_max);

	*decim_x = *decim_x > decim_x_min ? *decim_x : decim_x_min;
	if (*decim_x > *x_predecim)
		return -EINVAL;

	do {
		in_width = DIV_ROUND_UP(width, *decim_x);
	} while (*decim_x <= *x_predecim &&
			in_width > maxsinglelinewidth && ++*decim_x);

	if (in_width > maxsinglelinewidth) {
		DSSERR("Cannot scale width exceeds max line width");
		return -EINVAL;
	}

	*core_clk = dispc.feat->calc_core_clk(plane, in_width, in_height,
				out_width, out_height, mem_to_mem);
	return 0;
}

static int dispc_ovl_calc_scaling(enum omap_plane plane,
		enum omap_overlay_caps caps,
		const struct omap_video_timings *mgr_timings,
		u16 width, u16 height, u16 out_width, u16 out_height,
		enum omap_color_mode color_mode, bool *five_taps,
		int *x_predecim, int *y_predecim, u16 pos_x,
		enum omap_dss_rotation_type rotation_type, bool mem_to_mem)
{
	const int maxdownscale = dss_feat_get_param_max(FEAT_PARAM_DOWNSCALE);
	const int max_decim_limit = 16;
	unsigned long core_clk = 0;
	int decim_x, decim_y, ret;

	if (width == out_width && height == out_height)
		return 0;

	if ((caps & OMAP_DSS_OVL_CAP_SCALE) == 0)
		return -EINVAL;

	if (plane == OMAP_DSS_WB) {
		*x_predecim = *y_predecim = 1;
	} else {
		*x_predecim = max_decim_limit;
		*y_predecim = (rotation_type == OMAP_DSS_ROT_TILER &&
				dss_has_feature(FEAT_BURST_2D)) ?
				2 : max_decim_limit;
	}

	if (color_mode == OMAP_DSS_COLOR_CLUT1 ||
	    color_mode == OMAP_DSS_COLOR_CLUT2 ||
	    color_mode == OMAP_DSS_COLOR_CLUT4 ||
	    color_mode == OMAP_DSS_COLOR_CLUT8) {
		*x_predecim = 1;
		*y_predecim = 1;
		*five_taps = false;
		return 0;
	}

	decim_x = DIV_ROUND_UP(DIV_ROUND_UP(width, out_width), maxdownscale);
	decim_y = DIV_ROUND_UP(DIV_ROUND_UP(height, out_height), maxdownscale);

	if (decim_x > *x_predecim || out_width > width * 8)
		return -EINVAL;

	if (decim_y > *y_predecim || out_height > height * 8)
		return -EINVAL;

	ret = dispc.feat->calc_scaling(plane, mgr_timings, width, height,
		out_width, out_height, color_mode, five_taps,
		x_predecim, y_predecim, &decim_x, &decim_y, pos_x, &core_clk,
		mem_to_mem);
	if (ret)
		return ret;

	DSSDBG("required core clk rate = %lu Hz\n", core_clk);
	DSSDBG("current core clk rate = %lu Hz\n", dispc_core_clk_rate());

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

static int dispc_ovl_setup_common(enum omap_plane plane,
		enum omap_overlay_caps caps, u32 paddr, u32 p_uv_addr,
		u16 screen_width, int pos_x, int pos_y, u16 width, u16 height,
		u16 out_width, u16 out_height, enum omap_color_mode color_mode,
		u8 rotation, bool mirror, u8 zorder, u8 pre_mult_alpha,
		u8 global_alpha, enum omap_dss_rotation_type rotation_type,
		bool replication, const struct omap_video_timings *mgr_timings,
		bool mem_to_mem)
{
	bool five_taps = true;
	bool fieldmode = 0;
	int r, cconv = 0;
	unsigned offset0, offset1;
	s32 row_inc;
	s32 pix_inc;
	u16 frame_width, frame_height;
	unsigned int field_offset = 0;
	u16 in_height = height;
	u16 in_width = width;
	int x_predecim = 1, y_predecim = 1;
	bool ilace = mgr_timings->interlace;

	if (paddr == 0)
		return -EINVAL;

	out_width = out_width == 0 ? width : out_width;
	out_height = out_height == 0 ? height : out_height;

	if (ilace && height == out_height)
		fieldmode = 1;

	if (ilace) {
		if (fieldmode)
			in_height /= 2;
		pos_y /= 2;
		out_height /= 2;

		DSSDBG("adjusting for ilace: height %d, pos_y %d, "
			"out_height %d\n", in_height, pos_y,
			out_height);
	}

	if (!dss_feat_color_mode_supported(plane, color_mode))
		return -EINVAL;

	r = dispc_ovl_calc_scaling(plane, caps, mgr_timings, in_width,
			in_height, out_width, out_height, color_mode,
			&five_taps, &x_predecim, &y_predecim, pos_x,
			rotation_type, mem_to_mem);
	if (r)
		return r;

	in_width = DIV_ROUND_UP(in_width, x_predecim);
	in_height = DIV_ROUND_UP(in_height, y_predecim);

	if (color_mode == OMAP_DSS_COLOR_YUV2 ||
			color_mode == OMAP_DSS_COLOR_UYVY ||
			color_mode == OMAP_DSS_COLOR_NV12)
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

	if (rotation_type == OMAP_DSS_ROT_TILER)
		calc_tiler_rotation_offset(screen_width, frame_width,
				color_mode, fieldmode, field_offset,
				&offset0, &offset1, &row_inc, &pix_inc,
				x_predecim, y_predecim);
	else if (rotation_type == OMAP_DSS_ROT_DMA)
		calc_dma_rotation_offset(rotation, mirror, screen_width,
				frame_width, frame_height,
				color_mode, fieldmode, field_offset,
				&offset0, &offset1, &row_inc, &pix_inc,
				x_predecim, y_predecim);
	else
		calc_vrfb_rotation_offset(rotation, mirror,
				screen_width, frame_width, frame_height,
				color_mode, fieldmode, field_offset,
				&offset0, &offset1, &row_inc, &pix_inc,
				x_predecim, y_predecim);

	DSSDBG("offset0 %u, offset1 %u, row_inc %d, pix_inc %d\n",
			offset0, offset1, row_inc, pix_inc);

	dispc_ovl_set_color_mode(plane, color_mode);

	dispc_ovl_configure_burst_type(plane, rotation_type);

	dispc_ovl_set_ba0(plane, paddr + offset0);
	dispc_ovl_set_ba1(plane, paddr + offset1);

	if (OMAP_DSS_COLOR_NV12 == color_mode) {
		dispc_ovl_set_ba0_uv(plane, p_uv_addr + offset0);
		dispc_ovl_set_ba1_uv(plane, p_uv_addr + offset1);
	}

	dispc_ovl_set_row_inc(plane, row_inc);
	dispc_ovl_set_pix_inc(plane, pix_inc);

	DSSDBG("%d,%d %dx%d -> %dx%d\n", pos_x, pos_y, in_width,
			in_height, out_width, out_height);

	dispc_ovl_set_pos(plane, caps, pos_x, pos_y);

	dispc_ovl_set_input_size(plane, in_width, in_height);

	if (caps & OMAP_DSS_OVL_CAP_SCALE) {
		dispc_ovl_set_scaling(plane, in_width, in_height, out_width,
				   out_height, ilace, five_taps, fieldmode,
				   color_mode, rotation);
		dispc_ovl_set_output_size(plane, out_width, out_height);
		dispc_ovl_set_vid_color_conv(plane, cconv);
	}

	dispc_ovl_set_rotation_attrs(plane, rotation, mirror, color_mode);

	dispc_ovl_set_zorder(plane, caps, zorder);
	dispc_ovl_set_pre_mult_alpha(plane, caps, pre_mult_alpha);
	dispc_ovl_setup_global_alpha(plane, caps, global_alpha);

	dispc_ovl_enable_replication(plane, caps, replication);

	return 0;
}

int dispc_ovl_setup(enum omap_plane plane, const struct omap_overlay_info *oi,
		bool replication, const struct omap_video_timings *mgr_timings,
		bool mem_to_mem)
{
	int r;
	enum omap_overlay_caps caps = dss_feat_get_overlay_caps(plane);
	enum omap_channel channel;

	channel = dispc_ovl_get_channel_out(plane);

	DSSDBG("dispc_ovl_setup %d, pa %x, pa_uv %x, sw %d, %d,%d, %dx%d -> "
		"%dx%d, cmode %x, rot %d, mir %d, chan %d repl %d\n",
		plane, oi->paddr, oi->p_uv_addr, oi->screen_width, oi->pos_x,
		oi->pos_y, oi->width, oi->height, oi->out_width, oi->out_height,
		oi->color_mode, oi->rotation, oi->mirror, channel, replication);

	r = dispc_ovl_setup_common(plane, caps, oi->paddr, oi->p_uv_addr,
		oi->screen_width, oi->pos_x, oi->pos_y, oi->width, oi->height,
		oi->out_width, oi->out_height, oi->color_mode, oi->rotation,
		oi->mirror, oi->zorder, oi->pre_mult_alpha, oi->global_alpha,
		oi->rotation_type, replication, mgr_timings, mem_to_mem);

	return r;
}

int dispc_wb_setup(const struct omap_dss_writeback_info *wi,
		bool mem_to_mem, const struct omap_video_timings *mgr_timings)
{
	int r;
	u32 l;
	enum omap_plane plane = OMAP_DSS_WB;
	const int pos_x = 0, pos_y = 0;
	const u8 zorder = 0, global_alpha = 0;
	const bool replication = false;
	bool truncation;
	int in_width = mgr_timings->x_res;
	int in_height = mgr_timings->y_res;
	enum omap_overlay_caps caps =
		OMAP_DSS_OVL_CAP_SCALE | OMAP_DSS_OVL_CAP_PRE_MULT_ALPHA;

	DSSDBG("dispc_wb_setup, pa %x, pa_uv %x, %d,%d -> %dx%d, cmode %x, "
		"rot %d, mir %d\n", wi->paddr, wi->p_uv_addr, in_width,
		in_height, wi->width, wi->height, wi->color_mode, wi->rotation,
		wi->mirror);

	r = dispc_ovl_setup_common(plane, caps, wi->paddr, wi->p_uv_addr,
		wi->buf_width, pos_x, pos_y, in_width, in_height, wi->width,
		wi->height, wi->color_mode, wi->rotation, wi->mirror, zorder,
		wi->pre_mult_alpha, global_alpha, wi->rotation_type,
		replication, mgr_timings, mem_to_mem);

	switch (wi->color_mode) {
	case OMAP_DSS_COLOR_RGB16:
	case OMAP_DSS_COLOR_RGB24P:
	case OMAP_DSS_COLOR_ARGB16:
	case OMAP_DSS_COLOR_RGBA16:
	case OMAP_DSS_COLOR_RGB12U:
	case OMAP_DSS_COLOR_ARGB16_1555:
	case OMAP_DSS_COLOR_XRGB16_1555:
	case OMAP_DSS_COLOR_RGBX16:
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
	dispc_write_reg(DISPC_OVL_ATTRIBUTES(plane), l);

	return r;
}

int dispc_ovl_enable(enum omap_plane plane, bool enable)
{
	DSSDBG("dispc_enable_plane %d, %d\n", plane, enable);

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), enable ? 1 : 0, 0, 0);

	return 0;
}

bool dispc_ovl_enabled(enum omap_plane plane)
{
	return REG_GET(DISPC_OVL_ATTRIBUTES(plane), 0, 0);
}

static void dispc_mgr_disable_isr(void *data, u32 mask)
{
	struct completion *compl = data;
	complete(compl);
}

void dispc_mgr_enable(enum omap_channel channel, bool enable)
{
	mgr_fld_write(channel, DISPC_MGR_FLD_ENABLE, enable);
	/* flush posted write */
	mgr_fld_read(channel, DISPC_MGR_FLD_ENABLE);
}

bool dispc_mgr_is_enabled(enum omap_channel channel)
{
	return !!mgr_fld_read(channel, DISPC_MGR_FLD_ENABLE);
}

static void dispc_mgr_enable_lcd_out(enum omap_channel channel)
{
	dispc_mgr_enable(channel, true);
}

static void dispc_mgr_disable_lcd_out(enum omap_channel channel)
{
	DECLARE_COMPLETION_ONSTACK(framedone_compl);
	int r;
	u32 irq;

	if (dispc_mgr_is_enabled(channel) == false)
		return;

	/*
	 * When we disable LCD output, we need to wait for FRAMEDONE to know
	 * that DISPC has finished with the LCD output.
	 */

	irq = dispc_mgr_get_framedone_irq(channel);

	r = omap_dispc_register_isr(dispc_mgr_disable_isr, &framedone_compl,
			irq);
	if (r)
		DSSERR("failed to register FRAMEDONE isr\n");

	dispc_mgr_enable(channel, false);

	/* if we couldn't register for framedone, just sleep and exit */
	if (r) {
		msleep(100);
		return;
	}

	if (!wait_for_completion_timeout(&framedone_compl,
				msecs_to_jiffies(100)))
		DSSERR("timeout waiting for FRAME DONE\n");

	r = omap_dispc_unregister_isr(dispc_mgr_disable_isr, &framedone_compl,
			irq);
	if (r)
		DSSERR("failed to unregister FRAMEDONE isr\n");
}

static void dispc_digit_out_enable_isr(void *data, u32 mask)
{
	struct completion *compl = data;

	/* ignore any sync lost interrupts */
	if (mask & (DISPC_IRQ_EVSYNC_EVEN | DISPC_IRQ_EVSYNC_ODD))
		complete(compl);
}

static void dispc_mgr_enable_digit_out(void)
{
	DECLARE_COMPLETION_ONSTACK(vsync_compl);
	int r;
	u32 irq_mask;

	if (dispc_mgr_is_enabled(OMAP_DSS_CHANNEL_DIGIT) == true)
		return;

	/*
	 * Digit output produces some sync lost interrupts during the first
	 * frame when enabling. Those need to be ignored, so we register for the
	 * sync lost irq to prevent the error handler from triggering.
	 */

	irq_mask = dispc_mgr_get_vsync_irq(OMAP_DSS_CHANNEL_DIGIT) |
		dispc_mgr_get_sync_lost_irq(OMAP_DSS_CHANNEL_DIGIT);

	r = omap_dispc_register_isr(dispc_digit_out_enable_isr, &vsync_compl,
			irq_mask);
	if (r) {
		DSSERR("failed to register %x isr\n", irq_mask);
		return;
	}

	dispc_mgr_enable(OMAP_DSS_CHANNEL_DIGIT, true);

	/* wait for the first evsync */
	if (!wait_for_completion_timeout(&vsync_compl, msecs_to_jiffies(100)))
		DSSERR("timeout waiting for digit out to start\n");

	r = omap_dispc_unregister_isr(dispc_digit_out_enable_isr, &vsync_compl,
			irq_mask);
	if (r)
		DSSERR("failed to unregister %x isr\n", irq_mask);
}

static void dispc_mgr_disable_digit_out(void)
{
	DECLARE_COMPLETION_ONSTACK(framedone_compl);
	enum dss_hdmi_venc_clk_source_select src;
	int r, i;
	u32 irq_mask;
	int num_irqs;

	if (dispc_mgr_is_enabled(OMAP_DSS_CHANNEL_DIGIT) == false)
		return;

	src = dss_get_hdmi_venc_clk_source();

	/*
	 * When we disable the digit output, we need to wait for FRAMEDONE to
	 * know that DISPC has finished with the output. For analog tv out we'll
	 * use vsync, as omap2/3 don't have framedone for TV.
	 */

	if (src == DSS_HDMI_M_PCLK) {
		irq_mask = DISPC_IRQ_FRAMEDONETV;
		num_irqs = 1;
	} else {
		irq_mask = dispc_mgr_get_vsync_irq(OMAP_DSS_CHANNEL_DIGIT);
		/*
		 * We need to wait for both even and odd vsyncs. Note that this
		 * is not totally reliable, as we could get a vsync interrupt
		 * before we disable the output, which leads to timeout in the
		 * wait_for_completion.
		 */
		num_irqs = 2;
	}

	r = omap_dispc_register_isr(dispc_mgr_disable_isr, &framedone_compl,
			irq_mask);
	if (r)
		DSSERR("failed to register %x isr\n", irq_mask);

	dispc_mgr_enable(OMAP_DSS_CHANNEL_DIGIT, false);

	/* if we couldn't register the irq, just sleep and exit */
	if (r) {
		msleep(100);
		return;
	}

	for (i = 0; i < num_irqs; ++i) {
		if (!wait_for_completion_timeout(&framedone_compl,
					msecs_to_jiffies(100)))
			DSSERR("timeout waiting for digit out to stop\n");
	}

	r = omap_dispc_unregister_isr(dispc_mgr_disable_isr, &framedone_compl,
			irq_mask);
	if (r)
		DSSERR("failed to unregister %x isr\n", irq_mask);
}

void dispc_mgr_enable_sync(enum omap_channel channel)
{
	if (dss_mgr_is_lcd(channel))
		dispc_mgr_enable_lcd_out(channel);
	else if (channel == OMAP_DSS_CHANNEL_DIGIT)
		dispc_mgr_enable_digit_out();
	else
		WARN_ON(1);
}

void dispc_mgr_disable_sync(enum omap_channel channel)
{
	if (dss_mgr_is_lcd(channel))
		dispc_mgr_disable_lcd_out(channel);
	else if (channel == OMAP_DSS_CHANNEL_DIGIT)
		dispc_mgr_disable_digit_out();
	else
		WARN_ON(1);
}

void dispc_wb_enable(bool enable)
{
	enum omap_plane plane = OMAP_DSS_WB;
	struct completion frame_done_completion;
	bool is_on;
	int r;
	u32 irq;

	is_on = REG_GET(DISPC_OVL_ATTRIBUTES(plane), 0, 0);
	irq = DISPC_IRQ_FRAMEDONEWB;

	if (!enable && is_on) {
		init_completion(&frame_done_completion);

		r = omap_dispc_register_isr(dispc_mgr_disable_isr,
				&frame_done_completion, irq);
		if (r)
			DSSERR("failed to register FRAMEDONEWB isr\n");
	}

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), enable ? 1 : 0, 0, 0);

	if (!enable && is_on) {
		if (!wait_for_completion_timeout(&frame_done_completion,
					msecs_to_jiffies(100)))
			DSSERR("timeout waiting for FRAMEDONEWB\n");

		r = omap_dispc_unregister_isr(dispc_mgr_disable_isr,
				&frame_done_completion, irq);
		if (r)
			DSSERR("failed to unregister FRAMEDONEWB isr\n");
	}
}

bool dispc_wb_is_enabled(void)
{
	enum omap_plane plane = OMAP_DSS_WB;

	return REG_GET(DISPC_OVL_ATTRIBUTES(plane), 0, 0);
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

static void dispc_mgr_enable_fifohandcheck(enum omap_channel channel, bool enable)
{
	mgr_fld_write(channel, DISPC_MGR_FLD_FIFOHANDCHECK, enable);
}


static void dispc_mgr_set_lcd_type_tft(enum omap_channel channel)
{
	mgr_fld_write(channel, DISPC_MGR_FLD_STNTFT, 1);
}

void dispc_set_loadmode(enum omap_dss_load_mode mode)
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

void dispc_mgr_setup(enum omap_channel channel,
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

void dispc_mgr_set_lcd_config(enum omap_channel channel,
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

static bool _dispc_lcd_timings_ok(int hsw, int hfp, int hbp,
		int vsw, int vfp, int vbp)
{
	if (hsw < 1 || hsw > dispc.feat->sw_max ||
			hfp < 1 || hfp > dispc.feat->hp_max ||
			hbp < 1 || hbp > dispc.feat->hp_max ||
			vsw < 1 || vsw > dispc.feat->sw_max ||
			vfp < 0 || vfp > dispc.feat->vp_max ||
			vbp < 0 || vbp > dispc.feat->vp_max)
		return false;
	return true;
}

bool dispc_mgr_timings_ok(enum omap_channel channel,
		const struct omap_video_timings *timings)
{
	bool timings_ok;

	timings_ok = _dispc_mgr_size_ok(timings->x_res, timings->y_res);

	if (dss_mgr_is_lcd(channel))
		timings_ok =  timings_ok && _dispc_lcd_timings_ok(timings->hsw,
						timings->hfp, timings->hbp,
						timings->vsw, timings->vfp,
						timings->vbp);

	return timings_ok;
}

static void _dispc_mgr_set_lcd_timings(enum omap_channel channel, int hsw,
		int hfp, int hbp, int vsw, int vfp, int vbp,
		enum omap_dss_signal_level vsync_level,
		enum omap_dss_signal_level hsync_level,
		enum omap_dss_signal_edge data_pclk_edge,
		enum omap_dss_signal_level de_level,
		enum omap_dss_signal_edge sync_pclk_edge)

{
	u32 timing_h, timing_v, l;
	bool onoff, rf, ipc;

	timing_h = FLD_VAL(hsw-1, dispc.feat->sw_start, 0) |
			FLD_VAL(hfp-1, dispc.feat->fp_start, 8) |
			FLD_VAL(hbp-1, dispc.feat->bp_start, 20);
	timing_v = FLD_VAL(vsw-1, dispc.feat->sw_start, 0) |
			FLD_VAL(vfp, dispc.feat->fp_start, 8) |
			FLD_VAL(vbp, dispc.feat->bp_start, 20);

	dispc_write_reg(DISPC_TIMING_H(channel), timing_h);
	dispc_write_reg(DISPC_TIMING_V(channel), timing_v);

	switch (data_pclk_edge) {
	case OMAPDSS_DRIVE_SIG_RISING_EDGE:
		ipc = false;
		break;
	case OMAPDSS_DRIVE_SIG_FALLING_EDGE:
		ipc = true;
		break;
	case OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES:
	default:
		BUG();
	}

	switch (sync_pclk_edge) {
	case OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES:
		onoff = false;
		rf = false;
		break;
	case OMAPDSS_DRIVE_SIG_FALLING_EDGE:
		onoff = true;
		rf = false;
		break;
	case OMAPDSS_DRIVE_SIG_RISING_EDGE:
		onoff = true;
		rf = true;
		break;
	default:
		BUG();
	};

	l = dispc_read_reg(DISPC_POL_FREQ(channel));
	l |= FLD_VAL(onoff, 17, 17);
	l |= FLD_VAL(rf, 16, 16);
	l |= FLD_VAL(de_level, 15, 15);
	l |= FLD_VAL(ipc, 14, 14);
	l |= FLD_VAL(hsync_level, 13, 13);
	l |= FLD_VAL(vsync_level, 12, 12);
	dispc_write_reg(DISPC_POL_FREQ(channel), l);
}

/* change name to mode? */
void dispc_mgr_set_timings(enum omap_channel channel,
		const struct omap_video_timings *timings)
{
	unsigned xtot, ytot;
	unsigned long ht, vt;
	struct omap_video_timings t = *timings;

	DSSDBG("channel %d xres %u yres %u\n", channel, t.x_res, t.y_res);

	if (!dispc_mgr_timings_ok(channel, &t)) {
		BUG();
		return;
	}

	if (dss_mgr_is_lcd(channel)) {
		_dispc_mgr_set_lcd_timings(channel, t.hsw, t.hfp, t.hbp, t.vsw,
				t.vfp, t.vbp, t.vsync_level, t.hsync_level,
				t.data_pclk_edge, t.de_level, t.sync_pclk_edge);

		xtot = t.x_res + t.hfp + t.hsw + t.hbp;
		ytot = t.y_res + t.vfp + t.vsw + t.vbp;

		ht = (timings->pixel_clock * 1000) / xtot;
		vt = (timings->pixel_clock * 1000) / xtot / ytot;

		DSSDBG("pck %u\n", timings->pixel_clock);
		DSSDBG("hsw %d hfp %d hbp %d vsw %d vfp %d vbp %d\n",
			t.hsw, t.hfp, t.hbp, t.vsw, t.vfp, t.vbp);
		DSSDBG("vsync_level %d hsync_level %d data_pclk_edge %d de_level %d sync_pclk_edge %d\n",
			t.vsync_level, t.hsync_level, t.data_pclk_edge,
			t.de_level, t.sync_pclk_edge);

		DSSDBG("hsync %luHz, vsync %luHz\n", ht, vt);
	} else {
		if (t.interlace == true)
			t.y_res /= 2;
	}

	dispc_mgr_set_size(channel, t.x_res, t.y_res);
}

static void dispc_mgr_set_lcd_divisor(enum omap_channel channel, u16 lck_div,
		u16 pck_div)
{
	BUG_ON(lck_div < 1);
	BUG_ON(pck_div < 1);

	dispc_write_reg(DISPC_DIVISORo(channel),
			FLD_VAL(lck_div, 23, 16) | FLD_VAL(pck_div, 7, 0));
}

static void dispc_mgr_get_lcd_divisor(enum omap_channel channel, int *lck_div,
		int *pck_div)
{
	u32 l;
	l = dispc_read_reg(DISPC_DIVISORo(channel));
	*lck_div = FLD_GET(l, 23, 16);
	*pck_div = FLD_GET(l, 7, 0);
}

unsigned long dispc_fclk_rate(void)
{
	struct platform_device *dsidev;
	unsigned long r = 0;

	switch (dss_get_dispc_clk_source()) {
	case OMAP_DSS_CLK_SRC_FCK:
		r = clk_get_rate(dispc.dss_clk);
		break;
	case OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DISPC:
		dsidev = dsi_get_dsidev_from_id(0);
		r = dsi_get_pll_hsdiv_dispc_rate(dsidev);
		break;
	case OMAP_DSS_CLK_SRC_DSI2_PLL_HSDIV_DISPC:
		dsidev = dsi_get_dsidev_from_id(1);
		r = dsi_get_pll_hsdiv_dispc_rate(dsidev);
		break;
	default:
		BUG();
		return 0;
	}

	return r;
}

unsigned long dispc_mgr_lclk_rate(enum omap_channel channel)
{
	struct platform_device *dsidev;
	int lcd;
	unsigned long r;
	u32 l;

	if (dss_mgr_is_lcd(channel)) {
		l = dispc_read_reg(DISPC_DIVISORo(channel));

		lcd = FLD_GET(l, 23, 16);

		switch (dss_get_lcd_clk_source(channel)) {
		case OMAP_DSS_CLK_SRC_FCK:
			r = clk_get_rate(dispc.dss_clk);
			break;
		case OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DISPC:
			dsidev = dsi_get_dsidev_from_id(0);
			r = dsi_get_pll_hsdiv_dispc_rate(dsidev);
			break;
		case OMAP_DSS_CLK_SRC_DSI2_PLL_HSDIV_DISPC:
			dsidev = dsi_get_dsidev_from_id(1);
			r = dsi_get_pll_hsdiv_dispc_rate(dsidev);
			break;
		default:
			BUG();
			return 0;
		}

		return r / lcd;
	} else {
		return dispc_fclk_rate();
	}
}

unsigned long dispc_mgr_pclk_rate(enum omap_channel channel)
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
		enum dss_hdmi_venc_clk_source_select source;

		source = dss_get_hdmi_venc_clk_source();

		switch (source) {
		case DSS_VENC_TV_CLK:
			return venc_get_pixel_clock();
		case DSS_HDMI_M_PCLK:
			return hdmi_get_pixel_clock();
		default:
			BUG();
			return 0;
		}
	}
}

unsigned long dispc_core_clk_rate(void)
{
	int lcd;
	unsigned long fclk = dispc_fclk_rate();

	if (dss_has_feature(FEAT_CORE_CLK_DIV))
		lcd = REG_GET(DISPC_DIVISOR, 23, 16);
	else
		lcd = REG_GET(DISPC_DIVISORo(OMAP_DSS_CHANNEL_LCD), 23, 16);

	return fclk / lcd;
}

static unsigned long dispc_plane_pclk_rate(enum omap_plane plane)
{
	enum omap_channel channel = dispc_ovl_get_channel_out(plane);

	return dispc_mgr_pclk_rate(channel);
}

static unsigned long dispc_plane_lclk_rate(enum omap_plane plane)
{
	enum omap_channel channel = dispc_ovl_get_channel_out(plane);

	return dispc_mgr_lclk_rate(channel);
}

static void dispc_dump_clocks_channel(struct seq_file *s, enum omap_channel channel)
{
	int lcd, pcd;
	enum omap_dss_clk_source lcd_clk_src;

	seq_printf(s, "- %s -\n", mgr_desc[channel].name);

	lcd_clk_src = dss_get_lcd_clk_source(channel);

	seq_printf(s, "%s clk source = %s (%s)\n", mgr_desc[channel].name,
		dss_get_generic_clk_source_name(lcd_clk_src),
		dss_feat_get_clk_source_name(lcd_clk_src));

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
	enum omap_dss_clk_source dispc_clk_src = dss_get_dispc_clk_source();

	if (dispc_runtime_get())
		return;

	seq_printf(s, "- DISPC -\n");

	seq_printf(s, "dispc fclk source = %s (%s)\n",
			dss_get_generic_clk_source_name(dispc_clk_src),
			dss_feat_get_clk_source_name(dispc_clk_src));

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

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
static void dispc_dump_irqs(struct seq_file *s)
{
	unsigned long flags;
	struct dispc_irq_stats stats;

	spin_lock_irqsave(&dispc.irq_stats_lock, flags);

	stats = dispc.irq_stats;
	memset(&dispc.irq_stats, 0, sizeof(dispc.irq_stats));
	dispc.irq_stats.last_reset = jiffies;

	spin_unlock_irqrestore(&dispc.irq_stats_lock, flags);

	seq_printf(s, "period %u ms\n",
			jiffies_to_msecs(jiffies - stats.last_reset));

	seq_printf(s, "irqs %d\n", stats.irq_count);
#define PIS(x) \
	seq_printf(s, "%-20s %10d\n", #x, stats.irqs[ffs(DISPC_IRQ_##x)-1]);

	PIS(FRAMEDONE);
	PIS(VSYNC);
	PIS(EVSYNC_EVEN);
	PIS(EVSYNC_ODD);
	PIS(ACBIAS_COUNT_STAT);
	PIS(PROG_LINE_NUM);
	PIS(GFX_FIFO_UNDERFLOW);
	PIS(GFX_END_WIN);
	PIS(PAL_GAMMA_MASK);
	PIS(OCP_ERR);
	PIS(VID1_FIFO_UNDERFLOW);
	PIS(VID1_END_WIN);
	PIS(VID2_FIFO_UNDERFLOW);
	PIS(VID2_END_WIN);
	if (dss_feat_get_num_ovls() > 3) {
		PIS(VID3_FIFO_UNDERFLOW);
		PIS(VID3_END_WIN);
	}
	PIS(SYNC_LOST);
	PIS(SYNC_LOST_DIGIT);
	PIS(WAKEUP);
	if (dss_has_feature(FEAT_MGR_LCD2)) {
		PIS(FRAMEDONE2);
		PIS(VSYNC2);
		PIS(ACBIAS_COUNT_STAT2);
		PIS(SYNC_LOST2);
	}
	if (dss_has_feature(FEAT_MGR_LCD3)) {
		PIS(FRAMEDONE3);
		PIS(VSYNC3);
		PIS(ACBIAS_COUNT_STAT3);
		PIS(SYNC_LOST3);
	}
#undef PIS
}
#endif

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

		DUMPREG(i, DISPC_DEFAULT_COLOR);
		DUMPREG(i, DISPC_TRANS_COLOR);
		DUMPREG(i, DISPC_TIMING_H);
		DUMPREG(i, DISPC_TIMING_V);
		DUMPREG(i, DISPC_POL_FREQ);
		DUMPREG(i, DISPC_DIVISORo);
		DUMPREG(i, DISPC_SIZE_MGR);

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
		if (dss_has_feature(FEAT_PRELOAD))
			DUMPREG(i, DISPC_OVL_PRELOAD);
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

/* with fck as input clock rate, find dispc dividers that produce req_pck */
void dispc_find_clk_divs(unsigned long req_pck, unsigned long fck,
		struct dispc_clock_info *cinfo)
{
	u16 pcd_min, pcd_max;
	unsigned long best_pck;
	u16 best_ld, cur_ld;
	u16 best_pd, cur_pd;

	pcd_min = dss_feat_get_param_min(FEAT_PARAM_DSS_PCD);
	pcd_max = dss_feat_get_param_max(FEAT_PARAM_DSS_PCD);

	best_pck = 0;
	best_ld = 0;
	best_pd = 0;

	for (cur_ld = 1; cur_ld <= 255; ++cur_ld) {
		unsigned long lck = fck / cur_ld;

		for (cur_pd = pcd_min; cur_pd <= pcd_max; ++cur_pd) {
			unsigned long pck = lck / cur_pd;
			long old_delta = abs(best_pck - req_pck);
			long new_delta = abs(pck - req_pck);

			if (best_pck == 0 || new_delta < old_delta) {
				best_pck = pck;
				best_ld = cur_ld;
				best_pd = cur_pd;

				if (pck == req_pck)
					goto found;
			}

			if (pck < req_pck)
				break;
		}

		if (lck / pcd_min < req_pck)
			break;
	}

found:
	cinfo->lck_div = best_ld;
	cinfo->pck_div = best_pd;
	cinfo->lck = fck / cinfo->lck_div;
	cinfo->pck = cinfo->lck / cinfo->pck_div;
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

u32 dispc_read_irqstatus(void)
{
	return dispc_read_reg(DISPC_IRQSTATUS);
}

void dispc_clear_irqstatus(u32 mask)
{
	dispc_write_reg(DISPC_IRQSTATUS, mask);
}

u32 dispc_read_irqenable(void)
{
	return dispc_read_reg(DISPC_IRQENABLE);
}

void dispc_write_irqenable(u32 mask)
{
	u32 old_mask = dispc_read_reg(DISPC_IRQENABLE);

	/* clear the irqstatus for newly enabled irqs */
	dispc_clear_irqstatus((mask ^ old_mask) & mask);

	dispc_write_reg(DISPC_IRQENABLE, mask);
}

/* dispc.irq_lock has to be locked by the caller */
static void _omap_dispc_set_irqs(void)
{
	u32 mask;
	int i;
	struct omap_dispc_isr_data *isr_data;

	mask = dispc.irq_error_mask;

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc.registered_isr[i];

		if (isr_data->isr == NULL)
			continue;

		mask |= isr_data->mask;
	}

	dispc_write_irqenable(mask);
}

int omap_dispc_register_isr(omap_dispc_isr_t isr, void *arg, u32 mask)
{
	int i;
	int ret;
	unsigned long flags;
	struct omap_dispc_isr_data *isr_data;

	if (isr == NULL)
		return -EINVAL;

	spin_lock_irqsave(&dispc.irq_lock, flags);

	/* check for duplicate entry */
	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc.registered_isr[i];
		if (isr_data->isr == isr && isr_data->arg == arg &&
				isr_data->mask == mask) {
			ret = -EINVAL;
			goto err;
		}
	}

	isr_data = NULL;
	ret = -EBUSY;

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc.registered_isr[i];

		if (isr_data->isr != NULL)
			continue;

		isr_data->isr = isr;
		isr_data->arg = arg;
		isr_data->mask = mask;
		ret = 0;

		break;
	}

	if (ret)
		goto err;

	_omap_dispc_set_irqs();

	spin_unlock_irqrestore(&dispc.irq_lock, flags);

	return 0;
err:
	spin_unlock_irqrestore(&dispc.irq_lock, flags);

	return ret;
}
EXPORT_SYMBOL(omap_dispc_register_isr);

int omap_dispc_unregister_isr(omap_dispc_isr_t isr, void *arg, u32 mask)
{
	int i;
	unsigned long flags;
	int ret = -EINVAL;
	struct omap_dispc_isr_data *isr_data;

	spin_lock_irqsave(&dispc.irq_lock, flags);

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc.registered_isr[i];
		if (isr_data->isr != isr || isr_data->arg != arg ||
				isr_data->mask != mask)
			continue;

		/* found the correct isr */

		isr_data->isr = NULL;
		isr_data->arg = NULL;
		isr_data->mask = 0;

		ret = 0;
		break;
	}

	if (ret == 0)
		_omap_dispc_set_irqs();

	spin_unlock_irqrestore(&dispc.irq_lock, flags);

	return ret;
}
EXPORT_SYMBOL(omap_dispc_unregister_isr);

static void print_irq_status(u32 status)
{
	if ((status & dispc.irq_error_mask) == 0)
		return;

#define PIS(x) (status & DISPC_IRQ_##x) ? (#x " ") : ""

	pr_debug("DISPC IRQ: 0x%x: %s%s%s%s%s%s%s%s%s\n",
		status,
		PIS(OCP_ERR),
		PIS(GFX_FIFO_UNDERFLOW),
		PIS(VID1_FIFO_UNDERFLOW),
		PIS(VID2_FIFO_UNDERFLOW),
		dss_feat_get_num_ovls() > 3 ? PIS(VID3_FIFO_UNDERFLOW) : "",
		PIS(SYNC_LOST),
		PIS(SYNC_LOST_DIGIT),
		dss_has_feature(FEAT_MGR_LCD2) ? PIS(SYNC_LOST2) : "",
		dss_has_feature(FEAT_MGR_LCD3) ? PIS(SYNC_LOST3) : "");
#undef PIS
}

/* Called from dss.c. Note that we don't touch clocks here,
 * but we presume they are on because we got an IRQ. However,
 * an irq handler may turn the clocks off, so we may not have
 * clock later in the function. */
static irqreturn_t omap_dispc_irq_handler(int irq, void *arg)
{
	int i;
	u32 irqstatus, irqenable;
	u32 handledirqs = 0;
	u32 unhandled_errors;
	struct omap_dispc_isr_data *isr_data;
	struct omap_dispc_isr_data registered_isr[DISPC_MAX_NR_ISRS];

	spin_lock(&dispc.irq_lock);

	irqstatus = dispc_read_irqstatus();
	irqenable = dispc_read_irqenable();

	/* IRQ is not for us */
	if (!(irqstatus & irqenable)) {
		spin_unlock(&dispc.irq_lock);
		return IRQ_NONE;
	}

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
	spin_lock(&dispc.irq_stats_lock);
	dispc.irq_stats.irq_count++;
	dss_collect_irq_stats(irqstatus, dispc.irq_stats.irqs);
	spin_unlock(&dispc.irq_stats_lock);
#endif

	print_irq_status(irqstatus);

	/* Ack the interrupt. Do it here before clocks are possibly turned
	 * off */
	dispc_clear_irqstatus(irqstatus);
	/* flush posted write */
	dispc_read_irqstatus();

	/* make a copy and unlock, so that isrs can unregister
	 * themselves */
	memcpy(registered_isr, dispc.registered_isr,
			sizeof(registered_isr));

	spin_unlock(&dispc.irq_lock);

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &registered_isr[i];

		if (!isr_data->isr)
			continue;

		if (isr_data->mask & irqstatus) {
			isr_data->isr(isr_data->arg, irqstatus);
			handledirqs |= isr_data->mask;
		}
	}

	spin_lock(&dispc.irq_lock);

	unhandled_errors = irqstatus & ~handledirqs & dispc.irq_error_mask;

	if (unhandled_errors) {
		dispc.error_irqs |= unhandled_errors;

		dispc.irq_error_mask &= ~unhandled_errors;
		_omap_dispc_set_irqs();

		schedule_work(&dispc.error_work);
	}

	spin_unlock(&dispc.irq_lock);

	return IRQ_HANDLED;
}

static void dispc_error_worker(struct work_struct *work)
{
	int i;
	u32 errors;
	unsigned long flags;
	static const unsigned fifo_underflow_bits[] = {
		DISPC_IRQ_GFX_FIFO_UNDERFLOW,
		DISPC_IRQ_VID1_FIFO_UNDERFLOW,
		DISPC_IRQ_VID2_FIFO_UNDERFLOW,
		DISPC_IRQ_VID3_FIFO_UNDERFLOW,
	};

	spin_lock_irqsave(&dispc.irq_lock, flags);
	errors = dispc.error_irqs;
	dispc.error_irqs = 0;
	spin_unlock_irqrestore(&dispc.irq_lock, flags);

	dispc_runtime_get();

	for (i = 0; i < omap_dss_get_num_overlays(); ++i) {
		struct omap_overlay *ovl;
		unsigned bit;

		ovl = omap_dss_get_overlay(i);
		bit = fifo_underflow_bits[i];

		if (bit & errors) {
			DSSERR("FIFO UNDERFLOW on %s, disabling the overlay\n",
					ovl->name);
			dispc_ovl_enable(ovl->id, false);
			dispc_mgr_go(ovl->manager->id);
			msleep(50);
		}
	}

	for (i = 0; i < omap_dss_get_num_overlay_managers(); ++i) {
		struct omap_overlay_manager *mgr;
		unsigned bit;

		mgr = omap_dss_get_overlay_manager(i);
		bit = mgr_desc[i].sync_lost_irq;

		if (bit & errors) {
			int j;

			DSSERR("SYNC_LOST on channel %s, restarting the output "
					"with video overlays disabled\n",
					mgr->name);

			dss_mgr_disable(mgr);

			for (j = 0; j < omap_dss_get_num_overlays(); ++j) {
				struct omap_overlay *ovl;
				ovl = omap_dss_get_overlay(j);

				if (ovl->id != OMAP_DSS_GFX &&
						ovl->manager == mgr)
					ovl->disable(ovl);
			}

			dss_mgr_enable(mgr);
		}
	}

	if (errors & DISPC_IRQ_OCP_ERR) {
		DSSERR("OCP_ERR\n");
		for (i = 0; i < omap_dss_get_num_overlay_managers(); ++i) {
			struct omap_overlay_manager *mgr;

			mgr = omap_dss_get_overlay_manager(i);
			dss_mgr_disable(mgr);
		}
	}

	spin_lock_irqsave(&dispc.irq_lock, flags);
	dispc.irq_error_mask |= errors;
	_omap_dispc_set_irqs();
	spin_unlock_irqrestore(&dispc.irq_lock, flags);

	dispc_runtime_put();
}

int omap_dispc_wait_for_irq_timeout(u32 irqmask, unsigned long timeout)
{
	void dispc_irq_wait_handler(void *data, u32 mask)
	{
		complete((struct completion *)data);
	}

	int r;
	DECLARE_COMPLETION_ONSTACK(completion);

	r = omap_dispc_register_isr(dispc_irq_wait_handler, &completion,
			irqmask);

	if (r)
		return r;

	timeout = wait_for_completion_timeout(&completion, timeout);

	omap_dispc_unregister_isr(dispc_irq_wait_handler, &completion, irqmask);

	if (timeout == 0)
		return -ETIMEDOUT;

	return 0;
}

int omap_dispc_wait_for_irq_interruptible_timeout(u32 irqmask,
		unsigned long timeout)
{
	void dispc_irq_wait_handler(void *data, u32 mask)
	{
		complete((struct completion *)data);
	}

	int r;
	DECLARE_COMPLETION_ONSTACK(completion);

	r = omap_dispc_register_isr(dispc_irq_wait_handler, &completion,
			irqmask);

	if (r)
		return r;

	timeout = wait_for_completion_interruptible_timeout(&completion,
			timeout);

	omap_dispc_unregister_isr(dispc_irq_wait_handler, &completion, irqmask);

	if (timeout == 0)
		return -ETIMEDOUT;

	if (timeout == -ERESTARTSYS)
		return -ERESTARTSYS;

	return 0;
}

static void _omap_dispc_initialize_irq(void)
{
	unsigned long flags;

	spin_lock_irqsave(&dispc.irq_lock, flags);

	memset(dispc.registered_isr, 0, sizeof(dispc.registered_isr));

	dispc.irq_error_mask = DISPC_IRQ_MASK_ERROR;
	if (dss_has_feature(FEAT_MGR_LCD2))
		dispc.irq_error_mask |= DISPC_IRQ_SYNC_LOST2;
	if (dss_has_feature(FEAT_MGR_LCD3))
		dispc.irq_error_mask |= DISPC_IRQ_SYNC_LOST3;
	if (dss_feat_get_num_ovls() > 3)
		dispc.irq_error_mask |= DISPC_IRQ_VID3_FIFO_UNDERFLOW;

	/* there's SYNC_LOST_DIGIT waiting after enabling the DSS,
	 * so clear it */
	dispc_clear_irqstatus(dispc_read_irqstatus());

	_omap_dispc_set_irqs();

	spin_unlock_irqrestore(&dispc.irq_lock, flags);
}

void dispc_enable_sidle(void)
{
	REG_FLD_MOD(DISPC_SYSCONFIG, 2, 4, 3);	/* SIDLEMODE: smart idle */
}

void dispc_disable_sidle(void)
{
	REG_FLD_MOD(DISPC_SYSCONFIG, 1, 4, 3);	/* SIDLEMODE: no idle */
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
	}

	/* FUNCGATED */
	if (dss_has_feature(FEAT_FUNCGATED))
		REG_FLD_MOD(DISPC_CONFIG, 1, 9, 9);

	dispc_setup_color_conv_coef();

	dispc_set_loadmode(OMAP_DSS_LOAD_FRAME_ONLY);

	dispc_init_fifos();

	dispc_configure_burst_sizes();

	dispc_ovl_enable_zorder_planes();
}

static const struct dispc_features omap24xx_dispc_feats __initconst = {
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
	.calc_scaling		=	dispc_ovl_calc_scaling_24xx,
	.calc_core_clk		=	calc_core_clk_24xx,
	.num_fifos		=	3,
};

static const struct dispc_features omap34xx_rev1_0_dispc_feats __initconst = {
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
	.calc_scaling		=	dispc_ovl_calc_scaling_34xx,
	.calc_core_clk		=	calc_core_clk_34xx,
	.num_fifos		=	3,
};

static const struct dispc_features omap34xx_rev3_0_dispc_feats __initconst = {
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
	.calc_scaling		=	dispc_ovl_calc_scaling_34xx,
	.calc_core_clk		=	calc_core_clk_34xx,
	.num_fifos		=	3,
};

static const struct dispc_features omap44xx_dispc_feats __initconst = {
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
	.calc_scaling		=	dispc_ovl_calc_scaling_44xx,
	.calc_core_clk		=	calc_core_clk_44xx,
	.num_fifos		=	5,
	.gfx_fifo_workaround	=	true,
};

static const struct dispc_features omap54xx_dispc_feats __initconst = {
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
	.calc_scaling		=	dispc_ovl_calc_scaling_44xx,
	.calc_core_clk		=	calc_core_clk_44xx,
	.num_fifos		=	5,
	.gfx_fifo_workaround	=	true,
};

static int __init dispc_init_features(struct platform_device *pdev)
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
		src = &omap34xx_rev3_0_dispc_feats;
		break;

	case OMAPDSS_VER_OMAP4430_ES1:
	case OMAPDSS_VER_OMAP4430_ES2:
	case OMAPDSS_VER_OMAP4:
		src = &omap44xx_dispc_feats;
		break;

	case OMAPDSS_VER_OMAP5:
		src = &omap54xx_dispc_feats;
		break;

	default:
		return -ENODEV;
	}

	memcpy(dst, src, sizeof(*dst));
	dispc.feat = dst;

	return 0;
}

/* DISPC HW IP initialisation */
static int __init omap_dispchw_probe(struct platform_device *pdev)
{
	u32 rev;
	int r = 0;
	struct resource *dispc_mem;
	struct clk *clk;

	dispc.pdev = pdev;

	r = dispc_init_features(dispc.pdev);
	if (r)
		return r;

	spin_lock_init(&dispc.irq_lock);

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
	spin_lock_init(&dispc.irq_stats_lock);
	dispc.irq_stats.last_reset = jiffies;
#endif

	INIT_WORK(&dispc.error_work, dispc_error_worker);

	dispc_mem = platform_get_resource(dispc.pdev, IORESOURCE_MEM, 0);
	if (!dispc_mem) {
		DSSERR("can't get IORESOURCE_MEM DISPC\n");
		return -EINVAL;
	}

	dispc.base = devm_ioremap(&pdev->dev, dispc_mem->start,
				  resource_size(dispc_mem));
	if (!dispc.base) {
		DSSERR("can't ioremap DISPC\n");
		return -ENOMEM;
	}

	dispc.irq = platform_get_irq(dispc.pdev, 0);
	if (dispc.irq < 0) {
		DSSERR("platform_get_irq failed\n");
		return -ENODEV;
	}

	r = devm_request_irq(&pdev->dev, dispc.irq, omap_dispc_irq_handler,
			     IRQF_SHARED, "OMAP DISPC", dispc.pdev);
	if (r < 0) {
		DSSERR("request_irq failed\n");
		return r;
	}

	clk = clk_get(&pdev->dev, "fck");
	if (IS_ERR(clk)) {
		DSSERR("can't get fck\n");
		r = PTR_ERR(clk);
		return r;
	}

	dispc.dss_clk = clk;

	pm_runtime_enable(&pdev->dev);

	r = dispc_runtime_get();
	if (r)
		goto err_runtime_get;

	_omap_dispc_initial_config();

	_omap_dispc_initialize_irq();

	rev = dispc_read_reg(DISPC_REVISION);
	dev_dbg(&pdev->dev, "OMAP DISPC rev %d.%d\n",
	       FLD_GET(rev, 7, 4), FLD_GET(rev, 3, 0));

	dispc_runtime_put();

	dss_debugfs_create_file("dispc", dispc_dump_regs);

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
	dss_debugfs_create_file("dispc_irq", dispc_dump_irqs);
#endif
	return 0;

err_runtime_get:
	pm_runtime_disable(&pdev->dev);
	clk_put(dispc.dss_clk);
	return r;
}

static int __exit omap_dispchw_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	clk_put(dispc.dss_clk);

	return 0;
}

static int dispc_runtime_suspend(struct device *dev)
{
	dispc_save_context();

	return 0;
}

static int dispc_runtime_resume(struct device *dev)
{
	dispc_restore_context();

	return 0;
}

static const struct dev_pm_ops dispc_pm_ops = {
	.runtime_suspend = dispc_runtime_suspend,
	.runtime_resume = dispc_runtime_resume,
};

static struct platform_driver omap_dispchw_driver = {
	.remove         = __exit_p(omap_dispchw_remove),
	.driver         = {
		.name   = "omapdss_dispc",
		.owner  = THIS_MODULE,
		.pm	= &dispc_pm_ops,
	},
};

int __init dispc_init_platform_driver(void)
{
	return platform_driver_probe(&omap_dispchw_driver, omap_dispchw_probe);
}

void __exit dispc_uninit_platform_driver(void)
{
	platform_driver_unregister(&omap_dispchw_driver);
}
