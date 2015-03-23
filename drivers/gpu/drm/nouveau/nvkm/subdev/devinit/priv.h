#ifndef __NVKM_DEVINIT_PRIV_H__
#define __NVKM_DEVINIT_PRIV_H__
#include <subdev/devinit.h>

struct nvkm_devinit_impl {
	struct nvkm_oclass base;
	void (*meminit)(struct nvkm_devinit *);
	int  (*pll_set)(struct nvkm_devinit *, u32 type, u32 freq);
	u64  (*disable)(struct nvkm_devinit *);
	u32  (*mmio)(struct nvkm_devinit *, u32);
	int  (*post)(struct nvkm_subdev *, bool);
};

#define nvkm_devinit_create(p,e,o,d)                                        \
	nvkm_devinit_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nvkm_devinit_destroy(p) ({                                          \
	struct nvkm_devinit *d = (p);                                       \
	_nvkm_devinit_dtor(nv_object(d));                                   \
})
#define nvkm_devinit_init(p) ({                                             \
	struct nvkm_devinit *d = (p);                                       \
	_nvkm_devinit_init(nv_object(d));                                   \
})
#define nvkm_devinit_fini(p,s) ({                                           \
	struct nvkm_devinit *d = (p);                                       \
	_nvkm_devinit_fini(nv_object(d), (s));                              \
})

int nvkm_devinit_create_(struct nvkm_object *, struct nvkm_object *,
			    struct nvkm_oclass *, int, void **);
void _nvkm_devinit_dtor(struct nvkm_object *);
int _nvkm_devinit_init(struct nvkm_object *);
int _nvkm_devinit_fini(struct nvkm_object *, bool suspend);
#endif
