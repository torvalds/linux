// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx VCU Init
 *
 * Copyright (C) 2016 - 2017 Xilinx, Inc.
 *
 * Contacts   Dhaval Shah <dshah@xilinx.com>
 */
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

/* Address map for different registers implemented in the VCU LogiCORE IP. */
#define VCU_ECODER_ENABLE		0x00
#define VCU_DECODER_ENABLE		0x04
#define VCU_MEMORY_DEPTH		0x08
#define VCU_ENC_COLOR_DEPTH		0x0c
#define VCU_ENC_VERTICAL_RANGE		0x10
#define VCU_ENC_FRAME_SIZE_X		0x14
#define VCU_ENC_FRAME_SIZE_Y		0x18
#define VCU_ENC_COLOR_FORMAT		0x1c
#define VCU_ENC_FPS			0x20
#define VCU_MCU_CLK			0x24
#define VCU_CORE_CLK			0x28
#define VCU_PLL_BYPASS			0x2c
#define VCU_ENC_CLK			0x30
#define VCU_PLL_CLK			0x34
#define VCU_ENC_VIDEO_STANDARD		0x38
#define VCU_STATUS			0x3c
#define VCU_AXI_ENC_CLK			0x40
#define VCU_AXI_DEC_CLK			0x44
#define VCU_AXI_MCU_CLK			0x48
#define VCU_DEC_VIDEO_STANDARD		0x4c
#define VCU_DEC_FRAME_SIZE_X		0x50
#define VCU_DEC_FRAME_SIZE_Y		0x54
#define VCU_DEC_FPS			0x58
#define VCU_BUFFER_B_FRAME		0x5c
#define VCU_WPP_EN			0x60
#define VCU_PLL_CLK_DEC			0x64
#define VCU_GASKET_INIT			0x74
#define VCU_GASKET_VALUE		0x03

/* vcu slcr registers, bitmask and shift */
#define VCU_PLL_CTRL			0x24
#define VCU_PLL_CTRL_RESET_MASK		0x01
#define VCU_PLL_CTRL_RESET_SHIFT	0
#define VCU_PLL_CTRL_BYPASS_MASK	0x01
#define VCU_PLL_CTRL_BYPASS_SHIFT	3
#define VCU_PLL_CTRL_FBDIV_MASK		0x7f
#define VCU_PLL_CTRL_FBDIV_SHIFT	8
#define VCU_PLL_CTRL_POR_IN_MASK	0x01
#define VCU_PLL_CTRL_POR_IN_SHIFT	1
#define VCU_PLL_CTRL_PWR_POR_MASK	0x01
#define VCU_PLL_CTRL_PWR_POR_SHIFT	2
#define VCU_PLL_CTRL_CLKOUTDIV_MASK	0x03
#define VCU_PLL_CTRL_CLKOUTDIV_SHIFT	16
#define VCU_PLL_CTRL_DEFAULT		0
#define VCU_PLL_DIV2			2

#define VCU_PLL_CFG			0x28
#define VCU_PLL_CFG_RES_MASK		0x0f
#define VCU_PLL_CFG_RES_SHIFT		0
#define VCU_PLL_CFG_CP_MASK		0x0f
#define VCU_PLL_CFG_CP_SHIFT		5
#define VCU_PLL_CFG_LFHF_MASK		0x03
#define VCU_PLL_CFG_LFHF_SHIFT		10
#define VCU_PLL_CFG_LOCK_CNT_MASK	0x03ff
#define VCU_PLL_CFG_LOCK_CNT_SHIFT	13
#define VCU_PLL_CFG_LOCK_DLY_MASK	0x7f
#define VCU_PLL_CFG_LOCK_DLY_SHIFT	25
#define VCU_ENC_CORE_CTRL		0x30
#define VCU_ENC_MCU_CTRL		0x34
#define VCU_DEC_CORE_CTRL		0x38
#define VCU_DEC_MCU_CTRL		0x3c
#define VCU_PLL_DIVISOR_MASK		0x3f
#define VCU_PLL_DIVISOR_SHIFT		4
#define VCU_SRCSEL_MASK			0x01
#define VCU_SRCSEL_SHIFT		0
#define VCU_SRCSEL_PLL			1

