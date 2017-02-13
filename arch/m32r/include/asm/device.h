/*
 * Arch specific extensions to struct device
 *
 * This file is released under the GPLv2
 */
struct dev_archdata {
	struct dma_map_ops *dma_ops;
};

struct pdev_archdata {
};
