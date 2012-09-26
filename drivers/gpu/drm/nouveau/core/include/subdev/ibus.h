#ifndef __NOUVEAU_IBUS_H__
#define __NOUVEAU_IBUS_H__

#include <core/subdev.h>
#include <core/device.h>

struct nouveau_ibus {
	struct nouveau_subdev base;
};

static inline struct nouveau_ibus *
nouveau_ibus(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_IBUS];
}

#define nouveau_ibus_create(p,e,o,d)                                           \
	nouveau_subdev_create_((p), (e), (o), 0, "PIBUS", "ibus",              \
			       sizeof(**d), (void **)d)
#define nouveau_ibus_destroy(p)                                                \
	nouveau_subdev_destroy(&(p)->base)
#define nouveau_ibus_init(p)                                                   \
	nouveau_subdev_init(&(p)->base)
#define nouveau_ibus_fini(p,s)                                                 \
	nouveau_subdev_fini(&(p)->base, (s))

#define _nouveau_ibus_dtor _nouveau_subdev_dtor
#define _nouveau_ibus_init _nouveau_subdev_init
#define _nouveau_ibus_fini _nouveau_subdev_fini

extern struct nouveau_oclass nvc0_ibus_oclass;
extern struct nouveau_oclass nve0_ibus_oclass;

#endif
