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
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/hardirq.h>
#include <linux/interrupt.h>

#include <plat/sram.h>
#include <plat/clock.h>

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

struct dispc_h_coef {
	s8 hc4;
	s8 hc3;
	u8 hc2;
	s8 hc1;
	s8 hc0;
};

struct dispc_v_coef {
	s8 vc22;
	s8 vc2;
	u8 vc1;
	s8 vc0;
	s8 vc00;
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

static struct {
	struct platform_device *pdev;
	void __iomem    *base;
	int irq;

	u32	fifo_size[3];

	spinlock_t irq_lock;
	u32 irq_error_mask;
	struct omap_dispc_isr_data registered_isr[DISPC_MAX_NR_ISRS];
	u32 error_irqs;
	struct work_struct error_work;

	u32		ctx[DISPC_SZ_REGS / sizeof(u32)];

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

static void _omap_dispc_set_irqs(void);

static inline void dispc_write_reg(const u16 idx, u32 val)
{
	__raw_writel(val, dispc.base + idx);
}

static inline u32 dispc_read_reg(const u16 idx)
{
	return __raw_readl(dispc.base + idx);
}

#define SR(reg) \
	dispc.ctx[DISPC_##reg / sizeof(u32)] = dispc_read_reg(DISPC_##reg)
#define RR(reg) \
	dispc_write_reg(DISPC_##reg, dispc.ctx[DISPC_##reg / sizeof(u32)])

void dispc_save_context(void)
{
	int i;
	if (cpu_is_omap24xx())
		return;

	SR(SYSCONFIG);
	SR(IRQENABLE);
	SR(CONTROL);
	SR(CONFIG);
	SR(DEFAULT_COLOR(OMAP_DSS_CHANNEL_LCD));
	SR(DEFAULT_COLOR(OMAP_DSS_CHANNEL_DIGIT));
	SR(TRANS_COLOR(OMAP_DSS_CHANNEL_LCD));
	SR(TRANS_COLOR(OMAP_DSS_CHANNEL_DIGIT));
	SR(LINE_NUMBER);
	SR(TIMING_H(OMAP_DSS_CHANNEL_LCD));
	SR(TIMING_V(OMAP_DSS_CHANNEL_LCD));
	SR(POL_FREQ(OMAP_DSS_CHANNEL_LCD));
	SR(DIVISORo(OMAP_DSS_CHANNEL_LCD));
	SR(GLOBAL_ALPHA);
	SR(SIZE_MGR(OMAP_DSS_CHANNEL_DIGIT));
	SR(SIZE_MGR(OMAP_DSS_CHANNEL_LCD));
	if (dss_has_feature(FEAT_MGR_LCD2)) {
		SR(CONTROL2);
		SR(DEFAULT_COLOR(OMAP_DSS_CHANNEL_LCD2));
		SR(TRANS_COLOR(OMAP_DSS_CHANNEL_LCD2));
		SR(SIZE_MGR(OMAP_DSS_CHANNEL_LCD2));
		SR(TIMING_H(OMAP_DSS_CHANNEL_LCD2));
		SR(TIMING_V(OMAP_DSS_CHANNEL_LCD2));
		SR(POL_FREQ(OMAP_DSS_CHANNEL_LCD2));
		SR(DIVISORo(OMAP_DSS_CHANNEL_LCD2));
		SR(CONFIG2);
	}

	SR(OVL_BA0(OMAP_DSS_GFX));
	SR(OVL_BA1(OMAP_DSS_GFX));
	SR(OVL_POSITION(OMAP_DSS_GFX));
	SR(OVL_SIZE(OMAP_DSS_GFX));
	SR(OVL_ATTRIBUTES(OMAP_DSS_GFX));
	SR(OVL_FIFO_THRESHOLD(OMAP_DSS_GFX));
	SR(OVL_ROW_INC(OMAP_DSS_GFX));
	SR(OVL_PIXEL_INC(OMAP_DSS_GFX));
	SR(OVL_WINDOW_SKIP(OMAP_DSS_GFX));
	SR(OVL_TABLE_BA(OMAP_DSS_GFX));

	SR(DATA_CYCLE1(OMAP_DSS_CHANNEL_LCD));
	SR(DATA_CYCLE2(OMAP_DSS_CHANNEL_LCD));
	SR(DATA_CYCLE3(OMAP_DSS_CHANNEL_LCD));

	SR(CPR_COEF_R(OMAP_DSS_CHANNEL_LCD));
	SR(CPR_COEF_G(OMAP_DSS_CHANNEL_LCD));
	SR(CPR_COEF_B(OMAP_DSS_CHANNEL_LCD));
	if (dss_has_feature(FEAT_MGR_LCD2)) {
		SR(CPR_COEF_B(OMAP_DSS_CHANNEL_LCD2));
		SR(CPR_COEF_G(OMAP_DSS_CHANNEL_LCD2));
		SR(CPR_COEF_R(OMAP_DSS_CHANNEL_LCD2));

		SR(DATA_CYCLE1(OMAP_DSS_CHANNEL_LCD2));
		SR(DATA_CYCLE2(OMAP_DSS_CHANNEL_LCD2));
		SR(DATA_CYCLE3(OMAP_DSS_CHANNEL_LCD2));
	}

	SR(OVL_PRELOAD(OMAP_DSS_GFX));

	/* VID1 */
	SR(OVL_BA0(OMAP_DSS_VIDEO1));
	SR(OVL_BA1(OMAP_DSS_VIDEO1));
	SR(OVL_POSITION(OMAP_DSS_VIDEO1));
	SR(OVL_SIZE(OMAP_DSS_VIDEO1));
	SR(OVL_ATTRIBUTES(OMAP_DSS_VIDEO1));
	SR(OVL_FIFO_THRESHOLD(OMAP_DSS_VIDEO1));
	SR(OVL_ROW_INC(OMAP_DSS_VIDEO1));
	SR(OVL_PIXEL_INC(OMAP_DSS_VIDEO1));
	SR(OVL_FIR(OMAP_DSS_VIDEO1));
	SR(OVL_PICTURE_SIZE(OMAP_DSS_VIDEO1));
	SR(OVL_ACCU0(OMAP_DSS_VIDEO1));
	SR(OVL_ACCU1(OMAP_DSS_VIDEO1));

	for (i = 0; i < 8; i++)
		SR(OVL_FIR_COEF_H(OMAP_DSS_VIDEO1, i));

	for (i = 0; i < 8; i++)
		SR(OVL_FIR_COEF_HV(OMAP_DSS_VIDEO1, i));

	for (i = 0; i < 5; i++)
		SR(OVL_CONV_COEF(OMAP_DSS_VIDEO1, i));

	for (i = 0; i < 8; i++)
		SR(OVL_FIR_COEF_V(OMAP_DSS_VIDEO1, i));

	if (dss_has_feature(FEAT_HANDLE_UV_SEPARATE)) {
		SR(OVL_BA0_UV(OMAP_DSS_VIDEO1));
		SR(OVL_BA1_UV(OMAP_DSS_VIDEO1));
		SR(OVL_FIR2(OMAP_DSS_VIDEO1));
		SR(OVL_ACCU2_0(OMAP_DSS_VIDEO1));
		SR(OVL_ACCU2_1(OMAP_DSS_VIDEO1));

		for (i = 0; i < 8; i++)
			SR(OVL_FIR_COEF_H2(OMAP_DSS_VIDEO1, i));

		for (i = 0; i < 8; i++)
			SR(OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO1, i));

		for (i = 0; i < 8; i++)
			SR(OVL_FIR_COEF_V2(OMAP_DSS_VIDEO1, i));
	}
	if (dss_has_feature(FEAT_ATTR2))
		SR(OVL_ATTRIBUTES2(OMAP_DSS_VIDEO1));

	SR(OVL_PRELOAD(OMAP_DSS_VIDEO1));

	/* VID2 */
	SR(OVL_BA0(OMAP_DSS_VIDEO2));
	SR(OVL_BA1(OMAP_DSS_VIDEO2));
	SR(OVL_POSITION(OMAP_DSS_VIDEO2));
	SR(OVL_SIZE(OMAP_DSS_VIDEO2));
	SR(OVL_ATTRIBUTES(OMAP_DSS_VIDEO2));
	SR(OVL_FIFO_THRESHOLD(OMAP_DSS_VIDEO2));
	SR(OVL_ROW_INC(OMAP_DSS_VIDEO2));
	SR(OVL_PIXEL_INC(OMAP_DSS_VIDEO2));
	SR(OVL_FIR(OMAP_DSS_VIDEO2));
	SR(OVL_PICTURE_SIZE(OMAP_DSS_VIDEO2));
	SR(OVL_ACCU0(OMAP_DSS_VIDEO2));
	SR(OVL_ACCU1(OMAP_DSS_VIDEO2));

	for (i = 0; i < 8; i++)
		SR(OVL_FIR_COEF_H(OMAP_DSS_VIDEO2, i));

	for (i = 0; i < 8; i++)
		SR(OVL_FIR_COEF_HV(OMAP_DSS_VIDEO2, i));

	for (i = 0; i < 5; i++)
		SR(OVL_CONV_COEF(OMAP_DSS_VIDEO2, i));

	for (i = 0; i < 8; i++)
		SR(OVL_FIR_COEF_V(OMAP_DSS_VIDEO2, i));

	if (dss_has_feature(FEAT_HANDLE_UV_SEPARATE)) {
		SR(OVL_BA0_UV(OMAP_DSS_VIDEO2));
		SR(OVL_BA1_UV(OMAP_DSS_VIDEO2));
		SR(OVL_FIR2(OMAP_DSS_VIDEO2));
		SR(OVL_ACCU2_0(OMAP_DSS_VIDEO2));
		SR(OVL_ACCU2_1(OMAP_DSS_VIDEO2));

		for (i = 0; i < 8; i++)
			SR(OVL_FIR_COEF_H2(OMAP_DSS_VIDEO2, i));

		for (i = 0; i < 8; i++)
			SR(OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO2, i));

		for (i = 0; i < 8; i++)
			SR(OVL_FIR_COEF_V2(OMAP_DSS_VIDEO2, i));
	}
	if (dss_has_feature(FEAT_ATTR2))
		SR(OVL_ATTRIBUTES2(OMAP_DSS_VIDEO2));

	SR(OVL_PRELOAD(OMAP_DSS_VIDEO2));

	if (dss_has_feature(FEAT_CORE_CLK_DIV))
		SR(DIVISOR);
}

void dispc_restore_context(void)
{
	int i;
	RR(SYSCONFIG);
	/*RR(IRQENABLE);*/
	/*RR(CONTROL);*/
	RR(CONFIG);
	RR(DEFAULT_COLOR(OMAP_DSS_CHANNEL_LCD));
	RR(DEFAULT_COLOR(OMAP_DSS_CHANNEL_DIGIT));
	RR(TRANS_COLOR(OMAP_DSS_CHANNEL_LCD));
	RR(TRANS_COLOR(OMAP_DSS_CHANNEL_DIGIT));
	RR(LINE_NUMBER);
	RR(TIMING_H(OMAP_DSS_CHANNEL_LCD));
	RR(TIMING_V(OMAP_DSS_CHANNEL_LCD));
	RR(POL_FREQ(OMAP_DSS_CHANNEL_LCD));
	RR(DIVISORo(OMAP_DSS_CHANNEL_LCD));
	RR(GLOBAL_ALPHA);
	RR(SIZE_MGR(OMAP_DSS_CHANNEL_DIGIT));
	RR(SIZE_MGR(OMAP_DSS_CHANNEL_LCD));
	if (dss_has_feature(FEAT_MGR_LCD2)) {
		RR(DEFAULT_COLOR(OMAP_DSS_CHANNEL_LCD2));
		RR(TRANS_COLOR(OMAP_DSS_CHANNEL_LCD2));
		RR(SIZE_MGR(OMAP_DSS_CHANNEL_LCD2));
		RR(TIMING_H(OMAP_DSS_CHANNEL_LCD2));
		RR(TIMING_V(OMAP_DSS_CHANNEL_LCD2));
		RR(POL_FREQ(OMAP_DSS_CHANNEL_LCD2));
		RR(DIVISORo(OMAP_DSS_CHANNEL_LCD2));
		RR(CONFIG2);
	}

	RR(OVL_BA0(OMAP_DSS_GFX));
	RR(OVL_BA1(OMAP_DSS_GFX));
	RR(OVL_POSITION(OMAP_DSS_GFX));
	RR(OVL_SIZE(OMAP_DSS_GFX));
	RR(OVL_ATTRIBUTES(OMAP_DSS_GFX));
	RR(OVL_FIFO_THRESHOLD(OMAP_DSS_GFX));
	RR(OVL_ROW_INC(OMAP_DSS_GFX));
	RR(OVL_PIXEL_INC(OMAP_DSS_GFX));
	RR(OVL_WINDOW_SKIP(OMAP_DSS_GFX));
	RR(OVL_TABLE_BA(OMAP_DSS_GFX));


	RR(DATA_CYCLE1(OMAP_DSS_CHANNEL_LCD));
	RR(DATA_CYCLE2(OMAP_DSS_CHANNEL_LCD));
	RR(DATA_CYCLE3(OMAP_DSS_CHANNEL_LCD));

	RR(CPR_COEF_R(OMAP_DSS_CHANNEL_LCD));
	RR(CPR_COEF_G(OMAP_DSS_CHANNEL_LCD));
	RR(CPR_COEF_B(OMAP_DSS_CHANNEL_LCD));
	if (dss_has_feature(FEAT_MGR_LCD2)) {
		RR(DATA_CYCLE1(OMAP_DSS_CHANNEL_LCD2));
		RR(DATA_CYCLE2(OMAP_DSS_CHANNEL_LCD2));
		RR(DATA_CYCLE3(OMAP_DSS_CHANNEL_LCD2));

		RR(CPR_COEF_B(OMAP_DSS_CHANNEL_LCD2));
		RR(CPR_COEF_G(OMAP_DSS_CHANNEL_LCD2));
		RR(CPR_COEF_R(OMAP_DSS_CHANNEL_LCD2));
	}

	RR(OVL_PRELOAD(OMAP_DSS_GFX));

	/* VID1 */
	RR(OVL_BA0(OMAP_DSS_VIDEO1));
	RR(OVL_BA1(OMAP_DSS_VIDEO1));
	RR(OVL_POSITION(OMAP_DSS_VIDEO1));
	RR(OVL_SIZE(OMAP_DSS_VIDEO1));
	RR(OVL_ATTRIBUTES(OMAP_DSS_VIDEO1));
	RR(OVL_FIFO_THRESHOLD(OMAP_DSS_VIDEO1));
	RR(OVL_ROW_INC(OMAP_DSS_VIDEO1));
	RR(OVL_PIXEL_INC(OMAP_DSS_VIDEO1));
	RR(OVL_FIR(OMAP_DSS_VIDEO1));
	RR(OVL_PICTURE_SIZE(OMAP_DSS_VIDEO1));
	RR(OVL_ACCU0(OMAP_DSS_VIDEO1));
	RR(OVL_ACCU1(OMAP_DSS_VIDEO1));

	for (i = 0; i < 8; i++)
		RR(OVL_FIR_COEF_H(OMAP_DSS_VIDEO1, i));

	for (i = 0; i < 8; i++)
		RR(OVL_FIR_COEF_HV(OMAP_DSS_VIDEO1, i));

	for (i = 0; i < 5; i++)
		RR(OVL_CONV_COEF(OMAP_DSS_VIDEO1, i));

	for (i = 0; i < 8; i++)
		RR(OVL_FIR_COEF_V(OMAP_DSS_VIDEO1, i));

	if (dss_has_feature(FEAT_HANDLE_UV_SEPARATE)) {
		RR(OVL_BA0_UV(OMAP_DSS_VIDEO1));
		RR(OVL_BA1_UV(OMAP_DSS_VIDEO1));
		RR(OVL_FIR2(OMAP_DSS_VIDEO1));
		RR(OVL_ACCU2_0(OMAP_DSS_VIDEO1));
		RR(OVL_ACCU2_1(OMAP_DSS_VIDEO1));

		for (i = 0; i < 8; i++)
			RR(OVL_FIR_COEF_H2(OMAP_DSS_VIDEO1, i));

		for (i = 0; i < 8; i++)
			RR(OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO1, i));

		for (i = 0; i < 8; i++)
			RR(OVL_FIR_COEF_V2(OMAP_DSS_VIDEO1, i));
	}
	if (dss_has_feature(FEAT_ATTR2))
		RR(OVL_ATTRIBUTES2(OMAP_DSS_VIDEO1));

