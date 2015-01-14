#ifndef __NVKM_FB_NV40_H__
#define __NVKM_FB_NV40_H__
#include "priv.h"

struct nv40_ram {
	struct nvkm_ram base;
	u32 ctrl;
	u32 coef;
};

int  nv40_ram_calc(struct nvkm_fb *, u32);
int  nv40_ram_prog(struct nvkm_fb *);
void nv40_ram_tidy(struct nvkm_fb *);
#endif
