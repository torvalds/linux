// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Broadcom MPI3 Storage Controllers
 *
 * Copyright (C) 2017-2022 Broadcom Inc.
 *  (mailto: mpi3mr-linuxdrv.pdl@broadcom.com)
 *
 */

#include "mpi3mr.h"

/**
 * mpi3mr_convert_phy_link_rate -
 * @link_rate: link rate as defined in the MPI header
 *
 * Convert link_rate from mpi format into sas_transport layer
 * form.
 *
 * Return: A valid SAS transport layer defined link rate
 */
static enum sas_linkrate mpi3mr_convert_phy_link_rate(u8 link_rate)
{
	enum sas_linkrate rc;

	switch (link_rate) {
	case MPI3_SAS_NEG_LINK_RATE_1_5:
		rc = SAS_LINK_RATE_1_5_GBPS;
		break;
	case MPI3_SAS_NEG_LINK_RATE_3_0:
		rc = SAS_LINK_RATE_3_0_GBPS;
		break;
	case MPI3_SAS_NEG_LINK_RATE_6_0:
		rc = SAS_LINK_RATE_6_0_GBPS;
		break;
	case MPI3_SAS_NEG_LINK_RATE_12_0:
		rc = SAS_LINK_RATE_12_0_GBPS;
		break;
	case MPI3_SAS_NEG_LINK_RATE_22_5:
		rc = SAS_LINK_RATE_22_5_GBPS;
		break;
	case MPI3_SAS_NEG_LINK_RATE_PHY_DISABLED:
		rc = SAS_PHY_DISABLED;
		break;
	case MPI3_SAS_NEG_LINK_RATE_NEGOTIATION_FAILED:
		rc = SAS_LINK_RATE_FAILED;
		break;
	case MPI3_SAS_NEG_LINK_RATE_PORT_SELECTOR:
		rc = SAS_SATA_PORT_SELECTOR;
		break;
	case MPI3_SAS_NEG_LINK_RATE_SMP_RESET_IN_PROGRESS:
		rc = SAS_PHY_RESET_IN_PROGRESS;
		break;
	case MPI3_SAS_NEG_LINK_RATE_SATA_OOB_COMPLETE:
	case MPI3_SAS_NEG_LINK_RATE_UNKNOWN_LINK_RATE:
	default:
		rc = SAS_LINK_RATE_UNKNOWN;
		break;
	}
	return rc;
}

/**
 * mpi3mr_delete_sas_phy - Remove a single phy from port
 * @mrioc: Adapter instance reference
 * @mr_sas_port: Internal Port object
 * @mr_sas_phy: Internal Phy object
 *
 * Return: None.
 */
static void mpi3mr_delete_sas_phy(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_sas_port *mr_sas_port,
	struct mpi3mr_sas_phy *mr_sas_phy)
{
	u64 sas_address = mr_sas_port->remote_identify.sas_address;

	dev_info(&mr_sas_phy->phy->dev,
	    "remove: sas_address(0x%016llx), phy(%d)\n",
	    (unsigned long long) sas_address, mr_sas_phy->phy_id);

	list_del(&mr_sas_phy->port_siblings);
	mr_sas_port->num_phys--;
	sas_port_delete_phy(mr_sas_port->port, mr_sas_phy->phy);
	mr_sas_phy->phy_belongs_to_port = 0;
}

/**
 * mpi3mr_add_sas_phy - Adding a single phy to a port
 * @mrioc: Adapter instance reference
 * @mr_sas_port: Internal Port object
 * @mr_sas_phy: Internal Phy object
 *
 * Return: None.
 */
static void mpi3mr_add_sas_phy(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_sas_port *mr_sas_port,
	struct mpi3mr_sas_phy *mr_sas_phy)
{
	u64 sas_address = mr_sas_port->remote_identify.sas_address;

	dev_info(&mr_sas_phy->phy->dev,
	    "add: sas_address(0x%016llx), phy(%d)\n", (unsigned long long)
	    sas_address, mr_sas_phy->phy_id);

	list_add_tail(&mr_sas_phy->port_siblings, &mr_sas_port->phy_list);
	mr_sas_port->num_phys++;
	sas_port_add_phy(mr_sas_port->port, mr_sas_phy->phy);
	mr_sas_phy->phy_belongs_to_port = 1;
}

