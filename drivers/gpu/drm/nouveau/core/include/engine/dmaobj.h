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
	u32 conf0;
};

struct nouveau_dmaeng {
	struct nouveau_engine base;

	/* creates a "physical" dma object from a struct nouveau_dmaobj */
	int (*bind)(struct nouveau_dmaeng *dmaeng,
		    struct nouveau_object *parent,
		    struct nouveau_dmaobj *dmaobj,
		    struct nouveau_gpuobj **);
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
extern struct nouveau_oclass nvd0_dmaeng_oclass;

extern struct nouveau_oclass nouveau_dmaobj_sclass[];

#endif
