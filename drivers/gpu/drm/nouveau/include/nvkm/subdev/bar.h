/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_BAR_H__
#define __NVKM_BAR_H__
#include <core/subdev.h>
struct nvkm_vma;

struct nvkm_bar {
	const struct nvkm_bar_func *func;
	struct nvkm_subdev subdev;

	spinlock_t lock;
	bool bar2;

	void __iomem *flushBAR2PhysMode;
	struct nvkm_memory *flushFBZero;
	void __iomem *flushBAR2;

	/* whether the BAR supports to be ioremapped WC or should be uncached */
	bool iomap_uncached;
};

struct nvkm_vmm *nvkm_bar_bar1_vmm(struct nvkm_device *);
void nvkm_bar_bar1_reset(struct nvkm_device *);
void nvkm_bar_bar2_init(struct nvkm_device *);
void nvkm_bar_bar2_fini(struct nvkm_device *);
void nvkm_bar_bar2_reset(struct nvkm_device *);
struct nvkm_vmm *nvkm_bar_bar2_vmm(struct nvkm_device *);
void nvkm_bar_flush(struct nvkm_bar *);

int nv50_bar_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_bar **);
int g84_bar_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_bar **);
int gf100_bar_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_bar **);
int gk20a_bar_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_bar **);
int gm107_bar_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_bar **);
int gm20b_bar_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_bar **);
int tu102_bar_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_bar **);
#endif
