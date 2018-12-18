/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NV50_DEVINIT_H__
#define __NV50_DEVINIT_H__
#define nv50_devinit(p) container_of((p), struct nv50_devinit, base)
#include "priv.h"

struct nv50_devinit {
	struct nvkm_devinit base;
	u32 r001540;
};

int nv50_devinit_new_(const struct nvkm_devinit_func *, struct nvkm_device *,
		      int, struct nvkm_devinit **);
void nv50_devinit_preinit(struct nvkm_devinit *);
void nv50_devinit_init(struct nvkm_devinit *);
int  nv50_devinit_pll_set(struct nvkm_devinit *, u32, u32);

int  gt215_devinit_pll_set(struct nvkm_devinit *, u32, u32);

int  gf100_devinit_ctor(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, void *, u32,
			struct nvkm_object **);
int  gf100_devinit_pll_set(struct nvkm_devinit *, u32, u32);
void gf100_devinit_preinit(struct nvkm_devinit *);

u64  gm107_devinit_disable(struct nvkm_devinit *);

int gm200_devinit_post(struct nvkm_devinit *, bool);
#endif
