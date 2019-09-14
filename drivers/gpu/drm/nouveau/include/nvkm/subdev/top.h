/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_TOP_H__
#define __NVKM_TOP_H__
#include <core/subdev.h>

struct nvkm_top {
	const struct nvkm_top_func *func;
	struct nvkm_subdev subdev;
	struct list_head device;
};

u32 nvkm_top_addr(struct nvkm_device *, enum nvkm_devidx);
u32 nvkm_top_reset(struct nvkm_device *, enum nvkm_devidx);
u32 nvkm_top_intr(struct nvkm_device *, u32 intr, u64 *subdevs);
u32 nvkm_top_intr_mask(struct nvkm_device *, enum nvkm_devidx);
int nvkm_top_fault_id(struct nvkm_device *, enum nvkm_devidx);
enum nvkm_devidx nvkm_top_fault(struct nvkm_device *, int fault);
enum nvkm_devidx nvkm_top_engine(struct nvkm_device *, int, int *runl, int *engn);

int gk104_top_new(struct nvkm_device *, int, struct nvkm_top **);
#endif
