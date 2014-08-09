#ifndef __NVKM_DMAOBJ_PRIV_H__
#define __NVKM_DMAOBJ_PRIV_H__

#include <engine/dmaobj.h>

int _nvkm_dmaeng_ctor(struct nouveau_object *, struct nouveau_object *,
		      struct nouveau_oclass *, void *, u32,
		      struct nouveau_object **);
#define _nvkm_dmaeng_dtor _nouveau_engine_dtor
#define _nvkm_dmaeng_init _nouveau_engine_init
#define _nvkm_dmaeng_fini _nouveau_engine_fini

struct nvkm_dmaeng_impl {
	struct nouveau_oclass base;
	int (*bind)(struct nouveau_dmaeng *, struct nouveau_object *,
		    struct nouveau_dmaobj *, struct nouveau_gpuobj **);
};

#endif
