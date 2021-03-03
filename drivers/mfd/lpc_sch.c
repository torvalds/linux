// SPDX-License-Identifier: GPL-2.0-only
/*
 *  lpc_sch.c - LPC interface for Intel Poulsbo SCH
 *
 *  LPC bridge function of the Intel SCH contains many other
 *  functional units, such as Interrupt controllers, Timers,
 *  Power Management, System Management, GPIO, RTC, and LPC
 *  Configuration Registers.
 *
 *  Copyright (c) 2010 CompuLab Ltd
 *  Copyright (c) 2014 Intel Corp.
 *  Author: Denis Turischev <denis@compulab.co.il>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include <linux/mfd/core.h>

#define SMBASE		0x40
#define SMBUS_IO_SIZE	64

#define GPIOBASE	0x44
#define GPIO_IO_SIZE	64
#define GPIO_IO_SIZE_CENTERTON	128

#define WDTBASE		0x84
#define WDT_IO_SIZE	64

enum sch_chipsets {
	LPC_SCH = 0,		/* Intel Poulsbo SCH */
	LPC_ITC,		/* Intel Tunnel Creek */
	LPC_CENTERTON,		/* Intel Centerton */
	LPC_QUARK_X1000,	/* Intel Quark X1000 */
};

struct lpc_sch_info {
	unsigned int io_size_smbus;
	unsigned int io_size_gpio;
	unsigned int io_size_wdt;
};

static struct lpc_sch_info sch_chipset_info[] = {
	[LPC_SCH] = {
		.io_size_smbus = SMBUS_IO_SIZE,
		.io_size_gpio = GPIO_IO_SIZE,
	},
	[LPC_ITC] = {
		.io_size_smbus = SMBUS_IO_SIZE,
		.io_size_gpio = GPIO_IO_SIZE,
		.io_size_wdt = WDT_IO_SIZE,
	},
	[LPC_CENTERTON] = {
		.io_size_smbus = SMBUS_IO_SIZE,
		.io_size_gpio = GPIO_IO_SIZE_CENTERTON,
		.io_size_wdt = WDT_IO_SIZE,
	},
	[LPC_QUARK_X1000] = {
		.io_size_gpio = GPIO_IO_SIZE,
		.io_size_wdt = WDT_IO_SIZE,
	},
};

static const struct pci_device_id lpc_sch_ids[] = {
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_SCH_LPC), LPC_SCH },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ITC_LPC), LPC_ITC },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_CENTERTON_ILB), LPC_CENTERTON },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_QUARK_X1000_ILB), LPC_QUARK_X1000 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, lpc_sch_ids);

#define LPC_NO_RESOURCE		1
#define LPC_SKIP_RESOURCE	2

static int lpc_sch_get_io(struct pci_dev *pdev, int where, const char *name,
			  struct resource *res, int size)
{
	unsigned int base_addr_cfg;
	unsigned short base_addr;

	if (size == 0)
		return LPC_NO_RESOURCE;

	pci_read_config_dword(pdev, where, &base_addr_cfg);
	base_addr = 0;
	if (!(base_addr_cfg & (1 << 31)))
		dev_warn(&pdev->dev, "Decode of the %s I/O range disabled\n",
			 name);
	else
		base_addr = (unsigned short)base_addr_cfg;

	if (base_addr == 0) {
		dev_warn(&pdev->dev, "I/O space for %s uninitialized\n", name);
		return LPC_SKIP_RESOURCE;
	}

	res->start = base_addr;
	res->end = base_addr + size - 1;
	res->flags = IORESOURCE_IO;

	return 0;
}

static int lpc_sch_populate_cell(struct pci_dev *pdev, int where,
				 const char *name, int size, int id,
				 struct mfd_cell *cell)
{
	struct resource *res;
	int ret;

	res = devm_kzalloc(&pdev->dev, sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	ret = lpc_sch_get_io(pdev, where, name, res, size);
	if (ret)
		return ret;

	memset(cell, 0, sizeof(*cell));

	cell->name = name;
	cell->resources = res;
	cell->num_resources = 1;
	cell->ignore_resource_conflicts = true;
	cell->id = id;

	return 0;
}

static int lpc_sch_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct mfd_cell lpc_sch_cells[3];
	struct lpc_sch_info *info = &sch_chipset_info[id->driver_data];
	unsigned int cells = 0;
	int ret;

	ret = lpc_sch_populate_cell(dev, SMBASE, "isch_smbus",
				    info->io_size_smbus,
				    id->device, &lpc_sch_cells[cells]);
	if (ret < 0)
		return ret;
	if (ret == 0)
		cells++;

	ret = lpc_sch_populate_cell(dev, GPIOBASE, "sch_gpio",
				    info->io_size_gpio,
				    id->device, &lpc_sch_cells[cells]);
	if (ret < 0)
		return ret;
	if (ret == 0)
		cells++;

	ret = lpc_sch_populate_cell(dev, WDTBASE, "ie6xx_wdt",
				    info->io_size_wdt,
				    id->device, &lpc_sch_cells[cells]);
	if (ret < 0)
		return ret;
	if (ret == 0)
		cells++;

	if (cells == 0) {
		dev_err(&dev->dev, "All decode registers disabled.\n");
		return -ENODEV;
	}

	return mfd_add_devices(&dev->dev, 0, lpc_sch_cells, cells, NULL, 0, NULL);
}

static void lpc_sch_remove(struct pci_dev *dev)
{
	mfd_remove_devices(&dev->dev);
}

static struct pci_driver lpc_sch_driver = {
	.name		= "lpc_sch",
	.id_table	= lpc_sch_ids,
	.probe		= lpc_sch_probe,
	.remove		= lpc_sch_remove,
};

module_pci_driver(lpc_sch_driver);

MODULE_AUTHOR("Denis Turischev <denis@compulab.co.il>");
MODULE_DESCRIPTION("LPC interface for Intel Poulsbo SCH");
MODULE_LICENSE("GPL");
