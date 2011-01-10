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

#ifndef __BFI_MS_H__
#define __BFI_MS_H__

#include "bfi.h"
#include "bfa_fc.h"
#include "bfa_defs_svc.h"

#pragma pack(1)

enum bfi_iocfc_h2i_msgs {
	BFI_IOCFC_H2I_CFG_REQ		= 1,
	BFI_IOCFC_H2I_SET_INTR_REQ	= 2,
	BFI_IOCFC_H2I_UPDATEQ_REQ	= 3,
};

enum bfi_iocfc_i2h_msgs {
	BFI_IOCFC_I2H_CFG_REPLY		= BFA_I2HM(1),
	BFI_IOCFC_I2H_UPDATEQ_RSP	= BFA_I2HM(3),
};

struct bfi_iocfc_cfg_s {
	u8	num_cqs;	/*  Number of CQs to be used	*/
	u8	 sense_buf_len;	/*  SCSI sense length	    */
	u16	rsvd_1;
	u32	endian_sig;	/*  endian signature of host     */

	/*
	 * Request and response circular queue base addresses, size and
	 * shadow index pointers.
	 */
	union bfi_addr_u  req_cq_ba[BFI_IOC_MAX_CQS];
	union bfi_addr_u  req_shadow_ci[BFI_IOC_MAX_CQS];
	__be16    req_cq_elems[BFI_IOC_MAX_CQS];
	union bfi_addr_u  rsp_cq_ba[BFI_IOC_MAX_CQS];
	union bfi_addr_u  rsp_shadow_pi[BFI_IOC_MAX_CQS];
	__be16    rsp_cq_elems[BFI_IOC_MAX_CQS];

	union bfi_addr_u  stats_addr;	/*  DMA-able address for stats	  */
	union bfi_addr_u  cfgrsp_addr;	/*  config response dma address  */
	union bfi_addr_u  ioim_snsbase;  /*  IO sense buffer base address */
	struct bfa_iocfc_intr_attr_s intr_attr; /*  IOC interrupt attributes */
};

/*
 * Boot target wwn information for this port. This contains either the stored
 * or discovered boot target port wwns for the port.
 */
struct bfi_iocfc_bootwwns {
	wwn_t		wwn[BFA_BOOT_BOOTLUN_MAX];
	u8		nwwns;
	u8		rsvd[7];
};

struct bfi_iocfc_cfgrsp_s {
	struct bfa_iocfc_fwcfg_s	fwcfg;
	struct bfa_iocfc_intr_attr_s	intr_attr;
	struct bfi_iocfc_bootwwns	bootwwns;
	struct bfi_pbc_s		pbc_cfg;
};

/*
 * BFI_IOCFC_H2I_CFG_REQ message
 */
struct bfi_iocfc_cfg_req_s {
	struct bfi_mhdr_s      mh;
	union bfi_addr_u      ioc_cfg_dma_addr;
};


/*
 * BFI_IOCFC_I2H_CFG_REPLY message
 */
struct bfi_iocfc_cfg_reply_s {
	struct bfi_mhdr_s  mh;		/*  Common msg header	  */
	u8	 cfg_success;	/*  cfg reply status	   */
	u8	 lpu_bm;		/*  LPUs assigned for this IOC */
	u8	 rsvd[2];
};


/*
 * BFI_IOCFC_H2I_SET_INTR_REQ message
 */
struct bfi_iocfc_set_intr_req_s {
	struct bfi_mhdr_s mh;		/*  common msg header		*/
	u8		coalesce;	/*  enable intr coalescing	*/
	u8		rsvd[3];
	__be16	delay;		/*  delay timer 0..1125us	*/
	__be16	latency;	/*  latency timer 0..225us	*/
};


/*
 * BFI_IOCFC_H2I_UPDATEQ_REQ message
 */
struct bfi_iocfc_updateq_req_s {
	struct bfi_mhdr_s mh;		/*  common msg header		*/
	u32 reqq_ba;		/*  reqq base addr		*/
	u32 rspq_ba;		/*  rspq base addr		*/
	u32 reqq_sci;		/*  reqq shadow ci		*/
	u32 rspq_spi;		/*  rspq shadow pi		*/
};


