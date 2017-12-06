/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_LTC_H__
#define __NVKM_LTC_H__
#include <core/subdev.h>
#include <core/mm.h>

#define NVKM_LTC_MAX_ZBC_CNT 16

struct nvkm_ltc {
	const struct nvkm_ltc_func *func;
	struct nvkm_subdev subdev;

	u32 ltc_nr;
	u32 lts_nr;

	u32 num_tags;
	u32 tag_base;
	struct nvkm_mm tags;
	struct nvkm_mm_node *tag_ram;

	int zbc_min;
	int zbc_max;
	u32 zbc_color[NVKM_LTC_MAX_ZBC_CNT][4];
	u32 zbc_depth[NVKM_LTC_MAX_ZBC_CNT];
};

int nvkm_ltc_tags_alloc(struct nvkm_ltc *, u32 count, struct nvkm_mm_node **);
void nvkm_ltc_tags_free(struct nvkm_ltc *, struct nvkm_mm_node **);
void nvkm_ltc_tags_clear(struct nvkm_ltc *, u32 first, u32 count);

int nvkm_ltc_zbc_color_get(struct nvkm_ltc *, int index, const u32[4]);
int nvkm_ltc_zbc_depth_get(struct nvkm_ltc *, int index, const u32);

void nvkm_ltc_invalidate(struct nvkm_ltc *);
void nvkm_ltc_flush(struct nvkm_ltc *);

int gf100_ltc_new(struct nvkm_device *, int, struct nvkm_ltc **);
int gk104_ltc_new(struct nvkm_device *, int, struct nvkm_ltc **);
int gk20a_ltc_new(struct nvkm_device *, int, struct nvkm_ltc **);
int gm107_ltc_new(struct nvkm_device *, int, struct nvkm_ltc **);
int gm200_ltc_new(struct nvkm_device *, int, struct nvkm_ltc **);
int gp100_ltc_new(struct nvkm_device *, int, struct nvkm_ltc **);
#endif
