/*
 * $Copyright Open Broadcom Corporation$
 *
 * Fundamental types and constants relating to 802.11
 *
 * $Id: 802.11.h 495738 2014-08-08 03:36:17Z $
 */

#ifndef _802_11_H_
#define _802_11_H_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif

#ifndef _NET_ETHERNET_H_
#include <proto/ethernet.h>
#endif

#include <proto/wpa.h>

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>


#define DOT11_TU_TO_US			1024	/* 802.11 Time Unit is 1024 microseconds */

/* Generic 802.11 frame constants */
#define DOT11_A3_HDR_LEN		24	/* d11 header length with A3 */
#define DOT11_A4_HDR_LEN		30	/* d11 header length with A4 */
#define DOT11_MAC_HDR_LEN		DOT11_A3_HDR_LEN	/* MAC header length */
#define DOT11_FCS_LEN			4	/* d11 FCS length */
#define DOT11_ICV_LEN			4	/* d11 ICV length */
#define DOT11_ICV_AES_LEN		8	/* d11 ICV/AES length */
#define DOT11_QOS_LEN			2	/* d11 QoS length */
#define DOT11_HTC_LEN			4	/* d11 HT Control field length */

#define DOT11_KEY_INDEX_SHIFT		6	/* d11 key index shift */
#define DOT11_IV_LEN			4	/* d11 IV length */
#define DOT11_IV_TKIP_LEN		8	/* d11 IV TKIP length */
#define DOT11_IV_AES_OCB_LEN		4	/* d11 IV/AES/OCB length */
#define DOT11_IV_AES_CCM_LEN		8	/* d11 IV/AES/CCM length */
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
#define	DOT11_CTS_LEN		10		/* d11 CTS frame length */

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
#define	DOT11_MGMT_HDR_LEN	24		/* d11 management header length */

/* Management frame payloads */

BWL_PRE_PACKED_STRUCT struct dot11_bcn_prb {
	uint32			timestamp[2];
	uint16			beacon_interval;
	uint16			capability;
} BWL_POST_PACKED_STRUCT;
#define	DOT11_BCN_PRB_LEN	12		/* 802.11 beacon/probe frame fixed length */
#define	DOT11_BCN_PRB_FIXED_LEN	12		/* 802.11 beacon/probe frame fixed length */

BWL_PRE_PACKED_STRUCT struct dot11_auth {
	uint16			alg;		/* algorithm */
	uint16			seq;		/* sequence control */
	uint16			status;		/* status code */
} BWL_POST_PACKED_STRUCT;
#define DOT11_AUTH_FIXED_LEN	6		/* length of auth frame without challenge IE */

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
#define DOT11_MNG_IE_TPC_REPORT_LEN	2 	/* length of IE data, not including 2 byte header */

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
	uint8	oui[3];
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
	uint8 id;	/* id DOT11_MNG_EXT_CHANNEL_SWITCH_ID */
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

#define DOT11_EXTCAP_LEN_MAX	8

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

BWL_PRE_PACKED_STRUCT struct dot11_extcap {
	uint8 extcap[DOT11_EXTCAP_LEN_MAX];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_extcap dot11_extcap_t;

/* TDLS Capabilities */
#define DOT11_TDLS_CAP_TDLS			37		/* TDLS support */
#define DOT11_TDLS_CAP_PU_BUFFER_STA	28		/* TDLS Peer U-APSD buffer STA support */
#define DOT11_TDLS_CAP_PEER_PSM		20		/* TDLS Peer PSM support */
#define DOT11_TDLS_CAP_CH_SW			30		/* TDLS Channel switch */
#define DOT11_TDLS_CAP_PROH			38		/* TDLS prohibited */
#define DOT11_TDLS_CAP_CH_SW_PROH		39		/* TDLS Channel switch prohibited */
#define DOT11_TDLS_CAP_TDLS_WIDER_BW	61	/* TDLS Wider Band-Width */

#define TDLS_CAP_MAX_BIT		39		/* TDLS max bit defined in ext cap */

/* 802.11h/802.11k Measurement Request/Report IEs */
/* Measurement Type field */
#define DOT11_MEASURE_TYPE_BASIC 	0	/* d11 measurement basic type */
#define DOT11_MEASURE_TYPE_CCA 		1	/* d11 measurement CCA type */
#define DOT11_MEASURE_TYPE_RPI		2	/* d11 measurement RPI type */
#define DOT11_MEASURE_TYPE_CHLOAD		3	/* d11 measurement Channel Load type */
#define DOT11_MEASURE_TYPE_NOISE		4	/* d11 measurement Noise Histogram type */
#define DOT11_MEASURE_TYPE_BEACON		5	/* d11 measurement Beacon type */
#define DOT11_MEASURE_TYPE_FRAME	6	/* d11 measurement Frame type */
#define DOT11_MEASURE_TYPE_STAT		7	/* d11 measurement STA Statistics type */
#define DOT11_MEASURE_TYPE_LCI		8	/* d11 measurement LCI type */
#define DOT11_MEASURE_TYPE_TXSTREAM		9	/* d11 measurement TX Stream type */
#define DOT11_MEASURE_TYPE_PAUSE		255	/* d11 measurement pause type */

/* Measurement Request Modes */
#define DOT11_MEASURE_MODE_PARALLEL 	(1<<0)	/* d11 measurement parallel */
#define DOT11_MEASURE_MODE_ENABLE 	(1<<1)	/* d11 measurement enable */
#define DOT11_MEASURE_MODE_REQUEST	(1<<2)	/* d11 measurement request */
#define DOT11_MEASURE_MODE_REPORT 	(1<<3)	/* d11 measurement report */
#define DOT11_MEASURE_MODE_DUR 	(1<<4)	/* d11 measurement dur mandatory */
/* Measurement Report Modes */
#define DOT11_MEASURE_MODE_LATE 	(1<<0)	/* d11 measurement late */
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
		uint8 data[1];
	} BWL_POST_PACKED_STRUCT rep;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_meas_rep dot11_meas_rep_t;

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
	uint8 id; 			/* 11, DOT11_MNG_QBSS_LOAD_ID */
	uint8 length;
	uint16 station_count; 		/* total number of STAs associated */
	uint8 channel_utilization;	/* % of time, normalized to 255, QAP sensed medium busy */
	uint16 aac; 			/* available admission capacity */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_qbss_load_ie dot11_qbss_load_ie_t;
