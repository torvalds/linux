// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2019-2022, Intel Corporation. All rights reserved.
 */

#include <linux/irq.h>
#include <linux/mei_aux.h>
#include "i915_drv.h"
#include "i915_reg.h"
#include "gt/intel_gsc.h"
#include "gt/intel_gt.h"

#define GSC_BAR_LENGTH  0x00000FFC

static void gsc_irq_mask(struct irq_data *d)
{
	/* generic irq handling */
}

static void gsc_irq_unmask(struct irq_data *d)
{
	/* generic irq handling */
}

static struct irq_chip gsc_irq_chip = {
	.name = "gsc_irq_chip",
	.irq_mask = gsc_irq_mask,
	.irq_unmask = gsc_irq_unmask,
};

static int gsc_irq_init(int irq)
{
	irq_set_chip_and_handler_name(irq, &gsc_irq_chip,
				      handle_simple_irq, "gsc_irq_handler");

	return irq_set_chip_data(irq, NULL);
}

struct gsc_def {
	const char *name;
	unsigned long bar;
	size_t bar_size;
};

/* gsc resources and definitions (HECI1 and HECI2) */
static const struct gsc_def gsc_def_dg1[] = {
	{
		/* HECI1 not yet implemented. */
	},
	{
		.name = "mei-gscfi",
		.bar = DG1_GSC_HECI2_BASE,
		.bar_size = GSC_BAR_LENGTH,
	}
};

static const struct gsc_def gsc_def_dg2[] = {
	{
		.name = "mei-gsc",
		.bar = DG2_GSC_HECI1_BASE,
		.bar_size = GSC_BAR_LENGTH,
	},
	{
		.name = "mei-gscfi",
		.bar = DG2_GSC_HECI2_BASE,
		.bar_size = GSC_BAR_LENGTH,
	}
};

static void gsc_release_dev(struct device *dev)
{
	struct auxiliary_device *aux_dev = to_auxiliary_dev(dev);
	struct mei_aux_device *adev = auxiliary_dev_to_mei_aux_dev(aux_dev);

	kfree(adev);
}

static void gsc_destroy_one(struct intel_gsc_intf *intf)
{
	if (intf->adev) {
		auxiliary_device_delete(&intf->adev->aux_dev);
		auxiliary_device_uninit(&intf->adev->aux_dev);
		intf->adev = NULL;
	}
	if (intf->irq >= 0)
		irq_free_desc(intf->irq);
	intf->irq = -1;
}

static void gsc_init_one(struct drm_i915_private *i915,
			 struct intel_gsc_intf *intf,
			 unsigned int intf_id)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	struct mei_aux_device *adev;
	struct auxiliary_device *aux_dev;
	const struct gsc_def *def;
	int ret;

	intf->irq = -1;
	intf->id = intf_id;

	if (intf_id == 0 && !HAS_HECI_PXP(i915))
		return;

	if (IS_DG1(i915)) {
		def = &gsc_def_dg1[intf_id];
	} else if (IS_DG2(i915)) {
		def = &gsc_def_dg2[intf_id];
	} else {
		drm_warn_once(&i915->drm, "Unknown platform\n");
		return;
	}

	if (!def->name) {
		drm_warn_once(&i915->drm, "HECI%d is not implemented!\n", intf_id + 1);
		return;
	}

	intf->irq = irq_alloc_desc(0);
	if (intf->irq < 0) {
		drm_err(&i915->drm, "gsc irq error %d\n", intf->irq);
		return;
	}

	ret = gsc_irq_init(intf->irq);
	if (ret < 0) {
		drm_err(&i915->drm, "gsc irq init failed %d\n", ret);
		goto fail;
	}

	adev = kzalloc(sizeof(*adev), GFP_KERNEL);
	if (!adev)
		goto fail;

	adev->irq = intf->irq;
	adev->bar.parent = &pdev->resource[0];
	adev->bar.start = def->bar + pdev->resource[0].start;
	adev->bar.end = adev->bar.start + def->bar_size - 1;
	adev->bar.flags = IORESOURCE_MEM;
	adev->bar.desc = IORES_DESC_NONE;

	aux_dev = &adev->aux_dev;
	aux_dev->name = def->name;
	aux_dev->id = (pci_domain_nr(pdev->bus) << 16) |
		      PCI_DEVID(pdev->bus->number, pdev->devfn);
	aux_dev->dev.parent = &pdev->dev;
	aux_dev->dev.release = gsc_release_dev;

	ret = auxiliary_device_init(aux_dev);
	if (ret < 0) {
		drm_err(&i915->drm, "gsc aux init failed %d\n", ret);
		kfree(adev);
		goto fail;
	}

	ret = auxiliary_device_add(aux_dev);
	if (ret < 0) {
		drm_err(&i915->drm, "gsc aux add failed %d\n", ret);
		/* adev will be freed with the put_device() and .release sequence */
		auxiliary_device_uninit(aux_dev);
		goto fail;
	}
	intf->adev = adev;

	return;
fail:
	gsc_destroy_one(intf);
}

static void gsc_irq_handler(struct intel_gt *gt, unsigned int intf_id)
{
	int ret;

	if (intf_id >= INTEL_GSC_NUM_INTERFACES) {
		drm_warn_once(&gt->i915->drm, "GSC irq: intf_id %d is out of range", intf_id);
		return;
	}

	if (!HAS_HECI_GSC(gt->i915)) {
		drm_warn_once(&gt->i915->drm, "GSC irq: not supported");
		return;
	}

	if (gt->gsc.intf[intf_id].irq < 0) {
		drm_err_ratelimited(&gt->i915->drm, "GSC irq: irq not set");
		return;
	}

	ret = generic_handle_irq(gt->gsc.intf[intf_id].irq);
	if (ret)
		drm_err_ratelimited(&gt->i915->drm, "error handling GSC irq: %d\n", ret);
}

void intel_gsc_irq_handler(struct intel_gt *gt, u32 iir)
{
	if (iir & GSC_IRQ_INTF(0))
		gsc_irq_handler(gt, 0);
	if (iir & GSC_IRQ_INTF(1))
		gsc_irq_handler(gt, 1);
}

void intel_gsc_init(struct intel_gsc *gsc, struct drm_i915_private *i915)
{
	unsigned int i;

	if (!HAS_HECI_GSC(i915))
		return;

	for (i = 0; i < INTEL_GSC_NUM_INTERFACES; i++)
		gsc_init_one(i915, &gsc->intf[i], i);
}

void intel_gsc_fini(struct intel_gsc *gsc)
{
	struct intel_gt *gt = gsc_to_gt(gsc);
	unsigned int i;

	if (!HAS_HECI_GSC(gt->i915))
		return;

	for (i = 0; i < INTEL_GSC_NUM_INTERFACES; i++)
		gsc_destroy_one(&gsc->intf[i]);
}
