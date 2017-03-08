#ifndef _HALMAC_TX_DESC_AP_H_
#define _HALMAC_TX_DESC_AP_H_
#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

/*TXDESC_WORD0*/

#define SET_TX_DESC_DISQSELSEQ(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0x1, 31)
#define SET_TX_DESC_DISQSELSEQ_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0x1, 31)
#define GET_TX_DESC_DISQSELSEQ(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, 0x1, 31)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_GF(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0x1, 30)
#define SET_TX_DESC_GF_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0x1, 30)
#define GET_TX_DESC_GF(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, 0x1, 30)
#define SET_TX_DESC_NO_ACM(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0x1, 29)
#define SET_TX_DESC_NO_ACM_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0x1, 29)
#define GET_TX_DESC_NO_ACM(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, 0x1, 29)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_BCNPKT_TSF_CTRL(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0x1, 28)
#define SET_TX_DESC_BCNPKT_TSF_CTRL_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0x1, 28)
#define GET_TX_DESC_BCNPKT_TSF_CTRL(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, 0x1, 28)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_AMSDU_PAD_EN(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0x1, 27)
#define SET_TX_DESC_AMSDU_PAD_EN_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0x1, 27)
#define GET_TX_DESC_AMSDU_PAD_EN(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, 0x1, 27)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_LS(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0x1, 26)
#define SET_TX_DESC_LS_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0x1, 26)
#define GET_TX_DESC_LS(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, 0x1, 26)
#define SET_TX_DESC_HTC(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0x1, 25)
#define SET_TX_DESC_HTC_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0x1, 25)
#define GET_TX_DESC_HTC(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, 0x1, 25)
#define SET_TX_DESC_BMC(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0x1, 24)
#define SET_TX_DESC_BMC_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0x1, 24)
#define GET_TX_DESC_BMC(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, 0x1, 24)
#define SET_TX_DESC_OFFSET(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0xff, 16)
#define SET_TX_DESC_OFFSET_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0xff, 16)
#define GET_TX_DESC_OFFSET(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, 0xff, 16)
#define SET_TX_DESC_TXPKTSIZE(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0xffff, 0)
#define SET_TX_DESC_TXPKTSIZE_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, __Value, 0xffff, 0)
#define GET_TX_DESC_TXPKTSIZE(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword0, 0xffff, 0)

/*TXDESC_WORD1*/

#define SET_TX_DESC_MOREDATA(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x1, 29)
#define SET_TX_DESC_MOREDATA_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x1, 29)
#define GET_TX_DESC_MOREDATA(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, 0x1, 29)
#define SET_TX_DESC_PKT_OFFSET(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x1f, 24)
#define SET_TX_DESC_PKT_OFFSET_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x1f, 24)
#define GET_TX_DESC_PKT_OFFSET(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, 0x1f, 24)
#define SET_TX_DESC_SEC_TYPE(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x3, 22)
#define SET_TX_DESC_SEC_TYPE_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x3, 22)
#define GET_TX_DESC_SEC_TYPE(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, 0x3, 22)
#define SET_TX_DESC_EN_DESC_ID(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x1, 21)
#define SET_TX_DESC_EN_DESC_ID_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x1, 21)
#define GET_TX_DESC_EN_DESC_ID(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, 0x1, 21)
#define SET_TX_DESC_RATE_ID(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x1f, 16)
#define SET_TX_DESC_RATE_ID_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x1f, 16)
#define GET_TX_DESC_RATE_ID(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, 0x1f, 16)
#define SET_TX_DESC_PIFS(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x1, 15)
#define SET_TX_DESC_PIFS_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x1, 15)
#define GET_TX_DESC_PIFS(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, 0x1, 15)
#define SET_TX_DESC_LSIG_TXOP_EN(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x1, 14)
#define SET_TX_DESC_LSIG_TXOP_EN_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x1, 14)
#define GET_TX_DESC_LSIG_TXOP_EN(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, 0x1, 14)
#define SET_TX_DESC_RD_NAV_EXT(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x1, 13)
#define SET_TX_DESC_RD_NAV_EXT_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x1, 13)
#define GET_TX_DESC_RD_NAV_EXT(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, 0x1, 13)
#define SET_TX_DESC_QSEL(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x1f, 8)
#define SET_TX_DESC_QSEL_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x1f, 8)
#define GET_TX_DESC_QSEL(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, 0x1f, 8)
#define SET_TX_DESC_MACID(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x7f, 0)
#define SET_TX_DESC_MACID_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, __Value, 0x7f, 0)
#define GET_TX_DESC_MACID(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword1, 0x7f, 0)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)


