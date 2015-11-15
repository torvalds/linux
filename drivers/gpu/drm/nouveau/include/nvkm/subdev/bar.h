#ifndef __NVKM_BAR_H__
#define __NVKM_BAR_H__
#include <core/subdev.h>
struct nvkm_vma;

struct nvkm_bar {
	const struct nvkm_bar_func *func;
	struct nvkm_subdev subdev;

	spinlock_t lock;

	/* whether the BAR supports to be ioremapped WC or should be uncached */
	bool iomap_uncached;
};

void nvkm_bar_flush(struct nvkm_bar *);
struct nvkm_vm *nvkm_bar_kmap(struct nvkm_bar *);
int nvkm_bar_umap(struct nvkm_bar *, u64 size, int type, struct nvkm_vma *);

int nv50_bar_new(struct nvkm_device *, int, struct nvkm_bar **);
int g84_bar_new(struct nvkm_device *, int, struct nvkm_bar **);
int gf100_bar_new(struct nvkm_device *, int, struct nvkm_bar **);
int gk20a_bar_new(struct nvkm_device *, int, struct nvkm_bar **);
#endif
