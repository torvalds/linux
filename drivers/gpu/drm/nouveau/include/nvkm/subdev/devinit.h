/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_DEVINIT_H__
#define __NVKM_DEVINIT_H__
#include <core/subdev.h>
struct nvkm_devinit;

struct nvkm_devinit {
	const struct nvkm_devinit_func *func;
	struct nvkm_subdev subdev;
	bool post;
	bool force_post;
};

u32 nvkm_devinit_mmio(struct nvkm_devinit *, u32 addr);
int nvkm_devinit_pll_set(struct nvkm_devinit *, u32 type, u32 khz);
void nvkm_devinit_meminit(struct nvkm_devinit *);
u64 nvkm_devinit_disable(struct nvkm_devinit *);
int nvkm_devinit_post(struct nvkm_devinit *, u64 *disable);

int nv04_devinit_new(struct nvkm_device *, int, struct nvkm_devinit **);
int nv05_devinit_new(struct nvkm_device *, int, struct nvkm_devinit **);
int nv10_devinit_new(struct nvkm_device *, int, struct nvkm_devinit **);
int nv1a_devinit_new(struct nvkm_device *, int, struct nvkm_devinit **);
int nv20_devinit_new(struct nvkm_device *, int, struct nvkm_devinit **);
int nv50_devinit_new(struct nvkm_device *, int, struct nvkm_devinit **);
int g84_devinit_new(struct nvkm_device *, int, struct nvkm_devinit **);
int g98_devinit_new(struct nvkm_device *, int, struct nvkm_devinit **);
int gt215_devinit_new(struct nvkm_device *, int, struct nvkm_devinit **);
int mcp89_devinit_new(struct nvkm_device *, int, struct nvkm_devinit **);
int gf100_devinit_new(struct nvkm_device *, int, struct nvkm_devinit **);
int gm107_devinit_new(struct nvkm_device *, int, struct nvkm_devinit **);
int gm200_devinit_new(struct nvkm_device *, int, struct nvkm_devinit **);
#endif
