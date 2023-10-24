/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_INSTMEM_PRIV_H__
#define __NVKM_INSTMEM_PRIV_H__
#define nvkm_instmem(p) container_of((p), struct nvkm_instmem, subdev)
#include <subdev/instmem.h>

struct nvkm_instmem_func {
	void *(*dtor)(struct nvkm_instmem *);
	int (*oneinit)(struct nvkm_instmem *);
	void (*fini)(struct nvkm_instmem *);
	u32  (*rd32)(struct nvkm_instmem *, u32 addr);
	void (*wr32)(struct nvkm_instmem *, u32 addr, u32 data);
	int (*memory_new)(struct nvkm_instmem *, u32 size, u32 align,
			  bool zero, struct nvkm_memory **);
	int (*memory_wrap)(struct nvkm_instmem *, struct nvkm_memory *, struct nvkm_memory **);
	bool zero;
};

void nvkm_instmem_ctor(const struct nvkm_instmem_func *, struct nvkm_device *,
		       enum nvkm_subdev_type, int, struct nvkm_instmem *);
void nvkm_instmem_boot(struct nvkm_instmem *);

#include <core/memory.h>

struct nvkm_instobj {
	struct nvkm_memory memory;
	struct list_head head;
	bool preserve;
	u32 *suspend;
};

void nvkm_instobj_ctor(const struct nvkm_memory_func *func,
		       struct nvkm_instmem *, struct nvkm_instobj *);
void nvkm_instobj_dtor(struct nvkm_instmem *, struct nvkm_instobj *);
#endif
