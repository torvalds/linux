/*
 * Basic types and constants relating to 802.11ax/HE STA
 * This is a portion of 802.11ax definition. The rest are in 802.11.h.
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

#ifndef _802_11ax_h_
#define _802_11ax_h_

#include <typedefs.h>
#include <802.11.h>
#include <bcmtlv.h>

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

/* HT Control Field: (Table 9-9a) */
#define HTC_HE_VARIANT		0x03u
#define HTC_HEVAR_SHIFT		0	/* HE VARIANT shift */
#define HTC_HEVAR(htc)		(((htc) & HTC_HE_VARIANT) >> HTC_HEVAR_SHIFT)

/* HT Control IDs: (Table 9-18a & Table 9-9a) */
#define HTC_HE_CTLID_SHIFT	0x02u	/* HTC HE CTLID shift */
#define HTC_HE_CTLID_MASK	0x0Fu	/* HTC HE CTLID mask  */
#define HTC_HE_CTLID(htc)	(((htc) >> HTC_HE_CTLID_SHIFT) & HTC_HE_CTLID_MASK)

#define HTC_HE_CTLID_TRS	0x0u	/* Triggered response scheduling */
#define HTC_HE_CTLID_OMI	0x1u	/* Operating mode */
#define HTC_HE_CTLID_HLA	0x2u	/* HE link adaptation */
#define HTC_HE_CTLID_BSR	0x3u	/* Buffer status report */
#define HTC_HE_CTLID_UPH	0x4u	/* UL power headroom */
#define HTC_HE_CTLID_BQR	0x5u	/* Bandwidth query report */
#define HTC_HE_CTLID_CAS	0x6u	/* Command and status */

/* HTC-Control field definitions: (Table 9.9a HTC Control field) */
#define HTC_HE_CTL_SIZE		30u	/* HTC Control field size */
#define HTC_HE_CTL_DEFAULT	0xFFFFFFFC

/* A-Control offset definitions: (Figure 9.18a Control ID subfield values) */
#define HE_ACTRL_TRS_FSZ	26u
#define HE_ACTRL_OMI_FSZ	12u
#define HE_ACTRL_HLA_FSZ	26u
#define HE_ACTRL_BSR_FSZ	26u
#define HE_ACTRL_UPH_FSZ	8u
#define HE_ACTRL_BQR_FSZ	10u
#define HE_ACTRL_CAS_FSZ	8u

/* OM-Control Field definitions: (Figure 9.15d Control Information subfield for OM Control) */
#define HE_OMI_RXNSS_FSZ		3
#define HE_OMI_RXNSS_IDX		0
#define HE_OMI_RXNSS_MASK		0x07u
#define HE_OMI_CHW_FSZ			2
#define HE_OMI_CHW_IDX			3
#define HE_OMI_CHW_MASK			0x18u
#define HE_OMI_ULMU_DIS_FSZ		1
#define HE_OMI_ULMU_DIS_IDX		5
#define HE_OMI_ULMU_DIS_MASK		0x20u
#define HE_OMI_TXNSTS_FSZ		3
#define HE_OMI_TXNSTS_IDX		6
#define HE_OMI_TXNSTS_MASK		0x1c0u
#define HE_OMI_ERSU_DIS_FSZ		1
#define HE_OMI_ERSU_DIS_IDX		9
#define HE_OMI_ERSU_DIS_MASK		0x200u
#define HE_OMI_DLMU_RSD_RCM_FSZ		1
#define HE_OMI_DLMU_RSD_RCM_IDX		10
#define HE_OMI_DLMU_RSD_RCM_MASK	0x400u
#define HE_OMI_ULMU_DATA_DIS_FSZ	1
#define HE_OMI_ULMU_DATA_DIS_IDX	11
#define HE_OMI_ULMU_DATA_DIS_MASK	0x800u

/* OM-Control Channel Width Subfield definition, as per 9.2.4.6a.2 OM Control */
#define OMI_CHW_20MHZ			0
#define OMI_CHW_40MHZ			1
#define OMI_CHW_80MHZ			2
#define OMI_CHW_160MHZ_80P80MHZ		3

/* Table 9-18d ACI Bitmap subfield encoding */
#define HE_BSR_ACI_MAP_BE		0u
#define HE_BSR_ACI_MAP_BK		1u
#define HE_BSR_ACI_MAP_VI		2u
#define HE_BSR_ACI_MAP_VO		3u

/* GI And LTF Type subfield encoding (Table 9-31d) */
#define HE_LTF_1_GI_1_6us	(0u)
#define HE_LTF_2_GI_1_6us	(1u)
#define HE_LTF_4_GI_3_2us	(2u)

/* special STA-IDs (Section 27.11.1) */
#define HE_STAID_BSS_BCAST		0
#define HE_STAID_UNASSOCIATED_STA	2045u
#define HE_STAID_NO_USER		2046u
#define HE_STAID_MBSS_BCAST		2047u
#define HE_STAID_MASK			0x07FFu
#define HE_AID12_MASK			0x0FFFu

/**
 * HE Capabilites element (sec 9.4.2.218)
 */

/* HE MAC Capabilities Information field (figure 9-589ck) */
#define HE_MAC_CAP_INFO_SIZE	6u
typedef uint8 he_mac_cap_t[HE_MAC_CAP_INFO_SIZE];

/* bit position and field width */
#define HE_MAC_HTC_HE_SUPPORT_IDX		0	/* HTC HE Support */
#define HE_MAC_HTC_HE_SUPPORT_FSZ		1
#define HE_MAC_TWT_REQ_SUPPORT_IDX		1	/* TWT Requestor Support */
#define HE_MAC_TWT_REQ_SUPPORT_FSZ		1
#define HE_MAC_TWT_RESP_SUPPORT_IDX		2	/* TWT Responder Support */
#define HE_MAC_TWT_RESP_SUPPORT_FSZ		1
#define HE_MAC_FRAG_SUPPORT_IDX			3	/* Fragmentation Support */
#define HE_MAC_FRAG_SUPPORT_FSZ			2
#define HE_MAC_MAX_MSDU_FRAGS_IDX		5	/* Max. Fragmented MSDUs */
#define HE_MAC_MAX_MSDU_FRAGS_FSZ		3
#define HE_MAC_MIN_FRAG_SIZE_IDX		8	/* Min. Fragment Size */
#define HE_MAC_MIN_FRAG_SIZE_FSZ		2
#define HE_MAC_TRIG_MAC_PAD_DUR_IDX		10	/* Trigger Frame MAC Pad Dur */
#define HE_MAC_TRIG_MAC_PAD_DUR_FSZ		2
#define HE_MAC_MULTI_TID_AGG_RX_IDX		12	/* Multi TID Agg. Rx support */
#define HE_MAC_MULTI_TID_AGG_RX_FSZ		3
#define HE_MAC_LINK_ADAPT_IDX			15	/* HE Link Adaptation Support */
#define HE_MAC_LINK_ADAPT_FSZ			2
#define HE_MAC_ALL_ACK_SUPPORT_IDX		17	/* All Ack Support */
#define HE_MAC_ALL_ACK_SUPPORT_FSZ		1
#define HE_MAC_TRS_SUPPORT_IDX			18	/* TRS Support */
#define HE_MAC_TRS_SUPPORT_FSZ			1
#define HE_MAC_BSR_SUPPORT_IDX			19	/* BSR Support */
#define HE_MAC_BSR_SUPPORT_FSZ			1
#define HE_MAC_BCAST_TWT_SUPPORT_IDX		20	/* Broadcast TWT Support */
#define HE_MAC_BCAST_TWT_SUPPORT_FSZ		1
#define HE_MAC_32BA_BITMAP_SUPPORT_IDX		21	/* 32-bit BA Bitmap Support */
#define HE_MAC_32BA_BITMAP_SUPPORT_FSZ		1
#define HE_MAC_MU_CASCADE_SUPPORT_IDX		22	/* MU Cascade Support */
#define HE_MAC_MU_CASCADE_SUPPORT_FSZ		1
#define HE_MAC_ACK_ENAB_AGG_SUPPORT_IDX		23	/* Ack Enabled Agg. Support */
#define HE_MAC_ACK_ENAB_AGG_SUPPORT_FSZ		1
/* bit 24 - Reserved */
#define HE_MAC_OMI_CONTROL_SUPPORT_IDX		25	/* OMI Control Support */
#define HE_MAC_OMI_CONTROL_SUPPORT_FSZ		1
#define HE_MAC_OFDMA_RA_SUPPORT_IDX		26	/* OFDMA RA Support */
#define HE_MAC_OFDMA_RA_SUPPORT_FSZ		1
#define HE_MAC_MAX_AMPDU_LEN_EXP_IDX		27	/* Max AMPDU Length Exponent */
#define HE_MAC_MAX_AMPDU_LEN_EXP_FSZ		2
#define HE_MAC_AMSDU_FRAG_SUPPORT_IDX		29	/* AMSDU Fragementation Support */
#define HE_MAC_AMSDU_FRAG_SUPPORT_FSZ		1
#define HE_MAC_FLEX_TWT_SCHEDULE_IDX		30	/* Flexible TWT Schedule Support */
#define HE_MAC_FLEX_TWT_SCHEDULE_FSZ		1
#define HE_MAC_RX_MBSS_CTL_FRAME_IDX		31	/* Rx control frames to Multi BSS */
#define HE_MAC_RX_MBSS_CTL_FRAME_FSZ		1
#define HE_MAC_RX_AGG_BSRP_BQRP_IDX		32	/* Aggregated BSRP BQRP Rx */
#define HE_MAC_RX_AGG_BSRP_BQRP_FSZ		1
#define HE_MAC_QTP_SUPPORT_IDX			33	/* Support Quiet time period */
#define HE_MAC_QTP_SUPPORT_FSZ			1
#define HE_MAC_BQR_SUPPORT_IDX			34	/* Support BQR */
#define HE_MAC_BQR_SUPPORT_FSZ			1
#define HE_MAC_SRP_RESPONDER_IDX		35	/* SRP responder Support */
#define HE_MAC_SRP_RESPONDER_FSZ		1
#define HE_MAC_NDP_FDBK_SUPPORT_IDX		36	/* NDP feedback report Support */
#define HE_MAC_NDP_FDBK_SUPPORT_FSZ		1
#define HE_MAC_OPS_SUPPORT_IDX			37	/* OPS support */
#define HE_MAC_OPS_SUPPORT_FSZ			1
#define HE_MAC_AMSDU_IN_AMPDU_IDX		38	/* AMSDU in AMPDU support */
#define HE_MAC_AMSDU_IN_AMPDU_FSZ		1
#define HE_MAC_MULTI_TID_AGG_TX_IDX		39	/* Multi TID Agg. Tx support */
#define HE_MAC_MULTI_TID_AGG_TX_FSZ		3
#define HE_MAC_SST_SUPPORT_IDX			42	/* Sub-channel Selective channel */
#define HE_MAC_SST_SUPPORT_FSZ			1
#define HE_MAC_UL_2X_996_TONE_RU_SUPP_IDX	43	/* UL 2X 996 tone RU Support */
#define HE_MAC_UL_2X_996_TONE_RU_SUPP_FSZ	1
#define HE_MAC_UL_MU_DATA_DISABLE_RX_IDX	44	/* OM - UL MU Data Disable RX */
#define HE_MAC_UL_MU_DATA_DISABLE_RX_FSZ	1
#define HE_MAC_DYNAMIC_SM_PWR_SAVE_IDX		45	/* HE Dynamic SM Power Save */
#define HE_MAC_DYNAMIC_SM_PWR_SAVE_FSZ		1
#define HE_MAC_PUNCT_SOUNDING_SUPP_IDX		46	/* Punctured Sounding Support */
#define HE_MAC_PUNCT_SOUNDING_SUPP_FSZ		1
#define HE_MAC_HT_VHT_TRIG_FRAME_RX_IDX		47	/* HT And VHT Trigger Frame RX Support */
#define HE_MAC_HT_VHT_TRIG_FRAME_RX_FSZ		1

