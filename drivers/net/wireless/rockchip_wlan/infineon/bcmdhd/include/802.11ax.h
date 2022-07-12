/*
 * Basic types and constants relating to 802.11ax/HE STA
 * This is a portion of 802.11ax definition. The rest are in 802.11.h.
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

#ifndef _802_11ax_h_
#define _802_11ax_h_

#include <typedefs.h>

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

/* special STA-IDs (Section 27.11.1) */
#define HE_STAID_BSS_BCAST		0
#define HE_STAID_UNASSOCIATED_STA	2045
#define HE_STAID_NO_USER		2046
#define HE_STAID_MBSS_BCAST		2047
#define HE_STAID_MASK			0x07FF
#define HE_AID12_MASK			0x0FFF

/* Deprecated */
#define HE_STAID_RU_NODATA		2046

/**
 * HE Capabilites element (sec 9.4.2.218)
 */

/* HE MAC Capabilities Information field (figure 9-589ck) */
#define HE_MAC_CAP_INFO_SIZE	6
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
#define HE_MAC_MAX_MSDU_AMSDU_FRAGS_IDX		5	/* Max. Fragmented MSDUs/AMSDUs Exponent */
#define HE_MAC_MAX_MSDU_AMSDU_FRAGS_FSZ		3
#define HE_MAC_MIN_FRAG_SIZE_IDX		8	/* Min. Fragment Size */
#define HE_MAC_MIN_FRAG_SIZE_FSZ		2
#define HE_MAC_TRG_PAD_DUR_IDX			10	/* Trigger Frame MAC Pad Dur */
#define HE_MAC_TRG_PAD_DUR_FSZ			2
#define HE_MAC_MULTI_TID_RX_AGG_IDX		12	/* Multi TID RX Aggregation */
#define HE_MAC_MULTI_TID_RX_AGG_FSZ		3
#define HE_MAC_LINK_ADAPT_IDX			15	/* HE Link Adaptation */
#define HE_MAC_LINK_ADAPT_FSZ			2
#define HE_MAC_ALL_ACK_SUPPORT_IDX		17	/* All Ack Support */
#define HE_MAC_ALL_ACK_SUPPORT_FSZ		1
#define HE_MAC_TRS_SUPPORT_IDX			18	/* TRS Support */
#define HE_MAC_TRS_SUPPORT_FSZ			1
#define HE_MAC_A_BSR_IDX			19	/* A-BSR Support */
#define HE_MAC_A_BSR_FSZ			1
#define HE_MAC_BCAST_TWT_SUPPORT_IDX		20	/* Broadcast TWT Support */
#define HE_MAC_BCAST_TWT_SUPPORT_FSZ		1
#define HE_MAC_BA_32BITMAP_SUPPORT_IDX		21	/* 32-bit BA Bitmap Support */
#define HE_MAC_BA_32BITMAP_SUPPORT_FSZ		1
#define HE_MAC_MU_CASCADE_SUPPORT_IDX		22	/* MU Cascade Support */
#define HE_MAC_MU_CASCADE_SUPPORT_FSZ		1
#define HE_MAC_MULTI_TID_AGG_ACK_IDX		23	/* Ack Enabled Multi TID Agg. */
#define HE_MAC_MULTI_TID_AGG_ACK_FSZ		1
#define HE_MAC_RESVD1_IDX			24	/* Reserved Bit */
#define HE_MAC_RESVD1_FSZ			1
#define HE_MAC_OMI_ACONTROL_SUPPORT_IDX		25	/* OMI A-Control Support */
#define HE_MAC_OMI_ACONTROL_SUPPORT_FSZ		1
#define HE_MAC_OFDMA_RA_SUPPORT_IDX		26	/* OFDMA RA Support */
#define HE_MAC_OFDMA_RA_SUPPORT_FSZ		1
#define HE_MAC_MAX_AMPDU_LEN_EXP_EXT_IDX	27	/* Max AMPDU Length Exponent Extention */
#define HE_MAC_MAX_AMPDU_LEN_EXP_EXT_FSZ	2
#define HE_MAC_AMSDU_FRAG_SUPPORT_IDX		29	/* AMSDU Fragementation Support */
#define HE_MAC_AMSDU_FRAG_SUPPORT_FSZ		1
#define HE_MAC_FLEX_TWT_SCHEDULE_IDX		30	/* Flexible TWT Schedule Support */
#define HE_MAC_FLEX_TWT_SCHEDULE_FSZ		1
#define HE_MAC_RX_MBSS_CTL_FRAME_IDX		31	/* Rx of Control frames of MBSS */
#define HE_MAC_RX_MBSS_CTL_FRAME_FSZ		1
#define HE_MAC_RX_AGG_BSRP_IDX			32	/* Support Rx of aggregated BSRP BQRP */
#define HE_MAC_RX_AGG_BSRP_FSZ			1
#define HE_MAC_QTP_SUPPORT_IDX			33	/* Support Quiet time period */
#define HE_MAC_QTP_SUPPORT_FSZ			1
#define HE_MAC_ABQR_SUPPORT_IDX			34	/* Support aggregated BQR */
#define HE_MAC_ABQR_SUPPORT_FSZ			1
#define HE_MAC_SRP_RSPNDR_IDX			35	/* SRP responder */
#define HE_MAC_SRP_RSPNDR_FSZ			1
#define HE_MAC_NDP_FDBCK_SUPPORT_IDX		36	/* NDP feedback report */
#define HE_MAC_NDP_FDBCK_SUPPORT_FSZ		1
#define HE_MAC_OPS_SUPPORT_IDX			37	/* OPS support */
#define HE_MAC_OPS_SUPPORT_FSZ			1
#define HE_MAC_AMSDU_IN_AMPDU_IDX		38	/* AMSDU in AMPDU support */
#define HE_MAC_AMSDU_IN_AMPDU_FSZ		1
#define HE_MAC_MULTI_TID_TX_AGG_IDX		39	/* Multi TID TX Aggregation */
#define HE_MAC_MULTI_TID_TX_AGG_FSZ		3
#define HE_MAC_SUBCH_SEL_TR_SUPPORT_IDX		42	/* HE Subchl Selective Trns Sup */
#define HE_MAC_SUBCH_SEL_TR_SUPPORT_FSZ		1
#define HE_MAC_UL_TONE_RU_SUPPORT_IDX		43	/* UL tone RUSupport */
#define HE_MAC_UL_TONE_RU_SUPPORT_FSZ		1
#define HE_MAC_OMC_UL_MU_DIS_RX_IDX		44	/* OM Control ULMUData Dis RX Sup */
#define HE_MAC_OMC_UL_MU_DIS_RX_FSZ		1
#define HE_MAC_HE_DSMPS_SUPPORT_IDX		45	/* HE Dynamic SM Power Save Sup */
#define HE_MAC_HE_DSMPS_SUPPORT_FSZ		1
#define HE_MAC_PUNC_SOUND_SUPPORT_IDX          46      /* Punctured Sounding Sup */
#define HE_MAC_PUNC_SOUND_SUPPORT_FSZ          1
#define HE_MAC_NONAX_TFRX_SUPPORT_IDX          47      /* HT and VHT TriggerFrame Rx Sup */
#define HE_MAC_NONAX_TFRX_SUPPORT_FSZ          1

