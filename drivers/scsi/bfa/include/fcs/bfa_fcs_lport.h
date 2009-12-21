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
 *  bfa_fcs_port.h BFA fcs port module public interface
 */

#ifndef __BFA_FCS_PORT_H__
#define __BFA_FCS_PORT_H__

#include <defs/bfa_defs_status.h>
#include <defs/bfa_defs_port.h>
#include <defs/bfa_defs_pport.h>
#include <defs/bfa_defs_rport.h>
#include <cs/bfa_q.h>
#include <bfa_svc.h>
#include <cs/bfa_wc.h>

struct bfa_fcs_s;
struct bfa_fcs_fabric_s;

/*
* @todo : need to move to a global config file.
 * Maximum Vports supported per physical port or vf.
 */
#define BFA_FCS_MAX_VPORTS_SUPP_CB  255
#define BFA_FCS_MAX_VPORTS_SUPP_CT  191

/*
* @todo : need to move to a global config file.
 * Maximum Rports supported per port (physical/logical).
 */
#define BFA_FCS_MAX_RPORTS_SUPP  256	/* @todo : tentative value */


struct bfa_fcs_port_ns_s {
	bfa_sm_t        sm;		/*  state machine */
	struct bfa_timer_s timer;
	struct bfa_fcs_port_s *port;	/*  parent port */
	struct bfa_fcxp_s *fcxp;
	struct bfa_fcxp_wqe_s fcxp_wqe;
};


struct bfa_fcs_port_scn_s {
	bfa_sm_t        sm;		/*  state machine */
	struct bfa_timer_s timer;
	struct bfa_fcs_port_s *port;	/*  parent port */
	struct bfa_fcxp_s *fcxp;
	struct bfa_fcxp_wqe_s fcxp_wqe;
};


struct bfa_fcs_port_fdmi_s {
	bfa_sm_t        sm;		/*  state machine */
	struct bfa_timer_s timer;
	struct bfa_fcs_port_ms_s *ms;	/*  parent ms */
	struct bfa_fcxp_s *fcxp;
	struct bfa_fcxp_wqe_s fcxp_wqe;
	u8         retry_cnt;	/*  retry count */
	u8	 	   rsvd[3];
};


struct bfa_fcs_port_ms_s {
	bfa_sm_t        sm;		/*  state machine */
	struct bfa_timer_s timer;
	struct bfa_fcs_port_s *port;	/*  parent port */
	struct bfa_fcxp_s *fcxp;
	struct bfa_fcxp_wqe_s fcxp_wqe;
	struct bfa_fcs_port_fdmi_s fdmi;	/*  FDMI component of MS */
	u8         retry_cnt;	/*  retry count */
	u8	 	   rsvd[3];
};


struct bfa_fcs_port_fab_s {
	struct bfa_fcs_port_ns_s ns;	/*  NS component of port */
	struct bfa_fcs_port_scn_s scn;	/*  scn component of port */
	struct bfa_fcs_port_ms_s ms;	/*  MS component of port */
};



#define 	MAX_ALPA_COUNT 		127

struct bfa_fcs_port_loop_s {
	u8         num_alpa;	/*  Num of ALPA entries in the map */
	u8         alpa_pos_map[MAX_ALPA_COUNT];	/*  ALPA Positional
							 *Map */
	struct bfa_fcs_port_s *port;	/*  parent port */
};



struct bfa_fcs_port_n2n_s {
	u32        rsvd;
	u16        reply_oxid;	/*  ox_id from the req flogi to be
					 *used in flogi acc */
	wwn_t           rem_port_wwn;	/*  Attached port's wwn */
};


union bfa_fcs_port_topo_u {
	struct bfa_fcs_port_fab_s pfab;
	struct bfa_fcs_port_loop_s ploop;
	struct bfa_fcs_port_n2n_s pn2n;
};


struct bfa_fcs_port_s {
	struct list_head         qe;	/*  used by port/vport */
	bfa_sm_t               sm;	/*  state machine */
	struct bfa_fcs_fabric_s *fabric;/*  parent fabric */
	struct bfa_port_cfg_s  port_cfg;/*  port configuration */
	struct bfa_timer_s link_timer;	/*  timer for link offline */
	u32 pid:24;	/*  FC address */
	u8  lp_tag;	/*  lport tag */
	u16 num_rports;	/*  Num of r-ports */
	struct list_head rport_q;	/*  queue of discovered r-ports */
	struct bfa_fcs_s *fcs;	/*  FCS instance */
	union bfa_fcs_port_topo_u port_topo;	/*  fabric/loop/n2n details */
	struct bfad_port_s *bfad_port;	/*  driver peer instance */
	struct bfa_fcs_vport_s *vport;	/*  NULL for base ports */
	struct bfa_fcxp_s *fcxp;
	struct bfa_fcxp_wqe_s fcxp_wqe;
	struct bfa_port_stats_s stats;
	struct bfa_wc_s        wc;	/*  waiting counter for events */
};

