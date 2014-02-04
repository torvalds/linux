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

extern struct nouveau_oclass *nv04_instmem_oclass;
extern struct nouveau_oclass *nv40_instmem_oclass;
extern struct nouveau_oclass *nv50_instmem_oclass;

#endif
