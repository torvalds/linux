#ifndef __NOUVEAU_BUS_H__
#define __NOUVEAU_BUS_H__

#include <core/subdev.h>
#include <core/device.h>

struct nouveau_bus_intr {
	u32 stat;
	u32 unit;
};

struct nouveau_bus {
	struct nouveau_subdev base;
};

static inline struct nouveau_bus *
nouveau_bus(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_BUS];
}

#define nouveau_bus_create(p, e, o, d)                                         \
	nouveau_subdev_create_((p), (e), (o), 0, "PBUS", "master",             \
			       sizeof(**d), (void **)d)
#define nouveau_bus_destroy(p)                                                 \
	nouveau_subdev_destroy(&(p)->base)
#define nouveau_bus_init(p)                                                    \
	nouveau_subdev_init(&(p)->base)
#define nouveau_bus_fini(p, s)                                                 \
	nouveau_subdev_fini(&(p)->base, (s))

#define _nouveau_bus_dtor _nouveau_subdev_dtor
#define _nouveau_bus_init _nouveau_subdev_init
#define _nouveau_bus_fini _nouveau_subdev_fini

extern struct nouveau_oclass nv04_bus_oclass;
extern struct nouveau_oclass nv31_bus_oclass;
extern struct nouveau_oclass nv50_bus_oclass;
extern struct nouveau_oclass nvc0_bus_oclass;

#endif
