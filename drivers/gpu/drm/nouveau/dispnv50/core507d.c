/*
 * Copyright 2018 Red Hat Inc.
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
#include "core.h"
#include "head.h"

#include <nvif/if0014.h>
#include <nvif/push507c.h>
#include <nvif/timer.h>

#include <nvhw/class/cl507d.h>

#include "nouveau_bo.h"

int
core507d_update(struct nv50_core *core, u32 *interlock, bool ntfy)
{
	struct nvif_push *push = core->chan.push;
	int ret;

	if ((ret = PUSH_WAIT(push, (ntfy ? 2 : 0) + 3)))
		return ret;

	if (ntfy) {
		PUSH_MTHD(push, NV507D, SET_NOTIFIER_CONTROL,
			  NVDEF(NV507D, SET_NOTIFIER_CONTROL, MODE, WRITE) |
			  NVVAL(NV507D, SET_NOTIFIER_CONTROL, OFFSET, NV50_DISP_CORE_NTFY >> 2) |
			  NVDEF(NV507D, SET_NOTIFIER_CONTROL, NOTIFY, ENABLE));
	}

	PUSH_MTHD(push, NV507D, UPDATE, interlock[NV50_DISP_INTERLOCK_BASE] |
					interlock[NV50_DISP_INTERLOCK_OVLY] |
		  NVDEF(NV507D, UPDATE, NOT_DRIVER_FRIENDLY, FALSE) |
		  NVDEF(NV507D, UPDATE, NOT_DRIVER_UNFRIENDLY, FALSE) |
		  NVDEF(NV507D, UPDATE, INHIBIT_INTERRUPTS, FALSE),

				SET_NOTIFIER_CONTROL,
		  NVDEF(NV507D, SET_NOTIFIER_CONTROL, NOTIFY, DISABLE));

	return PUSH_KICK(push);
}

int
core507d_ntfy_wait_done(struct nouveau_bo *bo, u32 offset,
			struct nvif_device *device)
{
	s64 time = nvif_msec(device, 2000ULL,
		if (NVBO_TD32(bo, offset, NV_DISP_CORE_NOTIFIER_1, COMPLETION_0, DONE, ==, TRUE))
			break;
		usleep_range(1, 2);
	);
	return time < 0 ? time : 0;
}

void
core507d_ntfy_init(struct nouveau_bo *bo, u32 offset)
{
	NVBO_WR32(bo, offset, NV_DISP_CORE_NOTIFIER_1, COMPLETION_0,
			NVDEF(NV_DISP_CORE_NOTIFIER_1, COMPLETION_0, DONE, FALSE));
}

int
core507d_read_caps(struct nv50_disp *disp)
{
	struct nvif_push *push = disp->core->chan.push;
	int ret;

	ret = PUSH_WAIT(push, 6);
	if (ret)
		return ret;

	PUSH_MTHD(push, NV507D, SET_NOTIFIER_CONTROL,
		  NVDEF(NV507D, SET_NOTIFIER_CONTROL, MODE, WRITE) |
		  NVVAL(NV507D, SET_NOTIFIER_CONTROL, OFFSET, NV50_DISP_CORE_NTFY >> 2) |
		  NVDEF(NV507D, SET_NOTIFIER_CONTROL, NOTIFY, ENABLE));

	PUSH_MTHD(push, NV507D, GET_CAPABILITIES, 0x00000000);

	PUSH_MTHD(push, NV507D, SET_NOTIFIER_CONTROL,
		  NVDEF(NV507D, SET_NOTIFIER_CONTROL, NOTIFY, DISABLE));

	return PUSH_KICK(push);
}

int
core507d_caps_init(struct nouveau_drm *drm, struct nv50_disp *disp)
{
	struct nv50_core *core = disp->core;
	struct nouveau_bo *bo = disp->sync;
	s64 time;
	int ret;

	NVBO_WR32(bo, NV50_DISP_CORE_NTFY, NV_DISP_CORE_NOTIFIER_1, CAPABILITIES_1,
				     NVDEF(NV_DISP_CORE_NOTIFIER_1, CAPABILITIES_1, DONE, FALSE));

	ret = core507d_read_caps(disp);
	if (ret < 0)
		return ret;

	time = nvif_msec(core->chan.base.device, 2000ULL,
			 if (NVBO_TD32(bo, NV50_DISP_CORE_NTFY,
				       NV_DISP_CORE_NOTIFIER_1, CAPABILITIES_1, DONE, ==, TRUE))
				 break;
			 usleep_range(1, 2);
			 );
	if (time < 0)
		NV_ERROR(drm, "core caps notifier timeout\n");

	return 0;
}

int
core507d_init(struct nv50_core *core)
{
	struct nvif_push *push = core->chan.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV507D, SET_CONTEXT_DMA_NOTIFIER, core->chan.sync.handle);
	return PUSH_KICK(push);
}

static const struct nv50_core_func
core507d = {
	.init = core507d_init,
	.ntfy_init = core507d_ntfy_init,
	.caps_init = core507d_caps_init,
	.ntfy_wait_done = core507d_ntfy_wait_done,
	.update = core507d_update,
	.head = &head507d,
	.dac = &dac507d,
	.sor = &sor507d,
	.pior = &pior507d,
};

int
core507d_new_(const struct nv50_core_func *func, struct nouveau_drm *drm,
	      s32 oclass, struct nv50_core **pcore)
{
	struct nvif_disp_chan_v0 args = {};
	struct nv50_disp *disp = nv50_disp(drm->dev);
	struct nv50_core *core;
	int ret;

	if (!(core = *pcore = kzalloc(sizeof(*core), GFP_KERNEL)))
		return -ENOMEM;
	core->func = func;

	ret = nv50_dmac_create(&drm->client.device, &disp->disp->object,
			       &oclass, 0, &args, sizeof(args),
			       disp->sync->offset, &core->chan);
	if (ret) {
		NV_ERROR(drm, "core%04x allocation failed: %d\n", oclass, ret);
		return ret;
	}

	return 0;
}

int
core507d_new(struct nouveau_drm *drm, s32 oclass, struct nv50_core **pcore)
{
	return core507d_new_(&core507d, drm, oclass, pcore);
}
