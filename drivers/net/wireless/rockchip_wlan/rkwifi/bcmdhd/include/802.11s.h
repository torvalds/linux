/*
 * Fundamental types and constants relating to 802.11s Mesh
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
 * $Id: 802.11s.h 700076 2017-05-17 14:42:22Z $
 */

#ifndef _802_11s_h_
#define _802_11s_h_

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

#define DOT11_MESH_FLAGS_AE_MASK	0x3
#define DOT11_MESH_FLAGS_AE_SHIFT	0

#define DOT11_MESH_CONNECTED_AS_SET         7
#define DOT11_MESH_NUMBER_PEERING_SET       1
#define DOT11_MESH_MESH_GWSET               0

#define DOT11_MESH_ACTION_LINK_MET_REP      0
#define DOT11_MESH_ACTION_PATH_SEL          1
#define DOT11_MESH_ACTION_GATE_ANN          2
#define DOT11_MESH_ACTION_CONG_CONT_NOTIF   3
#define DOT11_MESH_ACTION_MCCA_SETUP_REQ    4
#define DOT11_MESH_ACTION_MCCA_SETUP_REP    5
#define DOT11_MESH_ACTION_MCCA_ADVT_REQ     6
#define DOT11_MESH_ACTION_MCCA_ADVT         7
#define DOT11_MESH_ACTION_MCCA_TEARDOWN     8
#define DOT11_MESH_ACTION_TBTT_ADJ_REQ      9
#define DOT11_MESH_ACTION_TBTT_ADJ_RESP     10

/* self-protected action field values: 7-57v24 */
#define DOT11_SELFPROT_ACTION_MESH_PEER_OPEN    1
#define DOT11_SELFPROT_ACTION_MESH_PEER_CONFM   2
#define DOT11_SELFPROT_ACTION_MESH_PEER_CLOSE   3
#define DOT11_SELFPROT_ACTION_MESH_PEER_GK_INF  4
#define DOT11_SELFPROT_ACTION_MESH_PEER_GK_ACK  5

#define DOT11_MESH_AUTH_PROTO_NONE        0
#define DOT11_MESH_AUTH_PROTO_SAE         1
#define DOT11_MESH_AUTH_PROTO_8021X       2
#define DOT11_MESH_AUTH_PROTO_VS        255

#define DOT11_MESH_PATHSEL_LEN   2
#define DOT11_MESH_PERR_LEN1     2   /* Least PERR length fixed */
#define DOT11_MESH_PERR_LEN2     13  /* Least PERR length variable */
#define DOT11_MESH_PREP_LEN      31  /* Least PREP length */
#define DOT11_MESH_PREQ_LEN      37  /* Least PREQ length */

#define DOT11_MESH_PATHSEL_PROTID_HWMP    1
#define DOT11_MESH_PATHSEL_METRICID_ALM   1 /* Air link metric */
#define DOT11_MESH_CONGESTCTRL_NONE       0
#define DOT11_MESH_CONGESTCTRL_SP         1
#define DOT11_MESH_SYNCMETHOD_NOFFSET     1

BWL_PRE_PACKED_STRUCT struct dot11_meshctrl_hdr {
	uint8               flags;      /* flag bits such as ae etc */
	uint8               ttl;        /* time to live */
	uint32              seq;        /* sequence control */
	struct ether_addr   a5;         /* optional address 5 */
	struct ether_addr   a6;         /* optional address 6 */
} BWL_POST_PACKED_STRUCT;

