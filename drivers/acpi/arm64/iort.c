/*
 * Copyright (C) 2016, Semihalf
 *	Author: Tomasz Nowicki <tn@semihalf.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * This file implements early detection/parsing of I/O mapping
 * reported to OS through firmware via I/O Remapping Table (IORT)
 * IORT document number: ARM DEN 0049A
 */

#define pr_fmt(fmt)	"ACPI: IORT: " fmt

#include <linux/acpi_iort.h>
#include <linux/iommu.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define IORT_TYPE_MASK(type)	(1 << (type))
#define IORT_MSI_TYPE		(1 << ACPI_IORT_NODE_ITS_GROUP)
#define IORT_IOMMU_TYPE		((1 << ACPI_IORT_NODE_SMMU) |	\
				(1 << ACPI_IORT_NODE_SMMU_V3))

struct iort_its_msi_chip {
	struct list_head	list;
	struct fwnode_handle	*fw_node;
	u32			translation_id;
};

struct iort_fwnode {
	struct list_head list;
	struct acpi_iort_node *iort_node;
	struct fwnode_handle *fwnode;
};
static LIST_HEAD(iort_fwnode_list);
static DEFINE_SPINLOCK(iort_fwnode_lock);

/**
 * iort_set_fwnode() - Create iort_fwnode and use it to register
 *		       iommu data in the iort_fwnode_list
 *
 * @node: IORT table node associated with the IOMMU
 * @fwnode: fwnode associated with the IORT node
 *
 * Returns: 0 on success
 *          <0 on failure
 */
static inline int iort_set_fwnode(struct acpi_iort_node *iort_node,
				  struct fwnode_handle *fwnode)
{
	struct iort_fwnode *np;

	np = kzalloc(sizeof(struct iort_fwnode), GFP_ATOMIC);

	if (WARN_ON(!np))
		return -ENOMEM;

	INIT_LIST_HEAD(&np->list);
	np->iort_node = iort_node;
	np->fwnode = fwnode;

	spin_lock(&iort_fwnode_lock);
	list_add_tail(&np->list, &iort_fwnode_list);
	spin_unlock(&iort_fwnode_lock);

	return 0;
}

/**
 * iort_get_fwnode() - Retrieve fwnode associated with an IORT node
 *
 * @node: IORT table node to be looked-up
 *
 * Returns: fwnode_handle pointer on success, NULL on failure
 */
static inline
struct fwnode_handle *iort_get_fwnode(struct acpi_iort_node *node)
{
	struct iort_fwnode *curr;
	struct fwnode_handle *fwnode = NULL;

	spin_lock(&iort_fwnode_lock);
	list_for_each_entry(curr, &iort_fwnode_list, list) {
		if (curr->iort_node == node) {
			fwnode = curr->fwnode;
			break;
		}
	}
	spin_unlock(&iort_fwnode_lock);

	return fwnode;
}

/**
 * iort_delete_fwnode() - Delete fwnode associated with an IORT node
 *
 * @node: IORT table node associated with fwnode to delete
 */
static inline void iort_delete_fwnode(struct acpi_iort_node *node)
{
	struct iort_fwnode *curr, *tmp;

	spin_lock(&iort_fwnode_lock);
	list_for_each_entry_safe(curr, tmp, &iort_fwnode_list, list) {
		if (curr->iort_node == node) {
			list_del(&curr->list);
			kfree(curr);
			break;
		}
	}
	spin_unlock(&iort_fwnode_lock);
}

typedef acpi_status (*iort_find_node_callback)
	(struct acpi_iort_node *node, void *context);

/* Root pointer to the mapped IORT table */
static struct acpi_table_header *iort_table;

static LIST_HEAD(iort_msi_chip_list);
static DEFINE_SPINLOCK(iort_msi_chip_lock);

/**
 * iort_register_domain_token() - register domain token and related ITS ID
 * to the list from where we can get it back later on.
 * @trans_id: ITS ID.
 * @fw_node: Domain token.
 *
 * Returns: 0 on success, -ENOMEM if no memory when allocating list element
 */
