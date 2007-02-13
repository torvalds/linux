/*
 * File Name:
 *   defxx.h
 *
 * Copyright Information:
 *   Copyright Digital Equipment Corporation 1996.
 *
 *   This software may be used and distributed according to the terms of
 *   the GNU General Public License, incorporated herein by reference.
 *
 * Abstract:
 *   Contains all definitions specified by port specification and required
 *   by the defxx.c driver.
 *
 * The original author:
 *   LVS	Lawrence V. Stefani <lstefani@yahoo.com>
 *
 * Maintainers:
 *   macro	Maciej W. Rozycki <macro@linux-mips.org>
 *
 * Modification History:
 *		Date		Name	Description
 *		16-Aug-96	LVS		Created.
 *		09-Sep-96	LVS		Added group_prom field.  Moved read/write I/O
 *							macros to DEFXX.C.
 *		12-Sep-96	LVS		Removed packet request header pointers.
 *		04 Aug 2003	macro		Converted to the DMA API.
 *		23 Oct 2006	macro		Big-endian host support.
 *		14 Dec 2006	macro		TURBOchannel support.
 */

#ifndef _DEFXX_H_
#define _DEFXX_H_

/* Define basic types for unsigned chars, shorts, longs */

typedef u8	PI_UINT8;
typedef u16	PI_UINT16;
typedef u32	PI_UINT32;

/* Define general structures */

typedef struct							/* 64-bit counter */
	{
	PI_UINT32  ms;
	PI_UINT32  ls;
	} PI_CNTR;

typedef struct							/* LAN address */
	{
	PI_UINT32  lwrd_0;
	PI_UINT32  lwrd_1;
	} PI_LAN_ADDR;

typedef struct							/* Station ID address */
	{
	PI_UINT32  octet_7_4;
	PI_UINT32  octet_3_0;
	} PI_STATION_ID;


/* Define general constants */

#define PI_ALIGN_K_DESC_BLK	  			8192	/* Descriptor block boundary		*/
#define PI_ALIGN_K_CONS_BLK	  	 		64		/* Consumer block boundary		  	*/
#define PI_ALIGN_K_CMD_REQ_BUFF  		128	 	/* Xmt Command que buffer alignment */
#define PI_ALIGN_K_CMD_RSP_BUFF	 		128	 	/* Rcv Command que buffer alignment */
#define PI_ALIGN_K_UNSOL_BUFF	 		128	 	/* Unsol que buffer alignment	   	*/
#define PI_ALIGN_K_XMT_DATA_BUFF 		0	   	/* Xmt data que buffer alignment	*/
#define PI_ALIGN_K_RCV_DATA_BUFF 		128	 	/* Rcv que buffer alignment			*/

/* Define PHY index values */

#define PI_PHY_K_S						0		/* Index to S phy */
#define PI_PHY_K_A						0		/* Index to A phy */
#define PI_PHY_K_B						1		/* Index to B phy */
#define PI_PHY_K_MAX					2		/* Max number of phys */

/* Define FMC descriptor fields */

#define PI_FMC_DESCR_V_SOP				31
#define PI_FMC_DESCR_V_EOP				30
#define PI_FMC_DESCR_V_FSC				27
#define PI_FMC_DESCR_V_FSB_ERROR		26
#define PI_FMC_DESCR_V_FSB_ADDR_RECOG	25
#define PI_FMC_DESCR_V_FSB_ADDR_COPIED	24
#define PI_FMC_DESCR_V_FSB				22
#define PI_FMC_DESCR_V_RCC_FLUSH		21
#define PI_FMC_DESCR_V_RCC_CRC			20
#define PI_FMC_DESCR_V_RCC_RRR			17
#define PI_FMC_DESCR_V_RCC_DD			15
#define PI_FMC_DESCR_V_RCC_SS			13
#define PI_FMC_DESCR_V_RCC				13
#define PI_FMC_DESCR_V_LEN				0

#define PI_FMC_DESCR_M_SOP				0x80000000
#define PI_FMC_DESCR_M_EOP				0x40000000
#define PI_FMC_DESCR_M_FSC				0x38000000
#define PI_FMC_DESCR_M_FSB_ERROR		0x04000000
#define PI_FMC_DESCR_M_FSB_ADDR_RECOG	0x02000000
#define PI_FMC_DESCR_M_FSB_ADDR_COPIED	0x01000000
#define PI_FMC_DESCR_M_FSB				0x07C00000
#define PI_FMC_DESCR_M_RCC_FLUSH		0x00200000
#define PI_FMC_DESCR_M_RCC_CRC			0x00100000
#define PI_FMC_DESCR_M_RCC_RRR			0x000E0000
#define PI_FMC_DESCR_M_RCC_DD			0x00018000
#define PI_FMC_DESCR_M_RCC_SS			0x00006000
#define PI_FMC_DESCR_M_RCC				0x003FE000
#define PI_FMC_DESCR_M_LEN				0x00001FFF

#define PI_FMC_DESCR_K_RCC_FMC_INT_ERR	0x01AA

#define PI_FMC_DESCR_K_RRR_SUCCESS		0x00
#define PI_FMC_DESCR_K_RRR_SA_MATCH		0x01
#define PI_FMC_DESCR_K_RRR_DA_MATCH		0x02
#define PI_FMC_DESCR_K_RRR_FMC_ABORT	0x03
#define PI_FMC_DESCR_K_RRR_LENGTH_BAD	0x04
#define PI_FMC_DESCR_K_RRR_FRAGMENT		0x05
#define PI_FMC_DESCR_K_RRR_FORMAT_ERR	0x06
#define PI_FMC_DESCR_K_RRR_MAC_RESET	0x07

#define PI_FMC_DESCR_K_DD_NO_MATCH		0x0
#define PI_FMC_DESCR_K_DD_PROMISCUOUS	0x1
#define PI_FMC_DESCR_K_DD_CAM_MATCH		0x2
#define PI_FMC_DESCR_K_DD_LOCAL_MATCH	0x3

#define PI_FMC_DESCR_K_SS_NO_MATCH		0x0
#define PI_FMC_DESCR_K_SS_BRIDGE_MATCH	0x1
#define PI_FMC_DESCR_K_SS_NOT_POSSIBLE	0x2
#define PI_FMC_DESCR_K_SS_LOCAL_MATCH	0x3

/* Define some max buffer sizes */

#define PI_CMD_REQ_K_SIZE_MAX			512
#define PI_CMD_RSP_K_SIZE_MAX			512
#define PI_UNSOL_K_SIZE_MAX				512
#define PI_SMT_HOST_K_SIZE_MAX			4608		/* 4 1/2 K */
#define PI_RCV_DATA_K_SIZE_MAX			4608		/* 4 1/2 K */
#define PI_XMT_DATA_K_SIZE_MAX			4608		/* 4 1/2 K */

/* Define adapter states */

#define PI_STATE_K_RESET				0
#define PI_STATE_K_UPGRADE		  		1
#define PI_STATE_K_DMA_UNAVAIL			2
#define PI_STATE_K_DMA_AVAIL			3
#define PI_STATE_K_LINK_AVAIL			4
#define PI_STATE_K_LINK_UNAVAIL	 		5
#define PI_STATE_K_HALTED		   		6
#define PI_STATE_K_RING_MEMBER			7
#define PI_STATE_K_NUMBER				8

/* Define codes for command type */

#define PI_CMD_K_START					0x00
#define PI_CMD_K_FILTERS_SET			0x01
#define PI_CMD_K_FILTERS_GET			0x02
#define PI_CMD_K_CHARS_SET				0x03
#define PI_CMD_K_STATUS_CHARS_GET		0x04
#define PI_CMD_K_CNTRS_GET				0x05
#define PI_CMD_K_CNTRS_SET				0x06
#define PI_CMD_K_ADDR_FILTER_SET		0x07
#define PI_CMD_K_ADDR_FILTER_GET		0x08
#define PI_CMD_K_ERROR_LOG_CLEAR		0x09
#define PI_CMD_K_ERROR_LOG_GET			0x0A
#define PI_CMD_K_FDDI_MIB_GET			0x0B
#define PI_CMD_K_DEC_EXT_MIB_GET		0x0C
#define PI_CMD_K_DEVICE_SPECIFIC_GET	0x0D
#define PI_CMD_K_SNMP_SET				0x0E
#define PI_CMD_K_UNSOL_TEST				0x0F
#define PI_CMD_K_SMT_MIB_GET			0x10
#define PI_CMD_K_SMT_MIB_SET			0x11
#define PI_CMD_K_MAX					0x11	/* Must match last */

/* Define item codes for Chars_Set and Filters_Set commands */

#define PI_ITEM_K_EOL					0x00 	/* End-of-Item list 		  */
#define PI_ITEM_K_T_REQ					0x01 	/* DECnet T_REQ 			  */
#define PI_ITEM_K_TVX					0x02 	/* DECnet TVX 				  */
#define PI_ITEM_K_RESTRICTED_TOKEN		0x03 	/* DECnet Restricted Token 	  */
#define PI_ITEM_K_LEM_THRESHOLD			0x04 	/* DECnet LEM Threshold 	  */
#define PI_ITEM_K_RING_PURGER			0x05 	/* DECnet Ring Purger Enable  */
#define PI_ITEM_K_CNTR_INTERVAL			0x06 	/* Chars_Set 				  */
#define PI_ITEM_K_IND_GROUP_PROM		0x07 	/* Filters_Set 				  */
#define PI_ITEM_K_GROUP_PROM			0x08 	/* Filters_Set 				  */
#define PI_ITEM_K_BROADCAST				0x09 	/* Filters_Set 				  */
#define PI_ITEM_K_SMT_PROM				0x0A 	/* Filters_Set				  */
#define PI_ITEM_K_SMT_USER				0x0B 	/* Filters_Set 				  */
#define PI_ITEM_K_RESERVED				0x0C 	/* Filters_Set 				  */
#define PI_ITEM_K_IMPLEMENTOR			0x0D 	/* Filters_Set 				  */
#define PI_ITEM_K_LOOPBACK_MODE			0x0E 	/* Chars_Set 				  */
#define PI_ITEM_K_CONFIG_POLICY			0x10 	/* SMTConfigPolicy 			  */
#define PI_ITEM_K_CON_POLICY			0x11 	/* SMTConnectionPolicy 		  */
#define PI_ITEM_K_T_NOTIFY				0x12 	/* SMTTNotify 				  */
#define PI_ITEM_K_STATION_ACTION		0x13 	/* SMTStationAction			  */
#define PI_ITEM_K_MAC_PATHS_REQ	   		0x15 	/* MACPathsRequested 		  */
#define PI_ITEM_K_MAC_ACTION			0x17 	/* MACAction 				  */
#define PI_ITEM_K_CON_POLICIES			0x18 	/* PORTConnectionPolicies	  */
#define PI_ITEM_K_PORT_PATHS_REQ		0x19 	/* PORTPathsRequested 		  */
#define PI_ITEM_K_MAC_LOOP_TIME			0x1A 	/* PORTMACLoopTime 			  */
#define PI_ITEM_K_TB_MAX				0x1B 	/* PORTTBMax 				  */
#define PI_ITEM_K_LER_CUTOFF			0x1C 	/* PORTLerCutoff 			  */
#define PI_ITEM_K_LER_ALARM				0x1D 	/* PORTLerAlarm 			  */
#define PI_ITEM_K_PORT_ACTION			0x1E 	/* PORTAction 				  */
#define PI_ITEM_K_FLUSH_TIME			0x20 	/* Chars_Set 				  */
#define PI_ITEM_K_MAC_T_REQ				0x29 	/* MACTReq 					  */
#define PI_ITEM_K_EMAC_RING_PURGER		0x2A 	/* eMACRingPurgerEnable		  */
#define PI_ITEM_K_EMAC_RTOKEN_TIMEOUT	0x2B 	/* eMACRestrictedTokenTimeout */
#define PI_ITEM_K_FDX_ENB_DIS			0x2C 	/* eFDXEnable				  */
#define PI_ITEM_K_MAX					0x2C 	/* Must equal high item		  */

