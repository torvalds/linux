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
#include <core/memory.h>
#include <subdev/mmu.h>
#include <subdev/gsp.h>
#include <subdev/pmu.h>
#include <engine/sec2.h>
#include <engine/nvdec.h>

static struct nvkm_acr_hsfw *
nvkm_acr_hsfw_find(struct nvkm_acr *acr, const char *name)
{
	struct nvkm_acr_hsfw *hsfw;

	list_for_each_entry(hsfw, &acr->hsfw, head) {
		if (!strcmp(hsfw->fw.fw.name, name))
			return hsfw;
	}

	return NULL;
}

int
nvkm_acr_hsfw_boot(struct nvkm_acr *acr, const char *name)
{
	struct nvkm_subdev *subdev = &acr->subdev;
	struct nvkm_acr_hsfw *hsfw;

	hsfw = nvkm_acr_hsfw_find(acr, name);
	if (!hsfw)
		return -EINVAL;

	return nvkm_falcon_fw_boot(&hsfw->fw, subdev, true, NULL, NULL,
				   hsfw->boot_mbox0, hsfw->intr_clear);
}

static struct nvkm_acr_lsf *
nvkm_acr_rtos(struct nvkm_acr *acr)
{
	struct nvkm_acr_lsf *lsf;

	if (acr) {
		list_for_each_entry(lsf, &acr->lsf, head) {
			if (lsf->func->bootstrap_falcon)
				return lsf;
		}
	}

	return NULL;
}

static void
nvkm_acr_unload(struct nvkm_acr *acr)
{
	if (acr->done) {
		if (acr->rtos) {
			nvkm_subdev_unref(acr->rtos->falcon->owner);
			acr->rtos = NULL;
		}

		nvkm_acr_hsfw_boot(acr, "unload");
		acr->done = false;
	}
}

static int
nvkm_acr_load(struct nvkm_acr *acr)
{
	struct nvkm_subdev *subdev = &acr->subdev;
	struct nvkm_acr_lsf *rtos = nvkm_acr_rtos(acr);
	u64 start, limit;
	int ret;

	if (list_empty(&acr->lsf)) {
		nvkm_debug(subdev, "No LSF(s) present.\n");
		return 0;
	}

	ret = acr->func->init(acr);
	if (ret)
		return ret;

	acr->func->wpr_check(acr, &start, &limit);

	if (start != acr->wpr_start || limit != acr->wpr_end) {
		nvkm_error(subdev, "WPR not configured as expected: "
				   "%016llx-%016llx vs %016llx-%016llx\n",
			   acr->wpr_start, acr->wpr_end, start, limit);
		return -EIO;
	}

	acr->done = true;

	if (rtos) {
		ret = nvkm_subdev_ref(rtos->falcon->owner);
		if (ret)
			return ret;

		acr->rtos = rtos;
	}

	return ret;
}

static int
nvkm_acr_reload(struct nvkm_acr *acr)
{
	nvkm_acr_unload(acr);
	return nvkm_acr_load(acr);
}

int
nvkm_acr_bootstrap_falcons(struct nvkm_device *device, unsigned long mask)
{
	struct nvkm_acr *acr = device->acr;
	struct nvkm_acr_lsf *rtos = nvkm_acr_rtos(acr);
	unsigned long id;

	/* If there's no LS FW managing bootstrapping of other LS falcons,
	 * we depend on the HS firmware being able to do it instead.
	 */
	if (!rtos) {
		/* Which isn't possible everywhere... */
		if ((mask & acr->func->bootstrap_falcons) == mask) {
			int ret = nvkm_acr_reload(acr);
			if (ret)
				return ret;

			return acr->done ? 0 : -EINVAL;
		}
		return -ENOSYS;
	}

	if ((mask & rtos->func->bootstrap_falcons) != mask)
		return -ENOSYS;

	if (rtos->func->bootstrap_multiple_falcons)
		return rtos->func->bootstrap_multiple_falcons(rtos->falcon, mask);

	for_each_set_bit(id, &mask, NVKM_ACR_LSF_NUM) {
		int ret = rtos->func->bootstrap_falcon(rtos->falcon, id);
		if (ret)
			return ret;
	}

	return 0;
}

bool
nvkm_acr_managed_falcon(struct nvkm_device *device, enum nvkm_acr_lsf_id id)
{
	struct nvkm_acr *acr = device->acr;

	if (acr) {
		if (acr->managed_falcons & BIT_ULL(id))
			return true;
	}

	return false;
}

