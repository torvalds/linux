#ifndef __NOUVEAU_MC_H__
#define __NOUVEAU_MC_H__

#include <core/subdev.h>
#include <core/device.h>

struct nouveau_mc {
	struct nouveau_subdev base;
	bool use_msi;
	unsigned int irq;
	void (*unk260)(struct nouveau_mc *, u32);
};

static inline struct nouveau_mc *
nouveau_mc(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_MC];
}

extern struct nouveau_oclass *nv04_mc_oclass;
extern struct nouveau_oclass *nv40_mc_oclass;
extern struct nouveau_oclass *nv44_mc_oclass;
extern struct nouveau_oclass *nv4c_mc_oclass;
extern struct nouveau_oclass *nv50_mc_oclass;
extern struct nouveau_oclass *nv94_mc_oclass;
extern struct nouveau_oclass *nv98_mc_oclass;
extern struct nouveau_oclass *nvc0_mc_oclass;
extern struct nouveau_oclass *nvc3_mc_oclass;
extern struct nouveau_oclass *gk20a_mc_oclass;

#endif
