// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Core driver for the Ocelot chip family.
 *
 * The VSC7511, 7512, 7513, and 7514 can be controlled internally via an
 * on-chip MIPS processor, or externally via SPI, I2C, PCIe. This core driver is
 * intended to be the bus-agnostic glue between, for example, the SPI bus and
 * the child devices.
 *
 * Copyright 2021-2022 Innovative Advantage Inc.
 *
 * Author: Colin Foster <colin.foster@in-advantage.com>
 */

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/iopoll.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mfd/ocelot.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include <soc/mscc/ocelot.h>

#include "ocelot.h"

#define REG_GCB_SOFT_RST		0x0008

#define BIT_SOFT_CHIP_RST		BIT(0)

#define VSC7512_MIIM0_RES_START		0x7107009c
#define VSC7512_MIIM1_RES_START		0x710700c0
#define VSC7512_MIIM_RES_SIZE		0x024

#define VSC7512_PHY_RES_START		0x710700f0
#define VSC7512_PHY_RES_SIZE		0x004

#define VSC7512_GPIO_RES_START		0x71070034
#define VSC7512_GPIO_RES_SIZE		0x06c

#define VSC7512_SIO_CTRL_RES_START	0x710700f8
#define VSC7512_SIO_CTRL_RES_SIZE	0x100

#define VSC7512_GCB_RST_SLEEP_US	100
#define VSC7512_GCB_RST_TIMEOUT_US	100000

static int ocelot_gcb_chip_rst_status(struct ocelot_ddata *ddata)
{
	int val, err;

	err = regmap_read(ddata->gcb_regmap, REG_GCB_SOFT_RST, &val);
	if (err)
		return err;

	return val;
}

int ocelot_chip_reset(struct device *dev)
{
	struct ocelot_ddata *ddata = dev_get_drvdata(dev);
	int ret, val;

	/*
	 * Reset the entire chip here to put it into a completely known state.
	 * Other drivers may want to reset their own subsystems. The register
	 * self-clears, so one write is all that is needed and wait for it to
	 * clear.
	 */
	ret = regmap_write(ddata->gcb_regmap, REG_GCB_SOFT_RST, BIT_SOFT_CHIP_RST);
	if (ret)
		return ret;

	return readx_poll_timeout(ocelot_gcb_chip_rst_status, ddata, val, !val,
				  VSC7512_GCB_RST_SLEEP_US, VSC7512_GCB_RST_TIMEOUT_US);
}
EXPORT_SYMBOL_NS(ocelot_chip_reset, MFD_OCELOT);

static const struct resource vsc7512_miim0_resources[] = {
	DEFINE_RES_REG_NAMED(VSC7512_MIIM0_RES_START, VSC7512_MIIM_RES_SIZE, "gcb_miim0"),
	DEFINE_RES_REG_NAMED(VSC7512_PHY_RES_START, VSC7512_PHY_RES_SIZE, "gcb_phy"),
};

static const struct resource vsc7512_miim1_resources[] = {
	DEFINE_RES_REG_NAMED(VSC7512_MIIM1_RES_START, VSC7512_MIIM_RES_SIZE, "gcb_miim1"),
};

static const struct resource vsc7512_pinctrl_resources[] = {
	DEFINE_RES_REG_NAMED(VSC7512_GPIO_RES_START, VSC7512_GPIO_RES_SIZE, "gcb_gpio"),
};

static const struct resource vsc7512_sgpio_resources[] = {
	DEFINE_RES_REG_NAMED(VSC7512_SIO_CTRL_RES_START, VSC7512_SIO_CTRL_RES_SIZE, "gcb_sio"),
};

static const struct mfd_cell vsc7512_devs[] = {
	{
		.name = "ocelot-pinctrl",
		.of_compatible = "mscc,ocelot-pinctrl",
		.num_resources = ARRAY_SIZE(vsc7512_pinctrl_resources),
		.resources = vsc7512_pinctrl_resources,
	}, {
		.name = "ocelot-sgpio",
		.of_compatible = "mscc,ocelot-sgpio",
		.num_resources = ARRAY_SIZE(vsc7512_sgpio_resources),
		.resources = vsc7512_sgpio_resources,
	}, {
		.name = "ocelot-miim0",
		.of_compatible = "mscc,ocelot-miim",
		.of_reg = VSC7512_MIIM0_RES_START,
		.use_of_reg = true,
		.num_resources = ARRAY_SIZE(vsc7512_miim0_resources),
		.resources = vsc7512_miim0_resources,
	}, {
		.name = "ocelot-miim1",
		.of_compatible = "mscc,ocelot-miim",
		.of_reg = VSC7512_MIIM1_RES_START,
		.use_of_reg = true,
		.num_resources = ARRAY_SIZE(vsc7512_miim1_resources),
		.resources = vsc7512_miim1_resources,
	},
};

static void ocelot_core_try_add_regmap(struct device *dev,
				       const struct resource *res)
{
	if (dev_get_regmap(dev, res->name))
		return;

	ocelot_spi_init_regmap(dev, res);
}

static void ocelot_core_try_add_regmaps(struct device *dev,
					const struct mfd_cell *cell)
{
	int i;

	for (i = 0; i < cell->num_resources; i++)
		ocelot_core_try_add_regmap(dev, &cell->resources[i]);
}

int ocelot_core_init(struct device *dev)
{
	int i, ndevs;

	ndevs = ARRAY_SIZE(vsc7512_devs);

	for (i = 0; i < ndevs; i++)
		ocelot_core_try_add_regmaps(dev, &vsc7512_devs[i]);

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, vsc7512_devs, ndevs, NULL, 0, NULL);
}
EXPORT_SYMBOL_NS(ocelot_core_init, MFD_OCELOT);

MODULE_DESCRIPTION("Externally Controlled Ocelot Chip Driver");
MODULE_AUTHOR("Colin Foster <colin.foster@in-advantage.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(MFD_OCELOT_SPI);
