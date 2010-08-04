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

#ifndef __BFA_DEFS_IOCFC_H__
#define __BFA_DEFS_IOCFC_H__

#include <protocol/types.h>
#include <defs/bfa_defs_types.h>
#include <defs/bfa_defs_version.h>
#include <defs/bfa_defs_adapter.h>
#include <defs/bfa_defs_pm.h>

#define BFA_IOCFC_INTR_DELAY	1125
#define BFA_IOCFC_INTR_LATENCY	225
#define BFA_IOCFCOE_INTR_DELAY  25
#define BFA_IOCFCOE_INTR_LATENCY 5

/**
 * Interrupt coalescing configuration.
 */
struct bfa_iocfc_intr_attr_s {
	bfa_boolean_t	coalesce;	/*  enable/disable coalescing */
	u16	latency;	/*  latency in microseconds   */
	u16	delay;		/*  delay in microseconds     */
};

/**
 * IOC firmware configuraton
 */
struct bfa_iocfc_fwcfg_s {
	u16        num_fabrics;	/*  number of fabrics		*/
	u16        num_lports;	/*  number of local lports	*/
	u16        num_rports;	/*  number of remote ports	*/
	u16        num_ioim_reqs;	/*  number of IO reqs		*/
	u16        num_tskim_reqs;	/*  task management requests	*/
	u16        num_iotm_reqs;	/*  number of TM IO reqs	*/
	u16        num_tsktm_reqs;	/*  TM task management requests*/
	u16        num_fcxp_reqs;	/*  unassisted FC exchanges	*/
	u16        num_uf_bufs;	/*  unsolicited recv buffers	*/
	u8		num_cqs;
	u8		rsvd[5];
};

struct bfa_iocfc_drvcfg_s {
	u16        num_reqq_elems;	/*  number of req queue elements */
	u16        num_rspq_elems;	/*  number of rsp queue elements */
	u16        num_sgpgs;	/*  number of total SG pages	  */
	u16        num_sboot_tgts;	/*  number of SAN boot targets	  */
	u16        num_sboot_luns;	/*  number of SAN boot luns	  */
	u16	    ioc_recover;	/*  IOC recovery mode		  */
	u16	    min_cfg;	/*  minimum configuration	  */
	u16        path_tov;	/*  device path timeout	  */
	bfa_boolean_t   delay_comp; /*  delay completion of
							failed inflight IOs */
	u32		rsvd;
};
/**
 * IOC configuration
 */
struct bfa_iocfc_cfg_s {
	struct bfa_iocfc_fwcfg_s	fwcfg;	/*  firmware side config */
	struct bfa_iocfc_drvcfg_s	drvcfg;	/*  driver side config	  */
};

/**
 * IOC firmware IO stats
 */
struct bfa_fw_io_stats_s {
	u32	host_abort;		/*  IO aborted by host driver*/
	u32	host_cleanup;		/*  IO clean up by host driver */

	u32	fw_io_timeout;		/*  IOs timedout */
	u32	fw_frm_parse;		/*  frame parsed by f/w */
	u32	fw_frm_data;		/*  fcp_data frame parsed by f/w */
	u32	fw_frm_rsp;		/*  fcp_rsp frame parsed by f/w */
	u32	fw_frm_xfer_rdy;	/*  xfer_rdy frame parsed by f/w */
	u32	fw_frm_bls_acc;		/*  BLS ACC  frame parsed by f/w */
	u32	fw_frm_tgt_abort;	/*  target ABTS parsed by f/w */
	u32	fw_frm_unknown;		/*  unknown parsed by f/w */
	u32	fw_data_dma;		/*  f/w DMA'ed the data frame */
	u32	fw_frm_drop;		/*  f/w drop the frame */

	u32	rec_timeout;		/*  FW rec timed out */
	u32	error_rec;			/*  FW sending rec on
							* an error condition*/
	u32	wait_for_si;		/*  FW wait for SI */
	u32	rec_rsp_inval;		/*  REC rsp invalid */
	u32	seqr_io_abort;		/*  target does not know cmd so abort */
	u32	seqr_io_retry;		/*  SEQR failed so retry IO */

	u32	itn_cisc_upd_rsp;	/*  ITN cisc updated on fcp_rsp */
	u32	itn_cisc_upd_data;	/*  ITN cisc updated on fcp_data */
	u32	itn_cisc_upd_xfer_rdy;	/*  ITN cisc updated on fcp_data */

	u32	fcp_data_lost;		/*  fcp data lost */

