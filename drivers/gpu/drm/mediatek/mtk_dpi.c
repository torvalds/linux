// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Jie Qiu <jie.qiu@mediatek.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/media-bus-format.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk-mmsys.h>
#include <linux/types.h>

#include <video/videomode.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_simple_kms_helper.h>

#include "mtk_ddp_comp.h"
#include "mtk_disp_drv.h"
#include "mtk_dpi_regs.h"
#include "mtk_drm_drv.h"

enum mtk_dpi_out_bit_num {
	MTK_DPI_OUT_BIT_NUM_8BITS,
	MTK_DPI_OUT_BIT_NUM_10BITS,
	MTK_DPI_OUT_BIT_NUM_12BITS,
	MTK_DPI_OUT_BIT_NUM_16BITS
};

enum mtk_dpi_out_yc_map {
	MTK_DPI_OUT_YC_MAP_RGB,
	MTK_DPI_OUT_YC_MAP_CYCY,
	MTK_DPI_OUT_YC_MAP_YCYC,
	MTK_DPI_OUT_YC_MAP_CY,
	MTK_DPI_OUT_YC_MAP_YC
};

enum mtk_dpi_out_channel_swap {
	MTK_DPI_OUT_CHANNEL_SWAP_RGB,
	MTK_DPI_OUT_CHANNEL_SWAP_GBR,
	MTK_DPI_OUT_CHANNEL_SWAP_BRG,
	MTK_DPI_OUT_CHANNEL_SWAP_RBG,
	MTK_DPI_OUT_CHANNEL_SWAP_GRB,
	MTK_DPI_OUT_CHANNEL_SWAP_BGR
};

enum mtk_dpi_out_color_format {
	MTK_DPI_COLOR_FORMAT_RGB,
	MTK_DPI_COLOR_FORMAT_YCBCR_422,
	MTK_DPI_COLOR_FORMAT_YCBCR_444
};

struct mtk_dpi {
	struct drm_encoder encoder;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_connector *connector;
	void __iomem *regs;
	struct device *dev;
	struct device *mmsys_dev;
	struct clk *engine_clk;
	struct clk *pixel_clk;
	struct clk *tvd_clk;
	int irq;
	struct drm_display_mode mode;
	const struct mtk_dpi_conf *conf;
	enum mtk_dpi_out_color_format color_format;
	enum mtk_dpi_out_yc_map yc_map;
	enum mtk_dpi_out_bit_num bit_num;
	enum mtk_dpi_out_channel_swap channel_swap;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_gpio;
	struct pinctrl_state *pins_dpi;
	u32 output_fmt;
	int refcount;
};

static inline struct mtk_dpi *bridge_to_dpi(struct drm_bridge *b)
{
	return container_of(b, struct mtk_dpi, bridge);
}

enum mtk_dpi_polarity {
	MTK_DPI_POLARITY_RISING,
	MTK_DPI_POLARITY_FALLING,
};

struct mtk_dpi_polarities {
	enum mtk_dpi_polarity de_pol;
	enum mtk_dpi_polarity ck_pol;
	enum mtk_dpi_polarity hsync_pol;
	enum mtk_dpi_polarity vsync_pol;
};

struct mtk_dpi_sync_param {
	u32 sync_width;
	u32 front_porch;
	u32 back_porch;
	bool shift_half_line;
};

struct mtk_dpi_yc_limit {
	u16 y_top;
	u16 y_bottom;
	u16 c_top;
	u16 c_bottom;
};

struct mtk_dpi_factor {
	u32 clock;
	u8 factor;
};

/**
 * struct mtk_dpi_conf - Configuration of mediatek dpi.
 * @dpi_factor: SoC-specific pixel clock PLL factor values.
 * @num_dpi_factor: Number of pixel clock PLL factor values.
 * @reg_h_fre_con: Register address of frequency control.
 * @max_clock_khz: Max clock frequency supported for this SoCs in khz units.
 * @edge_sel_en: Enable of edge selection.
 * @output_fmts: Array of supported output formats.
 * @num_output_fmts: Quantity of supported output formats.
 * @is_ck_de_pol: Support CK/DE polarity.
 * @swap_input_support: Support input swap function.
 * @support_direct_pin: IP supports direct connection to dpi panels.
 * @dimension_mask: Mask used for HWIDTH, HPORCH, VSYNC_WIDTH and VSYNC_PORCH
 *		    (no shift).
 * @hvsize_mask: Mask of HSIZE and VSIZE mask (no shift).
 * @channel_swap_shift: Shift value of channel swap.
 * @yuv422_en_bit: Enable bit of yuv422.
 * @csc_enable_bit: Enable bit of CSC.
 * @input_2p_en_bit: Enable bit for input two pixel per round feature.
 *		     If present, implies that the feature must be enabled.
 * @pixels_per_iter: Quantity of transferred pixels per iteration.
 * @edge_cfg_in_mmsys: If the edge configuration for DPI's output needs to be set in MMSYS.
 * @clocked_by_hdmi: HDMI IP outputs clock to dpi_pixel_clk input clock, needed
 *		     for DPI registers access.
 * @output_1pixel: Enable outputting one pixel per round; if the input is two pixel per
 *                 round, the DPI hardware will internally transform it to 1T1P.
 */
struct mtk_dpi_conf {
	const struct mtk_dpi_factor *dpi_factor;
	const u8 num_dpi_factor;
	u32 reg_h_fre_con;
	u32 max_clock_khz;
	bool edge_sel_en;
	const u32 *output_fmts;
	u32 num_output_fmts;
	bool is_ck_de_pol;
	bool swap_input_support;
	bool support_direct_pin;
	u32 dimension_mask;
	u32 hvsize_mask;
	u32 channel_swap_shift;
	u32 yuv422_en_bit;
	u32 csc_enable_bit;
	u32 input_2p_en_bit;
	u32 pixels_per_iter;
	bool edge_cfg_in_mmsys;
	bool clocked_by_hdmi;
	bool output_1pixel;
};

static void mtk_dpi_mask(struct mtk_dpi *dpi, u32 offset, u32 val, u32 mask)
{
	u32 tmp = readl(dpi->regs + offset) & ~mask;

	tmp |= (val & mask);
	writel(tmp, dpi->regs + offset);
}

static void mtk_dpi_test_pattern_en(struct mtk_dpi *dpi, u8 type, bool enable)
{
	u32 val;

	if (enable)
		val = FIELD_PREP(DPI_PAT_SEL, type) | DPI_PAT_EN;
	else
		val = 0;

	mtk_dpi_mask(dpi, DPI_PATTERN0, val, DPI_PAT_SEL | DPI_PAT_EN);
}

