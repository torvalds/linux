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
#include <rm/rpc.h>

#include "nvrm/ctrl.h"
#include "nvrm/rpcfn.h"

static void
r535_gsp_rpc_rm_ctrl_done(struct nvkm_gsp_object *object, void *params)
{
	rpc_gsp_rm_control_v03_00 *rpc = to_payload_hdr(params, rpc);

	if (!params)
		return;
	nvkm_gsp_rpc_done(object->client->gsp, rpc);
}

static int
r535_gsp_rpc_rm_ctrl_push(struct nvkm_gsp_object *object, void **params, u32 repc)
{
	rpc_gsp_rm_control_v03_00 *rpc = to_payload_hdr((*params), rpc);
	struct nvkm_gsp *gsp = object->client->gsp;
	int ret = 0;

	rpc = nvkm_gsp_rpc_push(gsp, rpc, NVKM_GSP_RPC_REPLY_RECV, repc);
	if (IS_ERR_OR_NULL(rpc)) {
		*params = NULL;
		return PTR_ERR(rpc);
	}

	if (rpc->status) {
		ret = r535_rpc_status_to_errno(rpc->status);
		if (ret != -EAGAIN && ret != -EBUSY)
			nvkm_error(&gsp->subdev, "cli:0x%08x obj:0x%08x ctrl cmd:0x%08x failed: 0x%08x\n",
				   object->client->object.handle, object->handle, rpc->cmd, rpc->status);
	}

	if (repc)
		*params = rpc->params;
	else
		nvkm_gsp_rpc_done(gsp, rpc);

	return ret;
}

static void *
r535_gsp_rpc_rm_ctrl_get(struct nvkm_gsp_object *object, u32 cmd, u32 params_size)
{
	struct nvkm_gsp_client *client = object->client;
	struct nvkm_gsp *gsp = client->gsp;
	rpc_gsp_rm_control_v03_00 *rpc;

	nvkm_debug(&gsp->subdev, "cli:0x%08x obj:0x%08x ctrl cmd:0x%08x params_size:%d\n",
		   client->object.handle, object->handle, cmd, params_size);

	rpc = nvkm_gsp_rpc_get(gsp, NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL,
			       sizeof(*rpc) + params_size);
	if (IS_ERR(rpc))
		return rpc;

	rpc->hClient    = client->object.handle;
	rpc->hObject    = object->handle;
	rpc->cmd	= cmd;
	rpc->status     = 0;
	rpc->paramsSize = params_size;
	return rpc->params;
}

const struct nvkm_rm_api_ctrl
r535_ctrl = {
	.get = r535_gsp_rpc_rm_ctrl_get,
	.push = r535_gsp_rpc_rm_ctrl_push,
	.done = r535_gsp_rpc_rm_ctrl_done,
};
