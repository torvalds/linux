/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006  Ralf Baechle <ralf@linux-mips.org>
 *
 */
#ifndef __ASM_DMA_COHERENCE_H
#define __ASM_DMA_COHERENCE_H

#ifdef CONFIG_DMA_MAYBE_COHERENT
extern bool dma_default_coherent;
static inline bool dev_is_dma_coherent(struct device *dev)
{
	return dma_default_coherent;
}
#else
#define dma_default_coherent	(!IS_ENABLED(CONFIG_DMA_NONCOHERENT))
#endif

#endif