/* HE PHY Capabilities Information field (figure 9-589cl) */
#define HE_PHY_CAP_INFO_SIZE			11u
typedef uint8 he_phy_cap_t[HE_PHY_CAP_INFO_SIZE];

/* bit position and field width */
/* bit 0 - Reserved */
#define HE_PHY_CH_WIDTH_SET_IDX			1	/* Channel Width Set */
#define HE_PHY_CH_WIDTH_SET_FSZ			7
#define HE_PHY_PUNCT_PREAMBLE_RX_IDX		8	/* Punctured Preamble Rx */
#define HE_PHY_PUNCT_PREAMBLE_RX_FSZ		4
#define HE_PHY_DEVICE_CLASS_IDX			12	/* Device Class */
#define HE_PHY_DEVICE_CLASS_FSZ			1
#define HE_PHY_LDPC_PYLD_IDX			13	/* LDPC Coding In Payload */
#define HE_PHY_LDPC_PYLD_FSZ			1
#define HE_PHY_SU_PPDU_1x_LTF_0_8_GI_IDX	14	/* SU PPDU 1x LTF GI 0.8 us */
#define HE_PHY_SU_PPDU_1x_LTF_0_8_GI_FSZ	1
#define HE_PHY_MIDAMBLE_MAX_NSTS_IDX		15	/* Midamble Tx/Rx Max NSTS */
#define HE_PHY_MIDAMBLE_MAX_NSTS_FSZ		2
#define HE_PHY_NDP_4x_LTF_3_2_GI_IDX		17	/* NDP with 4xLTF 3.2us GI */
#define HE_PHY_NDP_4x_LTF_3_2_GI_FSZ		1
#define HE_PHY_STBC_TX_IDX			18	/* STBC Tx for <= 80 MHz */
#define HE_PHY_STBC_TX_FSZ			1
#define HE_PHY_STBC_RX_IDX			19	/* STBC Rx for <= 80 MHz */
#define HE_PHY_STBC_RX_FSZ			1
#define HE_PHY_DOPPLER_TX_IDX			20	/* Doppler Tx */
#define HE_PHY_DOPPLER_TX_FSZ			1
#define HE_PHY_DOPPLER_RX_IDX			21	/* Doppler Rx */
#define HE_PHY_DOPPLER_RX_FSZ			1
#define HE_PHY_FULL_BW_UL_MU_MIMO_IDX		22	/* Full bandwidth UL MU MIMO */
#define HE_PHY_FULL_BW_UL_MU_MIMO_FSZ		1
#define HE_PHY_PART_BW_UL_MU_MIMO_IDX		23	/* Partial bandwidth UL MU MIMO */
#define HE_PHY_PART_BW_UL_MU_MIMO_FSZ		1
#define HE_PHY_DCM_MAX_CONST_TX_IDX		24	/* DCM Max Constellation Tx */
#define HE_PHY_DCM_MAX_CONST_TX_FSZ		2
#define HE_PHY_DCM_MAX_NSS_TX_IDX		26	/* DCM Max NSS Tx */
#define HE_PHY_DCM_MAX_NSS_TX_FSZ		1
#define HE_PHY_DCM_MAX_CONST_RX_IDX		27	/* DCM Max Constellation Rx */
#define HE_PHY_DCM_MAX_CONST_RX_FSZ		2
#define HE_PHY_DCM_MAX_NSS_RX_IDX		29	/* DCM Max NSS Rx */
#define HE_PHY_DCM_MAX_NSS_RX_FSZ		1
#define HE_PHY_RX_MU_PPDU_IDX			30	/* Rx HE MU PPDU From nonAP STA */
#define HE_PHY_RX_MU_PPDU_FSZ			1
#define HE_PHY_SU_BEAMFORMER_IDX		31	/* SU Beamformer */
#define HE_PHY_SU_BEAMFORMER_FSZ		1
#define HE_PHY_SU_BEAMFORMEE_IDX		32	/* SU Beamformee */
#define HE_PHY_SU_BEAMFORMEE_FSZ		1
#define HE_PHY_MU_BEAMFORMER_IDX		33	/* MU Beamformer */
#define HE_PHY_MU_BEAMFORMER_FSZ		1
#define HE_PHY_BEAMFORMEE_STS_BELOW80MHZ_IDX	34	/* Beamformee STS For <= 80MHz */
#define HE_PHY_BEAMFORMEE_STS_BELOW80MHZ_FSZ	3
#define HE_PHY_BEAMFORMEE_STS_ABOVE80MHZ_IDX	37	/* Beamformee STS For >80 MHz */
#define HE_PHY_BEAMFORMEE_STS_ABOVE80MHZ_FSZ	3
#define HE_PHY_SOUND_DIM_BELOW80MHZ_IDX		40	/* Num. Sounding Dim.<= 80 MHz */
#define HE_PHY_SOUND_DIM_BELOW80MHZ_FSZ		3
#define HE_PHY_SOUND_DIM_ABOVE80MHZ_IDX		43	/* Num. Sounding Dim.> 80 MHz */
#define HE_PHY_SOUND_DIM_ABOVE80MHZ_FSZ		3
#define HE_PHY_SU_FEEDBACK_NG16_SUPPORT_IDX	46	/* Ng=16 For SU Feedback */
#define HE_PHY_SU_FEEDBACK_NG16_SUPPORT_FSZ	1
#define HE_PHY_MU_FEEDBACK_NG16_SUPPORT_IDX	47	/* Ng=16 For MU Feedback */
#define HE_PHY_MU_FEEDBACK_NG16_SUPPORT_FSZ	1
#define HE_PHY_SU_CODEBOOK_SUPPORT_IDX		48	/* Codebook Sz {4, 2} For SU */
#define HE_PHY_SU_CODEBOOK_SUPPORT_FSZ		1
#define HE_PHY_MU_CODEBOOK_SUPPORT_IDX		49	/* Codebook Size {7, 5} For MU */
#define HE_PHY_MU_CODEBOOK_SUPPORT_FSZ		1
#define HE_PHY_TRG_SU_BFM_FEEDBACK_IDX		50	/* Triggered SU TXBF Feedback */
#define HE_PHY_TRG_SU_BFM_FEEDBACK_FSZ		1
#define HE_PHY_TRG_MU_BFM_FEEDBACK_IDX		51	/* Triggered MU TXBF partial BW Feedback */
#define HE_PHY_TRG_MU_BFM_FEEDBACK_FSZ		1
#define HE_PHY_TRG_CQI_FEEDBACK_IDX		52	/* Triggered CQI Feedback */
#define HE_PHY_TRG_CQI_FEEDBACK_FSZ		1
#define HE_PHY_PART_BW_EXT_RANGE_IDX		53	/* Partial BW Extended Range */
#define HE_PHY_PART_BW_EXT_RANGE_FSZ		1
#define HE_PHY_DL_MU_MIMO_PART_BW_IDX		54	/* Partial Bandwidth DL MU MIMO */
#define HE_PHY_DL_MU_MIMO_PART_BW_FSZ		1
#define HE_PHY_PPE_THRESH_PRESENT_IDX		55	/* PPE Threshold Present */
#define HE_PHY_PPE_THRESH_PRESENT_FSZ		1
#define HE_PHY_SRP_SR_SUPPORT_IDX		56	/* SRP based SR Support */
#define HE_PHY_SRP_SR_SUPPORT_FSZ		1
#define HE_PHY_POWER_BOOST_FACTOR_IDX		57	/* Power Boost Factor Support */
#define HE_PHY_POWER_BOOST_FACTOR_FSZ		1
#define HE_PHY_4X_LTF_0_8_GI_SUPPORT_IDX	58	/* HE SU PPDU And HE MU PPDU with
							* 4x HE-LTF And 0.8 us GI
							*/
