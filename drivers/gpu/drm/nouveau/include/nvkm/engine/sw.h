#ifndef __NVKM_SW_H__
#define __NVKM_SW_H__
#include <core/engine.h>

struct nvkm_sw {
	struct nvkm_engine engine;
	const struct nvkm_sw_func *func;
	struct list_head chan;
};

bool nvkm_sw_mthd(struct nvkm_sw *sw, int chid, int subc, u32 mthd, u32 data);

#define nvkm_sw_create(p,e,c,d)                                       \
	nvkm_sw_ctor((p), (e), (c), sizeof(**d), (void **)d)
int
nvkm_sw_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	     struct nvkm_oclass *oclass, int length, void **pobject);
#define nvkm_sw_destroy(d)                                            \
	nvkm_engine_destroy(&(d)->engine)
#define nvkm_sw_init(d)                                               \
	nvkm_engine_init_old(&(d)->engine)
#define nvkm_sw_fini(d,s)                                             \
	nvkm_engine_fini_old(&(d)->engine, (s))

#define _nvkm_sw_dtor _nvkm_engine_dtor
#define _nvkm_sw_init _nvkm_engine_init
#define _nvkm_sw_fini _nvkm_engine_fini

extern struct nvkm_oclass *nv04_sw_oclass;
extern struct nvkm_oclass *nv10_sw_oclass;
extern struct nvkm_oclass *nv50_sw_oclass;
extern struct nvkm_oclass *gf100_sw_oclass;

void nv04_sw_intr(struct nvkm_subdev *);
#endif
