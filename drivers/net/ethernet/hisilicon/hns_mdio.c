/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/acpi.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define MDIO_DRV_NAME "Hi-HNS_MDIO"
#define MDIO_BUS_NAME "Hisilicon MII Bus"

#define MDIO_TIMEOUT			1000000

struct hns_mdio_sc_reg {
	u16 mdio_clk_en;
	u16 mdio_clk_dis;
	u16 mdio_reset_req;
	u16 mdio_reset_dreq;
	u16 mdio_clk_st;
	u16 mdio_reset_st;
};

struct hns_mdio_device {
	void *vbase;		/* mdio reg base address */
	struct regmap *subctrl_vbase;
	struct hns_mdio_sc_reg sc_reg;
};

/* mdio reg */
#define MDIO_COMMAND_REG		0x0
#define MDIO_ADDR_REG			0x4
#define MDIO_WDATA_REG			0x8
#define MDIO_RDATA_REG			0xc
#define MDIO_STA_REG			0x10

/* cfg phy bit map */
#define MDIO_CMD_DEVAD_M	0x1f
#define MDIO_CMD_DEVAD_S	0
#define MDIO_CMD_PRTAD_M	0x1f
#define MDIO_CMD_PRTAD_S	5
#define MDIO_CMD_OP_S		10
#define MDIO_CMD_ST_S		12
#define MDIO_CMD_START_B	14

#define MDIO_ADDR_DATA_M	0xffff
#define MDIO_ADDR_DATA_S	0

#define MDIO_WDATA_DATA_M	0xffff
#define MDIO_WDATA_DATA_S	0

#define MDIO_RDATA_DATA_M	0xffff
#define MDIO_RDATA_DATA_S	0

#define MDIO_STATE_STA_B	0

enum mdio_st_clause {
	MDIO_ST_CLAUSE_45 = 0,
	MDIO_ST_CLAUSE_22
};

enum mdio_c22_op_seq {
	MDIO_C22_WRITE = 1,
	MDIO_C22_READ = 2
};

enum mdio_c45_op_seq {
	MDIO_C45_WRITE_ADDR = 0,
	MDIO_C45_WRITE_DATA,
	MDIO_C45_READ_INCREMENT,
	MDIO_C45_READ
};

/* peri subctrl reg */
#define MDIO_SC_CLK_EN		0x338
#define MDIO_SC_CLK_DIS		0x33C
#define MDIO_SC_RESET_REQ	0xA38
#define MDIO_SC_RESET_DREQ	0xA3C
#define MDIO_SC_CLK_ST		0x531C
#define MDIO_SC_RESET_ST	0x5A1C

static void mdio_write_reg(void *base, u32 reg, u32 value)
{
	u8 __iomem *reg_addr = (u8 __iomem *)base;

	writel_relaxed(value, reg_addr + reg);
}

#define MDIO_WRITE_REG(a, reg, value) \
	mdio_write_reg((a)->vbase, (reg), (value))

static u32 mdio_read_reg(void *base, u32 reg)
{
	u8 __iomem *reg_addr = (u8 __iomem *)base;

	return readl_relaxed(reg_addr + reg);
}

#define mdio_set_field(origin, mask, shift, val) \
	do { \
		(origin) &= (~((mask) << (shift))); \
		(origin) |= (((val) & (mask)) << (shift)); \
	} while (0)

#define mdio_get_field(origin, mask, shift) (((origin) >> (shift)) & (mask))

static void mdio_set_reg_field(void *base, u32 reg, u32 mask, u32 shift,
			       u32 val)
{
	u32 origin = mdio_read_reg(base, reg);

	mdio_set_field(origin, mask, shift, val);
	mdio_write_reg(base, reg, origin);
}

#define MDIO_SET_REG_FIELD(dev, reg, mask, shift, val) \
	mdio_set_reg_field((dev)->vbase, (reg), (mask), (shift), (val))

static u32 mdio_get_reg_field(void *base, u32 reg, u32 mask, u32 shift)
{
	u32 origin;

	origin = mdio_read_reg(base, reg);
	return mdio_get_field(origin, mask, shift);
}

