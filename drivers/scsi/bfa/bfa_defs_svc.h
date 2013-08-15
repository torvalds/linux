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

#ifndef __BFA_DEFS_SVC_H__
#define __BFA_DEFS_SVC_H__

#include "bfa_defs.h"
#include "bfa_fc.h"
#include "bfi.h"

#define BFA_IOCFC_INTR_DELAY	1125
#define BFA_IOCFC_INTR_LATENCY	225
#define BFA_IOCFCOE_INTR_DELAY	25
#define BFA_IOCFCOE_INTR_LATENCY 5

/*
 * Interrupt coalescing configuration.
 */
#pragma pack(1)
struct bfa_iocfc_intr_attr_s {
	u8		coalesce;	/*  enable/disable coalescing */
	u8		rsvd[3];
	__be16		latency;	/*  latency in microseconds   */
	__be16		delay;		/*  delay in microseconds     */
};

/*
 * IOC firmware configuraton
 */
struct bfa_iocfc_fwcfg_s {
	u16		num_fabrics;	/*  number of fabrics		*/
	u16		num_lports;	/*  number of local lports	*/
	u16		num_rports;	/*  number of remote ports	*/
	u16		num_ioim_reqs;	/*  number of IO reqs		*/
	u16		num_tskim_reqs;	/*  task management requests	*/
	u16		num_fwtio_reqs;	/* number of TM IO reqs in FW   */
	u16		num_fcxp_reqs;	/*  unassisted FC exchanges	*/
	u16		num_uf_bufs;	/*  unsolicited recv buffers	*/
	u8		num_cqs;
	u8		fw_tick_res;	/*  FW clock resolution in ms */
	u8		rsvd[6];
};
#pragma pack()

struct bfa_iocfc_drvcfg_s {
	u16		num_reqq_elems;	/*  number of req queue elements */
	u16		num_rspq_elems;	/*  number of rsp queue elements */
	u16		num_sgpgs;	/*  number of total SG pages	 */
	u16		num_sboot_tgts;	/*  number of SAN boot targets	 */
	u16		num_sboot_luns;	/*  number of SAN boot luns	 */
	u16		ioc_recover;	/*  IOC recovery mode		 */
	u16		min_cfg;	/*  minimum configuration	 */
	u16		path_tov;	/*  device path timeout		*/
	u16		num_tio_reqs;	/* number of TM IO reqs	*/
	u8		port_mode;
	u8		rsvd_a;
	bfa_boolean_t	delay_comp;	/* delay completion of failed
					 * inflight IOs */
	u16		num_ttsk_reqs;	 /* TM task management requests */
	u32		rsvd;
};

/*
 * IOC configuration
 */
struct bfa_iocfc_cfg_s {
	struct bfa_iocfc_fwcfg_s	fwcfg;	/*  firmware side config */
	struct bfa_iocfc_drvcfg_s	drvcfg;	/*  driver side config	  */
};

/*
 * IOC firmware IO stats
 */
struct bfa_fw_ioim_stats_s {
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
	u32	error_rec;		/*  FW sending rec on
					 *  an error condition*/
	u32	wait_for_si;		/*  FW wait for SI */
	u32	rec_rsp_inval;		/*  REC rsp invalid */
	u32     rec_rsp_xchg_comp;	/*  REC rsp xchg complete */
	u32     rec_rsp_rd_si_ownd;	/*  REC rsp read si owned */

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
	u32     fcp_rsp_under_run_wr;   /*  fcp rsp IO underrun for write */
	u32	fcp_rsp_under_run_err;	/*  fcp rsp IO underrun error */
	u32     fcp_rsp_resid_inval;    /*  invalid residue */
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
	u32	ioh_len_err_event;	/*  IOH len error - fcp_dl !=
					 *  bytes xfered */
	u32	ioh_seq_len_err_event;	/*  IOH seq len error */
	u32	ioh_data_oor_event;	/*  Data out of range */
	u32	ioh_ro_ooo_event;	/*  Relative offset out of range */
	u32	ioh_cpu_owned_event;	/*  IOH hit -iost owned by f/w */
	u32	ioh_unexp_frame_event;	/*  unexpected frame received
					 *  count */
	u32	ioh_err_int;		/*  IOH error int during data-phase
					 *  for scsi write */
};

struct bfa_fw_tio_stats_s {
	u32	tio_conf_proc;	    /* TIO CONF processed */
	u32	tio_conf_drop;      /* TIO CONF dropped */
	u32	tio_cleanup_req;    /* TIO cleanup requested */
	u32	tio_cleanup_comp;   /* TIO cleanup completed */
	u32	tio_abort_rsp;      /* TIO abort response */
	u32	tio_abort_rsp_comp; /* TIO abort rsp completed */
	u32	tio_abts_req;       /* TIO ABTS requested */
	u32	tio_abts_ack;       /* TIO ABTS ack-ed */
	u32	tio_abts_ack_nocomp;/* TIO ABTS ack-ed but not completed */
	u32	tio_abts_tmo;       /* TIO ABTS timeout */
	u32	tio_snsdata_dma;    /* TIO sense data DMA */
	u32	tio_rxwchan_wait;   /* TIO waiting for RX wait channel */
	u32	tio_rxwchan_avail;  /* TIO RX wait channel available */
	u32	tio_hit_bls;        /* TIO IOH BLS event */
	u32	tio_uf_recv;        /* TIO received UF */
	u32	tio_rd_invalid_sm;  /* TIO read reqst in wrong state machine */
	u32	tio_wr_invalid_sm;  /* TIO write reqst in wrong state machine */

	u32	ds_rxwchan_wait;    /* DS waiting for RX wait channel */
	u32	ds_rxwchan_avail;   /* DS RX wait channel available */
	u32	ds_unaligned_rd;    /* DS unaligned read */
	u32	ds_rdcomp_invalid_sm; /* DS read completed in wrong state
				       * machine */
	u32	ds_wrcomp_invalid_sm; /* DS write completed in wrong state
				       * machine */
	u32	ds_flush_req;       /* DS flush requested */
	u32	ds_flush_comp;      /* DS flush completed */
	u32	ds_xfrdy_exp;       /* DS XFER_RDY expired */
	u32	ds_seq_cnt_err;     /* DS seq cnt error */
	u32	ds_seq_len_err;     /* DS seq len error */
	u32	ds_data_oor;        /* DS data out of order */
	u32	ds_hit_bls;	    /* DS hit BLS */
	u32	ds_edtov_timer_exp; /* DS edtov expired */
	u32	ds_cpu_owned;       /* DS cpu owned */
	u32	ds_hit_class2;      /* DS hit class2 */
	u32	ds_length_err;      /* DS length error */
	u32	ds_ro_ooo_err;      /* DS relative offset out-of-order error */
	u32	ds_rectov_timer_exp;/* DS rectov expired */
	u32	ds_unexp_fr_err;    /* DS unexp frame error */
};

/*
 * IOC firmware IO stats
 */
struct bfa_fw_io_stats_s {
	struct bfa_fw_ioim_stats_s	ioim_stats;
	struct bfa_fw_tio_stats_s	tio_stats;
};

/*
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
	u32    arb_rx;
	u32    mrk_rx;
	u32    const_mrk_rx;
	u32    prim_unknown;
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
	u32    hwsm_lrr_rx;        /*  No. of times LRR rx-ed by HWSM      */
	u32    hwsm_lr_rx;         /*  No. of times LR rx-ed by HWSM       */
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
	u32    asn8g_attempts;	   /* SNSM HWSM at 8Gbps attempts	   */
	u32    adapt_success;	   /* SNSM adaptation success	*/
	u32    adapt_fails;	   /* SNSM adaptation failures */
	u32    adapt_ign_fails;	   /* SNSM adaptation failures ignored */
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
	u32    logo_req;           /*  FIP logos received                  */
	u32    clrvlink_req;       /*  Clear virtual link req              */
	u32    op_unsupp;          /*  Unsupported FIP operation           */
	u32    untagged;           /*  Untagged frames (ignored)           */
	u32    invalid_version;    /*  Invalid FIP version                 */
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

