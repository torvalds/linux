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

#ifndef __FC_H__
#define __FC_H__

#include <protocol/types.h>

#pragma pack(1)

/*
 * Fibre Channel Header Structure (FCHS) definition
 */
struct fchs_s {
#ifdef __BIGENDIAN
	u32        routing:4;	/* routing bits */
	u32        cat_info:4;	/* category info */
#else
	u32        cat_info:4;	/* category info */
	u32        routing:4;	/* routing bits */
#endif
	u32        d_id:24;	/* destination identifier */

	u32        cs_ctl:8;	/* class specific control */
	u32        s_id:24;	/* source identifier */

	u32        type:8;		/* data structure type */
	u32        f_ctl:24;	/* initial frame control */

	u8         seq_id;		/* sequence identifier */
	u8         df_ctl;		/* data field control */
	u16        seq_cnt;	/* sequence count */

	u16        ox_id;		/* originator exchange ID */
	u16        rx_id;		/* responder exchange ID */

	u32        ro;		/* relative offset */
};
/*
 * Fibre Channel BB_E Header Structure
 */
struct fcbbehs_s {
	u16	ver_rsvd;
	u32	rsvd[2];
	u32	rsvd__sof;
};

#define FC_SEQ_ID_MAX		256

/*
 * routing bit definitions
 */
enum {
	FC_RTG_FC4_DEV_DATA	= 0x0,	/* FC-4 Device Data */
	FC_RTG_EXT_LINK		= 0x2,	/* Extended Link Data */
	FC_RTG_FC4_LINK_DATA	= 0x3,	/* FC-4 Link Data */
	FC_RTG_VIDEO_DATA	= 0x4,	/* Video Data */
	FC_RTG_EXT_HDR		= 0x5,	/* VFT, IFR or Encapsuled */
	FC_RTG_BASIC_LINK	= 0x8,	/* Basic Link data */
	FC_RTG_LINK_CTRL	= 0xC,	/* Link Control */
};

/*
 * information category for extended link data and FC-4 Link Data
 */
enum {
	FC_CAT_LD_REQUEST	= 0x2,	/* Request */
	FC_CAT_LD_REPLY		= 0x3,	/* Reply */
	FC_CAT_LD_DIAG		= 0xF,	/* for DIAG use only */
};

/*
 * information category for extended headers (VFT, IFR or encapsulation)
 */
enum {
	FC_CAT_VFT_HDR = 0x0,	/* Virtual fabric tagging header */
	FC_CAT_IFR_HDR = 0x1,	/* Inter-Fabric routing header */
	FC_CAT_ENC_HDR = 0x2,	/* Encapsulation header */
};

/*
 * information category for FC-4 device data
 */
enum {
	FC_CAT_UNCATEG_INFO	= 0x0,	/* Uncategorized information */
	FC_CAT_SOLICIT_DATA	= 0x1,	/* Solicited Data */
	FC_CAT_UNSOLICIT_CTRL	= 0x2,	/* Unsolicited Control */
	FC_CAT_SOLICIT_CTRL	= 0x3,	/* Solicited Control */
	FC_CAT_UNSOLICIT_DATA	= 0x4,	/* Unsolicited Data */
	FC_CAT_DATA_DESC	= 0x5,	/* Data Descriptor */
	FC_CAT_UNSOLICIT_CMD	= 0x6,	/* Unsolicited Command */
	FC_CAT_CMD_STATUS	= 0x7,	/* Command Status */
};

/*
 * information category for Link Control
 */
enum {
	FC_CAT_ACK_1		= 0x00,
	FC_CAT_ACK_0_N		= 0x01,
	FC_CAT_P_RJT		= 0x02,
	FC_CAT_F_RJT		= 0x03,
	FC_CAT_P_BSY		= 0x04,
	FC_CAT_F_BSY_DATA	= 0x05,
	FC_CAT_F_BSY_LINK_CTL	= 0x06,
	FC_CAT_F_LCR		= 0x07,
	FC_CAT_NTY		= 0x08,
	FC_CAT_END		= 0x09,
};

/*
 * Type Field Definitions. FC-PH Section 18.5 pg. 165
 */
enum {
	FC_TYPE_BLS		= 0x0,	/* Basic Link Service */
	FC_TYPE_ELS		= 0x1,	/* Extended Link Service */
	FC_TYPE_IP		= 0x5,	/* IP */
	FC_TYPE_FCP		= 0x8,	/* SCSI-FCP */
	FC_TYPE_GPP		= 0x9,	/* SCSI_GPP */
	FC_TYPE_SERVICES	= 0x20,	/* Fibre Channel Services */
	FC_TYPE_FC_FSS		= 0x22,	/* Fabric Switch Services */
	FC_TYPE_FC_AL		= 0x23,	/* FC-AL */
	FC_TYPE_FC_SNMP		= 0x24,	/* FC-SNMP */
	FC_TYPE_MAX		= 256,	/* 256 FC-4 types */
};

