/*
 * Freescale Management Complex (MC) bus private declarations
 *
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef _FSL_MC_PRIVATE_H_
#define _FSL_MC_PRIVATE_H_

#include "../include/mc.h"
#include "dprc.h"
#include <linux/mutex.h>

/**
 * Maximum number of total IRQs that can be pre-allocated for an MC bus'
 * IRQ pool
 */
#define FSL_MC_IRQ_POOL_MAX_TOTAL_IRQS	256

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
	int max_count;
	int free_count;
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

int __must_check fsl_mc_device_add(struct fsl_mc_obj_desc *obj_desc,
				   struct fsl_mc_io *mc_io,
				   struct device *parent_dev,
				   struct fsl_mc_device **new_mc_dev);

void fsl_mc_device_remove(struct fsl_mc_device *mc_dev);

int __init dprc_driver_init(void);

void dprc_driver_exit(void);

int __init fsl_mc_allocator_driver_init(void);

void fsl_mc_allocator_driver_exit(void);

void fsl_mc_init_all_resource_pools(struct fsl_mc_device *mc_bus_dev);

void fsl_mc_cleanup_all_resource_pools(struct fsl_mc_device *mc_bus_dev);

int __must_check fsl_mc_resource_allocate(struct fsl_mc_bus *mc_bus,
					  enum fsl_mc_pool_type pool_type,
					  struct fsl_mc_resource
							  **new_resource);

void fsl_mc_resource_free(struct fsl_mc_resource *resource);

int fsl_mc_msi_domain_alloc_irqs(struct device *dev,
				 unsigned int irq_count);

void fsl_mc_msi_domain_free_irqs(struct device *dev);

int __init its_fsl_mc_msi_init(void);

void its_fsl_mc_msi_cleanup(void);

int fsl_mc_find_msi_domain(struct device *mc_platform_dev,
			   struct irq_domain **mc_msi_domain);

int fsl_mc_populate_irq_pool(struct fsl_mc_bus *mc_bus,
			     unsigned int irq_count);

void fsl_mc_cleanup_irq_pool(struct fsl_mc_bus *mc_bus);

int __must_check fsl_create_mc_io(struct device *dev,
				  phys_addr_t mc_portal_phys_addr,
				  u32 mc_portal_size,
				  struct fsl_mc_device *dpmcp_dev,
				  u32 flags, struct fsl_mc_io **new_mc_io);

void fsl_destroy_mc_io(struct fsl_mc_io *mc_io);

bool fsl_mc_is_root_dprc(struct device *dev);

#endif /* _FSL_MC_PRIVATE_H_ */
