#ifndef __NVKM_RAM_NVC0_H__
#define __NVKM_RAM_NVC0_H__

#include "priv.h"
#include "nv50.h"

struct nvc0_fb_priv {
	struct nouveau_fb base;
	struct page *r100c10_page;
	dma_addr_t r100c10;
};

int  nvc0_fb_ctor(struct nouveau_object *, struct nouveau_object *,
		  struct nouveau_oclass *, void *, u32,
		  struct nouveau_object **);
void nvc0_fb_dtor(struct nouveau_object *);
int  nvc0_fb_init(struct nouveau_object *);
bool nvc0_fb_memtype_valid(struct nouveau_fb *, u32);


#define nvc0_ram_create(p,e,o,d)                                               \
	nvc0_ram_create_((p), (e), (o), sizeof(**d), (void **)d)
int  nvc0_ram_create_(struct nouveau_object *, struct nouveau_object *,
		      struct nouveau_oclass *, int, void **);
int  nvc0_ram_get(struct nouveau_fb *, u64, u32, u32, u32,
		  struct nouveau_mem **);
void nvc0_ram_put(struct nouveau_fb *, struct nouveau_mem **);

#endif
