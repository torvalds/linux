#ifndef __NVKM_DEVINIT_PRIV_H__
#define __NVKM_DEVINIT_PRIV_H__

#include <subdev/bios.h>
#include <subdev/bios/pll.h>
#include <subdev/clock/pll.h>
#include <subdev/devinit.h>

struct nouveau_devinit_impl {
	struct nouveau_oclass base;
	void (*meminit)(struct nouveau_devinit *);
	int  (*pll_set)(struct nouveau_devinit *, u32 type, u32 freq);
	u64  (*disable)(struct nouveau_devinit *);
};

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

#endif
