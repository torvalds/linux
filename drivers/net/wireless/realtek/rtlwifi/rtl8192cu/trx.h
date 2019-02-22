/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92CU_TRX_H__
#define __RTL92CU_TRX_H__

#define RTL92C_USB_BULK_IN_NUM			1
#define RTL92C_NUM_RX_URBS			8
#define RTL92C_NUM_TX_URBS			32

#define RTL92C_SIZE_MAX_RX_BUFFER		15360   /* 8192 */
#define RX_DRV_INFO_SIZE_UNIT			8

#define RTL_AGG_ON				1

enum usb_rx_agg_mode {
	USB_RX_AGG_DISABLE,
	USB_RX_AGG_DMA,
	USB_RX_AGG_USB,
	USB_RX_AGG_DMA_USB
};

#define TX_SELE_HQ				BIT(0)	/* High Queue */
#define TX_SELE_LQ				BIT(1)	/* Low Queue */
#define TX_SELE_NQ				BIT(2)	/* Normal Queue */

#define RTL_USB_TX_AGG_NUM_DESC			5

#define RTL_USB_RX_AGG_PAGE_NUM			4
#define RTL_USB_RX_AGG_PAGE_TIMEOUT		3

#define RTL_USB_RX_AGG_BLOCK_NUM		5
#define RTL_USB_RX_AGG_BLOCK_TIMEOUT		3

/*======================== rx status =========================================*/

struct rx_drv_info_92c {
	/*
	 * Driver info contain PHY status and other variabel size info
	 * PHY Status content as below
	 */

	/* DWORD 0 */
	u8 gain_trsw[4];

	/* DWORD 1 */
	u8 pwdb_all;
	u8 cfosho[4];

	/* DWORD 2 */
	u8 cfotail[4];

	/* DWORD 3 */
	s8 rxevm[2];
	s8 rxsnr[4];

	/* DWORD 4 */
	u8 pdsnr[2];

	/* DWORD 5 */
	u8 csi_current[2];
	u8 csi_target[2];

	/* DWORD 6 */
	u8 sigevm;
	u8 max_ex_pwr;
	u8 ex_intf_flag:1;
	u8 sgi_en:1;
	u8 rxsc:2;
	u8 reserve:4;
} __packed;

/* macros to read various fields in RX descriptor */

/* DWORD 0 */
#define GET_RX_DESC_PKT_LEN(__rxdesc)		\
	LE_BITS_TO_4BYTE((__rxdesc), 0, 14)
#define GET_RX_DESC_CRC32(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc, 14, 1)
#define GET_RX_DESC_ICV(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc, 15, 1)
#define GET_RX_DESC_DRVINFO_SIZE(__rxdesc)	\
	LE_BITS_TO_4BYTE(__rxdesc, 16, 4)
#define GET_RX_DESC_SECURITY(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc, 20, 3)
#define GET_RX_DESC_QOS(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc, 23, 1)
#define GET_RX_DESC_SHIFT(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc, 24, 2)
#define GET_RX_DESC_PHY_STATUS(__rxdesc)	\
	LE_BITS_TO_4BYTE(__rxdesc, 26, 1)
#define GET_RX_DESC_SWDEC(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc, 27, 1)
#define GET_RX_DESC_LAST_SEG(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc, 28, 1)
#define GET_RX_DESC_FIRST_SEG(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc, 29, 1)
#define GET_RX_DESC_EOR(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc, 30, 1)
#define GET_RX_DESC_OWN(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc, 31, 1)

/* DWORD 1 */
#define GET_RX_DESC_MACID(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 4, 0, 5)
#define GET_RX_DESC_TID(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 4, 5, 4)
#define GET_RX_DESC_PAGGR(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 4, 14, 1)
#define GET_RX_DESC_FAGGR(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 4, 15, 1)
#define GET_RX_DESC_A1_FIT(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 4, 16, 4)
#define GET_RX_DESC_A2_FIT(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 4, 20, 4)
#define GET_RX_DESC_PAM(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 4, 24, 1)
#define GET_RX_DESC_PWR(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 4, 25, 1)
#define GET_RX_DESC_MORE_DATA(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 4, 26, 1)
#define GET_RX_DESC_MORE_FRAG(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 4, 27, 1)
#define GET_RX_DESC_TYPE(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 4, 28, 2)
#define GET_RX_DESC_MC(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 4, 30, 1)
#define GET_RX_DESC_BC(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 4, 31, 1)