/*
 * BFI_IOCFC_I2H_UPDATEQ_RSP message
 */
struct bfi_iocfc_updateq_rsp_s {
	struct bfi_mhdr_s mh;		/*  common msg header	*/
	u8	status;			/*  updateq  status	*/
	u8	rsvd[3];
};


/*
 * H2I Messages
 */
union bfi_iocfc_h2i_msg_u {
	struct bfi_mhdr_s		mh;
	struct bfi_iocfc_cfg_req_s	cfg_req;
	struct bfi_iocfc_updateq_req_s updateq_req;
	u32 mboxmsg[BFI_IOC_MSGSZ];
};


/*
 * I2H Messages
 */
union bfi_iocfc_i2h_msg_u {
	struct bfi_mhdr_s		mh;
	struct bfi_iocfc_cfg_reply_s	cfg_reply;
	struct bfi_iocfc_updateq_rsp_s updateq_rsp;
	u32 mboxmsg[BFI_IOC_MSGSZ];
};


enum bfi_fcport_h2i {
	BFI_FCPORT_H2I_ENABLE_REQ		= (1),
	BFI_FCPORT_H2I_DISABLE_REQ		= (2),
	BFI_FCPORT_H2I_SET_SVC_PARAMS_REQ	= (3),
	BFI_FCPORT_H2I_STATS_GET_REQ		= (4),
	BFI_FCPORT_H2I_STATS_CLEAR_REQ		= (5),
};


enum bfi_fcport_i2h {
	BFI_FCPORT_I2H_ENABLE_RSP		= BFA_I2HM(1),
	BFI_FCPORT_I2H_DISABLE_RSP		= BFA_I2HM(2),
	BFI_FCPORT_I2H_SET_SVC_PARAMS_RSP	= BFA_I2HM(3),
	BFI_FCPORT_I2H_STATS_GET_RSP		= BFA_I2HM(4),
	BFI_FCPORT_I2H_STATS_CLEAR_RSP		= BFA_I2HM(5),
	BFI_FCPORT_I2H_EVENT			= BFA_I2HM(6),
	BFI_FCPORT_I2H_TRUNK_SCN		= BFA_I2HM(7),
	BFI_FCPORT_I2H_ENABLE_AEN		= BFA_I2HM(8),
	BFI_FCPORT_I2H_DISABLE_AEN		= BFA_I2HM(9),
};


/*
 * Generic REQ type
 */
struct bfi_fcport_req_s {
	struct bfi_mhdr_s  mh;		/*  msg header			    */
	u32	   msgtag;	/*  msgtag for reply		    */
};

/*
 * Generic RSP type
 */
struct bfi_fcport_rsp_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		    */
	u8		   status;	/*  port enable status		    */
	u8		   rsvd[3];
	struct	bfa_port_cfg_s port_cfg;/* port configuration	*/
	u32	msgtag;			/* msgtag for reply	*/
};

/*
 * BFI_FCPORT_H2I_ENABLE_REQ
 */
struct bfi_fcport_enable_req_s {
	struct bfi_mhdr_s  mh;		/*  msg header			    */
	u32	   rsvd1;
	wwn_t		   nwwn;	/*  node wwn of physical port	    */
	wwn_t		   pwwn;	/*  port wwn of physical port	    */
	struct bfa_port_cfg_s port_cfg; /*  port configuration	    */
	union bfi_addr_u   stats_dma_addr; /*  DMA address for stats	    */
	u32	   msgtag;	/*  msgtag for reply		    */
	u8	use_flash_cfg;	/* get prot cfg from flash */
	u8	rsvd2[3];
};

/*
 * BFI_FCPORT_H2I_SET_SVC_PARAMS_REQ
 */
struct bfi_fcport_set_svc_params_req_s {
	struct bfi_mhdr_s  mh;		/*  msg header */
	__be16	   tx_bbcredit;	/*  Tx credits */
	u16	   rsvd;
};

/*
 * BFI_FCPORT_I2H_EVENT
 */
struct bfi_fcport_event_s {
	struct bfi_mhdr_s	mh;	/*  common msg header */
	struct bfa_port_link_s	link_state;
};

