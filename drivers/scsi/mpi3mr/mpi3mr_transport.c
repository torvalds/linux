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
 * mpi3mr_post_transport_req - Issue transport requests and wait
 * @mrioc: Adapter instance reference
 * @request: Properly populated MPI3 request
 * @request_sz: Size of the MPI3 request
 * @reply: Pointer to return MPI3 reply
 * @reply_sz: Size of the MPI3 reply buffer
 * @timeout: Timeout in seconds
 * @ioc_status: Pointer to return ioc status
 *
 * A generic function for posting MPI3 requests from the SAS
 * transport layer that uses transport command infrastructure.
 * This blocks for the completion of request for timeout seconds
 * and if the request times out this function faults the
 * controller with proper reason code.
 *
 * On successful completion of the request this function returns
 * appropriate ioc status from the firmware back to the caller.
 *
 * Return: 0 on success, non-zero on failure.
 */
static int mpi3mr_post_transport_req(struct mpi3mr_ioc *mrioc, void *request,
	u16 request_sz, void *reply, u16 reply_sz, int timeout,
	u16 *ioc_status)
{
	int retval = 0;

	mutex_lock(&mrioc->transport_cmds.mutex);
	if (mrioc->transport_cmds.state & MPI3MR_CMD_PENDING) {
		retval = -1;
		ioc_err(mrioc, "sending transport request failed due to command in use\n");
		mutex_unlock(&mrioc->transport_cmds.mutex);
		goto out;
	}
	mrioc->transport_cmds.state = MPI3MR_CMD_PENDING;
	mrioc->transport_cmds.is_waiting = 1;
	mrioc->transport_cmds.callback = NULL;
	mrioc->transport_cmds.ioc_status = 0;
	mrioc->transport_cmds.ioc_loginfo = 0;

	init_completion(&mrioc->transport_cmds.done);
	dprint_cfg_info(mrioc, "posting transport request\n");
	if (mrioc->logging_level & MPI3_DEBUG_TRANSPORT_INFO)
		dprint_dump(request, request_sz, "transport_req");
	retval = mpi3mr_admin_request_post(mrioc, request, request_sz, 1);
	if (retval) {
		ioc_err(mrioc, "posting transport request failed\n");
		goto out_unlock;
	}
	wait_for_completion_timeout(&mrioc->transport_cmds.done,
	    (timeout * HZ));
	if (!(mrioc->transport_cmds.state & MPI3MR_CMD_COMPLETE)) {
		mpi3mr_check_rh_fault_ioc(mrioc,
		    MPI3MR_RESET_FROM_SAS_TRANSPORT_TIMEOUT);
		ioc_err(mrioc, "transport request timed out\n");
		retval = -1;
		goto out_unlock;
	}
	*ioc_status = mrioc->transport_cmds.ioc_status &
		MPI3_IOCSTATUS_STATUS_MASK;
	if ((*ioc_status) != MPI3_IOCSTATUS_SUCCESS)
		dprint_transport_err(mrioc,
		    "transport request returned with ioc_status(0x%04x), log_info(0x%08x)\n",
		    *ioc_status, mrioc->transport_cmds.ioc_loginfo);

	if ((reply) && (mrioc->transport_cmds.state & MPI3MR_CMD_REPLY_VALID))
		memcpy((u8 *)reply, mrioc->transport_cmds.reply, reply_sz);

out_unlock:
	mrioc->transport_cmds.state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&mrioc->transport_cmds.mutex);

out:
	return retval;
}

/* report manufacture request structure */
struct rep_manu_request {
	u8 smp_frame_type;
	u8 function;
	u8 reserved;
	u8 request_length;
};

/* report manufacture reply structure */
struct rep_manu_reply {
	u8 smp_frame_type; /* 0x41 */
	u8 function; /* 0x01 */
	u8 function_result;
	u8 response_length;
	u16 expander_change_count;
	u8 reserved0[2];
	u8 sas_format;
	u8 reserved2[3];
	u8 vendor_id[SAS_EXPANDER_VENDOR_ID_LEN];
	u8 product_id[SAS_EXPANDER_PRODUCT_ID_LEN];
	u8 product_rev[SAS_EXPANDER_PRODUCT_REV_LEN];
	u8 component_vendor_id[SAS_EXPANDER_COMPONENT_VENDOR_ID_LEN];
	u16 component_id;
	u8 component_revision_id;
	u8 reserved3;
	u8 vendor_specific[8];
};

/**
 * mpi3mr_report_manufacture - obtain SMP report_manufacture
 * @mrioc: Adapter instance reference
 * @sas_address: SAS address of the expander device
 * @edev: SAS transport layer sas_expander_device object
 * @port_id: ID of the HBA port
 *
 * Fills in the sas_expander_device with manufacturing info.
 *
 * Return: 0 for success, non-zero for failure.
 */
static int mpi3mr_report_manufacture(struct mpi3mr_ioc *mrioc,
	u64 sas_address, struct sas_expander_device *edev, u8 port_id)
{
	struct mpi3_smp_passthrough_request mpi_request;
	struct mpi3_smp_passthrough_reply mpi_reply;
	struct rep_manu_reply *manufacture_reply;
	struct rep_manu_request *manufacture_request;
	int rc = 0;
	void *psge;
	void *data_out = NULL;
	dma_addr_t data_out_dma;
	dma_addr_t data_in_dma;
	size_t data_in_sz;
	size_t data_out_sz;
	u8 sgl_flags = MPI3MR_SGEFLAGS_SYSTEM_SIMPLE_END_OF_LIST;
	u16 request_sz = sizeof(struct mpi3_smp_passthrough_request);
	u16 reply_sz = sizeof(struct mpi3_smp_passthrough_reply);
	u16 ioc_status;

	if (mrioc->reset_in_progress) {
		ioc_err(mrioc, "%s: host reset in progress!\n", __func__);
		return -EFAULT;
	}

	data_out_sz = sizeof(struct rep_manu_request);
	data_in_sz = sizeof(struct rep_manu_reply);
	data_out = dma_alloc_coherent(&mrioc->pdev->dev,
	    data_out_sz + data_in_sz, &data_out_dma, GFP_KERNEL);
	if (!data_out) {
		rc = -ENOMEM;
		goto out;
	}

	data_in_dma = data_out_dma + data_out_sz;
	manufacture_reply = data_out + data_out_sz;

	manufacture_request = data_out;
	manufacture_request->smp_frame_type = 0x40;
	manufacture_request->function = 1;
	manufacture_request->reserved = 0;
	manufacture_request->request_length = 0;

	memset(&mpi_request, 0, request_sz);
	memset(&mpi_reply, 0, reply_sz);
	mpi_request.host_tag = cpu_to_le16(MPI3MR_HOSTTAG_TRANSPORT_CMDS);
	mpi_request.function = MPI3_FUNCTION_SMP_PASSTHROUGH;
	mpi_request.io_unit_port = (u8) port_id;
	mpi_request.sas_address = cpu_to_le64(sas_address);

	psge = &mpi_request.request_sge;
	mpi3mr_add_sg_single(psge, sgl_flags, data_out_sz, data_out_dma);

	psge = &mpi_request.response_sge;
	mpi3mr_add_sg_single(psge, sgl_flags, data_in_sz, data_in_dma);

	dprint_transport_info(mrioc,
	    "sending report manufacturer SMP request to sas_address(0x%016llx), port(%d)\n",
	    (unsigned long long)sas_address, port_id);

	if (mpi3mr_post_transport_req(mrioc, &mpi_request, request_sz,
	    &mpi_reply, reply_sz, MPI3MR_INTADMCMD_TIMEOUT, &ioc_status))
		goto out;

	dprint_transport_info(mrioc,
	    "report manufacturer SMP request completed with ioc_status(0x%04x)\n",
	    ioc_status);

	if (ioc_status == MPI3_IOCSTATUS_SUCCESS) {
		u8 *tmp;

		dprint_transport_info(mrioc,
		    "report manufacturer - reply data transfer size(%d)\n",
		    le16_to_cpu(mpi_reply.response_data_length));

		if (le16_to_cpu(mpi_reply.response_data_length) !=
		    sizeof(struct rep_manu_reply))
			goto out;

		strscpy(edev->vendor_id, manufacture_reply->vendor_id,
		     SAS_EXPANDER_VENDOR_ID_LEN);
		strscpy(edev->product_id, manufacture_reply->product_id,
		     SAS_EXPANDER_PRODUCT_ID_LEN);
		strscpy(edev->product_rev, manufacture_reply->product_rev,
		     SAS_EXPANDER_PRODUCT_REV_LEN);
		edev->level = manufacture_reply->sas_format & 1;
		if (edev->level) {
			strscpy(edev->component_vendor_id,
			    manufacture_reply->component_vendor_id,
			     SAS_EXPANDER_COMPONENT_VENDOR_ID_LEN);
			tmp = (u8 *)&manufacture_reply->component_id;
			edev->component_id = tmp[0] << 8 | tmp[1];
			edev->component_revision_id =
			    manufacture_reply->component_revision_id;
		}
	}

out:
	if (data_out)
		dma_free_coherent(&mrioc->pdev->dev, data_out_sz + data_in_sz,
		    data_out, data_out_dma);

	return rc;
}