static int
nvkm_acr_fini(struct nvkm_subdev *subdev, bool suspend)
{
	if (!subdev->use.enabled)
		return 0;

	nvkm_acr_unload(nvkm_acr(subdev));
	return 0;
}

static int
nvkm_acr_init(struct nvkm_subdev *subdev)
{
	struct nvkm_acr *acr = nvkm_acr(subdev);

	if (!nvkm_acr_rtos(acr))
		return 0;

	return nvkm_acr_load(acr);
}

static void
nvkm_acr_cleanup(struct nvkm_acr *acr)
{
	nvkm_acr_lsfw_del_all(acr);

	nvkm_firmware_put(acr->wpr_fw);
	acr->wpr_fw = NULL;
}

static int
nvkm_acr_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_device *device = subdev->device;
	struct nvkm_acr *acr = nvkm_acr(subdev);
	struct nvkm_acr_hsfw *hsfw;
	struct nvkm_acr_lsfw *lsfw, *lsft;
	struct nvkm_acr_lsf *lsf, *rtos;
	struct nvkm_falcon *falcon;
	u32 wpr_size = 0;
	u64 falcons;
	int ret, i;

	if (list_empty(&acr->hsfw)) {
		nvkm_debug(subdev, "No HSFW(s)\n");
		nvkm_acr_cleanup(acr);
		return 0;
	}

	/* Determine layout/size of WPR image up-front, as we need to know
	 * it to allocate memory before we begin constructing it.
	 */
	list_for_each_entry_safe(lsfw, lsft, &acr->lsfw, head) {
		/* Cull unknown falcons that are present in WPR image. */
		if (acr->wpr_fw) {
			if (!lsfw->func) {
				nvkm_acr_lsfw_del(lsfw);
				continue;
			}

			wpr_size = acr->wpr_fw->size;
		}

		/* Ensure we've fetched falcon configuration. */
		ret = nvkm_falcon_get(lsfw->falcon, subdev);
		if (ret)
			return ret;

		nvkm_falcon_put(lsfw->falcon, subdev);

		if (!(lsf = kmalloc(sizeof(*lsf), GFP_KERNEL)))
			return -ENOMEM;
		lsf->func = lsfw->func;
		lsf->falcon = lsfw->falcon;
		lsf->id = lsfw->id;
		list_add_tail(&lsf->head, &acr->lsf);
		acr->managed_falcons |= BIT_ULL(lsf->id);
	}

	/* Ensure the falcon that'll provide ACR functions is booted first. */
	rtos = nvkm_acr_rtos(acr);
	if (rtos) {
		falcons = rtos->func->bootstrap_falcons;
		list_move(&rtos->head, &acr->lsf);
	} else {
		falcons = acr->func->bootstrap_falcons;
	}

	/* Cull falcons that can't be bootstrapped, or the HSFW can fail to
	 * boot and leave the GPU in a weird state.
	 */
	list_for_each_entry_safe(lsfw, lsft, &acr->lsfw, head) {
		if (!(falcons & BIT_ULL(lsfw->id))) {
			nvkm_warn(subdev, "%s falcon cannot be bootstrapped\n",
				  nvkm_acr_lsf_id(lsfw->id));
			nvkm_acr_lsfw_del(lsfw);
		}
	}

	if (!acr->wpr_fw || acr->wpr_comp)
		wpr_size = acr->func->wpr_layout(acr);

	/* Allocate/Locate WPR + fill ucode blob pointer.
	 *
	 *  dGPU: allocate WPR + shadow blob
	 * Tegra: locate WPR with regs, ensure size is sufficient,
	 *        allocate ucode blob.
	 */
	ret = acr->func->wpr_alloc(acr, wpr_size);
	if (ret)
		return ret;

	nvkm_debug(subdev, "WPR region is from 0x%llx-0x%llx (shadow 0x%llx)\n",
		   acr->wpr_start, acr->wpr_end, acr->shadow_start);

	/* Write WPR to ucode blob. */
	nvkm_kmap(acr->wpr);
	if (acr->wpr_fw && !acr->wpr_comp)
		nvkm_wobj(acr->wpr, 0, acr->wpr_fw->data, acr->wpr_fw->size);

	if (!acr->wpr_fw || acr->wpr_comp)
		acr->func->wpr_build(acr, rtos);
	acr->func->wpr_patch(acr, (s64)acr->wpr_start - acr->wpr_prev);

	if (acr->wpr_fw && acr->wpr_comp) {
		nvkm_kmap(acr->wpr);
		for (i = 0; i < acr->wpr_fw->size; i += 4) {
			u32 us = nvkm_ro32(acr->wpr, i);
			u32 fw = ((u32 *)acr->wpr_fw->data)[i/4];
			if (fw != us) {
				nvkm_warn(subdev, "%08x: %08x %08x\n",
					  i, us, fw);
			}
		}
		return -EINVAL;
	}
	nvkm_done(acr->wpr);

	/* Allocate instance block for ACR-related stuff. */
	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 0x1000, 0, true,
			      &acr->inst);
	if (ret)
		return ret;

	ret = nvkm_vmm_new(device, 0, 0, NULL, 0, NULL, "acr", &acr->vmm);
	if (ret)
		return ret;

	acr->vmm->debug = acr->subdev.debug;

	ret = nvkm_vmm_join(acr->vmm, acr->inst);
	if (ret)
		return ret;

	/* Load HS firmware blobs into ACR VMM. */
	list_for_each_entry(hsfw, &acr->hsfw, head) {
		switch (hsfw->falcon_id) {
		case NVKM_ACR_HSF_PMU : falcon = &device->pmu->falcon; break;
		case NVKM_ACR_HSF_SEC2: falcon = &device->sec2->falcon; break;
		case NVKM_ACR_HSF_GSP : falcon = &device->gsp->falcon; break;
		default:
			WARN_ON(1);
			return -EINVAL;
		}

		ret = nvkm_falcon_fw_oneinit(&hsfw->fw, falcon, acr->vmm, acr->inst);
		if (ret)
			return ret;
	}

	/* Kill temporary data. */
	nvkm_acr_cleanup(acr);
	return 0;
}

