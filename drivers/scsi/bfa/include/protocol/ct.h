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

#ifndef __CT_H__
#define __CT_H__

#include <protocol/types.h>

#pragma pack(1)

struct ct_hdr_s{
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
 * definitions for CT reason code
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
	CT_NS_EXP_DATABASEEMPTY			= 0x12,
	CT_NS_EXP_NOT_REG_IN_SCOPE 		= 0x13,
	CT_NS_EXP_DOM_ID_NOT_PRESENT 	= 0x14,
	CT_NS_EXP_PORT_NUM_NOT_PRESENT  = 0x15,
	CT_NS_EXP_NO_DEVICE_ATTACHED 	= 0x16
};

/*
 * definitions for the explanation code for all servers
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
	u32	rsvd:8;
	u32	dap:24;	/* port identifier */
};
#define fcgs_gpnid_req_t struct fcgs_id_req_s
#define fcgs_gnnid_req_t struct fcgs_id_req_s
#define fcgs_gspnid_req_t struct fcgs_id_req_s

struct fcgs_gidpn_req_s{
	wwn_t	port_name;	/* port wwn */
};

struct fcgs_gidpn_resp_s{
	u32	rsvd:8;
	u32	dap:24;	/* port identifier */
};

/**
 * RFT_ID
 */
struct fcgs_rftid_req_s {
	u32	rsvd:8;
	u32	dap:24;		/* port identifier */
	u32	fc4_type[8];	/* fc4 types */
};

/**
 * RFF_ID : Register FC4 features.
 */

#define FC_GS_FCP_FC4_FEATURE_INITIATOR  0x02
#define FC_GS_FCP_FC4_FEATURE_TARGET	 0x01

struct fcgs_rffid_req_s{
    u32    rsvd:8;
    u32    dap:24;		/* port identifier	*/
    u32    rsvd1:16;
    u32    fc4ftr_bits:8;	/* fc4 feature bits	*/
    u32    fc4_type:8;	/* corresponding FC4 Type */
};

/**
 * GID_FT Request
 */
struct fcgs_gidft_req_s{
	u8	reserved;
	u8	domain_id;	/* domain, 0 - all fabric */
	u8	area_id;	/* area, 0 - whole domain */
	u8	fc4_type;	/* FC_TYPE_FCP for SCSI devices */
};				/* GID_FT Request */

/**
 * GID_FT Response
 */
struct fcgs_gidft_resp_s {
	u8		last:1;	/* last port identifier flag */
	u8		reserved:7;
	u32	pid:24;	/* port identifier */
};				/* GID_FT Response */

/**
 * RSPN_ID
 */
struct fcgs_rspnid_req_s{
	u32	rsvd:8;
	u32	dap:24;		/* port identifier */
	u8		spn_len;	/* symbolic port name length */
	u8		spn[256];	/* symbolic port name */
};

/**
 * RPN_ID
 */
struct fcgs_rpnid_req_s{
	u32	rsvd:8;
	u32	port_id:24;
	wwn_t		port_name;
};

/**
 * RNN_ID
 */
struct fcgs_rnnid_req_s{
	u32	rsvd:8;
	u32	port_id:24;
	wwn_t		node_name;
};

/**
 * RCS_ID
 */
struct fcgs_rcsid_req_s{
	u32	rsvd:8;
	u32	port_id:24;
	u32	cos;
};

/**
 * RPT_ID
 */
struct fcgs_rptid_req_s{
	u32	rsvd:8;
	u32	port_id:24;
	u32	port_type:8;
	u32	rsvd1:24;
};

/**
 * GA_NXT Request
 */
struct fcgs_ganxt_req_s{
	u32	rsvd:8;
	u32	port_id:24;
};

/**
 * GA_NXT Response
 */
struct fcgs_ganxt_rsp_s{
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
union fcgs_port_val_u{
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
struct fcgs_ftrace_req_s{
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
struct fcgs_ftrace_path_info_s{
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
struct fcgs_ftrace_resp_s{
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
struct fcgs_fcping_req_s{
	u32	revision;
	u16	port_tag;
	u16	port_len;	/* Port len */
	union fcgs_port_val_u port_val;	/* Port value */
	u32	token;
};

/*
 * FC Ping Response
 */
struct fcgs_fcping_resp_s{
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
struct zs_gzme_req_s{
	u8	znamelen;
	u8	rsvd[3];
	u8	zname[ZS_GZME_ZNAMELEN];
};

enum zs_mbr_type{
	ZS_MBR_TYPE_PWWN	= 1,
	ZS_MBR_TYPE_DOMPORT	= 2,
	ZS_MBR_TYPE_PORTID	= 3,
	ZS_MBR_TYPE_NWWN	= 4,
};

struct zs_mbr_wwn_s{
	u8	mbr_type;
	u8	rsvd[3];
	wwn_t	wwn;
};

struct zs_query_resp_s{
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
	wwn_t    wwn; 	/* PWWN/NWWN */
};

#define fcgs_gmal_req_t struct fcgs_req_s
#define fcgs_gfn_req_t struct fcgs_req_s

/* Accept Response to GMAL */
struct fcgs_gmal_resp_s {
	u32 		ms_len;   /* Num of entries */
	u8     	ms_ma[256];
};

struct fc_gmal_entry_s {
	u8  len;
	u8  prefix[7]; /* like "http://" */
	u8  ip_addr[248];
};

#pragma pack()

#endif
