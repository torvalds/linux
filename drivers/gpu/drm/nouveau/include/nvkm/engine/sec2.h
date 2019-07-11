/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_SEC2_H__
#define __NVKM_SEC2_H__
#include <core/engine.h>

struct nvkm_sec2 {
	struct nvkm_engine engine;
	u32 addr;

	struct nvkm_falcon *falcon;
	struct nvkm_msgqueue *queue;
	struct work_struct work;
};

int gp102_sec2_new(struct nvkm_device *, int, struct nvkm_sec2 **);
int tu102_sec2_new(struct nvkm_device *, int, struct nvkm_sec2 **);
#endif
