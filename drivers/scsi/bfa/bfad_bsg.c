/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/uaccess.h>
#include "bfad_drv.h"
#include "bfad_im.h"
#include "bfad_bsg.h"

BFA_TRC_FILE(LDRV, BSG);

/* bfad_im_bsg_get_kobject - increment the bfa refcnt */
static void
bfad_im_bsg_get_kobject(struct fc_bsg_job *job)
{
	struct Scsi_Host *shost = job->shost;
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	__module_get(shost->dma_dev->driver->owner);
	spin_unlock_irqrestore(shost->host_lock, flags);
}

/* bfad_im_bsg_put_kobject - decrement the bfa refcnt */
static void
bfad_im_bsg_put_kobject(struct fc_bsg_job *job)
{
	struct Scsi_Host *shost = job->shost;
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	module_put(shost->dma_dev->driver->owner);
	spin_unlock_irqrestore(shost->host_lock, flags);
}

static int
bfad_iocmd_ioc_get_info(struct bfad_s *bfad, void *cmd)
{
	int	i;
	struct bfa_bsg_ioc_info_s *iocmd = (struct bfa_bsg_ioc_info_s *)cmd;
	struct bfad_im_port_s	*im_port;
	struct bfa_port_attr_s	pattr;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfa_fcport_get_attr(&bfad->bfa, &pattr);
	iocmd->nwwn = pattr.nwwn;
	iocmd->pwwn = pattr.pwwn;
	iocmd->ioc_type = bfa_get_type(&bfad->bfa);
	iocmd->mac = bfa_get_mac(&bfad->bfa);
	iocmd->factory_mac = bfa_get_mfg_mac(&bfad->bfa);
	bfa_get_adapter_serial_num(&bfad->bfa, iocmd->serialnum);
	iocmd->factorynwwn = pattr.factorynwwn;
	iocmd->factorypwwn = pattr.factorypwwn;
	im_port = bfad->pport.im_port;
	iocmd->host = im_port->shost->host_no;
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	strcpy(iocmd->name, bfad->adapter_name);
	strcpy(iocmd->port_name, bfad->port_name);
	strcpy(iocmd->hwpath, bfad->pci_name);

	/* set adapter hw path */
	strcpy(iocmd->adapter_hwpath, bfad->pci_name);
	i = strlen(iocmd->adapter_hwpath) - 1;
	while (iocmd->adapter_hwpath[i] != '.')
		i--;
	iocmd->adapter_hwpath[i] = '\0';
	iocmd->status = BFA_STATUS_OK;
	return 0;
}

