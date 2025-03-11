// SPDX-License-Identifier: GPL-2.0+
/*
 * TQ-Systems PLD MFD core driver, based on vendor driver by
 * Vadim V.Vlasov <vvlasov@dev.rtsoft.ru>
 *
 * Copyright (c) 2015 TQ-Systems GmbH
 * Copyright (c) 2019 Andrew Lunn <andrew@lunn.ch>
 */

#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/platform_data/i2c-ocores.h>
#include <linux/platform_device.h>

#define TQMX86_IOBASE	0x180
#define TQMX86_IOSIZE	0x20
#define TQMX86_IOBASE_I2C	0x1a0
#define TQMX86_IOSIZE_I2C	0xa
#define TQMX86_IOBASE_WATCHDOG	0x18b
#define TQMX86_IOSIZE_WATCHDOG	0x2
#define TQMX86_IOBASE_GPIO	0x18d
#define TQMX86_IOSIZE_GPIO	0x4

#define TQMX86_REG_BOARD_ID	0x00
#define TQMX86_REG_BOARD_ID_E38M	1
#define TQMX86_REG_BOARD_ID_50UC	2
#define TQMX86_REG_BOARD_ID_E38C	3
#define TQMX86_REG_BOARD_ID_60EB	4
#define TQMX86_REG_BOARD_ID_E39MS	5
#define TQMX86_REG_BOARD_ID_E39C1	6
#define TQMX86_REG_BOARD_ID_E39C2	7
#define TQMX86_REG_BOARD_ID_70EB	8
#define TQMX86_REG_BOARD_ID_80UC	9
#define TQMX86_REG_BOARD_ID_120UC	10
#define TQMX86_REG_BOARD_ID_110EB	11
#define TQMX86_REG_BOARD_ID_E40M	12
#define TQMX86_REG_BOARD_ID_E40S	13
#define TQMX86_REG_BOARD_ID_E40C1	14
#define TQMX86_REG_BOARD_ID_E40C2	15
#define TQMX86_REG_BOARD_ID_130UC	16
#define TQMX86_REG_BOARD_ID_E41S	19
#define TQMX86_REG_BOARD_REV	0x01
#define TQMX86_REG_IO_EXT_INT	0x06
#define TQMX86_REG_IO_EXT_INT_NONE		0
#define TQMX86_REG_IO_EXT_INT_7			1
#define TQMX86_REG_IO_EXT_INT_9			2
#define TQMX86_REG_IO_EXT_INT_12		3
#define TQMX86_REG_IO_EXT_INT_MASK		0x3
#define TQMX86_REG_IO_EXT_INT_I2C1_SHIFT	0
#define TQMX86_REG_IO_EXT_INT_GPIO_SHIFT	4
#define TQMX86_REG_SAUC		0x17

#define TQMX86_REG_I2C_DETECT	0x1a7
#define TQMX86_REG_I2C_DETECT_SOFT		0xa5

static uint gpio_irq;
module_param(gpio_irq, uint, 0);
MODULE_PARM_DESC(gpio_irq, "GPIO IRQ number (valid parameters: 7, 9, 12)");

static uint i2c1_irq;
module_param(i2c1_irq, uint, 0);
MODULE_PARM_DESC(i2c1_irq, "I2C1 IRQ number (valid parameters: 7, 9, 12)");

enum tqmx86_i2c1_resource_type {
	TQMX86_I2C1_IO,
	TQMX86_I2C1_IRQ,
};

static struct resource tqmx_i2c_soft_resources[] = {
	[TQMX86_I2C1_IO] = DEFINE_RES_IO(TQMX86_IOBASE_I2C, TQMX86_IOSIZE_I2C),
	/* Placeholder for IRQ resource */
	[TQMX86_I2C1_IRQ] = {},
};

static const struct resource tqmx_watchdog_resources[] = {
	DEFINE_RES_IO(TQMX86_IOBASE_WATCHDOG, TQMX86_IOSIZE_WATCHDOG),
};

enum tqmx86_gpio_resource_type {
	TQMX86_GPIO_IO,
	TQMX86_GPIO_IRQ,
};

static struct resource tqmx_gpio_resources[] = {
	[TQMX86_GPIO_IO] = DEFINE_RES_IO(TQMX86_IOBASE_GPIO, TQMX86_IOSIZE_GPIO),
	/* Placeholder for IRQ resource */
	[TQMX86_GPIO_IRQ] = {},
};

static struct i2c_board_info tqmx86_i2c_devices[] = {
	{
		/* 4K EEPROM at 0x50 */
		I2C_BOARD_INFO("24c32", 0x50),
	},
};

