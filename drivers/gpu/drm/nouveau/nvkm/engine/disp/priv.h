/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_DISP_PRIV_H__
#define __NVKM_DISP_PRIV_H__
#include <engine/disp.h>
#include "outp.h"

int nvkm_disp_ctor(const struct nvkm_disp_func *, struct nvkm_device *,
		   int index, struct nvkm_disp *);
int nvkm_disp_new_(const struct nvkm_disp_func *, struct nvkm_device *,
		   int index, struct nvkm_disp **);
void nvkm_disp_vblank(struct nvkm_disp *, int head);

struct nvkm_disp_func {
	void *(*dtor)(struct nvkm_disp *);
	int (*oneinit)(struct nvkm_disp *);
	int (*init)(struct nvkm_disp *);
	void (*fini)(struct nvkm_disp *);
	void (*intr)(struct nvkm_disp *);

	const struct nvkm_disp_oclass *(*root)(struct nvkm_disp *);
};

int  nvkm_disp_ntfy(struct nvkm_object *, u32, struct nvkm_event **);

extern const struct nvkm_disp_oclass nv04_disp_root_oclass;

struct nvkm_disp_oclass {
	int (*ctor)(struct nvkm_disp *, const struct nvkm_oclass *,
		    void *data, u32 size, struct nvkm_object **);
	struct nvkm_sclass base;
};
#endif