static void *
nvkm_acr_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_acr *acr = nvkm_acr(subdev);
	struct nvkm_acr_hsfw *hsfw, *hsft;
	struct nvkm_acr_lsf *lsf, *lst;

	list_for_each_entry_safe(hsfw, hsft, &acr->hsfw, head) {
		nvkm_falcon_fw_dtor(&hsfw->fw);
		list_del(&hsfw->head);
		kfree(hsfw);
	}

	nvkm_vmm_part(acr->vmm, acr->inst);
	nvkm_vmm_unref(&acr->vmm);
	nvkm_memory_unref(&acr->inst);

	nvkm_memory_unref(&acr->wpr);

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
	.init = nvkm_acr_init,
	.fini = nvkm_acr_fini,
};

static int
nvkm_acr_ctor_wpr(struct nvkm_acr *acr, int ver)
{
	struct nvkm_subdev *subdev = &acr->subdev;
	struct nvkm_device *device = subdev->device;
	int ret;

	ret = nvkm_firmware_get(subdev, "acr/wpr", ver, &acr->wpr_fw);
	if (ret < 0)
		return ret;

	/* Pre-add LSFs in the order they appear in the FW WPR image so that
	 * we're able to do a binary comparison with our own generator.
	 */
	ret = acr->func->wpr_parse(acr);
	if (ret)
		return ret;

	acr->wpr_comp = nvkm_boolopt(device->cfgopt, "NvAcrWprCompare", false);
	acr->wpr_prev = nvkm_longopt(device->cfgopt, "NvAcrWprPrevAddr", 0);
	return 0;
}

int
nvkm_acr_new_(const struct nvkm_acr_fwif *fwif, struct nvkm_device *device,
	      enum nvkm_subdev_type type, int inst, struct nvkm_acr **pacr)
{
	struct nvkm_acr *acr;
	long wprfw;

	if (!(acr = *pacr = kzalloc(sizeof(*acr), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_subdev_ctor(&nvkm_acr, device, type, inst, &acr->subdev);
	INIT_LIST_HEAD(&acr->hsfw);
	INIT_LIST_HEAD(&acr->lsfw);
	INIT_LIST_HEAD(&acr->lsf);

	fwif = nvkm_firmware_load(&acr->subdev, fwif, "Acr", acr);
	if (IS_ERR(fwif))
		return PTR_ERR(fwif);

	acr->func = fwif->func;

	wprfw = nvkm_longopt(device->cfgopt, "NvAcrWpr", -1);
	if (wprfw >= 0) {
		int ret = nvkm_acr_ctor_wpr(acr, wprfw);
		if (ret)
			return ret;
	}

	return 0;
}
