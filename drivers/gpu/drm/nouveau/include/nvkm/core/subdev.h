#ifndef __NVKM_SUBDEV_H__
#define __NVKM_SUBDEV_H__
#include <core/object.h>

#define NV_SUBDEV_(sub,var) (NV_SUBDEV_CLASS | ((var) << 8) | (sub))
#define NV_SUBDEV(name,var)  NV_SUBDEV_(NVDEV_SUBDEV_##name, (var))

struct nvkm_subdev {
	struct nvkm_object object;

	struct nvkm_device *device;

	struct mutex mutex;
	const char *name;
	u32 debug;
	u32 unit;

	void (*intr)(struct nvkm_subdev *);
};

static inline struct nvkm_subdev *
nv_subdev(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (unlikely(!nv_iclass(obj, NV_SUBDEV_CLASS)))
		nv_assert("BAD CAST -> NvSubDev, %08x", nv_hclass(obj));
#endif
	return obj;
}

static inline int
nv_subidx(struct nvkm_subdev *subdev)
{
	return nv_hclass(subdev) & 0xff;
}

struct nvkm_subdev *nvkm_subdev(void *obj, int idx);

#define nvkm_subdev_create(p,e,o,v,s,f,d)                                   \
	nvkm_subdev_create_((p), (e), (o), (v), (s), (f),                   \
			       sizeof(**d),(void **)d)

int  nvkm_subdev_create_(struct nvkm_object *, struct nvkm_object *,
			    struct nvkm_oclass *, u32 pclass,
			    const char *sname, const char *fname,
			    int size, void **);
void nvkm_subdev_destroy(struct nvkm_subdev *);
int  nvkm_subdev_init(struct nvkm_subdev *);
int  nvkm_subdev_fini(struct nvkm_subdev *, bool suspend);
void nvkm_subdev_reset(struct nvkm_object *);

void _nvkm_subdev_dtor(struct nvkm_object *);
int  _nvkm_subdev_init(struct nvkm_object *);
int  _nvkm_subdev_fini(struct nvkm_object *, bool suspend);

#define s_printk(s,l,f,a...) do {                                              \
	if ((s)->debug >= OS_DBG_##l) {                                        \
		nv_printk((s)->base.parent, (s)->name, l, f, ##a);             \
	}                                                                      \
} while(0)

#include <core/engine.h>
#endif