#define VCU_PLL_STATUS			0x60
#define VCU_PLL_STATUS_LOCK_STATUS_MASK	0x01

#define MHZ				1000000
#define FVCO_MIN			(1500U * MHZ)
#define FVCO_MAX			(3000U * MHZ)
#define DIVISOR_MIN			0
#define DIVISOR_MAX			63
#define FRAC				100
#define LIMIT				(10 * MHZ)

/**
 * struct xvcu_device - Xilinx VCU init device structure
 * @dev: Platform device
 * @pll_ref: pll ref clock source
 * @aclk: axi clock source
 * @logicore_reg_ba: logicore reg base address
 * @vcu_slcr_ba: vcu_slcr Register base address
 * @coreclk: core clock frequency
 */
struct xvcu_device {
	struct device *dev;
	struct clk *pll_ref;
	struct clk *aclk;
	void __iomem *logicore_reg_ba;
	void __iomem *vcu_slcr_ba;
	u32 coreclk;
};

/**
 * struct xvcu_pll_cfg - Helper data
 * @fbdiv: The integer portion of the feedback divider to the PLL
 * @cp: PLL charge pump control
 * @res: PLL loop filter resistor control
 * @lfhf: PLL loop filter high frequency capacitor control
 * @lock_dly: Lock circuit configuration settings for lock windowsize
 * @lock_cnt: Lock circuit counter setting
 */
struct xvcu_pll_cfg {
	u32 fbdiv;
	u32 cp;
	u32 res;
	u32 lfhf;
	u32 lock_dly;
	u32 lock_cnt;
};

