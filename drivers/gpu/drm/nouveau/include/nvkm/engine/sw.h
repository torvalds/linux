#ifndef __NVKM_SW_H__
#define __NVKM_SW_H__
#include <core/engctx.h>

struct nvkm_sw_chan {
	struct nvkm_engctx base;

	int (*flip)(void *);
	void *flip_data;
};

#define nvkm_sw_context_create(p,e,c,d)                               \
	nvkm_engctx_create((p), (e), (c), (p), 0, 0, 0, (d))
#define nvkm_sw_context_destroy(d)                                    \
	nvkm_engctx_destroy(&(d)->base)
#define nvkm_sw_context_init(d)                                       \
	nvkm_engctx_init(&(d)->base)
#define nvkm_sw_context_fini(d,s)                                     \
	nvkm_engctx_fini(&(d)->base, (s))

#define _nvkm_sw_context_dtor _nvkm_engctx_dtor
#define _nvkm_sw_context_init _nvkm_engctx_init
#define _nvkm_sw_context_fini _nvkm_engctx_fini

#include <core/engine.h>

struct nvkm_sw {
	struct nvkm_engine base;
};

#define nvkm_sw_create(p,e,c,d)                                       \
	nvkm_engine_create((p), (e), (c), true, "SW", "software", (d))
#define nvkm_sw_destroy(d)                                            \
	nvkm_engine_destroy(&(d)->base)
#define nvkm_sw_init(d)                                               \
	nvkm_engine_init(&(d)->base)
#define nvkm_sw_fini(d,s)                                             \
	nvkm_engine_fini(&(d)->base, (s))

#define _nvkm_sw_dtor _nvkm_engine_dtor
#define _nvkm_sw_init _nvkm_engine_init
#define _nvkm_sw_fini _nvkm_engine_fini

extern struct nvkm_oclass *nv04_sw_oclass;
extern struct nvkm_oclass *nv10_sw_oclass;
extern struct nvkm_oclass *nv50_sw_oclass;
extern struct nvkm_oclass *gf100_sw_oclass;

void nv04_sw_intr(struct nvkm_subdev *);
#endif
