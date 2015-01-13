#ifndef __NVKM_PMU_PRIV_H__
#define __NVKM_PMU_PRIV_H__

#include <subdev/pmu.h>
#include <subdev/pmu/fuc/os.h>

#define nouveau_pmu_create(p, e, o, d)                                         \
	nouveau_pmu_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_pmu_destroy(p)                                                 \
	nouveau_subdev_destroy(&(p)->base)
#define nouveau_pmu_init(p) ({                                                 \
	struct nouveau_pmu *_pmu = (p);                                       \
	_nouveau_pmu_init(nv_object(_pmu));                                   \
})
#define nouveau_pmu_fini(p,s) ({                                               \
	struct nouveau_pmu *_pmu = (p);                                       \
	_nouveau_pmu_fini(nv_object(_pmu), (s));                              \
})

int nouveau_pmu_create_(struct nouveau_object *, struct nouveau_object *,
			struct nouveau_oclass *, int, void **);

int _nouveau_pmu_ctor(struct nouveau_object *, struct nouveau_object *,
		      struct nouveau_oclass *, void *, u32,
		      struct nouveau_object **);
#define _nouveau_pmu_dtor _nouveau_subdev_dtor
int _nouveau_pmu_init(struct nouveau_object *);
int _nouveau_pmu_fini(struct nouveau_object *, bool);
void nouveau_pmu_pgob(struct nouveau_pmu *pmu, bool enable);

struct nvkm_pmu_impl {
	struct nouveau_oclass base;
	struct {
		u32 *data;
		u32  size;
	} code;
	struct {
		u32 *data;
		u32  size;
	} data;

	void (*pgob)(struct nouveau_pmu *, bool);
};

#endif