int iort_register_domain_token(int trans_id, struct fwnode_handle *fw_node)
{
	struct iort_its_msi_chip *its_msi_chip;

	its_msi_chip = kzalloc(sizeof(*its_msi_chip), GFP_KERNEL);
	if (!its_msi_chip)
		return -ENOMEM;

	its_msi_chip->fw_node = fw_node;
	its_msi_chip->translation_id = trans_id;

	spin_lock(&iort_msi_chip_lock);
	list_add(&its_msi_chip->list, &iort_msi_chip_list);
	spin_unlock(&iort_msi_chip_lock);

	return 0;
}

/**
 * iort_deregister_domain_token() - Deregister domain token based on ITS ID
 * @trans_id: ITS ID.
 *
 * Returns: none.
 */
void iort_deregister_domain_token(int trans_id)
{
	struct iort_its_msi_chip *its_msi_chip, *t;

	spin_lock(&iort_msi_chip_lock);
	list_for_each_entry_safe(its_msi_chip, t, &iort_msi_chip_list, list) {
		if (its_msi_chip->translation_id == trans_id) {
			list_del(&its_msi_chip->list);
			kfree(its_msi_chip);
			break;
		}
	}
	spin_unlock(&iort_msi_chip_lock);
}

/**
 * iort_find_domain_token() - Find domain token based on given ITS ID
 * @trans_id: ITS ID.
 *
 * Returns: domain token when find on the list, NULL otherwise
 */
struct fwnode_handle *iort_find_domain_token(int trans_id)
{
	struct fwnode_handle *fw_node = NULL;
	struct iort_its_msi_chip *its_msi_chip;

	spin_lock(&iort_msi_chip_lock);
	list_for_each_entry(its_msi_chip, &iort_msi_chip_list, list) {
		if (its_msi_chip->translation_id == trans_id) {
			fw_node = its_msi_chip->fw_node;
			break;
		}
	}
	spin_unlock(&iort_msi_chip_lock);

	return fw_node;
}

static struct acpi_iort_node *iort_scan_node(enum acpi_iort_node_type type,
					     iort_find_node_callback callback,
					     void *context)
{
	struct acpi_iort_node *iort_node, *iort_end;
	struct acpi_table_iort *iort;
	int i;

	if (!iort_table)
		return NULL;

	/* Get the first IORT node */
	iort = (struct acpi_table_iort *)iort_table;
	iort_node = ACPI_ADD_PTR(struct acpi_iort_node, iort,
				 iort->node_offset);
	iort_end = ACPI_ADD_PTR(struct acpi_iort_node, iort_table,
				iort_table->length);

	for (i = 0; i < iort->node_count; i++) {
		if (WARN_TAINT(iort_node >= iort_end, TAINT_FIRMWARE_WORKAROUND,
			       "IORT node pointer overflows, bad table!\n"))
			return NULL;

		if (iort_node->type == type &&
		    ACPI_SUCCESS(callback(iort_node, context)))
			return iort_node;

		iort_node = ACPI_ADD_PTR(struct acpi_iort_node, iort_node,
					 iort_node->length);
	}

	return NULL;
}

static acpi_status iort_match_node_callback(struct acpi_iort_node *node,
					    void *context)
{
	struct device *dev = context;
	acpi_status status = AE_NOT_FOUND;

	if (node->type == ACPI_IORT_NODE_NAMED_COMPONENT) {
		struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
		struct acpi_device *adev = to_acpi_device_node(dev->fwnode);
		struct acpi_iort_named_component *ncomp;

		if (!adev)
			goto out;

		status = acpi_get_name(adev->handle, ACPI_FULL_PATHNAME, &buf);
		if (ACPI_FAILURE(status)) {
			dev_warn(dev, "Can't get device full path name\n");
			goto out;
		}

		ncomp = (struct acpi_iort_named_component *)node->node_data;
		status = !strcmp(ncomp->device_name, buf.pointer) ?
							AE_OK : AE_NOT_FOUND;
		acpi_os_free(buf.pointer);
	} else if (node->type == ACPI_IORT_NODE_PCI_ROOT_COMPLEX) {
		struct acpi_iort_root_complex *pci_rc;
		struct pci_bus *bus;

		bus = to_pci_bus(dev);
		pci_rc = (struct acpi_iort_root_complex *)node->node_data;

		/*
		 * It is assumed that PCI segment numbers maps one-to-one
		 * with root complexes. Each segment number can represent only
		 * one root complex.
		 */
		status = pci_rc->pci_segment_number == pci_domain_nr(bus) ?
							AE_OK : AE_NOT_FOUND;
	}
out:
	return status;
}

