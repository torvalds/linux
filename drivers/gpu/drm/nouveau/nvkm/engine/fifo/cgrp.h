/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_CGRP_H__
#define __NVKM_CGRP_H__
#include <core/os.h>
struct nvkm_chan;

struct nvkm_cgrp {
	const struct nvkm_cgrp_func {
	} *func;
	char name[64];
	struct nvkm_runl *runl;
	struct nvkm_vmm *vmm;
	bool hw;
	int id;
	struct kref kref;

	struct nvkm_chan *chans;
	int chan_nr;

	spinlock_t lock; /* protects irq handler channel (group) lookup */

	struct list_head head;
	struct list_head chan;
};

int nvkm_cgrp_new(struct nvkm_runl *, const char *name, struct nvkm_vmm *, bool hw,
		  struct nvkm_cgrp **);
struct nvkm_cgrp *nvkm_cgrp_ref(struct nvkm_cgrp *);
void nvkm_cgrp_unref(struct nvkm_cgrp **);

#define CGRP_PRCLI(c,l,p,f,a...) RUNL_PRINT((c)->runl, l, p, "%04x:[%s]"f, (c)->id, (c)->name, ##a)
#define CGRP_PRINT(c,l,p,f,a...) RUNL_PRINT((c)->runl, l, p, "%04x:"f, (c)->id, ##a)
#define CGRP_ERROR(c,f,a...) CGRP_PRCLI((c), ERROR,    err, " "f"\n", ##a)
#define CGRP_TRACE(c,f,a...) CGRP_PRINT((c), TRACE,   info, " "f"\n", ##a)
#endif
