#ifndef __GF100_BAR_H__
#define __GF100_BAR_H__
#define gf100_bar(p) container_of((p), struct gf100_bar, base)
#include "priv.h"

struct gf100_barN {
	struct nvkm_memory *inst;
	struct nvkm_vmm *vmm;
};

struct gf100_bar {
	struct nvkm_bar base;
	bool bar2_halve;
	struct gf100_barN bar[2];
};

int gf100_bar_new_(const struct nvkm_bar_func *, struct nvkm_device *,
		   int, struct nvkm_bar **);
void *gf100_bar_dtor(struct nvkm_bar *);
int gf100_bar_oneinit(struct nvkm_bar *);
void gf100_bar_bar1_init(struct nvkm_bar *);
void gf100_bar_bar1_wait(struct nvkm_bar *);
struct nvkm_vmm *gf100_bar_bar1_vmm(struct nvkm_bar *);
void gf100_bar_bar2_init(struct nvkm_bar *);
struct nvkm_vmm *gf100_bar_bar2_vmm(struct nvkm_bar *);
#endif
