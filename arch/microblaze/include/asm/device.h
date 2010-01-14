/*
 * Arch specific extensions to struct device
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_DEVICE_H
#define _ASM_MICROBLAZE_DEVICE_H

struct device_node;

struct dev_archdata {
	/* Optional pointer to an OF device node */
	struct device_node	*of_node;

	/* DMA operations on that device */
	struct dma_map_ops	*dma_ops;
	void                    *dma_data;
};

struct pdev_archdata {
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

#endif /* _ASM_MICROBLAZE_DEVICE_H */


