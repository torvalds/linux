/*
 * Copyright (C) 2014 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/pci.h>

#include <asm/mach-malta/malta-pm.h>

static struct pci_bus *pm_pci_bus;
static resource_size_t pm_io_offset;

int mips_pm_suspend(unsigned state)
{
	int spec_devid;
	u16 sts;

	if (!pm_pci_bus || !pm_io_offset)
		return -ENODEV;

	/* Ensure the power button status is clear */
	while (1) {
		sts = inw(pm_io_offset + PIIX4_FUNC3IO_PMSTS);
		if (!(sts & PIIX4_FUNC3IO_PMSTS_PWRBTN_STS))
			break;
		outw(sts, pm_io_offset + PIIX4_FUNC3IO_PMSTS);
	}

	/* Enable entry to suspend */
	outw(state | PIIX4_FUNC3IO_PMCNTRL_SUS_EN,
	     pm_io_offset + PIIX4_FUNC3IO_PMCNTRL);

	/* If the special cycle occurs too soon this doesn't work... */
	mdelay(10);

	/*
	 * The PIIX4 will enter the suspend state only after seeing a special
	 * cycle with the correct magic data on the PCI bus. Generate that
	 * cycle now.
	 */
	spec_devid = PCI_DEVID(0, PCI_DEVFN(0x1f, 0x7));
	pci_bus_write_config_dword(pm_pci_bus, spec_devid, 0,
				   PIIX4_SUSPEND_MAGIC);

	/* Give the system some time to power down */
	mdelay(1000);

	return 0;
}

static int __init malta_pm_setup(void)
{
	struct pci_dev *dev;
	int res, io_region = PCI_BRIDGE_RESOURCES;

	/* Find a reference to the PCI bus */
	pm_pci_bus = pci_find_next_bus(NULL);
	if (!pm_pci_bus) {
		pr_warn("malta-pm: failed to find reference to PCI bus\n");
		return -ENODEV;
	}

	/* Find the PIIX4 PM device */
	dev = pci_get_subsys(PCI_VENDOR_ID_INTEL,
			     PCI_DEVICE_ID_INTEL_82371AB_3, PCI_ANY_ID,
			     PCI_ANY_ID, NULL);
	if (!dev) {
		pr_warn("malta-pm: failed to find PIIX4 PM\n");
		return -ENODEV;
	}

	/* Request access to the PIIX4 PM IO registers */
	res = pci_request_region(dev, io_region, "PIIX4 PM IO registers");
	if (res) {
		pr_warn("malta-pm: failed to request PM IO registers (%d)\n",
			res);
		pci_dev_put(dev);
		return -ENODEV;
	}

	/* Find the offset to the PIIX4 PM IO registers */
	pm_io_offset = pci_resource_start(dev, io_region);

	pci_dev_put(dev);
	return 0;
}

late_initcall(malta_pm_setup);
