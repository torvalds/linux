/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_VOLT_H__
#define __NVKM_VOLT_H__
#include <core/subdev.h>

struct nvkm_volt {
	const struct nvkm_volt_func *func;
	struct nvkm_subdev subdev;

	u8 vid_mask;
	u8 vid_nr;
	struct {
		u32 uv;
		u8 vid;
	} vid[256];

	u32 max_uv;
	u32 min_uv;

	/*
	 * These are fully functional map entries creating a sw ceiling for
	 * the voltage. These all can describe different kind of curves, so
	 * that for any given temperature a different one can return the lowest
	 * value of all three.
	 */
	u8 max0_id;
	u8 max1_id;
	u8 max2_id;

	int speedo;
};

int nvkm_volt_map(struct nvkm_volt *volt, u8 id, u8 temperature);
int nvkm_volt_map_min(struct nvkm_volt *volt, u8 id);
int nvkm_volt_get(struct nvkm_volt *);
int nvkm_volt_set_id(struct nvkm_volt *, u8 id, u8 min_id, u8 temp,
		     int condition);

int nv40_volt_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_volt **);
int gf100_volt_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_volt **);
int gf117_volt_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_volt **);
int gk104_volt_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_volt **);
int gk20a_volt_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_volt **);
int gm20b_volt_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_volt **);
#endif