/*TXDESC_WORD2*/

#define SET_TX_DESC_HW_AES_IV(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 31)
#define SET_TX_DESC_HW_AES_IV_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 31)
#define GET_TX_DESC_HW_AES_IV(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, 0x1, 31)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_FTM_EN(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 30)
#define SET_TX_DESC_FTM_EN_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 30)
#define GET_TX_DESC_FTM_EN(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, 0x1, 30)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_G_ID(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x3f, 24)
#define SET_TX_DESC_G_ID_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x3f, 24)
#define GET_TX_DESC_G_ID(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, 0x3f, 24)
#define SET_TX_DESC_BT_NULL(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 23)
#define SET_TX_DESC_BT_NULL_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 23)
#define GET_TX_DESC_BT_NULL(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, 0x1, 23)
#define SET_TX_DESC_AMPDU_DENSITY(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x7, 20)
#define SET_TX_DESC_AMPDU_DENSITY_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x7, 20)
#define GET_TX_DESC_AMPDU_DENSITY(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, 0x7, 20)
#define SET_TX_DESC_SPE_RPT(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 19)
#define SET_TX_DESC_SPE_RPT_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 19)
#define GET_TX_DESC_SPE_RPT(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, 0x1, 19)
#define SET_TX_DESC_RAW(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 18)
#define SET_TX_DESC_RAW_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 18)
#define GET_TX_DESC_RAW(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, 0x1, 18)
#define SET_TX_DESC_MOREFRAG(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 17)
#define SET_TX_DESC_MOREFRAG_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 17)
#define GET_TX_DESC_MOREFRAG(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, 0x1, 17)
#define SET_TX_DESC_BK(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 16)
#define SET_TX_DESC_BK_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 16)
#define GET_TX_DESC_BK(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, 0x1, 16)
#define SET_TX_DESC_NULL_1(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 15)
#define SET_TX_DESC_NULL_1_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 15)
#define GET_TX_DESC_NULL_1(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, 0x1, 15)
#define SET_TX_DESC_NULL_0(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 14)
#define SET_TX_DESC_NULL_0_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 14)
#define GET_TX_DESC_NULL_0(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, 0x1, 14)
#define SET_TX_DESC_RDG_EN(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 13)
#define SET_TX_DESC_RDG_EN_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 13)
#define GET_TX_DESC_RDG_EN(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, 0x1, 13)
#define SET_TX_DESC_AGG_EN(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 12)
#define SET_TX_DESC_AGG_EN_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 12)
#define GET_TX_DESC_AGG_EN(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, 0x1, 12)
#define SET_TX_DESC_CCA_RTS(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x3, 10)
#define SET_TX_DESC_CCA_RTS_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x3, 10)
#define GET_TX_DESC_CCA_RTS(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, 0x3, 10)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_TRI_FRAME(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 9)
#define SET_TX_DESC_TRI_FRAME_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1, 9)
#define GET_TX_DESC_TRI_FRAME(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, 0x1, 9)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_P_AID(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1ff, 0)
#define SET_TX_DESC_P_AID_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, __Value, 0x1ff, 0)
#define GET_TX_DESC_P_AID(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword2, 0x1ff, 0)

/*TXDESC_WORD3*/

