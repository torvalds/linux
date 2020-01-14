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

#include <core/firmware.h>

static void *
nvkm_acr_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_acr *acr = nvkm_acr(subdev);

	nvkm_acr_lsfw_del_all(acr);

	return acr;
}

static const struct nvkm_subdev_func
nvkm_acr = {
	.dtor = nvkm_acr_dtor,
};

int
nvkm_acr_new_(const struct nvkm_acr_fwif *fwif, struct nvkm_device *device,
	      int index, struct nvkm_acr **pacr)
{
	struct nvkm_acr *acr;

	if (!(acr = *pacr = kzalloc(sizeof(*acr), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_subdev_ctor(&nvkm_acr, device, index, &acr->subdev);
	INIT_LIST_HEAD(&acr->lsfw);

	fwif = nvkm_firmware_load(&acr->subdev, fwif, "Acr", acr);
	if (IS_ERR(fwif))
		return PTR_ERR(fwif);

	acr->func = fwif->func;
	return 0;
}
