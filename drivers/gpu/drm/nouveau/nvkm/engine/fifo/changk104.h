#ifndef __GK104_FIFO_CHAN_H__
#define __GK104_FIFO_CHAN_H__
#include "chan.h"
#include "gk104.h"

struct gk104_fifo_base {
	struct nvkm_fifo_base base;
	struct nvkm_gpuobj *pgd;
	struct nvkm_vm *vm;
};

struct gk104_fifo_chan {
	struct nvkm_fifo_chan base;
	u32 engine;
	enum {
		STOPPED,
		RUNNING,
		KILLED
	} state;
};

extern struct nvkm_oclass gk104_fifo_cclass;
extern struct nvkm_oclass gk104_fifo_sclass[];
extern struct nvkm_ofuncs gk104_fifo_chan_ofuncs;

extern struct nvkm_oclass gm204_fifo_sclass[];
#endif