static const struct xvcu_pll_cfg xvcu_pll_cfg[] = {
	{ 25, 3, 10, 3, 63, 1000 },
	{ 26, 3, 10, 3, 63, 1000 },
	{ 27, 4, 6, 3, 63, 1000 },
	{ 28, 4, 6, 3, 63, 1000 },
	{ 29, 4, 6, 3, 63, 1000 },
	{ 30, 4, 6, 3, 63, 1000 },
	{ 31, 6, 1, 3, 63, 1000 },
	{ 32, 6, 1, 3, 63, 1000 },
	{ 33, 4, 10, 3, 63, 1000 },
	{ 34, 5, 6, 3, 63, 1000 },
	{ 35, 5, 6, 3, 63, 1000 },
	{ 36, 5, 6, 3, 63, 1000 },
	{ 37, 5, 6, 3, 63, 1000 },
	{ 38, 5, 6, 3, 63, 975 },
	{ 39, 3, 12, 3, 63, 950 },
	{ 40, 3, 12, 3, 63, 925 },
	{ 41, 3, 12, 3, 63, 900 },
	{ 42, 3, 12, 3, 63, 875 },
	{ 43, 3, 12, 3, 63, 850 },
	{ 44, 3, 12, 3, 63, 850 },
	{ 45, 3, 12, 3, 63, 825 },
	{ 46, 3, 12, 3, 63, 800 },
	{ 47, 3, 12, 3, 63, 775 },
	{ 48, 3, 12, 3, 63, 775 },
	{ 49, 3, 12, 3, 63, 750 },
	{ 50, 3, 12, 3, 63, 750 },
	{ 51, 3, 2, 3, 63, 725 },
	{ 52, 3, 2, 3, 63, 700 },
	{ 53, 3, 2, 3, 63, 700 },
	{ 54, 3, 2, 3, 63, 675 },
	{ 55, 3, 2, 3, 63, 675 },
	{ 56, 3, 2, 3, 63, 650 },
	{ 57, 3, 2, 3, 63, 650 },
	{ 58, 3, 2, 3, 63, 625 },
	{ 59, 3, 2, 3, 63, 625 },
	{ 60, 3, 2, 3, 63, 625 },
	{ 61, 3, 2, 3, 63, 600 },
	{ 62, 3, 2, 3, 63, 600 },
	{ 63, 3, 2, 3, 63, 600 },
	{ 64, 3, 2, 3, 63, 600 },
	{ 65, 3, 2, 3, 63, 600 },
	{ 66, 3, 2, 3, 63, 600 },
	{ 67, 3, 2, 3, 63, 600 },
	{ 68, 3, 2, 3, 63, 600 },
	{ 69, 3, 2, 3, 63, 600 },
	{ 70, 3, 2, 3, 63, 600 },
	{ 71, 3, 2, 3, 63, 600 },
	{ 72, 3, 2, 3, 63, 600 },
	{ 73, 3, 2, 3, 63, 600 },
	{ 74, 3, 2, 3, 63, 600 },
	{ 75, 3, 2, 3, 63, 600 },
	{ 76, 3, 2, 3, 63, 600 },
	{ 77, 3, 2, 3, 63, 600 },
	{ 78, 3, 2, 3, 63, 600 },
	{ 79, 3, 2, 3, 63, 600 },
	{ 80, 3, 2, 3, 63, 600 },
	{ 81, 3, 2, 3, 63, 600 },
	{ 82, 3, 2, 3, 63, 600 },
	{ 83, 4, 2, 3, 63, 600 },
	{ 84, 4, 2, 3, 63, 600 },
	{ 85, 4, 2, 3, 63, 600 },
	{ 86, 4, 2, 3, 63, 600 },
	{ 87, 4, 2, 3, 63, 600 },
	{ 88, 4, 2, 3, 63, 600 },
	{ 89, 4, 2, 3, 63, 600 },
	{ 90, 4, 2, 3, 63, 600 },
	{ 91, 4, 2, 3, 63, 600 },
	{ 92, 4, 2, 3, 63, 600 },
	{ 93, 4, 2, 3, 63, 600 },
	{ 94, 4, 2, 3, 63, 600 },
	{ 95, 4, 2, 3, 63, 600 },
	{ 96, 4, 2, 3, 63, 600 },
	{ 97, 4, 2, 3, 63, 600 },
	{ 98, 4, 2, 3, 63, 600 },
	{ 99, 4, 2, 3, 63, 600 },
	{ 100, 4, 2, 3, 63, 600 },
	{ 101, 4, 2, 3, 63, 600 },
	{ 102, 4, 2, 3, 63, 600 },
	{ 103, 5, 2, 3, 63, 600 },
	{ 104, 5, 2, 3, 63, 600 },
	{ 105, 5, 2, 3, 63, 600 },
	{ 106, 5, 2, 3, 63, 600 },
	{ 107, 3, 4, 3, 63, 600 },
	{ 108, 3, 4, 3, 63, 600 },
	{ 109, 3, 4, 3, 63, 600 },
	{ 110, 3, 4, 3, 63, 600 },
	{ 111, 3, 4, 3, 63, 600 },
	{ 112, 3, 4, 3, 63, 600 },
	{ 113, 3, 4, 3, 63, 600 },
	{ 114, 3, 4, 3, 63, 600 },
	{ 115, 3, 4, 3, 63, 600 },
	{ 116, 3, 4, 3, 63, 600 },
	{ 117, 3, 4, 3, 63, 600 },
	{ 118, 3, 4, 3, 63, 600 },
	{ 119, 3, 4, 3, 63, 600 },
	{ 120, 3, 4, 3, 63, 600 },
	{ 121, 3, 4, 3, 63, 600 },
	{ 122, 3, 4, 3, 63, 600 },
	{ 123, 3, 4, 3, 63, 600 },
	{ 124, 3, 4, 3, 63, 600 },
	{ 125, 3, 4, 3, 63, 600 },
};

/**
 * xvcu_read - Read from the VCU register space
 * @iomem:	vcu reg space base address
 * @offset:	vcu reg offset from base
 *
 * Return:	Returns 32bit value from VCU register specified
 *
 */
static inline u32 xvcu_read(void __iomem *iomem, u32 offset)
{
	return ioread32(iomem + offset);
}

/**
 * xvcu_write - Write to the VCU register space
 * @iomem:	vcu reg space base address
 * @offset:	vcu reg offset from base
 * @value:	Value to write
 */
static inline void xvcu_write(void __iomem *iomem, u32 offset, u32 value)
{
	iowrite32(value, iomem + offset);
}

/**
 * xvcu_write_field_reg - Write to the vcu reg field
 * @iomem:	vcu reg space base address
 * @offset:	vcu reg offset from base
 * @field:	vcu reg field to write to
 * @mask:	vcu reg mask
 * @shift:	vcu reg number of bits to shift the bitfield
 */
