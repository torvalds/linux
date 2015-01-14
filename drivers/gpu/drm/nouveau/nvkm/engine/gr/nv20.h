#ifndef __NV20_GR_H__
#define __NV20_GR_H__

#include <core/enum.h>

#include <engine/gr.h>
#include <engine/fifo.h>

struct nv20_gr_priv {
	struct nouveau_gr base;
	struct nouveau_gpuobj *ctxtab;
};

struct nv20_gr_chan {
	struct nouveau_gr_chan base;
	int chid;
};

extern struct nouveau_oclass nv25_gr_sclass[];
int  nv20_gr_context_init(struct nouveau_object *);
int  nv20_gr_context_fini(struct nouveau_object *, bool);

void nv20_gr_tile_prog(struct nouveau_engine *, int);
void nv20_gr_intr(struct nouveau_subdev *);

void nv20_gr_dtor(struct nouveau_object *);
int  nv20_gr_init(struct nouveau_object *);

int  nv30_gr_init(struct nouveau_object *);

#endif
