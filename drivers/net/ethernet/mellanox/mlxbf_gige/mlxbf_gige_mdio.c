// SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause

/* MDIO support for Mellanox Gigabit Ethernet driver
 *
 * Copyright (C) 2020-2021 NVIDIA CORPORATION & AFFILIATES
 */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/ioport.h>
#include <linux/irqreturn.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#include "mlxbf_gige.h"
#include "mlxbf_gige_regs.h"

#define MLXBF_GIGE_MDIO_GW_OFFSET	0x0
#define MLXBF_GIGE_MDIO_CFG_OFFSET	0x4

#define MLXBF_GIGE_MDIO_FREQ_REFERENCE 156250000ULL
#define MLXBF_GIGE_MDIO_COREPLL_CONST  16384ULL
#define MLXBF_GIGE_MDC_CLK_NS          400
#define MLXBF_GIGE_MDIO_PLL_I1CLK_REG1 0x4
#define MLXBF_GIGE_MDIO_PLL_I1CLK_REG2 0x8
#define MLXBF_GIGE_MDIO_CORE_F_SHIFT   0
#define MLXBF_GIGE_MDIO_CORE_F_MASK    GENMASK(25, 0)
#define MLXBF_GIGE_MDIO_CORE_R_SHIFT   26
#define MLXBF_GIGE_MDIO_CORE_R_MASK    GENMASK(31, 26)
#define MLXBF_GIGE_MDIO_CORE_OD_SHIFT  0
#define MLXBF_GIGE_MDIO_CORE_OD_MASK   GENMASK(3, 0)

/* Support clause 22 */
#define MLXBF_GIGE_MDIO_CL22_ST1	0x1
#define MLXBF_GIGE_MDIO_CL22_WRITE	0x1
#define MLXBF_GIGE_MDIO_CL22_READ	0x2

/* Busy bit is set by software and cleared by hardware */
#define MLXBF_GIGE_MDIO_SET_BUSY	0x1

/* MDIO GW register bits */
#define MLXBF_GIGE_MDIO_GW_AD_MASK	GENMASK(15, 0)
#define MLXBF_GIGE_MDIO_GW_DEVAD_MASK	GENMASK(20, 16)
#define MLXBF_GIGE_MDIO_GW_PARTAD_MASK	GENMASK(25, 21)
#define MLXBF_GIGE_MDIO_GW_OPCODE_MASK	GENMASK(27, 26)
#define MLXBF_GIGE_MDIO_GW_ST1_MASK	GENMASK(28, 28)
#define MLXBF_GIGE_MDIO_GW_BUSY_MASK	GENMASK(30, 30)

/* MDIO config register bits */
#define MLXBF_GIGE_MDIO_CFG_MDIO_MODE_MASK		GENMASK(1, 0)
#define MLXBF_GIGE_MDIO_CFG_MDIO3_3_MASK		GENMASK(2, 2)
#define MLXBF_GIGE_MDIO_CFG_MDIO_FULL_DRIVE_MASK	GENMASK(4, 4)
#define MLXBF_GIGE_MDIO_CFG_MDC_PERIOD_MASK		GENMASK(15, 8)
#define MLXBF_GIGE_MDIO_CFG_MDIO_IN_SAMP_MASK		GENMASK(23, 16)
#define MLXBF_GIGE_MDIO_CFG_MDIO_OUT_SAMP_MASK		GENMASK(31, 24)

#define MLXBF_GIGE_MDIO_CFG_VAL (FIELD_PREP(MLXBF_GIGE_MDIO_CFG_MDIO_MODE_MASK, 1) | \
				 FIELD_PREP(MLXBF_GIGE_MDIO_CFG_MDIO3_3_MASK, 1) | \
				 FIELD_PREP(MLXBF_GIGE_MDIO_CFG_MDIO_FULL_DRIVE_MASK, 1) | \
				 FIELD_PREP(MLXBF_GIGE_MDIO_CFG_MDIO_IN_SAMP_MASK, 6) | \
				 FIELD_PREP(MLXBF_GIGE_MDIO_CFG_MDIO_OUT_SAMP_MASK, 13))

