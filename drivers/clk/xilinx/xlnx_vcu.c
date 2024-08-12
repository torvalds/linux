// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx VCU Init
 *
 * Copyright (C) 2016 - 2017 Xilinx, Inc.
 *
 * Contacts   Dhaval Shah <dshah@xilinx.com>
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/xlnx-vcu.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/xlnx-vcu.h>

#define VCU_PLL_CTRL			0x24
#define VCU_PLL_CTRL_RESET		BIT(0)
#define VCU_PLL_CTRL_POR_IN		BIT(1)
#define VCU_PLL_CTRL_PWR_POR		BIT(2)
#define VCU_PLL_CTRL_BYPASS		BIT(3)
#define VCU_PLL_CTRL_FBDIV		GENMASK(14, 8)
#define VCU_PLL_CTRL_CLKOUTDIV		GENMASK(18, 16)

#define VCU_PLL_CFG			0x28
#define VCU_PLL_CFG_RES			GENMASK(3, 0)
#define VCU_PLL_CFG_CP			GENMASK(8, 5)
#define VCU_PLL_CFG_LFHF		GENMASK(12, 10)
#define VCU_PLL_CFG_LOCK_CNT		GENMASK(22, 13)
#define VCU_PLL_CFG_LOCK_DLY		GENMASK(31, 25)
#define VCU_ENC_CORE_CTRL		0x30
#define VCU_ENC_MCU_CTRL		0x34
#define VCU_DEC_CORE_CTRL		0x38
#define VCU_DEC_MCU_CTRL		0x3c
#define VCU_PLL_STATUS			0x60
#define VCU_PLL_STATUS_LOCK_STATUS	BIT(0)

#define MHZ				1000000
#define FVCO_MIN			(1500U * MHZ)
#define FVCO_MAX			(3000U * MHZ)

/**
 * struct xvcu_device - Xilinx VCU init device structure
 * @dev: Platform device
 * @pll_ref: pll ref clock source
 * @aclk: axi clock source
 * @logicore_reg_ba: logicore reg base address
 * @vcu_slcr_ba: vcu_slcr Register base address
 * @pll: handle for the VCU PLL
 * @pll_post: handle for the VCU PLL post divider
 * @clk_data: clocks provided by the vcu clock provider
 */
struct xvcu_device {
	struct device *dev;
	struct clk *pll_ref;
	struct clk *aclk;
	struct regmap *logicore_reg_ba;
	void __iomem *vcu_slcr_ba;
	struct clk_hw *pll;
	struct clk_hw *pll_post;
	struct clk_hw_onecell_data *clk_data;
};

static const struct regmap_config vcu_settings_regmap_config = {
	.name = "regmap",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0xfff,
	.cache_type = REGCACHE_NONE,
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

#define to_vcu_pll(_hw) container_of(_hw, struct vcu_pll, hw)

struct vcu_pll {
	struct clk_hw hw;
	void __iomem *reg_base;
	unsigned long fvco_min;
	unsigned long fvco_max;
};

static int xvcu_pll_wait_for_lock(struct vcu_pll *pll)
{
	void __iomem *base = pll->reg_base;
	unsigned long timeout;
	u32 lock_status;

	timeout = jiffies + msecs_to_jiffies(2000);
	do {
		lock_status = xvcu_read(base, VCU_PLL_STATUS);
		if (lock_status & VCU_PLL_STATUS_LOCK_STATUS)
			return 0;
	} while (!time_after(jiffies, timeout));

	return -ETIMEDOUT;
}

static struct clk_hw *xvcu_register_pll_post(struct device *dev,
					     const char *name,
					     const struct clk_hw *parent_hw,
					     void __iomem *reg_base)
{
	u32 div;
	u32 vcu_pll_ctrl;

	/*
	 * The output divider of the PLL must be set to 1/2 to meet the
	 * timing in the design.
	 */
	vcu_pll_ctrl = xvcu_read(reg_base, VCU_PLL_CTRL);
	div = FIELD_GET(VCU_PLL_CTRL_CLKOUTDIV, vcu_pll_ctrl);
	if (div != 1)
		return ERR_PTR(-EINVAL);

	return clk_hw_register_fixed_factor(dev, "vcu_pll_post",
					    clk_hw_get_name(parent_hw),
					    CLK_SET_RATE_PARENT, 1, 2);
}

static const struct xvcu_pll_cfg *xvcu_find_cfg(int div)
{
	const struct xvcu_pll_cfg *cfg = NULL;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(xvcu_pll_cfg) - 1; i++)
		if (xvcu_pll_cfg[i].fbdiv == div)
			cfg = &xvcu_pll_cfg[i];

	return cfg;
}

