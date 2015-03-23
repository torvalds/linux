#ifndef __NVKM_PM_NV40_H__
#define __NVKM_PM_NV40_H__
#include "priv.h"

struct nv40_pm_oclass {
	struct nvkm_oclass base;
	const struct nvkm_specdom *doms;
};

struct nv40_pm_priv {
	struct nvkm_pm base;
	u32 sequence;
};

int nv40_pm_ctor(struct nvkm_object *, struct nvkm_object *,
		      struct nvkm_oclass *, void *data, u32 size,
		      struct nvkm_object **pobject);

struct nv40_pm_cntr {
	struct nvkm_perfctr base;
};

extern const struct nvkm_funcdom nv40_perfctr_func;
#endif