/*
 * BFI_FCPORT_I2H_TRUNK_SCN
 */
struct bfi_fcport_trunk_link_s {
	wwn_t			trunk_wwn;
	u8			fctl;		/* bfa_trunk_link_fctl_t */
	u8			state;		/* bfa_trunk_link_state_t */
	u8			speed;		/* bfa_port_speed_t */
	u8			rsvd;
	__be32		deskew;
};

#define BFI_FCPORT_MAX_LINKS	2
struct bfi_fcport_trunk_scn_s {
	struct bfi_mhdr_s	mh;
	u8			trunk_state;	/* bfa_trunk_state_t */
	u8			trunk_speed;	/* bfa_port_speed_t */
	u8			rsvd_a[2];
	struct bfi_fcport_trunk_link_s tlink[BFI_FCPORT_MAX_LINKS];
};

/*
 * fcport H2I message
 */
union bfi_fcport_h2i_msg_u {
	struct bfi_mhdr_s			*mhdr;
	struct bfi_fcport_enable_req_s		*penable;
	struct bfi_fcport_req_s			*pdisable;
	struct bfi_fcport_set_svc_params_req_s	*psetsvcparams;
	struct bfi_fcport_req_s			*pstatsget;
	struct bfi_fcport_req_s			*pstatsclear;
};

/*
 * fcport I2H message
 */
union bfi_fcport_i2h_msg_u {
	struct bfi_msg_s			*msg;
	struct bfi_fcport_rsp_s			*penable_rsp;
	struct bfi_fcport_rsp_s			*pdisable_rsp;
	struct bfi_fcport_rsp_s			*psetsvcparams_rsp;
	struct bfi_fcport_rsp_s			*pstatsget_rsp;
	struct bfi_fcport_rsp_s			*pstatsclear_rsp;
	struct bfi_fcport_event_s		*event;
	struct bfi_fcport_trunk_scn_s		*trunk_scn;
};

enum bfi_fcxp_h2i {
	BFI_FCXP_H2I_SEND_REQ = 1,
};

enum bfi_fcxp_i2h {
	BFI_FCXP_I2H_SEND_RSP = BFA_I2HM(1),
};

#define BFA_FCXP_MAX_SGES	2

/*
 * FCXP send request structure
 */
struct bfi_fcxp_send_req_s {
	struct bfi_mhdr_s  mh;		/*  Common msg header		    */
	__be16	fcxp_tag;	/*  driver request tag		    */
	__be16	max_frmsz;	/*  max send frame size	    */
	__be16	vf_id;		/*  vsan tag if applicable	    */
	u16	rport_fw_hndl;	/*  FW Handle for the remote port  */
	u8	 class;		/*  FC class used for req/rsp	    */
	u8	 rsp_timeout;	/*  timeout in secs, 0-no response */
	u8	 cts;		/*  continue sequence		    */
	u8	 lp_tag;	/*  lport tag			    */
	struct fchs_s	fchs;	/*  request FC header structure    */
	__be32	req_len;	/*  request payload length	    */
	__be32	rsp_maxlen;	/*  max response length expected   */
	struct bfi_sge_s   req_sge[BFA_FCXP_MAX_SGES];	/*  request buf    */
	struct bfi_sge_s   rsp_sge[BFA_FCXP_MAX_SGES];	/*  response buf   */
};

/*
 * FCXP send response structure
 */
struct bfi_fcxp_send_rsp_s {
	struct bfi_mhdr_s  mh;		/*  Common msg header		    */
	__be16	fcxp_tag;	/*  send request tag		    */
	u8	 req_status;	/*  request status		    */
	u8	 rsvd;
	__be32	rsp_len;	/*  actual response length	    */
	__be32	residue_len;	/*  residual response length	    */
	struct fchs_s	fchs;	/*  response FC header structure   */
};

enum bfi_uf_h2i {
	BFI_UF_H2I_BUF_POST = 1,
};

enum bfi_uf_i2h {
	BFI_UF_I2H_FRM_RCVD = BFA_I2HM(1),
};

#define BFA_UF_MAX_SGES	2