struct fc_fc4types_s{
	u8         bits[FC_TYPE_MAX / 8];
};

/*
 * Frame Control Definitions. FC-PH Table-45. pg. 168
 */
enum {
	FCTL_EC_ORIG = 0x000000,	/* exchange originator */
	FCTL_EC_RESP = 0x800000,	/* exchange responder */
	FCTL_SEQ_INI = 0x000000,	/* sequence initiator */
	FCTL_SEQ_REC = 0x400000,	/* sequence recipient */
	FCTL_FS_EXCH = 0x200000,	/* first sequence of xchg */
	FCTL_LS_EXCH = 0x100000,	/* last sequence of xchg */
	FCTL_END_SEQ = 0x080000,	/* last frame of sequence */
	FCTL_SI_XFER = 0x010000,	/* seq initiative transfer */
	FCTL_RO_PRESENT = 0x000008,	/* relative offset present */
	FCTL_FILLBYTE_MASK = 0x000003	/* , fill byte mask */
};

/*
 * Fabric Well Known Addresses
 */
enum {
	FC_MIN_WELL_KNOWN_ADDR		= 0xFFFFF0,
	FC_DOMAIN_CONTROLLER_MASK 	= 0xFFFC00,
	FC_ALIAS_SERVER			= 0xFFFFF8,
	FC_MGMT_SERVER			= 0xFFFFFA,
	FC_TIME_SERVER			= 0xFFFFFB,
	FC_NAME_SERVER			= 0xFFFFFC,
	FC_FABRIC_CONTROLLER		= 0xFFFFFD,
	FC_FABRIC_PORT			= 0xFFFFFE,
	FC_BROADCAST_SERVER		= 0xFFFFFF
};

/*
 * domain/area/port defines
 */
#define FC_DOMAIN_MASK  0xFF0000
#define FC_DOMAIN_SHIFT 16
#define FC_AREA_MASK    0x00FF00
#define FC_AREA_SHIFT   8
#define FC_PORT_MASK    0x0000FF
#define FC_PORT_SHIFT   0

#define FC_GET_DOMAIN(p)	(((p) & FC_DOMAIN_MASK) >> FC_DOMAIN_SHIFT)
#define FC_GET_AREA(p)		(((p) & FC_AREA_MASK) >> FC_AREA_SHIFT)
#define FC_GET_PORT(p)		(((p) & FC_PORT_MASK) >> FC_PORT_SHIFT)

#define FC_DOMAIN_CTRLR(p)	(FC_DOMAIN_CONTROLLER_MASK | (FC_GET_DOMAIN(p)))

enum {
	FC_RXID_ANY = 0xFFFFU,
};

/*
 * generic ELS command
 */
struct fc_els_cmd_s{
	u32        els_code:8;	/* ELS Command Code */
	u32        reserved:24;
};

/*
 * ELS Command Codes. FC-PH Table-75. pg. 223
 */
enum {
	FC_ELS_LS_RJT = 0x1,	/* Link Service Reject. */
	FC_ELS_ACC = 0x02,	/* Accept */
	FC_ELS_PLOGI = 0x03,	/* N_Port Login. */
	FC_ELS_FLOGI = 0x04,	/* F_Port Login. */
	FC_ELS_LOGO = 0x05,	/* Logout. */
	FC_ELS_ABTX = 0x06,	/* Abort Exchange */
	FC_ELS_RES = 0x08,	/* Read Exchange status */
	FC_ELS_RSS = 0x09,	/* Read sequence status block */
	FC_ELS_RSI = 0x0A,	/* Request Sequence Initiative */
	FC_ELS_ESTC = 0x0C,	/* Estimate Credit. */
	FC_ELS_RTV = 0x0E,	/* Read Timeout Value. */
	FC_ELS_RLS = 0x0F,	/* Read Link Status. */
	FC_ELS_ECHO = 0x10,	/* Echo */
	FC_ELS_TEST = 0x11,	/* Test */
	FC_ELS_RRQ = 0x12,	/* Reinstate Recovery Qualifier. */
	FC_ELS_REC = 0x13,	/* Add this for TAPE support in FCR */
	FC_ELS_PRLI = 0x20,	/* Process Login */
	FC_ELS_PRLO = 0x21,	/* Process Logout. */
	FC_ELS_SCN = 0x22,	/* State Change Notification. */
	FC_ELS_TPRLO = 0x24,	/* Third Party Process Logout. */
	FC_ELS_PDISC = 0x50,	/* Discover N_Port Parameters. */
	FC_ELS_FDISC = 0x51,	/* Discover F_Port Parameters. */
	FC_ELS_ADISC = 0x52,	/* Discover Address. */
	FC_ELS_FAN = 0x60,	/* Fabric Address Notification */
	FC_ELS_RSCN = 0x61,	/* Reg State Change Notification */
	FC_ELS_SCR = 0x62,	/* State Change Registration. */
	FC_ELS_RTIN = 0x77,	/* Mangement server request */
	FC_ELS_RNID = 0x78,	/* Mangement server request */
	FC_ELS_RLIR = 0x79,	/* Registered Link Incident Record */

