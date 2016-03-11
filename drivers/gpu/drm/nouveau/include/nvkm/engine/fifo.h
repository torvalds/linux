#ifndef __NVKM_FIFO_H__
#define __NVKM_FIFO_H__
#include <core/engine.h>
#include <core/event.h>

#define NVKM_FIFO_CHID_NR 4096

struct nvkm_fifo_engn {
	struct nvkm_object *object;
	int refcount;
	int usecount;
};

struct nvkm_fifo_chan {
	const struct nvkm_fifo_chan_func *func;
	struct nvkm_fifo *fifo;
	u64 engines;
	struct nvkm_object object;

	struct list_head head;
	u16 chid;
	struct nvkm_gpuobj *inst;
	struct nvkm_gpuobj *push;
	struct nvkm_vm *vm;
	void __iomem *user;
	u64 addr;
	u32 size;

	struct nvkm_fifo_engn engn[NVKM_SUBDEV_NR];
};

struct nvkm_fifo {
	const struct nvkm_fifo_func *func;
	struct nvkm_engine engine;

	DECLARE_BITMAP(mask, NVKM_FIFO_CHID_NR);
	int nr;
	struct list_head chan;
	spinlock_t lock;

	struct nvkm_event uevent; /* async user trigger */
	struct nvkm_event cevent; /* channel creation event */
};

void nvkm_fifo_pause(struct nvkm_fifo *, unsigned long *);
void nvkm_fifo_start(struct nvkm_fifo *, unsigned long *);

void nvkm_fifo_chan_put(struct nvkm_fifo *, unsigned long flags,
			struct nvkm_fifo_chan **);
struct nvkm_fifo_chan *
nvkm_fifo_chan_inst(struct nvkm_fifo *, u64 inst, unsigned long *flags);
struct nvkm_fifo_chan *
nvkm_fifo_chan_chid(struct nvkm_fifo *, int chid, unsigned long *flags);

int nv04_fifo_new(struct nvkm_device *, int, struct nvkm_fifo **);
int nv10_fifo_new(struct nvkm_device *, int, struct nvkm_fifo **);
int nv17_fifo_new(struct nvkm_device *, int, struct nvkm_fifo **);
int nv40_fifo_new(struct nvkm_device *, int, struct nvkm_fifo **);
int nv50_fifo_new(struct nvkm_device *, int, struct nvkm_fifo **);
int g84_fifo_new(struct nvkm_device *, int, struct nvkm_fifo **);
int gf100_fifo_new(struct nvkm_device *, int, struct nvkm_fifo **);
int gk104_fifo_new(struct nvkm_device *, int, struct nvkm_fifo **);
int gk110_fifo_new(struct nvkm_device *, int, struct nvkm_fifo **);
int gk208_fifo_new(struct nvkm_device *, int, struct nvkm_fifo **);
int gk20a_fifo_new(struct nvkm_device *, int, struct nvkm_fifo **);
int gm200_fifo_new(struct nvkm_device *, int, struct nvkm_fifo **);
int gm20b_fifo_new(struct nvkm_device *, int, struct nvkm_fifo **);
#endif
