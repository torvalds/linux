/*
 * Copyright 2023 Red Hat Inc.
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
#include <rm/rm.h>

#include "nvrm/device.h"
#include "nvrm/event.h"

static void
r535_gsp_event_dtor(struct nvkm_gsp_event *event)
{
	struct nvkm_gsp_device *device = event->device;
	struct nvkm_gsp_client *client = device->object.client;
	struct nvkm_gsp *gsp = client->gsp;

	mutex_lock(&gsp->client_id.mutex);
	if (event->func) {
		list_del(&event->head);
		event->func = NULL;
	}
	mutex_unlock(&gsp->client_id.mutex);

	nvkm_gsp_rm_free(&event->object);
	event->device = NULL;
}

static int
r535_gsp_device_event_get(struct nvkm_gsp_event *event)
{
	struct nvkm_gsp_device *device = event->device;
	NV2080_CTRL_EVENT_SET_NOTIFICATION_PARAMS *ctrl;

	ctrl = nvkm_gsp_rm_ctrl_get(&device->subdevice,
				    NV2080_CTRL_CMD_EVENT_SET_NOTIFICATION, sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	ctrl->event = event->id;
	ctrl->action = NV2080_CTRL_EVENT_SET_NOTIFICATION_ACTION_REPEAT;
	return nvkm_gsp_rm_ctrl_wr(&device->subdevice, ctrl);
}

static int
r535_gsp_device_event_ctor(struct nvkm_gsp_device *device, u32 handle, u32 id,
			   nvkm_gsp_event_func func, struct nvkm_gsp_event *event)
{
	struct nvkm_gsp_client *client = device->object.client;
	struct nvkm_gsp *gsp = client->gsp;
	NV0005_ALLOC_PARAMETERS *args;
	int ret;

	args = nvkm_gsp_rm_alloc_get(&device->subdevice, handle,
				     NV01_EVENT_KERNEL_CALLBACK_EX, sizeof(*args),
				     &event->object);
	if (IS_ERR(args))
		return PTR_ERR(args);

	args->hParentClient = client->object.handle;
	args->hSrcResource = 0;
	args->hClass = NV01_EVENT_KERNEL_CALLBACK_EX;
	args->notifyIndex = NV01_EVENT_CLIENT_RM | id;
	args->data = NULL;

	ret = nvkm_gsp_rm_alloc_wr(&event->object, args);
	if (ret)
		return ret;

	event->device = device;
	event->id = id;

	ret = r535_gsp_device_event_get(event);
	if (ret) {
		nvkm_gsp_event_dtor(event);
		return ret;
	}

	mutex_lock(&gsp->client_id.mutex);
	event->func = func;
	list_add(&event->head, &client->events);
	mutex_unlock(&gsp->client_id.mutex);
	return 0;
}

static void
r535_gsp_device_dtor(struct nvkm_gsp_device *device)
{
	nvkm_gsp_rm_free(&device->subdevice);
	nvkm_gsp_rm_free(&device->object);
}

static int
r535_gsp_subdevice_ctor(struct nvkm_gsp_device *device)
{
	NV2080_ALLOC_PARAMETERS *args;

	return nvkm_gsp_rm_alloc(&device->object, NVKM_RM_SUBDEVICE, NV20_SUBDEVICE_0,
				 sizeof(*args), &device->subdevice);
}

static int
r535_gsp_device_ctor(struct nvkm_gsp_client *client, struct nvkm_gsp_device *device)
{
	NV0080_ALLOC_PARAMETERS *args;
	int ret;

	args = nvkm_gsp_rm_alloc_get(&client->object, NVKM_RM_DEVICE, NV01_DEVICE_0, sizeof(*args),
				     &device->object);
	if (IS_ERR(args))
		return PTR_ERR(args);

	args->hClientShare = client->object.handle;

	ret = nvkm_gsp_rm_alloc_wr(&device->object, args);
	if (ret)
		return ret;

	ret = r535_gsp_subdevice_ctor(device);
	if (ret)
		nvkm_gsp_rm_free(&device->object);

	return ret;
}

const struct nvkm_rm_api_device
r535_device = {
	.ctor = r535_gsp_device_ctor,
	.dtor = r535_gsp_device_dtor,
	.event.ctor = r535_gsp_device_event_ctor,
	.event.dtor = r535_gsp_event_dtor,
};