	FC_ELS_RPSC = 0x7D,	/* Report Port Speed Capabilities */
	FC_ELS_QSA = 0x7E,	/* Query Security Attributes. Ref FC-SP */
	FC_ELS_E2E_LBEACON = 0x81,
				/* End-to-End Link Beacon */
	FC_ELS_AUTH = 0x90,	/* Authentication. Ref FC-SP */
	FC_ELS_RFCN = 0x97,	/* Request Fabric Change Notification. Ref
				 *FC-SP */

};

/*
 *  Version numbers for FC-PH standards,
 *  used in login to indicate what port
 *  supports. See FC-PH-X table 158.
 */
enum {
	FC_PH_VER_4_3 = 0x09,
	FC_PH_VER_PH_3 = 0x20,
};

/*
 * PDU size defines
 */
enum {
	FC_MIN_PDUSZ = 512,
	FC_MAX_PDUSZ = 2112,
};

/*
 * N_Port PLOGI Common Service Parameters.
 * FC-PH-x. Figure-76. pg. 308.
 */
struct fc_plogi_csp_s{
	u8         verhi;	/* FC-PH high version */
	u8         verlo;	/* FC-PH low version */
	u16        bbcred;	/* BB_Credit */

#ifdef __BIGENDIAN
	u8         ciro:1,		/* continuously increasing RO */
			rro:1,		/* random relative offset */
			npiv_supp:1,	/* NPIV supported */
			port_type:1,	/* N_Port/F_port */
			altbbcred:1,	/* alternate BB_Credit */
			resolution:1,	/* ms/ns ED_TOV resolution */
			vvl_info:1,	/* VVL Info included */
			reserved1:1;

	u8         hg_supp:1,
			query_dbc:1,
			security:1,
			sync_cap:1,
			r_t_tov:1,
			dh_dup_supp:1,
			cisc:1,		/* continuously increasing seq count */
			payload:1;
#else
	u8         reserved2:2,
			resolution:1,	/* ms/ns ED_TOV resolution */
			altbbcred:1,	/* alternate BB_Credit */
			port_type:1,	/* N_Port/F_port */
			npiv_supp:1,	/* NPIV supported */
			rro:1,		/* random relative offset */
			ciro:1;		/* continuously increasing RO */

	u8         payload:1,
			cisc:1,		/* continuously increasing seq count */
			dh_dup_supp:1,
			r_t_tov:1,
			sync_cap:1,
			security:1,
			query_dbc:1,
			hg_supp:1;
#endif

	u16        rxsz;		/* recieve data_field size */

	u16        conseq;
	u16        ro_bitmap;

	u32        e_d_tov;
};

/*
 * N_Port PLOGI Class Specific Parameters.
 * FC-PH-x. Figure 78. pg. 318.
 */
struct fc_plogi_clp_s{
#ifdef __BIGENDIAN
	u32        class_valid:1;
	u32        intermix:1;	/* class intermix supported if set =1.
					 * valid only for class1. Reserved for
					 * class2 & class3
					 */
	u32        reserved1:2;
	u32        sequential:1;
	u32        reserved2:3;
#else
	u32        reserved2:3;
	u32        sequential:1;
	u32        reserved1:2;
	u32        intermix:1;	/* class intermix supported if set =1.
					 * valid only for class1. Reserved for
					 * class2 & class3
					 */
	u32        class_valid:1;
#endif

	u32        reserved3:24;

	u32        reserved4:16;
	u32        rxsz:16;	/* Receive data_field size */

	u32        reserved5:8;
	u32        conseq:8;
	u32        e2e_credit:16;	/* end to end credit */

	u32        reserved7:8;
	u32        ospx:8;
	u32        reserved8:16;
};

#define FLOGI_VVL_BRCD    0x42524344 /* ASCII value for each character in
				      * string "BRCD" */

/*
 * PLOGI els command and reply payload
 */
struct fc_logi_s{
	struct fc_els_cmd_s els_cmd;	/* ELS command code */
	struct fc_plogi_csp_s  csp;		/* common service params */
	wwn_t           port_name;
	wwn_t           node_name;
	struct fc_plogi_clp_s  class1;		/* class 1 service parameters */
	struct fc_plogi_clp_s  class2;		/* class 2 service parameters */
	struct fc_plogi_clp_s  class3;		/* class 3 service parameters */
	struct fc_plogi_clp_s  class4;		/* class 4 service parameters */
	u8         vvl[16];	/* vendor version level */
};

/*
 * LOGO els command payload
 */
struct fc_logo_s{
	struct fc_els_cmd_s    els_cmd;	/* ELS command code */
	u32        res1:8;
	u32        nport_id:24;	/* N_Port identifier of source */
	wwn_t           orig_port_name;	/* Port name of the LOGO originator */
};