/**
 * __mpi3mr_expander_find_by_handle - expander search by handle
 * @mrioc: Adapter instance reference
 * @handle: Firmware device handle of the expander
 *
 * Context: The caller should acquire sas_node_lock
 *
 * This searches for expander device based on handle, then
 * returns the sas_node object.
 *
 * Return: Expander sas_node object reference or NULL
 */
struct mpi3mr_sas_node *__mpi3mr_expander_find_by_handle(struct mpi3mr_ioc
	*mrioc, u16 handle)
{
	struct mpi3mr_sas_node *sas_expander, *r;

	r = NULL;
	list_for_each_entry(sas_expander, &mrioc->sas_expander_list, list) {
		if (sas_expander->handle != handle)
			continue;
		r = sas_expander;
		goto out;
	}
 out:
	return r;
}

/**
 * mpi3mr_is_expander_device - if device is an expander
 * @device_info: Bitfield providing information about the device
 *
 * Return: 1 if the device is expander device, else 0.
 */
u8 mpi3mr_is_expander_device(u16 device_info)
{
	if ((device_info & MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_MASK) ==
	     MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_EXPANDER)
		return 1;
	else
		return 0;
}

/**
 * mpi3mr_get_sas_address - retrieve sas_address for handle
 * @mrioc: Adapter instance reference
 * @handle: Firmware device handle
 * @sas_address: Address to hold sas address
 *
 * This function issues device page0 read for a given device
 * handle and gets the SAS address and return it back
 *
 * Return: 0 for success, non-zero for failure
 */
static int mpi3mr_get_sas_address(struct mpi3mr_ioc *mrioc, u16 handle,
	u64 *sas_address)
{
	struct mpi3_device_page0 dev_pg0;
	u16 ioc_status;
	struct mpi3_device0_sas_sata_format *sasinf;

	*sas_address = 0;

	if ((mpi3mr_cfg_get_dev_pg0(mrioc, &ioc_status, &dev_pg0,
	    sizeof(dev_pg0), MPI3_DEVICE_PGAD_FORM_HANDLE,
	    handle))) {
		ioc_err(mrioc, "%s: device page0 read failed\n", __func__);
		return -ENXIO;
	}

	if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "device page read failed for handle(0x%04x), with ioc_status(0x%04x) failure at %s:%d/%s()!\n",
		    handle, ioc_status, __FILE__, __LINE__, __func__);
		return -ENXIO;
	}

	if (le16_to_cpu(dev_pg0.flags) &
	    MPI3_DEVICE0_FLAGS_CONTROLLER_DEV_HANDLE)
		*sas_address = mrioc->sas_hba.sas_address;
	else if (dev_pg0.device_form == MPI3_DEVICE_DEVFORM_SAS_SATA) {
		sasinf = &dev_pg0.device_specific.sas_sata_format;
		*sas_address = le64_to_cpu(sasinf->sas_address);
	} else {
		ioc_err(mrioc, "%s: device_form(%d) is not SAS_SATA\n",
		    __func__, dev_pg0.device_form);
		return -ENXIO;
	}
	return 0;
}

/**
 * __mpi3mr_get_tgtdev_by_addr - target device search
 * @mrioc: Adapter instance reference
 * @sas_address: SAS address of the device
 * @hba_port: HBA port entry
 *
 * This searches for target device from sas address and hba port
 * pointer then return mpi3mr_tgt_dev object.
 *
 * Return: Valid tget_dev or NULL
 */
static struct mpi3mr_tgt_dev *__mpi3mr_get_tgtdev_by_addr(struct mpi3mr_ioc *mrioc,
	u64 sas_address, struct mpi3mr_hba_port *hba_port)
{
	struct mpi3mr_tgt_dev *tgtdev;

	assert_spin_locked(&mrioc->tgtdev_lock);

	list_for_each_entry(tgtdev, &mrioc->tgtdev_list, list)
		if ((tgtdev->dev_type == MPI3_DEVICE_DEVFORM_SAS_SATA) &&
		    (tgtdev->dev_spec.sas_sata_inf.sas_address == sas_address)
		    && (tgtdev->dev_spec.sas_sata_inf.hba_port == hba_port))
			goto found_device;
	return NULL;
found_device:
	mpi3mr_tgtdev_get(tgtdev);
	return tgtdev;
}

/**
 * mpi3mr_get_tgtdev_by_addr - target device search
 * @mrioc: Adapter instance reference
 * @sas_address: SAS address of the device
 * @hba_port: HBA port entry
 *
 * This searches for target device from sas address and hba port
 * pointer then return mpi3mr_tgt_dev object.
 *
 * Context: This function will acquire tgtdev_lock and will
 * release before returning the mpi3mr_tgt_dev object.
 *
 * Return: Valid tget_dev or NULL
 */
static struct mpi3mr_tgt_dev *mpi3mr_get_tgtdev_by_addr(struct mpi3mr_ioc *mrioc,
	u64 sas_address, struct mpi3mr_hba_port *hba_port)
{
	struct mpi3mr_tgt_dev *tgtdev = NULL;
	unsigned long flags;

	if (!hba_port)
		goto out;

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	tgtdev = __mpi3mr_get_tgtdev_by_addr(mrioc, sas_address, hba_port);
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);

out:
	return tgtdev;
}

/**
 * mpi3mr_remove_device_by_sas_address - remove the device
 * @mrioc: Adapter instance reference
 * @sas_address: SAS address of the device
 * @hba_port: HBA port entry
 *
 * This searches for target device using sas address and hba
 * port pointer then removes it from the OS.
 *
 * Return: None
 */
static void mpi3mr_remove_device_by_sas_address(struct mpi3mr_ioc *mrioc,
	u64 sas_address, struct mpi3mr_hba_port *hba_port)
{
	struct mpi3mr_tgt_dev *tgtdev = NULL;
	unsigned long flags;
	u8 was_on_tgtdev_list = 0;

	if (!hba_port)
		return;

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	tgtdev = __mpi3mr_get_tgtdev_by_addr(mrioc,
			 sas_address, hba_port);
	if (tgtdev) {
		if (!list_empty(&tgtdev->list)) {
			list_del_init(&tgtdev->list);
			was_on_tgtdev_list = 1;
			mpi3mr_tgtdev_put(tgtdev);
		}
	}
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);
	if (was_on_tgtdev_list) {
		if (tgtdev->host_exposed)
			mpi3mr_remove_tgtdev_from_host(mrioc, tgtdev);
		mpi3mr_tgtdev_put(tgtdev);
	}
}

