#ifndef __NVKM_SW_CHAN_H__
#define __NVKM_SW_CHAN_H__
#define nvkm_sw_chan(p) container_of((p), struct nvkm_sw_chan, object)
#include "priv.h"
#include <core/event.h>

struct nvkm_sw_chan {
	const struct nvkm_sw_chan_func *func;
	struct nvkm_object object;
	struct nvkm_sw *sw;
	struct nvkm_fifo_chan *fifo;
	struct list_head head;

	struct nvkm_event event;
};

struct nvkm_sw_chan_func {
	void *(*dtor)(struct nvkm_sw_chan *);
	bool (*mthd)(struct nvkm_sw_chan *, int subc, u32 mthd, u32 data);
};

int nvkm_sw_chan_ctor(const struct nvkm_sw_chan_func *, struct nvkm_sw *,
		      struct nvkm_fifo_chan *, const struct nvkm_oclass *,
		      struct nvkm_sw_chan *);
bool nvkm_sw_chan_mthd(struct nvkm_sw_chan *, int subc, u32 mthd, u32 data);
#endif