#define SET_TX_DESC_AMPDU_MAX_TIME(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0xff, 24)
#define SET_TX_DESC_AMPDU_MAX_TIME_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0xff, 24)
#define GET_TX_DESC_AMPDU_MAX_TIME(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, 0xff, 24)
#define SET_TX_DESC_NDPA(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x3, 22)
#define SET_TX_DESC_NDPA_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x3, 22)
#define GET_TX_DESC_NDPA(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, 0x3, 22)
#define SET_TX_DESC_MAX_AGG_NUM(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1f, 17)
#define SET_TX_DESC_MAX_AGG_NUM_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1f, 17)
#define GET_TX_DESC_MAX_AGG_NUM(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, 0x1f, 17)
#define SET_TX_DESC_USE_MAX_TIME_EN(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 16)
#define SET_TX_DESC_USE_MAX_TIME_EN_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 16)
#define GET_TX_DESC_USE_MAX_TIME_EN(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, 0x1, 16)
#define SET_TX_DESC_NAVUSEHDR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 15)
#define SET_TX_DESC_NAVUSEHDR_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 15)
#define GET_TX_DESC_NAVUSEHDR(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, 0x1, 15)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_CHK_EN(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 14)
#define SET_TX_DESC_CHK_EN_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 14)
#define GET_TX_DESC_CHK_EN(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, 0x1, 14)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_HW_RTS_EN(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 13)
#define SET_TX_DESC_HW_RTS_EN_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 13)
#define GET_TX_DESC_HW_RTS_EN(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, 0x1, 13)
#define SET_TX_DESC_RTSEN(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 12)
#define SET_TX_DESC_RTSEN_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 12)
#define GET_TX_DESC_RTSEN(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, 0x1, 12)
#define SET_TX_DESC_CTS2SELF(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 11)
#define SET_TX_DESC_CTS2SELF_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 11)
#define GET_TX_DESC_CTS2SELF(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, 0x1, 11)
#define SET_TX_DESC_DISDATAFB(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 10)
#define SET_TX_DESC_DISDATAFB_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 10)
#define GET_TX_DESC_DISDATAFB(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, 0x1, 10)
#define SET_TX_DESC_DISRTSFB(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 9)
#define SET_TX_DESC_DISRTSFB_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 9)
#define GET_TX_DESC_DISRTSFB(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, 0x1, 9)
#define SET_TX_DESC_USE_RATE(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 8)
#define SET_TX_DESC_USE_RATE_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1, 8)
#define GET_TX_DESC_USE_RATE(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, 0x1, 8)
#define SET_TX_DESC_HW_SSN_SEL(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x3, 6)
#define SET_TX_DESC_HW_SSN_SEL_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x3, 6)
#define GET_TX_DESC_HW_SSN_SEL(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, 0x3, 6)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_WHEADER_LEN(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1f, 0)
#define SET_TX_DESC_WHEADER_LEN_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, __Value, 0x1f, 0)
#define GET_TX_DESC_WHEADER_LEN(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword3, 0x1f, 0)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)


/*TXDESC_WORD4*/

#define SET_TX_DESC_PCTS_MASK_IDX(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0x3, 30)
#define SET_TX_DESC_PCTS_MASK_IDX_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0x3, 30)
#define GET_TX_DESC_PCTS_MASK_IDX(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, 0x3, 30)
#define SET_TX_DESC_PCTS_EN(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0x1, 29)
#define SET_TX_DESC_PCTS_EN_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0x1, 29)
#define GET_TX_DESC_PCTS_EN(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, 0x1, 29)
#define SET_TX_DESC_RTSRATE(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0x1f, 24)
#define SET_TX_DESC_RTSRATE_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0x1f, 24)
#define GET_TX_DESC_RTSRATE(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, 0x1f, 24)
#define SET_TX_DESC_RTS_DATA_RTY_LMT(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0x3f, 18)
#define SET_TX_DESC_RTS_DATA_RTY_LMT_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0x3f, 18)
#define GET_TX_DESC_RTS_DATA_RTY_LMT(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, 0x3f, 18)
#define SET_TX_DESC_RTY_LMT_EN(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0x1, 17)
#define SET_TX_DESC_RTY_LMT_EN_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0x1, 17)
#define GET_TX_DESC_RTY_LMT_EN(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, 0x1, 17)
#define SET_TX_DESC_RTS_RTY_LOWEST_RATE(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0xf, 13)
#define SET_TX_DESC_RTS_RTY_LOWEST_RATE_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0xf, 13)
#define GET_TX_DESC_RTS_RTY_LOWEST_RATE(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, 0xf, 13)
#define SET_TX_DESC_DATA_RTY_LOWEST_RATE(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0x1f, 8)
#define SET_TX_DESC_DATA_RTY_LOWEST_RATE_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0x1f, 8)
#define GET_TX_DESC_DATA_RTY_LOWEST_RATE(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, 0x1f, 8)
#define SET_TX_DESC_TRY_RATE(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0x1, 7)
#define SET_TX_DESC_TRY_RATE_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0x1, 7)
#define GET_TX_DESC_TRY_RATE(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, 0x1, 7)
#define SET_TX_DESC_DATARATE(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0x7f, 0)
#define SET_TX_DESC_DATARATE_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, __Value, 0x7f, 0)
#define GET_TX_DESC_DATARATE(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword4, 0x7f, 0)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)


/*TXDESC_WORD5*/

