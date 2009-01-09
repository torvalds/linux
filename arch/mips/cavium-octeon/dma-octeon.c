/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000  Ani Joshi <ajoshi@unixbox.com>
 * Copyright (C) 2000, 2001  Ralf Baechle <ralf@gnu.org>
 * Copyright (C) 2005 Ilya A. Volynets-Evenbakh <ilya@total-knowledge.com>
 * swiped from i386, and cloned for MIPS by Geert, polished by Ralf.
 * IP32 changes by Ilya.
 * Cavium Networks: Create new dma setup for Cavium Networks Octeon based on
 * the kernels original.
 */
#include <linux/types.h>
#include <linux/mm.h>

#include <dma-coherence.h>

dma_addr_t octeon_map_dma_mem(struct device *dev, void *ptr, size_t size)
{
	/* Without PCI/PCIe this function can be called for Octeon internal
	   devices such as USB. These devices all support 64bit addressing */
	mb();
	return virt_to_phys(ptr);
}

void octeon_unmap_dma_mem(struct device *dev, dma_addr_t dma_addr)
{
	/* Without PCI/PCIe this function can be called for Octeon internal
	 * devices such as USB. These devices all support 64bit addressing */
	return;
}
