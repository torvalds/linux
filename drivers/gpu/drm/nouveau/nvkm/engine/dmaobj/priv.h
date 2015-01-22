#ifndef __NVKM_DMAOBJ_PRIV_H__
#define __NVKM_DMAOBJ_PRIV_H__
#include <engine/dmaobj.h>

#define nvkm_dmaobj_create(p,e,c,pa,sa,d)                                      \
	nvkm_dmaobj_create_((p), (e), (c), (pa), (sa), sizeof(**d), (void **)d)

int nvkm_dmaobj_create_(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, void **, u32 *,
			int, void **);
#define _nvkm_dmaobj_dtor nvkm_object_destroy
#define _nvkm_dmaobj_init nvkm_object_init
#define _nvkm_dmaobj_fini nvkm_object_fini

int _nvkm_dmaeng_ctor(struct nvkm_object *, struct nvkm_object *,
		      struct nvkm_oclass *, void *, u32,
		      struct nvkm_object **);
#define _nvkm_dmaeng_dtor _nvkm_engine_dtor
#define _nvkm_dmaeng_init _nvkm_engine_init
#define _nvkm_dmaeng_fini _nvkm_engine_fini

struct nvkm_dmaeng_impl {
	struct nvkm_oclass base;
	struct nvkm_oclass *sclass;
	int (*bind)(struct nvkm_dmaobj *, struct nvkm_object *,
		    struct nvkm_gpuobj **);
};
#endif
