#ifndef __NVKM_PM_NVC0_H__
#define __NVKM_PM_NVC0_H__
#include "priv.h"

struct gf100_pm_priv {
	struct nvkm_pm base;
};

struct gf100_pm_cntr {
	struct nvkm_perfctr base;
};

extern const struct nvkm_funcdom gf100_perfctr_func;
int gf100_pm_fini(struct nvkm_object *, bool);
#endif