/**
 * __mpi3mr_get_tgtdev_by_addr_and_rphy - target device search
 * @mrioc: Adapter instance reference
 * @sas_address: SAS address of the device
 * @rphy: SAS transport layer rphy object
 *
 * This searches for target device from sas address and rphy
 * pointer then return mpi3mr_tgt_dev object.
 *
 * Return: Valid tget_dev or NULL
 */
struct mpi3mr_tgt_dev *__mpi3mr_get_tgtdev_by_addr_and_rphy(
	struct mpi3mr_ioc *mrioc, u64 sas_address, struct sas_rphy *rphy)
{
	struct mpi3mr_tgt_dev *tgtdev;

	assert_spin_locked(&mrioc->tgtdev_lock);

	list_for_each_entry(tgtdev, &mrioc->tgtdev_list, list)
		if ((tgtdev->dev_type == MPI3_DEVICE_DEVFORM_SAS_SATA) &&
		    (tgtdev->dev_spec.sas_sata_inf.sas_address == sas_address)
		    && (tgtdev->dev_spec.sas_sata_inf.rphy == rphy))
			goto found_device;
	return NULL;
found_device:
	mpi3mr_tgtdev_get(tgtdev);
	return tgtdev;
}

/**
 * mpi3mr_expander_find_by_sas_address - sas expander search
 * @mrioc: Adapter instance reference
 * @sas_address: SAS address of expander
 * @hba_port: HBA port entry
 *
 * Return: A valid SAS expander node or NULL.
 *
 */
static struct mpi3mr_sas_node *mpi3mr_expander_find_by_sas_address(
	struct mpi3mr_ioc *mrioc, u64 sas_address,
	struct mpi3mr_hba_port *hba_port)
{
	struct mpi3mr_sas_node *sas_expander, *r = NULL;

	if (!hba_port)
		goto out;

	list_for_each_entry(sas_expander, &mrioc->sas_expander_list, list) {
		if ((sas_expander->sas_address != sas_address) ||
					 (sas_expander->hba_port != hba_port))
			continue;
		r = sas_expander;
		goto out;
	}
out:
	return r;
}

/**
 * __mpi3mr_sas_node_find_by_sas_address - sas node search
 * @mrioc: Adapter instance reference
 * @sas_address: SAS address of expander or sas host
 * @hba_port: HBA port entry
 * Context: Caller should acquire mrioc->sas_node_lock.
 *
 * If the SAS address indicates the device is direct attached to
 * the controller (controller's SAS address) then the SAS node
 * associated with the controller is returned back else the SAS
 * address and hba port are used to identify the exact expander
 * and the associated sas_node object is returned. If there is
 * no match NULL is returned.
 *
 * Return: A valid SAS node or NULL.
 *
 */
static struct mpi3mr_sas_node *__mpi3mr_sas_node_find_by_sas_address(
	struct mpi3mr_ioc *mrioc, u64 sas_address,
	struct mpi3mr_hba_port *hba_port)
{

	if (mrioc->sas_hba.sas_address == sas_address)
		return &mrioc->sas_hba;
	return mpi3mr_expander_find_by_sas_address(mrioc, sas_address,
	    hba_port);
}

/**
 * mpi3mr_parent_present - Is parent present for a phy
 * @mrioc: Adapter instance reference
 * @phy: SAS transport layer phy object
 *
 * Return: 0 if parent is present else non-zero
 */
static int mpi3mr_parent_present(struct mpi3mr_ioc *mrioc, struct sas_phy *phy)
{
	unsigned long flags;
	struct mpi3mr_hba_port *hba_port = phy->hostdata;

	spin_lock_irqsave(&mrioc->sas_node_lock, flags);
	if (__mpi3mr_sas_node_find_by_sas_address(mrioc,
	    phy->identify.sas_address,
	    hba_port) == NULL) {
		spin_unlock_irqrestore(&mrioc->sas_node_lock, flags);
		return -1;
	}
	spin_unlock_irqrestore(&mrioc->sas_node_lock, flags);
	return 0;
}

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
 * mpi3mr_delete_sas_port - helper function to removing a port
 * @mrioc: Adapter instance reference
 * @mr_sas_port: Internal Port object
 *
 * Return: None.
 */