	RR(OVL_PRELOAD(OMAP_DSS_VIDEO1));

	/* VID2 */
	RR(OVL_BA0(OMAP_DSS_VIDEO2));
	RR(OVL_BA1(OMAP_DSS_VIDEO2));
	RR(OVL_POSITION(OMAP_DSS_VIDEO2));
	RR(OVL_SIZE(OMAP_DSS_VIDEO2));
	RR(OVL_ATTRIBUTES(OMAP_DSS_VIDEO2));
	RR(OVL_FIFO_THRESHOLD(OMAP_DSS_VIDEO2));
	RR(OVL_ROW_INC(OMAP_DSS_VIDEO2));
	RR(OVL_PIXEL_INC(OMAP_DSS_VIDEO2));
	RR(OVL_FIR(OMAP_DSS_VIDEO2));
	RR(OVL_PICTURE_SIZE(OMAP_DSS_VIDEO2));
	RR(OVL_ACCU0(OMAP_DSS_VIDEO2));
	RR(OVL_ACCU1(OMAP_DSS_VIDEO2));

	for (i = 0; i < 8; i++)
		RR(OVL_FIR_COEF_H(OMAP_DSS_VIDEO2, i));

	for (i = 0; i < 8; i++)
		RR(OVL_FIR_COEF_HV(OMAP_DSS_VIDEO2, i));

	for (i = 0; i < 5; i++)
		RR(OVL_CONV_COEF(OMAP_DSS_VIDEO2, i));

	for (i = 0; i < 8; i++)
		RR(OVL_FIR_COEF_V(OMAP_DSS_VIDEO2, i));

	if (dss_has_feature(FEAT_HANDLE_UV_SEPARATE)) {
		RR(OVL_BA0_UV(OMAP_DSS_VIDEO2));
		RR(OVL_BA1_UV(OMAP_DSS_VIDEO2));
		RR(OVL_FIR2(OMAP_DSS_VIDEO2));
		RR(OVL_ACCU2_0(OMAP_DSS_VIDEO2));
		RR(OVL_ACCU2_1(OMAP_DSS_VIDEO2));

		for (i = 0; i < 8; i++)
			RR(OVL_FIR_COEF_H2(OMAP_DSS_VIDEO2, i));

		for (i = 0; i < 8; i++)
			RR(OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO2, i));

		for (i = 0; i < 8; i++)
			RR(OVL_FIR_COEF_V2(OMAP_DSS_VIDEO2, i));
	}
	if (dss_has_feature(FEAT_ATTR2))
		RR(OVL_ATTRIBUTES2(OMAP_DSS_VIDEO2));

	RR(OVL_PRELOAD(OMAP_DSS_VIDEO2));

	if (dss_has_feature(FEAT_CORE_CLK_DIV))
		RR(DIVISOR);

	/* enable last, because LCD & DIGIT enable are here */
	RR(CONTROL);
	if (dss_has_feature(FEAT_MGR_LCD2))
		RR(CONTROL2);
	/* clear spurious SYNC_LOST_DIGIT interrupts */
	dispc_write_reg(DISPC_IRQSTATUS, DISPC_IRQ_SYNC_LOST_DIGIT);

	/*
	 * enable last so IRQs won't trigger before
	 * the context is fully restored
	 */
	RR(IRQENABLE);
}

#undef SR
#undef RR

static inline void enable_clocks(bool enable)
{
	if (enable)
		dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK);
	else
		dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK);
}

bool dispc_go_busy(enum omap_channel channel)
{
	int bit;

	if (channel == OMAP_DSS_CHANNEL_LCD ||
			channel == OMAP_DSS_CHANNEL_LCD2)
		bit = 5; /* GOLCD */
	else
		bit = 6; /* GODIGIT */

	if (channel == OMAP_DSS_CHANNEL_LCD2)
		return REG_GET(DISPC_CONTROL2, bit, bit) == 1;
	else
		return REG_GET(DISPC_CONTROL, bit, bit) == 1;
}

void dispc_go(enum omap_channel channel)
{
	int bit;
	bool enable_bit, go_bit;

	enable_clocks(1);

	if (channel == OMAP_DSS_CHANNEL_LCD ||
			channel == OMAP_DSS_CHANNEL_LCD2)
		bit = 0; /* LCDENABLE */
	else
		bit = 1; /* DIGITALENABLE */

	/* if the channel is not enabled, we don't need GO */
	if (channel == OMAP_DSS_CHANNEL_LCD2)
		enable_bit = REG_GET(DISPC_CONTROL2, bit, bit) == 1;
	else
		enable_bit = REG_GET(DISPC_CONTROL, bit, bit) == 1;

	if (!enable_bit)
		goto end;

	if (channel == OMAP_DSS_CHANNEL_LCD ||
			channel == OMAP_DSS_CHANNEL_LCD2)
		bit = 5; /* GOLCD */
	else
		bit = 6; /* GODIGIT */

	if (channel == OMAP_DSS_CHANNEL_LCD2)
		go_bit = REG_GET(DISPC_CONTROL2, bit, bit) == 1;
	else
		go_bit = REG_GET(DISPC_CONTROL, bit, bit) == 1;

	if (go_bit) {
		DSSERR("GO bit not down for channel %d\n", channel);
		goto end;
	}

	DSSDBG("GO %s\n", channel == OMAP_DSS_CHANNEL_LCD ? "LCD" :
		(channel == OMAP_DSS_CHANNEL_LCD2 ? "LCD2" : "DIGIT"));

	if (channel == OMAP_DSS_CHANNEL_LCD2)
		REG_FLD_MOD(DISPC_CONTROL2, 1, bit, bit);
	else
		REG_FLD_MOD(DISPC_CONTROL, 1, bit, bit);
end:
	enable_clocks(0);
}

static void _dispc_write_firh_reg(enum omap_plane plane, int reg, u32 value)
{
	dispc_write_reg(DISPC_OVL_FIR_COEF_H(plane, reg), value);
}

static void _dispc_write_firhv_reg(enum omap_plane plane, int reg, u32 value)
{
	dispc_write_reg(DISPC_OVL_FIR_COEF_HV(plane, reg), value);
}

static void _dispc_write_firv_reg(enum omap_plane plane, int reg, u32 value)
{
	dispc_write_reg(DISPC_OVL_FIR_COEF_V(plane, reg), value);
}

static void _dispc_write_firh2_reg(enum omap_plane plane, int reg, u32 value)
{
	BUG_ON(plane == OMAP_DSS_GFX);

	dispc_write_reg(DISPC_OVL_FIR_COEF_H2(plane, reg), value);
}

static void _dispc_write_firhv2_reg(enum omap_plane plane, int reg, u32 value)
{
	BUG_ON(plane == OMAP_DSS_GFX);

	dispc_write_reg(DISPC_OVL_FIR_COEF_HV2(plane, reg), value);
}

static void _dispc_write_firv2_reg(enum omap_plane plane, int reg, u32 value)
{
	BUG_ON(plane == OMAP_DSS_GFX);

	dispc_write_reg(DISPC_OVL_FIR_COEF_V2(plane, reg), value);
}

static void _dispc_set_scale_coef(enum omap_plane plane, int hscaleup,
				  int vscaleup, int five_taps,
				  enum omap_color_component color_comp)
{
	/* Coefficients for horizontal up-sampling */
	static const struct dispc_h_coef coef_hup[8] = {
		{  0,   0, 128,   0,  0 },
		{ -1,  13, 124,  -8,  0 },
		{ -2,  30, 112, -11, -1 },
		{ -5,  51,  95, -11, -2 },
		{  0,  -9,  73,  73, -9 },
		{ -2, -11,  95,  51, -5 },
		{ -1, -11, 112,  30, -2 },
		{  0,  -8, 124,  13, -1 },
	};

	/* Coefficients for vertical up-sampling */
	static const struct dispc_v_coef coef_vup_3tap[8] = {
		{ 0,  0, 128,  0, 0 },
		{ 0,  3, 123,  2, 0 },
		{ 0, 12, 111,  5, 0 },
		{ 0, 32,  89,  7, 0 },
		{ 0,  0,  64, 64, 0 },
		{ 0,  7,  89, 32, 0 },
		{ 0,  5, 111, 12, 0 },
		{ 0,  2, 123,  3, 0 },
	};

	static const struct dispc_v_coef coef_vup_5tap[8] = {
		{  0,   0, 128,   0,  0 },
		{ -1,  13, 124,  -8,  0 },
		{ -2,  30, 112, -11, -1 },
		{ -5,  51,  95, -11, -2 },
		{  0,  -9,  73,  73, -9 },
		{ -2, -11,  95,  51, -5 },
		{ -1, -11, 112,  30, -2 },
		{  0,  -8, 124,  13, -1 },
	};

	/* Coefficients for horizontal down-sampling */
	static const struct dispc_h_coef coef_hdown[8] = {
		{   0, 36, 56, 36,  0 },
		{   4, 40, 55, 31, -2 },
		{   8, 44, 54, 27, -5 },
		{  12, 48, 53, 22, -7 },
		{  -9, 17, 52, 51, 17 },
		{  -7, 22, 53, 48, 12 },
		{  -5, 27, 54, 44,  8 },
		{  -2, 31, 55, 40,  4 },
	};

	/* Coefficients for vertical down-sampling */
	static const struct dispc_v_coef coef_vdown_3tap[8] = {
		{ 0, 36, 56, 36, 0 },
		{ 0, 40, 57, 31, 0 },
		{ 0, 45, 56, 27, 0 },
		{ 0, 50, 55, 23, 0 },
		{ 0, 18, 55, 55, 0 },
		{ 0, 23, 55, 50, 0 },
		{ 0, 27, 56, 45, 0 },
		{ 0, 31, 57, 40, 0 },
	};

	static const struct dispc_v_coef coef_vdown_5tap[8] = {
		{   0, 36, 56, 36,  0 },
		{   4, 40, 55, 31, -2 },
		{   8, 44, 54, 27, -5 },
		{  12, 48, 53, 22, -7 },
		{  -9, 17, 52, 51, 17 },
		{  -7, 22, 53, 48, 12 },
		{  -5, 27, 54, 44,  8 },
		{  -2, 31, 55, 40,  4 },
	};

	const struct dispc_h_coef *h_coef;
	const struct dispc_v_coef *v_coef;
	int i;

	if (hscaleup)
		h_coef = coef_hup;
	else
		h_coef = coef_hdown;

	if (vscaleup)
		v_coef = five_taps ? coef_vup_5tap : coef_vup_3tap;
	else
		v_coef = five_taps ? coef_vdown_5tap : coef_vdown_3tap;

	for (i = 0; i < 8; i++) {
		u32 h, hv;

		h = FLD_VAL(h_coef[i].hc0, 7, 0)
			| FLD_VAL(h_coef[i].hc1, 15, 8)
			| FLD_VAL(h_coef[i].hc2, 23, 16)
			| FLD_VAL(h_coef[i].hc3, 31, 24);
		hv = FLD_VAL(h_coef[i].hc4, 7, 0)
			| FLD_VAL(v_coef[i].vc0, 15, 8)
			| FLD_VAL(v_coef[i].vc1, 23, 16)
			| FLD_VAL(v_coef[i].vc2, 31, 24);

		if (color_comp == DISPC_COLOR_COMPONENT_RGB_Y) {
			_dispc_write_firh_reg(plane, i, h);
			_dispc_write_firhv_reg(plane, i, hv);
		} else {
			_dispc_write_firh2_reg(plane, i, h);
			_dispc_write_firhv2_reg(plane, i, hv);
		}

	}

	if (five_taps) {
		for (i = 0; i < 8; i++) {
			u32 v;
			v = FLD_VAL(v_coef[i].vc00, 7, 0)
				| FLD_VAL(v_coef[i].vc22, 15, 8);
			if (color_comp == DISPC_COLOR_COMPONENT_RGB_Y)
				_dispc_write_firv_reg(plane, i, v);
			else
				_dispc_write_firv2_reg(plane, i, v);
		}
	}
}

