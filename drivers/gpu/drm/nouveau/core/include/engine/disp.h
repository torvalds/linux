#ifndef __NOUVEAU_DISP_H__
#define __NOUVEAU_DISP_H__

#include <core/object.h>
#include <core/engine.h>
#include <core/device.h>

struct nouveau_disp {
	struct nouveau_engine base;

	struct {
		struct list_head list;
		spinlock_t lock;
		void (*notify)(void *, int);
		void (*get)(void *, int);
		void (*put)(void *, int);
		void *data;
	} vblank;
};

static inline struct nouveau_disp *
nouveau_disp(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_ENGINE_DISP];
}

#define nouveau_disp_create(p,e,c,i,x,d)                                       \
	nouveau_engine_create((p), (e), (c), true, (i), (x), (d))
#define nouveau_disp_destroy(d)                                                \
	nouveau_engine_destroy(&(d)->base)
#define nouveau_disp_init(d)                                                   \
	nouveau_engine_init(&(d)->base)
#define nouveau_disp_fini(d,s)                                                 \
	nouveau_engine_fini(&(d)->base, (s))

#define _nouveau_disp_dtor _nouveau_engine_dtor
#define _nouveau_disp_init _nouveau_engine_init
#define _nouveau_disp_fini _nouveau_engine_fini

extern struct nouveau_oclass nv04_disp_oclass;
extern struct nouveau_oclass nv50_disp_oclass;
extern struct nouveau_oclass nvd0_disp_oclass;

#endif