#define SET_TX_DESC_POLLUTED(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x1, 31)
#define SET_TX_DESC_POLLUTED_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x1, 31)
#define GET_TX_DESC_POLLUTED(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, 0x1, 31)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_TXPWR_OFSET(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x7, 28)
#define SET_TX_DESC_TXPWR_OFSET_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x7, 28)
#define GET_TX_DESC_TXPWR_OFSET(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, 0x7, 28)
#define SET_TX_DESC_TX_ANT(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0xf, 24)
#define SET_TX_DESC_TX_ANT_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0xf, 24)
#define GET_TX_DESC_TX_ANT(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, 0xf, 24)
#define SET_TX_DESC_PORT_ID(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x7, 21)
#define SET_TX_DESC_PORT_ID_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x7, 21)
#define GET_TX_DESC_PORT_ID(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, 0x7, 21)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_MULTIPLE_PORT(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x7, 18)
#define SET_TX_DESC_MULTIPLE_PORT_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x7, 18)
#define GET_TX_DESC_MULTIPLE_PORT(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, 0x7, 18)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_SIGNALING_TAPKT_EN(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x1, 17)
#define SET_TX_DESC_SIGNALING_TAPKT_EN_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x1, 17)
#define GET_TX_DESC_SIGNALING_TAPKT_EN(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, 0x1, 17)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_RTS_SC(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0xf, 13)
#define SET_TX_DESC_RTS_SC_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0xf, 13)
#define GET_TX_DESC_RTS_SC(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, 0xf, 13)
#define SET_TX_DESC_RTS_SHORT(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x1, 12)
#define SET_TX_DESC_RTS_SHORT_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x1, 12)
#define GET_TX_DESC_RTS_SHORT(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, 0x1, 12)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_VCS_STBC(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x3, 10)
#define SET_TX_DESC_VCS_STBC_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x3, 10)
#define GET_TX_DESC_VCS_STBC(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, 0x3, 10)

#endif

#if (HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_RTS_STBC(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x3, 10)
#define SET_TX_DESC_RTS_STBC_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x3, 10)
#define GET_TX_DESC_RTS_STBC(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, 0x3, 10)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_DATA_STBC(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x3, 8)
#define SET_TX_DESC_DATA_STBC_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x3, 8)
#define GET_TX_DESC_DATA_STBC(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, 0x3, 8)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_DATA_LDPC(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x1, 7)
#define SET_TX_DESC_DATA_LDPC_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x1, 7)
#define GET_TX_DESC_DATA_LDPC(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, 0x1, 7)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_DATA_BW(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x3, 5)
#define SET_TX_DESC_DATA_BW_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x3, 5)
#define GET_TX_DESC_DATA_BW(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, 0x3, 5)
#define SET_TX_DESC_DATA_SHORT(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x1, 4)
#define SET_TX_DESC_DATA_SHORT_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0x1, 4)
#define GET_TX_DESC_DATA_SHORT(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, 0x1, 4)
#define SET_TX_DESC_DATA_SC(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0xf, 0)
#define SET_TX_DESC_DATA_SC_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, __Value, 0xf, 0)
#define GET_TX_DESC_DATA_SC(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword5, 0xf, 0)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)


/*TXDESC_WORD6*/

#define SET_TX_DESC_ANTSEL_D(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0x3, 30)
#define SET_TX_DESC_ANTSEL_D_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0x3, 30)
#define GET_TX_DESC_ANTSEL_D(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, 0x3, 30)
#define SET_TX_DESC_ANT_MAPD(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0x3, 28)
#define SET_TX_DESC_ANT_MAPD_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0x3, 28)
#define GET_TX_DESC_ANT_MAPD(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, 0x3, 28)
#define SET_TX_DESC_ANT_MAPC(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0x3, 26)
#define SET_TX_DESC_ANT_MAPC_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0x3, 26)
#define GET_TX_DESC_ANT_MAPC(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, 0x3, 26)
#define SET_TX_DESC_ANT_MAPB(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0x3, 24)
#define SET_TX_DESC_ANT_MAPB_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0x3, 24)
#define GET_TX_DESC_ANT_MAPB(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, 0x3, 24)
#define SET_TX_DESC_ANT_MAPA(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0x3, 22)
#define SET_TX_DESC_ANT_MAPA_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0x3, 22)
#define GET_TX_DESC_ANT_MAPA(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, 0x3, 22)
#define SET_TX_DESC_ANTSEL_C(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0x3, 20)
#define SET_TX_DESC_ANTSEL_C_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0x3, 20)
#define GET_TX_DESC_ANTSEL_C(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, 0x3, 20)
#define SET_TX_DESC_ANTSEL_B(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0x3, 18)
#define SET_TX_DESC_ANTSEL_B_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0x3, 18)
#define GET_TX_DESC_ANTSEL_B(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, 0x3, 18)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_ANTSEL_A(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0x3, 16)
#define SET_TX_DESC_ANTSEL_A_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0x3, 16)
#define GET_TX_DESC_ANTSEL_A(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, 0x3, 16)
#define SET_TX_DESC_MBSSID(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0xf, 12)
#define SET_TX_DESC_MBSSID_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0xf, 12)
#define GET_TX_DESC_MBSSID(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, 0xf, 12)
#define SET_TX_DESC_SW_DEFINE(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0xfff, 0)
#define SET_TX_DESC_SW_DEFINE_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, __Value, 0xfff, 0)
#define GET_TX_DESC_SW_DEFINE(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword6, 0xfff, 0)