static void  mpi3mr_delete_sas_port(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_sas_port *mr_sas_port)
{
	u64 sas_address = mr_sas_port->remote_identify.sas_address;
	struct mpi3mr_hba_port *hba_port = mr_sas_port->hba_port;
	enum sas_device_type device_type =
	    mr_sas_port->remote_identify.device_type;

	dev_info(&mr_sas_port->port->dev,
	    "remove: sas_address(0x%016llx)\n",
	    (unsigned long long) sas_address);

	if (device_type == SAS_END_DEVICE)
		mpi3mr_remove_device_by_sas_address(mrioc, sas_address,
		    hba_port);

	else if (device_type == SAS_EDGE_EXPANDER_DEVICE ||
	    device_type == SAS_FANOUT_EXPANDER_DEVICE)
		mpi3mr_expander_remove(mrioc, sas_address, hba_port);
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
			if ((mr_sas_port->num_phys == 1) &&
			    !mrioc->reset_in_progress)
				mpi3mr_delete_sas_port(mrioc, mr_sas_port);
			else
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

/**
 * mpi3mr_alloc_hba_port - alloc hba port object
 * @mrioc: Adapter instance reference
 * @port_id: Port number
 *
 * Alloc memory for hba port object.
 */
static struct mpi3mr_hba_port *
mpi3mr_alloc_hba_port(struct mpi3mr_ioc *mrioc, u16 port_id)
{
	struct mpi3mr_hba_port *hba_port;

	hba_port = kzalloc(sizeof(struct mpi3mr_hba_port),
	    GFP_KERNEL);
	if (!hba_port)
		return NULL;
	hba_port->port_id = port_id;
	ioc_info(mrioc, "hba_port entry: %p, port: %d is added to hba_port list\n",
	    hba_port, hba_port->port_id);
	list_add_tail(&hba_port->list, &mrioc->hba_port_table_list);
	return hba_port;
}

/**
 * mpi3mr_get_hba_port_by_id - find hba port by id
 * @mrioc: Adapter instance reference
 * @port_id - Port ID to search
 *
 * Return: mpi3mr_hba_port reference for the matched port
 */

struct mpi3mr_hba_port *mpi3mr_get_hba_port_by_id(struct mpi3mr_ioc *mrioc,
	u8 port_id)
{
	struct mpi3mr_hba_port *port, *port_next;

	list_for_each_entry_safe(port, port_next,
	    &mrioc->hba_port_table_list, list) {
		if (port->port_id != port_id)
			continue;
		if (port->flags & MPI3MR_HBA_PORT_FLAG_DIRTY)
			continue;
		return port;
	}

	return NULL;
}

/**
 * mpi3mr_update_links - refreshing SAS phy link changes
 * @mrioc: Adapter instance reference
 * @sas_address_parent: SAS address of parent expander or host
 * @handle: Firmware device handle of attached device
 * @phy_number: Phy number
 * @link_rate: New link rate
 * @hba_port: HBA port entry
 *
 * Return: None.
 */
void mpi3mr_update_links(struct mpi3mr_ioc *mrioc,
	u64 sas_address_parent, u16 handle, u8 phy_number, u8 link_rate,
	struct mpi3mr_hba_port *hba_port)
{
	unsigned long flags;
	struct mpi3mr_sas_node *mr_sas_node;
	struct mpi3mr_sas_phy *mr_sas_phy;

	if (mrioc->reset_in_progress)
		return;

	spin_lock_irqsave(&mrioc->sas_node_lock, flags);
	mr_sas_node = __mpi3mr_sas_node_find_by_sas_address(mrioc,
	    sas_address_parent, hba_port);
	if (!mr_sas_node) {
		spin_unlock_irqrestore(&mrioc->sas_node_lock, flags);
		return;
	}

	mr_sas_phy = &mr_sas_node->phy[phy_number];
	mr_sas_phy->attached_handle = handle;
	spin_unlock_irqrestore(&mrioc->sas_node_lock, flags);
	if (handle && (link_rate >= MPI3_SAS_NEG_LINK_RATE_1_5)) {
		mpi3mr_set_identify(mrioc, handle,
		    &mr_sas_phy->remote_identify);
		mpi3mr_add_phy_to_an_existing_port(mrioc, mr_sas_node,
		    mr_sas_phy, mr_sas_phy->remote_identify.sas_address,
		    hba_port);
	} else
		memset(&mr_sas_phy->remote_identify, 0, sizeof(struct
		    sas_identify));

	if (mr_sas_phy->phy)
		mr_sas_phy->phy->negotiated_linkrate =
		    mpi3mr_convert_phy_link_rate(link_rate);

	if ((mrioc->logging_level & MPI3_DEBUG_TRANSPORT_INFO))
		dev_info(&mr_sas_phy->phy->dev,
		    "refresh: parent sas_address(0x%016llx),\n"
		    "\tlink_rate(0x%02x), phy(%d)\n"
		    "\tattached_handle(0x%04x), sas_address(0x%016llx)\n",
		    (unsigned long long)sas_address_parent,
		    link_rate, phy_number, handle, (unsigned long long)
		    mr_sas_phy->remote_identify.sas_address);
}

/**
 * mpi3mr_sas_host_refresh - refreshing sas host object contents
 * @mrioc: Adapter instance reference
 *
 * This function refreshes the controllers phy information and
 * updates the SAS transport layer with updated information,
 * this is executed for each device addition or device info
 * change events
 *
 * Return: None.
 */
void mpi3mr_sas_host_refresh(struct mpi3mr_ioc *mrioc)
{
	int i;
	u8 link_rate;
	u16 sz, port_id, attached_handle;
	struct mpi3_sas_io_unit_page0 *sas_io_unit_pg0 = NULL;

	dprint_transport_info(mrioc,
	    "updating handles for sas_host(0x%016llx)\n",
	    (unsigned long long)mrioc->sas_hba.sas_address);

	sz = offsetof(struct mpi3_sas_io_unit_page0, phy_data) +
	    (mrioc->sas_hba.num_phys *
	     sizeof(struct mpi3_sas_io_unit0_phy_data));
	sas_io_unit_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!sas_io_unit_pg0)
		return;
	if (mpi3mr_cfg_get_sas_io_unit_pg0(mrioc, sas_io_unit_pg0, sz)) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		goto out;
	}

	mrioc->sas_hba.handle = 0;
	for (i = 0; i < mrioc->sas_hba.num_phys; i++) {
		if (sas_io_unit_pg0->phy_data[i].phy_flags &
		    (MPI3_SASIOUNIT0_PHYFLAGS_HOST_PHY |
		     MPI3_SASIOUNIT0_PHYFLAGS_VIRTUAL_PHY))
			continue;
		link_rate =
		    sas_io_unit_pg0->phy_data[i].negotiated_link_rate >> 4;
		if (!mrioc->sas_hba.handle)
			mrioc->sas_hba.handle = le16_to_cpu(
			    sas_io_unit_pg0->phy_data[i].controller_dev_handle);
		port_id = sas_io_unit_pg0->phy_data[i].io_unit_port;
		if (!(mpi3mr_get_hba_port_by_id(mrioc, port_id)))
			if (!mpi3mr_alloc_hba_port(mrioc, port_id))
				goto out;

		mrioc->sas_hba.phy[i].handle = mrioc->sas_hba.handle;
		attached_handle = le16_to_cpu(
		    sas_io_unit_pg0->phy_data[i].attached_dev_handle);
		if (attached_handle && link_rate < MPI3_SAS_NEG_LINK_RATE_1_5)
			link_rate = MPI3_SAS_NEG_LINK_RATE_1_5;
		mrioc->sas_hba.phy[i].hba_port =
			mpi3mr_get_hba_port_by_id(mrioc, port_id);
		mpi3mr_update_links(mrioc, mrioc->sas_hba.sas_address,
		    attached_handle, i, link_rate,
		    mrioc->sas_hba.phy[i].hba_port);
	}
 out:
	kfree(sas_io_unit_pg0);
}

/**
 * mpi3mr_sas_host_add - create sas host object
 * @mrioc: Adapter instance reference
 *
 * This function creates the controllers phy information and
 * updates the SAS transport layer with updated information,
 * this is executed for first device addition or device info
 * change event.
 *
 * Return: None.
 */
