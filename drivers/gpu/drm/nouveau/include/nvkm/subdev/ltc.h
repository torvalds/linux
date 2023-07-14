/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_LTC_H__
#define __NVKM_LTC_H__
#include <core/subdev.h>
#include <core/mm.h>

#define NVKM_LTC_MAX_ZBC_COLOR_CNT 32
#define NVKM_LTC_MAX_ZBC_DEPTH_CNT 16

struct nvkm_ltc {
	const struct nvkm_ltc_func *func;
	struct nvkm_subdev subdev;

	u32 ltc_nr;
	u32 lts_nr;

	struct mutex mutex; /* serialises CBC operations */
	u32 num_tags;
	u32 tag_base;
	struct nvkm_memory *tag_ram;

	int zbc_color_min;
	int zbc_color_max;
	u32 zbc_color[NVKM_LTC_MAX_ZBC_COLOR_CNT][4];
	int zbc_depth_min;
	int zbc_depth_max;
	u32 zbc_depth[NVKM_LTC_MAX_ZBC_DEPTH_CNT];
	u32 zbc_stencil[NVKM_LTC_MAX_ZBC_DEPTH_CNT];
};

void nvkm_ltc_tags_clear(struct nvkm_device *, u32 first, u32 count);

int nvkm_ltc_zbc_color_get(struct nvkm_ltc *, int index, const u32[4]);
int nvkm_ltc_zbc_depth_get(struct nvkm_ltc *, int index, const u32);
int nvkm_ltc_zbc_stencil_get(struct nvkm_ltc *, int index, const u32);

void nvkm_ltc_invalidate(struct nvkm_ltc *);
void nvkm_ltc_flush(struct nvkm_ltc *);

int gf100_ltc_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_ltc **);
int gk104_ltc_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_ltc **);
int gm107_ltc_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_ltc **);
int gm200_ltc_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_ltc **);
int gp100_ltc_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_ltc **);
int gp102_ltc_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_ltc **);
int gp10b_ltc_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_ltc **);
int ga102_ltc_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_ltc **);
#endif
