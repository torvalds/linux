/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014- QLogic Corporation.
 * All rights reserved
 * www.qlogic.com
 *
 * Linux driver for QLogic BR-series Fibre Channel Host Bus Adapter.
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

int
bfad_iocmd_ioc_enable(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	/* If IOC is not in disabled state - return */
	if (!bfa_ioc_is_disabled(&bfad->bfa.ioc)) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_OK;
		return 0;
	}

	init_completion(&bfad->enable_comp);
	bfa_iocfc_enable(&bfad->bfa);
	iocmd->status = BFA_STATUS_OK;
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	wait_for_completion(&bfad->enable_comp);

	return 0;
}

int
bfad_iocmd_ioc_disable(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	if (bfa_ioc_is_disabled(&bfad->bfa.ioc)) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_OK;
		return 0;
	}

	if (bfad->disable_active) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		return -EBUSY;
	}

	bfad->disable_active = BFA_TRUE;
	init_completion(&bfad->disable_comp);
	bfa_iocfc_disable(&bfad->bfa);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	wait_for_completion(&bfad->disable_comp);
	bfad->disable_active = BFA_FALSE;
	iocmd->status = BFA_STATUS_OK;

	return 0;
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
	iocmd->bfad_num = bfad->inst_no;
	im_port = bfad->pport.im_port;
	iocmd->host = im_port->shost->host_no;
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	strcpy(iocmd->name, bfad->adapter_name);
	strcpy(iocmd->port_name, bfad->port_name);
	strcpy(iocmd->hwpath, bfad->pci_name);

	/* set adapter hw path */
	strcpy(iocmd->adapter_hwpath, bfad->pci_name);
	for (i = 0; iocmd->adapter_hwpath[i] != ':' && i < BFA_STRING_32; i++)
		;
	for (; iocmd->adapter_hwpath[++i] != ':' && i < BFA_STRING_32; )
		;
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

int
bfad_iocmd_ioc_get_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_ioc_stats_s *iocmd = (struct bfa_bsg_ioc_stats_s *)cmd;

	bfa_ioc_get_stats(&bfad->bfa, &iocmd->ioc_stats);
	iocmd->status = BFA_STATUS_OK;
	return 0;
}

int
bfad_iocmd_ioc_get_fwstats(struct bfad_s *bfad, void *cmd,
			unsigned int payload_len)
{
	struct bfa_bsg_ioc_fwstats_s *iocmd =
			(struct bfa_bsg_ioc_fwstats_s *)cmd;
	void	*iocmd_bufptr;
	unsigned long	flags;

	if (bfad_chk_iocmd_sz(payload_len,
			sizeof(struct bfa_bsg_ioc_fwstats_s),
			sizeof(struct bfa_fw_stats_s)) != BFA_STATUS_OK) {
		iocmd->status = BFA_STATUS_VERSION_FAIL;
		goto out;
	}

	iocmd_bufptr = (char *)iocmd + sizeof(struct bfa_bsg_ioc_fwstats_s);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_ioc_fw_stats_get(&bfad->bfa.ioc, iocmd_bufptr);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (iocmd->status != BFA_STATUS_OK) {
		bfa_trc(bfad, iocmd->status);
		goto out;
	}
out:
	bfa_trc(bfad, 0x6666);
	return 0;
}

int
bfad_iocmd_ioc_reset_stats(struct bfad_s *bfad, void *cmd, unsigned int v_cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	unsigned long	flags;

	if (v_cmd == IOCMD_IOC_RESET_STATS) {
		bfa_ioc_clear_stats(&bfad->bfa);
		iocmd->status = BFA_STATUS_OK;
	} else if (v_cmd == IOCMD_IOC_RESET_FWSTATS) {
		spin_lock_irqsave(&bfad->bfad_lock, flags);
		iocmd->status = bfa_ioc_fw_stats_clear(&bfad->bfa.ioc);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	}

	return 0;
}

int
bfad_iocmd_ioc_set_name(struct bfad_s *bfad, void *cmd, unsigned int v_cmd)
{
	struct bfa_bsg_ioc_name_s *iocmd = (struct bfa_bsg_ioc_name_s *) cmd;

	if (v_cmd == IOCMD_IOC_SET_ADAPTER_NAME)
		strcpy(bfad->adapter_name, iocmd->name);
	else if (v_cmd == IOCMD_IOC_SET_PORT_NAME)
		strcpy(bfad->port_name, iocmd->name);

	iocmd->status = BFA_STATUS_OK;
	return 0;
}

int
bfad_iocmd_iocfc_get_attr(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_iocfc_attr_s *iocmd = (struct bfa_bsg_iocfc_attr_s *)cmd;

	iocmd->status = BFA_STATUS_OK;
	bfa_iocfc_get_attr(&bfad->bfa, &iocmd->iocfc_attr);

	return 0;
}

int
bfad_iocmd_ioc_fw_sig_inv(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	unsigned long flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_ioc_fwsig_invalidate(&bfad->bfa.ioc);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	return 0;
}

int
bfad_iocmd_iocfc_set_intr(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_iocfc_intr_s *iocmd = (struct bfa_bsg_iocfc_intr_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_iocfc_israttr_set(&bfad->bfa, &iocmd->attr);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

int
bfad_iocmd_port_enable(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_port_enable(&bfad->bfa.modules.port,
					bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK) {
		bfa_trc(bfad, iocmd->status);
		return 0;
	}
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
	return 0;
}

int
bfad_iocmd_port_disable(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_port_disable(&bfad->bfa.modules.port,
				bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (iocmd->status != BFA_STATUS_OK) {
		bfa_trc(bfad, iocmd->status);
		return 0;
	}
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
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

int
bfad_iocmd_port_get_stats(struct bfad_s *bfad, void *cmd,
			unsigned int payload_len)
{
	struct bfa_bsg_port_stats_s *iocmd = (struct bfa_bsg_port_stats_s *)cmd;
	struct bfad_hal_comp fcomp;
	void	*iocmd_bufptr;
	unsigned long	flags;

	if (bfad_chk_iocmd_sz(payload_len,
			sizeof(struct bfa_bsg_port_stats_s),
			sizeof(union bfa_port_stats_u)) != BFA_STATUS_OK) {
		iocmd->status = BFA_STATUS_VERSION_FAIL;
		return 0;
	}

	iocmd_bufptr = (char *)iocmd + sizeof(struct bfa_bsg_port_stats_s);

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_port_get_stats(&bfad->bfa.modules.port,
				iocmd_bufptr, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK) {
		bfa_trc(bfad, iocmd->status);
		goto out;
	}

	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_port_reset_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long	flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_port_clear_stats(&bfad->bfa.modules.port,
					bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK) {
		bfa_trc(bfad, iocmd->status);
		return 0;
	}
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
	return 0;
}

int
bfad_iocmd_set_port_cfg(struct bfad_s *bfad, void *iocmd, unsigned int v_cmd)
{
	struct bfa_bsg_port_cfg_s *cmd = (struct bfa_bsg_port_cfg_s *)iocmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	if (v_cmd == IOCMD_PORT_CFG_TOPO)
		cmd->status = bfa_fcport_cfg_topology(&bfad->bfa, cmd->param);
	else if (v_cmd == IOCMD_PORT_CFG_SPEED)
		cmd->status = bfa_fcport_cfg_speed(&bfad->bfa, cmd->param);
	else if (v_cmd == IOCMD_PORT_CFG_ALPA)
		cmd->status = bfa_fcport_cfg_hardalpa(&bfad->bfa, cmd->param);
	else if (v_cmd == IOCMD_PORT_CLR_ALPA)
		cmd->status = bfa_fcport_clr_hardalpa(&bfad->bfa);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

int
bfad_iocmd_port_cfg_maxfrsize(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_port_cfg_maxfrsize_s *iocmd =
				(struct bfa_bsg_port_cfg_maxfrsize_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_fcport_cfg_maxfrsize(&bfad->bfa, iocmd->maxfrsize);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

int
bfad_iocmd_port_cfg_bbcr(struct bfad_s *bfad, unsigned int cmd, void *pcmd)
{
	struct bfa_bsg_bbcr_enable_s *iocmd =
			(struct bfa_bsg_bbcr_enable_s *)pcmd;
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	if (cmd == IOCMD_PORT_BBCR_ENABLE)
		rc = bfa_fcport_cfg_bbcr(&bfad->bfa, BFA_TRUE, iocmd->bb_scn);
	else if (cmd == IOCMD_PORT_BBCR_DISABLE)
		rc = bfa_fcport_cfg_bbcr(&bfad->bfa, BFA_FALSE, 0);
	else {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	iocmd->status = rc;
	return 0;
}

int
bfad_iocmd_port_get_bbcr_attr(struct bfad_s *bfad, void *pcmd)
{
	struct bfa_bsg_bbcr_attr_s *iocmd = (struct bfa_bsg_bbcr_attr_s *) pcmd;
	unsigned long flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status =
		bfa_fcport_get_bbcr_attr(&bfad->bfa, &iocmd->attr);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

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

int
bfad_iocmd_lport_get_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_fcs_lport_s *fcs_port;
	struct bfa_bsg_lport_stats_s *iocmd =
			(struct bfa_bsg_lport_stats_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_port = bfa_fcs_lookup_port(&bfad->bfa_fcs,
				iocmd->vf_id, iocmd->pwwn);
	if (fcs_port == NULL) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_LWWN;
		goto out;
	}

	bfa_fcs_lport_get_stats(fcs_port, &iocmd->port_stats);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	iocmd->status = BFA_STATUS_OK;
out:
	return 0;
}

int
bfad_iocmd_lport_reset_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_fcs_lport_s *fcs_port;
	struct bfa_bsg_reset_stats_s *iocmd =
			(struct bfa_bsg_reset_stats_s *)cmd;
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(&bfad->bfa);
	struct list_head *qe, *qen;
	struct bfa_itnim_s *itnim;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_port = bfa_fcs_lookup_port(&bfad->bfa_fcs,
				iocmd->vf_id, iocmd->vpwwn);
	if (fcs_port == NULL) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_LWWN;
		goto out;
	}

	bfa_fcs_lport_clear_stats(fcs_port);
	/* clear IO stats from all active itnims */
	list_for_each_safe(qe, qen, &fcpim->itnim_q) {
		itnim = (struct bfa_itnim_s *) qe;
		if (itnim->rport->rport_info.lp_tag != fcs_port->lp_tag)
			continue;
		bfa_itnim_clear_stats(itnim);
	}
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	iocmd->status = BFA_STATUS_OK;
out:
	return 0;
}

int
bfad_iocmd_lport_get_iostats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_fcs_lport_s *fcs_port;
	struct bfa_bsg_lport_iostats_s *iocmd =
			(struct bfa_bsg_lport_iostats_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_port = bfa_fcs_lookup_port(&bfad->bfa_fcs,
				iocmd->vf_id, iocmd->pwwn);
	if (fcs_port == NULL) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_LWWN;
		goto out;
	}

	bfa_fcpim_port_iostats(&bfad->bfa, &iocmd->iostats,
			fcs_port->lp_tag);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	iocmd->status = BFA_STATUS_OK;
out:
	return 0;
}

int
bfad_iocmd_lport_get_rports(struct bfad_s *bfad, void *cmd,
			unsigned int payload_len)
{
	struct bfa_bsg_lport_get_rports_s *iocmd =
			(struct bfa_bsg_lport_get_rports_s *)cmd;
	struct bfa_fcs_lport_s *fcs_port;
	unsigned long	flags;
	void	*iocmd_bufptr;

	if (iocmd->nrports == 0)
		return -EINVAL;

	if (bfad_chk_iocmd_sz(payload_len,
			sizeof(struct bfa_bsg_lport_get_rports_s),
			sizeof(struct bfa_rport_qualifier_s) * iocmd->nrports)
			!= BFA_STATUS_OK) {
		iocmd->status = BFA_STATUS_VERSION_FAIL;
		return 0;
	}

	iocmd_bufptr = (char *)iocmd +
			sizeof(struct bfa_bsg_lport_get_rports_s);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_port = bfa_fcs_lookup_port(&bfad->bfa_fcs,
				iocmd->vf_id, iocmd->pwwn);
	if (fcs_port == NULL) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		bfa_trc(bfad, 0);
		iocmd->status = BFA_STATUS_UNKNOWN_LWWN;
		goto out;
	}

	bfa_fcs_lport_get_rport_quals(fcs_port,
			(struct bfa_rport_qualifier_s *)iocmd_bufptr,
			&iocmd->nrports);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	iocmd->status = BFA_STATUS_OK;
out:
	return 0;
}

int
bfad_iocmd_rport_get_attr(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_rport_attr_s *iocmd = (struct bfa_bsg_rport_attr_s *)cmd;
	struct bfa_fcs_lport_s *fcs_port;
	struct bfa_fcs_rport_s *fcs_rport;
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

	if (iocmd->pid)
		fcs_rport = bfa_fcs_lport_get_rport_by_qualifier(fcs_port,
						iocmd->rpwwn, iocmd->pid);
	else
		fcs_rport = bfa_fcs_rport_lookup(fcs_port, iocmd->rpwwn);
	if (fcs_rport == NULL) {
		bfa_trc(bfad, 0);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_RWWN;
		goto out;
	}

	bfa_fcs_rport_get_attr(fcs_rport, &iocmd->attr);
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

int
bfad_iocmd_rport_get_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_rport_stats_s *iocmd =
			(struct bfa_bsg_rport_stats_s *)cmd;
	struct bfa_fcs_lport_s *fcs_port;
	struct bfa_fcs_rport_s *fcs_rport;
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

	fcs_rport = bfa_fcs_rport_lookup(fcs_port, iocmd->rpwwn);
	if (fcs_rport == NULL) {
		bfa_trc(bfad, 0);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_RWWN;
		goto out;
	}

	memcpy((void *)&iocmd->stats, (void *)&fcs_rport->stats,
		sizeof(struct bfa_rport_stats_s));
	if (bfa_fcs_rport_get_halrport(fcs_rport)) {
		memcpy((void *)&iocmd->stats.hal_stats,
		       (void *)&(bfa_fcs_rport_get_halrport(fcs_rport)->stats),
			sizeof(struct bfa_rport_hal_stats_s));
	}

	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	iocmd->status = BFA_STATUS_OK;
out:
	return 0;
}

int
bfad_iocmd_rport_clr_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_rport_reset_stats_s *iocmd =
				(struct bfa_bsg_rport_reset_stats_s *)cmd;
	struct bfa_fcs_lport_s *fcs_port;
	struct bfa_fcs_rport_s *fcs_rport;
	struct bfa_rport_s *rport;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_port = bfa_fcs_lookup_port(&bfad->bfa_fcs,
				iocmd->vf_id, iocmd->pwwn);
	if (fcs_port == NULL) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_LWWN;
		goto out;
	}

	fcs_rport = bfa_fcs_rport_lookup(fcs_port, iocmd->rpwwn);
	if (fcs_rport == NULL) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_RWWN;
		goto out;
	}

	memset((char *)&fcs_rport->stats, 0, sizeof(struct bfa_rport_stats_s));
	rport = bfa_fcs_rport_get_halrport(fcs_rport);
	if (rport)
		memset(&rport->stats, 0, sizeof(rport->stats));
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	iocmd->status = BFA_STATUS_OK;
out:
	return 0;
}

int
bfad_iocmd_rport_set_speed(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_rport_set_speed_s *iocmd =
				(struct bfa_bsg_rport_set_speed_s *)cmd;
	struct bfa_fcs_lport_s *fcs_port;
	struct bfa_fcs_rport_s *fcs_rport;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_port = bfa_fcs_lookup_port(&bfad->bfa_fcs,
				iocmd->vf_id, iocmd->pwwn);
	if (fcs_port == NULL) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_LWWN;
		goto out;
	}

	fcs_rport = bfa_fcs_rport_lookup(fcs_port, iocmd->rpwwn);
	if (fcs_rport == NULL) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_RWWN;
		goto out;
	}

	fcs_rport->rpf.assigned_speed  = iocmd->speed;
	/* Set this speed in f/w only if the RPSC speed is not available */
	if (fcs_rport->rpf.rpsc_speed == BFA_PORT_SPEED_UNKNOWN)
		if (fcs_rport->bfa_rport)
			bfa_rport_speed(fcs_rport->bfa_rport, iocmd->speed);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	iocmd->status = BFA_STATUS_OK;
