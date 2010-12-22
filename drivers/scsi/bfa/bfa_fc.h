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

#ifndef __BFA_FC_H__
#define __BFA_FC_H__

#include "bfa_os_inc.h"

typedef u64 wwn_t;
typedef u64 lun_t;

#define WWN_NULL	(0)
#define FC_SYMNAME_MAX	256	/*  max name server symbolic name size */
#define FC_ALPA_MAX	128

#pragma pack(1)

#define MAC_ADDRLEN	(6)
struct mac_s { u8 mac[MAC_ADDRLEN]; };
#define mac_t struct mac_s

/*
 * generic SCSI cdb definition
 */
#define SCSI_MAX_CDBLEN     16
struct scsi_cdb_s {
	u8         scsi_cdb[SCSI_MAX_CDBLEN];
};
#define scsi_cdb_t struct scsi_cdb_s

/* ------------------------------------------------------------
 * SCSI status byte values
 * ------------------------------------------------------------
 */
#define SCSI_STATUS_GOOD                   0x00
#define SCSI_STATUS_CHECK_CONDITION        0x02
#define SCSI_STATUS_CONDITION_MET          0x04
#define SCSI_STATUS_BUSY                   0x08
#define SCSI_STATUS_INTERMEDIATE           0x10
#define SCSI_STATUS_ICM                    0x14 /* intermediate condition met */
#define SCSI_STATUS_RESERVATION_CONFLICT   0x18
#define SCSI_STATUS_COMMAND_TERMINATED     0x22
#define SCSI_STATUS_QUEUE_FULL             0x28
#define SCSI_STATUS_ACA_ACTIVE             0x30

#define SCSI_MAX_ALLOC_LEN      0xFF    /* maximum allocarion length */

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

#define FC_SOF_LEN		4
#define FC_EOF_LEN		4
#define FC_CRC_LEN		4

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
	FC_TYPE_FC_SPINFAB	= 0xEE,	/* SPINFAB */
	FC_TYPE_FC_DIAG		= 0xEF,	/* DIAG */
	FC_TYPE_MAX		= 256,	/* 256 FC-4 types */
};

struct fc_fc4types_s {
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
	FC_DOMAIN_CONTROLLER_MASK	= 0xFFFC00,
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
struct fc_els_cmd_s {
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
	FC_ELS_FARP_REQ = 0x54,	/* FARP Request. */
	FC_ELS_FARP_REP = 0x55,	/* FARP Reply. */
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
struct fc_plogi_csp_s {
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
struct fc_plogi_clp_s {
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

/* ASCII value for each character in string "BRCD" */
#define FLOGI_VVL_BRCD    0x42524344

/*
 * PLOGI els command and reply payload
 */
struct fc_logi_s {
	struct fc_els_cmd_s els_cmd;	/* ELS command code */
	struct fc_plogi_csp_s csp;		/* common service params */
	wwn_t           port_name;
	wwn_t           node_name;
	struct fc_plogi_clp_s class1;		/* class 1 service parameters */
	struct fc_plogi_clp_s class2;		/* class 2 service parameters */
	struct fc_plogi_clp_s class3;		/* class 3 service parameters */
	struct fc_plogi_clp_s class4;		/* class 4 service parameters */
	u8         vvl[16];	/* vendor version level */
};

/*
 * LOGO els command payload
 */
struct fc_logo_s {
	struct fc_els_cmd_s els_cmd;	/* ELS command code */
	u32        res1:8;
	u32        nport_id:24;	/* N_Port identifier of source */
	wwn_t           orig_port_name;	/* Port name of the LOGO originator */
};

/*
 * ADISC els command payload
 */
struct fc_adisc_s {
	struct fc_els_cmd_s els_cmd;	/* ELS command code */
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
struct fc_exch_status_blk_s {
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
	struct fc_els_cmd_s els_cmd;	/* ELS command code */
	u32        res1:8;
	u32        nport_id:24;	/* N_Port identifier of source */
	u32        oxid:16;
	u32        rxid:16;
	u8         assoc_hdr[32];
};

/*
 * RES els accept payload
 */
struct fc_res_acc_s {
	struct fc_els_cmd_s		els_cmd;	/* ELS command code */
	struct fc_exch_status_blk_s	fc_exch_blk; /* Exchange status block */
};

/*
 * REC els command payload
 */
struct fc_rec_s {
	struct fc_els_cmd_s els_cmd;	/* ELS command code */
	u32        res1:8;
	u32        nport_id:24;	/* N_Port identifier of source */
	u32        oxid:16;
	u32        rxid:16;
};

#define FC_REC_ESB_OWN_RSP	0x80000000	/* responder owns */
#define FC_REC_ESB_SI		0x40000000	/* SI is owned	*/
#define FC_REC_ESB_COMP		0x20000000	/* exchange is complete	*/
#define FC_REC_ESB_ENDCOND_ABN	0x10000000	/* abnormal ending	*/
#define FC_REC_ESB_RQACT	0x04000000	/* recovery qual active	*/
#define FC_REC_ESB_ERRP_MSK	0x03000000
#define FC_REC_ESB_OXID_INV	0x00800000	/* invalid OXID		*/
#define FC_REC_ESB_RXID_INV	0x00400000	/* invalid RXID		*/
#define FC_REC_ESB_PRIO_INUSE	0x00200000

/*
 * REC els accept payload
 */
struct fc_rec_acc_s {
	struct fc_els_cmd_s els_cmd;	/* ELS command code */
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
	struct fc_els_cmd_s els_cmd;
	u32        res1:8;
	u32        orig_sid:24;
	u32        oxid:16;
	u32        rxid:16;
};

/*
 * structure for PRLI paramater pages, both request & response
 * see FC-PH-X table 113 & 115 for explanation also FCP table 8
 */
struct fc_prli_params_s {
	u32        reserved:16;
#ifdef __BIGENDIAN
	u32        reserved1:5;
	u32        rec_support:1;
	u32        task_retry_id:1;
	u32        retry:1;