static void _dispc_setup_color_conv_coef(void)
{
	const struct color_conv_coef {
		int  ry,  rcr,  rcb,   gy,  gcr,  gcb,   by,  bcr,  bcb;
		int  full_range;
	}  ctbl_bt601_5 = {
		298,  409,    0,  298, -208, -100,  298,    0,  517, 0,
	};

	const struct color_conv_coef *ct;

#define CVAL(x, y) (FLD_VAL(x, 26, 16) | FLD_VAL(y, 10, 0))

	ct = &ctbl_bt601_5;

	dispc_write_reg(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO1, 0),
		CVAL(ct->rcr, ct->ry));
	dispc_write_reg(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO1, 1),
		CVAL(ct->gy,  ct->rcb));
	dispc_write_reg(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO1, 2),
		CVAL(ct->gcb, ct->gcr));
	dispc_write_reg(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO1, 3),
		CVAL(ct->bcr, ct->by));
	dispc_write_reg(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO1, 4),
		CVAL(0, ct->bcb));

	dispc_write_reg(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO2, 0),
		CVAL(ct->rcr, ct->ry));
	dispc_write_reg(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO2, 1),
		CVAL(ct->gy, ct->rcb));
	dispc_write_reg(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO2, 2),
		CVAL(ct->gcb, ct->gcr));
	dispc_write_reg(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO2, 3),
		CVAL(ct->bcr, ct->by));
	dispc_write_reg(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO2, 4),
		CVAL(0, ct->bcb));

#undef CVAL

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(OMAP_DSS_VIDEO1),
		ct->full_range, 11, 11);
	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(OMAP_DSS_VIDEO2),
		ct->full_range, 11, 11);
}


static void _dispc_set_plane_ba0(enum omap_plane plane, u32 paddr)
{
	dispc_write_reg(DISPC_OVL_BA0(plane), paddr);
}

static void _dispc_set_plane_ba1(enum omap_plane plane, u32 paddr)
{
	dispc_write_reg(DISPC_OVL_BA1(plane), paddr);
}

static void _dispc_set_plane_ba0_uv(enum omap_plane plane, u32 paddr)
{
	dispc_write_reg(DISPC_OVL_BA0_UV(plane), paddr);
}

static void _dispc_set_plane_ba1_uv(enum omap_plane plane, u32 paddr)
{
	dispc_write_reg(DISPC_OVL_BA1_UV(plane), paddr);
}

static void _dispc_set_plane_pos(enum omap_plane plane, int x, int y)
{
	u32 val = FLD_VAL(y, 26, 16) | FLD_VAL(x, 10, 0);

	dispc_write_reg(DISPC_OVL_POSITION(plane), val);
}

static void _dispc_set_pic_size(enum omap_plane plane, int width, int height)
{
	u32 val = FLD_VAL(height - 1, 26, 16) | FLD_VAL(width - 1, 10, 0);

	if (plane == OMAP_DSS_GFX)
		dispc_write_reg(DISPC_OVL_SIZE(plane), val);
	else
		dispc_write_reg(DISPC_OVL_PICTURE_SIZE(plane), val);
}

static void _dispc_set_vid_size(enum omap_plane plane, int width, int height)
{
	u32 val;

	BUG_ON(plane == OMAP_DSS_GFX);

	val = FLD_VAL(height - 1, 26, 16) | FLD_VAL(width - 1, 10, 0);

	dispc_write_reg(DISPC_OVL_SIZE(plane), val);
}

static void _dispc_set_pre_mult_alpha(enum omap_plane plane, bool enable)
{
	if (!dss_has_feature(FEAT_PRE_MULT_ALPHA))
		return;

	if (!dss_has_feature(FEAT_GLOBAL_ALPHA_VID1) &&
		plane == OMAP_DSS_VIDEO1)
		return;

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), enable ? 1 : 0, 28, 28);
}

static void _dispc_setup_global_alpha(enum omap_plane plane, u8 global_alpha)
{
	if (!dss_has_feature(FEAT_GLOBAL_ALPHA))
		return;

	if (!dss_has_feature(FEAT_GLOBAL_ALPHA_VID1) &&
		plane == OMAP_DSS_VIDEO1)
		return;

	if (plane == OMAP_DSS_GFX)
		REG_FLD_MOD(DISPC_GLOBAL_ALPHA, global_alpha, 7, 0);
	else if (plane == OMAP_DSS_VIDEO2)
		REG_FLD_MOD(DISPC_GLOBAL_ALPHA, global_alpha, 23, 16);
}

static void _dispc_set_pix_inc(enum omap_plane plane, s32 inc)
{
	dispc_write_reg(DISPC_OVL_PIXEL_INC(plane), inc);
}

static void _dispc_set_row_inc(enum omap_plane plane, s32 inc)
{
	dispc_write_reg(DISPC_OVL_ROW_INC(plane), inc);
}

static void _dispc_set_color_mode(enum omap_plane plane,
		enum omap_color_mode color_mode)
{
	u32 m = 0;
	if (plane != OMAP_DSS_GFX) {
		switch (color_mode) {
		case OMAP_DSS_COLOR_NV12:
			m = 0x0; break;
		case OMAP_DSS_COLOR_RGB12U:
			m = 0x1; break;
		case OMAP_DSS_COLOR_RGBA16:
			m = 0x2; break;
		case OMAP_DSS_COLOR_RGBX16:
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
			BUG(); break;
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
			BUG(); break;
		}
	}

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), m, 4, 1);
}

static void _dispc_set_channel_out(enum omap_plane plane,
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
		default:
			BUG();
		}

		val = FLD_MOD(val, chan, shift, shift);
		val = FLD_MOD(val, chan2, 31, 30);
	} else {
		val = FLD_MOD(val, channel, shift, shift);
	}
	dispc_write_reg(DISPC_OVL_ATTRIBUTES(plane), val);
}

void dispc_set_burst_size(enum omap_plane plane,
		enum omap_burst_size burst_size)
{
	int shift;
	u32 val;

	enable_clocks(1);

	switch (plane) {
	case OMAP_DSS_GFX:
		shift = 6;
		break;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		shift = 14;
		break;
	default:
		BUG();
		return;
	}

	val = dispc_read_reg(DISPC_OVL_ATTRIBUTES(plane));
	val = FLD_MOD(val, burst_size, shift+1, shift);
	dispc_write_reg(DISPC_OVL_ATTRIBUTES(plane), val);

	enable_clocks(0);
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

static void _dispc_set_vid_color_conv(enum omap_plane plane, bool enable)
{
	u32 val;

	BUG_ON(plane == OMAP_DSS_GFX);

	val = dispc_read_reg(DISPC_OVL_ATTRIBUTES(plane));
	val = FLD_MOD(val, enable, 9, 9);
	dispc_write_reg(DISPC_OVL_ATTRIBUTES(plane), val);
}

void dispc_enable_replication(enum omap_plane plane, bool enable)
{
	int bit;

	if (plane == OMAP_DSS_GFX)
		bit = 5;
	else
		bit = 10;

	enable_clocks(1);
	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), enable, bit, bit);
	enable_clocks(0);
}

void dispc_set_lcd_size(enum omap_channel channel, u16 width, u16 height)
{
	u32 val;
	BUG_ON((width > (1 << 11)) || (height > (1 << 11)));
	val = FLD_VAL(height - 1, 26, 16) | FLD_VAL(width - 1, 10, 0);
	enable_clocks(1);
	dispc_write_reg(DISPC_SIZE_MGR(channel), val);
	enable_clocks(0);
}

void dispc_set_digit_size(u16 width, u16 height)
{
	u32 val;
	BUG_ON((width > (1 << 11)) || (height > (1 << 11)));
	val = FLD_VAL(height - 1, 26, 16) | FLD_VAL(width - 1, 10, 0);
	enable_clocks(1);
	dispc_write_reg(DISPC_SIZE_MGR(OMAP_DSS_CHANNEL_DIGIT), val);
	enable_clocks(0);
}

static void dispc_read_plane_fifo_sizes(void)
{
	u32 size;
	int plane;
	u8 start, end;

	enable_clocks(1);

	dss_feat_get_reg_field(FEAT_REG_FIFOSIZE, &start, &end);

	for (plane = 0; plane < ARRAY_SIZE(dispc.fifo_size); ++plane) {
		size = FLD_GET(dispc_read_reg(DISPC_OVL_FIFO_SIZE_STATUS(plane)),
			start, end);
		dispc.fifo_size[plane] = size;
	}

	enable_clocks(0);
}

u32 dispc_get_plane_fifo_size(enum omap_plane plane)
{
	return dispc.fifo_size[plane];
}

void dispc_setup_plane_fifo(enum omap_plane plane, u32 low, u32 high)
{
	u8 hi_start, hi_end, lo_start, lo_end;

	dss_feat_get_reg_field(FEAT_REG_FIFOHIGHTHRESHOLD, &hi_start, &hi_end);
	dss_feat_get_reg_field(FEAT_REG_FIFOLOWTHRESHOLD, &lo_start, &lo_end);

	enable_clocks(1);

	DSSDBG("fifo(%d) low/high old %u/%u, new %u/%u\n",
			plane,
			REG_GET(DISPC_OVL_FIFO_THRESHOLD(plane),
				lo_start, lo_end),
			REG_GET(DISPC_OVL_FIFO_THRESHOLD(plane),
				hi_start, hi_end),
			low, high);

	dispc_write_reg(DISPC_OVL_FIFO_THRESHOLD(plane),
			FLD_VAL(high, hi_start, hi_end) |
			FLD_VAL(low, lo_start, lo_end));

	enable_clocks(0);
}

void dispc_enable_fifomerge(bool enable)
{
	enable_clocks(1);

	DSSDBG("FIFO merge %s\n", enable ? "enabled" : "disabled");
	REG_FLD_MOD(DISPC_CONFIG, enable ? 1 : 0, 14, 14);

	enable_clocks(0);
}

static void _dispc_set_fir(enum omap_plane plane,
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

static void _dispc_set_vid_accu0(enum omap_plane plane, int haccu, int vaccu)
{
	u32 val;
	u8 hor_start, hor_end, vert_start, vert_end;

	dss_feat_get_reg_field(FEAT_REG_HORIZONTALACCU, &hor_start, &hor_end);
	dss_feat_get_reg_field(FEAT_REG_VERTICALACCU, &vert_start, &vert_end);

	val = FLD_VAL(vaccu, vert_start, vert_end) |
			FLD_VAL(haccu, hor_start, hor_end);

	dispc_write_reg(DISPC_OVL_ACCU0(plane), val);
}

static void _dispc_set_vid_accu1(enum omap_plane plane, int haccu, int vaccu)
{
	u32 val;
	u8 hor_start, hor_end, vert_start, vert_end;

	dss_feat_get_reg_field(FEAT_REG_HORIZONTALACCU, &hor_start, &hor_end);
	dss_feat_get_reg_field(FEAT_REG_VERTICALACCU, &vert_start, &vert_end);

	val = FLD_VAL(vaccu, vert_start, vert_end) |
			FLD_VAL(haccu, hor_start, hor_end);

	dispc_write_reg(DISPC_OVL_ACCU1(plane), val);
}

static void _dispc_set_vid_accu2_0(enum omap_plane plane, int haccu, int vaccu)
{
	u32 val;

	val = FLD_VAL(vaccu, 26, 16) | FLD_VAL(haccu, 10, 0);
	dispc_write_reg(DISPC_OVL_ACCU2_0(plane), val);
}

static void _dispc_set_vid_accu2_1(enum omap_plane plane, int haccu, int vaccu)
{
	u32 val;

	val = FLD_VAL(vaccu, 26, 16) | FLD_VAL(haccu, 10, 0);
	dispc_write_reg(DISPC_OVL_ACCU2_1(plane), val);
}

static void _dispc_set_scale_param(enum omap_plane plane,
		u16 orig_width, u16 orig_height,
		u16 out_width, u16 out_height,
		bool five_taps, u8 rotation,
		enum omap_color_component color_comp)
{
	int fir_hinc, fir_vinc;
	int hscaleup, vscaleup;

	hscaleup = orig_width <= out_width;
	vscaleup = orig_height <= out_height;

	_dispc_set_scale_coef(plane, hscaleup, vscaleup, five_taps, color_comp);

	fir_hinc = 1024 * orig_width / out_width;
	fir_vinc = 1024 * orig_height / out_height;

	_dispc_set_fir(plane, fir_hinc, fir_vinc, color_comp);
}

static void _dispc_set_scaling_common(enum omap_plane plane,
		u16 orig_width, u16 orig_height,
		u16 out_width, u16 out_height,
		bool ilace, bool five_taps,
		bool fieldmode, enum omap_color_mode color_mode,
		u8 rotation)
{
	int accu0 = 0;
	int accu1 = 0;
	u32 l;

	_dispc_set_scale_param(plane, orig_width, orig_height,
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

	_dispc_set_vid_accu0(plane, 0, accu0);
	_dispc_set_vid_accu1(plane, 0, accu1);
}

static void _dispc_set_scaling_uv(enum omap_plane plane,
		u16 orig_width, u16 orig_height,
		u16 out_width, u16 out_height,
		bool ilace, bool five_taps,
		bool fieldmode, enum omap_color_mode color_mode,
		u8 rotation)
{
	int scale_x = out_width != orig_width;
	int scale_y = out_height != orig_height;

	if (!dss_has_feature(FEAT_HANDLE_UV_SEPARATE))
		return;
	if ((color_mode != OMAP_DSS_COLOR_YUV2 &&
			color_mode != OMAP_DSS_COLOR_UYVY &&
			color_mode != OMAP_DSS_COLOR_NV12)) {
		/* reset chroma resampling for RGB formats  */
		REG_FLD_MOD(DISPC_OVL_ATTRIBUTES2(plane), 0, 8, 8);
		return;
	}
	switch (color_mode) {
	case OMAP_DSS_COLOR_NV12:
		/* UV is subsampled by 2 vertically*/
		orig_height >>= 1;
		/* UV is subsampled by 2 horz.*/
		orig_width >>= 1;
		break;
	case OMAP_DSS_COLOR_YUV2:
	case OMAP_DSS_COLOR_UYVY:
		/*For YUV422 with 90/270 rotation,
		 *we don't upsample chroma
		 */
		if (rotation == OMAP_DSS_ROT_0 ||
			rotation == OMAP_DSS_ROT_180)
			/* UV is subsampled by 2 hrz*/
			orig_width >>= 1;
		/* must use FIR for YUV422 if rotated */
		if (rotation != OMAP_DSS_ROT_0)
			scale_x = scale_y = true;
		break;
	default:
		BUG();
	}

	if (out_width != orig_width)
		scale_x = true;
	if (out_height != orig_height)
		scale_y = true;

	_dispc_set_scale_param(plane, orig_width, orig_height,
			out_width, out_height, five_taps,
				rotation, DISPC_COLOR_COMPONENT_UV);

	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES2(plane),
		(scale_x || scale_y) ? 1 : 0, 8, 8);
	/* set H scaling */
	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), scale_x ? 1 : 0, 5, 5);
	/* set V scaling */
	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), scale_y ? 1 : 0, 6, 6);

	_dispc_set_vid_accu2_0(plane, 0x80, 0);
	_dispc_set_vid_accu2_1(plane, 0x80, 0);
}

