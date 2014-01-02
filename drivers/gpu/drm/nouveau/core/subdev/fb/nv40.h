#ifndef __NVKM_FB_NV40_H__
#define __NVKM_FB_NV40_H__

#include "priv.h"

struct nv40_ram {
	struct nouveau_ram base;
	u32 ctrl;
	u32 coef;
};


int  nv40_ram_calc(struct nouveau_fb *, u32);
int  nv40_ram_prog(struct nouveau_fb *);
void nv40_ram_tidy(struct nouveau_fb *);

#endif
