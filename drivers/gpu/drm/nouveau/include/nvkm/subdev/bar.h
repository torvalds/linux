#ifndef __NVKM_BAR_H__
#define __NVKM_BAR_H__
#include <core/subdev.h>
struct nvkm_mem;
struct nvkm_vma;

struct nvkm_bar {
	struct nvkm_subdev subdev;

	struct nvkm_vm *(*kmap)(struct nvkm_bar *);
	int  (*umap)(struct nvkm_bar *, u64 size, int type, struct nvkm_vma *);
	void (*unmap)(struct nvkm_bar *, struct nvkm_vma *);
	void (*flush)(struct nvkm_bar *);

	/* whether the BAR supports to be ioremapped WC or should be uncached */
	bool iomap_uncached;
};

static inline struct nvkm_bar *
nvkm_bar(void *obj)
{
	return (void *)nvkm_subdev(obj, NVDEV_SUBDEV_BAR);
}

extern struct nvkm_oclass nv50_bar_oclass;
extern struct nvkm_oclass gf100_bar_oclass;
extern struct nvkm_oclass gk20a_bar_oclass;
#endif
