#ifndef __NOUVEAU_PARENT_H__
#define __NOUVEAU_PARENT_H__

#include <core/device.h>
#include <core/object.h>

struct nouveau_sclass {
	struct nouveau_sclass *sclass;
	struct nouveau_engine *engine;
	struct nouveau_oclass *oclass;
};

struct nouveau_parent {
	struct nouveau_object base;

	struct nouveau_sclass *sclass;
	u32 engine;

	int  (*context_attach)(struct nouveau_object *,
			       struct nouveau_object *);
	int  (*context_detach)(struct nouveau_object *, bool suspend,
			       struct nouveau_object *);

	int  (*object_attach)(struct nouveau_object *parent,
			      struct nouveau_object *object, u32 name);
	void (*object_detach)(struct nouveau_object *parent, int cookie);
};

static inline struct nouveau_parent *
nv_parent(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (unlikely(!(nv_iclass(obj, NV_PARENT_CLASS))))
		nv_assert("BAD CAST -> NvParent, %08x", nv_hclass(obj));
#endif
	return obj;
}

#define nouveau_parent_create(p,e,c,v,s,m,d)                                   \
	nouveau_parent_create_((p), (e), (c), (v), (s), (m),                   \
			       sizeof(**d), (void **)d)
#define nouveau_parent_init(p)                                                 \
	nouveau_object_init(&(p)->base)
#define nouveau_parent_fini(p,s)                                               \
	nouveau_object_fini(&(p)->base, (s))

int  nouveau_parent_create_(struct nouveau_object *, struct nouveau_object *,
			    struct nouveau_oclass *, u32 pclass,
			    struct nouveau_oclass *, u64 engcls,
			    int size, void **);
void nouveau_parent_destroy(struct nouveau_parent *);

void _nouveau_parent_dtor(struct nouveau_object *);
#define _nouveau_parent_init _nouveau_object_init
#define _nouveau_parent_fini _nouveau_object_fini

int nouveau_parent_sclass(struct nouveau_object *, u16 handle,
			  struct nouveau_object **pengine,
			  struct nouveau_oclass **poclass);

#endif