struct bfi_uf_buf_post_s {
	struct bfi_mhdr_s  mh;		/*  Common msg header		*/
	u16	buf_tag;	/*  buffer tag			*/
	__be16	buf_len;	/*  total buffer length	*/
	struct bfi_sge_s   sge[BFA_UF_MAX_SGES]; /*  buffer DMA SGEs	*/
};

struct bfi_uf_frm_rcvd_s {
	struct bfi_mhdr_s  mh;		/*  Common msg header		*/
	u16	buf_tag;	/*  buffer tag			*/
	u16	rsvd;
	u16	frm_len;	/*  received frame length	*/
	u16	xfr_len;	/*  tranferred length		*/
};

enum bfi_lps_h2i_msgs {
	BFI_LPS_H2I_LOGIN_REQ	= 1,
	BFI_LPS_H2I_LOGOUT_REQ	= 2,
	BFI_LPS_H2I_N2N_PID_REQ = 3,
};

enum bfi_lps_i2h_msgs {
	BFI_LPS_H2I_LOGIN_RSP	= BFA_I2HM(1),
	BFI_LPS_H2I_LOGOUT_RSP	= BFA_I2HM(2),
	BFI_LPS_H2I_CVL_EVENT	= BFA_I2HM(3),
};

struct bfi_lps_login_req_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u8		lp_tag;
	u8		alpa;
	__be16		pdu_size;
	wwn_t		pwwn;
	wwn_t		nwwn;
	u8		fdisc;
	u8		auth_en;
	u8		rsvd[2];
};

struct bfi_lps_login_rsp_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u8		lp_tag;
	u8		status;
	u8		lsrjt_rsn;
	u8		lsrjt_expl;
	wwn_t		port_name;
	wwn_t		node_name;
	__be16		bb_credit;
	u8		f_port;
	u8		npiv_en;
	u32	lp_pid:24;
	u32	auth_req:8;
	mac_t		lp_mac;
	mac_t		fcf_mac;
	u8		ext_status;
	u8		brcd_switch;	/*  attached peer is brcd switch */
};

struct bfi_lps_logout_req_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u8		lp_tag;
	u8		rsvd[3];
	wwn_t		port_name;
};

struct bfi_lps_logout_rsp_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u8		lp_tag;
	u8		status;
	u8		rsvd[2];
};

struct bfi_lps_cvl_event_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u8		lp_tag;
	u8		rsvd[3];
};

struct bfi_lps_n2n_pid_req_s {
	struct bfi_mhdr_s	mh;	/*  common msg header		*/
	u8	lp_tag;
	u32	lp_pid:24;
};

union bfi_lps_h2i_msg_u {
	struct bfi_mhdr_s		*msg;
	struct bfi_lps_login_req_s	*login_req;
	struct bfi_lps_logout_req_s	*logout_req;
	struct bfi_lps_n2n_pid_req_s	*n2n_pid_req;
};

union bfi_lps_i2h_msg_u {
	struct bfi_msg_s		*msg;
	struct bfi_lps_login_rsp_s	*login_rsp;
	struct bfi_lps_logout_rsp_s	*logout_rsp;
	struct bfi_lps_cvl_event_s	*cvl_event;
};

enum bfi_rport_h2i_msgs {
	BFI_RPORT_H2I_CREATE_REQ = 1,
	BFI_RPORT_H2I_DELETE_REQ = 2,
	BFI_RPORT_H2I_SET_SPEED_REQ  = 3,
};

enum bfi_rport_i2h_msgs {
	BFI_RPORT_I2H_CREATE_RSP = BFA_I2HM(1),
	BFI_RPORT_I2H_DELETE_RSP = BFA_I2HM(2),
	BFI_RPORT_I2H_QOS_SCN    = BFA_I2HM(3),
};

struct bfi_rport_create_req_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u16	bfa_handle;	/*  host rport handle		*/
	__be16	max_frmsz;	/*  max rcv pdu size		*/
	u32	pid:24,	/*  remote port ID		*/
		lp_tag:8;	/*  local port tag		*/
	u32	local_pid:24,	/*  local port ID		*/
		cisc:8;
	u8	fc_class;	/*  supported FC classes	*/
	u8	vf_en;		/*  virtual fabric enable	*/
	u16	vf_id;		/*  virtual fabric ID		*/
};

