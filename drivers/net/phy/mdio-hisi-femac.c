// SPDX-License-Identifier: GPL-2.0+
/*
 * Hisilicon Fast Ethernet MDIO Bus Driver
 *
 * Copyright (c) 2016 HiSilicon Technologies Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>
#include <linux/platform_device.h>

#define MDIO_RWCTRL		0x00
#define MDIO_RO_DATA		0x04
#define MDIO_WRITE		BIT(13)
#define MDIO_RW_FINISH		BIT(15)
#define BIT_PHY_ADDR_OFFSET	8
#define BIT_WR_DATA_OFFSET	16

struct hisi_femac_mdio_data {
	struct clk *clk;
	void __iomem *membase;
};

static int hisi_femac_mdio_wait_ready(struct hisi_femac_mdio_data *data)
{
	u32 val;

	return readl_poll_timeout(data->membase + MDIO_RWCTRL,
				  val, val & MDIO_RW_FINISH, 20, 10000);
}

static int hisi_femac_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct hisi_femac_mdio_data *data = bus->priv;
	int ret;

	ret = hisi_femac_mdio_wait_ready(data);
	if (ret)
		return ret;

	writel((mii_id << BIT_PHY_ADDR_OFFSET) | regnum,
	       data->membase + MDIO_RWCTRL);

	ret = hisi_femac_mdio_wait_ready(data);
	if (ret)
		return ret;

	return readl(data->membase + MDIO_RO_DATA) & 0xFFFF;
}

static int hisi_femac_mdio_write(struct mii_bus *bus, int mii_id, int regnum,
				 u16 value)
{
	struct hisi_femac_mdio_data *data = bus->priv;
	int ret;

	ret = hisi_femac_mdio_wait_ready(data);
	if (ret)
		return ret;

	writel(MDIO_WRITE | (value << BIT_WR_DATA_OFFSET) |
	       (mii_id << BIT_PHY_ADDR_OFFSET) | regnum,
	       data->membase + MDIO_RWCTRL);

	return hisi_femac_mdio_wait_ready(data);
}

static int hisi_femac_mdio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mii_bus *bus;
	struct hisi_femac_mdio_data *data;
	struct resource *res;
	int ret;

	bus = mdiobus_alloc_size(sizeof(*data));
	if (!bus)
		return -ENOMEM;

	bus->name = "hisi_femac_mii_bus";
	bus->read = &hisi_femac_mdio_read;
	bus->write = &hisi_femac_mdio_write;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", pdev->name);
	bus->parent = &pdev->dev;

	data = bus->priv;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->membase)) {
		ret = PTR_ERR(data->membase);
		goto err_out_free_mdiobus;
	}

	data->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(data->clk)) {
		ret = PTR_ERR(data->clk);
		goto err_out_free_mdiobus;
	}

	ret = clk_prepare_enable(data->clk);
	if (ret)
		goto err_out_free_mdiobus;

	ret = of_mdiobus_register(bus, np);
	if (ret)
		goto err_out_disable_clk;

	platform_set_drvdata(pdev, bus);

	return 0;

err_out_disable_clk:
	clk_disable_unprepare(data->clk);
err_out_free_mdiobus:
	mdiobus_free(bus);
	return ret;
}

static int hisi_femac_mdio_remove(struct platform_device *pdev)
{
	struct mii_bus *bus = platform_get_drvdata(pdev);
	struct hisi_femac_mdio_data *data = bus->priv;

	mdiobus_unregister(bus);
	clk_disable_unprepare(data->clk);
	mdiobus_free(bus);

	return 0;
}

static const struct of_device_id hisi_femac_mdio_dt_ids[] = {
	{ .compatible = "hisilicon,hisi-femac-mdio" },
	{ }
};
MODULE_DEVICE_TABLE(of, hisi_femac_mdio_dt_ids);

static struct platform_driver hisi_femac_mdio_driver = {
	.probe = hisi_femac_mdio_probe,
	.remove = hisi_femac_mdio_remove,
	.driver = {
		.name = "hisi-femac-mdio",
		.of_match_table = hisi_femac_mdio_dt_ids,
	},
};

module_platform_driver(hisi_femac_mdio_driver);

MODULE_DESCRIPTION("Hisilicon Fast Ethernet MAC MDIO interface driver");
MODULE_AUTHOR("Dongpo Li <lidongpo@hisilicon.com>");
MODULE_LICENSE("GPL");