	u32        confirm:1;
	u32        doverlay:1;
	u32        initiator:1;
	u32        target:1;
	u32        cdmix:1;
	u32        drmix:1;
	u32        rxrdisab:1;
	u32        wxrdisab:1;
#else
	u32        retry:1;
	u32        task_retry_id:1;
	u32        rec_support:1;
	u32        reserved1:5;

	u32        wxrdisab:1;
	u32        rxrdisab:1;
	u32        drmix:1;
	u32        cdmix:1;
	u32        target:1;
	u32        initiator:1;
	u32        doverlay:1;
	u32        confirm:1;
#endif
};

/*
 * valid values for rspcode in PRLI ACC payload
 */
enum {
	FC_PRLI_ACC_XQTD = 0x1,		/* request executed */
	FC_PRLI_ACC_PREDEF_IMG = 0x5,	/* predefined image - no prli needed */
};

struct fc_prli_params_page_s {
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
	struct fc_prli_params_s servparams;
};

/*
 * PRLI request and accept payload, FC-PH-X tables 112 & 114
 */
struct fc_prli_s {
	u32        command:8;
	u32        pglen:8;
	u32        pagebytes:16;
	struct fc_prli_params_page_s parampage;
};

/*
 * PRLO logout params page
 */
struct fc_prlo_params_page_s {
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
struct fc_prlo_s {
	u32	command:8;
	u32	page_len:8;
	u32	payload_len:16;
	struct fc_prlo_params_page_s	prlo_params[1];
};

/*
 * PRLO Logout response parameter page
 */
struct fc_prlo_acc_params_page_s {
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
struct fc_prlo_acc_s {
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

struct fc_scr_s {
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
	struct fc_els_cmd_s els_cmd;		/* ELS command code */
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
struct fc_rrq_s {
	struct fc_els_cmd_s els_cmd;	/* ELS command code */
	u32        res1:8;
	u32        s_id:24;	/* exchange originator S_ID */

	u32        ox_id:16;	/* originator exchange ID */
	u32        rx_id:16;	/* responder exchange ID */

	u32        res2[8];	/* optional association header */
};

/*
 * ABTS BA_ACC reply payload
 */
struct fc_ba_acc_s {
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
struct fc_ba_rjt_s {
	u32        res1:8;		/* Reserved */
	u32        reason_code:8;	/* reason code for reject */
	u32        reason_expl:8;	/* reason code explanation */
	u32        vendor_unique:8;/* vendor unique reason code,set to 0 */
};

/*
 * TPRLO logout parameter page
 */
struct fc_tprlo_params_page_s {
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
struct fc_tprlo_s {
	u32        command:8;
	u32        page_len:8;
	u32        payload_len:16;

	struct fc_tprlo_params_page_s tprlo_params[1];
};

enum fc_tprlo_type {
	FC_GLOBAL_LOGO = 1,
	FC_TPR_LOGO
};

/*
 * TPRLO els command ACC payload
 */
struct fc_tprlo_acc_s {
	u32	command:8;
	u32	page_len:8;
	u32	payload_len:16;
	struct fc_prlo_acc_params_page_s tprlo_acc_params[1];
};

/*
 * RSCN els command req payload
 */
#define FC_RSCN_PGLEN	0x4

enum fc_rscn_format {
	FC_RSCN_FORMAT_PORTID	= 0x0,
	FC_RSCN_FORMAT_AREA	= 0x1,
	FC_RSCN_FORMAT_DOMAIN	= 0x2,
	FC_RSCN_FORMAT_FABRIC	= 0x3,
};

struct fc_rscn_event_s {
	u32        format:2;
	u32        qualifier:4;
	u32        resvd:2;
	u32        portid:24;
};

struct fc_rscn_pl_s {
	u8         command;
	u8         pagelen;
	u16        payldlen;
	struct fc_rscn_event_s event[1];
};

/*
 * ECHO els command req payload
 */
struct fc_echo_s {
	struct fc_els_cmd_s els_cmd;
};

/*
 * RNID els command
 */

#define RNID_NODEID_DATA_FORMAT_COMMON			0x00
#define RNID_NODEID_DATA_FORMAT_FCP3			0x08
#define RNID_NODEID_DATA_FORMAT_DISCOVERY		0xDF

#define RNID_ASSOCIATED_TYPE_UNKNOWN			0x00000001
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
struct fc_rnid_cmd_s {
	struct fc_els_cmd_s els_cmd;
	u32        node_id_data_format:8;
	u32        reserved:24;
};

/*
 * RNID els response payload
 */

struct fc_rnid_common_id_data_s {
	wwn_t           port_name;
	wwn_t           node_name;
};

struct fc_rnid_general_topology_data_s {
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

struct fc_rnid_acc_s {
	struct fc_els_cmd_s els_cmd;
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

enum fc_rpsc_speed_cap {
	RPSC_SPEED_CAP_1G = 0x8000,
	RPSC_SPEED_CAP_2G = 0x4000,
	RPSC_SPEED_CAP_4G = 0x2000,
	RPSC_SPEED_CAP_10G = 0x1000,
	RPSC_SPEED_CAP_8G = 0x0800,
	RPSC_SPEED_CAP_16G = 0x0400,