struct bfi_rport_create_rsp_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u8		status;		/*  rport creation status	*/
	u8		rsvd[3];
	u16	bfa_handle;	/*  host rport handle		*/
	u16	fw_handle;	/*  firmware rport handle	*/
	struct bfa_rport_qos_attr_s qos_attr;  /* QoS Attributes */
};

struct bfa_rport_speed_req_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u16	fw_handle;	/*  firmware rport handle	*/
	u8		speed;		/*  rport's speed via RPSC	*/
	u8		rsvd;
};

struct bfi_rport_delete_req_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u16	fw_handle;	/*  firmware rport handle	*/
	u16	rsvd;
};

struct bfi_rport_delete_rsp_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u16	bfa_handle;	/*  host rport handle		*/
	u8		status;		/*  rport deletion status	*/
	u8		rsvd;
};

struct bfi_rport_qos_scn_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u16	bfa_handle;	/*  host rport handle		*/
	u16	rsvd;
	struct bfa_rport_qos_attr_s old_qos_attr;  /* Old QoS Attributes */
	struct bfa_rport_qos_attr_s new_qos_attr;  /* New QoS Attributes */
};

union bfi_rport_h2i_msg_u {
	struct bfi_msg_s		*msg;
	struct bfi_rport_create_req_s	*create_req;
	struct bfi_rport_delete_req_s	*delete_req;
	struct bfi_rport_speed_req_s	*speed_req;
};

union bfi_rport_i2h_msg_u {
	struct bfi_msg_s		*msg;
	struct bfi_rport_create_rsp_s	*create_rsp;
	struct bfi_rport_delete_rsp_s	*delete_rsp;
	struct bfi_rport_qos_scn_s	*qos_scn_evt;
};

/*
 * Initiator mode I-T nexus interface defines.
 */

enum bfi_itnim_h2i {
	BFI_ITNIM_H2I_CREATE_REQ = 1,	/*  i-t nexus creation */
	BFI_ITNIM_H2I_DELETE_REQ = 2,	/*  i-t nexus deletion */
};

enum bfi_itnim_i2h {
	BFI_ITNIM_I2H_CREATE_RSP = BFA_I2HM(1),
	BFI_ITNIM_I2H_DELETE_RSP = BFA_I2HM(2),
	BFI_ITNIM_I2H_SLER_EVENT = BFA_I2HM(3),
};

struct bfi_itnim_create_req_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		 */
	u16	fw_handle;	/*  f/w handle for itnim	 */
	u8	class;		/*  FC class for IO		 */
	u8	seq_rec;	/*  sequence recovery support	 */
	u8	msg_no;		/*  seq id of the msg		 */
};

struct bfi_itnim_create_rsp_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		 */
	u16	bfa_handle;	/*  bfa handle for itnim	 */
	u8	status;		/*  fcp request status		 */
	u8	seq_id;		/*  seq id of the msg		 */
};

struct bfi_itnim_delete_req_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		 */
	u16	fw_handle;	/*  f/w itnim handle		 */
	u8	seq_id;		/*  seq id of the msg		 */
	u8	rsvd;
};

struct bfi_itnim_delete_rsp_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		 */
	u16	bfa_handle;	/*  bfa handle for itnim	 */
	u8	status;		/*  fcp request status		 */
	u8	seq_id;		/*  seq id of the msg		 */
};

struct bfi_itnim_sler_event_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		 */
	u16	bfa_handle;	/*  bfa handle for itnim	 */
	u16	rsvd;
};

union bfi_itnim_h2i_msg_u {
	struct bfi_itnim_create_req_s *create_req;
	struct bfi_itnim_delete_req_s *delete_req;
	struct bfi_msg_s	*msg;
};

union bfi_itnim_i2h_msg_u {
	struct bfi_itnim_create_rsp_s *create_rsp;
	struct bfi_itnim_delete_rsp_s *delete_rsp;
	struct bfi_itnim_sler_event_s *sler_event;
	struct bfi_msg_s	*msg;
};

/*
 * Initiator mode IO interface defines.
 */

