/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_VFN_PRIV_H__
#define __NVKM_VFN_PRIV_H__
#define nvkm_vfn(p) container_of((p), struct nvkm_vfn, subdev)
#include <subdev/vfn.h>

struct nvkm_vfn_func {
	void (*dtor)(struct nvkm_vfn *);

	const struct nvkm_intr_func *intr;
	const struct nvkm_intr_data *intrs;

	struct {
		u32 addr;
		u32 size;
		struct nvkm_sclass base;
	} user;
};

int r535_vfn_new(const struct nvkm_vfn_func *hw, struct nvkm_device *, enum nvkm_subdev_type, int,
		 u32 addr, struct nvkm_vfn **);

int nvkm_vfn_new_(const struct nvkm_vfn_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		  u32 addr, struct nvkm_vfn **);

extern const struct nvkm_intr_func tu102_vfn_intr;

int nvkm_uvfn_new(struct nvkm_device *, const struct nvkm_oclass *, void *, u32,
		  struct nvkm_object **);
#endif
