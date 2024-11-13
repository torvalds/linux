// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_reg.h"
#include "i9xx_display_sr.h"
#include "intel_de.h"
#include "intel_gmbus.h"
#include "intel_pci_config.h"

static void i9xx_display_save_swf(struct drm_i915_private *i915)
{
	int i;

	/* Scratch space */
	if (GRAPHICS_VER(i915) == 2 && IS_MOBILE(i915)) {
		for (i = 0; i < 7; i++) {
			i915->regfile.saveSWF0[i] = intel_de_read(i915, SWF0(i915, i));
			i915->regfile.saveSWF1[i] = intel_de_read(i915, SWF1(i915, i));
		}
		for (i = 0; i < 3; i++)
			i915->regfile.saveSWF3[i] = intel_de_read(i915, SWF3(i915, i));
	} else if (GRAPHICS_VER(i915) == 2) {
		for (i = 0; i < 7; i++)
			i915->regfile.saveSWF1[i] = intel_de_read(i915, SWF1(i915, i));
	} else if (HAS_GMCH(i915)) {
		for (i = 0; i < 16; i++) {
			i915->regfile.saveSWF0[i] = intel_de_read(i915, SWF0(i915, i));
			i915->regfile.saveSWF1[i] = intel_de_read(i915, SWF1(i915, i));
		}
		for (i = 0; i < 3; i++)
			i915->regfile.saveSWF3[i] = intel_de_read(i915, SWF3(i915, i));
	}
}

static void i9xx_display_restore_swf(struct drm_i915_private *i915)
{
	int i;

	/* Scratch space */
	if (GRAPHICS_VER(i915) == 2 && IS_MOBILE(i915)) {
		for (i = 0; i < 7; i++) {
			intel_de_write(i915, SWF0(i915, i), i915->regfile.saveSWF0[i]);
			intel_de_write(i915, SWF1(i915, i), i915->regfile.saveSWF1[i]);
		}
		for (i = 0; i < 3; i++)
			intel_de_write(i915, SWF3(i915, i), i915->regfile.saveSWF3[i]);
	} else if (GRAPHICS_VER(i915) == 2) {
		for (i = 0; i < 7; i++)
			intel_de_write(i915, SWF1(i915, i), i915->regfile.saveSWF1[i]);
	} else if (HAS_GMCH(i915)) {
		for (i = 0; i < 16; i++) {
			intel_de_write(i915, SWF0(i915, i), i915->regfile.saveSWF0[i]);
			intel_de_write(i915, SWF1(i915, i), i915->regfile.saveSWF1[i]);
		}
		for (i = 0; i < 3; i++)
			intel_de_write(i915, SWF3(i915, i), i915->regfile.saveSWF3[i]);
	}
}

void i9xx_display_sr_save(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);

	if (!HAS_DISPLAY(i915))
		return;

	/* Display arbitration control */
	if (GRAPHICS_VER(i915) <= 4)
		i915->regfile.saveDSPARB = intel_de_read(i915, DSPARB(i915));

	if (GRAPHICS_VER(i915) == 4)
		pci_read_config_word(pdev, GCDGMBUS, &i915->regfile.saveGCDGMBUS);

	i9xx_display_save_swf(i915);
}

void i9xx_display_sr_restore(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);

	if (!HAS_DISPLAY(i915))
		return;

	i9xx_display_restore_swf(i915);

	if (GRAPHICS_VER(i915) == 4)
		pci_write_config_word(pdev, GCDGMBUS, i915->regfile.saveGCDGMBUS);

	/* Display arbitration */
	if (GRAPHICS_VER(i915) <= 4)
		intel_de_write(i915, DSPARB(i915), i915->regfile.saveDSPARB);
}
