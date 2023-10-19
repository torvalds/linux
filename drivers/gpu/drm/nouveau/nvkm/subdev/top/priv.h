/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_TOP_PRIV_H__
#define __NVKM_TOP_PRIV_H__
#define nvkm_top(p) container_of((p), struct nvkm_top, subdev)
#include <subdev/top.h>

struct nvkm_top_func {
	int (*oneinit)(struct nvkm_top *);
};

int nvkm_top_new_(const struct nvkm_top_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		  struct nvkm_top **);

struct nvkm_top_device *nvkm_top_device_new(struct nvkm_top *);
#endif
