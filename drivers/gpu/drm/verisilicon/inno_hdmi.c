// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 *    Zheng Yang <zhengyang@rock-chips.com>
 *    Yakir Yang <ykk@rock-chips.com>
 */

#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hdmi.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/component.h>
#include <linux/reset.h>
#include <drm/drm_scdc_helper.h>
#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/gpio/consumer.h>

#include "vs_drv.h"

#include "inno_hdmi.h"

#define to_inno_hdmi(x)	container_of(x, struct inno_hdmi, x)

struct inno_hdmi_i2c {
	struct i2c_adapter adap;

	u8 ddc_addr;
	u8 segment_addr;

	struct mutex lock;
	struct completion cmp;
};

static const struct pre_pll_config pre_pll_cfg_table[] = {
	{ 25175000,  25175000, 1,  100, 2, 3, 3, 12, 3, 3, 4, 0, 0xF55555},
	{ 25200000,  25200000, 1,  100, 2, 3, 3, 12, 3, 3, 4, 0, 0},
	{ 27000000,  27000000, 1,  90, 3, 2, 2, 10, 3, 3, 4, 0, 0},
	{ 27027000,  27027000, 1,  90, 3, 2, 2, 10, 3, 3, 4, 0, 0x170A3D},
	{ 28320000,  28320000, 1,  28, 2, 1, 1,  3, 0, 3, 4, 0, 0x51EB85},
	{ 30240000,  30240000, 1,  30, 2, 1, 1,  3, 0, 3, 4, 0, 0x3D70A3},
	{ 31500000,  31500000, 1,  31, 2, 1, 1,  3, 0, 3, 4, 0, 0x7FFFFF},
	{ 33750000,  33750000, 1,  33, 2, 1, 1,  3, 0, 3, 4, 0, 0xCFFFFF},
	{ 36000000,  36000000, 1,  36, 2, 1, 1,  3, 0, 3, 4, 0, 0},
	{ 40000000,  40000000, 1,  80, 2, 2, 2, 12, 2, 2, 2, 0, 0},
	{ 46970000,  46970000, 1,  46, 2, 1, 1,  3, 0, 3, 4, 0, 0xF851EB},
	{ 49500000,  49500000, 1,  49, 2, 1, 1,  3, 0, 3, 4, 0, 0x7FFFFF},
	{ 49000000,  49000000, 1,  49, 2, 1, 1,  3, 0, 3, 4, 0, 0},
	{ 50000000,  50000000, 1,  50, 2, 1, 1,  3, 0, 3, 4, 0, 0},
	{ 54000000,  54000000, 1,  54, 2, 1, 1,  3, 0, 3, 4, 0, 0},
	{ 54054000,  54054000, 1,  54, 2, 1, 1,  3, 0, 3, 4, 0, 0x0DD2F1},
	{ 57284000,  57284000, 1,  57, 2, 1, 1,  3, 0, 3, 4, 0, 0x48B439},
	{ 58230000,  58230000, 1,  58, 2, 1, 1,  3, 0, 3, 4, 0, 0x3AE147},
	{ 59341000,  59341000, 1,  59, 2, 1, 1,  3, 0, 3, 4, 0, 0x574BC6},
	{ 59400000,  59400000, 1,  99, 3, 1, 1,  1, 3, 3, 4, 0, 0},
	{ 65000000,  65000000, 1, 130, 2, 2, 2,  12, 0, 2, 2, 0, 0},
	{ 68250000,  68250000, 1, 68,  2, 1, 1,  3,  0, 3, 4, 0, 0x3FFFFF},
	{ 71000000,  71000000, 1,  71, 2, 1, 1,  3, 0, 3,  4, 0, 0},
	{ 74176000,  74176000, 1,  98, 1, 2, 2,  1, 2, 3, 4, 0, 0xE6AE6B},
	{ 74250000,  74250000, 1,  99, 1, 2, 2,  1, 2, 3, 4, 0, 0},
	{ 75000000,  75000000, 1,  75, 2, 1, 1,  3, 0, 3, 4, 0, 0},
	{ 78750000,  78750000, 1,  78, 2, 1, 1,  3, 0, 3, 4, 0, 0xCFFFFF},
	{ 79500000,  79500000, 1,  79, 2, 1, 1,  3, 0, 3, 4, 0, 0x7FFFFF},
	{ 83500000,  83500000, 2, 167, 2, 1, 1,  1, 0, 0,  6, 0, 0},
	{ 83500000, 104375000, 1, 104, 2, 1, 1,  1, 1, 0,  5, 0, 0x600000},
	{ 85500000,  85500000, 1,  85, 2, 1, 1,  3, 0, 3,  4, 0, 0x7FFFFF},
	{ 85750000,  85750000, 1,  85, 2, 1, 1,  3, 0, 3,  4, 0, 0xCFFFFF},
	{ 85800000,  85800000, 1,  85, 2, 1, 1,  3, 0, 3,  4, 0, 0xCCCCCC},
	{ 88750000,  88750000, 1,  88, 2, 1, 1,  3, 0, 3,  4, 0, 0xCFFFFF},
	{ 89000000,  89000000, 1,  89, 2, 1, 1,  3, 0, 3, 4, 0, 0},
	{ 89910000,  89910000, 1,  89, 2, 1, 1,  3, 0, 3, 4, 0, 0xE8F5C1},
	{ 90000000,  90000000, 1,  90, 2, 1, 1,  3, 0, 3, 4, 0, 0},
	{ 99000000,  99000000, 1,  99, 2, 1, 1,  3, 0, 3, 4, 0, 0},
	{101000000, 101000000, 1, 101, 2, 1, 1,  3, 0, 3, 4, 0, 0},
	{102250000, 102250000, 1, 102, 2, 1, 1,  3, 0, 3, 4, 0, 0x3FFFFF},
	{106500000, 106500000, 1, 106, 2, 1, 1,  3, 0, 3, 4, 0, 0x7FFFFF},
	{108000000, 108000000, 1,  90, 3, 0, 0,  5, 0, 2,  2, 0, 0},
	{118800000, 118800000, 1, 118, 2, 1, 1,  3, 0, 3,  4, 0, 0xCCCCCC},
	{119000000, 119000000, 1, 119, 2, 1, 1,  3, 0, 3,  4, 0, 0},
	{131481000, 131481000, 1,  131, 2, 1, 1,  3, 0, 3,  4, 0, 0x7B22D1},
	{135000000, 135000000, 1,  135, 2, 1, 1,  3, 0, 3, 4, 0, 0},
	{136750000, 136750000, 1,  136, 2, 1, 1,  3, 0, 3, 4, 0, 0xCFFFFF},
	{147180000, 147180000, 1,  147, 2, 1, 1,  3, 0, 3, 4, 0, 0x2E147A},
	{148352000, 148352000, 1,  98, 1, 1, 1,  1, 2, 2, 2, 0, 0xE6AE6B},
	{148500000, 148500000, 1,  99, 1, 1, 1,  1, 2, 2, 2, 0, 0},
	{154000000, 154000000, 1, 154, 2, 1, 1,  3, 0, 3, 4, 0, 0},
	{156000000, 156000000, 1, 156, 2, 1, 1,  3, 0, 3, 4, 0, 0},
	{157000000, 157000000, 1, 157, 2, 1, 1,  3, 0, 3, 4, 0, 0},
	{162000000, 162000000, 1, 162, 2, 1, 1,  3, 0, 3, 4, 0, 0},
	{174250000, 174250000, 1, 145, 3, 0, 0,  5, 0, 2, 2, 0, 0x355555},
	{174500000, 174500000, 1, 174, 2, 1, 1,  3, 0, 3, 4, 0, 0x7FFFFF},
	{174570000, 174570000, 1, 174, 2, 1, 1,  3, 0, 3, 4, 0, 0x91EB84},
	{175500000, 175500000, 1, 175, 2, 1, 1,  3, 0, 3, 4, 0, 0x7FFFFF},
	{185590000, 185590000, 1, 185, 2, 1, 1,  3, 0, 3, 4, 0, 0x970A3C},
	{185625000, 185625000, 1, 185, 2, 1, 1,  3, 0, 3, 4, 0, 0xA00000},
	{187000000, 187000000, 1, 187, 2, 1, 1,  3, 0, 3, 4, 0, 0},
	{198000000, 198000000, 1, 198, 2, 1, 1,  3, 0, 3, 4, 0, 0},
	{241500000, 241500000, 1, 161, 1, 1, 1,  4, 0, 2,  2, 0, 0},
	{241700000, 241700000, 1, 241, 2, 1, 1,  3, 0, 3,  4, 0, 0xB33332},
	{262750000, 262750000, 1, 262, 2, 1, 1,  3, 0, 3,  4, 0, 0xCFFFFF},
	{296500000, 296500000, 1, 296, 2, 1, 1,  3, 0, 3,  4, 0, 0x7FFFFF},
	{296703000, 296703000, 1,  98, 0, 1, 1,  1, 0, 2,  2, 0, 0xE6AE6B},
	{297000000, 297000000, 1,  99, 0, 1, 1,  1, 0, 2,  2, 0, 0},
	{594000000, 594000000, 1,  99, 0, 2, 0,  1, 0, 1,  1, 0, 0},
	{0, 0, 0,  0, 0, 0, 0,  0, 0, 0,  0, 0, 0},
};

