// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk-provider.h>
#include <linux/reset-controller.h>
#include <linux/mfd/rk628.h>
#include <dt-bindings/reset/rk628-rgu.h>
#include <dt-bindings/clock/rk628-cgu.h>

#include "clk-regmap.h"

#define REG(x)			((x) + 0xc0000)

#define CRU_CPLL_CON0		REG(0x0000)
#define CRU_CPLL_CON1		REG(0x0004)
#define CRU_CPLL_CON2		REG(0x0008)
#define CRU_CPLL_CON3		REG(0x000c)
#define CRU_CPLL_CON4		REG(0x0010)
#define CRU_GPLL_CON0		REG(0x0020)
#define CRU_GPLL_CON1		REG(0x0024)
#define CRU_GPLL_CON2		REG(0x0028)
#define CRU_GPLL_CON3		REG(0x002c)
#define CRU_GPLL_CON4		REG(0x0030)
#define CRU_MODE_CON		REG(0x0060)
#define CRU_CLKSEL_CON00	REG(0x0080)
#define CRU_CLKSEL_CON01	REG(0x0084)
#define CRU_CLKSEL_CON02	REG(0x0088)
#define CRU_CLKSEL_CON03	REG(0x008c)
#define CRU_CLKSEL_CON04	REG(0x0090)
#define CRU_CLKSEL_CON05	REG(0x0094)
#define CRU_CLKSEL_CON06	REG(0x0098)
#define CRU_CLKSEL_CON07	REG(0x009c)
#define CRU_CLKSEL_CON08	REG(0x00a0)
#define CRU_CLKSEL_CON09	REG(0x00a4)
#define CRU_CLKSEL_CON10	REG(0x00a8)
#define CRU_CLKSEL_CON11	REG(0x00ac)
#define CRU_CLKSEL_CON12	REG(0x00b0)
#define CRU_CLKSEL_CON13	REG(0x00b4)
#define CRU_CLKSEL_CON14	REG(0x00b8)
#define CRU_CLKSEL_CON15	REG(0x00bc)
#define CRU_CLKSEL_CON16	REG(0x00c0)
#define CRU_CLKSEL_CON17	REG(0x00c4)
#define CRU_CLKSEL_CON18	REG(0x00c8)
#define CRU_CLKSEL_CON20	REG(0x00d0)
#define CRU_CLKSEL_CON21	REG(0x00d4)
#define CRU_GATE_CON00		REG(0x0180)
#define CRU_GATE_CON01		REG(0x0184)
#define CRU_GATE_CON02		REG(0x0188)
#define CRU_GATE_CON03		REG(0x018c)
#define CRU_GATE_CON04		REG(0x0190)
#define CRU_GATE_CON05		REG(0x0194)
#define CRU_SOFTRST_CON00	REG(0x0200)
#define CRU_SOFTRST_CON01	REG(0x0204)
#define CRU_SOFTRST_CON02	REG(0x0208)
#define CRU_SOFTRST_CON04	REG(0x0210)
#define CRU_MAX_REGISTER	CRU_SOFTRST_CON04

#define reset_to_cru(_rst) container_of(_rst, struct rk628_cru, rcdev)

struct rk628_cru {
	struct device *dev;
	struct rk628 *parent;
	struct regmap *regmap;
	struct reset_controller_dev rcdev;
	struct clk_onecell_data clk_data;
};

#define CNAME(x) "rk628_" x

#define PNAME(x) static const char *const x[]

PNAME(mux_cpll_osc_p) = { "xin_osc0_func", CNAME("clk_cpll") };
PNAME(mux_gpll_osc_p) = { "xin_osc0_func", CNAME("clk_gpll") };
PNAME(mux_cpll_gpll_mux_p) = { CNAME("clk_cpll_mux"), CNAME("clk_gpll_mux") };
PNAME(mux_mclk_i2s_8ch_p) = { CNAME("clk_i2s_8ch_src"), CNAME("clk_i2s_8ch_frac"), "i2s_mclkin", "xin_osc0_half" };
PNAME(mux_i2s_mclkout_p) = { CNAME("mclk_i2s_8ch"), "xin_osc0_half" };

static const struct clk_pll_data rk628_clk_plls[] = {
	RK628_PLL(CGU_CLK_CPLL, CNAME("clk_cpll"), "xin_osc0_func",
		  CRU_CPLL_CON0,
		  0),
	RK628_PLL(CGU_CLK_GPLL, CNAME("clk_gpll"), "xin_osc0_func",
		  CRU_GPLL_CON0,
		  0),
};