	RPSC_SPEED_CAP_UNKNOWN = 0x0001,
};

enum fc_rpsc_op_speed {
	RPSC_OP_SPEED_1G = 0x8000,
	RPSC_OP_SPEED_2G = 0x4000,
	RPSC_OP_SPEED_4G = 0x2000,
	RPSC_OP_SPEED_10G = 0x1000,
	RPSC_OP_SPEED_8G = 0x0800,
	RPSC_OP_SPEED_16G = 0x0400,

	RPSC_OP_SPEED_NOT_EST = 0x0001,	/*! speed not established */
};

struct fc_rpsc_speed_info_s {
	u16        port_speed_cap;	/*! see enum fc_rpsc_speed_cap */
	u16        port_op_speed;	/*! see enum fc_rpsc_op_speed */
};

enum link_e2e_beacon_subcmd {
	LINK_E2E_BEACON_ON = 1,
	LINK_E2E_BEACON_OFF = 2
};

enum beacon_type {
	BEACON_TYPE_NORMAL	= 1,	/*! Normal Beaconing. Green */
	BEACON_TYPE_WARN	= 2,	/*! Warning Beaconing. Yellow/Amber */
	BEACON_TYPE_CRITICAL	= 3	/*! Critical Beaconing. Red */
};

struct link_e2e_beacon_param_s {
	u8         beacon_type;	/* Beacon Type. See enum beacon_type */
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
 * Link E2E beacon request/good response format.
 * For LS_RJTs use struct fc_ls_rjt_s
 */
struct link_e2e_beacon_req_s {
	u32        ls_code;	/*! FC_ELS_E2E_LBEACON in requests *
					 *or FC_ELS_ACC in good replies */
	u32        ls_sub_cmd;	/*! See enum link_e2e_beacon_subcmd */
	struct link_e2e_beacon_param_s beacon_parm;
};

/*
 * If RPSC request is sent to the Domain Controller, the request is for
 * all the ports within that domain (TODO - I don't think FOS implements
 * this...).
 */
struct fc_rpsc_cmd_s {
	struct fc_els_cmd_s els_cmd;
};

/*
 * RPSC Acc
 */
struct fc_rpsc_acc_s {
	u32        command:8;
	u32        rsvd:8;
	u32        num_entries:16;

	struct fc_rpsc_speed_info_s speed_info[1];
};

/*
 * If RPSC2 request is sent to the Domain Controller,
 */
#define FC_BRCD_TOKEN    0x42524344

struct fc_rpsc2_cmd_s {
	struct fc_els_cmd_s els_cmd;
	u32	token;
	u16	resvd;
	u16	num_pids;	/* Number of pids in the request */
	struct  {
		u32	rsvd1:8;
		u32	pid:24;		/* port identifier */
	} pid_list[1];
};

enum fc_rpsc2_port_type {
	RPSC2_PORT_TYPE_UNKNOWN = 0,
	RPSC2_PORT_TYPE_NPORT   = 1,
	RPSC2_PORT_TYPE_NLPORT  = 2,
	RPSC2_PORT_TYPE_NPIV_PORT  = 0x5f,
	RPSC2_PORT_TYPE_NPORT_TRUNK  = 0x6f,
};
/*
 * RPSC2 portInfo entry structure
 */
struct fc_rpsc2_port_info_s {
    u32    pid;        /* PID */
    u16    resvd1;
    u16    index;      /* port number / index */
    u8     resvd2;
    u8	   type;	/* port type N/NL/... */
    u16    speed;      /* port Operating Speed */
};

/*
 * RPSC2 Accept payload
 */
struct fc_rpsc2_acc_s {
	u8        els_cmd;
	u8        resvd;
    u16       num_pids;  /* Number of pids in the request */
    struct fc_rpsc2_port_info_s port_info[1];    /* port information */
};

/*
 * bit fields so that multiple classes can be specified
 */
enum fc_cos {
	FC_CLASS_2	= 0x04,
	FC_CLASS_3	= 0x08,
	FC_CLASS_2_3	= 0x0C,
};

/*
 * symbolic name
 */
struct fc_symname_s {
	u8         symname[FC_SYMNAME_MAX];
};

struct fc_alpabm_s {
	u8         alpa_bm[FC_ALPA_MAX / 8];
};

/*
 * protocol default timeout values
 */
#define FC_ED_TOV		2
#define FC_REC_TOV		(FC_ED_TOV + 1)
#define FC_RA_TOV		10
#define FC_ELS_TOV		(2 * FC_RA_TOV)
#define FC_FCCT_TOV		(3 * FC_RA_TOV)

/*
 * virtual fabric related defines
 */
#define FC_VF_ID_NULL    0	/*  must not be used as VF_ID */
#define FC_VF_ID_MIN     1
#define FC_VF_ID_MAX     0xEFF
#define FC_VF_ID_CTL     0xFEF	/*  control VF_ID */

/*
 * Virtual Fabric Tagging header format
 * @caution This is defined only in BIG ENDIAN format.
 */
struct fc_vft_s {
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

/*
 * FCP
 */
enum {
	FCP_RJT		= 0x01000000,	/* SRR reject */
	FCP_SRR_ACCEPT	= 0x02000000,	/* SRR accept */
	FCP_SRR		= 0x14000000,	/* Sequence Retransmission Request */
};

/*
 * SRR FC-4 LS payload
 */
struct fc_srr_s {
	u32	ls_cmd;
	u32        ox_id:16;	/* ox-id */
	u32        rx_id:16;	/* rx-id */
	u32        ro;		/* relative offset */
	u32        r_ctl:8;		/* R_CTL for I.U. */
	u32        res:24;
};


/*
 * FCP_CMND definitions
 */
#define FCP_CMND_CDB_LEN    16
#define FCP_CMND_LUN_LEN    8

struct fcp_cmnd_s {
	lun_t           lun;		/* 64-bit LU number */
	u8         crn;		/* command reference number */
#ifdef __BIGENDIAN
	u8         resvd:1,
			priority:4,	/* FCP-3: SAM-3 priority */
			taskattr:3;	/* scsi task attribute */
#else
	u8         taskattr:3,	/* scsi task attribute */
			priority:4,	/* FCP-3: SAM-3 priority */
			resvd:1;
#endif
	u8         tm_flags;	/* task management flags */
#ifdef __BIGENDIAN
	u8         addl_cdb_len:6,	/* additional CDB length words */
			iodir:2;	/* read/write FCP_DATA IUs */
#else
	u8         iodir:2,	/* read/write FCP_DATA IUs */
			addl_cdb_len:6;	/* additional CDB length */
#endif
	scsi_cdb_t      cdb;

