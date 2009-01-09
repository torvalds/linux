/*
 * Serial Attached SCSI (SAS) Expander discovery and configuration
 *
 * Copyright (C) 2007 James E.J. Bottomley
 *		<James.Bottomley@HansenPartnership.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 only.
 */
#include <linux/scatterlist.h>
#include <linux/blkdev.h>

#include "sas_internal.h"

#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_sas.h>
#include "../scsi_sas_internal.h"

static void sas_host_smp_discover(struct sas_ha_struct *sas_ha, u8 *resp_data,
				  u8 phy_id)
{
	struct sas_phy *phy;
	struct sas_rphy *rphy;

	if (phy_id >= sas_ha->num_phys) {
		resp_data[2] = SMP_RESP_NO_PHY;
		return;
	}
	resp_data[2] = SMP_RESP_FUNC_ACC;

	phy = sas_ha->sas_phy[phy_id]->phy;
	resp_data[9] = phy_id;
	resp_data[13] = phy->negotiated_linkrate;
	memcpy(resp_data + 16, sas_ha->sas_addr, SAS_ADDR_SIZE);
	memcpy(resp_data + 24, sas_ha->sas_phy[phy_id]->attached_sas_addr,
	       SAS_ADDR_SIZE);
	resp_data[40] = (phy->minimum_linkrate << 4) |
		phy->minimum_linkrate_hw;
	resp_data[41] = (phy->maximum_linkrate << 4) |
		phy->maximum_linkrate_hw;

	if (!sas_ha->sas_phy[phy_id]->port ||
	    !sas_ha->sas_phy[phy_id]->port->port_dev)
		return;

	rphy = sas_ha->sas_phy[phy_id]->port->port_dev->rphy;
	resp_data[12] = rphy->identify.device_type << 4;
	resp_data[14] = rphy->identify.initiator_port_protocols;
	resp_data[15] = rphy->identify.target_port_protocols;
}

static void sas_report_phy_sata(struct sas_ha_struct *sas_ha, u8 *resp_data,
				u8 phy_id)
{
	struct sas_rphy *rphy;
	struct dev_to_host_fis *fis;
	int i;

	if (phy_id >= sas_ha->num_phys) {
		resp_data[2] = SMP_RESP_NO_PHY;
		return;
	}

	resp_data[2] = SMP_RESP_PHY_NO_SATA;

	if (!sas_ha->sas_phy[phy_id]->port)
		return;

	rphy = sas_ha->sas_phy[phy_id]->port->port_dev->rphy;
	fis = (struct dev_to_host_fis *)
		sas_ha->sas_phy[phy_id]->port->port_dev->frame_rcvd;
	if (rphy->identify.target_port_protocols != SAS_PROTOCOL_SATA)
		return;

	resp_data[2] = SMP_RESP_FUNC_ACC;
	resp_data[9] = phy_id;
	memcpy(resp_data + 16, sas_ha->sas_phy[phy_id]->attached_sas_addr,
	       SAS_ADDR_SIZE);

	/* check to see if we have a valid d2h fis */
	if (fis->fis_type != 0x34)
		return;

	/* the d2h fis is required by the standard to be in LE format */
	for (i = 0; i < 20; i += 4) {
		u8 *dst = resp_data + 24 + i, *src =
			&sas_ha->sas_phy[phy_id]->port->port_dev->frame_rcvd[i];
		dst[0] = src[3];
		dst[1] = src[2];
		dst[2] = src[1];
		dst[3] = src[0];
	}
}

static void sas_phy_control(struct sas_ha_struct *sas_ha, u8 phy_id,
			    u8 phy_op, enum sas_linkrate min,
			    enum sas_linkrate max, u8 *resp_data)
{
	struct sas_internal *i =
		to_sas_internal(sas_ha->core.shost->transportt);
	struct sas_phy_linkrates rates;

	if (phy_id >= sas_ha->num_phys) {
		resp_data[2] = SMP_RESP_NO_PHY;
		return;
	}
	switch (phy_op) {
	case PHY_FUNC_NOP:
	case PHY_FUNC_LINK_RESET:
	case PHY_FUNC_HARD_RESET:
	case PHY_FUNC_DISABLE:
	case PHY_FUNC_CLEAR_ERROR_LOG:
	case PHY_FUNC_CLEAR_AFFIL:
	case PHY_FUNC_TX_SATA_PS_SIGNAL:
		break;

	default:
		resp_data[2] = SMP_RESP_PHY_UNK_OP;
		return;
	}

	rates.minimum_linkrate = min;
	rates.maximum_linkrate = max;

	if (i->dft->lldd_control_phy(sas_ha->sas_phy[phy_id], phy_op, &rates))
		resp_data[2] = SMP_RESP_FUNC_FAILED;
	else
		resp_data[2] = SMP_RESP_FUNC_ACC;
}