out:
	return 0;
}

int
bfad_iocmd_vport_get_attr(struct bfad_s *bfad, void *cmd)
{
	struct bfa_fcs_vport_s *fcs_vport;
	struct bfa_bsg_vport_attr_s *iocmd = (struct bfa_bsg_vport_attr_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_vport = bfa_fcs_vport_lookup(&bfad->bfa_fcs,
				iocmd->vf_id, iocmd->vpwwn);
	if (fcs_vport == NULL) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_VWWN;
		goto out;
	}

	bfa_fcs_vport_get_attr(fcs_vport, &iocmd->vport_attr);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	iocmd->status = BFA_STATUS_OK;
out:
	return 0;
}

int
bfad_iocmd_vport_get_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_fcs_vport_s *fcs_vport;
	struct bfa_bsg_vport_stats_s *iocmd =
				(struct bfa_bsg_vport_stats_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_vport = bfa_fcs_vport_lookup(&bfad->bfa_fcs,
				iocmd->vf_id, iocmd->vpwwn);
	if (fcs_vport == NULL) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_VWWN;
		goto out;
	}

	memcpy((void *)&iocmd->vport_stats, (void *)&fcs_vport->vport_stats,
		sizeof(struct bfa_vport_stats_s));
	memcpy((void *)&iocmd->vport_stats.port_stats,
	       (void *)&fcs_vport->lport.stats,
		sizeof(struct bfa_lport_stats_s));
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	iocmd->status = BFA_STATUS_OK;
out:
	return 0;
}

int
bfad_iocmd_vport_clr_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_fcs_vport_s *fcs_vport;
	struct bfa_bsg_reset_stats_s *iocmd =
				(struct bfa_bsg_reset_stats_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_vport = bfa_fcs_vport_lookup(&bfad->bfa_fcs,
				iocmd->vf_id, iocmd->vpwwn);
	if (fcs_vport == NULL) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_VWWN;
		goto out;
	}

	memset(&fcs_vport->vport_stats, 0, sizeof(struct bfa_vport_stats_s));
	memset(&fcs_vport->lport.stats, 0, sizeof(struct bfa_lport_stats_s));
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
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

int
bfad_iocmd_qos_set_bw(struct bfad_s *bfad, void *pcmd)
{
	struct bfa_bsg_qos_bw_s *iocmd = (struct bfa_bsg_qos_bw_s *)pcmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_fcport_set_qos_bw(&bfad->bfa, &iocmd->qos_bw);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

int
bfad_iocmd_ratelim(struct bfad_s *bfad, unsigned int cmd, void *pcmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)pcmd;
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(&bfad->bfa);
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);

	if ((fcport->cfg.topology == BFA_PORT_TOPOLOGY_LOOP) &&
		(fcport->topology == BFA_PORT_TOPOLOGY_LOOP))
		iocmd->status = BFA_STATUS_TOPOLOGY_LOOP;
	else {
		if (cmd == IOCMD_RATELIM_ENABLE)
			fcport->cfg.ratelimit = BFA_TRUE;
		else if (cmd == IOCMD_RATELIM_DISABLE)
			fcport->cfg.ratelimit = BFA_FALSE;

		if (fcport->cfg.trl_def_speed == BFA_PORT_SPEED_UNKNOWN)
			fcport->cfg.trl_def_speed = BFA_PORT_SPEED_1GBPS;

		iocmd->status = BFA_STATUS_OK;
	}

	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

int
bfad_iocmd_ratelim_speed(struct bfad_s *bfad, unsigned int cmd, void *pcmd)
{
	struct bfa_bsg_trl_speed_s *iocmd = (struct bfa_bsg_trl_speed_s *)pcmd;
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(&bfad->bfa);
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);

	/* Auto and speeds greater than the supported speed, are invalid */
	if ((iocmd->speed == BFA_PORT_SPEED_AUTO) ||
	    (iocmd->speed > fcport->speed_sup)) {
		iocmd->status = BFA_STATUS_UNSUPP_SPEED;
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		return 0;
	}

	if ((fcport->cfg.topology == BFA_PORT_TOPOLOGY_LOOP) &&
		(fcport->topology == BFA_PORT_TOPOLOGY_LOOP))
		iocmd->status = BFA_STATUS_TOPOLOGY_LOOP;
	else {
		fcport->cfg.trl_def_speed = iocmd->speed;
		iocmd->status = BFA_STATUS_OK;
	}
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

int
bfad_iocmd_cfg_fcpim(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_fcpim_s *iocmd = (struct bfa_bsg_fcpim_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfa_fcpim_path_tov_set(&bfad->bfa, iocmd->param);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	iocmd->status = BFA_STATUS_OK;
	return 0;
}

int
bfad_iocmd_fcpim_get_modstats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_fcpim_modstats_s *iocmd =
			(struct bfa_bsg_fcpim_modstats_s *)cmd;
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(&bfad->bfa);
	struct list_head *qe, *qen;
	struct bfa_itnim_s *itnim;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	/* accumulate IO stats from itnim */
	memset((void *)&iocmd->modstats, 0, sizeof(struct bfa_itnim_iostats_s));
	list_for_each_safe(qe, qen, &fcpim->itnim_q) {
		itnim = (struct bfa_itnim_s *) qe;
		bfa_fcpim_add_stats(&iocmd->modstats, &(itnim->stats));
	}
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	iocmd->status = BFA_STATUS_OK;
	return 0;
}

