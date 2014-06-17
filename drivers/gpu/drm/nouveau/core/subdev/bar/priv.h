#ifndef __NVKM_BAR_PRIV_H__
#define __NVKM_BAR_PRIV_H__

#include <subdev/bar.h>

#define nouveau_bar_create(p,e,o,d)                                            \
	nouveau_bar_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_bar_init(p)                                                    \
	nouveau_subdev_init(&(p)->base)
#define nouveau_bar_fini(p,s)                                                  \
	nouveau_subdev_fini(&(p)->base, (s))

int nouveau_bar_create_(struct nouveau_object *, struct nouveau_object *,
			struct nouveau_oclass *, int, void **);
void nouveau_bar_destroy(struct nouveau_bar *);

void _nouveau_bar_dtor(struct nouveau_object *);
#define _nouveau_bar_init _nouveau_subdev_init
#define _nouveau_bar_fini _nouveau_subdev_fini

int  nouveau_bar_alloc(struct nouveau_bar *, struct nouveau_object *,
		       struct nouveau_mem *, struct nouveau_object **);

void nv84_bar_flush(struct nouveau_bar *);

#endif
