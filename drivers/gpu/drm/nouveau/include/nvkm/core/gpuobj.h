#ifndef __NVKM_GPUOBJ_H__
#define __NVKM_GPUOBJ_H__
#include <core/memory.h>
#include <core/mm.h>

#define NVOBJ_FLAG_ZERO_ALLOC 0x00000001
#define NVOBJ_FLAG_HEAP       0x00000004

struct nvkm_gpuobj {
	union {
		const struct nvkm_gpuobj_func *func;
		const struct nvkm_gpuobj_func *ptrs;
	};
	struct nvkm_gpuobj *parent;
	struct nvkm_memory *memory;
	struct nvkm_mm_node *node;

	u64 addr;
	u32 size;
	struct nvkm_mm heap;

	void __iomem *map;
};

struct nvkm_gpuobj_func {
	void *(*acquire)(struct nvkm_gpuobj *);
	void (*release)(struct nvkm_gpuobj *);
	u32 (*rd32)(struct nvkm_gpuobj *, u32 offset);
	void (*wr32)(struct nvkm_gpuobj *, u32 offset, u32 data);
	int (*map)(struct nvkm_gpuobj *, u64 offset, struct nvkm_vmm *,
		   struct nvkm_vma *, void *argv, u32 argc);
};

int nvkm_gpuobj_new(struct nvkm_device *, u32 size, int align, bool zero,
		    struct nvkm_gpuobj *parent, struct nvkm_gpuobj **);
void nvkm_gpuobj_del(struct nvkm_gpuobj **);
int nvkm_gpuobj_wrap(struct nvkm_memory *, struct nvkm_gpuobj **);
void nvkm_gpuobj_memcpy_to(struct nvkm_gpuobj *dst, u32 dstoffset, void *src,
			   u32 length);
void nvkm_gpuobj_memcpy_from(void *dst, struct nvkm_gpuobj *src, u32 srcoffset,
			     u32 length);
#endif