static void xvcu_write_field_reg(void __iomem *iomem, int offset,
				 u32 field, u32 mask, int shift)
{
	u32 val = xvcu_read(iomem, offset);

	val &= ~(mask << shift);
	val |= (field & mask) << shift;

	xvcu_write(iomem, offset, val);
}

/**
 * xvcu_set_vcu_pll_info - Set the VCU PLL info
 * @xvcu:	Pointer to the xvcu_device structure
 *
 * Programming the VCU PLL based on the user configuration
 * (ref clock freq, core clock freq, mcu clock freq).
 * Core clock frequency has higher priority than mcu clock frequency
 * Errors in following cases
 *    - When mcu or clock clock get from logicoreIP is 0
 *    - When VCU PLL DIV related bits value other than 1
 *    - When proper data not found for given data
 *    - When sis570_1 clocksource related operation failed
 *
 * Return:	Returns status, either success or error+reason
 */
static int xvcu_set_vcu_pll_info(struct xvcu_device *xvcu)
{
	u32 refclk, coreclk, mcuclk, inte, deci;
	u32 divisor_mcu, divisor_core, fvco;
	u32 clkoutdiv, vcu_pll_ctrl, pll_clk;
	u32 cfg_val, mod, ctrl;
	int ret, i;
	const struct xvcu_pll_cfg *found = NULL;

	inte = xvcu_read(xvcu->logicore_reg_ba, VCU_PLL_CLK);
	deci = xvcu_read(xvcu->logicore_reg_ba, VCU_PLL_CLK_DEC);
	coreclk = xvcu_read(xvcu->logicore_reg_ba, VCU_CORE_CLK) * MHZ;
	mcuclk = xvcu_read(xvcu->logicore_reg_ba, VCU_MCU_CLK) * MHZ;
	if (!mcuclk || !coreclk) {
		dev_err(xvcu->dev, "Invalid mcu and core clock data\n");
		return -EINVAL;
	}

	refclk = (inte * MHZ) + (deci * (MHZ / FRAC));
	dev_dbg(xvcu->dev, "Ref clock from logicoreIP is %uHz\n", refclk);
	dev_dbg(xvcu->dev, "Core clock from logicoreIP is %uHz\n", coreclk);
	dev_dbg(xvcu->dev, "Mcu clock from logicoreIP is %uHz\n", mcuclk);

	clk_disable_unprepare(xvcu->pll_ref);
	ret = clk_set_rate(xvcu->pll_ref, refclk);
	if (ret)
		dev_warn(xvcu->dev, "failed to set logicoreIP refclk rate\n");

	ret = clk_prepare_enable(xvcu->pll_ref);
	if (ret) {
		dev_err(xvcu->dev, "failed to enable pll_ref clock source\n");
		return ret;
	}

	refclk = clk_get_rate(xvcu->pll_ref);

	/*
	 * The divide-by-2 should be always enabled (==1)
	 * to meet the timing in the design.
	 * Otherwise, it's an error
	 */
	vcu_pll_ctrl = xvcu_read(xvcu->vcu_slcr_ba, VCU_PLL_CTRL);
	clkoutdiv = vcu_pll_ctrl >> VCU_PLL_CTRL_CLKOUTDIV_SHIFT;
	clkoutdiv = clkoutdiv && VCU_PLL_CTRL_CLKOUTDIV_MASK;
	if (clkoutdiv != 1) {
		dev_err(xvcu->dev, "clkoutdiv value is invalid\n");
		return -EINVAL;
	}

	for (i = ARRAY_SIZE(xvcu_pll_cfg) - 1; i >= 0; i--) {
		const struct xvcu_pll_cfg *cfg = &xvcu_pll_cfg[i];

		fvco = cfg->fbdiv * refclk;
		if (fvco >= FVCO_MIN && fvco <= FVCO_MAX) {
			pll_clk = fvco / VCU_PLL_DIV2;
			if (fvco % VCU_PLL_DIV2 != 0)
				pll_clk++;
			mod = pll_clk % coreclk;
			if (mod < LIMIT) {
				divisor_core = pll_clk / coreclk;
			} else if (coreclk - mod < LIMIT) {
				divisor_core = pll_clk / coreclk;
				divisor_core++;
			} else {
				continue;
			}
			if (divisor_core >= DIVISOR_MIN &&
			    divisor_core <= DIVISOR_MAX) {
				found = cfg;
				divisor_mcu = pll_clk / mcuclk;
				mod = pll_clk % mcuclk;
				if (mcuclk - mod < LIMIT)
					divisor_mcu++;
				break;
			}
		}
	}

	if (!found) {
		dev_err(xvcu->dev, "Invalid clock combination.\n");
		return -EINVAL;
	}

	xvcu->coreclk = pll_clk / divisor_core;
	mcuclk = pll_clk / divisor_mcu;
	dev_dbg(xvcu->dev, "Actual Ref clock freq is %uHz\n", refclk);
	dev_dbg(xvcu->dev, "Actual Core clock freq is %uHz\n", xvcu->coreclk);
	dev_dbg(xvcu->dev, "Actual Mcu clock freq is %uHz\n", mcuclk);

	vcu_pll_ctrl &= ~(VCU_PLL_CTRL_FBDIV_MASK << VCU_PLL_CTRL_FBDIV_SHIFT);
	vcu_pll_ctrl |= (found->fbdiv & VCU_PLL_CTRL_FBDIV_MASK) <<
			 VCU_PLL_CTRL_FBDIV_SHIFT;
	vcu_pll_ctrl &= ~(VCU_PLL_CTRL_POR_IN_MASK <<
			  VCU_PLL_CTRL_POR_IN_SHIFT);
	vcu_pll_ctrl |= (VCU_PLL_CTRL_DEFAULT & VCU_PLL_CTRL_POR_IN_MASK) <<
			 VCU_PLL_CTRL_POR_IN_SHIFT;
	vcu_pll_ctrl &= ~(VCU_PLL_CTRL_PWR_POR_MASK <<
			  VCU_PLL_CTRL_PWR_POR_SHIFT);
	vcu_pll_ctrl |= (VCU_PLL_CTRL_DEFAULT & VCU_PLL_CTRL_PWR_POR_MASK) <<
			 VCU_PLL_CTRL_PWR_POR_SHIFT;
	xvcu_write(xvcu->vcu_slcr_ba, VCU_PLL_CTRL, vcu_pll_ctrl);

	/* Set divisor for the core and mcu clock */
	ctrl = xvcu_read(xvcu->vcu_slcr_ba, VCU_ENC_CORE_CTRL);
	ctrl &= ~(VCU_PLL_DIVISOR_MASK << VCU_PLL_DIVISOR_SHIFT);
	ctrl |= (divisor_core & VCU_PLL_DIVISOR_MASK) <<
		 VCU_PLL_DIVISOR_SHIFT;
	ctrl &= ~(VCU_SRCSEL_MASK << VCU_SRCSEL_SHIFT);
	ctrl |= (VCU_SRCSEL_PLL & VCU_SRCSEL_MASK) << VCU_SRCSEL_SHIFT;
	xvcu_write(xvcu->vcu_slcr_ba, VCU_ENC_CORE_CTRL, ctrl);

	ctrl = xvcu_read(xvcu->vcu_slcr_ba, VCU_DEC_CORE_CTRL);
	ctrl &= ~(VCU_PLL_DIVISOR_MASK << VCU_PLL_DIVISOR_SHIFT);
	ctrl |= (divisor_core & VCU_PLL_DIVISOR_MASK) <<
		 VCU_PLL_DIVISOR_SHIFT;
	ctrl &= ~(VCU_SRCSEL_MASK << VCU_SRCSEL_SHIFT);
	ctrl |= (VCU_SRCSEL_PLL & VCU_SRCSEL_MASK) << VCU_SRCSEL_SHIFT;
	xvcu_write(xvcu->vcu_slcr_ba, VCU_DEC_CORE_CTRL, ctrl);

	ctrl = xvcu_read(xvcu->vcu_slcr_ba, VCU_ENC_MCU_CTRL);
	ctrl &= ~(VCU_PLL_DIVISOR_MASK << VCU_PLL_DIVISOR_SHIFT);
	ctrl |= (divisor_mcu & VCU_PLL_DIVISOR_MASK) << VCU_PLL_DIVISOR_SHIFT;
	ctrl &= ~(VCU_SRCSEL_MASK << VCU_SRCSEL_SHIFT);
	ctrl |= (VCU_SRCSEL_PLL & VCU_SRCSEL_MASK) << VCU_SRCSEL_SHIFT;
	xvcu_write(xvcu->vcu_slcr_ba, VCU_ENC_MCU_CTRL, ctrl);

	ctrl = xvcu_read(xvcu->vcu_slcr_ba, VCU_DEC_MCU_CTRL);
	ctrl &= ~(VCU_PLL_DIVISOR_MASK << VCU_PLL_DIVISOR_SHIFT);
	ctrl |= (divisor_mcu & VCU_PLL_DIVISOR_MASK) << VCU_PLL_DIVISOR_SHIFT;
	ctrl &= ~(VCU_SRCSEL_MASK << VCU_SRCSEL_SHIFT);
	ctrl |= (VCU_SRCSEL_PLL & VCU_SRCSEL_MASK) << VCU_SRCSEL_SHIFT;
	xvcu_write(xvcu->vcu_slcr_ba, VCU_DEC_MCU_CTRL, ctrl);

	/* Set RES, CP, LFHF, LOCK_CNT and LOCK_DLY cfg values */
	cfg_val = (found->res << VCU_PLL_CFG_RES_SHIFT) |
		   (found->cp << VCU_PLL_CFG_CP_SHIFT) |
		   (found->lfhf << VCU_PLL_CFG_LFHF_SHIFT) |
		   (found->lock_cnt << VCU_PLL_CFG_LOCK_CNT_SHIFT) |
		   (found->lock_dly << VCU_PLL_CFG_LOCK_DLY_SHIFT);
	xvcu_write(xvcu->vcu_slcr_ba, VCU_PLL_CFG, cfg_val);

	return 0;
}

