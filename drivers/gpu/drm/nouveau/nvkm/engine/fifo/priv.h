/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_FIFO_PRIV_H__
#define __NVKM_FIFO_PRIV_H__
#define nvkm_fifo(p) container_of((p), struct nvkm_fifo, engine)
#include <engine/fifo.h>
#include <core/enum.h>
struct nvkm_cctx;
struct nvkm_cgrp;
struct nvkm_engn;
struct nvkm_memory;
struct nvkm_runl;
struct nvkm_runq;
struct nvkm_vctx;

struct nvkm_fifo_func {
	int (*chid_nr)(struct nvkm_fifo *);
	int (*chid_ctor)(struct nvkm_fifo *, int nr);
	int (*runq_nr)(struct nvkm_fifo *);
	int (*runl_ctor)(struct nvkm_fifo *);

	void (*init)(struct nvkm_fifo *);
	void (*init_pbdmas)(struct nvkm_fifo *, u32 mask);

	irqreturn_t (*intr)(struct nvkm_inth *);
	void (*intr_mmu_fault_unit)(struct nvkm_fifo *, int unit);
	void (*intr_ctxsw_timeout)(struct nvkm_fifo *, u32 engm);

	const struct nvkm_fifo_func_mmu_fault {
		void (*recover)(struct nvkm_fifo *, struct nvkm_fault_data *);
		const struct nvkm_enum *access;
		const struct nvkm_enum *engine;
		const struct nvkm_enum *reason;
		const struct nvkm_enum *hubclient;
		const struct nvkm_enum *gpcclient;
	} *mmu_fault;

	void (*pause)(struct nvkm_fifo *, unsigned long *);
	void (*start)(struct nvkm_fifo *, unsigned long *);

	int (*nonstall_ctor)(struct nvkm_fifo *);
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
	} chan;
};

int nvkm_fifo_new_(const struct nvkm_fifo_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		   struct nvkm_fifo **);

int nv04_fifo_chid_ctor(struct nvkm_fifo *, int);
int nv04_fifo_runl_ctor(struct nvkm_fifo *);
void nv04_fifo_init(struct nvkm_fifo *);
irqreturn_t nv04_fifo_intr(struct nvkm_inth *);
void nv04_fifo_pause(struct nvkm_fifo *, unsigned long *);
void nv04_fifo_start(struct nvkm_fifo *, unsigned long *);
extern const struct nvkm_runl_func nv04_runl;
extern const struct nvkm_engn_func nv04_engn;
extern const struct nvkm_cgrp_func nv04_cgrp;
extern const struct nvkm_chan_func_inst nv04_chan_inst;
extern const struct nvkm_chan_func_userd nv04_chan_userd;
void nv04_chan_ramfc_clear(struct nvkm_chan *);
void nv04_chan_start(struct nvkm_chan *);
void nv04_chan_stop(struct nvkm_chan *);
void nv04_eobj_ramht_del(struct nvkm_chan *, int);

int nv10_fifo_chid_nr(struct nvkm_fifo *);

int nv50_fifo_chid_nr(struct nvkm_fifo *);
int nv50_fifo_chid_ctor(struct nvkm_fifo *, int);
void nv50_fifo_init(struct nvkm_fifo *);
extern const struct nvkm_runl_func nv50_runl;
int nv50_runl_update(struct nvkm_runl *);
int nv50_runl_wait(struct nvkm_runl *);
extern const struct nvkm_engn_func nv50_engn_sw;
extern const struct nvkm_chan_func_inst nv50_chan_inst;
extern const struct nvkm_chan_func_userd nv50_chan_userd;
void nv50_chan_unbind(struct nvkm_chan *);
void nv50_chan_start(struct nvkm_chan *);
void nv50_chan_stop(struct nvkm_chan *);
void nv50_chan_preempt(struct nvkm_chan *);
int nv50_eobj_ramht_add(struct nvkm_engn *, struct nvkm_object *, struct nvkm_chan *);
void nv50_eobj_ramht_del(struct nvkm_chan *, int);

extern const struct nvkm_event_func g84_fifo_nonstall;
extern const struct nvkm_engn_func g84_engn;
extern const struct nvkm_chan_func g84_chan;

int gf100_fifo_chid_ctor(struct nvkm_fifo *, int);
int gf100_fifo_runq_nr(struct nvkm_fifo *);
bool gf100_fifo_intr_pbdma(struct nvkm_fifo *);
void gf100_fifo_intr_mmu_fault(struct nvkm_fifo *);
void gf100_fifo_intr_mmu_fault_unit(struct nvkm_fifo *, int);
void gf100_fifo_intr_sched(struct nvkm_fifo *);
void gf100_fifo_intr_ctxsw_timeout(struct nvkm_fifo *, u32);
void gf100_fifo_mmu_fault_recover(struct nvkm_fifo *, struct nvkm_fault_data *);
extern const struct nvkm_enum gf100_fifo_mmu_fault_access[];
extern const struct nvkm_event_func gf100_fifo_nonstall;
bool gf100_runl_preempt_pending(struct nvkm_runl *);
void gf100_runq_init(struct nvkm_runq *);
bool gf100_runq_intr(struct nvkm_runq *, struct nvkm_runl *);
void gf100_engn_mmu_fault_trigger(struct nvkm_engn *);
bool gf100_engn_mmu_fault_triggered(struct nvkm_engn *);
extern const struct nvkm_engn_func gf100_engn_sw;
extern const struct nvkm_chan_func_inst gf100_chan_inst;
void gf100_chan_userd_clear(struct nvkm_chan *);
void gf100_chan_preempt(struct nvkm_chan *);