static int xvcu_pll_set_div(struct vcu_pll *pll, int div)
{
	void __iomem *base = pll->reg_base;
	const struct xvcu_pll_cfg *cfg = NULL;
	u32 vcu_pll_ctrl;
	u32 cfg_val;

	cfg = xvcu_find_cfg(div);
	if (!cfg)
		return -EINVAL;

	vcu_pll_ctrl = xvcu_read(base, VCU_PLL_CTRL);
	vcu_pll_ctrl &= ~VCU_PLL_CTRL_FBDIV;
	vcu_pll_ctrl |= FIELD_PREP(VCU_PLL_CTRL_FBDIV, cfg->fbdiv);
	xvcu_write(base, VCU_PLL_CTRL, vcu_pll_ctrl);

	cfg_val = FIELD_PREP(VCU_PLL_CFG_RES, cfg->res) |
		  FIELD_PREP(VCU_PLL_CFG_CP, cfg->cp) |
		  FIELD_PREP(VCU_PLL_CFG_LFHF, cfg->lfhf) |
		  FIELD_PREP(VCU_PLL_CFG_LOCK_CNT, cfg->lock_cnt) |
		  FIELD_PREP(VCU_PLL_CFG_LOCK_DLY, cfg->lock_dly);
	xvcu_write(base, VCU_PLL_CFG, cfg_val);

	return 0;
}

static long xvcu_pll_round_rate(struct clk_hw *hw,
				unsigned long rate, unsigned long *parent_rate)
{
	struct vcu_pll *pll = to_vcu_pll(hw);
	unsigned int feedback_div;

	rate = clamp_t(unsigned long, rate, pll->fvco_min, pll->fvco_max);

	feedback_div = DIV_ROUND_CLOSEST_ULL(rate, *parent_rate);
	feedback_div = clamp_t(unsigned int, feedback_div, 25, 125);

	return *parent_rate * feedback_div;
}

static unsigned long xvcu_pll_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct vcu_pll *pll = to_vcu_pll(hw);
	void __iomem *base = pll->reg_base;
	unsigned int div;
	u32 vcu_pll_ctrl;

	vcu_pll_ctrl = xvcu_read(base, VCU_PLL_CTRL);
	div = FIELD_GET(VCU_PLL_CTRL_FBDIV, vcu_pll_ctrl);

	return div * parent_rate;
}

static int xvcu_pll_set_rate(struct clk_hw *hw,
			     unsigned long rate, unsigned long parent_rate)
{
	struct vcu_pll *pll = to_vcu_pll(hw);

	return xvcu_pll_set_div(pll, rate / parent_rate);
}

static int xvcu_pll_enable(struct clk_hw *hw)
{
	struct vcu_pll *pll = to_vcu_pll(hw);
	void __iomem *base = pll->reg_base;
	u32 vcu_pll_ctrl;
	int ret;

	vcu_pll_ctrl = xvcu_read(base, VCU_PLL_CTRL);
	vcu_pll_ctrl |= VCU_PLL_CTRL_BYPASS;
	xvcu_write(base, VCU_PLL_CTRL, vcu_pll_ctrl);

	vcu_pll_ctrl = xvcu_read(base, VCU_PLL_CTRL);
	vcu_pll_ctrl &= ~VCU_PLL_CTRL_POR_IN;
	vcu_pll_ctrl &= ~VCU_PLL_CTRL_PWR_POR;
	vcu_pll_ctrl &= ~VCU_PLL_CTRL_RESET;
	xvcu_write(base, VCU_PLL_CTRL, vcu_pll_ctrl);

	ret = xvcu_pll_wait_for_lock(pll);
	if (ret) {
		pr_err("VCU PLL is not locked\n");
		goto err;
	}

	vcu_pll_ctrl = xvcu_read(base, VCU_PLL_CTRL);
	vcu_pll_ctrl &= ~VCU_PLL_CTRL_BYPASS;
	xvcu_write(base, VCU_PLL_CTRL, vcu_pll_ctrl);

err:
	return ret;
}

static void xvcu_pll_disable(struct clk_hw *hw)
{
	struct vcu_pll *pll = to_vcu_pll(hw);
	void __iomem *base = pll->reg_base;
	u32 vcu_pll_ctrl;

	vcu_pll_ctrl = xvcu_read(base, VCU_PLL_CTRL);
	vcu_pll_ctrl |= VCU_PLL_CTRL_POR_IN;
	vcu_pll_ctrl |= VCU_PLL_CTRL_PWR_POR;
	vcu_pll_ctrl |= VCU_PLL_CTRL_RESET;
	xvcu_write(base, VCU_PLL_CTRL, vcu_pll_ctrl);
}

static const struct clk_ops vcu_pll_ops = {
	.enable = xvcu_pll_enable,
	.disable = xvcu_pll_disable,
	.round_rate = xvcu_pll_round_rate,
	.recalc_rate = xvcu_pll_recalc_rate,
	.set_rate = xvcu_pll_set_rate,
};

