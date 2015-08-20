#ifndef __GF100_FIFO_CHAN_H__
#define __GF100_FIFO_CHAN_H__
#include "chan.h"
#include "gf100.h"

struct gf100_fifo_base {
	struct nvkm_fifo_base base;
	struct nvkm_gpuobj *pgd;
	struct nvkm_vm *vm;
};

struct gf100_fifo_chan {
	struct nvkm_fifo_chan base;
	enum {
		STOPPED,
		RUNNING,
		KILLED
	} state;
};

extern struct nvkm_oclass gf100_fifo_cclass;
extern struct nvkm_oclass gf100_fifo_sclass[];
#endif