/* Mesh Path Selection Action Frame */
BWL_PRE_PACKED_STRUCT struct dot11_mesh_pathsel {
	uint8   category;
	uint8   meshaction;
	uint8   data[];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_mesh_pathsel dot11_mesh_pathsel_t;

/*  Mesh PREQ IE */
BWL_PRE_PACKED_STRUCT struct mesh_preq_ie {
	uint8   id;
	uint8   len;
	uint8   flags;
	uint8   hop_count;
	uint8   ttl;
	uint32  pathdis_id;
	struct  ether_addr  originator_addr;
	uint32  originator_seq;
	union {
		BWL_PRE_PACKED_STRUCT struct {
			struct ether_addr target_ext_add;
			uint32  lifetime;
			uint32  metric;
			uint8   target_count;
			uint8   data[];
		} BWL_POST_PACKED_STRUCT oea;

		BWL_PRE_PACKED_STRUCT struct {
			uint32  lifetime;
			uint32  metric;
			uint8   target_count;
			uint8   data[];
		} BWL_POST_PACKED_STRUCT noea;
	} u;
} BWL_POST_PACKED_STRUCT;
typedef struct mesh_preq_ie mesh_preq_ie_t;

/* Target info (part of Mesh PREQ IE) */
BWL_PRE_PACKED_STRUCT struct mesh_targetinfo {
	uint8	target_flag;
	struct	ether_addr	target_addr;
	uint32	target_seq;
} BWL_POST_PACKED_STRUCT;
typedef struct mesh_targetinfo mesh_targetinfo_t;


/* Mesh PREP IE */
BWL_PRE_PACKED_STRUCT struct mesh_prep_ie {
	uint8	id;
	uint8	len;
	uint8	flags;
	uint8	hop_count;
	uint8	ttl;
	struct	ether_addr	target_addr;
	uint32	target_seq;
	union {
		BWL_PRE_PACKED_STRUCT struct {
			struct ether_addr target_ext_add;
			uint32  lifetime;
			uint32  metric;
			uint8   target_count;
	                struct	ether_addr	originator_addr;
	                uint32	originator_seq;
		} BWL_POST_PACKED_STRUCT oea;

		BWL_PRE_PACKED_STRUCT struct {
			uint32  lifetime;
			uint32  metric;
			uint8   target_count;
	                struct	ether_addr	originator_addr;
	                uint32	originator_seq;
		} BWL_POST_PACKED_STRUCT noea;
	} u;
} BWL_POST_PACKED_STRUCT;
typedef struct mesh_prep_ie mesh_prep_ie_t;


/* Mesh PERR IE */
struct mesh_perr_ie {
	uint8	id;
	uint8	len;
	uint8	ttl;
	uint8	num_dest;
	uint8	data[];
};
typedef struct mesh_perr_ie mesh_perr_ie_t;

/* Destination info is part of PERR IE */
BWL_PRE_PACKED_STRUCT struct mesh_perr_destinfo {
	uint8	flags;
	struct	ether_addr	destination_addr;
	uint32	dest_seq;
	union {
		BWL_PRE_PACKED_STRUCT struct {
			struct  ether_addr      dest_ext_addr;
		} BWL_POST_PACKED_STRUCT dea;

		BWL_PRE_PACKED_STRUCT struct {
		/* 1 byte reason code to be populated manually in software */
			uint16	reason_code;
		} BWL_POST_PACKED_STRUCT nodea;
	} u;
} BWL_POST_PACKED_STRUCT;
typedef struct mesh_perr_destinfo mesh_perr_destinfo_t;

/* Mesh peering action frame hdr */
BWL_PRE_PACKED_STRUCT struct mesh_peering_frmhdr {
	uint8	category;
	uint8	action;
	union	{
		struct {
			uint16	capability;
		} open;
		struct {
			uint16	capability;
			uint16	AID;
		} confirm;
		uint8 data[1];
	} u;
} BWL_POST_PACKED_STRUCT;
typedef struct mesh_peering_frmhdr mesh_peering_frmhdr_t;

/* Mesh peering mgmt IE */
BWL_PRE_PACKED_STRUCT struct mesh_peer_mgmt_ie_common {
	uint16	mesh_peer_prot_id;
	uint16	local_link_id;
} BWL_POST_PACKED_STRUCT;
typedef struct mesh_peer_mgmt_ie_common mesh_peer_mgmt_ie_common_t;
#define MESH_PEER_MGMT_IE_OPEN_LEN	(4)

BWL_PRE_PACKED_STRUCT struct mesh_peer_mgmt_ie_cfm {
	mesh_peer_mgmt_ie_common_t	common;
	uint16	peer_link_id;
} BWL_POST_PACKED_STRUCT;
typedef struct mesh_peer_mgmt_ie_cfm mesh_peer_mgmt_ie_cfm_t;
#define MESH_PEER_MGMT_IE_CONF_LEN	(6)

BWL_PRE_PACKED_STRUCT struct mesh_peer_mgmt_ie_close {
	mesh_peer_mgmt_ie_common_t	common;
	/* uint16	peer_link_id;
	* simplicity: not supported, TODO for future
	*/
	uint16	reason_code;
} BWL_POST_PACKED_STRUCT;
typedef struct mesh_peer_mgmt_ie_close mesh_peer_mgmt_ie_close_t;
#define MESH_PEER_MGMT_IE_CLOSE_LEN	(6)

struct mesh_config_ie {
	uint8	activ_path_sel_prot_id;
	uint8	activ_path_sel_metric_id;
	uint8	cong_ctl_mode_id;
	uint8	sync_method_id;
	uint8	auth_prot_id;
	uint8	mesh_formation_info;
	uint8	mesh_cap;
};
typedef struct mesh_config_ie mesh_config_ie_t;
#define MESH_CONFIG_IE_LEN	(7)

/* Mesh peering states */
#define MESH_PEERING_IDLE               0
#define MESH_PEERING_OPEN_SNT           1
#define MESH_PEERING_CNF_RCVD           2
#define MESH_PEERING_OPEN_RCVD          3
#define MESH_PEERING_ESTAB              4
#define MESH_PEERING_HOLDING            5
#define MESH_PEERING_LAST_STATE         6
/* for debugging: mapping strings */
#define MESH_PEERING_STATE_STRINGS \
	{"IDLE  ", "OPNSNT", "CNFRCV", "OPNRCV", "ESTAB ", "HOLDNG"}

typedef BWL_PRE_PACKED_STRUCT struct mesh_peer_info {
	/* mesh_peer_instance as given in the spec. Note that, peer address
	 * is stored in scb
	 */
	uint16	mesh_peer_prot_id;
	uint16	local_link_id;
	uint16	peer_link_id;
	/* AID generated by *peer* to self & received in peer_confirm */
	uint16  peer_aid;

	/* TODO: no mention in spec? possibly used in PS case. Note that aid generated
	* from self to peer is stored in scb.
	*/
	uint8   state;
	/* TODO: struct mesh_peer_info *next; this field is required
	 * if multiple peerings per same src is allowed, which is
	 * true as per spec.
	 */
} BWL_POST_PACKED_STRUCT mesh_peer_info_t;

/* once an entry is added into mesh_peer_list, if peering is lost, it will
* get retried for peering, MAX_MESH_PEER_ENTRY_RETRIES times. after wards, it
* wont get retried and will be moved to MESH_PEER_ENTRY_STATE_TIMEDOUT state,
* until user adds it again explicitely, when its entry_state is changed
* to MESH_PEER_ENTRY_STATE_ACTIVE and tried again.
*/
#define MAX_MESH_SELF_PEER_ENTRY_RETRIES	3
#define MESH_SELF_PEER_ENTRY_STATE_ACTIVE	1
#define MESH_SELF_PEER_ENTRY_STATE_TIMEDOUT	2

/** Mesh Channel Switch Parameter IE data structure */
BWL_PRE_PACKED_STRUCT struct dot11_mcsp_body {
	uint8 ttl;           /* remaining number of hops allowed for this element. */
	uint8 flags;         /* attributes of this channel switch attempt */
	uint8 reason;        /* reason for the mesh channel switch */
	uint16 precedence;    /* random value in the range 0 to 65535 */
} BWL_POST_PACKED_STRUCT;

#define DOT11_MCSP_TTL_DEFAULT          1
#define DOT11_MCSP_FLAG_TRANS_RESTRICT	0x1	/* no transmit except frames with mcsp */
#define DOT11_MCSP_FLAG_INIT		0x2	/* initiates the channel switch attempt */
#define DOT11_MCSP_FLAG_REASON		0x4	/* validity of reason code field */
#define DOT11_MCSP_REASON_REGULATORY	0	/* meet regulatory requirements */
#define DOT11_MCSP_REASON_UNSPECIFIED	1	/* unspecified reason */

BWL_PRE_PACKED_STRUCT struct dot11_mesh_csp {
	uint8 id; /* id DOT11_MNG_MESH_CSP_ID */
	uint8 len; /* length of IE */
	struct dot11_mcsp_body body; /* body of the ie */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_mesh_csp dot11_mesh_csp_ie_t;
#define DOT11_MESH_CSP_IE_LEN    5       /* length of mesh channel switch parameter IE body */

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif  /* #ifndef _802_11s_H_ */
