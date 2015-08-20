#ifndef __NVKM_INSTMEM_PRIV_H__
#define __NVKM_INSTMEM_PRIV_H__
#include <subdev/instmem.h>

struct nvkm_instmem_impl {
	struct nvkm_oclass base;
	int (*memory_new)(struct nvkm_instmem *, u32 size, u32 align,
			  bool zero, struct nvkm_memory **);
	bool persistent;
	bool zero;
};

#define nvkm_instmem_create(p,e,o,d)                                        \
	nvkm_instmem_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nvkm_instmem_destroy(p)                                             \
	nvkm_subdev_destroy(&(p)->subdev)
#define nvkm_instmem_init(p) ({                                             \
	struct nvkm_instmem *_imem = (p);                                    \
	_nvkm_instmem_init(nv_object(_imem));                                \
})
#define nvkm_instmem_fini(p,s) ({                                           \
	struct nvkm_instmem *_imem = (p);                                    \
	_nvkm_instmem_fini(nv_object(_imem), (s));                           \
})

int nvkm_instmem_create_(struct nvkm_object *, struct nvkm_object *,
			    struct nvkm_oclass *, int, void **);
#define _nvkm_instmem_dtor _nvkm_subdev_dtor
int _nvkm_instmem_init(struct nvkm_object *);
int _nvkm_instmem_fini(struct nvkm_object *, bool);
#endif