void mpi3mr_sas_host_add(struct mpi3mr_ioc *mrioc)
{
	int i;
	u16 sz, num_phys = 1, port_id, ioc_status;
	struct mpi3_sas_io_unit_page0 *sas_io_unit_pg0 = NULL;
	struct mpi3_sas_phy_page0 phy_pg0;
	struct mpi3_device_page0 dev_pg0;
	struct mpi3_enclosure_page0 encl_pg0;
	struct mpi3_device0_sas_sata_format *sasinf;

	sz = offsetof(struct mpi3_sas_io_unit_page0, phy_data) +
	    (num_phys * sizeof(struct mpi3_sas_io_unit0_phy_data));
	sas_io_unit_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!sas_io_unit_pg0)
		return;

	if (mpi3mr_cfg_get_sas_io_unit_pg0(mrioc, sas_io_unit_pg0, sz)) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		goto out;
	}
	num_phys = sas_io_unit_pg0->num_phys;
	kfree(sas_io_unit_pg0);

	mrioc->sas_hba.host_node = 1;
	INIT_LIST_HEAD(&mrioc->sas_hba.sas_port_list);
	mrioc->sas_hba.parent_dev = &mrioc->shost->shost_gendev;
	mrioc->sas_hba.phy = kcalloc(num_phys,
	    sizeof(struct mpi3mr_sas_phy), GFP_KERNEL);
	if (!mrioc->sas_hba.phy)
		return;

	mrioc->sas_hba.num_phys = num_phys;

	sz = offsetof(struct mpi3_sas_io_unit_page0, phy_data) +
	    (num_phys * sizeof(struct mpi3_sas_io_unit0_phy_data));
	sas_io_unit_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!sas_io_unit_pg0)
		return;

	if (mpi3mr_cfg_get_sas_io_unit_pg0(mrioc, sas_io_unit_pg0, sz)) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		goto out;
	}

	mrioc->sas_hba.handle = 0;
	for (i = 0; i < mrioc->sas_hba.num_phys; i++) {
		if (sas_io_unit_pg0->phy_data[i].phy_flags &
		    (MPI3_SASIOUNIT0_PHYFLAGS_HOST_PHY |
		    MPI3_SASIOUNIT0_PHYFLAGS_VIRTUAL_PHY))
			continue;
		if (mpi3mr_cfg_get_sas_phy_pg0(mrioc, &ioc_status, &phy_pg0,
		    sizeof(struct mpi3_sas_phy_page0),
		    MPI3_SAS_PHY_PGAD_FORM_PHY_NUMBER, i)) {
			ioc_err(mrioc, "failure at %s:%d/%s()!\n",
			    __FILE__, __LINE__, __func__);
			goto out;
		}
		if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
			ioc_err(mrioc, "failure at %s:%d/%s()!\n",
			    __FILE__, __LINE__, __func__);
			goto out;
		}

		if (!mrioc->sas_hba.handle)
			mrioc->sas_hba.handle = le16_to_cpu(
			    sas_io_unit_pg0->phy_data[i].controller_dev_handle);
		port_id = sas_io_unit_pg0->phy_data[i].io_unit_port;

		if (!(mpi3mr_get_hba_port_by_id(mrioc, port_id)))
			if (!mpi3mr_alloc_hba_port(mrioc, port_id))
				goto out;

		mrioc->sas_hba.phy[i].handle = mrioc->sas_hba.handle;
		mrioc->sas_hba.phy[i].phy_id = i;
		mrioc->sas_hba.phy[i].hba_port =
		    mpi3mr_get_hba_port_by_id(mrioc, port_id);
		mpi3mr_add_host_phy(mrioc, &mrioc->sas_hba.phy[i],
		    phy_pg0, mrioc->sas_hba.parent_dev);
	}
	if ((mpi3mr_cfg_get_dev_pg0(mrioc, &ioc_status, &dev_pg0,
	    sizeof(dev_pg0), MPI3_DEVICE_PGAD_FORM_HANDLE,
	    mrioc->sas_hba.handle))) {
		ioc_err(mrioc, "%s: device page0 read failed\n", __func__);
		goto out;
	}
	if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "device page read failed for handle(0x%04x), with ioc_status(0x%04x) failure at %s:%d/%s()!\n",
		    mrioc->sas_hba.handle, ioc_status, __FILE__, __LINE__,
		    __func__);
		goto out;
	}
	mrioc->sas_hba.enclosure_handle =
	    le16_to_cpu(dev_pg0.enclosure_handle);
	sasinf = &dev_pg0.device_specific.sas_sata_format;
	mrioc->sas_hba.sas_address =
	    le64_to_cpu(sasinf->sas_address);
	ioc_info(mrioc,
	    "host_add: handle(0x%04x), sas_addr(0x%016llx), phys(%d)\n",
	    mrioc->sas_hba.handle,
	    (unsigned long long) mrioc->sas_hba.sas_address,
	    mrioc->sas_hba.num_phys);

	if (mrioc->sas_hba.enclosure_handle) {
		if (!(mpi3mr_cfg_get_enclosure_pg0(mrioc, &ioc_status,
		    &encl_pg0, sizeof(dev_pg0),
		    MPI3_ENCLOS_PGAD_FORM_HANDLE,
		    mrioc->sas_hba.enclosure_handle)) &&
		    (ioc_status == MPI3_IOCSTATUS_SUCCESS))
			mrioc->sas_hba.enclosure_logical_id =
				le64_to_cpu(encl_pg0.enclosure_logical_id);
	}

out:
	kfree(sas_io_unit_pg0);
}

/**
 * mpi3mr_sas_port_add - Expose the SAS device to the SAS TL
 * @mrioc: Adapter instance reference
 * @handle: Firmware device handle of the attached device
 * @sas_address_parent: sas address of parent expander or host
 * @hba_port: HBA port entry
 *
 * This function creates a new sas port object for the given end
 * device matching sas address and hba_port and adds it to the
 * sas_node's sas_port_list and expose the attached sas device
 * to the SAS transport layer through sas_rphy_add.
 *
 * Returns a valid mpi3mr_sas_port reference or NULL.
 */
static struct mpi3mr_sas_port *mpi3mr_sas_port_add(struct mpi3mr_ioc *mrioc,
	u16 handle, u64 sas_address_parent, struct mpi3mr_hba_port *hba_port)
{
	struct mpi3mr_sas_phy *mr_sas_phy, *next;
	struct mpi3mr_sas_port *mr_sas_port;
	unsigned long flags;
	struct mpi3mr_sas_node *mr_sas_node;
	struct sas_rphy *rphy;
	struct mpi3mr_tgt_dev *tgtdev = NULL;
	int i;
	struct sas_port *port;

	if (!hba_port) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return NULL;
	}

	mr_sas_port = kzalloc(sizeof(struct mpi3mr_sas_port), GFP_KERNEL);
	if (!mr_sas_port)
		return NULL;

	INIT_LIST_HEAD(&mr_sas_port->port_list);
	INIT_LIST_HEAD(&mr_sas_port->phy_list);
	spin_lock_irqsave(&mrioc->sas_node_lock, flags);
	mr_sas_node = __mpi3mr_sas_node_find_by_sas_address(mrioc,
	    sas_address_parent, hba_port);
	spin_unlock_irqrestore(&mrioc->sas_node_lock, flags);

	if (!mr_sas_node) {
		ioc_err(mrioc, "%s:could not find parent sas_address(0x%016llx)!\n",
		    __func__, (unsigned long long)sas_address_parent);
		goto out_fail;
	}

	if ((mpi3mr_set_identify(mrioc, handle,
	    &mr_sas_port->remote_identify))) {
		ioc_err(mrioc,  "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		goto out_fail;
	}

	if (mr_sas_port->remote_identify.device_type == SAS_PHY_UNUSED) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		goto out_fail;
	}

	mr_sas_port->hba_port = hba_port;
	mpi3mr_sas_port_sanity_check(mrioc, mr_sas_node,
	    mr_sas_port->remote_identify.sas_address, hba_port);

	for (i = 0; i < mr_sas_node->num_phys; i++) {
		if ((mr_sas_node->phy[i].remote_identify.sas_address !=
		    mr_sas_port->remote_identify.sas_address) ||
		    (mr_sas_node->phy[i].hba_port != hba_port))
			continue;
		list_add_tail(&mr_sas_node->phy[i].port_siblings,
		    &mr_sas_port->phy_list);
		mr_sas_port->num_phys++;
	}

	if (!mr_sas_port->num_phys) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		goto out_fail;
	}

	if (mr_sas_port->remote_identify.device_type == SAS_END_DEVICE) {
		tgtdev = mpi3mr_get_tgtdev_by_addr(mrioc,
		    mr_sas_port->remote_identify.sas_address,
		    mr_sas_port->hba_port);

		if (!tgtdev) {
			ioc_err(mrioc, "failure at %s:%d/%s()!\n",
			    __FILE__, __LINE__, __func__);
			goto out_fail;
		}
		tgtdev->dev_spec.sas_sata_inf.pend_sas_rphy_add = 1;
	}

	if (!mr_sas_node->parent_dev) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		goto out_fail;
	}

	port = sas_port_alloc_num(mr_sas_node->parent_dev);
	if ((sas_port_add(port))) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		goto out_fail;
	}

	list_for_each_entry(mr_sas_phy, &mr_sas_port->phy_list,
	    port_siblings) {
		if ((mrioc->logging_level & MPI3_DEBUG_TRANSPORT_INFO))
			dev_info(&port->dev,
			    "add: handle(0x%04x), sas_address(0x%016llx), phy(%d)\n",
			    handle, (unsigned long long)
			    mr_sas_port->remote_identify.sas_address,
			    mr_sas_phy->phy_id);
		sas_port_add_phy(port, mr_sas_phy->phy);
		mr_sas_phy->phy_belongs_to_port = 1;
		mr_sas_phy->hba_port = hba_port;
	}

	mr_sas_port->port = port;
	if (mr_sas_port->remote_identify.device_type == SAS_END_DEVICE) {
		rphy = sas_end_device_alloc(port);
		tgtdev->dev_spec.sas_sata_inf.rphy = rphy;
	} else {
		rphy = sas_expander_alloc(port,
		    mr_sas_port->remote_identify.device_type);
	}
	rphy->identify = mr_sas_port->remote_identify;

	if (mrioc->current_event)
		mrioc->current_event->pending_at_sml = 1;

	if ((sas_rphy_add(rphy))) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
	}
	if (mr_sas_port->remote_identify.device_type == SAS_END_DEVICE) {
		tgtdev->dev_spec.sas_sata_inf.pend_sas_rphy_add = 0;
		tgtdev->dev_spec.sas_sata_inf.sas_transport_attached = 1;
		mpi3mr_tgtdev_put(tgtdev);
	}

	dev_info(&rphy->dev,
	    "%s: added: handle(0x%04x), sas_address(0x%016llx)\n",
	    __func__, handle, (unsigned long long)
	    mr_sas_port->remote_identify.sas_address);

	mr_sas_port->rphy = rphy;
	spin_lock_irqsave(&mrioc->sas_node_lock, flags);
	list_add_tail(&mr_sas_port->port_list, &mr_sas_node->sas_port_list);
	spin_unlock_irqrestore(&mrioc->sas_node_lock, flags);

	if (mrioc->current_event) {
		mrioc->current_event->pending_at_sml = 0;
		if (mrioc->current_event->discard)
			mpi3mr_print_device_event_notice(mrioc, true);
	}

	/* fill in report manufacture */
	if (mr_sas_port->remote_identify.device_type ==
	    SAS_EDGE_EXPANDER_DEVICE ||
	    mr_sas_port->remote_identify.device_type ==
	    SAS_FANOUT_EXPANDER_DEVICE)
		mpi3mr_report_manufacture(mrioc,
		    mr_sas_port->remote_identify.sas_address,
		    rphy_to_expander_device(rphy), hba_port->port_id);

	return mr_sas_port;

 out_fail:
	list_for_each_entry_safe(mr_sas_phy, next, &mr_sas_port->phy_list,
	    port_siblings)
		list_del(&mr_sas_phy->port_siblings);
	kfree(mr_sas_port);
	return NULL;
}