int sas_smp_host_handler(struct Scsi_Host *shost, struct request *req,
			 struct request *rsp)
{
	u8 *req_data = NULL, *resp_data = NULL, *buf;
	struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(shost);
	int error = -EINVAL, resp_data_len = rsp->data_len;

	/* eight is the minimum size for request and response frames */
	if (req->data_len < 8 || rsp->data_len < 8)
		goto out;

	if (bio_offset(req->bio) + req->data_len > PAGE_SIZE ||
	    bio_offset(rsp->bio) + rsp->data_len > PAGE_SIZE) {
		shost_printk(KERN_ERR, shost,
			"SMP request/response frame crosses page boundary");
		goto out;
	}

	req_data = kzalloc(req->data_len, GFP_KERNEL);

	/* make sure frame can always be built ... we copy
	 * back only the requested length */
	resp_data = kzalloc(max(rsp->data_len, 128U), GFP_KERNEL);

	if (!req_data || !resp_data) {
		error = -ENOMEM;
		goto out;
	}

	local_irq_disable();
	buf = kmap_atomic(bio_page(req->bio), KM_USER0) + bio_offset(req->bio);
	memcpy(req_data, buf, req->data_len);
	kunmap_atomic(buf - bio_offset(req->bio), KM_USER0);
	local_irq_enable();

	if (req_data[0] != SMP_REQUEST)
		goto out;

	/* always succeeds ... even if we can't process the request
	 * the result is in the response frame */
	error = 0;

	/* set up default don't know response */
	resp_data[0] = SMP_RESPONSE;
	resp_data[1] = req_data[1];
	resp_data[2] = SMP_RESP_FUNC_UNK;

	switch (req_data[1]) {
	case SMP_REPORT_GENERAL:
		req->data_len -= 8;
		resp_data_len -= 32;
		resp_data[2] = SMP_RESP_FUNC_ACC;
		resp_data[9] = sas_ha->num_phys;
		break;

	case SMP_REPORT_MANUF_INFO:
		req->data_len -= 8;
		resp_data_len -= 64;
		resp_data[2] = SMP_RESP_FUNC_ACC;
		memcpy(resp_data + 12, shost->hostt->name,
		       SAS_EXPANDER_VENDOR_ID_LEN);
		memcpy(resp_data + 20, "libsas virt phy",
		       SAS_EXPANDER_PRODUCT_ID_LEN);
		break;

	case SMP_READ_GPIO_REG:
		/* FIXME: need GPIO support in the transport class */
		break;

	case SMP_DISCOVER:
		req->data_len -= 16;
		if ((int)req->data_len < 0) {
			req->data_len = 0;
			error = -EINVAL;
			goto out;
		}
		resp_data_len -= 56;
		sas_host_smp_discover(sas_ha, resp_data, req_data[9]);
		break;

	case SMP_REPORT_PHY_ERR_LOG:
		/* FIXME: could implement this with additional
		 * libsas callbacks providing the HW supports it */
		break;

	case SMP_REPORT_PHY_SATA:
		req->data_len -= 16;
		if ((int)req->data_len < 0) {
			req->data_len = 0;
			error = -EINVAL;
			goto out;
		}
		resp_data_len -= 60;
		sas_report_phy_sata(sas_ha, resp_data, req_data[9]);
		break;

	case SMP_REPORT_ROUTE_INFO:
		/* Can't implement; hosts have no routes */
		break;

	case SMP_WRITE_GPIO_REG:
		/* FIXME: need GPIO support in the transport class */
		break;

	case SMP_CONF_ROUTE_INFO:
		/* Can't implement; hosts have no routes */
		break;

	case SMP_PHY_CONTROL:
		req->data_len -= 44;
		if ((int)req->data_len < 0) {
			req->data_len = 0;
			error = -EINVAL;
			goto out;
		}
		resp_data_len -= 8;
		sas_phy_control(sas_ha, req_data[9], req_data[10],
				req_data[32] >> 4, req_data[33] >> 4,
				resp_data);
		break;

	case SMP_PHY_TEST_FUNCTION:
		/* FIXME: should this be implemented? */
		break;

	default:
		/* probably a 2.0 function */
		break;
	}

	local_irq_disable();
	buf = kmap_atomic(bio_page(rsp->bio), KM_USER0) + bio_offset(rsp->bio);
	memcpy(buf, resp_data, rsp->data_len);
	flush_kernel_dcache_page(bio_page(rsp->bio));
	kunmap_atomic(buf - bio_offset(rsp->bio), KM_USER0);
	local_irq_enable();
	rsp->data_len = resp_data_len;

 out:
	kfree(req_data);
	kfree(resp_data);
	return error;
}
