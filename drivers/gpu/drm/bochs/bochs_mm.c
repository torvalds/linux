// SPDX-License-Identifier: GPL-2.0-or-later
/*
 */

#include "bochs.h"

/* ---------------------------------------------------------------------- */

int bochs_mm_init(struct bochs_device *bochs)
{
	struct drm_vram_mm *vmm;

	vmm = drm_vram_helper_alloc_mm(bochs->dev, bochs->fb_base,
				       bochs->fb_size,
				       &drm_gem_vram_mm_funcs);
	return PTR_ERR_OR_ZERO(vmm);
}

void bochs_mm_fini(struct bochs_device *bochs)
{
	if (!bochs->dev->vram_mm)
		return;

	drm_vram_helper_release_mm(bochs->dev);
}
