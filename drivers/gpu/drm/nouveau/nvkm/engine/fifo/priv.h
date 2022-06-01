/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_FIFO_PRIV_H__
#define __NVKM_FIFO_PRIV_H__
#define nvkm_fifo(p) container_of((p), struct nvkm_fifo, engine)
#include <engine/fifo.h>
struct nvkm_cgrp;
struct nvkm_memory;
struct gk104_fifo;
struct gk104_fifo_chan;

void nvkm_fifo_uevent(struct nvkm_fifo *);
void nvkm_fifo_kevent(struct nvkm_fifo *, int chid);
void nvkm_fifo_recover_chan(struct nvkm_fifo *, int chid);

struct nvkm_fifo_chan *
nvkm_fifo_chan_inst_locked(struct nvkm_fifo *, u64 inst);

struct nvkm_fifo_chan_oclass;
struct nvkm_fifo_func {
	void *(*dtor)(struct nvkm_fifo *);

	int (*oneinit)(struct nvkm_fifo *);
	int (*chid_nr)(struct nvkm_fifo *);
	int (*chid_ctor)(struct nvkm_fifo *, int nr);

	int (*info)(struct nvkm_fifo *, u64 mthd, u64 *data);
	void (*init)(struct nvkm_fifo *);
	void (*fini)(struct nvkm_fifo *);

	void (*intr)(struct nvkm_fifo *);
	void (*intr_mmu_fault_unit)(struct nvkm_fifo *, int unit);

	const struct nvkm_fifo_func_mmu_fault {
		void (*recover)(struct nvkm_fifo *, struct nvkm_fault_data *);
	} *mmu_fault;

	struct {
		const struct nvkm_enum *access;
		const struct nvkm_enum *engine;
		const struct nvkm_enum *reason;
		const struct nvkm_enum *hubclient;
		const struct nvkm_enum *gpcclient;
	} fault;

	int (*engine_id)(struct nvkm_fifo *, struct nvkm_engine *);
	struct nvkm_engine *(*id_engine)(struct nvkm_fifo *, int engi);
	void (*pause)(struct nvkm_fifo *, unsigned long *);
	void (*start)(struct nvkm_fifo *, unsigned long *);
	void (*uevent_init)(struct nvkm_fifo *);
	void (*uevent_fini)(struct nvkm_fifo *);
	void (*recover_chan)(struct nvkm_fifo *, int chid);

	const struct gk104_fifo_runlist_func {
		u8 size;
		void (*cgrp)(struct nvkm_fifo_cgrp *,
			     struct nvkm_memory *, u32 offset);
		void (*chan)(struct gk104_fifo_chan *,
			     struct nvkm_memory *, u32 offset);
		void (*commit)(struct gk104_fifo *, int runl,
			       struct nvkm_memory *, int entries);
	} *runlist;

	const struct gk104_fifo_pbdma_func {
		int (*nr)(struct gk104_fifo *);
		void (*init)(struct gk104_fifo *);
		void (*init_timeout)(struct gk104_fifo *);
	} *pbdma;

	struct nvkm_fifo_func_cgrp {
		struct nvkm_sclass user;
		const struct nvkm_cgrp_func *func;
		bool force;
	} cgrp;

	struct nvkm_fifo_func_chan {
		struct nvkm_sclass user;
		const struct nvkm_chan_func *func;
		const struct nvkm_fifo_chan_oclass {
			int (*ctor)(struct nvkm_fifo *, const struct nvkm_oclass *,
			void *data, u32 size, struct nvkm_object **);
		} *oclass;
		int (*ctor)(struct gk104_fifo *, const struct nvkm_oclass *, void *, u32,
			    struct nvkm_object **);
	} chan;
};

int nvkm_fifo_ctor(const struct nvkm_fifo_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		   struct nvkm_fifo *);

int nv04_fifo_chid_ctor(struct nvkm_fifo *, int);
void nv04_fifo_init(struct nvkm_fifo *);
void nv04_fifo_intr(struct nvkm_fifo *);
int nv04_fifo_engine_id(struct nvkm_fifo *, struct nvkm_engine *);
struct nvkm_engine *nv04_fifo_id_engine(struct nvkm_fifo *, int);
void nv04_fifo_pause(struct nvkm_fifo *, unsigned long *);
void nv04_fifo_start(struct nvkm_fifo *, unsigned long *);
extern const struct nvkm_cgrp_func nv04_cgrp;

int nv10_fifo_chid_nr(struct nvkm_fifo *);

int nv50_fifo_chid_nr(struct nvkm_fifo *);
int nv50_fifo_chid_ctor(struct nvkm_fifo *, int);

extern const struct nvkm_chan_func g84_chan;

int gf100_fifo_chid_ctor(struct nvkm_fifo *, int);
void gf100_fifo_intr_mmu_fault_unit(struct nvkm_fifo *, int);

int gk104_fifo_chid_nr(struct nvkm_fifo *);
void gk104_fifo_intr(struct nvkm_fifo *);
extern const struct nvkm_fifo_func_mmu_fault gk104_fifo_mmu_fault;
void gk104_fifo_fault(struct nvkm_fifo *, struct nvkm_fault_data *);
void gk104_fifo_recover_chan(struct nvkm_fifo *, int);
int gk104_fifo_engine_id(struct nvkm_fifo *, struct nvkm_engine *);
struct nvkm_engine *gk104_fifo_id_engine(struct nvkm_fifo *, int);

int gk110_fifo_chid_ctor(struct nvkm_fifo *, int);
extern const struct nvkm_cgrp_func gk110_cgrp;
extern const struct nvkm_chan_func gk110_chan;

void gm107_fifo_intr_mmu_fault_unit(struct nvkm_fifo *, int);
extern const struct nvkm_fifo_func_mmu_fault gm107_fifo_mmu_fault;
extern const struct nvkm_chan_func gm107_chan;

int gm200_fifo_chid_nr(struct nvkm_fifo *);

void gp100_fifo_intr_mmu_fault_unit(struct nvkm_fifo *, int);

extern const struct nvkm_fifo_func_mmu_fault tu102_fifo_mmu_fault;

int nvkm_uchan_new(struct nvkm_fifo *, struct nvkm_cgrp *, const struct nvkm_oclass *,
		   void *argv, u32 argc, struct nvkm_object **);
#endif
