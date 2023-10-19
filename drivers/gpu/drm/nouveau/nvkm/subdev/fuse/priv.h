/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_FUSE_PRIV_H__
#define __NVKM_FUSE_PRIV_H__
#define nvkm_fuse(p) container_of((p), struct nvkm_fuse, subdev)
#include <subdev/fuse.h>

struct nvkm_fuse_func {
	u32 (*read)(struct nvkm_fuse *, u32 addr);
};

int nvkm_fuse_new_(const struct nvkm_fuse_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		   struct nvkm_fuse **);
#endif
