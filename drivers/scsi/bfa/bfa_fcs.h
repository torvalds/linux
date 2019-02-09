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

#ifndef __BFA_FCS_H__
#define __BFA_FCS_H__

#include "bfa_cs.h"
#include "bfa_defs.h"
#include "bfa_defs_fcs.h"
#include "bfa_modules.h"
#include "bfa_fc.h"

#define BFA_FCS_OS_STR_LEN		64

/*
 *  lps_pvt BFA LPS private functions
 */

enum bfa_lps_event {
	BFA_LPS_SM_LOGIN	= 1,	/* login request from user      */
	BFA_LPS_SM_LOGOUT	= 2,	/* logout request from user     */
	BFA_LPS_SM_FWRSP	= 3,	/* f/w response to login/logout */
	BFA_LPS_SM_RESUME	= 4,	/* space present in reqq queue  */
	BFA_LPS_SM_DELETE	= 5,	/* lps delete from user         */
	BFA_LPS_SM_OFFLINE	= 6,	/* Link is offline              */
	BFA_LPS_SM_RX_CVL	= 7,	/* Rx clear virtual link        */
	BFA_LPS_SM_SET_N2N_PID  = 8,	/* Set assigned PID for n2n */
};


/*
 * !!! Only append to the enums defined here to avoid any versioning
 * !!! needed between trace utility and driver version
 */
enum {
	BFA_TRC_FCS_FCS		= 1,
	BFA_TRC_FCS_PORT	= 2,
	BFA_TRC_FCS_RPORT	= 3,
	BFA_TRC_FCS_FCPIM	= 4,
};


struct bfa_fcs_s;

#define __fcs_min_cfg(__fcs)       ((__fcs)->min_cfg)

#define BFA_FCS_BRCD_SWITCH_OUI  0x051e
#define N2N_LOCAL_PID	    0x010000
#define N2N_REMOTE_PID		0x020000
#define	BFA_FCS_RETRY_TIMEOUT 2000
#define BFA_FCS_MAX_NS_RETRIES 5
#define BFA_FCS_PID_IS_WKA(pid)  ((bfa_ntoh3b(pid) > 0xFFF000) ?  1 : 0)
#define BFA_FCS_MAX_RPORT_LOGINS 1024

struct bfa_fcs_lport_ns_s {
	bfa_sm_t        sm;		/*  state machine */
	struct bfa_timer_s timer;
	struct bfa_fcs_lport_s *port;	/*  parent port */
	struct bfa_fcxp_s *fcxp;
	struct bfa_fcxp_wqe_s fcxp_wqe;
	u8	num_rnnid_retries;
	u8	num_rsnn_nn_retries;
};


struct bfa_fcs_lport_scn_s {
	bfa_sm_t        sm;		/*  state machine */
	struct bfa_timer_s timer;
	struct bfa_fcs_lport_s *port;	/*  parent port */
	struct bfa_fcxp_s *fcxp;
	struct bfa_fcxp_wqe_s fcxp_wqe;
};


struct bfa_fcs_lport_fdmi_s {
	bfa_sm_t        sm;		/*  state machine */
	struct bfa_timer_s timer;
	struct bfa_fcs_lport_ms_s *ms;	/*  parent ms */
	struct bfa_fcxp_s *fcxp;
	struct bfa_fcxp_wqe_s fcxp_wqe;
	u8	retry_cnt;	/*  retry count */
	u8	rsvd[3];
};


struct bfa_fcs_lport_ms_s {
	bfa_sm_t        sm;		/*  state machine */
	struct bfa_timer_s timer;
	struct bfa_fcs_lport_s *port;	/*  parent port */
	struct bfa_fcxp_s *fcxp;
	struct bfa_fcxp_wqe_s fcxp_wqe;
	struct bfa_fcs_lport_fdmi_s fdmi;	/*  FDMI component of MS */
	u8         retry_cnt;	/*  retry count */
	u8	rsvd[3];
};


struct bfa_fcs_lport_fab_s {
	struct bfa_fcs_lport_ns_s ns;	/*  NS component of port */
	struct bfa_fcs_lport_scn_s scn;	/*  scn component of port */
	struct bfa_fcs_lport_ms_s ms;	/*  MS component of port */
};

#define	MAX_ALPA_COUNT	127

struct bfa_fcs_lport_loop_s {
	u8	num_alpa;	/*  Num of ALPA entries in the map */
	u8	alpabm_valid;	/* alpa bitmap valid or not (1 or 0) */
	u8	alpa_pos_map[MAX_ALPA_COUNT]; /*  ALPA Positional Map */
	struct bfa_fcs_lport_s *port;	/*  parent port */
};

struct bfa_fcs_lport_n2n_s {
	u32        rsvd;
	__be16     reply_oxid;	/*  ox_id from the req flogi to be
					 *used in flogi acc */
	wwn_t           rem_port_wwn;	/*  Attached port's wwn */
};


union bfa_fcs_lport_topo_u {
	struct bfa_fcs_lport_fab_s pfab;
	struct bfa_fcs_lport_loop_s ploop;
	struct bfa_fcs_lport_n2n_s pn2n;
};