/* bit position and field width */
#define HE_SU_PPDU_FORMAT_IDX				0
#define HE_SU_PPDU_FORMAT_FSZ				1
#define HE_SU_PPDU_BEAM_CHANGE_IDX			1
#define HE_SU_PPDU_BEAM_CHANGE_FSZ			1
#define HE_SU_PPDU_DL_UL_IDX				2
#define HE_SU_PPDU_DL_UL_FSZ				1
#define HE_SU_PPDU_MCS_IDX				3
#define HE_SU_PPDU_MCS_FSZ				4
#define HE_SU_PPDU_DCM_IDX				7
#define HE_SU_PPDU_DCM_FSZ				1
#define HE_SU_PPDU_BSS_COLOR_IDX			8
#define HE_SU_PPDU_BSS_COLOR_FSZ			6
#define HE_SU_PPDU_SR_IDX				15
#define HE_SU_PPDU_SR_FSZ				4
#define HE_SU_PPDU_BW_IDX				19
#define HE_SU_PPDU_BW_FSZ				2
#define HE_SU_PPDU_LTF_IDX				21
#define HE_SU_PPDU_LTF_FSZ				2
#define HE_SU_PPDU_NSTS_IDX				23
#define HE_SU_PPDU_NSTS_FSZ				3
#define HE_SU_PPDU_TXOP_IDX				26
#define HE_SU_PPDU_TXOP_FSZ				7
#define HE_SU_PPDU_CODING_IDX				33
#define HE_SU_PPDU_CODING_FSZ				1
#define HE_SU_PPDU_LDPC_IDX				34
#define HE_SU_PPDU_LDPC_FSZ				1
#define HE_SU_PPDU_STBC_IDX				35
#define HE_SU_PPDU_STBC_FSZ				1
#define HE_SU_PPDU_TXBF_IDX				36
#define HE_SU_PPDU_TXBF_FSZ				1

/* HT Control Field: (Table 9-9a) */
#define HTC_HE_VARIANT			0x3F
/* HT Control IDs: (Table 9-18a) */
#define HTC_HE_CTLID_SHIFT		0x2
#define HTC_HE_CTLID_TRS		0x0
#define HTC_HE_CTLID_OMI		0x1
#define HTC_HE_CTLID_HLA		0x2
#define HTC_HE_CTLID_BSR		0x3
#define HTC_HE_CTLID_UPH		0x4
#define HTC_HE_CTLID_BQR		0x5
#define HTC_HE_CTLID_CAS		0x6
#define HTC_HE_CTLID_NONE		0xF

#define HE_LTF_1_GI_1_6us		(0)
#define HE_LTF_2_GI_0_8us		(1)
#define HE_LTF_2_GI_1_6us		(2)
#define HE_LTF_4_GI_3_2us		(3)

/* max. # of spacial streams */
#define HE_CAP_MCS_MAP_NSS_MAX		8

/* HE PHY Capabilities Information field (figure 9-589cl) */
#define HE_PHY_CAP_INFO_SIZE				11
typedef uint8 he_phy_cap_t[HE_PHY_CAP_INFO_SIZE];

