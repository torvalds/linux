#ifndef __NVKM_PM_NV40_H__
#define __NVKM_PM_NV40_H__

#include "priv.h"

struct nv40_perfmon_oclass {
	struct nouveau_oclass base;
	const struct nouveau_specdom *doms;
};

struct nv40_perfmon_priv {
	struct nouveau_perfmon base;
	u32 sequence;
};

int nv40_perfmon_ctor(struct nouveau_object *, struct nouveau_object *,
		      struct nouveau_oclass *, void *data, u32 size,
		      struct nouveau_object **pobject);

struct nv40_perfmon_cntr {
	struct nouveau_perfctr base;
};

extern const struct nouveau_funcdom nv40_perfctr_func;

#endif
