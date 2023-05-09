// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Code Construct
 *
 * Author: Jeremy Kerr <jk@codeconstruct.com.au>
 */

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "dw-i3c-master.h"

/* AST2600-specific global register set */
#define AST2600_I3CG_REG0(idx)	(((idx) * 4 * 4) + 0x10)
#define AST2600_I3CG_REG1(idx)	(((idx) * 4 * 4) + 0x14)

#define AST2600_I3CG_REG0_SDA_PULLUP_EN_MASK	GENMASK(29, 28)
#define AST2600_I3CG_REG0_SDA_PULLUP_EN_2K	(0x0 << 28)
#define AST2600_I3CG_REG0_SDA_PULLUP_EN_750	(0x2 << 28)
#define AST2600_I3CG_REG0_SDA_PULLUP_EN_545	(0x3 << 28)

#define AST2600_I3CG_REG1_I2C_MODE		BIT(0)
#define AST2600_I3CG_REG1_TEST_MODE		BIT(1)
#define AST2600_I3CG_REG1_ACT_MODE_MASK		GENMASK(3, 2)
#define AST2600_I3CG_REG1_ACT_MODE(x)		(((x) << 2) & AST2600_I3CG_REG1_ACT_MODE_MASK)
#define AST2600_I3CG_REG1_PENDING_INT_MASK	GENMASK(7, 4)
#define AST2600_I3CG_REG1_PENDING_INT(x)	(((x) << 4) & AST2600_I3CG_REG1_PENDING_INT_MASK)
#define AST2600_I3CG_REG1_SA_MASK		GENMASK(14, 8)
#define AST2600_I3CG_REG1_SA(x)			(((x) << 8) & AST2600_I3CG_REG1_SA_MASK)
#define AST2600_I3CG_REG1_SA_EN			BIT(15)
#define AST2600_I3CG_REG1_INST_ID_MASK		GENMASK(19, 16)
#define AST2600_I3CG_REG1_INST_ID(x)		(((x) << 16) & AST2600_I3CG_REG1_INST_ID_MASK)

#define AST2600_DEFAULT_SDA_PULLUP_OHMS		2000

#define DEV_ADDR_TABLE_IBI_PEC			BIT(11)

struct ast2600_i3c {
	struct dw_i3c_master dw;
	struct regmap *global_regs;
	unsigned int global_idx;
	unsigned int sda_pullup;
};

static struct ast2600_i3c *to_ast2600_i3c(struct dw_i3c_master *dw)
{
	return container_of(dw, struct ast2600_i3c, dw);
}

static int ast2600_i3c_pullup_to_reg(unsigned int ohms, u32 *regp)
{
	u32 reg;

	switch (ohms) {
	case 2000:
		reg = AST2600_I3CG_REG0_SDA_PULLUP_EN_2K;
		break;
	case 750:
		reg = AST2600_I3CG_REG0_SDA_PULLUP_EN_750;
		break;
	case 545:
		reg = AST2600_I3CG_REG0_SDA_PULLUP_EN_545;
		break;
	default:
		return -EINVAL;
	}

	if (regp)
		*regp = reg;

	return 0;
}

static int ast2600_i3c_init(struct dw_i3c_master *dw)
{
	struct ast2600_i3c *i3c = to_ast2600_i3c(dw);
	u32 reg = 0;
	int rc;

	/* reg0: set SDA pullup values */
	rc = ast2600_i3c_pullup_to_reg(i3c->sda_pullup, &reg);
	if (rc)
		return rc;

	rc = regmap_write(i3c->global_regs,
			  AST2600_I3CG_REG0(i3c->global_idx), reg);
	if (rc)
		return rc;

	/* reg1: set up the instance id, but leave everything else disabled,
	 * as it's all for client mode
	 */
	reg = AST2600_I3CG_REG1_INST_ID(i3c->global_idx);
	rc = regmap_write(i3c->global_regs,
			  AST2600_I3CG_REG1(i3c->global_idx), reg);

	return rc;
}

static void ast2600_i3c_set_dat_ibi(struct dw_i3c_master *i3c,
				    struct i3c_dev_desc *dev,
				    bool enable, u32 *dat)
{
	/*
	 * The ast2600 i3c controller will lock up on receiving 4n+1-byte IBIs
	 * if the PEC is disabled. We have no way to restrict the length of
	 * IBIs sent to the controller, so we need to unconditionally enable
	 * PEC checking, which means we drop a byte of payload data
	 */
	if (enable && dev->info.bcr & I3C_BCR_IBI_PAYLOAD) {
		dev_warn_once(&i3c->base.dev,
		      "Enabling PEC workaround. IBI payloads will be truncated\n");
		*dat |= DEV_ADDR_TABLE_IBI_PEC;
	}
}

static const struct dw_i3c_platform_ops ast2600_i3c_ops = {
	.init = ast2600_i3c_init,
	.set_dat_ibi = ast2600_i3c_set_dat_ibi,
};

static int ast2600_i3c_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct of_phandle_args gspec;
	struct ast2600_i3c *i3c;
	int rc;

	i3c = devm_kzalloc(&pdev->dev, sizeof(*i3c), GFP_KERNEL);
	if (!i3c)
		return -ENOMEM;

	rc = of_parse_phandle_with_fixed_args(np, "aspeed,global-regs", 1, 0,
					      &gspec);
	if (rc)
		return -ENODEV;

	i3c->global_regs = syscon_node_to_regmap(gspec.np);
	of_node_put(gspec.np);

	if (IS_ERR(i3c->global_regs))
		return PTR_ERR(i3c->global_regs);

	i3c->global_idx = gspec.args[0];

	rc = of_property_read_u32(np, "sda-pullup-ohms", &i3c->sda_pullup);
	if (rc)
		i3c->sda_pullup = AST2600_DEFAULT_SDA_PULLUP_OHMS;

	rc = ast2600_i3c_pullup_to_reg(i3c->sda_pullup, NULL);
	if (rc)
		dev_err(&pdev->dev, "invalid sda-pullup value %d\n",
			i3c->sda_pullup);

	i3c->dw.platform_ops = &ast2600_i3c_ops;
	i3c->dw.ibi_capable = true;
	return dw_i3c_common_probe(&i3c->dw, pdev);
}

static void ast2600_i3c_remove(struct platform_device *pdev)
{
	struct dw_i3c_master *dw_i3c = platform_get_drvdata(pdev);

	dw_i3c_common_remove(dw_i3c);
}

static const struct of_device_id ast2600_i3c_master_of_match[] = {
	{ .compatible = "aspeed,ast2600-i3c", },
	{},
};
MODULE_DEVICE_TABLE(of, ast2600_i3c_master_of_match);

static struct platform_driver ast2600_i3c_driver = {
	.probe = ast2600_i3c_probe,
	.remove_new = ast2600_i3c_remove,
	.driver = {
		.name = "ast2600-i3c-master",
		.of_match_table = ast2600_i3c_master_of_match,
	},
};
module_platform_driver(ast2600_i3c_driver);

MODULE_AUTHOR("Jeremy Kerr <jk@codeconstruct.com.au>");
MODULE_DESCRIPTION("ASPEED AST2600 I3C driver");
MODULE_LICENSE("GPL");
