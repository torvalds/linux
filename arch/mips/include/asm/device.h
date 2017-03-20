/*
 * Arch specific extensions to struct device
 *
 * This file is released under the GPLv2
 */
#ifndef _ASM_MIPS_DEVICE_H
#define _ASM_MIPS_DEVICE_H

struct dev_archdata {
#ifdef CONFIG_DMA_PERDEV_COHERENT
	/* Non-zero if DMA is coherent with CPU caches */
	bool dma_coherent;
#endif
};

struct pdev_archdata {
};

#endif /* _ASM_MIPS_DEVICE_H*/