	/*
	 * !!! additional cdb bytes follows here!!!
	 */
	u32        fcp_dl;	/* bytes to be transferred */
};

#define fcp_cmnd_cdb_len(_cmnd) ((_cmnd)->addl_cdb_len * 4 + FCP_CMND_CDB_LEN)
#define fcp_cmnd_fcpdl(_cmnd)	((&(_cmnd)->fcp_dl)[(_cmnd)->addl_cdb_len])

/*
 * struct fcp_cmnd_s .iodir field values
 */
enum fcp_iodir {
	FCP_IODIR_NONE	= 0,
	FCP_IODIR_WRITE = 1,
	FCP_IODIR_READ	= 2,
	FCP_IODIR_RW	= 3,
};

/*
 * Task attribute field
 */
enum {
	FCP_TASK_ATTR_SIMPLE	= 0,
	FCP_TASK_ATTR_HOQ	= 1,
	FCP_TASK_ATTR_ORDERED	= 2,
	FCP_TASK_ATTR_ACA	= 4,
	FCP_TASK_ATTR_UNTAGGED	= 5,	/* obsolete in FCP-3 */
};

/*
 * Task management flags field - only one bit shall be set
 */
enum fcp_tm_cmnd {
	FCP_TM_ABORT_TASK_SET	= BIT(1),
	FCP_TM_CLEAR_TASK_SET	= BIT(2),
	FCP_TM_LUN_RESET	= BIT(4),
	FCP_TM_TARGET_RESET	= BIT(5),	/* obsolete in FCP-3 */
	FCP_TM_CLEAR_ACA	= BIT(6),
};

/*
 * FCP_XFER_RDY IU defines
 */
struct fcp_xfer_rdy_s {
	u32        data_ro;
	u32        burst_len;
	u32        reserved;
};

/*
 * FCP_RSP residue flags
 */
enum fcp_residue {
	FCP_NO_RESIDUE = 0,	/* no residue */
	FCP_RESID_OVER = 1,	/* more data left that was not sent */
	FCP_RESID_UNDER = 2,	/* less data than requested */
};

enum {
	FCP_RSPINFO_GOOD = 0,
	FCP_RSPINFO_DATALEN_MISMATCH = 1,
	FCP_RSPINFO_CMND_INVALID = 2,
	FCP_RSPINFO_ROLEN_MISMATCH = 3,
	FCP_RSPINFO_TM_NOT_SUPP = 4,
	FCP_RSPINFO_TM_FAILED = 5,
};

struct fcp_rspinfo_s {
	u32        res0:24;
	u32        rsp_code:8;	/* response code (as above) */
	u32        res1;
};

struct fcp_resp_s {
	u32        reserved[2];	/* 2 words reserved */
	u16        reserved2;
#ifdef __BIGENDIAN
	u8         reserved3:3;
	u8         fcp_conf_req:1;	/* FCP_CONF is requested */
	u8         resid_flags:2;	/* underflow/overflow */
	u8         sns_len_valid:1;/* sense len is valid */
	u8         rsp_len_valid:1;/* response len is valid */
#else
	u8         rsp_len_valid:1;/* response len is valid */
	u8         sns_len_valid:1;/* sense len is valid */
	u8         resid_flags:2;	/* underflow/overflow */
	u8         fcp_conf_req:1;	/* FCP_CONF is requested */
	u8         reserved3:3;
#endif
	u8         scsi_status;	/* one byte SCSI status */
	u32        residue;	/* residual data bytes */
	u32        sns_len;	/* length od sense info */
	u32        rsp_len;	/* length of response info */
};

#define fcp_snslen(__fcprsp)	((__fcprsp)->sns_len_valid ?		\
					(__fcprsp)->sns_len : 0)
#define fcp_rsplen(__fcprsp)	((__fcprsp)->rsp_len_valid ?		\
					(__fcprsp)->rsp_len : 0)
#define fcp_rspinfo(__fcprsp)	((struct fcp_rspinfo_s *)((__fcprsp) + 1))
#define fcp_snsinfo(__fcprsp)	(((u8 *)fcp_rspinfo(__fcprsp)) +	\
						fcp_rsplen(__fcprsp))

struct fcp_cmnd_fr_s {
	struct fchs_s fchs;
	struct fcp_cmnd_s fcp;
};

/*
 * CT
 */
struct ct_hdr_s {
	u32	rev_id:8;	/* Revision of the CT */
	u32	in_id:24;	/* Initiator Id */
	u32	gs_type:8;	/* Generic service Type */
	u32	gs_sub_type:8;	/* Generic service sub type */
	u32	options:8;	/* options */
	u32	rsvrd:8;	/* reserved */
	u32	cmd_rsp_code:16;/* ct command/response code */
	u32	max_res_size:16;/* maximum/residual size */
	u32	frag_id:8;	/* fragment ID */
	u32	reason_code:8;	/* reason code */
	u32	exp_code:8;	/* explanation code */
	u32	vendor_unq:8;	/* vendor unique */
};

/*
 * defines for the Revision
 */
enum {
	CT_GS3_REVISION = 0x01,
};

/*
 * defines for gs_type
 */
enum {
	CT_GSTYPE_KEYSERVICE	= 0xF7,
	CT_GSTYPE_ALIASSERVICE	= 0xF8,
	CT_GSTYPE_MGMTSERVICE	= 0xFA,
	CT_GSTYPE_TIMESERVICE	= 0xFB,
	CT_GSTYPE_DIRSERVICE	= 0xFC,
};

/*
 * defines for gs_sub_type for gs type directory service
 */
enum {
	CT_GSSUBTYPE_NAMESERVER = 0x02,
};

/*
 * defines for gs_sub_type for gs type management service
 */
enum {
	CT_GSSUBTYPE_CFGSERVER	= 0x01,
	CT_GSSUBTYPE_UNZONED_NS = 0x02,
	CT_GSSUBTYPE_ZONESERVER = 0x03,
	CT_GSSUBTYPE_LOCKSERVER = 0x04,
	CT_GSSUBTYPE_HBA_MGMTSERVER = 0x10,	/* for FDMI */
};

/*
 * defines for CT response code field
 */
enum {
	CT_RSP_REJECT = 0x8001,
	CT_RSP_ACCEPT = 0x8002,
};

/*
 * defintions for CT reason code
 */
enum {
	CT_RSN_INV_CMD		= 0x01,
	CT_RSN_INV_VER		= 0x02,
	CT_RSN_LOGIC_ERR	= 0x03,
	CT_RSN_INV_SIZE		= 0x04,
	CT_RSN_LOGICAL_BUSY	= 0x05,
	CT_RSN_PROTO_ERR	= 0x07,
	CT_RSN_UNABLE_TO_PERF	= 0x09,
	CT_RSN_NOT_SUPP			= 0x0B,
	CT_RSN_SERVER_NOT_AVBL  = 0x0D,
	CT_RSN_SESSION_COULD_NOT_BE_ESTBD = 0x0E,
	CT_RSN_VENDOR_SPECIFIC  = 0xFF,

};

/*
 * definitions for explanations code for Name server
 */
enum {
	CT_NS_EXP_NOADDITIONAL	= 0x00,
	CT_NS_EXP_ID_NOT_REG	= 0x01,
	CT_NS_EXP_PN_NOT_REG	= 0x02,
	CT_NS_EXP_NN_NOT_REG	= 0x03,
	CT_NS_EXP_CS_NOT_REG	= 0x04,
	CT_NS_EXP_IPN_NOT_REG	= 0x05,
	CT_NS_EXP_IPA_NOT_REG	= 0x06,
	CT_NS_EXP_FT_NOT_REG	= 0x07,
	CT_NS_EXP_SPN_NOT_REG	= 0x08,
	CT_NS_EXP_SNN_NOT_REG	= 0x09,
	CT_NS_EXP_PT_NOT_REG	= 0x0A,
	CT_NS_EXP_IPP_NOT_REG	= 0x0B,
	CT_NS_EXP_FPN_NOT_REG	= 0x0C,
	CT_NS_EXP_HA_NOT_REG	= 0x0D,
	CT_NS_EXP_FD_NOT_REG	= 0x0E,
	CT_NS_EXP_FF_NOT_REG	= 0x0F,
	CT_NS_EXP_ACCESSDENIED	= 0x10,
	CT_NS_EXP_UNACCEPTABLE_ID = 0x11,
	CT_NS_EXP_DATABASEEMPTY		= 0x12,
	CT_NS_EXP_NOT_REG_IN_SCOPE	= 0x13,
	CT_NS_EXP_DOM_ID_NOT_PRESENT	= 0x14,
	CT_NS_EXP_PORT_NUM_NOT_PRESENT	= 0x15,
	CT_NS_EXP_NO_DEVICE_ATTACHED	= 0x16
};

/*
 * defintions for the explanation code for all servers
 */
enum {
	CT_EXP_AUTH_EXCEPTION			= 0xF1,
	CT_EXP_DB_FULL					= 0xF2,
	CT_EXP_DB_EMPTY					= 0xF3,
	CT_EXP_PROCESSING_REQ			= 0xF4,
	CT_EXP_UNABLE_TO_VERIFY_CONN	= 0xF5,
	CT_EXP_DEVICES_NOT_IN_CMN_ZONE  = 0xF6
};

/*
 * Command codes for Name server
 */
enum {
	GS_GID_PN	= 0x0121,	/* Get Id on port name */
	GS_GPN_ID	= 0x0112,	/* Get port name on ID */
	GS_GNN_ID	= 0x0113,	/* Get node name on ID */
	GS_GID_FT	= 0x0171,	/* Get Id on FC4 type */
	GS_GSPN_ID	= 0x0118,	/* Get symbolic PN on ID */
	GS_RFT_ID	= 0x0217,	/* Register fc4type on ID */
	GS_RSPN_ID	= 0x0218,	/* Register symbolic PN on ID */
	GS_RPN_ID	= 0x0212,	/* Register port name */
	GS_RNN_ID	= 0x0213,	/* Register node name */
	GS_RCS_ID	= 0x0214,	/* Register class of service */
	GS_RPT_ID	= 0x021A,	/* Register port type */
	GS_GA_NXT	= 0x0100,	/* Get all next */
	GS_RFF_ID	= 0x021F,	/* Register FC4 Feature		*/
};

struct fcgs_id_req_s{
	u32 rsvd:8;
	u32 dap:24; /* port identifier */
};
#define fcgs_gpnid_req_t struct fcgs_id_req_s
#define fcgs_gnnid_req_t struct fcgs_id_req_s
#define fcgs_gspnid_req_t struct fcgs_id_req_s

struct fcgs_gidpn_req_s {
	wwn_t	port_name;	/* port wwn */
};

struct fcgs_gidpn_resp_s {
	u32	rsvd:8;
	u32	dap:24;	/* port identifier */
};

/*
 * RFT_ID
 */
struct fcgs_rftid_req_s {
	u32	rsvd:8;
	u32	dap:24;		/* port identifier */
	u32	fc4_type[8];	/* fc4 types */
};

/*
 * RFF_ID : Register FC4 features.
 */

#define FC_GS_FCP_FC4_FEATURE_INITIATOR  0x02
#define FC_GS_FCP_FC4_FEATURE_TARGET	 0x01

struct fcgs_rffid_req_s {
    u32    rsvd:8;
    u32    dap:24;		/* port identifier	*/
    u32    rsvd1:16;
    u32    fc4ftr_bits:8;		/* fc4 feature bits	*/
    u32    fc4_type:8;		/* corresponding FC4 Type */
};

/*
 * GID_FT Request
 */
struct fcgs_gidft_req_s {
	u8	reserved;
	u8	domain_id;	/* domain, 0 - all fabric */
	u8	area_id;	/* area, 0 - whole domain */
	u8	fc4_type;	/* FC_TYPE_FCP for SCSI devices */
};		/* GID_FT Request */

/*
 * GID_FT Response
 */
struct fcgs_gidft_resp_s {
	u8		last:1;	/* last port identifier flag */
	u8		reserved:7;
	u32	pid:24;	/* port identifier */
};		/* GID_FT Response */

/*
 * RSPN_ID
 */
struct fcgs_rspnid_req_s {
	u32	rsvd:8;
	u32	dap:24;		/* port identifier */
	u8		spn_len;	/* symbolic port name length */
	u8		spn[256];	/* symbolic port name */
};

/*
 * RPN_ID
 */
struct fcgs_rpnid_req_s {
	u32	rsvd:8;
	u32	port_id:24;
	wwn_t		port_name;
};

/*
 * RNN_ID
 */
struct fcgs_rnnid_req_s {
	u32	rsvd:8;
	u32	port_id:24;
	wwn_t		node_name;
};

/*
 * RCS_ID
 */
struct fcgs_rcsid_req_s {
	u32	rsvd:8;
	u32	port_id:24;
	u32	cos;
};

/*
 * RPT_ID
 */
struct fcgs_rptid_req_s {
	u32	rsvd:8;
	u32	port_id:24;
	u32	port_type:8;
	u32	rsvd1:24;
};

/*
 * GA_NXT Request
 */
struct fcgs_ganxt_req_s {
	u32	rsvd:8;
	u32	port_id:24;
};

/*
 * GA_NXT Response
 */
struct fcgs_ganxt_rsp_s {
	u32	port_type:8;	/* Port Type */
	u32	port_id:24;	/* Port Identifier */
	wwn_t		port_name;	/* Port Name */
	u8		spn_len;	/* Length of Symbolic Port Name */
	char		spn[255];	/* Symbolic Port Name */
	wwn_t		node_name;	/* Node Name */
	u8		snn_len;	/* Length of Symbolic Node Name */
	char		snn[255];	/* Symbolic Node Name */
	u8		ipa[8];		/* Initial Process Associator */
	u8		ip[16];		/* IP Address */
	u32	cos;		/* Class of Service */
	u32	fc4types[8];	/* FC-4 TYPEs */
	wwn_t		fabric_port_name;
					/* Fabric Port Name */
	u32	rsvd:8;		/* Reserved */
	u32	hard_addr:24;	/* Hard Address */
};

/*
 * Fabric Config Server
 */

/*
 * Command codes for Fabric Configuration Server
 */
enum {
	GS_FC_GFN_CMD	= 0x0114,	/* GS FC Get Fabric Name  */
	GS_FC_GMAL_CMD	= 0x0116,	/* GS FC GMAL  */
	GS_FC_TRACE_CMD	= 0x0400,	/* GS FC Trace Route */
	GS_FC_PING_CMD	= 0x0401,	/* GS FC Ping */
};

/*
 * Source or Destination Port Tags.
 */
enum {
	GS_FTRACE_TAG_NPORT_ID		= 1,
	GS_FTRACE_TAG_NPORT_NAME	= 2,
};

/*
* Port Value : Could be a Port id or wwn
 */
union fcgs_port_val_u {
	u32	nport_id;
	wwn_t		nport_wwn;
};

#define GS_FTRACE_MAX_HOP_COUNT	20
#define GS_FTRACE_REVISION	1

/*
 * Ftrace Related Structures.
 */

/*
 * STR (Switch Trace) Reject Reason Codes. From FC-SW.
 */
enum {
	GS_FTRACE_STR_CMD_COMPLETED_SUCC	= 0,
	GS_FTRACE_STR_CMD_NOT_SUPP_IN_NEXT_SWITCH,
	GS_FTRACE_STR_NO_RESP_FROM_NEXT_SWITCH,
	GS_FTRACE_STR_MAX_HOP_CNT_REACHED,
	GS_FTRACE_STR_SRC_PORT_NOT_FOUND,
	GS_FTRACE_STR_DST_PORT_NOT_FOUND,
	GS_FTRACE_STR_DEVICES_NOT_IN_COMMON_ZONE,
	GS_FTRACE_STR_NO_ROUTE_BW_PORTS,
	GS_FTRACE_STR_NO_ADDL_EXPLN,
	GS_FTRACE_STR_FABRIC_BUSY,
	GS_FTRACE_STR_FABRIC_BUILD_IN_PROGRESS,
	GS_FTRACE_STR_VENDOR_SPECIFIC_ERR_START = 0xf0,
	GS_FTRACE_STR_VENDOR_SPECIFIC_ERR_END = 0xff,
};

/*
 * Ftrace Request
 */
struct fcgs_ftrace_req_s {
	u32	revision;
	u16	src_port_tag;	/* Source Port tag */
	u16	src_port_len;	/* Source Port len */
	union fcgs_port_val_u src_port_val;	/* Source Port value */
	u16	dst_port_tag;	/* Destination Port tag */
	u16	dst_port_len;	/* Destination Port len */
	union fcgs_port_val_u dst_port_val;	/* Destination Port value */
	u32	token;
	u8		vendor_id[8];	/* T10 Vendor Identifier */
	u8		vendor_info[8];	/* Vendor specific Info */
	u32	max_hop_cnt;	/* Max Hop Count */
};

/*
 * Path info structure
 */
struct fcgs_ftrace_path_info_s {
	wwn_t		switch_name;		/* Switch WWN */
	u32	domain_id;
	wwn_t		ingress_port_name;	/* Ingress ports wwn */
	u32	ingress_phys_port_num;	/* Ingress ports physical port
						 * number
						 */
	wwn_t		egress_port_name;	/* Ingress ports wwn */
	u32	egress_phys_port_num;	/* Ingress ports physical port
						 * number
						 */
};

/*
 * Ftrace Acc Response
 */
struct fcgs_ftrace_resp_s {
	u32	revision;
	u32	token;
	u8		vendor_id[8];		/* T10 Vendor Identifier */
	u8		vendor_info[8];		/* Vendor specific Info */
	u32	str_rej_reason_code;	/* STR Reject Reason Code */
	u32	num_path_info_entries;	/* No. of path info entries */
	/*
	 * path info entry/entries.
	 */
	struct fcgs_ftrace_path_info_s path_info[1];

};

/*
* Fabric Config Server : FCPing
 */

/*
 * FC Ping Request
 */
struct fcgs_fcping_req_s {
	u32	revision;
	u16	port_tag;
	u16	port_len;	/* Port len */
	union fcgs_port_val_u port_val;	/* Port value */
	u32	token;
};

/*
 * FC Ping Response
 */
struct fcgs_fcping_resp_s {
	u32	token;
};

/*
 * Command codes for zone server query.
 */
enum {
	ZS_GZME = 0x0124,	/* Get zone member extended */
};

/*
 * ZS GZME request
 */
#define ZS_GZME_ZNAMELEN	32
struct zs_gzme_req_s {
	u8	znamelen;
	u8	rsvd[3];
	u8	zname[ZS_GZME_ZNAMELEN];
};

enum zs_mbr_type {
	ZS_MBR_TYPE_PWWN	= 1,
	ZS_MBR_TYPE_DOMPORT	= 2,
	ZS_MBR_TYPE_PORTID	= 3,
	ZS_MBR_TYPE_NWWN	= 4,
};

struct zs_mbr_wwn_s {
	u8	mbr_type;
	u8	rsvd[3];
	wwn_t	wwn;
};

struct zs_query_resp_s {
	u32	nmbrs;	/*  number of zone members */
	struct zs_mbr_wwn_s	mbr[1];
};

/*
 * GMAL Command ( Get ( interconnect Element) Management Address List)
 * To retrieve the IP Address of a Switch.
 */

#define CT_GMAL_RESP_PREFIX_TELNET	 "telnet://"
#define CT_GMAL_RESP_PREFIX_HTTP	 "http://"

/*  GMAL/GFN request */
struct fcgs_req_s {
	wwn_t    wwn;   /* PWWN/NWWN */
};

#define fcgs_gmal_req_t struct fcgs_req_s
#define fcgs_gfn_req_t struct fcgs_req_s

/* Accept Response to GMAL */
struct fcgs_gmal_resp_s {
	u32	ms_len;   /* Num of entries */
	u8	ms_ma[256];
};

struct fcgs_gmal_entry_s {
	u8  len;
	u8  prefix[7]; /* like "http://" */
	u8  ip_addr[248];
};

/*
 * FDMI
 */
/*
 * FDMI Command Codes
 */
#define	FDMI_GRHL		0x0100
#define	FDMI_GHAT		0x0101
#define	FDMI_GRPL		0x0102
#define	FDMI_GPAT		0x0110
#define	FDMI_RHBA		0x0200
#define	FDMI_RHAT		0x0201
#define	FDMI_RPRT		0x0210
#define	FDMI_RPA		0x0211
#define	FDMI_DHBA		0x0300
#define	FDMI_DPRT		0x0310

/*
 * FDMI reason codes
 */
#define	FDMI_NO_ADDITIONAL_EXP		0x00
#define	FDMI_HBA_ALREADY_REG		0x10
#define	FDMI_HBA_ATTRIB_NOT_REG		0x11
#define	FDMI_HBA_ATTRIB_MULTIPLE	0x12
#define	FDMI_HBA_ATTRIB_LENGTH_INVALID	0x13
#define	FDMI_HBA_ATTRIB_NOT_PRESENT	0x14
#define	FDMI_PORT_ORIG_NOT_IN_LIST	0x15
#define	FDMI_PORT_HBA_NOT_IN_LIST	0x16
#define	FDMI_PORT_ATTRIB_NOT_REG	0x20
#define	FDMI_PORT_NOT_REG		0x21
#define	FDMI_PORT_ATTRIB_MULTIPLE	0x22
#define	FDMI_PORT_ATTRIB_LENGTH_INVALID	0x23
#define	FDMI_PORT_ALREADY_REGISTEREED	0x24

/*
 * FDMI Transmission Speed Mask values
 */
#define	FDMI_TRANS_SPEED_1G		0x00000001
#define	FDMI_TRANS_SPEED_2G		0x00000002
#define	FDMI_TRANS_SPEED_10G		0x00000004
#define	FDMI_TRANS_SPEED_4G		0x00000008
#define	FDMI_TRANS_SPEED_8G		0x00000010
#define	FDMI_TRANS_SPEED_16G		0x00000020
#define	FDMI_TRANS_SPEED_UNKNOWN	0x00008000

/*
 * FDMI HBA attribute types
 */
enum fdmi_hba_attribute_type {
	FDMI_HBA_ATTRIB_NODENAME = 1,	/* 0x0001 */
	FDMI_HBA_ATTRIB_MANUFACTURER,	/* 0x0002 */
	FDMI_HBA_ATTRIB_SERIALNUM,	/* 0x0003 */
	FDMI_HBA_ATTRIB_MODEL,		/* 0x0004 */
	FDMI_HBA_ATTRIB_MODEL_DESC,	/* 0x0005 */
	FDMI_HBA_ATTRIB_HW_VERSION,	/* 0x0006 */
	FDMI_HBA_ATTRIB_DRIVER_VERSION,	/* 0x0007 */
	FDMI_HBA_ATTRIB_ROM_VERSION,	/* 0x0008 */
	FDMI_HBA_ATTRIB_FW_VERSION,	/* 0x0009 */
	FDMI_HBA_ATTRIB_OS_NAME,	/* 0x000A */
	FDMI_HBA_ATTRIB_MAX_CT,		/* 0x000B */