static void mtk_dpi_sw_reset(struct mtk_dpi *dpi, bool reset)
{
	mtk_dpi_mask(dpi, DPI_RET, reset ? RST : 0, RST);
}

static void mtk_dpi_enable(struct mtk_dpi *dpi)
{
	mtk_dpi_mask(dpi, DPI_EN, EN, EN);
}

static void mtk_dpi_disable(struct mtk_dpi *dpi)
{
	mtk_dpi_mask(dpi, DPI_EN, 0, EN);
}

static void mtk_dpi_config_hsync(struct mtk_dpi *dpi,
				 struct mtk_dpi_sync_param *sync)
{
	mtk_dpi_mask(dpi, DPI_TGEN_HWIDTH, sync->sync_width << HPW,
		     dpi->conf->dimension_mask << HPW);
	mtk_dpi_mask(dpi, DPI_TGEN_HPORCH, sync->back_porch << HBP,
		     dpi->conf->dimension_mask << HBP);
	mtk_dpi_mask(dpi, DPI_TGEN_HPORCH, sync->front_porch << HFP,
		     dpi->conf->dimension_mask << HFP);
}

static void mtk_dpi_config_vsync(struct mtk_dpi *dpi,
				 struct mtk_dpi_sync_param *sync,
				 u32 width_addr, u32 porch_addr)
{
	mtk_dpi_mask(dpi, width_addr,
		     sync->shift_half_line << VSYNC_HALF_LINE_SHIFT,
		     VSYNC_HALF_LINE_MASK);
	mtk_dpi_mask(dpi, width_addr,
		     sync->sync_width << VSYNC_WIDTH_SHIFT,
		     dpi->conf->dimension_mask << VSYNC_WIDTH_SHIFT);
	mtk_dpi_mask(dpi, porch_addr,
		     sync->back_porch << VSYNC_BACK_PORCH_SHIFT,
		     dpi->conf->dimension_mask << VSYNC_BACK_PORCH_SHIFT);
	mtk_dpi_mask(dpi, porch_addr,
		     sync->front_porch << VSYNC_FRONT_PORCH_SHIFT,
		     dpi->conf->dimension_mask << VSYNC_FRONT_PORCH_SHIFT);
}

static void mtk_dpi_config_vsync_lodd(struct mtk_dpi *dpi,
				      struct mtk_dpi_sync_param *sync)
{
	mtk_dpi_config_vsync(dpi, sync, DPI_TGEN_VWIDTH, DPI_TGEN_VPORCH);
}

static void mtk_dpi_config_vsync_leven(struct mtk_dpi *dpi,
				       struct mtk_dpi_sync_param *sync)
{
	mtk_dpi_config_vsync(dpi, sync, DPI_TGEN_VWIDTH_LEVEN,
			     DPI_TGEN_VPORCH_LEVEN);
}

static void mtk_dpi_config_vsync_rodd(struct mtk_dpi *dpi,
				      struct mtk_dpi_sync_param *sync)
{
	mtk_dpi_config_vsync(dpi, sync, DPI_TGEN_VWIDTH_RODD,
			     DPI_TGEN_VPORCH_RODD);
}

static void mtk_dpi_config_vsync_reven(struct mtk_dpi *dpi,
				       struct mtk_dpi_sync_param *sync)
{
	mtk_dpi_config_vsync(dpi, sync, DPI_TGEN_VWIDTH_REVEN,
			     DPI_TGEN_VPORCH_REVEN);
}

static void mtk_dpi_config_pol(struct mtk_dpi *dpi,
			       struct mtk_dpi_polarities *dpi_pol)
{
	unsigned int pol;
	unsigned int mask;

	mask = HSYNC_POL | VSYNC_POL;
	pol = (dpi_pol->hsync_pol == MTK_DPI_POLARITY_RISING ? 0 : HSYNC_POL) |
	      (dpi_pol->vsync_pol == MTK_DPI_POLARITY_RISING ? 0 : VSYNC_POL);
	if (dpi->conf->is_ck_de_pol) {
		mask |= CK_POL | DE_POL;
		pol |= (dpi_pol->ck_pol == MTK_DPI_POLARITY_RISING ?
			0 : CK_POL) |
		       (dpi_pol->de_pol == MTK_DPI_POLARITY_RISING ?
			0 : DE_POL);
	}

	mtk_dpi_mask(dpi, DPI_OUTPUT_SETTING, pol, mask);
}

static void mtk_dpi_config_3d(struct mtk_dpi *dpi, bool en_3d)
{
	mtk_dpi_mask(dpi, DPI_CON, en_3d ? TDFP_EN : 0, TDFP_EN);
}

static void mtk_dpi_config_interface(struct mtk_dpi *dpi, bool inter)
{
	mtk_dpi_mask(dpi, DPI_CON, inter ? INTL_EN : 0, INTL_EN);
}

static void mtk_dpi_config_fb_size(struct mtk_dpi *dpi, u32 width, u32 height)
{
	mtk_dpi_mask(dpi, DPI_SIZE, width << HSIZE,
		     dpi->conf->hvsize_mask << HSIZE);
	mtk_dpi_mask(dpi, DPI_SIZE, height << VSIZE,
		     dpi->conf->hvsize_mask << VSIZE);
}

static void mtk_dpi_config_channel_limit(struct mtk_dpi *dpi)
{
	struct mtk_dpi_yc_limit limit;

	if (drm_default_rgb_quant_range(&dpi->mode) ==
	    HDMI_QUANTIZATION_RANGE_LIMITED) {
		limit.y_bottom = 0x10;
		limit.y_top = 0xfe0;
		limit.c_bottom = 0x10;
		limit.c_top = 0xfe0;
	} else {
		limit.y_bottom = 0;
		limit.y_top = 0xfff;
		limit.c_bottom = 0;
		limit.c_top = 0xfff;
	}

	mtk_dpi_mask(dpi, DPI_Y_LIMIT, limit.y_bottom << Y_LIMINT_BOT,
		     Y_LIMINT_BOT_MASK);
	mtk_dpi_mask(dpi, DPI_Y_LIMIT, limit.y_top << Y_LIMINT_TOP,
		     Y_LIMINT_TOP_MASK);
	mtk_dpi_mask(dpi, DPI_C_LIMIT, limit.c_bottom << C_LIMIT_BOT,
		     C_LIMIT_BOT_MASK);
	mtk_dpi_mask(dpi, DPI_C_LIMIT, limit.c_top << C_LIMIT_TOP,
		     C_LIMIT_TOP_MASK);
}