#define MDIO_GET_REG_FIELD(dev, reg, mask, shift) \
		mdio_get_reg_field((dev)->vbase, (reg), (mask), (shift))

#define MDIO_GET_REG_BIT(dev, reg, bit) \
		mdio_get_reg_field((dev)->vbase, (reg), 0x1ull, (bit))

#define MDIO_CHECK_SET_ST	1
#define MDIO_CHECK_CLR_ST	0

static int mdio_sc_cfg_reg_write(struct hns_mdio_device *mdio_dev,
				 u32 cfg_reg, u32 set_val,
				 u32 st_reg, u32 st_msk, u8 check_st)
{
	u32 time_cnt;
	u32 reg_value;

	regmap_write(mdio_dev->subctrl_vbase, cfg_reg, set_val);

	for (time_cnt = MDIO_TIMEOUT; time_cnt; time_cnt--) {
		regmap_read(mdio_dev->subctrl_vbase, st_reg, &reg_value);
		reg_value &= st_msk;
		if ((!!check_st) == (!!reg_value))
			break;
	}

	if ((!!check_st) != (!!reg_value))
		return -EBUSY;

	return 0;
}

static int hns_mdio_wait_ready(struct mii_bus *bus)
{
	struct hns_mdio_device *mdio_dev = bus->priv;
	u32 cmd_reg_value;
	int i;

	/* waitting for MDIO_COMMAND_REG 's mdio_start==0 */
	/* after that can do read or write*/
	for (i = 0; i < MDIO_TIMEOUT; i++) {
		cmd_reg_value = MDIO_GET_REG_BIT(mdio_dev,
						 MDIO_COMMAND_REG,
						 MDIO_CMD_START_B);
		if (!cmd_reg_value)
			break;
	}
	if ((i == MDIO_TIMEOUT) && cmd_reg_value)
		return -ETIMEDOUT;

	return 0;
}

static void hns_mdio_cmd_write(struct hns_mdio_device *mdio_dev,
			       u8 is_c45, u8 op, u8 phy_id, u16 cmd)
{
	u32 cmd_reg_value;
	u8 st = is_c45 ? MDIO_ST_CLAUSE_45 : MDIO_ST_CLAUSE_22;

	cmd_reg_value = st << MDIO_CMD_ST_S;
	cmd_reg_value |= op << MDIO_CMD_OP_S;
	cmd_reg_value |=
		(phy_id & MDIO_CMD_PRTAD_M) << MDIO_CMD_PRTAD_S;
	cmd_reg_value |= (cmd & MDIO_CMD_DEVAD_M) << MDIO_CMD_DEVAD_S;
	cmd_reg_value |= 1 << MDIO_CMD_START_B;

	MDIO_WRITE_REG(mdio_dev, MDIO_COMMAND_REG, cmd_reg_value);
}

/**
 * hns_mdio_write - access phy register
 * @bus: mdio bus
 * @phy_id: phy id
 * @regnum: register num
 * @value: register value
 *
 * Return 0 on success, negative on failure
 */
static int hns_mdio_write(struct mii_bus *bus,
			  int phy_id, int regnum, u16 data)
{
	int ret;
	struct hns_mdio_device *mdio_dev = (struct hns_mdio_device *)bus->priv;
	u8 devad = ((regnum >> 16) & 0x1f);
	u8 is_c45 = !!(regnum & MII_ADDR_C45);
	u16 reg = (u16)(regnum & 0xffff);
	u8 op;
	u16 cmd_reg_cfg;

	dev_dbg(&bus->dev, "mdio write %s,base is %p\n",
		bus->id, mdio_dev->vbase);
	dev_dbg(&bus->dev, "phy id=%d, is_c45=%d, devad=%d, reg=%#x, write data=%d\n",
		phy_id, is_c45, devad, reg, data);

	/* wait for ready */
	ret = hns_mdio_wait_ready(bus);
	if (ret) {
		dev_err(&bus->dev, "MDIO bus is busy\n");
		return ret;
	}

	if (!is_c45) {
		cmd_reg_cfg = reg;
		op = MDIO_C22_WRITE;
	} else {
		/* config the cmd-reg to write addr*/
		MDIO_SET_REG_FIELD(mdio_dev, MDIO_ADDR_REG, MDIO_ADDR_DATA_M,
				   MDIO_ADDR_DATA_S, reg);

		hns_mdio_cmd_write(mdio_dev, is_c45,
				   MDIO_C45_WRITE_ADDR, phy_id, devad);

		/* check for read or write opt is finished */
		ret = hns_mdio_wait_ready(bus);
		if (ret) {
			dev_err(&bus->dev, "MDIO bus is busy\n");
			return ret;
		}

		/* config the data needed writing */
		cmd_reg_cfg = devad;
		op = MDIO_C45_WRITE_DATA;
	}

	MDIO_SET_REG_FIELD(mdio_dev, MDIO_WDATA_REG, MDIO_WDATA_DATA_M,
			   MDIO_WDATA_DATA_S, data);

	hns_mdio_cmd_write(mdio_dev, is_c45, op, phy_id, cmd_reg_cfg);

	return 0;
}

