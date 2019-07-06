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
	phys_addr_t		base_addr;
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
static inline struct fwnode_handle *iort_get_fwnode(
			struct acpi_iort_node *node)
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

/**
 * iort_get_iort_node() - Retrieve iort_node associated with an fwnode
 *
 * @fwnode: fwnode associated with device to be looked-up
 *
 * Returns: iort_node pointer on success, NULL on failure
 */
static inline struct acpi_iort_node *iort_get_iort_node(
			struct fwnode_handle *fwnode)
{
	struct iort_fwnode *curr;
	struct acpi_iort_node *iort_node = NULL;

	spin_lock(&iort_fwnode_lock);
	list_for_each_entry(curr, &iort_fwnode_list, list) {
		if (curr->fwnode == fwnode) {
			iort_node = curr->iort_node;
			break;
		}
	}
	spin_unlock(&iort_fwnode_lock);

	return iort_node;
}

typedef acpi_status (*iort_find_node_callback)
	(struct acpi_iort_node *node, void *context);

/* Root pointer to the mapped IORT table */
static struct acpi_table_header *iort_table;

static LIST_HEAD(iort_msi_chip_list);
static DEFINE_SPINLOCK(iort_msi_chip_lock);

/**
 * iort_register_domain_token() - register domain token along with related
 * ITS ID and base address to the list from where we can get it back later on.
 * @trans_id: ITS ID.
 * @base: ITS base address.
 * @fw_node: Domain token.
 *
 * Returns: 0 on success, -ENOMEM if no memory when allocating list element
 */
int iort_register_domain_token(int trans_id, phys_addr_t base,
			       struct fwnode_handle *fw_node)
{
	struct iort_its_msi_chip *its_msi_chip;

	its_msi_chip = kzalloc(sizeof(*its_msi_chip), GFP_KERNEL);
	if (!its_msi_chip)
		return -ENOMEM;

	its_msi_chip->fw_node = fw_node;
	its_msi_chip->translation_id = trans_id;
	its_msi_chip->base_addr = base;

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

static struct acpi_iort_node *iort_node_get_id(struct acpi_iort_node *node,
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
		    node->type == ACPI_IORT_NODE_PCI_ROOT_COMPLEX ||
		    node->type == ACPI_IORT_NODE_SMMU_V3 ||
		    node->type == ACPI_IORT_NODE_PMCG) {
			*id_out = map->output_base;
			return parent;
		}
	}

	return NULL;
}

static int iort_get_id_mapping_index(struct acpi_iort_node *node)
{
	struct acpi_iort_smmu_v3 *smmu;

	switch (node->type) {
	case ACPI_IORT_NODE_SMMU_V3:
		/*
		 * SMMUv3 dev ID mapping index was introduced in revision 1
		 * table, not available in revision 0
		 */
		if (node->revision < 1)
			return -EINVAL;

		smmu = (struct acpi_iort_smmu_v3 *)node->node_data;
		/*
		 * ID mapping index is only ignored if all interrupts are
		 * GSIV based
		 */
		if (smmu->event_gsiv && smmu->pri_gsiv && smmu->gerr_gsiv
		    && smmu->sync_gsiv)
			return -EINVAL;

		if (smmu->id_mapping_index >= node->mapping_count) {
			pr_err(FW_BUG "[node %p type %d] ID mapping index overflows valid mappings\n",
			       node, node->type);
			return -EINVAL;
		}

		return smmu->id_mapping_index;
	case ACPI_IORT_NODE_PMCG:
		return 0;
	default:
		return -EINVAL;
	}
}

