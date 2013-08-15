#ifndef __NOUVEAU_DEVINIT_H__
#define __NOUVEAU_DEVINIT_H__

#include <core/subdev.h>
#include <core/device.h>

struct nouveau_devinit {
	struct nouveau_subdev base;
	bool post;
	void (*meminit)(struct nouveau_devinit *);
	int  (*pll_set)(struct nouveau_devinit *, u32 type, u32 freq);

};

static inline struct nouveau_devinit *
nouveau_devinit(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_DEVINIT];
}

#define nouveau_devinit_create(p,e,o,d)                                        \
	nouveau_devinit_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_devinit_destroy(p)                                             \
	nouveau_subdev_destroy(&(p)->base)
#define nouveau_devinit_init(p) ({                                             \
	struct nouveau_devinit *d = (p);                                       \
	_nouveau_devinit_init(nv_object(d));                                   \
})
#define nouveau_devinit_fini(p,s) ({                                           \
	struct nouveau_devinit *d = (p);                                       \
	_nouveau_devinit_fini(nv_object(d), (s));                              \
})

int nouveau_devinit_create_(struct nouveau_object *, struct nouveau_object *,
			    struct nouveau_oclass *, int, void **);
#define _nouveau_devinit_dtor _nouveau_subdev_dtor
int _nouveau_devinit_init(struct nouveau_object *);
int _nouveau_devinit_fini(struct nouveau_object *, bool suspend);

extern struct nouveau_oclass nv04_devinit_oclass;
extern struct nouveau_oclass nv05_devinit_oclass;
extern struct nouveau_oclass nv10_devinit_oclass;
extern struct nouveau_oclass nv1a_devinit_oclass;
extern struct nouveau_oclass nv20_devinit_oclass;
extern struct nouveau_oclass nv50_devinit_oclass;
extern struct nouveau_oclass nva3_devinit_oclass;
extern struct nouveau_oclass nvc0_devinit_oclass;

#endif