/**
 * hns_mdio_read - access phy register
 * @bus: mdio bus
 * @phy_id: phy id
 * @regnum: register num
 * @value: register value
 *
 * Return phy register value
 */
static int hns_mdio_read(struct mii_bus *bus, int phy_id, int regnum)
{
	int ret;
	u16 reg_val = 0;
	u8 devad = ((regnum >> 16) & 0x1f);
	u8 is_c45 = !!(regnum & MII_ADDR_C45);
	u16 reg = (u16)(regnum & 0xffff);
	struct hns_mdio_device *mdio_dev = (struct hns_mdio_device *)bus->priv;

	dev_dbg(&bus->dev, "mdio read %s,base is %p\n",
		bus->id, mdio_dev->vbase);
	dev_dbg(&bus->dev, "phy id=%d, is_c45=%d, devad=%d, reg=%#x!\n",
		phy_id, is_c45, devad, reg);

	/* Step 1: wait for ready */
	ret = hns_mdio_wait_ready(bus);
	if (ret) {
		dev_err(&bus->dev, "MDIO bus is busy\n");
		return ret;
	}

	if (!is_c45) {
		hns_mdio_cmd_write(mdio_dev, is_c45,
				   MDIO_C22_READ, phy_id, reg);
	} else {
		MDIO_SET_REG_FIELD(mdio_dev, MDIO_ADDR_REG, MDIO_ADDR_DATA_M,
				   MDIO_ADDR_DATA_S, reg);

		/* Step 2; config the cmd-reg to write addr*/
		hns_mdio_cmd_write(mdio_dev, is_c45,
				   MDIO_C45_WRITE_ADDR, phy_id, devad);

		/* Step 3: check for read or write opt is finished */
		ret = hns_mdio_wait_ready(bus);
		if (ret) {
			dev_err(&bus->dev, "MDIO bus is busy\n");
			return ret;
		}

		hns_mdio_cmd_write(mdio_dev, is_c45,
				   MDIO_C45_READ, phy_id, devad);
	}

	/* Step 5: waitting for MDIO_COMMAND_REG 's mdio_start==0,*/
	/* check for read or write opt is finished */
	ret = hns_mdio_wait_ready(bus);
	if (ret) {
		dev_err(&bus->dev, "MDIO bus is busy\n");
		return ret;
	}

	reg_val = MDIO_GET_REG_BIT(mdio_dev, MDIO_STA_REG, MDIO_STATE_STA_B);
	if (reg_val) {
		dev_err(&bus->dev, " ERROR! MDIO Read failed!\n");
		return -EBUSY;
	}

	/* Step 6; get out data*/
	reg_val = (u16)MDIO_GET_REG_FIELD(mdio_dev, MDIO_RDATA_REG,
					  MDIO_RDATA_DATA_M, MDIO_RDATA_DATA_S);

	return reg_val;
}

/**
 * hns_mdio_reset - reset mdio bus
 * @bus: mdio bus
 *
 * Return 0 on success, negative on failure
 */
