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

static inline void dev_archdata_set_node(struct dev_archdata *ad,
					 struct device_node *np)
{
	ad->of_node = np;
}

static inline struct device_node *
dev_archdata_get_node(const struct dev_archdata *ad)
{
	return ad->of_node;
}

#endif /* _ASM_POWERPC_DEVICE_H */
