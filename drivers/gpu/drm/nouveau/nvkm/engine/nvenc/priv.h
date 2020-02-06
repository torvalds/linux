/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_NVENC_PRIV_H__
#define __NVKM_NVENC_PRIV_H__
#include <engine/nvenc.h>

struct nvkm_nvenc_func {
	const struct nvkm_falcon_func *flcn;
};

struct nvkm_nvenc_fwif {
	int version;
	int (*load)(struct nvkm_nvenc *, int ver,
		    const struct nvkm_nvenc_fwif *);
	const struct nvkm_nvenc_func *func;
};

int nvkm_nvenc_new_(const struct nvkm_nvenc_fwif *, struct nvkm_device *,
		    int, struct nvkm_nvenc **pnvenc);
#endif