/*
 * ADISC els command payload
 */
struct fc_adisc_s {
	struct fc_els_cmd_s    els_cmd;	/* ELS command code */
	u32        res1:8;
	u32        orig_HA:24;	/* originator hard address */
	wwn_t           orig_port_name;	/* originator port name */
	wwn_t           orig_node_name;	/* originator node name */
	u32        res2:8;
	u32        nport_id:24;	/* originator NPortID */
};

/*
 * Exchange status block
 */
struct fc_exch_status_blk_s{
	u32        oxid:16;
	u32        rxid:16;
	u32        res1:8;
	u32        orig_np:24;	/* originator NPortID */
	u32        res2:8;
	u32        resp_np:24;	/* responder NPortID */
	u32        es_bits;
	u32        res3;
	/*
	 * un modified section of the fields
	 */
};

/*
 * RES els command payload
 */
struct fc_res_s {
	struct fc_els_cmd_s    els_cmd;	/* ELS command code */
	u32        res1:8;
	u32        nport_id:24;	/* N_Port identifier of source */
	u32        oxid:16;
	u32        rxid:16;
	u8         assoc_hdr[32];
};

/*
 * RES els accept payload
 */
struct fc_res_acc_s{
	struct fc_els_cmd_s els_cmd;	/* ELS command code */
	struct fc_exch_status_blk_s fc_exch_blk; /* Exchange status block */
};

/*
 * REC els command payload
 */
struct fc_rec_s {
	struct fc_els_cmd_s    els_cmd;	/* ELS command code */
	u32        res1:8;
	u32        nport_id:24;	/* N_Port identifier of source */
	u32        oxid:16;
	u32        rxid:16;
};

#define FC_REC_ESB_OWN_RSP	0x80000000	/* responder owns */
#define FC_REC_ESB_SI		0x40000000	/* SI is owned 	*/
#define FC_REC_ESB_COMP		0x20000000	/* exchange is complete	*/
#define FC_REC_ESB_ENDCOND_ABN	0x10000000	/* abnormal ending 	*/
#define FC_REC_ESB_RQACT	0x04000000	/* recovery qual active	*/
#define FC_REC_ESB_ERRP_MSK	0x03000000
#define FC_REC_ESB_OXID_INV	0x00800000	/* invalid OXID		*/
#define FC_REC_ESB_RXID_INV	0x00400000	/* invalid RXID		*/
#define FC_REC_ESB_PRIO_INUSE	0x00200000

/*
 * REC els accept payload
 */
struct fc_rec_acc_s {
	struct fc_els_cmd_s    els_cmd;	/* ELS command code */
	u32        oxid:16;
	u32        rxid:16;
	u32        res1:8;
	u32        orig_id:24;	/* N_Port id of exchange originator */
	u32        res2:8;
	u32        resp_id:24;	/* N_Port id of exchange responder */
	u32        count;		/* data transfer count */
	u32        e_stat;		/* exchange status */
};

/*
 * RSI els payload
 */
struct fc_rsi_s {
	struct fc_els_cmd_s    els_cmd;
	u32        res1:8;
	u32        orig_sid:24;
	u32        oxid:16;
	u32        rxid:16;
};

/*
 * structure for PRLI paramater pages, both request & response
 * see FC-PH-X table 113 & 115 for explanation also FCP table 8
 */
struct fc_prli_params_s{
	u32        reserved: 16;
#ifdef __BIGENDIAN
	u32        reserved1: 5;
	u32        rec_support : 1;
	u32        task_retry_id : 1;
	u32        retry : 1;

	u32        confirm : 1;
	u32        doverlay:1;
	u32        initiator:1;
	u32        target:1;
	u32        cdmix:1;
	u32        drmix:1;
	u32        rxrdisab:1;
	u32        wxrdisab:1;
#else
	u32        retry : 1;
	u32        task_retry_id : 1;
	u32        rec_support : 1;
	u32        reserved1: 5;

	u32        wxrdisab:1;
	u32        rxrdisab:1;
	u32        drmix:1;
	u32        cdmix:1;
	u32        target:1;
	u32        initiator:1;
	u32        doverlay:1;
	u32        confirm : 1;
#endif
};

/*
 * valid values for rspcode in PRLI ACC payload
 */
enum {
	FC_PRLI_ACC_XQTD = 0x1,		/* request executed */
	FC_PRLI_ACC_PREDEF_IMG = 0x5,	/* predefined image - no prli needed */
};

struct fc_prli_params_page_s{
	u32        type:8;
	u32        codext:8;
#ifdef __BIGENDIAN
	u32        origprocasv:1;
	u32        rsppav:1;
	u32        imagepair:1;
	u32        reserved1:1;
	u32        rspcode:4;
#else
	u32        rspcode:4;
	u32        reserved1:1;
	u32        imagepair:1;
	u32        rsppav:1;
	u32        origprocasv:1;
#endif
	u32        reserved2:8;

