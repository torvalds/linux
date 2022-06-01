/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_VFN_PRIV_H__
#define __NVKM_VFN_PRIV_H__
#define nvkm_vfn(p) container_of((p), struct nvkm_vfn, subdev)
#include <subdev/vfn.h>

struct nvkm_vfn_func {
};

int nvkm_vfn_new_(const struct nvkm_vfn_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		  u32 addr, struct nvkm_vfn **);
#endif
