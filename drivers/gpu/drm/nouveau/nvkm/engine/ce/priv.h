/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_CE_PRIV_H__
#define __NVKM_CE_PRIV_H__
#include <engine/ce.h>

void gt215_ce_intr(struct nvkm_falcon *, struct nvkm_chan *);
void gk104_ce_intr(struct nvkm_engine *);
void gp100_ce_intr(struct nvkm_engine *);

extern const struct nvkm_object_func gv100_ce_cclass;

int ga100_ce_oneinit(struct nvkm_engine *);
int ga100_ce_init(struct nvkm_engine *);
int ga100_ce_fini(struct nvkm_engine *, bool);
#endif
