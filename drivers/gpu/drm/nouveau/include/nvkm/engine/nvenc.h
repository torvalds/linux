/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_NVENC_H__
#define __NVKM_NVENC_H__
#define nvkm_nvenc(p) container_of((p), struct nvkm_nvenc, engine)
#include <core/engine.h>
#include <core/falcon.h>

struct nvkm_nvenc {
	const struct nvkm_nvenc_func *func;
	struct nvkm_engine engine;
	struct nvkm_falcon falcon;
};

int gm107_nvenc_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_nvenc **);
#endif