struct bfa_fcs_lport_s {
	struct list_head         qe;	/*  used by port/vport */
	bfa_sm_t               sm;	/*  state machine */
	struct bfa_fcs_fabric_s *fabric;	/*  parent fabric */
	struct bfa_lport_cfg_s  port_cfg;	/*  port configuration */
	struct bfa_timer_s link_timer;	/*  timer for link offline */
	u32        pid:24;	/*  FC address */
	u8         lp_tag;		/*  lport tag */
	u16        num_rports;	/*  Num of r-ports */
	struct list_head         rport_q; /*  queue of discovered r-ports */
	struct bfa_fcs_s *fcs;	/*  FCS instance */
	union bfa_fcs_lport_topo_u port_topo;	/*  fabric/loop/n2n details */
	struct bfad_port_s *bfad_port;	/*  driver peer instance */
	struct bfa_fcs_vport_s *vport;	/*  NULL for base ports */
	struct bfa_fcxp_s *fcxp;
	struct bfa_fcxp_wqe_s fcxp_wqe;
	struct bfa_lport_stats_s stats;
	struct bfa_wc_s        wc;	/*  waiting counter for events */
};
#define BFA_FCS_GET_HAL_FROM_PORT(port)  (port->fcs->bfa)
#define BFA_FCS_GET_NS_FROM_PORT(port)  (&port->port_topo.pfab.ns)
#define BFA_FCS_GET_SCN_FROM_PORT(port)  (&port->port_topo.pfab.scn)
#define BFA_FCS_GET_MS_FROM_PORT(port)  (&port->port_topo.pfab.ms)
#define BFA_FCS_GET_FDMI_FROM_PORT(port)  (&port->port_topo.pfab.ms.fdmi)
#define	BFA_FCS_VPORT_IS_INITIATOR_MODE(port) \
		(port->port_cfg.roles & BFA_LPORT_ROLE_FCP_IM)

/*
 * forward declaration
 */
struct bfad_vf_s;

enum bfa_fcs_fabric_type {
	BFA_FCS_FABRIC_UNKNOWN = 0,
	BFA_FCS_FABRIC_SWITCHED = 1,
	BFA_FCS_FABRIC_N2N = 2,
	BFA_FCS_FABRIC_LOOP = 3,
};


struct bfa_fcs_fabric_s {
	struct list_head   qe;		/*  queue element */
	bfa_sm_t	 sm;		/*  state machine */
	struct bfa_fcs_s *fcs;		/*  FCS instance */
	struct bfa_fcs_lport_s  bport;	/*  base logical port */
	enum bfa_fcs_fabric_type fab_type; /*  fabric type */
	enum bfa_port_type oper_type;	/*  current link topology */
	u8         is_vf;		/*  is virtual fabric? */
	u8         is_npiv;	/*  is NPIV supported ? */
	u8         is_auth;	/*  is Security/Auth supported ? */
	u16        bb_credit;	/*  BB credit from fabric */
	u16        vf_id;		/*  virtual fabric ID */
	u16        num_vports;	/*  num vports */
	u16        rsvd;
	struct list_head         vport_q;	/*  queue of virtual ports */
	struct list_head         vf_q;	/*  queue of virtual fabrics */
	struct bfad_vf_s      *vf_drv;	/*  driver vf structure */
	struct bfa_timer_s link_timer;	/*  Link Failure timer. Vport */
	wwn_t           fabric_name;	/*  attached fabric name */
	bfa_boolean_t   auth_reqd;	/*  authentication required	*/
	struct bfa_timer_s delay_timer;	/*  delay timer		*/
	union {
		u16        swp_vfid;/*  switch port VF id		*/
	} event_arg;
	struct bfa_wc_s        wc;	/*  wait counter for delete	*/
	struct bfa_vf_stats_s	stats;	/*  fabric/vf stats		*/
	struct bfa_lps_s	*lps;	/*  lport login services	*/
	u8	fabric_ip_addr[BFA_FCS_FABRIC_IPADDR_SZ];
					/*  attached fabric's ip addr  */
	struct bfa_wc_s stop_wc;	/*  wait counter for stop */
};

#define bfa_fcs_fabric_npiv_capable(__f)    ((__f)->is_npiv)
#define bfa_fcs_fabric_is_switched(__f)			\
	((__f)->fab_type == BFA_FCS_FABRIC_SWITCHED)

/*
 *   The design calls for a single implementation of base fabric and vf.
 */
#define bfa_fcs_vf_t struct bfa_fcs_fabric_s

struct bfa_vf_event_s {
	u32        undefined;
};

struct bfa_fcs_s;
struct bfa_fcs_fabric_s;

/*
 * @todo : need to move to a global config file.
 * Maximum Rports supported per port (physical/logical).
 */
#define BFA_FCS_MAX_RPORTS_SUPP  256	/* @todo : tentative value */

#define bfa_fcs_lport_t struct bfa_fcs_lport_s

/*
 * Symbolic Name related defines
 *  Total bytes 255.
 *  Physical Port's symbolic name 128 bytes.
 *  For Vports, Vport's symbolic name is appended to the Physical port's
 *  Symbolic Name.
 *
 *  Physical Port's symbolic name Format : (Total 128 bytes)
 *  Adapter Model number/name : 16 bytes
 *  Driver Version     : 10 bytes
 *  Host Machine Name  : 30 bytes
 *  Host OS Info	   : 44 bytes
 *  Host OS PATCH Info : 16 bytes
 *  ( remaining 12 bytes reserved to be used for separator)
 */
#define BFA_FCS_PORT_SYMBNAME_SEPARATOR			" | "

#define BFA_FCS_PORT_SYMBNAME_MODEL_SZ			16
#define BFA_FCS_PORT_SYMBNAME_VERSION_SZ		10
#define BFA_FCS_PORT_SYMBNAME_MACHINENAME_SZ		30
#define BFA_FCS_PORT_SYMBNAME_OSINFO_SZ			44
#define BFA_FCS_PORT_SYMBNAME_OSPATCH_SZ		16

/*
 * Get FC port ID for a logical port.
 */