	u32        origprocas;
	u32        rspprocas;
	struct fc_prli_params_s  servparams;
};

/*
 * PRLI request and accept payload, FC-PH-X tables 112 & 114
 */
struct fc_prli_s{
	u32        command:8;
	u32        pglen:8;
	u32        pagebytes:16;
	struct fc_prli_params_page_s parampage;
};

/*
 * PRLO logout params page
 */
struct fc_prlo_params_page_s{
	u32        type:8;
	u32        type_ext:8;
#ifdef __BIGENDIAN
	u32        opa_valid:1;	/* originator process associator
					 * valid
					 */
	u32        rpa_valid:1;	/* responder process associator valid */
	u32        res1:14;
#else
	u32        res1:14;
	u32        rpa_valid:1;	/* responder process associator valid */
	u32        opa_valid:1;	/* originator process associator
					 * valid
					 */
#endif
	u32        orig_process_assc;
	u32        resp_process_assc;

	u32        res2;
};

/*
 * PRLO els command payload
 */
struct fc_prlo_s{
	u32        	command:8;
	u32        	page_len:8;
	u32        	payload_len:16;
	struct fc_prlo_params_page_s 	prlo_params[1];
};

/*
 * PRLO Logout response parameter page
 */
struct fc_prlo_acc_params_page_s{
	u32        type:8;
	u32        type_ext:8;

#ifdef __BIGENDIAN
	u32        opa_valid:1;	/* originator process associator
					 * valid
					 */
	u32        rpa_valid:1;	/* responder process associator valid */
	u32        res1:14;
#else
	u32        res1:14;
	u32        rpa_valid:1;	/* responder process associator valid */
	u32        opa_valid:1;	/* originator process associator
					 * valid
					 */
#endif
	u32        orig_process_assc;
	u32        resp_process_assc;

	u32        fc4type_csp;
};

/*
 * PRLO els command ACC payload
 */
struct fc_prlo_acc_s{
	u32        command:8;
	u32        page_len:8;
	u32        payload_len:16;
	struct fc_prlo_acc_params_page_s prlo_acc_params[1];
};

/*
 * SCR els command payload
 */
enum {
	FC_SCR_REG_FUNC_FABRIC_DETECTED = 0x01,
	FC_SCR_REG_FUNC_N_PORT_DETECTED = 0x02,
	FC_SCR_REG_FUNC_FULL = 0x03,
	FC_SCR_REG_FUNC_CLEAR_REG = 0xFF,
};

/* SCR VU registrations */
enum {
	FC_VU_SCR_REG_FUNC_FABRIC_NAME_CHANGE = 0x01
};

struct fc_scr_s{
	u32 command:8;
	u32 res:24;
	u32 vu_reg_func:8; /* Vendor Unique Registrations */
	u32 res1:16;
	u32 reg_func:8;
};

/*
 * Information category for Basic link data
 */
enum {
	FC_CAT_NOP	= 0x0,
	FC_CAT_ABTS	= 0x1,
	FC_CAT_RMC	= 0x2,
	FC_CAT_BA_ACC	= 0x4,
	FC_CAT_BA_RJT	= 0x5,
	FC_CAT_PRMT	= 0x6,
};

/*
 * LS_RJT els reply payload
 */
struct fc_ls_rjt_s {
	struct fc_els_cmd_s    els_cmd;		/* ELS command code */
	u32        res1:8;
	u32        reason_code:8;		/* Reason code for reject */
	u32        reason_code_expl:8;	/* Reason code explanation */
	u32        vendor_unique:8;	/* Vendor specific */
};

/*
 * LS_RJT reason codes
 */
enum {
	FC_LS_RJT_RSN_INV_CMD_CODE	= 0x01,
	FC_LS_RJT_RSN_LOGICAL_ERROR	= 0x03,
	FC_LS_RJT_RSN_LOGICAL_BUSY	= 0x05,
	FC_LS_RJT_RSN_PROTOCOL_ERROR	= 0x07,
	FC_LS_RJT_RSN_UNABLE_TO_PERF_CMD = 0x09,
	FC_LS_RJT_RSN_CMD_NOT_SUPP	= 0x0B,
};

/*
 * LS_RJT reason code explanation
 */
