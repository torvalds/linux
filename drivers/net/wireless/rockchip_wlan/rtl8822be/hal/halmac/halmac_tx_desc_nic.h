#ifndef _HALMAC_TX_DESC_NIC_H_
#define _HALMAC_TX_DESC_NIC_H_
#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

/*TXDESC_WORD0*/

#define SET_TX_DESC_DISQSELSEQ(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x00, 31, 1, __Value)
#define GET_TX_DESC_DISQSELSEQ(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x00, 31, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_GF(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x00, 30, 1, __Value)
#define GET_TX_DESC_GF(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x00, 30, 1)
#define SET_TX_DESC_NO_ACM(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x00, 29, 1, __Value)
#define GET_TX_DESC_NO_ACM(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x00, 29, 1)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_BCNPKT_TSF_CTRL(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x00, 28, 1, __Value)
#define GET_TX_DESC_BCNPKT_TSF_CTRL(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x00, 28, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_AMSDU_PAD_EN(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x00, 27, 1, __Value)
#define GET_TX_DESC_AMSDU_PAD_EN(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x00, 27, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_LS(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x00, 26, 1, __Value)
#define GET_TX_DESC_LS(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x00, 26, 1)
#define SET_TX_DESC_HTC(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x00, 25, 1, __Value)
#define GET_TX_DESC_HTC(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x00, 25, 1)
#define SET_TX_DESC_BMC(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x00, 24, 1, __Value)
#define GET_TX_DESC_BMC(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x00, 24, 1)
#define SET_TX_DESC_OFFSET(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x00, 16, 8, __Value)
#define GET_TX_DESC_OFFSET(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x00, 16, 8)
#define SET_TX_DESC_TXPKTSIZE(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x00, 0, 16, __Value)
#define GET_TX_DESC_TXPKTSIZE(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x00, 0, 16)

/*TXDESC_WORD1*/

#define SET_TX_DESC_MOREDATA(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x04, 29, 1, __Value)
#define GET_TX_DESC_MOREDATA(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x04, 29, 1)
#define SET_TX_DESC_PKT_OFFSET(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x04, 24, 5, __Value)
#define GET_TX_DESC_PKT_OFFSET(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x04, 24, 5)
#define SET_TX_DESC_SEC_TYPE(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x04, 22, 2, __Value)
#define GET_TX_DESC_SEC_TYPE(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x04, 22, 2)
#define SET_TX_DESC_EN_DESC_ID(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x04, 21, 1, __Value)
#define GET_TX_DESC_EN_DESC_ID(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x04, 21, 1)
#define SET_TX_DESC_RATE_ID(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x04, 16, 5, __Value)
#define GET_TX_DESC_RATE_ID(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x04, 16, 5)
#define SET_TX_DESC_PIFS(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x04, 15, 1, __Value)
#define GET_TX_DESC_PIFS(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x04, 15, 1)
#define SET_TX_DESC_LSIG_TXOP_EN(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x04, 14, 1, __Value)
#define GET_TX_DESC_LSIG_TXOP_EN(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x04, 14, 1)
#define SET_TX_DESC_RD_NAV_EXT(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x04, 13, 1, __Value)
#define GET_TX_DESC_RD_NAV_EXT(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x04, 13, 1)
#define SET_TX_DESC_QSEL(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x04, 8, 5, __Value)
#define GET_TX_DESC_QSEL(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x04, 8, 5)
#define SET_TX_DESC_MACID(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x04, 0, 7, __Value)
#define GET_TX_DESC_MACID(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x04, 0, 7)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)


/*TXDESC_WORD2*/