#define bfa_fcs_lport_get_fcid(_lport)	((_lport)->pid)
#define bfa_fcs_lport_get_pwwn(_lport)	((_lport)->port_cfg.pwwn)
#define bfa_fcs_lport_get_nwwn(_lport)	((_lport)->port_cfg.nwwn)
#define bfa_fcs_lport_get_psym_name(_lport)	((_lport)->port_cfg.sym_name)
#define bfa_fcs_lport_get_nsym_name(_lport) ((_lport)->port_cfg.node_sym_name)
#define bfa_fcs_lport_is_initiator(_lport)			\
	((_lport)->port_cfg.roles & BFA_LPORT_ROLE_FCP_IM)
#define bfa_fcs_lport_get_nrports(_lport)	\
	((_lport) ? (_lport)->num_rports : 0)

static inline struct bfad_port_s *
bfa_fcs_lport_get_drvport(struct bfa_fcs_lport_s *port)
{
	return port->bfad_port;
}

#define bfa_fcs_lport_get_opertype(_lport)	((_lport)->fabric->oper_type)
#define bfa_fcs_lport_get_fabric_name(_lport)	((_lport)->fabric->fabric_name)
#define bfa_fcs_lport_get_fabric_ipaddr(_lport)		\
		((_lport)->fabric->fabric_ip_addr)

/*
 * bfa fcs port public functions
 */

bfa_boolean_t   bfa_fcs_lport_is_online(struct bfa_fcs_lport_s *port);
struct bfa_fcs_lport_s *bfa_fcs_get_base_port(struct bfa_fcs_s *fcs);
void bfa_fcs_lport_get_rport_quals(struct bfa_fcs_lport_s *port,
			struct bfa_rport_qualifier_s rport[], int *nrports);
wwn_t bfa_fcs_lport_get_rport(struct bfa_fcs_lport_s *port, wwn_t wwn,
			      int index, int nrports, bfa_boolean_t bwwn);

struct bfa_fcs_lport_s *bfa_fcs_lookup_port(struct bfa_fcs_s *fcs,
					    u16 vf_id, wwn_t lpwwn);

void bfa_fcs_lport_set_symname(struct bfa_fcs_lport_s *port, char *symname);
void bfa_fcs_lport_get_info(struct bfa_fcs_lport_s *port,
			    struct bfa_lport_info_s *port_info);
void bfa_fcs_lport_get_attr(struct bfa_fcs_lport_s *port,
			    struct bfa_lport_attr_s *port_attr);
void bfa_fcs_lport_get_stats(struct bfa_fcs_lport_s *fcs_port,
			     struct bfa_lport_stats_s *port_stats);
void bfa_fcs_lport_clear_stats(struct bfa_fcs_lport_s *fcs_port);
enum bfa_port_speed bfa_fcs_lport_get_rport_max_speed(
			struct bfa_fcs_lport_s *port);

/* MS FCS routines */
void bfa_fcs_lport_ms_init(struct bfa_fcs_lport_s *port);
void bfa_fcs_lport_ms_offline(struct bfa_fcs_lport_s *port);
void bfa_fcs_lport_ms_online(struct bfa_fcs_lport_s *port);
void bfa_fcs_lport_ms_fabric_rscn(struct bfa_fcs_lport_s *port);

/* FDMI FCS routines */
void bfa_fcs_lport_fdmi_init(struct bfa_fcs_lport_ms_s *ms);
void bfa_fcs_lport_fdmi_offline(struct bfa_fcs_lport_ms_s *ms);
void bfa_fcs_lport_fdmi_online(struct bfa_fcs_lport_ms_s *ms);
void bfa_fcs_lport_uf_recv(struct bfa_fcs_lport_s *lport, struct fchs_s *fchs,
				     u16 len);
void bfa_fcs_lport_attach(struct bfa_fcs_lport_s *lport, struct bfa_fcs_s *fcs,
			u16 vf_id, struct bfa_fcs_vport_s *vport);
void bfa_fcs_lport_init(struct bfa_fcs_lport_s *lport,
				struct bfa_lport_cfg_s *port_cfg);
void            bfa_fcs_lport_online(struct bfa_fcs_lport_s *port);
void            bfa_fcs_lport_offline(struct bfa_fcs_lport_s *port);
void            bfa_fcs_lport_delete(struct bfa_fcs_lport_s *port);
void		bfa_fcs_lport_stop(struct bfa_fcs_lport_s *port);
struct bfa_fcs_rport_s *bfa_fcs_lport_get_rport_by_pid(
		struct bfa_fcs_lport_s *port, u32 pid);
struct bfa_fcs_rport_s *bfa_fcs_lport_get_rport_by_old_pid(
		struct bfa_fcs_lport_s *port, u32 pid);
struct bfa_fcs_rport_s *bfa_fcs_lport_get_rport_by_pwwn(
		struct bfa_fcs_lport_s *port, wwn_t pwwn);
struct bfa_fcs_rport_s *bfa_fcs_lport_get_rport_by_nwwn(
		struct bfa_fcs_lport_s *port, wwn_t nwwn);
struct bfa_fcs_rport_s *bfa_fcs_lport_get_rport_by_qualifier(
		struct bfa_fcs_lport_s *port, wwn_t pwwn, u32 pid);
void            bfa_fcs_lport_add_rport(struct bfa_fcs_lport_s *port,
				       struct bfa_fcs_rport_s *rport);
void            bfa_fcs_lport_del_rport(struct bfa_fcs_lport_s *port,
				       struct bfa_fcs_rport_s *rport);
