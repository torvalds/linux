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

/*
 *  bfa_attr.c Linux driver configuration interface module.
 */

#include "bfad_drv.h"
#include "bfad_im.h"

/*
 * FC transport template entry, get SCSI target port ID.
 */
static void
bfad_im_get_starget_port_id(struct scsi_target *starget)
{
	struct Scsi_Host *shost;
	struct bfad_im_port_s *im_port;
	struct bfad_s         *bfad;
	struct bfad_itnim_s   *itnim = NULL;
	u32        fc_id = -1;
	unsigned long   flags;

	shost = dev_to_shost(starget->dev.parent);
	im_port = (struct bfad_im_port_s *) shost->hostdata[0];
	bfad = im_port->bfad;
	spin_lock_irqsave(&bfad->bfad_lock, flags);

	itnim = bfad_get_itnim(im_port, starget->id);
	if (itnim)
		fc_id = bfa_fcs_itnim_get_fcid(&itnim->fcs_itnim);

	fc_starget_port_id(starget) = fc_id;
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
}

/*
 * FC transport template entry, get SCSI target nwwn.
 */
static void
bfad_im_get_starget_node_name(struct scsi_target *starget)
{
	struct Scsi_Host *shost;
	struct bfad_im_port_s *im_port;
	struct bfad_s         *bfad;
	struct bfad_itnim_s   *itnim = NULL;
	u64             node_name = 0;
	unsigned long   flags;

	shost = dev_to_shost(starget->dev.parent);
	im_port = (struct bfad_im_port_s *) shost->hostdata[0];
	bfad = im_port->bfad;
	spin_lock_irqsave(&bfad->bfad_lock, flags);

	itnim = bfad_get_itnim(im_port, starget->id);
	if (itnim)
		node_name = bfa_fcs_itnim_get_nwwn(&itnim->fcs_itnim);

	fc_starget_node_name(starget) = cpu_to_be64(node_name);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
}

/*
 * FC transport template entry, get SCSI target pwwn.
 */
static void
bfad_im_get_starget_port_name(struct scsi_target *starget)
{
	struct Scsi_Host *shost;
	struct bfad_im_port_s *im_port;
	struct bfad_s         *bfad;
	struct bfad_itnim_s   *itnim = NULL;
	u64             port_name = 0;
	unsigned long   flags;

	shost = dev_to_shost(starget->dev.parent);
	im_port = (struct bfad_im_port_s *) shost->hostdata[0];
	bfad = im_port->bfad;
	spin_lock_irqsave(&bfad->bfad_lock, flags);

	itnim = bfad_get_itnim(im_port, starget->id);
	if (itnim)
		port_name = bfa_fcs_itnim_get_pwwn(&itnim->fcs_itnim);

	fc_starget_port_name(starget) = cpu_to_be64(port_name);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
}

/*
 * FC transport template entry, get SCSI host port ID.
 */
static void
bfad_im_get_host_port_id(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_port_s    *port = im_port->port;

	fc_host_port_id(shost) =
			bfa_hton3b(bfa_fcs_lport_get_fcid(port->fcs_port));
}

/*
 * FC transport template entry, get SCSI host port type.
 */
static void
bfad_im_get_host_port_type(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfa_lport_attr_s port_attr;

	bfa_fcs_lport_get_attr(&bfad->bfa_fcs.fabric.bport, &port_attr);

	switch (port_attr.port_type) {
	case BFA_PORT_TYPE_NPORT:
		fc_host_port_type(shost) = FC_PORTTYPE_NPORT;
		break;
	case BFA_PORT_TYPE_NLPORT:
		fc_host_port_type(shost) = FC_PORTTYPE_NLPORT;
		break;
	case BFA_PORT_TYPE_P2P:
		fc_host_port_type(shost) = FC_PORTTYPE_PTP;
		break;
	case BFA_PORT_TYPE_LPORT:
		fc_host_port_type(shost) = FC_PORTTYPE_LPORT;
		break;
	default:
		fc_host_port_type(shost) = FC_PORTTYPE_UNKNOWN;
		break;
	}
}