#define SET_TX_DESC_HW_AES_IV(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x08, 31, 1, __Value)
#define GET_TX_DESC_HW_AES_IV(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x08, 31, 1)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_FTM_EN(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x08, 30, 1, __Value)
#define GET_TX_DESC_FTM_EN(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x08, 30, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_G_ID(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x08, 24, 6, __Value)
#define GET_TX_DESC_G_ID(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x08, 24, 6)
#define SET_TX_DESC_BT_NULL(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x08, 23, 1, __Value)
#define GET_TX_DESC_BT_NULL(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x08, 23, 1)
#define SET_TX_DESC_AMPDU_DENSITY(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x08, 20, 3, __Value)
#define GET_TX_DESC_AMPDU_DENSITY(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x08, 20, 3)
#define SET_TX_DESC_SPE_RPT(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x08, 19, 1, __Value)
#define GET_TX_DESC_SPE_RPT(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x08, 19, 1)
#define SET_TX_DESC_RAW(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x08, 18, 1, __Value)
#define GET_TX_DESC_RAW(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x08, 18, 1)
#define SET_TX_DESC_MOREFRAG(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x08, 17, 1, __Value)
#define GET_TX_DESC_MOREFRAG(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x08, 17, 1)
#define SET_TX_DESC_BK(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x08, 16, 1, __Value)
#define GET_TX_DESC_BK(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x08, 16, 1)
#define SET_TX_DESC_NULL_1(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x08, 15, 1, __Value)
#define GET_TX_DESC_NULL_1(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x08, 15, 1)
#define SET_TX_DESC_NULL_0(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x08, 14, 1, __Value)
#define GET_TX_DESC_NULL_0(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x08, 14, 1)
#define SET_TX_DESC_RDG_EN(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x08, 13, 1, __Value)
#define GET_TX_DESC_RDG_EN(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x08, 13, 1)
#define SET_TX_DESC_AGG_EN(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x08, 12, 1, __Value)
#define GET_TX_DESC_AGG_EN(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x08, 12, 1)
#define SET_TX_DESC_CCA_RTS(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x08, 10, 2, __Value)
#define GET_TX_DESC_CCA_RTS(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x08, 10, 2)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_TRI_FRAME(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x08, 9, 1, __Value)
#define GET_TX_DESC_TRI_FRAME(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x08, 9, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_P_AID(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x08, 0, 9, __Value)
#define GET_TX_DESC_P_AID(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x08, 0, 9)

/*TXDESC_WORD3*/

#define SET_TX_DESC_AMPDU_MAX_TIME(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x0C, 24, 8, __Value)
#define GET_TX_DESC_AMPDU_MAX_TIME(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x0C, 24, 8)
#define SET_TX_DESC_NDPA(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x0C, 22, 2, __Value)
#define GET_TX_DESC_NDPA(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x0C, 22, 2)
#define SET_TX_DESC_MAX_AGG_NUM(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x0C, 17, 5, __Value)
#define GET_TX_DESC_MAX_AGG_NUM(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x0C, 17, 5)
#define SET_TX_DESC_USE_MAX_TIME_EN(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x0C, 16, 1, __Value)
#define GET_TX_DESC_USE_MAX_TIME_EN(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x0C, 16, 1)
#define SET_TX_DESC_NAVUSEHDR(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x0C, 15, 1, __Value)
#define GET_TX_DESC_NAVUSEHDR(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x0C, 15, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_CHK_EN(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x0C, 14, 1, __Value)
#define GET_TX_DESC_CHK_EN(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x0C, 14, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_HW_RTS_EN(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x0C, 13, 1, __Value)
#define GET_TX_DESC_HW_RTS_EN(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x0C, 13, 1)
#define SET_TX_DESC_RTSEN(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x0C, 12, 1, __Value)
#define GET_TX_DESC_RTSEN(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x0C, 12, 1)
#define SET_TX_DESC_CTS2SELF(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x0C, 11, 1, __Value)
#define GET_TX_DESC_CTS2SELF(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x0C, 11, 1)
#define SET_TX_DESC_DISDATAFB(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x0C, 10, 1, __Value)
#define GET_TX_DESC_DISDATAFB(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x0C, 10, 1)
#define SET_TX_DESC_DISRTSFB(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x0C, 9, 1, __Value)
#define GET_TX_DESC_DISRTSFB(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x0C, 9, 1)
#define SET_TX_DESC_USE_RATE(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x0C, 8, 1, __Value)
#define GET_TX_DESC_USE_RATE(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x0C, 8, 1)
#define SET_TX_DESC_HW_SSN_SEL(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x0C, 6, 2, __Value)
#define GET_TX_DESC_HW_SSN_SEL(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x0C, 6, 2)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_WHEADER_LEN(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x0C, 0, 5, __Value)
#define GET_TX_DESC_WHEADER_LEN(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x0C, 0, 5)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)


