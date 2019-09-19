/* SPDX-License-Identifier: MIT */
#ifndef __NV20_GR_H__
#define __NV20_GR_H__
#define nv20_gr(p) container_of((p), struct nv20_gr, base)
#include "priv.h"

struct nv20_gr {
	struct nvkm_gr base;
	struct nvkm_memory *ctxtab;
};

int nv20_gr_new_(const struct nvkm_gr_func *, struct nvkm_device *,
		 int, struct nvkm_gr **);
void *nv20_gr_dtor(struct nvkm_gr *);
int nv20_gr_oneinit(struct nvkm_gr *);
int nv20_gr_init(struct nvkm_gr *);
void nv20_gr_intr(struct nvkm_gr *);
void nv20_gr_tile(struct nvkm_gr *, int, struct nvkm_fb_tile *);

int nv30_gr_init(struct nvkm_gr *);

#define nv20_gr_chan(p) container_of((p), struct nv20_gr_chan, object)
#include <core/object.h>

struct nv20_gr_chan {
	struct nvkm_object object;
	struct nv20_gr *gr;
	int chid;
	struct nvkm_memory *inst;
};

void *nv20_gr_chan_dtor(struct nvkm_object *);
int nv20_gr_chan_init(struct nvkm_object *);
int nv20_gr_chan_fini(struct nvkm_object *, bool);
#endif
