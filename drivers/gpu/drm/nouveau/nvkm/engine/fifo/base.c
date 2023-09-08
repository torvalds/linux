/*
 * Copyright 2012 Red Hat Inc.
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
 *
 * Authors: Ben Skeggs
 */
#include "priv.h"
#include "chan.h"
#include "chid.h"
#include "runl.h"
#include "runq.h"

#include <core/gpuobj.h>
#include <subdev/bar.h>
#include <subdev/mc.h>
#include <subdev/mmu.h>

#include <nvif/cl0080.h>
#include <nvif/unpack.h>

bool
nvkm_fifo_ctxsw_in_progress(struct nvkm_engine *engine)
{
	struct nvkm_runl *runl;
	struct nvkm_engn *engn;

	nvkm_runl_foreach(runl, engine->subdev.device->fifo) {
		nvkm_runl_foreach_engn(engn, runl) {
			if (engn->engine == engine)
				return engn->func->chsw ? engn->func->chsw(engn) : false;
		}
	}

	return false;
}

void
nvkm_fifo_pause(struct nvkm_fifo *fifo, unsigned long *flags)
{
	return fifo->func->pause(fifo, flags);
}

void
nvkm_fifo_start(struct nvkm_fifo *fifo, unsigned long *flags)
{
	return fifo->func->start(fifo, flags);
}

void
nvkm_fifo_fault(struct nvkm_fifo *fifo, struct nvkm_fault_data *info)
{
	return fifo->func->mmu_fault->recover(fifo, info);
}

static int
nvkm_fifo_class_new(struct nvkm_device *device, const struct nvkm_oclass *oclass,
		    void *argv, u32 argc, struct nvkm_object **pobject)
{
	struct nvkm_fifo *fifo = nvkm_fifo(oclass->engine);

	if (oclass->engn == &fifo->func->cgrp.user)
		return nvkm_ucgrp_new(fifo, oclass, argv, argc, pobject);

	if (oclass->engn == &fifo->func->chan.user)
		return nvkm_uchan_new(fifo, NULL, oclass, argv, argc, pobject);

	WARN_ON(1);
	return -ENOSYS;
}

static const struct nvkm_device_oclass
nvkm_fifo_class = {
	.ctor = nvkm_fifo_class_new,
};

static int
nvkm_fifo_class_get(struct nvkm_oclass *oclass, int index, const struct nvkm_device_oclass **class)
{
	struct nvkm_fifo *fifo = nvkm_fifo(oclass->engine);
	const struct nvkm_fifo_func_cgrp *cgrp = &fifo->func->cgrp;
	const struct nvkm_fifo_func_chan *chan = &fifo->func->chan;
	int c = 0;

	/* *_CHANNEL_GROUP_* */
	if (cgrp->user.oclass) {
		if (c++ == index) {
			oclass->base = cgrp->user;
			oclass->engn = &fifo->func->cgrp.user;
			*class = &nvkm_fifo_class;
			return 0;
		}
	}

	/* *_CHANNEL_DMA, *_CHANNEL_GPFIFO_* */
	if (chan->user.oclass) {
		if (c++ == index) {
			oclass->base = chan->user;
			oclass->engn = &fifo->func->chan.user;
			*class = &nvkm_fifo_class;
			return 0;
		}
	}

	return c;
}

static int
nvkm_fifo_fini(struct nvkm_engine *engine, bool suspend)
{
	struct nvkm_fifo *fifo = nvkm_fifo(engine);
	struct nvkm_runl *runl;

	nvkm_inth_block(&fifo->engine.subdev.inth);

	nvkm_runl_foreach(runl, fifo)
		nvkm_runl_fini(runl);

	return 0;
}