#define HE_PHY_4X_LTF_0_8_GI_SUPPORT_FSZ	1
#define HE_PHY_MAX_NC_IDX			59	/* Maximum NC */
#define HE_PHY_MAX_NC_FSZ			3
#define HE_PHY_STBC_TX_ABOVE_80_IDX		62	/* STBC Tx above 80 MHz */
#define HE_PHY_STBC_TX_ABOVE_80_FSZ		1
#define HE_PHY_STBC_RX_ABOVE_80_IDX		63	/* STBC Rx above 80 MHz */
#define HE_PHY_STBC_RX_ABOVE_80_FSZ		1
#define HE_PHY_ER_SU_4X_LTF_0_8_GI_IDX		64	/* ER SU PPDU 4x HE-LTF 0.8 GI */
#define HE_PHY_ER_SU_4X_LTF_0_8_GI_FSZ		1
#define HE_PHY_20_IN_40_2G_IDX			65	/* 20 in 40 MHz HE PPDU in 2G */
#define HE_PHY_20_IN_40_2G_FSZ			1
#define HE_PHY_20_IN_160_80P80_IDX		66	/* 20 in 160/80+80 MHz HE PPDU */
#define HE_PHY_20_IN_160_80P80_FSZ		1
#define HE_PHY_80_IN_160_80P80_IDX		67	/* 80 in 160/80+80 MHz HE PPDU */
#define HE_PHY_80_IN_160_80P80_FSZ		1
#define HE_PHY_ER_SU_1X_LTF_0_8_GI_IDX		68	/* HE ER SU 1x HE-LTF 0.8 GI */
#define HE_PHY_ER_SU_1X_LTF_0_8_GI_FSZ		1
#define HE_PHY_MIDAMBLE_2X_1X_LTF_IDX		69	/* Midamble TX/RX 2x & 1x HE LTF */
#define HE_PHY_MIDAMBLE_2X_1X_LTF_FSZ		1
#define HE_PHY_DCM_MAX_BW_IDX			70	/* DCM Max BW */
#define HE_PHY_DCM_MAX_BW_FSZ			2
#define HE_PHY_ABOVE16_OFDM_SYM_IDX		72	/* Longer than 16 HE-SIGB OFDM
							 * Symbol support
							 */
#define HE_PHY_ABOVE16_OFDM_SYM_FSZ		1
#define HE_PHY_NON_TRIG_CQI_FDBK_IDX		73	/* Non-triggered CQI feedback Support */
#define HE_PHY_NON_TRIG_CQI_FDBK_FSZ		1
#define HE_PHY_1024_QAM_TX_BELOW_242_RU_IDX	74	/* Tx 1024 QAM in < 242 RU Tone Support */
#define HE_PHY_1024_QAM_TX_BELOW_242_RU_FSZ	1
#define HE_PHY_1024_QAM_RX_BELOW_242_RU_IDX	75	/* Rx 1024 QAM in < 242 RU Tone Support */
#define HE_PHY_1024_QAM_RX_BELOW_242_RU_FSZ	1
#define HE_PHY_RX_FULL_BW_MU_COMP_SIGB_IDX	76	/* Rx Full BW MU PPDU with Comp. SIGB */
#define HE_PHY_RX_FULL_BW_MU_COMP_SIGB_FSZ	1
#define HE_PHY_RX_FULL_BW_MU_NON_COMP_SIGB_IDX	77	/* Rx Full BW MU PPDU Non-Comp SIGB */
#define HE_PHY_RX_FULL_BW_MU_NON_COMP_SIGB_FSZ	1

/* HE Mac Capabilities values */
/* b3-b4: Fragmentation Support field (table 9-262z) */
#define HE_MAC_FRAG_NOSUPPORT		0	/* dynamic fragmentation not supported */
#define HE_MAC_FRAG_PER_MPDU		1	/* dynamic fragmentation of MPDU/SMPDU */
#define HE_MAC_FRAG_ONE_PER_AMPDU	2	/* upto 1 fragment per AMPDU/MMPDU */
#define HE_MAC_FRAG_MULTI_PER_AMPDU	3	/* multiple fragment per AMPDU */

/* b5-b7 : Maximum Number Of Fragmented MSDUs/AMSDUs Exponent */
#define HE_MAC_MAXFRAG_NUM_NO_RESTRICT	7

/* b8-b9: Minimum payload size of first fragment */
#define HE_MAC_MINFRAG_NO_RESTRICT	0	/* no restriction on min. payload size */
#define HE_MAC_MINFRAG_SIZE_128		1	/* minimum payload size of 128 Bytes */
#define HE_MAC_MINFRAG_SIZE_256		2	/* minimum payload size of 256 Bytes */
#define HE_MAC_MINFRAG_SIZE_512		3	/* minimum payload size of 512 Bytes */

/* b10-b11: Trigger Frame MAC Padding Duration */
#define HE_MAC_TRIG_MAC_PAD_0		0
#define HE_MAC_TRIG_MAC_PAD_8us		1
#define HE_MAC_TRIG_MAC_PAD_16us	2

/* b15-b16: HE Link Adaptation */
#define HE_MAC_SEND_NO_MFB		0	/* if STA does not provide HE MFB */
#define HE_MAC_SEND_UNSOLICATED_MFB	2	/* if STA provides unsolicited HE MFB */
#define HE_MAC_SEND_MFB_IN_RESPONSE	3	/* if STA can provide HE MFB in response to
						* HE MRQ and if the STA provides unsolicited HE MFB.
						*/

/* b27-b28: Max. AMPDU Length HE Exponent */
/* Use Max AMPDU length exponent from VHT or HT */
#define HE_MAC_MAX_AMPDU_EXP_ADOPT_VHT	(0)
/* Max. AMPDU length =
 * 2^(20 + MAX_AMPDU_LEN_HE_EXPO_1) -1 (if this value in VHT CAP is 7) or
 * 2^(16 + MAX_AMPDU_LEN_HE_EXPO_1) -1 (if this value in HT CAP is 3).
 */
#define HE_MAC_MAX_AMPDU_EXP_HE_1	(1)
/* Max. AMPDU length =
 * 2^(20 + MAX_AMPDU_LEN_HE_EXPO_2) -1 (if this value in VHT CAP is 7) or
 * 2^(16 + MAX_AMPDU_LEN_HE_EXPO_2) -1 (if this value in HT CAP is 3).
 */
#define HE_MAC_MAX_AMPDU_EXP_HE_2	(2)
/* Max. AMPDU length =
 * 2^(20 + MAX_AMPDU_LEN_HE_EXPO_3) -1 (if this value in VHT CAP is 7) or
 * 2^(16 + MAX_AMPDU_LEN_HE_EXPO_3) -1 (if this value in HT CAP is 3).
 */
#define HE_MAC_MAX_AMPDU_EXP_HE_3	(3)

/* HE PHY Capabilities values */
/* b1-b7: Channel Width Support field */
#define HE_PHY_CH_WIDTH_2G_40		0x01
#define HE_PHY_CH_WIDTH_5G_80		0x02
#define HE_PHY_CH_WIDTH_5G_160		0x04
#define HE_PHY_CH_WIDTH_5G_80P80	0x08
#define HE_PHY_CH_WIDTH_2G_242RU	0x10
#define HE_PHY_CH_WIDTH_5G_242RU	0x20

/* b8-b11: Preamble puncturing Rx */
/* Rx of 80 MHz preamble where secondary 20 MHz subchannel is punctured */
#define HE_PHY_PREAMBLE_PUNC_RX_0	0x1
/* Rx of 80 MHz preamble where one of two 20 MHz subchannels in secondary 40 MHz is punctured */
#define HE_PHY_PREAMBLE_PUNC_RX_1	0x2
/* Rx of 160 MHz or 80+80 MHz preamble where primary 80 MHz of
 * preamble only the secondary 20 MHz is punctured
 */
#define HE_PHY_PREAMBLE_PUNC_RX_2	0x4
/* Rx of 160 MHz or 80+80 MHz preamble where primary 80 MHz of
 * the preamble, the primary 40 MHz is present
 */
#define HE_PHY_PREAMBLE_PUNC_RX_3	0x8

/* b24-b26: DCM Encoding Tx */
#define HE_PHY_TX_DCM_ENC_NOSUPPORT	0x00
#define HE_PHY_TX_DCM_ENC_BPSK		0x01
#define HE_PHY_TX_DCM_ENC_QPSK		0x02
#define HE_PHY_TX_DCM_ENC_QAM		0x03

#define HE_PHY_TX_DCM_1_SS		0x00
#define HE_PHY_TX_DCM_2_SS		0x01

/* b27-b29: DCM Encoding Rx */
#define HE_PHY_RX_DCM_ENC_NOSUPPORT	0x00
#define HE_PHY_RX_DCM_ENC_BPSK		0x01
#define HE_PHY_RX_DCM_ENC_QPSK		0x02
#define HE_PHY_RX_DCM_ENC_QAM		0x03

