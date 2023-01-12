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
#include "mlxbf_gige_mdio_bf2.h"
#include "mlxbf_gige_mdio_bf3.h"

static struct mlxbf_gige_mdio_gw mlxbf_gige_mdio_gw_t[] = {
	[MLXBF_GIGE_VERSION_BF2] = {
		.gw_address = MLXBF2_GIGE_MDIO_GW_OFFSET,
		.read_data_address = MLXBF2_GIGE_MDIO_GW_OFFSET,
		.busy = {
			.mask = MLXBF2_GIGE_MDIO_GW_BUSY_MASK,
			.shift = MLXBF2_GIGE_MDIO_GW_BUSY_SHIFT,
		},
		.read_data = {
			.mask = MLXBF2_GIGE_MDIO_GW_AD_MASK,
			.shift = MLXBF2_GIGE_MDIO_GW_AD_SHIFT,
		},
		.write_data = {
			.mask = MLXBF2_GIGE_MDIO_GW_AD_MASK,
			.shift = MLXBF2_GIGE_MDIO_GW_AD_SHIFT,
		},
		.devad = {
			.mask = MLXBF2_GIGE_MDIO_GW_DEVAD_MASK,
			.shift = MLXBF2_GIGE_MDIO_GW_DEVAD_SHIFT,
		},
		.partad = {
			.mask = MLXBF2_GIGE_MDIO_GW_PARTAD_MASK,
			.shift = MLXBF2_GIGE_MDIO_GW_PARTAD_SHIFT,
		},
		.opcode = {
			.mask = MLXBF2_GIGE_MDIO_GW_OPCODE_MASK,
			.shift = MLXBF2_GIGE_MDIO_GW_OPCODE_SHIFT,
		},
		.st1 = {
			.mask = MLXBF2_GIGE_MDIO_GW_ST1_MASK,
			.shift = MLXBF2_GIGE_MDIO_GW_ST1_SHIFT,
		},
	},
	[MLXBF_GIGE_VERSION_BF3] = {
		.gw_address = MLXBF3_GIGE_MDIO_GW_OFFSET,
		.read_data_address = MLXBF3_GIGE_MDIO_DATA_READ,
		.busy = {
			.mask = MLXBF3_GIGE_MDIO_GW_BUSY_MASK,
			.shift = MLXBF3_GIGE_MDIO_GW_BUSY_SHIFT,
		},
		.read_data = {
			.mask = MLXBF3_GIGE_MDIO_GW_DATA_READ_MASK,
			.shift = MLXBF3_GIGE_MDIO_GW_DATA_READ_SHIFT,
		},
		.write_data = {
			.mask = MLXBF3_GIGE_MDIO_GW_DATA_MASK,
			.shift = MLXBF3_GIGE_MDIO_GW_DATA_SHIFT,
		},
		.devad = {
			.mask = MLXBF3_GIGE_MDIO_GW_DEVAD_MASK,
			.shift = MLXBF3_GIGE_MDIO_GW_DEVAD_SHIFT,
		},
		.partad = {
			.mask = MLXBF3_GIGE_MDIO_GW_PARTAD_MASK,
			.shift = MLXBF3_GIGE_MDIO_GW_PARTAD_SHIFT,
		},
		.opcode = {
			.mask = MLXBF3_GIGE_MDIO_GW_OPCODE_MASK,
			.shift = MLXBF3_GIGE_MDIO_GW_OPCODE_SHIFT,
		},
		.st1 = {
			.mask = MLXBF3_GIGE_MDIO_GW_ST1_MASK,
			.shift = MLXBF3_GIGE_MDIO_GW_ST1_SHIFT,
		},
	},
};

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

#define MLXBF_GIGE_BF2_COREPLL_ADDR 0x02800c30
#define MLXBF_GIGE_BF2_COREPLL_SIZE 0x0000000c
#define MLXBF_GIGE_BF3_COREPLL_ADDR 0x13409824
#define MLXBF_GIGE_BF3_COREPLL_SIZE 0x00000010