static int iort_id_map(struct acpi_iort_id_mapping *map, u8 type, u32 rid_in,
		       u32 *rid_out)
{
	/* Single mapping does not care for input id */
	if (map->flags & ACPI_IORT_ID_SINGLE_MAPPING) {
		if (type == ACPI_IORT_NODE_NAMED_COMPONENT ||
		    type == ACPI_IORT_NODE_PCI_ROOT_COMPLEX) {
			*rid_out = map->output_base;
			return 0;
		}

		pr_warn(FW_BUG "[map %p] SINGLE MAPPING flag not allowed for node type %d, skipping ID map\n",
			map, type);
		return -ENXIO;
	}

	if (rid_in < map->input_base ||
	    (rid_in >= map->input_base + map->id_count))
		return -ENXIO;

	*rid_out = map->output_base + (rid_in - map->input_base);
	return 0;
}

static
struct acpi_iort_node *iort_node_get_id(struct acpi_iort_node *node,
					u32 *id_out, int index)
{
	struct acpi_iort_node *parent;
	struct acpi_iort_id_mapping *map;

	if (!node->mapping_offset || !node->mapping_count ||
				     index >= node->mapping_count)
		return NULL;

	map = ACPI_ADD_PTR(struct acpi_iort_id_mapping, node,
			   node->mapping_offset + index * sizeof(*map));

	/* Firmware bug! */
	if (!map->output_reference) {
		pr_err(FW_BUG "[node %p type %d] ID map has NULL parent reference\n",
		       node, node->type);
		return NULL;
	}

	parent = ACPI_ADD_PTR(struct acpi_iort_node, iort_table,
			       map->output_reference);

	if (map->flags & ACPI_IORT_ID_SINGLE_MAPPING) {
		if (node->type == ACPI_IORT_NODE_NAMED_COMPONENT ||
		    node->type == ACPI_IORT_NODE_PCI_ROOT_COMPLEX) {
			*id_out = map->output_base;
			return parent;
		}
	}

	return NULL;
}

static struct acpi_iort_node *iort_node_map_id(struct acpi_iort_node *node,
					       u32 id_in, u32 *id_out,
					       u8 type_mask)
{
	u32 id = id_in;

	/* Parse the ID mapping tree to find specified node type */
	while (node) {
		struct acpi_iort_id_mapping *map;
		int i;

		if (IORT_TYPE_MASK(node->type) & type_mask) {
			if (id_out)
				*id_out = id;
			return node;
		}

		if (!node->mapping_offset || !node->mapping_count)
			goto fail_map;

		map = ACPI_ADD_PTR(struct acpi_iort_id_mapping, node,
				   node->mapping_offset);

		/* Firmware bug! */
		if (!map->output_reference) {
			pr_err(FW_BUG "[node %p type %d] ID map has NULL parent reference\n",
			       node, node->type);
			goto fail_map;
		}

		/* Do the ID translation */
		for (i = 0; i < node->mapping_count; i++, map++) {
			if (!iort_id_map(map, node->type, id, &id))
				break;
		}

		if (i == node->mapping_count)
			goto fail_map;

		node = ACPI_ADD_PTR(struct acpi_iort_node, iort_table,
				    map->output_reference);
	}

fail_map:
	/* Map input ID to output ID unchanged on mapping failure */
	if (id_out)
		*id_out = id_in;

	return NULL;
}

static
struct acpi_iort_node *iort_node_map_platform_id(struct acpi_iort_node *node,
						 u32 *id_out, u8 type_mask,
						 int index)
{
	struct acpi_iort_node *parent;
	u32 id;

	/* step 1: retrieve the initial dev id */
	parent = iort_node_get_id(node, &id, index);
	if (!parent)
		return NULL;

	/*
	 * optional step 2: map the initial dev id if its parent is not
	 * the target type we want, map it again for the use cases such
	 * as NC (named component) -> SMMU -> ITS. If the type is matched,
	 * return the initial dev id and its parent pointer directly.
	 */
	if (!(IORT_TYPE_MASK(parent->type) & type_mask))
		parent = iort_node_map_id(parent, id, id_out, type_mask);
	else
		if (id_out)
			*id_out = id;

	return parent;
}

