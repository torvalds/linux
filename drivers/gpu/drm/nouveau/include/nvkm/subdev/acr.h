/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_ACR_H__
#define __NVKM_ACR_H__
#define nvkm_acr(p) container_of((p), struct nvkm_acr, subdev)
#include <core/subdev.h>

struct nvkm_acr {
	struct nvkm_subdev subdev;
};
#endif