enum {
	FC_LS_RJT_EXP_NO_ADDL_INFO		= 0x00,
	FC_LS_RJT_EXP_SPARMS_ERR_OPTIONS	= 0x01,
	FC_LS_RJT_EXP_SPARMS_ERR_INI_CTL	= 0x03,
	FC_LS_RJT_EXP_SPARMS_ERR_REC_CTL	= 0x05,
	FC_LS_RJT_EXP_SPARMS_ERR_RXSZ		= 0x07,
	FC_LS_RJT_EXP_SPARMS_ERR_CONSEQ		= 0x09,
	FC_LS_RJT_EXP_SPARMS_ERR_CREDIT		= 0x0B,
	FC_LS_RJT_EXP_INV_PORT_NAME		= 0x0D,
	FC_LS_RJT_EXP_INV_NODE_FABRIC_NAME	= 0x0E,
	FC_LS_RJT_EXP_INV_CSP			= 0x0F,
	FC_LS_RJT_EXP_INV_ASSOC_HDR		= 0x11,
	FC_LS_RJT_EXP_ASSOC_HDR_REQD		= 0x13,
	FC_LS_RJT_EXP_INV_ORIG_S_ID		= 0x15,
	FC_LS_RJT_EXP_INV_OXID_RXID_COMB	= 0x17,
	FC_LS_RJT_EXP_CMD_ALREADY_IN_PROG	= 0x19,
	FC_LS_RJT_EXP_LOGIN_REQUIRED		= 0x1E,
	FC_LS_RJT_EXP_INVALID_NPORT_ID		= 0x1F,
	FC_LS_RJT_EXP_INSUFF_RES		= 0x29,
	FC_LS_RJT_EXP_CMD_NOT_SUPP		= 0x2C,
	FC_LS_RJT_EXP_INV_PAYLOAD_LEN		= 0x2D,
};

/*
 * RRQ els command payload
 */
struct fc_rrq_s{
	struct fc_els_cmd_s    els_cmd;	/* ELS command code */
	u32        res1:8;
	u32        s_id:24;	/* exchange originator S_ID */

	u32        ox_id:16;	/* originator exchange ID */
	u32        rx_id:16;	/* responder exchange ID */

	u32        res2[8];	/* optional association header */
};

/*
 * ABTS BA_ACC reply payload
 */
struct fc_ba_acc_s{
	u32        seq_id_valid:8;	/* set to 0x00 for Abort Exchange */
	u32        seq_id:8;	/* invalid for Abort Exchange */
	u32        res2:16;
	u32        ox_id:16;	/* OX_ID from ABTS frame */
	u32        rx_id:16;	/* RX_ID from ABTS frame */
	u32        low_seq_cnt:16;	/* set to 0x0000 for Abort Exchange */
	u32        high_seq_cnt:16;/* set to 0xFFFF for Abort Exchange */
};

/*
 * ABTS BA_RJT reject payload
 */
struct fc_ba_rjt_s{
	u32        res1:8;		/* Reserved */
	u32        reason_code:8;	/* reason code for reject */
	u32        reason_expl:8;	/* reason code explanation */
	u32        vendor_unique:8;/* vendor unique reason code,set to 0 */
};

/*
 * TPRLO logout parameter page
 */
struct fc_tprlo_params_page_s{
	u32        type:8;
	u32        type_ext:8;

#ifdef __BIGENDIAN
	u32        opa_valid:1;
	u32        rpa_valid:1;
	u32        tpo_nport_valid:1;
	u32        global_process_logout:1;
	u32        res1:12;
#else
	u32        res1:12;
	u32        global_process_logout:1;
	u32        tpo_nport_valid:1;
	u32        rpa_valid:1;
	u32        opa_valid:1;
#endif

	u32        orig_process_assc;
	u32        resp_process_assc;

	u32        res2:8;
	u32        tpo_nport_id;
};

/*
 * TPRLO ELS command payload
 */
struct fc_tprlo_s{
	u32        command:8;
	u32        page_len:8;
	u32        payload_len:16;

	struct fc_tprlo_params_page_s tprlo_params[1];
};

enum fc_tprlo_type{
	FC_GLOBAL_LOGO = 1,
	FC_TPR_LOGO
};

/*
 * TPRLO els command ACC payload
 */
struct fc_tprlo_acc_s{
	u32	command:8;
	u32	page_len:8;
	u32	payload_len:16;
	struct fc_prlo_acc_params_page_s tprlo_acc_params[1];
};

/*
 * RSCN els command req payload
 */
#define FC_RSCN_PGLEN	0x4

enum fc_rscn_format{
	FC_RSCN_FORMAT_PORTID	= 0x0,
	FC_RSCN_FORMAT_AREA	= 0x1,
	FC_RSCN_FORMAT_DOMAIN	= 0x2,
	FC_RSCN_FORMAT_FABRIC	= 0x3,
};

struct fc_rscn_event_s{
	u32        format:2;
	u32        qualifier:4;
	u32        resvd:2;
	u32        portid:24;
};

struct fc_rscn_pl_s{
	u8         command;
	u8         pagelen;
	u16        payldlen;
	struct fc_rscn_event_s event[1];
};

/*
 * ECHO els command req payload
 */
struct fc_echo_s {
	struct fc_els_cmd_s    els_cmd;
};

/*
 * RNID els command
 */

