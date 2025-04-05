// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <drm/drm_device.h>

#include "i915_reg.h"
#include "i9xx_display_sr.h"
#include "i9xx_wm_regs.h"
#include "intel_de.h"
#include "intel_gmbus.h"
#include "intel_pci_config.h"

static void i9xx_display_save_swf(struct intel_display *display)
{
	int i;

	/* Scratch space */
	if (DISPLAY_VER(display) == 2 && display->platform.mobile) {
		for (i = 0; i < 7; i++) {
			display->restore.saveSWF0[i] = intel_de_read(display, SWF0(display, i));
			display->restore.saveSWF1[i] = intel_de_read(display, SWF1(display, i));
		}
		for (i = 0; i < 3; i++)
			display->restore.saveSWF3[i] = intel_de_read(display, SWF3(display, i));
	} else if (DISPLAY_VER(display) == 2) {
		for (i = 0; i < 7; i++)
			display->restore.saveSWF1[i] = intel_de_read(display, SWF1(display, i));
	} else if (HAS_GMCH(display)) {
		for (i = 0; i < 16; i++) {
			display->restore.saveSWF0[i] = intel_de_read(display, SWF0(display, i));
			display->restore.saveSWF1[i] = intel_de_read(display, SWF1(display, i));
		}
		for (i = 0; i < 3; i++)
			display->restore.saveSWF3[i] = intel_de_read(display, SWF3(display, i));
	}
}

static void i9xx_display_restore_swf(struct intel_display *display)
{
	int i;

	/* Scratch space */
	if (DISPLAY_VER(display) == 2 && display->platform.mobile) {
		for (i = 0; i < 7; i++) {
			intel_de_write(display, SWF0(display, i), display->restore.saveSWF0[i]);
			intel_de_write(display, SWF1(display, i), display->restore.saveSWF1[i]);
		}
		for (i = 0; i < 3; i++)
			intel_de_write(display, SWF3(display, i), display->restore.saveSWF3[i]);
	} else if (DISPLAY_VER(display) == 2) {
		for (i = 0; i < 7; i++)
			intel_de_write(display, SWF1(display, i), display->restore.saveSWF1[i]);
	} else if (HAS_GMCH(display)) {
		for (i = 0; i < 16; i++) {
			intel_de_write(display, SWF0(display, i), display->restore.saveSWF0[i]);
			intel_de_write(display, SWF1(display, i), display->restore.saveSWF1[i]);
		}
		for (i = 0; i < 3; i++)
			intel_de_write(display, SWF3(display, i), display->restore.saveSWF3[i]);
	}
}

void i9xx_display_sr_save(struct intel_display *display)
{
	struct pci_dev *pdev = to_pci_dev(display->drm->dev);

	if (!HAS_DISPLAY(display))
		return;

	/* Display arbitration control */
	if (DISPLAY_VER(display) <= 4)
		display->restore.saveDSPARB = intel_de_read(display, DSPARB(display));

	if (DISPLAY_VER(display) == 4)
		pci_read_config_word(pdev, GCDGMBUS, &display->restore.saveGCDGMBUS);

	i9xx_display_save_swf(display);
}

void i9xx_display_sr_restore(struct intel_display *display)
{
	struct pci_dev *pdev = to_pci_dev(display->drm->dev);

	if (!HAS_DISPLAY(display))
		return;

	i9xx_display_restore_swf(display);

	if (DISPLAY_VER(display) == 4)
		pci_write_config_word(pdev, GCDGMBUS, display->restore.saveGCDGMBUS);

	/* Display arbitration */
	if (DISPLAY_VER(display) <= 4)
		intel_de_write(display, DSPARB(display), display->restore.saveDSPARB);
}