int
bfad_iocmd_fcpim_clr_modstats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_fcpim_modstatsclr_s *iocmd =
				(struct bfa_bsg_fcpim_modstatsclr_s *)cmd;
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(&bfad->bfa);
	struct list_head *qe, *qen;
	struct bfa_itnim_s *itnim;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	list_for_each_safe(qe, qen, &fcpim->itnim_q) {
		itnim = (struct bfa_itnim_s *) qe;
		bfa_itnim_clear_stats(itnim);
	}
	memset(&fcpim->del_itn_stats, 0,
		sizeof(struct bfa_fcpim_del_itn_stats_s));
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	iocmd->status = BFA_STATUS_OK;
	return 0;
}

int
bfad_iocmd_fcpim_get_del_itn_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_fcpim_del_itn_stats_s *iocmd =
			(struct bfa_bsg_fcpim_del_itn_stats_s *)cmd;
	struct bfa_fcpim_s *fcpim = BFA_FCPIM(&bfad->bfa);
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	memcpy((void *)&iocmd->modstats, (void *)&fcpim->del_itn_stats,
		sizeof(struct bfa_fcpim_del_itn_stats_s));
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	iocmd->status = BFA_STATUS_OK;
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

static int
bfad_iocmd_itnim_get_iostats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_itnim_iostats_s *iocmd =
			(struct bfa_bsg_itnim_iostats_s *)cmd;
	struct bfa_fcs_lport_s *fcs_port;
	struct bfa_fcs_itnim_s *itnim;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_port = bfa_fcs_lookup_port(&bfad->bfa_fcs,
				iocmd->vf_id, iocmd->lpwwn);
	if (!fcs_port) {
		iocmd->status = BFA_STATUS_UNKNOWN_LWWN;
		bfa_trc(bfad, 0);
	} else {
		itnim = bfa_fcs_itnim_lookup(fcs_port, iocmd->rpwwn);
		if (itnim == NULL)
			iocmd->status = BFA_STATUS_UNKNOWN_RWWN;
		else {
			iocmd->status = BFA_STATUS_OK;
			if (bfa_fcs_itnim_get_halitn(itnim))
				memcpy((void *)&iocmd->iostats, (void *)
				&(bfa_fcs_itnim_get_halitn(itnim)->stats),
				       sizeof(struct bfa_itnim_iostats_s));
		}
	}
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	return 0;
}

static int
bfad_iocmd_itnim_reset_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_rport_reset_stats_s *iocmd =
			(struct bfa_bsg_rport_reset_stats_s *)cmd;
	struct bfa_fcs_lport_s	*fcs_port;
	struct bfa_fcs_itnim_s	*itnim;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_port = bfa_fcs_lookup_port(&bfad->bfa_fcs,
				iocmd->vf_id, iocmd->pwwn);
	if (!fcs_port)
		iocmd->status = BFA_STATUS_UNKNOWN_LWWN;
	else {
		itnim = bfa_fcs_itnim_lookup(fcs_port, iocmd->rpwwn);
		if (itnim == NULL)
			iocmd->status = BFA_STATUS_UNKNOWN_RWWN;
		else {
			iocmd->status = BFA_STATUS_OK;
			bfa_fcs_itnim_stats_clear(fcs_port, iocmd->rpwwn);
			bfa_itnim_clear_stats(bfa_fcs_itnim_get_halitn(itnim));
		}
	}
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

static int
bfad_iocmd_itnim_get_itnstats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_itnim_itnstats_s *iocmd =
			(struct bfa_bsg_itnim_itnstats_s *)cmd;
	struct bfa_fcs_lport_s *fcs_port;
	struct bfa_fcs_itnim_s *itnim;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_port = bfa_fcs_lookup_port(&bfad->bfa_fcs,
				iocmd->vf_id, iocmd->lpwwn);
	if (!fcs_port) {
		iocmd->status = BFA_STATUS_UNKNOWN_LWWN;
		bfa_trc(bfad, 0);
	} else {
		itnim = bfa_fcs_itnim_lookup(fcs_port, iocmd->rpwwn);
		if (itnim == NULL)
			iocmd->status = BFA_STATUS_UNKNOWN_RWWN;
		else {
			iocmd->status = BFA_STATUS_OK;
			bfa_fcs_itnim_stats_get(fcs_port, iocmd->rpwwn,
					&iocmd->itnstats);
		}
	}
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	return 0;
}

int
bfad_iocmd_fcport_enable(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	unsigned long flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_fcport_enable(&bfad->bfa);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

int
bfad_iocmd_fcport_disable(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	unsigned long flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_fcport_disable(&bfad->bfa);
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
				iocmd->pcifn_class, iocmd->bw_min,
				iocmd->bw_max, bfad_hcb_comp, &fcomp);
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
				iocmd->pcifn_id, iocmd->bw_min,
				iocmd->bw_max, bfad_hcb_comp, &fcomp);
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

int
bfad_iocmd_cee_attr(struct bfad_s *bfad, void *cmd, unsigned int payload_len)
{
	struct bfa_bsg_cee_attr_s *iocmd =
				(struct bfa_bsg_cee_attr_s *)cmd;
	void	*iocmd_bufptr;
	struct bfad_hal_comp	cee_comp;
	unsigned long	flags;

	if (bfad_chk_iocmd_sz(payload_len,
			sizeof(struct bfa_bsg_cee_attr_s),
			sizeof(struct bfa_cee_attr_s)) != BFA_STATUS_OK) {
		iocmd->status = BFA_STATUS_VERSION_FAIL;
		return 0;
	}

	iocmd_bufptr = (char *)iocmd + sizeof(struct bfa_bsg_cee_attr_s);

	cee_comp.status = 0;
	init_completion(&cee_comp.comp);
	mutex_lock(&bfad_mutex);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_cee_get_attr(&bfad->bfa.modules.cee, iocmd_bufptr,
					 bfad_hcb_comp, &cee_comp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK) {
		mutex_unlock(&bfad_mutex);
		bfa_trc(bfad, 0x5555);
		goto out;
	}
	wait_for_completion(&cee_comp.comp);
	mutex_unlock(&bfad_mutex);
out:
	return 0;
}

int
bfad_iocmd_cee_get_stats(struct bfad_s *bfad, void *cmd,
			unsigned int payload_len)
{
	struct bfa_bsg_cee_stats_s *iocmd =
				(struct bfa_bsg_cee_stats_s *)cmd;
	void	*iocmd_bufptr;
	struct bfad_hal_comp	cee_comp;
	unsigned long	flags;

	if (bfad_chk_iocmd_sz(payload_len,
			sizeof(struct bfa_bsg_cee_stats_s),
			sizeof(struct bfa_cee_stats_s)) != BFA_STATUS_OK) {
		iocmd->status = BFA_STATUS_VERSION_FAIL;
		return 0;
	}

	iocmd_bufptr = (char *)iocmd + sizeof(struct bfa_bsg_cee_stats_s);

	cee_comp.status = 0;
	init_completion(&cee_comp.comp);
	mutex_lock(&bfad_mutex);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_cee_get_stats(&bfad->bfa.modules.cee, iocmd_bufptr,
					bfad_hcb_comp, &cee_comp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK) {
		mutex_unlock(&bfad_mutex);
		bfa_trc(bfad, 0x5555);
		goto out;
	}
	wait_for_completion(&cee_comp.comp);
	mutex_unlock(&bfad_mutex);
out:
	return 0;
}

int
bfad_iocmd_cee_reset_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_cee_reset_stats(&bfad->bfa.modules.cee, NULL, NULL);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK)
		bfa_trc(bfad, 0x5555);
	return 0;
}