/* PHY Ccapabilites for D3.0 */
#define HE_PHY_RESVD1_IDX			0	/* Reserved */
#define HE_PHY_RESVD1_FSZ			1
#define HE_PHY_CH_WIDTH_SET_IDX			1	/* Channel Width Set */
#define HE_PHY_CH_WIDTH_SET_FSZ			7
#define HE_PHY_PREAMBLE_PUNCT_RX_IDX		8	/* Preamble Puncturing Rx */
#define HE_PHY_PREAMBLE_PUNCT_RX_FSZ		4
#define HE_PHY_DEVICE_CLASS_IDX			12	/* Device Class */
#define HE_PHY_DEVICE_CLASS_FSZ			1
#define HE_PHY_LDPC_PYLD_IDX			13	/* LDPC Coding In Payload */
#define HE_PHY_LDPC_PYLD_FSZ			1
#define HE_PHY_SU_PPDU_1x_LTF_0_8_GI_IDX	14	/* SU PPDU 1x LTF GI 0.8 us */
#define HE_PHY_SU_PPDU_1x_LTF_0_8_GI_FSZ	1
#define HE_PHY_MIDAMBLE_RX_MAX_NSTS_IDX		15	/* Midamble Tx/Rx Max NSTS */
#define HE_PHY_MIDAMBLE_RX_MAX_NSTS_FSZ		2
#define HE_PHY_NDP_4x_LTF_3_2_GI_RX_IDX		17	/* NDP with 4xLTF 3.2us GI Rx */
#define HE_PHY_NDP_4x_LTF_3_2_GI_RX_FSZ		1
#define HE_PHY_STBC_TX_LESS_EQ_80_IDX		18	/* STBC Tx <= 80 MHz */
#define HE_PHY_STBC_TX_LESS_EQ_80_FSZ		1
#define HE_PHY_STBC_RX_LESS_EQ_80_IDX		19	/* STBC Rx <= 80 MHz */
#define HE_PHY_STBC_RX_LESS_EQ_80_FSZ		1
#define HE_PHY_DOPPLER_TX_IDX			20	/* Doppler Tx */
#define HE_PHY_DOPPLER_TX_FSZ			1
#define HE_PHY_DOPPLER_RX_IDX			21	/* Doppler Rx */
#define HE_PHY_DOPPLER_RX_FSZ			1
#define HE_PHY_FULL_BW_UL_MU_IDX		22	/* Full bandwidth UL MU */
#define HE_PHY_FULL_BW_UL_MU_FSZ		1
#define HE_PHY_PART_BW_UL_MU_IDX		23	/* Partial bandwidth UL MU */
#define HE_PHY_PART_BW_UL_MU_FSZ		1
#define HE_PHY_DCM_MAX_CONST_TX_IDX		24	/* DCM Max constellation */
#define HE_PHY_DCM_MAX_CONST_TX_FSZ		2
#define HE_PHY_DCM_NSS_TX_IDX			26	/* DCM Encoding Tx */
#define HE_PHY_DCM_NSS_TX_FSZ			1
#define HE_PHY_DCM_MAX_CONST_RX_IDX		27	/* DCM Max constellation */
#define HE_PHY_DCM_MAX_CONST_RX_FSZ		2
#define HE_PHY_DCM_NSS_RX_IDX			29	/* DCM Encoding Rx */
#define HE_PHY_DCM_NSS_RX_FSZ			1
#define HE_PHY_RX_MUPPDU_NON_AP_STA_IDX		30	/* Rx HE MMPDUE from Non-AP */
#define HE_PHY_RX_MUPPDU_NON_AP_STA_FSZ		1
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
#define HE_PHY_TRG_MU_BFM_FEEDBACK_IDX		51	/* Triggered MU TXBF Feedback */
#define HE_PHY_TRG_MU_BFM_FEEDBACK_FSZ		1
#define HE_PHY_TRG_CQI_FEEDBACK_IDX		52	/* Triggered CQI Feedback */
#define HE_PHY_TRG_CQI_FEEDBACK_FSZ		1
#define HE_PHY_EXT_RANGE_SU_PYLD_IDX		53	/* HE ER SU PPDU Payload */
#define HE_PHY_EXT_RANGE_SU_PYLD_FSZ		1
#define HE_PHY_DL_MU_MIMO_PART_BW_IDX		54	/* DL MUMIMO On Partial BW */
#define HE_PHY_DL_MU_MIMO_PART_BW_FSZ		1
#define HE_PHY_PPE_THRESH_PRESENT_IDX		55	/* PPE Threshold Present */
#define HE_PHY_PPE_THRESH_PRESENT_FSZ		1
#define HE_PHY_SRP_SR_SUPPORT_IDX		56	/* SRP based SR Support */
#define HE_PHY_SRP_SR_SUPPORT_FSZ		1
#define HE_PHY_POWER_BOOST_FACTOR_IDX		57	/* Power Boost Factor Support */
#define HE_PHY_POWER_BOOST_FACTOR_FSZ		1
#define HE_PHY_LONG_LTF_SHORT_GI_SU_PPDU_IDX	58	/* HE SU - Long LTF Short GI */
#define HE_PHY_LONG_LTF_SHORT_GI_SU_PPDU_FSZ	1
#define HE_PHY_MAX_NC_IDX			59	/* Max Nc */
#define HE_PHY_MAX_NC_FSZ			3
#define HE_PHY_STBC_GT80_TX_IDX			62	/* STBC Tx > 80 MHz */
#define HE_PHY_STBC_GT80_TX_FSZ			1
#define HE_PHY_STBC_GT80_RX_IDX			63	/* STBC Rx > 80 MHz */
#define HE_PHY_STBC_GT80_RX_FSZ			1
#define HE_PHY_HE_ER_SU_PPDU_4X_RX_IDX		64	/* HEERSUPPDU With 4x HE-LTF & 0.8 GI */
#define HE_PHY_HE_ER_SU_PPDU_4X_RX_FSZ		1
#define HE_PHY_20_40_HE_PPDU_IDX		65	/* 20MHz In 40MHz HEPPDU In 2.4GHz Band */
#define HE_PHY_20_40_HE_PPDU_FSZ		1
#define HE_PHY_20_160_HE_PPDU_IDX		66	/* 20MHz In 160/80+80MHz HEPPDU */
#define HE_PHY_20_160_HE_PPDU_FSZ		1
#define HE_PHY_80_160_HE_PPDU_IDX		67	/* 80MHz In 160/80+80MHz HEPPDU */
#define HE_PHY_80_160_HE_PPDU_FSZ		1
#define HE_PHY_HE_ER_SU_PPDU_IDX		68	/* HEERSUPPDU With 1x HE-LTF & 0.8 GI */
#define HE_PHY_HE_ER_SU_PPDU_FSZ		1
#define HE_PHY_MIDAMBLE_TX_RX_2X_1X_HE_LTF_IDX	69	/* Midamble RX 2x & 1x HE LTF */
#define HE_PHY_MIDAMBLE_TX_RX_2X_1X_HE_LTF_FSZ	1
#define HE_PHY_DCM_MAX_BW_IDX			70	/* DCM Max BW */
#define HE_PHY_DCM_MAX_BW_FSZ			2
#define HE_PHY_SIGB_SYM_GT16_RX_SUPPORT_IDX	72	/* Greater than 16 HESIG-B OFDM Symb Sup */
#define HE_PHY_SIGB_SYM_GT16_RX_SUPPORT_FSZ	1
#define HE_PHY_NON_TRIG_CQI_FEEDBACK_IDX	73	/* Non- Triggered CQI Feedback */
#define HE_PHY_NON_TRIG_CQI_FEEDBACK_FSZ	1
#define HE_PHY_1024QAM_TX_IN_RU_LT242_IDX	74	/* Tx 1024-QAM < 242-tone RU Support */
#define HE_PHY_1024QAM_TX_IN_RU_LT242_FSZ	1
#define HE_PHY_1024QAM_RX_IN_RU_LT242_IDX	75	/* Rx 1024-QAM < 242-tone RU Support */
#define HE_PHY_1024QAM_RX_IN_RU_LT242_FSZ	1
#define HE_PHY_RX_HE_MU_COMPR_SIGB_IDX		76	/* RxFull BWSU HEMUPPDU Wt CompSIGB */
#define HE_PHY_RX_HE_MU_COMPR_SIGB_FSZ		1
#define HE_PHY_RX_HE_MU_NCOMP_SIGB_IDX		77	/* RxFull BWSU HEMUPPDU wt NCompSIGB */
#define HE_PHY_RX_HE_MU_NCOMP_SIGB_FSZ		1
#define HE_PHY_RESVD2_IDX			78	/* Reserved */
#define HE_PHY_RESVD2_FSZ			10

