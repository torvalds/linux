/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_ACR_H__
#define __NVKM_ACR_H__
#define nvkm_acr(p) container_of((p), struct nvkm_acr, subdev)
#include <core/subdev.h>
struct nvkm_falcon;

struct nvkm_acr {
	const struct nvkm_acr_func *func;
	struct nvkm_subdev subdev;
};

int gm200_acr_new(struct nvkm_device *, int, struct nvkm_acr **);
int gm20b_acr_new(struct nvkm_device *, int, struct nvkm_acr **);
int gp102_acr_new(struct nvkm_device *, int, struct nvkm_acr **);
int gp108_acr_new(struct nvkm_device *, int, struct nvkm_acr **);
int gp10b_acr_new(struct nvkm_device *, int, struct nvkm_acr **);
#endif