static const struct clk_mux_data rk628_clk_muxes[] = {
	MUX(CGU_CLK_CPLL_MUX, CNAME("clk_cpll_mux"), mux_cpll_osc_p,
	    CRU_MODE_CON, 0, 1,
	    0),
	MUX(CGU_CLK_GPLL_MUX, CNAME("clk_gpll_mux"), mux_gpll_osc_p,
	    CRU_MODE_CON, 2, 1,
	    0),
};

static const struct clk_gate_data rk628_clk_gates[] = {
	GATE(CGU_PCLK_GPIO0, CNAME("pclk_gpio0"), CNAME("pclk_logic"),
	     CRU_GATE_CON01, 0,
	     0),
	GATE(CGU_PCLK_GPIO1, CNAME("pclk_gpio1"), CNAME("pclk_logic"),
	     CRU_GATE_CON01, 1,
	     0),
	GATE(CGU_PCLK_GPIO2, CNAME("pclk_gpio2"), CNAME("pclk_logic"),
	     CRU_GATE_CON01, 2,
	     0),
	GATE(CGU_PCLK_GPIO3, CNAME("pclk_gpio3"), CNAME("pclk_logic"),
	     CRU_GATE_CON01, 3,
	     0),

	GATE(CGU_PCLK_TXPHY_CON, CNAME("pclk_txphy_con"), CNAME("pclk_logic"),
	     CRU_GATE_CON02, 3,
	     CLK_IGNORE_UNUSED),
	GATE(CGU_PCLK_EFUSE, CNAME("pclk_efuse"), CNAME("pclk_logic"),
	     CRU_GATE_CON00, 5,
	     0),
	GATE(0, CNAME("pclk_i2c2apb"), CNAME("pclk_logic"),
	     CRU_GATE_CON00, 3,
	     CLK_IGNORE_UNUSED),
	GATE(0, CNAME("pclk_cru"), CNAME("pclk_logic"),
	     CRU_GATE_CON00, 1,
	     CLK_IGNORE_UNUSED),
	GATE(0, CNAME("pclk_adapter"), CNAME("pclk_logic"),
	     CRU_GATE_CON00, 7,
	     CLK_IGNORE_UNUSED),
	GATE(0, CNAME("pclk_regfile"), CNAME("pclk_logic"),
	     CRU_GATE_CON00, 2,
	     CLK_IGNORE_UNUSED),
	GATE(CGU_PCLK_DSI0, CNAME("pclk_dsi0"), CNAME("pclk_logic"),
	     CRU_GATE_CON02, 6,
	     0),
	GATE(CGU_PCLK_DSI1, CNAME("pclk_dsi1"), CNAME("pclk_logic"),
	     CRU_GATE_CON02, 7,
	     0),
	GATE(CGU_PCLK_CSI, CNAME("pclk_csi"), CNAME("pclk_logic"),
	     CRU_GATE_CON02, 8,
	     0),
	GATE(CGU_PCLK_HDMITX, CNAME("pclk_hdmitx"), CNAME("pclk_logic"),
	     CRU_GATE_CON02, 4,
	     0),
	GATE(CGU_PCLK_RXPHY, CNAME("pclk_rxphy"), CNAME("pclk_logic"),
	     CRU_GATE_CON02, 0,
	     0),
	GATE(CGU_PCLK_HDMIRX, CNAME("pclk_hdmirx"), CNAME("pclk_logic"),
	     CRU_GATE_CON02, 2,
	     0),
	GATE(CGU_PCLK_GVIHOST, CNAME("pclk_gvihost"), CNAME("pclk_logic"),
	     CRU_GATE_CON02, 5,
	     0),
	GATE(CGU_CLK_CFG_DPHY0, CNAME("clk_cfg_dphy0"), "xin_osc0_func",
	     CRU_GATE_CON02, 13,
	     0),
	GATE(CGU_CLK_CFG_DPHY1, CNAME("clk_cfg_dphy1"), "xin_osc0_func",
	     CRU_GATE_CON02, 14,
	     0),
	GATE(CGU_CLK_TXESC, CNAME("clk_txesc"), "xin_osc0_func",
	     CRU_GATE_CON02, 12,
	     0),
};