void            bfa_fcs_lport_ns_init(struct bfa_fcs_lport_s *vport);
void            bfa_fcs_lport_ns_offline(struct bfa_fcs_lport_s *vport);
void            bfa_fcs_lport_ns_online(struct bfa_fcs_lport_s *vport);
void            bfa_fcs_lport_ns_query(struct bfa_fcs_lport_s *port);
void		bfa_fcs_lport_ns_util_send_rspn_id(void *cbarg,
				struct bfa_fcxp_s *fcxp_alloced);
void            bfa_fcs_lport_scn_init(struct bfa_fcs_lport_s *vport);
void            bfa_fcs_lport_scn_offline(struct bfa_fcs_lport_s *vport);
void            bfa_fcs_lport_fab_scn_online(struct bfa_fcs_lport_s *vport);
void            bfa_fcs_lport_scn_process_rscn(struct bfa_fcs_lport_s *port,
					      struct fchs_s *rx_frame, u32 len);
void		bfa_fcs_lport_lip_scn_online(bfa_fcs_lport_t *port);

struct bfa_fcs_vport_s {
	struct list_head		qe;		/*  queue elem	*/
	bfa_sm_t		sm;		/*  state machine	*/
	bfa_fcs_lport_t		lport;		/*  logical port	*/
	struct bfa_timer_s	timer;
	struct bfad_vport_s	*vport_drv;	/*  Driver private	*/
	struct bfa_vport_stats_s vport_stats;	/*  vport statistics	*/
	struct bfa_lps_s	*lps;		/*  Lport login service*/
	int			fdisc_retries;
};

#define bfa_fcs_vport_get_port(vport)			\
	((struct bfa_fcs_lport_s  *)(&vport->port))

/*
 * bfa fcs vport public functions
 */
bfa_status_t bfa_fcs_vport_create(struct bfa_fcs_vport_s *vport,
				  struct bfa_fcs_s *fcs, u16 vf_id,
				  struct bfa_lport_cfg_s *port_cfg,
				  struct bfad_vport_s *vport_drv);
bfa_status_t bfa_fcs_pbc_vport_create(struct bfa_fcs_vport_s *vport,
				      struct bfa_fcs_s *fcs, u16 vf_id,
				      struct bfa_lport_cfg_s *port_cfg,
				      struct bfad_vport_s *vport_drv);
bfa_boolean_t bfa_fcs_is_pbc_vport(struct bfa_fcs_vport_s *vport);
bfa_status_t bfa_fcs_vport_delete(struct bfa_fcs_vport_s *vport);
bfa_status_t bfa_fcs_vport_start(struct bfa_fcs_vport_s *vport);
bfa_status_t bfa_fcs_vport_stop(struct bfa_fcs_vport_s *vport);
void bfa_fcs_vport_get_attr(struct bfa_fcs_vport_s *vport,
			    struct bfa_vport_attr_s *vport_attr);
struct bfa_fcs_vport_s *bfa_fcs_vport_lookup(struct bfa_fcs_s *fcs,
					     u16 vf_id, wwn_t vpwwn);
void bfa_fcs_vport_cleanup(struct bfa_fcs_vport_s *vport);
void bfa_fcs_vport_online(struct bfa_fcs_vport_s *vport);
void bfa_fcs_vport_offline(struct bfa_fcs_vport_s *vport);
void bfa_fcs_vport_delete_comp(struct bfa_fcs_vport_s *vport);
void bfa_fcs_vport_fcs_delete(struct bfa_fcs_vport_s *vport);
void bfa_fcs_vport_fcs_stop(struct bfa_fcs_vport_s *vport);
void bfa_fcs_vport_stop_comp(struct bfa_fcs_vport_s *vport);

#define BFA_FCS_RPORT_DEF_DEL_TIMEOUT	90	/* in secs */
#define BFA_FCS_RPORT_MAX_RETRIES	(5)

/*
 * forward declarations
 */
struct bfad_rport_s;

struct bfa_fcs_itnim_s;
struct bfa_fcs_tin_s;
struct bfa_fcs_iprp_s;

/* Rport Features (RPF) */
struct bfa_fcs_rpf_s {
	bfa_sm_t	sm;	/*  state machine */
	struct bfa_fcs_rport_s *rport;	/*  parent rport */
	struct bfa_timer_s	timer;	/*  general purpose timer */
	struct bfa_fcxp_s	*fcxp;	/*  FCXP needed for discarding */
	struct bfa_fcxp_wqe_s	fcxp_wqe; /*  fcxp wait queue element */
	int	rpsc_retries;	/*  max RPSC retry attempts */
	enum bfa_port_speed	rpsc_speed;
	/*  Current Speed from RPSC. O if RPSC fails */
	enum bfa_port_speed	assigned_speed;
	/*
	 * Speed assigned by the user.  will be used if RPSC is
	 * not supported by the rport.
	 */
};

