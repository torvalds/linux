/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_CAM_H__
#define __RTW89_CAM_H__

#include "core.h"

#define RTW89_SEC_CAM_LEN	20

#define RTW89_BSSID_MATCH_ALL GENMASK(5, 0)
#define RTW89_BSSID_MATCH_5_BYTES GENMASK(4, 0)

static inline void FWCMD_SET_ADDR_IDX(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 1, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_OFFSET(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 1, value, GENMASK(15, 8));
}

static inline void FWCMD_SET_ADDR_LEN(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 1, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_VALID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, BIT(0));
}

static inline void FWCMD_SET_ADDR_NET_TYPE(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(2, 1));
}

static inline void FWCMD_SET_ADDR_BCN_HIT_COND(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(4, 3));
}

static inline void FWCMD_SET_ADDR_HIT_RULE(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(6, 5));
}

static inline void FWCMD_SET_ADDR_BB_SEL(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, BIT(7));
}

static inline void FWCMD_SET_ADDR_ADDR_MASK(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(13, 8));
}

static inline void FWCMD_SET_ADDR_MASK_SEL(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(15, 14));
}

static inline void FWCMD_SET_ADDR_SMA_HASH(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_TMA_HASH(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 2, value, GENMASK(31, 24));
}

static inline void FWCMD_SET_ADDR_BSSID_CAM_IDX(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 3, value, GENMASK(5, 0));
}

static inline void FWCMD_SET_ADDR_SMA0(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 4, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_SMA1(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 4, value, GENMASK(15, 8));
}

static inline void FWCMD_SET_ADDR_SMA2(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 4, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_SMA3(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 4, value, GENMASK(31, 24));
}

static inline void FWCMD_SET_ADDR_SMA4(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 5, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_SMA5(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 5, value, GENMASK(15, 8));
}

static inline void FWCMD_SET_ADDR_TMA0(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 5, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_TMA1(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 5, value, GENMASK(31, 24));
}

static inline void FWCMD_SET_ADDR_TMA2(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 6, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_TMA3(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 6, value, GENMASK(15, 8));
}

static inline void FWCMD_SET_ADDR_TMA4(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 6, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_TMA5(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 6, value, GENMASK(31, 24));
}

static inline void FWCMD_SET_ADDR_MACID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 8, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_PORT_INT(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 8, value, GENMASK(10, 8));
}

static inline void FWCMD_SET_ADDR_TSF_SYNC(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 8, value, GENMASK(13, 11));
}

static inline void FWCMD_SET_ADDR_TF_TRS(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 8, value, BIT(14));
}

static inline void FWCMD_SET_ADDR_LSIG_TXOP(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 8, value, BIT(15));
}

static inline void FWCMD_SET_ADDR_TGT_IND(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 8, value, GENMASK(26, 24));
}

static inline void FWCMD_SET_ADDR_FRM_TGT_IND(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 8, value, GENMASK(29, 27));
}

static inline void FWCMD_SET_ADDR_AID12(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(11, 0));
}

static inline void FWCMD_SET_ADDR_AID12_0(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_AID12_1(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(11, 8));
}

static inline void FWCMD_SET_ADDR_WOL_PATTERN(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, BIT(12));
}

static inline void FWCMD_SET_ADDR_WOL_UC(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, BIT(13));
}

static inline void FWCMD_SET_ADDR_WOL_MAGIC(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, BIT(14));
}

static inline void FWCMD_SET_ADDR_WAPI(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, BIT(15));
}

static inline void FWCMD_SET_ADDR_SEC_ENT_MODE(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(17, 16));
}

static inline void FWCMD_SET_ADDR_SEC_ENT0_KEYID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(19, 18));
}

static inline void FWCMD_SET_ADDR_SEC_ENT1_KEYID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(21, 20));
}

static inline void FWCMD_SET_ADDR_SEC_ENT2_KEYID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(23, 22));
}

static inline void FWCMD_SET_ADDR_SEC_ENT3_KEYID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(25, 24));
}

static inline void FWCMD_SET_ADDR_SEC_ENT4_KEYID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(27, 26));
}

