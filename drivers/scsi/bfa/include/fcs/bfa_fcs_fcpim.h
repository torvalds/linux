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
 *  bfa_fcs_fcpim.h BFA FCS FCP Initiator Mode interfaces/defines.
 */

#ifndef __BFA_FCS_FCPIM_H__
#define __BFA_FCS_FCPIM_H__

#include <defs/bfa_defs_status.h>
#include <defs/bfa_defs_itnim.h>
#include <fcs/bfa_fcs.h>
#include <fcs/bfa_fcs_rport.h>
#include <fcs/bfa_fcs_lport.h>
#include <bfa_fcpim.h>

/*
 * forward declarations
 */
struct bfad_itnim_s;

struct bfa_fcs_itnim_s {
	bfa_sm_t		sm;		/*  state machine */
	struct bfa_fcs_rport_s 	*rport;		/*  parent remote rport  */
	struct bfad_itnim_s   	*itnim_drv;	/*  driver peer instance */
	struct bfa_fcs_s      	*fcs;		/*  fcs instance         */
	struct bfa_timer_s 	timer;		/*  timer functions      */
	struct bfa_itnim_s 	*bfa_itnim;	/*  BFA itnim struct     */
	u32                	prli_retries;   /*  max prli retry attempts */
	bfa_boolean_t	 	seq_rec;	/*  seq recovery support */
	bfa_boolean_t	 	rec_support;	/*  REC supported        */
	bfa_boolean_t	 	conf_comp;	/*  FCP_CONF     support */
	bfa_boolean_t	 	task_retry_id;	/*  task retry id supp   */
	struct bfa_fcxp_wqe_s 	fcxp_wqe;	/*  wait qelem for fcxp  */
	struct bfa_fcxp_s *fcxp;		/*  FCXP in use          */
	struct bfa_itnim_stats_s 	stats;	/*  itn statistics       */
};


static inline struct bfad_port_s *
bfa_fcs_itnim_get_drvport(struct bfa_fcs_itnim_s *itnim)
{
	return itnim->rport->port->bfad_port;
}


static inline struct bfa_fcs_port_s *
bfa_fcs_itnim_get_port(struct bfa_fcs_itnim_s *itnim)
{
	return itnim->rport->port;
}


static inline wwn_t
bfa_fcs_itnim_get_nwwn(struct bfa_fcs_itnim_s *itnim)
{
	return itnim->rport->nwwn;
}


static inline wwn_t
bfa_fcs_itnim_get_pwwn(struct bfa_fcs_itnim_s *itnim)
{
	return itnim->rport->pwwn;
}


static inline u32
bfa_fcs_itnim_get_fcid(struct bfa_fcs_itnim_s *itnim)
{
	return itnim->rport->pid;
}


static inline   u32
bfa_fcs_itnim_get_maxfrsize(struct bfa_fcs_itnim_s *itnim)
{
	return itnim->rport->maxfrsize;
}


static inline   enum fc_cos
bfa_fcs_itnim_get_cos(struct bfa_fcs_itnim_s *itnim)
{
	return itnim->rport->fc_cos;
}


static inline struct bfad_itnim_s *
bfa_fcs_itnim_get_drvitn(struct bfa_fcs_itnim_s *itnim)
{
	return itnim->itnim_drv;
}


static inline struct bfa_itnim_s *
bfa_fcs_itnim_get_halitn(struct bfa_fcs_itnim_s *itnim)
{
	return itnim->bfa_itnim;
}

/**
 * bfa fcs FCP Initiator mode API functions
 */
void bfa_fcs_itnim_get_attr(struct bfa_fcs_itnim_s *itnim,
			struct bfa_itnim_attr_s *attr);
void bfa_fcs_itnim_get_stats(struct bfa_fcs_itnim_s *itnim,
			struct bfa_itnim_stats_s *stats);
struct bfa_fcs_itnim_s *bfa_fcs_itnim_lookup(struct bfa_fcs_port_s *port,
			wwn_t rpwwn);
bfa_status_t bfa_fcs_itnim_attr_get(struct bfa_fcs_port_s *port, wwn_t rpwwn,
			struct bfa_itnim_attr_s *attr);
bfa_status_t bfa_fcs_itnim_stats_get(struct bfa_fcs_port_s *port, wwn_t rpwwn,
			struct bfa_itnim_stats_s *stats);
bfa_status_t bfa_fcs_itnim_stats_clear(struct bfa_fcs_port_s *port,
			wwn_t rpwwn);
#endif /* __BFA_FCS_FCPIM_H__ */