static const struct post_pll_config post_pll_cfg_table[] = {
	{25200000,	1, 80, 13, 3, 1},
	{27000000,	1, 40, 11, 3, 1},
	{27027000,	1, 40, 11, 3, 1},
	{33750000,	1, 40, 11, 3, 1},
	//{33750000,	1, 80, 8, 2},
	{49000000,	1, 20, 1, 3, 3},
	{65000000,	1, 20, 1, 3, 3},
	{74250000,	1, 20, 1, 3, 3},
	{88750000,  1, 20, 1, 3, 3},
	{108000000,  1, 20, 1, 3, 3},
	{148500000, 1, 20, 1, 3, 3},
	{162000000, 1, 20, 1, 3, 3},
	{174250000, 1, 20, 1, 3, 3},
	{187000000, 1, 20, 1, 3, 3},
	{241700000, 1, 20, 1, 3, 3},
	{297000000, 4, 20, 0, 0, 3},
	{594000000, 4, 20, 0, 0, 0},//postpll_postdiv_en = 0
	{ /* sentinel */ }
};

inline u8 hdmi_readb(struct inno_hdmi *hdmi, u16 offset)
{
	return readl_relaxed(hdmi->regs + (offset) * 0x04);
}

inline void hdmi_writeb(struct inno_hdmi *hdmi, u16 offset, u32 val)
{
	writel_relaxed(val, hdmi->regs + (offset) * 0x04);
}

inline void hdmi_modb(struct inno_hdmi *hdmi, u16 offset,
			     u32 msk, u32 val)
{
	u8 temp = hdmi_readb(hdmi, offset) & ~msk;

	temp |= val & msk;
	hdmi_writeb(hdmi, offset, temp);
}

