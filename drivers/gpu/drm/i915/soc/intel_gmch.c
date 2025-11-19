// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/pci.h>
#include <linux/vgaarb.h>

#include <drm/drm_print.h>
#include <drm/intel/i915_drm.h>

#include "../display/intel_display_core.h" /* FIXME */
#include "../display/intel_display_types.h" /* FIXME */

#include "intel_gmch.h"
#include "intel_pci_config.h"

static int intel_gmch_vga_set_state(struct intel_display *display, bool enable_decode)
{
	struct pci_dev *pdev = to_pci_dev(display->drm->dev);
	unsigned int reg = DISPLAY_VER(display) >= 6 ? SNB_GMCH_CTRL : INTEL_GMCH_CTRL;
	u16 gmch_ctrl;

	if (pci_bus_read_config_word(pdev->bus, PCI_DEVFN(0, 0), reg, &gmch_ctrl)) {
		drm_err(display->drm, "failed to read control word\n");
		return -EIO;
	}

	if (!!(gmch_ctrl & INTEL_GMCH_VGA_DISABLE) == !enable_decode)
		return 0;

	if (enable_decode)
		gmch_ctrl &= ~INTEL_GMCH_VGA_DISABLE;
	else
		gmch_ctrl |= INTEL_GMCH_VGA_DISABLE;

	if (pci_bus_write_config_word(pdev->bus, PCI_DEVFN(0, 0), reg, gmch_ctrl)) {
		drm_err(display->drm, "failed to write control word\n");
		return -EIO;
	}

	return 0;
}

unsigned int intel_gmch_vga_set_decode(struct pci_dev *pdev, bool enable_decode)
{
	struct intel_display *display = to_intel_display(pdev);

	intel_gmch_vga_set_state(display, enable_decode);

	if (enable_decode)
		return VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM |
		       VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
	else
		return VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
}
