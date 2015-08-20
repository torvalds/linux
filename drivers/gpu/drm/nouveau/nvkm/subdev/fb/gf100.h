#ifndef __NVKM_RAM_NVC0_H__
#define __NVKM_RAM_NVC0_H__
#include "priv.h"

struct gf100_fb {
	struct nvkm_fb base;
	struct page *r100c10_page;
	dma_addr_t r100c10;
};

int  gf100_fb_ctor(struct nvkm_object *, struct nvkm_object *,
		  struct nvkm_oclass *, void *, u32,
		  struct nvkm_object **);
void gf100_fb_dtor(struct nvkm_object *);
int  gf100_fb_init(struct nvkm_object *);
bool gf100_fb_memtype_valid(struct nvkm_fb *, u32);
#endif
