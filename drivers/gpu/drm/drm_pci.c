/*
 * Copyright 2003 José Fonseca.
 * Copyright 2003 Leif Delgass.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <drm/drm_auth.h>
#include <drm/drm.h>
#include <drm/drm_drv.h>
#include <drm/drm_print.h>

#include "drm_internal.h"

static int drm_get_pci_domain(struct drm_device *dev)
{
#ifndef __alpha__
	/* For historical reasons, drm_get_pci_domain() is busticated
	 * on most archs and has to remain so for userspace interface
	 * < 1.4, except on alpha which was right from the beginning
	 */
	if (dev->if_version < 0x10004)
		return 0;
#endif /* __alpha__ */

	return pci_domain_nr(to_pci_dev(dev->dev)->bus);
}

int drm_pci_set_busid(struct drm_device *dev, struct drm_master *master)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	master->unique = kasprintf(GFP_KERNEL, "pci:%04x:%02x:%02x.%d",
					drm_get_pci_domain(dev),
					pdev->bus->number,
					PCI_SLOT(pdev->devfn),
					PCI_FUNC(pdev->devfn));
	if (!master->unique)
		return -ENOMEM;

	master->unique_len = strlen(master->unique);
	return 0;
}
