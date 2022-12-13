/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_SEC2_H__
#define __NVKM_SEC2_H__
#define nvkm_sec2(p) container_of((p), struct nvkm_sec2, engine)
#include <core/engine.h>
#include <core/falcon.h>

struct nvkm_sec2 {
	const struct nvkm_sec2_func *func;
	struct nvkm_engine engine;
	struct nvkm_falcon falcon;

	atomic_t running;
	atomic_t initmsg;

	struct nvkm_falcon_qmgr *qmgr;
	struct nvkm_falcon_cmdq *cmdq;
	struct nvkm_falcon_msgq *msgq;

	struct work_struct work;
};

int gp102_sec2_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_sec2 **);
int gp108_sec2_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_sec2 **);
int tu102_sec2_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_sec2 **);
int ga102_sec2_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_sec2 **);
#endif