/*
 * IOC firmware FCoE port stats
 */
struct bfa_fw_fcoe_port_stats_s {
	struct bfa_fw_fcoe_stats_s		fcoe_stats;
	struct bfa_fw_fip_stats_s		fip_stats;
};

/**
 * @brief LPSM statistics
 */
struct bfa_fw_lpsm_stats_s {
	u32	cls_rx;		/* LPSM cls_rx			*/
	u32	cls_tx;		/* LPSM cls_tx			*/
	u32	arbf0_rx;	/* LPSM abrf0 rcvd		*/
	u32	arbf0_tx;	/* LPSM abrf0 xmit		*/
	u32	init_rx;	/* LPSM loop init start		*/
	u32	unexp_hwst;	/* LPSM unknown hw state	*/
	u32	unexp_frame;	/* LPSM unknown_frame		*/
	u32	unexp_prim;	/* LPSM unexpected primitive	*/
	u32	prev_alpa_unavail; /* LPSM prev alpa unavailable */
	u32	alpa_unavail;	/* LPSM alpa not available	*/
	u32	lip_rx;		/* LPSM lip rcvd		*/
	u32	lip_f7f7_rx;	/* LPSM lip f7f7 rcvd		*/
	u32	lip_f8_rx;	/* LPSM lip f8 rcvd		*/
	u32	lip_f8f7_rx;	/* LPSM lip f8f7 rcvd		*/
	u32	lip_other_rx;	/* LPSM lip other rcvd		*/
	u32	lip_tx;		/* LPSM lip xmit		*/
	u32	retry_tov;	/* LPSM retry TOV		*/
	u32	lip_tov;	/* LPSM LIP wait TOV		*/
	u32	idle_tov;	/* LPSM idle wait TOV		*/
	u32	arbf0_tov;	/* LPSM arbfo wait TOV		*/
	u32	stop_loop_tov;	/* LPSM stop loop wait TOV	*/
	u32	lixa_tov;	/* LPSM lisa wait TOV		*/
	u32	lixx_tov;	/* LPSM lilp/lirp wait TOV	*/
	u32	cls_tov;	/* LPSM cls wait TOV		*/
	u32	sler;		/* LPSM SLER recvd		*/
	u32	failed;		/* LPSM failed			*/
	u32	success;	/* LPSM online			*/
};

/*
 * IOC firmware FC uport stats
 */
struct bfa_fw_fc_uport_stats_s {
	struct bfa_fw_port_snsm_stats_s		snsm_stats;
	struct bfa_fw_port_lksm_stats_s		lksm_stats;
	struct bfa_fw_lpsm_stats_s		lpsm_stats;
};

/*
 * IOC firmware FC port stats
 */
union bfa_fw_fc_port_stats_s {
	struct bfa_fw_fc_uport_stats_s		fc_stats;
	struct bfa_fw_fcoe_port_stats_s		fcoe_stats;
};

/*
 * IOC firmware port stats
 */
struct bfa_fw_port_stats_s {
	struct bfa_fw_port_fpg_stats_s		fpg_stats;
	struct bfa_fw_port_physm_stats_s	physm_stats;
	union  bfa_fw_fc_port_stats_s		fc_port;
};

/*
 * fcxchg module statistics
 */
struct bfa_fw_fcxchg_stats_s {
	u32	ua_tag_inv;
	u32	ua_state_inv;
};

/*
 *  Trunk statistics
 */
struct bfa_fw_trunk_stats_s {
	u32 emt_recvd;		/*  Trunk EMT received		*/
	u32 emt_accepted;	/*  Trunk EMT Accepted		*/
	u32 emt_rejected;	/*  Trunk EMT rejected		*/
	u32 etp_recvd;		/*  Trunk ETP received		*/
	u32 etp_accepted;	/*  Trunk ETP Accepted		*/
	u32 etp_rejected;	/*  Trunk ETP rejected		*/
	u32 lr_recvd;		/*  Trunk LR received		*/
	u32 rsvd;		/*  padding for 64 bit alignment */
};

struct bfa_fw_aport_stats_s {
	u32 flogi_sent;		/*  Flogi sent			*/
	u32 flogi_acc_recvd;	/*  Flogi Acc received		*/
	u32 flogi_rjt_recvd;	/*  Flogi rejects received	*/
	u32 flogi_retries;	/*  Flogi retries		*/

	u32 elp_recvd;		/*  ELP received		*/
	u32 elp_accepted;	/*  ELP Accepted		*/
	u32 elp_rejected;	/*  ELP rejected		*/
	u32 elp_dropped;	/*  ELP dropped			*/

	u32 bbcr_lr_count;	/*!< BBCR Link Resets		*/
	u32 frame_lost_intrs;	/*!< BBCR Frame loss intrs	*/
	u32 rrdy_lost_intrs;	/*!< BBCR Rrdy loss intrs	*/

	u32 rsvd;
};

/*
 * IOCFC firmware stats
 */
struct bfa_fw_iocfc_stats_s {
	u32	cfg_reqs;	/*  cfg request */
	u32	updq_reqs;	/*  update queue request */
	u32	ic_reqs;	/*  interrupt coalesce reqs */
	u32	unknown_reqs;
	u32	set_intr_reqs;	/*  set interrupt reqs */
};

/*
 * IOC attributes returned in queries
 */
struct bfa_iocfc_attr_s {
	struct bfa_iocfc_cfg_s		config;		/*  IOCFC config   */
	struct bfa_iocfc_intr_attr_s	intr_attr;	/*  interrupt attr */
};

/*
 * Eth_sndrcv mod stats
 */
struct bfa_fw_eth_sndrcv_stats_s {
	u32	crc_err;
	u32	rsvd;		/*  64bit align    */
};

/*
 * CT MAC mod stats
 */
struct bfa_fw_mac_mod_stats_s {
	u32	mac_on;		/*  MAC got turned-on */
	u32	link_up;	/*  link-up */
	u32	signal_off;	/*  lost signal */
	u32	dfe_on;		/*  DFE on */
	u32	mac_reset;	/*  # of MAC reset to bring lnk up */
	u32	pcs_reset;	/*  # of PCS reset to bring lnk up */
	u32	loopback;	/*  MAC got into serdes loopback */
	u32	lb_mac_reset;
			/*  # of MAC reset to bring link up in loopback */
	u32	lb_pcs_reset;
			/*  # of PCS reset to bring link up in loopback */
	u32	rsvd;		/*  64bit align    */
};

/*
 * CT MOD stats
 */
struct bfa_fw_ct_mod_stats_s {
	u32	rxa_rds_undrun;	/*  RxA RDS underrun */
	u32	rad_bpc_ovfl;	/*  RAD BPC overflow */
	u32	rad_rlb_bpc_ovfl; /*  RAD RLB BPC overflow */
	u32	bpc_fcs_err;	/*  BPC FCS_ERR */
	u32	txa_tso_hdr;	/*  TxA TSO header too long */
	u32	rsvd;		/*  64bit align    */
};

/*
 * RDS mod stats
 */
struct bfa_fw_rds_stats_s {
	u32	no_fid_drop_err; /* RDS no fid drop error */
	u32	rsvd;		 /* 64bit align */
};

