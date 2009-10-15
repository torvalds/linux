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

/**
 *  port_api.c BFA FCS port
 */

#include <fcs/bfa_fcs.h>
#include <fcs/bfa_fcs_lport.h>
#include <fcs/bfa_fcs_rport.h>
#include "fcs_rport.h"
#include "fcs_fabric.h"
#include "fcs_trcmod.h"
#include "fcs_vport.h"

BFA_TRC_FILE(FCS, PORT_API);



/**
 *  fcs_port_api BFA FCS port API
 */

void
bfa_fcs_cfg_base_port(struct bfa_fcs_s *fcs, struct bfa_port_cfg_s *port_cfg)
{
}

struct bfa_fcs_port_s *
bfa_fcs_get_base_port(struct bfa_fcs_s *fcs)
{
	return (&fcs->fabric.bport);
}

wwn_t
bfa_fcs_port_get_rport(struct bfa_fcs_port_s *port, wwn_t wwn, int index,
		       int nrports, bfa_boolean_t bwwn)
{
	struct list_head *qh, *qe;
	struct bfa_fcs_rport_s *rport = NULL;
	int             i;
	struct bfa_fcs_s *fcs;

	if (port == NULL || nrports == 0)
		return (wwn_t) 0;

	fcs = port->fcs;
	bfa_trc(fcs, (u32) nrports);

	i = 0;
	qh = &port->rport_q;
	qe = bfa_q_first(qh);

	while ((qe != qh) && (i < nrports)) {
		rport = (struct bfa_fcs_rport_s *)qe;
		if (bfa_os_ntoh3b(rport->pid) > 0xFFF000) {
			qe = bfa_q_next(qe);
			bfa_trc(fcs, (u32) rport->pwwn);
			bfa_trc(fcs, rport->pid);
			bfa_trc(fcs, i);
			continue;
		}

		if (bwwn) {
			if (!memcmp(&wwn, &rport->pwwn, 8))
				break;
		} else {
			if (i == index)
				break;
		}

		i++;
		qe = bfa_q_next(qe);
	}

	bfa_trc(fcs, i);
	if (rport) {
		return rport->pwwn;
	} else {
		return (wwn_t) 0;
	}
}

void
bfa_fcs_port_get_rports(struct bfa_fcs_port_s *port, wwn_t rport_wwns[],
			int *nrports)
{
	struct list_head *qh, *qe;
	struct bfa_fcs_rport_s *rport = NULL;
	int             i;
	struct bfa_fcs_s *fcs;

	if (port == NULL || rport_wwns == NULL || *nrports == 0)
		return;

	fcs = port->fcs;
	bfa_trc(fcs, (u32) *nrports);

	i = 0;
	qh = &port->rport_q;
	qe = bfa_q_first(qh);

	while ((qe != qh) && (i < *nrports)) {
		rport = (struct bfa_fcs_rport_s *)qe;
		if (bfa_os_ntoh3b(rport->pid) > 0xFFF000) {
			qe = bfa_q_next(qe);
			bfa_trc(fcs, (u32) rport->pwwn);
			bfa_trc(fcs, rport->pid);
			bfa_trc(fcs, i);
			continue;
		}

		rport_wwns[i] = rport->pwwn;

		i++;
		qe = bfa_q_next(qe);
	}

	bfa_trc(fcs, i);
	*nrports = i;
	return;
}

/*
 * Iterate's through all the rport's in the given port to
 * determine the maximum operating speed.
 */
