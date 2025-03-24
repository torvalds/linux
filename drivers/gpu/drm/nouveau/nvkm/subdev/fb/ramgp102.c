// SPDX-License-Identifier: MIT
#include "ram.h"

#include <subdev/bios.h>

static const struct nvkm_ram_func
gp102_ram = {
	.init = gp100_ram_init,
};

int
gp102_ram_new(struct nvkm_fb *fb, struct nvkm_ram **pram)
{
	enum nvkm_ram_type type = nvkm_fb_bios_memtype(fb->subdev.device->bios);
	const u32 rsvd_head = ( 256 * 1024); /* vga memory */
	const u32 rsvd_tail = (1024 * 1024); /* vbios etc */
	u64 size = fb->func->vidmem.size(fb);
	int ret;

	ret = nvkm_ram_new_(&gp102_ram, fb, type, size, pram);
	if (ret)
		return ret;

	nvkm_mm_fini(&(*pram)->vram);

	return nvkm_mm_init(&(*pram)->vram, NVKM_RAM_MM_NORMAL,
			    rsvd_head >> NVKM_RAM_MM_SHIFT,
			    (size - rsvd_head - rsvd_tail) >> NVKM_RAM_MM_SHIFT,
			    1);

}
