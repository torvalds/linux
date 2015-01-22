#ifndef __NVKM_BAR_PRIV_H__
#define __NVKM_BAR_PRIV_H__
#include <subdev/bar.h>

#define nvkm_bar_create(p,e,o,d)                                            \
	nvkm_bar_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nvkm_bar_init(p)                                                    \
	nvkm_subdev_init(&(p)->base)
#define nvkm_bar_fini(p,s)                                                  \
	nvkm_subdev_fini(&(p)->base, (s))

int nvkm_bar_create_(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, int, void **);
void nvkm_bar_destroy(struct nvkm_bar *);

void _nvkm_bar_dtor(struct nvkm_object *);
#define _nvkm_bar_init _nvkm_subdev_init
#define _nvkm_bar_fini _nvkm_subdev_fini

int  nvkm_bar_alloc(struct nvkm_bar *, struct nvkm_object *,
		    struct nvkm_mem *, struct nvkm_object **);

void g84_bar_flush(struct nvkm_bar *);

int gf100_bar_ctor(struct nvkm_object *, struct nvkm_object *,
		  struct nvkm_oclass *, void *, u32,
		  struct nvkm_object **);
void gf100_bar_dtor(struct nvkm_object *);
int gf100_bar_init(struct nvkm_object *);
#endif