/*
 * IOC firmware stats
 */
struct bfa_fw_stats_s {
	struct bfa_fw_ioc_stats_s	ioc_stats;
	struct bfa_fw_iocfc_stats_s	iocfc_stats;
	struct bfa_fw_io_stats_s	io_stats;
	struct bfa_fw_port_stats_s	port_stats;
	struct bfa_fw_fcxchg_stats_s	fcxchg_stats;
	struct bfa_fw_lps_stats_s	lps_stats;
	struct bfa_fw_trunk_stats_s	trunk_stats;
	struct bfa_fw_aport_stats_s	aport_stats;
	struct bfa_fw_mac_mod_stats_s	macmod_stats;
	struct bfa_fw_ct_mod_stats_s	ctmod_stats;
	struct bfa_fw_eth_sndrcv_stats_s	ethsndrcv_stats;
	struct bfa_fw_rds_stats_s	rds_stats;
};

#define BFA_IOCFC_PATHTOV_MAX	60
#define BFA_IOCFC_QDEPTH_MAX	2000

/*
 * QoS states
 */
enum bfa_qos_state {
	BFA_QOS_DISABLED = 0,		/* QoS is disabled */
	BFA_QOS_ONLINE = 1,		/*  QoS is online */
	BFA_QOS_OFFLINE = 2,		/*  QoS is offline */
};

/*
 * QoS  Priority levels.
 */
enum bfa_qos_priority {
	BFA_QOS_UNKNOWN = 0,
	BFA_QOS_HIGH  = 1,	/*  QoS Priority Level High */
	BFA_QOS_MED  =  2,	/*  QoS Priority Level Medium */
	BFA_QOS_LOW  =  3,	/*  QoS Priority Level Low */
};

/*
 * QoS  bandwidth allocation for each priority level
 */
enum bfa_qos_bw_alloc {
	BFA_QOS_BW_HIGH  = 60,	/*  bandwidth allocation for High */
	BFA_QOS_BW_MED  =  30,	/*  bandwidth allocation for Medium */
	BFA_QOS_BW_LOW  =  10,	/*  bandwidth allocation for Low */
};
#pragma pack(1)

struct bfa_qos_bw_s {
	u8	qos_bw_set;
	u8	high;
	u8	med;
	u8	low;
};

/*
 * QoS attribute returned in QoS Query
 */
struct bfa_qos_attr_s {
	u8	state;		/*  QoS current state */
	u8	rsvd1[3];
	u32	total_bb_cr;	/*  Total BB Credits */
	struct bfa_qos_bw_s qos_bw;	/* QOS bw cfg */
	struct bfa_qos_bw_s qos_bw_op;	/* QOS bw operational */
};

enum bfa_bbcr_state {
	BFA_BBCR_DISABLED,	/*!< BBCR is disable */
	BFA_BBCR_ONLINE,	/*!< BBCR is online  */
	BFA_BBCR_OFFLINE,	/*!< BBCR is offline */
};

enum bfa_bbcr_err_reason {
	BFA_BBCR_ERR_REASON_NONE, /*!< Unknown */
	BFA_BBCR_ERR_REASON_SPEED_UNSUP, /*!< Port speed < max sup_speed */
	BFA_BBCR_ERR_REASON_PEER_UNSUP,	/*!< BBCR is disable on peer port */
	BFA_BBCR_ERR_REASON_NON_BRCD_SW, /*!< Connected to non BRCD switch */
	BFA_BBCR_ERR_REASON_FLOGI_RJT, /*!< Login rejected by the switch */
};

struct bfa_bbcr_attr_s {
	u8	state;
	u8	peer_bb_scn;
	u8	reason;
	u8	rsvd;
};

/*
 * These fields should be displayed only from the CLI.
 * There will be a separate BFAL API (get_qos_vc_attr ?)
 * to retrieve this.
 *
 */
#define  BFA_QOS_MAX_VC  16

struct bfa_qos_vc_info_s {
	u8 vc_credit;
	u8 borrow_credit;
	u8 priority;
	u8 resvd;
};

struct bfa_qos_vc_attr_s {
	u16  total_vc_count;                    /*  Total VC Count */
	u16  shared_credit;
	u32  elp_opmode_flags;
	struct bfa_qos_vc_info_s vc_info[BFA_QOS_MAX_VC];  /* as many as
							    * total_vc_count */
};

/*
 * QoS statistics
 */
struct bfa_qos_stats_s {
	u32	flogi_sent;		/*  QoS Flogi sent */
	u32	flogi_acc_recvd;	/*  QoS Flogi Acc received */
	u32	flogi_rjt_recvd;	/*  QoS Flogi rejects received */
	u32	flogi_retries;		/*  QoS Flogi retries */

	u32	elp_recvd;		/*  QoS ELP received */
	u32	elp_accepted;		/*  QoS ELP Accepted */
	u32	elp_rejected;		/*  QoS ELP rejected */
	u32	elp_dropped;		/*  QoS ELP dropped  */

	u32	qos_rscn_recvd;		/*  QoS RSCN received */
	u32	rsvd;			/* padding for 64 bit alignment */
};

/*
 * FCoE statistics
 */
struct bfa_fcoe_stats_s {
	u64	secs_reset;	/*  Seconds since stats reset	     */
	u64	cee_linkups;	/*  CEE link up			     */
	u64	cee_linkdns;	/*  CEE link down		     */
	u64	fip_linkups;	/*  FIP link up			     */
	u64	fip_linkdns;	/*  FIP link down		     */
	u64	fip_fails;	/*  FIP failures		     */
	u64	mac_invalids;	/*  Invalid mac assignments	     */
	u64	vlan_req;	/*  Vlan requests		     */
	u64	vlan_notify;	/*  Vlan notifications		     */
	u64	vlan_err;	/*  Vlan notification errors	     */
	u64	vlan_timeouts;	/*  Vlan request timeouts	     */
	u64	vlan_invalids;	/*  Vlan invalids		     */
	u64	disc_req;	/*  Discovery requests		     */
	u64	disc_rsp;	/*  Discovery responses		     */
	u64	disc_err;	/*  Discovery error frames	     */
	u64	disc_unsol;	/*  Discovery unsolicited	     */
	u64	disc_timeouts;	/*  Discovery timeouts		     */
	u64	disc_fcf_unavail; /*  Discovery FCF not avail	     */
	u64	linksvc_unsupp;	/*  FIP link service req unsupp	     */
	u64	linksvc_err;	/*  FIP link service req errors	     */
	u64	logo_req;	/*  FIP logos received		     */
	u64	clrvlink_req;	/*  Clear virtual link requests	     */
	u64	op_unsupp;	/*  FIP operation unsupp.	     */
	u64	untagged;	/*  FIP untagged frames		     */
	u64	txf_ucast;	/*  Tx FCoE unicast frames	     */
	u64	txf_ucast_vlan;	/*  Tx FCoE unicast vlan frames      */
	u64	txf_ucast_octets; /*  Tx FCoE unicast octets	     */
	u64	txf_mcast;	/*  Tx FCoE multicast frames	     */
	u64	txf_mcast_vlan;	/*  Tx FCoE multicast vlan frames    */
	u64	txf_mcast_octets; /*  Tx FCoE multicast octets	     */
	u64	txf_bcast;	/*  Tx FCoE broadcast frames	     */
	u64	txf_bcast_vlan;	/*  Tx FCoE broadcast vlan frames    */
	u64	txf_bcast_octets; /*  Tx FCoE broadcast octets	     */
	u64	txf_timeout;	  /*  Tx timeouts		     */
	u64	txf_parity_errors; /*  Transmit parity err	     */
	u64	txf_fid_parity_errors; /*  Transmit FID parity err   */
	u64	rxf_ucast_octets; /*  Rx FCoE unicast octets	     */
	u64	rxf_ucast;	/*  Rx FCoE unicast frames	     */
	u64	rxf_ucast_vlan;	/*  Rx FCoE unicast vlan frames	     */
	u64	rxf_mcast_octets; /*  Rx FCoE multicast octets	     */
	u64	rxf_mcast;	/*  Rx FCoE multicast frames	     */
	u64	rxf_mcast_vlan;	/*  Rx FCoE multicast vlan frames    */
	u64	rxf_bcast_octets; /*  Rx FCoE broadcast octets	     */
	u64	rxf_bcast;	/*  Rx FCoE broadcast frames	     */
	u64	rxf_bcast_vlan;	/*  Rx FCoE broadcast vlan frames    */
};