static int hns_mdio_reset(struct mii_bus *bus)
{
	struct hns_mdio_device *mdio_dev = (struct hns_mdio_device *)bus->priv;
	const struct hns_mdio_sc_reg *sc_reg;
	int ret;

	if (dev_of_node(bus->parent)) {
		if (!mdio_dev->subctrl_vbase) {
			dev_err(&bus->dev, "mdio sys ctl reg has not maped\n");
			return -ENODEV;
		}

		sc_reg = &mdio_dev->sc_reg;
		/* 1. reset req, and read reset st check */
		ret = mdio_sc_cfg_reg_write(mdio_dev, sc_reg->mdio_reset_req,
					    0x1, sc_reg->mdio_reset_st, 0x1,
					    MDIO_CHECK_SET_ST);
		if (ret) {
			dev_err(&bus->dev, "MDIO reset fail\n");
			return ret;
		}

		/* 2. dis clk, and read clk st check */
		ret = mdio_sc_cfg_reg_write(mdio_dev, sc_reg->mdio_clk_dis,
					    0x1, sc_reg->mdio_clk_st, 0x1,
					    MDIO_CHECK_CLR_ST);
		if (ret) {
			dev_err(&bus->dev, "MDIO dis clk fail\n");
			return ret;
		}

		/* 3. reset dreq, and read reset st check */
		ret = mdio_sc_cfg_reg_write(mdio_dev, sc_reg->mdio_reset_dreq,
					    0x1, sc_reg->mdio_reset_st, 0x1,
					    MDIO_CHECK_CLR_ST);
		if (ret) {
			dev_err(&bus->dev, "MDIO dis clk fail\n");
			return ret;
		}

		/* 4. en clk, and read clk st check */
		ret = mdio_sc_cfg_reg_write(mdio_dev, sc_reg->mdio_clk_en,
					    0x1, sc_reg->mdio_clk_st, 0x1,
					    MDIO_CHECK_SET_ST);
		if (ret)
			dev_err(&bus->dev, "MDIO en clk fail\n");
	} else if (is_acpi_node(bus->parent->fwnode)) {
		acpi_status s;

		s = acpi_evaluate_object(ACPI_HANDLE(bus->parent),
					 "_RST", NULL, NULL);
		if (ACPI_FAILURE(s)) {
			dev_err(&bus->dev, "Reset failed, return:%#x\n", s);
			ret = -EBUSY;
		} else {
			ret = 0;
		}
	} else {
		dev_err(&bus->dev, "Can not get cfg data from DT or ACPI\n");
		ret = -ENXIO;
	}
	return ret;
}

/**
 * hns_mdio_probe - probe mdio device
 * @pdev: mdio platform device
 *
 * Return 0 on success, negative on failure
 */