struct bfa_fcs_rport_s {
	struct list_head	qe;	/*  used by port/vport */
	struct bfa_fcs_lport_s *port;	/*  parent FCS port */
	struct bfa_fcs_s	*fcs;	/*  fcs instance */
	struct bfad_rport_s	*rp_drv;	/*  driver peer instance */
	u32	pid;	/*  port ID of rport */
	u32	old_pid;	/* PID before rport goes offline */
	u16	maxfrsize;	/*  maximum frame size */
	__be16	reply_oxid;	/*  OX_ID of inbound requests */
	enum fc_cos	fc_cos;	/*  FC classes of service supp */
	bfa_boolean_t	cisc;	/*  CISC capable device */
	bfa_boolean_t	prlo;	/*  processing prlo or LOGO */
	bfa_boolean_t	plogi_pending;	/* Rx Plogi Pending */
	wwn_t	pwwn;	/*  port wwn of rport */
	wwn_t	nwwn;	/*  node wwn of rport */
	struct bfa_rport_symname_s psym_name; /*  port symbolic name  */
	bfa_sm_t	sm;		/*  state machine */
	struct bfa_timer_s timer;	/*  general purpose timer */
	struct bfa_fcs_itnim_s *itnim;	/*  ITN initiator mode role */
	struct bfa_fcs_tin_s *tin;	/*  ITN initiator mode role */
	struct bfa_fcs_iprp_s *iprp;	/*  IP/FC role */
	struct bfa_rport_s *bfa_rport;	/*  BFA Rport */
	struct bfa_fcxp_s *fcxp;	/*  FCXP needed for discarding */
	int	plogi_retries;	/*  max plogi retry attempts */
	int	ns_retries;	/*  max NS query retry attempts */
	struct bfa_fcxp_wqe_s	fcxp_wqe; /*  fcxp wait queue element */
	struct bfa_rport_stats_s stats;	/*  rport stats */
	enum bfa_rport_function	scsi_function;  /*  Initiator/Target */
	struct bfa_fcs_rpf_s rpf;	/* Rport features module */
	bfa_boolean_t   scn_online;	/* SCN online flag */
};

static inline struct bfa_rport_s *
bfa_fcs_rport_get_halrport(struct bfa_fcs_rport_s *rport)
{
	return rport->bfa_rport;
}

/*
 * bfa fcs rport API functions
 */
void bfa_fcs_rport_get_attr(struct bfa_fcs_rport_s *rport,
			struct bfa_rport_attr_s *attr);
struct bfa_fcs_rport_s *bfa_fcs_rport_lookup(struct bfa_fcs_lport_s *port,
					     wwn_t rpwwn);
struct bfa_fcs_rport_s *bfa_fcs_rport_lookup_by_nwwn(
	struct bfa_fcs_lport_s *port, wwn_t rnwwn);
void bfa_fcs_rport_set_del_timeout(u8 rport_tmo);
void bfa_fcs_rport_set_max_logins(u32 max_logins);
void bfa_fcs_rport_uf_recv(struct bfa_fcs_rport_s *rport,
	 struct fchs_s *fchs, u16 len);
void bfa_fcs_rport_scn(struct bfa_fcs_rport_s *rport);

struct bfa_fcs_rport_s *bfa_fcs_rport_create(struct bfa_fcs_lport_s *port,
	 u32 pid);
void bfa_fcs_rport_start(struct bfa_fcs_lport_s *port, struct fchs_s *rx_fchs,
			 struct fc_logi_s *plogi_rsp);
void bfa_fcs_rport_plogi_create(struct bfa_fcs_lport_s *port,
				struct fchs_s *rx_fchs,
				struct fc_logi_s *plogi);
void bfa_fcs_rport_plogi(struct bfa_fcs_rport_s *rport, struct fchs_s *fchs,
			 struct fc_logi_s *plogi);
void bfa_fcs_rport_prlo(struct bfa_fcs_rport_s *rport, __be16 ox_id);

void bfa_fcs_rport_itntm_ack(struct bfa_fcs_rport_s *rport);
void bfa_fcs_rport_fcptm_offline_done(struct bfa_fcs_rport_s *rport);
int  bfa_fcs_rport_get_state(struct bfa_fcs_rport_s *rport);
struct bfa_fcs_rport_s *bfa_fcs_rport_create_by_wwn(
			struct bfa_fcs_lport_s *port, wwn_t wwn);
void  bfa_fcs_rpf_init(struct bfa_fcs_rport_s *rport);
void  bfa_fcs_rpf_rport_online(struct bfa_fcs_rport_s *rport);
void  bfa_fcs_rpf_rport_offline(struct bfa_fcs_rport_s *rport);

/*
 * forward declarations
 */
struct bfad_itnim_s;

struct bfa_fcs_itnim_s {
	bfa_sm_t		sm;		/*  state machine */
	struct bfa_fcs_rport_s	*rport;		/*  parent remote rport  */
	struct bfad_itnim_s	*itnim_drv;	/*  driver peer instance */
	struct bfa_fcs_s	*fcs;		/*  fcs instance	*/
	struct bfa_timer_s	timer;		/*  timer functions	*/
	struct bfa_itnim_s	*bfa_itnim;	/*  BFA itnim struct	*/
	u32		prli_retries;	/*  max prli retry attempts */
	bfa_boolean_t		seq_rec;	/*  seq recovery support */
	bfa_boolean_t		rec_support;	/*  REC supported	*/
	bfa_boolean_t		conf_comp;	/*  FCP_CONF	support */
	bfa_boolean_t		task_retry_id;	/*  task retry id supp	*/
	struct bfa_fcxp_wqe_s	fcxp_wqe;	/*  wait qelem for fcxp  */
	struct bfa_fcxp_s	*fcxp;		/*  FCXP in use	*/
	struct bfa_itnim_stats_s	stats;	/*  itn statistics	*/
};
#define bfa_fcs_fcxp_alloc(__fcs, __req)				\
	bfa_fcxp_req_rsp_alloc(NULL, (__fcs)->bfa, 0, 0,		\
			       NULL, NULL, NULL, NULL, __req)
#define bfa_fcs_fcxp_alloc_wait(__bfa, __wqe, __alloc_cbfn,		\
				__alloc_cbarg, __req)			\
	bfa_fcxp_req_rsp_alloc_wait(__bfa, __wqe, __alloc_cbfn,		\
		__alloc_cbarg, NULL, 0, 0, NULL, NULL, NULL, NULL, __req)

static inline struct bfad_port_s *
bfa_fcs_itnim_get_drvport(struct bfa_fcs_itnim_s *itnim)
{
	return itnim->rport->port->bfad_port;
}


static inline struct bfa_fcs_lport_s *
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


