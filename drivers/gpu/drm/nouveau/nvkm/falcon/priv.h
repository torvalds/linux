/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_FALCON_PRIV_H__
#define __NVKM_FALCON_PRIV_H__
#include <engine/falcon.h>

void
nvkm_falcon_ctor(const struct nvkm_falcon_func *, struct nvkm_subdev *,
		 const char *, u32, struct nvkm_falcon *);
#endif
