/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_FIFO_PRIV_H__
#define __NVKM_FIFO_PRIV_H__
#define nvkm_fifo(p) container_of((p), struct nvkm_fifo, engine)
#include <engine/fifo.h>
#include <core/enum.h>
struct nvkm_cgrp;
struct nvkm_memory;
struct nvkm_runl;
struct nvkm_runq;
struct gk104_fifo;
struct gk104_fifo_chan;

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
	int (*runq_nr)(struct nvkm_fifo *);
	int (*runl_ctor)(struct nvkm_fifo *);

	void (*init)(struct nvkm_fifo *);
	void (*init_pbdmas)(struct nvkm_fifo *, u32 mask);

	void (*fini)(struct nvkm_fifo *);

	irqreturn_t (*intr)(struct nvkm_inth *);
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
	void (*pause)(struct nvkm_fifo *, unsigned long *);
	void (*start)(struct nvkm_fifo *, unsigned long *);
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

	const struct nvkm_event_func *nonstall;

	const struct nvkm_runl_func *runl;
	const struct nvkm_runq_func *runq;
	const struct nvkm_engn_func *engn;
	const struct nvkm_engn_func *engn_sw;
	const struct nvkm_engn_func *engn_ce;

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
int nv04_fifo_runl_ctor(struct nvkm_fifo *);
void nv04_fifo_init(struct nvkm_fifo *);
irqreturn_t nv04_fifo_intr(struct nvkm_inth *);
int nv04_fifo_engine_id(struct nvkm_fifo *, struct nvkm_engine *);
void nv04_fifo_pause(struct nvkm_fifo *, unsigned long *);
void nv04_fifo_start(struct nvkm_fifo *, unsigned long *);
extern const struct nvkm_runl_func nv04_runl;
extern const struct nvkm_engn_func nv04_engn;
extern const struct nvkm_cgrp_func nv04_cgrp;

int nv10_fifo_chid_nr(struct nvkm_fifo *);

int nv50_fifo_chid_nr(struct nvkm_fifo *);
int nv50_fifo_chid_ctor(struct nvkm_fifo *, int);
extern const struct nvkm_runl_func nv50_runl;
extern const struct nvkm_engn_func nv50_engn_sw;

extern const struct nvkm_event_func g84_fifo_nonstall;
extern const struct nvkm_engn_func g84_engn;
extern const struct nvkm_chan_func g84_chan;

int gf100_fifo_chid_ctor(struct nvkm_fifo *, int);
int gf100_fifo_runq_nr(struct nvkm_fifo *);
bool gf100_fifo_intr_pbdma(struct nvkm_fifo *);
void gf100_fifo_intr_mmu_fault_unit(struct nvkm_fifo *, int);
extern const struct nvkm_event_func gf100_fifo_nonstall;
void gf100_runq_init(struct nvkm_runq *);
bool gf100_runq_intr(struct nvkm_runq *, struct nvkm_runl *);
extern const struct nvkm_engn_func gf100_engn_sw;

int gk104_fifo_chid_nr(struct nvkm_fifo *);
int gk104_fifo_runl_ctor(struct nvkm_fifo *);
void gk104_fifo_init_pbdmas(struct nvkm_fifo *, u32);
irqreturn_t gk104_fifo_intr(struct nvkm_inth *);
void gk104_fifo_intr_chsw(struct nvkm_fifo *);
void gk104_fifo_intr_bind(struct nvkm_fifo *);
extern const struct nvkm_fifo_func_mmu_fault gk104_fifo_mmu_fault;
void gk104_fifo_fault(struct nvkm_fifo *, struct nvkm_fault_data *);
void gk104_fifo_recover_chan(struct nvkm_fifo *, int);
int gk104_fifo_engine_id(struct nvkm_fifo *, struct nvkm_engine *);
extern const struct nvkm_runq_func gk104_runq;
void gk104_runq_init(struct nvkm_runq *);
bool gk104_runq_intr(struct nvkm_runq *, struct nvkm_runl *);
extern const struct nvkm_bitfield gk104_runq_intr_0_names[];
extern const struct nvkm_engn_func gk104_engn;
extern const struct nvkm_engn_func gk104_engn_ce;

int gk110_fifo_chid_ctor(struct nvkm_fifo *, int);
extern const struct nvkm_runl_func gk110_runl;
extern const struct nvkm_cgrp_func gk110_cgrp;
extern const struct nvkm_chan_func gk110_chan;

extern const struct nvkm_runq_func gk208_runq;
void gk208_runq_init(struct nvkm_runq *);

void gm107_fifo_intr_mmu_fault_unit(struct nvkm_fifo *, int);
extern const struct nvkm_fifo_func_mmu_fault gm107_fifo_mmu_fault;
extern const struct nvkm_runl_func gm107_runl;
extern const struct nvkm_chan_func gm107_chan;

int gm200_fifo_chid_nr(struct nvkm_fifo *);
int gm200_fifo_runq_nr(struct nvkm_fifo *);

void gp100_fifo_intr_mmu_fault_unit(struct nvkm_fifo *, int);

extern const struct nvkm_runq_func gv100_runq;
extern const struct nvkm_engn_func gv100_engn;
extern const struct nvkm_engn_func gv100_engn_ce;

extern const struct nvkm_fifo_func_mmu_fault tu102_fifo_mmu_fault;

int nvkm_uchan_new(struct nvkm_fifo *, struct nvkm_cgrp *, const struct nvkm_oclass *,
		   void *argv, u32 argc, struct nvkm_object **);
#endif
