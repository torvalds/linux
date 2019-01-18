// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/iopoll.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/mfd/syscon.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drmP.h>
#include <uapi/linux/videodev2.h>
#include <video/mipi_display.h>
#include <asm/unaligned.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"
#include "rockchip-mipi-csi-tx.h"

#define DSI_PHY_TMR_LPCLK_CFG		0x98
#define PHY_CLKHS2LP_TIME(lbcc)		(((lbcc) & 0x3ff) << 16)
#define PHY_CLKLP2HS_TIME(lbcc)		((lbcc) & 0x3ff)

#define DSI_PHY_TMR_CFG			0x9c
#define PHY_HS2LP_TIME(lbcc)		(((lbcc) & 0xff) << 24)
#define PHY_LP2HS_TIME(lbcc)		(((lbcc) & 0xff) << 16)
#define MAX_RD_TIME(lbcc)		((lbcc) & 0x7fff)

#define DSI_PHY_RSTZ			0xa0
#define PHY_DISFORCEPLL			0
#define PHY_ENFORCEPLL			BIT(3)
#define PHY_DISABLECLK			0
#define PHY_ENABLECLK			BIT(2)
#define PHY_RSTZ			0
#define PHY_UNRSTZ			BIT(1)
#define PHY_SHUTDOWNZ			0
#define PHY_UNSHUTDOWNZ			BIT(0)
#define DSI_PHY_TST_CTRL0		0xb4
#define PHY_TESTCLK			BIT(1)
#define PHY_UNTESTCLK			0
#define PHY_TESTCLR			BIT(0)
#define PHY_UNTESTCLR			0

#define DSI_PHY_TST_CTRL1		0xb8
#define PHY_TESTEN			BIT(16)
#define PHY_UNTESTEN			0
#define PHY_TESTDOUT(n)			(((n) & 0xff) << 8)
#define PHY_TESTDIN(n)			(((n) & 0xff) << 0)
#define BYPASS_VCO_RANGE	BIT(7)
#define VCO_RANGE_CON_SEL(val)	(((val) & 0x7) << 3)
#define VCO_IN_CAP_CON_DEFAULT	(0x0 << 1)
#define VCO_IN_CAP_CON_LOW	(0x1 << 1)
#define VCO_IN_CAP_CON_HIGH	(0x2 << 1)
#define REF_BIAS_CUR_SEL	BIT(0)

#define CP_CURRENT_3MA		BIT(3)
#define CP_PROGRAM_EN		BIT(7)
#define LPF_PROGRAM_EN		BIT(6)
#define LPF_RESISTORS_20_KOHM	0
#define HSFREQRANGE_SEL(val)	(((val) & 0x3f) << 1)

#define INPUT_DIVIDER(val)	(((val) - 1) & 0x7f)
#define LOW_PROGRAM_EN		0
#define HIGH_PROGRAM_EN		BIT(7)
#define LOOP_DIV_LOW_SEL(val)	(((val) - 1) & 0x1f)
#define LOOP_DIV_HIGH_SEL(val)	((((val) - 1) >> 5) & 0x1f)
#define PLL_LOOP_DIV_EN		BIT(5)
#define PLL_INPUT_DIV_EN	BIT(4)

#define POWER_CONTROL		BIT(6)
#define INTERNAL_REG_CURRENT	BIT(3)
#define BIAS_BLOCK_ON		BIT(2)
#define BANDGAP_ON		BIT(0)

#define TER_RESISTOR_HIGH	BIT(7)
#define	TER_RESISTOR_LOW	0
#define LEVEL_SHIFTERS_ON	BIT(6)
#define TER_CAL_DONE		BIT(5)
#define SETRD_MAX		(0x7 << 2)
#define POWER_MANAGE		BIT(1)
#define TER_RESISTORS_ON	BIT(0)

#define BIASEXTR_SEL(val)	((val) & 0x7)
#define BANDGAP_SEL(val)	((val) & 0x7)
#define TLP_PROGRAM_EN		BIT(7)
#define THS_PRE_PROGRAM_EN	BIT(7)
#define THS_ZERO_PROGRAM_EN	BIT(6)

#define FPGA_DSI_PHY_TST_READ		0x18
#define FPGA_DSI_PHY_TST_CTRL0		0x20

/* #define FPGA_PLATFORM_TEST		1 */

struct dphy_pll_testdin_map {
	unsigned int max_mbps;
	u8 testdin;
};

/* The table is based on 27MHz DPHY pll reference clock. */
static const struct dphy_pll_testdin_map dp_tdin_map[] = {
	{  90, 0x00}, { 100, 0x10}, { 110, 0x20}, { 130, 0x01},
	{ 140, 0x11}, { 150, 0x21}, { 170, 0x02}, { 180, 0x12},
	{ 200, 0x22}, { 220, 0x03}, { 240, 0x13}, { 250, 0x23},
	{ 270, 0x04}, { 300, 0x14}, { 330, 0x05}, { 360, 0x15},
	{ 400, 0x25}, { 450, 0x06}, { 500, 0x16}, { 550, 0x07},
	{ 600, 0x17}, { 650, 0x08}, { 700, 0x18}, { 750, 0x09},
	{ 800, 0x19}, { 850, 0x29}, { 900, 0x39}, { 950, 0x0a},
	{1000, 0x1a}, {1050, 0x2a}, {1100, 0x3a}, {1150, 0x0b},
	{1200, 0x1b}, {1250, 0x2b}, {1300, 0x3b}, {1350, 0x0c},
	{1400, 0x1c}, {1450, 0x2c}, {1500, 0x3c}
};

enum {
	BANDGAP_97_07,
	BANDGAP_98_05,
	BANDGAP_99_02,
	BANDGAP_100_00,
	BANDGAP_93_17,
	BANDGAP_94_15,
	BANDGAP_95_12,
	BANDGAP_96_10,
};

enum {
	BIASEXTR_87_1,
	BIASEXTR_91_5,
	BIASEXTR_95_9,
	BIASEXTR_100,
	BIASEXTR_105_94,
	BIASEXTR_111_88,
	BIASEXTR_118_8,
	BIASEXTR_127_7,
};