static void mtk_dpi_config_bit_num(struct mtk_dpi *dpi,
				   enum mtk_dpi_out_bit_num num)
{
	u32 val;

	switch (num) {
	case MTK_DPI_OUT_BIT_NUM_8BITS:
		val = OUT_BIT_8;
		break;
	case MTK_DPI_OUT_BIT_NUM_10BITS:
		val = OUT_BIT_10;
		break;
	case MTK_DPI_OUT_BIT_NUM_12BITS:
		val = OUT_BIT_12;
		break;
	case MTK_DPI_OUT_BIT_NUM_16BITS:
		val = OUT_BIT_16;
		break;
	default:
		val = OUT_BIT_8;
		break;
	}
	mtk_dpi_mask(dpi, DPI_OUTPUT_SETTING, val << OUT_BIT,
		     OUT_BIT_MASK);
}

static void mtk_dpi_config_yc_map(struct mtk_dpi *dpi,
				  enum mtk_dpi_out_yc_map map)
{
	u32 val;

	switch (map) {
	case MTK_DPI_OUT_YC_MAP_RGB:
		val = YC_MAP_RGB;
		break;
	case MTK_DPI_OUT_YC_MAP_CYCY:
		val = YC_MAP_CYCY;
		break;
	case MTK_DPI_OUT_YC_MAP_YCYC:
		val = YC_MAP_YCYC;
		break;
	case MTK_DPI_OUT_YC_MAP_CY:
		val = YC_MAP_CY;
		break;
	case MTK_DPI_OUT_YC_MAP_YC:
		val = YC_MAP_YC;
		break;
	default:
		val = YC_MAP_RGB;
		break;
	}

	mtk_dpi_mask(dpi, DPI_OUTPUT_SETTING, val << YC_MAP, YC_MAP_MASK);
}

static void mtk_dpi_config_channel_swap(struct mtk_dpi *dpi,
					enum mtk_dpi_out_channel_swap swap)
{
	u32 val;

	switch (swap) {
	case MTK_DPI_OUT_CHANNEL_SWAP_RGB:
		val = SWAP_RGB;
		break;
	case MTK_DPI_OUT_CHANNEL_SWAP_GBR:
		val = SWAP_GBR;
		break;
	case MTK_DPI_OUT_CHANNEL_SWAP_BRG:
		val = SWAP_BRG;
		break;
	case MTK_DPI_OUT_CHANNEL_SWAP_RBG:
		val = SWAP_RBG;
		break;
	case MTK_DPI_OUT_CHANNEL_SWAP_GRB:
		val = SWAP_GRB;
		break;
	case MTK_DPI_OUT_CHANNEL_SWAP_BGR:
		val = SWAP_BGR;
		break;
	default:
		val = SWAP_RGB;
		break;
	}

	mtk_dpi_mask(dpi, DPI_OUTPUT_SETTING,
		     val << dpi->conf->channel_swap_shift,
		     CH_SWAP_MASK << dpi->conf->channel_swap_shift);
}

static void mtk_dpi_config_yuv422_enable(struct mtk_dpi *dpi, bool enable)
{
	mtk_dpi_mask(dpi, DPI_CON, enable ? dpi->conf->yuv422_en_bit : 0,
		     dpi->conf->yuv422_en_bit);
}

static void mtk_dpi_config_csc_enable(struct mtk_dpi *dpi, bool enable)
{
	mtk_dpi_mask(dpi, DPI_CON, enable ? dpi->conf->csc_enable_bit : 0,
		     dpi->conf->csc_enable_bit);
}

static void mtk_dpi_config_swap_input(struct mtk_dpi *dpi, bool enable)
{
	mtk_dpi_mask(dpi, DPI_CON, enable ? IN_RB_SWAP : 0, IN_RB_SWAP);
}

static void mtk_dpi_config_2n_h_fre(struct mtk_dpi *dpi)
{
	if (dpi->conf->reg_h_fre_con)
		mtk_dpi_mask(dpi, dpi->conf->reg_h_fre_con, H_FRE_2N, H_FRE_2N);
}

static void mtk_dpi_config_disable_edge(struct mtk_dpi *dpi)
{
	if (dpi->conf->edge_sel_en && dpi->conf->reg_h_fre_con)
		mtk_dpi_mask(dpi, dpi->conf->reg_h_fre_con, 0, EDGE_SEL_EN);
}

static void mtk_dpi_config_color_format(struct mtk_dpi *dpi,
					enum mtk_dpi_out_color_format format)
{
	mtk_dpi_config_channel_swap(dpi, dpi->channel_swap);

	switch (format) {
	case MTK_DPI_COLOR_FORMAT_YCBCR_444:
		mtk_dpi_config_yuv422_enable(dpi, false);
		mtk_dpi_config_csc_enable(dpi, true);
		if (dpi->conf->swap_input_support)
			mtk_dpi_config_swap_input(dpi, false);
		break;
	case MTK_DPI_COLOR_FORMAT_YCBCR_422:
		mtk_dpi_config_yuv422_enable(dpi, true);
		mtk_dpi_config_csc_enable(dpi, true);

		/*
		 * If height is smaller than 720, we need to use RGB_TO_BT601
		 * to transfer to yuv422. Otherwise, we use RGB_TO_JPEG.
		 */
		mtk_dpi_mask(dpi, DPI_MATRIX_SET, dpi->mode.hdisplay <= 720 ?
			     MATRIX_SEL_RGB_TO_BT601 : MATRIX_SEL_RGB_TO_JPEG,
			     INT_MATRIX_SEL_MASK);
		break;
	default:
	case MTK_DPI_COLOR_FORMAT_RGB:
		mtk_dpi_config_yuv422_enable(dpi, false);
		mtk_dpi_config_csc_enable(dpi, false);
		if (dpi->conf->swap_input_support)
			mtk_dpi_config_swap_input(dpi, false);
		break;
	}
}

static void mtk_dpi_dual_edge(struct mtk_dpi *dpi)
{
	if ((dpi->output_fmt == MEDIA_BUS_FMT_RGB888_2X12_LE) ||
	    (dpi->output_fmt == MEDIA_BUS_FMT_RGB888_2X12_BE)) {
		mtk_dpi_mask(dpi, DPI_DDR_SETTING, DDR_EN | DDR_4PHASE,
			     DDR_EN | DDR_4PHASE);
		mtk_dpi_mask(dpi, DPI_OUTPUT_SETTING,
			     dpi->output_fmt == MEDIA_BUS_FMT_RGB888_2X12_LE ?
			     EDGE_SEL : 0, EDGE_SEL);
		if (dpi->conf->edge_cfg_in_mmsys)
			mtk_mmsys_ddp_dpi_fmt_config(dpi->mmsys_dev, MTK_DPI_RGB888_DDR_CON);
	} else {
		mtk_dpi_mask(dpi, DPI_DDR_SETTING, DDR_EN | DDR_4PHASE, 0);
		if (dpi->conf->edge_cfg_in_mmsys)
			mtk_mmsys_ddp_dpi_fmt_config(dpi->mmsys_dev, MTK_DPI_RGB888_SDR_CON);
	}
}

