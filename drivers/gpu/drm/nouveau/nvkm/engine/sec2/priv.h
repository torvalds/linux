/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_SEC2_PRIV_H__
#define __NVKM_SEC2_PRIV_H__
#include <engine/sec2.h>
struct nvkm_acr_lsfw;

int r535_sec2_new(const struct nvkm_sec2_func *,
		  struct nvkm_device *, enum nvkm_subdev_type, int, u32 addr, struct nvkm_sec2 **);

struct nvkm_sec2_func {
	const struct nvkm_falcon_func *flcn;
	u8 unit_unload;
	u8 unit_acr;
	struct nvkm_intr *(*intr_vector)(struct nvkm_sec2 *, enum nvkm_intr_type *);
	irqreturn_t (*intr)(struct nvkm_inth *);
	int (*initmsg)(struct nvkm_sec2 *);
};

irqreturn_t gp102_sec2_intr(struct nvkm_inth *);
int gp102_sec2_initmsg(struct nvkm_sec2 *);

struct nvkm_sec2_fwif {
	int version;
	int (*load)(struct nvkm_sec2 *, int ver, const struct nvkm_sec2_fwif *);
	const struct nvkm_sec2_func *func;
	const struct nvkm_acr_lsf_func *acr;
};

int gp102_sec2_nofw(struct nvkm_sec2 *, int, const struct nvkm_sec2_fwif *);
int gp102_sec2_load(struct nvkm_sec2 *, int, const struct nvkm_sec2_fwif *);
extern const struct nvkm_sec2_func gp102_sec2;
extern const struct nvkm_acr_lsf_func gp102_sec2_acr_1;
void gp102_sec2_acr_bld_write_1(struct nvkm_acr *, u32, struct nvkm_acr_lsfw *);
void gp102_sec2_acr_bld_patch_1(struct nvkm_acr *, u32, s64);

int nvkm_sec2_new_(const struct nvkm_sec2_fwif *, struct nvkm_device *, enum nvkm_subdev_type,
		   int, u32 addr, struct nvkm_sec2 **);
#endif
