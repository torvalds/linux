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
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/slab.h>

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
	acpi_status status;

	if (node->type == ACPI_IORT_NODE_NAMED_COMPONENT) {
		struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
		struct acpi_device *adev = to_acpi_device_node(dev->fwnode);
		struct acpi_iort_named_component *ncomp;

		if (!adev) {
			status = AE_NOT_FOUND;
			goto out;
		}

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
	} else {
		status = AE_NOT_FOUND;
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

static struct acpi_iort_node *iort_node_map_rid(struct acpi_iort_node *node,
						u32 rid_in, u32 *rid_out,
						u8 type)
{
	u32 rid = rid_in;

	/* Parse the ID mapping tree to find specified node type */
	while (node) {
		struct acpi_iort_id_mapping *map;
		int i;

		if (node->type == type) {
			if (rid_out)
				*rid_out = rid;
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

		/* Do the RID translation */
		for (i = 0; i < node->mapping_count; i++, map++) {
			if (!iort_id_map(map, node->type, rid, &rid))
				break;
		}

		if (i == node->mapping_count)
			goto fail_map;

		node = ACPI_ADD_PTR(struct acpi_iort_node, iort_table,
				    map->output_reference);
	}

fail_map:
	/* Map input RID to output RID unchanged on mapping failure*/
	if (rid_out)
		*rid_out = rid_in;

	return NULL;
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

	iort_node_map_rid(node, req_id, &dev_id, ACPI_IORT_NODE_ITS_GROUP);
	return dev_id;
}

/**
 * iort_dev_find_its_id() - Find the ITS identifier for a device
 * @dev: The device.
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

	node = iort_node_map_rid(node, req_id, NULL, ACPI_IORT_NODE_ITS_GROUP);
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

	acpi_probe_device_table(iort);
}