static const char * const csi_tx_intr[] = {
	"RX frame start interrupt status!",
	"RX frame end interrupt status!",
	"RX line end interrupt status!",
	"TX frame start interrupt status!",
	"TX frame end interrupt status!",
	"TX line end interrupt status!",
	"Line flag0 interrupt status!",
	"Line flag1 interrupt status!",
	"PHY stopstate interrupt status!",
	"PHY PLL lock interrupt status!",
	"CSITX idle interrupt status!"
};

static const char * const csi_tx_err_intr[] = {
	"IDI header fifo overflow raw interrupt!",
	"IDI header fifo underflow raw interrupt!",
	"IDI payload fifo overflow raw interrupt!",
	"IDI payload fifo underflow raw interrupt!",
	"Header fifo overflow raw interrupt!",
	"Header fifo underflow raw interrupt!",
	"Payload fifo overflow raw interrupt!",
	"Payload fifo underflow raw interrupt!",
	"Output fifo overflow raw interrupt!",
	"Output fifo underflow raw interrupt!",
	"Txreadyhs error0 raw interrupt!",
	"Txreadyhs error1 raw interrupt!"
};

static void
grf_field_write(struct rockchip_mipi_csi *csi, enum grf_reg_fields index,
		unsigned int val)
{
	const u32 field = csi->pdata->csi0_grf_reg_fields[index];
	u16 reg;
	u8 msb, lsb;

	if (!field || !csi->grf)
		return;

	reg = (field >> 16) & 0xffff;
	lsb = (field >>  8) & 0xff;
	msb = (field >>  0) & 0xff;

	regmap_write(csi->grf, reg, (val << lsb) | (GENMASK(msb, lsb) << 16));
}

static inline void csi_writel(struct rockchip_mipi_csi *csi, u32 offset, u32 v)
{
	writel(v, csi->regs + offset);
	csi->regsbak[offset >> 2] = v;
}

static inline u32 csi_readl(struct rockchip_mipi_csi *csi, u32 offset)
{
	return readl(csi->regs + offset);
}

static inline void csi_mask_write(struct rockchip_mipi_csi *csi, u32 offset,
				  u32 mask, u32 val, bool regbak)
{
	u32 v;
	u32 cached_val = csi->regsbak[offset >> 2];

	v = (cached_val & ~(mask)) | val;

	if (regbak)
		csi_writel(csi, offset, v);
	else
		writel(v, csi->regs + offset);
}

static int phy_max_mbps_to_testdin(unsigned int max_mbps)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dp_tdin_map); i++)
		if (dp_tdin_map[i].max_mbps > max_mbps)
			return dp_tdin_map[i].testdin;

	return -EINVAL;
}

static inline struct rockchip_mipi_csi *host_to_csi(struct mipi_dsi_host *host)
{
	return container_of(host, struct rockchip_mipi_csi, dsi_host);
}

static inline struct rockchip_mipi_csi *con_to_csi(struct drm_connector *con)
{
	return container_of(con, struct rockchip_mipi_csi, connector);
}

static inline
struct rockchip_mipi_csi *encoder_to_csi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct rockchip_mipi_csi, encoder);
}