static int inno_hdmi_enable_clk_deassert_rst(struct device *dev, struct inno_hdmi *hdmi)
{
	int ret;

	ret = clk_prepare_enable(hdmi->sys_clk);
	if (ret) {
		DRM_DEV_ERROR(dev,
			"Cannot enable HDMI sys clock: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(hdmi->mclk);
	if (ret) {
		DRM_DEV_ERROR(dev,
			"Cannot enable HDMI mclk clock: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(hdmi->bclk);
	if (ret) {
		DRM_DEV_ERROR(dev,
			"Cannot enable HDMI bclk clock: %d\n", ret);
		return ret;
	}
	ret = reset_control_deassert(hdmi->tx_rst);
	if (ret < 0) {
		dev_err(dev, "failed to deassert tx_rst\n");
		return ret;
	}
	return 0;
}

static void inno_hdmi_disable_clk_assert_rst(struct device *dev, struct inno_hdmi *hdmi)
{
	int ret;

	ret = reset_control_assert(hdmi->tx_rst);
	if (ret < 0)
		dev_err(dev, "failed to assert tx_rst\n");

	clk_disable_unprepare(hdmi->sys_clk);
	clk_disable_unprepare(hdmi->mclk);
	clk_disable_unprepare(hdmi->bclk);
}

#ifdef CONFIG_PM_SLEEP
static int hdmi_system_pm_suspend(struct device *dev)
{
	struct inno_hdmi *hdmi = dev_get_drvdata(dev);

	pm_runtime_force_suspend(dev);

	regulator_disable(hdmi->hdmi_1p8);
	udelay(100);
	regulator_disable(hdmi->hdmi_0p9);
	udelay(100);
	return 0;
}

static int hdmi_system_pm_resume(struct device *dev)
{
	struct inno_hdmi *hdmi = dev_get_drvdata(dev);
	int ret;
	//pmic turn on
	ret = regulator_enable(hdmi->hdmi_1p8);
	if (ret) {
		dev_err(dev, "Cannot enable hdmi_1p8 regulator\n");
		return ret;
	}
	udelay(100);
	ret = regulator_enable(hdmi->hdmi_0p9);
	if (ret) {
		dev_err(dev, "Cannot enable hdmi_0p9 regulator\n");
		return ret;
	}
	udelay(100);
	return pm_runtime_force_resume(dev);
}
#endif

#ifdef CONFIG_PM
static int hdmi_runtime_suspend(struct device *dev)
{
	struct inno_hdmi *hdmi = dev_get_drvdata(dev);

	inno_hdmi_disable_clk_assert_rst(dev, hdmi);

	return 0;
}

static int hdmi_runtime_resume(struct device *dev)
{
	struct inno_hdmi *hdmi = dev_get_drvdata(dev);

	return inno_hdmi_enable_clk_deassert_rst(dev, hdmi);
}
#endif

static void inno_hdmi_tx_phy_power_down(struct inno_hdmi *hdmi)
{	
	hdmi_writeb(hdmi, 0x00, 0x63);
}

static void inno_hdmi_config_pll(struct inno_hdmi *hdmi)
{
	u8 reg_1ad_value = hdmi->post_cfg->post_div_en ?
		 hdmi->post_cfg->postdiv : 0x00;
	u8 reg_1aa_value = hdmi->post_cfg->post_div_en ?
		 0x0e : 0x02;

	const reg_value_t cfg_pll_data[] = {
		{0x1a0, 0x01},
		{0x1aa, 0x0f},
		{0x1a1, hdmi->pre_cfg->prediv},
		{0x1a2, 0xf0 | hdmi->pre_cfg->fbdiv>>8},
		{0x1a3, hdmi->pre_cfg->fbdiv & 0xff},
		{0x1a4, ((hdmi->pre_cfg->tmds_div_a << 4) | (hdmi->pre_cfg->tmds_div_b << 2) | (hdmi->pre_cfg->tmds_div_c))},
		{0x1a5, (hdmi->pre_cfg->pclk_div_b << 5) | hdmi->pre_cfg->pclk_div_a},
		{0x1a6, (hdmi->pre_cfg->pclk_div_c << 5) | hdmi->pre_cfg->pclk_div_d},
		{0x1ab, hdmi->post_cfg->prediv},
		{0x1ac, hdmi->post_cfg->fbdiv & 0xff},
		{0x1ad, reg_1ad_value},
		{0x1aa, reg_1aa_value},//(9'h1aa,{4'b0000, 2'b11, 2'b10});
		//{0x1a0, 0x00},
	};

	int i;
	for (i = 0; i < sizeof(cfg_pll_data) / sizeof(reg_value_t); i++)
	{
		hdmi_writeb(hdmi, cfg_pll_data[i].reg, cfg_pll_data[i].value);
	}

	if (hdmi->pre_cfg->fracdiv){
		hdmi_writeb(hdmi, 0x1a2, 0xc0 | hdmi->pre_cfg->fbdiv>>8);
		hdmi_writeb(hdmi, 0x1d3, INNO_PRE_PLL_FRAC_DIV_7_0(hdmi->pre_cfg->fracdiv));
		hdmi_writeb(hdmi, 0x1d2, INNO_PRE_PLL_FRAC_DIV_15_8(hdmi->pre_cfg->fracdiv));
		hdmi_writeb(hdmi, 0x1d1, INNO_PRE_PLL_FRAC_DIV_23_16(hdmi->pre_cfg->fracdiv));
	}
	hdmi_writeb(hdmi, 0x1a0, 0);
	return;
}

static void inno_hdmi_tx_phy_power_on(struct inno_hdmi *hdmi)
{
	const reg_value_t pwon_data[] = {
		{0x00, 0x61},
	};
	int i;
	for (i = 0; i < sizeof(pwon_data)/sizeof(reg_value_t); i++) {
		hdmi_writeb(hdmi, pwon_data[i].reg, pwon_data[i].value);
	}
	return;
}

void inno_hdmi_tmds_driver_on(struct inno_hdmi *hdmi)
{
	hdmi_writeb(hdmi, 0x1b2, 0x8f);
}


static void inno_hdmi_i2c_init(struct inno_hdmi *hdmi)
{
	int ddc_bus_freq;

	ddc_bus_freq = (hdmi->tmds_rate >> 2) / HDMI_SCL_RATE;

	hdmi_writeb(hdmi, DDC_BUS_FREQ_L, ddc_bus_freq & 0xFF);
	hdmi_writeb(hdmi, DDC_BUS_FREQ_H, (ddc_bus_freq >> 8) & 0xFF);

	/* Clear the EDID interrupt flag and mute the interrupt */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_MASK1, 0);
	hdmi_writeb(hdmi, HDMI_INTERRUPT_STATUS1, m_INT_EDID_READY);
}

static void inno_hdmi_sys_power(struct inno_hdmi *hdmi, bool enable)
{
	if (enable)
		hdmi_modb(hdmi, HDMI_SYS_CTRL, m_POWER, v_PWR_ON);
	else
		hdmi_modb(hdmi, HDMI_SYS_CTRL, m_POWER, v_PWR_OFF);
}

static void inno_hdmi_set_pwr_mode(struct inno_hdmi *hdmi, int mode)
{
	switch (mode) {
	case NORMAL:
		inno_hdmi_sys_power(hdmi, true);
		break;

	case LOWER_PWR:
		inno_hdmi_sys_power(hdmi, false);
		break;

	default:
		DRM_DEV_ERROR(hdmi->dev, "Unknown power mode %d\n", mode);
	}
}

static const
struct pre_pll_config *inno_hdmi_phy_get_pre_pll_cfg(struct inno_hdmi *hdmi,
							  unsigned long rate)
{
	const struct pre_pll_config *cfg = pre_pll_cfg_table;
	rate = (rate / 1000) * 1000;

	for (; cfg->pixclock != 0; cfg++)
		if (cfg->tmdsclock == rate && cfg->pixclock == rate)
			break;

	if (cfg->pixclock == 0)
		return ERR_PTR(-EINVAL);

	return cfg;
}

static int inno_hdmi_phy_clk_set_rate(struct inno_hdmi *hdmi,unsigned long rate)
{
	unsigned long tmdsclock;
	hdmi->post_cfg = post_pll_cfg_table;

	tmdsclock = hdmi->tmds_rate;
	dev_info(hdmi->dev, "%s rate %lu tmdsclk %lu\n",__func__, rate, tmdsclock);

	hdmi->pre_cfg = inno_hdmi_phy_get_pre_pll_cfg(hdmi, tmdsclock);
	if (IS_ERR(hdmi->pre_cfg))
	return PTR_ERR(hdmi->pre_cfg);

	for (; hdmi->post_cfg->tmdsclock != 0; hdmi->post_cfg++)
		if (tmdsclock <= hdmi->post_cfg->tmdsclock)
			break;
	mdelay(100);

	dev_info(hdmi->dev, "%s hdmi->pre_cfg->pixclock = %lu\n",__func__, hdmi->pre_cfg->pixclock);

	inno_hdmi_config_pll(hdmi);

#if 0 //pre pll + post pll configire

	/*pre-pll power down*/
	hdmi_modb(hdmi, 0x1a0, INNO_PRE_PLL_POWER_DOWN, INNO_PRE_PLL_POWER_DOWN);

	/* Configure pre-pll */
	hdmi_modb(hdmi, 0x1a0, INNO_PCLK_VCO_DIV_5_MASK, INNO_PCLK_VCO_DIV_5(hdmi->pre_cfg->vco_div_5_en));
	hdmi_writeb(hdmi, 0x1a1, INNO_PRE_PLL_PRE_DIV(hdmi->pre_cfg->prediv));

	u32 val;
	val = INNO_SPREAD_SPECTRUM_MOD_DISABLE;
	if (!hdmi->pre_cfg->fracdiv)
		val |= INNO_PRE_PLL_FRAC_DIV_DISABLE;
	hdmi_writeb(hdmi, 0x1a2, INNO_PRE_PLL_FB_DIV_11_8(hdmi->pre_cfg->fbdiv | val));

	hdmi_writeb(hdmi, 0x1a3, INNO_PRE_PLL_FB_DIV_7_0(hdmi->pre_cfg->fbdiv));

	hdmi_writeb(hdmi, 0x1a5, INNO_PRE_PLL_PCLK_DIV_A(hdmi->pre_cfg->pclk_div_a) |
			INNO_PRE_PLL_PCLK_DIV_B(hdmi->pre_cfg->pclk_div_b));

	hdmi_writeb(hdmi, 0x1a6, INNO_PRE_PLL_PCLK_DIV_C(hdmi->pre_cfg->pclk_div_c) |
			INNO_PRE_PLL_PCLK_DIV_D(hdmi->pre_cfg->pclk_div_d));

	hdmi_writeb(hdmi, 0x1a4, INNO_PRE_PLL_TMDSCLK_DIV_C(hdmi->pre_cfg->tmds_div_c) |
			INNO_PRE_PLL_TMDSCLK_DIV_A(hdmi->pre_cfg->tmds_div_a) |
			INNO_PRE_PLL_TMDSCLK_DIV_B(hdmi->pre_cfg->tmds_div_b));

	hdmi_writeb(hdmi, 0x1d3, INNO_PRE_PLL_FRAC_DIV_7_0(hdmi->pre_cfg->fracdiv));
	hdmi_writeb(hdmi, 0x1d2, INNO_PRE_PLL_FRAC_DIV_15_8(hdmi->pre_cfg->fracdiv));
	hdmi_writeb(hdmi, 0x1d1, INNO_PRE_PLL_FRAC_DIV_23_16(hdmi->pre_cfg->fracdiv));

	/*pre-pll power down*/
	hdmi_modb(hdmi, 0x1a0, INNO_PRE_PLL_POWER_DOWN, 0);

	const struct phy_config *phy_cfg = inno_phy_cfg;

	for (; phy_cfg->tmdsclock != 0; phy_cfg++)
		if (tmdsclock <= phy_cfg->tmdsclock)
			break;

	hdmi_modb(hdmi, 0x1aa, INNO_POST_PLL_POWER_DOWN, INNO_POST_PLL_POWER_DOWN);

	hdmi_writeb(hdmi, 0x1ac, INNO_POST_PLL_FB_DIV_7_0(hdmi->post_cfg->fbdiv));

	if (hdmi->post_cfg->postdiv == 1) {
		hdmi_modb(hdmi, 0x1aa, INNO_POST_PLL_REFCLK_SEL_TMDS, INNO_POST_PLL_REFCLK_SEL_TMDS);
		hdmi_modb(hdmi, 0x1aa, BIT(4), INNO_POST_PLL_FB_DIV_8(hdmi->post_cfg->fbdiv));
		hdmi_modb(hdmi, 0x1ab, INNO_POST_PLL_Pre_DIV_MASK, INNO_POST_PLL_PRE_DIV(hdmi->post_cfg->prediv));
	} else {
		v = (hdmi->post_cfg->postdiv / 2) - 1;
		v &= INNO_POST_PLL_POST_DIV_MASK;
		hdmi_modb(hdmi, 0x1ad, INNO_POST_PLL_POST_DIV_MASK, v);
		hdmi_modb(hdmi, 0x1aa, BIT(4), INNO_POST_PLL_FB_DIV_8(hdmi->post_cfg->fbdiv));
		hdmi_modb(hdmi, 0x1ab, INNO_POST_PLL_Pre_DIV_MASK, INNO_POST_PLL_PRE_DIV(hdmi->post_cfg->prediv));
		hdmi_modb(hdmi, 0x1aa, INNO_POST_PLL_REFCLK_SEL_TMDS, INNO_POST_PLL_REFCLK_SEL_TMDS);
		hdmi_modb(hdmi, 0x1aa, INNO_POST_PLL_POST_DIV_ENABLE, INNO_POST_PLL_POST_DIV_ENABLE);
	}

	for (v = 0; v < 14; v++){
		hdmi_writeb(hdmi, 0x1b5 + v, phy_cfg->regs[v]);
	}

	if (phy_cfg->tmdsclock > 340000000) {
		/* Set termination resistor to 100ohm */
		v = clk_get_rate(hdmi->sys_clk) / 100000;
	
		hdmi_writeb(hdmi, 0x1c5, INNO_TERM_RESISTOR_CALIB_SPEED_14_8(v)
			   | INNO_BYPASS_TERM_RESISTOR_CALIB);
	
		hdmi_writeb(hdmi, 0x1c6, INNO_TERM_RESISTOR_CALIB_SPEED_7_0(v));
		hdmi_writeb(hdmi, 0x1c7, INNO_TERM_RESISTOR_100);		
		hdmi_modb(hdmi, 0x1c5, INNO_BYPASS_TERM_RESISTOR_CALIB, 0);
	} else {
		hdmi_writeb(hdmi, 0x1c5, INNO_BYPASS_TERM_RESISTOR_CALIB);

		/* clk termination resistor is 50ohm (parallel resistors) */
		if (phy_cfg->tmdsclock > 165000000){
			hdmi_modb(hdmi, 0x1c8,
					 INNO_ESD_DETECT_MASK,
					 INNO_TERM_RESISTOR_200);
		}
		/* data termination resistor for D2, D1 and D0 is 150ohm */
		for (v = 0; v < 3; v++){
			hdmi_modb(hdmi, 0x1c9 + v,
					 INNO_ESD_DETECT_MASK,
					 INNO_TERM_RESISTOR_200);
		}
	}

	hdmi_modb(hdmi, 0x1aa, INNO_POST_PLL_POWER_DOWN, 0);


#endif
	return 0;
}

static void inno_hdmi_improve_eye_diagram(struct inno_hdmi *hdmi)
{
	 switch (hdmi->hdmi_data.vic) {
	 case 95:
	 case 94:
	 case 93:
		 hdmi_writeb(hdmi, 0x100, 0x00);
		 hdmi_writeb(hdmi, 0x1bb, 0x40);
		 hdmi_writeb(hdmi, 0x1bc, 0x40);
		 hdmi_writeb(hdmi, 0x1bd, 0x40);
		 hdmi_writeb(hdmi, 0x1bf, 0x02);
		 hdmi_writeb(hdmi, 0x1c0, 0x22);
		 break;
	 case 16:
	 case 31:
		 hdmi_writeb(hdmi, 0x1bf, 0x02);
		 hdmi_writeb(hdmi, 0x1c0, 0x22);
		 break;
	 case 4:
	 case 3:
	 case 1:
		 hdmi_writeb(hdmi, 0x1bf, 0x00);
		 hdmi_writeb(hdmi, 0x1c0, 0x00);
		 break;

	 }
}

static int inno_hdmi_config_video_timing(struct inno_hdmi *hdmi,
				  struct drm_display_mode *mode)
{
	 int value;
	 /* Set detail external video timing */
	 value = mode->htotal;
	 hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HTOTAL_L, value & 0xFF);
	 hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HTOTAL_H, (value >> 8) & 0xFF);

	 value = mode->htotal - mode->hdisplay;
	 hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HBLANK_L, value & 0xFF);
	 hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HBLANK_H, (value >> 8) & 0xFF);

	 value = mode->htotal - mode->hsync_start;
	 hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HDELAY_L, value & 0xFF);
	 hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HDELAY_H, (value >> 8) & 0xFF);

	 value = mode->hsync_end - mode->hsync_start;
	 hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HDURATION_L, value & 0xFF);
	 hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HDURATION_H, (value >> 8) & 0xFF);

	 value = mode->vtotal;
	 hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VTOTAL_L, value & 0xFF);
	 hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VTOTAL_H, (value >> 8) & 0xFF);

	 value = mode->vtotal - mode->vdisplay;
	 hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VBLANK, value & 0xFF);

	 value = mode->vtotal - mode->vsync_start;
	 hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VDELAY, value & 0xFF);

	 value = mode->vsync_end - mode->vsync_start;
	 hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VDURATION, value & 0xFF);


	 /* Set detail external video timing polarity and interlace mode */
	 value = v_EXTERANL_VIDEO(1);
	 value |= mode->flags & DRM_MODE_FLAG_PHSYNC ?
		  v_HSYNC_POLARITY(1) : v_HSYNC_POLARITY(0);
	 value |= mode->flags & DRM_MODE_FLAG_PVSYNC ?
		  v_VSYNC_POLARITY(1) : v_VSYNC_POLARITY(0);
	 value |= mode->flags & DRM_MODE_FLAG_INTERLACE ?
		  v_INETLACE(1) : v_INETLACE(0);

	 hdmi_writeb(hdmi, HDMI_VIDEO_TIMING_CTL, value);
	 return 0;
}

