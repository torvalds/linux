/*
 * Copyright 2019 Red Hat Inc.
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
#include "gf100.h"

#include <subdev/acr.h>

const struct nvkm_acr_lsf_func
gp108_gr_gpccs_acr = {
};

const struct nvkm_acr_lsf_func
gp108_gr_fecs_acr = {
};

MODULE_FIRMWARE("nvidia/gp108/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/sw_method_init.bin");

static const struct gf100_gr_fwif
gp108_gr_fwif[] = {
	{ 0, gm200_gr_load, &gp107_gr, &gp108_gr_fecs_acr, &gp108_gr_gpccs_acr },
	{}
};

int
gp108_gr_new(struct nvkm_device *device, int index, struct nvkm_gr **pgr)
{
	return gf100_gr_new_(gp108_gr_fwif, device, index, pgr);
}
