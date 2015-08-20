#ifndef __NVKM_NVSW_H__
#define __NVKM_NVSW_H__
#include "priv.h"

extern struct nvkm_ofuncs nvkm_nvsw_ofuncs;
int
nvkm_nvsw_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject);
#endif