#define MLXBF_GIGE_BF2_COREPLL_ADDR 0x02800c30
#define MLXBF_GIGE_BF2_COREPLL_SIZE 0x0000000c

static struct resource corepll_params[] = {
	[MLXBF_GIGE_VERSION_BF2] = {
		.start = MLXBF_GIGE_BF2_COREPLL_ADDR,
		.end = MLXBF_GIGE_BF2_COREPLL_ADDR + MLXBF_GIGE_BF2_COREPLL_SIZE - 1,
		.name = "COREPLL_RES"
	},
};

/* Returns core clock i1clk in Hz */
static u64 calculate_i1clk(struct mlxbf_gige *priv)
{
	u8 core_od, core_r;
	u64 freq_output;
	u32 reg1, reg2;
	u32 core_f;

	reg1 = readl(priv->clk_io + MLXBF_GIGE_MDIO_PLL_I1CLK_REG1);
	reg2 = readl(priv->clk_io + MLXBF_GIGE_MDIO_PLL_I1CLK_REG2);

	core_f = (reg1 & MLXBF_GIGE_MDIO_CORE_F_MASK) >>
		MLXBF_GIGE_MDIO_CORE_F_SHIFT;
	core_r = (reg1 & MLXBF_GIGE_MDIO_CORE_R_MASK) >>
		MLXBF_GIGE_MDIO_CORE_R_SHIFT;
	core_od = (reg2 & MLXBF_GIGE_MDIO_CORE_OD_MASK) >>
		MLXBF_GIGE_MDIO_CORE_OD_SHIFT;

	/* Compute PLL output frequency as follow:
	 *
	 *                                     CORE_F / 16384
	 * freq_output = freq_reference * ----------------------------
	 *                              (CORE_R + 1) * (CORE_OD + 1)
	 */
	freq_output = div_u64((MLXBF_GIGE_MDIO_FREQ_REFERENCE * core_f),
			      MLXBF_GIGE_MDIO_COREPLL_CONST);
	freq_output = div_u64(freq_output, (core_r + 1) * (core_od + 1));

	return freq_output;
}

/* Formula for encoding the MDIO period. The encoded value is
 * passed to the MDIO config register.
 *
 * mdc_clk = 2*(val + 1)*(core clock in sec)
 *
 * i1clk is in Hz:
 * 400 ns = 2*(val + 1)*(1/i1clk)
 *
 * val = (((400/10^9) / (1/i1clk) / 2) - 1)
 * val = (400/2 * i1clk)/10^9 - 1
 */
static u8 mdio_period_map(struct mlxbf_gige *priv)
{
	u8 mdio_period;
	u64 i1clk;

	i1clk = calculate_i1clk(priv);

	mdio_period = div_u64((MLXBF_GIGE_MDC_CLK_NS >> 1) * i1clk, 1000000000) - 1;

	return mdio_period;
}

static u32 mlxbf_gige_mdio_create_cmd(u16 data, int phy_add,
				      int phy_reg, u32 opcode)
{
	u32 gw_reg = 0;

	gw_reg |= FIELD_PREP(MLXBF_GIGE_MDIO_GW_AD_MASK, data);
	gw_reg |= FIELD_PREP(MLXBF_GIGE_MDIO_GW_DEVAD_MASK, phy_reg);
	gw_reg |= FIELD_PREP(MLXBF_GIGE_MDIO_GW_PARTAD_MASK, phy_add);
	gw_reg |= FIELD_PREP(MLXBF_GIGE_MDIO_GW_OPCODE_MASK, opcode);
	gw_reg |= FIELD_PREP(MLXBF_GIGE_MDIO_GW_ST1_MASK,
			     MLXBF_GIGE_MDIO_CL22_ST1);
	gw_reg |= FIELD_PREP(MLXBF_GIGE_MDIO_GW_BUSY_MASK,
			     MLXBF_GIGE_MDIO_SET_BUSY);

	return gw_reg;
}

