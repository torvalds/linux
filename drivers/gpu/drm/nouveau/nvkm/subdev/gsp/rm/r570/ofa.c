/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include <rm/engine.h>

#include "nvrm/ofa.h"

static int
r570_ofa_alloc(struct nvkm_gsp_object *parent, u32 handle, u32 oclass, int inst,
	       struct nvkm_gsp_object *ofa)
{
	NV_OFA_ALLOCATION_PARAMETERS *args;

	args = nvkm_gsp_rm_alloc_get(parent, handle, oclass, sizeof(*args), ofa);
	if (WARN_ON(IS_ERR(args)))
		return PTR_ERR(args);

	args->size = sizeof(*args);
	args->engineInstance = inst;

	return nvkm_gsp_rm_alloc_wr(ofa, args);
}

const struct nvkm_rm_api_engine
r570_ofa = {
	.alloc = r570_ofa_alloc,
};
