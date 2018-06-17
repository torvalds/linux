/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL8822B_TRX_H__
#define __RTL8822B_TRX_H__

#include "../halmac/halmac_tx_desc_nic.h"
#include "../halmac/halmac_rx_desc_nic.h"

#define TX_DESC_SIZE	64

#define RX_DRV_INFO_SIZE_UNIT	8

#define TX_DESC_NEXT_DESC_OFFSET	48
#define USB_HWDESC_HEADER_LEN	48

#define RX_DESC_SIZE	24
#define MAX_RECEIVE_BUFFER_SIZE	8192

#define SET_EARLYMODE_PKTNUM(__paddr, __val)                                   \
	SET_BITS_TO_LE_4BYTE(__paddr, 0, 4, __val)
#define SET_EARLYMODE_LEN0(__paddr, __val)                                     \
	SET_BITS_TO_LE_4BYTE(__paddr, 4, 15, __val)
#define SET_EARLYMODE_LEN1(__paddr, __val)                                     \
	SET_BITS_TO_LE_4BYTE(__paddr, 16, 2, __val)
#define SET_EARLYMODE_LEN1_1(__paddr, __val)                                   \
	SET_BITS_TO_LE_4BYTE(__paddr, 19, 13, __val)
#define SET_EARLYMODE_LEN1_2(__paddr, __val)                                   \
	SET_BITS_TO_LE_4BYTE(__paddr + 4, 0, 2, __val)
#define SET_EARLYMODE_LEN2(__paddr, __val)                                     \
	SET_BITS_TO_LE_4BYTE(__paddr + 4, 2, 15, __val)
#define SET_EARLYMODE_LEN2_1(__paddr, __val)                                   \
	SET_BITS_TO_LE_4BYTE(__paddr, 2, 4, __val)
#define SET_EARLYMODE_LEN2_2(__paddr, __val)                                   \
	SET_BITS_TO_LE_4BYTE(__paddr + 4, 0, 8, __val)
#define SET_EARLYMODE_LEN3(__paddr, __val)                                     \
	SET_BITS_TO_LE_4BYTE(__paddr + 4, 17, 15, __val)
#define SET_EARLYMODE_LEN4(__paddr, __val)                                     \
	SET_BITS_TO_LE_4BYTE(__paddr + 4, 20, 12, __val)

/* TX/RX buffer descriptor */

/* for Txfilldescroptor8822be, fill the desc content. */
#define SET_TXBUFFER_DESC_LEN_WITH_OFFSET(__pdesc, __offset, __val)            \
	SET_BITS_TO_LE_4BYTE((__pdesc) + ((__offset) * 16), 0, 16, __val)
#define SET_TXBUFFER_DESC_AMSDU_WITH_OFFSET(__pdesc, __offset, __val)          \
	SET_BITS_TO_LE_4BYTE((__pdesc) + ((__offset) * 16), 31, 1, __val)
#define SET_TXBUFFER_DESC_ADD_LOW_WITH_OFFSET(__pdesc, __offset, __val)        \
	SET_BITS_TO_LE_4BYTE((__pdesc) + ((__offset) * 16) + 4, 0, 32, __val)
#define SET_TXBUFFER_DESC_ADD_HIGH_WITH_OFFSET(pbd, off, val, dma64)	       \
	(dma64 ? SET_BITS_TO_LE_4BYTE((pbd) + ((off) * 16) + 8, 0, 32, val) : 0)
#define GET_TXBUFFER_DESC_ADDR_LOW(__pdesc, __offset)                          \
	LE_BITS_TO_4BYTE((__pdesc) + ((__offset) * 16) + 4, 0, 32)
#define GET_TXBUFFER_DESC_ADDR_HIGH(pbd, off, dma64)			       \
	(dma64 ? LE_BITS_TO_4BYTE((pbd) + ((off) * 16) + 8, 0, 32) : 0)

/* Dword 0 */
#define SET_TX_BUFF_DESC_LEN_0(__pdesc, __val)                                 \
	SET_BITS_TO_LE_4BYTE(__pdesc, 0, 14, __val)
#define SET_TX_BUFF_DESC_PSB(__pdesc, __val)                                   \
	SET_BITS_TO_LE_4BYTE(__pdesc, 16, 15, __val)
#define SET_TX_BUFF_DESC_OWN(__pdesc, __val)                                   \
	SET_BITS_TO_LE_4BYTE(__pdesc, 31, 1, __val)

/* Dword 1 */
#define SET_TX_BUFF_DESC_ADDR_LOW_0(__pdesc, __val)                            \
	SET_BITS_TO_LE_4BYTE((__pdesc) + 4, 0, 32, __val)
/* Dword 2 */
#define SET_TX_BUFF_DESC_ADDR_HIGH_0(bdesc, val, dma64)			       \
	SET_TXBUFFER_DESC_ADD_HIGH_WITH_OFFSET(bdesc, 0, val, dma64)
