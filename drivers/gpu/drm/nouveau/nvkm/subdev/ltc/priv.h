#ifndef __NVKM_LTC_PRIV_H__
#define __NVKM_LTC_PRIV_H__
#define nvkm_ltc(p) container_of((p), struct nvkm_ltc, subdev)
#include <subdev/ltc.h>

int nvkm_ltc_new_(const struct nvkm_ltc_func *, struct nvkm_device *,
		  int index, struct nvkm_ltc **);

struct nvkm_ltc_func {
	int  (*oneinit)(struct nvkm_ltc *);
	void (*init)(struct nvkm_ltc *);
	void (*intr)(struct nvkm_ltc *);

	void (*cbc_clear)(struct nvkm_ltc *, u32 start, u32 limit);
	void (*cbc_wait)(struct nvkm_ltc *);

	int zbc;
	void (*zbc_clear_color)(struct nvkm_ltc *, int, const u32[4]);
	void (*zbc_clear_depth)(struct nvkm_ltc *, int, const u32);

	void (*invalidate)(struct nvkm_ltc *);
	void (*flush)(struct nvkm_ltc *);
};

int gf100_ltc_oneinit(struct nvkm_ltc *);
int gf100_ltc_oneinit_tag_ram(struct nvkm_ltc *);
void gf100_ltc_intr(struct nvkm_ltc *);
void gf100_ltc_cbc_clear(struct nvkm_ltc *, u32, u32);
void gf100_ltc_cbc_wait(struct nvkm_ltc *);
void gf100_ltc_zbc_clear_color(struct nvkm_ltc *, int, const u32[4]);
void gf100_ltc_zbc_clear_depth(struct nvkm_ltc *, int, const u32);
void gf100_ltc_invalidate(struct nvkm_ltc *);
void gf100_ltc_flush(struct nvkm_ltc *);
#endif
