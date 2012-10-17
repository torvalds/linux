#ifndef __NOUVEAU_DMAOBJ_H__
#define __NOUVEAU_DMAOBJ_H__

#include <core/object.h>
#include <core/engine.h>

struct nouveau_gpuobj;

struct nouveau_dmaobj {
	struct nouveau_object base;
	u32 target;
	u32 access;
	u64 start;
	u64 limit;
};

#define nouveau_dmaobj_create(p,e,c,a,s,d)                                     \
	nouveau_dmaobj_create_((p), (e), (c), (a), (s), sizeof(**d), (void **)d)
#define nouveau_dmaobj_destroy(p)                                              \
	nouveau_object_destroy(&(p)->base)
#define nouveau_dmaobj_init(p)                                                 \
	nouveau_object_init(&(p)->base)
#define nouveau_dmaobj_fini(p,s)                                               \
	nouveau_object_fini(&(p)->base, (s))

int nouveau_dmaobj_create_(struct nouveau_object *, struct nouveau_object *,
			   struct nouveau_oclass *, void *data, u32 size,
			   int length, void **);

#define _nouveau_dmaobj_dtor nouveau_object_destroy
#define _nouveau_dmaobj_init nouveau_object_init
#define _nouveau_dmaobj_fini nouveau_object_fini

struct nouveau_dmaeng {
	struct nouveau_engine base;
	int (*bind)(struct nouveau_dmaeng *, struct nouveau_object *parent,
		    struct nouveau_dmaobj *, struct nouveau_gpuobj **);
};

#define nouveau_dmaeng_create(p,e,c,d)                                         \
	nouveau_engine_create((p), (e), (c), true, "DMAOBJ", "dmaobj", (d))
#define nouveau_dmaeng_destroy(p)                                              \
	nouveau_engine_destroy(&(p)->base)
#define nouveau_dmaeng_init(p)                                                 \
	nouveau_engine_init(&(p)->base)
#define nouveau_dmaeng_fini(p,s)                                               \
	nouveau_engine_fini(&(p)->base, (s))

#define _nouveau_dmaeng_dtor _nouveau_engine_dtor
#define _nouveau_dmaeng_init _nouveau_engine_init
#define _nouveau_dmaeng_fini _nouveau_engine_fini

extern struct nouveau_oclass nv04_dmaeng_oclass;
extern struct nouveau_oclass nv50_dmaeng_oclass;
extern struct nouveau_oclass nvc0_dmaeng_oclass;

#endif