#define RNID_NODEID_DATA_FORMAT_COMMON    		 0x00
#define RNID_NODEID_DATA_FORMAT_FCP3        		 0x08
#define RNID_NODEID_DATA_FORMAT_DISCOVERY     		0xDF

#define RNID_ASSOCIATED_TYPE_UNKNOWN                    0x00000001
#define RNID_ASSOCIATED_TYPE_OTHER                      0x00000002
#define RNID_ASSOCIATED_TYPE_HUB                        0x00000003
#define RNID_ASSOCIATED_TYPE_SWITCH                     0x00000004
#define RNID_ASSOCIATED_TYPE_GATEWAY                    0x00000005
#define RNID_ASSOCIATED_TYPE_STORAGE_DEVICE             0x00000009
#define RNID_ASSOCIATED_TYPE_HOST                       0x0000000A
#define RNID_ASSOCIATED_TYPE_STORAGE_SUBSYSTEM          0x0000000B
#define RNID_ASSOCIATED_TYPE_STORAGE_ACCESS_DEVICE      0x0000000E
#define RNID_ASSOCIATED_TYPE_NAS_SERVER                 0x00000011
#define RNID_ASSOCIATED_TYPE_BRIDGE                     0x00000002
#define RNID_ASSOCIATED_TYPE_VIRTUALIZATION_DEVICE      0x00000003
#define RNID_ASSOCIATED_TYPE_MULTI_FUNCTION_DEVICE      0x000000FF

/*
 * RNID els command payload
 */
struct fc_rnid_cmd_s{
	struct fc_els_cmd_s    els_cmd;
	u32        node_id_data_format:8;
	u32        reserved:24;
};

/*
 * RNID els response payload
 */

struct fc_rnid_common_id_data_s{
	wwn_t           port_name;
	wwn_t           node_name;
};

struct fc_rnid_general_topology_data_s{
	u32        vendor_unique[4];
	u32        asso_type;
	u32        phy_port_num;
	u32        num_attached_nodes;
	u32        node_mgmt:8;
	u32        ip_version:8;
	u32        udp_tcp_port_num:16;
	u32        ip_address[4];
	u32        reserved:16;
	u32        vendor_specific:16;
};

struct fc_rnid_acc_s{
	struct fc_els_cmd_s    els_cmd;
	u32        node_id_data_format:8;
	u32        common_id_data_length:8;
	u32        reserved:8;
	u32        specific_id_data_length:8;
	struct fc_rnid_common_id_data_s common_id_data;
	struct fc_rnid_general_topology_data_s gen_topology_data;
};

#define RNID_ASSOCIATED_TYPE_UNKNOWN                    0x00000001
#define RNID_ASSOCIATED_TYPE_OTHER                      0x00000002
#define RNID_ASSOCIATED_TYPE_HUB                        0x00000003
#define RNID_ASSOCIATED_TYPE_SWITCH                     0x00000004
#define RNID_ASSOCIATED_TYPE_GATEWAY                    0x00000005
#define RNID_ASSOCIATED_TYPE_STORAGE_DEVICE             0x00000009
#define RNID_ASSOCIATED_TYPE_HOST                       0x0000000A
#define RNID_ASSOCIATED_TYPE_STORAGE_SUBSYSTEM          0x0000000B
#define RNID_ASSOCIATED_TYPE_STORAGE_ACCESS_DEVICE      0x0000000E
#define RNID_ASSOCIATED_TYPE_NAS_SERVER                 0x00000011
#define RNID_ASSOCIATED_TYPE_BRIDGE                     0x00000002
#define RNID_ASSOCIATED_TYPE_VIRTUALIZATION_DEVICE      0x00000003
#define RNID_ASSOCIATED_TYPE_MULTI_FUNCTION_DEVICE      0x000000FF

enum fc_rpsc_speed_cap{
	RPSC_SPEED_CAP_1G = 0x8000,
	RPSC_SPEED_CAP_2G = 0x4000,
	RPSC_SPEED_CAP_4G = 0x2000,
	RPSC_SPEED_CAP_10G = 0x1000,
	RPSC_SPEED_CAP_8G = 0x0800,
	RPSC_SPEED_CAP_16G = 0x0400,

	RPSC_SPEED_CAP_UNKNOWN = 0x0001,
};

enum fc_rpsc_op_speed_s{
	RPSC_OP_SPEED_1G = 0x8000,
	RPSC_OP_SPEED_2G = 0x4000,
	RPSC_OP_SPEED_4G = 0x2000,
	RPSC_OP_SPEED_10G = 0x1000,
	RPSC_OP_SPEED_8G = 0x0800,
	RPSC_OP_SPEED_16G = 0x0400,

	RPSC_OP_SPEED_NOT_EST = 0x0001,	/*! speed not established */
};

struct fc_rpsc_speed_info_s{
	u16        port_speed_cap;	/*! see fc_rpsc_speed_cap_t */
	u16        port_op_speed;	/*! see fc_rpsc_op_speed_t */
};

