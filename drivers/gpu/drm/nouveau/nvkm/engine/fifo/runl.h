#ifndef __NVKM_RUNL_H__
#define __NVKM_RUNL_H__
#include <core/intr.h>
struct nvkm_cctx;
struct nvkm_cgrp;
struct nvkm_chan;
struct nvkm_memory;
struct nvkm_object;
struct nvkm_vctx;
enum nvkm_subdev_type;

struct nvkm_engn {
	const struct nvkm_engn_func {
		bool (*chsw)(struct nvkm_engn *);
		int (*cxid)(struct nvkm_engn *, bool *cgid);
		void (*mmu_fault_trigger)(struct nvkm_engn *);
		bool (*mmu_fault_triggered)(struct nvkm_engn *);
		int (*ctor)(struct nvkm_engn *, struct nvkm_vctx *);
		void (*bind)(struct nvkm_engn *, struct nvkm_cctx *, struct nvkm_chan *);
		int (*ramht_add)(struct nvkm_engn *, struct nvkm_object *, struct nvkm_chan *);
		void (*ramht_del)(struct nvkm_chan *, int hash);
	} *func;
	struct nvkm_runl *runl;
	int id;

	struct nvkm_engine *engine;

	int fault;

	struct list_head head;
};

#define ENGN_PRINT(e,l,p,f,a...)                                                           \
	RUNL_PRINT((e)->runl, l, p, "%02d[%8s]:"f, (e)->id, (e)->engine->subdev.name, ##a)
#define ENGN_DEBUG(e,f,a...) ENGN_PRINT((e), DEBUG,   info, " "f"\n", ##a)

struct nvkm_runl {
	const struct nvkm_runl_func {
		void (*init)(struct nvkm_runl *);
		void (*fini)(struct nvkm_runl *);
		int runqs;
		u8 size;
		int (*update)(struct nvkm_runl *);
		void (*insert_cgrp)(struct nvkm_cgrp *, struct nvkm_memory *, u64 offset);
		void (*insert_chan)(struct nvkm_chan *, struct nvkm_memory *, u64 offset);
		void (*commit)(struct nvkm_runl *, struct nvkm_memory *, u32 start, int count);
		int (*wait)(struct nvkm_runl *);
		bool (*pending)(struct nvkm_runl *);
		void (*block)(struct nvkm_runl *, u32 engm);
		void (*allow)(struct nvkm_runl *, u32 engm);
		void (*fault_clear)(struct nvkm_runl *);
		void (*preempt)(struct nvkm_runl *);
		bool (*preempt_pending)(struct nvkm_runl *);
	} *func;
	struct nvkm_fifo *fifo;
	int id;
	u32 addr;
	u32 chan;
	u16 doorbell;

	struct nvkm_chid *cgid;
#define NVKM_CHAN_EVENT_ERRORED BIT(0)
	struct nvkm_chid *chid;

	struct list_head engns;

	struct nvkm_runq *runq[2];
	int runq_nr;

	struct nvkm_inth inth;

	struct list_head cgrps;
	int cgrp_nr;
	int chan_nr;
	atomic_t changed;
	struct nvkm_memory *mem;
	u32 offset;
	struct mutex mutex;

	int blocked;

	struct work_struct work;
	atomic_t rc_triggered;
	atomic_t rc_pending;

	struct list_head head;
};

struct nvkm_runl *nvkm_runl_new(struct nvkm_fifo *, int runi, u32 addr, int id_nr);
struct nvkm_runl *nvkm_runl_get(struct nvkm_fifo *, int runi, u32 addr);
struct nvkm_engn *nvkm_runl_add(struct nvkm_runl *, int engi, const struct nvkm_engn_func *,
				enum nvkm_subdev_type, int inst);
void nvkm_runl_del(struct nvkm_runl *);
void nvkm_runl_fini(struct nvkm_runl *);
void nvkm_runl_block(struct nvkm_runl *);
void nvkm_runl_allow(struct nvkm_runl *);
void nvkm_runl_update_locked(struct nvkm_runl *, bool wait);
bool nvkm_runl_update_pending(struct nvkm_runl *);
int nvkm_runl_preempt_wait(struct nvkm_runl *);

void nvkm_runl_rc_engn(struct nvkm_runl *, struct nvkm_engn *);
void nvkm_runl_rc_cgrp(struct nvkm_cgrp *);

struct nvkm_cgrp *nvkm_runl_cgrp_get_cgid(struct nvkm_runl *, int cgid, unsigned long *irqflags);
struct nvkm_chan *nvkm_runl_chan_get_chid(struct nvkm_runl *, int chid, unsigned long *irqflags);
struct nvkm_chan *nvkm_runl_chan_get_inst(struct nvkm_runl *, u64 inst, unsigned long *irqflags);

#define nvkm_runl_find_engn(engn,runl,cond) nvkm_list_find(engn, &(runl)->engns, head, (cond))

#define nvkm_runl_first(fifo) list_first_entry(&(fifo)->runls, struct nvkm_runl, head)
#define nvkm_runl_foreach(runl,fifo) list_for_each_entry((runl), &(fifo)->runls, head)
#define nvkm_runl_foreach_cond(runl,fifo,cond) nvkm_list_foreach(runl, &(fifo)->runls, head, (cond))
#define nvkm_runl_foreach_engn(engn,runl) list_for_each_entry((engn), &(runl)->engns, head)
#define nvkm_runl_foreach_engn_cond(engn,runl,cond) \
	nvkm_list_foreach(engn, &(runl)->engns, head, (cond))
#define nvkm_runl_foreach_cgrp(cgrp,runl) list_for_each_entry((cgrp), &(runl)->cgrps, head)
#define nvkm_runl_foreach_cgrp_safe(cgrp,gtmp,runl) \
	list_for_each_entry_safe((cgrp), (gtmp), &(runl)->cgrps, head)

#define RUNL_PRINT(r,l,p,f,a...)                                                          \
	nvkm_printk__(&(r)->fifo->engine.subdev, NV_DBG_##l, p, "%06x:"f, (r)->addr, ##a)
#define RUNL_ERROR(r,f,a...) RUNL_PRINT((r), ERROR,    err, " "f"\n", ##a)
#define RUNL_DEBUG(r,f,a...) RUNL_PRINT((r), DEBUG,   info, " "f"\n", ##a)
#define RUNL_TRACE(r,f,a...) RUNL_PRINT((r), TRACE,   info, " "f"\n", ##a)
#endif
