/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_OFA_H__
#define __NVKM_OFA_H__
#include <core/engine.h>

int ga100_ofa_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_engine **);
int ga102_ofa_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_engine **);
int ad102_ofa_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_engine **);
#endif
