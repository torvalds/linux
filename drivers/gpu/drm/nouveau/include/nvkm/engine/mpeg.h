/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_MPEG_H__
#define __NVKM_MPEG_H__
#include <core/engine.h>
int nv31_mpeg_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_engine **);
int nv40_mpeg_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_engine **);
int nv44_mpeg_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_engine **);
int nv50_mpeg_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_engine **);
int g84_mpeg_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_engine **);
#endif
