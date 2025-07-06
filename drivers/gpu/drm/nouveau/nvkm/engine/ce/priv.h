/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_CE_PRIV_H__
#define __NVKM_CE_PRIV_H__
#include <engine/ce.h>

int r535_ce_new(const struct nvkm_engine_func *, struct nvkm_device *,
		enum nvkm_subdev_type, int, struct nvkm_engine **);

void gt215_ce_intr(struct nvkm_falcon *, struct nvkm_chan *);
void gk104_ce_intr(struct nvkm_engine *);
void gp100_ce_intr(struct nvkm_engine *);

extern const struct nvkm_object_func gv100_ce_cclass;

int ga100_ce_oneinit(struct nvkm_engine *);
int ga100_ce_init(struct nvkm_engine *);
int ga100_ce_fini(struct nvkm_engine *, bool);
int ga100_ce_nonstall(struct nvkm_engine *);

u32 gb202_ce_grce_mask(struct nvkm_device *);
#endif