static const struct clk_composite_data rk628_clk_composites[] = {
	COMPOSITE(CGU_CLK_IMODET, CNAME("clk_imodet"), mux_cpll_gpll_mux_p,
		  CRU_CLKSEL_CON05, 5, 1,
		  CRU_CLKSEL_CON05, 0, 5,
		  CRU_GATE_CON02, 11,
		  0),
	COMPOSITE(CGU_CLK_HDMIRX_AUD, CNAME("clk_hdmirx_aud"), mux_cpll_gpll_mux_p,
		  CRU_CLKSEL_CON05, 15, 1,
		  CRU_CLKSEL_CON05, 6, 8,
		  CRU_GATE_CON02, 10,
		  0),
	COMPOSITE_FRAC_NOMUX(CGU_CLK_HDMIRX_CEC, CNAME("clk_hdmirx_cec"), "xin_osc0_func",
			     CRU_CLKSEL_CON12,
			     CRU_GATE_CON01, 15,
			     0),
	COMPOSITE_FRAC(CGU_CLK_RX_READ, CNAME("clk_rx_read"), mux_cpll_gpll_mux_p,
		       CRU_CLKSEL_CON02, 8, 1,
		       CRU_CLKSEL_CON14,
		       CRU_GATE_CON00, 11,
		       0),
	COMPOSITE_FRAC(CGU_SCLK_VOP, CNAME("sclk_vop"), mux_cpll_gpll_mux_p,
		       CRU_CLKSEL_CON02, 9, 1,
		       CRU_CLKSEL_CON13,
		       CRU_GATE_CON00, 13,
		       CLK_SET_RATE_NO_REPARENT),
	COMPOSITE(CGU_PCLK_LOGIC, CNAME("pclk_logic"), mux_cpll_gpll_mux_p,
		  CRU_CLKSEL_CON00, 7, 1,
		  CRU_CLKSEL_CON00, 0, 5,
		  CRU_GATE_CON00, 0,
		  0),
	COMPOSITE_NOMUX(CGU_CLK_GPIO_DB0, CNAME("clk_gpio_db0"), "xin_osc0_func",
		  CRU_CLKSEL_CON08, 0, 10,
		  CRU_GATE_CON01, 4,
		  0),
	COMPOSITE_NOMUX(CGU_CLK_GPIO_DB1, CNAME("clk_gpio_db1"), "xin_osc0_func",
		  CRU_CLKSEL_CON09, 0, 10,
		  CRU_GATE_CON01, 5,
		  0),
	COMPOSITE_NOMUX(CGU_CLK_GPIO_DB2, CNAME("clk_gpio_db2"), "xin_osc0_func",
		  CRU_CLKSEL_CON10, 0, 10,
		  CRU_GATE_CON01, 6,
		  0),
	COMPOSITE_NOMUX(CGU_CLK_GPIO_DB3, CNAME("clk_gpio_db3"), "xin_osc0_func",
		  CRU_CLKSEL_CON11, 0, 10,
		  CRU_GATE_CON01, 7,
		  0),
	COMPOSITE(CGU_CLK_I2S_8CH_SRC, CNAME("clk_i2s_8ch_src"), mux_cpll_gpll_mux_p,
		  CRU_CLKSEL_CON03, 13, 1,
		  CRU_CLKSEL_CON03, 8, 5,
		  CRU_GATE_CON03, 9,
		  0),
	COMPOSITE_FRAC_NOMUX(CGU_CLK_I2S_8CH_FRAC, CNAME("clk_i2s_8ch_frac"), CNAME("clk_i2s_8ch_src"),
			     CRU_CLKSEL_CON04,
			     CRU_GATE_CON03, 10,
			     0),
	COMPOSITE_NODIV(CGU_MCLK_I2S_8CH, CNAME("mclk_i2s_8ch"), mux_mclk_i2s_8ch_p,
			CRU_CLKSEL_CON03, 14, 2,
			CRU_GATE_CON03, 11,
			CLK_SET_RATE_PARENT),
	COMPOSITE_NODIV(CGU_I2S_MCLKOUT, CNAME("i2s_mclkout"), mux_i2s_mclkout_p,
			CRU_CLKSEL_CON03, 7, 1,
			CRU_GATE_CON03, 12,
			CLK_SET_RATE_PARENT),
	COMPOSITE(CGU_BT1120DEC, CNAME("clk_bt1120dec"), mux_cpll_gpll_mux_p,
		  CRU_CLKSEL_CON02, 7, 1,
		  CRU_CLKSEL_CON02, 0, 5,
		  CRU_GATE_CON00, 12,
		  0),
};