/*TXDESC_WORD7*/

#define SET_TX_DESC_DMA_TXAGG_NUM(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, __Value, 0xff, 24)
#define SET_TX_DESC_DMA_TXAGG_NUM_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, __Value, 0xff, 24)
#define GET_TX_DESC_DMA_TXAGG_NUM(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, 0xff, 24)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_FINAL_DATA_RATE(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, __Value, 0xff, 24)
#define SET_TX_DESC_FINAL_DATA_RATE_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, __Value, 0xff, 24)
#define GET_TX_DESC_FINAL_DATA_RATE(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, 0xff, 24)
#define SET_TX_DESC_NTX_MAP(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, __Value, 0xf, 20)
#define SET_TX_DESC_NTX_MAP_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, __Value, 0xf, 20)
#define GET_TX_DESC_NTX_MAP(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, 0xf, 20)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_TX_BUFF_SIZE(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, __Value, 0xffff, 0)
#define SET_TX_DESC_TX_BUFF_SIZE_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, __Value, 0xffff, 0)
#define GET_TX_DESC_TX_BUFF_SIZE(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, 0xffff, 0)
#define SET_TX_DESC_TXDESC_CHECKSUM(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, __Value, 0xffff, 0)
#define SET_TX_DESC_TXDESC_CHECKSUM_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, __Value, 0xffff, 0)
#define GET_TX_DESC_TXDESC_CHECKSUM(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, 0xffff, 0)
#define SET_TX_DESC_TIMESTAMP(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, __Value, 0xffff, 0)
#define SET_TX_DESC_TIMESTAMP_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, __Value, 0xffff, 0)
#define GET_TX_DESC_TIMESTAMP(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword7, 0xffff, 0)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)


/*TXDESC_WORD8*/

#define SET_TX_DESC_TXWIFI_CP(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 31)
#define SET_TX_DESC_TXWIFI_CP_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 31)
#define GET_TX_DESC_TXWIFI_CP(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, 0x1, 31)
#define SET_TX_DESC_MAC_CP(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 30)
#define SET_TX_DESC_MAC_CP_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 30)
#define GET_TX_DESC_MAC_CP(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, 0x1, 30)
#define SET_TX_DESC_STW_PKTRE_DIS(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 29)
#define SET_TX_DESC_STW_PKTRE_DIS_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 29)
#define GET_TX_DESC_STW_PKTRE_DIS(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, 0x1, 29)
#define SET_TX_DESC_STW_RB_DIS(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 28)
#define SET_TX_DESC_STW_RB_DIS_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 28)
#define GET_TX_DESC_STW_RB_DIS(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, 0x1, 28)
#define SET_TX_DESC_STW_RATE_DIS(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 27)
#define SET_TX_DESC_STW_RATE_DIS_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 27)
#define GET_TX_DESC_STW_RATE_DIS(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, 0x1, 27)
#define SET_TX_DESC_STW_ANT_DIS(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 26)
#define SET_TX_DESC_STW_ANT_DIS_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 26)
#define GET_TX_DESC_STW_ANT_DIS(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, 0x1, 26)
#define SET_TX_DESC_STW_EN(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 25)
#define SET_TX_DESC_STW_EN_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 25)
#define GET_TX_DESC_STW_EN(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, 0x1, 25)
#define SET_TX_DESC_SMH_EN(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 24)
#define SET_TX_DESC_SMH_EN_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 24)
#define GET_TX_DESC_SMH_EN(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, 0x1, 24)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_TAILPAGE_L(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0xff, 24)
#define SET_TX_DESC_TAILPAGE_L_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0xff, 24)
#define GET_TX_DESC_TAILPAGE_L(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, 0xff, 24)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_SDIO_DMASEQ(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0xff, 16)
#define SET_TX_DESC_SDIO_DMASEQ_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0xff, 16)
#define GET_TX_DESC_SDIO_DMASEQ(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, 0xff, 16)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_NEXTHEADPAGE_L(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0xff, 16)
#define SET_TX_DESC_NEXTHEADPAGE_L_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0xff, 16)
#define GET_TX_DESC_NEXTHEADPAGE_L(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, 0xff, 16)
#define SET_TX_DESC_EN_HWSEQ(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 15)
#define SET_TX_DESC_EN_HWSEQ_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 15)
#define GET_TX_DESC_EN_HWSEQ(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, 0x1, 15)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_EN_HWEXSEQ(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 14)
#define SET_TX_DESC_EN_HWEXSEQ_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x1, 14)
#define GET_TX_DESC_EN_HWEXSEQ(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, 0x1, 14)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_DATA_RC(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x3f, 8)
#define SET_TX_DESC_DATA_RC_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x3f, 8)
#define GET_TX_DESC_DATA_RC(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, 0x3f, 8)
#define SET_TX_DESC_BAR_RTY_TH(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x3, 6)
#define SET_TX_DESC_BAR_RTY_TH_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x3, 6)
#define GET_TX_DESC_BAR_RTY_TH(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, 0x3, 6)
#define SET_TX_DESC_RTS_RC(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x3f, 0)
#define SET_TX_DESC_RTS_RC_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, __Value, 0x3f, 0)
#define GET_TX_DESC_RTS_RC(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword8, 0x3f, 0)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)