static void mtk_dpi_power_off(struct mtk_dpi *dpi)
{
	if (WARN_ON(dpi->refcount == 0))
		return;

	if (--dpi->refcount != 0)
		return;

	mtk_dpi_disable(dpi);
	clk_disable_unprepare(dpi->pixel_clk);
	clk_disable_unprepare(dpi->tvd_clk);
	clk_disable_unprepare(dpi->engine_clk);
}

static int mtk_dpi_power_on(struct mtk_dpi *dpi)
{
	int ret;

	if (++dpi->refcount != 1)
		return 0;

	ret = clk_prepare_enable(dpi->engine_clk);
	if (ret) {
		dev_err(dpi->dev, "Failed to enable engine clock: %d\n", ret);
		goto err_refcount;
	}

	ret = clk_prepare_enable(dpi->tvd_clk);
	if (ret) {
		dev_err(dpi->dev, "Failed to enable tvd pll: %d\n", ret);
		goto err_engine;
	}

	ret = clk_prepare_enable(dpi->pixel_clk);
	if (ret) {
		dev_err(dpi->dev, "Failed to enable pixel clock: %d\n", ret);
		goto err_pixel;
	}

	return 0;

err_pixel:
	clk_disable_unprepare(dpi->tvd_clk);
err_engine:
	clk_disable_unprepare(dpi->engine_clk);
err_refcount:
	dpi->refcount--;
	return ret;
}

static unsigned int mtk_dpi_calculate_factor(struct mtk_dpi *dpi, int mode_clk)
{
	const struct mtk_dpi_factor *dpi_factor = dpi->conf->dpi_factor;
	int i;

	for (i = 0; i < dpi->conf->num_dpi_factor; i++) {
		if (mode_clk <= dpi_factor[i].clock)
			return dpi_factor[i].factor;
	}

	/* If no match try the lowest possible factor */
	return dpi_factor[dpi->conf->num_dpi_factor - 1].factor;
}

static void mtk_dpi_set_pixel_clk(struct mtk_dpi *dpi, struct videomode *vm, int mode_clk)
{
	unsigned long pll_rate;
	unsigned int factor;

	/* let pll_rate can fix the valid range of tvdpll (1G~2GHz) */
	factor = mtk_dpi_calculate_factor(dpi, mode_clk);
	pll_rate = vm->pixelclock * factor;

	dev_dbg(dpi->dev, "Want PLL %lu Hz, pixel clock %lu Hz\n",
		pll_rate, vm->pixelclock);

	clk_set_rate(dpi->tvd_clk, pll_rate);
	pll_rate = clk_get_rate(dpi->tvd_clk);

	/*
	 * Depending on the IP version, we may output a different amount of
	 * pixels for each iteration: divide the clock by this number and
	 * adjust the display porches accordingly.
	 */
	vm->pixelclock = pll_rate / factor;
	vm->pixelclock /= dpi->conf->pixels_per_iter;

	if ((dpi->output_fmt == MEDIA_BUS_FMT_RGB888_2X12_LE) ||
	    (dpi->output_fmt == MEDIA_BUS_FMT_RGB888_2X12_BE))
		clk_set_rate(dpi->pixel_clk, vm->pixelclock * 2);
	else
		clk_set_rate(dpi->pixel_clk, vm->pixelclock);

	vm->pixelclock = clk_get_rate(dpi->pixel_clk);

	dev_dbg(dpi->dev, "Got  PLL %lu Hz, pixel clock %lu Hz\n",
		pll_rate, vm->pixelclock);
}

static int mtk_dpi_set_display_mode(struct mtk_dpi *dpi,
				    struct drm_display_mode *mode)
{
	struct mtk_dpi_polarities dpi_pol;
	struct mtk_dpi_sync_param hsync;
	struct mtk_dpi_sync_param vsync_lodd = { 0 };
	struct mtk_dpi_sync_param vsync_leven = { 0 };
	struct mtk_dpi_sync_param vsync_rodd = { 0 };
	struct mtk_dpi_sync_param vsync_reven = { 0 };
	struct videomode vm = { 0 };

	drm_display_mode_to_videomode(mode, &vm);

	if (!dpi->conf->clocked_by_hdmi)
		mtk_dpi_set_pixel_clk(dpi, &vm, mode->clock);

	dpi_pol.ck_pol = MTK_DPI_POLARITY_FALLING;
	dpi_pol.de_pol = MTK_DPI_POLARITY_RISING;
	dpi_pol.hsync_pol = vm.flags & DISPLAY_FLAGS_HSYNC_HIGH ?
			    MTK_DPI_POLARITY_FALLING : MTK_DPI_POLARITY_RISING;
	dpi_pol.vsync_pol = vm.flags & DISPLAY_FLAGS_VSYNC_HIGH ?
			    MTK_DPI_POLARITY_FALLING : MTK_DPI_POLARITY_RISING;

	/*
	 * Depending on the IP version, we may output a different amount of
	 * pixels for each iteration: divide the clock by this number and
	 * adjust the display porches accordingly.
	 */
	hsync.sync_width = vm.hsync_len / dpi->conf->pixels_per_iter;
	hsync.back_porch = vm.hback_porch / dpi->conf->pixels_per_iter;
	hsync.front_porch = vm.hfront_porch / dpi->conf->pixels_per_iter;

	hsync.shift_half_line = false;
	vsync_lodd.sync_width = vm.vsync_len;
	vsync_lodd.back_porch = vm.vback_porch;
	vsync_lodd.front_porch = vm.vfront_porch;
	vsync_lodd.shift_half_line = false;

	if (vm.flags & DISPLAY_FLAGS_INTERLACED &&
	    mode->flags & DRM_MODE_FLAG_3D_MASK) {
		vsync_leven = vsync_lodd;
		vsync_rodd = vsync_lodd;
		vsync_reven = vsync_lodd;
		vsync_leven.shift_half_line = true;
		vsync_reven.shift_half_line = true;
	} else if (vm.flags & DISPLAY_FLAGS_INTERLACED &&
		   !(mode->flags & DRM_MODE_FLAG_3D_MASK)) {
		vsync_leven = vsync_lodd;
		vsync_leven.shift_half_line = true;
	} else if (!(vm.flags & DISPLAY_FLAGS_INTERLACED) &&
		   mode->flags & DRM_MODE_FLAG_3D_MASK) {
		vsync_rodd = vsync_lodd;
	}
	mtk_dpi_sw_reset(dpi, true);
	mtk_dpi_config_pol(dpi, &dpi_pol);

