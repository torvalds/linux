/*
 * Copyright 2013 Red Hat Inc.
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
#include "conn.h"
#include "head.h"
#include "ior.h"
#include "outp.h"

#include <core/client.h>
#include <core/ramht.h>

#include <nvif/class.h>
#include <nvif/cl0046.h>
#include <nvif/event.h>
#include <nvif/unpack.h>

static void
nvkm_disp_vblank_fini(struct nvkm_event *event, int type, int id)
{
	struct nvkm_disp *disp = container_of(event, typeof(*disp), vblank);
	struct nvkm_head *head = nvkm_head_find(disp, id);
	if (head)
		head->func->vblank_put(head);
}

static void
nvkm_disp_vblank_init(struct nvkm_event *event, int type, int id)
{
	struct nvkm_disp *disp = container_of(event, typeof(*disp), vblank);
	struct nvkm_head *head = nvkm_head_find(disp, id);
	if (head)
		head->func->vblank_get(head);
}

static const struct nvkm_event_func
nvkm_disp_vblank_func = {
	.init = nvkm_disp_vblank_init,
	.fini = nvkm_disp_vblank_fini,
};

void
nvkm_disp_vblank(struct nvkm_disp *disp, int head)
{
	nvkm_event_ntfy(&disp->vblank, head, NVKM_DISP_HEAD_EVENT_VBLANK);
}

static int
nvkm_disp_class_new(struct nvkm_device *device,
		    const struct nvkm_oclass *oclass, void *data, u32 size,
		    struct nvkm_object **pobject)
{
	return nvkm_udisp_new(oclass, data, size, pobject);
}

static const struct nvkm_device_oclass
nvkm_disp_sclass = {
	.ctor = nvkm_disp_class_new,
};

static int
nvkm_disp_class_get(struct nvkm_oclass *oclass, int index,
		    const struct nvkm_device_oclass **class)
{
	struct nvkm_disp *disp = nvkm_disp(oclass->engine);
	if (index == 0) {
		oclass->base = disp->func->root;
		*class = &nvkm_disp_sclass;
		return 0;
	}
	return 1;
}

static void
nvkm_disp_intr(struct nvkm_engine *engine)
{
	struct nvkm_disp *disp = nvkm_disp(engine);
	disp->func->intr(disp);
}

static int
nvkm_disp_fini(struct nvkm_engine *engine, bool suspend)
{
	struct nvkm_disp *disp = nvkm_disp(engine);
	struct nvkm_outp *outp;

	if (disp->func->fini)
		disp->func->fini(disp);

	list_for_each_entry(outp, &disp->outps, head) {
		if (outp->func->fini)
			outp->func->fini(outp);
	}

	return 0;
}

static int
nvkm_disp_init(struct nvkm_engine *engine)
{
	struct nvkm_disp *disp = nvkm_disp(engine);
	struct nvkm_outp *outp;
	struct nvkm_ior *ior;

	list_for_each_entry(outp, &disp->outps, head) {
		if (outp->func->init)
			outp->func->init(outp);
	}

	if (disp->func->init) {
		int ret = disp->func->init(disp);
		if (ret)
			return ret;
	}

	/* Set 'normal' (ie. when it's attached to a head) state for
	 * each output resource to 'fully enabled'.
	 */
	list_for_each_entry(ior, &disp->iors, head) {
		ior->func->power(ior, true, true, true, true, true);
	}

	return 0;
}

static int
nvkm_disp_oneinit(struct nvkm_engine *engine)
{
	struct nvkm_disp *disp = nvkm_disp(engine);
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_head *head;
	int ret, i;

	if (disp->func->oneinit) {
		ret = disp->func->oneinit(disp);
		if (ret)
			return ret;
	}

	i = 0;
	list_for_each_entry(head, &disp->heads, head)
		i = max(i, head->id + 1);

	return nvkm_event_init(&nvkm_disp_vblank_func, subdev, 1, i, &disp->vblank);
}

static void *
nvkm_disp_dtor(struct nvkm_engine *engine)
{
	struct nvkm_disp *disp = nvkm_disp(engine);
	struct nvkm_conn *conn;
	struct nvkm_outp *outp;
	struct nvkm_ior *ior;
	struct nvkm_head *head;
	void *data = disp;

	nvkm_ramht_del(&disp->ramht);
	nvkm_gpuobj_del(&disp->inst);

	nvkm_event_fini(&disp->uevent);

	if (disp->super.wq) {
		destroy_workqueue(disp->super.wq);
		mutex_destroy(&disp->super.mutex);
	}

	nvkm_event_fini(&disp->vblank);

	while (!list_empty(&disp->conns)) {
		conn = list_first_entry(&disp->conns, typeof(*conn), head);
		list_del(&conn->head);
		nvkm_conn_del(&conn);
	}

	while (!list_empty(&disp->outps)) {
		outp = list_first_entry(&disp->outps, typeof(*outp), head);
		list_del(&outp->head);
		nvkm_outp_del(&outp);
	}

	while (!list_empty(&disp->iors)) {
		ior = list_first_entry(&disp->iors, typeof(*ior), head);
		nvkm_ior_del(&ior);
	}

	while (!list_empty(&disp->heads)) {
		head = list_first_entry(&disp->heads, typeof(*head), head);
		nvkm_head_del(&head);
	}

	return data;
}

static const struct nvkm_engine_func
nvkm_disp = {
	.dtor = nvkm_disp_dtor,
	.oneinit = nvkm_disp_oneinit,
	.init = nvkm_disp_init,
	.fini = nvkm_disp_fini,
	.intr = nvkm_disp_intr,
	.base.sclass = nvkm_disp_class_get,
};

int
nvkm_disp_new_(const struct nvkm_disp_func *func, struct nvkm_device *device,
	       enum nvkm_subdev_type type, int inst, struct nvkm_disp **pdisp)
{
	struct nvkm_disp *disp;
	int ret;

	if (!(disp = *pdisp = kzalloc(sizeof(**pdisp), GFP_KERNEL)))
		return -ENOMEM;

	disp->func = func;
	INIT_LIST_HEAD(&disp->heads);
	INIT_LIST_HEAD(&disp->iors);
	INIT_LIST_HEAD(&disp->outps);
	INIT_LIST_HEAD(&disp->conns);
	spin_lock_init(&disp->client.lock);

	ret = nvkm_engine_ctor(&nvkm_disp, device, type, inst, true, &disp->engine);
	if (ret)
		return ret;

	if (func->super) {
		disp->super.wq = create_singlethread_workqueue("nvkm-disp");
		if (!disp->super.wq)
			return -ENOMEM;

		INIT_WORK(&disp->super.work, func->super);
		mutex_init(&disp->super.mutex);
	}

	return nvkm_event_init(func->uevent, &disp->engine.subdev, 1, ARRAY_SIZE(disp->chan),
			       &disp->uevent);
}
