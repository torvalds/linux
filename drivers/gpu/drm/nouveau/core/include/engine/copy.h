#ifndef __NOUVEAU_COPY_H__
#define __NOUVEAU_COPY_H__

#include <core/engine.h>
#include <core/engctx.h>

struct nouveau_copy_chan {
	struct nouveau_engctx base;
};

#define nouveau_copy_context_create(p,e,c,g,s,a,f,d)                           \
	nouveau_engctx_create((p), (e), (c), (g), (s), (a), (f), (d))
#define nouveau_copy_context_destroy(d)                                        \
	nouveau_engctx_destroy(&(d)->base)
#define nouveau_copy_context_init(d)                                           \
	nouveau_engctx_init(&(d)->base)
#define nouveau_copy_context_fini(d,s)                                         \
	nouveau_engctx_fini(&(d)->base, (s))

#define _nouveau_copy_context_dtor _nouveau_engctx_dtor
#define _nouveau_copy_context_init _nouveau_engctx_init
#define _nouveau_copy_context_fini _nouveau_engctx_fini
#define _nouveau_copy_context_rd32 _nouveau_engctx_rd32
#define _nouveau_copy_context_wr32 _nouveau_engctx_wr32

struct nouveau_copy {
	struct nouveau_engine base;
};

#define nouveau_copy_create(p,e,c,y,i,d)                                       \
	nouveau_engine_create((p), (e), (c), (y), "PCE"#i, "copy"#i, (d))
#define nouveau_copy_destroy(d)                                                \
	nouveau_engine_destroy(&(d)->base)
#define nouveau_copy_init(d)                                                   \
	nouveau_engine_init(&(d)->base)
#define nouveau_copy_fini(d,s)                                                 \
	nouveau_engine_fini(&(d)->base, (s))

#define _nouveau_copy_dtor _nouveau_engine_dtor
#define _nouveau_copy_init _nouveau_engine_init
#define _nouveau_copy_fini _nouveau_engine_fini

extern struct nouveau_oclass nva3_copy_oclass;
extern struct nouveau_oclass nvc0_copy0_oclass;
extern struct nouveau_oclass nvc0_copy1_oclass;
extern struct nouveau_oclass nve0_copy0_oclass;
extern struct nouveau_oclass nve0_copy1_oclass;

#endif
