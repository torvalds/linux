#ifndef __NV20_GR_H__
#define __NV20_GR_H__
#define nv20_gr(p) container_of((p), struct nv20_gr, base)
#include "priv.h"

struct nv20_gr {
	struct nvkm_gr base;
	struct nvkm_memory *ctxtab;
};

#define nv20_gr_chan(p) container_of((p), struct nv20_gr_chan, object)

struct nv20_gr_chan {
	struct nvkm_object object;
	struct nv20_gr *gr;
	int chid;
	struct nvkm_memory *inst;
};

void *nv20_gr_chan_dtor(struct nvkm_object *);
int  nv20_gr_chan_init(struct nvkm_object *);
int  nv20_gr_chan_fini(struct nvkm_object *, bool);

void nv20_gr_tile_prog(struct nvkm_engine *, int);
void nv20_gr_intr(struct nvkm_subdev *);

void nv20_gr_dtor(struct nvkm_object *);
int  nv20_gr_init(struct nvkm_object *);

int  nv30_gr_init(struct nvkm_object *);
#endif
