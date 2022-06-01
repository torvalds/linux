/*
 * Copyright 2021 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "cgrp.h"
#include "chan.h"
#include "chid.h"
#include "runl.h"
#include "priv.h"

#include <subdev/mmu.h>

static void
nvkm_cgrp_del(struct kref *kref)
{
	struct nvkm_cgrp *cgrp = container_of(kref, typeof(*cgrp), kref);
	struct nvkm_runl *runl = cgrp->runl;

	if (runl->cgid)
		nvkm_chid_put(runl->cgid, cgrp->id, &cgrp->lock);

	nvkm_vmm_unref(&cgrp->vmm);
	kfree(cgrp);
}

void
nvkm_cgrp_unref(struct nvkm_cgrp **pcgrp)
{
	struct nvkm_cgrp *cgrp = *pcgrp;

	if (!cgrp)
		return;

	kref_put(&cgrp->kref, nvkm_cgrp_del);
	*pcgrp = NULL;
}

struct nvkm_cgrp *
nvkm_cgrp_ref(struct nvkm_cgrp *cgrp)
{
	if (cgrp)
		kref_get(&cgrp->kref);

	return cgrp;
}

int
nvkm_cgrp_new(struct nvkm_runl *runl, const char *name, struct nvkm_vmm *vmm, bool hw,
	      struct nvkm_cgrp **pcgrp)
{
	struct nvkm_cgrp *cgrp;

	if (!(cgrp = *pcgrp = kmalloc(sizeof(*cgrp), GFP_KERNEL)))
		return -ENOMEM;

	cgrp->func = runl->fifo->func->cgrp.func;
	strscpy(cgrp->name, name, sizeof(cgrp->name));
	cgrp->runl = runl;
	cgrp->vmm = nvkm_vmm_ref(vmm);
	cgrp->hw = hw;
	cgrp->id = -1;
	kref_init(&cgrp->kref);
	cgrp->chans = NULL;
	cgrp->chan_nr = 0;
	spin_lock_init(&cgrp->lock);

	if (runl->cgid) {
		cgrp->id = nvkm_chid_get(runl->cgid, cgrp);
		if (cgrp->id < 0) {
			RUNL_ERROR(runl, "!cgids");
			nvkm_cgrp_unref(pcgrp);
			return -ENOSPC;
		}
	}

	return 0;
}