static inline	u32
bfa_fcs_itnim_get_maxfrsize(struct bfa_fcs_itnim_s *itnim)
{
	return itnim->rport->maxfrsize;
}


static inline	enum fc_cos
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

/*
 * bfa fcs FCP Initiator mode API functions
 */
void bfa_fcs_itnim_get_attr(struct bfa_fcs_itnim_s *itnim,
			    struct bfa_itnim_attr_s *attr);
void bfa_fcs_itnim_get_stats(struct bfa_fcs_itnim_s *itnim,
			     struct bfa_itnim_stats_s *stats);
struct bfa_fcs_itnim_s *bfa_fcs_itnim_lookup(struct bfa_fcs_lport_s *port,
					     wwn_t rpwwn);
bfa_status_t bfa_fcs_itnim_attr_get(struct bfa_fcs_lport_s *port, wwn_t rpwwn,
				    struct bfa_itnim_attr_s *attr);
bfa_status_t bfa_fcs_itnim_stats_get(struct bfa_fcs_lport_s *port, wwn_t rpwwn,
				     struct bfa_itnim_stats_s *stats);
bfa_status_t bfa_fcs_itnim_stats_clear(struct bfa_fcs_lport_s *port,
				       wwn_t rpwwn);
struct bfa_fcs_itnim_s *bfa_fcs_itnim_create(struct bfa_fcs_rport_s *rport);
void bfa_fcs_itnim_delete(struct bfa_fcs_itnim_s *itnim);
void bfa_fcs_itnim_rport_offline(struct bfa_fcs_itnim_s *itnim);
void bfa_fcs_itnim_brp_online(struct bfa_fcs_itnim_s *itnim);
bfa_status_t bfa_fcs_itnim_get_online_state(struct bfa_fcs_itnim_s *itnim);
void bfa_fcs_itnim_is_initiator(struct bfa_fcs_itnim_s *itnim);
void bfa_fcs_fcpim_uf_recv(struct bfa_fcs_itnim_s *itnim,
			struct fchs_s *fchs, u16 len);

#define BFA_FCS_FDMI_SUPP_SPEEDS_4G	(FDMI_TRANS_SPEED_1G  |	\
				FDMI_TRANS_SPEED_2G |		\
				FDMI_TRANS_SPEED_4G)

#define BFA_FCS_FDMI_SUPP_SPEEDS_8G	(FDMI_TRANS_SPEED_1G  |	\
				FDMI_TRANS_SPEED_2G |		\
				FDMI_TRANS_SPEED_4G |		\
				FDMI_TRANS_SPEED_8G)

#define BFA_FCS_FDMI_SUPP_SPEEDS_16G	(FDMI_TRANS_SPEED_2G  |	\
				FDMI_TRANS_SPEED_4G |		\
				FDMI_TRANS_SPEED_8G |		\
				FDMI_TRANS_SPEED_16G)

#define BFA_FCS_FDMI_SUPP_SPEEDS_10G	FDMI_TRANS_SPEED_10G

#define BFA_FCS_FDMI_VENDOR_INFO_LEN    8
#define BFA_FCS_FDMI_FC4_TYPE_LEN       32

/*
 * HBA Attribute Block : BFA internal representation. Note : Some variable
 * sizes have been trimmed to suit BFA For Ex : Model will be "QLogic ". Based
 * on this the size has been reduced to 16 bytes from the standard's 64 bytes.
 */
struct bfa_fcs_fdmi_hba_attr_s {
	wwn_t           node_name;
	u8         manufacturer[64];
	u8         serial_num[64];
	u8         model[16];
	u8         model_desc[128];
	u8         hw_version[8];
	u8         driver_version[BFA_VERSION_LEN];
	u8         option_rom_ver[BFA_VERSION_LEN];
	u8         fw_version[BFA_VERSION_LEN];
	u8         os_name[256];
	__be32        max_ct_pyld;
	struct      bfa_lport_symname_s node_sym_name;
	u8     vendor_info[BFA_FCS_FDMI_VENDOR_INFO_LEN];
	__be32    num_ports;
	wwn_t       fabric_name;
	u8     bios_ver[BFA_VERSION_LEN];
};

/*
 * Port Attribute Block
 */
struct bfa_fcs_fdmi_port_attr_s {
	u8         supp_fc4_types[BFA_FCS_FDMI_FC4_TYPE_LEN];
	__be32        supp_speed;	/* supported speed */
	__be32        curr_speed;	/* current Speed */
	__be32        max_frm_size;	/* max frame size */
	u8         os_device_name[256];	/* OS device Name */
	u8         host_name[256];	/* host name */
	wwn_t       port_name;
	wwn_t       node_name;
	struct      bfa_lport_symname_s port_sym_name;
	__be32    port_type;
	enum fc_cos    scos;
	wwn_t       port_fabric_name;
	u8     port_act_fc4_type[BFA_FCS_FDMI_FC4_TYPE_LEN];
	__be32    port_state;
	__be32    num_ports;
};

struct bfa_fcs_stats_s {
	struct {
		u32	untagged; /*  untagged receive frames */
		u32	tagged;	/*  tagged receive frames */
		u32	vfid_unknown;	/*  VF id is unknown */
	} uf;
};

struct bfa_fcs_driver_info_s {
	u8	 version[BFA_VERSION_LEN];		/* Driver Version */
	u8	 host_machine_name[BFA_FCS_OS_STR_LEN];
	u8	 host_os_name[BFA_FCS_OS_STR_LEN]; /* OS name and version */
	u8	 host_os_patch[BFA_FCS_OS_STR_LEN]; /* patch or service pack */
	u8	 os_device_name[BFA_FCS_OS_STR_LEN]; /* Driver Device Name */
};

