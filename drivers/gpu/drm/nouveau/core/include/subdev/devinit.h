#ifndef __NOUVEAU_DEVINIT_H__
#define __NOUVEAU_DEVINIT_H__

#include <core/subdev.h>
#include <core/device.h>

struct nouveau_devinit {
	struct nouveau_subdev base;
	bool post;
	void (*meminit)(struct nouveau_devinit *);
	int  (*pll_set)(struct nouveau_devinit *, u32 type, u32 freq);
	u32  (*mmio)(struct nouveau_devinit *, u32 addr);
};

static inline struct nouveau_devinit *
nouveau_devinit(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_DEVINIT];
}

extern struct nouveau_oclass *nv04_devinit_oclass;
extern struct nouveau_oclass *nv05_devinit_oclass;
extern struct nouveau_oclass *nv10_devinit_oclass;
extern struct nouveau_oclass *nv1a_devinit_oclass;
extern struct nouveau_oclass *nv20_devinit_oclass;
extern struct nouveau_oclass *nv50_devinit_oclass;
extern struct nouveau_oclass *nv84_devinit_oclass;
extern struct nouveau_oclass *nv98_devinit_oclass;
extern struct nouveau_oclass *nva3_devinit_oclass;
extern struct nouveau_oclass *nvaf_devinit_oclass;
extern struct nouveau_oclass *nvc0_devinit_oclass;
extern struct nouveau_oclass *gm107_devinit_oclass;

#endif