/*
 * FC transport template entry, get SCSI host port state.
 */
static void
bfad_im_get_host_port_state(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfa_port_attr_s attr;

	bfa_fcport_get_attr(&bfad->bfa, &attr);

	switch (attr.port_state) {
	case BFA_PORT_ST_LINKDOWN:
		fc_host_port_state(shost) = FC_PORTSTATE_LINKDOWN;
		break;
	case BFA_PORT_ST_LINKUP:
		fc_host_port_state(shost) = FC_PORTSTATE_ONLINE;
		break;
	case BFA_PORT_ST_DISABLED:
	case BFA_PORT_ST_STOPPED:
	case BFA_PORT_ST_IOCDOWN:
	case BFA_PORT_ST_IOCDIS:
		fc_host_port_state(shost) = FC_PORTSTATE_OFFLINE;
		break;
	case BFA_PORT_ST_UNINIT:
	case BFA_PORT_ST_ENABLING_QWAIT:
	case BFA_PORT_ST_ENABLING:
	case BFA_PORT_ST_DISABLING_QWAIT:
	case BFA_PORT_ST_DISABLING:
	default:
		fc_host_port_state(shost) = FC_PORTSTATE_UNKNOWN;
		break;
	}
}

/*
 * FC transport template entry, get SCSI host active fc4s.
 */
static void
bfad_im_get_host_active_fc4s(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_port_s    *port = im_port->port;

	memset(fc_host_active_fc4s(shost), 0,
	       sizeof(fc_host_active_fc4s(shost)));

	if (port->supported_fc4s & BFA_LPORT_ROLE_FCP_IM)
		fc_host_active_fc4s(shost)[2] = 1;

	fc_host_active_fc4s(shost)[7] = 1;
}

/*
 * FC transport template entry, get SCSI host link speed.
 */
static void
bfad_im_get_host_speed(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfa_port_attr_s attr;

	bfa_fcport_get_attr(&bfad->bfa, &attr);
	switch (attr.speed) {
	case BFA_PORT_SPEED_10GBPS:
		fc_host_speed(shost) = FC_PORTSPEED_10GBIT;
		break;
	case BFA_PORT_SPEED_16GBPS:
		fc_host_speed(shost) = FC_PORTSPEED_16GBIT;
		break;
	case BFA_PORT_SPEED_8GBPS:
		fc_host_speed(shost) = FC_PORTSPEED_8GBIT;
		break;
	case BFA_PORT_SPEED_4GBPS:
		fc_host_speed(shost) = FC_PORTSPEED_4GBIT;
		break;
	case BFA_PORT_SPEED_2GBPS:
		fc_host_speed(shost) = FC_PORTSPEED_2GBIT;
		break;
	case BFA_PORT_SPEED_1GBPS:
		fc_host_speed(shost) = FC_PORTSPEED_1GBIT;
		break;
	default:
		fc_host_speed(shost) = FC_PORTSPEED_UNKNOWN;
		break;
	}
}

/*
 * FC transport template entry, get SCSI host port type.
 */
static void
bfad_im_get_host_fabric_name(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_port_s    *port = im_port->port;
	wwn_t           fabric_nwwn = 0;

	fabric_nwwn = bfa_fcs_lport_get_fabric_name(port->fcs_port);

	fc_host_fabric_name(shost) = cpu_to_be64(fabric_nwwn);

}

/*
 * FC transport template entry, get BFAD statistics.
 */