static int
nvkm_fifo_init(struct nvkm_engine *engine)
{
	struct nvkm_fifo *fifo = nvkm_fifo(engine);
	struct nvkm_runq *runq;
	struct nvkm_runl *runl;
	u32 mask = 0;

	if (fifo->func->init_pbdmas) {
		nvkm_runq_foreach(runq, fifo)
			mask |= BIT(runq->id);

		fifo->func->init_pbdmas(fifo, mask);

		nvkm_runq_foreach(runq, fifo)
			runq->func->init(runq);
	}

	nvkm_runl_foreach(runl, fifo) {
		if (runl->func->init)
			runl->func->init(runl);
	}

	if (fifo->func->init)
		fifo->func->init(fifo);

	nvkm_inth_allow(&fifo->engine.subdev.inth);
	return 0;
}

static int
nvkm_fifo_info(struct nvkm_engine *engine, u64 mthd, u64 *data)
{
	struct nvkm_fifo *fifo = nvkm_fifo(engine);
	struct nvkm_runl *runl;
	struct nvkm_engn *engn;
	int ret;

	ret = nvkm_subdev_oneinit(&fifo->engine.subdev);
	if (ret)
		return ret;

	switch (mthd) {
	case NV_DEVICE_HOST_CHANNELS: *data = fifo->chid ? fifo->chid->nr : 0; return 0;
	case NV_DEVICE_HOST_RUNLISTS:
		*data = 0;
		nvkm_runl_foreach(runl, fifo)
			*data |= BIT(runl->id);
		return 0;
	case NV_DEVICE_HOST_RUNLIST_ENGINES:
		runl = nvkm_runl_get(fifo, *data, 0);
		if (runl) {
			*data = 0;
			nvkm_runl_foreach_engn(engn, runl) {
#define CASE(n) case NVKM_ENGINE_##n: *data |= NV_DEVICE_HOST_RUNLIST_ENGINES_##n; break
				switch (engn->engine->subdev.type) {
				case NVKM_ENGINE_DMAOBJ:
					break;
				CASE(SW    );
				CASE(GR    );
				CASE(MPEG  );
				CASE(ME    );
				CASE(CIPHER);
				CASE(BSP   );
				CASE(VP    );
				CASE(CE    );
				CASE(SEC   );
				CASE(MSVLD );
				CASE(MSPDEC);
				CASE(MSPPP );
				CASE(MSENC );
				CASE(VIC   );
				CASE(SEC2  );
				CASE(NVDEC );
				CASE(NVENC );
				default:
					WARN_ON(1);
					break;
				}
#undef CASE
			}
			return 0;
		}
		return -EINVAL;
	case NV_DEVICE_HOST_RUNLIST_CHANNELS:
		if (!fifo->chid) {
			runl = nvkm_runl_get(fifo, *data, 0);
			if (runl) {
				*data = runl->chid->nr;
				return 0;
			}
		}
		return -EINVAL;
	default:
		break;
	}

	return -ENOSYS;
}

static int
nvkm_fifo_oneinit(struct nvkm_engine *engine)
{
	struct nvkm_subdev *subdev = &engine->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_fifo *fifo = nvkm_fifo(engine);
	struct nvkm_runl *runl;
	struct nvkm_engn *engn;
	int ret, nr, i;

	/* Initialise CHID/CGID allocator(s) on GPUs where they aren't per-runlist. */
	if (fifo->func->chid_nr) {
		ret = fifo->func->chid_ctor(fifo, fifo->func->chid_nr(fifo));
		if (ret)
			return ret;
	}

	/* Create runqueues for each PBDMA. */
	if (fifo->func->runq_nr) {
		for (nr = fifo->func->runq_nr(fifo), i = 0; i < nr; i++) {
			if (!nvkm_runq_new(fifo, i))
				return -ENOMEM;
		}
	}

	/* Create runlists. */
	ret = fifo->func->runl_ctor(fifo);
	if (ret)
		return ret;

	nvkm_runl_foreach(runl, fifo) {
		RUNL_DEBUG(runl, "chan:%06x", runl->chan);
		nvkm_runl_foreach_engn(engn, runl) {
			ENGN_DEBUG(engn, "");
		}
	}

	/* Register interrupt handler. */
	if (fifo->func->intr) {
		ret = nvkm_inth_add(&device->mc->intr, NVKM_INTR_SUBDEV, NVKM_INTR_PRIO_NORMAL,
				    subdev, fifo->func->intr, &subdev->inth);
		if (ret) {
			nvkm_error(subdev, "intr %d\n", ret);
			return ret;
		}
	}

	/* Initialise non-stall intr handling. */
	if (fifo->func->nonstall_ctor) {
		ret = fifo->func->nonstall_ctor(fifo);
		if (ret) {
			nvkm_error(subdev, "nonstall %d\n", ret);
		}
	}

	/* Allocate USERD + BAR1 polling area. */
	if (fifo->func->chan.func->userd->bar == 1) {
		struct nvkm_vmm *bar1 = nvkm_bar_bar1_vmm(device);

		ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, fifo->chid->nr *
				      fifo->func->chan.func->userd->size, 0, true,
				      &fifo->userd.mem);
		if (ret)
			return ret;

		ret = nvkm_vmm_get(bar1, 12, nvkm_memory_size(fifo->userd.mem), &fifo->userd.bar1);
		if (ret)
			return ret;

		ret = nvkm_memory_map(fifo->userd.mem, 0, bar1, fifo->userd.bar1, NULL, 0);
		if (ret)
			return ret;
	}

	return 0;
}