/**
 * xvcu_set_pll - PLL init sequence
 * @xvcu:	Pointer to the xvcu_device structure
 *
 * Call the api to set the PLL info and once that is done then
 * init the PLL sequence to make the PLL stable.
 *
 * Return:	Returns status, either success or error+reason
 */
static int xvcu_set_pll(struct xvcu_device *xvcu)
{
	u32 lock_status;
	unsigned long timeout;
	int ret;

	ret = xvcu_set_vcu_pll_info(xvcu);
	if (ret) {
		dev_err(xvcu->dev, "failed to set pll info\n");
		return ret;
	}

	xvcu_write_field_reg(xvcu->vcu_slcr_ba, VCU_PLL_CTRL,
			     1, VCU_PLL_CTRL_BYPASS_MASK,
			     VCU_PLL_CTRL_BYPASS_SHIFT);
	xvcu_write_field_reg(xvcu->vcu_slcr_ba, VCU_PLL_CTRL,
			     1, VCU_PLL_CTRL_RESET_MASK,
			     VCU_PLL_CTRL_RESET_SHIFT);
	xvcu_write_field_reg(xvcu->vcu_slcr_ba, VCU_PLL_CTRL,
			     0, VCU_PLL_CTRL_RESET_MASK,
			     VCU_PLL_CTRL_RESET_SHIFT);
	/*
	 * Defined the timeout for the max time to wait the
	 * PLL_STATUS to be locked.
	 */
	timeout = jiffies + msecs_to_jiffies(2000);
	do {
		lock_status = xvcu_read(xvcu->vcu_slcr_ba, VCU_PLL_STATUS);
		if (lock_status & VCU_PLL_STATUS_LOCK_STATUS_MASK) {
			xvcu_write_field_reg(xvcu->vcu_slcr_ba, VCU_PLL_CTRL,
					     0, VCU_PLL_CTRL_BYPASS_MASK,
					     VCU_PLL_CTRL_BYPASS_SHIFT);
			return 0;
		}
	} while (!time_after(jiffies, timeout));

	/* PLL is not locked even after the timeout of the 2sec */
	dev_err(xvcu->dev, "PLL is not locked\n");
	return -ETIMEDOUT;
}

