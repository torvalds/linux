#ifndef __NOUVEAU_PPP_H__
#define __NOUVEAU_PPP_H__

#include <core/engine.h>
#include <core/engctx.h>

struct nouveau_ppp_chan {
	struct nouveau_engctx base;
};

#define nouveau_ppp_context_create(p,e,c,g,s,a,f,d)                            \
	nouveau_engctx_create((p), (e), (c), (g), (s), (a), (f), (d))
#define nouveau_ppp_context_destroy(d)                                         \
	nouveau_engctx_destroy(&(d)->base)
#define nouveau_ppp_context_init(d)                                            \
	nouveau_engctx_init(&(d)->base)
#define nouveau_ppp_context_fini(d,s)                                          \
	nouveau_engctx_fini(&(d)->base, (s))

#define _nouveau_ppp_context_dtor _nouveau_engctx_dtor
#define _nouveau_ppp_context_init _nouveau_engctx_init
#define _nouveau_ppp_context_fini _nouveau_engctx_fini
#define _nouveau_ppp_context_rd32 _nouveau_engctx_rd32
#define _nouveau_ppp_context_wr32 _nouveau_engctx_wr32

struct nouveau_ppp {
	struct nouveau_engine base;
};

#define nouveau_ppp_create(p,e,c,d)                                            \
	nouveau_engine_create((p), (e), (c), true, "PPPP", "ppp", (d))
#define nouveau_ppp_destroy(d)                                                 \
	nouveau_engine_destroy(&(d)->base)
#define nouveau_ppp_init(d)                                                    \
	nouveau_engine_init(&(d)->base)
#define nouveau_ppp_fini(d,s)                                                  \
	nouveau_engine_fini(&(d)->base, (s))

#define _nouveau_ppp_dtor _nouveau_engine_dtor
#define _nouveau_ppp_init _nouveau_engine_init
#define _nouveau_ppp_fini _nouveau_engine_fini

extern struct nouveau_oclass nv98_ppp_oclass;

#endif
