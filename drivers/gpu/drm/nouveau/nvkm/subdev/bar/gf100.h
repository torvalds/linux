#ifndef __GF100_BAR_H__
#define __GF100_BAR_H__
#define gf100_bar(p) container_of((p), struct gf100_bar, base)
#include "priv.h"

struct gf100_bar_vm {
	struct nvkm_memory *mem;
	struct nvkm_gpuobj *pgd;
	struct nvkm_vm *vm;
};

struct gf100_bar {
	struct nvkm_bar base;
	bool bar2_halve;
	struct gf100_bar_vm bar[2];
};

int gf100_bar_new_(const struct nvkm_bar_func *, struct nvkm_device *,
		   int, struct nvkm_bar **);
void *gf100_bar_dtor(struct nvkm_bar *);
int gf100_bar_oneinit(struct nvkm_bar *);
int gf100_bar_init(struct nvkm_bar *);
int gf100_bar_umap(struct nvkm_bar *, u64, int, struct nvkm_vma *);
#endif