static inline void FWCMD_SET_ADDR_SEC_ENT5_KEYID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(29, 28));
}

static inline void FWCMD_SET_ADDR_SEC_ENT6_KEYID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 9, value, GENMASK(31, 30));
}

static inline void FWCMD_SET_ADDR_SEC_ENT_VALID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 10, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_SEC_ENT0(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 10, value, GENMASK(15, 8));
}

static inline void FWCMD_SET_ADDR_SEC_ENT1(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 10, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_SEC_ENT2(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 10, value, GENMASK(31, 24));
}

static inline void FWCMD_SET_ADDR_SEC_ENT3(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 11, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_SEC_ENT4(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 11, value, GENMASK(15, 8));
}

static inline void FWCMD_SET_ADDR_SEC_ENT5(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 11, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_SEC_ENT6(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 11, value, GENMASK(31, 24));
}

static inline void FWCMD_SET_ADDR_BSSID_IDX(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 12, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_BSSID_OFFSET(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 12, value, GENMASK(15, 8));
}

static inline void FWCMD_SET_ADDR_BSSID_LEN(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 12, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_BSSID_VALID(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 13, value, BIT(0));
}

static inline void FWCMD_SET_ADDR_BSSID_BB_SEL(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 13, value, BIT(1));
}

static inline void FWCMD_SET_ADDR_BSSID_MASK(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 13, value, GENMASK(7, 2));
}

static inline void FWCMD_SET_ADDR_BSSID_BSS_COLOR(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 13, value, GENMASK(13, 8));
}

static inline void FWCMD_SET_ADDR_BSSID_BSSID0(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 13, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_BSSID_BSSID1(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 13, value, GENMASK(31, 24));
}

static inline void FWCMD_SET_ADDR_BSSID_BSSID2(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 14, value, GENMASK(7, 0));
}

static inline void FWCMD_SET_ADDR_BSSID_BSSID3(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 14, value, GENMASK(15, 8));
}

static inline void FWCMD_SET_ADDR_BSSID_BSSID4(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 14, value, GENMASK(23, 16));
}

static inline void FWCMD_SET_ADDR_BSSID_BSSID5(void *cmd, u32 value)
{
	le32p_replace_bits((__le32 *)(cmd) + 14, value, GENMASK(31, 24));
}

struct rtw89_h2c_dctlinfo_ud_v1 {
	__le32 c0;
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
	__le32 w4;
	__le32 w5;
	__le32 w6;
	__le32 w7;
	__le32 m0;
	__le32 m1;
	__le32 m2;
	__le32 m3;
	__le32 m4;
	__le32 m5;
	__le32 m6;
	__le32 m7;
} __packed;

#define DCTLINFO_V1_C0_MACID GENMASK(6, 0)
#define DCTLINFO_V1_C0_OP BIT(7)

