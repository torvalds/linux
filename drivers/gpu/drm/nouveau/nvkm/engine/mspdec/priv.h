/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_MSPDEC_PRIV_H__
#define __NVKM_MSPDEC_PRIV_H__
#include <engine/mspdec.h>

int nvkm_mspdec_new_(const struct nvkm_falcon_func *, struct nvkm_device *, enum nvkm_subdev_type,
		     int, struct nvkm_engine **);

void g98_mspdec_init(struct nvkm_falcon *);

void gf100_mspdec_init(struct nvkm_falcon *);
#endif