/*
 * QoS or FCoE stats (fcport stats excluding physical FC port stats)
 */
union bfa_fcport_stats_u {
	struct bfa_qos_stats_s	fcqos;
	struct bfa_fcoe_stats_s	fcoe;
};
#pragma pack()

struct bfa_fcpim_del_itn_stats_s {
	u32	del_itn_iocomp_aborted;	   /* Aborted IO requests	      */
	u32	del_itn_iocomp_timedout;   /* IO timeouts		      */
	u32	del_itn_iocom_sqer_needed; /* IO retry for SQ error recovery  */
	u32	del_itn_iocom_res_free;    /* Delayed freeing of IO resources */
	u32	del_itn_iocom_hostabrts;   /* Host IO abort requests	      */
	u32	del_itn_total_ios;	   /* Total IO count		      */
	u32	del_io_iocdowns;	   /* IO cleaned-up due to IOC down   */
	u32	del_tm_iocdowns;	   /* TM cleaned-up due to IOC down   */
};

struct bfa_itnim_iostats_s {

	u32	total_ios;		/*  Total IO Requests		*/
	u32	input_reqs;		/*  Data in-bound requests	*/
	u32	output_reqs;		/*  Data out-bound requests	*/
	u32	io_comps;		/*  Total IO Completions	*/
	u32	wr_throughput;		/*  Write data transferred in bytes */
	u32	rd_throughput;		/*  Read data transferred in bytes  */

	u32	iocomp_ok;		/*  Slowpath IO completions	*/
	u32	iocomp_underrun;	/*  IO underrun		*/
	u32	iocomp_overrun;		/*  IO overrun			*/
	u32	qwait;			/*  IO Request-Q wait		*/
	u32	qresumes;		/*  IO Request-Q wait done	*/
	u32	no_iotags;		/*  No free IO tag		*/
	u32	iocomp_timedout;	/*  IO timeouts		*/
	u32	iocom_nexus_abort;	/*  IO failure due to target offline */
	u32	iocom_proto_err;	/*  IO protocol errors		*/
	u32	iocom_dif_err;		/*  IO SBC-3 protection errors	*/

	u32	iocom_sqer_needed;	/*  fcp-2 error recovery failed	*/
	u32	iocom_res_free;		/*  Delayed freeing of IO tag	*/


	u32	io_aborts;		/*  Host IO abort requests	*/
	u32	iocom_hostabrts;	/*  Host IO abort completions	*/
	u32	io_cleanups;		/*  IO clean-up requests	*/
	u32	path_tov_expired;	/*  IO path tov expired	*/
	u32	iocomp_aborted;		/*  IO abort completions	*/
	u32	io_iocdowns;		/*  IO cleaned-up due to IOC down */
	u32	iocom_utags;		/*  IO comp with unknown tags	*/

	u32	io_tmaborts;		/*  Abort request due to TM command */
	u32	tm_io_comps;		/* Abort completion due to TM command */

	u32	creates;		/*  IT Nexus create requests	*/
	u32	fw_create;		/*  IT Nexus FW create requests	*/
	u32	create_comps;		/*  IT Nexus FW create completions */
	u32	onlines;		/*  IT Nexus onlines		*/
	u32	offlines;		/*  IT Nexus offlines		*/
	u32	fw_delete;		/*  IT Nexus FW delete requests	*/
	u32	delete_comps;		/*  IT Nexus FW delete completions */
	u32	deletes;		/*  IT Nexus delete requests	   */
	u32	sler_events;		/*  SLER events		*/
	u32	ioc_disabled;		/*  Num IOC disables		*/
	u32	cleanup_comps;		/*  IT Nexus cleanup completions    */

	u32	tm_cmnds;		/*  TM Requests		*/
	u32	tm_fw_rsps;		/*  TM Completions		*/
	u32	tm_success;		/*  TM initiated IO cleanup success */
	u32	tm_failures;		/*  TM initiated IO cleanup failure */
	u32	no_tskims;		/*  No free TM tag		*/
	u32	tm_qwait;		/*  TM Request-Q wait		*/
	u32	tm_qresumes;		/*  TM Request-Q wait done	*/

	u32	tm_iocdowns;		/*  TM cleaned-up due to IOC down   */
	u32	tm_cleanups;		/*  TM cleanup requests	*/
	u32	tm_cleanup_comps;	/*  TM cleanup completions	*/
	u32	rsvd[6];
};

/* Modify char* port_stt[] in bfal_port.c if a new state was added */
enum bfa_port_states {
	BFA_PORT_ST_UNINIT		= 1,
	BFA_PORT_ST_ENABLING_QWAIT	= 2,
	BFA_PORT_ST_ENABLING		= 3,
	BFA_PORT_ST_LINKDOWN		= 4,
	BFA_PORT_ST_LINKUP		= 5,
	BFA_PORT_ST_DISABLING_QWAIT	= 6,
	BFA_PORT_ST_DISABLING		= 7,
	BFA_PORT_ST_DISABLED		= 8,
	BFA_PORT_ST_STOPPED		= 9,
	BFA_PORT_ST_IOCDOWN		= 10,
	BFA_PORT_ST_IOCDIS		= 11,
	BFA_PORT_ST_FWMISMATCH		= 12,
	BFA_PORT_ST_PREBOOT_DISABLED	= 13,
	BFA_PORT_ST_TOGGLING_QWAIT	= 14,
	BFA_PORT_ST_FAA_MISCONFIG	= 15,
	BFA_PORT_ST_DPORT		= 16,
	BFA_PORT_ST_DDPORT		= 17,
	BFA_PORT_ST_MAX_STATE,
};

/*
 *	Port operational type (in sync with SNIA port type).
 */
enum bfa_port_type {
	BFA_PORT_TYPE_UNKNOWN	= 1,	/*  port type is unknown */
	BFA_PORT_TYPE_NPORT	= 5,	/*  P2P with switched fabric */
	BFA_PORT_TYPE_NLPORT	= 6,	/*  public loop */
	BFA_PORT_TYPE_LPORT	= 20,	/*  private loop */
	BFA_PORT_TYPE_P2P	= 21,	/*  P2P with no switched fabric */
	BFA_PORT_TYPE_VPORT	= 22,	/*  NPIV - virtual port */
};

/*
 *	Port topology setting. A port's topology and fabric login status
 *	determine its operational type.
 */
enum bfa_port_topology {
	BFA_PORT_TOPOLOGY_NONE = 0,	/*  No valid topology */
	BFA_PORT_TOPOLOGY_P2P_OLD_VER = 1, /* P2P def for older ver */
	BFA_PORT_TOPOLOGY_LOOP = 2,	/* LOOP topology */
	BFA_PORT_TOPOLOGY_AUTO_OLD_VER = 3, /* auto def for older ver */
	BFA_PORT_TOPOLOGY_AUTO = 4,	/* auto topology selection */
	BFA_PORT_TOPOLOGY_P2P = 5,	/* P2P only */
};

