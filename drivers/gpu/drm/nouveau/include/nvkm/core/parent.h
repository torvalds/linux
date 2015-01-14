#ifndef __NVKM_PARENT_H__
#define __NVKM_PARENT_H__
#include <core/object.h>

struct nvkm_sclass {
	struct nvkm_sclass *sclass;
	struct nvkm_engine *engine;
	struct nvkm_oclass *oclass;
};

struct nvkm_parent {
	struct nvkm_object object;

	struct nvkm_sclass *sclass;
	u64 engine;

	int  (*context_attach)(struct nvkm_object *, struct nvkm_object *);
	int  (*context_detach)(struct nvkm_object *, bool suspend,
			       struct nvkm_object *);

	int  (*object_attach)(struct nvkm_object *parent,
			      struct nvkm_object *object, u32 name);
	void (*object_detach)(struct nvkm_object *parent, int cookie);
};

static inline struct nvkm_parent *
nv_parent(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (unlikely(!(nv_iclass(obj, NV_PARENT_CLASS))))
		nv_assert("BAD CAST -> NvParent, %08x", nv_hclass(obj));
#endif
	return obj;
}

#define nvkm_parent_create(p,e,c,v,s,m,d)                                   \
	nvkm_parent_create_((p), (e), (c), (v), (s), (m),                   \
			       sizeof(**d), (void **)d)
#define nvkm_parent_init(p)                                                 \
	nvkm_object_init(&(p)->object)
#define nvkm_parent_fini(p,s)                                               \
	nvkm_object_fini(&(p)->object, (s))

int  nvkm_parent_create_(struct nvkm_object *, struct nvkm_object *,
			    struct nvkm_oclass *, u32 pclass,
			    struct nvkm_oclass *, u64 engcls,
			    int size, void **);
void nvkm_parent_destroy(struct nvkm_parent *);

void _nvkm_parent_dtor(struct nvkm_object *);
#define _nvkm_parent_init nvkm_object_init
#define _nvkm_parent_fini nvkm_object_fini

int nvkm_parent_sclass(struct nvkm_object *, u16 handle,
		       struct nvkm_object **pengine,
		       struct nvkm_oclass **poclass);
int nvkm_parent_lclass(struct nvkm_object *, u32 *, int);
#endif