static void rk628_clk_add_lookup(struct rk628_cru *cru, struct clk *clk,
				 unsigned int id)
{
	if (cru->clk_data.clks && id)
		cru->clk_data.clks[id] = clk;
}

static void rk628_clk_register_muxes(struct rk628_cru *cru)
{
	struct clk *clk;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rk628_clk_muxes); i++) {
		const struct clk_mux_data *data = &rk628_clk_muxes[i];

		clk = devm_clk_regmap_register_mux(cru->dev, data->name,
						   data->parent_names,
						   data->num_parents,
						   cru->regmap, data->reg,
						   data->shift, data->width,
						   data->flags);
		if (IS_ERR(clk)) {
			dev_err(cru->dev, "failed to register clock %s\n",
				data->name);
			continue;
		}

		rk628_clk_add_lookup(cru, clk, data->id);
	}
}

static void rk628_clk_register_gates(struct rk628_cru *cru)
{
	struct clk *clk;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rk628_clk_gates); i++) {
		const struct clk_gate_data *data = &rk628_clk_gates[i];

		clk = devm_clk_regmap_register_gate(cru->dev, data->name,
						    data->parent_name,
						    cru->regmap,
						    data->reg, data->shift,
						    data->flags);
		if (IS_ERR(clk)) {
			dev_err(cru->dev, "failed to register clock %s\n",
				data->name);
			continue;
		}

		rk628_clk_add_lookup(cru, clk, data->id);
	}
}

static void rk628_clk_register_composites(struct rk628_cru *cru)
{
	struct clk *clk;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rk628_clk_composites); i++) {
		const struct clk_composite_data *data =
					&rk628_clk_composites[i];

		clk = devm_clk_regmap_register_composite(cru->dev, data->name,
							 data->parent_names,
							 data->num_parents,
							 cru->regmap,
							 data->mux_reg,
							 data->mux_shift,
							 data->mux_width,
							 data->div_reg,
							 data->div_shift,
							 data->div_width,
							 data->div_flags,
							 data->gate_reg,
							 data->gate_shift,
							 data->flags);
		if (IS_ERR(clk)) {
			dev_err(cru->dev, "failed to register clock %s\n",
				data->name);
			continue;
		}

		rk628_clk_add_lookup(cru, clk, data->id);
	}
}

static void rk628_clk_register_plls(struct rk628_cru *cru)
{
	struct clk *clk;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rk628_clk_plls); i++) {
		const struct clk_pll_data *data = &rk628_clk_plls[i];

		clk = devm_clk_regmap_register_pll(cru->dev, data->name,
						   data->parent_name,
						   cru->regmap,
						   data->reg,
						   data->pd_shift,
						   data->dsmpd_shift,
						   data->lock_shift,
						   data->flags);
		if (IS_ERR(clk)) {
			dev_err(cru->dev, "failed to register clock %s\n",
				data->name);
			continue;
		}

		rk628_clk_add_lookup(cru, clk, data->id);
	}
}

struct rk628_rgu_data {
	unsigned int id;
	unsigned int reg;
	unsigned int bit;
};

#define RSTGEN(_id, _reg, _bit)	\
	{	\
		.id = (_id),	\
		.reg = (_reg),	\
		.bit = (_bit),	\
	}