static int hns_mdio_probe(struct platform_device *pdev)
{
	struct hns_mdio_device *mdio_dev;
	struct mii_bus *new_bus;
	struct resource *res;
	int ret = -ENODEV;

	if (!pdev) {
		dev_err(NULL, "pdev is NULL!\r\n");
		return -ENODEV;
	}

	mdio_dev = devm_kzalloc(&pdev->dev, sizeof(*mdio_dev), GFP_KERNEL);
	if (!mdio_dev)
		return -ENOMEM;

	new_bus = devm_mdiobus_alloc(&pdev->dev);
	if (!new_bus) {
		dev_err(&pdev->dev, "mdiobus_alloc fail!\n");
		return -ENOMEM;
	}

	new_bus->name = MDIO_BUS_NAME;
	new_bus->read = hns_mdio_read;
	new_bus->write = hns_mdio_write;
	new_bus->reset = hns_mdio_reset;
	new_bus->priv = mdio_dev;
	new_bus->parent = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mdio_dev->vbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mdio_dev->vbase)) {
		ret = PTR_ERR(mdio_dev->vbase);
		return ret;
	}

	platform_set_drvdata(pdev, new_bus);
	snprintf(new_bus->id, MII_BUS_ID_SIZE, "%s-%s", "Mii",
		 dev_name(&pdev->dev));
	if (dev_of_node(&pdev->dev)) {
		struct of_phandle_args reg_args;

		ret = of_parse_phandle_with_fixed_args(pdev->dev.of_node,
						       "subctrl-vbase",
						       4,
						       0,
						       &reg_args);
		if (!ret) {
			mdio_dev->subctrl_vbase =
				syscon_node_to_regmap(reg_args.np);
			if (IS_ERR(mdio_dev->subctrl_vbase)) {
				dev_warn(&pdev->dev, "syscon_node_to_regmap error\n");
				mdio_dev->subctrl_vbase = NULL;
			} else {
				if (reg_args.args_count == 4) {
					mdio_dev->sc_reg.mdio_clk_en =
						(u16)reg_args.args[0];
					mdio_dev->sc_reg.mdio_clk_dis =
						(u16)reg_args.args[0] + 4;
					mdio_dev->sc_reg.mdio_reset_req =
						(u16)reg_args.args[1];
					mdio_dev->sc_reg.mdio_reset_dreq =
						(u16)reg_args.args[1] + 4;
					mdio_dev->sc_reg.mdio_clk_st =
						(u16)reg_args.args[2];
					mdio_dev->sc_reg.mdio_reset_st =
						(u16)reg_args.args[3];
				} else {
					/* for compatible */
					mdio_dev->sc_reg.mdio_clk_en =
						MDIO_SC_CLK_EN;
					mdio_dev->sc_reg.mdio_clk_dis =
						MDIO_SC_CLK_DIS;
					mdio_dev->sc_reg.mdio_reset_req =
						MDIO_SC_RESET_REQ;
					mdio_dev->sc_reg.mdio_reset_dreq =
						MDIO_SC_RESET_DREQ;
					mdio_dev->sc_reg.mdio_clk_st =
						MDIO_SC_CLK_ST;
					mdio_dev->sc_reg.mdio_reset_st =
						MDIO_SC_RESET_ST;
				}
			}
		} else {
			dev_warn(&pdev->dev, "find syscon ret = %#x\n", ret);
			mdio_dev->subctrl_vbase = NULL;
		}

		ret = of_mdiobus_register(new_bus, pdev->dev.of_node);
	} else if (is_acpi_node(pdev->dev.fwnode)) {
		/* Clear all the IRQ properties */
		memset(new_bus->irq, PHY_POLL, 4 * PHY_MAX_ADDR);

		/* Mask out all PHYs from auto probing. */
		new_bus->phy_mask = ~0;

		/* Register the MDIO bus */
		ret = mdiobus_register(new_bus);
	} else {
		dev_err(&pdev->dev, "Can not get cfg data from DT or ACPI\n");
		ret = -ENXIO;
	}

	if (ret) {
		dev_err(&pdev->dev, "Cannot register as MDIO bus!\n");
		platform_set_drvdata(pdev, NULL);
		return ret;
	}

	return 0;
}

/**
 * hns_mdio_remove - remove mdio device
 * @pdev: mdio platform device
 *
 * Return 0 on success, negative on failure
 */
static int hns_mdio_remove(struct platform_device *pdev)
{
	struct mii_bus *bus;

	bus = platform_get_drvdata(pdev);

	mdiobus_unregister(bus);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id hns_mdio_match[] = {
	{.compatible = "hisilicon,mdio"},
	{.compatible = "hisilicon,hns-mdio"},
	{}
};
MODULE_DEVICE_TABLE(of, hns_mdio_match);

static const struct acpi_device_id hns_mdio_acpi_match[] = {
	{ "HISI0141", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, hns_mdio_acpi_match);

static struct platform_driver hns_mdio_driver = {
	.probe = hns_mdio_probe,
	.remove = hns_mdio_remove,
	.driver = {
		   .name = MDIO_DRV_NAME,
		   .of_match_table = hns_mdio_match,
		   .acpi_match_table = ACPI_PTR(hns_mdio_acpi_match),
		   },
};

module_platform_driver(hns_mdio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huawei Tech. Co., Ltd.");
MODULE_DESCRIPTION("Hisilicon HNS MDIO driver");
MODULE_ALIAS("platform:" MDIO_DRV_NAME);