/* DCM */
#define HE_PHY_CAP_DCM_NOT_SUPP	0x0
#define HE_PHY_CAP_DCM_BPSK	0x1
#define HE_PHY_CAP_DCM_QPSK	0x2
#define HE_PHY_CAP_DCM_16_QAM	0x3
#define HE_PHY_CAP_DCM_1SS	0x0
#define HE_PHY_CAP_DCM_2SS	0x1

/* HE Mac Capabilities values */

/* b3-b4: Fragmentation Support field (table 9-262z) */
#define HE_MAC_FRAG_NOSUPPORT		0	/* dynamic frag not supported */
#define HE_MAC_FRAG_VHT_MPDU		1	/* Frag support for VHT single MPDU only */
#define HE_MAC_FRAG_ONE_PER_AMPDU	2	/* 1 frag per MPDU in A-MPDU */
#define HE_MAC_FRAG_MULTI_PER_AMPDU	3	/* 2+ frag per MPDU in A-MPDU */

/* b8-b9: Minimum payload size of first fragment */
/* no restriction on min. payload size */
#define HE_MAC_MINFRAG_NO_RESTRICT	0
/* minimum payload size of 128 Bytes */
#define HE_MAC_MINFRAG_SIZE_128	1
/* minimum payload size of 256 Bytes */
#define HE_MAC_MINFRAG_SIZE_256	2
/* minimum payload size of 512 Bytes */
#define HE_MAC_MINFRAG_SIZE_512	3

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
 * 2^(16 + MAX_AMPDU_LEN_HE_EXPO_1) -1 (if this value in HT CAP is 7).
 */
#define HE_MAC_MAX_AMPDU_EXP_HE_1	(1)
/* Max. AMPDU length =
 * 2^(20 + MAX_AMPDU_LEN_HE_EXPO_2) -1 (if this value in VHT CAP is 7) or
 * 2^(16 + MAX_AMPDU_LEN_HE_EXPO_2) -1 (if this value in HT CAP is 7).
 */
#define HE_MAC_MAX_AMPDU_EXP_HE_2	(2)

/* HE PHY Capabilities values */
/* b1-b7: Channel Width Support field */
#define HE_PHY_CH_WIDTH_2G_40		0x01
#define HE_PHY_CH_WIDTH_5G_80		0x02
#define HE_PHY_CH_WIDTH_5G_160		0x04
#define HE_PHY_CH_WIDTH_5G_80P80	0x08
#define HE_PHY_CH_WIDTH_2G_40_RU	0x10
#define HE_PHY_CH_WIDTH_5G_242_RU	0x20

/* b8-b11: Preamble puncturing Rx */
#define HE_PHY_PREAMBLE_PUNC_RX_0	0x1
#define HE_PHY_PREAMBLE_PUNC_RX_1	0x2
#define HE_PHY_PREAMBLE_PUNC_RX_2	0x4
#define HE_PHY_PREAMBLE_PUNC_RX_3	0x8

