/*
 * Copyright 2021 Red Hat Inc.
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

void
ga100_acr_wpr_check(struct nvkm_acr *acr, u64 *start, u64 *limit)
{
	struct nvkm_device *device = acr->subdev.device;

	*start = (u64)(nvkm_rd32(device, 0x1fa81c) & 0xffffff00) << 8;
	*limit = (u64)(nvkm_rd32(device, 0x1fa820) & 0xffffff00) << 8;
	*limit = *limit + 0x20000;
}

int
ga100_acr_hsfw_ctor(struct nvkm_acr *acr, const char *bl, const char *fw,
		    const char *name, int ver, const struct nvkm_acr_hsf_fwif *fwif)
{
	struct nvkm_acr_hsfw *hsfw;

	if (!(hsfw = kzalloc(sizeof(*hsfw), GFP_KERNEL)))
		return -ENOMEM;

	hsfw->falcon_id = fwif->falcon_id;
	hsfw->boot_mbox0 = fwif->boot_mbox0;
	hsfw->intr_clear = fwif->intr_clear;
	list_add_tail(&hsfw->head, &acr->hsfw);

	return nvkm_falcon_fw_ctor_hs_v2(fwif->func, name, &acr->subdev, fw, ver, NULL, &hsfw->fw);
}
