// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026, Beijing ESWIN Computing Technology Co., Ltd..
 * All rights reserved.
 *
 * ESWIN EIC7700 HSP Clock Driver
 *
 * Authors: Xuyang Dong <dongxuyang@eswincomputing.com>
 */

#include <linux/auxiliary_bus.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/eswin,eic7700-hspcrg.h>

#include "common.h"

#define EIC7700_HSP_SATA_REG		0x300
#define EIC7700_HSP_MSHC0_REG		0x510
#define EIC7700_HSP_MSHC1_REG		0x610
#define EIC7700_HSP_MSHC2_REG		0x710
#define EIC7700_HSP_USB0_REG		0x800
#define EIC7700_HSP_USB0_REF_REG	0x83c
#define EIC7700_HSP_USB1_REG		0x900
#define EIC7700_HSP_USB1_REF_REG	0x93c

#define USB_REF_XTAL24M			0x2a
#define EIC7700_HSP_NR_CLKS		(EIC7700_HSP_CLK_GATE_SATA + 1)

struct eic7700_hsp_clk_gate {
	struct clk_hw hw;
	unsigned int id;
	struct regmap *regmap;
	unsigned int reg;
	unsigned int ref_reg;
	const char *name;
	const struct clk_parent_data *parent_data;
	unsigned long flags;
	unsigned int offset;
	unsigned int ref_offset;
	u8 bit_idx;
};

static const struct regmap_config eic7700_hsp_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.max_register = 0x1ffc,
	.reg_stride = 4,
	.fast_io = true,
	.use_raw_spinlock = true,
};

static inline struct eic7700_hsp_clk_gate *to_gate_clk(struct clk_hw *hw)
{
	return container_of(hw, struct eic7700_hsp_clk_gate, hw);
}

#define EIC7700_HSP_GATE(_id, _name, _pdata, _flags, _offset, _idx,	\
			 _ref_offset)					\
	{								\
		.id		= _id,					\
		.name		= _name,				\
		.parent_data	= _pdata,				\
		.flags		= _flags,				\
		.offset		= _offset,				\
		.ref_offset	= _ref_offset,				\
		.bit_idx	= _idx,					\
	}

static void hsp_clk_gate_endisable(struct clk_hw *hw, bool enable)
{
	struct eic7700_hsp_clk_gate *gate = to_gate_clk(hw);

	if (enable) {
		/*
		 * Hardware bug: The USB reference clock must be 24MHz.
		 * The default register value after reset is invalid.
		 * Workaround: Rewrite the correct value before enabling
		 * the USB gate clock.
		 */
		regmap_update_bits(gate->regmap, gate->ref_reg, 0x3f,
				   USB_REF_XTAL24M);
	}
	regmap_assign_bits(gate->regmap, gate->reg, BIT(gate->bit_idx), enable);
}

static int hsp_clk_gate_enable(struct clk_hw *hw)
{
	hsp_clk_gate_endisable(hw, true);

	return 0;
}

static void hsp_clk_gate_disable(struct clk_hw *hw)
{
	hsp_clk_gate_endisable(hw, false);
}

static int hsp_clk_gate_is_enabled(struct clk_hw *hw)
{
	struct eic7700_hsp_clk_gate *gate = to_gate_clk(hw);
	unsigned int val;
	int ret;

	ret = regmap_read(gate->regmap, gate->reg, &val);
	if (ret != 0)
		return ret;

	return !!(val & BIT(gate->bit_idx));
}

static const struct clk_ops hsp_clk_gate_ops = {
	.enable = hsp_clk_gate_enable,
	.disable = hsp_clk_gate_disable,
	.is_enabled = hsp_clk_gate_is_enabled,
};

