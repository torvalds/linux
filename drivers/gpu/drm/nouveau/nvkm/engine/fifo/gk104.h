#ifndef __NVKM_FIFO_NVE0_H__
#define __NVKM_FIFO_NVE0_H__
#include <engine/fifo.h>

int  gk104_fifo_ctor(struct nvkm_object *, struct nvkm_object *,
		    struct nvkm_oclass *, void *, u32,
		    struct nvkm_object **);
void gk104_fifo_dtor(struct nvkm_object *);
int  gk104_fifo_init(struct nvkm_object *);
int  gk104_fifo_fini(struct nvkm_object *, bool);

struct gk104_fifo_impl {
	struct nvkm_oclass base;
	u32 channels;
};
#endif
