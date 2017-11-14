#ifndef __GF100_FIFO_CHAN_H__
#define __GF100_FIFO_CHAN_H__
#define gf100_fifo_chan(p) container_of((p), struct gf100_fifo_chan, base)
#include "chan.h"
#include "gf100.h"

struct gf100_fifo_chan {
	struct nvkm_fifo_chan base;
	struct gf100_fifo *fifo;

	struct list_head head;
	bool killed;

	struct {
		struct nvkm_gpuobj *inst;
		struct nvkm_vma *vma;
	} engn[NVKM_SUBDEV_NR];
};

extern const struct nvkm_fifo_chan_oclass gf100_fifo_gpfifo_oclass;
#endif