struct bfa_fcs_s {
	struct bfa_s	  *bfa;	/*  corresponding BFA bfa instance */
	struct bfad_s	      *bfad; /*  corresponding BDA driver instance */
	struct bfa_trc_mod_s  *trcmod;	/*  tracing module */
	bfa_boolean_t	vf_enabled;	/*  VF mode is enabled */
	bfa_boolean_t	fdmi_enabled;	/*  FDMI is enabled */
	bfa_boolean_t min_cfg;		/* min cfg enabled/disabled */
	u16	port_vfid;	/*  port default VF ID */
	struct bfa_fcs_driver_info_s driver_info;
	struct bfa_fcs_fabric_s fabric; /*  base fabric state machine */
	struct bfa_fcs_stats_s	stats;	/*  FCS statistics */
	struct bfa_wc_s		wc;	/*  waiting counter */
	int			fcs_aen_seq;
	u32		num_rport_logins;
};

/*
 *  fcs_fabric_sm fabric state machine functions
 */

/*
 * Fabric state machine events
 */
enum bfa_fcs_fabric_event {
	BFA_FCS_FABRIC_SM_CREATE        = 1,    /*  create from driver        */
	BFA_FCS_FABRIC_SM_DELETE        = 2,    /*  delete from driver        */
	BFA_FCS_FABRIC_SM_LINK_DOWN     = 3,    /*  link down from port      */
	BFA_FCS_FABRIC_SM_LINK_UP       = 4,    /*  link up from port         */
	BFA_FCS_FABRIC_SM_CONT_OP       = 5,    /*  flogi/auth continue op   */
	BFA_FCS_FABRIC_SM_RETRY_OP      = 6,    /*  flogi/auth retry op      */
	BFA_FCS_FABRIC_SM_NO_FABRIC     = 7,    /*  from flogi/auth           */
	BFA_FCS_FABRIC_SM_PERF_EVFP     = 8,    /*  from flogi/auth           */
	BFA_FCS_FABRIC_SM_ISOLATE       = 9,    /*  from EVFP processing     */
	BFA_FCS_FABRIC_SM_NO_TAGGING    = 10,   /*  no VFT tagging from EVFP */
	BFA_FCS_FABRIC_SM_DELAYED       = 11,   /*  timeout delay event      */
	BFA_FCS_FABRIC_SM_AUTH_FAILED   = 12,   /*  auth failed       */
	BFA_FCS_FABRIC_SM_AUTH_SUCCESS  = 13,   /*  auth successful           */
	BFA_FCS_FABRIC_SM_DELCOMP       = 14,   /*  all vports deleted event */
	BFA_FCS_FABRIC_SM_LOOPBACK      = 15,   /*  Received our own FLOGI   */
	BFA_FCS_FABRIC_SM_START         = 16,   /*  from driver       */
	BFA_FCS_FABRIC_SM_STOP		= 17,	/*  Stop from driver	*/
	BFA_FCS_FABRIC_SM_STOPCOMP	= 18,	/*  Stop completion	*/
	BFA_FCS_FABRIC_SM_LOGOCOMP	= 19,	/*  FLOGO completion	*/
};

/*
 *  fcs_rport_sm FCS rport state machine events
 */

enum rport_event {
	RPSM_EVENT_PLOGI_SEND   = 1,    /*  new rport; start with PLOGI */
	RPSM_EVENT_PLOGI_RCVD   = 2,    /*  Inbound PLOGI from remote port */
	RPSM_EVENT_PLOGI_COMP   = 3,    /*  PLOGI completed to rport    */
	RPSM_EVENT_LOGO_RCVD    = 4,    /*  LOGO from remote device     */
	RPSM_EVENT_LOGO_IMP     = 5,    /*  implicit logo for SLER      */
	RPSM_EVENT_FCXP_SENT    = 6,    /*  Frame from has been sent    */
	RPSM_EVENT_DELETE       = 7,    /*  RPORT delete request        */
	RPSM_EVENT_FAB_SCN	= 8,    /*  state change notification   */
	RPSM_EVENT_ACCEPTED     = 9,    /*  Good response from remote device */
	RPSM_EVENT_FAILED       = 10,   /*  Request to rport failed.    */
	RPSM_EVENT_TIMEOUT      = 11,   /*  Rport SM timeout event      */
	RPSM_EVENT_HCB_ONLINE  = 12,    /*  BFA rport online callback   */
	RPSM_EVENT_HCB_OFFLINE = 13,    /*  BFA rport offline callback  */
	RPSM_EVENT_FC4_OFFLINE = 14,    /*  FC-4 offline complete       */
	RPSM_EVENT_ADDRESS_CHANGE = 15, /*  Rport's PID has changed     */
	RPSM_EVENT_ADDRESS_DISC = 16,   /*  Need to Discover rport's PID */
	RPSM_EVENT_PRLO_RCVD   = 17,    /*  PRLO from remote device     */
	RPSM_EVENT_PLOGI_RETRY = 18,    /*  Retry PLOGI continuously */
	RPSM_EVENT_SCN_OFFLINE = 19,	/* loop scn offline		*/
	RPSM_EVENT_SCN_ONLINE   = 20,	/* loop scn online		*/
	RPSM_EVENT_FC4_FCS_ONLINE = 21, /* FC-4 FCS online complete */
};

/*
 * fcs_itnim_sm FCS itnim state machine events
 */