/* Values for some of the items */

#define PI_K_FALSE						0	   /* Generic false */
#define PI_K_TRUE						1	   /* Generic true  */

#define PI_SNMP_K_TRUE					1	   /* SNMP true/false values */
#define PI_SNMP_K_FALSE					2

#define PI_FSTATE_K_BLOCK				0	   /* Filter State */
#define PI_FSTATE_K_PASS				1

/* Define command return codes */

#define PI_RSP_K_SUCCESS				0x00
#define PI_RSP_K_FAILURE				0x01
#define PI_RSP_K_WARNING				0x02
#define PI_RSP_K_LOOP_MODE_BAD			0x03
#define PI_RSP_K_ITEM_CODE_BAD			0x04
#define PI_RSP_K_TVX_BAD				0x05
#define PI_RSP_K_TREQ_BAD				0x06
#define PI_RSP_K_TOKEN_BAD				0x07
#define PI_RSP_K_NO_EOL					0x0C
#define PI_RSP_K_FILTER_STATE_BAD		0x0D
#define PI_RSP_K_CMD_TYPE_BAD			0x0E
#define PI_RSP_K_ADAPTER_STATE_BAD		0x0F
#define PI_RSP_K_RING_PURGER_BAD		0x10
#define PI_RSP_K_LEM_THRESHOLD_BAD		0x11
#define PI_RSP_K_LOOP_NOT_SUPPORTED		0x12
#define PI_RSP_K_FLUSH_TIME_BAD			0x13
#define PI_RSP_K_NOT_IMPLEMENTED		0x14
#define PI_RSP_K_CONFIG_POLICY_BAD		0x15
#define PI_RSP_K_STATION_ACTION_BAD		0x16
#define PI_RSP_K_MAC_ACTION_BAD			0x17
#define PI_RSP_K_CON_POLICIES_BAD		0x18
#define PI_RSP_K_MAC_LOOP_TIME_BAD		0x19
#define PI_RSP_K_TB_MAX_BAD				0x1A
#define PI_RSP_K_LER_CUTOFF_BAD			0x1B
#define PI_RSP_K_LER_ALARM_BAD			0x1C
#define PI_RSP_K_MAC_PATHS_REQ_BAD		0x1D
#define PI_RSP_K_MAC_T_REQ_BAD			0x1E
#define PI_RSP_K_EMAC_RING_PURGER_BAD	0x1F
#define PI_RSP_K_EMAC_RTOKEN_TIME_BAD	0x20
#define PI_RSP_K_NO_SUCH_ENTRY			0x21
#define PI_RSP_K_T_NOTIFY_BAD			0x22
#define PI_RSP_K_TR_MAX_EXP_BAD			0x23
#define PI_RSP_K_MAC_FRM_ERR_THR_BAD	0x24
#define PI_RSP_K_MAX_T_REQ_BAD			0x25
#define PI_RSP_K_FDX_ENB_DIS_BAD		0x26
#define PI_RSP_K_ITEM_INDEX_BAD			0x27
#define PI_RSP_K_PORT_ACTION_BAD		0x28

/* Commonly used structures */

typedef struct									/* Item list */
	{
	PI_UINT32  item_code;
	PI_UINT32  value;
	} PI_ITEM_LIST;

typedef struct									/* Response header */
	{
	PI_UINT32  reserved;
	PI_UINT32  cmd_type;
	PI_UINT32  status;
	} PI_RSP_HEADER;


/* Start Command */

typedef struct
	{
	PI_UINT32  cmd_type;
	} PI_CMD_START_REQ;

/* Start Response */

typedef struct
	{
	PI_RSP_HEADER   header;
	} PI_CMD_START_RSP;

/* Filters_Set Request */

#define PI_CMD_FILTERS_SET_K_ITEMS_MAX  63		/* Fits in a 512 byte buffer */

typedef struct
	{
	PI_UINT32		cmd_type;
	PI_ITEM_LIST	item[PI_CMD_FILTERS_SET_K_ITEMS_MAX];
	} PI_CMD_FILTERS_SET_REQ;

/* Filters_Set Response */

typedef struct
	{
	PI_RSP_HEADER   header;
	} PI_CMD_FILTERS_SET_RSP;

/* Filters_Get Request */

typedef struct
	{
	PI_UINT32		cmd_type;
	} PI_CMD_FILTERS_GET_REQ;

/* Filters_Get Response */

typedef struct
	{
	PI_RSP_HEADER   header;
	PI_UINT32		ind_group_prom;
	PI_UINT32		group_prom;
	PI_UINT32		broadcast_all;
	PI_UINT32		smt_all;
	PI_UINT32		smt_user;
	PI_UINT32		reserved_all;
	PI_UINT32		implementor_all;
	} PI_CMD_FILTERS_GET_RSP;


/* Chars_Set Request */

#define PI_CMD_CHARS_SET_K_ITEMS_MAX 42		/* Fits in a 512 byte buffer */

typedef struct
	{
	PI_UINT32		cmd_type;
	struct							  		/* Item list */
		{
		PI_UINT32	item_code;
		PI_UINT32	value;
		PI_UINT32	item_index;
		} item[PI_CMD_CHARS_SET_K_ITEMS_MAX];
	} PI_CMD_CHARS_SET_REQ;

/* Chars_Set Response */

typedef struct
	{
	PI_RSP_HEADER   header;
	} PI_CMD_CHARS_SET_RSP;


/* SNMP_Set Request */

#define PI_CMD_SNMP_SET_K_ITEMS_MAX 42	   	/* Fits in a 512 byte buffer */

typedef struct
	{
	PI_UINT32		cmd_type;
	struct							   		/* Item list */
		{
		PI_UINT32	item_code;
		PI_UINT32	value;
		PI_UINT32	item_index;
		} item[PI_CMD_SNMP_SET_K_ITEMS_MAX];
	} PI_CMD_SNMP_SET_REQ;

/* SNMP_Set Response */

typedef struct
	{
	PI_RSP_HEADER   header;
	} PI_CMD_SNMP_SET_RSP;


/* SMT_MIB_Set Request */

#define PI_CMD_SMT_MIB_SET_K_ITEMS_MAX 42	/* Max number of items */

typedef struct
	{
	PI_UINT32	cmd_type;
	struct
		{
		PI_UINT32	item_code;
		PI_UINT32	value;
		PI_UINT32	item_index;
		} item[PI_CMD_SMT_MIB_SET_K_ITEMS_MAX];
	} PI_CMD_SMT_MIB_SET_REQ;

/* SMT_MIB_Set Response */

typedef struct
	{
	PI_RSP_HEADER   header;
	} PI_CMD_SMT_MIB_SET_RSP;

/* SMT_MIB_Get Request */

typedef struct
	{
	PI_UINT32  cmd_type;
	} PI_CMD_SMT_MIB_GET_REQ;

/* SMT_MIB_Get Response */

typedef struct						  /* Refer to ANSI FDDI SMT Rev. 7.3 */
	{
	PI_RSP_HEADER  header;

	/* SMT GROUP */

	PI_STATION_ID  	smt_station_id;
	PI_UINT32 		smt_op_version_id;
	PI_UINT32	   	smt_hi_version_id;
	PI_UINT32	   	smt_lo_version_id;
	PI_UINT32	   	smt_user_data[8];
	PI_UINT32	   	smt_mib_version_id;
	PI_UINT32	   	smt_mac_ct;
	PI_UINT32	   	smt_non_master_ct;
	PI_UINT32	   	smt_master_ct;
	PI_UINT32	   	smt_available_paths;
	PI_UINT32	   	smt_config_capabilities;
	PI_UINT32	   	smt_config_policy;
	PI_UINT32	   	smt_connection_policy;
	PI_UINT32	   	smt_t_notify;
	PI_UINT32	   	smt_stat_rpt_policy;
	PI_UINT32	   	smt_trace_max_expiration;
	PI_UINT32	   	smt_bypass_present;
	PI_UINT32	  	smt_ecm_state;
	PI_UINT32	   	smt_cf_state;
	PI_UINT32	   	smt_remote_disconnect_flag;
	PI_UINT32	   	smt_station_status;
	PI_UINT32	   	smt_peer_wrap_flag;
	PI_CNTR	   		smt_msg_time_stamp;
	PI_CNTR	  		smt_transition_time_stamp;

	/* MAC GROUP */

	PI_UINT32		mac_frame_status_functions;
	PI_UINT32		mac_t_max_capability;
	PI_UINT32		mac_tvx_capability;
	PI_UINT32		mac_available_paths;
	PI_UINT32		mac_current_path;
	PI_LAN_ADDR		mac_upstream_nbr;
	PI_LAN_ADDR		mac_downstream_nbr;
	PI_LAN_ADDR		mac_old_upstream_nbr;
	PI_LAN_ADDR		mac_old_downstream_nbr;
	PI_UINT32	   	mac_dup_address_test;
	PI_UINT32	   	mac_requested_paths;
	PI_UINT32	   	mac_downstream_port_type;
	PI_LAN_ADDR		mac_smt_address;
	PI_UINT32		mac_t_req;
	PI_UINT32		mac_t_neg;
	PI_UINT32		mac_t_max;
	PI_UINT32		mac_tvx_value;
	PI_UINT32		mac_frame_error_threshold;
	PI_UINT32		mac_frame_error_ratio;
	PI_UINT32		mac_rmt_state;
	PI_UINT32		mac_da_flag;
	PI_UINT32		mac_unda_flag;
	PI_UINT32		mac_frame_error_flag;
	PI_UINT32		mac_ma_unitdata_available;
	PI_UINT32		mac_hardware_present;
	PI_UINT32		mac_ma_unitdata_enable;

	/* PATH GROUP */

	PI_UINT32		path_configuration[8];
	PI_UINT32		path_tvx_lower_bound;
	PI_UINT32		path_t_max_lower_bound;
	PI_UINT32		path_max_t_req;

	/* PORT GROUP */

	PI_UINT32		port_my_type[PI_PHY_K_MAX];
	PI_UINT32		port_neighbor_type[PI_PHY_K_MAX];
	PI_UINT32		port_connection_policies[PI_PHY_K_MAX];
	PI_UINT32		port_mac_indicated[PI_PHY_K_MAX];
	PI_UINT32		port_current_path[PI_PHY_K_MAX];
	PI_UINT32		port_requested_paths[PI_PHY_K_MAX];
	PI_UINT32		port_mac_placement[PI_PHY_K_MAX];
	PI_UINT32		port_available_paths[PI_PHY_K_MAX];
	PI_UINT32		port_pmd_class[PI_PHY_K_MAX];
	PI_UINT32		port_connection_capabilities[PI_PHY_K_MAX];
	PI_UINT32		port_bs_flag[PI_PHY_K_MAX];
	PI_UINT32		port_ler_estimate[PI_PHY_K_MAX];
	PI_UINT32		port_ler_cutoff[PI_PHY_K_MAX];
	PI_UINT32		port_ler_alarm[PI_PHY_K_MAX];
	PI_UINT32		port_connect_state[PI_PHY_K_MAX];
	PI_UINT32		port_pcm_state[PI_PHY_K_MAX];
	PI_UINT32		port_pc_withhold[PI_PHY_K_MAX];
	PI_UINT32		port_ler_flag[PI_PHY_K_MAX];
	PI_UINT32		port_hardware_present[PI_PHY_K_MAX];

	/* GROUP for things that were added later, so must be at the end. */

	PI_CNTR	   		path_ring_latency;

	} PI_CMD_SMT_MIB_GET_RSP;