static struct ocores_i2c_platform_data ocores_platform_data = {
	.num_devices = ARRAY_SIZE(tqmx86_i2c_devices),
	.devices = tqmx86_i2c_devices,
};

static const struct mfd_cell tqmx86_i2c_soft_dev[] = {
	{
		.name = "ocores-i2c",
		.platform_data = &ocores_platform_data,
		.pdata_size = sizeof(ocores_platform_data),
		.resources = tqmx_i2c_soft_resources,
		.num_resources = ARRAY_SIZE(tqmx_i2c_soft_resources),
	},
};

static const struct mfd_cell tqmx86_devs[] = {
	{
		.name = "tqmx86-wdt",
		.resources = tqmx_watchdog_resources,
		.num_resources = ARRAY_SIZE(tqmx_watchdog_resources),
		.ignore_resource_conflicts = true,
	},
	{
		.name = "tqmx86-gpio",
		.resources = tqmx_gpio_resources,
		.num_resources = ARRAY_SIZE(tqmx_gpio_resources),
		.ignore_resource_conflicts = true,
	},
};

static const char *tqmx86_board_id_to_name(u8 board_id, u8 sauc)
{
	switch (board_id) {
	case TQMX86_REG_BOARD_ID_E38M:
		return "TQMxE38M";
	case TQMX86_REG_BOARD_ID_50UC:
		return "TQMx50UC";
	case TQMX86_REG_BOARD_ID_E38C:
		return "TQMxE38C";
	case TQMX86_REG_BOARD_ID_60EB:
		return "TQMx60EB";
	case TQMX86_REG_BOARD_ID_E39MS:
		return (sauc == 0xff) ? "TQMxE39M" : "TQMxE39S";
	case TQMX86_REG_BOARD_ID_E39C1:
		return "TQMxE39C1";
	case TQMX86_REG_BOARD_ID_E39C2:
		return "TQMxE39C2";
	case TQMX86_REG_BOARD_ID_70EB:
		return "TQMx70EB";
	case TQMX86_REG_BOARD_ID_80UC:
		return "TQMx80UC";
	case TQMX86_REG_BOARD_ID_120UC:
		return "TQMx120UC";
	case TQMX86_REG_BOARD_ID_110EB:
		return "TQMx110EB";
	case TQMX86_REG_BOARD_ID_E40M:
		return "TQMxE40M";
	case TQMX86_REG_BOARD_ID_E40S:
		return "TQMxE40S";
	case TQMX86_REG_BOARD_ID_E40C1:
		return "TQMxE40C1";
	case TQMX86_REG_BOARD_ID_E40C2:
		return "TQMxE40C2";
	case TQMX86_REG_BOARD_ID_130UC:
		return "TQMx130UC";
	case TQMX86_REG_BOARD_ID_E41S:
		return "TQMxE41S";
	default:
		return "Unknown";
	}
}

static int tqmx86_board_id_to_clk_rate(struct device *dev, u8 board_id)
{
	switch (board_id) {
	case TQMX86_REG_BOARD_ID_50UC:
	case TQMX86_REG_BOARD_ID_60EB:
	case TQMX86_REG_BOARD_ID_70EB:
	case TQMX86_REG_BOARD_ID_80UC:
	case TQMX86_REG_BOARD_ID_120UC:
	case TQMX86_REG_BOARD_ID_110EB:
	case TQMX86_REG_BOARD_ID_E40M:
	case TQMX86_REG_BOARD_ID_E40S:
	case TQMX86_REG_BOARD_ID_E40C1:
	case TQMX86_REG_BOARD_ID_E40C2:
	case TQMX86_REG_BOARD_ID_130UC:
	case TQMX86_REG_BOARD_ID_E41S:
		return 24000;
	case TQMX86_REG_BOARD_ID_E39MS:
	case TQMX86_REG_BOARD_ID_E39C1:
	case TQMX86_REG_BOARD_ID_E39C2:
		return 25000;
	case TQMX86_REG_BOARD_ID_E38M:
	case TQMX86_REG_BOARD_ID_E38C:
		return 33000;
	default:
		dev_warn(dev, "unknown board %d, assuming 24MHz LPC clock\n",
			 board_id);
		return 24000;
	}
}