#define HE_PHY_RX_DCM_1_SS		0x00
#define HE_PHY_RX_DCM_2_SS		0x01

/* b70-b71: DCM Max BW */
#define HE_PHY_DCM_MAX_BW_20		0
#define HE_PHY_DCM_MAX_BW_40		1
#define HE_PHY_DCM_MAX_BW_80		2
#define HE_PHY_DCM_MAX_BW_160		3

/* HE Duration based RTS Threshold Figure 9-589cr */
#define HE_RTS_THRES_DISABLED		1023
#define HE_RTS_THRES_ALL_FRAMES		0
#define HE_RTS_THRES_MASK		0x03ff

/* Tx Rx HE MCS Support field format : Table 9-589cm */
#define HE_TX_RX_MCS_NSS_SUP_FIELD_MIN_SIZE	4u

/**
* Bandwidth configuration indices used in the HE TX-RX MCS support field
* Section 9.4.2.218.4
*/
#define HE_BW20_CFG_IDX		0
#define HE_BW40_CFG_IDX		1
#define HE_BW80_CFG_IDX		2
#define HE_BW80P80_CFG_IDX	3
#define HE_BW160_CFG_IDX	4
#define HE_MAX_BW_CFG		5

#define HE_MCS_CODE_0_7		0u
#define HE_MCS_CODE_0_9		1u
#define HE_MCS_CODE_0_11	2u
#define HE_MCS_CODE_NONE	3u
#define HE_MCS_CODE_SIZE	2u	/* num bits */
#define HE_MCS_CODE_MASK	0x3u	/* mask for 1-stream */

/* Defines for The Max HE MCS For n SS subfield (where n = 1, ..., 8) */
#define HE_MCS_MAP_NSS_MAX	8u	/* Max number of streams possible */
#define HE_MCS_NSS_SET_MASK	0xffffu /* Field is to be 16 bits long */
#define HE_MCS_NSS_GET_SS_IDX(nss) (((nss)-1u) * HE_MCS_CODE_SIZE)
#define HE_MCS_NSS_GET_MCS(nss, mcs_nss_map) \
	(((mcs_nss_map) >> HE_MCS_NSS_GET_SS_IDX(nss)) & HE_MCS_CODE_MASK)
#define HE_MCS_NSS_SET_MCS(nss, mcs_code, mcs_nss_map) \
	do { \
	(mcs_nss_map) &= (~(HE_MCS_CODE_MASK << HE_MCS_NSS_GET_SS_IDX(nss))); \
	(mcs_nss_map) |= (((mcs_code) & HE_MCS_CODE_MASK) << HE_MCS_NSS_GET_SS_IDX(nss)); \
	(mcs_nss_map) &= (HE_MCS_NSS_SET_MASK); \
	} while (0)

#define HE_BW80_ORDR_IDX	0u
#define HE_BW160_ORDR_IDX	1u
#define HE_BW80P80_ORDR_IDX	2u

#define HE_MCS_NSS_SUP_FLD_UNIT_MAP_LEN	2u	/* 2 bytes */
#define HE_MCS_NSS_SUP_FLD_UNIT_MAP_SZ	(HE_MCS_NSS_SUP_FLD_UNIT_MAP_LEN * 8u) /* 16 bits */

/* Two unit-maps (TX+RX) */
#define HE_MCS_NSS_SUP_FLD_TXRX_MAP_LEN	(HE_MCS_NSS_SUP_FLD_UNIT_MAP_LEN * 2u)
#define HE_MCS_NSS_SUP_FLD_TXRX_MAP_SZ (HE_MCS_NSS_SUP_FLD_TXRX_MAP_LEN * 8u) /* 32 bits */

/* One TX-RX unit-map (80 MHz) */
#define HE_MCS_NSS_SUP_FLD_MIN_LEN	(HE_MCS_NSS_SUP_FLD_TXRX_MAP_LEN)
/* Three TX-RX unit-maps (80 MHz, 160MHz, 80+80MHz) */
#define HE_MCS_NSS_SUP_FLD_MAX_LEN	(HE_MCS_NSS_SUP_FLD_TXRX_MAP_LEN * 3u)

/* HE Capabilities element */
BWL_PRE_PACKED_STRUCT struct he_cap_ie {
	uint8 id;
	uint8 len;
	uint8 id_ext;
	he_mac_cap_t mac_cap;		/* MAC Capabilities Information */
	he_phy_cap_t phy_cap;		/* PHY Capabilities Information */
	/* he_tx_rx_mcs_nss_sup_t txx_rx_mcs_nss_sup; */ /* Tx Rx HE MCS NSS Support (variable) */
	/* he_ppe_ths_t ppe_ths; */	/* PPE Thresholds (optional) */
} BWL_POST_PACKED_STRUCT;

typedef struct he_cap_ie he_cap_ie_t;

/* Multiple BSSID element */
BWL_PRE_PACKED_STRUCT struct nontrans_BSSID_cap {
	uint8 id; /* 83 */
	uint8 len;
	uint16 capability;
} BWL_POST_PACKED_STRUCT;

typedef struct nontrans_BSSID_cap nontrans_BSSID_cap_t;

BWL_PRE_PACKED_STRUCT struct multi_BSSID_index {
	uint8 id; /* 85 */
	uint8 len; /* 3 in beacon, 1 in probe response */
	uint8 bssid_index; /* between 1 and 2^n - 1 */
	uint8 dtim_period; /* only valid in beacon */
	uint8 dtim_count; /* only valid in beacon */
} BWL_POST_PACKED_STRUCT;

typedef struct multi_BSSID_index multi_BSSID_index_t;

BWL_PRE_PACKED_STRUCT struct fms_descriptor {
	uint8 id; /* 86 */
	uint8 len;
	uint8 num_FMS_counters;
	uint8 *FMS_counters;
	uint8 *FMSID;
} BWL_POST_PACKED_STRUCT;

typedef struct fms_descriptor fms_descriptor_t;

BWL_PRE_PACKED_STRUCT struct nontrans_BSSID_profile_subie {
	uint8 subie_id; /* 0 */
	uint8 subie_len;
	uint8 moreie[1];
} BWL_POST_PACKED_STRUCT;

typedef struct nontrans_BSSID_profile_subie nontrans_BSSID_profile_subie_t;

BWL_PRE_PACKED_STRUCT struct multi_BSSID_ie {
	uint8 id;
	uint8 len;
	uint8 maxBSSID_indicator;
	nontrans_BSSID_profile_subie_t profile[1];
} BWL_POST_PACKED_STRUCT;

typedef struct multi_BSSID_ie multi_BSSID_ie_t;
#define DOT11_MULTIPLE_BSSID_PROFILE_SUBID 0

/* Table 9-262ab, Highest MCS Supported subfield encoding */
#define HE_CAP_MCS_CODE_0_7		0
#define HE_CAP_MCS_CODE_0_8		1
#define HE_CAP_MCS_CODE_0_9		2
#define HE_CAP_MCS_CODE_0_10		3
#define HE_CAP_MCS_CODE_0_11		4
#define HE_CAP_MCS_CODE_SIZE		3	/* num bits for 1-stream */
#define HE_CAP_MCS_CODE_MASK		0x7	/* mask for 1-stream */

#define HE_CAP_MCS_MAP_NSS_MAX	8u	/* Max number of streams possible */

#define HE_MAX_RU_COUNT		4u	/* Max number of RU allocation possible */

#define HE_NSSM1_IDX		0	/* Offset of NSSM1 field */
#define HE_NSSM1_LEN		3	/* length of NSSM1 field in bits */

#define HE_RU_INDEX_MASK_IDX	3	/* Offset of RU index mask field */
#define HE_RU_INDEX_MASK_LEN	4u	/* length of RU Index mask field in bits */

/* PPE Threshold field (figure 9-589co) */
#define HE_PPE_THRESH_NSS_RU_FSZ	3u

/* PPE Threshold Info field (figure 9-589cp) */
/* ruc: RU Count; NSSnM1: NSSn - 1; RUmM1: RUm - 1 */
/* bit offset in PPE Threshold field */
#define HE_PPET16_BIT_OFFSET(ruc, NSSnM1, RUmM1) \
	(HE_NSSM1_LEN + HE_RU_INDEX_MASK_LEN + ((NSSnM1) * (ruc) + (RUmM1)) * 6)

#define HE_PPET8_BIT_OFFSET(ruc, NSSnM1, RUmM1) \
	(HE_NSSM1_LEN + HE_RU_INDEX_MASK_LEN + ((NSSnM1) * (ruc) + (RUmM1)) * 6 + 3)

/* Total PPE Threshold field byte length (Figure 9-589cq) */
#define HE_PPE_THRESH_LEN(nss, ruc) \
	(CEIL((HE_NSSM1_LEN + HE_RU_INDEX_MASK_LEN + ((nss) * (ruc) * 6)), 8))

/* RU Allocation Index encoding (table 9-262ae) */
#define HE_RU_ALLOC_IDX_242		0	/* RU alloc: 282 tones */
#define HE_RU_ALLOC_IDX_484		1	/* RU alloc: 484 tones - 40Mhz */
#define HE_RU_ALLOC_IDX_996		2	/* RU alloc: 996 tones - 80Mhz */
#define HE_RU_ALLOC_IDX_2x996		3	/* RU alloc: 2x996 tones - 80p80/160Mhz */

/* Constellation Index encoding (table 9-262ac) */
#define HE_CONST_IDX_BPSK		0
#define HE_CONST_IDX_QPSK		1
#define HE_CONST_IDX_16QAM		2
#define HE_CONST_IDX_64QAM		3
#define HE_CONST_IDX_256QAM		4
#define HE_CONST_IDX_1024QAM		5
#define HE_CONST_IDX_RSVD		6
#define HE_CONST_IDX_NONE		7