static const struct rk628_rgu_data rk628_rgu_data[] = {
	RSTGEN(RGU_LOGIC,	CRU_SOFTRST_CON00,  0),
	RSTGEN(RGU_CRU,		CRU_SOFTRST_CON00,  1),
	RSTGEN(RGU_REGFILE,	CRU_SOFTRST_CON00,  2),
	RSTGEN(RGU_I2C2APB,	CRU_SOFTRST_CON00,  3),
	RSTGEN(RGU_EFUSE,	CRU_SOFTRST_CON00,  5),
	RSTGEN(RGU_ADAPTER,	CRU_SOFTRST_CON00,  7),
	RSTGEN(RGU_CLK_RX,	CRU_SOFTRST_CON00, 11),
	RSTGEN(RGU_BT1120DEC,	CRU_SOFTRST_CON00, 12),
	RSTGEN(RGU_VOP,		CRU_SOFTRST_CON00, 13),

	RSTGEN(RGU_GPIO0,	CRU_SOFTRST_CON01,  0),
	RSTGEN(RGU_GPIO1,	CRU_SOFTRST_CON01,  1),
	RSTGEN(RGU_GPIO2,	CRU_SOFTRST_CON01,  2),
	RSTGEN(RGU_GPIO3,	CRU_SOFTRST_CON01,  3),
	RSTGEN(RGU_GPIO_DB0,	CRU_SOFTRST_CON01,  4),
	RSTGEN(RGU_GPIO_DB1,	CRU_SOFTRST_CON01,  5),
	RSTGEN(RGU_GPIO_DB2,	CRU_SOFTRST_CON01,  6),
	RSTGEN(RGU_GPIO_DB3,	CRU_SOFTRST_CON01,  7),

	RSTGEN(RGU_RXPHY,	CRU_SOFTRST_CON02,  0),
	RSTGEN(RGU_HDMIRX,	CRU_SOFTRST_CON02,  2),
	RSTGEN(RGU_TXPHY_CON,	CRU_SOFTRST_CON02,  3),
	RSTGEN(RGU_HDMITX,	CRU_SOFTRST_CON02,  4),
	RSTGEN(RGU_GVIHOST,	CRU_SOFTRST_CON02,  5),
	RSTGEN(RGU_DSI0,	CRU_SOFTRST_CON02,  6),
	RSTGEN(RGU_DSI1,	CRU_SOFTRST_CON02,  7),
	RSTGEN(RGU_CSI,		CRU_SOFTRST_CON02,  8),
	RSTGEN(RGU_TXDATA,	CRU_SOFTRST_CON02,  9),
	RSTGEN(RGU_DECODER,	CRU_SOFTRST_CON02, 10),
	RSTGEN(RGU_ENCODER,	CRU_SOFTRST_CON02, 11),
	RSTGEN(RGU_HDMIRX_PON,	CRU_SOFTRST_CON02, 12),
	RSTGEN(RGU_TXBYTEHS,	CRU_SOFTRST_CON02, 13),
	RSTGEN(RGU_TXESC,	CRU_SOFTRST_CON02, 14),
};

static int rk628_rgu_update(struct rk628_cru *cru, unsigned long id, int assert)
{
	const struct rk628_rgu_data *data = &rk628_rgu_data[id];

	return regmap_write(cru->regmap, data->reg,
			    BIT(data->bit + 16) | (assert << data->bit));
}

static int rk628_rgu_assert(struct reset_controller_dev *rcdev,
			    unsigned long id)
{
	struct rk628_cru *cru = reset_to_cru(rcdev);

	return rk628_rgu_update(cru, id, 1);
}

static int rk628_rgu_deassert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct rk628_cru *cru = reset_to_cru(rcdev);

	return rk628_rgu_update(cru, id, 0);
}

static struct reset_control_ops rk628_rgu_ops = {
	.assert = rk628_rgu_assert,
	.deassert = rk628_rgu_deassert,
};

static int rk628_reset_controller_register(struct rk628_cru *cru)
{
	struct device *dev = cru->dev;

	cru->rcdev.owner = THIS_MODULE;
	cru->rcdev.nr_resets =  ARRAY_SIZE(rk628_rgu_data);
	cru->rcdev.of_node = dev->of_node;
	cru->rcdev.ops = &rk628_rgu_ops;

	return reset_controller_register(&cru->rcdev);
}

static const struct regmap_range rk628_cru_readable_ranges[] = {
	regmap_reg_range(CRU_CPLL_CON0, CRU_CPLL_CON4),
	regmap_reg_range(CRU_GPLL_CON0, CRU_GPLL_CON4),
	regmap_reg_range(CRU_MODE_CON, CRU_MODE_CON),
	regmap_reg_range(CRU_CLKSEL_CON00, CRU_CLKSEL_CON21),
	regmap_reg_range(CRU_GATE_CON00, CRU_GATE_CON05),
	regmap_reg_range(CRU_SOFTRST_CON00, CRU_SOFTRST_CON04),
};

static const struct regmap_access_table rk628_cru_readable_table = {
	.yes_ranges     = rk628_cru_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_cru_readable_ranges),
};

static const struct regmap_config rk628_cru_regmap_config = {
	.name = "cru",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = CRU_MAX_REGISTER,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.rd_table = &rk628_cru_readable_table,
};