static int inno_hdmi_setup(struct inno_hdmi *hdmi,
			   struct drm_display_mode *mode)
{
	u8 val;

	val = hdmi_readb(hdmi,0x1b0);
	val |= 0x4;
	hdmi_writeb(hdmi, 0x1b0, val);
	hdmi_writeb(hdmi, 0x1cc, 0xf);
	hdmi->hdmi_data.vic = drm_match_cea_mode(mode);

	hdmi->tmds_rate = mode->clock * 1000;
	inno_hdmi_phy_clk_set_rate(hdmi,hdmi->tmds_rate);

	while (!(hdmi_readb(hdmi, 0x1a9) & 0x1))
	;
	while (!(hdmi_readb(hdmi, 0x1af) & 0x1))
	;

	/*turn on LDO*/
	hdmi_writeb(hdmi, 0x1b4, 0x7);
	/*turn on serializer*/
	hdmi_writeb(hdmi, 0x1be, 0x71);
	inno_hdmi_improve_eye_diagram(hdmi);
	inno_hdmi_tx_phy_power_down(hdmi);

	inno_hdmi_config_video_timing(hdmi, mode);

	inno_hdmi_tx_phy_power_on(hdmi);
	inno_hdmi_tmds_driver_on(hdmi);

	hdmi_writeb(hdmi, 0xce, 0x0);
	hdmi_writeb(hdmi, 0xce, 0x1);

