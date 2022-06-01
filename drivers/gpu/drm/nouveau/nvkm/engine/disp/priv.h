/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_DISP_PRIV_H__
#define __NVKM_DISP_PRIV_H__
#include <engine/disp.h>
#include "outp.h"
struct nv50_disp;

int nvkm_disp_ctor(const struct nvkm_disp_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		   struct nvkm_disp *);
int nvkm_disp_new_(const struct nvkm_disp_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		   struct nvkm_disp **);
void nvkm_disp_vblank(struct nvkm_disp *, int head);

struct nvkm_disp_func {
	void *(*dtor)(struct nvkm_disp *);
	int (*oneinit)(struct nvkm_disp *);
	int (*init)(struct nvkm_disp *);
	void (*fini)(struct nvkm_disp *);
	void (*intr)(struct nvkm_disp *);

	const struct nvkm_disp_oclass *root;

	int (*init_)(struct nv50_disp *);
	void (*fini_)(struct nv50_disp *);
	void (*intr_)(struct nv50_disp *);
	void (*intr_error)(struct nv50_disp *, int chid);

	const struct nvkm_event_func *uevent;
	void (*super)(struct work_struct *);

	struct {
		int (*cnt)(struct nvkm_disp *, unsigned long *mask);
		int (*new)(struct nvkm_disp *, int id);
	} wndw, head, dac, sor, pior;

	u16 ramht_size;
};

int  nvkm_disp_ntfy(struct nvkm_object *, u32, struct nvkm_event **);

extern const struct nvkm_disp_oclass nv04_disp_root_oclass;

void *nv50_disp_dtor_(struct nvkm_disp *);
int nv50_disp_oneinit_(struct nvkm_disp *);
int nv50_disp_init_(struct nvkm_disp *);
void nv50_disp_fini_(struct nvkm_disp *);
void nv50_disp_intr_(struct nvkm_disp *);

struct nvkm_disp_oclass {
	int (*ctor)(struct nvkm_disp *, const struct nvkm_oclass *,
		    void *data, u32 size, struct nvkm_object **);
	struct nvkm_sclass base;
};
#endif
