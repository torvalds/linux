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
#include <rm/engine.h>

#include "nvrm/ce.h"
#include "nvrm/engine.h"

static int
r535_ce_alloc(struct nvkm_gsp_object *chan, u32 handle, u32 class, int inst,
	      struct nvkm_gsp_object *ce)
{
	NVC0B5_ALLOCATION_PARAMETERS *args;

	args = nvkm_gsp_rm_alloc_get(chan, handle, class, sizeof(*args), ce);
	if (WARN_ON(IS_ERR(args)))
		return PTR_ERR(args);

	args->version = 1;
	args->engineType = NV2080_ENGINE_TYPE_COPY0 + inst;

	return nvkm_gsp_rm_alloc_wr(ce, args);
}

const struct nvkm_rm_api_engine
r535_ce = {
	.alloc = r535_ce_alloc,
};
