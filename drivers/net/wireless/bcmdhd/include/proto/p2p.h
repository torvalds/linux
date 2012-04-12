/*
 * Copyright (C) 1999-2012, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * Fundamental types and constants relating to WFA P2P (aka WiFi Direct)
 *
 * $Id: p2p.h 311270 2012-01-28 00:11:54Z $
 */

#ifndef _P2P_H_
#define _P2P_H_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif
#include <wlioctl.h>
#include <proto/802.11.h>


#include <packed_section_start.h>



#define P2P_OUI			WFA_OUI			
#define P2P_VER			WFA_OUI_TYPE_P2P	

#define P2P_IE_ID		0xdd			


BWL_PRE_PACKED_STRUCT struct wifi_p2p_ie {
	uint8	id;		
	uint8	len;		
	uint8	OUI[3];		
	uint8	oui_type;	
	uint8	subelts[1];	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_ie wifi_p2p_ie_t;

#define P2P_IE_FIXED_LEN	6

#define P2P_ATTR_ID_OFF		0
#define P2P_ATTR_LEN_OFF	1
#define P2P_ATTR_DATA_OFF	3

#define P2P_ATTR_ID_LEN		1	
#define P2P_ATTR_LEN_LEN	2	
#define P2P_ATTR_HDR_LEN	3 


#define P2P_SEID_STATUS			0	
#define P2P_SEID_MINOR_RC		1	
#define P2P_SEID_P2P_INFO		2	
#define P2P_SEID_DEV_ID			3	
#define P2P_SEID_INTENT			4	
#define P2P_SEID_CFG_TIMEOUT		5	
#define P2P_SEID_CHANNEL		6	
#define P2P_SEID_GRP_BSSID		7	
#define P2P_SEID_XT_TIMING		8	
#define P2P_SEID_INTINTADDR		9	
#define P2P_SEID_P2P_MGBTY		10	
#define P2P_SEID_CHAN_LIST		11	
#define P2P_SEID_ABSENCE		12	
#define P2P_SEID_DEV_INFO		13	
#define P2P_SEID_GROUP_INFO		14	
#define P2P_SEID_GROUP_ID		15	
#define P2P_SEID_P2P_IF			16	
#define P2P_SEID_OP_CHANNEL		17	
#define P2P_SEID_INVITE_FLAGS		18	
#define P2P_SEID_VNDR			221	

#define P2P_SE_VS_ID_SERVICES	0x1b 



BWL_PRE_PACKED_STRUCT struct wifi_p2p_info_se_s {
	uint8	eltId;		
	uint8	len[2];		
	uint8	dev;		
	uint8	group;		
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_info_se_s wifi_p2p_info_se_t;


#define P2P_CAPSE_DEV_SERVICE_DIS	0x1 
#define P2P_CAPSE_DEV_CLIENT_DIS	0x2 
#define P2P_CAPSE_DEV_CONCURRENT	0x4 
#define P2P_CAPSE_DEV_INFRA_MAN		0x8 
#define P2P_CAPSE_DEV_LIMIT			0x10 
#define P2P_CAPSE_INVITE_PROC		0x20 


#define P2P_CAPSE_GRP_OWNER			0x1 
#define P2P_CAPSE_PERSIST_GRP		0x2 
#define P2P_CAPSE_GRP_LIMIT			0x4 
#define P2P_CAPSE_GRP_INTRA_BSS		0x8 
#define P2P_CAPSE_GRP_X_CONNECT		0x10 
#define P2P_CAPSE_GRP_PERSISTENT	0x20 
#define P2P_CAPSE_GRP_FORMATION		0x40 



BWL_PRE_PACKED_STRUCT struct wifi_p2p_intent_se_s {
	uint8	eltId;		
	uint8	len[2];		
	uint8	intent;		
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_intent_se_s wifi_p2p_intent_se_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2p_cfg_tmo_se_s {
	uint8	eltId;		
	uint8	len[2];		
	uint8	go_tmo;		
	uint8	client_tmo;	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_cfg_tmo_se_s wifi_p2p_cfg_tmo_se_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2p_listen_channel_se_s {
	uint8	eltId;		
	uint8	len[2];		
	uint8	country[3];	
	uint8	op_class;	
	uint8	channel;	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_listen_channel_se_s wifi_p2p_listen_channel_se_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2p_grp_bssid_se_s {
	uint8	eltId;		
	uint8	len[2];		
	uint8	mac[6];		
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_grp_bssid_se_s wifi_p2p_grp_bssid_se_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2p_grp_id_se_s {
	uint8	eltId;		
	uint8	len[2];		
	uint8	mac[6];		
	uint8	ssid[1];	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_grp_id_se_s wifi_p2p_grp_id_se_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2p_intf_se_s {
	uint8	eltId;		
	uint8	len[2];		
	uint8	mac[6];		
	uint8	ifaddrs;	
	uint8	ifaddr[1][6];	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_intf_se_s wifi_p2p_intf_se_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2p_status_se_s {
	uint8	eltId;		
	uint8	len[2];		
	uint8	status;		
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_status_se_s wifi_p2p_status_se_t;


#define P2P_STATSE_SUCCESS			0
				
#define P2P_STATSE_FAIL_INFO_CURR_UNAVAIL	1
				
#define P2P_STATSE_PASSED_UP			P2P_STATSE_FAIL_INFO_CURR_UNAVAIL
				
#define P2P_STATSE_FAIL_INCOMPAT_PARAMS		2
				
#define P2P_STATSE_FAIL_LIMIT_REACHED		3
				
#define P2P_STATSE_FAIL_INVALID_PARAMS		4
				
#define P2P_STATSE_FAIL_UNABLE_TO_ACCOM		5
				
#define P2P_STATSE_FAIL_PROTO_ERROR		6
				
#define P2P_STATSE_FAIL_NO_COMMON_CHAN		7
				
#define P2P_STATSE_FAIL_UNKNOWN_GROUP		8
				
#define P2P_STATSE_FAIL_INTENT			9
				
#define P2P_STATSE_FAIL_INCOMPAT_PROVIS		10
				
#define P2P_STATSE_FAIL_USER_REJECT		11
				


BWL_PRE_PACKED_STRUCT struct wifi_p2p_ext_se_s {
	uint8	eltId;		
	uint8	len[2];		
	uint8	avail[2];	
	uint8	interval[2];	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_ext_se_s wifi_p2p_ext_se_t;

#define P2P_EXT_MIN	10	


BWL_PRE_PACKED_STRUCT struct wifi_p2p_intintad_se_s {
	uint8	eltId;		
	uint8	len[2];		
	uint8	mac[6];		
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_intintad_se_s wifi_p2p_intintad_se_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2p_channel_se_s {
	uint8	eltId;		
	uint8	len[2];		
	uint8	band;		
	uint8	channel;	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_channel_se_s wifi_p2p_channel_se_t;



BWL_PRE_PACKED_STRUCT struct wifi_p2p_chanlist_entry_s {
	uint8	band;						
	uint8	num_channels;				
	uint8	channels[WL_NUMCHANNELS];	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_chanlist_entry_s wifi_p2p_chanlist_entry_t;
#define WIFI_P2P_CHANLIST_SE_MAX_ENTRIES 2


BWL_PRE_PACKED_STRUCT struct wifi_p2p_chanlist_se_s {
	uint8	eltId;		
	uint8	len[2];		
	uint8	country[3];	
	uint8	num_entries;	
	wifi_p2p_chanlist_entry_t	entries[WIFI_P2P_CHANLIST_SE_MAX_ENTRIES];
						
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_chanlist_se_s wifi_p2p_chanlist_se_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2p_pri_devtype_s {
	uint16	cat_id;		
	uint8	OUI[3];		
	uint8	oui_type;	
	uint16	sub_cat_id;	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_pri_devtype_s wifi_p2p_pri_devtype_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2p_devinfo_se_s {
	uint8	eltId;			
	uint8	len[2];			
	uint8	mac[6];			
	uint16	wps_cfg_meths;		
	uint8	pri_devtype[8];		
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_devinfo_se_s wifi_p2p_devinfo_se_t;

#define P2P_DEV_TYPE_LEN	8


BWL_PRE_PACKED_STRUCT struct wifi_p2p_cid_fixed_s {
	uint8	len;
	uint8	devaddr[ETHER_ADDR_LEN];	
	uint8	ifaddr[ETHER_ADDR_LEN];		
	uint8	devcap;				
	uint8	cfg_meths[2];			
	uint8	pridt[P2P_DEV_TYPE_LEN];	
	uint8	secdts;				
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_cid_fixed_s wifi_p2p_cid_fixed_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2p_devid_se_s {
	uint8	eltId;
	uint8	len[2];
	struct ether_addr	addr;			
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_devid_se_s wifi_p2p_devid_se_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2p_mgbt_se_s {
	uint8	eltId;		
	uint8	len[2];		
	uint8	mg_bitmap;	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_mgbt_se_s wifi_p2p_mgbt_se_t;

#define P2P_MGBTSE_P2PDEVMGMT_FLAG   0x1 


BWL_PRE_PACKED_STRUCT struct wifi_p2p_grpinfo_se_s {
	uint8	eltId;			
	uint8	len[2];			
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_grpinfo_se_s wifi_p2p_grpinfo_se_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2p_op_channel_se_s {
	uint8	eltId;		
	uint8	len[2];		
	uint8	country[3];	
	uint8	op_class;	
	uint8	channel;	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_op_channel_se_s wifi_p2p_op_channel_se_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2p_invite_flags_se_s {
	uint8	eltId;		
	uint8	len[2];		
	uint8	flags;		
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_invite_flags_se_s wifi_p2p_invite_flags_se_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2p_action_frame {
	uint8	category;	
	uint8	OUI[3];		
	uint8	type;		
	uint8	subtype;	
	uint8	dialog_token;	
	uint8	elts[1];	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_action_frame wifi_p2p_action_frame_t;
#define P2P_AF_CATEGORY		0x7f

#define P2P_AF_FIXED_LEN	7


#define P2P_AF_NOTICE_OF_ABSENCE	0	
#define P2P_AF_PRESENCE_REQ		1	
#define P2P_AF_PRESENCE_RSP		2	
#define P2P_AF_GO_DISC_REQ		3	



BWL_PRE_PACKED_STRUCT struct wifi_p2p_pub_act_frame {
	uint8	category;	
	uint8	action;		
	uint8	oui[3];		
	uint8	oui_type;	
	uint8	subtype;	
	uint8	dialog_token;	
	uint8	elts[1];	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_pub_act_frame wifi_p2p_pub_act_frame_t;
#define P2P_PUB_AF_FIXED_LEN	8
#define P2P_PUB_AF_CATEGORY	0x04
#define P2P_PUB_AF_ACTION	0x09


#define P2P_PAF_GON_REQ		0	
#define P2P_PAF_GON_RSP		1	
#define P2P_PAF_GON_CONF	2	
#define P2P_PAF_INVITE_REQ	3	
#define P2P_PAF_INVITE_RSP	4	
#define P2P_PAF_DEVDIS_REQ	5	
#define P2P_PAF_DEVDIS_RSP	6	
#define P2P_PAF_PROVDIS_REQ	7	
#define P2P_PAF_PROVDIS_RSP	8	


#define P2P_TYPE_MNREQ		P2P_PAF_GON_REQ
#define P2P_TYPE_MNRSP		P2P_PAF_GON_RSP
#define P2P_TYPE_MNCONF		P2P_PAF_GON_CONF


BWL_PRE_PACKED_STRUCT struct wifi_p2p_noa_desc {
	uint8	cnt_type;	
	uint32	duration;	
	uint32	interval;	
	uint32	start;		
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_noa_desc wifi_p2p_noa_desc_t;

BWL_PRE_PACKED_STRUCT struct wifi_p2p_noa_se {
	uint8	eltId;		
	uint8	len[2];		
	uint8	index;		
	uint8	ops_ctw_parms;	
	wifi_p2p_noa_desc_t	desc[1];	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_noa_se wifi_p2p_noa_se_t;

#define P2P_NOA_SE_FIXED_LEN	5


#define P2P_NOA_DESC_CNT_RESERVED	0	
#define P2P_NOA_DESC_CNT_REPEAT		255	
#define P2P_NOA_DESC_TYPE_PREFERRED	1	
#define P2P_NOA_DESC_TYPE_ACCEPTABLE	2	


#define P2P_NOA_CTW_MASK	0x7f
#define P2P_NOA_OPS_MASK	0x80
#define P2P_NOA_OPS_SHIFT	7

#define P2P_CTW_MIN	10	


#define	P2PSD_ACTION_CATEGORY		0x04
				
#define	P2PSD_ACTION_ID_GAS_IREQ	0x0a
				
#define	P2PSD_ACTION_ID_GAS_IRESP	0x0b
				
#define	P2PSD_ACTION_ID_GAS_CREQ	0x0c
				
#define	P2PSD_ACTION_ID_GAS_CRESP	0x0d
				
#define P2PSD_AD_EID				0x6c
				
#define P2PSD_ADP_TUPLE_QLMT_PAMEBI	0x00
				
#define P2PSD_ADP_PROTO_ID			0x00
				
#define P2PSD_GAS_OUI				P2P_OUI
				
#define P2PSD_GAS_OUI_SUBTYPE		P2P_VER
				
#define P2PSD_GAS_NQP_INFOID		0xDDDD
				
#define P2PSD_GAS_COMEBACKDEALY		0x00
				


typedef enum p2psd_svc_protype {
	SVC_RPOTYPE_ALL = 0,
	SVC_RPOTYPE_BONJOUR = 1,
	SVC_RPOTYPE_UPNP = 2,
	SVC_RPOTYPE_WSD = 3,
	SVC_RPOTYPE_VENDOR = 255
} p2psd_svc_protype_t;


typedef enum {
	P2PSD_RESP_STATUS_SUCCESS = 0,
	P2PSD_RESP_STATUS_PROTYPE_NA = 1,
	P2PSD_RESP_STATUS_DATA_NA = 2,
	P2PSD_RESP_STATUS_BAD_REQUEST = 3
} p2psd_resp_status_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2psd_adp_tpl {
	uint8	llm_pamebi;	
	uint8	adp_id;		
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_adp_tpl wifi_p2psd_adp_tpl_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2psd_adp_ie {
	uint8	id;		
	uint8	len;	
	wifi_p2psd_adp_tpl_t adp_tpl;  
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_adp_ie wifi_p2psd_adp_ie_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2psd_nqp_query_vsc {
	uint8	oui_subtype;	
	uint16	svc_updi;		
	uint8	svc_tlvs[1];	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_nqp_query_vsc wifi_p2psd_nqp_query_vsc_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2psd_qreq_tlv {
	uint16	len;			
	uint8	svc_prot;		
	uint8	svc_tscid;		
	uint8	query_data[1];	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_qreq_tlv wifi_p2psd_qreq_tlv_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2psd_qreq_frame {
	uint16	info_id;	
	uint16	len;		
	uint8	oui[3];		
	uint8	qreq_vsc[1]; 

} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_qreq_frame wifi_p2psd_qreq_frame_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2psd_gas_ireq_frame {
	wifi_p2psd_adp_ie_t		adp_ie;		
	uint16					qreq_len;	
	uint8	qreq_frm[1];	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_gas_ireq_frame wifi_p2psd_gas_ireq_frame_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2psd_qresp_tlv {
	uint16	len;				
	uint8	svc_prot;			
	uint8	svc_tscid;			
	uint8	status;				
	uint8	query_data[1];		
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_qresp_tlv wifi_p2psd_qresp_tlv_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2psd_qresp_frame {
	uint16	info_id;	
	uint16	len;		
	uint8	oui[3];		
	uint8	qresp_vsc[1]; 

} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_qresp_frame wifi_p2psd_qresp_frame_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2psd_gas_iresp_frame {
	uint16	status;			
	uint16	cb_delay;		
	wifi_p2psd_adp_ie_t	adp_ie;		
	uint16		qresp_len;	
	uint8	qresp_frm[1];	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_gas_iresp_frame wifi_p2psd_gas_iresp_frame_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2psd_gas_cresp_frame {
	uint16	status;			
	uint8	fragment_id;	
	uint16	cb_delay;		
	wifi_p2psd_adp_ie_t	adp_ie;		
	uint16	qresp_len;		
	uint8	qresp_frm[1];	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_gas_cresp_frame wifi_p2psd_gas_cresp_frame_t;


BWL_PRE_PACKED_STRUCT struct wifi_p2psd_gas_pub_act_frame {
	uint8	category;		
	uint8	action;			
	uint8	dialog_token;	
	uint8	query_data[1];	
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_gas_pub_act_frame wifi_p2psd_gas_pub_act_frame_t;


#include <packed_section_end.h>

#endif 
