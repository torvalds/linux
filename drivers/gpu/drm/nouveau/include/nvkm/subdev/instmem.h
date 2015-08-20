#ifndef __NVKM_INSTMEM_H__
#define __NVKM_INSTMEM_H__
#include <core/subdev.h>
struct nvkm_memory;

struct nvkm_instmem {
	struct nvkm_subdev subdev;
	struct list_head list;

	u32 reserved;
	int (*alloc)(struct nvkm_instmem *, u32 size, u32 align, bool zero,
		     struct nvkm_memory **);

	const struct nvkm_instmem_func *func;

	struct nvkm_memory *vbios;
	struct nvkm_ramht  *ramht;
	struct nvkm_memory *ramro;
	struct nvkm_memory *ramfc;
};

struct nvkm_instmem_func {
	u32  (*rd32)(struct nvkm_instmem *, u32 addr);
	void (*wr32)(struct nvkm_instmem *, u32 addr, u32 data);
};

static inline struct nvkm_instmem *
nvkm_instmem(void *obj)
{
	/* nv04/nv40 impls need to create objects in their constructor,
	 * which is before the subdev pointer is valid
	 */
	if (nv_iclass(obj, NV_SUBDEV_CLASS) &&
	    nv_subidx(obj) == NVDEV_SUBDEV_INSTMEM)
		return obj;

	return (void *)nvkm_subdev(obj, NVDEV_SUBDEV_INSTMEM);
}

extern struct nvkm_oclass *nv04_instmem_oclass;
extern struct nvkm_oclass *nv40_instmem_oclass;
extern struct nvkm_oclass *nv50_instmem_oclass;
extern struct nvkm_oclass *gk20a_instmem_oclass;
#endif