/*
 *  Item and group code definitions for SMT 7.3 mandatory objects.  These
 *  definitions are to be used as appropriate in SMT_MIB_SET commands and
 *  certain host-sent SMT frames such as PMF Get and Set requests.  The
 *  codes have been taken from the MIB summary section of ANSI SMT 7.3.
 */

#define PI_GRP_K_SMT_STATION_ID			0x100A
#define PI_ITEM_K_SMT_STATION_ID		0x100B
#define PI_ITEM_K_SMT_OP_VERS_ID		0x100D
#define PI_ITEM_K_SMT_HI_VERS_ID		0x100E
#define PI_ITEM_K_SMT_LO_VERS_ID		0x100F
#define PI_ITEM_K_SMT_USER_DATA			0x1011
#define PI_ITEM_K_SMT_MIB_VERS_ID	  	0x1012

#define PI_GRP_K_SMT_STATION_CONFIG		0x1014
#define PI_ITEM_K_SMT_MAC_CT			0x1015
#define PI_ITEM_K_SMT_NON_MASTER_CT		0x1016
#define PI_ITEM_K_SMT_MASTER_CT			0x1017
#define PI_ITEM_K_SMT_AVAIL_PATHS		0x1018
#define PI_ITEM_K_SMT_CONFIG_CAPS		0x1019
#define PI_ITEM_K_SMT_CONFIG_POL		0x101A
#define PI_ITEM_K_SMT_CONN_POL			0x101B
#define PI_ITEM_K_SMT_T_NOTIFY			0x101D
#define PI_ITEM_K_SMT_STAT_POL			0x101E
#define PI_ITEM_K_SMT_TR_MAX_EXP		0x101F
#define PI_ITEM_K_SMT_PORT_INDEXES		0x1020
#define PI_ITEM_K_SMT_MAC_INDEXES		0x1021
#define PI_ITEM_K_SMT_BYPASS_PRESENT	0x1022

#define PI_GRP_K_SMT_STATUS				0x1028
#define PI_ITEM_K_SMT_ECM_STATE			0x1029
#define PI_ITEM_K_SMT_CF_STATE		 	0x102A
#define PI_ITEM_K_SMT_REM_DISC_FLAG		0x102C
#define PI_ITEM_K_SMT_STATION_STATUS	0x102D
#define PI_ITEM_K_SMT_PEER_WRAP_FLAG	0x102E

#define PI_GRP_K_SMT_MIB_OPERATION	 	0x1032
#define PI_ITEM_K_SMT_MSG_TIME_STAMP 	0x1033
#define PI_ITEM_K_SMT_TRN_TIME_STAMP 	0x1034

#define PI_ITEM_K_SMT_STATION_ACT		0x103C

#define PI_GRP_K_MAC_CAPABILITIES	  	0x200A
#define PI_ITEM_K_MAC_FRM_STAT_FUNC		0x200B
#define PI_ITEM_K_MAC_T_MAX_CAP			0x200D
#define PI_ITEM_K_MAC_TVX_CAP		  	0x200E

#define PI_GRP_K_MAC_CONFIG				0x2014
#define PI_ITEM_K_MAC_AVAIL_PATHS	  	0x2016
#define PI_ITEM_K_MAC_CURRENT_PATH	 	0x2017
#define PI_ITEM_K_MAC_UP_NBR			0x2018
#define PI_ITEM_K_MAC_DOWN_NBR			0x2019
#define PI_ITEM_K_MAC_OLD_UP_NBR	 	0x201A
#define PI_ITEM_K_MAC_OLD_DOWN_NBR	 	0x201B
#define PI_ITEM_K_MAC_DUP_ADDR_TEST		0x201D
#define PI_ITEM_K_MAC_REQ_PATHS			0x2020
#define PI_ITEM_K_MAC_DOWN_PORT_TYPE   	0x2021
#define PI_ITEM_K_MAC_INDEX				0x2022

#define PI_GRP_K_MAC_ADDRESS			0x2028
#define PI_ITEM_K_MAC_SMT_ADDRESS		0x2029

#define PI_GRP_K_MAC_OPERATION			0x2032
#define PI_ITEM_K_MAC_TREQ				0x2033
#define PI_ITEM_K_MAC_TNEG				0x2034
#define PI_ITEM_K_MAC_TMAX				0x2035
#define PI_ITEM_K_MAC_TVX_VALUE			0x2036

#define PI_GRP_K_MAC_COUNTERS			0x2046
#define PI_ITEM_K_MAC_FRAME_CT			0x2047
#define PI_ITEM_K_MAC_COPIED_CT			0x2048
#define PI_ITEM_K_MAC_TRANSMIT_CT		0x2049
#define PI_ITEM_K_MAC_ERROR_CT			0x2051
#define PI_ITEM_K_MAC_LOST_CT			0x2052

#define PI_GRP_K_MAC_FRM_ERR_COND		0x205A
#define PI_ITEM_K_MAC_FRM_ERR_THR		0x205F
#define PI_ITEM_K_MAC_FRM_ERR_RAT		0x2060

#define PI_GRP_K_MAC_STATUS				0x206E
#define PI_ITEM_K_MAC_RMT_STATE			0x206F
#define PI_ITEM_K_MAC_DA_FLAG			0x2070
#define PI_ITEM_K_MAC_UNDA_FLAG			0x2071
#define PI_ITEM_K_MAC_FRM_ERR_FLAG		0x2072
#define PI_ITEM_K_MAC_MA_UNIT_AVAIL		0x2074
#define PI_ITEM_K_MAC_HW_PRESENT		0x2075
#define PI_ITEM_K_MAC_MA_UNIT_ENAB		0x2076

#define PI_GRP_K_PATH_CONFIG			0x320A
#define PI_ITEM_K_PATH_INDEX			0x320B
#define PI_ITEM_K_PATH_CONFIGURATION 	0x3212
#define PI_ITEM_K_PATH_TVX_LB			0x3215
#define PI_ITEM_K_PATH_T_MAX_LB			0x3216
#define PI_ITEM_K_PATH_MAX_T_REQ		0x3217

#define PI_GRP_K_PORT_CONFIG			0x400A
#define PI_ITEM_K_PORT_MY_TYPE			0x400C
#define PI_ITEM_K_PORT_NBR_TYPE			0x400D
#define PI_ITEM_K_PORT_CONN_POLS		0x400E
#define PI_ITEM_K_PORT_MAC_INDICATED  	0x400F
#define PI_ITEM_K_PORT_CURRENT_PATH		0x4010
#define PI_ITEM_K_PORT_REQ_PATHS		0x4011
#define PI_ITEM_K_PORT_MAC_PLACEMENT 	0x4012
#define PI_ITEM_K_PORT_AVAIL_PATHS		0x4013
#define PI_ITEM_K_PORT_PMD_CLASS		0x4016
#define PI_ITEM_K_PORT_CONN_CAPS		0x4017
#define PI_ITEM_K_PORT_INDEX			0x401D

#define PI_GRP_K_PORT_OPERATION			0x401E
#define PI_ITEM_K_PORT_BS_FLAG		 	0x4021

#define PI_GRP_K_PORT_ERR_CNTRS			0x4028
#define PI_ITEM_K_PORT_LCT_FAIL_CT	 	0x402A

#define PI_GRP_K_PORT_LER			  	0x4032
#define PI_ITEM_K_PORT_LER_ESTIMATE		0x4033
#define PI_ITEM_K_PORT_LEM_REJ_CT		0x4034
#define PI_ITEM_K_PORT_LEM_CT			0x4035
#define PI_ITEM_K_PORT_LER_CUTOFF		0x403A
#define PI_ITEM_K_PORT_LER_ALARM		0x403B

#define PI_GRP_K_PORT_STATUS			0x403C
#define PI_ITEM_K_PORT_CONNECT_STATE	0x403D
#define PI_ITEM_K_PORT_PCM_STATE		0x403E
#define PI_ITEM_K_PORT_PC_WITHHOLD		0x403F
#define PI_ITEM_K_PORT_LER_FLAG			0x4040
#define PI_ITEM_K_PORT_HW_PRESENT		0x4041

#define PI_ITEM_K_PORT_ACT				0x4046

/* Addr_Filter_Set Request */

#define PI_CMD_ADDR_FILTER_K_SIZE   62

typedef struct
	{
	PI_UINT32	cmd_type;
	PI_LAN_ADDR	entry[PI_CMD_ADDR_FILTER_K_SIZE];
	} PI_CMD_ADDR_FILTER_SET_REQ;

/* Addr_Filter_Set Response */

typedef struct
	{
	PI_RSP_HEADER   header;
	} PI_CMD_ADDR_FILTER_SET_RSP;

/* Addr_Filter_Get Request */