	mtk_dpi_config_hsync(dpi, &hsync);
	mtk_dpi_config_vsync_lodd(dpi, &vsync_lodd);
	mtk_dpi_config_vsync_rodd(dpi, &vsync_rodd);
	mtk_dpi_config_vsync_leven(dpi, &vsync_leven);
	mtk_dpi_config_vsync_reven(dpi, &vsync_reven);

	mtk_dpi_config_3d(dpi, !!(mode->flags & DRM_MODE_FLAG_3D_MASK));
	mtk_dpi_config_interface(dpi, !!(vm.flags &
					 DISPLAY_FLAGS_INTERLACED));
	if (vm.flags & DISPLAY_FLAGS_INTERLACED)
		mtk_dpi_config_fb_size(dpi, vm.hactive, vm.vactive >> 1);
	else
		mtk_dpi_config_fb_size(dpi, vm.hactive, vm.vactive);

	mtk_dpi_config_channel_limit(dpi);
	mtk_dpi_config_bit_num(dpi, dpi->bit_num);
	mtk_dpi_config_channel_swap(dpi, dpi->channel_swap);
	mtk_dpi_config_color_format(dpi, dpi->color_format);
	if (dpi->conf->support_direct_pin) {
		mtk_dpi_config_yc_map(dpi, dpi->yc_map);
		mtk_dpi_config_2n_h_fre(dpi);

		/* DPI can connect to either an external bridge or the internal HDMI encoder */
		if (dpi->conf->output_1pixel)
			mtk_dpi_mask(dpi, DPI_CON, DPI_OUTPUT_1T1P_EN, DPI_OUTPUT_1T1P_EN);
		else
			mtk_dpi_dual_edge(dpi);

		mtk_dpi_config_disable_edge(dpi);
	}
	if (dpi->conf->input_2p_en_bit) {
		mtk_dpi_mask(dpi, DPI_CON, dpi->conf->input_2p_en_bit,
			     dpi->conf->input_2p_en_bit);
	}
	mtk_dpi_sw_reset(dpi, false);

	return 0;
}

static u32 *mtk_dpi_bridge_atomic_get_output_bus_fmts(struct drm_bridge *bridge,
						      struct drm_bridge_state *bridge_state,
						      struct drm_crtc_state *crtc_state,
						      struct drm_connector_state *conn_state,
						      unsigned int *num_output_fmts)
{
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);
	u32 *output_fmts;

	*num_output_fmts = 0;

	if (!dpi->conf->output_fmts) {
		dev_err(dpi->dev, "output_fmts should not be null\n");
		return NULL;
	}

	output_fmts = kcalloc(dpi->conf->num_output_fmts, sizeof(*output_fmts),
			     GFP_KERNEL);
	if (!output_fmts)
		return NULL;

	*num_output_fmts = dpi->conf->num_output_fmts;

	memcpy(output_fmts, dpi->conf->output_fmts,
	       sizeof(*output_fmts) * dpi->conf->num_output_fmts);

	return output_fmts;
}

static u32 *mtk_dpi_bridge_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
						     struct drm_bridge_state *bridge_state,
						     struct drm_crtc_state *crtc_state,
						     struct drm_connector_state *conn_state,
						     u32 output_fmt,
						     unsigned int *num_input_fmts)
{
	u32 *input_fmts;

	*num_input_fmts = 0;

	input_fmts = kcalloc(1, sizeof(*input_fmts),
			     GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	*num_input_fmts = 1;
	input_fmts[0] = MEDIA_BUS_FMT_RGB888_1X24;

	return input_fmts;
}

static unsigned int mtk_dpi_bus_fmt_bit_num(unsigned int out_bus_format)
{
	switch (out_bus_format) {
	default:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_BGR888_1X24:
	case MEDIA_BUS_FMT_RGB888_2X12_LE:
	case MEDIA_BUS_FMT_RGB888_2X12_BE:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YUV8_1X24:
		return MTK_DPI_OUT_BIT_NUM_8BITS;
	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_YUYV10_1X20:
	case MEDIA_BUS_FMT_YUV10_1X30:
		return MTK_DPI_OUT_BIT_NUM_10BITS;
	case MEDIA_BUS_FMT_YUYV12_1X24:
		return MTK_DPI_OUT_BIT_NUM_12BITS;
	}
}

static unsigned int mtk_dpi_bus_fmt_channel_swap(unsigned int out_bus_format)
{
	switch (out_bus_format) {
	default:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB888_2X12_LE:
	case MEDIA_BUS_FMT_RGB888_2X12_BE:
	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YUYV10_1X20:
	case MEDIA_BUS_FMT_YUYV12_1X24:
		return MTK_DPI_OUT_CHANNEL_SWAP_RGB;
	case MEDIA_BUS_FMT_BGR888_1X24:
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_YUV10_1X30:
		return MTK_DPI_OUT_CHANNEL_SWAP_BGR;
	}
}

static unsigned int mtk_dpi_bus_fmt_color_format(unsigned int out_bus_format)
{
	switch (out_bus_format) {
	default:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_BGR888_1X24:
	case MEDIA_BUS_FMT_RGB888_2X12_LE:
	case MEDIA_BUS_FMT_RGB888_2X12_BE:
	case MEDIA_BUS_FMT_RGB101010_1X30:
		return MTK_DPI_COLOR_FORMAT_RGB;
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YUYV10_1X20:
	case MEDIA_BUS_FMT_YUYV12_1X24:
		return MTK_DPI_COLOR_FORMAT_YCBCR_422;
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_YUV10_1X30:
		return MTK_DPI_COLOR_FORMAT_YCBCR_444;
	}
}

static int mtk_dpi_bridge_atomic_check(struct drm_bridge *bridge,
				       struct drm_bridge_state *bridge_state,
				       struct drm_crtc_state *crtc_state,
				       struct drm_connector_state *conn_state)
{
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);
	unsigned int out_bus_format;

	out_bus_format = bridge_state->output_bus_cfg.format;

	if (out_bus_format == MEDIA_BUS_FMT_FIXED)
		if (dpi->conf->num_output_fmts)
			out_bus_format = dpi->conf->output_fmts[0];

	dev_dbg(dpi->dev, "input format 0x%04x, output format 0x%04x\n",
		bridge_state->input_bus_cfg.format,
		bridge_state->output_bus_cfg.format);

	dpi->output_fmt = out_bus_format;
	dpi->bit_num = mtk_dpi_bus_fmt_bit_num(out_bus_format);
	dpi->channel_swap = mtk_dpi_bus_fmt_channel_swap(out_bus_format);
	dpi->yc_map = MTK_DPI_OUT_YC_MAP_RGB;
	dpi->color_format = mtk_dpi_bus_fmt_color_format(out_bus_format);

	return 0;
}