enum link_e2e_beacon_subcmd{
	LINK_E2E_BEACON_ON = 1,
	LINK_E2E_BEACON_OFF = 2
};

enum beacon_type{
	BEACON_TYPE_NORMAL	= 1,	/*! Normal Beaconing. Green */
	BEACON_TYPE_WARN	= 2,	/*! Warning Beaconing. Yellow/Amber */
	BEACON_TYPE_CRITICAL	= 3	/*! Critical Beaconing. Red */
};

struct link_e2e_beacon_param_s {
	u8         beacon_type;	/* Beacon Type. See beacon_type_t */
	u8         beacon_frequency;
					/* Beacon frequency. Number of blinks
					 * per 10 seconds
					 */
	u16        beacon_duration;/* Beacon duration (in Seconds). The
					 * command operation should be
					 * terminated at the end of this
					 * timeout value.
					 *
					 * Ignored if diag_sub_cmd is
					 * LINK_E2E_BEACON_OFF.
					 *
					 * If 0, beaconing will continue till a
					 * BEACON OFF request is received
					 */
};

/*
 * Link E2E beacon request/good response format. For LS_RJTs use fc_ls_rjt_t
 */
struct link_e2e_beacon_req_s{
	u32        ls_code;	/*! FC_ELS_E2E_LBEACON in requests *
					 *or FC_ELS_ACC in good replies */
	u32        ls_sub_cmd;	/*! See link_e2e_beacon_subcmd_t */
	struct link_e2e_beacon_param_s beacon_parm;
};

/**
 * If RPSC request is sent to the Domain Controller, the request is for
 * all the ports within that domain (TODO - I don't think FOS implements
 * this...).
 */
struct fc_rpsc_cmd_s{
	struct fc_els_cmd_s    els_cmd;
};

/*
 * RPSC Acc
 */
struct fc_rpsc_acc_s{
	u32        command:8;
	u32        rsvd:8;
	u32        num_entries:16;

	struct fc_rpsc_speed_info_s speed_info[1];
};

/**
 * If RPSC2 request is sent to the Domain Controller,
 */
#define FC_BRCD_TOKEN    0x42524344

struct fc_rpsc2_cmd_s{
	struct fc_els_cmd_s    els_cmd;
	u32       	token;
	u16     	resvd;
	u16     	num_pids;       /* Number of pids in the request */
	struct  {
		u32	rsvd1:8;
		u32	pid:24;	/* port identifier */
	} pid_list[1];
};

enum fc_rpsc2_port_type{
	RPSC2_PORT_TYPE_UNKNOWN = 0,
	RPSC2_PORT_TYPE_NPORT   = 1,
	RPSC2_PORT_TYPE_NLPORT  = 2,
	RPSC2_PORT_TYPE_NPIV_PORT  = 0x5f,
	RPSC2_PORT_TYPE_NPORT_TRUNK  = 0x6f,
};

/*
 * RPSC2 portInfo entry structure
 */
struct fc_rpsc2_port_info_s{
    u32    pid;        /* PID */
    u16    resvd1;
    u16    index;      /* port number / index */
    u8     resvd2;
    u8    	type;        /* port type N/NL/... */
    u16    speed;      /* port Operating Speed */
};

/*
 * RPSC2 Accept payload
 */
struct fc_rpsc2_acc_s{
	u8        els_cmd;
	u8        resvd;
	u16       num_pids;  /* Number of pids in the request */
	struct fc_rpsc2_port_info_s  port_info[1];    /* port information */
};

/**
 * bit fields so that multiple classes can be specified
 */
enum fc_cos{
	FC_CLASS_2	= 0x04,
	FC_CLASS_3	= 0x08,
	FC_CLASS_2_3	= 0x0C,
};

/*
 * symbolic name
 */
struct fc_symname_s{
	u8         symname[FC_SYMNAME_MAX];
};

struct fc_alpabm_s{
	u8         alpa_bm[FC_ALPA_MAX / 8];
};

/*
 * protocol default timeout values
 */
#define FC_ED_TOV		2
#define FC_REC_TOV		(FC_ED_TOV + 1)
#define FC_RA_TOV		10
#define FC_ELS_TOV		(2 * FC_RA_TOV)

/*
 * virtual fabric related defines
 */
#define FC_VF_ID_NULL    0	/*  must not be used as VF_ID */
#define FC_VF_ID_MIN     1
#define FC_VF_ID_MAX     0xEFF
#define FC_VF_ID_CTL     0xFEF	/*  control VF_ID */

/**
 * Virtual Fabric Tagging header format
 * @caution This is defined only in BIG ENDIAN format.
 */
struct fc_vft_s{
	u32        r_ctl:8;
	u32        ver:2;
	u32        type:4;
	u32        res_a:2;
	u32        priority:3;
	u32        vf_id:12;
	u32        res_b:1;
	u32        hopct:8;
	u32        res_c:24;
};

#pragma pack()

#endif
