#ifndef __NVKM_MPEG_H__
#define __NVKM_MPEG_H__
#include <core/engctx.h>

struct nvkm_mpeg_chan {
	struct nvkm_engctx base;
};

#define nvkm_mpeg_context_create(p,e,c,g,s,a,f,d)                           \
	nvkm_engctx_create((p), (e), (c), (g), (s), (a), (f), (d))
#define nvkm_mpeg_context_destroy(d)                                        \
	nvkm_engctx_destroy(&(d)->base)
#define nvkm_mpeg_context_init(d)                                           \
	nvkm_engctx_init(&(d)->base)
#define nvkm_mpeg_context_fini(d,s)                                         \
	nvkm_engctx_fini(&(d)->base, (s))

#define _nvkm_mpeg_context_dtor _nvkm_engctx_dtor
#define _nvkm_mpeg_context_init _nvkm_engctx_init
#define _nvkm_mpeg_context_fini _nvkm_engctx_fini
#define _nvkm_mpeg_context_rd32 _nvkm_engctx_rd32
#define _nvkm_mpeg_context_wr32 _nvkm_engctx_wr32

#include <core/engine.h>

struct nvkm_mpeg {
	struct nvkm_engine base;
};

#define nvkm_mpeg_create(p,e,c,d)                                           \
	nvkm_engine_create((p), (e), (c), true, "PMPEG", "mpeg", (d))
#define nvkm_mpeg_destroy(d)                                                \
	nvkm_engine_destroy(&(d)->base)
#define nvkm_mpeg_init(d)                                                   \
	nvkm_engine_init(&(d)->base)
#define nvkm_mpeg_fini(d,s)                                                 \
	nvkm_engine_fini(&(d)->base, (s))

#define _nvkm_mpeg_dtor _nvkm_engine_dtor
#define _nvkm_mpeg_init _nvkm_engine_init
#define _nvkm_mpeg_fini _nvkm_engine_fini

extern struct nvkm_oclass nv31_mpeg_oclass;
extern struct nvkm_oclass nv40_mpeg_oclass;
extern struct nvkm_oclass nv44_mpeg_oclass;
extern struct nvkm_oclass nv50_mpeg_oclass;
extern struct nvkm_oclass g84_mpeg_oclass;
extern struct nvkm_ofuncs nv31_mpeg_ofuncs;
extern struct nvkm_oclass nv31_mpeg_cclass;
extern struct nvkm_oclass nv31_mpeg_sclass[];
extern struct nvkm_oclass nv40_mpeg_sclass[];
void nv31_mpeg_intr(struct nvkm_subdev *);
void nv31_mpeg_tile_prog(struct nvkm_engine *, int);
int  nv31_mpeg_init(struct nvkm_object *);

extern struct nvkm_ofuncs nv50_mpeg_ofuncs;
int  nv50_mpeg_context_ctor(struct nvkm_object *, struct nvkm_object *,
			    struct nvkm_oclass *, void *, u32,
			    struct nvkm_object **);
void nv50_mpeg_intr(struct nvkm_subdev *);
int  nv50_mpeg_init(struct nvkm_object *);
#endif
