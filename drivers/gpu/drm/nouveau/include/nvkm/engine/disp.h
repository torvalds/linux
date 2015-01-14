#ifndef __NVKM_DISP_H__
#define __NVKM_DISP_H__
#include <core/engine.h>
#include <core/event.h>

struct nvkm_disp {
	struct nvkm_engine base;

	struct list_head outp;

	struct nvkm_event hpd;
	struct nvkm_event vblank;
};

static inline struct nvkm_disp *
nvkm_disp(void *obj)
{
	return (void *)nvkm_engine(obj, NVDEV_ENGINE_DISP);
}

extern struct nvkm_oclass *nv04_disp_oclass;
extern struct nvkm_oclass *nv50_disp_oclass;
extern struct nvkm_oclass *g84_disp_oclass;
extern struct nvkm_oclass *gt200_disp_oclass;
extern struct nvkm_oclass *g94_disp_oclass;
extern struct nvkm_oclass *gt215_disp_oclass;
extern struct nvkm_oclass *gf110_disp_oclass;
extern struct nvkm_oclass *gk104_disp_oclass;
extern struct nvkm_oclass *gk110_disp_oclass;
extern struct nvkm_oclass *gm107_disp_oclass;
extern struct nvkm_oclass *gm204_disp_oclass;
#endif
