/*
 *  lpc_sch.c - LPC interface for Intel Poulsbo SCH
 *
 *  LPC bridge function of the Intel SCH contains many other
 *  functional units, such as Interrupt controllers, Timers,
 *  Power Management, System Management, GPIO, RTC, and LPC
 *  Configuration Registers.
 *
 *  Copyright (c) 2010 CompuLab Ltd
 *  Author: Denis Turischev <denis@compulab.co.il>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License 2 as published
 *  by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
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

static struct resource smbus_sch_resource = {
		.flags = IORESOURCE_IO,
};

static struct resource gpio_sch_resource = {
		.flags = IORESOURCE_IO,
};

static struct resource wdt_sch_resource = {
		.flags = IORESOURCE_IO,
};

static struct mfd_cell lpc_sch_cells[3];

static struct mfd_cell isch_smbus_cell = {
	.name = "isch_smbus",
	.num_resources = 1,
	.resources = &smbus_sch_resource,
	.ignore_resource_conflicts = true,
};

static struct mfd_cell sch_gpio_cell = {
	.name = "sch_gpio",
	.num_resources = 1,
	.resources = &gpio_sch_resource,
	.ignore_resource_conflicts = true,
};

static struct mfd_cell wdt_sch_cell = {
	.name = "ie6xx_wdt",
	.num_resources = 1,
	.resources = &wdt_sch_resource,
	.ignore_resource_conflicts = true,
};

static const struct pci_device_id lpc_sch_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_SCH_LPC) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ITC_LPC) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_CENTERTON_ILB) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, lpc_sch_ids);

static int lpc_sch_probe(struct pci_dev *dev,
				const struct pci_device_id *id)
{
	unsigned int base_addr_cfg;
	unsigned short base_addr;
	int i, cells = 0;
	int ret;

	pci_read_config_dword(dev, SMBASE, &base_addr_cfg);
	base_addr = 0;
	if (!(base_addr_cfg & (1 << 31)))
		dev_warn(&dev->dev, "Decode of the SMBus I/O range disabled\n");
	else
		base_addr = (unsigned short)base_addr_cfg;

	if (base_addr == 0) {
		dev_warn(&dev->dev, "I/O space for SMBus uninitialized\n");
	} else {
		lpc_sch_cells[cells++] = isch_smbus_cell;
		smbus_sch_resource.start = base_addr;
		smbus_sch_resource.end = base_addr + SMBUS_IO_SIZE - 1;
	}

	pci_read_config_dword(dev, GPIOBASE, &base_addr_cfg);
	base_addr = 0;
	if (!(base_addr_cfg & (1 << 31)))
		dev_warn(&dev->dev, "Decode of the GPIO I/O range disabled\n");
	else
		base_addr = (unsigned short)base_addr_cfg;

	if (base_addr == 0) {
		dev_warn(&dev->dev, "I/O space for GPIO uninitialized\n");
	} else {
		lpc_sch_cells[cells++] = sch_gpio_cell;
		gpio_sch_resource.start = base_addr;
		if (id->device == PCI_DEVICE_ID_INTEL_CENTERTON_ILB)
			gpio_sch_resource.end = base_addr + GPIO_IO_SIZE_CENTERTON - 1;
		else
			gpio_sch_resource.end = base_addr + GPIO_IO_SIZE - 1;
	}

	if (id->device == PCI_DEVICE_ID_INTEL_ITC_LPC
	    || id->device == PCI_DEVICE_ID_INTEL_CENTERTON_ILB) {
		pci_read_config_dword(dev, WDTBASE, &base_addr_cfg);
		base_addr = 0;
		if (!(base_addr_cfg & (1 << 31)))
			dev_warn(&dev->dev, "Decode of the WDT I/O range disabled\n");
		else
			base_addr = (unsigned short)base_addr_cfg;
		if (base_addr == 0)
			dev_warn(&dev->dev, "I/O space for WDT uninitialized\n");
		else {
			lpc_sch_cells[cells++] = wdt_sch_cell;
			wdt_sch_resource.start = base_addr;
			wdt_sch_resource.end = base_addr + WDT_IO_SIZE - 1;
		}
	}

	if (WARN_ON(cells > ARRAY_SIZE(lpc_sch_cells))) {
		dev_err(&dev->dev, "Cell count exceeds array size");
		return -ENODEV;
	}

	if (cells == 0) {
		dev_err(&dev->dev, "All decode registers disabled.\n");
		return -ENODEV;
	}

	for (i = 0; i < cells; i++)
		lpc_sch_cells[i].id = id->device;

	ret = mfd_add_devices(&dev->dev, 0, lpc_sch_cells, cells, NULL, 0, NULL);
	if (ret)
		mfd_remove_devices(&dev->dev);

	return ret;
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
