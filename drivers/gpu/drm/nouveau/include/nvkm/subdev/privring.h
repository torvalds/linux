/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_PRIVRING_H__
#define __NVKM_PRIVRING_H__
#include <core/subdev.h>

int gf100_privring_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_subdev **);
int gf117_privring_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_subdev **);
int gk104_privring_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_subdev **);
int gk20a_privring_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_subdev **);
int gm200_privring_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_subdev **);
int gp10b_privring_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_subdev **);
#endif
