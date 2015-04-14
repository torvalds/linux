#ifndef __NVKM_ENGINE_H__
#define __NVKM_ENGINE_H__
#include <core/subdev.h>

#define NV_ENGINE_(eng,var) (NV_ENGINE_CLASS | ((var) << 8) | (eng))
#define NV_ENGINE(name,var)  NV_ENGINE_(NVDEV_ENGINE_##name, (var))

struct nvkm_engine {
	struct nvkm_subdev subdev;
	struct nvkm_oclass *cclass;
	struct nvkm_oclass *sclass;

	struct list_head contexts;
	spinlock_t lock;

	void (*tile_prog)(struct nvkm_engine *, int region);
	int  (*tlb_flush)(struct nvkm_engine *);
};

static inline struct nvkm_engine *
nv_engine(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (unlikely(!nv_iclass(obj, NV_ENGINE_CLASS)))
		nv_assert("BAD CAST -> NvEngine, %08x", nv_hclass(obj));
#endif
	return obj;
}

static inline int
nv_engidx(struct nvkm_engine *engine)
{
	return nv_subidx(&engine->subdev);
}

struct nvkm_engine *nvkm_engine(void *obj, int idx);

#define nvkm_engine_create(p,e,c,d,i,f,r)                                   \
	nvkm_engine_create_((p), (e), (c), (d), (i), (f),                   \
			       sizeof(**r),(void **)r)

#define nvkm_engine_destroy(p)                                              \
	nvkm_subdev_destroy(&(p)->subdev)
#define nvkm_engine_init(p)                                                 \
	nvkm_subdev_init(&(p)->subdev)
#define nvkm_engine_fini(p,s)                                               \
	nvkm_subdev_fini(&(p)->subdev, (s))

int nvkm_engine_create_(struct nvkm_object *, struct nvkm_object *,
			   struct nvkm_oclass *, bool, const char *,
			   const char *, int, void **);

#define _nvkm_engine_dtor _nvkm_subdev_dtor
#define _nvkm_engine_init _nvkm_subdev_init
#define _nvkm_engine_fini _nvkm_subdev_fini
#endif