#define BSS_LOAD_IE_SIZE 	7	/* BSS load IE size */

#define WLC_QBSS_LOAD_CHAN_FREE_MAX	0xff	/* max for channel free score */

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
#define DOT11_AID_MASK			0x3fff	/* d11 AID mask */

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
/* 12 is unused */

/* 32-39 are QSTA specific reasons added in 11e */
#define DOT11_RC_UNSPECIFIED_QOS	32	/* unspecified QoS-related reason */
#define DOT11_RC_INSUFFCIENT_BW		33	/* QAP lacks sufficient bandwidth */
#define DOT11_RC_EXCESSIVE_FRAMES	34	/* excessive number of frames need ack */
#define DOT11_RC_TX_OUTSIDE_TXOP	35	/* transmitting outside the limits of txop */
#define DOT11_RC_LEAVING_QBSS		36	/* QSTA is leaving the QBSS (or restting) */
#define DOT11_RC_BAD_MECHANISM		37	/* does not want to use the mechanism */
#define DOT11_RC_SETUP_NEEDED		38	/* mechanism needs a setup */
#define DOT11_RC_TIMEOUT		39	/* timeout */

#define DOT11_RC_MAX			23	/* Reason codes > 23 are reserved */

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
#define DOT11_SC_ASSOC_VHT_REQUIRED	104	/* Association denied because the requesting
						 * station does not support VHT features.
						 */

#define DOT11_SC_TRANSMIT_FAILURE	79	/* transmission failure */

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

/* TLV defines */
#define TLV_TAG_OFF		0	/* tag offset */
#define TLV_LEN_OFF		1	/* length offset */
#define TLV_HDR_LEN		2	/* header length */
#define TLV_BODY_OFF		2	/* body offset */
#define TLV_BODY_LEN_MAX	255	/* max body length */

