/*
 * CXL Flash Device Driver
 *
 * Written by: Matthew R. Ochs <mrochs@linux.vnet.ibm.com>, IBM Corporation
 *             Uma Krishnan <ukrishn@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2018 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <misc/ocxl.h>

#include "backend.h"
#include "ocxl_hw.h"

/**
 * ocxlflash_destroy_afu() - destroy the AFU structure
 * @afu_cookie:	AFU to be freed.
 */
static void ocxlflash_destroy_afu(void *afu_cookie)
{
	struct ocxl_hw_afu *afu = afu_cookie;

	if (!afu)
		return;

	kfree(afu);
}

/**
 * ocxlflash_config_fn() - configure the host function
 * @pdev:	PCI device associated with the host.
 * @afu:	AFU associated with the host.
 *
 * Return: 0 on success, -errno on failure
 */
static int ocxlflash_config_fn(struct pci_dev *pdev, struct ocxl_hw_afu *afu)
{
	struct ocxl_fn_config *fcfg = &afu->fcfg;
	struct device *dev = &pdev->dev;
	u16 base, enabled, supported;
	int rc = 0;

	/* Read DVSEC config of the function */
	rc = ocxl_config_read_function(pdev, fcfg);
	if (unlikely(rc)) {
		dev_err(dev, "%s: ocxl_config_read_function failed rc=%d\n",
			__func__, rc);
		goto out;
	}

	/* Check if function has AFUs defined, only 1 per function supported */
	if (fcfg->max_afu_index >= 0) {
		afu->is_present = true;
		if (fcfg->max_afu_index != 0)
			dev_warn(dev, "%s: Unexpected AFU index value %d\n",
				 __func__, fcfg->max_afu_index);
	}

	rc = ocxl_config_get_actag_info(pdev, &base, &enabled, &supported);
	if (unlikely(rc)) {
		dev_err(dev, "%s: ocxl_config_get_actag_info failed rc=%d\n",
			__func__, rc);
		goto out;
	}

	afu->fn_actag_base = base;
	afu->fn_actag_enabled = enabled;

	ocxl_config_set_actag(pdev, fcfg->dvsec_function_pos, base, enabled);
	dev_dbg(dev, "%s: Function acTag range base=%u enabled=%u\n",
		__func__, base, enabled);
out:
	return rc;
}

/**
 * ocxlflash_config_afu() - configure the host AFU
 * @pdev:	PCI device associated with the host.
 * @afu:	AFU associated with the host.
 *
 * Must be called _after_ host function configuration.
 *
 * Return: 0 on success, -errno on failure
 */
static int ocxlflash_config_afu(struct pci_dev *pdev, struct ocxl_hw_afu *afu)
{
	struct ocxl_afu_config *acfg = &afu->acfg;
	struct ocxl_fn_config *fcfg = &afu->fcfg;
	struct device *dev = &pdev->dev;
	int count;
	int base;
	int pos;
	int rc = 0;

	/* This HW AFU function does not have any AFUs defined */
	if (!afu->is_present)
		goto out;

	/* Read AFU config at index 0 */
	rc = ocxl_config_read_afu(pdev, fcfg, acfg, 0);
	if (unlikely(rc)) {
		dev_err(dev, "%s: ocxl_config_read_afu failed rc=%d\n",
			__func__, rc);
		goto out;
	}

	/* Only one AFU per function is supported, so actag_base is same */
	base = afu->fn_actag_base;
	count = min_t(int, acfg->actag_supported, afu->fn_actag_enabled);
	pos = acfg->dvsec_afu_control_pos;

	ocxl_config_set_afu_actag(pdev, pos, base, count);
	dev_dbg(dev, "%s: acTag base=%d enabled=%d\n", __func__, base, count);
	afu->afu_actag_base = base;
	afu->afu_actag_enabled = count;
	afu->max_pasid = 1 << acfg->pasid_supported_log;

	ocxl_config_set_afu_pasid(pdev, pos, 0, acfg->pasid_supported_log);
out:
	return rc;
}

/**
 * ocxlflash_create_afu() - create the AFU for OCXL
 * @pdev:	PCI device associated with the host.
 *
 * Return: AFU on success, NULL on failure
 */
static void *ocxlflash_create_afu(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct ocxl_hw_afu *afu;
	int rc;

	afu = kzalloc(sizeof(*afu), GFP_KERNEL);
	if (unlikely(!afu)) {
		dev_err(dev, "%s: HW AFU allocation failed\n", __func__);
		goto out;
	}

	afu->pdev = pdev;
	afu->dev = dev;

	rc = ocxlflash_config_fn(pdev, afu);
	if (unlikely(rc)) {
		dev_err(dev, "%s: Function configuration failed rc=%d\n",
			__func__, rc);
		goto err1;
	}

	rc = ocxlflash_config_afu(pdev, afu);
	if (unlikely(rc)) {
		dev_err(dev, "%s: AFU configuration failed rc=%d\n",
			__func__, rc);
		goto err1;
	}
out:
	return afu;
err1:
	kfree(afu);
	afu = NULL;
	goto out;
}

/* Backend ops to ocxlflash services */
const struct cxlflash_backend_ops cxlflash_ocxl_ops = {
	.module			= THIS_MODULE,
	.create_afu		= ocxlflash_create_afu,
	.destroy_afu		= ocxlflash_destroy_afu,
};
