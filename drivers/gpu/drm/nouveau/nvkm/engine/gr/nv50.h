#ifndef __NV50_GR_H__
#define __NV50_GR_H__
#define nv50_gr(p) container_of((p), struct nv50_gr, base)
#include "priv.h"

struct nv50_gr {
	struct nvkm_gr base;
	const struct nv50_gr_func *func;
	spinlock_t lock;
	u32 size;
};

struct nv50_gr_func {
	void *(*dtor)(struct nv50_gr *);
	struct nvkm_sclass sclass[];
};

#define nv50_gr_chan(p) container_of((p), struct nv50_gr_chan, object)

struct nv50_gr_chan {
	struct nvkm_object object;
	struct nv50_gr *gr;
};

int  nv50_grctx_init(struct nvkm_device *, u32 *size);
void nv50_grctx_fill(struct nvkm_device *, struct nvkm_gpuobj *);
#endif