/* Management Frame Information Element IDs */
#define DOT11_MNG_SSID_ID			0	/* d11 management SSID id */
#define DOT11_MNG_RATES_ID			1	/* d11 management rates id */
#define DOT11_MNG_FH_PARMS_ID			2	/* d11 management FH parameter id */
#define DOT11_MNG_DS_PARMS_ID			3	/* d11 management DS parameter id */
#define DOT11_MNG_CF_PARMS_ID			4	/* d11 management CF parameter id */
#define DOT11_MNG_TIM_ID			5	/* d11 management TIM id */
#define DOT11_MNG_IBSS_PARMS_ID			6	/* d11 management IBSS parameter id */
#define DOT11_MNG_COUNTRY_ID			7	/* d11 management country id */
#define DOT11_MNG_HOPPING_PARMS_ID		8	/* d11 management hopping parameter id */
#define DOT11_MNG_HOPPING_TABLE_ID		9	/* d11 management hopping table id */
#define DOT11_MNG_REQUEST_ID			10	/* d11 management request id */
#define DOT11_MNG_QBSS_LOAD_ID 			11	/* d11 management QBSS Load id */
#define DOT11_MNG_EDCA_PARAM_ID			12	/* 11E EDCA Parameter id */
#define DOT11_MNG_TSPEC_ID			13	/* d11 management TSPEC id */
#define DOT11_MNG_TCLAS_ID			14	/* d11 management TCLAS id */
#define DOT11_MNG_CHALLENGE_ID			16	/* d11 management chanllenge id */
#define DOT11_MNG_PWR_CONSTRAINT_ID		32	/* 11H PowerConstraint */
#define DOT11_MNG_PWR_CAP_ID			33	/* 11H PowerCapability */
#define DOT11_MNG_TPC_REQUEST_ID 		34	/* 11H TPC Request */
#define DOT11_MNG_TPC_REPORT_ID			35	/* 11H TPC Report */
#define DOT11_MNG_SUPP_CHANNELS_ID		36	/* 11H Supported Channels */
#define DOT11_MNG_CHANNEL_SWITCH_ID		37	/* 11H ChannelSwitch Announcement */
#define DOT11_MNG_MEASURE_REQUEST_ID		38	/* 11H MeasurementRequest */
#define DOT11_MNG_MEASURE_REPORT_ID		39	/* 11H MeasurementReport */
#define DOT11_MNG_QUIET_ID			40	/* 11H Quiet */
#define DOT11_MNG_IBSS_DFS_ID			41	/* 11H IBSS_DFS */
#define DOT11_MNG_ERP_ID			42	/* d11 management ERP id */
#define DOT11_MNG_TS_DELAY_ID			43	/* d11 management TS Delay id */
#define DOT11_MNG_TCLAS_PROC_ID			44	/* d11 management TCLAS processing id */
#define	DOT11_MNG_HT_CAP			45	/* d11 mgmt HT cap id */
#define DOT11_MNG_QOS_CAP_ID			46	/* 11E QoS Capability id */
#define DOT11_MNG_NONERP_ID			47	/* d11 management NON-ERP id */
#define DOT11_MNG_RSN_ID			48	/* d11 management RSN id */
#define DOT11_MNG_EXT_RATES_ID			50	/* d11 management ext. rates id */
#define DOT11_MNG_AP_CHREP_ID			51	/* 11k AP Channel report id */
#define DOT11_MNG_NEIGHBOR_REP_ID		52	/* 11k & 11v Neighbor report id */
#define DOT11_MNG_RCPI_ID			53	/* 11k RCPI */
#define DOT11_MNG_MDIE_ID			54	/* 11r Mobility domain id */
#define DOT11_MNG_FTIE_ID			55	/* 11r Fast Bss Transition id */
#define DOT11_MNG_FT_TI_ID			56	/* 11r Timeout Interval id */
#define DOT11_MNG_RDE_ID			57	/* 11r RIC Data Element id */
#define	DOT11_MNG_REGCLASS_ID			59	/* d11 management regulatory class id */
#define DOT11_MNG_EXT_CSA_ID			60	/* d11 Extended CSA */
#define	DOT11_MNG_HT_ADD			61	/* d11 mgmt additional HT info */
#define	DOT11_MNG_EXT_CHANNEL_OFFSET		62	/* d11 mgmt ext channel offset */
#define DOT11_MNG_BSS_AVR_ACCESS_DELAY_ID	63	/* 11k bss average access delay */
#define DOT11_MNG_ANTENNA_ID			64	/* 11k antenna id */
#define DOT11_MNG_RSNI_ID			65	/* 11k RSNI id */
#define DOT11_MNG_MEASUREMENT_PILOT_TX_ID	66	/* 11k measurement pilot tx info id */
#define DOT11_MNG_BSS_AVAL_ADMISSION_CAP_ID	67	/* 11k bss aval admission cap id */
#define DOT11_MNG_BSS_AC_ACCESS_DELAY_ID	68	/* 11k bss AC access delay id */
#define DOT11_MNG_WAPI_ID			68	/* d11 management WAPI id */
#define DOT11_MNG_TIME_ADVERTISE_ID	69	/* 11p time advertisement */
#define DOT11_MNG_RRM_CAP_ID		70	/* 11k radio measurement capability */
#define DOT11_MNG_MULTIPLE_BSSID_ID		71	/* 11k multiple BSSID id */
#define	DOT11_MNG_HT_BSS_COEXINFO_ID		72	/* d11 mgmt OBSS Coexistence INFO */
#define	DOT11_MNG_HT_BSS_CHANNEL_REPORT_ID	73	/* d11 mgmt OBSS Intolerant Channel list */
#define	DOT11_MNG_HT_OBSS_ID			74	/* d11 mgmt OBSS HT info */
#define DOT11_MNG_MMIE_ID			76	/* d11 mgmt MIC IE */
#define DOT11_MNG_FMS_DESCR_ID			86	/* 11v FMS descriptor */
#define DOT11_MNG_FMS_REQ_ID			87	/* 11v FMS request id */
#define DOT11_MNG_FMS_RESP_ID			88	/* 11v FMS response id */
#define DOT11_MNG_BSS_MAX_IDLE_PERIOD_ID	90	/* 11v bss max idle id */
#define DOT11_MNG_TFS_REQUEST_ID		91	/* 11v tfs request id */
#define DOT11_MNG_TFS_RESPONSE_ID		92	/* 11v tfs response id */
#define DOT11_MNG_WNM_SLEEP_MODE_ID		93	/* 11v wnm-sleep mode id */
#define DOT11_MNG_TIMBC_REQ_ID			94	/* 11v TIM broadcast request id */
#define DOT11_MNG_TIMBC_RESP_ID			95	/* 11v TIM broadcast response id */
#define DOT11_MNG_CHANNEL_USAGE			97	/* 11v channel usage */
#define DOT11_MNG_TIME_ZONE_ID			98	/* 11v time zone */
#define DOT11_MNG_DMS_REQUEST_ID		99	/* 11v dms request id */
#define DOT11_MNG_DMS_RESPONSE_ID		100	/* 11v dms response id */
#define DOT11_MNG_LINK_IDENTIFIER_ID		101	/* 11z TDLS Link Identifier IE */
#define DOT11_MNG_WAKEUP_SCHEDULE_ID		102	/* 11z TDLS Wakeup Schedule IE */
#define DOT11_MNG_CHANNEL_SWITCH_TIMING_ID	104	/* 11z TDLS Channel Switch Timing IE */
#define DOT11_MNG_PTI_CONTROL_ID		105	/* 11z TDLS PTI Control IE */
#define DOT11_MNG_PU_BUFFER_STATUS_ID	106	/* 11z TDLS PU Buffer Status IE */
#define DOT11_MNG_INTERWORKING_ID		107	/* 11u interworking */
#define DOT11_MNG_ADVERTISEMENT_ID		108	/* 11u advertisement protocol */
#define DOT11_MNG_EXP_BW_REQ_ID			109	/* 11u expedited bandwith request */
#define DOT11_MNG_QOS_MAP_ID			110	/* 11u QoS map set */
#define DOT11_MNG_ROAM_CONSORT_ID		111	/* 11u roaming consortium */
#define DOT11_MNG_EMERGCY_ALERT_ID		112	/* 11u emergency alert identifier */
#define	DOT11_MNG_EXT_CAP_ID			127	/* d11 mgmt ext capability */
#define	DOT11_MNG_VHT_CAP_ID			191	/* d11 mgmt VHT cap id */
#define	DOT11_MNG_VHT_OPERATION_ID		192	/* d11 mgmt VHT op id */
#define DOT11_MNG_WIDE_BW_CHANNEL_SWITCH_ID		194	/* Wide BW Channel Switch IE */
#define DOT11_MNG_VHT_TRANSMIT_POWER_ENVELOPE_ID	195	/* VHT transmit Power Envelope IE */
#define DOT11_MNG_CHANNEL_SWITCH_WRAPPER_ID		196	/* Channel Switch Wrapper IE */
#define DOT11_MNG_AID_ID					197	/* Association ID  IE */
#define	DOT11_MNG_OPER_MODE_NOTIF_ID	199	/* d11 mgmt VHT oper mode notif */


#define DOT11_MNG_WPA_ID			221	/* d11 management WPA id */
#define DOT11_MNG_PROPR_ID			221
/* should start using this one instead of above two */
#define DOT11_MNG_VS_ID				221	/* d11 management Vendor Specific IE */

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
 * 802.11-2012 and 802.11ac_D4.0 sec 8.4.2.3
 * These selector values are advertised in Supported Rates and Extended Supported Rates IEs
 * in the supported rates list with the Basic rate bit set.
 * Constants below include the basic bit.
 */
#define DOT11_BSS_MEMBERSHIP_HT         0xFF  /* Basic 0x80 + 127, HT Required to join */
#define DOT11_BSS_MEMBERSHIP_VHT        0xFE  /* Basic 0x80 + 126, VHT Required to join */

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
#define DOT11_EXT_CAP_OBSS_COEX_MGMT		0
/* Extended Channel Switching support bit position */
#define DOT11_EXT_CAP_EXT_CHAN_SWITCHING	2
/* scheduled PSMP support bit position */
#define DOT11_EXT_CAP_SPSMP			6
/*  Flexible Multicast Service */
#define DOT11_EXT_CAP_FMS			11
/* proxy ARP service support bit position */
#define DOT11_EXT_CAP_PROXY_ARP			12
/* Traffic Filter Service */
#define DOT11_EXT_CAP_TFS			16
/* WNM-Sleep Mode */
#define DOT11_EXT_CAP_WNM_SLEEP			17
/* TIM Broadcast service */
#define DOT11_EXT_CAP_TIMBC			18
/* BSS Transition Management support bit position */
#define DOT11_EXT_CAP_BSSTRANS_MGMT		19
/* Direct Multicast Service */
#define DOT11_EXT_CAP_DMS			26
/* Interworking support bit position */
#define DOT11_EXT_CAP_IW			31
/* QoS map support bit position */
#define DOT11_EXT_CAP_QOS_MAP		32
/* service Interval granularity bit position and mask */
#define DOT11_EXT_CAP_SI			41
#define DOT11_EXT_CAP_SI_MASK			0x0E
/* WNM notification */
#define DOT11_EXT_CAP_WNM_NOTIF			46
/* Operating mode notification - VHT (11ac D3.0 - 8.4.2.29) */
#define DOT11_EXT_CAP_OPER_MODE_NOTIF		62

