// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/pci.h>
#include <linux/types.h>

#include "regs/xe_bars.h"
#include "xe_device_types.h"
#include "xe_module.h"
#include "xe_pci_rebar.h"
#include "xe_printk.h"

static void resize_bar(struct xe_device *xe, int resno, resource_size_t size)
{
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	int bar_size = pci_rebar_bytes_to_size(size);
	int ret;

	ret = pci_resize_resource(pdev, resno, bar_size, 0);
	if (ret) {
		xe_info(xe, "Failed to resize BAR%d to %dMiB (%pe). Consider enabling 'Resizable BAR' support in your BIOS\n",
			resno, 1 << bar_size, ERR_PTR(ret));
		return;
	}

	xe_info(xe, "BAR%d resized to %dMiB\n", resno, 1 << bar_size);
}

/*
 * xe_pci_rebar_resize - Resize the LMEMBAR
 * @xe: xe device instance
 *
 * If vram_bar_size module param is set, attempt to set to the requested size
 * else set to maximum possible size.
 */
void xe_pci_rebar_resize(struct xe_device *xe)
{
	int force_vram_bar_size = xe_modparam.force_vram_bar_size;
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	struct pci_bus *root = pdev->bus;
	resource_size_t current_size;
	resource_size_t rebar_size;
	struct resource *root_res;
	int max_size, i;
	u32 pci_cmd;

	/* gather some relevant info */
	current_size = pci_resource_len(pdev, LMEM_BAR);

	if (force_vram_bar_size < 0)
		return;

	/* set to a specific size? */
	if (force_vram_bar_size) {
		rebar_size = pci_rebar_bytes_to_size(force_vram_bar_size *
						     (resource_size_t)SZ_1M);

		if (!pci_rebar_size_supported(pdev, LMEM_BAR, rebar_size)) {
			xe_info(xe, "Requested size %lluMiB is not supported by rebar sizes: 0x%llx. Leaving default: %lluMiB\n",
				(u64)pci_rebar_size_to_bytes(rebar_size) >> ilog2(SZ_1M),
				pci_rebar_get_possible_sizes(pdev, LMEM_BAR),
				(u64)current_size >> ilog2(SZ_1M));
			return;
		}

		rebar_size = pci_rebar_size_to_bytes(rebar_size);
		if (rebar_size == current_size)
			return;
	} else {
		max_size = pci_rebar_get_max_size(pdev, LMEM_BAR);
		if (max_size < 0)
			return;
		rebar_size = pci_rebar_size_to_bytes(max_size);

		/* only resize if larger than current */
		if (rebar_size <= current_size)
			return;
	}

	xe_info(xe, "Attempting to resize bar from %lluMiB -> %lluMiB\n",
		(u64)current_size >> ilog2(SZ_1M), (u64)rebar_size >> ilog2(SZ_1M));

	while (root->parent)
		root = root->parent;

	pci_bus_for_each_resource(root, root_res, i) {
		if (root_res && root_res->flags & (IORESOURCE_MEM | IORESOURCE_MEM_64) &&
		    (u64)root_res->start > 0x100000000ul)
			break;
	}

	if (!root_res) {
		xe_info(xe, "Can't resize VRAM BAR - platform support is missing. Consider enabling 'Resizable BAR' support in your BIOS\n");
		return;
	}

	pci_read_config_dword(pdev, PCI_COMMAND, &pci_cmd);
	pci_write_config_dword(pdev, PCI_COMMAND, pci_cmd & ~PCI_COMMAND_MEMORY);

	resize_bar(xe, LMEM_BAR, rebar_size);

	pci_assign_unassigned_bus_resources(pdev->bus);
	pci_write_config_dword(pdev, PCI_COMMAND, pci_cmd);
}
