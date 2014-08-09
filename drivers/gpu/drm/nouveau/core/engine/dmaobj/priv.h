#ifndef __NVKM_DMAOBJ_PRIV_H__
#define __NVKM_DMAOBJ_PRIV_H__

#include <engine/dmaobj.h>

#define nvkm_dmaobj_create(p,e,c,pa,sa,d)                                      \
	nvkm_dmaobj_create_((p), (e), (c), (pa), (sa), sizeof(**d), (void **)d)

int nvkm_dmaobj_create_(struct nouveau_object *, struct nouveau_object *,
			struct nouveau_oclass *, void **, u32 *,
			int, void **);
#define _nvkm_dmaobj_dtor nouveau_object_destroy
#define _nvkm_dmaobj_init nouveau_object_init
#define _nvkm_dmaobj_fini nouveau_object_fini

int _nvkm_dmaeng_ctor(struct nouveau_object *, struct nouveau_object *,
		      struct nouveau_oclass *, void *, u32,
		      struct nouveau_object **);
#define _nvkm_dmaeng_dtor _nouveau_engine_dtor
#define _nvkm_dmaeng_init _nouveau_engine_init
#define _nvkm_dmaeng_fini _nouveau_engine_fini

struct nvkm_dmaeng_impl {
	struct nouveau_oclass base;
	struct nouveau_oclass *sclass;
	int (*bind)(struct nouveau_dmaobj *, struct nouveau_object *,
		    struct nouveau_gpuobj **);
};

#endif
