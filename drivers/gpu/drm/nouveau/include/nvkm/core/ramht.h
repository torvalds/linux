/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_RAMHT_H__
#define __NVKM_RAMHT_H__
#include <core/gpuobj.h>

struct nvkm_ramht_data {
	struct nvkm_gpuobj *inst;
	int chid;
	u32 handle;
};

struct nvkm_ramht {
	struct nvkm_device *device;
	struct nvkm_gpuobj *parent;
	struct nvkm_gpuobj *gpuobj;
	int size;
	int bits;
	struct nvkm_ramht_data data[];
};

int  nvkm_ramht_new(struct nvkm_device *, u32 size, u32 align,
		    struct nvkm_gpuobj *, struct nvkm_ramht **);
void nvkm_ramht_del(struct nvkm_ramht **);
int  nvkm_ramht_insert(struct nvkm_ramht *, struct nvkm_object *,
		       int chid, int addr, u32 handle, u32 context);
void nvkm_ramht_remove(struct nvkm_ramht *, int cookie);
struct nvkm_gpuobj *
nvkm_ramht_search(struct nvkm_ramht *, int chid, u32 handle);
#endif
