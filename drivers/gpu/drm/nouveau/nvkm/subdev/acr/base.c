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

static struct nvkm_acr_lsf *
nvkm_acr_falcon(struct nvkm_device *device)
{
	struct nvkm_acr *acr = device->acr;
	struct nvkm_acr_lsf *lsf;

	if (acr) {
		list_for_each_entry(lsf, &acr->lsf, head) {
			if (lsf->func->bootstrap_falcon)
				return lsf;
		}
	}

	return NULL;
}

int
nvkm_acr_bootstrap_falcons(struct nvkm_device *device, unsigned long mask)
{
	struct nvkm_acr_lsf *acrflcn = nvkm_acr_falcon(device);
	unsigned long id;

	if (!acrflcn)
		return -ENOSYS;

	if (acrflcn->func->bootstrap_multiple_falcons) {
		return acrflcn->func->
			bootstrap_multiple_falcons(acrflcn->falcon, mask);
	}

	for_each_set_bit(id, &mask, NVKM_ACR_LSF_NUM) {
		int ret = acrflcn->func->bootstrap_falcon(acrflcn->falcon, id);
		if (ret)
			return ret;
	}

	return 0;
}

int
nvkm_acr_boot_ls_falcons(struct nvkm_device *device)
{
	struct nvkm_acr *acr = device->acr;
	struct nvkm_acr_lsf *lsf;
	int ret;

	if (!acr)
		return -ENOSYS;

	list_for_each_entry(lsf, &acr->lsf, head) {
		if (lsf->func->boot) {
			ret = lsf->func->boot(lsf->falcon);
			if (ret)
				break;
		}
	}

	return ret;
}

static void
nvkm_acr_cleanup(struct nvkm_acr *acr)
{
	nvkm_acr_lsfw_del_all(acr);
}

static int
nvkm_acr_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_acr *acr = nvkm_acr(subdev);
	struct nvkm_acr_lsfw *lsfw;
	struct nvkm_acr_lsf *lsf;

	list_for_each_entry(lsfw, &acr->lsfw, head) {
		if (!(lsf = kmalloc(sizeof(*lsf), GFP_KERNEL)))
			return -ENOMEM;
		lsf->func = lsfw->func;
		lsf->falcon = lsfw->falcon;
		lsf->id = lsfw->id;
		list_add_tail(&lsf->head, &acr->lsf);
	}

	nvkm_acr_cleanup(acr);
	return 0;
}

static void *
nvkm_acr_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_acr *acr = nvkm_acr(subdev);
	struct nvkm_acr_lsf *lsf, *lst;

	list_for_each_entry_safe(lsf, lst, &acr->lsf, head) {
		list_del(&lsf->head);
		kfree(lsf);
	}

	nvkm_acr_cleanup(acr);
	return acr;
}

static const struct nvkm_subdev_func
nvkm_acr = {
	.dtor = nvkm_acr_dtor,
	.oneinit = nvkm_acr_oneinit,
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
	INIT_LIST_HEAD(&acr->lsf);

	fwif = nvkm_firmware_load(&acr->subdev, fwif, "Acr", acr);
	if (IS_ERR(fwif))
		return PTR_ERR(fwif);

	acr->func = fwif->func;
	return 0;
}
