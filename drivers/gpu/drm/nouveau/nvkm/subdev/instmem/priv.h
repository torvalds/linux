/* SPDX-License-Identifier: GPL-2.0 */
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
	bool persistent;
	bool zero;
};

void nvkm_instmem_ctor(const struct nvkm_instmem_func *, struct nvkm_device *,
		       int index, struct nvkm_instmem *);
#endif
