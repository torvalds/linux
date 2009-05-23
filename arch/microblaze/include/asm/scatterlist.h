/*
 * Copyright (C) 2008 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_SCATTERLIST_H
#define _ASM_MICROBLAZE_SCATTERLIST_H

struct scatterlist {
#ifdef CONFIG_DEBUG_SG
	unsigned long	sg_magic;
#endif
	unsigned long	page_link;
	dma_addr_t	dma_address;
	unsigned int	offset;
	unsigned int	length;
};

#define sg_dma_address(sg)      ((sg)->dma_address)
#define sg_dma_len(sg)          ((sg)->length)

#define ISA_DMA_THRESHOLD (~0UL)

#endif /* _ASM_MICROBLAZE_SCATTERLIST_H */