static struct acpi_iort_node *iort_find_dev_node(struct device *dev)
{
	struct pci_bus *pbus;

	if (!dev_is_pci(dev))
		return iort_scan_node(ACPI_IORT_NODE_NAMED_COMPONENT,
				      iort_match_node_callback, dev);

	/* Find a PCI root bus */
	pbus = to_pci_dev(dev)->bus;
	while (!pci_is_root_bus(pbus))
		pbus = pbus->parent;

	return iort_scan_node(ACPI_IORT_NODE_PCI_ROOT_COMPLEX,
			      iort_match_node_callback, &pbus->dev);
}

/**
 * iort_msi_map_rid() - Map a MSI requester ID for a device
 * @dev: The device for which the mapping is to be done.
 * @req_id: The device requester ID.
 *
 * Returns: mapped MSI RID on success, input requester ID otherwise
 */
u32 iort_msi_map_rid(struct device *dev, u32 req_id)
{
	struct acpi_iort_node *node;
	u32 dev_id;

	node = iort_find_dev_node(dev);
	if (!node)
		return req_id;

	iort_node_map_id(node, req_id, &dev_id, IORT_MSI_TYPE);
	return dev_id;
}

/**
 * iort_pmsi_get_dev_id() - Get the device id for a device
 * @dev: The device for which the mapping is to be done.
 * @dev_id: The device ID found.
 *
 * Returns: 0 for successful find a dev id, -ENODEV on error
 */
int iort_pmsi_get_dev_id(struct device *dev, u32 *dev_id)
{
	int i;
	struct acpi_iort_node *node;

	node = iort_find_dev_node(dev);
	if (!node)
		return -ENODEV;

	for (i = 0; i < node->mapping_count; i++) {
		if (iort_node_map_platform_id(node, dev_id, IORT_MSI_TYPE, i))
			return 0;
	}

	return -ENODEV;
}

/**
 * iort_dev_find_its_id() - Find the ITS identifier for a device
 * @dev: The device.
 * @req_id: Device's requester ID
 * @idx: Index of the ITS identifier list.
 * @its_id: ITS identifier.
 *
 * Returns: 0 on success, appropriate error value otherwise
 */
static int iort_dev_find_its_id(struct device *dev, u32 req_id,
				unsigned int idx, int *its_id)
{
	struct acpi_iort_its_group *its;
	struct acpi_iort_node *node;

	node = iort_find_dev_node(dev);
	if (!node)
		return -ENXIO;

	node = iort_node_map_id(node, req_id, NULL, IORT_MSI_TYPE);
	if (!node)
		return -ENXIO;

	/* Move to ITS specific data */
	its = (struct acpi_iort_its_group *)node->node_data;
	if (idx > its->its_count) {
		dev_err(dev, "requested ITS ID index [%d] is greater than available [%d]\n",
			idx, its->its_count);
		return -ENXIO;
	}

	*its_id = its->identifiers[idx];
	return 0;
}

/**
 * iort_get_device_domain() - Find MSI domain related to a device
 * @dev: The device.
 * @req_id: Requester ID for the device.
 *
 * Returns: the MSI domain for this device, NULL otherwise
 */
struct irq_domain *iort_get_device_domain(struct device *dev, u32 req_id)
{
	struct fwnode_handle *handle;
	int its_id;

	if (iort_dev_find_its_id(dev, req_id, 0, &its_id))
		return NULL;

	handle = iort_find_domain_token(its_id);
	if (!handle)
		return NULL;

	return irq_find_matching_fwnode(handle, DOMAIN_BUS_PCI_MSI);
}

/**
 * iort_get_platform_device_domain() - Find MSI domain related to a
 * platform device
 * @dev: the dev pointer associated with the platform device
 *
 * Returns: the MSI domain for this device, NULL otherwise
 */
static struct irq_domain *iort_get_platform_device_domain(struct device *dev)
{
	struct acpi_iort_node *node, *msi_parent;
	struct fwnode_handle *iort_fwnode;
	struct acpi_iort_its_group *its;
	int i;

	/* find its associated iort node */
	node = iort_scan_node(ACPI_IORT_NODE_NAMED_COMPONENT,
			      iort_match_node_callback, dev);
	if (!node)
		return NULL;

