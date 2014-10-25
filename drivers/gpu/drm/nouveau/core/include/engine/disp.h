#ifndef __NOUVEAU_DISP_H__
#define __NOUVEAU_DISP_H__

#include <core/object.h>
#include <core/engine.h>
#include <core/device.h>
#include <core/event.h>

struct nouveau_disp {
	struct nouveau_engine base;

	struct list_head outp;

	struct nvkm_event hpd;
	struct nvkm_event vblank;
};

static inline struct nouveau_disp *
nouveau_disp(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_ENGINE_DISP];
}

extern struct nouveau_oclass *nv04_disp_oclass;
extern struct nouveau_oclass *nv50_disp_oclass;
extern struct nouveau_oclass *nv84_disp_oclass;
extern struct nouveau_oclass *nva0_disp_oclass;
extern struct nouveau_oclass *nv94_disp_oclass;
extern struct nouveau_oclass *nva3_disp_oclass;
extern struct nouveau_oclass *nvd0_disp_oclass;
extern struct nouveau_oclass *nve0_disp_oclass;
extern struct nouveau_oclass *nvf0_disp_oclass;
extern struct nouveau_oclass *gm107_disp_oclass;

#endif
