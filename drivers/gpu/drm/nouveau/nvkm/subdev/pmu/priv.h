#ifndef __NVKM_PMU_PRIV_H__
#define __NVKM_PMU_PRIV_H__
#include <subdev/pmu.h>
#include <subdev/pmu/fuc/os.h>

#define nvkm_pmu_create(p, e, o, d)                                         \
	nvkm_pmu_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nvkm_pmu_destroy(p)                                                 \
	nvkm_subdev_destroy(&(p)->base)
#define nvkm_pmu_init(p) ({                                                 \
	struct nvkm_pmu *_pmu = (p);                                       \
	_nvkm_pmu_init(nv_object(_pmu));                                   \
})
#define nvkm_pmu_fini(p,s) ({                                               \
	struct nvkm_pmu *_pmu = (p);                                       \
	_nvkm_pmu_fini(nv_object(_pmu), (s));                              \
})

int nvkm_pmu_create_(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, int, void **);

int _nvkm_pmu_ctor(struct nvkm_object *, struct nvkm_object *,
		      struct nvkm_oclass *, void *, u32,
		      struct nvkm_object **);
#define _nvkm_pmu_dtor _nvkm_subdev_dtor
int _nvkm_pmu_init(struct nvkm_object *);
int _nvkm_pmu_fini(struct nvkm_object *, bool);
void nvkm_pmu_pgob(struct nvkm_pmu *pmu, bool enable);

struct nvkm_pmu_impl {
	struct nvkm_oclass base;
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
#endif
