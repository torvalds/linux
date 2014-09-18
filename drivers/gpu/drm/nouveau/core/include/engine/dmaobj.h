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

struct nouveau_dmaeng {
	struct nouveau_engine base;

	/* creates a "physical" dma object from a struct nouveau_dmaobj */
	int (*bind)(struct nouveau_dmaobj *dmaobj,
		    struct nouveau_object *parent,
		    struct nouveau_gpuobj **);
};

extern struct nouveau_oclass *nv04_dmaeng_oclass;
extern struct nouveau_oclass *nv50_dmaeng_oclass;
extern struct nouveau_oclass *nvc0_dmaeng_oclass;
extern struct nouveau_oclass *nvd0_dmaeng_oclass;

#endif
