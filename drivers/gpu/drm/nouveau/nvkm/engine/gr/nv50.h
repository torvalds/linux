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

int nv50_gr_new_(const struct nvkm_gr_func *, struct nvkm_device *, int index,
		 struct nvkm_gr **);
int nv50_gr_init(struct nvkm_gr *);
void nv50_gr_intr(struct nvkm_gr *);
u64 nv50_gr_units(struct nvkm_gr *);

int g84_gr_tlb_flush(struct nvkm_gr *);

#define nv50_gr_chan(p) container_of((p), struct nv50_gr_chan, object)

struct nv50_gr_chan {
	struct nvkm_object object;
	struct nv50_gr *gr;
};

int nv50_gr_chan_new(struct nvkm_gr *, struct nvkm_fifo_chan *,
		     const struct nvkm_oclass *, struct nvkm_object **);

extern const struct nvkm_object_func nv50_gr_object;

int  nv50_grctx_init(struct nvkm_device *, u32 *size);
void nv50_grctx_fill(struct nvkm_device *, struct nvkm_gpuobj *);
#endif
