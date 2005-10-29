/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 by Ralf Baechle
 *
 * Dummy ISA DMA functions for systems that don't have ISA but share drivers
 * with ISA such as legacy free PCI.
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/spinlock.h>

DEFINE_SPINLOCK(dma_spin_lock);

int request_dma(unsigned int dmanr, const char * device_id)
{
	return -EINVAL;
}

void free_dma(unsigned int dmanr)
{
}

EXPORT_SYMBOL(dma_spin_lock);
EXPORT_SYMBOL(request_dma);
EXPORT_SYMBOL(free_dma);
