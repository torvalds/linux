#ifndef __NOUVEAU_BAR_H__
#define __NOUVEAU_BAR_H__

#include <core/subdev.h>
#include <core/device.h>

struct nouveau_mem;
struct nouveau_vma;

struct nouveau_bar {
	struct nouveau_subdev base;

	int (*alloc)(struct nouveau_bar *, struct nouveau_object *,
		     struct nouveau_mem *, struct nouveau_object **);
	void __iomem *iomem;

	int (*kmap)(struct nouveau_bar *, struct nouveau_mem *,
		    u32 flags, struct nouveau_vma *);
	int (*umap)(struct nouveau_bar *, struct nouveau_mem *,
		    u32 flags, struct nouveau_vma *);
	void (*unmap)(struct nouveau_bar *, struct nouveau_vma *);
	void (*flush)(struct nouveau_bar *);
};

static inline struct nouveau_bar *
nouveau_bar(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_BAR];
}

extern struct nouveau_oclass nv50_bar_oclass;
extern struct nouveau_oclass nvc0_bar_oclass;

#endif