static void _dispc_set_scaling(enum omap_plane plane,
		u16 orig_width, u16 orig_height,
		u16 out_width, u16 out_height,
		bool ilace, bool five_taps,
		bool fieldmode, enum omap_color_mode color_mode,
		u8 rotation)
{
	BUG_ON(plane == OMAP_DSS_GFX);

	_dispc_set_scaling_common(plane,
			orig_width, orig_height,
			out_width, out_height,
			ilace, five_taps,
			fieldmode, color_mode,
			rotation);

	_dispc_set_scaling_uv(plane,
		orig_width, orig_height,
		out_width, out_height,
		ilace, five_taps,
		fieldmode, color_mode,
		rotation);
}

static void _dispc_set_rotation_attrs(enum omap_plane plane, u8 rotation,
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
}

static void calc_vrfb_rotation_offset(u8 rotation, bool mirror,
		u16 screen_width,
		u16 width, u16 height,
		enum omap_color_mode color_mode, bool fieldmode,
		unsigned int field_offset,
		unsigned *offset0, unsigned *offset1,
		s32 *row_inc, s32 *pix_inc)
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

		*row_inc = pixinc(1 + (screen_width - width) +
				(fieldmode ? screen_width : 0),
				ps);
		*pix_inc = pixinc(1, ps);
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
		*row_inc = pixinc(1 - (screen_width + width) -
				(fieldmode ? screen_width : 0),
				ps);
		*pix_inc = pixinc(1, ps);
		break;

	default:
		BUG();
	}
}

