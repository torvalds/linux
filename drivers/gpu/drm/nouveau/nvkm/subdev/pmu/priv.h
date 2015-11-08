#ifndef __NVKM_PMU_PRIV_H__
#define __NVKM_PMU_PRIV_H__
#define nvkm_pmu(p) container_of((p), struct nvkm_pmu, subdev)
#include <subdev/pmu.h>
#include <subdev/pmu/fuc/os.h>

int nvkm_pmu_new_(const struct nvkm_pmu_func *, struct nvkm_device *,
		  int index, struct nvkm_pmu **);

struct nvkm_pmu_func {
	void (*reset)(struct nvkm_pmu *);

	struct {
		u32 *data;
		u32  size;
	} code;

	struct {
		u32 *data;
		u32  size;
	} data;

	void (*pgob)(struct nvkm_pmu *, bool);
};

void gk110_pmu_pgob(struct nvkm_pmu *, bool);
#endif