static struct fc_host_statistics *
bfad_im_get_stats(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfad_hal_comp fcomp;
	union bfa_port_stats_u *fcstats;
	struct fc_host_statistics *hstats;
	bfa_status_t    rc;
	unsigned long   flags;

	fcstats = kzalloc(sizeof(union bfa_port_stats_u), GFP_KERNEL);
	if (fcstats == NULL)
		return NULL;

	hstats = &bfad->link_stats;
	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	memset(hstats, 0, sizeof(struct fc_host_statistics));
	rc = bfa_port_get_stats(BFA_FCPORT(&bfad->bfa),
				fcstats, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (rc != BFA_STATUS_OK) {
		kfree(fcstats);
		return NULL;
	}

	wait_for_completion(&fcomp.comp);

	/* Fill the fc_host_statistics structure */
	hstats->seconds_since_last_reset = fcstats->fc.secs_reset;
	hstats->tx_frames = fcstats->fc.tx_frames;
	hstats->tx_words  = fcstats->fc.tx_words;
	hstats->rx_frames = fcstats->fc.rx_frames;
	hstats->rx_words  = fcstats->fc.rx_words;
	hstats->lip_count = fcstats->fc.lip_count;
	hstats->nos_count = fcstats->fc.nos_count;
	hstats->error_frames = fcstats->fc.error_frames;
	hstats->dumped_frames = fcstats->fc.dropped_frames;
	hstats->link_failure_count = fcstats->fc.link_failures;
	hstats->loss_of_sync_count = fcstats->fc.loss_of_syncs;
	hstats->loss_of_signal_count = fcstats->fc.loss_of_signals;
	hstats->prim_seq_protocol_err_count = fcstats->fc.primseq_errs;
	hstats->invalid_crc_count = fcstats->fc.invalid_crcs;

	kfree(fcstats);
	return hstats;
}

/*
 * FC transport template entry, reset BFAD statistics.
 */
static void
bfad_im_reset_stats(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfad_hal_comp fcomp;
	unsigned long   flags;
	bfa_status_t    rc;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	rc = bfa_port_clear_stats(BFA_FCPORT(&bfad->bfa), bfad_hcb_comp,
					&fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (rc != BFA_STATUS_OK)
		return;

	wait_for_completion(&fcomp.comp);

	return;
}

/*
 * FC transport template entry, set rport loss timeout.
 * Update dev_loss_tmo based on the value pushed down by the stack
 * In case it is lesser than path_tov of driver, set it to path_tov + 1
 * to ensure that the driver times out before the application
 */
static void
bfad_im_set_rport_loss_tmo(struct fc_rport *rport, u32 timeout)
{
	struct bfad_itnim_data_s *itnim_data = rport->dd_data;
	struct bfad_itnim_s   *itnim = itnim_data->itnim;
	struct bfad_s         *bfad = itnim->im->bfad;
	uint16_t path_tov = bfa_fcpim_path_tov_get(&bfad->bfa);

	rport->dev_loss_tmo = timeout;
	if (timeout < path_tov)
		rport->dev_loss_tmo = path_tov + 1;
}

static int
bfad_im_vport_create(struct fc_vport *fc_vport, bool disable)
{
	char *vname = fc_vport->symbolic_name;
	struct Scsi_Host *shost = fc_vport->shost;
	struct bfad_im_port_s *im_port =
		(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s *bfad = im_port->bfad;
	struct bfa_lport_cfg_s port_cfg;
	struct bfad_vport_s *vp;
	int status = 0, rc;
	unsigned long flags;

	memset(&port_cfg, 0, sizeof(port_cfg));
	u64_to_wwn(fc_vport->node_name, (u8 *)&port_cfg.nwwn);
	u64_to_wwn(fc_vport->port_name, (u8 *)&port_cfg.pwwn);
	if (strlen(vname) > 0)
		strcpy((char *)&port_cfg.sym_name, vname);
	port_cfg.roles = BFA_LPORT_ROLE_FCP_IM;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	list_for_each_entry(vp, &bfad->pbc_vport_list, list_entry) {
		if (port_cfg.pwwn ==
				vp->fcs_vport.lport.port_cfg.pwwn) {
			port_cfg.preboot_vp =
				vp->fcs_vport.lport.port_cfg.preboot_vp;
			break;
		}
	}
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	rc = bfad_vport_create(bfad, 0, &port_cfg, &fc_vport->dev);
	if (rc == BFA_STATUS_OK) {
		struct bfad_vport_s *vport;
		struct bfa_fcs_vport_s *fcs_vport;
		struct Scsi_Host *vshost;

		spin_lock_irqsave(&bfad->bfad_lock, flags);
		fcs_vport = bfa_fcs_vport_lookup(&bfad->bfa_fcs, 0,
					port_cfg.pwwn);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		if (fcs_vport == NULL)
			return VPCERR_BAD_WWN;

		fc_vport_set_state(fc_vport, FC_VPORT_ACTIVE);
		if (disable) {
			spin_lock_irqsave(&bfad->bfad_lock, flags);
			bfa_fcs_vport_stop(fcs_vport);
			spin_unlock_irqrestore(&bfad->bfad_lock, flags);
			fc_vport_set_state(fc_vport, FC_VPORT_DISABLED);
		}

		vport = fcs_vport->vport_drv;
		vshost = vport->drv_port.im_port->shost;
		fc_host_node_name(vshost) = wwn_to_u64((u8 *)&port_cfg.nwwn);
		fc_host_port_name(vshost) = wwn_to_u64((u8 *)&port_cfg.pwwn);
		fc_host_supported_classes(vshost) = FC_COS_CLASS3;

		memset(fc_host_supported_fc4s(vshost), 0,
			sizeof(fc_host_supported_fc4s(vshost)));

		/* For FCP type 0x08 */
		if (supported_fc4s & BFA_LPORT_ROLE_FCP_IM)
			fc_host_supported_fc4s(vshost)[2] = 1;

		/* For fibre channel services type 0x20 */
		fc_host_supported_fc4s(vshost)[7] = 1;

		fc_host_supported_speeds(vshost) =
				bfad_im_supported_speeds(&bfad->bfa);
		fc_host_maxframe_size(vshost) =
				bfa_fcport_get_maxfrsize(&bfad->bfa);

		fc_vport->dd_data = vport;
		vport->drv_port.im_port->fc_vport = fc_vport;
	} else if (rc == BFA_STATUS_INVALID_WWN)
		return VPCERR_BAD_WWN;
	else if (rc == BFA_STATUS_VPORT_EXISTS)
		return VPCERR_BAD_WWN;
	else if (rc == BFA_STATUS_VPORT_MAX)
		return VPCERR_NO_FABRIC_SUPP;
	else if (rc == BFA_STATUS_VPORT_WWN_BP)
		return VPCERR_BAD_WWN;
	else
		return FC_VPORT_FAILED;

	return status;
}

int
bfad_im_issue_fc_host_lip(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s *bfad = im_port->bfad;
	struct bfad_hal_comp fcomp;
	unsigned long flags;
	uint32_t status;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	status = bfa_port_disable(&bfad->bfa.modules.port,
					bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (status != BFA_STATUS_OK)
		return -EIO;

	wait_for_completion(&fcomp.comp);
	if (fcomp.status != BFA_STATUS_OK)
		return -EIO;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	status = bfa_port_enable(&bfad->bfa.modules.port,
					bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (status != BFA_STATUS_OK)
		return -EIO;

	wait_for_completion(&fcomp.comp);
	if (fcomp.status != BFA_STATUS_OK)
		return -EIO;

	return 0;
}

static int
bfad_im_vport_delete(struct fc_vport *fc_vport)
{
	struct bfad_vport_s *vport = (struct bfad_vport_s *)fc_vport->dd_data;
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) vport->drv_port.im_port;
	struct bfad_s *bfad = im_port->bfad;
	struct bfa_fcs_vport_s *fcs_vport;
	struct Scsi_Host *vshost;
	wwn_t   pwwn;
	int rc;
	unsigned long flags;
	struct completion fcomp;

	if (im_port->flags & BFAD_PORT_DELETE) {
		bfad_scsi_host_free(bfad, im_port);
		list_del(&vport->list_entry);
		kfree(vport);
		return 0;
	}

	vshost = vport->drv_port.im_port->shost;
	u64_to_wwn(fc_host_port_name(vshost), (u8 *)&pwwn);

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_vport = bfa_fcs_vport_lookup(&bfad->bfa_fcs, 0, pwwn);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (fcs_vport == NULL)
		return VPCERR_BAD_WWN;

	vport->drv_port.flags |= BFAD_PORT_DELETE;

	vport->comp_del = &fcomp;
	init_completion(vport->comp_del);

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	rc = bfa_fcs_vport_delete(&vport->fcs_vport);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (rc == BFA_STATUS_PBC) {
		vport->drv_port.flags &= ~BFAD_PORT_DELETE;
		vport->comp_del = NULL;
		return -1;
	}

	wait_for_completion(vport->comp_del);

	bfad_scsi_host_free(bfad, im_port);
	list_del(&vport->list_entry);
	kfree(vport);

	return 0;
}

static int
bfad_im_vport_disable(struct fc_vport *fc_vport, bool disable)
{
	struct bfad_vport_s *vport;
	struct bfad_s *bfad;
	struct bfa_fcs_vport_s *fcs_vport;
	struct Scsi_Host *vshost;
	wwn_t   pwwn;
	unsigned long flags;

	vport = (struct bfad_vport_s *)fc_vport->dd_data;
	bfad = vport->drv_port.bfad;
	vshost = vport->drv_port.im_port->shost;
	u64_to_wwn(fc_host_port_name(vshost), (u8 *)&pwwn);

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_vport = bfa_fcs_vport_lookup(&bfad->bfa_fcs, 0, pwwn);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (fcs_vport == NULL)
		return VPCERR_BAD_WWN;

	if (disable) {
		bfa_fcs_vport_stop(fcs_vport);
		fc_vport_set_state(fc_vport, FC_VPORT_DISABLED);
	} else {
		bfa_fcs_vport_start(fcs_vport);
		fc_vport_set_state(fc_vport, FC_VPORT_ACTIVE);
	}

	return 0;
}

void
bfad_im_vport_set_symbolic_name(struct fc_vport *fc_vport)
{
	struct bfad_vport_s *vport = (struct bfad_vport_s *)fc_vport->dd_data;
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *)vport->drv_port.im_port;
	struct bfad_s *bfad = im_port->bfad;
	struct Scsi_Host *vshost = vport->drv_port.im_port->shost;
	char *sym_name = fc_vport->symbolic_name;
	struct bfa_fcs_vport_s *fcs_vport;
	wwn_t	pwwn;
	unsigned long flags;

	u64_to_wwn(fc_host_port_name(vshost), (u8 *)&pwwn);

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	fcs_vport = bfa_fcs_vport_lookup(&bfad->bfa_fcs, 0, pwwn);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (fcs_vport == NULL)
		return;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	if (strlen(sym_name) > 0)
		bfa_fcs_lport_set_symname(&fcs_vport->lport, sym_name);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
}

struct fc_function_template bfad_im_fc_function_template = {

	/* Target dynamic attributes */
	.get_starget_port_id = bfad_im_get_starget_port_id,
	.show_starget_port_id = 1,
	.get_starget_node_name = bfad_im_get_starget_node_name,
	.show_starget_node_name = 1,
	.get_starget_port_name = bfad_im_get_starget_port_name,
	.show_starget_port_name = 1,

	/* Host dynamic attribute */
	.get_host_port_id = bfad_im_get_host_port_id,
	.show_host_port_id = 1,

	/* Host fixed attributes */
	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_fc4s = 1,
	.show_host_supported_speeds = 1,
	.show_host_maxframe_size = 1,

	/* More host dynamic attributes */
	.show_host_port_type = 1,
	.get_host_port_type = bfad_im_get_host_port_type,
	.show_host_port_state = 1,
	.get_host_port_state = bfad_im_get_host_port_state,
	.show_host_active_fc4s = 1,
	.get_host_active_fc4s = bfad_im_get_host_active_fc4s,
	.show_host_speed = 1,
	.get_host_speed = bfad_im_get_host_speed,
	.show_host_fabric_name = 1,
	.get_host_fabric_name = bfad_im_get_host_fabric_name,

	.show_host_symbolic_name = 1,

	/* Statistics */
	.get_fc_host_stats = bfad_im_get_stats,
	.reset_fc_host_stats = bfad_im_reset_stats,

	/* Allocation length for host specific data */
	.dd_fcrport_size = sizeof(struct bfad_itnim_data_s *),

	/* Remote port fixed attributes */
	.show_rport_maxframe_size = 1,
	.show_rport_supported_classes = 1,
	.show_rport_dev_loss_tmo = 1,
	.set_rport_dev_loss_tmo = bfad_im_set_rport_loss_tmo,
	.issue_fc_host_lip = bfad_im_issue_fc_host_lip,
	.vport_create = bfad_im_vport_create,
	.vport_delete = bfad_im_vport_delete,
	.vport_disable = bfad_im_vport_disable,
	.set_vport_symbolic_name = bfad_im_vport_set_symbolic_name,
	.bsg_request = bfad_im_bsg_request,
	.bsg_timeout = bfad_im_bsg_timeout,
};

struct fc_function_template bfad_im_vport_fc_function_template = {

	/* Target dynamic attributes */
	.get_starget_port_id = bfad_im_get_starget_port_id,
	.show_starget_port_id = 1,
	.get_starget_node_name = bfad_im_get_starget_node_name,
	.show_starget_node_name = 1,
	.get_starget_port_name = bfad_im_get_starget_port_name,
	.show_starget_port_name = 1,

	/* Host dynamic attribute */
	.get_host_port_id = bfad_im_get_host_port_id,
	.show_host_port_id = 1,

	/* Host fixed attributes */
	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_fc4s = 1,
	.show_host_supported_speeds = 1,
	.show_host_maxframe_size = 1,

	/* More host dynamic attributes */
	.show_host_port_type = 1,
	.get_host_port_type = bfad_im_get_host_port_type,
	.show_host_port_state = 1,
	.get_host_port_state = bfad_im_get_host_port_state,
	.show_host_active_fc4s = 1,
	.get_host_active_fc4s = bfad_im_get_host_active_fc4s,
	.show_host_speed = 1,
	.get_host_speed = bfad_im_get_host_speed,
	.show_host_fabric_name = 1,
	.get_host_fabric_name = bfad_im_get_host_fabric_name,

	.show_host_symbolic_name = 1,

	/* Statistics */
	.get_fc_host_stats = bfad_im_get_stats,
	.reset_fc_host_stats = bfad_im_reset_stats,

	/* Allocation length for host specific data */
	.dd_fcrport_size = sizeof(struct bfad_itnim_data_s *),

	/* Remote port fixed attributes */
	.show_rport_maxframe_size = 1,
	.show_rport_supported_classes = 1,
	.show_rport_dev_loss_tmo = 1,
	.set_rport_dev_loss_tmo = bfad_im_set_rport_loss_tmo,
};

/*
 *  Scsi_Host_attrs SCSI host attributes
 */
static ssize_t
bfad_im_serial_num_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s *bfad = im_port->bfad;
	char serial_num[BFA_ADAPTER_SERIAL_NUM_LEN];

	bfa_get_adapter_serial_num(&bfad->bfa, serial_num);
	return snprintf(buf, PAGE_SIZE, "%s\n", serial_num);
}

static ssize_t
bfad_im_model_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s *bfad = im_port->bfad;
	char model[BFA_ADAPTER_MODEL_NAME_LEN];

	bfa_get_adapter_model(&bfad->bfa, model);
	return snprintf(buf, PAGE_SIZE, "%s\n", model);
}

static ssize_t
bfad_im_model_desc_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s *bfad = im_port->bfad;
	char model[BFA_ADAPTER_MODEL_NAME_LEN];
	char model_descr[BFA_ADAPTER_MODEL_DESCR_LEN];
	int nports = 0;

	bfa_get_adapter_model(&bfad->bfa, model);
	nports = bfa_get_nports(&bfad->bfa);
	if (!strcmp(model, "QLogic-425"))
		snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
			"QLogic BR-series 4Gbps PCIe dual port FC HBA");
	else if (!strcmp(model, "QLogic-825"))
		snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
			"QLogic BR-series 8Gbps PCIe dual port FC HBA");
	else if (!strcmp(model, "QLogic-42B"))
		snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
			"QLogic BR-series 4Gbps PCIe dual port FC HBA for HP");
	else if (!strcmp(model, "QLogic-82B"))
		snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
			"QLogic BR-series 8Gbps PCIe dual port FC HBA for HP");
	else if (!strcmp(model, "QLogic-1010"))
		snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
			"QLogic BR-series 10Gbps single port CNA");
	else if (!strcmp(model, "QLogic-1020"))
		snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
			"QLogic BR-series 10Gbps dual port CNA");
	else if (!strcmp(model, "QLogic-1007"))
		snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
			"QLogic BR-series 10Gbps CNA for IBM Blade Center");
	else if (!strcmp(model, "QLogic-415"))
		snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
			"QLogic BR-series 4Gbps PCIe single port FC HBA");
	else if (!strcmp(model, "QLogic-815"))
		snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
			"QLogic BR-series 8Gbps PCIe single port FC HBA");
	else if (!strcmp(model, "QLogic-41B"))
		snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
			"QLogic BR-series 4Gbps PCIe single port FC HBA for HP");
	else if (!strcmp(model, "QLogic-81B"))
		snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
			"QLogic BR-series 8Gbps PCIe single port FC HBA for HP");
	else if (!strcmp(model, "QLogic-804"))
		snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
			"QLogic BR-series 8Gbps FC HBA for HP Bladesystem C-class");
	else if (!strcmp(model, "QLogic-1741"))
		snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
			"QLogic BR-series 10Gbps CNA for Dell M-Series Blade Servers");
	else if (strstr(model, "QLogic-1860")) {
		if (nports == 1 && bfa_ioc_is_cna(&bfad->bfa.ioc))
			snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
				"QLogic BR-series 10Gbps single port CNA");
		else if (nports == 1 && !bfa_ioc_is_cna(&bfad->bfa.ioc))
			snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
				"QLogic BR-series 16Gbps PCIe single port FC HBA");
		else if (nports == 2 && bfa_ioc_is_cna(&bfad->bfa.ioc))
			snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
				"QLogic BR-series 10Gbps dual port CNA");
		else if (nports == 2 && !bfa_ioc_is_cna(&bfad->bfa.ioc))
			snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
				"QLogic BR-series 16Gbps PCIe dual port FC HBA");
	} else if (!strcmp(model, "QLogic-1867")) {
		if (nports == 1 && !bfa_ioc_is_cna(&bfad->bfa.ioc))
			snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
				"QLogic BR-series 16Gbps PCIe single port FC HBA for IBM");
		else if (nports == 2 && !bfa_ioc_is_cna(&bfad->bfa.ioc))
			snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
				"QLogic BR-series 16Gbps PCIe dual port FC HBA for IBM");
	} else
		snprintf(model_descr, BFA_ADAPTER_MODEL_DESCR_LEN,
			"Invalid Model");

	return snprintf(buf, PAGE_SIZE, "%s\n", model_descr);
}

