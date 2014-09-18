#ifndef __NOUVEAU_ENGINE_H__
#define __NOUVEAU_ENGINE_H__

#include <core/object.h>
#include <core/subdev.h>

#define NV_ENGINE_(eng,var) (NV_ENGINE_CLASS | ((var) << 8) | (eng))
#define NV_ENGINE(name,var)  NV_ENGINE_(NVDEV_ENGINE_##name, (var))

struct nouveau_engine {
	struct nouveau_subdev base;
	struct nouveau_oclass *cclass;
	struct nouveau_oclass *sclass;

	struct list_head contexts;
	spinlock_t lock;

	void (*tile_prog)(struct nouveau_engine *, int region);
	int  (*tlb_flush)(struct nouveau_engine *);
};

static inline struct nouveau_engine *
nv_engine(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (unlikely(!nv_iclass(obj, NV_ENGINE_CLASS)))
		nv_assert("BAD CAST -> NvEngine, %08x", nv_hclass(obj));
#endif
	return obj;
}

static inline int
nv_engidx(struct nouveau_object *object)
{
	return nv_subidx(object);
}

#define nouveau_engine_create(p,e,c,d,i,f,r)                                   \
	nouveau_engine_create_((p), (e), (c), (d), (i), (f),                   \
			       sizeof(**r),(void **)r)

#define nouveau_engine_destroy(p)                                              \
	nouveau_subdev_destroy(&(p)->base)
#define nouveau_engine_init(p)                                                 \
	nouveau_subdev_init(&(p)->base)
#define nouveau_engine_fini(p,s)                                               \
	nouveau_subdev_fini(&(p)->base, (s))

int nouveau_engine_create_(struct nouveau_object *, struct nouveau_object *,
			   struct nouveau_oclass *, bool, const char *,
			   const char *, int, void **);

#define _nouveau_engine_dtor _nouveau_subdev_dtor
#define _nouveau_engine_init _nouveau_subdev_init
#define _nouveau_engine_fini _nouveau_subdev_fini

#endif
