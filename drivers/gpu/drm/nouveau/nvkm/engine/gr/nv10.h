/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NV10_GR_H__
#define __NV10_GR_H__
#include "priv.h"

int nv10_gr_new_(const struct nvkm_gr_func *, struct nvkm_device *, int index,
		 struct nvkm_gr **);
int nv10_gr_init(struct nvkm_gr *);
void nv10_gr_intr(struct nvkm_gr *);
void nv10_gr_tile(struct nvkm_gr *, int, struct nvkm_fb_tile *);

int nv10_gr_chan_new(struct nvkm_gr *, struct nvkm_fifo_chan *,
		     const struct nvkm_oclass *, struct nvkm_object **);
#endif
