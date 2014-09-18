#ifndef __NVKM_FIFO_NVE0_H__
#define __NVKM_FIFO_NVE0_H__

#include <engine/fifo.h>

int  nve0_fifo_ctor(struct nouveau_object *, struct nouveau_object *,
		    struct nouveau_oclass *, void *, u32,
		    struct nouveau_object **);
void nve0_fifo_dtor(struct nouveau_object *);
int  nve0_fifo_init(struct nouveau_object *);
int  nve0_fifo_fini(struct nouveau_object *, bool);

struct nve0_fifo_impl {
	struct nouveau_oclass base;
	u32 channels;
};

#endif
