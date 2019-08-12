// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Contains common dma routines for all powerpc platforms.
 *
 * Copyright (C) 2019 Shawn Anastasio.
 */

#include <linux/mm.h>
#include <linux/dma-noncoherent.h>

pgprot_t arch_dma_mmap_pgprot(struct device *dev, pgprot_t prot,
		unsigned long attrs)
{
	if (!dev_is_dma_coherent(dev))
		return pgprot_noncached(prot);
	return prot;
}
