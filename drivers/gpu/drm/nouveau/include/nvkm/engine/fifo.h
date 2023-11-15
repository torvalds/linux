/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_FIFO_H__
#define __NVKM_FIFO_H__
#include <core/engine.h>
#include <core/object.h>
#include <core/event.h>
#include <subdev/gsp.h>
struct nvkm_fault_data;

#define NVKM_FIFO_ENGN_NR 16

struct nvkm_chan {
	const struct nvkm_chan_func *func;
	char name[64];
	struct nvkm_cgrp *cgrp;
	int runq;

	struct nvkm_gpuobj *inst;
	struct nvkm_vmm *vmm;
	struct nvkm_gpuobj *push;
	int id;

	struct {
		struct nvkm_memory *mem;
		u32 base;
	} userd;

	u32 ramfc_offset;
	struct nvkm_gpuobj *ramfc;
	struct nvkm_gpuobj *cache;
	struct nvkm_gpuobj *eng;
	struct nvkm_gpuobj *pgd;
	struct nvkm_ramht *ramht;

	spinlock_t lock;
	atomic_t blocked;
	atomic_t errored;

	struct {
		struct nvkm_gsp_object object;
		struct {
			dma_addr_t addr;
			void *ptr;
		} mthdbuf;
		struct nvkm_vctx *grctx;
	} rm;

	struct list_head cctxs;
	struct list_head head;
};

struct nvkm_chan *nvkm_chan_get_chid(struct nvkm_engine *, int id, unsigned long *irqflags);
struct nvkm_chan *nvkm_chan_get_inst(struct nvkm_engine *, u64 inst, unsigned long *irqflags);
void nvkm_chan_put(struct nvkm_chan **, unsigned long irqflags);

struct nvkm_chan *nvkm_uchan_chan(struct nvkm_object *);

struct nvkm_fifo {
	const struct nvkm_fifo_func *func;
	struct nvkm_engine engine;

	struct nvkm_chid *chid;
	struct nvkm_chid *cgid;

	struct list_head runqs;
	struct list_head runls;

	struct {
#define NVKM_FIFO_NONSTALL_EVENT BIT(0)
		struct nvkm_event event;
		struct nvkm_inth intr;
	} nonstall;

	struct {
		u32 chan_msec;
	} timeout;

	struct {
		struct nvkm_memory *mem;
		struct nvkm_vma *bar1;

		struct mutex mutex;
		struct list_head list;
	} userd;

	struct {
		u32 mthdbuf_size;
	} rm;

	spinlock_t lock;
	struct mutex mutex;
};

void nvkm_fifo_fault(struct nvkm_fifo *, struct nvkm_fault_data *);
void nvkm_fifo_pause(struct nvkm_fifo *, unsigned long *);
void nvkm_fifo_start(struct nvkm_fifo *, unsigned long *);
bool nvkm_fifo_ctxsw_in_progress(struct nvkm_engine *);

int nv04_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int nv10_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int nv17_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int nv40_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int nv50_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int g84_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int g98_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int gf100_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int gk104_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int gk110_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int gk208_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int gk20a_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int gm107_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int gm200_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int gp100_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int gv100_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int tu102_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int ga100_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
int ga102_fifo_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fifo **);
#endif