static void rockchip_mipi_csi_phy_write(struct rockchip_mipi_csi *csi,
					u8 test_code, u8 test_data)
{
	/*
	 * With the falling edge on TESTCLK, the TESTDIN[7:0] signal content
	 * is latched internally as the current test code. Test data is
	 * programmed internally by rising edge on TESTCLK.
	 */
	writel(0x01000100, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
	writel(0x00ff0000 | test_code,
	       csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
	writel(0x02000200, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
	writel(0x01000000, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
	writel(0x02000000, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
	writel(0x01000000, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
	writel(0x00ff0000 | test_data,
	       csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
	writel(0x01000100, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
}

static u8 __maybe_unused
rockchip_mipi_csi_phy_read(struct rockchip_mipi_csi *csi, u8 test_code)
{
	u8 val;

	writel(0x02ff0200 | test_code,
	       csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
	writel(0x01000000, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);

	val = readl(csi->test_code_regs + FPGA_DSI_PHY_TST_READ) & 0xff;
	writel(0x03000100, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);

	return val;
}

static void rockchip_bidir4l_board_phy_reset(struct rockchip_mipi_csi *csi)
{
	writel(0x04000000, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
	writel(0x08000000, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
	writel(0x01000100, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
	writel(0x80008000, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
	writel(0x80000000, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
	writel(0x40004000, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
}

static void rockchip_bidir4l_board_phy_enable(struct rockchip_mipi_csi *csi)
{
	writel(0x01000100, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
	writel(0x02000000, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
	writel(0x08000800, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
	writel(0x04000400, csi->test_code_regs + FPGA_DSI_PHY_TST_CTRL0);
}

static void rockchip_mipi_csi_irq_init(struct rockchip_mipi_csi *csi)
{
	/* enable csi err irq */
	writel(m_ERR_INTR_EN | m_ERR_INTR_MASK, csi->regs + CSITX_ERR_INTR_EN);

	/* disable csi frame end tx irq */
	writel(m_FRM_END_TX | v_FRM_END_TX(0), csi->regs + CSITX_INTR_EN);
}

static void rockchip_mipi_csi_irq_disable(struct rockchip_mipi_csi *csi)
{
	/* disable csi err irq */
	writel(m_ERR_INTR_MASK, csi->regs + CSITX_ERR_INTR_EN);

	/* disable csi tx irq */
	writel(m_INTR_MASK, csi->regs + CSITX_INTR_EN);
}

static int rockchip_mipi_dphy_power_on(struct rockchip_mipi_csi *csi)
{
	if (csi->dphy.phy)
		phy_power_on(csi->dphy.phy);

	udelay(10);

	return 0;
}

static void rockchip_mipi_dphy_power_off(struct rockchip_mipi_csi *csi)
{
	if (csi->dphy.phy)
		phy_power_off(csi->dphy.phy);
}

static void rockchip_mipi_csi_tx_en(struct rockchip_mipi_csi *csi)
{
	u32 mask, val;

	/* enable csi tx, dphy and config lane num */
	mask = m_CSITX_EN | m_DPHY_EN | m_LANE_NUM;
	val = v_CSITX_EN(1) | v_DPHY_EN(1) | v_LANE_NUM(csi->lanes - 1);
	csi_mask_write(csi, CSITX_ENABLE, mask, val, true);
}

static void rockchip_mipi_csi_host_power_on(struct rockchip_mipi_csi *csi)
{
	u32 mask, val;

	rockchip_mipi_csi_tx_en(csi);
	rockchip_mipi_csi_irq_init(csi);

	mask = m_CONFIG_DONE | m_CONFIG_DONE_IMD | m_CONFIG_DONE_MODE;
	val = v_CONFIG_DONE(0) | v_CONFIG_DONE_IMD(1) | v_CONFIG_DONE_MODE(0);
	csi_mask_write(csi, CSITX_CONFIG_DONE, mask, val, false);
}

static void rockchip_mipi_csi_host_power_off(struct rockchip_mipi_csi *csi)
{
	u32 mask, val;

	rockchip_mipi_csi_irq_disable(csi);

	/* disable csi tx, dphy and config lane num */
	mask = m_CSITX_EN | m_DPHY_EN;
	val = v_CSITX_EN(0) | v_DPHY_EN(0);
	csi_mask_write(csi, CSITX_ENABLE, mask, val, true);
	csi_mask_write(csi, CSITX_CONFIG_DONE, m_CONFIG_DONE,
		       v_CONFIG_DONE(1), false);
}

static void rockchip_mipi_csi_phy_pll_init(struct rockchip_mipi_csi *csi)
{
	rockchip_mipi_csi_phy_write(csi, 0x17,
				    INPUT_DIVIDER(csi->dphy.input_div));
	rockchip_mipi_csi_phy_write(csi, 0x18,
				    LOOP_DIV_LOW_SEL(csi->dphy.feedback_div) |
				    LOW_PROGRAM_EN);
	rockchip_mipi_csi_phy_write(csi, 0x19,
				    PLL_LOOP_DIV_EN | PLL_INPUT_DIV_EN);
	rockchip_mipi_csi_phy_write(csi, 0x18,
				    LOOP_DIV_HIGH_SEL(csi->dphy.feedback_div) |
				    HIGH_PROGRAM_EN);
	rockchip_mipi_csi_phy_write(csi, 0x19,
				    PLL_LOOP_DIV_EN | PLL_INPUT_DIV_EN);
}

static int __maybe_unused
rockchip_mipi_csi_phy_init(struct rockchip_mipi_csi *csi)
{
	int testdin, vco;

	vco = (csi->lane_mbps < 200) ? 0 : (csi->lane_mbps + 100) / 200;

	testdin = phy_max_mbps_to_testdin(csi->lane_mbps);
	if (testdin < 0) {
		dev_err(csi->dev,
			"failed to get testdin for %dmbps lane clock\n",
			csi->lane_mbps);
		return testdin;
	}

	rockchip_mipi_csi_phy_write(csi, 0xb0, 0x01);
	rockchip_mipi_csi_phy_write(csi, 0xac, csi->lanes - 1);
	rockchip_mipi_csi_phy_write(csi, 0xb1, 0x00);
	rockchip_mipi_csi_phy_write(csi, 0xb2, 0x00);
	rockchip_mipi_csi_phy_write(csi, 0xb3, 0x00);
	rockchip_mipi_csi_phy_write(csi, 0x10, BYPASS_VCO_RANGE |
					 VCO_RANGE_CON_SEL(vco) |
					 VCO_IN_CAP_CON_LOW |
					 REF_BIAS_CUR_SEL);

	rockchip_mipi_csi_phy_write(csi, 0x11, CP_CURRENT_3MA);
	rockchip_mipi_csi_phy_write(csi, 0x12, CP_PROGRAM_EN | LPF_PROGRAM_EN |
					 LPF_RESISTORS_20_KOHM);

	rockchip_mipi_csi_phy_write(csi, 0x44, HSFREQRANGE_SEL(testdin));

	rockchip_mipi_csi_phy_pll_init(csi);

	rockchip_mipi_csi_phy_write(csi, 0x20, POWER_CONTROL |
					INTERNAL_REG_CURRENT | BIAS_BLOCK_ON |
					BANDGAP_ON);

	rockchip_mipi_csi_phy_write(csi, 0x21, TER_RESISTOR_LOW | TER_CAL_DONE |
					 SETRD_MAX | TER_RESISTORS_ON);
	rockchip_mipi_csi_phy_write(csi, 0x21, TER_RESISTOR_HIGH |
					LEVEL_SHIFTERS_ON | SETRD_MAX |
					POWER_MANAGE | TER_RESISTORS_ON);

	rockchip_mipi_csi_phy_write(csi, 0x22, LOW_PROGRAM_EN |
					 BIASEXTR_SEL(BIASEXTR_127_7));
	rockchip_mipi_csi_phy_write(csi, 0x22, HIGH_PROGRAM_EN |
					 BANDGAP_SEL(BANDGAP_96_10));

	rockchip_mipi_csi_phy_write(csi, 0x70, TLP_PROGRAM_EN | 0xf);
	rockchip_mipi_csi_phy_write(csi, 0x71, THS_PRE_PROGRAM_EN | 0x2d);
	rockchip_mipi_csi_phy_write(csi, 0x72, THS_ZERO_PROGRAM_EN | 0xa);

	return 0;
}

/**
 * mipi_csi_pixel_format_to_bpp - obtain the number of bits per pixel for any
 *                                given pixel format defined by the MIPI CSI
 *                                specification
 * @fmt: MIPI CSI pixel format
 *
 * Returns: The number of bits per pixel of the given pixel format.
 */
static inline int mipi_csi_pixel_format_to_bpp(int fmt)
{
	switch (fmt) {
	case MIPI_CSI_FMT_RAW8:
		return 8;
	case MIPI_CSI_FMT_RAW10:
		return 10;
	default:
		pr_info("mipi csi unsupported format: %d\n", fmt);
		return 24;
	}

	return -EINVAL;
}

static unsigned long
rockchip_mipi_csi_calc_bandwidth(struct rockchip_mipi_csi *csi)
{
	int bpp;
	unsigned long mpclk, tmp;
	unsigned long target_mbps = 1000;
	unsigned int value;
	struct device_node *np = csi->dev->of_node;
	unsigned int max_mbps;
	int lanes;

	/* optional override of the desired bandwidth */
	if (!of_property_read_u32(np, "rockchip,lane-rate", &value))
		return value;

	max_mbps = csi->pdata->max_bit_rate_per_lane / USEC_PER_SEC;

	bpp = mipi_csi_pixel_format_to_bpp(csi->format);
	if (bpp < 0) {
		dev_err(csi->dev, "failed to get bpp for pixel format %d\n",
			csi->format);
		bpp = 24;
	}

	lanes = csi->lanes;

	mpclk = DIV_ROUND_UP(csi->mode.clock, MSEC_PER_SEC);
	if (mpclk) {
		/*
		 * vop raw 1 cycle pclk can process 4 pixel, so multiply 4.
		 */
		tmp = mpclk * (bpp / lanes) * 4;
		if (tmp <= max_mbps)
			target_mbps = tmp;
		else
			dev_err(csi->dev, "DPHY clock freq is out of range\n");
	}

	return target_mbps;
}

static int rockchip_mipi_csi_get_lane_bps(struct rockchip_mipi_csi *csi)
{
	unsigned int i, pre;
	unsigned long pllref, tmp;
	unsigned int m = 1, n = 1;
	unsigned long target_mbps;

	target_mbps = rockchip_mipi_csi_calc_bandwidth(csi);
#ifdef FPGA_PLATFORM_TEST
	pllref = DIV_ROUND_UP(27000000, USEC_PER_SEC);
#else
	pllref = DIV_ROUND_UP(clk_get_rate(csi->dphy.ref_clk), USEC_PER_SEC);
#endif
	tmp = pllref;

	for (i = 1; i < 6; i++) {
		pre = pllref / i;
		if ((tmp > (target_mbps % pre)) && (target_mbps / pre < 512)) {
			tmp = target_mbps % pre;
			n = i;
			m = target_mbps / pre;
		}
		if (tmp == 0)
			break;
	}

	csi->lane_mbps = pllref / n * m;
	csi->dphy.input_div = n;
	csi->dphy.feedback_div = m;

	return 0;
}

static void rockchip_mipi_csi_set_hs_clk(struct rockchip_mipi_csi *csi)
{
	int ret;
	unsigned long target_mbps;
	unsigned long bw, rate;

	target_mbps = rockchip_mipi_csi_calc_bandwidth(csi);
	bw = target_mbps * USEC_PER_SEC;

	rate = clk_round_rate(csi->dphy.hs_clk, bw);
	ret = clk_set_rate(csi->dphy.hs_clk, rate);
	if (ret)
		dev_err(csi->dev, "failed to set hs clock rate: %lu\n",
			rate);

	csi->lane_mbps = rate / USEC_PER_SEC;
}

static int rockchip_mipi_csi_host_attach(struct mipi_dsi_host *host,
					 struct mipi_dsi_device *device)
{
	struct rockchip_mipi_csi *csi = host_to_csi(host);

	if (device->lanes == 0 || device->lanes > 8) {
		dev_err(csi->dev, "the number of data lanes(%u) is too many\n",
			device->lanes);
		return -EINVAL;
	}
	csi->client = device->dev.of_node;
	csi->lanes = device->lanes;
	csi->channel = device->channel;
	csi->format = device->format;
	csi->mode_flags = device->mode_flags;

	return 0;
}

static int rockchip_mipi_csi_host_detach(struct mipi_dsi_host *host,
					 struct mipi_dsi_device *device)
{
	struct rockchip_mipi_csi *csi = host_to_csi(host);

	if (csi->panel)
		drm_panel_detach(csi->panel);

	csi->panel = NULL;
	return 0;
}

static const struct mipi_dsi_host_ops rockchip_mipi_csi_host_ops = {
	.attach = rockchip_mipi_csi_host_attach,
	.detach = rockchip_mipi_csi_host_detach,
};

static void rockchip_mipi_csi_path_config(struct rockchip_mipi_csi *csi)
{
	u32 mask, val;
	u32 vop_wc = 0;
	u32 data_type = 0x2a;

	switch (csi->format) {
	case MIPI_CSI_FMT_RAW8:
		vop_wc = csi->mode.hdisplay;
		data_type = 0x2a;
		break;
	case MIPI_CSI_FMT_RAW10:
		vop_wc = csi->mode.hdisplay * 5 / 4;
		data_type = 0x2b;
		break;
	default:
		vop_wc = csi->mode.hdisplay;
		data_type = 0x2a;
		WARN_ON(1);
	}

	if (csi->path_mode == VOP_PATH) {
		/* bypass select */
		mask = m_BYPASS_SELECT;
		val = v_BYPASS_SELECT(0);
		csi_mask_write(csi, CSITX_SYS_CTRL1, mask, val, true);

		/* enable vop path
		 * todo: vc
		 */
		mask = m_VOP_PATH_EN | m_VOP_WC_USERDEFINE_EN |
			m_VOP_WC_USERDEFINE | m_VOP_DT_USERDEFINE_EN |
			m_VOP_DT_USERDEFINE;
		val = v_VOP_PATH_EN(1) | v_VOP_WC_USERDEFINE_EN(1) |
			v_VOP_WC_USERDEFINE(vop_wc) |
			v_VOP_DT_USERDEFINE_EN(1) |
			v_VOP_DT_USERDEFINE(data_type);
		csi_mask_write(csi, CSITX_VOP_PATH_CTRL, mask, val, true);

		/* disable bypass path */
		mask = m_BYPASS_PATH_EN;
		val = v_BYPASS_PATH_EN(0);
		csi_mask_write(csi, CSITX_BYPASS_PATH_CTRL, mask, val, true);
	} else {
		mask = m_BYPASS_SELECT;
		val = v_BYPASS_SELECT(1);
		/* bypass select */
		csi_mask_write(csi, CSITX_SYS_CTRL1, mask, val, true);

		/* disable vop path
		 * todo: dt, vc, wc
		 */
		mask = m_VOP_PATH_EN | m_VOP_WC_USERDEFINE_EN |
			 m_VOP_DT_USERDEFINE_EN;
		val = v_VOP_PATH_EN(0) | v_VOP_WC_USERDEFINE_EN(0) |
			v_VOP_DT_USERDEFINE_EN(0);
		csi_mask_write(csi, CSITX_VOP_PATH_CTRL, mask, val, true);

		/* enable bypass path */
		mask = m_BYPASS_PATH_EN;
		val = v_BYPASS_PATH_EN(1);
		csi_mask_write(csi, CSITX_BYPASS_PATH_CTRL, mask, val, true);

		/* enable idi_48bit path */
		mask = m_IDI_48BIT_EN;
		val = v_IDI_48BIT_EN(0);
		csi_mask_write(csi, CSITX_ENABLE, mask, val, true);
	}
}

static void rockchip_mipi_csi_video_mode_config(struct rockchip_mipi_csi *csi)
{
	u32 mask, val;

	if (csi->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS) {
		/* enable non continue mode */
		val = v_NON_CONTINUES_MODE_EN(1) | v_CONT_MODE_CLK_SET(0);
	} else {
		/* disable non continue mode */
		val = v_NON_CONTINUES_MODE_EN(0) | v_CONT_MODE_CLK_SET(1);
	}
	mask = m_NON_CONTINUES_MODE_EN | m_CONT_MODE_CLK_SET;
	csi_mask_write(csi, CSITX_SYS_CTRL3, mask, val, true);
}

static void rockchip_mipi_dphy_init(struct rockchip_mipi_csi *csi)
{
	u32 map[] = {0x1, 0x3, 0x7, 0xf};

	/* Configures DPHY Selete */
	grf_field_write(csi, DPHY_SEL, 0);

	/* Configures DPHY to work as a Master */
	grf_field_write(csi, MASTERSLAVEZ, 1);

	/* Configures lane as TX */
	grf_field_write(csi, BASEDIR, 0);

	/* Set all REQUEST inputs to zero */
	grf_field_write(csi, TURNREQUEST, 0);
	grf_field_write(csi, TURNDISABLE, 0);
	grf_field_write(csi, FORCETXSTOPMODE, 0);
	grf_field_write(csi, FORCERXMODE, 0);

	/* Enable Data Lane Module */
	grf_field_write(csi, ENABLE_N, map[csi->lanes - 1]);

	/* Enable Clock Lane Module */
	grf_field_write(csi, ENABLECLK, 1);
	if (!csi->dphy.phy) {
		/* reset dphy */
		rockchip_bidir4l_board_phy_reset(csi);
		udelay(1);
#ifdef FPGA_PLATFORM_TEST
		/* init dphy */
		rockchip_mipi_csi_phy_write(csi, 0xb0, 0x01);
		rockchip_mipi_csi_phy_write(csi, 0xac, csi->lanes - 1);

		rockchip_mipi_csi_phy_write(csi, 0x44, 0x0a);/* fpga:324Mbps */
		rockchip_mipi_csi_phy_write(csi, 0x19, 0x30);
		rockchip_mipi_csi_phy_write(csi, 0x17, 0x00);
		rockchip_mipi_csi_phy_write(csi, 0x18, 0xb);
		rockchip_mipi_csi_phy_write(csi, 0x18, 0x80);

		rockchip_mipi_csi_phy_write(csi, 0x10, 0x80);
		rockchip_mipi_csi_phy_write(csi, 0x11, 0x09);
		rockchip_mipi_csi_phy_write(csi, 0x12, 0xc2);
#else
		rockchip_mipi_csi_phy_init(csi);
#endif
		/* enable dphy */
		rockchip_bidir4l_board_phy_enable(csi);
	}
	udelay(1);
}

static void rockchip_mipi_csi_fmt_config(struct rockchip_mipi_csi *csi,
					 struct drm_display_mode *mode)
{
	u32 mask, val;

	mask = m_PIXEL_FORMAT;
	val = v_PIXEL_FORMAT(csi->format);
	csi_mask_write(csi, CSITX_VOP_PATH_CTRL, mask, val, true);

	mask = m_CAM_FORMAT;
	val = v_CAM_FORMAT(csi->format);
	csi_mask_write(csi, CSITX_BYPASS_PATH_CTRL, mask, val, true);
}

static void
rockchip_mipi_csi_encoder_mode_set(struct drm_encoder *encoder,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{
	struct rockchip_mipi_csi *csi = encoder_to_csi(encoder);

	drm_mode_copy(&csi->mode, adjusted_mode);
}

static void rockchip_mipi_csi_post_disable(struct rockchip_mipi_csi *csi)
{
	rockchip_mipi_csi_host_power_off(csi);
	rockchip_mipi_dphy_power_off(csi);

	pm_runtime_put(csi->dev);
	clk_disable_unprepare(csi->pclk);
	clk_disable_unprepare(csi->dphy.hs_clk);
	clk_disable_unprepare(csi->dphy.ref_clk);
}

static void rockchip_mipi_csi_encoder_disable(struct drm_encoder *encoder)
{
	struct rockchip_mipi_csi *csi = encoder_to_csi(encoder);

	if (csi->panel)
		drm_panel_disable(csi->panel);

	if (csi->panel)
		drm_panel_unprepare(csi->panel);

	rockchip_mipi_csi_post_disable(csi);
}

static bool
rockchip_mipi_csi_encoder_mode_fixup(struct drm_encoder *encoder,
				     const struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void rockchip_mipi_csi_pre_init(struct rockchip_mipi_csi *csi)
{
	if (csi->dphy.phy)
		rockchip_mipi_csi_set_hs_clk(csi);
	else
		rockchip_mipi_csi_get_lane_bps(csi);

	dev_info(csi->dev, "final CSI-Link bandwidth: %u x %d Mbps\n",
		 csi->lane_mbps, csi->lanes);
}

static void rockchip_mipi_csihost_enable_phy(struct rockchip_mipi_csi *csi)
{
	u32 mask, val;
	u32 map[] = {0x3, 0x7, 0xf, 0x1f};

	mask = m_CSITX_ENABLE_PHY;
	val = v_CSITX_ENABLE_PHY(map[csi->lanes - 1]);
	csi_mask_write(csi, CSITX_DPHY_CTRL, mask, val, true);
}

static void rockchip_mipi_csi_host_init(struct rockchip_mipi_csi *csi)
{
	rockchip_mipi_csi_fmt_config(csi, &csi->mode);
	rockchip_mipi_csi_video_mode_config(csi);
	rockchip_mipi_csi_path_config(csi);
	rockchip_mipi_csihost_enable_phy(csi);

	/* timging config */
}

static int rockchip_mipi_csi_calibration(struct rockchip_mipi_csi *csi)
{
	int ret = 0;
	unsigned int val, mask;

	/* calibration */
	grf_field_write(csi, TXSKEWCALHS, 0x1f);
	udelay(17);
	grf_field_write(csi, TXSKEWCALHS, 0x0);

	ret = readl_poll_timeout(csi->regs + CSITX_STATUS1,
				 val, (val & m_DPHY_PLL_LOCK),
				 1000, PHY_STATUS_TIMEOUT_US);
	if (ret < 0) {
		dev_err(csi->dev, "PHY is not locked\n");
		return ret;
	}

	mask = PHY_STOPSTATELANE;
	ret = readl_poll_timeout(csi->regs + CSITX_STATUS1,
				 val, (val & mask) == mask,
				 1000, PHY_STATUS_TIMEOUT_US);
	if (ret < 0) {
		dev_err(csi->dev, "lane module is not in stop state\n");
		return ret;
	}
	udelay(10);

	return 0;
}

static int rockchip_mipi_csi_pre_enable(struct rockchip_mipi_csi *csi)
{
	int i = 0;

	rockchip_mipi_csi_pre_init(csi);
	clk_prepare_enable(csi->dphy.ref_clk);
	clk_prepare_enable(csi->dphy.hs_clk);
	clk_prepare_enable(csi->pclk);
	pm_runtime_get_sync(csi->dev);

	/* MIPI CSI TX software reset request. */
	for (i = 0; i < csi->pdata->rsts_num; i++) {
		if (csi->tx_rsts[i])
			reset_control_assert(csi->tx_rsts[i]);
	}
	usleep_range(20, 100);
	for (i = 0; i < csi->pdata->rsts_num; i++) {
		if (csi->tx_rsts[i])
			reset_control_deassert(csi->tx_rsts[i]);
	}

	if (!csi->regsbak) {
		csi->regsbak =
			devm_kzalloc(csi->dev, csi->regs_len, GFP_KERNEL);

		if (!csi->regsbak)
			return -ENOMEM;

		memcpy(csi->regsbak, csi->regs, csi->regs_len);
	}

	rockchip_mipi_csi_host_init(csi);
	rockchip_mipi_dphy_init(csi);
	rockchip_mipi_dphy_power_on(csi);
	rockchip_mipi_csi_calibration(csi);
	rockchip_mipi_csi_host_power_on(csi);

	return 0;
}

static void rockchip_mipi_csi_encoder_enable(struct drm_encoder *encoder)
{
	struct rockchip_mipi_csi *csi = encoder_to_csi(encoder);

	rockchip_mipi_csi_pre_enable(csi);

	if (csi->panel)
		drm_panel_prepare(csi->panel);

	if (csi->panel)
		drm_panel_enable(csi->panel);
}

static int
rockchip_mipi_csi_encoder_atomic_check(struct drm_encoder *encoder,
				       struct drm_crtc_state *crtc_state,
				       struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct rockchip_mipi_csi *csi = encoder_to_csi(encoder);
	struct drm_connector *connector = conn_state->connector;
	struct drm_display_info *info = &connector->display_info;

	switch (csi->format) {
	case MIPI_CSI_FMT_RAW8:
		s->output_mode = ROCKCHIP_OUT_MODE_P888;
		break;
	case MIPI_CSI_FMT_RAW10:
		s->output_mode = ROCKCHIP_OUT_MODE_P666;
		break;
	default:
		WARN_ON(1);
		s->output_mode = ROCKCHIP_OUT_MODE_P888;
		break;
	}

	s->output_type = DRM_MODE_CONNECTOR_DSI;
	if (info->num_bus_formats)
		s->bus_format = info->bus_formats[0];
	else
		s->bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	s->tv_state = &conn_state->tv;
	s->eotf = TRADITIONAL_GAMMA_SDR;
	s->color_space = V4L2_COLORSPACE_DEFAULT;

	return 0;
}

static const struct drm_encoder_helper_funcs
rockchip_mipi_csi_encoder_helper_funcs = {
	.mode_fixup = rockchip_mipi_csi_encoder_mode_fixup,
	.mode_set = rockchip_mipi_csi_encoder_mode_set,
	.enable = rockchip_mipi_csi_encoder_enable,
	.disable = rockchip_mipi_csi_encoder_disable,
	.atomic_check = rockchip_mipi_csi_encoder_atomic_check,
};

static const struct drm_encoder_funcs rockchip_mipi_csi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int
rockchip_mipi_csi_connector_get_modes(struct drm_connector *connector)
{
	struct rockchip_mipi_csi *csi = con_to_csi(connector);

	return drm_panel_get_modes(csi->panel);
}

static struct drm_encoder *
rockchip_mipi_csi_connector_best_encoder(struct drm_connector *connector)
{
	struct rockchip_mipi_csi *csi = con_to_csi(connector);

	return &csi->encoder;
}

static int
rockchip_mipi_loader_protect(struct drm_connector *connector, bool on)
{
	struct rockchip_mipi_csi *csi = con_to_csi(connector);

	if (csi->panel)
		drm_panel_loader_protect(csi->panel, on);
	if (on) {
		pm_runtime_get_sync(csi->dev);
		if (!csi->regsbak) {
			csi->regsbak = devm_kzalloc(csi->dev, csi->regs_len,
						    GFP_KERNEL);
			if (!csi->regsbak)
				return -ENOMEM;
			memcpy(csi->regsbak, csi->regs, csi->regs_len);
		}
	} else {
		pm_runtime_put(csi->dev);
	}
	return 0;
}

static void
rockchip_mipi_csi_connector_atomic_flush(struct drm_connector *connector,
					 struct drm_connector_state *conn_state)
{
	struct rockchip_mipi_csi *csi = con_to_csi(connector);
	u32 mask, val;

	rockchip_mipi_csi_path_config(csi);
	mask = m_CONFIG_DONE | m_CONFIG_DONE_IMD | m_CONFIG_DONE_MODE;
	val = v_CONFIG_DONE(0) | v_CONFIG_DONE_IMD(1) | v_CONFIG_DONE_MODE(0);
	csi_mask_write(csi, CSITX_CONFIG_DONE, mask, val, false);
}

static const struct drm_connector_helper_funcs
rockchip_mipi_csi_connector_helper_funcs = {
	.loader_protect = rockchip_mipi_loader_protect,
	.get_modes = rockchip_mipi_csi_connector_get_modes,
	.best_encoder = rockchip_mipi_csi_connector_best_encoder,
	.atomic_flush = rockchip_mipi_csi_connector_atomic_flush,
};

static enum drm_connector_status
rockchip_mipi_csi_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void
rockchip_mipi_csi_drm_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static int
rockchip_mipi_csi_connector_set_property(struct drm_connector *connector,
					 struct drm_connector_state *state,
					 struct drm_property *property,
					 uint64_t val)
{
	struct rockchip_mipi_csi *csi = con_to_csi(connector);

	if (property == csi->csi_tx_path_property) {
		/*
		 * csi->path_mode = val;
		 * we get path mode from dts now
		 */
		return 0;
	}

	DRM_ERROR("failed to set mipi csi tx cproperty\n");
	return -EINVAL;
}

static int
rockchip_mipi_csi_connector_get_property(struct drm_connector *connector,
					 const struct drm_connector_state *state,
					 struct drm_property *property,
					 uint64_t *val)
{
	struct rockchip_mipi_csi *csi = con_to_csi(connector);

	if (property == csi->csi_tx_path_property) {
		*val = csi->path_mode;
		return 0;
	}

	DRM_ERROR("failed to get mipi csi tx cproperty\n");
	return -EINVAL;
}

static const
struct drm_connector_funcs rockchip_mipi_csi_atomic_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = rockchip_mipi_csi_detect,
	.destroy = rockchip_mipi_csi_drm_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_set_property = rockchip_mipi_csi_connector_set_property,
	.atomic_get_property = rockchip_mipi_csi_connector_get_property,
};

static int rockchip_mipi_csi_property_create(struct rockchip_mipi_csi *csi)
{
	struct drm_property *prop;

	prop = drm_property_create_range(csi->connector.dev, 0,
					 "CSI-TX-PATH",
					 0, 0x1);
	if (prop) {
		csi->csi_tx_path_property = prop;
		drm_object_attach_property(&csi->connector.base, prop, 0);
	}

	return 0;
}

static int rockchip_mipi_csi_register(struct drm_device *drm,
				      struct rockchip_mipi_csi *csi)
{
	struct drm_encoder *encoder = &csi->encoder;
	struct drm_connector *connector = &csi->connector;
	struct device *dev = csi->dev;
	int ret;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm,
							     dev->of_node);
	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	drm_encoder_helper_add(&csi->encoder,
			       &rockchip_mipi_csi_encoder_helper_funcs);
	ret = drm_encoder_init(drm, &csi->encoder,
			       &rockchip_mipi_csi_encoder_funcs,
			       DRM_MODE_ENCODER_DSI, NULL);
	if (ret) {
		dev_err(dev, "Failed to initialize encoder with drm\n");
		return ret;
	}

	csi->connector.port = dev->of_node;
	ret = drm_connector_init(drm, &csi->connector,
				 &rockchip_mipi_csi_atomic_connector_funcs,
				 DRM_MODE_CONNECTOR_DSI);
	if (ret) {
		dev_err(dev, "Failed to initialize connector\n");
		goto encoder_cleanup;
	}

	drm_connector_helper_add(connector,
				 &rockchip_mipi_csi_connector_helper_funcs);

	drm_mode_connector_attach_encoder(connector, encoder);

	ret = drm_panel_attach(csi->panel, &csi->connector);
	if (ret) {
		dev_err(dev, "Failed to attach panel: %d\n", ret);
		goto connector_cleanup;
	}
	return 0;

connector_cleanup:
	drm_connector_cleanup(connector);
encoder_cleanup:
	drm_encoder_cleanup(encoder);
	return ret;
}

static int rockchip_mipi_csi_bind(struct device *dev, struct device *master,
				  void *data)
{
	struct drm_device *drm = data;
	struct rockchip_mipi_csi *csi = dev_get_drvdata(dev);
	int ret;

	csi->panel = of_drm_find_panel(csi->client);
	if (!csi->panel) {
		DRM_ERROR("failed to find panel\n");
		return -EPROBE_DEFER;
	}

	ret = rockchip_mipi_csi_register(drm, csi);
	if (ret) {
		dev_err(dev, "Failed to register mipi_csi: %d\n", ret);
		return ret;
	}

	rockchip_mipi_csi_property_create(csi);
	pm_runtime_enable(dev);

	return ret;
}

static void rockchip_mipi_csi_unbind(struct device *dev, struct device *master,
				     void *data)
{
	pm_runtime_disable(dev);
}

static const struct component_ops rockchip_mipi_csi_ops = {
	.bind	= rockchip_mipi_csi_bind,
	.unbind	= rockchip_mipi_csi_unbind,
};

static irqreturn_t rockchip_mipi_csi_irq_handler(int irq, void *data)
{
	struct rockchip_mipi_csi *csi = data;
	u32 int_status, err_int_status;
	unsigned int i;

	int_status = csi_readl(csi, CSITX_INTR_STATUS);
	err_int_status = csi_readl(csi, CSITX_ERR_INTR_STATUS);

	for (i = 0; i < ARRAY_SIZE(csi_tx_intr); i++)
		if (int_status & BIT(i))
			DRM_DEV_ERROR_RATELIMITED(csi->dev, "%s\n",
						  csi_tx_intr[i]);

	for (i = 0; i < ARRAY_SIZE(csi_tx_err_intr); i++)
		if (err_int_status & BIT(i))
			DRM_DEV_ERROR_RATELIMITED(csi->dev, "%s\n",
						  csi_tx_err_intr[i]);
	writel(int_status | m_INTR_MASK, csi->regs + CSITX_INTR_CLR);
	writel(err_int_status | m_ERR_INTR_MASK,
	       csi->regs + CSITX_ERR_INTR_CLR);

	return IRQ_HANDLED;
}

static int rockchip_mipi_dphy_attach(struct rockchip_mipi_csi *csi)
{
	struct device *dev = csi->dev;
	int ret;

	csi->dphy.phy = devm_phy_optional_get(dev, "mipi_dphy");
	if (IS_ERR(csi->dphy.phy)) {
		ret = PTR_ERR(csi->dphy.phy);
		dev_err(dev, "failed to get mipi dphy: %d\n", ret);
		return ret;
	}

	if (csi->dphy.phy) {
		dev_dbg(dev, "Use Non-SNPS PHY\n");

		csi->dphy.hs_clk = devm_clk_get(dev, "hs_clk");
		if (IS_ERR(csi->dphy.hs_clk)) {
			dev_err(dev, "failed to get PHY high-speed clock\n");
			return PTR_ERR(csi->dphy.hs_clk);
		}
	} else {
		dev_dbg(dev, "Use SNPS PHY\n");
		csi->dphy.ref_clk = devm_clk_get(dev, "ref");
		if (IS_ERR(csi->dphy.ref_clk)) {
			dev_err(dev, "failed to get PHY reference clock\n");
			return PTR_ERR(csi->dphy.ref_clk);
		}
	}

	return 0;
}

static int dw_mipi_csi_parse_dt(struct rockchip_mipi_csi *csi)
{
	struct device *dev = csi->dev;
	struct device_node *np = dev->of_node;
	struct device_node *endpoint, *remote = NULL;

	endpoint = of_graph_get_endpoint_by_regs(np, 1, -1);
	if (endpoint) {
		remote = of_graph_get_remote_port_parent(endpoint);

		of_node_put(endpoint);
		if (!remote) {
			dev_err(dev, "no panel/bridge connected\n");
			return -ENODEV;
		}
		of_node_put(remote);
	}

	csi->client = remote;

	return 0;
}

static int rockchip_mipi_csi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_mipi_csi *csi;
	struct device_node *np = dev->of_node;
	struct resource *res;
	int ret, val, i;

	csi = devm_kzalloc(dev, sizeof(*csi), GFP_KERNEL);
	if (!csi)
		return -ENOMEM;

	csi->dev = dev;
	csi->pdata = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, csi);

	ret = dw_mipi_csi_parse_dt(csi);
	if (ret) {
		dev_err(dev, "failed to parse DT\n");
		return ret;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "csi_regs");
	csi->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(csi->regs))
		return PTR_ERR(csi->regs);
	csi->regs_len = resource_size(res);
	csi->regsbak = NULL;

	res = platform_get_resource_byname(pdev,
					   IORESOURCE_MEM, "test_code_regs");
	if (res) {
		csi->test_code_regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(csi->test_code_regs))
			dev_err(dev, "Unable to get test_code_regs\n");
	}

	csi->irq = platform_get_irq(pdev, 0);
	if (csi->irq < 0) {
		dev_err(dev, "Failed to ger csi tx irq\n");
		return -EINVAL;
	}

	csi->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(csi->pclk)) {
		ret = PTR_ERR(csi->pclk);
		dev_err(dev, "Unable to get pclk: %d\n", ret);
		return ret;
	}

	csi->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(csi->grf)) {
		dev_err(dev, "Unable to get rockchip,grf\n");
		csi->grf = NULL;
	}

	for (i = 0; i < csi->pdata->rsts_num; i++) {
		struct reset_control *rst =
			devm_reset_control_get(dev, csi->pdata->rsts[i]);
		if (IS_ERR(rst)) {
			dev_err(dev, "failed to get %s\n", csi->pdata->rsts[i]);
			return PTR_ERR(rst);
		}
		csi->tx_rsts[i] = rst;
	}

	ret = rockchip_mipi_dphy_attach(csi);
	if (ret)
		return ret;

	ret = devm_request_irq(dev, csi->irq, rockchip_mipi_csi_irq_handler,
			       IRQF_SHARED, dev_name(dev), csi);
	if (ret) {
		dev_err(dev, "failed to request irq: %d\n", ret);
		return ret;
	}

	csi->dsi_host.ops = &rockchip_mipi_csi_host_ops;
	csi->dsi_host.dev = dev;

	ret = mipi_dsi_host_register(&csi->dsi_host);
	if (ret)
		return ret;

	ret = component_add(dev, &rockchip_mipi_csi_ops);
	if (ret)
		mipi_dsi_host_unregister(&csi->dsi_host);

	if (!of_property_read_u32(np, "csi-tx-bypass-mode", &val))
		csi->path_mode = val;

	return ret;
}

static int rockchip_mipi_csi_remove(struct platform_device *pdev)
{
	struct rockchip_mipi_csi *csi = dev_get_drvdata(&pdev->dev);

	if (csi)
		mipi_dsi_host_unregister(&csi->dsi_host);
	component_del(&pdev->dev, &rockchip_mipi_csi_ops);
	return 0;
}

static const u32 rk1808_csi_grf_reg_fields[MAX_FIELDS] = {
	[DPHY_SEL]		= GRF_REG_FIELD(0x0440,  8,  8),
	[TXSKEWCALHS]		= GRF_REG_FIELD(0x0444, 11, 15),
	[FORCETXSTOPMODE]	= GRF_REG_FIELD(0x0444,  7, 10),
	[FORCERXMODE]		= GRF_REG_FIELD(0x0444,  6,  6),
	[TURNDISABLE]		= GRF_REG_FIELD(0x0444,  5,  5),
};

static const char * const rk1808_csi_tx_rsts[] = {
	"tx_apb",
	"tx_bytehs",
	"tx_esc",
	"tx_cam",
	"tx_i",
};

static const struct rockchip_mipi_csi_plat_data rk1808_socdata = {
	.csi0_grf_reg_fields = rk1808_csi_grf_reg_fields,
	.max_bit_rate_per_lane = 2000000000UL,
	.soc_type = RK1808,
	.rsts = rk1808_csi_tx_rsts,
	.rsts_num = ARRAY_SIZE(rk1808_csi_tx_rsts),
};

static const struct of_device_id rockchip_mipi_csi_dt_ids[] = {
	{ .compatible = "rockchip,rk1808-mipi-csi", .data = &rk1808_socdata, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rockchip_mipi_csi_dt_ids);

static struct platform_driver rockchip_mipi_csi_driver = {
	.probe		= rockchip_mipi_csi_probe,
	.remove		= rockchip_mipi_csi_remove,
	.driver		= {
		.of_match_table = rockchip_mipi_csi_dt_ids,
		.name	= DRIVER_NAME,
	},
};
module_platform_driver(rockchip_mipi_csi_driver);

MODULE_DESCRIPTION("ROCKCHIP MIPI CSI TX controller driver");
MODULE_AUTHOR("Sandy huang <hjc@rock-chips.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