static ssize_t
bfad_im_node_name_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_port_s    *port = im_port->port;
	u64        nwwn;

	nwwn = bfa_fcs_lport_get_nwwn(port->fcs_port);
	return snprintf(buf, PAGE_SIZE, "0x%llx\n", cpu_to_be64(nwwn));
}

static ssize_t
bfad_im_symbolic_name_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s *bfad = im_port->bfad;
	struct bfa_lport_attr_s port_attr;
	char symname[BFA_SYMNAME_MAXLEN];

	bfa_fcs_lport_get_attr(&bfad->bfa_fcs.fabric.bport, &port_attr);
	strlcpy(symname, port_attr.port_cfg.sym_name.symname,
			BFA_SYMNAME_MAXLEN);
	return snprintf(buf, PAGE_SIZE, "%s\n", symname);
}

static ssize_t
bfad_im_hw_version_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s *bfad = im_port->bfad;
	char hw_ver[BFA_VERSION_LEN];

	bfa_get_pci_chip_rev(&bfad->bfa, hw_ver);
	return snprintf(buf, PAGE_SIZE, "%s\n", hw_ver);
}

static ssize_t
bfad_im_drv_version_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", BFAD_DRIVER_VERSION);
}

static ssize_t
bfad_im_optionrom_version_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s *bfad = im_port->bfad;
	char optrom_ver[BFA_VERSION_LEN];

	bfa_get_adapter_optrom_ver(&bfad->bfa, optrom_ver);
	return snprintf(buf, PAGE_SIZE, "%s\n", optrom_ver);
}

