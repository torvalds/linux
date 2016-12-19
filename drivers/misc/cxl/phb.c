/*
 * Copyright 2014-2016 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/pci.h>
#include "cxl.h"

bool _cxl_pci_associate_default_context(struct pci_dev *dev, struct cxl_afu *afu)
{
	struct cxl_context *ctx;

	/*
	 * Allocate a context to do cxl things to. This is used for interrupts
	 * in the peer model using a real phb, and if we eventually do DMA ops
	 * in the virtual phb, we'll need a default context to attach them to.
	 */
	ctx = cxl_dev_context_init(dev);
	if (IS_ERR(ctx))
		return false;
	dev->dev.archdata.cxl_ctx = ctx;

	return (cxl_ops->afu_check_and_enable(afu) == 0);
}
/* exported via cxl_base */

void _cxl_pci_disable_device(struct pci_dev *dev)
{
	struct cxl_context *ctx = cxl_get_context(dev);

	if (ctx) {
		if (ctx->status == STARTED) {
			dev_err(&dev->dev, "Default context started\n");
			return;
		}
		dev->dev.archdata.cxl_ctx = NULL;
		cxl_release_context(ctx);
	}
}
/* exported via cxl_base */
