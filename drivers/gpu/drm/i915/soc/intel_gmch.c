// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/pci.h>
#include <linux/pnp.h>
#include <linux/vgaarb.h>

#include <drm/drm_managed.h>
#include <drm/i915_drm.h>

#include "i915_drv.h"
#include "intel_gmch.h"
#include "intel_pci_config.h"

static void intel_gmch_bridge_release(struct drm_device *dev, void *bridge)
{
	pci_dev_put(bridge);
}

int intel_gmch_bridge_setup(struct drm_i915_private *i915)
{
	int domain = pci_domain_nr(to_pci_dev(i915->drm.dev)->bus);

	i915->gmch.pdev = pci_get_domain_bus_and_slot(domain, 0, PCI_DEVFN(0, 0));
	if (!i915->gmch.pdev) {
		drm_err(&i915->drm, "bridge device not found\n");
		return -EIO;
	}

	return drmm_add_action_or_reset(&i915->drm, intel_gmch_bridge_release,
					i915->gmch.pdev);
}

static int mchbar_reg(struct drm_i915_private *i915)
{
	return GRAPHICS_VER(i915) >= 4 ? MCHBAR_I965 : MCHBAR_I915;
}

/* Allocate space for the MCH regs if needed, return nonzero on error */
static int
intel_alloc_mchbar_resource(struct drm_i915_private *i915)
{
	u32 temp_lo, temp_hi = 0;
	u64 mchbar_addr;
	int ret;

	if (GRAPHICS_VER(i915) >= 4)
		pci_read_config_dword(i915->gmch.pdev, mchbar_reg(i915) + 4, &temp_hi);
	pci_read_config_dword(i915->gmch.pdev, mchbar_reg(i915), &temp_lo);
	mchbar_addr = ((u64)temp_hi << 32) | temp_lo;

	/* If ACPI doesn't have it, assume we need to allocate it ourselves */
	if (IS_ENABLED(CONFIG_PNP) && mchbar_addr &&
	    pnp_range_reserved(mchbar_addr, mchbar_addr + MCHBAR_SIZE))
		return 0;

	/* Get some space for it */
	i915->gmch.mch_res.name = "i915 MCHBAR";
	i915->gmch.mch_res.flags = IORESOURCE_MEM;
	ret = pci_bus_alloc_resource(i915->gmch.pdev->bus,
				     &i915->gmch.mch_res,
				     MCHBAR_SIZE, MCHBAR_SIZE,
				     PCIBIOS_MIN_MEM,
				     0, pcibios_align_resource,
				     i915->gmch.pdev);
	if (ret) {
		drm_dbg(&i915->drm, "failed bus alloc: %d\n", ret);
		i915->gmch.mch_res.start = 0;
		return ret;
	}

	if (GRAPHICS_VER(i915) >= 4)
		pci_write_config_dword(i915->gmch.pdev, mchbar_reg(i915) + 4,
				       upper_32_bits(i915->gmch.mch_res.start));

	pci_write_config_dword(i915->gmch.pdev, mchbar_reg(i915),
			       lower_32_bits(i915->gmch.mch_res.start));
	return 0;
}

/* Setup MCHBAR if possible, return true if we should disable it again */
void intel_gmch_bar_setup(struct drm_i915_private *i915)
{
	u32 temp;
	bool enabled;

	if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
		return;

	i915->gmch.mchbar_need_disable = false;

	if (IS_I915G(i915) || IS_I915GM(i915)) {
		pci_read_config_dword(i915->gmch.pdev, DEVEN, &temp);
		enabled = !!(temp & DEVEN_MCHBAR_EN);
	} else {
		pci_read_config_dword(i915->gmch.pdev, mchbar_reg(i915), &temp);
		enabled = temp & 1;
	}

	/* If it's already enabled, don't have to do anything */
	if (enabled)
		return;

	if (intel_alloc_mchbar_resource(i915))
		return;

	i915->gmch.mchbar_need_disable = true;

	/* Space is allocated or reserved, so enable it. */
	if (IS_I915G(i915) || IS_I915GM(i915)) {
		pci_write_config_dword(i915->gmch.pdev, DEVEN,
				       temp | DEVEN_MCHBAR_EN);
	} else {
		pci_read_config_dword(i915->gmch.pdev, mchbar_reg(i915), &temp);
		pci_write_config_dword(i915->gmch.pdev, mchbar_reg(i915), temp | 1);
	}
}

void intel_gmch_bar_teardown(struct drm_i915_private *i915)
{
	if (i915->gmch.mchbar_need_disable) {
		if (IS_I915G(i915) || IS_I915GM(i915)) {
			u32 deven_val;

			pci_read_config_dword(i915->gmch.pdev, DEVEN,
					      &deven_val);
			deven_val &= ~DEVEN_MCHBAR_EN;
			pci_write_config_dword(i915->gmch.pdev, DEVEN,
					       deven_val);
		} else {
			u32 mchbar_val;

			pci_read_config_dword(i915->gmch.pdev, mchbar_reg(i915),
					      &mchbar_val);
			mchbar_val &= ~1;
			pci_write_config_dword(i915->gmch.pdev, mchbar_reg(i915),
					       mchbar_val);
		}
	}

	if (i915->gmch.mch_res.start)
		release_resource(&i915->gmch.mch_res);
}

int intel_gmch_vga_set_state(struct drm_i915_private *i915, bool enable_decode)
{
	unsigned int reg = DISPLAY_VER(i915) >= 6 ? SNB_GMCH_CTRL : INTEL_GMCH_CTRL;
	u16 gmch_ctrl;

	if (pci_read_config_word(i915->gmch.pdev, reg, &gmch_ctrl)) {
		drm_err(&i915->drm, "failed to read control word\n");
		return -EIO;
	}

	if (!!(gmch_ctrl & INTEL_GMCH_VGA_DISABLE) == !enable_decode)
		return 0;

	if (enable_decode)
		gmch_ctrl &= ~INTEL_GMCH_VGA_DISABLE;
	else
		gmch_ctrl |= INTEL_GMCH_VGA_DISABLE;

	if (pci_write_config_word(i915->gmch.pdev, reg, gmch_ctrl)) {
		drm_err(&i915->drm, "failed to write control word\n");
		return -EIO;
	}

	return 0;
}

unsigned int intel_gmch_vga_set_decode(struct pci_dev *pdev, bool enable_decode)
{
	struct drm_i915_private *i915 = pdev_to_i915(pdev);

	intel_gmch_vga_set_state(i915, enable_decode);

	if (enable_decode)
		return VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM |
		       VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
	else
		return VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
}