static int mtk_dpi_bridge_attach(struct drm_bridge *bridge,
				 struct drm_encoder *encoder,
				 enum drm_bridge_attach_flags flags)
{
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);
	int ret;

	dpi->next_bridge = devm_drm_of_get_bridge(dpi->dev, dpi->dev->of_node, 1, -1);
	if (IS_ERR(dpi->next_bridge)) {
		ret = PTR_ERR(dpi->next_bridge);
		if (ret == -EPROBE_DEFER)
			return ret;

		/* Old devicetree has only one endpoint */
		dpi->next_bridge = devm_drm_of_get_bridge(dpi->dev, dpi->dev->of_node, 0, 0);
		if (IS_ERR(dpi->next_bridge))
			return dev_err_probe(dpi->dev, PTR_ERR(dpi->next_bridge),
					     "Failed to get bridge\n");
	}

	return drm_bridge_attach(encoder, dpi->next_bridge,
				 &dpi->bridge, flags);
}

static void mtk_dpi_bridge_mode_set(struct drm_bridge *bridge,
				const struct drm_display_mode *mode,
				const struct drm_display_mode *adjusted_mode)
{
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);

	drm_mode_copy(&dpi->mode, adjusted_mode);
}

static void mtk_dpi_bridge_disable(struct drm_bridge *bridge)
{
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);

	mtk_dpi_power_off(dpi);

	if (dpi->pinctrl && dpi->pins_gpio)
		pinctrl_select_state(dpi->pinctrl, dpi->pins_gpio);
}

static void mtk_dpi_bridge_enable(struct drm_bridge *bridge)
{
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);

	if (dpi->pinctrl && dpi->pins_dpi)
		pinctrl_select_state(dpi->pinctrl, dpi->pins_dpi);

	mtk_dpi_power_on(dpi);
	mtk_dpi_set_display_mode(dpi, &dpi->mode);
	mtk_dpi_enable(dpi);
}

static enum drm_mode_status
mtk_dpi_bridge_mode_valid(struct drm_bridge *bridge,
			  const struct drm_display_info *info,
			  const struct drm_display_mode *mode)
{
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);

	if (mode->clock > dpi->conf->max_clock_khz)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static int mtk_dpi_debug_tp_show(struct seq_file *m, void *arg)
{
	struct mtk_dpi *dpi = m->private;
	bool en;
	u32 val;

	if (!dpi)
		return -EINVAL;

	val = readl(dpi->regs + DPI_PATTERN0);
	en = val & DPI_PAT_EN;
	val = FIELD_GET(DPI_PAT_SEL, val);

	seq_printf(m, "DPI Test Pattern: %s\n", en ? "Enabled" : "Disabled");

	if (en) {
		seq_printf(m, "Internal pattern %d: ", val);
		switch (val) {
		case 0:
			seq_puts(m, "256 Vertical Gray\n");
			break;
		case 1:
			seq_puts(m, "1024 Vertical Gray\n");
			break;
		case 2:
			seq_puts(m, "256 Horizontal Gray\n");
			break;
		case 3:
			seq_puts(m, "1024 Horizontal Gray\n");
			break;
		case 4:
			seq_puts(m, "Vertical Color bars\n");
			break;
		case 6:
			seq_puts(m, "Frame border\n");
			break;
		case 7:
			seq_puts(m, "Dot moire\n");
			break;
		default:
			seq_puts(m, "Invalid selection\n");
			break;
		}
	}

	return 0;
}

static ssize_t mtk_dpi_debug_tp_write(struct file *file, const char __user *ubuf,
				      size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	u32 en, type;
	char buf[6];

	if (!m || !m->private || *offp || len > sizeof(buf) - 1)
		return -EINVAL;

	memset(buf, 0, sizeof(buf));
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;

	if (sscanf(buf, "%u %u", &en, &type) != 2)
		return -EINVAL;

	if (en < 0 || en > 1 || type < 0 || type > 7)
		return -EINVAL;

	mtk_dpi_test_pattern_en((struct mtk_dpi *)m->private, type, en);
	return len;
}

static int mtk_dpi_debug_tp_open(struct inode *inode, struct file *file)
{
	return single_open(file, mtk_dpi_debug_tp_show, inode->i_private);
}

static const struct file_operations mtk_dpi_debug_tp_fops = {
	.owner = THIS_MODULE,
	.open = mtk_dpi_debug_tp_open,
	.read = seq_read,
	.write = mtk_dpi_debug_tp_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static void mtk_dpi_debugfs_init(struct drm_bridge *bridge, struct dentry *root)
{
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);

	debugfs_create_file("dpi_test_pattern", 0640, root, dpi, &mtk_dpi_debug_tp_fops);
}

static const struct drm_bridge_funcs mtk_dpi_bridge_funcs = {
	.attach = mtk_dpi_bridge_attach,
	.mode_set = mtk_dpi_bridge_mode_set,
	.mode_valid = mtk_dpi_bridge_mode_valid,
	.disable = mtk_dpi_bridge_disable,
	.enable = mtk_dpi_bridge_enable,
	.atomic_check = mtk_dpi_bridge_atomic_check,
	.atomic_get_output_bus_fmts = mtk_dpi_bridge_atomic_get_output_bus_fmts,
	.atomic_get_input_bus_fmts = mtk_dpi_bridge_atomic_get_input_bus_fmts,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.debugfs_init = mtk_dpi_debugfs_init,
};

void mtk_dpi_start(struct device *dev)
{
	struct mtk_dpi *dpi = dev_get_drvdata(dev);

	if (!dpi->conf->clocked_by_hdmi)
		mtk_dpi_power_on(dpi);
}

void mtk_dpi_stop(struct device *dev)
{
	struct mtk_dpi *dpi = dev_get_drvdata(dev);

	if (!dpi->conf->clocked_by_hdmi)
		mtk_dpi_power_off(dpi);
}

unsigned int mtk_dpi_encoder_index(struct device *dev)
{
	struct mtk_dpi *dpi = dev_get_drvdata(dev);
	unsigned int encoder_index = drm_encoder_index(&dpi->encoder);

	dev_dbg(dev, "encoder index:%d\n", encoder_index);
	return encoder_index;
}