typedef struct
	{
	PI_UINT32	cmd_type;
	} PI_CMD_ADDR_FILTER_GET_REQ;

/* Addr_Filter_Get Response */

typedef struct
	{
	PI_RSP_HEADER   header;
	PI_LAN_ADDR		entry[PI_CMD_ADDR_FILTER_K_SIZE];
	} PI_CMD_ADDR_FILTER_GET_RSP;

/* Status_Chars_Get Request */

typedef struct
	{
	PI_UINT32  cmd_type;
	} PI_CMD_STATUS_CHARS_GET_REQ;

/* Status_Chars_Get Response */

typedef struct
	{
	PI_RSP_HEADER   header;
	PI_STATION_ID   station_id;						/* Station */
	PI_UINT32		station_type;
	PI_UINT32		smt_ver_id;
	PI_UINT32		smt_ver_id_max;
	PI_UINT32		smt_ver_id_min;
	PI_UINT32		station_state;
	PI_LAN_ADDR		link_addr;						/* Link */
	PI_UINT32		t_req;
	PI_UINT32		tvx;
	PI_UINT32		token_timeout;
	PI_UINT32		purger_enb;
	PI_UINT32		link_state;
	PI_UINT32		tneg;
	PI_UINT32		dup_addr_flag;
	PI_LAN_ADDR		una;
	PI_LAN_ADDR		una_old;
	PI_UINT32		un_dup_addr_flag;
	PI_LAN_ADDR		dna;
	PI_LAN_ADDR		dna_old;
	PI_UINT32		purger_state;
	PI_UINT32		fci_mode;
	PI_UINT32		error_reason;
	PI_UINT32		loopback;
	PI_UINT32		ring_latency;
	PI_LAN_ADDR		last_dir_beacon_sa;
	PI_LAN_ADDR		last_dir_beacon_una;
	PI_UINT32		phy_type[PI_PHY_K_MAX];			/* Phy */
	PI_UINT32		pmd_type[PI_PHY_K_MAX];
	PI_UINT32		lem_threshold[PI_PHY_K_MAX];
	PI_UINT32		phy_state[PI_PHY_K_MAX];
	PI_UINT32		nbor_phy_type[PI_PHY_K_MAX];
	PI_UINT32		link_error_est[PI_PHY_K_MAX];
	PI_UINT32		broken_reason[PI_PHY_K_MAX];
	PI_UINT32		reject_reason[PI_PHY_K_MAX];
	PI_UINT32		cntr_interval;					/* Miscellaneous */
	PI_UINT32		module_rev;
	PI_UINT32		firmware_rev;
	PI_UINT32		mop_device_type;
	PI_UINT32		phy_led[PI_PHY_K_MAX];
	PI_UINT32		flush_time;
	} PI_CMD_STATUS_CHARS_GET_RSP;

/* FDDI_MIB_Get Request */

typedef struct
	{
	PI_UINT32  cmd_type;
	} PI_CMD_FDDI_MIB_GET_REQ;

/* FDDI_MIB_Get Response */

typedef struct
	{
	PI_RSP_HEADER   header;

	/* SMT GROUP */

	PI_STATION_ID   smt_station_id;
	PI_UINT32		smt_op_version_id;
	PI_UINT32		smt_hi_version_id;
	PI_UINT32		smt_lo_version_id;
	PI_UINT32		smt_mac_ct;
	PI_UINT32		smt_non_master_ct;
	PI_UINT32		smt_master_ct;
	PI_UINT32		smt_paths_available;
	PI_UINT32		smt_config_capabilities;
	PI_UINT32		smt_config_policy;
	PI_UINT32		smt_connection_policy;
	PI_UINT32		smt_t_notify;
	PI_UINT32		smt_status_reporting;
	PI_UINT32		smt_ecm_state;
	PI_UINT32		smt_cf_state;
	PI_UINT32		smt_hold_state;
	PI_UINT32		smt_remote_disconnect_flag;
	PI_UINT32		smt_station_action;

	/* MAC GROUP */

	PI_UINT32		mac_frame_status_capabilities;
	PI_UINT32		mac_t_max_greatest_lower_bound;
	PI_UINT32		mac_tvx_greatest_lower_bound;
	PI_UINT32		mac_paths_available;
	PI_UINT32		mac_current_path;
	PI_LAN_ADDR		mac_upstream_nbr;
	PI_LAN_ADDR		mac_old_upstream_nbr;
	PI_UINT32		mac_dup_addr_test;
	PI_UINT32		mac_paths_requested;
	PI_UINT32		mac_downstream_port_type;
	PI_LAN_ADDR		mac_smt_address;
	PI_UINT32		mac_t_req;
	PI_UINT32		mac_t_neg;
	PI_UINT32		mac_t_max;
	PI_UINT32		mac_tvx_value;
	PI_UINT32		mac_t_min;
	PI_UINT32		mac_current_frame_status;
	/*			  	mac_frame_cts 			*/
	/* 				mac_error_cts 			*/
	/* 		   		mac_lost_cts 			*/
	PI_UINT32		mac_frame_error_threshold;
	PI_UINT32		mac_frame_error_ratio;
	PI_UINT32		mac_rmt_state;
	PI_UINT32		mac_da_flag;
	PI_UINT32		mac_una_da_flag;
	PI_UINT32		mac_frame_condition;
	PI_UINT32		mac_chip_set;
	PI_UINT32		mac_action;

	/* PATH GROUP => Does not need to be implemented */

	/* PORT GROUP */

	PI_UINT32		port_pc_type[PI_PHY_K_MAX];
	PI_UINT32		port_pc_neighbor[PI_PHY_K_MAX];
	PI_UINT32		port_connection_policies[PI_PHY_K_MAX];
	PI_UINT32		port_remote_mac_indicated[PI_PHY_K_MAX];
	PI_UINT32		port_ce_state[PI_PHY_K_MAX];
	PI_UINT32		port_paths_requested[PI_PHY_K_MAX];
	PI_UINT32		port_mac_placement[PI_PHY_K_MAX];
	PI_UINT32		port_available_paths[PI_PHY_K_MAX];
	PI_UINT32		port_mac_loop_time[PI_PHY_K_MAX];
	PI_UINT32		port_tb_max[PI_PHY_K_MAX];
	PI_UINT32		port_bs_flag[PI_PHY_K_MAX];
	/*				port_lct_fail_cts[PI_PHY_K_MAX];	*/
	PI_UINT32		port_ler_estimate[PI_PHY_K_MAX];
	/*				port_lem_reject_cts[PI_PHY_K_MAX];	*/
	/*				port_lem_cts[PI_PHY_K_MAX];		*/
	PI_UINT32		port_ler_cutoff[PI_PHY_K_MAX];
	PI_UINT32		port_ler_alarm[PI_PHY_K_MAX];
	PI_UINT32		port_connect_state[PI_PHY_K_MAX];
	PI_UINT32		port_pcm_state[PI_PHY_K_MAX];
	PI_UINT32		port_pc_withhold[PI_PHY_K_MAX];
	PI_UINT32		port_ler_condition[PI_PHY_K_MAX];
	PI_UINT32		port_chip_set[PI_PHY_K_MAX];
	PI_UINT32		port_action[PI_PHY_K_MAX];

	/* ATTACHMENT GROUP */

	PI_UINT32		attachment_class;
	PI_UINT32		attachment_ob_present;
	PI_UINT32		attachment_imax_expiration;
	PI_UINT32		attachment_inserted_status;
	PI_UINT32		attachment_insert_policy;

	/* CHIP SET GROUP => Does not need to be implemented */

	} PI_CMD_FDDI_MIB_GET_RSP;

/* DEC_Ext_MIB_Get Request */

typedef struct
	{
	PI_UINT32  cmd_type;
	} PI_CMD_DEC_EXT_MIB_GET_REQ;

/* DEC_Ext_MIB_Get (efddi and efdx groups only) Response */

typedef struct
	{
	PI_RSP_HEADER   header;

	/* SMT GROUP */

	PI_UINT32		esmt_station_type;

	/* MAC GROUP */

	PI_UINT32		emac_link_state;
	PI_UINT32		emac_ring_purger_state;
	PI_UINT32		emac_ring_purger_enable;
	PI_UINT32		emac_frame_strip_mode;
	PI_UINT32		emac_ring_error_reason;
	PI_UINT32		emac_up_nbr_dup_addr_flag;
	PI_UINT32		emac_restricted_token_timeout;

	/* PORT GROUP */

	PI_UINT32		eport_pmd_type[PI_PHY_K_MAX];
	PI_UINT32		eport_phy_state[PI_PHY_K_MAX];
	PI_UINT32		eport_reject_reason[PI_PHY_K_MAX];

	/* FDX (Full-Duplex) GROUP */

	PI_UINT32		efdx_enable;				/* Valid only in SMT 7.3 */
	PI_UINT32		efdx_op;					/* Valid only in SMT 7.3 */
	PI_UINT32		efdx_state;					/* Valid only in SMT 7.3 */

	} PI_CMD_DEC_EXT_MIB_GET_RSP;

typedef struct
	{
	PI_CNTR		traces_rcvd;					/* Station */
	PI_CNTR		frame_cnt;						/* Link */
	PI_CNTR		error_cnt;
	PI_CNTR		lost_cnt;
	PI_CNTR		octets_rcvd;
	PI_CNTR		octets_sent;
	PI_CNTR		pdus_rcvd;
	PI_CNTR		pdus_sent;
	PI_CNTR		mcast_octets_rcvd;
	PI_CNTR		mcast_octets_sent;
	PI_CNTR		mcast_pdus_rcvd;
	PI_CNTR		mcast_pdus_sent;
	PI_CNTR		xmt_underruns;
	PI_CNTR		xmt_failures;
	PI_CNTR		block_check_errors;
	PI_CNTR		frame_status_errors;
	PI_CNTR		pdu_length_errors;
	PI_CNTR		rcv_overruns;
	PI_CNTR		user_buff_unavailable;
	PI_CNTR		inits_initiated;
	PI_CNTR		inits_rcvd;
	PI_CNTR		beacons_initiated;
	PI_CNTR		dup_addrs;
	PI_CNTR		dup_tokens;
	PI_CNTR		purge_errors;
	PI_CNTR		fci_strip_errors;
	PI_CNTR		traces_initiated;
	PI_CNTR		directed_beacons_rcvd;
	PI_CNTR		emac_frame_alignment_errors;
	PI_CNTR		ebuff_errors[PI_PHY_K_MAX];		/* Phy */
	PI_CNTR		lct_rejects[PI_PHY_K_MAX];
	PI_CNTR		lem_rejects[PI_PHY_K_MAX];
	PI_CNTR		link_errors[PI_PHY_K_MAX];
	PI_CNTR		connections[PI_PHY_K_MAX];
	PI_CNTR		copied_cnt;			 			/* Valid only if using SMT 7.3 */
	PI_CNTR		transmit_cnt;					/* Valid only if using SMT 7.3 */
	PI_CNTR		tokens;
	} PI_CNTR_BLK;

