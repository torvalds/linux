/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm.h>

static struct pci_dev *pm_dev;
static resource_size_t io_offset;

enum piix4_pm_io_reg {
	PIIX4_FUNC3IO_PMSTS			= 0x00,
#define PIIX4_FUNC3IO_PMSTS_PWRBTN_STS		BIT(8)
	PIIX4_FUNC3IO_PMCNTRL			= 0x04,
#define PIIX4_FUNC3IO_PMCNTRL_SUS_EN		BIT(13)
#define PIIX4_FUNC3IO_PMCNTRL_SUS_TYP_SOFF	(0x0 << 10)
};

#define PIIX4_SUSPEND_MAGIC			0x00120002

static const int piix4_pm_io_region = PCI_BRIDGE_RESOURCES;

static void piix4_poweroff(void)
{
	int spec_devid;
	u16 sts;

	/* Ensure the power button status is clear */
	while (1) {
		sts = inw(io_offset + PIIX4_FUNC3IO_PMSTS);
		if (!(sts & PIIX4_FUNC3IO_PMSTS_PWRBTN_STS))
			break;
		outw(sts, io_offset + PIIX4_FUNC3IO_PMSTS);
	}

	/* Enable entry to suspend */
	outw(PIIX4_FUNC3IO_PMCNTRL_SUS_TYP_SOFF | PIIX4_FUNC3IO_PMCNTRL_SUS_EN,
	     io_offset + PIIX4_FUNC3IO_PMCNTRL);

	/* If the special cycle occurs too soon this doesn't work... */
	mdelay(10);

	/*
	 * The PIIX4 will enter the suspend state only after seeing a special
	 * cycle with the correct magic data on the PCI bus. Generate that
	 * cycle now.
	 */
	spec_devid = PCI_DEVID(0, PCI_DEVFN(0x1f, 0x7));
	pci_bus_write_config_dword(pm_dev->bus, spec_devid, 0,
				   PIIX4_SUSPEND_MAGIC);

	/* Give the system some time to power down, then error */
	mdelay(1000);
	pr_emerg("Unable to poweroff system\n");
}

static int piix4_poweroff_probe(struct pci_dev *dev,
				const struct pci_device_id *id)
{
	int res;

	if (pm_dev)
		return -EINVAL;

	/* Request access to the PIIX4 PM IO registers */
	res = pci_request_region(dev, piix4_pm_io_region,
				 "PIIX4 PM IO registers");
	if (res) {
		dev_err(&dev->dev, "failed to request PM IO registers: %d\n",
			res);
		return res;
	}

	pm_dev = dev;
	io_offset = pci_resource_start(dev, piix4_pm_io_region);
	pm_power_off = piix4_poweroff;

	return 0;
}

static void piix4_poweroff_remove(struct pci_dev *dev)
{
	if (pm_power_off == piix4_poweroff)
		pm_power_off = NULL;

	pci_release_region(dev, piix4_pm_io_region);
	pm_dev = NULL;
}

static const struct pci_device_id piix4_poweroff_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_3) },
	{ 0 },
};

static struct pci_driver piix4_poweroff_driver = {
	.name		= "piix4-poweroff",
	.id_table	= piix4_poweroff_ids,
	.probe		= piix4_poweroff_probe,
	.remove		= piix4_poweroff_remove,
};

module_pci_driver(piix4_poweroff_driver);
MODULE_AUTHOR("Paul Burton <paul.burton@imgtec.com>");
MODULE_LICENSE("GPL");
