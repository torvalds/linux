#ifndef __NVKM_FB_NV50_H__
#define __NVKM_FB_NV50_H__
#include "priv.h"

struct nv50_fb {
	struct nvkm_fb base;
	struct page *r100c08_page;
	dma_addr_t r100c08;
};

int  nv50_fb_ctor(struct nvkm_object *, struct nvkm_object *,
		  struct nvkm_oclass *, void *, u32,
		  struct nvkm_object **);
void nv50_fb_dtor(struct nvkm_object *);
int  nv50_fb_init(struct nvkm_object *);

struct nv50_fb_impl {
	struct nvkm_fb_impl base;
	u32 trap;
};

extern int nv50_fb_memtype[0x80];
#endif
