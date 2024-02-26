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
#include "priv.h"

#include <core/pci.h>
#include <subdev/timer.h>
#include <subdev/vfn.h>
#include <engine/fifo/chan.h>
#include <engine/sec2.h>

#include <nvfw/fw.h>

#include <nvrm/nvtypes.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/class/cl0000.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/class/cl0005.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/class/cl0080.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/class/cl2080.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080event.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080gpu.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080internal.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/nvos.h>
#include <nvrm/535.113.01/common/shared/msgq/inc/msgq/msgq_priv.h>
#include <nvrm/535.113.01/common/uproc/os/common/include/libos_init_args.h>
#include <nvrm/535.113.01/nvidia/arch/nvalloc/common/inc/gsp/gsp_fw_sr_meta.h>
#include <nvrm/535.113.01/nvidia/arch/nvalloc/common/inc/gsp/gsp_fw_wpr_meta.h>
#include <nvrm/535.113.01/nvidia/arch/nvalloc/common/inc/rmRiscvUcode.h>
#include <nvrm/535.113.01/nvidia/arch/nvalloc/common/inc/rmgspseq.h>
#include <nvrm/535.113.01/nvidia/generated/g_allclasses.h>
#include <nvrm/535.113.01/nvidia/generated/g_os_nvoc.h>
#include <nvrm/535.113.01/nvidia/generated/g_rpc-structures.h>
#include <nvrm/535.113.01/nvidia/inc/kernel/gpu/gsp/gsp_fw_heap.h>
#include <nvrm/535.113.01/nvidia/inc/kernel/gpu/gsp/gsp_init_args.h>
#include <nvrm/535.113.01/nvidia/inc/kernel/gpu/gsp/gsp_static_config.h>
#include <nvrm/535.113.01/nvidia/inc/kernel/gpu/intr/engine_idx.h>
#include <nvrm/535.113.01/nvidia/kernel/inc/vgpu/rpc_global_enums.h>

#include <linux/acpi.h>

#define GSP_MSG_MIN_SIZE GSP_PAGE_SIZE
#define GSP_MSG_MAX_SIZE GSP_PAGE_MIN_SIZE * 16

struct r535_gsp_msg {
	u8 auth_tag_buffer[16];
	u8 aad_buffer[16];
	u32 checksum;
	u32 sequence;
	u32 elem_count;
	u32 pad;
	u8  data[];
};

#define GSP_MSG_HDR_SIZE offsetof(struct r535_gsp_msg, data)

static int
r535_rpc_status_to_errno(uint32_t rpc_status)
{
	switch (rpc_status) {
	case 0x55: /* NV_ERR_NOT_READY */
	case 0x66: /* NV_ERR_TIMEOUT_RETRY */
		return -EAGAIN;
	case 0x51: /* NV_ERR_NO_MEMORY */
		return -ENOMEM;
	default:
		return -EINVAL;
	}
}

static void *
r535_gsp_msgq_wait(struct nvkm_gsp *gsp, u32 repc, u32 *prepc, int *ptime)
{
	struct r535_gsp_msg *mqe;
	u32 size, rptr = *gsp->msgq.rptr;
	int used;
	u8 *msg;
	u32 len;

	size = DIV_ROUND_UP(GSP_MSG_HDR_SIZE + repc, GSP_PAGE_SIZE);
	if (WARN_ON(!size || size >= gsp->msgq.cnt))
		return ERR_PTR(-EINVAL);

	do {
		u32 wptr = *gsp->msgq.wptr;

		used = wptr + gsp->msgq.cnt - rptr;
		if (used >= gsp->msgq.cnt)
			used -= gsp->msgq.cnt;
		if (used >= size)
			break;

		usleep_range(1, 2);
	} while (--(*ptime));

	if (WARN_ON(!*ptime))
		return ERR_PTR(-ETIMEDOUT);

	mqe = (void *)((u8 *)gsp->shm.msgq.ptr + 0x1000 + rptr * 0x1000);

	if (prepc) {
		*prepc = (used * GSP_PAGE_SIZE) - sizeof(*mqe);
		return mqe->data;
	}

	msg = kvmalloc(repc, GFP_KERNEL);
	if (!msg)
		return ERR_PTR(-ENOMEM);

	len = ((gsp->msgq.cnt - rptr) * GSP_PAGE_SIZE) - sizeof(*mqe);
	len = min_t(u32, repc, len);
	memcpy(msg, mqe->data, len);

	rptr += DIV_ROUND_UP(len, GSP_PAGE_SIZE);
	if (rptr == gsp->msgq.cnt)
		rptr = 0;

	repc -= len;

	if (repc) {
		mqe = (void *)((u8 *)gsp->shm.msgq.ptr + 0x1000 + 0 * 0x1000);
		memcpy(msg + len, mqe, repc);

		rptr += DIV_ROUND_UP(repc, GSP_PAGE_SIZE);
	}

	mb();
	(*gsp->msgq.rptr) = rptr;
	return msg;
}

static void *
r535_gsp_msgq_recv(struct nvkm_gsp *gsp, u32 repc, int *ptime)
{
	return r535_gsp_msgq_wait(gsp, repc, NULL, ptime);
}

static int
r535_gsp_cmdq_push(struct nvkm_gsp *gsp, void *argv)
{
	struct r535_gsp_msg *cmd = container_of(argv, typeof(*cmd), data);
	struct r535_gsp_msg *cqe;
	u32 argc = cmd->checksum;
	u64 *ptr = (void *)cmd;
	u64 *end;
	u64 csum = 0;
	int free, time = 1000000;
	u32 wptr, size;
	u32 off = 0;

	argc = ALIGN(GSP_MSG_HDR_SIZE + argc, GSP_PAGE_SIZE);

	end = (u64 *)((char *)ptr + argc);
	cmd->pad = 0;
	cmd->checksum = 0;
	cmd->sequence = gsp->cmdq.seq++;
	cmd->elem_count = DIV_ROUND_UP(argc, 0x1000);

	while (ptr < end)
		csum ^= *ptr++;

	cmd->checksum = upper_32_bits(csum) ^ lower_32_bits(csum);

	wptr = *gsp->cmdq.wptr;
	do {
		do {
			free = *gsp->cmdq.rptr + gsp->cmdq.cnt - wptr - 1;
			if (free >= gsp->cmdq.cnt)
				free -= gsp->cmdq.cnt;
			if (free >= 1)
				break;

			usleep_range(1, 2);
		} while(--time);

		if (WARN_ON(!time)) {
			kvfree(cmd);
			return -ETIMEDOUT;
		}

		cqe = (void *)((u8 *)gsp->shm.cmdq.ptr + 0x1000 + wptr * 0x1000);
		size = min_t(u32, argc, (gsp->cmdq.cnt - wptr) * GSP_PAGE_SIZE);
		memcpy(cqe, (u8 *)cmd + off, size);

		wptr += DIV_ROUND_UP(size, 0x1000);
		if (wptr == gsp->cmdq.cnt)
			wptr = 0;

		off  += size;
		argc -= size;
	} while(argc);

	nvkm_trace(&gsp->subdev, "cmdq: wptr %d\n", wptr);
	wmb();
	(*gsp->cmdq.wptr) = wptr;
	mb();

	nvkm_falcon_wr32(&gsp->falcon, 0xc00, 0x00000000);

	kvfree(cmd);
	return 0;
}

static void *
r535_gsp_cmdq_get(struct nvkm_gsp *gsp, u32 argc)
{
	struct r535_gsp_msg *cmd;
	u32 size = GSP_MSG_HDR_SIZE + argc;

	size = ALIGN(size, GSP_MSG_MIN_SIZE);
	cmd = kvzalloc(size, GFP_KERNEL);
	if (!cmd)
		return ERR_PTR(-ENOMEM);

	cmd->checksum = argc;
	return cmd->data;
}

struct nvfw_gsp_rpc {
	u32 header_version;
	u32 signature;
	u32 length;
	u32 function;
	u32 rpc_result;
	u32 rpc_result_private;
	u32 sequence;
	union {
		u32 spare;
		u32 cpuRmGfid;
	};
	u8  data[];
};

static void
r535_gsp_msg_done(struct nvkm_gsp *gsp, struct nvfw_gsp_rpc *msg)
{
	kvfree(msg);
}

static void
r535_gsp_msg_dump(struct nvkm_gsp *gsp, struct nvfw_gsp_rpc *msg, int lvl)
{
	if (gsp->subdev.debug >= lvl) {
		nvkm_printk__(&gsp->subdev, lvl, info,
			      "msg fn:%d len:0x%x/0x%zx res:0x%x resp:0x%x\n",
			      msg->function, msg->length, msg->length - sizeof(*msg),
			      msg->rpc_result, msg->rpc_result_private);
		print_hex_dump(KERN_INFO, "msg: ", DUMP_PREFIX_OFFSET, 16, 1,
			       msg->data, msg->length - sizeof(*msg), true);
	}
}

static struct nvfw_gsp_rpc *
r535_gsp_msg_recv(struct nvkm_gsp *gsp, int fn, u32 repc)
{
	struct nvkm_subdev *subdev = &gsp->subdev;
	struct nvfw_gsp_rpc *msg;
	int time = 4000000, i;
	u32 size;

retry:
	msg = r535_gsp_msgq_wait(gsp, sizeof(*msg), &size, &time);
	if (IS_ERR_OR_NULL(msg))
		return msg;

	msg = r535_gsp_msgq_recv(gsp, msg->length, &time);
	if (IS_ERR_OR_NULL(msg))
		return msg;

	if (msg->rpc_result) {
		r535_gsp_msg_dump(gsp, msg, NV_DBG_ERROR);
		r535_gsp_msg_done(gsp, msg);
		return ERR_PTR(-EINVAL);
	}

	r535_gsp_msg_dump(gsp, msg, NV_DBG_TRACE);

	if (fn && msg->function == fn) {
		if (repc) {
			if (msg->length < sizeof(*msg) + repc) {
				nvkm_error(subdev, "msg len %d < %zd\n",
					   msg->length, sizeof(*msg) + repc);
				r535_gsp_msg_dump(gsp, msg, NV_DBG_ERROR);
				r535_gsp_msg_done(gsp, msg);
				return ERR_PTR(-EIO);
			}

			return msg;
		}

		r535_gsp_msg_done(gsp, msg);
		return NULL;
	}

	for (i = 0; i < gsp->msgq.ntfy_nr; i++) {
		struct nvkm_gsp_msgq_ntfy *ntfy = &gsp->msgq.ntfy[i];

		if (ntfy->fn == msg->function) {
			if (ntfy->func)
				ntfy->func(ntfy->priv, ntfy->fn, msg->data, msg->length - sizeof(*msg));
			break;
		}
	}

	if (i == gsp->msgq.ntfy_nr)
		r535_gsp_msg_dump(gsp, msg, NV_DBG_WARN);

	r535_gsp_msg_done(gsp, msg);
	if (fn)
		goto retry;

	if (*gsp->msgq.rptr != *gsp->msgq.wptr)
		goto retry;

	return NULL;
}

