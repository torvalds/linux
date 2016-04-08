#ifndef __NVKM_TOP_H__
#define __NVKM_TOP_H__
#include <core/subdev.h>

struct nvkm_top {
	const struct nvkm_top_func *func;
	struct nvkm_subdev subdev;
	struct list_head device;
};

u32 nvkm_top_reset(struct nvkm_top *, enum nvkm_devidx);
u32 nvkm_top_intr(struct nvkm_top *, u32 intr, u64 *subdevs);
enum nvkm_devidx nvkm_top_fault(struct nvkm_top *, int fault);
enum nvkm_devidx nvkm_top_engine(struct nvkm_top *, int, int *runl, int *engn);
#endif
