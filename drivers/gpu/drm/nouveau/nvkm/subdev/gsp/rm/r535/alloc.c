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

#include "nvrm/alloc.h"
#include "nvrm/rpcfn.h"

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
	return nvkm_gsp_rpc_wr(gsp, rpc, NVKM_GSP_RPC_REPLY_RECV);
}

static void
r535_gsp_rpc_rm_alloc_done(struct nvkm_gsp_object *object, void *params)
{
	rpc_gsp_rm_alloc_v03_00 *rpc = to_payload_hdr(params, rpc);

	nvkm_gsp_rpc_done(object->client->gsp, rpc);
}

static void *
r535_gsp_rpc_rm_alloc_push(struct nvkm_gsp_object *object, void *params)
{
	rpc_gsp_rm_alloc_v03_00 *rpc = to_payload_hdr(params, rpc);
	struct nvkm_gsp *gsp = object->client->gsp;
	void *ret = NULL;

	rpc = nvkm_gsp_rpc_push(gsp, rpc, NVKM_GSP_RPC_REPLY_RECV, sizeof(*rpc));
	if (IS_ERR_OR_NULL(rpc))
		return rpc;

	if (rpc->status) {
		ret = ERR_PTR(r535_rpc_status_to_errno(rpc->status));
		if (PTR_ERR(ret) != -EAGAIN && PTR_ERR(ret) != -EBUSY)
			nvkm_error(&gsp->subdev, "RM_ALLOC: 0x%x\n", rpc->status);
	}

	nvkm_gsp_rpc_done(gsp, rpc);

	return ret;
}

static void *
r535_gsp_rpc_rm_alloc_get(struct nvkm_gsp_object *object, u32 oclass,
			  u32 params_size)
{
	struct nvkm_gsp_client *client = object->client;
	struct nvkm_gsp *gsp = client->gsp;
	rpc_gsp_rm_alloc_v03_00 *rpc;

	nvkm_debug(&gsp->subdev, "cli:0x%08x obj:0x%08x new obj:0x%08x\n",
		   client->object.handle, object->parent->handle,
		   object->handle);

	nvkm_debug(&gsp->subdev, "cls:0x%08x params_size:%d\n", oclass,
		   params_size);

	rpc = nvkm_gsp_rpc_get(gsp, NV_VGPU_MSG_FUNCTION_GSP_RM_ALLOC,
			       sizeof(*rpc) + params_size);
	if (IS_ERR(rpc))
		return rpc;

	rpc->hClient = client->object.handle;
	rpc->hParent = object->parent->handle;
	rpc->hObject = object->handle;
	rpc->hClass = oclass;
	rpc->status = 0;
	rpc->paramsSize = params_size;
	return rpc->params;
}

const struct nvkm_rm_api_alloc
r535_alloc = {
	.get = r535_gsp_rpc_rm_alloc_get,
	.push = r535_gsp_rpc_rm_alloc_push,
	.done = r535_gsp_rpc_rm_alloc_done,
	.free = r535_gsp_rpc_rm_free,
};
