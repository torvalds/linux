/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_TOP_H__
#define __NVKM_TOP_H__
#include <core/subdev.h>

struct nvkm_top {
	const struct nvkm_top_func *func;
	struct nvkm_subdev subdev;
	struct list_head device;
};

struct nvkm_top_device {
	enum nvkm_subdev_type type;
	int inst;
	u32 addr;
	int fault;
	int engine;
	int runlist;
	int reset;
	int intr;
	struct list_head head;
};

int nvkm_top_parse(struct nvkm_device *);
u32 nvkm_top_addr(struct nvkm_device *, enum nvkm_subdev_type, int);
u32 nvkm_top_reset(struct nvkm_device *, enum nvkm_subdev_type, int);
u32 nvkm_top_intr_mask(struct nvkm_device *, enum nvkm_subdev_type, int);
int nvkm_top_fault_id(struct nvkm_device *, enum nvkm_subdev_type, int);
struct nvkm_subdev *nvkm_top_fault(struct nvkm_device *, int fault);

int gk104_top_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_top **);
int ga100_top_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_top **);
#endif
