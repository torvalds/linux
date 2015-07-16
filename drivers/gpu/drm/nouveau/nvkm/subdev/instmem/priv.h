#ifndef __NVKM_INSTMEM_PRIV_H__
#define __NVKM_INSTMEM_PRIV_H__
#include <subdev/instmem.h>

struct nvkm_instobj_impl {
	struct nvkm_oclass base;
};

struct nvkm_instobj_args {
	u32 size;
	u32 align;
};

#define nvkm_instobj_create(p,e,o,d)                                        \
	nvkm_instobj_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nvkm_instobj_destroy(p) ({                                          \
	struct nvkm_instobj *iobj = (p);                                    \
	_nvkm_instobj_dtor(nv_object(iobj));                                \
})
#define nvkm_instobj_init(p)                                                \
	nvkm_object_init(&(p)->base)
#define nvkm_instobj_fini(p,s)                                              \
	nvkm_object_fini(&(p)->base, (s))

int  nvkm_instobj_create_(struct nvkm_object *, struct nvkm_object *,
			     struct nvkm_oclass *, int, void **);
void _nvkm_instobj_dtor(struct nvkm_object *);
#define _nvkm_instobj_init nvkm_object_init
#define _nvkm_instobj_fini nvkm_object_fini

struct nvkm_instmem_impl {
	struct nvkm_oclass base;
	struct nvkm_oclass *instobj;
};

#define nvkm_instmem_create(p,e,o,d)                                        \
	nvkm_instmem_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nvkm_instmem_destroy(p)                                             \
	nvkm_subdev_destroy(&(p)->base)
#define nvkm_instmem_init(p) ({                                             \
	struct nvkm_instmem *imem = (p);                                    \
	_nvkm_instmem_init(nv_object(imem));                                \
})
#define nvkm_instmem_fini(p,s) ({                                           \
	struct nvkm_instmem *imem = (p);                                    \
	_nvkm_instmem_fini(nv_object(imem), (s));                           \
})

int nvkm_instmem_create_(struct nvkm_object *, struct nvkm_object *,
			    struct nvkm_oclass *, int, void **);
#define _nvkm_instmem_dtor _nvkm_subdev_dtor
int _nvkm_instmem_init(struct nvkm_object *);
int _nvkm_instmem_fini(struct nvkm_object *, bool);
#endif
