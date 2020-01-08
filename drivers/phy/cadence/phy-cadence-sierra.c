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
#define SIERRA_COMMON_CDB_OFFSET			0x0
#define SIERRA_MACRO_ID_REG				0x0
#define SIERRA_CMN_PLLLC_MODE_PREG			0x48
#define SIERRA_CMN_PLLLC_LF_COEFF_MODE1_PREG		0x49
#define SIERRA_CMN_PLLLC_LF_COEFF_MODE0_PREG		0x4A
#define SIERRA_CMN_PLLLC_LOCK_CNTSTART_PREG		0x4B
#define SIERRA_CMN_PLLLC_BWCAL_MODE1_PREG		0x4F
#define SIERRA_CMN_PLLLC_BWCAL_MODE0_PREG		0x50
#define SIERRA_CMN_PLLLC_SS_TIME_STEPSIZE_MODE_PREG	0x62

#define SIERRA_LANE_CDB_OFFSET(ln, block_offset, reg_offset)	\
				((0x4000 << (block_offset)) + \
				 (((ln) << 9) << (reg_offset)))

#define SIERRA_DET_STANDEC_A_PREG			0x000
#define SIERRA_DET_STANDEC_B_PREG			0x001
#define SIERRA_DET_STANDEC_C_PREG			0x002
#define SIERRA_DET_STANDEC_D_PREG			0x003
#define SIERRA_DET_STANDEC_E_PREG			0x004
#define SIERRA_PSM_LANECAL_DLY_A1_RESETS_PREG		0x008
#define SIERRA_PSM_A0IN_TMR_PREG			0x009
#define SIERRA_PSM_DIAG_PREG				0x015
#define SIERRA_PSC_TX_A0_PREG				0x028
#define SIERRA_PSC_TX_A1_PREG				0x029
#define SIERRA_PSC_TX_A2_PREG				0x02A
#define SIERRA_PSC_TX_A3_PREG				0x02B
#define SIERRA_PSC_RX_A0_PREG				0x030
#define SIERRA_PSC_RX_A1_PREG				0x031
#define SIERRA_PSC_RX_A2_PREG				0x032
#define SIERRA_PSC_RX_A3_PREG				0x033
#define SIERRA_PLLCTRL_SUBRATE_PREG			0x03A
#define SIERRA_PLLCTRL_GEN_D_PREG			0x03E
#define SIERRA_PLLCTRL_CPGAIN_MODE_PREG			0x03F
#define SIERRA_PLLCTRL_STATUS_PREG			0x044
#define SIERRA_CLKPATH_BIASTRIM_PREG			0x04B
#define SIERRA_DFE_BIASTRIM_PREG			0x04C
#define SIERRA_DRVCTRL_ATTEN_PREG			0x06A
#define SIERRA_CLKPATHCTRL_TMR_PREG			0x081
#define SIERRA_RX_CREQ_FLTR_A_MODE3_PREG		0x085
#define SIERRA_RX_CREQ_FLTR_A_MODE2_PREG		0x086
#define SIERRA_RX_CREQ_FLTR_A_MODE1_PREG		0x087
#define SIERRA_RX_CREQ_FLTR_A_MODE0_PREG		0x088
#define SIERRA_CREQ_CCLKDET_MODE01_PREG			0x08E
#define SIERRA_RX_CTLE_MAINTENANCE_PREG			0x091
#define SIERRA_CREQ_FSMCLK_SEL_PREG			0x092
#define SIERRA_CREQ_EQ_CTRL_PREG			0x093
#define SIERRA_CREQ_SPARE_PREG				0x096
#define SIERRA_CREQ_EQ_OPEN_EYE_THRESH_PREG		0x097
#define SIERRA_CTLELUT_CTRL_PREG			0x098
#define SIERRA_DFE_ECMP_RATESEL_PREG			0x0C0
#define SIERRA_DFE_SMP_RATESEL_PREG			0x0C1
#define SIERRA_DEQ_PHALIGN_CTRL				0x0C4
#define SIERRA_DEQ_CONCUR_CTRL1_PREG			0x0C8
#define SIERRA_DEQ_CONCUR_CTRL2_PREG			0x0C9
#define SIERRA_DEQ_EPIPWR_CTRL2_PREG			0x0CD
#define SIERRA_DEQ_FAST_MAINT_CYCLES_PREG		0x0CE
#define SIERRA_DEQ_ERRCMP_CTRL_PREG			0x0D0
#define SIERRA_DEQ_OFFSET_CTRL_PREG			0x0D8
#define SIERRA_DEQ_GAIN_CTRL_PREG			0x0E0
#define SIERRA_DEQ_VGATUNE_CTRL_PREG			0x0E1
#define SIERRA_DEQ_GLUT0				0x0E8
#define SIERRA_DEQ_GLUT1				0x0E9
#define SIERRA_DEQ_GLUT2				0x0EA
#define SIERRA_DEQ_GLUT3				0x0EB
#define SIERRA_DEQ_GLUT4				0x0EC
#define SIERRA_DEQ_GLUT5				0x0ED
#define SIERRA_DEQ_GLUT6				0x0EE
#define SIERRA_DEQ_GLUT7				0x0EF
#define SIERRA_DEQ_GLUT8				0x0F0
#define SIERRA_DEQ_GLUT9				0x0F1
#define SIERRA_DEQ_GLUT10				0x0F2
#define SIERRA_DEQ_GLUT11				0x0F3
#define SIERRA_DEQ_GLUT12				0x0F4
#define SIERRA_DEQ_GLUT13				0x0F5
#define SIERRA_DEQ_GLUT14				0x0F6
#define SIERRA_DEQ_GLUT15				0x0F7
#define SIERRA_DEQ_GLUT16				0x0F8
#define SIERRA_DEQ_ALUT0				0x108
#define SIERRA_DEQ_ALUT1				0x109
#define SIERRA_DEQ_ALUT2				0x10A
#define SIERRA_DEQ_ALUT3				0x10B
#define SIERRA_DEQ_ALUT4				0x10C
#define SIERRA_DEQ_ALUT5				0x10D
#define SIERRA_DEQ_ALUT6				0x10E
#define SIERRA_DEQ_ALUT7				0x10F
#define SIERRA_DEQ_ALUT8				0x110
#define SIERRA_DEQ_ALUT9				0x111
#define SIERRA_DEQ_ALUT10				0x112
#define SIERRA_DEQ_ALUT11				0x113
#define SIERRA_DEQ_ALUT12				0x114
#define SIERRA_DEQ_ALUT13				0x115
#define SIERRA_DEQ_DFETAP_CTRL_PREG			0x128
#define SIERRA_DFE_EN_1010_IGNORE_PREG			0x134
#define SIERRA_DEQ_TAU_CTRL1_SLOW_MAINT_PREG		0x150
#define SIERRA_DEQ_TAU_CTRL2_PREG			0x151
#define SIERRA_DEQ_PICTRL_PREG				0x161
#define SIERRA_CPICAL_TMRVAL_MODE1_PREG			0x170
#define SIERRA_CPICAL_TMRVAL_MODE0_PREG			0x171
#define SIERRA_CPICAL_PICNT_MODE1_PREG			0x174
#define SIERRA_CPI_OUTBUF_RATESEL_PREG			0x17C
#define SIERRA_CPICAL_RES_STARTCODE_MODE23_PREG		0x183
#define SIERRA_LFPSDET_SUPPORT_PREG			0x188
#define SIERRA_LFPSFILT_NS_PREG				0x18A
#define SIERRA_LFPSFILT_RD_PREG				0x18B
#define SIERRA_LFPSFILT_MP_PREG				0x18C
#define SIERRA_SIGDET_SUPPORT_PREG			0x190
#define SIERRA_SDFILT_H2L_A_PREG			0x191
#define SIERRA_SDFILT_L2H_PREG				0x193
#define SIERRA_RXBUFFER_CTLECTRL_PREG			0x19E
#define SIERRA_RXBUFFER_RCDFECTRL_PREG			0x19F
#define SIERRA_RXBUFFER_DFECTRL_PREG			0x1A0
#define SIERRA_DEQ_TAU_CTRL1_FAST_MAINT_PREG		0x14F
#define SIERRA_DEQ_TAU_CTRL1_SLOW_MAINT_PREG		0x150

