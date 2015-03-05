#ifndef __NVKM_LTC_H__
#define __NVKM_LTC_H__
#include <core/subdev.h>
struct nvkm_mm_node;

#define NVKM_LTC_MAX_ZBC_CNT 16

struct nvkm_ltc {
	struct nvkm_subdev base;

	int  (*tags_alloc)(struct nvkm_ltc *, u32 count,
			   struct nvkm_mm_node **);
	void (*tags_free)(struct nvkm_ltc *, struct nvkm_mm_node **);
	void (*tags_clear)(struct nvkm_ltc *, u32 first, u32 count);

	int zbc_min;
	int zbc_max;
	int (*zbc_color_get)(struct nvkm_ltc *, int index, const u32[4]);
	int (*zbc_depth_get)(struct nvkm_ltc *, int index, const u32);
};

static inline struct nvkm_ltc *
nvkm_ltc(void *obj)
{
	return (void *)nvkm_subdev(obj, NVDEV_SUBDEV_LTC);
}

extern struct nvkm_oclass *gf100_ltc_oclass;
extern struct nvkm_oclass *gk104_ltc_oclass;
extern struct nvkm_oclass *gm107_ltc_oclass;
#endif
