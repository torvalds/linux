#ifndef __NVKM_RAM_NVC0_H__
#define __NVKM_RAM_NVC0_H__
#define gf100_fb(p) container_of((p), struct gf100_fb, base)
#include "priv.h"

struct gf100_fb {
	struct nvkm_fb base;
	struct page *r100c10_page;
	dma_addr_t r100c10;
};

int gf100_fb_new_(const struct nvkm_fb_func *, struct nvkm_device *,
		  int index, struct nvkm_fb **);
void *gf100_fb_dtor(struct nvkm_fb *);
void gf100_fb_init(struct nvkm_fb *);
void gf100_fb_intr(struct nvkm_fb *);

void gp100_fb_init(struct nvkm_fb *);
#endif