/*TXDESC_WORD4*/

#define SET_TX_DESC_PCTS_MASK_IDX(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x10, 30, 2, __Value)
#define GET_TX_DESC_PCTS_MASK_IDX(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x10, 30, 2)
#define SET_TX_DESC_PCTS_EN(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x10, 29, 1, __Value)
#define GET_TX_DESC_PCTS_EN(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x10, 29, 1)
#define SET_TX_DESC_RTSRATE(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x10, 24, 5, __Value)
#define GET_TX_DESC_RTSRATE(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x10, 24, 5)
#define SET_TX_DESC_RTS_DATA_RTY_LMT(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x10, 18, 6, __Value)
#define GET_TX_DESC_RTS_DATA_RTY_LMT(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x10, 18, 6)
#define SET_TX_DESC_RTY_LMT_EN(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x10, 17, 1, __Value)
#define GET_TX_DESC_RTY_LMT_EN(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x10, 17, 1)
#define SET_TX_DESC_RTS_RTY_LOWEST_RATE(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x10, 13, 4, __Value)
#define GET_TX_DESC_RTS_RTY_LOWEST_RATE(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x10, 13, 4)
#define SET_TX_DESC_DATA_RTY_LOWEST_RATE(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x10, 8, 5, __Value)
#define GET_TX_DESC_DATA_RTY_LOWEST_RATE(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x10, 8, 5)
#define SET_TX_DESC_TRY_RATE(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x10, 7, 1, __Value)
#define GET_TX_DESC_TRY_RATE(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x10, 7, 1)
#define SET_TX_DESC_DATARATE(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x10, 0, 7, __Value)
#define GET_TX_DESC_DATARATE(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x10, 0, 7)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)


/*TXDESC_WORD5*/

#define SET_TX_DESC_POLLUTED(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x14, 31, 1, __Value)
#define GET_TX_DESC_POLLUTED(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x14, 31, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_TXPWR_OFSET(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x14, 28, 3, __Value)
#define GET_TX_DESC_TXPWR_OFSET(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x14, 28, 3)
#define SET_TX_DESC_TX_ANT(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x14, 24, 4, __Value)
#define GET_TX_DESC_TX_ANT(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x14, 24, 4)
#define SET_TX_DESC_PORT_ID(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x14, 21, 3, __Value)
#define GET_TX_DESC_PORT_ID(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x14, 21, 3)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_MULTIPLE_PORT(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x14, 18, 3, __Value)
#define GET_TX_DESC_MULTIPLE_PORT(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x14, 18, 3)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_SIGNALING_TAPKT_EN(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x14, 17, 1, __Value)
#define GET_TX_DESC_SIGNALING_TAPKT_EN(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x14, 17, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_RTS_SC(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x14, 13, 4, __Value)
#define GET_TX_DESC_RTS_SC(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x14, 13, 4)
#define SET_TX_DESC_RTS_SHORT(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x14, 12, 1, __Value)
#define GET_TX_DESC_RTS_SHORT(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x14, 12, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_VCS_STBC(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x14, 10, 2, __Value)
#define GET_TX_DESC_VCS_STBC(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x14, 10, 2)

#endif

