/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_FIFO_PRIV_H__
#define __NVKM_FIFO_PRIV_H__
#define nvkm_fifo(p) container_of((p), struct nvkm_fifo, engine)
#include <engine/fifo.h>

int nvkm_fifo_ctor(const struct nvkm_fifo_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		   int nr, struct nvkm_fifo *);
void nvkm_fifo_uevent(struct nvkm_fifo *);
void nvkm_fifo_kevent(struct nvkm_fifo *, int chid);
void nvkm_fifo_recover_chan(struct nvkm_fifo *, int chid);

struct nvkm_fifo_chan *
nvkm_fifo_chan_inst_locked(struct nvkm_fifo *, u64 inst);

struct nvkm_fifo_chan_oclass;
struct nvkm_fifo_func {
	void *(*dtor)(struct nvkm_fifo *);
	int (*oneinit)(struct nvkm_fifo *);
	int (*info)(struct nvkm_fifo *, u64 mthd, u64 *data);
	void (*init)(struct nvkm_fifo *);
	void (*fini)(struct nvkm_fifo *);
	void (*intr)(struct nvkm_fifo *);
	void (*fault)(struct nvkm_fifo *, struct nvkm_fault_data *);
	int (*engine_id)(struct nvkm_fifo *, struct nvkm_engine *);
	struct nvkm_engine *(*id_engine)(struct nvkm_fifo *, int engi);
	void (*pause)(struct nvkm_fifo *, unsigned long *);
	void (*start)(struct nvkm_fifo *, unsigned long *);
	void (*uevent_init)(struct nvkm_fifo *);
	void (*uevent_fini)(struct nvkm_fifo *);
	void (*recover_chan)(struct nvkm_fifo *, int chid);
	int (*class_get)(struct nvkm_fifo *, int index, struct nvkm_oclass *);
	int (*class_new)(struct nvkm_fifo *, const struct nvkm_oclass *,
			 void *, u32, struct nvkm_object **);
	const struct nvkm_fifo_chan_oclass *chan[];
};

void nv04_fifo_intr(struct nvkm_fifo *);
int nv04_fifo_engine_id(struct nvkm_fifo *, struct nvkm_engine *);
struct nvkm_engine *nv04_fifo_id_engine(struct nvkm_fifo *, int);
void nv04_fifo_pause(struct nvkm_fifo *, unsigned long *);
void nv04_fifo_start(struct nvkm_fifo *, unsigned long *);

void gf100_fifo_intr_fault(struct nvkm_fifo *, int);

int gk104_fifo_engine_id(struct nvkm_fifo *, struct nvkm_engine *);
struct nvkm_engine *gk104_fifo_id_engine(struct nvkm_fifo *, int);
#endif
