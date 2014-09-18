#ifndef __NOUVEAU_RAMHT_H__
#define __NOUVEAU_RAMHT_H__

#include <core/gpuobj.h>

struct nouveau_ramht {
	struct nouveau_gpuobj base;
	int bits;
};

int  nouveau_ramht_insert(struct nouveau_ramht *, int chid,
			  u32 handle, u32 context);
void nouveau_ramht_remove(struct nouveau_ramht *, int cookie);
int  nouveau_ramht_new(struct nouveau_object *, struct nouveau_object *,
		       u32 size, u32 align, struct nouveau_ramht **);

static inline void
nouveau_ramht_ref(struct nouveau_ramht *obj, struct nouveau_ramht **ref)
{
	nouveau_gpuobj_ref(&obj->base, (struct nouveau_gpuobj **)ref);
}

#endif