#define DCTLINFO_V1_W0_QOS_FIELD_H GENMASK(7, 0)
#define DCTLINFO_V1_W0_HW_EXSEQ_MACID GENMASK(14, 8)
#define DCTLINFO_V1_W0_QOS_DATA BIT(15)
#define DCTLINFO_V1_W0_AES_IV_L GENMASK(31, 16)
#define DCTLINFO_V1_W0_ALL GENMASK(31, 0)
#define DCTLINFO_V1_W1_AES_IV_H GENMASK(31, 0)
#define DCTLINFO_V1_W1_ALL GENMASK(31, 0)
#define DCTLINFO_V1_W2_SEQ0 GENMASK(11, 0)
#define DCTLINFO_V1_W2_SEQ1 GENMASK(23, 12)
#define DCTLINFO_V1_W2_AMSDU_MAX_LEN GENMASK(26, 24)
#define DCTLINFO_V1_W2_STA_AMSDU_EN BIT(27)
#define DCTLINFO_V1_W2_CHKSUM_OFLD_EN BIT(28)
#define DCTLINFO_V1_W2_WITH_LLC BIT(29)
#define DCTLINFO_V1_W2_ALL GENMASK(29, 0)
#define DCTLINFO_V1_W3_SEQ2 GENMASK(11, 0)
#define DCTLINFO_V1_W3_SEQ3 GENMASK(23, 12)
#define DCTLINFO_V1_W3_TGT_IND GENMASK(27, 24)
#define DCTLINFO_V1_W3_TGT_IND_EN BIT(28)
#define DCTLINFO_V1_W3_HTC_LB GENMASK(31, 29)
#define DCTLINFO_V1_W3_ALL GENMASK(31, 0)
#define DCTLINFO_V1_W4_MHDR_LEN GENMASK(4, 0)
#define DCTLINFO_V1_W4_VLAN_TAG_VALID BIT(5)
#define DCTLINFO_V1_W4_VLAN_TAG_SEL GENMASK(7, 6)
#define DCTLINFO_V1_W4_HTC_ORDER BIT(8)
#define DCTLINFO_V1_W4_SEC_KEY_ID GENMASK(10, 9)
#define DCTLINFO_V1_W4_WAPI BIT(15)
#define DCTLINFO_V1_W4_SEC_ENT_MODE GENMASK(17, 16)
#define DCTLINFO_V1_W4_SEC_ENT0_KEYID GENMASK(19, 18)
#define DCTLINFO_V1_W4_SEC_ENT1_KEYID GENMASK(21, 20)
#define DCTLINFO_V1_W4_SEC_ENT2_KEYID GENMASK(23, 22)
#define DCTLINFO_V1_W4_SEC_ENT3_KEYID GENMASK(25, 24)
#define DCTLINFO_V1_W4_SEC_ENT4_KEYID GENMASK(27, 26)
#define DCTLINFO_V1_W4_SEC_ENT5_KEYID GENMASK(29, 28)
#define DCTLINFO_V1_W4_SEC_ENT6_KEYID GENMASK(31, 30)
#define DCTLINFO_V1_W4_ALL (GENMASK(31, 15) | GENMASK(10, 0))
#define DCTLINFO_V1_W5_SEC_ENT_VALID GENMASK(7, 0)
#define DCTLINFO_V1_W5_SEC_ENT0 GENMASK(15, 8)
#define DCTLINFO_V1_W5_SEC_ENT1 GENMASK(23, 16)
#define DCTLINFO_V1_W5_SEC_ENT2 GENMASK(31, 24)
#define DCTLINFO_V1_W5_ALL GENMASK(31, 0)
#define DCTLINFO_V1_W6_SEC_ENT3 GENMASK(7, 0)
#define DCTLINFO_V1_W6_SEC_ENT4 GENMASK(15, 8)
#define DCTLINFO_V1_W6_SEC_ENT5 GENMASK(23, 16)
#define DCTLINFO_V1_W6_SEC_ENT6 GENMASK(31, 24)
#define DCTLINFO_V1_W6_ALL GENMASK(31, 0)

struct rtw89_h2c_dctlinfo_ud_v2 {
	__le32 c0;
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
	__le32 w4;
	__le32 w5;
	__le32 w6;
	__le32 w7;
	__le32 w8;
	__le32 w9;
	__le32 w10;
	__le32 w11;
	__le32 w12;
	__le32 w13;
	__le32 w14;
	__le32 w15;
	__le32 m0;
	__le32 m1;
	__le32 m2;
	__le32 m3;
	__le32 m4;
	__le32 m5;
	__le32 m6;
	__le32 m7;
	__le32 m8;
	__le32 m9;
	__le32 m10;
	__le32 m11;
	__le32 m12;
	__le32 m13;
	__le32 m14;
	__le32 m15;
} __packed;

#define DCTLINFO_V2_C0_MACID GENMASK(6, 0)
#define DCTLINFO_V2_C0_OP BIT(7)

