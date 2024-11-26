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
	struct intel_display *display = &i915->display;
	int i;

	/* Scratch space */
	if (DISPLAY_VER(i915) == 2 && IS_MOBILE(i915)) {
		for (i = 0; i < 7; i++) {
			display->restore.saveSWF0[i] = intel_de_read(display, SWF0(i915, i));
			display->restore.saveSWF1[i] = intel_de_read(display, SWF1(i915, i));
		}
		for (i = 0; i < 3; i++)
			display->restore.saveSWF3[i] = intel_de_read(display, SWF3(i915, i));
	} else if (DISPLAY_VER(i915) == 2) {
		for (i = 0; i < 7; i++)
			display->restore.saveSWF1[i] = intel_de_read(display, SWF1(i915, i));
	} else if (HAS_GMCH(i915)) {
		for (i = 0; i < 16; i++) {
			display->restore.saveSWF0[i] = intel_de_read(display, SWF0(i915, i));
			display->restore.saveSWF1[i] = intel_de_read(display, SWF1(i915, i));
		}
		for (i = 0; i < 3; i++)
			display->restore.saveSWF3[i] = intel_de_read(display, SWF3(i915, i));
	}
}

static void i9xx_display_restore_swf(struct drm_i915_private *i915)
{
	struct intel_display *display = &i915->display;
	int i;

	/* Scratch space */
	if (DISPLAY_VER(i915) == 2 && IS_MOBILE(i915)) {
		for (i = 0; i < 7; i++) {
			intel_de_write(display, SWF0(i915, i), display->restore.saveSWF0[i]);
			intel_de_write(display, SWF1(i915, i), display->restore.saveSWF1[i]);
		}
		for (i = 0; i < 3; i++)
			intel_de_write(display, SWF3(i915, i), display->restore.saveSWF3[i]);
	} else if (DISPLAY_VER(i915) == 2) {
		for (i = 0; i < 7; i++)
			intel_de_write(display, SWF1(i915, i), display->restore.saveSWF1[i]);
	} else if (HAS_GMCH(i915)) {
		for (i = 0; i < 16; i++) {
			intel_de_write(display, SWF0(i915, i), display->restore.saveSWF0[i]);
			intel_de_write(display, SWF1(i915, i), display->restore.saveSWF1[i]);
		}
		for (i = 0; i < 3; i++)
			intel_de_write(display, SWF3(i915, i), display->restore.saveSWF3[i]);
	}
}

void i9xx_display_sr_save(struct drm_i915_private *i915)
{
	struct intel_display *display = &i915->display;
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);

	if (!HAS_DISPLAY(i915))
		return;

	/* Display arbitration control */
	if (DISPLAY_VER(i915) <= 4)
		display->restore.saveDSPARB = intel_de_read(display, DSPARB(i915));

	if (DISPLAY_VER(i915) == 4)
		pci_read_config_word(pdev, GCDGMBUS, &display->restore.saveGCDGMBUS);

	i9xx_display_save_swf(i915);
}

void i9xx_display_sr_restore(struct drm_i915_private *i915)
{
	struct intel_display *display = &i915->display;
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);

	if (!HAS_DISPLAY(i915))
		return;

	i9xx_display_restore_swf(i915);

	if (DISPLAY_VER(i915) == 4)
		pci_write_config_word(pdev, GCDGMBUS, display->restore.saveGCDGMBUS);

	/* Display arbitration */
	if (DISPLAY_VER(i915) <= 4)
		intel_de_write(display, DSPARB(i915), display->restore.saveDSPARB);
}