/**
 * xvcu_probe - Probe existence of the logicoreIP
 *			and initialize PLL
 *
 * @pdev:	Pointer to the platform_device structure
 *
 * Return:	Returns 0 on success
 *		Negative error code otherwise
 */
static int xvcu_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct xvcu_device *xvcu;
	int ret;

	xvcu = devm_kzalloc(&pdev->dev, sizeof(*xvcu), GFP_KERNEL);
	if (!xvcu)
		return -ENOMEM;

	xvcu->dev = &pdev->dev;
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vcu_slcr");
	if (!res) {
		dev_err(&pdev->dev, "get vcu_slcr memory resource failed.\n");
		return -ENODEV;
	}

	xvcu->vcu_slcr_ba = devm_ioremap_nocache(&pdev->dev, res->start,
						 resource_size(res));
	if (!xvcu->vcu_slcr_ba) {
		dev_err(&pdev->dev, "vcu_slcr register mapping failed.\n");
		return -ENOMEM;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "logicore");
	if (!res) {
		dev_err(&pdev->dev, "get logicore memory resource failed.\n");
		return -ENODEV;
	}

	xvcu->logicore_reg_ba = devm_ioremap_nocache(&pdev->dev, res->start,
						     resource_size(res));
	if (!xvcu->logicore_reg_ba) {
		dev_err(&pdev->dev, "logicore register mapping failed.\n");
		return -ENOMEM;
	}

	xvcu->aclk = devm_clk_get(&pdev->dev, "aclk");
	if (IS_ERR(xvcu->aclk)) {
		dev_err(&pdev->dev, "Could not get aclk clock\n");
		return PTR_ERR(xvcu->aclk);
	}

	xvcu->pll_ref = devm_clk_get(&pdev->dev, "pll_ref");
	if (IS_ERR(xvcu->pll_ref)) {
		dev_err(&pdev->dev, "Could not get pll_ref clock\n");
		return PTR_ERR(xvcu->pll_ref);
	}

	ret = clk_prepare_enable(xvcu->aclk);
	if (ret) {
		dev_err(&pdev->dev, "aclk clock enable failed\n");
		return ret;
	}

	ret = clk_prepare_enable(xvcu->pll_ref);
	if (ret) {
		dev_err(&pdev->dev, "pll_ref clock enable failed\n");
		goto error_aclk;
	}

	/*
	 * Do the Gasket isolation and put the VCU out of reset
	 * Bit 0 : Gasket isolation
	 * Bit 1 : put VCU out of reset
	 */
	xvcu_write(xvcu->logicore_reg_ba, VCU_GASKET_INIT, VCU_GASKET_VALUE);

	/* Do the PLL Settings based on the ref clk,core and mcu clk freq */
	ret = xvcu_set_pll(xvcu);
	if (ret) {
		dev_err(&pdev->dev, "Failed to set the pll\n");
		goto error_pll_ref;
	}

	dev_set_drvdata(&pdev->dev, xvcu);

	dev_info(&pdev->dev, "%s: Probed successfully\n", __func__);

	return 0;