/* DWORD 2 */
#define GET_RX_DESC_SEQ(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 8, 0, 12)
#define GET_RX_DESC_FRAG(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 8, 12, 4)
#define GET_RX_DESC_USB_AGG_PKTNUM(__rxdesc)	\
	LE_BITS_TO_4BYTE(__rxdesc + 8, 16, 8)
#define GET_RX_DESC_NEXT_IND(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 8, 30, 1)

/* DWORD 3 */
#define GET_RX_DESC_RX_MCS(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 12, 0, 6)
#define GET_RX_DESC_RX_HT(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 12, 6, 1)
#define GET_RX_DESC_AMSDU(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 12, 7, 1)
#define GET_RX_DESC_SPLCP(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 12, 8, 1)
#define GET_RX_DESC_BW(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 12, 9, 1)
#define GET_RX_DESC_HTC(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 12, 10, 1)
#define GET_RX_DESC_TCP_CHK_RPT(__rxdesc)	\
	LE_BITS_TO_4BYTE(__rxdesc + 12, 11, 1)
#define GET_RX_DESC_IP_CHK_RPT(__rxdesc)	\
	LE_BITS_TO_4BYTE(__rxdesc + 12, 12, 1)
#define GET_RX_DESC_TCP_CHK_VALID(__rxdesc)	\
	LE_BITS_TO_4BYTE(__rxdesc + 12, 13, 1)
#define GET_RX_DESC_HWPC_ERR(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 12, 14, 1)
#define GET_RX_DESC_HWPC_IND(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 12, 15, 1)
#define GET_RX_DESC_IV0(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 12, 16, 16)

/* DWORD 4 */
#define GET_RX_DESC_IV1(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 16, 0, 32)

/* DWORD 5 */
#define GET_RX_DESC_TSFL(__rxdesc)		\
	LE_BITS_TO_4BYTE(__rxdesc + 20, 0, 32)

/*======================= tx desc ============================================*/

/* macros to set various fields in TX descriptor */

/* Dword 0 */
#define SET_TX_DESC_PKT_SIZE(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc, 0, 16, __value)
#define SET_TX_DESC_OFFSET(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc, 16, 8, __value)
#define SET_TX_DESC_BMC(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc, 24, 1, __value)
#define SET_TX_DESC_HTC(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc, 25, 1, __value)
#define SET_TX_DESC_LAST_SEG(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc, 26, 1, __value)
#define SET_TX_DESC_FIRST_SEG(__txdesc, __value)	\
	 SET_BITS_TO_LE_4BYTE(__txdesc, 27, 1, __value)
#define SET_TX_DESC_LINIP(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc, 28, 1, __value)
#define SET_TX_DESC_NO_ACM(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc, 29, 1, __value)
#define SET_TX_DESC_GF(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc, 30, 1, __value)
#define SET_TX_DESC_OWN(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc, 31, 1, __value)


/* Dword 1 */
#define SET_TX_DESC_MACID(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 4, 0, 5, __value)
#define SET_TX_DESC_AGG_ENABLE(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 4, 5, 1, __value)
#define SET_TX_DESC_AGG_BREAK(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 4, 6, 1, __value)
#define SET_TX_DESC_RDG_ENABLE(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 4, 7, 1, __value)
#define SET_TX_DESC_QUEUE_SEL(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 4, 8, 5, __value)
#define SET_TX_DESC_RDG_NAV_EXT(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 4, 13, 1, __value)
#define SET_TX_DESC_LSIG_TXOP_EN(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 4, 14, 1, __value)
#define SET_TX_DESC_PIFS(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 4, 15, 1, __value)
#define SET_TX_DESC_RATE_ID(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 4, 16, 4, __value)
#define SET_TX_DESC_RA_BRSR_ID(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 4, 16, 4, __value)
#define SET_TX_DESC_NAV_USE_HDR(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 4, 20, 1, __value)
#define SET_TX_DESC_EN_DESC_ID(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 4, 21, 1, __value)
#define SET_TX_DESC_SEC_TYPE(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 4, 22, 2, __value)
#define SET_TX_DESC_PKT_OFFSET(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 4, 26, 5, __value)