#define DCTLINFO_V2_W0_QOS_FIELD_H GENMASK(7, 0)
#define DCTLINFO_V2_W0_HW_EXSEQ_MACID GENMASK(14, 8)
#define DCTLINFO_V2_W0_QOS_DATA BIT(15)
#define DCTLINFO_V2_W0_AES_IV_L GENMASK(31, 16)
#define DCTLINFO_V2_W0_ALL GENMASK(31, 0)
#define DCTLINFO_V2_W1_AES_IV_H GENMASK(31, 0)
#define DCTLINFO_V2_W1_ALL GENMASK(31, 0)
#define DCTLINFO_V2_W2_SEQ0 GENMASK(11, 0)
#define DCTLINFO_V2_W2_SEQ1 GENMASK(23, 12)
#define DCTLINFO_V2_W2_AMSDU_MAX_LEN GENMASK(26, 24)
#define DCTLINFO_V2_W2_STA_AMSDU_EN BIT(27)
#define DCTLINFO_V2_W2_CHKSUM_OFLD_EN BIT(28)
#define DCTLINFO_V2_W2_WITH_LLC BIT(29)
#define DCTLINFO_V2_W2_NAT25_EN BIT(30)
#define DCTLINFO_V2_W2_IS_MLD BIT(31)
#define DCTLINFO_V2_W2_ALL GENMASK(31, 0)
#define DCTLINFO_V2_W3_SEQ2 GENMASK(11, 0)
#define DCTLINFO_V2_W3_SEQ3 GENMASK(23, 12)
#define DCTLINFO_V2_W3_TGT_IND GENMASK(27, 24)
#define DCTLINFO_V2_W3_TGT_IND_EN BIT(28)
#define DCTLINFO_V2_W3_HTC_LB GENMASK(31, 29)
#define DCTLINFO_V2_W3_ALL GENMASK(31, 0)
#define DCTLINFO_V2_W4_VLAN_TAG_SEL GENMASK(7, 5)
#define DCTLINFO_V2_W4_HTC_ORDER BIT(8)
#define DCTLINFO_V2_W4_SEC_KEY_ID GENMASK(10, 9)
#define DCTLINFO_V2_W4_VLAN_RX_DYNAMIC_PCP_EN BIT(11)
#define DCTLINFO_V2_W4_VLAN_RX_PKT_DROP BIT(12)
#define DCTLINFO_V2_W4_VLAN_RX_VALID BIT(13)
#define DCTLINFO_V2_W4_VLAN_TX_VALID BIT(14)
#define DCTLINFO_V2_W4_WAPI BIT(15)
#define DCTLINFO_V2_W4_SEC_ENT_MODE GENMASK(17, 16)
#define DCTLINFO_V2_W4_SEC_ENT0_KEYID GENMASK(19, 18)
#define DCTLINFO_V2_W4_SEC_ENT1_KEYID GENMASK(21, 20)
#define DCTLINFO_V2_W4_SEC_ENT2_KEYID GENMASK(23, 22)
#define DCTLINFO_V2_W4_SEC_ENT3_KEYID GENMASK(25, 24)
#define DCTLINFO_V2_W4_SEC_ENT4_KEYID GENMASK(27, 26)
#define DCTLINFO_V2_W4_SEC_ENT5_KEYID GENMASK(29, 28)
#define DCTLINFO_V2_W4_SEC_ENT6_KEYID GENMASK(31, 30)
#define DCTLINFO_V2_W4_ALL GENMASK(31, 5)
#define DCTLINFO_V2_W5_SEC_ENT7_KEYID GENMASK(1, 0)
#define DCTLINFO_V2_W5_SEC_ENT8_KEYID GENMASK(3, 2)
#define DCTLINFO_V2_W5_SEC_ENT_VALID_V1 GENMASK(23, 8)
#define DCTLINFO_V2_W5_SEC_ENT0_V1 GENMASK(31, 24)
#define DCTLINFO_V2_W5_ALL (GENMASK(31, 8) | GENMASK(3, 0))
#define DCTLINFO_V2_W6_SEC_ENT1_V1 GENMASK(7, 0)
#define DCTLINFO_V2_W6_SEC_ENT2_V1 GENMASK(15, 8)
#define DCTLINFO_V2_W6_SEC_ENT3_V1 GENMASK(23, 16)
#define DCTLINFO_V2_W6_SEC_ENT4_V1 GENMASK(31, 24)
#define DCTLINFO_V2_W6_ALL GENMASK(31, 0)
#define DCTLINFO_V2_W7_SEC_ENT5_V1 GENMASK(7, 0)
#define DCTLINFO_V2_W7_SEC_ENT6_V1 GENMASK(15, 8)
#define DCTLINFO_V2_W7_SEC_ENT7 GENMASK(23, 16)
#define DCTLINFO_V2_W7_SEC_ENT8 GENMASK(31, 24)
#define DCTLINFO_V2_W7_ALL GENMASK(31, 0)
#define DCTLINFO_V2_W8_MLD_SMA_L_V1 GENMASK(31, 0)
#define DCTLINFO_V2_W8_ALL GENMASK(31, 0)
#define DCTLINFO_V2_W9_MLD_SMA_H_V1 GENMASK(15, 0)
#define DCTLINFO_V2_W9_MLD_TMA_L_V1 GENMASK(31, 16)
#define DCTLINFO_V2_W9_ALL GENMASK(31, 0)
#define DCTLINFO_V2_W10_MLD_TMA_H_V1 GENMASK(31, 0)
#define DCTLINFO_V2_W10_ALL GENMASK(31, 0)
#define DCTLINFO_V2_W11_MLD_TA_BSSID_L_V1 GENMASK(31, 0)
#define DCTLINFO_V2_W11_ALL GENMASK(31, 0)
#define DCTLINFO_V2_W12_MLD_TA_BSSID_H_V1 GENMASK(15, 0)
#define DCTLINFO_V2_W12_ALL GENMASK(15, 0)

