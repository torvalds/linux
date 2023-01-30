/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_DEVINIT_PRIV_H__
#define __NVKM_DEVINIT_PRIV_H__
#define nvkm_devinit(p) container_of((p), struct nvkm_devinit, subdev)
#include <subdev/devinit.h>

struct nvkm_devinit_func {
	void *(*dtor)(struct nvkm_devinit *);
	void (*preinit)(struct nvkm_devinit *);
	void (*init)(struct nvkm_devinit *);
	int  (*post)(struct nvkm_devinit *, bool post);
	u32  (*mmio)(struct nvkm_devinit *, u32);
	void (*meminit)(struct nvkm_devinit *);
	int  (*pll_set)(struct nvkm_devinit *, u32 type, u32 freq);
	void (*disable)(struct nvkm_devinit *);
};

void nvkm_devinit_ctor(const struct nvkm_devinit_func *, struct nvkm_device *,
		       enum nvkm_subdev_type, int inst, struct nvkm_devinit *);
u64 nvkm_devinit_disable(struct nvkm_devinit *);

int nv04_devinit_post(struct nvkm_devinit *, bool);
int tu102_devinit_post(struct nvkm_devinit *, bool);
#endif