/* Counters_Get Request */

typedef struct
	{
	PI_UINT32  cmd_type;
	} PI_CMD_CNTRS_GET_REQ;

/* Counters_Get Response */

typedef struct
	{
	PI_RSP_HEADER   header;
	PI_CNTR		time_since_reset;
	PI_CNTR_BLK		cntrs;
	} PI_CMD_CNTRS_GET_RSP;

/* Counters_Set Request */

typedef struct
	{
	PI_UINT32	cmd_type;
	PI_CNTR_BLK	cntrs;
	} PI_CMD_CNTRS_SET_REQ;

/* Counters_Set Response */

typedef struct
	{
	PI_RSP_HEADER   header;
	} PI_CMD_CNTRS_SET_RSP;

/* Error_Log_Clear Request */

typedef struct
	{
	PI_UINT32  cmd_type;
	} PI_CMD_ERROR_LOG_CLEAR_REQ;

/* Error_Log_Clear Response */

typedef struct
	{
	PI_RSP_HEADER   header;
	} PI_CMD_ERROR_LOG_CLEAR_RSP;

/* Error_Log_Get Request */

#define PI_LOG_ENTRY_K_INDEX_MIN	0		/* Minimum index for entry */

typedef struct
	{
	PI_UINT32  cmd_type;
	PI_UINT32  entry_index;
	} PI_CMD_ERROR_LOG_GET_REQ;

/* Error_Log_Get Response */

#define PI_K_LOG_FW_SIZE			111		/* Max number of fw longwords */
#define PI_K_LOG_DIAG_SIZE	 		6		/* Max number of diag longwords */

typedef struct
	{
	struct
		{
		PI_UINT32	fru_imp_mask;
		PI_UINT32	test_id;
		PI_UINT32	reserved[PI_K_LOG_DIAG_SIZE];
		} diag;
	PI_UINT32		fw[PI_K_LOG_FW_SIZE];
	} PI_LOG_ENTRY;

typedef struct
	{
	PI_RSP_HEADER   header;
	PI_UINT32		event_status;
	PI_UINT32		caller_id;
	PI_UINT32		timestamp_l;
	PI_UINT32		timestamp_h;
	PI_UINT32		write_count;
	PI_LOG_ENTRY	entry_info;
	} PI_CMD_ERROR_LOG_GET_RSP;

/* Define error log related constants and types.					*/
/*   Not all of the caller id's can occur.  The only ones currently */
/*   implemented are: none, selftest, mfg, fw, console				*/

#define PI_LOG_EVENT_STATUS_K_VALID		0	/* Valid Event Status 		*/
#define PI_LOG_EVENT_STATUS_K_INVALID	1	/* Invalid Event Status 	*/
#define PI_LOG_CALLER_ID_K_NONE		 	0	/* No caller 				*/
#define PI_LOG_CALLER_ID_K_SELFTEST	 	1	/* Normal power-up selftest */
#define PI_LOG_CALLER_ID_K_MFG		 	2	/* Mfg power-up selftest 	*/
#define PI_LOG_CALLER_ID_K_ONLINE		3	/* On-line diagnostics 		*/
#define PI_LOG_CALLER_ID_K_HW			4	/* Hardware 				*/
#define PI_LOG_CALLER_ID_K_FW			5	/* Firmware 				*/
#define PI_LOG_CALLER_ID_K_CNS_HW		6	/* CNS firmware 			*/
#define PI_LOG_CALLER_ID_K_CNS_FW		7	/* CNS hardware 			*/
#define PI_LOG_CALLER_ID_K_CONSOLE	 	8   /* Console Caller Id 		*/

/*
 *  Place all DMA commands in the following request and response structures
 *  to simplify code.
 */

typedef union
	{
	PI_UINT32					cmd_type;
	PI_CMD_START_REQ			start;
	PI_CMD_FILTERS_SET_REQ		filter_set;
	PI_CMD_FILTERS_GET_REQ		filter_get;
	PI_CMD_CHARS_SET_REQ		char_set;
	PI_CMD_ADDR_FILTER_SET_REQ	addr_filter_set;
	PI_CMD_ADDR_FILTER_GET_REQ	addr_filter_get;
	PI_CMD_STATUS_CHARS_GET_REQ	stat_char_get;
	PI_CMD_CNTRS_GET_REQ		cntrs_get;
	PI_CMD_CNTRS_SET_REQ		cntrs_set;
	PI_CMD_ERROR_LOG_CLEAR_REQ	error_log_clear;
	PI_CMD_ERROR_LOG_GET_REQ	error_log_read;
	PI_CMD_SNMP_SET_REQ			snmp_set;
	PI_CMD_FDDI_MIB_GET_REQ		fddi_mib_get;
	PI_CMD_DEC_EXT_MIB_GET_REQ	dec_mib_get;
	PI_CMD_SMT_MIB_SET_REQ		smt_mib_set;
	PI_CMD_SMT_MIB_GET_REQ		smt_mib_get;
	char						pad[PI_CMD_REQ_K_SIZE_MAX];
	} PI_DMA_CMD_REQ;

typedef union
	{
	PI_RSP_HEADER				header;
	PI_CMD_START_RSP			start;
	PI_CMD_FILTERS_SET_RSP		filter_set;
	PI_CMD_FILTERS_GET_RSP		filter_get;
	PI_CMD_CHARS_SET_RSP		char_set;
	PI_CMD_ADDR_FILTER_SET_RSP	addr_filter_set;
	PI_CMD_ADDR_FILTER_GET_RSP	addr_filter_get;
	PI_CMD_STATUS_CHARS_GET_RSP	stat_char_get;
	PI_CMD_CNTRS_GET_RSP		cntrs_get;
	PI_CMD_CNTRS_SET_RSP		cntrs_set;
	PI_CMD_ERROR_LOG_CLEAR_RSP	error_log_clear;
	PI_CMD_ERROR_LOG_GET_RSP	error_log_get;
	PI_CMD_SNMP_SET_RSP			snmp_set;
	PI_CMD_FDDI_MIB_GET_RSP		fddi_mib_get;
	PI_CMD_DEC_EXT_MIB_GET_RSP	dec_mib_get;
	PI_CMD_SMT_MIB_SET_RSP		smt_mib_set;
	PI_CMD_SMT_MIB_GET_RSP		smt_mib_get;
	char						pad[PI_CMD_RSP_K_SIZE_MAX];
	} PI_DMA_CMD_RSP;

typedef union
	{
	PI_DMA_CMD_REQ	request;
	PI_DMA_CMD_RSP	response;
	} PI_DMA_CMD_BUFFER;


/* Define format of Consumer Block (resident in host memory) */

typedef struct
	{
	volatile PI_UINT32	xmt_rcv_data;
	volatile PI_UINT32	reserved_1;
	volatile PI_UINT32	smt_host;
	volatile PI_UINT32	reserved_2;
	volatile PI_UINT32	unsol;
	volatile PI_UINT32	reserved_3;
	volatile PI_UINT32	cmd_rsp;
	volatile PI_UINT32	reserved_4;
	volatile PI_UINT32	cmd_req;
	volatile PI_UINT32	reserved_5;
	} PI_CONSUMER_BLOCK;

#define PI_CONS_M_RCV_INDEX			0x000000FF
#define PI_CONS_M_XMT_INDEX			0x00FF0000
#define PI_CONS_V_RCV_INDEX			0
#define PI_CONS_V_XMT_INDEX			16

/* Offsets into consumer block */

#define PI_CONS_BLK_K_XMT_RCV		0x00
#define PI_CONS_BLK_K_SMT_HOST		0x08
#define PI_CONS_BLK_K_UNSOL			0x10
#define PI_CONS_BLK_K_CMD_RSP		0x18
#define PI_CONS_BLK_K_CMD_REQ		0x20

/* Offsets into descriptor block */

#define PI_DESCR_BLK_K_RCV_DATA		0x0000
#define PI_DESCR_BLK_K_XMT_DATA		0x0800
#define PI_DESCR_BLK_K_SMT_HOST 	0x1000
#define PI_DESCR_BLK_K_UNSOL		0x1200
#define PI_DESCR_BLK_K_CMD_RSP		0x1280
#define PI_DESCR_BLK_K_CMD_REQ		0x1300

/* Define format of a rcv descr (Rcv Data, Cmd Rsp, Unsolicited, SMT Host)   */
/*   Note a field has been added for later versions of the PDQ to allow for  */
/*   finer granularity of the rcv buffer alignment.  For backwards		 	 */
/*   compatibility, the two bits (which allow the rcv buffer to be longword  */
/*   aligned) have been added at the MBZ bits.  To support previous drivers, */
/*   the MBZ definition is left intact.									  	 */

typedef struct
	{
	PI_UINT32	long_0;
	PI_UINT32	long_1;
	} PI_RCV_DESCR;

#define	PI_RCV_DESCR_M_SOP	  		0x80000000
#define PI_RCV_DESCR_M_SEG_LEN_LO 	0x60000000
#define PI_RCV_DESCR_M_MBZ	  		0x60000000
#define PI_RCV_DESCR_M_SEG_LEN		0x1F800000
#define PI_RCV_DESCR_M_SEG_LEN_HI	0x1FF00000
#define PI_RCV_DESCR_M_SEG_CNT	  	0x000F0000
#define PI_RCV_DESCR_M_BUFF_HI	  	0x0000FFFF

#define	PI_RCV_DESCR_V_SOP	  		31
#define PI_RCV_DESCR_V_SEG_LEN_LO 	29
#define PI_RCV_DESCR_V_MBZ	  		29
#define PI_RCV_DESCR_V_SEG_LEN	  	23
#define PI_RCV_DESCR_V_SEG_LEN_HI 	20
#define PI_RCV_DESCR_V_SEG_CNT	  	16
#define PI_RCV_DESCR_V_BUFF_HI	 	0

/* Define the format of a transmit descriptor (Xmt Data, Cmd Req) */

typedef struct
	{
	PI_UINT32	long_0;
	PI_UINT32	long_1;
	} PI_XMT_DESCR;

#define	PI_XMT_DESCR_M_SOP			0x80000000
#define PI_XMT_DESCR_M_EOP			0x40000000
#define PI_XMT_DESCR_M_MBZ			0x20000000
#define PI_XMT_DESCR_M_SEG_LEN		0x1FFF0000
#define PI_XMT_DESCR_M_BUFF_HI		0x0000FFFF

#define	PI_XMT_DESCR_V_SOP			31
#define	PI_XMT_DESCR_V_EOP			30
#define PI_XMT_DESCR_V_MBZ			29
#define PI_XMT_DESCR_V_SEG_LEN		16
#define PI_XMT_DESCR_V_BUFF_HI		0