#define SIERRA_PHY_CONFIG_CTRL_OFFSET(block_offset)	\
				      (0xc000 << (block_offset))
#define SIERRA_PHY_PLL_CFG				0xe

#define SIERRA_MACRO_ID					0x00007364
#define SIERRA_MAX_LANES				16
#define PLL_LOCK_TIME					100000

static const struct reg_field macro_id_type =
				REG_FIELD(SIERRA_MACRO_ID_REG, 0, 15);
static const struct reg_field phy_pll_cfg_1 =
				REG_FIELD(SIERRA_PHY_PLL_CFG, 1, 1);
static const struct reg_field pllctrl_lock =
				REG_FIELD(SIERRA_PLLCTRL_STATUS_PREG, 0, 0);

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
		u32 pcie_cmn_regs;
		u32 pcie_ln_regs;
		u32 usb_cmn_regs;
		u32 usb_ln_regs;
		struct cdns_reg_pairs *pcie_cmn_vals;
		struct cdns_reg_pairs *pcie_ln_vals;
		struct cdns_reg_pairs *usb_cmn_vals;
		struct cdns_reg_pairs *usb_ln_vals;
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
	struct regmap_field *pllctrl_lock[SIERRA_MAX_LANES];
	struct clk *clk;
	struct clk *cmn_refclk_dig_div;
	struct clk *cmn_refclk1_dig_div;
	int nsubnodes;
	u32 num_lanes;
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
	SIERRA_LANE_CDB_REGMAP_CONF("4"),
	SIERRA_LANE_CDB_REGMAP_CONF("5"),
	SIERRA_LANE_CDB_REGMAP_CONF("6"),
	SIERRA_LANE_CDB_REGMAP_CONF("7"),
	SIERRA_LANE_CDB_REGMAP_CONF("8"),
	SIERRA_LANE_CDB_REGMAP_CONF("9"),
	SIERRA_LANE_CDB_REGMAP_CONF("10"),
	SIERRA_LANE_CDB_REGMAP_CONF("11"),
	SIERRA_LANE_CDB_REGMAP_CONF("12"),
	SIERRA_LANE_CDB_REGMAP_CONF("13"),
	SIERRA_LANE_CDB_REGMAP_CONF("14"),
	SIERRA_LANE_CDB_REGMAP_CONF("15"),
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

