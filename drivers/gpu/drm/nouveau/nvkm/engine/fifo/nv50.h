#ifndef __NV50_FIFO_H__
#define __NV50_FIFO_H__
#include <engine/fifo.h>

struct nv50_fifo_priv {
	struct nvkm_fifo base;
	struct nvkm_gpuobj *playlist[2];
	int cur_playlist;
};

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

void nv50_fifo_playlist_update(struct nv50_fifo_priv *);

void nv50_fifo_object_detach(struct nvkm_object *, int);
void nv50_fifo_chan_dtor(struct nvkm_object *);
int  nv50_fifo_chan_fini(struct nvkm_object *, bool);

void nv50_fifo_context_dtor(struct nvkm_object *);

void nv50_fifo_dtor(struct nvkm_object *);
int  nv50_fifo_init(struct nvkm_object *);
#endif
