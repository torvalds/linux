/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_VFN_PRIV_H__
#define __NVKM_VFN_PRIV_H__
#define nvkm_vfn(p) container_of((p), struct nvkm_vfn, subdev)
#include <subdev/vfn.h>

struct nvkm_vfn_func {
	const struct nvkm_intr_func *intr;
	const struct nvkm_intr_data *intrs;

	struct {
		u32 addr;
		u32 size;
		const struct nvkm_sclass base;
	} user;
};

int nvkm_vfn_new_(const struct nvkm_vfn_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		  u32 addr, struct nvkm_vfn **);

extern const struct nvkm_intr_func tu102_vfn_intr;

int nvkm_uvfn_new(struct nvkm_device *, const struct nvkm_oclass *, void *, u32,
		  struct nvkm_object **);
#endif