	return 0;
}


static void inno_hdmi_encoder_mode_set(struct drm_encoder *encoder,
				       struct drm_display_mode *mode,
				       struct drm_display_mode *adj_mode)
{
	struct inno_hdmi *hdmi = to_inno_hdmi(encoder);

	memcpy(&hdmi->previous_mode, adj_mode, sizeof(hdmi->previous_mode));
}

static void inno_hdmi_encoder_enable(struct drm_encoder *encoder)
{
	struct inno_hdmi *hdmi = to_inno_hdmi(encoder);
	int ret;

	ret = pm_runtime_get_sync(hdmi->dev);
	if (ret < 0)
		return;
	mdelay(10);
	inno_hdmi_setup(hdmi, &hdmi->previous_mode);

}

static void inno_hdmi_encoder_disable(struct drm_encoder *encoder)
{
	struct inno_hdmi *hdmi = to_inno_hdmi(encoder);

	inno_hdmi_set_pwr_mode(hdmi, LOWER_PWR);
	pm_runtime_put(hdmi->dev);

	return;
}

static bool inno_hdmi_encoder_mode_fixup(struct drm_encoder *encoder,
					 const struct drm_display_mode *mode,
					 struct drm_display_mode *adj_mode)
{
	return true;
}

static int
inno_hdmi_encoder_atomic_check(struct drm_encoder *encoder,
			       struct drm_crtc_state *crtc_state,
			       struct drm_connector_state *conn_state)
{
	return 0;
}

