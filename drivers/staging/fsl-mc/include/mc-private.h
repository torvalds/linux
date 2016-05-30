/*
 * Freescale Management Complex (MC) bus private declarations
 *
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 * Author: German Rivera <German.Rivera@freescale.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef _FSL_MC_PRIVATE_H_
#define _FSL_MC_PRIVATE_H_

#include "../include/mc.h"
#include <linux/mutex.h>
#include <linux/stringify.h>

#define FSL_MC_DPRC_DRIVER_NAME    "fsl_mc_dprc"

#define FSL_MC_DEVICE_MATCH(_mc_dev, _obj_desc) \
	(strcmp((_mc_dev)->obj_desc.type, (_obj_desc)->type) == 0 && \
	 (_mc_dev)->obj_desc.id == (_obj_desc)->id)

#define FSL_MC_IS_ALLOCATABLE(_obj_type) \
	(strcmp(_obj_type, "dpbp") == 0 || \
	 strcmp(_obj_type, "dpmcp") == 0 || \
	 strcmp(_obj_type, "dpcon") == 0)

struct irq_domain;
struct msi_domain_info;

/**
 * Maximum number of total IRQs that can be pre-allocated for an MC bus'
 * IRQ pool
 */
#define FSL_MC_IRQ_POOL_MAX_TOTAL_IRQS	256

struct device_node;
struct irq_domain;
struct msi_domain_info;

/**
 * struct fsl_mc - Private data of a "fsl,qoriq-mc" platform device
 * @root_mc_bus_dev: MC object device representing the root DPRC
 * @num_translation_ranges: number of entries in addr_translation_ranges
 * @translation_ranges: array of bus to system address translation ranges
 */
struct fsl_mc {
	struct fsl_mc_device *root_mc_bus_dev;
	u8 num_translation_ranges;
	struct fsl_mc_addr_translation_range *translation_ranges;
};

/**
 * struct fsl_mc_addr_translation_range - bus to system address translation
 * range
 * @mc_region_type: Type of MC region for the range being translated
 * @start_mc_offset: Start MC offset of the range being translated
 * @end_mc_offset: MC offset of the first byte after the range (last MC
 * offset of the range is end_mc_offset - 1)
 * @start_phys_addr: system physical address corresponding to start_mc_addr
 */
struct fsl_mc_addr_translation_range {
	enum dprc_region_type mc_region_type;
	u64 start_mc_offset;
	u64 end_mc_offset;
	phys_addr_t start_phys_addr;
};

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
	int16_t max_count;
	int16_t free_count;
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

int __must_check fsl_mc_device_add(struct dprc_obj_desc *obj_desc,
				   struct fsl_mc_io *mc_io,
				   struct device *parent_dev,
				   struct fsl_mc_device **new_mc_dev);

void fsl_mc_device_remove(struct fsl_mc_device *mc_dev);

int dprc_scan_container(struct fsl_mc_device *mc_bus_dev);

int dprc_scan_objects(struct fsl_mc_device *mc_bus_dev,
		      unsigned int *total_irq_count);

int __init dprc_driver_init(void);

void dprc_driver_exit(void);

int __init fsl_mc_allocator_driver_init(void);

void fsl_mc_allocator_driver_exit(void);

int __must_check fsl_mc_resource_allocate(struct fsl_mc_bus *mc_bus,
					  enum fsl_mc_pool_type pool_type,
					  struct fsl_mc_resource
							  **new_resource);

void fsl_mc_resource_free(struct fsl_mc_resource *resource);

struct irq_domain *fsl_mc_msi_create_irq_domain(struct fwnode_handle *fwnode,
						struct msi_domain_info *info,
						struct irq_domain *parent);

int fsl_mc_find_msi_domain(struct device *mc_platform_dev,
			   struct irq_domain **mc_msi_domain);

int fsl_mc_msi_domain_alloc_irqs(struct device *dev,
				 unsigned int irq_count);

void fsl_mc_msi_domain_free_irqs(struct device *dev);

int __init its_fsl_mc_msi_init(void);

void its_fsl_mc_msi_cleanup(void);

int fsl_mc_populate_irq_pool(struct fsl_mc_bus *mc_bus,
			     unsigned int irq_count);

void fsl_mc_cleanup_irq_pool(struct fsl_mc_bus *mc_bus);

#endif /* _FSL_MC_PRIVATE_H_ */
