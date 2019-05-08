/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "bochs.h"

/* ---------------------------------------------------------------------- */

int bochs_mm_init(struct bochs_device *bochs)
{
	struct drm_vram_mm *vmm;

	vmm = drm_vram_helper_alloc_mm(bochs->dev, bochs->fb_base,
				       bochs->fb_size,
				       &drm_gem_vram_mm_funcs);
	if (IS_ERR(vmm))
		return PTR_ERR(vmm);

	return 0;
}

void bochs_mm_fini(struct bochs_device *bochs)
{
	if (!bochs->dev->vram_mm)
		return;

	drm_vram_helper_release_mm(bochs->dev);
}
