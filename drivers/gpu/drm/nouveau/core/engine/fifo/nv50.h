#ifndef __NV50_FIFO_H__
#define __NV50_FIFO_H__

struct nv50_fifo_priv {
	struct nouveau_fifo base;
	struct nouveau_gpuobj *playlist[2];
	int cur_playlist;
};

struct nv50_fifo_base {
	struct nouveau_fifo_base base;
	struct nouveau_gpuobj *ramfc;
	struct nouveau_gpuobj *cache;
	struct nouveau_gpuobj *eng;
	struct nouveau_gpuobj *pgd;
	struct nouveau_vm *vm;
};

struct nv50_fifo_chan {
	struct nouveau_fifo_chan base;
	u32 subc[8];
	struct nouveau_ramht *ramht;
};

void nv50_fifo_playlist_update(struct nv50_fifo_priv *);

void nv50_fifo_object_detach(struct nouveau_object *, int);
void nv50_fifo_chan_dtor(struct nouveau_object *);
int  nv50_fifo_chan_fini(struct nouveau_object *, bool);

void nv50_fifo_context_dtor(struct nouveau_object *);

void nv50_fifo_dtor(struct nouveau_object *);
int  nv50_fifo_init(struct nouveau_object *);

#endif