/* Min HE cap ie length when only 80Mhz is supported */
#define HE_CAP_IE_MIN_LEN	(sizeof(he_cap_ie_t) - TLV_HDR_LEN + HE_MCS_NSS_SUP_FLD_MIN_LEN)

/* Max HE cap ie length considering MAX NSS and RU */
#define HE_CAP_IE_MAX_LEN	(sizeof(he_cap_ie_t) - TLV_HDR_LEN + HE_MCS_NSS_SUP_FLD_MAX_LEN + \
				HE_PPE_THRESH_LEN(HE_CAP_MCS_MAP_NSS_MAX, HE_MAX_RU_COUNT))
/**
 * HE Operation IE (Section 9.4.2.238)
 */
/* HE Operation Parameters field (figure 9-589cr) */
#define HE_OP_PARAMS_SIZE		3u
typedef uint8 he_op_parms_t[HE_OP_PARAMS_SIZE];

/* bit position and field width */
#define HE_OP_DEF_PE_DUR_IDX		0u	/* Default PE Duration */
#define HE_OP_DEF_PE_DUR_FSZ		3u
#define HE_OP_TWT_REQD_IDX		3u	/* TWT Required */
#define HE_OP_TWT_REQD_FSZ		1u
#define HE_OP_TXOP_DUR_RTS_THRESH_IDX	4u	/* TXOP Duration Based RTS Threshold */
#define HE_OP_TXOP_DUR_RTS_THRESH_FSZ	10u
#define HE_OP_VHT_OP_PRESENT_IDX	14u	/* VHT Oper Info Present */
#define HE_OP_VHT_OP_PRESENT_FSZ	1u
#define HE_OP_COL_LOC_BSS_IDX		15u
#define HE_OP_COL_LOC_BSS_FSZ		1u
#define HE_OP_ER_SU_DISABLE_IDX		16u
#define HE_OP_ER_SU_DISABLE_FSZ		1u
#define HE_OP_6G_OP_INFO_PRESENT_IDX  17u
#define HE_OP_6G_OP_INFO_PRESENT_FSZ  1u

/* BSS Color Information field (figure 9-589cs) */
#define HE_OP_BSS_COLOR_IDX		0	/* BSS Color */
#define HE_OP_BSS_COLOR_FSZ		6
#define HE_OP_PART_BSS_COLOR_IDX	6	/* Partial BSS Color */
#define HE_OP_PART_BSS_COLOR_FSZ	1
#define HE_OP_DISABLE_BSSCOLOR_IDX	7	/* BSS Color Disable */
#define HE_OP_DISABLE_BSSCOLOR_FSZ	1

/* b4-b13: TXOP Duration RTS threshold */
#define HE_OP_TXOP_RTS_THRESH_DISABLED	1023u

#define HE_BASIC_MCS_NSS_SIZE		2u
typedef uint8 he_basic_mcs_nss_set_t[HE_BASIC_MCS_NSS_SIZE];

#define HE_OP_MAX_BSSID_IND_LEN		1u
#define HE_OP_6G_OPER_INFO_LEN		5u
/* HE Operation element */
BWL_PRE_PACKED_STRUCT struct he_op_ie {
	uint8 id;
	uint8 len;
	uint8 id_ext;
	he_op_parms_t parms;
	uint8 bsscolor_info;
	he_basic_mcs_nss_set_t mcs_nss_op;	/* Basic HE MCS & NSS Set */
} BWL_POST_PACKED_STRUCT;

typedef struct he_op_ie he_op_ie_t;

#define HE_OP_IE_MIN_LEN	(sizeof(he_op_ie_t) - TLV_HDR_LEN)
#define HE_OP_IE_MAX_LEN (sizeof(he_op_ie_t) - TLV_HDR_LEN + VHT_OP_INFO_LEN +\
	HE_OP_MAX_BSSID_IND_LEN + HE_OP_6G_OPER_INFO_LEN)

#define HE_6G_OP_BW_20              0u
#define HE_6G_OP_BW_40              1u
#define HE_6G_OP_BW_80              2u
#define HE_6G_OP_BW_160_80P80       3u

/* Regulatory Info subfield in the United States */
#define HE_6G_OP_REG_INFO_INDOOR_AP_US  0u
#define HE_6G_OP_REG_INFO_SP_AP_US      1u

/* Figure 9-788l Control field format in Draft P802.11ax_D6.0 */
#define HE_6G_CTL_CHBW_MASK         0x03u
#define HE_6G_OP_CTL_CHBW(ctl) (ctl & HE_6G_CTL_CHBW_MASK)
#define HE_6G_CTL_DUP_BCN_MASK      0x04u
#define HE_6G_CTL_REG_INFO_MASK     0x38u
#define HE_6G_CTL_REG_INFO_SHIFT    3u
#define HE_6G_OP_CTL_REG_INFO(ctl) \
	((ctl & HE_6G_CTL_REG_INFO_MASK) >> HE_6G_CTL_REG_INFO_SHIFT)

/* HE 6G Operation info */
BWL_PRE_PACKED_STRUCT struct he_6g_op_info {
	uint8 pri_chan;
	uint8 control;
	uint8 seg0;
	uint8 seg1;
	uint8 min_rate;
} BWL_POST_PACKED_STRUCT;

typedef struct he_6g_op_info he_6g_op_info_t;

/* HE Extended Capabilities element */
BWL_PRE_PACKED_STRUCT struct he_6g_cap_ie {
	uint8 id;
	uint8 len;
	uint8 id_ext;
	uint16 cap_info;    /* Capabilities Information */
} BWL_POST_PACKED_STRUCT;

typedef struct he_6g_cap_ie he_6g_cap_ie_t;
#define HE_6G_CAP_IE_LEN  sizeof(he_6g_cap_ie_t)

/* HE Capabilities Information bit position and fieldwidth.
 * Figure 9-787ai Capabilities Information field format in
 * Draft P802.11ax_D5.0.
 */
#define HE_6G_CAP_MIN_MPDU_START_MASK          0x0007u
#define HE_6G_CAP_MAX_AMPDU_LEN_EXP_MASK       0x0038u
#define HE_6G_CAP_MAX_AMPDU_LEN_EXP_SHIFT           3u
#define HE_6G_CAP_MAX_MPDU_LEN_MASK            0x00C0u
#define HE_6G_CAP_MAX_MPDU_LEN_SHIFT                6u
#define HE_6G_CAP_SM_PW_SAVE_MASK              0x0600u
#define HE_6G_CAP_SM_PW_SAVE_SHIFT                  9u
#define HE_6G_CAP_RD_RESPONDER_MASK            0x0800u
#define HE_6G_CAP_RD_RESPONDER_SHIFT               11u
#define HE_6G_CAP_RX_ANT_PATN_CONST_MASK       0x1000u
#define HE_6G_CAP_RX_ANT_PATN_CONST_SHIFT          12u
#define HE_6G_CAP_TX_ANT_PATN_CONST_MASK       0x2000u
#define HE_6G_CAP_TX_ANT_PATN_CONST_SHIFT          13u

#define HE_6G_CAP_MIN_MPDU_START(cap)    ((cap) & HE_6G_CAP_MIN_MPDU_START_MASK)
#define HE_6G_CAP_MAX_AMPDU_LEN_EXP(cap) (((cap) & HE_6G_CAP_MAX_AMPDU_LEN_EXP_MASK) >> \
	HE_6G_CAP_MAX_AMPDU_LEN_EXP_SHIFT)
#define HE_6G_CAP_MAX_MPDU_LEN(cap)      (((cap) & HE_6G_CAP_MAX_MPDU_LEN_MASK) >> \
	HE_6G_CAP_MAX_MPDU_LEN_SHIFT)
#define HE_6G_CAP_SM_PW_SAVE(cap)        (((cap) & HE_6G_CAP_SM_PW_SAVE_MASK) >> \
	HE_6G_CAP_SM_PW_SAVE_SHIFT)
#define HE_6G_CAP_RD_RESPONDER(cap)      (((cap) & HE_6G_CAP_RD_RESPONDER_MASK) != 0)
#define HE_6G_CAP_RX_ANT_PATN_CONST(cap) (((cap) & HE_6G_CAP_RX_ANT_PATN_CONST_MASK) != 0)
#define HE_6G_CAP_TX_ANT_PATN_CONST(cap) (((cap) & HE_6G_CAP_TX_ANT_PATN_CONST_MASK) != 0)

/**
 * UORA parameter set element (sec 9.4.2.244)
 */
BWL_PRE_PACKED_STRUCT struct he_uora_ie {
	uint8 id;
	uint8 len;
	uint8 id_ext;
	uint8 ocw_range;
} BWL_POST_PACKED_STRUCT;

typedef struct he_uora_ie he_uora_ie_t;

/* Bit field Masks */
#define HE_UORA_EOCW_MIN_IDX		0u
#define HE_UORA_EOCW_MIN_FSZ		3u
#define HE_UORA_EOCW_MAX_IDX		3u
#define HE_UORA_EOCW_MAX_FSZ		3u
/* Reserved -bit6 -7 */

/**
 * MU EDCA parameter set element (sec 9.4.2.245)
 */
BWL_PRE_PACKED_STRUCT struct he_mu_ac_param_record {
	uint8 aci_aifsn;
	uint8 ecw_min_max;
	uint8 muedca_timer;
} BWL_POST_PACKED_STRUCT;

typedef struct he_mu_ac_param_record he_mu_ac_param_record_t;

