/*
 * Freescale Management Complex (MC) bus declarations
 *
 * Copyright (C) 2014-2016 Freescale Semiconductor, Inc.
 * Author: German Rivera <German.Rivera@freescale.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef _FSL_MC_MCBUS_H_
#define _FSL_MC_MCBUS_H_

#include "../include/mc.h"
#include <linux/mutex.h>

struct irq_domain;
struct msi_domain_info;

/**
 * Maximum number of total IRQs that can be pre-allocated for an MC bus'
 * IRQ pool
 */
#define FSL_MC_IRQ_POOL_MAX_TOTAL_IRQS	256

#ifdef CONFIG_FSL_MC_BUS
#define dev_is_fsl_mc(_dev) ((_dev)->bus == &fsl_mc_bus_type)
#else
/* If fsl-mc bus is not present device cannot belong to fsl-mc bus */
#define dev_is_fsl_mc(_dev) (0)
#endif

/**
 * struct fsl_mc_resource_pool - Pool of MC resources of a given
 * type
 * @type: type of resources in the pool
 * @max_count: maximum number of resources in the pool
 * @free_count: number of free resources in the pool
 * @mutex: mutex to serialize access to the pool's free list
 * @free_list: anchor node of list of free resources in the pool
 * @mc_bus: pointer to the MC bus that owns this resource pool
 */
struct fsl_mc_resource_pool {
	enum fsl_mc_pool_type type;
	s16 max_count;
	s16 free_count;
	struct mutex mutex;	/* serializes access to free_list */
	struct list_head free_list;
	struct fsl_mc_bus *mc_bus;
};

/**
 * struct fsl_mc_bus - logical bus that corresponds to a physical DPRC
 * @mc_dev: fsl-mc device for the bus device itself.
 * @resource_pools: array of resource pools (one pool per resource type)
 * for this MC bus. These resources represent allocatable entities
 * from the physical DPRC.
 * @irq_resources: Pointer to array of IRQ objects for the IRQ pool
 * @scan_mutex: Serializes bus scanning
 * @dprc_attr: DPRC attributes
 */
struct fsl_mc_bus {
	struct fsl_mc_device mc_dev;
	struct fsl_mc_resource_pool resource_pools[FSL_MC_NUM_POOL_TYPES];
	struct fsl_mc_device_irq *irq_resources;
	struct mutex scan_mutex;    /* serializes bus scanning */
	struct dprc_attributes dprc_attr;
};

#define to_fsl_mc_bus(_mc_dev) \
	container_of(_mc_dev, struct fsl_mc_bus, mc_dev)

int dprc_scan_container(struct fsl_mc_device *mc_bus_dev);

int dprc_scan_objects(struct fsl_mc_device *mc_bus_dev,
		      unsigned int *total_irq_count);

int __init dprc_driver_init(void);

void dprc_driver_exit(void);

int __init fsl_mc_allocator_driver_init(void);

void fsl_mc_allocator_driver_exit(void);

struct irq_domain *fsl_mc_msi_create_irq_domain(struct fwnode_handle *fwnode,
						struct msi_domain_info *info,
						struct irq_domain *parent);

int fsl_mc_find_msi_domain(struct device *mc_platform_dev,
			   struct irq_domain **mc_msi_domain);

int fsl_mc_populate_irq_pool(struct fsl_mc_bus *mc_bus,
			     unsigned int irq_count);

void fsl_mc_cleanup_irq_pool(struct fsl_mc_bus *mc_bus);

void fsl_mc_init_all_resource_pools(struct fsl_mc_device *mc_bus_dev);

void fsl_mc_cleanup_all_resource_pools(struct fsl_mc_device *mc_bus_dev);

bool fsl_mc_bus_exists(void);

void fsl_mc_get_root_dprc(struct device *dev,
			  struct device **root_dprc_dev);

bool fsl_mc_is_root_dprc(struct device *dev);

extern struct bus_type fsl_mc_bus_type;

#endif /* _FSL_MC_MCBUS_H_ */