static int cdns_sierra_phy_init(struct phy *gphy)
{
	struct cdns_sierra_inst *ins = phy_get_drvdata(gphy);
	struct cdns_sierra_phy *phy = dev_get_drvdata(gphy->dev.parent);
	struct regmap *regmap;
	int i, j;
	struct cdns_reg_pairs *cmn_vals, *ln_vals;
	u32 num_cmn_regs, num_ln_regs;

	/* Initialise the PHY registers, unless auto configured */
	if (phy->autoconf)
		return 0;

	clk_set_rate(phy->cmn_refclk_dig_div, 25000000);
	clk_set_rate(phy->cmn_refclk1_dig_div, 25000000);
	if (ins->phy_type == PHY_TYPE_PCIE) {
		num_cmn_regs = phy->init_data->pcie_cmn_regs;
		num_ln_regs = phy->init_data->pcie_ln_regs;
		cmn_vals = phy->init_data->pcie_cmn_vals;
		ln_vals = phy->init_data->pcie_ln_vals;
	} else if (ins->phy_type == PHY_TYPE_USB3) {
		num_cmn_regs = phy->init_data->usb_cmn_regs;
		num_ln_regs = phy->init_data->usb_ln_regs;
		cmn_vals = phy->init_data->usb_cmn_vals;
		ln_vals = phy->init_data->usb_ln_vals;
	} else {
		return -EINVAL;
	}

	regmap = phy->regmap_common_cdb;
	for (j = 0; j < num_cmn_regs ; j++)
		regmap_write(regmap, cmn_vals[j].off, cmn_vals[j].val);

	for (i = 0; i < ins->num_lanes; i++) {
		for (j = 0; j < num_ln_regs ; j++) {
			regmap = phy->regmap_lane_cdb[i + ins->mlane];
			regmap_write(regmap, ln_vals[j].off, ln_vals[j].val);
		}
	}

	return 0;
}

static int cdns_sierra_phy_on(struct phy *gphy)
{
	struct cdns_sierra_phy *sp = dev_get_drvdata(gphy->dev.parent);
	struct cdns_sierra_inst *ins = phy_get_drvdata(gphy);
	struct device *dev = sp->dev;
	u32 val;
	int ret;

	/* Take the PHY lane group out of reset */
	ret = reset_control_deassert(ins->lnk_rst);
	if (ret) {
		dev_err(dev, "Failed to take the PHY lane out of reset\n");
		return ret;
	}

	ret = regmap_field_read_poll_timeout(sp->pllctrl_lock[ins->mlane],
					     val, val, 1000, PLL_LOCK_TIME);
	if (ret < 0)
		dev_err(dev, "PLL lock of lane failed\n");

	return ret;
}

