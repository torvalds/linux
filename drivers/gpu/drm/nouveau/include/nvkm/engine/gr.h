#ifndef __NVKM_GR_H__
#define __NVKM_GR_H__
#include <core/engctx.h>

struct nvkm_gr_chan {
	struct nvkm_engctx base;
};

#define nvkm_gr_context_create(p,e,c,g,s,a,f,d)                          \
	nvkm_engctx_create((p), (e), (c), (g), (s), (a), (f), (d))
#define nvkm_gr_context_destroy(d)                                       \
	nvkm_engctx_destroy(&(d)->base)
#define nvkm_gr_context_init(d)                                          \
	nvkm_engctx_init(&(d)->base)
#define nvkm_gr_context_fini(d,s)                                        \
	nvkm_engctx_fini(&(d)->base, (s))

#define _nvkm_gr_context_dtor _nvkm_engctx_dtor
#define _nvkm_gr_context_init _nvkm_engctx_init
#define _nvkm_gr_context_fini _nvkm_engctx_fini
#define _nvkm_gr_context_rd32 _nvkm_engctx_rd32
#define _nvkm_gr_context_wr32 _nvkm_engctx_wr32

#include <core/engine.h>

struct nvkm_gr {
	struct nvkm_engine base;

	/* Returns chipset-specific counts of units packed into an u64.
	 */
	u64 (*units)(struct nvkm_gr *);
};

static inline struct nvkm_gr *
nvkm_gr(void *obj)
{
	return (void *)nvkm_engine(obj, NVDEV_ENGINE_GR);
}

#define nvkm_gr_create(p,e,c,y,d)                                        \
	nvkm_engine_create((p), (e), (c), (y), "PGR", "graphics", (d))
#define nvkm_gr_destroy(d)                                               \
	nvkm_engine_destroy(&(d)->base)
#define nvkm_gr_init(d)                                                  \
	nvkm_engine_init(&(d)->base)
#define nvkm_gr_fini(d,s)                                                \
	nvkm_engine_fini(&(d)->base, (s))

#define _nvkm_gr_dtor _nvkm_engine_dtor
#define _nvkm_gr_init _nvkm_engine_init
#define _nvkm_gr_fini _nvkm_engine_fini

extern struct nvkm_oclass nv04_gr_oclass;
extern struct nvkm_oclass nv10_gr_oclass;
extern struct nvkm_oclass nv20_gr_oclass;
extern struct nvkm_oclass nv25_gr_oclass;
extern struct nvkm_oclass nv2a_gr_oclass;
extern struct nvkm_oclass nv30_gr_oclass;
extern struct nvkm_oclass nv34_gr_oclass;
extern struct nvkm_oclass nv35_gr_oclass;
extern struct nvkm_oclass nv40_gr_oclass;
extern struct nvkm_oclass nv50_gr_oclass;
extern struct nvkm_oclass *gf100_gr_oclass;
extern struct nvkm_oclass *gf108_gr_oclass;
extern struct nvkm_oclass *gf104_gr_oclass;
extern struct nvkm_oclass *gf110_gr_oclass;
extern struct nvkm_oclass *gf117_gr_oclass;
extern struct nvkm_oclass *gf119_gr_oclass;
extern struct nvkm_oclass *gk104_gr_oclass;
extern struct nvkm_oclass *gk20a_gr_oclass;
extern struct nvkm_oclass *gk110_gr_oclass;
extern struct nvkm_oclass *gk110b_gr_oclass;
extern struct nvkm_oclass *gk208_gr_oclass;
extern struct nvkm_oclass *gm107_gr_oclass;

#include <core/enum.h>

extern const struct nvkm_bitfield nv04_gr_nsource[];
extern struct nvkm_ofuncs nv04_gr_ofuncs;
bool nv04_gr_idle(void *obj);

extern const struct nvkm_bitfield nv10_gr_intr_name[];
extern const struct nvkm_bitfield nv10_gr_nstatus[];

extern const struct nvkm_enum nv50_data_error_names[];
#endif
