/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include <rm/rm.h>

#include "nvrm/client.h"

static int
r570_gsp_client_ctor(struct nvkm_gsp_client *client, u32 handle)
{
	NV0000_ALLOC_PARAMETERS *args;

	args = nvkm_gsp_rm_alloc_get(&client->object, handle, NV01_ROOT, sizeof(*args),
				     &client->object);
	if (IS_ERR(args))
		return PTR_ERR(args);

	args->hClient = client->object.handle;
	args->processID = ~0;

	return nvkm_gsp_rm_alloc_wr(&client->object, args);
}

const struct nvkm_rm_api_client
r570_client = {
	.ctor = r570_gsp_client_ctor,
};