/* b24-b29: DCM Encoding at Tx and Rx */
#define HE_PHY_TX_DCM_ENC_NOSUPPORT	0x00
#define HE_PHY_TX_DCM_ENC_BPSK		0x01
#define HE_PHY_TX_DCM_ENC_QPSK		0x02
#define HE_PHY_TX_DCM_ENC_QAM		0x03

#define HE_PHY_TX_DCM_1_SS		0x00
#define HE_PHY_TX_DCM_2_SS		0x04

#define HE_PHY_RX_DCM_ENC_NOSUPPORT	0x00
#define HE_PHY_RX_DCM_ENC_BPSK		0x08
#define HE_PHY_RX_DCM_ENC_QPSK		0x10
#define HE_PHY_RX_DCM_ENC_QAM		0x18

#define HE_PHY_RX_DCM_1_SS		0x00
#define HE_PHY_RX_DCM_2_SS		0x20

/* HE Duration based RTS Threshold IEEE Draft P802.11ax D1.0 Figure 9-589cr */
#define HE_RTS_THRES_DISABLED		1023
#define HE_RTS_THRES_ALL_FRAMES		0
#define HE_RTS_THRES_MASK		0x03ff

/* Tx Rx HE MCS Support field format : IEEE Draft P802.11ax D0.5 Table 9-589cm */
#define HE_TX_RX_MCS_NSS_SUP_FIELD_MIN_SIZE	2	/* 2 bytes (16 bits) */

/* Fixed portion of the support field */
#define HE_MCS_NSS_MAX_NSS_M1_IDX	0
#define HE_MCS_NSS_MAX_NSS_M1_SZ	3
#define HE_MCS_NSS_MAX_MCS_IDX		3
#define HE_MCS_NSS_MAX_MCS_SZ		3
#define HE_MCS_NSS_TX_BW_BMP_IDX	6
#define HE_MCS_NSS_TX_BW_BMP_SZ		5
#define HE_MCS_NSS_RX_BW_BMP_IDX	11
#define HE_MCS_NSS_RX_BW_BMP_SZ		5

#define HE_CAP_MASK(idx, sz)		(((1 << sz) - 1) << idx)

/* Descriptor format IEEE Draft P802.11ax_D1.1 Figure 9-589cn */
#define HE_MCS_DESC_IDX			0
#define HE_MCS_DESC_SZ			4
#define HE_NSS_DESC_IDX			4
#define HE_NSS_DESC_SZ			3
#define HE_LAST_DESC_IDX		7
#define HE_LAST_DESC_SZ			1

#define HE_GET_DESC_MCS(desc)		((*((const uint8 *)desc) &\
		HE_CAP_MASK(HE_MCS_DESC_IDX, HE_MCS_DESC_SZ))\
		>> HE_MCS_DESC_IDX)
#define HE_GET_DESC_NSS(desc)		((*((const uint8 *)desc) &\
			HE_CAP_MASK(HE_NSS_DESC_IDX, HE_NSS_DESC_SZ))\
			>> HE_NSS_DESC_IDX)

/**
* Bandwidth configuration indices used in the HE TX-RX MCS support field
* IEEE Draft P802.11ax_D1.1 Section 9.4.2.218.4
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
		(mcs_nss_map) |= (((mcs_code) & HE_MCS_CODE_MASK) \
				<< HE_MCS_NSS_GET_SS_IDX(nss)); \
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

/* IEEE Draft P802.11ax D0.5 Table 9-262ab, Highest MCS Supported subfield encoding */
#define HE_CAP_MCS_CODE_0_7		0
#define HE_CAP_MCS_CODE_0_8		1
#define HE_CAP_MCS_CODE_0_9		2
#define HE_CAP_MCS_CODE_0_10		3
#define HE_CAP_MCS_CODE_0_11		4
#define HE_CAP_MCS_CODE_SIZE		3	/* num bits for 1-stream */
#define HE_CAP_MCS_CODE_MASK		0x7	/* mask for 1-stream */

/**
 * IEEE Draft P802.11ax D0.5 Figure 9-589cm
 * - Defines for TX & RX BW BITMAP
 *
 * (Size of TX BW bitmap = RX BW bitmap = 5 bits)
 */
#define HE_MCS_NSS_TX_BW_MASK		0x07c0
#define HE_MCS_NSS_TX_BW_SHIFT		6

#define HE_MCS_NSS_RX_BW_MASK		0xf800
#define HE_MCS_NSS_RX_BW_SHIFT		11

#define HE_CAP_MCS_MAP_NSS_MAX	8	/* Max number of streams possible */

#define HE_MAX_RU_COUNT		4	/* Max number of RU allocation possible */

#define HE_NSSM1_IDX		0	/* Offset of NSSM1 field */
#define HE_NSSM1_LEN		3	/* length of NSSM1 field in bits */

#define HE_RU_INDEX_MASK_IDX	3	/* Offset of RU index mask field */
#define HE_RU_INDEX_MASK_LEN	4	/* length of RU Index mask field in bits */

/* MU EDCA parameter set element */
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

