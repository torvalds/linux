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
#include "runl.h"
#include "chan.h"
#include "chid.h"
#include "priv.h"

void
nvkm_runl_del(struct nvkm_runl *runl)
{
	struct nvkm_engn *engn, *engt;

	list_for_each_entry_safe(engn, engt, &runl->engns, head) {
		list_del(&engn->head);
		kfree(engn);
	}

	nvkm_chid_unref(&runl->chid);
	nvkm_chid_unref(&runl->cgid);

	list_del(&runl->head);
	kfree(runl);
}

struct nvkm_engn *
nvkm_runl_add(struct nvkm_runl *runl, int engi, const struct nvkm_engn_func *func,
	      enum nvkm_subdev_type type, int inst)
{
	struct nvkm_device *device = runl->fifo->engine.subdev.device;
	struct nvkm_engine *engine;
	struct nvkm_engn *engn;

	engine = nvkm_device_engine(device, type, inst);
	if (!engine) {
		RUNL_DEBUG(runl, "engn %d.%d[%s] not found", engi, inst, nvkm_subdev_type[type]);
		return NULL;
	}

	if (!(engn = kzalloc(sizeof(*engn), GFP_KERNEL)))
		return NULL;

	engn->func = func;
	engn->runl = runl;
	engn->id = engi;
	engn->engine = engine;
	list_add_tail(&engn->head, &runl->engns);
	return engn;
}

struct nvkm_runl *
nvkm_runl_get(struct nvkm_fifo *fifo, int runi, u32 addr)
{
	struct nvkm_runl *runl;

	nvkm_runl_foreach(runl, fifo) {
		if ((runi >= 0 && runl->id == runi) || (runi < 0 && runl->addr == addr))
			return runl;
	}

	return NULL;
}

struct nvkm_runl *
nvkm_runl_new(struct nvkm_fifo *fifo, int runi, u32 addr, int id_nr)
{
	struct nvkm_subdev *subdev = &fifo->engine.subdev;
	struct nvkm_runl *runl;
	int ret;

	if (!(runl = kzalloc(sizeof(*runl), GFP_KERNEL)))
		return NULL;

	runl->func = fifo->func->runl;
	runl->fifo = fifo;
	runl->id = runi;
	runl->addr = addr;
	INIT_LIST_HEAD(&runl->engns);
	list_add_tail(&runl->head, &fifo->runls);

	if (!fifo->chid) {
		if ((ret = nvkm_chid_new(&nvkm_chan_event, subdev, id_nr, 0, id_nr, &runl->cgid)) ||
		    (ret = nvkm_chid_new(&nvkm_chan_event, subdev, id_nr, 0, id_nr, &runl->chid))) {
			RUNL_ERROR(runl, "cgid/chid: %d", ret);
			nvkm_runl_del(runl);
			return NULL;
		}
	} else {
		runl->cgid = nvkm_chid_ref(fifo->cgid);
		runl->chid = nvkm_chid_ref(fifo->chid);
	}

	return runl;
}
