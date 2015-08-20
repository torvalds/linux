#ifndef __NVKM_PM_NVC0_H__
#define __NVKM_PM_NVC0_H__
#include "priv.h"

struct gf100_pm_oclass {
	struct nvkm_oclass base;
	const struct nvkm_specdom *doms_hub;
	const struct nvkm_specdom *doms_gpc;
	const struct nvkm_specdom *doms_part;
};

int gf100_pm_ctor(struct nvkm_object *, struct nvkm_object *,
		  struct nvkm_oclass *, void *data, u32 size,
		  struct nvkm_object **pobject);

struct gf100_pm_cntr {
	struct nvkm_perfctr base;
};

extern const struct nvkm_funcdom gf100_perfctr_func;
int gf100_pm_fini(struct nvkm_object *, bool);

extern const struct nvkm_specdom gf100_pm_gpc[];

extern const struct nvkm_specsrc gf100_pbfb_sources[];
extern const struct nvkm_specsrc gf100_pmfb_sources[];

#endif