static int cdns_sierra_phy_off(struct phy *gphy)
{
	struct cdns_sierra_inst *ins = phy_get_drvdata(gphy);

	return reset_control_assert(ins->lnk_rst);
}

static const struct phy_ops ops = {
	.init		= cdns_sierra_phy_init,
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
	int i;

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

	for (i = 0; i < SIERRA_MAX_LANES; i++) {
		regmap = sp->regmap_lane_cdb[i];
		field = devm_regmap_field_alloc(dev, regmap, pllctrl_lock);
		if (IS_ERR(field)) {
			dev_err(dev, "P%d_ENABLE reg field init failed\n", i);
			return PTR_ERR(field);
		}
		sp->pllctrl_lock[i] =  field;
	}

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
	struct clk *clk;
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

	clk = devm_clk_get_optional(dev, "cmn_refclk_dig_div");
	if (IS_ERR(clk)) {
		dev_err(dev, "cmn_refclk_dig_div clock not found\n");
		ret = PTR_ERR(clk);
		return ret;
	}
	sp->cmn_refclk_dig_div = clk;

	clk = devm_clk_get_optional(dev, "cmn_refclk1_dig_div");
	if (IS_ERR(clk)) {
		dev_err(dev, "cmn_refclk1_dig_div clock not found\n");
		ret = PTR_ERR(clk);
		return ret;
	}
	sp->cmn_refclk1_dig_div = clk;

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
			of_reset_control_array_get_exclusive(child);

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

		sp->num_lanes += sp->phys[node].num_lanes;

		gphy = devm_phy_create(dev, child, &ops);

		if (IS_ERR(gphy)) {
			ret = PTR_ERR(gphy);
			goto put_child;
		}
		sp->phys[node].phy = gphy;
		phy_set_drvdata(gphy, &sp->phys[node]);

		node++;
	}
	sp->nsubnodes = node;

	if (sp->num_lanes > SIERRA_MAX_LANES) {
		dev_err(dev, "Invalid lane configuration\n");
		goto put_child2;
	}

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
	struct cdns_sierra_phy *phy = platform_get_drvdata(pdev);
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

/* refclk100MHz_32b_PCIe_cmn_pll_ext_ssc */
static struct cdns_reg_pairs cdns_pcie_cmn_regs_ext_ssc[] = {
	{0x2106, SIERRA_CMN_PLLLC_LF_COEFF_MODE1_PREG},
	{0x2106, SIERRA_CMN_PLLLC_LF_COEFF_MODE0_PREG},
	{0x8A06, SIERRA_CMN_PLLLC_BWCAL_MODE1_PREG},
	{0x8A06, SIERRA_CMN_PLLLC_BWCAL_MODE0_PREG},
	{0x1B1B, SIERRA_CMN_PLLLC_SS_TIME_STEPSIZE_MODE_PREG}
};

/* refclk100MHz_32b_PCIe_ln_ext_ssc */
static struct cdns_reg_pairs cdns_pcie_ln_regs_ext_ssc[] = {
	{0x813E, SIERRA_CLKPATHCTRL_TMR_PREG},
	{0x8047, SIERRA_RX_CREQ_FLTR_A_MODE3_PREG},
	{0x808F, SIERRA_RX_CREQ_FLTR_A_MODE2_PREG},
	{0x808F, SIERRA_RX_CREQ_FLTR_A_MODE1_PREG},
	{0x808F, SIERRA_RX_CREQ_FLTR_A_MODE0_PREG},
	{0x033C, SIERRA_RX_CTLE_MAINTENANCE_PREG},
	{0x44CC, SIERRA_CREQ_EQ_OPEN_EYE_THRESH_PREG}
};

/* refclk100MHz_20b_USB_cmn_pll_ext_ssc */
static struct cdns_reg_pairs cdns_usb_cmn_regs_ext_ssc[] = {
	{0x2085, SIERRA_CMN_PLLLC_LF_COEFF_MODE1_PREG},
	{0x2085, SIERRA_CMN_PLLLC_LF_COEFF_MODE0_PREG},
	{0x0000, SIERRA_CMN_PLLLC_BWCAL_MODE0_PREG},
	{0x0000, SIERRA_CMN_PLLLC_SS_TIME_STEPSIZE_MODE_PREG}
};