/* Dword 2 */
#define SET_TX_DESC_RTS_RC(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 8, 0, 6, __value)
#define SET_TX_DESC_DATA_RC(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 8, 6, 6, __value)
#define SET_TX_DESC_BAR_RTY_TH(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 8, 14, 2, __value)
#define SET_TX_DESC_MORE_FRAG(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 8, 17, 1, __value)
#define SET_TX_DESC_RAW(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 8, 18, 1, __value)
#define SET_TX_DESC_CCX(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 8, 19, 1, __value)
#define SET_TX_DESC_AMPDU_DENSITY(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 8, 20, 3, __value)
#define SET_TX_DESC_ANTSEL_A(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 8, 24, 1, __value)
#define SET_TX_DESC_ANTSEL_B(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 8, 25, 1, __value)
#define SET_TX_DESC_TX_ANT_CCK(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 8, 26, 2, __value)
#define SET_TX_DESC_TX_ANTL(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 8, 28, 2, __value)
#define SET_TX_DESC_TX_ANT_HT(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 8, 30, 2, __value)

/* Dword 3 */
#define SET_TX_DESC_NEXT_HEAP_PAGE(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 12, 0, 8, __value)
#define SET_TX_DESC_TAIL_PAGE(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 12, 8, 8, __value)
#define SET_TX_DESC_SEQ(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 12, 16, 12, __value)
#define SET_TX_DESC_PKT_ID(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 12, 28, 4, __value)

/* Dword 4 */
#define SET_TX_DESC_RTS_RATE(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 0, 5, __value)
#define SET_TX_DESC_AP_DCFE(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 5, 1, __value)
#define SET_TX_DESC_QOS(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 6, 1, __value)
#define SET_TX_DESC_HWSEQ_EN(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 7, 1, __value)
#define SET_TX_DESC_USE_RATE(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 8, 1, __value)
#define SET_TX_DESC_DISABLE_RTS_FB(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 9, 1, __value)
#define SET_TX_DESC_DISABLE_FB(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 10, 1, __value)
#define SET_TX_DESC_CTS2SELF(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 11, 1, __value)
#define SET_TX_DESC_RTS_ENABLE(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 12, 1, __value)
#define SET_TX_DESC_HW_RTS_ENABLE(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 13, 1, __value)
#define SET_TX_DESC_WAIT_DCTS(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 18, 1, __value)
#define SET_TX_DESC_CTS2AP_EN(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 19, 1, __value)
#define SET_TX_DESC_DATA_SC(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 20, 2, __value)
#define SET_TX_DESC_DATA_STBC(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 22, 2, __value)
#define SET_TX_DESC_DATA_SHORT(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 24, 1, __value)
#define SET_TX_DESC_DATA_BW(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 25, 1, __value)
#define SET_TX_DESC_RTS_SHORT(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 26, 1, __value)
#define SET_TX_DESC_RTS_BW(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 27, 1, __value)
#define SET_TX_DESC_RTS_SC(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 28, 2, __value)
#define SET_TX_DESC_RTS_STBC(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 16, 30, 2, __value)

/* Dword 5 */
#define SET_TX_DESC_TX_RATE(__pdesc, __val)		\
	SET_BITS_TO_LE_4BYTE(__pdesc + 20, 0, 6, __val)
#define SET_TX_DESC_DATA_SHORTGI(__pdesc, __val)	\
	SET_BITS_TO_LE_4BYTE(__pdesc + 20, 6, 1, __val)
#define SET_TX_DESC_CCX_TAG(__pdesc, __val)		\
	SET_BITS_TO_LE_4BYTE(__pdesc + 20, 7, 1, __val)
