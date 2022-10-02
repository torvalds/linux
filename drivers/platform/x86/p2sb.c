// SPDX-License-Identifier: GPL-2.0
/*
 * Primary to Sideband (P2SB) bridge access support
 *
 * Copyright (c) 2017, 2021-2022 Intel Corporation.
 *
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *	    Jonathan Yong <jonathan.yong@intel.com>
 */

#include <linux/bits.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/platform_data/x86/p2sb.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

#define P2SBC			0xe0
#define P2SBC_HIDE		BIT(8)

static const struct x86_cpu_id p2sb_cpu_ids[] = {
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_GOLDMONT,	PCI_DEVFN(13, 0)),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_GOLDMONT_D,	PCI_DEVFN(31, 1)),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_SILVERMONT_D,	PCI_DEVFN(31, 1)),
	X86_MATCH_INTEL_FAM6_MODEL(KABYLAKE,		PCI_DEVFN(31, 1)),
	X86_MATCH_INTEL_FAM6_MODEL(KABYLAKE_L,		PCI_DEVFN(31, 1)),
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE,		PCI_DEVFN(31, 1)),
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE_L,		PCI_DEVFN(31, 1)),
	{}
};

static int p2sb_get_devfn(unsigned int *devfn)
{
	const struct x86_cpu_id *id;

	id = x86_match_cpu(p2sb_cpu_ids);
	if (!id)
		return -ENODEV;

	*devfn = (unsigned int)id->driver_data;
	return 0;
}

/* Copy resource from the first BAR of the device in question */
static int p2sb_read_bar0(struct pci_dev *pdev, struct resource *mem)
{
	struct resource *bar0 = &pdev->resource[0];

	/* Make sure we have no dangling pointers in the output */
	memset(mem, 0, sizeof(*mem));

	/*
	 * We copy only selected fields from the original resource.
	 * Because a PCI device will be removed soon, we may not use
	 * any allocated data, hence we may not copy any pointers.
	 */
	mem->start = bar0->start;
	mem->end = bar0->end;
	mem->flags = bar0->flags;
	mem->desc = bar0->desc;

	return 0;
}

static int p2sb_scan_and_read(struct pci_bus *bus, unsigned int devfn, struct resource *mem)
{
	struct pci_dev *pdev;
	int ret;

	pdev = pci_scan_single_device(bus, devfn);
	if (!pdev)
		return -ENODEV;

	ret = p2sb_read_bar0(pdev, mem);

	pci_stop_and_remove_bus_device(pdev);
	return ret;
}

/**
 * p2sb_bar - Get Primary to Sideband (P2SB) bridge device BAR
 * @bus: PCI bus to communicate with
 * @devfn: PCI slot and function to communicate with
 * @mem: memory resource to be filled in
 *
 * The BIOS prevents the P2SB device from being enumerated by the PCI
 * subsystem, so we need to unhide and hide it back to lookup the BAR.
 *
 * if @bus is NULL, the bus 0 in domain 0 will be used.
 * If @devfn is 0, it will be replaced by devfn of the P2SB device.
 *
 * Caller must provide a valid pointer to @mem.
 *
 * Locking is handled by pci_rescan_remove_lock mutex.
 *
 * Return:
 * 0 on success or appropriate errno value on error.
 */
int p2sb_bar(struct pci_bus *bus, unsigned int devfn, struct resource *mem)
{
	struct pci_dev *pdev_p2sb;
	unsigned int devfn_p2sb;
	u32 value = P2SBC_HIDE;
	int ret;

	/* Get devfn for P2SB device itself */
	ret = p2sb_get_devfn(&devfn_p2sb);
	if (ret)
		return ret;

	/* if @bus is NULL, use bus 0 in domain 0 */
	bus = bus ?: pci_find_bus(0, 0);

	/*
	 * Prevent concurrent PCI bus scan from seeing the P2SB device and
	 * removing via sysfs while it is temporarily exposed.
	 */
	pci_lock_rescan_remove();

	/* Unhide the P2SB device, if needed */
	pci_bus_read_config_dword(bus, devfn_p2sb, P2SBC, &value);
	if (value & P2SBC_HIDE)
		pci_bus_write_config_dword(bus, devfn_p2sb, P2SBC, 0);

	pdev_p2sb = pci_scan_single_device(bus, devfn_p2sb);
	if (devfn)
		ret = p2sb_scan_and_read(bus, devfn, mem);
	else
		ret = p2sb_read_bar0(pdev_p2sb, mem);
	pci_stop_and_remove_bus_device(pdev_p2sb);

	/* Hide the P2SB device, if it was hidden */
	if (value & P2SBC_HIDE)
		pci_bus_write_config_dword(bus, devfn_p2sb, P2SBC, P2SBC_HIDE);

	pci_unlock_rescan_remove();

	if (ret)
		return ret;

	if (mem->flags == 0)
		return -ENODEV;

	return 0;
}
EXPORT_SYMBOL_GPL(p2sb_bar);