#if (HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_RTS_STBC(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x14, 10, 2, __Value)
#define GET_TX_DESC_RTS_STBC(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x14, 10, 2)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_DATA_STBC(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x14, 8, 2, __Value)
#define GET_TX_DESC_DATA_STBC(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x14, 8, 2)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_DATA_LDPC(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x14, 7, 1, __Value)
#define GET_TX_DESC_DATA_LDPC(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x14, 7, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_DATA_BW(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x14, 5, 2, __Value)
#define GET_TX_DESC_DATA_BW(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x14, 5, 2)
#define SET_TX_DESC_DATA_SHORT(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x14, 4, 1, __Value)
#define GET_TX_DESC_DATA_SHORT(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x14, 4, 1)
#define SET_TX_DESC_DATA_SC(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x14, 0, 4, __Value)
#define GET_TX_DESC_DATA_SC(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x14, 0, 4)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)


/*TXDESC_WORD6*/

#define SET_TX_DESC_ANTSEL_D(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x18, 30, 2, __Value)
#define GET_TX_DESC_ANTSEL_D(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x18, 30, 2)
#define SET_TX_DESC_ANT_MAPD(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x18, 28, 2, __Value)
#define GET_TX_DESC_ANT_MAPD(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x18, 28, 2)
#define SET_TX_DESC_ANT_MAPC(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x18, 26, 2, __Value)
#define GET_TX_DESC_ANT_MAPC(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x18, 26, 2)
#define SET_TX_DESC_ANT_MAPB(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x18, 24, 2, __Value)
#define GET_TX_DESC_ANT_MAPB(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x18, 24, 2)
#define SET_TX_DESC_ANT_MAPA(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x18, 22, 2, __Value)
#define GET_TX_DESC_ANT_MAPA(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x18, 22, 2)
#define SET_TX_DESC_ANTSEL_C(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x18, 20, 2, __Value)
#define GET_TX_DESC_ANTSEL_C(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x18, 20, 2)
#define SET_TX_DESC_ANTSEL_B(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x18, 18, 2, __Value)
#define GET_TX_DESC_ANTSEL_B(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x18, 18, 2)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_ANTSEL_A(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x18, 16, 2, __Value)
#define GET_TX_DESC_ANTSEL_A(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x18, 16, 2)
#define SET_TX_DESC_MBSSID(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x18, 12, 4, __Value)
#define GET_TX_DESC_MBSSID(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x18, 12, 4)
#define SET_TX_DESC_SW_DEFINE(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x18, 0, 12, __Value)
#define GET_TX_DESC_SW_DEFINE(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x18, 0, 12)

/*TXDESC_WORD7*/

#define SET_TX_DESC_DMA_TXAGG_NUM(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x1C, 24, 8, __Value)
#define GET_TX_DESC_DMA_TXAGG_NUM(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x1C, 24, 8)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_FINAL_DATA_RATE(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x1C, 24, 8, __Value)
#define GET_TX_DESC_FINAL_DATA_RATE(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x1C, 24, 8)
#define SET_TX_DESC_NTX_MAP(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x1C, 20, 4, __Value)
#define GET_TX_DESC_NTX_MAP(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x1C, 20, 4)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_TX_BUFF_SIZE(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x1C, 0, 16, __Value)
#define GET_TX_DESC_TX_BUFF_SIZE(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x1C, 0, 16)
#define SET_TX_DESC_TXDESC_CHECKSUM(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x1C, 0, 16, __Value)
#define GET_TX_DESC_TXDESC_CHECKSUM(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x1C, 0, 16)
#define SET_TX_DESC_TIMESTAMP(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x1C, 0, 16, __Value)
#define GET_TX_DESC_TIMESTAMP(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x1C, 0, 16)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)


/*TXDESC_WORD8*/