BWL_PRE_PACKED_STRUCT struct he_muedca_ie {
	uint8 id;
	uint8 len;
	uint8 id_ext;
	uint8 mu_qos_info;
	he_mu_ac_param_record_t param_ac[AC_COUNT];
} BWL_POST_PACKED_STRUCT;

typedef struct he_muedca_ie he_muedca_ie_t;

#define HE_MU_EDCA_PARAM_UPD_CNT_IDX	0u	/* EDCA Parameter Set Update Count */
#define HE_MU_EDCA_PARAM_UPD_CNT_LEN	4u

#define HE_MU_SIGA_SIGB_MCS_DPCU	0
#define HE_MU_SIGA_SIGB_SYMS_DPCU	3u
#define HE_MU_SIGA_GI_LTF_DPCU		3u

/**
 * Spatial Reuse Parameter Set element (sec 9.4.2.241)
 */
/* bit position and field width */
#define HE_SRP_CTRL_SRP_DISALLOW_IDX			0	/* SRP Disallowed */
#define HE_SRP_CTRL_SRP_DISALLOW_FSZ			1
#define HE_SRP_CTRL_NON_SRG_OBSS_PD_SR_DISALLOW_IDX	1	/* NonSRG OBSS PD SR Disallowed */
#define HE_SRP_CTRL_NON_SRG_OBSS_PD_SR_DISALLOW_FSZ	1
#define HE_SRP_CTRL_NON_SRG_OFFSET_PRESENT_IDX		2	/* NonSRG Offset Present */
#define HE_SRP_CTRL_NON_SRG_OFFSET_PRESENT_FSZ		1
#define HE_SRP_CTRL_SRG_INFO_PRESENT_IDX		3	/* SRG Information Present */
#define HE_SRP_CTRL_SRG_INFO_PRESENT_FSZ		1
#define HE_SRP_CTRL_HESIGA_SR_VALUE15_ALLOWED_IDX	4	/* HESIGA_SRP_value15_allowed */
#define HE_SRP_CTRL_HESIGA_SR_VALUE15_ALLOWED_FSZ	1
/* Reserved b5-b7 */

/* Spatial reuse element element */
BWL_PRE_PACKED_STRUCT struct he_srp_ie {
	uint8 id;
	uint8 len;
	uint8 id_ext;
	uint8 sr_control;
} BWL_POST_PACKED_STRUCT;

typedef struct he_srp_ie he_srp_ie_t;

#define HE_SRP_NON_SRG_OBSS_PD_MAX_OFFSET_LEN	1u
#define HE_SRP_SRG_OBSS_PD_MIN_OFFSET_LEN	1u
#define HE_SRP_SRG_OBSS_PD_MAX_OFFSET_LEN	1u
#define HE_SRP_SRG_BSSCOLOR_BITMAP_LEN		8u
#define HE_SRP_SRG_PARTIAL_BSSID_BITMAP_LEN	8u

#define HE_SRP_IE_MIN_LEN	(sizeof(he_srp_ie_t) - TLV_HDR_LEN)
#define HE_SRP_IE_MAX_LEN (sizeof(he_srp_ie_t) - TLV_HDR_LEN +\
	HE_SRP_NON_SRG_OBSS_PD_MAX_OFFSET_LEN + HE_SRP_SRG_OBSS_PD_MIN_OFFSET_LEN\
	HE_SRP_SRG_OBSS_PD_MAX_OFFSET_LEN + HE_SRP_SRG_BSSCOLOR_BITMAP_LEN\
	HE_SRP_SRG_PARTIAL_BSSID_BITMAP_LEN)

/* Bit field Masks */
#define HE_SRP_CTRL_SRP_DISALLOW		(1 << HE_SRP_CTRL_SRP_DISALLOW_IDX)
#define HE_SRP_CTRL_NON_SRG_OBSS_PD_SR_DISALLOW (1 << HE_SRP_CTRL_NON_SRG_OBSS_PD_SR_DISALLOW_IDX)
#define HE_SRP_CTRL_NON_SRG_OFFSET_PRESENT	(1 << HE_SRP_CTRL_NON_SRG_OFFSET_PRESENT_IDX)
#define HE_SRP_CTRL_SRG_INFO_PRESENT		(1 << HE_SRP_CTRL_SRG_INFO_PRESENT_IDX)
#define HE_SRP_CTRL_HESIGA_SR_VALUE15_ALLOWED	(1 << HE_SRP_CTRL_HESIGA_SR_VALUE15_ALLOWED_IDX)

/**
 * ref: (Table 28-21 Page 473 D3.0)
 *
 * -Spatial Reuse field encoding for an HE SU PPDU, HE ER SU PPDU, and HE MU PPDU
 */
#define HE_SRP_DISALLOW	0u	/* SRP_DISALLOW */
/* Values 1 to 12 are reserved */
#define HE_SR_RESTRICTED	13u	/* SR Restricted */
#define HE_SR_DELAY		14u	/* SR Delay */
#define HE_SRP_AND_NON_SRG_OBSS_PD_PROHIBITED	15u	/* SRP_AND_NON_SRG_OBSS_PD_PROHIBITED */
#define HE_SRP_MASK		0x0Fu

/**
 * BSS Color Change Announcement element (sec 9.4.2.243)
 */
/* bit position and field width */
#define HE_BSSCOLOR_CHANGE_NEWCOLOR_IDX		0	/* New BSSColor info */
#define HE_BSSCOLOR_CHANGE_NEWCOLOR_FSZ		6u

/* HE Bsscolor change element */
BWL_PRE_PACKED_STRUCT struct he_bsscolor_change_ie {
	uint8 id;
	uint8 len;
	uint8 id_ext;
	uint8 color_switch_cntdwn;
	uint8 new_bsscolor_info;
} BWL_POST_PACKED_STRUCT;

typedef struct he_bsscolor_change_ie he_bsscolor_change_ie_t;

/* HE SU bit position and field width */
#define HE_SU_PPDU_FORMAT_IDX			0u
#define HE_SU_PPDU_FORMAT_FSZ			1u
#define HE_SU_PPDU_BEAM_CHANGE_IDX		1u
#define HE_SU_PPDU_BEAM_CHANGE_FSZ		1u
#define HE_SU_PPDU_DL_UL_IDX			2u
#define HE_SU_PPDU_DL_UL_FSZ			1u
#define HE_SU_PPDU_MCS_IDX			3u
#define HE_SU_PPDU_MCS_FSZ			4u
#define HE_SU_PPDU_DCM_IDX			7u
#define HE_SU_PPDU_DCM_FSZ			1u
#define HE_SU_PPDU_BSS_COLOR_IDX		8u
#define HE_SU_PPDU_BSS_COLOR_FSZ		6u
#define HE_SU_PPDU_SR_IDX			15
#define HE_SU_PPDU_SR_FSZ			4u
#define HE_SU_PPDU_BW_IDX			19u
#define HE_SU_PPDU_BW_FSZ			2u
#define HE_SU_PPDU_GI_IDX			21u
#define HE_SU_PPDU_GI_FSZ			2u
#define HE_SU_PPDU_LTF_SIZE_IDX			21u
#define HE_SU_PPDU_LTF_SIZE_FSZ			2u
#define HE_SU_PPDU_NUM_LTF_IDX			21u
#define HE_SU_PPDU_NUM_LTF_FSZ			2u
#define HE_SU_PPDU_NSTS_IDX			23u
#define HE_SU_PPDU_NSTS_FSZ			3u
#define HE_SU_PPDU_DOPPLER_NOTSET_NSTS_IDX      23u
#define HE_SU_PPDU_DOPPLER_NOTSET_NSTS_FSZ      3u
#define HE_SU_PPDU_DOPPLER_SET_NSTS_IDX         23u
#define HE_SU_PPDU_DOPPLER_SET_NSTS_FSZ         2u
#define HE_SU_PPDU_MIDAMBLE_IDX                 25u
#define HE_SU_PPDU_MIDAMBLE_FSZ                 1u
#define HE_SU_PPDU_TXOP_IDX			26u
#define HE_SU_PPDU_TXOP_FSZ			7u
#define HE_SU_PPDU_CODING_IDX			33u
#define HE_SU_PPDU_CODING_FSZ			1u
#define HE_SU_PPDU_LDPC_IDX			34u
#define HE_SU_PPDU_LDPC_FSZ			1u
#define HE_SU_PPDU_STBC_IDX			35u
#define HE_SU_PPDU_STBC_FSZ			1u
#define HE_SU_PPDU_TXBF_IDX			36u
#define HE_SU_PPDU_TXBF_FSZ			1u
#define HE_SU_PPDU_PADDING_IDX			37u
#define HE_SU_PPDU_PADDING_FSZ			2u
#define HE_SU_PPDU_PE_IDX			39u
#define HE_SU_PPDU_PE_FSZ			1u
#define HE_SU_PPDU_DOPPLER_IDX			41u
#define HE_SU_PPDU_DOPPLER_FSZ			1u

