#ifndef __NV04_INSTMEM_H__
#define __NV04_INSTMEM_H__
#include "priv.h"

#include <core/mm.h>

extern struct nvkm_instobj_impl nv04_instobj_oclass;

struct nv04_instmem_priv {
	struct nvkm_instmem base;

	void __iomem *iomem;
	struct nvkm_mm heap;

	struct nvkm_gpuobj *vbios;
	struct nvkm_ramht  *ramht;
	struct nvkm_gpuobj *ramro;
	struct nvkm_gpuobj *ramfc;
};

static inline struct nv04_instmem_priv *
nv04_instmem(void *obj)
{
	return (void *)nvkm_instmem(obj);
}

struct nv04_instobj_priv {
	struct nvkm_instobj base;
	struct nvkm_mm_node *mem;
};

void nv04_instmem_dtor(struct nvkm_object *);

int nv04_instmem_alloc(struct nvkm_instmem *, struct nvkm_object *,
		       u32 size, u32 align, struct nvkm_object **pobject);
#endif