static int tqmx86_setup_irq(struct device *dev, const char *label, u8 irq,
			    void __iomem *io_base, u8 reg_shift)
{
	u8 val, readback;
	int irq_cfg;

	switch (irq) {
	case 0:
		irq_cfg = TQMX86_REG_IO_EXT_INT_NONE;
		break;
	case 7:
		irq_cfg = TQMX86_REG_IO_EXT_INT_7;
		break;
	case 9:
		irq_cfg = TQMX86_REG_IO_EXT_INT_9;
		break;
	case 12:
		irq_cfg = TQMX86_REG_IO_EXT_INT_12;
		break;
	default:
		dev_err(dev, "invalid %s IRQ (%d)\n", label, irq);
		return -EINVAL;
	}

	val = ioread8(io_base + TQMX86_REG_IO_EXT_INT);
	val &= ~(TQMX86_REG_IO_EXT_INT_MASK << reg_shift);
	val |= (irq_cfg & TQMX86_REG_IO_EXT_INT_MASK) << reg_shift;

	iowrite8(val, io_base + TQMX86_REG_IO_EXT_INT);
	readback = ioread8(io_base + TQMX86_REG_IO_EXT_INT);
	if (readback != val) {
		dev_warn(dev, "%s interrupts not supported\n", label);
		return -EINVAL;
	}

	return 0;
}

static int tqmx86_probe(struct platform_device *pdev)
{
	u8 board_id, sauc, rev, i2c_det;
	struct device *dev = &pdev->dev;
	const char *board_name;
	void __iomem *io_base;
	int err;

	io_base = devm_ioport_map(dev, TQMX86_IOBASE, TQMX86_IOSIZE);
	if (!io_base)
		return -ENOMEM;

	board_id = ioread8(io_base + TQMX86_REG_BOARD_ID);
	sauc = ioread8(io_base + TQMX86_REG_SAUC);
	board_name = tqmx86_board_id_to_name(board_id, sauc);
	rev = ioread8(io_base + TQMX86_REG_BOARD_REV);

	dev_info(dev,
		 "Found %s - Board ID %d, PCB Revision %d, PLD Revision %d\n",
		 board_name, board_id, rev >> 4, rev & 0xf);

	/*
	 * The I2C_DETECT register is in the range assigned to the I2C driver
	 * later, so we don't extend TQMX86_IOSIZE. Use inb() for this one-off
	 * access instead of ioport_map + unmap.
	 */
	i2c_det = inb(TQMX86_REG_I2C_DETECT);

	if (gpio_irq) {
		err = tqmx86_setup_irq(dev, "GPIO", gpio_irq, io_base,
				       TQMX86_REG_IO_EXT_INT_GPIO_SHIFT);
		if (!err)
			tqmx_gpio_resources[TQMX86_GPIO_IRQ] = DEFINE_RES_IRQ(gpio_irq);
	}

	ocores_platform_data.clock_khz = tqmx86_board_id_to_clk_rate(dev, board_id);

	if (i2c_det == TQMX86_REG_I2C_DETECT_SOFT) {
		if (i2c1_irq) {
			err = tqmx86_setup_irq(dev, "I2C1", i2c1_irq, io_base,
					       TQMX86_REG_IO_EXT_INT_I2C1_SHIFT);
			if (!err)
				tqmx_i2c_soft_resources[TQMX86_I2C1_IRQ] = DEFINE_RES_IRQ(i2c1_irq);
		}

		err = devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE,
					   tqmx86_i2c_soft_dev,
					   ARRAY_SIZE(tqmx86_i2c_soft_dev),
					   NULL, 0, NULL);
		if (err)
			return err;
	}

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE,
				    tqmx86_devs,
				    ARRAY_SIZE(tqmx86_devs),
				    NULL, 0, NULL);
}

static int tqmx86_create_platform_device(const struct dmi_system_id *id)
{
	struct platform_device *pdev;
	int err;

	pdev = platform_device_alloc("tqmx86", -1);
	if (!pdev)
		return -ENOMEM;

	err = platform_device_add(pdev);
	if (err)
		platform_device_put(pdev);

	return err;
}

static const struct dmi_system_id tqmx86_dmi_table[] __initconst = {
	{
		.ident = "TQMX86",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TQ-Group"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TQMx"),
		},
		.callback = tqmx86_create_platform_device,
	},
	{
		.ident = "TQMX86",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TQ-Systems"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TQMx"),
		},
		.callback = tqmx86_create_platform_device,
	},
	{}
};
MODULE_DEVICE_TABLE(dmi, tqmx86_dmi_table);

static struct platform_driver tqmx86_driver = {
	.driver		= {
		.name	= "tqmx86",
	},
	.probe		= tqmx86_probe,
};

static int __init tqmx86_init(void)
{
	if (!dmi_check_system(tqmx86_dmi_table))
		return -ENODEV;

	return platform_driver_register(&tqmx86_driver);
}

module_init(tqmx86_init);

MODULE_DESCRIPTION("TQMx86 PLD Core Driver");
MODULE_AUTHOR("Andrew Lunn <andrew@lunn.ch>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:tqmx86");
