#ifndef __NVKM_ENGINE_H__
#define __NVKM_ENGINE_H__
#include <core/subdev.h>
struct nvkm_device_oclass; /*XXX: DEV!ENG */
struct nvkm_fifo_chan;

#define NV_ENGINE_(eng,var) (((var) << 8) | (eng))
#define NV_ENGINE(name,var)  NV_ENGINE_(NVDEV_ENGINE_##name, (var))

struct nvkm_engine {
	struct nvkm_subdev subdev;
	const struct nvkm_engine_func *func;

	int usecount;

	struct nvkm_oclass *cclass;
	struct nvkm_oclass *sclass;

	struct list_head contexts;
	spinlock_t lock;

	void (*tile_prog)(struct nvkm_engine *, int region);
	int  (*tlb_flush)(struct nvkm_engine *);
};

struct nvkm_engine_func {
	void *(*dtor)(struct nvkm_engine *);
	int (*oneinit)(struct nvkm_engine *);
	int (*init)(struct nvkm_engine *);
	int (*fini)(struct nvkm_engine *, bool suspend);
	void (*intr)(struct nvkm_engine *);

	struct {
		int (*sclass)(struct nvkm_oclass *, int index,
			      const struct nvkm_device_oclass **);
	} base;

	struct {
		int (*cclass)(struct nvkm_fifo_chan *,
			      const struct nvkm_oclass *,
			      struct nvkm_object **);
		int (*sclass)(struct nvkm_oclass *, int index);
	} fifo;

	struct nvkm_sclass sclass[];
};

int nvkm_engine_ctor(const struct nvkm_engine_func *, struct nvkm_device *,
		     int index, u32 pmc_enable, bool enable,
		     struct nvkm_engine *);
int nvkm_engine_new_(const struct nvkm_engine_func *, struct nvkm_device *,
		     int index, u32 pmc_enable, bool enable,
		     struct nvkm_engine **);
struct nvkm_engine *nvkm_engine_ref(struct nvkm_engine *);
void nvkm_engine_unref(struct nvkm_engine **);

static inline struct nvkm_engine *
nv_engine(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	BUG_ON(!nv_iclass(obj, NV_ENGINE_CLASS));
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
#define nvkm_engine_init_old(p)                                                \
	nvkm_subdev_init_old(&(p)->subdev)
#define nvkm_engine_fini_old(p,s)                                              \
	nvkm_subdev_fini_old(&(p)->subdev, (s))

int nvkm_engine_create_(struct nvkm_object *, struct nvkm_object *,
			   struct nvkm_oclass *, bool, const char *,
			   const char *, int, void **);

#define _nvkm_engine_dtor _nvkm_subdev_dtor
#define _nvkm_engine_init _nvkm_subdev_init
#define _nvkm_engine_fini _nvkm_subdev_fini

#include <core/device.h>
#endif