/* Define format of the Descriptor Block (resident in host memory) */

#define PI_RCV_DATA_K_NUM_ENTRIES			256
#define PI_XMT_DATA_K_NUM_ENTRIES			256
#define PI_SMT_HOST_K_NUM_ENTRIES			64
#define PI_UNSOL_K_NUM_ENTRIES				16
#define PI_CMD_RSP_K_NUM_ENTRIES			16
#define PI_CMD_REQ_K_NUM_ENTRIES			16

typedef struct
	{
	PI_RCV_DESCR  rcv_data[PI_RCV_DATA_K_NUM_ENTRIES];
	PI_XMT_DESCR  xmt_data[PI_XMT_DATA_K_NUM_ENTRIES];
	PI_RCV_DESCR  smt_host[PI_SMT_HOST_K_NUM_ENTRIES];
	PI_RCV_DESCR  unsol[PI_UNSOL_K_NUM_ENTRIES];
	PI_RCV_DESCR  cmd_rsp[PI_CMD_RSP_K_NUM_ENTRIES];
	PI_XMT_DESCR  cmd_req[PI_CMD_REQ_K_NUM_ENTRIES];
	} PI_DESCR_BLOCK;

/* Define Port Registers - offsets from PDQ Base address */

#define PI_PDQ_K_REG_PORT_RESET			0x00000000
#define PI_PDQ_K_REG_HOST_DATA			0x00000004
#define PI_PDQ_K_REG_PORT_CTRL			0x00000008
#define PI_PDQ_K_REG_PORT_DATA_A		0x0000000C
#define PI_PDQ_K_REG_PORT_DATA_B		0x00000010
#define PI_PDQ_K_REG_PORT_STATUS		0x00000014
#define PI_PDQ_K_REG_TYPE_0_STATUS 		0x00000018
#define PI_PDQ_K_REG_HOST_INT_ENB	  	0x0000001C
#define PI_PDQ_K_REG_TYPE_2_PROD_NOINT 	0x00000020
#define PI_PDQ_K_REG_TYPE_2_PROD		0x00000024
#define PI_PDQ_K_REG_CMD_RSP_PROD		0x00000028
#define PI_PDQ_K_REG_CMD_REQ_PROD		0x0000002C
#define PI_PDQ_K_REG_SMT_HOST_PROD   	0x00000030
#define PI_PDQ_K_REG_UNSOL_PROD			0x00000034

/* Port Control Register - Command codes for primary commands */

#define PI_PCTRL_M_CMD_ERROR			0x8000
#define PI_PCTRL_M_BLAST_FLASH			0x4000
#define PI_PCTRL_M_HALT					0x2000
#define PI_PCTRL_M_COPY_DATA			0x1000
#define PI_PCTRL_M_ERROR_LOG_START		0x0800
#define PI_PCTRL_M_ERROR_LOG_READ		0x0400
#define PI_PCTRL_M_XMT_DATA_FLUSH_DONE	0x0200
#define PI_PCTRL_M_INIT					0x0100
#define PI_PCTRL_M_INIT_START		    0x0080
#define PI_PCTRL_M_CONS_BLOCK			0x0040
#define PI_PCTRL_M_UNINIT				0x0020
#define PI_PCTRL_M_RING_MEMBER			0x0010
#define PI_PCTRL_M_MLA					0x0008
#define PI_PCTRL_M_FW_REV_READ			0x0004
#define PI_PCTRL_M_DEV_SPECIFIC			0x0002
#define PI_PCTRL_M_SUB_CMD				0x0001

/* Define sub-commands accessed via the PI_PCTRL_M_SUB_CMD command */

#define PI_SUB_CMD_K_LINK_UNINIT		0x0001
#define PI_SUB_CMD_K_BURST_SIZE_SET		0x0002
#define PI_SUB_CMD_K_PDQ_REV_GET		0x0004
#define PI_SUB_CMD_K_HW_REV_GET			0x0008

/* Define some Port Data B values */

#define PI_PDATA_B_DMA_BURST_SIZE_4	 	0		/* valid values for command */
#define PI_PDATA_B_DMA_BURST_SIZE_8	 	1
#define PI_PDATA_B_DMA_BURST_SIZE_16	2
#define PI_PDATA_B_DMA_BURST_SIZE_32	3		/* not supported on PCI */
#define PI_PDATA_B_DMA_BURST_SIZE_DEF	PI_PDATA_B_DMA_BURST_SIZE_16

/* Port Data A Reset state */

#define PI_PDATA_A_RESET_M_UPGRADE		0x00000001
#define PI_PDATA_A_RESET_M_SOFT_RESET	0x00000002
#define PI_PDATA_A_RESET_M_SKIP_ST		0x00000004

/* Read adapter MLA address port control command constants */

#define PI_PDATA_A_MLA_K_LO				0
#define PI_PDATA_A_MLA_K_HI				1

/* Byte Swap values for init command */

#define PI_PDATA_A_INIT_M_DESC_BLK_ADDR			0x0FFFFE000
#define PI_PDATA_A_INIT_M_RESERVED				0x000001FFC
#define PI_PDATA_A_INIT_M_BSWAP_DATA			0x000000002
#define PI_PDATA_A_INIT_M_BSWAP_LITERAL			0x000000001

#define PI_PDATA_A_INIT_V_DESC_BLK_ADDR			13
#define PI_PDATA_A_INIT_V_RESERVED				3
#define PI_PDATA_A_INIT_V_BSWAP_DATA			1
#define PI_PDATA_A_INIT_V_BSWAP_LITERAL			0

/* Port Reset Register */

#define PI_RESET_M_ASSERT_RESET			1

/* Port Status register */

#define PI_PSTATUS_V_RCV_DATA_PENDING	31
#define PI_PSTATUS_V_XMT_DATA_PENDING	30
#define PI_PSTATUS_V_SMT_HOST_PENDING	29
#define PI_PSTATUS_V_UNSOL_PENDING		28
#define PI_PSTATUS_V_CMD_RSP_PENDING	27
#define PI_PSTATUS_V_CMD_REQ_PENDING	26
#define PI_PSTATUS_V_TYPE_0_PENDING		25
#define PI_PSTATUS_V_RESERVED_1			16
#define PI_PSTATUS_V_RESERVED_2			11
#define PI_PSTATUS_V_STATE				8
#define PI_PSTATUS_V_HALT_ID			0

#define PI_PSTATUS_M_RCV_DATA_PENDING	0x80000000
#define PI_PSTATUS_M_XMT_DATA_PENDING	0x40000000
#define PI_PSTATUS_M_SMT_HOST_PENDING	0x20000000
#define PI_PSTATUS_M_UNSOL_PENDING		0x10000000
#define PI_PSTATUS_M_CMD_RSP_PENDING	0x08000000
#define PI_PSTATUS_M_CMD_REQ_PENDING	0x04000000
#define PI_PSTATUS_M_TYPE_0_PENDING		0x02000000
#define PI_PSTATUS_M_RESERVED_1			0x01FF0000
#define PI_PSTATUS_M_RESERVED_2			0x0000F800
#define PI_PSTATUS_M_STATE				0x00000700
#define PI_PSTATUS_M_HALT_ID			0x000000FF

/* Define Halt Id's			 					*/
/*   Do not insert into this list, only append. */

#define PI_HALT_ID_K_SELFTEST_TIMEOUT	0
#define PI_HALT_ID_K_PARITY_ERROR		1
#define PI_HALT_ID_K_HOST_DIR_HALT		2
#define PI_HALT_ID_K_SW_FAULT			3
#define PI_HALT_ID_K_HW_FAULT			4
#define PI_HALT_ID_K_PC_TRACE			5
#define PI_HALT_ID_K_DMA_ERROR			6			/* Host Data has error reg */
#define PI_HALT_ID_K_IMAGE_CRC_ERROR	7   		/* Image is bad, update it */
#define PI_HALT_ID_K_BUS_EXCEPTION	 	8   		/* 68K bus exception	   */

/* Host Interrupt Enable Register as seen by host */

#define PI_HOST_INT_M_XMT_DATA_ENB		0x80000000	/* Type 2 Enables */
#define PI_HOST_INT_M_RCV_DATA_ENB		0x40000000
#define PI_HOST_INT_M_SMT_HOST_ENB		0x10000000	/* Type 1 Enables */
#define PI_HOST_INT_M_UNSOL_ENB			0x20000000
#define PI_HOST_INT_M_CMD_RSP_ENB		0x08000000
#define PI_HOST_INT_M_CMD_REQ_ENB		0x04000000
#define	PI_HOST_INT_M_TYPE_1_RESERVED	0x00FF0000
#define	PI_HOST_INT_M_TYPE_0_RESERVED	0x0000FF00	/* Type 0 Enables */
#define PI_HOST_INT_M_1MS				0x00000080
#define PI_HOST_INT_M_20MS				0x00000040
#define PI_HOST_INT_M_CSR_CMD_DONE		0x00000020
#define PI_HOST_INT_M_STATE_CHANGE		0x00000010
#define PI_HOST_INT_M_XMT_FLUSH			0x00000008
#define PI_HOST_INT_M_NXM				0x00000004
#define PI_HOST_INT_M_PM_PAR_ERR		0x00000002
#define PI_HOST_INT_M_BUS_PAR_ERR		0x00000001

#define PI_HOST_INT_V_XMT_DATA_ENB		31			/* Type 2 Enables */
#define PI_HOST_INT_V_RCV_DATA_ENB		30
#define PI_HOST_INT_V_SMT_HOST_ENB		29			/* Type 1 Enables */
#define PI_HOST_INT_V_UNSOL_ENB			28
#define PI_HOST_INT_V_CMD_RSP_ENB		27
#define PI_HOST_INT_V_CMD_REQ_ENB		26
#define	PI_HOST_INT_V_TYPE_1_RESERVED	16
#define	PI_HOST_INT_V_TYPE_0_RESERVED   8			/* Type 0 Enables */
#define PI_HOST_INT_V_1MS_ENB			7
#define PI_HOST_INT_V_20MS_ENB			6
#define PI_HOST_INT_V_CSR_CMD_DONE_ENB	5
#define PI_HOST_INT_V_STATE_CHANGE_ENB	4
#define PI_HOST_INT_V_XMT_FLUSH_ENB 	3
#define PI_HOST_INT_V_NXM_ENB			2
#define PI_HOST_INT_V_PM_PAR_ERR_ENB	1
#define PI_HOST_INT_V_BUS_PAR_ERR_ENB	0

