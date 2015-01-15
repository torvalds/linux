#ifndef __NOUVEAU_FUSE_H__
#define __NOUVEAU_FUSE_H__

#include <core/subdev.h>
#include <core/device.h>

struct nouveau_fuse {
	struct nouveau_subdev base;
};

static inline struct nouveau_fuse *
nouveau_fuse(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_FUSE];
}

#define nouveau_fuse_create(p, e, o, d)                                        \
	nouveau_fuse_create_((p), (e), (o), sizeof(**d), (void **)d)

int  nouveau_fuse_create_(struct nouveau_object *, struct nouveau_object *,
			  struct nouveau_oclass *, int, void **);
void _nouveau_fuse_dtor(struct nouveau_object *);
int  _nouveau_fuse_init(struct nouveau_object *);
#define _nouveau_fuse_fini _nouveau_subdev_fini

extern struct nouveau_oclass g80_fuse_oclass;
extern struct nouveau_oclass gf100_fuse_oclass;
extern struct nouveau_oclass gm107_fuse_oclass;

#endif