/*
 *	Physical port loopback types.
 */
enum bfa_port_opmode {
	BFA_PORT_OPMODE_NORMAL   = 0x00, /*  normal non-loopback mode */
	BFA_PORT_OPMODE_LB_INT   = 0x01, /*  internal loop back */
	BFA_PORT_OPMODE_LB_SLW   = 0x02, /*  serial link wrapback (serdes) */
	BFA_PORT_OPMODE_LB_EXT   = 0x04, /*  external loop back (serdes) */
	BFA_PORT_OPMODE_LB_CBL   = 0x08, /*  cabled loop back */
	BFA_PORT_OPMODE_LB_NLINT = 0x20, /*  NL_Port internal loopback */
};

#define BFA_PORT_OPMODE_LB_HARD(_mode)			\
	((_mode == BFA_PORT_OPMODE_LB_INT) ||		\
	(_mode == BFA_PORT_OPMODE_LB_SLW) ||		\
	(_mode == BFA_PORT_OPMODE_LB_EXT))

/*
 *	Port link state
 */
enum bfa_port_linkstate {
	BFA_PORT_LINKUP		= 1,	/*  Physical port/Trunk link up */
	BFA_PORT_LINKDOWN	= 2,	/*  Physical port/Trunk link down */
};

/*
 *	Port link state reason code
 */
enum bfa_port_linkstate_rsn {
	BFA_PORT_LINKSTATE_RSN_NONE		= 0,
	BFA_PORT_LINKSTATE_RSN_DISABLED		= 1,
	BFA_PORT_LINKSTATE_RSN_RX_NOS		= 2,
	BFA_PORT_LINKSTATE_RSN_RX_OLS		= 3,
	BFA_PORT_LINKSTATE_RSN_RX_LIP		= 4,
	BFA_PORT_LINKSTATE_RSN_RX_LIPF7		= 5,
	BFA_PORT_LINKSTATE_RSN_SFP_REMOVED	= 6,
	BFA_PORT_LINKSTATE_RSN_PORT_FAULT	= 7,
	BFA_PORT_LINKSTATE_RSN_RX_LOS		= 8,
	BFA_PORT_LINKSTATE_RSN_LOCAL_FAULT	= 9,
	BFA_PORT_LINKSTATE_RSN_REMOTE_FAULT	= 10,
	BFA_PORT_LINKSTATE_RSN_TIMEOUT		= 11,
	BFA_PORT_LINKSTATE_RSN_FAA_MISCONFIG	= 12,



	/* CEE related reason codes/errors */
	CEE_LLDP_INFO_AGED_OUT			= 20,
	CEE_LLDP_SHUTDOWN_TLV_RCVD		= 21,
	CEE_PEER_NOT_ADVERTISE_DCBX		= 22,
	CEE_PEER_NOT_ADVERTISE_PG		= 23,
	CEE_PEER_NOT_ADVERTISE_PFC		= 24,
	CEE_PEER_NOT_ADVERTISE_FCOE		= 25,
	CEE_PG_NOT_COMPATIBLE			= 26,
	CEE_PFC_NOT_COMPATIBLE			= 27,
	CEE_FCOE_NOT_COMPATIBLE			= 28,
	CEE_BAD_PG_RCVD				= 29,
	CEE_BAD_BW_RCVD				= 30,
	CEE_BAD_PFC_RCVD			= 31,
	CEE_BAD_APP_PRI_RCVD			= 32,
	CEE_FCOE_PRI_PFC_OFF			= 33,
	CEE_DUP_CONTROL_TLV_RCVD		= 34,
	CEE_DUP_FEAT_TLV_RCVD			= 35,
	CEE_APPLY_NEW_CFG			= 36, /* reason, not error */
	CEE_PROTOCOL_INIT			= 37, /* reason, not error */
	CEE_PHY_LINK_DOWN			= 38,
	CEE_LLS_FCOE_ABSENT			= 39,
	CEE_LLS_FCOE_DOWN			= 40,
	CEE_ISCSI_NOT_COMPATIBLE		= 41,
	CEE_ISCSI_PRI_PFC_OFF			= 42,
	CEE_ISCSI_PRI_OVERLAP_FCOE_PRI		= 43
};

#define MAX_LUN_MASK_CFG 16

/*
 * Initially flash content may be fff. On making LUN mask enable and disable
 * state chnage.  when report lun command is being processed it goes from
 * BFA_LUN_MASK_ACTIVE to BFA_LUN_MASK_FETCH and comes back to
 * BFA_LUN_MASK_ACTIVE.
 */
enum bfa_ioim_lun_mask_state_s {
	BFA_IOIM_LUN_MASK_INACTIVE = 0,
	BFA_IOIM_LUN_MASK_ACTIVE = 1,
	BFA_IOIM_LUN_MASK_FETCHED = 2,
};

enum bfa_lunmask_state_s {
	BFA_LUNMASK_DISABLED = 0x00,
	BFA_LUNMASK_ENABLED = 0x01,
	BFA_LUNMASK_MINCFG = 0x02,
	BFA_LUNMASK_UNINITIALIZED = 0xff,
};

/**
 * FEC states
 */
enum bfa_fec_state_s {
	BFA_FEC_ONLINE = 1,		/*!< FEC is online */
	BFA_FEC_OFFLINE = 2,		/*!< FEC is offline */
	BFA_FEC_OFFLINE_NOT_16G = 3,	/*!< FEC is offline (speed not 16Gig) */
};

#pragma pack(1)
/*
 * LUN mask configuration
 */
struct bfa_lun_mask_s {
	wwn_t		lp_wwn;
	wwn_t		rp_wwn;
	struct scsi_lun	lun;
	u8		ua;
	u8		rsvd[3];
	u16		rp_tag;
	u8		lp_tag;
	u8		state;
};

#define MAX_LUN_MASK_CFG 16
struct bfa_lunmask_cfg_s {
	u32	status;
	u32	rsvd;
	struct bfa_lun_mask_s	lun_list[MAX_LUN_MASK_CFG];
};

struct bfa_throttle_cfg_s {
	u16	is_valid;
	u16	value;
	u32	rsvd;
};

struct bfa_defs_fcpim_throttle_s {
	u16	max_value;
	u16	cur_value;
	u16	cfg_value;
	u16	rsvd;
};

#define BFA_BB_SCN_DEF 3
#define BFA_BB_SCN_MAX 0x0F

/*
 *      Physical port configuration
 */
struct bfa_port_cfg_s {
	u8	 topology;	/*  bfa_port_topology		*/
	u8	 speed;		/*  enum bfa_port_speed	*/
	u8	 trunked;	/*  trunked or not		*/
	u8	 qos_enabled;	/*  qos enabled or not		*/
	u8	 cfg_hardalpa;	/*  is hard alpa configured	*/
	u8	 hardalpa;	/*  configured hard alpa	*/
	__be16	 maxfrsize;	/*  maximum frame size		*/
	u8	 rx_bbcredit;	/*  receive buffer credits	*/
	u8	 tx_bbcredit;	/*  transmit buffer credits	*/
	u8	 ratelimit;	/*  ratelimit enabled or not	*/
	u8	 trl_def_speed;	/*  ratelimit default speed	*/
	u8	 bb_cr_enabled; /*!< Config state of BB_SCN	*/
	u8	 bb_scn;	/*!< BB_SCN value for FLOGI Exchg */
	u8	 faa_state;	/*  FAA enabled/disabled        */
	u8	 rsvd1;
	u16	 path_tov;	/*  device path timeout	*/
	u16	 q_depth;	/*  SCSI Queue depth		*/
	struct bfa_qos_bw_s qos_bw;	/* QOS bandwidth	*/
};
#pragma pack()

