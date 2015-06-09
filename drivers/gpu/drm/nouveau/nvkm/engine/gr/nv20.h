#ifndef __NV20_GR_H__
#define __NV20_GR_H__
#include <engine/gr.h>

struct nv20_gr_priv {
	struct nvkm_gr base;
	struct nvkm_gpuobj *ctxtab;
};

struct nv20_gr_chan {
	struct nvkm_gr_chan base;
	int chid;
};

extern struct nvkm_oclass nv25_gr_sclass[];
int  nv20_gr_context_init(struct nvkm_object *);
int  nv20_gr_context_fini(struct nvkm_object *, bool);

void nv20_gr_tile_prog(struct nvkm_engine *, int);
void nv20_gr_intr(struct nvkm_subdev *);

void nv20_gr_dtor(struct nvkm_object *);
int  nv20_gr_init(struct nvkm_object *);

int  nv30_gr_init(struct nvkm_object *);
#endif
