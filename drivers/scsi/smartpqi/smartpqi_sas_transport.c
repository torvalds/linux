/*
 *    driver for Microsemi PQI-based storage controllers
 *    Copyright (c) 2016-2017 Microsemi Corporation
 *    Copyright (c) 2016 PMC-Sierra, Inc.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; version 2 of the License.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    Questions/Comments/Bugfixes to esc.storagedev@microsemi.com
 *
 */

#include <linux/kernel.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_transport_sas.h>
#include "smartpqi.h"

static struct pqi_sas_phy *pqi_alloc_sas_phy(struct pqi_sas_port *pqi_sas_port)
{
	struct pqi_sas_phy *pqi_sas_phy;
	struct sas_phy *phy;

	pqi_sas_phy = kzalloc(sizeof(*pqi_sas_phy), GFP_KERNEL);
	if (!pqi_sas_phy)
		return NULL;

	phy = sas_phy_alloc(pqi_sas_port->parent_node->parent_dev,
		pqi_sas_port->next_phy_index);
	if (!phy) {
		kfree(pqi_sas_phy);
		return NULL;
	}

	pqi_sas_port->next_phy_index++;
	pqi_sas_phy->phy = phy;
	pqi_sas_phy->parent_port = pqi_sas_port;

	return pqi_sas_phy;
}

static void pqi_free_sas_phy(struct pqi_sas_phy *pqi_sas_phy)
{
	struct sas_phy *phy = pqi_sas_phy->phy;

	sas_port_delete_phy(pqi_sas_phy->parent_port->port, phy);
	if (pqi_sas_phy->added_to_port)
		list_del(&pqi_sas_phy->phy_list_entry);
	sas_phy_delete(phy);
	kfree(pqi_sas_phy);
}

static int pqi_sas_port_add_phy(struct pqi_sas_phy *pqi_sas_phy)
{
	int rc;
	struct pqi_sas_port *pqi_sas_port;
	struct sas_phy *phy;
	struct sas_identify *identify;

	pqi_sas_port = pqi_sas_phy->parent_port;
	phy = pqi_sas_phy->phy;

	identify = &phy->identify;
	memset(identify, 0, sizeof(*identify));
	identify->sas_address = pqi_sas_port->sas_address;
	identify->device_type = SAS_END_DEVICE;
	identify->initiator_port_protocols = SAS_PROTOCOL_STP;
	identify->target_port_protocols = SAS_PROTOCOL_STP;
	phy->minimum_linkrate_hw = SAS_LINK_RATE_UNKNOWN;
	phy->maximum_linkrate_hw = SAS_LINK_RATE_UNKNOWN;
	phy->minimum_linkrate = SAS_LINK_RATE_UNKNOWN;
	phy->maximum_linkrate = SAS_LINK_RATE_UNKNOWN;
	phy->negotiated_linkrate = SAS_LINK_RATE_UNKNOWN;

	rc = sas_phy_add(pqi_sas_phy->phy);
	if (rc)
		return rc;

	sas_port_add_phy(pqi_sas_port->port, pqi_sas_phy->phy);
	list_add_tail(&pqi_sas_phy->phy_list_entry,
		&pqi_sas_port->phy_list_head);
	pqi_sas_phy->added_to_port = true;

	return 0;
}

static int pqi_sas_port_add_rphy(struct pqi_sas_port *pqi_sas_port,
	struct sas_rphy *rphy)
{
	struct sas_identify *identify;

	identify = &rphy->identify;
	identify->sas_address = pqi_sas_port->sas_address;
	identify->initiator_port_protocols = SAS_PROTOCOL_STP;
	identify->target_port_protocols = SAS_PROTOCOL_STP;

	return sas_rphy_add(rphy);
}

static struct pqi_sas_port *pqi_alloc_sas_port(
	struct pqi_sas_node *pqi_sas_node, u64 sas_address)
{
	int rc;
	struct pqi_sas_port *pqi_sas_port;
	struct sas_port *port;

	pqi_sas_port = kzalloc(sizeof(*pqi_sas_port), GFP_KERNEL);
	if (!pqi_sas_port)
		return NULL;

	INIT_LIST_HEAD(&pqi_sas_port->phy_list_head);
	pqi_sas_port->parent_node = pqi_sas_node;

	port = sas_port_alloc_num(pqi_sas_node->parent_dev);
	if (!port)
		goto free_pqi_port;

	rc = sas_port_add(port);
	if (rc)
		goto free_sas_port;

	pqi_sas_port->port = port;
	pqi_sas_port->sas_address = sas_address;
	list_add_tail(&pqi_sas_port->port_list_entry,
		&pqi_sas_node->port_list_head);

	return pqi_sas_port;

free_sas_port:
	sas_port_free(port);
free_pqi_port:
	kfree(pqi_sas_port);

	return NULL;
}

static void pqi_free_sas_port(struct pqi_sas_port *pqi_sas_port)
{
	struct pqi_sas_phy *pqi_sas_phy;
	struct pqi_sas_phy *next;

	list_for_each_entry_safe(pqi_sas_phy, next,
			&pqi_sas_port->phy_list_head, phy_list_entry)
		pqi_free_sas_phy(pqi_sas_phy);

	sas_port_delete(pqi_sas_port->port);
	list_del(&pqi_sas_port->port_list_entry);
	kfree(pqi_sas_port);
}

