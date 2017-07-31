/*
 * PCI Express Precision Time Measurement
 * Copyright (c) 2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include "../pci.h"

static void pci_ptm_info(struct pci_dev *dev)
{
	char clock_desc[8];

	switch (dev->ptm_granularity) {
	case 0:
		snprintf(clock_desc, sizeof(clock_desc), "unknown");
		break;
	case 255:
		snprintf(clock_desc, sizeof(clock_desc), ">254ns");
		break;
	default:
		snprintf(clock_desc, sizeof(clock_desc), "%udns",
			 dev->ptm_granularity);
		break;
	}
	dev_info(&dev->dev, "PTM enabled%s, %s granularity\n",
		 dev->ptm_root ? " (root)" : "", clock_desc);
}

void pci_ptm_init(struct pci_dev *dev)
{
	int pos;
	u32 cap, ctrl;
	u8 local_clock;
	struct pci_dev *ups;

	if (!pci_is_pcie(dev))
		return;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_PTM);
	if (!pos)
		return;

	/*
	 * Enable PTM only on interior devices (root ports, switch ports,
	 * etc.) on the assumption that it causes no link traffic until an
	 * endpoint enables it.
	 */
	if ((pci_pcie_type(dev) == PCI_EXP_TYPE_ENDPOINT ||
	     pci_pcie_type(dev) == PCI_EXP_TYPE_RC_END))
		return;

	pci_read_config_dword(dev, pos + PCI_PTM_CAP, &cap);
	local_clock = (cap & PCI_PTM_GRANULARITY_MASK) >> 8;

	/*
	 * There's no point in enabling PTM unless it's enabled in the
	 * upstream device or this device can be a PTM Root itself.  Per
	 * the spec recommendation (PCIe r3.1, sec 7.32.3), select the
	 * furthest upstream Time Source as the PTM Root.
	 */
	ups = pci_upstream_bridge(dev);
	if (ups && ups->ptm_enabled) {
		ctrl = PCI_PTM_CTRL_ENABLE;
		if (ups->ptm_granularity == 0)
			dev->ptm_granularity = 0;
		else if (ups->ptm_granularity > local_clock)
			dev->ptm_granularity = ups->ptm_granularity;
	} else {
		if (cap & PCI_PTM_CAP_ROOT) {
			ctrl = PCI_PTM_CTRL_ENABLE | PCI_PTM_CTRL_ROOT;
			dev->ptm_root = 1;
			dev->ptm_granularity = local_clock;
		} else
			return;
	}

	ctrl |= dev->ptm_granularity << 8;
	pci_write_config_dword(dev, pos + PCI_PTM_CTRL, ctrl);
	dev->ptm_enabled = 1;

	pci_ptm_info(dev);
}

int pci_enable_ptm(struct pci_dev *dev, u8 *granularity)
{
	int pos;
	u32 cap, ctrl;
	struct pci_dev *ups;

	if (!pci_is_pcie(dev))
		return -EINVAL;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_PTM);
	if (!pos)
		return -EINVAL;

	pci_read_config_dword(dev, pos + PCI_PTM_CAP, &cap);
	if (!(cap & PCI_PTM_CAP_REQ))
		return -EINVAL;

	/*
	 * For a PCIe Endpoint, PTM is only useful if the endpoint can
	 * issue PTM requests to upstream devices that have PTM enabled.
	 *
	 * For Root Complex Integrated Endpoints, there is no upstream
	 * device, so there must be some implementation-specific way to
	 * associate the endpoint with a time source.
	 */
	if (pci_pcie_type(dev) == PCI_EXP_TYPE_ENDPOINT) {
		ups = pci_upstream_bridge(dev);
		if (!ups || !ups->ptm_enabled)
			return -EINVAL;

		dev->ptm_granularity = ups->ptm_granularity;
	} else if (pci_pcie_type(dev) == PCI_EXP_TYPE_RC_END) {
		dev->ptm_granularity = 0;
	} else
		return -EINVAL;

	ctrl = PCI_PTM_CTRL_ENABLE;
	ctrl |= dev->ptm_granularity << 8;
	pci_write_config_dword(dev, pos + PCI_PTM_CTRL, ctrl);
	dev->ptm_enabled = 1;

	pci_ptm_info(dev);

	if (granularity)
		*granularity = dev->ptm_granularity;
	return 0;
}
EXPORT_SYMBOL(pci_enable_ptm);
