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
#include "priv.h"

MODULE_FIRMWARE("nvidia/gp102/acr/unload_bl.bin");
MODULE_FIRMWARE("nvidia/gp102/acr/ucode_unload.bin");

MODULE_FIRMWARE("nvidia/gp104/acr/unload_bl.bin");
MODULE_FIRMWARE("nvidia/gp104/acr/ucode_unload.bin");

MODULE_FIRMWARE("nvidia/gp106/acr/unload_bl.bin");
MODULE_FIRMWARE("nvidia/gp106/acr/ucode_unload.bin");

MODULE_FIRMWARE("nvidia/gp107/acr/unload_bl.bin");
MODULE_FIRMWARE("nvidia/gp107/acr/ucode_unload.bin");

MODULE_FIRMWARE("nvidia/gp102/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp102/acr/ucode_load.bin");

MODULE_FIRMWARE("nvidia/gp104/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp104/acr/ucode_load.bin");

MODULE_FIRMWARE("nvidia/gp106/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp106/acr/ucode_load.bin");

MODULE_FIRMWARE("nvidia/gp107/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp107/acr/ucode_load.bin");

static const struct nvkm_acr_func
gp102_acr = {
};

int
gp102_acr_load(struct nvkm_acr *acr, int ver, const struct nvkm_acr_fwif *fwif)
{
	return 0;
}

static const struct nvkm_acr_fwif
gp102_acr_fwif[] = {
	{ 0, gp102_acr_load, &gp102_acr },
	{}
};

int
gp102_acr_new(struct nvkm_device *device, int index, struct nvkm_acr **pacr)
{
	return nvkm_acr_new_(gp102_acr_fwif, device, index, pacr);
}