error_pll_ref:
	clk_disable_unprepare(xvcu->pll_ref);
error_aclk:
	clk_disable_unprepare(xvcu->aclk);
	return ret;
}

/**
 * xvcu_remove - Insert gasket isolation
 *			and disable the clock
 * @pdev:	Pointer to the platform_device structure
 *
 * Return:	Returns 0 on success
 *		Negative error code otherwise
 */
static int xvcu_remove(struct platform_device *pdev)
{
	struct xvcu_device *xvcu;

	xvcu = platform_get_drvdata(pdev);
	if (!xvcu)
		return -ENODEV;

	/* Add the the Gasket isolation and put the VCU in reset. */
	xvcu_write(xvcu->logicore_reg_ba, VCU_GASKET_INIT, 0);

	clk_disable_unprepare(xvcu->pll_ref);
	clk_disable_unprepare(xvcu->aclk);

	return 0;
}

static const struct of_device_id xvcu_of_id_table[] = {
	{ .compatible = "xlnx,vcu" },
	{ .compatible = "xlnx,vcu-logicoreip-1.0" },
	{ }
};
MODULE_DEVICE_TABLE(of, xvcu_of_id_table);

static struct platform_driver xvcu_driver = {
	.driver = {
		.name           = "xilinx-vcu",
		.of_match_table = xvcu_of_id_table,
	},
	.probe                  = xvcu_probe,
	.remove                 = xvcu_remove,
};

module_platform_driver(xvcu_driver);

MODULE_AUTHOR("Dhaval Shah <dshah@xilinx.com>");
MODULE_DESCRIPTION("Xilinx VCU init Driver");
MODULE_LICENSE("GPL v2");