static void calc_dma_rotation_offset(u8 rotation, bool mirror,
		u16 screen_width,
		u16 width, u16 height,
		enum omap_color_mode color_mode, bool fieldmode,
		unsigned int field_offset,
		unsigned *offset0, unsigned *offset1,
		s32 *row_inc, s32 *pix_inc)
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
		*row_inc = pixinc(1 + (screen_width - fbw) +
				(fieldmode ? screen_width : 0),
				ps);
		*pix_inc = pixinc(1, ps);
		break;
	case OMAP_DSS_ROT_90:
		*offset1 = screen_width * (fbh - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 + field_offset * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(screen_width * (fbh - 1) + 1 +
				(fieldmode ? 1 : 0), ps);
		*pix_inc = pixinc(-screen_width, ps);
		break;
	case OMAP_DSS_ROT_180:
		*offset1 = (screen_width * (fbh - 1) + fbw - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 - field_offset * screen_width * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(-1 -
				(screen_width - fbw) -
				(fieldmode ? screen_width : 0),
				ps);
		*pix_inc = pixinc(-1, ps);
		break;
	case OMAP_DSS_ROT_270:
		*offset1 = (fbw - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 - field_offset * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(-screen_width * (fbh - 1) - 1 -
				(fieldmode ? 1 : 0), ps);
		*pix_inc = pixinc(screen_width, ps);
		break;

	/* mirroring */
	case OMAP_DSS_ROT_0 + 4:
		*offset1 = (fbw - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 + field_offset * screen_width * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(screen_width * 2 - 1 +
				(fieldmode ? screen_width : 0),
				ps);
		*pix_inc = pixinc(-1, ps);
		break;

	case OMAP_DSS_ROT_90 + 4:
		*offset1 = 0;
		if (field_offset)
			*offset0 = *offset1 + field_offset * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(-screen_width * (fbh - 1) + 1 +
				(fieldmode ? 1 : 0),
				ps);
		*pix_inc = pixinc(screen_width, ps);
		break;

	case OMAP_DSS_ROT_180 + 4:
		*offset1 = screen_width * (fbh - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 - field_offset * screen_width * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(1 - screen_width * 2 -
				(fieldmode ? screen_width : 0),
				ps);
		*pix_inc = pixinc(1, ps);
		break;

	case OMAP_DSS_ROT_270 + 4:
		*offset1 = (screen_width * (fbh - 1) + fbw - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 - field_offset * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(screen_width * (fbh - 1) - 1 -
				(fieldmode ? 1 : 0),
				ps);
		*pix_inc = pixinc(-screen_width, ps);
		break;

	default:
		BUG();
	}
}

static unsigned long calc_fclk_five_taps(enum omap_channel channel, u16 width,
		u16 height, u16 out_width, u16 out_height,
		enum omap_color_mode color_mode)
{
	u32 fclk = 0;
	/* FIXME venc pclk? */
	u64 tmp, pclk = dispc_pclk_rate(channel);

	if (height > out_height) {
		/* FIXME get real display PPL */
		unsigned int ppl = 800;

		tmp = pclk * height * out_width;
		do_div(tmp, 2 * out_height * ppl);
		fclk = tmp;

		if (height > 2 * out_height) {
			if (ppl == out_width)
				return 0;

			tmp = pclk * (height - 2 * out_height) * out_width;
			do_div(tmp, 2 * out_height * (ppl - out_width));
			fclk = max(fclk, (u32) tmp);
		}
	}

	if (width > out_width) {
		tmp = pclk * width;
		do_div(tmp, out_width);
		fclk = max(fclk, (u32) tmp);

		if (color_mode == OMAP_DSS_COLOR_RGB24U)
			fclk <<= 1;
	}

	return fclk;
}

static unsigned long calc_fclk(enum omap_channel channel, u16 width,
		u16 height, u16 out_width, u16 out_height)
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

	/* FIXME venc pclk? */
	return dispc_pclk_rate(channel) * vf * hf;
}

void dispc_set_channel_out(enum omap_plane plane, enum omap_channel channel_out)
{
	enable_clocks(1);
	_dispc_set_channel_out(plane, channel_out);
	enable_clocks(0);
}

static int _dispc_setup_plane(enum omap_plane plane,
		u32 paddr, u16 screen_width,
		u16 pos_x, u16 pos_y,
		u16 width, u16 height,
		u16 out_width, u16 out_height,
		enum omap_color_mode color_mode,
		bool ilace,
		enum omap_dss_rotation_type rotation_type,
		u8 rotation, int mirror,
		u8 global_alpha, u8 pre_mult_alpha,
		enum omap_channel channel, u32 puv_addr)
{
	const int maxdownscale = cpu_is_omap34xx() ? 4 : 2;
	bool five_taps = 0;
	bool fieldmode = 0;
	int cconv = 0;
	unsigned offset0, offset1;
	s32 row_inc;
	s32 pix_inc;
	u16 frame_height = height;
	unsigned int field_offset = 0;

	if (paddr == 0)
		return -EINVAL;

	if (ilace && height == out_height)
		fieldmode = 1;

	if (ilace) {
		if (fieldmode)
			height /= 2;
		pos_y /= 2;
		out_height /= 2;

		DSSDBG("adjusting for ilace: height %d, pos_y %d, "
				"out_height %d\n",
				height, pos_y, out_height);
	}

	if (!dss_feat_color_mode_supported(plane, color_mode))
		return -EINVAL;

	if (plane == OMAP_DSS_GFX) {
		if (width != out_width || height != out_height)
			return -EINVAL;
	} else {
		/* video plane */

		unsigned long fclk = 0;

		if (out_width < width / maxdownscale ||
		   out_width > width * 8)
			return -EINVAL;

		if (out_height < height / maxdownscale ||
		   out_height > height * 8)
			return -EINVAL;

		if (color_mode == OMAP_DSS_COLOR_YUV2 ||
			color_mode == OMAP_DSS_COLOR_UYVY ||
			color_mode == OMAP_DSS_COLOR_NV12)
			cconv = 1;

		/* Must use 5-tap filter? */
		five_taps = height > out_height * 2;

		if (!five_taps) {
			fclk = calc_fclk(channel, width, height, out_width,
					out_height);

			/* Try 5-tap filter if 3-tap fclk is too high */
			if (cpu_is_omap34xx() && height > out_height &&
					fclk > dispc_fclk_rate())
				five_taps = true;
		}

		if (width > (2048 >> five_taps)) {
			DSSERR("failed to set up scaling, fclk too low\n");
			return -EINVAL;
		}

		if (five_taps)
			fclk = calc_fclk_five_taps(channel, width, height,
					out_width, out_height, color_mode);

		DSSDBG("required fclk rate = %lu Hz\n", fclk);
		DSSDBG("current fclk rate = %lu Hz\n", dispc_fclk_rate());

		if (!fclk || fclk > dispc_fclk_rate()) {
			DSSERR("failed to set up scaling, "
					"required fclk rate = %lu Hz, "
					"current fclk rate = %lu Hz\n",
					fclk, dispc_fclk_rate());
			return -EINVAL;
		}
	}

	if (ilace && !fieldmode) {
		/*
		 * when downscaling the bottom field may have to start several
		 * source lines below the top field. Unfortunately ACCUI
		 * registers will only hold the fractional part of the offset
		 * so the integer part must be added to the base address of the
		 * bottom field.
		 */
		if (!height || height == out_height)
			field_offset = 0;
		else
			field_offset = height / out_height / 2;
	}

	/* Fields are independent but interleaved in memory. */
	if (fieldmode)
		field_offset = 1;

	if (rotation_type == OMAP_DSS_ROT_DMA)
		calc_dma_rotation_offset(rotation, mirror,
				screen_width, width, frame_height, color_mode,
				fieldmode, field_offset,
				&offset0, &offset1, &row_inc, &pix_inc);
	else
		calc_vrfb_rotation_offset(rotation, mirror,
				screen_width, width, frame_height, color_mode,
				fieldmode, field_offset,
				&offset0, &offset1, &row_inc, &pix_inc);

	DSSDBG("offset0 %u, offset1 %u, row_inc %d, pix_inc %d\n",
			offset0, offset1, row_inc, pix_inc);

	_dispc_set_color_mode(plane, color_mode);

	_dispc_set_plane_ba0(plane, paddr + offset0);
	_dispc_set_plane_ba1(plane, paddr + offset1);

	if (OMAP_DSS_COLOR_NV12 == color_mode) {
		_dispc_set_plane_ba0_uv(plane, puv_addr + offset0);
		_dispc_set_plane_ba1_uv(plane, puv_addr + offset1);
	}


	_dispc_set_row_inc(plane, row_inc);
	_dispc_set_pix_inc(plane, pix_inc);

	DSSDBG("%d,%d %dx%d -> %dx%d\n", pos_x, pos_y, width, height,
			out_width, out_height);

	_dispc_set_plane_pos(plane, pos_x, pos_y);

	_dispc_set_pic_size(plane, width, height);

	if (plane != OMAP_DSS_GFX) {
		_dispc_set_scaling(plane, width, height,
				   out_width, out_height,
				   ilace, five_taps, fieldmode,
				   color_mode, rotation);
		_dispc_set_vid_size(plane, out_width, out_height);
		_dispc_set_vid_color_conv(plane, cconv);
	}

	_dispc_set_rotation_attrs(plane, rotation, mirror, color_mode);

	_dispc_set_pre_mult_alpha(plane, pre_mult_alpha);
	_dispc_setup_global_alpha(plane, global_alpha);

	return 0;
}

static void _dispc_enable_plane(enum omap_plane plane, bool enable)
{
	REG_FLD_MOD(DISPC_OVL_ATTRIBUTES(plane), enable ? 1 : 0, 0, 0);
}

static void dispc_disable_isr(void *data, u32 mask)
{
	struct completion *compl = data;
	complete(compl);
}

static void _enable_lcd_out(enum omap_channel channel, bool enable)
{
	if (channel == OMAP_DSS_CHANNEL_LCD2)
		REG_FLD_MOD(DISPC_CONTROL2, enable ? 1 : 0, 0, 0);
	else
		REG_FLD_MOD(DISPC_CONTROL, enable ? 1 : 0, 0, 0);
}

static void dispc_enable_lcd_out(enum omap_channel channel, bool enable)
{
	struct completion frame_done_completion;
	bool is_on;
	int r;
	u32 irq;

	enable_clocks(1);

	/* When we disable LCD output, we need to wait until frame is done.
	 * Otherwise the DSS is still working, and turning off the clocks
	 * prevents DSS from going to OFF mode */
	is_on = channel == OMAP_DSS_CHANNEL_LCD2 ?
			REG_GET(DISPC_CONTROL2, 0, 0) :
			REG_GET(DISPC_CONTROL, 0, 0);

	irq = channel == OMAP_DSS_CHANNEL_LCD2 ? DISPC_IRQ_FRAMEDONE2 :
			DISPC_IRQ_FRAMEDONE;

	if (!enable && is_on) {
		init_completion(&frame_done_completion);

		r = omap_dispc_register_isr(dispc_disable_isr,
				&frame_done_completion, irq);

		if (r)
			DSSERR("failed to register FRAMEDONE isr\n");
	}

	_enable_lcd_out(channel, enable);

	if (!enable && is_on) {
		if (!wait_for_completion_timeout(&frame_done_completion,
					msecs_to_jiffies(100)))
			DSSERR("timeout waiting for FRAME DONE\n");

		r = omap_dispc_unregister_isr(dispc_disable_isr,
				&frame_done_completion, irq);

		if (r)
			DSSERR("failed to unregister FRAMEDONE isr\n");
	}

	enable_clocks(0);
}

static void _enable_digit_out(bool enable)
{
	REG_FLD_MOD(DISPC_CONTROL, enable ? 1 : 0, 1, 1);
}

static void dispc_enable_digit_out(bool enable)
{
	struct completion frame_done_completion;
	int r;

	enable_clocks(1);

	if (REG_GET(DISPC_CONTROL, 1, 1) == enable) {
		enable_clocks(0);
		return;
	}

	if (enable) {
		unsigned long flags;
		/* When we enable digit output, we'll get an extra digit
		 * sync lost interrupt, that we need to ignore */
		spin_lock_irqsave(&dispc.irq_lock, flags);
		dispc.irq_error_mask &= ~DISPC_IRQ_SYNC_LOST_DIGIT;
		_omap_dispc_set_irqs();
		spin_unlock_irqrestore(&dispc.irq_lock, flags);
	}

	/* When we disable digit output, we need to wait until fields are done.
	 * Otherwise the DSS is still working, and turning off the clocks
	 * prevents DSS from going to OFF mode. And when enabling, we need to
	 * wait for the extra sync losts */
	init_completion(&frame_done_completion);

	r = omap_dispc_register_isr(dispc_disable_isr, &frame_done_completion,
			DISPC_IRQ_EVSYNC_EVEN | DISPC_IRQ_EVSYNC_ODD);
	if (r)
		DSSERR("failed to register EVSYNC isr\n");

	_enable_digit_out(enable);

	/* XXX I understand from TRM that we should only wait for the
	 * current field to complete. But it seems we have to wait
	 * for both fields */
	if (!wait_for_completion_timeout(&frame_done_completion,
				msecs_to_jiffies(100)))
		DSSERR("timeout waiting for EVSYNC\n");

	if (!wait_for_completion_timeout(&frame_done_completion,
				msecs_to_jiffies(100)))
		DSSERR("timeout waiting for EVSYNC\n");

	r = omap_dispc_unregister_isr(dispc_disable_isr,
			&frame_done_completion,
			DISPC_IRQ_EVSYNC_EVEN | DISPC_IRQ_EVSYNC_ODD);
	if (r)
		DSSERR("failed to unregister EVSYNC isr\n");

	if (enable) {
		unsigned long flags;
		spin_lock_irqsave(&dispc.irq_lock, flags);
		dispc.irq_error_mask = DISPC_IRQ_MASK_ERROR;
		if (dss_has_feature(FEAT_MGR_LCD2))
			dispc.irq_error_mask |= DISPC_IRQ_SYNC_LOST2;
		dispc_write_reg(DISPC_IRQSTATUS, DISPC_IRQ_SYNC_LOST_DIGIT);
		_omap_dispc_set_irqs();
		spin_unlock_irqrestore(&dispc.irq_lock, flags);
	}

	enable_clocks(0);
}

bool dispc_is_channel_enabled(enum omap_channel channel)
{
	if (channel == OMAP_DSS_CHANNEL_LCD)
		return !!REG_GET(DISPC_CONTROL, 0, 0);
	else if (channel == OMAP_DSS_CHANNEL_DIGIT)
		return !!REG_GET(DISPC_CONTROL, 1, 1);
	else if (channel == OMAP_DSS_CHANNEL_LCD2)
		return !!REG_GET(DISPC_CONTROL2, 0, 0);
	else
		BUG();
}

void dispc_enable_channel(enum omap_channel channel, bool enable)
{
	if (channel == OMAP_DSS_CHANNEL_LCD ||
			channel == OMAP_DSS_CHANNEL_LCD2)
		dispc_enable_lcd_out(channel, enable);
	else if (channel == OMAP_DSS_CHANNEL_DIGIT)
		dispc_enable_digit_out(enable);
	else
		BUG();
}

void dispc_lcd_enable_signal_polarity(bool act_high)
{
	if (!dss_has_feature(FEAT_LCDENABLEPOL))
		return;

	enable_clocks(1);
	REG_FLD_MOD(DISPC_CONTROL, act_high ? 1 : 0, 29, 29);
	enable_clocks(0);
}

void dispc_lcd_enable_signal(bool enable)
{
	if (!dss_has_feature(FEAT_LCDENABLESIGNAL))
		return;

	enable_clocks(1);
	REG_FLD_MOD(DISPC_CONTROL, enable ? 1 : 0, 28, 28);
	enable_clocks(0);
}

void dispc_pck_free_enable(bool enable)
{
	if (!dss_has_feature(FEAT_PCKFREEENABLE))
		return;

	enable_clocks(1);
	REG_FLD_MOD(DISPC_CONTROL, enable ? 1 : 0, 27, 27);
	enable_clocks(0);
}

void dispc_enable_fifohandcheck(enum omap_channel channel, bool enable)
{
	enable_clocks(1);
	if (channel == OMAP_DSS_CHANNEL_LCD2)
		REG_FLD_MOD(DISPC_CONFIG2, enable ? 1 : 0, 16, 16);
	else
		REG_FLD_MOD(DISPC_CONFIG, enable ? 1 : 0, 16, 16);
	enable_clocks(0);
}


void dispc_set_lcd_display_type(enum omap_channel channel,
		enum omap_lcd_display_type type)
{
	int mode;

	switch (type) {
	case OMAP_DSS_LCD_DISPLAY_STN:
		mode = 0;
		break;

	case OMAP_DSS_LCD_DISPLAY_TFT:
		mode = 1;
		break;

	default:
		BUG();
		return;
	}

	enable_clocks(1);
	if (channel == OMAP_DSS_CHANNEL_LCD2)
		REG_FLD_MOD(DISPC_CONTROL2, mode, 3, 3);
	else
		REG_FLD_MOD(DISPC_CONTROL, mode, 3, 3);
	enable_clocks(0);
}

void dispc_set_loadmode(enum omap_dss_load_mode mode)
{
	enable_clocks(1);
	REG_FLD_MOD(DISPC_CONFIG, mode, 2, 1);
	enable_clocks(0);
}


void dispc_set_default_color(enum omap_channel channel, u32 color)
{
	enable_clocks(1);
	dispc_write_reg(DISPC_DEFAULT_COLOR(channel), color);
	enable_clocks(0);
}

u32 dispc_get_default_color(enum omap_channel channel)
{
	u32 l;

	BUG_ON(channel != OMAP_DSS_CHANNEL_DIGIT &&
		channel != OMAP_DSS_CHANNEL_LCD &&
		channel != OMAP_DSS_CHANNEL_LCD2);

	enable_clocks(1);
	l = dispc_read_reg(DISPC_DEFAULT_COLOR(channel));
	enable_clocks(0);

	return l;
}

void dispc_set_trans_key(enum omap_channel ch,
		enum omap_dss_trans_key_type type,
		u32 trans_key)
{
	enable_clocks(1);
	if (ch == OMAP_DSS_CHANNEL_LCD)
		REG_FLD_MOD(DISPC_CONFIG, type, 11, 11);
	else if (ch == OMAP_DSS_CHANNEL_DIGIT)
		REG_FLD_MOD(DISPC_CONFIG, type, 13, 13);
	else /* OMAP_DSS_CHANNEL_LCD2 */
		REG_FLD_MOD(DISPC_CONFIG2, type, 11, 11);

	dispc_write_reg(DISPC_TRANS_COLOR(ch), trans_key);
	enable_clocks(0);
}

void dispc_get_trans_key(enum omap_channel ch,
		enum omap_dss_trans_key_type *type,
		u32 *trans_key)
{
	enable_clocks(1);
	if (type) {
		if (ch == OMAP_DSS_CHANNEL_LCD)
			*type = REG_GET(DISPC_CONFIG, 11, 11);
		else if (ch == OMAP_DSS_CHANNEL_DIGIT)
			*type = REG_GET(DISPC_CONFIG, 13, 13);
		else if (ch == OMAP_DSS_CHANNEL_LCD2)
			*type = REG_GET(DISPC_CONFIG2, 11, 11);
		else
			BUG();
	}

	if (trans_key)
		*trans_key = dispc_read_reg(DISPC_TRANS_COLOR(ch));
	enable_clocks(0);
}

void dispc_enable_trans_key(enum omap_channel ch, bool enable)
{
	enable_clocks(1);
	if (ch == OMAP_DSS_CHANNEL_LCD)
		REG_FLD_MOD(DISPC_CONFIG, enable, 10, 10);
	else if (ch == OMAP_DSS_CHANNEL_DIGIT)
		REG_FLD_MOD(DISPC_CONFIG, enable, 12, 12);
	else /* OMAP_DSS_CHANNEL_LCD2 */
		REG_FLD_MOD(DISPC_CONFIG2, enable, 10, 10);
	enable_clocks(0);
}
void dispc_enable_alpha_blending(enum omap_channel ch, bool enable)
{
	if (!dss_has_feature(FEAT_GLOBAL_ALPHA))
		return;

	enable_clocks(1);
	if (ch == OMAP_DSS_CHANNEL_LCD)
		REG_FLD_MOD(DISPC_CONFIG, enable, 18, 18);
	else if (ch == OMAP_DSS_CHANNEL_DIGIT)
		REG_FLD_MOD(DISPC_CONFIG, enable, 19, 19);
	else /* OMAP_DSS_CHANNEL_LCD2 */
		REG_FLD_MOD(DISPC_CONFIG2, enable, 18, 18);
	enable_clocks(0);
}
bool dispc_alpha_blending_enabled(enum omap_channel ch)
{
	bool enabled;

	if (!dss_has_feature(FEAT_GLOBAL_ALPHA))
		return false;

	enable_clocks(1);
	if (ch == OMAP_DSS_CHANNEL_LCD)
		enabled = REG_GET(DISPC_CONFIG, 18, 18);
	else if (ch == OMAP_DSS_CHANNEL_DIGIT)
		enabled = REG_GET(DISPC_CONFIG, 19, 19);
	else if (ch == OMAP_DSS_CHANNEL_LCD2)
		enabled = REG_GET(DISPC_CONFIG2, 18, 18);
	else
		BUG();
	enable_clocks(0);

	return enabled;
}


bool dispc_trans_key_enabled(enum omap_channel ch)
{
	bool enabled;

	enable_clocks(1);
	if (ch == OMAP_DSS_CHANNEL_LCD)
		enabled = REG_GET(DISPC_CONFIG, 10, 10);
	else if (ch == OMAP_DSS_CHANNEL_DIGIT)
		enabled = REG_GET(DISPC_CONFIG, 12, 12);
	else if (ch == OMAP_DSS_CHANNEL_LCD2)
		enabled = REG_GET(DISPC_CONFIG2, 10, 10);
	else
		BUG();
	enable_clocks(0);

	return enabled;
}


void dispc_set_tft_data_lines(enum omap_channel channel, u8 data_lines)
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

	enable_clocks(1);
	if (channel == OMAP_DSS_CHANNEL_LCD2)
		REG_FLD_MOD(DISPC_CONTROL2, code, 9, 8);
	else
		REG_FLD_MOD(DISPC_CONTROL, code, 9, 8);
	enable_clocks(0);
}

void dispc_set_parallel_interface_mode(enum omap_channel channel,
		enum omap_parallel_interface_mode mode)
{
	u32 l;
	int stallmode;
	int gpout0 = 1;
	int gpout1;

	switch (mode) {
	case OMAP_DSS_PARALLELMODE_BYPASS:
		stallmode = 0;
		gpout1 = 1;
		break;

	case OMAP_DSS_PARALLELMODE_RFBI:
		stallmode = 1;
		gpout1 = 0;
		break;

	case OMAP_DSS_PARALLELMODE_DSI:
		stallmode = 1;
		gpout1 = 1;
		break;

	default:
		BUG();
		return;
	}

	enable_clocks(1);

	if (channel == OMAP_DSS_CHANNEL_LCD2) {
		l = dispc_read_reg(DISPC_CONTROL2);
		l = FLD_MOD(l, stallmode, 11, 11);
		dispc_write_reg(DISPC_CONTROL2, l);
	} else {
		l = dispc_read_reg(DISPC_CONTROL);
		l = FLD_MOD(l, stallmode, 11, 11);
		l = FLD_MOD(l, gpout0, 15, 15);
		l = FLD_MOD(l, gpout1, 16, 16);
		dispc_write_reg(DISPC_CONTROL, l);
	}

	enable_clocks(0);
}

static bool _dispc_lcd_timings_ok(int hsw, int hfp, int hbp,
		int vsw, int vfp, int vbp)
{
	if (cpu_is_omap24xx() || omap_rev() < OMAP3430_REV_ES3_0) {
		if (hsw < 1 || hsw > 64 ||
				hfp < 1 || hfp > 256 ||
				hbp < 1 || hbp > 256 ||
				vsw < 1 || vsw > 64 ||
				vfp < 0 || vfp > 255 ||
				vbp < 0 || vbp > 255)
			return false;
	} else {
		if (hsw < 1 || hsw > 256 ||
				hfp < 1 || hfp > 4096 ||
				hbp < 1 || hbp > 4096 ||
				vsw < 1 || vsw > 256 ||
				vfp < 0 || vfp > 4095 ||
				vbp < 0 || vbp > 4095)
			return false;
	}

	return true;
}

bool dispc_lcd_timings_ok(struct omap_video_timings *timings)
{
	return _dispc_lcd_timings_ok(timings->hsw, timings->hfp,
			timings->hbp, timings->vsw,
			timings->vfp, timings->vbp);
}

static void _dispc_set_lcd_timings(enum omap_channel channel, int hsw,
		int hfp, int hbp, int vsw, int vfp, int vbp)
{
	u32 timing_h, timing_v;

	if (cpu_is_omap24xx() || omap_rev() < OMAP3430_REV_ES3_0) {
		timing_h = FLD_VAL(hsw-1, 5, 0) | FLD_VAL(hfp-1, 15, 8) |
			FLD_VAL(hbp-1, 27, 20);

		timing_v = FLD_VAL(vsw-1, 5, 0) | FLD_VAL(vfp, 15, 8) |
			FLD_VAL(vbp, 27, 20);
	} else {
		timing_h = FLD_VAL(hsw-1, 7, 0) | FLD_VAL(hfp-1, 19, 8) |
			FLD_VAL(hbp-1, 31, 20);

		timing_v = FLD_VAL(vsw-1, 7, 0) | FLD_VAL(vfp, 19, 8) |
			FLD_VAL(vbp, 31, 20);
	}

	enable_clocks(1);
	dispc_write_reg(DISPC_TIMING_H(channel), timing_h);
	dispc_write_reg(DISPC_TIMING_V(channel), timing_v);
	enable_clocks(0);
}

/* change name to mode? */
void dispc_set_lcd_timings(enum omap_channel channel,
		struct omap_video_timings *timings)
{
	unsigned xtot, ytot;
	unsigned long ht, vt;

	if (!_dispc_lcd_timings_ok(timings->hsw, timings->hfp,
				timings->hbp, timings->vsw,
				timings->vfp, timings->vbp))
		BUG();

	_dispc_set_lcd_timings(channel, timings->hsw, timings->hfp,
			timings->hbp, timings->vsw, timings->vfp,
			timings->vbp);

	dispc_set_lcd_size(channel, timings->x_res, timings->y_res);

	xtot = timings->x_res + timings->hfp + timings->hsw + timings->hbp;
	ytot = timings->y_res + timings->vfp + timings->vsw + timings->vbp;

	ht = (timings->pixel_clock * 1000) / xtot;
	vt = (timings->pixel_clock * 1000) / xtot / ytot;

	DSSDBG("channel %d xres %u yres %u\n", channel, timings->x_res,
			timings->y_res);
	DSSDBG("pck %u\n", timings->pixel_clock);
	DSSDBG("hsw %d hfp %d hbp %d vsw %d vfp %d vbp %d\n",
			timings->hsw, timings->hfp, timings->hbp,
			timings->vsw, timings->vfp, timings->vbp);

	DSSDBG("hsync %luHz, vsync %luHz\n", ht, vt);
}

static void dispc_set_lcd_divisor(enum omap_channel channel, u16 lck_div,
		u16 pck_div)
{
	BUG_ON(lck_div < 1);
	BUG_ON(pck_div < 2);

	enable_clocks(1);
	dispc_write_reg(DISPC_DIVISORo(channel),
			FLD_VAL(lck_div, 23, 16) | FLD_VAL(pck_div, 7, 0));
	enable_clocks(0);
}

static void dispc_get_lcd_divisor(enum omap_channel channel, int *lck_div,
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
		r = dss_clk_get_rate(DSS_CLK_FCK);
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
	}

	return r;
}

unsigned long dispc_lclk_rate(enum omap_channel channel)
{
	struct platform_device *dsidev;
	int lcd;
	unsigned long r;
	u32 l;

	l = dispc_read_reg(DISPC_DIVISORo(channel));

	lcd = FLD_GET(l, 23, 16);

	switch (dss_get_lcd_clk_source(channel)) {
	case OMAP_DSS_CLK_SRC_FCK:
		r = dss_clk_get_rate(DSS_CLK_FCK);
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
	}

	return r / lcd;
}

unsigned long dispc_pclk_rate(enum omap_channel channel)
{
	int pcd;
	unsigned long r;
	u32 l;

	l = dispc_read_reg(DISPC_DIVISORo(channel));

	pcd = FLD_GET(l, 7, 0);

	r = dispc_lclk_rate(channel);

	return r / pcd;
}

void dispc_dump_clocks(struct seq_file *s)
{
	int lcd, pcd;
	u32 l;
	enum omap_dss_clk_source dispc_clk_src = dss_get_dispc_clk_source();
	enum omap_dss_clk_source lcd_clk_src;

	enable_clocks(1);

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
	seq_printf(s, "- LCD1 -\n");

	lcd_clk_src = dss_get_lcd_clk_source(OMAP_DSS_CHANNEL_LCD);

	seq_printf(s, "lcd1_clk source = %s (%s)\n",
		dss_get_generic_clk_source_name(lcd_clk_src),
		dss_feat_get_clk_source_name(lcd_clk_src));

	dispc_get_lcd_divisor(OMAP_DSS_CHANNEL_LCD, &lcd, &pcd);

	seq_printf(s, "lck\t\t%-16lulck div\t%u\n",
			dispc_lclk_rate(OMAP_DSS_CHANNEL_LCD), lcd);
	seq_printf(s, "pck\t\t%-16lupck div\t%u\n",
			dispc_pclk_rate(OMAP_DSS_CHANNEL_LCD), pcd);
	if (dss_has_feature(FEAT_MGR_LCD2)) {
		seq_printf(s, "- LCD2 -\n");

		lcd_clk_src = dss_get_lcd_clk_source(OMAP_DSS_CHANNEL_LCD2);

		seq_printf(s, "lcd2_clk source = %s (%s)\n",
			dss_get_generic_clk_source_name(lcd_clk_src),
			dss_feat_get_clk_source_name(lcd_clk_src));

		dispc_get_lcd_divisor(OMAP_DSS_CHANNEL_LCD2, &lcd, &pcd);

		seq_printf(s, "lck\t\t%-16lulck div\t%u\n",
				dispc_lclk_rate(OMAP_DSS_CHANNEL_LCD2), lcd);
		seq_printf(s, "pck\t\t%-16lupck div\t%u\n",
				dispc_pclk_rate(OMAP_DSS_CHANNEL_LCD2), pcd);
	}
	enable_clocks(0);
}

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
void dispc_dump_irqs(struct seq_file *s)
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
	PIS(SYNC_LOST);
	PIS(SYNC_LOST_DIGIT);
	PIS(WAKEUP);
	if (dss_has_feature(FEAT_MGR_LCD2)) {
		PIS(FRAMEDONE2);
		PIS(VSYNC2);
		PIS(ACBIAS_COUNT_STAT2);
		PIS(SYNC_LOST2);
	}
#undef PIS
}
#endif

