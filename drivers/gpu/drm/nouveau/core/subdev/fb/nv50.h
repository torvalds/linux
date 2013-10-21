#ifndef __NVKM_FB_NV50_H__
#define __NVKM_FB_NV50_H__

#include "priv.h"

struct nv50_fb_priv {
	struct nouveau_fb base;
	struct page *r100c08_page;
	dma_addr_t r100c08;
};

int  nv50_fb_ctor(struct nouveau_object *, struct nouveau_object *,
		  struct nouveau_oclass *, void *, u32,
		  struct nouveau_object **);
void nv50_fb_dtor(struct nouveau_object *);
int  nv50_fb_init(struct nouveau_object *);

struct nv50_fb_impl {
	struct nouveau_fb_impl base;
	u32 trap;
};

void __nv50_ram_put(struct nouveau_fb *, struct nouveau_mem *);
extern int nv50_fb_memtype[0x80];

#endif
