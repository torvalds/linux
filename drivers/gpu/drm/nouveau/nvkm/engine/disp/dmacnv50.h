/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NV50_DISP_DMAC_H__
#define __NV50_DISP_DMAC_H__
#define nv50_disp_dmac(p) container_of((p), struct nv50_disp_dmac, base)
#include "channv50.h"

struct nv50_disp_dmac {
	const struct nv50_disp_dmac_func *func;
	struct nv50_disp_chan base;
	u32 push;
};

struct nv50_disp_dmac_func {
	int  (*init)(struct nv50_disp_dmac *);
	void (*fini)(struct nv50_disp_dmac *);
	int  (*bind)(struct nv50_disp_dmac *, struct nvkm_object *, u32 handle);
};

int nv50_disp_dmac_new_(const struct nv50_disp_dmac_func *,
			const struct nv50_disp_chan_mthd *,
			struct nv50_disp *, int chid, int head, u64 push,
			const struct nvkm_oclass *, struct nvkm_object **);

extern const struct nv50_disp_dmac_func nv50_disp_dmac_func;
int nv50_disp_dmac_bind(struct nv50_disp_dmac *, struct nvkm_object *, u32);
extern const struct nv50_disp_dmac_func nv50_disp_core_func;

extern const struct nv50_disp_dmac_func gf119_disp_dmac_func;
void gf119_disp_dmac_fini(struct nv50_disp_dmac *);
int gf119_disp_dmac_bind(struct nv50_disp_dmac *, struct nvkm_object *, u32);
extern const struct nv50_disp_dmac_func gf119_disp_core_func;
void gf119_disp_core_fini(struct nv50_disp_dmac *);

extern const struct nv50_disp_dmac_func gp102_disp_dmac_func;
#endif
