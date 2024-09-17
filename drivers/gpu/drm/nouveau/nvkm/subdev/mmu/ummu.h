#ifndef __NVKM_UMMU_H__
#define __NVKM_UMMU_H__
#define nvkm_ummu(p) container_of((p), struct nvkm_ummu, object)
#include <core/object.h>
#include "priv.h"

struct nvkm_ummu {
	struct nvkm_object object;
	struct nvkm_mmu *mmu;
};

int nvkm_ummu_new(struct nvkm_device *, const struct nvkm_oclass *,
		  void *argv, u32 argc, struct nvkm_object **);
#endif
