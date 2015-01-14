#ifndef __NOUVEAU_GR_H__
#define __NOUVEAU_GR_H__

#include <core/engine.h>
#include <core/engctx.h>
#include <core/enum.h>

struct nouveau_gr_chan {
	struct nouveau_engctx base;
};

#define nouveau_gr_context_create(p,e,c,g,s,a,f,d)                          \
	nouveau_engctx_create((p), (e), (c), (g), (s), (a), (f), (d))
#define nouveau_gr_context_destroy(d)                                       \
	nouveau_engctx_destroy(&(d)->base)
#define nouveau_gr_context_init(d)                                          \
	nouveau_engctx_init(&(d)->base)
#define nouveau_gr_context_fini(d,s)                                        \
	nouveau_engctx_fini(&(d)->base, (s))

#define _nouveau_gr_context_dtor _nouveau_engctx_dtor
#define _nouveau_gr_context_init _nouveau_engctx_init
#define _nouveau_gr_context_fini _nouveau_engctx_fini
#define _nouveau_gr_context_rd32 _nouveau_engctx_rd32
#define _nouveau_gr_context_wr32 _nouveau_engctx_wr32

struct nouveau_gr {
	struct nouveau_engine base;

	/* Returns chipset-specific counts of units packed into an u64.
	 */
	u64 (*units)(struct nouveau_gr *);
};

static inline struct nouveau_gr *
nouveau_gr(void *obj)
{
	return (void *)nouveau_engine(obj, NVDEV_ENGINE_GR);
}

#define nouveau_gr_create(p,e,c,y,d)                                        \
	nouveau_engine_create((p), (e), (c), (y), "PGR", "graphics", (d))
#define nouveau_gr_destroy(d)                                               \
	nouveau_engine_destroy(&(d)->base)
#define nouveau_gr_init(d)                                                  \
	nouveau_engine_init(&(d)->base)
#define nouveau_gr_fini(d,s)                                                \
	nouveau_engine_fini(&(d)->base, (s))

#define _nouveau_gr_dtor _nouveau_engine_dtor
#define _nouveau_gr_init _nouveau_engine_init
#define _nouveau_gr_fini _nouveau_engine_fini

extern struct nouveau_oclass nv04_gr_oclass;
extern struct nouveau_oclass nv10_gr_oclass;
extern struct nouveau_oclass nv20_gr_oclass;
extern struct nouveau_oclass nv25_gr_oclass;
extern struct nouveau_oclass nv2a_gr_oclass;
extern struct nouveau_oclass nv30_gr_oclass;
extern struct nouveau_oclass nv34_gr_oclass;
extern struct nouveau_oclass nv35_gr_oclass;
extern struct nouveau_oclass nv40_gr_oclass;
extern struct nouveau_oclass nv50_gr_oclass;
extern struct nouveau_oclass *nvc0_gr_oclass;
extern struct nouveau_oclass *nvc1_gr_oclass;
extern struct nouveau_oclass *nvc4_gr_oclass;
extern struct nouveau_oclass *nvc8_gr_oclass;
extern struct nouveau_oclass *nvd7_gr_oclass;
extern struct nouveau_oclass *nvd9_gr_oclass;
extern struct nouveau_oclass *nve4_gr_oclass;
extern struct nouveau_oclass *gk20a_gr_oclass;
extern struct nouveau_oclass *nvf0_gr_oclass;
extern struct nouveau_oclass *gk110b_gr_oclass;
extern struct nouveau_oclass *nv108_gr_oclass;
extern struct nouveau_oclass *gm107_gr_oclass;

extern const struct nouveau_bitfield nv04_gr_nsource[];
extern struct nouveau_ofuncs nv04_gr_ofuncs;
bool nv04_gr_idle(void *obj);

extern const struct nouveau_bitfield nv10_gr_intr_name[];
extern const struct nouveau_bitfield nv10_gr_nstatus[];

extern const struct nouveau_enum nv50_data_error_names[];

#endif
