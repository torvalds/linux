#ifndef __NOUVEAU_BSP_H__
#define __NOUVEAU_BSP_H__

#include <core/engine.h>
#include <core/engctx.h>

struct nouveau_bsp_chan {
	struct nouveau_engctx base;
};

#define nouveau_bsp_context_create(p,e,c,g,s,a,f,d)                            \
	nouveau_engctx_create((p), (e), (c), (g), (s), (a), (f), (d))
#define nouveau_bsp_context_destroy(d)                                         \
	nouveau_engctx_destroy(&(d)->base)
#define nouveau_bsp_context_init(d)                                            \
	nouveau_engctx_init(&(d)->base)
#define nouveau_bsp_context_fini(d,s)                                          \
	nouveau_engctx_fini(&(d)->base, (s))

#define _nouveau_bsp_context_dtor _nouveau_engctx_dtor
#define _nouveau_bsp_context_init _nouveau_engctx_init
#define _nouveau_bsp_context_fini _nouveau_engctx_fini
#define _nouveau_bsp_context_rd32 _nouveau_engctx_rd32
#define _nouveau_bsp_context_wr32 _nouveau_engctx_wr32

struct nouveau_bsp {
	struct nouveau_engine base;
};

#define nouveau_bsp_create(p,e,c,d)                                            \
	nouveau_engine_create((p), (e), (c), true, "PBSP", "bsp", (d))
#define nouveau_bsp_destroy(d)                                                 \
	nouveau_engine_destroy(&(d)->base)
#define nouveau_bsp_init(d)                                                    \
	nouveau_engine_init(&(d)->base)
#define nouveau_bsp_fini(d,s)                                                  \
	nouveau_engine_fini(&(d)->base, (s))

#define _nouveau_bsp_dtor _nouveau_engine_dtor
#define _nouveau_bsp_init _nouveau_engine_init
#define _nouveau_bsp_fini _nouveau_engine_fini

extern struct nouveau_oclass nv84_bsp_oclass;

#endif