enum bfi_ioim_h2i {
	BFI_IOIM_H2I_IOABORT_REQ = 1,	/*  IO abort request	 */
	BFI_IOIM_H2I_IOCLEANUP_REQ = 2,	/*  IO cleanup request	 */
};

enum bfi_ioim_i2h {
	BFI_IOIM_I2H_IO_RSP = BFA_I2HM(1),	/*  non-fp IO response	 */
	BFI_IOIM_I2H_IOABORT_RSP = BFA_I2HM(2),	/*  ABORT rsp	 */
};

/*
 * IO command DIF info
 */
struct bfi_ioim_dif_s {
	u32	dif_info[4];
};

/*
 * FCP IO messages overview
 *
 * @note
 * - Max CDB length supported is 64 bytes.
 * - SCSI Linked commands and SCSI bi-directional Commands not
 *	supported.
 *
 */
struct bfi_ioim_req_s {
	struct bfi_mhdr_s  mh;		/*  Common msg header		 */
	__be16	io_tag;		/*  I/O tag			 */
	u16	rport_hdl;	/*  itnim/rport firmware handle */
	struct fcp_cmnd_s	cmnd;	/*  IO request info	*/

	/*
	 * SG elements array within the IO request must be double word
	 * aligned. This aligment is required to optimize SGM setup for the IO.
	 */
	struct bfi_sge_s	sges[BFI_SGE_INLINE_MAX];
	u8	io_timeout;
	u8	dif_en;
	u8	rsvd_a[2];
	struct bfi_ioim_dif_s  dif;
};

/*
 *	This table shows various IO status codes from firmware and their
 *	meaning. Host driver can use these status codes to further process
 *	IO completions.
 *
 *	BFI_IOIM_STS_OK		: IO completed with error free SCSI &
 *					transport status.
 *					 io-tag can be reused.
 *
 *	BFA_IOIM_STS_SCSI_ERR		: IO completed with scsi error.
 *	- io-tag can be reused.
 *
 *	BFI_IOIM_STS_HOST_ABORTED	: IO was aborted successfully due to
 *						host request.
 *					- io-tag cannot be reused yet.
 *
 *	BFI_IOIM_STS_ABORTED		: IO was aborted successfully
 *						internally by f/w.
 *					- io-tag cannot be reused yet.
 *
 *	BFI_IOIM_STS_TIMEDOUT	: IO timedout and ABTS/RRQ is happening
 *					in the firmware and
 *					- io-tag cannot be reused yet.
 *
 *	BFI_IOIM_STS_SQER_NEEDED	: Firmware could not recover the IO
 *					  with sequence level error
 *	logic and hence host needs to retry
 *					  this IO with a different IO tag
 *					- io-tag cannot be used yet.
 *
 *	BFI_IOIM_STS_NEXUS_ABORT	: Second Level Error Recovery from host
 *					  is required because 2 consecutive ABTS
 *					  timedout and host needs logout and
 *					  re-login with the target
 *					- io-tag cannot be used yet.
 *
 *	BFI_IOIM_STS_UNDERRUN	: IO completed with SCSI status good,
 *					  but the data tranferred is less than
 *					  the fcp data length in the command.
 *					  ex. SCSI INQUIRY where transferred
 *					  data length and residue count in FCP
 *					  response accounts for total fcp-dl
 *					  - io-tag can be reused.
 *
 *	BFI_IOIM_STS_OVERRUN	: IO completed with SCSI status good,
 *					  but the data transerred is more than
 *					  fcp data length in the command. ex.
 *					  TAPE IOs where blocks can of unequal
 *					  lengths.
 *					- io-tag can be reused.
 *
 *	BFI_IOIM_STS_RES_FREE	: Firmware has completed using io-tag
 *					  during abort process
 *					- io-tag can be reused.
 *
 *	BFI_IOIM_STS_PROTO_ERR	: Firmware detected a protocol error.
 *					  ex target sent more data than
 *					  requested, or there was data frame
 *					  loss and other reasons
 *					- io-tag cannot be used yet.
 *
 *	BFI_IOIM_STS_DIF_ERR	: Firwmare detected DIF error. ex: DIF
 *					CRC err or Ref Tag err or App tag err.
 *					- io-tag can be reused.
 *
 *	BFA_IOIM_STS_TSK_MGT_ABORT	: IO was aborted because of Task
 *					  Management command from the host
 *					  - io-tag can be reused.
 *
 *	BFI_IOIM_STS_UTAG		: Firmware does not know about this
 *					  io_tag.
 *					- io-tag can be reused.
 */
