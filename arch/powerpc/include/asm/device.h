/*
 * Arch specific extensions to struct device
 *
 * This file is released under the GPLv2
 */
#ifndef _ASM_POWERPC_DEVICE_H
#define _ASM_POWERPC_DEVICE_H

struct dma_mapping_ops;
struct device_node;

struct dev_archdata {
	/* Optional pointer to an OF device node */
	struct device_node	*of_node;

	/* DMA operations on that device */
	struct dma_mapping_ops	*dma_ops;
	void			*dma_data;
};

#endif /* _ASM_POWERPC_DEVICE_H */
