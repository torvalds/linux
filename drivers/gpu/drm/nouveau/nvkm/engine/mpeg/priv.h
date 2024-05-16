/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_MPEG_PRIV_H__
#define __NVKM_MPEG_PRIV_H__
#include <engine/mpeg.h>
struct nvkm_chan;

int nv31_mpeg_init(struct nvkm_engine *);
void nv31_mpeg_tile(struct nvkm_engine *, int, struct nvkm_fb_tile *);
extern const struct nvkm_object_func nv31_mpeg_object;

bool nv40_mpeg_mthd_dma(struct nvkm_device *, u32, u32);

int nv50_mpeg_init(struct nvkm_engine *);
void nv50_mpeg_intr(struct nvkm_engine *);

extern const struct nvkm_object_func nv50_mpeg_cclass;
#endif
