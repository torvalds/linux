/*
 * Fundamental types and constants relating to WFA P2P (aka WiFi Direct)
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#ifndef _P2P_H_
#define _P2P_H_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif // endif
#include <wlioctl.h>
#include <802.11.h>

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

/* WiFi P2P OUI values */
#define P2P_VER			WFA_OUI_TYPE_P2P	/* P2P version: 9=WiFi P2P v1.0 */

#define P2P_IE_ID		0xdd			/* P2P IE element ID */

/* WiFi P2P IE */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_ie {
	uint8	id;		/* IE ID: 0xDD */
	uint8	len;		/* IE length */
	uint8	OUI[3];		/* WiFi P2P specific OUI: P2P_OUI */
	uint8	oui_type;	/* Identifies P2P version: P2P_VER */
	uint8	subelts[1];	/* variable length subelements */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_ie wifi_p2p_ie_t;

#define P2P_IE_FIXED_LEN	6

#define P2P_ATTR_ID_OFF		0
#define P2P_ATTR_LEN_OFF	1
#define P2P_ATTR_DATA_OFF	3

#define P2P_ATTR_ID_LEN		1	/* ID filed length */
#define P2P_ATTR_LEN_LEN	2	/* length field length */
#define P2P_ATTR_HDR_LEN	3 /* ID + 2-byte length field spec 1.02 */

#define P2P_WFDS_HASH_LEN		6
#define P2P_WFDS_MAX_SVC_NAME_LEN	32

/* P2P IE Subelement IDs from WiFi P2P Technical Spec 1.00 */
#define P2P_SEID_STATUS			0	/* Status */
#define P2P_SEID_MINOR_RC		1	/* Minor Reason Code */
#define P2P_SEID_P2P_INFO		2	/* P2P Capability (capabilities info) */
#define P2P_SEID_DEV_ID			3	/* P2P Device ID */
#define P2P_SEID_INTENT			4	/* Group Owner Intent */
#define P2P_SEID_CFG_TIMEOUT		5	/* Configuration Timeout */
#define P2P_SEID_CHANNEL		6	/* Listen channel */
#define P2P_SEID_GRP_BSSID		7	/* P2P Group BSSID */
#define P2P_SEID_XT_TIMING		8	/* Extended Listen Timing */
#define P2P_SEID_INTINTADDR		9	/* Intended P2P Interface Address */
#define P2P_SEID_P2P_MGBTY		10	/* P2P Manageability */
#define P2P_SEID_CHAN_LIST		11	/* Channel List */
#define P2P_SEID_ABSENCE		12	/* Notice of Absence */
#define P2P_SEID_DEV_INFO		13	/* Device Info */
#define P2P_SEID_GROUP_INFO		14	/* Group Info */
#define P2P_SEID_GROUP_ID		15	/* Group ID */
#define P2P_SEID_P2P_IF			16	/* P2P Interface */
#define P2P_SEID_OP_CHANNEL		17	/* Operating Channel */
#define P2P_SEID_INVITE_FLAGS		18	/* Invitation Flags */
#define P2P_SEID_SERVICE_HASH		21	/* Service hash */
#define P2P_SEID_SESSION		22	/* Session information */
#define P2P_SEID_CONNECT_CAP		23	/* Connection capability */
#define P2P_SEID_ADVERTISE_ID		24	/* Advertisement ID */
#define P2P_SEID_ADVERTISE_SERVICE	25	/* Advertised service */
#define P2P_SEID_SESSION_ID		26	/* Session ID */
#define P2P_SEID_FEATURE_CAP		27	/* Feature capability */
#define	P2P_SEID_PERSISTENT_GROUP	28	/* Persistent group */
#define P2P_SEID_SESSION_INFO_RESP	29	/* Session Information Response */
#define P2P_SEID_VNDR			221	/* Vendor-specific subelement */

#define P2P_SE_VS_ID_SERVICES	0x1b

/* WiFi P2P IE subelement: P2P Capability (capabilities info) */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_info_se_s {
	uint8	eltId;		/* SE ID: P2P_SEID_P2P_INFO */
	uint8	len[2];		/* SE length not including eltId, len fields */
	uint8	dev;		/* Device Capability Bitmap */
	uint8	group;		/* Group Capability Bitmap */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_info_se_s wifi_p2p_info_se_t;

/* P2P Capability subelement's Device Capability Bitmap bit values */
#define P2P_CAPSE_DEV_SERVICE_DIS	0x1 /* Service Discovery */
#define P2P_CAPSE_DEV_CLIENT_DIS	0x2 /* Client Discoverability */
#define P2P_CAPSE_DEV_CONCURRENT	0x4 /* Concurrent Operation */
#define P2P_CAPSE_DEV_INFRA_MAN		0x8 /* P2P Infrastructure Managed */
#define P2P_CAPSE_DEV_LIMIT			0x10 /* P2P Device Limit */
#define P2P_CAPSE_INVITE_PROC		0x20 /* P2P Invitation Procedure */

/* P2P Capability subelement's Group Capability Bitmap bit values */
#define P2P_CAPSE_GRP_OWNER			0x1 /* P2P Group Owner */
#define P2P_CAPSE_PERSIST_GRP		0x2 /* Persistent P2P Group */
#define P2P_CAPSE_GRP_LIMIT			0x4 /* P2P Group Limit */
#define P2P_CAPSE_GRP_INTRA_BSS		0x8 /* Intra-BSS Distribution */
#define P2P_CAPSE_GRP_X_CONNECT		0x10 /* Cross Connection */
#define P2P_CAPSE_GRP_PERSISTENT	0x20 /* Persistent Reconnect */
#define P2P_CAPSE_GRP_FORMATION		0x40 /* Group Formation */

/* WiFi P2P IE subelement: Group Owner Intent */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_intent_se_s {
	uint8	eltId;		/* SE ID: P2P_SEID_INTENT */
	uint8	len[2];		/* SE length not including eltId, len fields */
	uint8	intent;		/* Intent Value 0...15 (0=legacy 15=master only) */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_intent_se_s wifi_p2p_intent_se_t;

/* WiFi P2P IE subelement: Configuration Timeout */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_cfg_tmo_se_s {
	uint8	eltId;		/* SE ID: P2P_SEID_CFG_TIMEOUT */
	uint8	len[2];		/* SE length not including eltId, len fields */
	uint8	go_tmo;		/* GO config timeout in units of 10 ms */
	uint8	client_tmo;	/* Client config timeout in units of 10 ms */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_cfg_tmo_se_s wifi_p2p_cfg_tmo_se_t;

/* WiFi P2P IE subelement: Listen Channel */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_listen_channel_se_s {
	uint8	eltId;		/* SE ID: P2P_SEID_CHANNEL */
	uint8	len[2];		/* SE length not including eltId, len fields */
	uint8	country[3];	/* Country String */
	uint8	op_class;	/* Operating Class */
	uint8	channel;	/* Channel */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_listen_channel_se_s wifi_p2p_listen_channel_se_t;

/* WiFi P2P IE subelement: P2P Group BSSID */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_grp_bssid_se_s {
	uint8	eltId;		/* SE ID: P2P_SEID_GRP_BSSID */
	uint8	len[2];		/* SE length not including eltId, len fields */
	uint8	mac[6];		/* P2P group bssid */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_grp_bssid_se_s wifi_p2p_grp_bssid_se_t;

/* WiFi P2P IE subelement: P2P Group ID */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_grp_id_se_s {
	uint8	eltId;		/* SE ID: P2P_SEID_GROUP_ID */
	uint8	len[2];		/* SE length not including eltId, len fields */
	uint8	mac[6];		/* P2P device address */
	uint8	ssid[1];	/* ssid. device id. variable length */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_grp_id_se_s wifi_p2p_grp_id_se_t;

/* WiFi P2P IE subelement: P2P Interface */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_intf_se_s {
	uint8	eltId;		/* SE ID: P2P_SEID_P2P_IF */
	uint8	len[2];		/* SE length not including eltId, len fields */
	uint8	mac[6];		/* P2P device address */
	uint8	ifaddrs;	/* P2P Interface Address count */
	uint8	ifaddr[1][6];	/* P2P Interface Address list */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_intf_se_s wifi_p2p_intf_se_t;

/* WiFi P2P IE subelement: Status */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_status_se_s {
	uint8	eltId;		/* SE ID: P2P_SEID_STATUS */
	uint8	len[2];		/* SE length not including eltId, len fields */
	uint8	status;		/* Status Code: P2P_STATSE_* */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_status_se_s wifi_p2p_status_se_t;

/* Status subelement Status Code definitions */
#define P2P_STATSE_SUCCESS			0
				/* Success */
#define P2P_STATSE_FAIL_INFO_CURR_UNAVAIL	1
				/* Failed, information currently unavailable */
#define P2P_STATSE_PASSED_UP			P2P_STATSE_FAIL_INFO_CURR_UNAVAIL
				/* Old name for above in P2P spec 1.08 and older */
#define P2P_STATSE_FAIL_INCOMPAT_PARAMS		2
				/* Failed, incompatible parameters */
#define P2P_STATSE_FAIL_LIMIT_REACHED		3
				/* Failed, limit reached */
#define P2P_STATSE_FAIL_INVALID_PARAMS		4
				/* Failed, invalid parameters */
#define P2P_STATSE_FAIL_UNABLE_TO_ACCOM		5
				/* Failed, unable to accomodate request */
#define P2P_STATSE_FAIL_PROTO_ERROR		6
				/* Failed, previous protocol error or disruptive behaviour */
#define P2P_STATSE_FAIL_NO_COMMON_CHAN		7
				/* Failed, no common channels */
#define P2P_STATSE_FAIL_UNKNOWN_GROUP		8
				/* Failed, unknown P2P Group */
#define P2P_STATSE_FAIL_INTENT			9
				/* Failed, both peers indicated Intent 15 in GO Negotiation */
#define P2P_STATSE_FAIL_INCOMPAT_PROVIS		10
				/* Failed, incompatible provisioning method */
#define P2P_STATSE_FAIL_USER_REJECT		11
				/* Failed, rejected by user */
#define P2P_STATSE_SUCCESS_USER_ACCEPT		12
				/* Success, accepted by user */

/* WiFi P2P IE attribute: Extended Listen Timing */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_ext_se_s {
	uint8	eltId;		/* ID: P2P_SEID_EXT_TIMING */
	uint8	len[2];		/* length not including eltId, len fields */
	uint8	avail[2];	/* availibility period */
	uint8	interval[2];	/* availibility interval */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_ext_se_s wifi_p2p_ext_se_t;

#define P2P_EXT_MIN	10	/* minimum 10ms */

/* WiFi P2P IE subelement: Intended P2P Interface Address */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_intintad_se_s {
	uint8	eltId;		/* SE ID: P2P_SEID_INTINTADDR */
	uint8	len[2];		/* SE length not including eltId, len fields */
	uint8	mac[6];		/* intended P2P interface MAC address */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_intintad_se_s wifi_p2p_intintad_se_t;

/* WiFi P2P IE subelement: Channel */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_channel_se_s {
	uint8	eltId;		/* SE ID: P2P_SEID_STATUS */
	uint8	len[2];		/* SE length not including eltId, len fields */
	uint8	band;		/* Regulatory Class (band) */
	uint8	channel;	/* Channel */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_channel_se_s wifi_p2p_channel_se_t;

/* Channel Entry structure within the Channel List SE */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_chanlist_entry_s {
	uint8	band;						/* Regulatory Class (band) */
	uint8	num_channels;				/* # of channels in the channel list */
	uint8	channels[WL_NUMCHANNELS];	/* Channel List */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_chanlist_entry_s wifi_p2p_chanlist_entry_t;
#define WIFI_P2P_CHANLIST_SE_MAX_ENTRIES 2

/* WiFi P2P IE subelement: Channel List */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_chanlist_se_s {
	uint8	eltId;		/* SE ID: P2P_SEID_CHAN_LIST */
	uint8	len[2];		/* SE length not including eltId, len fields */
	uint8	country[3];	/* Country String */
	uint8	num_entries;	/* # of channel entries */
	wifi_p2p_chanlist_entry_t	entries[WIFI_P2P_CHANLIST_SE_MAX_ENTRIES];
						/* Channel Entry List */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_chanlist_se_s wifi_p2p_chanlist_se_t;

/* WiFi Primary Device Type structure */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_pri_devtype_s {
	uint16	cat_id;		/* Category ID */
	uint8	OUI[3];		/* WFA OUI: 0x0050F2 */
	uint8	oui_type;	/* WPS_OUI_TYPE */
	uint16	sub_cat_id;	/* Sub Category ID */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_pri_devtype_s wifi_p2p_pri_devtype_t;

/* WiFi P2P Device Info Sub Element Primary Device Type Sub Category
 * maximum values for each category
 */
#define P2P_DISE_SUBCATEGORY_MINVAL		1
#define P2P_DISE_CATEGORY_COMPUTER		1
#define P2P_DISE_SUBCATEGORY_COMPUTER_MAXVAL		8
#define P2P_DISE_CATEGORY_INPUT_DEVICE		2
#define P2P_DISE_SUBCATEGORY_INPUT_DEVICE_MAXVAL	9
#define P2P_DISE_CATEGORY_PRINTER		3
#define P2P_DISE_SUBCATEGORY_PRINTER_MAXVAL		5
#define P2P_DISE_CATEGORY_CAMERA		4
#define P2P_DISE_SUBCATEGORY_CAMERA_MAXVAL		4
#define P2P_DISE_CATEGORY_STORAGE		5
#define P2P_DISE_SUBCATEGORY_STORAGE_MAXVAL		1
#define P2P_DISE_CATEGORY_NETWORK_INFRA		6
#define P2P_DISE_SUBCATEGORY_NETWORK_INFRA_MAXVAL	4
#define P2P_DISE_CATEGORY_DISPLAY		7
#define P2P_DISE_SUBCATEGORY_DISPLAY_MAXVAL		4
#define P2P_DISE_CATEGORY_MULTIMEDIA		8
#define P2P_DISE_SUBCATEGORY_MULTIMEDIA_MAXVAL		6
#define P2P_DISE_CATEGORY_GAMING		9
#define P2P_DISE_SUBCATEGORY_GAMING_MAXVAL		5
#define P2P_DISE_CATEGORY_TELEPHONE		10
#define P2P_DISE_SUBCATEGORY_TELEPHONE_MAXVAL		5
#define P2P_DISE_CATEGORY_AUDIO			11
#define P2P_DISE_SUBCATEGORY_AUDIO_MAXVAL		6

/* WiFi P2P IE's Device Info subelement */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_devinfo_se_s {
	uint8	eltId;			/* SE ID: P2P_SEID_DEVINFO */
	uint8	len[2];			/* SE length not including eltId, len fields */
	uint8	mac[6];			/* P2P Device MAC address */
	uint16	wps_cfg_meths;		/* Config Methods: reg_prototlv.h WPS_CONFMET_* */
	uint8	pri_devtype[8];		/* Primary Device Type */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_devinfo_se_s wifi_p2p_devinfo_se_t;

#define P2P_DEV_TYPE_LEN	8

/* WiFi P2P IE's Group Info subelement Client Info Descriptor */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_cid_fixed_s {
	uint8	len;
	uint8	devaddr[ETHER_ADDR_LEN];	/* P2P Device Address */
	uint8	ifaddr[ETHER_ADDR_LEN];		/* P2P Interface Address */
	uint8	devcap;				/* Device Capability */
	uint8	cfg_meths[2];			/* Config Methods: reg_prototlv.h WPS_CONFMET_* */
	uint8	pridt[P2P_DEV_TYPE_LEN];	/* Primary Device Type */
	uint8	secdts;				/* Number of Secondary Device Types */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_cid_fixed_s wifi_p2p_cid_fixed_t;

/* WiFi P2P IE's Device ID subelement */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_devid_se_s {
	uint8	eltId;
	uint8	len[2];
	struct ether_addr	addr;			/* P2P Device MAC address */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_devid_se_s wifi_p2p_devid_se_t;

/* WiFi P2P IE subelement: P2P Manageability */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_mgbt_se_s {
	uint8	eltId;		/* SE ID: P2P_SEID_P2P_MGBTY */
	uint8	len[2];		/* SE length not including eltId, len fields */
	uint8	mg_bitmap;	/* manageability bitmap */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_mgbt_se_s wifi_p2p_mgbt_se_t;
/* mg_bitmap field bit values */
#define P2P_MGBTSE_P2PDEVMGMT_FLAG   0x1 /* AP supports Managed P2P Device */

/* WiFi P2P IE subelement: Group Info */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_grpinfo_se_s {
	uint8	eltId;			/* SE ID: P2P_SEID_GROUP_INFO */
	uint8	len[2];			/* SE length not including eltId, len fields */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_grpinfo_se_s wifi_p2p_grpinfo_se_t;

/* WiFi IE subelement: Operating Channel */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_op_channel_se_s {
	uint8	eltId;		/* SE ID: P2P_SEID_OP_CHANNEL */
	uint8	len[2];		/* SE length not including eltId, len fields */
	uint8	country[3];	/* Country String */
	uint8	op_class;	/* Operating Class */
	uint8	channel;	/* Channel */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_op_channel_se_s wifi_p2p_op_channel_se_t;

/* WiFi IE subelement: INVITATION FLAGS */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_invite_flags_se_s {
	uint8	eltId;		/* SE ID: P2P_SEID_INVITE_FLAGS */
	uint8	len[2];		/* SE length not including eltId, len fields */
	uint8	flags;		/* Flags */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_invite_flags_se_s wifi_p2p_invite_flags_se_t;

/* WiFi P2P IE subelement: Service Hash */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_serv_hash_se_s {
	uint8	eltId;			/* SE ID: P2P_SEID_SERVICE_HASH */
	uint8	len[2];			/* SE length not including eltId, len fields
					 * in multiple of 6 Bytes
					*/
	uint8	hash[1];		/* Variable length - SHA256 hash of
					 * service names (can be more than one hashes)
					*/
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_serv_hash_se_s wifi_p2p_serv_hash_se_t;

/* WiFi P2P IE subelement: Service Instance Data */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_serv_inst_data_se_s {
	uint8	eltId;			/* SE ID: P2P_SEID_SESSION */
	uint8	len[2];			/* SE length not including eltId, len */
	uint8	ssn_info[1];		/* Variable length - Session information as specified by
					 * the service layer, type matches serv. name
					*/
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_serv_inst_data_se_s wifi_p2p_serv_inst_data_se_t;

/* WiFi P2P IE subelement: Connection capability */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_conn_cap_data_se_s {
	uint8	eltId;			/* SE ID: P2P_SEID_CONNECT_CAP */
	uint8	len[2];			/* SE length not including eltId, len */
	uint8	conn_cap;		/* 1byte capability as specified by the
					 * service layer, valid bitmask/values
					*/
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_conn_cap_data_se_s wifi_p2p_conn_cap_data_se_t;

/* WiFi P2P IE subelement: Advertisement ID */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_advt_id_se_s {
	uint8	eltId;			/* SE ID: P2P_SEID_ADVERTISE_ID */
	uint8	len[2];			/* SE length not including eltId, len fixed 4 Bytes */
	uint8	advt_id[4];		/* 4byte Advertisement ID of the peer device sent in
					 * PROV Disc in Network byte order
					*/
	uint8	advt_mac[6];			/* P2P device address of the service advertiser */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_advt_id_se_s wifi_p2p_advt_id_se_t;

/* WiFi P2P IE subelement: Advertise Service Hash */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_adv_serv_info_s {
	uint8	advt_id[4];		/* SE Advertise ID for the service */
	uint16	nw_cfg_method;	/* SE Network Config method for the service */
	uint8	serv_name_len;	/* SE length of the service name */
	uint8	serv_name[1];	/* Variable length service name field */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_adv_serv_info_s wifi_p2p_adv_serv_info_t;

/* WiFi P2P IE subelement: Advertise Service Hash */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_advt_serv_se_s {
	uint8	eltId;			/* SE ID: P2P_SEID_ADVERTISE_SERVICE */
	uint8	len[2];			/* SE length not including eltId, len fields mutiple len of
					 * wifi_p2p_adv_serv_info_t entries
					*/
	wifi_p2p_adv_serv_info_t	p_advt_serv_info[1]; /* Variable length
								of multiple instances
								of the advertise service info
								*/
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_advt_serv_se_s wifi_p2p_advt_serv_se_t;

/* WiFi P2P IE subelement: Session ID */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_ssn_id_se_s {
	uint8	eltId;			/* SE ID: P2P_SEID_SESSION_ID */
	uint8	len[2];			/* SE length not including eltId, len fixed 4 Bytes */
	uint8	ssn_id[4];		/* 4byte Session ID of the peer device sent in
							 * PROV Disc in Network byte order
							 */
	uint8	ssn_mac[6];		/* P2P device address of the seeker - session mac */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_ssn_id_se_s wifi_p2p_ssn_id_se_t;

#define P2P_ADVT_SERV_SE_FIXED_LEN	3	/* Includes only the element ID and len */
#define P2P_ADVT_SERV_INFO_FIXED_LEN	7	/* Per ADV Service Instance advt_id +
						 * nw_config_method + serv_name_len
						 */

/* WiFi P2P Action Frame */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_action_frame {
	uint8	category;	/* P2P_AF_CATEGORY */
	uint8	OUI[3];		/* OUI - P2P_OUI */
	uint8	type;		/* OUI Type - P2P_VER */
	uint8	subtype;	/* OUI Subtype - P2P_AF_* */
	uint8	dialog_token;	/* nonzero, identifies req/resp tranaction */
	uint8	elts[1];	/* Variable length information elements.  Max size =
				 * ACTION_FRAME_SIZE - sizeof(this structure) - 1
				 */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_action_frame wifi_p2p_action_frame_t;
#define P2P_AF_CATEGORY		0x7f

#define P2P_AF_FIXED_LEN	7

/* WiFi P2P Action Frame OUI Subtypes */
#define P2P_AF_NOTICE_OF_ABSENCE	0	/* Notice of Absence */
#define P2P_AF_PRESENCE_REQ		1	/* P2P Presence Request */
#define P2P_AF_PRESENCE_RSP		2	/* P2P Presence Response */
#define P2P_AF_GO_DISC_REQ		3	/* GO Discoverability Request */

/* WiFi P2P Public Action Frame */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_pub_act_frame {
	uint8	category;	/* P2P_PUB_AF_CATEGORY */
	uint8	action;		/* P2P_PUB_AF_ACTION */
	uint8	oui[3];		/* P2P_OUI */
	uint8	oui_type;	/* OUI type - P2P_VER */
	uint8	subtype;	/* OUI subtype - P2P_TYPE_* */
	uint8	dialog_token;	/* nonzero, identifies req/rsp transaction */
	uint8	elts[1];	/* Variable length information elements.  Max size =
				 * ACTION_FRAME_SIZE - sizeof(this structure) - 1
				 */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_pub_act_frame wifi_p2p_pub_act_frame_t;
#define P2P_PUB_AF_FIXED_LEN	8
#define P2P_PUB_AF_CATEGORY	0x04
#define P2P_PUB_AF_ACTION	0x09

/* WiFi P2P Public Action Frame OUI Subtypes */
#define P2P_PAF_GON_REQ		0	/* Group Owner Negotiation Req */
#define P2P_PAF_GON_RSP		1	/* Group Owner Negotiation Rsp */
#define P2P_PAF_GON_CONF	2	/* Group Owner Negotiation Confirm */
#define P2P_PAF_INVITE_REQ	3	/* P2P Invitation Request */
#define P2P_PAF_INVITE_RSP	4	/* P2P Invitation Response */
#define P2P_PAF_DEVDIS_REQ	5	/* Device Discoverability Request */
#define P2P_PAF_DEVDIS_RSP	6	/* Device Discoverability Response */
#define P2P_PAF_PROVDIS_REQ	7	/* Provision Discovery Request */
#define P2P_PAF_PROVDIS_RSP	8	/* Provision Discovery Response */
#define P2P_PAF_SUBTYPE_INVALID	255	/* Invalid Subtype */

/* TODO: Stop using these obsolete aliases for P2P_PAF_GON_* */
#define P2P_TYPE_MNREQ		P2P_PAF_GON_REQ
#define P2P_TYPE_MNRSP		P2P_PAF_GON_RSP
#define P2P_TYPE_MNCONF		P2P_PAF_GON_CONF

/* WiFi P2P IE subelement: Notice of Absence */
BWL_PRE_PACKED_STRUCT struct wifi_p2p_noa_desc {
	uint8	cnt_type;	/* Count/Type */
	uint32	duration;	/* Duration */
	uint32	interval;	/* Interval */
	uint32	start;		/* Start Time */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_noa_desc wifi_p2p_noa_desc_t;

BWL_PRE_PACKED_STRUCT struct wifi_p2p_noa_se {
	uint8	eltId;		/* Subelement ID */
	uint8	len[2];		/* Length */
	uint8	index;		/* Index */
	uint8	ops_ctw_parms;	/* CTWindow and OppPS Parameters */
	wifi_p2p_noa_desc_t	desc[1];	/* Notice of Absence Descriptor(s) */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2p_noa_se wifi_p2p_noa_se_t;

#define P2P_NOA_SE_FIXED_LEN	5

#define P2P_NOA_SE_MAX_DESC	2	/* max NoA descriptors in presence request */

/* cnt_type field values */
#define P2P_NOA_DESC_CNT_RESERVED	0	/* reserved and should not be used */
#define P2P_NOA_DESC_CNT_REPEAT		255	/* continuous schedule */
#define P2P_NOA_DESC_TYPE_PREFERRED	1	/* preferred values */
#define P2P_NOA_DESC_TYPE_ACCEPTABLE	2	/* acceptable limits */

/* ctw_ops_parms field values */
#define P2P_NOA_CTW_MASK	0x7f
#define P2P_NOA_OPS_MASK	0x80
#define P2P_NOA_OPS_SHIFT	7

#define P2P_CTW_MIN	10	/* minimum 10TU */

/*
 * P2P Service Discovery related
 */
#define	P2PSD_ACTION_CATEGORY		0x04
				/* Public action frame */
#define	P2PSD_ACTION_ID_GAS_IREQ	0x0a
				/* Action value for GAS Initial Request AF */
#define	P2PSD_ACTION_ID_GAS_IRESP	0x0b
				/* Action value for GAS Initial Response AF */
#define	P2PSD_ACTION_ID_GAS_CREQ	0x0c
				/* Action value for GAS Comeback Request AF */
#define	P2PSD_ACTION_ID_GAS_CRESP	0x0d
				/* Action value for GAS Comeback Response AF */
#define P2PSD_AD_EID				0x6c
				/* Advertisement Protocol IE ID */
#define P2PSD_ADP_TUPLE_QLMT_PAMEBI	0x00
				/* Query Response Length Limit 7 bits plus PAME-BI 1 bit */
#define P2PSD_ADP_PROTO_ID			0x00
				/* Advertisement Protocol ID. Always 0 for P2P SD */
#define P2PSD_GAS_OUI				P2P_OUI
				/* WFA OUI */
#define P2PSD_GAS_OUI_SUBTYPE		P2P_VER
				/* OUI Subtype for GAS IE */
#define P2PSD_GAS_NQP_INFOID		0xDDDD
				/* NQP Query Info ID: 56797 */
#define P2PSD_GAS_COMEBACKDEALY		0x00
				/* Not used in the Native GAS protocol */

/* Service Protocol Type */
typedef enum p2psd_svc_protype {
	SVC_RPOTYPE_ALL = 0,
	SVC_RPOTYPE_BONJOUR = 1,
	SVC_RPOTYPE_UPNP = 2,
	SVC_RPOTYPE_WSD = 3,
	SVC_RPOTYPE_WFDS = 11,
	SVC_RPOTYPE_VENDOR = 255
} p2psd_svc_protype_t;

/* Service Discovery response status code */
typedef enum {
	P2PSD_RESP_STATUS_SUCCESS = 0,
	P2PSD_RESP_STATUS_PROTYPE_NA = 1,
	P2PSD_RESP_STATUS_DATA_NA = 2,
	P2PSD_RESP_STATUS_BAD_REQUEST = 3
} p2psd_resp_status_t;

/* Advertisement Protocol IE tuple field */
BWL_PRE_PACKED_STRUCT struct wifi_p2psd_adp_tpl {
	uint8	llm_pamebi;	/* Query Response Length Limit bit 0-6, set to 0 plus
				* Pre-Associated Message Exchange BSSID Independent bit 7, set to 0
				*/
	uint8	adp_id;		/* Advertisement Protocol ID: 0 for NQP Native Query Protocol */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_adp_tpl wifi_p2psd_adp_tpl_t;

/* Advertisement Protocol IE */
BWL_PRE_PACKED_STRUCT struct wifi_p2psd_adp_ie {
	uint8	id;		/* IE ID: 0x6c - 108 */
	uint8	len;	/* IE length */
	wifi_p2psd_adp_tpl_t adp_tpl;  /* Advertisement Protocol Tuple field. Only one
				* tuple is defined for P2P Service Discovery
				*/
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_adp_ie wifi_p2psd_adp_ie_t;

/* NQP Vendor-specific Content */
BWL_PRE_PACKED_STRUCT struct wifi_p2psd_nqp_query_vsc {
	uint8	oui_subtype;	/* OUI Subtype: 0x09 */
	uint16	svc_updi;		/* Service Update Indicator */
	uint8	svc_tlvs[1];	/* wifi_p2psd_qreq_tlv_t type for service request,
				* wifi_p2psd_qresp_tlv_t type for service response
				*/
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_nqp_query_vsc wifi_p2psd_nqp_query_vsc_t;

/* Service Request TLV */
BWL_PRE_PACKED_STRUCT struct wifi_p2psd_qreq_tlv {
	uint16	len;			/* Length: 5 plus size of Query Data */
	uint8	svc_prot;		/* Service Protocol Type */
	uint8	svc_tscid;		/* Service Transaction ID */
	uint8	query_data[1];	/* Query Data, passed in from above Layer 2 */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_qreq_tlv wifi_p2psd_qreq_tlv_t;

/* Query Request Frame, defined in generic format, instead of NQP specific */
BWL_PRE_PACKED_STRUCT struct wifi_p2psd_qreq_frame {
	uint16	info_id;	/* Info ID: 0xDDDD */
	uint16	len;		/* Length of service request TLV, 5 plus the size of request data */
	uint8	oui[3];		/* WFA OUI: 0x0050F2 */
	uint8	qreq_vsc[1]; /* Vendor-specific Content: wifi_p2psd_nqp_query_vsc_t type for NQP */

} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_qreq_frame wifi_p2psd_qreq_frame_t;

/* GAS Initial Request AF body, "elts" in wifi_p2p_pub_act_frame */
BWL_PRE_PACKED_STRUCT struct wifi_p2psd_gas_ireq_frame {
	wifi_p2psd_adp_ie_t		adp_ie;		/* Advertisement Protocol IE */
	uint16					qreq_len;	/* Query Request Length */
	uint8	qreq_frm[1];	/* Query Request Frame wifi_p2psd_qreq_frame_t */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_gas_ireq_frame wifi_p2psd_gas_ireq_frame_t;

/* Service Response TLV */
BWL_PRE_PACKED_STRUCT struct wifi_p2psd_qresp_tlv {
	uint16	len;				/* Length: 5 plus size of Query Data */
	uint8	svc_prot;			/* Service Protocol Type */
	uint8	svc_tscid;			/* Service Transaction ID */
	uint8	status;				/* Value defined in Table 57 of P2P spec. */
	uint8	query_data[1];		/* Response Data, passed in from above Layer 2 */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_qresp_tlv wifi_p2psd_qresp_tlv_t;

/* Query Response Frame, defined in generic format, instead of NQP specific */
BWL_PRE_PACKED_STRUCT struct wifi_p2psd_qresp_frame {
	uint16	info_id;	/* Info ID: 0xDDDD */
	uint16	len;		/* Lenth of service response TLV, 6 plus the size of resp data */
	uint8	oui[3];		/* WFA OUI: 0x0050F2 */
	uint8	qresp_vsc[1]; /* Vendor-specific Content: wifi_p2psd_qresp_tlv_t type for NQP */

} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_qresp_frame wifi_p2psd_qresp_frame_t;

/* GAS Initial Response AF body, "elts" in wifi_p2p_pub_act_frame */
BWL_PRE_PACKED_STRUCT struct wifi_p2psd_gas_iresp_frame {
	uint16	status;			/* Value defined in Table 7-23 of IEEE P802.11u */
	uint16	cb_delay;		/* GAS Comeback Delay */
	wifi_p2psd_adp_ie_t	adp_ie;		/* Advertisement Protocol IE */
	uint16		qresp_len;	/* Query Response Length */
	uint8	qresp_frm[1];	/* Query Response Frame wifi_p2psd_qresp_frame_t */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_gas_iresp_frame wifi_p2psd_gas_iresp_frame_t;

/* GAS Comeback Response AF body, "elts" in wifi_p2p_pub_act_frame */
BWL_PRE_PACKED_STRUCT struct wifi_p2psd_gas_cresp_frame {
	uint16	status;			/* Value defined in Table 7-23 of IEEE P802.11u */
	uint8	fragment_id;	/* Fragmentation ID */
	uint16	cb_delay;		/* GAS Comeback Delay */
	wifi_p2psd_adp_ie_t	adp_ie;		/* Advertisement Protocol IE */
	uint16	qresp_len;		/* Query Response Length */
	uint8	qresp_frm[1];	/* Query Response Frame wifi_p2psd_qresp_frame_t */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_gas_cresp_frame wifi_p2psd_gas_cresp_frame_t;

/* Wi-Fi GAS Public Action Frame */
BWL_PRE_PACKED_STRUCT struct wifi_p2psd_gas_pub_act_frame {
	uint8	category;		/* 0x04 Public Action Frame */
	uint8	action;			/* 0x6c Advertisement Protocol */
	uint8	dialog_token;	/* nonzero, identifies req/rsp transaction */
	uint8	query_data[1];	/* Query Data. wifi_p2psd_gas_ireq_frame_t
					 * or wifi_p2psd_gas_iresp_frame_t format
					 */
} BWL_POST_PACKED_STRUCT;
typedef struct wifi_p2psd_gas_pub_act_frame wifi_p2psd_gas_pub_act_frame_t;

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif /* _P2P_H_ */