static int
r535_gsp_msg_ntfy_add(struct nvkm_gsp *gsp, u32 fn, nvkm_gsp_msg_ntfy_func func, void *priv)
{
	int ret = 0;

	mutex_lock(&gsp->msgq.mutex);
	if (WARN_ON(gsp->msgq.ntfy_nr >= ARRAY_SIZE(gsp->msgq.ntfy))) {
		ret = -ENOSPC;
	} else {
		gsp->msgq.ntfy[gsp->msgq.ntfy_nr].fn = fn;
		gsp->msgq.ntfy[gsp->msgq.ntfy_nr].func = func;
		gsp->msgq.ntfy[gsp->msgq.ntfy_nr].priv = priv;
		gsp->msgq.ntfy_nr++;
	}
	mutex_unlock(&gsp->msgq.mutex);
	return ret;
}

static int
r535_gsp_rpc_poll(struct nvkm_gsp *gsp, u32 fn)
{
	void *repv;

	mutex_lock(&gsp->cmdq.mutex);
	repv = r535_gsp_msg_recv(gsp, fn, 0);
	mutex_unlock(&gsp->cmdq.mutex);
	if (IS_ERR(repv))
		return PTR_ERR(repv);

	return 0;
}

static void *
r535_gsp_rpc_send(struct nvkm_gsp *gsp, void *argv, bool wait, u32 repc)
{
	struct nvfw_gsp_rpc *rpc = container_of(argv, typeof(*rpc), data);
	struct nvfw_gsp_rpc *msg;
	u32 fn = rpc->function;
	void *repv = NULL;
	int ret;

	if (gsp->subdev.debug >= NV_DBG_TRACE) {
		nvkm_trace(&gsp->subdev, "rpc fn:%d len:0x%x/0x%zx\n", rpc->function,
			   rpc->length, rpc->length - sizeof(*rpc));
		print_hex_dump(KERN_INFO, "rpc: ", DUMP_PREFIX_OFFSET, 16, 1,
			       rpc->data, rpc->length - sizeof(*rpc), true);
	}

	ret = r535_gsp_cmdq_push(gsp, rpc);
	if (ret)
		return ERR_PTR(ret);

	if (wait) {
		msg = r535_gsp_msg_recv(gsp, fn, repc);
		if (!IS_ERR_OR_NULL(msg))
			repv = msg->data;
		else
			repv = msg;
	}

	return repv;
}

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

	return nvkm_gsp_rm_alloc(&device->object, 0x5d1d0000, NV20_SUBDEVICE_0, sizeof(*args),
				 &device->subdevice);
}

