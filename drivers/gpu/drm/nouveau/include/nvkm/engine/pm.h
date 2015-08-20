#ifndef __NVKM_PM_H__
#define __NVKM_PM_H__
#include <core/engine.h>

struct nvkm_pm {
	const struct nvkm_pm_func *func;
	struct nvkm_engine engine;

	struct nvkm_object *perfmon;

	struct list_head domains;
	struct list_head sources;
	u32 sequence;
};

int nv40_pm_new(struct nvkm_device *, int, struct nvkm_pm **);
int nv50_pm_new(struct nvkm_device *, int, struct nvkm_pm **);
int g84_pm_new(struct nvkm_device *, int, struct nvkm_pm **);
int gt200_pm_new(struct nvkm_device *, int, struct nvkm_pm **);
int gt215_pm_new(struct nvkm_device *, int, struct nvkm_pm **);
int gf100_pm_new(struct nvkm_device *, int, struct nvkm_pm **);
int gf108_pm_new(struct nvkm_device *, int, struct nvkm_pm **);
int gf117_pm_new(struct nvkm_device *, int, struct nvkm_pm **);
int gk104_pm_new(struct nvkm_device *, int, struct nvkm_pm **);
#endif
