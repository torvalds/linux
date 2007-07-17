/*
 * arch/i386/video/fbdev.c - i386 Framebuffer
 *
 * Copyright (C) 2007 Antonino Daplas <adaplas@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */
#include <linux/fb.h>
#include <linux/pci.h>

int fb_is_primary_device(struct fb_info *info)
{
	struct device *device;
	struct pci_dev *pci_dev = NULL;
	struct resource *res = NULL;
	int retval = 0;

	device = info->device;

	if (device)
		pci_dev = to_pci_dev(device);

	if (pci_dev)
		res = &pci_dev->resource[PCI_ROM_RESOURCE];

	if (res && res->flags & IORESOURCE_ROM_SHADOW)
		retval = 1;

	return retval;
}
EXPORT_SYMBOL(fb_is_primary_device);