#define bfa_fcs_lport_t struct bfa_fcs_port_s

/**
 * Symbolic Name related defines
 *  Total bytes 255.
 *  Physical Port's symbolic name 128 bytes.
 *  For Vports, Vport's symbolic name is appended to the Physical port's
 *  Symbolic Name.
 *
 *  Physical Port's symbolic name Format : (Total 128 bytes)
 *  Adapter Model number/name : 12 bytes
 *  Driver Version     : 10 bytes
 *  Host Machine Name  : 30 bytes
 * 	Host OS Info	   : 48 bytes
 * 	Host OS PATCH Info : 16 bytes
 *  ( remaining 12 bytes reserved to be used for separator)
 */
#define BFA_FCS_PORT_SYMBNAME_SEPARATOR 		" | "

#define BFA_FCS_PORT_SYMBNAME_MODEL_SZ			12
#define BFA_FCS_PORT_SYMBNAME_VERSION_SZ 		10
#define BFA_FCS_PORT_SYMBNAME_MACHINENAME_SZ 	30
#define BFA_FCS_PORT_SYMBNAME_OSINFO_SZ			48
#define BFA_FCS_PORT_SYMBNAME_OSPATCH_SZ		16

/**
 * Get FC port ID for a logical port.
 */
#define bfa_fcs_port_get_fcid(_lport)	((_lport)->pid)
#define bfa_fcs_port_get_pwwn(_lport)	((_lport)->port_cfg.pwwn)
#define bfa_fcs_port_get_nwwn(_lport)	((_lport)->port_cfg.nwwn)
#define bfa_fcs_port_get_psym_name(_lport)	((_lport)->port_cfg.sym_name)
#define bfa_fcs_port_is_initiator(_lport)	\
			((_lport)->port_cfg.roles & BFA_PORT_ROLE_FCP_IM)
#define bfa_fcs_port_is_target(_lport)	\
			((_lport)->port_cfg.roles & BFA_PORT_ROLE_FCP_TM)
#define bfa_fcs_port_get_nrports(_lport)	\
			((_lport) ? (_lport)->num_rports : 0)

static inline struct bfad_port_s *
bfa_fcs_port_get_drvport(struct bfa_fcs_port_s *port)
{
	return port->bfad_port;
}


#define bfa_fcs_port_get_opertype(_lport)	((_lport)->fabric->oper_type)


#define bfa_fcs_port_get_fabric_name(_lport)	((_lport)->fabric->fabric_name)


#define bfa_fcs_port_get_fabric_ipaddr(_lport) \
		((_lport)->fabric->fabric_ip_addr)

/**
 * bfa fcs port public functions
 */
void bfa_fcs_cfg_base_port(struct bfa_fcs_s *fcs,
			struct bfa_port_cfg_s *port_cfg);
struct bfa_fcs_port_s *bfa_fcs_get_base_port(struct bfa_fcs_s *fcs);
void bfa_fcs_port_get_rports(struct bfa_fcs_port_s *port,
			wwn_t rport_wwns[], int *nrports);

wwn_t bfa_fcs_port_get_rport(struct bfa_fcs_port_s *port, wwn_t wwn,
			int index, int nrports, bfa_boolean_t bwwn);

struct bfa_fcs_port_s *bfa_fcs_lookup_port(struct bfa_fcs_s *fcs,
			u16 vf_id, wwn_t lpwwn);

void bfa_fcs_port_get_info(struct bfa_fcs_port_s *port,
			struct bfa_port_info_s *port_info);
void bfa_fcs_port_get_attr(struct bfa_fcs_port_s *port,
			struct bfa_port_attr_s *port_attr);
void bfa_fcs_port_get_stats(struct bfa_fcs_port_s *fcs_port,
			struct bfa_port_stats_s *port_stats);
void bfa_fcs_port_clear_stats(struct bfa_fcs_port_s *fcs_port);
enum bfa_pport_speed bfa_fcs_port_get_rport_max_speed(
			struct bfa_fcs_port_s *port);
void bfa_fcs_port_enable_ipfc_roles(struct bfa_fcs_port_s *fcs_port);
void bfa_fcs_port_disable_ipfc_roles(struct bfa_fcs_port_s *fcs_port);

#endif /* __BFA_FCS_PORT_H__ */
