#ifndef __NVKM_SW_NV50_H__
#define __NVKM_SW_NV50_H__
#include <engine/sw.h>
#include <core/notify.h>

struct nv50_sw_oclass {
	struct nvkm_oclass base;
	struct nvkm_oclass *cclass;
	struct nvkm_oclass *sclass;
};

struct nv50_sw_priv {
	struct nvkm_sw base;
};

int  nv50_sw_ctor(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, void *, u32,
			struct nvkm_object **);

struct nv50_sw_cclass {
	struct nvkm_oclass base;
	int (*vblank)(struct nvkm_notify *);
};

struct nv50_sw_chan {
	struct nvkm_sw_chan base;
	struct {
		struct nvkm_notify notify[4];
		u32 channel;
		u32 ctxdma;
		u64 offset;
		u32 value;
	} vblank;
};

int  nv50_sw_context_ctor(struct nvkm_object *,
				struct nvkm_object *,
				struct nvkm_oclass *, void *, u32,
				struct nvkm_object **);
void nv50_sw_context_dtor(struct nvkm_object *);

int nv50_sw_mthd_vblsem_value(struct nvkm_object *, u32, void *, u32);
int nv50_sw_mthd_vblsem_release(struct nvkm_object *, u32, void *, u32);
int nv50_sw_mthd_flip(struct nvkm_object *, u32, void *, u32);
#endif
