#ifndef __NVKM_MXM_H__
#define __NVKM_MXM_H__
#include <core/subdev.h>

#define MXM_SANITISE_DCB 0x00000001

struct nvkm_mxm {
	struct nvkm_subdev base;
	u32 action;
	u8 *mxms;
};

static inline struct nvkm_mxm *
nvkm_mxm(void *obj)
{
	return (void *)nvkm_subdev(obj, NVDEV_SUBDEV_MXM);
}

#define nvkm_mxm_create(p,e,o,d)                                            \
	nvkm_mxm_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nvkm_mxm_init(p)                                                    \
	nvkm_subdev_init(&(p)->base)
#define nvkm_mxm_fini(p,s)                                                  \
	nvkm_subdev_fini(&(p)->base, (s))
int  nvkm_mxm_create_(struct nvkm_object *, struct nvkm_object *,
			 struct nvkm_oclass *, int, void **);
void nvkm_mxm_destroy(struct nvkm_mxm *);

#define _nvkm_mxm_dtor _nvkm_subdev_dtor
#define _nvkm_mxm_init _nvkm_subdev_init
#define _nvkm_mxm_fini _nvkm_subdev_fini

extern struct nvkm_oclass nv50_mxm_oclass;
#endif