static int
bfad_iocmd_ioc_get_attr(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_ioc_attr_s *iocmd = (struct bfa_bsg_ioc_attr_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfa_ioc_get_attr(&bfad->bfa.ioc, &iocmd->ioc_attr);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	/* fill in driver attr info */
	strcpy(iocmd->ioc_attr.driver_attr.driver, BFAD_DRIVER_NAME);
	strncpy(iocmd->ioc_attr.driver_attr.driver_ver,
		BFAD_DRIVER_VERSION, BFA_VERSION_LEN);
	strcpy(iocmd->ioc_attr.driver_attr.fw_ver,
		iocmd->ioc_attr.adapter_attr.fw_ver);
	strcpy(iocmd->ioc_attr.driver_attr.bios_ver,
		iocmd->ioc_attr.adapter_attr.optrom_ver);

	/* copy chip rev info first otherwise it will be overwritten */
	memcpy(bfad->pci_attr.chip_rev, iocmd->ioc_attr.pci_attr.chip_rev,
		sizeof(bfad->pci_attr.chip_rev));
	memcpy(&iocmd->ioc_attr.pci_attr, &bfad->pci_attr,
		sizeof(struct bfa_ioc_pci_attr_s));

	iocmd->status = BFA_STATUS_OK;
	return 0;
}

static int
bfad_iocmd_port_get_attr(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_port_attr_s *iocmd = (struct bfa_bsg_port_attr_s *)cmd;
	struct bfa_lport_attr_s	port_attr;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfa_fcport_get_attr(&bfad->bfa, &iocmd->attr);
	bfa_fcs_lport_get_attr(&bfad->bfa_fcs.fabric.bport, &port_attr);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (iocmd->attr.topology != BFA_PORT_TOPOLOGY_NONE)
		iocmd->attr.pid = port_attr.pid;
	else
		iocmd->attr.pid = 0;

	iocmd->attr.port_type = port_attr.port_type;
	iocmd->attr.loopback = port_attr.loopback;
	iocmd->attr.authfail = port_attr.authfail;
	strncpy(iocmd->attr.port_symname.symname,
		port_attr.port_cfg.sym_name.symname,
		sizeof(port_attr.port_cfg.sym_name.symname));

	iocmd->status = BFA_STATUS_OK;
	return 0;
}

static int
bfad_iocmd_lport_get_attr(struct bfad_s *bfad, void *cmd)
{
	struct bfa_fcs_lport_s	*fcs_port;
	struct bfa_bsg_lport_attr_s *iocmd = (struct bfa_bsg_lport_attr_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_port = bfa_fcs_lookup_port(&bfad->bfa_fcs,
				iocmd->vf_id, iocmd->pwwn);
	if (fcs_port == NULL) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_LWWN;
		goto out;
	}

	bfa_fcs_lport_get_attr(fcs_port, &iocmd->port_attr);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	iocmd->status = BFA_STATUS_OK;
out:
	return 0;
}

static int
bfad_iocmd_rport_get_addr(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_rport_scsi_addr_s *iocmd =
			(struct bfa_bsg_rport_scsi_addr_s *)cmd;
	struct bfa_fcs_lport_s	*fcs_port;
	struct bfa_fcs_itnim_s	*fcs_itnim;
	struct bfad_itnim_s	*drv_itnim;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_port = bfa_fcs_lookup_port(&bfad->bfa_fcs,
				iocmd->vf_id, iocmd->pwwn);
	if (fcs_port == NULL) {
		bfa_trc(bfad, 0);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_LWWN;
		goto out;
	}

	fcs_itnim = bfa_fcs_itnim_lookup(fcs_port, iocmd->rpwwn);
	if (fcs_itnim == NULL) {
		bfa_trc(bfad, 0);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_RWWN;
		goto out;
	}

	drv_itnim = fcs_itnim->itnim_drv;

	if (drv_itnim && drv_itnim->im_port)
		iocmd->host = drv_itnim->im_port->shost->host_no;
	else {
		bfa_trc(bfad, 0);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_RWWN;
		goto out;
	}

	iocmd->target = drv_itnim->scsi_tgt_id;
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	iocmd->bus = 0;
	iocmd->lun = 0;
	iocmd->status = BFA_STATUS_OK;
out:
	return 0;
}

static int
bfad_iocmd_fabric_get_lports(struct bfad_s *bfad, void *cmd,
			unsigned int payload_len)
{
	struct bfa_bsg_fabric_get_lports_s *iocmd =
			(struct bfa_bsg_fabric_get_lports_s *)cmd;
	bfa_fcs_vf_t	*fcs_vf;
	uint32_t	nports = iocmd->nports;
	unsigned long	flags;
	void	*iocmd_bufptr;

	if (nports == 0) {
		iocmd->status = BFA_STATUS_EINVAL;
		goto out;
	}

	if (bfad_chk_iocmd_sz(payload_len,
		sizeof(struct bfa_bsg_fabric_get_lports_s),
		sizeof(wwn_t[iocmd->nports])) != BFA_STATUS_OK) {
		iocmd->status = BFA_STATUS_VERSION_FAIL;
		goto out;
	}

	iocmd_bufptr = (char *)iocmd +
			sizeof(struct bfa_bsg_fabric_get_lports_s);

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_vf = bfa_fcs_vf_lookup(&bfad->bfa_fcs, iocmd->vf_id);
	if (fcs_vf == NULL) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_VFID;
		goto out;
	}
	bfa_fcs_vf_get_ports(fcs_vf, (wwn_t *)iocmd_bufptr, &nports);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	iocmd->nports = nports;
	iocmd->status = BFA_STATUS_OK;
out:
	return 0;
}