	u32	ro_set_in_xfer_rdy;	/*  Target set RO in Xfer_rdy frame */
	u32	xfer_rdy_ooo_err;	/*  Out of order Xfer_rdy received */
	u32	xfer_rdy_unknown_err;	/*  unknown error in xfer_rdy frame */

	u32	io_abort_timeout;	/*  ABTS timedout  */
	u32	sler_initiated;		/*  SLER initiated */

	u32	unexp_fcp_rsp;		/*  fcp response in wrong state */

	u32	fcp_rsp_under_run;	/*  fcp rsp IO underrun */
	u32        fcp_rsp_under_run_wr;   /*  fcp rsp IO underrun for write */
	u32	fcp_rsp_under_run_err;	/*  fcp rsp IO underrun error */
	u32        fcp_rsp_resid_inval;    /*  invalid residue */
	u32	fcp_rsp_over_run;	/*  fcp rsp IO overrun */
	u32	fcp_rsp_over_run_err;	/*  fcp rsp IO overrun error */
	u32	fcp_rsp_proto_err;	/*  protocol error in fcp rsp */
	u32	fcp_rsp_sense_err;	/*  error in sense info in fcp rsp */
	u32	fcp_conf_req;		/*  FCP conf requested */

	u32	tgt_aborted_io;		/*  target initiated abort */

	u32	ioh_edtov_timeout_event;/*  IOH edtov timer popped */
	u32	ioh_fcp_rsp_excp_event;	/*  IOH FCP_RSP exception */
	u32	ioh_fcp_conf_event;	/*  IOH FCP_CONF */
	u32	ioh_mult_frm_rsp_event;	/*  IOH multi_frame FCP_RSP */
	u32	ioh_hit_class2_event;	/*  IOH hit class2 */
	u32	ioh_miss_other_event;	/*  IOH miss other */
	u32	ioh_seq_cnt_err_event;	/*  IOH seq cnt error */
	u32	ioh_len_err_event;		/*  IOH len error - fcp_dl !=
							* bytes xfered */
	u32	ioh_seq_len_err_event;	/*  IOH seq len error */
	u32	ioh_data_oor_event;	/*  Data out of range */
	u32	ioh_ro_ooo_event;	/*  Relative offset out of range */
	u32	ioh_cpu_owned_event;	/*  IOH hit -iost owned by f/w */
	u32	ioh_unexp_frame_event;	/*  unexpected frame recieved
						 *   count */
	u32	ioh_err_int;		/*  IOH error int during data-phase
						 *   for scsi write
						 */
};

/**
 * IOC port firmware stats
 */

struct bfa_fw_port_fpg_stats_s {
    u32    intr_evt;
    u32    intr;
    u32    intr_excess;
    u32    intr_cause0;
    u32    intr_other;
    u32    intr_other_ign;
    u32    sig_lost;
    u32    sig_regained;
    u32    sync_lost;
    u32    sync_to;
    u32    sync_regained;
    u32    div2_overflow;
    u32    div2_underflow;
    u32    efifo_overflow;
    u32    efifo_underflow;
    u32    idle_rx;
    u32    lrr_rx;
    u32    lr_rx;
    u32    ols_rx;
    u32    nos_rx;
    u32    lip_rx;
    u32    arbf0_rx;
    u32    mrk_rx;
    u32    const_mrk_rx;
    u32    prim_unknown;
    u32    rsvd;
};


struct bfa_fw_port_lksm_stats_s {
    u32    hwsm_success;       /*  hwsm state machine success          */
    u32    hwsm_fails;         /*  hwsm fails                          */
    u32    hwsm_wdtov;         /*  hwsm timed out                      */
    u32    swsm_success;       /*  swsm success                        */
    u32    swsm_fails;         /*  swsm fails                          */
    u32    swsm_wdtov;         /*  swsm timed out                      */
    u32    busybufs;           /*  link init failed due to busybuf     */
    u32    buf_waits;          /*  bufwait state entries               */
    u32    link_fails;         /*  link failures                       */
    u32    psp_errors;         /*  primitive sequence protocol errors  */
    u32    lr_unexp;           /*  No. of times LR rx-ed unexpectedly  */
    u32    lrr_unexp;          /*  No. of times LRR rx-ed unexpectedly */
    u32    lr_tx;              /*  No. of times LR tx started          */
    u32    lrr_tx;             /*  No. of times LRR tx started         */
    u32    ols_tx;             /*  No. of times OLS tx started         */
    u32    nos_tx;             /*  No. of times NOS tx started         */
};