void dispc_dump_regs(struct seq_file *s)
{
#define DUMPREG(r) seq_printf(s, "%-50s %08x\n", #r, dispc_read_reg(r))

	dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK);

	DUMPREG(DISPC_REVISION);
	DUMPREG(DISPC_SYSCONFIG);
	DUMPREG(DISPC_SYSSTATUS);
	DUMPREG(DISPC_IRQSTATUS);
	DUMPREG(DISPC_IRQENABLE);
	DUMPREG(DISPC_CONTROL);
	DUMPREG(DISPC_CONFIG);
	DUMPREG(DISPC_CAPABLE);
	DUMPREG(DISPC_DEFAULT_COLOR(OMAP_DSS_CHANNEL_LCD));
	DUMPREG(DISPC_DEFAULT_COLOR(OMAP_DSS_CHANNEL_DIGIT));
	DUMPREG(DISPC_TRANS_COLOR(OMAP_DSS_CHANNEL_LCD));
	DUMPREG(DISPC_TRANS_COLOR(OMAP_DSS_CHANNEL_DIGIT));
	DUMPREG(DISPC_LINE_STATUS);
	DUMPREG(DISPC_LINE_NUMBER);
	DUMPREG(DISPC_TIMING_H(OMAP_DSS_CHANNEL_LCD));
	DUMPREG(DISPC_TIMING_V(OMAP_DSS_CHANNEL_LCD));
	DUMPREG(DISPC_POL_FREQ(OMAP_DSS_CHANNEL_LCD));
	DUMPREG(DISPC_DIVISORo(OMAP_DSS_CHANNEL_LCD));
	DUMPREG(DISPC_GLOBAL_ALPHA);
	DUMPREG(DISPC_SIZE_MGR(OMAP_DSS_CHANNEL_DIGIT));
	DUMPREG(DISPC_SIZE_MGR(OMAP_DSS_CHANNEL_LCD));
	if (dss_has_feature(FEAT_MGR_LCD2)) {
		DUMPREG(DISPC_CONTROL2);
		DUMPREG(DISPC_CONFIG2);
		DUMPREG(DISPC_DEFAULT_COLOR(OMAP_DSS_CHANNEL_LCD2));
		DUMPREG(DISPC_TRANS_COLOR(OMAP_DSS_CHANNEL_LCD2));
		DUMPREG(DISPC_TIMING_H(OMAP_DSS_CHANNEL_LCD2));
		DUMPREG(DISPC_TIMING_V(OMAP_DSS_CHANNEL_LCD2));
		DUMPREG(DISPC_POL_FREQ(OMAP_DSS_CHANNEL_LCD2));
		DUMPREG(DISPC_DIVISORo(OMAP_DSS_CHANNEL_LCD2));
		DUMPREG(DISPC_SIZE_MGR(OMAP_DSS_CHANNEL_LCD2));
	}

	DUMPREG(DISPC_OVL_BA0(OMAP_DSS_GFX));
	DUMPREG(DISPC_OVL_BA1(OMAP_DSS_GFX));
	DUMPREG(DISPC_OVL_POSITION(OMAP_DSS_GFX));
	DUMPREG(DISPC_OVL_SIZE(OMAP_DSS_GFX));
	DUMPREG(DISPC_OVL_ATTRIBUTES(OMAP_DSS_GFX));
	DUMPREG(DISPC_OVL_FIFO_THRESHOLD(OMAP_DSS_GFX));
	DUMPREG(DISPC_OVL_FIFO_SIZE_STATUS(OMAP_DSS_GFX));
	DUMPREG(DISPC_OVL_ROW_INC(OMAP_DSS_GFX));
	DUMPREG(DISPC_OVL_PIXEL_INC(OMAP_DSS_GFX));
	DUMPREG(DISPC_OVL_WINDOW_SKIP(OMAP_DSS_GFX));
	DUMPREG(DISPC_OVL_TABLE_BA(OMAP_DSS_GFX));

	DUMPREG(DISPC_DATA_CYCLE1(OMAP_DSS_CHANNEL_LCD));
	DUMPREG(DISPC_DATA_CYCLE2(OMAP_DSS_CHANNEL_LCD));
	DUMPREG(DISPC_DATA_CYCLE3(OMAP_DSS_CHANNEL_LCD));

	DUMPREG(DISPC_CPR_COEF_R(OMAP_DSS_CHANNEL_LCD));
	DUMPREG(DISPC_CPR_COEF_G(OMAP_DSS_CHANNEL_LCD));
	DUMPREG(DISPC_CPR_COEF_B(OMAP_DSS_CHANNEL_LCD));
	if (dss_has_feature(FEAT_MGR_LCD2)) {
		DUMPREG(DISPC_DATA_CYCLE1(OMAP_DSS_CHANNEL_LCD2));
		DUMPREG(DISPC_DATA_CYCLE2(OMAP_DSS_CHANNEL_LCD2));
		DUMPREG(DISPC_DATA_CYCLE3(OMAP_DSS_CHANNEL_LCD2));

		DUMPREG(DISPC_CPR_COEF_R(OMAP_DSS_CHANNEL_LCD2));
		DUMPREG(DISPC_CPR_COEF_G(OMAP_DSS_CHANNEL_LCD2));
		DUMPREG(DISPC_CPR_COEF_B(OMAP_DSS_CHANNEL_LCD2));
	}

	DUMPREG(DISPC_OVL_PRELOAD(OMAP_DSS_GFX));

	DUMPREG(DISPC_OVL_BA0(OMAP_DSS_VIDEO1));
	DUMPREG(DISPC_OVL_BA1(OMAP_DSS_VIDEO1));
	DUMPREG(DISPC_OVL_POSITION(OMAP_DSS_VIDEO1));
	DUMPREG(DISPC_OVL_SIZE(OMAP_DSS_VIDEO1));
	DUMPREG(DISPC_OVL_ATTRIBUTES(OMAP_DSS_VIDEO1));
	DUMPREG(DISPC_OVL_FIFO_THRESHOLD(OMAP_DSS_VIDEO1));
	DUMPREG(DISPC_OVL_FIFO_SIZE_STATUS(OMAP_DSS_VIDEO1));
	DUMPREG(DISPC_OVL_ROW_INC(OMAP_DSS_VIDEO1));
	DUMPREG(DISPC_OVL_PIXEL_INC(OMAP_DSS_VIDEO1));
	DUMPREG(DISPC_OVL_FIR(OMAP_DSS_VIDEO1));
	DUMPREG(DISPC_OVL_PICTURE_SIZE(OMAP_DSS_VIDEO1));
	DUMPREG(DISPC_OVL_ACCU0(OMAP_DSS_VIDEO1));
	DUMPREG(DISPC_OVL_ACCU1(OMAP_DSS_VIDEO1));

	DUMPREG(DISPC_OVL_BA0(OMAP_DSS_VIDEO2));
	DUMPREG(DISPC_OVL_BA1(OMAP_DSS_VIDEO2));
	DUMPREG(DISPC_OVL_POSITION(OMAP_DSS_VIDEO2));
	DUMPREG(DISPC_OVL_SIZE(OMAP_DSS_VIDEO2));
	DUMPREG(DISPC_OVL_ATTRIBUTES(OMAP_DSS_VIDEO2));
	DUMPREG(DISPC_OVL_FIFO_THRESHOLD(OMAP_DSS_VIDEO2));
	DUMPREG(DISPC_OVL_FIFO_SIZE_STATUS(OMAP_DSS_VIDEO2));
	DUMPREG(DISPC_OVL_ROW_INC(OMAP_DSS_VIDEO2));
	DUMPREG(DISPC_OVL_PIXEL_INC(OMAP_DSS_VIDEO2));
	DUMPREG(DISPC_OVL_FIR(OMAP_DSS_VIDEO2));
	DUMPREG(DISPC_OVL_PICTURE_SIZE(OMAP_DSS_VIDEO2));
	DUMPREG(DISPC_OVL_ACCU0(OMAP_DSS_VIDEO2));
	DUMPREG(DISPC_OVL_ACCU1(OMAP_DSS_VIDEO2));

	DUMPREG(DISPC_OVL_FIR_COEF_H(OMAP_DSS_VIDEO1, 0));
	DUMPREG(DISPC_OVL_FIR_COEF_H(OMAP_DSS_VIDEO1, 1));
	DUMPREG(DISPC_OVL_FIR_COEF_H(OMAP_DSS_VIDEO1, 2));
	DUMPREG(DISPC_OVL_FIR_COEF_H(OMAP_DSS_VIDEO1, 3));
	DUMPREG(DISPC_OVL_FIR_COEF_H(OMAP_DSS_VIDEO1, 4));
	DUMPREG(DISPC_OVL_FIR_COEF_H(OMAP_DSS_VIDEO1, 5));
	DUMPREG(DISPC_OVL_FIR_COEF_H(OMAP_DSS_VIDEO1, 6));
	DUMPREG(DISPC_OVL_FIR_COEF_H(OMAP_DSS_VIDEO1, 7));
	DUMPREG(DISPC_OVL_FIR_COEF_HV(OMAP_DSS_VIDEO1, 0));
	DUMPREG(DISPC_OVL_FIR_COEF_HV(OMAP_DSS_VIDEO1, 1));
	DUMPREG(DISPC_OVL_FIR_COEF_HV(OMAP_DSS_VIDEO1, 2));
	DUMPREG(DISPC_OVL_FIR_COEF_HV(OMAP_DSS_VIDEO1, 3));
	DUMPREG(DISPC_OVL_FIR_COEF_HV(OMAP_DSS_VIDEO1, 4));
	DUMPREG(DISPC_OVL_FIR_COEF_HV(OMAP_DSS_VIDEO1, 5));
	DUMPREG(DISPC_OVL_FIR_COEF_HV(OMAP_DSS_VIDEO1, 6));
	DUMPREG(DISPC_OVL_FIR_COEF_HV(OMAP_DSS_VIDEO1, 7));
	DUMPREG(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO1, 0));
	DUMPREG(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO1, 1));
	DUMPREG(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO1, 2));
	DUMPREG(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO1, 3));
	DUMPREG(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO1, 4));
	DUMPREG(DISPC_OVL_FIR_COEF_V(OMAP_DSS_VIDEO1, 0));
	DUMPREG(DISPC_OVL_FIR_COEF_V(OMAP_DSS_VIDEO1, 1));
	DUMPREG(DISPC_OVL_FIR_COEF_V(OMAP_DSS_VIDEO1, 2));
	DUMPREG(DISPC_OVL_FIR_COEF_V(OMAP_DSS_VIDEO1, 3));
	DUMPREG(DISPC_OVL_FIR_COEF_V(OMAP_DSS_VIDEO1, 4));
	DUMPREG(DISPC_OVL_FIR_COEF_V(OMAP_DSS_VIDEO1, 5));
	DUMPREG(DISPC_OVL_FIR_COEF_V(OMAP_DSS_VIDEO1, 6));
	DUMPREG(DISPC_OVL_FIR_COEF_V(OMAP_DSS_VIDEO1, 7));

	if (dss_has_feature(FEAT_HANDLE_UV_SEPARATE)) {
		DUMPREG(DISPC_OVL_BA0_UV(OMAP_DSS_VIDEO1));
		DUMPREG(DISPC_OVL_BA1_UV(OMAP_DSS_VIDEO1));
		DUMPREG(DISPC_OVL_FIR2(OMAP_DSS_VIDEO1));
		DUMPREG(DISPC_OVL_ACCU2_0(OMAP_DSS_VIDEO1));
		DUMPREG(DISPC_OVL_ACCU2_1(OMAP_DSS_VIDEO1));

		DUMPREG(DISPC_OVL_FIR_COEF_H2(OMAP_DSS_VIDEO1, 0));
		DUMPREG(DISPC_OVL_FIR_COEF_H2(OMAP_DSS_VIDEO1, 1));
		DUMPREG(DISPC_OVL_FIR_COEF_H2(OMAP_DSS_VIDEO1, 2));
		DUMPREG(DISPC_OVL_FIR_COEF_H2(OMAP_DSS_VIDEO1, 3));
		DUMPREG(DISPC_OVL_FIR_COEF_H2(OMAP_DSS_VIDEO1, 4));
		DUMPREG(DISPC_OVL_FIR_COEF_H2(OMAP_DSS_VIDEO1, 5));
		DUMPREG(DISPC_OVL_FIR_COEF_H2(OMAP_DSS_VIDEO1, 6));
		DUMPREG(DISPC_OVL_FIR_COEF_H2(OMAP_DSS_VIDEO1, 7));

		DUMPREG(DISPC_OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO1, 0));
		DUMPREG(DISPC_OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO1, 1));
		DUMPREG(DISPC_OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO1, 2));
		DUMPREG(DISPC_OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO1, 3));
		DUMPREG(DISPC_OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO1, 4));
		DUMPREG(DISPC_OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO1, 5));
		DUMPREG(DISPC_OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO1, 6));
		DUMPREG(DISPC_OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO1, 7));

		DUMPREG(DISPC_OVL_FIR_COEF_V2(OMAP_DSS_VIDEO1, 0));
		DUMPREG(DISPC_OVL_FIR_COEF_V2(OMAP_DSS_VIDEO1, 1));
		DUMPREG(DISPC_OVL_FIR_COEF_V2(OMAP_DSS_VIDEO1, 2));
		DUMPREG(DISPC_OVL_FIR_COEF_V2(OMAP_DSS_VIDEO1, 3));
		DUMPREG(DISPC_OVL_FIR_COEF_V2(OMAP_DSS_VIDEO1, 4));
		DUMPREG(DISPC_OVL_FIR_COEF_V2(OMAP_DSS_VIDEO1, 5));
		DUMPREG(DISPC_OVL_FIR_COEF_V2(OMAP_DSS_VIDEO1, 6));
		DUMPREG(DISPC_OVL_FIR_COEF_V2(OMAP_DSS_VIDEO1, 7));
	}
	if (dss_has_feature(FEAT_ATTR2))
		DUMPREG(DISPC_OVL_ATTRIBUTES2(OMAP_DSS_VIDEO1));


	DUMPREG(DISPC_OVL_FIR_COEF_H(OMAP_DSS_VIDEO2, 0));
	DUMPREG(DISPC_OVL_FIR_COEF_H(OMAP_DSS_VIDEO2, 1));
	DUMPREG(DISPC_OVL_FIR_COEF_H(OMAP_DSS_VIDEO2, 2));
	DUMPREG(DISPC_OVL_FIR_COEF_H(OMAP_DSS_VIDEO2, 3));
	DUMPREG(DISPC_OVL_FIR_COEF_H(OMAP_DSS_VIDEO2, 4));
	DUMPREG(DISPC_OVL_FIR_COEF_H(OMAP_DSS_VIDEO2, 5));
	DUMPREG(DISPC_OVL_FIR_COEF_H(OMAP_DSS_VIDEO2, 6));
	DUMPREG(DISPC_OVL_FIR_COEF_H(OMAP_DSS_VIDEO2, 7));
	DUMPREG(DISPC_OVL_FIR_COEF_HV(OMAP_DSS_VIDEO2, 0));
	DUMPREG(DISPC_OVL_FIR_COEF_HV(OMAP_DSS_VIDEO2, 1));
	DUMPREG(DISPC_OVL_FIR_COEF_HV(OMAP_DSS_VIDEO2, 2));
	DUMPREG(DISPC_OVL_FIR_COEF_HV(OMAP_DSS_VIDEO2, 3));
	DUMPREG(DISPC_OVL_FIR_COEF_HV(OMAP_DSS_VIDEO2, 4));
	DUMPREG(DISPC_OVL_FIR_COEF_HV(OMAP_DSS_VIDEO2, 5));
	DUMPREG(DISPC_OVL_FIR_COEF_HV(OMAP_DSS_VIDEO2, 6));
	DUMPREG(DISPC_OVL_FIR_COEF_HV(OMAP_DSS_VIDEO2, 7));
	DUMPREG(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO2, 0));
	DUMPREG(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO2, 1));
	DUMPREG(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO2, 2));
	DUMPREG(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO2, 3));
	DUMPREG(DISPC_OVL_CONV_COEF(OMAP_DSS_VIDEO2, 4));
	DUMPREG(DISPC_OVL_FIR_COEF_V(OMAP_DSS_VIDEO2, 0));
	DUMPREG(DISPC_OVL_FIR_COEF_V(OMAP_DSS_VIDEO2, 1));
	DUMPREG(DISPC_OVL_FIR_COEF_V(OMAP_DSS_VIDEO2, 2));
	DUMPREG(DISPC_OVL_FIR_COEF_V(OMAP_DSS_VIDEO2, 3));
	DUMPREG(DISPC_OVL_FIR_COEF_V(OMAP_DSS_VIDEO2, 4));
	DUMPREG(DISPC_OVL_FIR_COEF_V(OMAP_DSS_VIDEO2, 5));
	DUMPREG(DISPC_OVL_FIR_COEF_V(OMAP_DSS_VIDEO2, 6));
	DUMPREG(DISPC_OVL_FIR_COEF_V(OMAP_DSS_VIDEO2, 7));

	if (dss_has_feature(FEAT_HANDLE_UV_SEPARATE)) {
		DUMPREG(DISPC_OVL_BA0_UV(OMAP_DSS_VIDEO2));
		DUMPREG(DISPC_OVL_BA1_UV(OMAP_DSS_VIDEO2));
		DUMPREG(DISPC_OVL_FIR2(OMAP_DSS_VIDEO2));
		DUMPREG(DISPC_OVL_ACCU2_0(OMAP_DSS_VIDEO2));
		DUMPREG(DISPC_OVL_ACCU2_1(OMAP_DSS_VIDEO2));

		DUMPREG(DISPC_OVL_FIR_COEF_H2(OMAP_DSS_VIDEO2, 0));
		DUMPREG(DISPC_OVL_FIR_COEF_H2(OMAP_DSS_VIDEO2, 1));
		DUMPREG(DISPC_OVL_FIR_COEF_H2(OMAP_DSS_VIDEO2, 2));
		DUMPREG(DISPC_OVL_FIR_COEF_H2(OMAP_DSS_VIDEO2, 3));
		DUMPREG(DISPC_OVL_FIR_COEF_H2(OMAP_DSS_VIDEO2, 4));
		DUMPREG(DISPC_OVL_FIR_COEF_H2(OMAP_DSS_VIDEO2, 5));
		DUMPREG(DISPC_OVL_FIR_COEF_H2(OMAP_DSS_VIDEO2, 6));
		DUMPREG(DISPC_OVL_FIR_COEF_H2(OMAP_DSS_VIDEO2, 7));

		DUMPREG(DISPC_OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO2, 0));
		DUMPREG(DISPC_OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO2, 1));
		DUMPREG(DISPC_OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO2, 2));
		DUMPREG(DISPC_OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO2, 3));
		DUMPREG(DISPC_OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO2, 4));
		DUMPREG(DISPC_OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO2, 5));
		DUMPREG(DISPC_OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO2, 6));
		DUMPREG(DISPC_OVL_FIR_COEF_HV2(OMAP_DSS_VIDEO2, 7));

		DUMPREG(DISPC_OVL_FIR_COEF_V2(OMAP_DSS_VIDEO2, 0));
		DUMPREG(DISPC_OVL_FIR_COEF_V2(OMAP_DSS_VIDEO2, 1));
		DUMPREG(DISPC_OVL_FIR_COEF_V2(OMAP_DSS_VIDEO2, 2));
		DUMPREG(DISPC_OVL_FIR_COEF_V2(OMAP_DSS_VIDEO2, 3));
		DUMPREG(DISPC_OVL_FIR_COEF_V2(OMAP_DSS_VIDEO2, 4));
		DUMPREG(DISPC_OVL_FIR_COEF_V2(OMAP_DSS_VIDEO2, 5));
		DUMPREG(DISPC_OVL_FIR_COEF_V2(OMAP_DSS_VIDEO2, 6));
		DUMPREG(DISPC_OVL_FIR_COEF_V2(OMAP_DSS_VIDEO2, 7));
	}
	if (dss_has_feature(FEAT_ATTR2))
		DUMPREG(DISPC_OVL_ATTRIBUTES2(OMAP_DSS_VIDEO2));

	DUMPREG(DISPC_OVL_PRELOAD(OMAP_DSS_VIDEO1));
	DUMPREG(DISPC_OVL_PRELOAD(OMAP_DSS_VIDEO2));

	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK);