/*
 *	Port attribute values.
 */
struct bfa_port_attr_s {
	/*
	 * Static fields
	 */
	wwn_t			nwwn;		/*  node wwn */
	wwn_t			pwwn;		/*  port wwn */
	wwn_t			factorynwwn;	/*  factory node wwn */
	wwn_t			factorypwwn;	/*  factory port wwn */
	enum fc_cos		cos_supported;	/*  supported class of
						 *  services */
	u32			rsvd;
	struct fc_symname_s	port_symname;	/*  port symbolic name */
	enum bfa_port_speed	speed_supported; /* supported speeds */
	bfa_boolean_t		pbind_enabled;

	/*
	 * Configured values
	 */
	struct bfa_port_cfg_s	pport_cfg;	/*  pport cfg */

	/*
	 * Dynamic field - info from BFA
	 */
	enum bfa_port_states	port_state;	/*  current port state */
	enum bfa_port_speed	speed;		/*  current speed */
	enum bfa_port_topology	topology;	/*  current topology */
	bfa_boolean_t		beacon;		/*  current beacon status */
	bfa_boolean_t		link_e2e_beacon; /* link beacon is on */
	bfa_boolean_t		bbsc_op_status;	/* fc credit recovery oper
						 * state */
	enum bfa_fec_state_s	fec_state;	/*!< current FEC state */

	/*
	 * Dynamic field - info from FCS
	 */
	u32			pid;		/*  port ID */
	enum bfa_port_type	port_type;	/*  current topology */
	u32			loopback;	/*  external loopback */
	u32			authfail;	/*  auth fail state */

	/* FCoE specific  */
	u16			fcoe_vlan;
	u8			rsvd1[2];
};

/*
 *	      Port FCP mappings.
 */
struct bfa_port_fcpmap_s {
	char	osdevname[256];
	u32	bus;
	u32	target;
	u32	oslun;
	u32	fcid;
	wwn_t	nwwn;
	wwn_t	pwwn;
	u64	fcplun;
	char	luid[256];
};

/*
 *	      Port RNID info.
 */
struct bfa_port_rnid_s {
	wwn_t	  wwn;
	u32	  unittype;
	u32	  portid;
	u32	  attached_nodes_num;
	u16	  ip_version;
	u16	  udp_port;
	u8	  ipaddr[16];
	u16	  rsvd;
	u16	  topologydiscoveryflags;
};

#pragma pack(1)
struct bfa_fcport_fcf_s {
	wwn_t	name;		/*  FCF name		   */
	wwn_t	fabric_name;    /*  Fabric Name		   */
	u8	fipenabled;	/*  FIP enabled or not	   */
	u8	fipfailed;	/*  FIP failed or not	   */
	u8	resv[2];
	u8	pri;		/*  FCF priority	   */
	u8	version;	/*  FIP version used	   */
	u8	available;      /*  Available for login    */
	u8	fka_disabled;   /*  FKA is disabled	   */
	u8	maxsz_verified; /*  FCoE max size verified */
	u8	fc_map[3];      /*  FC map		   */
	__be16	vlan;		/*  FCoE vlan tag/priority */
	u32	fka_adv_per;    /*  FIP  ka advert. period */
	mac_t	mac;		/*  FCF mac		   */
};

/*
 *	Trunk states for BCU/BFAL
 */
enum bfa_trunk_state {
	BFA_TRUNK_DISABLED	= 0,	/*  Trunk is not configured	*/
	BFA_TRUNK_ONLINE	= 1,	/*  Trunk is online		*/
	BFA_TRUNK_OFFLINE	= 2,	/*  Trunk is offline		*/
};

/*
 *	VC attributes for trunked link
 */
struct bfa_trunk_vc_attr_s {
	u32 bb_credit;
	u32 elp_opmode_flags;
	u32 req_credit;
	u16 vc_credits[8];
};

struct bfa_fcport_loop_info_s {
	u8	myalpa;		/* alpa claimed */
	u8	alpabm_val;	/* alpa bitmap valid or not (1 or 0) */
	u8	resvd[6];
	struct fc_alpabm_s alpabm;	/* alpa bitmap */
};

/*
 *	Link state information
 */
struct bfa_port_link_s {
	u8	 linkstate;	/*  Link state bfa_port_linkstate */
	u8	 linkstate_rsn;	/*  bfa_port_linkstate_rsn_t */
	u8	 topology;	/*  P2P/LOOP bfa_port_topology */
	u8	 speed;		/*  Link speed (1/2/4/8 G) */
	u32	 linkstate_opt; /*  Linkstate optional data (debug) */
	u8	 trunked;	/*  Trunked or not (1 or 0) */
	u8	 fec_state;	/*!< State of FEC */
	u8	 resvd[6];
	struct bfa_qos_attr_s  qos_attr;   /* QoS Attributes */
	union {
		struct bfa_fcport_loop_info_s loop_info;
		struct bfa_bbcr_attr_s bbcr_attr;
		union {
			struct bfa_qos_vc_attr_s qos_vc_attr;
					/*  VC info from ELP */
			struct bfa_trunk_vc_attr_s trunk_vc_attr;
			struct bfa_fcport_fcf_s fcf;
					/*  FCF information (for FCoE) */
		} vc_fcf;
	} attr;
};
#pragma pack()

enum bfa_trunk_link_fctl {
	BFA_TRUNK_LINK_FCTL_NORMAL,
	BFA_TRUNK_LINK_FCTL_VC,
	BFA_TRUNK_LINK_FCTL_VC_QOS,
};

enum bfa_trunk_link_state {
	BFA_TRUNK_LINK_STATE_UP = 1,		/* link part of trunk */
	BFA_TRUNK_LINK_STATE_DN_LINKDN = 2,	/* physical link down */
	BFA_TRUNK_LINK_STATE_DN_GRP_MIS = 3,	/* trunk group different */
	BFA_TRUNK_LINK_STATE_DN_SPD_MIS = 4,	/* speed mismatch */
	BFA_TRUNK_LINK_STATE_DN_MODE_MIS = 5,	/* remote port not trunked */
};

#define BFA_TRUNK_MAX_PORTS	2
struct bfa_trunk_link_attr_s {
	wwn_t    trunk_wwn;
	enum bfa_trunk_link_fctl fctl;
	enum bfa_trunk_link_state link_state;
	enum bfa_port_speed	speed;
	u32 deskew;
};

struct bfa_trunk_attr_s {
	enum bfa_trunk_state	state;
	enum bfa_port_speed	speed;
	u32		port_id;
	u32		rsvd;
	struct bfa_trunk_link_attr_s link_attr[BFA_TRUNK_MAX_PORTS];
};