/* For HE SU/RE SIG A : PLCP0 bit fields [32bit] */
#define HE_SU_RE_SIGA_FORMAT_MASK	0x00000001
#define HE_SU_RE_SIGA_RE_VAL		0x00000000
#define HE_SU_RE_SIGA_SU_VAL		0x00000001
#define HE_SU_RE_SIGA_FORMAT_SHIFT	0
#define HE_SU_RE_SIGA_UL_DL_SHIFT	2
#define HE_SU_RE_SIGA_MCS_MASK		0x00000078
#define HE_SU_RE_SIGA_MCS_SHIFT		3
#define HE_SU_RE_SIGA_DCM_MASK		0x00000080
#define HE_SU_RE_SIGA_DCM_SHIFT		7
#define HE_SU_RE_SIGA_BSS_COLOR_SHIFT	8	/* Bits 13:8 */
#define HE_SU_RE_SIGA_BSS_COLOR_MASK	0x00003F00
#define HE_SU_RE_SIGA_RSVD_PLCP0_VAL	0x00004000
#define HE_SU_SIGA_BW_MASK		0x00180000
#define HE_SU_SIGA_BW_SHIFT		19
#define HE_RE_SIGA_TONE_MASK		0x00180000
#define HE_RE_SIGA_TONE_SHIFT		19
#define HE_SU_RE_SIGA_20MHZ_VAL		0x00000000
#define HE_SU_RE_SIGA_40MHZ_VAL		0x00080000
#define HE_SU_RE_SIGA_80MHZ_VAL		0x00100000
#define HE_SU_RE_SIGA_160MHZ_VAL	0x00180000
#define HE_SU_RE_SIGA_GI_LTF_MASK	0x00600000
#define HE_SU_RE_SIGA_1xLTF_GI8us_VAL	0x00000000
#define HE_SU_RE_SIGA_2xLTF_GI8us_VAL	0x00200000
#define HE_SU_RE_SIGA_2xLTF_GI16us_VAL	0x00400000
#define HE_SU_RE_SIGA_4xLTF_GI32us_VAL	0x00600000
#define HE_SU_RE_SIGA_GI_LTF_SHIFT	21
#define HE_SU_RE_SIGA_NSTS_MASK		0x03800000
#define HE_SU_RE_SIGA_NSTS_SHIFT	23
#define HE_SU_RE_SIGA_TXOP_PLCP0_MASK	0xFC000000
#define HE_SU_RE_SIGA_TXOP_PLCP0_SHIFT	26

/* For HE MU SIG A : PLCP0 bit fields [32bit] */
#define HE_MU_SIGA_UL_DL_SHIFT		0
#define HE_MU_SIGA_UL_TB_PPDU		0
#define HE_MU_SIGA_SIGB_MCS_SHIFT	1
#define HE_MU_SIGA_SIGB_DCM_SHIFT	4
#define HE_MU_SIGA_SIGB_DCM_DISABLED	0
#define HE_MU_SIGA_BW_SHIFT		15
#define HE_MU_SIGA_BW_80_UNPUNCTURED	2
#define HE_MU_SIGA_BW_SEC_20_PUNCTURED	4
#define HE_MU_SIGA_BW_SEC_40_PUNCTURED	5
#define HE_MU_SIGA_SIGB_SYMS_SHIFT	18
#define HE_MU_SIGA_GI_LTF_SHIFT		23

/* PLCP1 starts with B6 of HE SIG A 2 */

/* For HE SU/RE SIG A : PLCP1 bit fields [16bit] */
#define HE_SU_RE_SIGA_TXOP_PLCP1_MASK	0x0001
#define HE_SU_RE_SIGA_TXOP_PLCP1_SHIFT	0
#define HE_SU_RE_SIGA_CODING_MASK	0x0002
#define HE_SU_RE_SIGA_CODING_SHIFT	1
#define HE_SU_RE_SIGA_STBC_MASK		0x0008
#define HE_SU_RE_SIGA_STBC_SHIFT	3
#define HE_SU_RE_SIGA_BEAMFORM_MASK	0x0010
#define HE_SU_RE_SIGA_BEAMFORM_SHIFT	4
#define HE_SU_RE_SIGA_RSVD_PLCP1_VAL	0x0100

/* For HE MU SIG A : PLCP1 bit fields [16bit] */
#define HE_MU_SIGA_RSVD_SHIFT		1
#define HE_MU_SIGA_LTF_SYMS_SHIFT	2

/* PPE Threshold field (figure 9-589co) */
#define HE_PPE_THRESH_NSS_RU_FSZ	3

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
 * HE Operation IE (sec 9.4.2.219)
 */
/* HE Operation Parameters field (figure 9-589cr) */
#define HE_OP_PARAMS_SIZE		3
typedef uint8 he_op_parms_t[HE_OP_PARAMS_SIZE];

#define HE_OP_BSS_COLOR_INFO		1
typedef uint8 he_op_bsscolorinfo_t[HE_OP_BSS_COLOR_INFO];

#define HE_BASIC_MCS_NSS_SIZE		2
typedef uint8 he_basic_mcs_nss_set_t[HE_BASIC_MCS_NSS_SIZE];

/* VHT_OP_INFO_LEN = 3 defined in 802.11.h file */
typedef uint8 he_vht_opinfo_t[VHT_OP_INFO_LEN];

#define HE_OP_MAX_BSSID_IND_LEN		1
typedef uint8 he_max_bssid_ind_t[HE_OP_MAX_BSSID_IND_LEN];

/* 6G Operation Information Element field (Figure 9-788k) */
#define HE_6G_OP_INFO			5
typedef uint8 he_6g_opinfo_t[HE_6G_OP_INFO];