#define PI_HOST_INT_K_ACK_ALL_TYPE_0	0x000000FF
#define PI_HOST_INT_K_DISABLE_ALL_INTS	0x00000000
#define PI_HOST_INT_K_ENABLE_ALL_INTS	0xFFFFFFFF
#define PI_HOST_INT_K_ENABLE_DEF_INTS	0xC000001F

/* Type 0 Interrupt Status Register */

#define PI_TYPE_0_STAT_M_1MS			0x00000080
#define PI_TYPE_0_STAT_M_20MS			0x00000040
#define PI_TYPE_0_STAT_M_CSR_CMD_DONE	0x00000020
#define PI_TYPE_0_STAT_M_STATE_CHANGE	0x00000010
#define PI_TYPE_0_STAT_M_XMT_FLUSH		0x00000008
#define PI_TYPE_0_STAT_M_NXM			0x00000004
#define PI_TYPE_0_STAT_M_PM_PAR_ERR		0x00000002
#define PI_TYPE_0_STAT_M_BUS_PAR_ERR	0x00000001

#define PI_TYPE_0_STAT_V_1MS			7
#define PI_TYPE_0_STAT_V_20MS			6
#define PI_TYPE_0_STAT_V_CSR_CMD_DONE	5
#define PI_TYPE_0_STAT_V_STATE_CHANGE	4
#define PI_TYPE_0_STAT_V_XMT_FLUSH		3
#define PI_TYPE_0_STAT_V_NXM			2
#define PI_TYPE_0_STAT_V_PM_PAR_ERR		1
#define PI_TYPE_0_STAT_V_BUS_PAR_ERR	0

/* Register definition structures are defined for both big and little endian systems */

#ifndef __BIG_ENDIAN

/* Little endian format of Type 1 Producer register */

typedef union
	{
	PI_UINT32	lword;
	struct
		{
		PI_UINT8	prod;
		PI_UINT8	comp;
		PI_UINT8	mbz_1;
		PI_UINT8	mbz_2;
		} index;
	} PI_TYPE_1_PROD_REG;

/* Little endian format of Type 2 Producer register */

typedef union
	{
	PI_UINT32	lword;
	struct
		{
		PI_UINT8	rcv_prod;
		PI_UINT8	xmt_prod;
		PI_UINT8	rcv_comp;
		PI_UINT8	xmt_comp;
		} index;
	} PI_TYPE_2_PROD_REG;

/* Little endian format of Type 1 Consumer Block longword */

typedef union
	{
	PI_UINT32	lword;
	struct
		{
		PI_UINT8	cons;
		PI_UINT8	res0;
		PI_UINT8	res1;
		PI_UINT8	res2;
		} index;
	} PI_TYPE_1_CONSUMER;

/* Little endian format of Type 2 Consumer Block longword */

typedef union
	{
	PI_UINT32	lword;
	struct
		{
		PI_UINT8	rcv_cons;
		PI_UINT8	res0;
		PI_UINT8	xmt_cons;
		PI_UINT8	res1;
		} index;
	} PI_TYPE_2_CONSUMER;

/* Define swapping required by DMA transfers.  */
#define PI_PDATA_A_INIT_M_BSWAP_INIT	\
	(PI_PDATA_A_INIT_M_BSWAP_DATA)

#else /* __BIG_ENDIAN */

/* Big endian format of Type 1 Producer register */

typedef union
	{
	PI_UINT32	lword;
	struct
		{
		PI_UINT8	mbz_2;
		PI_UINT8	mbz_1;
		PI_UINT8	comp;
		PI_UINT8	prod;
		} index;
	} PI_TYPE_1_PROD_REG;

/* Big endian format of Type 2 Producer register */

typedef union
	{
	PI_UINT32	lword;
	struct
		{
		PI_UINT8	xmt_comp;
		PI_UINT8	rcv_comp;
		PI_UINT8	xmt_prod;
		PI_UINT8	rcv_prod;
		} index;
	} PI_TYPE_2_PROD_REG;

/* Big endian format of Type 1 Consumer Block longword */

typedef union
	{
	PI_UINT32	lword;
	struct
		{
		PI_UINT8	res2;
		PI_UINT8	res1;
		PI_UINT8	res0;
		PI_UINT8	cons;
		} index;
	} PI_TYPE_1_CONSUMER;

/* Big endian format of Type 2 Consumer Block longword */

typedef union
	{
	PI_UINT32	lword;
	struct
		{
		PI_UINT8	res1;
		PI_UINT8	xmt_cons;
		PI_UINT8	res0;
		PI_UINT8	rcv_cons;
		} index;
	} PI_TYPE_2_CONSUMER;

/* Define swapping required by DMA transfers.  */
#define PI_PDATA_A_INIT_M_BSWAP_INIT	\
	(PI_PDATA_A_INIT_M_BSWAP_DATA | PI_PDATA_A_INIT_M_BSWAP_LITERAL)

#endif /* __BIG_ENDIAN */

/* Define TC PDQ CSR offset and length */

#define PI_TC_K_CSR_OFFSET		0x100000
#define PI_TC_K_CSR_LEN			0x40		/* 64 bytes */

/* Define EISA controller register offsets */

#define PI_ESIC_K_CSR_IO_LEN		0x80		/* 128 bytes */

#define PI_DEFEA_K_BURST_HOLDOFF	0x040

#define PI_ESIC_K_SLOT_ID            	0xC80
#define PI_ESIC_K_SLOT_CNTRL		0xC84
#define PI_ESIC_K_MEM_ADD_CMP_0     	0xC85
#define PI_ESIC_K_MEM_ADD_CMP_1     	0xC86
#define PI_ESIC_K_MEM_ADD_CMP_2     	0xC87
#define PI_ESIC_K_MEM_ADD_HI_CMP_0  	0xC88
#define PI_ESIC_K_MEM_ADD_HI_CMP_1  	0xC89
#define PI_ESIC_K_MEM_ADD_HI_CMP_2  	0xC8A
#define PI_ESIC_K_MEM_ADD_MASK_0     	0xC8B
#define PI_ESIC_K_MEM_ADD_MASK_1     	0xC8C
#define PI_ESIC_K_MEM_ADD_MASK_2     	0xC8D
#define PI_ESIC_K_MEM_ADD_LO_CMP_0  	0xC8E
#define PI_ESIC_K_MEM_ADD_LO_CMP_1  	0xC8F
#define PI_ESIC_K_MEM_ADD_LO_CMP_2  	0xC90
#define PI_ESIC_K_IO_ADD_CMP_0_0	0xC91
#define PI_ESIC_K_IO_ADD_CMP_0_1	0xC92
#define PI_ESIC_K_IO_ADD_CMP_1_0	0xC93
#define PI_ESIC_K_IO_ADD_CMP_1_1	0xC94
#define PI_ESIC_K_IO_ADD_CMP_2_0	0xC95
#define PI_ESIC_K_IO_ADD_CMP_2_1	0xC96
#define PI_ESIC_K_IO_ADD_CMP_3_0	0xC97
#define PI_ESIC_K_IO_ADD_CMP_3_1	0xC98
#define PI_ESIC_K_IO_ADD_MASK_0_0    	0xC99
#define PI_ESIC_K_IO_ADD_MASK_0_1    	0xC9A
#define PI_ESIC_K_IO_ADD_MASK_1_0    	0xC9B
#define PI_ESIC_K_IO_ADD_MASK_1_1    	0xC9C
#define PI_ESIC_K_IO_ADD_MASK_2_0    	0xC9D
#define PI_ESIC_K_IO_ADD_MASK_2_1    	0xC9E
#define PI_ESIC_K_IO_ADD_MASK_3_0    	0xC9F
#define PI_ESIC_K_IO_ADD_MASK_3_1    	0xCA0
#define PI_ESIC_K_MOD_CONFIG_1		0xCA1
#define PI_ESIC_K_MOD_CONFIG_2		0xCA2
#define PI_ESIC_K_MOD_CONFIG_3		0xCA3
#define PI_ESIC_K_MOD_CONFIG_4		0xCA4
#define PI_ESIC_K_MOD_CONFIG_5    	0xCA5
#define PI_ESIC_K_MOD_CONFIG_6		0xCA6
#define PI_ESIC_K_MOD_CONFIG_7		0xCA7
#define PI_ESIC_K_DIP_SWITCH         	0xCA8
#define PI_ESIC_K_IO_CONFIG_STAT_0   	0xCA9
#define PI_ESIC_K_IO_CONFIG_STAT_1   	0xCAA
#define PI_ESIC_K_DMA_CONFIG         	0xCAB
#define PI_ESIC_K_INPUT_PORT         	0xCAC
#define PI_ESIC_K_OUTPUT_PORT        	0xCAD
#define PI_ESIC_K_FUNCTION_CNTRL	0xCAE

/* Define the bits in the function control register. */

#define PI_FUNCTION_CNTRL_M_IOCS0	0x01
#define PI_FUNCTION_CNTRL_M_IOCS1	0x02
#define PI_FUNCTION_CNTRL_M_IOCS2	0x04
#define PI_FUNCTION_CNTRL_M_IOCS3	0x08
#define PI_FUNCTION_CNTRL_M_MEMCS0	0x10
#define PI_FUNCTION_CNTRL_M_MEMCS1	0x20
#define PI_FUNCTION_CNTRL_M_DMA		0x80

/* Define the bits in the slot control register. */

#define PI_SLOT_CNTRL_M_RESET		0x04	/* Don't use.       */
#define PI_SLOT_CNTRL_M_ERROR		0x02	/* Not implemented. */
#define PI_SLOT_CNTRL_M_ENB		0x01	/* Must be set.     */

/* Define the bits in the burst holdoff register. */

#define PI_BURST_HOLDOFF_M_HOLDOFF	0xFC
#define PI_BURST_HOLDOFF_M_RESERVED	0x02
#define PI_BURST_HOLDOFF_M_MEM_MAP	0x01

#define PI_BURST_HOLDOFF_V_HOLDOFF	2
#define PI_BURST_HOLDOFF_V_RESERVED	1
#define PI_BURST_HOLDOFF_V_MEM_MAP	0

/* Define the implicit mask of the Memory Address Mask Register.  */

#define PI_MEM_ADD_MASK_M		0x3ff

/*
 * Define the fields in the IO Compare registers.
 * The driver must initialize the slot field with the slot ID shifted by the
 * amount shown below.
 */

#define PI_IO_CMP_V_SLOT		4

/* Define the fields in the Interrupt Channel Configuration and Status reg */

#define PI_CONFIG_STAT_0_M_PEND			0x80
#define PI_CONFIG_STAT_0_M_RES_1		0x40
#define PI_CONFIG_STAT_0_M_IREQ_OUT		0x20
#define PI_CONFIG_STAT_0_M_IREQ_IN		0x10
#define PI_CONFIG_STAT_0_M_INT_ENB		0x08
#define PI_CONFIG_STAT_0_M_RES_0		0x04
#define PI_CONFIG_STAT_0_M_IRQ			0x03

