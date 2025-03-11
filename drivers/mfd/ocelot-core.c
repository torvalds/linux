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
#define VSC7512_MIIM_RES_SIZE		0x00000024

#define VSC7512_PHY_RES_START		0x710700f0
#define VSC7512_PHY_RES_SIZE		0x00000004

#define VSC7512_GPIO_RES_START		0x71070034
#define VSC7512_GPIO_RES_SIZE		0x0000006c

#define VSC7512_SIO_CTRL_RES_START	0x710700f8
#define VSC7512_SIO_CTRL_RES_SIZE	0x00000100

#define VSC7512_HSIO_RES_START		0x710d0000
#define VSC7512_HSIO_RES_SIZE		0x00000128

#define VSC7512_ANA_RES_START		0x71880000
#define VSC7512_ANA_RES_SIZE		0x00010000

#define VSC7512_QS_RES_START		0x71080000
#define VSC7512_QS_RES_SIZE		0x00000100

#define VSC7512_QSYS_RES_START		0x71800000
#define VSC7512_QSYS_RES_SIZE		0x00200000

#define VSC7512_REW_RES_START		0x71030000
#define VSC7512_REW_RES_SIZE		0x00010000

#define VSC7512_SYS_RES_START		0x71010000
#define VSC7512_SYS_RES_SIZE		0x00010000

#define VSC7512_S0_RES_START		0x71040000
#define VSC7512_S1_RES_START		0x71050000
#define VSC7512_S2_RES_START		0x71060000
#define VCAP_RES_SIZE			0x00000400

#define VSC7512_PORT_0_RES_START	0x711e0000
#define VSC7512_PORT_1_RES_START	0x711f0000
#define VSC7512_PORT_2_RES_START	0x71200000
#define VSC7512_PORT_3_RES_START	0x71210000
#define VSC7512_PORT_4_RES_START	0x71220000
#define VSC7512_PORT_5_RES_START	0x71230000
#define VSC7512_PORT_6_RES_START	0x71240000
#define VSC7512_PORT_7_RES_START	0x71250000
#define VSC7512_PORT_8_RES_START	0x71260000
#define VSC7512_PORT_9_RES_START	0x71270000
#define VSC7512_PORT_10_RES_START	0x71280000
#define VSC7512_PORT_RES_SIZE		0x00010000

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
EXPORT_SYMBOL_NS(ocelot_chip_reset, "MFD_OCELOT");

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

static const struct resource vsc7512_serdes_resources[] = {
	DEFINE_RES_REG_NAMED(VSC7512_HSIO_RES_START, VSC7512_HSIO_RES_SIZE, "hsio"),
};

static const struct resource vsc7512_switch_resources[] = {
	DEFINE_RES_REG_NAMED(VSC7512_ANA_RES_START, VSC7512_ANA_RES_SIZE, "ana"),
	DEFINE_RES_REG_NAMED(VSC7512_HSIO_RES_START, VSC7512_HSIO_RES_SIZE, "hsio"),
	DEFINE_RES_REG_NAMED(VSC7512_QS_RES_START, VSC7512_QS_RES_SIZE, "qs"),
	DEFINE_RES_REG_NAMED(VSC7512_QSYS_RES_START, VSC7512_QSYS_RES_SIZE, "qsys"),
	DEFINE_RES_REG_NAMED(VSC7512_REW_RES_START, VSC7512_REW_RES_SIZE, "rew"),
	DEFINE_RES_REG_NAMED(VSC7512_SYS_RES_START, VSC7512_SYS_RES_SIZE, "sys"),
	DEFINE_RES_REG_NAMED(VSC7512_S0_RES_START, VCAP_RES_SIZE, "s0"),
	DEFINE_RES_REG_NAMED(VSC7512_S1_RES_START, VCAP_RES_SIZE, "s1"),
	DEFINE_RES_REG_NAMED(VSC7512_S2_RES_START, VCAP_RES_SIZE, "s2"),
	DEFINE_RES_REG_NAMED(VSC7512_PORT_0_RES_START, VSC7512_PORT_RES_SIZE, "port0"),
	DEFINE_RES_REG_NAMED(VSC7512_PORT_1_RES_START, VSC7512_PORT_RES_SIZE, "port1"),
	DEFINE_RES_REG_NAMED(VSC7512_PORT_2_RES_START, VSC7512_PORT_RES_SIZE, "port2"),
	DEFINE_RES_REG_NAMED(VSC7512_PORT_3_RES_START, VSC7512_PORT_RES_SIZE, "port3"),
	DEFINE_RES_REG_NAMED(VSC7512_PORT_4_RES_START, VSC7512_PORT_RES_SIZE, "port4"),
	DEFINE_RES_REG_NAMED(VSC7512_PORT_5_RES_START, VSC7512_PORT_RES_SIZE, "port5"),
	DEFINE_RES_REG_NAMED(VSC7512_PORT_6_RES_START, VSC7512_PORT_RES_SIZE, "port6"),
	DEFINE_RES_REG_NAMED(VSC7512_PORT_7_RES_START, VSC7512_PORT_RES_SIZE, "port7"),
	DEFINE_RES_REG_NAMED(VSC7512_PORT_8_RES_START, VSC7512_PORT_RES_SIZE, "port8"),
	DEFINE_RES_REG_NAMED(VSC7512_PORT_9_RES_START, VSC7512_PORT_RES_SIZE, "port9"),
	DEFINE_RES_REG_NAMED(VSC7512_PORT_10_RES_START, VSC7512_PORT_RES_SIZE, "port10")
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
	}, {
		.name = "ocelot-serdes",
		.of_compatible = "mscc,vsc7514-serdes",
		.num_resources = ARRAY_SIZE(vsc7512_serdes_resources),
		.resources = vsc7512_serdes_resources,
	}, {
		.name = "ocelot-ext-switch",
		.of_compatible = "mscc,vsc7512-switch",
		.num_resources = ARRAY_SIZE(vsc7512_switch_resources),
		.resources = vsc7512_switch_resources,
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
EXPORT_SYMBOL_NS(ocelot_core_init, "MFD_OCELOT");

MODULE_DESCRIPTION("Externally Controlled Ocelot Chip Driver");
MODULE_AUTHOR("Colin Foster <colin.foster@in-advantage.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("MFD_OCELOT_SPI");