static struct clk_hw *
hsp_clk_register_gate(struct device *dev, unsigned int id, const char *name,
		      const struct clk_parent_data *parent_data,
		      unsigned long flags, struct regmap *regmap,
		      unsigned int reg, unsigned int ref_reg, u8 bit_idx)
{
	struct eic7700_hsp_clk_gate *gate;
	struct clk_init_data init = {};
	struct clk_hw *hw;
	int ret;

	gate = devm_kzalloc(dev, sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &hsp_clk_gate_ops;
	init.flags = flags;
	init.parent_data = parent_data;
	init.num_parents = 1;

	gate->id = id;
	gate->regmap = regmap;
	gate->reg = reg;
	gate->ref_reg = ref_reg;
	gate->bit_idx = bit_idx;
	gate->hw.init = &init;

	hw = &gate->hw;
	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		hw = ERR_PTR(ret);

	return hw;
}

static const struct clk_parent_data hsp_cfg[] = {
	{ .index = 0 }
};

static const struct clk_parent_data hsp_mmc[] = {
	{ .index = 1 }
};

static const struct clk_parent_data hsp_usb_sata[] = {
	{ .index = 2 }
};

static struct eswin_fixed_factor_clock eic7700_hsp_factor_clks[] = {
	ESWIN_FACTOR(EIC7700_HSP_CLK_FAC_CFG_DIV2, "factor_hsp_cfg_div2",
		     hsp_cfg, 1, 2, 0),
	ESWIN_FACTOR(EIC7700_HSP_CLK_FAC_CFG_DIV4, "factor_hsp_cfg_div4",
		     hsp_cfg, 1, 4, 0),
	ESWIN_FACTOR(EIC7700_HSP_CLK_FAC_MMC_DIV10, "factor_hsp_mmc_div10",
		     hsp_mmc, 1, 10, 0),
};

static struct eswin_gate_clock eic7700_hsp_gate_clks[] = {
	ESWIN_GATE(EIC7700_HSP_CLK_GATE_SATA, "gate_clk_hsp_sata", hsp_usb_sata,
		   CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
		   EIC7700_HSP_SATA_REG, 28, 0),
	ESWIN_GATE(EIC7700_HSP_CLK_GATE_MSHC0_TMR, "gate_clk_hsp_mshc0_tmr",
		   hsp_mmc, CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
		   EIC7700_HSP_MSHC0_REG, 8, 0),
	ESWIN_GATE(EIC7700_HSP_CLK_GATE_MSHC1_TMR, "gate_clk_hsp_mshc1_tmr",
		   hsp_mmc, CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
		   EIC7700_HSP_MSHC1_REG, 8, 0),
	ESWIN_GATE(EIC7700_HSP_CLK_GATE_MSHC2_TMR, "gate_clk_hsp_mshc2_tmr",
		   hsp_mmc, CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
		   EIC7700_HSP_MSHC2_REG, 8, 0),
};

static struct eic7700_hsp_clk_gate eic7700_hsp_spec_gate_clks[] = {
	EIC7700_HSP_GATE(EIC7700_HSP_CLK_GATE_USB0, "gate_clk_hsp_usb0",
			 hsp_usb_sata, CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			 EIC7700_HSP_USB0_REG, 28, EIC7700_HSP_USB0_REF_REG),
	EIC7700_HSP_GATE(EIC7700_HSP_CLK_GATE_USB1, "gate_clk_hsp_usb1",
			 hsp_usb_sata, CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			 EIC7700_HSP_USB1_REG, 28, EIC7700_HSP_USB1_REF_REG),
};

static const struct clk_parent_data mux_mmc_3mux1_p[] = {
	{ .fw_name = "cfg" },
	{ .hw = &eic7700_hsp_factor_clks[0].hw },
	{ .hw = &eic7700_hsp_factor_clks[1].hw },
};

static const struct clk_parent_data mux_mmc_2mux1_p[] = {
	{ .fw_name = "mmc" },
	{ .hw = &eic7700_hsp_factor_clks[2].hw },
};

static u32 mux_mmc_3mux1_tbl[] = { 0x0, 0x1, 0x3 };

static struct eswin_mux_clock eic7700_hsp_mux_clks[] = {
	ESWIN_MUX_TBL(EIC7700_HSP_CLK_MUX_EMMC_3MUX1, "mux_hsp_emmc_3mux1",
		      mux_mmc_3mux1_p, ARRAY_SIZE(mux_mmc_3mux1_p),
		      CLK_SET_RATE_PARENT, EIC7700_HSP_MSHC0_REG, 16, 2, 0,
		      mux_mmc_3mux1_tbl),
	ESWIN_MUX_TBL(EIC7700_HSP_CLK_MUX_SD0_3MUX1, "mux_hsp_sd0_3mux1",
		      mux_mmc_3mux1_p, ARRAY_SIZE(mux_mmc_3mux1_p),
		      CLK_SET_RATE_PARENT, EIC7700_HSP_MSHC1_REG, 16, 2, 0,
		      mux_mmc_3mux1_tbl),
	ESWIN_MUX_TBL(EIC7700_HSP_CLK_MUX_SD1_3MUX1, "mux_hsp_sd1_3mux1",
		      mux_mmc_3mux1_p, ARRAY_SIZE(mux_mmc_3mux1_p),
		      CLK_SET_RATE_PARENT, EIC7700_HSP_MSHC2_REG, 16, 2, 0,
		      mux_mmc_3mux1_tbl),
	ESWIN_MUX(EIC7700_HSP_CLK_MUX_EMMC_CQE_2MUX1, "mux_hsp_emmc_cqe_2mux1",
		  mux_mmc_2mux1_p, ARRAY_SIZE(mux_mmc_2mux1_p),
		  CLK_SET_RATE_PARENT, EIC7700_HSP_MSHC0_REG, 0, 1, 0),
	ESWIN_MUX(EIC7700_HSP_CLK_MUX_SD0_CQE_2MUX1, "mux_hsp_sd0_cqe_2mux1",
		  mux_mmc_2mux1_p, ARRAY_SIZE(mux_mmc_2mux1_p),
		  CLK_SET_RATE_PARENT, EIC7700_HSP_MSHC1_REG, 0, 1, 0),
	ESWIN_MUX(EIC7700_HSP_CLK_MUX_SD1_CQE_2MUX1, "mux_hsp_sd1_cqe_2mux1",
		  mux_mmc_2mux1_p, ARRAY_SIZE(mux_mmc_2mux1_p),
		  CLK_SET_RATE_PARENT, EIC7700_HSP_MSHC2_REG, 0, 1, 0),
};

static struct eswin_clk_info eic7700_hsp_clks[] = {
	ESWIN_GATE_TYPE(EIC7700_HSP_CLK_GATE_EMMC, "gate_clk_hsp_emmc",
			EIC7700_HSP_CLK_MUX_EMMC_3MUX1,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_HSP_MSHC0_REG, 24, 0),
	ESWIN_GATE_TYPE(EIC7700_HSP_CLK_GATE_SD0, "gate_clk_hsp_sd0",
			EIC7700_HSP_CLK_MUX_SD0_3MUX1,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_HSP_MSHC1_REG, 24, 0),
	ESWIN_GATE_TYPE(EIC7700_HSP_CLK_GATE_SD1, "gate_clk_hsp_sd1",
			EIC7700_HSP_CLK_MUX_SD1_3MUX1,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_HSP_MSHC2_REG, 24, 0),
};

static int eic7700_hsp_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct auxiliary_device *adev;
	struct eswin_clock_data *data;
	struct regmap *regmap;
	struct clk_hw *hw;
	int i, ret;

	data = eswin_clk_init(pdev, EIC7700_HSP_NR_CLKS);
	if (IS_ERR(data))
		return dev_err_probe(dev, PTR_ERR(data),
				     "failed to get clk data!\n");

	regmap = devm_regmap_init_mmio(dev, data->base,
				       &eic7700_hsp_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "failed to get regmap!\n");

	ret = eswin_clk_register_fixed_factor(dev, eic7700_hsp_factor_clks,
					      ARRAY_SIZE(eic7700_hsp_factor_clks),
					      data);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register fixed factor clock\n");

	ret = eswin_clk_register_gate(dev, eic7700_hsp_gate_clks,
				      ARRAY_SIZE(eic7700_hsp_gate_clks), data);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register gate clock\n");

	ret = eswin_clk_register_mux(dev, eic7700_hsp_mux_clks,
				     ARRAY_SIZE(eic7700_hsp_mux_clks),
				     data);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register mux clock\n");

	ret = eswin_clk_register_clks(dev, eic7700_hsp_clks,
				      ARRAY_SIZE(eic7700_hsp_clks), data);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register clock\n");

	for (i = 0; i < ARRAY_SIZE(eic7700_hsp_spec_gate_clks); i++) {
		struct eic7700_hsp_clk_gate *gate;

		gate = &eic7700_hsp_spec_gate_clks[i];
		hw = hsp_clk_register_gate(dev, gate->id, gate->name,
					   gate->parent_data, gate->flags,
					   regmap, gate->offset,
					   gate->ref_offset, gate->bit_idx);
		if (IS_ERR(hw))
			return dev_err_probe(dev, PTR_ERR(hw),
					     "failed to register gate clock\n");

		data->clk_data.hws[gate->id] = hw;
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					  &data->clk_data);
	if (ret)
		return dev_err_probe(dev, ret, "add clk provider failed\n");

	adev = devm_auxiliary_device_create(dev, "hsp-reset", NULL);
	if (!adev)
		return dev_err_probe(dev, -ENODEV,
				     "register hsp-reset device failed\n");

	return 0;
}

static const struct of_device_id eic7700_hsp_clock_dt_ids[] = {
	{ .compatible = "eswin,eic7700-hspcrg", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, eic7700_hsp_clock_dt_ids);

static struct platform_driver eic7700_hsp_clock_driver = {
	.probe	= eic7700_hsp_clk_probe,
	.driver = {
		.name	= "eic7700-hsp-clock",
		.of_match_table	= eic7700_hsp_clock_dt_ids,
	},
};

module_platform_driver(eic7700_hsp_clock_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xuyang Dong <dongxuyang@eswincomputing.com>");
MODULE_DESCRIPTION("ESWIN EIC7700 HSP clock controller driver");