#define SET_TX_DESC_DATA_RATE_FB_LIMIT(__txdesc, __value) \
	SET_BITS_TO_LE_4BYTE(__txdesc + 20, 8, 5, __value)
#define SET_TX_DESC_RTS_RATE_FB_LIMIT(__txdesc, __value) \
	SET_BITS_TO_LE_4BYTE(__txdesc + 20, 13, 4, __value)
#define SET_TX_DESC_RETRY_LIMIT_ENABLE(__txdesc, __value) \
	SET_BITS_TO_LE_4BYTE(__txdesc + 20, 17, 1, __value)
#define SET_TX_DESC_DATA_RETRY_LIMIT(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 20, 18, 6, __value)
#define SET_TX_DESC_USB_TXAGG_NUM(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 20, 24, 8, __value)

/* Dword 6 */
#define SET_TX_DESC_TXAGC_A(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 24, 0, 5, __value)
#define SET_TX_DESC_TXAGC_B(__txdesc, __value)		\
	SET_BITS_TO_LE_4BYTE(__txdesc + 24, 5, 5, __value)
#define SET_TX_DESC_USB_MAX_LEN(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 24, 10, 1, __value)
#define SET_TX_DESC_MAX_AGG_NUM(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 24, 11, 5, __value)
#define SET_TX_DESC_MCSG1_MAX_LEN(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 24, 16, 4, __value)
#define SET_TX_DESC_MCSG2_MAX_LEN(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 24, 20, 4, __value)
#define SET_TX_DESC_MCSG3_MAX_LEN(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 24, 24, 4, __value)
#define SET_TX_DESC_MCSG7_MAX_LEN(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 24, 28, 4, __value)

/* Dword 7 */
#define SET_TX_DESC_TX_DESC_CHECKSUM(__txdesc, __value) \
	SET_BITS_TO_LE_4BYTE(__txdesc + 28, 0, 16, __value)
#define SET_TX_DESC_MCSG4_MAX_LEN(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 28, 16, 4, __value)
#define SET_TX_DESC_MCSG5_MAX_LEN(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 28, 20, 4, __value)
#define SET_TX_DESC_MCSG6_MAX_LEN(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 28, 24, 4, __value)
#define SET_TX_DESC_MCSG15_MAX_LEN(__txdesc, __value)	\
	SET_BITS_TO_LE_4BYTE(__txdesc + 28, 28, 4, __value)


int  rtl8192cu_endpoint_mapping(struct ieee80211_hw *hw);
u16 rtl8192cu_mq_to_hwq(__le16 fc, u16 mac80211_queue_index);
bool rtl92cu_rx_query_desc(struct ieee80211_hw *hw,
			   struct rtl_stats *stats,
			   struct ieee80211_rx_status *rx_status,
			   u8 *p_desc, struct sk_buff *skb);
void  rtl8192cu_rx_hdl(struct ieee80211_hw *hw, struct sk_buff * skb);
void rtl8192c_tx_cleanup(struct ieee80211_hw *hw, struct sk_buff  *skb);
int rtl8192c_tx_post_hdl(struct ieee80211_hw *hw, struct urb *urb,
			 struct sk_buff *skb);
struct sk_buff *rtl8192c_tx_aggregate_hdl(struct ieee80211_hw *,
					   struct sk_buff_head *);
void rtl92cu_tx_fill_desc(struct ieee80211_hw *hw,
			  struct ieee80211_hdr *hdr, u8 *pdesc_tx,
			  u8 *pbd_desc_tx, struct ieee80211_tx_info *info,
			  struct ieee80211_sta *sta,
			  struct sk_buff *skb,
			  u8 queue_index,
			  struct rtl_tcb_desc *tcb_desc);
void rtl92cu_fill_fake_txdesc(struct ieee80211_hw *hw, u8 *pdesc,
			       u32 buffer_len, bool ispspoll);
void rtl92cu_tx_fill_cmddesc(struct ieee80211_hw *hw,
			     u8 *pdesc, bool b_firstseg,
			     bool b_lastseg, struct sk_buff *skb);

#endif
