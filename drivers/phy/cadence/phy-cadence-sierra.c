// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence Sierra PHY Driver
 *
 * Copyright (c) 2018 Cadence Design Systems
 * Author: Alan Douglas <adouglas@cadence.com>
 *
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <dt-bindings/phy/phy.h>

/* PHY register offsets */
#define SIERRA_COMMON_CDB_OFFSET	0x0
#define SIERRA_MACRO_ID_REG		0x0

#define SIERRA_LANE_CDB_OFFSET(ln, block_offset, reg_offset)	\
				((0x4000 << (block_offset)) + \
				 (((ln) << 9) << (reg_offset)))
#define SIERRA_DET_STANDEC_A		0x000
#define SIERRA_DET_STANDEC_B		0x001
#define SIERRA_DET_STANDEC_C		0x002
#define SIERRA_DET_STANDEC_D		0x003
#define SIERRA_DET_STANDEC_E		0x004
#define SIERRA_PSM_LANECAL		0x008
#define SIERRA_PSM_DIAG			0x015
#define SIERRA_PSC_TX_A0		0x028
#define SIERRA_PSC_TX_A1		0x029
#define SIERRA_PSC_TX_A2		0x02A
#define SIERRA_PSC_TX_A3		0x02B
#define SIERRA_PSC_RX_A0		0x030
#define SIERRA_PSC_RX_A1		0x031
#define SIERRA_PSC_RX_A2		0x032
#define SIERRA_PSC_RX_A3		0x033
#define SIERRA_PLLCTRL_SUBRATE		0x03A
#define SIERRA_PLLCTRL_GEN_D		0x03E
#define SIERRA_DRVCTRL_ATTEN		0x06A
#define SIERRA_CLKPATHCTRL_TMR		0x081
#define SIERRA_RX_CREQ_FLTR_A_MODE1	0x087
#define SIERRA_RX_CREQ_FLTR_A_MODE0	0x088
#define SIERRA_CREQ_CCLKDET_MODE01	0x08E
#define SIERRA_RX_CTLE_MAINTENANCE	0x091
#define SIERRA_CREQ_FSMCLK_SEL		0x092
#define SIERRA_CTLELUT_CTRL		0x098
#define SIERRA_DFE_ECMP_RATESEL		0x0C0
#define SIERRA_DFE_SMP_RATESEL		0x0C1
#define SIERRA_DEQ_VGATUNE_CTRL		0x0E1
#define SIERRA_TMRVAL_MODE3		0x16E
#define SIERRA_TMRVAL_MODE2		0x16F
#define SIERRA_TMRVAL_MODE1		0x170
#define SIERRA_TMRVAL_MODE0		0x171
#define SIERRA_PICNT_MODE1		0x174
#define SIERRA_CPI_OUTBUF_RATESEL	0x17C
#define SIERRA_LFPSFILT_NS		0x18A
#define SIERRA_LFPSFILT_RD		0x18B
#define SIERRA_LFPSFILT_MP		0x18C
#define SIERRA_SDFILT_H2L_A		0x191

#define SIERRA_PHY_CONFIG_CTRL_OFFSET(block_offset)	\
				      (0xc000 << (block_offset))
#define SIERRA_PHY_PLL_CFG		0xe

#define SIERRA_MACRO_ID			0x00007364
#define SIERRA_MAX_LANES		4

static const struct reg_field macro_id_type =
				REG_FIELD(SIERRA_MACRO_ID_REG, 0, 15);
static const struct reg_field phy_pll_cfg_1 =
				REG_FIELD(SIERRA_PHY_PLL_CFG, 1, 1);

struct cdns_sierra_inst {
	struct phy *phy;
	u32 phy_type;
	u32 num_lanes;
	u32 mlane;
	struct reset_control *lnk_rst;
};

struct cdns_reg_pairs {
	u16 val;
	u32 off;
};

struct cdns_sierra_data {
		u32 id_value;
		u8 block_offset_shift;
		u8 reg_offset_shift;
		u32 pcie_regs;
		u32 usb_regs;
		struct cdns_reg_pairs *pcie_vals;
		struct cdns_reg_pairs  *usb_vals;
};