/* For HE SU/RE SIG A : PLCP0 bit fields [32bit] */
#define HE_SU_RE_SIGA_FORMAT_MASK		0x00000001u
#define HE_SU_RE_SIGA_RE_VAL			0x00000000u
#define HE_SU_RE_SIGA_SU_VAL			0x00000001u
#define HE_SU_RE_SIGA_FORMAT_SHIFT		0u
#define HE_SU_RE_SIGA_BEAM_CHANGE_SHIFT	1u
#define HE_SU_RE_SIGA_UL_DL_SHIFT		2u
#define HE_SU_RE_SIGA_MCS_MASK			0x00000078u
#define HE_SU_RE_SIGA_MCS_SHIFT			3u
#define HE_SU_RE_SIGA_DCM_MASK			0x00000080u
#define HE_SU_RE_SIGA_DCM_SHIFT			7u
#define HE_SU_RE_SIGA_BSS_COLOR_SHIFT		8u	/* Bits 13:8 */
#define HE_SU_RE_SIGA_BSS_COLOR_MASK		0x00003F00u
#define HE_SU_RE_SIGA_RSVD_PLCP0_VAL		0x00004000u
#define HE_SU_RE_SIGA_SRP_VAL_SHIFT		15u	/* Bits 18:15 */
#define HE_SU_RE_SIGA_SRP_VAL_MASK		0x00078000u
#define HE_SU_SIGA_BW_MASK			0x00180000u
#define HE_SU_SIGA_BW_SHIFT			19u
#define HE_RE_SIGA_TONE_MASK			0x00180000u
#define HE_RE_SIGA_TONE_SHIFT			19u
#define HE_SU_RE_SIGA_20MHZ_VAL			0x00000000u
#define HE_SU_RE_SIGA_40MHZ_VAL			0x00080000u
#define HE_SU_RE_SIGA_80MHZ_VAL			0x00100000u
#define HE_SU_RE_SIGA_160MHZ_VAL		0x00180000u
#define HE_SU_RE_SIGA_GI_LTF_MASK		0x00600000u
#define HE_SU_RE_SIGA_1xLTF_GI8us_VAL		0x00000000u
#define HE_SU_RE_SIGA_2xLTF_GI8us_VAL		0x00200000u
#define HE_SU_RE_SIGA_2xLTF_GI16us_VAL		0x00400000u
#define HE_SU_RE_SIGA_4xLTF_GI32us_VAL		0x00600000u
#define HE_SU_RE_SIGA_GI_LTF_SHIFT		21u
#define HE_SU_RE_SIGA_NSTS_MASK			0x03800000u
#define HE_SU_RE_SIGA_NSTS_SHIFT		23u
#define HE_SU_RE_SIGA_TXOP_PLCP0_MASK		0xFC000000u
#define HE_SU_RE_SIGA_TXOP_PLCP0_SHIFT		26u

/* For HE SU SIG EXT : PLCP0 bit fields [32bit] */
#define HE_SU_SIG_EXT_GI_LTF_MASK         0x00000003u
#define HE_SU_SIG_EXT_1xLTF_GI8us_VAL     0x00000000u
#define HE_SU_SIG_EXT_2xLTF_GI8us_VAL     0x00000001u
#define HE_SU_SIG_EXT_2xLTF_GI16us_VAL    0x00000002u
#define HE_SU_SIG_EXT_4xLTF_GI32us_VAL    0x00000003u
#define HE_SU_SIG_EXT_STBC_MASK           0x00000040u
#define HE_SU_SIG_EXT_STBC_SHIFT          6u
#define HE_SU_SIG_EXT_LDPC_MASK           0x00000080u
#define HE_SU_SIG_EXT_LDPC_SHIFT          7u
#define HE_SU_SIG_EXT_MCS_MASK            0x0000f000u
#define HE_SU_SIG_EXT_MCS_SHIFT           12u
#define HE_SU_SIG_EXT_DCM_MASK            0x00010000u
#define HE_SU_SIG_EXT_DCM_SHIFT           16u
#define HE_SU_SIG_EXT_NSTS_MASK           0x000e0000u
#define HE_SU_SIG_EXT_NSTS_SHIFT          17u
#define HE_SU_SIG_EXT_CODING_MASK         0x00800000u
#define HE_SU_SIG_EXT_CODING_SHIFT        23u

/* HE mu ppdu - bit position and field width */
#define HE_MU_PPDU_DL_UL_IDX                    0u
#define HE_MU_PPDU_DL_UL_FSZ                    1u
#define HE_MU_PPDU_SIGB_MCS_IDX                 1u
#define HE_MU_PPDU_SIGB_MCS_FSZ                 3u
#define HE_MU_PPDU_SIGB_DCM_IDX                 4u
#define HE_MU_PPDU_SIGB_DCM_FSZ                 1u
#define HE_MU_PPDU_BSS_COLOR_IDX                5u
#define HE_MU_PPDU_BSS_COLOR_FSZ                6u
#define HE_MU_PPDU_SR_IDX                       11u
#define HE_MU_PPDU_SR_FSZ                       4u

#define HE_MU_PPDU_SIGB_SYM_MU_MIMO_USER_IDX    18u
#define HE_MU_PPDU_SIGB_SYM_MU_MIMO_USER_FSZ    3u

#define HE_MU_PPDU_PRE_PUNCR_SIGA_IDX           15u
#define HE_MU_PPDU_PRE_PUNCR_SIGA_FSZ           2u

#define HE_MU_PPDU_BW_SIGA_IDX                  15u
#define HE_MU_PPDU_BW_SIGA_FSZ                  2u
#define HE_MU_PPDU_BW_SIGA_KNOWN_IDX            17u
#define HE_MU_PPDU_BW_SIGA_KNOWN_FSZ            1u

#define HE_MU_PPDU_SIGB_SYMB_IDX                18u
#define HE_MU_PPDU_SIGB_SYMB_FSZ                4u

#define HE_MU_PPDU_SIGB_COMP_IDX                22u
#define HE_MU_PPDU_SIGB_COMP_FSZ                1u
#define HE_MU_PPDU_GI_IDX                       23u
#define HE_MU_PPDU_GI_FSZ                       2u
#define HE_MU_PPDU_LTF_SIZE_IDX                 23u
#define HE_MU_PPDU_LTF_SIZE_FSZ                 2u
#define HE_MU_PPDU_NUM_LTF_IDX			23u
#define HE_MU_PPDU_NUM_LTF_FSZ			2u
#define HE_MU_PPDU_DOPPLER_IDX                  25u
#define HE_MU_PPDU_DOPPLER_FSZ                  1u
#define HE_MU_PPDU_TXOP_IDX                     26u
#define HE_MU_PPDU_TXOP_FSZ                     7u
#define HE_MU_PPDU_MIDAMBLE_IDX                 34u
#define HE_MU_PPDU_MIDAMBLE_FSZ                 3u
#define HE_MU_PPDU_LDPC_IDX                     37u
#define HE_MU_PPDU_LDPC_FSZ                     1u
#define HE_MU_PPDU_STBC_IDX                     38u
#define HE_MU_PPDU_STBC_FSZ                     1u
#define HE_MU_PPDU_PADDING_IDX                  39u
#define HE_MU_PPDU_PADDING_FSZ                  2u
#define HE_MU_PPDU_PE_IDX                       41u
#define HE_MU_PPDU_PE_FSZ                       1u

/* he trigger ppdu - bit position and field width */
#define HE_TRIG_PPDU_BSS_COLOR_IDX                1u
#define HE_TRIG_PPDU_BSS_COLOR_FSZ                6u

/* full spatial reuse field */
#define HE_TRIG_PPDU_SR_IDX                       7u
#define HE_TRIG_PPDU_SR_FSZ                       16u

#define HE_TRIG_PPDU_SR1_IDX                      7u
#define HE_TRIG_PPDU_SR1_FSZ                      4u
#define HE_TRIG_PPDU_SR2_IDX                      11u
#define HE_TRIG_PPDU_SR2_FSZ                      4u
#define HE_TRIG_PPDU_SR3_IDX                      15u
#define HE_TRIG_PPDU_SR3_FSZ                      4u
#define HE_TRIG_PPDU_SR4_IDX                      19u
#define HE_TRIG_PPDU_SR4_FSZ                      4u
#define HE_TRIG_PPDU_TXOP_IDX                     26u
#define HE_TRIG_PPDU_TXOP_FSZ                     7u

/* For HE MU SIG A : PLCP0 bit fields [32bit] */
#define HE_MU_SIGA_UL_DL_SHIFT			0
#define HE_MU_SIGA_UL_TB_PPDU			0
#define HE_MU_SIGA_SIGB_MCS_MASK		0x000000E
#define HE_MU_SIGA_SIGB_MCS_SHIFT		1
#define HE_MU_SIGA_SIGB_DCM_SHIFT		4
#define HE_MU_SIGA_SIGB_DCM_DISABLED		0
#define HE_MU_SIGA_BW_SHIFT			15
#define HE_MU_SIGA_BW_80_UNPUNCTURED		2
#define HE_MU_SIGA_BW_SEC_20_PUNCTURED		4
#define HE_MU_SIGA_BW_SEC_40_PUNCTURED		5
#define HE_MU_SIGA_SIGB_SYMS_SHIFT		18
#define HE_MU_SIGA_GI_LTF_MASK			0x01800000
#define HE_MU_SIGA_GI_LTF_SHIFT			23

/* For HE MU SIG A : PLCP1 bit fields [32bit] */
#define HE_MU_SIGA_STBC_MASK		0x00000040
#define HE_MU_SIGA_STBC_SHIFT		6

/* For HE SU/RE SIG A : PLCP1 bit fields [16bit] */
#define HE_SU_RE_SIGA_TXOP_PLCP1_MASK	0x0001
#define HE_SU_RE_SIGA_TXOP_PLCP1_SHIFT	0
#define HE_SU_RE_SIGA_CODING_MASK	0x0002
#define HE_SU_RE_SIGA_CODING_SHIFT	1
#define HE_SU_RE_SIGA_LDPC_EXTRA_MASK	0x0004
#define HE_SU_RE_SIGA_LDPC_EXTRA_SHIFT	2
#define HE_SU_RE_SIGA_STBC_MASK		0x0008
#define HE_SU_RE_SIGA_STBC_SHIFT	3
#define HE_SU_RE_SIGA_BEAMFORM_MASK	0x0010
#define HE_SU_RE_SIGA_BEAMFORM_SHIFT	4
#define HE_SU_RE_SIGA_RSVD_PLCP1_VAL	0x0100

