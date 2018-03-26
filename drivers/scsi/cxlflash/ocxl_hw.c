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
 * ocxlflash_create_afu() - create the AFU for OCXL
 * @pdev:	PCI device associated with the host.
 *
 * Return: AFU on success, NULL on failure
 */
static void *ocxlflash_create_afu(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct ocxl_hw_afu *afu;

	afu = kzalloc(sizeof(*afu), GFP_KERNEL);
	if (unlikely(!afu)) {
		dev_err(dev, "%s: HW AFU allocation failed\n", __func__);
		goto out;
	}

	afu->pdev = pdev;
	afu->dev = dev;
out:
	return afu;
}

/* Backend ops to ocxlflash services */
const struct cxlflash_backend_ops cxlflash_ocxl_ops = {
	.module			= THIS_MODULE,
	.create_afu		= ocxlflash_create_afu,
	.destroy_afu		= ocxlflash_destroy_afu,
};