struct cdns_regmap_cdb_context {
	struct device *dev;
	void __iomem *base;
	u8 reg_offset_shift;
};

struct cdns_sierra_phy {
	struct device *dev;
	struct regmap *regmap;
	struct cdns_sierra_data *init_data;
	struct cdns_sierra_inst phys[SIERRA_MAX_LANES];
	struct reset_control *phy_rst;
	struct reset_control *apb_rst;
	struct regmap *regmap_lane_cdb[SIERRA_MAX_LANES];
	struct regmap *regmap_phy_config_ctrl;
	struct regmap *regmap_common_cdb;
	struct regmap_field *macro_id_type;
	struct regmap_field *phy_pll_cfg_1;
	struct clk *clk;
	int nsubnodes;
	bool autoconf;
};

static int cdns_regmap_write(void *context, unsigned int reg, unsigned int val)
{
	struct cdns_regmap_cdb_context *ctx = context;
	u32 offset = reg << ctx->reg_offset_shift;

	writew(val, ctx->base + offset);

	return 0;
}

static int cdns_regmap_read(void *context, unsigned int reg, unsigned int *val)
{
	struct cdns_regmap_cdb_context *ctx = context;
	u32 offset = reg << ctx->reg_offset_shift;

	*val = readw(ctx->base + offset);
	return 0;
}

#define SIERRA_LANE_CDB_REGMAP_CONF(n) \
{ \
	.name = "sierra_lane" n "_cdb", \
	.reg_stride = 1, \
	.fast_io = true, \
	.reg_write = cdns_regmap_write, \
	.reg_read = cdns_regmap_read, \
}

static struct regmap_config cdns_sierra_lane_cdb_config[] = {
	SIERRA_LANE_CDB_REGMAP_CONF("0"),
	SIERRA_LANE_CDB_REGMAP_CONF("1"),
	SIERRA_LANE_CDB_REGMAP_CONF("2"),
	SIERRA_LANE_CDB_REGMAP_CONF("3"),
};

static struct regmap_config cdns_sierra_common_cdb_config = {
	.name = "sierra_common_cdb",
	.reg_stride = 1,
	.fast_io = true,
	.reg_write = cdns_regmap_write,
	.reg_read = cdns_regmap_read,
};

static struct regmap_config cdns_sierra_phy_config_ctrl_config = {
	.name = "sierra_phy_config_ctrl",
	.reg_stride = 1,
	.fast_io = true,
	.reg_write = cdns_regmap_write,
	.reg_read = cdns_regmap_read,
};

static void cdns_sierra_phy_init(struct phy *gphy)
{
	struct cdns_sierra_inst *ins = phy_get_drvdata(gphy);
	struct cdns_sierra_phy *phy = dev_get_drvdata(gphy->dev.parent);
	struct regmap *regmap = phy->regmap;
	int i, j;
	struct cdns_reg_pairs *vals;
	u32 num_regs;

	if (ins->phy_type == PHY_TYPE_PCIE) {
		num_regs = phy->init_data->pcie_regs;
		vals = phy->init_data->pcie_vals;
	} else if (ins->phy_type == PHY_TYPE_USB3) {
		num_regs = phy->init_data->usb_regs;
		vals = phy->init_data->usb_vals;
	} else {
		return;
	}
	for (i = 0; i < ins->num_lanes; i++) {
		for (j = 0; j < num_regs ; j++) {
			regmap = phy->regmap_lane_cdb[i + ins->mlane];
			regmap_write(regmap, vals[j].off, vals[j].val);
		}
	}
}

static int cdns_sierra_phy_on(struct phy *gphy)
{
	struct cdns_sierra_inst *ins = phy_get_drvdata(gphy);

	/* Take the PHY lane group out of reset */
	return reset_control_deassert(ins->lnk_rst);
}

static int cdns_sierra_phy_off(struct phy *gphy)
{
	struct cdns_sierra_inst *ins = phy_get_drvdata(gphy);

	return reset_control_assert(ins->lnk_rst);
}

static const struct phy_ops ops = {
	.power_on	= cdns_sierra_phy_on,
	.power_off	= cdns_sierra_phy_off,
	.owner		= THIS_MODULE,
};