static int rk628_cru_probe(struct platform_device *pdev)
{
	struct rk628 *rk628 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk628_cru *cru;
	struct clk **clk_table;
	unsigned int i;
	int ret;

	cru = devm_kzalloc(dev, sizeof(*cru), GFP_KERNEL);
	if (!cru)
		return -ENOMEM;

	cru->regmap = devm_regmap_init_i2c(rk628->client,
					   &rk628_cru_regmap_config);
	if (IS_ERR(cru->regmap)) {
		ret = PTR_ERR(cru->regmap);
		dev_err(dev, "failed to allocate register map: %d\n", ret);
		return ret;
	}

	/* clock switch and first set gpll almost 99MHz */
	regmap_write(cru->regmap, CRU_GPLL_CON0, 0xffff701d);
	usleep_range(1000, 1100);
	/* set clk_gpll_mux from gpll */
	regmap_write(cru->regmap, CRU_MODE_CON, 0xffff0004);
	usleep_range(1000, 1100);
	/* set pclk_logic from clk_gpll_mux and set pclk div 4 */
	regmap_write(cru->regmap, CRU_CLKSEL_CON00, 0xff0080);
	regmap_write(cru->regmap, CRU_CLKSEL_CON00, 0xff0083);
	/* set cpll almost 400MHz */
	regmap_write(cru->regmap, CRU_CPLL_CON0, 0xffff3063);
	usleep_range(1000, 1100);
	/* set clk_cpll_mux from clk_cpll */
	regmap_write(cru->regmap, CRU_MODE_CON, 0xffff0005);
	/* set pclk use cpll, now div is 4 */
	regmap_write(cru->regmap, CRU_CLKSEL_CON00, 0xff0003);
	/* set pclk use cpll, now div is 12 */
	regmap_write(cru->regmap, CRU_CLKSEL_CON00, 0xff000b);
	/* gpll 983.04MHz */
	regmap_write(cru->regmap, CRU_GPLL_CON0, 0xffff1028);
	usleep_range(1000, 1100);
	/* set pclk use gpll, nuw div is 0xb */
	regmap_write(cru->regmap, CRU_CLKSEL_CON00, 0xff008b);
	/* set cpll 1188MHz */
	regmap_write(cru->regmap, CRU_CPLL_CON0, 0xffff1063);
	usleep_range(1000, 1100);
	/* set pclk use cpll, and set pclk 99MHz */
	regmap_write(cru->regmap, CRU_CLKSEL_CON00, 0xff000b);

	clk_table = devm_kcalloc(dev, CGU_NR_CLKS, sizeof(struct clk *),
				 GFP_KERNEL);
	if (!clk_table)
		return -ENOMEM;

	for (i = 0; i < CGU_NR_CLKS; i++)
		clk_table[i] = ERR_PTR(-ENOENT);

	cru->dev = dev;
	cru->parent = rk628;
	cru->clk_data.clks = clk_table;
	cru->clk_data.clk_num = CGU_NR_CLKS;
	platform_set_drvdata(pdev, cru);

	rk628_clk_register_plls(cru);
	rk628_clk_register_muxes(cru);
	rk628_clk_register_gates(cru);
	rk628_clk_register_composites(cru);
	rk628_reset_controller_register(cru);

	clk_prepare_enable(clk_table[CGU_PCLK_LOGIC]);

	return of_clk_add_provider(dev->of_node, of_clk_src_onecell_get,
				   &cru->clk_data);
}

static int rk628_cru_remove(struct platform_device *pdev)
{
	struct rk628_cru *cru = dev_get_drvdata(&pdev->dev);

	of_clk_del_provider(pdev->dev.of_node);
	reset_controller_unregister(&cru->rcdev);

	return 0;
}

static const struct of_device_id rk628_cru_of_match[] = {
	{ .compatible = "rockchip,rk628-cru", },
	{},
};
MODULE_DEVICE_TABLE(of, rk628_cru_of_match);

static struct platform_driver rk628_cru_driver = {
	.driver = {
		.name = "rk628-cru",
		.of_match_table = of_match_ptr(rk628_cru_of_match),
	},
	.probe	= rk628_cru_probe,
	.remove = rk628_cru_remove,
};
module_platform_driver(rk628_cru_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK628 CRU driver");
MODULE_LICENSE("GPL v2");
