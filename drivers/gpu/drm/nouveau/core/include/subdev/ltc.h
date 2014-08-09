#ifndef __NOUVEAU_LTC_H__
#define __NOUVEAU_LTC_H__

#include <core/subdev.h>
#include <core/device.h>

struct nouveau_mm_node;

struct nouveau_ltc {
	struct nouveau_subdev base;

	int  (*tags_alloc)(struct nouveau_ltc *, u32 count,
	                   struct nouveau_mm_node **);
	void (*tags_free)(struct nouveau_ltc *, struct nouveau_mm_node **);
	void (*tags_clear)(struct nouveau_ltc *, u32 first, u32 count);
};

static inline struct nouveau_ltc *
nouveau_ltc(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_LTC];
}

extern struct nouveau_oclass *gf100_ltc_oclass;
extern struct nouveau_oclass *gk104_ltc_oclass;
extern struct nouveau_oclass *gm107_ltc_oclass;

#endif
