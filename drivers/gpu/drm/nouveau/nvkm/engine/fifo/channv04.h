#ifndef __NV04_FIFO_CHAN_H__
#define __NV04_FIFO_CHAN_H__
#include "chan.h"
#include "nv04.h"

struct nv04_fifo_chan {
	struct nvkm_fifo_chan base;
	u32 subc[8];
	u32 ramfc;
};

int  nv04_fifo_object_attach(struct nvkm_object *, struct nvkm_object *, u32);
void nv04_fifo_object_detach(struct nvkm_object *, int);

void nv04_fifo_chan_dtor(struct nvkm_object *);
int  nv04_fifo_chan_init(struct nvkm_object *);
int  nv04_fifo_chan_fini(struct nvkm_object *, bool suspend);

extern struct nvkm_oclass nv04_fifo_cclass;
extern struct nvkm_oclass nv04_fifo_sclass[];
extern struct nvkm_oclass nv10_fifo_sclass[];
extern struct nvkm_oclass nv17_fifo_sclass[];
extern struct nvkm_oclass nv40_fifo_sclass[];
#endif