/* refclk100MHz_20b_USB_ln_ext_ssc */
static struct cdns_reg_pairs cdns_usb_ln_regs_ext_ssc[] = {
	{0xFE0A, SIERRA_DET_STANDEC_A_PREG},
	{0x000F, SIERRA_DET_STANDEC_B_PREG},
	{0x00A5, SIERRA_DET_STANDEC_C_PREG},
	{0x69ad, SIERRA_DET_STANDEC_D_PREG},
	{0x0241, SIERRA_DET_STANDEC_E_PREG},
	{0x0010, SIERRA_PSM_LANECAL_DLY_A1_RESETS_PREG},
	{0x0014, SIERRA_PSM_A0IN_TMR_PREG},
	{0xCF00, SIERRA_PSM_DIAG_PREG},
	{0x001F, SIERRA_PSC_TX_A0_PREG},
	{0x0007, SIERRA_PSC_TX_A1_PREG},
	{0x0003, SIERRA_PSC_TX_A2_PREG},
	{0x0003, SIERRA_PSC_TX_A3_PREG},
	{0x0FFF, SIERRA_PSC_RX_A0_PREG},
	{0x0619, SIERRA_PSC_RX_A1_PREG},
	{0x0003, SIERRA_PSC_RX_A2_PREG},
	{0x0001, SIERRA_PSC_RX_A3_PREG},
	{0x0001, SIERRA_PLLCTRL_SUBRATE_PREG},
	{0x0406, SIERRA_PLLCTRL_GEN_D_PREG},
	{0x5233, SIERRA_PLLCTRL_CPGAIN_MODE_PREG},
	{0x00CA, SIERRA_CLKPATH_BIASTRIM_PREG},
	{0x2512, SIERRA_DFE_BIASTRIM_PREG},
	{0x0000, SIERRA_DRVCTRL_ATTEN_PREG},
	{0x873E, SIERRA_CLKPATHCTRL_TMR_PREG},
	{0x03CF, SIERRA_RX_CREQ_FLTR_A_MODE1_PREG},
	{0x01CE, SIERRA_RX_CREQ_FLTR_A_MODE0_PREG},
	{0x7B3C, SIERRA_CREQ_CCLKDET_MODE01_PREG},
	{0x033F, SIERRA_RX_CTLE_MAINTENANCE_PREG},
	{0x3232, SIERRA_CREQ_FSMCLK_SEL_PREG},
	{0x0000, SIERRA_CREQ_EQ_CTRL_PREG},
	{0x8000, SIERRA_CREQ_SPARE_PREG},
	{0xCC44, SIERRA_CREQ_EQ_OPEN_EYE_THRESH_PREG},
	{0x8453, SIERRA_CTLELUT_CTRL_PREG},
	{0x4110, SIERRA_DFE_ECMP_RATESEL_PREG},
	{0x4110, SIERRA_DFE_SMP_RATESEL_PREG},
	{0x0002, SIERRA_DEQ_PHALIGN_CTRL},
	{0x3200, SIERRA_DEQ_CONCUR_CTRL1_PREG},
	{0x5064, SIERRA_DEQ_CONCUR_CTRL2_PREG},
	{0x0030, SIERRA_DEQ_EPIPWR_CTRL2_PREG},
	{0x0048, SIERRA_DEQ_FAST_MAINT_CYCLES_PREG},
	{0x5A5A, SIERRA_DEQ_ERRCMP_CTRL_PREG},
	{0x02F5, SIERRA_DEQ_OFFSET_CTRL_PREG},
	{0x02F5, SIERRA_DEQ_GAIN_CTRL_PREG},
	{0x9A8A, SIERRA_DEQ_VGATUNE_CTRL_PREG},
	{0x0014, SIERRA_DEQ_GLUT0},
	{0x0014, SIERRA_DEQ_GLUT1},
	{0x0014, SIERRA_DEQ_GLUT2},
	{0x0014, SIERRA_DEQ_GLUT3},
	{0x0014, SIERRA_DEQ_GLUT4},
	{0x0014, SIERRA_DEQ_GLUT5},
	{0x0014, SIERRA_DEQ_GLUT6},
	{0x0014, SIERRA_DEQ_GLUT7},
	{0x0014, SIERRA_DEQ_GLUT8},
	{0x0014, SIERRA_DEQ_GLUT9},
	{0x0014, SIERRA_DEQ_GLUT10},
	{0x0014, SIERRA_DEQ_GLUT11},
	{0x0014, SIERRA_DEQ_GLUT12},
	{0x0014, SIERRA_DEQ_GLUT13},
	{0x0014, SIERRA_DEQ_GLUT14},
	{0x0014, SIERRA_DEQ_GLUT15},
	{0x0014, SIERRA_DEQ_GLUT16},
	{0x0BAE, SIERRA_DEQ_ALUT0},
	{0x0AEB, SIERRA_DEQ_ALUT1},
	{0x0A28, SIERRA_DEQ_ALUT2},
	{0x0965, SIERRA_DEQ_ALUT3},
	{0x08A2, SIERRA_DEQ_ALUT4},
	{0x07DF, SIERRA_DEQ_ALUT5},
	{0x071C, SIERRA_DEQ_ALUT6},
	{0x0659, SIERRA_DEQ_ALUT7},
	{0x0596, SIERRA_DEQ_ALUT8},
	{0x0514, SIERRA_DEQ_ALUT9},
	{0x0492, SIERRA_DEQ_ALUT10},
	{0x0410, SIERRA_DEQ_ALUT11},
	{0x038E, SIERRA_DEQ_ALUT12},
	{0x030C, SIERRA_DEQ_ALUT13},
	{0x03F4, SIERRA_DEQ_DFETAP_CTRL_PREG},
	{0x0001, SIERRA_DFE_EN_1010_IGNORE_PREG},
	{0x3C01, SIERRA_DEQ_TAU_CTRL1_FAST_MAINT_PREG},
	{0x3C40, SIERRA_DEQ_TAU_CTRL1_SLOW_MAINT_PREG},
	{0x1C08, SIERRA_DEQ_TAU_CTRL2_PREG},
	{0x0033, SIERRA_DEQ_PICTRL_PREG},
	{0x0400, SIERRA_CPICAL_TMRVAL_MODE1_PREG},
	{0x0330, SIERRA_CPICAL_TMRVAL_MODE0_PREG},
	{0x01FF, SIERRA_CPICAL_PICNT_MODE1_PREG},
	{0x0009, SIERRA_CPI_OUTBUF_RATESEL_PREG},
	{0x3232, SIERRA_CPICAL_RES_STARTCODE_MODE23_PREG},
	{0x0005, SIERRA_LFPSDET_SUPPORT_PREG},
	{0x000F, SIERRA_LFPSFILT_NS_PREG},
	{0x0009, SIERRA_LFPSFILT_RD_PREG},
	{0x0001, SIERRA_LFPSFILT_MP_PREG},
	{0x8013, SIERRA_SDFILT_H2L_A_PREG},
	{0x8009, SIERRA_SDFILT_L2H_PREG},
	{0x0024, SIERRA_RXBUFFER_CTLECTRL_PREG},
	{0x0020, SIERRA_RXBUFFER_RCDFECTRL_PREG},
	{0x4243, SIERRA_RXBUFFER_DFECTRL_PREG}
};

