/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_SEC2_PRIV_H__
#define __NVKM_SEC2_PRIV_H__
#include <engine/sec2.h>

struct nvkm_sec2_func {
	const struct nvkm_falcon_func *flcn;
	u8 unit_acr;
	void (*intr)(struct nvkm_sec2 *);
	int (*initmsg)(struct nvkm_sec2 *);
};

void gp102_sec2_intr(struct nvkm_sec2 *);
int gp102_sec2_initmsg(struct nvkm_sec2 *);

struct nvkm_sec2_fwif {
	int version;
	int (*load)(struct nvkm_sec2 *, int ver, const struct nvkm_sec2_fwif *);
	const struct nvkm_sec2_func *func;
	const struct nvkm_acr_lsf_func *acr;
};

int gp102_sec2_load(struct nvkm_sec2 *, int, const struct nvkm_sec2_fwif *);
extern const struct nvkm_sec2_func gp102_sec2;
extern const struct nvkm_acr_lsf_func gp102_sec2_acr_1;

int nvkm_sec2_new_(const struct nvkm_sec2_fwif *, struct nvkm_device *,
		   int, u32 addr, struct nvkm_sec2 **);
#endif
