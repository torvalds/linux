#ifndef __NOUVEAU_DISP_H__
#define __NOUVEAU_DISP_H__

#include <core/object.h>
#include <core/engine.h>
#include <core/device.h>
#include <core/event.h>

struct nouveau_disp {
	struct nouveau_engine base;
	struct nouveau_event *vblank;
};

static inline struct nouveau_disp *
nouveau_disp(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_ENGINE_DISP];
}

#define nouveau_disp_create(p,e,c,h,i,x,d)                                     \
	nouveau_disp_create_((p), (e), (c), (h), (i), (x),                     \
			     sizeof(**d), (void **)d)
#define nouveau_disp_destroy(d) ({                                             \
	struct nouveau_disp *disp = (d);                                       \
	_nouveau_disp_dtor(nv_object(disp));                                   \
})
#define nouveau_disp_init(d)                                                   \
	nouveau_engine_init(&(d)->base)
#define nouveau_disp_fini(d,s)                                                 \
	nouveau_engine_fini(&(d)->base, (s))

int  nouveau_disp_create_(struct nouveau_object *, struct nouveau_object *,
			  struct nouveau_oclass *, int heads,
			  const char *, const char *, int, void **);
void _nouveau_disp_dtor(struct nouveau_object *);
#define _nouveau_disp_init _nouveau_engine_init
#define _nouveau_disp_fini _nouveau_engine_fini

extern struct nouveau_oclass nv04_disp_oclass;
extern struct nouveau_oclass nv50_disp_oclass;
extern struct nouveau_oclass nv84_disp_oclass;
extern struct nouveau_oclass nva0_disp_oclass;
extern struct nouveau_oclass nv94_disp_oclass;
extern struct nouveau_oclass nva3_disp_oclass;
extern struct nouveau_oclass nvd0_disp_oclass;
extern struct nouveau_oclass nve0_disp_oclass;
extern struct nouveau_oclass nvf0_disp_oclass;

#endif
