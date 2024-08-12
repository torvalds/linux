/*
 * Copyright (C) 2007 Antonino Daplas <adaplas@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/vgaarb.h>

#include <asm/video.h>

pgprot_t pgprot_framebuffer(pgprot_t prot,
			    unsigned long vm_start, unsigned long vm_end,
			    unsigned long offset)
{
	pgprot_val(prot) &= ~_PAGE_CACHE_MASK;
	if (boot_cpu_data.x86 > 3)
		pgprot_val(prot) |= cachemode2protval(_PAGE_CACHE_MODE_UC_MINUS);

	return prot;
}
EXPORT_SYMBOL(pgprot_framebuffer);

bool video_is_primary_device(struct device *dev)
{
	struct pci_dev *pdev;

	if (!dev_is_pci(dev))
		return false;

	pdev = to_pci_dev(dev);

	return (pdev == vga_default_device());
}
EXPORT_SYMBOL(video_is_primary_device);

MODULE_LICENSE("GPL");
