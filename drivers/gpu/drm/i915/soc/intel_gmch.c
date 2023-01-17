// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/pci.h>
#include <linux/pnp.h>

#include <drm/drm_managed.h>

#include "i915_drv.h"
#include "intel_gmch.h"
#include "intel_pci_config.h"

static void intel_gmch_bridge_release(struct drm_device *dev, void *bridge)
{
	pci_dev_put(bridge);
}

int intel_gmch_bridge_setup(struct drm_i915_private *dev_priv)
{
	int domain = pci_domain_nr(to_pci_dev(dev_priv->drm.dev)->bus);

	dev_priv->gmch.pdev =
		pci_get_domain_bus_and_slot(domain, 0, PCI_DEVFN(0, 0));
	if (!dev_priv->gmch.pdev) {
		drm_err(&dev_priv->drm, "bridge device not found\n");
		return -EIO;
	}

	return drmm_add_action_or_reset(&dev_priv->drm, intel_gmch_bridge_release,
					dev_priv->gmch.pdev);
}

/* Allocate space for the MCH regs if needed, return nonzero on error */
static int
intel_alloc_mchbar_resource(struct drm_i915_private *dev_priv)
{
	int reg = GRAPHICS_VER(dev_priv) >= 4 ? MCHBAR_I965 : MCHBAR_I915;
	u32 temp_lo, temp_hi = 0;
	u64 mchbar_addr;
	int ret;

	if (GRAPHICS_VER(dev_priv) >= 4)
		pci_read_config_dword(dev_priv->gmch.pdev, reg + 4, &temp_hi);
	pci_read_config_dword(dev_priv->gmch.pdev, reg, &temp_lo);
	mchbar_addr = ((u64)temp_hi << 32) | temp_lo;

	/* If ACPI doesn't have it, assume we need to allocate it ourselves */
#ifdef CONFIG_PNP
	if (mchbar_addr &&
	    pnp_range_reserved(mchbar_addr, mchbar_addr + MCHBAR_SIZE))
		return 0;
#endif

	/* Get some space for it */
	dev_priv->gmch.mch_res.name = "i915 MCHBAR";
	dev_priv->gmch.mch_res.flags = IORESOURCE_MEM;
	ret = pci_bus_alloc_resource(dev_priv->gmch.pdev->bus,
				     &dev_priv->gmch.mch_res,
				     MCHBAR_SIZE, MCHBAR_SIZE,
				     PCIBIOS_MIN_MEM,
				     0, pcibios_align_resource,
				     dev_priv->gmch.pdev);
	if (ret) {
		drm_dbg(&dev_priv->drm, "failed bus alloc: %d\n", ret);
		dev_priv->gmch.mch_res.start = 0;
		return ret;
	}

	if (GRAPHICS_VER(dev_priv) >= 4)
		pci_write_config_dword(dev_priv->gmch.pdev, reg + 4,
				       upper_32_bits(dev_priv->gmch.mch_res.start));

	pci_write_config_dword(dev_priv->gmch.pdev, reg,
			       lower_32_bits(dev_priv->gmch.mch_res.start));
	return 0;
}

/* Setup MCHBAR if possible, return true if we should disable it again */
void intel_gmch_bar_setup(struct drm_i915_private *dev_priv)
{
	int mchbar_reg = GRAPHICS_VER(dev_priv) >= 4 ? MCHBAR_I965 : MCHBAR_I915;
	u32 temp;
	bool enabled;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		return;

	dev_priv->gmch.mchbar_need_disable = false;

	if (IS_I915G(dev_priv) || IS_I915GM(dev_priv)) {
		pci_read_config_dword(dev_priv->gmch.pdev, DEVEN, &temp);
		enabled = !!(temp & DEVEN_MCHBAR_EN);
	} else {
		pci_read_config_dword(dev_priv->gmch.pdev, mchbar_reg, &temp);
		enabled = temp & 1;
	}

	/* If it's already enabled, don't have to do anything */
	if (enabled)
		return;

	if (intel_alloc_mchbar_resource(dev_priv))
		return;

	dev_priv->gmch.mchbar_need_disable = true;

	/* Space is allocated or reserved, so enable it. */
	if (IS_I915G(dev_priv) || IS_I915GM(dev_priv)) {
		pci_write_config_dword(dev_priv->gmch.pdev, DEVEN,
				       temp | DEVEN_MCHBAR_EN);
	} else {
		pci_read_config_dword(dev_priv->gmch.pdev, mchbar_reg, &temp);
		pci_write_config_dword(dev_priv->gmch.pdev, mchbar_reg, temp | 1);
	}
}

void intel_gmch_bar_teardown(struct drm_i915_private *dev_priv)
{
	int mchbar_reg = GRAPHICS_VER(dev_priv) >= 4 ? MCHBAR_I965 : MCHBAR_I915;

	if (dev_priv->gmch.mchbar_need_disable) {
		if (IS_I915G(dev_priv) || IS_I915GM(dev_priv)) {
			u32 deven_val;

			pci_read_config_dword(dev_priv->gmch.pdev, DEVEN,
					      &deven_val);
			deven_val &= ~DEVEN_MCHBAR_EN;
			pci_write_config_dword(dev_priv->gmch.pdev, DEVEN,
					       deven_val);
		} else {
			u32 mchbar_val;

			pci_read_config_dword(dev_priv->gmch.pdev, mchbar_reg,
					      &mchbar_val);
			mchbar_val &= ~1;
			pci_write_config_dword(dev_priv->gmch.pdev, mchbar_reg,
					       mchbar_val);
		}
	}

	if (dev_priv->gmch.mch_res.start)
		release_resource(&dev_priv->gmch.mch_res);
}
