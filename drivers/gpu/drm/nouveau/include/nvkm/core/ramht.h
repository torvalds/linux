#ifndef __NVKM_RAMHT_H__
#define __NVKM_RAMHT_H__
#include <core/gpuobj.h>

struct nvkm_ramht {
	struct nvkm_gpuobj gpuobj;
	int bits;
};

int  nvkm_ramht_insert(struct nvkm_ramht *, int chid, u32 handle, u32 context);
void nvkm_ramht_remove(struct nvkm_ramht *, int cookie);
int  nvkm_ramht_new(struct nvkm_object *, struct nvkm_object *, u32 size,
		    u32 align, struct nvkm_ramht **);

static inline void
nvkm_ramht_ref(struct nvkm_ramht *obj, struct nvkm_ramht **ref)
{
	nvkm_gpuobj_ref(&obj->gpuobj, (struct nvkm_gpuobj **)ref);
}
#endif