static int cdns_sierra_get_optional(struct cdns_sierra_inst *inst,
				    struct device_node *child)
{
	if (of_property_read_u32(child, "reg", &inst->mlane))
		return -EINVAL;

	if (of_property_read_u32(child, "cdns,num-lanes", &inst->num_lanes))
		return -EINVAL;

	if (of_property_read_u32(child, "cdns,phy-type", &inst->phy_type))
		return -EINVAL;

	return 0;
}

static const struct of_device_id cdns_sierra_id_table[];

static struct regmap *cdns_regmap_init(struct device *dev, void __iomem *base,
				       u32 block_offset, u8 reg_offset_shift,
				       const struct regmap_config *config)
{
	struct cdns_regmap_cdb_context *ctx;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->dev = dev;
	ctx->base = base + block_offset;
	ctx->reg_offset_shift = reg_offset_shift;

	return devm_regmap_init(dev, NULL, ctx, config);
}

static int cdns_regfield_init(struct cdns_sierra_phy *sp)
{
	struct device *dev = sp->dev;
	struct regmap_field *field;
	struct regmap *regmap;

	regmap = sp->regmap_common_cdb;
	field = devm_regmap_field_alloc(dev, regmap, macro_id_type);
	if (IS_ERR(field)) {
		dev_err(dev, "MACRO_ID_TYPE reg field init failed\n");
		return PTR_ERR(field);
	}
	sp->macro_id_type = field;

	regmap = sp->regmap_phy_config_ctrl;
	field = devm_regmap_field_alloc(dev, regmap, phy_pll_cfg_1);
	if (IS_ERR(field)) {
		dev_err(dev, "PHY_PLL_CFG_1 reg field init failed\n");
		return PTR_ERR(field);
	}
	sp->phy_pll_cfg_1 = field;

	return 0;
}

static int cdns_regmap_init_blocks(struct cdns_sierra_phy *sp,
				   void __iomem *base, u8 block_offset_shift,
				   u8 reg_offset_shift)
{
	struct device *dev = sp->dev;
	struct regmap *regmap;
	u32 block_offset;
	int i;

	for (i = 0; i < SIERRA_MAX_LANES; i++) {
		block_offset = SIERRA_LANE_CDB_OFFSET(i, block_offset_shift,
						      reg_offset_shift);
		regmap = cdns_regmap_init(dev, base, block_offset,
					  reg_offset_shift,
					  &cdns_sierra_lane_cdb_config[i]);
		if (IS_ERR(regmap)) {
			dev_err(dev, "Failed to init lane CDB regmap\n");
			return PTR_ERR(regmap);
		}
		sp->regmap_lane_cdb[i] = regmap;
	}

	regmap = cdns_regmap_init(dev, base, SIERRA_COMMON_CDB_OFFSET,
				  reg_offset_shift,
				  &cdns_sierra_common_cdb_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to init common CDB regmap\n");
		return PTR_ERR(regmap);
	}
	sp->regmap_common_cdb = regmap;

	block_offset = SIERRA_PHY_CONFIG_CTRL_OFFSET(block_offset_shift);
	regmap = cdns_regmap_init(dev, base, block_offset, reg_offset_shift,
				  &cdns_sierra_phy_config_ctrl_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to init PHY config and control regmap\n");
		return PTR_ERR(regmap);
	}
	sp->regmap_phy_config_ctrl = regmap;

	return 0;
}

