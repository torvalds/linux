/*
 * Arch specific extensions to struct device
 *
 * This file is released under the GPLv2
 */
#ifndef _ASM_SPARC_DEVICE_H
#define _ASM_SPARC_DEVICE_H

struct device_node;
struct of_device;

struct dev_archdata {
	void			*iommu;
	void			*stc;
	void			*host_controller;

	struct device_node	*prom_node;
	struct of_device	*op;

	int			numa_node;
};

static inline void dev_archdata_set_node(struct dev_archdata *ad,
					 struct device_node *np)
{
	ad->prom_node = np;
}

static inline struct device_node *
dev_archdata_get_node(const struct dev_archdata *ad)
{
	return ad->prom_node;
}

struct pdev_archdata {
};

#endif /* _ASM_SPARC_DEVICE_H */