/**
 * mpi3mr_sas_port_remove - remove port from the list
 * @mrioc: Adapter instance reference
 * @sas_address: SAS address of attached device
 * @sas_address_parent: SAS address of parent expander or host
 * @hba_port: HBA port entry
 *
 * Removing object and freeing associated memory from the
 * sas_port_list.
 *
 * Return: None
 */
static void mpi3mr_sas_port_remove(struct mpi3mr_ioc *mrioc, u64 sas_address,
	u64 sas_address_parent, struct mpi3mr_hba_port *hba_port)
{
	int i;
	unsigned long flags;
	struct mpi3mr_sas_port *mr_sas_port, *next;
	struct mpi3mr_sas_node *mr_sas_node;
	u8 found = 0;
	struct mpi3mr_sas_phy *mr_sas_phy, *next_phy;
	struct mpi3mr_hba_port *srch_port, *hba_port_next = NULL;

	if (!hba_port)
		return;

	spin_lock_irqsave(&mrioc->sas_node_lock, flags);
	mr_sas_node = __mpi3mr_sas_node_find_by_sas_address(mrioc,
	    sas_address_parent, hba_port);
	if (!mr_sas_node) {
		spin_unlock_irqrestore(&mrioc->sas_node_lock, flags);
		return;
	}
	list_for_each_entry_safe(mr_sas_port, next, &mr_sas_node->sas_port_list,
	    port_list) {
		if (mr_sas_port->remote_identify.sas_address != sas_address)
			continue;
		if (mr_sas_port->hba_port != hba_port)
			continue;
		found = 1;
		list_del(&mr_sas_port->port_list);
		goto out;
	}

 out:
	if (!found) {
		spin_unlock_irqrestore(&mrioc->sas_node_lock, flags);
		return;
	}

	if (mr_sas_node->host_node) {
		list_for_each_entry_safe(srch_port, hba_port_next,
		    &mrioc->hba_port_table_list, list) {
			if (srch_port != hba_port)
				continue;
			ioc_info(mrioc,
			    "removing hba_port entry: %p port: %d from hba_port list\n",
			    srch_port, srch_port->port_id);
			list_del(&hba_port->list);
			kfree(hba_port);
			break;
		}
	}

	for (i = 0; i < mr_sas_node->num_phys; i++) {
		if (mr_sas_node->phy[i].remote_identify.sas_address ==
		    sas_address)
			memset(&mr_sas_node->phy[i].remote_identify, 0,
			    sizeof(struct sas_identify));
	}

	spin_unlock_irqrestore(&mrioc->sas_node_lock, flags);

	if (mrioc->current_event)
		mrioc->current_event->pending_at_sml = 1;

	list_for_each_entry_safe(mr_sas_phy, next_phy,
	    &mr_sas_port->phy_list, port_siblings) {
		if ((mrioc->logging_level & MPI3_DEBUG_TRANSPORT_INFO))
			dev_info(&mr_sas_port->port->dev,
			    "remove: sas_address(0x%016llx), phy(%d)\n",
			    (unsigned long long)
			    mr_sas_port->remote_identify.sas_address,
			    mr_sas_phy->phy_id);
		mr_sas_phy->phy_belongs_to_port = 0;
		if (!mrioc->stop_drv_processing)
			sas_port_delete_phy(mr_sas_port->port,
			    mr_sas_phy->phy);
		list_del(&mr_sas_phy->port_siblings);
	}
	if (!mrioc->stop_drv_processing)
		sas_port_delete(mr_sas_port->port);
	ioc_info(mrioc, "%s: removed sas_address(0x%016llx)\n",
	    __func__, (unsigned long long)sas_address);

	if (mrioc->current_event) {
		mrioc->current_event->pending_at_sml = 0;
		if (mrioc->current_event->discard)
			mpi3mr_print_device_event_notice(mrioc, false);
	}

	kfree(mr_sas_port);
}

/**
 * mpi3mr_expander_node_add - insert an expander to the list.
 * @mrioc: Adapter instance reference
 * @sas_expander: Expander sas node
 * Context: This function will acquire sas_node_lock.
 *
 * Adding new object to the ioc->sas_expander_list.
 *
 * Return: None.
 */
static void mpi3mr_expander_node_add(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_sas_node *sas_expander)
{
	unsigned long flags;

	spin_lock_irqsave(&mrioc->sas_node_lock, flags);
	list_add_tail(&sas_expander->list, &mrioc->sas_expander_list);
	spin_unlock_irqrestore(&mrioc->sas_node_lock, flags);
}

/**
 * mpi3mr_expander_add -  Create expander object
 * @mrioc: Adapter instance reference
 * @handle: Expander firmware device handle
 *
 * This function creating expander object, stored in
 * sas_expander_list and expose it to the SAS transport
 * layer.
 *
 * Return: 0 for success, non-zero for failure.
 */