static struct resource corepll_params[] = {
	[MLXBF_GIGE_VERSION_BF2] = {
		.start = MLXBF_GIGE_BF2_COREPLL_ADDR,
		.end = MLXBF_GIGE_BF2_COREPLL_ADDR + MLXBF_GIGE_BF2_COREPLL_SIZE - 1,
		.name = "COREPLL_RES"
	},
	[MLXBF_GIGE_VERSION_BF3] = {
		.start = MLXBF_GIGE_BF3_COREPLL_ADDR,
		.end = MLXBF_GIGE_BF3_COREPLL_ADDR + MLXBF_GIGE_BF3_COREPLL_SIZE - 1,
		.name = "COREPLL_RES"
	}
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

static u32 mlxbf_gige_mdio_create_cmd(struct mlxbf_gige_mdio_gw *mdio_gw, u16 data, int phy_add,
				      int phy_reg, u32 opcode)
{
	u32 gw_reg = 0;

	gw_reg |= ((data << mdio_gw->write_data.shift) &
		   mdio_gw->write_data.mask);
	gw_reg |= ((phy_reg << mdio_gw->devad.shift) &
		   mdio_gw->devad.mask);
	gw_reg |= ((phy_add << mdio_gw->partad.shift) &
		   mdio_gw->partad.mask);
	gw_reg |= ((opcode << mdio_gw->opcode.shift) &
		   mdio_gw->opcode.mask);
	gw_reg |= ((MLXBF_GIGE_MDIO_CL22_ST1 << mdio_gw->st1.shift) &
		   mdio_gw->st1.mask);
	gw_reg |= ((MLXBF_GIGE_MDIO_SET_BUSY << mdio_gw->busy.shift) &
		   mdio_gw->busy.mask);

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
	cmd = mlxbf_gige_mdio_create_cmd(priv->mdio_gw, 0, phy_add, phy_reg,
					 MLXBF_GIGE_MDIO_CL22_READ);

	writel(cmd, priv->mdio_io + priv->mdio_gw->gw_address);

	ret = readl_poll_timeout_atomic(priv->mdio_io + priv->mdio_gw->gw_address,
					val, !(val & priv->mdio_gw->busy.mask),
					5, 1000000);

	if (ret) {
		writel(0, priv->mdio_io + priv->mdio_gw->gw_address);
		return ret;
	}

	ret = readl(priv->mdio_io + priv->mdio_gw->read_data_address);
	/* Only return ad bits of the gw register */
	ret &= priv->mdio_gw->read_data.mask;

	/* The MDIO lock is set on read. To release it, clear gw register */
	writel(0, priv->mdio_io + priv->mdio_gw->gw_address);

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
	cmd = mlxbf_gige_mdio_create_cmd(priv->mdio_gw, val, phy_add, phy_reg,
					 MLXBF_GIGE_MDIO_CL22_WRITE);
	writel(cmd, priv->mdio_io + priv->mdio_gw->gw_address);

	/* If the poll timed out, drop the request */
	ret = readl_poll_timeout_atomic(priv->mdio_io + priv->mdio_gw->gw_address,
					temp, !(temp & priv->mdio_gw->busy.mask),
					5, 1000000);

	/* The MDIO lock is set on read. To release it, clear gw register */
	writel(0, priv->mdio_io + priv->mdio_gw->gw_address);

	return ret;
}

static void mlxbf_gige_mdio_cfg(struct mlxbf_gige *priv)
{
	u8 mdio_period;
	u32 val;

	mdio_period = mdio_period_map(priv);

	if (priv->hw_version == MLXBF_GIGE_VERSION_BF2) {
		val = MLXBF2_GIGE_MDIO_CFG_VAL;
		val |= FIELD_PREP(MLXBF2_GIGE_MDIO_CFG_MDC_PERIOD_MASK, mdio_period);
		writel(val, priv->mdio_io + MLXBF2_GIGE_MDIO_CFG_OFFSET);
	} else {
		val = FIELD_PREP(MLXBF3_GIGE_MDIO_CFG_MDIO_MODE_MASK, 1) |
		      FIELD_PREP(MLXBF3_GIGE_MDIO_CFG_MDIO_FULL_DRIVE_MASK, 1);
		writel(val, priv->mdio_io + MLXBF3_GIGE_MDIO_CFG_REG0);
		val = FIELD_PREP(MLXBF3_GIGE_MDIO_CFG_MDC_PERIOD_MASK, mdio_period);
		writel(val, priv->mdio_io + MLXBF3_GIGE_MDIO_CFG_REG1);
		val = FIELD_PREP(MLXBF3_GIGE_MDIO_CFG_MDIO_IN_SAMP_MASK, 6) |
		      FIELD_PREP(MLXBF3_GIGE_MDIO_CFG_MDIO_OUT_SAMP_MASK, 13);
		writel(val, priv->mdio_io + MLXBF3_GIGE_MDIO_CFG_REG2);
	}
}

int mlxbf_gige_mdio_probe(struct platform_device *pdev, struct mlxbf_gige *priv)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	if (priv->hw_version > MLXBF_GIGE_VERSION_BF3)
		return -ENODEV;

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
		res = &corepll_params[priv->hw_version];
	}

	priv->clk_io = devm_ioremap(dev, res->start, resource_size(res));
	if (!priv->clk_io)
		return -ENOMEM;

	priv->mdio_gw = &mlxbf_gige_mdio_gw_t[priv->hw_version];

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