/* HE Operation Parameters for D3.0 */
#define HE_OP_DEFAULT_PE_DUR_IDX		0	/* Default PE Duration */
#define HE_OP_DEFAULT_PE_DUR_FSZ		3
#define HE_OP_TWT_REQUIRED_IDX			3	/* TWT Required */
#define HE_OP_TWT_REQUIRED_FSZ			1
#define HE_OP_TXOP_DUR_RTS_THOLD_IDX		4	/* TXOP Duration RTS Threshold */
#define HE_OP_TXOP_DUR_RTS_THOLD_FSZ		10
#define HE_OP_VHT_OP_INFO_PRESENT_IDX		14	/* VHT Operation Information Present */
#define HE_OP_VHT_OP_INFO_PRESENT_FSZ		1
#define HE_OP_CO_LOCATED_BSS_IDX		15	/* Co-Located BSS */
#define HE_OP_CO_LOCATED_BSS_FSZ		1
#define HE_OP_ER_SU_DISABLE_IDX			16	/* ER SU Disable */
#define HE_OP_ER_SU_DISABLE_FSZ			1
#define HE_OP_6G_OP_INFO_PRESENT_IDX		17	/* 6G Operation Information Present */
#define HE_OP_6G_OP_INFO_PRESENT_FSZ		1
#define HE_OP_RESERVED_IDX			18	/* Reserved */
#define HE_OP_RESERVED_FSZ			6

/* BSS Color for D3.0 */
#define HE_OP_BSS_COLOR_IDX			0	/* BSS Color */
#define HE_OP_BSS_COLOR_FSZ			6
#define HE_OP_PARTIAL_BSS_COLOR_IDX		6	/* Partial BSS color */
#define HE_OP_PARTIAL_BSS_COLOR_FSZ		1
#define HE_OP_BSS_COLOR_DIS_IDX			7	/* BSS Color Disabled */
#define HE_OP_BSS_COLOR_DIS_FSZ			1

/* 6 Ghz Operation Information Element for D8.0 */
#define HE_6G_OP_INFO_PRI_CHANNEL_IDX		0	/* Primary channel */
#define HE_6G_OP_INFO_PRI_CHANNEL_FSZ		8
#define HE_6G_OP_INFO_CONTROL_IDX		8	/* Control Field */
#define HE_6G_OP_INFO_CONTROL_FSZ		8
#define HE_6G_OP_INFO_FREQ_SEG0_IDX		16	/* Center Frequency segment0 */
#define HE_6G_OP_INFO_FREQ_SEG0_FSZ		8
#define HE_6G_OP_INFO_FREQ_SEG1_IDX		24	/* Center Frequency segment1 */
#define HE_6G_OP_INFO_FREQ_SEG1_FSZ		8
#define HE_6G_OP_INFO_MIN_RATE_IDX		32	/* Min Rate */
#define HE_6G_OP_INFO_MIN_RATE_FSZ		8
#define HE_6G_OP_INFO_CONTROL_IDX_CW_FSZ	2

/* Control Field Format (Figure 9-788I) */
#define HE_6G_CONTROL_CHANNEL_WIDTH_IDX		0	/* Channel Width */
#define HE_6G_CONTROL_CHANNEL_WIDTH_FSZ		2
#define HE_6G_CONTROL_DUP_BCN_IDX		2	/* Duplicate beacon */
#define HE_6G_CONTROL_DUP_BCN_FSZ		1
#define HE_6G_CONTROL_REG_INFO_IDX		3	/* Regulatory info */
#define HE_6G_CONTROL_REG_INFO_FSZ		3

/* HE Operation element */
BWL_PRE_PACKED_STRUCT struct he_op_ie {
	uint8 id;
	uint8 len;
	uint8 id_ext;
	he_op_parms_t parms;
	he_op_bsscolorinfo_t bsscolorinfo;
	he_basic_mcs_nss_set_t mcs_nss_op;	/* Basic HE MCS & NSS Set */
	/* he_vht_opinfo_t vht_opinfo; */	/* VHT Operation Information element */
	/* he_max_bssid_ind_t max_bssid_ind; */	/* Max Co-Hosted BSSID Indicator element */
	/* he_6g_opinfo_t he_6g_opinfo;	*/	/* 6 GHz Operation Information element */
} BWL_POST_PACKED_STRUCT;

typedef struct he_op_ie he_op_ie_t;

/* The Max HE MCS For n SS subfield (where n = 1, ..., 8) is encoded as follows:
 * P802.11ax D1.1 P94L53 - P94L61:
 */
#define HE_OP_MCS_CODE_0_7		0
#define HE_OP_MCS_CODE_0_8		1
#define HE_OP_MCS_CODE_0_9		2
#define HE_OP_MCS_CODE_0_10		3
#define HE_OP_MCS_CODE_0_11		4
#define HE_OP_MCS_CODE_NONE		7
#define HE_OP_MCS_CODE_SIZE		3	/* num bits */
#define HE_OP_MCS_CODE_MASK		0x7	/* mask for 1-stream */

/* Defines for The Max HE MCS For n SS subfield (where n = 1, ..., 8) */
#define HE_OP_MCS_NSS_SET_MASK		0x00ffffff /* Field is to be 24 bits long */
#define HE_OP_MCS_NSS_GET_SS_IDX(nss) (((nss)-1) * HE_OP_MCS_CODE_SIZE)
#define HE_OP_MCS_NSS_GET_MCS(nss, mcs_nss_map) \
	(((mcs_nss_map) >> HE_OP_MCS_NSS_GET_SS_IDX(nss)) & HE_OP_MCS_CODE_MASK)
#define HE_OP_MCS_NSS_SET_MCS(nss, mcs_code, mcs_nss_map) \
	do { \
		(mcs_nss_map) &= (~(HE_OP_MCS_CODE_MASK << HE_OP_MCS_NSS_GET_SS_IDX(nss))); \
		(mcs_nss_map) |= (((mcs_code) & HE_OP_MCS_CODE_MASK) \
				<< HE_OP_MCS_NSS_GET_SS_IDX(nss)); \
		(mcs_nss_map) &= (HE_OP_MCS_NSS_SET_MASK); \
	} while (0)