static int mlxbf_gige_mdio_read(struct mii_bus *bus, int phy_add, int phy_reg)
{
	struct mlxbf_gige *priv = bus->priv;
	u32 cmd;
	int ret;
	u32 val;

	if (phy_reg & MII_ADDR_C45)
		return -EOPNOTSUPP;

	/* Send mdio read request */
	cmd = mlxbf_gige_mdio_create_cmd(0, phy_add, phy_reg, MLXBF_GIGE_MDIO_CL22_READ);

	writel(cmd, priv->mdio_io + MLXBF_GIGE_MDIO_GW_OFFSET);

	ret = readl_poll_timeout_atomic(priv->mdio_io + MLXBF_GIGE_MDIO_GW_OFFSET,
					val, !(val & MLXBF_GIGE_MDIO_GW_BUSY_MASK),
					5, 1000000);

	if (ret) {
		writel(0, priv->mdio_io + MLXBF_GIGE_MDIO_GW_OFFSET);
		return ret;
	}

	ret = readl(priv->mdio_io + MLXBF_GIGE_MDIO_GW_OFFSET);
	/* Only return ad bits of the gw register */
	ret &= MLXBF_GIGE_MDIO_GW_AD_MASK;

	return ret;
}

static int mlxbf_gige_mdio_write(struct mii_bus *bus, int phy_add,
				 int phy_reg, u16 val)
{
	struct mlxbf_gige *priv = bus->priv;
	u32 temp;
	u32 cmd;
	int ret;

	if (phy_reg & MII_ADDR_C45)
		return -EOPNOTSUPP;

	/* Send mdio write request */
	cmd = mlxbf_gige_mdio_create_cmd(val, phy_add, phy_reg,
					 MLXBF_GIGE_MDIO_CL22_WRITE);
	writel(cmd, priv->mdio_io + MLXBF_GIGE_MDIO_GW_OFFSET);

	/* If the poll timed out, drop the request */
	ret = readl_poll_timeout_atomic(priv->mdio_io + MLXBF_GIGE_MDIO_GW_OFFSET,
					temp, !(temp & MLXBF_GIGE_MDIO_GW_BUSY_MASK),
					5, 1000000);

	return ret;
}

static void mlxbf_gige_mdio_cfg(struct mlxbf_gige *priv)
{
	u8 mdio_period;
	u32 val;

	mdio_period = mdio_period_map(priv);

	val = MLXBF_GIGE_MDIO_CFG_VAL;
	val |= FIELD_PREP(MLXBF_GIGE_MDIO_CFG_MDC_PERIOD_MASK, mdio_period);
	writel(val, priv->mdio_io + MLXBF_GIGE_MDIO_CFG_OFFSET);
}

int mlxbf_gige_mdio_probe(struct platform_device *pdev, struct mlxbf_gige *priv)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	priv->mdio_io = devm_platform_ioremap_resource(pdev, MLXBF_GIGE_RES_MDIO9);
	if (IS_ERR(priv->mdio_io))
		return PTR_ERR(priv->mdio_io);

	/* clk resource shared with other drivers so cannot use
	 * devm_platform_ioremap_resource
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, MLXBF_GIGE_RES_CLK);
	if (!res) {
		/* For backward compatibility with older ACPI tables, also keep
		 * CLK resource internal to the driver.
		 */
		res = &corepll_params[MLXBF_GIGE_VERSION_BF2];
	}

	priv->clk_io = devm_ioremap(dev, res->start, resource_size(res));
	if (IS_ERR(priv->clk_io))
		return PTR_ERR(priv->clk_io);

	mlxbf_gige_mdio_cfg(priv);

	priv->mdiobus = devm_mdiobus_alloc(dev);
	if (!priv->mdiobus) {
		dev_err(dev, "Failed to alloc MDIO bus\n");
		return -ENOMEM;
	}

	priv->mdiobus->name = "mlxbf-mdio";
	priv->mdiobus->read = mlxbf_gige_mdio_read;
	priv->mdiobus->write = mlxbf_gige_mdio_write;
	priv->mdiobus->parent = dev;
	priv->mdiobus->priv = priv;
	snprintf(priv->mdiobus->id, MII_BUS_ID_SIZE, "%s",
		 dev_name(dev));

	ret = mdiobus_register(priv->mdiobus);
	if (ret)
		dev_err(dev, "Failed to register MDIO bus\n");

	return ret;
}

void mlxbf_gige_mdio_remove(struct mlxbf_gige *priv)
{
	mdiobus_unregister(priv->mdiobus);
}