enum bfa_fcs_itnim_event {
	BFA_FCS_ITNIM_SM_FCS_ONLINE = 1,        /*  rport online event */
	BFA_FCS_ITNIM_SM_OFFLINE = 2,   /*  rport offline */
	BFA_FCS_ITNIM_SM_FRMSENT = 3,   /*  prli frame is sent */
	BFA_FCS_ITNIM_SM_RSP_OK = 4,    /*  good response */
	BFA_FCS_ITNIM_SM_RSP_ERROR = 5, /*  error response */
	BFA_FCS_ITNIM_SM_TIMEOUT = 6,   /*  delay timeout */
	BFA_FCS_ITNIM_SM_HCB_OFFLINE = 7, /*  BFA online callback */
	BFA_FCS_ITNIM_SM_HCB_ONLINE = 8, /*  BFA offline callback */
	BFA_FCS_ITNIM_SM_INITIATOR = 9, /*  rport is initiator */
	BFA_FCS_ITNIM_SM_DELETE = 10,   /*  delete event from rport */
	BFA_FCS_ITNIM_SM_PRLO = 11,     /*  delete event from rport */
	BFA_FCS_ITNIM_SM_RSP_NOT_SUPP = 12, /* cmd not supported rsp */
	BFA_FCS_ITNIM_SM_HAL_ONLINE = 13, /* bfa rport online event */
};

/*
 * bfa fcs API functions
 */
void bfa_fcs_attach(struct bfa_fcs_s *fcs, struct bfa_s *bfa,
		    struct bfad_s *bfad,
		    bfa_boolean_t min_cfg);
void bfa_fcs_init(struct bfa_fcs_s *fcs);
void bfa_fcs_pbc_vport_init(struct bfa_fcs_s *fcs);
void bfa_fcs_update_cfg(struct bfa_fcs_s *fcs);
void bfa_fcs_driver_info_init(struct bfa_fcs_s *fcs,
			      struct bfa_fcs_driver_info_s *driver_info);
void bfa_fcs_exit(struct bfa_fcs_s *fcs);
void bfa_fcs_stop(struct bfa_fcs_s *fcs);

/*
 * bfa fcs vf public functions
 */
bfa_fcs_vf_t *bfa_fcs_vf_lookup(struct bfa_fcs_s *fcs, u16 vf_id);
void bfa_fcs_vf_get_ports(bfa_fcs_vf_t *vf, wwn_t vpwwn[], int *nports);

/*
 * fabric protected interface functions
 */
void bfa_fcs_fabric_modinit(struct bfa_fcs_s *fcs);
void bfa_fcs_fabric_link_up(struct bfa_fcs_fabric_s *fabric);
void bfa_fcs_fabric_link_down(struct bfa_fcs_fabric_s *fabric);
void bfa_fcs_fabric_addvport(struct bfa_fcs_fabric_s *fabric,
	struct bfa_fcs_vport_s *vport);
void bfa_fcs_fabric_delvport(struct bfa_fcs_fabric_s *fabric,
	struct bfa_fcs_vport_s *vport);
struct bfa_fcs_vport_s *bfa_fcs_fabric_vport_lookup(
		struct bfa_fcs_fabric_s *fabric, wwn_t pwwn);
void bfa_fcs_fabric_modstart(struct bfa_fcs_s *fcs);
void bfa_fcs_fabric_uf_recv(struct bfa_fcs_fabric_s *fabric,
		struct fchs_s *fchs, u16 len);
void	bfa_fcs_fabric_psymb_init(struct bfa_fcs_fabric_s *fabric);
void	bfa_fcs_fabric_nsymb_init(struct bfa_fcs_fabric_s *fabric);
void bfa_fcs_fabric_set_fabric_name(struct bfa_fcs_fabric_s *fabric,
	       wwn_t fabric_name);
u16 bfa_fcs_fabric_get_switch_oui(struct bfa_fcs_fabric_s *fabric);
void bfa_fcs_fabric_modstop(struct bfa_fcs_s *fcs);
void bfa_fcs_fabric_sm_online(struct bfa_fcs_fabric_s *fabric,
			enum bfa_fcs_fabric_event event);
void bfa_fcs_fabric_sm_loopback(struct bfa_fcs_fabric_s *fabric,
			enum bfa_fcs_fabric_event event);
void bfa_fcs_fabric_sm_auth_failed(struct bfa_fcs_fabric_s *fabric,
			enum bfa_fcs_fabric_event event);

/*
 * BFA FCS callback interfaces
 */

/*
 * fcb Main fcs callbacks
 */

struct bfad_port_s;
struct bfad_vf_s;
struct bfad_vport_s;
struct bfad_rport_s;

/*
 * lport callbacks
 */
struct bfad_port_s *bfa_fcb_lport_new(struct bfad_s *bfad,
				      struct bfa_fcs_lport_s *port,
				      enum bfa_lport_role roles,
				      struct bfad_vf_s *vf_drv,
				      struct bfad_vport_s *vp_drv);

/*
 * vport callbacks
 */
void bfa_fcb_pbc_vport_create(struct bfad_s *bfad, struct bfi_pbc_vport_s);

/*
 * rport callbacks
 */
bfa_status_t bfa_fcb_rport_alloc(struct bfad_s *bfad,
				 struct bfa_fcs_rport_s **rport,
				 struct bfad_rport_s **rport_drv);

/*
 * itnim callbacks
 */
int bfa_fcb_itnim_alloc(struct bfad_s *bfad, struct bfa_fcs_itnim_s **itnim,
			struct bfad_itnim_s **itnim_drv);
void bfa_fcb_itnim_free(struct bfad_s *bfad,
			struct bfad_itnim_s *itnim_drv);
void bfa_fcb_itnim_online(struct bfad_itnim_s *itnim_drv);
void bfa_fcb_itnim_offline(struct bfad_itnim_s *itnim_drv);

#endif /* __BFA_FCS_H__ */