static int mtk_dpi_bind(struct device *dev, struct device *master, void *data)
{
	struct mtk_dpi *dpi = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct mtk_drm_private *priv = drm_dev->dev_private;
	int ret;

	dpi->mmsys_dev = priv->mmsys_dev;
	ret = drm_simple_encoder_init(drm_dev, &dpi->encoder,
				      DRM_MODE_ENCODER_TMDS);
	if (ret) {
		dev_err(dev, "Failed to initialize decoder: %d\n", ret);
		return ret;
	}

	ret = mtk_find_possible_crtcs(drm_dev, dpi->dev);
	if (ret < 0)
		goto err_cleanup;
	dpi->encoder.possible_crtcs = ret;

	ret = drm_bridge_attach(&dpi->encoder, &dpi->bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret)
		goto err_cleanup;

	dpi->connector = drm_bridge_connector_init(drm_dev, &dpi->encoder);
	if (IS_ERR(dpi->connector)) {
		dev_err(dev, "Unable to create bridge connector\n");
		ret = PTR_ERR(dpi->connector);
		goto err_cleanup;
	}
	drm_connector_attach_encoder(dpi->connector, &dpi->encoder);

	return 0;

err_cleanup:
	drm_encoder_cleanup(&dpi->encoder);
	return ret;
}

static void mtk_dpi_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct mtk_dpi *dpi = dev_get_drvdata(dev);

	drm_encoder_cleanup(&dpi->encoder);
}

static const struct component_ops mtk_dpi_component_ops = {
	.bind = mtk_dpi_bind,
	.unbind = mtk_dpi_unbind,
};

static const u32 mt8173_output_fmts[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
};

static const u32 mt8183_output_fmts[] = {
	MEDIA_BUS_FMT_RGB888_2X12_LE,
	MEDIA_BUS_FMT_RGB888_2X12_BE,
};

static const u32 mt8195_dpi_output_fmts[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_RGB888_2X12_LE,
	MEDIA_BUS_FMT_RGB888_2X12_BE,
	MEDIA_BUS_FMT_RGB101010_1X30,
	MEDIA_BUS_FMT_YUYV8_1X16,
	MEDIA_BUS_FMT_YUYV10_1X20,
	MEDIA_BUS_FMT_YUYV12_1X24,
	MEDIA_BUS_FMT_BGR888_1X24,
	MEDIA_BUS_FMT_YUV8_1X24,
	MEDIA_BUS_FMT_YUV10_1X30,
};

static const u32 mt8195_dp_intf_output_fmts[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_RGB888_2X12_LE,
	MEDIA_BUS_FMT_RGB888_2X12_BE,
	MEDIA_BUS_FMT_RGB101010_1X30,
	MEDIA_BUS_FMT_YUYV8_1X16,
	MEDIA_BUS_FMT_YUYV10_1X20,
	MEDIA_BUS_FMT_BGR888_1X24,
	MEDIA_BUS_FMT_YUV8_1X24,
	MEDIA_BUS_FMT_YUV10_1X30,
};

static const struct mtk_dpi_factor dpi_factor_mt2701[] = {
	{ 64000, 4 }, { 128000, 2 }, { U32_MAX, 1 }
};

static const struct mtk_dpi_factor dpi_factor_mt8173[] = {
	{ 27000, 48 }, { 84000, 24 }, { 167000, 12 }, { U32_MAX, 6 }
};

static const struct mtk_dpi_factor dpi_factor_mt8183[] = {
	{ 27000, 8 }, { 167000, 4 }, { U32_MAX, 2 }
};

static const struct mtk_dpi_factor dpi_factor_mt8195_dp_intf[] = {
	{ 70000 - 1, 4 }, { 200000 - 1, 2 }, { U32_MAX, 1 }
};

static const struct mtk_dpi_conf mt8173_conf = {
	.dpi_factor = dpi_factor_mt8173,
	.num_dpi_factor = ARRAY_SIZE(dpi_factor_mt8173),
	.reg_h_fre_con = 0xe0,
	.max_clock_khz = 300000,
	.output_fmts = mt8173_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8173_output_fmts),
	.pixels_per_iter = 1,
	.is_ck_de_pol = true,
	.swap_input_support = true,
	.support_direct_pin = true,
	.dimension_mask = HPW_MASK,
	.hvsize_mask = HSIZE_MASK,
	.channel_swap_shift = CH_SWAP,
	.yuv422_en_bit = YUV422_EN,
	.csc_enable_bit = CSC_ENABLE,
};

static const struct mtk_dpi_conf mt2701_conf = {
	.dpi_factor = dpi_factor_mt2701,
	.num_dpi_factor = ARRAY_SIZE(dpi_factor_mt2701),
	.reg_h_fre_con = 0xb0,
	.edge_sel_en = true,
	.max_clock_khz = 150000,
	.output_fmts = mt8173_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8173_output_fmts),
	.pixels_per_iter = 1,
	.is_ck_de_pol = true,
	.swap_input_support = true,
	.support_direct_pin = true,
	.dimension_mask = HPW_MASK,
	.hvsize_mask = HSIZE_MASK,
	.channel_swap_shift = CH_SWAP,
	.yuv422_en_bit = YUV422_EN,
	.csc_enable_bit = CSC_ENABLE,
};

static const struct mtk_dpi_conf mt8183_conf = {
	.dpi_factor = dpi_factor_mt8183,
	.num_dpi_factor = ARRAY_SIZE(dpi_factor_mt8183),
	.reg_h_fre_con = 0xe0,
	.max_clock_khz = 100000,
	.output_fmts = mt8183_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8183_output_fmts),
	.pixels_per_iter = 1,
	.is_ck_de_pol = true,
	.swap_input_support = true,
	.support_direct_pin = true,
	.dimension_mask = HPW_MASK,
	.hvsize_mask = HSIZE_MASK,
	.channel_swap_shift = CH_SWAP,
	.yuv422_en_bit = YUV422_EN,
	.csc_enable_bit = CSC_ENABLE,
};

static const struct mtk_dpi_conf mt8186_conf = {
	.dpi_factor = dpi_factor_mt8183,
	.num_dpi_factor = ARRAY_SIZE(dpi_factor_mt8183),
	.reg_h_fre_con = 0xe0,
	.max_clock_khz = 150000,
	.output_fmts = mt8183_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8183_output_fmts),
	.edge_cfg_in_mmsys = true,
	.pixels_per_iter = 1,
	.is_ck_de_pol = true,
	.swap_input_support = true,
	.support_direct_pin = true,
	.dimension_mask = HPW_MASK,
	.hvsize_mask = HSIZE_MASK,
	.channel_swap_shift = CH_SWAP,
	.yuv422_en_bit = YUV422_EN,
	.csc_enable_bit = CSC_ENABLE,
};