	/* then find its msi parent node */
	for (i = 0; i < node->mapping_count; i++) {
		msi_parent = iort_node_map_platform_id(node, NULL,
						       IORT_MSI_TYPE, i);
		if (msi_parent)
			break;
	}

	if (!msi_parent)
		return NULL;

	/* Move to ITS specific data */
	its = (struct acpi_iort_its_group *)msi_parent->node_data;

	iort_fwnode = iort_find_domain_token(its->identifiers[0]);
	if (!iort_fwnode)
		return NULL;

	return irq_find_matching_fwnode(iort_fwnode, DOMAIN_BUS_PLATFORM_MSI);
}

void acpi_configure_pmsi_domain(struct device *dev)
{
	struct irq_domain *msi_domain;

	msi_domain = iort_get_platform_device_domain(dev);
	if (msi_domain)
		dev_set_msi_domain(dev, msi_domain);
}

static int __get_pci_rid(struct pci_dev *pdev, u16 alias, void *data)
{
	u32 *rid = data;

	*rid = alias;
	return 0;
}

static int arm_smmu_iort_xlate(struct device *dev, u32 streamid,
			       struct fwnode_handle *fwnode,
			       const struct iommu_ops *ops)
{
	int ret = iommu_fwspec_init(dev, fwnode, ops);

	if (!ret)
		ret = iommu_fwspec_add_ids(dev, &streamid, 1);

	return ret;
}

static inline bool iort_iommu_driver_enabled(u8 type)
{
	switch (type) {
	case ACPI_IORT_NODE_SMMU_V3:
		return IS_BUILTIN(CONFIG_ARM_SMMU_V3);
	case ACPI_IORT_NODE_SMMU:
		return IS_BUILTIN(CONFIG_ARM_SMMU);
	default:
		pr_warn("IORT node type %u does not describe an SMMU\n", type);
		return false;
	}
}

#ifdef CONFIG_IOMMU_API
static inline
const struct iommu_ops *iort_fwspec_iommu_ops(struct iommu_fwspec *fwspec)
{
	return (fwspec && fwspec->ops) ? fwspec->ops : NULL;
}

static inline
int iort_add_device_replay(const struct iommu_ops *ops, struct device *dev)
{
	int err = 0;

	if (!IS_ERR_OR_NULL(ops) && ops->add_device && dev->bus &&
	    !dev->iommu_group)
		err = ops->add_device(dev);

	return err;
}
#else
static inline
const struct iommu_ops *iort_fwspec_iommu_ops(struct iommu_fwspec *fwspec)
{ return NULL; }
static inline
int iort_add_device_replay(const struct iommu_ops *ops, struct device *dev)
{ return 0; }
#endif

static const struct iommu_ops *iort_iommu_xlate(struct device *dev,
					struct acpi_iort_node *node,
					u32 streamid)
{
	const struct iommu_ops *ops = NULL;
	int ret = -ENODEV;
	struct fwnode_handle *iort_fwnode;

	if (node) {
		iort_fwnode = iort_get_fwnode(node);
		if (!iort_fwnode)
			return NULL;

		ops = iommu_ops_from_fwnode(iort_fwnode);
		/*
		 * If the ops look-up fails, this means that either
		 * the SMMU drivers have not been probed yet or that
		 * the SMMU drivers are not built in the kernel;
		 * Depending on whether the SMMU drivers are built-in
		 * in the kernel or not, defer the IOMMU configuration
		 * or just abort it.
		 */
		if (!ops)
			return iort_iommu_driver_enabled(node->type) ?
			       ERR_PTR(-EPROBE_DEFER) : NULL;

		ret = arm_smmu_iort_xlate(dev, streamid, iort_fwnode, ops);
	}

	return ret ? NULL : ops;
}

/**
 * iort_set_dma_mask - Set-up dma mask for a device.
 *
 * @dev: device to configure
 */
void iort_set_dma_mask(struct device *dev)
{
	/*
	 * Set default coherent_dma_mask to 32 bit.  Drivers are expected to
	 * setup the correct supported mask.
	 */
	if (!dev->coherent_dma_mask)
		dev->coherent_dma_mask = DMA_BIT_MASK(32);

	/*
	 * Set it to coherent_dma_mask by default if the architecture
	 * code has not set it.
	 */
	if (!dev->dma_mask)
		dev->dma_mask = &dev->coherent_dma_mask;
}