/*TXDESC_WORD9*/

#define SET_TX_DESC_TAILPAGE_H(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, __Value, 0xf, 28)
#define SET_TX_DESC_TAILPAGE_H_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, __Value, 0xf, 28)
#define GET_TX_DESC_TAILPAGE_H(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, 0xf, 28)
#define SET_TX_DESC_NEXTHEADPAGE_H(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, __Value, 0xf, 24)
#define SET_TX_DESC_NEXTHEADPAGE_H_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, __Value, 0xf, 24)
#define GET_TX_DESC_NEXTHEADPAGE_H(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, 0xf, 24)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_SW_SEQ(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, __Value, 0xfff, 12)
#define SET_TX_DESC_SW_SEQ_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, __Value, 0xfff, 12)
#define GET_TX_DESC_SW_SEQ(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, 0xfff, 12)
#define SET_TX_DESC_TXBF_PATH(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, __Value, 0x1, 11)
#define SET_TX_DESC_TXBF_PATH_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, __Value, 0x1, 11)
#define GET_TX_DESC_TXBF_PATH(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, 0x1, 11)
#define SET_TX_DESC_PADDING_LEN(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, __Value, 0x7ff, 0)
#define SET_TX_DESC_PADDING_LEN_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, __Value, 0x7ff, 0)
#define GET_TX_DESC_PADDING_LEN(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, 0x7ff, 0)
#define SET_TX_DESC_GROUP_BIT_IE_OFFSET(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, __Value, 0xff, 0)
#define SET_TX_DESC_GROUP_BIT_IE_OFFSET_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, __Value, 0xff, 0)
#define GET_TX_DESC_GROUP_BIT_IE_OFFSET(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword9, 0xff, 0)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)


/*WORD10*/

#define SET_TX_DESC_MU_DATARATE(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword10, __Value, 0xff, 8)
#define SET_TX_DESC_MU_DATARATE_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword10, __Value, 0xff, 8)
#define GET_TX_DESC_MU_DATARATE(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword10, 0xff, 8)
#define SET_TX_DESC_MU_RC(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword10, __Value, 0xf, 4)
#define SET_TX_DESC_MU_RC_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword10, __Value, 0xf, 4)
#define GET_TX_DESC_MU_RC(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword10, 0xf, 4)
#define SET_TX_DESC_SND_PKT_SEL(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword10, __Value, 0x3, 0)
#define SET_TX_DESC_SND_PKT_SEL_NO_CLR(__pTxDesc, __Value)    HALMAC_SET_DESC_FIELD_NO_CLR(((PHALMAC_TX_DESC)__pTxDesc)->Dword10, __Value, 0x3, 0)
#define GET_TX_DESC_SND_PKT_SEL(__pTxDesc)    HALMAC_GET_DESC_FIELD(((PHALMAC_TX_DESC)__pTxDesc)->Dword10, 0x3, 0)

#endif


#endif
