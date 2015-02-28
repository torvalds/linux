#ifndef __NVKM_IBUS_H__
#define __NVKM_IBUS_H__
#include <core/subdev.h>

struct nvkm_ibus {
	struct nvkm_subdev base;
};

static inline struct nvkm_ibus *
nvkm_ibus(void *obj)
{
	return (void *)nvkm_subdev(obj, NVDEV_SUBDEV_IBUS);
}

#define nvkm_ibus_create(p,e,o,d)                                           \
	nvkm_subdev_create_((p), (e), (o), 0, "PIBUS", "ibus",              \
			       sizeof(**d), (void **)d)
#define nvkm_ibus_destroy(p)                                                \
	nvkm_subdev_destroy(&(p)->base)
#define nvkm_ibus_init(p)                                                   \
	nvkm_subdev_init(&(p)->base)
#define nvkm_ibus_fini(p,s)                                                 \
	nvkm_subdev_fini(&(p)->base, (s))

#define _nvkm_ibus_dtor _nvkm_subdev_dtor
#define _nvkm_ibus_init _nvkm_subdev_init
#define _nvkm_ibus_fini _nvkm_subdev_fini

extern struct nvkm_oclass gf100_ibus_oclass;
extern struct nvkm_oclass gk104_ibus_oclass;
extern struct nvkm_oclass gk20a_ibus_oclass;
#endif