static struct pqi_sas_node *pqi_alloc_sas_node(struct device *parent_dev)
{
	struct pqi_sas_node *pqi_sas_node;

	pqi_sas_node = kzalloc(sizeof(*pqi_sas_node), GFP_KERNEL);
	if (pqi_sas_node) {
		pqi_sas_node->parent_dev = parent_dev;
		INIT_LIST_HEAD(&pqi_sas_node->port_list_head);
	}

	return pqi_sas_node;
}

static void pqi_free_sas_node(struct pqi_sas_node *pqi_sas_node)
{
	struct pqi_sas_port *pqi_sas_port;
	struct pqi_sas_port *next;

	if (!pqi_sas_node)
		return;

	list_for_each_entry_safe(pqi_sas_port, next,
			&pqi_sas_node->port_list_head, port_list_entry)
		pqi_free_sas_port(pqi_sas_port);

	kfree(pqi_sas_node);
}

struct pqi_scsi_dev *pqi_find_device_by_sas_rphy(
	struct pqi_ctrl_info *ctrl_info, struct sas_rphy *rphy)
{
	struct pqi_scsi_dev *device;

	list_for_each_entry(device, &ctrl_info->scsi_device_list,
		scsi_device_list_entry) {
		if (!device->sas_port)
			continue;
		if (device->sas_port->rphy == rphy)
			return device;
	}

	return NULL;
}

int pqi_add_sas_host(struct Scsi_Host *shost, struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	struct device *parent_dev;
	struct pqi_sas_node *pqi_sas_node;
	struct pqi_sas_port *pqi_sas_port;
	struct pqi_sas_phy *pqi_sas_phy;

	parent_dev = &shost->shost_gendev;

	pqi_sas_node = pqi_alloc_sas_node(parent_dev);
	if (!pqi_sas_node)
		return -ENOMEM;

	pqi_sas_port = pqi_alloc_sas_port(pqi_sas_node, ctrl_info->sas_address);
	if (!pqi_sas_port) {
		rc = -ENODEV;
		goto free_sas_node;
	}

	pqi_sas_phy = pqi_alloc_sas_phy(pqi_sas_port);
	if (!pqi_sas_phy) {
		rc = -ENODEV;
		goto free_sas_port;
	}

	rc = pqi_sas_port_add_phy(pqi_sas_phy);
	if (rc)
		goto free_sas_phy;

	ctrl_info->sas_host = pqi_sas_node;

	return 0;

free_sas_phy:
	pqi_free_sas_phy(pqi_sas_phy);
free_sas_port:
	pqi_free_sas_port(pqi_sas_port);
free_sas_node:
	pqi_free_sas_node(pqi_sas_node);

	return rc;
}

void pqi_delete_sas_host(struct pqi_ctrl_info *ctrl_info)
{
	pqi_free_sas_node(ctrl_info->sas_host);
}

int pqi_add_sas_device(struct pqi_sas_node *pqi_sas_node,
	struct pqi_scsi_dev *device)
{
	int rc;
	struct pqi_sas_port *pqi_sas_port;
	struct sas_rphy *rphy;

	pqi_sas_port = pqi_alloc_sas_port(pqi_sas_node, device->sas_address);
	if (!pqi_sas_port)
		return -ENOMEM;

	rphy = sas_end_device_alloc(pqi_sas_port->port);
	if (!rphy) {
		rc = -ENODEV;
		goto free_sas_port;
	}

	pqi_sas_port->rphy = rphy;
	device->sas_port = pqi_sas_port;

	rc = pqi_sas_port_add_rphy(pqi_sas_port, rphy);
	if (rc)
		goto free_sas_port;

	return 0;

free_sas_port:
	pqi_free_sas_port(pqi_sas_port);
	device->sas_port = NULL;

	return rc;
}

void pqi_remove_sas_device(struct pqi_scsi_dev *device)
{
	if (device->sas_port) {
		pqi_free_sas_port(device->sas_port);
		device->sas_port = NULL;
	}
}

static int pqi_sas_get_linkerrors(struct sas_phy *phy)
{
	return 0;
}

static int pqi_sas_get_enclosure_identifier(struct sas_rphy *rphy,
	u64 *identifier)
{
	return 0;
}

static int pqi_sas_get_bay_identifier(struct sas_rphy *rphy)
{
	return -ENXIO;
}

static int pqi_sas_phy_reset(struct sas_phy *phy, int hard_reset)
{
	return 0;
}

static int pqi_sas_phy_enable(struct sas_phy *phy, int enable)
{
	return 0;
}

static int pqi_sas_phy_setup(struct sas_phy *phy)
{
	return 0;
}

static void pqi_sas_phy_release(struct sas_phy *phy)
{
}

static int pqi_sas_phy_speed(struct sas_phy *phy,
	struct sas_phy_linkrates *rates)
{
	return -EINVAL;
}

struct sas_function_template pqi_sas_transport_functions = {
	.get_linkerrors = pqi_sas_get_linkerrors,
	.get_enclosure_identifier = pqi_sas_get_enclosure_identifier,
	.get_bay_identifier = pqi_sas_get_bay_identifier,
	.phy_reset = pqi_sas_phy_reset,
	.phy_enable = pqi_sas_phy_enable,
	.phy_setup = pqi_sas_phy_setup,
	.phy_release = pqi_sas_phy_release,
	.set_phy_speed = pqi_sas_phy_speed,
};
