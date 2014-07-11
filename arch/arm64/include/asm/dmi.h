/*
 * arch/arm64/include/asm/dmi.h
 *
 * Copyright (C) 2013 Linaro Limited.
 * Written by: Yi Li (yi.li@linaro.org)
 *
 * based on arch/ia64/include/asm/dmi.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef __ASM_DMI_H
#define __ASM_DMI_H

#include <linux/slab.h>
#include <linux/efi.h>

static inline void __iomem *dmi_remap(u64 phys, u64 size)
{
	void __iomem *p = efi_lookup_mapped_addr(phys);

	/*
	 * If the mapping spans multiple pages, do a minimal check to ensure
	 * that the mapping returned by efi_lookup_mapped_addr() covers the
	 * whole requested range (but ignore potential holes)
	 */
	if ((phys & ~PAGE_MASK) + size > PAGE_SIZE
	    && (p + size - 1) != efi_lookup_mapped_addr(phys + size - 1))
		return NULL;
	return p;
}

/* Reuse existing UEFI mappings for DMI */
#define dmi_alloc(l)			kzalloc(l, GFP_KERNEL)
#define dmi_early_remap(x, l)		dmi_remap(x, l)
#define dmi_early_unmap(x, l)
#define dmi_unmap(x)

#endif
