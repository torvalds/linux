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
 *  bfa_fcs_vport.h BFA fcs vport module public interface
 */

#ifndef __BFA_FCS_VPORT_H__
#define __BFA_FCS_VPORT_H__

#include <defs/bfa_defs_status.h>
#include <defs/bfa_defs_port.h>
#include <defs/bfa_defs_vport.h>
#include <fcs/bfa_fcs.h>
#include <fcb/bfa_fcb_vport.h>

struct bfa_fcs_vport_s {
	struct list_head		qe;		/*  queue elem	 */
	bfa_sm_t		sm;		/*  state machine	*/
	bfa_fcs_lport_t		lport;		/*  logical port	*/
	struct bfa_timer_s	timer;		/*  general purpose timer */
	struct bfad_vport_s	*vport_drv;	/*  Driver private	*/
	struct bfa_vport_stats_s vport_stats;	/*  vport statistics	*/
	struct bfa_lps_s	*lps;		/*  Lport login service */
	int			fdisc_retries;
};

#define bfa_fcs_vport_get_port(vport) \
			((struct bfa_fcs_port_s  *)(&vport->port))

/**
 * bfa fcs vport public functions
 */
bfa_status_t bfa_fcs_vport_create(struct bfa_fcs_vport_s *vport,
			struct bfa_fcs_s *fcs, u16 vf_id,
			struct bfa_port_cfg_s *port_cfg,
			struct bfad_vport_s *vport_drv);
bfa_status_t bfa_fcs_pbc_vport_create(struct bfa_fcs_vport_s *vport,
			struct bfa_fcs_s *fcs, uint16_t vf_id,
			struct bfa_port_cfg_s *port_cfg,
			struct bfad_vport_s *vport_drv);
bfa_status_t bfa_fcs_vport_delete(struct bfa_fcs_vport_s *vport);
bfa_status_t bfa_fcs_vport_start(struct bfa_fcs_vport_s *vport);
bfa_status_t bfa_fcs_vport_stop(struct bfa_fcs_vport_s *vport);
void bfa_fcs_vport_get_attr(struct bfa_fcs_vport_s *vport,
			struct bfa_vport_attr_s *vport_attr);
void bfa_fcs_vport_get_stats(struct bfa_fcs_vport_s *vport,
			struct bfa_vport_stats_s *vport_stats);
void bfa_fcs_vport_clr_stats(struct bfa_fcs_vport_s *vport);
struct bfa_fcs_vport_s *bfa_fcs_vport_lookup(struct bfa_fcs_s *fcs,
			u16 vf_id, wwn_t vpwwn);

#endif /* __BFA_FCS_VPORT_H__ */