static int
bfad_iocmd_itnim_get_attr(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_itnim_attr_s *iocmd = (struct bfa_bsg_itnim_attr_s *)cmd;
	struct bfa_fcs_lport_s	*fcs_port;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_port = bfa_fcs_lookup_port(&bfad->bfa_fcs,
				iocmd->vf_id, iocmd->lpwwn);
	if (!fcs_port)
		iocmd->status = BFA_STATUS_UNKNOWN_LWWN;
	else
		iocmd->status = bfa_fcs_itnim_attr_get(fcs_port,
					iocmd->rpwwn, &iocmd->attr);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	return 0;
}

int
bfad_iocmd_ioc_get_pcifn_cfg(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_pcifn_cfg_s *iocmd = (struct bfa_bsg_pcifn_cfg_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_ablk_query(&bfad->bfa.modules.ablk,
				&iocmd->pcifn_cfg,
				bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK)
		goto out;

	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_pcifn_create(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_pcifn_s *iocmd = (struct bfa_bsg_pcifn_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_ablk_pf_create(&bfad->bfa.modules.ablk,
				&iocmd->pcifn_id, iocmd->port,
				iocmd->pcifn_class, iocmd->bandwidth,
				bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK)
		goto out;

	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_pcifn_delete(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_pcifn_s *iocmd = (struct bfa_bsg_pcifn_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_ablk_pf_delete(&bfad->bfa.modules.ablk,
				iocmd->pcifn_id,
				bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK)
		goto out;

	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_pcifn_bw(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_pcifn_s *iocmd = (struct bfa_bsg_pcifn_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_ablk_pf_update(&bfad->bfa.modules.ablk,
				iocmd->pcifn_id, iocmd->bandwidth,
				bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	bfa_trc(bfad, iocmd->status);
	if (iocmd->status != BFA_STATUS_OK)
		goto out;

	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
	bfa_trc(bfad, iocmd->status);
out:
	return 0;
}

int
bfad_iocmd_adapter_cfg_mode(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_adapter_cfg_mode_s *iocmd =
			(struct bfa_bsg_adapter_cfg_mode_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long flags = 0;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_ablk_adapter_config(&bfad->bfa.modules.ablk,
				iocmd->cfg.mode, iocmd->cfg.max_pf,
				iocmd->cfg.max_vf, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK)
		goto out;

	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_port_cfg_mode(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_port_cfg_mode_s *iocmd =
			(struct bfa_bsg_port_cfg_mode_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long flags = 0;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_ablk_port_config(&bfad->bfa.modules.ablk,
				iocmd->instance, iocmd->cfg.mode,
				iocmd->cfg.max_pf, iocmd->cfg.max_vf,
				bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK)
		goto out;

	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_ablk_optrom(struct bfad_s *bfad, unsigned int cmd, void *pcmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)pcmd;
	struct bfad_hal_comp fcomp;
	unsigned long   flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	if (cmd == IOCMD_FLASH_ENABLE_OPTROM)
		iocmd->status = bfa_ablk_optrom_en(&bfad->bfa.modules.ablk,
					bfad_hcb_comp, &fcomp);
	else
		iocmd->status = bfa_ablk_optrom_dis(&bfad->bfa.modules.ablk,
					bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (iocmd->status != BFA_STATUS_OK)
		goto out;

	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_faa_enable(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	unsigned long   flags;
	struct bfad_hal_comp    fcomp;

	init_completion(&fcomp.comp);
	iocmd->status = BFA_STATUS_OK;
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_faa_enable(&bfad->bfa, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (iocmd->status != BFA_STATUS_OK)
		goto out;

	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_faa_disable(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	unsigned long   flags;
	struct bfad_hal_comp    fcomp;

	init_completion(&fcomp.comp);
	iocmd->status = BFA_STATUS_OK;
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_faa_disable(&bfad->bfa, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (iocmd->status != BFA_STATUS_OK)
		goto out;

	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_faa_query(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_faa_attr_s *iocmd = (struct bfa_bsg_faa_attr_s *)cmd;
	struct bfad_hal_comp    fcomp;
	unsigned long   flags;

	init_completion(&fcomp.comp);
	iocmd->status = BFA_STATUS_OK;
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_faa_query(&bfad->bfa, &iocmd->faa_attr,
				bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (iocmd->status != BFA_STATUS_OK)
		goto out;

	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

static int
bfad_iocmd_handler(struct bfad_s *bfad, unsigned int cmd, void *iocmd,
		unsigned int payload_len)
{
	int rc = EINVAL;

	switch (cmd) {
	case IOCMD_IOC_GET_INFO:
		rc = bfad_iocmd_ioc_get_info(bfad, iocmd);
		break;
	case IOCMD_IOC_GET_ATTR:
		rc = bfad_iocmd_ioc_get_attr(bfad, iocmd);
		break;
	case IOCMD_PORT_GET_ATTR:
		rc = bfad_iocmd_port_get_attr(bfad, iocmd);
		break;
	case IOCMD_LPORT_GET_ATTR:
		rc = bfad_iocmd_lport_get_attr(bfad, iocmd);
		break;
	case IOCMD_RPORT_GET_ADDR:
		rc = bfad_iocmd_rport_get_addr(bfad, iocmd);
		break;
	case IOCMD_FABRIC_GET_LPORTS:
		rc = bfad_iocmd_fabric_get_lports(bfad, iocmd, payload_len);
		break;
	case IOCMD_ITNIM_GET_ATTR:
		rc = bfad_iocmd_itnim_get_attr(bfad, iocmd);
		break;
	case IOCMD_IOC_PCIFN_CFG:
		rc = bfad_iocmd_ioc_get_pcifn_cfg(bfad, iocmd);
		break;
	case IOCMD_PCIFN_CREATE:
		rc = bfad_iocmd_pcifn_create(bfad, iocmd);
		break;
	case IOCMD_PCIFN_DELETE:
		rc = bfad_iocmd_pcifn_delete(bfad, iocmd);
		break;
	case IOCMD_PCIFN_BW:
		rc = bfad_iocmd_pcifn_bw(bfad, iocmd);
		break;
	case IOCMD_ADAPTER_CFG_MODE:
		rc = bfad_iocmd_adapter_cfg_mode(bfad, iocmd);
		break;
	case IOCMD_PORT_CFG_MODE:
		rc = bfad_iocmd_port_cfg_mode(bfad, iocmd);
		break;
	case IOCMD_FLASH_ENABLE_OPTROM:
	case IOCMD_FLASH_DISABLE_OPTROM:
		rc = bfad_iocmd_ablk_optrom(bfad, cmd, iocmd);
		break;
	case IOCMD_FAA_ENABLE:
		rc = bfad_iocmd_faa_enable(bfad, iocmd);
		break;
	case IOCMD_FAA_DISABLE:
		rc = bfad_iocmd_faa_disable(bfad, iocmd);
		break;
	case IOCMD_FAA_QUERY:
		rc = bfad_iocmd_faa_query(bfad, iocmd);
		break;
	default:
		rc = EINVAL;
		break;
	}
	return -rc;
}

static int
bfad_im_bsg_vendor_request(struct fc_bsg_job *job)
{
	uint32_t vendor_cmd = job->request->rqst_data.h_vendor.vendor_cmd[0];
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) job->shost->hostdata[0];
	struct bfad_s *bfad = im_port->bfad;
	void *payload_kbuf;
	int rc = -EINVAL;

	/* Allocate a temp buffer to hold the passed in user space command */
	payload_kbuf = kzalloc(job->request_payload.payload_len, GFP_KERNEL);
	if (!payload_kbuf) {
		rc = -ENOMEM;
		goto out;
	}

	/* Copy the sg_list passed in to a linear buffer: holds the cmnd data */
	sg_copy_to_buffer(job->request_payload.sg_list,
			  job->request_payload.sg_cnt, payload_kbuf,
			  job->request_payload.payload_len);

	/* Invoke IOCMD handler - to handle all the vendor command requests */
	rc = bfad_iocmd_handler(bfad, vendor_cmd, payload_kbuf,
				job->request_payload.payload_len);
	if (rc != BFA_STATUS_OK)
		goto error;

	/* Copy the response data to the job->reply_payload sg_list */
	sg_copy_from_buffer(job->reply_payload.sg_list,
			    job->reply_payload.sg_cnt,
			    payload_kbuf,
			    job->reply_payload.payload_len);

	/* free the command buffer */
	kfree(payload_kbuf);

	/* Fill the BSG job reply data */
	job->reply_len = job->reply_payload.payload_len;
	job->reply->reply_payload_rcv_len = job->reply_payload.payload_len;
	job->reply->result = rc;

	job->job_done(job);
	return rc;
error:
	/* free the command buffer */
	kfree(payload_kbuf);
out:
	job->reply->result = rc;
	job->reply_len = sizeof(uint32_t);
	job->reply->reply_payload_rcv_len = 0;
	return rc;
}

/* FC passthru call backs */
u64
bfad_fcxp_get_req_sgaddr_cb(void *bfad_fcxp, int sgeid)
{
	struct bfad_fcxp	*drv_fcxp = bfad_fcxp;
	struct bfa_sge_s  *sge;
	u64	addr;

	sge = drv_fcxp->req_sge + sgeid;
	addr = (u64)(size_t) sge->sg_addr;
	return addr;
}

u32
bfad_fcxp_get_req_sglen_cb(void *bfad_fcxp, int sgeid)
{
	struct bfad_fcxp	*drv_fcxp = bfad_fcxp;
	struct bfa_sge_s	*sge;

	sge = drv_fcxp->req_sge + sgeid;
	return sge->sg_len;
}

u64
bfad_fcxp_get_rsp_sgaddr_cb(void *bfad_fcxp, int sgeid)
{
	struct bfad_fcxp	*drv_fcxp = bfad_fcxp;
	struct bfa_sge_s	*sge;
	u64	addr;

	sge = drv_fcxp->rsp_sge + sgeid;
	addr = (u64)(size_t) sge->sg_addr;
	return addr;
}

u32
bfad_fcxp_get_rsp_sglen_cb(void *bfad_fcxp, int sgeid)
{
	struct bfad_fcxp	*drv_fcxp = bfad_fcxp;
	struct bfa_sge_s	*sge;

	sge = drv_fcxp->rsp_sge + sgeid;
	return sge->sg_len;
}

void
bfad_send_fcpt_cb(void *bfad_fcxp, struct bfa_fcxp_s *fcxp, void *cbarg,
		bfa_status_t req_status, u32 rsp_len, u32 resid_len,
		struct fchs_s *rsp_fchs)
{
	struct bfad_fcxp *drv_fcxp = bfad_fcxp;

	drv_fcxp->req_status = req_status;
	drv_fcxp->rsp_len = rsp_len;

	/* bfa_fcxp will be automatically freed by BFA */
	drv_fcxp->bfa_fcxp = NULL;
	complete(&drv_fcxp->comp);
}

struct bfad_buf_info *
bfad_fcxp_map_sg(struct bfad_s *bfad, void *payload_kbuf,
		 uint32_t payload_len, uint32_t *num_sgles)
{
	struct bfad_buf_info	*buf_base, *buf_info;
	struct bfa_sge_s	*sg_table;
	int sge_num = 1;

	buf_base = kzalloc((sizeof(struct bfad_buf_info) +
			   sizeof(struct bfa_sge_s)) * sge_num, GFP_KERNEL);
	if (!buf_base)
		return NULL;

	sg_table = (struct bfa_sge_s *) (((uint8_t *)buf_base) +
			(sizeof(struct bfad_buf_info) * sge_num));

	/* Allocate dma coherent memory */
	buf_info = buf_base;
	buf_info->size = payload_len;
	buf_info->virt = dma_alloc_coherent(&bfad->pcidev->dev, buf_info->size,
					&buf_info->phys, GFP_KERNEL);
	if (!buf_info->virt)
		goto out_free_mem;

	/* copy the linear bsg buffer to buf_info */
	memset(buf_info->virt, 0, buf_info->size);
	memcpy(buf_info->virt, payload_kbuf, buf_info->size);

	/*
	 * Setup SG table
	 */
	sg_table->sg_len = buf_info->size;
	sg_table->sg_addr = (void *)(size_t) buf_info->phys;

	*num_sgles = sge_num;

	return buf_base;

out_free_mem:
	kfree(buf_base);
	return NULL;
}

void
bfad_fcxp_free_mem(struct bfad_s *bfad, struct bfad_buf_info *buf_base,
		   uint32_t num_sgles)
{
	int i;
	struct bfad_buf_info *buf_info = buf_base;

	if (buf_base) {
		for (i = 0; i < num_sgles; buf_info++, i++) {
			if (buf_info->virt != NULL)
				dma_free_coherent(&bfad->pcidev->dev,
					buf_info->size, buf_info->virt,
					buf_info->phys);
		}
		kfree(buf_base);
	}
}

int
bfad_fcxp_bsg_send(struct fc_bsg_job *job, struct bfad_fcxp *drv_fcxp,
		   bfa_bsg_fcpt_t *bsg_fcpt)
{
	struct bfa_fcxp_s *hal_fcxp;
	struct bfad_s	*bfad = drv_fcxp->port->bfad;
	unsigned long	flags;
	uint8_t	lp_tag;

	spin_lock_irqsave(&bfad->bfad_lock, flags);

	/* Allocate bfa_fcxp structure */
	hal_fcxp = bfa_fcxp_alloc(drv_fcxp, &bfad->bfa,
				  drv_fcxp->num_req_sgles,
				  drv_fcxp->num_rsp_sgles,
				  bfad_fcxp_get_req_sgaddr_cb,
				  bfad_fcxp_get_req_sglen_cb,
				  bfad_fcxp_get_rsp_sgaddr_cb,
				  bfad_fcxp_get_rsp_sglen_cb);
	if (!hal_fcxp) {
		bfa_trc(bfad, 0);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		return BFA_STATUS_ENOMEM;
	}

	drv_fcxp->bfa_fcxp = hal_fcxp;

	lp_tag = bfa_lps_get_tag_from_pid(&bfad->bfa, bsg_fcpt->fchs.s_id);

	bfa_fcxp_send(hal_fcxp, drv_fcxp->bfa_rport, bsg_fcpt->vf_id, lp_tag,
		      bsg_fcpt->cts, bsg_fcpt->cos,
		      job->request_payload.payload_len,
		      &bsg_fcpt->fchs, bfad_send_fcpt_cb, bfad,
		      job->reply_payload.payload_len, bsg_fcpt->tsecs);

	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return BFA_STATUS_OK;
}

int
bfad_im_bsg_els_ct_request(struct fc_bsg_job *job)
{
	struct bfa_bsg_data *bsg_data;
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) job->shost->hostdata[0];
	struct bfad_s *bfad = im_port->bfad;
	bfa_bsg_fcpt_t *bsg_fcpt;
	struct bfad_fcxp    *drv_fcxp;
	struct bfa_fcs_lport_s *fcs_port;
	struct bfa_fcs_rport_s *fcs_rport;
	uint32_t command_type = job->request->msgcode;
	unsigned long flags;
	struct bfad_buf_info *rsp_buf_info;
	void *req_kbuf = NULL, *rsp_kbuf = NULL;
	int rc = -EINVAL;

	job->reply_len  = sizeof(uint32_t);	/* Atleast uint32_t reply_len */
	job->reply->reply_payload_rcv_len = 0;

	/* Get the payload passed in from userspace */
	bsg_data = (struct bfa_bsg_data *) (((char *)job->request) +
					sizeof(struct fc_bsg_request));
	if (bsg_data == NULL)
		goto out;

	/*
	 * Allocate buffer for bsg_fcpt and do a copy_from_user op for payload
	 * buffer of size bsg_data->payload_len
	 */
	bsg_fcpt = (struct bfa_bsg_fcpt_s *)
		   kzalloc(bsg_data->payload_len, GFP_KERNEL);
	if (!bsg_fcpt)
		goto out;

	if (copy_from_user((uint8_t *)bsg_fcpt, bsg_data->payload,
				bsg_data->payload_len)) {
		kfree(bsg_fcpt);
		goto out;
	}

	drv_fcxp = kzalloc(sizeof(struct bfad_fcxp), GFP_KERNEL);
	if (drv_fcxp == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_port = bfa_fcs_lookup_port(&bfad->bfa_fcs, bsg_fcpt->vf_id,
					bsg_fcpt->lpwwn);
	if (fcs_port == NULL) {
		bsg_fcpt->status = BFA_STATUS_UNKNOWN_LWWN;
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		goto out_free_mem;
	}

	/* Check if the port is online before sending FC Passthru cmd */
	if (!bfa_fcs_lport_is_online(fcs_port)) {
		bsg_fcpt->status = BFA_STATUS_PORT_OFFLINE;
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		goto out_free_mem;
	}

	drv_fcxp->port = fcs_port->bfad_port;

	if (drv_fcxp->port->bfad == 0)
		drv_fcxp->port->bfad = bfad;

	/* Fetch the bfa_rport - if nexus needed */
	if (command_type == FC_BSG_HST_ELS_NOLOGIN ||
	    command_type == FC_BSG_HST_CT) {
		/* BSG HST commands: no nexus needed */
		drv_fcxp->bfa_rport = NULL;

	} else if (command_type == FC_BSG_RPT_ELS ||
		   command_type == FC_BSG_RPT_CT) {
		/* BSG RPT commands: nexus needed */
		fcs_rport = bfa_fcs_lport_get_rport_by_pwwn(fcs_port,
							    bsg_fcpt->dpwwn);
		if (fcs_rport == NULL) {
			bsg_fcpt->status = BFA_STATUS_UNKNOWN_RWWN;
			spin_unlock_irqrestore(&bfad->bfad_lock, flags);
			goto out_free_mem;
		}

		drv_fcxp->bfa_rport = fcs_rport->bfa_rport;

	} else { /* Unknown BSG msgcode; return -EINVAL */
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		goto out_free_mem;
	}

	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	/* allocate memory for req / rsp buffers */
	req_kbuf = kzalloc(job->request_payload.payload_len, GFP_KERNEL);
	if (!req_kbuf) {
		printk(KERN_INFO "bfa %s: fcpt request buffer alloc failed\n",
				bfad->pci_name);
		rc = -ENOMEM;
		goto out_free_mem;
	}

	rsp_kbuf = kzalloc(job->reply_payload.payload_len, GFP_KERNEL);
	if (!rsp_kbuf) {
		printk(KERN_INFO "bfa %s: fcpt response buffer alloc failed\n",
				bfad->pci_name);
		rc = -ENOMEM;
		goto out_free_mem;
	}

	/* map req sg - copy the sg_list passed in to the linear buffer */
	sg_copy_to_buffer(job->request_payload.sg_list,
			  job->request_payload.sg_cnt, req_kbuf,
			  job->request_payload.payload_len);

	drv_fcxp->reqbuf_info = bfad_fcxp_map_sg(bfad, req_kbuf,
					job->request_payload.payload_len,
					&drv_fcxp->num_req_sgles);
	if (!drv_fcxp->reqbuf_info) {
		printk(KERN_INFO "bfa %s: fcpt request fcxp_map_sg failed\n",
				bfad->pci_name);
		rc = -ENOMEM;
		goto out_free_mem;
	}

	drv_fcxp->req_sge = (struct bfa_sge_s *)
			    (((uint8_t *)drv_fcxp->reqbuf_info) +
			    (sizeof(struct bfad_buf_info) *
					drv_fcxp->num_req_sgles));

	/* map rsp sg */
	drv_fcxp->rspbuf_info = bfad_fcxp_map_sg(bfad, rsp_kbuf,
					job->reply_payload.payload_len,
					&drv_fcxp->num_rsp_sgles);
	if (!drv_fcxp->rspbuf_info) {
		printk(KERN_INFO "bfa %s: fcpt response fcxp_map_sg failed\n",
				bfad->pci_name);
		rc = -ENOMEM;
		goto out_free_mem;
	}

	rsp_buf_info = (struct bfad_buf_info *)drv_fcxp->rspbuf_info;
	drv_fcxp->rsp_sge = (struct bfa_sge_s  *)
			    (((uint8_t *)drv_fcxp->rspbuf_info) +
			    (sizeof(struct bfad_buf_info) *
					drv_fcxp->num_rsp_sgles));

	/* fcxp send */
	init_completion(&drv_fcxp->comp);
	rc = bfad_fcxp_bsg_send(job, drv_fcxp, bsg_fcpt);
	if (rc == BFA_STATUS_OK) {
		wait_for_completion(&drv_fcxp->comp);
		bsg_fcpt->status = drv_fcxp->req_status;
	} else {
		bsg_fcpt->status = rc;
		goto out_free_mem;
	}

	/* fill the job->reply data */
	if (drv_fcxp->req_status == BFA_STATUS_OK) {
		job->reply_len = drv_fcxp->rsp_len;
		job->reply->reply_payload_rcv_len = drv_fcxp->rsp_len;
		job->reply->reply_data.ctels_reply.status = FC_CTELS_STATUS_OK;
	} else {
		job->reply->reply_payload_rcv_len =
					sizeof(struct fc_bsg_ctels_reply);
		job->reply_len = sizeof(uint32_t);
		job->reply->reply_data.ctels_reply.status =
						FC_CTELS_STATUS_REJECT;
	}

	/* Copy the response data to the reply_payload sg list */
	sg_copy_from_buffer(job->reply_payload.sg_list,
			    job->reply_payload.sg_cnt,
			    (uint8_t *)rsp_buf_info->virt,
			    job->reply_payload.payload_len);

out_free_mem:
	bfad_fcxp_free_mem(bfad, drv_fcxp->rspbuf_info,
			   drv_fcxp->num_rsp_sgles);
	bfad_fcxp_free_mem(bfad, drv_fcxp->reqbuf_info,
			   drv_fcxp->num_req_sgles);
	kfree(req_kbuf);
	kfree(rsp_kbuf);

	/* Need a copy to user op */
	if (copy_to_user(bsg_data->payload, (void *) bsg_fcpt,
			 bsg_data->payload_len))
		rc = -EIO;

	kfree(bsg_fcpt);
	kfree(drv_fcxp);
out:
	job->reply->result = rc;

	if (rc == BFA_STATUS_OK)
		job->job_done(job);

	return rc;
}

int
bfad_im_bsg_request(struct fc_bsg_job *job)
{
	uint32_t rc = BFA_STATUS_OK;

	/* Increment the bfa module refcnt - if bsg request is in service */
	bfad_im_bsg_get_kobject(job);

	switch (job->request->msgcode) {
	case FC_BSG_HST_VENDOR:
		/* Process BSG HST Vendor requests */
		rc = bfad_im_bsg_vendor_request(job);
		break;
	case FC_BSG_HST_ELS_NOLOGIN:
	case FC_BSG_RPT_ELS:
	case FC_BSG_HST_CT:
	case FC_BSG_RPT_CT:
		/* Process BSG ELS/CT commands */
		rc = bfad_im_bsg_els_ct_request(job);
		break;
	default:
		job->reply->result = rc = -EINVAL;
		job->reply->reply_payload_rcv_len = 0;
		break;
	}

	/* Decrement the bfa module refcnt - on completion of bsg request */
	bfad_im_bsg_put_kobject(job);

	return rc;
}

int
bfad_im_bsg_timeout(struct fc_bsg_job *job)
{
	/* Don't complete the BSG job request - return -EAGAIN
	 * to reset bsg job timeout : for ELS/CT pass thru we
	 * already have timer to track the request.
	 */
	return -EAGAIN;
}