int
bfad_iocmd_sfp_media(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_sfp_media_s *iocmd = (struct bfa_bsg_sfp_media_s *)cmd;
	struct bfad_hal_comp	fcomp;
	unsigned long	flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_sfp_media(BFA_SFP_MOD(&bfad->bfa), &iocmd->media,
				bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	bfa_trc(bfad, iocmd->status);
	if (iocmd->status != BFA_STATUS_SFP_NOT_READY)
		goto out;

	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_sfp_speed(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_sfp_speed_s *iocmd = (struct bfa_bsg_sfp_speed_s *)cmd;
	struct bfad_hal_comp	fcomp;
	unsigned long	flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_sfp_speed(BFA_SFP_MOD(&bfad->bfa), iocmd->speed,
				bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	bfa_trc(bfad, iocmd->status);
	if (iocmd->status != BFA_STATUS_SFP_NOT_READY)
		goto out;
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_flash_get_attr(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_flash_attr_s *iocmd =
			(struct bfa_bsg_flash_attr_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long	flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_flash_get_attr(BFA_FLASH(&bfad->bfa), &iocmd->attr,
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
bfad_iocmd_flash_erase_part(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_flash_s *iocmd = (struct bfa_bsg_flash_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long	flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_flash_erase_part(BFA_FLASH(&bfad->bfa), iocmd->type,
				iocmd->instance, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK)
		goto out;
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_flash_update_part(struct bfad_s *bfad, void *cmd,
			unsigned int payload_len)
{
	struct bfa_bsg_flash_s *iocmd = (struct bfa_bsg_flash_s *)cmd;
	void	*iocmd_bufptr;
	struct bfad_hal_comp fcomp;
	unsigned long	flags;

	if (bfad_chk_iocmd_sz(payload_len,
			sizeof(struct bfa_bsg_flash_s),
			iocmd->bufsz) != BFA_STATUS_OK) {
		iocmd->status = BFA_STATUS_VERSION_FAIL;
		return 0;
	}

	iocmd_bufptr = (char *)iocmd + sizeof(struct bfa_bsg_flash_s);

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_flash_update_part(BFA_FLASH(&bfad->bfa),
				iocmd->type, iocmd->instance, iocmd_bufptr,
				iocmd->bufsz, 0, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK)
		goto out;
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_flash_read_part(struct bfad_s *bfad, void *cmd,
			unsigned int payload_len)
{
	struct bfa_bsg_flash_s *iocmd = (struct bfa_bsg_flash_s *)cmd;
	struct bfad_hal_comp fcomp;
	void	*iocmd_bufptr;
	unsigned long	flags;

	if (bfad_chk_iocmd_sz(payload_len,
			sizeof(struct bfa_bsg_flash_s),
			iocmd->bufsz) != BFA_STATUS_OK) {
		iocmd->status = BFA_STATUS_VERSION_FAIL;
		return 0;
	}

	iocmd_bufptr = (char *)iocmd + sizeof(struct bfa_bsg_flash_s);

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_flash_read_part(BFA_FLASH(&bfad->bfa), iocmd->type,
				iocmd->instance, iocmd_bufptr, iocmd->bufsz, 0,
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
bfad_iocmd_diag_temp(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_diag_get_temp_s *iocmd =
			(struct bfa_bsg_diag_get_temp_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long	flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_diag_tsensor_query(BFA_DIAG_MOD(&bfad->bfa),
				&iocmd->result, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	bfa_trc(bfad, iocmd->status);
	if (iocmd->status != BFA_STATUS_OK)
		goto out;
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_diag_memtest(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_diag_memtest_s *iocmd =
			(struct bfa_bsg_diag_memtest_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long   flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_diag_memtest(BFA_DIAG_MOD(&bfad->bfa),
				&iocmd->memtest, iocmd->pat,
				&iocmd->result, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	bfa_trc(bfad, iocmd->status);
	if (iocmd->status != BFA_STATUS_OK)
		goto out;
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_diag_loopback(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_diag_loopback_s *iocmd =
			(struct bfa_bsg_diag_loopback_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long   flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_fcdiag_loopback(&bfad->bfa, iocmd->opmode,
				iocmd->speed, iocmd->lpcnt, iocmd->pat,
				&iocmd->result, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	bfa_trc(bfad, iocmd->status);
	if (iocmd->status != BFA_STATUS_OK)
		goto out;
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_diag_fwping(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_diag_fwping_s *iocmd =
			(struct bfa_bsg_diag_fwping_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long   flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_diag_fwping(BFA_DIAG_MOD(&bfad->bfa), iocmd->cnt,
				iocmd->pattern, &iocmd->result,
				bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	bfa_trc(bfad, iocmd->status);
	if (iocmd->status != BFA_STATUS_OK)
		goto out;
	bfa_trc(bfad, 0x77771);
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_diag_queuetest(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_diag_qtest_s *iocmd = (struct bfa_bsg_diag_qtest_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long   flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_fcdiag_queuetest(&bfad->bfa, iocmd->force,
				iocmd->queue, &iocmd->result,
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
bfad_iocmd_diag_sfp(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_sfp_show_s *iocmd =
			(struct bfa_bsg_sfp_show_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long   flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_sfp_show(BFA_SFP_MOD(&bfad->bfa), &iocmd->sfp,
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
bfad_iocmd_diag_led(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_diag_led_s *iocmd = (struct bfa_bsg_diag_led_s *)cmd;
	unsigned long   flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_diag_ledtest(BFA_DIAG_MOD(&bfad->bfa),
				&iocmd->ledtest);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	return 0;
}

int
bfad_iocmd_diag_beacon_lport(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_diag_beacon_s *iocmd =
			(struct bfa_bsg_diag_beacon_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_diag_beacon_port(BFA_DIAG_MOD(&bfad->bfa),
				iocmd->beacon, iocmd->link_e2e_beacon,
				iocmd->second);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	return 0;
}

int
bfad_iocmd_diag_lb_stat(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_diag_lb_stat_s *iocmd =
			(struct bfa_bsg_diag_lb_stat_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_fcdiag_lb_is_running(&bfad->bfa);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	bfa_trc(bfad, iocmd->status);

	return 0;
}

int
bfad_iocmd_diag_dport_enable(struct bfad_s *bfad, void *pcmd)
{
	struct bfa_bsg_dport_enable_s *iocmd =
				(struct bfa_bsg_dport_enable_s *)pcmd;
	unsigned long	flags;
	struct bfad_hal_comp fcomp;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_dport_enable(&bfad->bfa, iocmd->lpcnt,
					iocmd->pat, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK)
		bfa_trc(bfad, iocmd->status);
	else {
		wait_for_completion(&fcomp.comp);
		iocmd->status = fcomp.status;
	}
	return 0;
}

int
bfad_iocmd_diag_dport_disable(struct bfad_s *bfad, void *pcmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)pcmd;
	unsigned long	flags;
	struct bfad_hal_comp fcomp;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_dport_disable(&bfad->bfa, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK)
		bfa_trc(bfad, iocmd->status);
	else {
		wait_for_completion(&fcomp.comp);
		iocmd->status = fcomp.status;
	}
	return 0;
}

int
bfad_iocmd_diag_dport_start(struct bfad_s *bfad, void *pcmd)
{
	struct bfa_bsg_dport_enable_s *iocmd =
				(struct bfa_bsg_dport_enable_s *)pcmd;
	unsigned long   flags;
	struct bfad_hal_comp fcomp;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_dport_start(&bfad->bfa, iocmd->lpcnt,
					iocmd->pat, bfad_hcb_comp,
					&fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (iocmd->status != BFA_STATUS_OK) {
		bfa_trc(bfad, iocmd->status);
	} else {
		wait_for_completion(&fcomp.comp);
		iocmd->status = fcomp.status;
	}

	return 0;
}

int
bfad_iocmd_diag_dport_show(struct bfad_s *bfad, void *pcmd)
{
	struct bfa_bsg_diag_dport_show_s *iocmd =
				(struct bfa_bsg_diag_dport_show_s *)pcmd;
	unsigned long   flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_dport_show(&bfad->bfa, &iocmd->result);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}


int
bfad_iocmd_phy_get_attr(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_phy_attr_s *iocmd =
			(struct bfa_bsg_phy_attr_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long	flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_phy_get_attr(BFA_PHY(&bfad->bfa), iocmd->instance,
				&iocmd->attr, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK)
		goto out;
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_phy_get_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_phy_stats_s *iocmd =
			(struct bfa_bsg_phy_stats_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long	flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_phy_get_stats(BFA_PHY(&bfad->bfa), iocmd->instance,
				&iocmd->stats, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK)
		goto out;
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_phy_read(struct bfad_s *bfad, void *cmd, unsigned int payload_len)
{
	struct bfa_bsg_phy_s *iocmd = (struct bfa_bsg_phy_s *)cmd;
	struct bfad_hal_comp fcomp;
	void	*iocmd_bufptr;
	unsigned long	flags;

	if (bfad_chk_iocmd_sz(payload_len,
			sizeof(struct bfa_bsg_phy_s),
			iocmd->bufsz) != BFA_STATUS_OK) {
		iocmd->status = BFA_STATUS_VERSION_FAIL;
		return 0;
	}

	iocmd_bufptr = (char *)iocmd + sizeof(struct bfa_bsg_phy_s);
	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_phy_read(BFA_PHY(&bfad->bfa),
				iocmd->instance, iocmd_bufptr, iocmd->bufsz,
				0, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK)
		goto out;
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
	if (iocmd->status != BFA_STATUS_OK)
		goto out;
out:
	return 0;
}

int
bfad_iocmd_vhba_query(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_vhba_attr_s *iocmd =
			(struct bfa_bsg_vhba_attr_s *)cmd;
	struct bfa_vhba_attr_s *attr = &iocmd->attr;
	unsigned long flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	attr->pwwn =  bfad->bfa.ioc.attr->pwwn;
	attr->nwwn =  bfad->bfa.ioc.attr->nwwn;
	attr->plog_enabled = (bfa_boolean_t)bfad->bfa.plog->plog_enabled;
	attr->io_profile = bfa_fcpim_get_io_profile(&bfad->bfa);
	attr->path_tov  = bfa_fcpim_path_tov_get(&bfad->bfa);
	iocmd->status = BFA_STATUS_OK;
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	return 0;
}

int
bfad_iocmd_phy_update(struct bfad_s *bfad, void *cmd, unsigned int payload_len)
{
	struct bfa_bsg_phy_s *iocmd = (struct bfa_bsg_phy_s *)cmd;
	void	*iocmd_bufptr;
	struct bfad_hal_comp fcomp;
	unsigned long	flags;

	if (bfad_chk_iocmd_sz(payload_len,
			sizeof(struct bfa_bsg_phy_s),
			iocmd->bufsz) != BFA_STATUS_OK) {
		iocmd->status = BFA_STATUS_VERSION_FAIL;
		return 0;
	}

	iocmd_bufptr = (char *)iocmd + sizeof(struct bfa_bsg_phy_s);
	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_phy_update(BFA_PHY(&bfad->bfa),
				iocmd->instance, iocmd_bufptr, iocmd->bufsz,
				0, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK)
		goto out;
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_porglog_get(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_debug_s *iocmd = (struct bfa_bsg_debug_s *)cmd;
	void *iocmd_bufptr;

	if (iocmd->bufsz < sizeof(struct bfa_plog_s)) {
		bfa_trc(bfad, sizeof(struct bfa_plog_s));
		iocmd->status = BFA_STATUS_EINVAL;
		goto out;
	}

	iocmd->status = BFA_STATUS_OK;
	iocmd_bufptr = (char *)iocmd + sizeof(struct bfa_bsg_debug_s);
	memcpy(iocmd_bufptr, (u8 *) &bfad->plog_buf, sizeof(struct bfa_plog_s));
out:
	return 0;
}

#define BFA_DEBUG_FW_CORE_CHUNK_SZ	0x4000U /* 16K chunks for FW dump */
int
bfad_iocmd_debug_fw_core(struct bfad_s *bfad, void *cmd,
			unsigned int payload_len)
{
	struct bfa_bsg_debug_s *iocmd = (struct bfa_bsg_debug_s *)cmd;
	void	*iocmd_bufptr;
	unsigned long	flags;
	u32 offset;

	if (bfad_chk_iocmd_sz(payload_len, sizeof(struct bfa_bsg_debug_s),
			BFA_DEBUG_FW_CORE_CHUNK_SZ) != BFA_STATUS_OK) {
		iocmd->status = BFA_STATUS_VERSION_FAIL;
		return 0;
	}

	if (iocmd->bufsz < BFA_DEBUG_FW_CORE_CHUNK_SZ ||
			!IS_ALIGNED(iocmd->bufsz, sizeof(u16)) ||
			!IS_ALIGNED(iocmd->offset, sizeof(u32))) {
		bfa_trc(bfad, BFA_DEBUG_FW_CORE_CHUNK_SZ);
		iocmd->status = BFA_STATUS_EINVAL;
		goto out;
	}

	iocmd_bufptr = (char *)iocmd + sizeof(struct bfa_bsg_debug_s);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	offset = iocmd->offset;
	iocmd->status = bfa_ioc_debug_fwcore(&bfad->bfa.ioc, iocmd_bufptr,
				&offset, &iocmd->bufsz);
	iocmd->offset = offset;
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
out:
	return 0;
}

int
bfad_iocmd_debug_ctl(struct bfad_s *bfad, void *cmd, unsigned int v_cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	unsigned long	flags;

	if (v_cmd == IOCMD_DEBUG_FW_STATE_CLR) {
		spin_lock_irqsave(&bfad->bfad_lock, flags);
		bfad->bfa.ioc.dbg_fwsave_once = BFA_TRUE;
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	} else if (v_cmd == IOCMD_DEBUG_PORTLOG_CLR)
		bfad->plog_buf.head = bfad->plog_buf.tail = 0;
	else if (v_cmd == IOCMD_DEBUG_START_DTRC)
		bfa_trc_init(bfad->trcmod);
	else if (v_cmd == IOCMD_DEBUG_STOP_DTRC)
		bfa_trc_stop(bfad->trcmod);

	iocmd->status = BFA_STATUS_OK;
	return 0;
}

int
bfad_iocmd_porglog_ctl(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_portlogctl_s *iocmd = (struct bfa_bsg_portlogctl_s *)cmd;

	if (iocmd->ctl == BFA_TRUE)
		bfad->plog_buf.plog_enabled = 1;
	else
		bfad->plog_buf.plog_enabled = 0;

	iocmd->status = BFA_STATUS_OK;
	return 0;
}

int
bfad_iocmd_fcpim_cfg_profile(struct bfad_s *bfad, void *cmd, unsigned int v_cmd)
{
	struct bfa_bsg_fcpim_profile_s *iocmd =
				(struct bfa_bsg_fcpim_profile_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	if (v_cmd == IOCMD_FCPIM_PROFILE_ON)
		iocmd->status = bfa_fcpim_profile_on(&bfad->bfa, ktime_get_real_seconds());
	else if (v_cmd == IOCMD_FCPIM_PROFILE_OFF)
		iocmd->status = bfa_fcpim_profile_off(&bfad->bfa);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

static int
bfad_iocmd_itnim_get_ioprofile(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_itnim_ioprofile_s *iocmd =
				(struct bfa_bsg_itnim_ioprofile_s *)cmd;
	struct bfa_fcs_lport_s *fcs_port;
	struct bfa_fcs_itnim_s *itnim;
	unsigned long   flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_port = bfa_fcs_lookup_port(&bfad->bfa_fcs,
				iocmd->vf_id, iocmd->lpwwn);
	if (!fcs_port)
		iocmd->status = BFA_STATUS_UNKNOWN_LWWN;
	else {
		itnim = bfa_fcs_itnim_lookup(fcs_port, iocmd->rpwwn);
		if (itnim == NULL)
			iocmd->status = BFA_STATUS_UNKNOWN_RWWN;
		else
			iocmd->status = bfa_itnim_get_ioprofile(
						bfa_fcs_itnim_get_halitn(itnim),
						&iocmd->ioprofile);
	}
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	return 0;
}

int
bfad_iocmd_fcport_get_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_fcport_stats_s *iocmd =
				(struct bfa_bsg_fcport_stats_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long	flags;
	struct bfa_cb_pending_q_s cb_qe;

	init_completion(&fcomp.comp);
	bfa_pending_q_init(&cb_qe, (bfa_cb_cbfn_t)bfad_hcb_comp,
			   &fcomp, &iocmd->stats);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_fcport_get_stats(&bfad->bfa, &cb_qe);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK) {
		bfa_trc(bfad, iocmd->status);
		goto out;
	}
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_fcport_reset_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long	flags;
	struct bfa_cb_pending_q_s cb_qe;

	init_completion(&fcomp.comp);
	bfa_pending_q_init(&cb_qe, (bfa_cb_cbfn_t)bfad_hcb_comp, &fcomp, NULL);

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_fcport_clear_stats(&bfad->bfa, &cb_qe);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK) {
		bfa_trc(bfad, iocmd->status);
		goto out;
	}
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_boot_cfg(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_boot_s *iocmd = (struct bfa_bsg_boot_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long	flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_flash_update_part(BFA_FLASH(&bfad->bfa),
			BFA_FLASH_PART_BOOT, bfad->bfa.ioc.port_id,
			&iocmd->cfg, sizeof(struct bfa_boot_cfg_s), 0,
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
bfad_iocmd_boot_query(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_boot_s *iocmd = (struct bfa_bsg_boot_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long	flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_flash_read_part(BFA_FLASH(&bfad->bfa),
			BFA_FLASH_PART_BOOT, bfad->bfa.ioc.port_id,
			&iocmd->cfg, sizeof(struct bfa_boot_cfg_s), 0,
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
bfad_iocmd_preboot_query(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_preboot_s *iocmd = (struct bfa_bsg_preboot_s *)cmd;
	struct bfi_iocfc_cfgrsp_s *cfgrsp = bfad->bfa.iocfc.cfgrsp;
	struct bfa_boot_pbc_s *pbcfg = &iocmd->cfg;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	pbcfg->enable = cfgrsp->pbc_cfg.boot_enabled;
	pbcfg->nbluns = cfgrsp->pbc_cfg.nbluns;
	pbcfg->speed = cfgrsp->pbc_cfg.port_speed;
	memcpy(pbcfg->pblun, cfgrsp->pbc_cfg.blun, sizeof(pbcfg->pblun));
	iocmd->status = BFA_STATUS_OK;
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

int
bfad_iocmd_ethboot_cfg(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_ethboot_s *iocmd = (struct bfa_bsg_ethboot_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long	flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_flash_update_part(BFA_FLASH(&bfad->bfa),
				BFA_FLASH_PART_PXECFG,
				bfad->bfa.ioc.port_id, &iocmd->cfg,
				sizeof(struct bfa_ethboot_cfg_s), 0,
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
bfad_iocmd_ethboot_query(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_ethboot_s *iocmd = (struct bfa_bsg_ethboot_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long	flags;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_flash_read_part(BFA_FLASH(&bfad->bfa),
				BFA_FLASH_PART_PXECFG,
				bfad->bfa.ioc.port_id, &iocmd->cfg,
				sizeof(struct bfa_ethboot_cfg_s), 0,
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
bfad_iocmd_cfg_trunk(struct bfad_s *bfad, void *cmd, unsigned int v_cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(&bfad->bfa);
	struct bfa_fcport_trunk_s *trunk = &fcport->trunk;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);

	if (bfa_fcport_is_dport(&bfad->bfa)) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		return BFA_STATUS_DPORT_ERR;
	}

	if ((fcport->cfg.topology == BFA_PORT_TOPOLOGY_LOOP) ||
		(fcport->topology == BFA_PORT_TOPOLOGY_LOOP))
		iocmd->status = BFA_STATUS_TOPOLOGY_LOOP;
	else {
		if (v_cmd == IOCMD_TRUNK_ENABLE) {
			trunk->attr.state = BFA_TRUNK_OFFLINE;
			bfa_fcport_disable(&bfad->bfa);
			fcport->cfg.trunked = BFA_TRUE;
		} else if (v_cmd == IOCMD_TRUNK_DISABLE) {
			trunk->attr.state = BFA_TRUNK_DISABLED;
			bfa_fcport_disable(&bfad->bfa);
			fcport->cfg.trunked = BFA_FALSE;
		}

		if (!bfa_fcport_is_disabled(&bfad->bfa))
			bfa_fcport_enable(&bfad->bfa);

		iocmd->status = BFA_STATUS_OK;
	}

	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

int
bfad_iocmd_trunk_get_attr(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_trunk_attr_s *iocmd = (struct bfa_bsg_trunk_attr_s *)cmd;
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(&bfad->bfa);
	struct bfa_fcport_trunk_s *trunk = &fcport->trunk;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	if ((fcport->cfg.topology == BFA_PORT_TOPOLOGY_LOOP) ||
		(fcport->topology == BFA_PORT_TOPOLOGY_LOOP))
		iocmd->status = BFA_STATUS_TOPOLOGY_LOOP;
	else {
		memcpy((void *)&iocmd->attr, (void *)&trunk->attr,
			sizeof(struct bfa_trunk_attr_s));
		iocmd->attr.port_id = bfa_lps_get_base_pid(&bfad->bfa);
		iocmd->status = BFA_STATUS_OK;
	}
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

int
bfad_iocmd_qos(struct bfad_s *bfad, void *cmd, unsigned int v_cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(&bfad->bfa);
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	if (bfa_ioc_get_type(&bfad->bfa.ioc) == BFA_IOC_TYPE_FC) {
		if ((fcport->cfg.topology == BFA_PORT_TOPOLOGY_LOOP) &&
		(fcport->topology == BFA_PORT_TOPOLOGY_LOOP))
			iocmd->status = BFA_STATUS_TOPOLOGY_LOOP;
		else {
			if (v_cmd == IOCMD_QOS_ENABLE)
				fcport->cfg.qos_enabled = BFA_TRUE;
			else if (v_cmd == IOCMD_QOS_DISABLE) {
				fcport->cfg.qos_enabled = BFA_FALSE;
				fcport->cfg.qos_bw.high = BFA_QOS_BW_HIGH;
				fcport->cfg.qos_bw.med = BFA_QOS_BW_MED;
				fcport->cfg.qos_bw.low = BFA_QOS_BW_LOW;
			}
		}
	}
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

int
bfad_iocmd_qos_get_attr(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_qos_attr_s *iocmd = (struct bfa_bsg_qos_attr_s *)cmd;
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(&bfad->bfa);
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	if ((fcport->cfg.topology == BFA_PORT_TOPOLOGY_LOOP) &&
		(fcport->topology == BFA_PORT_TOPOLOGY_LOOP))
		iocmd->status = BFA_STATUS_TOPOLOGY_LOOP;
	else {
		iocmd->attr.state = fcport->qos_attr.state;
		iocmd->attr.total_bb_cr =
			be32_to_cpu(fcport->qos_attr.total_bb_cr);
		iocmd->attr.qos_bw.high = fcport->cfg.qos_bw.high;
		iocmd->attr.qos_bw.med = fcport->cfg.qos_bw.med;
		iocmd->attr.qos_bw.low = fcport->cfg.qos_bw.low;
		iocmd->attr.qos_bw_op = fcport->qos_attr.qos_bw_op;
		iocmd->status = BFA_STATUS_OK;
	}
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

int
bfad_iocmd_qos_get_vc_attr(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_qos_vc_attr_s *iocmd =
				(struct bfa_bsg_qos_vc_attr_s *)cmd;
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(&bfad->bfa);
	struct bfa_qos_vc_attr_s *bfa_vc_attr = &fcport->qos_vc_attr;
	unsigned long	flags;
	u32	i = 0;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->attr.total_vc_count = be16_to_cpu(bfa_vc_attr->total_vc_count);
	iocmd->attr.shared_credit  = be16_to_cpu(bfa_vc_attr->shared_credit);
	iocmd->attr.elp_opmode_flags  =
				be32_to_cpu(bfa_vc_attr->elp_opmode_flags);

	/* Individual VC info */
	while (i < iocmd->attr.total_vc_count) {
		iocmd->attr.vc_info[i].vc_credit =
				bfa_vc_attr->vc_info[i].vc_credit;
		iocmd->attr.vc_info[i].borrow_credit =
				bfa_vc_attr->vc_info[i].borrow_credit;
		iocmd->attr.vc_info[i].priority =
				bfa_vc_attr->vc_info[i].priority;
		i++;
	}
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	iocmd->status = BFA_STATUS_OK;
	return 0;
}

int
bfad_iocmd_qos_get_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_fcport_stats_s *iocmd =
				(struct bfa_bsg_fcport_stats_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long	flags;
	struct bfa_cb_pending_q_s cb_qe;
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(&bfad->bfa);

	init_completion(&fcomp.comp);
	bfa_pending_q_init(&cb_qe, (bfa_cb_cbfn_t)bfad_hcb_comp,
			   &fcomp, &iocmd->stats);

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	WARN_ON(!bfa_ioc_get_fcmode(&bfad->bfa.ioc));
	if ((fcport->cfg.topology == BFA_PORT_TOPOLOGY_LOOP) &&
		(fcport->topology == BFA_PORT_TOPOLOGY_LOOP))
		iocmd->status = BFA_STATUS_TOPOLOGY_LOOP;
	else
		iocmd->status = bfa_fcport_get_stats(&bfad->bfa, &cb_qe);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK) {
		bfa_trc(bfad, iocmd->status);
		goto out;
	}
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_qos_reset_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long	flags;
	struct bfa_cb_pending_q_s cb_qe;
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(&bfad->bfa);

	init_completion(&fcomp.comp);
	bfa_pending_q_init(&cb_qe, (bfa_cb_cbfn_t)bfad_hcb_comp,
			   &fcomp, NULL);

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	WARN_ON(!bfa_ioc_get_fcmode(&bfad->bfa.ioc));
	if ((fcport->cfg.topology == BFA_PORT_TOPOLOGY_LOOP) &&
		(fcport->topology == BFA_PORT_TOPOLOGY_LOOP))
		iocmd->status = BFA_STATUS_TOPOLOGY_LOOP;
	else
		iocmd->status = bfa_fcport_clear_stats(&bfad->bfa, &cb_qe);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status != BFA_STATUS_OK) {
		bfa_trc(bfad, iocmd->status);
		goto out;
	}
	wait_for_completion(&fcomp.comp);
	iocmd->status = fcomp.status;
out:
	return 0;
}

int
bfad_iocmd_vf_get_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_vf_stats_s *iocmd =
			(struct bfa_bsg_vf_stats_s *)cmd;
	struct bfa_fcs_fabric_s	*fcs_vf;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_vf = bfa_fcs_vf_lookup(&bfad->bfa_fcs, iocmd->vf_id);
	if (fcs_vf == NULL) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_VFID;
		goto out;
	}
	memcpy((void *)&iocmd->stats, (void *)&fcs_vf->stats,
		sizeof(struct bfa_vf_stats_s));
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	iocmd->status = BFA_STATUS_OK;
out:
	return 0;
}

int
bfad_iocmd_vf_clr_stats(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_vf_reset_stats_s *iocmd =
			(struct bfa_bsg_vf_reset_stats_s *)cmd;
	struct bfa_fcs_fabric_s	*fcs_vf;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_vf = bfa_fcs_vf_lookup(&bfad->bfa_fcs, iocmd->vf_id);
	if (fcs_vf == NULL) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		iocmd->status = BFA_STATUS_UNKNOWN_VFID;
		goto out;
	}
	memset((void *)&fcs_vf->stats, 0, sizeof(struct bfa_vf_stats_s));
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	iocmd->status = BFA_STATUS_OK;
out:
	return 0;
}

/* Function to reset the LUN SCAN mode */
static void
bfad_iocmd_lunmask_reset_lunscan_mode(struct bfad_s *bfad, int lunmask_cfg)
{
	struct bfad_im_port_s *pport_im = bfad->pport.im_port;
	struct bfad_vport_s *vport = NULL;

	/* Set the scsi device LUN SCAN flags for base port */
	bfad_reset_sdev_bflags(pport_im, lunmask_cfg);

	/* Set the scsi device LUN SCAN flags for the vports */
	list_for_each_entry(vport, &bfad->vport_list, list_entry)
		bfad_reset_sdev_bflags(vport->drv_port.im_port, lunmask_cfg);
}

int
bfad_iocmd_lunmask(struct bfad_s *bfad, void *pcmd, unsigned int v_cmd)
{
	struct bfa_bsg_gen_s *iocmd = (struct bfa_bsg_gen_s *)pcmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	if (v_cmd == IOCMD_FCPIM_LUNMASK_ENABLE) {
		iocmd->status = bfa_fcpim_lunmask_update(&bfad->bfa, BFA_TRUE);
		/* Set the LUN Scanning mode to be Sequential scan */
		if (iocmd->status == BFA_STATUS_OK)
			bfad_iocmd_lunmask_reset_lunscan_mode(bfad, BFA_TRUE);
	} else if (v_cmd == IOCMD_FCPIM_LUNMASK_DISABLE) {
		iocmd->status = bfa_fcpim_lunmask_update(&bfad->bfa, BFA_FALSE);
		/* Set the LUN Scanning mode to default REPORT_LUNS scan */
		if (iocmd->status == BFA_STATUS_OK)
			bfad_iocmd_lunmask_reset_lunscan_mode(bfad, BFA_FALSE);
	} else if (v_cmd == IOCMD_FCPIM_LUNMASK_CLEAR)
		iocmd->status = bfa_fcpim_lunmask_clear(&bfad->bfa);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	return 0;
}

int
bfad_iocmd_fcpim_lunmask_query(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_fcpim_lunmask_query_s *iocmd =
			(struct bfa_bsg_fcpim_lunmask_query_s *)cmd;
	struct bfa_lunmask_cfg_s *lun_mask = &iocmd->lun_mask;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_fcpim_lunmask_query(&bfad->bfa, lun_mask);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	return 0;
}

int
bfad_iocmd_fcpim_cfg_lunmask(struct bfad_s *bfad, void *cmd, unsigned int v_cmd)
{
	struct bfa_bsg_fcpim_lunmask_s *iocmd =
				(struct bfa_bsg_fcpim_lunmask_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	if (v_cmd == IOCMD_FCPIM_LUNMASK_ADD)
		iocmd->status = bfa_fcpim_lunmask_add(&bfad->bfa, iocmd->vf_id,
					&iocmd->pwwn, iocmd->rpwwn, iocmd->lun);
	else if (v_cmd == IOCMD_FCPIM_LUNMASK_DELETE)
		iocmd->status = bfa_fcpim_lunmask_delete(&bfad->bfa,
					iocmd->vf_id, &iocmd->pwwn,
					iocmd->rpwwn, iocmd->lun);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	return 0;
}

int
bfad_iocmd_fcpim_throttle_query(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_fcpim_throttle_s *iocmd =
			(struct bfa_bsg_fcpim_throttle_s *)cmd;
	unsigned long   flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_fcpim_throttle_get(&bfad->bfa,
				(void *)&iocmd->throttle);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

int
bfad_iocmd_fcpim_throttle_set(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_fcpim_throttle_s *iocmd =
			(struct bfa_bsg_fcpim_throttle_s *)cmd;
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_fcpim_throttle_set(&bfad->bfa,
				iocmd->throttle.cfg_value);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

int
bfad_iocmd_tfru_read(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_tfru_s *iocmd =
			(struct bfa_bsg_tfru_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long flags = 0;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_tfru_read(BFA_FRU(&bfad->bfa),
				&iocmd->data, iocmd->len, iocmd->offset,
				bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status == BFA_STATUS_OK) {
		wait_for_completion(&fcomp.comp);
		iocmd->status = fcomp.status;
	}

	return 0;
}

int
bfad_iocmd_tfru_write(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_tfru_s *iocmd =
			(struct bfa_bsg_tfru_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long flags = 0;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_tfru_write(BFA_FRU(&bfad->bfa),
				&iocmd->data, iocmd->len, iocmd->offset,
				bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status == BFA_STATUS_OK) {
		wait_for_completion(&fcomp.comp);
		iocmd->status = fcomp.status;
	}

	return 0;
}

int
bfad_iocmd_fruvpd_read(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_fruvpd_s *iocmd =
			(struct bfa_bsg_fruvpd_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long flags = 0;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_fruvpd_read(BFA_FRU(&bfad->bfa),
				&iocmd->data, iocmd->len, iocmd->offset,
				bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status == BFA_STATUS_OK) {
		wait_for_completion(&fcomp.comp);
		iocmd->status = fcomp.status;
	}

	return 0;
}

int
bfad_iocmd_fruvpd_update(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_fruvpd_s *iocmd =
			(struct bfa_bsg_fruvpd_s *)cmd;
	struct bfad_hal_comp fcomp;
	unsigned long flags = 0;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_fruvpd_update(BFA_FRU(&bfad->bfa),
				&iocmd->data, iocmd->len, iocmd->offset,
				bfad_hcb_comp, &fcomp, iocmd->trfr_cmpl);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (iocmd->status == BFA_STATUS_OK) {
		wait_for_completion(&fcomp.comp);
		iocmd->status = fcomp.status;
	}

	return 0;
}

int
bfad_iocmd_fruvpd_get_max_size(struct bfad_s *bfad, void *cmd)
{
	struct bfa_bsg_fruvpd_max_size_s *iocmd =
			(struct bfa_bsg_fruvpd_max_size_s *)cmd;
	unsigned long flags = 0;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	iocmd->status = bfa_fruvpd_get_max_size(BFA_FRU(&bfad->bfa),
						&iocmd->max_size);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;
}

static int
bfad_iocmd_handler(struct bfad_s *bfad, unsigned int cmd, void *iocmd,
		unsigned int payload_len)
{
	int rc = -EINVAL;

	switch (cmd) {
	case IOCMD_IOC_ENABLE:
		rc = bfad_iocmd_ioc_enable(bfad, iocmd);
		break;
	case IOCMD_IOC_DISABLE:
		rc = bfad_iocmd_ioc_disable(bfad, iocmd);
		break;
	case IOCMD_IOC_GET_INFO:
		rc = bfad_iocmd_ioc_get_info(bfad, iocmd);
		break;
	case IOCMD_IOC_GET_ATTR:
		rc = bfad_iocmd_ioc_get_attr(bfad, iocmd);
		break;
	case IOCMD_IOC_GET_STATS:
		rc = bfad_iocmd_ioc_get_stats(bfad, iocmd);
		break;
	case IOCMD_IOC_GET_FWSTATS:
		rc = bfad_iocmd_ioc_get_fwstats(bfad, iocmd, payload_len);
		break;
	case IOCMD_IOC_RESET_STATS:
	case IOCMD_IOC_RESET_FWSTATS:
		rc = bfad_iocmd_ioc_reset_stats(bfad, iocmd, cmd);
		break;
	case IOCMD_IOC_SET_ADAPTER_NAME:
	case IOCMD_IOC_SET_PORT_NAME:
		rc = bfad_iocmd_ioc_set_name(bfad, iocmd, cmd);
		break;
	case IOCMD_IOCFC_GET_ATTR:
		rc = bfad_iocmd_iocfc_get_attr(bfad, iocmd);
		break;
	case IOCMD_IOCFC_SET_INTR:
		rc = bfad_iocmd_iocfc_set_intr(bfad, iocmd);
		break;
	case IOCMD_PORT_ENABLE:
		rc = bfad_iocmd_port_enable(bfad, iocmd);
		break;
	case IOCMD_PORT_DISABLE:
		rc = bfad_iocmd_port_disable(bfad, iocmd);
		break;
	case IOCMD_PORT_GET_ATTR:
		rc = bfad_iocmd_port_get_attr(bfad, iocmd);
		break;
	case IOCMD_PORT_GET_STATS:
		rc = bfad_iocmd_port_get_stats(bfad, iocmd, payload_len);
		break;
	case IOCMD_PORT_RESET_STATS:
		rc = bfad_iocmd_port_reset_stats(bfad, iocmd);
		break;
	case IOCMD_PORT_CFG_TOPO:
	case IOCMD_PORT_CFG_SPEED:
	case IOCMD_PORT_CFG_ALPA:
	case IOCMD_PORT_CLR_ALPA:
		rc = bfad_iocmd_set_port_cfg(bfad, iocmd, cmd);
		break;
	case IOCMD_PORT_CFG_MAXFRSZ:
		rc = bfad_iocmd_port_cfg_maxfrsize(bfad, iocmd);
		break;
	case IOCMD_PORT_BBCR_ENABLE:
	case IOCMD_PORT_BBCR_DISABLE:
		rc = bfad_iocmd_port_cfg_bbcr(bfad, cmd, iocmd);
		break;
	case IOCMD_PORT_BBCR_GET_ATTR:
		rc = bfad_iocmd_port_get_bbcr_attr(bfad, iocmd);
		break;
	case IOCMD_LPORT_GET_ATTR:
		rc = bfad_iocmd_lport_get_attr(bfad, iocmd);
		break;
	case IOCMD_LPORT_GET_STATS:
		rc = bfad_iocmd_lport_get_stats(bfad, iocmd);
		break;
	case IOCMD_LPORT_RESET_STATS:
		rc = bfad_iocmd_lport_reset_stats(bfad, iocmd);
		break;
	case IOCMD_LPORT_GET_IOSTATS:
		rc = bfad_iocmd_lport_get_iostats(bfad, iocmd);
		break;
	case IOCMD_LPORT_GET_RPORTS:
		rc = bfad_iocmd_lport_get_rports(bfad, iocmd, payload_len);
		break;
	case IOCMD_RPORT_GET_ATTR:
		rc = bfad_iocmd_rport_get_attr(bfad, iocmd);
		break;
	case IOCMD_RPORT_GET_ADDR:
		rc = bfad_iocmd_rport_get_addr(bfad, iocmd);
		break;
	case IOCMD_RPORT_GET_STATS:
		rc = bfad_iocmd_rport_get_stats(bfad, iocmd);
		break;
	case IOCMD_RPORT_RESET_STATS:
		rc = bfad_iocmd_rport_clr_stats(bfad, iocmd);
		break;
	case IOCMD_RPORT_SET_SPEED:
		rc = bfad_iocmd_rport_set_speed(bfad, iocmd);
		break;
	case IOCMD_VPORT_GET_ATTR:
		rc = bfad_iocmd_vport_get_attr(bfad, iocmd);
		break;
	case IOCMD_VPORT_GET_STATS:
		rc = bfad_iocmd_vport_get_stats(bfad, iocmd);
		break;
	case IOCMD_VPORT_RESET_STATS:
		rc = bfad_iocmd_vport_clr_stats(bfad, iocmd);
		break;
	case IOCMD_FABRIC_GET_LPORTS:
		rc = bfad_iocmd_fabric_get_lports(bfad, iocmd, payload_len);
		break;
	case IOCMD_RATELIM_ENABLE:
	case IOCMD_RATELIM_DISABLE:
		rc = bfad_iocmd_ratelim(bfad, cmd, iocmd);
		break;
	case IOCMD_RATELIM_DEF_SPEED:
		rc = bfad_iocmd_ratelim_speed(bfad, cmd, iocmd);
		break;
	case IOCMD_FCPIM_FAILOVER:
		rc = bfad_iocmd_cfg_fcpim(bfad, iocmd);
		break;
	case IOCMD_FCPIM_MODSTATS:
		rc = bfad_iocmd_fcpim_get_modstats(bfad, iocmd);
		break;
	case IOCMD_FCPIM_MODSTATSCLR:
		rc = bfad_iocmd_fcpim_clr_modstats(bfad, iocmd);
		break;
	case IOCMD_FCPIM_DEL_ITN_STATS:
		rc = bfad_iocmd_fcpim_get_del_itn_stats(bfad, iocmd);
		break;
	case IOCMD_ITNIM_GET_ATTR:
		rc = bfad_iocmd_itnim_get_attr(bfad, iocmd);
		break;
	case IOCMD_ITNIM_GET_IOSTATS:
		rc = bfad_iocmd_itnim_get_iostats(bfad, iocmd);
		break;
	case IOCMD_ITNIM_RESET_STATS:
		rc = bfad_iocmd_itnim_reset_stats(bfad, iocmd);
		break;
	case IOCMD_ITNIM_GET_ITNSTATS:
		rc = bfad_iocmd_itnim_get_itnstats(bfad, iocmd);
		break;
	case IOCMD_FCPORT_ENABLE:
		rc = bfad_iocmd_fcport_enable(bfad, iocmd);
		break;
	case IOCMD_FCPORT_DISABLE:
		rc = bfad_iocmd_fcport_disable(bfad, iocmd);
		break;
	case IOCMD_IOC_PCIFN_CFG:
		rc = bfad_iocmd_ioc_get_pcifn_cfg(bfad, iocmd);
		break;
	case IOCMD_IOC_FW_SIG_INV:
		rc = bfad_iocmd_ioc_fw_sig_inv(bfad, iocmd);
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
	case IOCMD_FAA_QUERY:
		rc = bfad_iocmd_faa_query(bfad, iocmd);
		break;
	case IOCMD_CEE_GET_ATTR:
		rc = bfad_iocmd_cee_attr(bfad, iocmd, payload_len);
		break;
	case IOCMD_CEE_GET_STATS:
		rc = bfad_iocmd_cee_get_stats(bfad, iocmd, payload_len);
		break;
	case IOCMD_CEE_RESET_STATS:
		rc = bfad_iocmd_cee_reset_stats(bfad, iocmd);
		break;
	case IOCMD_SFP_MEDIA:
		rc = bfad_iocmd_sfp_media(bfad, iocmd);
		 break;
	case IOCMD_SFP_SPEED:
		rc = bfad_iocmd_sfp_speed(bfad, iocmd);
		break;
	case IOCMD_FLASH_GET_ATTR:
		rc = bfad_iocmd_flash_get_attr(bfad, iocmd);
		break;
	case IOCMD_FLASH_ERASE_PART:
		rc = bfad_iocmd_flash_erase_part(bfad, iocmd);
		break;
	case IOCMD_FLASH_UPDATE_PART:
		rc = bfad_iocmd_flash_update_part(bfad, iocmd, payload_len);
		break;
	case IOCMD_FLASH_READ_PART:
		rc = bfad_iocmd_flash_read_part(bfad, iocmd, payload_len);
		break;
	case IOCMD_DIAG_TEMP:
		rc = bfad_iocmd_diag_temp(bfad, iocmd);
		break;
	case IOCMD_DIAG_MEMTEST:
		rc = bfad_iocmd_diag_memtest(bfad, iocmd);
		break;
	case IOCMD_DIAG_LOOPBACK:
		rc = bfad_iocmd_diag_loopback(bfad, iocmd);
		break;
	case IOCMD_DIAG_FWPING:
		rc = bfad_iocmd_diag_fwping(bfad, iocmd);
		break;
	case IOCMD_DIAG_QUEUETEST:
		rc = bfad_iocmd_diag_queuetest(bfad, iocmd);
		break;
	case IOCMD_DIAG_SFP:
		rc = bfad_iocmd_diag_sfp(bfad, iocmd);
		break;
	case IOCMD_DIAG_LED:
		rc = bfad_iocmd_diag_led(bfad, iocmd);
		break;
	case IOCMD_DIAG_BEACON_LPORT:
		rc = bfad_iocmd_diag_beacon_lport(bfad, iocmd);
		break;
	case IOCMD_DIAG_LB_STAT:
		rc = bfad_iocmd_diag_lb_stat(bfad, iocmd);
		break;
	case IOCMD_DIAG_DPORT_ENABLE:
		rc = bfad_iocmd_diag_dport_enable(bfad, iocmd);
		break;
	case IOCMD_DIAG_DPORT_DISABLE:
		rc = bfad_iocmd_diag_dport_disable(bfad, iocmd);
		break;
	case IOCMD_DIAG_DPORT_SHOW:
		rc = bfad_iocmd_diag_dport_show(bfad, iocmd);
		break;
	case IOCMD_DIAG_DPORT_START:
		rc = bfad_iocmd_diag_dport_start(bfad, iocmd);
		break;
	case IOCMD_PHY_GET_ATTR:
		rc = bfad_iocmd_phy_get_attr(bfad, iocmd);
		break;
	case IOCMD_PHY_GET_STATS:
		rc = bfad_iocmd_phy_get_stats(bfad, iocmd);
		break;
	case IOCMD_PHY_UPDATE_FW:
		rc = bfad_iocmd_phy_update(bfad, iocmd, payload_len);
		break;
	case IOCMD_PHY_READ_FW:
		rc = bfad_iocmd_phy_read(bfad, iocmd, payload_len);
		break;
	case IOCMD_VHBA_QUERY:
		rc = bfad_iocmd_vhba_query(bfad, iocmd);
		break;
	case IOCMD_DEBUG_PORTLOG:
		rc = bfad_iocmd_porglog_get(bfad, iocmd);
		break;
	case IOCMD_DEBUG_FW_CORE:
		rc = bfad_iocmd_debug_fw_core(bfad, iocmd, payload_len);
		break;
	case IOCMD_DEBUG_FW_STATE_CLR:
	case IOCMD_DEBUG_PORTLOG_CLR:
	case IOCMD_DEBUG_START_DTRC:
	case IOCMD_DEBUG_STOP_DTRC:
		rc = bfad_iocmd_debug_ctl(bfad, iocmd, cmd);
		break;
	case IOCMD_DEBUG_PORTLOG_CTL:
		rc = bfad_iocmd_porglog_ctl(bfad, iocmd);
		break;
	case IOCMD_FCPIM_PROFILE_ON:
	case IOCMD_FCPIM_PROFILE_OFF:
		rc = bfad_iocmd_fcpim_cfg_profile(bfad, iocmd, cmd);
		break;
	case IOCMD_ITNIM_GET_IOPROFILE:
		rc = bfad_iocmd_itnim_get_ioprofile(bfad, iocmd);
		break;
	case IOCMD_FCPORT_GET_STATS:
		rc = bfad_iocmd_fcport_get_stats(bfad, iocmd);
		break;
	case IOCMD_FCPORT_RESET_STATS:
		rc = bfad_iocmd_fcport_reset_stats(bfad, iocmd);
		break;
	case IOCMD_BOOT_CFG:
		rc = bfad_iocmd_boot_cfg(bfad, iocmd);
		break;
	case IOCMD_BOOT_QUERY:
		rc = bfad_iocmd_boot_query(bfad, iocmd);
		break;
	case IOCMD_PREBOOT_QUERY:
		rc = bfad_iocmd_preboot_query(bfad, iocmd);
		break;
	case IOCMD_ETHBOOT_CFG:
		rc = bfad_iocmd_ethboot_cfg(bfad, iocmd);
		break;
	case IOCMD_ETHBOOT_QUERY:
		rc = bfad_iocmd_ethboot_query(bfad, iocmd);
		break;
	case IOCMD_TRUNK_ENABLE:
	case IOCMD_TRUNK_DISABLE:
		rc = bfad_iocmd_cfg_trunk(bfad, iocmd, cmd);
		break;
	case IOCMD_TRUNK_GET_ATTR:
		rc = bfad_iocmd_trunk_get_attr(bfad, iocmd);
		break;
	case IOCMD_QOS_ENABLE:
	case IOCMD_QOS_DISABLE:
		rc = bfad_iocmd_qos(bfad, iocmd, cmd);
		break;
	case IOCMD_QOS_GET_ATTR:
		rc = bfad_iocmd_qos_get_attr(bfad, iocmd);
		break;
	case IOCMD_QOS_GET_VC_ATTR:
		rc = bfad_iocmd_qos_get_vc_attr(bfad, iocmd);
		break;
	case IOCMD_QOS_GET_STATS:
		rc = bfad_iocmd_qos_get_stats(bfad, iocmd);
		break;
	case IOCMD_QOS_RESET_STATS:
		rc = bfad_iocmd_qos_reset_stats(bfad, iocmd);
		break;
	case IOCMD_QOS_SET_BW:
		rc = bfad_iocmd_qos_set_bw(bfad, iocmd);
		break;
	case IOCMD_VF_GET_STATS:
		rc = bfad_iocmd_vf_get_stats(bfad, iocmd);
		break;
	case IOCMD_VF_RESET_STATS:
		rc = bfad_iocmd_vf_clr_stats(bfad, iocmd);
		break;
	case IOCMD_FCPIM_LUNMASK_ENABLE:
	case IOCMD_FCPIM_LUNMASK_DISABLE:
	case IOCMD_FCPIM_LUNMASK_CLEAR:
		rc = bfad_iocmd_lunmask(bfad, iocmd, cmd);
		break;
	case IOCMD_FCPIM_LUNMASK_QUERY:
		rc = bfad_iocmd_fcpim_lunmask_query(bfad, iocmd);
		break;
	case IOCMD_FCPIM_LUNMASK_ADD:
	case IOCMD_FCPIM_LUNMASK_DELETE:
		rc = bfad_iocmd_fcpim_cfg_lunmask(bfad, iocmd, cmd);
		break;
	case IOCMD_FCPIM_THROTTLE_QUERY:
		rc = bfad_iocmd_fcpim_throttle_query(bfad, iocmd);
		break;
	case IOCMD_FCPIM_THROTTLE_SET:
		rc = bfad_iocmd_fcpim_throttle_set(bfad, iocmd);
		break;
	/* TFRU */
	case IOCMD_TFRU_READ:
		rc = bfad_iocmd_tfru_read(bfad, iocmd);
		break;
	case IOCMD_TFRU_WRITE:
		rc = bfad_iocmd_tfru_write(bfad, iocmd);
		break;
	/* FRU */
	case IOCMD_FRUVPD_READ:
		rc = bfad_iocmd_fruvpd_read(bfad, iocmd);
		break;
	case IOCMD_FRUVPD_UPDATE:
		rc = bfad_iocmd_fruvpd_update(bfad, iocmd);
		break;
	case IOCMD_FRUVPD_GET_MAX_SIZE:
		rc = bfad_iocmd_fruvpd_get_max_size(bfad, iocmd);
		break;
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}

static int
bfad_im_bsg_vendor_request(struct bsg_job *job)
{
	struct fc_bsg_request *bsg_request = job->request;
	struct fc_bsg_reply *bsg_reply = job->reply;
	uint32_t vendor_cmd = bsg_request->rqst_data.h_vendor.vendor_cmd[0];
	struct bfad_im_port_s *im_port = shost_priv(fc_bsg_to_shost(job));
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
	bsg_reply->reply_payload_rcv_len = job->reply_payload.payload_len;
	bsg_reply->result = rc;

	bsg_job_done(job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);
	return rc;
error:
	/* free the command buffer */
	kfree(payload_kbuf);
out:
	bsg_reply->result = rc;
	job->reply_len = sizeof(uint32_t);
	bsg_reply->reply_payload_rcv_len = 0;
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
	buf_info->virt = dma_zalloc_coherent(&bfad->pcidev->dev,
					     buf_info->size, &buf_info->phys,
					     GFP_KERNEL);
	if (!buf_info->virt)
		goto out_free_mem;

	/* copy the linear bsg buffer to buf_info */
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
bfad_fcxp_bsg_send(struct bsg_job *job, struct bfad_fcxp *drv_fcxp,
		   bfa_bsg_fcpt_t *bsg_fcpt)
{
	struct bfa_fcxp_s *hal_fcxp;
	struct bfad_s	*bfad = drv_fcxp->port->bfad;
	unsigned long	flags;
	uint8_t	lp_tag;

	spin_lock_irqsave(&bfad->bfad_lock, flags);

	/* Allocate bfa_fcxp structure */
	hal_fcxp = bfa_fcxp_req_rsp_alloc(drv_fcxp, &bfad->bfa,
				  drv_fcxp->num_req_sgles,
				  drv_fcxp->num_rsp_sgles,
				  bfad_fcxp_get_req_sgaddr_cb,
				  bfad_fcxp_get_req_sglen_cb,
				  bfad_fcxp_get_rsp_sgaddr_cb,
				  bfad_fcxp_get_rsp_sglen_cb, BFA_TRUE);
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
bfad_im_bsg_els_ct_request(struct bsg_job *job)
{
	struct bfa_bsg_data *bsg_data;
	struct bfad_im_port_s *im_port = shost_priv(fc_bsg_to_shost(job));
	struct bfad_s *bfad = im_port->bfad;
	bfa_bsg_fcpt_t *bsg_fcpt;
	struct bfad_fcxp    *drv_fcxp;
	struct bfa_fcs_lport_s *fcs_port;
	struct bfa_fcs_rport_s *fcs_rport;
	struct fc_bsg_request *bsg_request = job->request;
	struct fc_bsg_reply *bsg_reply = job->reply;
	uint32_t command_type = bsg_request->msgcode;
	unsigned long flags;
	struct bfad_buf_info *rsp_buf_info;
	void *req_kbuf = NULL, *rsp_kbuf = NULL;
	int rc = -EINVAL;

	job->reply_len  = sizeof(uint32_t);	/* Atleast uint32_t reply_len */
	bsg_reply->reply_payload_rcv_len = 0;

	/* Get the payload passed in from userspace */
	bsg_data = (struct bfa_bsg_data *) (((char *)bsg_request) +
					    sizeof(struct fc_bsg_request));
	if (bsg_data == NULL)
		goto out;

	/*
	 * Allocate buffer for bsg_fcpt and do a copy_from_user op for payload
	 * buffer of size bsg_data->payload_len
	 */
	bsg_fcpt = kzalloc(bsg_data->payload_len, GFP_KERNEL);
	if (!bsg_fcpt) {
		rc = -ENOMEM;
		goto out;
	}

	if (copy_from_user((uint8_t *)bsg_fcpt,
				(void *)(unsigned long)bsg_data->payload,
				bsg_data->payload_len)) {
		kfree(bsg_fcpt);
		rc = -EIO;
		goto out;
	}

	drv_fcxp = kzalloc(sizeof(struct bfad_fcxp), GFP_KERNEL);
	if (drv_fcxp == NULL) {
		kfree(bsg_fcpt);
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
		bsg_reply->reply_payload_rcv_len = drv_fcxp->rsp_len;
		bsg_reply->reply_data.ctels_reply.status = FC_CTELS_STATUS_OK;
	} else {
		bsg_reply->reply_payload_rcv_len =
					sizeof(struct fc_bsg_ctels_reply);
		job->reply_len = sizeof(uint32_t);
		bsg_reply->reply_data.ctels_reply.status =
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
	if (copy_to_user((void *)(unsigned long)bsg_data->payload,
			(void *)bsg_fcpt, bsg_data->payload_len))
		rc = -EIO;

	kfree(bsg_fcpt);
	kfree(drv_fcxp);
out:
	bsg_reply->result = rc;

	if (rc == BFA_STATUS_OK)
		bsg_job_done(job, bsg_reply->result,
			       bsg_reply->reply_payload_rcv_len);

	return rc;
}

int
bfad_im_bsg_request(struct bsg_job *job)
{
	struct fc_bsg_request *bsg_request = job->request;
	struct fc_bsg_reply *bsg_reply = job->reply;
	uint32_t rc = BFA_STATUS_OK;

	switch (bsg_request->msgcode) {
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
		bsg_reply->result = rc = -EINVAL;
		bsg_reply->reply_payload_rcv_len = 0;
		break;
	}

	return rc;
}

int
bfad_im_bsg_timeout(struct bsg_job *job)
{
	/* Don't complete the BSG job request - return -EAGAIN
	 * to reset bsg job timeout : for ELS/CT pass thru we
	 * already have timer to track the request.
	 */
	return -EAGAIN;
}
