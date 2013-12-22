#ifndef __NVKM_INSTMEM_PRIV_H__
#define __NVKM_INSTMEM_PRIV_H__

#include <subdev/instmem.h>

struct nouveau_instmem_impl {
	struct nouveau_oclass base;
	struct nouveau_oclass *instobj;
};

#define nouveau_instmem_create(p,e,o,d)                                        \
	nouveau_instmem_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_instmem_destroy(p)                                             \
	nouveau_subdev_destroy(&(p)->base)
#define nouveau_instmem_init(p) ({                                             \
	struct nouveau_instmem *imem = (p);                                    \
	_nouveau_instmem_init(nv_object(imem));                                \
})
#define nouveau_instmem_fini(p,s) ({                                           \
	struct nouveau_instmem *imem = (p);                                    \
	_nouveau_instmem_fini(nv_object(imem), (s));                           \
})

int nouveau_instmem_create_(struct nouveau_object *, struct nouveau_object *,
			    struct nouveau_oclass *, int, void **);
#define _nouveau_instmem_dtor _nouveau_subdev_dtor
int _nouveau_instmem_init(struct nouveau_object *);
int _nouveau_instmem_fini(struct nouveau_object *, bool);

#endif