/* VHT Operating mode bit fields -  (11ac D3.0 - 8.4.1.50) */
#define DOT11_OPER_MODE_CHANNEL_WIDTH_SHIFT 0
#define DOT11_OPER_MODE_CHANNEL_WIDTH_MASK 0x3
#define DOT11_OPER_MODE_RXNSS_SHIFT 4
#define DOT11_OPER_MODE_RXNSS_MASK 0x70
#define DOT11_OPER_MODE_RXNSS_TYPE_SHIFT 7
#define DOT11_OPER_MODE_RXNSS_TYPE_MASK 0x80

#define DOT11_OPER_MODE(type, nss, chanw) (\
	((type) << DOT11_OPER_MODE_RXNSS_TYPE_SHIFT &\
		 DOT11_OPER_MODE_RXNSS_TYPE_MASK) |\
	(((nss) - 1) << DOT11_OPER_MODE_RXNSS_SHIFT & DOT11_OPER_MODE_RXNSS_MASK) |\
	((chanw) << DOT11_OPER_MODE_CHANNEL_WIDTH_SHIFT &\
		 DOT11_OPER_MODE_CHANNEL_WIDTH_MASK))

#define DOT11_OPER_MODE_CHANNEL_WIDTH(mode) \
	(((mode) & DOT11_OPER_MODE_CHANNEL_WIDTH_MASK)\
		>> DOT11_OPER_MODE_CHANNEL_WIDTH_SHIFT)
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

#define DOT11_OPER_MODE_CHANNEL_WIDTH_20MHZ(mode) (\
	((mode) & DOT11_OPER_MODE_CHANNEL_WIDTH_MASK) == DOT11_OPER_MODE_20MHZ)
#define DOT11_OPER_MODE_CHANNEL_WIDTH_40MHZ(mode) (\
	((mode) & DOT11_OPER_MODE_CHANNEL_WIDTH_MASK) == DOT11_OPER_MODE_40MHZ)
#define DOT11_OPER_MODE_CHANNEL_WIDTH_80MHZ(mode) (\
	((mode) & DOT11_OPER_MODE_CHANNEL_WIDTH_MASK) == DOT11_OPER_MODE_80MHZ)
#define DOT11_OPER_MODE_CHANNEL_WIDTH_160MHZ(mode) (\
	((mode) & DOT11_OPER_MODE_CHANNEL_WIDTH_MASK) == DOT11_OPER_MODE_160MHZ)
#define DOT11_OPER_MODE_CHANNEL_WIDTH_8080MHZ(mode) (\
	((mode) & DOT11_OPER_MODE_CHANNEL_WIDTH_MASK) == DOT11_OPER_MODE_8080MHZ)

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
#define	DOT11_ACTION_CAT_SA_QUERY	8	/* security association query */
#define	DOT11_ACTION_CAT_PDPA		9	/* protected dual of public action */
#define DOT11_ACTION_CAT_WNM		10	/* category for WNM */
#define DOT11_ACTION_CAT_UWNM		11	/* category for Unprotected WNM */
#define DOT11_ACTION_NOTIFICATION	17
#define DOT11_ACTION_CAT_VHT		21	/* VHT action */
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
#define DOT11_PUB_ACTION_GAS_CB_REQ	12	/* GAS Comeback Request */

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
#define DOT11_TCLAS_IE_LEN		3	/* Fixed length, include id and len */

