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

#define nv50_ram_create(p,e,o,d)                                               \
	nv50_ram_create_((p), (e), (o), sizeof(**d), (void **)d)
int  nv50_ram_create_(struct nouveau_object *, struct nouveau_object *,
		      struct nouveau_oclass *, int, void **);
int  nv50_ram_get(struct nouveau_fb *, u64 size, u32 align, u32 ncmin,
		  u32 memtype, struct nouveau_mem **);
void nv50_ram_put(struct nouveau_fb *, struct nouveau_mem **);
void __nv50_ram_put(struct nouveau_fb *, struct nouveau_mem *);
extern int nv50_fb_memtype[0x80];

#endif