static const struct drm_encoder_helper_funcs inno_hdmi_encoder_helper_funcs = {
	.enable     = inno_hdmi_encoder_enable,
	.disable    = inno_hdmi_encoder_disable,
	.mode_fixup = inno_hdmi_encoder_mode_fixup,
	.mode_set   = inno_hdmi_encoder_mode_set,
	.atomic_check = inno_hdmi_encoder_atomic_check,
};

static enum drm_connector_status
inno_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	struct inno_hdmi *hdmi = to_inno_hdmi(connector);
	int ret;

	ret = pm_runtime_get_sync(hdmi->dev);
	if (ret < 0)
		return ret;
	mdelay(500);
	ret = (hdmi_readb(hdmi, HDMI_STATUS) & m_HOTPLUG) ?
		connector_status_connected : connector_status_disconnected;

	pm_runtime_put(hdmi->dev);

	return ret;
}

static int inno_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct inno_hdmi *hdmi = to_inno_hdmi(connector);
	struct edid *edid;
	int ret = 0;

	if (!hdmi->ddc)
		return 0;

	edid = drm_get_edid(connector, hdmi->ddc);
	if (edid) {
		hdmi->hdmi_data.sink_is_hdmi = drm_detect_hdmi_monitor(edid);
		hdmi->hdmi_data.sink_has_audio = drm_detect_monitor_audio(edid);
		drm_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}

	return ret;
}

static enum drm_mode_status
inno_hdmi_connector_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	const struct pre_pll_config *cfg = pre_pll_cfg_table;
	int pclk = mode->clock * 1000;
	bool valid = false;
	int i;

	for (i = 0; cfg[i].pixclock != (~0UL); i++) {
		if (pclk == cfg[i].pixclock) {
			if (pclk > 297000 * 1000) {
				continue;
			}
			valid = true;
			break;
		}
	}

	return (valid) ? MODE_OK : MODE_BAD;
}


static int
inno_hdmi_probe_single_connector_modes(struct drm_connector *connector,
				       uint32_t maxX, uint32_t maxY)
{
	return drm_helper_probe_single_connector_modes(connector, 3840, 2160);
}

static void inno_hdmi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs inno_hdmi_connector_funcs = {
	.fill_modes = inno_hdmi_probe_single_connector_modes,
	.detect = inno_hdmi_connector_detect,
	.destroy = inno_hdmi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct drm_connector_helper_funcs inno_hdmi_connector_helper_funcs = {
	.get_modes = inno_hdmi_connector_get_modes,
	.mode_valid = inno_hdmi_connector_mode_valid,
};

static int inno_hdmi_register(struct drm_device *drm, struct inno_hdmi *hdmi)
{
	struct drm_encoder *encoder = &hdmi->encoder;
	struct device *dev = hdmi->dev;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);

	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	drm_encoder_helper_add(encoder, &inno_hdmi_encoder_helper_funcs);
	drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_TMDS);

	hdmi->connector.polled = DRM_CONNECTOR_POLL_HPD;

	drm_connector_helper_add(&hdmi->connector,
				 &inno_hdmi_connector_helper_funcs);
	drm_connector_init_with_ddc(drm, &hdmi->connector,
				    &inno_hdmi_connector_funcs,
				    DRM_MODE_CONNECTOR_HDMIA,
				    hdmi->ddc);

	drm_connector_attach_encoder(&hdmi->connector, encoder);

	return 0;
}

static irqreturn_t inno_hdmi_i2c_irq(struct inno_hdmi *hdmi)
{
	struct inno_hdmi_i2c *i2c = hdmi->i2c;
	u8 stat;

	stat = hdmi_readb(hdmi, HDMI_INTERRUPT_STATUS1);
	if (!(stat & m_INT_EDID_READY))
		return IRQ_NONE;

	/* Clear HDMI EDID interrupt flag */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_STATUS1, m_INT_EDID_READY);

	complete(&i2c->cmp);

	return IRQ_HANDLED;
}

