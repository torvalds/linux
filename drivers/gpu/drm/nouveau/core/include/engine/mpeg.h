#ifndef __NOUVEAU_MPEG_H__
#define __NOUVEAU_MPEG_H__

#include <core/engine.h>
#include <core/engctx.h>

struct nouveau_mpeg_chan {
	struct nouveau_engctx base;
};

#define nouveau_mpeg_context_create(p,e,c,g,s,a,f,d)                           \
	nouveau_engctx_create((p), (e), (c), (g), (s), (a), (f), (d))
#define nouveau_mpeg_context_destroy(d)                                        \
	nouveau_engctx_destroy(&(d)->base)
#define nouveau_mpeg_context_init(d)                                           \
	nouveau_engctx_init(&(d)->base)
#define nouveau_mpeg_context_fini(d,s)                                         \
	nouveau_engctx_fini(&(d)->base, (s))

#define _nouveau_mpeg_context_dtor _nouveau_engctx_dtor
#define _nouveau_mpeg_context_init _nouveau_engctx_init
#define _nouveau_mpeg_context_fini _nouveau_engctx_fini
#define _nouveau_mpeg_context_rd32 _nouveau_engctx_rd32
#define _nouveau_mpeg_context_wr32 _nouveau_engctx_wr32

struct nouveau_mpeg {
	struct nouveau_engine base;
};

#define nouveau_mpeg_create(p,e,c,d)                                           \
	nouveau_engine_create((p), (e), (c), true, "PMPEG", "mpeg", (d))
#define nouveau_mpeg_destroy(d)                                                \
	nouveau_engine_destroy(&(d)->base)
#define nouveau_mpeg_init(d)                                                   \
	nouveau_engine_init(&(d)->base)
#define nouveau_mpeg_fini(d,s)                                                 \
	nouveau_engine_fini(&(d)->base, (s))

#define _nouveau_mpeg_dtor _nouveau_engine_dtor
#define _nouveau_mpeg_init _nouveau_engine_init
#define _nouveau_mpeg_fini _nouveau_engine_fini

extern struct nouveau_oclass nv31_mpeg_oclass;
extern struct nouveau_oclass nv40_mpeg_oclass;
extern struct nouveau_oclass nv50_mpeg_oclass;
extern struct nouveau_oclass nv84_mpeg_oclass;

extern struct nouveau_oclass nv31_mpeg_sclass[];
void nv31_mpeg_intr(struct nouveau_subdev *);
void nv31_mpeg_tile_prog(struct nouveau_engine *, int);
int  nv31_mpeg_init(struct nouveau_object *);

extern struct nouveau_ofuncs nv50_mpeg_ofuncs;
int  nv50_mpeg_context_ctor(struct nouveau_object *, struct nouveau_object *,
			    struct nouveau_oclass *, void *, u32,
			    struct nouveau_object **);
void nv50_mpeg_intr(struct nouveau_subdev *);
int  nv50_mpeg_init(struct nouveau_object *);

#endif
