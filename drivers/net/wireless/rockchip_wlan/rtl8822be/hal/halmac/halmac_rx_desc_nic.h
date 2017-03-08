#ifndef _HALMAC_RX_DESC_NIC_H_
#define _HALMAC_RX_DESC_NIC_H_
#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

/*RXDESC_WORD0*/

#define GET_RX_DESC_EOR(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x00, 30, 1)
#define GET_RX_DESC_PHYPKTIDC(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x00, 28, 1)
#define GET_RX_DESC_SWDEC(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x00, 27, 1)
#define GET_RX_DESC_PHYST(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x00, 26, 1)
#define GET_RX_DESC_SHIFT(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x00, 24, 2)
#define GET_RX_DESC_QOS(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x00, 23, 1)
#define GET_RX_DESC_SECURITY(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x00, 20, 3)
#define GET_RX_DESC_DRV_INFO_SIZE(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x00, 16, 4)
#define GET_RX_DESC_ICV_ERR(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x00, 15, 1)
#define GET_RX_DESC_CRC32(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x00, 14, 1)
#define GET_RX_DESC_PKT_LEN(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x00, 0, 14)

/*RXDESC_WORD1*/

#define GET_RX_DESC_BC(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 31, 1)
#define GET_RX_DESC_MC(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 30, 1)
#define GET_RX_DESC_TY_PE(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 28, 2)
#define GET_RX_DESC_MF(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 27, 1)
#define GET_RX_DESC_MD(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 26, 1)
#define GET_RX_DESC_PWR(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 25, 1)
#define GET_RX_DESC_PAM(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 24, 1)
#define GET_RX_DESC_CHK_VLD(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 23, 1)
#define GET_RX_DESC_RX_IS_TCP_UDP(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 22, 1)
#define GET_RX_DESC_RX_IPV(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 21, 1)
#define GET_RX_DESC_CHKERR(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 20, 1)
#define GET_RX_DESC_PAGGR(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 15, 1)
#define GET_RX_DESC_RXID_MATCH(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 14, 1)
#define GET_RX_DESC_AMSDU(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 13, 1)
#define GET_RX_DESC_MACID_VLD(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 12, 1)
#define GET_RX_DESC_TID(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 8, 4)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define GET_RX_DESC_EXT_SECTYPE(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 7, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define GET_RX_DESC_MACID(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x04, 0, 7)

/*RXDESC_WORD2*/

#define GET_RX_DESC_FCS_OK(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x08, 31, 1)
#define GET_RX_DESC_C2H(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x08, 28, 1)
#define GET_RX_DESC_HWRSVD(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x08, 24, 4)
#define GET_RX_DESC_WLANHD_IV_LEN(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x08, 18, 6)
#define GET_RX_DESC_RX_IS_QOS(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x08, 16, 1)
#define GET_RX_DESC_FRAG(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x08, 12, 4)
#define GET_RX_DESC_SEQ(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x08, 0, 12)

/*RXDESC_WORD3*/

#define GET_RX_DESC_MAGIC_WAKE(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x0C, 31, 1)
#define GET_RX_DESC_UNICAST_WAKE(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x0C, 30, 1)
#define GET_RX_DESC_PATTERN_MATCH(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x0C, 29, 1)
#define GET_RX_DESC_DMA_AGG_NUM(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x0C, 16, 8)
#define GET_RX_DESC_BSSID_FIT_1_0(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x0C, 12, 2)
#define GET_RX_DESC_EOSP(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x0C, 11, 1)
#define GET_RX_DESC_HTC(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x0C, 10, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define GET_RX_DESC_BSSID_FIT_4_2(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x0C, 7, 3)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define GET_RX_DESC_RX_RATE(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x0C, 0, 7)

/*RXDESC_WORD4*/

#define GET_RX_DESC_A1_FIT(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x10, 24, 5)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT)

#define GET_RX_DESC_MACID_RPT_BUFF(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x10, 17, 7)
#define GET_RX_DESC_RX_PRE_NDP_VLD(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x10, 16, 1)
#define GET_RX_DESC_RX_SCRAMBLER(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x10, 9, 7)
#define GET_RX_DESC_RX_EOF(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x10, 8, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

#define GET_RX_DESC_PATTERN_IDX(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x10, 0, 8)

/*RXDESC_WORD5*/

#define GET_RX_DESC_TSFL(__pRxDesc)    LE_BITS_TO_4BYTE(__pRxDesc + 0x14, 0, 32)

#endif


#endif
