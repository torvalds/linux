#ifndef __NOUVEAU_LTCG_H__
#define __NOUVEAU_LTCG_H__

#include <core/subdev.h>
#include <core/device.h>

struct nouveau_mm_node;

struct nouveau_ltcg {
	struct nouveau_subdev base;

	int  (*tags_alloc)(struct nouveau_ltcg *, u32 count,
	                   struct nouveau_mm_node **);
	void (*tags_free)(struct nouveau_ltcg *, struct nouveau_mm_node **);
	void (*tags_clear)(struct nouveau_ltcg *, u32 first, u32 count);
};

static inline struct nouveau_ltcg *
nouveau_ltcg(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_LTCG];
}

#define nouveau_ltcg_create(p,e,o,d)                                           \
	nouveau_subdev_create_((p), (e), (o), 0, "PLTCG", "level2",            \
			       sizeof(**d), (void **)d)
#define nouveau_ltcg_destroy(p)                                                \
	nouveau_subdev_destroy(&(p)->base)
#define nouveau_ltcg_init(p)                                                   \
	nouveau_subdev_init(&(p)->base)
#define nouveau_ltcg_fini(p,s)                                                 \
	nouveau_subdev_fini(&(p)->base, (s))

#define _nouveau_ltcg_dtor _nouveau_subdev_dtor
#define _nouveau_ltcg_init _nouveau_subdev_init
#define _nouveau_ltcg_fini _nouveau_subdev_fini

extern struct nouveau_oclass nvc0_ltcg_oclass;

#endif