/* For HE MU SIG A : PLCP1 bit fields [16bit] */
#define HE_MU_SIGA_RSVD_SHIFT		1
#define HE_MU_SIGA_LTF_SYMS_SHIFT	2

/* For HE SU SIG A : RX PLCP4 bit fields [8bit] */
#define HE_SU_SIGA2_STBC_RX_MASK	0x08u

/* For HE ER SIG A : RX PLCP4 bit fields [8bit] */
#define HE_ER_SIGA2_STBC_RX_MASK	0x08u

/* For HE MU SIG A : RX PLCP4 bit fields [8bit] */
#define HE_MU_SIGA2_STBC_RX_MASK	0x40u

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

/* HE Action Frame */
/* FIXME: use temporary Offsets until the spec assigns them */
#define HE_AF_CAT_OFF	0
#define HE_AF_ACT_OFF	1

/* TWT Setup */
#define HE_AF_TWT_SETUP_TOKEN_OFF	2
#define HE_AF_TWT_SETUP_TWT_IE_OFF	3

/* TWT Teardown */
#define HE_AF_TWT_TEARDOWN_FLOW_OFF	2

/* TWT Information */
#define HE_AF_TWT_INFO_OFF	2

/* HE Action ID */
/* FIXME: use temporary IDs until ANA assigns them */
#define HE_ACTION_TWT_SETUP	1
#define HE_ACTION_TWT_TEARDOWN	2
#define HE_ACTION_TWT_INFO	3

/* HE Basic trigger frame common info fields */
#define HE_TRIG_CMNINFO_SZ	8
typedef uint8 he_trig_cmninfo_set_t[HE_TRIG_CMNINFO_SZ];

/* bit position and field width */
#define HE_TRIG_CMNINFO_FRMTYPE_INDX		0	/* Trigger frame type */
#define HE_TRIG_CMNINFO_FRMTYPE_FSZ		4
#define HE_TRIG_CMNINFO_LSIGLEN_INDX		4	/* L-sig length */
#define HE_TRIG_CMNINFO_LSIGLEN_FSZ		12
#define HE_TRIG_CMNINFO_CASCADEIND_INDX		16	/* Cascade indication */
#define HE_TRIG_CMNINFO_CASCADEIND_FSZ		1
#define HE_TRIG_CMNINFO_CSREQ_INDX		17	/* Carrier sense indication */
#define HE_TRIG_CMNINFO_CSREQ_FSZ		1
#define HE_TRIG_CMNINFO_BWINFO_INDX		18	/* Bw info */
#define HE_TRIG_CMNINFO_BWINFO_FSZ		2
#define HE_TRIG_CMNINFO_GI_LTF_INDX		20	/* Cp-LTF size */
#define HE_TRIG_CMNINFO_GI_LTF_FSZ		2
#define HE_TRIG_CMNINFO_MUMIMO_LTF_INDX		22	/* HE-LTF mask enable */
#define HE_TRIG_CMNINFO_MUMIMO_LTF_FSZ		1
#define HE_TRIG_CMNINFO_HELTF_SYM_INDX		23	/* He-LTF sumbols */
#define HE_TRIG_CMNINFO_HELTF_SYM_FSZ		3
#define HE_TRIG_CMNINFO_STBC_INDX		26	/* STBC support */
#define HE_TRIG_CMNINFO_STBC_FSZ		1
#define HE_TRIG_CMNINFO_LDPC_EXTSYM_INDX	27	/* LDPC extra symbol */
#define HE_TRIG_CMNINFO_LDPC_EXTSYM_FSZ		1
#define HE_TRIG_CMNINFO_AP_TXPWR_INDX		28	/* AP TX power */
#define HE_TRIG_CMNINFO_AP_TXPWR_FSZ		6
#define HE_TRIG_CMNINFO_AFACT_INDX		34	/* a-factor */
#define HE_TRIG_CMNINFO_AFACT_FSZ		2
#define HE_TRIG_CMNINFO_PEDISAMBIG_INDX		36	/* PE disambiguity */
#define HE_TRIG_CMNINFO_PEDISAMBIG_FSZ		1
#define HE_TRIG_CMNINFO_SPTIAL_REUSE_INDX	37	/* spatial re-use */
#define HE_TRIG_CMNINFO_SPTIAL_REUSE_FSZ	16
#define HE_TRIG_CMNINFO_DOPPLER_INDX		53	/* doppler supoort */
#define HE_TRIG_CMNINFO_DOPPLER_FSZ		1
#define HE_TRIG_CMNINFO_HESIGA_RSVD_INDX	54	/* rsvd bits from HE-SIGA */
#define HE_TRIG_CMNINFO_HESIGA_RSVD_FSZ		9
#define HE_TRIG_CMNINFO_RSVD_INDX		63	/* reseved bit from HE-SIGA  */
#define HE_TRIG_CMNINFO_RSVD_FSZ		1

/* HE Basic trigger frame user info fields */
#define HE_TRIG_USRINFO_SZ	5
typedef uint8 he_trig_usrinfo_set_t[HE_TRIG_USRINFO_SZ];

/* bit position and field width */
#define HE_TRIG_USRINFO_AID_INDX		0	/* AID */
#define HE_TRIG_USRINFO_AID_FSZ			12
#define HE_TRIG_USRINFO_RU_ALLOC_INDX		12	/* RU allocation index */
#define HE_TRIG_USRINFO_RU_ALLOC_FSZ		8
#define HE_TRIG_USRINFO_CODING_INDX		20	/* coding type (BCC/LDPC) */
#define HE_TRIG_USRINFO_CODING_FSZ		1
#define HE_TRIG_USRINFO_MCS_INDX		21	/* MCS index value */
#define HE_TRIG_USRINFO_MCS_FSZ			4
#define HE_TRIG_USRINFO_DCM_INDX		25	/* Dual carrier modulation */
#define HE_TRIG_USRINFO_DCM_FSZ			1
#define HE_TRIG_USRINFO_SSALLOC_STRMOFFSET_INDX		26	/* stream offset */
#define HE_TRIG_USRINFO_SSALLOC_STRMOFFSET_FSZ		3
#define HE_TRIG_USRINFO_SSALLOC_NSS_INDX		29	/* number of spatial streams */
#define HE_TRIG_USRINFO_SSALLOC_NSS_FSZ		3
#define HE_TRIG_USRINFO_TARGET_RSSI_INDX	32	/* Target RSSI */
#define HE_TRIG_USRINFO_TARGET_RSSI_FSZ		7
#define HE_TRIG_USRINFO_RSVD_INDX		39	/* Reserved bit */
#define HE_TRIG_USRINFO_RSVD_FSZ		1

/* Different types of trigger frame */
#define HE_TRIG_TYPE_BASIC_FRM			0	/* basic trigger frame */
#define HE_TRIG_TYPE_BEAM_RPT_POLL_FRM		1	/* beamforming report poll frame */
#define HE_TRIG_TYPE_MU_BAR_FRM			2	/* MU-BAR frame */
#define HE_TRIG_TYPE_MU_RTS__FRM		3	/* MU-RTS frame */
#define HE_TRIG_TYPE_BSR_FRM			4	/* Buffer status report poll */

/* HE Timing related parameters (Table 28-9) */
#define HE_T_LEG_STF			8
#define HE_T_LEG_LTF			8
#define HE_T_LEG_LSIG			4
#define HE_T_RL_SIG			4
#define HE_T_SIGA			8
#define HE_T_STF			4	/* STF for SU / MU HE PPDUs */
#define HE_T_TB_PPDU_STF		8	/* STF for HE trigger based PPDUs */
#define HE_T_LEG_PREAMBLE		(HE_T_LEG_STF + HE_T_LEG_LTF + HE_T_LEG_LSIG)
#define HE_T_LEG_SYMB			4
#define HE_RU_26_TONE			26
#define HE_RU_52_TONE			52
#define HE_RU_106_TONE			106
#define HE_RU_242_TONE			242
#define HE_RU_484_TONE			484
#define HE_RU_996_TONE			996
#define HE_RU_2x996_TONE		1992
#define HE_MAX_26_TONE_RU_INDX		36
#define HE_MAX_52_TONE_RU_INDX		52
#define HE_MAX_106_TONE_RU_INDX		60
#define HE_MAX_242_TONE_RU_INDX		64
#define HE_MAX_484_TONE_RU_INDX		66
#define HE_MAX_996_TONE_RU_INDX		67
#define HE_MAX_2x996_TONE_RU_INDX	68

/**
 * ref: (Table 28-9 Page 285)
 *
 * - for calculation purpose - in multiples of 10 (*10)
 */
#define HE_T_LTF_1X			32
#define HE_T_LTF_2X			64
#define HE_T_LTF_4X			128
#define HE_T_SYM1			136	/* OFDM symbol duration with base GI */
#define HE_T_SYM2			144	/* OFDM symbol duration with double GI */
#define HE_T_SYM4			160	/* OFDM symbol duration with quad GI */

#define HE_N_LEG_SYM			3	/* bytes per legacy symbol */
#define HE_N_TAIL			6	/* tail field bits for BCC */
#define HE_N_SERVICE			16	/* bits in service field */
#define HE_T_MAX_PE			16	/* max Packet extension duration */

#endif /* _802_11ax_h_ */
