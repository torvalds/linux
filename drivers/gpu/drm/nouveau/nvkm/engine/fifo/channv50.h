#ifndef __NV50_FIFO_CHAN_H__
#define __NV50_FIFO_CHAN_H__
#include "chan.h"
#include "nv50.h"

struct nv50_fifo_base {
	struct nvkm_fifo_base base;
	struct nvkm_gpuobj *ramfc;
	struct nvkm_gpuobj *cache;
	struct nvkm_gpuobj *eng;
	struct nvkm_gpuobj *pgd;
	struct nvkm_vm *vm;
};

struct nv50_fifo_chan {
	struct nvkm_fifo_chan base;
	u32 subc[8];
	struct nvkm_ramht *ramht;
};

extern struct nvkm_oclass nv50_fifo_cclass;
extern struct nvkm_oclass nv50_fifo_sclass[];
void nv50_fifo_context_dtor(struct nvkm_object *);
void nv50_fifo_chan_dtor(struct nvkm_object *);
int  nv50_fifo_chan_init(struct nvkm_object *);
int  nv50_fifo_chan_fini(struct nvkm_object *, bool);
int  nv50_fifo_context_attach(struct nvkm_object *, struct nvkm_object *);
int  nv50_fifo_context_detach(struct nvkm_object *, bool,
			      struct nvkm_object *);
int  nv50_fifo_object_attach(struct nvkm_object *, struct nvkm_object *, u32);
void nv50_fifo_object_detach(struct nvkm_object *, int);
extern struct nvkm_ofuncs nv50_fifo_ofuncs_ind;

extern struct nvkm_oclass g84_fifo_cclass;
extern struct nvkm_oclass g84_fifo_sclass[];
int  g84_fifo_chan_init(struct nvkm_object *);
int  g84_fifo_context_attach(struct nvkm_object *, struct nvkm_object *);
int  g84_fifo_context_detach(struct nvkm_object *, bool,
			     struct nvkm_object *);
int  g84_fifo_object_attach(struct nvkm_object *, struct nvkm_object *, u32);
extern struct nvkm_ofuncs g84_fifo_ofuncs_ind;
#endif