/**
 * mpi3mr_add_phy_to_an_existing_port - add phy to existing port
 * @mrioc: Adapter instance reference
 * @mr_sas_node: Internal sas node object (expander or host)
 * @mr_sas_phy: Internal Phy object *
 * @sas_address: SAS address of device/expander were phy needs
 *             to be added to
 * @hba_port: HBA port entry
 *
 * Return: None.
 */
static void mpi3mr_add_phy_to_an_existing_port(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_sas_node *mr_sas_node, struct mpi3mr_sas_phy *mr_sas_phy,
	u64 sas_address, struct mpi3mr_hba_port *hba_port)
{
	struct mpi3mr_sas_port *mr_sas_port;
	struct mpi3mr_sas_phy *srch_phy;

	if (mr_sas_phy->phy_belongs_to_port == 1)
		return;

	if (!hba_port)
		return;

	list_for_each_entry(mr_sas_port, &mr_sas_node->sas_port_list,
	    port_list) {
		if (mr_sas_port->remote_identify.sas_address !=
		    sas_address)
			continue;
		if (mr_sas_port->hba_port != hba_port)
			continue;
		list_for_each_entry(srch_phy, &mr_sas_port->phy_list,
		    port_siblings) {
			if (srch_phy == mr_sas_phy)
				return;
		}
		mpi3mr_add_sas_phy(mrioc, mr_sas_port, mr_sas_phy);
		return;
	}
}

/**
 * mpi3mr_del_phy_from_an_existing_port - del phy from a port
 * @mrioc: Adapter instance reference
 * @mr_sas_node: Internal sas node object (expander or host)
 * @mr_sas_phy: Internal Phy object
 *
 * Return: None.
 */
static void mpi3mr_del_phy_from_an_existing_port(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_sas_node *mr_sas_node, struct mpi3mr_sas_phy *mr_sas_phy)
{
	struct mpi3mr_sas_port *mr_sas_port, *next;
	struct mpi3mr_sas_phy *srch_phy;

	if (mr_sas_phy->phy_belongs_to_port == 0)
		return;

	list_for_each_entry_safe(mr_sas_port, next, &mr_sas_node->sas_port_list,
	    port_list) {
		list_for_each_entry(srch_phy, &mr_sas_port->phy_list,
		    port_siblings) {
			if (srch_phy != mr_sas_phy)
				continue;
			mpi3mr_delete_sas_phy(mrioc, mr_sas_port,
			    mr_sas_phy);
			return;
		}
	}
}

/**
 * mpi3mr_sas_port_sanity_check - sanity check while adding port
 * @mrioc: Adapter instance reference
 * @mr_sas_node: Internal sas node object (expander or host)
 * @sas_address: SAS address of device/expander
 * @hba_port: HBA port entry
 *
 * Verifies whether the Phys attached to a device with the given
 * SAS address already belongs to an existing sas port if so
 * will remove those phys from the sas port
 *
 * Return: None.
 */
static void mpi3mr_sas_port_sanity_check(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_sas_node *mr_sas_node, u64 sas_address,
	struct mpi3mr_hba_port *hba_port)
{
	int i;

	for (i = 0; i < mr_sas_node->num_phys; i++) {
		if ((mr_sas_node->phy[i].remote_identify.sas_address !=
		    sas_address) || (mr_sas_node->phy[i].hba_port != hba_port))
			continue;
		if (mr_sas_node->phy[i].phy_belongs_to_port == 1)
			mpi3mr_del_phy_from_an_existing_port(mrioc,
			    mr_sas_node, &mr_sas_node->phy[i]);
	}
}

/**
 * mpi3mr_set_identify - set identify for phys and end devices
 * @mrioc: Adapter instance reference
 * @handle: Firmware device handle
 * @identify: SAS transport layer's identify info
 *
 * Populates sas identify info for a specific device.
 *
 * Return: 0 for success, non-zero for failure.
 */
static int mpi3mr_set_identify(struct mpi3mr_ioc *mrioc, u16 handle,
	struct sas_identify *identify)
{

	struct mpi3_device_page0 device_pg0;
	struct mpi3_device0_sas_sata_format *sasinf;
	u16 device_info;
	u16 ioc_status;

	if (mrioc->reset_in_progress) {
		ioc_err(mrioc, "%s: host reset in progress!\n", __func__);
		return -EFAULT;
	}