enum bfi_ioim_status {
	BFI_IOIM_STS_OK = 0,
	BFI_IOIM_STS_HOST_ABORTED = 1,
	BFI_IOIM_STS_ABORTED = 2,
	BFI_IOIM_STS_TIMEDOUT = 3,
	BFI_IOIM_STS_RES_FREE = 4,
	BFI_IOIM_STS_SQER_NEEDED = 5,
	BFI_IOIM_STS_PROTO_ERR = 6,
	BFI_IOIM_STS_UTAG = 7,
	BFI_IOIM_STS_PATHTOV = 8,
};

#define BFI_IOIM_SNSLEN	(256)
/*
 * I/O response message
 */
struct bfi_ioim_rsp_s {
	struct bfi_mhdr_s	mh;	/*  common msg header		*/
	__be16	io_tag;		/*  completed IO tag		 */
	u16	bfa_rport_hndl;	/*  releated rport handle	 */
	u8	io_status;	/*  IO completion status	 */
	u8	reuse_io_tag;	/*  IO tag can be reused	*/
	u16	abort_tag;	/*  host abort request tag	*/
	u8		scsi_status;	/*  scsi status from target	 */
	u8		sns_len;	/*  scsi sense length		 */
	u8		resid_flags;	/*  IO residue flags		 */
	u8		rsvd_a;
	__be32	residue;	/*  IO residual length in bytes */
	u32	rsvd_b[3];
};

struct bfi_ioim_abort_req_s {
	struct bfi_mhdr_s  mh;	/*  Common msg header  */
	__be16	io_tag;	/*  I/O tag	*/
	u16	abort_tag;	/*  unique request tag */
};

/*
 * Initiator mode task management command interface defines.
 */

enum bfi_tskim_h2i {
	BFI_TSKIM_H2I_TM_REQ	= 1, /*  task-mgmt command	*/
	BFI_TSKIM_H2I_ABORT_REQ = 2, /*  task-mgmt command	*/
};

enum bfi_tskim_i2h {
	BFI_TSKIM_I2H_TM_RSP = BFA_I2HM(1),
};

struct bfi_tskim_req_s {
	struct bfi_mhdr_s  mh;	/*  Common msg header	*/
	__be16	tsk_tag;	/*  task management tag	*/
	u16	itn_fhdl;	/*  itn firmware handle	*/
	struct 	scsi_lun lun;	/*  LU number	*/
	u8	tm_flags;	/*  see enum fcp_tm_cmnd	*/
	u8	t_secs;	/*  Timeout value in seconds	*/
	u8	rsvd[2];
};

struct bfi_tskim_abortreq_s {
	struct bfi_mhdr_s  mh;	/*  Common msg header	*/
	__be16	tsk_tag;	/*  task management tag	*/
	u16	rsvd;
};

enum bfi_tskim_status {
	/*
	 * Following are FCP-4 spec defined status codes,
	 * **DO NOT CHANGE THEM **
	 */
	BFI_TSKIM_STS_OK	= 0,
	BFI_TSKIM_STS_NOT_SUPP = 4,
	BFI_TSKIM_STS_FAILED	= 5,

	/*
	 * Defined by BFA
	 */
	BFI_TSKIM_STS_TIMEOUT  = 10,	/*  TM request timedout	*/
	BFI_TSKIM_STS_ABORTED  = 11,	/*  Aborted on host request */
};

struct bfi_tskim_rsp_s {
	struct bfi_mhdr_s  mh;		/*  Common msg header		 */
	__be16	tsk_tag;	/*  task mgmt cmnd tag		 */
	u8	tsk_status;	/*  @ref bfi_tskim_status */
	u8	rsvd;
};

#pragma pack()

#endif /* __BFI_MS_H__ */
