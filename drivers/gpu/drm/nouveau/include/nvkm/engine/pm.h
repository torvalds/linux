#ifndef __NVKM_PM_H__
#define __NVKM_PM_H__

#include <core/device.h>
#include <core/engine.h>
#include <core/engctx.h>

struct nouveau_perfdom;
struct nouveau_perfctr;
struct nouveau_pm {
	struct nouveau_engine base;

	struct nouveau_perfctx *context;
	void *profile_data;

	struct list_head domains;
	u32 sequence;

	/*XXX: temp for daemon backend */
	u32 pwr[8];
	u32 last;
};

static inline struct nouveau_pm *
nouveau_pm(void *obj)
{
	return (void *)nouveau_engine(obj, NVDEV_ENGINE_PM);
}

extern struct nouveau_oclass *nv40_pm_oclass;
extern struct nouveau_oclass *nv50_pm_oclass;
extern struct nouveau_oclass *nv84_pm_oclass;
extern struct nouveau_oclass *nva3_pm_oclass;
extern struct nouveau_oclass nvc0_pm_oclass;
extern struct nouveau_oclass nve0_pm_oclass;
extern struct nouveau_oclass nvf0_pm_oclass;

#endif