static irqreturn_t inno_hdmi_hardirq(int irq, void *dev_id)
{
	struct inno_hdmi *hdmi = dev_id;
	irqreturn_t ret = IRQ_NONE;
	u8 interrupt;

	ret = inno_hdmi_i2c_irq(hdmi);

	interrupt = hdmi_readb(hdmi, HDMI_STATUS);
	if (interrupt & m_INT_HOTPLUG) {
		hdmi_modb(hdmi, HDMI_STATUS, m_INT_HOTPLUG, m_INT_HOTPLUG);
	}

	return ret;
}

static irqreturn_t inno_hdmi_gpio_hardirq(int irq, void *dev_id)
{
	return IRQ_WAKE_THREAD;
}

static irqreturn_t inno_hdmi_gpio_irq(int irq, void *dev_id)
{
	struct inno_hdmi *hdmi = dev_id;

	drm_helper_hpd_irq_event(hdmi->connector.dev);

	return IRQ_HANDLED;
}

static int inno_hdmi_i2c_read(struct inno_hdmi *hdmi, struct i2c_msg *msgs)
{
	int length = msgs->len;
	u8 *buf = msgs->buf;
	int ret;

	ret = wait_for_completion_timeout(&hdmi->i2c->cmp, HZ / 10);
	if (!ret)
		return -EAGAIN;

	while (length--)
		*buf++ = hdmi_readb(hdmi, HDMI_EDID_FIFO_ADDR);

	return 0;
}

static int inno_hdmi_i2c_write(struct inno_hdmi *hdmi, struct i2c_msg *msgs)
{
	/*
	 * The DDC module only support read EDID message, so
	 * we assume that each word write to this i2c adapter
	 * should be the offset of EDID word address.
	 */
	if ((msgs->len != 1) ||
	    ((msgs->addr != DDC_ADDR) && (msgs->addr != DDC_SEGMENT_ADDR)))
		return -EINVAL;

	reinit_completion(&hdmi->i2c->cmp);

	if (msgs->addr == DDC_SEGMENT_ADDR)
		hdmi->i2c->segment_addr = msgs->buf[0];
	if (msgs->addr == DDC_ADDR)
		hdmi->i2c->ddc_addr = msgs->buf[0];

	/* Set edid fifo first addr */
	hdmi_writeb(hdmi, HDMI_EDID_FIFO_OFFSET, 0x00);

	/* Set edid word address 0x00/0x80 */
	hdmi_writeb(hdmi, HDMI_EDID_WORD_ADDR, hdmi->i2c->ddc_addr);

	/* Set edid segment pointer */
	hdmi_writeb(hdmi, HDMI_EDID_SEGMENT_POINTER, hdmi->i2c->segment_addr);

	return 0;
}

static int inno_hdmi_i2c_xfer(struct i2c_adapter *adap,
			      struct i2c_msg *msgs, int num)
{
	struct inno_hdmi *hdmi = i2c_get_adapdata(adap);
	struct inno_hdmi_i2c *i2c = hdmi->i2c;
	int i, ret = 0;

	mutex_lock(&i2c->lock);

	/* Clear the EDID interrupt flag and unmute the interrupt */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_MASK1, m_INT_EDID_READY);
	hdmi_writeb(hdmi, HDMI_INTERRUPT_STATUS1, m_INT_EDID_READY);

	for (i = 0; i < num; i++) {
		DRM_DEV_DEBUG(hdmi->dev,
			      "xfer: num: %d/%d, len: %d, flags: %#x\n",
			      i + 1, num, msgs[i].len, msgs[i].flags);

		if (msgs[i].flags & I2C_M_RD)
			ret = inno_hdmi_i2c_read(hdmi, &msgs[i]);
		else
			ret = inno_hdmi_i2c_write(hdmi, &msgs[i]);

		if (ret < 0)
			break;
	}

	if (!ret)
		ret = num;

	/* Mute HDMI EDID interrupt */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_MASK1, 0);

	mutex_unlock(&i2c->lock);

	return ret;
}

static u32 inno_hdmi_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm inno_hdmi_algorithm = {
	.master_xfer	= inno_hdmi_i2c_xfer,
	.functionality	= inno_hdmi_i2c_func,
};

static struct i2c_adapter *inno_hdmi_i2c_adapter(struct inno_hdmi *hdmi)
{
	struct i2c_adapter *adap;
	struct inno_hdmi_i2c *i2c;
	int ret;

	i2c = devm_kzalloc(hdmi->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return ERR_PTR(-ENOMEM);

	mutex_init(&i2c->lock);
	init_completion(&i2c->cmp);

	adap = &i2c->adap;
	adap->class = I2C_CLASS_DDC;
	adap->owner = THIS_MODULE;
	adap->dev.parent = hdmi->dev;
	adap->algo = &inno_hdmi_algorithm;
	strlcpy(adap->name, "Inno HDMI", sizeof(adap->name));
	i2c_set_adapdata(adap, hdmi);

	ret = i2c_add_adapter(adap);
	if (ret) {
		dev_warn(hdmi->dev, "cannot add %s I2C adapter\n", adap->name);
		devm_kfree(hdmi->dev, i2c);
		return ERR_PTR(ret);
	}

	hdmi->i2c = i2c;

	DRM_DEV_INFO(hdmi->dev, "registered %s I2C bus driver success\n", adap->name);

	return adap;
}

static int inno_hdmi_get_clk_rst(struct device *dev, struct inno_hdmi *hdmi)
{
	hdmi->sys_clk = devm_clk_get(dev, "sysclk");
	if (IS_ERR(hdmi->sys_clk)) {
		DRM_DEV_ERROR(dev, "Unable to get HDMI sysclk clk\n");
		return PTR_ERR(hdmi->sys_clk);
	}
	hdmi->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(hdmi->mclk)) {
		DRM_DEV_ERROR(dev, "Unable to get HDMI mclk clk\n");
		return PTR_ERR(hdmi->mclk);
	}
	hdmi->bclk = devm_clk_get(dev, "bclk");
	if (IS_ERR(hdmi->bclk)) {
		DRM_DEV_ERROR(dev, "Unable to get HDMI bclk clk\n");
		return PTR_ERR(hdmi->bclk);
	}
	hdmi->tx_rst = reset_control_get_shared(dev, "hdmi_tx");
	if (IS_ERR(hdmi->tx_rst)) {
		DRM_DEV_ERROR(dev, "Unable to get HDMI tx rst\n");
		return PTR_ERR(hdmi->tx_rst);
	}
	return 0;
}


