/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_NVSW_H__
#define __NVKM_NVSW_H__
#define nvkm_nvsw(p) container_of((p), struct nvkm_nvsw, object)
#include <core/object.h>

struct nvkm_nvsw {
	struct nvkm_object object;
	const struct nvkm_nvsw_func *func;
	struct nvkm_sw_chan *chan;
};

struct nvkm_nvsw_func {
	int (*mthd)(struct nvkm_nvsw *, u32 mthd, void *data, u32 size);
};

int nvkm_nvsw_new_(const struct nvkm_nvsw_func *, struct nvkm_sw_chan *,
		   const struct nvkm_oclass *, void *data, u32 size,
		   struct nvkm_object **pobject);
int nvkm_nvsw_new(struct nvkm_sw_chan *, const struct nvkm_oclass *,
		  void *data, u32 size, struct nvkm_object **pobject);
#endif
