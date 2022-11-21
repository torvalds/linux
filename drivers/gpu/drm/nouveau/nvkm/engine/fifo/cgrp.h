/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_CGRP_H__
#define __NVKM_CGRP_H__
#include <core/os.h>
struct nvkm_chan;
struct nvkm_client;

struct nvkm_vctx {
	struct nvkm_ectx *ectx;
	struct nvkm_vmm *vmm;
	refcount_t refs;

	struct nvkm_gpuobj *inst;
	struct nvkm_vma *vma;

	struct list_head head;
};

struct nvkm_ectx {
	struct nvkm_engn *engn;
	refcount_t refs;
	refcount_t uses;

	struct nvkm_object *object;

	struct list_head head;
};

struct nvkm_cgrp {
	const struct nvkm_cgrp_func {
		void (*preempt)(struct nvkm_cgrp *);
	} *func;
	char name[64];
	struct nvkm_runl *runl;
	struct nvkm_vmm *vmm;
	bool hw;
	int id;
	struct kref kref;

	struct list_head chans;
	int chan_nr;

	spinlock_t lock; /* protects irq handler channel (group) lookup */

	struct list_head ectxs;
	struct list_head vctxs;
	struct mutex mutex;

#define NVKM_CGRP_RC_NONE    0
#define NVKM_CGRP_RC_PENDING 1
#define NVKM_CGRP_RC_RUNNING 2
	atomic_t rc;

	struct list_head head;
};

int nvkm_cgrp_new(struct nvkm_runl *, const char *name, struct nvkm_vmm *, bool hw,
		  struct nvkm_cgrp **);
struct nvkm_cgrp *nvkm_cgrp_ref(struct nvkm_cgrp *);
void nvkm_cgrp_unref(struct nvkm_cgrp **);
int nvkm_cgrp_vctx_get(struct nvkm_cgrp *, struct nvkm_engn *, struct nvkm_chan *,
		       struct nvkm_vctx **, struct nvkm_client *);
void nvkm_cgrp_vctx_put(struct nvkm_cgrp *, struct nvkm_vctx **);

void nvkm_cgrp_put(struct nvkm_cgrp **, unsigned long irqflags);

#define nvkm_cgrp_foreach_chan(chan,cgrp) list_for_each_entry((chan), &(cgrp)->chans, head)
#define nvkm_cgrp_foreach_chan_safe(chan,ctmp,cgrp) \
	list_for_each_entry_safe((chan), (ctmp), &(cgrp)->chans, head)

#define CGRP_PRCLI(c,l,p,f,a...) RUNL_PRINT((c)->runl, l, p, "%04x:[%s]"f, (c)->id, (c)->name, ##a)
#define CGRP_PRINT(c,l,p,f,a...) RUNL_PRINT((c)->runl, l, p, "%04x:"f, (c)->id, ##a)
#define CGRP_ERROR(c,f,a...) CGRP_PRCLI((c), ERROR,    err, " "f"\n", ##a)
#define CGRP_TRACE(c,f,a...) CGRP_PRINT((c), TRACE,   info, " "f"\n", ##a)
#endif
