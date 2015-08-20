#ifndef __NVKM_SW_CHAN_H__
#define __NVKM_SW_CHAN_H__
#include "priv.h"
#include <core/engctx.h>
#include <core/event.h>

struct nvkm_sw_chan {
	struct nvkm_engctx base;
	struct nvkm_event event;
};

#define nvkm_sw_context_create(p,e,c,d)                               \
	nvkm_sw_chan_ctor((p), (e), (c), sizeof(**d), (void **)d)
int nvkm_sw_chan_ctor(struct nvkm_object *, struct nvkm_object *,
		      struct nvkm_oclass *, int, void **);
void nvkm_sw_chan_dtor(struct nvkm_object *);
#define nvkm_sw_context_init(d)                                       \
	nvkm_engctx_init(&(d)->base)
#define nvkm_sw_context_fini(d,s)                                     \
	nvkm_engctx_fini(&(d)->base, (s))

#define _nvkm_sw_context_dtor nvkm_sw_chan_dtor
#define _nvkm_sw_context_init _nvkm_engctx_init
#define _nvkm_sw_context_fini _nvkm_engctx_fini
#endif
