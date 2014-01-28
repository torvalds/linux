#ifndef __NOUVEAU_INSTMEM_H__
#define __NOUVEAU_INSTMEM_H__

#include <core/subdev.h>
#include <core/device.h>
#include <core/mm.h>

struct nouveau_instobj {
	struct nouveau_object base;
	struct list_head head;
	u32 *suspend;
	u64 addr;
	u32 size;
};

static inline struct nouveau_instobj *
nv_memobj(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (unlikely(!nv_iclass(obj, NV_MEMOBJ_CLASS)))
		nv_assert("BAD CAST -> NvMemObj, %08x", nv_hclass(obj));
#endif
	return obj;
}

#define nouveau_instobj_create(p,e,o,d)                                        \
	nouveau_instobj_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_instobj_init(p)                                                \
	nouveau_object_init(&(p)->base)
#define nouveau_instobj_fini(p,s)                                              \
	nouveau_object_fini(&(p)->base, (s))

int  nouveau_instobj_create_(struct nouveau_object *, struct nouveau_object *,
			     struct nouveau_oclass *, int, void **);
void nouveau_instobj_destroy(struct nouveau_instobj *);

void _nouveau_instobj_dtor(struct nouveau_object *);
#define _nouveau_instobj_init nouveau_object_init
#define _nouveau_instobj_fini nouveau_object_fini

struct nouveau_instmem {
	struct nouveau_subdev base;
	struct list_head list;

	u32 reserved;
	int (*alloc)(struct nouveau_instmem *, struct nouveau_object *,
		     u32 size, u32 align, struct nouveau_object **);
};

static inline struct nouveau_instmem *
nouveau_instmem(void *obj)
{
	/* nv04/nv40 impls need to create objects in their constructor,
	 * which is before the subdev pointer is valid
	 */
	if (nv_iclass(obj, NV_SUBDEV_CLASS) &&
	    nv_subidx(obj) == NVDEV_SUBDEV_INSTMEM)
		return obj;

	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_INSTMEM];
}

#define nouveau_instmem_create(p,e,o,d)                                        \
	nouveau_instmem_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_instmem_destroy(p)                                             \
	nouveau_subdev_destroy(&(p)->base)
int nouveau_instmem_create_(struct nouveau_object *, struct nouveau_object *,
			    struct nouveau_oclass *, int, void **);
int nouveau_instmem_init(struct nouveau_instmem *);
int nouveau_instmem_fini(struct nouveau_instmem *, bool);

#define _nouveau_instmem_dtor _nouveau_subdev_dtor
int _nouveau_instmem_init(struct nouveau_object *);
int _nouveau_instmem_fini(struct nouveau_object *, bool);

extern struct nouveau_oclass nv04_instmem_oclass;
extern struct nouveau_oclass nv40_instmem_oclass;
extern struct nouveau_oclass nv50_instmem_oclass;

#endif