int mpi3mr_expander_add(struct mpi3mr_ioc *mrioc, u16 handle)
{
	struct mpi3mr_sas_node *sas_expander;
	struct mpi3mr_enclosure_node *enclosure_dev;
	struct mpi3_sas_expander_page0 expander_pg0;
	struct mpi3_sas_expander_page1 expander_pg1;
	u16 ioc_status, parent_handle, temp_handle;
	u64 sas_address, sas_address_parent = 0;
	int i;
	unsigned long flags;
	u8 port_id, link_rate;
	struct mpi3mr_sas_port *mr_sas_port = NULL;
	struct mpi3mr_hba_port *hba_port;
	u32 phynum_handle;
	int rc = 0;

	if (!handle)
		return -1;

	if (mrioc->reset_in_progress)
		return -1;

	if ((mpi3mr_cfg_get_sas_exp_pg0(mrioc, &ioc_status, &expander_pg0,
	    sizeof(expander_pg0), MPI3_SAS_EXPAND_PGAD_FORM_HANDLE, handle))) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -1;
	}

	if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -1;
	}

	parent_handle = le16_to_cpu(expander_pg0.parent_dev_handle);
	if (mpi3mr_get_sas_address(mrioc, parent_handle, &sas_address_parent)
	    != 0) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -1;
	}

	port_id = expander_pg0.io_unit_port;
	hba_port = mpi3mr_get_hba_port_by_id(mrioc, port_id);
	if (!hba_port) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -1;
	}

	if (sas_address_parent != mrioc->sas_hba.sas_address) {
		spin_lock_irqsave(&mrioc->sas_node_lock, flags);
		sas_expander =
		   mpi3mr_expander_find_by_sas_address(mrioc,
		    sas_address_parent, hba_port);
		spin_unlock_irqrestore(&mrioc->sas_node_lock, flags);
		if (!sas_expander) {
			rc = mpi3mr_expander_add(mrioc, parent_handle);
			if (rc != 0)
				return rc;
		} else {
			/*
			 * When there is a parent expander present, update it's
			 * phys where child expander is connected with the link
			 * speed, attached dev handle and sas address.
			 */
			for (i = 0 ; i < sas_expander->num_phys ; i++) {
				phynum_handle =
				    (i << MPI3_SAS_EXPAND_PGAD_PHYNUM_SHIFT) |
				    parent_handle;
				if (mpi3mr_cfg_get_sas_exp_pg1(mrioc,
				    &ioc_status, &expander_pg1,
				    sizeof(expander_pg1),
				    MPI3_SAS_EXPAND_PGAD_FORM_HANDLE_PHY_NUM,
				    phynum_handle)) {
					ioc_err(mrioc, "failure at %s:%d/%s()!\n",
					    __FILE__, __LINE__, __func__);
					rc = -1;
					return rc;
				}
				if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
					ioc_err(mrioc, "failure at %s:%d/%s()!\n",
					    __FILE__, __LINE__, __func__);
					rc = -1;
					return rc;
				}
				temp_handle = le16_to_cpu(
				    expander_pg1.attached_dev_handle);
				if (temp_handle != handle)
					continue;
				link_rate = (expander_pg1.negotiated_link_rate &
				    MPI3_SAS_NEG_LINK_RATE_LOGICAL_MASK) >>
				    MPI3_SAS_NEG_LINK_RATE_LOGICAL_SHIFT;
				mpi3mr_update_links(mrioc, sas_address_parent,
				    handle, i, link_rate, hba_port);
			}
		}
	}

	spin_lock_irqsave(&mrioc->sas_node_lock, flags);
	sas_address = le64_to_cpu(expander_pg0.sas_address);
	sas_expander = mpi3mr_expander_find_by_sas_address(mrioc,
	    sas_address, hba_port);
	spin_unlock_irqrestore(&mrioc->sas_node_lock, flags);

	if (sas_expander)
		return 0;

	sas_expander = kzalloc(sizeof(struct mpi3mr_sas_node),
	    GFP_KERNEL);
	if (!sas_expander)
		return -1;

	sas_expander->handle = handle;
	sas_expander->num_phys = expander_pg0.num_phys;
	sas_expander->sas_address_parent = sas_address_parent;
	sas_expander->sas_address = sas_address;
	sas_expander->hba_port = hba_port;

	ioc_info(mrioc,
	    "expander_add: handle(0x%04x), parent(0x%04x), sas_addr(0x%016llx), phys(%d)\n",
	    handle, parent_handle, (unsigned long long)
	    sas_expander->sas_address, sas_expander->num_phys);

	if (!sas_expander->num_phys) {
		rc = -1;
		goto out_fail;
	}
	sas_expander->phy = kcalloc(sas_expander->num_phys,
	    sizeof(struct mpi3mr_sas_phy), GFP_KERNEL);
	if (!sas_expander->phy) {
		rc = -1;
		goto out_fail;
	}

	INIT_LIST_HEAD(&sas_expander->sas_port_list);
	mr_sas_port = mpi3mr_sas_port_add(mrioc, handle, sas_address_parent,
	    sas_expander->hba_port);
	if (!mr_sas_port) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		rc = -1;
		goto out_fail;
	}
	sas_expander->parent_dev = &mr_sas_port->rphy->dev;
	sas_expander->rphy = mr_sas_port->rphy;

	for (i = 0 ; i < sas_expander->num_phys ; i++) {
		phynum_handle = (i << MPI3_SAS_EXPAND_PGAD_PHYNUM_SHIFT) |
		    handle;
		if (mpi3mr_cfg_get_sas_exp_pg1(mrioc, &ioc_status,
		    &expander_pg1, sizeof(expander_pg1),
		    MPI3_SAS_EXPAND_PGAD_FORM_HANDLE_PHY_NUM,
		    phynum_handle)) {
			ioc_err(mrioc, "failure at %s:%d/%s()!\n",
			    __FILE__, __LINE__, __func__);
			rc = -1;
			goto out_fail;
		}
		if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
			ioc_err(mrioc, "failure at %s:%d/%s()!\n",
			    __FILE__, __LINE__, __func__);
			rc = -1;
			goto out_fail;
		}

		sas_expander->phy[i].handle = handle;
		sas_expander->phy[i].phy_id = i;
		sas_expander->phy[i].hba_port = hba_port;

		if ((mpi3mr_add_expander_phy(mrioc, &sas_expander->phy[i],
		    expander_pg1, sas_expander->parent_dev))) {
			ioc_err(mrioc, "failure at %s:%d/%s()!\n",
			    __FILE__, __LINE__, __func__);
			rc = -1;
			goto out_fail;
		}
	}

	if (sas_expander->enclosure_handle) {
		enclosure_dev =
			mpi3mr_enclosure_find_by_handle(mrioc,
						sas_expander->enclosure_handle);
		if (enclosure_dev)
			sas_expander->enclosure_logical_id = le64_to_cpu(
			    enclosure_dev->pg0.enclosure_logical_id);
	}

	mpi3mr_expander_node_add(mrioc, sas_expander);
	return 0;

out_fail:

	if (mr_sas_port)
		mpi3mr_sas_port_remove(mrioc,
		    sas_expander->sas_address,
		    sas_address_parent, sas_expander->hba_port);
	kfree(sas_expander->phy);
	kfree(sas_expander);
	return rc;
}

/**
 * mpi3mr_expander_node_remove - recursive removal of expander.
 * @mrioc: Adapter instance reference
 * @sas_expander: Expander device object
 *
 * Removes expander object and freeing associated memory from
 * the sas_expander_list and removes the same from SAS TL, if
 * one of the attached device is an expander then it recursively
 * removes the expander device too.
 *
 * Return nothing.
 */
static void mpi3mr_expander_node_remove(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_sas_node *sas_expander)
{
	struct mpi3mr_sas_port *mr_sas_port, *next;
	unsigned long flags;
	u8 port_id;

	/* remove sibling ports attached to this expander */
	list_for_each_entry_safe(mr_sas_port, next,
	   &sas_expander->sas_port_list, port_list) {
		if (mrioc->reset_in_progress)
			return;
		if (mr_sas_port->remote_identify.device_type ==
		    SAS_END_DEVICE)
			mpi3mr_remove_device_by_sas_address(mrioc,
			    mr_sas_port->remote_identify.sas_address,
			    mr_sas_port->hba_port);
		else if (mr_sas_port->remote_identify.device_type ==
		    SAS_EDGE_EXPANDER_DEVICE ||
		    mr_sas_port->remote_identify.device_type ==
		    SAS_FANOUT_EXPANDER_DEVICE)
			mpi3mr_expander_remove(mrioc,
			    mr_sas_port->remote_identify.sas_address,
			    mr_sas_port->hba_port);
	}

	port_id = sas_expander->hba_port->port_id;
	mpi3mr_sas_port_remove(mrioc, sas_expander->sas_address,
	    sas_expander->sas_address_parent, sas_expander->hba_port);

	ioc_info(mrioc, "expander_remove: handle(0x%04x), sas_addr(0x%016llx), port:%d\n",
	    sas_expander->handle, (unsigned long long)
	    sas_expander->sas_address, port_id);

	spin_lock_irqsave(&mrioc->sas_node_lock, flags);
	list_del(&sas_expander->list);
	spin_unlock_irqrestore(&mrioc->sas_node_lock, flags);

	kfree(sas_expander->phy);
	kfree(sas_expander);
}