/**
 * iort_iommu_configure - Set-up IOMMU configuration for a device.
 *
 * @dev: device to configure
 *
 * Returns: iommu_ops pointer on configuration success
 *          NULL on configuration failure
 */
const struct iommu_ops *iort_iommu_configure(struct device *dev)
{
	struct acpi_iort_node *node, *parent;
	const struct iommu_ops *ops = NULL;
	u32 streamid = 0;
	int err;

	/*
	 * If we already translated the fwspec there
	 * is nothing left to do, return the iommu_ops.
	 */
	ops = iort_fwspec_iommu_ops(dev->iommu_fwspec);
	if (ops)
		return ops;

	if (dev_is_pci(dev)) {
		struct pci_bus *bus = to_pci_dev(dev)->bus;
		u32 rid;

		pci_for_each_dma_alias(to_pci_dev(dev), __get_pci_rid,
				       &rid);

		node = iort_scan_node(ACPI_IORT_NODE_PCI_ROOT_COMPLEX,
				      iort_match_node_callback, &bus->dev);
		if (!node)
			return NULL;

		parent = iort_node_map_id(node, rid, &streamid,
					  IORT_IOMMU_TYPE);

		ops = iort_iommu_xlate(dev, parent, streamid);

	} else {
		int i = 0;

		node = iort_scan_node(ACPI_IORT_NODE_NAMED_COMPONENT,
				      iort_match_node_callback, dev);
		if (!node)
			return NULL;

		parent = iort_node_map_platform_id(node, &streamid,
						   IORT_IOMMU_TYPE, i++);

		while (parent) {
			ops = iort_iommu_xlate(dev, parent, streamid);
			if (IS_ERR_OR_NULL(ops))
				return ops;

			parent = iort_node_map_platform_id(node, &streamid,
							   IORT_IOMMU_TYPE,
							   i++);
		}
	}

	/*
	 * If we have reason to believe the IOMMU driver missed the initial
	 * add_device callback for dev, replay it to get things in order.
	 */
	err = iort_add_device_replay(ops, dev);
	if (err)
		ops = ERR_PTR(err);

	/* Ignore all other errors apart from EPROBE_DEFER */
	if (IS_ERR(ops) && (PTR_ERR(ops) != -EPROBE_DEFER)) {
		dev_dbg(dev, "Adding to IOMMU failed: %ld\n", PTR_ERR(ops));
		ops = NULL;
	}

	return ops;
}

static void __init acpi_iort_register_irq(int hwirq, const char *name,
					  int trigger,
					  struct resource *res)
{
	int irq = acpi_register_gsi(NULL, hwirq, trigger,
				    ACPI_ACTIVE_HIGH);

	if (irq <= 0) {
		pr_err("could not register gsi hwirq %d name [%s]\n", hwirq,
								      name);
		return;
	}

	res->start = irq;
	res->end = irq;
	res->flags = IORESOURCE_IRQ;
	res->name = name;
}

static int __init arm_smmu_v3_count_resources(struct acpi_iort_node *node)
{
	struct acpi_iort_smmu_v3 *smmu;
	/* Always present mem resource */
	int num_res = 1;

	/* Retrieve SMMUv3 specific data */
	smmu = (struct acpi_iort_smmu_v3 *)node->node_data;

	if (smmu->event_gsiv)
		num_res++;

	if (smmu->pri_gsiv)
		num_res++;

	if (smmu->gerr_gsiv)
		num_res++;

	if (smmu->sync_gsiv)
		num_res++;

	return num_res;
}

static void __init arm_smmu_v3_init_resources(struct resource *res,
					      struct acpi_iort_node *node)
{
	struct acpi_iort_smmu_v3 *smmu;
	int num_res = 0;

	/* Retrieve SMMUv3 specific data */
	smmu = (struct acpi_iort_smmu_v3 *)node->node_data;

	res[num_res].start = smmu->base_address;
	res[num_res].end = smmu->base_address + SZ_128K - 1;
	res[num_res].flags = IORESOURCE_MEM;

	num_res++;

	if (smmu->event_gsiv)
		acpi_iort_register_irq(smmu->event_gsiv, "eventq",
				       ACPI_EDGE_SENSITIVE,
				       &res[num_res++]);