	FDMI_HBA_ATTRIB_MAX_TYPE
};

/*
 * FDMI Port attribute types
 */
enum fdmi_port_attribute_type {
	FDMI_PORT_ATTRIB_FC4_TYPES = 1,	/* 0x0001 */
	FDMI_PORT_ATTRIB_SUPP_SPEED,	/* 0x0002 */
	FDMI_PORT_ATTRIB_PORT_SPEED,	/* 0x0003 */
	FDMI_PORT_ATTRIB_FRAME_SIZE,	/* 0x0004 */
	FDMI_PORT_ATTRIB_DEV_NAME,	/* 0x0005 */
	FDMI_PORT_ATTRIB_HOST_NAME,	/* 0x0006 */

	FDMI_PORT_ATTR_MAX_TYPE
};

/*
 * FDMI attribute
 */
struct fdmi_attr_s {
	u16        type;
	u16        len;
	u8         value[1];
};

/*
 * HBA Attribute Block
 */
struct fdmi_hba_attr_s {
	u32        attr_count;	/* # of attributes */
	struct fdmi_attr_s hba_attr;	/* n attributes */
};

/*
 * Registered Port List
 */
struct fdmi_port_list_s {
	u32        num_ports;	/* number Of Port Entries */
	wwn_t           port_entry;	/* one or more */
};

/*
 * Port Attribute Block
 */
struct fdmi_port_attr_s {
	u32        attr_count;	/* # of attributes */
	struct fdmi_attr_s port_attr;	/* n attributes */
};

/*
 * FDMI Register HBA Attributes
 */
struct fdmi_rhba_s {
	wwn_t           hba_id;		/* HBA Identifier */
	struct fdmi_port_list_s port_list;	/* Registered Port List */
	struct fdmi_hba_attr_s hba_attr_blk;	/* HBA attribute block */
};

/*
 * FDMI Register Port
 */
struct fdmi_rprt_s {
	wwn_t           hba_id;		/* HBA Identifier */
	wwn_t           port_name;	/* Port wwn */
	struct fdmi_port_attr_s port_attr_blk;	/* Port Attr Block */
};

/*
 * FDMI Register Port Attributes
 */
struct fdmi_rpa_s {
	wwn_t           port_name;	/* port wwn */
	struct fdmi_port_attr_s port_attr_blk;	/* Port Attr Block */
};

#pragma pack()

#endif	/* __BFA_FC_H__ */