static struct clk_hw *xvcu_register_pll(struct device *dev,
					void __iomem *reg_base,
					const char *name, const char *parent,
					unsigned long flags)
{
	struct vcu_pll *pll;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	init.name = name;
	init.parent_names = &parent;
	init.ops = &vcu_pll_ops;
	init.num_parents = 1;
	init.flags = flags;

	pll = devm_kmalloc(dev, sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->hw.init = &init;
	pll->reg_base = reg_base;
	pll->fvco_min = FVCO_MIN;
	pll->fvco_max = FVCO_MAX;

	hw = &pll->hw;
	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ERR_PTR(ret);

	clk_hw_set_rate_range(hw, pll->fvco_min, pll->fvco_max);

	return hw;
}

static struct clk_hw *xvcu_clk_hw_register_leaf(struct device *dev,
						const char *name,
						const struct clk_parent_data *parent_data,
						u8 num_parents,
						void __iomem *reg)
{
	u8 mux_flags = CLK_MUX_ROUND_CLOSEST;
	u8 divider_flags = CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
			   CLK_DIVIDER_ROUND_CLOSEST;
	struct clk_hw *mux = NULL;
	struct clk_hw *divider = NULL;
	struct clk_hw *gate = NULL;
	char *name_mux;
	char *name_div;
	int err;
	/* Protect register shared by clocks */
	spinlock_t *lock;

	lock = devm_kzalloc(dev, sizeof(*lock), GFP_KERNEL);
	if (!lock)
		return ERR_PTR(-ENOMEM);
	spin_lock_init(lock);

	name_mux = devm_kasprintf(dev, GFP_KERNEL, "%s%s", name, "_mux");
	if (!name_mux)
		return ERR_PTR(-ENOMEM);
	mux = clk_hw_register_mux_parent_data(dev, name_mux,
					      parent_data, num_parents,
					      CLK_SET_RATE_PARENT,
					      reg, 0, 1, mux_flags, lock);
	if (IS_ERR(mux))
		return mux;

	name_div = devm_kasprintf(dev, GFP_KERNEL, "%s%s", name, "_div");
	if (!name_div) {
		err = -ENOMEM;
		goto unregister_mux;
	}
	divider = clk_hw_register_divider_parent_hw(dev, name_div, mux,
						    CLK_SET_RATE_PARENT,
						    reg, 4, 6, divider_flags,
						    lock);
	if (IS_ERR(divider)) {
		err = PTR_ERR(divider);
		goto unregister_mux;
	}

	gate = clk_hw_register_gate_parent_hw(dev, name, divider,
					      CLK_SET_RATE_PARENT, reg, 12, 0,
					      lock);
	if (IS_ERR(gate)) {
		err = PTR_ERR(gate);
		goto unregister_divider;
	}

	return gate;

unregister_divider:
	clk_hw_unregister_divider(divider);
unregister_mux:
	clk_hw_unregister_mux(mux);

	return ERR_PTR(err);
}

static void xvcu_clk_hw_unregister_leaf(struct clk_hw *hw)
{
	struct clk_hw *gate = hw;
	struct clk_hw *divider;
	struct clk_hw *mux;

	if (!gate)
		return;

	divider = clk_hw_get_parent(gate);
	clk_hw_unregister_gate(gate);
	if (!divider)
		return;

	mux = clk_hw_get_parent(divider);
	clk_hw_unregister_mux(mux);
	if (!divider)
		return;

	clk_hw_unregister_divider(divider);
}

static int xvcu_register_clock_provider(struct xvcu_device *xvcu)
{
	struct device *dev = xvcu->dev;
	struct clk_parent_data parent_data[2] = { 0 };
	struct clk_hw_onecell_data *data;
	struct clk_hw **hws;
	struct clk_hw *hw;
	void __iomem *reg_base = xvcu->vcu_slcr_ba;

	data = devm_kzalloc(dev, struct_size(data, hws, CLK_XVCU_NUM_CLOCKS), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->num = CLK_XVCU_NUM_CLOCKS;
	hws = data->hws;

	xvcu->clk_data = data;

	hw = xvcu_register_pll(dev, reg_base,
			       "vcu_pll", __clk_get_name(xvcu->pll_ref),
			       CLK_SET_RATE_NO_REPARENT | CLK_OPS_PARENT_ENABLE);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	xvcu->pll = hw;

	hw = xvcu_register_pll_post(dev, "vcu_pll_post", xvcu->pll, reg_base);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	xvcu->pll_post = hw;

	parent_data[0].fw_name = "pll_ref";
	parent_data[1].hw = xvcu->pll_post;

	hws[CLK_XVCU_ENC_CORE] =
		xvcu_clk_hw_register_leaf(dev, "venc_core_clk",
					  parent_data,
					  ARRAY_SIZE(parent_data),
					  reg_base + VCU_ENC_CORE_CTRL);
	hws[CLK_XVCU_ENC_MCU] =
		xvcu_clk_hw_register_leaf(dev, "venc_mcu_clk",
					  parent_data,
					  ARRAY_SIZE(parent_data),
					  reg_base + VCU_ENC_MCU_CTRL);
	hws[CLK_XVCU_DEC_CORE] =
		xvcu_clk_hw_register_leaf(dev, "vdec_core_clk",
					  parent_data,
					  ARRAY_SIZE(parent_data),
					  reg_base + VCU_DEC_CORE_CTRL);
	hws[CLK_XVCU_DEC_MCU] =
		xvcu_clk_hw_register_leaf(dev, "vdec_mcu_clk",
					  parent_data,
					  ARRAY_SIZE(parent_data),
					  reg_base + VCU_DEC_MCU_CTRL);

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, data);
}

static void xvcu_unregister_clock_provider(struct xvcu_device *xvcu)
{
	struct clk_hw_onecell_data *data = xvcu->clk_data;
	struct clk_hw **hws = data->hws;

	if (!IS_ERR_OR_NULL(hws[CLK_XVCU_DEC_MCU]))
		xvcu_clk_hw_unregister_leaf(hws[CLK_XVCU_DEC_MCU]);
	if (!IS_ERR_OR_NULL(hws[CLK_XVCU_DEC_CORE]))
		xvcu_clk_hw_unregister_leaf(hws[CLK_XVCU_DEC_CORE]);
	if (!IS_ERR_OR_NULL(hws[CLK_XVCU_ENC_MCU]))
		xvcu_clk_hw_unregister_leaf(hws[CLK_XVCU_ENC_MCU]);
	if (!IS_ERR_OR_NULL(hws[CLK_XVCU_ENC_CORE]))
		xvcu_clk_hw_unregister_leaf(hws[CLK_XVCU_ENC_CORE]);

	clk_hw_unregister_fixed_factor(xvcu->pll_post);
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
	void __iomem *regs;
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

	xvcu->vcu_slcr_ba = devm_ioremap(&pdev->dev, res->start,
					 resource_size(res));
	if (!xvcu->vcu_slcr_ba) {
		dev_err(&pdev->dev, "vcu_slcr register mapping failed.\n");
		return -ENOMEM;
	}

	xvcu->logicore_reg_ba =
		syscon_regmap_lookup_by_compatible("xlnx,vcu-settings");
	if (IS_ERR(xvcu->logicore_reg_ba)) {
		dev_info(&pdev->dev,
			 "could not find xlnx,vcu-settings: trying direct register access\n");

		res = platform_get_resource_byname(pdev,
						   IORESOURCE_MEM, "logicore");
		if (!res) {
			dev_err(&pdev->dev, "get logicore memory resource failed.\n");
			return -ENODEV;
		}

		regs = devm_ioremap(&pdev->dev, res->start, resource_size(res));
		if (!regs) {
			dev_err(&pdev->dev, "logicore register mapping failed.\n");
			return -ENOMEM;
		}

		xvcu->logicore_reg_ba =
			devm_regmap_init_mmio(&pdev->dev, regs,
					      &vcu_settings_regmap_config);
		if (IS_ERR(xvcu->logicore_reg_ba)) {
			dev_err(&pdev->dev, "failed to init regmap\n");
			return PTR_ERR(xvcu->logicore_reg_ba);
		}
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

	/*
	 * Do the Gasket isolation and put the VCU out of reset
	 * Bit 0 : Gasket isolation
	 * Bit 1 : put VCU out of reset
	 */
	regmap_write(xvcu->logicore_reg_ba, VCU_GASKET_INIT, VCU_GASKET_VALUE);

	ret = xvcu_register_clock_provider(xvcu);
	if (ret) {
		dev_err(&pdev->dev, "failed to register clock provider\n");
		goto error_clk_provider;
	}

	dev_set_drvdata(&pdev->dev, xvcu);

	return 0;

error_clk_provider:
	xvcu_unregister_clock_provider(xvcu);
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
static void xvcu_remove(struct platform_device *pdev)
{
	struct xvcu_device *xvcu;

	xvcu = platform_get_drvdata(pdev);

	xvcu_unregister_clock_provider(xvcu);

	/* Add the Gasket isolation and put the VCU in reset. */
	regmap_write(xvcu->logicore_reg_ba, VCU_GASKET_INIT, 0);

	clk_disable_unprepare(xvcu->aclk);
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
	.remove_new             = xvcu_remove,
};

module_platform_driver(xvcu_driver);

MODULE_AUTHOR("Dhaval Shah <dshah@xilinx.com>");
MODULE_DESCRIPTION("Xilinx VCU init Driver");
MODULE_LICENSE("GPL v2");
