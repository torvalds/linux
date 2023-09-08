/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_RUNQ_H__
#define __NVKM_RUNQ_H__
#include <core/os.h>
struct nvkm_runl;

struct nvkm_runq {
	const struct nvkm_runq_func {
		void (*init)(struct nvkm_runq *);
		bool (*intr)(struct nvkm_runq *, struct nvkm_runl *);
		const struct nvkm_bitfield *intr_0_names;
		bool (*intr_1_ctxnotvalid)(struct nvkm_runq *, int chid);
		bool (*idle)(struct nvkm_runq *);
	} *func;
	struct nvkm_fifo *fifo;
	int id;

	struct list_head head;
};

struct nvkm_runq *nvkm_runq_new(struct nvkm_fifo *, int pbid);
void nvkm_runq_del(struct nvkm_runq *);

#define nvkm_runq_foreach(runq,fifo) list_for_each_entry((runq), &(fifo)->runqs, head)
#define nvkm_runq_foreach_cond(runq,fifo,cond) nvkm_list_foreach(runq, &(fifo)->runqs, head, (cond))

#define RUNQ_PRINT(r,l,p,f,a...)							   \
	nvkm_printk__(&(r)->fifo->engine.subdev, NV_DBG_##l, p, "PBDMA%d:"f, (r)->id, ##a)
#define RUNQ_ERROR(r,f,a...) RUNQ_PRINT((r), ERROR,    err, " "f"\n", ##a)
#define RUNQ_DEBUG(r,f,a...) RUNQ_PRINT((r), DEBUG,   info, " "f"\n", ##a)
#endif