/** TCLAS processing element */
BWL_PRE_PACKED_STRUCT struct dot11_tclas_proc_ie {
	uint8 id;				/* 44, DOT11_MNG_TCLAS_PROC_ID */
	uint8 len;
	uint8 process;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tclas_proc_ie dot11_tclas_proc_ie_t;
#define DOT11_TCLAS_PROC_IE_LEN		3	/* Fixed length, include id and len */

#define DOT11_TCLAS_PROC_MATCHALL	0	/* All high level element need to match */
#define DOT11_TCLAS_PROC_MATCHONE	1	/* One high level element need to match */
#define DOT11_TCLAS_PROC_NONMATCH	2	/* Non match to any high level element */


/* TSPEC element defined in 802.11 std section 8.4.2.32 - Not supported */
#define DOT11_TSPEC_IE_LEN		57	/* Fixed length */

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
#define DOT11_TFS_RESP_IE_LEN		1	/* Fixed length, without id and len */

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
/* bit18-bit26, not used for RRM_IOVAR */
#define DOT11_RRM_CAP_MPTI		27
#define DOT11_RRM_CAP_NBRTSFO		28
#define DOT11_RRM_CAP_RCPI		29
#define DOT11_RRM_CAP_RSNI		30
#define DOT11_RRM_CAP_BSSAAD		31
#define DOT11_RRM_CAP_BSSAAC		32
#define DOT11_RRM_CAP_AI		33

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
	uint8 id;
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
#define DOT11_RMREQ_BCN_LEN	18

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

/* Reporting Detail element definition */
#define DOT11_RMREQ_BCN_REPDET_FIXED	0	/* Fixed length fields only */
#define DOT11_RMREQ_BCN_REPDET_REQUEST	1	/* + requested information elems */
#define DOT11_RMREQ_BCN_REPDET_ALL	2	/* All fields */

/* Sub-element IDs for Beacon Report */
#define DOT11_RMREP_BCN_FRM_BODY	1

/* Sub-element IDs for Frame Report */
#define DOT11_RMREP_FRAME_COUNT_REPORT 1

/** Channel load request */
BWL_PRE_PACKED_STRUCT struct dot11_rmreq_chanload {
	uint8 id;
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
	uint8 id;
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
	uint8 id;
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
	uint8 id;
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

/** Transmit stream/category measurement request */
BWL_PRE_PACKED_STRUCT struct dot11_rmreq_tx_stream {
	uint8 id;
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

/** Measurement pause request */
BWL_PRE_PACKED_STRUCT struct dot11_rmreq_pause_time {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint16 pause_time;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq_pause_time dot11_rmreq_pause_time_t;


/* Neighbor Report subelements ID (11k & 11v) */
#define DOT11_NGBR_TSF_INFO_SE_ID	1
#define DOT11_NGBR_CCS_SE_ID		2
#define DOT11_NGBR_BSSTRANS_PREF_SE_ID	3
#define DOT11_NGBR_BSS_TERM_DUR_SE_ID	4
#define DOT11_NGBR_BEARING_SE_ID	5

/** Neighbor Report, BSS Transition Candidate Preference subelement */
BWL_PRE_PACKED_STRUCT struct dot11_ngbr_bsstrans_pref_se {
	uint8 sub_id;
	uint8 len;
	uint8 preference;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ngbr_bsstrans_pref_se dot11_ngbr_bsstrans_pref_se_t;
#define DOT11_NGBR_BSSTRANS_PREF_SE_LEN	1

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

/** Neighbor Report element (11k & 11v) */
BWL_PRE_PACKED_STRUCT struct dot11_neighbor_rep_ie {
	uint8 id;
	uint8 len;
	struct ether_addr bssid;
	uint32 bssid_info;
	uint8 reg;		/* Operating class */
	uint8 channel;
	uint8 phytype;
	uint8 data[1]; 		/* Variable size subelements */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_neighbor_rep_ie dot11_neighbor_rep_ie_t;
#define DOT11_NEIGHBOR_REP_IE_FIXED_LEN	13


/* MLME Enumerations */
#define DOT11_BSSTYPE_INFRASTRUCTURE		0	/* d11 infrastructure */
#define DOT11_BSSTYPE_INDEPENDENT		1	/* d11 independent */
#define DOT11_BSSTYPE_ANY			2	/* d11 any BSS type */
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

/* 802.11 B PHY constants */
#define BPHY_SLOT_TIME          20      /* BPHY slot time */
#define BPHY_SIFS_TIME          10      /* BPHY SIFS time */
#define BPHY_DIFS_TIME          50      /* BPHY DIFS time */
#define BPHY_PLCP_TIME          192     /* BPHY PLCP time */
#define BPHY_PLCP_SHORT_TIME    96      /* BPHY PLCP short time */
#define BPHY_CWMIN              31      /* BPHY cwmin */

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

#define BRCM_PROP_OUI		"\x00\x90\x4C"


/* Action frame type for RWL */
#define RWL_WIFI_DEFAULT		0
#define RWL_WIFI_FIND_MY_PEER		9 /* Used while finding server */
#define RWL_WIFI_FOUND_PEER		10 /* Server response to the client  */
#define RWL_ACTION_WIFI_FRAG_TYPE	85 /* Fragment indicator for receiver */

#define PROXD_AF_TYPE			11 /* Wifi proximity action frame type */
#define BRCM_RELMACST_AF_TYPE	        12 /* RMC action frame type */

#ifndef LINUX_POSTMOGRIFY_REMOVAL
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
	uint8 oui[3];
	uint8 type;		/* type of this IE */
	uint16 cap;		/* DPT capabilities */
} BWL_POST_PACKED_STRUCT;
typedef struct brcm_prop_ie_s brcm_prop_ie_t;

#define BRCM_PROP_IE_LEN	6	/* len of fixed part of brcm_prop ie */

#define DPT_IE_TYPE             2


#define BRCM_SYSCAP_IE_TYPE	3
#define WET_TUNNEL_IE_TYPE	3
#endif /* LINUX_POSTMOGRIFY_REMOVAL */

/* brcm syscap_ie cap */
#define BRCM_SYSCAP_WET_TUNNEL	0x0100	/* Device with WET_TUNNEL support */

#define BRCM_OUI		"\x00\x10\x18"	/* Broadcom OUI */

/** BRCM info element */
BWL_PRE_PACKED_STRUCT struct brcm_ie {
	uint8	id;		/* IE ID, 221, DOT11_MNG_PROPR_ID */
	uint8	len;		/* IE length */
	uint8	oui[3];
	uint8	ver;		/* type/ver of this IE */
	uint8	assoc;		/* # of assoc STAs */
	uint8	flags;		/* misc flags */
	uint8	flags1;		/* misc flags */
	uint16	amsdu_mtu_pref;	/* preferred A-MSDU MTU */
} BWL_POST_PACKED_STRUCT;
typedef	struct brcm_ie brcm_ie_t;
#define BRCM_IE_LEN		11	/* BRCM IE length */
#define BRCM_IE_VER		2	/* BRCM IE version */
#define BRCM_IE_LEGACY_AES_VER	1	/* BRCM IE legacy AES version */

/* brcm_ie flags */
#define	BRF_ABCAP		0x1	/* afterburner is obsolete,  defined for backward compat */
#define	BRF_ABRQRD		0x2	/* afterburner is obsolete,  defined for backward compat */
#define	BRF_LZWDS		0x4	/* lazy wds enabled */
#define	BRF_BLOCKACK		0x8	/* BlockACK capable */
#define BRF_ABCOUNTER_MASK	0xf0	/* afterburner is obsolete,  defined for backward compat */
#define BRF_PROP_11N_MCS	0x10	/* re-use afterburner bit */

#define GET_BRF_PROP_11N_MCS(brcm_ie) \
	(!((brcm_ie)->flags & BRF_ABCAP) && ((brcm_ie)->flags & BRF_PROP_11N_MCS))

/* brcm_ie flags1 */
#define	BRF1_AMSDU		0x1	/* A-MSDU capable */
#define BRF1_WMEPS		0x4	/* AP is capable of handling WME + PS w/o APSD */
#define BRF1_PSOFIX		0x8	/* AP has fixed PS mode out-of-order packets */
#define	BRF1_RX_LARGE_AGG	0x10	/* device can rx large aggregates */
#define BRF1_RFAWARE_DCS	0x20    /* RFAWARE dynamic channel selection (DCS) */
#define BRF1_SOFTAP		0x40    /* Configure as Broadcom SOFTAP */
#define BRF1_DWDS		0x80    /* DWDS capable */

/** Vendor IE structure */
BWL_PRE_PACKED_STRUCT struct vndr_ie {
	uchar id;
	uchar len;
	uchar oui [3];
	uchar data [1]; 	/* Variable size data */
} BWL_POST_PACKED_STRUCT;
typedef struct vndr_ie vndr_ie_t;

#define VNDR_IE_HDR_LEN		2	/* id + len field */
#define VNDR_IE_MIN_LEN		3	/* size of the oui field */
#define VNDR_IE_FIXED_LEN	(VNDR_IE_HDR_LEN + VNDR_IE_MIN_LEN)

#define VNDR_IE_MAX_LEN		255	/* vendor IE max length, without ID and len */

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
#define MEMBER_OF_BRCM_PROP_IE_TYPE		54

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

#define RELMCAST_BRCM_PROP_IE_TYPE	55

/* ************* HT definitions. ************* */
#define MCSSET_LEN	16	/* 16-bits per 8-bit set to give 128-bits bitmap of MCS Index */
#define MAX_MCS_NUM	(128)	/* max mcs number = 128 */

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
	uint8	oui[3];
	uint8	type;           /* type indicates what follows */
	ht_cap_ie_t cap_ie;
} BWL_POST_PACKED_STRUCT;
typedef struct ht_prop_cap_ie ht_prop_cap_ie_t;

#define HT_PROP_IE_OVERHEAD	4	/* overhead bytes for prop oui ie */
#define HT_CAP_IE_LEN		26	/* HT capability len (based on .11n d2.0) */
#define HT_CAP_IE_TYPE		51

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

#define HT_CAP_TXBF_FB_TYPE_NONE 	0
#define HT_CAP_TXBF_FB_TYPE_DELAYED 	1
#define HT_CAP_TXBF_FB_TYPE_IMMEDIATE 	2
#define HT_CAP_TXBF_FB_TYPE_BOTH 	3

#define HT_CAP_TX_BF_CAP_EXPLICIT_CSI_FB_MASK	0x400
#define HT_CAP_TX_BF_CAP_EXPLICIT_CSI_FB_SHIFT	10
#define HT_CAP_TX_BF_CAP_EXPLICIT_COMPRESSED_FB_MASK 0x18000
#define HT_CAP_TX_BF_CAP_EXPLICIT_COMPRESSED_FB_SHIFT 15

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

#define AMPDU_DELIMITER_LEN	4	/* length of ampdu delimiter */
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
	uint8	oui[3];
	uint8	type;		/* indicates what follows */
	ht_add_ie_t add_ie;
} BWL_POST_PACKED_STRUCT;
typedef struct ht_prop_add_ie ht_prop_add_ie_t;

#define HT_ADD_IE_LEN	22
#define HT_ADD_IE_TYPE	52

/* byte1 defn's */
#define HT_BW_ANY		0x04	/* set, STA can use 20 or 40MHz */
#define HT_RIFS_PERMITTED     	0x08	/* RIFS allowed */

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
#define VHT_CAP_INFO_RX_STBC_SHIFT              8
#define VHT_CAP_INFO_SU_BEAMFMR                 0x00000800
#define VHT_CAP_INFO_SU_BEAMFMEE                0x00001000
#define VHT_CAP_INFO_NUM_BMFMR_ANT_MASK         0x0000e000
#define VHT_CAP_INFO_NUM_BMFMR_ANT_SHIFT        13
#define VHT_CAP_INFO_NUM_SOUNDING_DIM_MASK      0x00070000
#define VHT_CAP_INFO_NUM_SOUNDING_DIM_SHIFT     16
#define VHT_CAP_INFO_MU_BEAMFMR                 0x00080000
#define VHT_CAP_INFO_MU_BEAMFMEE                0x00100000
#define VHT_CAP_INFO_TXOPPS                     0x00200000
#define VHT_CAP_INFO_HTCVHT                     0x00400000
#define VHT_CAP_INFO_AMPDU_MAXLEN_EXP_MASK      0x03800000
#define VHT_CAP_INFO_AMPDU_MAXLEN_EXP_SHIFT     23
#define VHT_CAP_INFO_LINK_ADAPT_CAP_MASK        0x0c000000
#define VHT_CAP_INFO_LINK_ADAPT_CAP_SHIFT       26

/* VHT Supported MCS Set - 64-bit - in VHT Cap IE */
#define VHT_CAP_SUPP_MCS_RX_HIGHEST_RATE_MASK   0x1fff
#define VHT_CAP_SUPP_MCS_RX_HIGHEST_RATE_SHIFT  0

#define VHT_CAP_SUPP_MCS_TX_HIGHEST_RATE_MASK   0x1fff
#define VHT_CAP_SUPP_MCS_TX_HIGHEST_RATE_SHIFT  0

#define VHT_CAP_MCS_MAP_0_7                     0
#define VHT_CAP_MCS_MAP_0_8                     1
#define VHT_CAP_MCS_MAP_0_9                     2
#define VHT_CAP_MCS_MAP_NONE                    3
#define VHT_CAP_MCS_MAP_S                       2 /* num bits for 1-stream */
#define VHT_CAP_MCS_MAP_M                       0x3 /* mask for 1-stream */
/* assumes VHT_CAP_MCS_MAP_NONE is 3 and 2 bits are used for encoding */
#define VHT_CAP_MCS_MAP_NONE_ALL                0xffff
/* mcsmap with MCS0-9 for Nss = 3 */
#define VHT_CAP_MCS_MAP_0_9_NSS3 \
	        ((VHT_CAP_MCS_MAP_0_9 << VHT_MCS_MAP_GET_SS_IDX(1)) | \
	         (VHT_CAP_MCS_MAP_0_9 << VHT_MCS_MAP_GET_SS_IDX(2)) | \
	         (VHT_CAP_MCS_MAP_0_9 << VHT_MCS_MAP_GET_SS_IDX(3)))

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
	((mcs_code == VHT_CAP_MCS_MAP_0_7) ? 0xff : \
	 (mcs_code == VHT_CAP_MCS_MAP_0_8) ? 0x1ff : \
	 (mcs_code == VHT_CAP_MCS_MAP_0_9) ? 0x3ff : 0)

/* Map the mcs bit map to mcs code */
#define VHT_MCS_MAP_TO_MCS_CODE(mcs_map) \
	((mcs_map == 0xff)  ? VHT_CAP_MCS_MAP_0_7 : \
	 (mcs_map == 0x1ff) ? VHT_CAP_MCS_MAP_0_8 : \
	 (mcs_map == 0x3ff) ? VHT_CAP_MCS_MAP_0_9 : VHT_CAP_MCS_MAP_NONE)

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
	VHT_OP_CHAN_WIDTH_160	= 2,
	VHT_OP_CHAN_WIDTH_80_80	= 3
} vht_op_chan_width_t;

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
	uint8 oui[3];
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


/* ************* WPA definitions. ************* */
#define WPA_OUI			"\x00\x50\xF2"	/* WPA OUI */
#define WPA_OUI_LEN		3		/* WPA OUI length */
#define WPA_OUI_TYPE		1
#define WPA_VERSION		1		/* WPA version */
#define WPA2_OUI		"\x00\x0F\xAC"	/* WPA2 OUI */
#define WPA2_OUI_LEN		3		/* WPA2 OUI length */
#define WPA2_VERSION		1		/* WPA2 version */
#define WPA2_VERSION_LEN	2		/* WAP2 version length */

/* ************* WPS definitions. ************* */
#define WPS_OUI			"\x00\x50\xF2"	/* WPS OUI */
#define WPS_OUI_LEN		3		/* WPS OUI length */
#define WPS_OUI_TYPE		4

/* ************* WFA definitions. ************* */

#ifdef P2P_IE_OVRD
#define WFA_OUI			MAC_OUI
#else
#define WFA_OUI			"\x50\x6F\x9A"	/* WFA OUI */
#endif /* P2P_IE_OVRD */
#define WFA_OUI_LEN		3		/* WFA OUI length */
#ifdef P2P_IE_OVRD
#define WFA_OUI_TYPE_P2P	MAC_OUI_TYPE_P2P
#else
#define WFA_OUI_TYPE_TPC	8
#define WFA_OUI_TYPE_P2P	9
#endif

#define WFA_OUI_TYPE_TPC	8
#ifdef WLTDLS
#define WFA_OUI_TYPE_TPQ	4	/* WFD Tunneled Probe ReQuest */
#define WFA_OUI_TYPE_TPS	5	/* WFD Tunneled Probe ReSponse */
#define WFA_OUI_TYPE_WFD	10
#endif /* WTDLS */
#define WFA_OUI_TYPE_HS20	0x10
#define WFA_OUI_TYPE_OSEN	0x12
#define WFA_OUI_TYPE_NAN	0x13

/* RSN authenticated key managment suite */
#define RSN_AKM_NONE		0	/* None (IBSS) */
#define RSN_AKM_UNSPECIFIED	1	/* Over 802.1x */
#define RSN_AKM_PSK		2	/* Pre-shared Key */
#define RSN_AKM_FBT_1X		3	/* Fast Bss transition using 802.1X */
#define RSN_AKM_FBT_PSK		4	/* Fast Bss transition using Pre-shared Key */
#define RSN_AKM_MFP_1X		5	/* SHA256 key derivation, using 802.1X */
#define RSN_AKM_MFP_PSK		6	/* SHA256 key derivation, using Pre-shared Key */
#define RSN_AKM_TPK			7	/* TPK(TDLS Peer Key) handshake */

/* OSEN authenticated key managment suite */
#define OSEN_AKM_UNSPECIFIED	RSN_AKM_UNSPECIFIED	/* Over 802.1x */

/* Key related defines */
#define DOT11_MAX_DEFAULT_KEYS	4	/* number of default keys */
#define DOT11_MAX_IGTK_KEYS		2
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
	uint8 len;
	uint16 mdid;		/* Mobility Domain Id */
	uint8 cap;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_mdid_ie dot11_mdid_ie_t;

#define FBT_MDID_CAP_OVERDS	0x01	/* Fast Bss transition over the DS support */
#define FBT_MDID_CAP_RRP	0x02	/* Resource request protocol support */

/** Fast Bss Transition IE */
BWL_PRE_PACKED_STRUCT struct dot11_ft_ie {
	uint8 id;
	uint8 len;
	uint16 mic_control;		/* Mic Control */
	uint8 mic[16];
	uint8 anonce[32];
	uint8 snonce[32];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ft_ie dot11_ft_ie_t;

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

#define BSSID_INVALID           "\x00\x00\x00\x00\x00\x00"
#define BSSID_BROADCAST         "\xFF\xFF\xFF\xFF\xFF\xFF"

#ifdef BCMWAPI_WAI
#define WAPI_IE_MIN_LEN 	20	/* WAPI IE min length */
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
#define TDLS_LINK_ID_IE_LEN		18

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

/* 802.11u interworking access network options */
#define IW_ANT_MASK				0x0f
#define IW_INTERNET_MASK		0x10
#define IW_ASRA_MASK			0x20
#define IW_ESR_MASK				0x40
#define IW_UESA_MASK			0x80

/* 802.11u interworking access network type */
#define IW_ANT_PRIVATE_NETWORK					0
#define IW_ANT_PRIVATE_NETWORK_WITH_GUEST		1
#define IW_ANT_CHARGEABLE_PUBLIC_NETWORK		2
#define IW_ANT_FREE_PUBLIC_NETWORK				3
#define IW_ANT_PERSONAL_DEVICE_NETWORK			4
#define IW_ANT_EMERGENCY_SERVICES_NETWORK		5
#define IW_ANT_TEST_NETWORK						14
#define IW_ANT_WILDCARD_NETWORK					15

/* 802.11u advertisement protocol */
#define ADVP_ANQP_PROTOCOL_ID	0

/* 802.11u advertisement protocol masks */
#define ADVP_QRL_MASK					0x7f
#define ADVP_PAME_BI_MASK				0x80

/* 802.11u advertisement protocol values */
#define ADVP_QRL_REQUEST				0x00
#define ADVP_QRL_RESPONSE				0x7f
#define ADVP_PAME_BI_DEPENDENT			0x00
#define ADVP_PAME_BI_INDEPENDENT		ADVP_PAME_BI_MASK

/* 802.11u ANQP information ID */
#define ANQP_ID_QUERY_LIST							256
#define ANQP_ID_CAPABILITY_LIST						257
#define ANQP_ID_VENUE_NAME_INFO						258
#define ANQP_ID_EMERGENCY_CALL_NUMBER_INFO			259
#define ANQP_ID_NETWORK_AUTHENTICATION_TYPE_INFO	260
#define ANQP_ID_ROAMING_CONSORTIUM_LIST				261
#define ANQP_ID_IP_ADDRESS_TYPE_AVAILABILITY_INFO	262
#define ANQP_ID_NAI_REALM_LIST						263
#define ANQP_ID_G3PP_CELLULAR_NETWORK_INFO			264
#define ANQP_ID_AP_GEOSPATIAL_LOCATION				265
#define ANQP_ID_AP_CIVIC_LOCATION					266
#define ANQP_ID_AP_LOCATION_PUBLIC_ID_URI			267
#define ANQP_ID_DOMAIN_NAME_LIST					268
#define ANQP_ID_EMERGENCY_ALERT_ID_URI				269
#define ANQP_ID_EMERGENCY_NAI						271
#define ANQP_ID_VENDOR_SPECIFIC_LIST				56797

/* 802.11u ANQP OUI */
#define ANQP_OUI_SUBTYPE	9

/* 802.11u venue name */
#define VENUE_LANGUAGE_CODE_SIZE		3
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
#define NATI_UNSPECIFIED							-1
#define NATI_ACCEPTANCE_OF_TERMS_CONDITIONS			0
#define NATI_ONLINE_ENROLLMENT_SUPPORTED			1
#define NATI_HTTP_HTTPS_REDIRECTION					2
#define NATI_DNS_REDIRECTION						3

/* 802.11u IP address type availability - IPv6 */
#define IPA_IPV6_SHIFT						0
#define IPA_IPV6_MASK						(0x03 << IPA_IPV6_SHIFT)
#define	IPA_IPV6_NOT_AVAILABLE				0x00
#define IPA_IPV6_AVAILABLE					0x01
#define IPA_IPV6_UNKNOWN_AVAILABILITY		0x02

/* 802.11u IP address type availability - IPv4 */
#define IPA_IPV4_SHIFT						2
#define IPA_IPV4_MASK						(0x3f << IPA_IPV4_SHIFT)
#define	IPA_IPV4_NOT_AVAILABLE				0x00
#define IPA_IPV4_PUBLIC						0x01
#define IPA_IPV4_PORT_RESTRICT				0x02
#define IPA_IPV4_SINGLE_NAT					0x03
#define IPA_IPV4_DOUBLE_NAT					0x04
#define IPA_IPV4_PORT_RESTRICT_SINGLE_NAT	0x05
#define IPA_IPV4_PORT_RESTRICT_DOUBLE_NAT	0x06
#define IPA_IPV4_UNKNOWN_AVAILABILITY		0x07

/* 802.11u NAI realm encoding */
#define REALM_ENCODING_RFC4282	0
#define REALM_ENCODING_UTF8		1

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
#define REALM_EXPANDED_EAP						1
#define REALM_NON_EAP_INNER_AUTHENTICATION		2
#define REALM_INNER_AUTHENTICATION_EAP			3
#define REALM_EXPANDED_INNER_EAP				4
#define REALM_CREDENTIAL						5
#define REALM_TUNNELED_EAP_CREDENTIAL			6
#define REALM_VENDOR_SPECIFIC_EAP				221

/* 802.11u non-EAP inner authentication type */
#define REALM_RESERVED_AUTH			0
#define REALM_PAP					1
#define REALM_CHAP					2
#define REALM_MSCHAP				3
#define REALM_MSCHAPV2				4

/* 802.11u credential type */
#define REALM_SIM					1
#define REALM_USIM					2
#define REALM_NFC					3
#define REALM_HARDWARE_TOKEN		4
#define REALM_SOFTOKEN				5
#define REALM_CERTIFICATE			6
#define REALM_USERNAME_PASSWORD		7
#define REALM_SERVER_SIDE			8
#define REALM_RESERVED_CRED			9
#define REALM_VENDOR_SPECIFIC_CRED	10

/* 802.11u 3GPP PLMN */
#define G3PP_GUD_VERSION		0
#define G3PP_PLMN_LIST_IE		0

/** hotspot2.0 indication element (vendor specific) */
BWL_PRE_PACKED_STRUCT struct hs20_ie {
	uint8 oui[3];
	uint8 type;
	uint8 config;
} BWL_POST_PACKED_STRUCT;
typedef struct hs20_ie hs20_ie_t;
#define HS20_IE_LEN 5	/* HS20 IE length */

/** IEEE 802.11 Annex E */
typedef enum {
	DOT11_2GHZ_20MHZ_CLASS_12		= 81,	/* Ch 1-11			 */
	DOT11_5GHZ_20MHZ_CLASS_1		= 115,	/* Ch 36-48			 */
	DOT11_5GHZ_20MHZ_CLASS_2_DFS	= 118,	/* Ch 52-64			 */
	DOT11_5GHZ_20MHZ_CLASS_3		= 124,	/* Ch 149-161		 */
	DOT11_5GHZ_20MHZ_CLASS_4_DFS	= 121,	/* Ch 100-140		 */
	DOT11_5GHZ_20MHZ_CLASS_5		= 125,	/* Ch 149-165		 */
	DOT11_5GHZ_40MHZ_CLASS_22		= 116,	/* Ch 36-44,   lower */
	DOT11_5GHZ_40MHZ_CLASS_23_DFS 	= 119,	/* Ch 52-60,   lower */
	DOT11_5GHZ_40MHZ_CLASS_24_DFS	= 122,	/* Ch 100-132, lower */
	DOT11_5GHZ_40MHZ_CLASS_25		= 126,	/* Ch 149-157, lower */
	DOT11_5GHZ_40MHZ_CLASS_27		= 117,	/* Ch 40-48,   upper */
	DOT11_5GHZ_40MHZ_CLASS_28_DFS	= 120,	/* Ch 56-64,   upper */
	DOT11_5GHZ_40MHZ_CLASS_29_DFS	= 123,	/* Ch 104-136, upper */
	DOT11_5GHZ_40MHZ_CLASS_30		= 127,	/* Ch 153-161, upper */
	DOT11_2GHZ_40MHZ_CLASS_32		= 83,	/* Ch 1-7,     lower */
	DOT11_2GHZ_40MHZ_CLASS_33		= 84,	/* Ch 5-11,    upper */
} dot11_op_class_t;

/* QoS map */
#define QOS_MAP_FIXED_LENGTH	(8 * 2)	/* DSCP ranges fixed with 8 entries */

#define BCM_AIBSS_IE_TYPE 56

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif /* _802_11_H_ */