#define PI_CONFIG_STAT_0_V_PEND			7
#define PI_CONFIG_STAT_0_V_RES_1		6
#define PI_CONFIG_STAT_0_V_IREQ_OUT		5
#define PI_CONFIG_STAT_0_V_IREQ_IN		4
#define PI_CONFIG_STAT_0_V_INT_ENB		3
#define PI_CONFIG_STAT_0_V_RES_0		2
#define PI_CONFIG_STAT_0_V_IRQ			0

#define PI_CONFIG_STAT_0_IRQ_K_9		0
#define PI_CONFIG_STAT_0_IRQ_K_10		1
#define PI_CONFIG_STAT_0_IRQ_K_11		2
#define PI_CONFIG_STAT_0_IRQ_K_15		3

/* Define DEC FDDIcontroller/EISA (DEFEA) EISA hardware ID's */

#define DEFEA_PRODUCT_ID	0x0030A310		/* DEC product 300 (no rev)	*/
#define DEFEA_PROD_ID_1		0x0130A310		/* DEC product 300, rev 1	*/
#define DEFEA_PROD_ID_2		0x0230A310		/* DEC product 300, rev 2	*/
#define DEFEA_PROD_ID_3		0x0330A310		/* DEC product 300, rev 3	*/
#define DEFEA_PROD_ID_4		0x0430A310		/* DEC product 300, rev 4	*/

/**********************************************/
/* Digital PFI Specification v1.0 Definitions */
/**********************************************/

/* PCI Configuration Space Constants */

#define PFI_K_LAT_TIMER_DEF			0x88	/* def max master latency timer */
#define PFI_K_LAT_TIMER_MIN			0x20	/* min max master latency timer */
#define PFI_K_CSR_MEM_LEN			0x80	/* 128 bytes */
#define PFI_K_CSR_IO_LEN			0x80	/* 128 bytes */
#define PFI_K_PKT_MEM_LEN			0x10000	/* 64K bytes */

/* PFI Register Offsets (starting at PDQ Register Base Address) */

#define PFI_K_REG_RESERVED_0		 0X00000038
#define PFI_K_REG_RESERVED_1		 0X0000003C
#define PFI_K_REG_MODE_CTRL		 0X00000040
#define PFI_K_REG_STATUS		 0X00000044
#define PFI_K_REG_FIFO_WRITE		 0X00000048
#define PFI_K_REG_FIFO_READ		 0X0000004C

/* PFI Mode Control Register Constants */

#define PFI_MODE_M_RESERVED		 0XFFFFFFF0
#define PFI_MODE_M_TGT_ABORT_ENB	 0X00000008
#define PFI_MODE_M_PDQ_INT_ENB		 0X00000004
#define PFI_MODE_M_PFI_INT_ENB		 0X00000002
#define PFI_MODE_M_DMA_ENB		 0X00000001

#define PFI_MODE_V_RESERVED		 4
#define PFI_MODE_V_TGT_ABORT_ENB	 3
#define PFI_MODE_V_PDQ_INT_ENB		 2
#define PFI_MODE_V_PFI_INT_ENB		 1
#define PFI_MODE_V_DMA_ENB		 0

#define PFI_MODE_K_ALL_DISABLE		 0X00000000

/* PFI Status Register Constants */

#define PFI_STATUS_M_RESERVED		 0XFFFFFFC0
#define PFI_STATUS_M_PFI_ERROR		 0X00000020		/* only valid in rev 1 or later PFI */
#define PFI_STATUS_M_PDQ_INT		 0X00000010
#define PFI_STATUS_M_PDQ_DMA_ABORT	 0X00000008
#define PFI_STATUS_M_FIFO_FULL		 0X00000004
#define PFI_STATUS_M_FIFO_EMPTY		 0X00000002
#define PFI_STATUS_M_DMA_IN_PROGRESS	 0X00000001

#define PFI_STATUS_V_RESERVED		 6
#define PFI_STATUS_V_PFI_ERROR		 5			/* only valid in rev 1 or later PFI */
#define PFI_STATUS_V_PDQ_INT		 4
#define PFI_STATUS_V_PDQ_DMA_ABORT	 3
#define PFI_STATUS_V_FIFO_FULL		 2
#define PFI_STATUS_V_FIFO_EMPTY		 1
#define PFI_STATUS_V_DMA_IN_PROGRESS 0

#define DFX_FC_PRH2_PRH1_PRH0		0x54003820	/* Packet Request Header bytes + FC */
#define DFX_PRH0_BYTE			0x20		/* Packet Request Header byte 0 */
#define DFX_PRH1_BYTE			0x38		/* Packet Request Header byte 1 */
#define DFX_PRH2_BYTE			0x00		/* Packet Request Header byte 2 */

/* Driver routine status (return) codes */

#define DFX_K_SUCCESS			0			/* routine succeeded */
#define DFX_K_FAILURE			1			/* routine failed */
#define DFX_K_OUTSTATE			2			/* bad state for command */
#define DFX_K_HW_TIMEOUT		3			/* command timed out */

/* Define LLC host receive buffer min/max/default values */

#define RCV_BUFS_MIN	2					/* minimum pre-allocated receive buffers */
#define RCV_BUFS_MAX	32					/* maximum pre-allocated receive buffers */
#define RCV_BUFS_DEF	8					/* default pre-allocated receive buffers */

/* Define offsets into FDDI LLC or SMT receive frame buffers - used when indicating frames */

#define RCV_BUFF_K_DESCR	0				/* four byte FMC descriptor */
#define RCV_BUFF_K_PADDING	4				/* three null bytes */
#define RCV_BUFF_K_FC		7				/* one byte frame control */
#define RCV_BUFF_K_DA		8				/* six byte destination address */
#define RCV_BUFF_K_SA		14				/* six byte source address */
#define RCV_BUFF_K_DATA		20				/* offset to start of packet data */

/* Define offsets into FDDI LLC transmit frame buffers - used when sending frames */

#define XMT_BUFF_K_FC		0				/* one byte frame control */
#define XMT_BUFF_K_DA		1				/* six byte destination address */
#define XMT_BUFF_K_SA		7				/* six byte source address */
#define XMT_BUFF_K_DATA		13				/* offset to start of packet data */

/* Macro for checking a "value" is within a specific range */

#define IN_RANGE(value,low,high) ((value >= low) && (value <= high))

/* Only execute special print call when debug driver was built */

#ifdef DEFXX_DEBUG
#define DBG_printk(args...) printk(## args)
#else
#define DBG_printk(args...)
#endif

/* Define constants for masking/unmasking interrupts */

#define DFX_MASK_INTERRUPTS		1
#define DFX_UNMASK_INTERRUPTS		0

/* Define structure for driver transmit descriptor block */

typedef struct
	{
	struct sk_buff	*p_skb;					/* ptr to skb */
	} XMT_DRIVER_DESCR;

typedef struct DFX_board_tag
	{
	/* Keep virtual and physical pointers to locked, physically contiguous memory */

	char				*kmalloced;					/* pci_free_consistent this on unload */
	dma_addr_t			kmalloced_dma;
	/* DMA handle for the above */
	PI_DESCR_BLOCK			*descr_block_virt;				/* PDQ descriptor block virt address */
	dma_addr_t			descr_block_phys;				/* PDQ descriptor block phys address */
	PI_DMA_CMD_REQ			*cmd_req_virt;					/* Command request buffer virt address */
	dma_addr_t			cmd_req_phys;					/* Command request buffer phys address */
	PI_DMA_CMD_RSP			*cmd_rsp_virt;					/* Command response buffer virt address */
	dma_addr_t			cmd_rsp_phys;					/* Command response buffer phys address */
	char				*rcv_block_virt;				/* LLC host receive queue buf blk virt */
	dma_addr_t			rcv_block_phys;					/* LLC host receive queue buf blk phys */
	PI_CONSUMER_BLOCK		*cons_block_virt;				/* PDQ consumer block virt address */
	dma_addr_t			cons_block_phys;				/* PDQ consumer block phys address */

	/* Keep local copies of Type 1 and Type 2 register data */

	PI_TYPE_1_PROD_REG		cmd_req_reg;					/* Command Request register */
	PI_TYPE_1_PROD_REG		cmd_rsp_reg;					/* Command Response register */
	PI_TYPE_2_PROD_REG		rcv_xmt_reg;					/* Type 2 (RCV/XMT) register */

	/* Storage for unicast and multicast address entries in adapter CAM */

	u8				uc_table[1*FDDI_K_ALEN];
	u32				uc_count;						/* number of unicast addresses */
	u8				mc_table[PI_CMD_ADDR_FILTER_K_SIZE*FDDI_K_ALEN];
	u32				mc_count;						/* number of multicast addresses */

	/* Current packet filter settings */

	u32				ind_group_prom;					/* LLC individual & group frame prom mode */
	u32				group_prom;					/* LLC group (multicast) frame prom mode */

	/* Link available flag needed to determine whether to drop outgoing packet requests */

	u32				link_available;					/* is link available? */

	/* Resources to indicate reset type when resetting adapter */

	u32				reset_type;					/* skip or rerun diagnostics */

	/* Store pointers to receive buffers for queue processing code */

	char				*p_rcv_buff_va[PI_RCV_DATA_K_NUM_ENTRIES];

	/* Store pointers to transmit buffers for transmit completion code */

	XMT_DRIVER_DESCR		xmt_drv_descr_blk[PI_XMT_DATA_K_NUM_ENTRIES];

	/* Transmit spinlocks */

	spinlock_t			lock;

	/* Store device, bus-specific, and parameter information for this adapter */

	struct net_device		*dev;						/* pointer to device structure */
	union {
		void __iomem *mem;
		int port;
	} base;										/* base address */
	struct device			*bus_dev;
	u32				full_duplex_enb;				/* FDDI Full Duplex enable (1 == on, 2 == off) */
	u32				req_ttrt;					/* requested TTRT value (in 80ns units) */
	u32				burst_size;					/* adapter burst size (enumerated) */
	u32				rcv_bufs_to_post;				/* receive buffers to post for LLC host queue */
	u8				factory_mac_addr[FDDI_K_ALEN];			/* factory (on-board) MAC address */

	/* Common FDDI statistics structure and private counters */

	struct fddi_statistics	stats;

	u32				rcv_discards;
	u32				rcv_crc_errors;
	u32				rcv_frame_status_errors;
	u32				rcv_length_errors;
	u32				rcv_total_frames;
	u32				rcv_multicast_frames;
	u32				rcv_total_bytes;

	u32				xmt_discards;
	u32				xmt_length_errors;
	u32				xmt_total_frames;
	u32				xmt_total_bytes;
	} DFX_board_t;

#endif	/* #ifndef _DEFXX_H_ */