#define SET_TX_DESC_TXWIFI_CP(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x20, 31, 1, __Value)
#define GET_TX_DESC_TXWIFI_CP(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x20, 31, 1)
#define SET_TX_DESC_MAC_CP(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x20, 30, 1, __Value)
#define GET_TX_DESC_MAC_CP(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x20, 30, 1)
#define SET_TX_DESC_STW_PKTRE_DIS(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x20, 29, 1, __Value)
#define GET_TX_DESC_STW_PKTRE_DIS(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x20, 29, 1)
#define SET_TX_DESC_STW_RB_DIS(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x20, 28, 1, __Value)
#define GET_TX_DESC_STW_RB_DIS(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x20, 28, 1)
#define SET_TX_DESC_STW_RATE_DIS(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x20, 27, 1, __Value)
#define GET_TX_DESC_STW_RATE_DIS(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x20, 27, 1)
#define SET_TX_DESC_STW_ANT_DIS(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x20, 26, 1, __Value)
#define GET_TX_DESC_STW_ANT_DIS(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x20, 26, 1)
#define SET_TX_DESC_STW_EN(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x20, 25, 1, __Value)
#define GET_TX_DESC_STW_EN(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x20, 25, 1)
#define SET_TX_DESC_SMH_EN(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x20, 24, 1, __Value)
#define GET_TX_DESC_SMH_EN(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x20, 24, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_TAILPAGE_L(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x20, 24, 8, __Value)
#define GET_TX_DESC_TAILPAGE_L(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x20, 24, 8)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_SDIO_DMASEQ(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x20, 16, 8, __Value)
#define GET_TX_DESC_SDIO_DMASEQ(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x20, 16, 8)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_NEXTHEADPAGE_L(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x20, 16, 8, __Value)
#define GET_TX_DESC_NEXTHEADPAGE_L(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x20, 16, 8)
#define SET_TX_DESC_EN_HWSEQ(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x20, 15, 1, __Value)
#define GET_TX_DESC_EN_HWSEQ(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x20, 15, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define SET_TX_DESC_EN_HWEXSEQ(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x20, 14, 1, __Value)
#define GET_TX_DESC_EN_HWEXSEQ(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x20, 14, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_DATA_RC(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x20, 8, 6, __Value)
#define GET_TX_DESC_DATA_RC(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x20, 8, 6)
#define SET_TX_DESC_BAR_RTY_TH(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x20, 6, 2, __Value)
#define GET_TX_DESC_BAR_RTY_TH(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x20, 6, 2)
#define SET_TX_DESC_RTS_RC(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x20, 0, 6, __Value)
#define GET_TX_DESC_RTS_RC(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x20, 0, 6)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)


/*TXDESC_WORD9*/

#define SET_TX_DESC_TAILPAGE_H(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x24, 28, 4, __Value)
#define GET_TX_DESC_TAILPAGE_H(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x24, 28, 4)
#define SET_TX_DESC_NEXTHEADPAGE_H(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x24, 24, 4, __Value)
#define GET_TX_DESC_NEXTHEADPAGE_H(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x24, 24, 4)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define SET_TX_DESC_SW_SEQ(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x24, 12, 12, __Value)
#define GET_TX_DESC_SW_SEQ(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x24, 12, 12)
#define SET_TX_DESC_TXBF_PATH(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x24, 11, 1, __Value)
#define GET_TX_DESC_TXBF_PATH(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x24, 11, 1)
#define SET_TX_DESC_PADDING_LEN(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x24, 0, 11, __Value)
#define GET_TX_DESC_PADDING_LEN(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x24, 0, 11)
#define SET_TX_DESC_GROUP_BIT_IE_OFFSET(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x24, 0, 8, __Value)
#define GET_TX_DESC_GROUP_BIT_IE_OFFSET(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x24, 0, 8)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)


/*WORD10*/

#define SET_TX_DESC_MU_DATARATE(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x28, 8, 8, __Value)
#define GET_TX_DESC_MU_DATARATE(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x28, 8, 8)
#define SET_TX_DESC_MU_RC(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x28, 4, 4, __Value)
#define GET_TX_DESC_MU_RC(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x28, 4, 4)
#define SET_TX_DESC_SND_PKT_SEL(__pTxDesc, __Value)    SET_BITS_TO_LE_4BYTE(__pTxDesc + 0x28, 0, 2, __Value)
#define GET_TX_DESC_SND_PKT_SEL(__pTxDesc)    LE_BITS_TO_4BYTE(__pTxDesc + 0x28, 0, 2)

#endif


#endif
