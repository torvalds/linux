// SPDX-License-Identifier: GPL-2.0
/*
 *    driver for Microsemi PQI-based storage controllers
 *    Copyright (c) 2019 Microchip Technology Inc. and its subsidiaries
 *    Copyright (c) 2016-2018 Microsemi Corporation
 *    Copyright (c) 2016 PMC-Sierra, Inc.
 *
 *    Questions/Comments/Bugfixes to storagedev@microchip.com
 *
 */

#include <linux/kernel.h>
#include <linux/bsg-lib.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_transport_sas.h>
#include <asm/unaligned.h>
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
	sas_phy_free(phy);
	if (pqi_sas_phy->added_to_port)
		list_del(&pqi_sas_phy->phy_list_entry);
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

	if (pqi_sas_port->device &&
		pqi_sas_port->device->is_expander_smp_device) {
		identify->initiator_port_protocols = SAS_PROTOCOL_SMP;
		identify->target_port_protocols = SAS_PROTOCOL_SMP;
	} else {
		identify->initiator_port_protocols = SAS_PROTOCOL_STP;
		identify->target_port_protocols = SAS_PROTOCOL_STP;
	}

	return sas_rphy_add(rphy);
}

static struct sas_rphy *pqi_sas_rphy_alloc(struct pqi_sas_port *pqi_sas_port)
{
	if (pqi_sas_port->device &&
		pqi_sas_port->device->is_expander_smp_device)
		return sas_expander_alloc(pqi_sas_port->port,
				SAS_FANOUT_EXPANDER_DEVICE);

	return sas_end_device_alloc(pqi_sas_port->port);
}

static struct pqi_sas_port *pqi_alloc_sas_port(
	struct pqi_sas_node *pqi_sas_node, u64 sas_address,
	struct pqi_scsi_dev *device)
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
	pqi_sas_port->device = device;
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

	parent_dev = &shost->shost_dev;

	pqi_sas_node = pqi_alloc_sas_node(parent_dev);
	if (!pqi_sas_node)
		return -ENOMEM;

	pqi_sas_port = pqi_alloc_sas_port(pqi_sas_node,
		ctrl_info->sas_address, NULL);
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

	pqi_sas_port = pqi_alloc_sas_port(pqi_sas_node,
		device->sas_address, device);
	if (!pqi_sas_port)
		return -ENOMEM;

	rphy = pqi_sas_rphy_alloc(pqi_sas_port);
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

	int rc;
	unsigned long flags;
	struct Scsi_Host *shost;
	struct pqi_ctrl_info *ctrl_info;
	struct pqi_scsi_dev *found_device;
	struct pqi_scsi_dev *device;

	if (!rphy)
		return -ENODEV;

	shost = rphy_to_shost(rphy);
	ctrl_info = shost_to_hba(shost);
	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);
	found_device = pqi_find_device_by_sas_rphy(ctrl_info, rphy);

	if (!found_device) {
		rc = -ENODEV;
		goto out;
	}

	if (found_device->devtype == TYPE_ENCLOSURE) {
		*identifier = get_unaligned_be64(&found_device->wwid);
		rc = 0;
		goto out;
	}

	if (found_device->box_index == 0xff ||
		found_device->phys_box_on_bus == 0 ||
		found_device->bay == 0xff) {
		rc = -EINVAL;
		goto out;
	}

	list_for_each_entry(device, &ctrl_info->scsi_device_list,
		scsi_device_list_entry) {
		if (device->devtype == TYPE_ENCLOSURE &&
			device->box_index == found_device->box_index &&
			device->phys_box_on_bus ==
				found_device->phys_box_on_bus &&
			memcmp(device->phys_connector,
				found_device->phys_connector, 2) == 0) {
			*identifier =
				get_unaligned_be64(&device->wwid);
			rc = 0;
			goto out;
		}
	}

	if (found_device->phy_connected_dev_type != SA_CONTROLLER_DEVICE) {
		rc = -EINVAL;
		goto out;
	}

	list_for_each_entry(device, &ctrl_info->scsi_device_list,
		scsi_device_list_entry) {
		if (device->devtype == TYPE_ENCLOSURE &&
			CISS_GET_DRIVE_NUMBER(device->scsi3addr) ==
				PQI_VSEP_CISS_BTL) {
			*identifier = get_unaligned_be64(&device->wwid);
			rc = 0;
			goto out;
		}
	}

	rc = -EINVAL;
out:
	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);

	return rc;

}