	if ((mpi3mr_cfg_get_dev_pg0(mrioc, &ioc_status, &device_pg0,
	    sizeof(device_pg0), MPI3_DEVICE_PGAD_FORM_HANDLE, handle))) {
		ioc_err(mrioc, "%s: device page0 read failed\n", __func__);
		return -ENXIO;
	}

	if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "device page read failed for handle(0x%04x), with ioc_status(0x%04x) failure at %s:%d/%s()!\n",
		    handle, ioc_status, __FILE__, __LINE__, __func__);
		return -EIO;
	}

	memset(identify, 0, sizeof(struct sas_identify));
	sasinf = &device_pg0.device_specific.sas_sata_format;
	device_info = le16_to_cpu(sasinf->device_info);

	/* sas_address */
	identify->sas_address = le64_to_cpu(sasinf->sas_address);

	/* phy number of the parent device this device is linked to */
	identify->phy_identifier = sasinf->phy_num;

	/* device_type */
	switch (device_info & MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_MASK) {
	case MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_NO_DEVICE:
		identify->device_type = SAS_PHY_UNUSED;
		break;
	case MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_END_DEVICE:
		identify->device_type = SAS_END_DEVICE;
		break;
	case MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_EXPANDER:
		identify->device_type = SAS_EDGE_EXPANDER_DEVICE;
		break;
	}

	/* initiator_port_protocols */
	if (device_info & MPI3_SAS_DEVICE_INFO_SSP_INITIATOR)
		identify->initiator_port_protocols |= SAS_PROTOCOL_SSP;
	/* MPI3.0 doesn't have define for SATA INIT so setting both here*/
	if (device_info & MPI3_SAS_DEVICE_INFO_STP_INITIATOR)
		identify->initiator_port_protocols |= (SAS_PROTOCOL_STP |
		    SAS_PROTOCOL_SATA);
	if (device_info & MPI3_SAS_DEVICE_INFO_SMP_INITIATOR)
		identify->initiator_port_protocols |= SAS_PROTOCOL_SMP;

	/* target_port_protocols */
	if (device_info & MPI3_SAS_DEVICE_INFO_SSP_TARGET)
		identify->target_port_protocols |= SAS_PROTOCOL_SSP;
	/* MPI3.0 doesn't have define for STP Target so setting both here*/
	if (device_info & MPI3_SAS_DEVICE_INFO_STP_SATA_TARGET)
		identify->target_port_protocols |= (SAS_PROTOCOL_STP |
		    SAS_PROTOCOL_SATA);
	if (device_info & MPI3_SAS_DEVICE_INFO_SMP_TARGET)
		identify->target_port_protocols |= SAS_PROTOCOL_SMP;
	return 0;
}

/**
 * mpi3mr_add_host_phy - report sas_host phy to SAS transport
 * @mrioc: Adapter instance reference
 * @mr_sas_phy: Internal Phy object
 * @phy_pg0: SAS phy page 0
 * @parent_dev: Prent device class object
 *
 * Return: 0 for success, non-zero for failure.
 */
static int mpi3mr_add_host_phy(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_sas_phy *mr_sas_phy, struct mpi3_sas_phy_page0 phy_pg0,
	struct device *parent_dev)
{
	struct sas_phy *phy;
	int phy_index = mr_sas_phy->phy_id;