	if (smmu->pri_gsiv)
		acpi_iort_register_irq(smmu->pri_gsiv, "priq",
				       ACPI_EDGE_SENSITIVE,
				       &res[num_res++]);

	if (smmu->gerr_gsiv)
		acpi_iort_register_irq(smmu->gerr_gsiv, "gerror",
				       ACPI_EDGE_SENSITIVE,
				       &res[num_res++]);

	if (smmu->sync_gsiv)
		acpi_iort_register_irq(smmu->sync_gsiv, "cmdq-sync",
				       ACPI_EDGE_SENSITIVE,
				       &res[num_res++]);
}

static bool __init arm_smmu_v3_is_coherent(struct acpi_iort_node *node)
{
	struct acpi_iort_smmu_v3 *smmu;

	/* Retrieve SMMUv3 specific data */
	smmu = (struct acpi_iort_smmu_v3 *)node->node_data;

	return smmu->flags & ACPI_IORT_SMMU_V3_COHACC_OVERRIDE;
}

static int __init arm_smmu_count_resources(struct acpi_iort_node *node)
{
	struct acpi_iort_smmu *smmu;

	/* Retrieve SMMU specific data */
	smmu = (struct acpi_iort_smmu *)node->node_data;

	/*
	 * Only consider the global fault interrupt and ignore the
	 * configuration access interrupt.
	 *
	 * MMIO address and global fault interrupt resources are always
	 * present so add them to the context interrupt count as a static
	 * value.
	 */
	return smmu->context_interrupt_count + 2;
}

static void __init arm_smmu_init_resources(struct resource *res,
					   struct acpi_iort_node *node)
{
	struct acpi_iort_smmu *smmu;
	int i, hw_irq, trigger, num_res = 0;
	u64 *ctx_irq, *glb_irq;

	/* Retrieve SMMU specific data */
	smmu = (struct acpi_iort_smmu *)node->node_data;

	res[num_res].start = smmu->base_address;
	res[num_res].end = smmu->base_address + smmu->span - 1;
	res[num_res].flags = IORESOURCE_MEM;
	num_res++;

	glb_irq = ACPI_ADD_PTR(u64, node, smmu->global_interrupt_offset);
	/* Global IRQs */
	hw_irq = IORT_IRQ_MASK(glb_irq[0]);
	trigger = IORT_IRQ_TRIGGER_MASK(glb_irq[0]);

	acpi_iort_register_irq(hw_irq, "arm-smmu-global", trigger,
				     &res[num_res++]);

	/* Context IRQs */
	ctx_irq = ACPI_ADD_PTR(u64, node, smmu->context_interrupt_offset);
	for (i = 0; i < smmu->context_interrupt_count; i++) {
		hw_irq = IORT_IRQ_MASK(ctx_irq[i]);
		trigger = IORT_IRQ_TRIGGER_MASK(ctx_irq[i]);

		acpi_iort_register_irq(hw_irq, "arm-smmu-context", trigger,
				       &res[num_res++]);
	}
}

static bool __init arm_smmu_is_coherent(struct acpi_iort_node *node)
{
	struct acpi_iort_smmu *smmu;

	/* Retrieve SMMU specific data */
	smmu = (struct acpi_iort_smmu *)node->node_data;

	return smmu->flags & ACPI_IORT_SMMU_COHERENT_WALK;
}

struct iort_iommu_config {
	const char *name;
	int (*iommu_init)(struct acpi_iort_node *node);
	bool (*iommu_is_coherent)(struct acpi_iort_node *node);
	int (*iommu_count_resources)(struct acpi_iort_node *node);
	void (*iommu_init_resources)(struct resource *res,
				     struct acpi_iort_node *node);
};

static const struct iort_iommu_config iort_arm_smmu_v3_cfg __initconst = {
	.name = "arm-smmu-v3",
	.iommu_is_coherent = arm_smmu_v3_is_coherent,
	.iommu_count_resources = arm_smmu_v3_count_resources,
	.iommu_init_resources = arm_smmu_v3_init_resources
};

static const struct iort_iommu_config iort_arm_smmu_cfg __initconst = {
	.name = "arm-smmu",
	.iommu_is_coherent = arm_smmu_is_coherent,
	.iommu_count_resources = arm_smmu_count_resources,
	.iommu_init_resources = arm_smmu_init_resources
};