struct bfa_fw_port_snsm_stats_s {
    u32    hwsm_success;       /*  Successful hwsm terminations        */
    u32    hwsm_fails;         /*  hwsm fail count                     */
    u32    hwsm_wdtov;         /*  hwsm timed out                      */
    u32    swsm_success;       /*  swsm success                        */
    u32    swsm_wdtov;         /*  swsm timed out                      */
    u32    error_resets;       /*  error resets initiated by upsm      */
    u32    sync_lost;          /*  Sync loss count                     */
    u32    sig_lost;           /*  Signal loss count                   */
};


struct bfa_fw_port_physm_stats_s {
    u32    module_inserts;     /*  Module insert count                 */
    u32    module_xtracts;     /*  Module extracts count               */
    u32    module_invalids;    /*  Invalid module inserted count       */
    u32    module_read_ign;    /*  Module validation status ignored    */
    u32    laser_faults;       /*  Laser fault count                   */
    u32    rsvd;
};


struct bfa_fw_fip_stats_s {
    u32    vlan_req;           /*  vlan discovery requests             */
    u32    vlan_notify;        /*  vlan notifications                  */
    u32    vlan_err;           /*  vlan response error                 */
    u32    vlan_timeouts;      /*  vlan disvoery timeouts              */
    u32    vlan_invalids;      /*  invalid vlan in discovery advert.   */
    u32    disc_req;           /*  Discovery solicit requests          */
    u32    disc_rsp;           /*  Discovery solicit response          */
    u32    disc_err;           /*  Discovery advt. parse errors        */
    u32    disc_unsol;         /*  Discovery unsolicited               */
    u32    disc_timeouts;      /*  Discovery timeouts                  */
    u32    disc_fcf_unavail;   /*  Discovery FCF Not Avail.            */
    u32    linksvc_unsupp;     /*  Unsupported link service req        */
    u32    linksvc_err;        /*  Parse error in link service req     */
    u32    logo_req;           /*  Number of FIP logos received        */
    u32    clrvlink_req;       /*  Clear virtual link req              */
    u32    op_unsupp;          /*  Unsupported FIP operation           */
    u32    untagged;           /*  Untagged frames (ignored)           */
    u32	   invalid_version;    /*!< Invalid FIP version           */
};


struct bfa_fw_lps_stats_s {
    u32    mac_invalids;       /*  Invalid mac assigned                */
    u32    rsvd;
};


struct bfa_fw_fcoe_stats_s {
    u32    cee_linkups;        /*  CEE link up count                   */
    u32    cee_linkdns;        /*  CEE link down count                 */
    u32    fip_linkups;        /*  FIP link up count                   */
    u32    fip_linkdns;        /*  FIP link up count                   */
    u32    fip_fails;          /*  FIP fail count                      */
    u32    mac_invalids;       /*  Invalid mac assigned                */
};

/**
 * IOC firmware FCoE port stats
 */
struct bfa_fw_fcoe_port_stats_s {
    struct bfa_fw_fcoe_stats_s  fcoe_stats;
    struct bfa_fw_fip_stats_s   fip_stats;
};

/**
 * IOC firmware FC port stats
 */
struct bfa_fw_fc_port_stats_s {
	struct bfa_fw_port_fpg_stats_s		fpg_stats;
	struct bfa_fw_port_physm_stats_s	physm_stats;
	struct bfa_fw_port_snsm_stats_s		snsm_stats;
	struct bfa_fw_port_lksm_stats_s		lksm_stats;
};

/**
 * IOC firmware FC port stats
 */
union bfa_fw_port_stats_s {
	struct bfa_fw_fc_port_stats_s	fc_stats;
	struct bfa_fw_fcoe_port_stats_s	fcoe_stats;
};

/**
 * IOC firmware stats
 */
struct bfa_fw_stats_s {
	struct bfa_fw_ioc_stats_s	ioc_stats;
	struct bfa_fw_io_stats_s	io_stats;
	union  bfa_fw_port_stats_s	port_stats;
};

/**
 * IOC statistics
 */
struct bfa_iocfc_stats_s {
	struct bfa_fw_stats_s 	fw_stats;	/*  firmware IOC stats      */
};

/**
 * IOC attributes returned in queries
 */
struct bfa_iocfc_attr_s {
	struct bfa_iocfc_cfg_s		config;		/*  IOCFC config   */
	struct bfa_iocfc_intr_attr_s	intr_attr;	/*  interrupt attr */
};

#define BFA_IOCFC_PATHTOV_MAX	60
#define BFA_IOCFC_QDEPTH_MAX	2000

#endif /* __BFA_DEFS_IOC_H__ */
