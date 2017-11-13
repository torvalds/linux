/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NV40_FB_RAM_H__
#define __NV40_FB_RAM_H__
#define nv40_ram(p) container_of((p), struct nv40_ram, base)
#include "ram.h"

struct nv40_ram {
	struct nvkm_ram base;
	u32 ctrl;
	u32 coef;
};

int nv40_ram_new_(struct nvkm_fb *fb, enum nvkm_ram_type, u64, u32,
		  struct nvkm_ram **);
#endif