static int cdns_sierra_phy_probe(struct platform_device *pdev)
{
	struct cdns_sierra_phy *sp;
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct cdns_sierra_data *data;
	unsigned int id_value;
	struct resource *res;
	int i, ret, node = 0;
	void __iomem *base;
	struct device_node *dn = dev->of_node, *child;

	if (of_get_child_count(dn) == 0)
		return -ENODEV;

	/* Get init data for this PHY */
	match = of_match_device(cdns_sierra_id_table, dev);
	if (!match)
		return -EINVAL;

	data = (struct cdns_sierra_data *)match->data;

	sp = devm_kzalloc(dev, sizeof(*sp), GFP_KERNEL);
	if (!sp)
		return -ENOMEM;
	dev_set_drvdata(dev, sp);
	sp->dev = dev;
	sp->init_data = data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base)) {
		dev_err(dev, "missing \"reg\"\n");
		return PTR_ERR(base);
	}

	ret = cdns_regmap_init_blocks(sp, base, data->block_offset_shift,
				      data->reg_offset_shift);
	if (ret)
		return ret;

	ret = cdns_regfield_init(sp);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, sp);

	sp->clk = devm_clk_get_optional(dev, "phy_clk");
	if (IS_ERR(sp->clk)) {
		dev_err(dev, "failed to get clock phy_clk\n");
		return PTR_ERR(sp->clk);
	}

	sp->phy_rst = devm_reset_control_get(dev, "sierra_reset");
	if (IS_ERR(sp->phy_rst)) {
		dev_err(dev, "failed to get reset\n");
		return PTR_ERR(sp->phy_rst);
	}

	sp->apb_rst = devm_reset_control_get_optional(dev, "sierra_apb");
	if (IS_ERR(sp->apb_rst)) {
		dev_err(dev, "failed to get apb reset\n");
		return PTR_ERR(sp->apb_rst);
	}

	ret = clk_prepare_enable(sp->clk);
	if (ret)
		return ret;

	/* Enable APB */
	reset_control_deassert(sp->apb_rst);

	/* Check that PHY is present */
	regmap_field_read(sp->macro_id_type, &id_value);
	if  (sp->init_data->id_value != id_value) {
		ret = -EINVAL;
		goto clk_disable;
	}

	sp->autoconf = of_property_read_bool(dn, "cdns,autoconf");

	for_each_available_child_of_node(dn, child) {
		struct phy *gphy;

		sp->phys[node].lnk_rst =
			of_reset_control_get_exclusive_by_index(child, 0);

		if (IS_ERR(sp->phys[node].lnk_rst)) {
			dev_err(dev, "failed to get reset %s\n",
				child->full_name);
			ret = PTR_ERR(sp->phys[node].lnk_rst);
			goto put_child2;
		}

		if (!sp->autoconf) {
			ret = cdns_sierra_get_optional(&sp->phys[node], child);
			if (ret) {
				dev_err(dev, "missing property in node %s\n",
					child->name);
				goto put_child;
			}
		}

		gphy = devm_phy_create(dev, child, &ops);

		if (IS_ERR(gphy)) {
			ret = PTR_ERR(gphy);
			goto put_child;
		}
		sp->phys[node].phy = gphy;
		phy_set_drvdata(gphy, &sp->phys[node]);

		/* Initialise the PHY registers, unless auto configured */
		if (!sp->autoconf)
			cdns_sierra_phy_init(gphy);

		node++;
	}
	sp->nsubnodes = node;

	/* If more than one subnode, configure the PHY as multilink */
	if (!sp->autoconf && sp->nsubnodes > 1)
		regmap_field_write(sp->phy_pll_cfg_1, 0x1);

	pm_runtime_enable(dev);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	reset_control_deassert(sp->phy_rst);
	return PTR_ERR_OR_ZERO(phy_provider);

put_child:
	node++;
put_child2:
	for (i = 0; i < node; i++)
		reset_control_put(sp->phys[i].lnk_rst);
	of_node_put(child);
clk_disable:
	clk_disable_unprepare(sp->clk);
	reset_control_assert(sp->apb_rst);
	return ret;
}

static int cdns_sierra_phy_remove(struct platform_device *pdev)
{
	struct cdns_sierra_phy *phy = dev_get_drvdata(pdev->dev.parent);
	int i;

	reset_control_assert(phy->phy_rst);
	reset_control_assert(phy->apb_rst);
	pm_runtime_disable(&pdev->dev);

	/*
	 * The device level resets will be put automatically.
	 * Need to put the subnode resets here though.
	 */
	for (i = 0; i < phy->nsubnodes; i++) {
		reset_control_assert(phy->phys[i].lnk_rst);
		reset_control_put(phy->phys[i].lnk_rst);
	}
	return 0;
}