int rtw89_cam_init(struct rtw89_dev *rtwdev, struct rtw89_vif_link *vif);
void rtw89_cam_deinit(struct rtw89_dev *rtwdev, struct rtw89_vif_link *vif);
int rtw89_cam_init_addr_cam(struct rtw89_dev *rtwdev,
			    struct rtw89_addr_cam_entry *addr_cam,
			    const struct rtw89_bssid_cam_entry *bssid_cam);
void rtw89_cam_deinit_addr_cam(struct rtw89_dev *rtwdev,
			       struct rtw89_addr_cam_entry *addr_cam);
int rtw89_cam_init_bssid_cam(struct rtw89_dev *rtwdev,
			     struct rtw89_vif_link *rtwvif_link,
			     struct rtw89_bssid_cam_entry *bssid_cam,
			     const u8 *bssid);
void rtw89_cam_deinit_bssid_cam(struct rtw89_dev *rtwdev,
				struct rtw89_bssid_cam_entry *bssid_cam);
void rtw89_cam_fill_addr_cam_info(struct rtw89_dev *rtwdev,
				  struct rtw89_vif_link *vif,
				  struct rtw89_sta *rtwsta,
				  const u8 *scan_mac_addr, u8 *cmd);
void rtw89_cam_fill_dctl_sec_cam_info_v1(struct rtw89_dev *rtwdev,
					 struct rtw89_vif_link *rtwvif_link,
					 struct rtw89_sta *rtwsta,
					 struct rtw89_h2c_dctlinfo_ud_v1 *h2c);
void rtw89_cam_fill_dctl_sec_cam_info_v2(struct rtw89_dev *rtwdev,
					 struct rtw89_vif_link *rtwvif_link,
					 struct rtw89_sta *rtwsta,
					 struct rtw89_h2c_dctlinfo_ud_v2 *h2c);
int rtw89_cam_fill_bssid_cam_info(struct rtw89_dev *rtwdev,
				  struct rtw89_vif_link *rtwvif_link,
				  struct rtw89_sta *rtwsta, u8 *cmd);
int rtw89_cam_sec_key_add(struct rtw89_dev *rtwdev,
			  struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key);
int rtw89_cam_sec_key_del(struct rtw89_dev *rtwdev,
			  struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key,
			  bool inform_fw);
void rtw89_cam_bssid_changed(struct rtw89_dev *rtwdev,
			     struct rtw89_vif_link *rtwvif_link);
void rtw89_cam_reset_keys(struct rtw89_dev *rtwdev);
#endif