static const struct mtk_dpi_conf mt8192_conf = {
	.dpi_factor = dpi_factor_mt8183,
	.num_dpi_factor = ARRAY_SIZE(dpi_factor_mt8183),
	.reg_h_fre_con = 0xe0,
	.max_clock_khz = 150000,
	.output_fmts = mt8183_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8183_output_fmts),
	.pixels_per_iter = 1,
	.is_ck_de_pol = true,
	.swap_input_support = true,
	.support_direct_pin = true,
	.dimension_mask = HPW_MASK,
	.hvsize_mask = HSIZE_MASK,
	.channel_swap_shift = CH_SWAP,
	.yuv422_en_bit = YUV422_EN,
	.csc_enable_bit = CSC_ENABLE,
};

static const struct mtk_dpi_conf mt8195_conf = {
	.max_clock_khz = 594000,
	.output_fmts = mt8195_dpi_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8195_dpi_output_fmts),
	.pixels_per_iter = 1,
	.is_ck_de_pol = true,
	.swap_input_support = true,
	.support_direct_pin = true,
	.dimension_mask = HPW_MASK,
	.hvsize_mask = HSIZE_MASK,
	.channel_swap_shift = CH_SWAP,
	.yuv422_en_bit = YUV422_EN,
	.csc_enable_bit = CSC_ENABLE,
	.input_2p_en_bit = DPI_INPUT_2P_EN,
	.clocked_by_hdmi = true,
	.output_1pixel = true,
};

static const struct mtk_dpi_conf mt8195_dpintf_conf = {
	.dpi_factor = dpi_factor_mt8195_dp_intf,
	.num_dpi_factor = ARRAY_SIZE(dpi_factor_mt8195_dp_intf),
	.max_clock_khz = 600000,
	.output_fmts = mt8195_dp_intf_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8195_dp_intf_output_fmts),
	.pixels_per_iter = 4,
	.dimension_mask = DPINTF_HPW_MASK,
	.hvsize_mask = DPINTF_HSIZE_MASK,
	.channel_swap_shift = DPINTF_CH_SWAP,
	.yuv422_en_bit = DPINTF_YUV422_EN,
	.csc_enable_bit = DPINTF_CSC_ENABLE,
	.input_2p_en_bit = DPINTF_INPUT_2P_EN,
};

static int mtk_dpi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_dpi *dpi;
	int ret;

	dpi = devm_drm_bridge_alloc(dev, struct mtk_dpi, bridge,
				    &mtk_dpi_bridge_funcs);
	if (IS_ERR(dpi))
		return PTR_ERR(dpi);

	dpi->dev = dev;
	dpi->conf = (struct mtk_dpi_conf *)of_device_get_match_data(dev);
	dpi->output_fmt = MEDIA_BUS_FMT_RGB888_1X24;

	dpi->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(dpi->pinctrl)) {
		dpi->pinctrl = NULL;
		dev_dbg(&pdev->dev, "Cannot find pinctrl!\n");
	}
	if (dpi->pinctrl) {
		dpi->pins_gpio = pinctrl_lookup_state(dpi->pinctrl, "sleep");
		if (IS_ERR(dpi->pins_gpio)) {
			dpi->pins_gpio = NULL;
			dev_dbg(&pdev->dev, "Cannot find pinctrl idle!\n");
		}
		if (dpi->pins_gpio)
			pinctrl_select_state(dpi->pinctrl, dpi->pins_gpio);

		dpi->pins_dpi = pinctrl_lookup_state(dpi->pinctrl, "default");
		if (IS_ERR(dpi->pins_dpi)) {
			dpi->pins_dpi = NULL;
			dev_dbg(&pdev->dev, "Cannot find pinctrl active!\n");
		}
	}
	dpi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dpi->regs))
		return dev_err_probe(dev, PTR_ERR(dpi->regs),
				     "Failed to ioremap mem resource\n");

	dpi->engine_clk = devm_clk_get(dev, "engine");
	if (IS_ERR(dpi->engine_clk))
		return dev_err_probe(dev, PTR_ERR(dpi->engine_clk),
				     "Failed to get engine clock\n");

	dpi->pixel_clk = devm_clk_get(dev, "pixel");
	if (IS_ERR(dpi->pixel_clk))
		return dev_err_probe(dev, PTR_ERR(dpi->pixel_clk),
				     "Failed to get pixel clock\n");

	dpi->tvd_clk = devm_clk_get(dev, "pll");
	if (IS_ERR(dpi->tvd_clk))
		return dev_err_probe(dev, PTR_ERR(dpi->tvd_clk),
				     "Failed to get tvdpll clock\n");

	dpi->irq = platform_get_irq(pdev, 0);
	if (dpi->irq < 0)
		return dpi->irq;

	platform_set_drvdata(pdev, dpi);

	dpi->bridge.of_node = dev->of_node;
	dpi->bridge.type = DRM_MODE_CONNECTOR_DPI;

	ret = devm_drm_bridge_add(dev, &dpi->bridge);
	if (ret)
		return ret;

	ret = component_add(dev, &mtk_dpi_component_ops);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add component.\n");

	return 0;
}

static void mtk_dpi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_dpi_component_ops);
}

static const struct of_device_id mtk_dpi_of_ids[] = {
	{ .compatible = "mediatek,mt2701-dpi", .data = &mt2701_conf },
	{ .compatible = "mediatek,mt8173-dpi", .data = &mt8173_conf },
	{ .compatible = "mediatek,mt8183-dpi", .data = &mt8183_conf },
	{ .compatible = "mediatek,mt8186-dpi", .data = &mt8186_conf },
	{ .compatible = "mediatek,mt8188-dp-intf", .data = &mt8195_dpintf_conf },
	{ .compatible = "mediatek,mt8192-dpi", .data = &mt8192_conf },
	{ .compatible = "mediatek,mt8195-dp-intf", .data = &mt8195_dpintf_conf },
	{ .compatible = "mediatek,mt8195-dpi", .data = &mt8195_conf },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mtk_dpi_of_ids);

struct platform_driver mtk_dpi_driver = {
	.probe = mtk_dpi_probe,
	.remove = mtk_dpi_remove,
	.driver = {
		.name = "mediatek-dpi",
		.of_match_table = mtk_dpi_of_ids,
	},
};
