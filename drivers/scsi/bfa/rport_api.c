/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
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
#include <bfa.h>
#include <bfa_svc.h>
#include "fcs_vport.h"
#include "fcs_lport.h"
#include "fcs_rport.h"
#include "fcs_trcmod.h"

BFA_TRC_FILE(FCS, RPORT_API);

/**
 *  rport_api.c Remote port implementation.
 */

/**
 *  fcs_rport_api FCS rport API.
 */

/**
 * 	Direct API to add a target by port wwn. This interface is used, for
 *	example, by bios when target pwwn is known from boot lun configuration.
 */
bfa_status_t
bfa_fcs_rport_add(struct bfa_fcs_port_s *port, wwn_t *pwwn,
			struct bfa_fcs_rport_s *rport,
			struct bfad_rport_s *rport_drv)
{
	bfa_trc(port->fcs, *pwwn);

	return BFA_STATUS_OK;
}

/**
 *	Direct API to remove a target and its associated resources. This
 *	interface is used, for example, by vmware driver to remove target
 *	ports from the target list for a VM.
 */
bfa_status_t
bfa_fcs_rport_remove(struct bfa_fcs_rport_s *rport_in)
{

	struct bfa_fcs_rport_s *rport;

	bfa_trc(rport_in->fcs, rport_in->pwwn);

	rport = bfa_fcs_port_get_rport_by_pwwn(rport_in->port, rport_in->pwwn);
	if (rport == NULL) {
		/*
		 * TBD Error handling
		 */
		bfa_trc(rport_in->fcs, rport_in->pid);
		return BFA_STATUS_UNKNOWN_RWWN;
	}

	/*
	 * TBD if this remote port is online, send a logo
	 */
	return BFA_STATUS_OK;

}

/**
 *	Remote device status for display/debug.
 */
void
bfa_fcs_rport_get_attr(struct bfa_fcs_rport_s *rport,
			struct bfa_rport_attr_s *rport_attr)
{
	struct bfa_rport_qos_attr_s qos_attr;
	struct bfa_fcs_port_s *port = rport->port;
	enum bfa_pport_speed rport_speed = rport->rpf.rpsc_speed;

	bfa_os_memset(rport_attr, 0, sizeof(struct bfa_rport_attr_s));

	rport_attr->pid = rport->pid;
	rport_attr->pwwn = rport->pwwn;
	rport_attr->nwwn = rport->nwwn;
	rport_attr->cos_supported = rport->fc_cos;
	rport_attr->df_sz = rport->maxfrsize;
	rport_attr->state = bfa_fcs_rport_get_state(rport);
	rport_attr->fc_cos = rport->fc_cos;
	rport_attr->cisc = rport->cisc;
	rport_attr->scsi_function = rport->scsi_function;
	rport_attr->curr_speed  = rport->rpf.rpsc_speed;
	rport_attr->assigned_speed  = rport->rpf.assigned_speed;

	bfa_rport_get_qos_attr(rport->bfa_rport, &qos_attr);
	rport_attr->qos_attr = qos_attr;

	rport_attr->trl_enforced = BFA_FALSE;

	if (bfa_fcport_is_ratelim(port->fcs->bfa)) {
		if (rport_speed == BFA_PPORT_SPEED_UNKNOWN) {
			/* Use default ratelim speed setting */
			rport_speed =
				bfa_fcport_get_ratelim_speed(rport->fcs->bfa);
		}
		if (rport_speed < bfa_fcs_port_get_rport_max_speed(port))
			rport_attr->trl_enforced = BFA_TRUE;
	}

	/*
	 * TODO
	 * rport->symname
	 */
}

/**
 * 	Per remote device statistics.
 */
void
bfa_fcs_rport_get_stats(struct bfa_fcs_rport_s *rport,
			struct bfa_rport_stats_s *stats)
{
	*stats = rport->stats;
}

void
bfa_fcs_rport_clear_stats(struct bfa_fcs_rport_s *rport)
{
	bfa_os_memset((char *)&rport->stats, 0,
			sizeof(struct bfa_rport_stats_s));
}

struct bfa_fcs_rport_s *
bfa_fcs_rport_lookup(struct bfa_fcs_port_s *port, wwn_t rpwwn)
{
	struct bfa_fcs_rport_s *rport;

	rport = bfa_fcs_port_get_rport_by_pwwn(port, rpwwn);
	if (rport == NULL) {
		/*
		 * TBD Error handling
		 */
	}

	return rport;
}

struct bfa_fcs_rport_s *
bfa_fcs_rport_lookup_by_nwwn(struct bfa_fcs_port_s *port, wwn_t rnwwn)
{
	struct bfa_fcs_rport_s *rport;

	rport = bfa_fcs_port_get_rport_by_nwwn(port, rnwwn);
	if (rport == NULL) {
		/*
		 * TBD Error handling
		 */
	}

	return rport;
}

/*
 * This API is to set the Rport's speed. Should be used when RPSC is not
 * supported by the rport.
 */
void
bfa_fcs_rport_set_speed(struct bfa_fcs_rport_s *rport,
			enum bfa_pport_speed speed)
{
	rport->rpf.assigned_speed  = speed;

	/* Set this speed in f/w only if the RPSC speed is not available */
	if (rport->rpf.rpsc_speed == BFA_PPORT_SPEED_UNKNOWN)
		bfa_rport_speed(rport->bfa_rport, speed);
}


