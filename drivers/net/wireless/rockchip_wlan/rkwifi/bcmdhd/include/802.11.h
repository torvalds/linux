/*
 * Fundamental types and constants relating to 802.11
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef _802_11_H_
#define _802_11_H_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif

#ifndef _NET_ETHERNET_H_
#include <ethernet.h>
#endif

/* Include WPA definitions here for compatibility */
#include <wpa.h>

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

#define DOT11_TU_TO_US			1024	/* 802.11 Time Unit is 1024 microseconds */
#define DOT11_SEC_TO_TU			977u	/* 1000000 / DOT11_TU_TO_US = ~977 TU */

/* Generic 802.11 frame constants */
#define DOT11_A3_HDR_LEN		24	/* d11 header length with A3 */
#define DOT11_A4_HDR_LEN		30	/* d11 header length with A4 */
#define DOT11_MAC_HDR_LEN		DOT11_A3_HDR_LEN	/* MAC header length */
#define DOT11_FCS_LEN			4u	/* d11 FCS length */
#define DOT11_ICV_LEN			4	/* d11 ICV length */
#define DOT11_ICV_AES_LEN		8	/* d11 ICV/AES length */
#define DOT11_MAX_ICV_AES_LEN		16	/* d11 MAX ICV/AES length */
#define DOT11_QOS_LEN			2	/* d11 QoS length */
#define DOT11_HTC_LEN			4	/* d11 HT Control field length */

#define DOT11_KEY_INDEX_SHIFT		6	/* d11 key index shift */
#define DOT11_IV_LEN			4	/* d11 IV length */
#define DOT11_IV_TKIP_LEN		8	/* d11 IV TKIP length */
#define DOT11_IV_AES_OCB_LEN		4	/* d11 IV/AES/OCB length */
#define DOT11_IV_AES_CCM_LEN		8	/* d11 IV/AES/CCM length */
#define DOT11_IV_WAPI_LEN		18	/* d11 IV WAPI length */
/* TODO: Need to change DOT11_IV_MAX_LEN to 18, but currently unable to change as the old
 * branches are still referencing to this component.
 */
#define DOT11_IV_MAX_LEN		8	/* maximum iv len for any encryption */

/* Includes MIC */
#define DOT11_MAX_MPDU_BODY_LEN		2304	/* max MPDU body length */
/* A4 header + QoS + CCMP + PDU + ICV + FCS = 2352 */
#define DOT11_MAX_MPDU_LEN		(DOT11_A4_HDR_LEN + \
					 DOT11_QOS_LEN + \
					 DOT11_IV_AES_CCM_LEN + \
					 DOT11_MAX_MPDU_BODY_LEN + \
					 DOT11_ICV_LEN + \
					 DOT11_FCS_LEN)	/* d11 max MPDU length */

#define DOT11_MAX_SSID_LEN		32	/* d11 max ssid length */

/* dot11RTSThreshold */
#define DOT11_DEFAULT_RTS_LEN		2347	/* d11 default RTS length */
#define DOT11_MAX_RTS_LEN		2347	/* d11 max RTS length */

/* dot11FragmentationThreshold */
#define DOT11_MIN_FRAG_LEN		256	/* d11 min fragmentation length */
#define DOT11_MAX_FRAG_LEN		2346	/* Max frag is also limited by aMPDUMaxLength
						* of the attached PHY
						*/
#define DOT11_DEFAULT_FRAG_LEN		2346	/* d11 default fragmentation length */

/* dot11BeaconPeriod */
#define DOT11_MIN_BEACON_PERIOD		1	/* d11 min beacon period */
#define DOT11_MAX_BEACON_PERIOD		0xFFFF	/* d11 max beacon period */

/* dot11DTIMPeriod */
#define DOT11_MIN_DTIM_PERIOD		1	/* d11 min DTIM period */
#define DOT11_MAX_DTIM_PERIOD		0xFF	/* d11 max DTIM period */

/** 802.2 LLC/SNAP header used by 802.11 per 802.1H */
#define DOT11_LLC_SNAP_HDR_LEN		8	/* d11 LLC/SNAP header length */
/* minimum LLC header length; DSAP, SSAP, 8 bit Control (unnumbered) */
#define DOT11_LLC_HDR_LEN_MIN		3
#define DOT11_OUI_LEN			3	/* d11 OUI length */
BWL_PRE_PACKED_STRUCT struct dot11_llc_snap_header {
	uint8	dsap;				/* always 0xAA */
	uint8	ssap;				/* always 0xAA */
	uint8	ctl;				/* always 0x03 */
	uint8	oui[DOT11_OUI_LEN];		/* RFC1042: 0x00 0x00 0x00
						 * Bridge-Tunnel: 0x00 0x00 0xF8
						 */
	uint16	type;				/* ethertype */
} BWL_POST_PACKED_STRUCT;

/* RFC1042 header used by 802.11 per 802.1H */
#define RFC1042_HDR_LEN	(ETHER_HDR_LEN + DOT11_LLC_SNAP_HDR_LEN)	/* RCF1042 header length */

#define SFH_LLC_SNAP_SZ	(RFC1042_HDR_LEN)

#define COPY_SFH_LLCSNAP(dst, src) \
	do { \
		*((uint32 *)dst + 0) = *((uint32 *)src + 0); \
		*((uint32 *)dst + 1) = *((uint32 *)src + 1); \
		*((uint32 *)dst + 2) = *((uint32 *)src + 2); \
		*((uint32 *)dst + 3) = *((uint32 *)src + 3); \
		*((uint32 *)dst + 4) = *((uint32 *)src + 4); \
		*(uint16 *)((uint32 *)dst + 5) = *(uint16 *)((uint32 *)src + 5); \
	} while (0)

/* Generic 802.11 MAC header */
/**
 * N.B.: This struct reflects the full 4 address 802.11 MAC header.
 *		 The fields are defined such that the shorter 1, 2, and 3
 *		 address headers just use the first k fields.
 */
BWL_PRE_PACKED_STRUCT struct dot11_header {
	uint16			fc;		/* frame control */
	uint16			durid;		/* duration/ID */
	struct ether_addr	a1;		/* address 1 */
	struct ether_addr	a2;		/* address 2 */
	struct ether_addr	a3;		/* address 3 */
	uint16			seq;		/* sequence control */
	struct ether_addr	a4;		/* address 4 */
} BWL_POST_PACKED_STRUCT;

/* Control frames */

BWL_PRE_PACKED_STRUCT struct dot11_rts_frame {
	uint16			fc;		/* frame control */
	uint16			durid;		/* duration/ID */
	struct ether_addr	ra;		/* receiver address */
	struct ether_addr	ta;		/* transmitter address */
} BWL_POST_PACKED_STRUCT;
#define	DOT11_RTS_LEN		16		/* d11 RTS frame length */

BWL_PRE_PACKED_STRUCT struct dot11_cts_frame {
	uint16			fc;		/* frame control */
	uint16			durid;		/* duration/ID */
	struct ether_addr	ra;		/* receiver address */
} BWL_POST_PACKED_STRUCT;
#define	DOT11_CTS_LEN		10u		/* d11 CTS frame length */

BWL_PRE_PACKED_STRUCT struct dot11_ack_frame {
	uint16			fc;		/* frame control */
	uint16			durid;		/* duration/ID */
	struct ether_addr	ra;		/* receiver address */
} BWL_POST_PACKED_STRUCT;
#define	DOT11_ACK_LEN		10		/* d11 ACK frame length */

BWL_PRE_PACKED_STRUCT struct dot11_ps_poll_frame {
	uint16			fc;		/* frame control */
	uint16			durid;		/* AID */
	struct ether_addr	bssid;		/* receiver address, STA in AP */
	struct ether_addr	ta;		/* transmitter address */
} BWL_POST_PACKED_STRUCT;
#define	DOT11_PS_POLL_LEN	16		/* d11 PS poll frame length */

BWL_PRE_PACKED_STRUCT struct dot11_cf_end_frame {
	uint16			fc;		/* frame control */
	uint16			durid;		/* duration/ID */
	struct ether_addr	ra;		/* receiver address */
	struct ether_addr	bssid;		/* transmitter address, STA in AP */
} BWL_POST_PACKED_STRUCT;
#define	DOT11_CS_END_LEN	16		/* d11 CF-END frame length */

/**
 * RWL wifi protocol: The Vendor Specific Action frame is defined for vendor-specific signaling
 *  category+OUI+vendor specific content ( this can be variable)
 */
BWL_PRE_PACKED_STRUCT struct dot11_action_wifi_vendor_specific {
	uint8	category;
	uint8	OUI[3];
	uint8	type;
	uint8	subtype;
	uint8	data[1040];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_action_wifi_vendor_specific dot11_action_wifi_vendor_specific_t;

/** generic vendor specific action frame with variable length */
BWL_PRE_PACKED_STRUCT struct dot11_action_vs_frmhdr {
	uint8	category;
	uint8	OUI[3];
	uint8	type;
	uint8	subtype;
	uint8	data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_action_vs_frmhdr dot11_action_vs_frmhdr_t;

#define DOT11_ACTION_VS_HDR_LEN	6

#define BCM_ACTION_OUI_BYTE0	0x00
#define BCM_ACTION_OUI_BYTE1	0x90
#define BCM_ACTION_OUI_BYTE2	0x4c

/* BA/BAR Control parameters */
#define DOT11_BA_CTL_POLICY_NORMAL	0x0000	/* normal ack */
#define DOT11_BA_CTL_POLICY_NOACK	0x0001	/* no ack */
#define DOT11_BA_CTL_POLICY_MASK	0x0001	/* ack policy mask */

#define DOT11_BA_CTL_MTID		0x0002	/* multi tid BA */
#define DOT11_BA_CTL_COMPRESSED		0x0004	/* compressed bitmap */

#define DOT11_BA_CTL_NUMMSDU_MASK	0x0FC0	/* num msdu in bitmap mask */
#define DOT11_BA_CTL_NUMMSDU_SHIFT	6	/* num msdu in bitmap shift */

#define DOT11_BA_CTL_TID_MASK		0xF000	/* tid mask */
#define DOT11_BA_CTL_TID_SHIFT		12	/* tid shift */

/** control frame header (BA/BAR) */
BWL_PRE_PACKED_STRUCT struct dot11_ctl_header {
	uint16			fc;		/* frame control */
	uint16			durid;		/* duration/ID */
	struct ether_addr	ra;		/* receiver address */
	struct ether_addr	ta;		/* transmitter address */
} BWL_POST_PACKED_STRUCT;
#define DOT11_CTL_HDR_LEN	16		/* control frame hdr len */

/** BAR frame payload */
BWL_PRE_PACKED_STRUCT struct dot11_bar {
	uint16			bar_control;	/* BAR Control */
	uint16			seqnum;		/* Starting Sequence control */
} BWL_POST_PACKED_STRUCT;
#define DOT11_BAR_LEN		4		/* BAR frame payload length */

#define DOT11_BA_BITMAP_LEN	128		/* bitmap length */
#define DOT11_BA_CMP_BITMAP_LEN	8		/* compressed bitmap length */
/** BA frame payload */
BWL_PRE_PACKED_STRUCT struct dot11_ba {
	uint16			ba_control;	/* BA Control */
	uint16			seqnum;		/* Starting Sequence control */
	uint8			bitmap[DOT11_BA_BITMAP_LEN];	/* Block Ack Bitmap */
} BWL_POST_PACKED_STRUCT;
#define DOT11_BA_LEN		4		/* BA frame payload len (wo bitmap) */

/** Management frame header */
BWL_PRE_PACKED_STRUCT struct dot11_management_header {
	uint16			fc;		/* frame control */
	uint16			durid;		/* duration/ID */
	struct ether_addr	da;		/* receiver address */
	struct ether_addr	sa;		/* transmitter address */
	struct ether_addr	bssid;		/* BSS ID */
	uint16			seq;		/* sequence control */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_management_header dot11_management_header_t;
#define	DOT11_MGMT_HDR_LEN	24u		/* d11 management header length */

/* Management frame payloads */

BWL_PRE_PACKED_STRUCT struct dot11_bcn_prb {
	uint32			timestamp[2];
	uint16			beacon_interval;
	uint16			capability;
	uint8			ies[];
} BWL_POST_PACKED_STRUCT;
#define	DOT11_BCN_PRB_LEN	12		/* 802.11 beacon/probe frame fixed length */
#define	DOT11_BCN_PRB_FIXED_LEN	12u		/* 802.11 beacon/probe frame fixed length */

BWL_PRE_PACKED_STRUCT struct dot11_auth {
	uint16			alg;		/* algorithm */
	uint16			seq;		/* sequence control */
	uint16			status;		/* status code */
} BWL_POST_PACKED_STRUCT;
#define DOT11_AUTH_FIXED_LEN		6	/* length of auth frame without challenge IE */
#define DOT11_AUTH_SEQ_STATUS_LEN	4	/* length of auth frame without challenge IE and
						 * without algorithm
						 */

BWL_PRE_PACKED_STRUCT struct dot11_assoc_req {
	uint16			capability;	/* capability information */
	uint16			listen;		/* listen interval */
} BWL_POST_PACKED_STRUCT;
#define DOT11_ASSOC_REQ_FIXED_LEN	4	/* length of assoc frame without info elts */

BWL_PRE_PACKED_STRUCT struct dot11_reassoc_req {
	uint16			capability;	/* capability information */
	uint16			listen;		/* listen interval */
	struct ether_addr	ap;		/* Current AP address */
} BWL_POST_PACKED_STRUCT;
#define DOT11_REASSOC_REQ_FIXED_LEN	10	/* length of assoc frame without info elts */

BWL_PRE_PACKED_STRUCT struct dot11_assoc_resp {
	uint16			capability;	/* capability information */
	uint16			status;		/* status code */
	uint16			aid;		/* association ID */
} BWL_POST_PACKED_STRUCT;
#define DOT11_ASSOC_RESP_FIXED_LEN	6	/* length of assoc resp frame without info elts */

BWL_PRE_PACKED_STRUCT struct dot11_action_measure {
	uint8	category;
	uint8	action;
	uint8	token;
	uint8	data[1];
} BWL_POST_PACKED_STRUCT;
#define DOT11_ACTION_MEASURE_LEN	3	/* d11 action measurement header length */

BWL_PRE_PACKED_STRUCT struct dot11_action_ht_ch_width {
	uint8	category;
	uint8	action;
	uint8	ch_width;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct dot11_action_ht_mimops {
	uint8	category;
	uint8	action;
	uint8	control;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct dot11_action_sa_query {
	uint8	category;
	uint8	action;
	uint16	id;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct dot11_action_vht_oper_mode {
	uint8	category;
	uint8	action;
	uint8	mode;
} BWL_POST_PACKED_STRUCT;

/* These lengths assume 64 MU groups, as specified in 802.11ac-2013 */
#define DOT11_ACTION_GID_MEMBERSHIP_LEN  8    /* bytes */
#define DOT11_ACTION_GID_USER_POS_LEN   16    /* bytes */
BWL_PRE_PACKED_STRUCT struct dot11_action_group_id {
	uint8   category;
	uint8   action;
	uint8   membership_status[DOT11_ACTION_GID_MEMBERSHIP_LEN];
	uint8   user_position[DOT11_ACTION_GID_USER_POS_LEN];
} BWL_POST_PACKED_STRUCT;

#define SM_PWRSAVE_ENABLE	1
#define SM_PWRSAVE_MODE		2

/* ************* 802.11h related definitions. ************* */
BWL_PRE_PACKED_STRUCT struct dot11_power_cnst {
	uint8 id;
	uint8 len;
	uint8 power;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_power_cnst dot11_power_cnst_t;

BWL_PRE_PACKED_STRUCT struct dot11_power_cap {
	int8 min;
	int8 max;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_power_cap dot11_power_cap_t;

BWL_PRE_PACKED_STRUCT struct dot11_tpc_rep {
	uint8 id;
	uint8 len;
	uint8 tx_pwr;
	uint8 margin;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tpc_rep dot11_tpc_rep_t;
#define DOT11_MNG_IE_TPC_REPORT_SIZE	(sizeof(dot11_tpc_rep_t))
#define DOT11_MNG_IE_TPC_REPORT_LEN	2	/* length of IE data, not including 2 byte header */

BWL_PRE_PACKED_STRUCT struct dot11_supp_channels {
	uint8 id;
	uint8 len;
	uint8 first_channel;
	uint8 num_channels;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_supp_channels dot11_supp_channels_t;

/**
 * Extension Channel Offset IE: 802.11n-D1.0 spec. added sideband
 * offset for 40MHz operation.  The possible 3 values are:
 * 1 = above control channel
 * 3 = below control channel
 * 0 = no extension channel
 */
BWL_PRE_PACKED_STRUCT struct dot11_extch {
	uint8	id;		/* IE ID, 62, DOT11_MNG_EXT_CHANNEL_OFFSET */
	uint8	len;		/* IE length */
	uint8	extch;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_extch dot11_extch_ie_t;

BWL_PRE_PACKED_STRUCT struct dot11_brcm_extch {
	uint8	id;		/* IE ID, 221, DOT11_MNG_PROPR_ID */
	uint8	len;		/* IE length */
	uint8	oui[3];		/* Proprietary OUI, BRCM_PROP_OUI */
	uint8	type;           /* type indicates what follows */
	uint8	extch;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_brcm_extch dot11_brcm_extch_ie_t;

#define BRCM_EXTCH_IE_LEN	5
#define BRCM_EXTCH_IE_TYPE	53	/* 802.11n ID not yet assigned */
#define DOT11_EXTCH_IE_LEN	1
#define DOT11_EXT_CH_MASK	0x03	/* extension channel mask */
#define DOT11_EXT_CH_UPPER	0x01	/* ext. ch. on upper sb */
#define DOT11_EXT_CH_LOWER	0x03	/* ext. ch. on lower sb */
#define DOT11_EXT_CH_NONE	0x00	/* no extension ch.  */

BWL_PRE_PACKED_STRUCT struct dot11_action_frmhdr {
	uint8	category;
	uint8	action;
	uint8	data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_action_frmhdr dot11_action_frmhdr_t;

/* Action Field length */
#define DOT11_ACTION_CATEGORY_LEN	1u
#define DOT11_ACTION_ACTION_LEN		1u
#define DOT11_ACTION_DIALOG_TOKEN_LEN	1u
#define DOT11_ACTION_CAPABILITY_LEN	2u
#define DOT11_ACTION_STATUS_CODE_LEN	2u
#define DOT11_ACTION_REASON_CODE_LEN	2u
#define DOT11_ACTION_TARGET_CH_LEN	1u
#define DOT11_ACTION_OPER_CLASS_LEN	1u

#define DOT11_ACTION_FRMHDR_LEN	2

/** CSA IE data structure */
BWL_PRE_PACKED_STRUCT struct dot11_channel_switch {
	uint8 id;	/* id DOT11_MNG_CHANNEL_SWITCH_ID */
	uint8 len;	/* length of IE */
	uint8 mode;	/* mode 0 or 1 */
	uint8 channel;	/* channel switch to */
	uint8 count;	/* number of beacons before switching */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_channel_switch dot11_chan_switch_ie_t;

#define DOT11_SWITCH_IE_LEN	3	/* length of IE data, not including 2 byte header */
/* CSA mode - 802.11h-2003 $7.3.2.20 */
#define DOT11_CSA_MODE_ADVISORY		0	/* no DOT11_CSA_MODE_NO_TX restriction imposed */
#define DOT11_CSA_MODE_NO_TX		1	/* no transmission upon receiving CSA frame. */

BWL_PRE_PACKED_STRUCT struct dot11_action_switch_channel {
	uint8	category;
	uint8	action;
	dot11_chan_switch_ie_t chan_switch_ie;	/* for switch IE */
	dot11_brcm_extch_ie_t extch_ie;		/* extension channel offset */
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct dot11_csa_body {
	uint8 mode;	/* mode 0 or 1 */
	uint8 reg;	/* regulatory class */
	uint8 channel;	/* channel switch to */
	uint8 count;	/* number of beacons before switching */
} BWL_POST_PACKED_STRUCT;

/** 11n Extended Channel Switch IE data structure */
BWL_PRE_PACKED_STRUCT struct dot11_ext_csa {
	uint8 id;	/* id DOT11_MNG_EXT_CSA_ID */
	uint8 len;	/* length of IE */
	struct dot11_csa_body b;	/* body of the ie */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ext_csa dot11_ext_csa_ie_t;
#define DOT11_EXT_CSA_IE_LEN	4	/* length of extended channel switch IE body */

BWL_PRE_PACKED_STRUCT struct dot11_action_ext_csa {
	uint8	category;
	uint8	action;
	dot11_ext_csa_ie_t chan_switch_ie;	/* for switch IE */
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct dot11y_action_ext_csa {
	uint8	category;
	uint8	action;
	struct dot11_csa_body b;	/* body of the ie */
} BWL_POST_PACKED_STRUCT;

/**  Wide Bandwidth Channel Switch IE data structure */
BWL_PRE_PACKED_STRUCT struct dot11_wide_bw_channel_switch {
	uint8 id;				/* id DOT11_MNG_WIDE_BW_CHANNEL_SWITCH_ID */
	uint8 len;				/* length of IE */
	uint8 channel_width;			/* new channel width */
	uint8 center_frequency_segment_0;	/* center frequency segment 0 */
	uint8 center_frequency_segment_1;	/* center frequency segment 1 */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_wide_bw_channel_switch dot11_wide_bw_chan_switch_ie_t;

#define DOT11_WIDE_BW_SWITCH_IE_LEN     3       /* length of IE data, not including 2 byte header */

/** Channel Switch Wrapper IE data structure */
BWL_PRE_PACKED_STRUCT struct dot11_channel_switch_wrapper {
	uint8 id;				/* id DOT11_MNG_WIDE_BW_CHANNEL_SWITCH_ID */
	uint8 len;				/* length of IE */
	dot11_wide_bw_chan_switch_ie_t wb_chan_switch_ie;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_channel_switch_wrapper dot11_chan_switch_wrapper_ie_t;

/* Proposed wide bandwidth channel IE */
typedef enum wide_bw_chan_width {
	WIDE_BW_CHAN_WIDTH_20	= 0,
	WIDE_BW_CHAN_WIDTH_40	= 1,
	WIDE_BW_CHAN_WIDTH_80	= 2,
	WIDE_BW_CHAN_WIDTH_160	= 3,
	WIDE_BW_CHAN_WIDTH_80_80	= 4
} wide_bw_chan_width_t;

/**  Wide Bandwidth Channel IE data structure */
BWL_PRE_PACKED_STRUCT struct dot11_wide_bw_channel {
	uint8 id;				/* id DOT11_MNG_WIDE_BW_CHANNEL_ID */
	uint8 len;				/* length of IE */
	uint8 channel_width;			/* channel width */
	uint8 center_frequency_segment_0;	/* center frequency segment 0 */
	uint8 center_frequency_segment_1;	/* center frequency segment 1 */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_wide_bw_channel dot11_wide_bw_chan_ie_t;

#define DOT11_WIDE_BW_IE_LEN     3       /* length of IE data, not including 2 byte header */
/** VHT Transmit Power Envelope IE data structure */
BWL_PRE_PACKED_STRUCT struct dot11_vht_transmit_power_envelope {
	uint8 id;				/* id DOT11_MNG_WIDE_BW_CHANNEL_SWITCH_ID */
	uint8 len;				/* length of IE */
	uint8 transmit_power_info;
	uint8 local_max_transmit_power_20;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_vht_transmit_power_envelope dot11_vht_transmit_power_envelope_ie_t;

/* vht transmit power envelope IE length depends on channel width */
#define DOT11_VHT_TRANSMIT_PWR_ENVELOPE_IE_LEN_40MHZ	1
#define DOT11_VHT_TRANSMIT_PWR_ENVELOPE_IE_LEN_80MHZ	2
#define DOT11_VHT_TRANSMIT_PWR_ENVELOPE_IE_LEN_160MHZ	3

/* TPE Transmit Power Information Field */
#define DOT11_TPE_INFO_MAX_TX_PWR_CNT_MASK               0x07u
#define DOT11_TPE_INFO_MAX_TX_PWR_INTRPN_MASK            0x38u
#define DOT11_TPE_INFO_MAX_TX_PWR_INTRPN_SHIFT           3u
#define DOT11_TPE_INFO_MAX_TX_PWR_CAT_MASK               0xC0u
#define DOT11_TPE_INFO_MAX_TX_PWR_CAT_SHIFT              6u

/* TPE Transmit Power Information Field Accessor */
#define DOT11_TPE_INFO_MAX_TX_PWR_CNT(x) \
	(x & DOT11_TPE_INFO_MAX_TX_PWR_CNT_MASK)
#define DOT11_TPE_INFO_MAX_TX_PWR_INTRPN(x) \
	(((x) & DOT11_TPE_INFO_MAX_TX_PWR_INTRPN_MASK) >> \
	DOT11_TPE_INFO_MAX_TX_PWR_INTRPN_SHIFT)
#define DOT11_TPE_INFO_MAX_TX_PWR_CAT(x) \
	(((x) & DOT11_TPE_INFO_MAX_TX_PWR_CAT_MASK) >> \
	DOT11_TPE_INFO_MAX_TX_PWR_CAT_SHIFT)

/* Maximum Transmit Power Interpretation subfield */
#define DOT11_TPE_MAX_TX_PWR_INTRPN_LOCAL_EIRP              0u
#define DOT11_TPE_MAX_TX_PWR_INTRPN_LOCAL_EIRP_PSD          1u
#define DOT11_TPE_MAX_TX_PWR_INTRPN_REG_CLIENT_EIRP         2u
#define DOT11_TPE_MAX_TX_PWR_INTRPN_REG_CLIENT_EIRP_PSD     3u

/* Maximum Transmit Power category subfield  */
#define DOT11_TPE_MAX_TX_PWR_CAT_DEFAULT                 0u

/* Maximum Transmit Power category subfield in US */
#define DOT11_TPE_MAX_TX_PWR_CAT_US_DEFAULT              0u
#define DOT11_TPE_MAX_TX_PWR_CAT_US_SUB_DEV              1u

/* Maximum Transmit Power Count subfield values when
 * Maximum Transmit Power Interpretation subfield is 0 or 2
 */
#define DOT11_TPE_INFO_MAX_TX_CNT_EIRP_20_MHZ                  0u
#define DOT11_TPE_INFO_MAX_TX_CNT_EIRP_20_40_MHZ               1u
#define DOT11_TPE_INFO_MAX_TX_CNT_EIRP_20_40_80_MHZ            2u
#define DOT11_TPE_INFO_MAX_TX_CNT_EIRP_20_40_80_160_MHZ        3u

/* Maximum Transmit Power Count subfield values when
 * Maximum Transmit Power Interpretation subfield is 1 or 3
 */
#define DOT11_TPE_INFO_MAX_TX_CNT_PSD_VAL_0                 0u
#define DOT11_TPE_INFO_MAX_TX_CNT_PSD_VAL_1                 1u
#define DOT11_TPE_INFO_MAX_TX_CNT_PSD_VAL_2                 2u
#define DOT11_TPE_INFO_MAX_TX_CNT_PSD_VAL_3                 4u
#define DOT11_TPE_INFO_MAX_TX_CNT_PSD_VAL_4                 8u

#define DOT11_TPE_MAX_TX_PWR_EIRP_MIN                    -128 /* 0.5 db step */
#define DOT11_TPE_MAX_TX_PWR_EIRP_MAX                     126  /* 0.5 db step */
#define DOT11_TPE_MAX_TX_PWR_EIRP_NO_LIMIT                127  /* 0.5 db step */

#define DOT11_TPE_MAX_TX_PWR_PSD_BLOCKED                 -128
#define DOT11_TPE_MAX_TX_PWR_PSD_NO_LIMIT                 127u
/** Transmit Power Envelope IE data structure as per 11ax draft */
BWL_PRE_PACKED_STRUCT struct dot11_transmit_power_envelope {
	uint8 id;				/* id DOT11_MNG_WIDE_BW_CHANNEL_SWITCH_ID */
	uint8 len;				/* length of IE */
	uint8 transmit_power_info;
	uint8 max_transmit_power[]; /* Variable length */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_transmit_power_envelope dot11_transmit_power_envelope_ie_t;
/* id (1) + len (1) + transmit_power_info(1) + max_transmit_power(1) */
#define DOT11_TPE_ELEM_MIN_LEN  4u

BWL_PRE_PACKED_STRUCT struct dot11_obss_coex {
	uint8	id;
	uint8	len;
	uint8	info;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_obss_coex dot11_obss_coex_t;
#define DOT11_OBSS_COEXINFO_LEN	1	/* length of OBSS Coexistence INFO IE */

#define	DOT11_OBSS_COEX_INFO_REQ		0x01
#define	DOT11_OBSS_COEX_40MHZ_INTOLERANT	0x02
#define	DOT11_OBSS_COEX_20MHZ_WIDTH_REQ	0x04

BWL_PRE_PACKED_STRUCT struct dot11_obss_chanlist {
	uint8	id;
	uint8	len;
	uint8	regclass;
	uint8	chanlist[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_obss_chanlist dot11_obss_chanlist_t;
#define DOT11_OBSS_CHANLIST_FIXED_LEN	1	/* fixed length of regclass */

BWL_PRE_PACKED_STRUCT struct dot11_extcap_ie {
	uint8 id;
	uint8 len;
	uint8 cap[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_extcap_ie dot11_extcap_ie_t;

#define DOT11_EXTCAP_LEN_COEX	1
#define DOT11_EXTCAP_LEN_BT	3
#define DOT11_EXTCAP_LEN_IW	4
#define DOT11_EXTCAP_LEN_SI	6

#define DOT11_EXTCAP_LEN_TDLS	5
#define DOT11_11AC_EXTCAP_LEN_TDLS	8

#define DOT11_EXTCAP_LEN_FMS			2
#define DOT11_EXTCAP_LEN_PROXY_ARP		2
#define DOT11_EXTCAP_LEN_TFS			3
#define DOT11_EXTCAP_LEN_WNM_SLEEP		3
#define DOT11_EXTCAP_LEN_TIMBC			3
#define DOT11_EXTCAP_LEN_BSSTRANS		3
#define DOT11_EXTCAP_LEN_DMS			4
#define DOT11_EXTCAP_LEN_WNM_NOTIFICATION	6
#define DOT11_EXTCAP_LEN_TDLS_WBW		8
#define DOT11_EXTCAP_LEN_OPMODE_NOTIFICATION	8
#define DOT11_EXTCAP_LEN_TWT			10u
#define DOT11_EXTCAP_LEN_BCN_PROT		11u

/* TDLS Capabilities */
#define DOT11_TDLS_CAP_TDLS			37	/* TDLS support */
#define DOT11_TDLS_CAP_PU_BUFFER_STA		28	/* TDLS Peer U-APSD buffer STA support */
#define DOT11_TDLS_CAP_PEER_PSM			20	/* TDLS Peer PSM support */
#define DOT11_TDLS_CAP_CH_SW			30	/* TDLS Channel switch */
#define DOT11_TDLS_CAP_PROH			38	/* TDLS prohibited */
#define DOT11_TDLS_CAP_CH_SW_PROH		39	/* TDLS Channel switch prohibited */
#define DOT11_TDLS_CAP_TDLS_WIDER_BW		61	/* TDLS Wider Band-Width */

#define TDLS_CAP_MAX_BIT			39	/* TDLS max bit defined in ext cap */

/* FIXME: remove redundant DOT11_CAP_SAE_HASH_TO_ELEMENT */
#define DOT11_CAP_SAE_HASH_TO_ELEMENT		5u	/* SAE Hash-to-element support */
#define DOT11_EXT_RSN_CAP_SAE_H2E		5u	/* SAE Hash-to-element support */
/* FIXME: Use these temporary IDs until ANA assigns IDs */
#define DOT11_EXT_RSN_CAP_SAE_PK		6u	/* SAE-PK support */
/* Last bit in extended rsn capabilities (RSNXE) */
#define DOT11_EXT_RSN_CAP_MAX_BIT		DOT11_EXT_RSN_CAP_SAE_PK

BWL_PRE_PACKED_STRUCT struct dot11_rsnxe {
	uint8 id;	/* id DOT11_MNG_RSNXE_ID */
	uint8 len;
	uint8 cap[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rsnxe dot11_rsnxe_t;

#define RSNXE_CAP_LENGTH_MASK		(0x0f)
#define RSNXE_CAP_LENGTH(cap)		((uint8)(cap) & RSNXE_CAP_LENGTH_MASK)
#define RSNXE_SET_CAP_LENGTH(cap, len)\
		(cap = (cap & ~RSNXE_CAP_LENGTH_MASK) | ((uint8)(len) & RSNXE_CAP_LENGTH_MASK))

BWL_PRE_PACKED_STRUCT struct dot11_rejected_groups_ie {
	uint8 id;	/* DOT11_MNG_EXT_ID */
	uint8 len;
	uint8 id_ext; /* DOT11_MNG_REJECTED_GROUPS_ID */
	uint16 groups[];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rejected_groups_ie dot11_rejected_groups_ie_t;

/* 802.11h/802.11k Measurement Request/Report IEs */
/* Measurement Type field */
#define DOT11_MEASURE_TYPE_BASIC	0   /* d11 measurement basic type */
#define DOT11_MEASURE_TYPE_CCA		1   /* d11 measurement CCA type */
#define DOT11_MEASURE_TYPE_RPI		2   /* d11 measurement RPI type */
#define DOT11_MEASURE_TYPE_CHLOAD	3   /* d11 measurement Channel Load type */
#define DOT11_MEASURE_TYPE_NOISE	4   /* d11 measurement Noise Histogram type */
#define DOT11_MEASURE_TYPE_BEACON	5   /* d11 measurement Beacon type */
#define DOT11_MEASURE_TYPE_FRAME	6   /* d11 measurement Frame type */
#define DOT11_MEASURE_TYPE_STAT		7   /* d11 measurement STA Statistics type */
#define DOT11_MEASURE_TYPE_LCI		8   /* d11 measurement LCI type */
#define DOT11_MEASURE_TYPE_TXSTREAM	9   /* d11 measurement TX Stream type */
#define DOT11_MEASURE_TYPE_MCDIAGS	10  /* d11 measurement multicast diagnostics */
#define DOT11_MEASURE_TYPE_CIVICLOC	11  /* d11 measurement location civic */
#define DOT11_MEASURE_TYPE_LOC_ID	12  /* d11 measurement location identifier */
#define DOT11_MEASURE_TYPE_DIRCHANQ	13  /* d11 measurement dir channel quality */
#define DOT11_MEASURE_TYPE_DIRMEAS	14  /* d11 measurement directional */
#define DOT11_MEASURE_TYPE_DIRSTATS	15  /* d11 measurement directional stats */
#define DOT11_MEASURE_TYPE_FTMRANGE	16  /* d11 measurement Fine Timing */
#define DOT11_MEASURE_TYPE_PAUSE	255	/* d11 measurement pause type */

/* Measurement Request Modes */
#define DOT11_MEASURE_MODE_PARALLEL	(1<<0)	/* d11 measurement parallel */
#define DOT11_MEASURE_MODE_ENABLE	(1<<1)	/* d11 measurement enable */
#define DOT11_MEASURE_MODE_REQUEST	(1<<2)	/* d11 measurement request */
#define DOT11_MEASURE_MODE_REPORT	(1<<3)	/* d11 measurement report */
#define DOT11_MEASURE_MODE_DUR		(1<<4)	/* d11 measurement dur mandatory */
/* Measurement Report Modes */
#define DOT11_MEASURE_MODE_LATE		(1<<0)	/* d11 measurement late */
#define DOT11_MEASURE_MODE_INCAPABLE	(1<<1)	/* d11 measurement incapable */
#define DOT11_MEASURE_MODE_REFUSED	(1<<2)	/* d11 measurement refuse */
/* Basic Measurement Map bits */
#define DOT11_MEASURE_BASIC_MAP_BSS	((uint8)(1<<0))	/* d11 measurement basic map BSS */
#define DOT11_MEASURE_BASIC_MAP_OFDM	((uint8)(1<<1))	/* d11 measurement map OFDM */
#define DOT11_MEASURE_BASIC_MAP_UKNOWN	((uint8)(1<<2))	/* d11 measurement map unknown */
#define DOT11_MEASURE_BASIC_MAP_RADAR	((uint8)(1<<3))	/* d11 measurement map radar */
#define DOT11_MEASURE_BASIC_MAP_UNMEAS	((uint8)(1<<4))	/* d11 measurement map unmeasuremnt */

BWL_PRE_PACKED_STRUCT struct dot11_meas_req {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint8 channel;
	uint8 start_time[8];
	uint16 duration;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_meas_req dot11_meas_req_t;
#define DOT11_MNG_IE_MREQ_LEN 14	/* d11 measurement request IE length */
/* length of Measure Request IE data not including variable len */
#define DOT11_MNG_IE_MREQ_FIXED_LEN 3	/* d11 measurement request IE fixed length */

BWL_PRE_PACKED_STRUCT struct dot11_meas_req_loc {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	BWL_PRE_PACKED_STRUCT union
	{
		BWL_PRE_PACKED_STRUCT struct {
			uint8 subject;
			uint8 data[1];
		} BWL_POST_PACKED_STRUCT lci;
		BWL_PRE_PACKED_STRUCT struct {
			uint8 subject;
			uint8 type;  /* type of civic location */
			uint8 siu;   /* service interval units */
			uint16 si; /* service interval */
			uint8 data[1];
		} BWL_POST_PACKED_STRUCT civic;
		BWL_PRE_PACKED_STRUCT struct {
			uint8 subject;
			uint8 siu;   /* service interval units */
			uint16 si; /* service interval */
			uint8 data[1];
		} BWL_POST_PACKED_STRUCT locid;
		BWL_PRE_PACKED_STRUCT struct {
			uint16 max_init_delay;		/* maximum random initial delay */
			uint8 min_ap_count;
			uint8 data[1];
		} BWL_POST_PACKED_STRUCT ftm_range;
	} BWL_POST_PACKED_STRUCT req;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_meas_req_loc dot11_meas_req_loc_t;
#define DOT11_MNG_IE_MREQ_MIN_LEN           4	/* d11 measurement report IE length */
#define DOT11_MNG_IE_MREQ_LCI_FIXED_LEN     4	/* d11 measurement report IE length */
#define DOT11_MNG_IE_MREQ_CIVIC_FIXED_LEN   8	/* d11 measurement report IE length */
#define DOT11_MNG_IE_MREQ_FRNG_FIXED_LEN    6	/* d11 measurement report IE length */

BWL_PRE_PACKED_STRUCT struct dot11_lci_subelement {
	uint8 subelement;
	uint8 length;
	uint8 lci_data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_lci_subelement dot11_lci_subelement_t;

BWL_PRE_PACKED_STRUCT struct dot11_colocated_bssid_list_se {
	uint8 sub_id;
	uint8 length;
	uint8 max_bssid_ind; /* MaxBSSID Indicator */
	struct ether_addr bssid[1]; /* variable */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_colocated_bssid_list_se dot11_colocated_bssid_list_se_t;
#define DOT11_LCI_COLOCATED_BSSID_LIST_FIXED_LEN     3
#define DOT11_LCI_COLOCATED_BSSID_SUBELEM_ID         7

BWL_PRE_PACKED_STRUCT struct dot11_civic_subelement {
	uint8 type;  /* type of civic location */
	uint8 subelement;
	uint8 length;
	uint8 civic_data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_civic_subelement dot11_civic_subelement_t;

BWL_PRE_PACKED_STRUCT struct dot11_meas_rep {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	BWL_PRE_PACKED_STRUCT union
	{
		BWL_PRE_PACKED_STRUCT struct {
			uint8 channel;
			uint8 start_time[8];
			uint16 duration;
			uint8 map;
		} BWL_POST_PACKED_STRUCT basic;
		BWL_PRE_PACKED_STRUCT struct {
			uint8 subelement;
			uint8 length;
			uint8 data[1];
		} BWL_POST_PACKED_STRUCT lci;
		BWL_PRE_PACKED_STRUCT struct {
			uint8 type;  /* type of civic location */
			uint8 subelement;
			uint8 length;
			uint8 data[1];
		} BWL_POST_PACKED_STRUCT civic;
		BWL_PRE_PACKED_STRUCT struct {
			uint8 exp_tsf[8];
			uint8 subelement;
			uint8 length;
			uint8 data[1];
		} BWL_POST_PACKED_STRUCT locid;
		BWL_PRE_PACKED_STRUCT struct {
			uint8 entry_count;
			uint8 data[1];
		} BWL_POST_PACKED_STRUCT ftm_range;
		uint8 data[1];
	} BWL_POST_PACKED_STRUCT rep;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_meas_rep dot11_meas_rep_t;
#define DOT11_MNG_IE_MREP_MIN_LEN           5	/* d11 measurement report IE length */
#define DOT11_MNG_IE_MREP_LCI_FIXED_LEN     5	/* d11 measurement report IE length */
#define DOT11_MNG_IE_MREP_CIVIC_FIXED_LEN   6	/* d11 measurement report IE length */
#define DOT11_MNG_IE_MREP_LOCID_FIXED_LEN   13	/* d11 measurement report IE length */
#define DOT11_MNG_IE_MREP_BASIC_FIXED_LEN   15	/* d11 measurement report IE length */
#define DOT11_MNG_IE_MREP_FRNG_FIXED_LEN    4

/* length of Measure Report IE data not including variable len */
#define DOT11_MNG_IE_MREP_FIXED_LEN	3	/* d11 measurement response IE fixed length */

BWL_PRE_PACKED_STRUCT struct dot11_meas_rep_basic {
	uint8 channel;
	uint8 start_time[8];
	uint16 duration;
	uint8 map;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_meas_rep_basic dot11_meas_rep_basic_t;
#define DOT11_MEASURE_BASIC_REP_LEN	12	/* d11 measurement basic report length */

BWL_PRE_PACKED_STRUCT struct dot11_quiet {
	uint8 id;
	uint8 len;
	uint8 count;	/* TBTTs until beacon interval in quiet starts */
	uint8 period;	/* Beacon intervals between periodic quiet periods ? */
	uint16 duration;	/* Length of quiet period, in TU's */
	uint16 offset;	/* TU's offset from TBTT in Count field */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_quiet dot11_quiet_t;

BWL_PRE_PACKED_STRUCT struct chan_map_tuple {
	uint8 channel;
	uint8 map;
} BWL_POST_PACKED_STRUCT;
typedef struct chan_map_tuple chan_map_tuple_t;

BWL_PRE_PACKED_STRUCT struct dot11_ibss_dfs {
	uint8 id;
	uint8 len;
	uint8 eaddr[ETHER_ADDR_LEN];
	uint8 interval;
	chan_map_tuple_t map[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ibss_dfs dot11_ibss_dfs_t;

/* WME Elements */
#define WME_OUI			"\x00\x50\xf2"	/* WME OUI */
#define WME_OUI_LEN		3
#define WME_OUI_TYPE		2	/* WME type */
#define WME_TYPE		2	/* WME type, deprecated */
#define WME_SUBTYPE_IE		0	/* Information Element */
#define WME_SUBTYPE_PARAM_IE	1	/* Parameter Element */
#define WME_SUBTYPE_TSPEC	2	/* Traffic Specification */
#define WME_VER			1	/* WME version */

/* WME Access Category Indices (ACIs) */
#define AC_BE			0	/* Best Effort */
#define AC_BK			1	/* Background */
#define AC_VI			2	/* Video */
#define AC_VO			3	/* Voice */
#define AC_COUNT		4	/* number of ACs */

typedef uint8 ac_bitmap_t;	/* AC bitmap of (1 << AC_xx) */

#define AC_BITMAP_NONE		0x0	/* No ACs */
#define AC_BITMAP_ALL		0xf	/* All ACs */
#define AC_BITMAP_TST(ab, ac)	(((ab) & (1 << (ac))) != 0)
#define AC_BITMAP_SET(ab, ac)	(((ab) |= (1 << (ac))))
#define AC_BITMAP_RESET(ab, ac) (((ab) &= ~(1 << (ac))))

/* Management PKT Lifetime indices */
/* Removing flag checks 'WLTEST'
 * while merging MERGE BIS120RC4 to DINGO2
 */
#define MGMT_ALL		0xffff
#define MGMT_AUTH_LT	FC_SUBTYPE_AUTH
#define MGMT_ASSOC_LT	FC_SUBTYPE_ASSOC_REQ

/** WME Information Element (IE) */
BWL_PRE_PACKED_STRUCT struct wme_ie {
	uint8 oui[3];
	uint8 type;
	uint8 subtype;
	uint8 version;
	uint8 qosinfo;
} BWL_POST_PACKED_STRUCT;
typedef struct wme_ie wme_ie_t;
#define WME_IE_LEN 7	/* WME IE length */

BWL_PRE_PACKED_STRUCT struct edcf_acparam {
	uint8	ACI;
	uint8	ECW;
	uint16  TXOP;		/* stored in network order (ls octet first) */
} BWL_POST_PACKED_STRUCT;
typedef struct edcf_acparam edcf_acparam_t;

/** WME Parameter Element (PE) */
BWL_PRE_PACKED_STRUCT struct wme_param_ie {
	uint8 oui[3];
	uint8 type;
	uint8 subtype;
	uint8 version;
	uint8 qosinfo;
	uint8 rsvd;
	edcf_acparam_t acparam[AC_COUNT];
} BWL_POST_PACKED_STRUCT;
typedef struct wme_param_ie wme_param_ie_t;
#define WME_PARAM_IE_LEN            24          /* WME Parameter IE length */

/* QoS Info field for IE as sent from AP */
#define WME_QI_AP_APSD_MASK         0x80        /* U-APSD Supported mask */
#define WME_QI_AP_APSD_SHIFT        7           /* U-APSD Supported shift */
#define WME_QI_AP_COUNT_MASK        0x0f        /* Parameter set count mask */
#define WME_QI_AP_COUNT_SHIFT       0           /* Parameter set count shift */

/* QoS Info field for IE as sent from STA */
#define WME_QI_STA_MAXSPLEN_MASK    0x60        /* Max Service Period Length mask */
#define WME_QI_STA_MAXSPLEN_SHIFT   5           /* Max Service Period Length shift */
#define WME_QI_STA_APSD_ALL_MASK    0xf         /* APSD all AC bits mask */
#define WME_QI_STA_APSD_ALL_SHIFT   0           /* APSD all AC bits shift */
#define WME_QI_STA_APSD_BE_MASK     0x8         /* APSD AC_BE mask */
#define WME_QI_STA_APSD_BE_SHIFT    3           /* APSD AC_BE shift */
#define WME_QI_STA_APSD_BK_MASK     0x4         /* APSD AC_BK mask */
#define WME_QI_STA_APSD_BK_SHIFT    2           /* APSD AC_BK shift */
#define WME_QI_STA_APSD_VI_MASK     0x2         /* APSD AC_VI mask */
#define WME_QI_STA_APSD_VI_SHIFT    1           /* APSD AC_VI shift */
#define WME_QI_STA_APSD_VO_MASK     0x1         /* APSD AC_VO mask */
#define WME_QI_STA_APSD_VO_SHIFT    0           /* APSD AC_VO shift */

/* ACI */
#define EDCF_AIFSN_MIN               1           /* AIFSN minimum value */
#define EDCF_AIFSN_MAX               15          /* AIFSN maximum value */
#define EDCF_AIFSN_MASK              0x0f        /* AIFSN mask */
#define EDCF_ACM_MASK                0x10        /* ACM mask */
#define EDCF_ACI_MASK                0x60        /* ACI mask */
#define EDCF_ACI_SHIFT               5           /* ACI shift */
#define EDCF_AIFSN_SHIFT             12          /* 4 MSB(0xFFF) in ifs_ctl for AC idx */

/* ECW */
#define EDCF_ECW_MIN                 0           /* cwmin/cwmax exponent minimum value */
#define EDCF_ECW_MAX                 15          /* cwmin/cwmax exponent maximum value */
#define EDCF_ECW2CW(exp)             ((1 << (exp)) - 1)
#define EDCF_ECWMIN_MASK             0x0f        /* cwmin exponent form mask */
#define EDCF_ECWMAX_MASK             0xf0        /* cwmax exponent form mask */
#define EDCF_ECWMAX_SHIFT            4           /* cwmax exponent form shift */

/* TXOP */
#define EDCF_TXOP_MIN                0           /* TXOP minimum value */
#define EDCF_TXOP_MAX                65535       /* TXOP maximum value */
#define EDCF_TXOP2USEC(txop)         ((txop) << 5)

/* Default BE ACI value for non-WME connection STA */
#define NON_EDCF_AC_BE_ACI_STA          0x02

/* Default EDCF parameters that AP advertises for STA to use; WMM draft Table 12 */
#define EDCF_AC_BE_ACI_STA           0x03	/* STA ACI value for best effort AC */
#define EDCF_AC_BE_ECW_STA           0xA4	/* STA ECW value for best effort AC */
#define EDCF_AC_BE_TXOP_STA          0x0000	/* STA TXOP value for best effort AC */
#define EDCF_AC_BK_ACI_STA           0x27	/* STA ACI value for background AC */
#define EDCF_AC_BK_ECW_STA           0xA4	/* STA ECW value for background AC */
#define EDCF_AC_BK_TXOP_STA          0x0000	/* STA TXOP value for background AC */
#define EDCF_AC_VI_ACI_STA           0x42	/* STA ACI value for video AC */
#define EDCF_AC_VI_ECW_STA           0x43	/* STA ECW value for video AC */
#define EDCF_AC_VI_TXOP_STA          0x005e	/* STA TXOP value for video AC */
#define EDCF_AC_VO_ACI_STA           0x62	/* STA ACI value for audio AC */
#define EDCF_AC_VO_ECW_STA           0x32	/* STA ECW value for audio AC */
#define EDCF_AC_VO_TXOP_STA          0x002f	/* STA TXOP value for audio AC */

/* Default EDCF parameters that AP uses; WMM draft Table 14 */
#define EDCF_AC_BE_ACI_AP            0x03	/* AP ACI value for best effort AC */
#define EDCF_AC_BE_ECW_AP            0x64	/* AP ECW value for best effort AC */
#define EDCF_AC_BE_TXOP_AP           0x0000	/* AP TXOP value for best effort AC */
#define EDCF_AC_BK_ACI_AP            0x27	/* AP ACI value for background AC */
#define EDCF_AC_BK_ECW_AP            0xA4	/* AP ECW value for background AC */
#define EDCF_AC_BK_TXOP_AP           0x0000	/* AP TXOP value for background AC */
#define EDCF_AC_VI_ACI_AP            0x41	/* AP ACI value for video AC */
#define EDCF_AC_VI_ECW_AP            0x43	/* AP ECW value for video AC */
#define EDCF_AC_VI_TXOP_AP           0x005e	/* AP TXOP value for video AC */
#define EDCF_AC_VO_ACI_AP            0x61	/* AP ACI value for audio AC */
#define EDCF_AC_VO_ECW_AP            0x32	/* AP ECW value for audio AC */
#define EDCF_AC_VO_TXOP_AP           0x002f	/* AP TXOP value for audio AC */

/** EDCA Parameter IE */
BWL_PRE_PACKED_STRUCT struct edca_param_ie {
	uint8 qosinfo;
	uint8 rsvd;
	edcf_acparam_t acparam[AC_COUNT];
} BWL_POST_PACKED_STRUCT;
typedef struct edca_param_ie edca_param_ie_t;
#define EDCA_PARAM_IE_LEN            18          /* EDCA Parameter IE length */

/** QoS Capability IE */
BWL_PRE_PACKED_STRUCT struct qos_cap_ie {
	uint8 qosinfo;
} BWL_POST_PACKED_STRUCT;
typedef struct qos_cap_ie qos_cap_ie_t;

BWL_PRE_PACKED_STRUCT struct dot11_qbss_load_ie {
	uint8 id;			/* 11, DOT11_MNG_QBSS_LOAD_ID */
	uint8 length;
	uint16 station_count;		/* total number of STAs associated */
	uint8 channel_utilization;	/* % of time, normalized to 255, QAP sensed medium busy */
	uint16 aac;			/* available admission capacity */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_qbss_load_ie dot11_qbss_load_ie_t;
#define BSS_LOAD_IE_SIZE	7	/* BSS load IE size */

#define WLC_QBSS_LOAD_CHAN_FREE_MAX	0xff	/* max for channel free score */

/* Estimated Service Parameters (ESP) IE - 802.11-2016 9.4.2.174 */
typedef BWL_PRE_PACKED_STRUCT struct dot11_esp_ie {
	uint8		id;
	uint8		length;
	uint8		id_ext;
	/* variable len info */
	uint8		esp_info_lists[];
} BWL_POST_PACKED_STRUCT dot11_esp_ie_t;

#define DOT11_ESP_IE_HDR_SIZE	(OFFSETOF(dot11_esp_ie_t, esp_info_lists))

/* ESP Information list - 802.11-2016 9.4.2.174 */
typedef BWL_PRE_PACKED_STRUCT struct dot11_esp_ie_info_list {
	/* acess category, data format, ba win size */
	uint8		ac_df_baws;
	/* estimated air time fraction */
	uint8		eat_frac;
	/* data PPDU duration target (50us units) */
	uint8		ppdu_dur;
} BWL_POST_PACKED_STRUCT dot11_esp_ie_info_list_t;

#define DOT11_ESP_IE_INFO_LIST_SIZE	(sizeof(dot11_esp_ie_info_list_t))

#define DOT11_ESP_NBR_INFO_LISTS	4u	/* max nbr of esp information lists */
#define DOT11_ESP_INFO_LIST_AC_BK	0u	/* access category of esp information list AC_BK */
#define DOT11_ESP_INFO_LIST_AC_BE	1u	/* access category of esp information list AC_BE */
#define DOT11_ESP_INFO_LIST_AC_VI	2u	/* access category of esp information list AC_VI */
#define DOT11_ESP_INFO_LIST_AC_VO	3u	/* access category of esp information list AC_VO */

#define DOT11_ESP_INFO_LIST_DF_MASK    0x18		/* Data Format Mask */
#define DOT11_ESP_INFO_LIST_BAWS_MASK  0xE0		/* BA window size mask */

/* nom_msdu_size */
#define FIXED_MSDU_SIZE 0x8000		/* MSDU size is fixed */
#define MSDU_SIZE_MASK	0x7fff		/* (Nominal or fixed) MSDU size */

/* surplus_bandwidth */
/* Represented as 3 bits of integer, binary point, 13 bits fraction */
#define	INTEGER_SHIFT	13	/* integer shift */
#define FRACTION_MASK	0x1FFF	/* fraction mask */

/** Management Notification Frame */
BWL_PRE_PACKED_STRUCT struct dot11_management_notification {
	uint8 category;			/* DOT11_ACTION_NOTIFICATION */
	uint8 action;
	uint8 token;
	uint8 status;
	uint8 data[1];			/* Elements */
} BWL_POST_PACKED_STRUCT;
#define DOT11_MGMT_NOTIFICATION_LEN 4	/* Fixed length */

/** Timeout Interval IE */
BWL_PRE_PACKED_STRUCT struct ti_ie {
	uint8 ti_type;
	uint32 ti_val;
} BWL_POST_PACKED_STRUCT;
typedef struct ti_ie ti_ie_t;
#define TI_TYPE_REASSOC_DEADLINE	1
#define TI_TYPE_KEY_LIFETIME		2

#ifndef CISCO_AIRONET_OUI
#define CISCO_AIRONET_OUI	"\x00\x40\x96"	/* Cisco AIRONET OUI */
#endif
/* QoS FastLane IE. */
BWL_PRE_PACKED_STRUCT struct ccx_qfl_ie {
	uint8	id;		/* 221, DOT11_MNG_VS_ID */
	uint8	length;		/* 5 */
	uint8	oui[3];		/* 00:40:96 */
	uint8	type;		/* 11 */
	uint8	data;
} BWL_POST_PACKED_STRUCT;
typedef struct ccx_qfl_ie ccx_qfl_ie_t;
#define CCX_QFL_IE_TYPE	11
#define CCX_QFL_ENABLE_SHIFT	5
#define CCX_QFL_ENALBE (1 << CCX_QFL_ENABLE_SHIFT)

/* WME Action Codes */
#define WME_ADDTS_REQUEST	0	/* WME ADDTS request */
#define WME_ADDTS_RESPONSE	1	/* WME ADDTS response */
#define WME_DELTS_REQUEST	2	/* WME DELTS request */

/* WME Setup Response Status Codes */
#define WME_ADMISSION_ACCEPTED		0	/* WME admission accepted */
#define WME_INVALID_PARAMETERS		1	/* WME invalide parameters */
#define WME_ADMISSION_REFUSED		3	/* WME admission refused */

/* Macro to take a pointer to a beacon or probe response
 * body and return the char* pointer to the SSID info element
 */
#define BCN_PRB_SSID(body) ((char*)(body) + DOT11_BCN_PRB_LEN)

/* Authentication frame payload constants */
#define DOT11_OPEN_SYSTEM	0	/* d11 open authentication */
#define DOT11_SHARED_KEY	1	/* d11 shared authentication */
#define DOT11_FAST_BSS		2	/* d11 fast bss authentication */
#define DOT11_SAE		3	/* d11 simultaneous authentication of equals */
#define DOT11_FILS_SKEY		4	/* d11 fils shared key authentication w/o pfs */
#define DOT11_FILS_SKEY_PFS	5	/* d11 fils shared key authentication w/ pfs */
#define DOT11_FILS_PKEY		6	/* d11 fils public key authentication */
#define DOT11_MAX_AUTH_ALG  DOT11_FILS_PKEY /* maximum value of an auth alg */
#define DOT11_CHALLENGE_LEN	128	/* d11 challenge text length */

/* Frame control macros */
#define FC_PVER_MASK		0x3	/* PVER mask */
#define FC_PVER_SHIFT		0	/* PVER shift */
#define FC_TYPE_MASK		0xC	/* type mask */
#define FC_TYPE_SHIFT		2	/* type shift */
#define FC_SUBTYPE_MASK		0xF0	/* subtype mask */
#define FC_SUBTYPE_SHIFT	4	/* subtype shift */
#define FC_TODS			0x100	/* to DS */
#define FC_TODS_SHIFT		8	/* to DS shift */
#define FC_FROMDS		0x200	/* from DS */
#define FC_FROMDS_SHIFT		9	/* from DS shift */
#define FC_MOREFRAG		0x400	/* more frag. */
#define FC_MOREFRAG_SHIFT	10	/* more frag. shift */
#define FC_RETRY		0x800	/* retry */
#define FC_RETRY_SHIFT		11	/* retry shift */
#define FC_PM			0x1000	/* PM */
#define FC_PM_SHIFT		12	/* PM shift */
#define FC_MOREDATA		0x2000	/* more data */
#define FC_MOREDATA_SHIFT	13	/* more data shift */
#define FC_WEP			0x4000	/* WEP */
#define FC_WEP_SHIFT		14	/* WEP shift */
#define FC_ORDER		0x8000	/* order */
#define FC_ORDER_SHIFT		15	/* order shift */

/* sequence control macros */
#define SEQNUM_SHIFT		4	/* seq. number shift */
#define SEQNUM_MAX		0x1000	/* max seqnum + 1 */
#define FRAGNUM_MASK		0xF	/* frag. number mask */

/* Frame Control type/subtype defs */

/* FC Types */
#define FC_TYPE_MNG		0	/* management type */
#define FC_TYPE_CTL		1	/* control type */
#define FC_TYPE_DATA		2	/* data type */

/* Management Subtypes */
#define FC_SUBTYPE_ASSOC_REQ		0	/* assoc. request */
#define FC_SUBTYPE_ASSOC_RESP		1	/* assoc. response */
#define FC_SUBTYPE_REASSOC_REQ		2	/* reassoc. request */
#define FC_SUBTYPE_REASSOC_RESP		3	/* reassoc. response */
#define FC_SUBTYPE_PROBE_REQ		4	/* probe request */
#define FC_SUBTYPE_PROBE_RESP		5	/* probe response */
#define FC_SUBTYPE_BEACON		8	/* beacon */
#define FC_SUBTYPE_ATIM			9	/* ATIM */
#define FC_SUBTYPE_DISASSOC		10	/* disassoc. */
#define FC_SUBTYPE_AUTH			11	/* authentication */
#define FC_SUBTYPE_DEAUTH		12	/* de-authentication */
#define FC_SUBTYPE_ACTION		13	/* action */
#define FC_SUBTYPE_ACTION_NOACK		14	/* action no-ack */

/* Control Subtypes */
#define FC_SUBTYPE_TRIGGER		2	/* Trigger frame */
#define FC_SUBTYPE_NDPA                 5	/* NDPA  */
#define FC_SUBTYPE_CTL_WRAPPER		7	/* Control Wrapper */
#define FC_SUBTYPE_BLOCKACK_REQ		8	/* Block Ack Req */
#define FC_SUBTYPE_BLOCKACK		9	/* Block Ack */
#define FC_SUBTYPE_PS_POLL		10	/* PS poll */
#define FC_SUBTYPE_RTS			11	/* RTS */
#define FC_SUBTYPE_CTS			12	/* CTS */
#define FC_SUBTYPE_ACK			13	/* ACK */
#define FC_SUBTYPE_CF_END		14	/* CF-END */
#define FC_SUBTYPE_CF_END_ACK		15	/* CF-END ACK */

/* Data Subtypes */
#define FC_SUBTYPE_DATA			0	/* Data */
#define FC_SUBTYPE_DATA_CF_ACK		1	/* Data + CF-ACK */
#define FC_SUBTYPE_DATA_CF_POLL		2	/* Data + CF-Poll */
#define FC_SUBTYPE_DATA_CF_ACK_POLL	3	/* Data + CF-Ack + CF-Poll */
#define FC_SUBTYPE_NULL			4	/* Null */
#define FC_SUBTYPE_CF_ACK		5	/* CF-Ack */
#define FC_SUBTYPE_CF_POLL		6	/* CF-Poll */
#define FC_SUBTYPE_CF_ACK_POLL		7	/* CF-Ack + CF-Poll */
#define FC_SUBTYPE_QOS_DATA		8	/* QoS Data */
#define FC_SUBTYPE_QOS_DATA_CF_ACK	9	/* QoS Data + CF-Ack */
#define FC_SUBTYPE_QOS_DATA_CF_POLL	10	/* QoS Data + CF-Poll */
#define FC_SUBTYPE_QOS_DATA_CF_ACK_POLL	11	/* QoS Data + CF-Ack + CF-Poll */
#define FC_SUBTYPE_QOS_NULL		12	/* QoS Null */
#define FC_SUBTYPE_QOS_CF_POLL		14	/* QoS CF-Poll */
#define FC_SUBTYPE_QOS_CF_ACK_POLL	15	/* QoS CF-Ack + CF-Poll */

/* Data Subtype Groups */
#define FC_SUBTYPE_ANY_QOS(s)		(((s) & 8) != 0)
#define FC_SUBTYPE_ANY_NULL(s)		(((s) & 4) != 0)
#define FC_SUBTYPE_ANY_CF_POLL(s)	(((s) & 2) != 0)
#define FC_SUBTYPE_ANY_CF_ACK(s)	(((s) & 1) != 0)
#define FC_SUBTYPE_ANY_PSPOLL(s)	(((s) & 10) != 0)

/* Type/Subtype Combos */
#define FC_KIND_MASK		(FC_TYPE_MASK | FC_SUBTYPE_MASK)	/* FC kind mask */

#define FC_KIND(t, s)	(((t) << FC_TYPE_SHIFT) | ((s) << FC_SUBTYPE_SHIFT))	/* FC kind */

#define FC_SUBTYPE(fc)	(((fc) & FC_SUBTYPE_MASK) >> FC_SUBTYPE_SHIFT)	/* Subtype from FC */
#define FC_TYPE(fc)	(((fc) & FC_TYPE_MASK) >> FC_TYPE_SHIFT)	/* Type from FC */

#define FC_ASSOC_REQ	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_ASSOC_REQ)	/* assoc. request */
#define FC_ASSOC_RESP	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_ASSOC_RESP)	/* assoc. response */
#define FC_REASSOC_REQ	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_REASSOC_REQ)	/* reassoc. request */
#define FC_REASSOC_RESP	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_REASSOC_RESP)	/* reassoc. response */
#define FC_PROBE_REQ	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_PROBE_REQ)	/* probe request */
#define FC_PROBE_RESP	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_PROBE_RESP)	/* probe response */
#define FC_BEACON	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_BEACON)		/* beacon */
#define FC_ATIM		FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_ATIM)		/* ATIM */
#define FC_DISASSOC	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_DISASSOC)	/* disassoc */
#define FC_AUTH		FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_AUTH)		/* authentication */
#define FC_DEAUTH	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_DEAUTH)		/* deauthentication */
#define FC_ACTION	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_ACTION)		/* action */
#define FC_ACTION_NOACK	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_ACTION_NOACK)	/* action no-ack */

#define FC_CTL_TRIGGER	FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_TRIGGER)	/* Trigger frame */
#define FC_CTL_NDPA	FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_NDPA)	/* NDPA frame */
#define FC_CTL_WRAPPER	FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_CTL_WRAPPER)	/* Control Wrapper */
#define FC_BLOCKACK_REQ	FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_BLOCKACK_REQ)	/* Block Ack Req */
#define FC_BLOCKACK	FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_BLOCKACK)	/* Block Ack */
#define FC_PS_POLL	FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_PS_POLL)	/* PS poll */
#define FC_RTS		FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_RTS)		/* RTS */
#define FC_CTS		FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_CTS)		/* CTS */
#define FC_ACK		FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_ACK)		/* ACK */
#define FC_CF_END	FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_CF_END)		/* CF-END */
#define FC_CF_END_ACK	FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_CF_END_ACK)	/* CF-END ACK */

#define FC_DATA		FC_KIND(FC_TYPE_DATA, FC_SUBTYPE_DATA)		/* data */
#define FC_NULL_DATA	FC_KIND(FC_TYPE_DATA, FC_SUBTYPE_NULL)		/* null data */
#define FC_DATA_CF_ACK	FC_KIND(FC_TYPE_DATA, FC_SUBTYPE_DATA_CF_ACK)	/* data CF ACK */
#define FC_QOS_DATA	FC_KIND(FC_TYPE_DATA, FC_SUBTYPE_QOS_DATA)	/* QoS data */
#define FC_QOS_NULL	FC_KIND(FC_TYPE_DATA, FC_SUBTYPE_QOS_NULL)	/* QoS null */

/* QoS Control Field */

/* 802.1D Priority */
#define QOS_PRIO_SHIFT		0	/* QoS priority shift */
#define QOS_PRIO_MASK		0x0007	/* QoS priority mask */
#define QOS_PRIO(qos)		(((qos) & QOS_PRIO_MASK) >> QOS_PRIO_SHIFT)	/* QoS priority */

/* Traffic Identifier */
#define QOS_TID_SHIFT		0	/* QoS TID shift */
#define QOS_TID_MASK		0x000f	/* QoS TID mask */
#define QOS_TID(qos)		(((qos) & QOS_TID_MASK) >> QOS_TID_SHIFT)	/* QoS TID */

/* End of Service Period (U-APSD) */
#define QOS_EOSP_SHIFT		4	/* QoS End of Service Period shift */
#define QOS_EOSP_MASK		0x0010	/* QoS End of Service Period mask */
#define QOS_EOSP(qos)		(((qos) & QOS_EOSP_MASK) >> QOS_EOSP_SHIFT)	/* Qos EOSP */

/* Ack Policy */
#define QOS_ACK_NORMAL_ACK	0	/* Normal Ack */
#define QOS_ACK_NO_ACK		1	/* No Ack (eg mcast) */
#define QOS_ACK_NO_EXP_ACK	2	/* No Explicit Ack */
#define QOS_ACK_BLOCK_ACK	3	/* Block Ack */
#define QOS_ACK_SHIFT		5	/* QoS ACK shift */
#define QOS_ACK_MASK		0x0060	/* QoS ACK mask */
#define QOS_ACK(qos)		(((qos) & QOS_ACK_MASK) >> QOS_ACK_SHIFT)	/* QoS ACK */

/* A-MSDU flag */
#define QOS_AMSDU_SHIFT		7	/* AMSDU shift */
#define QOS_AMSDU_MASK		0x0080	/* AMSDU mask */

/* QOS Mesh Flags */
#define QOS_MESH_CTL_FLAG       0x0100u // Mesh Control Present
#define QOS_MESH_PSL_FLAG       0x0200u // Mesh Power Save Level
#define QOS_MESH_RSPI_FLAG      0x0400u // Mesh RSPI

/* QOS Mesh Accessor macros */
#define QOS_MESH_CTL(qos)       (((qos) & QOS_MESH_CTL_FLAG) != 0)
#define QOS_MESH_PSL(qos)       (((qos) & QOS_MESH_PSL_FLAG) != 0)
#define QOS_MESH_RSPI(qos)      (((qos) & QOS_MESH_RSPI_FLAG) != 0)

/* Management Frames */

/* Management Frame Constants */

/* Fixed fields */
#define DOT11_MNG_AUTH_ALGO_LEN		2	/* d11 management auth. algo. length */
#define DOT11_MNG_AUTH_SEQ_LEN		2	/* d11 management auth. seq. length */
#define DOT11_MNG_BEACON_INT_LEN	2	/* d11 management beacon interval length */
#define DOT11_MNG_CAP_LEN		2	/* d11 management cap. length */
#define DOT11_MNG_AP_ADDR_LEN		6	/* d11 management AP address length */
#define DOT11_MNG_LISTEN_INT_LEN	2	/* d11 management listen interval length */
#define DOT11_MNG_REASON_LEN		2	/* d11 management reason length */
#define DOT11_MNG_AID_LEN		2	/* d11 management AID length */
#define DOT11_MNG_STATUS_LEN		2	/* d11 management status length */
#define DOT11_MNG_TIMESTAMP_LEN		8	/* d11 management timestamp length */

/* DUR/ID field in assoc resp is 0xc000 | AID */
#define DOT11_AID_MASK				0x3fff	/* d11 AID mask */
#define DOT11_AID_OCTET_VAL_SHIFT		3u	/* AID octet value shift */
#define DOT11_AID_BIT_POS_IN_OCTET		0x07	/* AID bit position in octet */

/* Reason Codes */
#define DOT11_RC_RESERVED		0	/* d11 RC reserved */
#define DOT11_RC_UNSPECIFIED		1	/* Unspecified reason */
#define DOT11_RC_AUTH_INVAL		2	/* Previous authentication no longer valid */
#define DOT11_RC_DEAUTH_LEAVING		3	/* Deauthenticated because sending station
						 * is leaving (or has left) IBSS or ESS
						 */
#define DOT11_RC_INACTIVITY		4	/* Disassociated due to inactivity */
#define DOT11_RC_BUSY			5	/* Disassociated because AP is unable to handle
						 * all currently associated stations
						 */
#define DOT11_RC_INVAL_CLASS_2		6	/* Class 2 frame received from
						 * nonauthenticated station
						 */
#define DOT11_RC_INVAL_CLASS_3		7	/* Class 3 frame received from
						 *  nonassociated station
						 */
#define DOT11_RC_DISASSOC_LEAVING	8	/* Disassociated because sending station is
						 * leaving (or has left) BSS
						 */
#define DOT11_RC_NOT_AUTH		9	/* Station requesting (re)association is not
						 * authenticated with responding station
						 */
#define DOT11_RC_BAD_PC			10	/* Unacceptable power capability element */
#define DOT11_RC_BAD_CHANNELS		11	/* Unacceptable supported channels element */

/* 12 is unused by STA but could be used by AP/GO */
#define DOT11_RC_DISASSOC_BTM		12	/* Disassociated due to BSS Transition Magmt */

/* 13-23 are WPA/802.11i reason codes defined in wpa.h */

/* 32-39 are QSTA specific reasons added in 11e */
#define DOT11_RC_UNSPECIFIED_QOS	32	/* unspecified QoS-related reason */
#define DOT11_RC_INSUFFCIENT_BW		33	/* QAP lacks sufficient bandwidth */
#define DOT11_RC_EXCESSIVE_FRAMES	34	/* excessive number of frames need ack */
#define DOT11_RC_TX_OUTSIDE_TXOP	35	/* transmitting outside the limits of txop */
#define DOT11_RC_LEAVING_QBSS		36	/* QSTA is leaving the QBSS (or restting) */
#define DOT11_RC_BAD_MECHANISM		37	/* does not want to use the mechanism */
#define DOT11_RC_SETUP_NEEDED		38	/* mechanism needs a setup */
#define DOT11_RC_TIMEOUT		39	/* timeout */

#define DOT11_RC_MESH_PEERING_CANCELLED		52
#define DOT11_RC_MESH_MAX_PEERS			53
#define DOT11_RC_MESH_CONFIG_POLICY_VIOLN	54
#define DOT11_RC_MESH_CLOSE_RECVD		55
#define DOT11_RC_MESH_MAX_RETRIES		56
#define DOT11_RC_MESH_CONFIRM_TIMEOUT		57
#define DOT11_RC_MESH_INVALID_GTK		58
#define DOT11_RC_MESH_INCONSISTENT_PARAMS	59

#define DOT11_RC_MESH_INVALID_SEC_CAP		60
#define DOT11_RC_MESH_PATHERR_NOPROXYINFO	61
#define DOT11_RC_MESH_PATHERR_NOFWINFO		62
#define DOT11_RC_MESH_PATHERR_DSTUNREACH	63
#define DOT11_RC_MESH_MBSSMAC_EXISTS		64
#define DOT11_RC_MESH_CHANSWITCH_REGREQ		65
#define DOT11_RC_MESH_CHANSWITCH_UNSPEC		66

#define DOT11_RC_POOR_RSSI_CONDITIONS		71	/* Poor RSSI */
#define DOT11_RC_MAX			71	/* Reason codes > 71 are reserved */

#define DOT11_RC_TDLS_PEER_UNREACH	25
#define DOT11_RC_TDLS_DOWN_UNSPECIFIED	26

/* Status Codes */
#define DOT11_SC_SUCCESS		0	/* Successful */
#define DOT11_SC_FAILURE		1	/* Unspecified failure */
#define DOT11_SC_TDLS_WAKEUP_SCH_ALT 2	/* TDLS wakeup schedule rejected but alternative  */
					/* schedule provided */
#define DOT11_SC_TDLS_WAKEUP_SCH_REJ 3	/* TDLS wakeup schedule rejected */
#define DOT11_SC_TDLS_SEC_DISABLED	5	/* TDLS Security disabled */
#define DOT11_SC_LIFETIME_REJ		6	/* Unacceptable lifetime */
#define DOT11_SC_NOT_SAME_BSS		7	/* Not in same BSS */
#define DOT11_SC_CAP_MISMATCH		10	/* Cannot support all requested
						 * capabilities in the Capability
						 * Information field
						 */
#define DOT11_SC_REASSOC_FAIL		11	/* Reassociation denied due to inability
						 * to confirm that association exists
						 */
#define DOT11_SC_ASSOC_FAIL		12	/* Association denied due to reason
						 * outside the scope of this standard
						 */
#define DOT11_SC_AUTH_MISMATCH		13	/* Responding station does not support
						 * the specified authentication
						 * algorithm
						 */
#define DOT11_SC_AUTH_SEQ		14	/* Received an Authentication frame
						 * with authentication transaction
						 * sequence number out of expected
						 * sequence
						 */
#define DOT11_SC_AUTH_CHALLENGE_FAIL	15	/* Authentication rejected because of
						 * challenge failure
						 */
#define DOT11_SC_AUTH_TIMEOUT		16	/* Authentication rejected due to timeout
						 * waiting for next frame in sequence
						 */
#define DOT11_SC_ASSOC_BUSY_FAIL	17	/* Association denied because AP is
						 * unable to handle additional
						 * associated stations
						 */
#define DOT11_SC_ASSOC_RATE_MISMATCH	18	/* Association denied due to requesting
						 * station not supporting all of the
						 * data rates in the BSSBasicRateSet
						 * parameter
						 */
#define DOT11_SC_ASSOC_SHORT_REQUIRED	19	/* Association denied due to requesting
						 * station not supporting the Short
						 * Preamble option
						 */
#define DOT11_SC_ASSOC_PBCC_REQUIRED	20	/* Association denied due to requesting
						 * station not supporting the PBCC
						 * Modulation option
						 */
#define DOT11_SC_ASSOC_AGILITY_REQUIRED	21	/* Association denied due to requesting
						 * station not supporting the Channel
						 * Agility option
						 */
#define DOT11_SC_ASSOC_SPECTRUM_REQUIRED	22	/* Association denied because Spectrum
							 * Management capability is required.
							 */
#define DOT11_SC_ASSOC_BAD_POWER_CAP	23	/* Association denied because the info
						 * in the Power Cap element is
						 * unacceptable.
						 */
#define DOT11_SC_ASSOC_BAD_SUP_CHANNELS	24	/* Association denied because the info
						 * in the Supported Channel element is
						 * unacceptable
						 */
#define DOT11_SC_ASSOC_SHORTSLOT_REQUIRED	25	/* Association denied due to requesting
							 * station not supporting the Short Slot
							 * Time option
							 */
#define DOT11_SC_ASSOC_DSSSOFDM_REQUIRED 26	/* Association denied because requesting station
						 * does not support the DSSS-OFDM option
						 */
#define DOT11_SC_ASSOC_HT_REQUIRED	27	/* Association denied because the requesting
						 * station does not support HT features
						 */
#define DOT11_SC_ASSOC_R0KH_UNREACHABLE	28	/* Association denied due to AP
						 * being unable to reach the R0 Key Holder
						 */
#define DOT11_SC_ASSOC_TRY_LATER	30	/* Association denied temporarily, try again later
						 */
#define DOT11_SC_ASSOC_MFP_VIOLATION	31	/* Association denied due to Robust Management
						 * frame policy violation
						 */

#define DOT11_SC_POOR_RSSI_CONDN	34	/* Association denied due to poor RSSI */
#define	DOT11_SC_DECLINED		37	/* request declined */
#define	DOT11_SC_INVALID_PARAMS		38	/* One or more params have invalid values */
#define DOT11_SC_INVALID_PAIRWISE_CIPHER	42 /* invalid pairwise cipher */
#define	DOT11_SC_INVALID_AKMP		43	/* Association denied due to invalid AKMP */
#define DOT11_SC_INVALID_RSNIE_CAP	45	/* invalid RSN IE capabilities */
#define DOT11_SC_DLS_NOT_ALLOWED	48	/* DLS is not allowed in the BSS by policy */
#define	DOT11_SC_INVALID_PMKID		53	/* Association denied due to invalid PMKID */
#define	DOT11_SC_INVALID_MDID		54	/* Association denied due to invalid MDID */
#define	DOT11_SC_INVALID_FTIE		55	/* Association denied due to invalid FTIE */

#define DOT11_SC_ADV_PROTO_NOT_SUPPORTED	59	/* ad proto not supported */
#define DOT11_SC_NO_OUTSTAND_REQ			60	/* no outstanding req */
#define DOT11_SC_RSP_NOT_RX_FROM_SERVER		61	/* no response from server */
#define DOT11_SC_TIMEOUT					62	/* timeout */
#define DOT11_SC_QUERY_RSP_TOO_LARGE		63	/* query rsp too large */
#define DOT11_SC_SERVER_UNREACHABLE			65	/* server unreachable */

#define DOT11_SC_UNEXP_MSG			70	/* Unexpected message */
#define DOT11_SC_INVALID_SNONCE		71	/* Invalid SNonce */
#define DOT11_SC_INVALID_RSNIE		72	/* Invalid contents of RSNIE */

#define DOT11_SC_ANTICLOG_TOCKEN_REQUIRED	76	/* Anti-clogging tocken required */
#define DOT11_SC_INVALID_FINITE_CYCLIC_GRP	77	/* Invalid contents of RSNIE */
#define DOT11_SC_TRANSMIT_FAILURE		79      /* transmission failure */

#define DOT11_SC_TCLAS_RESOURCES_EXHAUSTED	81u	/* TCLAS resources exhausted */

#define DOT11_SC_TCLAS_PROCESSING_TERMINATED	97	/* End traffic classification */

#define DOT11_SC_ASSOC_VHT_REQUIRED		104	/* Association denied because the requesting
							 * station does not support VHT features.
							 */
#define DOT11_SC_UNKNOWN_PASSWORD_IDENTIFIER	123u	/* mismatch of password id */

#define DOT11_SC_SAE_HASH_TO_ELEMENT		126u	/* SAE Hash-to-element PWE required */
#define DOT11_SC_SAE_PK				127u	/* SAE PK required */

/* Requested TCLAS processing has been terminated by the AP due to insufficient QoS capacity. */
#define DOT11_SC_TCLAS_PROCESSING_TERMINATED_INSUFFICIENT_QOS	128u

/* Requested TCLAS processing has been terminated by the AP due to conflict with
 * higher layer QoS policies.
 */
#define DOT11_SC_TCLAS_PROCESSING_TERMINATED_POLICY_CONFLICT	129u

/* Info Elts, length of INFORMATION portion of Info Elts */
#define DOT11_MNG_DS_PARAM_LEN			1	/* d11 management DS parameter length */
#define DOT11_MNG_IBSS_PARAM_LEN		2	/* d11 management IBSS parameter length */

/* TIM Info element has 3 bytes fixed info in INFORMATION field,
 * followed by 1 to 251 bytes of Partial Virtual Bitmap
 */
#define DOT11_MNG_TIM_FIXED_LEN			3	/* d11 management TIM fixed length */
#define DOT11_MNG_TIM_DTIM_COUNT		0	/* d11 management DTIM count */
#define DOT11_MNG_TIM_DTIM_PERIOD		1	/* d11 management DTIM period */
#define DOT11_MNG_TIM_BITMAP_CTL		2	/* d11 management TIM BITMAP control  */
#define DOT11_MNG_TIM_PVB			3	/* d11 management TIM PVB */

#define DOT11_MNG_TIM_BITMAP_CTL_BCMC_MASK	0x01	/* Mask for bcmc bit in tim bitmap ctrl */
#define DOT11_MNG_TIM_BITMAP_CTL_PVBOFF_MASK	0xFE	/* Mask for partial virtual bitmap */

/* TLV defines */
#define TLV_TAG_OFF         0	/* tag offset */
#define TLV_LEN_OFF         1	/* length offset */
#define TLV_HDR_LEN         2	/* header length */
#define TLV_BODY_OFF        2	/* body offset */
#define TLV_BODY_LEN_MAX    255	/* max body length */
#define TLV_EXT_HDR_LEN     3u  /* extended IE header length */
#define TLV_EXT_BODY_OFF    3u  /* extended IE body offset */

/* Management Frame Information Element IDs */
enum dot11_tag_ids {
	DOT11_MNG_SSID_ID			= 0,	/* d11 management SSID id */
	DOT11_MNG_RATES_ID			= 1,	/* d11 management rates id */
	DOT11_MNG_FH_PARMS_ID			= 2,	/* d11 management FH parameter id */
	DOT11_MNG_DS_PARMS_ID			= 3,	/* d11 management DS parameter id */
	DOT11_MNG_CF_PARMS_ID			= 4,	/* d11 management CF parameter id */
	DOT11_MNG_TIM_ID			= 5,	/* d11 management TIM id */
	DOT11_MNG_IBSS_PARMS_ID			= 6,	/* d11 management IBSS parameter id */
	DOT11_MNG_COUNTRY_ID			= 7,	/* d11 management country id */
	DOT11_MNG_HOPPING_PARMS_ID		= 8,	/* d11 management hopping parameter id */
	DOT11_MNG_HOPPING_TABLE_ID		= 9,	/* d11 management hopping table id */
	DOT11_MNG_FTM_SYNC_INFO_ID		= 9,	/* 11mc D4.3 */
	DOT11_MNG_REQUEST_ID			= 10,	/* d11 management request id */
	DOT11_MNG_QBSS_LOAD_ID			= 11,	/* d11 management QBSS Load id */
	DOT11_MNG_EDCA_PARAM_ID			= 12,	/* 11E EDCA Parameter id */
	DOT11_MNG_TSPEC_ID			= 13,	/* d11 management TSPEC id */
	DOT11_MNG_TCLAS_ID			= 14,	/* d11 management TCLAS id */
	DOT11_MNG_CHALLENGE_ID			= 16,	/* d11 management chanllenge id */
	DOT11_MNG_PWR_CONSTRAINT_ID		= 32,	/* 11H PowerConstraint */
	DOT11_MNG_PWR_CAP_ID			= 33,	/* 11H PowerCapability */
	DOT11_MNG_TPC_REQUEST_ID		= 34,	/* 11H TPC Request */
	DOT11_MNG_TPC_REPORT_ID			= 35,	/* 11H TPC Report */
	DOT11_MNG_SUPP_CHANNELS_ID		= 36,	/* 11H Supported Channels */
	DOT11_MNG_CHANNEL_SWITCH_ID		= 37,	/* 11H ChannelSwitch Announcement */
	DOT11_MNG_MEASURE_REQUEST_ID		= 38,	/* 11H MeasurementRequest */
	DOT11_MNG_MEASURE_REPORT_ID		= 39,	/* 11H MeasurementReport */
	DOT11_MNG_QUIET_ID			= 40,	/* 11H Quiet */
	DOT11_MNG_IBSS_DFS_ID			= 41,	/* 11H IBSS_DFS */
	DOT11_MNG_ERP_ID			= 42,	/* d11 management ERP id */
	DOT11_MNG_TS_DELAY_ID			= 43,	/* d11 management TS Delay id */
	DOT11_MNG_TCLAS_PROC_ID			= 44,	/* d11 management TCLAS processing id */
	DOT11_MNG_HT_CAP			= 45,	/* d11 mgmt HT cap id */
	DOT11_MNG_QOS_CAP_ID			= 46,	/* 11E QoS Capability id */
	DOT11_MNG_NONERP_ID			= 47,	/* d11 management NON-ERP id */
	DOT11_MNG_RSN_ID			= 48,	/* d11 management RSN id */
	DOT11_MNG_EXT_RATES_ID			= 50,	/* d11 management ext. rates id */
	DOT11_MNG_AP_CHREP_ID			= 51,	/* 11k AP Channel report id */
	DOT11_MNG_NEIGHBOR_REP_ID		= 52,	/* 11k & 11v Neighbor report id */
	DOT11_MNG_RCPI_ID			= 53,	/* 11k RCPI */
	DOT11_MNG_MDIE_ID			= 54,	/* 11r Mobility domain id */
	DOT11_MNG_FTIE_ID			= 55,	/* 11r Fast Bss Transition id */
	DOT11_MNG_FT_TI_ID			= 56,	/* 11r Timeout Interval id */
	DOT11_MNG_RDE_ID			= 57,	/* 11r RIC Data Element id */
	DOT11_MNG_REGCLASS_ID			= 59,	/* d11 management regulatory class id */
	DOT11_MNG_EXT_CSA_ID			= 60,	/* d11 Extended CSA */
	DOT11_MNG_HT_ADD			= 61,	/* d11 mgmt additional HT info */
	DOT11_MNG_EXT_CHANNEL_OFFSET		= 62,	/* d11 mgmt ext channel offset */
	DOT11_MNG_BSS_AVR_ACCESS_DELAY_ID	= 63,	/* 11k bss average access delay */
	DOT11_MNG_ANTENNA_ID			= 64,	/* 11k antenna id */
	DOT11_MNG_RSNI_ID			= 65,	/* 11k RSNI id */
	DOT11_MNG_MEASUREMENT_PILOT_TX_ID	= 66,	/* 11k measurement pilot tx info id */
	DOT11_MNG_BSS_AVAL_ADMISSION_CAP_ID	= 67,	/* 11k bss aval admission cap id */
	DOT11_MNG_BSS_AC_ACCESS_DELAY_ID	= 68,	/* 11k bss AC access delay id */
	DOT11_MNG_WAPI_ID			= 68,	/* d11 management WAPI id */
	DOT11_MNG_TIME_ADVERTISE_ID		= 69,	/* 11p time advertisement */
	DOT11_MNG_RRM_CAP_ID			= 70,	/* 11k radio measurement capability */
	DOT11_MNG_MULTIPLE_BSSID_ID		= 71,	/* 11k multiple BSSID id */
	DOT11_MNG_HT_BSS_COEXINFO_ID		= 72,	/* d11 mgmt OBSS Coexistence INFO */
	DOT11_MNG_HT_BSS_CHANNEL_REPORT_ID	= 73,	/* d11 mgmt OBSS Intolerant Channel list */
	DOT11_MNG_HT_OBSS_ID			= 74,	/* d11 mgmt OBSS HT info */
	DOT11_MNG_MMIE_ID			= 76,	/* d11 mgmt MIC IE */
	DOT11_MNG_NONTRANS_BSSID_CAP_ID		= 83,	/* 11k nontransmitted BSSID capability */
	DOT11_MNG_MULTIPLE_BSSIDINDEX_ID	= 85,	/* 11k multiple BSSID index */
	DOT11_MNG_FMS_DESCR_ID			= 86,	/* 11v FMS descriptor */
	DOT11_MNG_FMS_REQ_ID			= 87,	/* 11v FMS request id */
	DOT11_MNG_FMS_RESP_ID			= 88,	/* 11v FMS response id */
	DOT11_MNG_BSS_MAX_IDLE_PERIOD_ID	= 90,	/* 11v bss max idle id */
	DOT11_MNG_TFS_REQUEST_ID		= 91,	/* 11v tfs request id */
	DOT11_MNG_TFS_RESPONSE_ID		= 92,	/* 11v tfs response id */
	DOT11_MNG_WNM_SLEEP_MODE_ID		= 93,	/* 11v wnm-sleep mode id */
	DOT11_MNG_TIMBC_REQ_ID			= 94,	/* 11v TIM broadcast request id */
	DOT11_MNG_TIMBC_RESP_ID			= 95,	/* 11v TIM broadcast response id */
	DOT11_MNG_CHANNEL_USAGE			= 97,	/* 11v channel usage */
	DOT11_MNG_TIME_ZONE_ID			= 98,	/* 11v time zone */
	DOT11_MNG_DMS_REQUEST_ID		= 99,	/* 11v dms request id */
	DOT11_MNG_DMS_RESPONSE_ID		= 100,	/* 11v dms response id */
	DOT11_MNG_LINK_IDENTIFIER_ID		= 101,	/* 11z TDLS Link Identifier IE */
	DOT11_MNG_WAKEUP_SCHEDULE_ID		= 102,	/* 11z TDLS Wakeup Schedule IE */
	DOT11_MNG_CHANNEL_SWITCH_TIMING_ID	= 104,	/* 11z TDLS Channel Switch Timing IE */
	DOT11_MNG_PTI_CONTROL_ID		= 105,	/* 11z TDLS PTI Control IE */
	DOT11_MNG_PU_BUFFER_STATUS_ID		= 106,	/* 11z TDLS PU Buffer Status IE */
	DOT11_MNG_INTERWORKING_ID		= 107,	/* 11u interworking */
	DOT11_MNG_ADVERTISEMENT_ID		= 108,	/* 11u advertisement protocol */
	DOT11_MNG_EXP_BW_REQ_ID			= 109,	/* 11u expedited bandwith request */
	DOT11_MNG_QOS_MAP_ID			= 110,	/* 11u QoS map set */
	DOT11_MNG_ROAM_CONSORT_ID		= 111,	/* 11u roaming consortium */
	DOT11_MNG_EMERGCY_ALERT_ID		= 112,	/* 11u emergency alert identifier */
	DOT11_MNG_MESH_CONFIG			= 113,	/* Mesh Configuration */
	DOT11_MNG_MESH_ID			= 114,	/* Mesh ID */
	DOT11_MNG_MESH_PEER_MGMT_ID		= 117,	/* Mesh PEER MGMT IE */
	DOT11_MNG_EXT_CAP_ID			= 127,	/* d11 mgmt ext capability */
	DOT11_MNG_EXT_PREQ_ID			= 130,	/* Mesh PREQ IE */
	DOT11_MNG_EXT_PREP_ID			= 131,	/* Mesh PREP IE */
	DOT11_MNG_EXT_PERR_ID			= 132,	/* Mesh PERR IE */
	DOT11_MNG_VHT_CAP_ID			= 191,	/* d11 mgmt VHT cap id */
	DOT11_MNG_VHT_OPERATION_ID		= 192,	/* d11 mgmt VHT op id */
	DOT11_MNG_EXT_BSSLOAD_ID		= 193,	/* d11 mgmt VHT extended bss load id */
	DOT11_MNG_WIDE_BW_CHANNEL_SWITCH_ID	= 194,	/* Wide BW Channel Switch IE */
	DOT11_MNG_VHT_TRANSMIT_POWER_ENVELOPE_ID= 195,	/* VHT transmit Power Envelope IE */
	DOT11_MNG_CHANNEL_SWITCH_WRAPPER_ID	= 196,	/* Channel Switch Wrapper IE */
	DOT11_MNG_AID_ID			= 197,	/* Association ID  IE */
	DOT11_MNG_OPER_MODE_NOTIF_ID		= 199,	/* d11 mgmt VHT oper mode notif */
	DOT11_MNG_RNR_ID			= 201,
	/* FIXME: Use these temp. IDs until ANA assigns IDs */
	DOT11_MNG_FTM_PARAMS_ID			= 206,	/* mcd3.2/2014 this is not final yet */
	DOT11_MNG_TWT_ID			= 216,	/* 11ah D5.0 */
	DOT11_MNG_WPA_ID			= 221,	/* d11 management WPA id */
	DOT11_MNG_PROPR_ID			= 221,	/* d11 management proprietary id */
	/* should start using this one instead of above two */
	DOT11_MNG_VS_ID				= 221,	/* d11 management Vendor Specific IE */
	DOT11_MNG_MESH_CSP_ID			= 222,	/* d11 Mesh Channel Switch Parameter */
	DOT11_MNG_FILS_IND_ID			= 240,	/* 11ai FILS Indication element */
	DOT11_MNG_FRAGMENT_ID			= 242, /* IE's fragment ID */
	DOT11_MNG_RSNXE_ID			= 244, /* RSN Extension Element (RSNXE) ID */

	/* The follwing ID extensions should be defined >= 255
	 * i.e. the values should include 255 (DOT11_MNG_ID_EXT_ID + ID Extension).
	 */
	DOT11_MNG_ID_EXT_ID			= 255	/* Element ID Extension 11mc D4.3 */
};

/* FILS and OCE ext ids */
#define FILS_EXTID_MNG_REQ_PARAMS		2u	/* FILS Request Parameters element */
#define DOT11_MNG_FILS_REQ_PARAMS		(DOT11_MNG_ID_EXT_ID + FILS_EXTID_MNG_REQ_PARAMS)
#define FILS_EXTID_MNG_KEY_CONFIRMATION_ID	3u	/* FILS Key Confirmation element */
#define DOT11_MNG_FILS_KEY_CONFIRMATION		(DOT11_MNG_ID_EXT_ID + \
						 FILS_EXTID_MNG_KEY_CONFIRMATION_ID)
#define FILS_EXTID_MNG_SESSION_ID		4u	/* FILS Session element */
#define DOT11_MNG_FILS_SESSION			(DOT11_MNG_ID_EXT_ID + FILS_EXTID_MNG_SESSION_ID)
#define FILS_EXTID_MNG_HLP_CONTAINER_ID		5u	/* FILS HLP Container element */
#define DOT11_MNG_FILS_HLP_CONTAINER		(DOT11_MNG_ID_EXT_ID + \
						 FILS_EXTID_MNG_HLP_CONTAINER_ID)
#define FILS_EXTID_MNG_KEY_DELIVERY_ID		7u	/* FILS Key Delivery element */
#define DOT11_MNG_FILS_KEY_DELIVERY		(DOT11_MNG_ID_EXT_ID + \
						 FILS_EXTID_MNG_KEY_DELIVERY_ID)
#define FILS_EXTID_MNG_WRAPPED_DATA_ID		8u	/* FILS Wrapped Data element */
#define DOT11_MNG_FILS_WRAPPED_DATA		(DOT11_MNG_ID_EXT_ID + \
						 FILS_EXTID_MNG_WRAPPED_DATA_ID)

#define OCE_EXTID_MNG_ESP_ID			11u	/* Estimated Service Parameters element */
#define DOT11_MNG_ESP				(DOT11_MNG_ID_EXT_ID + OCE_EXTID_MNG_ESP_ID)
#define FILS_EXTID_MNG_PUBLIC_KEY_ID		12u	/* FILS Public Key element */
#define DOT11_MNG_FILS_PUBLIC_KEY		(DOT11_MNG_ID_EXT_ID + FILS_EXTID_MNG_PUBLIC_KEY_ID)
#define FILS_EXTID_MNG_NONCE_ID			13u	/* FILS Nonce element */
#define DOT11_MNG_FILS_NONCE			(DOT11_MNG_ID_EXT_ID + FILS_EXTID_MNG_NONCE_ID)

#define EXT_MNG_OWE_DH_PARAM_ID			32u	/* OWE DH Param ID - RFC 8110 */
#define DOT11_MNG_OWE_DH_PARAM_ID		(DOT11_MNG_ID_EXT_ID + EXT_MNG_OWE_DH_PARAM_ID)
#define EXT_MSG_PASSWORD_IDENTIFIER_ID		33u	/* Password ID EID */
#define DOT11_MSG_PASSWORD_IDENTIFIER_ID	(DOT11_MNG_ID_EXT_ID + \
						 EXT_MSG_PASSWORD_IDENTIFIER_ID)
#define EXT_MNG_HE_CAP_ID			35u	/* HE Capabilities, 11ax */
#define DOT11_MNG_HE_CAP_ID			(DOT11_MNG_ID_EXT_ID + EXT_MNG_HE_CAP_ID)
#define EXT_MNG_HE_OP_ID			36u	/* HE Operation IE, 11ax */
#define DOT11_MNG_HE_OP_ID			(DOT11_MNG_ID_EXT_ID + EXT_MNG_HE_OP_ID)
#define EXT_MNG_UORA_ID				37u	/* UORA Parameter Set */
#define DOT11_MNG_UORA_ID			(DOT11_MNG_ID_EXT_ID + EXT_MNG_UORA_ID)
#define EXT_MNG_MU_EDCA_ID			38u	/* MU EDCA Parameter Set */
#define DOT11_MNG_MU_EDCA_ID			(DOT11_MNG_ID_EXT_ID + EXT_MNG_MU_EDCA_ID)
#define EXT_MNG_SRPS_ID				39u	/* Spatial Reuse Parameter Set */
#define DOT11_MNG_SRPS_ID			(DOT11_MNG_ID_EXT_ID + EXT_MNG_SRPS_ID)
#define EXT_MNG_BSSCOLOR_CHANGE_ID		42u	/* BSS Color Change Announcement */
#define DOT11_MNG_BSSCOLOR_CHANGE_ID		(DOT11_MNG_ID_EXT_ID + EXT_MNG_BSSCOLOR_CHANGE_ID)
#define OCV_EXTID_MNG_OCI_ID			54u     /* OCI element */
#define DOT11_MNG_OCI_ID			(DOT11_MNG_ID_EXT_ID + OCV_EXT_OCI_ID)
#define EXT_MNG_SHORT_SSID_ID			58u	/* SHORT SSID ELEMENT */
#define DOT11_MNG_SHORT_SSID_LIST_ID		(DOT11_MNG_ID_EXT_ID + EXT_MNG_SHORT_SSID_ID)
#define EXT_MNG_HE_6G_CAP_ID			59u	/* HE Extended Capabilities, 11ax */
#define DOT11_MNG_HE_6G_CAP_ID			(DOT11_MNG_ID_EXT_ID + EXT_MNG_HE_6G_CAP_ID)

#define MSCS_EXTID_MNG_DESCR_ID			88u	/* Ext ID for the MSCS descriptor */
#define DOT11_MNG_MSCS_DESCR_ID			(DOT11_MNG_ID_EXT_ID + MSCS_EXTID_MNG_DESCR_ID)

#define TCLAS_EXTID_MNG_MASK_ID			89u	/* Ext ID for the TCLAS Mask element */
#define DOT11_MNG_TCLASS_MASK_ID		(DOT11_MNG_ID_EXT_ID + TCLAS_EXTID_MNG_MASK_ID)

#define SAE_EXT_REJECTED_GROUPS_ID		92u	/* SAE Rejected Groups element */
#define DOT11_MNG_REJECTED_GROUPS_ID		(DOT11_MNG_ID_EXT_ID + SAE_EXT_REJECTED_GROUPS_ID)
#define SAE_EXT_ANTICLOG_TOKEN_CONTAINER_ID	93u	/* SAE Anti-clogging token container */
#define DOT11_MNG_ANTICLOG_TOKEN_CONTAINER_ID	(DOT11_MNG_ID_EXT_ID + \
						 SAE_EXT_ANTICLOG_TOKEN_CONTAINER_ID)
#define EXT_MNG_EHT_CAP_ID			100u	/* EHT Capabilities IE FIXME */
#define DOT11_MNG_EHT_CAP_ID			(DOT11_MNG_ID_EXT_ID + EXT_MNG_EHT_CAP_ID)
#define EXT_MNG_EHT_OP_ID			101u	/* EHT Operation IE # FIXME */
#define DOT11_MNG_EHT_OP_ID			(DOT11_MNG_ID_EXT_ID + EXT_MNG_EHT_OP_ID)

/* unassigned IDs for ranging parameter elements. To be updated after final
 * assignement.
 */
#define DOT11_MNG_FTM_RANGING_EXT_ID		100u /* 11AZ sounding mode parameter element */
#define DOT11_MNG_FTM_ISTA_AVAIL_EXT_ID		101u /* 11 AZ TN ISTA avaialability window */
#define DOT11_MNG_FTM_RSTA_AVAIL_EXT_ID		102u /* 11 AZ TN RSTA avaialability window */
#define DOT11_MNG_FTM_SECURE_LTF_EXT_ID		103u /* 11 AZ Secure LTF parameter element */

#define DOT11_FTM_NTB_SUB_ELT_ID		0u /* non-TB ranging parameter sub-element ID */
#define DOT11_FTM_TB_SUB_ELT_ID			1u /* TB ranging parameter sub-element ID */

/* deprecated definitions, do not use, to be deleted later */
#define FILS_HLP_CONTAINER_EXT_ID		FILS_EXTID_MNG_HLP_CONTAINER_ID
#define DOT11_ESP_EXT_ID			OCE_EXTID_MNG_ESP_ID
#define FILS_REQ_PARAMS_EXT_ID			FILS_EXTID_MNG_REQ_PARAMS
#define EXT_MNG_RAPS_ID				37u	/* OFDMA Random Access Parameter Set */
#define DOT11_MNG_RAPS_ID			(DOT11_MNG_ID_EXT_ID + EXT_MNG_RAPS_ID)
/* End of deprecated definitions */

#define DOT11_MNG_IE_ID_EXT_MATCH(_ie, _id) (\
	((_ie)->id == DOT11_MNG_ID_EXT_ID) && \
	((_ie)->len > 0) && \
	((_id) == ((uint8 *)(_ie) + TLV_HDR_LEN)[0]))

#define DOT11_MNG_IE_ID_EXT_INIT(_ie, _id, _len) do {\
		(_ie)->id = DOT11_MNG_ID_EXT_ID; \
		(_ie)->len = _len; \
		(_ie)->id_ext = _id; \
	} while (0)

/* Rate Defines */

/* Valid rates for the Supported Rates and Extended Supported Rates IEs.
 * Encoding is the rate in 500kbps units, rouding up for fractional values.
 * 802.11-2012, section 6.5.5.2, DATA_RATE parameter enumerates all the values.
 * The rate values cover DSSS, HR/DSSS, ERP, and OFDM phy rates.
 * The defines below do not cover the rates specific to 10MHz, {3, 4.5, 27},
 * and 5MHz, {1.5, 2.25, 3, 4.5, 13.5}, which are not supported by Broadcom devices.
 */

#define DOT11_RATE_1M   2       /* 1  Mbps in 500kbps units */
#define DOT11_RATE_2M   4       /* 2  Mbps in 500kbps units */
#define DOT11_RATE_5M5  11      /* 5.5 Mbps in 500kbps units */
#define DOT11_RATE_11M  22      /* 11 Mbps in 500kbps units */
#define DOT11_RATE_6M   12      /* 6  Mbps in 500kbps units */
#define DOT11_RATE_9M   18      /* 9  Mbps in 500kbps units */
#define DOT11_RATE_12M  24      /* 12 Mbps in 500kbps units */
#define DOT11_RATE_18M  36      /* 18 Mbps in 500kbps units */
#define DOT11_RATE_24M  48      /* 24 Mbps in 500kbps units */
#define DOT11_RATE_36M  72      /* 36 Mbps in 500kbps units */
#define DOT11_RATE_48M  96      /* 48 Mbps in 500kbps units */
#define DOT11_RATE_54M  108     /* 54 Mbps in 500kbps units */
#define DOT11_RATE_MAX  108     /* highest rate (54 Mbps) in 500kbps units */

/* Supported Rates and Extended Supported Rates IEs
 * The supported rates octets are defined a the MSB indicatin a Basic Rate
 * and bits 0-6 as the rate value
 */
#define DOT11_RATE_BASIC                0x80 /* flag for a Basic Rate */
#define DOT11_RATE_MASK                 0x7F /* mask for numeric part of rate */

/* BSS Membership Selector parameters
 * 802.11-2016 (and 802.11ax-D1.1), Sec 9.4.2.3
 * These selector values are advertised in Supported Rates and Extended Supported Rates IEs
 * in the supported rates list with the Basic rate bit set.
 * Constants below include the basic bit.
 */
#define DOT11_BSS_MEMBERSHIP_HT         0xFF  /* Basic 0x80 + 127, HT Required to join */
#define DOT11_BSS_MEMBERSHIP_VHT        0xFE  /* Basic 0x80 + 126, VHT Required to join */
#define DOT11_BSS_MEMBERSHIP_HE         0xFD  /* Basic 0x80 + 125, HE Required to join */
#define DOT11_BSS_SAE_HASH_TO_ELEMENT	123u	/* SAE Hash-to-element Required to join */

/* ERP info element bit values */
#define DOT11_MNG_ERP_LEN			1	/* ERP is currently 1 byte long */
#define DOT11_MNG_NONERP_PRESENT		0x01	/* NonERP (802.11b) STAs are present
							 *in the BSS
							 */
#define DOT11_MNG_USE_PROTECTION		0x02	/* Use protection mechanisms for
							 *ERP-OFDM frames
							 */
#define DOT11_MNG_BARKER_PREAMBLE		0x04	/* Short Preambles: 0 == allowed,
							 * 1 == not allowed
							 */
/* TS Delay element offset & size */
#define DOT11_MGN_TS_DELAY_LEN		4	/* length of TS DELAY IE */
#define TS_DELAY_FIELD_SIZE			4	/* TS DELAY field size */

/* Capability Information Field */
#define DOT11_CAP_ESS				0x0001	/* d11 cap. ESS */
#define DOT11_CAP_IBSS				0x0002	/* d11 cap. IBSS */
#define DOT11_CAP_POLLABLE			0x0004	/* d11 cap. pollable */
#define DOT11_CAP_POLL_RQ			0x0008	/* d11 cap. poll request */
#define DOT11_CAP_PRIVACY			0x0010	/* d11 cap. privacy */
#define DOT11_CAP_SHORT				0x0020	/* d11 cap. short */
#define DOT11_CAP_PBCC				0x0040	/* d11 cap. PBCC */
#define DOT11_CAP_AGILITY			0x0080	/* d11 cap. agility */
#define DOT11_CAP_SPECTRUM			0x0100	/* d11 cap. spectrum */
#define DOT11_CAP_QOS				0x0200	/* d11 cap. qos */
#define DOT11_CAP_SHORTSLOT			0x0400	/* d11 cap. shortslot */
#define DOT11_CAP_APSD				0x0800	/* d11 cap. apsd */
#define DOT11_CAP_RRM				0x1000	/* d11 cap. 11k radio measurement */
#define DOT11_CAP_CCK_OFDM			0x2000	/* d11 cap. CCK/OFDM */
#define DOT11_CAP_DELAY_BA			0x4000	/* d11 cap. delayed block ack */
#define DOT11_CAP_IMMEDIATE_BA			0x8000	/* d11 cap. immediate block ack */

/* Extended capabilities IE bitfields */
/* 20/40 BSS Coexistence Management support bit position */
#define DOT11_EXT_CAP_OBSS_COEX_MGMT		0u
/* Extended Channel Switching support bit position */
#define DOT11_EXT_CAP_EXT_CHAN_SWITCHING	2u
/* scheduled PSMP support bit position */
#define DOT11_EXT_CAP_SPSMP			6u
/*  Flexible Multicast Service */
#define DOT11_EXT_CAP_FMS			11u
/* proxy ARP service support bit position */
#define DOT11_EXT_CAP_PROXY_ARP			12u
/* Civic Location */
#define DOT11_EXT_CAP_CIVIC_LOC			14u
/* Geospatial Location */
#define DOT11_EXT_CAP_LCI			15u
/* Traffic Filter Service */
#define DOT11_EXT_CAP_TFS			16u
/* WNM-Sleep Mode */
#define DOT11_EXT_CAP_WNM_SLEEP			17u
/* TIM Broadcast service */
#define DOT11_EXT_CAP_TIMBC			18u
/* BSS Transition Management support bit position */
#define DOT11_EXT_CAP_BSSTRANS_MGMT		19u
/* Multiple BSSID support position */
#define DOT11_EXT_CAP_MULTIBSSID		22u
/* Direct Multicast Service */
#define DOT11_EXT_CAP_DMS			26u
/* Interworking support bit position */
#define DOT11_EXT_CAP_IW			31u
/* QoS map support bit position */
#define DOT11_EXT_CAP_QOS_MAP			32u
/* service Interval granularity bit position and mask */
#define DOT11_EXT_CAP_SI			41u
#define DOT11_EXT_CAP_SI_MASK			0x0E
/* Location Identifier service */
#define DOT11_EXT_CAP_IDENT_LOC			44u
/* WNM notification */
#define DOT11_EXT_CAP_WNM_NOTIF			46u
/* Operating mode notification - VHT (11ac D3.0 - 8.4.2.29) */
#define DOT11_EXT_CAP_OPER_MODE_NOTIF		62u
/* Fine timing measurement - D3.0 */
#define DOT11_EXT_CAP_FTM_RESPONDER		70u
#define DOT11_EXT_CAP_FTM_INITIATOR		71u /* tentative 11mcd3.0 */
#define DOT11_EXT_CAP_FILS			72u /* FILS Capability */
/* TWT support */
#define DOT11_EXT_CAP_TWT_REQUESTER		77u
#define DOT11_EXT_CAP_TWT_RESPONDER		78u
#define DOT11_EXT_CAP_OBSS_NB_RU_OFDMA		79u
/* FIXME: Use these temp. IDs until ANA assigns IDs */
#define DOT11_EXT_CAP_EMBSS_ADVERTISE		80u
/* SAE password ID */
#define DOT11_EXT_CAP_SAE_PWD_ID_INUSE		81u
#define DOT11_EXT_CAP_SAE_PWD_ID_USED_EXCLUSIVE	82u
/* Beacon Protection Enabled 802.11 D3.0 - 9.4.2.26
 * This field is reserved for a STA.
 */
#define DOT11_EXT_CAP_BCN_PROT			84u

/* Mirrored SCS (MSCS) support */
#define DOT11_EXT_CAP_MSCS			85u

/* TODO: Update DOT11_EXT_CAP_MAX_IDX to reflect the highest offset.
 * Note: DOT11_EXT_CAP_MAX_IDX must only be used in attach path.
 *       It will cause ROM invalidation otherwise.
 */
#define DOT11_EXT_CAP_MAX_IDX			85u

/* Remove this hack (DOT11_EXT_CAP_MAX_BIT_IDX) when no one
 * references DOT11_EXTCAP_LEN_MAX
 */
#define DOT11_EXT_CAP_MAX_BIT_IDX		95u	/* !!!update this please!!! */

/* Remove DOT11_EXTCAP_LEN_MAX when no one references it */
/* extended capability */
#ifndef DOT11_EXTCAP_LEN_MAX
#define DOT11_EXTCAP_LEN_MAX ((DOT11_EXT_CAP_MAX_BIT_IDX + 8) >> 3)
#endif
/* Remove dot11_extcap when no one references it */
BWL_PRE_PACKED_STRUCT struct dot11_extcap {
	uint8 extcap[DOT11_EXTCAP_LEN_MAX];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_extcap dot11_extcap_t;

/* VHT Operating mode bit fields -  (11ac D8.0/802.11-2016 - 9.4.1.53) */
#define DOT11_OPER_MODE_CHANNEL_WIDTH_SHIFT 0
#define DOT11_OPER_MODE_CHANNEL_WIDTH_MASK 0x3
#define DOT11_OPER_MODE_160_8080_BW_SHIFT 2
#define DOT11_OPER_MODE_160_8080_BW_MASK 0x04
#define DOT11_OPER_MODE_NOLDPC_SHIFT 3
#define DOT11_OPER_MODE_NOLDPC_MASK 0x08
#define DOT11_OPER_MODE_RXNSS_SHIFT 4
#define DOT11_OPER_MODE_RXNSS_MASK 0x70
#define DOT11_OPER_MODE_RXNSS_TYPE_SHIFT 7
#define DOT11_OPER_MODE_RXNSS_TYPE_MASK 0x80

#define DOT11_OPER_MODE_RESET_CHAN_WIDTH_160MHZ(oper_mode) \
	(oper_mode & (~(DOT11_OPER_MODE_CHANNEL_WIDTH_MASK | \
		DOT11_OPER_MODE_160_8080_BW_MASK)))
#define DOT11_OPER_MODE_SET_CHAN_WIDTH_160MHZ(oper_mode) \
	(oper_mode = (DOT11_OPER_MODE_RESET_CHAN_WIDTH_160MHZ(oper_mode) | \
		(DOT11_OPER_MODE_80MHZ | DOT11_OPER_MODE_160_8080_BW_MASK)))

#ifdef DOT11_OPER_MODE_LEFT_SHIFT_FIX

#define DOT11_OPER_MODE(type, nss, chanw) (\
	((type) << DOT11_OPER_MODE_RXNSS_TYPE_SHIFT &\
		 DOT11_OPER_MODE_RXNSS_TYPE_MASK) |\
	(((nss) - 1u) << DOT11_OPER_MODE_RXNSS_SHIFT & DOT11_OPER_MODE_RXNSS_MASK) |\
	((chanw) << DOT11_OPER_MODE_CHANNEL_WIDTH_SHIFT &\
		 DOT11_OPER_MODE_CHANNEL_WIDTH_MASK))

#define DOT11_D8_OPER_MODE(type, nss, ldpc, bw160_8080, chanw) (\
	((type) << DOT11_OPER_MODE_RXNSS_TYPE_SHIFT &\
		 DOT11_OPER_MODE_RXNSS_TYPE_MASK) |\
	(((nss) - 1u) << DOT11_OPER_MODE_RXNSS_SHIFT & DOT11_OPER_MODE_RXNSS_MASK) |\
	((ldpc) << DOT11_OPER_MODE_NOLDPC_SHIFT & DOT11_OPER_MODE_NOLDPC_MASK) |\
	((bw160_8080) << DOT11_OPER_MODE_160_8080_BW_SHIFT &\
		 DOT11_OPER_MODE_160_8080_BW_MASK) |\
	((chanw) << DOT11_OPER_MODE_CHANNEL_WIDTH_SHIFT &\
		 DOT11_OPER_MODE_CHANNEL_WIDTH_MASK))

#else

/* avoid invalidation from above fix on release branches, can be removed when older release
 * branches no longer use component/proto from trunk
 */

#define DOT11_OPER_MODE(type, nss, chanw) (\
	((type) << DOT11_OPER_MODE_RXNSS_TYPE_SHIFT &\
		 DOT11_OPER_MODE_RXNSS_TYPE_MASK) |\
	(((nss) - 1) << DOT11_OPER_MODE_RXNSS_SHIFT & DOT11_OPER_MODE_RXNSS_MASK) |\
	((chanw) << DOT11_OPER_MODE_CHANNEL_WIDTH_SHIFT &\
		 DOT11_OPER_MODE_CHANNEL_WIDTH_MASK))

#define DOT11_D8_OPER_MODE(type, nss, ldpc, bw160_8080, chanw) (\
	((type) << DOT11_OPER_MODE_RXNSS_TYPE_SHIFT &\
		 DOT11_OPER_MODE_RXNSS_TYPE_MASK) |\
	(((nss) - 1) << DOT11_OPER_MODE_RXNSS_SHIFT & DOT11_OPER_MODE_RXNSS_MASK) |\
	((ldpc) << DOT11_OPER_MODE_NOLDPC_SHIFT & DOT11_OPER_MODE_NOLDPC_MASK) |\
	((bw160_8080) << DOT11_OPER_MODE_160_8080_BW_SHIFT &\
		 DOT11_OPER_MODE_160_8080_BW_MASK) |\
	((chanw) << DOT11_OPER_MODE_CHANNEL_WIDTH_SHIFT &\
		 DOT11_OPER_MODE_CHANNEL_WIDTH_MASK))

#endif /* DOT11_OPER_MODE_LEFT_SHIFT_FIX */

#define DOT11_OPER_MODE_CHANNEL_WIDTH(mode) \
	(((mode) & DOT11_OPER_MODE_CHANNEL_WIDTH_MASK)\
		>> DOT11_OPER_MODE_CHANNEL_WIDTH_SHIFT)
#define DOT11_OPER_MODE_160_8080(mode) \
	(((mode) & DOT11_OPER_MODE_160_8080_BW_MASK)\
		>> DOT11_OPER_MODE_160_8080_BW_SHIFT)
#define DOT11_OPER_MODE_NOLDPC(mode) \
		(((mode) & DOT11_OPER_MODE_NOLDPC_MASK)\
			>> DOT11_OPER_MODE_NOLDPC_SHIFT)
#define DOT11_OPER_MODE_RXNSS(mode) \
	((((mode) & DOT11_OPER_MODE_RXNSS_MASK)		\
		>> DOT11_OPER_MODE_RXNSS_SHIFT) + 1)
#define DOT11_OPER_MODE_RXNSS_TYPE(mode) \
	(((mode) & DOT11_OPER_MODE_RXNSS_TYPE_MASK)\
		>> DOT11_OPER_MODE_RXNSS_TYPE_SHIFT)

#define DOT11_OPER_MODE_20MHZ 0
#define DOT11_OPER_MODE_40MHZ 1
#define DOT11_OPER_MODE_80MHZ 2
#define DOT11_OPER_MODE_160MHZ 3
#define DOT11_OPER_MODE_8080MHZ 3
#define DOT11_OPER_MODE_1608080MHZ 1

#define DOT11_OPER_MODE_CHANNEL_WIDTH_20MHZ(mode) (\
	((mode) & DOT11_OPER_MODE_CHANNEL_WIDTH_MASK) == DOT11_OPER_MODE_20MHZ)
#define DOT11_OPER_MODE_CHANNEL_WIDTH_40MHZ(mode) (\
	((mode) & DOT11_OPER_MODE_CHANNEL_WIDTH_MASK) == DOT11_OPER_MODE_40MHZ)
#define DOT11_OPER_MODE_CHANNEL_WIDTH_80MHZ(mode) (\
	((mode) & DOT11_OPER_MODE_CHANNEL_WIDTH_MASK) == DOT11_OPER_MODE_80MHZ)
#define DOT11_OPER_MODE_CHANNEL_WIDTH_160MHZ(mode) (\
	((mode) & DOT11_OPER_MODE_160_8080_BW_MASK))
#define DOT11_OPER_MODE_CHANNEL_WIDTH_8080MHZ(mode) (\
	((mode) & DOT11_OPER_MODE_160_8080_BW_MASK))

/* Operating mode information element 802.11ac D3.0 - 8.4.2.168 */
BWL_PRE_PACKED_STRUCT struct dot11_oper_mode_notif_ie {
	uint8 mode;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_oper_mode_notif_ie dot11_oper_mode_notif_ie_t;

#define DOT11_OPER_MODE_NOTIF_IE_LEN 1

/* Extended Capability Information Field */
#define DOT11_OBSS_COEX_MNG_SUPPORT	0x01	/* 20/40 BSS Coexistence Management support */

/*
 * Action Frame Constants
 */
#define DOT11_ACTION_HDR_LEN		2	/* action frame category + action field */
#define DOT11_ACTION_CAT_OFF		0	/* category offset */
#define DOT11_ACTION_ACT_OFF		1	/* action offset */

/* Action Category field (sec 8.4.1.11) */
#define DOT11_ACTION_CAT_ERR_MASK	0x80	/* category error mask */
#define DOT11_ACTION_CAT_MASK		0x7F	/* category mask */
#define DOT11_ACTION_CAT_SPECT_MNG	0	/* category spectrum management */
#define DOT11_ACTION_CAT_QOS		1	/* category QoS */
#define DOT11_ACTION_CAT_DLS		2	/* category DLS */
#define DOT11_ACTION_CAT_BLOCKACK	3	/* category block ack */
#define DOT11_ACTION_CAT_PUBLIC		4	/* category public */
#define DOT11_ACTION_CAT_RRM		5	/* category radio measurements */
#define DOT11_ACTION_CAT_FBT	6	/* category fast bss transition */
#define DOT11_ACTION_CAT_HT		7	/* category for HT */
#define DOT11_ACTION_CAT_SA_QUERY	8	/* security association query */
#define DOT11_ACTION_CAT_PDPA		9	/* protected dual of public action */
#define DOT11_ACTION_CAT_WNM		10	/* category for WNM */
#define DOT11_ACTION_CAT_UWNM		11	/* category for Unprotected WNM */
#define DOT11_ACTION_CAT_MESH		13	/* category for Mesh */
#define DOT11_ACTION_CAT_SELFPROT	15	/* category for Mesh, self protected */
#define DOT11_ACTION_NOTIFICATION	17

#define DOT11_ACTION_RAV_STREAMING	19	/* category for Robust AV streaming:
						 * SCS, MSCS, etc.
						 */

#define DOT11_ACTION_CAT_VHT		21	/* VHT action */
#define DOT11_ACTION_CAT_S1G		22	/* S1G action */
/* FIXME: Use temp. ID until ANA assigns one */
#define DOT11_ACTION_CAT_HE		27	/* HE action frame */
#define DOT11_ACTION_CAT_FILS		26	/* FILS action frame */
#define DOT11_ACTION_CAT_VSP		126	/* protected vendor specific */
#define DOT11_ACTION_CAT_VS		127	/* category Vendor Specific */

/* Spectrum Management Action IDs (sec 7.4.1) */
#define DOT11_SM_ACTION_M_REQ		0	/* d11 action measurement request */
#define DOT11_SM_ACTION_M_REP		1	/* d11 action measurement response */
#define DOT11_SM_ACTION_TPC_REQ		2	/* d11 action TPC request */
#define DOT11_SM_ACTION_TPC_REP		3	/* d11 action TPC response */
#define DOT11_SM_ACTION_CHANNEL_SWITCH	4	/* d11 action channel switch */
#define DOT11_SM_ACTION_EXT_CSA		5	/* d11 extened CSA for 11n */

/* QoS action ids */
#define DOT11_QOS_ACTION_ADDTS_REQ	0	/* d11 action ADDTS request */
#define DOT11_QOS_ACTION_ADDTS_RESP	1	/* d11 action ADDTS response */
#define DOT11_QOS_ACTION_DELTS		2	/* d11 action DELTS */
#define DOT11_QOS_ACTION_SCHEDULE	3	/* d11 action schedule */
#define DOT11_QOS_ACTION_QOS_MAP	4	/* d11 action QOS map */

/* HT action ids */
#define DOT11_ACTION_ID_HT_CH_WIDTH	0	/* notify channel width action id */
#define DOT11_ACTION_ID_HT_MIMO_PS	1	/* mimo ps action id */

/* Public action ids */
#define DOT11_PUB_ACTION_BSS_COEX_MNG	0	/* 20/40 Coexistence Management action id */
#define DOT11_PUB_ACTION_CHANNEL_SWITCH	4	/* d11 action channel switch */
#define DOT11_PUB_ACTION_VENDOR_SPEC	9	/* Vendor specific */
#define DOT11_PUB_ACTION_GAS_CB_REQ	12	/* GAS Comeback Request */
#define DOT11_PUB_ACTION_FTM_REQ	32	/* FTM request */
#define DOT11_PUB_ACTION_FTM		33	/* FTM measurement */
/* unassigned value. Will change after final assignement.
 * for now, use 34(same as FILS DISC) due to QT/TB/chipsim support from uCode
 */
#define DOT11_PUB_ACTION_FTM_LMR	34	/* FTM 11AZ Location Management Report */

#define DOT11_PUB_ACTION_FTM_REQ_TRIGGER_START	1u	/* FTM request start trigger */
#define DOT11_PUB_ACTION_FTM_REQ_TRIGGER_STOP	0u	/* FTM request stop trigger */

/* Block Ack action types */
#define DOT11_BA_ACTION_ADDBA_REQ	0	/* ADDBA Req action frame type */
#define DOT11_BA_ACTION_ADDBA_RESP	1	/* ADDBA Resp action frame type */
#define DOT11_BA_ACTION_DELBA		2	/* DELBA action frame type */

/* ADDBA action parameters */
#define DOT11_ADDBA_PARAM_AMSDU_SUP	0x0001	/* AMSDU supported under BA */
#define DOT11_ADDBA_PARAM_POLICY_MASK	0x0002	/* policy mask(ack vs delayed) */
#define DOT11_ADDBA_PARAM_POLICY_SHIFT	1	/* policy shift */
#define DOT11_ADDBA_PARAM_TID_MASK	0x003c	/* tid mask */
#define DOT11_ADDBA_PARAM_TID_SHIFT	2	/* tid shift */
#define DOT11_ADDBA_PARAM_BSIZE_MASK	0xffc0	/* buffer size mask */
#define DOT11_ADDBA_PARAM_BSIZE_SHIFT	6	/* buffer size shift */

#define DOT11_ADDBA_POLICY_DELAYED	0	/* delayed BA policy */
#define DOT11_ADDBA_POLICY_IMMEDIATE	1	/* immediate BA policy */

/* Fast Transition action types */
#define DOT11_FT_ACTION_FT_RESERVED		0
#define DOT11_FT_ACTION_FT_REQ			1	/* FBT request - for over-the-DS FBT */
#define DOT11_FT_ACTION_FT_RES			2	/* FBT response - for over-the-DS FBT */
#define DOT11_FT_ACTION_FT_CON			3	/* FBT confirm - for OTDS with RRP */
#define DOT11_FT_ACTION_FT_ACK			4	/* FBT ack */

/* DLS action types */
#define DOT11_DLS_ACTION_REQ			0	/* DLS Request */
#define DOT11_DLS_ACTION_RESP			1	/* DLS Response */
#define DOT11_DLS_ACTION_TD			2	/* DLS Teardown */

/* Robust Audio Video streaming action types */
#define DOT11_RAV_SCS_REQ			0	/* SCS Request */
#define DOT11_RAV_SCS_RES			1	/* SCS Response */
#define DOT11_RAV_GM_REQ			2	/* Group Membership Request */
#define DOT11_RAV_GM_RES			3	/* Group Membership Response */
#define DOT11_RAV_MSCS_REQ			4	/* MSCS Request */
#define DOT11_RAV_MSCS_RES			5	/* MSCS Response */

/* Wireless Network Management (WNM) action types */
#define DOT11_WNM_ACTION_EVENT_REQ		0
#define DOT11_WNM_ACTION_EVENT_REP		1
#define DOT11_WNM_ACTION_DIAG_REQ		2
#define DOT11_WNM_ACTION_DIAG_REP		3
#define DOT11_WNM_ACTION_LOC_CFG_REQ		4
#define DOT11_WNM_ACTION_LOC_RFG_RESP		5
#define DOT11_WNM_ACTION_BSSTRANS_QUERY		6
#define DOT11_WNM_ACTION_BSSTRANS_REQ		7
#define DOT11_WNM_ACTION_BSSTRANS_RESP		8
#define DOT11_WNM_ACTION_FMS_REQ		9
#define DOT11_WNM_ACTION_FMS_RESP		10
#define DOT11_WNM_ACTION_COL_INTRFRNCE_REQ	11
#define DOT11_WNM_ACTION_COL_INTRFRNCE_REP	12
#define DOT11_WNM_ACTION_TFS_REQ		13
#define DOT11_WNM_ACTION_TFS_RESP		14
#define DOT11_WNM_ACTION_TFS_NOTIFY_REQ		15
#define DOT11_WNM_ACTION_WNM_SLEEP_REQ		16
#define DOT11_WNM_ACTION_WNM_SLEEP_RESP		17
#define DOT11_WNM_ACTION_TIMBC_REQ		18
#define DOT11_WNM_ACTION_TIMBC_RESP		19
#define DOT11_WNM_ACTION_QOS_TRFC_CAP_UPD	20
#define DOT11_WNM_ACTION_CHAN_USAGE_REQ		21
#define DOT11_WNM_ACTION_CHAN_USAGE_RESP	22
#define DOT11_WNM_ACTION_DMS_REQ		23
#define DOT11_WNM_ACTION_DMS_RESP		24
#define DOT11_WNM_ACTION_TMNG_MEASUR_REQ	25
#define DOT11_WNM_ACTION_NOTFCTN_REQ		26
#define DOT11_WNM_ACTION_NOTFCTN_RESP		27
#define DOT11_WNM_ACTION_TFS_NOTIFY_RESP	28

/* Unprotected Wireless Network Management (WNM) action types */
#define DOT11_UWNM_ACTION_TIM			0
#define DOT11_UWNM_ACTION_TIMING_MEASUREMENT	1

#define DOT11_MNG_COUNTRY_ID_LEN 3

/* VHT category action types - 802.11ac D3.0 - 8.5.23.1 */
#define DOT11_VHT_ACTION_CBF				0	/* Compressed Beamforming */
#define DOT11_VHT_ACTION_GID_MGMT			1	/* Group ID Management */
#define DOT11_VHT_ACTION_OPER_MODE_NOTIF	2	/* Operating mode notif'n */

/* FILS category action types - 802.11ai D11.0 - 9.6.8.1 */
#define DOT11_FILS_ACTION_DISCOVERY		34	/* FILS Discovery */

/** DLS Request frame header */
BWL_PRE_PACKED_STRUCT struct dot11_dls_req {
	uint8 category;			/* category of action frame (2) */
	uint8 action;				/* DLS action: req (0) */
	struct ether_addr	da;		/* destination address */
	struct ether_addr	sa;		/* source address */
	uint16 cap;				/* capability */
	uint16 timeout;			/* timeout value */
	uint8 data[1];				/* IE:support rate, extend support rate, HT cap */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_dls_req dot11_dls_req_t;
#define DOT11_DLS_REQ_LEN 18	/* Fixed length */

/** DLS response frame header */
BWL_PRE_PACKED_STRUCT struct dot11_dls_resp {
	uint8 category;			/* category of action frame (2) */
	uint8 action;				/* DLS action: req (0) */
	uint16 status;				/* status code field */
	struct ether_addr	da;		/* destination address */
	struct ether_addr	sa;		/* source address */
	uint8 data[1];				/* optional: capability, rate ... */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_dls_resp dot11_dls_resp_t;
#define DOT11_DLS_RESP_LEN 16	/* Fixed length */

/* ************* 802.11v related definitions. ************* */

/** BSS Management Transition Query frame header */
BWL_PRE_PACKED_STRUCT struct dot11_bsstrans_query {
	uint8 category;			/* category of action frame (10) */
	uint8 action;			/* WNM action: trans_query (6) */
	uint8 token;			/* dialog token */
	uint8 reason;			/* transition query reason */
	uint8 data[1];			/* Elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_bsstrans_query dot11_bsstrans_query_t;
#define DOT11_BSSTRANS_QUERY_LEN 4	/* Fixed length */

/* BTM transition reason */
#define DOT11_BSSTRANS_REASON_UNSPECIFIED		0
#define DOT11_BSSTRANS_REASON_EXC_FRAME_LOSS		1
#define DOT11_BSSTRANS_REASON_EXC_TRAFFIC_DELAY		2
#define DOT11_BSSTRANS_REASON_INSUFF_QOS_CAPACITY	3
#define DOT11_BSSTRANS_REASON_FIRST_ASSOC		4
#define DOT11_BSSTRANS_REASON_LOAD_BALANCING		5
#define DOT11_BSSTRANS_REASON_BETTER_AP_FOUND		6
#define DOT11_BSSTRANS_REASON_DEAUTH_RX			7
#define DOT11_BSSTRANS_REASON_8021X_EAP_AUTH_FAIL	8
#define DOT11_BSSTRANS_REASON_4WAY_HANDSHK_FAIL		9
#define DOT11_BSSTRANS_REASON_MANY_REPLAYCNT_FAIL	10
#define DOT11_BSSTRANS_REASON_MANY_DATAMIC_FAIL		11
#define DOT11_BSSTRANS_REASON_EXCEED_MAX_RETRANS	12
#define DOT11_BSSTRANS_REASON_MANY_BCAST_DISASSOC_RX	13
#define DOT11_BSSTRANS_REASON_MANY_BCAST_DEAUTH_RX	14
#define DOT11_BSSTRANS_REASON_PREV_TRANSITION_FAIL	15
#define DOT11_BSSTRANS_REASON_LOW_RSSI			16
#define DOT11_BSSTRANS_REASON_ROAM_FROM_NON_80211	17
#define DOT11_BSSTRANS_REASON_RX_BTM_REQ		18
#define DOT11_BSSTRANS_REASON_PREF_LIST_INCLUDED	19
#define DOT11_BSSTRANS_REASON_LEAVING_ESS		20

/** BSS Management Transition Request frame header */
BWL_PRE_PACKED_STRUCT struct dot11_bsstrans_req {
	uint8 category;			/* category of action frame (10) */
	uint8 action;			/* WNM action: trans_req (7) */
	uint8 token;			/* dialog token */
	uint8 reqmode;			/* transition request mode */
	uint16 disassoc_tmr;		/* disassociation timer */
	uint8 validity_intrvl;		/* validity interval */
	uint8 data[1];			/* optional: BSS term duration, ... */
						/* ...session info URL, candidate list */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_bsstrans_req dot11_bsstrans_req_t;
#define DOT11_BSSTRANS_REQ_LEN 7	/* Fixed length */
#define DOT11_BSSTRANS_REQ_FIXED_LEN 7u	/* Fixed length */

/* BSS Mgmt Transition Request Mode Field - 802.11v */
#define DOT11_BSSTRANS_REQMODE_PREF_LIST_INCL		0x01
#define DOT11_BSSTRANS_REQMODE_ABRIDGED			0x02
#define DOT11_BSSTRANS_REQMODE_DISASSOC_IMMINENT	0x04
#define DOT11_BSSTRANS_REQMODE_BSS_TERM_INCL		0x08
#define DOT11_BSSTRANS_REQMODE_ESS_DISASSOC_IMNT	0x10

/** BSS Management transition response frame header */
BWL_PRE_PACKED_STRUCT struct dot11_bsstrans_resp {
	uint8 category;			/* category of action frame (10) */
	uint8 action;			/* WNM action: trans_resp (8) */
	uint8 token;			/* dialog token */
	uint8 status;			/* transition status */
	uint8 term_delay;		/* validity interval */
	uint8 data[1];			/* optional: BSSID target, candidate list */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_bsstrans_resp dot11_bsstrans_resp_t;
#define DOT11_BSSTRANS_RESP_LEN 5	/* Fixed length */

/* BSS Mgmt Transition Response Status Field */
#define DOT11_BSSTRANS_RESP_STATUS_ACCEPT			0
#define DOT11_BSSTRANS_RESP_STATUS_REJECT			1
#define DOT11_BSSTRANS_RESP_STATUS_REJ_INSUFF_BCN		2
#define DOT11_BSSTRANS_RESP_STATUS_REJ_INSUFF_CAP		3
#define DOT11_BSSTRANS_RESP_STATUS_REJ_TERM_UNDESIRED		4
#define DOT11_BSSTRANS_RESP_STATUS_REJ_TERM_DELAY_REQ		5
#define DOT11_BSSTRANS_RESP_STATUS_REJ_BSS_LIST_PROVIDED	6
#define DOT11_BSSTRANS_RESP_STATUS_REJ_NO_SUITABLE_BSS		7
#define DOT11_BSSTRANS_RESP_STATUS_REJ_LEAVING_ESS		8

/** BSS Max Idle Period element */
BWL_PRE_PACKED_STRUCT struct dot11_bss_max_idle_period_ie {
	uint8 id;				/* 90, DOT11_MNG_BSS_MAX_IDLE_PERIOD_ID */
	uint8 len;
	uint16 max_idle_period;			/* in unit of 1000 TUs */
	uint8 idle_opt;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_bss_max_idle_period_ie dot11_bss_max_idle_period_ie_t;
#define DOT11_BSS_MAX_IDLE_PERIOD_IE_LEN	3	/* bss max idle period IE size */
#define DOT11_BSS_MAX_IDLE_PERIOD_OPT_PROTECTED	1	/* BSS max idle option */

/** TIM Broadcast request element */
BWL_PRE_PACKED_STRUCT struct dot11_timbc_req_ie {
	uint8 id;				/* 94, DOT11_MNG_TIMBC_REQ_ID */
	uint8 len;
	uint8 interval;				/* in unit of beacon interval */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_timbc_req_ie dot11_timbc_req_ie_t;
#define DOT11_TIMBC_REQ_IE_LEN		1	/* Fixed length */

/** TIM Broadcast request frame header */
BWL_PRE_PACKED_STRUCT struct dot11_timbc_req {
	uint8 category;				/* category of action frame (10) */
	uint8 action;				/* WNM action: DOT11_WNM_ACTION_TIMBC_REQ(18) */
	uint8 token;				/* dialog token */
	uint8 data[1];				/* TIM broadcast request element */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_timbc_req dot11_timbc_req_t;
#define DOT11_TIMBC_REQ_LEN		3	/* Fixed length */

/** TIM Broadcast response element */
BWL_PRE_PACKED_STRUCT struct dot11_timbc_resp_ie {
	uint8 id;				/* 95, DOT11_MNG_TIM_BROADCAST_RESP_ID */
	uint8 len;
	uint8 status;				/* status of add request */
	uint8 interval;				/* in unit of beacon interval */
	int32 offset;				/* in unit of ms */
	uint16 high_rate;			/* in unit of 0.5 Mb/s */
	uint16 low_rate;			/* in unit of 0.5 Mb/s */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_timbc_resp_ie dot11_timbc_resp_ie_t;
#define DOT11_TIMBC_DENY_RESP_IE_LEN	1	/* Deny. Fixed length */
#define DOT11_TIMBC_ACCEPT_RESP_IE_LEN	10	/* Accept. Fixed length */

#define DOT11_TIMBC_STATUS_ACCEPT		0
#define DOT11_TIMBC_STATUS_ACCEPT_TSTAMP	1
#define DOT11_TIMBC_STATUS_DENY			2
#define DOT11_TIMBC_STATUS_OVERRIDDEN		3
#define DOT11_TIMBC_STATUS_RESERVED		4

/** TIM Broadcast request frame header */
BWL_PRE_PACKED_STRUCT struct dot11_timbc_resp {
	uint8 category;			/* category of action frame (10) */
	uint8 action;			/* action: DOT11_WNM_ACTION_TIMBC_RESP(19) */
	uint8 token;			/* dialog token */
	uint8 data[1];			/* TIM broadcast response element */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_timbc_resp dot11_timbc_resp_t;
#define DOT11_TIMBC_RESP_LEN	3	/* Fixed length */

/** TIM element */
BWL_PRE_PACKED_STRUCT struct dot11_tim_ie {
	uint8 id;			/* 5, DOT11_MNG_TIM_ID	 */
	uint8 len;			/* 4 - 255 */
	uint8 dtim_count;		/* DTIM decrementing counter */
	uint8 dtim_period;		/* DTIM period */
	uint8 bitmap_control;		/* AID 0 + bitmap offset */
	uint8 pvb[1];			/* Partial Virtual Bitmap, variable length */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tim_ie dot11_tim_ie_t;
#define DOT11_TIM_IE_FIXED_LEN	3	/* Fixed length, without id and len */
#define DOT11_TIM_IE_FIXED_TOTAL_LEN	5	/* Fixed length, with id and len */

/** TIM Broadcast frame header */
BWL_PRE_PACKED_STRUCT struct dot11_timbc {
	uint8 category;			/* category of action frame (11) */
	uint8 action;			/* action: TIM (0) */
	uint8 check_beacon;		/* need to check-beacon */
	uint8 tsf[8];			/* Time Synchronization Function */
	dot11_tim_ie_t tim_ie;		/* TIM element */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_timbc dot11_timbc_t;
#define DOT11_TIMBC_HDR_LEN	(sizeof(dot11_timbc_t) - sizeof(dot11_tim_ie_t))
#define DOT11_TIMBC_FIXED_LEN	(sizeof(dot11_timbc_t) - 1)	/* Fixed length */
#define DOT11_TIMBC_LEN			11	/* Fixed length */

/** TCLAS frame classifier type */
BWL_PRE_PACKED_STRUCT struct dot11_tclas_fc_hdr {
	uint8 type;
	uint8 mask;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tclas_fc_hdr dot11_tclas_fc_hdr_t;
#define DOT11_TCLAS_FC_HDR_LEN		2	/* Fixed length */

#define DOT11_TCLAS_MASK_0		0x1
#define DOT11_TCLAS_MASK_1		0x2
#define DOT11_TCLAS_MASK_2		0x4
#define DOT11_TCLAS_MASK_3		0x8
#define DOT11_TCLAS_MASK_4		0x10
#define DOT11_TCLAS_MASK_5		0x20
#define DOT11_TCLAS_MASK_6		0x40
#define DOT11_TCLAS_MASK_7		0x80

#define DOT11_TCLAS_FC_0_ETH		0
#define DOT11_TCLAS_FC_1_IP		1
#define DOT11_TCLAS_FC_2_8021Q		2
#define DOT11_TCLAS_FC_3_OFFSET		3
#define DOT11_TCLAS_FC_4_IP_HIGHER	4
#define DOT11_TCLAS_FC_5_8021D		5

/** TCLAS frame classifier type 0 parameters for Ethernet */
BWL_PRE_PACKED_STRUCT struct dot11_tclas_fc_0_eth {
	uint8 type;
	uint8 mask;
	uint8 sa[ETHER_ADDR_LEN];
	uint8 da[ETHER_ADDR_LEN];
	uint16 eth_type;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tclas_fc_0_eth dot11_tclas_fc_0_eth_t;
#define DOT11_TCLAS_FC_0_ETH_LEN	16

/** TCLAS frame classifier type 1 parameters for IPV4 */
BWL_PRE_PACKED_STRUCT struct dot11_tclas_fc_1_ipv4 {
	uint8 type;
	uint8 mask;
	uint8 version;
	uint32 src_ip;
	uint32 dst_ip;
	uint16 src_port;
	uint16 dst_port;
	uint8 dscp;
	uint8 protocol;
	uint8 reserved;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tclas_fc_1_ipv4 dot11_tclas_fc_1_ipv4_t;
#define DOT11_TCLAS_FC_1_IPV4_LEN	18

/** TCLAS frame classifier type 2 parameters for 802.1Q */
BWL_PRE_PACKED_STRUCT struct dot11_tclas_fc_2_8021q {
	uint8 type;
	uint8 mask;
	uint16 tci;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tclas_fc_2_8021q dot11_tclas_fc_2_8021q_t;
#define DOT11_TCLAS_FC_2_8021Q_LEN	4

/** TCLAS frame classifier type 3 parameters for filter offset */
BWL_PRE_PACKED_STRUCT struct dot11_tclas_fc_3_filter {
	uint8 type;
	uint8 mask;
	uint16 offset;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tclas_fc_3_filter dot11_tclas_fc_3_filter_t;
#define DOT11_TCLAS_FC_3_FILTER_LEN	4

/** TCLAS frame classifier type 4 parameters for IPV4 is the same as TCLAS type 1 */
typedef struct dot11_tclas_fc_1_ipv4 dot11_tclas_fc_4_ipv4_t;
#define DOT11_TCLAS_FC_4_IPV4_LEN	DOT11_TCLAS_FC_1_IPV4_LEN

/** TCLAS frame classifier type 4 parameters for IPV6 */
BWL_PRE_PACKED_STRUCT struct dot11_tclas_fc_4_ipv6 {
	uint8 type;
	uint8 mask;
	uint8 version;
	uint8 saddr[16];
	uint8 daddr[16];
	uint16 src_port;
	uint16 dst_port;
	uint8 dscp;
	uint8 nexthdr;
	uint8 flow_lbl[3];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tclas_fc_4_ipv6 dot11_tclas_fc_4_ipv6_t;
#define DOT11_TCLAS_FC_4_IPV6_LEN	44

/** TCLAS frame classifier type 5 parameters for 802.1D */
BWL_PRE_PACKED_STRUCT struct dot11_tclas_fc_5_8021d {
	uint8 type;
	uint8 mask;
	uint8 pcp;
	uint8 cfi;
	uint16 vid;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tclas_fc_5_8021d dot11_tclas_fc_5_8021d_t;
#define DOT11_TCLAS_FC_5_8021D_LEN	6

/** TCLAS frame classifier type parameters */
BWL_PRE_PACKED_STRUCT union dot11_tclas_fc {
	uint8 data[1];
	dot11_tclas_fc_hdr_t hdr;
	dot11_tclas_fc_0_eth_t t0_eth;
	dot11_tclas_fc_1_ipv4_t	t1_ipv4;
	dot11_tclas_fc_2_8021q_t t2_8021q;
	dot11_tclas_fc_3_filter_t t3_filter;
	dot11_tclas_fc_4_ipv4_t	t4_ipv4;
	dot11_tclas_fc_4_ipv6_t	t4_ipv6;
	dot11_tclas_fc_5_8021d_t t5_8021d;
} BWL_POST_PACKED_STRUCT;
typedef union dot11_tclas_fc dot11_tclas_fc_t;

#define DOT11_TCLAS_FC_MIN_LEN		4	/* Classifier Type 2 has the min size */
#define DOT11_TCLAS_FC_MAX_LEN		254

/** TCLAS element */
BWL_PRE_PACKED_STRUCT struct dot11_tclas_ie {
	uint8 id;				/* 14, DOT11_MNG_TCLAS_ID */
	uint8 len;
	uint8 user_priority;
	dot11_tclas_fc_t fc;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tclas_ie dot11_tclas_ie_t;
#define DOT11_TCLAS_IE_LEN		3u	/* Fixed length, include id and len */

/** TCLAS processing element */
BWL_PRE_PACKED_STRUCT struct dot11_tclas_proc_ie {
	uint8 id;				/* 44, DOT11_MNG_TCLAS_PROC_ID */
	uint8 len;
	uint8 process;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tclas_proc_ie dot11_tclas_proc_ie_t;
#define DOT11_TCLAS_PROC_IE_LEN		3	/* Fixed length, include id and len */

#define DOT11_TCLAS_PROC_LEN		1u	/* Proc ie length is always 1 byte */

#define DOT11_TCLAS_PROC_MATCHALL	0	/* All high level element need to match */
#define DOT11_TCLAS_PROC_MATCHONE	1	/* One high level element need to match */
#define DOT11_TCLAS_PROC_NONMATCH	2	/* Non match to any high level element */

/* TSPEC element defined in 802.11 std section 8.4.2.32 - Not supported */
#define DOT11_TSPEC_IE_LEN		57	/* Fixed length */

/** TCLAS Mask element */
BWL_PRE_PACKED_STRUCT struct dot11_tclas_mask_ie {
	uint8 id;				/* DOT11_MNG_ID_EXT_ID (255) */
	uint8 len;
	uint8 id_ext;				/* TCLAS_EXTID_MNG_MASK_ID (89) */
	dot11_tclas_fc_t fc;			/* Variable length frame classifier (fc) */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tclas_mask_ie dot11_tclas_mask_ie_t;
#define DOT11_TCLAS_MASK_IE_LEN		1u	/* Fixed length, excludes id and len */
#define DOT11_TCLAS_MASK_IE_HDR_LEN	3u	/* Fixed length */

/* Bitmap definitions for the User Priority Bitmap
 * Each bit in the bitmap corresponds to a user priority.
 */
#define DOT11_UP_CTRL_UP_0		0u
#define DOT11_UP_CTRL_UP_1		1u
#define DOT11_UP_CTRL_UP_2		2u
#define DOT11_UP_CTRL_UP_3		3u
#define DOT11_UP_CTRL_UP_4		4u
#define DOT11_UP_CTRL_UP_5		5u
#define DOT11_UP_CTRL_UP_6		6u
#define DOT11_UP_CTRL_UP_7		7u

/* User priority control (up_ctl)  macros */
#define DOT11_UPC_UP_BITMAP_MASK	0xFFu	/* UP bitmap mask */
#define DOT11_UPC_UP_BITMAP_SHIFT	0u	/* UP bitmap shift */
#define DOT11_UPC_UP_LIMIT_MASK		0x700u	/* UP limit mask */
#define DOT11_UPC_UP_LIMIT_SHIFT	8u	/* UP limit shift */

/* MSCS Request Types */
#define DOT11_MSCS_REQ_TYPE_ADD		0u
#define DOT11_MSCS_REQ_TYPE_REMOVE	1u
#define DOT11_MSCS_REQ_TYPE_CHANGE	2u

/** MSCS Descriptor element */
BWL_PRE_PACKED_STRUCT struct dot11_mscs_descr_ie {
	uint8  id;				/* DOT11_MNG_ID_EXT_ID (255) */
	uint8  len;
	uint8  id_ext;				/* MSCS_EXTID_MNG_DESCR_ID (88) */
	uint8  req_type;			/* MSCS request type */
	uint16 up_ctl;				/* User priority control:
						 * Bits 0..7, up_bitmap(8 bits);
						 * Bits 8..10, up_limit (3 bits)
						 * Bits 11..15 reserved (5 bits)
						 */
	uint32 stream_timeout;
	uint8  data[];
	/* optional tclas mask elements */	/* dot11_tclas_mask_ie_t */
	/* optional sub-elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_mscs_descr_ie dot11_mscs_descr_ie_t;
#define DOT11_MSCS_DESCR_IE_LEN		8u	/* Fixed length, exludes id and len */
#define DOT11_MSCS_DESCR_IE_HDR_LEN	10u	/* Entire descriptor header length */

/** MSCS Request frame, refer section 9.4.18.6 in the spec P802.11REVmd_D3.1 */
BWL_PRE_PACKED_STRUCT struct dot11_mscs_req {
	uint8 category;				/* ACTION_RAV_STREAMING (19) */
	uint8 robust_action;			/* action: MSCS Req (4), MSCS Res (5), etc. */
	uint8 dialog_token;			/* To identify the MSCS request and response */
	dot11_mscs_descr_ie_t mscs_descr;	/* MSCS descriptor */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_mscs_req dot11_mscs_req_t;
#define DOT11_MSCS_REQ_HDR_LEN		3u	/* Fixed length */

/** MSCS Response frame, refer section 9.4.18.7 in the spec P802.11REVmd_D3.1 */
BWL_PRE_PACKED_STRUCT struct dot11_mscs_res {
	uint8  category;			/* ACTION_RAV_STREAMING (19) */
	uint8  robust_action;			/* action: MSCS Req (4), MSCS Res (5), etc. */
	uint8  dialog_token;			/* To identify the MSCS request and response */
	uint16 status;				/* status code */
	uint8  data[];				/* optional MSCS descriptor */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_mscs_res dot11_mscs_res_t;
#define DOT11_MSCS_RES_HDR_LEN		5u	/* Fixed length */

/* MSCS subelement */
#define DOT11_MSCS_SUBELEM_ID_STATUS	1u	/* MSCS subelement ID for the status */

BWL_PRE_PACKED_STRUCT struct dot11_mscs_subelement {
	uint8 id;				/* MSCS specific subelement ID */
	uint8 len;				/* Length in bytes */
	uint8 data[];				/* Subelement specific data */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_mscs_subelement dot11_mscs_subelement_t;
#define DOT11_MSCS_DESCR_SUBELEM_IE_STATUS_LEN	2u	/* Subelement ID status length */

/** TFS request element */
BWL_PRE_PACKED_STRUCT struct dot11_tfs_req_ie {
	uint8 id;				/* 91, DOT11_MNG_TFS_REQUEST_ID */
	uint8 len;
	uint8 tfs_id;
	uint8 actcode;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tfs_req_ie dot11_tfs_req_ie_t;
#define DOT11_TFS_REQ_IE_LEN		2	/* Fixed length, without id and len */

/** TFS request action codes (bitfield) */
#define DOT11_TFS_ACTCODE_DELETE	1
#define DOT11_TFS_ACTCODE_NOTIFY	2

/** TFS request subelement IDs */
#define DOT11_TFS_REQ_TFS_SE_ID		1
#define DOT11_TFS_REQ_VENDOR_SE_ID	221

/** TFS subelement */
BWL_PRE_PACKED_STRUCT struct dot11_tfs_se {
	uint8 sub_id;
	uint8 len;
	uint8 data[1];				/* TCLAS element(s) + optional TCLAS proc */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tfs_se dot11_tfs_se_t;

/** TFS response element */
BWL_PRE_PACKED_STRUCT struct dot11_tfs_resp_ie {
	uint8 id;				/* 92, DOT11_MNG_TFS_RESPONSE_ID */
	uint8 len;
	uint8 tfs_id;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tfs_resp_ie dot11_tfs_resp_ie_t;
#define DOT11_TFS_RESP_IE_LEN		1u	/* Fixed length, without id and len */

/** TFS response subelement IDs (same subelments, but different IDs than in TFS request */
#define DOT11_TFS_RESP_TFS_STATUS_SE_ID		1
#define DOT11_TFS_RESP_TFS_SE_ID		2
#define DOT11_TFS_RESP_VENDOR_SE_ID		221

/** TFS status subelement */
BWL_PRE_PACKED_STRUCT struct dot11_tfs_status_se {
	uint8 sub_id;				/* 92, DOT11_MNG_TFS_RESPONSE_ID */
	uint8 len;
	uint8 resp_st;
	uint8 data[1];				/* Potential dot11_tfs_se_t included */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tfs_status_se dot11_tfs_status_se_t;
#define DOT11_TFS_STATUS_SE_LEN			1	/* Fixed length, without id and len */

/* Following Definition should be merged to FMS_TFS macro below */
/* TFS Response status code. Identical to FMS Element status, without N/A  */
#define DOT11_TFS_STATUS_ACCEPT			0
#define DOT11_TFS_STATUS_DENY_FORMAT		1
#define DOT11_TFS_STATUS_DENY_RESOURCE		2
#define DOT11_TFS_STATUS_DENY_POLICY		4
#define DOT11_TFS_STATUS_DENY_UNSPECIFIED	5
#define DOT11_TFS_STATUS_ALTPREF_POLICY		7
#define DOT11_TFS_STATUS_ALTPREF_TCLAS_UNSUPP	14

/* FMS Element Status and TFS Response Status Definition */
#define DOT11_FMS_TFS_STATUS_ACCEPT		0
#define DOT11_FMS_TFS_STATUS_DENY_FORMAT	1
#define DOT11_FMS_TFS_STATUS_DENY_RESOURCE	2
#define DOT11_FMS_TFS_STATUS_DENY_MULTIPLE_DI	3
#define DOT11_FMS_TFS_STATUS_DENY_POLICY	4
#define DOT11_FMS_TFS_STATUS_DENY_UNSPECIFIED	5
#define DOT11_FMS_TFS_STATUS_ALT_DIFF_DI	6
#define DOT11_FMS_TFS_STATUS_ALT_POLICY		7
#define DOT11_FMS_TFS_STATUS_ALT_CHANGE_DI	8
#define DOT11_FMS_TFS_STATUS_ALT_MCRATE		9
#define DOT11_FMS_TFS_STATUS_TERM_POLICY	10
#define DOT11_FMS_TFS_STATUS_TERM_RESOURCE	11
#define DOT11_FMS_TFS_STATUS_TERM_HIGHER_PRIO	12
#define DOT11_FMS_TFS_STATUS_ALT_CHANGE_MDI	13
#define DOT11_FMS_TFS_STATUS_ALT_TCLAS_UNSUPP	14

/** TFS Management Request frame header */
BWL_PRE_PACKED_STRUCT struct dot11_tfs_req {
	uint8 category;				/* category of action frame (10) */
	uint8 action;				/* WNM action: TFS request (13) */
	uint8 token;				/* dialog token */
	uint8 data[1];				/* Elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tfs_req dot11_tfs_req_t;
#define DOT11_TFS_REQ_LEN		3	/* Fixed length */

/** TFS Management Response frame header */
BWL_PRE_PACKED_STRUCT struct dot11_tfs_resp {
	uint8 category;				/* category of action frame (10) */
	uint8 action;				/* WNM action: TFS request (14) */
	uint8 token;				/* dialog token */
	uint8 data[1];				/* Elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tfs_resp dot11_tfs_resp_t;
#define DOT11_TFS_RESP_LEN		3	/* Fixed length */

/** TFS Management Notify frame request header */
BWL_PRE_PACKED_STRUCT struct dot11_tfs_notify_req {
	uint8 category;				/* category of action frame (10) */
	uint8 action;				/* WNM action: TFS notify request (15) */
	uint8 tfs_id_cnt;			/* TFS IDs count */
	uint8 tfs_id[1];			/* Array of TFS IDs */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tfs_notify_req dot11_tfs_notify_req_t;
#define DOT11_TFS_NOTIFY_REQ_LEN	3	/* Fixed length */

/** TFS Management Notify frame response header */
BWL_PRE_PACKED_STRUCT struct dot11_tfs_notify_resp {
	uint8 category;				/* category of action frame (10) */
	uint8 action;				/* WNM action: TFS notify response (28) */
	uint8 tfs_id_cnt;			/* TFS IDs count */
	uint8 tfs_id[1];			/* Array of TFS IDs */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tfs_notify_resp dot11_tfs_notify_resp_t;
#define DOT11_TFS_NOTIFY_RESP_LEN	3	/* Fixed length */

/** WNM-Sleep Management Request frame header */
BWL_PRE_PACKED_STRUCT struct dot11_wnm_sleep_req {
	uint8 category;				/* category of action frame (10) */
	uint8 action;				/* WNM action: wnm-sleep request (16) */
	uint8 token;				/* dialog token */
	uint8 data[1];				/* Elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_wnm_sleep_req dot11_wnm_sleep_req_t;
#define DOT11_WNM_SLEEP_REQ_LEN		3	/* Fixed length */

/** WNM-Sleep Management Response frame header */
BWL_PRE_PACKED_STRUCT struct dot11_wnm_sleep_resp {
	uint8 category;				/* category of action frame (10) */
	uint8 action;				/* WNM action: wnm-sleep request (17) */
	uint8 token;				/* dialog token */
	uint16 key_len;				/* key data length */
	uint8 data[1];				/* Elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_wnm_sleep_resp dot11_wnm_sleep_resp_t;
#define DOT11_WNM_SLEEP_RESP_LEN	5	/* Fixed length */

#define DOT11_WNM_SLEEP_SUBELEM_ID_GTK	0
#define DOT11_WNM_SLEEP_SUBELEM_ID_IGTK	1

BWL_PRE_PACKED_STRUCT struct dot11_wnm_sleep_subelem_gtk {
	uint8 sub_id;
	uint8 len;
	uint16 key_info;
	uint8 key_length;
	uint8 rsc[8];
	uint8 key[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_wnm_sleep_subelem_gtk dot11_wnm_sleep_subelem_gtk_t;
#define DOT11_WNM_SLEEP_SUBELEM_GTK_FIXED_LEN	11	/* without sub_id, len, and key */
#define DOT11_WNM_SLEEP_SUBELEM_GTK_MAX_LEN	43	/* without sub_id and len */

BWL_PRE_PACKED_STRUCT struct dot11_wnm_sleep_subelem_igtk {
	uint8 sub_id;
	uint8 len;
	uint16 key_id;
	uint8 pn[6];
	uint8 key[16];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_wnm_sleep_subelem_igtk dot11_wnm_sleep_subelem_igtk_t;
#define DOT11_WNM_SLEEP_SUBELEM_IGTK_LEN 24	/* Fixed length */

BWL_PRE_PACKED_STRUCT struct dot11_wnm_sleep_ie {
	uint8 id;				/* 93, DOT11_MNG_WNM_SLEEP_MODE_ID */
	uint8 len;
	uint8 act_type;
	uint8 resp_status;
	uint16 interval;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_wnm_sleep_ie dot11_wnm_sleep_ie_t;
#define DOT11_WNM_SLEEP_IE_LEN		4	/* Fixed length */

#define DOT11_WNM_SLEEP_ACT_TYPE_ENTER	0
#define DOT11_WNM_SLEEP_ACT_TYPE_EXIT	1

#define DOT11_WNM_SLEEP_RESP_ACCEPT	0
#define DOT11_WNM_SLEEP_RESP_UPDATE	1
#define DOT11_WNM_SLEEP_RESP_DENY	2
#define DOT11_WNM_SLEEP_RESP_DENY_TEMP	3
#define DOT11_WNM_SLEEP_RESP_DENY_KEY	4
#define DOT11_WNM_SLEEP_RESP_DENY_INUSE	5
#define DOT11_WNM_SLEEP_RESP_LAST	6

/** DMS Management Request frame header */
BWL_PRE_PACKED_STRUCT struct dot11_dms_req {
	uint8 category;				/* category of action frame (10) */
	uint8 action;				/* WNM action: dms request (23) */
	uint8 token;				/* dialog token */
	uint8 data[1];				/* Elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_dms_req dot11_dms_req_t;
#define DOT11_DMS_REQ_LEN		3	/* Fixed length */

/** DMS Management Response frame header */
BWL_PRE_PACKED_STRUCT struct dot11_dms_resp {
	uint8 category;				/* category of action frame (10) */
	uint8 action;				/* WNM action: dms request (24) */
	uint8 token;				/* dialog token */
	uint8 data[1];				/* Elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_dms_resp dot11_dms_resp_t;
#define DOT11_DMS_RESP_LEN		3	/* Fixed length */

/** DMS request element */
BWL_PRE_PACKED_STRUCT struct dot11_dms_req_ie {
	uint8 id;				/* 99, DOT11_MNG_DMS_REQUEST_ID */
	uint8 len;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_dms_req_ie dot11_dms_req_ie_t;
#define DOT11_DMS_REQ_IE_LEN		2	/* Fixed length */

/** DMS response element */
BWL_PRE_PACKED_STRUCT struct dot11_dms_resp_ie {
	uint8 id;				/* 100, DOT11_MNG_DMS_RESPONSE_ID */
	uint8 len;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_dms_resp_ie dot11_dms_resp_ie_t;
#define DOT11_DMS_RESP_IE_LEN		2	/* Fixed length */

/** DMS request descriptor */
BWL_PRE_PACKED_STRUCT struct dot11_dms_req_desc {
	uint8 dms_id;
	uint8 len;
	uint8 type;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_dms_req_desc dot11_dms_req_desc_t;
#define DOT11_DMS_REQ_DESC_LEN		3	/* Fixed length */

#define DOT11_DMS_REQ_TYPE_ADD		0
#define DOT11_DMS_REQ_TYPE_REMOVE	1
#define DOT11_DMS_REQ_TYPE_CHANGE	2

/** DMS response status */
BWL_PRE_PACKED_STRUCT struct dot11_dms_resp_st {
	uint8 dms_id;
	uint8 len;
	uint8 type;
	uint16 lsc;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_dms_resp_st dot11_dms_resp_st_t;
#define DOT11_DMS_RESP_STATUS_LEN	5	/* Fixed length */

#define DOT11_DMS_RESP_TYPE_ACCEPT	0
#define DOT11_DMS_RESP_TYPE_DENY	1
#define DOT11_DMS_RESP_TYPE_TERM	2

#define DOT11_DMS_RESP_LSC_UNSUPPORTED	0xFFFF

/** WNM-Notification Request frame header */
BWL_PRE_PACKED_STRUCT struct dot11_wnm_notif_req {
	uint8 category;				/* category of action frame (10) */
	uint8 action;				/* WNM action: Notification request (26) */
	uint8 token;				/* dialog token */
	uint8 type;				   /* type */
	uint8 data[1];				/* Sub-elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_wnm_notif_req dot11_wnm_notif_req_t;
#define DOT11_WNM_NOTIF_REQ_LEN		4	/* Fixed length */

/** FMS Management Request frame header */
BWL_PRE_PACKED_STRUCT struct dot11_fms_req {
	uint8 category;				/* category of action frame (10) */
	uint8 action;				/* WNM action: fms request (9) */
	uint8 token;				/* dialog token */
	uint8 data[1];				/* Elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_fms_req dot11_fms_req_t;
#define DOT11_FMS_REQ_LEN		3	/* Fixed length */

/** FMS Management Response frame header */
BWL_PRE_PACKED_STRUCT struct dot11_fms_resp {
	uint8 category;				/* category of action frame (10) */
	uint8 action;				/* WNM action: fms request (10) */
	uint8 token;				/* dialog token */
	uint8 data[1];				/* Elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_fms_resp dot11_fms_resp_t;
#define DOT11_FMS_RESP_LEN		3	/* Fixed length */

/** FMS Descriptor element */
BWL_PRE_PACKED_STRUCT struct dot11_fms_desc {
	uint8 id;
	uint8 len;
	uint8 num_fms_cnt;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_fms_desc dot11_fms_desc_t;
#define DOT11_FMS_DESC_LEN		1	/* Fixed length */

#define DOT11_FMS_CNTR_MAX		0x8
#define DOT11_FMS_CNTR_ID_MASK		0x7
#define DOT11_FMS_CNTR_ID_SHIFT		0x0
#define DOT11_FMS_CNTR_COUNT_MASK	0xf1
#define DOT11_FMS_CNTR_SHIFT		0x3

/** FMS request element */
BWL_PRE_PACKED_STRUCT struct dot11_fms_req_ie {
	uint8 id;
	uint8 len;
	uint8 fms_token;			/* token used to identify fms stream set */
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_fms_req_ie dot11_fms_req_ie_t;
#define DOT11_FMS_REQ_IE_FIX_LEN		1	/* Fixed length */

BWL_PRE_PACKED_STRUCT struct dot11_rate_id_field {
	uint8 mask;
	uint8 mcs_idx;
	uint16 rate;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rate_id_field dot11_rate_id_field_t;
#define DOT11_RATE_ID_FIELD_MCS_SEL_MASK	0x7
#define DOT11_RATE_ID_FIELD_MCS_SEL_OFFSET	0
#define DOT11_RATE_ID_FIELD_RATETYPE_MASK	0x18
#define DOT11_RATE_ID_FIELD_RATETYPE_OFFSET	3
#define DOT11_RATE_ID_FIELD_LEN		sizeof(dot11_rate_id_field_t)

/** FMS request subelements */
BWL_PRE_PACKED_STRUCT struct dot11_fms_se {
	uint8 sub_id;
	uint8 len;
	uint8 interval;
	uint8 max_interval;
	dot11_rate_id_field_t rate;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_fms_se dot11_fms_se_t;
#define DOT11_FMS_REQ_SE_LEN		6	/* Fixed length */

#define DOT11_FMS_REQ_SE_ID_FMS		1	/* FMS subelement */
#define DOT11_FMS_REQ_SE_ID_VS		221	/* Vendor Specific subelement */

/** FMS response element */
BWL_PRE_PACKED_STRUCT struct dot11_fms_resp_ie {
	uint8 id;
	uint8 len;
	uint8 fms_token;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_fms_resp_ie dot11_fms_resp_ie_t;
#define DOT11_FMS_RESP_IE_FIX_LEN		1	/* Fixed length */

/* FMS status subelements */
#define DOT11_FMS_STATUS_SE_ID_FMS	1	/* FMS Status */
#define DOT11_FMS_STATUS_SE_ID_TCLAS	2	/* TCLAS Status */
#define DOT11_FMS_STATUS_SE_ID_VS	221	/* Vendor Specific subelement */

/** FMS status subelement */
BWL_PRE_PACKED_STRUCT struct dot11_fms_status_se {
	uint8 sub_id;
	uint8 len;
	uint8 status;
	uint8 interval;
	uint8 max_interval;
	uint8 fmsid;
	uint8 counter;
	dot11_rate_id_field_t rate;
	uint8 mcast_addr[ETHER_ADDR_LEN];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_fms_status_se dot11_fms_status_se_t;
#define DOT11_FMS_STATUS_SE_LEN		15	/* Fixed length */

/** TCLAS status subelement */
BWL_PRE_PACKED_STRUCT struct dot11_tclas_status_se {
	uint8 sub_id;
	uint8 len;
	uint8 fmsid;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tclas_status_se dot11_tclas_status_se_t;
#define DOT11_TCLAS_STATUS_SE_LEN		1	/* Fixed length */

BWL_PRE_PACKED_STRUCT struct dot11_addba_req {
	uint8 category;				/* category of action frame (3) */
	uint8 action;				/* action: addba req */
	uint8 token;				/* identifier */
	uint16 addba_param_set;		/* parameter set */
	uint16 timeout;				/* timeout in seconds */
	uint16 start_seqnum;		/* starting sequence number */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_addba_req dot11_addba_req_t;
#define DOT11_ADDBA_REQ_LEN		9	/* length of addba req frame */

BWL_PRE_PACKED_STRUCT struct dot11_addba_resp {
	uint8 category;				/* category of action frame (3) */
	uint8 action;				/* action: addba resp */
	uint8 token;				/* identifier */
	uint16 status;				/* status of add request */
	uint16 addba_param_set;			/* negotiated parameter set */
	uint16 timeout;				/* negotiated timeout in seconds */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_addba_resp dot11_addba_resp_t;
#define DOT11_ADDBA_RESP_LEN		9	/* length of addba resp frame */

/* DELBA action parameters */
#define DOT11_DELBA_PARAM_INIT_MASK	0x0800	/* initiator mask */
#define DOT11_DELBA_PARAM_INIT_SHIFT	11	/* initiator shift */
#define DOT11_DELBA_PARAM_TID_MASK	0xf000	/* tid mask */
#define DOT11_DELBA_PARAM_TID_SHIFT	12	/* tid shift */

BWL_PRE_PACKED_STRUCT struct dot11_delba {
	uint8 category;				/* category of action frame (3) */
	uint8 action;				/* action: addba req */
	uint16 delba_param_set;			/* paarmeter set */
	uint16 reason;				/* reason for dellba */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_delba dot11_delba_t;
#define DOT11_DELBA_LEN			6	/* length of delba frame */

/* SA Query action field value */
#define SA_QUERY_REQUEST		0
#define SA_QUERY_RESPONSE		1

/* ************* 802.11r related definitions. ************* */

/** Over-the-DS Fast Transition Request frame header */
BWL_PRE_PACKED_STRUCT struct dot11_ft_req {
	uint8 category;			/* category of action frame (6) */
	uint8 action;			/* action: ft req */
	uint8 sta_addr[ETHER_ADDR_LEN];
	uint8 tgt_ap_addr[ETHER_ADDR_LEN];
	uint8 data[1];			/* Elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ft_req dot11_ft_req_t;
#define DOT11_FT_REQ_FIXED_LEN 14

/** Over-the-DS Fast Transition Response frame header */
BWL_PRE_PACKED_STRUCT struct dot11_ft_res {
	uint8 category;			/* category of action frame (6) */
	uint8 action;			/* action: ft resp */
	uint8 sta_addr[ETHER_ADDR_LEN];
	uint8 tgt_ap_addr[ETHER_ADDR_LEN];
	uint16 status;			/* status code */
	uint8 data[1];			/* Elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ft_res dot11_ft_res_t;
#define DOT11_FT_RES_FIXED_LEN 16

/** RDE RIC Data Element. */
BWL_PRE_PACKED_STRUCT struct dot11_rde_ie {
	uint8 id;			/* 11r, DOT11_MNG_RDE_ID */
	uint8 length;
	uint8 rde_id;			/* RDE identifier. */
	uint8 rd_count;			/* Resource Descriptor Count. */
	uint16 status;			/* Status Code. */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rde_ie dot11_rde_ie_t;

/* 11r - Size of the RDE (RIC Data Element) IE, including TLV header. */
#define DOT11_MNG_RDE_IE_LEN sizeof(dot11_rde_ie_t)

/* ************* 802.11k related definitions. ************* */

/* Radio measurements enabled capability ie */
#define DOT11_RRM_CAP_LEN		5	/* length of rrm cap bitmap */
#define RCPI_IE_LEN 1
#define RSNI_IE_LEN 1
BWL_PRE_PACKED_STRUCT struct dot11_rrm_cap_ie {
	uint8 cap[DOT11_RRM_CAP_LEN];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rrm_cap_ie dot11_rrm_cap_ie_t;

/* Bitmap definitions for cap ie */
#define DOT11_RRM_CAP_LINK		0
#define DOT11_RRM_CAP_NEIGHBOR_REPORT	1
#define DOT11_RRM_CAP_PARALLEL		2
#define DOT11_RRM_CAP_REPEATED		3
#define DOT11_RRM_CAP_BCN_PASSIVE	4
#define DOT11_RRM_CAP_BCN_ACTIVE	5
#define DOT11_RRM_CAP_BCN_TABLE		6
#define DOT11_RRM_CAP_BCN_REP_COND	7
#define DOT11_RRM_CAP_FM		8
#define DOT11_RRM_CAP_CLM		9
#define DOT11_RRM_CAP_NHM		10
#define DOT11_RRM_CAP_SM		11
#define DOT11_RRM_CAP_LCIM		12
#define DOT11_RRM_CAP_LCIA		13
#define DOT11_RRM_CAP_TSCM		14
#define DOT11_RRM_CAP_TTSCM		15
#define DOT11_RRM_CAP_AP_CHANREP	16
#define DOT11_RRM_CAP_RMMIB		17
/* bit18-bit23, not used for RRM_IOVAR */
#define DOT11_RRM_CAP_MPC0		24
#define DOT11_RRM_CAP_MPC1		25
#define DOT11_RRM_CAP_MPC2		26
#define DOT11_RRM_CAP_MPTI		27
#define DOT11_RRM_CAP_NBRTSFO		28
#define DOT11_RRM_CAP_RCPI		29
#define DOT11_RRM_CAP_RSNI		30
#define DOT11_RRM_CAP_BSSAAD		31
#define DOT11_RRM_CAP_BSSAAC		32
#define DOT11_RRM_CAP_AI		33
#define DOT11_RRM_CAP_FTM_RANGE		34
#define DOT11_RRM_CAP_CIVIC_LOC		35
#define DOT11_RRM_CAP_IDENT_LOC		36
#define DOT11_RRM_CAP_LAST		36

#ifdef WL11K_ALL_MEAS
#define DOT11_RRM_CAP_LINK_ENAB			(1 << DOT11_RRM_CAP_LINK)
#define DOT11_RRM_CAP_FM_ENAB			(1 << (DOT11_RRM_CAP_FM - 8))
#define DOT11_RRM_CAP_CLM_ENAB			(1 << (DOT11_RRM_CAP_CLM - 8))
#define DOT11_RRM_CAP_NHM_ENAB			(1 << (DOT11_RRM_CAP_NHM - 8))
#define DOT11_RRM_CAP_SM_ENAB			(1 << (DOT11_RRM_CAP_SM - 8))
#define DOT11_RRM_CAP_LCIM_ENAB			(1 << (DOT11_RRM_CAP_LCIM - 8))
#define DOT11_RRM_CAP_TSCM_ENAB			(1 << (DOT11_RRM_CAP_TSCM - 8))
#ifdef WL11K_AP
#define DOT11_RRM_CAP_MPC0_ENAB			(1 << (DOT11_RRM_CAP_MPC0 - 24))
#define DOT11_RRM_CAP_MPC1_ENAB			(1 << (DOT11_RRM_CAP_MPC1 - 24))
#define DOT11_RRM_CAP_MPC2_ENAB			(1 << (DOT11_RRM_CAP_MPC2 - 24))
#define DOT11_RRM_CAP_MPTI_ENAB			(1 << (DOT11_RRM_CAP_MPTI - 24))
#else
#define DOT11_RRM_CAP_MPC0_ENAB			0
#define DOT11_RRM_CAP_MPC1_ENAB			0
#define DOT11_RRM_CAP_MPC2_ENAB			0
#define DOT11_RRM_CAP_MPTI_ENAB			0
#endif /* WL11K_AP */
#define DOT11_RRM_CAP_CIVIC_LOC_ENAB		(1 << (DOT11_RRM_CAP_CIVIC_LOC - 32))
#define DOT11_RRM_CAP_IDENT_LOC_ENAB		(1 << (DOT11_RRM_CAP_IDENT_LOC - 32))
#else
#define DOT11_RRM_CAP_LINK_ENAB			0
#define DOT11_RRM_CAP_FM_ENAB			0
#define DOT11_RRM_CAP_CLM_ENAB			0
#define DOT11_RRM_CAP_NHM_ENAB			0
#define DOT11_RRM_CAP_SM_ENAB			0
#define DOT11_RRM_CAP_LCIM_ENAB			0
#define DOT11_RRM_CAP_TSCM_ENAB			0
#define DOT11_RRM_CAP_MPC0_ENAB			0
#define DOT11_RRM_CAP_MPC1_ENAB			0
#define DOT11_RRM_CAP_MPC2_ENAB			0
#define DOT11_RRM_CAP_MPTI_ENAB			0
#define DOT11_RRM_CAP_CIVIC_LOC_ENAB		0
#define DOT11_RRM_CAP_IDENT_LOC_ENAB		0
#endif /* WL11K_ALL_MEAS */
#ifdef WL11K_NBR_MEAS
#define DOT11_RRM_CAP_NEIGHBOR_REPORT_ENAB	(1 << DOT11_RRM_CAP_NEIGHBOR_REPORT)
#else
#define DOT11_RRM_CAP_NEIGHBOR_REPORT_ENAB	0
#endif /* WL11K_NBR_MEAS */
#ifdef WL11K_BCN_MEAS
#define DOT11_RRM_CAP_BCN_PASSIVE_ENAB		(1 << DOT11_RRM_CAP_BCN_PASSIVE)
#define DOT11_RRM_CAP_BCN_ACTIVE_ENAB		(1 << DOT11_RRM_CAP_BCN_ACTIVE)
#else
#define DOT11_RRM_CAP_BCN_PASSIVE_ENAB		0
#define DOT11_RRM_CAP_BCN_ACTIVE_ENAB		0
#endif /* WL11K_BCN_MEAS */
#define DOT11_RRM_CAP_MPA_MASK		0x7
/* Operating Class (formerly "Regulatory Class") definitions */
#define DOT11_OP_CLASS_NONE			255

BWL_PRE_PACKED_STRUCT struct do11_ap_chrep {
	uint8 id;
	uint8 len;
	uint8 reg;
	uint8 chanlist[1];
} BWL_POST_PACKED_STRUCT;
typedef struct do11_ap_chrep dot11_ap_chrep_t;

/* Radio Measurements action ids */
#define DOT11_RM_ACTION_RM_REQ		0	/* Radio measurement request */
#define DOT11_RM_ACTION_RM_REP		1	/* Radio measurement report */
#define DOT11_RM_ACTION_LM_REQ		2	/* Link measurement request */
#define DOT11_RM_ACTION_LM_REP		3	/* Link measurement report */
#define DOT11_RM_ACTION_NR_REQ		4	/* Neighbor report request */
#define DOT11_RM_ACTION_NR_REP		5	/* Neighbor report response */
#define DOT11_PUB_ACTION_MP		7	/* Measurement Pilot public action id */

/** Generic radio measurement action frame header */
BWL_PRE_PACKED_STRUCT struct dot11_rm_action {
	uint8 category;				/* category of action frame (5) */
	uint8 action;				/* radio measurement action */
	uint8 token;				/* dialog token */
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rm_action dot11_rm_action_t;
#define DOT11_RM_ACTION_LEN 3

BWL_PRE_PACKED_STRUCT struct dot11_rmreq {
	uint8 category;				/* category of action frame (5) */
	uint8 action;				/* radio measurement action */
	uint8 token;				/* dialog token */
	uint16 reps;				/* no. of repetitions */
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq dot11_rmreq_t;
#define DOT11_RMREQ_LEN	5

BWL_PRE_PACKED_STRUCT struct dot11_rm_ie {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rm_ie dot11_rm_ie_t;
#define DOT11_RM_IE_LEN	5

/* Definitions for "mode" bits in rm req */
#define DOT11_RMREQ_MODE_PARALLEL	1
#define DOT11_RMREQ_MODE_ENABLE		2
#define DOT11_RMREQ_MODE_REQUEST	4
#define DOT11_RMREQ_MODE_REPORT		8
#define DOT11_RMREQ_MODE_DURMAND	0x10	/* Duration Mandatory */

/* Definitions for "mode" bits in rm rep */
#define DOT11_RMREP_MODE_LATE		1
#define DOT11_RMREP_MODE_INCAPABLE	2
#define DOT11_RMREP_MODE_REFUSED	4

BWL_PRE_PACKED_STRUCT struct dot11_rmreq_bcn {
	uint8 id;		/* use dot11_rm_ie_t ? */
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint8 reg;
	uint8 channel;
	uint16 interval;
	uint16 duration;
	uint8 bcn_mode;
	struct ether_addr	bssid;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq_bcn dot11_rmreq_bcn_t;
#define DOT11_RMREQ_BCN_LEN	18u

BWL_PRE_PACKED_STRUCT struct dot11_rmrep_bcn {
	uint8 reg;
	uint8 channel;
	uint32 starttime[2];
	uint16 duration;
	uint8 frame_info;
	uint8 rcpi;
	uint8 rsni;
	struct ether_addr	bssid;
	uint8 antenna_id;
	uint32 parent_tsf;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmrep_bcn dot11_rmrep_bcn_t;
#define DOT11_RMREP_BCN_LEN	26

/* Beacon request measurement mode */
#define DOT11_RMREQ_BCN_PASSIVE	0
#define DOT11_RMREQ_BCN_ACTIVE	1
#define DOT11_RMREQ_BCN_TABLE	2

/* Sub-element IDs for Beacon Request */
#define DOT11_RMREQ_BCN_SSID_ID 0
#define DOT11_RMREQ_BCN_REPINFO_ID  1
#define DOT11_RMREQ_BCN_REPDET_ID   2
#define DOT11_RMREQ_BCN_REQUEST_ID  10
#define DOT11_RMREQ_BCN_APCHREP_ID  DOT11_MNG_AP_CHREP_ID
#define DOT11_RMREQ_BCN_LAST_RPT_IND_REQ_ID 164

/* Reporting Detail element definition */
#define DOT11_RMREQ_BCN_REPDET_FIXED	0	/* Fixed length fields only */
#define DOT11_RMREQ_BCN_REPDET_REQUEST	1	/* + requested information elems */
#define DOT11_RMREQ_BCN_REPDET_ALL	2	/* All fields */

/* Reporting Information (reporting condition) element definition */
#define DOT11_RMREQ_BCN_REPINFO_LEN	2	/* Beacon Reporting Information length */
#define DOT11_RMREQ_BCN_REPCOND_DEFAULT	0	/* Report to be issued after each measurement */

/* Last Beacon Report Indication Request definition */
#define DOT11_RMREQ_BCN_LAST_RPT_IND_REQ_ENAB  1

BWL_PRE_PACKED_STRUCT struct dot11_rmrep_last_bcn_rpt_ind_req {
	uint8 id;                       /* DOT11_RMREQ_BCN_LAST_RPT_IND_REQ_ID */
	uint8 len;                      /* length of remaining fields */
	uint8 data;                     /* data = 1 means last bcn rpt ind requested */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmrep_last_bcn_rpt_ind_req dot11_rmrep_last_bcn_rpt_ind_req_t;

/* Sub-element IDs for Beacon Report */
#define DOT11_RMREP_BCN_FRM_BODY	1
#define DOT11_RMREP_BCN_FRM_BODY_FRAG_ID	2
#define DOT11_RMREP_BCN_LAST_RPT_IND 164
#define DOT11_RMREP_BCN_FRM_BODY_LEN_MAX	224 /* 802.11k-2008 7.3.2.22.6 */

/* Refer IEEE P802.11-REVmd/D1.0 9.4.2.21.7 Beacon report */
BWL_PRE_PACKED_STRUCT struct dot11_rmrep_bcn_frm_body_fragmt_id {
	uint8 id;                       /* DOT11_RMREP_BCN_FRM_BODY_FRAG_ID */
	uint8 len;                      /* length of remaining fields */
	/* More fragments(B15), fragment Id(B8-B14), Bcn rpt instance ID (B0 - B7) */
	uint16 frag_info_rpt_id;
} BWL_POST_PACKED_STRUCT;

typedef struct dot11_rmrep_bcn_frm_body_fragmt_id dot11_rmrep_bcn_frm_body_fragmt_id_t;

BWL_PRE_PACKED_STRUCT struct dot11_rmrep_bcn_frm_body_frag_id {
	uint8 id;                       /* DOT11_RMREP_BCN_FRM_BODY_FRAG_ID */
	uint8 len;                      /* length of remaining fields */
	uint8 bcn_rpt_id;               /* Bcn rpt instance ID */
	uint8 frag_info;                /* fragment Id(7 bits) | More fragments(1 bit) */
} BWL_POST_PACKED_STRUCT;

typedef struct dot11_rmrep_bcn_frm_body_frag_id dot11_rmrep_bcn_frm_body_frag_id_t;
#define DOT11_RMREP_BCNRPT_FRAG_ID_DATA_LEN  2u
#define DOT11_RMREP_BCNRPT_FRAG_ID_SE_LEN sizeof(dot11_rmrep_bcn_frm_body_frag_id_t)
#define DOT11_RMREP_BCNRPT_FRAG_ID_NUM_SHIFT  1u
#define DOT11_RMREP_BCNRPT_FRAGMT_ID_SE_LEN sizeof(dot11_rmrep_bcn_frm_body_fragmt_id_t)
#define DOT11_RMREP_BCNRPT_BCN_RPT_ID_MASK  0x00FFu
#define DOT11_RMREP_BCNRPT_FRAGMT_ID_NUM_SHIFT  8u
#define DOT11_RMREP_BCNRPT_FRAGMT_ID_NUM_MASK  0x7F00u
#define DOT11_RMREP_BCNRPT_MORE_FRAG_SHIFT  15u
#define DOT11_RMREP_BCNRPT_MORE_FRAG_MASK  0x8000u

BWL_PRE_PACKED_STRUCT struct dot11_rmrep_last_bcn_rpt_ind {
	uint8 id;                       /* DOT11_RMREP_BCN_LAST_RPT_IND */
	uint8 len;                      /* length of remaining fields */
	uint8 data;                     /* data = 1 is last bcn rpt */
} BWL_POST_PACKED_STRUCT;

typedef struct dot11_rmrep_last_bcn_rpt_ind dot11_rmrep_last_bcn_rpt_ind_t;
#define DOT11_RMREP_LAST_BCN_RPT_IND_DATA_LEN 1
#define DOT11_RMREP_LAST_BCN_RPT_IND_SE_LEN sizeof(dot11_rmrep_last_bcn_rpt_ind_t)

/* Sub-element IDs for Frame Report */
#define DOT11_RMREP_FRAME_COUNT_REPORT 1

/* Channel load request */
BWL_PRE_PACKED_STRUCT struct dot11_rmreq_chanload {
	uint8 id;		/* use dot11_rm_ie_t ? */
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint8 reg;
	uint8 channel;
	uint16 interval;
	uint16 duration;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq_chanload dot11_rmreq_chanload_t;
#define DOT11_RMREQ_CHANLOAD_LEN	11

/** Channel load report */
BWL_PRE_PACKED_STRUCT struct dot11_rmrep_chanload {
	uint8 reg;
	uint8 channel;
	uint32 starttime[2];
	uint16 duration;
	uint8 channel_load;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmrep_chanload dot11_rmrep_chanload_t;
#define DOT11_RMREP_CHANLOAD_LEN	13

/** Noise histogram request */
BWL_PRE_PACKED_STRUCT struct dot11_rmreq_noise {
	uint8 id;		/* use dot11_rm_ie_t ? */
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint8 reg;
	uint8 channel;
	uint16 interval;
	uint16 duration;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq_noise dot11_rmreq_noise_t;
#define DOT11_RMREQ_NOISE_LEN 11

/** Noise histogram report */
BWL_PRE_PACKED_STRUCT struct dot11_rmrep_noise {
	uint8 reg;
	uint8 channel;
	uint32 starttime[2];
	uint16 duration;
	uint8 antid;
	uint8 anpi;
	uint8 ipi0_dens;
	uint8 ipi1_dens;
	uint8 ipi2_dens;
	uint8 ipi3_dens;
	uint8 ipi4_dens;
	uint8 ipi5_dens;
	uint8 ipi6_dens;
	uint8 ipi7_dens;
	uint8 ipi8_dens;
	uint8 ipi9_dens;
	uint8 ipi10_dens;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmrep_noise dot11_rmrep_noise_t;
#define DOT11_RMREP_NOISE_LEN 25

/** Frame request */
BWL_PRE_PACKED_STRUCT struct dot11_rmreq_frame {
	uint8 id;		/* use dot11_rm_ie_t ? */
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint8 reg;
	uint8 channel;
	uint16 interval;
	uint16 duration;
	uint8 req_type;
	struct ether_addr	ta;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq_frame dot11_rmreq_frame_t;
#define DOT11_RMREQ_FRAME_LEN 18

/** Frame report */
BWL_PRE_PACKED_STRUCT struct dot11_rmrep_frame {
	uint8 reg;
	uint8 channel;
	uint32 starttime[2];
	uint16 duration;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmrep_frame dot11_rmrep_frame_t;
#define DOT11_RMREP_FRAME_LEN 12

/** Frame report entry */
BWL_PRE_PACKED_STRUCT struct dot11_rmrep_frmentry {
	struct ether_addr	ta;
	struct ether_addr	bssid;
	uint8 phy_type;
	uint8 avg_rcpi;
	uint8 last_rsni;
	uint8 last_rcpi;
	uint8 ant_id;
	uint16 frame_cnt;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmrep_frmentry dot11_rmrep_frmentry_t;
#define DOT11_RMREP_FRMENTRY_LEN 19

/** STA statistics request */
BWL_PRE_PACKED_STRUCT struct dot11_rmreq_stat {
	uint8 id;		/* use dot11_rm_ie_t ? */
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	struct ether_addr	peer;
	uint16 interval;
	uint16 duration;
	uint8 group_id;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq_stat dot11_rmreq_stat_t;
#define DOT11_RMREQ_STAT_LEN 16

/** STA statistics report */
BWL_PRE_PACKED_STRUCT struct dot11_rmrep_stat {
	uint16 duration;
	uint8 group_id;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmrep_stat dot11_rmrep_stat_t;

/* Statistics Group Report: Group IDs */
enum {
	DOT11_RRM_STATS_GRP_ID_0 = 0,
	DOT11_RRM_STATS_GRP_ID_1,
	DOT11_RRM_STATS_GRP_ID_2,
	DOT11_RRM_STATS_GRP_ID_3,
	DOT11_RRM_STATS_GRP_ID_4,
	DOT11_RRM_STATS_GRP_ID_5,
	DOT11_RRM_STATS_GRP_ID_6,
	DOT11_RRM_STATS_GRP_ID_7,
	DOT11_RRM_STATS_GRP_ID_8,
	DOT11_RRM_STATS_GRP_ID_9,
	DOT11_RRM_STATS_GRP_ID_10,
	DOT11_RRM_STATS_GRP_ID_11,
	DOT11_RRM_STATS_GRP_ID_12,
	DOT11_RRM_STATS_GRP_ID_13,
	DOT11_RRM_STATS_GRP_ID_14,
	DOT11_RRM_STATS_GRP_ID_15,
	DOT11_RRM_STATS_GRP_ID_16
};

/* Statistics Group Report: Group Data length  */
#define DOT11_RRM_STATS_RPT_LEN_GRP_ID_0	28
typedef struct rrm_stat_group_0 {
	uint32	txfrag;
	uint32	txmulti;
	uint32	txfail;
	uint32	rxframe;
	uint32	rxmulti;
	uint32	rxbadfcs;
	uint32	txframe;
} rrm_stat_group_0_t;

#define DOT11_RRM_STATS_RPT_LEN_GRP_ID_1	24
typedef struct rrm_stat_group_1 {
	uint32	txretry;
	uint32	txretries;
	uint32	rxdup;
	uint32	txrts;
	uint32	rtsfail;
	uint32	ackfail;
} rrm_stat_group_1_t;

/* group 2-9 use same qos data structure (tid 0-7), total 52 bytes */
#define DOT11_RRM_STATS_RPT_LEN_GRP_ID_2_9	52
typedef struct rrm_stat_group_qos {
	uint32	txfrag;
	uint32	txfail;
	uint32	txretry;
	uint32	txretries;
	uint32	rxdup;
	uint32	txrts;
	uint32	rtsfail;
	uint32	ackfail;
	uint32	rxfrag;
	uint32	txframe;
	uint32	txdrop;
	uint32	rxmpdu;
	uint32	rxretries;
} rrm_stat_group_qos_t;

/* dot11BSSAverageAccessDelay Group (only available at an AP): 8 byte */
#define DOT11_RRM_STATS_RPT_LEN_GRP_ID_10	8
typedef BWL_PRE_PACKED_STRUCT struct rrm_stat_group_10 {
	uint8	apavgdelay;
	uint8	avgdelaybe;
	uint8	avgdelaybg;
	uint8	avgdelayvi;
	uint8	avgdelayvo;
	uint16	stacount;
	uint8	chanutil;
} BWL_POST_PACKED_STRUCT rrm_stat_group_10_t;

/* AMSDU, 40 bytes */
#define DOT11_RRM_STATS_RPT_LEN_GRP_ID_11	40
typedef struct rrm_stat_group_11 {
	uint32	txamsdu;
	uint32	amsdufail;
	uint32	amsduretry;
	uint32	amsduretries;
	uint32	txamsdubyte_h;
	uint32	txamsdubyte_l;
	uint32	amsduackfail;
	uint32	rxamsdu;
	uint32	rxamsdubyte_h;
	uint32	rxamsdubyte_l;
} rrm_stat_group_11_t;

/* AMPDU, 36 bytes */
#define DOT11_RRM_STATS_RPT_LEN_GRP_ID_12	36
typedef struct rrm_stat_group_12 {
	uint32	txampdu;
	uint32	txmpdu;
	uint32	txampdubyte_h;
	uint32	txampdubyte_l;
	uint32	rxampdu;
	uint32	rxmpdu;
	uint32	rxampdubyte_h;
	uint32	rxampdubyte_l;
	uint32	ampducrcfail;
} rrm_stat_group_12_t;

/* BACK etc, 36 bytes */
#define DOT11_RRM_STATS_RPT_LEN_GRP_ID_13	36
typedef struct rrm_stat_group_13 {
	uint32	rximpbarfail;
	uint32	rxexpbarfail;
	uint32	chanwidthsw;
	uint32	txframe20mhz;
	uint32	txframe40mhz;
	uint32	rxframe20mhz;
	uint32	rxframe40mhz;
	uint32	psmpgrantdur;
	uint32	psmpuseddur;
} rrm_stat_group_13_t;

/* RD Dual CTS etc, 36 bytes */
#define DOT11_RRM_STATS_RPT_LEN_GRP_ID_14	36
typedef struct rrm_stat_group_14 {
	uint32	grantrdgused;
	uint32	grantrdgunused;
	uint32	txframeingrantrdg;
	uint32	txbyteingrantrdg_h;
	uint32	txbyteingrantrdg_l;
	uint32	dualcts;
	uint32	dualctsfail;
	uint32	rtslsi;
	uint32	rtslsifail;
} rrm_stat_group_14_t;

/* bf and STBC etc, 20 bytes */
#define DOT11_RRM_STATS_RPT_LEN_GRP_ID_15	20
typedef struct rrm_stat_group_15 {
	uint32	bfframe;
	uint32	stbccts;
	uint32	stbcctsfail;
	uint32	nonstbccts;
	uint32	nonstbcctsfail;
} rrm_stat_group_15_t;

/* RSNA, 28 bytes */
#define DOT11_RRM_STATS_RPT_LEN_GRP_ID_16	28
typedef struct rrm_stat_group_16 {
	uint32	rsnacmacicverr;
	uint32	rsnacmacreplay;
	uint32	rsnarobustmgmtccmpreplay;
	uint32	rsnatkipicverr;
	uint32	rsnatkipicvreplay;
	uint32	rsnaccmpdecrypterr;
	uint32	rsnaccmpreplay;
} rrm_stat_group_16_t;

/* Transmit stream/category measurement request */
BWL_PRE_PACKED_STRUCT struct dot11_rmreq_tx_stream {
	uint8 id;		/* use dot11_rm_ie_t ? */
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint16 interval;
	uint16 duration;
	struct ether_addr	peer;
	uint8 traffic_id;
	uint8 bin0_range;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq_tx_stream dot11_rmreq_tx_stream_t;
#define DOT11_RMREQ_TXSTREAM_LEN	17

/** Transmit stream/category measurement report */
BWL_PRE_PACKED_STRUCT struct dot11_rmrep_tx_stream {
	uint32 starttime[2];
	uint16 duration;
	struct ether_addr	peer;
	uint8 traffic_id;
	uint8 reason;
	uint32 txmsdu_cnt;
	uint32 msdu_discarded_cnt;
	uint32 msdufailed_cnt;
	uint32 msduretry_cnt;
	uint32 cfpolls_lost_cnt;
	uint32 avrqueue_delay;
	uint32 avrtx_delay;
	uint8 bin0_range;
	uint32 bin0;
	uint32 bin1;
	uint32 bin2;
	uint32 bin3;
	uint32 bin4;
	uint32 bin5;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmrep_tx_stream dot11_rmrep_tx_stream_t;
#define DOT11_RMREP_TXSTREAM_LEN	71

typedef struct rrm_tscm {
	uint32 msdu_tx;
	uint32 msdu_exp;
	uint32 msdu_fail;
	uint32 msdu_retries;
	uint32 cfpolls_lost;
	uint32 queue_delay;
	uint32 tx_delay_sum;
	uint32 tx_delay_cnt;
	uint32 bin0_range_us;
	uint32 bin0;
	uint32 bin1;
	uint32 bin2;
	uint32 bin3;
	uint32 bin4;
	uint32 bin5;
} rrm_tscm_t;
enum {
	DOT11_FTM_LOCATION_SUBJ_LOCAL = 0,		/* Where am I? */
	DOT11_FTM_LOCATION_SUBJ_REMOTE = 1,		/* Where are you? */
	DOT11_FTM_LOCATION_SUBJ_THIRDPARTY = 2   /* Where is he/she? */
};

BWL_PRE_PACKED_STRUCT struct dot11_rmreq_ftm_lci {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint8 subj;

	/* Following 3 fields are unused. Keep for ROM compatibility. */
	uint8 lat_res;
	uint8 lon_res;
	uint8 alt_res;

	/* optional sub-elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq_ftm_lci dot11_rmreq_ftm_lci_t;
#define DOT11_RMREQ_LCI_LEN	9

BWL_PRE_PACKED_STRUCT struct dot11_rmrep_ftm_lci {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint8 lci_sub_id;
	uint8 lci_sub_len;
	/* optional LCI field */
	/* optional sub-elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmrep_ftm_lci dot11_rmrep_ftm_lci_t;

#define DOT11_FTM_LCI_SUBELEM_ID		0
#define DOT11_FTM_LCI_SUBELEM_LEN		2
#define DOT11_FTM_LCI_FIELD_LEN			16
#define DOT11_FTM_LCI_UNKNOWN_LEN		2

BWL_PRE_PACKED_STRUCT struct dot11_rmreq_ftm_civic {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint8 subj;
	uint8 civloc_type;
	uint8 siu;	/* service interval units */
	uint16 si;  /* service interval */
	/* optional sub-elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq_ftm_civic dot11_rmreq_ftm_civic_t;
#define DOT11_RMREQ_CIVIC_LEN	10

BWL_PRE_PACKED_STRUCT struct dot11_rmrep_ftm_civic {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint8 civloc_type;
	uint8 civloc_sub_id;
	uint8 civloc_sub_len;
	/* optional location civic field */
	/* optional sub-elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmrep_ftm_civic dot11_rmrep_ftm_civic_t;

#define DOT11_FTM_CIVIC_LOC_TYPE_RFC4776	0
#define DOT11_FTM_CIVIC_SUBELEM_ID		0
#define DOT11_FTM_CIVIC_SUBELEM_LEN		2
#define DOT11_FTM_CIVIC_LOC_SI_NONE		0
#define DOT11_FTM_CIVIC_TYPE_LEN		1
#define DOT11_FTM_CIVIC_UNKNOWN_LEN		3

/* Location Identifier measurement request */
BWL_PRE_PACKED_STRUCT struct dot11_rmreq_locid {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint8 subj;
	uint8 siu;
	uint16 si;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq_locid dot11_rmreq_locid_t;
#define DOT11_RMREQ_LOCID_LEN	9

/* Location Identifier measurement report */
BWL_PRE_PACKED_STRUCT struct dot11_rmrep_locid {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint8 exp_tsf[8];
	uint8 locid_sub_id;
	uint8 locid_sub_len;
	/* optional location identifier field */
	/* optional sub-elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmrep_locid dot11_rmrep_locid_t;
#define DOT11_LOCID_UNKNOWN_LEN		10
#define DOT11_LOCID_SUBELEM_ID		0

BWL_PRE_PACKED_STRUCT struct dot11_ftm_range_subel {
	uint8 id;
	uint8 len;
	uint16 max_age;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ftm_range_subel dot11_ftm_range_subel_t;
#define DOT11_FTM_RANGE_SUBELEM_ID      4
#define DOT11_FTM_RANGE_SUBELEM_LEN     2

BWL_PRE_PACKED_STRUCT struct dot11_rmreq_ftm_range {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint16 max_init_delay;		/* maximum random initial delay */
	uint8 min_ap_count;
	uint8 data[1];
	/* neighbor report sub-elements */
	/* optional sub-elements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq_ftm_range dot11_rmreq_ftm_range_t;
#define DOT11_RMREQ_FTM_RANGE_LEN 8

#define DOT11_FTM_RANGE_LEN		3
BWL_PRE_PACKED_STRUCT struct dot11_ftm_range_entry {
	uint32 start_tsf;		/* 4 lsb of tsf */
	struct ether_addr bssid;
	uint8 range[DOT11_FTM_RANGE_LEN];
	uint8 max_err[DOT11_FTM_RANGE_LEN];
	uint8  rsvd;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ftm_range_entry dot11_ftm_range_entry_t;
#define DOT11_FTM_RANGE_ENTRY_MAX_COUNT   15

enum {
	DOT11_FTM_RANGE_ERROR_AP_INCAPABLE = 2,
	DOT11_FTM_RANGE_ERROR_AP_FAILED = 3,
	DOT11_FTM_RANGE_ERROR_TX_FAILED = 8,
	DOT11_FTM_RANGE_ERROR_MAX
};

BWL_PRE_PACKED_STRUCT struct dot11_ftm_range_error_entry {
	uint32 start_tsf;		/* 4 lsb of tsf */
	struct ether_addr bssid;
	uint8  code;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ftm_range_error_entry dot11_ftm_range_error_entry_t;
#define DOT11_FTM_RANGE_ERROR_ENTRY_MAX_COUNT   11

BWL_PRE_PACKED_STRUCT struct dot11_rmrep_ftm_range {
    uint8 id;
    uint8 len;
    uint8 token;
    uint8 mode;
    uint8 type;
    uint8 entry_count;
    uint8 data[2]; /* includes pad */
	/*
	dot11_ftm_range_entry_t entries[entry_count];
	uint8 error_count;
	dot11_ftm_error_entry_t errors[error_count];
	 */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmrep_ftm_range dot11_rmrep_ftm_range_t;

#define DOT11_FTM_RANGE_REP_MIN_LEN     6       /* No extra byte for error_count */
#define DOT11_FTM_RANGE_ENTRY_CNT_MAX   15
#define DOT11_FTM_RANGE_ERROR_CNT_MAX   11
#define DOT11_FTM_RANGE_REP_FIXED_LEN   1       /* No extra byte for error_count */
/** Measurement pause request */
BWL_PRE_PACKED_STRUCT struct dot11_rmreq_pause_time {
	uint8 id;		/* use dot11_rm_ie_t ? */
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint16 pause_time;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq_pause_time dot11_rmreq_pause_time_t;
#define DOT11_RMREQ_PAUSE_LEN	7

/* Neighbor Report subelements ID (11k & 11v) */
#define DOT11_NGBR_TSF_INFO_SE_ID	1
#define DOT11_NGBR_CCS_SE_ID		2
#define DOT11_NGBR_BSSTRANS_PREF_SE_ID	3
#define DOT11_NGBR_BSS_TERM_DUR_SE_ID	4
#define DOT11_NGBR_BEARING_SE_ID	5
#define DOT11_NGBR_WIDE_BW_CHAN_SE_ID	6 /* proposed */

/** Neighbor Report, BSS Transition Candidate Preference subelement */
BWL_PRE_PACKED_STRUCT struct dot11_ngbr_bsstrans_pref_se {
	uint8 sub_id;
	uint8 len;
	uint8 preference;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ngbr_bsstrans_pref_se dot11_ngbr_bsstrans_pref_se_t;
#define DOT11_NGBR_BSSTRANS_PREF_SE_LEN		1
#define DOT11_NGBR_BSSTRANS_PREF_SE_IE_LEN	3
#define DOT11_NGBR_BSSTRANS_PREF_SE_HIGHEST	0xff

/** Neighbor Report, BSS Termination Duration subelement */
BWL_PRE_PACKED_STRUCT struct dot11_ngbr_bss_term_dur_se {
	uint8 sub_id;
	uint8 len;
	uint8 tsf[8];
	uint16 duration;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ngbr_bss_term_dur_se dot11_ngbr_bss_term_dur_se_t;
#define DOT11_NGBR_BSS_TERM_DUR_SE_LEN	10

/* Neighbor Report BSSID Information Field */
#define DOT11_NGBR_BI_REACHABILTY_UNKN	0x0002
#define DOT11_NGBR_BI_REACHABILTY	0x0003
#define DOT11_NGBR_BI_SEC		0x0004
#define DOT11_NGBR_BI_KEY_SCOPE		0x0008
#define DOT11_NGBR_BI_CAP		0x03f0
#define DOT11_NGBR_BI_CAP_SPEC_MGMT	0x0010
#define DOT11_NGBR_BI_CAP_QOS		0x0020
#define DOT11_NGBR_BI_CAP_APSD		0x0040
#define DOT11_NGBR_BI_CAP_RDIO_MSMT	0x0080
#define DOT11_NGBR_BI_CAP_DEL_BA	0x0100
#define DOT11_NGBR_BI_CAP_IMM_BA	0x0200
#define DOT11_NGBR_BI_MOBILITY		0x0400
#define DOT11_NGBR_BI_HT		0x0800
#define DOT11_NGBR_BI_VHT		0x1000
#define DOT11_NGBR_BI_FTM		0x2000

/** Neighbor Report element (11k & 11v) */
BWL_PRE_PACKED_STRUCT struct dot11_neighbor_rep_ie {
	uint8 id;
	uint8 len;
	struct ether_addr bssid;
	uint32 bssid_info;
	uint8 reg;		/* Operating class */
	uint8 channel;
	uint8 phytype;
	uint8 data[1];		/* Variable size subelements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_neighbor_rep_ie dot11_neighbor_rep_ie_t;
#define DOT11_NEIGHBOR_REP_IE_FIXED_LEN	13u

/* MLME Enumerations */
#define DOT11_BSSTYPE_INFRASTRUCTURE		0	/* d11 infrastructure */
#define DOT11_BSSTYPE_INDEPENDENT		1	/* d11 independent */
#define DOT11_BSSTYPE_ANY			2	/* d11 any BSS type */
#define DOT11_BSSTYPE_MESH			3	/* d11 Mesh */
#define DOT11_SCANTYPE_ACTIVE			0	/* d11 scan active */
#define DOT11_SCANTYPE_PASSIVE			1	/* d11 scan passive */

/** Link Measurement */
BWL_PRE_PACKED_STRUCT struct dot11_lmreq {
	uint8 category;				/* category of action frame (5) */
	uint8 action;				/* radio measurement action */
	uint8 token;				/* dialog token */
	uint8 txpwr;				/* Transmit Power Used */
	uint8 maxtxpwr;				/* Max Transmit Power */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_lmreq dot11_lmreq_t;
#define DOT11_LMREQ_LEN	5

BWL_PRE_PACKED_STRUCT struct dot11_lmrep {
	uint8 category;				/* category of action frame (5) */
	uint8 action;				/* radio measurement action */
	uint8 token;				/* dialog token */
	dot11_tpc_rep_t tpc;			/* TPC element */
	uint8 rxant;				/* Receive Antenna ID */
	uint8 txant;				/* Transmit Antenna ID */
	uint8 rcpi;				/* RCPI */
	uint8 rsni;				/* RSNI */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_lmrep dot11_lmrep_t;
#define DOT11_LMREP_LEN	11

#define DOT11_MP_CAP_SPECTRUM			0x01	/* d11 cap. spectrum */
#define DOT11_MP_CAP_SHORTSLOT			0x02	/* d11 cap. shortslot */
/* Measurement Pilot */
BWL_PRE_PACKED_STRUCT struct dot11_mprep {
	uint8 cap_info;				/* Condensed capability Info. */
	uint8 country[2];				/* Condensed country string */
	uint8 opclass;				/* Op. Class */
	uint8 channel;				/* Channel */
	uint8 mp_interval;			/* Measurement Pilot Interval */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_mprep dot11_mprep_t;
#define DOT11_MPREP_LEN	6

/* 802.11 BRCM "Compromise" Pre N constants */
#define PREN_PREAMBLE		24	/* green field preamble time */
#define PREN_MM_EXT		12	/* extra mixed mode preamble time */
#define PREN_PREAMBLE_EXT	4	/* extra preamble (multiply by unique_streams-1) */

/* 802.11N PHY constants */
#define RIFS_11N_TIME		2	/* NPHY RIFS time */

/* 802.11 HT PLCP format 802.11n-2009, sec 20.3.9.4.3
 * HT-SIG is composed of two 24 bit parts, HT-SIG1 and HT-SIG2
 */
/* HT-SIG1 */
#define HT_SIG1_MCS_MASK        0x00007F
#define HT_SIG1_CBW             0x000080
#define HT_SIG1_HT_LENGTH       0xFFFF00

/* HT-SIG2 */
#define HT_SIG2_SMOOTHING       0x000001
#define HT_SIG2_NOT_SOUNDING    0x000002
#define HT_SIG2_RESERVED        0x000004
#define HT_SIG2_AGGREGATION     0x000008
#define HT_SIG2_STBC_MASK       0x000030
#define HT_SIG2_STBC_SHIFT      4
#define HT_SIG2_FEC_CODING      0x000040
#define HT_SIG2_SHORT_GI        0x000080
#define HT_SIG2_ESS_MASK        0x000300
#define HT_SIG2_ESS_SHIFT       8
#define HT_SIG2_CRC             0x03FC00
#define HT_SIG2_TAIL            0x1C0000

/* HT Timing-related parameters (802.11-2012, sec 20.3.6) */
#define HT_T_LEG_PREAMBLE      16
#define HT_T_L_SIG              4
#define HT_T_SIG                8
#define HT_T_LTF1               4
#define HT_T_GF_LTF1            8
#define HT_T_LTFs               4
#define HT_T_STF                4
#define HT_T_GF_STF             8
#define HT_T_SYML               4

#define HT_N_SERVICE           16       /* bits in SERVICE field */
#define HT_N_TAIL               6       /* tail bits per BCC encoder */

/* 802.11 A PHY constants */
#define APHY_SLOT_TIME          9       /* APHY slot time */
#define APHY_SIFS_TIME          16      /* APHY SIFS time */
#define APHY_DIFS_TIME          (APHY_SIFS_TIME + (2 * APHY_SLOT_TIME))  /* APHY DIFS time */
#define APHY_PREAMBLE_TIME      16      /* APHY preamble time */
#define APHY_SIGNAL_TIME        4       /* APHY signal time */
#define APHY_SYMBOL_TIME        4       /* APHY symbol time */
#define APHY_SERVICE_NBITS      16      /* APHY service nbits */
#define APHY_TAIL_NBITS         6       /* APHY tail nbits */
#define APHY_CWMIN              15      /* APHY cwmin */
#define APHY_PHYHDR_DUR		20	/* APHY PHY Header Duration */

/* 802.11 B PHY constants */
#define BPHY_SLOT_TIME          20      /* BPHY slot time */
#define BPHY_SIFS_TIME          10      /* BPHY SIFS time */
#define BPHY_DIFS_TIME          50      /* BPHY DIFS time */
#define BPHY_PLCP_TIME          192     /* BPHY PLCP time */
#define BPHY_PLCP_SHORT_TIME    96      /* BPHY PLCP short time */
#define BPHY_CWMIN              31      /* BPHY cwmin */
#define BPHY_SHORT_PHYHDR_DUR	96	/* BPHY Short PHY Header Duration */
#define BPHY_LONG_PHYHDR_DUR	192	/* BPHY Long PHY Header Duration */

/* 802.11 G constants */
#define DOT11_OFDM_SIGNAL_EXTENSION	6	/* d11 OFDM signal extension */

#define PHY_CWMAX		1023	/* PHY cwmax */

#define	DOT11_MAXNUMFRAGS	16	/* max # fragments per MSDU */

/* 802.11 VHT constants */

typedef int vht_group_id_t;

/* for VHT-A1 */
/* SIG-A1 reserved bits */
#define VHT_SIGA1_CONST_MASK            0x800004

#define VHT_SIGA1_BW_MASK               0x000003
#define VHT_SIGA1_20MHZ_VAL             0x000000
#define VHT_SIGA1_40MHZ_VAL             0x000001
#define VHT_SIGA1_80MHZ_VAL             0x000002
#define VHT_SIGA1_160MHZ_VAL            0x000003

#define VHT_SIGA1_STBC                  0x000008

#define VHT_SIGA1_GID_MASK              0x0003f0
#define VHT_SIGA1_GID_SHIFT             4
#define VHT_SIGA1_GID_TO_AP             0x00
#define VHT_SIGA1_GID_NOT_TO_AP         0x3f
#define VHT_SIGA1_GID_MAX_GID           0x3f

#define VHT_SIGA1_NSTS_SHIFT_MASK_USER0 0x001C00
#define VHT_SIGA1_NSTS_SHIFT            10
#define VHT_SIGA1_MAX_USERPOS           3

#define VHT_SIGA1_PARTIAL_AID_MASK      0x3fe000
#define VHT_SIGA1_PARTIAL_AID_SHIFT     13

#define VHT_SIGA1_TXOP_PS_NOT_ALLOWED   0x400000

/* for VHT-A2 */
#define VHT_SIGA2_GI_NONE               0x000000
#define VHT_SIGA2_GI_SHORT              0x000001
#define VHT_SIGA2_GI_W_MOD10            0x000002
#define VHT_SIGA2_CODING_LDPC           0x000004
#define VHT_SIGA2_LDPC_EXTRA_OFDM_SYM   0x000008
#define VHT_SIGA2_BEAMFORM_ENABLE       0x000100
#define VHT_SIGA2_MCS_SHIFT             4

#define VHT_SIGA2_B9_RESERVED           0x000200
#define VHT_SIGA2_TAIL_MASK             0xfc0000
#define VHT_SIGA2_TAIL_VALUE            0x000000

/* VHT Timing-related parameters (802.11ac D4.0, sec 22.3.6) */
#define VHT_T_LEG_PREAMBLE      16
#define VHT_T_L_SIG              4
#define VHT_T_SIG_A              8
#define VHT_T_LTF                4
#define VHT_T_STF                4
#define VHT_T_SIG_B              4
#define VHT_T_SYML               4

#define VHT_N_SERVICE           16	/* bits in SERVICE field */
#define VHT_N_TAIL               6	/* tail bits per BCC encoder */

/** dot11Counters Table - 802.11 spec., Annex D */
typedef struct d11cnt {
	uint32		txfrag;		/* dot11TransmittedFragmentCount */
	uint32		txmulti;	/* dot11MulticastTransmittedFrameCount */
	uint32		txfail;		/* dot11FailedCount */
	uint32		txretry;	/* dot11RetryCount */
	uint32		txretrie;	/* dot11MultipleRetryCount */
	uint32		rxdup;		/* dot11FrameduplicateCount */
	uint32		txrts;		/* dot11RTSSuccessCount */
	uint32		txnocts;	/* dot11RTSFailureCount */
	uint32		txnoack;	/* dot11ACKFailureCount */
	uint32		rxfrag;		/* dot11ReceivedFragmentCount */
	uint32		rxmulti;	/* dot11MulticastReceivedFrameCount */
	uint32		rxcrc;		/* dot11FCSErrorCount */
	uint32		txfrmsnt;	/* dot11TransmittedFrameCount */
	uint32		rxundec;	/* dot11WEPUndecryptableCount */
} d11cnt_t;

/* OUI for BRCM proprietary IE */
#define BRCM_PROP_OUI		"\x00\x90\x4C"	/* Broadcom proprietary OUI */

/* Broadcom Proprietary OUI type list. Please update below page when adding a new type.
 * Twiki http://hwnbu-twiki.sj.broadcom.com/bin/view/Mwgroup/WlBrcmPropIE
 */
/* The following BRCM_PROP_OUI types are currently in use (defined in
 * relevant subsections). Each of them will be in a separate proprietary(221) IE
 * #define RWL_WIFI_DEFAULT		0
 * #define SES_VNDR_IE_TYPE		1   (defined in src/ses/shared/ses.h)
 * #define VHT_FEATURES_IE_TYPE		4
 * #define RWL_WIFI_FIND_MY_PEER	9
 * #define RWL_WIFI_FOUND_PEER		10
 * #define PROXD_IE_TYPE		11
 */

#define BRCM_FTM_IE_TYPE		14

/* #define HT_CAP_IE_TYPE		51
 * #define HT_ADD_IE_TYPE		52
 * #define BRCM_EXTCH_IE_TYPE		53
 * #define MEMBER_OF_BRCM_PROP_IE_TYPE	54
 * #define BRCM_RELMACST_IE_TYPE	55
 * #define BRCM_EVT_WL_BSS_INFO		64
 * #define RWL_ACTION_WIFI_FRAG_TYPE	85
 * #define BTC_INFO_BRCM_PROP_IE_TYPE	90
 * #define ULB_BRCM_PROP_IE_TYPE	91
 * #define SDB_BRCM_PROP_IE_TYPE	92
 */

/* Action frame type for RWL */
#define RWL_WIFI_DEFAULT		0
#define RWL_WIFI_FIND_MY_PEER		9 /* Used while finding server */
#define RWL_WIFI_FOUND_PEER		10 /* Server response to the client  */
#define RWL_ACTION_WIFI_FRAG_TYPE	85 /* Fragment indicator for receiver */

#define PROXD_AF_TYPE			11 /* Wifi proximity action frame type */
#define BRCM_RELMACST_AF_TYPE	        12 /* RMC action frame type */

/* Action frame type for FTM Initiator Report */
#define BRCM_FTM_VS_AF_TYPE	14
enum {
	BRCM_FTM_VS_INITIATOR_RPT_SUBTYPE = 1,	/* FTM Initiator Report */
	BRCM_FTM_VS_COLLECT_SUBTYPE = 2,	/* FTM Collect debug protocol */
};

/* Action frame type for vendor specific action frames */
#define	VS_AF_TYPE	221

#ifdef WL_VS_AFTX
/* Vendor specific action frame subtype for transmit using SU EDCA */
#define VS_AF_SUBTYPE_SUEDCA	1

#define VENDOR_PROP_OUI		"\x00\x17\xF2"
#endif /* WL_VS_AFTX */

/*
 * This BRCM_PROP_OUI types is intended for use in events to embed additional
 * data, and would not be expected to appear on the air -- but having an IE
 * format allows IE frame data with extra data in events in that allows for
 * more flexible parsing.
 */
#define BRCM_EVT_WL_BSS_INFO	64

/**
 * Following is the generic structure for brcm_prop_ie (uses BRCM_PROP_OUI).
 * DPT uses this format with type set to DPT_IE_TYPE
 */
BWL_PRE_PACKED_STRUCT struct brcm_prop_ie_s {
	uint8 id;		/* IE ID, 221, DOT11_MNG_PROPR_ID */
	uint8 len;		/* IE length */
	uint8 oui[3];		/* Proprietary OUI, BRCM_PROP_OUI */
	uint8 type;		/* type of this IE */
	uint16 cap;		/* DPT capabilities */
} BWL_POST_PACKED_STRUCT;
typedef struct brcm_prop_ie_s brcm_prop_ie_t;

#define BRCM_PROP_IE_LEN	6	/* len of fixed part of brcm_prop ie */

#define DPT_IE_TYPE             2

#define BRCM_SYSCAP_IE_TYPE	3
#define WET_TUNNEL_IE_TYPE	3

/* brcm syscap_ie cap */
#define BRCM_SYSCAP_WET_TUNNEL	0x0100	/* Device with WET_TUNNEL support */

/* BRCM OUI: Used in the proprietary(221) IE in all broadcom devices */
#define BRCM_OUI		"\x00\x10\x18"	/* Broadcom OUI */

/** BRCM info element */
BWL_PRE_PACKED_STRUCT struct brcm_ie {
	uint8	id;		/* IE ID, 221, DOT11_MNG_PROPR_ID */
	uint8	len;		/* IE length */
	uint8	oui[3];		/* Proprietary OUI, BRCM_OUI */
	uint8	ver;		/* type/ver of this IE */
	uint8	assoc;		/* # of assoc STAs */
	uint8	flags;		/* misc flags */
	uint8	flags1;		/* misc flags */
	uint16	amsdu_mtu_pref;	/* preferred A-MSDU MTU */
	uint8	flags2;		/* Bit 0: DTPC TX cap, Bit 1: DTPC Recv Cap */
} BWL_POST_PACKED_STRUCT;
typedef	struct brcm_ie brcm_ie_t;
#define BRCM_IE_LEN		12u	/* BRCM IE length */
#define BRCM_IE_VER		2u	/* BRCM IE version */
#define BRCM_IE_LEGACY_AES_VER	1u	/* BRCM IE legacy AES version */

/* brcm_ie flags */
#define	BRF_ABCAP		0x1	/* afterburner is obsolete,  defined for backward compat */
#define	BRF_ABRQRD		0x2	/* afterburner is obsolete,  defined for backward compat */
#define	BRF_LZWDS		0x4	/* lazy wds enabled */
#define	BRF_BLOCKACK		0x8	/* BlockACK capable */
#define BRF_ABCOUNTER_MASK	0xf0	/* afterburner is obsolete,  defined for backward compat */
#define BRF_PROP_11N_MCS	0x10	/* re-use afterburner bit */
#define BRF_MEDIA_CLIENT	0x20	/* re-use afterburner bit to indicate media client device */

/**
 * Support for Broadcom proprietary HT MCS rates. Re-uses afterburner bits since
 * afterburner is not used anymore. Checks for BRF_ABCAP to stay compliant with 'old'
 * images in the field.
 */
#define GET_BRF_PROP_11N_MCS(brcm_ie) \
	(!((brcm_ie)->flags & BRF_ABCAP) && ((brcm_ie)->flags & BRF_PROP_11N_MCS))

/* brcm_ie flags1 */
#define	BRF1_AMSDU		0x1	/* A-MSDU capable */
#define	BRF1_WNM		0x2	/* WNM capable */
#define BRF1_WMEPS		0x4	/* AP is capable of handling WME + PS w/o APSD */
#define BRF1_PSOFIX		0x8	/* AP has fixed PS mode out-of-order packets */
#define	BRF1_RX_LARGE_AGG	0x10	/* device can rx large aggregates */
#define BRF1_RFAWARE_DCS	0x20    /* RFAWARE dynamic channel selection (DCS) */
#define BRF1_SOFTAP		0x40    /* Configure as Broadcom SOFTAP */
#define BRF1_DWDS		0x80    /* DWDS capable */

/* brcm_ie flags2 */
#define BRF2_DTPC_TX		0x1u	/* DTPC: DTPC TX Cap */
#define BRF2_DTPC_RX		0x2u	/* DTPC: DTPC RX Cap */
#define BRF2_DTPC_TX_RX		0x3u	/* DTPC: Enable Both DTPC TX and RX Cap */

/** Vendor IE structure */
BWL_PRE_PACKED_STRUCT struct vndr_ie {
	uchar id;
	uchar len;
	uchar oui [3];
	uchar data [1];   /* Variable size data */
} BWL_POST_PACKED_STRUCT;
typedef struct vndr_ie vndr_ie_t;

#define VNDR_IE_HDR_LEN		2u	/* id + len field */
#define VNDR_IE_MIN_LEN		3u	/* size of the oui field */
#define VNDR_IE_FIXED_LEN	(VNDR_IE_HDR_LEN + VNDR_IE_MIN_LEN)

#define VNDR_IE_MAX_LEN		255u	/* vendor IE max length, without ID and len */

/** BRCM PROP DEVICE PRIMARY MAC ADDRESS IE */
BWL_PRE_PACKED_STRUCT struct member_of_brcm_prop_ie {
	uchar id;
	uchar len;
	uchar oui[3];
	uint8	type;           /* type indicates what follows */
	struct ether_addr ea;   /* Device Primary MAC Adrress */
} BWL_POST_PACKED_STRUCT;
typedef struct member_of_brcm_prop_ie member_of_brcm_prop_ie_t;

#define MEMBER_OF_BRCM_PROP_IE_LEN		10	/* IE max length */
#define MEMBER_OF_BRCM_PROP_IE_HDRLEN	        (sizeof(member_of_brcm_prop_ie_t))
#define MEMBER_OF_BRCM_PROP_IE_TYPE		54      /* used in prop IE 221 only */

/** BRCM Reliable Multicast IE */
BWL_PRE_PACKED_STRUCT struct relmcast_brcm_prop_ie {
	uint8 id;
	uint8 len;
	uint8 oui[3];
	uint8 type;           /* type indicates what follows */
	struct ether_addr ea;   /* The ack sender's MAC Adrress */
	struct ether_addr mcast_ea;  /* The multicast MAC address */
	uint8 updtmo; /* time interval(second) for client to send null packet to report its rssi */
} BWL_POST_PACKED_STRUCT;
typedef struct relmcast_brcm_prop_ie relmcast_brcm_prop_ie_t;

/* IE length */
/* BRCM_PROP_IE_LEN = sizeof(relmcast_brcm_prop_ie_t)-((sizeof (id) + sizeof (len)))? */
#define RELMCAST_BRCM_PROP_IE_LEN	(sizeof(relmcast_brcm_prop_ie_t)-(2*sizeof(uint8)))

#define RELMCAST_BRCM_PROP_IE_TYPE	55	/* used in prop IE 221 only */

/* BRCM BTC IE */
BWL_PRE_PACKED_STRUCT struct btc_brcm_prop_ie {
	uint8 id;
	uint8 len;
	uint8 oui[3];
	uint8 type;           /* type inidicates what follows */
	uint32 info;
} BWL_POST_PACKED_STRUCT;
typedef struct btc_brcm_prop_ie btc_brcm_prop_ie_t;

#define BTC_INFO_BRCM_PROP_IE_TYPE	90
#define BRCM_BTC_INFO_TYPE_LEN	(sizeof(btc_brcm_prop_ie_t) - (2 * sizeof(uint8)))

/* ************* HT definitions. ************* */
#define MCSSET_LEN	16	/* 16-bits per 8-bit set to give 128-bits bitmap of MCS Index */
#define MAX_MCS_NUM	(128)	/* max mcs number = 128 */
#define BASIC_HT_MCS	0xFFu	/* HT MCS supported rates */

BWL_PRE_PACKED_STRUCT struct ht_cap_ie {
	uint16	cap;
	uint8	params;
	uint8	supp_mcs[MCSSET_LEN];
	uint16	ext_htcap;
	uint32	txbf_cap;
	uint8	as_cap;
} BWL_POST_PACKED_STRUCT;
typedef struct ht_cap_ie ht_cap_ie_t;

BWL_PRE_PACKED_STRUCT struct dot11_ht_cap_ie {
	uint8	id;
	uint8	len;
	ht_cap_ie_t ht_cap;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ht_cap_ie dot11_ht_cap_ie_t;

/* CAP IE: HT 1.0 spec. simply stole a 802.11 IE, we use our prop. IE until this is resolved */
/* the capability IE is primarily used to convey this nodes abilities */
BWL_PRE_PACKED_STRUCT struct ht_prop_cap_ie {
	uint8	id;		/* IE ID, 221, DOT11_MNG_PROPR_ID */
	uint8	len;		/* IE length */
	uint8	oui[3];		/* Proprietary OUI, BRCM_PROP_OUI */
	uint8	type;           /* type indicates what follows */
	ht_cap_ie_t cap_ie;
} BWL_POST_PACKED_STRUCT;
typedef struct ht_prop_cap_ie ht_prop_cap_ie_t;

#define HT_PROP_IE_OVERHEAD	4	/* overhead bytes for prop oui ie */
#define HT_CAP_IE_LEN		26	/* HT capability len (based on .11n d2.0) */
#define HT_CAP_IE_TYPE		51      /* used in prop IE 221 only */

#define HT_CAP_LDPC_CODING	0x0001	/* Support for rx of LDPC coded pkts */
#define HT_CAP_40MHZ		0x0002  /* FALSE:20Mhz, TRUE:20/40MHZ supported */
#define HT_CAP_MIMO_PS_MASK	0x000C  /* Mimo PS mask */
#define HT_CAP_MIMO_PS_SHIFT	0x0002	/* Mimo PS shift */
#define HT_CAP_MIMO_PS_OFF	0x0003	/* Mimo PS, no restriction */
#define HT_CAP_MIMO_PS_RTS	0x0001	/* Mimo PS, send RTS/CTS around MIMO frames */
#define HT_CAP_MIMO_PS_ON	0x0000	/* Mimo PS, MIMO disallowed */
#define HT_CAP_GF		0x0010	/* Greenfield preamble support */
#define HT_CAP_SHORT_GI_20	0x0020	/* 20MHZ short guard interval support */
#define HT_CAP_SHORT_GI_40	0x0040	/* 40Mhz short guard interval support */
#define HT_CAP_TX_STBC		0x0080	/* Tx STBC support */
#define HT_CAP_RX_STBC_MASK	0x0300	/* Rx STBC mask */
#define HT_CAP_RX_STBC_SHIFT	8	/* Rx STBC shift */
#define HT_CAP_DELAYED_BA	0x0400	/* delayed BA support */
#define HT_CAP_MAX_AMSDU	0x0800	/* Max AMSDU size in bytes , 0=3839, 1=7935 */

#define HT_CAP_DSSS_CCK	0x1000	/* DSSS/CCK supported by the BSS */
#define HT_CAP_PSMP		0x2000	/* Power Save Multi Poll support */
#define HT_CAP_40MHZ_INTOLERANT 0x4000	/* 40MHz Intolerant */
#define HT_CAP_LSIG_TXOP	0x8000	/* L-SIG TXOP protection support */

#define HT_CAP_RX_STBC_NO		0x0	/* no rx STBC support */
#define HT_CAP_RX_STBC_ONE_STREAM	0x1	/* rx STBC support of 1 spatial stream */
#define HT_CAP_RX_STBC_TWO_STREAM	0x2	/* rx STBC support of 1-2 spatial streams */
#define HT_CAP_RX_STBC_THREE_STREAM	0x3	/* rx STBC support of 1-3 spatial streams */

#define HT_CAP_TXBF_CAP_IMPLICIT_TXBF_RX	0x1
#define HT_CAP_TXBF_CAP_NDP_RX			0x8
#define HT_CAP_TXBF_CAP_NDP_TX			0x10
#define HT_CAP_TXBF_CAP_EXPLICIT_CSI		0x100
#define HT_CAP_TXBF_CAP_EXPLICIT_NC_STEERING	0x200
#define HT_CAP_TXBF_CAP_EXPLICIT_C_STEERING	0x400
#define HT_CAP_TXBF_CAP_EXPLICIT_CSI_FB_MASK	0x1800
#define HT_CAP_TXBF_CAP_EXPLICIT_CSI_FB_SHIFT	11
#define HT_CAP_TXBF_CAP_EXPLICIT_NC_FB_MASK	0x6000
#define HT_CAP_TXBF_CAP_EXPLICIT_NC_FB_SHIFT	13
#define HT_CAP_TXBF_CAP_EXPLICIT_C_FB_MASK	0x18000
#define HT_CAP_TXBF_CAP_EXPLICIT_C_FB_SHIFT	15
#define HT_CAP_TXBF_CAP_CSI_BFR_ANT_SHIFT	19
#define HT_CAP_TXBF_CAP_NC_BFR_ANT_SHIFT	21
#define HT_CAP_TXBF_CAP_C_BFR_ANT_SHIFT		23
#define HT_CAP_TXBF_CAP_C_BFR_ANT_MASK		0x1800000

#define HT_CAP_TXBF_CAP_CHAN_ESTIM_SHIFT	27
#define HT_CAP_TXBF_CAP_CHAN_ESTIM_MASK		0x18000000

#define HT_CAP_TXBF_FB_TYPE_NONE	0
#define HT_CAP_TXBF_FB_TYPE_DELAYED	1
#define HT_CAP_TXBF_FB_TYPE_IMMEDIATE	2
#define HT_CAP_TXBF_FB_TYPE_BOTH	3

#define HT_CAP_TX_BF_CAP_EXPLICIT_CSI_FB_MASK	0x400
#define HT_CAP_TX_BF_CAP_EXPLICIT_CSI_FB_SHIFT	10
#define HT_CAP_TX_BF_CAP_EXPLICIT_COMPRESSED_FB_MASK 0x18000
#define HT_CAP_TX_BF_CAP_EXPLICIT_COMPRESSED_FB_SHIFT 15

#define HT_CAP_MCS_FLAGS_SUPP_BYTE 12 /* byte offset in HT Cap Supported MCS for various flags */
#define HT_CAP_MCS_RX_8TO15_BYTE_OFFSET                1
#define HT_CAP_MCS_FLAGS_TX_RX_UNEQUAL              0x02
#define HT_CAP_MCS_FLAGS_MAX_SPATIAL_STREAM_MASK    0x0C

#define VHT_MAX_MPDU		11454	/* max mpdu size for now (bytes) */
#define VHT_MPDU_MSDU_DELTA	56		/* Difference in spec - vht mpdu, amsdu len */
/* Max AMSDU len - per spec */
#define VHT_MAX_AMSDU		(VHT_MAX_MPDU - VHT_MPDU_MSDU_DELTA)

#define HT_MAX_AMSDU		7935	/* max amsdu size (bytes) per the HT spec */
#define HT_MIN_AMSDU		3835	/* min amsdu size (bytes) per the HT spec */

#define HT_PARAMS_RX_FACTOR_MASK	0x03	/* ampdu rcv factor mask */
#define HT_PARAMS_DENSITY_MASK		0x1C	/* ampdu density mask */
#define HT_PARAMS_DENSITY_SHIFT	2	/* ampdu density shift */

/* HT/AMPDU specific define */
#define AMPDU_MAX_MPDU_DENSITY  7       /* max mpdu density; in 1/4 usec units */
#define AMPDU_DENSITY_NONE      0       /* No density requirement */
#define AMPDU_DENSITY_1over4_US 1       /* 1/4 us density */
#define AMPDU_DENSITY_1over2_US 2       /* 1/2 us density */
#define AMPDU_DENSITY_1_US      3       /*   1 us density */
#define AMPDU_DENSITY_2_US      4       /*   2 us density */
#define AMPDU_DENSITY_4_US      5       /*   4 us density */
#define AMPDU_DENSITY_8_US      6       /*   8 us density */
#define AMPDU_DENSITY_16_US     7       /*  16 us density */
#define AMPDU_RX_FACTOR_8K      0       /* max rcv ampdu len (8kb) */
#define AMPDU_RX_FACTOR_16K     1       /* max rcv ampdu len (16kb) */
#define AMPDU_RX_FACTOR_32K     2       /* max rcv ampdu len (32kb) */
#define AMPDU_RX_FACTOR_64K     3       /* max rcv ampdu len (64kb) */

/* AMPDU RX factors for VHT rates */
#define AMPDU_RX_FACTOR_128K    4       /* max rcv ampdu len (128kb) */
#define AMPDU_RX_FACTOR_256K    5       /* max rcv ampdu len (256kb) */
#define AMPDU_RX_FACTOR_512K    6       /* max rcv ampdu len (512kb) */
#define AMPDU_RX_FACTOR_1024K   7       /* max rcv ampdu len (1024kb) */

#define AMPDU_RX_FACTOR_BASE    8*1024  /* ampdu factor base for rx len */
#define AMPDU_RX_FACTOR_BASE_PWR	13	/* ampdu factor base for rx len in power of 2 */

#define AMPDU_DELIMITER_LEN	4u	/* length of ampdu delimiter */
#define AMPDU_DELIMITER_LEN_MAX	63	/* max length of ampdu delimiter(enforced in HW) */

#define HT_CAP_EXT_PCO			0x0001
#define HT_CAP_EXT_PCO_TTIME_MASK	0x0006
#define HT_CAP_EXT_PCO_TTIME_SHIFT	1
#define HT_CAP_EXT_MCS_FEEDBACK_MASK	0x0300
#define HT_CAP_EXT_MCS_FEEDBACK_SHIFT	8
#define HT_CAP_EXT_HTC			0x0400
#define HT_CAP_EXT_RD_RESP		0x0800

/** 'ht_add' is called 'HT Operation' information element in the 802.11 standard */
BWL_PRE_PACKED_STRUCT struct ht_add_ie {
	uint8	ctl_ch;			/* control channel number */
	uint8	byte1;			/* ext ch,rec. ch. width, RIFS support */
	uint16	opmode;			/* operation mode */
	uint16	misc_bits;		/* misc bits */
	uint8	basic_mcs[MCSSET_LEN];  /* required MCS set */
} BWL_POST_PACKED_STRUCT;
typedef struct ht_add_ie ht_add_ie_t;

/* ADD IE: HT 1.0 spec. simply stole a 802.11 IE, we use our prop. IE until this is resolved */
/* the additional IE is primarily used to convey the current BSS configuration */
BWL_PRE_PACKED_STRUCT struct ht_prop_add_ie {
	uint8	id;		/* IE ID, 221, DOT11_MNG_PROPR_ID */
	uint8	len;		/* IE length */
	uint8	oui[3];		/* Proprietary OUI, BRCM_PROP_OUI */
	uint8	type;		/* indicates what follows */
	ht_add_ie_t add_ie;
} BWL_POST_PACKED_STRUCT;
typedef struct ht_prop_add_ie ht_prop_add_ie_t;

#define HT_ADD_IE_LEN	22	/* HT capability len (based on .11n d1.0) */
#define HT_ADD_IE_TYPE	52	/* faked out as current spec is illegal */

/* byte1 defn's */
#define HT_BW_ANY		0x04	/* set, STA can use 20 or 40MHz */
#define HT_RIFS_PERMITTED	0x08	/* RIFS allowed */

/* opmode defn's */
#define HT_OPMODE_MASK	        0x0003	/* protection mode mask */
#define HT_OPMODE_SHIFT		0	/* protection mode shift */
#define HT_OPMODE_PURE		0x0000	/* protection mode PURE */
#define HT_OPMODE_OPTIONAL	0x0001	/* protection mode optional */
#define HT_OPMODE_HT20IN40	0x0002	/* protection mode 20MHz HT in 40MHz BSS */
#define HT_OPMODE_MIXED	0x0003	/* protection mode Mixed Mode */
#define HT_OPMODE_NONGF	0x0004	/* protection mode non-GF */
#define DOT11N_TXBURST		0x0008	/* Tx burst limit */
#define DOT11N_OBSS_NONHT	0x0010	/* OBSS Non-HT STA present */
#define HT_OPMODE_CCFS2_MASK	0x1fe0	/* Channel Center Frequency Segment 2 mask */
#define HT_OPMODE_CCFS2_SHIFT	5	/* Channel Center Frequency Segment 2 shift */

/* misc_bites defn's */
#define HT_BASIC_STBC_MCS	0x007f	/* basic STBC MCS */
#define HT_DUAL_STBC_PROT	0x0080	/* Dual STBC Protection */
#define HT_SECOND_BCN		0x0100	/* Secondary beacon support */
#define HT_LSIG_TXOP		0x0200	/* L-SIG TXOP Protection full support */
#define HT_PCO_ACTIVE		0x0400	/* PCO active */
#define HT_PCO_PHASE		0x0800	/* PCO phase */
#define HT_DUALCTS_PROTECTION	0x0080	/* DUAL CTS protection needed */

/* Tx Burst Limits */
#define DOT11N_2G_TXBURST_LIMIT	6160	/* 2G band Tx burst limit per 802.11n Draft 1.10 (usec) */
#define DOT11N_5G_TXBURST_LIMIT	3080	/* 5G band Tx burst limit per 802.11n Draft 1.10 (usec) */

/* Macros for opmode */
#define GET_HT_OPMODE(add_ie)		((ltoh16_ua(&add_ie->opmode) & HT_OPMODE_MASK) \
					>> HT_OPMODE_SHIFT)
#define HT_MIXEDMODE_PRESENT(add_ie)	((ltoh16_ua(&add_ie->opmode) & HT_OPMODE_MASK) \
					== HT_OPMODE_MIXED)	/* mixed mode present */
#define HT_HT20_PRESENT(add_ie)	((ltoh16_ua(&add_ie->opmode) & HT_OPMODE_MASK) \
					== HT_OPMODE_HT20IN40)	/* 20MHz HT present */
#define HT_OPTIONAL_PRESENT(add_ie)	((ltoh16_ua(&add_ie->opmode) & HT_OPMODE_MASK) \
					== HT_OPMODE_OPTIONAL)	/* Optional protection present */
#define HT_USE_PROTECTION(add_ie)	(HT_HT20_PRESENT((add_ie)) || \
					HT_MIXEDMODE_PRESENT((add_ie))) /* use protection */
#define HT_NONGF_PRESENT(add_ie)	((ltoh16_ua(&add_ie->opmode) & HT_OPMODE_NONGF) \
					== HT_OPMODE_NONGF)	/* non-GF present */
#define DOT11N_TXBURST_PRESENT(add_ie)	((ltoh16_ua(&add_ie->opmode) & DOT11N_TXBURST) \
					== DOT11N_TXBURST)	/* Tx Burst present */
#define DOT11N_OBSS_NONHT_PRESENT(add_ie)	((ltoh16_ua(&add_ie->opmode) & DOT11N_OBSS_NONHT) \
					== DOT11N_OBSS_NONHT)	/* OBSS Non-HT present */
#define HT_OPMODE_CCFS2_GET(add_ie)	((ltoh16_ua(&(add_ie)->opmode) & HT_OPMODE_CCFS2_MASK) \
					>> HT_OPMODE_CCFS2_SHIFT)	/* get CCFS2 */
#define HT_OPMODE_CCFS2_SET(add_ie, ccfs2)	do { /* set CCFS2 */ \
	(add_ie)->opmode &= htol16(~HT_OPMODE_CCFS2_MASK); \
	(add_ie)->opmode |= htol16(((ccfs2) << HT_OPMODE_CCFS2_SHIFT) & HT_OPMODE_CCFS2_MASK); \
} while (0)

/* Macros for HT MCS field access */
#define HT_CAP_MCS_BITMASK(supp_mcs)                 \
	((supp_mcs)[HT_CAP_MCS_RX_8TO15_BYTE_OFFSET])
#define HT_CAP_MCS_TX_RX_UNEQUAL(supp_mcs)          \
	((supp_mcs)[HT_CAP_MCS_FLAGS_SUPP_BYTE] & HT_CAP_MCS_FLAGS_TX_RX_UNEQUAL)
#define HT_CAP_MCS_TX_STREAM_SUPPORT(supp_mcs)          \
		((supp_mcs)[HT_CAP_MCS_FLAGS_SUPP_BYTE] & HT_CAP_MCS_FLAGS_MAX_SPATIAL_STREAM_MASK)

BWL_PRE_PACKED_STRUCT struct obss_params {
	uint16	passive_dwell;
	uint16	active_dwell;
	uint16	bss_widthscan_interval;
	uint16	passive_total;
	uint16	active_total;
	uint16	chanwidth_transition_dly;
	uint16	activity_threshold;
} BWL_POST_PACKED_STRUCT;
typedef struct obss_params obss_params_t;

BWL_PRE_PACKED_STRUCT struct dot11_obss_ie {
	uint8	id;
	uint8	len;
	obss_params_t obss_params;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_obss_ie dot11_obss_ie_t;
#define DOT11_OBSS_SCAN_IE_LEN	sizeof(obss_params_t)	/* HT OBSS len (based on 802.11n d3.0) */

/* HT control field */
#define HT_CTRL_LA_TRQ		0x00000002	/* sounding request */
#define HT_CTRL_LA_MAI		0x0000003C	/* MCS request or antenna selection indication */
#define HT_CTRL_LA_MAI_SHIFT	2
#define HT_CTRL_LA_MAI_MRQ	0x00000004	/* MCS request */
#define HT_CTRL_LA_MAI_MSI	0x00000038	/* MCS request sequence identifier */
#define HT_CTRL_LA_MFSI		0x000001C0	/* MFB sequence identifier */
#define HT_CTRL_LA_MFSI_SHIFT	6
#define HT_CTRL_LA_MFB_ASELC	0x0000FE00	/* MCS feedback, antenna selection command/data */
#define HT_CTRL_LA_MFB_ASELC_SH	9
#define HT_CTRL_LA_ASELC_CMD	0x00000C00	/* ASEL command */
#define HT_CTRL_LA_ASELC_DATA	0x0000F000	/* ASEL data */
#define HT_CTRL_CAL_POS		0x00030000	/* Calibration position */
#define HT_CTRL_CAL_SEQ		0x000C0000	/* Calibration sequence */
#define HT_CTRL_CSI_STEERING	0x00C00000	/* CSI/Steering */
#define HT_CTRL_CSI_STEER_SHIFT	22
#define HT_CTRL_CSI_STEER_NFB	0		/* no fedback required */
#define HT_CTRL_CSI_STEER_CSI	1		/* CSI, H matrix */
#define HT_CTRL_CSI_STEER_NCOM	2		/* non-compressed beamforming */
#define HT_CTRL_CSI_STEER_COM	3		/* compressed beamforming */
#define HT_CTRL_NDP_ANNOUNCE	0x01000000	/* NDP announcement */
#define HT_CTRL_AC_CONSTRAINT	0x40000000	/* AC Constraint */
#define HT_CTRL_RDG_MOREPPDU	0x80000000	/* RDG/More PPDU */

/* ************* VHT definitions. ************* */

/**
 * VHT Capabilites IE (sec 8.4.2.160)
 */

BWL_PRE_PACKED_STRUCT struct vht_cap_ie {
	uint32  vht_cap_info;
	/* supported MCS set - 64 bit field */
	uint16	rx_mcs_map;
	uint16  rx_max_rate;
	uint16  tx_mcs_map;
	uint16	tx_max_rate;
} BWL_POST_PACKED_STRUCT;
typedef struct vht_cap_ie vht_cap_ie_t;

/* 4B cap_info + 8B supp_mcs */
#define VHT_CAP_IE_LEN 12

/* VHT Capabilities Info field - 32bit - in VHT Cap IE */
#define VHT_CAP_INFO_MAX_MPDU_LEN_MASK          0x00000003
#define VHT_CAP_INFO_SUPP_CHAN_WIDTH_MASK       0x0000000c
#define VHT_CAP_INFO_LDPC                       0x00000010
#define VHT_CAP_INFO_SGI_80MHZ                  0x00000020
#define VHT_CAP_INFO_SGI_160MHZ                 0x00000040
#define VHT_CAP_INFO_TX_STBC                    0x00000080
#define VHT_CAP_INFO_RX_STBC_MASK               0x00000700
#define VHT_CAP_INFO_RX_STBC_SHIFT              8u
#define VHT_CAP_INFO_SU_BEAMFMR                 0x00000800
#define VHT_CAP_INFO_SU_BEAMFMEE                0x00001000
#define VHT_CAP_INFO_NUM_BMFMR_ANT_MASK         0x0000e000
#define VHT_CAP_INFO_NUM_BMFMR_ANT_SHIFT        13u
#define VHT_CAP_INFO_NUM_SOUNDING_DIM_MASK      0x00070000
#define VHT_CAP_INFO_NUM_SOUNDING_DIM_SHIFT     16u
#define VHT_CAP_INFO_MU_BEAMFMR                 0x00080000
#define VHT_CAP_INFO_MU_BEAMFMEE                0x00100000
#define VHT_CAP_INFO_TXOPPS                     0x00200000
#define VHT_CAP_INFO_HTCVHT                     0x00400000
#define VHT_CAP_INFO_AMPDU_MAXLEN_EXP_MASK      0x03800000
#define VHT_CAP_INFO_AMPDU_MAXLEN_EXP_SHIFT     23u
#define VHT_CAP_INFO_LINK_ADAPT_CAP_MASK        0x0c000000
#define VHT_CAP_INFO_LINK_ADAPT_CAP_SHIFT       26u
#define VHT_CAP_INFO_EXT_NSS_BW_SUP_MASK        0xc0000000
#define VHT_CAP_INFO_EXT_NSS_BW_SUP_SHIFT       30u

/* get Extended NSS BW Support passing vht cap info */
#define VHT_CAP_EXT_NSS_BW_SUP(cap_info) \
	(((cap_info) & VHT_CAP_INFO_EXT_NSS_BW_SUP_MASK) >> VHT_CAP_INFO_EXT_NSS_BW_SUP_SHIFT)

/* VHT CAP INFO extended NSS BW support - refer to IEEE 802.11 REVmc D8.0 Figure 9-559 */
#define VHT_CAP_INFO_EXT_NSS_BW_HALF_160	1 /* 160MHz at half NSS CAP */
#define VHT_CAP_INFO_EXT_NSS_BW_HALF_160_80P80	2 /* 160 & 80p80 MHz at half NSS CAP */

/* VHT Supported MCS Set - 64-bit - in VHT Cap IE */
#define VHT_CAP_SUPP_MCS_RX_HIGHEST_RATE_MASK   0x1fff
#define VHT_CAP_SUPP_MCS_RX_HIGHEST_RATE_SHIFT  0

#define VHT_CAP_SUPP_MCS_TX_HIGHEST_RATE_MASK   0x1fff
#define VHT_CAP_SUPP_MCS_TX_HIGHEST_RATE_SHIFT  0

/* defines for field(s) in vht_cap_ie->rx_max_rate */
#define VHT_CAP_MAX_NSTS_MASK			0xe000
#define VHT_CAP_MAX_NSTS_SHIFT			13

/* defines for field(s) in vht_cap_ie->tx_max_rate */
#define VHT_CAP_EXT_NSS_BW_CAP			0x2000

#define VHT_CAP_MCS_MAP_0_7                     0
#define VHT_CAP_MCS_MAP_0_8                     1
#define VHT_CAP_MCS_MAP_0_9                     2
#define VHT_CAP_MCS_MAP_NONE                    3
#define VHT_CAP_MCS_MAP_S                       2 /* num bits for 1-stream */
#define VHT_CAP_MCS_MAP_M                       0x3 /* mask for 1-stream */
/* assumes VHT_CAP_MCS_MAP_NONE is 3 and 2 bits are used for encoding */
#define VHT_CAP_MCS_MAP_NONE_ALL                0xffff

/* VHT rates bitmap */
#define VHT_CAP_MCS_0_7_RATEMAP		0x00ff
#define VHT_CAP_MCS_0_8_RATEMAP		0x01ff
#define VHT_CAP_MCS_0_9_RATEMAP		0x03ff
#define VHT_CAP_MCS_FULL_RATEMAP	VHT_CAP_MCS_0_9_RATEMAP

#define VHT_PROP_MCS_MAP_10_11                   0
#define VHT_PROP_MCS_MAP_UNUSED1                 1
#define VHT_PROP_MCS_MAP_UNUSED2                 2
#define VHT_PROP_MCS_MAP_NONE                    3
#define VHT_PROP_MCS_MAP_NONE_ALL                0xffff

/* VHT prop rates bitmap */
#define VHT_PROP_MCS_10_11_RATEMAP	0x0c00
#define VHT_PROP_MCS_FULL_RATEMAP	VHT_PROP_MCS_10_11_RATEMAP

#if !defined(VHT_CAP_MCS_MAP_0_9_NSS3)
/* remove after moving define to wlc_rate.h */
/* mcsmap with MCS0-9 for Nss = 3 */
#define VHT_CAP_MCS_MAP_0_9_NSS3 \
	        ((VHT_CAP_MCS_MAP_0_9 << VHT_MCS_MAP_GET_SS_IDX(1)) | \
	         (VHT_CAP_MCS_MAP_0_9 << VHT_MCS_MAP_GET_SS_IDX(2)) | \
	         (VHT_CAP_MCS_MAP_0_9 << VHT_MCS_MAP_GET_SS_IDX(3)))
#endif /* !VHT_CAP_MCS_MAP_0_9_NSS3 */

#define VHT_CAP_MCS_MAP_NSS_MAX                 8

/* get mcsmap with given mcs for given nss streams */
#define VHT_CAP_MCS_MAP_CREATE(mcsmap, nss, mcs) \
	do { \
		int i; \
		for (i = 1; i <= nss; i++) { \
			VHT_MCS_MAP_SET_MCS_PER_SS(i, mcs, mcsmap); \
		} \
	} while (0)

/* Map the mcs code to mcs bit map */
#define VHT_MCS_CODE_TO_MCS_MAP(mcs_code) \
	((mcs_code == VHT_CAP_MCS_MAP_0_7) ? VHT_CAP_MCS_0_7_RATEMAP : \
	 (mcs_code == VHT_CAP_MCS_MAP_0_8) ? VHT_CAP_MCS_0_8_RATEMAP : \
	 (mcs_code == VHT_CAP_MCS_MAP_0_9) ? VHT_CAP_MCS_0_9_RATEMAP : 0)

/* Map the proprietary mcs code to proprietary mcs bitmap */
#define VHT_PROP_MCS_CODE_TO_PROP_MCS_MAP(mcs_code) \
	((mcs_code == VHT_PROP_MCS_MAP_10_11) ? VHT_PROP_MCS_10_11_RATEMAP : 0)

/* Map the mcs bit map to mcs code */
#define VHT_MCS_MAP_TO_MCS_CODE(mcs_map) \
	((mcs_map == VHT_CAP_MCS_0_7_RATEMAP) ? VHT_CAP_MCS_MAP_0_7 : \
	 (mcs_map == VHT_CAP_MCS_0_8_RATEMAP) ? VHT_CAP_MCS_MAP_0_8 : \
	 (mcs_map == VHT_CAP_MCS_0_9_RATEMAP) ? VHT_CAP_MCS_MAP_0_9 : VHT_CAP_MCS_MAP_NONE)

/* Map the proprietary mcs map to proprietary mcs code */
#define VHT_PROP_MCS_MAP_TO_PROP_MCS_CODE(mcs_map) \
	(((mcs_map & 0xc00) == 0xc00)  ? VHT_PROP_MCS_MAP_10_11 : VHT_PROP_MCS_MAP_NONE)

/** VHT Capabilities Supported Channel Width */
typedef enum vht_cap_chan_width {
	VHT_CAP_CHAN_WIDTH_SUPPORT_MANDATORY = 0x00,
	VHT_CAP_CHAN_WIDTH_SUPPORT_160       = 0x04,
	VHT_CAP_CHAN_WIDTH_SUPPORT_160_8080  = 0x08
} vht_cap_chan_width_t;

/** VHT Capabilities Supported max MPDU LEN (sec 8.4.2.160.2) */
typedef enum vht_cap_max_mpdu_len {
	VHT_CAP_MPDU_MAX_4K     = 0x00,
	VHT_CAP_MPDU_MAX_8K     = 0x01,
	VHT_CAP_MPDU_MAX_11K    = 0x02
} vht_cap_max_mpdu_len_t;

/* Maximum MPDU Length byte counts for the VHT Capabilities advertised limits */
#define VHT_MPDU_LIMIT_4K        3895
#define VHT_MPDU_LIMIT_8K        7991
#define VHT_MPDU_LIMIT_11K      11454

/**
 * VHT Operation IE (sec 8.4.2.161)
 */

BWL_PRE_PACKED_STRUCT struct vht_op_ie {
	uint8	chan_width;
	uint8	chan1;
	uint8	chan2;
	uint16	supp_mcs;  /*  same def as above in vht cap */
} BWL_POST_PACKED_STRUCT;
typedef struct vht_op_ie vht_op_ie_t;

/* 3B VHT Op info + 2B Basic MCS */
#define VHT_OP_IE_LEN 5

typedef enum vht_op_chan_width {
	VHT_OP_CHAN_WIDTH_20_40	= 0,
	VHT_OP_CHAN_WIDTH_80	= 1,
	VHT_OP_CHAN_WIDTH_160	= 2, /* deprecated - IEEE 802.11 REVmc D8.0 Table 11-25 */
	VHT_OP_CHAN_WIDTH_80_80	= 3  /* deprecated - IEEE 802.11 REVmc D8.0 Table 11-25 */
} vht_op_chan_width_t;

#define VHT_OP_INFO_LEN		3

/* AID length */
#define AID_IE_LEN		2
/**
 * BRCM vht features IE header
 * The header if the fixed part of the IE
 * On the 5GHz band this is the entire IE,
 * on 2.4GHz the VHT IEs as defined in the 802.11ac
 * specification follows
 *
 *
 * VHT features rates  bitmap.
 * Bit0:		5G MCS 0-9 BW 160MHz
 * Bit1:		5G MCS 0-9 support BW 80MHz
 * Bit2:		5G MCS 0-9 support BW 20MHz
 * Bit3:		2.4G MCS 0-9 support BW 20MHz
 * Bits:4-7	Reserved for future use
 *
 */
#define VHT_FEATURES_IE_TYPE	0x4
BWL_PRE_PACKED_STRUCT struct vht_features_ie_hdr {
	uint8 oui[3];		/* Proprietary OUI, BRCM_PROP_OUI */
	uint8 type;		/* type of this IE = 4 */
	uint8 rate_mask;	/* VHT rate mask */
} BWL_POST_PACKED_STRUCT;
typedef struct vht_features_ie_hdr vht_features_ie_hdr_t;

/* Def for rx & tx basic mcs maps - ea ss num has 2 bits of info */
#define VHT_MCS_MAP_GET_SS_IDX(nss) (((nss)-1) * VHT_CAP_MCS_MAP_S)
#define VHT_MCS_MAP_GET_MCS_PER_SS(nss, mcsMap) \
	(((mcsMap) >> VHT_MCS_MAP_GET_SS_IDX(nss)) & VHT_CAP_MCS_MAP_M)
#define VHT_MCS_MAP_SET_MCS_PER_SS(nss, numMcs, mcsMap) \
	do { \
	 (mcsMap) &= (~(VHT_CAP_MCS_MAP_M << VHT_MCS_MAP_GET_SS_IDX(nss))); \
	 (mcsMap) |= (((numMcs) & VHT_CAP_MCS_MAP_M) << VHT_MCS_MAP_GET_SS_IDX(nss)); \
	} while (0)
#define VHT_MCS_SS_SUPPORTED(nss, mcsMap) \
		 (VHT_MCS_MAP_GET_MCS_PER_SS((nss), (mcsMap)) != VHT_CAP_MCS_MAP_NONE)

/* Get the max ss supported from the mcs map */
#define VHT_MAX_SS_SUPPORTED(mcsMap) \
	VHT_MCS_SS_SUPPORTED(8, mcsMap) ? 8 : \
	VHT_MCS_SS_SUPPORTED(7, mcsMap) ? 7 : \
	VHT_MCS_SS_SUPPORTED(6, mcsMap) ? 6 : \
	VHT_MCS_SS_SUPPORTED(5, mcsMap) ? 5 : \
	VHT_MCS_SS_SUPPORTED(4, mcsMap) ? 4 : \
	VHT_MCS_SS_SUPPORTED(3, mcsMap) ? 3 : \
	VHT_MCS_SS_SUPPORTED(2, mcsMap) ? 2 : \
	VHT_MCS_SS_SUPPORTED(1, mcsMap) ? 1 : 0

#ifdef IBSS_RMC
/* customer's OUI */
#define RMC_PROP_OUI		"\x00\x16\x32"
#endif

/* ************* WPA definitions. ************* */
#define WPA_OUI			"\x00\x50\xF2"	/* WPA OUI */
#define WPA_OUI_LEN		3		/* WPA OUI length */
#define WPA_OUI_TYPE		1
#define WPA_VERSION		1		/* WPA version */
#define WPA_VERSION_LEN 2 /* WPA version length */

/* ************* WPA2 definitions. ************* */
#define WPA2_OUI		"\x00\x0F\xAC"	/* WPA2 OUI */
#define WPA2_OUI_LEN		3		/* WPA2 OUI length */
#define WPA2_VERSION		1		/* WPA2 version */
#define WPA2_VERSION_LEN	2		/* WAP2 version length */
#define MAX_RSNE_SUPPORTED_VERSION  WPA2_VERSION /* Max supported version */

/* ************* WPS definitions. ************* */
#define WPS_OUI			"\x00\x50\xF2"	/* WPS OUI */
#define WPS_OUI_LEN		3		/* WPS OUI length */
#define WPS_OUI_TYPE		4

/* ************* TPC definitions. ************* */
#define TPC_OUI			"\x00\x50\xF2"	/* TPC OUI */
#define TPC_OUI_LEN		3		/* TPC OUI length */
#define TPC_OUI_TYPE		8
#define WFA_OUI_TYPE_TPC	8		/* deprecated */

/* ************* WFA definitions. ************* */
#define WFA_OUI			"\x50\x6F\x9A"  /* WFA OUI */
#define WFA_OUI_LEN		3		/* WFA OUI length */
#define WFA_OUI_TYPE_P2P	9

/* WFA definitions for LEGACY P2P */
#ifdef WL_LEGACY_P2P
#define APPLE_OUI		"\x00\x17\xF2"	/* MACOSX OUI */
#define APPLE_OUI_LEN		3
#define APPLE_OUI_TYPE_P2P	5
#endif /* WL_LEGACY_P2P */

#ifndef WL_LEGACY_P2P
#define P2P_OUI         WFA_OUI
#define P2P_OUI_LEN     WFA_OUI_LEN
#define P2P_OUI_TYPE    WFA_OUI_TYPE_P2P
#else
#define P2P_OUI         APPLE_OUI
#define P2P_OUI_LEN     APPLE_OUI_LEN
#define P2P_OUI_TYPE    APPLE_OUI_TYPE_P2P
#endif /* !WL_LEGACY_P2P */

#ifdef WLTDLS
#define WFA_OUI_TYPE_TPQ	4	/* WFD Tunneled Probe ReQuest */
#define WFA_OUI_TYPE_TPS	5	/* WFD Tunneled Probe ReSponse */
#define WFA_OUI_TYPE_WFD	10
#endif /* WTDLS */
#define WFA_OUI_TYPE_HS20		0x10
#define WFA_OUI_TYPE_OSEN		0x12
#define WFA_OUI_TYPE_NAN		0x13
#define WFA_OUI_TYPE_MBO		0x16
#define WFA_OUI_TYPE_MBO_OCE		0x16
#define WFA_OUI_TYPE_OWE		0x1C
#define WFA_OUI_TYPE_SAE_PK		0x1F
#define WFA_OUI_TYPE_TD_INDICATION	0x20

#define SAE_PK_MOD_LEN		32u
BWL_PRE_PACKED_STRUCT struct dot11_sae_pk_element {
	uint8 id;			/* IE ID, 221, DOT11_MNG_PROPR_ID */
	uint8 len;			/* IE length */
	uint8 oui[WFA_OUI_LEN];		/* WFA_OUI */
	uint8 type;			/* SAE-PK */
	uint8 data[SAE_PK_MOD_LEN];	/* Modifier. 32Byte fixed */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_sae_pk_element dot11_sae_pk_element_t;

/* RSN authenticated key managment suite */
#define RSN_AKM_NONE			0	/* None (IBSS) */
#define RSN_AKM_UNSPECIFIED		1	/* Over 802.1x */
#define RSN_AKM_PSK			2	/* Pre-shared Key */
#define RSN_AKM_FBT_1X			3	/* Fast Bss transition using 802.1X */
#define RSN_AKM_FBT_PSK			4	/* Fast Bss transition using Pre-shared Key */
/* RSN_AKM_MFP_1X and RSN_AKM_MFP_PSK are not used any more
 * Just kept here to avoid build issue in BISON/CARIBOU branch
 */
#define RSN_AKM_MFP_1X			5	/* SHA256 key derivation, using 802.1X */
#define RSN_AKM_MFP_PSK			6	/* SHA256 key derivation, using Pre-shared Key */
#define RSN_AKM_SHA256_1X		5	/* SHA256 key derivation, using 802.1X */
#define RSN_AKM_SHA256_PSK		6	/* SHA256 key derivation, using Pre-shared Key */
#define RSN_AKM_TPK			7	/* TPK(TDLS Peer Key) handshake */
#define RSN_AKM_SAE_PSK			8       /* AKM for SAE with 4-way handshake */
#define RSN_AKM_SAE_FBT			9       /* AKM for SAE with FBT */
#define RSN_AKM_SUITEB_SHA256_1X	11	/* Suite B SHA256 */
#define RSN_AKM_SUITEB_SHA384_1X	12	/* Suite B-192 SHA384 */
#define RSN_AKM_FBT_SHA384_1X		13	/* FBT SHA384 */
#define RSN_AKM_FILS_SHA256		14	/* SHA256 key derivation, using FILS */
#define RSN_AKM_FILS_SHA384		15	/* SHA384 key derivation, using FILS */
#define RSN_AKM_FBT_SHA256_FILS		16
#define RSN_AKM_FBT_SHA384_FILS		17
#define RSN_AKM_OWE			18	/* RFC 8110  OWE */
#define RSN_AKM_FBT_SHA384_PSK		19
#define RSN_AKM_PSK_SHA384		20
/* OSEN authenticated key managment suite */
#define OSEN_AKM_UNSPECIFIED	RSN_AKM_UNSPECIFIED	/* Over 802.1x */
/* WFA DPP RSN authenticated key managment */
#define RSN_AKM_DPP			02u	/* DPP RSN */

/* Key related defines */
#define DOT11_MAX_DEFAULT_KEYS	4	/* number of default keys */
#define DOT11_MAX_IGTK_KEYS		2
#define DOT11_MAX_BIGTK_KEYS		2
#define DOT11_MAX_KEY_SIZE	32	/* max size of any key */
#define DOT11_MAX_IV_SIZE	16	/* max size of any IV */
#define DOT11_EXT_IV_FLAG	(1<<5)	/* flag to indicate IV is > 4 bytes */
#define DOT11_WPA_KEY_RSC_LEN   8       /* WPA RSC key len */

#define WEP1_KEY_SIZE		5	/* max size of any WEP key */
#define WEP1_KEY_HEX_SIZE	10	/* size of WEP key in hex. */
#define WEP128_KEY_SIZE		13	/* max size of any WEP key */
#define WEP128_KEY_HEX_SIZE	26	/* size of WEP key in hex. */
#define TKIP_MIC_SIZE		8	/* size of TKIP MIC */
#define TKIP_EOM_SIZE		7	/* max size of TKIP EOM */
#define TKIP_EOM_FLAG		0x5a	/* TKIP EOM flag byte */
#define TKIP_KEY_SIZE		32	/* size of any TKIP key, includs MIC keys */
#define TKIP_TK_SIZE		16
#define TKIP_MIC_KEY_SIZE	8
#define TKIP_MIC_AUTH_TX	16	/* offset to Authenticator MIC TX key */
#define TKIP_MIC_AUTH_RX	24	/* offset to Authenticator MIC RX key */
#define TKIP_MIC_SUP_RX		TKIP_MIC_AUTH_TX	/* offset to Supplicant MIC RX key */
#define TKIP_MIC_SUP_TX		TKIP_MIC_AUTH_RX	/* offset to Supplicant MIC TX key */
#define AES_KEY_SIZE		16	/* size of AES key */
#define AES_MIC_SIZE		8	/* size of AES MIC */
#define BIP_KEY_SIZE		16	/* size of BIP key */
#define BIP_MIC_SIZE		8   /* sizeof BIP MIC */

#define AES_GCM_MIC_SIZE	16	/* size of MIC for 128-bit GCM - .11adD9 */

#define AES256_KEY_SIZE		32	/* size of AES 256 key - .11acD5 */
#define AES256_MIC_SIZE		16	/* size of MIC for 256 bit keys, incl BIP */

/* WCN */
#define WCN_OUI			"\x00\x50\xf2"	/* WCN OUI */
#define WCN_TYPE		4	/* WCN type */

#ifdef BCMWAPI_WPI
#define SMS4_KEY_LEN		16
#define SMS4_WPI_CBC_MAC_LEN	16
#endif

/* 802.11r protocol definitions */

/** Mobility Domain IE */
BWL_PRE_PACKED_STRUCT struct dot11_mdid_ie {
	uint8 id;
	uint8 len;		/* DOT11_MDID_IE_DATA_LEN (3) */
	uint16 mdid;		/* Mobility Domain Id */
	uint8 cap;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_mdid_ie dot11_mdid_ie_t;

/* length of data portion of Mobility Domain IE */
#define DOT11_MDID_IE_DATA_LEN	3
#define DOT11_MDID_LEN		2
#define FBT_MDID_CAP_OVERDS	0x01	/* Fast Bss transition over the DS support */
#define FBT_MDID_CAP_RRP	0x02	/* Resource request protocol support */

/* BITs in FTIE mic control field */
#define DOT11_FTIE_RSNXE_USED	0x1u

/* Fast Bss Transition IE */
#ifdef FT_IE_VER_V2
typedef BWL_PRE_PACKED_STRUCT struct dot11_ft_ie_v2 {
	uint8 id;
	uint8 len;
	uint16 mic_control;
	/* dynamic offset to following mic[], anonce[], snonce[] */
} BWL_POST_PACKED_STRUCT dot11_ft_ie_v2;
typedef struct dot11_ft_ie_v2 dot11_ft_ie_t;
#else
BWL_PRE_PACKED_STRUCT struct dot11_ft_ie {
	uint8 id;
	uint8 len;			/* At least equal to DOT11_FT_IE_FIXED_LEN (82) */
	uint16 mic_control;		/* Mic Control */
	uint8 mic[16];
	uint8 anonce[32];
	uint8 snonce[32];
	/* Optional sub-elements follow */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ft_ie dot11_ft_ie_t;

/* Fixed length of data portion of Fast BSS Transition IE. There could be
 * optional parameters, which if present, could raise the FT IE length to 255.
 */
#define DOT11_FT_IE_FIXED_LEN	82
#endif /* FT_IE_VER_V2 */

#ifdef FT_IE_VER_V2
#define DOT11_FT_IE_LEN(mic_len) (sizeof(dot11_ft_ie_v2) + mic_len + EAPOL_WPA_KEY_NONCE_LEN *2)
#define FT_IE_MIC(pos) ((uint8 *)pos + sizeof(dot11_ft_ie_v2))
#define FT_IE_ANONCE(pos, mic_len) ((uint8 *)pos + sizeof(dot11_ft_ie_v2) + mic_len)
#define FT_IE_SNONCE(pos, mic_len) ((uint8 *)pos + sizeof(dot11_ft_ie_v2) + mic_len + \
	EAPOL_WPA_KEY_NONCE_LEN)
#else
#define DOT11_FT_IE_LEN(mic_len) sizeof(dot11_ft_ie)
#define FT_IE_MIC(pos) ((uint8 *)&pos->mic)
#define FT_IE_ANONCE(pos, mic_len) ((uint8 *)&pos->anonce)
#define FT_IE_SNONCE(pos, mic_len) ((uint8 *)&pos->snonce)
#endif /* FT_IE_VER_V2 */
#define TIE_TYPE_RESERVED		0
#define TIE_TYPE_REASSOC_DEADLINE	1
#define TIE_TYPE_KEY_LIEFTIME		2
#define TIE_TYPE_ASSOC_COMEBACK		3
BWL_PRE_PACKED_STRUCT struct dot11_timeout_ie {
	uint8 id;
	uint8 len;
	uint8 type;		/* timeout interval type */
	uint32 value;		/* timeout interval value */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_timeout_ie dot11_timeout_ie_t;

/** GTK ie */
BWL_PRE_PACKED_STRUCT struct dot11_gtk_ie {
	uint8 id;
	uint8 len;
	uint16 key_info;
	uint8 key_len;
	uint8 rsc[8];
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_gtk_ie dot11_gtk_ie_t;

/** Management MIC ie */
BWL_PRE_PACKED_STRUCT struct mmic_ie {
	uint8   id;					/* IE ID: DOT11_MNG_MMIE_ID */
	uint8   len;				/* IE length */
	uint16  key_id;				/* key id */
	uint8   ipn[6];				/* ipn */
	uint8   mic[16];			/* mic */
} BWL_POST_PACKED_STRUCT;
typedef struct mmic_ie mmic_ie_t;

#define DOT11_MMIC_IE_HDR_SIZE (OFFSETOF(mmic_ie_t, mic))

/* 802.11r-2008, 11A.10.3 - RRB frame format */
BWL_PRE_PACKED_STRUCT struct dot11_ft_rrb_frame {
	uint8  frame_type; /* 1 for RRB */
	uint8  packet_type; /* 0 for Request 1 for Response */
	uint16 len;
	uint8  cur_ap_addr[ETHER_ADDR_LEN];
	uint8  data[1];	/* IEs Received/Sent in FT Action Req/Resp Frame */
} BWL_POST_PACKED_STRUCT;

typedef struct dot11_ft_rrb_frame dot11_ft_rrb_frame_t;

#define DOT11_FT_RRB_FIXED_LEN 10
#define DOT11_FT_REMOTE_FRAME_TYPE 1
#define DOT11_FT_PACKET_REQ 0
#define DOT11_FT_PACKET_RESP 1

#define BSSID_INVALID           "\x00\x00\x00\x00\x00\x00"
#define BSSID_BROADCAST         "\xFF\xFF\xFF\xFF\xFF\xFF"

#ifdef BCMWAPI_WAI
#define WAPI_IE_MIN_LEN		20	/* WAPI IE min length */
#define WAPI_VERSION		1	/* WAPI version */
#define WAPI_VERSION_LEN	2	/* WAPI version length */
#define WAPI_OUI		"\x00\x14\x72"	/* WAPI OUI */
#define WAPI_OUI_LEN		DOT11_OUI_LEN	/* WAPI OUI length */
#endif /* BCMWAPI_WAI */

/* ************* WMM Parameter definitions. ************* */
#define WMM_OUI			"\x00\x50\xF2"	/* WNN OUI */
#define WMM_OUI_LEN		3		/* WMM OUI length */
#define WMM_OUI_TYPE	2		/* WMM OUT type */
#define WMM_VERSION		1
#define WMM_VERSION_LEN	1

/* WMM OUI subtype */
#define WMM_OUI_SUBTYPE_PARAMETER	1
#define WMM_PARAMETER_IE_LEN		24

/** Link Identifier Element */
BWL_PRE_PACKED_STRUCT struct link_id_ie {
	uint8 id;
	uint8 len;
	struct ether_addr	bssid;
	struct ether_addr	tdls_init_mac;
	struct ether_addr	tdls_resp_mac;
} BWL_POST_PACKED_STRUCT;
typedef struct link_id_ie link_id_ie_t;
#define TDLS_LINK_ID_IE_LEN		18u

/** Link Wakeup Schedule Element */
BWL_PRE_PACKED_STRUCT struct wakeup_sch_ie {
	uint8 id;
	uint8 len;
	uint32 offset;			/* in ms between TSF0 and start of 1st Awake Window */
	uint32 interval;		/* in ms bwtween the start of 2 Awake Windows */
	uint32 awake_win_slots;	/* in backof slots, duration of Awake Window */
	uint32 max_wake_win;	/* in ms, max duration of Awake Window */
	uint16 idle_cnt;		/* number of consecutive Awake Windows */
} BWL_POST_PACKED_STRUCT;
typedef struct wakeup_sch_ie wakeup_sch_ie_t;
#define TDLS_WAKEUP_SCH_IE_LEN		18

/** Channel Switch Timing Element */
BWL_PRE_PACKED_STRUCT struct channel_switch_timing_ie {
	uint8 id;
	uint8 len;
	uint16 switch_time;		/* in ms, time to switch channels */
	uint16 switch_timeout;	/* in ms */
} BWL_POST_PACKED_STRUCT;
typedef struct channel_switch_timing_ie channel_switch_timing_ie_t;
#define TDLS_CHANNEL_SWITCH_TIMING_IE_LEN		4

/** PTI Control Element */
BWL_PRE_PACKED_STRUCT struct pti_control_ie {
	uint8 id;
	uint8 len;
	uint8 tid;
	uint16 seq_control;
} BWL_POST_PACKED_STRUCT;
typedef struct pti_control_ie pti_control_ie_t;
#define TDLS_PTI_CONTROL_IE_LEN		3

/** PU Buffer Status Element */
BWL_PRE_PACKED_STRUCT struct pu_buffer_status_ie {
	uint8 id;
	uint8 len;
	uint8 status;
} BWL_POST_PACKED_STRUCT;
typedef struct pu_buffer_status_ie pu_buffer_status_ie_t;
#define TDLS_PU_BUFFER_STATUS_IE_LEN	1
#define TDLS_PU_BUFFER_STATUS_AC_BK		1
#define TDLS_PU_BUFFER_STATUS_AC_BE		2
#define TDLS_PU_BUFFER_STATUS_AC_VI		4
#define TDLS_PU_BUFFER_STATUS_AC_VO		8

/* TDLS Action Field Values */
#define TDLS_SETUP_REQ				0
#define TDLS_SETUP_RESP				1
#define TDLS_SETUP_CONFIRM			2
#define TDLS_TEARDOWN				3
#define TDLS_PEER_TRAFFIC_IND			4
#define TDLS_CHANNEL_SWITCH_REQ			5
#define TDLS_CHANNEL_SWITCH_RESP		6
#define TDLS_PEER_PSM_REQ			7
#define TDLS_PEER_PSM_RESP			8
#define TDLS_PEER_TRAFFIC_RESP			9
#define TDLS_DISCOVERY_REQ			10

/* 802.11z TDLS Public Action Frame action field */
#define TDLS_DISCOVERY_RESP			14

/* 802.11u GAS action frames */
#define GAS_REQUEST_ACTION_FRAME				10
#define GAS_RESPONSE_ACTION_FRAME				11
#define GAS_COMEBACK_REQUEST_ACTION_FRAME		12
#define GAS_COMEBACK_RESPONSE_ACTION_FRAME		13

/* FTM - fine timing measurement public action frames */
BWL_PRE_PACKED_STRUCT struct dot11_ftm_req {
	uint8 category;				/* category of action frame (4) */
	uint8 action;				/* public action (32) */
	uint8 trigger;				/* trigger/continue? */
	/* optional lci, civic loc, ftm params */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ftm_req dot11_ftm_req_t;

BWL_PRE_PACKED_STRUCT struct dot11_ftm {
	uint8 category;				/* category of action frame (4) */
	uint8 action;				/* public action (33) */
	uint8 dialog;				/* dialog token */
	uint8 follow_up;			/* follow up dialog token */
	uint8 tod[6];				/* t1 - last depart timestamp */
	uint8 toa[6];				/* t4 - last ack arrival timestamp */
	uint8 tod_err[2];			/* t1 error */
	uint8 toa_err[2];			/* t4 error */
	/* optional lci report, civic loc report, ftm params */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ftm dot11_ftm_t;

BWL_PRE_PACKED_STRUCT struct dot11_ftm_lmr {
	uint8    category;          /* category of action frame (4) */
	uint8    action;            /* public action (33) */
	uint8    dialog;            /* dialog token */
	uint8    tod[6];            /* RSTA t3 or ISTA t1:
	                             * last departure of NDP
	                             */
	uint8    toa[6];            /* RSTA t2 or ISTA t4:
	                             * last arrival of NDP
	                             */
	uint8    tod_err[2];        /* t3 or t1 error */
	uint8    toa_err[2];        /* t2 or t4 error */
	uint16   cfo;               /* I2R LMR: clock difference between ISTA and RSTA. */
	uint8    sec_ltf_params[];  /* Optional Secure LTF parameters */
	/* no AOA feedback */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ftm_lmr dot11_ftm_lmr_t;

BWL_PRE_PACKED_STRUCT struct dot11_ftm_ranging_ndpa {
	uint16			fc;		/* frame control */
	uint16			durid;		/* duration/ID */
	struct ether_addr	ra;		/* receiver address */
	struct ether_addr	ta;		/* transmitter address */
	uint8           dialog_token; /* sounding dialog token */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ftm_ranging_ndpa dot11_ftm_ranging_ndpa_t;

/* NDPA types = dialog token byte lower 2 bits */
#define DOT11_NDPA_TYPE_MASK     0x03
#define DOT11_NDPA_TYPE_VHT      0x00
#define DOT11_NDPA_TYPE_RANGING  0x01
#define DOT11_NDPA_TYPE_HE       0x02

#define DOT11_FTM_ERR_NOT_CONT_OFFSET 1
#define DOT11_FTM_ERR_NOT_CONT_MASK 0x80
#define DOT11_FTM_ERR_NOT_CONT_SHIFT 7
#define DOT11_FTM_ERR_NOT_CONT(_err) (((_err)[DOT11_FTM_ERR_NOT_CONT_OFFSET] & \
	DOT11_FTM_ERR_NOT_CONT_MASK) >> DOT11_FTM_ERR_NOT_CONT_SHIFT)
#define DOT11_FTM_ERR_SET_NOT_CONT(_err, _val) do {\
	uint8 _err2 = (_err)[DOT11_FTM_ERR_NOT_CONT_OFFSET]; \
	_err2 &= ~DOT11_FTM_ERR_NOT_CONT_MASK; \
	_err2 |= ((_val) << DOT11_FTM_ERR_NOT_CONT_SHIFT) & DOT11_FTM_ERR_NOT_CONT_MASK; \
	(_err)[DOT11_FTM_ERR_NOT_CONT_OFFSET] = _err2; \
} while (0)

#define DOT11_FTM_ERR_MAX_ERR_OFFSET 0
#define DOT11_FTM_ERR_MAX_ERR_MASK 0x7fff
#define DOT11_FTM_ERR_MAX_ERR_SHIFT 0
#define DOT11_FTM_ERR_MAX_ERR(_err) (((((_err)[1] & 0x7f) << 8) | (_err)[0]))
#define DOT11_FTM_ERR_SET_MAX_ERR(_err, _val) do {\
	uint16 _val2; \
	uint16 _not_cont; \
	_val2 =  (((_val) & DOT11_FTM_ERR_MAX_ERR_MASK) << DOT11_FTM_ERR_MAX_ERR_SHIFT); \
	_val2 = (_val2 > 0x3fff) ? 0 : _val2; /* not expecting > 16ns error */ \
	_not_cont = DOT11_FTM_ERR_NOT_CONT(_err); \
	(_err)[0] = _val2 & 0xff; \
	(_err)[1] = (_val2 >> 8) & 0xff; \
	DOT11_FTM_ERR_SET_NOT_CONT(_err, _not_cont); \
} while (0)

#if defined(DOT11_FTM_ERR_ROM_COMPAT)
/* incorrect defs - here for ROM compatibiity */
#undef DOT11_FTM_ERR_NOT_CONT_OFFSET
#undef DOT11_FTM_ERR_NOT_CONT_MASK
#undef DOT11_FTM_ERR_NOT_CONT_SHIFT
#undef DOT11_FTM_ERR_NOT_CONT
#undef DOT11_FTM_ERR_SET_NOT_CONT

#define DOT11_FTM_ERR_NOT_CONT_OFFSET 0
#define DOT11_FTM_ERR_NOT_CONT_MASK 0x0001
#define DOT11_FTM_ERR_NOT_CONT_SHIFT 0
#define DOT11_FTM_ERR_NOT_CONT(_err) (((_err)[DOT11_FTM_ERR_NOT_CONT_OFFSET] & \
	DOT11_FTM_ERR_NOT_CONT_MASK) >> DOT11_FTM_ERR_NOT_CONT_SHIFT)
#define DOT11_FTM_ERR_SET_NOT_CONT(_err, _val) do {\
	uint8 _err2 = (_err)[DOT11_FTM_ERR_NOT_CONT_OFFSET]; \
	_err2 &= ~DOT11_FTM_ERR_NOT_CONT_MASK; \
	_err2 |= ((_val) << DOT11_FTM_ERR_NOT_CONT_SHIFT) & DOT11_FTM_ERR_NOT_CONT_MASK; \
	(_err)[DOT11_FTM_ERR_NOT_CONT_OFFSET] = _err2; \
} while (0)

#undef DOT11_FTM_ERR_MAX_ERR_OFFSET
#undef DOT11_FTM_ERR_MAX_ERR_MASK
#undef DOT11_FTM_ERR_MAX_ERR_SHIFT
#undef DOT11_FTM_ERR_MAX_ERR
#undef DOT11_FTM_ERR_SET_MAX_ERR

#define DOT11_FTM_ERR_MAX_ERR_OFFSET 0
#define DOT11_FTM_ERR_MAX_ERR_MASK 0xfff7
#define DOT11_FTM_ERR_MAX_ERR_SHIFT 1
#define DOT11_FTM_ERR_MAX_ERR(_err) ((((_err)[1] << 7) | (_err)[0]) >> 1)
#define DOT11_FTM_ERR_SET_MAX_ERR(_err, _val) do {\
	uint16 _val2; \
	_val2 =  (((_val) << DOT11_FTM_ERR_MAX_ERR_SHIFT) |\
		 ((_err)[DOT11_FTM_ERR_NOT_CONT_OFFSET] & DOT11_FTM_ERR_NOT_CONT_MASK)); \
	(_err)[0] = _val2 & 0xff; \
	(_err)[1] = _val2 >> 8 & 0xff; \
} while (0)
#endif /* DOT11_FTM_ERR_ROM_COMPAT */

BWL_PRE_PACKED_STRUCT struct dot11_ftm_params {
	uint8 id;		/* DOT11_MNG_FTM_PARAM_ID 8.4.2.166 11mcd2.6/2014 - revisit */
	uint8 len;
	uint8 info[9];
} BWL_POST_PACKED_STRUCT;

typedef struct dot11_ftm_params dot11_ftm_params_t;
#define DOT11_FTM_PARAMS_IE_LEN (sizeof(dot11_ftm_params_t) - 2)

/* common part for both TB and NTB */
BWL_PRE_PACKED_STRUCT struct dot11_ftm_ranging_params {
	uint8 id; /* 255 */
	uint8 len;
	uint8 ext_id; /* DOT11_MNG_FTM_RANGING_EXT_ID */
	uint8 info[6];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ftm_ranging_params dot11_ftm_ranging_params_t;
#define DOT11_FTM_CMN_RANGING_PARAMS_IE_LEN (sizeof(dot11_ftm_ranging_params_t) - TLV_EXT_HDR_LEN)

/* FTM NTB specific */
BWL_PRE_PACKED_STRUCT struct dot11_ftm_ntb_params {
	uint8 id; /* DOT11_FTM_NTB_SUB_ELT_ID */
	uint8 len;
	uint8 info[6];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ftm_ntb_params dot11_ftm_ntb_params_t;

#define DOT11_FTM_NTB_PARAMS_SUB_IE_LEN (sizeof(dot11_ftm_ntb_params_t))
#define DOT11_FTM_NTB_PARAMS_IE_LEN DOT11_FTM_CMN_RANGING_PARAMS_IE_LEN + \
	DOT11_FTM_NTB_PARAMS_SUB_IE_LEN

/* FTM TB specific */
BWL_PRE_PACKED_STRUCT struct dot11_ftm_tb_params {
	uint8 id; /* DOT11_FTM_TB_SUB_ELT_ID */
	uint8 len;
	uint8 info[1]; /* variable length, minimum 1 */
} BWL_POST_PACKED_STRUCT;

typedef struct dot11_ftm_tb_params dot11_ftm_tb_params_t;
#define DOT11_FTM_TB_PARAMS_IE_LEN sizeof(dot11_ftm_tb_params_t)

BWL_PRE_PACKED_STRUCT struct dot11_ftm_sec_ltf_params {
	uint8 id; /* 255 */
	uint8 len;
	uint8 ext_id; /* DOT11_MNG_FTM_SECURE_LTF_EXT_ID */
	uint8 info[11];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ftm_sec_ltf_params dot11_ftm_sec_ltf_params_t;
#define DOT11_FTM_SEC_LTF_PARAMS_IE_LEN (sizeof(dot11_ftm_sec_ltf_params_t) - 3)

#define FTM_PARAMS_FIELD(_p, _off, _mask, _shift) (((_p)->info[(_off)] & (_mask)) >> (_shift))
#define FTM_PARAMS_SET_FIELD(_p, _off, _mask, _shift, _val) do {\
	uint8 _ptmp = (_p)->info[_off] & ~(_mask); \
	(_p)->info[(_off)] = _ptmp | (((_val) << (_shift)) & (_mask)); \
} while (0)

#define FTM_PARAMS_STATUS_OFFSET 0
#define FTM_PARAMS_STATUS_MASK 0x03
#define FTM_PARAMS_STATUS_SHIFT 0
#define FTM_PARAMS_STATUS(_p) FTM_PARAMS_FIELD(_p, FTM_PARAMS_STATUS_OFFSET, \
	FTM_PARAMS_STATUS_MASK, FTM_PARAMS_STATUS_SHIFT)
#define FTM_PARAMS_SET_STATUS(_p, _status) FTM_PARAMS_SET_FIELD(_p, \
	FTM_PARAMS_STATUS_OFFSET, FTM_PARAMS_STATUS_MASK, FTM_PARAMS_STATUS_SHIFT, _status)

#define FTM_PARAMS_VALUE_OFFSET 0
#define FTM_PARAMS_VALUE_MASK 0x7c
#define FTM_PARAMS_VALUE_SHIFT 2
#define FTM_PARAMS_VALUE(_p) FTM_PARAMS_FIELD(_p, FTM_PARAMS_VALUE_OFFSET, \
	FTM_PARAMS_VALUE_MASK, FTM_PARAMS_VALUE_SHIFT)
#define FTM_PARAMS_SET_VALUE(_p, _value) FTM_PARAMS_SET_FIELD(_p, \
	FTM_PARAMS_VALUE_OFFSET, FTM_PARAMS_VALUE_MASK, FTM_PARAMS_VALUE_SHIFT, _value)
#define FTM_PARAMS_MAX_VALUE 32

#define FTM_PARAMS_NBURSTEXP_OFFSET 1
#define FTM_PARAMS_NBURSTEXP_MASK 0x0f
#define FTM_PARAMS_NBURSTEXP_SHIFT 0
#define FTM_PARAMS_NBURSTEXP(_p) FTM_PARAMS_FIELD(_p, FTM_PARAMS_NBURSTEXP_OFFSET, \
	FTM_PARAMS_NBURSTEXP_MASK, FTM_PARAMS_NBURSTEXP_SHIFT)
#define FTM_PARAMS_SET_NBURSTEXP(_p, _bexp) FTM_PARAMS_SET_FIELD(_p, \
	FTM_PARAMS_NBURSTEXP_OFFSET, FTM_PARAMS_NBURSTEXP_MASK, FTM_PARAMS_NBURSTEXP_SHIFT, \
	_bexp)

#define FTM_PARAMS_NBURST(_p) (1 << FTM_PARAMS_NBURSTEXP(_p))

enum {
	FTM_PARAMS_NBURSTEXP_NOPREF = 15
};

enum {
	FTM_PARAMS_BURSTTMO_NOPREF = 15
};

#define FTM_PARAMS_BURSTTMO_OFFSET 1
#define FTM_PARAMS_BURSTTMO_MASK 0xf0
#define FTM_PARAMS_BURSTTMO_SHIFT 4
#define FTM_PARAMS_BURSTTMO(_p) FTM_PARAMS_FIELD(_p, FTM_PARAMS_BURSTTMO_OFFSET, \
	FTM_PARAMS_BURSTTMO_MASK, FTM_PARAMS_BURSTTMO_SHIFT)
/* set timeout in params using _tmo where timeout = 2^(_tmo) * 250us */
#define FTM_PARAMS_SET_BURSTTMO(_p, _tmo) FTM_PARAMS_SET_FIELD(_p, \
	FTM_PARAMS_BURSTTMO_OFFSET, FTM_PARAMS_BURSTTMO_MASK, FTM_PARAMS_BURSTTMO_SHIFT, (_tmo)+2)

#define FTM_PARAMS_BURSTTMO_USEC(_val) ((1 << ((_val)-2)) * 250)
#define FTM_PARAMS_BURSTTMO_VALID(_val) ((((_val) < 12 && (_val) > 1)) || \
	(_val) == FTM_PARAMS_BURSTTMO_NOPREF)
#define FTM_PARAMS_BURSTTMO_MAX_MSEC 128 /* 2^9 * 250us */
#define FTM_PARAMS_BURSTTMO_MAX_USEC 128000 /* 2^9 * 250us */

#define FTM_PARAMS_MINDELTA_OFFSET 2
#define FTM_PARAMS_MINDELTA_USEC(_p) ((_p)->info[FTM_PARAMS_MINDELTA_OFFSET] * 100)
#define FTM_PARAMS_SET_MINDELTA_USEC(_p, _delta) do { \
	(_p)->info[FTM_PARAMS_MINDELTA_OFFSET] = (_delta) / 100; \
} while (0)

enum {
	FTM_PARAMS_MINDELTA_NOPREF = 0
};

#define FTM_PARAMS_PARTIAL_TSF(_p) ((_p)->info[4] << 8 | (_p)->info[3])
#define FTM_PARAMS_SET_PARTIAL_TSF(_p, _partial_tsf) do { \
	(_p)->info[3] = (_partial_tsf) & 0xff; \
	(_p)->info[4] = ((_partial_tsf) >> 8) & 0xff; \
} while (0)

#define FTM_PARAMS_PARTIAL_TSF_MASK 0x0000000003fffc00ULL
#define FTM_PARAMS_PARTIAL_TSF_SHIFT 10
#define FTM_PARAMS_PARTIAL_TSF_BIT_LEN 16
#define FTM_PARAMS_PARTIAL_TSF_MAX 0xffff

/* FTM can indicate upto 62k TUs forward and 1k TU backward */
#define FTM_PARAMS_TSF_FW_HI (63487 << 10)	/* in micro sec */
#define FTM_PARAMS_TSF_BW_LOW (64512 << 10)	/* in micro sec */
#define FTM_PARAMS_TSF_BW_HI (65535 << 10)	/* in micro sec */
#define FTM_PARAMS_TSF_FW_MAX FTM_PARAMS_TSF_FW_HI
#define FTM_PARAMS_TSF_BW_MAX (FTM_PARAMS_TSF_BW_HI - FTM_PARAMS_TSF_BW_LOW)

#define FTM_PARAMS_PTSFNOPREF_OFFSET 5
#define FTM_PARAMS_PTSFNOPREF_MASK 0x1
#define FTM_PARAMS_PTSFNOPREF_SHIFT 0
#define FTM_PARAMS_PTSFNOPREF(_p) FTM_PARAMS_FIELD(_p, FTM_PARAMS_PTSFNOPREF_OFFSET, \
	FTM_PARAMS_PTSFNOPREF_MASK, FTM_PARAMS_PTSFNOPREF_SHIFT)
#define FTM_PARAMS_SET_PTSFNOPREF(_p, _nopref) FTM_PARAMS_SET_FIELD(_p, \
	FTM_PARAMS_PTSFNOPREF_OFFSET, FTM_PARAMS_PTSFNOPREF_MASK, \
	FTM_PARAMS_PTSFNOPREF_SHIFT, _nopref)

#define FTM_PARAMS_ASAP_OFFSET 5
#define FTM_PARAMS_ASAP_MASK 0x4
#define FTM_PARAMS_ASAP_SHIFT 2
#define FTM_PARAMS_ASAP(_p) FTM_PARAMS_FIELD(_p, FTM_PARAMS_ASAP_OFFSET, \
	FTM_PARAMS_ASAP_MASK, FTM_PARAMS_ASAP_SHIFT)
#define FTM_PARAMS_SET_ASAP(_p, _asap) FTM_PARAMS_SET_FIELD(_p, \
	FTM_PARAMS_ASAP_OFFSET, FTM_PARAMS_ASAP_MASK, FTM_PARAMS_ASAP_SHIFT, _asap)

/* FTM1 - AKA ASAP Capable */
#define FTM_PARAMS_FTM1_OFFSET 5
#define FTM_PARAMS_FTM1_MASK 0x02
#define FTM_PARAMS_FTM1_SHIFT 1
#define FTM_PARAMS_FTM1(_p) FTM_PARAMS_FIELD(_p, FTM_PARAMS_FTM1_OFFSET, \
	FTM_PARAMS_FTM1_MASK, FTM_PARAMS_FTM1_SHIFT)
#define FTM_PARAMS_SET_FTM1(_p, _ftm1) FTM_PARAMS_SET_FIELD(_p, \
	FTM_PARAMS_FTM1_OFFSET, FTM_PARAMS_FTM1_MASK, FTM_PARAMS_FTM1_SHIFT, _ftm1)

#define FTM_PARAMS_FTMS_PER_BURST_OFFSET 5
#define FTM_PARAMS_FTMS_PER_BURST_MASK 0xf8
#define FTM_PARAMS_FTMS_PER_BURST_SHIFT 3
#define FTM_PARAMS_FTMS_PER_BURST(_p) FTM_PARAMS_FIELD(_p, FTM_PARAMS_FTMS_PER_BURST_OFFSET, \
	FTM_PARAMS_FTMS_PER_BURST_MASK, FTM_PARAMS_FTMS_PER_BURST_SHIFT)
#define FTM_PARAMS_SET_FTMS_PER_BURST(_p, _nftms) FTM_PARAMS_SET_FIELD(_p, \
	FTM_PARAMS_FTMS_PER_BURST_OFFSET, FTM_PARAMS_FTMS_PER_BURST_MASK, \
	FTM_PARAMS_FTMS_PER_BURST_SHIFT, _nftms)

enum {
	FTM_PARAMS_FTMS_PER_BURST_NOPREF = 0
};

#define FTM_PARAMS_CHAN_INFO_OFFSET 6
#define FTM_PARAMS_CHAN_INFO_MASK 0xfc
#define FTM_PARAMS_CHAN_INFO_SHIFT 2
#define FTM_PARAMS_CHAN_INFO(_p) FTM_PARAMS_FIELD(_p, FTM_PARAMS_CHAN_INFO_OFFSET, \
	FTM_PARAMS_CHAN_INFO_MASK, FTM_PARAMS_CHAN_INFO_SHIFT)
#define FTM_PARAMS_SET_CHAN_INFO(_p, _ci) FTM_PARAMS_SET_FIELD(_p, \
	FTM_PARAMS_CHAN_INFO_OFFSET, FTM_PARAMS_CHAN_INFO_MASK, FTM_PARAMS_CHAN_INFO_SHIFT, _ci)

/* burst period - units of 100ms */
#define FTM_PARAMS_BURST_PERIOD(_p) (((_p)->info[8] << 8) | (_p)->info[7])
#define FTM_PARAMS_SET_BURST_PERIOD(_p, _bp) do {\
	(_p)->info[7] = (_bp) & 0xff; \
	(_p)->info[8] = ((_bp) >> 8) & 0xff; \
} while (0)

#define FTM_PARAMS_BURST_PERIOD_MS(_p) (FTM_PARAMS_BURST_PERIOD(_p) * 100)

enum {
	FTM_PARAMS_BURST_PERIOD_NOPREF = 0
};

/* FTM status values - last updated from 11mcD4.0 */
enum {
	FTM_PARAMS_STATUS_RESERVED	= 0,
	FTM_PARAMS_STATUS_SUCCESSFUL = 1,
	FTM_PARAMS_STATUS_INCAPABLE = 2,
	FTM_PARAMS_STATUS_FAILED = 3,
	/* Below are obsolte */
	FTM_PARAMS_STATUS_OVERRIDDEN = 4,
	FTM_PARAMS_STATUS_ASAP_INCAPABLE = 5,
	FTM_PARAMS_STATUS_ASAP_FAILED = 6,
	/* rest are reserved */
};

enum {
	FTM_PARAMS_CHAN_INFO_NO_PREF		= 0,
	FTM_PARAMS_CHAN_INFO_RESERVE1		= 1,
	FTM_PARAMS_CHAN_INFO_RESERVE2		= 2,
	FTM_PARAMS_CHAN_INFO_RESERVE3		= 3,
	FTM_PARAMS_CHAN_INFO_NON_HT_5		= 4,
	FTM_PARAMS_CHAN_INFO_RESERVE5		= 5,
	FTM_PARAMS_CHAN_INFO_NON_HT_10		= 6,
	FTM_PARAMS_CHAN_INFO_RESERVE7		= 7,
	FTM_PARAMS_CHAN_INFO_NON_HT_20		= 8, /* excludes 2.4G, and High rate DSSS */
	FTM_PARAMS_CHAN_INFO_HT_MF_20		= 9,
	FTM_PARAMS_CHAN_INFO_VHT_20		= 10,
	FTM_PARAMS_CHAN_INFO_HT_MF_40		= 11,
	FTM_PARAMS_CHAN_INFO_VHT_40		= 12,
	FTM_PARAMS_CHAN_INFO_VHT_80		= 13,
	FTM_PARAMS_CHAN_INFO_VHT_80_80		= 14,
	FTM_PARAMS_CHAN_INFO_VHT_160_2_RFLOS	= 15,
	FTM_PARAMS_CHAN_INFO_VHT_160		= 16,
	/* Reserved from 17 - 30 */
	FTM_PARAMS_CHAN_INFO_DMG_2160		= 31,
	/* Reserved from 32 - 63 */
	FTM_PARAMS_CHAN_INFO_MAX		= 63
};

/* tag_ID/length/value_buffer tuple */
typedef BWL_PRE_PACKED_STRUCT struct {
	uint8	id;
	uint8	len;
	uint8	data[1];
} BWL_POST_PACKED_STRUCT ftm_vs_tlv_t;

BWL_PRE_PACKED_STRUCT struct dot11_ftm_vs_ie {
	uint8 id;						/* DOT11_MNG_VS_ID */
	uint8 len;						/* length following */
	uint8 oui[3];					/* BRCM_PROP_OUI (or Customer) */
	uint8 sub_type;					/* BRCM_FTM_IE_TYPE (or Customer) */
	uint8 version;
	ftm_vs_tlv_t	tlvs[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ftm_vs_ie dot11_ftm_vs_ie_t;

/* same as payload of dot11_ftm_vs_ie.
* This definition helps in having struct access
* of pay load while building FTM VS IE from other modules(NAN)
*/
BWL_PRE_PACKED_STRUCT struct dot11_ftm_vs_ie_pyld {
	uint8 sub_type;					/* BRCM_FTM_IE_TYPE (or Customer) */
	uint8 version;
	ftm_vs_tlv_t	tlvs[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ftm_vs_ie_pyld dot11_ftm_vs_ie_pyld_t;

/* ftm vs api version */
#define BCM_FTM_VS_PARAMS_VERSION 0x01

/* ftm vendor specific information tlv types */
enum {
	FTM_VS_TLV_NONE = 0,
	FTM_VS_TLV_REQ_PARAMS = 1,		/* additional request params (in FTM_REQ) */
	FTM_VS_TLV_MEAS_INFO = 2,		/* measurement information (in FTM_MEAS) */
	FTM_VS_TLV_SEC_PARAMS = 3,		/* security parameters (in either) */
	FTM_VS_TLV_SEQ_PARAMS = 4,		/* toast parameters (FTM_REQ, BRCM proprietary) */
	FTM_VS_TLV_MF_BUF = 5,			/* multi frame buffer - may span ftm vs ie's */
	FTM_VS_TLV_TIMING_PARAMS = 6,            /* timing adjustments */
	FTM_VS_TLV_MF_STATS_BUF = 7		/* multi frame statistics buffer */
	/* add additional types above */
};

/* the following definitions are *DEPRECATED* and moved to implementation files. They
 * are retained here because previous (May 2016) some branches use them
 */
#define FTM_TPK_LEN				16u
#define FTM_RI_RR_BUF_LEN			32u
#define FTM_TPK_RI_RR_LEN			13
#define FTM_TPK_RI_RR_LEN_SECURE_2_0		28
#define FTM_TPK_RI_PHY_LEN			7u
#define FTM_TPK_RR_PHY_LEN			7u
#define FTM_TPK_DATA_BUFFER_LEN			88u
#define FTM_TPK_LEN_SECURE_2_0			64u
#define FTM_TPK_RI_PHY_LEN_SECURE_2_0		14u
#define FTM_TPK_RR_PHY_LEN_SECURE_2_0		14u

#define FTM_RI_RR_BUF_LEN_20MHZ			32u
#define FTM_RI_RR_BUF_LEN_80MHZ			64u

#define FTM_RI_RR_BUF_LEN_FROM_CHANSPEC(chanspec) \
	(CHSPEC_IS20((chanspec)) ? \
	FTM_RI_RR_BUF_LEN_20MHZ : FTM_RI_RR_BUF_LEN_80MHZ)

#define FTM_TPK_RI_RR_LEN_SECURE_2_0_20MHZ      28u
#define FTM_TPK_RI_RR_LEN_SECURE_2_0_80MHZ      62u
#define FTM_TPK_RI_RR_LEN_SECURE_2_0_2G		FTM_TPK_RI_RR_LEN_SECURE_2_0
#define FTM_TPK_RI_RR_LEN_SECURE_2_0_5G		FTM_TPK_RI_RR_LEN_SECURE_2_0_80MHZ

#define FTM_TPK_RI_RR_LEN_FROM_CHANSPEC(chanspec) \
	(CHSPEC_IS20((chanspec)) ? FTM_TPK_RI_RR_LEN_SECURE_2_0_20MHZ : \
	FTM_TPK_RI_RR_LEN_SECURE_2_0_80MHZ)

#define FTM_TPK_RI_PHY_LEN_SECURE_2_0_20MHZ     14u
#define FTM_TPK_RI_PHY_LEN_SECURE_2_0_80MHZ	31u
#define FTM_TPK_RR_PHY_LEN_SECURE_2_0_80MHZ	31u

#define FTM_TPK_RI_PHY_LEN_FROM_CHANSPEC(chanspec) \
	(CHSPEC_IS20((chanspec)) ? FTM_TPK_RI_PHY_LEN_SECURE_2_0_20MHZ : \
	FTM_TPK_RI_PHY_LEN_SECURE_2_0_80MHZ)

#define FTM_TPK_RR_PHY_LEN_SECURE_2_0_20MHZ     14u

#define FTM_TPK_RR_PHY_LEN_FROM_CHANSPEC(chanspec) \
	(CHSPEC_IS20((chanspec)) ? FTM_TPK_RR_PHY_LEN_SECURE_2_0_20MHZ : \
	FTM_TPK_RR_PHY_LEN_SECURE_2_0_80MHZ)

BWL_PRE_PACKED_STRUCT struct dot11_ftm_vs_params {
	uint8 id;                       /* DOT11_MNG_VS_ID */
	uint8 len;
	uint8 oui[3];                   /* Proprietary OUI, BRCM_PROP_OUI */
	uint8 bcm_vs_id;
	ftm_vs_tlv_t ftm_tpk_ri_rr[1];          /* ftm_TPK_ri_rr place holder */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ftm_vs_params dot11_ftm_vs_tpk_ri_rr_params_t;
#define DOT11_FTM_VS_LEN  (sizeof(dot11_ftm_vs_tpk_ri_rr_params_t) - TLV_HDR_LEN)
/* end *DEPRECATED* ftm definitions */

BWL_PRE_PACKED_STRUCT struct dot11_ftm_sync_info {
	uint8 id;		/* Extended - 255 11mc D4.3  */
	uint8 len;
	uint8 id_ext;
	uint8 tsf_sync_info[4];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ftm_sync_info dot11_ftm_sync_info_t;

/* ftm tsf sync info ie len - includes id ext */
#define DOT11_FTM_SYNC_INFO_IE_LEN (sizeof(dot11_ftm_sync_info_t) - TLV_HDR_LEN)

#define DOT11_FTM_IS_SYNC_INFO_IE(_ie) (\
	DOT11_MNG_IE_ID_EXT_MATCH(_ie, DOT11_MNG_FTM_SYNC_INFO) && \
	(_ie)->len == DOT11_FTM_SYNC_INFO_IE_LEN)

BWL_PRE_PACKED_STRUCT struct dot11_dh_param_ie {
	uint8   id;	/* OWE */
	uint8   len;
	uint8   ext_id;	/* EXT_MNG_OWE_DH_PARAM_ID */
	uint16  group;
	uint8   pub_key[0];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_dh_param_ie dot11_dh_param_ie_t;

#define DOT11_DH_EXTID_OFFSET   (OFFSETOF(dot11_dh_param_ie_t, ext_id))

#define DOT11_OWE_DH_PARAM_IE(_ie) (\
	DOT11_MNG_IE_ID_EXT_MATCH(_ie, EXT_MNG_OWE_DH_PARAM_ID))

#define DOT11_MNG_OWE_IE_ID_EXT_INIT(_ie, _id, _len) do {\
	(_ie)->id = DOT11_MNG_ID_EXT_ID; \
	(_ie)->len = _len; \
	(_ie)->ext_id = _id; \
} while (0)

/* 802.11u interworking access network options */
#define IW_ANT_MASK					0x0f
#define IW_INTERNET_MASK				0x10
#define IW_ASRA_MASK					0x20
#define IW_ESR_MASK					0x40
#define IW_UESA_MASK					0x80

/* 802.11u interworking access network type */
#define IW_ANT_PRIVATE_NETWORK				0
#define IW_ANT_PRIVATE_NETWORK_WITH_GUEST		1
#define IW_ANT_CHARGEABLE_PUBLIC_NETWORK		2
#define IW_ANT_FREE_PUBLIC_NETWORK			3
#define IW_ANT_PERSONAL_DEVICE_NETWORK			4
#define IW_ANT_EMERGENCY_SERVICES_NETWORK		5
#define IW_ANT_TEST_NETWORK				14
#define IW_ANT_WILDCARD_NETWORK				15

#define IW_ANT_LEN			1
#define IW_VENUE_LEN			2
#define IW_HESSID_LEN			6
#define IW_HESSID_OFF			(IW_ANT_LEN + IW_VENUE_LEN)
#define IW_MAX_LEN			(IW_ANT_LEN + IW_VENUE_LEN + IW_HESSID_LEN)

/* 802.11u advertisement protocol */
#define ADVP_ANQP_PROTOCOL_ID				0
#define ADVP_MIH_PROTOCOL_ID				1

/* 802.11u advertisement protocol masks */
#define ADVP_QRL_MASK					0x7f
#define ADVP_PAME_BI_MASK				0x80

/* 802.11u advertisement protocol values */
#define ADVP_QRL_REQUEST				0x00
#define ADVP_QRL_RESPONSE				0x7f
#define ADVP_PAME_BI_DEPENDENT				0x00
#define ADVP_PAME_BI_INDEPENDENT			ADVP_PAME_BI_MASK

/* 802.11u ANQP information ID */
#define ANQP_ID_QUERY_LIST				256
#define ANQP_ID_CAPABILITY_LIST				257
#define ANQP_ID_VENUE_NAME_INFO				258
#define ANQP_ID_EMERGENCY_CALL_NUMBER_INFO		259
#define ANQP_ID_NETWORK_AUTHENTICATION_TYPE_INFO	260
#define ANQP_ID_ROAMING_CONSORTIUM_LIST			261
#define ANQP_ID_IP_ADDRESS_TYPE_AVAILABILITY_INFO	262
#define ANQP_ID_NAI_REALM_LIST				263
#define ANQP_ID_G3PP_CELLULAR_NETWORK_INFO		264
#define ANQP_ID_AP_GEOSPATIAL_LOCATION			265
#define ANQP_ID_AP_CIVIC_LOCATION			266
#define ANQP_ID_AP_LOCATION_PUBLIC_ID_URI		267
#define ANQP_ID_DOMAIN_NAME_LIST			268
#define ANQP_ID_EMERGENCY_ALERT_ID_URI			269
#define ANQP_ID_EMERGENCY_NAI				271
#define ANQP_ID_NEIGHBOR_REPORT				272
#define ANQP_ID_VENDOR_SPECIFIC_LIST			56797

/* 802.11u ANQP ID len */
#define ANQP_INFORMATION_ID_LEN				2

/* 802.11u ANQP OUI */
#define ANQP_OUI_SUBTYPE				9

/* 802.11u venue name */
#define VENUE_LANGUAGE_CODE_SIZE			3
#define VENUE_NAME_SIZE					255

/* 802.11u venue groups */
#define VENUE_UNSPECIFIED				0
#define VENUE_ASSEMBLY					1
#define VENUE_BUSINESS					2
#define VENUE_EDUCATIONAL				3
#define VENUE_FACTORY					4
#define VENUE_INSTITUTIONAL				5
#define VENUE_MERCANTILE				6
#define VENUE_RESIDENTIAL				7
#define VENUE_STORAGE					8
#define VENUE_UTILITY					9
#define VENUE_VEHICULAR					10
#define VENUE_OUTDOOR					11

/* 802.11u network authentication type indicator */
#define NATI_UNSPECIFIED				-1
#define NATI_ACCEPTANCE_OF_TERMS_CONDITIONS		0
#define NATI_ONLINE_ENROLLMENT_SUPPORTED		1
#define NATI_HTTP_HTTPS_REDIRECTION			2
#define NATI_DNS_REDIRECTION				3

/* 802.11u IP address type availability - IPv6 */
#define IPA_IPV6_SHIFT					0
#define IPA_IPV6_MASK					(0x03 << IPA_IPV6_SHIFT)
#define	IPA_IPV6_NOT_AVAILABLE				0x00
#define IPA_IPV6_AVAILABLE				0x01
#define IPA_IPV6_UNKNOWN_AVAILABILITY			0x02

/* 802.11u IP address type availability - IPv4 */
#define IPA_IPV4_SHIFT					2
#define IPA_IPV4_MASK					(0x3f << IPA_IPV4_SHIFT)
#define	IPA_IPV4_NOT_AVAILABLE				0x00
#define IPA_IPV4_PUBLIC					0x01
#define IPA_IPV4_PORT_RESTRICT				0x02
#define IPA_IPV4_SINGLE_NAT				0x03
#define IPA_IPV4_DOUBLE_NAT				0x04
#define IPA_IPV4_PORT_RESTRICT_SINGLE_NAT		0x05
#define IPA_IPV4_PORT_RESTRICT_DOUBLE_NAT		0x06
#define IPA_IPV4_UNKNOWN_AVAILABILITY			0x07

/* 802.11u NAI realm encoding */
#define REALM_ENCODING_RFC4282				0
#define REALM_ENCODING_UTF8				1

/* 802.11u IANA EAP method type numbers */
#define REALM_EAP_TLS					13
#define REALM_EAP_LEAP					17
#define REALM_EAP_SIM					18
#define REALM_EAP_TTLS					21
#define REALM_EAP_AKA					23
#define REALM_EAP_PEAP					25
#define REALM_EAP_FAST					43
#define REALM_EAP_PSK					47
#define REALM_EAP_AKAP					50
#define REALM_EAP_EXPANDED				254

/* 802.11u authentication ID */
#define REALM_EXPANDED_EAP				1
#define REALM_NON_EAP_INNER_AUTHENTICATION		2
#define REALM_INNER_AUTHENTICATION_EAP			3
#define REALM_EXPANDED_INNER_EAP			4
#define REALM_CREDENTIAL				5
#define REALM_TUNNELED_EAP_CREDENTIAL			6
#define REALM_VENDOR_SPECIFIC_EAP			221

/* 802.11u non-EAP inner authentication type */
#define REALM_RESERVED_AUTH				0
#define REALM_PAP					1
#define REALM_CHAP					2
#define REALM_MSCHAP					3
#define REALM_MSCHAPV2					4

/* 802.11u credential type */
#define REALM_SIM					1
#define REALM_USIM					2
#define REALM_NFC					3
#define REALM_HARDWARE_TOKEN				4
#define REALM_SOFTOKEN					5
#define REALM_CERTIFICATE				6
#define REALM_USERNAME_PASSWORD				7
#define REALM_SERVER_SIDE				8
#define REALM_RESERVED_CRED				9
#define REALM_VENDOR_SPECIFIC_CRED			10

/* 802.11u 3GPP PLMN */
#define G3PP_GUD_VERSION				0
#define G3PP_PLMN_LIST_IE				0

/* AP Location Public ID Info encoding */
#define PUBLIC_ID_URI_FQDN_SE_ID		0
/* URI/FQDN Descriptor field values */
#define LOCATION_ENCODING_HELD			1
#define LOCATION_ENCODING_SUPL			2
#define URI_FQDN_SIZE					255

/** hotspot2.0 indication element (vendor specific) */
BWL_PRE_PACKED_STRUCT struct hs20_ie {
	uint8 oui[3];
	uint8 type;
	uint8 config;
} BWL_POST_PACKED_STRUCT;
typedef struct hs20_ie hs20_ie_t;
#define HS20_IE_LEN 5	/* HS20 IE length */

/* Short SSID list Extended Capabilities element */
BWL_PRE_PACKED_STRUCT struct short_ssid_list_ie {
	uint8 id;
	uint8 len;
	uint8 id_ext;
	uint8 data[1];    /* Capabilities Information */
} BWL_POST_PACKED_STRUCT;

typedef struct short_ssid_list_ie short_ssid_list_ie_t;
#define SHORT_SSID_LIST_IE_FIXED_LEN	3	/* SHORT SSID LIST IE LENGTH */

/** IEEE 802.11 Annex E */
typedef enum {
	DOT11_2GHZ_20MHZ_CLASS_12	= 81,	/* Ch 1-11 */
	DOT11_5GHZ_20MHZ_CLASS_1	= 115,	/* Ch 36-48 */
	DOT11_5GHZ_20MHZ_CLASS_2_DFS	= 118,	/* Ch 52-64 */
	DOT11_5GHZ_20MHZ_CLASS_3	= 124,	/* Ch 149-161 */
	DOT11_5GHZ_20MHZ_CLASS_4_DFS	= 121,	/* Ch 100-140 */
	DOT11_5GHZ_20MHZ_CLASS_5	= 125,	/* Ch 149-165 */
	DOT11_5GHZ_40MHZ_CLASS_22	= 116,	/* Ch 36-44,   lower */
	DOT11_5GHZ_40MHZ_CLASS_23_DFS	= 119,	/* Ch 52-60,   lower */
	DOT11_5GHZ_40MHZ_CLASS_24_DFS	= 122,	/* Ch 100-132, lower */
	DOT11_5GHZ_40MHZ_CLASS_25	= 126,	/* Ch 149-157, lower */
	DOT11_5GHZ_40MHZ_CLASS_27	= 117,	/* Ch 40-48,   upper */
	DOT11_5GHZ_40MHZ_CLASS_28_DFS	= 120,	/* Ch 56-64,   upper */
	DOT11_5GHZ_40MHZ_CLASS_29_DFS	= 123,	/* Ch 104-136, upper */
	DOT11_5GHZ_40MHZ_CLASS_30	= 127,	/* Ch 153-161, upper */
	DOT11_2GHZ_40MHZ_CLASS_32	= 83,	/* Ch 1-7,     lower */
	DOT11_2GHZ_40MHZ_CLASS_33	= 84,	/* Ch 5-11,    upper */
} dot11_op_class_t;

/* QoS map */
#define QOS_MAP_FIXED_LENGTH	(8 * 2)	/* DSCP ranges fixed with 8 entries */

/* BCM proprietary IE type for AIBSS */
#define BCM_AIBSS_IE_TYPE 56

/* BCM proprietary flag type for WL_DISCO_VSIE */
#define SSE_OUI                                  "\x00\x00\xF0"
#define VENDOR_ENTERPRISE_STA_OUI_TYPE           0x22
#define MAX_VSIE_DISASSOC                        (1)
#define DISCO_VSIE_LEN                           0x09u

/* Single PMK IE */
#define CCX_SPMK_TYPE	3	/* CCX Extended Cap IE type for SPMK */
/* CCX Extended Capability IE */
BWL_PRE_PACKED_STRUCT struct ccx_spmk_cap_ie {
	uint8 id;		/* 221, DOT11_MNG_PROPR_ID */
	uint8 len;
	uint8 oui[DOT11_OUI_LEN];	/* 00:40:96, CISCO_AIRONET_OUI */
	uint8 type;		/* 11 */
	uint8 cap;
} BWL_POST_PACKED_STRUCT;
typedef struct ccx_spmk_cap_ie ccx_spmk_cap_ie_t;

/* OWE definitions */
/* ID + len + OUI + OI type + BSSID + SSID_len */
#define OWE_TRANS_MODE_IE_FIXED_LEN  13u

/* Supported Operating Classes element */
BWL_PRE_PACKED_STRUCT struct supp_op_classes_ie {
	uint8 id;
	uint8 len;
	uint8 cur_op_class;
	uint8 op_classes[];    /* Supported Operating Classes */
} BWL_POST_PACKED_STRUCT;
typedef struct supp_op_classes_ie supp_op_classes_ie_t;

/* Transition mode (bit number) */
#define TRANSISION_MODE_WPA3_PSK		0u
#define TRANSITION_MODE_SAE_PK			1u
#define TRANSITION_MODE_WPA3_ENTERPRISE		2u
#define TRANSITION_MODE_ENHANCED_OPEN		3u

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif /* _802_11_H_ */