static __init
const struct iort_iommu_config *iort_get_iommu_cfg(struct acpi_iort_node *node)
{
	switch (node->type) {
	case ACPI_IORT_NODE_SMMU_V3:
		return &iort_arm_smmu_v3_cfg;
	case ACPI_IORT_NODE_SMMU:
		return &iort_arm_smmu_cfg;
	default:
		return NULL;
	}
}

/**
 * iort_add_smmu_platform_device() - Allocate a platform device for SMMU
 * @node: Pointer to SMMU ACPI IORT node
 *
 * Returns: 0 on success, <0 failure
 */
static int __init iort_add_smmu_platform_device(struct acpi_iort_node *node)
{
	struct fwnode_handle *fwnode;
	struct platform_device *pdev;
	struct resource *r;
	enum dev_dma_attr attr;
	int ret, count;
	const struct iort_iommu_config *ops = iort_get_iommu_cfg(node);

	if (!ops)
		return -ENODEV;

	pdev = platform_device_alloc(ops->name, PLATFORM_DEVID_AUTO);
	if (!pdev)
		return -ENOMEM;

	count = ops->iommu_count_resources(node);

	r = kcalloc(count, sizeof(*r), GFP_KERNEL);
	if (!r) {
		ret = -ENOMEM;
		goto dev_put;
	}

	ops->iommu_init_resources(r, node);

	ret = platform_device_add_resources(pdev, r, count);
	/*
	 * Resources are duplicated in platform_device_add_resources,
	 * free their allocated memory
	 */
	kfree(r);

	if (ret)
		goto dev_put;

	/*
	 * Add a copy of IORT node pointer to platform_data to
	 * be used to retrieve IORT data information.
	 */
	ret = platform_device_add_data(pdev, &node, sizeof(node));
	if (ret)
		goto dev_put;

	/*
	 * We expect the dma masks to be equivalent for
	 * all SMMUs set-ups
	 */
	pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

	fwnode = iort_get_fwnode(node);

	if (!fwnode) {
		ret = -ENODEV;
		goto dev_put;
	}

	pdev->dev.fwnode = fwnode;

	attr = ops->iommu_is_coherent(node) ?
			     DEV_DMA_COHERENT : DEV_DMA_NON_COHERENT;

	/* Configure DMA for the page table walker */
	acpi_dma_configure(&pdev->dev, attr);

	ret = platform_device_add(pdev);
	if (ret)
		goto dma_deconfigure;

	return 0;

dma_deconfigure:
	acpi_dma_deconfigure(&pdev->dev);
dev_put:
	platform_device_put(pdev);

	return ret;
}

static void __init iort_init_platform_devices(void)
{
	struct acpi_iort_node *iort_node, *iort_end;
	struct acpi_table_iort *iort;
	struct fwnode_handle *fwnode;
	int i, ret;

	/*
	 * iort_table and iort both point to the start of IORT table, but
	 * have different struct types
	 */
	iort = (struct acpi_table_iort *)iort_table;

	/* Get the first IORT node */
	iort_node = ACPI_ADD_PTR(struct acpi_iort_node, iort,
				 iort->node_offset);
	iort_end = ACPI_ADD_PTR(struct acpi_iort_node, iort,
				iort_table->length);

	for (i = 0; i < iort->node_count; i++) {
		if (iort_node >= iort_end) {
			pr_err("iort node pointer overflows, bad table\n");
			return;
		}

		if ((iort_node->type == ACPI_IORT_NODE_SMMU) ||
			(iort_node->type == ACPI_IORT_NODE_SMMU_V3)) {

			fwnode = acpi_alloc_fwnode_static();
			if (!fwnode)
				return;

			iort_set_fwnode(iort_node, fwnode);

			ret = iort_add_smmu_platform_device(iort_node);
			if (ret) {
				iort_delete_fwnode(iort_node);
				acpi_free_fwnode_static(fwnode);
				return;
			}
		}

		iort_node = ACPI_ADD_PTR(struct acpi_iort_node, iort_node,
					 iort_node->length);
	}
}

void __init acpi_iort_init(void)
{
	acpi_status status;

	status = acpi_get_table(ACPI_SIG_IORT, 0, &iort_table);
	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND) {
			const char *msg = acpi_format_exception(status);

			pr_err("Failed to get table, %s\n", msg);
		}

		return;
	}

	iort_init_platform_devices();
}
