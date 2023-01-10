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
#define nvkm_ucgrp(p) container_of((p), struct nvkm_ucgrp, object)
#include "priv.h"
#include "cgrp.h"
#include "runl.h"

#include <subdev/mmu.h>

#include <nvif/if0021.h>

struct nvkm_ucgrp {
	struct nvkm_object object;
	struct nvkm_cgrp *cgrp;
};

static int
nvkm_ucgrp_chan_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		    struct nvkm_object **pobject)
{
	struct nvkm_cgrp *cgrp = nvkm_ucgrp(oclass->parent)->cgrp;

	return nvkm_uchan_new(cgrp->runl->fifo, cgrp, oclass, argv, argc, pobject);
}

static int
nvkm_ucgrp_sclass(struct nvkm_object *object, int index, struct nvkm_oclass *oclass)
{
	struct nvkm_cgrp *cgrp = nvkm_ucgrp(object)->cgrp;
	struct nvkm_fifo *fifo = cgrp->runl->fifo;
	const struct nvkm_fifo_func_chan *chan = &fifo->func->chan;
	int c = 0;

	/* *_CHANNEL_GPFIFO_* */
	if (chan->user.oclass) {
		if (c++ == index) {
			oclass->base = chan->user;
			oclass->ctor = nvkm_ucgrp_chan_new;
			return 0;
		}
	}

	return -EINVAL;
}

static void *
nvkm_ucgrp_dtor(struct nvkm_object *object)
{
	struct nvkm_ucgrp *ucgrp = nvkm_ucgrp(object);

	nvkm_cgrp_unref(&ucgrp->cgrp);
	return ucgrp;
}

static const struct nvkm_object_func
nvkm_ucgrp = {
	.dtor = nvkm_ucgrp_dtor,
	.sclass = nvkm_ucgrp_sclass,
};

int
nvkm_ucgrp_new(struct nvkm_fifo *fifo, const struct nvkm_oclass *oclass, void *argv, u32 argc,
	       struct nvkm_object **pobject)
{
	union nvif_cgrp_args *args = argv;
	struct nvkm_runl *runl;
	struct nvkm_vmm *vmm;
	struct nvkm_ucgrp *ucgrp;
	int ret;

	if (argc < sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	argc -= sizeof(args->v0);

	if (args->v0.namelen != argc)
		return -EINVAL;

	/* Lookup objects referenced in args. */
	runl = nvkm_runl_get(fifo, args->v0.runlist, 0);
	if (!runl)
		return -EINVAL;

	vmm = nvkm_uvmm_search(oclass->client, args->v0.vmm);
	if (IS_ERR(vmm))
		return PTR_ERR(vmm);

	/* Allocate channel group. */
	if (!(ucgrp = kzalloc(sizeof(*ucgrp), GFP_KERNEL))) {
		ret = -ENOMEM;
		goto done;
	}

	nvkm_object_ctor(&nvkm_ucgrp, oclass, &ucgrp->object);
	*pobject = &ucgrp->object;

	ret = nvkm_cgrp_new(runl, args->v0.name, vmm, true, &ucgrp->cgrp);
	if (ret)
		goto done;

	/* Return channel group info to caller. */
	args->v0.cgid = ucgrp->cgrp->id;

done:
	nvkm_vmm_unref(&vmm);
	return ret;
}