enum bfa_pport_speed
bfa_fcs_port_get_rport_max_speed(struct bfa_fcs_port_s *port)
{
	struct list_head *qh, *qe;
	struct bfa_fcs_rport_s *rport = NULL;
	struct bfa_fcs_s *fcs;
	enum bfa_pport_speed max_speed = 0;
	struct bfa_pport_attr_s pport_attr;
	enum bfa_pport_speed pport_speed;

	if (port == NULL)
		return 0;

	fcs = port->fcs;

	/*
	 * Get Physical port's current speed
	 */
	bfa_pport_get_attr(port->fcs->bfa, &pport_attr);
	pport_speed = pport_attr.speed;
	bfa_trc(fcs, pport_speed);

	qh = &port->rport_q;
	qe = bfa_q_first(qh);

	while (qe != qh) {
		rport = (struct bfa_fcs_rport_s *)qe;
		if ((bfa_os_ntoh3b(rport->pid) > 0xFFF000)
		    || (bfa_fcs_rport_get_state(rport) == BFA_RPORT_OFFLINE)) {
			qe = bfa_q_next(qe);
			continue;
		}

		if ((rport->rpf.rpsc_speed == BFA_PPORT_SPEED_8GBPS)
		    || (rport->rpf.rpsc_speed > pport_speed)) {
			max_speed = rport->rpf.rpsc_speed;
			break;
		} else if (rport->rpf.rpsc_speed > max_speed) {
			max_speed = rport->rpf.rpsc_speed;
		}

		qe = bfa_q_next(qe);
	}

	bfa_trc(fcs, max_speed);
	return max_speed;
}

struct bfa_fcs_port_s *
bfa_fcs_lookup_port(struct bfa_fcs_s *fcs, u16 vf_id, wwn_t lpwwn)
{
	struct bfa_fcs_vport_s *vport;
	bfa_fcs_vf_t   *vf;

	bfa_assert(fcs != NULL);

	vf = bfa_fcs_vf_lookup(fcs, vf_id);
	if (vf == NULL) {
		bfa_trc(fcs, vf_id);
		return (NULL);
	}

	if (!lpwwn || (vf->bport.port_cfg.pwwn == lpwwn))
		return (&vf->bport);

	vport = bfa_fcs_fabric_vport_lookup(vf, lpwwn);
	if (vport)
		return (&vport->lport);

	return (NULL);
}

/*
 *  API corresponding to VmWare's NPIV_VPORT_GETINFO.
 */
void
bfa_fcs_port_get_info(struct bfa_fcs_port_s *port,
		      struct bfa_port_info_s *port_info)
{

	bfa_trc(port->fcs, port->fabric->fabric_name);

	if (port->vport == NULL) {
		/*
		 * This is a Physical port
		 */
		port_info->port_type = BFA_PORT_TYPE_PHYSICAL;

		/*
		 * @todo : need to fix the state & reason
		 */
		port_info->port_state = 0;
		port_info->offline_reason = 0;

		port_info->port_wwn = bfa_fcs_port_get_pwwn(port);
		port_info->node_wwn = bfa_fcs_port_get_nwwn(port);

		port_info->max_vports_supp = bfa_fcs_vport_get_max(port->fcs);
		port_info->num_vports_inuse =
			bfa_fcs_fabric_vport_count(port->fabric);
		port_info->max_rports_supp = BFA_FCS_MAX_RPORTS_SUPP;
		port_info->num_rports_inuse = port->num_rports;
	} else {
		/*
		 * This is a virtual port
		 */
		port_info->port_type = BFA_PORT_TYPE_VIRTUAL;

		/*
		 * @todo : need to fix the state & reason
		 */
		port_info->port_state = 0;
		port_info->offline_reason = 0;

		port_info->port_wwn = bfa_fcs_port_get_pwwn(port);
		port_info->node_wwn = bfa_fcs_port_get_nwwn(port);
	}
}

void
bfa_fcs_port_get_stats(struct bfa_fcs_port_s *fcs_port,
		       struct bfa_port_stats_s *port_stats)
{
	bfa_os_memcpy(port_stats, &fcs_port->stats,
		      sizeof(struct bfa_port_stats_s));
	return;
}

void
bfa_fcs_port_clear_stats(struct bfa_fcs_port_s *fcs_port)
{
	bfa_os_memset(&fcs_port->stats, 0, sizeof(struct bfa_port_stats_s));
	return;
}

void
bfa_fcs_port_enable_ipfc_roles(struct bfa_fcs_port_s *fcs_port)
{
	fcs_port->port_cfg.roles |= BFA_PORT_ROLE_FCP_IPFC;
	return;
}

void
bfa_fcs_port_disable_ipfc_roles(struct bfa_fcs_port_s *fcs_port)
{
	fcs_port->port_cfg.roles &= ~BFA_PORT_ROLE_FCP_IPFC;
	return;
}


