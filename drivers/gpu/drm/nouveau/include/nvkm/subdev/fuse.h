#ifndef __NVKM_FUSE_H__
#define __NVKM_FUSE_H__
#include <core/subdev.h>
#include <core/device.h>

struct nvkm_fuse {
	struct nvkm_subdev base;
};

static inline struct nvkm_fuse *
nvkm_fuse(void *obj)
{
	return (void *)nvkm_subdev(obj, NVDEV_SUBDEV_FUSE);
}

#define nvkm_fuse_create(p, e, o, d)                                        \
	nvkm_fuse_create_((p), (e), (o), sizeof(**d), (void **)d)

int  nvkm_fuse_create_(struct nvkm_object *, struct nvkm_object *,
			  struct nvkm_oclass *, int, void **);
void _nvkm_fuse_dtor(struct nvkm_object *);
int  _nvkm_fuse_init(struct nvkm_object *);
#define _nvkm_fuse_fini _nvkm_subdev_fini

extern struct nvkm_oclass nv50_fuse_oclass;
extern struct nvkm_oclass gf100_fuse_oclass;
extern struct nvkm_oclass gm107_fuse_oclass;
#endif
