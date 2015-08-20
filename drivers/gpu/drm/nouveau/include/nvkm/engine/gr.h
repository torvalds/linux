#ifndef __NVKM_GR_H__
#define __NVKM_GR_H__
#include <core/engine.h>

struct nvkm_gr {
	struct nvkm_engine engine;
	const struct nvkm_gr_func *func;

	/* Returns chipset-specific counts of units packed into an u64.
	 */
	u64 (*units)(struct nvkm_gr *);
};

#define nvkm_gr_create(p,e,c,y,d)                                        \
	nvkm_gr_create_((p), (e), (c), (y), sizeof(**d), (void **)(d))
int
nvkm_gr_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, bool enable,
		int length, void **pobject);
#define nvkm_gr_destroy(d)                                               \
	nvkm_engine_destroy(&(d)->engine)
#define nvkm_gr_init(d)                                                  \
	nvkm_engine_init_old(&(d)->engine)
#define nvkm_gr_fini(d,s)                                                \
	nvkm_engine_fini_old(&(d)->engine, (s))

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
extern struct nvkm_oclass *gm204_gr_oclass;
extern struct nvkm_oclass *gm206_gr_oclass;
extern struct nvkm_oclass *gm20b_gr_oclass;

#include <core/enum.h>

extern const struct nvkm_bitfield nv04_gr_nsource[];
bool nv04_gr_idle(struct nvkm_gr *);

extern const struct nvkm_bitfield nv10_gr_intr_name[];
extern const struct nvkm_bitfield nv10_gr_nstatus[];

extern const struct nvkm_enum nv50_data_error_names[];
#endif
