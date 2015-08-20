#ifndef __NVKM_GR_PRIV_H__
#define __NVKM_GR_PRIV_H__
#define nvkm_gr(p) container_of((p), struct nvkm_gr, engine)
#include <engine/gr.h>
struct nvkm_fifo_chan;

struct nvkm_gr_func {
	int (*chan_new)(struct nvkm_gr *, struct nvkm_fifo_chan *,
			const struct nvkm_oclass *, struct nvkm_object **);
	int (*object_get)(struct nvkm_gr *, int, struct nvkm_sclass *);
	struct nvkm_sclass sclass[];
};

extern const struct nvkm_object_func nv04_gr_object;
#endif