#undef DUMPREG
}

static void _dispc_set_pol_freq(enum omap_channel channel, bool onoff, bool rf,
		bool ieo, bool ipc, bool ihs, bool ivs, u8 acbi, u8 acb)
{
	u32 l = 0;

	DSSDBG("onoff %d rf %d ieo %d ipc %d ihs %d ivs %d acbi %d acb %d\n",
			onoff, rf, ieo, ipc, ihs, ivs, acbi, acb);

	l |= FLD_VAL(onoff, 17, 17);
	l |= FLD_VAL(rf, 16, 16);
	l |= FLD_VAL(ieo, 15, 15);
	l |= FLD_VAL(ipc, 14, 14);
	l |= FLD_VAL(ihs, 13, 13);
	l |= FLD_VAL(ivs, 12, 12);
	l |= FLD_VAL(acbi, 11, 8);
	l |= FLD_VAL(acb, 7, 0);

	enable_clocks(1);
	dispc_write_reg(DISPC_POL_FREQ(channel), l);
	enable_clocks(0);
}

void dispc_set_pol_freq(enum omap_channel channel,
		enum omap_panel_config config, u8 acbi, u8 acb)
{
	_dispc_set_pol_freq(channel, (config & OMAP_DSS_LCD_ONOFF) != 0,
			(config & OMAP_DSS_LCD_RF) != 0,
			(config & OMAP_DSS_LCD_IEO) != 0,
			(config & OMAP_DSS_LCD_IPC) != 0,
			(config & OMAP_DSS_LCD_IHS) != 0,
			(config & OMAP_DSS_LCD_IVS) != 0,
			acbi, acb);
}

/* with fck as input clock rate, find dispc dividers that produce req_pck */
void dispc_find_clk_divs(bool is_tft, unsigned long req_pck, unsigned long fck,
		struct dispc_clock_info *cinfo)
{
	u16 pcd_min = is_tft ? 2 : 3;
	unsigned long best_pck;
	u16 best_ld, cur_ld;
	u16 best_pd, cur_pd;

	best_pck = 0;
	best_ld = 0;
	best_pd = 0;