/**
 * mpi3mr_expander_remove - Remove expander object
 * @mrioc: Adapter instance reference
 * @sas_address: Remove expander sas_address
 * @hba_port: HBA port reference
 *
 * This function remove expander object, stored in
 * mrioc->sas_expander_list and removes it from the SAS TL by
 * calling mpi3mr_expander_node_remove().
 *
 * Return: None
 */
void mpi3mr_expander_remove(struct mpi3mr_ioc *mrioc, u64 sas_address,
	struct mpi3mr_hba_port *hba_port)
{
	struct mpi3mr_sas_node *sas_expander;
	unsigned long flags;

	if (mrioc->reset_in_progress)
		return;

	if (!hba_port)
		return;

	spin_lock_irqsave(&mrioc->sas_node_lock, flags);
	sas_expander = mpi3mr_expander_find_by_sas_address(mrioc, sas_address,
	    hba_port);
	spin_unlock_irqrestore(&mrioc->sas_node_lock, flags);
	if (sas_expander)
		mpi3mr_expander_node_remove(mrioc, sas_expander);

}

/**
 * mpi3mr_get_sas_negotiated_logical_linkrate - get linkrate
 * @mrioc: Adapter instance reference
 * @tgtdev: Target device
 *
 * This function identifies whether the target device is
 * attached directly or through expander and issues sas phy
 * page0 or expander phy page1 and gets the link rate, if there
 * is any failure in reading the pages then this returns link
 * rate of 1.5.
 *
 * Return: logical link rate.
 */
static u8 mpi3mr_get_sas_negotiated_logical_linkrate(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_tgt_dev *tgtdev)
{
	u8 link_rate = MPI3_SAS_NEG_LINK_RATE_1_5, phy_number;
	struct mpi3_sas_expander_page1 expander_pg1;
	struct mpi3_sas_phy_page0 phy_pg0;
	u32 phynum_handle;
	u16 ioc_status;

	phy_number = tgtdev->dev_spec.sas_sata_inf.phy_id;
	if (!(tgtdev->devpg0_flag & MPI3_DEVICE0_FLAGS_ATT_METHOD_DIR_ATTACHED)) {
		phynum_handle = ((phy_number<<MPI3_SAS_EXPAND_PGAD_PHYNUM_SHIFT)
				 | tgtdev->parent_handle);
		if (mpi3mr_cfg_get_sas_exp_pg1(mrioc, &ioc_status,
		    &expander_pg1, sizeof(expander_pg1),
		    MPI3_SAS_EXPAND_PGAD_FORM_HANDLE_PHY_NUM,
		    phynum_handle)) {
			ioc_err(mrioc, "failure at %s:%d/%s()!\n",
			    __FILE__, __LINE__, __func__);
			goto out;
		}
		if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
			ioc_err(mrioc, "failure at %s:%d/%s()!\n",
			    __FILE__, __LINE__, __func__);
			goto out;
		}
		link_rate = (expander_pg1.negotiated_link_rate &
			     MPI3_SAS_NEG_LINK_RATE_LOGICAL_MASK) >>
			MPI3_SAS_NEG_LINK_RATE_LOGICAL_SHIFT;
		goto out;
	}
	if (mpi3mr_cfg_get_sas_phy_pg0(mrioc, &ioc_status, &phy_pg0,
	    sizeof(struct mpi3_sas_phy_page0),
	    MPI3_SAS_PHY_PGAD_FORM_PHY_NUMBER, phy_number)) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		goto out;
	}
	if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		goto out;
	}
	link_rate = (phy_pg0.negotiated_link_rate &
		     MPI3_SAS_NEG_LINK_RATE_LOGICAL_MASK) >>
		MPI3_SAS_NEG_LINK_RATE_LOGICAL_SHIFT;
out:
	return link_rate;
}

/**
 * mpi3mr_report_tgtdev_to_sas_transport - expose dev to SAS TL
 * @mrioc: Adapter instance reference
 * @tgtdev: Target device
 *
 * This function exposes the target device after
 * preparing host_phy, setting up link rate etc.
 *
 * Return: 0 on success, non-zero for failure.
 */
int mpi3mr_report_tgtdev_to_sas_transport(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_tgt_dev *tgtdev)
{
	int retval = 0;
	u8 link_rate, parent_phy_number;
	u64 sas_address_parent, sas_address;
	struct mpi3mr_hba_port *hba_port;
	u8 port_id;

	if ((tgtdev->dev_type != MPI3_DEVICE_DEVFORM_SAS_SATA) ||
	    !mrioc->sas_transport_enabled)
		return -1;

	sas_address = tgtdev->dev_spec.sas_sata_inf.sas_address;
	if (!mrioc->sas_hba.num_phys)
		mpi3mr_sas_host_add(mrioc);
	else
		mpi3mr_sas_host_refresh(mrioc);

	if (mpi3mr_get_sas_address(mrioc, tgtdev->parent_handle,
	    &sas_address_parent) != 0) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -1;
	}
	tgtdev->dev_spec.sas_sata_inf.sas_address_parent = sas_address_parent;

	parent_phy_number = tgtdev->dev_spec.sas_sata_inf.phy_id;
	port_id = tgtdev->io_unit_port;

	hba_port = mpi3mr_get_hba_port_by_id(mrioc, port_id);
	if (!hba_port) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -1;
	}
	tgtdev->dev_spec.sas_sata_inf.hba_port = hba_port;

	link_rate = mpi3mr_get_sas_negotiated_logical_linkrate(mrioc, tgtdev);

	mpi3mr_update_links(mrioc, sas_address_parent, tgtdev->dev_handle,
	    parent_phy_number, link_rate, hba_port);

	tgtdev->host_exposed = 1;
	if (!mpi3mr_sas_port_add(mrioc, tgtdev->dev_handle,
	    sas_address_parent, hba_port)) {
		tgtdev->host_exposed = 0;
		retval = -1;
	} else if ((!tgtdev->starget)) {
		if (!mrioc->is_driver_loading)
			mpi3mr_sas_port_remove(mrioc, sas_address,
			    sas_address_parent, hba_port);
		tgtdev->host_exposed = 0;
		retval = -1;
	}
	return retval;
}

/**
 * mpi3mr_remove_tgtdev_from_sas_transport - remove from SAS TL
 * @mrioc: Adapter instance reference
 * @tgtdev: Target device
 *
 * This function removes the target device
 *
 * Return: None.
 */
void mpi3mr_remove_tgtdev_from_sas_transport(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_tgt_dev *tgtdev)
{
	u64 sas_address_parent, sas_address;
	struct mpi3mr_hba_port *hba_port;

	if ((tgtdev->dev_type != MPI3_DEVICE_DEVFORM_SAS_SATA) ||
	    !mrioc->sas_transport_enabled)
		return;

	hba_port = tgtdev->dev_spec.sas_sata_inf.hba_port;
	sas_address = tgtdev->dev_spec.sas_sata_inf.sas_address;
	sas_address_parent = tgtdev->dev_spec.sas_sata_inf.sas_address_parent;
	mpi3mr_sas_port_remove(mrioc, sas_address, sas_address_parent,
	    hba_port);
	tgtdev->host_exposed = 0;
}