static ssize_t
bfad_im_fw_version_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s *bfad = im_port->bfad;
	char fw_ver[BFA_VERSION_LEN];

	bfa_get_adapter_fw_ver(&bfad->bfa, fw_ver);
	return snprintf(buf, PAGE_SIZE, "%s\n", fw_ver);
}

static ssize_t
bfad_im_num_of_ports_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s *bfad = im_port->bfad;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			bfa_get_nports(&bfad->bfa));
}

static ssize_t
bfad_im_drv_name_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", BFAD_DRIVER_NAME);
}

static ssize_t
bfad_im_num_of_discovered_ports_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_port_s    *port = im_port->port;
	struct bfad_s         *bfad = im_port->bfad;
	int        nrports = 2048;
	struct bfa_rport_qualifier_s *rports = NULL;
	unsigned long   flags;

	rports = kcalloc(nrports, sizeof(struct bfa_rport_qualifier_s),
			 GFP_ATOMIC);
	if (rports == NULL)
		return snprintf(buf, PAGE_SIZE, "Failed\n");

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfa_fcs_lport_get_rport_quals(port->fcs_port, rports, &nrports);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	kfree(rports);

	return snprintf(buf, PAGE_SIZE, "%d\n", nrports);
}

static          DEVICE_ATTR(serial_number, S_IRUGO,
				bfad_im_serial_num_show, NULL);
