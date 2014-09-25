#ifndef __NVKM_INSTMEM_PRIV_H__
#define __NVKM_INSTMEM_PRIV_H__

#include <subdev/instmem.h>

struct nouveau_instobj_impl {
	struct nouveau_oclass base;
};

struct nouveau_instobj_args {
	u32 size;
	u32 align;
};

#define nouveau_instobj_create(p,e,o,d)                                        \
	nouveau_instobj_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_instobj_destroy(p) ({                                          \
	struct nouveau_instobj *iobj = (p);                                    \
	_nouveau_instobj_dtor(nv_object(iobj));                                \
})
#define nouveau_instobj_init(p)                                                \
	nouveau_object_init(&(p)->base)
#define nouveau_instobj_fini(p,s)                                              \
	nouveau_object_fini(&(p)->base, (s))

int  nouveau_instobj_create_(struct nouveau_object *, struct nouveau_object *,
			     struct nouveau_oclass *, int, void **);
void _nouveau_instobj_dtor(struct nouveau_object *);
#define _nouveau_instobj_init nouveau_object_init
#define _nouveau_instobj_fini nouveau_object_fini

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
