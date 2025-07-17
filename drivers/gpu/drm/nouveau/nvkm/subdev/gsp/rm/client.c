/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "rm.h"

void
nvkm_gsp_client_dtor(struct nvkm_gsp_client *client)
{
	const unsigned int id = client->object.handle - NVKM_RM_CLIENT(0);
	struct nvkm_gsp *gsp = client->gsp;

	if (!gsp)
		return;

	if (client->object.client)
		nvkm_gsp_rm_free(&client->object);

	mutex_lock(&gsp->client_id.mutex);
	idr_remove(&gsp->client_id.idr, id);
	mutex_unlock(&gsp->client_id.mutex);

	client->gsp = NULL;
}

int
nvkm_gsp_client_ctor(struct nvkm_gsp *gsp, struct nvkm_gsp_client *client)
{
	int id, ret;

	if (WARN_ON(!gsp->rm))
		return -ENOSYS;

	mutex_lock(&gsp->client_id.mutex);
	id = idr_alloc(&gsp->client_id.idr, client, 0, NVKM_RM_CLIENT_MASK + 1, GFP_KERNEL);
	mutex_unlock(&gsp->client_id.mutex);
	if (id < 0)
		return id;

	client->gsp = gsp;
	client->object.client = client;
	INIT_LIST_HEAD(&client->events);

	ret = gsp->rm->api->client->ctor(client, NVKM_RM_CLIENT(id));
	if (ret)
		nvkm_gsp_client_dtor(client);

	return ret;
}
