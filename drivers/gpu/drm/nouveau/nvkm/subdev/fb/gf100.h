#ifndef __NVKM_RAM_NVC0_H__
#define __NVKM_RAM_NVC0_H__
#include "priv.h"
#include "nv50.h"

struct gf100_fb_priv {
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

#define gf100_ram_create(p,e,o,m,d)                                             \
	gf100_ram_create_((p), (e), (o), (m), sizeof(**d), (void **)d)
int  gf100_ram_create_(struct nvkm_object *, struct nvkm_object *,
		      struct nvkm_oclass *, u32, int, void **);
int  gf100_ram_get(struct nvkm_fb *, u64, u32, u32, u32,
		  struct nvkm_mem **);
void gf100_ram_put(struct nvkm_fb *, struct nvkm_mem **);

int  gk104_ram_init(struct nvkm_object*);
#endif