struct bfa_rport_hal_stats_s {
	u32        sm_un_cr;	    /*  uninit: create events      */
	u32        sm_un_unexp;	    /*  uninit: exception events   */
	u32        sm_cr_on;	    /*  created: online events     */
	u32        sm_cr_del;	    /*  created: delete events     */
	u32        sm_cr_hwf;	    /*  created: IOC down          */
	u32        sm_cr_unexp;	    /*  created: exception events  */
	u32        sm_fwc_rsp;	    /*  fw create: f/w responses   */
	u32        sm_fwc_del;	    /*  fw create: delete events   */
	u32        sm_fwc_off;	    /*  fw create: offline events  */
	u32        sm_fwc_hwf;	    /*  fw create: IOC down        */
	u32        sm_fwc_unexp;    /*  fw create: exception events*/
	u32        sm_on_off;	    /*  online: offline events     */
	u32        sm_on_del;	    /*  online: delete events      */
	u32        sm_on_hwf;	    /*  online: IOC down events    */
	u32        sm_on_unexp;	    /*  online: exception events   */
	u32        sm_fwd_rsp;	    /*  fw delete: fw responses    */
	u32        sm_fwd_del;	    /*  fw delete: delete events   */
	u32        sm_fwd_hwf;	    /*  fw delete: IOC down events */
	u32        sm_fwd_unexp;    /*  fw delete: exception events*/
	u32        sm_off_del;	    /*  offline: delete events     */
	u32        sm_off_on;	    /*  offline: online events     */
	u32        sm_off_hwf;	    /*  offline: IOC down events   */
	u32        sm_off_unexp;    /*  offline: exception events  */
	u32        sm_del_fwrsp;    /*  delete: fw responses       */
	u32        sm_del_hwf;	    /*  delete: IOC down events    */
	u32        sm_del_unexp;    /*  delete: exception events   */
	u32        sm_delp_fwrsp;   /*  delete pend: fw responses  */
	u32        sm_delp_hwf;	    /*  delete pend: IOC downs     */
	u32        sm_delp_unexp;   /*  delete pend: exceptions    */
	u32        sm_offp_fwrsp;   /*  off-pending: fw responses  */
	u32        sm_offp_del;	    /*  off-pending: deletes       */
	u32        sm_offp_hwf;	    /*  off-pending: IOC downs     */
	u32        sm_offp_unexp;   /*  off-pending: exceptions    */
	u32        sm_iocd_off;	    /*  IOC down: offline events   */
	u32        sm_iocd_del;	    /*  IOC down: delete events    */
	u32        sm_iocd_on;	    /*  IOC down: online events    */
	u32        sm_iocd_unexp;   /*  IOC down: exceptions       */
	u32        rsvd;
};
#pragma pack(1)
/*
 *  Rport's QoS attributes
 */
struct bfa_rport_qos_attr_s {
	u8		qos_priority;	/*  rport's QoS priority   */
	u8		rsvd[3];
	u32		qos_flow_id;	/*  QoS flow Id	 */
};
#pragma pack()

#define BFA_IOBUCKET_MAX 14

struct bfa_itnim_latency_s {
	u32 min[BFA_IOBUCKET_MAX];
	u32 max[BFA_IOBUCKET_MAX];
	u32 count[BFA_IOBUCKET_MAX];
	u32 avg[BFA_IOBUCKET_MAX];
};

struct bfa_itnim_ioprofile_s {
	u32 clock_res_mul;
	u32 clock_res_div;
	u32 index;
	u32 io_profile_start_time;	/*  IO profile start time	*/
	u32 iocomps[BFA_IOBUCKET_MAX];	/*  IO completed	*/
	struct bfa_itnim_latency_s io_latency;
};

/*
 *	vHBA port attribute values.
 */
struct bfa_vhba_attr_s {
	wwn_t	nwwn;       /* node wwn */
	wwn_t	pwwn;       /* port wwn */
	u32	pid;        /* port ID */
	bfa_boolean_t       io_profile; /* get it from fcpim mod */
	bfa_boolean_t       plog_enabled;   /* portlog is enabled */
	u16	path_tov;
	u8	rsvd[2];
};

/*
 * FC physical port statistics.
 */
struct bfa_port_fc_stats_s {
	u64     secs_reset;     /*  Seconds since stats is reset */
	u64     tx_frames;      /*  Tx frames                   */
	u64     tx_words;       /*  Tx words                    */
	u64     tx_lip;         /*  Tx LIP                      */
	u64	tx_lip_f7f7;	/*  Tx LIP_F7F7		*/
	u64	tx_lip_f8f7;	/*  Tx LIP_F8F7		*/
	u64	tx_arbf0;	/*  Tx ARB F0			*/
	u64     tx_nos;         /*  Tx NOS                      */
	u64     tx_ols;         /*  Tx OLS                      */
	u64     tx_lr;          /*  Tx LR                       */
	u64     tx_lrr;         /*  Tx LRR                      */
	u64     rx_frames;      /*  Rx frames                   */
	u64     rx_words;       /*  Rx words                    */
	u64     lip_count;      /*  Rx LIP                      */
	u64	rx_lip_f7f7;	/*  Rx LIP_F7F7		*/
	u64	rx_lip_f8f7;	/*  Rx LIP_F8F7		*/
	u64	rx_arbf0;	/*  Rx ARB F0			*/
	u64     nos_count;      /*  Rx NOS                      */
	u64     ols_count;      /*  Rx OLS                      */
	u64     lr_count;       /*  Rx LR                       */
	u64     lrr_count;      /*  Rx LRR                      */
	u64     invalid_crcs;   /*  Rx CRC err frames           */
	u64     invalid_crc_gd_eof; /*  Rx CRC err good EOF frames */
	u64     undersized_frm; /*  Rx undersized frames        */
	u64     oversized_frm;  /*  Rx oversized frames */
	u64     bad_eof_frm;    /*  Rx frames with bad EOF      */
	u64     error_frames;   /*  Errored frames              */
	u64     dropped_frames; /*  Dropped frames              */
	u64     link_failures;  /*  Link Failure (LF) count     */
	u64     loss_of_syncs;  /*  Loss of sync count          */
	u64     loss_of_signals; /*  Loss of signal count       */
	u64     primseq_errs;   /*  Primitive sequence protocol err. */
	u64     bad_os_count;   /*  Invalid ordered sets        */
	u64     err_enc_out;    /*  Encoding err nonframe_8b10b */
	u64     err_enc;        /*  Encoding err frame_8b10b    */
	u64	bbcr_frames_lost; /*!< BBCR Frames Lost */
	u64	bbcr_rrdys_lost; /*!< BBCR RRDYs Lost */
	u64	bbcr_link_resets; /*!< BBCR Link Resets */
	u64	bbcr_frame_lost_intrs; /*!< BBCR Frame loss intrs */
	u64	bbcr_rrdy_lost_intrs; /*!< BBCR Rrdy loss intrs */
	u64	loop_timeouts;	/*  Loop timeouts		*/
};

/*
 * Eth Physical Port statistics.
 */