static struct cdns_reg_pairs cdns_usb_regs[] = {
	/*
	 * Write USB configuration parameters to the PHY.
	 * These values are specific to this specific hardware
	 * configuration.
	 */
	{0xFE0A, SIERRA_DET_STANDEC_A},
	{0x000F, SIERRA_DET_STANDEC_B},
	{0x55A5, SIERRA_DET_STANDEC_C},
	{0x69AD, SIERRA_DET_STANDEC_D},
	{0x0241, SIERRA_DET_STANDEC_E},
	{0x0110, SIERRA_PSM_LANECAL},
	{0xCF00, SIERRA_PSM_DIAG},
	{0x001F, SIERRA_PSC_TX_A0},
	{0x0007, SIERRA_PSC_TX_A1},
	{0x0003, SIERRA_PSC_TX_A2},
	{0x0003, SIERRA_PSC_TX_A3},
	{0x0FFF, SIERRA_PSC_RX_A0},
	{0x0003, SIERRA_PSC_RX_A1},
	{0x0003, SIERRA_PSC_RX_A2},
	{0x0001, SIERRA_PSC_RX_A3},
	{0x0001, SIERRA_PLLCTRL_SUBRATE},
	{0x0406, SIERRA_PLLCTRL_GEN_D},
	{0x0000, SIERRA_DRVCTRL_ATTEN},
	{0x823E, SIERRA_CLKPATHCTRL_TMR},
	{0x078F, SIERRA_RX_CREQ_FLTR_A_MODE1},
	{0x078F, SIERRA_RX_CREQ_FLTR_A_MODE0},
	{0x7B3C, SIERRA_CREQ_CCLKDET_MODE01},
	{0x023C, SIERRA_RX_CTLE_MAINTENANCE},
	{0x3232, SIERRA_CREQ_FSMCLK_SEL},
	{0x8452, SIERRA_CTLELUT_CTRL},
	{0x4121, SIERRA_DFE_ECMP_RATESEL},
	{0x4121, SIERRA_DFE_SMP_RATESEL},
	{0x9999, SIERRA_DEQ_VGATUNE_CTRL},
	{0x0330, SIERRA_TMRVAL_MODE0},
	{0x01FF, SIERRA_PICNT_MODE1},
	{0x0009, SIERRA_CPI_OUTBUF_RATESEL},
	{0x000F, SIERRA_LFPSFILT_NS},
	{0x0009, SIERRA_LFPSFILT_RD},
	{0x0001, SIERRA_LFPSFILT_MP},
	{0x8013, SIERRA_SDFILT_H2L_A},
	{0x0400, SIERRA_TMRVAL_MODE1},
};

static struct cdns_reg_pairs cdns_pcie_regs[] = {
	/*
	 * Write PCIe configuration parameters to the PHY.
	 * These values are specific to this specific hardware
	 * configuration.
	 */
	{0x891f, SIERRA_DET_STANDEC_D},
	{0x0053, SIERRA_DET_STANDEC_E},
	{0x0400, SIERRA_TMRVAL_MODE2},
	{0x0200, SIERRA_TMRVAL_MODE3},
};

static const struct cdns_sierra_data cdns_map_sierra = {
	SIERRA_MACRO_ID,
	0x2,
	0x2,
	ARRAY_SIZE(cdns_pcie_regs),
	ARRAY_SIZE(cdns_usb_regs),
	cdns_pcie_regs,
	cdns_usb_regs
};

static const struct of_device_id cdns_sierra_id_table[] = {
	{
		.compatible = "cdns,sierra-phy-t0",
		.data = &cdns_map_sierra,
	},
	{}
};
MODULE_DEVICE_TABLE(of, cdns_sierra_id_table);

static struct platform_driver cdns_sierra_driver = {
	.probe		= cdns_sierra_phy_probe,
	.remove		= cdns_sierra_phy_remove,
	.driver		= {
		.name	= "cdns-sierra-phy",
		.of_match_table = cdns_sierra_id_table,
	},
};
module_platform_driver(cdns_sierra_driver);

MODULE_ALIAS("platform:cdns_sierra");
MODULE_AUTHOR("Cadence Design Systems");
MODULE_DESCRIPTION("CDNS sierra phy driver");
MODULE_LICENSE("GPL v2");
