#ifndef __NVKM_DISP_H__
#define __NVKM_DISP_H__
#define nvkm_disp(p) container_of((p), struct nvkm_disp, engine)
#include <core/engine.h>
#include <core/event.h>

struct nvkm_disp {
	const struct nvkm_disp_func *func;
	struct nvkm_engine engine;

	struct nvkm_oproxy *client;

	struct list_head outp;
	struct list_head conn;

	struct nvkm_event hpd;
	struct nvkm_event vblank;

	struct {
		int nr;
	} head;
};

int nv04_disp_new(struct nvkm_device *, int, struct nvkm_disp **);
int nv50_disp_new(struct nvkm_device *, int, struct nvkm_disp **);
int g84_disp_new(struct nvkm_device *, int, struct nvkm_disp **);
int gt200_disp_new(struct nvkm_device *, int, struct nvkm_disp **);
int g94_disp_new(struct nvkm_device *, int, struct nvkm_disp **);
int gt215_disp_new(struct nvkm_device *, int, struct nvkm_disp **);
int gf119_disp_new(struct nvkm_device *, int, struct nvkm_disp **);
int gk104_disp_new(struct nvkm_device *, int, struct nvkm_disp **);
int gk110_disp_new(struct nvkm_device *, int, struct nvkm_disp **);
int gm107_disp_new(struct nvkm_device *, int, struct nvkm_disp **);
int gm200_disp_new(struct nvkm_device *, int, struct nvkm_disp **);
#endif
