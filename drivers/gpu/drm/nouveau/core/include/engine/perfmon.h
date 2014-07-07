#ifndef __NVKM_PERFMON_H__
#define __NVKM_PERFMON_H__

#include <core/device.h>
#include <core/engine.h>
#include <core/engctx.h>
#include <core/class.h>

struct nouveau_perfdom;
struct nouveau_perfctr;
struct nouveau_perfmon {
	struct nouveau_engine base;

	struct nouveau_perfctx *context;
	void *profile_data;

	struct list_head domains;
	u32 sequence;

	/*XXX: temp for daemon backend */
	u32 pwr[8];
	u32 last;
};

static inline struct nouveau_perfmon *
nouveau_perfmon(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_ENGINE_PERFMON];
}

extern struct nouveau_oclass *nv40_perfmon_oclass;
extern struct nouveau_oclass *nv50_perfmon_oclass;
extern struct nouveau_oclass *nv84_perfmon_oclass;
extern struct nouveau_oclass *nva3_perfmon_oclass;
extern struct nouveau_oclass nvc0_perfmon_oclass;
extern struct nouveau_oclass nve0_perfmon_oclass;
extern struct nouveau_oclass nvf0_perfmon_oclass;

#endif
