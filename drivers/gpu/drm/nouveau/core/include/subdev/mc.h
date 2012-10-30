#ifndef __NOUVEAU_MC_H__
#define __NOUVEAU_MC_H__

#include <core/subdev.h>
#include <core/device.h>

struct nouveau_mc_intr {
	u32 stat;
	u32 unit;
};

struct nouveau_mc {
	struct nouveau_subdev base;
	const struct nouveau_mc_intr *intr_map;
};

static inline struct nouveau_mc *
nouveau_mc(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_MC];
}

#define nouveau_mc_create(p,e,o,d)                                             \
	nouveau_subdev_create_((p), (e), (o), 0, "PMC", "master",              \
			       sizeof(**d), (void **)d)
#define nouveau_mc_destroy(p)                                                  \
	nouveau_subdev_destroy(&(p)->base)
#define nouveau_mc_init(p)                                                     \
	nouveau_subdev_init(&(p)->base)
#define nouveau_mc_fini(p,s)                                                   \
	nouveau_subdev_fini(&(p)->base, (s))

#define _nouveau_mc_dtor _nouveau_subdev_dtor
#define _nouveau_mc_init _nouveau_subdev_init
#define _nouveau_mc_fini _nouveau_subdev_fini

extern struct nouveau_oclass nv04_mc_oclass;
extern struct nouveau_oclass nv44_mc_oclass;
extern struct nouveau_oclass nv50_mc_oclass;
extern struct nouveau_oclass nv98_mc_oclass;
extern struct nouveau_oclass nvc0_mc_oclass;

void nouveau_mc_intr(struct nouveau_subdev *);

extern const struct nouveau_mc_intr nv04_mc_intr[];
int nv04_mc_init(struct nouveau_object *);
int nv50_mc_init(struct nouveau_object *);

#endif