/* Dword 3 / RESERVED 0 */

/* RX buffer  */

/* DWORD 0 */
#define SET_RX_BUFFER_DESC_DATA_LENGTH(__rx_status_desc, __val)                \
	SET_BITS_TO_LE_4BYTE(__rx_status_desc, 0, 14, __val)
#define SET_RX_BUFFER_DESC_LS(__rx_status_desc, __val)                         \
	SET_BITS_TO_LE_4BYTE(__rx_status_desc, 15, 1, __val)
#define SET_RX_BUFFER_DESC_FS(__rx_status_desc, __val)                         \
	SET_BITS_TO_LE_4BYTE(__rx_status_desc, 16, 1, __val)
#define SET_RX_BUFFER_DESC_TOTAL_LENGTH(__rx_status_desc, __val)               \
	SET_BITS_TO_LE_4BYTE(__rx_status_desc, 16, 15, __val)

#define GET_RX_BUFFER_DESC_OWN(__rx_status_desc)                               \
	LE_BITS_TO_4BYTE(__rx_status_desc, 31, 1)
#define GET_RX_BUFFER_DESC_LS(__rx_status_desc)                                \
	LE_BITS_TO_4BYTE(__rx_status_desc, 15, 1)
#define GET_RX_BUFFER_DESC_FS(__rx_status_desc)                                \
	LE_BITS_TO_4BYTE(__rx_status_desc, 16, 1)
#define GET_RX_BUFFER_DESC_TOTAL_LENGTH(__rx_status_desc)                      \
	LE_BITS_TO_4BYTE(__rx_status_desc, 16, 15)

/* DWORD 1 */
#define SET_RX_BUFFER_PHYSICAL_LOW(__rx_status_desc, __val)                    \
	SET_BITS_TO_LE_4BYTE(__rx_status_desc + 4, 0, 32, __val)

/* DWORD 2 */
#define SET_RX_BUFFER_PHYSICAL_HIGH(__rx_status_desc, __val, dma64)            \
	(dma64 ? SET_BITS_TO_LE_4BYTE((__rx_status_desc) + 8, 0, 32, __val) : 0)

#define CLEAR_PCI_TX_DESC_CONTENT(__pdesc, _size)                              \
	do {                                                                   \
		if (_size > TX_DESC_NEXT_DESC_OFFSET)                          \
			memset(__pdesc, 0, TX_DESC_NEXT_DESC_OFFSET);          \
		else                                                           \
			memset(__pdesc, 0, _size);                             \
	} while (0)

void rtl8822be_rx_check_dma_ok(struct ieee80211_hw *hw, u8 *header_desc,
			       u8 queue_index);
u16 rtl8822be_rx_desc_buff_remained_cnt(struct ieee80211_hw *hw,
					u8 queue_index);
u16 rtl8822be_get_available_desc(struct ieee80211_hw *hw, u8 queue_index);
void rtl8822be_pre_fill_tx_bd_desc(struct ieee80211_hw *hw, u8 *tx_bd_desc,
				   u8 *desc, u8 queue_index,
				   struct sk_buff *skb, dma_addr_t addr);

void rtl8822be_tx_fill_desc(struct ieee80211_hw *hw, struct ieee80211_hdr *hdr,
			    u8 *pdesc_tx, u8 *pbd_desc_tx,
			    struct ieee80211_tx_info *info,
			    struct ieee80211_sta *sta, struct sk_buff *skb,
			    u8 hw_queue, struct rtl_tcb_desc *ptcb_desc);
void rtl8822be_tx_fill_special_desc(struct ieee80211_hw *hw, u8 *pdesc,
				    u8 *pbd_desc, struct sk_buff *skb,
				    u8 hw_queue);
bool rtl8822be_rx_query_desc(struct ieee80211_hw *hw, struct rtl_stats *status,
			     struct ieee80211_rx_status *rx_status, u8 *pdesc,
			     struct sk_buff *skb);
void rtl8822be_set_desc(struct ieee80211_hw *hw, u8 *pdesc, bool istx,
			u8 desc_name, u8 *val);
u64 rtl8822be_get_desc(struct ieee80211_hw *hw,
		       u8 *pdesc, bool istx, u8 desc_name);
bool rtl8822be_is_tx_desc_closed(struct ieee80211_hw *hw, u8 hw_queue,
				 u16 index);
void rtl8822be_tx_polling(struct ieee80211_hw *hw, u8 hw_queue);
void rtl8822be_tx_fill_cmddesc(struct ieee80211_hw *hw, u8 *pdesc,
			       bool firstseg, bool lastseg,
			       struct sk_buff *skb);
u32 rtl8822be_rx_command_packet(struct ieee80211_hw *hw,
				const struct rtl_stats *status,
				struct sk_buff *skb);
#endif
