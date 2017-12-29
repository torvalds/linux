/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NV31_MPEG_H__
#define __NV31_MPEG_H__
#define nv31_mpeg(p) container_of((p), struct nv31_mpeg, engine)
#include "priv.h"
#include <engine/mpeg.h>

struct nv31_mpeg {
	const struct nv31_mpeg_func *func;
	struct nvkm_engine engine;
	struct nv31_mpeg_chan *chan;
};

int nv31_mpeg_new_(const struct nv31_mpeg_func *, struct nvkm_device *,
		   int index, struct nvkm_engine **);

struct nv31_mpeg_func {
	bool (*mthd_dma)(struct nvkm_device *, u32 mthd, u32 data);
};

#define nv31_mpeg_chan(p) container_of((p), struct nv31_mpeg_chan, object)
#include <core/object.h>

struct nv31_mpeg_chan {
	struct nvkm_object object;
	struct nv31_mpeg *mpeg;
	struct nvkm_fifo_chan *fifo;
};

int nv31_mpeg_chan_new(struct nvkm_fifo_chan *, const struct nvkm_oclass *,
		       struct nvkm_object **);
#endif
