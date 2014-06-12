#ifndef __NVKM_PWR_PRIV_H__
#define __NVKM_PWR_PRIV_H__

#include <subdev/pwr.h>
#include <subdev/pwr/fuc/os.h>

#define nouveau_pwr_create(p, e, o, d)                                         \
	nouveau_pwr_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_pwr_destroy(p)                                                 \
	nouveau_subdev_destroy(&(p)->base)
#define nouveau_pwr_init(p) ({                                                 \
	struct nouveau_pwr *_ppwr = (p);                                       \
	_nouveau_pwr_init(nv_object(_ppwr));                                   \
})
#define nouveau_pwr_fini(p,s) ({                                               \
	struct nouveau_pwr *_ppwr = (p);                                       \
	_nouveau_pwr_fini(nv_object(_ppwr), (s));                              \
})

int nouveau_pwr_create_(struct nouveau_object *, struct nouveau_object *,
			struct nouveau_oclass *, int, void **);

int _nouveau_pwr_ctor(struct nouveau_object *, struct nouveau_object *,
		      struct nouveau_oclass *, void *, u32,
		      struct nouveau_object **);
#define _nouveau_pwr_dtor _nouveau_subdev_dtor
int _nouveau_pwr_init(struct nouveau_object *);
int _nouveau_pwr_fini(struct nouveau_object *, bool);

struct nvkm_pwr_impl {
	struct nouveau_oclass base;
	struct {
		u32 *data;
		u32  size;
	} code;
	struct {
		u32 *data;
		u32  size;
	} data;

	void (*pgob)(struct nouveau_pwr *, bool);
};

#endif