static void
nvkm_fifo_preinit(struct nvkm_engine *engine)
{
	nvkm_mc_reset(engine->subdev.device, NVKM_ENGINE_FIFO, 0);
}

static void *
nvkm_fifo_dtor(struct nvkm_engine *engine)
{
	struct nvkm_fifo *fifo = nvkm_fifo(engine);
	struct nvkm_runl *runl, *runt;
	struct nvkm_runq *runq, *rtmp;

	if (fifo->userd.bar1)
		nvkm_vmm_put(nvkm_bar_bar1_vmm(engine->subdev.device), &fifo->userd.bar1);
	nvkm_memory_unref(&fifo->userd.mem);

	list_for_each_entry_safe(runl, runt, &fifo->runls, head)
		nvkm_runl_del(runl);
	list_for_each_entry_safe(runq, rtmp, &fifo->runqs, head)
		nvkm_runq_del(runq);

	nvkm_chid_unref(&fifo->cgid);
	nvkm_chid_unref(&fifo->chid);

	nvkm_event_fini(&fifo->nonstall.event);
	mutex_destroy(&fifo->mutex);
	return fifo;
}

static const struct nvkm_engine_func
nvkm_fifo = {
	.dtor = nvkm_fifo_dtor,
	.preinit = nvkm_fifo_preinit,
	.oneinit = nvkm_fifo_oneinit,
	.info = nvkm_fifo_info,
	.init = nvkm_fifo_init,
	.fini = nvkm_fifo_fini,
	.base.sclass = nvkm_fifo_class_get,
};

int
nvkm_fifo_new_(const struct nvkm_fifo_func *func, struct nvkm_device *device,
	       enum nvkm_subdev_type type, int inst, struct nvkm_fifo **pfifo)
{
	struct nvkm_fifo *fifo;
	int ret;

	if (!(fifo = *pfifo = kzalloc(sizeof(*fifo), GFP_KERNEL)))
		return -ENOMEM;

	fifo->func = func;
	INIT_LIST_HEAD(&fifo->runqs);
	INIT_LIST_HEAD(&fifo->runls);
	/*TODO: Needs to be >CTXSW_TIMEOUT, so RC can recover before this is hit.
	 *      CTXSW_TIMEOUT HW default seems to differ between GPUs, so just a
	 *      large number for now until we support changing it.
	 */
	fifo->timeout.chan_msec = 10000;
	spin_lock_init(&fifo->lock);
	mutex_init(&fifo->mutex);

	ret = nvkm_engine_ctor(&nvkm_fifo, device, type, inst, true, &fifo->engine);
	if (ret)
		return ret;

	if (func->nonstall) {
		ret = nvkm_event_init(func->nonstall, &fifo->engine.subdev, 1, 1,
				      &fifo->nonstall.event);
		if (ret)
			return ret;
	}

	return 0;
}