static int pqi_sas_get_bay_identifier(struct sas_rphy *rphy)
{

	int rc;
	unsigned long flags;
	struct pqi_ctrl_info *ctrl_info;
	struct pqi_scsi_dev *device;
	struct Scsi_Host *shost;

	if (!rphy)
		return -ENODEV;

	shost = rphy_to_shost(rphy);
	ctrl_info = shost_to_hba(shost);
	spin_lock_irqsave(&ctrl_info->scsi_device_list_lock, flags);
	device = pqi_find_device_by_sas_rphy(ctrl_info, rphy);

	if (!device) {
		rc = -ENODEV;
		goto out;
	}

	if (device->bay == 0xff)
		rc = -EINVAL;
	else
		rc = device->bay;

out:
	spin_unlock_irqrestore(&ctrl_info->scsi_device_list_lock, flags);

	return rc;
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

#define CSMI_IOCTL_TIMEOUT	60
#define SMP_CRC_FIELD_LENGTH	4

static struct bmic_csmi_smp_passthru_buffer *
pqi_build_csmi_smp_passthru_buffer(struct sas_rphy *rphy,
	struct bsg_job *job)
{
	struct bmic_csmi_smp_passthru_buffer *smp_buf;
	struct bmic_csmi_ioctl_header *ioctl_header;
	struct bmic_csmi_smp_passthru *parameters;
	u32 req_size;
	u32 resp_size;

	smp_buf = kzalloc(sizeof(*smp_buf), GFP_KERNEL);
	if (!smp_buf)
		return NULL;

	req_size = job->request_payload.payload_len;
	resp_size = job->reply_payload.payload_len;

	ioctl_header = &smp_buf->ioctl_header;
	put_unaligned_le32(sizeof(smp_buf->ioctl_header),
		&ioctl_header->header_length);
	put_unaligned_le32(CSMI_IOCTL_TIMEOUT, &ioctl_header->timeout);
	put_unaligned_le32(CSMI_CC_SAS_SMP_PASSTHRU,
		&ioctl_header->control_code);
	put_unaligned_le32(sizeof(smp_buf->parameters), &ioctl_header->length);

	parameters = &smp_buf->parameters;
	parameters->phy_identifier = rphy->identify.phy_identifier;
	parameters->port_identifier = 0;
	parameters->connection_rate = 0;
	put_unaligned_be64(rphy->identify.sas_address,
		&parameters->destination_sas_address);

	if (req_size > SMP_CRC_FIELD_LENGTH)
		req_size -= SMP_CRC_FIELD_LENGTH;

	put_unaligned_le32(req_size, &parameters->request_length);

	put_unaligned_le32(resp_size, &parameters->response_length);

	sg_copy_to_buffer(job->request_payload.sg_list,
		job->reply_payload.sg_cnt, &parameters->request,
		req_size);

	return smp_buf;
}

static unsigned int pqi_build_sas_smp_handler_reply(
	struct bmic_csmi_smp_passthru_buffer *smp_buf, struct bsg_job *job,
	struct pqi_raid_error_info *error_info)
{
	sg_copy_from_buffer(job->reply_payload.sg_list,
		job->reply_payload.sg_cnt, &smp_buf->parameters.response,
		le32_to_cpu(smp_buf->parameters.response_length));

	job->reply_len = le16_to_cpu(error_info->sense_data_length);
	memcpy(job->reply, error_info->data,
			le16_to_cpu(error_info->sense_data_length));

	return job->reply_payload.payload_len -
		get_unaligned_le32(&error_info->data_in_transferred);
}

void pqi_sas_smp_handler(struct bsg_job *job, struct Scsi_Host *shost,
	struct sas_rphy *rphy)
{
	int rc;
	struct pqi_ctrl_info *ctrl_info = shost_to_hba(shost);
	struct bmic_csmi_smp_passthru_buffer *smp_buf;
	struct pqi_raid_error_info error_info;
	unsigned int reslen = 0;

	pqi_ctrl_busy(ctrl_info);

	if (job->reply_payload.payload_len == 0) {
		rc = -ENOMEM;
		goto out;
	}

	if (!rphy) {
		rc = -EINVAL;
		goto out;
	}

	if (rphy->identify.device_type != SAS_FANOUT_EXPANDER_DEVICE) {
		rc = -EINVAL;
		goto out;
	}

	if (job->request_payload.sg_cnt > 1 || job->reply_payload.sg_cnt > 1) {
		rc = -EINVAL;
		goto out;
	}

	if (pqi_ctrl_offline(ctrl_info)) {
		rc = -ENXIO;
		goto out;
	}

	if (pqi_ctrl_blocked(ctrl_info)) {
		rc = -EBUSY;
		goto out;
	}

	smp_buf = pqi_build_csmi_smp_passthru_buffer(rphy, job);
	if (!smp_buf) {
		rc = -ENOMEM;
		goto out;
	}

	rc = pqi_csmi_smp_passthru(ctrl_info, smp_buf, sizeof(*smp_buf),
		&error_info);
	if (rc)
		goto out;

	reslen = pqi_build_sas_smp_handler_reply(smp_buf, job, &error_info);
out:
	bsg_job_done(job, rc, reslen);
	pqi_ctrl_unbusy(ctrl_info);
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
	.smp_handler = pqi_sas_smp_handler,
};
