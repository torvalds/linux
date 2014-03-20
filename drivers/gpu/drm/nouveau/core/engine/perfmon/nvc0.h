#ifndef __NVKM_PM_NVC0_H__
#define __NVKM_PM_NVC0_H__

#include "priv.h"

struct nvc0_perfmon_priv {
	struct nouveau_perfmon base;
};

struct nvc0_perfmon_cntr {
	struct nouveau_perfctr base;
};

extern const struct nouveau_funcdom nvc0_perfctr_func;
int nvc0_perfmon_fini(struct nouveau_object *, bool);

#endif