static const struct cdns_sierra_data cdns_map_sierra = {
	SIERRA_MACRO_ID,
	0x2,
	0x2,
	ARRAY_SIZE(cdns_pcie_cmn_regs_ext_ssc),
	ARRAY_SIZE(cdns_pcie_ln_regs_ext_ssc),
	ARRAY_SIZE(cdns_usb_cmn_regs_ext_ssc),
	ARRAY_SIZE(cdns_usb_ln_regs_ext_ssc),
	cdns_pcie_cmn_regs_ext_ssc,
	cdns_pcie_ln_regs_ext_ssc,
	cdns_usb_cmn_regs_ext_ssc,
	cdns_usb_ln_regs_ext_ssc,
};

static const struct cdns_sierra_data cdns_ti_map_sierra = {
	SIERRA_MACRO_ID,
	0x0,
	0x1,
	ARRAY_SIZE(cdns_pcie_cmn_regs_ext_ssc),
	ARRAY_SIZE(cdns_pcie_ln_regs_ext_ssc),
	ARRAY_SIZE(cdns_usb_cmn_regs_ext_ssc),
	ARRAY_SIZE(cdns_usb_ln_regs_ext_ssc),
	cdns_pcie_cmn_regs_ext_ssc,
	cdns_pcie_ln_regs_ext_ssc,
	cdns_usb_cmn_regs_ext_ssc,
	cdns_usb_ln_regs_ext_ssc,
};

static const struct of_device_id cdns_sierra_id_table[] = {
	{
		.compatible = "cdns,sierra-phy-t0",
		.data = &cdns_map_sierra,
	},
	{
		.compatible = "ti,sierra-phy-t0",
		.data = &cdns_ti_map_sierra,
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
