#ifndef __NVKM_SW_NV50_H__
#define __NVKM_SW_NV50_H__

#include <engine/software.h>

struct nv50_software_oclass {
	struct nouveau_oclass base;
	struct nouveau_oclass *cclass;
	struct nouveau_oclass *sclass;
};

struct nv50_software_priv {
	struct nouveau_software base;
};

int  nv50_software_ctor(struct nouveau_object *, struct nouveau_object *,
			struct nouveau_oclass *, void *, u32,
			struct nouveau_object **);

struct nv50_software_cclass {
	struct nouveau_oclass base;
	int (*vblank)(void *, u32, int);
};

struct nv50_software_chan {
	struct nouveau_software_chan base;
	struct {
		struct nouveau_eventh **event;
		int nr_event;
		u32 channel;
		u32 ctxdma;
		u64 offset;
		u32 value;
	} vblank;
};

int  nv50_software_context_ctor(struct nouveau_object *,
				struct nouveau_object *,
				struct nouveau_oclass *, void *, u32,
				struct nouveau_object **);
void nv50_software_context_dtor(struct nouveau_object *);

int nv50_software_mthd_vblsem_value(struct nouveau_object *, u32, void *, u32);
int nv50_software_mthd_vblsem_release(struct nouveau_object *, u32, void *, u32);
int nv50_software_mthd_flip(struct nouveau_object *, u32, void *, u32);

#endif
