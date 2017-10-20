/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _HALMAC_RX_DESC_NIC_H_
#define _HALMAC_RX_DESC_NIC_H_

/*RXDESC_WORD0*/

#define GET_RX_DESC_EOR(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x00, 30, 1)
#define GET_RX_DESC_PHYPKTIDC(__rx_desc)                                       \
	LE_BITS_TO_4BYTE(__rx_desc + 0x00, 28, 1)
#define GET_RX_DESC_SWDEC(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x00, 27, 1)
#define GET_RX_DESC_PHYST(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x00, 26, 1)
#define GET_RX_DESC_SHIFT(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x00, 24, 2)
#define GET_RX_DESC_QOS(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x00, 23, 1)
#define GET_RX_DESC_SECURITY(__rx_desc)                                        \
	LE_BITS_TO_4BYTE(__rx_desc + 0x00, 20, 3)
#define GET_RX_DESC_DRV_INFO_SIZE(__rx_desc)                                   \
	LE_BITS_TO_4BYTE(__rx_desc + 0x00, 16, 4)
#define GET_RX_DESC_ICV_ERR(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x00, 15, 1)
#define GET_RX_DESC_CRC32(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x00, 14, 1)
#define GET_RX_DESC_PKT_LEN(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x00, 0, 14)

/*RXDESC_WORD1*/

#define GET_RX_DESC_BC(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x04, 31, 1)
#define GET_RX_DESC_MC(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x04, 30, 1)
#define GET_RX_DESC_TY_PE(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x04, 28, 2)
#define GET_RX_DESC_MF(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x04, 27, 1)
#define GET_RX_DESC_MD(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x04, 26, 1)
#define GET_RX_DESC_PWR(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x04, 25, 1)
#define GET_RX_DESC_PAM(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x04, 24, 1)
#define GET_RX_DESC_CHK_VLD(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x04, 23, 1)
#define GET_RX_DESC_RX_IS_TCP_UDP(__rx_desc)                                   \
	LE_BITS_TO_4BYTE(__rx_desc + 0x04, 22, 1)
#define GET_RX_DESC_RX_IPV(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x04, 21, 1)
#define GET_RX_DESC_CHKERR(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x04, 20, 1)
#define GET_RX_DESC_PAGGR(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x04, 15, 1)
#define GET_RX_DESC_RXID_MATCH(__rx_desc)                                      \
	LE_BITS_TO_4BYTE(__rx_desc + 0x04, 14, 1)
#define GET_RX_DESC_AMSDU(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x04, 13, 1)
#define GET_RX_DESC_MACID_VLD(__rx_desc)                                       \
	LE_BITS_TO_4BYTE(__rx_desc + 0x04, 12, 1)
#define GET_RX_DESC_TID(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x04, 8, 4)

#define GET_RX_DESC_EXT_SECTYPE(__rx_desc)                                     \
	LE_BITS_TO_4BYTE(__rx_desc + 0x04, 7, 1)

#define GET_RX_DESC_MACID(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x04, 0, 7)

/*RXDESC_WORD2*/

#define GET_RX_DESC_FCS_OK(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x08, 31, 1)

#define GET_RX_DESC_PPDU_CNT(__rx_desc)                                        \
	LE_BITS_TO_4BYTE(__rx_desc + 0x08, 29, 2)

#define GET_RX_DESC_C2H(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x08, 28, 1)
#define GET_RX_DESC_HWRSVD(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x08, 24, 4)
#define GET_RX_DESC_WLANHD_IV_LEN(__rx_desc)                                   \
	LE_BITS_TO_4BYTE(__rx_desc + 0x08, 18, 6)
#define GET_RX_DESC_RX_IS_QOS(__rx_desc)                                       \
	LE_BITS_TO_4BYTE(__rx_desc + 0x08, 16, 1)
#define GET_RX_DESC_FRAG(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x08, 12, 4)
#define GET_RX_DESC_SEQ(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x08, 0, 12)

/*RXDESC_WORD3*/

#define GET_RX_DESC_MAGIC_WAKE(__rx_desc)                                      \
	LE_BITS_TO_4BYTE(__rx_desc + 0x0C, 31, 1)
#define GET_RX_DESC_UNICAST_WAKE(__rx_desc)                                    \
	LE_BITS_TO_4BYTE(__rx_desc + 0x0C, 30, 1)
#define GET_RX_DESC_PATTERN_MATCH(__rx_desc)                                   \
	LE_BITS_TO_4BYTE(__rx_desc + 0x0C, 29, 1)

#define GET_RX_DESC_RXPAYLOAD_MATCH(__rx_desc)                                 \
	LE_BITS_TO_4BYTE(__rx_desc + 0x0C, 28, 1)
#define GET_RX_DESC_RXPAYLOAD_ID(__rx_desc)                                    \
	LE_BITS_TO_4BYTE(__rx_desc + 0x0C, 24, 4)

#define GET_RX_DESC_DMA_AGG_NUM(__rx_desc)                                     \
	LE_BITS_TO_4BYTE(__rx_desc + 0x0C, 16, 8)
#define GET_RX_DESC_BSSID_FIT_1_0(__rx_desc)                                   \
	LE_BITS_TO_4BYTE(__rx_desc + 0x0C, 12, 2)
#define GET_RX_DESC_EOSP(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x0C, 11, 1)
#define GET_RX_DESC_HTC(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x0C, 10, 1)

#define GET_RX_DESC_BSSID_FIT_4_2(__rx_desc)                                   \
	LE_BITS_TO_4BYTE(__rx_desc + 0x0C, 7, 3)

#define GET_RX_DESC_RX_RATE(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x0C, 0, 7)

/*RXDESC_WORD4*/

#define GET_RX_DESC_A1_FIT(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x10, 24, 5)

#define GET_RX_DESC_MACID_RPT_BUFF(__rx_desc)                                  \
	LE_BITS_TO_4BYTE(__rx_desc + 0x10, 17, 7)
#define GET_RX_DESC_RX_PRE_NDP_VLD(__rx_desc)                                  \
	LE_BITS_TO_4BYTE(__rx_desc + 0x10, 16, 1)
#define GET_RX_DESC_RX_SCRAMBLER(__rx_desc)                                    \
	LE_BITS_TO_4BYTE(__rx_desc + 0x10, 9, 7)
#define GET_RX_DESC_RX_EOF(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x10, 8, 1)

#define GET_RX_DESC_PATTERN_IDX(__rx_desc)                                     \
	LE_BITS_TO_4BYTE(__rx_desc + 0x10, 0, 8)

/*RXDESC_WORD5*/

#define GET_RX_DESC_TSFL(__rx_desc) LE_BITS_TO_4BYTE(__rx_desc + 0x14, 0, 32)

#endif
