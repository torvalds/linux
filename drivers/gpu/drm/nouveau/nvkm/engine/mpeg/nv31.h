#ifndef __NV31_MPEG_H__
#define __NV31_MPEG_H__
#include <engine/mpeg.h>
#include <engine/fifo.h>

struct nv31_mpeg_chan {
	struct nvkm_object base;
	struct nvkm_fifo_chan *fifo;
};

struct nv31_mpeg {
	struct nvkm_mpeg base;
	struct nv31_mpeg_chan *chan;
	bool (*mthd_dma)(struct nvkm_device *, u32 mthd, u32 data);
};
#endif