	INIT_LIST_HEAD(&mr_sas_phy->port_siblings);
	phy = sas_phy_alloc(parent_dev, phy_index);
	if (!phy) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -1;
	}
	if ((mpi3mr_set_identify(mrioc, mr_sas_phy->handle,
	    &mr_sas_phy->identify))) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		sas_phy_free(phy);
		return -1;
	}
	phy->identify = mr_sas_phy->identify;
	mr_sas_phy->attached_handle = le16_to_cpu(phy_pg0.attached_dev_handle);
	if (mr_sas_phy->attached_handle)
		mpi3mr_set_identify(mrioc, mr_sas_phy->attached_handle,
		    &mr_sas_phy->remote_identify);
	phy->identify.phy_identifier = mr_sas_phy->phy_id;
	phy->negotiated_linkrate = mpi3mr_convert_phy_link_rate(
	    (phy_pg0.negotiated_link_rate &
	    MPI3_SAS_NEG_LINK_RATE_LOGICAL_MASK) >>
	    MPI3_SAS_NEG_LINK_RATE_LOGICAL_SHIFT);
	phy->minimum_linkrate_hw = mpi3mr_convert_phy_link_rate(
	    phy_pg0.hw_link_rate & MPI3_SAS_HWRATE_MIN_RATE_MASK);
	phy->maximum_linkrate_hw = mpi3mr_convert_phy_link_rate(
	    phy_pg0.hw_link_rate >> 4);
	phy->minimum_linkrate = mpi3mr_convert_phy_link_rate(
	    phy_pg0.programmed_link_rate & MPI3_SAS_PRATE_MIN_RATE_MASK);
	phy->maximum_linkrate = mpi3mr_convert_phy_link_rate(
	    phy_pg0.programmed_link_rate >> 4);
	phy->hostdata = mr_sas_phy->hba_port;

	if ((sas_phy_add(phy))) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		sas_phy_free(phy);
		return -1;
	}
	if ((mrioc->logging_level & MPI3_DEBUG_TRANSPORT_INFO))
		dev_info(&phy->dev,
		    "add: handle(0x%04x), sas_address(0x%016llx)\n"
		    "\tattached_handle(0x%04x), sas_address(0x%016llx)\n",
		    mr_sas_phy->handle, (unsigned long long)
		    mr_sas_phy->identify.sas_address,
		    mr_sas_phy->attached_handle,
		    (unsigned long long)
		    mr_sas_phy->remote_identify.sas_address);
	mr_sas_phy->phy = phy;
	return 0;
}

/**
 * mpi3mr_add_expander_phy - report expander phy to transport
 * @mrioc: Adapter instance reference
 * @mr_sas_phy: Internal Phy object
 * @expander_pg1: SAS Expander page 1
 * @parent_dev: Parent device class object
 *
 * Return: 0 for success, non-zero for failure.
 */
static int mpi3mr_add_expander_phy(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_sas_phy *mr_sas_phy,
	struct mpi3_sas_expander_page1 expander_pg1,
	struct device *parent_dev)
{
	struct sas_phy *phy;
	int phy_index = mr_sas_phy->phy_id;

	INIT_LIST_HEAD(&mr_sas_phy->port_siblings);
	phy = sas_phy_alloc(parent_dev, phy_index);
	if (!phy) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -1;
	}
	if ((mpi3mr_set_identify(mrioc, mr_sas_phy->handle,
	    &mr_sas_phy->identify))) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		sas_phy_free(phy);
		return -1;
	}
	phy->identify = mr_sas_phy->identify;
	mr_sas_phy->attached_handle =
	    le16_to_cpu(expander_pg1.attached_dev_handle);
	if (mr_sas_phy->attached_handle)
		mpi3mr_set_identify(mrioc, mr_sas_phy->attached_handle,
		    &mr_sas_phy->remote_identify);
	phy->identify.phy_identifier = mr_sas_phy->phy_id;
	phy->negotiated_linkrate = mpi3mr_convert_phy_link_rate(
	    (expander_pg1.negotiated_link_rate &
	    MPI3_SAS_NEG_LINK_RATE_LOGICAL_MASK) >>
	    MPI3_SAS_NEG_LINK_RATE_LOGICAL_SHIFT);
	phy->minimum_linkrate_hw = mpi3mr_convert_phy_link_rate(
	    expander_pg1.hw_link_rate & MPI3_SAS_HWRATE_MIN_RATE_MASK);
	phy->maximum_linkrate_hw = mpi3mr_convert_phy_link_rate(
	    expander_pg1.hw_link_rate >> 4);
	phy->minimum_linkrate = mpi3mr_convert_phy_link_rate(
	    expander_pg1.programmed_link_rate & MPI3_SAS_PRATE_MIN_RATE_MASK);
	phy->maximum_linkrate = mpi3mr_convert_phy_link_rate(
	    expander_pg1.programmed_link_rate >> 4);
	phy->hostdata = mr_sas_phy->hba_port;

	if ((sas_phy_add(phy))) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		sas_phy_free(phy);
		return -1;
	}
	if ((mrioc->logging_level & MPI3_DEBUG_TRANSPORT_INFO))
		dev_info(&phy->dev,
		    "add: handle(0x%04x), sas_address(0x%016llx)\n"
		    "\tattached_handle(0x%04x), sas_address(0x%016llx)\n",
		    mr_sas_phy->handle, (unsigned long long)
		    mr_sas_phy->identify.sas_address,
		    mr_sas_phy->attached_handle,
		    (unsigned long long)
		    mr_sas_phy->remote_identify.sas_address);
	mr_sas_phy->phy = phy;
	return 0;
}