static          DEVICE_ATTR(model, S_IRUGO, bfad_im_model_show, NULL);
static          DEVICE_ATTR(model_description, S_IRUGO,
				bfad_im_model_desc_show, NULL);
static          DEVICE_ATTR(node_name, S_IRUGO, bfad_im_node_name_show, NULL);
static          DEVICE_ATTR(symbolic_name, S_IRUGO,
				bfad_im_symbolic_name_show, NULL);
static          DEVICE_ATTR(hardware_version, S_IRUGO,
				bfad_im_hw_version_show, NULL);
static          DEVICE_ATTR(driver_version, S_IRUGO,
				bfad_im_drv_version_show, NULL);
static          DEVICE_ATTR(option_rom_version, S_IRUGO,
				bfad_im_optionrom_version_show, NULL);
static          DEVICE_ATTR(firmware_version, S_IRUGO,
				bfad_im_fw_version_show, NULL);
static          DEVICE_ATTR(number_of_ports, S_IRUGO,
				bfad_im_num_of_ports_show, NULL);
static          DEVICE_ATTR(driver_name, S_IRUGO, bfad_im_drv_name_show, NULL);
static          DEVICE_ATTR(number_of_discovered_ports, S_IRUGO,
				bfad_im_num_of_discovered_ports_show, NULL);

struct device_attribute *bfad_im_host_attrs[] = {
	&dev_attr_serial_number,
	&dev_attr_model,
	&dev_attr_model_description,
	&dev_attr_node_name,
	&dev_attr_symbolic_name,
	&dev_attr_hardware_version,
	&dev_attr_driver_version,
	&dev_attr_option_rom_version,
	&dev_attr_firmware_version,
	&dev_attr_number_of_ports,
	&dev_attr_driver_name,
	&dev_attr_number_of_discovered_ports,
	NULL,
};

struct device_attribute *bfad_im_vport_attrs[] = {
	&dev_attr_serial_number,
	&dev_attr_model,
	&dev_attr_model_description,
	&dev_attr_node_name,
	&dev_attr_symbolic_name,
	&dev_attr_hardware_version,
	&dev_attr_driver_version,
	&dev_attr_option_rom_version,
	&dev_attr_firmware_version,
	&dev_attr_number_of_ports,
	&dev_attr_driver_name,
	&dev_attr_number_of_discovered_ports,
	NULL,
};