static int
r535_gsp_device_ctor(struct nvkm_gsp_client *client, struct nvkm_gsp_device *device)
{
	NV0080_ALLOC_PARAMETERS *args;
	int ret;

	args = nvkm_gsp_rm_alloc_get(&client->object, 0xde1d0000, NV01_DEVICE_0, sizeof(*args),
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

static void
r535_gsp_client_dtor(struct nvkm_gsp_client *client)
{
	struct nvkm_gsp *gsp = client->gsp;

	nvkm_gsp_rm_free(&client->object);

	mutex_lock(&gsp->client_id.mutex);
	idr_remove(&gsp->client_id.idr, client->object.handle & 0xffff);
	mutex_unlock(&gsp->client_id.mutex);

	client->gsp = NULL;
}

static int
r535_gsp_client_ctor(struct nvkm_gsp *gsp, struct nvkm_gsp_client *client)
{
	NV0000_ALLOC_PARAMETERS *args;
	int ret;

	mutex_lock(&gsp->client_id.mutex);
	ret = idr_alloc(&gsp->client_id.idr, client, 0, 0xffff + 1, GFP_KERNEL);
	mutex_unlock(&gsp->client_id.mutex);
	if (ret < 0)
		return ret;

	client->gsp = gsp;
	client->object.client = client;
	INIT_LIST_HEAD(&client->events);

	args = nvkm_gsp_rm_alloc_get(&client->object, 0xc1d00000 | ret, NV01_ROOT, sizeof(*args),
				     &client->object);
	if (IS_ERR(args)) {
		r535_gsp_client_dtor(client);
		return ret;
	}

	args->hClient = client->object.handle;
	args->processID = ~0;

	ret = nvkm_gsp_rm_alloc_wr(&client->object, args);
	if (ret) {
		r535_gsp_client_dtor(client);
		return ret;
	}

	return 0;
}

static int
r535_gsp_rpc_rm_free(struct nvkm_gsp_object *object)
{
	struct nvkm_gsp_client *client = object->client;
	struct nvkm_gsp *gsp = client->gsp;
	rpc_free_v03_00 *rpc;

	nvkm_debug(&gsp->subdev, "cli:0x%08x obj:0x%08x free\n",
		   client->object.handle, object->handle);

	rpc = nvkm_gsp_rpc_get(gsp, NV_VGPU_MSG_FUNCTION_FREE, sizeof(*rpc));
	if (WARN_ON(IS_ERR_OR_NULL(rpc)))
		return -EIO;

	rpc->params.hRoot = client->object.handle;
	rpc->params.hObjectParent = 0;
	rpc->params.hObjectOld = object->handle;
	return nvkm_gsp_rpc_wr(gsp, rpc, true);
}

static void
r535_gsp_rpc_rm_alloc_done(struct nvkm_gsp_object *object, void *repv)
{
	rpc_gsp_rm_alloc_v03_00 *rpc = container_of(repv, typeof(*rpc), params);

	nvkm_gsp_rpc_done(object->client->gsp, rpc);
}

static void *
r535_gsp_rpc_rm_alloc_push(struct nvkm_gsp_object *object, void *argv, u32 repc)
{
	rpc_gsp_rm_alloc_v03_00 *rpc = container_of(argv, typeof(*rpc), params);
	struct nvkm_gsp *gsp = object->client->gsp;
	void *ret;

	rpc = nvkm_gsp_rpc_push(gsp, rpc, true, sizeof(*rpc) + repc);
	if (IS_ERR_OR_NULL(rpc))
		return rpc;

	if (rpc->status) {
		ret = ERR_PTR(r535_rpc_status_to_errno(rpc->status));
		if (PTR_ERR(ret) != -EAGAIN)
			nvkm_error(&gsp->subdev, "RM_ALLOC: 0x%x\n", rpc->status);
	} else {
		ret = repc ? rpc->params : NULL;
	}

	nvkm_gsp_rpc_done(gsp, rpc);

	return ret;
}

static void *
r535_gsp_rpc_rm_alloc_get(struct nvkm_gsp_object *object, u32 oclass, u32 argc)
{
	struct nvkm_gsp_client *client = object->client;
	struct nvkm_gsp *gsp = client->gsp;
	rpc_gsp_rm_alloc_v03_00 *rpc;

	nvkm_debug(&gsp->subdev, "cli:0x%08x obj:0x%08x new obj:0x%08x cls:0x%08x argc:%d\n",
		   client->object.handle, object->parent->handle, object->handle, oclass, argc);

	rpc = nvkm_gsp_rpc_get(gsp, NV_VGPU_MSG_FUNCTION_GSP_RM_ALLOC, sizeof(*rpc) + argc);
	if (IS_ERR(rpc))
		return rpc;

	rpc->hClient = client->object.handle;
	rpc->hParent = object->parent->handle;
	rpc->hObject = object->handle;
	rpc->hClass = oclass;
	rpc->status = 0;
	rpc->paramsSize = argc;
	return rpc->params;
}

static void
r535_gsp_rpc_rm_ctrl_done(struct nvkm_gsp_object *object, void *repv)
{
	rpc_gsp_rm_control_v03_00 *rpc = container_of(repv, typeof(*rpc), params);

	if (!repv)
		return;
	nvkm_gsp_rpc_done(object->client->gsp, rpc);
}

static int
r535_gsp_rpc_rm_ctrl_push(struct nvkm_gsp_object *object, void **argv, u32 repc)
{
	rpc_gsp_rm_control_v03_00 *rpc = container_of((*argv), typeof(*rpc), params);
	struct nvkm_gsp *gsp = object->client->gsp;
	int ret = 0;

	rpc = nvkm_gsp_rpc_push(gsp, rpc, true, repc);
	if (IS_ERR_OR_NULL(rpc)) {
		*argv = NULL;
		return PTR_ERR(rpc);
	}

	if (rpc->status) {
		ret = r535_rpc_status_to_errno(rpc->status);
		if (ret != -EAGAIN)
			nvkm_error(&gsp->subdev, "cli:0x%08x obj:0x%08x ctrl cmd:0x%08x failed: 0x%08x\n",
				   object->client->object.handle, object->handle, rpc->cmd, rpc->status);
	}

	if (repc)
		*argv = rpc->params;
	else
		nvkm_gsp_rpc_done(gsp, rpc);

	return ret;
}

static void *
r535_gsp_rpc_rm_ctrl_get(struct nvkm_gsp_object *object, u32 cmd, u32 argc)
{
	struct nvkm_gsp_client *client = object->client;
	struct nvkm_gsp *gsp = client->gsp;
	rpc_gsp_rm_control_v03_00 *rpc;

	nvkm_debug(&gsp->subdev, "cli:0x%08x obj:0x%08x ctrl cmd:0x%08x argc:%d\n",
		   client->object.handle, object->handle, cmd, argc);

	rpc = nvkm_gsp_rpc_get(gsp, NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL, sizeof(*rpc) + argc);
	if (IS_ERR(rpc))
		return rpc;

	rpc->hClient    = client->object.handle;
	rpc->hObject    = object->handle;
	rpc->cmd	= cmd;
	rpc->status     = 0;
	rpc->paramsSize = argc;
	return rpc->params;
}

static void
r535_gsp_rpc_done(struct nvkm_gsp *gsp, void *repv)
{
	struct nvfw_gsp_rpc *rpc = container_of(repv, typeof(*rpc), data);

	r535_gsp_msg_done(gsp, rpc);
}

static void *
r535_gsp_rpc_get(struct nvkm_gsp *gsp, u32 fn, u32 argc)
{
	struct nvfw_gsp_rpc *rpc;

	rpc = r535_gsp_cmdq_get(gsp, ALIGN(sizeof(*rpc) + argc, sizeof(u64)));
	if (IS_ERR(rpc))
		return ERR_CAST(rpc);

	rpc->header_version = 0x03000000;
	rpc->signature = ('C' << 24) | ('P' << 16) | ('R' << 8) | 'V';
	rpc->function = fn;
	rpc->rpc_result = 0xffffffff;
	rpc->rpc_result_private = 0xffffffff;
	rpc->length = sizeof(*rpc) + argc;
	return rpc->data;
}

static void *
r535_gsp_rpc_push(struct nvkm_gsp *gsp, void *argv, bool wait, u32 repc)
{
	struct nvfw_gsp_rpc *rpc = container_of(argv, typeof(*rpc), data);
	struct r535_gsp_msg *cmd = container_of((void *)rpc, typeof(*cmd), data);
	const u32 max_msg_size = (16 * 0x1000) - sizeof(struct r535_gsp_msg);
	const u32 max_rpc_size = max_msg_size - sizeof(*rpc);
	u32 rpc_size = rpc->length - sizeof(*rpc);
	void *repv;

	mutex_lock(&gsp->cmdq.mutex);
	if (rpc_size > max_rpc_size) {
		const u32 fn = rpc->function;

		/* Adjust length, and send initial RPC. */
		rpc->length = sizeof(*rpc) + max_rpc_size;
		cmd->checksum = rpc->length;

		repv = r535_gsp_rpc_send(gsp, argv, false, 0);
		if (IS_ERR(repv))
			goto done;

		argv += max_rpc_size;
		rpc_size -= max_rpc_size;

		/* Remaining chunks sent as CONTINUATION_RECORD RPCs. */
		while (rpc_size) {
			u32 size = min(rpc_size, max_rpc_size);
			void *next;

			next = r535_gsp_rpc_get(gsp, NV_VGPU_MSG_FUNCTION_CONTINUATION_RECORD, size);
			if (IS_ERR(next)) {
				repv = next;
				goto done;
			}

			memcpy(next, argv, size);

			repv = r535_gsp_rpc_send(gsp, next, false, 0);
			if (IS_ERR(repv))
				goto done;

			argv += size;
			rpc_size -= size;
		}

		/* Wait for reply. */
		if (wait) {
			rpc = r535_gsp_msg_recv(gsp, fn, repc);
			if (!IS_ERR_OR_NULL(rpc))
				repv = rpc->data;
			else
				repv = rpc;
		} else {
			repv = NULL;
		}
	} else {
		repv = r535_gsp_rpc_send(gsp, argv, wait, repc);
	}

done:
	mutex_unlock(&gsp->cmdq.mutex);
	return repv;
}

const struct nvkm_gsp_rm
r535_gsp_rm = {
	.rpc_get = r535_gsp_rpc_get,
	.rpc_push = r535_gsp_rpc_push,
	.rpc_done = r535_gsp_rpc_done,

	.rm_ctrl_get = r535_gsp_rpc_rm_ctrl_get,
	.rm_ctrl_push = r535_gsp_rpc_rm_ctrl_push,
	.rm_ctrl_done = r535_gsp_rpc_rm_ctrl_done,

	.rm_alloc_get = r535_gsp_rpc_rm_alloc_get,
	.rm_alloc_push = r535_gsp_rpc_rm_alloc_push,
	.rm_alloc_done = r535_gsp_rpc_rm_alloc_done,

	.rm_free = r535_gsp_rpc_rm_free,

	.client_ctor = r535_gsp_client_ctor,
	.client_dtor = r535_gsp_client_dtor,

	.device_ctor = r535_gsp_device_ctor,
	.device_dtor = r535_gsp_device_dtor,

	.event_ctor = r535_gsp_device_event_ctor,
	.event_dtor = r535_gsp_event_dtor,
};

static void
r535_gsp_msgq_work(struct work_struct *work)
{
	struct nvkm_gsp *gsp = container_of(work, typeof(*gsp), msgq.work);

	mutex_lock(&gsp->cmdq.mutex);
	if (*gsp->msgq.rptr != *gsp->msgq.wptr)
		r535_gsp_msg_recv(gsp, 0, 0);
	mutex_unlock(&gsp->cmdq.mutex);
}

static irqreturn_t
r535_gsp_intr(struct nvkm_inth *inth)
{
	struct nvkm_gsp *gsp = container_of(inth, typeof(*gsp), subdev.inth);
	struct nvkm_subdev *subdev = &gsp->subdev;
	u32 intr = nvkm_falcon_rd32(&gsp->falcon, 0x0008);
	u32 inte = nvkm_falcon_rd32(&gsp->falcon, gsp->falcon.func->addr2 +
						  gsp->falcon.func->riscv_irqmask);
	u32 stat = intr & inte;

	if (!stat) {
		nvkm_debug(subdev, "inte %08x %08x\n", intr, inte);
		return IRQ_NONE;
	}

	if (stat & 0x00000040) {
		nvkm_falcon_wr32(&gsp->falcon, 0x004, 0x00000040);
		schedule_work(&gsp->msgq.work);
		stat &= ~0x00000040;
	}

	if (stat) {
		nvkm_error(subdev, "intr %08x\n", stat);
		nvkm_falcon_wr32(&gsp->falcon, 0x014, stat);
		nvkm_falcon_wr32(&gsp->falcon, 0x004, stat);
	}

	nvkm_falcon_intr_retrigger(&gsp->falcon);
	return IRQ_HANDLED;
}

static int
r535_gsp_intr_get_table(struct nvkm_gsp *gsp)
{
	NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_PARAMS *ctrl;
	int ret = 0;

	ctrl = nvkm_gsp_rm_ctrl_get(&gsp->internal.device.subdevice,
				    NV2080_CTRL_CMD_INTERNAL_INTR_GET_KERNEL_TABLE, sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	ret = nvkm_gsp_rm_ctrl_push(&gsp->internal.device.subdevice, &ctrl, sizeof(*ctrl));
	if (WARN_ON(ret)) {
		nvkm_gsp_rm_ctrl_done(&gsp->internal.device.subdevice, ctrl);
		return ret;
	}

	for (unsigned i = 0; i < ctrl->tableLen; i++) {
		enum nvkm_subdev_type type;
		int inst;

		nvkm_debug(&gsp->subdev,
			   "%2d: engineIdx %3d pmcIntrMask %08x stall %08x nonStall %08x\n", i,
			   ctrl->table[i].engineIdx, ctrl->table[i].pmcIntrMask,
			   ctrl->table[i].vectorStall, ctrl->table[i].vectorNonStall);

		switch (ctrl->table[i].engineIdx) {
		case MC_ENGINE_IDX_GSP:
			type = NVKM_SUBDEV_GSP;
			inst = 0;
			break;
		case MC_ENGINE_IDX_DISP:
			type = NVKM_ENGINE_DISP;
			inst = 0;
			break;
		case MC_ENGINE_IDX_CE0 ... MC_ENGINE_IDX_CE9:
			type = NVKM_ENGINE_CE;
			inst = ctrl->table[i].engineIdx - MC_ENGINE_IDX_CE0;
			break;
		case MC_ENGINE_IDX_GR0:
			type = NVKM_ENGINE_GR;
			inst = 0;
			break;
		case MC_ENGINE_IDX_NVDEC0 ... MC_ENGINE_IDX_NVDEC7:
			type = NVKM_ENGINE_NVDEC;
			inst = ctrl->table[i].engineIdx - MC_ENGINE_IDX_NVDEC0;
			break;
		case MC_ENGINE_IDX_MSENC ... MC_ENGINE_IDX_MSENC2:
			type = NVKM_ENGINE_NVENC;
			inst = ctrl->table[i].engineIdx - MC_ENGINE_IDX_MSENC;
			break;
		case MC_ENGINE_IDX_NVJPEG0 ... MC_ENGINE_IDX_NVJPEG7:
			type = NVKM_ENGINE_NVJPG;
			inst = ctrl->table[i].engineIdx - MC_ENGINE_IDX_NVJPEG0;
			break;
		case MC_ENGINE_IDX_OFA0:
			type = NVKM_ENGINE_OFA;
			inst = 0;
			break;
		default:
			continue;
		}

		if (WARN_ON(gsp->intr_nr == ARRAY_SIZE(gsp->intr))) {
			ret = -ENOSPC;
			break;
		}

		gsp->intr[gsp->intr_nr].type = type;
		gsp->intr[gsp->intr_nr].inst = inst;
		gsp->intr[gsp->intr_nr].stall = ctrl->table[i].vectorStall;
		gsp->intr[gsp->intr_nr].nonstall = ctrl->table[i].vectorNonStall;
		gsp->intr_nr++;
	}

	nvkm_gsp_rm_ctrl_done(&gsp->internal.device.subdevice, ctrl);
	return ret;
}

static int
r535_gsp_rpc_get_gsp_static_info(struct nvkm_gsp *gsp)
{
	GspStaticConfigInfo *rpc;
	int last_usable = -1;

	rpc = nvkm_gsp_rpc_rd(gsp, NV_VGPU_MSG_FUNCTION_GET_GSP_STATIC_INFO, sizeof(*rpc));
	if (IS_ERR(rpc))
		return PTR_ERR(rpc);

	gsp->internal.client.object.client = &gsp->internal.client;
	gsp->internal.client.object.parent = NULL;
	gsp->internal.client.object.handle = rpc->hInternalClient;
	gsp->internal.client.gsp = gsp;

	gsp->internal.device.object.client = &gsp->internal.client;
	gsp->internal.device.object.parent = &gsp->internal.client.object;
	gsp->internal.device.object.handle = rpc->hInternalDevice;

	gsp->internal.device.subdevice.client = &gsp->internal.client;
	gsp->internal.device.subdevice.parent = &gsp->internal.device.object;
	gsp->internal.device.subdevice.handle = rpc->hInternalSubdevice;

	gsp->bar.rm_bar1_pdb = rpc->bar1PdeBase;
	gsp->bar.rm_bar2_pdb = rpc->bar2PdeBase;

	for (int i = 0; i < rpc->fbRegionInfoParams.numFBRegions; i++) {
		NV2080_CTRL_CMD_FB_GET_FB_REGION_FB_REGION_INFO *reg =
			&rpc->fbRegionInfoParams.fbRegion[i];

		nvkm_debug(&gsp->subdev, "fb region %d: "
			   "%016llx-%016llx rsvd:%016llx perf:%08x comp:%d iso:%d prot:%d\n", i,
			   reg->base, reg->limit, reg->reserved, reg->performance,
			   reg->supportCompressed, reg->supportISO, reg->bProtected);

		if (!reg->reserved && !reg->bProtected) {
			if (reg->supportCompressed && reg->supportISO &&
			    !WARN_ON_ONCE(gsp->fb.region_nr >= ARRAY_SIZE(gsp->fb.region))) {
					const u64 size = (reg->limit + 1) - reg->base;

					gsp->fb.region[gsp->fb.region_nr].addr = reg->base;
					gsp->fb.region[gsp->fb.region_nr].size = size;
					gsp->fb.region_nr++;
			}

			last_usable = i;
		}
	}

	if (last_usable >= 0) {
		u32 rsvd_base = rpc->fbRegionInfoParams.fbRegion[last_usable].limit + 1;

		gsp->fb.rsvd_size = gsp->fb.heap.addr - rsvd_base;
	}

	for (int gpc = 0; gpc < ARRAY_SIZE(rpc->tpcInfo); gpc++) {
		if (rpc->gpcInfo.gpcMask & BIT(gpc)) {
			gsp->gr.tpcs += hweight32(rpc->tpcInfo[gpc].tpcMask);
			gsp->gr.gpcs++;
		}
	}

	nvkm_gsp_rpc_done(gsp, rpc);
	return 0;
}

static void
nvkm_gsp_mem_dtor(struct nvkm_gsp *gsp, struct nvkm_gsp_mem *mem)
{
	if (mem->data) {
		/*
		 * Poison the buffer to catch any unexpected access from
		 * GSP-RM if the buffer was prematurely freed.
		 */
		memset(mem->data, 0xFF, mem->size);

		dma_free_coherent(gsp->subdev.device->dev, mem->size, mem->data, mem->addr);
		memset(mem, 0, sizeof(*mem));
	}
}

static int
nvkm_gsp_mem_ctor(struct nvkm_gsp *gsp, size_t size, struct nvkm_gsp_mem *mem)
{
	mem->size = size;
	mem->data = dma_alloc_coherent(gsp->subdev.device->dev, size, &mem->addr, GFP_KERNEL);
	if (WARN_ON(!mem->data))
		return -ENOMEM;

	return 0;
}

static int
r535_gsp_postinit(struct nvkm_gsp *gsp)
{
	struct nvkm_device *device = gsp->subdev.device;
	int ret;

	ret = r535_gsp_rpc_get_gsp_static_info(gsp);
	if (WARN_ON(ret))
		return ret;

	INIT_WORK(&gsp->msgq.work, r535_gsp_msgq_work);

	ret = r535_gsp_intr_get_table(gsp);
	if (WARN_ON(ret))
		return ret;

	ret = nvkm_gsp_intr_stall(gsp, gsp->subdev.type, gsp->subdev.inst);
	if (WARN_ON(ret < 0))
		return ret;

	ret = nvkm_inth_add(&device->vfn->intr, ret, NVKM_INTR_PRIO_NORMAL, &gsp->subdev,
			    r535_gsp_intr, &gsp->subdev.inth);
	if (WARN_ON(ret))
		return ret;

	nvkm_inth_allow(&gsp->subdev.inth);
	nvkm_wr32(device, 0x110004, 0x00000040);

	/* Release the DMA buffers that were needed only for boot and init */
	nvkm_gsp_mem_dtor(gsp, &gsp->boot.fw);
	nvkm_gsp_mem_dtor(gsp, &gsp->libos);
	nvkm_gsp_mem_dtor(gsp, &gsp->rmargs);
	nvkm_gsp_mem_dtor(gsp, &gsp->wpr_meta);

	return ret;
}

static int
r535_gsp_rpc_unloading_guest_driver(struct nvkm_gsp *gsp, bool suspend)
{
	rpc_unloading_guest_driver_v1F_07 *rpc;

	rpc = nvkm_gsp_rpc_get(gsp, NV_VGPU_MSG_FUNCTION_UNLOADING_GUEST_DRIVER, sizeof(*rpc));
	if (IS_ERR(rpc))
		return PTR_ERR(rpc);

	if (suspend) {
		rpc->bInPMTransition = 1;
		rpc->bGc6Entering = 0;
		rpc->newLevel = NV2080_CTRL_GPU_SET_POWER_STATE_GPU_LEVEL_3;
	} else {
		rpc->bInPMTransition = 0;
		rpc->bGc6Entering = 0;
		rpc->newLevel = NV2080_CTRL_GPU_SET_POWER_STATE_GPU_LEVEL_0;
	}

	return nvkm_gsp_rpc_wr(gsp, rpc, true);
}

/* dword only */
struct nv_gsp_registry_entries {
	const char *name;
	u32 value;
};

static const struct nv_gsp_registry_entries r535_registry_entries[] = {
	{ "RMSecBusResetEnable", 1 },
	{ "RMForcePcieConfigSave", 1 },
};
#define NV_GSP_REG_NUM_ENTRIES ARRAY_SIZE(r535_registry_entries)

static int
r535_gsp_rpc_set_registry(struct nvkm_gsp *gsp)
{
	PACKED_REGISTRY_TABLE *rpc;
	char *strings;
	int str_offset;
	int i;
	size_t rpc_size = struct_size(rpc, entries, NV_GSP_REG_NUM_ENTRIES);

	/* add strings + null terminator */
	for (i = 0; i < NV_GSP_REG_NUM_ENTRIES; i++)
		rpc_size += strlen(r535_registry_entries[i].name) + 1;

	rpc = nvkm_gsp_rpc_get(gsp, NV_VGPU_MSG_FUNCTION_SET_REGISTRY, rpc_size);
	if (IS_ERR(rpc))
		return PTR_ERR(rpc);

	rpc->numEntries = NV_GSP_REG_NUM_ENTRIES;

	str_offset = offsetof(typeof(*rpc), entries[NV_GSP_REG_NUM_ENTRIES]);
	strings = (char *)&rpc->entries[NV_GSP_REG_NUM_ENTRIES];
	for (i = 0; i < NV_GSP_REG_NUM_ENTRIES; i++) {
		int name_len = strlen(r535_registry_entries[i].name) + 1;

		rpc->entries[i].nameOffset = str_offset;
		rpc->entries[i].type = 1;
		rpc->entries[i].data = r535_registry_entries[i].value;
		rpc->entries[i].length = 4;
		memcpy(strings, r535_registry_entries[i].name, name_len);
		strings += name_len;
		str_offset += name_len;
	}
	rpc->size = str_offset;

	return nvkm_gsp_rpc_wr(gsp, rpc, false);
}

#if defined(CONFIG_ACPI) && defined(CONFIG_X86)
static void
r535_gsp_acpi_caps(acpi_handle handle, CAPS_METHOD_DATA *caps)
{
	const guid_t NVOP_DSM_GUID =
		GUID_INIT(0xA486D8F8, 0x0BDA, 0x471B,
			  0xA7, 0x2B, 0x60, 0x42, 0xA6, 0xB5, 0xBE, 0xE0);
	u64 NVOP_DSM_REV = 0x00000100;
	union acpi_object argv4 = {
		.buffer.type    = ACPI_TYPE_BUFFER,
		.buffer.length  = 4,
		.buffer.pointer = kmalloc(argv4.buffer.length, GFP_KERNEL),
	}, *obj;

	caps->status = 0xffff;

	if (!acpi_check_dsm(handle, &NVOP_DSM_GUID, NVOP_DSM_REV, BIT_ULL(0x1a)))
		return;

	obj = acpi_evaluate_dsm(handle, &NVOP_DSM_GUID, NVOP_DSM_REV, 0x1a, &argv4);
	if (!obj)
		return;

	if (WARN_ON(obj->type != ACPI_TYPE_BUFFER) ||
	    WARN_ON(obj->buffer.length != 4))
		return;

	caps->status = 0;
	caps->optimusCaps = *(u32 *)obj->buffer.pointer;

	ACPI_FREE(obj);

	kfree(argv4.buffer.pointer);
}

static void
r535_gsp_acpi_jt(acpi_handle handle, JT_METHOD_DATA *jt)
{
	const guid_t JT_DSM_GUID =
		GUID_INIT(0xCBECA351L, 0x067B, 0x4924,
			  0x9C, 0xBD, 0xB4, 0x6B, 0x00, 0xB8, 0x6F, 0x34);
	u64 JT_DSM_REV = 0x00000103;
	u32 caps;
	union acpi_object argv4 = {
		.buffer.type    = ACPI_TYPE_BUFFER,
		.buffer.length  = sizeof(caps),
		.buffer.pointer = kmalloc(argv4.buffer.length, GFP_KERNEL),
	}, *obj;

	jt->status = 0xffff;

	obj = acpi_evaluate_dsm(handle, &JT_DSM_GUID, JT_DSM_REV, 0x1, &argv4);
	if (!obj)
		return;

	if (WARN_ON(obj->type != ACPI_TYPE_BUFFER) ||
	    WARN_ON(obj->buffer.length != 4))
		return;

	jt->status = 0;
	jt->jtCaps = *(u32 *)obj->buffer.pointer;
	jt->jtRevId = (jt->jtCaps & 0xfff00000) >> 20;
	jt->bSBIOSCaps = 0;

	ACPI_FREE(obj);

	kfree(argv4.buffer.pointer);
}

static void
r535_gsp_acpi_mux_id(acpi_handle handle, u32 id, MUX_METHOD_DATA_ELEMENT *mode,
						 MUX_METHOD_DATA_ELEMENT *part)
{
	union acpi_object mux_arg = { ACPI_TYPE_INTEGER };
	struct acpi_object_list input = { 1, &mux_arg };
	acpi_handle iter = NULL, handle_mux = NULL;
	acpi_status status;
	unsigned long long value;

	mode->status = 0xffff;
	part->status = 0xffff;

	do {
		status = acpi_get_next_object(ACPI_TYPE_DEVICE, handle, iter, &iter);
		if (ACPI_FAILURE(status) || !iter)
			return;

		status = acpi_evaluate_integer(iter, "_ADR", NULL, &value);
		if (ACPI_FAILURE(status) || value != id)
			continue;

		handle_mux = iter;
	} while (!handle_mux);

	if (!handle_mux)
		return;

	/* I -think- 0 means "acquire" according to nvidia's driver source */
	input.pointer->integer.type = ACPI_TYPE_INTEGER;
	input.pointer->integer.value = 0;

	status = acpi_evaluate_integer(handle_mux, "MXDM", &input, &value);
	if (ACPI_SUCCESS(status)) {
		mode->acpiId = id;
		mode->mode   = value;
		mode->status = 0;
	}

	status = acpi_evaluate_integer(handle_mux, "MXDS", &input, &value);
	if (ACPI_SUCCESS(status)) {
		part->acpiId = id;
		part->mode   = value;
		part->status = 0;
	}
}

static void
r535_gsp_acpi_mux(acpi_handle handle, DOD_METHOD_DATA *dod, MUX_METHOD_DATA *mux)
{
	mux->tableLen = dod->acpiIdListLen / sizeof(dod->acpiIdList[0]);

	for (int i = 0; i < mux->tableLen; i++) {
		r535_gsp_acpi_mux_id(handle, dod->acpiIdList[i], &mux->acpiIdMuxModeTable[i],
								 &mux->acpiIdMuxPartTable[i]);
	}
}

static void
r535_gsp_acpi_dod(acpi_handle handle, DOD_METHOD_DATA *dod)
{
	acpi_status status;
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *_DOD;

	dod->status = 0xffff;

	status = acpi_evaluate_object(handle, "_DOD", NULL, &output);
	if (ACPI_FAILURE(status))
		return;

	_DOD = output.pointer;

	if (WARN_ON(_DOD->type != ACPI_TYPE_PACKAGE) ||
	    WARN_ON(_DOD->package.count > ARRAY_SIZE(dod->acpiIdList)))
		return;

	for (int i = 0; i < _DOD->package.count; i++) {
		if (WARN_ON(_DOD->package.elements[i].type != ACPI_TYPE_INTEGER))
			return;

		dod->acpiIdList[i] = _DOD->package.elements[i].integer.value;
		dod->acpiIdListLen += sizeof(dod->acpiIdList[0]);
	}

	dod->status = 0;
	kfree(output.pointer);
}
#endif

static void
r535_gsp_acpi_info(struct nvkm_gsp *gsp, ACPI_METHOD_DATA *acpi)
{
#if defined(CONFIG_ACPI) && defined(CONFIG_X86)
	acpi_handle handle = ACPI_HANDLE(gsp->subdev.device->dev);

	if (!handle)
		return;

	acpi->bValid = 1;

	r535_gsp_acpi_dod(handle, &acpi->dodMethodData);
	if (acpi->dodMethodData.status == 0)
		r535_gsp_acpi_mux(handle, &acpi->dodMethodData, &acpi->muxMethodData);

	r535_gsp_acpi_jt(handle, &acpi->jtMethodData);
	r535_gsp_acpi_caps(handle, &acpi->capsMethodData);
#endif
}

static int
r535_gsp_rpc_set_system_info(struct nvkm_gsp *gsp)
{
	struct nvkm_device *device = gsp->subdev.device;
	struct nvkm_device_pci *pdev = container_of(device, typeof(*pdev), device);
	GspSystemInfo *info;

	if (WARN_ON(device->type == NVKM_DEVICE_TEGRA))
		return -ENOSYS;

	info = nvkm_gsp_rpc_get(gsp, NV_VGPU_MSG_FUNCTION_GSP_SET_SYSTEM_INFO, sizeof(*info));
	if (IS_ERR(info))
		return PTR_ERR(info);

	info->gpuPhysAddr = device->func->resource_addr(device, 0);
	info->gpuPhysFbAddr = device->func->resource_addr(device, 1);
	info->gpuPhysInstAddr = device->func->resource_addr(device, 3);
	info->nvDomainBusDeviceFunc = pci_dev_id(pdev->pdev);
	info->maxUserVa = TASK_SIZE;
	info->pciConfigMirrorBase = 0x088000;
	info->pciConfigMirrorSize = 0x001000;
	r535_gsp_acpi_info(gsp, &info->acpiMethodData);

	return nvkm_gsp_rpc_wr(gsp, info, false);
}

static int
r535_gsp_msg_os_error_log(void *priv, u32 fn, void *repv, u32 repc)
{
	struct nvkm_gsp *gsp = priv;
	struct nvkm_subdev *subdev = &gsp->subdev;
	rpc_os_error_log_v17_00 *msg = repv;

	if (WARN_ON(repc < sizeof(*msg)))
		return -EINVAL;

	nvkm_error(subdev, "Xid:%d %s\n", msg->exceptType, msg->errString);
	return 0;
}

static int
r535_gsp_msg_rc_triggered(void *priv, u32 fn, void *repv, u32 repc)
{
	rpc_rc_triggered_v17_02 *msg = repv;
	struct nvkm_gsp *gsp = priv;
	struct nvkm_subdev *subdev = &gsp->subdev;
	struct nvkm_chan *chan;
	unsigned long flags;

	if (WARN_ON(repc < sizeof(*msg)))
		return -EINVAL;

	nvkm_error(subdev, "rc engn:%08x chid:%d type:%d scope:%d part:%d\n",
		   msg->nv2080EngineType, msg->chid, msg->exceptType, msg->scope,
		   msg->partitionAttributionId);

	chan = nvkm_chan_get_chid(&subdev->device->fifo->engine, msg->chid / 8, &flags);
	if (!chan) {
		nvkm_error(subdev, "rc chid:%d not found!\n", msg->chid);
		return 0;
	}

	nvkm_chan_error(chan, false);
	nvkm_chan_put(&chan, flags);
	return 0;
}

static int
r535_gsp_msg_mmu_fault_queued(void *priv, u32 fn, void *repv, u32 repc)
{
	struct nvkm_gsp *gsp = priv;
	struct nvkm_subdev *subdev = &gsp->subdev;

	WARN_ON(repc != 0);

	nvkm_error(subdev, "mmu fault queued\n");
	return 0;
}

static int
r535_gsp_msg_post_event(void *priv, u32 fn, void *repv, u32 repc)
{
	struct nvkm_gsp *gsp = priv;
	struct nvkm_gsp_client *client;
	struct nvkm_subdev *subdev = &gsp->subdev;
	rpc_post_event_v17_00 *msg = repv;

	if (WARN_ON(repc < sizeof(*msg)))
		return -EINVAL;
	if (WARN_ON(repc != sizeof(*msg) + msg->eventDataSize))
		return -EINVAL;

	nvkm_debug(subdev, "event: %08x %08x %d %08x %08x %d %d\n",
		   msg->hClient, msg->hEvent, msg->notifyIndex, msg->data,
		   msg->status, msg->eventDataSize, msg->bNotifyList);

	mutex_lock(&gsp->client_id.mutex);
	client = idr_find(&gsp->client_id.idr, msg->hClient & 0xffff);
	if (client) {
		struct nvkm_gsp_event *event;
		bool handled = false;

		list_for_each_entry(event, &client->events, head) {
			if (event->object.handle == msg->hEvent) {
				event->func(event, msg->eventData, msg->eventDataSize);
				handled = true;
			}
		}

		if (!handled) {
			nvkm_error(subdev, "event: cid 0x%08x event 0x%08x not found!\n",
				   msg->hClient, msg->hEvent);
		}
	} else {
		nvkm_error(subdev, "event: cid 0x%08x not found!\n", msg->hClient);
	}
	mutex_unlock(&gsp->client_id.mutex);
	return 0;
}

/**
 * r535_gsp_msg_run_cpu_sequencer() -- process I/O commands from the GSP
 *
 * The GSP sequencer is a list of I/O commands that the GSP can send to
 * the driver to perform for various purposes.  The most common usage is to
 * perform a special mid-initialization reset.
 */
static int
r535_gsp_msg_run_cpu_sequencer(void *priv, u32 fn, void *repv, u32 repc)
{
	struct nvkm_gsp *gsp = priv;
	struct nvkm_subdev *subdev = &gsp->subdev;
	struct nvkm_device *device = subdev->device;
	rpc_run_cpu_sequencer_v17_00 *seq = repv;
	int ptr = 0, ret;

	nvkm_debug(subdev, "seq: %08x %08x\n", seq->bufferSizeDWord, seq->cmdIndex);

	while (ptr < seq->cmdIndex) {
		GSP_SEQUENCER_BUFFER_CMD *cmd = (void *)&seq->commandBuffer[ptr];

		ptr += 1;
		ptr += GSP_SEQUENCER_PAYLOAD_SIZE_DWORDS(cmd->opCode);

		switch (cmd->opCode) {
		case GSP_SEQ_BUF_OPCODE_REG_WRITE: {
			u32 addr = cmd->payload.regWrite.addr;
			u32 data = cmd->payload.regWrite.val;

			nvkm_trace(subdev, "seq wr32 %06x %08x\n", addr, data);
			nvkm_wr32(device, addr, data);
		}
			break;
		case GSP_SEQ_BUF_OPCODE_REG_MODIFY: {
			u32 addr = cmd->payload.regModify.addr;
			u32 mask = cmd->payload.regModify.mask;
			u32 data = cmd->payload.regModify.val;

			nvkm_trace(subdev, "seq mask %06x %08x %08x\n", addr, mask, data);
			nvkm_mask(device, addr, mask, data);
		}
			break;
		case GSP_SEQ_BUF_OPCODE_REG_POLL: {
			u32 addr = cmd->payload.regPoll.addr;
			u32 mask = cmd->payload.regPoll.mask;
			u32 data = cmd->payload.regPoll.val;
			u32 usec = cmd->payload.regPoll.timeout ?: 4000000;
			//u32 error = cmd->payload.regPoll.error;

			nvkm_trace(subdev, "seq poll %06x %08x %08x %d\n", addr, mask, data, usec);
			nvkm_rd32(device, addr);
			nvkm_usec(device, usec,
				if ((nvkm_rd32(device, addr) & mask) == data)
					break;
			);
		}
			break;
		case GSP_SEQ_BUF_OPCODE_DELAY_US: {
			u32 usec = cmd->payload.delayUs.val;

			nvkm_trace(subdev, "seq usec %d\n", usec);
			udelay(usec);
		}
			break;
		case GSP_SEQ_BUF_OPCODE_REG_STORE: {
			u32 addr = cmd->payload.regStore.addr;
			u32 slot = cmd->payload.regStore.index;

			seq->regSaveArea[slot] = nvkm_rd32(device, addr);
			nvkm_trace(subdev, "seq save %08x -> %d: %08x\n", addr, slot,
				   seq->regSaveArea[slot]);
		}
			break;
		case GSP_SEQ_BUF_OPCODE_CORE_RESET:
			nvkm_trace(subdev, "seq core reset\n");
			nvkm_falcon_reset(&gsp->falcon);
			nvkm_falcon_mask(&gsp->falcon, 0x624, 0x00000080, 0x00000080);
			nvkm_falcon_wr32(&gsp->falcon, 0x10c, 0x00000000);
			break;
		case GSP_SEQ_BUF_OPCODE_CORE_START:
			nvkm_trace(subdev, "seq core start\n");
			if (nvkm_falcon_rd32(&gsp->falcon, 0x100) & 0x00000040)
				nvkm_falcon_wr32(&gsp->falcon, 0x130, 0x00000002);
			else
				nvkm_falcon_wr32(&gsp->falcon, 0x100, 0x00000002);
			break;
		case GSP_SEQ_BUF_OPCODE_CORE_WAIT_FOR_HALT:
			nvkm_trace(subdev, "seq core wait halt\n");
			nvkm_msec(device, 2000,
				if (nvkm_falcon_rd32(&gsp->falcon, 0x100) & 0x00000010)
					break;
			);
			break;
		case GSP_SEQ_BUF_OPCODE_CORE_RESUME: {
			struct nvkm_sec2 *sec2 = device->sec2;
			u32 mbox0;

			nvkm_trace(subdev, "seq core resume\n");

			ret = gsp->func->reset(gsp);
			if (WARN_ON(ret))
				return ret;

			nvkm_falcon_wr32(&gsp->falcon, 0x040, lower_32_bits(gsp->libos.addr));
			nvkm_falcon_wr32(&gsp->falcon, 0x044, upper_32_bits(gsp->libos.addr));

			nvkm_falcon_start(&sec2->falcon);

			if (nvkm_msec(device, 2000,
				if (nvkm_rd32(device, 0x1180f8) & 0x04000000)
					break;
			) < 0)
				return -ETIMEDOUT;

			mbox0 = nvkm_falcon_rd32(&sec2->falcon, 0x040);
			if (WARN_ON(mbox0)) {
				nvkm_error(&gsp->subdev, "seq core resume sec2: 0x%x\n", mbox0);
				return -EIO;
			}

			nvkm_falcon_wr32(&gsp->falcon, 0x080, gsp->boot.app_version);

			if (WARN_ON(!nvkm_falcon_riscv_active(&gsp->falcon)))
				return -EIO;
		}
			break;
		default:
			nvkm_error(subdev, "unknown sequencer opcode %08x\n", cmd->opCode);
			return -EINVAL;
		}
	}

	return 0;
}

static int
r535_gsp_booter_unload(struct nvkm_gsp *gsp, u32 mbox0, u32 mbox1)
{
	struct nvkm_subdev *subdev = &gsp->subdev;
	struct nvkm_device *device = subdev->device;
	u32 wpr2_hi;
	int ret;

	wpr2_hi = nvkm_rd32(device, 0x1fa828);
	if (!wpr2_hi) {
		nvkm_debug(subdev, "WPR2 not set - skipping booter unload\n");
		return 0;
	}

	ret = nvkm_falcon_fw_boot(&gsp->booter.unload, &gsp->subdev, true, &mbox0, &mbox1, 0, 0);
	if (WARN_ON(ret))
		return ret;

	wpr2_hi = nvkm_rd32(device, 0x1fa828);
	if (WARN_ON(wpr2_hi))
		return -EIO;

	return 0;
}

static int
r535_gsp_booter_load(struct nvkm_gsp *gsp, u32 mbox0, u32 mbox1)
{
	int ret;

	ret = nvkm_falcon_fw_boot(&gsp->booter.load, &gsp->subdev, true, &mbox0, &mbox1, 0, 0);
	if (ret)
		return ret;

	nvkm_falcon_wr32(&gsp->falcon, 0x080, gsp->boot.app_version);

	if (WARN_ON(!nvkm_falcon_riscv_active(&gsp->falcon)))
		return -EIO;

	return 0;
}

static int
r535_gsp_wpr_meta_init(struct nvkm_gsp *gsp)
{
	GspFwWprMeta *meta;
	int ret;

	ret = nvkm_gsp_mem_ctor(gsp, 0x1000, &gsp->wpr_meta);
	if (ret)
		return ret;

	meta = gsp->wpr_meta.data;

	meta->magic = GSP_FW_WPR_META_MAGIC;
	meta->revision = GSP_FW_WPR_META_REVISION;

	meta->sysmemAddrOfRadix3Elf = gsp->radix3.mem[0].addr;
	meta->sizeOfRadix3Elf = gsp->fb.wpr2.elf.size;

	meta->sysmemAddrOfBootloader = gsp->boot.fw.addr;
	meta->sizeOfBootloader = gsp->boot.fw.size;
	meta->bootloaderCodeOffset = gsp->boot.code_offset;
	meta->bootloaderDataOffset = gsp->boot.data_offset;
	meta->bootloaderManifestOffset = gsp->boot.manifest_offset;

	meta->sysmemAddrOfSignature = gsp->sig.addr;
	meta->sizeOfSignature = gsp->sig.size;

	meta->gspFwRsvdStart = gsp->fb.heap.addr;
	meta->nonWprHeapOffset = gsp->fb.heap.addr;
	meta->nonWprHeapSize = gsp->fb.heap.size;
	meta->gspFwWprStart = gsp->fb.wpr2.addr;
	meta->gspFwHeapOffset = gsp->fb.wpr2.heap.addr;
	meta->gspFwHeapSize = gsp->fb.wpr2.heap.size;
	meta->gspFwOffset = gsp->fb.wpr2.elf.addr;
	meta->bootBinOffset = gsp->fb.wpr2.boot.addr;
	meta->frtsOffset = gsp->fb.wpr2.frts.addr;
	meta->frtsSize = gsp->fb.wpr2.frts.size;
	meta->gspFwWprEnd = ALIGN_DOWN(gsp->fb.bios.vga_workspace.addr, 0x20000);
	meta->fbSize = gsp->fb.size;
	meta->vgaWorkspaceOffset = gsp->fb.bios.vga_workspace.addr;
	meta->vgaWorkspaceSize = gsp->fb.bios.vga_workspace.size;
	meta->bootCount = 0;
	meta->partitionRpcAddr = 0;
	meta->partitionRpcRequestOffset = 0;
	meta->partitionRpcReplyOffset = 0;
	meta->verified = 0;
	return 0;
}

static int
r535_gsp_shared_init(struct nvkm_gsp *gsp)
{
	struct {
		msgqTxHeader tx;
		msgqRxHeader rx;
	} *cmdq, *msgq;
	int ret, i;

	gsp->shm.cmdq.size = 0x40000;
	gsp->shm.msgq.size = 0x40000;

	gsp->shm.ptes.nr  = (gsp->shm.cmdq.size + gsp->shm.msgq.size) >> GSP_PAGE_SHIFT;
	gsp->shm.ptes.nr += DIV_ROUND_UP(gsp->shm.ptes.nr * sizeof(u64), GSP_PAGE_SIZE);
	gsp->shm.ptes.size = ALIGN(gsp->shm.ptes.nr * sizeof(u64), GSP_PAGE_SIZE);

	ret = nvkm_gsp_mem_ctor(gsp, gsp->shm.ptes.size +
				     gsp->shm.cmdq.size +
				     gsp->shm.msgq.size,
				&gsp->shm.mem);
	if (ret)
		return ret;

	gsp->shm.ptes.ptr = gsp->shm.mem.data;
	gsp->shm.cmdq.ptr = (u8 *)gsp->shm.ptes.ptr + gsp->shm.ptes.size;
	gsp->shm.msgq.ptr = (u8 *)gsp->shm.cmdq.ptr + gsp->shm.cmdq.size;

	for (i = 0; i < gsp->shm.ptes.nr; i++)
		gsp->shm.ptes.ptr[i] = gsp->shm.mem.addr + (i << GSP_PAGE_SHIFT);

	cmdq = gsp->shm.cmdq.ptr;
	cmdq->tx.version = 0;
	cmdq->tx.size = gsp->shm.cmdq.size;
	cmdq->tx.entryOff = GSP_PAGE_SIZE;
	cmdq->tx.msgSize = GSP_PAGE_SIZE;
	cmdq->tx.msgCount = (cmdq->tx.size - cmdq->tx.entryOff) / cmdq->tx.msgSize;
	cmdq->tx.writePtr = 0;
	cmdq->tx.flags = 1;
	cmdq->tx.rxHdrOff = offsetof(typeof(*cmdq), rx.readPtr);

	msgq = gsp->shm.msgq.ptr;

	gsp->cmdq.cnt = cmdq->tx.msgCount;
	gsp->cmdq.wptr = &cmdq->tx.writePtr;
	gsp->cmdq.rptr = &msgq->rx.readPtr;
	gsp->msgq.cnt = cmdq->tx.msgCount;
	gsp->msgq.wptr = &msgq->tx.writePtr;
	gsp->msgq.rptr = &cmdq->rx.readPtr;
	return 0;
}

static int
r535_gsp_rmargs_init(struct nvkm_gsp *gsp, bool resume)
{
	GSP_ARGUMENTS_CACHED *args;
	int ret;

	if (!resume) {
		ret = r535_gsp_shared_init(gsp);
		if (ret)
			return ret;

		ret = nvkm_gsp_mem_ctor(gsp, 0x1000, &gsp->rmargs);
		if (ret)
			return ret;
	}

	args = gsp->rmargs.data;
	args->messageQueueInitArguments.sharedMemPhysAddr = gsp->shm.mem.addr;
	args->messageQueueInitArguments.pageTableEntryCount = gsp->shm.ptes.nr;
	args->messageQueueInitArguments.cmdQueueOffset =
		(u8 *)gsp->shm.cmdq.ptr - (u8 *)gsp->shm.mem.data;
	args->messageQueueInitArguments.statQueueOffset =
		(u8 *)gsp->shm.msgq.ptr - (u8 *)gsp->shm.mem.data;

	if (!resume) {
		args->srInitArguments.oldLevel = 0;
		args->srInitArguments.flags = 0;
		args->srInitArguments.bInPMTransition = 0;
	} else {
		args->srInitArguments.oldLevel = NV2080_CTRL_GPU_SET_POWER_STATE_GPU_LEVEL_3;
		args->srInitArguments.flags = 0;
		args->srInitArguments.bInPMTransition = 1;
	}

	return 0;
}

static inline u64
r535_gsp_libos_id8(const char *name)
{
	u64 id = 0;

	for (int i = 0; i < sizeof(id) && *name; i++, name++)
		id = (id << 8) | *name;

	return id;
}

/**
 * create_pte_array() - creates a PTE array of a physically contiguous buffer
 * @ptes: pointer to the array
 * @addr: base address of physically contiguous buffer (GSP_PAGE_SIZE aligned)
 * @size: size of the buffer
 *
 * GSP-RM sometimes expects physically-contiguous buffers to have an array of
 * "PTEs" for each page in that buffer.  Although in theory that allows for
 * the buffer to be physically discontiguous, GSP-RM does not currently
 * support that.
 *
 * In this case, the PTEs are DMA addresses of each page of the buffer.  Since
 * the buffer is physically contiguous, calculating all the PTEs is simple
 * math.
 *
 * See memdescGetPhysAddrsForGpu()
 */
static void create_pte_array(u64 *ptes, dma_addr_t addr, size_t size)
{
	unsigned int num_pages = DIV_ROUND_UP_ULL(size, GSP_PAGE_SIZE);
	unsigned int i;

	for (i = 0; i < num_pages; i++)
		ptes[i] = (u64)addr + (i << GSP_PAGE_SHIFT);
}

/**
 * r535_gsp_libos_init() -- create the libos arguments structure
 *
 * The logging buffers are byte queues that contain encoded printf-like
 * messages from GSP-RM.  They need to be decoded by a special application
 * that can parse the buffers.
 *
 * The 'loginit' buffer contains logs from early GSP-RM init and
 * exception dumps.  The 'logrm' buffer contains the subsequent logs. Both are
 * written to directly by GSP-RM and can be any multiple of GSP_PAGE_SIZE.
 *
 * The physical address map for the log buffer is stored in the buffer
 * itself, starting with offset 1. Offset 0 contains the "put" pointer.
 *
 * The GSP only understands 4K pages (GSP_PAGE_SIZE), so even if the kernel is
 * configured for a larger page size (e.g. 64K pages), we need to give
 * the GSP an array of 4K pages. Fortunately, since the buffer is
 * physically contiguous, it's simple math to calculate the addresses.
 *
 * The buffers must be a multiple of GSP_PAGE_SIZE.  GSP-RM also currently
 * ignores the @kind field for LOGINIT, LOGINTR, and LOGRM, but expects the
 * buffers to be physically contiguous anyway.
 *
 * The memory allocated for the arguments must remain until the GSP sends the
 * init_done RPC.
 *
 * See _kgspInitLibosLoggingStructures (allocates memory for buffers)
 * See kgspSetupLibosInitArgs_IMPL (creates pLibosInitArgs[] array)
 */
static int
r535_gsp_libos_init(struct nvkm_gsp *gsp)
{
	LibosMemoryRegionInitArgument *args;
	int ret;

	ret = nvkm_gsp_mem_ctor(gsp, 0x1000, &gsp->libos);
	if (ret)
		return ret;

	args = gsp->libos.data;

	ret = nvkm_gsp_mem_ctor(gsp, 0x10000, &gsp->loginit);
	if (ret)
		return ret;

	args[0].id8  = r535_gsp_libos_id8("LOGINIT");
	args[0].pa   = gsp->loginit.addr;
	args[0].size = gsp->loginit.size;
	args[0].kind = LIBOS_MEMORY_REGION_CONTIGUOUS;
	args[0].loc  = LIBOS_MEMORY_REGION_LOC_SYSMEM;
	create_pte_array(gsp->loginit.data + sizeof(u64), gsp->loginit.addr, gsp->loginit.size);

	ret = nvkm_gsp_mem_ctor(gsp, 0x10000, &gsp->logintr);
	if (ret)
		return ret;

	args[1].id8  = r535_gsp_libos_id8("LOGINTR");
	args[1].pa   = gsp->logintr.addr;
	args[1].size = gsp->logintr.size;
	args[1].kind = LIBOS_MEMORY_REGION_CONTIGUOUS;
	args[1].loc  = LIBOS_MEMORY_REGION_LOC_SYSMEM;
	create_pte_array(gsp->logintr.data + sizeof(u64), gsp->logintr.addr, gsp->logintr.size);

	ret = nvkm_gsp_mem_ctor(gsp, 0x10000, &gsp->logrm);
	if (ret)
		return ret;

	args[2].id8  = r535_gsp_libos_id8("LOGRM");
	args[2].pa   = gsp->logrm.addr;
	args[2].size = gsp->logrm.size;
	args[2].kind = LIBOS_MEMORY_REGION_CONTIGUOUS;
	args[2].loc  = LIBOS_MEMORY_REGION_LOC_SYSMEM;
	create_pte_array(gsp->logrm.data + sizeof(u64), gsp->logrm.addr, gsp->logrm.size);

	ret = r535_gsp_rmargs_init(gsp, false);
	if (ret)
		return ret;

	args[3].id8  = r535_gsp_libos_id8("RMARGS");
	args[3].pa   = gsp->rmargs.addr;
	args[3].size = gsp->rmargs.size;
	args[3].kind = LIBOS_MEMORY_REGION_CONTIGUOUS;
	args[3].loc  = LIBOS_MEMORY_REGION_LOC_SYSMEM;
	return 0;
}

void
nvkm_gsp_sg_free(struct nvkm_device *device, struct sg_table *sgt)
{
	struct scatterlist *sgl;
	int i;

	dma_unmap_sgtable(device->dev, sgt, DMA_BIDIRECTIONAL, 0);

	for_each_sgtable_sg(sgt, sgl, i) {
		struct page *page = sg_page(sgl);

		__free_page(page);
	}

	sg_free_table(sgt);
}

int
nvkm_gsp_sg(struct nvkm_device *device, u64 size, struct sg_table *sgt)
{
	const u64 pages = DIV_ROUND_UP(size, PAGE_SIZE);
	struct scatterlist *sgl;
	int ret, i;

	ret = sg_alloc_table(sgt, pages, GFP_KERNEL);
	if (ret)
		return ret;

	for_each_sgtable_sg(sgt, sgl, i) {
		struct page *page = alloc_page(GFP_KERNEL);

		if (!page) {
			nvkm_gsp_sg_free(device, sgt);
			return -ENOMEM;
		}

		sg_set_page(sgl, page, PAGE_SIZE, 0);
	}

	ret = dma_map_sgtable(device->dev, sgt, DMA_BIDIRECTIONAL, 0);
	if (ret)
		nvkm_gsp_sg_free(device, sgt);

	return ret;
}

static void
nvkm_gsp_radix3_dtor(struct nvkm_gsp *gsp, struct nvkm_gsp_radix3 *rx3)
{
	for (int i = ARRAY_SIZE(rx3->mem) - 1; i >= 0; i--)
		nvkm_gsp_mem_dtor(gsp, &rx3->mem[i]);
}

/**
 * nvkm_gsp_radix3_sg - build a radix3 table from a S/G list
 *
 * The GSP uses a three-level page table, called radix3, to map the firmware.
 * Each 64-bit "pointer" in the table is either the bus address of an entry in
 * the next table (for levels 0 and 1) or the bus address of the next page in
 * the GSP firmware image itself.
 *
 * Level 0 contains a single entry in one page that points to the first page
 * of level 1.
 *
 * Level 1, since it's also only one page in size, contains up to 512 entries,
 * one for each page in Level 2.
 *
 * Level 2 can be up to 512 pages in size, and each of those entries points to
 * the next page of the firmware image.  Since there can be up to 512*512
 * pages, that limits the size of the firmware to 512*512*GSP_PAGE_SIZE = 1GB.
 *
 * Internally, the GSP has its window into system memory, but the base
 * physical address of the aperture is not 0.  In fact, it varies depending on
 * the GPU architecture.  Since the GPU is a PCI device, this window is
 * accessed via DMA and is therefore bound by IOMMU translation.  The end
 * result is that GSP-RM must translate the bus addresses in the table to GSP
 * physical addresses.  All this should happen transparently.
 *
 * Returns 0 on success, or negative error code
 *
 * See kgspCreateRadix3_IMPL
 */
static int
nvkm_gsp_radix3_sg(struct nvkm_gsp *gsp, struct sg_table *sgt, u64 size,
		   struct nvkm_gsp_radix3 *rx3)
{
	u64 addr;

	for (int i = ARRAY_SIZE(rx3->mem) - 1; i >= 0; i--) {
		u64 *ptes;
		size_t bufsize;
		int ret, idx;

		bufsize = ALIGN((size / GSP_PAGE_SIZE) * sizeof(u64), GSP_PAGE_SIZE);
		ret = nvkm_gsp_mem_ctor(gsp, bufsize, &rx3->mem[i]);
		if (ret)
			return ret;

		ptes = rx3->mem[i].data;
		if (i == 2) {
			struct scatterlist *sgl;

			for_each_sgtable_dma_sg(sgt, sgl, idx) {
				for (int j = 0; j < sg_dma_len(sgl) / GSP_PAGE_SIZE; j++)
					*ptes++ = sg_dma_address(sgl) + (GSP_PAGE_SIZE * j);
			}
		} else {
			for (int j = 0; j < size / GSP_PAGE_SIZE; j++)
				*ptes++ = addr + GSP_PAGE_SIZE * j;
		}

		size = rx3->mem[i].size;
		addr = rx3->mem[i].addr;
	}

	return 0;
}

int
r535_gsp_fini(struct nvkm_gsp *gsp, bool suspend)
{
	u32 mbox0 = 0xff, mbox1 = 0xff;
	int ret;

	if (!gsp->running)
		return 0;

	if (suspend) {
		GspFwWprMeta *meta = gsp->wpr_meta.data;
		u64 len = meta->gspFwWprEnd - meta->gspFwWprStart;
		GspFwSRMeta *sr;

		ret = nvkm_gsp_sg(gsp->subdev.device, len, &gsp->sr.sgt);
		if (ret)
			return ret;

		ret = nvkm_gsp_radix3_sg(gsp, &gsp->sr.sgt, len, &gsp->sr.radix3);
		if (ret)
			return ret;

		ret = nvkm_gsp_mem_ctor(gsp, sizeof(*sr), &gsp->sr.meta);
		if (ret)
			return ret;

		sr = gsp->sr.meta.data;
		sr->magic = GSP_FW_SR_META_MAGIC;
		sr->revision = GSP_FW_SR_META_REVISION;
		sr->sysmemAddrOfSuspendResumeData = gsp->sr.radix3.mem[0].addr;
		sr->sizeOfSuspendResumeData = len;

		mbox0 = lower_32_bits(gsp->sr.meta.addr);
		mbox1 = upper_32_bits(gsp->sr.meta.addr);
	}

	ret = r535_gsp_rpc_unloading_guest_driver(gsp, suspend);
	if (WARN_ON(ret))
		return ret;

	nvkm_msec(gsp->subdev.device, 2000,
		if (nvkm_falcon_rd32(&gsp->falcon, 0x040) & 0x80000000)
			break;
	);

	nvkm_falcon_reset(&gsp->falcon);

	ret = nvkm_gsp_fwsec_sb(gsp);
	WARN_ON(ret);

	ret = r535_gsp_booter_unload(gsp, mbox0, mbox1);
	WARN_ON(ret);

	gsp->running = false;
	return 0;
}

int
r535_gsp_init(struct nvkm_gsp *gsp)
{
	u32 mbox0, mbox1;
	int ret;

	if (!gsp->sr.meta.data) {
		mbox0 = lower_32_bits(gsp->wpr_meta.addr);
		mbox1 = upper_32_bits(gsp->wpr_meta.addr);
	} else {
		r535_gsp_rmargs_init(gsp, true);

		mbox0 = lower_32_bits(gsp->sr.meta.addr);
		mbox1 = upper_32_bits(gsp->sr.meta.addr);
	}

	/* Execute booter to handle (eventually...) booting GSP-RM. */
	ret = r535_gsp_booter_load(gsp, mbox0, mbox1);
	if (WARN_ON(ret))
		goto done;

	ret = r535_gsp_rpc_poll(gsp, NV_VGPU_MSG_EVENT_GSP_INIT_DONE);
	if (ret)
		goto done;

	gsp->running = true;

done:
	if (gsp->sr.meta.data) {
		nvkm_gsp_mem_dtor(gsp, &gsp->sr.meta);
		nvkm_gsp_radix3_dtor(gsp, &gsp->sr.radix3);
		nvkm_gsp_sg_free(gsp->subdev.device, &gsp->sr.sgt);
		return ret;
	}

	if (ret == 0)
		ret = r535_gsp_postinit(gsp);

	return ret;
}

static int
r535_gsp_rm_boot_ctor(struct nvkm_gsp *gsp)
{
	const struct firmware *fw = gsp->fws.bl;
	const struct nvfw_bin_hdr *hdr;
	RM_RISCV_UCODE_DESC *desc;
	int ret;

	hdr = nvfw_bin_hdr(&gsp->subdev, fw->data);
	desc = (void *)fw->data + hdr->header_offset;

	ret = nvkm_gsp_mem_ctor(gsp, hdr->data_size, &gsp->boot.fw);
	if (ret)
		return ret;

	memcpy(gsp->boot.fw.data, fw->data + hdr->data_offset, hdr->data_size);

	gsp->boot.code_offset = desc->monitorCodeOffset;
	gsp->boot.data_offset = desc->monitorDataOffset;
	gsp->boot.manifest_offset = desc->manifestOffset;
	gsp->boot.app_version = desc->appVersion;
	return 0;
}

static const struct nvkm_firmware_func
r535_gsp_fw = {
	.type = NVKM_FIRMWARE_IMG_SGT,
};

static int
r535_gsp_elf_section(struct nvkm_gsp *gsp, const char *name, const u8 **pdata, u64 *psize)
{
	const u8 *img = gsp->fws.rm->data;
	const struct elf64_hdr *ehdr = (const struct elf64_hdr *)img;
	const struct elf64_shdr *shdr = (const struct elf64_shdr *)&img[ehdr->e_shoff];
	const char *names = &img[shdr[ehdr->e_shstrndx].sh_offset];

	for (int i = 0; i < ehdr->e_shnum; i++, shdr++) {
		if (!strcmp(&names[shdr->sh_name], name)) {
			*pdata = &img[shdr->sh_offset];
			*psize = shdr->sh_size;
			return 0;
		}
	}

	nvkm_error(&gsp->subdev, "section '%s' not found\n", name);
	return -ENOENT;
}

static void
r535_gsp_dtor_fws(struct nvkm_gsp *gsp)
{
	nvkm_firmware_put(gsp->fws.bl);
	gsp->fws.bl = NULL;
	nvkm_firmware_put(gsp->fws.booter.unload);
	gsp->fws.booter.unload = NULL;
	nvkm_firmware_put(gsp->fws.booter.load);
	gsp->fws.booter.load = NULL;
	nvkm_firmware_put(gsp->fws.rm);
	gsp->fws.rm = NULL;
}

void
r535_gsp_dtor(struct nvkm_gsp *gsp)
{
	idr_destroy(&gsp->client_id.idr);
	mutex_destroy(&gsp->client_id.mutex);

	nvkm_gsp_radix3_dtor(gsp, &gsp->radix3);
	nvkm_gsp_mem_dtor(gsp, &gsp->sig);
	nvkm_firmware_dtor(&gsp->fw);

	nvkm_falcon_fw_dtor(&gsp->booter.unload);
	nvkm_falcon_fw_dtor(&gsp->booter.load);

	mutex_destroy(&gsp->msgq.mutex);
	mutex_destroy(&gsp->cmdq.mutex);

	r535_gsp_dtor_fws(gsp);

	nvkm_gsp_mem_dtor(gsp, &gsp->shm.mem);
	nvkm_gsp_mem_dtor(gsp, &gsp->loginit);
	nvkm_gsp_mem_dtor(gsp, &gsp->logintr);
	nvkm_gsp_mem_dtor(gsp, &gsp->logrm);
}

int
r535_gsp_oneinit(struct nvkm_gsp *gsp)
{
	struct nvkm_device *device = gsp->subdev.device;
	const u8 *data;
	u64 size;
	int ret;

	mutex_init(&gsp->cmdq.mutex);
	mutex_init(&gsp->msgq.mutex);

	ret = gsp->func->booter.ctor(gsp, "booter-load", gsp->fws.booter.load,
				     &device->sec2->falcon, &gsp->booter.load);
	if (ret)
		return ret;

	ret = gsp->func->booter.ctor(gsp, "booter-unload", gsp->fws.booter.unload,
				     &device->sec2->falcon, &gsp->booter.unload);
	if (ret)
		return ret;

	/* Load GSP firmware from ELF image into DMA-accessible memory. */
	ret = r535_gsp_elf_section(gsp, ".fwimage", &data, &size);
	if (ret)
		return ret;

	ret = nvkm_firmware_ctor(&r535_gsp_fw, "gsp-rm", device, data, size, &gsp->fw);
	if (ret)
		return ret;

	/* Load relevant signature from ELF image. */
	ret = r535_gsp_elf_section(gsp, gsp->func->sig_section, &data, &size);
	if (ret)
		return ret;

	ret = nvkm_gsp_mem_ctor(gsp, ALIGN(size, 256), &gsp->sig);
	if (ret)
		return ret;

	memcpy(gsp->sig.data, data, size);

	/* Build radix3 page table for ELF image. */
	ret = nvkm_gsp_radix3_sg(gsp, &gsp->fw.mem.sgt, gsp->fw.len, &gsp->radix3);
	if (ret)
		return ret;

	r535_gsp_msg_ntfy_add(gsp, NV_VGPU_MSG_EVENT_GSP_RUN_CPU_SEQUENCER,
			      r535_gsp_msg_run_cpu_sequencer, gsp);
	r535_gsp_msg_ntfy_add(gsp, NV_VGPU_MSG_EVENT_POST_EVENT, r535_gsp_msg_post_event, gsp);
	r535_gsp_msg_ntfy_add(gsp, NV_VGPU_MSG_EVENT_RC_TRIGGERED,
			      r535_gsp_msg_rc_triggered, gsp);
	r535_gsp_msg_ntfy_add(gsp, NV_VGPU_MSG_EVENT_MMU_FAULT_QUEUED,
			      r535_gsp_msg_mmu_fault_queued, gsp);
	r535_gsp_msg_ntfy_add(gsp, NV_VGPU_MSG_EVENT_OS_ERROR_LOG, r535_gsp_msg_os_error_log, gsp);
	r535_gsp_msg_ntfy_add(gsp, NV_VGPU_MSG_EVENT_PERF_BRIDGELESS_INFO_UPDATE, NULL, NULL);
	r535_gsp_msg_ntfy_add(gsp, NV_VGPU_MSG_EVENT_UCODE_LIBOS_PRINT, NULL, NULL);
	r535_gsp_msg_ntfy_add(gsp, NV_VGPU_MSG_EVENT_GSP_SEND_USER_SHARED_DATA, NULL, NULL);
	ret = r535_gsp_rm_boot_ctor(gsp);
	if (ret)
		return ret;

	/* Release FW images - we've copied them to DMA buffers now. */
	r535_gsp_dtor_fws(gsp);

	/* Calculate FB layout. */
	gsp->fb.wpr2.frts.size = 0x100000;
	gsp->fb.wpr2.frts.addr = ALIGN_DOWN(gsp->fb.bios.addr, 0x20000) - gsp->fb.wpr2.frts.size;

	gsp->fb.wpr2.boot.size = gsp->boot.fw.size;
	gsp->fb.wpr2.boot.addr = ALIGN_DOWN(gsp->fb.wpr2.frts.addr - gsp->fb.wpr2.boot.size, 0x1000);

	gsp->fb.wpr2.elf.size = gsp->fw.len;
	gsp->fb.wpr2.elf.addr = ALIGN_DOWN(gsp->fb.wpr2.boot.addr - gsp->fb.wpr2.elf.size, 0x10000);

	{
		u32 fb_size_gb = DIV_ROUND_UP_ULL(gsp->fb.size, 1 << 30);

		gsp->fb.wpr2.heap.size =
			gsp->func->wpr_heap.os_carveout_size +
			gsp->func->wpr_heap.base_size +
			ALIGN(GSP_FW_HEAP_PARAM_SIZE_PER_GB_FB * fb_size_gb, 1 << 20) +
			ALIGN(GSP_FW_HEAP_PARAM_CLIENT_ALLOC_SIZE, 1 << 20);

		gsp->fb.wpr2.heap.size = max(gsp->fb.wpr2.heap.size, gsp->func->wpr_heap.min_size);
	}

	gsp->fb.wpr2.heap.addr = ALIGN_DOWN(gsp->fb.wpr2.elf.addr - gsp->fb.wpr2.heap.size, 0x100000);
	gsp->fb.wpr2.heap.size = ALIGN_DOWN(gsp->fb.wpr2.elf.addr - gsp->fb.wpr2.heap.addr, 0x100000);

	gsp->fb.wpr2.addr = ALIGN_DOWN(gsp->fb.wpr2.heap.addr - sizeof(GspFwWprMeta), 0x100000);
	gsp->fb.wpr2.size = gsp->fb.wpr2.frts.addr + gsp->fb.wpr2.frts.size - gsp->fb.wpr2.addr;

	gsp->fb.heap.size = 0x100000;
	gsp->fb.heap.addr = gsp->fb.wpr2.addr - gsp->fb.heap.size;

	ret = nvkm_gsp_fwsec_frts(gsp);
	if (WARN_ON(ret))
		return ret;

	ret = r535_gsp_libos_init(gsp);
	if (WARN_ON(ret))
		return ret;

	ret = r535_gsp_wpr_meta_init(gsp);
	if (WARN_ON(ret))
		return ret;

	ret = r535_gsp_rpc_set_system_info(gsp);
	if (WARN_ON(ret))
		return ret;

	ret = r535_gsp_rpc_set_registry(gsp);
	if (WARN_ON(ret))
		return ret;

	/* Reset GSP into RISC-V mode. */
	ret = gsp->func->reset(gsp);
	if (WARN_ON(ret))
		return ret;

	nvkm_falcon_wr32(&gsp->falcon, 0x040, lower_32_bits(gsp->libos.addr));
	nvkm_falcon_wr32(&gsp->falcon, 0x044, upper_32_bits(gsp->libos.addr));

	mutex_init(&gsp->client_id.mutex);
	idr_init(&gsp->client_id.idr);
	return 0;
}

static int
r535_gsp_load_fw(struct nvkm_gsp *gsp, const char *name, const char *ver,
		 const struct firmware **pfw)
{
	char fwname[64];

	snprintf(fwname, sizeof(fwname), "gsp/%s-%s", name, ver);
	return nvkm_firmware_get(&gsp->subdev, fwname, 0, pfw);
}

int
r535_gsp_load(struct nvkm_gsp *gsp, int ver, const struct nvkm_gsp_fwif *fwif)
{
	struct nvkm_subdev *subdev = &gsp->subdev;
	int ret;
	bool enable_gsp = fwif->enable;

#if IS_ENABLED(CONFIG_DRM_NOUVEAU_GSP_DEFAULT)
	enable_gsp = true;
#endif
	if (!nvkm_boolopt(subdev->device->cfgopt, "NvGspRm", enable_gsp))
		return -EINVAL;

	if ((ret = r535_gsp_load_fw(gsp, "gsp", fwif->ver, &gsp->fws.rm)) ||
	    (ret = r535_gsp_load_fw(gsp, "booter_load", fwif->ver, &gsp->fws.booter.load)) ||
	    (ret = r535_gsp_load_fw(gsp, "booter_unload", fwif->ver, &gsp->fws.booter.unload)) ||
	    (ret = r535_gsp_load_fw(gsp, "bootloader", fwif->ver, &gsp->fws.bl))) {
		r535_gsp_dtor_fws(gsp);
		return ret;
	}

	return 0;
}

#define NVKM_GSP_FIRMWARE(chip)                                  \
MODULE_FIRMWARE("nvidia/"#chip"/gsp/booter_load-535.113.01.bin");   \
MODULE_FIRMWARE("nvidia/"#chip"/gsp/booter_unload-535.113.01.bin"); \
MODULE_FIRMWARE("nvidia/"#chip"/gsp/bootloader-535.113.01.bin");    \
MODULE_FIRMWARE("nvidia/"#chip"/gsp/gsp-535.113.01.bin")

NVKM_GSP_FIRMWARE(tu102);
NVKM_GSP_FIRMWARE(tu104);
NVKM_GSP_FIRMWARE(tu106);

NVKM_GSP_FIRMWARE(tu116);
NVKM_GSP_FIRMWARE(tu117);

NVKM_GSP_FIRMWARE(ga100);

NVKM_GSP_FIRMWARE(ga102);
NVKM_GSP_FIRMWARE(ga103);
NVKM_GSP_FIRMWARE(ga104);
NVKM_GSP_FIRMWARE(ga106);
NVKM_GSP_FIRMWARE(ga107);

NVKM_GSP_FIRMWARE(ad102);
NVKM_GSP_FIRMWARE(ad103);
NVKM_GSP_FIRMWARE(ad104);
NVKM_GSP_FIRMWARE(ad106);
NVKM_GSP_FIRMWARE(ad107);
