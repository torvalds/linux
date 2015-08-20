#ifndef __NV31_MPEG_H__
#define __NV31_MPEG_H__
#define nv31_mpeg(p) container_of((p), struct nv31_mpeg, base.engine)
#include "priv.h"
#include <engine/mpeg.h>

struct nv31_mpeg {
	struct nvkm_mpeg base;
	struct nv31_mpeg_chan *chan;
	bool (*mthd_dma)(struct nvkm_device *, u32 mthd, u32 data);
};

#define nv31_mpeg_chan(p) container_of((p), struct nv31_mpeg_chan, object)

struct nv31_mpeg_chan {
	struct nvkm_object object;
	struct nv31_mpeg *mpeg;
	struct nvkm_fifo_chan *fifo;
};

int nv31_mpeg_chan_new(struct nvkm_fifo_chan *, const struct nvkm_oclass *,
		       struct nvkm_object **);
#endif