static struct acpi_iort_node *iort_node_map_id(struct acpi_iort_node *node,
					       u32 id_in, u32 *id_out,
					       u8 type_mask)
{
	u32 id = id_in;

	/* Parse the ID mapping tree to find specified node type */
	while (node) {
		struct acpi_iort_id_mapping *map;
		int i, index;

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

		/*
		 * Get the special ID mapping index (if any) and skip its
		 * associated ID map to prevent erroneous multi-stage
		 * IORT ID translations.
		 */
		index = iort_get_id_mapping_index(node);

		/* Do the ID translation */
		for (i = 0; i < node->mapping_count; i++, map++) {
			/* if it is special mapping index, skip it */
			if (i == index)
				continue;

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

static struct acpi_iort_node *iort_node_map_platform_id(
		struct acpi_iort_node *node, u32 *id_out, u8 type_mask,
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

	if (!dev_is_pci(dev)) {
		struct acpi_iort_node *node;
		/*
		 * scan iort_fwnode_list to see if it's an iort platform
		 * device (such as SMMU, PMCG),its iort node already cached
		 * and associated with fwnode when iort platform devices
		 * were initialized.
		 */
		node = iort_get_iort_node(dev->fwnode);
		if (node)
			return node;

		/*
		 * if not, then it should be a platform device defined in
		 * DSDT/SSDT (with Named Component node in IORT)
		 */
		return iort_scan_node(ACPI_IORT_NODE_NAMED_COMPONENT,
				      iort_match_node_callback, dev);
	}

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
	int i, index;
	struct acpi_iort_node *node;

	node = iort_find_dev_node(dev);
	if (!node)
		return -ENODEV;

	index = iort_get_id_mapping_index(node);
	/* if there is a valid index, go get the dev_id directly */
	if (index >= 0) {
		if (iort_node_get_id(node, dev_id, index))
			return 0;
	} else {
		for (i = 0; i < node->mapping_count; i++) {
			if (iort_node_map_platform_id(node, dev_id,
						      IORT_MSI_TYPE, i))
				return 0;
		}
	}

	return -ENODEV;
}

static int __maybe_unused iort_find_its_base(u32 its_id, phys_addr_t *base)
{
	struct iort_its_msi_chip *its_msi_chip;
	int ret = -ENODEV;

	spin_lock(&iort_msi_chip_lock);
	list_for_each_entry(its_msi_chip, &iort_msi_chip_list, list) {
		if (its_msi_chip->translation_id == its_id) {
			*base = its_msi_chip->base_addr;
			ret = 0;
			break;
		}
	}
	spin_unlock(&iort_msi_chip_lock);

	return ret;
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

static void iort_set_device_domain(struct device *dev,
				   struct acpi_iort_node *node)
{
	struct acpi_iort_its_group *its;
	struct acpi_iort_node *msi_parent;
	struct acpi_iort_id_mapping *map;
	struct fwnode_handle *iort_fwnode;
	struct irq_domain *domain;
	int index;

	index = iort_get_id_mapping_index(node);
	if (index < 0)
		return;

	map = ACPI_ADD_PTR(struct acpi_iort_id_mapping, node,
			   node->mapping_offset + index * sizeof(*map));

	/* Firmware bug! */
	if (!map->output_reference ||
	    !(map->flags & ACPI_IORT_ID_SINGLE_MAPPING)) {
		pr_err(FW_BUG "[node %p type %d] Invalid MSI mapping\n",
		       node, node->type);
		return;
	}

	msi_parent = ACPI_ADD_PTR(struct acpi_iort_node, iort_table,
				  map->output_reference);

	if (!msi_parent || msi_parent->type != ACPI_IORT_NODE_ITS_GROUP)
		return;

	/* Move to ITS specific data */
	its = (struct acpi_iort_its_group *)msi_parent->node_data;

	iort_fwnode = iort_find_domain_token(its->identifiers[0]);
	if (!iort_fwnode)
		return;

	domain = irq_find_matching_fwnode(iort_fwnode, DOMAIN_BUS_PLATFORM_MSI);
	if (domain)
		dev_set_msi_domain(dev, domain);
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
	struct acpi_iort_node *node, *msi_parent = NULL;
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

static int __maybe_unused __get_pci_rid(struct pci_dev *pdev, u16 alias,
					void *data)
{
	u32 *rid = data;

	*rid = alias;
	return 0;
}

#ifdef CONFIG_IOMMU_API
static struct acpi_iort_node *iort_get_msi_resv_iommu(struct device *dev)
{
	struct acpi_iort_node *iommu;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	iommu = iort_get_iort_node(fwspec->iommu_fwnode);

	if (iommu && (iommu->type == ACPI_IORT_NODE_SMMU_V3)) {
		struct acpi_iort_smmu_v3 *smmu;

		smmu = (struct acpi_iort_smmu_v3 *)iommu->node_data;
		if (smmu->model == ACPI_IORT_SMMU_V3_HISILICON_HI161X)
			return iommu;
	}

	return NULL;
}

static inline const struct iommu_ops *iort_fwspec_iommu_ops(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	return (fwspec && fwspec->ops) ? fwspec->ops : NULL;
}

static inline int iort_add_device_replay(const struct iommu_ops *ops,
					 struct device *dev)
{
	int err = 0;

	if (dev->bus && !device_iommu_mapped(dev))
		err = iommu_probe_device(dev);

	return err;
}

/**
 * iort_iommu_msi_get_resv_regions - Reserved region driver helper
 * @dev: Device from iommu_get_resv_regions()
 * @head: Reserved region list from iommu_get_resv_regions()
 *
 * Returns: Number of msi reserved regions on success (0 if platform
 *          doesn't require the reservation or no associated msi regions),
 *          appropriate error value otherwise. The ITS interrupt translation
 *          spaces (ITS_base + SZ_64K, SZ_64K) associated with the device
 *          are the msi reserved regions.
 */
int iort_iommu_msi_get_resv_regions(struct device *dev, struct list_head *head)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct acpi_iort_its_group *its;
	struct acpi_iort_node *iommu_node, *its_node = NULL;
	int i, resv = 0;

	iommu_node = iort_get_msi_resv_iommu(dev);
	if (!iommu_node)
		return 0;

	/*
	 * Current logic to reserve ITS regions relies on HW topologies
	 * where a given PCI or named component maps its IDs to only one
	 * ITS group; if a PCI or named component can map its IDs to
	 * different ITS groups through IORT mappings this function has
	 * to be reworked to ensure we reserve regions for all ITS groups
	 * a given PCI or named component may map IDs to.
	 */

	for (i = 0; i < fwspec->num_ids; i++) {
		its_node = iort_node_map_id(iommu_node,
					fwspec->ids[i],
					NULL, IORT_MSI_TYPE);
		if (its_node)
			break;
	}

	if (!its_node)
		return 0;

	/* Move to ITS specific data */
	its = (struct acpi_iort_its_group *)its_node->node_data;

	for (i = 0; i < its->its_count; i++) {
		phys_addr_t base;

		if (!iort_find_its_base(its->identifiers[i], &base)) {
			int prot = IOMMU_WRITE | IOMMU_NOEXEC | IOMMU_MMIO;
			struct iommu_resv_region *region;

			region = iommu_alloc_resv_region(base + SZ_64K, SZ_64K,
							 prot, IOMMU_RESV_MSI);
			if (region) {
				list_add_tail(&region->list, head);
				resv++;
			}
		}
	}

	return (resv == its->its_count) ? resv : -ENODEV;
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

static int arm_smmu_iort_xlate(struct device *dev, u32 streamid,
			       struct fwnode_handle *fwnode,
			       const struct iommu_ops *ops)
{
	int ret = iommu_fwspec_init(dev, fwnode, ops);

	if (!ret)
		ret = iommu_fwspec_add_ids(dev, &streamid, 1);

	return ret;
}

static bool iort_pci_rc_supports_ats(struct acpi_iort_node *node)
{
	struct acpi_iort_root_complex *pci_rc;

	pci_rc = (struct acpi_iort_root_complex *)node->node_data;
	return pci_rc->ats_attribute & ACPI_IORT_ATS_SUPPORTED;
}

static int iort_iommu_xlate(struct device *dev, struct acpi_iort_node *node,
			    u32 streamid)
{
	const struct iommu_ops *ops;
	struct fwnode_handle *iort_fwnode;

	if (!node)
		return -ENODEV;

	iort_fwnode = iort_get_fwnode(node);
	if (!iort_fwnode)
		return -ENODEV;

	/*
	 * If the ops look-up fails, this means that either
	 * the SMMU drivers have not been probed yet or that
	 * the SMMU drivers are not built in the kernel;
	 * Depending on whether the SMMU drivers are built-in
	 * in the kernel or not, defer the IOMMU configuration
	 * or just abort it.
	 */
	ops = iommu_ops_from_fwnode(iort_fwnode);
	if (!ops)
		return iort_iommu_driver_enabled(node->type) ?
		       -EPROBE_DEFER : -ENODEV;

	return arm_smmu_iort_xlate(dev, streamid, iort_fwnode, ops);
}

struct iort_pci_alias_info {
	struct device *dev;
	struct acpi_iort_node *node;
};

static int iort_pci_iommu_init(struct pci_dev *pdev, u16 alias, void *data)
{
	struct iort_pci_alias_info *info = data;
	struct acpi_iort_node *parent;
	u32 streamid;

	parent = iort_node_map_id(info->node, alias, &streamid,
				  IORT_IOMMU_TYPE);
	return iort_iommu_xlate(info->dev, parent, streamid);
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
	const struct iommu_ops *ops;
	u32 streamid = 0;
	int err = -ENODEV;

	/*
	 * If we already translated the fwspec there
	 * is nothing left to do, return the iommu_ops.
	 */
	ops = iort_fwspec_iommu_ops(dev);
	if (ops)
		return ops;

	if (dev_is_pci(dev)) {
		struct pci_bus *bus = to_pci_dev(dev)->bus;
		struct iort_pci_alias_info info = { .dev = dev };

		node = iort_scan_node(ACPI_IORT_NODE_PCI_ROOT_COMPLEX,
				      iort_match_node_callback, &bus->dev);
		if (!node)
			return NULL;

		info.node = node;
		err = pci_for_each_dma_alias(to_pci_dev(dev),
					     iort_pci_iommu_init, &info);

		if (!err && iort_pci_rc_supports_ats(node))
			dev->iommu_fwspec->flags |= IOMMU_FWSPEC_PCI_RC_ATS;
	} else {
		int i = 0;

		node = iort_scan_node(ACPI_IORT_NODE_NAMED_COMPONENT,
				      iort_match_node_callback, dev);
		if (!node)
			return NULL;

		do {
			parent = iort_node_map_platform_id(node, &streamid,
							   IORT_IOMMU_TYPE,
							   i++);

			if (parent)
				err = iort_iommu_xlate(dev, parent, streamid);
		} while (parent && !err);
	}

	/*
	 * If we have reason to believe the IOMMU driver missed the initial
	 * add_device callback for dev, replay it to get things in order.
	 */
	if (!err) {
		ops = iort_fwspec_iommu_ops(dev);
		err = iort_add_device_replay(ops, dev);
	}

	/* Ignore all other errors apart from EPROBE_DEFER */
	if (err == -EPROBE_DEFER) {
		ops = ERR_PTR(err);
	} else if (err) {
		dev_dbg(dev, "Adding to IOMMU failed: %d\n", err);
		ops = NULL;
	}

	return ops;
}
#else
static inline const struct iommu_ops *iort_fwspec_iommu_ops(struct device *dev)
{ return NULL; }
static inline int iort_add_device_replay(const struct iommu_ops *ops,
					 struct device *dev)
{ return 0; }
int iort_iommu_msi_get_resv_regions(struct device *dev, struct list_head *head)
{ return 0; }
const struct iommu_ops *iort_iommu_configure(struct device *dev)
{ return NULL; }
#endif

static int nc_dma_get_range(struct device *dev, u64 *size)
{
	struct acpi_iort_node *node;
	struct acpi_iort_named_component *ncomp;

	node = iort_scan_node(ACPI_IORT_NODE_NAMED_COMPONENT,
			      iort_match_node_callback, dev);
	if (!node)
		return -ENODEV;

	ncomp = (struct acpi_iort_named_component *)node->node_data;

	*size = ncomp->memory_address_limit >= 64 ? U64_MAX :
			1ULL<<ncomp->memory_address_limit;

	return 0;
}

static int rc_dma_get_range(struct device *dev, u64 *size)
{
	struct acpi_iort_node *node;
	struct acpi_iort_root_complex *rc;
	struct pci_bus *pbus = to_pci_dev(dev)->bus;

	node = iort_scan_node(ACPI_IORT_NODE_PCI_ROOT_COMPLEX,
			      iort_match_node_callback, &pbus->dev);
	if (!node || node->revision < 1)
		return -ENODEV;

	rc = (struct acpi_iort_root_complex *)node->node_data;

	*size = rc->memory_address_limit >= 64 ? U64_MAX :
			1ULL<<rc->memory_address_limit;

	return 0;
}

/**
 * iort_dma_setup() - Set-up device DMA parameters.
 *
 * @dev: device to configure
 * @dma_addr: device DMA address result pointer
 * @size: DMA range size result pointer
 */
void iort_dma_setup(struct device *dev, u64 *dma_addr, u64 *dma_size)
{
	u64 mask, dmaaddr = 0, size = 0, offset = 0;
	int ret, msb;

	/*
	 * If @dev is expected to be DMA-capable then the bus code that created
	 * it should have initialised its dma_mask pointer by this point. For
	 * now, we'll continue the legacy behaviour of coercing it to the
	 * coherent mask if not, but we'll no longer do so quietly.
	 */
	if (!dev->dma_mask) {
		dev_warn(dev, "DMA mask not set\n");
		dev->dma_mask = &dev->coherent_dma_mask;
	}

	if (dev->coherent_dma_mask)
		size = max(dev->coherent_dma_mask, dev->coherent_dma_mask + 1);
	else
		size = 1ULL << 32;

	if (dev_is_pci(dev)) {
		ret = acpi_dma_get_range(dev, &dmaaddr, &offset, &size);
		if (ret == -ENODEV)
			ret = rc_dma_get_range(dev, &size);
	} else {
		ret = nc_dma_get_range(dev, &size);
	}

	if (!ret) {
		msb = fls64(dmaaddr + size - 1);
		/*
		 * Round-up to the power-of-two mask or set
		 * the mask to the whole 64-bit address space
		 * in case the DMA region covers the full
		 * memory window.
		 */
		mask = msb == 64 ? U64_MAX : (1ULL << msb) - 1;
		/*
		 * Limit coherent and dma mask based on size
		 * retrieved from firmware.
		 */
		dev->bus_dma_mask = mask;
		dev->coherent_dma_mask = mask;
		*dev->dma_mask = mask;
	}

	*dma_addr = dmaaddr;
	*dma_size = size;

	dev->dma_pfn_offset = PFN_DOWN(offset);
	dev_dbg(dev, "dma_pfn_offset(%#08llx)\n", offset);
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

static bool arm_smmu_v3_is_combined_irq(struct acpi_iort_smmu_v3 *smmu)
{
	/*
	 * Cavium ThunderX2 implementation doesn't not support unique
	 * irq line. Use single irq line for all the SMMUv3 interrupts.
	 */
	if (smmu->model != ACPI_IORT_SMMU_V3_CAVIUM_CN99XX)
		return false;

	/*
	 * ThunderX2 doesn't support MSIs from the SMMU, so we're checking
	 * SPI numbers here.
	 */
	return smmu->event_gsiv == smmu->pri_gsiv &&
	       smmu->event_gsiv == smmu->gerr_gsiv &&
	       smmu->event_gsiv == smmu->sync_gsiv;
}

static unsigned long arm_smmu_v3_resource_size(struct acpi_iort_smmu_v3 *smmu)
{
	/*
	 * Override the size, for Cavium ThunderX2 implementation
	 * which doesn't support the page 1 SMMU register space.
	 */
	if (smmu->model == ACPI_IORT_SMMU_V3_CAVIUM_CN99XX)
		return SZ_64K;

	return SZ_128K;
}

static void __init arm_smmu_v3_init_resources(struct resource *res,
					      struct acpi_iort_node *node)
{
	struct acpi_iort_smmu_v3 *smmu;
	int num_res = 0;

	/* Retrieve SMMUv3 specific data */
	smmu = (struct acpi_iort_smmu_v3 *)node->node_data;

	res[num_res].start = smmu->base_address;
	res[num_res].end = smmu->base_address +
				arm_smmu_v3_resource_size(smmu) - 1;
	res[num_res].flags = IORESOURCE_MEM;

	num_res++;
	if (arm_smmu_v3_is_combined_irq(smmu)) {
		if (smmu->event_gsiv)
			acpi_iort_register_irq(smmu->event_gsiv, "combined",
					       ACPI_EDGE_SENSITIVE,
					       &res[num_res++]);
	} else {

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
}

static void __init arm_smmu_v3_dma_configure(struct device *dev,
					     struct acpi_iort_node *node)
{
	struct acpi_iort_smmu_v3 *smmu;
	enum dev_dma_attr attr;

	/* Retrieve SMMUv3 specific data */
	smmu = (struct acpi_iort_smmu_v3 *)node->node_data;

	attr = (smmu->flags & ACPI_IORT_SMMU_V3_COHACC_OVERRIDE) ?
			DEV_DMA_COHERENT : DEV_DMA_NON_COHERENT;

	/* We expect the dma masks to be equivalent for all SMMUv3 set-ups */
	dev->dma_mask = &dev->coherent_dma_mask;

	/* Configure DMA for the page table walker */
	acpi_dma_configure(dev, attr);
}

#if defined(CONFIG_ACPI_NUMA)
/*
 * set numa proximity domain for smmuv3 device
 */
static int  __init arm_smmu_v3_set_proximity(struct device *dev,
					      struct acpi_iort_node *node)
{
	struct acpi_iort_smmu_v3 *smmu;

	smmu = (struct acpi_iort_smmu_v3 *)node->node_data;
	if (smmu->flags & ACPI_IORT_SMMU_V3_PXM_VALID) {
		int node = acpi_map_pxm_to_node(smmu->pxm);

		if (node != NUMA_NO_NODE && !node_online(node))
			return -EINVAL;

		set_dev_node(dev, node);
		pr_info("SMMU-v3[%llx] Mapped to Proximity domain %d\n",
			smmu->base_address,
			smmu->pxm);
	}
	return 0;
}
#else
#define arm_smmu_v3_set_proximity NULL
#endif

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

static void __init arm_smmu_dma_configure(struct device *dev,
					  struct acpi_iort_node *node)
{
	struct acpi_iort_smmu *smmu;
	enum dev_dma_attr attr;

	/* Retrieve SMMU specific data */
	smmu = (struct acpi_iort_smmu *)node->node_data;

	attr = (smmu->flags & ACPI_IORT_SMMU_COHERENT_WALK) ?
			DEV_DMA_COHERENT : DEV_DMA_NON_COHERENT;

	/* We expect the dma masks to be equivalent for SMMU set-ups */
	dev->dma_mask = &dev->coherent_dma_mask;

	/* Configure DMA for the page table walker */
	acpi_dma_configure(dev, attr);
}

static int __init arm_smmu_v3_pmcg_count_resources(struct acpi_iort_node *node)
{
	struct acpi_iort_pmcg *pmcg;

	/* Retrieve PMCG specific data */
	pmcg = (struct acpi_iort_pmcg *)node->node_data;

	/*
	 * There are always 2 memory resources.
	 * If the overflow_gsiv is present then add that for a total of 3.
	 */
	return pmcg->overflow_gsiv ? 3 : 2;
}

static void __init arm_smmu_v3_pmcg_init_resources(struct resource *res,
						   struct acpi_iort_node *node)
{
	struct acpi_iort_pmcg *pmcg;

	/* Retrieve PMCG specific data */
	pmcg = (struct acpi_iort_pmcg *)node->node_data;

	res[0].start = pmcg->page0_base_address;
	res[0].end = pmcg->page0_base_address + SZ_4K - 1;
	res[0].flags = IORESOURCE_MEM;
	res[1].start = pmcg->page1_base_address;
	res[1].end = pmcg->page1_base_address + SZ_4K - 1;
	res[1].flags = IORESOURCE_MEM;

	if (pmcg->overflow_gsiv)
		acpi_iort_register_irq(pmcg->overflow_gsiv, "overflow",
				       ACPI_EDGE_SENSITIVE, &res[2]);
}

static struct acpi_platform_list pmcg_plat_info[] __initdata = {
	/* HiSilicon Hip08 Platform */
	{"HISI  ", "HIP08   ", 0, ACPI_SIG_IORT, greater_than_or_equal,
	 "Erratum #162001800", IORT_SMMU_V3_PMCG_HISI_HIP08},
	{ }
};

static int __init arm_smmu_v3_pmcg_add_platdata(struct platform_device *pdev)
{
	u32 model;
	int idx;

	idx = acpi_match_platform_list(pmcg_plat_info);
	if (idx >= 0)
		model = pmcg_plat_info[idx].data;
	else
		model = IORT_SMMU_V3_PMCG_GENERIC;

	return platform_device_add_data(pdev, &model, sizeof(model));
}

struct iort_dev_config {
	const char *name;
	int (*dev_init)(struct acpi_iort_node *node);
	void (*dev_dma_configure)(struct device *dev,
				  struct acpi_iort_node *node);
	int (*dev_count_resources)(struct acpi_iort_node *node);
	void (*dev_init_resources)(struct resource *res,
				     struct acpi_iort_node *node);
	int (*dev_set_proximity)(struct device *dev,
				    struct acpi_iort_node *node);
	int (*dev_add_platdata)(struct platform_device *pdev);
};

static const struct iort_dev_config iort_arm_smmu_v3_cfg __initconst = {
	.name = "arm-smmu-v3",
	.dev_dma_configure = arm_smmu_v3_dma_configure,
	.dev_count_resources = arm_smmu_v3_count_resources,
	.dev_init_resources = arm_smmu_v3_init_resources,
	.dev_set_proximity = arm_smmu_v3_set_proximity,
};

static const struct iort_dev_config iort_arm_smmu_cfg __initconst = {
	.name = "arm-smmu",
	.dev_dma_configure = arm_smmu_dma_configure,
	.dev_count_resources = arm_smmu_count_resources,
	.dev_init_resources = arm_smmu_init_resources,
};

static const struct iort_dev_config iort_arm_smmu_v3_pmcg_cfg __initconst = {
	.name = "arm-smmu-v3-pmcg",
	.dev_count_resources = arm_smmu_v3_pmcg_count_resources,
	.dev_init_resources = arm_smmu_v3_pmcg_init_resources,
	.dev_add_platdata = arm_smmu_v3_pmcg_add_platdata,
};

static __init const struct iort_dev_config *iort_get_dev_cfg(
			struct acpi_iort_node *node)
{
	switch (node->type) {
	case ACPI_IORT_NODE_SMMU_V3:
		return &iort_arm_smmu_v3_cfg;
	case ACPI_IORT_NODE_SMMU:
		return &iort_arm_smmu_cfg;
	case ACPI_IORT_NODE_PMCG:
		return &iort_arm_smmu_v3_pmcg_cfg;
	default:
		return NULL;
	}
}

/**
 * iort_add_platform_device() - Allocate a platform device for IORT node
 * @node: Pointer to device ACPI IORT node
 *
 * Returns: 0 on success, <0 failure
 */
static int __init iort_add_platform_device(struct acpi_iort_node *node,
					   const struct iort_dev_config *ops)
{
	struct fwnode_handle *fwnode;
	struct platform_device *pdev;
	struct resource *r;
	int ret, count;

	pdev = platform_device_alloc(ops->name, PLATFORM_DEVID_AUTO);
	if (!pdev)
		return -ENOMEM;

	if (ops->dev_set_proximity) {
		ret = ops->dev_set_proximity(&pdev->dev, node);
		if (ret)
			goto dev_put;
	}

	count = ops->dev_count_resources(node);

	r = kcalloc(count, sizeof(*r), GFP_KERNEL);
	if (!r) {
		ret = -ENOMEM;
		goto dev_put;
	}

	ops->dev_init_resources(r, node);

	ret = platform_device_add_resources(pdev, r, count);
	/*
	 * Resources are duplicated in platform_device_add_resources,
	 * free their allocated memory
	 */
	kfree(r);

	if (ret)
		goto dev_put;

	/*
	 * Platform devices based on PMCG nodes uses platform_data to
	 * pass the hardware model info to the driver. For others, add
	 * a copy of IORT node pointer to platform_data to be used to
	 * retrieve IORT data information.
	 */
	if (ops->dev_add_platdata)
		ret = ops->dev_add_platdata(pdev);
	else
		ret = platform_device_add_data(pdev, &node, sizeof(node));

	if (ret)
		goto dev_put;

	fwnode = iort_get_fwnode(node);

	if (!fwnode) {
		ret = -ENODEV;
		goto dev_put;
	}

	pdev->dev.fwnode = fwnode;

	if (ops->dev_dma_configure)
		ops->dev_dma_configure(&pdev->dev, node);

	iort_set_device_domain(&pdev->dev, node);

	ret = platform_device_add(pdev);
	if (ret)
		goto dma_deconfigure;

	return 0;

dma_deconfigure:
	arch_teardown_dma_ops(&pdev->dev);
dev_put:
	platform_device_put(pdev);

	return ret;
}

#ifdef CONFIG_PCI
static void __init iort_enable_acs(struct acpi_iort_node *iort_node)
{
	static bool acs_enabled __initdata;

	if (acs_enabled)
		return;

	if (iort_node->type == ACPI_IORT_NODE_PCI_ROOT_COMPLEX) {
		struct acpi_iort_node *parent;
		struct acpi_iort_id_mapping *map;
		int i;

		map = ACPI_ADD_PTR(struct acpi_iort_id_mapping, iort_node,
				   iort_node->mapping_offset);

		for (i = 0; i < iort_node->mapping_count; i++, map++) {
			if (!map->output_reference)
				continue;

			parent = ACPI_ADD_PTR(struct acpi_iort_node,
					iort_table,  map->output_reference);
			/*
			 * If we detect a RC->SMMU mapping, make sure
			 * we enable ACS on the system.
			 */
			if ((parent->type == ACPI_IORT_NODE_SMMU) ||
				(parent->type == ACPI_IORT_NODE_SMMU_V3)) {
				pci_request_acs();
				acs_enabled = true;
				return;
			}
		}
	}
}
#else
static inline void iort_enable_acs(struct acpi_iort_node *iort_node) { }
#endif

static void __init iort_init_platform_devices(void)
{
	struct acpi_iort_node *iort_node, *iort_end;
	struct acpi_table_iort *iort;
	struct fwnode_handle *fwnode;
	int i, ret;
	const struct iort_dev_config *ops;

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

		iort_enable_acs(iort_node);

		ops = iort_get_dev_cfg(iort_node);
		if (ops) {
			fwnode = acpi_alloc_fwnode_static();
			if (!fwnode)
				return;

			iort_set_fwnode(iort_node, fwnode);

			ret = iort_add_platform_device(iort_node, ops);
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