static int inno_hdmi_bind(struct device *dev, struct device *master,
				 void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct inno_hdmi *hdmi;
	struct resource *iores;
	int irq;
	int ret;

	dev_info(dev, "inno hdmi bind begin\n");

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->dev = dev;
	hdmi->drm_dev = drm;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hdmi->regs = devm_ioremap_resource(dev, iores);
	if (IS_ERR(hdmi->regs))
		return PTR_ERR(hdmi->regs);

	hdmi->hdmi_1p8 = devm_regulator_get(dev, "hdmi_1p8");
	if (IS_ERR(hdmi->hdmi_1p8))
		return PTR_ERR(hdmi->hdmi_1p8);

	hdmi->hdmi_0p9 = devm_regulator_get(dev, "hdmi_0p9");
	if (IS_ERR(hdmi->hdmi_0p9))
		return PTR_ERR(hdmi->hdmi_0p9);

	//pmic turn on
	ret = regulator_enable(hdmi->hdmi_1p8);
	if (ret) {
		dev_err(dev, "Cannot enable hdmi_1p8 regulator\n");
		goto err_reg_1p8;
	}
	udelay(100);
	ret = regulator_enable(hdmi->hdmi_0p9);
	if (ret) {
		dev_err(dev, "Cannot enable hdmi_0p9 regulator\n");
		goto err_reg_0p9;
	}
	udelay(100);

	ret = inno_hdmi_get_clk_rst(dev, hdmi);
	ret = inno_hdmi_enable_clk_deassert_rst(dev, hdmi);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_disable_clk;
	}

	hdmi->hpd_gpio = devm_gpiod_get(dev, "hpd", GPIOD_IN);
	if (IS_ERR(hdmi->hpd_gpio)) {
		DRM_DEV_ERROR(dev, "cannot get hpd gpio property\n");
		return PTR_ERR(hdmi->hpd_gpio);
	}

	hdmi->irq = gpiod_to_irq(hdmi->hpd_gpio);
	if (hdmi->irq < 0) {
		DRM_DEV_ERROR(dev, "failed to get GPIO irq\n");
		return	hdmi->irq;
	}

	hdmi->ddc = inno_hdmi_i2c_adapter(hdmi);
	if (IS_ERR(hdmi->ddc)) {
		ret = PTR_ERR(hdmi->ddc);
		hdmi->ddc = NULL;
		goto err_disable_clk;
	}

	hdmi->tmds_rate = 51200000;

	inno_hdmi_i2c_init(hdmi);

	ret = inno_hdmi_register(drm, hdmi);
	if (ret)
		goto err_put_adapter;

	dev_set_drvdata(dev, hdmi);

	/* Unmute hotplug interrupt */
	hdmi_modb(hdmi, HDMI_STATUS, m_MASK_INT_HOTPLUG, v_MASK_INT_HOTPLUG(1));

	ret = devm_request_threaded_irq(dev, irq, inno_hdmi_hardirq,
					NULL, IRQF_SHARED,
					dev_name(dev), hdmi);
	if (ret < 0)
		goto err_cleanup_hdmi;

	ret = devm_request_threaded_irq(dev, hdmi->irq, inno_hdmi_gpio_hardirq,
		inno_hdmi_gpio_irq, IRQF_TRIGGER_RISING |
		IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
		"hdmi", hdmi);
	if (ret < 0)
		goto err_cleanup_hdmi;

	ret = starfive_hdmi_audio_init(hdmi);
	if (ret)
		dev_err(dev, "failed to audio init\n");

	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 5000);
	pm_runtime_enable(&pdev->dev);

	inno_hdmi_disable_clk_assert_rst(dev, hdmi);

	dev_info(dev, "inno hdmi bind end\n");

	return 0;
err_cleanup_hdmi:
	hdmi->connector.funcs->destroy(&hdmi->connector);
	hdmi->encoder.funcs->destroy(&hdmi->encoder);
err_put_adapter:
	i2c_put_adapter(hdmi->ddc);
err_disable_clk:
	//clk_disable_unprepare(hdmi->pclk);
err_reg_0p9:
	regulator_disable(hdmi->hdmi_1p8);
err_reg_1p8:
	return ret;
}

static void inno_hdmi_unbind(struct device *dev, struct device *master,
			     void *data)
{
	struct inno_hdmi *hdmi = dev_get_drvdata(dev);

	hdmi->connector.funcs->destroy(&hdmi->connector);
	hdmi->encoder.funcs->destroy(&hdmi->encoder);

	i2c_put_adapter(hdmi->ddc);

	inno_hdmi_disable_clk_assert_rst(dev, hdmi);

	regulator_disable(hdmi->hdmi_1p8);
	udelay(100);
	regulator_disable(hdmi->hdmi_0p9);
}

static const struct component_ops inno_hdmi_ops = {
	.bind	= inno_hdmi_bind,
	.unbind	= inno_hdmi_unbind,
};

static int inno_hdmi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &inno_hdmi_ops);
}

static int inno_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &inno_hdmi_ops);

	return 0;
}

static const struct dev_pm_ops hdmi_pm_ops = {
	SET_RUNTIME_PM_OPS(hdmi_runtime_suspend, hdmi_runtime_resume, NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(hdmi_system_pm_suspend, hdmi_system_pm_resume)
};

static const struct of_device_id inno_hdmi_dt_ids[] = {
	{ .compatible = "inno,hdmi",
	},
	{},
};
MODULE_DEVICE_TABLE(of, inno_hdmi_dt_ids);

struct platform_driver inno_hdmi_driver = {
	.probe  = inno_hdmi_probe,
	.remove = inno_hdmi_remove,
	.driver = {
		.name = "innohdmi-starfive",
		.of_match_table = inno_hdmi_dt_ids,
		.pm = &hdmi_pm_ops,
	},
};
