#ifndef __NVKM_DISP_H__
#define __NVKM_DISP_H__
#define nvkm_disp(p) container_of((p), struct nvkm_disp, engine)
#include <core/engine.h>
#include <core/event.h>

struct nvkm_disp {
	struct nvkm_engine engine;
	const struct nvkm_disp_func *func;

	struct nvkm_oproxy *client;

	struct list_head outp;
	struct list_head conn;

	struct nvkm_event hpd;
	struct nvkm_event vblank;
};

struct nvkm_disp_func {
	const struct nvkm_disp_oclass *root;
};

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