struct bfa_port_eth_stats_s {
	u64     secs_reset;     /*  Seconds since stats is reset */
	u64     frame_64;       /*  Frames 64 bytes             */
	u64     frame_65_127;   /*  Frames 65-127 bytes */
	u64     frame_128_255;  /*  Frames 128-255 bytes        */
	u64     frame_256_511;  /*  Frames 256-511 bytes        */
	u64     frame_512_1023; /*  Frames 512-1023 bytes       */
	u64     frame_1024_1518; /*  Frames 1024-1518 bytes     */
	u64     frame_1519_1522; /*  Frames 1519-1522 bytes     */
	u64     tx_bytes;       /*  Tx bytes                    */
	u64     tx_packets;      /*  Tx packets         */
	u64     tx_mcast_packets; /*  Tx multicast packets      */
	u64     tx_bcast_packets; /*  Tx broadcast packets      */
	u64     tx_control_frame; /*  Tx control frame          */
	u64     tx_drop;        /*  Tx drops                    */
	u64     tx_jabber;      /*  Tx jabber                   */
	u64     tx_fcs_error;   /*  Tx FCS errors               */
	u64     tx_fragments;   /*  Tx fragments                */
	u64     rx_bytes;       /*  Rx bytes                    */
	u64     rx_packets;     /*  Rx packets                  */
	u64     rx_mcast_packets; /*  Rx multicast packets      */
	u64     rx_bcast_packets; /*  Rx broadcast packets      */
	u64     rx_control_frames; /*  Rx control frames        */
	u64     rx_unknown_opcode; /*  Rx unknown opcode        */
	u64     rx_drop;        /*  Rx drops                    */
	u64     rx_jabber;      /*  Rx jabber                   */
	u64     rx_fcs_error;   /*  Rx FCS errors               */
	u64     rx_alignment_error; /*  Rx alignment errors     */
	u64     rx_frame_length_error; /*  Rx frame len errors  */
	u64     rx_code_error;  /*  Rx code errors              */
	u64     rx_fragments;   /*  Rx fragments                */
	u64     rx_pause;       /*  Rx pause                    */
	u64     rx_zero_pause;  /*  Rx zero pause               */
	u64     tx_pause;       /*  Tx pause                    */
	u64     tx_zero_pause;  /*  Tx zero pause               */
	u64     rx_fcoe_pause;  /*  Rx FCoE pause               */
	u64     rx_fcoe_zero_pause; /*  Rx FCoE zero pause      */
	u64     tx_fcoe_pause;  /*  Tx FCoE pause               */
	u64     tx_fcoe_zero_pause; /*  Tx FCoE zero pause      */
	u64     rx_iscsi_pause; /*  Rx iSCSI pause              */
	u64     rx_iscsi_zero_pause; /*  Rx iSCSI zero pause    */
	u64     tx_iscsi_pause; /*  Tx iSCSI pause              */
	u64     tx_iscsi_zero_pause; /*  Tx iSCSI zero pause    */
};

/*
 *              Port statistics.
 */
union bfa_port_stats_u {
	struct bfa_port_fc_stats_s      fc;
	struct bfa_port_eth_stats_s     eth;
};

struct bfa_port_cfg_mode_s {
	u16		max_pf;
	u16		max_vf;
	enum bfa_mode_s	mode;
};

#pragma pack(1)

#define BFA_CEE_LLDP_MAX_STRING_LEN	(128)
#define BFA_CEE_DCBX_MAX_PRIORITY	(8)
#define BFA_CEE_DCBX_MAX_PGID		(8)

struct bfa_cee_lldp_str_s {
	u8	sub_type;
	u8	len;
	u8	rsvd[2];
	u8	value[BFA_CEE_LLDP_MAX_STRING_LEN];
};

struct bfa_cee_lldp_cfg_s {
	struct bfa_cee_lldp_str_s chassis_id;
	struct bfa_cee_lldp_str_s port_id;
	struct bfa_cee_lldp_str_s port_desc;
	struct bfa_cee_lldp_str_s sys_name;
	struct bfa_cee_lldp_str_s sys_desc;
	struct bfa_cee_lldp_str_s mgmt_addr;
	u16	time_to_live;
	u16	enabled_system_cap;
};

/* CEE/DCBX parameters */
struct bfa_cee_dcbx_cfg_s {
	u8	pgid[BFA_CEE_DCBX_MAX_PRIORITY];
	u8	pg_percentage[BFA_CEE_DCBX_MAX_PGID];
	u8	pfc_primap; /* bitmap of priorties with PFC enabled */
	u8	fcoe_primap; /* bitmap of priorities used for FcoE traffic */
	u8	iscsi_primap; /* bitmap of priorities used for iSCSI traffic */
	u8	dcbx_version; /* operating version:CEE or preCEE */
	u8	lls_fcoe; /* FCoE Logical Link Status */
	u8	lls_lan; /* LAN Logical Link Status */
	u8	rsvd[2];
};

/* CEE Query */
struct bfa_cee_attr_s {
	u8	cee_status;
	u8	error_reason;
	struct bfa_cee_lldp_cfg_s lldp_remote;
	struct bfa_cee_dcbx_cfg_s dcbx_remote;
	mac_t src_mac;
	u8	link_speed;
	u8	nw_priority;
	u8	filler[2];
};

/* LLDP/DCBX/CEE Statistics */
struct bfa_cee_stats_s {
	u32		lldp_tx_frames;		/* LLDP Tx Frames */
	u32		lldp_rx_frames;		/* LLDP Rx Frames */
	u32		lldp_rx_frames_invalid; /* LLDP Rx Frames invalid */
	u32		lldp_rx_frames_new;     /* LLDP Rx Frames new */
	u32		lldp_tlvs_unrecognized; /* LLDP Rx unrecog. TLVs */
	u32		lldp_rx_shutdown_tlvs;  /* LLDP Rx shutdown TLVs */
	u32		lldp_info_aged_out;     /* LLDP remote info aged */
	u32		dcbx_phylink_ups;       /* DCBX phy link ups */
	u32		dcbx_phylink_downs;     /* DCBX phy link downs */
	u32		dcbx_rx_tlvs;           /* DCBX Rx TLVs */
	u32		dcbx_rx_tlvs_invalid;   /* DCBX Rx TLVs invalid */
	u32		dcbx_control_tlv_error; /* DCBX control TLV errors */
	u32		dcbx_feature_tlv_error; /* DCBX feature TLV errors */
	u32		dcbx_cee_cfg_new;       /* DCBX new CEE cfg rcvd */
	u32		cee_status_down;        /* DCB status down */
	u32		cee_status_up;          /* DCB status up */
	u32		cee_hw_cfg_changed;     /* DCB hw cfg changed */
	u32		cee_rx_invalid_cfg;     /* DCB invalid cfg */
};

#pragma pack()

/*
 *			AEN related definitions
 */
#define BFAD_NL_VENDOR_ID (((u64)0x01 << SCSI_NL_VID_TYPE_SHIFT) \
			   | BFA_PCI_VENDOR_ID_BROCADE)

/* BFA remote port events */
enum bfa_rport_aen_event {
	BFA_RPORT_AEN_ONLINE     = 1,   /* RPort online event */
	BFA_RPORT_AEN_OFFLINE    = 2,   /* RPort offline event */
	BFA_RPORT_AEN_DISCONNECT = 3,   /* RPort disconnect event */
	BFA_RPORT_AEN_QOS_PRIO   = 4,   /* QOS priority change event */
	BFA_RPORT_AEN_QOS_FLOWID = 5,   /* QOS flow Id change event */
};

struct bfa_rport_aen_data_s {
	u16             vf_id;  /* vf_id of this logical port */
	u16             rsvd[3];
	wwn_t           ppwwn;  /* WWN of its physical port */
	wwn_t           lpwwn;  /* WWN of this logical port */
	wwn_t           rpwwn;  /* WWN of this remote port */
	union {
		struct bfa_rport_qos_attr_s qos;
	} priv;
};

union bfa_aen_data_u {
	struct bfa_adapter_aen_data_s	adapter;
	struct bfa_port_aen_data_s	port;
	struct bfa_lport_aen_data_s	lport;
	struct bfa_rport_aen_data_s	rport;
	struct bfa_itnim_aen_data_s	itnim;
	struct bfa_audit_aen_data_s	audit;
	struct bfa_ioc_aen_data_s	ioc;
};

#define BFA_AEN_MAX_ENTRY	512

struct bfa_aen_entry_s {
	struct list_head	qe;
	enum bfa_aen_category   aen_category;
	u32                     aen_type;
	union bfa_aen_data_u    aen_data;
	struct timeval          aen_tv;
	u32                     seq_num;
	u32                     bfad_num;
};

#endif /* __BFA_DEFS_SVC_H__ */