	for (cur_ld = 1; cur_ld <= 255; ++cur_ld) {
		unsigned long lck = fck / cur_ld;

		for (cur_pd = pcd_min; cur_pd <= 255; ++cur_pd) {
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
	if (cinfo->pck_div < 2 || cinfo->pck_div > 255)
		return -EINVAL;

	cinfo->lck = dispc_fclk_rate / cinfo->lck_div;
	cinfo->pck = cinfo->lck / cinfo->pck_div;

	return 0;
}

int dispc_set_clock_div(enum omap_channel channel,
		struct dispc_clock_info *cinfo)
{
	DSSDBG("lck = %lu (%u)\n", cinfo->lck, cinfo->lck_div);
	DSSDBG("pck = %lu (%u)\n", cinfo->pck, cinfo->pck_div);

	dispc_set_lcd_divisor(channel, cinfo->lck_div, cinfo->pck_div);

	return 0;
}

int dispc_get_clock_div(enum omap_channel channel,
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

/* dispc.irq_lock has to be locked by the caller */
static void _omap_dispc_set_irqs(void)
{
	u32 mask;
	u32 old_mask;
	int i;
	struct omap_dispc_isr_data *isr_data;

	mask = dispc.irq_error_mask;

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc.registered_isr[i];

		if (isr_data->isr == NULL)
			continue;

		mask |= isr_data->mask;
	}

	enable_clocks(1);

	old_mask = dispc_read_reg(DISPC_IRQENABLE);
	/* clear the irqstatus for newly enabled irqs */
	dispc_write_reg(DISPC_IRQSTATUS, (mask ^ old_mask) & mask);

	dispc_write_reg(DISPC_IRQENABLE, mask);

	enable_clocks(0);
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

#ifdef DEBUG
static void print_irq_status(u32 status)
{
	if ((status & dispc.irq_error_mask) == 0)
		return;

	printk(KERN_DEBUG "DISPC IRQ: 0x%x: ", status);

#define PIS(x) \
	if (status & DISPC_IRQ_##x) \
		printk(#x " ");
	PIS(GFX_FIFO_UNDERFLOW);
	PIS(OCP_ERR);
	PIS(VID1_FIFO_UNDERFLOW);
	PIS(VID2_FIFO_UNDERFLOW);
	PIS(SYNC_LOST);
	PIS(SYNC_LOST_DIGIT);
	if (dss_has_feature(FEAT_MGR_LCD2))
		PIS(SYNC_LOST2);
#undef PIS

	printk("\n");
}
#endif

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

	irqstatus = dispc_read_reg(DISPC_IRQSTATUS);
	irqenable = dispc_read_reg(DISPC_IRQENABLE);

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

#ifdef DEBUG
	if (dss_debug)
		print_irq_status(irqstatus);
#endif
	/* Ack the interrupt. Do it here before clocks are possibly turned
	 * off */
	dispc_write_reg(DISPC_IRQSTATUS, irqstatus);
	/* flush posted write */
	dispc_read_reg(DISPC_IRQSTATUS);

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

	spin_lock_irqsave(&dispc.irq_lock, flags);
	errors = dispc.error_irqs;
	dispc.error_irqs = 0;
	spin_unlock_irqrestore(&dispc.irq_lock, flags);

	if (errors & DISPC_IRQ_GFX_FIFO_UNDERFLOW) {
		DSSERR("GFX_FIFO_UNDERFLOW, disabling GFX\n");
		for (i = 0; i < omap_dss_get_num_overlays(); ++i) {
			struct omap_overlay *ovl;
			ovl = omap_dss_get_overlay(i);

			if (!(ovl->caps & OMAP_DSS_OVL_CAP_DISPC))
				continue;

			if (ovl->id == 0) {
				dispc_enable_plane(ovl->id, 0);
				dispc_go(ovl->manager->id);
				mdelay(50);
				break;
			}
		}
	}

	if (errors & DISPC_IRQ_VID1_FIFO_UNDERFLOW) {
		DSSERR("VID1_FIFO_UNDERFLOW, disabling VID1\n");
		for (i = 0; i < omap_dss_get_num_overlays(); ++i) {
			struct omap_overlay *ovl;
			ovl = omap_dss_get_overlay(i);

			if (!(ovl->caps & OMAP_DSS_OVL_CAP_DISPC))
				continue;

			if (ovl->id == 1) {
				dispc_enable_plane(ovl->id, 0);
				dispc_go(ovl->manager->id);
				mdelay(50);
				break;
			}
		}
	}

	if (errors & DISPC_IRQ_VID2_FIFO_UNDERFLOW) {
		DSSERR("VID2_FIFO_UNDERFLOW, disabling VID2\n");
		for (i = 0; i < omap_dss_get_num_overlays(); ++i) {
			struct omap_overlay *ovl;
			ovl = omap_dss_get_overlay(i);

			if (!(ovl->caps & OMAP_DSS_OVL_CAP_DISPC))
				continue;

			if (ovl->id == 2) {
				dispc_enable_plane(ovl->id, 0);
				dispc_go(ovl->manager->id);
				mdelay(50);
				break;
			}
		}
	}

	if (errors & DISPC_IRQ_SYNC_LOST) {
		struct omap_overlay_manager *manager = NULL;
		bool enable = false;

		DSSERR("SYNC_LOST, disabling LCD\n");

		for (i = 0; i < omap_dss_get_num_overlay_managers(); ++i) {
			struct omap_overlay_manager *mgr;
			mgr = omap_dss_get_overlay_manager(i);

			if (mgr->id == OMAP_DSS_CHANNEL_LCD) {
				manager = mgr;
				enable = mgr->device->state ==
						OMAP_DSS_DISPLAY_ACTIVE;
				mgr->device->driver->disable(mgr->device);
				break;
			}
		}

		if (manager) {
			struct omap_dss_device *dssdev = manager->device;
			for (i = 0; i < omap_dss_get_num_overlays(); ++i) {
				struct omap_overlay *ovl;
				ovl = omap_dss_get_overlay(i);

				if (!(ovl->caps & OMAP_DSS_OVL_CAP_DISPC))
					continue;

				if (ovl->id != 0 && ovl->manager == manager)
					dispc_enable_plane(ovl->id, 0);
			}

			dispc_go(manager->id);
			mdelay(50);
			if (enable)
				dssdev->driver->enable(dssdev);
		}
	}

	if (errors & DISPC_IRQ_SYNC_LOST_DIGIT) {
		struct omap_overlay_manager *manager = NULL;
		bool enable = false;

		DSSERR("SYNC_LOST_DIGIT, disabling TV\n");

		for (i = 0; i < omap_dss_get_num_overlay_managers(); ++i) {
			struct omap_overlay_manager *mgr;
			mgr = omap_dss_get_overlay_manager(i);

			if (mgr->id == OMAP_DSS_CHANNEL_DIGIT) {
				manager = mgr;
				enable = mgr->device->state ==
						OMAP_DSS_DISPLAY_ACTIVE;
				mgr->device->driver->disable(mgr->device);
				break;
			}
		}

		if (manager) {
			struct omap_dss_device *dssdev = manager->device;
			for (i = 0; i < omap_dss_get_num_overlays(); ++i) {
				struct omap_overlay *ovl;
				ovl = omap_dss_get_overlay(i);

				if (!(ovl->caps & OMAP_DSS_OVL_CAP_DISPC))
					continue;

				if (ovl->id != 0 && ovl->manager == manager)
					dispc_enable_plane(ovl->id, 0);
			}

			dispc_go(manager->id);
			mdelay(50);
			if (enable)
				dssdev->driver->enable(dssdev);
		}
	}

	if (errors & DISPC_IRQ_SYNC_LOST2) {
		struct omap_overlay_manager *manager = NULL;
		bool enable = false;

		DSSERR("SYNC_LOST for LCD2, disabling LCD2\n");

		for (i = 0; i < omap_dss_get_num_overlay_managers(); ++i) {
			struct omap_overlay_manager *mgr;
			mgr = omap_dss_get_overlay_manager(i);

			if (mgr->id == OMAP_DSS_CHANNEL_LCD2) {
				manager = mgr;
				enable = mgr->device->state ==
						OMAP_DSS_DISPLAY_ACTIVE;
				mgr->device->driver->disable(mgr->device);
				break;
			}
		}

		if (manager) {
			struct omap_dss_device *dssdev = manager->device;
			for (i = 0; i < omap_dss_get_num_overlays(); ++i) {
				struct omap_overlay *ovl;
				ovl = omap_dss_get_overlay(i);

				if (!(ovl->caps & OMAP_DSS_OVL_CAP_DISPC))
					continue;

				if (ovl->id != 0 && ovl->manager == manager)
					dispc_enable_plane(ovl->id, 0);
			}

			dispc_go(manager->id);
			mdelay(50);
			if (enable)
				dssdev->driver->enable(dssdev);
		}
	}

	if (errors & DISPC_IRQ_OCP_ERR) {
		DSSERR("OCP_ERR\n");
		for (i = 0; i < omap_dss_get_num_overlay_managers(); ++i) {
			struct omap_overlay_manager *mgr;
			mgr = omap_dss_get_overlay_manager(i);

			if (mgr->caps & OMAP_DSS_OVL_CAP_DISPC)
				mgr->device->driver->disable(mgr->device);
		}
	}

	spin_lock_irqsave(&dispc.irq_lock, flags);
	dispc.irq_error_mask |= errors;
	_omap_dispc_set_irqs();
	spin_unlock_irqrestore(&dispc.irq_lock, flags);
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

	if (timeout == -ERESTARTSYS)
		return -ERESTARTSYS;

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

#ifdef CONFIG_OMAP2_DSS_FAKE_VSYNC
void dispc_fake_vsync_irq(void)
{
	u32 irqstatus = DISPC_IRQ_VSYNC;
	int i;

	WARN_ON(!in_interrupt());

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		struct omap_dispc_isr_data *isr_data;
		isr_data = &dispc.registered_isr[i];

		if (!isr_data->isr)
			continue;

		if (isr_data->mask & irqstatus)
			isr_data->isr(isr_data->arg, irqstatus);
	}
}
#endif

static void _omap_dispc_initialize_irq(void)
{
	unsigned long flags;

	spin_lock_irqsave(&dispc.irq_lock, flags);

	memset(dispc.registered_isr, 0, sizeof(dispc.registered_isr));

	dispc.irq_error_mask = DISPC_IRQ_MASK_ERROR;
	if (dss_has_feature(FEAT_MGR_LCD2))
		dispc.irq_error_mask |= DISPC_IRQ_SYNC_LOST2;

	/* there's SYNC_LOST_DIGIT waiting after enabling the DSS,
	 * so clear it */
	dispc_write_reg(DISPC_IRQSTATUS, dispc_read_reg(DISPC_IRQSTATUS));

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

	l = dispc_read_reg(DISPC_SYSCONFIG);
	l = FLD_MOD(l, 2, 13, 12);	/* MIDLEMODE: smart standby */
	l = FLD_MOD(l, 2, 4, 3);	/* SIDLEMODE: smart idle */
	l = FLD_MOD(l, 1, 2, 2);	/* ENWAKEUP */
	l = FLD_MOD(l, 1, 0, 0);	/* AUTOIDLE */
	dispc_write_reg(DISPC_SYSCONFIG, l);

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

	/* L3 firewall setting: enable access to OCM RAM */
	/* XXX this should be somewhere in plat-omap */
	if (cpu_is_omap24xx())
		__raw_writel(0x402000b0, OMAP2_L3_IO_ADDRESS(0x680050a0));

	_dispc_setup_color_conv_coef();

	dispc_set_loadmode(OMAP_DSS_LOAD_FRAME_ONLY);

	dispc_read_plane_fifo_sizes();
}

int dispc_enable_plane(enum omap_plane plane, bool enable)
{
	DSSDBG("dispc_enable_plane %d, %d\n", plane, enable);

	enable_clocks(1);
	_dispc_enable_plane(plane, enable);
	enable_clocks(0);

	return 0;
}

int dispc_setup_plane(enum omap_plane plane,
		       u32 paddr, u16 screen_width,
		       u16 pos_x, u16 pos_y,
		       u16 width, u16 height,
		       u16 out_width, u16 out_height,
		       enum omap_color_mode color_mode,
		       bool ilace,
		       enum omap_dss_rotation_type rotation_type,
		       u8 rotation, bool mirror, u8 global_alpha,
		       u8 pre_mult_alpha, enum omap_channel channel,
		       u32 puv_addr)
{
	int r = 0;

	DSSDBG("dispc_setup_plane %d, pa %x, sw %d, %d, %d, %dx%d -> "
	       "%dx%d, ilace %d, cmode %x, rot %d, mir %d chan %d\n",
	       plane, paddr, screen_width, pos_x, pos_y,
	       width, height,
	       out_width, out_height,
	       ilace, color_mode,
	       rotation, mirror, channel);

	enable_clocks(1);

	r = _dispc_setup_plane(plane,
			   paddr, screen_width,
			   pos_x, pos_y,
			   width, height,
			   out_width, out_height,
			   color_mode, ilace,
			   rotation_type,
			   rotation, mirror,
			   global_alpha,
			   pre_mult_alpha,
			   channel, puv_addr);

	enable_clocks(0);

	return r;
}

/* DISPC HW IP initialisation */
static int omap_dispchw_probe(struct platform_device *pdev)
{
	u32 rev;
	int r = 0;
	struct resource *dispc_mem;

	dispc.pdev = pdev;

	spin_lock_init(&dispc.irq_lock);

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
	spin_lock_init(&dispc.irq_stats_lock);
	dispc.irq_stats.last_reset = jiffies;
#endif

	INIT_WORK(&dispc.error_work, dispc_error_worker);

	dispc_mem = platform_get_resource(dispc.pdev, IORESOURCE_MEM, 0);
	if (!dispc_mem) {
		DSSERR("can't get IORESOURCE_MEM DISPC\n");
		r = -EINVAL;
		goto fail0;
	}
	dispc.base = ioremap(dispc_mem->start, resource_size(dispc_mem));
	if (!dispc.base) {
		DSSERR("can't ioremap DISPC\n");
		r = -ENOMEM;
		goto fail0;
	}
	dispc.irq = platform_get_irq(dispc.pdev, 0);
	if (dispc.irq < 0) {
		DSSERR("platform_get_irq failed\n");
		r = -ENODEV;
		goto fail1;
	}

	r = request_irq(dispc.irq, omap_dispc_irq_handler, IRQF_SHARED,
		"OMAP DISPC", dispc.pdev);
	if (r < 0) {
		DSSERR("request_irq failed\n");
		goto fail1;
	}

	enable_clocks(1);

	_omap_dispc_initial_config();

	_omap_dispc_initialize_irq();

	dispc_save_context();

	rev = dispc_read_reg(DISPC_REVISION);
	dev_dbg(&pdev->dev, "OMAP DISPC rev %d.%d\n",
	       FLD_GET(rev, 7, 4), FLD_GET(rev, 3, 0));

	enable_clocks(0);

	return 0;
fail1:
	iounmap(dispc.base);
fail0:
	return r;
}

static int omap_dispchw_remove(struct platform_device *pdev)
{
	free_irq(dispc.irq, dispc.pdev);
	iounmap(dispc.base);
	return 0;
}

static struct platform_driver omap_dispchw_driver = {
	.probe          = omap_dispchw_probe,
	.remove         = omap_dispchw_remove,
	.driver         = {
		.name   = "omapdss_dispc",
		.owner  = THIS_MODULE,
	},
};

int dispc_init_platform_driver(void)
{
	return platform_driver_register(&omap_dispchw_driver);
}

void dispc_uninit_platform_driver(void)
{
	return platform_driver_unregister(&omap_dispchw_driver);
}
