// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2023, Intel Corporation. All rights reserved.
 */

#include <linux/irq.h>
#include <linux/mei_aux.h>
#include <linux/pci.h>
#include <linux/sizes.h>

#include "xe_device_types.h"
#include "xe_drv.h"
#include "xe_heci_gsc.h"
#include "xe_platform_types.h"

#define GSC_BAR_LENGTH  0x00000FFC

#define DG1_GSC_HECI2_BASE			0x259000
#define PVC_GSC_HECI2_BASE			0x285000
#define DG2_GSC_HECI2_BASE			0x374000

static void heci_gsc_irq_mask(struct irq_data *d)
{
	/* generic irq handling */
}

static void heci_gsc_irq_unmask(struct irq_data *d)
{
	/* generic irq handling */
}

static struct irq_chip heci_gsc_irq_chip = {
	.name = "gsc_irq_chip",
	.irq_mask = heci_gsc_irq_mask,
	.irq_unmask = heci_gsc_irq_unmask,
};

static int heci_gsc_irq_init(int irq)
{
	irq_set_chip_and_handler_name(irq, &heci_gsc_irq_chip,
				      handle_simple_irq, "heci_gsc_irq_handler");

	return irq_set_chip_data(irq, NULL);
}

/**
 * struct heci_gsc_def - graphics security controller heci interface definitions
 *
 * @name: name of the heci device
 * @bar: address of the mmio bar
 * @bar_size: size of the mmio bar
 * @use_polling: indication of using polling mode for the device
 * @slow_firmware: indication of whether the device is slow (needs longer timeouts)
 */
struct heci_gsc_def {
	const char *name;
	unsigned long bar;
	size_t bar_size;
	bool use_polling;
	bool slow_firmware;
};

/* gsc resources and definitions */
static const struct heci_gsc_def heci_gsc_def_dg1 = {
	.name = "mei-gscfi",
	.bar = DG1_GSC_HECI2_BASE,
	.bar_size = GSC_BAR_LENGTH,
};

static const struct heci_gsc_def heci_gsc_def_dg2 = {
	.name = "mei-gscfi",
	.bar = DG2_GSC_HECI2_BASE,
	.bar_size = GSC_BAR_LENGTH,
};

static const struct heci_gsc_def heci_gsc_def_pvc = {
	.name = "mei-gscfi",
	.bar = PVC_GSC_HECI2_BASE,
	.bar_size = GSC_BAR_LENGTH,
	.slow_firmware = true,
};

static void heci_gsc_release_dev(struct device *dev)
{
	struct auxiliary_device *aux_dev = to_auxiliary_dev(dev);
	struct mei_aux_device *adev = auxiliary_dev_to_mei_aux_dev(aux_dev);

	kfree(adev);
}

void xe_heci_gsc_fini(struct xe_device *xe)
{
	struct xe_heci_gsc *heci_gsc = &xe->heci_gsc;

	if (!HAS_HECI_GSCFI(xe))
		return;

	if (heci_gsc->adev) {
		struct auxiliary_device *aux_dev = &heci_gsc->adev->aux_dev;

		auxiliary_device_delete(aux_dev);
		auxiliary_device_uninit(aux_dev);
		heci_gsc->adev = NULL;
	}

	if (heci_gsc->irq >= 0)
		irq_free_desc(heci_gsc->irq);
	heci_gsc->irq = -1;
}

static int heci_gsc_irq_setup(struct xe_device *xe)
{
	struct xe_heci_gsc *heci_gsc = &xe->heci_gsc;
	int ret;

	heci_gsc->irq = irq_alloc_desc(0);
	if (heci_gsc->irq < 0) {
		drm_err(&xe->drm, "gsc irq error %d\n", heci_gsc->irq);
		return heci_gsc->irq;
	}

	ret = heci_gsc_irq_init(heci_gsc->irq);
	if (ret < 0)
		drm_err(&xe->drm, "gsc irq init failed %d\n", ret);

	return ret;
}

static int heci_gsc_add_device(struct xe_device *xe, const struct heci_gsc_def *def)
{
	struct xe_heci_gsc *heci_gsc = &xe->heci_gsc;
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	struct auxiliary_device *aux_dev;
	struct mei_aux_device *adev;
	int ret;

	adev = kzalloc(sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;
	adev->irq = heci_gsc->irq;
	adev->bar.parent = &pdev->resource[0];
	adev->bar.start = def->bar + pdev->resource[0].start;
	adev->bar.end = adev->bar.start + def->bar_size - 1;
	adev->bar.flags = IORESOURCE_MEM;
	adev->bar.desc = IORES_DESC_NONE;
	adev->slow_firmware = def->slow_firmware;

	aux_dev = &adev->aux_dev;
	aux_dev->name = def->name;
	aux_dev->id = (pci_domain_nr(pdev->bus) << 16) |
		      PCI_DEVID(pdev->bus->number, pdev->devfn);
	aux_dev->dev.parent = &pdev->dev;
	aux_dev->dev.release = heci_gsc_release_dev;

	ret = auxiliary_device_init(aux_dev);
	if (ret < 0) {
		drm_err(&xe->drm, "gsc aux init failed %d\n", ret);
		kfree(adev);
		return ret;
	}

	heci_gsc->adev = adev; /* needed by the notifier */
	ret = auxiliary_device_add(aux_dev);
	if (ret < 0) {
		drm_err(&xe->drm, "gsc aux add failed %d\n", ret);
		heci_gsc->adev = NULL;

		/* adev will be freed with the put_device() and .release sequence */
		auxiliary_device_uninit(aux_dev);
	}
	return ret;
}

void xe_heci_gsc_init(struct xe_device *xe)
{
	struct xe_heci_gsc *heci_gsc = &xe->heci_gsc;
	const struct heci_gsc_def *def;
	int ret;

	if (!HAS_HECI_GSCFI(xe))
		return;

	heci_gsc->irq = -1;

	if (xe->info.platform == XE_PVC) {
		def = &heci_gsc_def_pvc;
	} else if (xe->info.platform == XE_DG2) {
		def = &heci_gsc_def_dg2;
	} else if (xe->info.platform == XE_DG1) {
		def = &heci_gsc_def_dg1;
	} else {
		drm_warn_once(&xe->drm, "Unknown platform\n");
		return;
	}

	if (!def->name) {
		drm_warn_once(&xe->drm, "HECI is not implemented!\n");
		return;
	}

	if (!def->use_polling) {
		ret = heci_gsc_irq_setup(xe);
		if (ret)
			goto fail;
	}

	ret = heci_gsc_add_device(xe, def);
	if (ret)
		goto fail;

	return;
fail:
	xe_heci_gsc_fini(xe);
}

void xe_heci_gsc_irq_handler(struct xe_device *xe, u32 iir)
{
	int ret;

	if ((iir & GSC_IRQ_INTF(1)) == 0)
		return;

	if (!HAS_HECI_GSCFI(xe)) {
		drm_warn_once(&xe->drm, "GSC irq: not supported");
		return;
	}

	if (xe->heci_gsc.irq < 0)
		return;

	ret = generic_handle_irq(xe->heci_gsc.irq);
	if (ret)
		drm_err_ratelimited(&xe->drm, "error handling GSC irq: %d\n", ret);
}