int gk104_fifo_chid_nr(struct nvkm_fifo *);
int gk104_fifo_runl_ctor(struct nvkm_fifo *);
void gk104_fifo_init(struct nvkm_fifo *);
void gk104_fifo_init_pbdmas(struct nvkm_fifo *, u32);
irqreturn_t gk104_fifo_intr(struct nvkm_inth *);
void gk104_fifo_intr_runlist(struct nvkm_fifo *);
void gk104_fifo_intr_chsw(struct nvkm_fifo *);
void gk104_fifo_intr_bind(struct nvkm_fifo *);
extern const struct nvkm_fifo_func_mmu_fault gk104_fifo_mmu_fault;
extern const struct nvkm_enum gk104_fifo_mmu_fault_reason[];
extern const struct nvkm_enum gk104_fifo_mmu_fault_hubclient[];
extern const struct nvkm_enum gk104_fifo_mmu_fault_gpcclient[];
void gk104_runl_insert_chan(struct nvkm_chan *, struct nvkm_memory *, u64);
void gk104_runl_commit(struct nvkm_runl *, struct nvkm_memory *, u32, int);
bool gk104_runl_pending(struct nvkm_runl *);
void gk104_runl_block(struct nvkm_runl *, u32);
void gk104_runl_allow(struct nvkm_runl *, u32);
void gk104_runl_fault_clear(struct nvkm_runl *);
extern const struct nvkm_runq_func gk104_runq;
void gk104_runq_init(struct nvkm_runq *);
bool gk104_runq_intr(struct nvkm_runq *, struct nvkm_runl *);
extern const struct nvkm_bitfield gk104_runq_intr_0_names[];
bool gk104_runq_idle(struct nvkm_runq *);
extern const struct nvkm_engn_func gk104_engn;
bool gk104_engn_chsw(struct nvkm_engn *);
int gk104_engn_cxid(struct nvkm_engn *, bool *cgid);
int gk104_ectx_ctor(struct nvkm_engn *, struct nvkm_vctx *);
extern const struct nvkm_engn_func gk104_engn_ce;
extern const struct nvkm_chan_func_userd gk104_chan_userd;
extern const struct nvkm_chan_func_ramfc gk104_chan_ramfc;
void gk104_chan_bind(struct nvkm_chan *);
void gk104_chan_bind_inst(struct nvkm_chan *);
void gk104_chan_unbind(struct nvkm_chan *);
void gk104_chan_start(struct nvkm_chan *);
void gk104_chan_stop(struct nvkm_chan *);

int gk110_fifo_chid_ctor(struct nvkm_fifo *, int);
extern const struct nvkm_runl_func gk110_runl;
extern const struct nvkm_cgrp_func gk110_cgrp;
void gk110_runl_insert_cgrp(struct nvkm_cgrp *, struct nvkm_memory *, u64);
extern const struct nvkm_chan_func gk110_chan;
void gk110_chan_preempt(struct nvkm_chan *);

extern const struct nvkm_runq_func gk208_runq;
void gk208_runq_init(struct nvkm_runq *);

void gm107_fifo_intr_mmu_fault_unit(struct nvkm_fifo *, int);
extern const struct nvkm_fifo_func_mmu_fault gm107_fifo_mmu_fault;
extern const struct nvkm_runl_func gm107_runl;
extern const struct nvkm_chan_func gm107_chan;

int gm200_fifo_chid_nr(struct nvkm_fifo *);
int gm200_fifo_runq_nr(struct nvkm_fifo *);

extern const struct nvkm_enum gv100_fifo_mmu_fault_access[];
extern const struct nvkm_enum gv100_fifo_mmu_fault_reason[];
extern const struct nvkm_enum gv100_fifo_mmu_fault_hubclient[];
extern const struct nvkm_enum gv100_fifo_mmu_fault_gpcclient[];
void gv100_runl_insert_cgrp(struct nvkm_cgrp *, struct nvkm_memory *, u64);
void gv100_runl_insert_chan(struct nvkm_chan *, struct nvkm_memory *, u64);
void gv100_runl_preempt(struct nvkm_runl *);
extern const struct nvkm_runq_func gv100_runq;
extern const struct nvkm_engn_func gv100_engn;
void gv100_ectx_bind(struct nvkm_engn *, struct nvkm_cctx *, struct nvkm_chan *);
extern const struct nvkm_engn_func gv100_engn_ce;
int gv100_ectx_ce_ctor(struct nvkm_engn *, struct nvkm_vctx *);
void gv100_ectx_ce_bind(struct nvkm_engn *, struct nvkm_cctx *, struct nvkm_chan *);
extern const struct nvkm_chan_func_userd gv100_chan_userd;
extern const struct nvkm_chan_func_ramfc gv100_chan_ramfc;

void tu102_fifo_intr_ctxsw_timeout_info(struct nvkm_engn *, u32 info);
extern const struct nvkm_fifo_func_mmu_fault tu102_fifo_mmu_fault;

int ga100_fifo_runl_ctor(struct nvkm_fifo *);
int ga100_fifo_nonstall_ctor(struct nvkm_fifo *);
extern const struct nvkm_event_func ga100_fifo_nonstall;
extern const struct nvkm_runl_func ga100_runl;
extern const struct nvkm_runq_func ga100_runq;
extern const struct nvkm_engn_func ga100_engn;
extern const struct nvkm_engn_func ga100_engn_ce;
extern const struct nvkm_cgrp_func ga100_cgrp;
extern const struct nvkm_chan_func ga100_chan;

int nvkm_uchan_new(struct nvkm_fifo *, struct nvkm_cgrp *, const struct nvkm_oclass *,
		   void *argv, u32 argc, struct nvkm_object **);
int nvkm_ucgrp_new(struct nvkm_fifo *, const struct nvkm_oclass *, void *argv, u32 argc,
		   struct nvkm_object **);
#endif
