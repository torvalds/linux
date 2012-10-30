#ifndef __NOUVEAU_MXM_H__
#define __NOUVEAU_MXM_H__

#include <core/subdev.h>
#include <core/device.h>

#define MXM_SANITISE_DCB 0x00000001

struct nouveau_mxm {
	struct nouveau_subdev base;
	u32 action;
	u8 *mxms;
};

static inline struct nouveau_mxm *
nouveau_mxm(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_MXM];
}

#define nouveau_mxm_create(p,e,o,d)                                            \
	nouveau_mxm_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_mxm_init(p)                                                    \
	nouveau_subdev_init(&(p)->base)
#define nouveau_mxm_fini(p,s)                                                  \
	nouveau_subdev_fini(&(p)->base, (s))
int  nouveau_mxm_create_(struct nouveau_object *, struct nouveau_object *,
			 struct nouveau_oclass *, int, void **);
void nouveau_mxm_destroy(struct nouveau_mxm *);

#define _nouveau_mxm_dtor _nouveau_subdev_dtor
#define _nouveau_mxm_init _nouveau_subdev_init
#define _nouveau_mxm_fini _nouveau_subdev_fini

extern struct nouveau_oclass nv50_mxm_oclass;

#endif