#define HE_OP_IE_MIN_LEN	(sizeof(he_op_ie_t) - TLV_HDR_LEN)
#define HE_OP_IE_MAX_LEN	(sizeof(he_op_ie_t) - TLV_HDR_LEN + VHT_OP_INFO_LEN +\
	HE_OP_MAX_BSSID_IND_LEN + HE_6G_OP_INFO)

/* bit position and field width */
#define HE_BSSCOLOR_CHANGE_NEWCOLOR_IDX		0	/* New BSSColor info */
#define HE_BSSCOLOR_CHANGE_NEWCOLOR_FSZ		6

/* HE Bsscolor change element */
BWL_PRE_PACKED_STRUCT struct he_bsscolor_change_ie {
	uint8 id;
	uint8 len;
	uint8 id_ext;
	uint8 color_switch_cntdwn;
	uint8 new_bsscolor_info;
} BWL_POST_PACKED_STRUCT;

typedef struct he_bsscolor_change_ie he_bsscolor_change_ie_t;

/*
 * HE 6 GHz Band Capabilities element (sec 9.4.2.263)
 * Capabilities Information field format (figure 9-788aj)
 */

#define HE_6GBAND_CAP_IE_SIZE	2
typedef uint8 he_6gband_cap_t[HE_6GBAND_CAP_IE_SIZE];

/* HE 6 GHz Band Capabilities */
#define HE_6GBAND_MPDU_STRT_SPACE_IDX	0	/* Minimum MPDU Start Spacing */
#define HE_6GBAND_MPDU_STRT_SPACE_FSZ	3
#define HE_6GBAND_MAX_AMPDU_LENEXP_IDX	3	/* Maximum A-MPDU Length Exponent */
#define HE_6GBAND_MAX_AMPDU_LENEXP_FSZ	3
#define HE_6GBAND_MAX_MPDU_LEN_IDX	6	/* Maximum MPDU Length */
#define HE_6GBAND_MAX_MPDU_LEN_FSZ	2
/* B8 is reserved */
#define HE_6GBAND_SM_PWRSAVE_IDX	9	/* SM Power Save */
#define HE_6GBAND_SM_PWRSAVE_FSZ	2
#define HE_6GBAND_RD_RESP_IDX	11	/* RD Responder */
#define HE_6GBAND_RD_RESP_FSZ	1
#define HE_6GBAND_RXANT_PAT_IDX		12	/* Rx Antenna Pattern Consistency */
#define HE_6GBAND_RXANT_PAT_FSZ		1
#define HE_6GBAND_TXANT_PAT_IDX		13	/* Tx Antenna Pattern Consistency */
#define HE_6GBAND_TXANT_PAT_FSZ		1
/* B14-15 are reserved */

BWL_PRE_PACKED_STRUCT struct he_6gband_cap_ie {
	uint8 id;
	uint8 len;
	uint8 id_ext;
	he_6gband_cap_t	he_6gband_cap;
} BWL_POST_PACKED_STRUCT;

typedef struct he_6gband_cap_ie he_6gband_cap_ie_t;

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

/* HE Action Frame */
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
#define HE_TRIG_TYPE_MU_RTS_FRM			3	/* MU-RTS frame */
#define HE_TRIG_TYPE_BSR_FRM			4	/* Buffer status report poll */

/* HE Timing related parameters (802.11ax D1.2 Table 28-9) */
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
#define HE_MAX_26_TONE_RU_INDX		36
#define HE_MAX_52_TONE_RU_INDX		52
#define HE_MAX_106_TONE_RU_INDX		60
#define HE_MAX_242_TONE_RU_INDX		64
#define HE_MAX_484_TONE_RU_INDX		66

/**
 * Ref : (802.11ax D3.0 Figure 9-27 Page 85)
 */
#define HE_BAR_CONTROL_SZ	2
typedef uint8 he_bar_control_set_t[HE_BAR_CONTROL_SZ];

/* bit position and field width */
#define HE_BAR_CONTROL_ACK_POLICY_INDX		0	/* BAR ack policy */
#define HE_BAR_CONTROL_ACK_POLICY_FSZ		1
#define HE_BAR_CONTROL_ACK_TYPE_INDX		1	/* BAR ack type */
#define HE_BAR_CONTROL_ACK_TYPE_FSZ		4
#define HE_BAR_CONTROL_RSVD_INDX		5	/* Reserved */
#define HE_BAR_CONTROL_RSVD_FSZ			7
#define HE_BAR_CONTROL_TID_INFO_INDX		12	/* BAR TID INFO */
#define HE_BAR_CONTROL_TID_INFO_FSZ		4

#define BAR_TYPE_BASIC				0
#define BAR_TYPE_EXT_COMPRESSED			1
#define BAR_TYPE_COMPRESSED			2
#define BAR_TYPE_MULTI_TID			3

/**
 * Ref : 802.11-2016.pdf Page 674
 * Figure 9-28 Block Ack Starting Sequence Control subfield
 */
#define HE_BAR_INFO_SZ	2
typedef uint8 he_cba_bar_info_set_t[HE_BAR_INFO_SZ];

/* bit position and field width */
#define HE_CBA_BAR_INFO_FRAGNUM_INDX		0	/* Fragment Number */
#define HE_CBA_BAR_INFO_FRAGNUM_FSZ		4
#define HE_CBA_BAR_INFO_SEQNUM_INDX		4	/* Starting Sequence Number */
#define HE_CBA_BAR_INFO_SEQNUM_FSZ		12

/**
 * ref: (802.11ax D1.2 Table 28-9 Page 285)
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
