#ifndef __GK104_FIFO_CHAN_H__
#define __GK104_FIFO_CHAN_H__
#define gk104_fifo_chan(p) container_of((p), struct gk104_fifo_chan, base)
#include "chan.h"
#include "gk104.h"

struct gk104_fifo_chan {
	struct nvkm_fifo_chan base;
	struct gk104_fifo *fifo;
	int engine;

	struct list_head head;
	bool killed;

	struct nvkm_gpuobj *pgd;
	struct nvkm_vm *vm;

	struct {
		struct nvkm_gpuobj *inst;
		struct nvkm_vma vma;
	} engn[NVKM_SUBDEV_NR];
};

int gk104_fifo_gpfifo_new(struct nvkm_fifo *, const struct nvkm_oclass *,
			  void *data, u32 size, struct nvkm_object **);

extern const struct nvkm_fifo_chan_oclass gk104_fifo_gpfifo_oclass;
extern const struct nvkm_fifo_chan_oclass gm204_fifo_gpfifo_oclass;
#endif
